#include "aero/compiler/XamlApplication.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "aero/core/UiResourceContext.hpp"

namespace fs = std::filesystem;

namespace aero {

namespace {

bool ReadUtf8File(const fs::path& p, std::string& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

void CollectRsFilesRecursive(const fs::path& rootDir, std::vector<fs::path>& outFiles) {
    std::error_code ec;
    if (!fs::is_directory(rootDir, ec)) return;
    for (const auto& entry : fs::recursive_directory_iterator(rootDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fullStr = entry.path().generic_string();
        if (fullStr.find("/obj/") != std::string::npos ||
            fullStr.find("/bin/") != std::string::npos ||
            fullStr.find("/Properties/") != std::string::npos) {
            continue;
        }
        const std::string fn = entry.path().filename().string();
        if ((fn.size() > 5u && fn.substr(fn.size() - 5u) == ".g.rs") ||
            (fn.size() > 5u && fn.substr(fn.size() - 5u) == ".g.cs") ||
            (fn.size() > 12u && fn.substr(fn.size() - 12u) == ".Designer.cs") ||
            fn == "AssemblyInfo.cs") {
            continue;
        }
        const fs::path ext = entry.path().extension();
        if (ext == ".rs" || ext == ".cs") {
            outFiles.push_back(entry.path());
        }
    }
    std::sort(outFiles.begin(), outFiles.end());
}

void PreRegisterLocalUserControlsAndDictionaries(
    AeroCompiler& compiler,
    const fs::path& searchDir,
    const fs::path& skipMainXaml,
    const fs::path& skipAppXaml,
    const std::vector<std::string>& resourceSearchPaths,
    const std::vector<SourceFile>& discoveredScripts,
    std::vector<SourceFile>& outGeneratedUserControlStubs) {
    std::error_code ec;
    if (!fs::is_directory(searchDir, ec)) return;
    std::vector<fs::path> xamls;
    for (const auto& entry : fs::recursive_directory_iterator(searchDir, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".xaml") continue;
        bool skipDir = false;
        const fs::path rel = entry.path().lexically_relative(searchDir);
        for (const auto& comp : rel) {
            const std::string compStr = comp.string();
            if (compStr == "obj" || compStr == "bin" || compStr == ".git" || compStr == ".vs") {
                skipDir = true;
                break;
            }
        }
        if (skipDir) continue;
        if (fs::equivalent(entry.path(), skipMainXaml, ec)) continue;
        if (!skipAppXaml.empty() && fs::equivalent(entry.path(), skipAppXaml, ec)) continue;
        xamls.push_back(entry.path());
    }
    std::sort(xamls.begin(), xamls.end());
    auto extractXmlRootTag = [](std::string_view xml) -> std::string_view {
        std::size_t pos = 0u;
        if (xml.size() >= 3u &&
            static_cast<unsigned char>(xml[0]) == 0xEF &&
            static_cast<unsigned char>(xml[1]) == 0xBB &&
            static_cast<unsigned char>(xml[2]) == 0xBF) {
            pos = 3u;
        }
        while (pos < xml.size()) {
            while (pos < xml.size() && std::isspace(static_cast<unsigned char>(xml[pos]))) {
                ++pos;
            }
            if (pos >= xml.size() || xml[pos] != '<') break;
            if (xml.compare(pos, 4u, "<!--") == 0) {
                const std::size_t end = xml.find("-->", pos + 4u);
                if (end == std::string_view::npos) break;
                pos = end + 3u;
                continue;
            }
            if (pos + 1u < xml.size() && (xml[pos + 1u] == '?' || xml[pos + 1u] == '!')) {
                const std::size_t end = xml.find('>', pos + 2u);
                if (end == std::string_view::npos) break;
                pos = end + 1u;
                continue;
            }
            const std::size_t nameStart = pos + 1u;
            std::size_t nameEnd = nameStart;
            while (nameEnd < xml.size() &&
                   !std::isspace(static_cast<unsigned char>(xml[nameEnd])) &&
                   xml[nameEnd] != '>' &&
                   xml[nameEnd] != '/') {
                ++nameEnd;
            }
            std::string_view tag = xml.substr(nameStart, nameEnd - nameStart);
            if (const std::size_t colon = tag.rfind(':'); colon != std::string_view::npos) {
                tag = tag.substr(colon + 1u);
            }
            return tag;
        }
        return {};
    };
    for (const auto& xp : xamls) {
        std::string xml;
        if (!ReadUtf8File(xp, xml)) continue;
        const std::string_view rootTag = extractXmlRootTag(xml);
        if (rootTag != "UserControl" && rootTag != "ResourceDictionary") continue;
        AeroCompileInput subIn{};
        subIn.xamlPath = xp.string();
        subIn.xamlContent = std::move(xml);
        subIn.resourceSearchRoot = xp.parent_path().string();
        subIn.resourceSearchPaths = resourceSearchPaths;
        subIn.autoMergeGlobalResources = false;
        AeroCompileResult subRes = compiler.Compile(subIn);
        if (!subRes.succeeded) continue;
        if (rootTag == "UserControl" && !subRes.axbPackage.className.empty() && !discoveredScripts.empty()) {
            std::string shortUc = subRes.axbPackage.className;
            std::string modUc = "Aero.UI";
            if (const auto dot = shortUc.rfind('.'); dot != std::string::npos) {
                modUc = shortUc.substr(0u, dot);
                shortUc = shortUc.substr(dot + 1u);
            }
            auto isIdentCh = [](unsigned char c) noexcept -> bool {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            };
            auto stripCommentsAndStrings = [](std::string_view src) -> std::string {
                std::string out;
                out.reserve(src.size());
                bool inStr = false;
                for (std::size_t i = 0u; i < src.size(); ++i) {
                    if (inStr) {
                        out.push_back(' ');
                        if (src[i] == '\\' && i + 1u < src.size()) {
                            out.push_back(' ');
                            ++i;
                        } else if (src[i] == '"') {
                            inStr = false;
                        }
                        continue;
                    }
                    if (src[i] == '"') {
                        inStr = true;
                        out.push_back(' ');
                    } else if (src[i] == '/' && i + 1u < src.size() && src[i + 1u] == '/') {
                        while (i < src.size() && src[i] != '\n') ++i;
                        out.push_back('\n');
                    } else if (src[i] == '/' && i + 1u < src.size() && src[i + 1u] == '*') {
                        i += 2u;
                        while (i + 1u < src.size() && !(src[i] == '*' && src[i + 1u] == '/')) ++i;
                        ++i;
                        out.push_back(' ');
                    } else {
                        out.push_back(src[i]);
                    }
                }
                return out;
            };
            const std::string needle = "class " + shortUc;
            bool ucHasUserCs = false;
            bool ucUserHasBase = false;
            bool ucUserDefinesInitComp = false;
            bool ucUserDefinesDataContext = false;
            for (const auto& usf : discoveredScripts) {
                const std::string clean = stripCommentsAndStrings(usf.content);
                std::size_t pos = 0u;
                while ((pos = clean.find(needle, pos)) != std::string::npos) {
                    const bool leftOk = (pos == 0u) || !isIdentCh(static_cast<unsigned char>(clean[pos - 1u]));
                    const std::size_t after = pos + needle.size();
                    const bool rightOk = (after >= clean.size()) || !isIdentCh(static_cast<unsigned char>(clean[after]));
                    pos = after;
                    if (!leftOk || !rightOk) {
                        continue;
                    }
                    ucHasUserCs = true;
                    const std::size_t openBrace = clean.find('{', after);
                    if (openBrace != std::string::npos) {
                        std::string_view header = std::string_view(clean).substr(after, openBrace - after);
                        while (!header.empty() && (header.front() == ' ' || header.front() == '\t' || header.front() == '\r' || header.front() == '\n')) {
                            header.remove_prefix(1u);
                        }
                        if (!header.empty() && header.front() == ':') {
                            ucUserHasBase = true;
                        }
                        int depth = 1;
                        std::size_t bi = openBrace + 1u;
                        for (; bi < clean.size() && depth > 0; ++bi) {
                            if (clean[bi] == '{') ++depth;
                            else if (clean[bi] == '}') --depth;
                        }
                        const std::string_view body = std::string_view(clean).substr(openBrace, bi - openBrace);
                        if (body.find("void InitializeComponent(") != std::string_view::npos ||
                            body.find("void InitializeComponent (") != std::string_view::npos) {
                            ucUserDefinesInitComp = true;
                        }
                        std::size_t dPos = 0u;
                        while ((dPos = body.find("DataContext", dPos)) != std::string_view::npos) {
                            const bool dl = (dPos == 0u) || !isIdentCh(static_cast<unsigned char>(body[dPos - 1u]));
                            const std::size_t da = dPos + 11u;
                            const bool dr = (da >= body.size()) || !isIdentCh(static_cast<unsigned char>(body[da]));
                            dPos = da;
                            if (!dl || !dr) continue;
                            std::size_t k = da;
                            while (k < body.size() && (body[k] == ' ' || body[k] == '\t' || body[k] == '\r' || body[k] == '\n')) ++k;
                            if (k < body.size() && body[k] == '{') {
                                ucUserDefinesDataContext = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (ucHasUserCs) {
                const std::string ucLeaf = xp.filename().string();
                std::string ucStub = "// Auto-generated UserControl stub for " + xp.string() + "\n";
                ucStub += "module " + modUc + ";\n";
                if (modUc != "Aero.UI") ucStub += "import Aero.UI;\n";
                ucStub += "\n[Observable]\npublic partial class " + shortUc + (ucUserHasBase ? "\n{\n" : " : UserControl\n{\n");
                ucStub += "    private bool __aero_premounted = false;\n";
                ucStub += "    private bool __aero_component_loaded = false;\n";
                ucStub += "    private void __AeroEnsureLoaded() { if (!this.__aero_component_loaded && !this.__aero_premounted) { this.__aero_component_loaded = true; Aero_LoadComponent__" +
                          shortUc + "(this, \"" + ucLeaf + "\"); } }\n";
                auto mapUcTag = [](std::string_view rt) -> std::string {
                    if (const auto c = rt.rfind(':'); c != std::string_view::npos) rt = rt.substr(c + 1u);
                    if (rt == "Window") return "Window";
                    if (rt == "UserControl") return "UserControl";
                    if (rt == "Page") return "Page";
                    if (rt == "Button") return "Button";
                    if (rt == "RepeatButton") return "RepeatButton";
                    if (rt == "CheckBox") return "CheckBox";
                    if (rt == "RadioButton") return "RadioButton";
                    if (rt == "ToggleButton") return "ToggleButton";
                    if (rt == "Hyperlink") return "Hyperlink";
                    if (rt == "TextBlock" || rt == "Label") return "TextBlock";
                    if (rt == "TextBox") return "TextBox";
                    if (rt == "PasswordBox") return "PasswordBox";
                    if (rt == "Slider") return "Slider";
                    if (rt == "ProgressBar") return "ProgressBar";
                    if (rt == "ListBox") return "ListBox";
                    if (rt == "ListView") return "ListView";
                    if (rt == "ComboBox") return "ComboBox";
                    if (rt == "Expander") return "Expander";
                    if (rt == "Border") return "Border";
                    if (rt == "Grid") return "Grid";
                    if (rt == "StackPanel" || rt == "VirtualizingStackPanel") return "StackPanel";
                    if (rt == "DockPanel") return "DockPanel";
                    if (rt == "WrapPanel") return "WrapPanel";
                    if (rt == "UniformGrid") return "UniformGrid";
                    if (rt == "ScrollViewer") return "ScrollViewer";
                    if (rt == "Canvas") return "Canvas";
                    if (rt == "Image") return "Image";
                    if (rt == "GroupBox" || rt == "HeaderedContentControl") return "GroupBox";
                    if (rt == "TabControl") return "TabControl";
                    if (rt == "TabItem") return "TabItem";
                    if (rt == "TreeView") return "TreeView";
                    if (rt == "TreeViewItem") return "TreeViewItem";
                    if (rt == "Menu") return "Menu";
                    if (rt == "MenuItem") return "MenuItem";
                    if (rt == "ToolBar") return "ToolBar";
                    if (rt == "StatusBar") return "StatusBar";
                    if (rt == "StatusBarItem") return "StatusBarItem";
                    if (rt == "Popup") return "Popup";
                    if (rt == "ToolTip") return "ToolTip";
                    if (rt == "ContextMenu") return "ContextMenu";
                    if (rt == "GridSplitter") return "GridSplitter";
                    if (rt == "RiveControl") return "RiveControl";
                    if (rt == "ContentControl") return "ContentControl";
                    return "FrameworkElement";
                };
                for (const auto& nc : subRes.axbPackage.namedControls) {
                    if (nc.name.empty()) continue;
                    const std::string st = mapUcTag(nc.typeName);
                    ucStub += "    public handle __aero_handle_" + nc.name + ";\n";
                    ucStub += "    private " + st + " __aero_cache_" + nc.name + " = null;\n";
                    ucStub += "    public " + st + " " + nc.name + " { get { "
                              "this.__AeroEnsureLoaded(); "
                              "if (this.__aero_cache_" + nc.name + " == null) { " + st + " w = new " + st + "(); "
                              "w.__nativeRootHandle = this.__aero_handle_" + nc.name + "; "
                              "UIElement_RegisterWrapper(this.__aero_handle_" + nc.name + ", w); "
                              "this.__aero_cache_" + nc.name + " = w; } else { "
                              "this.__aero_cache_" + nc.name + ".__nativeRootHandle = this.__aero_handle_" + nc.name + "; "
                              "UIElement_RegisterWrapper(this.__aero_handle_" + nc.name + ", this.__aero_cache_" + nc.name + "); } "
                              "return this.__aero_cache_" + nc.name + "; } }\n";
                }
                if (!ucUserDefinesInitComp) {
                    ucStub += "    public void InitializeComponent() { }\n";
                }
                if (!ucUserDefinesDataContext) {
                    ucStub += "    public object DataContext { get { return GUI_GetDataContextObject(this); } set { GUI_SetDataContextObject(this, value); } }\n";
                }
                ucStub += "}\n";
                ucStub += "public bool Aero_LoadComponent__" + shortUc + "(" + shortUc + " component, string uri) { return false; }\n";
                outGeneratedUserControlStubs.push_back(SourceFile{shortUc + ".g.rs", std::move(ucStub)});
            }
        }
        AxbTemplateRegistry::Instance().Register(xp.filename().string(), subRes.axbPackage);
        if (!subRes.axbPackage.className.empty()) {
            AxbTemplateRegistry::Instance().Register(subRes.axbPackage.className, subRes.axbPackage);
        }
        if (subRes.axbPackage.rootTypeName == "ResourceDictionary") {
            const std::string leaf = xp.filename().string();
            AxbTemplateRegistry::Instance().RegisterDictionary(leaf, subRes.axbPackage);
            const std::string relUri = xp.lexically_relative(searchDir).generic_string();
            if (!relUri.empty() && relUri != leaf) {
                AxbTemplateRegistry::Instance().RegisterDictionary(relUri, subRes.axbPackage);
            }
            std::string lowerLeaf = leaf;
            for (char& ch : lowerLeaf) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (skipAppXaml.empty() && lowerLeaf == "resources.xaml") {
                AxbTemplateRegistry::Instance().RegisterResources(subRes.axbPackage);
            }
        }
    }
}

} // namespace

XamlApplication::XamlApplication() = default;
XamlApplication::~XamlApplication() = default;

bool XamlApplication::Load(std::string_view appOrWindowOrDirPath, std::string* outError) {
    XamlApplicationOptions opts{};
    opts.appOrWindowXamlPath = std::string(appOrWindowOrDirPath);
    return Load(opts, outError);
}

bool XamlApplication::Load(const XamlApplicationOptions& options, std::string* outError) {
    viewportW_ = options.viewportWidth;
    viewportH_ = options.viewportHeight;
    ImageAtlas::Instance().ClearSearchDirectories();
    RiveControl::ClearAssetSearchDirectories();
    UiResourceContext::Current().ClearXamlSearchDirectories();

    std::error_code ec;
    fs::path targetPath(options.appOrWindowXamlPath);
    fs::path baseDir;
    fs::path appXamlPath;
    fs::path windowXamlPath;

    auto tryResolveXamlInDir = [&](const fs::path& dir) -> bool {
        if (fs::is_regular_file(dir / "App.xaml", ec)) {
            appXamlPath = dir / "App.xaml";
            baseDir = dir;
            return true;
        }
        if (fs::is_regular_file(dir / "Data/App.xaml", ec)) {
            appXamlPath = dir / "Data/App.xaml";
            baseDir = dir / "Data";
            return true;
        }
        if (fs::is_regular_file(dir / "MainWindow.xaml", ec)) {
            windowXamlPath = dir / "MainWindow.xaml";
            baseDir = dir;
            return true;
        }
        if (fs::is_regular_file(dir / "Data/MainWindow.xaml", ec)) {
            windowXamlPath = dir / "Data/MainWindow.xaml";
            baseDir = dir / "Data";
            return true;
        }
        if (fs::is_regular_file(dir / "Data/MainPage.xaml", ec)) {
            windowXamlPath = dir / "Data/MainPage.xaml";
            baseDir = dir / "Data";
            return true;
        }
        return false;
    };

    auto tryResolveFromSln = [&](const fs::path& slnFile) -> bool {
        std::string slnText;
        if (!ReadUtf8File(slnFile, slnText)) return false;
        const fs::path slnDir = slnFile.parent_path();
        std::vector<fs::path> candidateDirs;
        std::size_t scan = 0u;
        while (scan < slnText.size()) {
            const std::size_t csPos = slnText.find(".csproj\"", scan);
            if (csPos == std::string::npos) break;
            const std::size_t quoteStart = slnText.rfind('"', csPos);
            if (quoteStart != std::string::npos) {
                std::string rel = slnText.substr(quoteStart + 1u, (csPos + 7u) - (quoteStart + 1u));
                std::replace(rel.begin(), rel.end(), '\\', '/');
                if (!rel.empty()) {
                    candidateDirs.push_back((slnDir / fs::path(rel)).parent_path());
                }
            }
            scan = csPos + 8u;
        }
        for (const fs::path& projDir : candidateDirs) {
            if (fs::is_directory(projDir, ec) && tryResolveXamlInDir(projDir)) {
                return true;
            }
        }
        return false;
    };

    auto tryResolveInSubdirs = [&](const fs::path& rootDir) -> bool {
        std::vector<fs::path> subdirs;
        for (const auto& entry : fs::directory_iterator(rootDir, ec)) {
            if (entry.is_directory(ec)) {
                subdirs.push_back(entry.path());
            }
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (const fs::path& sub : subdirs) {
            if (tryResolveXamlInDir(sub)) {
                return true;
            }
        }
        return false;
    };

    if (fs::is_directory(targetPath, ec)) {
        baseDir = targetPath;
        if (!tryResolveXamlInDir(targetPath)) {
            std::vector<fs::path> slns;
            for (const auto& entry : fs::directory_iterator(targetPath, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".sln") {
                    slns.push_back(entry.path());
                }
            }
            std::sort(slns.begin(), slns.end());
            bool resolved = false;
            for (const fs::path& sln : slns) {
                if (tryResolveFromSln(sln)) {
                    resolved = true;
                    break;
                }
            }
            if (!resolved) {
                (void)tryResolveInSubdirs(targetPath);
            }
        }
    } else if (fs::is_regular_file(targetPath, ec) &&
               (targetPath.extension() == ".csproj" || targetPath.extension() == ".sln")) {
        baseDir = targetPath.parent_path();
        if (!tryResolveXamlInDir(baseDir)) {
            if (targetPath.extension() == ".sln") {
                if (!tryResolveFromSln(targetPath)) {
                    (void)tryResolveInSubdirs(baseDir);
                }
            } else {
                (void)tryResolveInSubdirs(baseDir);
            }
        }
    } else if (fs::is_regular_file(targetPath, ec)) {
        baseDir = targetPath.parent_path();
        if (targetPath.stem().string() == "App") {
            appXamlPath = targetPath;
        } else {
            windowXamlPath = targetPath;
            if (fs::is_regular_file(baseDir / "App.xaml", ec)) {
                appXamlPath = baseDir / "App.xaml";
            }
        }
    } else {
        if (outError) *outError = "XAML path not found: " + options.appOrWindowXamlPath;
        return false;
    }

    AeroCompiler compiler;
    AxbPackage appDictPkg{};
    bool hasAppDict = false;
    if (!appXamlPath.empty()) {
        std::string appXml;
        if (!ReadUtf8File(appXamlPath, appXml)) {
            if (outError) *outError = "Failed to read App.xaml: " + appXamlPath.string();
            return false;
        }
        AeroCompileInput appIn{};
        appIn.xamlPath = appXamlPath.string();
        appIn.xamlContent = std::move(appXml);
        appIn.resourceSearchRoot = baseDir.string();
        appIn.resourceSearchPaths = options.resourceSearchPaths;
        AeroCompileResult appRes = compiler.Compile(appIn);
        if (appRes.succeeded) {
            appDictPkg = appRes.axbPackage;
            hasAppDict = true;
            AxbTemplateRegistry::Instance().RegisterResources(appRes.axbPackage);
            resolvedStartupUri_ = appRes.axbPackage.startupUri;
            if (windowXamlPath.empty() && !resolvedStartupUri_.empty()) {
                windowXamlPath = appXamlPath.parent_path() / resolvedStartupUri_;
            }
        }
    }

    if (windowXamlPath.empty() || !fs::is_regular_file(windowXamlPath, ec)) {
        if (outError) *outError = "Resolved StartupUri/Window XAML not found: " + windowXamlPath.string();
        return false;
    }

    {
        const fs::path winDir = windowXamlPath.parent_path();
        ImageAtlas::Instance().SetBaseDirectory(winDir.string());
        ImageAtlas::Instance().AddSearchDirectory(winDir.string());
        RiveControl::AddAssetSearchDirectory(winDir.string());
        UiResourceContext::Current().AddXamlSearchDirectory(winDir.string());
        if (!baseDir.empty() && baseDir != winDir) {
            ImageAtlas::Instance().AddSearchDirectory(baseDir.string());
            RiveControl::AddAssetSearchDirectory(baseDir.string());
            UiResourceContext::Current().AddXamlSearchDirectory(baseDir.string());
        }
        for (const auto& extraDir : options.resourceSearchPaths) {
            if (!extraDir.empty()) {
                ImageAtlas::Instance().AddSearchDirectory(extraDir);
                RiveControl::AddAssetSearchDirectory(extraDir);
                UiResourceContext::Current().AddXamlSearchDirectory(extraDir);
            }
        }
        for (const char* sub : {"Fonts", "Assets", "Assets/Fonts", "Images", "Resources"}) {
            const fs::path cand = winDir / sub;
            if (fs::is_directory(cand, ec)) {
                ImageAtlas::Instance().AddSearchDirectory(cand.string());
                RiveControl::AddAssetSearchDirectory(cand.string());
                for (const auto& fe : fs::directory_iterator(cand, ec)) {
                    if (!fe.is_regular_file(ec)) continue;
                    std::string fext = fe.path().extension().string();
                    for (char& ch : fext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (fext == ".ttf" || fext == ".otf") {
                        (void)FontAtlas::Instance().LoadTrueTypeFontFromFile(
                            fe.path().string(),
                            fe.path().stem().string(),
                            false);
                    }
                }
            }
        }
    }

    std::string winXml;
    if (!ReadUtf8File(windowXamlPath, winXml)) {
        if (outError) *outError = "Failed to read window XAML: " + windowXamlPath.string();
        return false;
    }

    AeroCompileInput winIn{};
    winIn.xamlPath = windowXamlPath.string();
    winIn.xamlContent = std::move(winXml);
    winIn.resourceSearchRoot = windowXamlPath.parent_path().string();
    winIn.resourceSearchPaths = options.resourceSearchPaths;

    if (options.autoDiscoverScripts) {
        std::vector<fs::path> rsPaths;
        CollectRsFilesRecursive(windowXamlPath.parent_path(), rsPaths);
        for (const auto& rp : rsPaths) {
            std::string rsContent;
            if (ReadUtf8File(rp, rsContent)) {
                winIn.scriptSources.push_back(SourceFile{rp.string(), std::move(rsContent)});
            }
        }
    }
    for (const auto& extraPath : options.extraScriptPaths) {
        std::string rsContent;
        if (ReadUtf8File(extraPath, rsContent)) {
            winIn.scriptSources.push_back(SourceFile{extraPath, std::move(rsContent)});
        }
    }

    std::vector<SourceFile> ucStubs;
    PreRegisterLocalUserControlsAndDictionaries(
        compiler,
        windowXamlPath.parent_path(),
        windowXamlPath,
        appXamlPath,
        options.resourceSearchPaths,
        winIn.scriptSources,
        ucStubs);
    for (auto& us : ucStubs) {
        winIn.scriptSources.push_back(std::move(us));
    }

    compileResult_ = compiler.Compile(winIn);
    if (!compileResult_.succeeded) {
        if (outError) {
            std::string msg = "XAML+Script compile failed:";
            for (const auto& d : compileResult_.diagnostics) msg += std::string(1, char(10)) + "  " + d;
            *outError = std::move(msg);
        }
        return false;
    }

    AxbTemplateRegistry::Instance().RegisterResources(compileResult_.axbPackage);
    const std::string leafName = windowXamlPath.filename().string();
    AxbTemplateRegistry::Instance().Register(leafName, compileResult_.axbPackage);
    if (!compileResult_.axbPackage.className.empty()) {
        AxbTemplateRegistry::Instance().Register(compileResult_.axbPackage.className, compileResult_.axbPackage);
    }

    animationEngine_ = std::make_unique<AnimationEngine>();
    styleEngine_ = std::make_unique<StyleTriggerEngine>();
    styleEngine_->SetAnimationEngine(animationEngine_.get());

    rootElement_ = CreateElementByTypeName(compileResult_.axbPackage.rootTypeName);
    if (!rootElement_) {
        rootElement_ = MakeIntrusive<Window>();
    }
    rootElement_->SetAsRootVisual(true, &domains_.nativeHandles(), &domains_.uiHeap());

    if (compileResult_.programImage) {
        domains_.registerProgramObservableMetadata(*compileResult_.programImage);
        nativeBindings_ = std::make_shared<BindingRegistry>();
        scriptBridge_ = std::make_unique<ScriptUiBridge>(domains_);
        scriptBridge_->RegisterCoreUiBindings(*nativeBindings_);

        interpreter_ = std::make_unique<Interpreter>(compileResult_.programImage, domains_.uiHeapPtr());
        interpreter_->setExecutionDomainContext(&domains_, DomainKind::UiPresentation);
        interpreter_->setBindingRegistry(nativeBindings_);

        bindingEngine_ = std::make_unique<DualTrackBindingEngine>(
            domains_, compileResult_.programImage.get(), interpreter_.get());
        bindingEngine_->SetAnimationEngine(animationEngine_.get());
        scriptBridge_->SetUnderlyingEngine(bindingEngine_.get());

        // Instantiate code-behind class (via CreateMainWindow factory or direct WPF constructor)
        std::string classModulePrefix;
        std::string shortClassName = compileResult_.axbPackage.className;
        if (const auto dotPos = shortClassName.rfind('.'); dotPos != std::string::npos) {
            classModulePrefix = shortClassName.substr(0u, dotPos);
            shortClassName = shortClassName.substr(dotPos + 1u);
        }

        std::vector<std::string> factoryCands;
        if (!shortClassName.empty()) {
            if (!classModulePrefix.empty()) {
                factoryCands.push_back(classModulePrefix + "::__AeroCreateInstance_" + shortClassName);
            }
            factoryCands.push_back("__AeroCreateInstance_" + shortClassName);
        }
        if (!classModulePrefix.empty()) factoryCands.push_back(classModulePrefix + "::CreateMainWindow");
        factoryCands.push_back("CreateMainWindow");
        if (!classModulePrefix.empty()) factoryCands.push_back(classModulePrefix + "::CreateViewModel");
        factoryCands.push_back("CreateViewModel");

        // Instantiate WPF App : Application (App.xaml.cs) if present and invoke OnStartup(StartupEventArgs)
        if (!appDictPkg.className.empty() || hasAppDict) {
            std::string appModPrefix;
            std::string appShortName = appDictPkg.className.empty() ? std::string("App") : appDictPkg.className;
            if (const auto dotPos = appShortName.rfind('.'); dotPos != std::string::npos) {
                appModPrefix = appShortName.substr(0u, dotPos);
                appShortName = appShortName.substr(dotPos + 1u);
            }
            const rs_sem::TypeSymbol* appSym = nullptr;
            const rs_sem::TypeSymbol* startupArgsSym = nullptr;
            for (const auto& mod : compileResult_.programImage->modules()) {
                for (const auto& ts : mod.types) {
                    if (mod.name == "Aero.UI" && ts.name == "StartupEventArgs") {
                        startupArgsSym = &ts;
                    }
                    if ((appModPrefix.empty() || mod.name == appModPrefix) && ts.name == appShortName) {
                        appSym = &ts;
                    }
                }
            }
            if (appSym) {
                RuntimeError aErr{};
                std::vector<UnifiedValue> initF(appSym->fields.size(), UnifiedValue(NullObject{}));
                std::vector<std::size_t> refF;
                for (std::size_t fi = 0u; fi < appSym->fields.size(); ++fi) {
                    if (appSym->fields[fi].name == "StartupUri") {
                        if (auto sr = domains_.uiHeap().allocateString(resolvedStartupUri_, &aErr)) {
                            initF[fi] = UnifiedValue(*sr);
                        }
                    }
                    if (appSym->fields[fi].type == rs_sem::PrimitiveType::Object ||
                        appSym->fields[fi].type == rs_sem::PrimitiveType::String ||
                        appSym->fields[fi].type == rs_sem::PrimitiveType::Array) {
                        refF.push_back(fi);
                    }
                }
                if (auto appOpt = domains_.uiHeap().allocateObject(appSym->id, std::move(initF), std::move(refF), &aErr)) {
                    appObj_ = *appOpt;
                    appPin_ = domains_.uiHeap().retain(appObj_);
                    SetActiveApplicationObject(&domains_, appObj_);
                    rs_rt::ExecutionOptions execOpts{};
                    execOpts.limits.instructionBudget = 5'000'000;
                    for (const auto& m : appSym->methods) {
                        if ((m.name == ".ctor" || m.name == appSym->name) && !m.staticMethod && m.parameters.empty() && m.id != 0) {
                            (void)interpreter_->invoke(m.id, {appObj_}, execOpts);
                            break;
                        }
                    }
                    for (const auto& m : appSym->methods) {
                        if (m.name == "OnStartup" && !m.staticMethod && m.id != 0) {
                            if (m.parameters.size() == 1u) {
                                (void)interpreter_->invoke(m.id, {appObj_}, execOpts);
                            } else if (m.parameters.size() == 2u && startupArgsSym) {
                                std::vector<UnifiedValue> sf(startupArgsSym->fields.size(), UnifiedValue(NullObject{}));
                                std::vector<std::size_t> srf;
                                for (std::size_t fi = 0u; fi < startupArgsSym->fields.size(); ++fi) {
                                    if (startupArgsSym->fields[fi].name == "Handled") sf[fi] = UnifiedValue(false);
                                    if (startupArgsSym->fields[fi].type == rs_sem::PrimitiveType::Object ||
                                        startupArgsSym->fields[fi].type == rs_sem::PrimitiveType::String ||
                                        startupArgsSym->fields[fi].type == rs_sem::PrimitiveType::Array) {
                                        srf.push_back(fi);
                                    }
                                }
                                if (auto sArg = domains_.uiHeap().allocateObject(startupArgsSym->id, std::move(sf), std::move(srf), &aErr)) {
                                    (void)interpreter_->invoke(m.id, {appObj_, *sArg}, execOpts);
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }

        SetPendingMainWindowMountTarget(&domains_, rootElement_.Get(), &compileResult_.axbPackage);

        for (const auto& fnName : factoryCands) {
            if (auto fnSym = compileResult_.programImage->findFunction(fnName)) {
                rs_rt::ExecutionOptions execOpts{};
                execOpts.limits.instructionBudget = 5'000'000;
                const auto vmExec = interpreter_->invoke(*fnSym, {}, execOpts);
                if (vmExec.succeeded) {
                    if (const auto* oref = std::get_if<ObjectRef>(&vmExec.value); oref && oref->valid()) {
                        codeBehindObj_ = *oref;
                        codeBehindPin_ = domains_.uiHeap().retain(codeBehindObj_);
                        (void)domains_.uiHeap().consumeDirtyObjects();
                        domains_.uiHeap().clearDirtyFieldMask(codeBehindObj_);
                        break;
                    }
                }
            }
        }

        if (!codeBehindObj_.valid() && !shortClassName.empty()) {
            const rs_sem::TypeSymbol* clsSym = nullptr;
            for (const auto& mod : compileResult_.programImage->modules()) {
                if (!classModulePrefix.empty() && mod.name != classModulePrefix) continue;
                for (const auto& ts : mod.types) {
                    if (ts.name == shortClassName) {
                        clsSym = &ts;
                        break;
                    }
                }
                if (clsSym) break;
            }
            if (clsSym) {
                RuntimeError allocErr{};
                const NativeHandle rootNh = rootElement_->ToNativeHandle(domains_.nativeHandles());
                std::vector<UnifiedValue> initFields(clsSym->fields.size(), UnifiedValue(NullObject{}));
                std::vector<std::size_t> refFields;
                for (std::size_t fi = 0u; fi < clsSym->fields.size(); ++fi) {
                    if (clsSym->fields[fi].name == "__nativeRootHandle") {
                        initFields[fi] = UnifiedValue(rootNh);
                    }
                    if (clsSym->fields[fi].type == rs_sem::PrimitiveType::Object ||
                        clsSym->fields[fi].type == rs_sem::PrimitiveType::String ||
                        clsSym->fields[fi].type == rs_sem::PrimitiveType::Array) {
                        refFields.push_back(fi);
                    }
                }
                if (auto objOpt = domains_.uiHeap().allocateObject(clsSym->id, std::move(initFields), std::move(refFields), &allocErr)) {
                    codeBehindObj_ = *objOpt;
                    codeBehindPin_ = domains_.uiHeap().retain(codeBehindObj_);
                    for (const auto& m : clsSym->methods) {
                        if ((m.name == ".ctor" || m.name == clsSym->name) && !m.staticMethod && m.parameters.empty() && m.id != 0) {
                            rs_rt::ExecutionOptions execOpts{};
                            execOpts.limits.instructionBudget = 5'000'000;
                            (void)interpreter_->invoke(m.id, {codeBehindObj_}, execOpts);
                            break;
                        }
                    }
                    (void)domains_.uiHeap().consumeDirtyObjects();
                    domains_.uiHeap().clearDirtyFieldMask(codeBehindObj_);
                }
            }
        }
    } else {
        bindingEngine_ = std::make_unique<DualTrackBindingEngine>(domains_);
        bindingEngine_->SetAnimationEngine(animationEngine_.get());
    }

    styleEngine_->SetBindingEngine(bindingEngine_.get());

    const bool mountedOk = MountAndWireMainWindow(
        rootElement_.Get(),
        compileResult_.axbPackage,
        domains_,
        *bindingEngine_,
        *styleEngine_,
        codeBehindObj_,
        interpreter_.get(),
        nullptr,
        animationEngine_.get(),
        &viewModelObj_);
    ClearPendingMainWindowMountTarget(&domains_);
    if (!mountedOk) {
        if (outError) *outError = "MountAndWireMainWindow failed";
        return false;
    }

    if (viewModelObj_.valid()) {
        vmPin_ = domains_.uiHeap().retain(viewModelObj_);
    }

    if (hasAppDict) {
        (void)AxbTemplateRegistry::Instance().ApplyDynamicDictionary(appDictPkg, rootElement_.Get());
        styleEngine_->ImportRegistryStyles();
        (void)styleEngine_->EvaluateAndApply();
    }

    inputRouter_ = std::make_unique<InputEventRouter>(rootElement_.Get());
    inputRouter_->SetAnimationEngine(animationEngine_.get());
    initialLayoutDone_ = false;
    renderTree_.Clear();

    Tick(0.0f);
    return true;
}

void XamlApplication::Tick(float dtSeconds) {
    if (!rootElement_) return;
    std::size_t inputCount = 0u;
    if (inputRouter_) {
        inputCount = inputRouter_->FlushQueuedEvents();
        inputRouter_->Tick(dtSeconds);
    }
    std::size_t bindUpdates = 0u;
    if (bindingEngine_) {
        bindUpdates = bindingEngine_->FlushTrackA();
    }
    std::size_t animUpdates = 0u;
    if (animationEngine_ && dtSeconds > 0.0f) {
        (void)animationEngine_->EvaluateStateMachines(dtSeconds);
        animUpdates = animationEngine_->Tick(dtSeconds);
    }
    const std::uint8_t preFlags = rootElement_->GetLayoutDirtyFlags();
    if (styleEngine_ && (!initialLayoutDone_ || inputCount > 0u || bindUpdates > 0u || animUpdates > 0u || preFlags != 0u)) {
        (void)styleEngine_->EvaluateAndApply();
    }
    const std::uint8_t layFlags = rootElement_->GetLayoutDirtyFlags();
    if (!initialLayoutDone_ ||
        (layFlags & (MeasureDirty | ArrangeDirty | SubtreeLayoutDirty)) != 0u) {
        rootElement_->Measure(Size{viewportW_, viewportH_});
        rootElement_->Arrange(Rect{0.0f, 0.0f, viewportW_, viewportH_});
        initialLayoutDone_ = true;
        if (bindingEngine_) {
            (void)bindingEngine_->FlushTrackA();
        }
    }
    if (renderTree_.NodeCount() > 0u) {
        (void)renderTree_.SyncVisualTree(rootElement_.Get());
    }
}

void XamlApplication::RasterizeToRgba8(std::vector<std::uint8_t>& outRgba8, Color clearColor) {
    if (!rootElement_) {
        outRgba8.clear();
        return;
    }
    if (!initialLayoutDone_) {
        Tick(0.0f);
    }
    if (renderTree_.NodeCount() == 0u) {
        renderTree_.BuildFromVisualTree(rootElement_.Get());
    } else {
        (void)renderTree_.SyncVisualTree(rootElement_.Get());
    }
    ZeroCopyGpuStream::RasterizeTreeToRgba8(
        renderTree_,
        static_cast<std::uint32_t>(std::max(1.0f, viewportW_)),
        static_cast<std::uint32_t>(std::max(1.0f, viewportH_)),
        outRgba8,
        clearColor);
}

} // namespace aero
