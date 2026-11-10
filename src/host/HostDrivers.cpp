#include "aero/host/HostDrivers.hpp"

#include <algorithm>
#include <cstdio>

#include "aero/controls/Controls.hpp"

namespace aero {

namespace {

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
    PagedRenderTree* renderTree) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree) {}

void EmbeddedGameHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_) {
        telemetry_.inputEventsDispatched += inputRouter_->FlushQueuedEvents();
    }

    if ((activePhaseMask & UiPhase_TrackABinding) != 0u && bindingEngine_) {
        telemetry_.trackAUpdates += bindingEngine_->FlushTrackA();
    }

    if ((activePhaseMask & UiPhase_TrackBBinding) != 0u && bindingEngine_) {
        if (renderTree_ && (activePhaseMask & UiPhase_Layout) == 0u) {
            // Fast HUD Track B direct render-tree path (bypasses DP and LayoutEngine)
            telemetry_.trackBUpdates += renderTree_->BatchUpdateFromEcsTrackB(*bindingEngine_);
        } else {
            telemetry_.trackBUpdates += bindingEngine_->FlushTrackB();
        }
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

    // Automatically synchronize dirty UIElement nodes into PagedRenderTree in O(dirty_nodes)
    // (Skipped on pure HUD fast-refresh frames where Track B writes directly to CommandPages)
    if (renderTree_ && rootVisual_ &&
        (activePhaseMask & UiPhase_RenderRecord) != 0u &&
        (activePhaseMask & UiPhase_Layout) != 0u) {
        telemetry_.renderNodesSynced += renderTree_->SyncVisualTree(rootVisual_);
    }
}

ZeroCopySubmissionStats EmbeddedGameHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
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
    PagedRenderTree* renderTree) noexcept
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree) {}

void DesktopEventHost::SetStateFlags(
    bool hasActiveAnimations,
    bool hasDirtyObjects,
    bool hasDirtyLayout,
    bool hasPendingInputEvents) noexcept {
    hasActiveAnimations_ = hasActiveAnimations;
    hasDirtyObjects_ = hasDirtyObjects;
    hasDirtyLayout_ = hasDirtyLayout;
    hasPendingInputEvents_ = hasPendingInputEvents;
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
    const bool animEngineActive = animationEngine_ && animationEngine_->HasActiveAnimations();
    return !hasActiveAnimations_ &&
           !animEngineActive &&
           !hasDirtyObjects_ &&
           !hasDirtyLayout_ &&
           !hasPendingInputEvents_ &&
           !routerPending &&
           !rootVisualDirty;
}

void DesktopEventHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    if (CanSleepIdle()) {
        // 0.0% CPU / 0.0% GPU polling when idle
        return;
    }

    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_ && inputRouter_->HasPendingEvents()) {
        const std::size_t dispatched = inputRouter_->FlushQueuedEvents();
        telemetry_.inputEventsDispatched += dispatched;
        if (dispatched > 0u) {
            hasDirtyObjects_ = true;
            hasDirtyLayout_ = true;
        }
    }

    if (hasDirtyObjects_ && bindingEngine_ && (activePhaseMask & UiPhase_TrackABinding) != 0u) {
        const std::size_t updated = bindingEngine_->FlushTrackA();
        telemetry_.trackAUpdates += updated;
        if (updated > 0) {
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
}

ZeroCopySubmissionStats DesktopEventHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    if (renderTree.DirtyPageCount() == 0 && CanSleepIdle()) {
        return ZeroCopySubmissionStats{};
    }
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 3. WebImeAdapter & WebFontStreamProvider & WebCanvasHost
// ============================================================================

void WebImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    if (focusedElement_ != focusedElement) {
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
    focusedElement_ = nullptr;
    imeFocused_ = false;
    isComposing_ = false;
    preeditText_.clear();
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
    committedText_ += std::string(committedText);
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
    // Pre-load Basic ASCII shard (U+0000 .. U+00FF) at startup so Latin HUD text never stalls
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

bool WebFontStreamProvider::OnShardDataArrived(
    std::uint32_t shardId,
    const std::vector<std::uint8_t>& shardPayload,
    PagedRenderTree* renderTreeToInvalidate) {
    auto it = shards_.find(shardId);
    if (it == shards_.end()) {
        return false;
    }

    UnicodeShardDescriptor& shard = it->second;
    if (shard.state != ShardLoadState::Loaded) {
        shard.state = ShardLoadState::Loaded;
        shard.loadedByteSize = shardPayload.size();
        loadedShardCount_ += 1u;
    }

    for (auto reqIt = pendingRequests_.begin(); reqIt != pendingRequests_.end();) {
        if (reqIt->shardId == shardId) {
            if (renderTreeToInvalidate && reqIt->requestingRenderNodeId < renderTreeToInvalidate->NodeCount()) {
                (void)renderTreeToInvalidate->InvalidateNodePage(reqIt->requestingRenderNodeId);
            }
            reqIt = pendingRequests_.erase(reqIt);
        } else {
            ++reqIt;
        }
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
    PagedRenderTree* renderTree)
    : rootVisual_(rootVisual)
    , bindingEngine_(bindingEngine)
    , layoutQueue_(layoutQueue)
    , renderTree_(renderTree)
    , webBackend_(webBackend) {}

void WebCanvasHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    (void)dt;
    if (inputRouter_ && inputRouter_->HasPendingEvents()) {
        rafState_ = WebRafState::ActiveTicking;
    }
    if (rafState_ == WebRafState::SuspendedIdle) {
        return;
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

    // Automatically suspend rAF if no IME composition, no pending font shards, and no dirty bindings/layout/render
    const bool animStillRunning = animationEngine_ && animationEngine_->HasActiveAnimations();
    if (changes == 0 && !animStillRunning && !imeAdapter_.IsComposing() && fontStream_.PendingFetchRequests().empty()) {
        rafState_ = WebRafState::SuspendedIdle;
    }
}

ZeroCopySubmissionStats WebCanvasHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    gpuStream.SetBackend(webBackend_);
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

} // namespace aero
