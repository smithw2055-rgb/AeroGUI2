#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/host/HostDrivers.hpp"
#include "aero/render/GpuBackend.hpp"

namespace aero {

enum class NativeWindowMode : std::uint8_t {
    Uninitialized = 0,
    LinuxX11OpenGL = 1,
    LinuxX11Software = 2,
    Win32WglOpenGL = 3,
    Win32GdiSoftware = 4,
    EmscriptenWebGL2 = 5,
    HeadlessSoftware = 6,
};

struct NativeWindowConfig {
    std::string title = "AeroGUI 2.0 Window";
    std::string canvasSelector = "#canvas";
    std::uint32_t width = 1280u;
    std::uint32_t height = 720u;
    Color clearColor{0.06f, 0.08f, 0.11f, 1.0f};
    bool preferHardwareGpu = true;
    bool allowHeadlessFallback = true;
    bool forceHeadless = false;
    std::size_t maxPagesInRing = 256u;
    std::uint32_t defaultAtlasSize = 256u;
    float animationFrameIntervalMs = 16.0f;
};

struct NativeWindowTelemetry {
    std::uint64_t eventLoopWakeCount = 0;
    std::uint64_t pipeOrEventWakeCount = 0;
    std::uint64_t osEventsTranslated = 0;
    std::uint64_t framesRendered = 0;
    std::uint64_t idleSleepEntries = 0;
    Rect lastSyncedImeCaretRect{0.0f, 0.0f, 0.0f, 0.0f};
    bool lastImeActive = false;
    GpuBackendFrameStats lastBackendFrame{};
};

// ============================================================================
// NativeWindowHost: Tri-Platform Native/Web Window & Event-Wait Bridge
// - Linux: Dynamic dlopen X11 + GLX/EGL + XPending/XNextEvent + poll() self-pipe 0% CPU wait
//          (gracefully degrades to SoftwareSurfaceBackend when no X11 display is present)
// - Windows (#ifdef _WIN32): Win32 HWND + WGL 3.3 Core + IMM32 IME + MsgWaitForMultipleObjects
// - WebAssembly (#ifdef __EMSCRIPTEN__): WebGL2 + HTML5 Canvas Input/IME + on-demand rAF state machine
// ============================================================================
class NativeWindowHost {
public:
    NativeWindowHost() noexcept;
    ~NativeWindowHost();

    NativeWindowHost(const NativeWindowHost&) = delete;
    NativeWindowHost& operator=(const NativeWindowHost&) = delete;

    // Bind host driver, render tree, GPU stream, and input router
    void BindDesktopHost(
        DesktopEventHost* desktopHost,
        PagedRenderTree* renderTree,
        ZeroCopyGpuStream* gpuStream = nullptr,
        InputEventRouter* inputRouter = nullptr) noexcept;

    void BindWebCanvasHost(
        WebCanvasHost* webHost,
        PagedRenderTree* renderTree,
        ZeroCopyGpuStream* gpuStream = nullptr,
        InputEventRouter* inputRouter = nullptr) noexcept;

    // Optional injection of custom IGpuDeviceBackend (e.g. mock GL spy backend in unit tests)
    void SetCustomGpuBackend(std::unique_ptr<IGpuDeviceBackend> customBackend) noexcept;

    // Optional binding of dedicated UiResourceContext (falls back to desktopHost_/webHost_ context when unset)
    void SetResourceContext(std::shared_ptr<UiResourceContext> context) noexcept {
        resourceContext_ = std::move(context);
    }
    [[nodiscard]] std::shared_ptr<UiResourceContext> GetResourceContext() const noexcept {
        return EffectiveResourceContext();
    }

    bool Initialize(const NativeWindowConfig& config = NativeWindowConfig{});
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return mode_ != NativeWindowMode::Uninitialized; }
    [[nodiscard]] NativeWindowMode Mode() const noexcept { return mode_; }
    [[nodiscard]] std::string_view ModeName() const noexcept;
    [[nodiscard]] bool IsHeadless() const noexcept { return mode_ == NativeWindowMode::HeadlessSoftware; }
    [[nodiscard]] bool ShouldClose() const noexcept {
        return closeRequested_.load(std::memory_order_acquire);
    }
    void RequestClose() noexcept;

    // Thread-safe wake signal: wakes a blocked X11 poll() / Win32 MsgWaitForMultipleObjects / Web rAF
    void WakeEventLoop() noexcept;

    // Pump pending OS events; if DesktopEventHost::CanSleepIdle() is true and maxWaitMs != 0,
    // blocks at 0% CPU until an OS input/window event or WakeEventLoop() arrives (maxWaitMs < 0 waits indefinitely).
    // Immediately runs TickUi + RecordGpuCommands + GPU/Software Present when dirty or woken.
    bool PumpEventsAndRender(float dtSeconds = 0.016f, std::int32_t maxWaitMs = 0);

    // Run event-driven main loop until RequestClose() or maxFrames is reached (0 = unlimited).
    // On Emscripten, registers non-blocking on-demand rAF callbacks and returns immediately (0 ASYNCIFY).
    std::uint64_t RunEventLoop(std::uint64_t maxFrames = 0u);

    // Explicitly execute a single UI + GPU frame tick & swap/present
    GpuBackendFrameStats TickAndPresentFrame(float dtSeconds = 0.016f, bool forcePresent = false);

    // Unified cross-platform input translation & injection entry points (used by OS window procedures,
    // HTML5 DOM listeners, and deterministic headless / CI integration tests)
    void OnPlatformMouseMove(float x, float y, std::uint8_t modifiers) noexcept;
    void OnPlatformMouseButton(
        float x,
        float y,
        PointerButton button,
        bool isDown,
        std::uint8_t modifiers) noexcept;
    void OnPlatformMouseWheel(
        float x,
        float y,
        float scrollDeltaX,
        float scrollDeltaY,
        std::uint8_t modifiers) noexcept;
    void OnPlatformKey(KeyCode key, bool isDown, std::uint8_t modifiers) noexcept;
    void OnPlatformTextInput(std::string_view utf8Text) noexcept;
    void OnPlatformResize(std::uint32_t newWidth, std::uint32_t newHeight) noexcept;

    // Framebuffer readback & PPM/BMP screenshot export
    bool CaptureFramebufferRgba8(
        std::vector<std::uint8_t>& outRgba8,
        std::uint32_t& outWidth,
        std::uint32_t& outHeight) const;
    bool SaveScreenshotPpm(std::string_view filePath) const;
    bool SaveScreenshotBmp(std::string_view filePath) const;

    [[nodiscard]] std::uint32_t Width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t Height() const noexcept { return height_; }
    [[nodiscard]] Color ClearColor() const noexcept { return clearColor_; }
    void SetClearColor(Color color) noexcept { clearColor_ = color; }

    [[nodiscard]] IGpuDeviceBackend* GpuBackend() const noexcept { return gpuBackend_.get(); }
    [[nodiscard]] ZeroCopyGpuStream* GpuStream() noexcept { return activeGpuStream_; }
    [[nodiscard]] DesktopImeAdapter& DesktopIme() noexcept { return desktopIme_; }
    [[nodiscard]] const DesktopImeAdapter& DesktopIme() const noexcept { return desktopIme_; }
    [[nodiscard]] ITextInputMethodHost* ActiveImeHost() noexcept;
    [[nodiscard]] std::string_view LastDiagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const NativeWindowTelemetry& Telemetry() const noexcept { return telemetry_; }

private:
    static void OnHostWakeTrampoline(void* userData) noexcept;
    static void OnDesktopImeSpotSyncTrampoline(Rect caretRect, bool active, void* userData) noexcept;

    bool InitializePlatformWindowAndBackend(const NativeWindowConfig& config);
    void ShutdownPlatformWindow() noexcept;
    void SwapOrBlitPlatformSurface() noexcept;
    void DrainWakeSignal() noexcept;
    bool WaitForEventsOrWake(std::int32_t timeoutMs) noexcept;
    void SyncNativeImeSpot(Rect caretRect, bool active) noexcept;
    [[nodiscard]] bool CanHostSleepIdle() const noexcept;
    void NotifyHostInputArrived() noexcept;
    [[nodiscard]] std::shared_ptr<UiResourceContext> EffectiveResourceContext() const noexcept;

#if defined(_WIN32)
    bool EnsureWin32ImmLoaded() noexcept;
#endif
#if defined(__EMSCRIPTEN__)
    void ScheduleEmscriptenRafIfNeeded() noexcept;
#endif

    DesktopEventHost* desktopHost_ = nullptr;
    WebCanvasHost* webHost_ = nullptr;
    std::shared_ptr<UiResourceContext> resourceContext_{};
    PagedRenderTree* renderTree_ = nullptr;
    InputEventRouter* inputRouter_ = nullptr;
    std::unique_ptr<ZeroCopyGpuStream> ownedGpuStream_;
    ZeroCopyGpuStream* activeGpuStream_ = nullptr;
    std::unique_ptr<IGpuDeviceBackend> gpuBackend_;

    DesktopImeAdapter desktopIme_{};
    NativeWindowConfig config_{};
    NativeWindowMode mode_ = NativeWindowMode::Uninitialized;
    std::uint32_t width_ = 1280u;
    std::uint32_t height_ = 720u;
    Color clearColor_{0.06f, 0.08f, 0.11f, 1.0f};
    Point lastPointerPos_{0.0f, 0.0f};
    std::uint8_t activeModifiers_ = KeyMod_None;

    std::atomic<bool> closeRequested_{false};
    std::atomic<bool> wakeSignaled_{false};
    bool needsInitialRender_ = true;
    std::chrono::steady_clock::time_point lastFrameTimestamp_{};
    bool hasLastFrameTimestamp_ = false;

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    int wakePipeFds_[2]{-1, -1};
#endif
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    struct LinuxX11State;
    std::unique_ptr<LinuxX11State> x11_;
#elif defined(_WIN32)
    void* win32Hwnd_ = nullptr;
    void* win32Hdc_ = nullptr;
    void* win32Hglrc_ = nullptr;
    void* win32OpenglModule_ = nullptr;
    void* win32ImmModule_ = nullptr;
    void* win32ImmGetContext_ = nullptr;
    void* win32ImmSetCompositionWindow_ = nullptr;
    void* win32ImmGetCompositionStringW_ = nullptr;
    void* win32ImmReleaseContext_ = nullptr;
    bool win32ImmProbed_ = false;
    void* win32WakeEvent_ = nullptr;
    std::uint16_t pendingHighSurrogate_ = 0u;
    std::uint8_t win32PressedMouseMask_ = 0u;
#elif defined(__EMSCRIPTEN__)
    std::string emCanvasSelector_;
    int webglContextHandle_ = 0;
    bool rafScheduled_ = false;
    float lastRafTimeMs_ = -1.0f;
#endif

    std::string diagnostics_;
    NativeWindowTelemetry telemetry_{};
};

} // namespace aero
