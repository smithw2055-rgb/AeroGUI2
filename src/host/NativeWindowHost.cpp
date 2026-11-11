#include "aero/host/NativeWindowHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace aero {

namespace {

[[maybe_unused]] void EncodeCodePointToUtf8(std::uint32_t cp, char outBuf[4], std::size_t& outLen) noexcept {
    outLen = 0u;
    if (cp <= 0x7Fu) {
        outBuf[0] = static_cast<char>(cp);
        outLen = 1u;
    } else if (cp <= 0x7FFu) {
        outBuf[0] = static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu));
        outBuf[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        outLen = 2u;
    } else if (cp <= 0xFFFFu) {
        outBuf[0] = static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu));
        outBuf[1] = static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
        outBuf[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
        outLen = 3u;
    } else if (cp <= 0x10FFFFu) {
        outBuf[0] = static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u));
        outBuf[1] = static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu));
        outBuf[2] = static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
        outBuf[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
        outLen = 4u;
    }
}

template <typename FnPtr>
[[maybe_unused]] void BindDynSymbol(FnPtr& outTyped, void* rawSym) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(void*), "Function pointer size mismatch");
    std::memcpy(&outTyped, &rawSym, sizeof(void*));
}

} // namespace

// ============================================================================
// Linux X11 + GLX Dynamic Loader State (0 compile-time libX11/libGL link deps)
// ============================================================================
#if defined(__linux__) && !defined(__EMSCRIPTEN__)

namespace detail::x11_abi {

using XID = unsigned long;
using Atom = unsigned long;
using VisualID = unsigned long;
using Time = unsigned long;
using KeySym = unsigned long;
using X11Window = XID;
using Colormap = XID;
using Drawable = XID;

struct XDisplay;
struct XVisual;
struct XGC;
struct XIM_Opaque;
struct XIC_Opaque;
struct GLXContext_Opaque;
struct GLXFBConfig_Opaque;

constexpr int kKeyPress = 2;
constexpr int kKeyRelease = 3;
constexpr int kButtonPress = 4;
constexpr int kButtonRelease = 5;
constexpr int kMotionNotify = 6;
constexpr int kExpose = 12;
constexpr int kConfigureNotify = 22;
constexpr int kClientMessage = 33;

constexpr long kKeyPressMask = (1L << 0);
constexpr long kKeyReleaseMask = (1L << 1);
constexpr long kButtonPressMask = (1L << 2);
constexpr long kButtonReleaseMask = (1L << 3);
constexpr long kPointerMotionMask = (1L << 6);
constexpr long kExposureMask = (1L << 15);
constexpr long kStructureNotifyMask = (1L << 17);
constexpr long kFocusChangeMask = (1L << 21);

constexpr unsigned int kShiftMask = (1u << 0u);
constexpr unsigned int kControlMask = (1u << 2u);
constexpr unsigned int kMod1Mask = (1u << 3u);

constexpr unsigned long kCWColormap = (1UL << 13u);
constexpr unsigned long kCWEventMask = (1UL << 11u);
constexpr unsigned long kCWBorderPixel = (1UL << 3u);
constexpr int kInputOutput = 1;
constexpr int kZPixmap = 2;

struct XVisualInfo {
    XVisual* visual;
    VisualID visualid;
    int screen;
    int depth;
    int c_class;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
};

struct XSetWindowAttributes {
    XID background_pixmap;
    unsigned long background_pixel;
    XID border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    long event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    Colormap colormap;
    XID cursor;
};

struct XKeyEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    X11Window window;
    X11Window root;
    X11Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
};

struct XButtonEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    X11Window window;
    X11Window root;
    X11Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
};

struct XMotionEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    X11Window window;
    X11Window root;
    X11Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    char is_hint;
    int same_screen;
};

struct XConfigureEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    X11Window event;
    X11Window window;
    int x, y;
    int width, height;
    int border_width;
    X11Window above;
    int override_redirect;
};

struct XClientMessageEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    X11Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
};

union XEvent {
    int type;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XConfigureEvent xconfigure;
    XClientMessageEvent xclient;
    long pad[24];
};

struct XPoint {
    short x;
    short y;
};

struct XImage {
    int width, height;
    int xoffset;
    int format;
    char* data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    char* obdata;
    struct funcs {
        XImage* (*create_image)(XDisplay*, XVisual*, unsigned int, int, int, char*, unsigned int, unsigned int, int, int);
        int (*destroy_image)(XImage*);
        unsigned long (*get_pixel)(XImage*, int, int);
        int (*put_pixel)(XImage*, int, int, unsigned long);
        XImage* (*sub_image)(XImage*, int, int, unsigned int, unsigned int);
        int (*add_pixel)(XImage*, long);
    } f;
};

[[nodiscard]] std::uint8_t TranslateX11Modifiers(unsigned int state) noexcept {
    std::uint8_t mods = KeyMod_None;
    if ((state & kShiftMask) != 0u) mods |= KeyMod_Shift;
    if ((state & kControlMask) != 0u) mods |= KeyMod_Ctrl;
    if ((state & kMod1Mask) != 0u) mods |= KeyMod_Alt;
    return mods;
}

[[nodiscard]] KeyCode TranslateX11KeySym(KeySym ks) noexcept {
    switch (ks) {
        case 0xFF09UL: case 0xFE20UL: return KeyCode::Tab; // XK_Tab / XK_ISO_Left_Tab
        case 0xFF0DUL: case 0xFF8DUL: return KeyCode::Enter;
        case 0x0020UL: return KeyCode::Space;
        case 0xFF1BUL: return KeyCode::Escape;
        case 0xFF08UL: return KeyCode::Backspace;
        case 0xFFFFUL: return KeyCode::Delete;
        case 0xFF51UL: return KeyCode::Left;
        case 0xFF53UL: return KeyCode::Right;
        case 0xFF52UL: return KeyCode::Up;
        case 0xFF54UL: return KeyCode::Down;
        case 0xFF50UL: return KeyCode::Home;
        case 0xFF57UL: return KeyCode::End;
        case 0xFF55UL: return KeyCode::PageUp;
        case 0xFF56UL: return KeyCode::PageDown;
        default: return KeyCode::None;
    }
}

} // namespace detail::x11_abi

struct NativeWindowHost::LinuxX11State {
    void* x11Lib = nullptr;
    void* glLib = nullptr;

    detail::x11_abi::XDisplay* display = nullptr;
    detail::x11_abi::X11Window window = 0;
    detail::x11_abi::Colormap colormap = 0;
    detail::x11_abi::Atom wmDeleteWindow = 0;
    detail::x11_abi::GLXContext_Opaque* glxContext = nullptr;
    detail::x11_abi::XIM_Opaque* xim = nullptr;
    detail::x11_abi::XIC_Opaque* xic = nullptr;
    int screen = 0;
    int x11Fd = -1;
    std::vector<std::uint8_t> swBlitBgraBuffer;

    // X11 function pointers
    detail::x11_abi::XDisplay* (*XOpenDisplay)(const char*) = nullptr;
    int (*XCloseDisplay)(detail::x11_abi::XDisplay*) = nullptr;
    int (*XDefaultScreen)(detail::x11_abi::XDisplay*) = nullptr;
    detail::x11_abi::X11Window (*XRootWindow)(detail::x11_abi::XDisplay*, int) = nullptr;
    int (*XConnectionNumber)(detail::x11_abi::XDisplay*) = nullptr;
    detail::x11_abi::Colormap (*XCreateColormap)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::X11Window,
        detail::x11_abi::XVisual*,
        int) = nullptr;
    int (*XFreeColormap)(detail::x11_abi::XDisplay*, detail::x11_abi::Colormap) = nullptr;
    detail::x11_abi::X11Window (*XCreateWindow)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::X11Window,
        int, int, unsigned int, unsigned int, unsigned int,
        int, unsigned int, detail::x11_abi::XVisual*,
        unsigned long, detail::x11_abi::XSetWindowAttributes*) = nullptr;
    detail::x11_abi::X11Window (*XCreateSimpleWindow)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::X11Window,
        int, int, unsigned int, unsigned int, unsigned int,
        unsigned long, unsigned long) = nullptr;
    int (*XSelectInput)(detail::x11_abi::XDisplay*, detail::x11_abi::X11Window, long) = nullptr;
    int (*XStoreName)(detail::x11_abi::XDisplay*, detail::x11_abi::X11Window, const char*) = nullptr;
    detail::x11_abi::Atom (*XInternAtom)(detail::x11_abi::XDisplay*, const char*, int) = nullptr;
    int (*XSetWMProtocols)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::X11Window,
        detail::x11_abi::Atom*,
        int) = nullptr;
    int (*XMapWindow)(detail::x11_abi::XDisplay*, detail::x11_abi::X11Window) = nullptr;
    int (*XDestroyWindow)(detail::x11_abi::XDisplay*, detail::x11_abi::X11Window) = nullptr;
    int (*XPending)(detail::x11_abi::XDisplay*) = nullptr;
    int (*XNextEvent)(detail::x11_abi::XDisplay*, detail::x11_abi::XEvent*) = nullptr;
    int (*XFlush)(detail::x11_abi::XDisplay*) = nullptr;
    int (*XFree)(void*) = nullptr;
    int (*XLookupString)(
        detail::x11_abi::XKeyEvent*,
        char*,
        int,
        detail::x11_abi::KeySym*,
        void*) = nullptr;
    int (*Xutf8LookupString)(
        detail::x11_abi::XIC_Opaque*,
        detail::x11_abi::XKeyEvent*,
        char*,
        int,
        detail::x11_abi::KeySym*,
        int*) = nullptr;
    detail::x11_abi::XIM_Opaque* (*XOpenIM)(detail::x11_abi::XDisplay*, void*, char*, char*) = nullptr;
    int (*XCloseIM)(detail::x11_abi::XIM_Opaque*) = nullptr;
    detail::x11_abi::XIC_Opaque* (*XCreateIC)(detail::x11_abi::XIM_Opaque*, ...) = nullptr;
    void (*XDestroyIC)(detail::x11_abi::XIC_Opaque*) = nullptr;
    char* (*XSetICValues)(detail::x11_abi::XIC_Opaque*, ...) = nullptr;
    void* (*XVaCreateNestedList)(int, ...) = nullptr;
    int (*XFilterEvent)(detail::x11_abi::XEvent*, detail::x11_abi::X11Window) = nullptr;

    // X11 Software PutImage fallback
    detail::x11_abi::XVisual* (*XDefaultVisual)(detail::x11_abi::XDisplay*, int) = nullptr;
    int (*XDefaultDepth)(detail::x11_abi::XDisplay*, int) = nullptr;
    detail::x11_abi::XGC* (*XDefaultGC)(detail::x11_abi::XDisplay*, int) = nullptr;
    detail::x11_abi::XImage* (*XCreateImage)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::XVisual*,
        unsigned int, int, int, char*,
        unsigned int, unsigned int, int, int) = nullptr;
    int (*XPutImage)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::Drawable,
        detail::x11_abi::XGC*,
        detail::x11_abi::XImage*,
        int, int, int, int, unsigned int, unsigned int) = nullptr;

    // GLX function pointers
    detail::x11_abi::GLXFBConfig_Opaque** (*glXChooseFBConfig)(
        detail::x11_abi::XDisplay*, int, const int*, int*) = nullptr;
    detail::x11_abi::XVisualInfo* (*glXGetVisualFromFBConfig)(
        detail::x11_abi::XDisplay*, detail::x11_abi::GLXFBConfig_Opaque*) = nullptr;
    detail::x11_abi::XVisualInfo* (*glXChooseVisual)(
        detail::x11_abi::XDisplay*, int, int*) = nullptr;
    detail::x11_abi::GLXContext_Opaque* (*glXCreateContext)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::XVisualInfo*,
        detail::x11_abi::GLXContext_Opaque*,
        int) = nullptr;
    detail::x11_abi::GLXContext_Opaque* (*glXCreateContextAttribsARB)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::GLXFBConfig_Opaque*,
        detail::x11_abi::GLXContext_Opaque*,
        int,
        const int*) = nullptr;
    int (*glXMakeCurrent)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::Drawable,
        detail::x11_abi::GLXContext_Opaque*) = nullptr;
    void (*glXSwapBuffers)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::Drawable) = nullptr;
    void (*glXDestroyContext)(
        detail::x11_abi::XDisplay*,
        detail::x11_abi::GLXContext_Opaque*) = nullptr;
    void (*(*glXGetProcAddressARB)(const std::uint8_t*))() = nullptr;
};

#endif // defined(__linux__) && !defined(__EMSCRIPTEN__)

// ============================================================================
// Emscripten HTML5 Canvas & WebGL2 Trampolines (0 fp64 literal keywords)
// ============================================================================
#if defined(__EMSCRIPTEN__)

namespace detail::wasm_abi {

template <typename FnPtr>
struct FirstCallbackParam;

template <typename Ret, typename Arg0, typename... Rest>
struct FirstCallbackParam<Ret (*)(Arg0, Rest...)> {
    using Type = Arg0;
};

using EmRafTimestamp = typename FirstCallbackParam<em_raf_callback_func>::Type;

[[nodiscard]] std::uint8_t TranslateEmModifiers(
    EM_BOOL shiftKey,
    EM_BOOL ctrlKey,
    EM_BOOL altKey) noexcept {
    std::uint8_t mods = KeyMod_None;
    if (shiftKey) mods |= KeyMod_Shift;
    if (ctrlKey) mods |= KeyMod_Ctrl;
    if (altKey) mods |= KeyMod_Alt;
    return mods;
}

[[nodiscard]] KeyCode TranslateDomKeyName(std::string_view domKey) noexcept {
    if (domKey == "Tab") return KeyCode::Tab;
    if (domKey == "Enter") return KeyCode::Enter;
    if (domKey == " " || domKey == "Spacebar") return KeyCode::Space;
    if (domKey == "Escape") return KeyCode::Escape;
    if (domKey == "Backspace") return KeyCode::Backspace;
    if (domKey == "Delete") return KeyCode::Delete;
    if (domKey == "ArrowLeft") return KeyCode::Left;
    if (domKey == "ArrowRight") return KeyCode::Right;
    if (domKey == "ArrowUp") return KeyCode::Up;
    if (domKey == "ArrowDown") return KeyCode::Down;
    if (domKey == "Home") return KeyCode::Home;
    if (domKey == "End") return KeyCode::End;
    if (domKey == "PageUp") return KeyCode::PageUp;
    if (domKey == "PageDown") return KeyCode::PageDown;
    return KeyCode::None;
}

} // namespace detail::wasm_abi

#endif // defined(__EMSCRIPTEN__)

// ============================================================================
// NativeWindowHost Core Lifecycle & Host Bridging
// ============================================================================

NativeWindowHost::NativeWindowHost() noexcept {
    desktopIme_.SetNativeSpotSyncCallback(&NativeWindowHost::OnDesktopImeSpotSyncTrampoline, this);
}

NativeWindowHost::~NativeWindowHost() {
    Shutdown();
}

void NativeWindowHost::OnHostWakeTrampoline(void* userData) noexcept {
    if (auto* self = static_cast<NativeWindowHost*>(userData)) {
        self->WakeEventLoop();
    }
}

void NativeWindowHost::OnDesktopImeSpotSyncTrampoline(
    Rect caretRect,
    bool active,
    void* userData) noexcept {
    if (auto* self = static_cast<NativeWindowHost*>(userData)) {
        self->SyncNativeImeSpot(caretRect, active);
    }
}

void NativeWindowHost::BindDesktopHost(
    DesktopEventHost* desktopHost,
    PagedRenderTree* renderTree,
    ZeroCopyGpuStream* gpuStream,
    InputEventRouter* inputRouter) noexcept {
    if (desktopHost_) {
        desktopHost_->SetWakeCallback(nullptr, nullptr);
    }
    desktopHost_ = desktopHost;
    webHost_ = nullptr;
    renderTree_ = renderTree ? renderTree : (desktopHost ? desktopHost->GetRenderTree() : nullptr);
    inputRouter_ = inputRouter ? inputRouter : (desktopHost ? desktopHost->InputRouter() : nullptr);

    if (gpuStream) {
        activeGpuStream_ = gpuStream;
    } else if (!activeGpuStream_) {
        ownedGpuStream_ = std::make_unique<ZeroCopyGpuStream>(ShaderBackend::DesktopGL33, 256u);
        activeGpuStream_ = ownedGpuStream_.get();
    }

    if (desktopHost_) {
        if (renderTree_ && !desktopHost_->GetRenderTree()) {
            desktopHost_->SetRenderTree(renderTree_);
        }
        if (inputRouter_ && !desktopHost_->InputRouter()) {
            desktopHost_->SetInputRouter(inputRouter_);
        }
        desktopHost_->SetWakeCallback(&NativeWindowHost::OnHostWakeTrampoline, this);
    }
    if (inputRouter_ && !inputRouter_->ImeHost()) {
        inputRouter_->SetImeHost(&desktopIme_);
    }
}

void NativeWindowHost::BindWebCanvasHost(
    WebCanvasHost* webHost,
    PagedRenderTree* renderTree,
    ZeroCopyGpuStream* gpuStream,
    InputEventRouter* inputRouter) noexcept {
    if (webHost_) {
        webHost_->SetWakeCallback(nullptr, nullptr);
    }
    webHost_ = webHost;
    desktopHost_ = nullptr;
    renderTree_ = renderTree ? renderTree : (webHost ? webHost->GetRenderTree() : nullptr);
    inputRouter_ = inputRouter ? inputRouter : (webHost ? webHost->InputRouter() : nullptr);

    if (gpuStream) {
        activeGpuStream_ = gpuStream;
    } else if (!activeGpuStream_) {
        ownedGpuStream_ = std::make_unique<ZeroCopyGpuStream>(ShaderBackend::WebGL2, 256u);
        activeGpuStream_ = ownedGpuStream_.get();
    }

    if (webHost_) {
        if (renderTree_ && !webHost_->GetRenderTree()) {
            webHost_->SetRenderTree(renderTree_);
        }
        if (inputRouter_ && !webHost_->InputRouter()) {
            webHost_->SetInputRouter(inputRouter_);
        }
        webHost_->SetWakeCallback(&NativeWindowHost::OnHostWakeTrampoline, this);
        if (inputRouter_ && !inputRouter_->ImeHost()) {
            inputRouter_->SetImeHost(&webHost_->ImeAdapter());
        }
    }
}

void NativeWindowHost::SetCustomGpuBackend(std::unique_ptr<IGpuDeviceBackend> customBackend) noexcept {
    gpuBackend_ = std::move(customBackend);
}

ITextInputMethodHost* NativeWindowHost::ActiveImeHost() noexcept {
    if (inputRouter_ && inputRouter_->ImeHost()) {
        return inputRouter_->ImeHost();
    }
    if (webHost_) {
        return &webHost_->ImeAdapter();
    }
    return &desktopIme_;
}

std::string_view NativeWindowHost::ModeName() const noexcept {
    switch (mode_) {
        case NativeWindowMode::LinuxX11OpenGL:   return "LinuxX11OpenGL";
        case NativeWindowMode::LinuxX11Software: return "LinuxX11Software";
        case NativeWindowMode::Win32WglOpenGL:   return "Win32WglOpenGL";
        case NativeWindowMode::Win32GdiSoftware: return "Win32GdiSoftware";
        case NativeWindowMode::EmscriptenWebGL2: return "EmscriptenWebGL2";
        case NativeWindowMode::HeadlessSoftware: return "HeadlessSoftware";
        case NativeWindowMode::Uninitialized:
        default:                                 return "Uninitialized";
    }
}

bool NativeWindowHost::Initialize(const NativeWindowConfig& config) {
    ShutdownPlatformWindow();

    config_ = config;
    width_ = std::max<std::uint32_t>(1u, config.width);
    height_ = std::max<std::uint32_t>(1u, config.height);
    clearColor_ = config.clearColor;
    closeRequested_.store(false, std::memory_order_release);
    wakeSignaled_.store(false, std::memory_order_release);
    needsInitialRender_ = true;

    if (!activeGpuStream_) {
#if defined(__EMSCRIPTEN__)
        ownedGpuStream_ = std::make_unique<ZeroCopyGpuStream>(
            ShaderBackend::WebGL2,
            std::max<std::size_t>(16u, config.maxPagesInRing));
#else
        ownedGpuStream_ = std::make_unique<ZeroCopyGpuStream>(
            ShaderBackend::DesktopGL33,
            std::max<std::size_t>(16u, config.maxPagesInRing));
#endif
        activeGpuStream_ = ownedGpuStream_.get();
    }

    if (desktopHost_) {
        desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (webHost_) {
        webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (inputRouter_ && !inputRouter_->ImeHost()) {
        inputRouter_->SetImeHost(webHost_ ? static_cast<ITextInputMethodHost*>(&webHost_->ImeAdapter()) : &desktopIme_);
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (pipe(wakePipeFds_) == 0) {
        for (int fd : wakePipeFds_) {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) {
                (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            }
            const int clo = fcntl(fd, F_GETFD, 0);
            if (clo >= 0) {
                (void)fcntl(fd, F_SETFD, clo | FD_CLOEXEC);
            }
        }
    }
#elif defined(_WIN32)
    win32WakeEvent_ = static_cast<void*>(CreateEventW(nullptr, FALSE, FALSE, nullptr));
#endif

    ScopedUiResourceContext resourceScope(EffectiveResourceContext());
    if (!InitializePlatformWindowAndBackend(config)) {
        Shutdown();
        return false;
    }
    return true;
}

void NativeWindowHost::Shutdown() noexcept {
    if (desktopHost_) {
        desktopHost_->SetWakeCallback(nullptr, nullptr);
    }
    if (webHost_) {
        webHost_->SetWakeCallback(nullptr, nullptr);
    }
    ShutdownPlatformWindow();
}

void NativeWindowHost::ShutdownPlatformWindow() noexcept {
    if (gpuBackend_) {
        gpuBackend_->Shutdown();
        gpuBackend_.reset();
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (x11_) {
        if (x11_->display) {
            if (x11_->glxContext && x11_->glXMakeCurrent && x11_->glXDestroyContext) {
                (void)x11_->glXMakeCurrent(x11_->display, 0, nullptr);
                x11_->glXDestroyContext(x11_->display, x11_->glxContext);
                x11_->glxContext = nullptr;
            }
            if (x11_->xic && x11_->XDestroyIC) {
                x11_->XDestroyIC(x11_->xic);
                x11_->xic = nullptr;
            }
            if (x11_->xim && x11_->XCloseIM) {
                (void)x11_->XCloseIM(x11_->xim);
                x11_->xim = nullptr;
            }
            if (x11_->window != 0 && x11_->XDestroyWindow) {
                (void)x11_->XDestroyWindow(x11_->display, x11_->window);
                x11_->window = 0;
            }
            if (x11_->colormap != 0 && x11_->XFreeColormap) {
                (void)x11_->XFreeColormap(x11_->display, x11_->colormap);
                x11_->colormap = 0;
            }
            if (x11_->XCloseDisplay) {
                (void)x11_->XCloseDisplay(x11_->display);
            }
            x11_->display = nullptr;
        }
        if (x11_->glLib) {
            dlclose(x11_->glLib);
            x11_->glLib = nullptr;
        }
        if (x11_->x11Lib) {
            dlclose(x11_->x11Lib);
            x11_->x11Lib = nullptr;
        }
        x11_.reset();
    }
    for (int& fd : wakePipeFds_) {
        if (fd >= 0) {
            (void)close(fd);
            fd = -1;
        }
    }
#elif defined(_WIN32)
    if (win32Hglrc_) {
        HMODULE glMod = GetModuleHandleA("opengl32.dll");
        if (glMod) {
            auto wglMakeCur = reinterpret_cast<BOOL (WINAPI*)(HDC, HGLRC)>(
                GetProcAddress(glMod, "wglMakeCurrent"));
            auto wglDelCtx = reinterpret_cast<BOOL (WINAPI*)(HGLRC)>(
                GetProcAddress(glMod, "wglDeleteContext"));
            if (wglMakeCur) wglMakeCur(nullptr, nullptr);
            if (wglDelCtx) wglDelCtx(static_cast<HGLRC>(win32Hglrc_));
        }
        win32Hglrc_ = nullptr;
    }
    if (win32Hdc_ && win32Hwnd_) {
        ReleaseDC(static_cast<HWND>(win32Hwnd_), static_cast<HDC>(win32Hdc_));
        win32Hdc_ = nullptr;
    }
    if (win32Hwnd_) {
        DestroyWindow(static_cast<HWND>(win32Hwnd_));
        win32Hwnd_ = nullptr;
    }
    if (win32WakeEvent_) {
        CloseHandle(static_cast<HANDLE>(win32WakeEvent_));
        win32WakeEvent_ = nullptr;
    }
#elif defined(__EMSCRIPTEN__)
    if (webglContextHandle_ > 0) {
        (void)emscripten_webgl_destroy_context(webglContextHandle_);
        webglContextHandle_ = 0;
    }
    rafScheduled_ = false;
#endif

    mode_ = NativeWindowMode::Uninitialized;
}

bool NativeWindowHost::InitializePlatformWindowAndBackend(const NativeWindowConfig& config) {
    GpuBackendConfig gpuCfg{};
    gpuCfg.maxPagesInRing = std::max<std::size_t>(16u, config.maxPagesInRing);
    gpuCfg.defaultAtlasSize = config.defaultAtlasSize;
    gpuCfg.initialViewportWidth = width_;
    gpuCfg.initialViewportHeight = height_;
    gpuCfg.preferHardwareGpu = config.preferHardwareGpu;

    // If caller injected a custom IGpuDeviceBackend (e.g. unit test GL spy), initialize it directly
    if (gpuBackend_) {
        if (!gpuBackend_->IsInitialized() && !gpuBackend_->Initialize(gpuCfg)) {
            diagnostics_ = "Custom GPU backend failed to initialize: " + std::string(gpuBackend_->LastDiagnostics());
            return false;
        }
        mode_ = NativeWindowMode::HeadlessSoftware;
        diagnostics_ = "Initialized with custom injected IGpuDeviceBackend.";
        return true;
    }

    if (config.forceHeadless) {
        gpuCfg.preferHardwareGpu = false;
        auto sw = std::make_unique<SoftwareSurfaceBackend>();
        if (!sw->Initialize(gpuCfg)) {
            diagnostics_ = "SoftwareSurfaceBackend initialization failed.";
            return false;
        }
        gpuBackend_ = std::move(sw);
        mode_ = NativeWindowMode::HeadlessSoftware;
        diagnostics_ = "Forced HeadlessSoftware mode initialized.";
        return true;
    }

#if defined(__EMSCRIPTEN__)
    gpuCfg.shaderBackend = ShaderBackend::WebGL2;
    if (activeGpuStream_) {
        activeGpuStream_->SetBackend(ShaderBackend::WebGL2);
    }

    EmscriptenWebGLContextAttributes attrs{};
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = EM_TRUE;
    attrs.depth = EM_FALSE;
    attrs.stencil = EM_FALSE;
    attrs.antialias = EM_FALSE;
    attrs.premultipliedAlpha = EM_FALSE;
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;

    const char* selector = config.canvasSelector.empty() ? "#canvas" : config.canvasSelector.c_str();
    (void)emscripten_set_canvas_element_size(selector, static_cast<int>(width_), static_cast<int>(height_));
    webglContextHandle_ = emscripten_webgl_create_context(selector, &attrs);
    if (webglContextHandle_ > 0 &&
        emscripten_webgl_make_context_current(webglContextHandle_) == EMSCRIPTEN_RESULT_SUCCESS) {
        auto glBackend = std::make_unique<OpenGlDeviceBackend>();
        if (glBackend->Initialize(gpuCfg)) {
            gpuBackend_ = std::move(glBackend);
            mode_ = NativeWindowMode::EmscriptenWebGL2;

            (void)emscripten_set_mousemove_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenMouseEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    self->OnPlatformMouseMove(
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY),
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_mousedown_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenMouseEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const PointerButton btn = (e->button == 2) ? PointerButton::Right
                        : ((e->button == 1) ? PointerButton::Middle : PointerButton::Left);
                    self->OnPlatformMouseButton(
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY),
                        btn,
                        true,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_mouseup_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenMouseEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const PointerButton btn = (e->button == 2) ? PointerButton::Right
                        : ((e->button == 1) ? PointerButton::Middle : PointerButton::Left);
                    self->OnPlatformMouseButton(
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY),
                        btn,
                        false,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_wheel_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenWheelEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const float scale = (e->deltaMode == DOM_DELTA_LINE) ? 16.0f
                        : ((e->deltaMode == DOM_DELTA_PAGE) ? static_cast<float>(self->Height()) : 1.0f);
                    self->OnPlatformMouseWheel(
                        static_cast<float>(e->mouse.targetX),
                        static_cast<float>(e->mouse.targetY),
                        static_cast<float>(e->deltaX) * scale,
                        static_cast<float>(e->deltaY) * scale,
                        detail::wasm_abi::TranslateEmModifiers(
                            e->mouse.shiftKey, e->mouse.ctrlKey, e->mouse.altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE,
                [](int, const EmscriptenKeyboardEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const std::uint8_t mods = detail::wasm_abi::TranslateEmModifiers(
                        e->shiftKey, e->ctrlKey, e->altKey);
                    const std::string_view keySv(e->key);
                    const KeyCode kc = detail::wasm_abi::TranslateDomKeyName(keySv);
                    if (kc != KeyCode::None) {
                        self->OnPlatformKey(kc, true, mods);
                        return EM_TRUE;
                    }
                    if (!e->ctrlKey && !e->altKey && !e->metaKey && !keySv.empty()) {
                        std::size_t cursor = 0u;
                        std::uint32_t firstCp = 0u;
                        if (DecodeNextUtf8CodePoint(keySv, cursor, firstCp) && cursor == keySv.size() && firstCp >= 0x20u) {
                            self->OnPlatformTextInput(keySv);
                            return EM_TRUE;
                        }
                    }
                    return EM_FALSE;
                });
            (void)emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE,
                [](int, const EmscriptenKeyboardEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const KeyCode kc = detail::wasm_abi::TranslateDomKeyName(e->key);
                    if (kc != KeyCode::None) {
                        self->OnPlatformKey(
                            kc,
                            false,
                            detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                        return EM_TRUE;
                    }
                    return EM_FALSE;
                });

            diagnostics_ = "Emscripten WebGL2 context + HTML5 input listeners initialized.";
            return true;
        }
    }

    if (!config.allowHeadlessFallback) {
        diagnostics_ = "Failed to initialize Emscripten WebGL2 context.";
        return false;
    }
    auto sw = std::make_unique<SoftwareSurfaceBackend>();
    (void)sw->Initialize(gpuCfg);
    gpuBackend_ = std::move(sw);
    mode_ = NativeWindowMode::HeadlessSoftware;
    diagnostics_ = "Emscripten WebGL2 unavailable; degraded to HeadlessSoftware.";
    return true;

#elif defined(_WIN32)
    static bool sWinClassRegistered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    constexpr const wchar_t* kWndClassName = L"AeroGui2NativeWindowClass";

    auto winProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT {
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        auto* self = reinterpret_cast<NativeWindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        auto queryWinMods = []() noexcept -> std::uint8_t {
            std::uint8_t m = KeyMod_None;
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) m |= KeyMod_Shift;
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) m |= KeyMod_Ctrl;
            if ((GetKeyState(VK_MENU) & 0x8000) != 0) m |= KeyMod_Alt;
            return m;
        };
        auto mapVkToKey = [](WPARAM vk) noexcept -> KeyCode {
            switch (vk) {
                case VK_TAB:    return KeyCode::Tab;
                case VK_RETURN: return KeyCode::Enter;
                case VK_SPACE:  return KeyCode::Space;
                case VK_ESCAPE: return KeyCode::Escape;
                case VK_BACK:   return KeyCode::Backspace;
                case VK_DELETE: return KeyCode::Delete;
                case VK_LEFT:   return KeyCode::Left;
                case VK_RIGHT:  return KeyCode::Right;
                case VK_UP:     return KeyCode::Up;
                case VK_DOWN:   return KeyCode::Down;
                case VK_HOME:   return KeyCode::Home;
                case VK_END:    return KeyCode::End;
                case VK_PRIOR:  return KeyCode::PageUp;
                case VK_NEXT:   return KeyCode::PageDown;
                default:        return KeyCode::None;
            }
        };

        switch (msg) {
            case WM_CLOSE:
                self->RequestClose();
                return 0;
            case WM_SIZE: {
                const std::uint32_t nw = static_cast<std::uint32_t>(LOWORD(lParam));
                const std::uint32_t nh = static_cast<std::uint32_t>(HIWORD(lParam));
                if (nw > 0u && nh > 0u) {
                    self->OnPlatformResize(nw, nh);
                }
                return 0;
            }
            case WM_MOUSEMOVE:
                self->OnPlatformMouseMove(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    queryWinMods());
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                if (msg == WM_LBUTTONDOWN) SetCapture(hwnd); else ReleaseCapture();
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Left,
                    msg == WM_LBUTTONDOWN,
                    queryWinMods());
                return 0;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Right,
                    msg == WM_RBUTTONDOWN,
                    queryWinMods());
                return 0;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Middle,
                    msg == WM_MBUTTONDOWN,
                    queryWinMods());
                return 0;
            case WM_MOUSEWHEEL: {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
                self->OnPlatformMouseWheel(
                    static_cast<float>(pt.x),
                    static_cast<float>(pt.y),
                    0.0f,
                    -steps * 40.0f,
                    queryWinMods());
                return 0;
            }
            case WM_MOUSEHWHEEL: {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
                self->OnPlatformMouseWheel(
                    static_cast<float>(pt.x),
                    static_cast<float>(pt.y),
                    steps * 40.0f,
                    0.0f,
                    queryWinMods());
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const KeyCode kc = mapVkToKey(wParam);
                if (kc != KeyCode::None) {
                    self->OnPlatformKey(kc, true, queryWinMods());
                    return 0;
                }
                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const KeyCode kc = mapVkToKey(wParam);
                if (kc != KeyCode::None) {
                    self->OnPlatformKey(kc, false, queryWinMods());
                    return 0;
                }
                break;
            }
            case WM_CHAR: {
                const auto u16 = static_cast<std::uint16_t>(wParam);
                if (u16 >= 0xD800u && u16 <= 0xDBFFu) {
                    self->pendingHighSurrogate_ = u16;
                    return 0;
                }
                std::uint32_t cp = u16;
                if (u16 >= 0xDC00u && u16 <= 0xDFFFu && self->pendingHighSurrogate_ != 0u) {
                    cp = 0x10000u +
                        ((static_cast<std::uint32_t>(self->pendingHighSurrogate_) - 0xD800u) << 10u) +
                        (static_cast<std::uint32_t>(u16) - 0xDC00u);
                    self->pendingHighSurrogate_ = 0u;
                } else {
                    self->pendingHighSurrogate_ = 0u;
                }
                if (cp >= 0x20u && cp != 0x7Fu) {
                    char utf8[4]{};
                    std::size_t len = 0u;
                    EncodeCodePointToUtf8(cp, utf8, len);
                    if (len > 0u) {
                        self->OnPlatformTextInput(std::string_view(utf8, len));
                    }
                }
                return 0;
            }
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    if (!sWinClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = winProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kWndClassName;
        if (RegisterClassExW(&wc) != 0) {
            sWinClassRegistered = true;
        }
    }

    RECT wr{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0,
        kWndClassName,
        L"AeroGUI 2.0",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        hInst,
        this);

    if (hwnd) {
        win32Hwnd_ = hwnd;
        HDC hdc = GetDC(hwnd);
        win32Hdc_ = hdc;

        if (config.preferHardwareGpu && hdc) {
            PIXELFORMATDESCRIPTOR pfd{};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            const int pf = ChoosePixelFormat(hdc, &pfd);
            if (pf != 0 && SetPixelFormat(hdc, pf, &pfd)) {
                HMODULE glMod = LoadLibraryA("opengl32.dll");
                if (glMod) {
                    auto wglCreateCtx = reinterpret_cast<HGLRC (WINAPI*)(HDC)>(
                        GetProcAddress(glMod, "wglCreateContext"));
                    auto wglMakeCur = reinterpret_cast<BOOL (WINAPI*)(HDC, HGLRC)>(
                        GetProcAddress(glMod, "wglMakeCurrent"));
                    auto wglDelCtx = reinterpret_cast<BOOL (WINAPI*)(HGLRC)>(
                        GetProcAddress(glMod, "wglDeleteContext"));
                    auto wglGetProc = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
                        GetProcAddress(glMod, "wglGetProcAddress"));

                    HGLRC tempRc = wglCreateCtx ? wglCreateCtx(hdc) : nullptr;
                    if (tempRc && wglMakeCur && wglMakeCur(hdc, tempRc)) {
                        HGLRC finalRc = tempRc;
                        if (wglGetProc) {
                            using CreateAttribsFn = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
                            auto createAttribs = reinterpret_cast<CreateAttribsFn>(
                                wglGetProc("wglCreateContextAttribsARB"));
                            if (createAttribs) {
                                const int attribs[] = {
                                    0x2091, 3,      // WGL_CONTEXT_MAJOR_VERSION_ARB
                                    0x2092, 3,      // WGL_CONTEXT_MINOR_VERSION_ARB
                                    0x9126, 0x0001, // WGL_CONTEXT_CORE_PROFILE_BIT_ARB
                                    0
                                };
                                if (HGLRC coreRc = createAttribs(hdc, nullptr, attribs)) {
                                    wglMakeCur(hdc, coreRc);
                                    if (wglDelCtx) wglDelCtx(tempRc);
                                    finalRc = coreRc;
                                }
                            }
                        }
                        win32Hglrc_ = finalRc;
                        auto glBackend = std::make_unique<OpenGlDeviceBackend>();
                        if (glBackend->Initialize(gpuCfg)) {
                            gpuBackend_ = std::move(glBackend);
                            mode_ = NativeWindowMode::Win32WglOpenGL;
                            diagnostics_ = "Win32 HWND + WGL OpenGL 3.3 Core context initialized.";
                            return true;
                        }
                    }
                }
            }
        }

        auto sw = std::make_unique<SoftwareSurfaceBackend>();
        if (sw->Initialize(gpuCfg)) {
            gpuBackend_ = std::move(sw);
            mode_ = NativeWindowMode::Win32GdiSoftware;
            diagnostics_ = "Win32 HWND initialized with GDI SoftwareSurfaceBackend presenter.";
            return true;
        }
    }

    if (!config.allowHeadlessFallback) {
        diagnostics_ = "Win32 CreateWindowExW failed and headless fallback is disabled.";
        return false;
    }
    auto sw = std::make_unique<SoftwareSurfaceBackend>();
    (void)sw->Initialize(gpuCfg);
    gpuBackend_ = std::move(sw);
    mode_ = NativeWindowMode::HeadlessSoftware;
    diagnostics_ = "Win32 window unavailable; degraded to HeadlessSoftware.";
    return true;

#elif defined(__linux__)
    x11_ = std::make_unique<LinuxX11State>();
    x11_->x11Lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!x11_->x11Lib) {
        x11_->x11Lib = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    }

    if (x11_->x11Lib) {
        BindDynSymbol(x11_->XOpenDisplay, dlsym(x11_->x11Lib, "XOpenDisplay"));
        BindDynSymbol(x11_->XCloseDisplay, dlsym(x11_->x11Lib, "XCloseDisplay"));
        BindDynSymbol(x11_->XDefaultScreen, dlsym(x11_->x11Lib, "XDefaultScreen"));
        BindDynSymbol(x11_->XRootWindow, dlsym(x11_->x11Lib, "XRootWindow"));
        BindDynSymbol(x11_->XConnectionNumber, dlsym(x11_->x11Lib, "XConnectionNumber"));
        BindDynSymbol(x11_->XCreateColormap, dlsym(x11_->x11Lib, "XCreateColormap"));
        BindDynSymbol(x11_->XFreeColormap, dlsym(x11_->x11Lib, "XFreeColormap"));
        BindDynSymbol(x11_->XCreateWindow, dlsym(x11_->x11Lib, "XCreateWindow"));
        BindDynSymbol(x11_->XCreateSimpleWindow, dlsym(x11_->x11Lib, "XCreateSimpleWindow"));
        BindDynSymbol(x11_->XSelectInput, dlsym(x11_->x11Lib, "XSelectInput"));
        BindDynSymbol(x11_->XStoreName, dlsym(x11_->x11Lib, "XStoreName"));
        BindDynSymbol(x11_->XInternAtom, dlsym(x11_->x11Lib, "XInternAtom"));
        BindDynSymbol(x11_->XSetWMProtocols, dlsym(x11_->x11Lib, "XSetWMProtocols"));
        BindDynSymbol(x11_->XMapWindow, dlsym(x11_->x11Lib, "XMapWindow"));
        BindDynSymbol(x11_->XDestroyWindow, dlsym(x11_->x11Lib, "XDestroyWindow"));
        BindDynSymbol(x11_->XPending, dlsym(x11_->x11Lib, "XPending"));
        BindDynSymbol(x11_->XNextEvent, dlsym(x11_->x11Lib, "XNextEvent"));
        BindDynSymbol(x11_->XFlush, dlsym(x11_->x11Lib, "XFlush"));
        BindDynSymbol(x11_->XFree, dlsym(x11_->x11Lib, "XFree"));
        BindDynSymbol(x11_->XLookupString, dlsym(x11_->x11Lib, "XLookupString"));
        BindDynSymbol(x11_->Xutf8LookupString, dlsym(x11_->x11Lib, "Xutf8LookupString"));
        BindDynSymbol(x11_->XOpenIM, dlsym(x11_->x11Lib, "XOpenIM"));
        BindDynSymbol(x11_->XCloseIM, dlsym(x11_->x11Lib, "XCloseIM"));
        BindDynSymbol(x11_->XCreateIC, dlsym(x11_->x11Lib, "XCreateIC"));
        BindDynSymbol(x11_->XDestroyIC, dlsym(x11_->x11Lib, "XDestroyIC"));
        BindDynSymbol(x11_->XSetICValues, dlsym(x11_->x11Lib, "XSetICValues"));
        BindDynSymbol(x11_->XVaCreateNestedList, dlsym(x11_->x11Lib, "XVaCreateNestedList"));
        BindDynSymbol(x11_->XFilterEvent, dlsym(x11_->x11Lib, "XFilterEvent"));
        BindDynSymbol(x11_->XDefaultVisual, dlsym(x11_->x11Lib, "XDefaultVisual"));
        BindDynSymbol(x11_->XDefaultDepth, dlsym(x11_->x11Lib, "XDefaultDepth"));
        BindDynSymbol(x11_->XDefaultGC, dlsym(x11_->x11Lib, "XDefaultGC"));
        BindDynSymbol(x11_->XCreateImage, dlsym(x11_->x11Lib, "XCreateImage"));
        BindDynSymbol(x11_->XPutImage, dlsym(x11_->x11Lib, "XPutImage"));

        if (x11_->XOpenDisplay) {
            x11_->display = x11_->XOpenDisplay(nullptr);
        }
    }

    if (x11_->display) {
        x11_->screen = x11_->XDefaultScreen ? x11_->XDefaultScreen(x11_->display) : 0;
        x11_->x11Fd = x11_->XConnectionNumber ? x11_->XConnectionNumber(x11_->display) : -1;
        const auto rootWin = x11_->XRootWindow(x11_->display, x11_->screen);

        constexpr long kEventMask =
            detail::x11_abi::kKeyPressMask |
            detail::x11_abi::kKeyReleaseMask |
            detail::x11_abi::kButtonPressMask |
            detail::x11_abi::kButtonReleaseMask |
            detail::x11_abi::kPointerMotionMask |
            detail::x11_abi::kExposureMask |
            detail::x11_abi::kStructureNotifyMask |
            detail::x11_abi::kFocusChangeMask;

        bool glxReady = false;
        if (config.preferHardwareGpu) {
            x11_->glLib = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (!x11_->glLib) {
                x11_->glLib = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
            }
            if (x11_->glLib) {
                BindDynSymbol(x11_->glXChooseFBConfig, dlsym(x11_->glLib, "glXChooseFBConfig"));
                BindDynSymbol(x11_->glXGetVisualFromFBConfig, dlsym(x11_->glLib, "glXGetVisualFromFBConfig"));
                BindDynSymbol(x11_->glXChooseVisual, dlsym(x11_->glLib, "glXChooseVisual"));
                BindDynSymbol(x11_->glXCreateContext, dlsym(x11_->glLib, "glXCreateContext"));
                BindDynSymbol(x11_->glXMakeCurrent, dlsym(x11_->glLib, "glXMakeCurrent"));
                BindDynSymbol(x11_->glXSwapBuffers, dlsym(x11_->glLib, "glXSwapBuffers"));
                BindDynSymbol(x11_->glXDestroyContext, dlsym(x11_->glLib, "glXDestroyContext"));
                BindDynSymbol(x11_->glXGetProcAddressARB, dlsym(x11_->glLib, "glXGetProcAddressARB"));
                if (x11_->glXGetProcAddressARB) {
                    auto extFn = x11_->glXGetProcAddressARB(
                        reinterpret_cast<const std::uint8_t*>("glXCreateContextAttribsARB"));
                    BindDynSymbol(x11_->glXCreateContextAttribsARB, reinterpret_cast<void*>(extFn));
                }

                if (x11_->glXChooseFBConfig && x11_->glXGetVisualFromFBConfig && x11_->glXMakeCurrent) {
                    const int fbAttribs[] = {
                        0x8012, 1,      // GLX_X_RENDERABLE
                        0x8010, 0x0001, // GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT
                        0x8011, 0x0001, // GLX_RENDER_TYPE, GLX_RGBA_BIT
                        8,      8,      // GLX_RED_SIZE
                        9,      8,      // GLX_GREEN_SIZE
                        10,     8,      // GLX_BLUE_SIZE
                        11,     8,      // GLX_ALPHA_SIZE
                        5,      1,      // GLX_DOUBLEBUFFER
                        0
                    };
                    int fbCount = 0;
                    auto** fbc = x11_->glXChooseFBConfig(x11_->display, x11_->screen, fbAttribs, &fbCount);
                    if (fbc && fbCount > 0) {
                        auto* vi = x11_->glXGetVisualFromFBConfig(x11_->display, fbc[0]);
                        if (vi) {
                            x11_->colormap = x11_->XCreateColormap(x11_->display, rootWin, vi->visual, 0);
                            detail::x11_abi::XSetWindowAttributes swa{};
                            swa.colormap = x11_->colormap;
                            swa.border_pixel = 0;
                            swa.event_mask = kEventMask;
                            x11_->window = x11_->XCreateWindow(
                                x11_->display,
                                rootWin,
                                0, 0, width_, height_, 0,
                                vi->depth,
                                detail::x11_abi::kInputOutput,
                                vi->visual,
                                detail::x11_abi::kCWColormap | detail::x11_abi::kCWBorderPixel | detail::x11_abi::kCWEventMask,
                                &swa);

                            if (x11_->window != 0) {
                                if (x11_->glXCreateContextAttribsARB) {
                                    const int ctxAttribs[] = {
                                        0x2091, 3,      // GLX_CONTEXT_MAJOR_VERSION_ARB
                                        0x2092, 3,      // GLX_CONTEXT_MINOR_VERSION_ARB
                                        0x9126, 0x0001, // GLX_CONTEXT_CORE_PROFILE_BIT_ARB
                                        0
                                    };
                                    x11_->glxContext = x11_->glXCreateContextAttribsARB(
                                        x11_->display, fbc[0], nullptr, 1, ctxAttribs);
                                }
                                if (!x11_->glxContext && x11_->glXCreateContext) {
                                    x11_->glxContext = x11_->glXCreateContext(x11_->display, vi, nullptr, 1);
                                }
                                if (x11_->glxContext &&
                                    x11_->glXMakeCurrent(x11_->display, x11_->window, x11_->glxContext) != 0) {
                                    auto glBackend = std::make_unique<OpenGlDeviceBackend>();
                                    if (glBackend->Initialize(gpuCfg)) {
                                        gpuBackend_ = std::move(glBackend);
                                        glxReady = true;
                                    }
                                }
                            }
                            if (x11_->XFree) x11_->XFree(vi);
                        }
                        if (x11_->XFree) x11_->XFree(fbc);
                    }
                }
            }
        }

        if (!glxReady) {
            if (x11_->window == 0 && x11_->XCreateSimpleWindow) {
                x11_->window = x11_->XCreateSimpleWindow(
                    x11_->display, rootWin, 0, 0, width_, height_, 0, 0UL, 0x0F141CUL);
                if (x11_->window != 0 && x11_->XSelectInput) {
                    (void)x11_->XSelectInput(x11_->display, x11_->window, kEventMask);
                }
            }
            auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
            if (!swBackend->Initialize(gpuCfg)) {
                diagnostics_ = "X11 SoftwareSurfaceBackend initialization failed.";
                return false;
            }
            gpuBackend_ = std::move(swBackend);
        }

        if (x11_->window != 0) {
            if (x11_->XStoreName) {
                (void)x11_->XStoreName(x11_->display, x11_->window, config.title.c_str());
            }
            if (x11_->XInternAtom && x11_->XSetWMProtocols) {
                x11_->wmDeleteWindow = x11_->XInternAtom(x11_->display, "WM_DELETE_WINDOW", 0);
                if (x11_->wmDeleteWindow != 0) {
                    (void)x11_->XSetWMProtocols(x11_->display, x11_->window, &x11_->wmDeleteWindow, 1);
                }
            }
            if (x11_->XOpenIM && x11_->XCreateIC) {
                x11_->xim = x11_->XOpenIM(x11_->display, nullptr, nullptr, nullptr);
                if (x11_->xim) {
                    // XIMPreeditNothing | XIMStatusNothing = 0x0008 | 0x0400
                    x11_->xic = x11_->XCreateIC(
                        x11_->xim,
                        "inputStyle", 0x0408L,
                        "clientWindow", x11_->window,
                        "focusWindow", x11_->window,
                        nullptr);
                }
            }
            if (x11_->XMapWindow) {
                (void)x11_->XMapWindow(x11_->display, x11_->window);
            }
            if (x11_->XFlush) {
                (void)x11_->XFlush(x11_->display);
            }
            mode_ = glxReady ? NativeWindowMode::LinuxX11OpenGL : NativeWindowMode::LinuxX11Software;
            diagnostics_ = glxReady
                ? "Linux X11 window + GLX OpenGL 3.3 Core backend initialized."
                : "Linux X11 window + XImage SoftwareSurfaceBackend presenter initialized.";
            return true;
        }
    }

    // No X11 display server available (e.g. DISPLAY="" in headless server / CI container):
    // gracefully fall back to HeadlessSoftware + self-pipe 0% CPU event loop!
    if (!config.allowHeadlessFallback) {
        diagnostics_ = "No X11 display server found and allowHeadlessFallback is false.";
        return false;
    }

    const GlDynamicProbeResult glProbe = ProbeOpenGlDynamicLibrary();
    auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
    if (!swBackend->Initialize(gpuCfg)) {
        diagnostics_ = "SoftwareSurfaceBackend initialization failed during headless fallback.";
        return false;
    }
    gpuBackend_ = std::move(swBackend);
    mode_ = NativeWindowMode::HeadlessSoftware;
    diagnostics_ = "No X11 DISPLAY server detected; gracefully degraded to SoftwareSurfaceBackend (" +
        glProbe.diagnostics + ")";
    return true;
#else
    auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
    if (!swBackend->Initialize(gpuCfg)) {
        return false;
    }
    gpuBackend_ = std::move(swBackend);
    mode_ = NativeWindowMode::HeadlessSoftware;
    diagnostics_ = "Generic HeadlessSoftware mode initialized.";
    return true;
#endif
}

void NativeWindowHost::RequestClose() noexcept {
    closeRequested_.store(true, std::memory_order_release);
    WakeEventLoop();
}

void NativeWindowHost::WakeEventLoop() noexcept {
    const bool alreadySignaled = wakeSignaled_.exchange(true, std::memory_order_acq_rel);
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (!alreadySignaled && wakePipeFds_[1] >= 0) {
        const std::uint8_t wakeByte = 1u;
        const auto written = write(wakePipeFds_[1], &wakeByte, 1u);
        (void)written;
    }
#elif defined(_WIN32)
    if (!alreadySignaled && win32WakeEvent_) {
        SetEvent(static_cast<HANDLE>(win32WakeEvent_));
    }
#elif defined(__EMSCRIPTEN__)
    (void)alreadySignaled;
    ScheduleEmscriptenRafIfNeeded();
#else
    (void)alreadySignaled;
#endif
}

void NativeWindowHost::DrainWakeSignal() noexcept {
    if (wakeSignaled_.exchange(false, std::memory_order_acq_rel)) {
        telemetry_.pipeOrEventWakeCount += 1u;
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (wakePipeFds_[0] >= 0) {
        std::uint8_t drainBuf[64]{};
        while (read(wakePipeFds_[0], drainBuf, sizeof(drainBuf)) > 0) {}
    }
#endif
}

std::shared_ptr<UiResourceContext> NativeWindowHost::EffectiveResourceContext() const noexcept {
    if (resourceContext_) {
        return resourceContext_;
    }
    if (desktopHost_) {
        return desktopHost_->GetResourceContext();
    }
    if (webHost_) {
        return webHost_->GetResourceContext();
    }
    return nullptr;
}

bool NativeWindowHost::CanHostSleepIdle() const noexcept {
    if (needsInitialRender_ || wakeSignaled_.load(std::memory_order_acquire)) {
        return false;
    }
    if (inputRouter_ && inputRouter_->HasPendingEvents()) {
        return false;
    }
    if (renderTree_ && renderTree_->DirtyPageCount() > 0u) {
        return false;
    }
    if (desktopHost_) {
        return desktopHost_->CanSleepIdle();
    }
    if (webHost_) {
        return webHost_->CanSleepIdle();
    }
    return true;
}

void NativeWindowHost::NotifyHostInputArrived() noexcept {
    if (desktopHost_) {
        desktopHost_->NotifyInputEvent();
    } else if (webHost_) {
        webHost_->RequestWakeFrame();
    } else {
        WakeEventLoop();
    }
}

void NativeWindowHost::OnPlatformMouseMove(float x, float y, std::uint8_t modifiers) noexcept {
    lastPointerPos_ = Point{x, y};
    activeModifiers_ = modifiers;
    telemetry_.osEventsTranslated += 1u;
    if (inputRouter_) {
        (void)inputRouter_->QueuePointerMove(lastPointerPos_, modifiers);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::OnPlatformMouseButton(
    float x,
    float y,
    PointerButton button,
    bool isDown,
    std::uint8_t modifiers) noexcept {
    lastPointerPos_ = Point{x, y};
    activeModifiers_ = modifiers;
    telemetry_.osEventsTranslated += 1u;
    if (inputRouter_) {
        (void)inputRouter_->QueuePointerButton(lastPointerPos_, button, isDown, modifiers);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::OnPlatformMouseWheel(
    float x,
    float y,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) noexcept {
    lastPointerPos_ = Point{x, y};
    activeModifiers_ = modifiers;
    telemetry_.osEventsTranslated += 1u;
    if (inputRouter_) {
        (void)inputRouter_->QueueMouseWheel(lastPointerPos_, scrollDeltaX, scrollDeltaY, modifiers);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::OnPlatformKey(KeyCode key, bool isDown, std::uint8_t modifiers) noexcept {
    activeModifiers_ = modifiers;
    telemetry_.osEventsTranslated += 1u;
    if (inputRouter_) {
        (void)inputRouter_->QueueKey(key, isDown, modifiers);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::OnPlatformTextInput(std::string_view utf8Text) noexcept {
    if (utf8Text.empty()) {
        return;
    }
    telemetry_.osEventsTranslated += 1u;
    if (inputRouter_) {
        (void)inputRouter_->QueueTextInput(utf8Text);
    }
    if (webHost_) {
        ScopedUiResourceContext resourceScope(EffectiveResourceContext());
        (void)webHost_->FontStream().InspectTextAndQueueMissingShards(utf8Text);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::OnPlatformResize(std::uint32_t newWidth, std::uint32_t newHeight) noexcept {
    if (newWidth == 0u || newHeight == 0u) {
        return;
    }
    if (width_ == newWidth && height_ == newHeight) {
        return;
    }
    width_ = newWidth;
    height_ = newHeight;
    needsInitialRender_ = true;
    telemetry_.osEventsTranslated += 1u;
    if (desktopHost_) {
        desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (webHost_) {
        webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    WakeEventLoop();
}

void NativeWindowHost::SyncNativeImeSpot(Rect caretRect, bool active) noexcept {
    telemetry_.lastSyncedImeCaretRect = caretRect;
    telemetry_.lastImeActive = active;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (active && x11_ && x11_->xic && x11_->XVaCreateNestedList && x11_->XSetICValues) {
        detail::x11_abi::XPoint spot{};
        spot.x = static_cast<short>(std::clamp(caretRect.x, 0.0f, 32767.0f));
        spot.y = static_cast<short>(std::clamp(caretRect.y + caretRect.height, 0.0f, 32767.0f));
        void* preeditAttr = x11_->XVaCreateNestedList(0, "spotLocation", &spot, nullptr);
        if (preeditAttr) {
            (void)x11_->XSetICValues(x11_->xic, "preeditAttributes", preeditAttr, nullptr);
            if (x11_->XFree) {
                x11_->XFree(preeditAttr);
            }
        }
    }
#elif defined(_WIN32)
    if (active && win32Hwnd_) {
        HMODULE immMod = LoadLibraryA("imm32.dll");
        if (immMod) {
            struct WinCompForm {
                DWORD dwStyle;
                POINT ptCurrentPos;
                RECT rcArea;
            };
            using ImmGetCtxFn = HANDLE (WINAPI*)(HWND);
            using ImmSetCompFn = BOOL (WINAPI*)(HANDLE, const WinCompForm*);
            using ImmRelCtxFn = BOOL (WINAPI*)(HWND, HANDLE);
            auto immGet = reinterpret_cast<ImmGetCtxFn>(GetProcAddress(immMod, "ImmGetContext"));
            auto immSet = reinterpret_cast<ImmSetCompFn>(GetProcAddress(immMod, "ImmSetCompositionWindow"));
            auto immRel = reinterpret_cast<ImmRelCtxFn>(GetProcAddress(immMod, "ImmReleaseContext"));
            if (immGet && immSet && immRel) {
                if (HANDLE himc = immGet(static_cast<HWND>(win32Hwnd_))) {
                    WinCompForm cf{};
                    cf.dwStyle = 0x0002u; // CFS_POINT
                    cf.ptCurrentPos.x = static_cast<LONG>(caretRect.x);
                    cf.ptCurrentPos.y = static_cast<LONG>(caretRect.y);
                    (void)immSet(himc, &cf);
                    (void)immRel(static_cast<HWND>(win32Hwnd_), himc);
                }
            }
            FreeLibrary(immMod);
        }
    }
#endif
}

void NativeWindowHost::SwapOrBlitPlatformSurface() noexcept {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (mode_ == NativeWindowMode::LinuxX11OpenGL && x11_ && x11_->display && x11_->window != 0 && x11_->glXSwapBuffers) {
        x11_->glXSwapBuffers(x11_->display, x11_->window);
    } else if (mode_ == NativeWindowMode::LinuxX11Software && x11_ && x11_->display && x11_->window != 0 &&
               x11_->XCreateImage && x11_->XPutImage && x11_->XDefaultVisual && x11_->XDefaultGC && x11_->XDefaultDepth) {
        std::vector<std::uint8_t> rgba;
        std::uint32_t rw = 0u;
        std::uint32_t rh = 0u;
        if (gpuBackend_ && gpuBackend_->ReadbackFramebufferRgba8(rgba, rw, rh)) {
            x11_->swBlitBgraBuffer.resize(rgba.size());
            const std::size_t pxCount = static_cast<std::size_t>(rw) * rh;
            for (std::size_t i = 0; i < pxCount; ++i) {
                x11_->swBlitBgraBuffer[i * 4u + 0u] = rgba[i * 4u + 2u];
                x11_->swBlitBgraBuffer[i * 4u + 1u] = rgba[i * 4u + 1u];
                x11_->swBlitBgraBuffer[i * 4u + 2u] = rgba[i * 4u + 0u];
                x11_->swBlitBgraBuffer[i * 4u + 3u] = 255u;
            }
            auto* img = x11_->XCreateImage(
                x11_->display,
                x11_->XDefaultVisual(x11_->display, x11_->screen),
                static_cast<unsigned int>(x11_->XDefaultDepth(x11_->display, x11_->screen)),
                detail::x11_abi::kZPixmap,
                0,
                reinterpret_cast<char*>(x11_->swBlitBgraBuffer.data()),
                rw,
                rh,
                32,
                static_cast<int>(rw * 4u));
            if (img) {
                (void)x11_->XPutImage(
                    x11_->display,
                    x11_->window,
                    x11_->XDefaultGC(x11_->display, x11_->screen),
                    img,
                    0, 0, 0, 0, rw, rh);
                img->data = nullptr;
                if (img->f.destroy_image) {
                    (void)img->f.destroy_image(img);
                } else if (x11_->XFree) {
                    x11_->XFree(img);
                }
                if (x11_->XFlush) {
                    (void)x11_->XFlush(x11_->display);
                }
            }
        }
    }
#elif defined(_WIN32)
    if (mode_ == NativeWindowMode::Win32WglOpenGL && win32Hdc_) {
        SwapBuffers(static_cast<HDC>(win32Hdc_));
    } else if (mode_ == NativeWindowMode::Win32GdiSoftware && win32Hdc_) {
        std::vector<std::uint8_t> rgba;
        std::uint32_t rw = 0u;
        std::uint32_t rh = 0u;
        if (gpuBackend_ && gpuBackend_->ReadbackFramebufferRgba8(rgba, rw, rh)) {
            std::vector<std::uint8_t> bgra(rgba.size());
            const std::size_t pxCount = static_cast<std::size_t>(rw) * rh;
            for (std::size_t i = 0; i < pxCount; ++i) {
                bgra[i * 4u + 0u] = rgba[i * 4u + 2u];
                bgra[i * 4u + 1u] = rgba[i * 4u + 1u];
                bgra[i * 4u + 2u] = rgba[i * 4u + 0u];
                bgra[i * 4u + 3u] = 255u;
            }
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(rw);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(rh);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(
                static_cast<HDC>(win32Hdc_),
                0, 0, static_cast<int>(rw), static_cast<int>(rh),
                0, 0, static_cast<int>(rw), static_cast<int>(rh),
                bgra.data(),
                &bmi,
                DIB_RGB_COLORS,
                SRCCOPY);
        }
    }
#endif
}

GpuBackendFrameStats NativeWindowHost::TickAndPresentFrame(float dtSeconds, bool forcePresent) {
    GpuBackendFrameStats frameStats{};
    if (!IsInitialized() || !gpuBackend_ || !activeGpuStream_) {
        return frameStats;
    }
    ScopedUiResourceContext resourceScope(EffectiveResourceContext());

    if (desktopHost_) {
        if (forcePresent && desktopHost_->CanSleepIdle()) {
            desktopHost_->NotifyDirtyLayout();
        }
        desktopHost_->TickUi(dtSeconds, UiPhase_All);
        if (renderTree_) {
            if (forcePresent && renderTree_->DirtyPageCount() == 0u && renderTree_->PageCount() > 0u) {
                (void)renderTree_->InvalidateNodePage(0u);
            }
            const ZeroCopySubmissionStats subStats = desktopHost_->RecordGpuCommands(*renderTree_, *activeGpuStream_);
            if (forcePresent || needsInitialRender_ || subStats.dirtyPagesUploaded > 0u || subStats.drawCallCount > 0u) {
                frameStats = gpuBackend_->PresentStream(
                    *renderTree_,
                    *activeGpuStream_,
                    width_,
                    height_,
                    clearColor_);
                SwapOrBlitPlatformSurface();
                needsInitialRender_ = false;
                telemetry_.framesRendered += 1u;
                telemetry_.lastBackendFrame = frameStats;
            }
        }
    } else if (webHost_) {
        if (forcePresent) {
            webHost_->RequestWakeFrame();
        }
        webHost_->TickUi(dtSeconds, UiPhase_All);
        if (renderTree_) {
            (void)webHost_->RecordGpuCommands(*renderTree_, *activeGpuStream_);
            frameStats = gpuBackend_->PresentStream(
                *renderTree_,
                *activeGpuStream_,
                width_,
                height_,
                clearColor_);
            SwapOrBlitPlatformSurface();
            needsInitialRender_ = false;
            telemetry_.framesRendered += 1u;
            telemetry_.lastBackendFrame = frameStats;
        }
    } else if (renderTree_) {
        if (inputRouter_ && inputRouter_->HasPendingEvents()) {
            (void)inputRouter_->FlushQueuedEvents();
        }
        frameStats = gpuBackend_->SubmitAndPresent(
            *renderTree_,
            *activeGpuStream_,
            width_,
            height_,
            clearColor_);
        SwapOrBlitPlatformSurface();
        needsInitialRender_ = false;
        telemetry_.framesRendered += 1u;
        telemetry_.lastBackendFrame = frameStats;
    }

    // Refresh IME caret screen bounds if a TextBox is currently focused and moved during layout
    if (inputRouter_) {
        if (auto* focused = inputRouter_->FocusedElement()) {
            if (auto* tb = focused->DynamicCast<TextBox>();
                tb && !tb->IsReadOnly() && !tb->IsPassword()) {
                const Rect currentCaret = tb->CaretScreenRect();
                if (!(currentCaret == telemetry_.lastSyncedImeCaretRect) || !telemetry_.lastImeActive) {
                    if (ITextInputMethodHost* ime = ActiveImeHost()) {
                        ime->FocusTextInput(tb, currentCaret);
                    }
                    SyncNativeImeSpot(currentCaret, true);
                }
            }
        } else if (telemetry_.lastImeActive) {
            SyncNativeImeSpot(Rect{}, false);
        }
    }

    return frameStats;
}

bool NativeWindowHost::PumpEventsAndRender(float dtSeconds, std::int32_t maxWaitMs) {
    if (!IsInitialized()) {
        return false;
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    auto drainX11Events = [&]() {
        if (!x11_ || !x11_->display || !x11_->XPending || !x11_->XNextEvent) {
            return;
        }
        while (x11_->XPending(x11_->display) > 0) {
            detail::x11_abi::XEvent ev{};
            x11_->XNextEvent(x11_->display, &ev);
            if (x11_->XFilterEvent && x11_->XFilterEvent(&ev, 0) != 0) {
                continue;
            }
            switch (ev.type) {
                case detail::x11_abi::kMotionNotify:
                    OnPlatformMouseMove(
                        static_cast<float>(ev.xmotion.x),
                        static_cast<float>(ev.xmotion.y),
                        detail::x11_abi::TranslateX11Modifiers(ev.xmotion.state));
                    break;
                case detail::x11_abi::kButtonPress:
                case detail::x11_abi::kButtonRelease: {
                    const bool isDown = (ev.type == detail::x11_abi::kButtonPress);
                    const std::uint8_t mods = detail::x11_abi::TranslateX11Modifiers(ev.xbutton.state);
                    const float mx = static_cast<float>(ev.xbutton.x);
                    const float my = static_cast<float>(ev.xbutton.y);
                    if (ev.xbutton.button == 1u) {
                        OnPlatformMouseButton(mx, my, PointerButton::Left, isDown, mods);
                    } else if (ev.xbutton.button == 2u) {
                        OnPlatformMouseButton(mx, my, PointerButton::Middle, isDown, mods);
                    } else if (ev.xbutton.button == 3u) {
                        OnPlatformMouseButton(mx, my, PointerButton::Right, isDown, mods);
                    } else if (isDown && ev.xbutton.button == 4u) {
                        OnPlatformMouseWheel(mx, my, 0.0f, -40.0f, mods);
                    } else if (isDown && ev.xbutton.button == 5u) {
                        OnPlatformMouseWheel(mx, my, 0.0f, 40.0f, mods);
                    } else if (isDown && ev.xbutton.button == 6u) {
                        OnPlatformMouseWheel(mx, my, -40.0f, 0.0f, mods);
                    } else if (isDown && ev.xbutton.button == 7u) {
                        OnPlatformMouseWheel(mx, my, 40.0f, 0.0f, mods);
                    }
                    break;
                }
                case detail::x11_abi::kKeyPress:
                case detail::x11_abi::kKeyRelease: {
                    const bool isDown = (ev.type == detail::x11_abi::kKeyPress);
                    const std::uint8_t mods = detail::x11_abi::TranslateX11Modifiers(ev.xkey.state);
                    detail::x11_abi::KeySym ks = 0UL;
                    char utf8Buf[64]{};
                    int utf8Len = 0;
                    if (isDown && x11_->xic && x11_->Xutf8LookupString) {
                        int status = 0;
                        utf8Len = x11_->Xutf8LookupString(
                            x11_->xic, &ev.xkey, utf8Buf, static_cast<int>(sizeof(utf8Buf) - 1), &ks, &status);
                    } else if (x11_->XLookupString) {
                        utf8Len = x11_->XLookupString(
                            &ev.xkey, utf8Buf, static_cast<int>(sizeof(utf8Buf) - 1), &ks, nullptr);
                    }
                    const KeyCode kc = detail::x11_abi::TranslateX11KeySym(ks);
                    if (kc != KeyCode::None) {
                        OnPlatformKey(kc, isDown, mods);
                    }
                    if (isDown && utf8Len > 0 && (mods & (KeyMod_Ctrl | KeyMod_Alt)) == 0u) {
                        const auto firstByte = static_cast<std::uint8_t>(utf8Buf[0]);
                        if (firstByte >= 0x20u && firstByte != 0x7Fu) {
                            OnPlatformTextInput(std::string_view(utf8Buf, static_cast<std::size_t>(utf8Len)));
                        }
                    }
                    break;
                }
                case detail::x11_abi::kConfigureNotify:
                    if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0) {
                        OnPlatformResize(
                            static_cast<std::uint32_t>(ev.xconfigure.width),
                            static_cast<std::uint32_t>(ev.xconfigure.height));
                    }
                    break;
                case detail::x11_abi::kExpose:
                    needsInitialRender_ = true;
                    if (desktopHost_) {
                        desktopHost_->NotifyDirtyLayout();
                    }
                    break;
                case detail::x11_abi::kClientMessage:
                    if (x11_->wmDeleteWindow != 0 &&
                        static_cast<detail::x11_abi::Atom>(ev.xclient.data.l[0]) == x11_->wmDeleteWindow) {
                        RequestClose();
                    }
                    break;
                default:
                    break;
            }
        }
    };

    drainX11Events();

    if (CanHostSleepIdle() && maxWaitMs != 0 && !ShouldClose()) {
        telemetry_.idleSleepEntries += 1u;
        struct pollfd fds[2]{};
        nfds_t nfds = 0;
        if (wakePipeFds_[0] >= 0) {
            fds[nfds].fd = wakePipeFds_[0];
            fds[nfds].events = POLLIN;
            ++nfds;
        }
        if (x11_ && x11_->x11Fd >= 0) {
            fds[nfds].fd = x11_->x11Fd;
            fds[nfds].events = POLLIN;
            ++nfds;
        }
        if (nfds > 0) {
            const int rc = poll(fds, nfds, maxWaitMs);
            if (rc > 0) {
                telemetry_.eventLoopWakeCount += 1u;
            }
        }
        drainX11Events();
    } else if (!CanHostSleepIdle()) {
        telemetry_.eventLoopWakeCount += 1u;
    }

    DrainWakeSignal();

#elif defined(_WIN32)
    auto drainWin32Messages = [&]() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                RequestClose();
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    drainWin32Messages();

    if (CanHostSleepIdle() && maxWaitMs != 0 && !ShouldClose()) {
        telemetry_.idleSleepEntries += 1u;
        const DWORD timeoutDw = (maxWaitMs < 0) ? INFINITE : static_cast<DWORD>(maxWaitMs);
        HANDLE handles[1]{static_cast<HANDLE>(win32WakeEvent_)};
        const DWORD handleCount = win32WakeEvent_ ? 1u : 0u;
        const DWORD waitRes = MsgWaitForMultipleObjects(
            handleCount,
            handleCount > 0u ? handles : nullptr,
            FALSE,
            timeoutDw,
            QS_ALLINPUT);
        if (waitRes != WAIT_TIMEOUT) {
            telemetry_.eventLoopWakeCount += 1u;
        }
        drainWin32Messages();
    } else if (!CanHostSleepIdle()) {
        telemetry_.eventLoopWakeCount += 1u;
    }

    DrainWakeSignal();

#elif defined(__EMSCRIPTEN__)
    (void)maxWaitMs;
    DrainWakeSignal();
#endif

    if (ShouldClose()) {
        return false;
    }

    if (!CanHostSleepIdle()) {
        (void)TickAndPresentFrame(dtSeconds, needsInitialRender_);
    }

    return !ShouldClose();
}

#if defined(__EMSCRIPTEN__)
void NativeWindowHost::ScheduleEmscriptenRafIfNeeded() noexcept {
    if (rafScheduled_ || ShouldClose() || mode_ != NativeWindowMode::EmscriptenWebGL2) {
        return;
    }
    rafScheduled_ = true;
    (void)emscripten_request_animation_frame(
        [](detail::wasm_abi::EmRafTimestamp timeMs, void* userData) -> EM_BOOL {
            auto* self = static_cast<NativeWindowHost*>(userData);
            self->rafScheduled_ = false;
            if (self->ShouldClose()) {
                return EM_FALSE;
            }
            const float nowMs = static_cast<float>(timeMs);
            float dtSec = 0.016f;
            if (self->lastRafTimeMs_ >= 0.0f && nowMs > self->lastRafTimeMs_) {
                dtSec = std::clamp((nowMs - self->lastRafTimeMs_) * 0.001f, 0.001f, 0.1f);
            }
            self->lastRafTimeMs_ = nowMs;

            self->DrainWakeSignal();
            (void)self->TickAndPresentFrame(dtSec, self->needsInitialRender_);

            if (!self->CanHostSleepIdle() && !self->ShouldClose()) {
                self->rafScheduled_ = true;
                return EM_TRUE;
            }
            self->telemetry_.idleSleepEntries += 1u;
            return EM_FALSE;
        },
        this);
}
#endif

std::uint64_t NativeWindowHost::RunEventLoop(std::uint64_t maxFrames) {
    if (!IsInitialized()) {
        return 0u;
    }

#if defined(__EMSCRIPTEN__)
    if (mode_ == NativeWindowMode::EmscriptenWebGL2) {
        ScheduleEmscriptenRafIfNeeded();
        return telemetry_.framesRendered;
    }
#endif

    const std::uint64_t startFrames = telemetry_.framesRendered;
    while (!ShouldClose()) {
        if (maxFrames > 0u && (telemetry_.framesRendered - startFrames) >= maxFrames) {
            break;
        }
        const std::int32_t waitMs = CanHostSleepIdle()
            ? -1
            : static_cast<std::int32_t>(std::max(1.0f, config_.animationFrameIntervalMs));
        if (!PumpEventsAndRender(config_.animationFrameIntervalMs * 0.001f, waitMs)) {
            break;
        }
    }
    return telemetry_.framesRendered - startFrames;
}

bool NativeWindowHost::CaptureFramebufferRgba8(
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight) const {
    if (!gpuBackend_) {
        return false;
    }
    return gpuBackend_->ReadbackFramebufferRgba8(outRgba8, outWidth, outHeight);
}

bool NativeWindowHost::SaveScreenshotPpm(std::string_view filePath) const {
    std::vector<std::uint8_t> rgba;
    std::uint32_t w = 0u;
    std::uint32_t h = 0u;
    if (!CaptureFramebufferRgba8(rgba, w, h)) {
        return false;
    }
    return WriteRgba8ToPpm(filePath, w, h, rgba.data());
}

bool NativeWindowHost::SaveScreenshotBmp(std::string_view filePath) const {
    std::vector<std::uint8_t> rgba;
    std::uint32_t w = 0u;
    std::uint32_t h = 0u;
    if (!CaptureFramebufferRgba8(rgba, w, h)) {
        return false;
    }
    return WriteRgba8ToBmp(filePath, w, h, rgba.data());
}

} // namespace aero
