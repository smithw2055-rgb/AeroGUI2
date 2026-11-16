#include "aero/host/HostDrivers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

namespace {

AriaShadowNode BuildAriaNodeRecursive(const UIElement* elem) {
    AriaShadowNode node{};
    if (!elem) {
        return node;
    }
    node.bounds = elem->FinalRect();
    node.label = elem->Name();

    AriaElementInfo info{};
    elem->PopulateAriaInfo(info);
    node.role = std::move(info.role);
    node.value = std::move(info.value);
    if (node.label.empty()) {
        node.label = std::move(info.label);
    }
    node.focusable = info.focusable;

    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            node.children.push_back(BuildAriaNodeRecursive(c));
        }
    }
    return node;
}

} // namespace

// ============================================================================
// 0. HostDriverBase
// ============================================================================

HostDriverBase::HostDriverBase(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceContext> resourceContext,
    Size initialViewportSize) noexcept
    : rootVisual_(rootVisual)
    , resourceContext_(std::move(resourceContext))
    , viewportSize_(initialViewportSize) {
    viewContext_.SetBindingEngine(bindingEngine);
    viewContext_.SetDomains(bindingEngine ? &bindingEngine->Domains() : nullptr);
    viewContext_.SetLayoutQueue(layoutQueue);
    viewContext_.SetRenderTree(renderTree);
    viewContext_.SetResourceContext(resourceContext_.get());
    if (rootVisual_ && !rootVisual_->ViewContext()) {
        rootVisual_->SetViewContext(&viewContext_);
    }
}

HostDriverBase::~HostDriverBase() {
    UnregisterWakeRegistryListener();
}

void HostDriverBase::SetResourceContext(std::shared_ptr<UIResourceContext> context) {
    if (resourceContext_ == context) {
        return;
    }
    resourceContext_ = std::move(context);
    viewContext_.SetResourceContext(resourceContext_.get());
    OnResourceContextChanged();
}

void HostDriverBase::SetRootVisual(UIElement* root) noexcept {
    ScopedUIResourceContext scope(resourceContext_);
    rootVisual_ = root;
    if (rootVisual_ && !rootVisual_->ViewContext()) {
        rootVisual_->SetViewContext(&viewContext_);
    }
    lastLaidOutViewport_ = {-1.0f, -1.0f};
    OnRootVisualChanged();
}

void HostDriverBase::SetViewportSize(Size viewport) noexcept {
    if (viewportSize_ != viewport) {
        viewportSize_ = viewport;
        OnViewportOrDpiChanged();
    }
}

void HostDriverBase::SetDpiScale(float scale) noexcept {
    const float clamped = (std::isfinite(scale) && scale > 0.0f)
        ? std::clamp(scale, 0.25f, 8.0f)
        : 1.0f;
    const bool changed = std::fabs(viewContext_.GetDpiScale() - clamped) > 1e-4f;
    viewContext_.SetDpiScale(clamped);
    if (changed) {
        OnViewportOrDpiChanged();
    }
    if (rootVisual_ && rootVisual_->ViewContext() && rootVisual_->ViewContext() != &viewContext_) {
        rootVisual_->ViewContext()->SetDpiScale(viewContext_.GetDpiScale());
    }
    if (auto* rt = GetRenderTree()) {
        rt->SetRootDpiScale(viewContext_.GetDpiScale());
    }
}

void HostDriverBase::RegisterWakeRegistryListener(HostWakeCallback callback, void* userData) {
    UnregisterWakeRegistryListener();
    wakeRegisteredRegistry_ = resourceContext_
        ? &resourceContext_->Templates()
        : &AxbTemplateRegistry::Instance();
    wakeRegisteredRegistry_->RegisterHostWakeListener(this, callback, userData);
}

void HostDriverBase::UnregisterWakeRegistryListener() noexcept {
    if (wakeRegisteredRegistry_ && AxbTemplateRegistry::IsLiveRegistry(wakeRegisteredRegistry_)) {
        wakeRegisteredRegistry_->UnregisterHostWakeListener(this);
    }
    wakeRegisteredRegistry_ = nullptr;
}

HostDriverBase::UiPhaseRunResult HostDriverBase::RunUiPhases(
    float dt,
    std::uint32_t activePhaseMask,
    const UiPhaseRunConfig& config) {
    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

    InputEventRouter* inputRouter = viewContext_.GetInputRouter();
    DualTrackBindingEngine* bindingEngine = viewContext_.GetBindingEngine();
    AnimationEngine* animationEngine = viewContext_.GetAnimationEngine();
    StyleTriggerEngine* styleEngine = viewContext_.GetStyleEngine();
    DepthBucketLayoutQueue* layoutQueue = viewContext_.GetLayoutQueue();
    PagedRenderTree* renderTree = viewContext_.GetRenderTree();
    const float dpiScale = viewContext_.GetDpiScale();

    if (inputRouter && animationEngine && !inputRouter->GetAnimationEngine()) {
        inputRouter->SetAnimationEngine(animationEngine);
    }
    if (bindingEngine && animationEngine && !bindingEngine->GetAnimationEngine()) {
        bindingEngine->SetAnimationEngine(animationEngine);
    }

    auto markDirtyObjects = [&]() noexcept {
        if (config.ioHasDirtyObjects) {
            *config.ioHasDirtyObjects = true;
        }
    };
    auto markDirtyLayout = [&]() noexcept {
        if (config.ioHasDirtyLayout) {
            *config.ioHasDirtyLayout = true;
        }
    };
    const auto isDirtyObjects = [&]() noexcept -> bool {
        return config.ioHasDirtyObjects && *config.ioHasDirtyObjects;
    };
    const auto isDirtyLayout = [&]() noexcept -> bool {
        return config.ioHasDirtyLayout && *config.ioHasDirtyLayout;
    };

    UiPhaseRunResult result{};

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter && inputRouter->HasPendingEvents()) {
        result.inputDispatched = inputRouter->FlushQueuedEvents();
        telemetry_.inputEventsDispatched += result.inputDispatched;
        if (result.inputDispatched > 0u) {
            markDirtyObjects();
            markDirtyLayout();
        }
    }

    if (rootVisual_ && dt > 0.0f) {
        result.liveTickUpdated = TickLiveControlsInSubtree(rootVisual_, dt);
        if (result.liveTickUpdated > 0u) {
            markDirtyObjects();
            markDirtyLayout();
        }
    }

    const bool canRunTrackA = !config.gatePhasesByDirtyFlags ||
        isDirtyObjects() || (bindingEngine && bindingEngine->HasPendingDirtyObjects());
    if (canRunTrackA && bindingEngine && (activePhaseMask & UiPhase_TrackABinding) != 0u) {
        result.trackAUpdates = bindingEngine->FlushTrackA();
        telemetry_.trackAUpdates += result.trackAUpdates;
        if (result.trackAUpdates > 0u) {
            markDirtyLayout();
        }
    }

    if (config.runTrackB && bindingEngine && (activePhaseMask & UiPhase_TrackBBinding) != 0u) {
        if (config.allowTrackBEcsFastPath && renderTree && (activePhaseMask & UiPhase_Layout) == 0u) {
            result.trackBUpdates = renderTree->BatchUpdateFromEcsTrackB(*bindingEngine);
        } else {
            result.trackBUpdates = bindingEngine->FlushTrackB();
        }
        telemetry_.trackBUpdates += result.trackBUpdates;
    }

    if ((activePhaseMask & UiPhase_MotionStateMachine) != 0u && animationEngine) {
        result.stateMachineTransitions = animationEngine->EvaluateStateMachines(dt);
        telemetry_.stateMachineTransitions += result.stateMachineTransitions;
        if (result.stateMachineTransitions > 0u) {
            markDirtyLayout();
        }
    }

    const bool canRunStyle = !config.gatePhasesByDirtyFlags ||
        styleEngine != nullptr || isDirtyObjects() || isDirtyLayout();
    if ((activePhaseMask & UiPhase_Style) != 0u && canRunStyle) {
        telemetry_.stylePassesRun += 1u;
        if (styleEngine) {
            if (bindingEngine && !styleEngine->GetBindingEngine()) {
                styleEngine->SetBindingEngine(bindingEngine);
            }
            result.styleChanges = styleEngine->EvaluateAndApply();
            telemetry_.styleTriggerChanges += result.styleChanges;
            if (result.styleChanges > 0u) {
                markDirtyLayout();
            }
        }
    }

    if ((activePhaseMask & UiPhase_Animation) != 0u && animationEngine &&
        (!config.requireActiveAnimationsForTick || animationEngine->HasActiveAnimations())) {
        result.animationUpdates = animationEngine->Tick(dt);
        telemetry_.animationTrackUpdates += result.animationUpdates;
    }

    const Size logicalViewport = LogicalViewportSize();
    const bool viewportOrDpiChanged =
        (rootVisual_ && logicalViewport != lastLaidOutViewport_);
    const bool rootNeedsLayout = rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) != 0u;
    const bool canRunLayout = !config.gatePhasesByDirtyFlags ||
        isDirtyLayout() || rootNeedsLayout || viewportOrDpiChanged;
    if ((activePhaseMask & UiPhase_Layout) != 0u && canRunLayout) {
        telemetry_.layoutPassesRun += 1u;
        if (rootVisual_ && rootVisual_->ViewContext()) {
            rootVisual_->ViewContext()->SetDpiScale(dpiScale);
        }
        if (rootVisual_ && logicalViewport != lastLaidOutViewport_) {
            rootVisual_->InvalidateLayout();
            lastLaidOutViewport_ = logicalViewport;
        }
        if (layoutQueue) {
            if (rootVisual_) {
                layoutQueue->EnqueueDirtySubtree(rootVisual_);
            }
            result.layoutUpdates = layoutQueue->FlushLayout(logicalViewport);
        } else if (rootVisual_) {
            rootVisual_->Measure(logicalViewport);
            rootVisual_->Arrange(Rect{0.0f, 0.0f, logicalViewport.width, logicalViewport.height});
        }
    }

    if (renderTree && rootVisual_ && (activePhaseMask & UiPhase_RenderRecord) != 0u &&
        (!config.requireLayoutPhaseForRenderSync || (activePhaseMask & UiPhase_Layout) != 0u)) {
        renderTree->SetRootDpiScale(dpiScale);
        result.renderNodesSynced = renderTree->SyncVisualTree(rootVisual_);
        telemetry_.renderNodesSynced += result.renderNodesSynced;
    }

    return result;
}

ZeroCopySubmissionStats HostDriverBase::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    ScopedUIResourceContext resourceScope(resourceContext_);
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 1. EmbeddedGameHost
// ============================================================================

EmbeddedGameHost::EmbeddedGameHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceContext> resourceContext) noexcept
    : HostDriverBase(
        rootVisual,
        bindingEngine,
        layoutQueue,
        renderTree,
        std::move(resourceContext),
        Size{1920.0f, 1080.0f}) {}

void EmbeddedGameHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUIResourceContext resourceScope(resourceContext_);

    UiPhaseRunConfig cfg{};
    cfg.runTrackB = true;
    cfg.allowTrackBEcsFastPath = true;
    cfg.requireActiveAnimationsForTick = false;
    cfg.requireLayoutPhaseForRenderSync = true;
    cfg.gatePhasesByDirtyFlags = false;
    (void)RunUiPhases(dt, activePhaseMask, cfg);
}

// ============================================================================
// 2. DesktopEventHost
// ============================================================================

DesktopEventHost::DesktopEventHost(
    UIElement* rootVisual,
    DualTrackBindingEngine* bindingEngine,
    DepthBucketLayoutQueue* layoutQueue,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceContext> resourceContext) noexcept
    : HostDriverBase(
        rootVisual,
        bindingEngine,
        layoutQueue,
        renderTree,
        std::move(resourceContext),
        Size{1920.0f, 1080.0f}) {
    RegisterWakeRegistryListener(
        [](void* ud) noexcept {
            static_cast<DesktopEventHost*>(ud)->NotifyDirtyLayout();
        },
        this);
}

DesktopEventHost::~DesktopEventHost() {
    UnregisterWakeRegistryListener();
}

void DesktopEventHost::OnResourceContextChanged() {
    RegisterWakeRegistryListener(
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
    auto* renderTree = GetRenderTree();
    auto* inputRouter = InputRouter();
    auto* bindingEngine = GetBindingEngine();
    auto* animationEngine = GetAnimationEngine();

    const bool rootLayoutDirty = rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) != 0u;
    const bool rootRenderDirty = renderTree && rootVisual_ &&
        (rootVisual_->GetLayoutDirtyFlags() &
         (RenderDirty | SubtreeRenderDirty)) != 0u;
    const bool rootVisualDirty = rootLayoutDirty || rootRenderDirty;
    const bool routerPending = inputRouter && inputRouter->HasPendingEvents();
    const bool bindingDirty = bindingEngine && bindingEngine->HasPendingDirtyObjects();
    const bool animEngineActive = animationEngine && animationEngine->HasActiveAnimations();
    const bool liveControlsTick = SubtreeNeedsContinuousTick(rootVisual_);
    return !hasActiveAnimations_ &&
           !animEngineActive &&
           !liveControlsTick &&
           !hasDirtyObjects_ &&
           !bindingDirty &&
           !hasDirtyLayout_ &&
           !hasPendingInputEvents_ &&
           !routerPending &&
           !rootVisualDirty;
}

void DesktopEventHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUIResourceContext resourceScope(resourceContext_);

    if (CanSleepIdle()) {
        // 0.0% CPU / 0.0% GPU polling when idle
        return;
    }

    UiPhaseRunConfig cfg{};
    cfg.runTrackB = false;
    cfg.allowTrackBEcsFastPath = false;
    cfg.requireActiveAnimationsForTick = true;
    cfg.requireLayoutPhaseForRenderSync = false;
    cfg.gatePhasesByDirtyFlags = true;
    cfg.ioHasDirtyObjects = &hasDirtyObjects_;
    cfg.ioHasDirtyLayout = &hasDirtyLayout_;
    (void)RunUiPhases(dt, activePhaseMask, cfg);

    // Clear transient dirty flags after processing; if animations are off, next check can sleep idle
    hasDirtyObjects_ = false;
    hasDirtyLayout_ = false;
    hasPendingInputEvents_ = false;
    if (auto* animationEngine = GetAnimationEngine()) {
        hasActiveAnimations_ = animationEngine->HasActiveAnimations();
    }
}

ZeroCopySubmissionStats DesktopEventHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    if (renderTree.DirtyPageCount() == 0 && CanSleepIdle()) {
        return ZeroCopySubmissionStats{};
    }
    return HostDriverBase::RecordGpuCommands(renderTree, gpuStream);
}

// ============================================================================
// 3. DesktopImeAdapter, WebImeAdapter, WebFontStreamProvider & WebCanvasHost
// ============================================================================

void DesktopImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    ime_.BeginFocus(focusedElement);
    caretScreenRect_ = caretScreenRect;
    focusSyncCount_ += 1u;
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, ime_.IsImeActive(), spotSyncUserData_);
    }
}

void DesktopImeAdapter::BlurTextInput() {
    ime_.EndBlur();
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, false, spotSyncUserData_);
    }
}

void DesktopImeAdapter::OnCompositionStart() {
    if (ime_.StartComposition(&caretScreenRect_)) {
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
    }
}

void DesktopImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    if (ime_.BuildPreviewAndApply(preeditText, caretOffset, nullptr, &caretScreenRect_)) {
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
    }
}

void DesktopImeAdapter::OnCompositionEnd(std::string_view committedText) {
    if (ime_.CommitComposition(committedText, &caretScreenRect_)) {
        focusSyncCount_ += 1u;
        if (spotSyncCallback_) {
            spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
        }
    }
}

void WebImeAdapter::FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) {
    ime_.BeginFocus(focusedElement);
    baseCaretRect_ = caretScreenRect;
    overlayDomRect_ = caretScreenRect;
}

void WebImeAdapter::BlurTextInput() {
    ime_.EndBlur();
}

void WebImeAdapter::OnCompositionStart() {
    if (ime_.StartComposition(&baseCaretRect_)) {
        overlayDomRect_ = baseCaretRect_;
    } else {
        overlayDomRect_.x = baseCaretRect_.x +
            static_cast<float>(ime_.CommittedString().size()) * 2.0f;
    }
}

void WebImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    if (ime_.BuildPreviewAndApply(preeditText, caretOffset, &baseCaretRect_, nullptr)) {
        overlayDomRect_.x = baseCaretRect_.x + static_cast<float>(caretOffset) * 2.0f;
        return;
    }

    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(ime_.CommittedString().size() + caretOffset) * 2.0f;
    if (UIElement* focused = ime_.FocusedElement()) {
        focused->SetValue(
            PropId_Text,
            value::FromString(ime_.CommittedString() + ime_.PreeditString()),
            ValueSourceRank::Local);
    }
}

void WebImeAdapter::OnCompositionEnd(std::string_view committedText) {
    if (ime_.CommitComposition(committedText, &baseCaretRect_)) {
        overlayDomRect_ = baseCaretRect_;
        return;
    }

    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(ime_.CommittedString().size()) * 2.0f;
    if (UIElement* focused = ime_.FocusedElement()) {
        focused->SetValue(
            PropId_Text,
            value::FromString(ime_.CommittedString()),
            ValueSourceRank::Local);
        (void)focused->NotifyTwoWayPropertyMutated(PropId_Text);
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
    const std::shared_ptr<UIResourceContext> boundCtx =
        (UIResourceContext::ActiveOverride() == nullptr) ? resourceContext_.lock() : nullptr;
    ScopedUIResourceContext resourceScope(boundCtx);
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
    const std::shared_ptr<UIResourceContext> boundCtx =
        (UIResourceContext::ActiveOverride() == nullptr) ? resourceContext_.lock() : nullptr;
    ScopedUIResourceContext resourceScope(boundCtx);

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
    std::shared_ptr<UIResourceContext> resourceContext)
    : HostDriverBase(
        rootVisual,
        bindingEngine,
        layoutQueue,
        renderTree,
        std::move(resourceContext),
        Size{1280.0f, 720.0f})
    , webBackend_(webBackend) {
    fontStream_.SetResourceContext(resourceContext_);
    fontStream_.SetWakeCallback(
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
    RegisterWakeRegistryListener(
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
}

WebCanvasHost::~WebCanvasHost() {
    fontStream_.SetWakeCallback(nullptr, nullptr);
    UnregisterWakeRegistryListener();
}

bool WebCanvasHost::OnFontShardLoaded(
    std::uint32_t shardId,
    const std::vector<std::uint8_t>& shardPayload) {
    const bool loaded = fontStream_.OnShardDataArrived(shardId, shardPayload, GetRenderTree());
    if (loaded) {
        RequestWakeFrame();
    }
    return loaded;
}

void WebCanvasHost::OnResourceContextChanged() {
    fontStream_.SetResourceContext(resourceContext_);
    RegisterWakeRegistryListener(
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
}

bool WebCanvasHost::CanSleepIdle() const noexcept {
    if (auto* inputRouter = InputRouter(); inputRouter && inputRouter->HasPendingEvents()) {
        return false;
    }
    if (auto* bindingEngine = GetBindingEngine(); bindingEngine && bindingEngine->HasPendingDirtyObjects()) {
        return false;
    }
    if (auto* animationEngine = GetAnimationEngine(); animationEngine && animationEngine->HasActiveAnimations()) {
        return false;
    }
    return rafState_ == WebRafState::SuspendedIdle;
}

void WebCanvasHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    ScopedUIResourceContext resourceScope(resourceContext_);

    auto* inputRouter = InputRouter();
    auto* bindingEngine = GetBindingEngine();
    auto* animationEngine = GetAnimationEngine();

    if ((inputRouter && inputRouter->HasPendingEvents()) ||
        (bindingEngine && bindingEngine->HasPendingDirtyObjects()) ||
        (animationEngine && animationEngine->HasActiveAnimations()) ||
        SubtreeNeedsContinuousTick(rootVisual_)) {
        rafState_ = WebRafState::ActiveTicking;
    }
    if (rafState_ == WebRafState::SuspendedIdle) {
        return;
    }

    UiPhaseRunConfig cfg{};
    cfg.runTrackB = true;
    cfg.allowTrackBEcsFastPath = false;
    cfg.requireActiveAnimationsForTick = true;
    cfg.requireLayoutPhaseForRenderSync = false;
    cfg.gatePhasesByDirtyFlags = false;
    const UiPhaseRunResult res = RunUiPhases(dt, activePhaseMask, cfg);

    const std::size_t changes = res.TotalChanges();
    const bool animStillRunning = animationEngine && animationEngine->HasActiveAnimations();
    const bool liveStillRunning = SubtreeNeedsContinuousTick(rootVisual_);
    if (changes == 0 && !animStillRunning && !liveStillRunning && !imeAdapter_.IsComposing()) {
        rafState_ = WebRafState::SuspendedIdle;
    }
}

ZeroCopySubmissionStats WebCanvasHost::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    gpuStream.SetBackend(webBackend_);
    return HostDriverBase::RecordGpuCommands(renderTree, gpuStream);
}

} // namespace aero
