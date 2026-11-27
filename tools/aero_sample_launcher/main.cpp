#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/app/AeroAppHost.hpp"
#include "aero/app/AppBootstrap.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/UIResourceStore.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"

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

void PrintUsage() {
    std::printf(
        "%s - AeroGUI 3.0 Standalone Sample App\n"
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
        "  --jit                   Enable tiered StencilJIT in bytecode/DLL mode\n"
        "  --no-jit                Disable StencilJIT (default)\n"
        "  --jit-threshold <N>     Calls before promoting a UI function to native (default: 32)\n"
        "  --compiled-xaml-catalog Compile catalog XAMLs (Data/Samples, Theme brushes, Languages)\n"
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
    bool enableStencilJit = false;
    std::uint32_t jitThreshold = 32u;
    bool compiledXamlCatalog = false;

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
        } else if (arg == "--jit") {
            enableStencilJit = true;
        } else if (arg == "--no-jit") {
            enableStencilJit = false;
        } else if (arg == "--jit-threshold") {
            const char* v = needNext(arg);
            if (!v) return 2;
            jitThreshold = static_cast<std::uint32_t>(std::max<unsigned long long>(1ull, std::strtoull(v, nullptr, 10)));
        } else if (arg == "--compiled-xaml-catalog") {
            compiledXamlCatalog = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    const std::filesystem::path exeDir = aero::app::ResolveExecutableDir(argc > 0 ? argv[0] : nullptr);

#if defined(AERO_SAMPLE_PRECOMPILED_ARBC)
    const std::filesystem::path precompiledArbc = exeDir / AERO_SAMPLE_PRECOMPILED_ARBC;
    if (!std::filesystem::is_regular_file(precompiledArbc)) {
        std::cerr << "Missing precompiled bytecode bundle: " << precompiledArbc.generic_string() << "\n";
        return 1;
    }
    aero::AeroAppHostConfig hostCfg;
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
    hostCfg.enableStencilJit = enableStencilJit;
    hostCfg.stencilJitHotnessThreshold = jitThreshold;

    aero::AeroAppHost host(hostCfg);
    host.DiscoverAssetDirectories(exeDir);
    if (!host.LoadProgramImageFile(precompiledArbc)) {
        std::cerr << host.LastError() << "\n";
        return 1;
    }
#elif !defined(AERO_HAS_COMPILER_V3)
    (void)exeDir;
    std::cerr << "Standalone sample requires AERO_HAS_COMPILER_V3 or AERO_SAMPLE_PRECOMPILED_ARBC.\n";
    return 1;
#else
    aero::AeroV3CompileInput compileInput;
    aero::AeroAppHostConfig hostCfg;
    if (!aero::DiscoverSampleProject(exeDir, compileInput, hostCfg)) {
        std::cerr << "Failed to discover bundled sample project adjacent to executable: "
                  << exeDir.generic_string() << "\n";
        return 1;
    }

    compileInput.includeStdlib = true;
    compileInput.useStdlibDll = useStdlibDll;
    if (compiledXamlCatalog) {
        aero::DiscoverCatalogXamlSources(exeDir, compileInput.catalogXamlSources);
    }
    compileInput.stdlibDllPath = stdlibDllPath;
    compileInput.retainStdlibDllCache = false;
    compileInput.treeShakeStdlib = false;
    compileInput.optimizeBytecode = false;
    compileInput.emitAotCpp = false;
    compileInput.compilerSession = nullptr;
    compileInput.allowDiskCache = false;
    compileInput.allowStdlibCache = false;
    if (useStdlibDll) {
        compileInput.stdlibRootDir.clear();
        compileInput.cacheDir.clear();
    } else {
        compileInput.stdlibRootDir = stdlibDir;
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
    hostCfg.enableStencilJit = enableStencilJit;
    hostCfg.stencilJitHotnessThreshold = jitThreshold;

    aero::AeroAppHost host(hostCfg);
    std::string err;
    if (!host.CompileAndLoad(compileInput, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    compileInput = {};
#endif

    auto printProfile = [&](double firstFrameMs) {
        if (!profileStartup) {
            return;
        }
#ifdef AERO_HAS_COMPILER_V3
        const auto* cr = host.LastCompileResult();
        const double dllMs = cr ? cr->stdlibDllLoadMs : 0.0;
        const double compMs = cr ? cr->compileFrontendAndMirMs : 0.0;
        const double lnkMs = cr ? cr->linkMs : 0.0;
        const char* modeName = (cr && cr->usedStdlibDll) ? "dll" : "source";
#else
        const double dllMs = 0.0;
        const double compMs = 0.0;
        const double lnkMs = 0.0;
        const char* modeName = "precompiled-arbc";
#endif
        const auto tNow = std::chrono::steady_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(tNow - tProcStart).count();
        const auto prog = host.Program();
        const std::size_t compiledLazy = prog ? prog->compiledLazyFunctionCount() : 0u;
        const std::size_t totalLazy = prog ? prog->totalLazyFunctionCount() : 0u;
        const double ratioPct = totalLazy > 0u
            ? (100.0 * static_cast<double>(compiledLazy) / static_cast<double>(totalLazy))
            : 0.0;
        const double rssMb = aero::app::QueryWorkingSetMb();
        const auto& uiHeap = host.Domains().uiHeap();
        const auto& simHeap = host.Domains().simHeap();
        const auto& gcStats = uiHeap.statistics();
        const auto resStore = host.ResourceStore();
        const double uiHeapMb = static_cast<double>(uiHeap.liveBytes()) / (1024.0 * 1024.0);
        const double simHeapMb = static_cast<double>(simHeap.liveBytes()) / (1024.0 * 1024.0);
        const double worldMb = static_cast<double>(host.World().EstimatedMemoryBytes()) / (1024.0 * 1024.0);
        const double renderMb = static_cast<double>(host.RenderTree().EstimatedMemoryBytes()) / (1024.0 * 1024.0);
        const double resStoreMb = resStore ? static_cast<double>(resStore->EstimatedMemoryBytes()) / (1024.0 * 1024.0) : 0.0;
        const double fontMb = (resStore && resStore->TryFonts())
            ? static_cast<double>(resStore->TryFonts()->EstimatedMemoryBytes()) / (1024.0 * 1024.0) : 0.0;
        const double imgMb = (resStore && resStore->TryImages())
            ? static_cast<double>(resStore->TryImages()->EstimatedMemoryBytes()) / (1024.0 * 1024.0) : 0.0;
        const double resMb = resStoreMb + fontMb + imgMb;
        std::printf(
            "[PROFILE] mode=%s dll_load_ms=%.2f compile_ms=%.2f link_ms=%.2f first_frame_ms=%.2f total_ms=%.2f lazy_compiled=%zu/%zu (%.1f%%) rss_mb=%.2f\n",
            modeName,
            dllMs,
            compMs,
            lnkMs,
            firstFrameMs,
            totalMs,
            compiledLazy,
            totalLazy,
            ratioPct,
            rssMb);
        std::printf(
            "[MEM_BREAKDOWN] rss_mb=%.2f priv_mb=%.2f ui_heap_objs=%zu ui_heap_mb=%.2f (peak=%.2f allocs=%llu) sim_heap_mb=%.2f world_mb=%.2f (nodes=%zu) render_mb=%.2f (nodes=%zu pages=%zu) res_mb=%.2f (fonts=%.2f imgs=%.2f) lazy_lru=%zu\n",
            rssMb,
            aero::app::QueryPrivateUsageMb(),
            uiHeap.liveObjects(),
            uiHeapMb,
            static_cast<double>(gcStats.peakLiveBytes) / (1024.0 * 1024.0),
            static_cast<unsigned long long>(gcStats.objectsAllocated),
            simHeapMb,
            worldMb,
            host.World().LiveNodeCount(),
            renderMb,
            host.RenderTree().NodeCount(),
            host.RenderTree().PageCount(),
            resMb,
            fontMb,
            imgMb,
            prog ? prog->decodedLazyChunkCacheSize() : 0u);
        const auto js = host.StencilJitStats();
        if (js.active) {
            std::printf(
                "[JIT_PROFILE] native_backend=%d tracked=%zu native=%zu threaded=%zu fallback=%zu code_bytes=%zu invoke_hits=%llu fallback_hits=%llu\n",
                js.nativeBackendActive ? 1 : 0,
                js.trackedFunctions,
                js.nativeFunctions,
                js.threadedFunctions,
                js.fallbackFunctions,
                js.machineCodeBytes,
                static_cast<unsigned long long>(js.invokeHits),
                static_cast<unsigned long long>(js.fallbackHits));
        }
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
        if (!aero::app::HasNonUniformRenderedPixels(rgba)) {
            std::cerr << "Headless render assertion failed: blank or uniform frame.\n";
            return 1;
        }
        if (!outPpmPath.empty() && !aero::app::WriteRgba8PpmFile(outPpmPath, width, height, rgba)) {
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
}
