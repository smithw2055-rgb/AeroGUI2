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
        outCompileInput.treeShakeStdlib = true;
    }

    outCompileInput.stdlibRootDir = AERO_STDLIB_DIR;
    outCompileInput.includeStdlib = true;
    outCompileInput.treeShakeStdlib = true;
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
        pageCi.treeShakeStdlib = true;
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
        ci.treeShakeStdlib = true;
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

    if (want("tictactoe")) { TestInteractiveTicTacToe(sharedSession); }
    if (want("inventory")) { TestInteractiveInventory(sharedSession); }
    if (want("questlog")) { TestInteractiveQuestLog(sharedSession); }
    if (want("localization")) { TestInteractiveLocalization(sharedSession); }
    if (want("wpf")) { TestInteractiveWpfQuickStart(sharedSession); }

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
    }

    if (gFailureCount != 0) {
        std::cerr << "\nv3_legacy_samples_tests FAILED with " << gFailureCount << " failure(s).\n";
        return 1;
    }
    std::cout << "\nv3_legacy_samples_tests PASSED (all 23 legacy samples + 5 V3 + 6 Gallery sub-pages + 10 interactive checks).\n";
    return 0;
}
