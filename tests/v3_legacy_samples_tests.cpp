#include "aero/app/AeroAppHost.hpp"
#include "aero/asset/GradientBrush.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

namespace fs = std::filesystem;

int gFailureCount = 0;
bool gUseStdlibDll = false;
std::string gStdlibDllPath;

void ConfigureStdlibForTest(
    aero::AeroV3CompileInput& ci,
    aero::rs_comp::CompilerSession* session,
    bool allowTreeShakeSource = false) {
    ci.includeStdlib = true;
    ci.optimizeBytecode = false;
    ci.emitAotCpp = false;
    if (gUseStdlibDll) {
        ci.useStdlibDll = true;
        ci.stdlibDllPath = gStdlibDllPath;
        ci.stdlibRootDir.clear();
        ci.treeShakeStdlib = false;
        ci.allowDiskCache = false;
        ci.allowStdlibCache = false;
        ci.compilerSession = nullptr;
    } else {
        ci.useStdlibDll = false;
        ci.stdlibDllPath.clear();
        ci.stdlibRootDir = AERO_STDLIB_DIR;
        ci.treeShakeStdlib = allowTreeShakeSource;
        ci.allowDiskCache = true;
        ci.compilerSession = session;
    }
}

void Require(bool cond, std::string_view msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailureCount;
    }
}

[[nodiscard]] std::string ReadFileText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::string NormalizeSlash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

[[nodiscard]] std::string RelativeToSamplesDir(const std::string& pathStr) {
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(pathStr), fs::path(AERO_SAMPLES_DIR), ec);
    if (!ec && !rel.empty()) {
        return NormalizeSlash(rel.string());
    }
    return NormalizeSlash(pathStr);
}

[[nodiscard]] bool HasErrorDiagnostic(const std::vector<std::string>& diags) {
    for (const std::string& d : diags) {
        if (d.find("error:") != std::string::npos || d.find("Error:") != std::string::npos ||
            d.find("ERROR") != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct ImageMetrics {
    std::size_t distinctColors = 0u;
    double nonBlankPercent = 0.0;
    std::uint64_t fnv1a64 = 0u;
};

[[nodiscard]] std::uint32_t PackRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept {
    return (static_cast<std::uint32_t>(r) << 24) |
           (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) |
           static_cast<std::uint32_t>(a);
}

[[nodiscard]] std::uint32_t QuantizedClearColorRgba(const aero::Color& clearColor) {
    aero::PagedRenderTree emptyTree;
    std::vector<std::uint8_t> px;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(emptyTree, 1u, 1u, px, clearColor);
    if (px.size() >= 4u) {
        return PackRgba(px[0], px[1], px[2], px[3]);
    }
    return 0x0F172AFFu;
}

void MaybeDumpScreenshot(
    aero::AeroAppHost& host,
    std::string_view fileName,
    std::uint32_t width,
    std::uint32_t height) {
    const char* dir = std::getenv("AERO_DUMP_SCREENSHOT_DIR");
    if (!dir || dir[0] == '\0') {
        return;
    }
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    const fs::path outPath = fs::path(dir) / fileName;
    Require(host.SaveScreenshotPpm(outPath.string(), width, height),
        "Failed to dump screenshot: " + outPath.string());
}

[[nodiscard]] ImageMetrics ComputeExactImageMetrics(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t clearRgba) {
    ImageMetrics m{};
    const std::size_t pixelCount = rgba.size() / 4u;
    if (pixelCount == 0u) {
        return m;
    }

    std::unordered_set<std::uint32_t> uniqueColors;
    uniqueColors.reserve(1024u);
    std::size_t nonBlank = 0u;

    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    for (std::size_t i = 0u; i < pixelCount; ++i) {
        const std::size_t off = i * 4u;
        const std::uint8_t r = rgba[off];
        const std::uint8_t g = rgba[off + 1u];
        const std::uint8_t b = rgba[off + 2u];
        const std::uint8_t a = rgba[off + 3u];
        const std::uint32_t packed = PackRgba(r, g, b, a);
        uniqueColors.insert(packed);
        if (packed != clearRgba) {
            ++nonBlank;
        }
        hash ^= r; hash *= kPrime;
        hash ^= g; hash *= kPrime;
        hash ^= b; hash *= kPrime;
        hash ^= a; hash *= kPrime;
    }

    m.distinctColors = uniqueColors.size();
    m.nonBlankPercent = (100.0 * static_cast<double>(nonBlank)) / static_cast<double>(pixelCount);
    m.fnv1a64 = hash;
    return m;
}

[[nodiscard]] std::size_t CountChangedPixels(
    const std::vector<std::uint8_t>& a,
    const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return a.size() / 4u;
    }
    std::size_t diffPixels = 0u;
    for (std::size_t i = 0u; i + 3u < a.size(); i += 4u) {
        const int dr = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        const int dg = std::abs(static_cast<int>(a[i + 1u]) - static_cast<int>(b[i + 1u]));
        const int db = std::abs(static_cast<int>(a[i + 2u]) - static_cast<int>(b[i + 2u]));
        const int da = std::abs(static_cast<int>(a[i + 3u]) - static_cast<int>(b[i + 3u]));
        if (dr + dg + db + da > 6) {
            ++diffPixels;
        }
    }
    return diffPixels;
}

[[nodiscard]] bool RequireNodeCenter(
    const aero::AeroAppHost& host,
    std::string_view name,
    float& outCx,
    float& outCy,
    aero::VisualNodeId* outId = nullptr) {
    const aero::VisualNodeId id = host.FindVisualByOwnerName(name);
    if (!id.IsValid()) {
        Require(false, "Expected named VisualNode '" + std::string(name) + "' not found");
        return false;
    }
    const aero::VisualNode* node = host.World().GetNode(id);
    if (!node) {
        Require(false, "Named VisualNode '" + std::string(name) + "' resolved to null node");
        return false;
    }
    const aero::Rect r = node->layout.layoutRect;
    if (r.width <= 1.0f || r.height <= 1.0f) {
        Require(false, "Named VisualNode '" + std::string(name) + "' has degenerate layoutRect (" +
            std::to_string(r.width) + "x" + std::to_string(r.height) + ")");
        return false;
    }
    if (outId) {
        *outId = id;
    }
    const aero::Point localCenter{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const aero::Point vpCenter = node->layout.hasPerspective3D
        ? node->WorldMatrix3D().ProjectPoint2D(localCenter)
        : node->layout.worldTransform.TransformPoint(localCenter);
    outCx = vpCenter.x;
    outCy = vpCenter.y;
    return true;
}

[[nodiscard]] bool ReadIntMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    std::int64_t& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    return v.tryGetInt64(outVal);
}

[[nodiscard]] bool ReadBoolMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    bool& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    return v.tryGetBool(outVal);
}

[[nodiscard]] bool ReadFloatMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    float& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    return v.tryGetFloat(outVal);
}

[[nodiscard]] bool ReadStringMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    std::string& outVal) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&v)) {
        outVal = sv->str();
        return true;
    }
    if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&v);
        ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
        if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
            outVal.assign(opt->data(), opt->size());
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ReadObjectMember(
    const aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view path,
    aero::ObjectRef& outRef) {
    aero::UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&v); ref && ref->valid()) {
        outRef = *ref;
        return true;
    }
    return false;
}

[[nodiscard]] bool ReadListCountMember(
    aero::AeroAppHost& host,
    aero::ObjectRef obj,
    std::string_view listPath,
    std::int64_t& outCount) {
    aero::ObjectRef listRef{};
    if (!ReadObjectMember(host, obj, listPath, listRef)) {
        return false;
    }
    if (listRef.kind == aero::rs_rt::ObjectKind::Array) {
        if (const auto len = host.Domains().uiHeap().arrayLength(listRef)) {
            outCount = static_cast<std::int64_t>(*len);
            return true;
        }
    }
    aero::UnifiedValue arrVal{};
    if (host.ReadScriptMember(listRef, "_items", arrVal)) {
        if (const auto* arrRef = std::get_if<aero::rs_rt::ObjectRef>(&arrVal);
            arrRef && arrRef->valid() && arrRef->kind == aero::rs_rt::ObjectKind::Array) {
            if (const auto len = host.Domains().uiHeap().arrayLength(*arrRef)) {
                outCount = static_cast<std::int64_t>(*len);
                return true;
            }
        }
    }
    aero::UnifiedValue res{};
    if (host.InvokeScriptMethod(listRef, "Count", {}, &res)) {
        return res.tryGetInt64(outCount);
    }
    return false;
}

[[nodiscard]] bool BuildDiscoveredSample(
    const fs::path& sampleDir,
    aero::rs_comp::CompilerSession* session,
    aero::AeroV3CompileInput& outCompileInput,
    aero::AeroAppHostConfig& outHostConfig,
    std::uint32_t width = 320u,
    std::uint32_t height = 180u) {
    outCompileInput = aero::AeroV3CompileInput{};
    outHostConfig = aero::AeroAppHostConfig{};

    const bool ok = aero::DiscoverSampleProject(sampleDir, outCompileInput, outHostConfig);
    if (!ok) {
        // Check if this is a code-first .rs-only sample (e.g. Samples/V3/01_CodeFirstCounter)
        std::error_code ec;
        std::vector<fs::path> scripts;
        for (const auto& entry : fs::directory_iterator(sampleDir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext == ".rs" || ext == ".cs") {
                scripts.push_back(entry.path());
            }
        }
        std::sort(scripts.begin(), scripts.end());
        if (scripts.empty()) {
            return false;
        }
        outCompileInput.xamlPath.clear();
        outCompileInput.xamlContent.clear();
        for (const fs::path& p : scripts) {
            outCompileInput.scriptSources.push_back(aero::SourceFile{p.string(), ReadFileText(p)});
        }
        outCompileInput.includeStdlib = true;
        outCompileInput.treeShakeStdlib = false;
    }

    ConfigureStdlibForTest(outCompileInput, session, /*allowTreeShakeSource=*/false);

    outHostConfig.viewportWidth = width;
    outHostConfig.viewportHeight = height;
    outHostConfig.headless = true;
    outHostConfig.preferSoftwareRasterizer = true;
    return true;
}

void TestDiscoverSampleProjectAndCache(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Test] Project discovery, view instantiation modes, and stdlib/MIR optimization...\n";
    const fs::path samplesRoot(AERO_SAMPLES_DIR);

    auto containsPathPart = [](const std::vector<aero::SourceFile>& list, std::string_view needle) {
        for (const auto& sf : list) {
            if (NormalizeSlash(sf.path).find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    auto containsStringPart = [](const std::vector<std::string>& list, std::string_view needle) {
        for (const auto& s : list) {
            if (NormalizeSlash(s).find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    // 1. Buttons: MainPage.xaml chosen, Theme search path registered, Page root sizes to viewport
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "Buttons", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for Buttons");
        Require(NormalizeSlash(ci.xamlPath).find("Buttons/Data/MainPage.xaml") != std::string::npos,
            "Buttons entry XAML should be Data/MainPage.xaml, got: " + ci.xamlPath);
        Require(containsStringPart(ci.resourceSearchPaths, "Theme/Data/Theme"),
            "DiscoverSampleProject should register Samples/Theme/Data/Theme in resourceSearchPaths");
        Require(containsStringPart(hc.assetSearchDirs, "Buttons/Data"),
            "DiscoverSampleProject should register Buttons/Data in assetSearchDirs");

        aero::AeroV3CompileInput invCi;
        aero::AeroAppHostConfig invHc;
        Require(BuildDiscoveredSample(samplesRoot / "Inventory", &session, invCi, invHc, 640u, 360u),
            "DiscoverSampleProject failed for Inventory");
        Require(containsStringPart(invHc.assetSearchDirs, "Inventory/Data/Images") &&
                    containsStringPart(invHc.assetSearchDirs, "Inventory/Data/Images/Items"),
            "DiscoverSampleProject should register Inventory/Data/Images and Images/Items in assetSearchDirs");

        aero::AeroAppHost host(hc);
        std::string err;
        const bool loaded = host.CompileAndLoad(ci, &err);
        if (!loaded && err.empty()) {
            const auto* cr = host.LastCompileResult();
            err = "(empty outError; host.LastError='" + host.LastError() + "', hasCompileRes=" +
                std::to_string(cr ? 1 : 0) + ", crSucceeded=" +
                std::to_string(cr && cr->succeeded ? 1 : 0) + ", crDiags=" +
                std::to_string(cr ? cr->diagnostics.size() : 0u) + ")";
        }
        Require(loaded, "Buttons CompileAndLoad failed: " + err);
        Require(host.LastCompileResult() && host.LastCompileResult()->rootClassName == "Buttons.MainWindow",
            "Buttons rootClassName should be Buttons.MainWindow");
        host.StepFrame(1.0f / 60.0f, true);
        const aero::VisualNode* root = host.World().GetNode(host.RootVisualNode());
        Require(root && root->layout.layoutRect.width >= 300.0f && root->layout.layoutRect.height >= 180.0f,
            "Buttons Page root should fill viewport");
    }

    // 2. Gallery: MainWindow.xaml chosen (never Resources.xaml or Data/Samples/*.xaml), sub-samples excluded
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "Gallery", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for Gallery");
        Require(NormalizeSlash(ci.xamlPath).find("Gallery/Data/MainWindow.xaml") != std::string::npos,
            "Gallery entry XAML should be Data/MainWindow.xaml, got: " + ci.xamlPath);
        Require(!containsPathPart(ci.additionalXamlSources, "Data/Samples/"),
            "Gallery additionalXamlSources must NOT auto-bundle standalone Data/Samples/*.xaml");
        Require(!containsPathPart(ci.additionalXamlSources, "Resources.xaml"),
            "Gallery additionalXamlSources must NOT include Resources.xaml as a standalone view");
    }

    // 3. Localization: Language-*.xaml excluded from entry selection
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "Localization", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for Localization");
        Require(NormalizeSlash(ci.xamlPath).find("Localization/Data/MainPage.xaml") != std::string::npos,
            "Localization entry XAML should be MainPage.xaml, got: " + ci.xamlPath);
    }

    // 4. UserControl: NumericUpDown.xaml bundled cleanly in additionalXamlSources
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "UserControl", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for UserControl");
        Require(NormalizeSlash(ci.xamlPath).find("UserControl/Data/MainPage.xaml") != std::string::npos,
            "UserControl entry XAML should be MainPage.xaml");
        Require(containsPathPart(ci.additionalXamlSources, "NumericUpDown.xaml"),
            "UserControl should bundle NumericUpDown.xaml in additionalXamlSources");
    }

    // 5. WpfQuickStart: SprintSummaryCard.xaml and App.xaml bundled cleanly
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "WpfQuickStart", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for WpfQuickStart");
        Require(NormalizeSlash(ci.xamlPath).find("WpfQuickStart/Data/MainWindow.xaml") != std::string::npos,
            "WpfQuickStart entry XAML should be MainWindow.xaml");
        Require(containsPathPart(ci.additionalXamlSources, "SprintSummaryCard.xaml"),
            "WpfQuickStart should bundle Data/Controls/SprintSummaryCard.xaml in additionalXamlSources");
        Require(containsPathPart(ci.additionalXamlSources, "App.xaml"),
            "WpfQuickStart should bundle Data/App.xaml in additionalXamlSources");
    }

    // 6. Theme: MainWindow.xaml chosen (Theme/Data/Theme/* excluded from entry & auto-bundle)
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(samplesRoot / "Theme", &session, ci, hc, 640u, 360u),
            "DiscoverSampleProject failed for Theme");
        Require(NormalizeSlash(ci.xamlPath).find("Theme/Data/MainWindow.xaml") != std::string::npos,
            "Theme entry XAML should be MainWindow.xaml, got: " + ci.xamlPath);
        Require(!containsPathPart(ci.additionalXamlSources, "Data/Theme/"),
            "Theme additionalXamlSources must NOT auto-bundle Data/Theme/*.xaml");
    }

    // 7. Standalone <Stem>View instantiation without x:Class (Gallery Brushes.xaml)
    {
        aero::AeroV3CompileInput galleryCi;
        aero::AeroAppHostConfig galleryHc;
        Require(BuildDiscoveredSample(samplesRoot / "Gallery", &session, galleryCi, galleryHc, 320u, 180u),
            "DiscoverSampleProject failed for Gallery");
        const fs::path brushesPath = samplesRoot / "Gallery" / "Data" / "Samples" / "Brushes.xaml";
        aero::AeroV3CompileInput pageCi;
        pageCi.xamlPath = brushesPath.string();
        pageCi.xamlContent = ReadFileText(brushesPath);
        ConfigureStdlibForTest(pageCi, &session, /*allowTreeShakeSource=*/false);
        pageCi.resourceSearchRoot = (samplesRoot / "Gallery" / "Data" / "Samples").string();
        pageCi.resourceSearchPaths = galleryCi.resourceSearchPaths;
        pageCi.resourceSearchPaths.push_back((samplesRoot / "Gallery" / "Data" / "Samples").string());

        aero::AeroAppHost host(galleryHc);
        std::string err;
        Require(host.CompileAndLoad(pageCi, &err), "Standalone Brushes.xaml CompileAndLoad failed: " + err);
        const auto* brushesRes = host.LastCompileResult();
        const std::string_view brushesRoot =
            brushesRes ? std::string_view(brushesRes->rootClassName) : std::string_view{};
        constexpr std::string_view kBrushesSuffix = ".BrushesView";
        const bool isBrushesView =
            brushesRoot == "BrushesView" ||
            (brushesRoot.size() >= kBrushesSuffix.size() &&
             brushesRoot.compare(brushesRoot.size() - kBrushesSuffix.size(), kBrushesSuffix.size(), kBrushesSuffix) == 0);
        Require(isBrushesView,
            "Standalone Brushes.xaml without x:Class should generate *.BrushesView root, got: " +
                std::string(brushesRoot));
        host.StepFrame(1.0f / 60.0f, true);
        Require(host.World().LiveNodeCount() > 0u && host.RenderTree().NodeCount() > 0u,
            "Standalone BrushesView should populate VisualWorld and PagedRenderTree");
    }

    // 8. Cold vs Warm Stdlib Disk Cache + Process-Wide Default Cache + Tree-Shaking Comparison (source mode)
    // or Stdlib DLL Lazy Verification (DLL mode)
    if (gUseStdlibDll) {
        aero::AeroV3CompileInput dllCi;
        aero::AeroAppHostConfig dllHc;
        Require(BuildDiscoveredSample(samplesRoot / "Buttons", nullptr, dllCi, dllHc, 320u, 180u),
            "BuildDiscoveredSample for DLL verification failed");
        const aero::AeroCompilerV3 compiler;
        const auto tDll0 = std::chrono::steady_clock::now();
        const aero::AeroV3CompileResult dllRes = compiler.Compile(dllCi);
        const auto tDll1 = std::chrono::steady_clock::now();
        const double dllMs = std::chrono::duration<double, std::milli>(tDll1 - tDll0).count();
        Require(dllRes.succeeded && !HasErrorDiagnostic(dllRes.diagnostics) && dllRes.usedStdlibDll,
            "DLL mode compile failed on Buttons");
        Require(dllRes.programImage && dllRes.programImage->totalLazyFunctionCount() > 0u,
            "DLL mode ProgramImage must track lazy stdlib functions");
        Require(dllRes.programImage->compiledLazyFunctionCount() <
                dllRes.programImage->totalLazyFunctionCount(),
            "DLL mode Buttons should leave unused stdlib functions uncompiled");
        std::cout << std::fixed << std::setprecision(2)
                  << "[DLL] Buttons uncached_compile_ms=" << dllMs
                  << " dll_load_ms=" << dllRes.stdlibDllLoadMs
                  << " link_ms=" << dllRes.linkMs
                  << " lazy_compiled=" << dllRes.programImage->compiledLazyFunctionCount()
                  << "/" << dllRes.programImage->totalLazyFunctionCount() << "\n";
    } else {
    const fs::path tempCacheDir = fs::path(AERO_TEST_TEMP_DIR) / "v3_legacy_bench_cache";
    std::error_code ec;
    fs::remove_all(tempCacheDir, ec);
    fs::create_directories(tempCacheDir, ec);

    aero::AeroV3CompileInput baseCi;
    aero::AeroAppHostConfig baseHc;
    Require(BuildDiscoveredSample(samplesRoot / "Buttons", nullptr, baseCi, baseHc, 320u, 180u),
        "BuildDiscoveredSample for cache benchmark failed");
    baseCi.compilerSession = nullptr; // exercise disk / process-wide cache path explicitly
    baseCi.cacheDir = tempCacheDir.string();
    baseCi.allowDiskCache = true;

    const aero::AeroCompilerV3 compiler;

    // Cold compile (fresh empty cache dir)
    const auto tCold0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult coldRes = compiler.Compile(baseCi);
    const auto tCold1 = std::chrono::steady_clock::now();
    const double coldMs = std::chrono::duration<double, std::milli>(tCold1 - tCold0).count();
    std::string coldDiag;
    for (const auto& d : coldRes.diagnostics) {
        if (!coldDiag.empty()) { coldDiag += " | "; }
        coldDiag += d;
    }
    Require(coldRes.succeeded && !HasErrorDiagnostic(coldRes.diagnostics),
        "Cold cache compile failed (succeeded=" + std::to_string(coldRes.succeeded ? 1 : 0) +
        ", diags=" + coldDiag + ")");

    // Warm disk-cache compile (fresh CompilerSession, cacheDir populated)
    const auto tWarmDisk0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult warmDiskRes = compiler.Compile(baseCi);
    const auto tWarmDisk1 = std::chrono::steady_clock::now();
    const double warmDiskMs = std::chrono::duration<double, std::milli>(tWarmDisk1 - tWarmDisk0).count();
    Require(warmDiskRes.succeeded && warmDiskRes.usedStdlibCache,
        "Second compile with same cacheDir should hit stdlib disk cache (usedStdlibCache == true)");

    // Process-wide default cache (empty cacheDir -> default temp cache)
    aero::AeroV3CompileInput defaultCacheCi = baseCi;
    defaultCacheCi.cacheDir.clear();
    defaultCacheCi.compilerSession = nullptr;
    const aero::AeroV3CompileResult def1 = compiler.Compile(defaultCacheCi);
    const aero::AeroV3CompileResult def2 = compiler.Compile(defaultCacheCi);
    Require(def1.succeeded && def2.succeeded && def2.usedStdlibCache,
        "Process-wide default stdlib disk cache should succeed and report usedStdlibCache == true on reuse");

    // Unpruned vs Tree-Shaken comparison (Buttons)
    aero::AeroV3CompileInput unprunedCi = baseCi;
    unprunedCi.compilerSession = &session;
    unprunedCi.treeShakeStdlib = false;
    const aero::AeroV3CompileResult unprunedRes = compiler.Compile(unprunedCi);
    Require(unprunedRes.succeeded, "Unpruned Buttons compile failed");

    aero::AeroV3CompileInput prunedCi = baseCi;
    prunedCi.compilerSession = &session;
    prunedCi.treeShakeStdlib = true;
    const aero::AeroV3CompileResult prunedRes = compiler.Compile(prunedCi);
    Require(prunedRes.succeeded, "Tree-shaken Buttons compile failed");
    Require(prunedRes.programImageBytes.size() < unprunedRes.programImageBytes.size(),
        "Tree-shaken .rsb should be smaller than unpruned .rsb");
    Require(prunedRes.prunedMirTypes > 0u && prunedRes.prunedMirFunctions > 0u,
        "Tree-shaking should prune unreachable MIR types and functions");

    // Pre-warm shared session with 1 compile so all 28 benchmark samples measure warm session compiles
    {
        aero::AeroV3CompileInput warmSeed = prunedCi;
        (void)compiler.Compile(warmSeed);
    }

    std::cout << std::fixed << std::setprecision(2)
              << "[OPT] Buttons unpruned_rsb_kb=" << (static_cast<double>(unprunedRes.programImageBytes.size()) / 1024.0)
              << " pruned_rsb_kb=" << (static_cast<double>(prunedRes.programImageBytes.size()) / 1024.0)
              << " mir_types_kept=" << prunedRes.retainedMirTypes
              << " mir_types_pruned=" << prunedRes.prunedMirTypes
              << " mir_fns_kept=" << prunedRes.retainedMirFunctions
              << " mir_fns_pruned=" << prunedRes.prunedMirFunctions
              << " cold_ms=" << coldMs
              << " warm_disk_ms=" << warmDiskMs << "\n";

    fs::remove_all(tempCacheDir, ec);
    }
}

struct SampleBenchRow {
    std::string sampleName;
    std::string entryPath;
    double rsbKb = 0.0;
    double warmCompileMs = 0.0;
    std::size_t liveNodes = 0u;
    std::size_t renderNodes = 0u;
    std::size_t renderPages = 0u;
    double layoutMs = 0.0;
    double rasterMs = 0.0;
    std::size_t distinctColors = 0u;
    double nonBlankPct = 0.0;
    std::uint64_t fnv1a64 = 0u;
    std::size_t mirKeptTypes = 0u;
    std::size_t mirPrunedTypes = 0u;
    std::size_t mirKeptFns = 0u;
    std::size_t mirPrunedFns = 0u;
};

void BenchmarkAndVerifySample(
    const std::string& displayName,
    const fs::path& sampleDir,
    std::uint32_t clearRgba,
    std::vector<SampleBenchRow>& outRows) {
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    if (!BuildDiscoveredSample(sampleDir, nullptr, ci, hc, 320u, 180u)) {
        Require(false, displayName + ": failed to discover sample directory " + sampleDir.string());
        return;
    }

    const aero::AeroCompilerV3 compiler;
    const auto tComp0 = std::chrono::steady_clock::now();
    const aero::AeroV3CompileResult compiled = compiler.Compile(ci);
    const auto tComp1 = std::chrono::steady_clock::now();
    if (!compiled.succeeded || !compiled.programImage || HasErrorDiagnostic(compiled.diagnostics)) {
        std::string errMsg;
        for (const auto& d : compiled.diagnostics) {
            errMsg.append("\n  ").append(d);
        }
        Require(false, displayName + " compile failed:" + errMsg);
        return;
    }
    if (gUseStdlibDll) {
        Require(compiled.usedStdlibDll, displayName + " expected usedStdlibDll == true");
        Require(compiled.programImage->totalLazyFunctionCount() > 0u,
            displayName + " expected totalLazyFunctionCount > 0");
    }

    aero::AeroAppHost host(hc);
    const std::string entryHint = ci.xamlContent.empty() ? std::string{} : compiled.rootClassName;
    if (!host.LoadProgramImage(compiled.programImage, entryHint)) {
        Require(false, displayName + " LoadProgramImage failed: " + host.LastError());
        return;
    }

    // Step 45 frames at 60 FPS for storyboards/animations/bindings to settle cleanly
    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }

    const std::size_t liveNodes = host.World().LiveNodeCount();
    const std::size_t renderNodes = host.RenderTree().NodeCount();
    const std::size_t renderPages = host.RenderTree().Pages().size();
    Require(liveNodes > 0u, displayName + " VisualWorld::LiveNodeCount() must be > 0");
    Require(renderNodes > 0u, displayName + " PagedRenderTree::NodeCount() must be > 0");

    // Force full viewport re-layout to measure layout pass deterministically at 320x180
    host.World().UpdateLayout(319.0f, 180.0f);
    const auto tLay0 = std::chrono::steady_clock::now();
    host.World().UpdateLayout(320.0f, 180.0f);
    const auto tLay1 = std::chrono::steady_clock::now();
    host.StepFrame(0.0f, true);

    std::vector<std::uint8_t> rgba;
    const auto tRas0 = std::chrono::steady_clock::now();
    const bool rendered = host.RenderHeadlessRgba(320u, 180u, rgba);
    const auto tRas1 = std::chrono::steady_clock::now();
    Require(rendered && rgba.size() == 320u * 180u * 4u, displayName + " 320x180 headless render failed");

    const ImageMetrics metrics = ComputeExactImageMetrics(rgba, clearRgba);
    Require(metrics.distinctColors >= 2u,
        displayName + " rendered uniform single-color frame (distinctColors=" +
        std::to_string(metrics.distinctColors) + ")");

    SampleBenchRow row;
    row.sampleName = displayName;
    if (!ci.xamlContent.empty() && !ci.xamlPath.empty()) {
        row.entryPath = RelativeToSamplesDir(ci.xamlPath);
    } else if (!ci.scriptSources.empty()) {
        row.entryPath = RelativeToSamplesDir(ci.scriptSources.front().path);
    }
    row.rsbKb = static_cast<double>(compiled.programImageBytes.size()) / 1024.0;
    row.warmCompileMs = std::chrono::duration<double, std::milli>(tComp1 - tComp0).count();
    row.liveNodes = liveNodes;
    row.renderNodes = renderNodes;
    row.renderPages = renderPages;
    row.layoutMs = std::chrono::duration<double, std::milli>(tLay1 - tLay0).count();
    row.rasterMs = std::chrono::duration<double, std::milli>(tRas1 - tRas0).count();
    row.distinctColors = metrics.distinctColors;
    row.nonBlankPct = metrics.nonBlankPercent;
    row.fnv1a64 = metrics.fnv1a64;
    row.mirKeptTypes = compiled.retainedMirTypes;
    row.mirPrunedTypes = compiled.prunedMirTypes;
    row.mirKeptFns = compiled.retainedMirFunctions;
    row.mirPrunedFns = compiled.prunedMirFunctions;

    std::cout << std::fixed << std::setprecision(2)
              << "[BENCH] sample=" << row.sampleName
              << " entry=" << row.entryPath
              << " rsb_kb=" << row.rsbKb
              << " compile_ms=" << row.warmCompileMs
              << " live_nodes=" << row.liveNodes
              << " render_nodes=" << row.renderNodes
              << " pages_4kb=" << row.renderPages
              << " layout_ms=" << row.layoutMs
              << " raster_ms=" << row.rasterMs
              << " colors=" << row.distinctColors
              << " non_blank_pct=" << row.nonBlankPct
              << " hash=0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << row.fnv1a64
              << std::dec << std::setfill(' ')
              << " mir_types=" << row.mirKeptTypes << "/" << (row.mirKeptTypes + row.mirPrunedTypes)
              << " mir_fns=" << row.mirKeptFns << "/" << (row.mirKeptFns + row.mirPrunedFns)
              << "\n";

    outRows.push_back(std::move(row));
}

void TestAll23LegacyAnd5V3Samples(std::uint32_t clearRgba) {
    std::cout << "[Test] Running headless build+layout+render suite across all 23 legacy + 5 V3 samples...\n";
    const fs::path samplesRoot(AERO_SAMPLES_DIR);

    const std::array<const char*, 23> kLegacySamples = {
        "BackgroundBlur",
        "BrushShaders",
        "Buttons",
        "CustomAnimation",
        "CustomControl",
        "CustomRender",
        "DataBinding",
        "Gallery",
        "HelloWorld",
        "Integration",
        "IntegrationGLUT",
        "Inventory",
        "Localization",
        "Login",
        "Menu3D",
        "QuestLog",
        "Rive",
        "Scoreboard",
        "Theme",
        "TicTacToe",
        "UserControl",
        "VideoEffect",
        "WpfQuickStart",
    };

    const std::array<const char*, 5> kV3Samples = {
        "01_CodeFirstCounter",
        "02_CompiledXamlMvvm",
        "03_ControlGallery",
        "04_VectorAndCanvas",
        "05_DualDomainSimHud",
    };

    std::vector<SampleBenchRow> rows;
    rows.reserve(28u);

    for (const char* name : kLegacySamples) {
        BenchmarkAndVerifySample(name, samplesRoot / name, clearRgba, rows);
    }
    for (const char* name : kV3Samples) {
        BenchmarkAndVerifySample(std::string("V3/") + name, samplesRoot / "V3" / name, clearRgba, rows);
    }

    Require(rows.size() == 28u, "Expected 28 total benchmark rows (23 legacy + 5 V3)");
}

void TestGalleryStandaloneSubPages(std::uint32_t clearRgba) {
    std::cout << "[Test] Gallery standalone sub-pages (Brushes, Blending, Effects, Button, Slider, Canvas)...\n";
    const fs::path samplesRoot(AERO_SAMPLES_DIR);

    aero::AeroV3CompileInput baseCi;
    aero::AeroAppHostConfig baseHc;
    Require(BuildDiscoveredSample(samplesRoot / "Gallery", nullptr, baseCi, baseHc, 320u, 180u),
        "Failed to discover Gallery base search paths");

    const std::array<const char*, 6> kSubPages = {
        "Brushes.xaml",
        "Blending.xaml",
        "Effects.xaml",
        "Button.xaml",
        "Slider.xaml",
        "Canvas.xaml",
    };

    for (const char* fileName : kSubPages) {
        const fs::path pagePath = samplesRoot / "Gallery" / "Data" / "Samples" / fileName;
        aero::AeroV3CompileInput ci;
        ci.xamlPath = pagePath.string();
        ci.xamlContent = ReadFileText(pagePath);
        Require(!ci.xamlContent.empty(), std::string("Missing Gallery sub-page: ") + fileName);
        ConfigureStdlibForTest(ci, nullptr, /*allowTreeShakeSource=*/false);
        ci.resourceSearchRoot = (samplesRoot / "Gallery" / "Data" / "Samples").string();
        ci.resourceSearchPaths = baseCi.resourceSearchPaths;
        ci.resourceSearchPaths.push_back((samplesRoot / "Gallery" / "Data" / "Samples").string());

        aero::AeroAppHostConfig hc = baseHc;
        hc.viewportWidth = 320u;
        hc.viewportHeight = 180u;

        aero::AeroAppHost host(hc);
        std::cout << "[SUBPAGE_START] Gallery/" << fileName << "\n";
        std::string err;
        Require(host.CompileAndLoad(ci, &err),
            std::string("Gallery sub-page ") + fileName + " failed: " + err);
        const auto* res = host.LastCompileResult();
        Require(res && res->succeeded && !HasErrorDiagnostic(res->diagnostics),
            std::string("Gallery sub-page ") + fileName + " had compile errors");

        for (int f = 0; f < 5; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 4);
        }
        Require(host.World().LiveNodeCount() > 0u && host.RenderTree().NodeCount() > 0u,
            std::string("Gallery sub-page ") + fileName + " produced empty tree");

        std::vector<std::uint8_t> rgba;
        Require(host.RenderHeadlessRgba(320u, 180u, rgba),
            std::string("Gallery sub-page ") + fileName + " RenderHeadlessRgba failed");
        const ImageMetrics m = ComputeExactImageMetrics(rgba, clearRgba);
        Require(m.distinctColors >= 4u,
            std::string("Gallery sub-page ") + fileName + " distinctColors=" + std::to_string(m.distinctColors));
        std::cout << "[SUBPAGE] Gallery/" << fileName
                  << " live_nodes=" << host.World().LiveNodeCount()
                  << " render_nodes=" << host.RenderTree().NodeCount()
                  << " colors=" << m.distinctColors << "\n";
    }
}

[[nodiscard]] aero::VisualNodeId FindDescendantByOwnerName(
    aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view ownerName) {
    const aero::VisualWorld& world = host.World();
    std::vector<aero::VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const aero::VisualNode* n = world.GetNode(q[i]);
        if (!n) { continue; }
        std::string name;
        if (q[i] != rootId &&
            n->hasScriptOwner &&
            n->scriptOwner.valid() &&
            ReadStringMember(host, n->scriptOwner, "Name", name) &&
            name == ownerName) {
            return q[i];
        }
        for (aero::VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
    return aero::VisualNodeId{};
}

void TestInteractiveButtons(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Buttons...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Buttons", nullptr, ci, hc, 800u, 600u),
        "Buttons discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Buttons load failed: " + err);

    // Let 1.0s Intro storyboard translate buttons from X=-200 into view
    for (int f = 0; f < 70; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 69);
    }

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Buttons initial render failed");

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "ViewModel", vmRef),
        "Buttons ViewModel missing");

    std::int64_t start0 = -1;
    std::int64_t settings0 = -1;
    std::string state0;
    Require(ReadIntMember(host, vmRef, "StartCount", start0) && start0 == 0, "Buttons StartCount should start at 0");
    Require(ReadIntMember(host, vmRef, "SettingsCount", settings0) && settings0 == 0, "Buttons SettingsCount should start at 0");
    Require(ReadStringMember(host, vmRef, "State", state0) && state0 == "Menu", "Buttons initial State should be Menu");

    const aero::VisualNodeId startBtnId = host.FindVisualByOwnerName("StartButton");
    const aero::VisualNodeId settingsBtnId = host.FindVisualByOwnerName("SettingsButton");
    Require(startBtnId.IsValid() && settingsBtnId.IsValid(), "Buttons StartButton/SettingsButton visual nodes missing");
    const aero::VisualNode* startBtnNode = host.World().GetNode(startBtnId);
    const aero::VisualNode* settingsBtnNode = host.World().GetNode(settingsBtnId);
    bool startFocused0 = false;
    bool settingsFocused0 = true;
    Require(startBtnNode && ReadBoolMember(host, startBtnNode->scriptOwner, "IsFocused", startFocused0) && startFocused0,
        "Buttons StartButton should start focused");
    Require(settingsBtnNode && ReadBoolMember(host, settingsBtnNode->scriptOwner, "IsFocused", settingsFocused0) && !settingsFocused0,
        "Buttons SettingsButton should start unfocused");

    const aero::VisualNodeId startHostId = FindDescendantByOwnerName(host, startBtnId, "ContentHost");
    const aero::VisualNodeId settingsHostId = FindDescendantByOwnerName(host, settingsBtnId, "ContentHost");
    const aero::VisualNodeId startFocusOuterId = FindDescendantByOwnerName(host, startBtnId, "FocusedOuter");
    const aero::VisualNodeId startFocusHiId = FindDescendantByOwnerName(host, startBtnId, "FocusedOuterHi");
    const aero::VisualNodeId settingsFocusOuterId = FindDescendantByOwnerName(host, settingsBtnId, "FocusedOuter");
    Require(startHostId.IsValid() && settingsHostId.IsValid() &&
            startFocusOuterId.IsValid() && startFocusHiId.IsValid() && settingsFocusOuterId.IsValid(),
        "Buttons template ContentHost/FocusedOuter parts missing");
    const aero::VisualNode* startHost = host.World().GetNode(startHostId);
    const aero::VisualNode* settingsHost = host.World().GetNode(settingsHostId);
    const aero::VisualNode* startFocusOuter = host.World().GetNode(startFocusOuterId);
    const aero::VisualNode* startFocusHi = host.World().GetNode(startFocusHiId);
    const aero::VisualNode* settingsFocusOuter = host.World().GetNode(settingsFocusOuterId);
    Require(startHost && startHost->Text().fgRgba == 0xFFFFFFFFu,
        "Buttons focused StartButton ContentHost should be White (0xFFFFFFFF)");
    Require(settingsHost && settingsHost->Text().fgRgba == 0xFFB58E48u,
        "Buttons unfocused SettingsButton ContentHost should be #FF488EB5 (0xFFB58E48), got " +
        std::to_string(settingsHost ? settingsHost->Text().fgRgba : 0u));
    Require(startFocusOuter && startFocusOuter->Vector().strokeOpacity > 0.9f &&
            startFocusHi && startFocusHi->Vector().strokeOpacity > 0.9f,
        "Buttons focused StartButton FocusedOuter & FocusedOuterHi should have visible strokeOpacity");
    Require(settingsFocusOuter && settingsFocusOuter->Vector().strokeOpacity < 0.1f,
        "Buttons unfocused SettingsButton FocusedOuter should remain hidden");

    float sx = 0.0f, sy = 0.0f;
    float stx = 0.0f, sty = 0.0f;
    Require(RequireNodeCenter(host, "StartButton", sx, sy), "Buttons StartButton missing");
    Require(RequireNodeCenter(host, "SettingsButton", stx, sty), "Buttons SettingsButton missing");

    // Hover SettingsButton -> MouseEnter triggers SetFocusAction -> GotFocus plays AudioSlide.mp3
    const std::size_t soundsBeforeHover = host.PlayedSoundCount();
    host.InjectPointerMove(stx, sty);
    for (int f = 0; f < 18; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 17);
    }
    bool startFocusedHover = true;
    bool settingsFocusedHover = false;
    Require(ReadBoolMember(host, startBtnNode->scriptOwner, "IsFocused", startFocusedHover) && !startFocusedHover &&
            ReadBoolMember(host, settingsBtnNode->scriptOwner, "IsFocused", settingsFocusedHover) && settingsFocusedHover,
        "Buttons hover on SettingsButton should transfer focus from StartButton");
    startHost = host.World().GetNode(startHostId);
    settingsHost = host.World().GetNode(settingsHostId);
    startFocusOuter = host.World().GetNode(startFocusOuterId);
    settingsFocusOuter = host.World().GetNode(settingsFocusOuterId);
    Require(settingsHost && settingsHost->Text().fgRgba == 0xFFFFFFFFu,
        "Buttons newly focused SettingsButton ContentHost should turn White");
    Require(startHost && startHost->Text().fgRgba == 0xFFB58E48u,
        "Buttons unfocused StartButton ContentHost should transition back to #FF488EB5");
    Require(settingsFocusOuter && settingsFocusOuter->Vector().strokeOpacity > 0.9f &&
            startFocusOuter && startFocusOuter->Vector().strokeOpacity < 0.1f,
        "Buttons focus glow strokeOpacity should transfer to SettingsButton");
    Require(host.PlayedSoundCount() > soundsBeforeHover,
        "Buttons hovering SettingsButton should trigger AudioSlide.mp3 via GotFocus");

    host.InjectPointerMove(sx, sy);
    for (int f = 0; f < 18; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 17);
    }
    host.InjectClick(sx, sy);
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t start1 = 0;
    std::string state1;
    Require(ReadIntMember(host, vmRef, "StartCount", start1) && start1 == 1,
        "Buttons StartButton click did not increment StartCount");
    Require(ReadStringMember(host, vmRef, "State", state1) && state1 == "Started",
        "Buttons StartButton click did not transition State to Started");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Buttons post-click render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "Buttons render pixels should update after Start click");

    host.InjectPointerMove(stx, sty);
    host.InjectClick(stx, sty);
    host.StepFrame(1.0f / 60.0f, true);
    std::int64_t settings1 = 0;
    std::string state2;
    Require(ReadIntMember(host, vmRef, "SettingsCount", settings1) && settings1 == 1,
        "Buttons SettingsButton click did not increment SettingsCount");
    Require(ReadStringMember(host, vmRef, "State", state2) && state2 == "Settings",
        "Buttons SettingsButton click did not transition State to Settings");
}

void TestInteractiveLogin(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Login...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Login", nullptr, ci, hc, 800u, 600u),
        "Login discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Login load failed: " + err);

    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "Login VM missing");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Login initial render failed");

    bool ok0 = true;
    std::int64_t attempts0 = -1;
    Require(ReadBoolMember(host, vmRef, "LoginSuccessful", ok0) && !ok0, "Login should start unauthenticated");
    Require(ReadIntMember(host, vmRef, "LoginAttempts", attempts0) && attempts0 == 0, "LoginAttempts should start at 0");

    // Set credentials via UI input boxes + trigger LoginBtn click
    float ux = 0.0f, uy = 0.0f;
    float px = 0.0f, py = 0.0f;
    float lx = 0.0f, ly = 0.0f;
    Require(RequireNodeCenter(host, "UserBox", ux, uy), "Login UserBox missing");
    Require(RequireNodeCenter(host, "PassBox", px, py), "Login PassBox missing");
    Require(RequireNodeCenter(host, "LoginBtn", lx, ly), "Login LoginBtn missing");

    // First verify wrong attempt transitions attempts & message, then valid credentials succeed
    host.InjectClick(ux, uy);
    host.InjectTextInput("noesis");
    host.StepFrame(1.0f / 60.0f, true);

    host.InjectClick(px, py);
    host.InjectTextInput("12345");
    host.StepFrame(1.0f / 60.0f, true);

    host.InjectClick(lx, ly);
    host.StepFrame(1.0f / 60.0f, true);

    bool ok1 = false;
    std::int64_t attempts1 = 0;
    std::string msg1;
    (void)ReadBoolMember(host, vmRef, "LoginSuccessful", ok1);
    (void)ReadIntMember(host, vmRef, "LoginAttempts", attempts1);
    (void)ReadStringMember(host, vmRef, "Message", msg1);

    Require(ok1 && attempts1 >= 1,
        "LoginBtn click should authenticate with real UI input (ok1=" +
        std::string(ok1 ? "true" : "false") + ", attempts=" +
        std::to_string(attempts1) + ", msg='" + msg1 + "')");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Login post-login render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "Login visuals should change after login");
}

void TestInteractiveTicTacToe(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] TicTacToe...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "TicTacToe", nullptr, ci, hc, 800u, 600u),
        "TicTacToe discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "TicTacToe load failed: " + err);

    // Step past the 1.6s Intro storyboard so Board slides from TranslateY=240 to 0 cleanly.
    for (int f = 0; f < 105; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 104);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "TicTacToe VM missing");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "TicTacToe initial render failed");

    std::int64_t cell0Before = -1;
    Require(ReadIntMember(host, vmRef, "Cell0", cell0Before) && cell0Before == 0,
        "TicTacToe Cell0 should start empty (0)");

    float cx = 0.0f, cy = 0.0f;
    Require(RequireNodeCenter(host, "Cell0", cx, cy), "TicTacToe Cell0 element missing");
    host.InjectClick(cx, cy);
    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }

    std::int64_t cell0After = 0;
    Require(ReadIntMember(host, vmRef, "Cell0", cell0After) && cell0After != 0,
        "TicTacToe clicking Cell0 should place Player 1 mark (Cell0 != 0)");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "TicTacToe post-move render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "TicTacToe visuals should change after move");
}

[[nodiscard]] bool TryReadNodeTypeName(
    aero::AeroAppHost& host,
    const aero::VisualNode* n,
    std::string& outTypeName);
[[nodiscard]] aero::VisualNodeId FindDescendantByTypeName(
    aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view typeName);
[[nodiscard]] aero::Rect NodeViewportBounds(const aero::VisualNode& node) noexcept;
void CollectVisualNodesByOwnerName(
    const aero::AeroAppHost& host,
    std::string_view ownerName,
    std::vector<aero::VisualNodeId>& outIds);
void CollectDescendantsByTypeName(
    aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view typeName,
    std::vector<aero::VisualNodeId>& outIds);
void CollectSubtreeText(
    const aero::VisualWorld& world,
    aero::VisualNodeId rootId,
    std::string& outJoined);

void TestInteractiveInventory(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Inventory...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Inventory", nullptr, ci, hc, 800u, 600u),
        "Inventory discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Inventory load failed: " + err);

    for (int f = 0; f < 190; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 189);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "ViewModel", vmRef),
        "Inventory ViewModel missing");

    std::int64_t invCount0 = -1;
    std::int64_t slotsCount = -1;
    std::int64_t totalDps0 = -1;
    Require(ReadListCountMember(host, vmRef, "Inventory", invCount0) && invCount0 == 45,
        "Inventory should contain 45 bag items initially with empty equipment slots");
    Require(ReadListCountMember(host, vmRef, "Player.Slots", slotsCount) && slotsCount == 8,
        "Inventory Player.Slots should contain 8 empty equipment slots");
    Require(ReadIntMember(host, vmRef, "Player.TotalDps", totalDps0) && totalDps0 == 2174,
        "Inventory initial Player.TotalDps should equal base 2174");

    const aero::VisualNodeId statsId = host.FindVisualByOwnerName("Stats");
    Require(statsId.IsValid(), "Inventory Stats grid missing");
    std::string statsText;
    CollectSubtreeText(host.World(), statsId, statsText);
    Require(statsText.find("1423") != std::string::npos,
        "Inventory initial animated base Life stat should reach 1423 (got: " + statsText + ")");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Inventory initial render failed");

    // Equip Inventory[0] (Club, Hand, Dps=8) into empty slot 2 (Hand) -> bag has 44 items, TotalDps +8
    aero::UnifiedValue eqRes{};
    bool eqOk = false;
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "EquipFromInventory",
                {aero::UnifiedValue{std::int64_t{0}}, aero::UnifiedValue{std::int64_t{2}}}, &eqRes) &&
            eqRes.tryGetBool(eqOk) && eqOk,
        "Inventory EquipFromInventory(0, 2) should return true");
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t totalDps1 = -1;
    Require(ReadIntMember(host, vmRef, "Player.TotalDps", totalDps1) && totalDps1 == totalDps0 + 8,
        "EquipFromInventory(0, 2) equipping Club(8) into empty slot should raise TotalDps by 8");

    // Unequip slot 2 (Club) -> bag Inventory.Count() returns to 45
    aero::UnifiedValue uqRes{};
    bool uqOk = false;
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "UnequipSlot",
                {aero::UnifiedValue{std::int64_t{2}}}, &uqRes) &&
            uqRes.tryGetBool(uqOk) && uqOk,
        "Inventory UnequipSlot(2) should return true");
    host.StepFrame(1.0f / 60.0f, true);
    std::int64_t invCount1 = -1;
    Require(ReadListCountMember(host, vmRef, "Inventory", invCount1) && invCount1 == 45,
        "UnequipSlot(2) should return Club to bag (Inventory.Count() == 45)");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Inventory post-equip render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "Inventory UI should update visually after equip");

    // DragDrop compatibility + DataTemplate VSM state + Escape cancel + equip click flow
    const aero::VisualNodeId listBoxId = host.FindVisualByOwnerName("InventoryListBox");
    Require(listBoxId.IsValid(), "Inventory InventoryListBox missing");
    aero::VisualNodeId slotHeadId{};
    aero::VisualNodeId slotLeftId{};
    float headCx = 0.0f, headCy = 0.0f;
    float leftCx = 0.0f, leftCy = 0.0f;
    Require(RequireNodeCenter(host, "SlotHead", headCx, headCy, &slotHeadId), "Inventory SlotHead missing");
    Require(RequireNodeCenter(host, "SlotLeft", leftCx, leftCy, &slotLeftId), "Inventory SlotLeft missing");

    std::vector<aero::VisualNodeId> bagItems;
    CollectDescendantsByTypeName(host, listBoxId, "ListBoxItem", bagItems);
    Require(bagItems.size() >= 2u, "InventoryListBox should realize at least 2 ListBoxItem containers");

    const aero::VisualNode* bag0Node = host.World().GetNode(bagItems[0]);
    Require(bag0Node != nullptr, "Inventory bagItems[0] node missing");
    const aero::Rect bag0Rect = bag0Node->layout.layoutRect;
    const aero::Point bag0Local{bag0Rect.x + bag0Rect.width * 0.5f, bag0Rect.y + bag0Rect.height * 0.5f};
    const aero::Point bag0Vp = bag0Node->layout.hasPerspective3D
        ? bag0Node->WorldMatrix3D().ProjectPoint2D(bag0Local)
        : bag0Node->layout.worldTransform.TransformPoint(bag0Local);

    // Click bagItems[0] (Sword, Category="Hand") -> SlotHead incompatible, SlotLeft compatible
    host.InjectPointerMove(bag0Vp.x, bag0Vp.y);
    host.StepFrame(1.0f / 60.0f, false);
    host.InjectClick(bag0Vp.x, bag0Vp.y);
    for (int f = 0; f < 10; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 9);
    }

    const aero::VisualNode* slotHeadPicked = host.World().GetNode(slotHeadId);
    const aero::VisualNode* slotLeftPicked = host.World().GetNode(slotLeftId);
    Require(slotHeadPicked && !slotHeadPicked->layout.isEnabled,
        "Inventory selecting Hand item should disable incompatible SlotHead");
    Require(slotLeftPicked && slotLeftPicked->layout.isEnabled,
        "Inventory selecting Hand item should keep compatible SlotLeft enabled");
    const aero::VisualNodeId invalidBorderId = FindDescendantByOwnerName(host, slotHeadId, "InvalidDropBorder");
    Require(invalidBorderId.IsValid(), "Inventory SlotHead InvalidDropBorder missing");
    const aero::VisualNode* invalidBorderNode = host.World().GetNode(invalidBorderId);
    Require(invalidBorderNode && invalidBorderNode->box.bgOpacity > 0.3f,
        "Inventory SlotHead InvalidDropBorder should become visible (NonCompatible VSM state)");

    // Press Escape to cancel drag selection -> SlotHead restores Compatible + IsEnabled=true
    host.InjectKey(aero::KeyCode::Escape, true);
    host.InjectKey(aero::KeyCode::Escape, false);
    for (int f = 0; f < 10; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 9);
    }
    slotHeadPicked = host.World().GetNode(slotHeadId);
    invalidBorderNode = host.World().GetNode(invalidBorderId);
    Require(slotHeadPicked && slotHeadPicked->layout.isEnabled,
        "Inventory Escape should cancel selection and re-enable SlotHead");
    Require(invalidBorderNode && invalidBorderNode->box.bgOpacity < 0.1f,
        "Inventory Escape should return InvalidDropBorder bgOpacity near 0");

    // After EquipFromInventory(0, 2) + UnequipSlot(2), SlotLeft is empty and bagItems[1] is MorningStar (Dps=16)
    const aero::VisualNode* bag1Node = host.World().GetNode(bagItems[1]);
    Require(bag1Node != nullptr, "Inventory bagItems[1] node missing");
    const aero::Rect bag1Rect = bag1Node->layout.layoutRect;
    const aero::Point bag1Local{bag1Rect.x + bag1Rect.width * 0.5f, bag1Rect.y + bag1Rect.height * 0.5f};
    const aero::Point bag1Vp = bag1Node->layout.hasPerspective3D
        ? bag1Node->WorldMatrix3D().ProjectPoint2D(bag1Local)
        : bag1Node->layout.worldTransform.TransformPoint(bag1Local);

    host.InjectPointerMove(bag1Vp.x, bag1Vp.y);
    host.StepFrame(1.0f / 60.0f, false);
    host.InjectClick(bag1Vp.x, bag1Vp.y);
    host.StepFrame(1.0f / 60.0f, false);
    host.InjectPointerMove(leftCx, leftCy);
    host.StepFrame(1.0f / 60.0f, false);
    host.InjectClick(leftCx, leftCy);
    for (int f = 0; f < 25; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 24);
    }

    const aero::VisualNode* slotLeftAfter = host.World().GetNode(slotLeftId);
    std::string equippedName;
    const bool readEqOk = slotLeftAfter && slotLeftAfter->hasScriptOwner &&
        ReadStringMember(host, slotLeftAfter->scriptOwner, "Content.Name", equippedName);
    Require(readEqOk && equippedName == "MorningStar",
        "Inventory equipping bagItems[1] onto empty SlotLeft should set Content.Name='MorningStar' (got: '" + equippedName + "')");
    std::int64_t totalDps2 = -1;
    const bool readDps2Ok = ReadIntMember(host, vmRef, "Player.TotalDps", totalDps2);
    Require(readDps2Ok && totalDps2 == totalDps0 + 16,
        "Inventory equipping MorningStar(16) onto empty SlotLeft should raise TotalDps by 16 (got: " +
            std::to_string(totalDps2) + ", expected: " + std::to_string(totalDps0 + 16) + ")");
}

void TestInteractiveQuestLog(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] QuestLog...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "QuestLog", nullptr, ci, hc, 800u, 600u),
        "QuestLog discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "QuestLog load failed: " + err);

    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }
    Require(host.PlayedSoundCount() == 0u,
        "QuestLog initial startup should not play sound while SelectionSoundAction starts disabled (got " +
        std::to_string(host.PlayedSoundCount()) + ")");

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "QuestLog VM missing");

    std::int64_t questCount = 0;
    std::int64_t idx0 = -1;
    std::string title0;
    bool completed0 = false;
    Require(ReadListCountMember(host, vmRef, "Quests", questCount) && questCount == 12,
        "QuestLog should have 12 quests");
    Require(ReadIntMember(host, vmRef, "SelectedIndex", idx0) && idx0 == 0,
        "QuestLog initial SelectedIndex should be 0");
    Require(ReadStringMember(host, vmRef, "SelectedQuest.Title", title0) && !title0.empty(),
        "QuestLog initial SelectedQuest.Title missing");
    Require(ReadBoolMember(host, vmRef, "SelectedQuest.Completed", completed0),
        "QuestLog SelectedQuest.Completed missing");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "QuestLog initial render failed");

    Require(host.InvokeScriptMethod(host.RootScriptObject(), "SelectQuestByIndex",
                {aero::UnifiedValue{std::int64_t{2}}}),
        "QuestLog SelectQuestByIndex(2) failed");
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t idx1 = -1;
    std::string title1;
    bool completedBeforeToggle = false;
    Require(ReadIntMember(host, vmRef, "SelectedIndex", idx1) && idx1 == 2,
        "QuestLog SelectedIndex should update to 2");
    Require(ReadStringMember(host, vmRef, "SelectedQuest.Title", title1) && title1 != title0,
        "QuestLog SelectedQuest.Title should update on selection");
    Require(ReadBoolMember(host, vmRef, "SelectedQuest.Completed", completedBeforeToggle),
        "QuestLog SelectedQuest.Completed should be readable");

    Require(host.InvokeScriptMethod(host.RootScriptObject(), "ToggleSelectedQuestCompleted"),
        "QuestLog ToggleSelectedQuestCompleted failed");
    host.StepFrame(1.0f / 60.0f, true);

    bool completedAfterToggle = false;
    Require(ReadBoolMember(host, vmRef, "SelectedQuest.Completed", completedAfterToggle) &&
            completedAfterToggle != completedBeforeToggle,
        "QuestLog ToggleSelectedQuestCompleted should toggle Completed state");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "QuestLog post-selection render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "QuestLog visuals should change after quest switch");

    // 2. Verify left ScrollBar Thumb/BdMiddle realized geometry and golden pixels
    const aero::VisualNodeId vBarId = host.FindVisualByOwnerName("PART_VerticalScrollBar");
    const aero::VisualNodeId trackId = host.FindVisualByOwnerName("PART_Track");
    const aero::VisualNodeId thumbId = trackId.IsValid()
        ? FindDescendantByTypeName(host, trackId, "Thumb")
        : aero::VisualNodeId{};
    const aero::VisualNodeId bdMidId = host.FindVisualByOwnerName("BdMiddle");
    Require(vBarId.IsValid() && trackId.IsValid() && thumbId.IsValid() && bdMidId.IsValid(),
        "QuestLog PART_VerticalScrollBar / PART_Track / Thumb / BdMiddle missing");
    const aero::VisualNode* vBarNode = host.World().GetNode(vBarId);
    const aero::VisualNode* thumbNode = host.World().GetNode(thumbId);
    const aero::VisualNode* bdMidNode = host.World().GetNode(bdMidId);
    Require(vBarNode && vBarNode->layout.visibility == aero::Visibility::Visible,
        "QuestLog PART_VerticalScrollBar should be visible");
    Require(thumbNode && thumbNode->layout.visibility == aero::Visibility::Visible &&
            thumbNode->layout.layoutRect.height > 40.0f,
        "QuestLog Thumb should be visible with height > 40 (got " +
        std::to_string(thumbNode ? thumbNode->layout.layoutRect.height : 0.0f) + ")");
    Require(bdMidNode && bdMidNode->layout.layoutRect.height > 20.0f,
        "QuestLog BdMiddle should have height > 20 (got " +
        std::to_string(bdMidNode ? bdMidNode->layout.layoutRect.height : 0.0f) + ")");

    auto countPixelsInRect = [](
        const std::vector<std::uint8_t>& rgba,
        std::uint32_t imgW,
        std::uint32_t imgH,
        const aero::Rect& box,
        auto&& pred) -> int {
        const int x0 = std::max(0, static_cast<int>(std::floor(box.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(box.y)));
        const int x1 = std::min(static_cast<int>(imgW), static_cast<int>(std::ceil(box.x + box.width)));
        const int y1 = std::min(static_cast<int>(imgH), static_cast<int>(std::ceil(box.y + box.height)));
        int count = 0;
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const std::size_t o = (static_cast<std::size_t>(py) * imgW + static_cast<std::size_t>(px)) * 4u;
                if (pred(rgba[o], rgba[o + 1u], rgba[o + 2u], rgba[o + 3u])) {
                    ++count;
                }
            }
        }
        return count;
    };

    const aero::Rect thumbVp = NodeViewportBounds(*thumbNode);
    const int thumbGoldPx = countPixelsInRect(frame0, 800u, 600u, thumbVp,
        [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            return a > 200u && r > 90u && r > g && g > b;
        });
    Require(thumbGoldPx >= 20,
        "QuestLog ScrollBar Thumb should render visible golden pixels (got " + std::to_string(thumbGoldPx) + ")");

    // 3. Verify PanelHeaderDetails bottom chevron ornament & InnerFrame double-contour corner/edge strokes
    const aero::VisualNodeId hdrDetailsId = host.FindVisualByOwnerName("PanelHeaderDetails");
    const aero::VisualNodeId innerFrameId = host.FindVisualByOwnerName("InnerFrame");
    Require(hdrDetailsId.IsValid() && innerFrameId.IsValid(),
        "QuestLog PanelHeaderDetails / InnerFrame missing");
    const aero::VisualNode* hdrNode = host.World().GetNode(hdrDetailsId);
    const aero::VisualNode* innerFrameNode = host.World().GetNode(innerFrameId);
    Require(hdrNode && innerFrameNode, "QuestLog PanelHeaderDetails / InnerFrame null");
    const aero::Rect hdrVp = NodeViewportBounds(*hdrNode);
    const aero::Rect chevronBox{
        hdrVp.x + hdrVp.width * 0.5f - 14.0f,
        hdrVp.y + hdrVp.height - 14.0f,
        28.0f,
        16.0f};
    const int chevronPx = countPixelsInRect(frame0, 800u, 600u, chevronBox,
        [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            return a > 200u && r >= 100u && static_cast<int>(r) > static_cast<int>(b) + 20;
        });
    Require(chevronPx >= 8,
        "QuestLog header bottom-center chevron ornament missing (px=" + std::to_string(chevronPx) + ")");

    const aero::Rect innerVp = NodeViewportBounds(*innerFrameNode);
    const aero::Rect cornerBox{innerVp.x, innerVp.y, 26.0f, 26.0f};
    const aero::Rect topEdgeBox{innerVp.x + 28.0f, innerVp.y, std::max(10.0f, innerVp.width - 56.0f), 8.0f};
    const int cornerStrokePx = countPixelsInRect(frame0, 800u, 600u, cornerBox,
        [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            return a > 200u && r > 60u && r > b;
        });
    const int topEdgeStrokePx = countPixelsInRect(frame0, 800u, 600u, topEdgeBox,
        [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            return a > 200u && r > 60u && r > b;
        });
    Require(cornerStrokePx >= 25 && topEdgeStrokePx >= 50,
        "QuestLog InnerFrame corner/edge strokes incomplete (corner=" +
        std::to_string(cornerStrokePx) + ", topEdge=" + std::to_string(topEdgeStrokePx) + ")");

    // 4. Hover card switch + QuestSelected animation + VisualTransition smooth select/unselect & interruption
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "SelectQuestByIndex",
                {aero::UnifiedValue{std::int64_t{0}}}),
        "QuestLog SelectQuestByIndex(0) failed");
    for (int f = 0; f < 30; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 29);
    }

    auto findNamedDescendant = [&](aero::VisualNodeId rootId, std::string_view name) -> aero::VisualNodeId {
        std::vector<aero::VisualNodeId> q{rootId};
        for (std::size_t qi = 0u; qi < q.size(); ++qi) {
            const aero::VisualNode* n = host.World().GetNode(q[qi]);
            if (!n) { continue; }
            if (q[qi] != rootId && n->hasScriptOwner && n->scriptOwner.valid()) {
                std::string ownerName;
                if (ReadStringMember(host, n->scriptOwner, "Name", ownerName) && ownerName == name) {
                    return q[qi];
                }
            }
            for (aero::VisualNodeId cid : n->children) {
                q.push_back(cid);
            }
        }
        return aero::VisualNodeId{};
    };

    const aero::VisualNodeId questsId = host.FindVisualByOwnerName("Quests");
    const aero::VisualNodeId selBorderId = host.FindVisualByOwnerName("SelectedQuestBorder");
    Require(questsId.IsValid() && selBorderId.IsValid(), "QuestLog Quests / SelectedQuestBorder missing");

    std::vector<aero::VisualNodeId> listItems;
    {
        std::vector<aero::VisualNodeId> q{questsId};
        for (std::size_t qi = 0u; qi < q.size(); ++qi) {
            const aero::VisualNode* n = host.World().GetNode(q[qi]);
            if (!n) { continue; }
            std::string tn;
            if (q[qi] != questsId && TryReadNodeTypeName(host, n, tn) && tn == "ListBoxItem") {
                listItems.push_back(q[qi]);
                continue;
            }
            for (aero::VisualNodeId cid : n->children) {
                q.push_back(cid);
            }
        }
    }
    Require(listItems.size() >= 8u,
        "QuestLog Quests should realize ListBoxItem containers (got " + std::to_string(listItems.size()) + ")");

    const aero::VisualNodeId grid0Id = findNamedDescendant(listItems[0], "QuestItemGrid");
    const aero::VisualNodeId grid1Id = findNamedDescendant(listItems[1], "QuestItemGrid");
    const aero::VisualNodeId grid2Id = findNamedDescendant(listItems[2], "QuestItemGrid");
    const aero::VisualNodeId outer0Id = findNamedDescendant(listItems[0], "OuterBorder");
    const aero::VisualNodeId outer1Id = findNamedDescendant(listItems[1], "OuterBorder");
    const aero::VisualNodeId outer2Id = findNamedDescendant(listItems[2], "OuterBorder");
    Require(grid0Id.IsValid() && grid1Id.IsValid() && grid2Id.IsValid() &&
            outer0Id.IsValid() && outer1Id.IsValid() && outer2Id.IsValid(),
        "QuestLog ListBoxItem template QuestItemGrid / OuterBorder parts missing");

    const aero::Rect item1Vp = NodeViewportBounds(*host.World().GetNode(listItems[1]));
    host.InjectPointerMove(item1Vp.x + item1Vp.width * 0.5f, item1Vp.y + item1Vp.height * 0.5f);
    host.StepFrame(1.0f / 60.0f, true);

    const aero::VisualNode* questsNode = host.World().GetNode(questsId);
    std::int64_t hoverIdx1 = -1;
    std::string hoverTitle1;
    Require(questsNode && ReadIntMember(host, questsNode->scriptOwner, "SelectedIndex", hoverIdx1) && hoverIdx1 == 1,
        "Hovering ListBoxItem[1] should select index 1 (got " + std::to_string(hoverIdx1) + ")");
    Require(ReadStringMember(host, vmRef, "SelectedQuest.Title", hoverTitle1) && hoverTitle1 == "Calming the Wake",
        "Hovering ListBoxItem[1] should switch right card to Calming the Wake (got '" + hoverTitle1 + "')");

    const aero::VisualNode* selBorder = host.World().GetNode(selBorderId);
    const aero::VisualNode* grid0 = host.World().GetNode(grid0Id);
    const aero::VisualNode* grid1 = host.World().GetNode(grid1Id);
    Require(selBorder && (selBorder->layout.opacity < 0.95f || selBorder->transform2D.tx < -1.0f),
        "Hover selection should trigger QuestSelected animation on SelectedQuestBorder");
    Require(grid1 && grid1->transform2D.tx > 0.05f && grid1->transform2D.tx < 7.95f,
        "Hovering ListBoxItem[1] should animate QuestItemGrid.X smoothly via VisualTransition (got " +
        std::to_string(grid1 ? grid1->transform2D.tx : -1.0f) + ")");
    Require(grid0 && grid0->transform2D.tx > 0.05f && grid0->transform2D.tx < 7.95f,
        "Leaving ListBoxItem[0] should cancel selection smoothly via VisualTransition (got " +
        std::to_string(grid0 ? grid0->transform2D.tx : -1.0f) + ")");

    // Interrupt mid-transition by moving pointer onto ListBoxItem[2]
    for (int f = 0; f < 2; ++f) {
        host.StepFrame(1.0f / 60.0f, false);
    }
    const aero::Rect item2Vp = NodeViewportBounds(*host.World().GetNode(listItems[2]));
    host.InjectPointerMove(item2Vp.x + item2Vp.width * 0.5f, item2Vp.y + item2Vp.height * 0.5f);
    host.StepFrame(1.0f / 60.0f, true);

    questsNode = host.World().GetNode(questsId);
    std::int64_t hoverIdx2 = -1;
    std::string hoverTitle2;
    Require(questsNode && ReadIntMember(host, questsNode->scriptOwner, "SelectedIndex", hoverIdx2) && hoverIdx2 == 2,
        "Mid-transition hover onto ListBoxItem[2] should select index 2 (got " + std::to_string(hoverIdx2) + ")");
    Require(ReadStringMember(host, vmRef, "SelectedQuest.Title", hoverTitle2) && hoverTitle2 == "Tomen's Curiosity",
        "Mid-transition hover onto ListBoxItem[2] should update card title to Tomen's Curiosity");

    for (int f = 0; f < 35; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 34);
    }
    selBorder = host.World().GetNode(selBorderId);
    grid0 = host.World().GetNode(grid0Id);
    grid1 = host.World().GetNode(grid1Id);
    const aero::VisualNode* grid2 = host.World().GetNode(grid2Id);
    const aero::VisualNode* outer0 = host.World().GetNode(outer0Id);
    const aero::VisualNode* outer1 = host.World().GetNode(outer1Id);
    const aero::VisualNode* outer2 = host.World().GetNode(outer2Id);
    Require(selBorder && selBorder->layout.opacity > 0.99f && std::abs(selBorder->transform2D.tx) < 0.1f,
        "SelectedQuestBorder should settle cleanly after QuestSelected animation");
    Require(grid0 && grid0->transform2D.tx < 0.1f && outer0 && outer0->Vector().fillRgba == 0xFF53646Bu,
        "Previously selected ListBoxItem[0] should settle back to Unselected state");
    Require(grid1 && grid1->transform2D.tx < 0.1f && outer1 && outer1->Vector().fillRgba == 0xFF53646Bu,
        "Interrupted ListBoxItem[1] should settle back to Unselected state");
    Require(grid2 && grid2->transform2D.tx > 7.9f && outer2 && outer2->Vector().fillRgba == 0xFF77ABBDu,
        "Hovered ListBoxItem[2] should settle cleanly to Selected state");

    // 5. Sound effects: SelectionChanged invokes PlaySoundAction once SelectionSoundAction is enabled by Loaded
    Require(host.PlayedSoundCount() >= 2u,
        "QuestLog hover/selection changes should trigger WaterDropSmall.mp3 via PlaySoundAction (got " +
        std::to_string(host.PlayedSoundCount()) + ")");
    if (!host.PlayedSounds().empty()) {
        const auto& lastSound = host.PlayedSounds().back();
        Require(lastSound.uri.find("WaterDropSmall.mp3") != std::string::npos,
            "QuestLog played sound URI mismatch: " + lastSound.uri);
        Require(std::fabs(lastSound.volume - 0.2f) < 0.01f,
            "QuestLog SelectionSoundAction Volume should be 0.2 (got " + std::to_string(lastSound.volume) + ")");
        Require(!lastSound.resolvedPath.empty() && fs::exists(lastSound.resolvedPath),
            "QuestLog WaterDropSmall.mp3 resolved path missing: " + lastSound.resolvedPath);
#if defined(_WIN32)
        std::uint32_t decSr = 0u;
        std::uint16_t decCh = 0u;
        std::vector<std::int16_t> decPcm;
        Require(aero::NativeWindowHost::DecodeSoundFileToPcm16(lastSound.resolvedPath, decSr, decCh, decPcm) &&
                decSr > 0u && decCh >= 1u && decPcm.size() > 100u,
            "Win32 Media Foundation failed to decode WaterDropSmall.mp3 to PCM16");
        bool hasNonZero = false;
        for (std::int16_t s : decPcm) {
            if (s != 0) { hasNonZero = true; break; }
        }
        Require(hasNonZero, "Decoded WaterDropSmall.mp3 PCM16 buffer is completely silent");
#endif
    }

    // 6. Mouse wheel direction on Quests list: wheel down (-1 notch) scrolls down (+offset), wheel up (+1 notch) scrolls back up
    const aero::VisualNodeId qSvId = FindDescendantByTypeName(host, questsId, "ScrollViewer");
    Require(qSvId.IsValid(), "QuestLog Quests ScrollViewer missing");
    const aero::VisualNode* qSvNode = host.World().GetNode(qSvId);
    Require(qSvNode && qSvNode->scriptOwner.valid(), "QuestLog Quests ScrollViewer scriptOwner missing");
    float qOff0 = -1.0f;
    Require(ReadFloatMember(host, qSvNode->scriptOwner, "VerticalOffset", qOff0) && std::fabs(qOff0) < 0.1f,
        "QuestLog initial VerticalOffset should be 0");
    const aero::Rect qVp = NodeViewportBounds(*host.World().GetNode(questsId));
    const float qCx = qVp.x + qVp.width * 0.5f;
    const float qCy = qVp.y + qVp.height * 0.5f;
    host.InjectPointerMove(qCx, qCy);
    host.InjectMouseWheel(qCx, qCy, 0.0f, -1.0f);
    host.StepFrame(1.0f / 60.0f, true);
    float qOffDown = 0.0f;
    Require(ReadFloatMember(host, qSvNode->scriptOwner, "VerticalOffset", qOffDown) &&
            qOffDown > 20.0f && qOffDown < 80.0f,
        "QuestLog wheel down (-1 notch) should scroll DOWN (+~48px), got " + std::to_string(qOffDown));
    host.InjectMouseWheel(qCx, qCy, 0.0f, 1.0f);
    host.StepFrame(1.0f / 60.0f, true);
    float qOffUp = -1.0f;
    Require(ReadFloatMember(host, qSvNode->scriptOwner, "VerticalOffset", qOffUp) && std::fabs(qOffUp) < 1.0f,
        "QuestLog wheel up (+1 notch) should scroll UP back to ~0, got " + std::to_string(qOffUp));

    // 7. ScrollBar Thumb hover highlight (OverState: #FF696140 <-> #FFDCCD8F)
    bdMidNode = host.World().GetNode(bdMidId);
    thumbNode = host.World().GetNode(thumbId);
    const aero::Rect thumbHoverVp = NodeViewportBounds(*thumbNode);
    const aero::GradientBrushRegistry& qGrads = host.World().ResourceStore()->GradientBrushes();
    aero::GradientBrushDesc thumbRestGrad{};
    Require(bdMidNode && bdMidNode->Vector().fillGradId > 0 &&
            qGrads.TryGetBrush(static_cast<std::uint32_t>(bdMidNode->Vector().fillGradId), thumbRestGrad) &&
            !thumbRestGrad.stops.empty() && thumbRestGrad.stops[0].color.r < 0.55f,
        "QuestLog Thumb BdMiddle should rest at muted gold (#FF696140)");

    std::vector<std::uint8_t> preThumbHover;
    Require(host.RenderHeadlessRgba(800u, 600u, preThumbHover), "QuestLog pre-thumb-hover render failed");

    host.InjectPointerMove(
        thumbHoverVp.x + thumbHoverVp.width * 0.5f,
        thumbHoverVp.y + thumbHoverVp.height * 0.5f);
    for (int f = 0; f < 16; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 15);
    }
    bdMidNode = host.World().GetNode(bdMidId);
    aero::GradientBrushDesc thumbOverGrad{};
    Require(bdMidNode && bdMidNode->Vector().fillGradId > 0 &&
            qGrads.TryGetBrush(static_cast<std::uint32_t>(bdMidNode->Vector().fillGradId), thumbOverGrad) &&
            !thumbOverGrad.stops.empty() && thumbOverGrad.stops[0].color.r > 0.75f,
        "QuestLog Thumb hover should animate BdMiddle stop 0 to highlighted gold (#FFDCCD8F)");
    std::vector<std::uint8_t> postThumbHover;
    Require(host.RenderHeadlessRgba(800u, 600u, postThumbHover), "QuestLog post-thumb-hover render failed");
    Require(CountChangedPixels(preThumbHover, postThumbHover) > 0u,
        "QuestLog Thumb hover highlight should change rendered pixels");

    host.InjectPointerMove(5.0f, 5.0f);
    for (int f = 0; f < 22; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 21);
    }
    bdMidNode = host.World().GetNode(bdMidId);
    aero::GradientBrushDesc thumbLeaveGrad{};
    Require(bdMidNode && bdMidNode->Vector().fillGradId > 0 &&
            qGrads.TryGetBrush(static_cast<std::uint32_t>(bdMidNode->Vector().fillGradId), thumbLeaveGrad) &&
            !thumbLeaveGrad.stops.empty() && thumbLeaveGrad.stops[0].color.r < 0.55f,
        "QuestLog Thumb MouseLeave should restore BdMiddle stop 0 back to #FF696140");
}

void TestInteractiveLocalization(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Localization...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Localization", nullptr, ci, hc, 800u, 600u),
        "Localization discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Localization load failed: " + err);

    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "Localization VM missing");

    std::int64_t langCount = 0;
    std::string name0;
    std::string src0;
    Require(ReadListCountMember(host, vmRef, "Languages", langCount) && langCount == 4,
        "Localization should have 4 languages");
    Require(ReadStringMember(host, vmRef, "SelectedLanguage.Name", name0) && name0 == "English",
        "Localization default language should be English");
    Require(ReadStringMember(host, vmRef, "SelectedLanguage.Source", src0) && src0 == "Language-en.xaml",
        "Localization default source should be Language-en.xaml");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Localization initial render failed");
    MaybeDumpScreenshot(host, "localization.ppm", 800u, 600u);

    // Switch to French (index 1)
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "SelectLanguageByIndex",
                {aero::UnifiedValue{std::int64_t{1}}}),
        "Localization SelectLanguageByIndex(1) failed");
    host.StepFrame(1.0f / 60.0f, true);

    std::string name1;
    std::string src1;
    Require(ReadStringMember(host, vmRef, "SelectedLanguage.Name", name1) && name1 == "Français",
        "Localization index 1 should switch SelectedLanguage.Name to Français");
    Require(ReadStringMember(host, vmRef, "SelectedLanguage.Source", src1) && src1 == "Language-fr.xaml",
        "Localization index 1 should switch Source to Language-fr.xaml");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Localization post-switch render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u,
        "Localization render should update visually when switching SelectedLanguage");

    // Switch to Arabic RTL (index 3)
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "SelectLanguageByIndex",
                {aero::UnifiedValue{std::int64_t{3}}}),
        "Localization SelectLanguageByIndex(3) failed");
    host.StepFrame(1.0f / 60.0f, true);

    std::string src3;
    Require(ReadStringMember(host, vmRef, "SelectedLanguage.Source", src3) && src3 == "Language-ar.xaml",
        "Localization index 3 should switch Source to Language-ar.xaml");
}

void TestInteractiveUserControl(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] UserControl...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "UserControl", nullptr, ci, hc, 800u, 600u),
        "UserControl discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "UserControl load failed: " + err);

    for (int f = 0; f < 15; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 14);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "UserControl VM missing");

    std::int64_t str0 = 0;
    std::int64_t pts0 = -1;
    Require(ReadIntMember(host, vmRef, "Strength", str0) && str0 == 10,
        "UserControl initial Strength should be 10");
    Require(ReadIntMember(host, vmRef, "PointsAvailable", pts0) && pts0 == 6,
        "UserControl initial PointsAvailable should be 6");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "UserControl initial render failed");

    // Clamp Strength to available budget (10 + 6 = 16) when requesting 25
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "SetStrength",
                {aero::UnifiedValue{std::int64_t{25}}}),
        "UserControl SetStrength(25) failed");
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t strClamped = 0;
    std::int64_t ptsAfterClamp = -1;
    Require(ReadIntMember(host, vmRef, "Strength", strClamped) && strClamped == 16,
        "UserControl Strength should clamp to 16 under budget (got " + std::to_string(strClamped) + ")");
    Require(ReadIntMember(host, vmRef, "PointsAvailable", ptsAfterClamp) && ptsAfterClamp == 0,
        "UserControl PointsAvailable should drop to 0 at budget ceiling");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "UserControl post-clamp render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u,
        "UserControl visuals should update when StatStr/Strength changes");

    // Also test NumericUpDown IncrementStrength + ResetStats
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "ResetStats"), "ResetStats failed");
    host.StepFrame(1.0f / 60.0f, true);
    std::int64_t strReset = 0;
    Require(ReadIntMember(host, vmRef, "Strength", strReset) && strReset == 10, "ResetStats should restore Strength=10");

    Require(host.InvokeScriptMethod(host.RootScriptObject(), "IncrementStrength"), "IncrementStrength failed");
    host.StepFrame(1.0f / 60.0f, true);
    std::int64_t strInc = 0;
    std::int64_t ptsInc = -1;
    Require(ReadIntMember(host, vmRef, "Strength", strInc) && strInc == 11, "IncrementStrength should set Strength=11");
    Require(ReadIntMember(host, vmRef, "PointsAvailable", ptsInc) && ptsInc == 5, "IncrementStrength should set PointsAvailable=5");
}

void TestInteractiveWpfQuickStart(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] WpfQuickStart...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "WpfQuickStart", nullptr, ci, hc, 800u, 600u),
        "WpfQuickStart discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "WpfQuickStart load failed: " + err);

    for (int f = 0; f < 15; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 14);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
        "WpfQuickStart VM missing");

    std::int64_t count0 = 0;
    std::int64_t rows0 = 0;
    std::int64_t active0 = 0;
    Require(ReadIntMember(host, vmRef, "TaskCount", count0) && count0 == 3,
        "WpfQuickStart initial TaskCount should be 3");
    Require(ReadListCountMember(host, vmRef, "TaskRows", rows0) && rows0 == 3,
        "WpfQuickStart initial TaskRows.Count() should be 3");
    Require(ReadListCountMember(host, vmRef, "ActiveTasks", active0) && active0 == 3,
        "WpfQuickStart initial ActiveTasks.Count() should be 3");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "WpfQuickStart initial render failed");

    // Click AddTaskButton to exercise RelayCommand Binding (AddTaskCommand) end-to-end
    float ax = 0.0f, ay = 0.0f;
    Require(RequireNodeCenter(host, "AddTaskButton", ax, ay), "WpfQuickStart AddTaskButton missing");
    host.InjectClick(ax, ay);
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t count1 = 0;
    std::int64_t rows1 = 0;
    std::int64_t active1 = 0;
    Require(ReadIntMember(host, vmRef, "TaskCount", count1) && count1 == 4,
        "WpfQuickStart clicking AddTaskButton should grow TaskCount to 4 (got " + std::to_string(count1) + ")");
    Require(ReadListCountMember(host, vmRef, "TaskRows", rows1) && rows1 == 4,
        "WpfQuickStart TaskRows.Count() should grow to 4");
    Require(ReadListCountMember(host, vmRef, "ActiveTasks", active1) && active1 == 4,
        "WpfQuickStart ActiveTasks.Count() should grow to 4");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "WpfQuickStart post-add render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u,
        "WpfQuickStart UI should update visually after AddTaskCommand");
}

void TestInteractiveCustomControlAndTheme(aero::rs_comp::CompilerSession& session) {
    // 1. CustomControl clock tick
    {
        std::cout << "[Interactive] CustomControl...\n";
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "CustomControl", nullptr, ci, hc, 800u, 600u),
            "CustomControl discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "CustomControl load failed: " + err);
        for (int f = 0; f < 10; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 9);
        }
        std::vector<std::uint8_t> frame0;
        Require(host.RenderHeadlessRgba(800u, 600u, frame0), "CustomControl initial render failed");

        // Verify dark/black foreground inheritance on Page Background="White", FontSize="40" on Digital clock,
        // and 400x400 analog RadialTicks/RadialNeedle layout
        bool foundAnalogLabel = false;
        bool foundDigitalHour40 = false;
        bool foundRadialTicks400 = false;
        bool foundRadialNeedle400 = false;
        bool foundRotatedContainerBg = false;
        const aero::VisualWorld& cWorld = host.World();
        for (std::size_t s = 0u; s < cWorld.ArenaSlotCount(); ++s) {
            const aero::VisualNode* n = cWorld.NodeAtSlot(s);
            if (!n) { continue; }
            if (n->kind == aero::VisualKind::Text) {
                if (n->Text().text.find("Analog Clock Template") != std::string::npos) {
                    foundAnalogLabel = true;
                    Require(n->Text().fgRgba == 0xFF000000u,
                        "CustomControl label should render in black on White background");
                }
                if (n->Text().fontSize >= 38.0f && n->Text().text == ":") {
                    foundDigitalHour40 = true;
                    Require(n->Text().fgRgba == 0xFF000000u,
                        "CustomControl Digital clock ':' should render in black at FontSize=40");
                }
            }
            std::string tn;
            if (TryReadNodeTypeName(host, n, tn)) {
                if (tn == "RadialTicks" && n->layout.layoutRect.width >= 399.0f && n->layout.layoutRect.height >= 399.0f) {
                    foundRadialTicks400 = true;
                }
                if (tn == "RadialNeedle" && n->layout.layoutRect.width >= 399.0f && n->layout.layoutRect.height >= 399.0f) {
                    foundRadialNeedle400 = true;
                }
            }
            if (n->kind == aero::VisualKind::Box &&
                n->Effect().kind == aero::UIEffectKind::DropShadow &&
                n->box.bgRgba == 0u && n->box.bgGradId <= 0) {
                // Ensure transparent DropShadow containers do not emit opaque/shadow boxes in render tree
                for (std::uint32_t k = 0u; k < n->layout.renderNodeCount; ++k) {
                    const aero::RenderNodeDescriptor* rd = host.RenderTree().GetNode(n->layout.renderNodeId + k);
                    if (rd && rd->sourceElement == n) {
                        foundRotatedContainerBg = true;
                    }
                }
            }
        }
        Require(foundAnalogLabel, "CustomControl 'Analog Clock Template:' TextBlock missing");
        Require(foundDigitalHour40, "CustomControl Digital clock TextElement.FontSize=40 not preserved");
        Require(foundRadialTicks400, "CustomControl RadialTicks should stretch to 400x400");
        Require(foundRadialNeedle400, "CustomControl RadialNeedle should stretch to 400x400");
        Require(!foundRotatedContainerBg, "Transparent DropShadow container should not emit a box primitive");

        Require(host.InvokeScriptMethod(host.RootScriptObject(), "AdvanceAllClocks",
                    {aero::UnifiedValue{std::int64_t{3665}}}),
            "CustomControl AdvanceAllClocks failed");
        for (int f = 0; f < 4; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 3);
        }
        std::vector<std::uint8_t> frame1;
        Require(host.RenderHeadlessRgba(800u, 600u, frame1), "CustomControl post-tick render failed");
        Require(CountChangedPixels(frame0, frame1) > 0u,
            "CustomControl clock hands/digits should update visually after AdvanceAllClocks(3665)");
    }

    // 2. Theme light/dark + accent switching
    {
        std::cout << "[Interactive] Theme...\n";
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Theme", nullptr, ci, hc, 800u, 600u),
            "Theme discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Theme load failed: " + err);
        for (int f = 0; f < 15; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 14);
        }

        aero::ObjectRef vmRef{};
        Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef),
            "Theme VM missing");

        bool dark0 = false;
        std::string accent0;
        Require(ReadBoolMember(host, vmRef, "IsDarkTheme", dark0), "Theme IsDarkTheme unreadable");
        Require(ReadStringMember(host, vmRef, "CurrentAccent", accent0) && !accent0.empty(),
            "Theme CurrentAccent unreadable");

        std::vector<std::uint8_t> frame0;
        Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Theme initial render failed");

        Require(host.InvokeScriptMethod(host.RootScriptObject(), "ToggleTheme"), "Theme ToggleTheme failed");
        for (int f = 0; f < 5; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 4);
        }

        bool dark1 = dark0;
        Require(ReadBoolMember(host, vmRef, "IsDarkTheme", dark1) && dark1 != dark0,
            "Theme ToggleTheme should flip IsDarkTheme");
        std::vector<std::uint8_t> frame1;
        Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Theme post-toggle render failed");
        Require(CountChangedPixels(frame0, frame1) > 0u,
            "Theme visuals should update when switching Dark <-> Light");

        Require(host.InvokeScriptMethod(host.RootScriptObject(), "NextAccent"), "Theme NextAccent failed");
        host.StepFrame(1.0f / 60.0f, true);
        std::string accent1;
        Require(ReadStringMember(host, vmRef, "CurrentAccent", accent1) && accent1 != accent0,
            "Theme NextAccent should update CurrentAccent");
    }
}

void CollectVisualNodesByOwnerName(
    const aero::AeroAppHost& host,
    std::string_view ownerName,
    std::vector<aero::VisualNodeId>& outIds) {
    outIds.clear();
    const aero::VisualWorld& world = host.World();
    const std::size_t slots = world.ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        const aero::VisualNode* n = world.NodeAtSlot(s);
        if (!n || !n->hasScriptOwner || !n->scriptOwner.valid()) {
            continue;
        }
        std::string name;
        if (ReadStringMember(host, n->scriptOwner, "Name", name) && name == ownerName) {
            outIds.push_back(n->id);
        }
    }
}

[[nodiscard]] bool TryReadNodeTypeName(
    aero::AeroAppHost& host,
    const aero::VisualNode* n,
    std::string& outTypeName) {
    if (!n || !n->hasScriptOwner || !n->scriptOwner.valid()) {
        return false;
    }
    aero::UnifiedValue res{};
    if (!host.InvokeScriptMethod(n->scriptOwner, "GetTypeName", {}, &res)) {
        return false;
    }
    if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&res)) {
        outTypeName = sv->str();
        return true;
    }
    if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&res);
        ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
        if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
            outTypeName.assign(opt->data(), opt->size());
            return true;
        }
    }
    return false;
}

[[nodiscard]] aero::VisualNodeId FindDescendantByTypeName(
    aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view typeName) {
    const aero::VisualWorld& world = host.World();
    std::vector<aero::VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const aero::VisualNode* n = world.GetNode(q[i]);
        if (!n) { continue; }
        std::string tn;
        if (q[i] != rootId && TryReadNodeTypeName(host, n, tn) && tn == typeName) {
            return q[i];
        }
        for (aero::VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
    return aero::VisualNodeId{};
}

[[nodiscard]] aero::Rect NodeViewportBounds(const aero::VisualNode& node) noexcept {
    aero::Rect r = node.layout.layoutRect;
    if (node.layout.hasPerspective3D) {
        return node.WorldMatrix3D().TransformRectBounds(r);
    }
    if (!node.layout.worldTransform.IsIdentity()) {
        return node.layout.worldTransform.TransformRectBounds(r);
    }
    return r;
}

void TestInteractiveScoreboard(aero::rs_comp::CompilerSession&) {
    std::cout << "[Interactive] Scoreboard...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Scoreboard", nullptr, ci, hc, 1280u, 800u),
        "Scoreboard discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Scoreboard load failed: " + err);
    for (int f = 0; f < 25; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 24);
    }

    // 1. Scrollbar thumb position (starts at top, moves down on drag) and hover brightness (#FF3E4146 -> #FF5D6064)
    aero::VisualNodeId trackId = host.FindVisualByOwnerName("PART_Track");
    aero::VisualNodeId thumbId = host.FindVisualByOwnerName("Thumb");
    Require(trackId.IsValid() && thumbId.IsValid(), "Scoreboard PART_Track / Thumb missing");
    const aero::VisualNode* trackNode = host.World().GetNode(trackId);
    const aero::VisualNode* thumbNode = host.World().GetNode(thumbId);
    Require(trackNode && thumbNode, "Scoreboard track/thumb nodes null");
    Require(thumbNode->layout.visibility == aero::Visibility::Visible, "Scoreboard Thumb should be visible");

    const aero::Rect trackVp = NodeViewportBounds(*trackNode);
    const aero::Rect thumbVp0 = NodeViewportBounds(*thumbNode);
    Require(std::abs(thumbVp0.y - trackVp.y) < 4.0f,
        "Scoreboard vertical Thumb should start at top of PART_Track (trackY=" +
        std::to_string(trackVp.y) + ", thumbY=" + std::to_string(thumbVp0.y) + ")");

    const aero::VisualNodeId thumbBorderId = FindDescendantByTypeName(host, thumbId, "Border");
    Require(thumbBorderId.IsValid(), "Scoreboard Thumb template Border missing");
    const aero::VisualNode* thumbBorderNode = host.World().GetNode(thumbBorderId);
    Require(thumbBorderNode && thumbBorderNode->box.bgRgba == 0xFF46413Eu,
        "Scoreboard Thumb initial color should be #FF3E4146 (0xFF46413E), got 0x" +
        std::to_string(thumbBorderNode ? thumbBorderNode->box.bgRgba : 0u));

    const float thumbCx = thumbVp0.x + thumbVp0.width * 0.5f;
    const float thumbCy = thumbVp0.y + thumbVp0.height * 0.5f;
    host.InjectPointerMove(thumbCx, thumbCy);
    host.StepFrame(1.0f / 60.0f, true);
    thumbBorderNode = host.World().GetNode(thumbBorderId);
    Require(thumbBorderNode && thumbBorderNode->box.bgRgba == 0xFF64605Du,
        "Scoreboard Thumb hover color should be #FF5D6064 (0xFF64605D), got 0x" +
        std::to_string(thumbBorderNode ? thumbBorderNode->box.bgRgba : 0u));

    // Drag thumb downward and verify normal -> hover -> pressed/dragging -> hover -> normal states
    host.InjectPointerButton(thumbCx, thumbCy, aero::PointerButton::Left, true);
    host.StepFrame(1.0f / 60.0f, true);
    thumbBorderNode = host.World().GetNode(thumbBorderId);
    Require(thumbBorderNode && thumbBorderNode->box.bgRgba == 0xFF756F6Au,
        "Scoreboard Thumb dragging color should be #FF6A6F75 (0xFF756F6A)");

    host.InjectPointerMove(thumbCx, thumbCy + 30.0f);
    host.StepFrame(1.0f / 60.0f, true);
    host.InjectPointerButton(thumbCx, thumbCy + 30.0f, aero::PointerButton::Left, false);
    host.StepFrame(1.0f / 60.0f, true);
    thumbBorderNode = host.World().GetNode(thumbBorderId);
    Require(thumbBorderNode && thumbBorderNode->box.bgRgba == 0xFF64605Du,
        "Scoreboard Thumb should return to hover color #FF5D6064 (0xFF64605D) after drag release while hovered");

    thumbNode = host.World().GetNode(thumbId);
    const aero::Rect thumbVpAfterDrag = thumbNode ? NodeViewportBounds(*thumbNode) : aero::Rect{};
    Require(thumbVpAfterDrag.y > thumbVp0.y + 4.0f,
        "Scoreboard dragging Thumb down should move Thumb downward (before=" +
        std::to_string(thumbVp0.y) + ", after=" + std::to_string(thumbVpAfterDrag.y) + ")");

    // Reset hover & scroll back to top
    host.InjectPointerMove(5.0f, 5.0f);
    host.StepFrame(1.0f / 60.0f, true);
    thumbBorderNode = host.World().GetNode(thumbBorderId);
    {
        bool thOver = false, thDrag = false;
        (void)ReadBoolMember(host, thumbNode->scriptOwner, "IsMouseOver", thOver);
        (void)ReadBoolMember(host, thumbNode->scriptOwner, "IsDragging", thDrag);
        Require(thumbBorderNode && thumbBorderNode->box.bgRgba == 0xFF46413Eu,
            "Scoreboard Thumb color should revert to #FF3E4146 when mouse leaves (got 0x" +
            ([&]() {
                std::ostringstream hx;
                hx << std::hex << std::uppercase << (thumbBorderNode ? thumbBorderNode->box.bgRgba : 0u);
                return hx.str();
            })() +
            ", IsMouseOver=" + std::to_string(thOver) + ", IsDragging=" + std::to_string(thDrag) + ")");
    }
    float psx = 0.0f, psy = 0.0f;
    aero::VisualNodeId psId{};
    Require(RequireNodeCenter(host, "PlayersScroll", psx, psy, &psId), "Scoreboard PlayersScroll missing");
    const aero::VisualNode* psScrollNode = host.World().GetNode(psId);
    Require(psScrollNode && psScrollNode->scriptOwner.valid(), "Scoreboard PlayersScroll scriptOwner missing");
    host.InjectMouseWheel(psx, psy, 0.0f, 5.0f);
    host.StepFrame(1.0f / 60.0f, true);
    float psOffTop = -1.0f;
    Require(ReadFloatMember(host, psScrollNode->scriptOwner, "VerticalOffset", psOffTop) && std::fabs(psOffTop) < 0.5f,
        "Scoreboard wheel up (+5 notches) should return PlayersScroll to top (got " + std::to_string(psOffTop) + ")");
    // Scroll down with wheel (-3 notches) while in Overall before switching to Alliance (shrink clamp check)
    host.InjectMouseWheel(psx, psy, 0.0f, -3.0f);
    host.StepFrame(1.0f / 60.0f, true);
    float psOffScrolled = 0.0f;
    Require(ReadFloatMember(host, psScrollNode->scriptOwner, "VerticalOffset", psOffScrolled) && psOffScrolled > 20.0f,
        "Scoreboard wheel down (-3 notches) in Overall should scroll down (got " + std::to_string(psOffScrolled) + ")");

    // 2. VisibleTeam ComboBox hover, click -> PART_Popup opens inside viewport -> click Alliance filters list
    float cbCx = 0.0f, cbCy = 0.0f;
    aero::VisualNodeId comboId{};
    Require(RequireNodeCenter(host, "VisibleTeam", cbCx, cbCy, &comboId), "Scoreboard VisibleTeam missing");
    aero::VisualNodeId dropBtnId = host.FindVisualByOwnerName("DropDownButton");
    Require(dropBtnId.IsValid(), "Scoreboard DropDownButton missing");
    aero::VisualNodeId dropBorderId = FindDescendantByTypeName(host, dropBtnId, "Border");
    Require(dropBorderId.IsValid(), "Scoreboard DropDownButton Border missing");
    const aero::VisualNode* dropBorderNode = host.World().GetNode(dropBorderId);
    Require(dropBorderNode && dropBorderNode->box.bgRgba == 0xFF46413Eu,
        "Scoreboard ComboBox initial DropDownButton color should be #FF3E4146");

    host.InjectPointerMove(cbCx, cbCy);
    host.StepFrame(1.0f / 60.0f, true);
    dropBorderNode = host.World().GetNode(dropBorderId);
    Require(dropBorderNode && dropBorderNode->box.bgRgba == 0xFF64605Du,
        "Scoreboard ComboBox DropDownButton should highlight on hover (#FF5D6064)");

    // Click ComboBox to open PART_Popup
    host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, true);
    host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, false);
    for (int f = 0; f < 3; ++f) {
        host.StepFrame(1.0f / 60.0f, true);
    }

    aero::VisualNodeId popupId = host.FindVisualByOwnerName("PART_Popup");
    aero::VisualNodeId popupBorderId = host.FindVisualByOwnerName("DropDownBorder");
    Require(popupId.IsValid() && popupBorderId.IsValid(), "Scoreboard PART_Popup / DropDownBorder missing");
    const aero::VisualNode* popupNode = host.World().GetNode(popupId);
    const aero::VisualNode* popupBorderNode = host.World().GetNode(popupBorderId);
    Require(popupNode && popupNode->layout.visibility == aero::Visibility::Visible,
        "Scoreboard PART_Popup should be visible after clicking VisibleTeam");
    Require(popupBorderNode && popupBorderNode->layout.visibility == aero::Visibility::Visible,
        "Scoreboard DropDownBorder should be visible");

    const aero::Rect popVp = NodeViewportBounds(*popupBorderNode);
    Require(popVp.width > 80.0f && popVp.height > 40.0f &&
            popVp.x >= 0.0f && popVp.y >= 0.0f &&
            popVp.x + popVp.width <= 1280.5f && popVp.y + popVp.height <= 800.5f,
        "Scoreboard PART_Popup should fit inside 1280x800 viewport (got " +
        std::to_string(popVp.x) + "," + std::to_string(popVp.y) + " " +
        std::to_string(popVp.width) + "x" + std::to_string(popVp.height) + ")");

    // Locate ComboBoxItem nodes (Overall=0, Alliance=1, Horde=2)
    std::vector<aero::VisualNodeId> cbItems;
    {
        std::vector<aero::VisualNodeId> q{popupId};
        for (std::size_t i = 0u; i < q.size(); ++i) {
            const aero::VisualNode* n = host.World().GetNode(q[i]);
            if (!n) { continue; }
            std::string tn;
            if (TryReadNodeTypeName(host, n, tn) && tn == "ComboBoxItem") {
                cbItems.push_back(q[i]);
            }
            for (aero::VisualNodeId cid : n->children) {
                q.push_back(cid);
            }
        }
    }
    Require(cbItems.size() == 3u, "Scoreboard VisibleTeam should realize 3 ComboBoxItem entries, got " +
        std::to_string(cbItems.size()));

    if (cbItems.size() == 3u) {
        const aero::VisualNodeId rootId = host.FindVisualByOwnerName("Root");
        const aero::VisualNodeId listMaskNodeId = host.FindVisualByOwnerName("ListMask");
        Require(rootId.IsValid() && listMaskNodeId.IsValid(), "Scoreboard Root / ListMask missing");

        const aero::Rect allianceVp = NodeViewportBounds(*host.World().GetNode(cbItems[1]));
        const float ax = allianceVp.x + allianceVp.width * 0.5f;
        const float ay = allianceVp.y + allianceVp.height * 0.5f;
        host.InjectClick(ax, ay);
        host.StepFrame(1.0f / 60.0f, true);

        const aero::VisualNode* rootMid = host.World().GetNode(rootId);
        const aero::VisualNode* lmMid = host.World().GetNode(listMaskNodeId);
        Require(rootMid && !rootMid->layout.isEnabled,
            "Scoreboard Root.IsEnabled should become false during VisibleTeamChanged animation");
        Require(lmMid && lmMid->OpacityMask().enabled,
            "Scoreboard ListMask.opacityMask.enabled should be true during VisibleTeamChanged");
        std::vector<std::uint8_t> teamAnimF1;
        Require(host.RenderHeadlessRgba(1280u, 800u, teamAnimF1), "Scoreboard team switch frame 1 render failed");

        for (int f = 0; f < 8; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 7);
        }
        std::vector<std::uint8_t> teamAnimF9;
        Require(host.RenderHeadlessRgba(1280u, 800u, teamAnimF9), "Scoreboard team switch frame 9 render failed");
        Require(CountChangedPixels(teamAnimF1, teamAnimF9) > 150u,
            "Scoreboard ListMask OpacityMask should reveal rows progressively over VisibleTeamChanged (diff=" +
            std::to_string(CountChangedPixels(teamAnimF1, teamAnimF9)) + ")");

        for (int f = 0; f < 16; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 15);
        }
        rootMid = host.World().GetNode(rootId);
        Require(rootMid && rootMid->layout.isEnabled,
            "Scoreboard Root.IsEnabled should re-enable after VisibleTeamChanged Storyboard.Completed");

        float psOffAlliance = -1.0f;
        Require(ReadFloatMember(host, psScrollNode->scriptOwner, "VerticalOffset", psOffAlliance) &&
                std::fabs(psOffAlliance) < 0.5f,
            "Scoreboard switching from scrolled Overall to short Alliance must clamp VerticalOffset to 0 (got " +
            std::to_string(psOffAlliance) + ")");

        // In Alliance (5 rows, no scrollbar), wheel down must NOT scroll into empty space
        host.InjectMouseWheel(psx, psy, 0.0f, -3.0f);
        host.StepFrame(1.0f / 60.0f, true);
        float psOffNoBarWheel = -1.0f;
        Require(ReadFloatMember(host, psScrollNode->scriptOwner, "VerticalOffset", psOffNoBarWheel) &&
                std::fabs(psOffNoBarWheel) < 0.5f,
            "Scoreboard Alliance with no scrollbar must ignore wheel down (got " +
            std::to_string(psOffNoBarWheel) + ")");

        popupNode = host.World().GetNode(popupId);
        Require(popupNode && popupNode->layout.visibility == aero::Visibility::Collapsed,
            "Scoreboard PART_Popup should close after selecting ComboBoxItem");
        dropBorderNode = host.World().GetNode(dropBorderId);
        Require(dropBorderNode && dropBorderNode->box.bgRgba == 0xFF46413Eu,
            "Scoreboard DropDownButton should revert to normal color #FF3E4146 (0xFF46413E) when popup closes off-hover");

        std::vector<aero::VisualNodeId> rows;
        CollectVisualNodesByOwnerName(host, "PlayerRow", rows);
        int visibleRows = 0, collapsedRows = 0;
        for (aero::VisualNodeId rid : rows) {
            const aero::VisualNode* rn = host.World().GetNode(rid);
            if (!rn) { continue; }
            if (rn->layout.visibility == aero::Visibility::Visible) { ++visibleRows; }
            else if (rn->layout.visibility == aero::Visibility::Collapsed) { ++collapsedRows; }
        }
        Require(visibleRows == 5 && collapsedRows == 5,
            "Selecting Alliance should show 5 Alliance rows and collapse 5 Horde rows (visible=" +
            std::to_string(visibleRows) + ", collapsed=" + std::to_string(collapsedRows) + ")");

        // Switch back to Overall and wait full VisibleTeamChanged duration so Root.IsEnabled restores
        host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, true);
        host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, false);
        for (int f = 0; f < 3; ++f) {
            host.StepFrame(1.0f / 60.0f, true);
        }
        const aero::Rect overallVp = NodeViewportBounds(*host.World().GetNode(cbItems[0]));
        host.InjectClick(overallVp.x + overallVp.width * 0.5f, overallVp.y + overallVp.height * 0.5f);
        for (int f = 0; f < 25; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 24);
        }
        visibleRows = 0;
        for (aero::VisualNodeId rid : rows) {
            const aero::VisualNode* rn = host.World().GetNode(rid);
            if (rn && rn->layout.visibility == aero::Visibility::Visible) { ++visibleRows; }
        }
        Require(visibleRows == 10, "Selecting Overall should restore all 10 PlayerRows");
    }

    // 3. PlayerRow hover animations (OverOn / OverOff)
    host.InjectPointerMove(5.0f, 5.0f);
    host.StepFrame(1.0f / 60.0f, true);

    std::vector<aero::VisualNodeId> rows, overs, icons, statsList;
    CollectVisualNodesByOwnerName(host, "PlayerRow", rows);
    CollectVisualNodesByOwnerName(host, "Over", overs);
    CollectVisualNodesByOwnerName(host, "ClassIcon", icons);
    CollectVisualNodesByOwnerName(host, "PlayerStats", statsList);
    Require(rows.size() == 10u && overs.size() == 10u && icons.size() == 10u && statsList.size() == 10u,
        "Scoreboard should have 10 PlayerRow/Over/ClassIcon/PlayerStats nodes");

    const aero::VisualNode* over0 = host.World().GetNode(overs[0]);
    const aero::VisualNode* icon0 = host.World().GetNode(icons[0]);
    const aero::VisualNode* stats0 = host.World().GetNode(statsList[0]);
    Require(over0 && icon0 && stats0, "PlayerRow[0] child nodes null");
    Require(over0->box.bgOpacity < 0.05f && icon0->layout.opacity == 1.0f && icon0->Effect().opacity == 0.0f,
        "PlayerRow[0] rest state should keep Over bgOpacity=0, ClassIcon opacity=1, glow effect opacity=0");

    const aero::Rect row0Vp = NodeViewportBounds(*host.World().GetNode(rows[0]));
    host.InjectPointerMove(row0Vp.x + row0Vp.width * 0.5f, row0Vp.y + row0Vp.height * 0.5f);
    host.StepFrame(0.06f, true);

    over0 = host.World().GetNode(overs[0]);
    icon0 = host.World().GetNode(icons[0]);
    stats0 = host.World().GetNode(statsList[0]);
    Require(over0->box.bgOpacity > 0.4f,
        "PlayerRow[0] hover OverOn should animate Over.bgOpacity -> 0.5 (got " +
        std::to_string(over0->box.bgOpacity) + ")");
    Require(icon0->transform2D.sx > 1.05f && icon0->transform2D.sy > 1.05f,
        "PlayerRow[0] hover OverOn should scale ClassIcon -> 1.1 (got sx=" +
        std::to_string(icon0->transform2D.sx) + ")");
    Require(icon0->Effect().opacity > 0.3f && icon0->layout.opacity == 1.0f,
        "PlayerRow[0] hover OverOn should animate ClassIcon glow EffectOpacity -> 0.4 without hiding ClassIcon (eff=" +
        std::to_string(icon0->Effect().opacity) + ", op=" + std::to_string(icon0->layout.opacity) + ")");
    Require(stats0->transform2D.tx > 1.5f,
        "PlayerRow[0] hover OverOn should nudge PlayerStats.X -> 2 (got " +
        std::to_string(stats0->transform2D.tx) + ")");

    // Move away -> OverOff
    host.InjectPointerMove(5.0f, 5.0f);
    host.StepFrame(0.12f, true);

    over0 = host.World().GetNode(overs[0]);
    icon0 = host.World().GetNode(icons[0]);
    stats0 = host.World().GetNode(statsList[0]);
    Require(over0->box.bgOpacity < 0.05f,
        "PlayerRow[0] leave OverOff should animate Over.bgOpacity -> 0 (got " +
        std::to_string(over0->box.bgOpacity) + ")");
    Require(icon0->transform2D.sx < 1.02f && icon0->Effect().opacity < 0.05f && icon0->layout.opacity == 1.0f,
        "PlayerRow[0] leave OverOff should restore ClassIcon scale/glow without hiding it");
    Require(stats0->transform2D.tx < 0.2f,
        "PlayerRow[0] leave OverOff should restore PlayerStats.X -> 0");

    // 4. Multi-resolution resize + team switch regression check (ListMask.Opacity & visible player list pixels)
    const aero::VisualNodeId listMaskId = host.FindVisualByOwnerName("ListMask");
    const aero::VisualNodeId playersScrollId = host.FindVisualByOwnerName("PlayersScroll");
    Require(listMaskId.IsValid() && playersScrollId.IsValid(), "Scoreboard ListMask / PlayersScroll missing");

    auto countBrightListPixels = [&](const std::vector<std::uint8_t>& rgba,
                                     std::uint32_t w,
                                     std::uint32_t h,
                                     const aero::Rect& vp) -> int {
        const int x0 = std::max(0, static_cast<int>(std::floor(vp.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(vp.y)));
        const int x1 = std::min(static_cast<int>(w), static_cast<int>(std::ceil(vp.x + vp.width)));
        const int y1 = std::min(static_cast<int>(h), static_cast<int>(std::ceil(vp.y + vp.height)));
        int bright = 0;
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const std::size_t o = (static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)) * 4u;
                const std::uint8_t mx = std::max(rgba[o], std::max(rgba[o + 1u], rgba[o + 2u]));
                if (rgba[o + 3u] > 200u && mx > 150u) {
                    ++bright;
                }
            }
        }
        return bright;
    };

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> kResizeSteps{{
        {800u, 480u},
        {640u, 400u},
        {1920u, 1200u},
        {960u, 540u},
        {1280u, 800u},
    }};
    for (const auto& [rw, rh] : kResizeSteps) {
        host.InjectResize(rw, rh);
        host.StepFrame(1.0f / 60.0f, true);
        std::vector<std::uint8_t> resizedRgba;
        Require(host.RenderHeadlessRgba(rw, rh, resizedRgba),
            "Scoreboard RenderHeadlessRgba failed after resize to " + std::to_string(rw) + "x" + std::to_string(rh));

        const aero::VisualNode* lmNode = host.World().GetNode(listMaskId);
        const aero::VisualNode* psNode = host.World().GetNode(playersScrollId);
        Require(lmNode && lmNode->layout.opacity > 0.99f,
            "Scoreboard ListMask.Opacity should remain 1.0 after resize (got " +
            std::to_string(lmNode ? lmNode->layout.opacity : -1.0f) + ")");
        const aero::Rect psVp = psNode ? NodeViewportBounds(*psNode) : aero::Rect{};
        Require(psVp.width > 100.0f && psVp.height > 60.0f,
            "Scoreboard PlayersScroll viewport bounds collapsed after resize " +
            std::to_string(rw) + "x" + std::to_string(rh));
        const int brightPx = countBrightListPixels(resizedRgba, rw, rh, psVp);
        Require(brightPx >= 50,
            "Scoreboard player list content disappeared after resize " +
            std::to_string(rw) + "x" + std::to_string(rh) + " (brightPx=" + std::to_string(brightPx) + ")");
    }

    // Shrink to 640x400 and switch VisibleTeam to verify VisibleTeamChanged does not fade out ListMask
    host.InjectResize(640u, 400u);
    host.StepFrame(1.0f / 60.0f, true);
    if (RequireNodeCenter(host, "VisibleTeam", cbCx, cbCy)) {
        host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, true);
        host.InjectPointerButton(cbCx, cbCy, aero::PointerButton::Left, false);
        for (int f = 0; f < 3; ++f) {
            host.StepFrame(1.0f / 60.0f, true);
        }
        if (cbItems.size() == 3u) {
            const aero::Rect allianceVp = NodeViewportBounds(*host.World().GetNode(cbItems[1]));
            host.InjectClick(allianceVp.x + allianceVp.width * 0.5f, allianceVp.y + allianceVp.height * 0.5f);
            for (int f = 0; f < 30; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 29);
            }
            std::vector<std::uint8_t> smallTeamRgba;
            Require(host.RenderHeadlessRgba(640u, 400u, smallTeamRgba), "Scoreboard 640x400 team render failed");
            const aero::VisualNode* lmNode = host.World().GetNode(listMaskId);
            const aero::VisualNode* psNode = host.World().GetNode(playersScrollId);
            Require(lmNode && lmNode->layout.opacity > 0.99f,
                "Scoreboard ListMask.Opacity clobbered during VisibleTeamChanged (got " +
                std::to_string(lmNode ? lmNode->layout.opacity : -1.0f) + ")");
            const int brightPx = psNode ? countBrightListPixels(smallTeamRgba, 640u, 400u, NodeViewportBounds(*psNode)) : 0;
            Require(brightPx >= 50,
                "Scoreboard player rows missing at 640x400 after team switch (brightPx=" + std::to_string(brightPx) + ")");
        }
    }
}

void CollectDescendantsByTypeName(
    aero::AeroAppHost& host,
    aero::VisualNodeId rootId,
    std::string_view typeName,
    std::vector<aero::VisualNodeId>& outIds) {
    outIds.clear();
    const aero::VisualWorld& world = host.World();
    std::vector<aero::VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const aero::VisualNode* n = world.GetNode(q[i]);
        if (!n) { continue; }
        std::string tn;
        if (q[i] != rootId && TryReadNodeTypeName(host, n, tn) && tn == typeName) {
            outIds.push_back(q[i]);
        }
        for (aero::VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
}

void CollectSubtreeText(
    const aero::VisualWorld& world,
    aero::VisualNodeId rootId,
    std::string& outJoined) {
    outJoined.clear();
    std::vector<aero::VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const aero::VisualNode* n = world.GetNode(q[i]);
        if (!n) { continue; }
        if (n->kind == aero::VisualKind::Text && !n->Text().text.empty()) {
            if (!outJoined.empty()) { outJoined.push_back(' '); }
            outJoined.append(n->Text().text);
        }
        for (aero::VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
}

void TestInteractiveDataBinding(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] DataBinding...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "DataBinding", nullptr, ci, hc, 960u, 540u),
        "DataBinding discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "DataBinding load failed: " + err);

    for (int f = 0; f < 25; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 24);
    }

    std::vector<aero::VisualNodeId> items;
    CollectDescendantsByTypeName(host, host.RootVisualNode(), "ListBoxItem", items);
    Require(items.size() >= 10u,
        "DataBinding should realize 10 solar-system ListBoxItems (got " + std::to_string(items.size()) + ")");

    std::vector<aero::VisualNodeId> tooltips;
    CollectVisualNodesByOwnerName(host, "Tooltip", tooltips);
    Require(tooltips.size() >= 10u, "DataBinding should create 10 Tooltip Popups");

    auto findVisibleTooltip = [&]() -> aero::VisualNodeId {
        for (aero::VisualNodeId tid : tooltips) {
            const aero::VisualNode* n = host.World().GetNode(tid);
            if (n && n->layout.visibility == aero::Visibility::Visible && !n->children.empty()) {
                return tid;
            }
        }
        return aero::VisualNodeId{};
    };

    auto tooltipCardBounds = [&](aero::VisualNodeId tipId) -> aero::Rect {
        const aero::VisualNode* tip = host.World().GetNode(tipId);
        if (!tip) { return aero::Rect{}; }
        const aero::VisualNodeId bdId = FindDescendantByTypeName(host, tipId, "Border");
        const aero::VisualNode* bd = bdId.IsValid() ? host.World().GetNode(bdId) : tip;
        return bd ? NodeViewportBounds(*bd) : aero::Rect{};
    };

    // 1. Select Sun (item 0): diameter must format as "1380000" (no scientific notation "1.38e+06")
    if (!items.empty()) {
        const aero::Rect sunVp = NodeViewportBounds(*host.World().GetNode(items[0]));
        host.InjectClick(sunVp.x + sunVp.width * 0.5f, sunVp.y + sunVp.height * 0.5f);
        for (int f = 0; f < 25; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 24);
        }
        const aero::VisualNodeId sunTip = findVisibleTooltip();
        Require(sunTip.IsValid(), "DataBinding Sun Tooltip should be visible after click");
        if (sunTip.IsValid()) {
            std::string tipText;
            CollectSubtreeText(host.World(), sunTip, tipText);
            Require(tipText.find("1380000") != std::string::npos,
                "DataBinding Sun Tooltip should format Diameter as 1380000 (got text: " + tipText + ")");
            Require(tipText.find("1.38e") == std::string::npos && tipText.find("1.38E") == std::string::npos,
                "DataBinding Sun Tooltip must not use scientific notation (got text: " + tipText + ")");
        }
        MaybeDumpScreenshot(host, "databinding_sun.ppm", 960u, 540u);
    }

    // 2. Select Pluto (last item) and check automatic popup placement + resize recalculation
    if (items.size() >= 10u) {
        const aero::Rect plutoVp = NodeViewportBounds(*host.World().GetNode(items[9]));
        host.InjectClick(plutoVp.x + plutoVp.width * 0.5f, plutoVp.y + plutoVp.height * 0.5f);
        for (int f = 0; f < 25; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 24);
        }
        const aero::VisualNodeId plutoTip = findVisibleTooltip();
        Require(plutoTip.IsValid(), "DataBinding Pluto Tooltip should be visible after click");
        if (plutoTip.IsValid()) {
            const aero::Rect vp0 = tooltipCardBounds(plutoTip);
            Require(vp0.width > 100.0f && vp0.x >= -0.5f && vp0.y >= -0.5f &&
                    vp0.x + vp0.width <= 961.0f && vp0.y + vp0.height <= 541.0f,
                "DataBinding Pluto Tooltip should stay on-screen at 960x540 (got " +
                std::to_string(vp0.x) + "," + std::to_string(vp0.y) + " " +
                std::to_string(vp0.width) + "x" + std::to_string(vp0.height) + ")");

            host.InjectResize(540u, 530u);
            for (int f = 0; f < 4; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 3);
            }
            const aero::Rect vp540 = tooltipCardBounds(plutoTip);
            Require(vp540.width > 50.0f && vp540.x >= -0.5f && vp540.y >= -0.5f &&
                    vp540.x + vp540.width <= 541.0f && vp540.y + vp540.height <= 531.0f,
                "DataBinding Pluto Tooltip should recalculate inside 540x530 without clipping (got " +
                std::to_string(vp540.x) + "," + std::to_string(vp540.y) + " " +
                std::to_string(vp540.width) + "x" + std::to_string(vp540.height) + ")");
            MaybeDumpScreenshot(host, "databinding_pluto_540.ppm", 540u, 530u);

            host.InjectResize(960u, 540u);
            for (int f = 0; f < 4; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 3);
            }
            const aero::Rect vp960 = tooltipCardBounds(plutoTip);
            Require(vp960.width > 100.0f && vp960.x >= -0.5f && vp960.y >= -0.5f &&
                    vp960.x + vp960.width <= 961.0f && vp960.y + vp960.height <= 541.0f,
                "DataBinding Pluto Tooltip should recalculate cleanly after resize back to 960x540");
            MaybeDumpScreenshot(host, "databinding_pluto_960.ppm", 960u, 540u);
        }
    }
}

void TestInteractiveIntegration(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Integration...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Integration", nullptr, ci, hc, 960u, 540u),
        "Integration discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Integration load failed: " + err);

    for (int f = 0; f < 15; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 14);
    }

    std::vector<aero::VisualNodeId> tabs;
    CollectDescendantsByTypeName(host, host.RootVisualNode(), "TabItem", tabs);
    Require(tabs.size() == 3u, "Integration should have 3 TabItems (got " + std::to_string(tabs.size()) + ")");
    MaybeDumpScreenshot(host, "integration_game.ppm", 960u, 540u);

    if (tabs.size() >= 2u) {
        const aero::Rect videoTabVp = NodeViewportBounds(*host.World().GetNode(tabs[1]));
        host.InjectClick(videoTabVp.x + videoTabVp.width * 0.5f, videoTabVp.y + videoTabVp.height * 0.5f);
        for (int f = 0; f < 10; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 9);
        }
        MaybeDumpScreenshot(host, "integration_video.ppm", 960u, 540u);

        // Open Screen Resolution ComboBox (first ComboBox in active Video tab)
        std::vector<aero::VisualNodeId> combos;
        CollectDescendantsByTypeName(host, host.RootVisualNode(), "ComboBox", combos);
        aero::VisualNodeId resComboId;
        for (aero::VisualNodeId cid : combos) {
            const aero::VisualNode* n = host.World().GetNode(cid);
            if (n && n->layout.visibility == aero::Visibility::Visible && n->layout.layoutRect.width > 10.0f) {
                resComboId = cid;
                break;
            }
        }
        Require(resComboId.IsValid(), "Integration Video ComboBox not visible");
        if (resComboId.IsValid()) {
            const aero::Rect cbVp = NodeViewportBounds(*host.World().GetNode(resComboId));
            const aero::VisualNodeId popId = FindDescendantByOwnerName(host, resComboId, "PART_Popup");
            host.InjectClick(cbVp.x + cbVp.width * 0.5f, cbVp.y + cbVp.height * 0.5f);
            for (int f = 0; f < 4; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 3);
            }
            const aero::VisualNode* popNode = popId.IsValid() ? host.World().GetNode(popId) : nullptr;
            Require(popNode && popNode->layout.visibility == aero::Visibility::Visible,
                "Integration ComboBox PART_Popup should be visible after click");
            if (popId.IsValid()) {
                std::vector<aero::VisualNodeId> cbItems;
                CollectDescendantsByTypeName(host, popId, "ComboBoxItem", cbItems);
                Require(!cbItems.empty(), "Integration ComboBox should realize ComboBoxItems");
                if (!cbItems.empty()) {
                    const aero::Rect itemVp = NodeViewportBounds(*host.World().GetNode(cbItems[0]));
                    Require(itemVp.height >= 26.0f,
                        "Integration ComboBoxItem height should reflect 10,8 padding (got " +
                        std::to_string(itemVp.height) + ")");
                }
            }
            MaybeDumpScreenshot(host, "integration_combobox_open.ppm", 960u, 540u);
            // Close ComboBox dropdown by clicking header again
            host.InjectClick(cbVp.x + cbVp.width * 0.5f, cbVp.y + cbVp.height * 0.5f);
            for (int f = 0; f < 3; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 2);
            }
        }

        // Hover vertical ScrollBar on Video tab -> expands & shows dark track/chevrons
        std::vector<aero::VisualNodeId> bars;
        CollectDescendantsByTypeName(host, host.RootVisualNode(), "ScrollBar", bars);
        aero::VisualNodeId vBarId;
        for (aero::VisualNodeId bid : bars) {
            const aero::VisualNode* bn = host.World().GetNode(bid);
            const aero::Rect bvp = bn ? NodeViewportBounds(*bn) : aero::Rect{};
            if (bn && bn->layout.visibility == aero::Visibility::Visible && bvp.height > bvp.width && bvp.width > 2.0f) {
                vBarId = bid;
                break;
            }
        }
        Require(vBarId.IsValid(), "Integration visible vertical ScrollBar not found");
        if (vBarId.IsValid()) {
            std::vector<std::uint8_t> preHover;
            Require(host.RenderHeadlessRgba(960u, 540u, preHover), "Integration pre-scrollbar-hover render failed");
            const aero::Rect sbVp = NodeViewportBounds(*host.World().GetNode(vBarId));
            host.InjectPointerMove(sbVp.x + sbVp.width * 0.5f, sbVp.y + sbVp.height * 0.5f);
            for (int f = 0; f < 15; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 14);
            }
            std::vector<std::uint8_t> postHover;
            Require(host.RenderHeadlessRgba(960u, 540u, postHover), "Integration post-scrollbar-hover render failed");
            Require(CountChangedPixels(preHover, postHover) > 50u,
                "Integration ScrollBar hover should animate dark track and chevrons");
            MaybeDumpScreenshot(host, "integration_scrollbar_hover.ppm", 960u, 540u);
        }
    }

    if (tabs.size() >= 3u) {
        const aero::Rect audioTabVp = NodeViewportBounds(*host.World().GetNode(tabs[2]));
        host.InjectClick(audioTabVp.x + audioTabVp.width * 0.5f, audioTabVp.y + audioTabVp.height * 0.5f);
        for (int f = 0; f < 10; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 9);
        }
        MaybeDumpScreenshot(host, "integration_audio.ppm", 960u, 540u);
    }
}

void TestInteractiveMenu3D(aero::rs_comp::CompilerSession& session, std::uint32_t clearRgba) {
    std::cout << "[Interactive] Menu3D...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Menu3D", nullptr, ci, hc, 1280u, 720u),
        "Menu3D discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Menu3D load failed: " + err);

    for (int f = 0; f < 30; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 29);
    }

    // 1. MainMenu checks: AnimatedBackground parallax layers & pixels, PC-only MenuDesc, single active tooltip
    const std::array<const char*, 5> kParallaxLayers = {
        "SkyAndSun", "BackClouds", "Mountains", "FrontClouds", "FrontTrees"
    };
    const std::array<const char*, 5> kParallaxWidths = {
        "SkyAndSunWidth", "BackCloudsWidth", "MountainsWidth", "FrontCloudsWidth", "FrontTreesWidth"
    };
    for (const char* layerName : kParallaxLayers) {
        const aero::VisualNodeId lid = host.FindVisualByOwnerName(layerName);
        const aero::VisualNode* ln = lid.IsValid() ? host.World().GetNode(lid) : nullptr;
        Require(ln && ln->layout.layoutRect.width > 100.0f && ln->layout.layoutRect.height > 15.0f,
            std::string("Menu3D parallax layer '") + layerName + "' should have non-zero layout bounds");
    }
    for (const char* widthName : kParallaxWidths) {
        const aero::VisualNodeId wid = host.FindVisualByOwnerName(widthName);
        const aero::VisualNode* wn = wid.IsValid() ? host.World().GetNode(wid) : nullptr;
        Require(wn && wn->layout.layoutRect.width > 100.0f && wn->layout.layoutRect.height > 15.0f,
            std::string("Menu3D parallax image '") + widthName + "' should have non-zero layout bounds");
    }
    const aero::VisualNodeId ftId = host.FindVisualByOwnerName("FrontTrees");
    const aero::VisualNode* ftNode0 = ftId.IsValid() ? host.World().GetNode(ftId) : nullptr;
    const float ftTxBefore = ftNode0 ? ftNode0->transform2D.tx : 0.0f;
    for (int f = 0; f < 30; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 29);
    }
    const aero::VisualNode* ftNode1 = ftId.IsValid() ? host.World().GetNode(ftId) : nullptr;
    const float ftTxAfter = ftNode1 ? ftNode1->transform2D.tx : 0.0f;
    Require(ftNode1 && std::fabs(ftTxAfter - ftTxBefore) > 0.01f,
        "Menu3D BackgroundAnim should animate FrontTrees parallax TranslateTransform.X (before=" +
        std::to_string(ftTxBefore) + ", after=" + std::to_string(ftTxAfter) + ")");

    std::vector<std::uint8_t> mainRgba;
    Require(host.RenderHeadlessRgba(1280u, 720u, mainRgba), "Menu3D MainMenu render failed");
    const ImageMetrics mainMetrics = ComputeExactImageMetrics(mainRgba, clearRgba);
    Require(mainMetrics.nonBlankPercent > 70.0,
        "Menu3D AnimatedBackground should cover canvas (nonBlankPercent=" +
        std::to_string(mainMetrics.nonBlankPercent) + "%)");

    auto checkVis = [&](std::string_view name, aero::Visibility expected) {
        const aero::VisualNodeId id = host.FindVisualByOwnerName(name);
        const aero::VisualNode* n = id.IsValid() ? host.World().GetNode(id) : nullptr;
        Require(n && n->layout.visibility == expected,
            "Menu3D '" + std::string(name) + "' visibility mismatch");
    };
    checkVis("PCAccept", aero::Visibility::Visible);
    checkVis("PCCancel", aero::Visibility::Visible);
    checkVis("XBOXAccept", aero::Visibility::Collapsed);
    checkVis("PS4Accept", aero::Visibility::Collapsed);
    checkVis("XBOXCancel", aero::Visibility::Collapsed);
    checkVis("PS4Cancel", aero::Visibility::Collapsed);

    const aero::VisualNodeId tipsId = host.FindVisualByOwnerName("ToolTips");
    if (tipsId.IsValid()) {
        const aero::VisualNode* tips = host.World().GetNode(tipsId);
        int activeTips = 0;
        if (tips) {
            for (aero::VisualNodeId cid : tips->children) {
                const aero::VisualNode* c = host.World().GetNode(cid);
                if (!c || c->layout.visibility != aero::Visibility::Visible) {
                    continue;
                }
                float effOpacity = c->layout.opacity;
                std::vector<aero::VisualNodeId> q{cid};
                for (std::size_t qi = 0u; qi < q.size(); ++qi) {
                    const aero::VisualNode* qn = host.World().GetNode(q[qi]);
                    if (!qn) { continue; }
                    if (qn->kind == aero::VisualKind::Text) {
                        effOpacity *= qn->Text().fgOpacity;
                        break;
                    }
                    for (aero::VisualNodeId sub : qn->children) {
                        q.push_back(sub);
                    }
                }
                if (effOpacity > 0.5f) {
                    ++activeTips;
                }
            }
        }
        Require(activeTips <= 1,
            "Menu3D ToolTips should display at most 1 active tooltip at a time (got " +
            std::to_string(activeTips) + ")");
    }
    MaybeDumpScreenshot(host, "menu3d_main.ppm", 1280u, 720u);

    // 2. Navigate to Start -> StartMenu (Casual/Normal/Expert badges + Back/Esc)
    float sx = 0.0f, sy = 0.0f;
    if (RequireNodeCenter(host, "Start", sx, sy)) {
        host.InjectClick(sx, sy);
        for (int f = 0; f < 65; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 64);
        }
        float cx = 0.0f, cy = 0.0f, nx = 0.0f, ny = 0.0f;
        Require(RequireNodeCenter(host, "Casual", cx, cy), "Menu3D StartMenu Casual button missing");
        host.InjectClick(cx, cy);
        for (int f = 0; f < 16; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 15);
        }
        const aero::VisualNodeId casualTreeId = host.FindVisualByOwnerName("CasualTree");
        const aero::VisualNode* casualTree = casualTreeId.IsValid() ? host.World().GetNode(casualTreeId) : nullptr;
        Require(casualTree && casualTree->layout.opacity > 0.5f,
            "Menu3D StartMenu CasualTree should activate after checking Casual");

        if (RequireNodeCenter(host, "Normal", nx, ny)) {
            host.InjectClick(nx, ny);
            for (int f = 0; f < 16; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 15);
            }
            const aero::VisualNodeId normalTreeId = host.FindVisualByOwnerName("NormalTree");
            const aero::VisualNode* normalTree = normalTreeId.IsValid() ? host.World().GetNode(normalTreeId) : nullptr;
            Require(normalTree && normalTree->layout.opacity > 0.5f,
                "Menu3D StartMenu NormalTree should activate after checking Normal");
        }
        MaybeDumpScreenshot(host, "menu3d_start.ppm", 1280u, 720u);

        float bx = 0.0f, by = 0.0f;
        if (RequireNodeCenter(host, "Back", bx, by)) {
            host.InjectClick(bx, by);
            for (int f = 0; f < 65; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 64);
            }
        }
        Require(host.FindVisualByOwnerName("Settings").IsValid(),
            "Menu3D Back from StartMenu should return to MainMenu");
    }

    // 3. Navigate to Settings -> SettingsMenu (OptionSelector < / > + keys, CheckBox, Esc back)
    float setX = 0.0f, setY = 0.0f;
    if (RequireNodeCenter(host, "Settings", setX, setY)) {
        host.InjectClick(setX, setY);
        for (int f = 0; f < 65; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 64);
        }
        const aero::VisualNodeId texId = host.FindVisualByOwnerName("TexDetailContent");
        const aero::VisualNodeId soundId = host.FindVisualByOwnerName("SoundVol");
        const aero::VisualNodeId aaId = host.FindVisualByOwnerName("AAlias");
        const aero::VisualNode* texNode = texId.IsValid() ? host.World().GetNode(texId) : nullptr;
        const aero::VisualNode* soundNode = soundId.IsValid() ? host.World().GetNode(soundId) : nullptr;
        Require(texNode && texNode->scriptOwner.valid(), "Menu3D Settings TexDetailContent missing");
        const aero::VisualNodeId soundListId = soundNode ? FindDescendantByTypeName(host, soundId, "ListBox") : aero::VisualNodeId{};
        const aero::VisualNode* soundList = soundListId.IsValid() ? host.World().GetNode(soundListId) : nullptr;

        if (texNode && texNode->scriptOwner.valid()) {
            auto dumpMenu3DState = [&](const char* tag) {
                std::int64_t si = -999, rsi = -999, rpend = -999;
                const bool okSi = ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", si);
                const bool okRsi = ReadIntMember(host, texNode->scriptOwner, "_selectedIndex", rsi);
                const bool okPend = ReadIntMember(host, texNode->scriptOwner, "_pendingSelectedIndex", rpend);
                const aero::VisualNodeId auxId = FindDescendantByOwnerName(host, texId, "SelectedIndexAux");
                const aero::VisualNode* auxN = auxId.IsValid() ? host.World().GetNode(auxId) : nullptr;
                float auxVal = -999.0f, auxMin = -999.0f, auxMax = -999.0f;
                bool okAv = auxN && ReadFloatMember(host, auxN->scriptOwner, "Value", auxVal);
                bool okAmin = auxN && ReadFloatMember(host, auxN->scriptOwner, "Minimum", auxMin);
                bool okAmax = auxN && ReadFloatMember(host, auxN->scriptOwner, "Maximum", auxMax);
                const aero::VisualNodeId nbId = FindDescendantByOwnerName(host, texId, "NextButton");
                const aero::VisualNode* nb = nbId.IsValid() ? host.World().GetNode(nbId) : nullptr;
                bool nbEn = false;
                if (nb) { (void)ReadBoolMember(host, nb->scriptOwner, "IsEnabled", nbEn); }
                std::fprintf(stderr, "[M3D_DIAG] %s: SelectedIndex(ok=%d val=%lld) _selectedIndex(ok=%d %lld) _pending(ok=%d %lld) aux(id=%u okV=%d val=%.2f min=%.2f max=%.2f) nextEn=%d\n",
                    tag, okSi?1:0, static_cast<long long>(si), okRsi?1:0, static_cast<long long>(rsi),
                    okPend?1:0, static_cast<long long>(rpend),
                    auxN ? auxN->id.index : 0u, okAv?1:0, auxVal, auxMin, auxMax, nbEn?1:0);
            };
            dumpMenu3DState("initial");
            std::int64_t idx0 = -1, snd0 = -1;
            Require(ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idx0) && idx0 == 1,
                "Menu3D TexDetailContent initial SelectedIndex should be 1 (got " + std::to_string(idx0) + ")");
            if (soundList && soundList->scriptOwner.valid()) {
                (void)ReadIntMember(host, soundList->scriptOwner, "SelectedIndex", snd0);
            }

            const aero::VisualNodeId nextBtnId = FindDescendantByOwnerName(host, texId, "NextButton");
            Require(nextBtnId.IsValid(), "Menu3D TexDetailContent NextButton missing");
            if (nextBtnId.IsValid()) {
                const aero::Rect nextVp = NodeViewportBounds(*host.World().GetNode(nextBtnId));
                host.InjectClick(nextVp.x + nextVp.width * 0.5f, nextVp.y + nextVp.height * 0.5f);
                for (int f = 0; f < 5; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 4);
                }
                dumpMenu3DState("after_next");
                std::int64_t idxAfterNext = -1, sndAfterNext = -1;
                const bool okNextIdx = ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idxAfterNext);
                Require(okNextIdx && idxAfterNext == 2,
                    "Menu3D NextButton should advance TexDetailContent SelectedIndex to 2 (got " +
                    std::to_string(idxAfterNext) + ")");
                if (soundList && soundList->scriptOwner.valid()) {
                    const bool okSnd = ReadIntMember(host, soundList->scriptOwner, "SelectedIndex", sndAfterNext);
                    Require(okSnd && sndAfterNext == snd0,
                        "Menu3D TexDetail NextButton must not mutate sibling SoundVol SelectedIndex");
                }
            }

            const aero::VisualNodeId prevBtnId = FindDescendantByOwnerName(host, texId, "PrevButton");
            if (prevBtnId.IsValid()) {
                const aero::Rect prevVp = NodeViewportBounds(*host.World().GetNode(prevBtnId));
                host.InjectClick(prevVp.x + prevVp.width * 0.5f, prevVp.y + prevVp.height * 0.5f);
                for (int f = 0; f < 5; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 4);
                }
                dumpMenu3DState("after_prev");
                std::int64_t idxAfterPrev = -1;
                const bool okPrevIdx = ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idxAfterPrev);
                Require(okPrevIdx && idxAfterPrev == 1,
                    "Menu3D PrevButton should return TexDetailContent SelectedIndex to 1 (got " +
                    std::to_string(idxAfterPrev) + ")");
            }

            // Keyboard Right on focused TexDetailContent -> advances SelectedIndex to 2
            const aero::Rect texVp = NodeViewportBounds(*texNode);
            host.InjectClick(texVp.x + texVp.width * 0.5f, texVp.y + texVp.height * 0.5f);
            host.InjectKey(aero::KeyCode::Right, true);
            host.InjectKey(aero::KeyCode::Right, false);
            for (int f = 0; f < 5; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 4);
            }
            dumpMenu3DState("after_key");
            std::int64_t idxAfterKey = -1;
            const bool okKeyIdx = ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idxAfterKey);
            Require(okKeyIdx && idxAfterKey == 2,
                "Menu3D Right key should advance TexDetailContent SelectedIndex to 2 (got " +
                std::to_string(idxAfterKey) + ")");
        }

        if (aaId.IsValid()) {
            const aero::VisualNodeId cbId = FindDescendantByTypeName(host, aaId, "CheckBox");
            const aero::VisualNode* cb = cbId.IsValid() ? host.World().GetNode(cbId) : nullptr;
            if (cb && cb->scriptOwner.valid()) {
                const aero::Rect cbVp = NodeViewportBounds(*cb);
                host.InjectClick(cbVp.x + cbVp.width * 0.5f, cbVp.y + cbVp.height * 0.5f);
                for (int f = 0; f < 18; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 17);
                }
                bool aaChecked = false;
                Require(ReadBoolMember(host, cb->scriptOwner, "IsChecked", aaChecked) && aaChecked,
                    "Menu3D Anti-Aliasing CheckBox should toggle IsChecked on click");
            }
        }
        MaybeDumpScreenshot(host, "menu3d_settings.ppm", 1280u, 720u);

        host.InjectKey(aero::KeyCode::Escape, true);
        host.InjectKey(aero::KeyCode::Escape, false);
        for (int f = 0; f < 65; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 64);
        }
        Require(host.FindVisualByOwnerName("Start").IsValid(),
            "Menu3D Escape from SettingsMenu should navigate back to MainMenu");
    }

    // 4. Code-Behind NavigateToSettings / NavigateToStart / NavigateToMain keep VM.ActiveMenu & UI synchronized
    aero::ObjectRef menuVm{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "VM", menuVm), "Menu3D VM missing");
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "NavigateToSettings"),
        "Menu3D NavigateToSettings code-behind failed");
    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }
    std::string activeAfterSettings;
    Require(ReadStringMember(host, menuVm, "ActiveMenu", activeAfterSettings) && activeAfterSettings == "Settings",
        "Menu3D NavigateToSettings should set VM.ActiveMenu = 'Settings'");
    Require(host.FindVisualByOwnerName("TexDetailContent").IsValid(),
        "Menu3D NavigateToSettings should show SettingsMenu (TexDetailContent)");

    Require(host.InvokeScriptMethod(host.RootScriptObject(), "NavigateToStart"),
        "Menu3D NavigateToStart code-behind failed");
    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }
    std::string activeAfterStart;
    Require(ReadStringMember(host, menuVm, "ActiveMenu", activeAfterStart) && activeAfterStart == "Start",
        "Menu3D NavigateToStart should set VM.ActiveMenu = 'Start'");
    Require(host.FindVisualByOwnerName("Casual").IsValid(),
        "Menu3D NavigateToStart should show StartMenu (Casual)");

    Require(host.InvokeScriptMethod(host.RootScriptObject(), "NavigateToMain"),
        "Menu3D NavigateToMain code-behind failed");
    for (int f = 0; f < 45; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 44);
    }
    std::string activeAfterMain;
    Require(ReadStringMember(host, menuVm, "ActiveMenu", activeAfterMain) && activeAfterMain == "Main",
        "Menu3D NavigateToMain should set VM.ActiveMenu = 'Main'");
    Require(host.FindVisualByOwnerName("Start").IsValid() && host.FindVisualByOwnerName("Settings").IsValid(),
        "Menu3D NavigateToMain should show MainMenu (Start/Settings)");
}

void TestInteractiveBackgroundBlur(aero::rs_comp::CompilerSession&) {
    std::cout << "[Interactive] BackgroundBlur...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "BackgroundBlur", nullptr, ci, hc, 960u, 540u),
        "BackgroundBlur discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "BackgroundBlur load failed: " + err);

    for (int f = 0; f < 15; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 14);
    }

    const aero::VisualNodeId topBlurId = host.FindVisualByOwnerName("TopPanelBlur");
    const aero::VisualNodeId botBlurId = host.FindVisualByOwnerName("BottomPanelBlur");
    const aero::VisualNodeId topSliderId = host.FindVisualByOwnerName("TopBlurRadius");
    const aero::VisualNodeId botSliderId = host.FindVisualByOwnerName("BottomBlurRadius");
    Require(topBlurId.IsValid() && botBlurId.IsValid() && topSliderId.IsValid() && botSliderId.IsValid(),
        "BackgroundBlur TopPanelBlur / BottomPanelBlur / Sliders missing");

    const aero::VisualNode* topBlurNode = host.World().GetNode(topBlurId);
    const aero::VisualNode* botBlurNode = host.World().GetNode(botBlurId);
    Require(topBlurNode && topBlurNode->Effect().kind == aero::UIEffectKind::BackdropBlur &&
            topBlurNode->Effect().radius > 15.0f,
        "BackgroundBlur TopPanelBlur should bind initial BackdropBlur radius (~20)");
    Require(botBlurNode && botBlurNode->Effect().kind == aero::UIEffectKind::BackdropBlur &&
            botBlurNode->Effect().radius > 5.0f,
        "BackgroundBlur BottomPanelBlur should bind initial BackdropBlur radius (~10)");

    const auto& spans0 = host.RenderTree().BlurLayerSpans();
    int backdropSpanCount = 0;
    aero::Rect firstBackdropMask{};
    for (const auto& sp : spans0) {
        if (sp.effectMode == aero::UIEffectKind::BackdropBlur) {
            ++backdropSpanCount;
            Require(sp.maskDestRect.width > 10.0f && sp.maskDestRect.height > 10.0f,
                "BackdropBlur BlurLayerSpan::maskDestRect must be valid and non-zero");
            Require(sp.maskOpacity > 0.0f, "BackdropBlur maskOpacity must be > 0");
            if (backdropSpanCount == 1) {
                firstBackdropMask = sp.maskDestRect;
            }
        }
    }
    Require(backdropSpanCount >= 2,
        "BackgroundBlur should emit >= 2 BackdropBlur BlurLayerSpans (got " + std::to_string(backdropSpanCount) + ")");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(960u, 540u, frame0), "BackgroundBlur initial render failed");

    // Verify BackdropBlur produces real pixel blur: compare TopBlurRadius slider changed to minimum (~0)
    const aero::VisualNode* topSliderNode = host.World().GetNode(topSliderId);
    const aero::VisualNode* botSliderNode = host.World().GetNode(botSliderId);
    Require(topSliderNode && topSliderNode->scriptOwner.valid() &&
            botSliderNode && botSliderNode->scriptOwner.valid(),
        "BackgroundBlur Slider scriptOwners missing");
    if (!topSliderNode || !botSliderNode) {
        return;
    }

    float topVal0 = 0.0f;
    float botVal0 = 0.0f;
    Require(ReadFloatMember(host, topSliderNode->scriptOwner, "Value", topVal0),
        "BackgroundBlur TopBlurRadius.Value unreadable");
    Require(ReadFloatMember(host, botSliderNode->scriptOwner, "Value", botVal0),
        "BackgroundBlur BottomBlurRadius.Value unreadable");

    // Click near left edge of TopBlurRadius slider to reduce blur radius sharply
    const aero::Rect topSliderVp = NodeViewportBounds(*topSliderNode);
    host.InjectClick(topSliderVp.x + 2.0f, topSliderVp.y + topSliderVp.height * 0.5f);
    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }

    float topVal1 = topVal0;
    Require(ReadFloatMember(host, topSliderNode->scriptOwner, "Value", topVal1) &&
            topVal1 < topVal0 - 5.0f,
        "Clicking left end of TopBlurRadius slider should decrease Value (before=" +
        std::to_string(topVal0) + ", after=" + std::to_string(topVal1) + ")");

    topBlurNode = host.World().GetNode(topBlurId);
    Require(topBlurNode && std::fabs(topBlurNode->Effect().radius - topVal1) < 1.5f,
        "TopPanelBlur BackdropBlur radius should update live with TopBlurRadius slider (nodeRadius=" +
        std::to_string(topBlurNode ? topBlurNode->Effect().radius : -1.0f) + ", slider=" +
        std::to_string(topVal1) + ")");

    bool foundUpdatedSpan = false;
    for (const auto& sp : host.RenderTree().BlurLayerSpans()) {
        if (sp.effectMode == aero::UIEffectKind::BackdropBlur &&
            std::fabs(sp.radiusPx - topVal1) < 1.5f) {
            foundUpdatedSpan = true;
            break;
        }
    }
    Require(foundUpdatedSpan, "BlurLayerSpan::radiusPx should reflect updated TopBlurRadius slider value");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(960u, 540u, frame1), "BackgroundBlur post-top-slider render failed");
    Require(CountChangedPixels(frame0, frame1) > 20u,
        "Changing TopBlurRadius slider must produce real BackdropBlur pixel changes (diff=" +
        std::to_string(CountChangedPixels(frame0, frame1)) + ")");

    // Also click near right edge of BottomBlurRadius slider to increase bottom blur radius
    const aero::Rect botSliderVp = NodeViewportBounds(*botSliderNode);
    host.InjectClick(botSliderVp.x + botSliderVp.width - 2.0f, botSliderVp.y + botSliderVp.height * 0.5f);
    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }
    float botVal1 = botVal0;
    Require(ReadFloatMember(host, botSliderNode->scriptOwner, "Value", botVal1) &&
            botVal1 > botVal0 + 5.0f,
        "Clicking right end of BottomBlurRadius slider should increase Value (before=" +
        std::to_string(botVal0) + ", after=" + std::to_string(botVal1) + ")");
    botBlurNode = host.World().GetNode(botBlurId);
    Require(botBlurNode && std::fabs(botBlurNode->Effect().radius - botVal1) < 1.5f,
        "BottomPanelBlur BackdropBlur radius should update live with BottomBlurRadius slider");

    std::vector<std::uint8_t> frame2;
    Require(host.RenderHeadlessRgba(960u, 540u, frame2), "BackgroundBlur post-bottom-slider render failed");
    Require(CountChangedPixels(frame1, frame2) > 20u,
        "Changing BottomBlurRadius slider must produce real BackdropBlur pixel changes");
    (void)firstBackdropMask;
}

void TestInteractiveGallery(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Gallery...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Gallery", &session, ci, hc, 1280u, 800u),
        "Gallery discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Gallery load failed: " + err);

    for (int f = 0; f < 20; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 19);
    }

    const aero::VisualNodeId selectorTreeId = host.FindVisualByOwnerName("SelectorTree");
    Require(selectorTreeId.IsValid(), "Gallery SelectorTree missing");
    const aero::VisualNode* selectorTreeNode = host.World().GetNode(selectorTreeId);
    Require(selectorTreeNode != nullptr, "Gallery SelectorTree node null");
    if (!selectorTreeNode) {
        return;
    }

    // If SelectorBar starts collapsed off-screen to the left, click SelectorExpanderButton to slide it in
    if (NodeViewportBounds(*selectorTreeNode).x < -5.0f) {
        float ex = 0.0f, ey = 0.0f;
        if (RequireNodeCenter(host, "SelectorExpanderButton", ex, ey)) {
            host.InjectClick(ex, ey);
            for (int f = 0; f < 38; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 37);
            }
        }
        selectorTreeNode = host.World().GetNode(selectorTreeId);
    }
    const aero::Rect treeVp = selectorTreeNode ? NodeViewportBounds(*selectorTreeNode) : aero::Rect{};
    Require(treeVp.x >= -2.0f && treeVp.width > 120.0f && treeVp.height > 200.0f,
        "Gallery SelectorTree should be visible on screen (x=" + std::to_string(treeVp.x) +
        ", w=" + std::to_string(treeVp.width) + ")");

    // Collect TreeViewItems realized by HierarchicalDataTemplate (categories + child samples)
    std::vector<aero::VisualNodeId> allTreeItems;
    CollectDescendantsByTypeName(host, selectorTreeId, "TreeViewItem", allTreeItems);
    Require(allTreeItems.size() >= 7u,
        "Gallery SelectorTree should realize >= 7 category TreeViewItems via HierarchicalDataTemplate (got " +
        std::to_string(allTreeItems.size()) + ")");

    auto findTreeItemByText = [&](aero::VisualNodeId scopeId, std::string_view needle) -> aero::VisualNodeId {
        std::vector<aero::VisualNodeId> candidates;
        CollectDescendantsByTypeName(host, scopeId, "TreeViewItem", candidates);
        for (aero::VisualNodeId tid : candidates) {
            std::string txt;
            CollectSubtreeText(host.World(), tid, txt);
            if (txt.find(needle) != std::string::npos) {
                return tid;
            }
        }
        return aero::VisualNodeId{};
    };

    const aero::VisualNodeId mediaCatId = findTreeItemByText(selectorTreeId, "Media");
    const aero::VisualNodeId basicCatId = findTreeItemByText(selectorTreeId, "Basic Input");
    Require(mediaCatId.IsValid() && basicCatId.IsValid(),
        "Gallery TreeView missing 'Media' or 'Basic Input' category items");

    // Verify HierarchicalDataTemplate registered >= 5 child items under 'Media' before expansion
    std::int64_t mediaLogicalItemCnt = 0;
    {
        const aero::VisualNode* mediaNode = host.World().GetNode(mediaCatId);
        aero::UnifiedValue cntVal{};
        Require(mediaNode && mediaNode->scriptOwner.valid() &&
                host.InvokeScriptMethod(mediaNode->scriptOwner, "ItemCount", {}, &cntVal) &&
                cntVal.tryGetInt64(mediaLogicalItemCnt) && mediaLogicalItemCnt >= 5,
            "HierarchicalDataTemplate should register >= 5 logical items under 'Media' (got " +
            std::to_string(mediaLogicalItemCnt) + ")");
    }

    // Helper to expand a category TreeViewItem by clicking its header Border/ExpandButton
    auto clickTreeItemHeader = [&](aero::VisualNodeId itemId) {
        if (!itemId.IsValid()) { return; }
        aero::VisualNodeId headerTarget = FindDescendantByOwnerName(host, itemId, "Border");
        if (!headerTarget.IsValid()) {
            headerTarget = FindDescendantByOwnerName(host, itemId, "ExpandButton");
        }
        if (!headerTarget.IsValid()) {
            headerTarget = itemId;
        }
        const aero::VisualNode* targetNode = host.World().GetNode(headerTarget);
        if (!targetNode) { return; }
        const aero::Rect vp = NodeViewportBounds(*targetNode);
        host.InjectClick(vp.x + vp.width * 0.5f, vp.y + vp.height * 0.5f);
    };

    if (mediaCatId.IsValid()) {
        clickTreeItemHeader(mediaCatId);
        for (int f = 0; f < 20; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 19);
        }
        bool mediaExpanded = false;
        const aero::VisualNode* mediaNode = host.World().GetNode(mediaCatId);
        Require(mediaNode && ReadBoolMember(host, mediaNode->scriptOwner, "IsExpanded", mediaExpanded) && mediaExpanded,
            "Clicking 'Media' category TreeViewItem should expand it (IsExpanded == true)");
    }

    // Verify HierarchicalDataTemplate materialized child TreeViewItems under 'Media' upon expansion
    std::vector<aero::VisualNodeId> mediaSubItems;
    CollectDescendantsByTypeName(host, mediaCatId, "TreeViewItem", mediaSubItems);
    Require(mediaSubItems.size() >= 5u,
        "HierarchicalDataTemplate should generate >= 5 sample TreeViewItems under 'Media' after expansion (got " +
        std::to_string(mediaSubItems.size()) + ")");

    auto activeSampleContainerId = [&]() -> aero::VisualNodeId {
        const aero::VisualNodeId c1 = host.FindVisualByOwnerName("SampleContainer1");
        const aero::VisualNodeId c2 = host.FindVisualByOwnerName("SampleContainer2");
        const aero::VisualNode* n1 = c1.IsValid() ? host.World().GetNode(c1) : nullptr;
        const aero::VisualNode* n2 = c2.IsValid() ? host.World().GetNode(c2) : nullptr;
        if (n1 && n1->layout.visibility == aero::Visibility::Visible && !n1->children.empty()) {
            return c1;
        }
        if (n2 && n2->layout.visibility == aero::Visibility::Visible && !n2->children.empty()) {
            return c2;
        }
        return aero::VisualNodeId{};
    };

    // Click 'Brushes' child TreeViewItem -> loads real Samples/Brushes.xaml tree (not fake colored tiles)
    const aero::VisualNodeId brushesItemId = mediaCatId.IsValid()
        ? findTreeItemByText(mediaCatId, "Brushes")
        : aero::VisualNodeId{};
    Require(brushesItemId.IsValid(), "Gallery 'Media' category missing 'Brushes' child TreeViewItem");
    if (brushesItemId.IsValid()) {
        clickTreeItemHeader(brushesItemId);
        for (int f = 0; f < 55; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 54);
        }
        std::string loadedUri;
        Require(ReadStringMember(host, host.RootScriptObject(), "LoadedSampleUri", loadedUri) &&
                loadedUri == "Samples/Brushes.xaml",
            "Clicking 'Brushes' should load Samples/Brushes.xaml (got '" + loadedUri + "')");
        const aero::VisualNodeId containerId = activeSampleContainerId();
        Require(containerId.IsValid(), "Gallery SampleContainer should become visible after selecting Brushes");
        if (containerId.IsValid()) {
            std::string containerText;
            CollectSubtreeText(host.World(), containerId, containerText);
            Require(containerText.find("SolidColorBrush") != std::string::npos &&
                    containerText.find("LinearGradientBrush") != std::string::npos,
                "Gallery Brushes should render real Brushes.xaml XAML tree (got: " + containerText.substr(0, 120) + ")");
            Require(containerText.find("Brushes #1") == std::string::npos &&
                    containerText.find("AeroGUI v3 Controls Gallery") == std::string::npos,
                "Gallery Brushes must not fall back to placeholder blocks");
        }
    }

    std::vector<std::uint8_t> frameBrushes;
    Require(host.RenderHeadlessRgba(1280u, 800u, frameBrushes), "Gallery Brushes render failed");

    // Click 'Effects' child TreeViewItem -> loads real Samples/Effects.xaml tree
    const aero::VisualNodeId effectsItemId = mediaCatId.IsValid()
        ? findTreeItemByText(mediaCatId, "Effects")
        : aero::VisualNodeId{};
    Require(effectsItemId.IsValid(), "Gallery 'Media' category missing 'Effects' child TreeViewItem");
    if (effectsItemId.IsValid()) {
        clickTreeItemHeader(effectsItemId);
        for (int f = 0; f < 55; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 54);
        }
        std::string loadedUri;
        Require(ReadStringMember(host, host.RootScriptObject(), "LoadedSampleUri", loadedUri) &&
                loadedUri == "Samples/Effects.xaml",
            "Clicking 'Effects' should load Samples/Effects.xaml (got '" + loadedUri + "')");
        const aero::VisualNodeId containerId = activeSampleContainerId();
        Require(containerId.IsValid(), "Gallery SampleContainer should become visible after selecting Effects");
        if (containerId.IsValid()) {
            std::string containerText;
            CollectSubtreeText(host.World(), containerId, containerText);
            Require(containerText.find("BlurEffect") != std::string::npos &&
                    containerText.find("DropShadowEffect") != std::string::npos,
                "Gallery Effects should render real Effects.xaml XAML tree (got: " + containerText.substr(0, 120) + ")");
            Require(containerText.find("Effects #1") == std::string::npos,
                "Gallery Effects must not fall back to placeholder blocks");
        }
    }

    // Expand 'Basic Input' and click 'Button' -> loads real Samples/Button.xaml tree
    if (basicCatId.IsValid()) {
        clickTreeItemHeader(basicCatId);
        for (int f = 0; f < 20; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 19);
        }
    }
    const aero::VisualNodeId buttonItemId = basicCatId.IsValid()
        ? findTreeItemByText(basicCatId, "Button")
        : aero::VisualNodeId{};
    Require(buttonItemId.IsValid(), "Gallery 'Basic Input' category missing 'Button' child TreeViewItem");
    if (buttonItemId.IsValid()) {
        clickTreeItemHeader(buttonItemId);
        for (int f = 0; f < 55; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 54);
        }
        std::string loadedUri;
        Require(ReadStringMember(host, host.RootScriptObject(), "LoadedSampleUri", loadedUri) &&
                loadedUri == "Samples/Button.xaml",
            "Clicking 'Button' should load Samples/Button.xaml (got '" + loadedUri + "')");
        const aero::VisualNodeId containerId = activeSampleContainerId();
        Require(containerId.IsValid(), "Gallery SampleContainer should become visible after selecting Button");
        if (containerId.IsValid()) {
            std::string containerText;
            CollectSubtreeText(host.World(), containerId, containerText);
            Require(containerText.find("ClickMode") != std::string::npos &&
                    containerText.find("Normal Button") != std::string::npos,
                "Gallery Button should render real Button.xaml XAML tree (got: " + containerText.substr(0, 120) + ")");
            Require(containerText.find("Button #1") == std::string::npos,
                "Gallery Button must not fall back to placeholder blocks");
        }
    }

    std::vector<std::uint8_t> frameButton;
    Require(host.RenderHeadlessRgba(1280u, 800u, frameButton), "Gallery Button render failed");
    Require(CountChangedPixels(frameBrushes, frameButton) > 200u,
        "Switching Gallery samples (Brushes -> Button) must produce substantial visual changes");

    // Theme switching via ThemeColorPicker ComboBox (Light -> Dark) -> real pixel difference
    const aero::VisualNodeId colorPickerId = host.FindVisualByOwnerName("ThemeColorPicker");
    Require(colorPickerId.IsValid(), "Gallery ThemeColorPicker ComboBox missing");
    if (colorPickerId.IsValid()) {
        const aero::Rect pickerVp = NodeViewportBounds(*host.World().GetNode(colorPickerId));
        host.InjectClick(pickerVp.x + pickerVp.width - 16.0f, pickerVp.y + pickerVp.height * 0.5f);
        for (int f = 0; f < 5; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 4);
        }
        aero::VisualNodeId popupId;
        std::vector<aero::VisualNodeId> themeItems;
        {
            std::vector<aero::VisualNodeId> popups;
            CollectVisualNodesByOwnerName(host, "PART_Popup", popups);
            for (aero::VisualNodeId pid : popups) {
                const aero::VisualNode* pn = host.World().GetNode(pid);
                if (pn && pn->layout.visibility == aero::Visibility::Visible) {
                    std::vector<aero::VisualNodeId> items;
                    CollectDescendantsByTypeName(host, pid, "ComboBoxItem", items);
                    if (!items.empty()) {
                        popupId = pid;
                        themeItems = std::move(items);
                        break;
                    }
                }
            }
            if (!popupId.IsValid()) {
                popupId = FindDescendantByOwnerName(host, colorPickerId, "PART_Popup");
                if (popupId.IsValid()) {
                    CollectDescendantsByTypeName(host, popupId, "ComboBoxItem", themeItems);
                }
            }
        }
        Require(themeItems.size() >= 2u,
            "Gallery ThemeColorPicker should open PART_Popup with Dark & Light ComboBoxItems (got " +
            std::to_string(themeItems.size()) + ")");
        if (!themeItems.empty()) {
            const aero::Rect darkItemVp = NodeViewportBounds(*host.World().GetNode(themeItems[0]));
            host.InjectClick(darkItemVp.x + darkItemVp.width * 0.5f, darkItemVp.y + darkItemVp.height * 0.5f);
            for (int f = 0; f < 55; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 54);
            }
            aero::ObjectRef vmRef{};
            std::string themeName;
            const bool okVm = ReadObjectMember(host, host.RootScriptObject(), "ViewModel", vmRef);
            const bool okThemeName = okVm && ReadStringMember(host, vmRef, "SelectedThemeColor.Name", themeName);
            Require(okThemeName && themeName == "Dark",
                "Selecting Dark in ThemeColorPicker should update ViewModel.SelectedThemeColor.Name to 'Dark' (got '" +
                themeName + "')");
        }
    }

    std::vector<std::uint8_t> frameDarkTheme;
    Require(host.RenderHeadlessRgba(1280u, 800u, frameDarkTheme), "Gallery Dark theme render failed");
    Require(CountChangedPixels(frameButton, frameDarkTheme) > 500u,
        "Gallery theme switch (Light -> Dark) must change rendered pixels");
}

void TestInteractiveGalleryReportedSubPages(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Gallery Reported SubPages (treeShakeStdlib=true)...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Gallery", &session, ci, hc, 1280u, 800u),
        "GalleryReported discover failed");
    // Match aero-viewer defaults exactly
    ci.treeShakeStdlib = true;
    ci.allowStdlibCache = false;
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "GalleryReported load failed: " + err);

    for (int f = 0; f < 20; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 19);
    }

    auto readProcMemMb = [](double& outPrivateMb, double& outWorkingMb) {
        outPrivateMb = 0.0;
        outWorkingMb = 0.0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (K32GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                sizeof(pmc))) {
            outPrivateMb = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
            outWorkingMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }
#endif
    };

    auto activeSampleContainerId = [&]() -> aero::VisualNodeId {
        const aero::VisualNodeId c1 = host.FindVisualByOwnerName("SampleContainer1");
        const aero::VisualNodeId c2 = host.FindVisualByOwnerName("SampleContainer2");
        const aero::VisualNode* n1 = c1.IsValid() ? host.World().GetNode(c1) : nullptr;
        const aero::VisualNode* n2 = c2.IsValid() ? host.World().GetNode(c2) : nullptr;
        if (n1 && n1->layout.visibility == aero::Visibility::Visible && !n1->children.empty()) {
            return c1;
        }
        if (n2 && n2->layout.visibility == aero::Visibility::Visible && !n2->children.empty()) {
            return c2;
        }
        return aero::VisualNodeId{};
    };

    auto loadSample = [&](const char* name, int frames = 55) -> aero::VisualNodeId {
        aero::UnifiedValue ret{};
        const bool okCall = host.InvokeScriptMethod(
            host.RootScriptObject(),
            "SelectSampleByName",
            {aero::UnifiedValue{aero::rs_rt::StringValue{std::string(name)}}},
            &ret);
        bool okBool = false;
        (void)ret.tryGetBool(okBool);
        Require(okCall && okBool, std::string("GalleryReported SelectSampleByName('") + name + "') failed");
        for (int f = 0; f < frames; ++f) {
            host.StepFrame(1.0f / 60.0f, f == (frames - 1));
        }
        const aero::VisualNodeId cid = activeSampleContainerId();
        Require(cid.IsValid(), std::string("GalleryReported SampleContainer not active after loading ") + name);
        return cid;
    };

    auto scrollActiveSampleTo = [&](aero::VisualNodeId containerId, aero::VisualNodeId targetId) {
        if (!containerId.IsValid() || !targetId.IsValid()) { return; }
        const aero::VisualNode* tn = host.World().GetNode(targetId);
        if (!tn) { return; }
        const aero::Rect vp = NodeViewportBounds(*tn);
        if (vp.y >= 70.0f && vp.y + vp.height <= 760.0f) {
            return;
        }
        std::vector<aero::VisualNodeId> svs;
        CollectDescendantsByTypeName(host, containerId, "ScrollViewer", svs);
        if (svs.empty()) { return; }
        const aero::VisualNode* svNode = host.World().GetNode(svs[0]);
        if (!svNode || !svNode->scriptOwner.valid()) { return; }
        float curOff = 0.0f;
        (void)ReadFloatMember(host, svNode->scriptOwner, "VerticalOffset", curOff);
        const aero::Rect svVp = NodeViewportBounds(*svNode);
        float nextOff = curOff + (vp.y - (svVp.y + 80.0f));
        if (nextOff < 0.0f) { nextOff = 0.0f; }
        (void)host.InvokeScriptMethod(
            svNode->scriptOwner,
            "ScrollToVerticalOffset",
            {aero::UnifiedValue{aero::FloatValue{nextOff}}});
        for (int f = 0; f < 4; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 3);
        }
    };

    auto readItemAndContainerCounts = [&](aero::VisualNodeId ctrlId, std::int64_t& outItems, std::int64_t& outCont) {
        outItems = -1;
        outCont = -1;
        const aero::VisualNode* n = host.World().GetNode(ctrlId);
        if (!n || !n->scriptOwner.valid()) { return; }
        aero::UnifiedValue r1{}, r2{};
        if (host.InvokeScriptMethod(n->scriptOwner, "ItemCount", {}, &r1)) {
            (void)r1.tryGetInt64(outItems);
        }
        if (host.InvokeScriptMethod(n->scriptOwner, "ContainerCount", {}, &r2)) {
            (void)r2.tryGetInt64(outCont);
        }
    };

    auto findVisibleDropdownPopup = [&](std::vector<aero::VisualNodeId>& outComboItems) -> aero::VisualNodeId {
        outComboItems.clear();
        std::vector<aero::VisualNodeId> popups;
        CollectVisualNodesByOwnerName(host, "PART_Popup", popups);
        for (aero::VisualNodeId pid : popups) {
            const aero::VisualNode* pn = host.World().GetNode(pid);
            if (pn && pn->layout.visibility == aero::Visibility::Visible) {
                CollectDescendantsByTypeName(host, pid, "ComboBoxItem", outComboItems);
                return pid;
            }
        }
        return aero::VisualNodeId{};
    };

    // 1. ComboBox sub-page: item counts + dropdown open on Colors, FontFamilies, FontSizes
    {
        const aero::VisualNodeId cid = loadSample("ComboBox");
        if (cid.IsValid()) {
            const char* kComboNames[3] = {"Colors", "FontFamilies", "FontSizes"};
            const std::int64_t kExpectedItems[3] = {3, 3, 8};
            for (int ciIdx = 0; ciIdx < 3; ++ciIdx) {
                const char* cbName = kComboNames[ciIdx];
                const aero::VisualNodeId cbId = FindDescendantByOwnerName(host, cid, cbName);
                Require(cbId.IsValid(), std::string("Gallery/ComboBox missing ") + cbName);
                if (!cbId.IsValid()) { continue; }
                scrollActiveSampleTo(cid, cbId);
                const aero::VisualNode* cbNode = host.World().GetNode(cbId);
                std::int64_t itemCnt = -1, contCnt = -1;
                readItemAndContainerCounts(cbId, itemCnt, contCnt);
                const aero::Rect cbVp = cbNode ? NodeViewportBounds(*cbNode) : aero::Rect{};
                std::cout << "[DIAG][ComboBox][" << cbName << "] items=" << itemCnt
                          << " containers=" << contCnt
                          << " vp=(" << cbVp.x << "," << cbVp.y << " " << cbVp.width << "x" << cbVp.height << ")\n";
                Require(itemCnt == kExpectedItems[ciIdx],
                    std::string("Gallery/ComboBox ") + cbName + " expected " +
                    std::to_string(kExpectedItems[ciIdx]) + " logical items (got items=" +
                    std::to_string(itemCnt) + ", containers=" + std::to_string(contCnt) + ")");
                Require(cbVp.width > 20.0f && cbVp.height > 10.0f,
                    std::string("Gallery/ComboBox ") + cbName + " degenerate viewport bounds");

                // Click arrow toggle region near right edge (works for both normal and IsEditable ComboBoxes)
                host.InjectClick(cbVp.x + cbVp.width - 14.0f, cbVp.y + cbVp.height * 0.5f);
                for (int f = 0; f < 6; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 5);
                }
                std::int64_t postItemCnt = -1, postContCnt = -1;
                readItemAndContainerCounts(cbId, postItemCnt, postContCnt);
                bool isOpen = false;
                if (cbNode && cbNode->scriptOwner.valid()) {
                    (void)ReadBoolMember(host, cbNode->scriptOwner, "IsDropDownOpen", isOpen);
                }
                std::vector<aero::VisualNodeId> popItems;
                const aero::VisualNodeId visPopId = findVisibleDropdownPopup(popItems);
                const aero::VisualNode* visPopNode = visPopId.IsValid() ? host.World().GetNode(visPopId) : nullptr;
                const aero::Rect popVp = visPopNode ? NodeViewportBounds(*visPopNode) : aero::Rect{};
                std::cout << "[DIAG][ComboBox][" << cbName << "] afterClick IsDropDownOpen=" << (isOpen ? 1 : 0)
                          << " containers=" << postContCnt
                          << " visPop=" << (visPopId.IsValid() ? 1 : 0)
                          << " popItems=" << popItems.size()
                          << " popVp=(" << popVp.x << "," << popVp.y << " " << popVp.width << "x" << popVp.height << ")\n";
                Require(isOpen && postContCnt == kExpectedItems[ciIdx] &&
                        visPopId.IsValid() && popVp.width > 20.0f && popVp.height > 20.0f &&
                        static_cast<std::int64_t>(popItems.size()) >= kExpectedItems[ciIdx],
                    std::string("Gallery/ComboBox ") + cbName + " failed to open dropdown with items (open=" +
                    std::to_string(isOpen ? 1 : 0) + ", containers=" + std::to_string(postContCnt) +
                    ", popItems=" + std::to_string(popItems.size()) + ")");

                // Dismiss popup via Escape before testing next ComboBox
                host.InjectKey(aero::KeyCode::Escape, true);
                host.InjectKey(aero::KeyCode::Escape, false);
                for (int f = 0; f < 4; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 3);
                }
            }
        }
    }

    // 2. ListBox sub-page: List1 (10 items), SelectionMode ComboBox opens, second ListBox (ItemsSource=10 items)
    {
        const aero::VisualNodeId cid = loadSample("ListBox");
        if (cid.IsValid()) {
            const aero::VisualNodeId list1Id = FindDescendantByOwnerName(host, cid, "List1");
            Require(list1Id.IsValid(), "Gallery/ListBox missing List1");
            if (list1Id.IsValid()) {
                std::int64_t itemCnt = -1, contCnt = -1;
                readItemAndContainerCounts(list1Id, itemCnt, contCnt);
                std::vector<aero::VisualNodeId> realized;
                CollectDescendantsByTypeName(host, list1Id, "ListBoxItem", realized);
                std::string txt;
                CollectSubtreeText(host.World(), list1Id, txt);
                std::cout << "[DIAG][ListBox][List1] itemCnt=" << itemCnt << " contCnt=" << contCnt
                          << " realized=" << realized.size() << " text='" << txt.substr(0, 60) << "'\n";
                Require(itemCnt == 10 && realized.size() == 10u && txt.find("Item 0") != std::string::npos,
                    "Gallery/ListBox List1 should have 10 ListBoxItems");
            }
            const aero::VisualNodeId selModeId = FindDescendantByOwnerName(host, cid, "SelectionMode");
            Require(selModeId.IsValid(), "Gallery/ListBox missing SelectionMode ComboBox");
            if (selModeId.IsValid()) {
                scrollActiveSampleTo(cid, selModeId);
                const aero::VisualNode* smNode = host.World().GetNode(selModeId);
                const aero::Rect smVp = NodeViewportBounds(*smNode);
                host.InjectClick(smVp.x + smVp.width - 14.0f, smVp.y + smVp.height * 0.5f);
                for (int f = 0; f < 6; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 5);
                }
                bool isOpen = false;
                (void)ReadBoolMember(host, smNode->scriptOwner, "IsDropDownOpen", isOpen);
                std::vector<aero::VisualNodeId> popItems;
                const aero::VisualNodeId visPopId = findVisibleDropdownPopup(popItems);
                std::cout << "[DIAG][ListBox][SelectionMode] IsDropDownOpen=" << (isOpen ? 1 : 0)
                          << " popItems=" << popItems.size() << "\n";
                Require(isOpen && visPopId.IsValid() && popItems.size() == 3u,
                    "Gallery/ListBox SelectionMode ComboBox should open with 3 items");
                host.InjectKey(aero::KeyCode::Escape, true);
                host.InjectKey(aero::KeyCode::Escape, false);
                host.StepFrame(1.0f / 60.0f, true);
            }
            std::vector<aero::VisualNodeId> allLbs;
            CollectDescendantsByTypeName(host, cid, "ListBox", allLbs);
            std::cout << "[DIAG][ListBox] total ListBox count=" << allLbs.size() << "\n";
            Require(allLbs.size() >= 2u, "Gallery/ListBox expected >= 2 ListBoxes");
            if (allLbs.size() >= 2u) {
                std::int64_t ic2 = -1, cc2 = -1;
                readItemAndContainerCounts(allLbs[1], ic2, cc2);
                std::vector<aero::VisualNodeId> realized2;
                CollectDescendantsByTypeName(host, allLbs[1], "ListBoxItem", realized2);
                std::cout << "[DIAG][ListBox][ItemsSource] itemCnt=" << ic2 << " contCnt=" << cc2
                          << " realized=" << realized2.size() << "\n";
                Require(ic2 == 10 && realized2.size() == 10u,
                    "Gallery/ListBox #2 (ItemsSource) should realize 10 ListBoxItems (got items=" +
                    std::to_string(ic2) + ", realized=" + std::to_string(realized2.size()) + ")");
            }
        }
    }

    // 3. ListView sub-page: 15 rows + column headers + text
    {
        const aero::VisualNodeId cid = loadSample("ListView");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> lvs;
            CollectDescendantsByTypeName(host, cid, "ListView", lvs);
            Require(!lvs.empty(), "Gallery/ListView missing ListView");
            if (!lvs.empty()) {
                std::int64_t ic = -1, cc = -1;
                readItemAndContainerCounts(lvs[0], ic, cc);
                std::vector<aero::VisualNodeId> items, hdrs;
                CollectDescendantsByTypeName(host, lvs[0], "ListViewItem", items);
                CollectDescendantsByTypeName(host, lvs[0], "GridViewColumnHeader", hdrs);
                std::string txt;
                CollectSubtreeText(host.World(), lvs[0], txt);
                std::cout << "[DIAG][ListView] itemCnt=" << ic << " contCnt=" << cc
                          << " items=" << items.size() << " hdrs=" << hdrs.size()
                          << " text='" << txt.substr(0, 80) << "'\n";
                Require(ic == 15 && items.size() >= 15u && hdrs.size() >= 5u &&
                        txt.find("Carrot") != std::string::npos &&
                        txt.find("Broccoli") != std::string::npos &&
                        txt.find("#1") != std::string::npos &&
                        txt.find("0.69") != std::string::npos,
                    "Gallery/ListView expected 15 ListViewItems, >=5 headers, and Carrot/Broccoli rows");
            }
        }
    }

    // 4. TreeView sub-page: Tree1 (7 root items) + Tree2 (ItemsSource FileSystem -> Computer)
    {
        const aero::VisualNodeId cid = loadSample("TreeView");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> tvs;
            CollectDescendantsByTypeName(host, cid, "TreeView", tvs);
            Require(tvs.size() >= 2u, "Gallery/TreeView expected >= 2 TreeViews (got " + std::to_string(tvs.size()) + ")");
            if (!tvs.empty()) {
                std::int64_t ic1 = -1, cc1 = -1;
                readItemAndContainerCounts(tvs[0], ic1, cc1);
                std::vector<aero::VisualNodeId> tvis1;
                CollectDescendantsByTypeName(host, tvs[0], "TreeViewItem", tvis1);
                std::string txt1;
                CollectSubtreeText(host.World(), tvs[0], txt1);
                std::cout << "[DIAG][TreeView][Tree1] itemCnt=" << ic1 << " contCnt=" << cc1
                          << " allTreeItems=" << tvis1.size() << " text='" << txt1.substr(0, 60) << "'\n";
                Require(ic1 == 7 && tvis1.size() >= 7u && txt1.find("Level 1") != std::string::npos,
                    "Gallery/TreeView Tree1 expected 7 root TreeViewItems");
            }
            if (tvs.size() >= 2u) {
                scrollActiveSampleTo(cid, tvs[1]);
                std::int64_t ic2 = -1, cc2 = -1;
                readItemAndContainerCounts(tvs[1], ic2, cc2);
                std::vector<aero::VisualNodeId> tvis2;
                CollectDescendantsByTypeName(host, tvs[1], "TreeViewItem", tvis2);
                std::string txt2;
                CollectSubtreeText(host.World(), tvs[1], txt2);
                std::cout << "[DIAG][TreeView][Tree2] itemCnt=" << ic2 << " contCnt=" << cc2
                          << " allTreeItems=" << tvis2.size() << " text='" << txt2.substr(0, 80) << "'\n";
                Require(ic2 >= 1 && !tvis2.empty() && txt2.find("Computer") != std::string::npos,
                    "Gallery/TreeView #2 (ItemsSource) expected realized TreeViewItem containing 'Computer'");
            }
        }
    }

    // 5. TabControl sub-page: TabControl #1 (3 TabItems) + TabControl #2 (ItemsSource + ContentTemplate)
    //    + EnableContent ToggleSwitch background leak + TabControl pixel verification
    {
        const aero::VisualNodeId cid = loadSample("TabControl");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> tcs;
            CollectDescendantsByTypeName(host, cid, "TabControl", tcs);
            Require(tcs.size() >= 2u, "Gallery/TabControl expected >= 2 TabControls (got " + std::to_string(tcs.size()) + ")");
            if (!tcs.empty()) {
                std::int64_t ic1 = -1, cc1 = -1;
                readItemAndContainerCounts(tcs[0], ic1, cc1);
                std::vector<aero::VisualNodeId> tabs1;
                CollectDescendantsByTypeName(host, tcs[0], "TabItem", tabs1);
                std::string txt1;
                CollectSubtreeText(host.World(), tcs[0], txt1);
                const aero::VisualNode* tc0Node = host.World().GetNode(tcs[0]);
                const aero::Rect tc0Vp = tc0Node ? NodeViewportBounds(*tc0Node) : aero::Rect{};
                const aero::VisualNodeId scbId = FindDescendantByOwnerName(host, tcs[0], "SelectedContentBorder");
                const aero::VisualNode* scbNode = scbId.IsValid() ? host.World().GetNode(scbId) : nullptr;
                const aero::Rect scbVp = scbNode ? NodeViewportBounds(*scbNode) : aero::Rect{};
                std::cout << "[DIAG][TabControl][1] itemCnt=" << ic1 << " contCnt=" << cc1
                          << " tabs=" << tabs1.size()
                          << " tcVp=(" << tc0Vp.x << "," << tc0Vp.y << " " << tc0Vp.width << "x" << tc0Vp.height << ")"
                          << " scbVp=(" << scbVp.x << "," << scbVp.y << " " << scbVp.width << "x" << scbVp.height << ")"
                          << " text='" << txt1.substr(0, 60) << "'\n";
                Require(ic1 == 3 && tabs1.size() == 3u && txt1.find("Page 1") != std::string::npos,
                    "Gallery/TabControl #1 expected 3 TabItems with Page 1..3");
                Require(tc0Vp.width >= 300.0f && tc0Vp.height >= 180.0f &&
                        scbVp.width >= 300.0f && scbVp.height >= 100.0f,
                    "Gallery/TabControl #1 must have non-collapsed TabControl and SelectedContentBorder bounds");
                for (aero::VisualNodeId tid : tabs1) {
                    const aero::VisualNode* tn = host.World().GetNode(tid);
                    const aero::Rect tvp = tn ? NodeViewportBounds(*tn) : aero::Rect{};
                    Require(tvp.width > 20.0f && tvp.height > 15.0f,
                        "Gallery/TabControl #1 TabItem must have non-zero bounds");
                }

                std::vector<std::uint8_t> tabRgba;
                Require(host.RenderHeadlessRgba(1280u, 800u, tabRgba), "Gallery/TabControl render failed");
                std::unordered_set<std::uint32_t> contentColors;
                const int x0 = std::max(0, static_cast<int>(scbVp.x + 8.0f));
                const int y0 = std::max(0, static_cast<int>(scbVp.y + 8.0f));
                const int x1 = std::min(1280, static_cast<int>(scbVp.x + scbVp.width - 8.0f));
                const int y1 = std::min(800, static_cast<int>(scbVp.y + scbVp.height - 8.0f));
                for (int py = y0; py < y1; py += 4) {
                    for (int px = x0; px < x1; px += 4) {
                        const std::size_t off = (static_cast<std::size_t>(py) * 1280u + static_cast<std::size_t>(px)) * 4u;
                        contentColors.insert(PackRgba(tabRgba[off], tabRgba[off + 1u], tabRgba[off + 2u], tabRgba[off + 3u]));
                    }
                }
                Require(contentColors.size() >= 4u,
                    "Gallery/TabControl #1 SelectedContentBorder must render colorful Page 1 rectangles (colors=" +
                    std::to_string(contentColors.size()) + ")");

                const aero::VisualNodeId enableId = FindDescendantByOwnerName(host, cid, "EnableContent");
                const aero::VisualNode* enableNode = enableId.IsValid() ? host.World().GetNode(enableId) : nullptr;
                Require(enableNode != nullptr, "Gallery/TabControl missing EnableContent ToggleSwitch");
                if (enableNode) {
                    Require((enableNode->box.bgRgba >> 24u) == 0u && enableNode->box.bgGradId <= 0,
                        "Gallery/TabControl EnableContent ToggleButton must not leak default #FF3B82F6 Background");
                    const aero::Rect evp = NodeViewportBounds(*enableNode);
                    const int cx = std::clamp(static_cast<int>(evp.x + 1.0f), 0, 1279);
                    const int cy = std::clamp(static_cast<int>(evp.y + 1.0f), 0, 799);
                    const std::size_t coff = (static_cast<std::size_t>(cy) * 1280u + static_cast<std::size_t>(cx)) * 4u;
                    const bool isDefaultBlue =
                        std::abs(static_cast<int>(tabRgba[coff]) - 0x3B) <= 4 &&
                        std::abs(static_cast<int>(tabRgba[coff + 1u]) - 0x82) <= 4 &&
                        std::abs(static_cast<int>(tabRgba[coff + 2u]) - 0xF6) <= 4;
                    Require(!isDefaultBlue,
                        "Gallery/TabControl EnableContent top-left corner pixel must not render #FF3B82F6 rectangle");
                }
            }
            if (tcs.size() >= 2u) {
                scrollActiveSampleTo(cid, tcs[1]);
                std::int64_t ic2 = -1, cc2 = -1;
                readItemAndContainerCounts(tcs[1], ic2, cc2);
                std::vector<aero::VisualNodeId> tabs2;
                CollectDescendantsByTypeName(host, tcs[1], "TabItem", tabs2);
                std::string txt2;
                CollectSubtreeText(host.World(), tcs[1], txt2);
                const aero::VisualNode* tc1Node = host.World().GetNode(tcs[1]);
                const aero::Rect tc1Vp = tc1Node ? NodeViewportBounds(*tc1Node) : aero::Rect{};
                std::cout << "[DIAG][TabControl][2] itemCnt=" << ic2 << " contCnt=" << cc2
                          << " tabs=" << tabs2.size()
                          << " tcVp=(" << tc1Vp.x << "," << tc1Vp.y << " " << tc1Vp.width << "x" << tc1Vp.height << ")"
                          << " text='" << txt2.substr(0, 120) << "'\n";
                Require(ic2 == 3 && tabs2.size() == 3u &&
                        tc1Vp.width >= 300.0f && tc1Vp.height >= 180.0f &&
                        txt2.find("10") != std::string::npos &&
                        txt2.find("Lorem ipsum") != std::string::npos,
                    "Gallery/TabControl #2 (ItemsSource) expected 3 tabs, non-collapsed bounds, and ContentTemplate Lorem ipsum content");
            }
        }
    }

    // 5b. ThemeAccentPicker dropdown popup scrollbar must align flush to right edge of DropDownBorder
    {
        host.InjectResize(1280u, 520u);
        for (int f = 0; f < 4; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 3);
        }
        const aero::VisualNodeId accentPickerId = host.FindVisualByOwnerName("ThemeAccentPicker");
        Require(accentPickerId.IsValid(), "Gallery missing ThemeAccentPicker");
        if (accentPickerId.IsValid()) {
            const aero::Rect apVp = NodeViewportBounds(*host.World().GetNode(accentPickerId));
            host.InjectClick(apVp.x + apVp.width - 14.0f, apVp.y + apVp.height * 0.5f);
            for (int f = 0; f < 6; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 5);
            }
            std::vector<aero::VisualNodeId> popItems;
            const aero::VisualNodeId visPopId = findVisibleDropdownPopup(popItems);
            Require(visPopId.IsValid() && popItems.size() >= 5u,
                "Gallery ThemeAccentPicker failed to open dropdown popup");
            if (visPopId.IsValid()) {
                const aero::VisualNodeId ddId = FindDescendantByOwnerName(host, visPopId, "DropDownBorder");
                const aero::VisualNodeId sbId = FindDescendantByOwnerName(host, visPopId, "PART_VerticalScrollBar");
                const aero::VisualNode* ddNode = ddId.IsValid() ? host.World().GetNode(ddId) : nullptr;
                const aero::VisualNode* sbNode = sbId.IsValid() ? host.World().GetNode(sbId) : nullptr;
                Require(ddNode && sbNode && sbNode->layout.visibility == aero::Visibility::Visible,
                    "Gallery ThemeAccentPicker popup must have visible DropDownBorder and PART_VerticalScrollBar");
                if (ddNode && sbNode) {
                    const aero::Rect ddVp = NodeViewportBounds(*ddNode);
                    const aero::Rect sbVp = NodeViewportBounds(*sbNode);
                    const float rightGap = std::fabs((ddVp.x + ddVp.width) - (sbVp.x + sbVp.width));
                    std::cout << "[DIAG][ThemeAccentPicker] ddVp=(" << ddVp.x << "," << ddVp.y << " " << ddVp.width << "x" << ddVp.height
                              << ") sbVp=(" << sbVp.x << "," << sbVp.y << " " << sbVp.width << "x" << sbVp.height
                              << ") rightGap=" << rightGap << "\n";
                    Require(sbVp.width > 5.0f && sbVp.width <= 24.0f && rightGap <= 3.0f,
                        "Gallery ThemeAccentPicker PART_VerticalScrollBar must sit flush on right edge (rightGap=" +
                        std::to_string(rightGap) + ", sbWidth=" + std::to_string(sbVp.width) + ")");
                    const aero::VisualNodeId thumbId = FindDescendantByOwnerName(host, sbId, "Thumb");
                    const aero::VisualNode* thumbNode = thumbId.IsValid() ? host.World().GetNode(thumbId) : nullptr;
                    Require(thumbNode != nullptr, "Gallery ThemeAccentPicker missing Thumb inside PART_VerticalScrollBar");
                    if (thumbNode) {
                        const aero::Rect thumbVp = NodeViewportBounds(*thumbNode);
                        const float thumbRightGap = (ddVp.x + ddVp.width) - (thumbVp.x + thumbVp.width);
                        Require(thumbRightGap >= 0.0f && thumbRightGap <= 12.0f,
                            "Gallery ThemeAccentPicker scrollbar Thumb must stay aligned near right edge (thumbRightGap=" +
                            std::to_string(thumbRightGap) + ")");
                    }
                }
            }
            host.InjectKey(aero::KeyCode::Escape, true);
            host.InjectKey(aero::KeyCode::Escape, false);
            for (int f = 0; f < 4; ++f) {
                host.StepFrame(1.0f / 60.0f, f == 3);
            }
        }
        host.InjectResize(1280u, 800u);
        for (int f = 0; f < 4; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 3);
        }
    }

    // 5c. ToggleButton sub-page: EnableContent has no blue leak, custom switch renders gradient shadows & smooth VSM transitions
    {
        const aero::VisualNodeId cid = loadSample("ToggleButton");
        if (cid.IsValid()) {
            const aero::VisualNodeId enableId = FindDescendantByOwnerName(host, cid, "EnableContent");
            const aero::VisualNode* enableNode = enableId.IsValid() ? host.World().GetNode(enableId) : nullptr;
            Require(enableNode && (enableNode->box.bgRgba >> 24u) == 0u && enableNode->box.bgGradId <= 0,
                "Gallery/ToggleButton EnableContent ToggleSwitch must not leak #FF3B82F6 Background");

            std::vector<aero::VisualNodeId> tbs;
            CollectDescendantsByTypeName(host, cid, "ToggleButton", tbs);
            aero::VisualNodeId customTbId;
            aero::VisualNodeId switchSpaceId;
            aero::VisualNodeId offGroupId;
            for (aero::VisualNodeId tbId : tbs) {
                const aero::VisualNodeId ssId = FindDescendantByOwnerName(host, tbId, "SwitchSpace");
                const aero::VisualNodeId ogId = FindDescendantByOwnerName(host, tbId, "OffGroup");
                if (ssId.IsValid() && ogId.IsValid()) {
                    customTbId = tbId;
                    switchSpaceId = ssId;
                    offGroupId = ogId;
                    break;
                }
            }
            Require(customTbId.IsValid() && switchSpaceId.IsValid() && offGroupId.IsValid(),
                "Gallery/ToggleButton missing custom switch ToggleButton with SwitchSpace and OffGroup");
            {
                // Verify indicator Rectangles (CheckedRect1 over DodgerBlue, UncheckedRect1 over OrangeRed)
                // do not leak a 1px colored seam at their right edge before scrolling away from the top sections.
                host.StepFrame(1.0f / 60.0f, true);
                std::vector<std::uint8_t> topRgba;
                Require(host.RenderHeadlessRgba(1280u, 800u, topRgba), "Gallery/ToggleButton top render failed");
                const aero::VisualNodeId chkRectId = FindDescendantByOwnerName(host, cid, "CheckedRect1");
                const aero::VisualNodeId unchkRectId = FindDescendantByOwnerName(host, cid, "UncheckedRect1");
                const aero::VisualNode* chkRectNode = chkRectId.IsValid() ? host.World().GetNode(chkRectId) : nullptr;
                const aero::VisualNode* unchkRectNode = unchkRectId.IsValid() ? host.World().GetNode(unchkRectId) : nullptr;
                Require(chkRectNode && unchkRectNode, "Gallery/ToggleButton missing CheckedRect1 / UncheckedRect1");
                if (chkRectNode && unchkRectNode) {
                    const aero::Rect cr1 = NodeViewportBounds(*chkRectNode);
                    const aero::Rect ur1 = NodeViewportBounds(*unchkRectNode);
                    const int cy1 = std::clamp(static_cast<int>(cr1.y + cr1.height * 0.5f), 0, 799);
                    const int xMin1 = std::max(0, static_cast<int>(std::floor(cr1.x)) - 2);
                    const int xMax1 = std::min(1279, static_cast<int>(std::ceil(cr1.x + cr1.width)) + 2);
                    for (int px = xMin1; px <= xMax1; ++px) {
                        const std::size_t off = (static_cast<std::size_t>(cy1) * 1280u + static_cast<std::size_t>(px)) * 4u;
                        const bool isDodgerSeam = topRgba[off] < 80u && topRgba[off + 1u] > 100u && topRgba[off + 2u] > 200u;
                        Require(!isDodgerSeam,
                            "Gallery/ToggleButton CheckedRect1 leaked DodgerBlue seam pixel at x=" + std::to_string(px));
                    }
                    const int cy2 = std::clamp(static_cast<int>(ur1.y + ur1.height * 0.5f), 0, 799);
                    const int xMin2 = std::max(0, static_cast<int>(std::floor(ur1.x)) - 2);
                    const int xMax2 = std::min(1279, static_cast<int>(std::ceil(ur1.x + ur1.width)) + 2);
                    for (int px = xMin2; px <= xMax2; ++px) {
                        const std::size_t off = (static_cast<std::size_t>(cy2) * 1280u + static_cast<std::size_t>(px)) * 4u;
                        const bool isOrangeRedSeam = topRgba[off] > 220u && topRgba[off + 1u] < 110u && topRgba[off + 2u] < 60u;
                        Require(!isOrangeRedSeam,
                            "Gallery/ToggleButton UncheckedRect1 leaked OrangeRed seam pixel at x=" + std::to_string(px));
                    }
                }
            }
            if (customTbId.IsValid()) {
                scrollActiveSampleTo(cid, customTbId);
                host.StepFrame(1.0f / 60.0f, true);

                // Verify gradient-border shadow nodes inside custom ToggleButton emit GradientRoundedRect
                std::unordered_set<const void*> borderGradNodes;
                {
                    std::vector<aero::VisualNodeId> bds;
                    CollectDescendantsByTypeName(host, customTbId, "Border", bds);
                    for (aero::VisualNodeId bid : bds) {
                        const aero::VisualNode* bn = host.World().GetNode(bid);
                        if (bn && bn->box.borderGradId > 0) {
                            borderGradNodes.insert(bn);
                        }
                    }
                }
                Require(borderGradNodes.size() >= 3u,
                    "Gallery/ToggleButton custom switch expected >= 3 gradient-border shadow Borders");
                std::size_t gradRectShadowCount = 0u;
                const std::size_t rCount = host.RenderTree().NodeCount();
                for (std::size_t ri = 0u; ri < rCount; ++ri) {
                    const aero::RenderNodeDescriptor* rd = host.RenderTree().GetNode(static_cast<std::uint32_t>(ri));
                    if (rd && rd->sourceElement != nullptr && borderGradNodes.count(rd->sourceElement) > 0u) {
                        Require(rd->primType == aero::AeroPrimType::GradientRoundedRect,
                            "Gallery/ToggleButton shadow Border with LinearGradientBrush BorderBrush must emit GradientRoundedRect");
                        ++gradRectShadowCount;
                    }
                }
                Require(gradRectShadowCount >= 3u,
                    "Gallery/ToggleButton expected >= 3 GradientRoundedRect shadow render nodes (got " +
                    std::to_string(gradRectShadowCount) + ")");

                auto readSwitchState = [&](float& outSx, float& outOffOpacity, bool& outChecked) {
                    const aero::VisualNode* ssN = host.World().GetNode(switchSpaceId);
                    const aero::VisualNode* ogN = host.World().GetNode(offGroupId);
                    const aero::VisualNode* tbN = host.World().GetNode(customTbId);
                    outSx = ssN ? ssN->layout.layoutTransform2D.sx : -1.0f;
                    outOffOpacity = ogN ? ogN->layout.opacity : -1.0f;
                    outChecked = false;
                    if (tbN && tbN->scriptOwner.valid()) {
                        (void)ReadBoolMember(host, tbN->scriptOwner, "IsChecked", outChecked);
                    }
                };

                float sx0 = 0.0f, op0 = 0.0f;
                bool chk0 = true;
                readSwitchState(sx0, op0, chk0);
                Require(!chk0 && sx0 < 0.05f && op0 > 0.95f,
                    "Gallery/ToggleButton custom switch should start Unchecked (sx=" +
                    std::to_string(sx0) + ", offOp=" + std::to_string(op0) + ")");

                const aero::Rect tbVp = NodeViewportBounds(*host.World().GetNode(customTbId));
                host.InjectClick(tbVp.x + tbVp.width * 0.5f, tbVp.y + tbVp.height * 0.5f);
                for (int f = 0; f < 9; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 8);
                }
                float sxMid = 0.0f, opMid = 0.0f;
                bool chkMid = false;
                readSwitchState(sxMid, opMid, chkMid);
                std::cout << "[DIAG][ToggleButton][CheckMid] checked=" << (chkMid ? 1 : 0)
                          << " sx=" << sxMid << " offOpacity=" << opMid << "\n";
                Require(chkMid && sxMid > 0.05f && sxMid < 0.95f && opMid > 0.05f && opMid < 0.95f,
                    "Gallery/ToggleButton custom switch Check transition must animate smoothly (sx=" +
                    std::to_string(sxMid) + ", offOp=" + std::to_string(opMid) + ")");

                for (int f = 0; f < 30; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 29);
                }
                float sxEnd = 0.0f, opEnd = 0.0f;
                bool chkEnd = false;
                readSwitchState(sxEnd, opEnd, chkEnd);
                Require(chkEnd && sxEnd > 0.95f && opEnd < 0.05f,
                    "Gallery/ToggleButton custom switch should finish Checked (sx=" +
                    std::to_string(sxEnd) + ", offOp=" + std::to_string(opEnd) + ")");

                // Pixel verification of thumb right & bottom gradient shadows (must fade smoothly, never solid #CC/#A5 bars)
                {
                    std::vector<std::uint8_t> swRgba;
                    Require(host.RenderHeadlessRgba(1280u, 800u, swRgba), "Gallery/ToggleButton switch render failed");
                    const aero::VisualNodeId thumbBorderId = FindDescendantByOwnerName(host, customTbId, "border");
                    const aero::VisualNode* thumbBorderNode = thumbBorderId.IsValid() ? host.World().GetNode(thumbBorderId) : nullptr;
                    Require(thumbBorderNode != nullptr, "Gallery/ToggleButton missing SwitchThumb border");
                    if (thumbBorderNode) {
                        const aero::Rect tvp = NodeViewportBounds(*thumbBorderNode);
                        const int rx = std::clamp(static_cast<int>(std::ceil(tvp.x + tvp.width)), 0, 1279);
                        const int ry = std::clamp(static_cast<int>(tvp.y + tvp.height * 0.5f), 0, 799);
                        const std::size_t roff = (static_cast<std::size_t>(ry) * 1280u + static_cast<std::size_t>(rx)) * 4u;
                        Require(swRgba[roff] >= 100u,
                            "Gallery/ToggleButton SwitchThumb right shadow rendered solid dark bar (R=" +
                            std::to_string(swRgba[roff]) + ")");

                        const int bx = std::clamp(static_cast<int>(tvp.x + tvp.width * 0.5f), 0, 1279);
                        const int by0 = std::clamp(static_cast<int>(std::floor(tvp.y + tvp.height)) + 1, 0, 797);
                        const std::uint8_t r0 = swRgba[(static_cast<std::size_t>(by0 + 0) * 1280u + static_cast<std::size_t>(bx)) * 4u];
                        const std::uint8_t r2 = swRgba[(static_cast<std::size_t>(by0 + 2) * 1280u + static_cast<std::size_t>(bx)) * 4u];
                        Require(r2 > static_cast<std::uint8_t>(std::min<int>(255, static_cast<int>(r0) + 15)),
                            "Gallery/ToggleButton SwitchThumb bottom shadow must fade lighter vertically (r0=" +
                            std::to_string(r0) + ", r2=" + std::to_string(r2) + ")");
                    }
                }

                // Click again -> Unchecked transition
                host.InjectClick(tbVp.x + tbVp.width * 0.5f, tbVp.y + tbVp.height * 0.5f);
                for (int f = 0; f < 12; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 11);
                }
                float sxUnMid = 0.0f, opUnMid = 0.0f;
                bool chkUnMid = true;
                readSwitchState(sxUnMid, opUnMid, chkUnMid);
                std::cout << "[DIAG][ToggleButton][UncheckMid] checked=" << (chkUnMid ? 1 : 0)
                          << " sx=" << sxUnMid << " offOpacity=" << opUnMid << "\n";
                Require(!chkUnMid && sxUnMid > 0.05f && sxUnMid < 0.95f && opUnMid > 0.05f && opUnMid < 0.95f,
                    "Gallery/ToggleButton custom switch Uncheck transition must animate smoothly (sx=" +
                    std::to_string(sxUnMid) + ", offOp=" + std::to_string(opUnMid) + ")");

                for (int f = 0; f < 30; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 29);
                }
                float sxUnEnd = 0.0f, opUnEnd = 0.0f;
                bool chkUnEnd = true;
                readSwitchState(sxUnEnd, opUnEnd, chkUnEnd);
                Require(!chkUnEnd && sxUnEnd < 0.05f && opUnEnd > 0.95f,
                    "Gallery/ToggleButton custom switch should finish Unchecked (sx=" +
                    std::to_string(sxUnEnd) + ", offOp=" + std::to_string(opUnEnd) + ")");
            }
        }
    }

    // 6. Menu sub-page: top-level MenuItems render with bounds & clicking "File" opens submenu popup
    {
        const aero::VisualNodeId cid = loadSample("Menu");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> menus;
            CollectDescendantsByTypeName(host, cid, "Menu", menus);
            std::cout << "[DIAG][Menu] menuCount=" << menus.size() << "\n";
            Require(!menus.empty(), "Gallery/Menu missing Menu controls");
            if (!menus.empty()) {
                std::int64_t ic = -1, cc = -1;
                readItemAndContainerCounts(menus[0], ic, cc);
                std::vector<aero::VisualNodeId> mis;
                CollectDescendantsByTypeName(host, menus[0], "MenuItem", mis);
                std::string mTxt;
                CollectSubtreeText(host.World(), menus[0], mTxt);
                const aero::VisualNode* m0Node = host.World().GetNode(menus[0]);
                const aero::Rect m0Vp = m0Node ? NodeViewportBounds(*m0Node) : aero::Rect{};
                std::cout << "[DIAG][Menu][0] itemCnt=" << ic << " contCnt=" << cc
                          << " menuItemNodes=" << mis.size()
                          << " vp=(" << m0Vp.x << "," << m0Vp.y << " " << m0Vp.width << "x" << m0Vp.height << ")"
                          << " text='" << mTxt.substr(0, 80) << "'\n";
                Require(ic == 3 && m0Vp.width > 50.0f && m0Vp.height > 12.0f &&
                        mTxt.find("File") != std::string::npos &&
                        mTxt.find("Edit") != std::string::npos &&
                        mTxt.find("Help") != std::string::npos,
                    "Gallery/Menu #0 should realize File/Edit/Help MenuItems with non-zero bounds");
                if (!mis.empty()) {
                    const aero::VisualNode* fileNode = host.World().GetNode(mis[0]);
                    const aero::Rect fVp = fileNode ? NodeViewportBounds(*fileNode) : aero::Rect{};
                    std::cout << "[DIAG][Menu][File] vp=(" << fVp.x << "," << fVp.y << " " << fVp.width << "x" << fVp.height << ")\n";
                    Require(fVp.width > 10.0f && fVp.height > 10.0f, "Gallery/Menu File MenuItem degenerate bounds");
                    host.InjectClick(fVp.x + fVp.width * 0.5f, fVp.y + fVp.height * 0.5f);
                    for (int f = 0; f < 6; ++f) {
                        host.StepFrame(1.0f / 60.0f, f == 5);
                    }
                    bool subOpen = false;
                    if (fileNode && fileNode->scriptOwner.valid()) {
                        (void)ReadBoolMember(host, fileNode->scriptOwner, "IsSubmenuOpen", subOpen);
                    }
                    std::cout << "[DIAG][Menu][File] IsSubmenuOpen=" << (subOpen ? 1 : 0) << "\n";
                    Require(subOpen, "Gallery/Menu clicking File should set IsSubmenuOpen=true");
                    host.InjectKey(aero::KeyCode::Escape, true);
                    host.InjectKey(aero::KeyCode::Escape, false);
                    host.StepFrame(1.0f / 60.0f, true);
                }
            }
            if (menus.size() >= 3u) {
                host.InjectResize(1280u, 735u);
                for (int f = 0; f < 3; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 2);
                }
                std::vector<aero::VisualNodeId> misLow;
                CollectDescendantsByTypeName(host, menus[2], "MenuItem", misLow);
                aero::VisualNodeId lowFileId = (!misLow.empty()) ? misLow[0] : aero::VisualNodeId{};
                if (lowFileId.IsValid()) {
                    const aero::Rect lowVp = NodeViewportBounds(*host.World().GetNode(lowFileId));
                    const float spaceBelow = 735.0f - (lowVp.y + lowVp.height);
                    std::cout << "[DIAG][Menu][BottomFile] vp=(" << lowVp.x << "," << lowVp.y
                              << " " << lowVp.width << "x" << lowVp.height << ") spaceBelow=" << spaceBelow << "\n";
                    Require(spaceBelow > 96.0f && spaceBelow < 145.0f,
                        "Gallery/Menu BottomFile test precondition requires 96 < spaceBelow < 145 (got " +
                        std::to_string(spaceBelow) + ")");
                    host.InjectClick(lowVp.x + lowVp.width * 0.5f, lowVp.y + lowVp.height * 0.5f);
                    // Frame 1: must open directly UPWARD on the very first rendered frame (no downward flash).
                    host.StepFrame(1.0f / 60.0f, true);
                    auto findVisibleOpenPopup = [&]() -> aero::VisualNodeId {
                        std::vector<aero::VisualNodeId> pops;
                        CollectDescendantsByTypeName(host, host.RootVisualNode(), "Popup", pops);
                        for (aero::VisualNodeId pid : pops) {
                            const aero::VisualNode* pn = host.World().GetNode(pid);
                            bool isOpen = false;
                            if (pn && pn->layout.visibility == aero::Visibility::Visible &&
                                pn->scriptOwner.valid() &&
                                ReadBoolMember(host, pn->scriptOwner, "IsOpen", isOpen) && isOpen) {
                                return pid;
                            }
                        }
                        return aero::VisualNodeId{};
                    };
                    const aero::VisualNodeId popF1Id = findVisibleOpenPopup();
                    const aero::VisualNode* popF1Node = popF1Id.IsValid() ? host.World().GetNode(popF1Id) : nullptr;
                    const aero::Rect popF1Vp = popF1Node ? NodeViewportBounds(*popF1Node) : aero::Rect{};
                    std::cout << "[DIAG][Menu][BottomPopup][f1] open=" << (popF1Id.IsValid() ? 1 : 0)
                              << " vp=(" << popF1Vp.x << "," << popF1Vp.y
                              << " " << popF1Vp.width << "x" << popF1Vp.height << ")\n";
                    Require(popF1Id.IsValid() && popF1Vp.height > spaceBelow &&
                            popF1Vp.y >= 0.0f && (popF1Vp.y + popF1Vp.height <= lowVp.y + 4.0f),
                        "Gallery/Menu near bottom must expand upward on Frame 1 (lowY=" +
                        std::to_string(lowVp.y) + ", popY=" + std::to_string(popF1Vp.y) +
                        ", popH=" + std::to_string(popF1Vp.height) + ")");
                    for (int f = 0; f < 3; ++f) {
                        host.StepFrame(1.0f / 60.0f, f == 2);
                    }
                    const aero::VisualNode* popF4Node = host.World().GetNode(popF1Id);
                    const aero::Rect popF4Vp = popF4Node ? NodeViewportBounds(*popF4Node) : aero::Rect{};
                    Require(std::fabs(popF4Vp.y - popF1Vp.y) < 1.0f,
                        "Gallery/Menu bottom popup position should stay stable across subsequent frames");
                    host.InjectKey(aero::KeyCode::Escape, true);
                    host.InjectKey(aero::KeyCode::Escape, false);
                    host.StepFrame(1.0f / 60.0f, true);
                }
                host.InjectResize(1280u, 800u);
                for (int f = 0; f < 3; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 2);
                }
            }
        }
    }

    // 7. ContextMenu sub-page: right-click popup position near cursor + Section 3 RemoveElementAction
    {
        const aero::VisualNodeId cid = loadSample("ContextMenu");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> borders;
            CollectDescendantsByTypeName(host, cid, "Border", borders);
            aero::VisualNodeId targetBorderId;
            for (aero::VisualNodeId bid : borders) {
                std::string bTxt;
                CollectSubtreeText(host.World(), bid, bTxt);
                if (bTxt.find("Right click to open Context Menu") != std::string::npos) {
                    const aero::VisualNode* bn = host.World().GetNode(bid);
                    if (bn && bn->layout.layoutRect.width > 100.0f && bn->layout.layoutRect.width < 500.0f) {
                        targetBorderId = bid;
                        break;
                    }
                }
            }
            Require(targetBorderId.IsValid(), "Gallery/ContextMenu target Border missing");
            if (targetBorderId.IsValid()) {
                scrollActiveSampleTo(cid, targetBorderId);
                const aero::VisualNode* tb = host.World().GetNode(targetBorderId);
                const aero::Rect tbVp = NodeViewportBounds(*tb);
                const float clickX = tbVp.x + tbVp.width * 0.5f;
                const float clickY = tbVp.y + tbVp.height * 0.5f;
                host.InjectClick(clickX, clickY, aero::PointerButton::Right);
                for (int f = 0; f < 6; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 5);
                }
                std::vector<aero::VisualNodeId> cms;
                CollectDescendantsByTypeName(host, host.RootVisualNode(), "ContextMenu", cms);
                aero::VisualNodeId openCmId;
                for (aero::VisualNodeId cmId : cms) {
                    const aero::VisualNode* cmNode = host.World().GetNode(cmId);
                    bool isOpen = false;
                    if (cmNode && cmNode->scriptOwner.valid() &&
                        ReadBoolMember(host, cmNode->scriptOwner, "IsOpen", isOpen) && isOpen) {
                        openCmId = cmId;
                        break;
                    }
                }
                const aero::VisualNode* openCmNode = openCmId.IsValid() ? host.World().GetNode(openCmId) : nullptr;
                const aero::Rect cmVp = openCmNode ? NodeViewportBounds(*openCmNode) : aero::Rect{};
                std::string cmTxt;
                if (openCmId.IsValid()) { CollectSubtreeText(host.World(), openCmId, cmTxt); }
                std::cout << "[DIAG][ContextMenu] click=(" << clickX << "," << clickY << ") open="
                          << (openCmId.IsValid() ? 1 : 0) << " cmVp=(" << cmVp.x << "," << cmVp.y
                          << " " << cmVp.width << "x" << cmVp.height << ") text='" << cmTxt << "'\n";
                Require(openCmId.IsValid() && cmVp.width > 40.0f && cmVp.height > 20.0f &&
                        std::fabs(cmVp.x - clickX) < 40.0f && std::fabs(cmVp.y - clickY) < 40.0f &&
                        cmTxt.find("Option 1") != std::string::npos,
                    "Gallery/ContextMenu right-click popup should open near click (x,y) without double offset");
                host.InjectKey(aero::KeyCode::Escape, true);
                host.InjectKey(aero::KeyCode::Escape, false);
                host.StepFrame(1.0f / 60.0f, true);
            }

            // Also check section 3 ListBox (9 items) and Remove Action
            std::vector<aero::VisualNodeId> lbs;
            CollectDescendantsByTypeName(host, cid, "ListBox", lbs);
            if (!lbs.empty()) {
                scrollActiveSampleTo(cid, lbs[0]);
                std::vector<aero::VisualNodeId> lbItems;
                CollectDescendantsByTypeName(host, lbs[0], "ListBoxItem", lbItems);
                std::cout << "[DIAG][ContextMenu][ListBox] itemCount=" << lbItems.size() << "\n";
                Require(lbItems.size() == 9u,
                    "Gallery/ContextMenu ListBox expected 9 ListBoxItems (got " + std::to_string(lbItems.size()) + ")");
            }
        }
    }

    // 8. Canvas sub-page: zIndex1 slider updates Canvas.ZIndex on child 1
    {
        const aero::VisualNodeId cid = loadSample("Canvas");
        if (cid.IsValid()) {
            const aero::VisualNodeId s1Id = FindDescendantByOwnerName(host, cid, "zIndex1");
            const aero::VisualNodeId s4Id = FindDescendantByOwnerName(host, cid, "zIndex4");
            std::vector<aero::VisualNodeId> canvases;
            CollectDescendantsByTypeName(host, cid, "Canvas", canvases);
            Require(s1Id.IsValid() && s4Id.IsValid() && !canvases.empty(), "Gallery/Canvas missing sliders or Canvas");
            if (s1Id.IsValid() && !canvases.empty()) {
                scrollActiveSampleTo(cid, s1Id);
                const aero::VisualNode* cNode = host.World().GetNode(canvases[0]);
                Require(cNode && cNode->children.size() >= 4u, "Gallery/Canvas expected 4 child Borders");
                if (cNode && cNode->children.size() >= 4u) {
                    const aero::VisualNodeId b1Id = cNode->children[0];
                    const aero::VisualNodeId b2Id = cNode->children[1];
                    const aero::VisualNodeId b3Id = cNode->children[2];
                    const aero::VisualNodeId b4Id = cNode->children[3];
                    auto zOf = [&](aero::VisualNodeId nid) -> std::int32_t {
                        const aero::VisualNode* n = host.World().GetNode(nid);
                        return n ? n->layout.zIndex : -999;
                    };
                    std::cout << "[DIAG][Canvas] initial zIndex=(" << zOf(b1Id) << "," << zOf(b2Id)
                              << "," << zOf(b3Id) << "," << zOf(b4Id) << ")\n";
                    Require(zOf(b4Id) == 3,
                        "Gallery/Canvas initial zIndex4 child should bind to 3 (got " + std::to_string(zOf(b4Id)) + ")");
                    std::vector<std::uint8_t> canvasBefore;
                    (void)host.RenderHeadlessRgba(1280u, 800u, canvasBefore);

                    const aero::VisualNode* s1Node = host.World().GetNode(s1Id);
                    const aero::Rect s1Vp = NodeViewportBounds(*s1Node);
                    const aero::VisualNodeId thumbId = FindDescendantByOwnerName(host, s1Id, "Thumb");
                    const aero::VisualNode* thumbNode = thumbId.IsValid() ? host.World().GetNode(thumbId) : nullptr;
                    if (thumbNode) {
                        const aero::Rect tVp = NodeViewportBounds(*thumbNode);
                        const float startX = tVp.x + tVp.width * 0.5f;
                        const float midY = tVp.y + tVp.height * 0.5f;
                        const float endX = s1Vp.x + s1Vp.width - 12.0f;
                        host.InjectPointerMove(startX, midY);
                        host.StepFrame(1.0f / 60.0f, false);
                        host.InjectPointerButton(startX, midY, aero::PointerButton::Left, true);
                        host.StepFrame(1.0f / 60.0f, false);
                        host.InjectPointerMove(endX, midY);
                        host.StepFrame(1.0f / 60.0f, false);
                        host.InjectPointerButton(endX, midY, aero::PointerButton::Left, false);
                    } else {
                        for (int ci = 0; ci < 5; ++ci) {
                            host.InjectClick(s1Vp.x + s1Vp.width - 20.0f, s1Vp.y + s1Vp.height * 0.5f);
                            host.StepFrame(1.0f / 60.0f, false);
                        }
                    }
                    for (int f = 0; f < 6; ++f) {
                        host.StepFrame(1.0f / 60.0f, f == 5);
                    }
                    float s1Val = 0.0f;
                    (void)ReadFloatMember(host, s1Node->scriptOwner, "Value", s1Val);
                    std::cout << "[DIAG][Canvas] after zIndex1 click val=" << s1Val
                              << " zIndex1Child=" << zOf(b1Id) << " zIndex4Child=" << zOf(b4Id) << "\n";
                    std::vector<std::uint8_t> canvasAfter;
                    (void)host.RenderHeadlessRgba(1280u, 800u, canvasAfter);
                    const std::size_t diff = CountChangedPixels(canvasBefore, canvasAfter);
                    std::cout << "[DIAG][Canvas] pixelDiff=" << diff << "\n";
                    Require(s1Val >= 4.0f && zOf(b1Id) >= 4 && zOf(b4Id) == 3 && diff > 200u,
                        "Gallery/Canvas moving zIndex1 slider must update b1 zIndex and change rendered pixels (val=" +
                        std::to_string(s1Val) + ", z1=" + std::to_string(zOf(b1Id)) + ", diff=" + std::to_string(diff) + ")");
                }
            }
        }
    }

    // 9. Expander sub-page: clicking collapsed Expander ("Audio Options" / "Down") expands it
    {
        const aero::VisualNodeId cid = loadSample("Expander");
        if (cid.IsValid()) {
            std::vector<aero::VisualNodeId> expanders;
            CollectDescendantsByTypeName(host, cid, "Expander", expanders);
            std::cout << "[DIAG][Expander] count=" << expanders.size() << "\n";
            Require(expanders.size() >= 6u, "Gallery/Expander expected >= 6 Expander controls");
            if (expanders.size() >= 2u) {
                for (std::size_t ei = 0u; ei < expanders.size(); ++ei) {
                    const aero::VisualNode* en = host.World().GetNode(expanders[ei]);
                    bool isExp = false;
                    (void)ReadBoolMember(host, en->scriptOwner, "IsExpanded", isExp);
                    const aero::VisualNodeId cbId = FindDescendantByOwnerName(host, expanders[ei], "ContentBorder");
                    const aero::VisualNode* cbNode = cbId.IsValid() ? host.World().GetNode(cbId) : nullptr;
                    std::cout << "[DIAG][Expander][" << ei << "] init IsExpanded=" << (isExp ? 1 : 0)
                              << " ContentBorderVis=" << (cbNode ? static_cast<int>(cbNode->layout.visibility) : -1) << "\n";
                    if (ei == 0u) {
                        Require(isExp && cbNode && cbNode->layout.visibility == aero::Visibility::Visible,
                            "Gallery/Expander[0] should start expanded with Visible ContentBorder");
                    }
                }
                auto readExpanderMetrics = [&](aero::VisualNodeId expId, bool& outExp, int& outVis,
                                               float& outSx, float& outSy, float& outWm00, float& outWm11) {
                    const aero::VisualNode* en = host.World().GetNode(expId);
                    const aero::VisualNodeId cbId = FindDescendantByOwnerName(host, expId, "ContentBorder");
                    const aero::VisualNodeId cbgId = FindDescendantByOwnerName(host, expId, "ContentBg");
                    const aero::VisualNode* cbNode = cbId.IsValid() ? host.World().GetNode(cbId) : nullptr;
                    const aero::VisualNode* cbgNode = cbgId.IsValid() ? host.World().GetNode(cbgId) : nullptr;
                    outExp = false;
                    if (en && en->scriptOwner.valid()) {
                        (void)ReadBoolMember(host, en->scriptOwner, "IsExpanded", outExp);
                    }
                    outVis = cbNode ? static_cast<int>(cbNode->layout.visibility) : -1;
                    outSx = cbgNode ? cbgNode->transform2D.sx : -1.0f;
                    outSy = cbgNode ? cbgNode->transform2D.sy : -1.0f;
                    outWm00 = cbgNode ? cbgNode->layout.worldTransform.m00 : -1.0f;
                    outWm11 = cbgNode ? cbgNode->layout.worldTransform.m11 : -1.0f;
                };
                auto clickExpander = [&](aero::VisualNodeId expId) {
                    scrollActiveSampleTo(cid, expId);
                    const aero::VisualNodeId hdrBtnId = FindDescendantByOwnerName(host, expId, "ExpanderButton");
                    const aero::VisualNodeId clickId = hdrBtnId.IsValid() ? hdrBtnId : expId;
                    const aero::VisualNode* clickNode = host.World().GetNode(clickId);
                    const aero::Rect cVp = NodeViewportBounds(*clickNode);
                    host.InjectClick(cVp.x + cVp.width * 0.5f, cVp.y + cVp.height * 0.5f);
                };

                // 9a. Expand Audio Options (expanders[1]): no frame-0 full-scale flash, finishes at sy == 1.
                const aero::VisualNodeId audioExpId = expanders[1];
                clickExpander(audioExpId);
                host.StepFrame(0.0f, true);
                bool aExp0 = false;
                int aVis0 = -1;
                float aSx0 = 0.0f, aSy0 = 0.0f, aW00 = 0.0f, aW11 = 0.0f;
                readExpanderMetrics(audioExpId, aExp0, aVis0, aSx0, aSy0, aW00, aW11);
                Require(aExp0 && aVis0 == 0 && aSy0 < 0.35f && aW11 < 0.35f,
                    "Gallery/Expander Audio Options must start expand smoothly without frame-0 flash (sy=" +
                    std::to_string(aSy0) + ", m11=" + std::to_string(aW11) + ")");
                for (int f = 1; f < 25; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 24);
                }
                bool audioExp = false;
                int aVis24 = -1;
                float aSx24 = 0.0f, aSy24 = 0.0f, aW2400 = 0.0f, aW2411 = 0.0f;
                readExpanderMetrics(audioExpId, audioExp, aVis24, aSx24, aSy24, aW2400, aW2411);
                std::cout << "[DIAG][Expander][1] after click IsExpanded=" << (audioExp ? 1 : 0)
                          << " ContentBorderVis=" << aVis24
                          << " ContentBg.sy=" << aSy24 << " m11=" << aW2411 << "\n";
                Require(audioExp && aVis24 == 0 && aSy24 > 0.95f && aW2411 > 0.95f,
                    "Gallery/Expander clicking Audio Options should finish expanded (sy=" +
                    std::to_string(aSy24) + ")");

                // 9b. Clicking Down (expanders[2]) must keep Audio Options expanded & visible on frame 0 and after.
                const aero::VisualNodeId downExpId = expanders[2];
                clickExpander(downExpId);
                for (int f = 0; f < 25; ++f) {
                    host.StepFrame(f == 0 ? 0.0f : (1.0f / 60.0f), f == 0 || f == 24);
                    if (f == 0 || f == 24) {
                        bool dExp = false, prevExp = false;
                        int dVis = -1, prevVis = -1;
                        float dSx = 0.0f, dSy = 0.0f, dW00 = 0.0f, dW11 = 0.0f;
                        float prevSx = 0.0f, prevSy = 0.0f, prevW00 = 0.0f, prevW11 = 0.0f;
                        readExpanderMetrics(downExpId, dExp, dVis, dSx, dSy, dW00, dW11);
                        readExpanderMetrics(audioExpId, prevExp, prevVis, prevSx, prevSy, prevW00, prevW11);
                        if (f == 0) {
                            Require(dExp && dVis == 0 && dSy < 0.35f && dW11 < 0.35f,
                                "Gallery/Expander Down must start expand smoothly without frame-0 flash");
                        } else {
                            Require(dExp && dVis == 0 && dSy > 0.95f && dW11 > 0.95f,
                                "Gallery/Expander Down should finish expanded");
                        }
                        Require(prevExp && prevVis == 0 && prevSy > 0.95f && prevW11 > 0.95f,
                            "Gallery/Expander Audio Options must remain visible when clicking Down (f=" +
                            std::to_string(f) + ", sy=" + std::to_string(prevSy) +
                            ", m11=" + std::to_string(prevW11) + ")");
                    }
                }

                // 9c. Horizontal expanders Right (expanders[4]) and Left (expanders[5]) preserve prior expanders too.
                const aero::VisualNodeId rightExpId = expanders[4];
                const aero::VisualNodeId leftExpId = expanders[5];
                clickExpander(rightExpId);
                for (int f = 0; f < 25; ++f) {
                    host.StepFrame(f == 0 ? 0.0f : (1.0f / 60.0f), f == 0 || f == 24);
                }
                {
                    bool dExp = false, rExp = false;
                    int dVis = -1, rVis = -1;
                    float dSx = 0.0f, dSy = 0.0f, dW00 = 0.0f, dW11 = 0.0f;
                    float rSx = 0.0f, rSy = 0.0f, rW00 = 0.0f, rW11 = 0.0f;
                    readExpanderMetrics(downExpId, dExp, dVis, dSx, dSy, dW00, dW11);
                    readExpanderMetrics(rightExpId, rExp, rVis, rSx, rSy, rW00, rW11);
                    Require(dExp && dVis == 0 && dSy > 0.95f && rExp && rVis == 0 && rSx > 0.95f && rSy > 0.95f,
                        "Gallery/Expander Right should expand while Down stays visible");
                }
                clickExpander(leftExpId);
                for (int f = 0; f < 25; ++f) {
                    host.StepFrame(f == 0 ? 0.0f : (1.0f / 60.0f), f == 0 || f == 24);
                }
                {
                    bool rExp = false, lExp = false;
                    int rVis = -1, lVis = -1;
                    float rSx = 0.0f, rSy = 0.0f, rW00 = 0.0f, rW11 = 0.0f;
                    float lSx = 0.0f, lSy = 0.0f, lW00 = 0.0f, lW11 = 0.0f;
                    readExpanderMetrics(rightExpId, rExp, rVis, rSx, rSy, rW00, rW11);
                    readExpanderMetrics(leftExpId, lExp, lVis, lSx, lSy, lW00, lW11);
                    Require(rExp && rVis == 0 && rSx > 0.95f && rW00 > 0.95f &&
                            lExp && lVis == 0 && lSx > 0.95f && lSy > 0.95f,
                        "Gallery/Expander Right must remain visible when clicking Left (rSx=" +
                        std::to_string(rSx) + ", lSx=" + std::to_string(lSx) + ")");
                }

                // 9d. Collapse and re-expand Video Options (expanders[0]): must NOT flash full-scale on frame 0!
                const aero::VisualNodeId videoExpId = expanders[0];
                clickExpander(videoExpId);
                for (int f = 0; f < 25; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 24);
                }
                {
                    bool vExp = true;
                    int vVis = -1;
                    float vSx = 0.0f, vSy = 0.0f, vW00 = 0.0f, vW11 = 0.0f;
                    readExpanderMetrics(videoExpId, vExp, vVis, vSx, vSy, vW00, vW11);
                    Require(!vExp && vVis == 2 && vSy < 0.05f,
                        "Gallery/Expander Video Options should collapse cleanly");
                }
                clickExpander(videoExpId);
                host.StepFrame(0.0f, true);
                {
                    bool vExp0 = false;
                    int vVis0 = -1;
                    float vSx0 = 0.0f, vSy0 = 0.0f, vW00 = 0.0f, vW11 = 0.0f;
                    readExpanderMetrics(videoExpId, vExp0, vVis0, vSx0, vSy0, vW00, vW11);
                    std::cout << "[DIAG][Expander][0] re-expand f0 vis=" << vVis0
                              << " sy=" << vSy0 << " m11=" << vW11 << "\n";
                    Require(vExp0 && vVis0 == 0 && vSy0 < 0.35f && vW11 < 0.35f,
                        "Gallery/Expander Video Options re-expand must NOT flash sy=1 on frame 0 (sy=" +
                        std::to_string(vSy0) + ", m11=" + std::to_string(vW11) + ")");
                }
                for (int f = 1; f < 25; ++f) {
                    host.StepFrame(1.0f / 60.0f, f == 24);
                }
                {
                    bool vExp24 = false;
                    int vVis24 = -1;
                    float vSx24 = 0.0f, vSy24 = 0.0f, vW2400 = 0.0f, vW2411 = 0.0f;
                    readExpanderMetrics(videoExpId, vExp24, vVis24, vSx24, vSy24, vW2400, vW2411);
                    Require(vExp24 && vVis24 == 0 && vSy24 > 0.95f && vW2411 > 0.95f,
                        "Gallery/Expander Video Options re-expand must finish expanded (sy=" +
                        std::to_string(vSy24) + ")");
                }
            }
        }
    }

    // 10. Memory stability loop: 2 full rounds across all 9 sub-pages (18 switches)
    {
        const std::array<const char*, 9> kPages = {
            "ComboBox", "ListBox", "ListView", "TreeView", "TabControl",
            "Menu", "ContextMenu", "Canvas", "Expander"
        };
        std::size_t round1EndLiveNodes = 0u, round2EndLiveNodes = 0u;
        std::size_t round1EndBrushes = 0u, round2EndBrushes = 0u;
        std::size_t round1EndHeapObjs = 0u, round2EndHeapObjs = 0u;
        std::size_t round1EndHeapBytes = 0u, round2EndHeapBytes = 0u;
        double round1EndPrivMb = 0.0, round2EndPrivMb = 0.0;

        for (int round = 0; round < 2; ++round) {
            for (const char* pg : kPages) {
                (void)loadSample(pg, 10);
                // Force a GC cycle via collectFull? Only use step frames (host's real incremental GC + dispose hooks)
                // plus print live heap stats.
                double privMb = 0.0, workMb = 0.0;
                readProcMemMb(privMb, workMb);
                const std::size_t liveNodes = host.World().LiveNodeCount();
                const std::size_t renderNodes = host.RenderTree().NodeCount();
                const std::size_t storeBrushes = host.ResourceStore() ? host.ResourceStore()->GradientBrushes().BrushCount() : 0u;
                const std::size_t defBrushes = aero::GradientBrushRegistry::Instance().BrushCount();
                const std::size_t heapObjs = host.Domains().uiHeap().liveObjects();
                const std::size_t heapBytes = host.Domains().uiHeap().liveBytes();
                std::cout << "[MEM][R" << (round + 1) << "][" << pg << "] nodes=" << liveNodes
                          << " render=" << renderNodes
                          << " brushes=" << storeBrushes << "/" << defBrushes
                          << " uiObjs=" << heapObjs << " uiKB=" << (heapBytes / 1024u)
                          << " privMB=" << std::fixed << std::setprecision(1) << privMb
                          << " wsMB=" << workMb << "\n";
                round2EndLiveNodes = liveNodes;
                round2EndBrushes = storeBrushes;
                round2EndHeapObjs = heapObjs;
                round2EndHeapBytes = heapBytes;
                round2EndPrivMb = privMb;
            }
            if (round == 0) {
                round1EndLiveNodes = round2EndLiveNodes;
                round1EndBrushes = round2EndBrushes;
                round1EndHeapObjs = round2EndHeapObjs;
                round1EndHeapBytes = round2EndHeapBytes;
                round1EndPrivMb = round2EndPrivMb;
            }
        }
        std::cout << "[MEM][SUMMARY] R1->R2 delta nodes="
                  << static_cast<long long>(round2EndLiveNodes) - static_cast<long long>(round1EndLiveNodes)
                  << " brushes=" << static_cast<long long>(round2EndBrushes) - static_cast<long long>(round1EndBrushes)
                  << " uiObjs=" << static_cast<long long>(round2EndHeapObjs) - static_cast<long long>(round1EndHeapObjs)
                  << " uiKB=" << (static_cast<long long>(round2EndHeapBytes) - static_cast<long long>(round1EndHeapBytes)) / 1024LL
                  << " privMB=" << (round2EndPrivMb - round1EndPrivMb) << "\n";
        Require(round2EndLiveNodes <= round1EndLiveNodes + 40u,
            "Gallery page switching should not leak VisualNodes across rounds (R1=" +
            std::to_string(round1EndLiveNodes) + ", R2=" + std::to_string(round2EndLiveNodes) + ")");
        Require(round2EndBrushes <= round1EndBrushes + 16u,
            "Gallery page switching should not leak cloned GradientBrushes across rounds (R1=" +
            std::to_string(round1EndBrushes) + ", R2=" + std::to_string(round2EndBrushes) + ")");
        Require(round2EndHeapObjs <= round1EndHeapObjs + 500u &&
                round2EndPrivMb - round1EndPrivMb < 15.0,
            "Gallery page switching should not monotonically leak managed objects or process memory across rounds");
    }
}

void TestInteractiveRive(aero::rs_comp::CompilerSession&) {
    std::cout << "[Interactive] Rive...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Rive", nullptr, ci, hc, 1280u, 720u),
        "Rive discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Rive load failed: " + err);

    for (int f = 0; f < 90; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 89);
    }

    const auto findFirstImageDescendant = [&](aero::VisualNodeId rootId) -> aero::VisualNodeId {
        const aero::VisualWorld& world = host.World();
        std::vector<aero::VisualNodeId> q{rootId};
        for (std::size_t i = 0u; i < q.size(); ++i) {
            const aero::VisualNode* n = world.GetNode(q[i]);
            if (!n) { continue; }
            if (q[i] != rootId && n->kind == aero::VisualKind::Image) {
                return q[i];
            }
            for (aero::VisualNodeId cid : n->children) {
                q.push_back(cid);
            }
        }
        return aero::VisualNodeId{};
    };

    const aero::VisualNodeId logoId = host.FindVisualByOwnerName("logo");
    Require(logoId.IsValid(), "Rive logo element missing");
    const aero::VisualNodeId logoImgId = findFirstImageDescendant(logoId);
    Require(logoImgId.IsValid(), "Rive logo internal Image node missing");
    const aero::VisualNode* logoImg = host.World().GetNode(logoImgId);
    Require(logoImg != nullptr, "Rive logo Image node resolved to null");
    const std::string uriBeforeZero = logoImg->Image().uri;
    Require(uriBeforeZero.rfind("rive://", 0) == 0u,
        "Rive logo Image URI should use rive:// scheme (got: '" + uriBeforeZero + "')");

    // dt == 0.0f layout/render sync must not advance Rive playback time
    host.StepFrame(0.0f, true);
    logoImg = host.World().GetNode(logoImgId);
    Require(logoImg && logoImg->Image().uri == uriBeforeZero,
        "Rive dt == 0.0f StepFrame must not advance animation URI");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(1280u, 720u, frame0), "Rive frame0 render failed");
    for (int f = 0; f < 30; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 29);
    }
    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(1280u, 720u, frame1), "Rive frame1 render failed");
    logoImg = host.World().GetNode(logoImgId);
    Require(logoImg && logoImg->Image().uri != uriBeforeZero,
        "Rive logo Image URI should advance over 0.5s");
    Require(CountChangedPixels(frame0, frame1) > 50u,
        "Rive animation frames should change rendered pixels over 0.5s");

    const aero::VisualNodeId bgId = host.FindVisualByOwnerName("background");
    Require(bgId.IsValid(), "Rive background element missing");
    const aero::VisualNodeId bgImgId = findFirstImageDescendant(bgId);
    Require(bgImgId.IsValid(), "Rive background internal Image node missing");

    aero::VisualNodeId startId{};
    aero::VisualNodeId rewardsId{};
    float startCx = 0.0f, startCy = 0.0f;
    float rewardsCx = 0.0f, rewardsCy = 0.0f;
    Require(RequireNodeCenter(host, "start", startCx, startCy, &startId), "Rive start button missing");
    Require(RequireNodeCenter(host, "rewards", rewardsCx, rewardsCy, &rewardsId), "Rive rewards button missing");

    // Hover & click start button -> must hit through transparent corner leaves overlay, focus start, and set States=1
    host.InjectPointerMove(startCx, startCy);
    host.StepFrame(1.0f / 60.0f, false);
    host.InjectClick(startCx, startCy);
    host.StepFrame(1.0f / 60.0f, true);

    bool startFocused = false;
    const aero::VisualNode* startNode = host.World().GetNode(startId);
    const bool readStartFocusOk = startNode && startNode->hasScriptOwner &&
        ReadBoolMember(host, startNode->scriptOwner, "IsFocused", startFocused);
    Require(readStartFocusOk && startFocused,
        "Rive clicking start button through transparent leaves overlay should set IsFocused=true");
    const aero::VisualNode* bgImg = host.World().GetNode(bgImgId);
    Require(bgImg && bgImg->Image().uri.find("States=1") != std::string::npos,
        "Rive focusing start should drive background RiveInput States=1 (got: '" +
            (bgImg ? bgImg->Image().uri : std::string("<null>")) + "')");

    // Hover rewards button -> MouseEnter SetFocusAction sets focus and switches background States=2
    host.InjectPointerMove(rewardsCx, rewardsCy);
    host.StepFrame(1.0f / 60.0f, true);
    bool rewardsFocused = false;
    const aero::VisualNode* rewardsNode = host.World().GetNode(rewardsId);
    const bool readRewardsFocusOk = rewardsNode && rewardsNode->hasScriptOwner &&
        ReadBoolMember(host, rewardsNode->scriptOwner, "IsFocused", rewardsFocused);
    Require(readRewardsFocusOk && rewardsFocused,
        "Rive hovering rewards button should set IsFocused=true");
    bgImg = host.World().GetNode(bgImgId);
    Require(bgImg && bgImg->Image().uri.find("States=2") != std::string::npos,
        "Rive hovering rewards should drive background RiveInput States=2 (got: '" +
            (bgImg ? bgImg->Image().uri : std::string("<null>")) + "')");
}

void TestInteractiveCustomRender(aero::rs_comp::CompilerSession&) {
    std::cout << "[Interactive] CustomRender...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "CustomRender", nullptr, ci, hc, 1280u, 720u),
        "CustomRender discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "CustomRender load failed: " + err);

    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }

    const aero::VisualNodeId gameId = host.FindVisualByOwnerName("GameControl");
    Require(gameId.IsValid(), "CustomRender GameControl missing");
    const aero::VisualNode* gameNode = host.World().GetNode(gameId);
    Require(gameNode && gameNode->hasScriptOwner, "CustomRender GameControl has no scriptOwner");

    bool isRunning0 = true;
    std::string status0;
    Require(ReadBoolMember(host, gameNode->scriptOwner, "IsRunning", isRunning0) && !isRunning0,
        "CustomRender GameControl.IsRunning should start false");
    Require(ReadStringMember(host, gameNode->scriptOwner, "StatusText", status0) && status0 == "READY",
        "CustomRender GameControl.StatusText should start 'READY' (got: '" + status0 + "')");

    std::vector<std::uint8_t> framePaused;
    Require(host.RenderHeadlessRgba(1280u, 720u, framePaused), "CustomRender paused render failed");

    const auto readBallY = [&]() -> float {
        const aero::DrawingCommandList* list = host.DrawBuffer().FindCommands(gameId);
        Require(list != nullptr, "CustomRender GameControl missing drawing commands");
        if (!list) { return 0.0f; }
        for (auto it = list->commands.rbegin(); it != list->commands.rend(); ++it) {
            if (it->kind == aero::DrawCommandKind::FillEllipse) {
                return it->rect.y;
            }
        }
        Require(false, "CustomRender GameControl FillEllipse command for ball not found");
        return 0.0f;
    };

    const float ballY0 = readBallY();
    host.StepFrame(0.02f, true);
    Require(std::fabs(readBallY() - ballY0) < 1e-3f,
        "CustomRender ball should not move while paused");

    // Start via Space key with dt == 0.0f flush
    host.InjectKey(aero::KeyCode::Space, true);
    host.InjectKey(aero::KeyCode::Space, false);
    host.StepFrame(0.0f, true);

    bool isRunning1 = false;
    std::string status1 = "<unread>";
    Require(ReadBoolMember(host, gameNode->scriptOwner, "IsRunning", isRunning1) && isRunning1,
        "CustomRender Space should set GameControl.IsRunning=true");
    Require(ReadStringMember(host, gameNode->scriptOwner, "StatusText", status1) && status1.empty(),
        "CustomRender Space should clear StatusText (got: '" + status1 + "')");

    const float ballYStart = readBallY();
    host.StepFrame(0.02f, true);
    const float ballDelta = readBallY() - ballYStart;
    // _ballVY = -260.0f -> single step of 0.02s moves ball by -5.2f (not -10.4f or 0.0f)
    Require(std::fabs(ballDelta - (-5.2f)) < 0.25f,
        "CustomRender single StepFrame(0.02f) should advance ball by ~-5.2px (got: " +
            std::to_string(ballDelta) + ")");

    std::vector<std::uint8_t> frameRunning;
    Require(host.RenderHeadlessRgba(1280u, 720u, frameRunning), "CustomRender running render failed");
    Require(CountChangedPixels(framePaused, frameRunning) > 50u,
        "CustomRender running frame should differ from paused frame");
}

void TestInteractiveBrushShaders(aero::rs_comp::CompilerSession&) {
    std::cout << "[Interactive] BrushShaders...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "BrushShaders", nullptr, ci, hc, 1280u, 720u),
        "BrushShaders discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "BrushShaders load failed: " + err);

    for (int f = 0; f < 5; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 4);
    }

    std::vector<aero::VisualNodeId> bdIds;
    CollectVisualNodesByOwnerName(host, "bd", bdIds);
    Require(bdIds.size() == 3u,
        "BrushShaders should realize 3 templated 'bd' Path nodes (got: " + std::to_string(bdIds.size()) + ")");

    std::vector<float> seeds(3, 0.0f);
    std::vector<std::string> imgSrcs(3);
    for (std::size_t i = 0u; i < 3u; ++i) {
        const aero::VisualNode* bdNode = host.World().GetNode(bdIds[i]);
        Require(bdNode && bdNode->hasScriptOwner, "BrushShaders bd node missing scriptOwner");
        Require(ReadFloatMember(host, bdNode->scriptOwner, "Fill.PlasmaSeed", seeds[i]),
            "BrushShaders bd Fill.PlasmaSeed readable");
        Require(ReadStringMember(host, bdNode->scriptOwner, "Fill.ImageSource", imgSrcs[i]),
            "BrushShaders bd Fill.ImageSource readable");
        Require(imgSrcs[i].rfind("shader://plasma?", 0) == 0u,
            "BrushShaders bd Fill.ImageSource should start with 'shader://plasma?' (got: '" + imgSrcs[i] + "')");
        std::string viewportStr;
        std::string viewportUnitsStr;
        Require(ReadStringMember(host, bdNode->scriptOwner, "Fill.Viewport", viewportStr) &&
                viewportStr == "0,0,300,90",
            "BrushShaders bd Fill.Viewport should be '0,0,300,90' (got: '" + viewportStr + "')");
        Require(ReadStringMember(host, bdNode->scriptOwner, "Fill.ViewportUnits", viewportUnitsStr) &&
                viewportUnitsStr == "Absolute",
            "BrushShaders bd Fill.ViewportUnits should be 'Absolute' (got: '" + viewportUnitsStr + "')");
    }
    Require(std::fabs(seeds[0] - 100.0f) < 0.1f &&
            std::fabs(seeds[1] - 200.0f) < 0.1f &&
            std::fabs(seeds[2] - 300.0f) < 0.1f,
        "BrushShaders 3 button templates should receive distinct Custom.PlasmaSeed_Single values 100/200/300");
    Require(imgSrcs[0] != imgSrcs[1] && imgSrcs[1] != imgSrcs[2] && imgSrcs[0] != imgSrcs[2],
        "BrushShaders 3 buttons should produce distinct plasma shader URIs");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(1280u, 720u, frame0), "BrushShaders frame0 render failed");
    for (int f = 0; f < 30; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 29);
    }
    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(1280u, 720u, frame1), "BrushShaders frame1 render failed");

    const aero::VisualNode* bd0 = host.World().GetNode(bdIds[0]);
    float plasmaTime = 0.0f;
    Require(bd0 && bd0->hasScriptOwner && ReadFloatMember(host, bd0->scriptOwner, "Fill.PlasmaTime", plasmaTime) &&
            plasmaTime > 0.2f,
        "BrushShaders AnimNoise (100.0:0:0 -> 8640000) should advance Fill.PlasmaTime > 0.2f over 0.5s (got: " +
            std::to_string(plasmaTime) + ")");
    Require(CountChangedPixels(frame0, frame1) > 50u,
        "BrushShaders AnimNoise should change rendered pixels over 0.5s");

    // Verify no solid-black opaque artifacts inside any bd plasma chevron bounding box
    for (std::size_t i = 0u; i < bdIds.size(); ++i) {
        const aero::VisualNode* bdNode = host.World().GetNode(bdIds[i]);
        Require(bdNode != nullptr, "BrushShaders bd node missing during pixel scan");
        if (!bdNode) { continue; }
        const aero::Rect vp = NodeViewportBounds(*bdNode);
        const int x0 = std::max(0, static_cast<int>(std::floor(vp.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(vp.y)));
        const int x1 = std::min(1280, static_cast<int>(std::ceil(vp.x + vp.width)));
        const int y1 = std::min(720, static_cast<int>(std::ceil(vp.y + vp.height)));
        std::size_t blackCount = 0u;
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const std::size_t off = (static_cast<std::size_t>(py) * 1280u + static_cast<std::size_t>(px)) * 4u;
                if (frame1[off + 0u] < 8u && frame1[off + 1u] < 8u && frame1[off + 2u] < 8u) {
                    ++blackCount;
                }
            }
        }
        Require(blackCount == 0u,
            "BrushShaders bd[" + std::to_string(i) + "] should not render solid-black pixels (found: " +
            std::to_string(blackCount) + ")");
    }

    // Click all 3 buttons (PLAY, STAGE, EXIT) and verify colors/background do NOT change
    const aero::VisualNodeId buttonsStackId = host.FindVisualByOwnerName("Buttons");
    std::vector<aero::VisualNodeId> btnIds;
    if (buttonsStackId.IsValid()) {
        CollectDescendantsByTypeName(host, buttonsStackId, "Button", btnIds);
    }
    Require(btnIds.size() == 3u,
        "BrushShaders should contain 3 Button nodes under 'Buttons' (got: " + std::to_string(btnIds.size()) + ")");
    for (const aero::VisualNodeId bid : btnIds) {
        const aero::VisualNode* btnNode = host.World().GetNode(bid);
        if (!btnNode) { continue; }
        const aero::Rect bvp = NodeViewportBounds(*btnNode);
        host.InjectClick(bvp.x + bvp.width * 0.5f, bvp.y + bvp.height * 0.5f);
        for (int f = 0; f < 15; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 14);
        }
    }
    std::vector<std::uint8_t> frameAfterClicks;
    Require(host.RenderHeadlessRgba(1280u, 720u, frameAfterClicks), "BrushShaders frameAfterClicks render failed");

    auto sampleRgba = [&](int x, int y, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
        const std::size_t off = (static_cast<std::size_t>(y) * 1280u + static_cast<std::size_t>(x)) * 4u;
        r = frameAfterClicks[off + 0u];
        g = frameAfterClicks[off + 1u];
        b = frameAfterClicks[off + 2u];
    };
    std::uint8_t tlR = 0, tlG = 0, tlB = 0, brR = 0, brG = 0, brB = 0;
    sampleRgba(20, 20, tlR, tlG, tlB);
    sampleRgba(1260, 700, brR, brG, brB);
    Require(tlB > tlR + 40 && brB > brR + 40,
        "BrushShaders background corners must stay blue after clicks (tl=" +
        std::to_string(tlR) + "," + std::to_string(tlG) + "," + std::to_string(tlB) +
        " br=" + std::to_string(brR) + "," + std::to_string(brG) + "," + std::to_string(brB) + ")");

    if (btnIds.size() == 3u) {
        auto scanButtonColor = [&](aero::VisualNodeId bid, bool expectOrange) -> bool {
            const aero::VisualNode* btnNode = host.World().GetNode(bid);
            if (!btnNode) { return false; }
            const aero::Rect vp = NodeViewportBounds(*btnNode);
            const int x0 = std::max(0, static_cast<int>(std::floor(vp.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(vp.y)));
            const int x1 = std::min(1280, static_cast<int>(std::ceil(vp.x + vp.width)));
            const int y1 = std::min(720, static_cast<int>(std::ceil(vp.y + vp.height)));
            std::size_t matchCount = 0u;
            for (int py = y0; py < y1; ++py) {
                for (int px = x0; px < x1; ++px) {
                    const std::size_t off = (static_cast<std::size_t>(py) * 1280u + static_cast<std::size_t>(px)) * 4u;
                    const std::uint8_t r = frameAfterClicks[off + 0u];
                    const std::uint8_t b = frameAfterClicks[off + 2u];
                    if (expectOrange) {
                        if (r > 180u && b < 80u) { ++matchCount; }
                    } else {
                        if (b > 180u && r < 100u) { ++matchCount; }
                    }
                }
            }
            return matchCount > 20u;
        };
        Require(scanButtonColor(btnIds[0], false),
            "BrushShaders PLAY button must remain cyan/blue after clicks");
        Require(scanButtonColor(btnIds[2], true),
            "BrushShaders EXIT button must remain orange after clicks");
    }
}


void TestRegressionReviewFixes(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Regression] All Review Fixes...\n";

    // 1. CustomRender: 3D glossy shapes, brick shatter particles, and paused/game-over ResetGame redraw
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "CustomRender", nullptr, ci, hc, 1280u, 720u),
            "CustomRender regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "CustomRender regression load failed: " + err);
        for (int f = 0; f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }

        const aero::VisualNodeId gameId = host.FindVisualByOwnerName("GameControl");
        const aero::VisualNode* gameNode = host.World().GetNode(gameId);
        Require(gameNode && gameNode->hasScriptOwner, "CustomRender GameControl missing");

        std::vector<std::uint8_t> initialResetFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, initialResetFrame), "CustomRender initial render failed");

        const aero::DrawingCommandList* initCmds = host.DrawBuffer().FindCommands(gameId);
        Require(initCmds != nullptr && initCmds->commands.size() >= 260u,
            "CustomRender should emit layered 3D commands for arena, 65 bricks, paddle, and ball");

        host.InjectKey(aero::KeyCode::Space, true);
        host.InjectKey(aero::KeyCode::Space, false);
        host.StepFrame(0.0f, true);
        std::int64_t scoreHit = 0, particlesHit = 0;
        for (int step = 0; step < 120; ++step) {
            host.StepFrame(1.0f / 60.0f, true);
            (void)ReadIntMember(host, gameNode->scriptOwner, "Score", scoreHit);
            if (scoreHit > 0) {
                for (int extra = 0; extra < 8; ++extra) { host.StepFrame(1.0f / 60.0f, true); }
                (void)ReadIntMember(host, gameNode->scriptOwner, "ActiveParticleCount", particlesHit);
                break;
            }
        }
        Require(scoreHit >= 10 && particlesHit >= 8,
            "CustomRender hitting brick should award 10 pts and spawn shatter particles (score=" +
            std::to_string(scoreHit) + ", particles=" + std::to_string(particlesHit) + ")");

        std::vector<std::uint8_t> midPlayFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, midPlayFrame), "CustomRender mid-play render failed");
        MaybeDumpScreenshot(host, "verify_customrender_shatter.ppm", 1280u, 720u);
        Require(CountChangedPixels(initialResetFrame, midPlayFrame) > 500u,
            "CustomRender mid-play frame should visibly differ from initial frame");

        // Pause first, then press R to reset while paused -> canvas MUST immediately redraw to READY state
        host.InjectKey(aero::KeyCode::Space, true);
        host.InjectKey(aero::KeyCode::Space, false);
        host.StepFrame(0.0f, true);
        host.InjectKey(aero::KeyCode::R, true);
        host.InjectKey(aero::KeyCode::R, false);
        host.StepFrame(0.0f, true);

        std::int64_t scoreAfterReset = -1, particlesAfterReset = -1;
        Require(ReadIntMember(host, gameNode->scriptOwner, "Score", scoreAfterReset) && scoreAfterReset == 0,
            "CustomRender R reset should restore Score=0");
        Require(ReadIntMember(host, gameNode->scriptOwner, "ActiveParticleCount", particlesAfterReset) && particlesAfterReset == 0,
            "CustomRender R reset should clear shatter particles");
        std::vector<std::uint8_t> afterResetFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, afterResetFrame), "CustomRender after-reset render failed");
        Require(CountChangedPixels(midPlayFrame, afterResetFrame) > 500u &&
                CountChangedPixels(initialResetFrame, afterResetFrame) < 80u,
            "CustomRender pressing R while paused must immediately redraw reset board");
    }

    // 2. Login: templated TextBox/PasswordBox typing, blinking caret, and placeholder trigger
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Login", nullptr, ci, hc, 1280u, 720u),
            "Login regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Login regression load failed: " + err);
        for (int f = 0; f < 40; ++f) { host.StepFrame(1.0f / 60.0f, f == 39); }

        float ux = 0.0f, uy = 0.0f;
        aero::VisualNodeId userId{};
        Require(RequireNodeCenter(host, "UserBox", ux, uy, &userId), "Login UserBox missing");
        host.InjectClick(ux, uy);
        host.InjectTextInput("abc");
        host.StepFrame(0.05f, true);
        MaybeDumpScreenshot(host, "verify_login_typing.ppm", 1280u, 720u);

        std::vector<std::uint8_t> caretOnFrame, caretOffFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, caretOnFrame), "Login caret-on render failed");
        host.StepFrame(0.55f, true);
        Require(host.RenderHeadlessRgba(1280u, 720u, caretOffFrame), "Login caret-off render failed");
        Require(CountChangedPixels(caretOnFrame, caretOffFrame) > 0u,
            "Login focused UserBox caret must blink between t=0.05s and t=0.60s");

        float px = 0.0f, py = 0.0f;
        aero::VisualNodeId passId{};
        Require(RequireNodeCenter(host, "PassBox", px, py, &passId), "Login PassBox missing");
        const aero::VisualNode* passNode = host.World().GetNode(passId);
        const aero::VisualNodeId passHostId = FindDescendantByOwnerName(host, passId, "PART_ContentHost");
        Require(passHostId.IsValid(), "Login PassBox PART_ContentHost missing");
        const aero::VisualNodeId passPlaceholderId = FindDescendantByOwnerName(host, passId, "Placeholder");
        const aero::VisualNode* passPlaceholderBefore =
            passPlaceholderId.IsValid() ? host.World().GetNode(passPlaceholderId) : nullptr;
        Require(passPlaceholderBefore && passPlaceholderBefore->layout.visibility == aero::Visibility::Visible,
            "Login PassBox Placeholder must be Visible before typing (noesis:Text.PasswordLength == 0 trigger)");
        host.InjectClick(px, py);
        host.InjectTextInput("secret");
        host.StepFrame(0.05f, true);
        std::string passRaw;
        Require(passNode && ReadStringMember(host, passNode->scriptOwner, "Password", passRaw) && passRaw == "secret",
            "Login PassBox Password should equal typed string (got '" + passRaw + "')");
        const aero::VisualNode* passPlaceholderAfter = host.World().GetNode(passPlaceholderId);
        Require(passPlaceholderAfter && passPlaceholderAfter->layout.visibility == aero::Visibility::Collapsed,
            "Login PassBox Placeholder must collapse once password length becomes 6");
        const aero::VisualNodeId passTxtId = FindDescendantByOwnerName(host, passId, "PART_Text");
        const aero::VisualNode* passTxt = host.World().GetNode(passTxtId);
        const std::string expectedBullets =
            "\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F";
        const std::string actualMaskText =
            (passTxt && passTxt->kind == aero::VisualKind::Text) ? passTxt->Text().text : std::string{};
        Require(passTxt && passTxt->kind == aero::VisualKind::Text && actualMaskText == expectedBullets,
            "Login PassBox PART_Text should render 6 PasswordChar (U+25CF) bullets instead of plaintext (got '" +
            actualMaskText + "')");
    }

    // 3. Menu3D: Settings keyboard Up/Down/Left/Right/Enter cycling & Exit menu Shutdown
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Menu3D", nullptr, ci, hc, 1280u, 720u),
            "Menu3D regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Menu3D regression load failed: " + err);
        for (int f = 0; f < 30; ++f) { host.StepFrame(1.0f / 60.0f, f == 29); }

        float sx = 0.0f, sy = 0.0f;
        Require(RequireNodeCenter(host, "Settings", sx, sy), "Menu3D Settings button missing");
        host.InjectClick(sx, sy);
        for (int f = 0; f < 65; ++f) { host.StepFrame(1.0f / 60.0f, f == 64); }

        const aero::VisualNodeId texRowId = host.FindVisualByOwnerName("TexDetail");
        const aero::VisualNodeId texId = host.FindVisualByOwnerName("TexDetailContent");
        const aero::VisualNode* texNode = host.World().GetNode(texId);
        Require(texRowId.IsValid() && texNode && texNode->scriptOwner.valid(), "Menu3D TexDetailContent missing");
        const aero::Rect texVp = NodeViewportBounds(*host.World().GetNode(texRowId));
        host.InjectPointerMove(texVp.x + 24.0f, texVp.y + texVp.height * 0.5f);
        host.InjectClick(texVp.x + 24.0f, texVp.y + texVp.height * 0.5f);
        host.StepFrame(1.0f / 60.0f, true);

        // TexDetail starts at index 1 (Medium); Enter cycles 1 -> 2 (High) -> 0 (Low)
        host.InjectKey(aero::KeyCode::Enter, true);
        host.InjectKey(aero::KeyCode::Enter, false);
        for (int f = 0; f < 4; ++f) { host.StepFrame(1.0f / 60.0f, f == 3); }
        std::int64_t idxAfterEnter1 = -1;
        Require(ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idxAfterEnter1) && idxAfterEnter1 == 2,
            "Menu3D Enter on TexDetail should advance SelectedIndex 1 -> 2 (got " + std::to_string(idxAfterEnter1) + ")");

        host.InjectKey(aero::KeyCode::Enter, true);
        host.InjectKey(aero::KeyCode::Enter, false);
        for (int f = 0; f < 4; ++f) { host.StepFrame(1.0f / 60.0f, f == 3); }
        std::int64_t idxAfterEnter2 = -1;
        Require(ReadIntMember(host, texNode->scriptOwner, "SelectedIndex", idxAfterEnter2) && idxAfterEnter2 == 0,
            "Menu3D Enter at max option should wrap SelectedIndex 2 -> 0 (got " + std::to_string(idxAfterEnter2) + ")");

        // Down arrow moves focus from TexDetail to AAlias CheckBox (not captured by inner horizontal ListBox)
        host.InjectKey(aero::KeyCode::Down, true);
        host.InjectKey(aero::KeyCode::Down, false);
        for (int f = 0; f < 4; ++f) { host.StepFrame(1.0f / 60.0f, f == 3); }
        const aero::VisualNodeId aaId = host.FindVisualByOwnerName("AAlias");
        const aero::VisualNodeId aaCbId = FindDescendantByTypeName(host, aaId, "CheckBox");
        const aero::VisualNode* aaCb = host.World().GetNode(aaCbId);
        bool aaFocused = false;
        Require(aaCb && ReadBoolMember(host, aaCb->scriptOwner, "IsFocused", aaFocused) && aaFocused,
            "Menu3D Down arrow from TexDetail must move focus to AAlias CheckBox");

        // Toggle AAlias CheckBox Checked -> Unchecked; empty <VisualState x:Name="Unchecked"/> must restore CheckMark
        const aero::Rect aaVp = NodeViewportBounds(*aaCb);
        const aero::VisualNodeId cmId = FindDescendantByOwnerName(host, aaCbId, "CheckMark");
        Require(cmId.IsValid(), "Menu3D AAlias CheckMark missing");
        host.InjectClick(aaVp.x + aaVp.width * 0.5f, aaVp.y + aaVp.height * 0.5f);
        for (int f = 0; f < 18; ++f) { host.StepFrame(1.0f / 60.0f, f == 17); }
        bool aaChecked1 = false;
        Require(ReadBoolMember(host, aaCb->scriptOwner, "IsChecked", aaChecked1) && aaChecked1,
            "Menu3D clicking AAlias CheckBox must check it");
        const aero::VisualNode* cmChecked = host.World().GetNode(cmId);
        Require(cmChecked && cmChecked->Vector().strokeOpacity > 0.9f,
            "Menu3D AAlias CheckMark strokeOpacity must reach ~1.0 when Checked");

        host.InjectClick(aaVp.x + aaVp.width * 0.5f, aaVp.y + aaVp.height * 0.5f);
        for (int f = 0; f < 25; ++f) { host.StepFrame(1.0f / 60.0f, f == 24); }
        bool aaChecked2 = true;
        Require(ReadBoolMember(host, aaCb->scriptOwner, "IsChecked", aaChecked2) && !aaChecked2,
            "Menu3D second click on AAlias CheckBox must uncheck it");
        const aero::VisualNode* cmUnchecked = host.World().GetNode(cmId);
        Require(cmUnchecked && cmUnchecked->Vector().strokeOpacity < 0.05f,
            "Menu3D empty Unchecked state must restore CheckMark strokeOpacity back to ~0");

        // Escape back to MainMenu, then click Exit -> host.IsCloseRequested() must become true
        host.InjectKey(aero::KeyCode::Escape, true);
        host.InjectKey(aero::KeyCode::Escape, false);
        for (int f = 0; f < 65; ++f) { host.StepFrame(1.0f / 60.0f, f == 64); }
        Require(!host.IsCloseRequested(), "Menu3D should not request close before Exit");
        float ex = 0.0f, ey = 0.0f;
        Require(RequireNodeCenter(host, "Exit", ex, ey), "Menu3D Exit button missing");
        host.InjectClick(ex, ey);
        host.StepFrame(1.0f / 60.0f, true);
        Require(host.IsCloseRequested(), "Menu3D clicking Exit must trigger Application.Shutdown / IsCloseRequested()");
    }

    // 4. HelloWorld: NoesisGUI text clipped at Width=0 during Logo rotation (t=0.25s) and revealed at t=1.05s
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "HelloWorld", nullptr, ci, hc, 1280u, 720u),
            "HelloWorld regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "HelloWorld regression load failed: " + err);
        for (int f = 0; f < 15; ++f) { host.StepFrame(1.0f / 60.0f, f == 14); } // t = 0.25s
        const aero::VisualNodeId txtPanelId = host.FindVisualByOwnerName("NoesisGUI");
        const aero::VisualNode* txtPanel = host.World().GetNode(txtPanelId);
        Require(txtPanel && txtPanel->layout.layoutRect.width < 1.0f && txtPanel->layout.needsLayoutClip,
            "HelloWorld NoesisGUI panel at t=0.25s must have Width~0 and needsLayoutClip=true");
        std::vector<std::uint8_t> earlyFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, earlyFrame), "HelloWorld t=0.25s render failed");
        MaybeDumpScreenshot(host, "verify_helloworld_t025.ppm", 1280u, 720u);

        for (int f = 0; f < 48; ++f) { host.StepFrame(1.0f / 60.0f, f == 47); } // t = 1.05s
        std::vector<std::uint8_t> lateFrame;
        Require(host.RenderHeadlessRgba(1280u, 720u, lateFrame), "HelloWorld t=1.05s render failed");
        MaybeDumpScreenshot(host, "verify_helloworld_t105.ppm", 1280u, 720u);
        Require(CountChangedPixels(earlyFrame, lateFrame) > 800u,
            "HelloWorld NoesisGUI text must reveal from left to right after Symbol rotation");
    }

    // 5. Inventory: pointer press -> drag -> release equips item onto compatible slot & updates Player.Slots[i]
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Inventory", nullptr, ci, hc, 1280u, 720u),
            "Inventory regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Inventory regression load failed: " + err);
        for (int f = 0; f < 190; ++f) { host.StepFrame(1.0f / 60.0f, f == 189); }

        const aero::VisualNodeId lbId = host.FindVisualByOwnerName("InventoryListBox");
        std::vector<aero::VisualNodeId> bagItems;
        CollectDescendantsByTypeName(host, lbId, "ListBoxItem", bagItems);
        Require(bagItems.size() >= 2u, "Inventory drag test requires >=2 bag items");
        const aero::VisualNode* bag0 = host.World().GetNode(bagItems[0]);
        const aero::Rect b0Vp = NodeViewportBounds(*bag0);
        float leftCx = 0.0f, leftCy = 0.0f;
        aero::VisualNodeId slotLeftId{};
        Require(RequireNodeCenter(host, "SlotLeft", leftCx, leftCy, &slotLeftId), "Inventory SlotLeft missing");
        const aero::VisualNode* slotLeftInit = host.World().GetNode(slotLeftId);
        std::string slotLeftInitName = "non-empty";
        Require(slotLeftInit && ReadStringMember(host, slotLeftInit->scriptOwner, "Content.Name", slotLeftInitName) &&
                slotLeftInitName.empty(),
            "Inventory SlotLeft must start empty before drag (got '" + slotLeftInitName + "')");

        const float sx = b0Vp.x + b0Vp.width * 0.5f;
        const float sy = b0Vp.y + b0Vp.height * 0.5f;
        host.InjectPointerMove(sx, sy);
        host.InjectPointerButton(sx, sy, aero::PointerButton::Left, true);
        host.StepFrame(1.0f / 60.0f, false);
        host.InjectPointerMove(sx + 16.0f, sy + 8.0f);
        host.StepFrame(1.0f / 60.0f, true);
        bool foundFullSizeAdorner = false;
        std::vector<aero::VisualNodeId> dfs{host.RootVisualNode()};
        while (!dfs.empty()) {
            const aero::VisualNodeId nid = dfs.back();
            dfs.pop_back();
            const aero::VisualNode* n = host.World().GetNode(nid);
            if (!n) { continue; }
            if (n->layout.zIndex == 9999 &&
                n->layout.layoutRect.width >= 96.0f &&
                n->layout.layoutRect.height >= 96.0f) {
                foundFullSizeAdorner = true;
                break;
            }
            for (aero::VisualNodeId ch : n->children) { dfs.push_back(ch); }
        }
        Require(foundFullSizeAdorner, "Inventory drag adorner should match source slot size (>= 96x96)");
        host.InjectPointerMove(leftCx, leftCy);
        host.StepFrame(1.0f / 60.0f, true);
        host.InjectPointerButton(leftCx, leftCy, aero::PointerButton::Left, false);
        for (int f = 0; f < 10; ++f) { host.StepFrame(1.0f / 60.0f, f == 9); }

        const aero::VisualNode* slotLeftNode = host.World().GetNode(slotLeftId);
        std::string slotLeftItemName;
        Require(slotLeftNode && ReadStringMember(host, slotLeftNode->scriptOwner, "Content.Name", slotLeftItemName) &&
                slotLeftItemName == "Club",
            "Inventory pointer drag-and-drop onto empty SlotLeft must equip 'Club' (got '" + slotLeftItemName + "')");
    }

    // 6. TicTacToe: implicit Style Trigger IsChecked=True disables occupied cell and blocks second move
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "TicTacToe", nullptr, ci, hc, 1280u, 720u),
            "TicTacToe regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "TicTacToe regression load failed: " + err);
        for (int f = 0; f < 110; ++f) { host.StepFrame(1.0f / 60.0f, f == 109); }

        aero::ObjectRef vmRef{};
        Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef), "TicTacToe VM missing");

        float cx = 0.0f, cy = 0.0f;
        aero::VisualNodeId cell0Id{};
        Require(RequireNodeCenter(host, "Cell0", cx, cy, &cell0Id), "TicTacToe Cell0 missing");
        const aero::VisualNode* cell0 = host.World().GetNode(cell0Id);
        Require(cell0 && cell0->scriptOwner.valid(), "TicTacToe Cell0 scriptOwner missing");

        host.InjectClick(cx, cy);
        for (int f = 0; f < 45; ++f) { host.StepFrame(1.0f / 60.0f, f == 44); }
        bool checked1 = false, enabled1 = true;
        std::int64_t mark1 = 0;
        Require(ReadBoolMember(host, cell0->scriptOwner, "IsChecked", checked1) && checked1,
            "TicTacToe first click on Cell0 should check Cell0");
        Require(ReadBoolMember(host, cell0->scriptOwner, "IsEnabled", enabled1) && !enabled1,
            "TicTacToe implicit Style Trigger IsChecked=True should disable Cell0 (IsEnabled=false)");
        Require(ReadIntMember(host, vmRef, "Cell0", mark1) && mark1 != 0,
            "TicTacToe first click should record mark in VM.Cell0");

        // Second click on occupied Cell0 must be rejected (neither unchecking nor switching to Player 2)
        host.InjectClick(cx, cy);
        for (int f = 0; f < 10; ++f) { host.StepFrame(1.0f / 60.0f, f == 9); }
        bool checked2 = false, enabled2 = true;
        std::int64_t mark2 = -1;
        Require(ReadBoolMember(host, cell0->scriptOwner, "IsChecked", checked2) && checked2,
            "TicTacToe second click on occupied Cell0 must keep IsChecked=true");
        Require(ReadBoolMember(host, cell0->scriptOwner, "IsEnabled", enabled2) && !enabled2,
            "TicTacToe occupied Cell0 must remain IsEnabled=false");
        Require(ReadIntMember(host, vmRef, "Cell0", mark2) && mark2 == mark1,
            "TicTacToe second click on occupied Cell0 must not overwrite VM.Cell0");
    }

    // 7. BackgroundBlur: progressive blur across 0 -> 20 -> 40 without mid-slider saturation
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "BackgroundBlur", nullptr, ci, hc, 1280u, 720u),
            "BackgroundBlur regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "BackgroundBlur regression load failed: " + err);
        for (int f = 0; f < 15; ++f) { host.StepFrame(1.0f / 60.0f, f == 14); }
        MaybeDumpScreenshot(host, "verify_backgroundblur.ppm", 1280u, 720u);

        std::vector<std::uint8_t> frameMid20;
        Require(host.RenderHeadlessRgba(1280u, 720u, frameMid20), "BackgroundBlur mid=20 render failed");

        const aero::VisualNodeId topSliderId = host.FindVisualByOwnerName("TopBlurRadius");
        const aero::VisualNode* topSlider = host.World().GetNode(topSliderId);
        Require(topSlider != nullptr, "BackgroundBlur TopBlurRadius missing");
        const aero::Rect sVp = NodeViewportBounds(*topSlider);

        host.InjectClick(sVp.x + sVp.width - 2.0f, sVp.y + sVp.height * 0.5f);
        for (int f = 0; f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }
        std::vector<std::uint8_t> frameMax40;
        Require(host.RenderHeadlessRgba(1280u, 720u, frameMax40), "BackgroundBlur max=40 render failed");
        Require(CountChangedPixels(frameMid20, frameMax40) > 500u,
            "BackgroundBlur must continue blurring progressively from Value=20 to Value=40");
    }

    // 8. CustomAnimation: dashed vector strokes, SVG elliptical arc gauge dots & true R=40 circular orbit
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "CustomAnimation", &session, ci, hc, 1280u, 720u),
            "CustomAnimation regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "CustomAnimation regression load failed: " + err);
        for (int f = 0; f < 15; ++f) { host.StepFrame(1.0f / 60.0f, f == 14); }

        const aero::VisualNodeId dot1Id = host.FindVisualByOwnerName("Dot1");
        const aero::VisualNode* dot1 = host.World().GetNode(dot1Id);
        Require(dot1 != nullptr, "CustomAnimation Dot1 missing");
        const float rOrbit = std::hypot(dot1->transform2D.tx, dot1->transform2D.ty);
        Require(rOrbit > 37.5f && rOrbit < 42.5f,
            "CustomAnimation Dot1 should follow true R=40 circle at t=0.25s (got r=" + std::to_string(rOrbit) + ")");
        for (int f = 0; f < 120; ++f) { host.StepFrame(1.0f / 60.0f, f == 119); } // t = 2.25s (second orbit!)
        const aero::VisualNode* dot225 = host.World().GetNode(dot1Id);
        const float tx225 = dot225 ? dot225->transform2D.tx : 0.0f;
        const float ty225 = dot225 ? dot225->transform2D.ty : 0.0f;
        const float rOrbit225 = std::hypot(tx225, ty225);
        Require(rOrbit225 > 37.5f && rOrbit225 < 42.5f,
            "CustomAnimation Dot1 must stay on R=40 circle past 2.0s at t=2.25s (got r=" + std::to_string(rOrbit225) + ")");
        for (int f = 0; f < 30; ++f) { host.StepFrame(1.0f / 60.0f, f == 29); } // t = 2.75s
        const aero::VisualNode* dot275 = host.World().GetNode(dot1Id);
        const float tx275 = dot275 ? dot275->transform2D.tx : 0.0f;
        const float ty275 = dot275 ? dot275->transform2D.ty : 0.0f;
        const float rOrbit275 = std::hypot(tx275, ty275);
        const float delta225To275 = std::hypot(tx275 - tx225, ty275 - ty225);
        Require(rOrbit275 > 37.5f && rOrbit275 < 42.5f && delta225To275 > 20.0f,
            "CustomAnimation Dot1 must keep orbiting smoothly between t=2.25s and t=2.75s");
        MaybeDumpScreenshot(host, "verify_customanimation.ppm", 1280u, 720u);
    }

    // 9. UserControl: NumericUpDown ElementName=NumericUpDownControl displays 10, 14, 8, 12 + BarStr + MinRepeatButton disabled
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "UserControl", &session, ci, hc, 1280u, 720u),
            "UserControl regression discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "UserControl regression load failed: " + err);
        for (int f = 0; f < 28; ++f) { host.StepFrame(1.0f / 60.0f, f == 27); }
        std::unordered_set<std::string> texts;
        auto walkTexts = [&](auto& self, aero::VisualNodeId nid) -> void {
            const aero::VisualNode* n = host.World().GetNode(nid);
            if (!n) return;
            if (n->kind == aero::VisualKind::Text && !n->Text().text.empty()) {
                texts.insert(n->Text().text);
            }
            for (aero::VisualNodeId cid : n->children) self(self, cid);
        };
        walkTexts(walkTexts, host.World().Root());
        Require(texts.count("10") > 0u && texts.count("14") > 0u &&
                texts.count("8") > 0u && texts.count("12") > 0u,
            "UserControl NumericUpDown TextBlocks must display 10, 14, 8, and 12");

        const aero::VisualNodeId barStrId = host.FindVisualByOwnerName("BarStr");
        const aero::VisualNode* barStrNode = host.World().GetNode(barStrId);
        float barStrVal = 0.0f;
        Require(barStrNode && ReadFloatMember(host, barStrNode->scriptOwner, "Value", barStrVal) &&
                std::fabs(barStrVal - 10.0f) < 0.2f,
            "UserControl BarStr Value should match StatStr Value=10 (got " + std::to_string(barStrVal) + ")");

        const aero::VisualNodeId statStrId = host.FindVisualByOwnerName("StatStr");
        const aero::VisualNode* statStrNode = host.World().GetNode(statStrId);
        std::int64_t minVal = 0;
        Require(statStrNode && ReadIntMember(host, statStrNode->scriptOwner, "MinValue", minVal) && minVal == 3,
            "UserControl StatStr MinValue should equal 3 from XAML attribute (got " + std::to_string(minVal) + ")");
        const aero::VisualNodeId downBtnId = FindDescendantByOwnerName(host, statStrId, "DownButton");
        const aero::VisualNode* downBtn = host.World().GetNode(downBtnId);
        bool downEnBefore = false;
        Require(downBtn && ReadBoolMember(host, downBtn->scriptOwner, "IsEnabled", downEnBefore) && downEnBefore,
            "UserControl StatStr DownButton should start enabled at Value=10");
        const aero::Rect downVp = NodeViewportBounds(*downBtn);
        for (int click = 0; click < 7; ++click) {
            host.InjectClick(downVp.x + downVp.width * 0.5f, downVp.y + downVp.height * 0.5f);
            for (int f = 0; f < 3; ++f) { host.StepFrame(1.0f / 60.0f, f == 2); }
        }
        float statStrAfter = 0.0f;
        Require(ReadFloatMember(host, statStrNode->scriptOwner, "Value", statStrAfter) &&
                std::fabs(statStrAfter - 3.0f) < 0.05f,
            "UserControl StatStr Value should decrement to MinValue=3 after 7 clicks (got " +
            std::to_string(statStrAfter) + ")");
        bool downEnAfter = true;
        Require(ReadBoolMember(host, downBtn->scriptOwner, "IsEnabled", downEnAfter) && !downEnAfter,
            "UserControl StatStr MinRepeatButton must disable DownButton when Value reaches MinValue=3");
        MaybeDumpScreenshot(host, "verify_usercontrol.ppm", 1280u, 720u);
    }

    // 10. Vertical templated ProgressBar: rotated horizontal template tracks PART_Indicator.Width without clobbering Height
    {
        aero::AeroV3CompileInput ci;
        ci.xamlPath = "VerticalTemplatedPb.xaml";
        ci.xamlContent = R"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      Width="200" Height="200">
  <ProgressBar x:Name="VertPb" Orientation="Vertical" Minimum="0" Maximum="100" Value="50"
               Width="16" Height="120">
    <ProgressBar.Template>
      <ControlTemplate TargetType="ProgressBar">
        <Grid Width="120" Height="16">
          <Grid.LayoutTransform>
            <RotateTransform Angle="-90"/>
          </Grid.LayoutTransform>
          <Border x:Name="PART_Track" Width="120" Height="16"/>
          <Border x:Name="PART_Indicator" HorizontalAlignment="Left" Width="10" Height="16"/>
        </Grid>
      </ControlTemplate>
    </ProgressBar.Template>
  </ProgressBar>
</Grid>
)XAML";
        ConfigureStdlibForTest(ci, &session, /*allowTreeShakeSource=*/true);
        aero::AeroAppHostConfig hc;
        hc.viewportWidth = 200u;
        hc.viewportHeight = 200u;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "VerticalTemplatedPb load failed: " + err);
        for (int f = 0; f < 4; ++f) { host.StepFrame(1.0f / 60.0f, f == 3); }
        const aero::VisualNodeId indId = host.FindVisualByOwnerName("PART_Indicator");
        const aero::VisualNode* indNode = host.World().GetNode(indId);
        Require(indNode != nullptr, "VerticalTemplatedPb PART_Indicator missing");
        const aero::Rect ir = indNode->layout.layoutRect;
        Require(ir.width > 55.0f && ir.width < 65.0f && ir.height > 14.0f && ir.height < 18.0f,
            "Vertical templated ProgressBar PART_Indicator must size Width=~60 and keep Height=~16 (got " +
            std::to_string(ir.width) + "x" + std::to_string(ir.height) + ")");
    }
}

void TestRound3All18Fixes(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Regression] Round-3 All 18 Engine & Gallery Fixes...\n";

    // 1. #07 Login: typing password with trailing '\r' + pressing Return key invokes LoginCommand(PassBox)
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Login", &session, ci, hc, 800u, 600u),
            "Login #07 discover failed");
        ci.treeShakeStdlib = true;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Login #07 load failed: " + err);
        for (int f = 0; f < 45; ++f) { host.StepFrame(1.0f / 60.0f, f == 44); }

        aero::ObjectRef vmRef{};
        Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef), "Login #07 VM missing");
        float ux = 0.0f, uy = 0.0f, px = 0.0f, py = 0.0f;
        aero::VisualNodeId passBoxId{};
        Require(RequireNodeCenter(host, "UserBox", ux, uy), "Login #07 UserBox missing");
        Require(RequireNodeCenter(host, "PassBox", px, py, &passBoxId), "Login #07 PassBox missing");

        host.InjectClick(ux, uy);
        host.InjectTextInput("noesis\r");
        host.StepFrame(1.0f / 60.0f, false);

        host.InjectClick(px, py);
        host.InjectTextInput("12345\r");
        host.StepFrame(1.0f / 60.0f, false);

        const aero::VisualNode* passNode = host.World().GetNode(passBoxId);
        std::string rawPwd;
        Require(passNode && ReadStringMember(host, passNode->scriptOwner, "Password", rawPwd) && rawPwd == "12345",
            "Login #07 PassBox must strip '\\r' from Password (got '" + rawPwd + "')");

        host.InjectKey(aero::KeyCode::Enter, true);
        host.InjectKey(aero::KeyCode::Enter, false);
        host.StepFrame(1.0f / 60.0f, true);

        bool loginOk = false;
        Require(ReadBoolMember(host, vmRef, "LoginSuccessful", loginOk) && loginOk,
            "Login #07 pressing Return key after password entry must authenticate via LoginOnEnter");
    }

    // 2. #03 & #04 TicTacToe: first frame after move has zero OX overlap flash + restart without mouse move
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "TicTacToe", &session, ci, hc, 800u, 600u),
            "TicTacToe #04 discover failed");
        ci.treeShakeStdlib = true;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "TicTacToe #04 load failed: " + err);
        for (int f = 0; f < 105; ++f) { host.StepFrame(1.0f / 60.0f, f == 104); }

        float cx0 = 0.0f, cy0 = 0.0f;
        aero::VisualNodeId cell0Id{};
        Require(RequireNodeCenter(host, "Cell0", cx0, cy0, &cell0Id), "TicTacToe #04 Cell0 missing");
        const aero::VisualNodeId checkGroupId = FindDescendantByOwnerName(host, cell0Id, "CheckGroup");
        const aero::VisualNodeId noughtId = FindDescendantByOwnerName(host, cell0Id, "CheckNought");
        const aero::VisualNodeId crossId = FindDescendantByOwnerName(host, cell0Id, "CheckCross");
        Require(checkGroupId.IsValid() && noughtId.IsValid() && crossId.IsValid(),
            "TicTacToe #04 CheckGroup/CheckNought/CheckCross missing");

        host.InjectClick(cx0, cy0);
        host.StepFrame(1.0f / 60.0f, true);

        const aero::VisualNode* cgNode = host.World().GetNode(checkGroupId);
        const aero::VisualNode* noughtNode = host.World().GetNode(noughtId);
        const aero::VisualNode* crossNode = host.World().GetNode(crossId);
        Require(cgNode && cgNode->layout.visibility == aero::Visibility::Visible,
            "TicTacToe #04 CheckGroup must be Visible on frame 1");
        Require(noughtNode && noughtNode->layout.visibility == aero::Visibility::Visible &&
                crossNode && crossNode->layout.visibility == aero::Visibility::Collapsed,
            "TicTacToe #04 frame 1 after Player1 move must show ONLY CheckNought and collapse CheckCross");

        const char* moves[] = {"Cell3", "Cell1", "Cell4", "Cell2"};
        for (const char* m : moves) {
            float mx = 0.0f, my = 0.0f;
            Require(RequireNodeCenter(host, m, mx, my), std::string("TicTacToe missing ") + m);
            for (int f = 0; f < 25; ++f) { host.StepFrame(1.0f / 60.0f, false); }
            host.InjectClick(mx, my);
            host.StepFrame(1.0f / 60.0f, true);
        }
        for (int f = 0; f < 65; ++f) { host.StepFrame(1.0f / 60.0f, f == 64); }
        float startX = 0.0f, startY = 0.0f;
        Require(RequireNodeCenter(host, "Start", startX, startY), "TicTacToe #03 Start button missing");
        host.InjectClick(startX, startY);
        bool sawAnimating = false;
        for (int f = 0; f < 90; ++f) {
            host.StepFrame(1.0f / 60.0f, f == 89);
            if (!host.DesktopHost().CanSleepIdle()) { sawAnimating = true; }
        }
        aero::ObjectRef vmRef{};
        std::int64_t cell0Reset = -1;
        Require(ReadObjectMember(host, host.RootScriptObject(), "VM", vmRef) &&
                ReadIntMember(host, vmRef, "Cell0", cell0Reset) && sawAnimating && cell0Reset == 0,
            "TicTacToe #03 restart must complete flip & reset Cell0==0 without mouse movement");
    }

    // 3. #01 & #02 QuestLog: close (1.5s) -> reopen Intro (0.7s) runs without mouse move & pathMask cache <= 16MB
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "QuestLog", &session, ci, hc, 800u, 600u),
            "QuestLog #01/#02 discover failed");
        ci.treeShakeStdlib = true;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "QuestLog #01/#02 load failed: " + err);
        for (int f = 0; f < 55; ++f) { host.StepFrame(1.0f / 60.0f, f == 54); }

        const aero::VisualNodeId dialogId = host.FindVisualByOwnerName("Dialog");
        Require(dialogId.IsValid(), "QuestLog Dialog missing");
        std::vector<aero::VisualNodeId> btns;
        CollectDescendantsByTypeName(host, dialogId, "Button", btns);
        Require(!btns.empty(), "QuestLog Close Button missing");
        const aero::Rect closeVp = NodeViewportBounds(*host.World().GetNode(btns[0]));
        const float closeX = closeVp.x + closeVp.width * 0.5f;
        const float closeY = closeVp.y + closeVp.height * 0.5f;

        for (int cycle = 0; cycle < 3; ++cycle) {
            host.InjectClick(closeX, closeY);
            // Close is 1.5s (90 frames) + Intro is 0.7s (42 frames) = 132 frames; step 150 frames without mouse move
            for (int f = 0; f < 150; ++f) {
                host.StepFrame(1.0f / 60.0f, true);
            }
            const aero::VisualNode* dlg = host.World().GetNode(dialogId);
            Require(dlg && dlg->layout.opacity > 0.95f && dlg->transform2D.sx > 0.95f,
                "QuestLog #01 Dialog must reopen to full scale & opacity without mouse movement");
        }
        const std::size_t maskCount = host.World().ResourceStore()->PathMaskCount();
        const std::size_t maskBytes = host.World().ResourceStore()->PathMaskTotalBytes();
        Require(maskCount <= 96u && maskBytes <= 16u * 1024u * 1024u,
            "QuestLog #02 PathMask cache must stay bounded <= 16MB across restarts (count=" +
            std::to_string(maskCount) + ", bytes=" + std::to_string(maskBytes) + ")");
    }

    // 4. #08 Menu3D: OptionSelector arrow restores stroke color when focus moves away
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Menu3D", &session, ci, hc, 1280u, 720u),
            "Menu3D #08 discover failed");
        ci.treeShakeStdlib = true;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Menu3D #08 load failed: " + err);
        for (int f = 0; f < 65; ++f) { host.StepFrame(1.0f / 60.0f, f == 64); }

        float setX = 0.0f, setY = 0.0f;
        Require(RequireNodeCenter(host, "Settings", setX, setY), "Menu3D #08 Settings button missing");
        host.InjectClick(setX, setY);
        for (int f = 0; f < 60; ++f) { host.StepFrame(1.0f / 60.0f, f == 59); }

        const aero::VisualNodeId texId = host.FindVisualByOwnerName("TexDetail");
        const aero::VisualNodeId aaId = host.FindVisualByOwnerName("AAlias");
        Require(texId.IsValid() && aaId.IsValid(), "Menu3D #08 TexDetail/AAlias missing");
        const aero::VisualNodeId nextBtnId = FindDescendantByOwnerName(host, texId, "NextButton");
        Require(nextBtnId.IsValid(), "Menu3D #08 TexDetail NextButton missing");
        const aero::VisualNodeId arrowId = FindDescendantByOwnerName(host, nextBtnId, "Arrow");
        Require(arrowId.IsValid(), "Menu3D #08 NextButton Arrow missing");

        const aero::Rect nextVp = NodeViewportBounds(*host.World().GetNode(nextBtnId));
        host.InjectPointerMove(nextVp.x + nextVp.width * 0.5f, nextVp.y + nextVp.height * 0.5f);
        for (int f = 0; f < 20; ++f) { host.StepFrame(1.0f / 60.0f, f == 19); }
        const std::uint32_t hoverStroke = host.World().GetNode(arrowId)->Vector().strokeRgba;

        const aero::Rect aaVp = NodeViewportBounds(*host.World().GetNode(aaId));
        host.InjectPointerMove(aaVp.x + aaVp.width * 0.5f, aaVp.y + aaVp.height * 0.5f);
        for (int f = 0; f < 25; ++f) { host.StepFrame(1.0f / 60.0f, f == 24); }
        const std::uint32_t restoredStroke = host.World().GetNode(arrowId)->Vector().strokeRgba;
        Require(restoredStroke != hoverStroke,
            "Menu3D #08 OptionSelector NextButton Arrow must restore color after hover/focus leave");
    }

    // 5. #09-#17 Gallery responsive sidebar + RepeatButton + RadioButton + TextBlock + TextBox + ListBox + TreeView + WrapPanel + ContextMenu
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Gallery", &session, ci, hc, 1280u, 800u),
            "Gallery #09-#17 discover failed");
        ci.treeShakeStdlib = true;
        ci.allowStdlibCache = false;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Gallery #09-#17 load failed: " + err);
        for (int f = 0; f < 20; ++f) { host.StepFrame(1.0f / 60.0f, f == 19); }

        // #09 Window width shrink collapses Controls Gallery sidebar
        const aero::VisualNodeId expBtnId = host.FindVisualByOwnerName("SelectorExpanderButton");
        const aero::VisualNodeId sampleOffId = host.FindVisualByOwnerName("SampleOffset");
        Require(expBtnId.IsValid() && sampleOffId.IsValid(), "Gallery SelectorExpanderButton/SampleOffset missing");
        Require(host.World().GetNode(expBtnId)->layout.visibility == aero::Visibility::Collapsed,
            "Gallery SelectorExpanderButton should start Collapsed at 1280x800");
        host.InjectResize(640u, 800u);
        for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
        Require(host.World().GetNode(expBtnId)->layout.visibility == aero::Visibility::Visible &&
                host.World().GetNode(sampleOffId)->layout.layoutRect.width < 1.0f,
            "Gallery #09 shrinking window width to 640 must collapse sidebar and show expander button");
        host.InjectResize(1280u, 800u);
        for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
        Require(host.World().GetNode(expBtnId)->layout.visibility == aero::Visibility::Collapsed &&
                host.World().GetNode(sampleOffId)->layout.layoutRect.width > 200.0f,
            "Gallery #09 restoring window width to 1280 must re-dock sidebar");

        auto activeContainer = [&]() -> aero::VisualNodeId {
            const aero::VisualNodeId c1 = host.FindVisualByOwnerName("SampleContainer1");
            const aero::VisualNodeId c2 = host.FindVisualByOwnerName("SampleContainer2");
            const aero::VisualNode* n1 = c1.IsValid() ? host.World().GetNode(c1) : nullptr;
            const aero::VisualNode* n2 = c2.IsValid() ? host.World().GetNode(c2) : nullptr;
            if (n1 && n1->layout.visibility == aero::Visibility::Visible && !n1->children.empty()) { return c1; }
            if (n2 && n2->layout.visibility == aero::Visibility::Visible && !n2->children.empty()) { return c2; }
            return aero::VisualNodeId{};
        };
        auto openSample = [&](const char* sampleName) -> aero::VisualNodeId {
            aero::UnifiedValue ret{};
            bool ok = false;
            Require(host.InvokeScriptMethod(
                        host.RootScriptObject(),
                        "SelectSampleByName",
                        {aero::UnifiedValue{std::string_view(sampleName)}},
                        &ret) &&
                    ret.tryGetBool(ok) && ok,
                std::string("Gallery SelectSampleByName('") + sampleName + "') failed");
            for (int f = 0; f < 45; ++f) { host.StepFrame(1.0f / 60.0f, f == 44); }
            const aero::VisualNodeId cid = activeContainer();
            Require(cid.IsValid(), std::string("Gallery active container missing for ") + sampleName);
            return cid;
        };

        // #10 RepeatButton HoverScroll (last RepeatButton in inner HoverScroll ScrollViewer is LineRightCommand ClickMode="Hover")
        {
            const aero::VisualNodeId cid = openSample("RepeatButton");
            std::vector<aero::VisualNodeId> svs;
            CollectDescendantsByTypeName(host, cid, "ScrollViewer", svs);
            Require(svs.size() >= 2u, "Gallery #10 HoverScroll inner ScrollViewer missing");
            const aero::VisualNodeId innerSv = svs.back();
            if (const aero::VisualNode* svNode0 = host.World().GetNode(innerSv)) {
                const aero::Rect curVp = NodeViewportBounds(*svNode0);
                if (curVp.y + curVp.height > 760.0f) {
                    (void)host.InvokeScriptMethod(
                        host.World().GetNode(svs[0])->scriptOwner,
                        "ScrollToVerticalOffset",
                        {aero::UnifiedValue{aero::FloatValue{450.0f}}});
                    for (int f = 0; f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }
                }
            }
            std::vector<aero::VisualNodeId> hoverRbs;
            CollectDescendantsByTypeName(host, innerSv, "RepeatButton", hoverRbs);
            Require(hoverRbs.size() >= 2u, "Gallery #10 HoverScroll RepeatButtons missing");
            const aero::VisualNodeId hoverRightRb = hoverRbs.back();
            float hOff0 = 0.0f;
            Require(ReadFloatMember(host, host.World().GetNode(innerSv)->scriptOwner, "HorizontalOffset", hOff0),
                "Gallery #10 inner ScrollViewer HorizontalOffset unreadable");
            const aero::VisualNode* rbNode = host.World().GetNode(hoverRightRb);
            Require(rbNode != nullptr, "Gallery #10 hoverRightRb node null");
            const aero::Rect rbVp = NodeViewportBounds(*rbNode);
            host.InjectPointerMove(rbVp.x + rbVp.width * 0.5f, rbVp.y + rbVp.height * 0.5f);
            for (int f = 0; f < 30; ++f) { host.StepFrame(1.0f / 60.0f, f == 29); }
            float hOff1 = 0.0f;
            (void)ReadFloatMember(host, host.World().GetNode(innerSv)->scriptOwner, "HorizontalOffset", hOff1);
            Require(hOff1 > hOff0 + 10.0f,
                "Gallery #10 hovering HoverScroll right RepeatButton must advance HorizontalOffset (got " +
                std::to_string(hOff1) + ")");
            host.InjectPointerMove(5.0f, 5.0f);
            host.StepFrame(1.0f / 60.0f, false);
        }

        // #11 RadioButton preview Border Background & BorderBrush update
        {
            const aero::VisualNodeId cid = openSample("RadioButton");
            aero::VisualNodeId previewBorderId{};
            std::vector<aero::VisualNodeId> stack{cid};
            while (!stack.empty()) {
                const aero::VisualNodeId cur = stack.back();
                stack.pop_back();
                const aero::VisualNode* cn = host.World().GetNode(cur);
                if (!cn) { continue; }
                std::string nm;
                if (cn->scriptOwner.valid() && ReadStringMember(host, cn->scriptOwner, "Name", nm) &&
                    nm == "Border" && cn->layout.layoutRect.height >= 45.0f) {
                    previewBorderId = cur;
                    break;
                }
                for (aero::VisualNodeId ch : cn->children) { stack.push_back(ch); }
            }
            Require(previewBorderId.IsValid(), "Gallery #11 50px preview Border missing");
            const aero::VisualNode* pb0 = host.World().GetNode(previewBorderId);
            const std::uint32_t bg0 = pb0->box.bgRgba;
            const std::uint32_t bd0 = pb0->box.borderRgba;

            std::vector<aero::VisualNodeId> radios;
            CollectDescendantsByTypeName(host, cid, "RadioButton", radios);
            Require(radios.size() >= 10u, "Gallery #11 expected >= 10 RadioButtons");
            std::vector<aero::VisualNodeId> svs;
            CollectDescendantsByTypeName(host, cid, "ScrollViewer", svs);
            if (!svs.empty()) {
                (void)host.InvokeScriptMethod(
                    host.World().GetNode(svs[0])->scriptOwner,
                    "ScrollToVerticalOffset",
                    {aero::UnifiedValue{aero::FloatValue{350.0f}}});
                for (int f = 0; f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }
            }
            int toggled = 0;
            for (std::size_t rIdx = 4u; rIdx < radios.size(); ++rIdx) {
                const aero::VisualNode* rn = host.World().GetNode(radios[rIdx]);
                bool isChk = false;
                if (rn && ReadBoolMember(host, rn->scriptOwner, "IsChecked", isChk) && !isChk) {
                    const aero::Rect rVp = NodeViewportBounds(*rn);
                    host.InjectClick(rVp.x + 12.0f, rVp.y + rVp.height * 0.5f);
                    host.StepFrame(1.0f / 60.0f, false);
                    ++toggled;
                }
            }
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
            const aero::VisualNode* pb1 = host.World().GetNode(previewBorderId);
            Require(toggled >= 2 && pb1 && (pb1->box.bgRgba != bg0 || pb1->box.borderRgba != bd0),
                "Gallery #11 toggling RadioButtons must change preview Border Background and BorderBrush");
        }

        // #12 TextBlock TextTrimming ComboBox feedback
        {
            const aero::VisualNodeId cid = openSample("TextBlock");
            std::vector<aero::VisualNodeId> combos;
            CollectDescendantsByTypeName(host, cid, "ComboBox", combos);
            Require(!combos.empty(), "Gallery #12 TextBlock ComboBox missing");
            const aero::VisualNode* trimCombo = host.World().GetNode(combos.back());
            Require(host.InvokeScriptMethod(
                        trimCombo->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{0}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #12 trimCombo NotifyItemClicked(0) failed");
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
            bool foundEllipsisMode = false;
            std::vector<aero::VisualNodeId> tbs;
            CollectDescendantsByTypeName(host, cid, "TextBlock", tbs);
            for (aero::VisualNodeId tid : tbs) {
                const aero::VisualNode* tn = host.World().GetNode(tid);
                if (tn && tn->HasExtras() && tn->Text().trim != aero::TextTrimming::None) {
                    foundEllipsisMode = true;
                }
            }
            if (!foundEllipsisMode) {
                Require(host.InvokeScriptMethod(
                            trimCombo->scriptOwner,
                            "NotifyItemClicked",
                            {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{1}}, aero::UnifiedValue{std::int64_t{0}}}),
                    "Gallery #12 trimCombo NotifyItemClicked(1) failed");
                for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
                for (aero::VisualNodeId tid : tbs) {
                    const aero::VisualNode* tn = host.World().GetNode(tid);
                    if (tn && tn->HasExtras() && tn->Text().trim != aero::TextTrimming::None) {
                        foundEllipsisMode = true;
                    }
                }
            }
            Require(foundEllipsisMode, "Gallery #12 changing TextTrimming ComboBox must propagate to TextBlock");
        }

        // #13 TextBox read-only SelectionBrush="BurlyWood" SelectionOpacity="0.5" non-black selection highlight
        {
            const aero::VisualNodeId cid = openSample("TextBox");
            std::vector<aero::VisualNodeId> boxes;
            CollectDescendantsByTypeName(host, cid, "TextBox", boxes);
            bool foundBurlySelection = false;
            for (aero::VisualNodeId bid : boxes) {
                const aero::VisualNode* bn = host.World().GetNode(bid);
                bool isRo = false;
                if (bn && ReadBoolMember(host, bn->scriptOwner, "IsReadOnly", isRo) && isRo) {
                    std::vector<aero::VisualNodeId> stack{bid};
                    while (!stack.empty()) {
                        const aero::VisualNodeId cur = stack.back();
                        stack.pop_back();
                        const aero::VisualNode* cn = host.World().GetNode(cur);
                        if (!cn) { continue; }
                        if (cn->kind == aero::VisualKind::Text && cn->HasExtras()) {
                            const std::uint32_t sel = cn->Text().selRgba;
                            const std::uint32_t r = sel & 0xFFu;
                            const std::uint32_t g = (sel >> 8u) & 0xFFu;
                            const std::uint32_t b = (sel >> 16u) & 0xFFu;
                            const std::uint32_t a = (sel >> 24u) & 0xFFu;
                            if (r == 0xDEu && g == 0xB8u && b == 0x87u && a >= 100u && a <= 160u) {
                                foundBurlySelection = true;
                            }
                        }
                        for (aero::VisualNodeId ch : cn->children) { stack.push_back(ch); }
                    }
                }
            }
            Require(foundBurlySelection,
                "Gallery #13 read-only TextBox must resolve BurlyWood with SelectionOpacity=0.5 (non-black)");
        }

        // #14 ListBox SelectionMode binding (Multiple allows multi-selection)
        {
            const aero::VisualNodeId cid = openSample("ListBox");
            std::vector<aero::VisualNodeId> lbs;
            CollectDescendantsByTypeName(host, cid, "ListBox", lbs);
            std::vector<aero::VisualNodeId> cbs;
            CollectDescendantsByTypeName(host, cid, "ComboBox", cbs);
            Require(!lbs.empty() && !cbs.empty(), "Gallery #14 ListBox/ComboBox missing");
            const aero::VisualNode* lb0 = host.World().GetNode(lbs[0]);
            const aero::VisualNode* modeCb = host.World().GetNode(cbs[0]);
            Require(host.InvokeScriptMethod(
                        modeCb->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{2}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #14 SelectionMode ComboBox NotifyItemClicked(2) failed");
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
            // Click two items in Multiple mode -> both remain in _selectedIndices
            Require(host.InvokeScriptMethod(
                        lb0->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{1}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #14 ListBox NotifyItemClicked(1) failed");
            Require(host.InvokeScriptMethod(
                        lb0->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{3}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #14 ListBox NotifyItemClicked(3) failed");
            host.StepFrame(1.0f / 60.0f, true);
            std::int64_t selCnt = 0;
            Require(ReadListCountMember(host, lb0->scriptOwner, "_selectedIndices", selCnt) && selCnt >= 2,
                "Gallery #14 Multiple SelectionMode must retain >= 2 selected items (got " + std::to_string(selCnt) + ")");
        }

        // #15 TreeView background style (no hardcoded #FF171B24 dark fill on themed TreeView)
        {
            const aero::VisualNodeId cid = openSample("TreeView");
            std::vector<aero::VisualNodeId> tvs;
            CollectDescendantsByTypeName(host, cid, "TreeView", tvs);
            Require(!tvs.empty(), "Gallery #15 TreeView missing");
            const aero::VisualNode* tv0 = host.World().GetNode(tvs[0]);
            Require(tv0 && tv0->box.bgRgba != 0xFF241B17u,
                "Gallery #15 styled TreeView must not inherit hardcoded dark #FF171B24 constructor background");
        }

        // #16 WrapPanel Orientation / ItemWidth / ItemHeight selection changes
        {
            const aero::VisualNodeId cid = openSample("WrapPanel");
            std::vector<aero::VisualNodeId> wps;
            CollectDescendantsByTypeName(host, cid, "WrapPanel", wps);
            Require(!wps.empty(), "Gallery #16 WrapPanel missing");
            const aero::VisualNodeId wpId = wps[0];
            const aero::VisualNodeId iwCbId = FindDescendantByOwnerName(host, cid, "itemWidth");
            const aero::VisualNodeId oriCbId = FindDescendantByOwnerName(host, cid, "orientation");
            Require(iwCbId.IsValid() && oriCbId.IsValid(), "Gallery #16 WrapPanel ComboBoxes missing");

            Require(host.InvokeScriptMethod(
                        host.World().GetNode(iwCbId)->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{1}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #16 itemWidth NotifyItemClicked(1) failed");
            Require(host.InvokeScriptMethod(
                        host.World().GetNode(oriCbId)->scriptOwner,
                        "NotifyItemClicked",
                        {aero::UnifiedValue{aero::rs_rt::NullObject{}}, aero::UnifiedValue{std::int64_t{1}}, aero::UnifiedValue{std::int64_t{0}}}),
                "Gallery #16 orientation NotifyItemClicked(1) failed");
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }
            const aero::VisualNode* wpAfter = host.World().GetNode(wpId);
            Require(wpAfter && wpAfter->layout.layoutParam0 > 10.0f && wpAfter->layout.layoutInt0 == 1,
                "Gallery #16 WrapPanel must update ItemWidth (param0=" +
                std::to_string(wpAfter ? wpAfter->layout.layoutParam0 : -1.0f) +
                ") and Vertical Orientation (layoutInt0=" +
                std::to_string(wpAfter ? wpAfter->layout.layoutInt0 : -1) + ")");
        }

        // #17 ContextMenu on ListBoxItem via Style Setter + RemoveElementAction
        {
            const aero::VisualNodeId cid = openSample("ContextMenu");
            std::vector<aero::VisualNodeId> lbs;
            CollectDescendantsByTypeName(host, cid, "ListBox", lbs);
            Require(!lbs.empty(), "Gallery #17 ContextMenu ListBox missing");
            const aero::VisualNodeId lbId = lbs[0];
            std::vector<aero::VisualNodeId> items;
            CollectDescendantsByTypeName(host, lbId, "ListBoxItem", items);
            Require(items.size() == 9u, "Gallery #17 ContextMenu ListBox should start with 9 items");

            std::vector<aero::VisualNodeId> svs;
            CollectDescendantsByTypeName(host, cid, "ScrollViewer", svs);
            if (!svs.empty()) {
                (void)host.InvokeScriptMethod(
                    host.World().GetNode(svs[0])->scriptOwner,
                    "ScrollToVerticalOffset",
                    {aero::UnifiedValue{aero::FloatValue{500.0f}}});
                for (int f = 0; f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }
            }

            const aero::Rect item0Vp = NodeViewportBounds(*host.World().GetNode(items[0]));
            const float rx = item0Vp.x + item0Vp.width * 0.5f;
            const float ry = item0Vp.y + item0Vp.height * 0.5f;
            host.InjectPointerMove(rx, ry);
            host.InjectPointerButton(rx, ry, aero::PointerButton::Right, true);
            host.InjectPointerButton(rx, ry, aero::PointerButton::Right, false);
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }

            aero::VisualNodeId removeMenuItemId{};
            std::vector<aero::VisualNodeId> dfs{host.RootVisualNode()};
            while (!dfs.empty()) {
                const aero::VisualNodeId nid = dfs.back();
                dfs.pop_back();
                const aero::VisualNode* n = host.World().GetNode(nid);
                if (!n) { continue; }
                std::string tname;
                if (n->layout.visibility == aero::Visibility::Visible &&
                    TryReadNodeTypeName(host, n, tname) && tname == "MenuItem") {
                    std::string txt;
                    CollectSubtreeText(host.World(), nid, txt);
                    if (txt.find("Remove") != std::string::npos) {
                        removeMenuItemId = nid;
                        break;
                    }
                }
                for (aero::VisualNodeId ch : n->children) { dfs.push_back(ch); }
            }
            Require(removeMenuItemId.IsValid(),
                "Gallery #17 right-clicking ListBoxItem must open Style ContextMenu with 'Remove' MenuItem");

            const aero::Rect rmVp = NodeViewportBounds(*host.World().GetNode(removeMenuItemId));
            host.InjectClick(rmVp.x + rmVp.width * 0.5f, rmVp.y + rmVp.height * 0.5f);
            for (int f = 0; f < 8; ++f) { host.StepFrame(1.0f / 60.0f, f == 7); }

            std::vector<aero::VisualNodeId> itemsAfter;
            CollectDescendantsByTypeName(host, lbId, "ListBoxItem", itemsAfter);
            Require(itemsAfter.size() == 8u,
                "Gallery #17 clicking 'Remove' in ContextMenu must remove target ListBoxItem (got " +
                std::to_string(itemsAfter.size()) + ")");

            // Verify shared Style ContextMenu remains live after removing an earlier item (incl. DisposeSubtree)
            const aero::VisualNodeId removedItemId = items[0];
            if (const aero::VisualNode* removedNode = host.World().GetNode(removedItemId);
                removedNode && removedNode->scriptOwner.valid()) {
                (void)host.InvokeScriptMethod(removedNode->scriptOwner, "DisposeSubtree", {});
            }
            const aero::Rect nextVp = NodeViewportBounds(*host.World().GetNode(itemsAfter[0]));
            const float rx2 = nextVp.x + nextVp.width * 0.5f;
            const float ry2 = nextVp.y + nextVp.height * 0.5f;
            host.InjectPointerMove(rx2, ry2);
            host.InjectPointerButton(rx2, ry2, aero::PointerButton::Right, true);
            host.InjectPointerButton(rx2, ry2, aero::PointerButton::Right, false);
            for (int f = 0; f < 6; ++f) { host.StepFrame(1.0f / 60.0f, f == 5); }

            aero::VisualNodeId removeMenuItemId2{};
            std::vector<aero::VisualNodeId> dfs2{host.RootVisualNode()};
            while (!dfs2.empty()) {
                const aero::VisualNodeId nid = dfs2.back();
                dfs2.pop_back();
                const aero::VisualNode* n = host.World().GetNode(nid);
                if (!n) { continue; }
                std::string tname;
                if (n->layout.visibility == aero::Visibility::Visible &&
                    TryReadNodeTypeName(host, n, tname) && tname == "MenuItem") {
                    std::string txt;
                    CollectSubtreeText(host.World(), nid, txt);
                    if (txt.find("Remove") != std::string::npos) {
                        removeMenuItemId2 = nid;
                        break;
                    }
                }
                for (aero::VisualNodeId ch : n->children) { dfs2.push_back(ch); }
            }
            Require(removeMenuItemId2.IsValid(),
                "Gallery #17 shared Style ContextMenu must remain openable after earlier item removal & disposal");
            const aero::Rect rmVp2 = NodeViewportBounds(*host.World().GetNode(removeMenuItemId2));
            host.InjectClick(rmVp2.x + rmVp2.width * 0.5f, rmVp2.y + rmVp2.height * 0.5f);
            for (int f = 0; f < 8; ++f) { host.StepFrame(1.0f / 60.0f, f == 7); }

            std::vector<aero::VisualNodeId> itemsAfter2;
            CollectDescendantsByTypeName(host, lbId, "ListBoxItem", itemsAfter2);
            Require(itemsAfter2.size() == 7u,
                "Gallery #17 second 'Remove' via shared Style ContextMenu must remove second item (got " +
                std::to_string(itemsAfter2.size()) + ")");
        }
    }
}

void TestRound4All6Fixes(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Round4All6Fixes (CustomRender shatter+reset, UserControl click sync, Gallery TreeView)...\n";

    // A. CustomRender: multi-brick shatter (no LCG integer overflow) & round-end brick reset
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "CustomRender", &session, ci, hc, 1280u, 720u),
            "CustomRender round4 discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        const bool loaded = host.CompileAndLoad(ci, &err);
        Require(loaded, "CustomRender round4 load failed: " + err);
        for (int f = 0; loaded && f < 5; ++f) { host.StepFrame(1.0f / 60.0f, f == 4); }

        const aero::VisualNodeId gameId = loaded ? host.FindVisualByOwnerName("GameControl") : aero::VisualNodeId{};
        const aero::VisualNode* gameNode = gameId.IsValid() ? host.World().GetNode(gameId) : nullptr;
        Require(gameNode && gameNode->hasScriptOwner, "CustomRender round4 GameControl missing");

        const aero::DrawingCommandList* initList = (loaded && gameNode) ? host.DrawBuffer().FindCommands(gameId) : nullptr;
        Require(initList && initList->commands.size() >= 260u,
            "CustomRender round4 initial draw commands expected >= 260");
        if (loaded && gameNode && gameNode->hasScriptOwner && initList) {
        const std::size_t initCmdCount = initList->commands.size();

        auto readBallCenter = [&]() -> aero::Point {
            const aero::DrawingCommandList* list = host.DrawBuffer().FindCommands(gameId);
            if (!list) { return aero::Point{640.0f, 360.0f}; }
            for (auto it = list->commands.rbegin(); it != list->commands.rend(); ++it) {
                if (it->kind == aero::DrawCommandKind::FillEllipse) {
                    return aero::Point{it->rect.x + it->rect.width * 0.5f, it->rect.y + it->rect.height * 0.5f};
                }
            }
            return aero::Point{640.0f, 360.0f};
        };

        host.InjectKey(aero::KeyCode::Space, true);
        host.InjectKey(aero::KeyCode::Space, false);
        host.StepFrame(0.0f, true);

        std::int64_t lastScore = 0;
        int brickHitsSeen = 0;
        for (int step = 0; step < 900; ++step) {
            const aero::Point bc = readBallCenter();
            host.InjectPointerMove(bc.x, 650.0f);
            host.StepFrame(1.0f / 60.0f, true);

            std::int64_t score = 0;
            std::int64_t particles = 0;
            Require(ReadIntMember(host, gameNode->scriptOwner, "Score", score),
                "CustomRender Score read failed");
            Require(ReadIntMember(host, gameNode->scriptOwner, "ActiveParticleCount", particles),
                "CustomRender ActiveParticleCount read failed");
            if (score > lastScore) {
                ++brickHitsSeen;
                lastScore = score;
                Require(particles >= 12,
                    "CustomRender brick hit #" + std::to_string(brickHitsSeen) +
                    " must spawn >= 12 shatter particles (got " + std::to_string(particles) + ")");
                if (brickHitsSeen >= 3 && score >= 30) {
                    break;
                }
            }
        }
        Require(brickHitsSeen >= 3 && lastScore >= 30,
            "CustomRender round4 expected >= 3 sequential brick hits (hits=" +
            std::to_string(brickHitsSeen) + ", score=" + std::to_string(lastScore) + ")");

        // Let the ball miss the paddle so Lives reaches 0 -> round ends & all bricks reset
        bool gameOver = false;
        for (int step = 0; step < 1200; ++step) {
            const aero::Point bc = readBallCenter();
            host.InjectPointerMove(bc.x > 640.0f ? 60.0f : 1220.0f, 650.0f);
            host.StepFrame(1.0f / 60.0f, true);
            (void)ReadBoolMember(host, gameNode->scriptOwner, "IsGameOver", gameOver);
            if (gameOver) {
                break;
            }
        }
        Require(gameOver, "CustomRender round4 missing paddle should end the round");

        // Let remaining shatter particles expire so command count reflects restored bricks directly
        for (int f = 0; f < 65; ++f) { host.StepFrame(1.0f / 60.0f, f == 64); }

        bool isRunning = true;
        std::int64_t livesAfter = -1;
        std::int64_t particlesAfter = -1;
        std::string statusAfter;
        Require(ReadBoolMember(host, gameNode->scriptOwner, "IsRunning", isRunning) && !isRunning,
            "CustomRender round4 IsRunning should be false after round end");
        Require(ReadIntMember(host, gameNode->scriptOwner, "Lives", livesAfter) && livesAfter == 0,
            "CustomRender round4 Lives should be 0 after round end");
        Require(ReadStringMember(host, gameNode->scriptOwner, "StatusText", statusAfter) && statusAfter == "GAME OVER",
            "CustomRender round4 StatusText should be 'GAME OVER' (got '" + statusAfter + "')");
        Require(ReadIntMember(host, gameNode->scriptOwner, "ActiveParticleCount", particlesAfter) && particlesAfter == 0,
            "CustomRender round4 particles should clear after settling");

        const aero::DrawingCommandList* resetList = host.DrawBuffer().FindCommands(gameId);
        Require(resetList && resetList->commands.size() == initCmdCount,
            "CustomRender round4 all bricks must reset immediately at round end (expected " +
            std::to_string(initCmdCount) + " cmds, got " +
            std::to_string(resetList ? resetList->commands.size() : 0u) + ")");
        }
    }

    // B. UserControl: clicking NumericUpDown +/- buttons syncs StatStr.Value, BarStr.Value & Points Available
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "UserControl", &session, ci, hc, 800u, 600u),
            "UserControl round4 discover failed");
        aero::AeroAppHost host(hc);
        std::string err;
        const bool loaded = host.CompileAndLoad(ci, &err);
        Require(loaded, "UserControl round4 load failed: " + err);
        for (int f = 0; loaded && f < 20; ++f) { host.StepFrame(1.0f / 60.0f, f == 19); }

        const aero::VisualNodeId statStrId = loaded ? host.FindVisualByOwnerName("StatStr") : aero::VisualNodeId{};
        const aero::VisualNodeId barStrId = loaded ? host.FindVisualByOwnerName("BarStr") : aero::VisualNodeId{};
        const aero::VisualNodeId upBtnId = statStrId.IsValid() ? FindDescendantByOwnerName(host, statStrId, "UpButton") : aero::VisualNodeId{};
        const aero::VisualNodeId downBtnId = statStrId.IsValid() ? FindDescendantByOwnerName(host, statStrId, "DownButton") : aero::VisualNodeId{};
        const aero::VisualNode* statNode = statStrId.IsValid() ? host.World().GetNode(statStrId) : nullptr;
        const aero::VisualNode* barNode = barStrId.IsValid() ? host.World().GetNode(barStrId) : nullptr;
        const aero::VisualNode* upNode = upBtnId.IsValid() ? host.World().GetNode(upBtnId) : nullptr;
        const aero::VisualNode* downNode = downBtnId.IsValid() ? host.World().GetNode(downBtnId) : nullptr;
        Require(statNode && statNode->hasScriptOwner && barNode && barNode->hasScriptOwner && upNode && downNode,
            "UserControl round4 StatStr UpButton/DownButton or BarStr missing");
        if (loaded && statNode && statNode->hasScriptOwner && barNode && barNode->hasScriptOwner && upNode && downNode) {
            auto readPointsAvailableText = [&]() -> std::string {
                std::vector<aero::VisualNodeId> bfs{host.World().Root()};
                for (std::size_t bi = 0; bi < bfs.size(); ++bi) {
                    const aero::VisualNode* bn = host.World().GetNode(bfs[bi]);
                    if (!bn) { continue; }
                    if (bn->kind == aero::VisualKind::Text && bn->Text().text == "POINTS AVAILABLE:" && bn->parent.IsValid()) {
                        const aero::VisualNode* panel = host.World().GetNode(bn->parent);
                        if (panel) {
                            for (aero::VisualNodeId ch : panel->children) {
                                if (ch == bfs[bi]) { continue; }
                                const aero::VisualNode* sib = host.World().GetNode(ch);
                                if (sib && sib->kind == aero::VisualKind::Text) {
                                    return sib->Text().text;
                                }
                                std::string joined;
                                CollectSubtreeText(host.World(), ch, joined);
                                if (!joined.empty()) { return joined; }
                            }
                        }
                    }
                    for (aero::VisualNodeId ch : bn->children) { bfs.push_back(ch); }
                }
                return {};
            };

            float stat0 = 0.0f;
            float bar0 = 0.0f;
            (void)ReadFloatMember(host, statNode->scriptOwner, "Value", stat0);
            (void)ReadFloatMember(host, barNode->scriptOwner, "Value", bar0);
            Require(std::fabs(stat0 - 10.0f) < 0.05f && std::fabs(bar0 - 10.0f) < 0.05f,
                "UserControl round4 initial StatStr/BarStr Value expected ~10.0 (got stat=" +
                std::to_string(stat0) + ", bar=" + std::to_string(bar0) + ")");

            const aero::Rect upVp = NodeViewportBounds(*upNode);
            host.InjectClick(upVp.x + upVp.width * 0.5f, upVp.y + upVp.height * 0.5f);
            for (int f = 0; f < 30; ++f) { host.StepFrame(1.0f / 60.0f, f == 29); }

            float stat1 = 0.0f;
            float bar1 = 0.0f;
            (void)ReadFloatMember(host, statNode->scriptOwner, "Value", stat1);
            (void)ReadFloatMember(host, barNode->scriptOwner, "Value", bar1);
            const std::string ptsText1 = readPointsAvailableText();
            Require(std::fabs(stat1 - 11.0f) < 0.05f && std::fabs(bar1 - 11.0f) < 0.05f,
                "UserControl round4 UpButton click must sync StatStr/BarStr Value to ~11.0 (stat=" +
                std::to_string(stat1) + ", bar=" + std::to_string(bar1) + ")");
            Require(ptsText1 == "5",
                "UserControl round4 POINTS AVAILABLE MultiBinding expected '5' after UpButton (got '" + ptsText1 + "')");

            const aero::Rect downVp = NodeViewportBounds(*downNode);
            host.InjectClick(downVp.x + downVp.width * 0.5f, downVp.y + downVp.height * 0.5f);
            for (int f = 0; f < 30; ++f) { host.StepFrame(1.0f / 60.0f, f == 29); }

            float stat2 = 0.0f;
            float bar2 = 0.0f;
            (void)ReadFloatMember(host, statNode->scriptOwner, "Value", stat2);
            (void)ReadFloatMember(host, barNode->scriptOwner, "Value", bar2);
            const std::string ptsText2 = readPointsAvailableText();
            Require(std::fabs(stat2 - 10.0f) < 0.05f && std::fabs(bar2 - 10.0f) < 0.05f,
                "UserControl round4 DownButton click must sync StatStr/BarStr Value back to ~10.0 (stat=" +
                std::to_string(stat2) + ", bar=" + std::to_string(bar2) + ")");
            Require(ptsText2 == "6",
                "UserControl round4 POINTS AVAILABLE MultiBinding expected '6' after DownButton (got '" + ptsText2 + "')");
        }
    }

    // C. Gallery TreeView: single-click syncs SelectedItem + SelectedItemChanged, double-click expands/collapses
    {
        aero::AeroV3CompileInput ci;
        aero::AeroAppHostConfig hc;
        Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Gallery", &session, ci, hc, 1280u, 800u),
            "Gallery round4 discover failed");
        ci.treeShakeStdlib = true;
        ci.allowStdlibCache = false;
        aero::AeroAppHost host(hc);
        std::string err;
        Require(host.CompileAndLoad(ci, &err), "Gallery round4 load failed: " + err);
        for (int f = 0; f < 20; ++f) { host.StepFrame(1.0f / 60.0f, f == 19); }

        aero::UnifiedValue ret{};
        bool ok = false;
        Require(host.InvokeScriptMethod(
                    host.RootScriptObject(),
                    "SelectSampleByName",
                    {aero::UnifiedValue{std::string_view("TreeView")}},
                    &ret) &&
                ret.tryGetBool(ok) && ok,
            "Gallery round4 SelectSampleByName('TreeView') failed");
        for (int f = 0; f < 45; ++f) { host.StepFrame(1.0f / 60.0f, f == 44); }

        const aero::VisualNodeId c1 = host.FindVisualByOwnerName("SampleContainer1");
        const aero::VisualNodeId c2 = host.FindVisualByOwnerName("SampleContainer2");
        const aero::VisualNode* n1 = c1.IsValid() ? host.World().GetNode(c1) : nullptr;
        const aero::VisualNode* n2 = c2.IsValid() ? host.World().GetNode(c2) : nullptr;
        const aero::VisualNodeId cid = (n1 && n1->layout.visibility == aero::Visibility::Visible && !n1->children.empty())
            ? c1
            : ((n2 && n2->layout.visibility == aero::Visibility::Visible && !n2->children.empty()) ? c2 : aero::VisualNodeId{});
        Require(cid.IsValid(), "Gallery round4 active container missing");

        const aero::VisualNodeId tree1Id = FindDescendantByOwnerName(host, cid, "Tree1");
        const aero::VisualNodeId rect1Id = FindDescendantByOwnerName(host, cid, "Rect1");
        const aero::VisualNode* tree1Node = tree1Id.IsValid() ? host.World().GetNode(tree1Id) : nullptr;
        Require(tree1Node && tree1Node->hasScriptOwner && rect1Id.IsValid(),
            "Gallery round4 Tree1 or Rect1 missing");

        std::vector<aero::VisualNodeId> treeItems;
        CollectDescendantsByTypeName(host, tree1Id, "TreeViewItem", treeItems);
        aero::VisualNodeId lvl2ItemId{};
        aero::VisualNodeId lvl2TextId{};
        for (aero::VisualNodeId tid : treeItems) {
            std::vector<aero::VisualNodeId> queue{tid};
            for (std::size_t qi = 0; qi < queue.size(); ++qi) {
                const aero::VisualNode* n = host.World().GetNode(queue[qi]);
                if (!n) { continue; }
                if (queue[qi] != tid && n->hasScriptOwner) {
                    std::string tn;
                    if (ReadStringMember(host, n->scriptOwner, "__TypeName", tn) && tn == "TreeViewItem") {
                        continue;
                    }
                }
                if (n->kind == aero::VisualKind::Text && n->Text().text == "Level 2") {
                    lvl2ItemId = tid;
                    lvl2TextId = queue[qi];
                    break;
                }
                for (aero::VisualNodeId ch : n->children) { queue.push_back(ch); }
            }
            if (lvl2ItemId.IsValid()) { break; }
        }
        const aero::VisualNode* lvl2ItemNode = lvl2ItemId.IsValid() ? host.World().GetNode(lvl2ItemId) : nullptr;
        const aero::VisualNode* lvl2TextNode = lvl2TextId.IsValid() ? host.World().GetNode(lvl2TextId) : nullptr;
        Require(lvl2ItemNode && lvl2ItemNode->hasScriptOwner && lvl2TextNode,
            "Gallery round4 'Level 2' TreeViewItem or header Text node missing");

        // Single-click Level 2 header -> SelectedItem syncs on right, SelectedItemChanged fires, stays collapsed
        const aero::Rect txtVp0 = NodeViewportBounds(*lvl2TextNode);
        host.InjectClick(txtVp0.x + txtVp0.width * 0.5f, txtVp0.y + txtVp0.height * 0.5f);
        host.StepFrame(1.0f / 60.0f, false);
        host.StepFrame(1.0f / 60.0f, true);
        Require(host.AnimEngine().ActiveTrackCount() >= 1u,
            "Gallery round4 TreeView.SelectedItemChanged should start Rect1 flash animation");
        for (int f = 0; f < 38; ++f) { host.StepFrame(1.0f / 60.0f, f == 37); }

        bool selAfterSingle = false;
        bool expAfterSingle = true;
        Require(ReadBoolMember(host, lvl2ItemNode->scriptOwner, "IsSelected", selAfterSingle) && selAfterSingle,
            "Gallery round4 single click should select 'Level 2'");
        Require(ReadBoolMember(host, lvl2ItemNode->scriptOwner, "IsExpanded", expAfterSingle) && !expAfterSingle,
            "Gallery round4 single click on header must NOT expand 'Level 2'");

        aero::ObjectRef selItemRef{};
        Require(ReadObjectMember(host, tree1Node->scriptOwner, "SelectedItem", selItemRef) &&
                selItemRef == lvl2ItemNode->scriptOwner,
            "Gallery round4 Tree1.SelectedItem should reference 'Level 2' TreeViewItem");

        bool syncedRightLabel = false;
        if (tree1Node->parent.IsValid()) {
            std::vector<aero::VisualNodeId> bfs{tree1Node->parent};
            for (std::size_t bi = 0; bi < bfs.size(); ++bi) {
                const aero::VisualNodeId cur = bfs[bi];
                if (cur == tree1Id) { continue; }
                const aero::VisualNode* cn = host.World().GetNode(cur);
                if (!cn) { continue; }
                if (cn->kind == aero::VisualKind::Text && cn->Text().text == "Level 2") {
                    syncedRightLabel = true;
                    break;
                }
                for (aero::VisualNodeId ch : cn->children) { bfs.push_back(ch); }
            }
        }
        Require(syncedRightLabel,
            "Gallery round4 right-hand TextBlock bound to Tree1.SelectedItem.Header must sync to 'Level 2'");

        // Double-click #1 -> expands 'Level 2'
        const aero::Rect txtVp1 = NodeViewportBounds(*host.World().GetNode(lvl2TextId));
        host.InjectClick(txtVp1.x + txtVp1.width * 0.5f, txtVp1.y + txtVp1.height * 0.5f);
        host.StepFrame(0.05f, false);
        host.InjectClick(txtVp1.x + txtVp1.width * 0.5f, txtVp1.y + txtVp1.height * 0.5f);
        for (int f = 0; f < 40; ++f) { host.StepFrame(1.0f / 60.0f, f == 39); }
        bool expAfterDbl1 = false;
        Require(ReadBoolMember(host, lvl2ItemNode->scriptOwner, "IsExpanded", expAfterDbl1) && expAfterDbl1,
            "Gallery round4 double click #1 must expand 'Level 2'");

        // Double-click #2 -> collapses 'Level 2'
        const aero::Rect txtVp2 = NodeViewportBounds(*host.World().GetNode(lvl2TextId));
        host.InjectClick(txtVp2.x + txtVp2.width * 0.5f, txtVp2.y + txtVp2.height * 0.5f);
        host.StepFrame(0.05f, false);
        host.InjectClick(txtVp2.x + txtVp2.width * 0.5f, txtVp2.y + txtVp2.height * 0.5f);
        for (int f = 0; f < 25; ++f) { host.StepFrame(1.0f / 60.0f, f == 24); }
        bool expAfterDbl2 = true;
        Require(ReadBoolMember(host, lvl2ItemNode->scriptOwner, "IsExpanded", expAfterDbl2) && !expAfterDbl2,
            "Gallery round4 double click #2 must collapse 'Level 2'");
    }
}

void TestGalleryContainerLazyAndMemoryLeak(aero::rs_comp::CompilerSession& session, std::uint32_t clearRgba) {
    (void)clearRgba;
    std::cout << "[Regression] Gallery Container Lazy Realization & 3-Cycle Leak Check...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Gallery", &session, ci, hc, 1280u, 800u),
        "Gallery container leak test discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Gallery container leak test load failed: " + err);
    for (int f = 0; f < 10; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 9);
    }

    auto selectContainerPage = [&](const char* pageName) -> bool {
        aero::UnifiedValue ret{};
        const bool okCall = host.InvokeScriptMethod(
            host.RootScriptObject(),
            "SelectSampleByName",
            {aero::UnifiedValue{aero::rs_rt::StringValue{std::string(pageName)}}},
            &ret);
        bool okLoad = false;
        (void)ret.tryGetBool(okLoad);
        if (!okCall || !okLoad) {
            return false;
        }
        host.StepFrame(1.0f / 60.0f, false);
        host.StepFrame(1.0f / 60.0f, true);
        return true;
    };

    auto verifyLazyInteraction = [&](const char* pageName, auto&& actionFn) -> bool {
        if (!selectContainerPage(pageName)) {
            return false;
        }
        std::vector<std::uint8_t> beforeRgba;
        if (!host.RenderHeadlessRgba(1280u, 800u, beforeRgba)) {
            return false;
        }
        if (!actionFn()) {
            return false;
        }
        host.StepFrame(1.0f / 60.0f, false);
        host.StepFrame(1.0f / 60.0f, true);
        std::vector<std::uint8_t> afterRgba;
        if (!host.RenderHeadlessRgba(1280u, 800u, afterRgba)) {
            return false;
        }
        return CountChangedPixels(beforeRgba, afterRgba) > 0u;
    };

    auto sampleRootOrWorld = [&]() -> aero::VisualNodeId {
        const aero::VisualNodeId sr = host.FindVisualByOwnerName("SampleContainer");
        return sr.IsValid() ? sr : host.World().Root();
    };

    const std::array<const char*, 11> kContainerPages = {
        "ItemsControl", "ComboBox", "ListBox", "ListView", "TreeView",
        "TabControl", "Expander", "ScrollViewer", "Menu", "ContextMenu", "ToolBar"
    };

    std::size_t cycle2Nodes = 0u, cycle3Nodes = 0u;
    std::size_t cycle2Boxes = 0u, cycle3Boxes = 0u;
    std::size_t cycle2Objs = 0u, cycle3Objs = 0u;

    for (int cyc = 1; cyc <= 3; ++cyc) {
        for (const char* pg : kContainerPages) {
            Require(selectContainerPage(pg),
                "Gallery container leak test failed switching to " + std::string(pg));
        }

        if (cyc == 1) {
            Require(verifyLazyInteraction("TreeView", [&]() {
                std::vector<aero::VisualNodeId> items;
                CollectDescendantsByTypeName(host, sampleRootOrWorld(), "TreeViewItem", items);
                for (const auto tid : items) {
                    const auto* tn = host.World().GetNode(tid);
                    if (!tn || !tn->hasScriptOwner || !tn->scriptOwner.valid()) { continue; }
                    const aero::Rect vp = NodeViewportBounds(*tn);
                    if (vp.x <= 220.0f || vp.width <= 20.0f || vp.height <= 10.0f) { continue; }
                    bool isExp = true;
                    if (ReadBoolMember(host, tn->scriptOwner, "_isExpanded", isExp) && !isExp) {
                        aero::UnifiedValue icVal{};
                        std::int64_t ic = 0;
                        if (host.InvokeScriptMethod(tn->scriptOwner, "ItemCount", {}, &icVal) &&
                            icVal.tryGetInt64(ic) && ic > 0) {
                            host.InjectClick(vp.x + 10.0f, vp.y + 10.0f);
                            return true;
                        }
                    }
                }
                return false;
            }), "Gallery container lazy TreeViewItem expand failed");

            Require(verifyLazyInteraction("ComboBox", [&]() {
                std::vector<aero::VisualNodeId> cbs;
                CollectDescendantsByTypeName(host, sampleRootOrWorld(), "ComboBox", cbs);
                for (const auto cid : cbs) {
                    const auto* cn = host.World().GetNode(cid);
                    if (!cn || !cn->hasScriptOwner || !cn->scriptOwner.valid()) { continue; }
                    const aero::Rect vp = NodeViewportBounds(*cn);
                    if (vp.width > 40.0f && vp.height > 10.0f && vp.x > 220.0f) {
                        host.InjectClick(vp.x + vp.width - 14.0f, vp.y + vp.height * 0.5f);
                        return true;
                    }
                }
                return false;
            }), "Gallery container lazy ComboBox dropdown open failed");

            Require(verifyLazyInteraction("Expander", [&]() {
                std::vector<aero::VisualNodeId> exps;
                CollectDescendantsByTypeName(host, sampleRootOrWorld(), "Expander", exps);
                for (const auto eid : exps) {
                    const auto* en = host.World().GetNode(eid);
                    if (!en || !en->hasScriptOwner || !en->scriptOwner.valid()) { continue; }
                    const aero::Rect vp = NodeViewportBounds(*en);
                    if (vp.x <= 220.0f || vp.width <= 100.0f || vp.height <= 20.0f) { continue; }
                    bool isExp = true;
                    if (ReadBoolMember(host, en->scriptOwner, "_isExpanded", isExp) && !isExp) {
                        host.InjectClick(vp.x + 30.0f, vp.y + 12.0f);
                        return true;
                    }
                }
                return false;
            }), "Gallery container lazy Expander expand failed");

            Require(verifyLazyInteraction("TabControl", [&]() {
                std::vector<aero::VisualNodeId> tcs;
                CollectDescendantsByTypeName(host, sampleRootOrWorld(), "TabControl", tcs);
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
            }), "Gallery container TabControl switch failed");
        }

        aero::rs_rt::ShadowStack emptyStack;
        host.Domains().uiHeap().collectFull(emptyStack);
        if (host.World().SweepDeadObjectBoxes() > 0u) {
            host.Domains().uiHeap().collectFull(emptyStack);
        }
        (void)host.Domains().uiHeap().trimExcessCapacity();

        if (cyc == 2) {
            cycle2Nodes = host.World().LiveNodeCount();
            cycle2Boxes = host.World().LiveObjectBoxCount();
            cycle2Objs = host.Domains().uiHeap().liveObjects();
        } else if (cyc == 3) {
            cycle3Nodes = host.World().LiveNodeCount();
            cycle3Boxes = host.World().LiveObjectBoxCount();
            cycle3Objs = host.Domains().uiHeap().liveObjects();
        }
    }

    std::cout << "[MEM][CONTAINER_3CYC] c2_nodes=" << cycle2Nodes << " c3_nodes=" << cycle3Nodes
              << " c2_boxes=" << cycle2Boxes << " c3_boxes=" << cycle3Boxes
              << " c2_objs=" << cycle2Objs << " c3_objs=" << cycle3Objs << "\n";
    Require(cycle3Nodes <= cycle2Nodes + 2u,
        "Gallery 3-cycle container switching must not leak VisualNodes (c2=" +
        std::to_string(cycle2Nodes) + ", c3=" + std::to_string(cycle3Nodes) + ")");
    Require(cycle3Boxes <= cycle2Boxes + 2u,
        "Gallery 3-cycle container switching must not leak ObjectBoxes (c2=" +
        std::to_string(cycle2Boxes) + ", c3=" + std::to_string(cycle3Boxes) + ")");
    Require(cycle3Objs <= cycle2Objs + 64u,
        "Gallery 3-cycle container switching must keep uiHeap liveObjects bounded (c2=" +
        std::to_string(cycle2Objs) + ", c3=" + std::to_string(cycle3Objs) + ")");
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::vector<std::string_view> filters;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i] || argv[i][0] == '\0') {
            continue;
        }
        const std::string_view arg(argv[i]);
        if (arg == "--stdlib-dll") {
            gUseStdlibDll = true;
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '\0') {
                const fs::path cand(argv[i + 1]);
                std::error_code ec;
                const std::string ext = cand.extension().string();
                if (ext == ".dll" || ext == ".so" || ext == ".dylib" || fs::exists(cand, ec)) {
                    gStdlibDllPath = argv[++i];
                }
            }
            continue;
        }
        constexpr std::string_view kDllPrefix = "--stdlib-dll=";
        if (arg.rfind(kDllPrefix, 0u) == 0u) {
            gUseStdlibDll = true;
            gStdlibDllPath = std::string(arg.substr(kDllPrefix.size()));
            continue;
        }
        filters.emplace_back(argv[i]);
    }
    const auto want = [&](std::string_view section) {
        if (filters.empty()) { return true; }
        for (std::string_view f : filters) {
            if (f == section) { return true; }
        }
        return false;
    };

    aero::rs_comp::CompilerSession sharedSession;
    const std::uint32_t clearRgba = QuantizedClearColorRgba(aero::AeroAppHostConfig{}.clearColor);

    if (want("discover")) { TestDiscoverSampleProjectAndCache(sharedSession); }
    if (want("bench")) { TestAll23LegacyAnd5V3Samples(clearRgba); }
    if (want("subpages")) { TestGalleryStandaloneSubPages(clearRgba); }

    if (!filters.empty()) {
        if (want("backgroundblur")) { TestInteractiveBackgroundBlur(sharedSession); }
        if (want("buttons")) { TestInteractiveButtons(sharedSession); }
        if (want("customcontrol")) { TestInteractiveCustomControlAndTheme(sharedSession); }
        if (want("databinding")) { TestInteractiveDataBinding(sharedSession); }
        if (want("gallery")) { TestInteractiveGallery(sharedSession); }
        if (want("--only-gallery-subpages") || want("gallerysubpages")) {
            TestInteractiveGalleryReportedSubPages(sharedSession);
        }
        if (want("integration")) { TestInteractiveIntegration(sharedSession); }
        if (want("login")) { TestInteractiveLogin(sharedSession); }
        if (want("tictactoe")) { TestInteractiveTicTacToe(sharedSession); }
        if (want("inventory")) { TestInteractiveInventory(sharedSession); }
        if (want("questlog")) { TestInteractiveQuestLog(sharedSession); }
        if (want("localization")) { TestInteractiveLocalization(sharedSession); }
        if (want("menu3d")) { TestInteractiveMenu3D(sharedSession, clearRgba); }
        if (want("usercontrol")) { TestInteractiveUserControl(sharedSession); }
        if (want("wpf")) { TestInteractiveWpfQuickStart(sharedSession); }
        if (want("scoreboard")) { TestInteractiveScoreboard(sharedSession); }
        if (want("rive")) { TestInteractiveRive(sharedSession); }
        if (want("customrender")) { TestInteractiveCustomRender(sharedSession); }
        if (want("brushshaders")) { TestInteractiveBrushShaders(sharedSession); }
        if (want("reviewfixes")) { TestRegressionReviewFixes(sharedSession); }
        if (want("round3fixes")) { TestRound3All18Fixes(sharedSession); }
        if (want("round4fixes")) { TestRound4All6Fixes(sharedSession); }
        if (want("galleryleak")) { TestGalleryContainerLazyAndMemoryLeak(sharedSession, clearRgba); }
    }

    if (want("interactive")) {
        TestInteractiveBackgroundBlur(sharedSession);
        TestInteractiveButtons(sharedSession);
        TestInteractiveDataBinding(sharedSession);
        TestInteractiveGallery(sharedSession);
        TestInteractiveGalleryReportedSubPages(sharedSession);
        TestInteractiveIntegration(sharedSession);
        TestInteractiveLogin(sharedSession);
        TestInteractiveTicTacToe(sharedSession);
        TestInteractiveInventory(sharedSession);
        TestInteractiveQuestLog(sharedSession);
        TestInteractiveLocalization(sharedSession);
        TestInteractiveMenu3D(sharedSession, clearRgba);
        TestInteractiveUserControl(sharedSession);
        TestInteractiveWpfQuickStart(sharedSession);
        TestInteractiveCustomControlAndTheme(sharedSession);
        TestInteractiveScoreboard(sharedSession);
        TestInteractiveRive(sharedSession);
        TestInteractiveCustomRender(sharedSession);
        TestInteractiveBrushShaders(sharedSession);
        TestRegressionReviewFixes(sharedSession);
        TestRound3All18Fixes(sharedSession);
        TestRound4All6Fixes(sharedSession);
        TestGalleryContainerLazyAndMemoryLeak(sharedSession, clearRgba);
    }

    if (gFailureCount != 0) {
        std::cerr << "\nv3_legacy_samples_tests FAILED with " << gFailureCount << " failure(s).\n";
        return 1;
    }
    std::cout << "\nv3_legacy_samples_tests PASSED ("
              << (gUseStdlibDll ? "stdlib-dll mode" : "stdlib-source mode")
              << ": all 23 legacy samples + 5 V3 + 6 Gallery sub-pages + 18 interactive checks).\n";
    return 0;
}
