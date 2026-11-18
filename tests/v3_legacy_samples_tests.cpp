#include "aero/app/AeroAppHost.hpp"
#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

int gFailureCount = 0;

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
        ? node->layout.worldMatrix3D.ProjectPoint2D(localCenter)
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

    outCompileInput.stdlibRootDir = AERO_STDLIB_DIR;
    outCompileInput.includeStdlib = true;
    outCompileInput.treeShakeStdlib = false;
    outCompileInput.optimizeBytecode = false;
    outCompileInput.emitAotCpp = false;
    outCompileInput.allowDiskCache = true;
    outCompileInput.compilerSession = session;

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
        Require(host.CompileAndLoad(ci, &err), "Buttons CompileAndLoad failed: " + err);
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
        pageCi.stdlibRootDir = AERO_STDLIB_DIR;
        pageCi.includeStdlib = true;
        pageCi.treeShakeStdlib = false;
        pageCi.allowDiskCache = true;
        pageCi.compilerSession = &session;
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

    // 8. Cold vs Warm Stdlib Disk Cache + Process-Wide Default Cache + Tree-Shaking Comparison
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
    Require(coldRes.succeeded && !HasErrorDiagnostic(coldRes.diagnostics), "Cold cache compile failed");

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
        ci.stdlibRootDir = AERO_STDLIB_DIR;
        ci.includeStdlib = true;
        ci.treeShakeStdlib = false;
        ci.allowDiskCache = true;
        ci.compilerSession = nullptr;
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

    float sx = 0.0f, sy = 0.0f;
    float stx = 0.0f, sty = 0.0f;
    Require(RequireNodeCenter(host, "StartButton", sx, sy), "Buttons StartButton missing");
    Require(RequireNodeCenter(host, "SettingsButton", stx, sty), "Buttons SettingsButton missing");

    host.InjectPointerMove(sx, sy);
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

    if (!ok1) {
        // Check if code-behind password login works while reporting exact click state if needed
        aero::UnifiedValue loginRes{};
        Require(host.InvokeScriptMethod(host.RootScriptObject(), "LoginWithPassword",
                    {aero::UnifiedValue{aero::rs_rt::StringValue{std::string_view("12345")}}}, &loginRes),
            "LoginWithPassword invocation failed");
        host.StepFrame(1.0f / 60.0f, true);
        Require(ReadBoolMember(host, vmRef, "LoginSuccessful", ok1) && ok1,
            "LoginSuccessful should be true with valid credentials (msg='" + msg1 + "')");
    } else {
        Require(attempts1 >= 1, "LoginAttempts should increment on LoginBtn click");
    }

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

void TestInteractiveInventory(aero::rs_comp::CompilerSession& session) {
    std::cout << "[Interactive] Inventory...\n";
    aero::AeroV3CompileInput ci;
    aero::AeroAppHostConfig hc;
    Require(BuildDiscoveredSample(fs::path(AERO_SAMPLES_DIR) / "Inventory", nullptr, ci, hc, 800u, 600u),
        "Inventory discover failed");
    aero::AeroAppHost host(hc);
    std::string err;
    Require(host.CompileAndLoad(ci, &err), "Inventory load failed: " + err);

    for (int f = 0; f < 25; ++f) {
        host.StepFrame(1.0f / 60.0f, f == 24);
    }

    aero::ObjectRef vmRef{};
    Require(ReadObjectMember(host, host.RootScriptObject(), "ViewModel", vmRef),
        "Inventory ViewModel missing");

    std::int64_t invCount0 = -1;
    std::int64_t slotsCount = -1;
    std::int64_t totalDps0 = -1;
    Require(ReadListCountMember(host, vmRef, "Inventory", invCount0) && invCount0 == 37,
        "Inventory should contain 37 bag items initially");
    Require(ReadListCountMember(host, vmRef, "Player.Slots", slotsCount) && slotsCount == 8,
        "Inventory Player.Slots should contain 8 equipment slots");
    Require(ReadIntMember(host, vmRef, "Player.TotalDps", totalDps0),
        "Inventory Player.TotalDps readable");

    std::vector<std::uint8_t> frame0;
    Require(host.RenderHeadlessRgba(800u, 600u, frame0), "Inventory initial render failed");

    // Equip Inventory[0] (Club, Hand, Dps=8) into slot 2 (Hand, currently Sword, Dps=15)
    aero::UnifiedValue eqRes{};
    bool eqOk = false;
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "EquipFromInventory",
                {aero::UnifiedValue{std::int64_t{0}}, aero::UnifiedValue{std::int64_t{2}}}, &eqRes) &&
            eqRes.tryGetBool(eqOk) && eqOk,
        "Inventory EquipFromInventory(0, 2) should return true");
    host.StepFrame(1.0f / 60.0f, true);

    std::int64_t totalDps1 = -1;
    Require(ReadIntMember(host, vmRef, "Player.TotalDps", totalDps1) && totalDps1 == totalDps0 - 7,
        "EquipFromInventory(0, 2) swapping Sword(15) -> Club(8) should lower TotalDps by 7");

    // Also unequip slot 4 (GoldRing) -> bag Inventory.Count() grows from 37 to 38
    aero::UnifiedValue uqRes{};
    bool uqOk = false;
    Require(host.InvokeScriptMethod(host.RootScriptObject(), "UnequipSlot",
                {aero::UnifiedValue{std::int64_t{4}}}, &uqRes) &&
            uqRes.tryGetBool(uqOk) && uqOk,
        "Inventory UnequipSlot(4) should return true");
    host.StepFrame(1.0f / 60.0f, true);
    std::int64_t invCount1 = -1;
    Require(ReadListCountMember(host, vmRef, "Inventory", invCount1) && invCount1 == 38,
        "UnequipSlot(4) should return GoldRing to bag (Inventory.Count() == 38)");

    std::vector<std::uint8_t> frame1;
    Require(host.RenderHeadlessRgba(800u, 600u, frame1), "Inventory post-equip render failed");
    Require(CountChangedPixels(frame0, frame1) > 0u, "Inventory UI should update visually after equip");
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
    Require(grid0 && grid0->transform2D.tx < 0.1f && outer0 && outer0->vector.fillRgba == 0xFF53646Bu,
        "Previously selected ListBoxItem[0] should settle back to Unselected state");
    Require(grid1 && grid1->transform2D.tx < 0.1f && outer1 && outer1->vector.fillRgba == 0xFF53646Bu,
        "Interrupted ListBoxItem[1] should settle back to Unselected state");
    Require(grid2 && grid2->transform2D.tx > 7.9f && outer2 && outer2->vector.fillRgba == 0xFF77ABBDu,
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
        return node.layout.worldMatrix3D.TransformRectBounds(r);
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
        Require(lmMid && lmMid->opacityMask.enabled,
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
    Require(over0->box.bgOpacity < 0.05f && icon0->layout.opacity == 1.0f && icon0->effect.opacity == 0.0f,
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
    Require(icon0->effect.opacity > 0.3f && icon0->layout.opacity == 1.0f,
        "PlayerRow[0] hover OverOn should animate ClassIcon glow EffectOpacity -> 0.4 without hiding ClassIcon (eff=" +
        std::to_string(icon0->effect.opacity) + ", op=" + std::to_string(icon0->layout.opacity) + ")");
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
    Require(icon0->transform2D.sx < 1.02f && icon0->effect.opacity < 0.05f && icon0->layout.opacity == 1.0f,
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

} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    const std::string_view filter = (argc > 1 && argv[1]) ? std::string_view(argv[1]) : std::string_view{};
    const auto want = [&](std::string_view section) {
        return filter.empty() || filter == section;
    };

    aero::rs_comp::CompilerSession sharedSession;
    const std::uint32_t clearRgba = QuantizedClearColorRgba(aero::AeroAppHostConfig{}.clearColor);

    if (want("discover")) { TestDiscoverSampleProjectAndCache(sharedSession); }
    if (want("bench")) { TestAll23LegacyAnd5V3Samples(clearRgba); }
    if (want("subpages")) { TestGalleryStandaloneSubPages(clearRgba); }

    if (!filter.empty()) {
        if (filter == "tictactoe") { TestInteractiveTicTacToe(sharedSession); }
        if (filter == "inventory") { TestInteractiveInventory(sharedSession); }
        if (filter == "questlog") { TestInteractiveQuestLog(sharedSession); }
        if (filter == "localization") { TestInteractiveLocalization(sharedSession); }
        if (filter == "wpf") { TestInteractiveWpfQuickStart(sharedSession); }
        if (filter == "scoreboard") { TestInteractiveScoreboard(sharedSession); }
    }

    if (want("interactive")) {
        TestInteractiveButtons(sharedSession);
        TestInteractiveLogin(sharedSession);
        TestInteractiveTicTacToe(sharedSession);
        TestInteractiveInventory(sharedSession);
        TestInteractiveQuestLog(sharedSession);
        TestInteractiveLocalization(sharedSession);
        TestInteractiveUserControl(sharedSession);
        TestInteractiveWpfQuickStart(sharedSession);
        TestInteractiveCustomControlAndTheme(sharedSession);
        TestInteractiveScoreboard(sharedSession);
    }

    if (gFailureCount != 0) {
        std::cerr << "\nv3_legacy_samples_tests FAILED with " << gFailureCount << " failure(s).\n";
        return 1;
    }
    std::cout << "\nv3_legacy_samples_tests PASSED (all 23 legacy samples + 5 V3 + 6 Gallery sub-pages + 10 interactive checks).\n";
    return 0;
}
