#include "aero/host/HostDrivers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "aero/core/StringUtils.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

namespace {

AriaShadowNode BuildAriaNodeRecursive(const VisualWorld* world, VisualNodeId id) {
    AriaShadowNode node{};
    if (!world) {
        return node;
    }
    const VisualNode* vnode = world->GetNode(id);
    if (!vnode || vnode->layout.visibility == Visibility::Collapsed) {
        return node;
    }
    node.bounds = vnode->layout.layoutRect;
    node.focusable = vnode->layout.isFocusable;
    if (vnode->kind == VisualKind::Text) {
        node.role = "text";
        node.value = vnode->text.text;
    } else {
        node.role = "region";
    }
    for (VisualNodeId childId : vnode->children) {
        if (const VisualNode* child = world->GetNode(childId);
            child && child->layout.visibility != Visibility::Collapsed) {
            node.children.push_back(BuildAriaNodeRecursive(world, childId));
        }
    }
    return node;
}

} // namespace

// ============================================================================
// 0. HostDriverBase
// ============================================================================

HostDriverBase::HostDriverBase(
    VisualWorld* world,
    DrawingCommandBuffer* drawBuffer,
    VisualAnimationEngine* animationEngine,
    VisualInputRouter* inputRouter,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceStore> resourceStore,
    Size initialViewportSize) noexcept
    : world_(world)
    , drawBuffer_(drawBuffer)
    , animationEngine_(animationEngine)
    , inputRouter_(inputRouter)
    , renderTree_(renderTree)
    , resourceStore_(std::move(resourceStore))
    , viewportSize_(initialViewportSize) {
    if (world_ && resourceStore_) {
        world_->SetResourceStore(resourceStore_.get());
    }
    if (renderTree_ && resourceStore_) {
        renderTree_->SetResourceStore(resourceStore_.get());
    }
}

HostDriverBase::~HostDriverBase() = default;

void HostDriverBase::SetResourceStore(std::shared_ptr<UIResourceStore> store) {
    if (resourceStore_ == store) {
        return;
    }
    resourceStore_ = std::move(store);
    if (world_) {
        world_->SetResourceStore(resourceStore_.get());
    }
    if (renderTree_) {
        renderTree_->SetResourceStore(resourceStore_.get());
    }
    renderSyncDirty_ = true;
    OnResourceStoreChanged();
}

void HostDriverBase::SetVisualWorld(VisualWorld* world) noexcept {
    world_ = world;
    if (world_ && resourceStore_) {
        world_->SetResourceStore(resourceStore_.get());
    }
    lastLaidOutViewport_ = Size{-1.0f, -1.0f};
    renderSyncDirty_ = true;
    OnRootVisualChanged();
}

void HostDriverBase::SetRenderTree(PagedRenderTree* tree) noexcept {
    renderTree_ = tree;
    if (renderTree_ && resourceStore_) {
        renderTree_->SetResourceStore(resourceStore_.get());
    }
    renderSyncDirty_ = true;
}

void HostDriverBase::SetViewportSize(Size viewport) noexcept {
    if (viewportSize_ != viewport) {
        viewportSize_ = viewport;
        renderSyncDirty_ = true;
        OnViewportOrDpiChanged();
    }
}

void HostDriverBase::SetDpiScale(float scale) noexcept {
    const float clamped = (std::isfinite(scale) && scale > 0.0f)
        ? std::clamp(scale, 0.25f, 8.0f)
        : 1.0f;
    const bool changed = std::fabs(dpiScale_ - clamped) > 1e-4f;
    dpiScale_ = clamped;
    if (changed) {
        renderSyncDirty_ = true;
        OnViewportOrDpiChanged();
    }
    if (renderTree_) {
        renderTree_->SetRootDpiScale(dpiScale_);
    }
}

HostDriverBase::UiPhaseRunResult HostDriverBase::RunUiPhases(
    float dt,
    std::uint32_t activePhaseMask,
    const UiPhaseRunConfig& config) {
    telemetry_.frameCount += 1u;
    telemetry_.lastActivePhaseMask = activePhaseMask;

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

    UiPhaseRunResult result{};

    if ((activePhaseMask & UiPhase_Input) != 0u && inputRouter_ && inputRouter_->HasPendingEvents()) {
        result.inputDispatched = inputRouter_->FlushQueuedEvents();
        telemetry_.inputEventsDispatched += result.inputDispatched;
        if (result.inputDispatched > 0u) {
            markDirtyObjects();
            markDirtyLayout();
        }
    }

    if (config.runTrackB && world_ && (activePhaseMask & UiPhase_TrackBBinding) != 0u) {
        result.trackBUpdates = world_->FlushTrackB();
        telemetry_.trackBUpdates += result.trackBUpdates;
    }

    if ((activePhaseMask & UiPhase_Animation) != 0u && animationEngine_ && world_ &&
        (!config.requireActiveAnimationsForTick || animationEngine_->HasActiveAnimations())) {
        if (animationEngine_->Tick(dt, *world_)) {
            result.animationUpdates = 1u;
            telemetry_.animationTrackUpdates += 1u;
            markDirtyLayout();
        }
    }

    if ((activePhaseMask & UiPhase_Layout) != 0u && world_) {
        const Size logicalViewport = LogicalViewportSize();
        const bool viewportChanged = (logicalViewport != lastLaidOutViewport_);
        const bool dirtyLayout = config.ioHasDirtyLayout && *config.ioHasDirtyLayout;
        if (!config.gatePhasesByDirtyFlags || viewportChanged || world_->HasAnyDirty() || dirtyLayout) {
            telemetry_.layoutPassesRun += 1u;
            world_->UpdateLayout(logicalViewport.width, logicalViewport.height);
            lastLaidOutViewport_ = logicalViewport;
            result.layoutUpdates = 1u;
        }
    }

    if (result.inputDispatched > 0u || result.trackBUpdates > 0u ||
        result.animationUpdates > 0u || result.layoutUpdates > 0u) {
        renderSyncDirty_ = true;
    }

    if ((activePhaseMask & UiPhase_RenderRecord) != 0u && renderTree_ && world_ &&
        (!config.requireLayoutPhaseForRenderSync || (activePhaseMask & UiPhase_Layout) != 0u)) {
        const bool dirtyObjects = config.ioHasDirtyObjects && *config.ioHasDirtyObjects;
        const bool dirtyLayout = config.ioHasDirtyLayout && *config.ioHasDirtyLayout;
        const bool needSync =
            !config.gatePhasesByDirtyFlags ||
            renderSyncDirty_ ||
            world_->HasAnyDirty() ||
            dirtyObjects ||
            dirtyLayout;
        if (needSync) {
            renderTree_->SetRootDpiScale(dpiScale_);
            result.renderNodesSynced = renderAdapter_.SyncPagedRenderTree(
                *world_, drawBuffer_, *renderTree_, resourceStore_.get());
            telemetry_.renderNodesSynced += result.renderNodesSynced;
            renderSyncDirty_ = false;
        }
    }

    return result;
}

ZeroCopySubmissionStats HostDriverBase::RecordGpuCommands(
    PagedRenderTree& renderTree,
    ZeroCopyGpuStream& gpuStream) {
    telemetry_.gpuStats = gpuStream.SubmitDirtyPages(renderTree);
    return telemetry_.gpuStats;
}

// ============================================================================
// 1. EmbeddedGameHost
// ============================================================================

EmbeddedGameHost::EmbeddedGameHost(
    VisualWorld* world,
    DrawingCommandBuffer* drawBuffer,
    VisualAnimationEngine* animationEngine,
    VisualInputRouter* inputRouter,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceStore> resourceStore) noexcept
    : HostDriverBase(
        world,
        drawBuffer,
        animationEngine,
        inputRouter,
        renderTree,
        std::move(resourceStore),
        Size{1920.0f, 1080.0f}) {}

void EmbeddedGameHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    UiPhaseRunConfig cfg{};
    cfg.runTrackB = true;
    cfg.allowTrackBEcsFastPath = true;
    cfg.requireActiveAnimationsForTick = false;
    cfg.requireLayoutPhaseForRenderSync = false;
    cfg.gatePhasesByDirtyFlags = false;
    (void)RunUiPhases(dt, activePhaseMask, cfg);
}

// ============================================================================
// 2. DesktopEventHost
// ============================================================================

DesktopEventHost::DesktopEventHost(
    VisualWorld* world,
    DrawingCommandBuffer* drawBuffer,
    VisualAnimationEngine* animationEngine,
    VisualInputRouter* inputRouter,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceStore> resourceStore) noexcept
    : HostDriverBase(
        world,
        drawBuffer,
        animationEngine,
        inputRouter,
        renderTree,
        std::move(resourceStore),
        Size{1280.0f, 800.0f}) {}

DesktopEventHost::~DesktopEventHost() = default;

void DesktopEventHost::OnResourceStoreChanged() {
    hasDirtyLayout_ = true;
    TriggerWake();
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
    const bool worldDirty = world_ && world_->HasAnyDirty();
    const bool routerPending = inputRouter_ && inputRouter_->HasPendingEvents();
    const bool animEngineActive = animationEngine_ && animationEngine_->HasActiveAnimations();
    return !hasActiveAnimations_ &&
           !animEngineActive &&
           !hasDirtyObjects_ &&
           !hasDirtyLayout_ &&
           !hasPendingInputEvents_ &&
           !routerPending &&
           !worldDirty;
}

void DesktopEventHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    if (CanSleepIdle()) {
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
    if (renderTree.DirtyPageCount() == 0 && CanSleepIdle()) {
        return ZeroCopySubmissionStats{};
    }
    return HostDriverBase::RecordGpuCommands(renderTree, gpuStream);
}

// ============================================================================
// 3. DesktopImeAdapter, WebImeAdapter, WebFontStreamProvider & WebCanvasHost
// ============================================================================

void DesktopImeAdapter::FocusTextInput(VisualNodeId focusedNode, Rect caretScreenRect) {
    ime_.BeginFocus(focusedNode);
    caretScreenRect_ = caretScreenRect;
    focusSyncCount_ += 1u;
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, ime_.IsImeFocused(), spotSyncUserData_);
    }
}

void DesktopImeAdapter::BlurTextInput() {
    ime_.EndBlur();
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, false, spotSyncUserData_);
    }
}

void DesktopImeAdapter::OnCompositionStart() {
    ime_.BeginComposition();
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
    }
}

void DesktopImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    ime_.UpdateComposition(preeditText, caretOffset);
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
    }
}

void DesktopImeAdapter::OnCompositionEnd(std::string_view committedText) {
    ime_.CommitComposition(committedText);
    focusSyncCount_ += 1u;
    if (spotSyncCallback_) {
        spotSyncCallback_(caretScreenRect_, true, spotSyncUserData_);
    }
}

void WebImeAdapter::FocusTextInput(VisualNodeId focusedNode, Rect caretScreenRect) {
    ime_.BeginFocus(focusedNode);
    baseCaretRect_ = caretScreenRect;
    overlayDomRect_ = caretScreenRect;
}

void WebImeAdapter::BlurTextInput() {
    ime_.EndBlur();
}

void WebImeAdapter::OnCompositionStart() {
    ime_.BeginComposition();
    overlayDomRect_ = baseCaretRect_;
}

void WebImeAdapter::OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) {
    ime_.UpdateComposition(preeditText, caretOffset);
    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(ime_.CommittedText().size() + caretOffset) * 2.0f;
}

void WebImeAdapter::OnCompositionEnd(std::string_view committedText) {
    ime_.CommitComposition(committedText);
    overlayDomRect_.x = baseCaretRect_.x +
        static_cast<float>(ime_.CommittedText().size()) * 2.0f;
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

AriaShadowNode WebImeAdapter::BuildAriaShadowTree(const VisualWorld* world) const {
    if (!world || !world->Root().IsValid()) {
        return AriaShadowNode{};
    }
    return BuildAriaNodeRecursive(world, world->Root());
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
    const std::uint32_t rangeStart = shardId << 8u;
    const std::uint32_t rangeEnd = rangeStart + 0xFFu;
    const std::shared_ptr<UIResourceStore> store = resourceStore_.lock();
    const FontAtlas& atlas = store ? store->Fonts() : FontAtlas::Instance();
    return atlas.ExportGlyphShard(fontFamily, rangeStart, rangeEnd, outShardPayload);
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
        const std::shared_ptr<UIResourceStore> store = resourceStore_.lock();
        FontAtlas& atlas = store ? store->Fonts() : FontAtlas::Instance();
        if (!atlas.UnpackGlyphShard(
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
    VisualWorld* world,
    DrawingCommandBuffer* drawBuffer,
    VisualAnimationEngine* animationEngine,
    VisualInputRouter* inputRouter,
    ShaderBackend webBackend,
    PagedRenderTree* renderTree,
    std::shared_ptr<UIResourceStore> resourceStore)
    : HostDriverBase(
        world,
        drawBuffer,
        animationEngine,
        inputRouter,
        renderTree,
        std::move(resourceStore),
        Size{1280.0f, 720.0f})
    , webBackend_(webBackend) {
    fontStream_.SetResourceStore(resourceStore_);
    fontStream_.SetWakeCallback(
        [](void* ud) noexcept {
            static_cast<WebCanvasHost*>(ud)->RequestWakeFrame();
        },
        this);
}

WebCanvasHost::~WebCanvasHost() {
    fontStream_.SetWakeCallback(nullptr, nullptr);
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

void WebCanvasHost::OnResourceStoreChanged() {
    fontStream_.SetResourceStore(resourceStore_);
}

bool WebCanvasHost::CanSleepIdle() const noexcept {
    if (inputRouter_ && inputRouter_->HasPendingEvents()) {
        return false;
    }
    if (animationEngine_ && animationEngine_->HasActiveAnimations()) {
        return false;
    }
    if (world_ && world_->HasAnyDirty()) {
        return false;
    }
    return rafState_ == WebRafState::SuspendedIdle && !imeAdapter_.IsComposing();
}

void WebCanvasHost::TickUi(float dt, std::uint32_t activePhaseMask) {
    if ((inputRouter_ && inputRouter_->HasPendingEvents()) ||
        (animationEngine_ && animationEngine_->HasActiveAnimations()) ||
        (world_ && world_->HasAnyDirty())) {
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
    cfg.gatePhasesByDirtyFlags = true;
    const UiPhaseRunResult res = RunUiPhases(dt, activePhaseMask, cfg);

    const std::size_t changes = res.TotalChanges();
    const bool animStillRunning = animationEngine_ && animationEngine_->HasActiveAnimations();
    const bool stillDirty = world_ && world_->HasAnyDirty();
    const bool pendingInput = inputRouter_ && inputRouter_->HasPendingEvents();
    if (changes == 0 && !animStillRunning && !stillDirty && !pendingInput && !imeAdapter_.IsComposing()) {
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
