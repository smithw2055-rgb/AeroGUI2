#include "aero/host/NativeWindowHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_map>

#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/visual/VisualInputRouter.hpp"

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
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <objbase.h>
#elif defined(__linux__) || defined(__APPLE__)
#if defined(__linux__)
#include <dlfcn.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace aero {

namespace {

[[maybe_unused]] void EncodeCodePointToUtf8(std::uint32_t cp, char outBuf[4], std::size_t& outLen) noexcept {
    outLen = 0u;
    if (cp >= 0xD800u && cp <= 0xDFFFu) {
        return;
    }
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

#if defined(_WIN32)
[[nodiscard]] std::string ConvertUtf16SliceToUtf8(
    const wchar_t* wstr,
    std::size_t wlen,
    std::size_t* outCodePointCount = nullptr) {
    std::string out;
    if (outCodePointCount) {
        *outCodePointCount = 0u;
    }
    if (!wstr || wlen == 0u) {
        return out;
    }
    out.reserve(wlen * 3u);
    std::size_t cpTotal = 0u;
    for (std::size_t i = 0u; i < wlen; ++i) {
        const auto u16 = static_cast<std::uint16_t>(wstr[i]);
        if (u16 == 0u) {
            break;
        }
        std::uint32_t cp = u16;
        if (u16 >= 0xD800u && u16 <= 0xDBFFu) {
            if (i + 1u < wlen) {
                const auto low = static_cast<std::uint16_t>(wstr[i + 1u]);
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u +
                        ((static_cast<std::uint32_t>(u16) - 0xD800u) << 10u) +
                        (static_cast<std::uint32_t>(low) - 0xDC00u);
                    ++i;
                } else {
                    continue; // Reject lone high surrogate
                }
            } else {
                continue; // Reject trailing lone high surrogate
            }
        } else if (u16 >= 0xDC00u && u16 <= 0xDFFFu) {
            continue; // Reject lone low surrogate
        }
        char utf8[4]{};
        std::size_t len = 0u;
        EncodeCodePointToUtf8(cp, utf8, len);
        if (len > 0u) {
            out.append(utf8, len);
            ++cpTotal;
        }
    }
    if (outCodePointCount) {
        *outCodePointCount = cpTotal;
    }
    return out;
}
#endif

#if defined(_WIN32)
void EnableWin32PerMonitorDpiV2() noexcept {
    static bool sAttempted = false;
    if (sAttempted) {
        return;
    }
    sAttempted = true;

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiCtxFn = BOOL (WINAPI*)(HANDLE);
        if (auto setCtx = reinterpret_cast<SetDpiCtxFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (DPI_AWARENESS_CONTEXT)-4
            if (setCtx(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-4)))) {
                return;
            }
        }
    }
    if (HMODULE shcore = LoadLibraryW(L"Shcore.dll")) {
        using SetAwarenessFn = HRESULT (WINAPI*)(int);
        if (auto setAwareness = reinterpret_cast<SetAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"))) {
            // PROCESS_PER_MONITOR_DPI_AWARE = 2
            if (setAwareness(2) == S_OK) {
                return;
            }
        }
    }
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiAwareFn = BOOL (WINAPI*)();
        if (auto setAware = reinterpret_cast<SetDpiAwareFn>(
                GetProcAddress(user32, "SetProcessDPIAware"))) {
            (void)setAware();
        }
    }
}

[[nodiscard]] float QueryWin32SystemDpiScale() noexcept {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiSysFn = UINT (WINAPI*)();
        if (auto getDpiSys = reinterpret_cast<GetDpiSysFn>(
                GetProcAddress(user32, "GetDpiForSystem"))) {
            const UINT dpi = getDpiSys();
            if (dpi > 0u) {
                return std::clamp(static_cast<float>(dpi) / 96.0f, 0.25f, 8.0f);
            }
        }
    }
    if (HDC dc = GetDC(nullptr)) {
        const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpiX > 0) {
            return std::clamp(static_cast<float>(dpiX) / 96.0f, 0.25f, 8.0f);
        }
    }
    return 1.0f;
}

[[nodiscard]] float QueryWin32WindowDpiScale(HWND hwnd) noexcept {
    if (hwnd) {
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            using GetDpiWinFn = UINT (WINAPI*)(HWND);
            if (auto getDpiWin = reinterpret_cast<GetDpiWinFn>(
                    GetProcAddress(user32, "GetDpiForWindow"))) {
                const UINT dpi = getDpiWin(hwnd);
                if (dpi > 0u) {
                    return std::clamp(static_cast<float>(dpi) / 96.0f, 0.25f, 8.0f);
                }
            }
        }
    }
    return QueryWin32SystemDpiScale();
}

void AdjustWin32WindowRectForDpi(
    RECT* rect,
    DWORD style,
    BOOL hasMenu,
    DWORD exStyle,
    float dpiScale) noexcept {
    if (!rect) {
        return;
    }
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using AdjustDpiFn = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        if (auto adjustDpi = reinterpret_cast<AdjustDpiFn>(
                GetProcAddress(user32, "AdjustWindowRectExForDpi"))) {
            const UINT dpi = static_cast<UINT>(
                std::max<long>(24L, std::lround(std::clamp(dpiScale, 0.25f, 8.0f) * 96.0f)));
            if (adjustDpi(rect, style, hasMenu, exStyle, dpi)) {
                return;
            }
        }
    }
    (void)AdjustWindowRectEx(rect, style, hasMenu, exStyle);
}

void EnableWin32DarkTitleBar(HWND hwnd) noexcept {
    if (!hwnd) {
        return;
    }
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using DwmSetAttrFn = HRESULT (WINAPI*)(HWND, DWORD, const void*, DWORD);
        if (auto setAttr = reinterpret_cast<DwmSetAttrFn>(
                GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
            const BOOL useDark = TRUE;
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Win10 2004+/Win11), fallback 19 (older builds)
            if (FAILED(setAttr(hwnd, 20u, &useDark, sizeof(useDark)))) {
                (void)setAttr(hwnd, 19u, &useDark, sizeof(useDark));
            }
        }
    }
}
#endif

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
    if (ks >= 0x0041UL && ks <= 0x005AUL) {
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(ks - 0x0041UL));
    }
    if (ks >= 0x0061UL && ks <= 0x007AUL) {
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(ks - 0x0061UL));
    }
    if (ks >= 0x0030UL && ks <= 0x0039UL) {
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::D0) + static_cast<std::uint16_t>(ks - 0x0030UL));
    }
    if (ks >= 0xFFBEUL && ks <= 0xFFC9UL) {
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::F1) + static_cast<std::uint16_t>(ks - 0xFFBEUL));
    }
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

[[nodiscard]] Point MapEmCanvasCssPoint(
    const char* selector,
    std::uint32_t canvasWidth,
    std::uint32_t canvasHeight,
    float cssX,
    float cssY) noexcept {
    EmRafTimestamp cssW{};
    EmRafTimestamp cssH{};
    if (selector &&
        emscripten_get_element_css_size(selector, &cssW, &cssH) == EMSCRIPTEN_RESULT_SUCCESS &&
        static_cast<float>(cssW) > 0.5f &&
        static_cast<float>(cssH) > 0.5f) {
        const float scaleX = static_cast<float>(canvasWidth) / static_cast<float>(cssW);
        const float scaleY = static_cast<float>(canvasHeight) / static_cast<float>(cssH);
        return Point{cssX * scaleX, cssY * scaleY};
    }
    const float dpr = std::max(0.25f, static_cast<float>(emscripten_get_device_pixel_ratio()));
    return Point{cssX * dpr, cssY * dpr};
}

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
    if (domKey.size() == 1u) {
        const char ch = domKey[0];
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<KeyCode>(
                static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(ch - 'A'));
        }
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<KeyCode>(
                static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(ch - 'a'));
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<KeyCode>(
                static_cast<std::uint16_t>(KeyCode::D0) + static_cast<std::uint16_t>(ch - '0'));
        }
    }
    if ((domKey.size() == 2u || domKey.size() == 3u) && domKey[0] == 'F') {
        int fnNum = 0;
        for (std::size_t i = 1u; i < domKey.size(); ++i) {
            if (domKey[i] < '0' || domKey[i] > '9') {
                fnNum = 0;
                break;
            }
            fnNum = fnNum * 10 + (domKey[i] - '0');
        }
        if (fnNum >= 1 && fnNum <= 12) {
            return static_cast<KeyCode>(
                static_cast<std::uint16_t>(KeyCode::F1) + static_cast<std::uint16_t>(fnNum - 1));
        }
    }
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

#if defined(_WIN32)
namespace detail::win32_audio {

template <typename T>
struct ComPtr final {
    T* ptr = nullptr;
    ComPtr() noexcept = default;
    ~ComPtr() {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
};

struct DecodedClip final {
    std::uint32_t sampleRate = 0u;
    std::uint16_t channels = 0u;
    std::vector<std::int16_t> samples;
};

[[nodiscard]] std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8u);
}

[[nodiscard]] std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

[[nodiscard]] bool DecodeWavFileToPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();

    const std::filesystem::path p{std::string(filePath)};
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const std::streamoff szOff = in.tellg();
    if (szOff < 44 || szOff > 64 * 1024 * 1024) {
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(szOff));
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), szOff)) {
        return false;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    std::uint16_t audioFormat = 0u;
    std::uint16_t channels = 0u;
    std::uint32_t sampleRate = 0u;
    std::uint16_t bitsPerSample = 0u;
    std::size_t dataOffset = 0u;
    std::size_t dataSize = 0u;
    bool hasFmt = false;
    bool hasData = false;

    std::size_t pos = 12u;
    while (pos + 8u <= bytes.size()) {
        const std::uint8_t* chId = bytes.data() + pos;
        const std::uint32_t chSize = ReadLe32(bytes.data() + pos + 4u);
        pos += 8u;
        if (pos + chSize > bytes.size()) {
            break;
        }
        if (std::memcmp(chId, "fmt ", 4) == 0 && chSize >= 16u) {
            const std::uint8_t* f = bytes.data() + pos;
            audioFormat = ReadLe16(f + 0u);
            channels = ReadLe16(f + 2u);
            sampleRate = ReadLe32(f + 4u);
            bitsPerSample = ReadLe16(f + 14u);
            if (audioFormat == 0xFFFEu && chSize >= 40u) {
                audioFormat = ReadLe16(f + 24u);
            }
            hasFmt = true;
        } else if (std::memcmp(chId, "data", 4) == 0) {
            dataOffset = pos;
            dataSize = chSize;
            hasData = true;
        }
        pos += chSize + (chSize & 1u);
    }

    if (!hasFmt || !hasData || channels == 0u || sampleRate == 0u || dataSize == 0u) {
        return false;
    }

    const std::uint8_t* src = bytes.data() + dataOffset;
    if (audioFormat == 1u && bitsPerSample == 16u) {
        const std::size_t n = dataSize / 2u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            outSamples[i] = static_cast<std::int16_t>(ReadLe16(src + i * 2u));
        }
    } else if (audioFormat == 1u && bitsPerSample == 8u) {
        outSamples.resize(dataSize);
        for (std::size_t i = 0u; i < dataSize; ++i) {
            outSamples[i] = static_cast<std::int16_t>((static_cast<int>(src[i]) - 128) << 8);
        }
    } else if (audioFormat == 1u && bitsPerSample == 24u) {
        const std::size_t n = dataSize / 3u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            const std::int32_t v =
                (static_cast<std::int32_t>(src[i * 3u + 0u]) << 8) |
                (static_cast<std::int32_t>(src[i * 3u + 1u]) << 16) |
                (static_cast<std::int32_t>(static_cast<std::int8_t>(src[i * 3u + 2u])) << 24);
            outSamples[i] = static_cast<std::int16_t>(v >> 16);
        }
    } else if (audioFormat == 3u && bitsPerSample == 32u) {
        const std::size_t n = dataSize / 4u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            float f = 0.0f;
            std::memcpy(&f, src + i * 4u, 4u);
            const float c = std::clamp(f, -1.0f, 1.0f);
            outSamples[i] = static_cast<std::int16_t>(std::lround(c * 32767.0f));
        }
    } else {
        return false;
    }

    outSampleRate = sampleRate;
    outChannels = channels;
    return !outSamples.empty();
}

[[nodiscard]] bool DecodeWithMediaFoundationPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();
    if (filePath.empty()) {
        return false;
    }

    int wideLen = MultiByteToWideChar(
        CP_UTF8, 0, filePath.data(), static_cast<int>(filePath.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    if (wideLen <= 0) {
        cp = CP_ACP;
        wideLen = MultiByteToWideChar(
            cp, 0, filePath.data(), static_cast<int>(filePath.size()), nullptr, 0);
    }
    if (wideLen <= 0) {
        return false;
    }
    std::wstring wpath(static_cast<std::size_t>(wideLen), L'\0');
    (void)MultiByteToWideChar(
        cp, 0, filePath.data(), static_cast<int>(filePath.size()), wpath.data(), wideLen);

    struct ComScope final {
        bool uninit = false;
        HRESULT hr = S_OK;
        ComScope() noexcept {
            hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            uninit = (hr == S_OK || hr == S_FALSE);
        }
        ~ComScope() {
            if (uninit) {
                CoUninitialize();
            }
        }
        [[nodiscard]] bool Ok() const noexcept {
            return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        }
    } comScope;
    if (!comScope.Ok()) {
        return false;
    }

    struct MfScope final {
        bool started = false;
        MfScope() noexcept {
            started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        }
        ~MfScope() {
            if (started) {
                (void)MFShutdown();
            }
        }
    } mfScope;
    if (!mfScope.started) {
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader.ptr)) || !reader.ptr) {
        return false;
    }
    (void)reader.ptr->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader.ptr->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) {
        return false;
    }

    ComPtr<IMFMediaType> partialType;
    if (FAILED(MFCreateMediaType(&partialType.ptr)) || !partialType.ptr) {
        return false;
    }
    if (FAILED(partialType.ptr->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(partialType.ptr->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(partialType.ptr->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16u)) ||
        FAILED(reader.ptr->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partialType.ptr))) {
        return false;
    }

    ComPtr<IMFMediaType> actualType;
    if (FAILED(reader.ptr->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType.ptr)) ||
        !actualType.ptr) {
        return false;
    }

    UINT32 sr = 0u;
    UINT32 ch = 0u;
    if (FAILED(actualType.ptr->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr)) ||
        FAILED(actualType.ptr->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch)) ||
        sr == 0u || ch == 0u) {
        return false;
    }

    for (;;) {
        DWORD streamIdx = 0u;
        DWORD flags = 0u;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader.ptr->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0u,
            &streamIdx,
            &flags,
            &timestamp,
            &sample.ptr);
        if (FAILED(hr)) {
            outSamples.clear();
            return false;
        }
        if (sample.ptr) {
            ComPtr<IMFMediaBuffer> buf;
            if (SUCCEEDED(sample.ptr->ConvertToContiguousBuffer(&buf.ptr)) && buf.ptr) {
                BYTE* data = nullptr;
                DWORD cb = 0u;
                if (SUCCEEDED(buf.ptr->Lock(&data, nullptr, &cb)) && data && cb >= 2u) {
                    const std::size_t sampleCount = cb / 2u;
                    const std::size_t oldSize = outSamples.size();
                    outSamples.resize(oldSize + sampleCount);
                    std::memcpy(outSamples.data() + oldSize, data, sampleCount * 2u);
                    (void)buf.ptr->Unlock();
                }
            }
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
            break;
        }
        if (!sample.ptr && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) == 0u) {
            break;
        }
    }

    if (outSamples.empty()) {
        return false;
    }
    outSampleRate = sr;
    outChannels = static_cast<std::uint16_t>(ch);
    return true;
}

} // namespace detail::win32_audio

struct NativeWindowHost::Win32AudioState final {
    struct PlayRequest final {
        std::string path;
        float volume = 1.0f;
    };
    struct Voice final {
        HWAVEOUT hwo = nullptr;
        WAVEHDR hdr{};
        bool prepared = false;
        std::vector<std::int16_t> scaledPcm;
    };

    static constexpr std::size_t kMaxVoices = 8u;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<PlayRequest> queue;
    std::unordered_map<std::string, std::shared_ptr<const detail::win32_audio::DecodedClip>> cache;
    std::vector<std::unique_ptr<Voice>> voices;
    bool stopping = false;
    std::thread worker;

    ~Win32AudioState() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        for (auto& v : voices) {
            DestroyVoice(std::move(v));
        }
        voices.clear();
    }

    void EnsureStartedLocked() {
        if (!worker.joinable() && !stopping) {
            worker = std::thread([this]() { WorkerLoop(); });
        }
    }

    void Enqueue(std::string_view path, float volume) {
        if (path.empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) {
                return;
            }
            EnsureStartedLocked();
            queue.push_back(PlayRequest{std::string(path), std::clamp(volume, 0.0f, 1.0f)});
        }
        cv.notify_one();
    }

private:
    static void DestroyVoice(std::unique_ptr<Voice> v) noexcept {
        if (!v) {
            return;
        }
        if (v->hwo) {
            (void)waveOutReset(v->hwo);
            if (v->prepared) {
                (void)waveOutUnprepareHeader(v->hwo, &v->hdr, sizeof(WAVEHDR));
                v->prepared = false;
            }
            (void)waveOutClose(v->hwo);
            v->hwo = nullptr;
        }
    }

    void ReapCompletedVoices() noexcept {
        for (std::size_t i = 0u; i < voices.size(); ) {
            Voice* v = voices[i].get();
            if (!v || !v->hwo || (v->hdr.dwFlags & WHDR_DONE) != 0u) {
                DestroyVoice(std::move(voices[i]));
                voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    [[nodiscard]] std::shared_ptr<const detail::win32_audio::DecodedClip> LoadClip(const std::string& path) {
        if (const auto it = cache.find(path); it != cache.end()) {
            return it->second;
        }
        auto clip = std::make_shared<detail::win32_audio::DecodedClip>();
        if (!NativeWindowHost::DecodeSoundFileToPcm16(
                path, clip->sampleRate, clip->channels, clip->samples)) {
            cache.emplace(path, nullptr);
            return nullptr;
        }
        cache.emplace(path, clip);
        return clip;
    }

    void PlayDecodedClip(
        const detail::win32_audio::DecodedClip& clip,
        float volume) {
        if (clip.samples.empty() || clip.sampleRate == 0u || clip.channels == 0u) {
            return;
        }
        ReapCompletedVoices();
        while (voices.size() >= kMaxVoices) {
            DestroyVoice(std::move(voices.front()));
            voices.erase(voices.begin());
        }

        auto voice = std::make_unique<Voice>();
        voice->scaledPcm.resize(clip.samples.size());
        const float gain = std::clamp(volume, 0.0f, 1.0f);
        if (gain >= 0.999f) {
            std::memcpy(voice->scaledPcm.data(), clip.samples.data(), clip.samples.size() * sizeof(std::int16_t));
        } else {
            for (std::size_t i = 0u; i < clip.samples.size(); ++i) {
                const float s = static_cast<float>(clip.samples[i]) * gain;
                voice->scaledPcm[i] = static_cast<std::int16_t>(
                    std::clamp(static_cast<long>(std::lround(s)), -32768L, 32767L));
            }
        }

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = clip.channels;
        wfx.nSamplesPerSec = clip.sampleRate;
        wfx.wBitsPerSample = 16u;
        wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * 2u);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0u;

        if (waveOutOpen(&voice->hwo, WAVE_MAPPER, &wfx, 0u, 0u, CALLBACK_NULL) != MMSYSERR_NOERROR ||
            !voice->hwo) {
            return;
        }
        voice->hdr.lpData = reinterpret_cast<LPSTR>(voice->scaledPcm.data());
        voice->hdr.dwBufferLength = static_cast<DWORD>(voice->scaledPcm.size() * sizeof(std::int16_t));
        voice->hdr.dwFlags = 0u;
        if (waveOutPrepareHeader(voice->hwo, &voice->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            DestroyVoice(std::move(voice));
            return;
        }
        voice->prepared = true;
        if (waveOutWrite(voice->hwo, &voice->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            DestroyVoice(std::move(voice));
            return;
        }
        voices.push_back(std::move(voice));
    }

    void WorkerLoop() {
        for (;;) {
            std::deque<PlayRequest> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (queue.empty() && !stopping) {
                    if (voices.empty()) {
                        cv.wait(lock, [this]() { return stopping || !queue.empty(); });
                    } else {
                        (void)cv.wait_for(
                            lock,
                            std::chrono::milliseconds(150),
                            [this]() { return stopping || !queue.empty(); });
                    }
                }
                if (stopping) {
                    break;
                }
                batch.swap(queue);
            }
            ReapCompletedVoices();
            while (!batch.empty()) {
                const PlayRequest req = std::move(batch.front());
                batch.pop_front();
                const auto clip = LoadClip(req.path);
                if (clip) {
                    PlayDecodedClip(*clip, req.volume);
                }
            }
        }
    }
};
#endif // defined(_WIN32)

bool NativeWindowHost::DecodeSoundFileToPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
#if defined(_WIN32)
    if (detail::win32_audio::DecodeWavFileToPcm16(filePath, outSampleRate, outChannels, outSamples)) {
        return true;
    }
    return detail::win32_audio::DecodeWithMediaFoundationPcm16(
        filePath, outSampleRate, outChannels, outSamples);
#else
    (void)filePath;
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();
    return false;
#endif
}

void NativeWindowHost::PlaySoundFile(std::string_view filePath, float volume) noexcept {
#if defined(_WIN32)
    if (filePath.empty() || mode_ == NativeWindowMode::HeadlessSoftware) {
        return;
    }
    if (!win32Audio_) {
        win32Audio_ = std::make_unique<Win32AudioState>();
    }
    win32Audio_->Enqueue(filePath, volume);
#else
    (void)filePath;
    (void)volume;
#endif
}

// ============================================================================
// NativeWindowHost Core Lifecycle & Host Bridging
// ============================================================================

NativeWindowHost::NativeWindowHost() noexcept {
    desktopIme_.SetNativeSpotSyncCallback(&NativeWindowHost::OnDesktopImeSpotSyncTrampoline, this);
}

NativeWindowHost::~NativeWindowHost() {
    if (inputRouter_ && inputRouter_->PlatformHooks() == &ownedPlatformHooks_) {
        inputRouter_->SetPlatformHooks(nullptr);
    }
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

KeyCode NativeWindowHost::TranslateWin32VirtualKey(std::uint32_t vk) noexcept {
    if (vk >= 0x41u && vk <= 0x5Au) { // 'A'..'Z'
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::A) + static_cast<std::uint16_t>(vk - 0x41u));
    }
    if (vk >= 0x30u && vk <= 0x39u) { // '0'..'9'
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::D0) + static_cast<std::uint16_t>(vk - 0x30u));
    }
    if (vk >= 0x70u && vk <= 0x7Bu) { // VK_F1..VK_F12
        return static_cast<KeyCode>(
            static_cast<std::uint16_t>(KeyCode::F1) + static_cast<std::uint16_t>(vk - 0x70u));
    }
    switch (vk) {
        case 0x09u: return KeyCode::Tab;       // VK_TAB
        case 0x0Du: return KeyCode::Enter;     // VK_RETURN
        case 0x20u: return KeyCode::Space;     // VK_SPACE
        case 0x1Bu: return KeyCode::Escape;    // VK_ESCAPE
        case 0x08u: return KeyCode::Backspace; // VK_BACK
        case 0x2Eu: return KeyCode::Delete;    // VK_DELETE
        case 0x25u: return KeyCode::Left;      // VK_LEFT
        case 0x27u: return KeyCode::Right;     // VK_RIGHT
        case 0x26u: return KeyCode::Up;        // VK_UP
        case 0x28u: return KeyCode::Down;      // VK_DOWN
        case 0x24u: return KeyCode::Home;      // VK_HOME
        case 0x23u: return KeyCode::End;       // VK_END
        case 0x21u: return KeyCode::PageUp;    // VK_PRIOR
        case 0x22u: return KeyCode::PageDown;  // VK_NEXT
        default:    return KeyCode::None;
    }
}

std::string NativeWindowHost::GetClipboardText() const {
#if defined(_WIN32)
    if (!IsHeadless() && win32Hwnd_ != nullptr) {
        if (OpenClipboard(static_cast<HWND>(win32Hwnd_)) != FALSE) {
            std::string result;
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData != nullptr) {
                if (const auto* wText = static_cast<const wchar_t*>(GlobalLock(hData))) {
                    const int needed = WideCharToMultiByte(
                        CP_UTF8, 0, wText, -1, nullptr, 0, nullptr, nullptr);
                    if (needed > 1) {
                        result.resize(static_cast<std::size_t>(needed - 1));
                        (void)WideCharToMultiByte(
                            CP_UTF8, 0, wText, -1, result.data(), needed, nullptr, nullptr);
                    }
                    GlobalUnlock(hData);
                }
            }
            (void)CloseClipboard();
            return result;
        }
    }
#endif
    std::lock_guard<std::mutex> lock(clipboardMutex_);
    return clipboardFallbackBuffer_;
}

void NativeWindowHost::SetClipboardText(std::string_view text) {
    {
        std::lock_guard<std::mutex> lock(clipboardMutex_);
        clipboardFallbackBuffer_.assign(text);
    }
#if defined(_WIN32)
    if (!IsHeadless() && win32Hwnd_ != nullptr) {
        if (OpenClipboard(static_cast<HWND>(win32Hwnd_)) != FALSE) {
            (void)EmptyClipboard();
            int wideCount = 1;
            if (!text.empty()) {
                const int req = MultiByteToWideChar(
                    CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
                if (req > 0) {
                    wideCount = req + 1;
                }
            }
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideCount) * sizeof(wchar_t));
            if (hMem != nullptr) {
                if (auto* dst = static_cast<wchar_t*>(GlobalLock(hMem))) {
                    if (wideCount > 1) {
                        (void)MultiByteToWideChar(
                            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), dst, wideCount - 1);
                    }
                    dst[wideCount - 1] = L'\0';
                    GlobalUnlock(hMem);
                    if (SetClipboardData(CF_UNICODETEXT, hMem) == nullptr) {
                        GlobalFree(hMem);
                    }
                } else {
                    GlobalFree(hMem);
                }
            }
            (void)CloseClipboard();
        }
    }
#endif
}

void NativeWindowHost::InstallPlatformHooks(UIHostPlatformHooks& hooks) noexcept {
    if (!hooks.syncImeRect) {
        hooks.syncImeRect = [this](Rect caretRect, bool active) {
            SyncNativeImeSpot(caretRect, active);
        };
    }
    if (!hooks.getClipboardText) {
        hooks.getClipboardText = [this]() -> std::string {
            return GetClipboardText();
        };
    }
    if (!hooks.setClipboardText) {
        hooks.setClipboardText = [this](std::string_view text) {
            SetClipboardText(text);
        };
    }
    if (!hooks.requestWake) {
        hooks.requestWake = [this]() {
            WakeEventLoop();
        };
    }
    if (!hooks.closeWindow) {
        hooks.closeWindow = [this]() {
            RequestClose();
        };
    }
    if (!hooks.playSound) {
        hooks.playSound = [this](std::string_view filePath, float volume) {
            PlaySoundFile(filePath, volume);
        };
    }
}

void NativeWindowHost::WireInputRouterHooks() noexcept {
    if (!inputRouter_) {
        return;
    }
    if (UIHostPlatformHooks* existingHooks = inputRouter_->PlatformHooks()) {
        InstallPlatformHooks(*existingHooks);
    } else {
        InstallPlatformHooks(ownedPlatformHooks_);
        inputRouter_->SetPlatformHooks(&ownedPlatformHooks_);
    }
}

void NativeWindowHost::BindDesktopHost(
    DesktopEventHost* desktopHost,
    PagedRenderTree* renderTree,
    ZeroCopyGpuStream* gpuStream,
    VisualInputRouter* inputRouter) noexcept {
    if (desktopHost_) {
        desktopHost_->SetWakeCallback(nullptr, nullptr);
    }
    if (inputRouter_ && inputRouter_ != inputRouter && inputRouter_->PlatformHooks() == &ownedPlatformHooks_) {
        inputRouter_->SetPlatformHooks(nullptr);
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
        desktopHost_->SetDpiScale(dpiScale_);
        desktopHost_->SetWakeCallback(&NativeWindowHost::OnHostWakeTrampoline, this);
    }
    if (inputRouter_ && !inputRouter_->ImeHost()) {
        inputRouter_->SetImeHost(&desktopIme_);
    }
    WireInputRouterHooks();
}

void NativeWindowHost::BindWebCanvasHost(
    WebCanvasHost* webHost,
    PagedRenderTree* renderTree,
    ZeroCopyGpuStream* gpuStream,
    VisualInputRouter* inputRouter) noexcept {
    if (webHost_) {
        webHost_->SetWakeCallback(nullptr, nullptr);
    }
    if (inputRouter_ && inputRouter_ != inputRouter && inputRouter_->PlatformHooks() == &ownedPlatformHooks_) {
        inputRouter_->SetPlatformHooks(nullptr);
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
        webHost_->SetDpiScale(dpiScale_);
        webHost_->SetWakeCallback(&NativeWindowHost::OnHostWakeTrampoline, this);
        if (inputRouter_ && !inputRouter_->ImeHost()) {
            inputRouter_->SetImeHost(&webHost_->ImeAdapter());
        }
    }
    WireInputRouterHooks();
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
    dpiScale_ = 1.0f;
#if defined(_WIN32)
    if (!config.forceHeadless && !gpuBackend_) {
        EnableWin32PerMonitorDpiV2();
    }
#endif
    if (config.dpiScale > 0.0f && std::isfinite(config.dpiScale)) {
        dpiScale_ = std::clamp(config.dpiScale, 0.25f, 8.0f);
    } else if (!config.forceHeadless && !gpuBackend_ && config.enableHighDpi) {
#if defined(_WIN32)
        dpiScale_ = QueryWin32SystemDpiScale();
#endif
    }
    if (config.enableHighDpi) {
        logicalWidth_ = std::max<std::uint32_t>(1u, config.width);
        logicalHeight_ = std::max<std::uint32_t>(1u, config.height);
        width_ = std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(std::lround(static_cast<double>(logicalWidth_) * static_cast<double>(dpiScale_))));
        height_ = std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(std::lround(static_cast<double>(logicalHeight_) * static_cast<double>(dpiScale_))));
    } else {
        width_ = std::max<std::uint32_t>(1u, config.width);
        height_ = std::max<std::uint32_t>(1u, config.height);
        const float invDpi = 1.0f / std::max(0.25f, dpiScale_);
        logicalWidth_ = std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(std::lround(static_cast<float>(width_) * invDpi)));
        logicalHeight_ = std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(std::lround(static_cast<float>(height_) * invDpi)));
    }
    clearColor_ = config.clearColor;
    closeRequested_.store(false, std::memory_order_release);
    wakeSignaled_.store(false, std::memory_order_release);
    needsInitialRender_ = true;
    hasLastFrameTimestamp_ = false;

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
        desktopHost_->SetDpiScale(dpiScale_);
        desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (webHost_) {
        webHost_->SetDpiScale(dpiScale_);
        webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (inputRouter_ && !inputRouter_->ImeHost()) {
        inputRouter_->SetImeHost(webHost_ ? static_cast<ITextInputMethodHost*>(&webHost_->ImeAdapter()) : &desktopIme_);
    }
    WireInputRouterHooks();

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
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

    (void)EffectiveResourceStore();
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
    inModalFrameDispatch_ = false;
    inTickAndPresent_ = false;
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
#endif

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    for (int& fd : wakePipeFds_) {
        if (fd >= 0) {
            (void)close(fd);
            fd = -1;
        }
    }
#elif defined(_WIN32)
    win32Audio_.reset();
    if (win32Hglrc_) {
        HMODULE glMod = win32OpenglModule_
            ? static_cast<HMODULE>(win32OpenglModule_)
            : GetModuleHandleA("opengl32.dll");
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
    if (win32OpenglModule_) {
        win32OpenglModule_ = nullptr;
    }
    if (win32ImmModule_) {
        FreeLibrary(static_cast<HMODULE>(win32ImmModule_));
        win32ImmModule_ = nullptr;
    }
    win32ImmGetContext_ = nullptr;
    win32ImmSetCompositionWindow_ = nullptr;
    win32ImmGetCompositionStringW_ = nullptr;
    win32ImmReleaseContext_ = nullptr;
    win32ImmProbed_ = false;
    pendingHighSurrogate_ = 0u;
    win32PressedMouseMask_ = 0u;
    win32WindowShown_ = false;
    win32InSizeMove_ = false;
    if (win32Hdc_ && win32Hwnd_) {
        ReleaseDC(static_cast<HWND>(win32Hwnd_), static_cast<HDC>(win32Hdc_));
        win32Hdc_ = nullptr;
    }
    if (win32Hwnd_) {
        HWND hwnd = static_cast<HWND>(win32Hwnd_);
        win32Hwnd_ = nullptr;
        KillTimer(hwnd, 0xAE20u);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
    if (win32WakeEvent_) {
        CloseHandle(static_cast<HANDLE>(win32WakeEvent_));
        win32WakeEvent_ = nullptr;
    }
#elif defined(__EMSCRIPTEN__)
    if (!emCanvasSelector_.empty()) {
        const char* sel = emCanvasSelector_.c_str();
        (void)emscripten_set_mousemove_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_mousedown_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_mouseup_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_wheel_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchstart_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchmove_callback(sel, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchend_callback(sel, nullptr, EM_FALSE, nullptr);
        emCanvasSelector_.clear();
    }
    (void)emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
    (void)emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
    if (webglContextHandle_ > 0) {
        (void)emscripten_webgl_destroy_context(webglContextHandle_);
        webglContextHandle_ = 0;
    }
    rafScheduled_ = false;
    lastRafTimeMs_ = -1.0f;
#endif

    hasLastFrameTimestamp_ = false;
    mode_ = NativeWindowMode::Uninitialized;
}

bool NativeWindowHost::InitializePlatformWindowAndBackend(const NativeWindowConfig& config) {
    GpuBackendConfig gpuCfg{};
    gpuCfg.maxPagesInRing = std::max<std::size_t>(16u, config.maxPagesInRing);
    gpuCfg.defaultAtlasSize = config.defaultAtlasSize;
    gpuCfg.initialViewportWidth = width_;
    gpuCfg.initialViewportHeight = height_;
    gpuCfg.preferHardwareGpu = config.preferHardwareGpu;

    auto makeSwCfg = [](const GpuBackendConfig& base) noexcept {
        GpuBackendConfig sw = base;
        sw.preferHardwareGpu = false;
        sw.autoBuildDefaultAtlas = false;
        sw.maxPagesInRing = std::min<std::size_t>(16u, base.maxPagesInRing);
        sw.initialQuadCapacity = sw.maxPagesInRing * 42u;
        return sw;
    };

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

    gpuCfg.autoBuildDefaultAtlas = false;
    if (config.forceHeadless) {
        const GpuBackendConfig swCfg = makeSwCfg(gpuCfg);
        auto sw = std::make_unique<SoftwareSurfaceBackend>();
        if (!sw->Initialize(swCfg)) {
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

    emCanvasSelector_ = config.canvasSelector.empty() ? "#canvas" : config.canvasSelector;
    const char* selector = emCanvasSelector_.c_str();
    {
        detail::wasm_abi::EmRafTimestamp cssW{};
        detail::wasm_abi::EmRafTimestamp cssH{};
        float dpr = 1.0f;
        if (config.dpiScale > 0.0f && std::isfinite(config.dpiScale)) {
            dpr = std::clamp(config.dpiScale, 0.25f, 8.0f);
        } else if (config.enableHighDpi) {
            dpr = std::clamp(static_cast<float>(emscripten_get_device_pixel_ratio()), 0.25f, 8.0f);
        }
        dpiScale_ = dpr;
        if (emscripten_get_element_css_size(selector, &cssW, &cssH) == EMSCRIPTEN_RESULT_SUCCESS &&
            static_cast<float>(cssW) > 1.0f &&
            static_cast<float>(cssH) > 1.0f) {
            logicalWidth_ = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(cssW))));
            logicalHeight_ = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(cssH))));
            const auto dprW = static_cast<std::uint32_t>(std::lround(static_cast<float>(cssW) * dpr));
            const auto dprH = static_cast<std::uint32_t>(std::lround(static_cast<float>(cssH) * dpr));
            if (dprW > 0u && dprH > 0u) {
                width_ = dprW;
                height_ = dprH;
                gpuCfg.initialViewportWidth = width_;
                gpuCfg.initialViewportHeight = height_;
                if (desktopHost_) {
                    desktopHost_->SetDpiScale(dpiScale_);
                    desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
                }
                if (webHost_) {
                    webHost_->SetDpiScale(dpiScale_);
                    webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
                }
            }
        }
    }
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
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY));
                    self->OnPlatformMouseMove(
                        pt.x,
                        pt.y,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_mousedown_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenMouseEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    const PointerButton btn = (e->button == 2) ? PointerButton::Right
                        : ((e->button == 1) ? PointerButton::Middle : PointerButton::Left);
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY));
                    self->OnPlatformMouseButton(
                        pt.x,
                        pt.y,
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
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->targetX),
                        static_cast<float>(e->targetY));
                    self->OnPlatformMouseButton(
                        pt.x,
                        pt.y,
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
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->mouse.targetX),
                        static_cast<float>(e->mouse.targetY));
                    self->OnPlatformMouseWheel(
                        pt.x,
                        pt.y,
                        -static_cast<float>(e->deltaX) * scale / 40.0f,
                        -static_cast<float>(e->deltaY) * scale / 40.0f,
                        detail::wasm_abi::TranslateEmModifiers(
                            e->mouse.shiftKey, e->mouse.ctrlKey, e->mouse.altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_touchstart_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenTouchEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    if (e->numTouches <= 0) return EM_FALSE;
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->touches[0].targetX),
                        static_cast<float>(e->touches[0].targetY));
                    self->OnPlatformMouseButton(
                        pt.x,
                        pt.y,
                        PointerButton::Left,
                        true,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_touchmove_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenTouchEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    if (e->numTouches <= 0) return EM_FALSE;
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->touches[0].targetX),
                        static_cast<float>(e->touches[0].targetY));
                    self->OnPlatformMouseMove(
                        pt.x,
                        pt.y,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
                    return EM_TRUE;
                });
            (void)emscripten_set_touchend_callback(selector, this, EM_FALSE,
                [](int, const EmscriptenTouchEvent* e, void* ud) -> EM_BOOL {
                    auto* self = static_cast<NativeWindowHost*>(ud);
                    if (e->numTouches <= 0) return EM_FALSE;
                    const Point pt = detail::wasm_abi::MapEmCanvasCssPoint(
                        self->emCanvasSelector_.c_str(),
                        self->Width(),
                        self->Height(),
                        static_cast<float>(e->touches[0].targetX),
                        static_cast<float>(e->touches[0].targetY));
                    self->OnPlatformMouseButton(
                        pt.x,
                        pt.y,
                        PointerButton::Left,
                        false,
                        detail::wasm_abi::TranslateEmModifiers(e->shiftKey, e->ctrlKey, e->altKey));
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
    if (webglContextHandle_ > 0) {
        (void)emscripten_webgl_destroy_context(webglContextHandle_);
        webglContextHandle_ = 0;
    }
    emCanvasSelector_.clear();

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
            return NativeWindowHost::TranslateWin32VirtualKey(static_cast<std::uint32_t>(vk));
        };

        switch (msg) {
            case WM_CLOSE:
                self->RequestClose();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case 0x0231: { // WM_ENTERSIZEMOVE
                self->win32InSizeMove_ = true;
                self->win32LastModalTick_ = std::chrono::steady_clock::now();
                SetTimer(hwnd, 0xAE20u, 16u, nullptr);
                return 0;
            }
            case 0x0232: { // WM_EXITSIZEMOVE
                KillTimer(hwnd, 0xAE20u);
                self->win32InSizeMove_ = false;
                if (self->gpuBackend_) {
                    self->DispatchModalLiveFrame(0.016f);
                }
                return 0;
            }
            case WM_TIMER: {
                if (wParam == 0xAE20u && self->win32InSizeMove_ && self->gpuBackend_) {
                    const auto now = std::chrono::steady_clock::now();
                    const float dt = std::clamp(
                        std::chrono::duration<float>(now - self->win32LastModalTick_).count(),
                        0.001f,
                        0.05f);
                    self->win32LastModalTick_ = now;
                    if (!self->CanHostSleepIdle() || self->needsInitialRender_) {
                        self->DispatchModalLiveFrame(dt);
                    }
                    return 0;
                }
                break;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                (void)BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                self->needsInitialRender_ = true;
                if (self->desktopHost_) {
                    self->desktopHost_->NotifyDirtyLayout();
                }
                if (self->win32InSizeMove_ && self->gpuBackend_) {
                    self->DispatchModalLiveFrame(0.0f);
                } else {
                    self->WakeEventLoop();
                }
                return 0;
            }
            case WM_SIZE: {
                const std::uint32_t nw = static_cast<std::uint32_t>(LOWORD(lParam));
                const std::uint32_t nh = static_cast<std::uint32_t>(HIWORD(lParam));
                if (nw > 0u && nh > 0u && wParam != SIZE_MINIMIZED) {
                    const bool changed = (self->width_ != nw || self->height_ != nh);
                    self->OnPlatformResize(nw, nh);
                    if (changed && self->gpuBackend_ && (self->win32InSizeMove_ || self->win32WindowShown_)) {
                        self->DispatchModalLiveFrame(0.0f);
                    }
                }
                return 0;
            }
            case 0x02E0: { // WM_DPICHANGED
                const UINT newDpiX = static_cast<UINT>(LOWORD(wParam));
                const float borderScale = (newDpiX > 0u)
                    ? std::clamp(static_cast<float>(newDpiX) / 96.0f, 0.25f, 8.0f)
                    : QueryWin32WindowDpiScale(hwnd);
                if (self->config_.enableHighDpi && self->config_.dpiScale <= 0.0f) {
                    self->UpdateDpiScale(borderScale);
                    if (const RECT* suggested = reinterpret_cast<const RECT*>(lParam)) {
                        SetWindowPos(
                            hwnd,
                            nullptr,
                            suggested->left,
                            suggested->top,
                            suggested->right - suggested->left,
                            suggested->bottom - suggested->top,
                            SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                } else {
                    RECT wr{0, 0, static_cast<LONG>(self->width_), static_cast<LONG>(self->height_)};
                    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
                    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
                    AdjustWin32WindowRectForDpi(&wr, style, FALSE, exStyle, borderScale);
                    SetWindowPos(
                        hwnd,
                        nullptr,
                        0,
                        0,
                        std::max(1, static_cast<int>(wr.right - wr.left)),
                        std::max(1, static_cast<int>(wr.bottom - wr.top)),
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
            case WM_LBUTTONUP: {
                const bool isDown = (msg == WM_LBUTTONDOWN);
                if (isDown) {
                    if (self->win32PressedMouseMask_ == 0u) SetCapture(hwnd);
                    self->win32PressedMouseMask_ |= 0x01u;
                } else {
                    self->win32PressedMouseMask_ = static_cast<std::uint8_t>(self->win32PressedMouseMask_ & ~0x01u);
                    if (self->win32PressedMouseMask_ == 0u && GetCapture() == hwnd) ReleaseCapture();
                }
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Left,
                    isDown,
                    queryWinMods());
                return 0;
            }
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP: {
                const bool isDown = (msg == WM_RBUTTONDOWN);
                if (isDown) {
                    if (self->win32PressedMouseMask_ == 0u) SetCapture(hwnd);
                    self->win32PressedMouseMask_ |= 0x02u;
                } else {
                    self->win32PressedMouseMask_ = static_cast<std::uint8_t>(self->win32PressedMouseMask_ & ~0x02u);
                    if (self->win32PressedMouseMask_ == 0u && GetCapture() == hwnd) ReleaseCapture();
                }
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Right,
                    isDown,
                    queryWinMods());
                return 0;
            }
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: {
                const bool isDown = (msg == WM_MBUTTONDOWN);
                if (isDown) {
                    if (self->win32PressedMouseMask_ == 0u) SetCapture(hwnd);
                    self->win32PressedMouseMask_ |= 0x04u;
                } else {
                    self->win32PressedMouseMask_ = static_cast<std::uint8_t>(self->win32PressedMouseMask_ & ~0x04u);
                    if (self->win32PressedMouseMask_ == 0u && GetCapture() == hwnd) ReleaseCapture();
                }
                self->OnPlatformMouseButton(
                    static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)),
                    PointerButton::Middle,
                    isDown,
                    queryWinMods());
                return 0;
            }
            case WM_CAPTURECHANGED: {
                if (reinterpret_cast<HWND>(lParam) != hwnd && self->win32PressedMouseMask_ != 0u) {
                    self->win32PressedMouseMask_ = 0u;
                    if (self->inputRouter_) {
                        self->inputRouter_->CancelPointerInteraction();
                    }
                    self->NotifyHostInputArrived();
                }
                return 0;
            }
            case WM_MOUSEWHEEL: {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
                self->OnPlatformMouseWheel(
                    static_cast<float>(pt.x),
                    static_cast<float>(pt.y),
                    0.0f,
                    steps,
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
                    -steps,
                    0.0f,
                    queryWinMods());
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const KeyCode kc = mapVkToKey(wParam);
                const std::uint8_t mods = queryWinMods();
                const bool isAltF4 = (msg == WM_SYSKEYDOWN && wParam == VK_F4 && (mods & KeyMod_Alt) != 0u);
                if (kc != KeyCode::None) {
                    self->OnPlatformKey(kc, true, mods);
                    if (!isAltF4) {
                        return 0;
                    }
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
            case WM_IME_STARTCOMPOSITION: {
                if (ITextInputMethodHost* ime = self->ActiveImeHost()) {
                    ime->OnCompositionStart();
                    self->telemetry_.osEventsTranslated += 1u;
                    self->NotifyHostInputArrived();
                }
                return 0;
            }
            case WM_IME_COMPOSITION: {
                if (self->EnsureWin32ImmLoaded()) {
                    using ImmGetCtxFn = HANDLE (WINAPI*)(HWND);
                    using ImmGetCompStrFn = LONG (WINAPI*)(HANDLE, DWORD, LPVOID, DWORD);
                    using ImmRelCtxFn = BOOL (WINAPI*)(HWND, HANDLE);
                    auto immGet = reinterpret_cast<ImmGetCtxFn>(self->win32ImmGetContext_);
                    auto immGetStr = reinterpret_cast<ImmGetCompStrFn>(self->win32ImmGetCompositionStringW_);
                    auto immRel = reinterpret_cast<ImmRelCtxFn>(self->win32ImmReleaseContext_);
                    if (HANDLE himc = immGet(hwnd)) {
                        bool handled = false;
                        ITextInputMethodHost* ime = self->ActiveImeHost();
                        // GCS_RESULTSTR = 0x0800
                        if ((lParam & 0x0800L) != 0 && ime) {
                            const LONG byteLen = immGetStr(himc, 0x0800u, nullptr, 0u);
                            std::string utf8;
                            if (byteLen > 0) {
                                std::vector<wchar_t> wbuf(
                                    static_cast<std::size_t>(byteLen) / sizeof(wchar_t) + 1u,
                                    L'\0');
                                const LONG got = immGetStr(himc, 0x0800u, wbuf.data(), static_cast<DWORD>(byteLen));
                                if (got > 0) {
                                    utf8 = ConvertUtf16SliceToUtf8(
                                        wbuf.data(),
                                        static_cast<std::size_t>(got) / sizeof(wchar_t));
                                }
                            }
                            ime->OnCompositionEnd(utf8);
                            if (self->webHost_ && !utf8.empty()) {
                                (void)self->EffectiveResourceStore();
                                (void)self->webHost_->FontStream().InspectTextAndQueueMissingShards(utf8);
                            }
                            handled = true;
                        }
                        // GCS_COMPSTR = 0x0008, GCS_CURSORPOS = 0x0080
                        if ((lParam & 0x0008L) != 0 && ime) {
                            const LONG byteLen = immGetStr(himc, 0x0008u, nullptr, 0u);
                            std::string utf8;
                            std::size_t cpCount = 0u;
                            if (byteLen > 0) {
                                std::vector<wchar_t> wbuf(
                                    static_cast<std::size_t>(byteLen) / sizeof(wchar_t) + 1u,
                                    L'\0');
                                const LONG got = immGetStr(himc, 0x0008u, wbuf.data(), static_cast<DWORD>(byteLen));
                                if (got > 0) {
                                    utf8 = ConvertUtf16SliceToUtf8(
                                        wbuf.data(),
                                        static_cast<std::size_t>(got) / sizeof(wchar_t),
                                        &cpCount);
                                }
                            }
                            const LONG rawCursorWchars = immGetStr(himc, 0x0080u, nullptr, 0u);
                            const auto caretCp = (rawCursorWchars >= 0)
                                ? std::min<std::uint32_t>(
                                    static_cast<std::uint32_t>(rawCursorWchars),
                                    static_cast<std::uint32_t>(cpCount))
                                : static_cast<std::uint32_t>(cpCount);
                            ime->OnCompositionUpdate(utf8, caretCp);
                            handled = true;
                        }
                        (void)immRel(hwnd, himc);
                        if (handled) {
                            self->telemetry_.osEventsTranslated += 1u;
                            self->NotifyHostInputArrived();
                            const LPARAM remFlags = lParam & ~(0x0800L | 0x0008L);
                            if (remFlags == 0) {
                                return 0;
                            }
                            return DefWindowProcW(hwnd, msg, wParam, remFlags);
                        }
                    }
                }
                break;
            }
            case WM_IME_ENDCOMPOSITION: {
                if (ITextInputMethodHost* ime = self->ActiveImeHost()) {
                    if (self->desktopIme_.IsComposing() ||
                        (self->webHost_ && self->webHost_->ImeAdapter().IsComposing())) {
                        ime->OnCompositionEnd("");
                    }
                    self->telemetry_.osEventsTranslated += 1u;
                    self->NotifyHostInputArrived();
                }
                return 0;
            }
            case WM_CHAR: {
                const auto u16 = static_cast<std::uint16_t>(wParam);
                if (u16 >= 0xD800u && u16 <= 0xDBFFu) {
                    self->pendingHighSurrogate_ = u16;
                    return 0;
                }
                if (u16 >= 0xDC00u && u16 <= 0xDFFFu) {
                    if (self->pendingHighSurrogate_ == 0u) {
                        return 0; // Reject lone low surrogate
                    }
                    const std::uint32_t cp = 0x10000u +
                        ((static_cast<std::uint32_t>(self->pendingHighSurrogate_) - 0xD800u) << 10u) +
                        (static_cast<std::uint32_t>(u16) - 0xDC00u);
                    self->pendingHighSurrogate_ = 0u;
                    char utf8[4]{};
                    std::size_t len = 0u;
                    EncodeCodePointToUtf8(cp, utf8, len);
                    if (len > 0u) {
                        self->OnPlatformTextInput(std::string_view(utf8, len));
                    }
                    return 0;
                }
                self->pendingHighSurrogate_ = 0u;
                const std::uint32_t cp = u16;
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

    std::wstring wideTitle = L"AeroGUI 2.0";
    if (!config.title.empty()) {
        const int needed = MultiByteToWideChar(
            CP_UTF8,
            0,
            config.title.data(),
            static_cast<int>(config.title.size()),
            nullptr,
            0);
        if (needed > 0) {
            wideTitle.resize(static_cast<std::size_t>(needed));
            (void)MultiByteToWideChar(
                CP_UTF8,
                0,
                config.title.data(),
                static_cast<int>(config.title.size()),
                wideTitle.data(),
                needed);
        }
    }

    auto computeClampedWindowBounds = [&](std::uint32_t clientW,
                                          std::uint32_t clientH,
                                          float scale,
                                          int& outW,
                                          int& outH,
                                          int& outX,
                                          int& outY,
                                          HWND refHwnd) {
        RECT wr{0, 0, static_cast<LONG>(clientW), static_cast<LONG>(clientH)};
        AdjustWin32WindowRectForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0u, scale);
        outW = std::max(1, static_cast<int>(wr.right - wr.left));
        outH = std::max(1, static_cast<int>(wr.bottom - wr.top));
        outX = CW_USEDEFAULT;
        outY = CW_USEDEFAULT;

        RECT workArea{};
        bool hasWorkArea = false;
        if (refHwnd) {
            if (HMONITOR hMon = MonitorFromWindow(refHwnd, MONITOR_DEFAULTTONEAREST)) {
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(hMon, &mi) &&
                    mi.rcWork.right > mi.rcWork.left &&
                    mi.rcWork.bottom > mi.rcWork.top) {
                    workArea = mi.rcWork;
                    hasWorkArea = true;
                }
            }
        }
        if (!hasWorkArea &&
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) &&
            workArea.right > workArea.left && workArea.bottom > workArea.top) {
            hasWorkArea = true;
        }
        if (hasWorkArea) {
            const int workW = static_cast<int>(workArea.right - workArea.left);
            const int workH = static_cast<int>(workArea.bottom - workArea.top);
            outW = std::min(outW, workW);
            outH = std::min(outH, workH);
            outX = static_cast<int>(workArea.left) + std::max(0, (workW - outW) / 2);
            outY = static_cast<int>(workArea.top) + std::max(0, (workH - outH) / 2);
        } else {
            const int screenW = GetSystemMetrics(SM_CXSCREEN);
            const int screenH = GetSystemMetrics(SM_CYSCREEN);
            if (screenW > 0 && screenH > 0) {
                outW = std::min(outW, screenW);
                outH = std::min(outH, screenH);
                outX = std::max(0, (screenW - outW) / 2);
                outY = std::max(0, (screenH - outH) / 2);
            }
        }
    };

    int outerW = 0;
    int outerH = 0;
    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;
    const float initialBorderDpi = QueryWin32SystemDpiScale();
    computeClampedWindowBounds(width_, height_, initialBorderDpi, outerW, outerH, posX, posY, nullptr);
    HWND hwnd = CreateWindowExW(
        0,
        kWndClassName,
        wideTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        posX,
        posY,
        outerW,
        outerH,
        nullptr,
        nullptr,
        hInst,
        this);

    if (hwnd) {
        win32Hwnd_ = hwnd;
        win32WindowShown_ = false;
        EnableWin32DarkTitleBar(hwnd);
        const float winBorderDpi = QueryWin32WindowDpiScale(hwnd);
        const std::uint32_t prevW = width_;
        const std::uint32_t prevH = height_;
        if (config.enableHighDpi && config.dpiScale <= 0.0f &&
            std::abs(winBorderDpi - dpiScale_) > 1e-3f) {
            dpiScale_ = winBorderDpi;
            width_ = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(std::lround(static_cast<double>(logicalWidth_) * static_cast<double>(dpiScale_))));
            height_ = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(std::lround(static_cast<double>(logicalHeight_) * static_cast<double>(dpiScale_))));
            gpuCfg.initialViewportWidth = width_;
            gpuCfg.initialViewportHeight = height_;
        }
        int adjW = 0;
        int adjH = 0;
        int adjX = 0;
        int adjY = 0;
        computeClampedWindowBounds(width_, height_, winBorderDpi, adjW, adjH, adjX, adjY, hwnd);
        if (std::abs(winBorderDpi - initialBorderDpi) > 1e-3f ||
            width_ != prevW ||
            height_ != prevH ||
            adjW != outerW ||
            adjH != outerH) {
            SetWindowPos(
                hwnd,
                nullptr,
                adjX,
                adjY,
                adjW,
                adjH,
                (adjX == CW_USEDEFAULT ? SWP_NOMOVE : 0u) | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RECT clientRect{};
        if (GetClientRect(hwnd, &clientRect)) {
            const auto cw = static_cast<std::uint32_t>(std::max<LONG>(1L, clientRect.right - clientRect.left));
            const auto ch = static_cast<std::uint32_t>(std::max<LONG>(1L, clientRect.bottom - clientRect.top));
            if (cw != width_ || ch != height_) {
                OnPlatformResize(cw, ch);
                gpuCfg.initialViewportWidth = width_;
                gpuCfg.initialViewportHeight = height_;
            }
        }
        if (desktopHost_) {
            desktopHost_->SetDpiScale(dpiScale_);
            desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
        }
        if (webHost_) {
            webHost_->SetDpiScale(dpiScale_);
            webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
        }
        if (renderTree_) {
            renderTree_->SetRootDpiScale(dpiScale_);
        }
        HDC hdc = GetDC(hwnd);
        win32Hdc_ = hdc;

        if (config.preferHardwareGpu && hdc) {
            PIXELFORMATDESCRIPTOR pfd{};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.cAlphaBits = 8;
            const int pf = ChoosePixelFormat(hdc, &pfd);
            if (pf != 0 && SetPixelFormat(hdc, pf, &pfd)) {
                static HMODULE sGlMod = LoadLibraryA("opengl32.dll");
                HMODULE glMod = sGlMod;
                if (glMod) {
                    win32OpenglModule_ = static_cast<void*>(glMod);
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
                        wglMakeCur(nullptr, nullptr);
                        if (wglDelCtx) {
                            wglDelCtx(finalRc);
                        }
                        win32Hglrc_ = nullptr;
                    } else if (tempRc && wglDelCtx) {
                        wglDelCtx(tempRc);
                    }
                    FreeLibrary(glMod);
                    win32OpenglModule_ = nullptr;
                }
            }
        }

        const GpuBackendConfig swCfg = makeSwCfg(gpuCfg);
        auto sw = std::make_unique<SoftwareSurfaceBackend>();
        if (sw->Initialize(swCfg)) {
            gpuBackend_ = std::move(sw);
            mode_ = NativeWindowMode::Win32GdiSoftware;
            if (renderTree_) {
                renderTree_->SetRootDpiScale(1.0f);
            }
            diagnostics_ = "Win32 HWND initialized with GDI SoftwareSurfaceBackend presenter.";
            return true;
        }
        if (win32Hdc_) {
            ReleaseDC(hwnd, static_cast<HDC>(win32Hdc_));
            win32Hdc_ = nullptr;
        }
        win32Hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }

    if (!config.allowHeadlessFallback) {
        diagnostics_ = "Win32 CreateWindowExW failed and headless fallback is disabled.";
        return false;
    }
    const GpuBackendConfig swCfg = makeSwCfg(gpuCfg);
    auto sw = std::make_unique<SoftwareSurfaceBackend>();
    (void)sw->Initialize(swCfg);
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
            const GpuBackendConfig swCfg = makeSwCfg(gpuCfg);
            auto swBackend = std::make_unique<SoftwareSurfaceBackend>();
            if (!swBackend->Initialize(swCfg)) {
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
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
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
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    if (wakePipeFds_[0] >= 0) {
        std::uint8_t drainBuf[64]{};
        while (read(wakePipeFds_[0], drainBuf, sizeof(drainBuf)) > 0) {}
    }
#endif
}

bool NativeWindowHost::WaitForEventsOrWake(std::int32_t timeoutMs) noexcept {
    if (timeoutMs == 0 || ShouldClose()) {
        return false;
    }
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    struct pollfd fds[2]{};
    nfds_t nfds = 0;
    if (wakePipeFds_[0] >= 0) {
        fds[nfds].fd = wakePipeFds_[0];
        fds[nfds].events = POLLIN;
        ++nfds;
    }
#if defined(__linux__)
    if (x11_ && x11_->x11Fd >= 0) {
        fds[nfds].fd = x11_->x11Fd;
        fds[nfds].events = POLLIN;
        ++nfds;
    }
#endif
    if (nfds > 0) {
        const int rc = poll(fds, nfds, timeoutMs);
        return rc > 0;
    }
    if (timeoutMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    }
    return false;
#elif defined(_WIN32)
    const DWORD timeoutDw = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    HANDLE handles[1]{static_cast<HANDLE>(win32WakeEvent_)};
    const DWORD handleCount = win32WakeEvent_ ? 1u : 0u;
    const DWORD waitRes = MsgWaitForMultipleObjects(
        handleCount,
        handleCount > 0u ? handles : nullptr,
        FALSE,
        timeoutDw,
        QS_ALLINPUT);
    return waitRes != WAIT_TIMEOUT;
#else
    if (timeoutMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    }
    return false;
#endif
}

std::shared_ptr<UIResourceStore> NativeWindowHost::EffectiveResourceStore() const noexcept {
    if (resourceStore_) {
        return resourceStore_;
    }
    if (desktopHost_) {
        return desktopHost_->GetResourceStore();
    }
    if (webHost_) {
        return webHost_->GetResourceStore();
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
    const float invDpi = 1.0f / std::max(0.25f, dpiScale_);
    lastPointerPos_ = Point{x * invDpi, y * invDpi};
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
    const float invDpi = 1.0f / std::max(0.25f, dpiScale_);
    lastPointerPos_ = Point{x * invDpi, y * invDpi};
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
    const float invDpi = 1.0f / std::max(0.25f, dpiScale_);
    lastPointerPos_ = Point{x * invDpi, y * invDpi};
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
        (void)EffectiveResourceStore();
        (void)webHost_->FontStream().InspectTextAndQueueMissingShards(utf8Text);
    }
    NotifyHostInputArrived();
}

void NativeWindowHost::DispatchModalLiveFrame(float dtSeconds) noexcept {
    if (inModalFrameDispatch_ || inTickAndPresent_ || ShouldClose() || !IsInitialized() || !gpuBackend_) {
        return;
    }
    struct ModalFrameScopeGuard {
        bool& flag;
        explicit ModalFrameScopeGuard(bool& f) noexcept : flag(f) { flag = true; }
        ~ModalFrameScopeGuard() noexcept { flag = false; }
    } modalGuard(inModalFrameDispatch_);
    if (modalFrameCallback_) {
        modalFrameCallback_(dtSeconds, modalFrameUserData_);
    } else {
        (void)TickAndPresentFrame(dtSeconds > 0.0f ? dtSeconds : 0.016f, true);
    }
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
    const float invDpi = 1.0f / std::max(0.25f, dpiScale_);
    logicalWidth_ = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(static_cast<float>(width_) * invDpi)));
    logicalHeight_ = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(static_cast<float>(height_) * invDpi)));
    needsInitialRender_ = true;
    telemetry_.osEventsTranslated += 1u;
    if (desktopHost_) {
        desktopHost_->SetDpiScale(dpiScale_);
        desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (webHost_) {
        webHost_->SetDpiScale(dpiScale_);
        webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    WakeEventLoop();
}

void NativeWindowHost::UpdateDpiScale(float newDpiScale) noexcept {
    const float clamped = (std::isfinite(newDpiScale) && newDpiScale > 0.05f)
        ? std::clamp(newDpiScale, 0.25f, 8.0f)
        : 1.0f;
    if (std::abs(clamped - dpiScale_) <= 1e-4f) {
        return;
    }
    dpiScale_ = clamped;
    const float invDpi = 1.0f / dpiScale_;
    logicalWidth_ = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(static_cast<float>(width_) * invDpi)));
    logicalHeight_ = std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>(std::lround(static_cast<float>(height_) * invDpi)));
    needsInitialRender_ = true;
    if (desktopHost_) {
        desktopHost_->SetDpiScale(dpiScale_);
        desktopHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
        desktopHost_->NotifyDirtyLayout();
    }
    if (webHost_) {
        webHost_->SetDpiScale(dpiScale_);
        webHost_->SetViewportSize(Size{static_cast<float>(width_), static_cast<float>(height_)});
    }
    if (renderTree_) {
        const float treeDpi = (mode_ == NativeWindowMode::Win32GdiSoftware) ? 1.0f : dpiScale_;
        renderTree_->SetRootDpiScale(treeDpi);
    }
    WakeEventLoop();
}

#if defined(_WIN32)
bool NativeWindowHost::EnsureWin32ImmLoaded() noexcept {
    if (!win32ImmProbed_) {
        win32ImmProbed_ = true;
        HMODULE immMod = LoadLibraryA("imm32.dll");
        if (immMod) {
            win32ImmModule_ = static_cast<void*>(immMod);
            win32ImmGetContext_ = reinterpret_cast<void*>(GetProcAddress(immMod, "ImmGetContext"));
            win32ImmSetCompositionWindow_ =
                reinterpret_cast<void*>(GetProcAddress(immMod, "ImmSetCompositionWindow"));
            win32ImmGetCompositionStringW_ =
                reinterpret_cast<void*>(GetProcAddress(immMod, "ImmGetCompositionStringW"));
            win32ImmReleaseContext_ = reinterpret_cast<void*>(GetProcAddress(immMod, "ImmReleaseContext"));
        }
    }
    return win32ImmGetContext_ != nullptr &&
           win32ImmSetCompositionWindow_ != nullptr &&
           win32ImmGetCompositionStringW_ != nullptr &&
           win32ImmReleaseContext_ != nullptr;
}
#endif

void NativeWindowHost::SyncNativeImeSpot(Rect caretRect, bool active) noexcept {
    telemetry_.lastSyncedImeCaretRect = caretRect;
    telemetry_.lastImeActive = active;
    const float scale = std::max(0.25f, dpiScale_);
    const float physX = caretRect.x * scale;
    const float physY = caretRect.y * scale;
    const float physBottom = (caretRect.y + caretRect.height) * scale;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (active && x11_ && x11_->xic && x11_->XVaCreateNestedList && x11_->XSetICValues) {
        detail::x11_abi::XPoint spot{};
        spot.x = static_cast<short>(std::clamp(physX, 0.0f, 32767.0f));
        spot.y = static_cast<short>(std::clamp(physBottom, 0.0f, 32767.0f));
        void* preeditAttr = x11_->XVaCreateNestedList(0, "spotLocation", &spot, nullptr);
        if (preeditAttr) {
            (void)x11_->XSetICValues(x11_->xic, "preeditAttributes", preeditAttr, nullptr);
            if (x11_->XFree) {
                x11_->XFree(preeditAttr);
            }
        }
    }
#elif defined(_WIN32)
    (void)physBottom;
    if (active && win32Hwnd_ && EnsureWin32ImmLoaded()) {
        struct WinCompForm {
            DWORD dwStyle;
            POINT ptCurrentPos;
            RECT rcArea;
        };
        using ImmGetCtxFn = HANDLE (WINAPI*)(HWND);
        using ImmSetCompFn = BOOL (WINAPI*)(HANDLE, const WinCompForm*);
        using ImmRelCtxFn = BOOL (WINAPI*)(HWND, HANDLE);
        auto immGet = reinterpret_cast<ImmGetCtxFn>(win32ImmGetContext_);
        auto immSet = reinterpret_cast<ImmSetCompFn>(win32ImmSetCompositionWindow_);
        auto immRel = reinterpret_cast<ImmRelCtxFn>(win32ImmReleaseContext_);
        if (HANDLE himc = immGet(static_cast<HWND>(win32Hwnd_))) {
            WinCompForm cf{};
            cf.dwStyle = 0x0002u; // CFS_POINT
            cf.ptCurrentPos.x = static_cast<LONG>(std::lround(physX));
            cf.ptCurrentPos.y = static_cast<LONG>(std::lround(physY));
            (void)immSet(himc, &cf);
            (void)immRel(static_cast<HWND>(win32Hwnd_), himc);
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
        std::uint32_t rw = 0u;
        std::uint32_t rh = 0u;
        if (gpuBackend_ && gpuBackend_->ReadbackFramebufferRgba8(swReadbackRgbaBuffer_, rw, rh)) {
            x11_->swBlitBgraBuffer.resize(swReadbackRgbaBuffer_.size());
            const std::size_t pxCount = static_cast<std::size_t>(rw) * rh;
            for (std::size_t i = 0; i < pxCount; ++i) {
                x11_->swBlitBgraBuffer[i * 4u + 0u] = swReadbackRgbaBuffer_[i * 4u + 2u];
                x11_->swBlitBgraBuffer[i * 4u + 1u] = swReadbackRgbaBuffer_[i * 4u + 1u];
                x11_->swBlitBgraBuffer[i * 4u + 2u] = swReadbackRgbaBuffer_[i * 4u + 0u];
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
        if (gpuBackend_ && gpuBackend_->Kind() == GpuBackendKind::Software) {
            const auto* sw = static_cast<const SoftwareSurfaceBackend*>(gpuBackend_.get());
            const auto& rgba = sw->FramebufferRgba8();
            const std::uint32_t rw = sw->FramebufferWidth();
            const std::uint32_t rh = sw->FramebufferHeight();
            if (!rgba.empty() && rw > 0u && rh > 0u) {
                BITMAPV4HEADER bmi{};
                bmi.bV4Size = sizeof(BITMAPV4HEADER);
                bmi.bV4Width = static_cast<LONG>(rw);
                bmi.bV4Height = -static_cast<LONG>(rh);
                bmi.bV4Planes = 1;
                bmi.bV4BitCount = 32;
                bmi.bV4V4Compression = BI_BITFIELDS;
                bmi.bV4RedMask = 0x000000FFu;
                bmi.bV4GreenMask = 0x0000FF00u;
                bmi.bV4BlueMask = 0x00FF0000u;
                SetStretchBltMode(static_cast<HDC>(win32Hdc_), COLORONCOLOR);
                StretchDIBits(
                    static_cast<HDC>(win32Hdc_),
                    0, 0, static_cast<int>(width_), static_cast<int>(height_),
                    0, 0, static_cast<int>(rw), static_cast<int>(rh),
                    rgba.data(),
                    reinterpret_cast<const BITMAPINFO*>(&bmi),
                    DIB_RGB_COLORS,
                    SRCCOPY);
            }
        } else {
            std::uint32_t rw = 0u;
            std::uint32_t rh = 0u;
            if (gpuBackend_ && gpuBackend_->ReadbackFramebufferRgba8(swReadbackRgbaBuffer_, rw, rh)) {
                swWin32BgraBuffer_.resize(swReadbackRgbaBuffer_.size());
                const std::size_t pxCount = static_cast<std::size_t>(rw) * rh;
                for (std::size_t i = 0; i < pxCount; ++i) {
                    swWin32BgraBuffer_[i * 4u + 0u] = swReadbackRgbaBuffer_[i * 4u + 2u];
                    swWin32BgraBuffer_[i * 4u + 1u] = swReadbackRgbaBuffer_[i * 4u + 1u];
                    swWin32BgraBuffer_[i * 4u + 2u] = swReadbackRgbaBuffer_[i * 4u + 0u];
                    swWin32BgraBuffer_[i * 4u + 3u] = 255u;
                }
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = static_cast<LONG>(rw);
                bmi.bmiHeader.biHeight = -static_cast<LONG>(rh);
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                SetStretchBltMode(static_cast<HDC>(win32Hdc_), COLORONCOLOR);
                StretchDIBits(
                    static_cast<HDC>(win32Hdc_),
                    0, 0, static_cast<int>(width_), static_cast<int>(height_),
                    0, 0, static_cast<int>(rw), static_cast<int>(rh),
                    swWin32BgraBuffer_.data(),
                    &bmi,
                    DIB_RGB_COLORS,
                    SRCCOPY);
            }
        }
    }
#endif
}

GpuBackendFrameStats NativeWindowHost::TickAndPresentFrame(
    float dtSeconds,
    bool forcePresent,
    std::uint32_t phaseMask) {
    GpuBackendFrameStats frameStats{};
    if (inTickAndPresent_ || !IsInitialized() || !gpuBackend_ || !activeGpuStream_) {
        return frameStats;
    }
    struct TickPresentScopeGuard {
        bool& flag;
        explicit TickPresentScopeGuard(bool& f) noexcept : flag(f) { flag = true; }
        ~TickPresentScopeGuard() noexcept { flag = false; }
    } presentGuard(inTickAndPresent_);
    (void)EffectiveResourceStore();

    const bool swScaledBlit = (mode_ == NativeWindowMode::Win32GdiSoftware);
    const std::uint32_t presentW = swScaledBlit ? std::max<std::uint32_t>(1u, logicalWidth_) : width_;
    const std::uint32_t presentH = swScaledBlit ? std::max<std::uint32_t>(1u, logicalHeight_) : height_;

    auto ensureNativeWindowShown = [&]() noexcept {
#if defined(_WIN32)
        if (win32Hwnd_ && !win32WindowShown_) {
            ShowWindow(static_cast<HWND>(win32Hwnd_), SW_SHOW);
            win32WindowShown_ = true;
        }
#endif
    };

    if (desktopHost_) {
        if (phaseMask != UiPhase_None) {
            if (forcePresent && desktopHost_->CanSleepIdle()) {
                desktopHost_->NotifyDirtyLayout();
            }
            desktopHost_->TickUi(dtSeconds, phaseMask);
        }
        if (renderTree_) {
            if (forcePresent && renderTree_->PageCount() > 0u) {
                renderTree_->MarkAllPagesDirty();
            }
            const ZeroCopySubmissionStats subStats = desktopHost_->RecordGpuCommands(*renderTree_, *activeGpuStream_);
            if (forcePresent || needsInitialRender_ || subStats.dirtyPagesUploaded > 0u || subStats.drawCallCount > 0u) {
                ensureNativeWindowShown();
                frameStats = gpuBackend_->PresentStream(
                    *renderTree_,
                    *activeGpuStream_,
                    presentW,
                    presentH,
                    clearColor_);
                SwapOrBlitPlatformSurface();
                needsInitialRender_ = false;
                telemetry_.framesRendered += 1u;
                telemetry_.lastBackendFrame = frameStats;
            }
        }
    } else if (webHost_) {
        const bool skipIdleWebTick = !forcePresent && !needsInitialRender_ &&
            webHost_->CanSleepIdle() &&
            (!renderTree_ || renderTree_->DirtyPageCount() == 0u);
        if (!skipIdleWebTick) {
            if (phaseMask != UiPhase_None) {
                if (forcePresent) {
                    webHost_->RequestWakeFrame();
                }
                webHost_->TickUi(dtSeconds, phaseMask);
            }
            if (renderTree_) {
                if (forcePresent && renderTree_->PageCount() > 0u) {
                    renderTree_->MarkAllPagesDirty();
                }
                const bool shouldPresent = forcePresent || needsInitialRender_ || renderTree_->DirtyPageCount() > 0u;
                if (shouldPresent) {
                    const ZeroCopySubmissionStats subStats =
                        webHost_->RecordGpuCommands(*renderTree_, *activeGpuStream_);
                    if (forcePresent || needsInitialRender_ || subStats.dirtyPagesUploaded > 0u || subStats.drawCallCount > 0u) {
                        ensureNativeWindowShown();
                        frameStats = gpuBackend_->PresentStream(
                            *renderTree_,
                            *activeGpuStream_,
                            presentW,
                            presentH,
                            clearColor_);
                        SwapOrBlitPlatformSurface();
                        needsInitialRender_ = false;
                        telemetry_.framesRendered += 1u;
                        telemetry_.lastBackendFrame = frameStats;
                    }
                }
            }
        }
    } else if (renderTree_) {
        if (inputRouter_ && inputRouter_->HasPendingEvents()) {
            (void)inputRouter_->FlushQueuedEvents();
        }
        ensureNativeWindowShown();
        frameStats = gpuBackend_->SubmitAndPresent(
            *renderTree_,
            *activeGpuStream_,
            presentW,
            presentH,
            clearColor_);
        SwapOrBlitPlatformSurface();
        needsInitialRender_ = false;
        telemetry_.framesRendered += 1u;
        telemetry_.lastBackendFrame = frameStats;
    }

    // Refresh IME caret screen bounds if a TextBox is currently focused and moved during layout
    if (inputRouter_) {
        if (inputRouter_->HasFocusedTextInput()) {
            const Rect currentCaret = inputRouter_->FocusedTextCaretRect();
            if (!(currentCaret == telemetry_.lastSyncedImeCaretRect) || !telemetry_.lastImeActive) {
                if (ITextInputMethodHost* ime = ActiveImeHost()) {
                    ime->FocusTextInput(inputRouter_->FocusedNode(), currentCaret);
                }
                SyncNativeImeSpot(currentCaret, true);
            }
        } else if (telemetry_.lastImeActive) {
            SyncNativeImeSpot(Rect{}, false);
        }
    }

    return frameStats;
}

bool NativeWindowHost::PumpPlatformEvents(std::int32_t maxWaitMs, bool* outSleptIdle) {
    if (!IsInitialized()) {
        return false;
    }

    bool sleptIdleThisIteration = false;

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    auto drainX11Events = [&]() {
#if defined(__linux__)
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
                        OnPlatformMouseWheel(mx, my, 0.0f, 1.0f, mods);
                    } else if (isDown && ev.xbutton.button == 5u) {
                        OnPlatformMouseWheel(mx, my, 0.0f, -1.0f, mods);
                    } else if (isDown && ev.xbutton.button == 6u) {
                        OnPlatformMouseWheel(mx, my, 1.0f, 0.0f, mods);
                    } else if (isDown && ev.xbutton.button == 7u) {
                        OnPlatformMouseWheel(mx, my, -1.0f, 0.0f, mods);
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
#endif
    };

    drainX11Events();

    if (CanHostSleepIdle() && maxWaitMs != 0 && !ShouldClose()) {
        sleptIdleThisIteration = true;
        hasLastFrameTimestamp_ = false;
        telemetry_.idleSleepEntries += 1u;
        if (WaitForEventsOrWake(maxWaitMs)) {
            telemetry_.eventLoopWakeCount += 1u;
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
        sleptIdleThisIteration = true;
        hasLastFrameTimestamp_ = false;
        telemetry_.idleSleepEntries += 1u;
        if (WaitForEventsOrWake(maxWaitMs)) {
            telemetry_.eventLoopWakeCount += 1u;
        }
        drainWin32Messages();
    } else if (!CanHostSleepIdle()) {
        telemetry_.eventLoopWakeCount += 1u;
    }

    DrainWakeSignal();

#elif defined(__EMSCRIPTEN__)
    (void)maxWaitMs;
    (void)sleptIdleThisIteration;
    DrainWakeSignal();
#else
    if (CanHostSleepIdle() && maxWaitMs != 0 && !ShouldClose()) {
        sleptIdleThisIteration = true;
        hasLastFrameTimestamp_ = false;
        telemetry_.idleSleepEntries += 1u;
        (void)WaitForEventsOrWake(maxWaitMs);
    } else if (!CanHostSleepIdle()) {
        telemetry_.eventLoopWakeCount += 1u;
    }
    DrainWakeSignal();
#endif

    if (outSleptIdle) {
        *outSleptIdle = sleptIdleThisIteration;
    }
    return !ShouldClose();
}

bool NativeWindowHost::PumpEventsAndRender(float dtSeconds, std::int32_t maxWaitMs) {
    if (!IsInitialized()) {
        return false;
    }

    const auto iterationStart = std::chrono::steady_clock::now();
    bool sleptIdleThisIteration = false;
    if (!PumpPlatformEvents(maxWaitMs, &sleptIdleThisIteration)) {
        return false;
    }

    if (!CanHostSleepIdle()) {
        const auto now = std::chrono::steady_clock::now();
        float effectiveDt = 0.016f;
        if (!hasLastFrameTimestamp_) {
            effectiveDt = (dtSeconds > 0.0f) ? std::clamp(dtSeconds, 0.0005f, 0.1f) : 0.016f;
            hasLastFrameTimestamp_ = true;
        } else {
            const float wallDt = std::chrono::duration<float>(now - lastFrameTimestamp_).count();
            effectiveDt = std::clamp(wallDt, 0.0005f, 0.1f);
        }
        lastFrameTimestamp_ = now;

        (void)TickAndPresentFrame(effectiveDt, needsInitialRender_);

#if !defined(__EMSCRIPTEN__)
        // Frame pacing when animations/updates keep the host awake and caller supplied a positive frame budget
        if (!sleptIdleThisIteration && maxWaitMs > 0 && !ShouldClose() && !CanHostSleepIdle()) {
            const auto afterFrame = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                afterFrame - iterationStart).count();
            if (elapsedMs < static_cast<long long>(maxWaitMs)) {
                const auto remainingMs = static_cast<std::int32_t>(maxWaitMs - elapsedMs);
                (void)WaitForEventsOrWake(remainingMs);
            }
        }
#endif
    } else {
        hasLastFrameTimestamp_ = false;
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
                dtSec = std::clamp((nowMs - self->lastRafTimeMs_) * 0.001f, 0.0005f, 0.1f);
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
    const float frameDtSec = config_.animationFrameIntervalMs * 0.001f;
    while (!ShouldClose()) {
        if (maxFrames > 0u && (telemetry_.framesRendered - startFrames) >= maxFrames) {
            break;
        }
        const std::uint64_t beforeIterFrames = telemetry_.framesRendered;
        const std::int32_t waitMs = CanHostSleepIdle()
            ? (maxFrames > 0u ? 0 : -1)
            : static_cast<std::int32_t>(std::max(1.0f, config_.animationFrameIntervalMs));
        if (!PumpEventsAndRender(frameDtSec, waitMs)) {
            break;
        }
        if (maxFrames > 0u && !ShouldClose() && telemetry_.framesRendered == beforeIterFrames) {
            (void)TickAndPresentFrame(frameDtSec, true);
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
#if defined(_WIN32)
    if (mode_ == NativeWindowMode::Win32WglOpenGL && win32OpenglModule_ && win32Hdc_ && win32Hglrc_) {
        auto wglMakeCur = reinterpret_cast<BOOL (WINAPI*)(HDC, HGLRC)>(
            GetProcAddress(static_cast<HMODULE>(win32OpenglModule_), "wglMakeCurrent"));
        if (wglMakeCur) {
            (void)wglMakeCur(static_cast<HDC>(win32Hdc_), static_cast<HGLRC>(win32Hglrc_));
        }
        if (renderTree_ && activeGpuStream_) {
            (void)EffectiveResourceStore();
            (void)gpuBackend_->PresentStream(*renderTree_, *activeGpuStream_, width_, height_, clearColor_);
        }
    }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (mode_ == NativeWindowMode::LinuxX11OpenGL && x11_ && x11_->display && x11_->window != 0 &&
        x11_->glxContext && x11_->glXMakeCurrent) {
        (void)x11_->glXMakeCurrent(x11_->display, x11_->window, x11_->glxContext);
        if (renderTree_ && activeGpuStream_) {
            (void)EffectiveResourceStore();
            (void)gpuBackend_->PresentStream(*renderTree_, *activeGpuStream_, width_, height_, clearColor_);
        }
    }
#endif
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
