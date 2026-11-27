#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/app/AeroAppHost.hpp"
#include "aero/app/AppBootstrap.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"

namespace {

void PrintUsage() {
    std::printf(
        "aero-viewer v3.0 - AeroGUI 3.0 Sample & Composition Viewer\n"
        "Usage: aero-viewer [options] [sample_dir|file.xaml|file.rs]\n"
        "  --dir <sample_dir>      Load sample directory (.xaml + .rs/.cs files)\n"
        "  --xaml <file.xaml>      Input XAML file (optional; repeatable via script inputs)\n"
        "  --rs <file.rs|file.cs>  Input RealScript / C#-compat file (repeatable)\n"
        "  --entry <ClassOrFn>     Optional entry class name or factory function\n"
        "  --headless              Run headlessly using SoftwareRasterizer\n"
        "  --frames <N>            Step N frames before exit/screenshot (headless default: 1)\n"
        "  --width <W>             Viewport width in DIPs (default: 800)\n"
        "  --height <H>            Viewport height in DIPs (default: 600)\n"
        "  --dpi-scale <S>         Override UI DPI scale factor (enables High-DPI scaling)\n"
        "  --no-dpi                Disable High-DPI scaling (1:1 physical pixels)\n"
        "  --out <screenshot.ppm>  Write rendered RGBA8 frame as binary PPM (P6)\n"
        "  --O2                    Enable optional MIR optimization (default: off / pure interpreter)\n"
        "  --jit, --stencil-jit    Enable StencilJIT native codegen when available (default: off)\n"
        "  --no-tree-shake         Disable stdlib AST/MIR reachability pruning\n"
        "  --software              Use low-memory Win32 GDI software presenter instead of OpenGL\n"
        "  --gpu                   Prefer hardware OpenGL presenter (default)\n"
        "  --stdlib-dll <path>     Path to aerogui_stdlib DLL (default mode: DLL)\n"
        "  --stdlib-source         Load stdlib from source files instead of aerogui_stdlib DLL\n"
        "  --stdlib <dir>          Path to stdlib root directory (implies --stdlib-source)\n"
        "  --cache-dir <dir>       Use custom disk bytecode/stdlib cache directory (source mode)\n"
        "  --no-cache              Disable disk cache\n"
        "  --profile-startup       Print startup, lazy-compile ratio, and working-set RSS metrics\n"
        "  -h, --help              Show this help\n");
}

using aero::app::QueryWorkingSetMb;
using aero::app::HasNonUniformRenderedPixels;

[[nodiscard]] std::string ResolveDefaultViewerCacheDir() {
    return aero::app::ResolveDefaultCacheDir();
}

[[nodiscard]] bool ReadFileText(const std::filesystem::path& path, std::string& out) {
    return aero::app::ReadUtf8TextFile(path, out);
}

[[nodiscard]] bool WritePpmFile(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba) {
    return aero::app::WriteRgba8PpmFile(path, width, height, rgba);
}

void AppendUniqueDir(std::vector<std::string>& list, const std::filesystem::path& dir) {
    if (dir.empty()) {
        return;
    }
    const std::string norm = dir.lexically_normal().generic_string();
    if (std::find(list.begin(), list.end(), norm) == list.end()) {
        list.push_back(norm);
    }
}

[[nodiscard]] bool LooksLikeStdlibDir(const std::filesystem::path& p) noexcept {
    std::error_code ec;
    return std::filesystem::exists(p / "Aero.UI.Core" / "Core.rs", ec) ||
           std::filesystem::exists(p / "Aero.UI" / "Core.rs", ec);
}

[[nodiscard]] std::string ResolveDefaultStdlibDir(const char* argv0) {
#ifdef AERO_STDLIB_DIR
    {
        const std::filesystem::path p(AERO_STDLIB_DIR);
        if (LooksLikeStdlibDir(p)) {
            return p.lexically_normal().generic_string();
        }
    }
#endif
    std::vector<std::filesystem::path> probes{
        std::filesystem::current_path() / "stdlib",
        std::filesystem::current_path().parent_path() / "stdlib",
        std::filesystem::current_path().parent_path().parent_path() / "stdlib",
    };
    if (argv0 && argv0[0] != '\0') {
        const std::filesystem::path exeDir = std::filesystem::path(argv0).parent_path();
        if (!exeDir.empty()) {
            probes.push_back(exeDir / "stdlib");
            probes.push_back(exeDir.parent_path() / "stdlib");
            probes.push_back(exeDir.parent_path().parent_path() / "stdlib");
        }
    }
    for (const auto& candidate : probes) {
        if (LooksLikeStdlibDir(candidate)) {
            return candidate.lexically_normal().generic_string();
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    const auto tProcStart = std::chrono::steady_clock::now();

    std::string sampleDir;
    std::string xamlPath;
    std::vector<std::string> scriptPaths;
    std::string entryName;
    bool headless = false;
    std::uint64_t frames = 0u;
    bool framesSet = false;
    std::uint32_t width = 800u;
    std::uint32_t height = 600u;
    bool noDpiFlag = false;
    float dpiScaleOverride = 0.0f;
    std::string outPpmPath;
    bool optimizeO2 = false;
    bool enableStencilJit = false;
    bool disableTreeShake = false;
    bool preferSoftware = false;
    bool softwareFlagSet = false;
#ifdef __EMSCRIPTEN__
    bool useStdlibDll = false;
#else
    bool useStdlibDll = true;
#endif
    bool stdlibDllFlagSet = false;
    std::string stdlibDllPath;
    std::string stdlibDir;
    std::string cacheDir;
    bool allowCache = true;
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
        if (arg == "--dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            sampleDir = v;
        } else if (arg == "--xaml") {
            const char* v = needNext(arg);
            if (!v) return 2;
            xamlPath = v;
        } else if (arg == "--rs") {
            const char* v = needNext(arg);
            if (!v) return 2;
            scriptPaths.emplace_back(v);
        } else if (arg == "--entry") {
            const char* v = needNext(arg);
            if (!v) return 2;
            entryName = v;
        } else if (arg == "--headless") {
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
            if (dpiScaleOverride <= 0.0f) {
                std::cerr << "Invalid --dpi-scale value: " << v << "\n";
                return 2;
            }
            noDpiFlag = false;
        } else if (arg == "--no-dpi") {
            noDpiFlag = true;
            dpiScaleOverride = 0.0f;
        } else if (arg == "--out") {
            const char* v = needNext(arg);
            if (!v) return 2;
            outPpmPath = v;
        } else if (arg == "--O2" || arg == "-O2") {
            optimizeO2 = true;
        } else if (arg == "--jit" || arg == "--stencil-jit") {
            enableStencilJit = true;
        } else if (arg == "--no-tree-shake") {
            disableTreeShake = true;
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
        } else if (arg == "--cache-dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            cacheDir = v;
            allowCache = true;
        } else if (arg == "--no-cache") {
            allowCache = false;
            cacheDir.clear();
        } else if (arg == "--profile-startup") {
            profileStartup = true;
        } else if (!arg.empty() && arg.front() != '-') {
            const std::filesystem::path posPath(arg);
            std::error_code posEc;
            if (std::filesystem::is_directory(posPath, posEc)) {
                sampleDir = std::string(arg);
            } else {
                const std::string ext = posPath.extension().string();
                if (aero::str::EqualsIgnoreCase(ext, ".xaml") ||
                    aero::str::EqualsIgnoreCase(ext, ".axaml")) {
                    xamlPath = std::string(arg);
                } else if (aero::str::EqualsIgnoreCase(ext, ".rs") ||
                           aero::str::EqualsIgnoreCase(ext, ".cs")) {
                    scriptPaths.emplace_back(arg);
                } else {
                    std::cerr << "Unknown positional argument: " << arg << "\n";
                    PrintUsage();
                    return 2;
                }
            }
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    if (sampleDir.empty() && xamlPath.empty() && scriptPaths.empty()) {
        PrintUsage();
        return 0;
    }

#ifndef AERO_HAS_COMPILER_V3
    std::cerr << "aero-viewer was built without AERO_HAS_COMPILER_V3 compiler support.\n";
    return 1;
#else
    aero::AeroV3CompileInput compileInput;
    aero::AeroAppHostConfig hostCfg;

    if (!sampleDir.empty()) {
        if (!aero::DiscoverSampleProject(std::filesystem::path(sampleDir), compileInput, hostCfg)) {
            std::cerr << "Failed to discover sample project: " << sampleDir << "\n";
            return 1;
        }
    }

    compileInput.includeStdlib = true;
    compileInput.useStdlibDll = useStdlibDll;
    compileInput.stdlibDllPath = stdlibDllPath;
    compileInput.retainStdlibDllCache = false;
    compileInput.treeShakeStdlib = useStdlibDll ? false : !disableTreeShake;
    compileInput.optimizeBytecode = optimizeO2;
    compileInput.emitAotCpp = false;
    compileInput.compilerSession = nullptr;
    if (useStdlibDll) {
        compileInput.allowDiskCache = allowCache;
        compileInput.allowStdlibCache = false;
        if (allowCache) {
            if (cacheDir.empty()) {
                cacheDir = ResolveDefaultViewerCacheDir();
            }
            compileInput.cacheDir = cacheDir;
        } else {
            compileInput.cacheDir.clear();
        }
        compileInput.stdlibRootDir.clear();
    } else {
        compileInput.allowDiskCache = allowCache;
        compileInput.allowStdlibCache = false;
        if (allowCache) {
            if (cacheDir.empty()) {
                cacheDir = ResolveDefaultViewerCacheDir();
            }
            compileInput.cacheDir = cacheDir;
        } else {
            compileInput.cacheDir.clear();
        }
        if (!stdlibDir.empty()) {
            compileInput.stdlibRootDir = stdlibDir;
        } else if (compileInput.stdlibRootDir.empty()) {
            compileInput.stdlibRootDir = ResolveDefaultStdlibDir(argc > 0 ? argv[0] : nullptr);
        }
    }

    if (!xamlPath.empty()) {
        const std::filesystem::path xp = std::filesystem::path(xamlPath).lexically_normal();
        compileInput.xamlPath = xp.generic_string();
        if (!ReadFileText(xp, compileInput.xamlContent)) {
            std::cerr << "Failed to read XAML file: " << xamlPath << "\n";
            return 1;
        }
        compileInput.additionalXamlSources.erase(
            std::remove_if(
                compileInput.additionalXamlSources.begin(),
                compileInput.additionalXamlSources.end(),
                [&](const aero::SourceFile& sf) {
                    return aero::str::EqualsIgnoreCase(
                        std::filesystem::path(sf.path).lexically_normal().generic_string(),
                        compileInput.xamlPath);
                }),
            compileInput.additionalXamlSources.end());

        if (xp.has_parent_path()) {
            const std::filesystem::path xpDir = xp.parent_path();
            if (compileInput.resourceSearchRoot.empty()) {
                compileInput.resourceSearchRoot = xpDir.generic_string();
            }
            AppendUniqueDir(compileInput.resourceSearchPaths, xpDir);
            AppendUniqueDir(hostCfg.assetSearchDirs, xpDir);
        }
    }

    for (const auto& sp : scriptPaths) {
        const std::filesystem::path spPath = std::filesystem::path(sp).lexically_normal();
        const std::string norm = spPath.generic_string();
        const bool alreadyAdded = std::any_of(
            compileInput.scriptSources.begin(),
            compileInput.scriptSources.end(),
            [&](const aero::SourceFile& sf) {
                return aero::str::EqualsIgnoreCase(
                    std::filesystem::path(sf.path).lexically_normal().generic_string(),
                    norm);
            });
        if (alreadyAdded) {
            continue;
        }
        std::string text;
        if (!ReadFileText(spPath, text)) {
            std::cerr << "Failed to read script file: " << sp << "\n";
            return 1;
        }
        aero::SourceFile sf;
        sf.path = norm;
        sf.content = std::move(text);
        compileInput.scriptSources.push_back(std::move(sf));
    }

    hostCfg.viewportWidth = width;
    hostCfg.viewportHeight = height;
    hostCfg.headless = headless;
    hostCfg.enableHighDpi = !headless && !noDpiFlag;
    hostCfg.dpiScale = noDpiFlag ? 0.0f : dpiScaleOverride;
    if (softwareFlagSet) {
        hostCfg.preferSoftwareRasterizer = preferSoftware;
    }
    hostCfg.optimizeBytecode = optimizeO2;
    hostCfg.enableStencilJit = enableStencilJit;
    if (!entryName.empty()) {
        hostCfg.entryClassName = entryName;
        hostCfg.entryFunctionName = entryName;
    }
    if (!compileInput.resourceSearchRoot.empty()) {
        AppendUniqueDir(hostCfg.assetSearchDirs, std::filesystem::path(compileInput.resourceSearchRoot));
    }

    aero::AeroAppHost host(hostCfg);

    std::string compileErr;
    if (!host.CompileAndLoad(compileInput, &compileErr)) {
        std::cerr << compileErr << "\n";
        return 1;
    }
    compileInput = {};

    auto printStartupProfile = [&](double firstFrameMs) {
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
        const auto tLoop0 = std::chrono::steady_clock::now();
        for (std::uint64_t f = 0u; f < stepCount; ++f) {
            const auto tf0 = std::chrono::steady_clock::now();
            host.StepFrame(1.0f / 60.0f, f + 1u == stepCount);
            if (f == 0u) {
                const auto tf1 = std::chrono::steady_clock::now();
                firstFrameMs = std::chrono::duration<double, std::milli>(tf1 - tf0).count();
            }
        }
        const double stepLoopMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tLoop0).count();

        std::vector<std::uint8_t> rgba;
        if (!host.RenderHeadlessRgba(width, height, rgba)) {
            std::cerr << "RenderHeadlessRgba failed.\n";
            return 1;
        }
        if (!HasNonUniformRenderedPixels(rgba)) {
            std::cerr << "Headless render assertion failed: output image is blank or uniform.\n";
            return 1;
        }
        if (!outPpmPath.empty() && !WritePpmFile(outPpmPath, width, height, rgba)) {
            std::cerr << "Failed to write PPM output: " << outPpmPath << "\n";
            return 1;
        }
        printStartupProfile(firstFrameMs);
        if (profileStartup) {
            const double avgMs = stepLoopMs / static_cast<double>(std::max<std::uint64_t>(1u, stepCount));
            const double fps = avgMs > 0.0001 ? (1000.0 / avgMs) : 0.0;
            const auto& stats = host.GpuStream().LastStats();
            std::printf(
                "[PROFILE_RUN] frames=%llu loop_ms=%.2f avg_frame_ms=%.2f fps=%.1f draw_calls=%zu end_rss_mb=%.2f\n",
                static_cast<unsigned long long>(stepCount),
                stepLoopMs,
                avgMs,
                fps,
                stats.drawCallCount,
                QueryWorkingSetMb());
            std::fflush(stdout);
        }
        std::printf(
            "aero-viewer headless OK (%ux%u, %llu frames, %zu nodes)\n",
            width,
            height,
            static_cast<unsigned long long>(stepCount),
            host.World().LiveNodeCount());
        return 0;
    }

    const auto tf0 = std::chrono::steady_clock::now();
    host.StepFrame(1.0f / 60.0f, true);
    const auto tf1 = std::chrono::steady_clock::now();
    printStartupProfile(std::chrono::duration<double, std::milli>(tf1 - tf0).count());

    std::printf(
        "aero-viewer window ready (%ux%u DIP, %ux%u px, %.2fx DPI).\n",
        host.WindowHost().LogicalWidth(),
        host.WindowHost().LogicalHeight(),
        host.WindowHost().Width(),
        host.WindowHost().Height(),
        static_cast<double>(host.WindowHost().DpiScale()));
    const auto tWinLoop0 = std::chrono::steady_clock::now();
    const std::uint64_t rendered = host.RunWindowedLoop(framesSet ? frames : 0u);
    const double winLoopMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tWinLoop0).count();
    if (profileStartup) {
        const double avgMs = winLoopMs / static_cast<double>(std::max<std::uint64_t>(1u, rendered));
        const double fps = (rendered > 0u && avgMs > 0.0001) ? (1000.0 / avgMs) : 0.0;
        const auto& stats = host.GpuStream().LastStats();
        std::printf(
            "[PROFILE_RUN] frames=%llu loop_ms=%.2f avg_frame_ms=%.2f fps=%.1f draw_calls=%zu end_rss_mb=%.2f\n",
            static_cast<unsigned long long>(rendered),
            winLoopMs,
            avgMs,
            fps,
            stats.drawCallCount,
            QueryWorkingSetMb());
        std::fflush(stdout);
    }
    if (!outPpmPath.empty()) {
        if (!host.SaveScreenshotPpm(outPpmPath, 0u, 0u)) {
            std::cerr << "Failed to write PPM screenshot: " << outPpmPath << "\n";
            return 1;
        }
    }
    std::printf("aero-viewer exited cleanly after %llu frames.\n", static_cast<unsigned long long>(rendered));
    return 0;
#endif
}
