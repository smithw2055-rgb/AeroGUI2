#include "aero/host/HostDrivers.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "aero/controls/Controls.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

namespace {

constexpr std::size_t kMaxImeCommittedHistoryBytes = 4096u;

void AppendBoundedImeCommittedText(std::string& ioHistory, std::string_view committed) {
    if (committed.empty()) {
        return;
    }
    if (committed.size() >= kMaxImeCommittedHistoryBytes) {
        std::size_t start = committed.size() - kMaxImeCommittedHistoryBytes;
        while (start < committed.size() &&
               (static_cast<std::uint8_t>(committed[start]) & 0xC0u) == 0x80u) {
            ++start;
        }
        ioHistory.assign(committed.substr(start));
        return;
    }
    if (ioHistory.size() + committed.size() > kMaxImeCommittedHistoryBytes) {
        std::size_t drop = (ioHistory.size() + committed.size()) - kMaxImeCommittedHistoryBytes;
        while (drop < ioHistory.size() &&
               (static_cast<std::uint8_t>(ioHistory[drop]) & 0xC0u) == 0x80u) {
            ++drop;
        }
        ioHistory.erase(0u, drop);
    }
    ioHistory.append(committed);
}

AriaShadowNode BuildAriaNodeRecursive(const UIElement* elem) {
    AriaShadowNode node{};
    if (!elem) {
        return node;
    }
    node.bounds = elem->FinalRect();
    node.label = elem->Name();

    if (const auto* rb = elem->DynamicCast<RadioButton>()) {
        node.role = "radio";
        node.value = rb->IsChecked() ? "true" : "false";
        if (node.label.empty()) node.label = rb->GetContentText();
        node.focusable = rb->CanReceiveFocus();
    } else if (const auto* cb = elem->DynamicCast<CheckBox>()) {
        node.role = "checkbox";
        node.value = cb->IsChecked() ? "true" : "false";
        if (node.label.empty()) node.label = cb->GetContentText();
        node.focusable = cb->CanReceiveFocus();
    } else if (const auto* slider = elem->DynamicCast<Slider>()) {
        node.role = "slider";
        node.value = rs_rt::valueToString(MakeFloat(slider->GetSliderValue()));
        node.focusable = slider->CanReceiveFocus();
    } else if (const auto* tbox = elem->DynamicCast<TextBox>()) {
        node.role = "textbox";
        node.value = tbox->GetDisplayString();
        node.focusable = tbox->CanReceiveFocus();
    } else if (const auto* combo = elem->DynamicCast<ComboBox>()) {
        node.role = "combobox";
        node.value = combo->GetSelectedItemText();
        node.focusable = combo->CanReceiveFocus();
    } else if (const auto* btn = elem->DynamicCast<Button>()) {
        node.role = "button";
        node.value = btn->GetContentText();
        node.focusable = btn->CanReceiveFocus();
    } else if (const auto* tb = elem->DynamicCast<TextBlock>()) {
        node.role = "text";
        node.value = tb->GetText();
        node.focusable = elem->CanReceiveFocus();
    } else if (elem->IsA<Window>()) {
        node.role = "dialog";
        node.focusable = true;
    } else {
        node.role = "group";
        node.focusable = elem->CanReceiveFocus();
    }

    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            node.children.push_back(BuildAriaNodeRecursive(c));
        }
    }
    return node;
}

} // namespace

// ============================================================================
// 1. EmbeddedGameHost
// ============================================================================

EmbeddedGameHost::EmbeddedGameHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree,
    std::shared_ptr<UiResourceContext> resourceContext) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree)
    , resourceContext_(std::move(resourceContext)) {}

void EmbeddedGameHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUiResourceContext resourceScope(resourceContext_);

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if (inputRouter_ && animationEngine_ && !inputRouter_->GetAnimationEngine()) {
        inputRouter_->SetAnimationEngine(animationEngine_);
    }
    if (bindingEngine_ && animationEngine_ && !bindingEngine_->GetAnimationEngine()) {
        bindingEngine_->SetAnimationEngine(animationEngine_);
    }

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_) {
        telemetry_.inputEventsDispatched += inputRouter_->FlushQueuedEvents();
    }

    if ((activePhaseMask & UiPhase_TrackABinding) != 0u && bindingEngine_) {
        telemetry_.trackAUpdates += bindingEngine_->FlushTrackA();
    }

    if ((activePhaseMask & UiPhase_TrackBBinding) != 0u && bindingEngine_) {
        if (renderTree_ && (activePhaseMask & UiPhase_Layout) == 0u) {
            telemetry_.trackBUpdates += renderTree_->BatchUpdateFromEcsTrackB(*bindingEngine_);
        } else {
            telemetry_.trackBUpdates += bindingEngine_->FlushTrackB();
        }
    }

    if ((activePhaseMask & UiPhase_MotionStateMachine) != 0u && animationEngine_) {
        telemetry_.stateMachineTransitions += animationEngine_->EvaluateStateMachines(dt);
    }

    if ((activePhaseMask & UiPhase_Style) != 0u) {
        telemetry_.stylePassesRun += 1u;
        if (styleEngine_) {
            if (bindingEngine_ && !styleEngine_->GetBindingEngine()) {
                styleEngine_->SetBindingEngine(bindingEngine_);
            }
            telemetry_.styleTriggerChanges += styleEngine_->EvaluateAndApply();
        }
    }

    if ((activePhaseMask & UiPhase_Animation) != 0u && animationEngine_) {
        telemetry_.animationTrackUpdates += animationEngine_->Tick(dt);
    }

    if ((activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.layoutPassesRun += 1u;
        if (rootVisual_ && viewportSize_ != lastLaidOutViewport_) {
            rootVisual_->InvalidateLayout();
            lastLaidOutViewport_ = viewportSize_;
        }
        if (layoutQueue_) {
            if (rootVisual_) {
                layoutQueue_->EnqueueDirtySubtree(rootVisual_);
            }
            (void)layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }

    if (renderTree_ && rootVisual_ &&
        (activePhaseMask & UiPhase_RenderRecord) != 0u &&
        (activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.renderNodesSynced += renderTree_->SyncVisualTree(rootVisual_);
    }
}

ZeroCopySubmissionStats EmbeddedGameHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    ScopedUiResourceContext resourceScope(resourceContext_);
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 2. DesktopEventHost
// ============================================================================

DesktopEventHost::DesktopEventHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree,
    std::shared_ptr<UiResourceContext> resourceContext) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree)
    , resourceContext_(std::move(resourceContext)) {
    wakeRegisteredRegistry_ = resourceContext_
        ? &resourceContext_->Templates()
        : &AxbTemplateRegistry::Instance();
    wakeRegisteredRegistry_->RegisterHostWakeListener(
        this,
        [](void* ud) noexcept {
            static_cast<DesktopEventHost*>(ud)->NotifyDirtyLayout();
        },
        this);
}

DesktopEventHost::~DesktopEventHost() {
    if (wakeRegisteredRegistry_ && AxbTemplateRegistry::IsLiveRegistry(wakeRegisteredRegistry_)) {
        wakeRegisteredRegistry_->UnregisterHostWakeListener(this);
    }
    wakeRegisteredRegistry_ = nullptr;
}

void DesktopEventHost::SetResourceContext(std::shared_ptr<UiResourceContext> context) {
    if (resourceContext_ == context) {
        return;
    }
    if (wakeRegisteredRegistry_ && AxbTemplateRegistry::IsLiveRegistry(wakeRegisteredRegistry_)) {
        wakeRegisteredRegistry_->UnregisterHostWakeListener(this);
    }
    resourceContext_ = std::move(context);
    wakeRegisteredRegistry_ = resourceContext_
        ? &resourceContext_->Templates()
        : &AxbTemplateRegistry::Instance();
    wakeRegisteredRegistry_->RegisterHostWakeListener(
        this,
        [](void* ud) noexcept {
            static_cast<DesktopEventHost*>(ud)->NotifyDirtyLayout();
        },
        this);
}

void DesktopEventHost::SetStateFlags(
    bool hasActiveAnimations,
    bool hasDirtyObjects,
    bool hasDirtyLayout,
    bool hasPendingInputEvents) noexcept {
    hasActiveAnimations_ = hasActiveAnimations;
    hasDirtyObjects_ = hasDirtyObjects;
    hasDirtyLayout_ = hasDirtyLayout;
    hasPendingInputEvents_ = hasPendingInputEvents;
    if (hasActiveAnimations || hasDirtyObjects || hasDirtyLayout || hasPendingInputEvents) {
        TriggerWake();
    }
}

bool DesktopEventHost::CanSleepIdle() const noexcept {
    const bool rootLayoutDirty = rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) != 0u;
    const bool rootRenderDirty = renderTree_ && rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (RenderDirty | SubtreeRenderDirty)) != 0u;
    const bool rootVisualDirty = rootLayoutDirty || rootRenderDirty;
    const bool routerPending = inputRouter_ && inputRouter_->HasPendingEvents();
    const bool bindingDirty = bindingEngine_ && bindingEngine_->HasPendingDirtyObjects();
    const bool animEngineActive = animationEngine_ && animationEngine_->HasActiveAnimations();
    return !hasActiveAnimations_ &&
           !animEngineActive &&
           !hasDirtyObjects_ &&
           !bindingDirty &&
           !hasDirtyLayout_ &&
           !hasPendingInputEvents_ &&
           !routerPending &&
           !rootVisualDirty;
}

void DesktopEventHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUiResourceContext resourceScope(resourceContext_);

    if (CanSleepIdle()) {
        // 0.0% CPU / 0.0% GPU polling when idle
        return;
    }

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if (inputRouter_ && animationEngine_ && !inputRouter_->GetAnimationEngine()) {
        inputRouter_->SetAnimationEngine(animationEngine_);
    }
    if (bindingEngine_ && animationEngine_ && !bindingEngine_->GetAnimationEngine()) {
        bindingEngine_->SetAnimationEngine(animationEngine_);
    }

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_ && inputRouter_->HasPendingEvents()) {
        const std::size_t dispatched = inputRouter_->FlushQueuedEvents();
        telemetry_.inputEventsDispatched += dispatched;
        if (dispatched > 0u) {
            hasDirtyObjects_ = true;
            hasDirtyLayout_ = true;
        }
    }

    if ((hasDirtyObjects_ || (bindingEngine_ && bindingEngine_->HasPendingDirtyObjects())) &&
        bindingEngine_ && (activePhaseMask & UiPhase_TrackABinding) != 0u) {
        const std::size_t updated = bindingEngine_->FlushTrackA();
        telemetry_.trackAUpdates += updated;
        if (updated > 0) {
            hasDirtyLayout_ = true;
        }
    }

    if ((activePhaseMask & UiPhase_MotionStateMachine) != 0u && animationEngine_) {
        const std::size_t smChanged = animationEngine_->EvaluateStateMachines(dt);
        telemetry_.stateMachineTransitions += smChanged;
        if (smChanged > 0u) {
            hasDirtyLayout_ = true;
        }
    }

    if ((activePhaseMask & UiPhase_Style) != 0u && (styleEngine_ || hasDirtyObjects_ || hasDirtyLayout_)) {
        telemetry_.stylePassesRun += 1u;
        if (styleEngine_) {
            if (bindingEngine_ && !styleEngine_->GetBindingEngine()) {
                styleEngine_->SetBindingEngine(bindingEngine_);
            }
            const std::size_t stChanged = styleEngine_->EvaluateAndApply();
            telemetry_.styleTriggerChanges += stChanged;
            if (stChanged > 0u) {
                hasDirtyLayout_ = true;
            }
        }
    }

    if ((activePhaseMask & UiPhase_Animation) != 0u && animationEngine_ && animationEngine_->HasActiveAnimations()) {
        telemetry_.animationTrackUpdates += animationEngine_->Tick(dt);
    }

    const bool rootNeedsLayout = rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) != 0u;
    if ((activePhaseMask & UiPhase_Layout) != 0u && (hasDirtyLayout_ || rootNeedsLayout)) {
        telemetry_.layoutPassesRun += 1u;
        if (rootVisual_ && viewportSize_ != lastLaidOutViewport_) {
            rootVisual_->InvalidateLayout();
            lastLaidOutViewport_ = viewportSize_;
        }
        if (layoutQueue_) {
            if (rootVisual_) {
                layoutQueue_->EnqueueDirtySubtree(rootVisual_);
            }
            (void)layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }

    if (renderTree_ && rootVisual_ && (activePhaseMask & UiPhase_RenderRecord) != 0u) {
        telemetry_.renderNodesSynced += renderTree_->SyncVisualTree(rootVisual_);
    }

    // Clear transient dirty flags after processing; if animations are off, next check can sleep idle
    hasDirtyObjects_ = false;
    hasDirtyLayout_ = false;
    hasPendingInputEvents_ = false;
    if (animationEngine_) {
        hasActiveAnimations_ = animationEngine_->HasActiveAnimations();
    }
}

ZeroCopySubmissionStats DesktopEventHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    ScopedUiResourceContext resourceScope(resourceContext_);
    if (renderTree.DirtyPageCount() == 0 && CanSleepIdle()) {
        return ZeroCopySubmissionStats{};
    }
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 3. DesktopImeAdapter, WebImeAdapter, WebFontStreamProvider & WebCanvasHost
// ============================================================================

void DesktopImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    if (focusedElement_ != focusedElement) {
        if (isComposing_) {
            if (auto* prevBox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
                prevBox->SetText(compositionBaseText_);
                prevBox->SetCaretIndex(compositionBaseCaret_);
            }
        }
        isComposing_ = false;
        preeditText_.clear();
        committedText_.clear();
        caretOffset_ = 0u;
        if (const auto* tbox = focusedElement ? focusedElement->DynamicCast<TextBox>() : nullptr) {
            compositionBaseText_ = tbox->GetText();
            compositionBaseCaret_ = tbox->GetCaretIndex();
        } else {
            compositionBaseText_.clear();
            compositionBaseCaret_ = 0u;
        }
    }
    focusedElement_ = focusedElement;
    caretScreenRect_ = caretScreenRect;
    imeFocused_ = (focusedElement != nullptr);
    focusSyncCount_ += 1u;
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, imeFocused_, spotSyncUserData_);
    }
}

void DesktopImeAdapter::BlurTextInput() {
    if (isComposing_) {
        if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
            tbox->SetText(compositionBaseText_);
            tbox->SetCaretIndex(compositionBaseCaret_);
        }
    }
    focusedElement_ = nullptr;
    imeFocused_ = false;
    isComposing_ = false;
    preeditText_.clear();
    committedText_.clear();
    compositionBaseText_.clear();
    compositionBaseCaret_ = 0u;
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, false, spotSyncUserData_);
    }
}

void DesktopImeAdapter::OnCompositionStart() {
    isComposing_ = true;
    preeditText_.clear();
    caretOffset_ = 0u;
    if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
        compositionBaseText_ = tbox->GetText();
        compositionBaseCaret_ = tbox->GetCaretIndex();
        caretScreenRect_ = tbox->CaretScreenRect();
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
    }
}

void DesktopImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
        if (!isComposing_) {
            compositionBaseText_ = tbox->GetText();
            compositionBaseCaret_ = tbox->GetCaretIndex();
            caretScreenRect_ = tbox->CaretScreenRect();
        }
        isComposing_ = true;
        preeditText_.assign(preeditText);
        caretOffset_ = caretOffset;

        std::size_t bytePos = 0u;
        std::size_t cpCount = 0u;
        std::uint32_t cp = 0u;
        while (cpCount < compositionBaseCaret_ && DecodeNextUtf8CodePoint(compositionBaseText_, bytePos, cp)) {
            ++cpCount;
        }
        const std::string preview =
            compositionBaseText_.substr(0, bytePos) + preeditText_ + compositionBaseText_.substr(bytePos);
        tbox->SetText(preview);
        caretScreenRect_ = tbox->CaretScreenRect();
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
        return;
    }

    isComposing_ = true;
    preeditText_.assign(preeditText);
    caretOffset_ = caretOffset;
}

void DesktopImeAdapter::OnCompositionEnd(std::string_view committedText) {
    const bool wasComposing = isComposing_;
    isComposing_ = false;
    preeditText_.clear();
    AppendBoundedImeCommittedText(committedText_, committedText);
    caretOffset_ = 0u;

    if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
        if (wasComposing) {
            tbox->SetText(compositionBaseText_);
            tbox->SetCaretIndex(compositionBaseCaret_);
        }
        (void)tbox->InsertText(committedText);
        compositionBaseText_ = tbox->GetText();
        compositionBaseCaret_ = tbox->GetCaretIndex();
        caretScreenRect_ = tbox->CaretScreenRect();
        focusSyncCount_ += 1u;
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
    }
}

void WebImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    if (focusedElement_ != focusedElement) {
        if (isComposing_) {
            if (auto* prevBox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
                prevBox->SetText(compositionBaseText_);
                prevBox->SetCaretIndex(compositionBaseCaret_);
            }
        }
        isComposing_ = false;
        preeditText_.clear();
        committedText_.clear();
        caretOffset_ = 0;
        if (const auto* tbox = focusedElement ? focusedElement->DynamicCast<TextBox>() : nullptr) {
            compositionBaseText_ = tbox->GetText();
            compositionBaseCaret_ = tbox->GetCaretIndex();
        } else {
            compositionBaseText_.clear();
            compositionBaseCaret_ = 0u;
        }
    }
    focusedElement_ = focusedElement;
    baseCaretRect_ = caretScreenRect;
    overlayDomRect_ = caretScreenRect;
    imeFocused_ = (focusedElement != nullptr);
}

void WebImeAdapter::BlurTextInput() {
    if (isComposing_) {
        if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
            tbox->SetText(compositionBaseText_);
            tbox->SetCaretIndex(compositionBaseCaret_);
        }
    }
    focusedElement_ = nullptr;
    imeFocused_ = false;
    isComposing_ = false;
    preeditText_.clear();
    committedText_.clear();
    compositionBaseText_.clear();
    compositionBaseCaret_ = 0u;
}

void WebImeAdapter::OnCompositionStart() {
    isComposing_ = true;
    preeditText_.clear();
    caretOffset_ = 0;
    if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
        compositionBaseText_ = tbox->GetText();
        compositionBaseCaret_ = tbox->GetCaretIndex();
        baseCaretRect_ = tbox->CaretScreenRect();
        overlayDomRect_ = baseCaretRect_;
    } else {
        overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(committedText_.size()) * 2.0f;
    }
}

void WebImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    if (auto* tbox = focusedElement_ ? focusedElement_->DynamicCast<TextBox>() : nullptr) {
        if (!isComposing_) {
            compositionBaseText_ = tbox->GetText();
            compositionBaseCaret_ = tbox->GetCaretIndex();
            baseCaretRect_ = tbox->CaretScreenRect();
        }
        isComposing_ = true;
        preeditText_.assign(preeditText);
        caretOffset_ = caretOffset;
        overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(caretOffset) * 2.0f;

        std::size_t bytePos = 0u;
        std::size_t cpCount = 0u;
        std::uint32_t cp = 0u;
        while (cpCount < compositionBaseCaret_ && DecodeNextUtf8CodePoint(compositionBaseText_, bytePos, cp)) {
            ++cpCount;
        }
        const std::string preview =
            compositionBaseText_.substr(0, bytePos) + preeditText_ + compositionBaseText_.substr(bytePos);
        tbox->SetText(preview);
        return;
    }

    isComposing_ = true;
    preeditText_.assign(preeditText);
    caretOffset_ = caretOffset;
    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(committedText_.size() + caretOffset) * 2.0f;
    if (focusedElement_) {
        focusedElement_->SetValue(
            PropId_Text,
            value::FromString(committedText_ + preeditText_),
            ValueSourceRank::Local);
    }
}

void WebImeAdapter::OnCompositionEnd(std::string_view committedText) {
    const bool wasComposing = isComposing_;
    isComposing_ = false;
    preeditText_.clear();
    AppendBoundedImeCommittedText(committedText_, committedText);
    caretOffset_ = 0;
    if (focusedElement_) {
        if (auto* tbox = focusedElement_->DynamicCast<TextBox>()) {
            if (wasComposing) {
                tbox->SetText(compositionBaseText_);
                tbox->SetCaretIndex(compositionBaseCaret_);
            }
            (void)tbox->InsertText(committedText);
            compositionBaseText_ = tbox->GetText();
            compositionBaseCaret_ = tbox->GetCaretIndex();
            baseCaretRect_ = tbox->CaretScreenRect();
            overlayDomRect_ = baseCaretRect_;
            return;
        }
        overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(committedText_.size()) * 2.0f;
        focusedElement_->SetValue(
            PropId_Text,
            value::FromString(committedText_),
            ValueSourceRank::Local);
        (void)focusedElement_->NotifyTwoWayPropertyMutated(PropId_Text);
    } else {
        overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(committedText_.size()) * 2.0f;
    }
}

std::string WebImeAdapter::GenerateOverlayCssStyle() const {
    char buf[256]{};
    std::snprintf(
        buf,
        sizeof(buf),
        "position:fixed;left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;opacity:0;pointer-events:none;z-index:9999;",
        overlayDomRect_.x,
        overlayDomRect_.y,
        std::max(1.0f, overlayDomRect_.width),
        std::max(16.0f, overlayDomRect_.height));
    return std::string(buf);
}

void WebImeAdapter::SyncClipboardFromDomEvent(std::string_view pastedText) {
    cachedClipboard_.assign(pastedText);
}

void WebImeAdapter::WriteClipboardToDom(std::string_view copiedText) {
    cachedClipboard_.assign(copiedText);
}

AriaShadowNode WebImeAdapter::BuildAriaShadowTree(const UIElement* rootElement) const {
    return BuildAriaNodeRecursive(rootElement);
}

WebFontStreamProvider::WebFontStreamProvider() {
    UnicodeShardDescriptor asciiShard{};
    asciiShard.shardId = 0u;
    asciiShard.rangeStart = 0x0000u;
    asciiShard.rangeEnd = 0x00FFu;
    asciiShard.shardUri = "fonts/shards/unicode_0000_00ff.bin";
    asciiShard.state = ShardLoadState::Loaded;
    asciiShard.loadedByteSize = 4096u;
    shards_[0u] = std::move(asciiShard);
    loadedShardCount_ = 1u;
}

std::uint32_t WebFontStreamProvider::ComputeShardIdForCodePoint(std::uint32_t codePoint) const noexcept {
    return codePoint >> 8u;
}

UnicodeShardDescriptor& WebFontStreamProvider::EnsureShardDescriptor(std::uint32_t codePoint) {
    const std::uint32_t shardId = ComputeShardIdForCodePoint(codePoint);
    auto it = shards_.find(shardId);
    if (it != shards_.end()) {
        return it->second;
    }

    const std::uint32_t startCp = shardId << 8u;
    const std::uint32_t endCp = startCp + 0xFFu;
    char uriBuf[128]{};
    std::snprintf(
        uriBuf,
        sizeof(uriBuf),
        "fonts/shards/cjk_shard_%04x_%04x.bin",
        startCp,
        endCp);

    UnicodeShardDescriptor desc{};
    desc.shardId = shardId;
    desc.rangeStart = startCp;
    desc.rangeEnd = endCp;
    desc.shardUri = uriBuf;
    desc.state = ShardLoadState::Unloaded;
    auto [insertedIt, _] = shards_.emplace(shardId, std::move(desc));
    return insertedIt->second;
}

std::size_t WebFontStreamProvider::InspectTextAndQueueMissingShards(
    std::string_view utf8Text,
    std::uint32_t targetRenderNodeId) {
    std::size_t queuedCount = 0;
    std::size_t cursor = 0;
    std::uint32_t cp = 0;

    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        UnicodeShardDescriptor& shard = EnsureShardDescriptor(cp);
        if (shard.state == ShardLoadState::Unloaded) {
            shard.state = ShardLoadState::FetchingAsync;
            pendingRequests_.push_back(FontShardFetchRequest{
                shard.shardId,
                shard.rangeStart,
                shard.rangeEnd,
                shard.shardUri,
                targetRenderNodeId});
            ++queuedCount;
        }
    }
    return queuedCount;
}

bool WebFontStreamProvider::ExportShardPayloadForId(
    std::uint32_t shardId,
    std::string_view fontFamily,
    std::vector<std::uint8_t>& outShardPayload) const {
    const std::shared_ptr<UiResourceContext> boundCtx =
        (UiResourceContext::ActiveOverride() == nullptr) ? resourceContext_.lock() : nullptr;
    ScopedUiResourceContext resourceScope(boundCtx);
    const std::uint32_t rangeStart = shardId << 8u;
    const std::uint32_t rangeEnd = rangeStart + 0xFFu;
    return FontAtlas::Instance().ExportGlyphShard(fontFamily, rangeStart, rangeEnd, outShardPayload);
}

bool WebFontStreamProvider::ExportRangeShardPayload(
    std::string_view fontFamily,
    std::uint32_t rangeStart,
    std::uint32_t rangeEnd,
    std::vector<std::uint8_t>& outShardPayload) {
    return FontAtlas::Instance().ExportGlyphShard(fontFamily, rangeStart, rangeEnd, outShardPayload);
}

bool WebFontStreamProvider::OnShardDataArrived(
    std::uint32_t shardId,
    const std::vector<std::uint8_t>& shardPayload,
    PagedRenderTree* renderTreeToInvalidate) {
    const std::shared_ptr<UiResourceContext> boundCtx =
        (UiResourceContext::ActiveOverride() == nullptr) ? resourceContext_.lock() : nullptr;
    ScopedUiResourceContext resourceScope(boundCtx);

    if (shardPayload.empty()) {
        return false;
    }

    bool hasBinaryHeader = false;
    if (shardPayload.size() >= 4u) {
        const std::uint32_t magic =
            static_cast<std::uint32_t>(shardPayload[0]) |
            (static_cast<std::uint32_t>(shardPayload[1]) << 8u) |
            (static_cast<std::uint32_t>(shardPayload[2]) << 16u) |
            (static_cast<std::uint32_t>(shardPayload[3]) << 24u);
        hasBinaryHeader = (magic == kAeroFontShardMagic);
    }

    auto it = shards_.find(shardId);
    if (it == shards_.end()) {
        if (!hasBinaryHeader) {
            return false;
        }
        (void)EnsureShardDescriptor(shardId << 8u);
        it = shards_.find(shardId);
    }

    std::uint32_t unpackedStart = shardId << 8u;
    std::uint32_t unpackedEnd = unpackedStart + 0xFFu;
    std::size_t unpackedCount = 0u;
    if (hasBinaryHeader) {
        if (!FontAtlas::Instance().UnpackGlyphShard(
                shardPayload.data(),
                shardPayload.size(),
                &unpackedStart,
                &unpackedEnd,
                &unpackedCount)) {
            return false;
        }
        totalUnpackedGlyphs_ += unpackedCount;
    }

    UnicodeShardDescriptor& shard = it->second;
    if (shard.state != ShardLoadState::Loaded) {
        shard.state = ShardLoadState::Loaded;
        shard.loadedByteSize = shardPayload.size();
        loadedShardCount_ += 1u;
    } else {
        shard.loadedByteSize = shardPayload.size();
    }

    if (hasBinaryHeader && renderTreeToInvalidate) {
        (void)renderTreeToInvalidate->RefreshGlyphRange(unpackedStart, unpackedEnd);
    }

    for (auto reqIt = pendingRequests_.begin(); reqIt != pendingRequests_.end();) {
        if (reqIt->shardId == shardId) {
            if (renderTreeToInvalidate && reqIt->requestingRenderNodeId < renderTreeToInvalidate->NodeCount()) {
                if (hasBinaryHeader) {
                    (void)renderTreeToInvalidate->RefreshTextNode(reqIt->requestingRenderNodeId);
                } else {
                    (void)renderTreeToInvalidate->InvalidateNodePage(reqIt->requestingRenderNodeId);
                }
            }
            reqIt = pendingRequests_.erase(reqIt);
        } else {
            ++reqIt;
        }
    }
    if (wakeCallback_) {
        wakeCallback_(wakeUserData_);
    }
    return true;
}

ShardLoadState WebFontStreamProvider::GetCodePointShardState(std::uint32_t codePoint) const noexcept {
    const std::uint32_t shardId = ComputeShardIdForCodePoint(codePoint);
    const auto it = shards_.find(shardId);
    if (it == shards_.end()) {
        return ShardLoadState::Unloaded;
    }
    return it->second.state;
}

WebCanvasHost::WebCanvasHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    ShaderBackend webBackend,
    PagedRenderTree* renderTree,
    std::shared_ptr<UiResourceContext> resourceContext)
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree)
    , resourceContext_(std::move(resourceContext))
    , webBackend_(webBackend) {
    fontStream_.SetResourceContext(resourceContext_);
    fontStream_.SetWakeCallback(
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
    wakeRegisteredRegistry_ = resourceContext_
        ? &resourceContext_->Templates()
        : &AxbTemplateRegistry::Instance();
    wakeRegisteredRegistry_->RegisterHostWakeListener(
        this,
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
}

WebCanvasHost::~WebCanvasHost() {
    fontStream_.SetWakeCallback(nullptr, nullptr);
    if (wakeRegisteredRegistry_ && AxbTemplateRegistry::IsLiveRegistry(wakeRegisteredRegistry_)) {
        wakeRegisteredRegistry_->UnregisterHostWakeListener(this);
    }
    wakeRegisteredRegistry_ = nullptr;
}

bool WebCanvasHost::OnFontShardLoaded(
    std::uint32_t shardId,
    const std::vector<std::uint8_t>& shardPayload) {
    const bool loaded = fontStream_.OnShardDataArrived(shardId, shardPayload, renderTree_);
    if (loaded) {
        RequestWakeFrame();
    }
    return loaded;
}

void WebCanvasHost::SetResourceContext(std::shared_ptr<UiResourceContext> context) {
    if (resourceContext_ == context) {
        return;
    }
    if (wakeRegisteredRegistry_ && AxbTemplateRegistry::IsLiveRegistry(wakeRegisteredRegistry_)) {
        wakeRegisteredRegistry_->UnregisterHostWakeListener(this);
    }
    resourceContext_ = std::move(context);
    fontStream_.SetResourceContext(resourceContext_);
    wakeRegisteredRegistry_ = resourceContext_
        ? &resourceContext_->Templates()
        : &AxbTemplateRegistry::Instance();
    wakeRegisteredRegistry_->RegisterHostWakeListener(
        this,
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
}

void WebCanvasHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUiResourceContext resourceScope(resourceContext_);

    if ((inputRouter_ && inputRouter_->HasPendingEvents()) ||
        (bindingEngine_ && bindingEngine_->HasPendingDirtyObjects()) ||
        (animationEngine_ && animationEngine_->HasActiveAnimations())) {
        rafState_ = WebRafState::ActiveTicking;
    }
    if (rafState_ == WebRafState::SuspendedIdle) {
        return;
    }

    if (inputRouter_ && animationEngine_ && !inputRouter_->GetAnimationEngine()) {
        inputRouter_->SetAnimationEngine(animationEngine_);
    }
    if (bindingEngine_ && animationEngine_ && !bindingEngine_->GetAnimationEngine()) {
        bindingEngine_->SetAnimationEngine(animationEngine_);
    }

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    std::size_t changes = 0;
    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_ && inputRouter_->HasPendingEvents()) {
        const std::size_t dispatched = inputRouter_->FlushQueuedEvents();
        telemetry_.inputEventsDispatched += dispatched;
        changes += dispatched;
    }
    if ((activePhaseMask & UiPhase_TrackABinding) != 0u && bindingEngine_) {
        const std::size_t a = bindingEngine_->FlushTrackA();
        telemetry_.trackAUpdates += a;
        changes += a;
    }
    if ((activePhaseMask & UiPhase_TrackBBinding) != 0u && bindingEngine_) {
        const std::size_t b = bindingEngine_->FlushTrackB();
        telemetry_.trackBUpdates += b;
        changes += b;
    }
    if ((activePhaseMask & UiPhase_MotionStateMachine) != 0u && animationEngine_) {
        const std::size_t smChanged = animationEngine_->EvaluateStateMachines(dt);
        telemetry_.stateMachineTransitions += smChanged;
        changes += smChanged;
    }
    if ((activePhaseMask & UiPhase_Style) != 0u) {
        telemetry_.stylePassesRun += 1u;
        if (styleEngine_) {
            if (bindingEngine_ && !styleEngine_->GetBindingEngine()) {
                styleEngine_->SetBindingEngine(bindingEngine_);
            }
            const std::size_t stChanged = styleEngine_->EvaluateAndApply();
            telemetry_.styleTriggerChanges += stChanged;
            changes += stChanged;
        }
    }
    if ((activePhaseMask & UiPhase_Animation) != 0u && animationEngine_ && animationEngine_->HasActiveAnimations()) {
        const std::size_t animUpdated = animationEngine_->Tick(dt);
        telemetry_.animationTrackUpdates += animUpdated;
        changes += animUpdated;
    }
    if ((activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.layoutPassesRun += 1u;
        if (rootVisual_ && viewportSize_ != lastLaidOutViewport_) {
            rootVisual_->InvalidateLayout();
            lastLaidOutViewport_ = viewportSize_;
        }
        if (layoutQueue_) {
            if (rootVisual_) {
                layoutQueue_->EnqueueDirtySubtree(rootVisual_);
            }
            changes += layoutQueue_->FlushLayout(viewportSize_);
        } else if (rootVisual_) {
            rootVisual_->Measure(viewportSize_);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, viewportSize_.width, viewportSize_.height});
        }
    }

    if (renderTree_ && rootVisual_ && (activePhaseMask & UiPhase_RenderRecord) != 0u) {
        const std::size_t synced = renderTree_->SyncVisualTree(rootVisual_);
        telemetry_.renderNodesSynced += synced;
        changes += synced;
    }

    const bool animStillRunning = animationEngine_ && animationEngine_->HasActiveAnimations();
    if (changes == 0 && !animStillRunning && !imeAdapter_.IsComposing()) {
        rafState_ = WebRafState::SuspendedIdle;
    }
}

ZeroCopySubmissionStats WebCanvasHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    ScopedUiResourceContext resourceScope(resourceContext_);
    gpuStream.SetBackend(webBackend_);
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

} // namespace aero
