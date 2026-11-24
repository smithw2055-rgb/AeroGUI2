#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/app/AeroAppHost.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach-o/dyld.h>
#elif !defined(__EMSCRIPTEN__)
#  include <unistd.h>
#endif

#ifndef AERO_SAMPLE_APP_TITLE
#define AERO_SAMPLE_APP_TITLE "AeroGUI Sample"
#endif

#ifndef AERO_SAMPLE_APP_WIDTH
#define AERO_SAMPLE_APP_WIDTH 1000u
#endif

#ifndef AERO_SAMPLE_APP_HEIGHT
#define AERO_SAMPLE_APP_HEIGHT 640u
#endif

namespace {

[[nodiscard]] std::filesystem::path ResolveSelfExecutableDir(const char* argv0) {
#if defined(_WIN32)
    std::wstring buf(1024u, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0u) {
            break;
        }
        if (len < buf.size() - 1u) {
            buf.resize(len);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#elif defined(__APPLE__)
    uint32_t size = 0u;
    (void)_NSGetExecutablePath(nullptr, &size);
    if (size > 0u) {
        std::string p(size, '\0');
        if (_NSGetExecutablePath(p.data(), &size) == 0) {
            return std::filesystem::path(p.c_str()).parent_path();
        }
    }
#elif !defined(__EMSCRIPTEN__)
    std::string buf(1024u, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1u);
        if (n <= 0) {
            break;
        }
        if (static_cast<std::size_t>(n) < buf.size() - 1u) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2u);
    }
#endif
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        const auto abs = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec && abs.has_parent_path()) {
            return abs.parent_path();
        }
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

[[nodiscard]] double QueryWorkingSetMb() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
    return 0.0;
#elif !defined(__EMSCRIPTEN__)
    std::ifstream in("/proc/self/statm");
    long pagesTotal = 0;
    long pagesRss = 0;
    if (in >> pagesTotal >> pagesRss) {
        const long pageSz = ::sysconf(_SC_PAGESIZE);
        if (pageSz > 0) {
            return static_cast<double>(pagesRss * pageSz) / (1024.0 * 1024.0);
        }
    }
    return 0.0;
#else
    return 0.0;
#endif
}

[[nodiscard]] bool WritePpmFile(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<char> rgb(pixelCount * 3u);
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        rgb[i * 3u + 0u] = static_cast<char>(rgba[i * 4u + 0u]);
        rgb[i * 3u + 1u] = static_cast<char>(rgba[i * 4u + 1u]);
        rgb[i * 3u + 2u] = static_cast<char>(rgba[i * 4u + 2u]);
    }
    out.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    return out.good();
}

[[nodiscard]] bool HasNonUniformRenderedPixels(const std::vector<std::uint8_t>& rgba) noexcept {
    if (rgba.size() < 4u) {
        return false;
    }
    const std::uint8_t r0 = rgba[0];
    const std::uint8_t g0 = rgba[1];
    const std::uint8_t b0 = rgba[2];
    const std::uint8_t a0 = rgba[3];
    bool anyNonZero = (r0 | g0 | b0 | a0) != 0u;
    bool anyDiff = false;
    for (std::size_t i = 4u; i + 3u < rgba.size(); i += 4u) {
        if ((rgba[i] | rgba[i + 1u] | rgba[i + 2u] | rgba[i + 3u]) != 0u) {
            anyNonZero = true;
        }
        if (rgba[i] != r0 || rgba[i + 1u] != g0 || rgba[i + 2u] != b0 || rgba[i + 3u] != a0) {
            anyDiff = true;
            break;
        }
    }
    return anyNonZero && anyDiff;
}

void PrintUsage() {
    std::printf(
        "%s - AeroGUI 3.0 Standalone Interpreted Sample App\n"
        "Usage: [options]\n"
        "  --headless              Run headlessly using SoftwareRasterizer\n"
        "  --frames <N>            Step N frames before exit/screenshot\n"
        "  --width <W>             Viewport width in DIPs\n"
        "  --height <H>            Viewport height in DIPs\n"
        "  --dpi-scale <S>         Override UI DPI scale factor\n"
        "  --no-dpi                Disable High-DPI scaling\n"
        "  --out <screenshot.ppm>  Write rendered RGBA8 frame as binary PPM (P6)\n"
        "  --software              Use low-memory software presenter\n"
        "  --gpu                   Prefer hardware OpenGL presenter (default)\n"
        "  --stdlib-dll <path>     Path to aerogui_stdlib DLL\n"
        "  --stdlib-source         Fallback to stdlib source tree compilation\n"
        "  --stdlib <dir>          Path to stdlib source dir (implies --stdlib-source)\n"
        "  --profile-startup       Print startup & lazy compilation profile metrics\n"
        "  -h, --help              Show this help\n",
        AERO_SAMPLE_APP_TITLE);
}

} // namespace

int main(int argc, char** argv) {
    const auto tProcStart = std::chrono::steady_clock::now();

    bool headless = false;
    std::uint64_t frames = 0u;
    bool framesSet = false;
    std::uint32_t width = static_cast<std::uint32_t>(AERO_SAMPLE_APP_WIDTH);
    std::uint32_t height = static_cast<std::uint32_t>(AERO_SAMPLE_APP_HEIGHT);
    bool noDpiFlag = false;
    float dpiScaleOverride = 0.0f;
    std::string outPpmPath;
    bool preferSoftware = false;
    bool softwareFlagSet = false;
    bool useStdlibDll = true;
    bool stdlibDllFlagSet = false;
    std::string stdlibDllPath;
    std::string stdlibDir;
    bool profileStartup = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto needNext = [&](std::string_view opt) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing argument for " << opt << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--frames") {
            const char* v = needNext(arg);
            if (!v) return 2;
            frames = static_cast<std::uint64_t>(std::strtoull(v, nullptr, 10));
            framesSet = true;
        } else if (arg == "--width") {
            const char* v = needNext(arg);
            if (!v) return 2;
            width = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)));
        } else if (arg == "--height") {
            const char* v = needNext(arg);
            if (!v) return 2;
            height = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)));
        } else if (arg == "--dpi-scale") {
            const char* v = needNext(arg);
            if (!v) return 2;
            dpiScaleOverride = static_cast<float>(std::strtod(v, nullptr));
            noDpiFlag = false;
        } else if (arg == "--no-dpi") {
            noDpiFlag = true;
            dpiScaleOverride = 0.0f;
        } else if (arg == "--out") {
            const char* v = needNext(arg);
            if (!v) return 2;
            outPpmPath = v;
        } else if (arg == "--software") {
            preferSoftware = true;
            softwareFlagSet = true;
        } else if (arg == "--gpu") {
            preferSoftware = false;
            softwareFlagSet = true;
        } else if (arg == "--stdlib-dll") {
            const char* v = needNext(arg);
            if (!v) return 2;
            stdlibDllPath = v;
            useStdlibDll = true;
            stdlibDllFlagSet = true;
        } else if (arg == "--stdlib-source") {
            useStdlibDll = false;
        } else if (arg == "--stdlib") {
            const char* v = needNext(arg);
            if (!v) return 2;
            stdlibDir = v;
            if (!stdlibDllFlagSet) {
                useStdlibDll = false;
            }
        } else if (arg == "--profile-startup") {
            profileStartup = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

#ifndef AERO_HAS_COMPILER_V3
    std::cerr << "Standalone sample requires AERO_HAS_COMPILER_V3.\n";
    return 1;
#else
    const std::filesystem::path exeDir = ResolveSelfExecutableDir(argc > 0 ? argv[0] : nullptr);
    aero::AeroV3CompileInput compileInput;
    aero::AeroAppHostConfig hostCfg;
    if (!aero::DiscoverSampleProject(exeDir, compileInput, hostCfg)) {
        std::cerr << "Failed to discover bundled sample project adjacent to executable: "
                  << exeDir.generic_string() << "\n";
        return 1;
    }

    compileInput.includeStdlib = true;
    compileInput.useStdlibDll = useStdlibDll;
    compileInput.stdlibDllPath = stdlibDllPath;
    compileInput.retainStdlibDllCache = false;
    compileInput.treeShakeStdlib = false;
    compileInput.optimizeBytecode = false;
    compileInput.emitAotCpp = false;
    compileInput.compilerSession = nullptr;
    if (useStdlibDll) {
        compileInput.stdlibRootDir.clear();
        compileInput.allowDiskCache = false;
        compileInput.allowStdlibCache = false;
        compileInput.cacheDir.clear();
    } else {
        compileInput.stdlibRootDir = stdlibDir;
        compileInput.allowDiskCache = false;
        compileInput.allowStdlibCache = false;
    }

    hostCfg.windowTitle = AERO_SAMPLE_APP_TITLE;
    hostCfg.viewportWidth = width;
    hostCfg.viewportHeight = height;
    hostCfg.headless = headless;
    hostCfg.enableHighDpi = !headless && !noDpiFlag;
    hostCfg.dpiScale = noDpiFlag ? 0.0f : dpiScaleOverride;
    if (softwareFlagSet) {
        hostCfg.preferSoftwareRasterizer = preferSoftware;
    }
    hostCfg.optimizeBytecode = false;
    hostCfg.enableStencilJit = false;

    aero::AeroAppHost host(hostCfg);
    std::string err;
    if (!host.CompileAndLoad(compileInput, &err)) {
        std::cerr << err << "\n";
        return 1;
    }

    const auto tReady = std::chrono::steady_clock::now();

    auto printProfile = [&](double firstFrameMs) {
        if (!profileStartup) {
            return;
        }
        const auto* cr = host.LastCompileResult();
        const double dllMs = cr ? cr->stdlibDllLoadMs : 0.0;
        const double compMs = cr ? cr->compileFrontendAndMirMs : 0.0;
        const double lnkMs = cr ? cr->linkMs : 0.0;
        const auto tNow = std::chrono::steady_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(tNow - tProcStart).count();
        const auto prog = host.Program();
        const std::size_t compiledLazy = prog ? prog->compiledLazyFunctionCount() : 0u;
        const std::size_t totalLazy = prog ? prog->totalLazyFunctionCount() : 0u;
        const double ratioPct = totalLazy > 0u
            ? (100.0 * static_cast<double>(compiledLazy) / static_cast<double>(totalLazy))
            : 0.0;
        const double rssMb = QueryWorkingSetMb();
        std::printf(
            "[PROFILE] mode=%s dll_load_ms=%.2f compile_ms=%.2f link_ms=%.2f first_frame_ms=%.2f total_ms=%.2f lazy_compiled=%zu/%zu (%.1f%%) rss_mb=%.2f\n",
            (cr && cr->usedStdlibDll) ? "dll" : "source",
            dllMs,
            compMs,
            lnkMs,
            firstFrameMs,
            totalMs,
            compiledLazy,
            totalLazy,
            ratioPct,
            rssMb);
        std::fflush(stdout);
    };

    if (headless) {
        const std::uint64_t stepCount = framesSet ? std::max<std::uint64_t>(1u, frames) : 1u;
        double firstFrameMs = 0.0;
        for (std::uint64_t f = 0u; f < stepCount; ++f) {
            const auto tf0 = std::chrono::steady_clock::now();
            host.StepFrame(1.0f / 60.0f, f + 1u == stepCount);
            if (f == 0u) {
                const auto tf1 = std::chrono::steady_clock::now();
                firstFrameMs = std::chrono::duration<double, std::milli>(tf1 - tf0).count();
            }
        }
        std::vector<std::uint8_t> rgba;
        if (!host.RenderHeadlessRgba(width, height, rgba)) {
            std::cerr << "RenderHeadlessRgba failed.\n";
            return 1;
        }
        if (!HasNonUniformRenderedPixels(rgba)) {
            std::cerr << "Headless render assertion failed: blank or uniform frame.\n";
            return 1;
        }
        if (!outPpmPath.empty() && !WritePpmFile(outPpmPath, width, height, rgba)) {
            std::cerr << "Failed to write PPM output: " << outPpmPath << "\n";
            return 1;
        }
        printProfile(firstFrameMs);
        std::printf(
            "%s headless OK (%ux%u, %llu frames, %zu nodes)\n",
            AERO_SAMPLE_APP_TITLE,
            width,
            height,
            static_cast<unsigned long long>(stepCount),
            host.World().LiveNodeCount());
        return 0;
    }

    (void)tReady;
    const auto tf0 = std::chrono::steady_clock::now();
    host.StepFrame(1.0f / 60.0f, true);
    const auto tf1 = std::chrono::steady_clock::now();
    printProfile(std::chrono::duration<double, std::milli>(tf1 - tf0).count());

    const std::uint64_t rendered = host.RunWindowedLoop(framesSet ? frames : 0u);
    if (!outPpmPath.empty() && !host.SaveScreenshotPpm(outPpmPath, 0u, 0u)) {
        std::cerr << "Failed to write PPM screenshot: " << outPpmPath << "\n";
        return 1;
    }
    std::printf("%s exited cleanly after %llu frames.\n",
        AERO_SAMPLE_APP_TITLE,
        static_cast<unsigned long long>(rendered));
    return 0;
#endif
}
