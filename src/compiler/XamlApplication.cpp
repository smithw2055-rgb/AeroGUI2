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
    const std::vector<std::string>& resourceSearchPaths) {
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

    std::error_code ec;
    fs::path targetPath(options.appOrWindowXamlPath);
    fs::path baseDir;
    fs::path appXamlPath;
    fs::path windowXamlPath;

    if (fs::is_directory(targetPath, ec)) {
        baseDir = targetPath;
        if (fs::is_regular_file(baseDir / "App.xaml", ec)) {
            appXamlPath = baseDir / "App.xaml";
        } else if (fs::is_regular_file(baseDir / "Data/App.xaml", ec)) {
            appXamlPath = baseDir / "Data/App.xaml";
            baseDir = baseDir / "Data";
        } else if (fs::is_regular_file(baseDir / "MainWindow.xaml", ec)) {
            windowXamlPath = baseDir / "MainWindow.xaml";
        } else if (fs::is_regular_file(baseDir / "Data/MainWindow.xaml", ec)) {
            windowXamlPath = baseDir / "Data/MainWindow.xaml";
            baseDir = baseDir / "Data";
        } else if (fs::is_regular_file(baseDir / "Data/MainPage.xaml", ec)) {
            windowXamlPath = baseDir / "Data/MainPage.xaml";
            baseDir = baseDir / "Data";
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

    std::string winXml;
    if (!ReadUtf8File(windowXamlPath, winXml)) {
        if (outError) *outError = "Failed to read window XAML: " + windowXamlPath.string();
        return false;
    }

    PreRegisterLocalUserControlsAndDictionaries(
        compiler, windowXamlPath.parent_path(), windowXamlPath, appXamlPath, options.resourceSearchPaths);

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

    Tick(0.0f);
    return true;
}

void XamlApplication::Tick(float dtSeconds) {
    if (!rootElement_) return;
    if (inputRouter_) {
        (void)inputRouter_->FlushQueuedEvents();
        inputRouter_->Tick(dtSeconds);
    }
    if (bindingEngine_) {
        (void)bindingEngine_->FlushTrackA();
    }
    if (animationEngine_ && dtSeconds > 0.0f) {
        animationEngine_->Tick(dtSeconds);
    }
    if (styleEngine_) {
        (void)styleEngine_->EvaluateAndApply();
    }
    rootElement_->Measure(Size{viewportW_, viewportH_});
    rootElement_->Arrange(Rect{0.0f, 0.0f, viewportW_, viewportH_});
    if (bindingEngine_) {
        (void)bindingEngine_->FlushTrackA();
    }
}

} // namespace aero
