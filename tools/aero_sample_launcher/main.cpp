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
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
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
}

[[nodiscard]] double QueryPrivateUsageMb() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmce{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmce),
                             sizeof(pmce))) {
        return static_cast<double>(pmce.PrivateUsage) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    return 0.0;
#endif
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
        "  --jit                   Enable tiered StencilJIT in bytecode/DLL mode\n"
        "  --no-jit                Disable StencilJIT (default)\n"
        "  --jit-threshold <N>     Calls before promoting a UI function to native (default: 32)\n"
        "  --benchmark-subpages <mode>  Benchmark Gallery subpages (containers|all)\n"
        "  --benchmark-cycles <N>  Number of benchmark cycles (default: 2)\n"
        "  -h, --help              Show this help\n",
        AERO_SAMPLE_APP_TITLE);
}

[[nodiscard]] bool ReadNodeTypeNameForBench(
    const aero::AeroAppHost& host,
    const aero::VisualNode* n,
    std::string& outTypeName) {
    outTypeName.clear();
    if (!n || !n->hasScriptOwner || !n->scriptOwner.valid()) {
        return false;
    }
    const auto* prog = host.Program().get();
    if (!prog) {
        return false;
    }
    const auto tid = host.Domains().uiHeap().objectTypeIdFast(n->scriptOwner);
    const auto* ts = prog->resolveType(tid);
    if (!ts) {
        return false;
    }
    outTypeName = ts->name;
    return !outTypeName.empty();
}

void CollectSubtreeNodesByTypeName(
    const aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view typeName,
    std::vector<aero::VisualNodeId>& outIds) {
    outIds.clear();
    const auto& world = host.World();
    std::vector<aero::VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const auto* n = world.GetNode(q[i]);
        if (!n) continue;
        std::string tn;
        if (q[i] != rootId && ReadNodeTypeNameForBench(host, n, tn) && tn == typeName) {
            outIds.push_back(q[i]);
        }
        for (const auto cid : n->children) {
            q.push_back(cid);
        }
    }
}

[[nodiscard]] aero::Rect NodeScreenBounds(const aero::VisualNode& n) noexcept {
    const aero::Rect r = n.layout.layoutRect;
    const aero::Point p0 = n.layout.worldTransform.TransformPoint(aero::Point{r.x, r.y});
    const aero::Point p1 = n.layout.worldTransform.TransformPoint(aero::Point{r.x + r.width, r.y + r.height});
    return aero::Rect{
        std::min(p0.x, p1.x),
        std::min(p0.y, p1.y),
        std::fabs(p1.x - p0.x),
        std::fabs(p1.y - p0.y)};
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
    std::string benchmarkSubpagesMode;
    std::uint64_t benchmarkCycles = 2u;

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
        } else if (arg == "--benchmark-subpages") {
            const char* v = needNext(arg);
            if (!v) return 2;
            benchmarkSubpagesMode = v;
        } else if (arg == "--benchmark-cycles") {
            const char* v = needNext(arg);
            if (!v) return 2;
            benchmarkCycles = std::max<std::uint64_t>(1u, std::strtoull(v, nullptr, 10));
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
    hostCfg.enableStencilJit = enableStencilJit;
    hostCfg.stencilJitHotnessThreshold = jitThreshold;

    aero::AeroAppHost host(hostCfg);
    std::string err;
    if (!host.CompileAndLoad(compileInput, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    compileInput = {};

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
        std::printf(
            "[MEM_BREAKDOWN] rss_mb=%.2f priv_mb=%.2f ui_heap_objs=%zu ui_heap_mb=%.2f (peak=%.2f allocs=%llu) sim_heap_mb=%.2f world_mb=%.2f (nodes=%zu) render_mb=%.2f (nodes=%zu pages=%zu) res_mb=%.2f (fonts=%.2f imgs=%.2f) lazy_lru=%zu\n",
            rssMb,
            QueryPrivateUsageMb(),
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
        if (!HasNonUniformRenderedPixels(rgba)) {
            std::cerr << "Headless render assertion failed: blank or uniform frame.\n";
            return 1;
        }
        if (!outPpmPath.empty() && !WritePpmFile(outPpmPath, width, height, rgba)) {
            std::cerr << "Failed to write PPM output: " << outPpmPath << "\n";
            return 1;
        }
        printProfile(firstFrameMs);

        if (!benchmarkSubpagesMode.empty()) {
            std::vector<const char*> subpages{
                "ItemsControl", "ComboBox", "ListBox", "ListView", "TreeView", "TabControl",
                "Canvas", "StackPanel", "WrapPanel", "DockPanel", "Grid", "UniformGrid",
                "GroupBox", "Expander", "ScrollViewer",
                "Menu", "ContextMenu", "ToolBar"
            };
            if (benchmarkSubpagesMode == "all") {
                const char* extra[] = {
                    "Button", "RepeatButton", "ToggleButton", "CheckBox", "RadioButton", "Slider",
                    "TextBlock", "TextBox", "PasswordBox", "Hyperlink",
                    "StatusBar", "ProgressBar", "ToolTip",
                    "Brushes", "Image", "Effects", "Blending", "Animation"
                };
                subpages.insert(subpages.end(), std::begin(extra), std::end(extra));
            }
            const auto prog = host.Program();
            double cycle1Total = 0.0, cycle1Max = 0.0, cycle2Total = 0.0, cycle2Max = 0.0;
            double peakRss = QueryWorkingSetMb();
            double cycle1EndRss = 0.0, cycle2EndRss = 0.0;
            std::size_t cycle1EndObjs = 0u, cycle2EndObjs = 0u;
            std::size_t cycle1EndNodes = 0u, cycle2EndNodes = 0u;
            std::vector<double> cycle1Loads, cycle2Loads;

            for (std::uint64_t cyc = 1u; cyc <= benchmarkCycles; ++cyc) {
                double cycTotal = 0.0;
                double cycMax = 0.0;
                for (const char* name : subpages) {
                    const auto t0 = std::chrono::steady_clock::now();
                    aero::UnifiedValue ret{};
                    const bool okCall = host.InvokeScriptMethod(
                        host.RootScriptObject(),
                        "SelectSampleByName",
                        {aero::UnifiedValue{aero::rs_rt::StringValue{std::string(name)}}},
                        &ret);
                    bool okLoad = false;
                    (void)ret.tryGetBool(okLoad);
                    const auto t1 = std::chrono::steady_clock::now();
                    if (!okCall || !okLoad) {
                        std::cerr << "SelectSampleByName failed for " << name << "\n";
                        return 1;
                    }
                    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

                    const auto tf0 = std::chrono::steady_clock::now();
                    host.StepFrame(1.0f / 60.0f, false);
                    host.StepFrame(1.0f / 60.0f, true);
                    std::vector<std::uint8_t> subRgba;
                    if (!host.RenderHeadlessRgba(width, height, subRgba) || !HasNonUniformRenderedPixels(subRgba)) {
                        std::cerr << "Subpage render failed for " << name << "\n";
                        return 1;
                    }
                    const auto tf1 = std::chrono::steady_clock::now();
                    const double frameMs = std::chrono::duration<double, std::milli>(tf1 - tf0).count();

                    const double rss = QueryWorkingSetMb();
                    peakRss = std::max(peakRss, rss);
                    const auto& uh = host.Domains().uiHeap();
                    const std::size_t liveObjs = uh.liveObjects();
                    const double heapMb = static_cast<double>(uh.liveBytes()) / (1024.0 * 1024.0);
                    const std::size_t nodes = host.World().LiveNodeCount();
                    const std::size_t compiledLazy = prog ? prog->compiledLazyFunctionCount() : 0u;
                    const std::size_t totalLazy = prog ? prog->totalLazyFunctionCount() : 0u;

                    cycTotal += loadMs;
                    cycMax = std::max(cycMax, loadMs);
                    if (cyc == 1u) cycle1Loads.push_back(loadMs);
                    else if (cyc == 2u) cycle2Loads.push_back(loadMs);

                    std::printf(
                        "[SUBPAGE_BENCH] cycle=%llu name=%-13s load_ms=%7.2f frame_ms=%6.2f nodes=%4zu heap_objs=%6zu heap_mb=%5.2f rss_mb=%6.2f lazy=%zu/%zu\n",
                        static_cast<unsigned long long>(cyc),
                        name,
                        loadMs,
                        frameMs,
                        nodes,
                        liveObjs,
                        heapMb,
                        rss,
                        compiledLazy,
                        totalLazy);
                }

                // Interactive realization verification at end of Cycle 1
                if (cyc == 1u) {
                    auto benchInteractive = [&](const char* pageName, const char* targetType, auto&& actionFn) -> bool {
                        aero::UnifiedValue ret{};
                        if (!host.InvokeScriptMethod(
                                host.RootScriptObject(),
                                "SelectSampleByName",
                                {aero::UnifiedValue{aero::rs_rt::StringValue{std::string(pageName)}}},
                                &ret)) {
                            return false;
                        }
                        host.StepFrame(1.0f / 60.0f, false);
                        host.StepFrame(1.0f / 60.0f, true);
                        std::vector<std::uint8_t> beforeRgba;
                        if (!host.RenderHeadlessRgba(width, height, beforeRgba)) return false;
                        const auto ti0 = std::chrono::steady_clock::now();
                        if (!actionFn()) return false;
                        host.StepFrame(1.0f / 60.0f, false);
                        host.StepFrame(1.0f / 60.0f, true);
                        std::vector<std::uint8_t> afterRgba;
                        if (!host.RenderHeadlessRgba(width, height, afterRgba)) return false;
                        const auto ti1 = std::chrono::steady_clock::now();
                        const double actMs = std::chrono::duration<double, std::milli>(ti1 - ti0).count();
                        std::size_t diffPx = 0u;
                        for (std::size_t pi = 0u; pi + 3u < beforeRgba.size(); pi += 4u) {
                            if (beforeRgba[pi] != afterRgba[pi] || beforeRgba[pi + 1u] != afterRgba[pi + 1u] ||
                                beforeRgba[pi + 2u] != afterRgba[pi + 2u]) {
                                ++diffPx;
                            }
                        }
                        std::printf(
                            "[INTERACT_BENCH] page=%-10s target=%-12s action_ms=%6.2f changed_px=%zu nodes=%zu\n",
                            pageName, targetType, actMs, diffPx, host.World().LiveNodeCount());
                        return diffPx > 0u;
                    };

                    auto sampleRootOrWorld = [&]() -> aero::VisualNodeId {
                        const aero::VisualNodeId sr = host.FindVisualByOwnerName("SampleContainer");
                        return sr.IsValid() ? sr : host.World().Root();
                    };
                    const bool okTv = benchInteractive("TreeView", "TreeViewItem", [&]() {
                        std::vector<aero::VisualNodeId> items;
                        CollectSubtreeNodesByTypeName(host, sampleRootOrWorld(), "TreeViewItem", items);
                        for (const auto tid : items) {
                            const auto* tn = host.World().GetNode(tid);
                            if (tn && tn->hasScriptOwner && tn->scriptOwner.valid()) {
                                const auto b = NodeScreenBounds(*tn);
                                if (b.x <= 220.0f || b.width <= 20.0f || b.height <= 10.0f) {
                                    continue;
                                }
                                aero::UnifiedValue expVal{};
                                if (host.ReadScriptMember(tn->scriptOwner, "_isExpanded", expVal)) {
                                    bool isExp = false;
                                    if (expVal.tryGetBool(isExp) && !isExp) {
                                        aero::UnifiedValue icVal{};
                                        std::int64_t ic = 0;
                                        if (host.InvokeScriptMethod(tn->scriptOwner, "ItemCount", {}, &icVal) &&
                                            icVal.tryGetInt64(ic) && ic > 0) {
                                            host.InjectClick(b.x + 10.0f, b.y + 10.0f);
                                            return true;
                                        }
                                    }
                                }
                            }
                        }
                        return false;
                    });
                    const bool okCb = benchInteractive("ComboBox", "ComboBox", [&]() {
                        std::vector<aero::VisualNodeId> cbs;
                        CollectSubtreeNodesByTypeName(host, sampleRootOrWorld(), "ComboBox", cbs);
                        for (const auto cid : cbs) {
                            const auto* cn = host.World().GetNode(cid);
                            if (cn && cn->hasScriptOwner && cn->scriptOwner.valid()) {
                                const auto b = NodeScreenBounds(*cn);
                                if (b.width > 40.0f && b.height > 10.0f && b.x > 220.0f) {
                                    host.InjectClick(b.x + b.width - 14.0f, b.y + b.height * 0.5f);
                                    return true;
                                }
                            }
                        }
                        return false;
                    });
                    const bool okExp = benchInteractive("Expander", "Expander", [&]() {
                        std::vector<aero::VisualNodeId> exps;
                        CollectSubtreeNodesByTypeName(host, sampleRootOrWorld(), "Expander", exps);
                        for (const auto eid : exps) {
                            const auto* en = host.World().GetNode(eid);
                            if (en && en->hasScriptOwner && en->scriptOwner.valid()) {
                                const auto b = NodeScreenBounds(*en);
                                if (b.x <= 220.0f || b.width <= 100.0f || b.height <= 20.0f) {
                                    continue;
                                }
                                aero::UnifiedValue ev{};
                                bool isExp = false;
                                if (host.ReadScriptMember(en->scriptOwner, "_isExpanded", ev) &&
                                    ev.tryGetBool(isExp) && !isExp) {
                                    host.InjectClick(b.x + 30.0f, b.y + 12.0f);
                                    return true;
                                }
                            }
                        }
                        return false;
                    });
                    const bool okTab = benchInteractive("TabControl", "TabControl", [&]() {
                        std::vector<aero::VisualNodeId> tcs;
                        CollectSubtreeNodesByTypeName(host, sampleRootOrWorld(), "TabControl", tcs);
                        if (!tcs.empty()) {
                            const auto* tn = host.World().GetNode(tcs[0]);
                            if (tn && tn->hasScriptOwner && tn->scriptOwner.valid()) {
                                return host.InvokeScriptMethod(
                                    tn->scriptOwner,
                                    "SelectTabIndex",
                                    {aero::UnifiedValue{std::int64_t{1}}});
                            }
                        }
                        return false;
                    });
                    if (!okTv || !okCb || !okExp || !okTab) {
                        std::cerr << "Interactive benchmark probe failed (tv=" << okTv
                                  << " cb=" << okCb << " exp=" << okExp << " tab=" << okTab << ")\n";
                        return 1;
                    }
                }

                // Trigger GC + trim at cycle end to measure true retained objects/RSS
                {
                    aero::rs_rt::ShadowStack emptyStack;
                    host.Domains().uiHeap().collectFull(emptyStack);
                    if (host.World().SweepDeadObjectBoxes() > 0u) {
                        host.Domains().uiHeap().collectFull(emptyStack);
                    }
                    (void)host.Domains().uiHeap().trimExcessCapacity();
                }
                if (cyc == 1u) {
                    cycle1Total = cycTotal;
                    cycle1Max = cycMax;
                    cycle1EndRss = QueryWorkingSetMb();
                    cycle1EndObjs = host.Domains().uiHeap().liveObjects();
                    cycle1EndNodes = host.World().LiveNodeCount();
                } else if (cyc == 2u) {
                    cycle2Total = cycTotal;
                    cycle2Max = cycMax;
                    cycle2EndRss = QueryWorkingSetMb();
                    cycle2EndObjs = host.Domains().uiHeap().liveObjects();
                    cycle2EndNodes = host.World().LiveNodeCount();
                }
            }

            auto medianOf = [](std::vector<double> v) -> double {
                if (v.empty()) return 0.0;
                std::sort(v.begin(), v.end());
                const std::size_t mid = v.size() / 2u;
                return (v.size() % 2u == 0u) ? 0.5 * (v[mid - 1u] + v[mid]) : v[mid];
            };
            std::printf(
                "[BENCH_SUMMARY] pages=%zu cycles=%llu c1_total_ms=%.2f c1_max_ms=%.2f c1_med_ms=%.2f c2_total_ms=%.2f c2_max_ms=%.2f c2_med_ms=%.2f peak_rss_mb=%.2f c1_end_rss_mb=%.2f c2_end_rss_mb=%.2f delta_rss_mb=%+.2f end_priv_mb=%.2f c1_objs=%zu c2_objs=%zu delta_objs=%+lld c1_nodes=%zu c2_nodes=%zu lazy_lru=%zu\n",
                subpages.size(),
                static_cast<unsigned long long>(benchmarkCycles),
                cycle1Total,
                cycle1Max,
                medianOf(cycle1Loads),
                cycle2Total,
                cycle2Max,
                medianOf(cycle2Loads),
                peakRss,
                cycle1EndRss,
                cycle2EndRss,
                cycle2EndRss - cycle1EndRss,
                QueryPrivateUsageMb(),
                cycle1EndObjs,
                cycle2EndObjs,
                static_cast<long long>(cycle2EndObjs) - static_cast<long long>(cycle1EndObjs),
                cycle1EndNodes,
                cycle2EndNodes,
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
        }

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
