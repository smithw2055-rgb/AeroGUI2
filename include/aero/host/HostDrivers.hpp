#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualRenderAdapter.hpp"
#include "aero/visual/VisualTree.hpp"

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

    virtual void SetResourceStore(std::shared_ptr<UIResourceStore> store) {
        (void)store;
    }
    [[nodiscard]] virtual std::shared_ptr<UIResourceStore> GetResourceStore() const noexcept {
        return nullptr;
    }
};

class HostDriverBase : public IHostDriver {
public:
    HostDriverBase(
        VisualWorld* world = nullptr,
        DrawingCommandBuffer* drawBuffer = nullptr,
        VisualAnimationEngine* animationEngine = nullptr,
        VisualInputRouter* inputRouter = nullptr,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UIResourceStore> resourceStore = nullptr,
        Size initialViewportSize = Size{1920.0f, 1080.0f}) noexcept;
    ~HostDriverBase() override;

    void SetResourceStore(std::shared_ptr<UIResourceStore> store) override;
    [[nodiscard]] std::shared_ptr<UIResourceStore> GetResourceStore() const noexcept override {
        return resourceStore_;
    }

    void SetVisualWorld(VisualWorld* world) noexcept;
    [[nodiscard]] VisualWorld* GetVisualWorld() const noexcept { return world_; }

    void SetDrawBuffer(DrawingCommandBuffer* drawBuffer) noexcept { drawBuffer_ = drawBuffer; }
    [[nodiscard]] DrawingCommandBuffer* GetDrawBuffer() const noexcept { return drawBuffer_; }

    void SetRenderTree(PagedRenderTree* tree) noexcept;
    [[nodiscard]] PagedRenderTree* GetRenderTree() const noexcept { return renderTree_; }

    void SetInputRouter(VisualInputRouter* router) noexcept { inputRouter_ = router; }
    [[nodiscard]] VisualInputRouter* InputRouter() const noexcept { return inputRouter_; }

    void SetAnimationEngine(VisualAnimationEngine* animEngine) noexcept { animationEngine_ = animEngine; }
    [[nodiscard]] VisualAnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }

    void SetDomains(ExecutionDomainContext* domains) noexcept { domains_ = domains; }
    [[nodiscard]] ExecutionDomainContext* Domains() const noexcept { return domains_; }

    void SetViewportSize(Size viewport) noexcept;
    [[nodiscard]] Size ViewportSize() const noexcept { return viewportSize_; }

    void SetDpiScale(float scale) noexcept;
    [[nodiscard]] float DpiScale() const noexcept { return dpiScale_; }
    [[nodiscard]] Size LogicalViewportSize() const noexcept {
        return Size{viewportSize_.width / dpiScale_, viewportSize_.height / dpiScale_};
    }

    ZeroCopySubmissionStats RecordGpuCommands(
        PagedRenderTree& renderTree,
        ZeroCopyGpuStream& gpuStream) override;

    [[nodiscard]] const HostFrameTelemetry& Telemetry() const noexcept { return telemetry_; }

protected:
    struct UiPhaseRunConfig {
        bool runTrackB = true;
        bool allowTrackBEcsFastPath = false;
        bool requireActiveAnimationsForTick = false;
        bool requireLayoutPhaseForRenderSync = false;
        bool gatePhasesByDirtyFlags = false;
        bool* ioHasDirtyObjects = nullptr;
        bool* ioHasDirtyLayout = nullptr;
    };

    struct UiPhaseRunResult {
        std::size_t inputDispatched = 0u;
        std::size_t liveTickUpdated = 0u;
        std::size_t trackAUpdates = 0u;
        std::size_t trackBUpdates = 0u;
        std::size_t stateMachineTransitions = 0u;
        std::size_t styleChanges = 0u;
        std::size_t animationUpdates = 0u;
        std::size_t layoutUpdates = 0u;
        std::size_t renderNodesSynced = 0u;

        [[nodiscard]] std::size_t TotalChanges() const noexcept {
            return inputDispatched + liveTickUpdated + trackAUpdates + trackBUpdates +
                   stateMachineTransitions + styleChanges + animationUpdates +
                   layoutUpdates + renderNodesSynced;
        }
    };

    virtual void OnRootVisualChanged() noexcept {}
    virtual void OnViewportOrDpiChanged() noexcept {}
    virtual void OnResourceStoreChanged() {}

    [[nodiscard]] UiPhaseRunResult RunUiPhases(
        float dt,
        std::uint32_t activePhaseMask,
        const UiPhaseRunConfig& config);

    void SetWakeCallbackImpl(HostWakeCallback callback, void* userData) noexcept {
        wakeCallback_ = callback;
        wakeUserData_ = userData;
    }
    void InvokeWakeCallback() const noexcept {
        if (wakeCallback_) {
            wakeCallback_(wakeUserData_);
        }
    }

    VisualWorld* world_ = nullptr;
    DrawingCommandBuffer* drawBuffer_ = nullptr;
    VisualAnimationEngine* animationEngine_ = nullptr;
    VisualInputRouter* inputRouter_ = nullptr;
    ExecutionDomainContext* domains_ = nullptr;
    PagedRenderTree* renderTree_ = nullptr;
    VisualRenderAdapter renderAdapter_{};
    std::shared_ptr<UIResourceStore> resourceStore_{};

    HostWakeCallback wakeCallback_ = nullptr;
    void* wakeUserData_ = nullptr;
    float dpiScale_ = 1.0f;
    Size viewportSize_{1920.0f, 1080.0f};
    Size lastLaidOutViewport_{-1.0f, -1.0f};
    bool renderSyncDirty_ = true;
    HostFrameTelemetry telemetry_{};
};

// ============================================================================
// 1. EmbeddedGameHost (Game Engine In-Process Host)
// ============================================================================
class EmbeddedGameHost final : public HostDriverBase {
public:
    EmbeddedGameHost(
        VisualWorld* world = nullptr,
        DrawingCommandBuffer* drawBuffer = nullptr,
        VisualAnimationEngine* animationEngine = nullptr,
        VisualInputRouter* inputRouter = nullptr,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UIResourceStore> resourceStore = nullptr) noexcept;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "EmbeddedGameHost";
    }

    void TickUi(float dt, std::uint32_t activePhaseMask = UiPhase_All) override;

    [[nodiscard]] bool CanSleepIdle() const noexcept override { return false; }
};

// ============================================================================
// 2. DesktopEventHost (Zero-Polling Event-Wait Productivity Host)
// ============================================================================
class DesktopEventHost final : public HostDriverBase {
public:
    DesktopEventHost(
        VisualWorld* world = nullptr,
        DrawingCommandBuffer* drawBuffer = nullptr,
        VisualAnimationEngine* animationEngine = nullptr,
        VisualInputRouter* inputRouter = nullptr,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UIResourceStore> resourceStore = nullptr) noexcept;
    ~DesktopEventHost() override;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "DesktopEventHost";
    }

    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        SetWakeCallbackImpl(callback, userData);
    }
    void TriggerWake() const noexcept {
        InvokeWakeCallback();
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

protected:
    void OnRootVisualChanged() noexcept override {
        hasDirtyLayout_ = true;
        TriggerWake();
    }
    void OnViewportOrDpiChanged() noexcept override {
        hasDirtyLayout_ = true;
        renderSyncDirty_ = true;
        TriggerWake();
    }
    void OnResourceStoreChanged() override;

private:
    bool hasActiveAnimations_ = false;
    bool hasDirtyObjects_ = false;
    bool hasDirtyLayout_ = false;
    bool hasPendingInputEvents_ = false;
};

// ============================================================================
// 3. IME Adapters & WebCanvasHost
// ============================================================================
using NativeImeRectSyncCallback = void (*)(Rect caretScreenRect, bool active, void* userData) noexcept;

class DesktopImeAdapter final : public ITextInputMethodHost {
public:
    DesktopImeAdapter() = default;

    void FocusTextInput(VisualNodeId focusedNode, Rect caretScreenRect) override;
    void BlurTextInput() override;
    void OnCompositionStart() override;
    void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) override;
    void OnCompositionEnd(std::string_view committedText) override;

    void SetNativeSpotSyncCallback(NativeImeRectSyncCallback callback, void* userData) noexcept {
        spotSyncCallback_ = callback;
        spotSyncUserData_ = userData;
    }

    [[nodiscard]] bool IsImeActive() const noexcept { return ime_.IsImeFocused(); }
    [[nodiscard]] bool IsComposing() const noexcept { return ime_.IsComposing(); }
    [[nodiscard]] VisualNodeId FocusedNode() const noexcept { return ime_.FocusedNode(); }
    [[nodiscard]] Rect CaretScreenRect() const noexcept { return caretScreenRect_; }
    [[nodiscard]] const std::string& PreeditString() const noexcept { return ime_.PreeditText(); }
    [[nodiscard]] const std::string& CommittedString() const noexcept { return ime_.CommittedText(); }
    [[nodiscard]] std::uint32_t FocusSyncCount() const noexcept { return focusSyncCount_; }

private:
    ImeCompositionController ime_{};
    Rect caretScreenRect_{0.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t focusSyncCount_ = 0;
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

    void FocusTextInput(VisualNodeId focusedNode, Rect caretScreenRect) override;
    void BlurTextInput() override;
    void OnCompositionStart() override;
    void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) override;
    void OnCompositionEnd(std::string_view committedText) override;

    [[nodiscard]] bool IsImeActive() const noexcept { return ime_.IsImeFocused(); }
    [[nodiscard]] bool IsComposing() const noexcept { return ime_.IsComposing(); }
    [[nodiscard]] Rect OverlayDomInputRect() const noexcept { return overlayDomRect_; }
    [[nodiscard]] const std::string& PreeditString() const noexcept { return ime_.PreeditText(); }
    [[nodiscard]] const std::string& CommittedString() const noexcept { return ime_.CommittedText(); }
    [[nodiscard]] std::string GenerateOverlayCssStyle() const;

    void SyncClipboardFromDomEvent(std::string_view pastedText);
    [[nodiscard]] std::string_view ReadCachedClipboard() const noexcept { return cachedClipboard_; }
    void WriteClipboardToDom(std::string_view copiedText);

    [[nodiscard]] AriaShadowNode BuildAriaShadowTree(const VisualWorld* world) const;

private:
    ImeCompositionController ime_{};
    Rect baseCaretRect_{0.0f, 0.0f, 0.0f, 0.0f};
    Rect overlayDomRect_{0.0f, 0.0f, 0.0f, 0.0f};
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

    void SetResourceStore(std::weak_ptr<UIResourceStore> store) noexcept {
        resourceStore_ = std::move(store);
    }

    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        wakeCallback_ = callback;
        wakeUserData_ = userData;
    }

    std::size_t InspectTextAndQueueMissingShards(
        std::string_view utf8Text,
        std::uint32_t targetRenderNodeId = 0);

    [[nodiscard]] bool ExportShardPayloadForId(
        std::uint32_t shardId,
        std::string_view fontFamily,
        std::vector<std::uint8_t>& outShardPayload) const;

    [[nodiscard]] static bool ExportRangeShardPayload(
        std::string_view fontFamily,
        std::uint32_t rangeStart,
        std::uint32_t rangeEnd,
        std::vector<std::uint8_t>& outShardPayload);

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

    std::weak_ptr<UIResourceStore> resourceStore_{};
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

class WebCanvasHost final : public HostDriverBase {
public:
    WebCanvasHost(
        VisualWorld* world = nullptr,
        DrawingCommandBuffer* drawBuffer = nullptr,
        VisualAnimationEngine* animationEngine = nullptr,
        VisualInputRouter* inputRouter = nullptr,
        ShaderBackend webBackend = ShaderBackend::WebGL2,
        PagedRenderTree* renderTree = nullptr,
        std::shared_ptr<UIResourceStore> resourceStore = nullptr);
    ~WebCanvasHost() override;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "WebCanvasHost";
    }

    void SetWakeCallback(HostWakeCallback callback, void* userData) noexcept {
        SetWakeCallbackImpl(callback, userData);
    }

    [[nodiscard]] static constexpr bool IsAsyncifyEnabled() noexcept { return false; }
    [[nodiscard]] WebRafState RafState() const noexcept { return rafState_; }
    [[nodiscard]] ShaderBackend WebShaderBackend() const noexcept { return webBackend_; }

    void RequestWakeFrame() noexcept {
        rafState_ = WebRafState::ActiveTicking;
        InvokeWakeCallback();
    }
    void SuspendIfClean(bool hasDirtyState) noexcept {
        rafState_ = hasDirtyState ? WebRafState::ActiveTicking : WebRafState::SuspendedIdle;
    }

    [[nodiscard]] bool CanSleepIdle() const noexcept override;

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

protected:
    void OnRootVisualChanged() noexcept override {
        RequestWakeFrame();
    }
    void OnViewportOrDpiChanged() noexcept override {
        RequestWakeFrame();
    }
    void OnResourceStoreChanged() override;

private:
    ShaderBackend webBackend_ = ShaderBackend::WebGL2;
    WebRafState rafState_ = WebRafState::ActiveTicking;
    WebImeAdapter imeAdapter_{};
    WebFontStreamProvider fontStream_{};
};

using DesktopHostDriver = DesktopEventHost;
using WasmHostDriver = WebCanvasHost;

} // namespace aero
