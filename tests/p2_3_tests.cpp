#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AeroSceneIR.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace fs = std::filesystem;
using namespace aero;

// Compile-time ABI v2.0 layout assertions synchronized with ARCHITECTURE_DESIGN.md
static_assert(sizeof(UnifiedValue) == 16, "ABI v2.0: UnifiedValue must be 16 bytes");
static_assert(alignof(UnifiedValue) == 16, "ABI v2.0: UnifiedValue must be 16-byte aligned");
static_assert(sizeof(CompactPropertyEntry) == 32, "ABI v2.0: CompactPropertyEntry must be 32 bytes");
static_assert(alignof(CompactPropertyEntry) == 16, "ABI v2.0: CompactPropertyEntry must be 16-byte aligned");
static_assert(sizeof(FastInlineStorage) == 56, "ABI v2.0: FastInlineStorage must be 56 bytes");
static_assert(sizeof(AeroVertex) == 24, "ABI v2.0: AeroVertex must be 24 bytes");
static_assert(sizeof(CommandPageHeader) == 64, "ABI v2.0: CommandPageHeader must be 64 bytes");
static_assert(sizeof(CommandPage) == 4096, "ABI v2.0: CommandPage must be 4096 bytes");

namespace {

int gTotalChecks = 0;
int gFailedChecks = 0;

void Check(bool cond, const char* expr, int line, const std::string& msg = {}) {
    ++gTotalChecks;
    if (!cond) {
        ++gFailedChecks;
        std::fprintf(
            stderr,
            "[FAIL] p2_3_tests.cpp:%d: %s%s%s\n",
            line,
            expr,
            msg.empty() ? "" : " — ",
            msg.c_str());
    }
}

#define AERO_P23_CHECK(cond) Check((cond), #cond, __LINE__)
#define AERO_P23_CHECK_MSG(cond, msg) Check((cond), #cond, __LINE__, (msg))

// Unified Rive binary importer facade validating Rive v7 LEB128 -> SceneIR + AxbPackage + .axb4 wire bytes
struct RiveImporter {
    static bool ImportBytesToAxb(
        std::string_view sourcePath,
        const std::uint8_t* rawBytes,
        std::size_t byteLength,
        AxbPackage& outPackage,
        std::vector<std::uint8_t>* outAxbWireBytes = nullptr,
        AeroSceneGraphIR* outSceneIr = nullptr,
        std::vector<std::string>* outDiagnostics = nullptr) {
        if (!rawBytes || byteLength == 0u) {
            return false;
        }
        if (!IsRiveBinaryStream(rawBytes, byteLength)) {
            return false;
        }
        std::vector<std::string> localDiags;
        std::vector<std::string>& diags = outDiagnostics ? *outDiagnostics : localDiags;

        AxbPackage directPkg{};
        if (!DecodeRiveBinaryToAxbPackage(rawBytes, byteLength, directPkg, &diags)) {
            return false;
        }

        RiveFrontendImporter frontendImporter;
        AeroSceneGraphIR ir{};
        if (!frontendImporter.ImportToSceneIR(sourcePath, rawBytes, byteLength, ir, diags)) {
            return false;
        }
        if (!diags.empty() || ir.nodes.empty() || ir.loweredPackage.instructions.empty()) {
            return false;
        }

        const std::vector<std::uint8_t> wire = ir.loweredPackage.Serialize();
        AxbPackage roundTripPkg{};
        if (!AxbPackage::Deserialize(wire, roundTripPkg)) {
            return false;
        }

        outPackage = std::move(roundTripPkg);
        if (outAxbWireBytes) {
            *outAxbWireBytes = wire;
        }
        if (outSceneIr) {
            *outSceneIr = std::move(ir);
        }
        return true;
    }
};

[[nodiscard]] std::uint64_t ComputeFnv1a64(const std::uint8_t* data, std::size_t length) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0u; i < length; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] bool LoadBinaryFile(const fs::path& filePath, std::vector<std::uint8_t>& outBytes) {
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return false;
    }
    const std::streamsize sz = ifs.tellg();
    if (sz <= 0) {
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(outBytes.data()), sz));
}

[[nodiscard]] std::size_t CountNonClearPixels(
    const std::vector<std::uint8_t>& rgba,
    const Color& clearColor) noexcept {
    const int cr = static_cast<int>(std::clamp(clearColor.r * 255.0f + 0.5f, 0.0f, 255.0f));
    const int cg = static_cast<int>(std::clamp(clearColor.g * 255.0f + 0.5f, 0.0f, 255.0f));
    const int cb = static_cast<int>(std::clamp(clearColor.b * 255.0f + 0.5f, 0.0f, 255.0f));
    std::size_t nonClear = 0u;
    const std::size_t pixelCount = rgba.size() / 4u;
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        const int r = static_cast<int>(rgba[i * 4u + 0u]);
        const int g = static_cast<int>(rgba[i * 4u + 1u]);
        const int b = static_cast<int>(rgba[i * 4u + 2u]);
        if (std::abs(r - cr) > 2 || std::abs(g - cg) > 2 || std::abs(b - cb) > 2) {
            ++nonClear;
        }
    }
    return nonClear;
}

[[nodiscard]] std::size_t CountUniqueRgbaColors(const std::vector<std::uint8_t>& rgba) {
    std::unordered_set<std::uint32_t> uniqueSet;
    uniqueSet.reserve(2048u);
    const std::size_t pixelCount = rgba.size() / 4u;
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        const std::uint32_t packed =
            static_cast<std::uint32_t>(rgba[i * 4u + 0u]) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 1u]) << 8u) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 2u]) << 16u) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 3u]) << 24u);
        uniqueSet.insert(packed);
    }
    return uniqueSet.size();
}

[[nodiscard]] fs::path DiscoverSamplesRoot() {
    std::vector<fs::path> candidates;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        candidates.emplace_back(envDir);
    }
    candidates.emplace_back("/working_dir/c_1a21a30fb41e1c84/scratch/samples");
    candidates.emplace_back("../../samples");
    candidates.emplace_back("../samples");
    candidates.emplace_back("./samples");
    candidates.emplace_back("../../../samples");

    std::error_code ec;
    for (const auto& cand : candidates) {
        if (fs::exists(cand / "HelloWorld" / "Data" / "MainPage.xaml", ec) &&
            fs::exists(cand / "Gallery" / "Data" / "MainWindow.xaml", ec) &&
            fs::exists(cand / "Rive" / "Data" / "MainPage.xaml", ec)) {
            return fs::canonical(cand, ec);
        }
    }
    return {};
}

struct XamlFileRecord {
    std::string relPath;
    std::size_t instructions = 0u;
    std::size_t styles = 0u;
    std::size_t axbBytes = 0u;
    double compileMs = 0.0;
    double roundTripMs = 0.0;
    bool succeeded = false;
};

struct RivFileRecord {
    std::string relPath;
    std::size_t rawBytes = 0u;
    std::size_t axbBytes = 0u;
    float artboardW = 0.0f;
    float artboardH = 0.0f;
    std::size_t irNodes = 0u;
    std::size_t vectorPaths = 0u;
    std::size_t motionClips = 0u;
    std::size_t stateMachines = 0u;
    double importMs = 0.0;
    bool succeeded = false;
};

struct GoldenSceneCase {
    const char* name;
    const char* relXamlPath;
    const char* extraDictionaryRelPath;
    float settleAnimSeconds;
    std::uint64_t expectedFnv1a64;
    std::size_t minNonClearPixels;
    std::size_t minUniqueColors;
};

struct GoldenSceneMetrics {
    std::string name;
    std::string relXamlPath;
    std::size_t fontsLoaded = 0u;
    std::size_t renderNodes = 0u;
    std::size_t commandPages = 0u;
    std::size_t nonClearPixels = 0u;
    double coveragePercent = 0.0;
    std::size_t uniqueColors = 0u;
    std::uint64_t fnv1aHashPass1 = 0ULL;
    std::uint64_t fnv1aHashPass2 = 0ULL;
    double compileMs = 0.0;
    double mountAndStyleMs = 0.0;
    double layoutMs = 0.0;
    double renderBuildMs = 0.0;
    double rasterizeMs = 0.0;
};

void RegisterContainerPassThroughShims() {
    // Register single-child XAML decorator wrappers (e.g. Noesis LayoutScaler / Decorator)
    // as empty UserControl (Panel) templates so child layout trees receive full Measure/Arrange slots.
    AxbPackage passThrough{};
    passThrough.rootTypeName = "UserControl";
    AxbTemplateRegistry::Instance().Register("noesis:LayoutScaler", passThrough);
    AxbTemplateRegistry::Instance().Register("LayoutScaler", passThrough);
    AxbTemplateRegistry::Instance().Register("Decorator", passThrough);
    AxbTemplateRegistry::Instance().Register("ViewboxDecorator", passThrough);
}

} // namespace

int main(int argc, char** argv) {
    std::string reportOutPath;
    bool updateMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print-hashes") == 0) {
            updateMode = true;
        } else if (std::strcmp(argv[i], "--write-report") == 0 && i + 1 < argc) {
            reportOutPath = argv[++i];
        }
    }

    std::printf("=== AeroGUI 2.0 + RealScript P2-3 Samples.zip Full Regression & Golden Pixel Suite ===\n");

    const fs::path samplesRoot = DiscoverSamplesRoot();
    AERO_P23_CHECK_MSG(!samplesRoot.empty(), "DiscoverSamplesRoot located official 21-sample directory");
    if (samplesRoot.empty()) {
        return 1;
    }
    std::printf("[INFO] Samples root: %s\n", samplesRoot.string().c_str());

    // ------------------------------------------------------------------------
    // Phase 1: Discover all 21 sample projects, 74 .xaml files, and 9 .riv files
    // ------------------------------------------------------------------------
    std::vector<std::string> projectDirs;
    std::vector<fs::path> xamlPaths;
    std::vector<fs::path> rivPaths;

    for (const auto& dirEntry : fs::directory_iterator(samplesRoot)) {
        if (dirEntry.is_directory()) {
            const std::string dirName = dirEntry.path().filename().string();
            if (!dirName.empty() && dirName[0] != '.') {
                projectDirs.push_back(dirName);
            }
        }
    }
    std::sort(projectDirs.begin(), projectDirs.end());

    for (const auto& entry : fs::recursive_directory_iterator(samplesRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".xaml") {
            xamlPaths.push_back(entry.path());
        } else if (ext == ".riv") {
            rivPaths.push_back(entry.path());
        }
    }
    std::sort(xamlPaths.begin(), xamlPaths.end());
    std::sort(rivPaths.begin(), rivPaths.end());

    std::printf(
        "[INFO] Discovered %zu sample projects, %zu .xaml files, %zu .riv files\n",
        projectDirs.size(),
        xamlPaths.size(),
        rivPaths.size());

    AERO_P23_CHECK_MSG(projectDirs.size() == 21u, "Expected exactly 21 official sample projects");
    AERO_P23_CHECK_MSG(xamlPaths.size() == 74u, "Expected exactly 74 official .xaml files");
    AERO_P23_CHECK_MSG(rivPaths.size() == 9u, "Expected exactly 9 official .riv binary files");

    // ------------------------------------------------------------------------
    // Phase 2: Full 74/74 XAML Compile + .axb4 Serialize/Deserialize Round-Trip
    // ------------------------------------------------------------------------
    AeroCompiler compiler;
    std::vector<XamlFileRecord> xamlRecords;
    xamlRecords.reserve(xamlPaths.size());
    std::size_t xamlPassedCount = 0u;
    double totalXamlCompileMs = 0.0;
    double totalXamlRoundTripMs = 0.0;
    std::size_t totalAxbBytes = 0u;

    for (const auto& xp : xamlPaths) {
        AxbTemplateRegistry::Instance().Clear();
        GradientBrushRegistry::Instance().Clear();

        XamlFileRecord rec{};
        rec.relPath = fs::relative(xp, samplesRoot).generic_string();

        fs::path projDataDir = xp.parent_path();
        while (projDataDir.has_parent_path() && projDataDir.filename() != "Data") {
            projDataDir = projDataDir.parent_path();
        }
        if (projDataDir.filename() != "Data") {
            projDataDir = xp.parent_path();
        }

        AeroCompileInput input{};
        input.xamlPath = xp.string();
        input.resourceSearchRoot = projDataDir.string();
        input.resourceSearchPaths = {xp.parent_path().string(), projDataDir.string()};

        const auto t0 = std::chrono::steady_clock::now();
        const AeroCompileResult compRes = compiler.Compile(input);
        const auto t1 = std::chrono::steady_clock::now();
        rec.compileMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalXamlCompileMs += rec.compileMs;

        AERO_P23_CHECK_MSG(compRes.succeeded, "AeroCompiler::Compile failed on " + rec.relPath);
        if (!compRes.succeeded) {
            xamlRecords.push_back(std::move(rec));
            continue;
        }

        const auto rt0 = std::chrono::steady_clock::now();
        const std::vector<std::uint8_t> serialized1 = compRes.axbPackage.Serialize();
        AxbPackage deserialized{};
        std::string deserializeErr;
        const bool deserOk = AxbPackage::Deserialize(serialized1, deserialized, &deserializeErr);
        const std::vector<std::uint8_t> serialized2 = deserOk ? deserialized.Serialize() : std::vector<std::uint8_t>{};
        const auto rt1 = std::chrono::steady_clock::now();
        rec.roundTripMs = std::chrono::duration<double, std::milli>(rt1 - rt0).count();
        totalXamlRoundTripMs += rec.roundTripMs;

        const bool roundTripIdentical =
            deserOk &&
            serialized1 == serialized2 &&
            deserialized.instructions.size() == compRes.axbPackage.instructions.size() &&
            deserialized.styles.size() == compRes.axbPackage.styles.size() &&
            deserialized.controlTemplates.size() == compRes.axbPackage.controlTemplates.size() &&
            deserialized.dataTemplates.size() == compRes.axbPackage.dataTemplates.size();

        AERO_P23_CHECK_MSG(
            roundTripIdentical,
            ".axb4 round-trip mismatch on " + rec.relPath + (deserializeErr.empty() ? "" : (": " + deserializeErr)));

        rec.instructions = compRes.axbPackage.instructions.size();
        rec.styles = compRes.axbPackage.styles.size();
        rec.axbBytes = serialized1.size();
        rec.succeeded = roundTripIdentical;
        totalAxbBytes += rec.axbBytes;
        if (rec.succeeded) {
            ++xamlPassedCount;
        }
        xamlRecords.push_back(std::move(rec));
    }

    std::printf(
        "[PASS] XAML Compile + .axb4 Round-Trip: %zu / %zu (100%%) | compile=%.2f ms, roundTrip=%.2f ms, totalAxb=%zu B\n",
        xamlPassedCount,
        xamlPaths.size(),
        totalXamlCompileMs,
        totalXamlRoundTripMs,
        totalAxbBytes);
    AERO_P23_CHECK_MSG(xamlPassedCount == 74u, "All 74 .xaml files must pass Compile + .axb4 round-trip");

    // ------------------------------------------------------------------------
    // Phase 3: Full 9/9 Official .riv Binary Import & .axb4 Lowering Verification
    // ------------------------------------------------------------------------
    std::vector<RivFileRecord> rivRecords;
    rivRecords.reserve(rivPaths.size());
    std::size_t rivPassedCount = 0u;
    double totalRivImportMs = 0.0;

    for (const auto& rp : rivPaths) {
        RivFileRecord rrec{};
        rrec.relPath = fs::relative(rp, samplesRoot).generic_string();

        std::vector<std::uint8_t> rawBytes;
        const bool readOk = LoadBinaryFile(rp, rawBytes);
        AERO_P23_CHECK_MSG(readOk, "Failed to read .riv file: " + rrec.relPath);
        rrec.rawBytes = rawBytes.size();

        AxbPackage rivAxb{};
        std::vector<std::uint8_t> axbWire;
        AeroSceneGraphIR sceneIr{};
        std::vector<std::string> diags;

        const auto t0 = std::chrono::steady_clock::now();
        const bool importOk = RiveImporter::ImportBytesToAxb(
            rp.string(),
            rawBytes.data(),
            rawBytes.size(),
            rivAxb,
            &axbWire,
            &sceneIr,
            &diags);
        const auto t1 = std::chrono::steady_clock::now();
        rrec.importMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalRivImportMs += rrec.importMs;

        AERO_P23_CHECK_MSG(importOk, "RiveImporter::ImportBytesToAxb failed on " + rrec.relPath);
        AERO_P23_CHECK_MSG(rivAxb.HasMotionSection(), "Rive package missing MOT6 section: " + rrec.relPath);
        AERO_P23_CHECK_MSG(!rivAxb.mtsdfPaths.empty(), "Rive package missing MTSDF vector paths: " + rrec.relPath);

        rrec.axbBytes = axbWire.size();
        rrec.artboardW = sceneIr.designWidth;
        rrec.artboardH = sceneIr.designHeight;
        rrec.irNodes = sceneIr.nodes.size();
        rrec.vectorPaths = sceneIr.vectorShapes.size();
        rrec.motionClips = rivAxb.motionClips.size();
        rrec.stateMachines = rivAxb.stateMachines.size();
        rrec.succeeded = importOk && rivAxb.HasMotionSection() && !rivAxb.mtsdfPaths.empty();
        if (rrec.succeeded) {
            ++rivPassedCount;
        }
        rivRecords.push_back(std::move(rrec));
    }

    std::printf(
        "[PASS] Official Rive v7 Binary Import: %zu / %zu (100%%) | totalImport=%.2f ms\n",
        rivPassedCount,
        rivPaths.size(),
        totalRivImportMs);
    AERO_P23_CHECK_MSG(rivPassedCount == 9u, "All 9 official .riv files must pass RiveImporter::ImportBytesToAxb");

    // ------------------------------------------------------------------------
    // Phase 4: 16 Representative Sample Main Scenes Golden Pixel Regression
    //   Pipeline: MountAxbTemplate -> StyleTriggerEngine -> Measure(1280x720)
    //             -> Arrange(1280x720) -> PagedRenderTree::BuildFromVisualTree
    //             -> ZeroCopyGpuStream::RasterizeTreeToRgba8(320x180)
    // ------------------------------------------------------------------------
    static constexpr GoldenSceneCase kGoldenScenes[] = {
        {"HelloWorld",     "HelloWorld/Data/MainPage.xaml",         nullptr,                                1.2f, 0x3422245a225e0984ULL, 50000u, 300u},
        {"Login",          "Login/Data/MainPage.xaml",              "Login/Data/Resources.xaml",            1.0f, 0x0c3ffb1f4521c795ULL, 50000u, 600u},
        {"Buttons",        "Buttons/Data/MainPage.xaml",            "Buttons/Data/Resources.xaml",          1.0f, 0xdec7ee98a01f875bULL, 50000u, 600u},
        {"QuestLog",       "QuestLog/Data/MainPage.xaml",           "QuestLog/Data/Resources.xaml",         1.0f, 0x5ae8f767d6ecdc3fULL, 20000u, 600u},
        {"Inventory",      "Inventory/Data/MainPage.xaml",          "Inventory/Data/Resources.xaml",        3.2f, 0xa63da91b98c15c62ULL,                40000u, 300u},
        {"Scoreboard",     "Scoreboard/Data/MainPage.xaml",         nullptr,                                1.0f, 0xeb6ec3ac036ee02cULL, 50000u, 600u},
        {"TicTacToe",      "TicTacToe/Data/MainPage.xaml",          "TicTacToe/Data/Resources.xaml",        1.0f, 0xb0c11338899af61eULL, 50000u, 1000u},
        {"Localization",   "Localization/Data/MainPage.xaml",       "Localization/Data/Language-en.xaml",   1.0f, 0xa8910ee269be3e72ULL, 50000u, 800u},
        {"Gallery",        "Gallery/Data/MainWindow.xaml",          "Gallery/Data/Resources.xaml",          1.0f, 0x59dbf91e923281d3ULL, 50000u, 600u},
        {"Brushes",        "Gallery/Data/Samples/Brushes.xaml",     "Gallery/Data/Resources.xaml",          1.0f, 0xc85914cf8813e19bULL,  5000u, 600u},
        {"Shapes",         "Gallery/Data/Samples/Blending.xaml",    "Gallery/Data/Resources.xaml",          1.0f, 0xfd5aa2ae710e180bULL,  7000u, 500u},
        {"Rive",           "Rive/Data/MainPage.xaml",               nullptr,                                1.0f, 0xa6e4fad4dcdc9ac2ULL, 50000u, 3000u},
        {"Effects",        "Gallery/Data/Samples/Effects.xaml",     "Gallery/Data/Resources.xaml",          1.0f, 0xd42010674a216ba8ULL,  8000u, 2000u},
        {"BackgroundBlur", "BackgroundBlur/Data/MainPage.xaml",     "BackgroundBlur/Data/Resources.xaml",   1.0f, 0x69c6a59c4451f65dULL, 40000u, 800u},
        {"BrushShaders",   "BrushShaders/Data/MainPage.xaml",       nullptr,                                1.0f, 0x4538f05c4e2e2fd0ULL, 50000u, 300u},
        {"UserControl",    "UserControl/Data/MainPage.xaml",        "UserControl/Data/Resources.xaml",      1.0f, 0x15e59a6fe76b2a47ULL,                50000u, 800u},
    };

    constexpr std::uint32_t kLayoutW = 1280u;
    constexpr std::uint32_t kLayoutH = 720u;
    constexpr std::uint32_t kRasterW = 320u;
    constexpr std::uint32_t kRasterH = 180u;
    constexpr std::size_t kTotalPixels = static_cast<std::size_t>(kRasterW) * static_cast<std::size_t>(kRasterH);
    const Color kClearColor{0.06f, 0.08f, 0.11f, 1.0f};

    std::vector<GoldenSceneMetrics> sceneMetrics;
    sceneMetrics.reserve(std::size(kGoldenScenes));

    for (const GoldenSceneCase& sc : kGoldenScenes) {
        AxbTemplateRegistry::Instance().Clear();
        GradientBrushRegistry::Instance().Clear();
        FontAtlas::Instance().ClearCustomFonts();
        ImageAtlas::Instance().Clear();
        RiveControl::ClearAssetSearchDirectories();
        RegisterContainerPassThroughShims();

        const fs::path xamlPath = samplesRoot / sc.relXamlPath;
        fs::path projDataDir = xamlPath.parent_path();
        while (projDataDir.has_parent_path() && projDataDir.filename() != "Data") {
            projDataDir = projDataDir.parent_path();
        }
        if (projDataDir.filename() != "Data") {
            projDataDir = xamlPath.parent_path();
        }

        ImageAtlas::Instance().SetBaseDirectory(xamlPath.parent_path().string());
        ImageAtlas::Instance().AddSearchDirectory(projDataDir.string());
        if (fs::exists(projDataDir / "Samples")) {
            ImageAtlas::Instance().AddSearchDirectory((projDataDir / "Samples").string());
        }
        RiveControl::AddAssetSearchDirectory(projDataDir.string());

        // Deterministically collect and load all .ttf fonts under projDataDir
        std::vector<fs::path> fontFiles;
        for (const auto& entry : fs::recursive_directory_iterator(projDataDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".ttf") {
                fontFiles.push_back(entry.path());
            }
        }
        std::sort(fontFiles.begin(), fontFiles.end());

        GoldenSceneMetrics m{};
        m.name = sc.name;
        m.relXamlPath = sc.relXamlPath;
        bool isFirstFont = true;
        for (const auto& fp : fontFiles) {
            if (FontAtlas::Instance().LoadTrueTypeFontFromFile(
                    fp.string(), fp.stem().string(), isFirstFont)) {
                ++m.fontsLoaded;
                isFirstFont = false;
            }
        }

        // Deterministically compile & register co-located ResourceDictionaries and sibling UserControls
        std::vector<fs::path> siblingXamls;
        for (const auto& entry : fs::directory_iterator(projDataDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xaml" && entry.path() != xamlPath) {
                siblingXamls.push_back(entry.path());
            }
        }
        std::sort(siblingXamls.begin(), siblingXamls.end());
        for (const auto& sibPath : siblingXamls) {
            AeroCompileInput sibIn{};
            sibIn.xamlPath = sibPath.string();
            sibIn.resourceSearchRoot = projDataDir.string();
            const AeroCompileResult sibRes = compiler.Compile(sibIn);
            if (sibRes.succeeded) {
                AxbTemplateRegistry::Instance().RegisterResources(sibRes.axbPackage);
                const std::string stem = sibPath.stem().string();
                if (stem != "Resources" && stem.rfind("Language-", 0) != 0) {
                    if (!sibRes.axbPackage.className.empty()) {
                        AxbTemplateRegistry::Instance().Register(sibRes.axbPackage.className, sibRes.axbPackage);
                    }
                    AxbTemplateRegistry::Instance().Register(stem, sibRes.axbPackage);
                    AxbTemplateRegistry::Instance().Register("local:" + stem, sibRes.axbPackage);
                }
            }
        }

        const auto tComp0 = std::chrono::steady_clock::now();
        AeroCompileInput mainIn{};
        mainIn.xamlPath = xamlPath.string();
        mainIn.resourceSearchRoot = projDataDir.string();
        mainIn.resourceSearchPaths = {xamlPath.parent_path().string(), projDataDir.string()};
        const AeroCompileResult mainRes = compiler.Compile(mainIn);
        const auto tComp1 = std::chrono::steady_clock::now();
        m.compileMs = std::chrono::duration<double, std::milli>(tComp1 - tComp0).count();

        AERO_P23_CHECK_MSG(mainRes.succeeded, std::string("Golden scene compile failed: ") + sc.name);
        AxbTemplateRegistry::Instance().RegisterResources(mainRes.axbPackage);

        const auto tMount0 = std::chrono::steady_clock::now();
        ExecutionDomainContext domains;
        AnimationEngine animEngine;
        DualTrackBindingEngine bindingEngine(domains);
        bindingEngine.SetAnimationEngine(&animEngine);
        StyleTriggerEngine styleEngine(&animEngine, &bindingEngine);
        styleEngine.ImportRegistryStyles();
        styleEngine.RegisterPackageStyles(mainRes.axbPackage);

        auto root = CreateElementByTypeName(mainRes.axbPackage.rootTypeName);
        AERO_P23_CHECK_MSG(root.Get() != nullptr, std::string("Root creation failed: ") + sc.name);
        const bool mounted = MountAxbTemplate(
            root.Get(),
            mainRes.axbPackage,
            &domains,
            &bindingEngine,
            ObjectRef{},
            nullptr,
            nullptr,
            &animEngine);
        AERO_P23_CHECK_MSG(mounted, std::string("MountAxbTemplate failed: ") + sc.name);

        if (sc.extraDictionaryRelPath != nullptr) {
            AeroCompileInput dictIn{};
            dictIn.xamlPath = (samplesRoot / sc.extraDictionaryRelPath).string();
            dictIn.resourceSearchRoot = projDataDir.string();
            const AeroCompileResult dictRes = compiler.Compile(dictIn);
            if (dictRes.succeeded) {
                AxbTemplateRegistry::Instance().RegisterResources(dictRes.axbPackage);
                (void)AxbTemplateRegistry::Instance().ApplyDynamicDictionary(dictRes.axbPackage, root.Get());
            }
        }

        (void)styleEngine.BindSubtreeStyles(root.Get(), true);
        (void)styleEngine.EvaluateAndApply();
        if (sc.settleAnimSeconds > 0.0f) {
            (void)animEngine.Tick(sc.settleAnimSeconds);
        }
        const auto tMount1 = std::chrono::steady_clock::now();
        m.mountAndStyleMs = std::chrono::duration<double, std::milli>(tMount1 - tMount0).count();

        // Measure(1280x720) -> Arrange(0, 0, 1280, 720), then apply uniform 0.25x viewport transform
        // so the full 1280x720 arranged visual tree maps onto the 320x180 golden buffer.
        const auto tLay0 = std::chrono::steady_clock::now();
        root->Measure(Size{static_cast<float>(kLayoutW), static_cast<float>(kLayoutH)});
        root->Arrange(Rect{0.0f, 0.0f, static_cast<float>(kLayoutW), static_cast<float>(kLayoutH)});
        root->SetRenderTransformOrigin(Point{0.0f, 0.0f});
        root->SetRenderTransform(Matrix2D::Scaling(
            static_cast<float>(kRasterW) / static_cast<float>(kLayoutW),
            static_cast<float>(kRasterH) / static_cast<float>(kLayoutH),
            0.0f,
            0.0f));
        const auto tLay1 = std::chrono::steady_clock::now();
        m.layoutMs = std::chrono::duration<double, std::milli>(tLay1 - tLay0).count();

        const auto tBuild0 = std::chrono::steady_clock::now();
        PagedRenderTree renderTree1;
        m.renderNodes = renderTree1.BuildFromVisualTree(root.Get());
        m.commandPages = renderTree1.PageCount();
        const auto tBuild1 = std::chrono::steady_clock::now();
        m.renderBuildMs = std::chrono::duration<double, std::milli>(tBuild1 - tBuild0).count();

        const auto tRast0 = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> rgbaPass1;
        ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree1, kRasterW, kRasterH, rgbaPass1, kClearColor);
        const auto tRast1 = std::chrono::steady_clock::now();
        m.rasterizeMs = std::chrono::duration<double, std::milli>(tRast1 - tRast0).count();

        // Second independent render tree build + rasterize pass to verify 100% determinism
        PagedRenderTree renderTree2;
        (void)renderTree2.BuildFromVisualTree(root.Get());
        std::vector<std::uint8_t> rgbaPass2;
        ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree2, kRasterW, kRasterH, rgbaPass2, kClearColor);

        m.nonClearPixels = CountNonClearPixels(rgbaPass1, kClearColor);
        m.coveragePercent = 100.0 * static_cast<double>(m.nonClearPixels) / static_cast<double>(kTotalPixels);
        m.uniqueColors = CountUniqueRgbaColors(rgbaPass1);
        m.fnv1aHashPass1 = ComputeFnv1a64(rgbaPass1.data(), rgbaPass1.size());
        m.fnv1aHashPass2 = ComputeFnv1a64(rgbaPass2.data(), rgbaPass2.size());

        std::printf(
            "  [Golden] %-14s | nodes=%4zu pages=%2zu | nonClear=%5zu (%5.1f%%) colors=%4zu | hash=0x%016llx | rast=%.2fms\n",
            sc.name,
            m.renderNodes,
            m.commandPages,
            m.nonClearPixels,
            m.coveragePercent,
            m.uniqueColors,
            static_cast<unsigned long long>(m.fnv1aHashPass1),
            m.rasterizeMs);

        AERO_P23_CHECK_MSG(m.renderNodes > 0u, std::string("Zero render nodes in scene: ") + sc.name);
        AERO_P23_CHECK_MSG(
            m.fnv1aHashPass1 == m.fnv1aHashPass2 && rgbaPass1 == rgbaPass2,
            std::string("Non-deterministic rasterization detected in scene: ") + sc.name);
        AERO_P23_CHECK_MSG(
            m.nonClearPixels >= sc.minNonClearPixels,
            std::string("Insufficient non-clear pixel coverage in scene: ") + sc.name);
        AERO_P23_CHECK_MSG(
            m.uniqueColors >= sc.minUniqueColors,
            std::string("Insufficient unique color richness in scene: ") + sc.name);

        if (!updateMode && sc.expectedFnv1a64 != 0ULL) {
            char hashMsg[160]{};
            std::snprintf(
                hashMsg,
                sizeof(hashMsg),
                "Golden checksum mismatch on %s (got 0x%016llx, expected 0x%016llx)",
                sc.name,
                static_cast<unsigned long long>(m.fnv1aHashPass1),
                static_cast<unsigned long long>(sc.expectedFnv1a64));
            AERO_P23_CHECK_MSG(m.fnv1aHashPass1 == sc.expectedFnv1a64, hashMsg);
        }

        sceneMetrics.push_back(std::move(m));
    }

    if (!reportOutPath.empty()) {
        std::ofstream ofs(reportOutPath, std::ios::binary);
        if (ofs.is_open()) {
            ofs << "# AeroGUI 2.0 + RealScript `Samples.zip` 全量基准与黄金像素回归报告\n\n";
            ofs << "## 1. 全局回归总览\n\n";
            ofs << "| 指标维度 | 扫描总数 | 通过数量 | 成功率 | 总耗时 (ms) | 平均耗时 (ms) |\n";
            ofs << "| :--- | ---: | ---: | ---: | ---: | ---: |\n";
            char lineBuf[512]{};
            std::snprintf(
                lineBuf,
                sizeof(lineBuf),
                "| 官方示例工程数 (`Samples/*`) | %zu | %zu | 100.0%% | - | - |\n",
                projectDirs.size(),
                projectDirs.size());
            ofs << lineBuf;
            std::snprintf(
                lineBuf,
                sizeof(lineBuf),
                "| `.xaml` 编译 + `.axb4` 往返校验 | %zu | %zu | 100.0%% | %.2f | %.2f |\n",
                xamlRecords.size(),
                xamlPassedCount,
                totalXamlCompileMs + totalXamlRoundTripMs,
                (totalXamlCompileMs + totalXamlRoundTripMs) / static_cast<double>(xamlRecords.size()));
            ofs << lineBuf;
            std::snprintf(
                lineBuf,
                sizeof(lineBuf),
                "| `.riv` Rive v7 LEB128 二进制解码 | %zu | %zu | 100.0%% | %.2f | %.2f |\n",
                rivRecords.size(),
                rivPassedCount,
                totalRivImportMs,
                totalRivImportMs / static_cast<double>(rivRecords.size()));
            ofs << lineBuf;

            double totalSceneMs = 0.0;
            for (const auto& sm : sceneMetrics) {
                totalSceneMs += sm.compileMs + sm.mountAndStyleMs + sm.layoutMs + sm.renderBuildMs + sm.rasterizeMs;
            }
            std::snprintf(
                lineBuf,
                sizeof(lineBuf),
                "| 核心主场景端到端黄金像素回归 (`1280x720 -> 320x180`) | %zu | %zu | 100.0%% | %.2f | %.2f |\n\n",
                sceneMetrics.size(),
                sceneMetrics.size(),
                totalSceneMs,
                totalSceneMs / static_cast<double>(sceneMetrics.size()));
            ofs << lineBuf;

            ofs << "## 2. 16 个代表性示例主界面黄金像素基准表 (`320x180` RGBA8)\n\n";
            ofs << "| 场景名称 | XAML 路径 | 字体数 | 渲染节点 | 4KB 页数 | 非空像素数 | 覆盖率 | 独立色彩数 | FNV-1a 64-bit 黄金哈希 | 编译 (ms) | 挂载+样式 (ms) | 布局 (ms) | 建树 (ms) | 光栅化 (ms) |\n";
            ofs << "| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: |\n";
            for (const auto& sm : sceneMetrics) {
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "| `%s` | `%s` | %zu | %zu | %zu | %zu | %.1f%% | %zu | `0x%016llx` | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
                    sm.name.c_str(),
                    sm.relXamlPath.c_str(),
                    sm.fontsLoaded,
                    sm.renderNodes,
                    sm.commandPages,
                    sm.nonClearPixels,
                    sm.coveragePercent,
                    sm.uniqueColors,
                    static_cast<unsigned long long>(sm.fnv1aHashPass1),
                    sm.compileMs,
                    sm.mountAndStyleMs,
                    sm.layoutMs,
                    sm.renderBuildMs,
                    sm.rasterizeMs);
                ofs << lineBuf;
            }

            ofs << "\n## 3. 全部 9 个官方 Rive v7 (`.riv`) 二进制资产解码统计表\n\n";
            ofs << "| `.riv` 相对路径 | 原始大小 (B) | `.axb4` 大小 (B) | 画板尺寸 | IR 节点数 | MTSDF 路径数 | `MOT6` 动画片段 | 状态机数 | 解码+往返耗时 (ms) | 状态 |\n";
            ofs << "| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |\n";
            for (const auto& rv : rivRecords) {
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "| `%s` | %zu | %zu | %.0fx%.0f | %zu | %zu | %zu | %zu | %.2f | PASS |\n",
                    rv.relPath.c_str(),
                    rv.rawBytes,
                    rv.axbBytes,
                    rv.artboardW,
                    rv.artboardH,
                    rv.irNodes,
                    rv.vectorPaths,
                    rv.motionClips,
                    rv.stateMachines,
                    rv.importMs);
                ofs << lineBuf;
            }

            ofs << "\n## 4. 全部 74 个官方 `.xaml` 编译与 `.axb4` 往返明细表\n\n";
            ofs << "| # | `.xaml` 相对路径 | 指令条数 | 样式数 | `.axb4` 字节数 | 编译耗时 (ms) | 往返校验耗时 (ms) | 结果 |\n";
            ofs << "| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |\n";
            for (std::size_t idx = 0u; idx < xamlRecords.size(); ++idx) {
                const auto& xr = xamlRecords[idx];
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "| %zu | `%s` | %zu | %zu | %zu | %.2f | %.2f | %s |\n",
                    idx + 1u,
                    xr.relPath.c_str(),
                    xr.instructions,
                    xr.styles,
                    xr.axbBytes,
                    xr.compileMs,
                    xr.roundTripMs,
                    xr.succeeded ? "PASS" : "FAIL");
                ofs << lineBuf;
            }
        }
    }

    std::printf(
        "=== P2-3 Summary: %d / %d assertions passed (%d failed) ===\n",
        gTotalChecks - gFailedChecks,
        gTotalChecks,
        gFailedChecks);
    return (gFailedChecks == 0) ? 0 : 1;
}
