#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/host/NativeWindowHost.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace fs = std::filesystem;

namespace {

struct SamplePreset {
    const char* name;
    const char* relXamlPath;
    const char* extraDictRelPath;
    float defaultSettleSec;
    const char* description;
};

constexpr SamplePreset kPresets[] = {
    {"HelloWorld",       "HelloWorld/Data/MainPage.xaml",         nullptr,                                1.2f, "Minimal XAML typography & layout"},
    {"Login",            "Login/Data/MainPage.xaml",              "Login/Data/Resources.xaml",            1.0f, "Animated login card, TextBox, PasswordBox & Button"},
    {"Buttons",          "Buttons/Data/MainPage.xaml",            "Buttons/Data/Resources.xaml",          1.0f, "Styled interactive game menu buttons"},
    {"QuestLog",         "QuestLog/Data/MainPage.xaml",           "QuestLog/Data/Resources.xaml",         1.0f, "RPG quest panel with vector badges & gradients"},
    {"Inventory",        "Inventory/Data/MainPage.xaml",          "Inventory/Data/Resources.xaml",        3.2f, "RPG equipment slots & item grid templates"},
    {"Scoreboard",       "Scoreboard/Data/MainPage.xaml",         nullptr,                                1.0f, "Match scoreboard table & team banners"},
    {"TicTacToe",        "TicTacToe/Data/MainPage.xaml",          "TicTacToe/Data/Resources.xaml",        1.0f, "3x3 interactive board & vector X/O templates"},
    {"Localization",     "Localization/Data/MainPage.xaml",       "Localization/Data/Language-en.xaml",   1.0f, "Multi-language ResourceDictionary & dynamic lookup"},
    {"Gallery",          "Gallery/Data/MainWindow.xaml",          "Gallery/Data/Resources.xaml",          1.0f, "Control gallery shell & navigation"},
    {"Brushes",          "Gallery/Data/Samples/Brushes.xaml",     "Gallery/Data/Resources.xaml",          1.0f, "Solid, LinearGradient & RadialGradient brushes"},
    {"Gallery_Blending", "Gallery/Data/Samples/Blending.xaml",    "Gallery/Data/Resources.xaml",          1.0f, "Alpha compositing & overlapping vector shapes"},
    {"Rive",             "Rive/Data/MainPage.xaml",               nullptr,                                1.0f, "9 live Rive v7 (.riv) vector artboards & state machines"},
    {"Effects",          "Gallery/Data/Samples/Effects.xaml",     "Gallery/Data/Resources.xaml",          1.0f, "DropShadowEffect & BlurEffect showcase"},
    {"BackgroundBlur",   "BackgroundBlur/Data/MainPage.xaml",     "BackgroundBlur/Data/Resources.xaml",   1.0f, "Frosted glass panel & layered blur"},
    {"BrushShaders",     "BrushShaders/Data/MainPage.xaml",       nullptr,                                1.0f, "Procedural brush shader surface"},
    {"UserControl",      "UserControl/Data/MainPage.xaml",        "UserControl/Data/Resources.xaml",      1.0f, "Composable NumericUpDown UserControl"},
};

std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool ReadTextFile(const fs::path& p, std::string& outText) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    outText = ss.str();
    return true;
}

fs::path DiscoverSamplesRoot() {
    std::vector<fs::path> candidates;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        candidates.emplace_back(envDir);
    }
#ifdef AERO_SAMPLES_DIR
    candidates.emplace_back(AERO_SAMPLES_DIR);
#endif
    constexpr const char* kRelProbes[] = {
        "Samples",
        "samples",
        "../Samples",
        "../samples",
        "../../Samples",
        "../../samples",
        "../../../Samples",
        "../../../samples",
    };
    for (const char* rel : kRelProbes) {
        candidates.emplace_back(rel);
    }

    std::error_code ec;
    for (const auto& cand : candidates) {
        if (fs::exists(cand / "HelloWorld" / "Data" / "MainPage.xaml", ec)) {
            const fs::path canon = fs::canonical(cand, ec);
            return ec ? cand : canon;
        }
    }
    return {};
}

void RegisterContainerPassThroughShims() {
    aero::AxbPackage passThrough{};
    passThrough.rootTypeName = "UserControl";
    aero::AxbTemplateRegistry::Instance().Register("noesis:LayoutScaler", passThrough);
    aero::AxbTemplateRegistry::Instance().Register("LayoutScaler", passThrough);
    aero::AxbTemplateRegistry::Instance().Register("Decorator", passThrough);
    aero::AxbTemplateRegistry::Instance().Register("ViewboxDecorator", passThrough);
}

void PrintUsage(const fs::path& samplesRoot) {
    std::cout
        << "AeroGUI 2.0 Interactive Sample & XAML/Rive Viewer\n"
        << "Usage:\n"
        << "  aero-viewer [PresetName | path/to/file.xaml | path/to/file.riv] [options]\n\n"
        << "Options:\n"
        << "  --sample <name|path>    Preset name (e.g. Login, Rive, TicTacToe) or .xaml/.riv path\n"
        << "  --list                  List all built-in sample presets and Gallery sub-samples\n"
        << "  --rs <file.rs>          Compile & bind RealScript ViewModel file (repeatable)\n"
        << "  --width <px>            Initial window width (default: 1280)\n"
        << "  --height <px>           Initial window height (default: 720)\n"
        << "  --settle <sec>          Pre-advance animations by <sec> before showing\n"
        << "  --frames <count>        Render <count> frames and exit automatically (0 = interactive)\n"
        << "  --screenshot <out.bmp>  Capture rendered framebuffer to BMP file\n"
        << "  --export-all <dir>      Render all 16 official presets to <dir>/*.bmp and exit\n"
        << "  --software              Prefer GDI/Software surface instead of OpenGL 3.3\n"
        << "  --headless              Run offscreen headless software rasterizer (no OS window)\n"
        << "  --help                  Show this help message\n";
    if (!samplesRoot.empty()) {
        std::cout << "\nDetected Samples root: " << samplesRoot.string() << "\n";
    }
}

void PrintPresetList(const fs::path& samplesRoot) {
    std::cout << "=== 16 Built-in Official Sample Presets ===\n";
    for (const auto& p : kPresets) {
        std::printf("  %-18s -> %-36s (%s)\n", p.name, p.relXamlPath, p.description);
    }
    if (!samplesRoot.empty()) {
        const fs::path gallerySub = samplesRoot / "Gallery" / "Data" / "Samples";
        std::error_code ec;
        if (fs::is_directory(gallerySub, ec)) {
            std::cout << "\n=== Gallery Control Sub-Samples (run e.g. `aero-viewer Slider`) ===\n  ";
            std::vector<std::string> names;
            for (const auto& entry : fs::directory_iterator(gallerySub, ec)) {
                if (entry.is_regular_file() && ToLowerAscii(entry.path().extension().string()) == ".xaml") {
                    names.push_back(entry.path().stem().string());
                }
            }
            std::sort(names.begin(), names.end());
            for (std::size_t i = 0; i < names.size(); ++i) {
                std::cout << names[i] << (i + 1 < names.size() ? ", " : "\n");
            }
        }
    }
}

struct ResolvedTarget {
    std::string displayName;
    fs::path filePath;
    fs::path extraDictPath;
    float defaultSettleSec = 0.0f;
    bool isRiveFile = false;
};

bool ResolveTarget(
    std::string_view targetSpec,
    const fs::path& samplesRoot,
    ResolvedTarget& outTarget) {
    const std::string specLower = ToLowerAscii(targetSpec);

    // 1. Check built-in 16 presets (plus "Blending" alias for "Gallery_Blending")
    for (const auto& p : kPresets) {
        if (ToLowerAscii(p.name) == specLower ||
            (specLower == "blending" && std::strcmp(p.name, "Gallery_Blending") == 0)) {
            if (samplesRoot.empty()) {
                return false;
            }
            outTarget.displayName = p.name;
            outTarget.filePath = samplesRoot / p.relXamlPath;
            if (p.extraDictRelPath != nullptr) {
                outTarget.extraDictPath = samplesRoot / p.extraDictRelPath;
            }
            outTarget.defaultSettleSec = p.defaultSettleSec;
            outTarget.isRiveFile = false;
            return fs::exists(outTarget.filePath);
        }
    }

    // 2. Direct path on disk
    std::error_code ec;
    fs::path direct(targetSpec);
    if (fs::is_regular_file(direct, ec)) {
        outTarget.filePath = direct;
        outTarget.displayName = direct.stem().string();
        outTarget.isRiveFile = (ToLowerAscii(direct.extension().string()) == ".riv");
        return true;
    }

    if (!samplesRoot.empty()) {
        // 3. Relative to Samples root (e.g. "Menu3D/Data/MainPage.xaml" or "Menu3D")
        fs::path underSamples = samplesRoot / targetSpec;
        if (fs::is_regular_file(underSamples, ec)) {
            outTarget.filePath = underSamples;
            outTarget.displayName = underSamples.stem().string();
            outTarget.isRiveFile = (ToLowerAscii(underSamples.extension().string()) == ".riv");
            return true;
        }
        if (fs::is_directory(underSamples, ec)) {
            for (const char* mainCandidate : {"Data/MainPage.xaml", "Data/MainWindow.xaml"}) {
                if (fs::is_regular_file(underSamples / mainCandidate, ec)) {
                    outTarget.filePath = underSamples / mainCandidate;
                    outTarget.displayName = underSamples.filename().string();
                    if (fs::is_regular_file(underSamples / "Data/Resources.xaml", ec)) {
                        outTarget.extraDictPath = underSamples / "Data/Resources.xaml";
                    }
                    return true;
                }
            }
        }

        // 4. Gallery sub-sample shorthand (e.g. "Slider", "TabControl", "ComboBox", "ListBox")
        const fs::path gallerySub = samplesRoot / "Gallery" / "Data" / "Samples";
        if (fs::is_directory(gallerySub, ec)) {
            for (const auto& entry : fs::directory_iterator(gallerySub, ec)) {
                if (entry.is_regular_file() &&
                    ToLowerAscii(entry.path().extension().string()) == ".xaml" &&
                    ToLowerAscii(entry.path().stem().string()) == specLower) {
                    outTarget.filePath = entry.path();
                    outTarget.displayName = "Gallery/" + entry.path().stem().string();
                    outTarget.extraDictPath = samplesRoot / "Gallery" / "Data" / "Resources.xaml";
                    outTarget.defaultSettleSec = 0.5f;
                    return true;
                }
            }
        }

        // 5. Rive file shorthand (e.g. "logo.riv" or "logo")
        const fs::path riveDir = samplesRoot / "Rive" / "Data";
        if (fs::is_directory(riveDir, ec)) {
            for (const auto& entry : fs::directory_iterator(riveDir, ec)) {
                if (entry.is_regular_file() &&
                    ToLowerAscii(entry.path().extension().string()) == ".riv" &&
                    (ToLowerAscii(entry.path().filename().string()) == specLower ||
                     ToLowerAscii(entry.path().stem().string()) == specLower)) {
                    outTarget.filePath = entry.path();
                    outTarget.displayName = entry.path().filename().string();
                    outTarget.isRiveFile = true;
                    return true;
                }
            }
        }
    }

    return false;
}

struct ViewerRunOptions {
    std::uint32_t width = 1280u;
    std::uint32_t height = 720u;
    bool preferHardwareGpu = true;
    bool forceHeadless = false;
    bool hasExplicitSettle = false;
    float settleSeconds = 0.0f;
    std::uint64_t maxFrames = 0u;
    std::string screenshotBmpPath;
    std::vector<std::string> rsPaths;
    bool quiet = false;
};

int MountAndRunSingleTarget(
    const ResolvedTarget& target,
    const ViewerRunOptions& opts) {
    aero::AxbTemplateRegistry::Instance().Clear();
    aero::GradientBrushRegistry::Instance().Clear();
    aero::FontAtlas::Instance().ClearCustomFonts();
    aero::ImageAtlas::Instance().Clear();
    aero::RiveControl::ClearAssetSearchDirectories();
    RegisterContainerPassThroughShims();

    fs::path projDataDir = target.filePath.parent_path();
    while (projDataDir.has_parent_path() && projDataDir.filename() != "Data") {
        projDataDir = projDataDir.parent_path();
    }
    if (projDataDir.filename() != "Data") {
        projDataDir = target.filePath.parent_path();
    }

    aero::ImageAtlas::Instance().SetBaseDirectory(target.filePath.parent_path().string());
    aero::ImageAtlas::Instance().AddSearchDirectory(projDataDir.string());
    std::error_code ec;
    if (fs::is_directory(projDataDir / "Samples", ec)) {
        aero::ImageAtlas::Instance().AddSearchDirectory((projDataDir / "Samples").string());
    }
    aero::RiveControl::AddAssetSearchDirectory(target.filePath.parent_path().string());
    aero::RiveControl::AddAssetSearchDirectory(projDataDir.string());

    // Load co-located .ttf / .otf fonts
    std::vector<fs::path> fontFiles;
    for (auto it = fs::recursive_directory_iterator(projDataDir, ec);
         it != fs::recursive_directory_iterator();
         ++it) {
        const std::string fn = it->path().filename().string();
        if (!fn.empty() && fn[0] == '.') {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file()) continue;
        const std::string ext = ToLowerAscii(it->path().extension().string());
        if (ext == ".ttf" || ext == ".otf") {
            fontFiles.push_back(it->path());
        }
    }
    std::sort(fontFiles.begin(), fontFiles.end());
    std::size_t fontsLoaded = 0u;
    bool isFirstFont = true;
    for (const auto& fp : fontFiles) {
        if (aero::FontAtlas::Instance().LoadTrueTypeFontFromFile(
                fp.string(), fp.stem().string(), isFirstFont)) {
            ++fontsLoaded;
            isFirstFont = false;
        }
    }

    aero::ExecutionDomainContext domains;
    aero::AnimationEngine animEngine;
    std::unique_ptr<aero::Interpreter> uiInterpreter;
    std::unique_ptr<aero::DualTrackBindingEngine> bindingEngine;
    aero::StyleTriggerEngine styleEngine(&animEngine, nullptr);
    aero::IntrusivePtr<aero::UIElement> root;
    aero::ObjectRef vmRef{};
    aero::PersistentRoot vmPin;

    if (target.isRiveFile) {
        bindingEngine = std::make_unique<aero::DualTrackBindingEngine>(domains);
        bindingEngine->SetAnimationEngine(&animEngine);
        styleEngine.SetBindingEngine(bindingEngine.get());

        auto border = aero::MakeIntrusive<aero::Border>();
        border->SetBackground(aero::Color{0.07f, 0.09f, 0.13f, 1.0f});
        auto riveCtrl = aero::MakeIntrusive<aero::RiveControl>();
        riveCtrl->SetAnimationEngine(&animEngine);
        riveCtrl->SetStretch(aero::Stretch::Uniform);
        riveCtrl->SetSource(target.filePath.filename().string());
        if (!riveCtrl->IsRiveLoaded()) {
            std::cerr << "[aero-viewer] Failed to load Rive file: " << target.filePath.string() << "\n";
            return 1;
        }
        border->AddChild(riveCtrl.Get(), &domains.nativeHandles(), &domains.uiHeap());
        root = border;
    } else {
        aero::AeroCompiler compiler;

        // Register sibling ResourceDictionaries and local UserControls in Data/
        std::vector<fs::path> siblingXamls;
        for (const auto& entry : fs::directory_iterator(projDataDir, ec)) {
            if (entry.is_regular_file() &&
                ToLowerAscii(entry.path().extension().string()) == ".xaml" &&
                entry.path() != target.filePath) {
                siblingXamls.push_back(entry.path());
            }
        }
        std::sort(siblingXamls.begin(), siblingXamls.end());
        for (const auto& sibPath : siblingXamls) {
            aero::AeroCompileInput sibIn{};
            sibIn.xamlPath = sibPath.string();
            sibIn.resourceSearchRoot = projDataDir.string();
            const aero::AeroCompileResult sibRes = compiler.Compile(sibIn);
            if (sibRes.succeeded) {
                aero::AxbTemplateRegistry::Instance().RegisterResources(sibRes.axbPackage);
                const std::string stem = sibPath.stem().string();
                if (stem != "Resources" && stem.rfind("Language-", 0) != 0) {
                    if (!sibRes.axbPackage.className.empty()) {
                        aero::AxbTemplateRegistry::Instance().Register(
                            sibRes.axbPackage.className, sibRes.axbPackage);
                    }
                    aero::AxbTemplateRegistry::Instance().Register(stem, sibRes.axbPackage);
                    aero::AxbTemplateRegistry::Instance().Register("local:" + stem, sibRes.axbPackage);
                }
            }
        }

        aero::AeroCompileInput mainIn{};
        mainIn.xamlPath = target.filePath.string();
        mainIn.resourceSearchRoot = projDataDir.string();
        mainIn.resourceSearchPaths = {
            target.filePath.parent_path().string(),
            projDataDir.string(),
        };
        for (const auto& rsFile : opts.rsPaths) {
            std::string rsCode;
            if (!ReadTextFile(rsFile, rsCode)) {
                std::cerr << "[aero-viewer] Failed to read RealScript file: " << rsFile << "\n";
                return 1;
            }
            mainIn.scriptSources.push_back(aero::SourceFile{rsFile, std::move(rsCode)});
        }

        const aero::AeroCompileResult mainRes = compiler.Compile(mainIn);
        if (!mainRes.succeeded) {
            std::cerr << "[aero-viewer] XAML compile failed for " << target.filePath.string() << ":\n";
            for (const auto& d : mainRes.diagnostics) {
                std::cerr << "  " << d << "\n";
            }
            return 1;
        }
        aero::AxbTemplateRegistry::Instance().RegisterResources(mainRes.axbPackage);

        if (mainRes.programImage) {
            domains.registerProgramObservableMetadata(*mainRes.programImage);
            uiInterpreter = std::make_unique<aero::Interpreter>(mainRes.programImage, domains.uiHeapPtr());
            uiInterpreter->setExecutionDomainContext(&domains, aero::DomainKind::UiPresentation);
            bindingEngine = std::make_unique<aero::DualTrackBindingEngine>(
                domains, mainRes.programImage.get(), uiInterpreter.get());
        } else {
            bindingEngine = std::make_unique<aero::DualTrackBindingEngine>(domains);
        }
        bindingEngine->SetAnimationEngine(&animEngine);
        styleEngine.SetBindingEngine(bindingEngine.get());
        styleEngine.ImportRegistryStyles();
        styleEngine.RegisterPackageStyles(mainRes.axbPackage);

        root = aero::CreateElementByTypeName(mainRes.axbPackage.rootTypeName);
        if (!root) {
            root = aero::MakeIntrusive<aero::UserControl>();
        }
        if (!aero::MountAxbTemplate(
                root.Get(),
                mainRes.axbPackage,
                &domains,
                bindingEngine.get(),
                vmRef,
                uiInterpreter.get(),
                nullptr,
                &animEngine)) {
            std::cerr << "[aero-viewer] MountAxbTemplate failed for " << target.filePath.string() << "\n";
            return 1;
        }

        fs::path dictToApply = target.extraDictPath;
        if (dictToApply.empty() && fs::is_regular_file(projDataDir / "Resources.xaml", ec) &&
            (projDataDir / "Resources.xaml") != target.filePath) {
            dictToApply = projDataDir / "Resources.xaml";
        }
        if (!dictToApply.empty() && fs::is_regular_file(dictToApply, ec)) {
            aero::AeroCompileInput dictIn{};
            dictIn.xamlPath = dictToApply.string();
            dictIn.resourceSearchRoot = projDataDir.string();
            const aero::AeroCompileResult dictRes = compiler.Compile(dictIn);
            if (dictRes.succeeded) {
                aero::AxbTemplateRegistry::Instance().RegisterResources(dictRes.axbPackage);
                (void)aero::AxbTemplateRegistry::Instance().ApplyDynamicDictionary(
                    dictRes.axbPackage, root.Get());
                styleEngine.ImportRegistryStyles();
            }
        }

        (void)styleEngine.BindSubtreeStyles(root.Get(), true);
        (void)styleEngine.EvaluateAndApply();
    }

    root->SetAsRootVisual(true, &domains.nativeHandles(), &domains.uiHeap());

    const float effectiveSettle = opts.hasExplicitSettle
        ? opts.settleSeconds
        : ((!opts.screenshotBmpPath.empty() || opts.maxFrames > 0u) ? target.defaultSettleSec : 0.0f);
    if (effectiveSettle > 0.0f) {
        (void)animEngine.EvaluateStateMachines(effectiveSettle);
        (void)animEngine.Tick(effectiveSettle);
    }

    aero::PagedRenderTree renderTree;
    aero::ZeroCopyGpuStream gpuStream(aero::ShaderBackend::DesktopGL33, 256u);
    aero::InputEventRouter inputRouter(root.Get());
    inputRouter.SetAnimationEngine(&animEngine);

    aero::DesktopEventHost desktopHost(root.Get(), bindingEngine.get(), nullptr, &renderTree);
    desktopHost.SetInputRouter(&inputRouter);
    desktopHost.SetStyleEngine(&styleEngine);
    desktopHost.SetAnimationEngine(&animEngine);
    desktopHost.SetViewportSize(aero::Size{
        static_cast<float>(opts.width),
        static_cast<float>(opts.height)});

    aero::NativeWindowHost windowHost;
    windowHost.BindDesktopHost(&desktopHost, &renderTree, &gpuStream, &inputRouter);

    aero::NativeWindowConfig winCfg{};
    winCfg.title = "AeroGUI 2.0 Viewer - " + target.displayName;
    winCfg.width = opts.width;
    winCfg.height = opts.height;
    winCfg.preferHardwareGpu = opts.preferHardwareGpu;
    winCfg.allowHeadlessFallback = true;
    winCfg.forceHeadless = opts.forceHeadless;
    winCfg.maxPagesInRing = 256u;

    if (!windowHost.Initialize(winCfg)) {
        std::cerr << "[aero-viewer] Window initialization failed: "
                  << windowHost.LastDiagnostics() << "\n";
        return 1;
    }

    // Drain initial OS window show/size messages and render initial frame
    desktopHost.SetStateFlags(
        animEngine.HasActiveAnimations(),
        false,
        true,
        false);
    (void)windowHost.PumpEventsAndRender(0.016f, 0);
    if (windowHost.Telemetry().framesRendered == 0u) {
        (void)windowHost.TickAndPresentFrame(0.016f, true);
    }

    if (!opts.quiet) {
        std::cout << "[aero-viewer] Scene: " << target.displayName
                  << " (" << target.filePath.string() << ")\n"
                  << "              Window Mode: " << windowHost.ModeName()
                  << " (" << windowHost.Width() << "x" << windowHost.Height() << ")\n"
                  << "              Fonts Loaded: " << fontsLoaded
                  << " | Render Nodes: " << renderTree.NodeCount()
                  << " | 4KB Pages: " << renderTree.PageCount() << "\n";
        if (opts.maxFrames == 0u && !windowHost.IsHeadless()) {
            std::cout << "              Interactive loop running — close the window to exit.\n";
        }
    }

    if (opts.maxFrames > 1u) {
        (void)windowHost.RunEventLoop(opts.maxFrames - 1u);
    } else if (opts.maxFrames == 0u && !windowHost.IsHeadless()) {
        (void)windowHost.RunEventLoop(0u);
    }

    if (!opts.screenshotBmpPath.empty()) {
        fs::path bmpOut(opts.screenshotBmpPath);
        if (bmpOut.has_parent_path()) {
            fs::create_directories(bmpOut.parent_path(), ec);
        }
        if (!windowHost.SaveScreenshotBmp(bmpOut.string())) {
            std::cerr << "[aero-viewer] Failed to save BMP screenshot: " << bmpOut.string() << "\n";
            windowHost.Shutdown();
            return 1;
        }
        if (!opts.quiet) {
            std::cout << "[aero-viewer] Saved BMP screenshot: " << bmpOut.string() << "\n";
        }
    }

    windowHost.Shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path samplesRoot = DiscoverSamplesRoot();

    std::string targetSpec;
    std::string exportAllDir;
    ViewerRunOptions opts{};
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(samplesRoot);
            return 0;
        } else if (arg == "--list" || arg == "-l") {
            listOnly = true;
        } else if (arg == "--sample" && i + 1 < argc) {
            targetSpec = argv[++i];
        } else if (arg == "--rs" && i + 1 < argc) {
            opts.rsPaths.emplace_back(argv[++i]);
        } else if (arg == "--width" && i + 1 < argc) {
            opts.width = static_cast<std::uint32_t>(std::max(64, std::atoi(argv[++i])));
        } else if (arg == "--height" && i + 1 < argc) {
            opts.height = static_cast<std::uint32_t>(std::max(64, std::atoi(argv[++i])));
        } else if (arg == "--settle" && i + 1 < argc) {
            opts.hasExplicitSettle = true;
            opts.settleSeconds = static_cast<float>(std::max(0.0, std::atof(argv[++i])));
        } else if (arg == "--frames" && i + 1 < argc) {
            opts.maxFrames = static_cast<std::uint64_t>(std::max(0LL, std::atoll(argv[++i])));
        } else if (arg == "--screenshot" && i + 1 < argc) {
            opts.screenshotBmpPath = argv[++i];
        } else if (arg == "--export-all" && i + 1 < argc) {
            exportAllDir = argv[++i];
        } else if (arg == "--software") {
            opts.preferHardwareGpu = false;
        } else if (arg == "--headless") {
            opts.forceHeadless = true;
            opts.preferHardwareGpu = false;
        } else if (!arg.empty() && arg[0] != '-' && targetSpec.empty()) {
            targetSpec = arg;
        } else {
            std::cerr << "[aero-viewer] Unknown option: " << arg << "\n";
            PrintUsage(samplesRoot);
            return 1;
        }
    }

    if (listOnly) {
        PrintPresetList(samplesRoot);
        return 0;
    }

    if (!exportAllDir.empty()) {
        if (samplesRoot.empty()) {
            std::cerr << "[aero-viewer] Cannot locate Samples directory for --export-all\n";
            return 1;
        }
        std::error_code ec;
        fs::create_directories(exportAllDir, ec);
        std::cout << "[aero-viewer] Exporting all " << std::size(kPresets)
                  << " official presets (" << opts.width << "x" << opts.height
                  << ") to " << exportAllDir << "...\n";
        int idx = 1;
        for (const auto& p : kPresets) {
            ResolvedTarget rt{};
            if (!ResolveTarget(p.name, samplesRoot, rt)) {
                std::cerr << "[aero-viewer] Missing sample preset: " << p.name << "\n";
                return 1;
            }
            ViewerRunOptions singleOpts = opts;
            singleOpts.maxFrames = 1u;
            singleOpts.quiet = true;
            char fname[128]{};
            std::snprintf(fname, sizeof(fname), "%02d_%s.bmp", idx++, p.name);
            singleOpts.screenshotBmpPath = (fs::path(exportAllDir) / fname).string();
            const int rc = MountAndRunSingleTarget(rt, singleOpts);
            if (rc != 0) {
                return rc;
            }
            std::cout << "  [OK] " << singleOpts.screenshotBmpPath << "\n";
        }
        std::cout << "[aero-viewer] All 16 screenshots exported successfully.\n";
        return 0;
    }

    if (targetSpec.empty()) {
        targetSpec = "Login";
        std::cout << "[aero-viewer] No sample specified; defaulting to 'Login' "
                  << "(pass `--list` to see all presets, e.g. `aero-viewer Rive` or `aero-viewer TicTacToe`).\n";
    }

    ResolvedTarget resolved{};
    if (!ResolveTarget(targetSpec, samplesRoot, resolved)) {
        std::cerr << "[aero-viewer] Could not resolve sample or file: '" << targetSpec << "'\n";
        PrintPresetList(samplesRoot);
        return 1;
    }

    return MountAndRunSingleTarget(resolved, opts);
}
