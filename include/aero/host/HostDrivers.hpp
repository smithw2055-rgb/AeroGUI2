#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace aero {

using HostWakeCallback = void (*)(void* userData) noexcept;

enum UiPhaseMask : std::uint32_t {
    UiPhase_None = 0u,
    UiPhase_Input = 1u << 0u,
    UiPhase_SimCommandFlush = 1u << 1u,
    UiPhase_TrackABinding = 1u << 2u,
    UiPhase_TrackBBinding = 1u << 3u,
    UiPhase_MotionStateMachine = 1u << 4u,
    UiPhase_Style = 1u << 5u,
    UiPhase_Animation = 1u << 6u,
    UiPhase_Layout = 1u << 7u,
    UiPhase_RenderRecord = 1u << 8u,
    UiPhase_All = 0x1FFu,
    // Fast HUD refresh mask: only Track B SIMD column scan + dirty 4KB page recording (skips Style & Layout)
    UiPhase_HudFastRefresh = UiPhase_TrackBBinding | UiPhase_RenderRecord,
};

struct HostFrameTelemetry {
    std::uint64_t frameCount = 0;
    std::uint32_t lastActivePhaseMask = 0;
    std::size_t stateMachineTransitions = 0;
    std::size_t stylePassesRun = 0;
    std::size_t styleTriggerChanges = 0;
    std::size_t animationTrackUpdates = 0;
    std::size_t layoutPassesRun = 0;
    std::size_t trackAUpdates = 0;
    std::size_t trackBUpdates = 0;
    std::size_t inputEventsDispatched = 0;
    std::size_t renderNodesSynced = 0;
    ZeroCopySubmissionStats gpuStats{};
};

class IHostDriver {
public:
    virtual ~IHostDriver() = default;

    [[nodiscard]] virtual std::string_view HostName() const noexcept = 0;
    virtual void TickUi(float dt, std::uint32_t activePhaseMask = UiPhase_All) = 0;
    virtual ZeroCopySubmissionStats RecordGpuCommands(
        PagedRenderTree& renderTree,
        ZeroCopyGpuStream& gpuStream) = 0;
    [[nodiscard]] virtual bool CanSleepIdle() const noexcept = 0;

    // P2-1: Optional instance-scoped UiResourceContext binding for multi-viewport / PIE isolation
    virtual void SetResourceContext(std::shared_ptr<UiResourceContext> context) {
        (void)context;
    }
    [[nodiscard]] virtual std::shared_ptr<UiResourceContext> GetResourceContext() const noexcept {
        return nullptr;
    }
};

// ============================================================================
// 1. EmbeddedGameHost (Game Engine In-Process Host)
// ============================================================================
class EmbeddedGameHost final : public IHostDriver {
public:
    EmbeddedGameHost(
        UIElement* rootVisual = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        DepthBucketLayoutQueue* layoutQueue = nullptr,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UiResourceContext> resourceContext = nullptr) noexcept;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "EmbeddedGameHost";
    }

    void SetResourceContext(std::shared_ptr<UiResourceContext> context) override {
        resourceContext_ = std::move(context);
    }
    [[nodiscard]] std::shared_ptr<UiResourceContext> GetResourceContext() const noexcept override {
        return resourceContext_;
    }

    void SetRootVisual(UIElement* root) noexcept {
        ScopedUiResourceContext scope(resourceContext_);
        rootVisual_ = root;
        lastLaidOutViewport_ = {-1.0f, -1.0f};
    }
    [[nodiscard]] UIElement* RootVisual() const noexcept { return rootVisual_; }
    void SetBindingEngine(DualTrackBindingEngine* engine) noexcept { bindingEngine_ = engine; }
    [[nodiscard]] DualTrackBindingEngine* GetBindingEngine() const noexcept { return bindingEngine_; }
    void SetLayoutQueue(DepthBucketLayoutQueue* queue) noexcept { layoutQueue_ = queue; }
    [[nodiscard]] DepthBucketLayoutQueue* GetLayoutQueue() const noexcept { return layoutQueue_; }
    void SetRenderTree(PagedRenderTree* tree) noexcept { renderTree_ = tree; }
    [[nodiscard]] PagedRenderTree* GetRenderTree() const noexcept { return renderTree_; }
    void SetInputRouter(InputEventRouter* router) noexcept { inputRouter_ = router; }
    [[nodiscard]] InputEventRouter* InputRouter() const noexcept { return inputRouter_; }
    void SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept { styleEngine_ = styleEngine; }
    [[nodiscard]] StyleTriggerEngine* StyleEngine() const noexcept { return styleEngine_; }
    void SetAnimationEngine(AnimationEngine* animEngine) noexcept { animationEngine_ = animEngine; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }
    void SetViewportSize(Size viewport) noexcept { viewportSize_ = viewport; }
    [[nodiscard]] Size ViewportSize() const noexcept { return viewportSize_; }

    void TickUi(float dt, std::uint32_t activePhaseMask = UiPhase_All) override;

    ZeroCopySubmissionStats RecordGpuCommands(
        PagedRenderTree& renderTree,
        ZeroCopyGpuStream& gpuStream) override;

    [[nodiscard]] bool CanSleepIdle() const noexcept override { return false; }
    [[nodiscard]] const HostFrameTelemetry& Telemetry() const noexcept { return telemetry_; }

private:
    UIElement* rootVisual_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    DepthBucketLayoutQueue* layoutQueue_ = nullptr;
    PagedRenderTree* renderTree_ = nullptr;
    InputEventRouter* inputRouter_ = nullptr;
    StyleTriggerEngine* styleEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    std::shared_ptr<UiResourceContext> resourceContext_{};
    Size viewportSize_{1920.0f, 1080.0f};
    Size lastLaidOutViewport_{-1.0f, -1.0f};
    HostFrameTelemetry telemetry_{};
};

// ============================================================================
// 2. DesktopEventHost (Zero-Polling Event-Wait Productivity Host)
// ============================================================================
class DesktopEventHost final : public IHostDriver {
public:
    DesktopEventHost(
        UIElement* rootVisual = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        DepthBucketLayoutQueue* layoutQueue = nullptr,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UiResourceContext> resourceContext = nullptr) noexcept;
    ~DesktopEventHost() override;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "DesktopEventHost";
    }

    void SetResourceContext(std::shared_ptr<UiResourceContext> context) override;
    [[nodiscard]] std::shared_ptr<UiResourceContext> GetResourceContext() const noexcept override {
        return resourceContext_;
    }

    void SetRootVisual(UIElement* root) noexcept {
        ScopedUiResourceContext scope(resourceContext_);
        rootVisual_ = root;
        lastLaidOutViewport_ = {-1.0f, -1.0f};
        hasDirtyLayout_ = true;
        TriggerWake();
    }
    [[nodiscard]] UIElement* RootVisual() const noexcept { return rootVisual_; }

    void SetBindingEngine(DualTrackBindingEngine* engine) noexcept { bindingEngine_ = engine; }
    [[nodiscard]] DualTrackBindingEngine* GetBindingEngine() const noexcept { return bindingEngine_; }

    void SetLayoutQueue(DepthBucketLayoutQueue* queue) noexcept { layoutQueue_ = queue; }
    [[nodiscard]] DepthBucketLayoutQueue* GetLayoutQueue() const noexcept { return layoutQueue_; }

    void SetRenderTree(PagedRenderTree* tree) noexcept { renderTree_ = tree; }
    [[nodiscard]] PagedRenderTree* GetRenderTree() const noexcept { return renderTree_; }

    void SetInputRouter(InputEventRouter* router) noexcept { inputRouter_ = router; }
    [[nodiscard]] InputEventRouter* InputRouter() const noexcept { return inputRouter_; }

    void SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept { styleEngine_ = styleEngine; }
    [[nodiscard]] StyleTriggerEngine* StyleEngine() const noexcept { return styleEngine_; }

    void SetAnimationEngine(AnimationEngine* animEngine) noexcept { animationEngine_ = animEngine; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }

    // Register a zero-allocation wake hook (e.g. NativeWindowHost pipe/Win32 Event signaler)
    // so dirty notifications from any thread wake a sleeping OS event loop immediately.
    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        wakeCallback_ = callback;
        wakeUserData_ = userData;
    }
    void TriggerWake() const noexcept {
        if (wakeCallback_) {
            wakeCallback_(wakeUserData_);
        }
    }

    void SetStateFlags(
        bool hasActiveAnimations,
        bool hasDirtyObjects,
        bool hasDirtyLayout,
        bool hasPendingInputEvents = false) noexcept;

    void NotifyInputEvent() noexcept {
        hasPendingInputEvents_ = true;
        TriggerWake();
    }
    void NotifyDirtyObjects() noexcept {
        hasDirtyObjects_ = true;
        TriggerWake();
    }
    void NotifyDirtyLayout() noexcept {
        hasDirtyLayout_ = true;
        TriggerWake();
    }
    void SetActiveAnimations(bool active) noexcept {
        hasActiveAnimations_ = active;
        if (active) {
            TriggerWake();
        }
    }
    void SetViewportSize(Size viewport) noexcept {
        if (viewportSize_ != viewport) {
            viewportSize_ = viewport;
            hasDirtyLayout_ = true;
            TriggerWake();
        }
    }
    [[nodiscard]] Size ViewportSize() const noexcept { return viewportSize_; }

    // True when !hasActiveAnimations && !hasDirtyObjects && !hasDirtyLayout && !hasPendingInputEvents
    [[nodiscard]] bool CanSleepIdle() const noexcept override;

    [[nodiscard]] float CpuPollingUsagePercent() const noexcept {
        return CanSleepIdle() ? 0.0f : 1.0f;
    }
    [[nodiscard]] float GpuPollingUsagePercent() const noexcept {
        return CanSleepIdle() ? 0.0f : 1.0f;
    }

    void TickUi(float dt, std::uint32_t activePhaseMask = UiPhase_All) override;

    ZeroCopySubmissionStats RecordGpuCommands(
        PagedRenderTree& renderTree,
        ZeroCopyGpuStream& gpuStream) override;

    [[nodiscard]] const HostFrameTelemetry& Telemetry() const noexcept { return telemetry_; }

private:
    UIElement* rootVisual_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    DepthBucketLayoutQueue* layoutQueue_ = nullptr;
    PagedRenderTree* renderTree_ = nullptr;
    InputEventRouter* inputRouter_ = nullptr;
    StyleTriggerEngine* styleEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    std::shared_ptr<UiResourceContext> resourceContext_{};
    AxbTemplateRegistry* wakeRegisteredRegistry_ = nullptr;
    HostWakeCallback wakeCallback_ = nullptr;
    void* wakeUserData_ = nullptr;
    Size viewportSize_{1920.0f, 1080.0f};
    Size lastLaidOutViewport_{-1.0f, -1.0f};
    bool hasActiveAnimations_ = false;
    bool hasDirtyObjects_ = false;
    bool hasDirtyLayout_ = false;
    bool hasPendingInputEvents_ = false;
    HostFrameTelemetry telemetry_{};
};

// ============================================================================
// 3. IME Adapters & WebCanvasHost
// ============================================================================
class ITextInputMethodHost {
public:
    virtual ~ITextInputMethodHost() = default;

    virtual void FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) = 0;
    virtual void BlurTextInput() = 0;
    virtual void OnCompositionStart() = 0;
    virtual void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) = 0;
    virtual void OnCompositionEnd(std::string_view committedText) = 0;
};

using NativeImeRectSyncCallback = void (*)(Rect caretScreenRect, bool active, void* userData) noexcept;

// Desktop X11 / Win32 / Headless IME adapter tracking caret screen bounds & composition text
class DesktopImeAdapter final : public ITextInputMethodHost {
public:
    DesktopImeAdapter() = default;

    void FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) override;
    void BlurTextInput() override;
    void OnCompositionStart() override;
    void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) override;
    void OnCompositionEnd(std::string_view committedText) override;

    void SetNativeSpotSyncCallback(NativeImeRectSyncCallback callback, void* userData) noexcept {
        spotSyncCallback_ = callback;
        spotSyncUserData_ = userData;
    }

    [[nodiscard]] bool IsImeActive() const noexcept { return imeFocused_; }
    [[nodiscard]] bool IsComposing() const noexcept { return isComposing_; }
    [[nodiscard]] UIElement* FocusedElement() const noexcept { return focusedElement_; }
    [[nodiscard]] Rect CaretScreenRect() const noexcept { return caretScreenRect_; }
    [[nodiscard]] const std::string& PreeditString() const noexcept { return preeditText_; }
    [[nodiscard]] const std::string& CommittedString() const noexcept { return committedText_; }
    [[nodiscard]] std::uint32_t FocusSyncCount() const noexcept { return focusSyncCount_; }

private:
    UIElement* focusedElement_ = nullptr;
    Rect caretScreenRect_{0.0f, 0.0f, 0.0f, 0.0f};
    bool imeFocused_ = false;
    bool isComposing_ = false;
    std::uint32_t caretOffset_ = 0;
    std::uint32_t focusSyncCount_ = 0;
    std::string preeditText_;
    std::string committedText_;
    std::string compositionBaseText_;
    std::size_t compositionBaseCaret_ = 0;
    NativeImeRectSyncCallback spotSyncCallback_ = nullptr;
    void* spotSyncUserData_ = nullptr;
};

struct AriaShadowNode {
    std::string role;
    std::string label;
    std::string value;
    Rect bounds{0.0f, 0.0f, 0.0f, 0.0f};
    bool focusable = false;
    std::vector<AriaShadowNode> children;
};

class WebImeAdapter final : public ITextInputMethodHost {
public:
    WebImeAdapter() = default;

    void FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) override;
    void BlurTextInput() override;
    void OnCompositionStart() override;
    void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) override;
    void OnCompositionEnd(std::string_view committedText) override;

    [[nodiscard]] bool IsImeActive() const noexcept { return imeFocused_; }
    [[nodiscard]] bool IsComposing() const noexcept { return isComposing_; }
    [[nodiscard]] Rect OverlayDomInputRect() const noexcept { return overlayDomRect_; }
    [[nodiscard]] const std::string& PreeditString() const noexcept { return preeditText_; }
    [[nodiscard]] const std::string& CommittedString() const noexcept { return committedText_; }
    [[nodiscard]] std::string GenerateOverlayCssStyle() const;

    // Browser Clipboard Pre-Caching Bridge
    void SyncClipboardFromDomEvent(std::string_view pastedText);
    [[nodiscard]] std::string_view ReadCachedClipboard() const noexcept { return cachedClipboard_; }
    void WriteClipboardToDom(std::string_view copiedText);

    // Accessible ARIA Shadow Semantic Tree Builder
    [[nodiscard]] AriaShadowNode BuildAriaShadowTree(const UIElement* rootElement) const;

private:
    UIElement* focusedElement_ = nullptr;
    Rect baseCaretRect_{0.0f, 0.0f, 0.0f, 0.0f};
    Rect overlayDomRect_{0.0f, 0.0f, 0.0f, 0.0f};
    bool imeFocused_ = false;
    bool isComposing_ = false;
    std::uint32_t caretOffset_ = 0;
    std::string preeditText_;
    std::string committedText_;
    std::string compositionBaseText_;
    std::size_t compositionBaseCaret_ = 0;
    std::string cachedClipboard_;
};

enum class ShardLoadState : std::uint8_t {
    Unloaded = 0,
    FetchingAsync = 1,
    Loaded = 2,
};

struct UnicodeShardDescriptor {
    std::uint32_t shardId = 0;
    std::uint32_t rangeStart = 0;
    std::uint32_t rangeEnd = 0;
    std::string shardUri;
    ShardLoadState state = ShardLoadState::Unloaded;
    std::size_t loadedByteSize = 0;
};

struct FontShardFetchRequest {
    std::uint32_t shardId = 0;
    std::uint32_t rangeStart = 0;
    std::uint32_t rangeEnd = 0;
    std::string url;
    std::uint32_t requestingRenderNodeId = 0;
};

class WebFontStreamProvider {
public:
    WebFontStreamProvider();

    void SetResourceContext(std::weak_ptr<UiResourceContext> context) noexcept {
        resourceContext_ = std::move(context);
    }

    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        wakeCallback_ = callback;
        wakeUserData_ = userData;
    }

    // Decode UTF-8 codePoints and enqueue non-blocking shard fetch requests for any unloaded CJK range
    std::size_t InspectTextAndQueueMissingShards(
        std::string_view utf8Text,
        std::uint32_t targetRenderNodeId = 0);

    // Export a compact binary "AGFS" glyph shard for shardId ([shardId*256 .. shardId*256+255])
    [[nodiscard]] bool ExportShardPayloadForId(
        std::uint32_t shardId,
        std::string_view fontFamily,
        std::vector<std::uint8_t>& outShardPayload) const;

    // Export a compact binary "AGFS" glyph shard for an arbitrary Unicode codePoint range
    [[nodiscard]] static bool ExportRangeShardPayload(
        std::string_view fontFamily,
        std::uint32_t rangeStart,
        std::uint32_t rangeEnd,
        std::vector<std::uint8_t>& outShardPayload);

    // Complete an async shard fetch, unpack binary SDF glyphs into FontAtlas, and mark affected pages dirty
    bool OnShardDataArrived(
        std::uint32_t shardId,
        const std::vector<std::uint8_t>& shardPayload,
        PagedRenderTree* renderTreeToInvalidate = nullptr);

    [[nodiscard]] ShardLoadState GetCodePointShardState(std::uint32_t codePoint) const noexcept;
    [[nodiscard]] std::uint32_t ComputeShardIdForCodePoint(std::uint32_t codePoint) const noexcept;
    [[nodiscard]] const std::vector<FontShardFetchRequest>& PendingFetchRequests() const noexcept {
        return pendingRequests_;
    }
    [[nodiscard]] std::size_t LoadedShardCount() const noexcept { return loadedShardCount_; }
    [[nodiscard]] std::size_t TotalUnpackedGlyphs() const noexcept { return totalUnpackedGlyphs_; }

private:
    UnicodeShardDescriptor& EnsureShardDescriptor(std::uint32_t codePoint);

    std::weak_ptr<UiResourceContext> resourceContext_{};
    HostWakeCallback wakeCallback_ = nullptr;
    void* wakeUserData_ = nullptr;
    std::unordered_map<std::uint32_t, UnicodeShardDescriptor> shards_;
    std::vector<FontShardFetchRequest> pendingRequests_;
    std::size_t loadedShardCount_ = 0;
    std::size_t totalUnpackedGlyphs_ = 0;
};

enum class WebRafState : std::uint8_t {
    SuspendedIdle = 0,
    ActiveTicking = 1,
};

class WebCanvasHost final : public IHostDriver {
public:
    WebCanvasHost(
        UIElement* rootVisual = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        DepthBucketLayoutQueue* layoutQueue = nullptr,
        ShaderBackend webBackend = ShaderBackend::WebGL2,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UiResourceContext> resourceContext = nullptr);
    ~WebCanvasHost() override;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "WebCanvasHost";
    }

    void SetResourceContext(std::shared_ptr<UiResourceContext> context) override;
    [[nodiscard]] std::shared_ptr<UiResourceContext> GetResourceContext() const noexcept override {
        return resourceContext_;
    }

    void SetRootVisual(UIElement* root) noexcept {
        ScopedUiResourceContext scope(resourceContext_);
        rootVisual_ = root;
        lastLaidOutViewport_ = {-1.0f, -1.0f};
        RequestWakeFrame();
    }
    [[nodiscard]] UIElement* RootVisual() const noexcept { return rootVisual_; }
    void SetRenderTree(PagedRenderTree* tree) noexcept { renderTree_ = tree; }
    [[nodiscard]] PagedRenderTree* GetRenderTree() const noexcept { return renderTree_; }
    void SetInputRouter(InputEventRouter* router) noexcept { inputRouter_ = router; }
    [[nodiscard]] InputEventRouter* InputRouter() const noexcept { return inputRouter_; }
    void SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept { styleEngine_ = styleEngine; }
    [[nodiscard]] StyleTriggerEngine* StyleEngine() const noexcept { return styleEngine_; }
    void SetAnimationEngine(AnimationEngine* animEngine) noexcept { animationEngine_ = animEngine; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }

    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        wakeCallback_ = callback;
        wakeUserData_ = userData;
    }

    // Strictly non-blocking rAF state machine (ASYNCIFY is strictly forbidden)
    [[nodiscard]] static constexpr bool IsAsyncifyEnabled() noexcept { return false; }
    [[nodiscard]] WebRafState RafState() const noexcept { return rafState_; }
    [[nodiscard]] ShaderBackend WebShaderBackend() const noexcept { return webBackend_; }

    void RequestWakeFrame() noexcept {
        rafState_ = WebRafState::ActiveTicking;
        if (wakeCallback_) {
            wakeCallback_(wakeUserData_);
        }
    }
    void SetViewportSize(Size viewport) noexcept {
        if (viewportSize_ != viewport) {
            viewportSize_ = viewport;
            RequestWakeFrame();
        }
    }
    [[nodiscard]] Size ViewportSize() const noexcept { return viewportSize_; }
    void SuspendIfClean(bool hasDirtyState) noexcept {
        rafState_ = hasDirtyState ? WebRafState::ActiveTicking : WebRafState::SuspendedIdle;
    }

    [[nodiscard]] bool CanSleepIdle() const noexcept override {
        if (inputRouter_ && inputRouter_->HasPendingEvents()) {
            return false;
        }
        if (bindingEngine_ && bindingEngine_->HasPendingDirtyObjects()) {
            return false;
        }
        if (animationEngine_ && animationEngine_->HasActiveAnimations()) {
            return false;
        }
        return rafState_ == WebRafState::SuspendedIdle;
    }

    void TickUi(float dt, std::uint32_t activePhaseMask = UiPhase_All) override;

    ZeroCopySubmissionStats RecordGpuCommands(
        PagedRenderTree& renderTree,
        ZeroCopyGpuStream& gpuStream) override;

    [[nodiscard]] WebImeAdapter& ImeAdapter() noexcept { return imeAdapter_; }
    [[nodiscard]] const WebImeAdapter& ImeAdapter() const noexcept { return imeAdapter_; }

    [[nodiscard]] WebFontStreamProvider& FontStream() noexcept { return fontStream_; }
    [[nodiscard]] const WebFontStreamProvider& FontStream() const noexcept { return fontStream_; }

    bool OnFontShardLoaded(
        std::uint32_t shardId,
        const std::vector<std::uint8_t>& shardPayload);

    [[nodiscard]] const HostFrameTelemetry& Telemetry() const noexcept { return telemetry_; }

private:
    UIElement* rootVisual_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    DepthBucketLayoutQueue* layoutQueue_ = nullptr;
    PagedRenderTree* renderTree_ = nullptr;
    InputEventRouter* inputRouter_ = nullptr;
    StyleTriggerEngine* styleEngine_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;
    std::shared_ptr<UiResourceContext> resourceContext_{};
    AxbTemplateRegistry* wakeRegisteredRegistry_ = nullptr;
    HostWakeCallback wakeCallback_ = nullptr;
    void* wakeUserData_ = nullptr;
    ShaderBackend webBackend_ = ShaderBackend::WebGL2;
    WebRafState rafState_ = WebRafState::ActiveTicking;
    WebImeAdapter imeAdapter_{};
    WebFontStreamProvider fontStream_{};
    Size viewportSize_{1280.0f, 720.0f};
    Size lastLaidOutViewport_{-1.0f, -1.0f};
    HostFrameTelemetry telemetry_{};
};

using DesktopHostDriver = DesktopEventHost;
using WasmHostDriver = WebCanvasHost;

} // namespace aero
