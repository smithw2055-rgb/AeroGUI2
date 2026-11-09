#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace aero {

enum UiPhaseMask : std::uint32_t {
    UiPhase_None = 0u,
    UiPhase_Input = 1u << 0u,
    UiPhase_SimCommandFlush = 1u << 1u,
    UiPhase_TrackABinding = 1u << 2u,
    UiPhase_TrackBBinding = 1u << 3u,
    UiPhase_Animation = 1u << 4u,
    UiPhase_Style = 1u << 5u,
    UiPhase_Layout = 1u << 6u,
    UiPhase_RenderRecord = 1u << 7u,
    UiPhase_All = 0xFFu,
    // Fast HUD refresh mask: only Track B SIMD column scan + dirty 4KB page recording (skips Style & Layout)
    UiPhase_HudFastRefresh = UiPhase_TrackBBinding | UiPhase_RenderRecord,
};

struct HostFrameTelemetry {
    std::uint64_t frameCount = 0;
    std::uint32_t lastActivePhaseMask = 0;
    std::size_t stylePassesRun = 0;
    std::size_t layoutPassesRun = 0;
    std::size_t trackAUpdates = 0;
    std::size_t trackBUpdates = 0;
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
        PagedRenderTree* renderTree = nullptr) noexcept;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "EmbeddedGameHost";
    }

    void SetRootVisual(UIElement* root) noexcept { rootVisual_ = root; }
    void SetBindingEngine(DualTrackBindingEngine* engine) noexcept { bindingEngine_ = engine; }
    void SetLayoutQueue(DepthBucketLayoutQueue* queue) noexcept { layoutQueue_ = queue; }
    void SetRenderTree(PagedRenderTree* tree) noexcept { renderTree_ = tree; }
    void SetViewportSize(Size viewport) noexcept { viewportSize_ = viewport; }

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
    Size viewportSize_{1920.0f, 1080.0f};
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
        DepthBucketLayoutQueue* layoutQueue = nullptr) noexcept;

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "DesktopEventHost";
    }

    void SetStateFlags(
        bool hasActiveAnimations,
        bool hasDirtyObjects,
        bool hasDirtyLayout,
        bool hasPendingInputEvents = false) noexcept;

    void NotifyInputEvent() noexcept { hasPendingInputEvents_ = true; }
    void NotifyDirtyObjects() noexcept { hasDirtyObjects_ = true; }
    void NotifyDirtyLayout() noexcept { hasDirtyLayout_ = true; }
    void SetActiveAnimations(bool active) noexcept { hasActiveAnimations_ = active; }

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
    Size viewportSize_{1920.0f, 1080.0f};
    bool hasActiveAnimations_ = false;
    bool hasDirtyObjects_ = false;
    bool hasDirtyLayout_ = false;
    bool hasPendingInputEvents_ = false;
    HostFrameTelemetry telemetry_{};
};

// ============================================================================
// 3. WebCanvasHost: Transparent DOM Bridge (IME + Clipboard + ARIA) & CJK Font Shards
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

    // Decode UTF-8 codePoints and enqueue non-blocking shard fetch requests for any unloaded CJK range
    std::size_t InspectTextAndQueueMissingShards(
        std::string_view utf8Text,
        std::uint32_t targetRenderNodeId = 0);

    // Complete an async shard fetch and mark affected PagedRenderTree nodes/pages dirty
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

private:
    UnicodeShardDescriptor& EnsureShardDescriptor(std::uint32_t codePoint);

    std::unordered_map<std::uint32_t, UnicodeShardDescriptor> shards_;
    std::vector<FontShardFetchRequest> pendingRequests_;
    std::size_t loadedShardCount_ = 0;
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
        ShaderBackend webBackend = ShaderBackend::WebGL2);

    [[nodiscard]] std::string_view HostName() const noexcept override {
        return "WebCanvasHost";
    }

    // Strictly non-blocking rAF state machine (ASYNCIFY is strictly forbidden)
    [[nodiscard]] static constexpr bool IsAsyncifyEnabled() noexcept { return false; }
    [[nodiscard]] WebRafState RafState() const noexcept { return rafState_; }
    [[nodiscard]] ShaderBackend WebShaderBackend() const noexcept { return webBackend_; }

    void RequestWakeFrame() noexcept { rafState_ = WebRafState::ActiveTicking; }
    void SuspendIfClean(bool hasDirtyState) noexcept {
        rafState_ = hasDirtyState ? WebRafState::ActiveTicking : WebRafState::SuspendedIdle;
    }

    [[nodiscard]] bool CanSleepIdle() const noexcept override {
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

    [[nodiscard]] const HostFrameTelemetry& Telemetry() const noexcept { return telemetry_; }

private:
    UIElement* rootVisual_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    DepthBucketLayoutQueue* layoutQueue_ = nullptr;
    ShaderBackend webBackend_ = ShaderBackend::WebGL2;
    WebRafState rafState_ = WebRafState::ActiveTicking;
    WebImeAdapter imeAdapter_{};
    WebFontStreamProvider fontStream_{};
    Size viewportSize_{1280.0f, 720.0f};
    HostFrameTelemetry telemetry_{};
};

} // namespace aero
