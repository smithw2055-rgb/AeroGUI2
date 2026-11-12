#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace fs = std::filesystem;
using namespace aero;

namespace {

int gTotalChecks = 0;
int gFailedChecks = 0;

void CheckImpl(bool cond, const char* expr, int line, const std::string& msg = {}) {
    ++gTotalChecks;
    if (!cond) {
        ++gFailedChecks;
        std::fprintf(
            stderr,
            "[FAIL] samples_mainwindow_tests.cpp:%d: %s%s%s\n",
            line,
            expr,
            msg.empty() ? "" : " -- ",
            msg.c_str());
    }
}

#define AERO_MW_CHECK(cond) CheckImpl((cond), #cond, __LINE__)
#define AERO_MW_CHECK_MSG(cond, msg) CheckImpl((cond), #cond, __LINE__, (msg))

[[nodiscard]] std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

[[nodiscard]] bool ReadTextFile(const fs::path& p, std::string& outText) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    outText = oss.str();
    return true;
}

[[nodiscard]] fs::path DiscoverSamplesRoot() {
    std::vector<fs::path> candidates;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        candidates.emplace_back(envDir);
    }
#ifdef AERO_SAMPLES_DIR
    candidates.emplace_back(AERO_SAMPLES_DIR);
#endif
    static constexpr const char* kRelProbes[] = {
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
        if (fs::exists(cand / "Buttons" / "Data" / "MainPage.xaml", ec) &&
            fs::exists(cand / "Gallery" / "Data" / "MainWindow.xaml", ec) &&
            fs::exists(cand / "UserControl" / "Data" / "MainPage.xaml", ec)) {
            const fs::path canon = fs::canonical(cand, ec);
            return ec ? cand : canon;
        }
    }
    return {};
}

[[nodiscard]] bool EndsWith(std::string_view full, std::string_view suffix) noexcept {
    return full.size() >= suffix.size() &&
           full.substr(full.size() - suffix.size()) == suffix;
}

void RegisterDecoratorShims() {
    AxbPackage passThrough{};
    passThrough.rootTypeName = "UserControl";
    AxbTemplateRegistry::Instance().Register("Decorator", passThrough);
    AxbTemplateRegistry::Instance().Register("local:Decorator", passThrough);
    AxbTemplateRegistry::Instance().Register("Gallery.Decorator", passThrough);
    AxbTemplateRegistry::Instance().Register("ViewboxDecorator", passThrough);
    AxbTemplateRegistry::Instance().Register("local:ViewboxDecorator", passThrough);
}

struct MountedSampleHarness {
    std::shared_ptr<UiResourceContext> resourceCtx;
    std::unique_ptr<ScopedUiResourceContext> resourceScope;
    ExecutionDomainContext domains;
    StyleTriggerEngine styleEngine;
    AnimationEngine animEngine;
    AeroCompileResult compileResult;
    std::shared_ptr<BindingRegistry> nativeBindings;
    std::unique_ptr<ScriptUiBridge> scriptBridge;
    std::unique_ptr<Interpreter> uiInterpreter;
    std::unique_ptr<DualTrackBindingEngine> bindingEngine;
    ObjectRef mainWinRef{};
    ObjectRef vmRef{};
    PersistentRoot mainWinPin{};
    PersistentRoot vmPin{};
    IntrusivePtr<UIElement> root;
    PagedRenderTree renderTree;
    Size viewport{1280.0f, 720.0f};

    ~MountedSampleHarness() {
        if (root) {
            root->SetAsRootVisual(false, &domains.nativeHandles(), &domains.uiHeap());
            root.Reset();
        }
        vmPin = PersistentRoot{};
        mainWinPin = PersistentRoot{};
        bindingEngine.reset();
        scriptBridge.reset();
        uiInterpreter.reset();
        nativeBindings.reset();
        resourceScope.reset();
        resourceCtx.reset();
    }

    bool LoadSample(
        const fs::path& samplesRoot,
        const std::string& sampleName,
        const std::string& mainXamlFilename = "MainPage.xaml",
        const std::string& expectedRootType = "Page") {
        std::error_code ec;
        const fs::path projDataDir = samplesRoot / sampleName / "Data";
        const fs::path xamlPath = projDataDir / mainXamlFilename;
        const fs::path rsPath = projDataDir / "MainWindow.rs";
        const fs::path themeDataDir = samplesRoot / "Theme" / "Data";

        AERO_MW_CHECK_MSG(fs::is_regular_file(xamlPath, ec), "Missing XAML: " + xamlPath.string());
        AERO_MW_CHECK_MSG(fs::is_regular_file(rsPath, ec), "Missing MainWindow.rs: " + rsPath.string());
        if (!fs::is_regular_file(xamlPath, ec) || !fs::is_regular_file(rsPath, ec)) {
            return false;
        }

        resourceCtx = std::make_shared<UiResourceContext>();
        resourceScope = std::make_unique<ScopedUiResourceContext>(resourceCtx);
        RegisterDecoratorShims();

        const fs::path robotoFont = samplesRoot / "DataBinding" / "Data" / "Roboto-Regular.ttf";
        if (fs::is_regular_file(robotoFont, ec)) {
            (void)FontAtlas::Instance().LoadTrueTypeFontFromFile(robotoFont.string(), "Roboto", true);
        }

        std::vector<std::string> searchPaths{
            xamlPath.parent_path().string(),
            projDataDir.string(),
        };
        if (fs::is_directory(projDataDir / "Samples", ec)) {
            searchPaths.push_back((projDataDir / "Samples").string());
        }
        if (fs::is_directory(themeDataDir, ec)) {
            searchPaths.push_back(themeDataDir.string());
            if (fs::is_directory(themeDataDir / "Theme", ec)) {
                searchPaths.push_back((themeDataDir / "Theme").string());
            }
        }
        for (const auto& sp : searchPaths) {
            UiResourceContext::Current().AddXamlSearchDirectory(sp);
            ImageAtlas::Instance().AddSearchDirectory(sp);
        }

        AeroCompiler compiler;

        // Pre-load NoesisTheme dictionaries
        if (fs::is_directory(themeDataDir, ec)) {
            const bool preferLightBlue =
                (sampleName == "Gallery") ||
                (sampleName == "UserControl") ||
                (sampleName == "DataBinding");
            const char* preferredThemeName =
                preferLightBlue ? "NoesisTheme.LightBlue.xaml" : "NoesisTheme.DarkBlue.xaml";
            for (const char* tName : {"NoesisTheme.DarkBlue.xaml", "NoesisTheme.LightBlue.xaml"}) {
                const fs::path tp = themeDataDir / "Theme" / tName;
                if (fs::is_regular_file(tp, ec)) {
                    AeroCompileInput tIn{};
                    tIn.xamlPath = tp.string();
                    tIn.resourceSearchRoot = themeDataDir.string();
                    tIn.resourceSearchPaths = searchPaths;
                    const AeroCompileResult tRes = compiler.Compile(tIn);
                    if (tRes.succeeded) {
                        if (std::strcmp(tName, preferredThemeName) == 0) {
                            AxbTemplateRegistry::Instance().RegisterResources(tRes.axbPackage);
                        }
                        AxbTemplateRegistry::Instance().RegisterDictionary(tName, tRes.axbPackage);
                        AxbTemplateRegistry::Instance().RegisterDictionary(tp.string(), tRes.axbPackage);
                    }
                }
            }
        }

        // Compile sibling .xaml dictionaries & local UserControls
        std::vector<fs::path> siblingXamls;
        for (const auto& entry : fs::directory_iterator(projDataDir, ec)) {
            if (entry.is_regular_file() &&
                ToLowerAscii(entry.path().extension().string()) == ".xaml" &&
                entry.path() != xamlPath) {
                siblingXamls.push_back(entry.path());
            }
        }
        std::sort(siblingXamls.begin(), siblingXamls.end());
        for (const auto& sibPath : siblingXamls) {
            AeroCompileInput sibIn{};
            sibIn.xamlPath = sibPath.string();
            sibIn.resourceSearchRoot = projDataDir.string();
            sibIn.resourceSearchPaths = searchPaths;
            const AeroCompileResult sibRes = compiler.Compile(sibIn);
            AERO_MW_CHECK_MSG(sibRes.succeeded, "Sibling XAML failed: " + sibPath.string());
            if (sibRes.succeeded) {
                const std::string stem = sibPath.stem().string();
                AxbTemplateRegistry::Instance().RegisterDictionary(
                    sibPath.filename().string(), sibRes.axbPackage);
                AxbTemplateRegistry::Instance().RegisterDictionary(
                    sibPath.string(), sibRes.axbPackage);
                const bool isLangDict = (stem.rfind("Language-", 0) == 0);
                if (!isLangDict || stem == "Language-en") {
                    AxbTemplateRegistry::Instance().RegisterResources(sibRes.axbPackage);
                }
                if (stem != "Resources" && !isLangDict) {
                    if (!sibRes.axbPackage.className.empty()) {
                        AxbTemplateRegistry::Instance().Register(
                            sibRes.axbPackage.className, sibRes.axbPackage);
                    }
                    AxbTemplateRegistry::Instance().Register(stem, sibRes.axbPackage);
                    AxbTemplateRegistry::Instance().Register("local:" + stem, sibRes.axbPackage);
                }
            }
        }

        // Compile main XAML + MainWindow.rs (and any sibling .rs in Data/)
        AeroCompileInput mainIn{};
        mainIn.xamlPath = xamlPath.string();
        mainIn.resourceSearchRoot = projDataDir.string();
        mainIn.resourceSearchPaths = searchPaths;

        std::vector<fs::path> rsFiles;
        for (const auto& entry : fs::directory_iterator(projDataDir, ec)) {
            if (entry.is_regular_file() &&
                ToLowerAscii(entry.path().extension().string()) == ".rs") {
                rsFiles.push_back(entry.path());
            }
        }
        std::sort(rsFiles.begin(), rsFiles.end());
        for (const auto& rf : rsFiles) {
            std::string rsCode;
            if (!ReadTextFile(rf, rsCode)) {
                AERO_MW_CHECK_MSG(false, "Failed to read script: " + rf.string());
                return false;
            }
            mainIn.scriptSources.push_back(SourceFile{rf.string(), std::move(rsCode)});
        }

        compileResult = compiler.Compile(mainIn);
        if (!compileResult.succeeded) {
            std::string diagSummary = sampleName + " compile diagnostics:";
            for (const auto& d : compileResult.diagnostics) {
                diagSummary += "\n  " + d;
            }
            AERO_MW_CHECK_MSG(false, diagSummary);
            return false;
        }
        AERO_MW_CHECK_MSG(
            compileResult.diagnostics.empty(),
            sampleName + " unexpected diagnostics count=" + std::to_string(compileResult.diagnostics.size()));
        AERO_MW_CHECK_MSG(
            compileResult.axbPackage.className == sampleName + ".MainWindow",
            sampleName + " className actual='" + compileResult.axbPackage.className + "'");
        AERO_MW_CHECK_MSG(
            compileResult.axbPackage.rootDataTypeName == sampleName + "::ViewModel",
            sampleName + " rootDataTypeName actual='" + compileResult.axbPackage.rootDataTypeName + "'");
        AERO_MW_CHECK_MSG(
            compileResult.axbPackage.rootTypeName == expectedRootType,
            sampleName + " rootTypeName actual='" + compileResult.axbPackage.rootTypeName + "'");
        AERO_MW_CHECK_MSG(
            compileResult.programImage != nullptr,
            sampleName + " missing compiled ProgramImage");
        if (!compileResult.programImage) {
            return false;
        }

        AxbTemplateRegistry::Instance().RegisterResources(compileResult.axbPackage);

        // Optional Resources.xaml / Language-en.xaml dynamic dictionary
        AxbPackage extraDictPkg{};
        bool hasExtraDictPkg = false;
        fs::path dictPath;
        if (fs::is_regular_file(projDataDir / "Resources.xaml", ec)) {
            dictPath = projDataDir / "Resources.xaml";
        } else if (fs::is_regular_file(projDataDir / "Language-en.xaml", ec)) {
            dictPath = projDataDir / "Language-en.xaml";
        }
        if (!dictPath.empty()) {
            AeroCompileInput dictIn{};
            dictIn.xamlPath = dictPath.string();
            dictIn.resourceSearchRoot = projDataDir.string();
            dictIn.resourceSearchPaths = searchPaths;
            const AeroCompileResult dictRes = compiler.Compile(dictIn);
            if (dictRes.succeeded) {
                extraDictPkg = dictRes.axbPackage;
                hasExtraDictPkg = true;
                AxbTemplateRegistry::Instance().RegisterResources(extraDictPkg);
            }
        }

        domains.registerProgramObservableMetadata(*compileResult.programImage);
        nativeBindings = std::make_shared<BindingRegistry>();
        scriptBridge = std::make_unique<ScriptUiBridge>(domains);
        scriptBridge->RegisterCoreUiBindings(*nativeBindings);

        uiInterpreter = std::make_unique<Interpreter>(compileResult.programImage, domains.uiHeapPtr());
        uiInterpreter->setExecutionDomainContext(&domains, DomainKind::UiPresentation);
        uiInterpreter->setBindingRegistry(nativeBindings);

        const std::string factoryFnName = sampleName + "::CreateMainWindow";
        const auto fnSym = compileResult.programImage->findFunction(factoryFnName);
        AERO_MW_CHECK_MSG(fnSym.has_value(), "Missing factory function: " + factoryFnName);
        if (!fnSym.has_value()) {
            return false;
        }

        rs_rt::ExecutionOptions execOpts{};
        execOpts.limits.instructionBudget = 5'000'000;
        const auto factoryRes = uiInterpreter->invoke(*fnSym, {}, execOpts);
        AERO_MW_CHECK_MSG(
            factoryRes.succeeded,
            sampleName + "::CreateMainWindow failed: " + factoryRes.error.message);
        if (!factoryRes.succeeded) {
            return false;
        }

        const auto* oref = std::get_if<ObjectRef>(&factoryRes.value);
        AERO_MW_CHECK_MSG(oref != nullptr && oref->valid(), sampleName + " CreateMainWindow returned invalid ObjectRef");
        if (!oref || !oref->valid()) {
            return false;
        }
        mainWinRef = *oref;
        mainWinPin = domains.uiHeap().retain(mainWinRef);
        (void)domains.uiHeap().consumeDirtyObjects();
        domains.uiHeap().clearDirtyFieldMask(mainWinRef);

        bindingEngine = std::make_unique<DualTrackBindingEngine>(
            domains, compileResult.programImage.get(), uiInterpreter.get());
        scriptBridge->SetUnderlyingEngine(bindingEngine.get());

        root = CreateElementByTypeName(compileResult.axbPackage.rootTypeName);
        AERO_MW_CHECK_MSG(root.Get() != nullptr, sampleName + " CreateElementByTypeName returned null");
        if (!root) {
            return false;
        }

        const bool mounted = MountAndWireMainWindow(
            root.Get(),
            compileResult.axbPackage,
            domains,
            *bindingEngine,
            styleEngine,
            mainWinRef,
            uiInterpreter.get(),
            nullptr,
            &animEngine,
            &vmRef);
        AERO_MW_CHECK_MSG(mounted, sampleName + " MountAndWireMainWindow failed");
        if (!mounted) {
            return false;
        }

        AERO_MW_CHECK_MSG(vmRef.valid(), sampleName + " outViewModelRef invalid");
        AERO_MW_CHECK_MSG(!(vmRef == mainWinRef), sampleName + " ViewModel must be distinct from MainWindow");
        if (vmRef.valid()) {
            vmPin = domains.uiHeap().retain(vmRef);
        }

        // Verify MainWindow & ViewModel runtime types
        const SymbolId mwTid = domains.uiHeap().objectTypeId(mainWinRef).value_or(0);
        const SymbolId vmTid = domains.uiHeap().objectTypeId(vmRef).value_or(0);
        const auto* mwType = compileResult.programImage->resolveType(mwTid);
        const auto* vmType = compileResult.programImage->resolveType(vmTid);
        AERO_MW_CHECK_MSG(mwType != nullptr && EndsWith(mwType->name, "MainWindow"), sampleName + " MainWindow runtime type mismatch");
        AERO_MW_CHECK_MSG(vmType != nullptr && EndsWith(vmType->name, "ViewModel"), sampleName + " ViewModel runtime type mismatch");

        // Verify BoundScriptWrapper == MainWindow and DataContext == ViewModel
        AERO_MW_CHECK_MSG(
            root->BoundScriptWrapper() == mainWinRef,
            sampleName + " root->BoundScriptWrapper() != mainWinRef");
        const UnifiedValue dcVal = root->GetValue(PropId_DataContext);
        const auto* dcObj = std::get_if<ObjectRef>(&dcVal);
        AERO_MW_CHECK_MSG(
            dcObj != nullptr && *dcObj == vmRef,
            sampleName + " root PropId_DataContext != vmRef");

        if (hasExtraDictPkg) {
            (void)AxbTemplateRegistry::Instance().ApplyDynamicDictionary(extraDictPkg, root.Get());
            styleEngine.ImportRegistryStyles();
            (void)styleEngine.EvaluateAndApply();
        }

        root->SetAsRootVisual(true, &domains.nativeHandles(), &domains.uiHeap());

        // Verify __nativeRootHandle on MainWindow points to root
        UIElement* nativeRoot = ResolveHandleField(mainWinRef, "__nativeRootHandle");
        AERO_MW_CHECK_MSG(
            nativeRoot == root.Get(),
            sampleName + " MainWindow.__nativeRootHandle did not resolve to root UIElement*");

        PumpFrame(0.05f);
        AERO_MW_CHECK_MSG(
            renderTree.NodeCount() > 0u,
            sampleName + " produced 0 render nodes on initial render");
        return true;
    }

    void PumpFrame(float dtSec = 0.016f) {
        if (!root || !bindingEngine) {
            return;
        }
        (void)bindingEngine->FlushTrackA();
        (void)styleEngine.EvaluateAndApply();
        (void)bindingEngine->FlushTrackA();
        if (dtSec > 0.0f) {
            (void)animEngine.EvaluateStateMachines(dtSec);
            (void)animEngine.Tick(dtSec);
        }
        root->Measure(viewport);
        root->Arrange(Rect{0.0f, 0.0f, viewport.width, viewport.height});
        (void)renderTree.BuildFromVisualTree(root.Get());
    }

    [[nodiscard]] UnifiedValue ReadMember(ObjectRef obj, std::string_view memberName) const {
        if (!obj.valid() || !compileResult.programImage) {
            return UnifiedValue{};
        }
        const auto* heap = domains.heapForObject(obj);
        if (!heap || !heap->isAlive(obj)) {
            return UnifiedValue{};
        }
        const SymbolId typeId = heap->objectTypeId(obj).value_or(0);
        if (typeId == 0) {
            return UnifiedValue{};
        }
        if (const auto slot = domains.resolvePropertySlot(*compileResult.programImage, typeId, std::string(memberName))) {
            UnifiedValue val{};
            if (const_cast<ExecutionDomainContext&>(domains).readPropertySlotFast(
                    DomainKind::Ui, obj, *slot, val, nullptr, uiInterpreter.get())) {
                return val;
            }
        }
        SymbolId walkTid = typeId;
        const std::string backingName = "<" + std::string(memberName) + ">k__BackingField";
        const std::string underscoreName = "_" + std::string(memberName);
        const std::string getterName = "get_" + std::string(memberName);
        for (int depth = 0; depth < 16 && walkTid != 0; ++depth) {
            const auto* ts = compileResult.programImage->resolveType(walkTid);
            if (!ts) {
                break;
            }
            for (const auto& f : ts->fields) {
                if (f.name == memberName || f.name == backingName || f.name == underscoreName) {
                    UnifiedValue fval{};
                    if (domains.readObjectField(DomainKind::Ui, obj, f.index, fval)) {
                        return fval;
                    }
                }
            }
            for (const auto& m : ts->methods) {
                if (m.name == getterName && !m.staticMethod && m.id != 0 && uiInterpreter) {
                    rs_rt::ExecutionOptions opts{};
                    opts.limits.instructionBudget = 1'000'000;
                    const auto res = uiInterpreter->invoke(m.id, {obj}, opts);
                    if (res.succeeded) {
                        return res.value;
                    }
                }
            }
            walkTid = ts->baseTypeId;
        }
        return UnifiedValue{};
    }

    [[nodiscard]] std::string ValueToString(const UnifiedValue& v) const {
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            return sv->str();
        }
        if (const auto* oref = std::get_if<ObjectRef>(&v)) {
            if (oref->valid() && oref->kind == ObjectKind::String) {
                if (const auto viewOpt = domains.stringViewAcrossDomains(*oref)) {
                    return std::string(*viewOpt);
                }
            }
        }
        return {};
    }

    [[nodiscard]] std::string ReadString(ObjectRef obj, std::string_view name) const {
        return ValueToString(ReadMember(obj, name));
    }

    [[nodiscard]] std::int64_t ReadInt(ObjectRef obj, std::string_view name, std::int64_t fallback = -999999) const {
        const UnifiedValue v = ReadMember(obj, name);
        std::int64_t iv = 0;
        if (v.tryGetInt64(iv)) {
            return iv;
        }
        EnumValue ev{};
        if (v.tryGetEnum(ev)) {
            return ev.value;
        }
        float fv = 0.0f;
        if (TryGetFloat(v, fv)) {
            return static_cast<std::int64_t>(std::llround(fv));
        }
        return fallback;
    }

    [[nodiscard]] float ReadFloat(ObjectRef obj, std::string_view name, float fallback = -999999.0f) const {
        const UnifiedValue v = ReadMember(obj, name);
        float fv = 0.0f;
        if (TryGetFloat(v, fv)) {
            return fv;
        }
        return fallback;
    }

    [[nodiscard]] bool ReadBool(ObjectRef obj, std::string_view name, bool fallback = false) const {
        const UnifiedValue v = ReadMember(obj, name);
        if (const auto* bv = std::get_if<bool>(&v)) {
            return *bv;
        }
        std::int64_t iv = 0;
        if (v.tryGetInt64(iv)) {
            return iv != 0;
        }
        return fallback;
    }

    [[nodiscard]] ObjectRef ReadObject(ObjectRef obj, std::string_view name) const {
        const UnifiedValue v = ReadMember(obj, name);
        if (const auto* oref = std::get_if<ObjectRef>(&v); oref && oref->valid()) {
            return *oref;
        }
        return ObjectRef{};
    }

    [[nodiscard]] std::vector<UnifiedValue> ReadList(ObjectRef obj, std::string_view name) const {
        std::vector<UnifiedValue> out;
        const ObjectRef colRef = ReadObject(obj, name);
        if (!colRef.valid()) {
            return out;
        }
        auto* heap = const_cast<ExecutionDomainContext&>(domains).heapForObject(colRef);
        if (!heap || !heap->isAlive(colRef)) {
            return out;
        }
        const ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(*heap, colRef);
        if (!arrRef.valid()) {
            return out;
        }
        const std::size_t len = heap->arrayLength(arrRef).value_or(0u);
        out.reserve(len);
        for (std::size_t i = 0u; i < len; ++i) {
            if (auto elem = heap->arrayGet(arrRef, i)) {
                out.push_back(*elem);
            }
        }
        return out;
    }

    [[nodiscard]] UIElement* ResolveHandleField(ObjectRef obj, std::string_view fieldName) {
        const UnifiedValue v = ReadMember(obj, fieldName);
        NativeHandle h{};
        if (!v.tryGetNativeHandle(h)) {
            return nullptr;
        }
        return UIElement::FromNativeHandle(domains.nativeHandles(), h);
    }

    [[nodiscard]] UnifiedValue MakeScriptString(std::string_view text) {
        if (auto sref = domains.uiHeap().allocateString(std::string(text))) {
            return *sref;
        }
        return value::FromString(std::string(text));
    }

    std::optional<UnifiedValue> InvokeMethod(
        ObjectRef receiver,
        std::string_view methodName,
        const std::vector<UnifiedValue>& extraArgs = {}) {
        if (!receiver.valid() || !compileResult.programImage || !uiInterpreter) {
            return std::nullopt;
        }
        const SymbolId startTid = domains.uiHeap().objectTypeId(receiver).value_or(0);
        SymbolId walkTid = startTid;
        SymbolId methodSym = 0;
        for (int depth = 0; depth < 16 && walkTid != 0 && methodSym == 0; ++depth) {
            const auto* ts = compileResult.programImage->resolveType(walkTid);
            if (!ts) {
                break;
            }
            for (const auto& m : ts->methods) {
                if (!m.staticMethod && m.name == methodName &&
                    m.parameters.size() == 1u + extraArgs.size()) {
                    methodSym = m.id;
                    break;
                }
            }
            walkTid = ts->baseTypeId;
        }
        if (methodSym == 0) {
            return std::nullopt;
        }
        std::vector<UnifiedValue> callArgs;
        callArgs.reserve(1u + extraArgs.size());
        callArgs.push_back(receiver);
        for (const auto& a : extraArgs) {
            callArgs.push_back(a);
        }
        rs_rt::ExecutionOptions opts{};
        opts.limits.instructionBudget = 5'000'000;
        const auto res = uiInterpreter->invoke(methodSym, callArgs, opts);
        AERO_MW_CHECK_MSG(
            res.succeeded,
            "InvokeMethod(" + std::string(methodName) + ") failed: " + res.error.message);
        if (!res.succeeded) {
            return std::nullopt;
        }
        PumpFrame(0.016f);
        return res.value;
    }

    bool ClickButton(UIElement* elem) {
        auto* btn = elem ? elem->DynamicCast<Button>() : nullptr;
        AERO_MW_CHECK_MSG(btn != nullptr, "ClickButton expected a Button*");
        if (!btn) {
            return false;
        }
        RuntimeError err{};
        const bool ok = btn->ActivateFromInput(&err);
        PumpFrame(0.016f);
        return ok;
    }

    void PumpSeconds(float totalSec, float stepSec = 0.05f) {
        float elapsed = 0.0f;
        while (elapsed + 1e-5f < totalSec) {
            const float dt = std::min(stepSec, totalSec - elapsed);
            PumpFrame(dt);
            elapsed += dt;
        }
    }
};

void CollectTextsRecursive(UIElement* node, std::vector<std::string>& out, bool visibleOnly = true) {
    if (!node) {
        return;
    }
    if (visibleOnly && node->GetVisibility() == Visibility::Collapsed) {
        return;
    }
    if (auto* tb = node->DynamicCast<TextBlock>()) {
        const std::string t = tb->GetText();
        if (!t.empty()) {
            out.push_back(t);
        }
    } else if (auto* cp = node->DynamicCast<ContentPresenter>()) {
        const std::string t = cp->GetEffectiveText();
        if (!t.empty()) {
            out.push_back(t);
        }
    } else if (auto* btn = node->DynamicCast<Button>()) {
        const std::string t = btn->GetContentText();
        if (!t.empty()) {
            out.push_back(t);
        }
    }
    if (UIElement* tplRoot = node->TemplateRoot()) {
        CollectTextsRecursive(tplRoot, out, visibleOnly);
    }
    for (UIElement* ch = node->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
        CollectTextsRecursive(ch, out, visibleOnly);
    }
}

[[nodiscard]] bool SubtreeContainsText(UIElement* root, std::string_view needle, bool visibleOnly = true) {
    std::vector<std::string> texts;
    CollectTextsRecursive(root, texts, visibleOnly);
    for (const auto& t : texts) {
        if (t.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UIElement* FindDescendantByName(UIElement* node, std::string_view targetName) {
    if (!node) {
        return nullptr;
    }
    if (node->Name() == targetName) {
        return node;
    }
    if (UIElement* tplRoot = node->TemplateRoot()) {
        if (UIElement* found = FindDescendantByName(tplRoot, targetName)) {
            return found;
        }
    }
    for (UIElement* ch = node->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
        if (UIElement* found = FindDescendantByName(ch, targetName)) {
            return found;
        }
    }
    return nullptr;
}

// ============================================================================
// 1. Buttons
// ============================================================================
void TestButtonsSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Buttons")) {
        return;
    }

    UIElement* layoutRoot = h.ResolveHandleField(h.mainWinRef, "LayoutRoot");
    UIElement* logo = h.ResolveHandleField(h.mainWinRef, "Logo");
    UIElement* startBtn = h.ResolveHandleField(h.mainWinRef, "StartButton");
    UIElement* settingsBtn = h.ResolveHandleField(h.mainWinRef, "SettingsButton");
    UIElement* exitBtn = h.ResolveHandleField(h.mainWinRef, "ExitButton");
    AERO_MW_CHECK(layoutRoot != nullptr);
    AERO_MW_CHECK(logo != nullptr);
    AERO_MW_CHECK(startBtn != nullptr && startBtn->DynamicCast<Button>() != nullptr);
    AERO_MW_CHECK(settingsBtn != nullptr && settingsBtn->DynamicCast<Button>() != nullptr);
    AERO_MW_CHECK(exitBtn != nullptr && exitBtn->DynamicCast<Button>() != nullptr);

    AERO_MW_CHECK(h.ReadString(h.vmRef, "State") == "Menu");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "LastAction").empty());
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "StartCount") == 0);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "SettingsCount") == 0);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "ExitCount") == 0);
    AERO_MW_CHECK(!h.ReadBool(h.vmRef, "IsExited", true));

    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "START"));
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "SETTINGS"));
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "EXIT"));

    AERO_MW_CHECK(h.ClickButton(startBtn));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "StartCount") == 1);
    AERO_MW_CHECK(h.ReadString(h.vmRef, "LastAction") == "Start Game");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "State") == "Started");

    AERO_MW_CHECK(h.ClickButton(settingsBtn));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "SettingsCount") == 1);
    AERO_MW_CHECK(h.ReadString(h.vmRef, "LastAction") == "Change Settings");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "State") == "Settings");

    AERO_MW_CHECK(h.ClickButton(exitBtn));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "ExitCount") == 1);
    AERO_MW_CHECK(h.ReadString(h.vmRef, "LastAction") == "Exit Game");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "State") == "Exited");
    AERO_MW_CHECK(h.ReadBool(h.vmRef, "IsExited"));
}

// ============================================================================
// 2. DataBinding
// ============================================================================
void TestDataBindingSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "DataBinding")) {
        return;
    }

    UIElement* bgCtrl = h.ResolveHandleField(h.mainWinRef, "BgCtrl");
    AERO_MW_CHECK(bgCtrl != nullptr);

    const auto bodies = h.ReadList(h.vmRef, "SolarSystemObjects");
    AERO_MW_CHECK_MSG(bodies.size() == 10u, "Expected 10 SolarSystemObjects (Sun..Pluto), got " + std::to_string(bodies.size()));
    if (bodies.size() == 10u) {
        const auto* sunRef = std::get_if<ObjectRef>(&bodies.front());
        const auto* plutoRef = std::get_if<ObjectRef>(&bodies.back());
        AERO_MW_CHECK_MSG(
            sunRef != nullptr && h.ReadString(*sunRef, "Name") == "Sun",
            "Sun Name actual='" + (sunRef ? h.ReadString(*sunRef, "Name") : "<null>") + "'");
        AERO_MW_CHECK_MSG(
            plutoRef != nullptr && h.ReadString(*plutoRef, "Name") == "Pluto",
            "Pluto Name actual='" + (plutoRef ? h.ReadString(*plutoRef, "Name") : "<null>") + "'");
        if (plutoRef) {
            AERO_MW_CHECK(std::fabs(h.ReadFloat(*plutoRef, "Orbit") - 39.5f) < 0.01f);
            AERO_MW_CHECK(std::fabs(h.ReadFloat(*plutoRef, "Diameter") - 2274.0f) < 0.5f);
        }
    }

    const ObjectRef initialSel = h.ReadObject(h.vmRef, "SelectedObject");
    AERO_MW_CHECK_MSG(
        initialSel.valid() && h.ReadString(initialSel, "Name") == "Sun",
        "Initial SelectedObject.Name actual='" + h.ReadString(initialSel, "Name") + "'");
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "Sun"));

    (void)h.InvokeMethod(h.mainWinRef, "SelectByName", {h.MakeScriptString("Pluto")});
    const ObjectRef plutoSel = h.ReadObject(h.vmRef, "SelectedObject");
    AERO_MW_CHECK_MSG(
        plutoSel.valid() && h.ReadString(plutoSel, "Name") == "Pluto",
        "Pluto SelectedObject.Name actual='" + h.ReadString(plutoSel, "Name") + "'");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "SelectedIndex") == 9);

    (void)h.InvokeMethod(h.mainWinRef, "SelectByIndex", {value::FromInt(3)});
    const ObjectRef earthSel = h.ReadObject(h.vmRef, "SelectedObject");
    AERO_MW_CHECK_MSG(
        earthSel.valid() && h.ReadString(earthSel, "Name") == "Earth",
        "Earth SelectedObject.Name actual='" + h.ReadString(earthSel, "Name") + "'");
}

// ============================================================================
// 3. Gallery
// ============================================================================
void TestGallerySample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Gallery", "MainWindow.xaml", "Window")) {
        return;
    }

    UIElement* windowContent = h.ResolveHandleField(h.mainWinRef, "WindowContent");
    UIElement* sampleContainer1 = h.ResolveHandleField(h.mainWinRef, "SampleContainer1");
    UIElement* sampleContainer2 = h.ResolveHandleField(h.mainWinRef, "SampleContainer2");
    UIElement* selectorTree = h.ResolveHandleField(h.mainWinRef, "SelectorTree");
    UIElement* colorPicker = h.ResolveHandleField(h.mainWinRef, "ThemeColorPicker");
    UIElement* accentPicker = h.ResolveHandleField(h.mainWinRef, "ThemeAccentPicker");
    AERO_MW_CHECK(windowContent != nullptr);
    AERO_MW_CHECK(sampleContainer1 != nullptr);
    AERO_MW_CHECK(sampleContainer2 != nullptr);
    AERO_MW_CHECK(selectorTree != nullptr);
    AERO_MW_CHECK(colorPicker != nullptr);
    AERO_MW_CHECK(accentPicker != nullptr);

    const auto categories = h.ReadList(h.vmRef, "Categories");
    AERO_MW_CHECK_MSG(categories.size() == 7u, "Expected 7 Gallery categories, got " + std::to_string(categories.size()));
    std::size_t totalSamples = 0u;
    for (const auto& catVal : categories) {
        if (const auto* catRef = std::get_if<ObjectRef>(&catVal); catRef && catRef->valid()) {
            totalSamples += h.ReadList(*catRef, "Samples").size();
        }
    }
    AERO_MW_CHECK_MSG(totalSamples == 36u, "Expected 36 Gallery samples, got " + std::to_string(totalSamples));

    const auto themeColors = h.ReadList(h.vmRef, "ThemeColors");
    const auto themeAccents = h.ReadList(h.vmRef, "ThemeAccents");
    AERO_MW_CHECK(themeColors.size() == 2u);
    AERO_MW_CHECK(themeAccents.size() == 9u);
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedThemeColor"), "Name") == "Light");
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedThemeAccent"), "Name") == "Blue");

    // Load 1st sub-XAML into SampleContainer1 via MainWindow
    const auto sel1 = h.InvokeMethod(h.mainWinRef, "SelectSampleByName", {h.MakeScriptString("Button")});
    AERO_MW_CHECK(sel1.has_value() && std::holds_alternative<bool>(*sel1) && std::get<bool>(*sel1));
    AERO_MW_CHECK(h.ReadInt(h.mainWinRef, "ActiveContainerIndex") == 1);
    AERO_MW_CHECK(h.ReadString(h.mainWinRef, "LoadedSampleUri") == "Samples/Button.xaml");
    AERO_MW_CHECK(sampleContainer1 != nullptr && sampleContainer1->ChildCount() > 0u);

    // Load 2nd sub-XAML into SampleContainer2 (container alternation)
    const auto sel2 = h.InvokeMethod(h.mainWinRef, "SelectSampleByName", {h.MakeScriptString("Slider")});
    AERO_MW_CHECK(sel2.has_value() && std::holds_alternative<bool>(*sel2) && std::get<bool>(*sel2));
    AERO_MW_CHECK(h.ReadInt(h.mainWinRef, "ActiveContainerIndex") == 2);
    AERO_MW_CHECK(h.ReadString(h.mainWinRef, "LoadedSampleUri") == "Samples/Slider.xaml");
    AERO_MW_CHECK(sampleContainer2 != nullptr && sampleContainer2->ChildCount() > 0u);

    // Theme switching
    const auto themeRes = h.InvokeMethod(
        h.mainWinRef,
        "SetTheme",
        {h.MakeScriptString("Dark"), h.MakeScriptString("Green")});
    AERO_MW_CHECK(themeRes.has_value() && std::holds_alternative<bool>(*themeRes) && std::get<bool>(*themeRes));
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedThemeColor"), "Name") == "Dark");
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedThemeAccent"), "Name") == "Green");
}

// ============================================================================
// 4. Inventory
// ============================================================================
void TestInventorySample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Inventory")) {
        return;
    }

    UIElement* playerLabel = h.ResolveHandleField(h.mainWinRef, "PLAYER");
    UIElement* slotHead = h.ResolveHandleField(h.mainWinRef, "SlotHead");
    UIElement* slotFeet = h.ResolveHandleField(h.mainWinRef, "SlotFeet");
    UIElement* invList = h.ResolveHandleField(h.mainWinRef, "InventoryListBox");
    AERO_MW_CHECK(playerLabel != nullptr);
    AERO_MW_CHECK(slotHead != nullptr);
    AERO_MW_CHECK(slotFeet != nullptr);
    AERO_MW_CHECK(invList != nullptr);

    const ObjectRef playerRef = h.ReadObject(h.vmRef, "Player");
    AERO_MW_CHECK(playerRef.valid());
    AERO_MW_CHECK(h.ReadString(playerRef, "Name") == "Morgan Hearson");
    if (auto* tb = playerLabel ? playerLabel->DynamicCast<TextBlock>() : nullptr) {
        AERO_MW_CHECK(tb->GetText() == "Morgan Hearson");
    }

    const auto defaults = h.ReadList(h.vmRef, "Defaults");
    const auto equippedSlots = h.ReadList(playerRef, "Slots");
    const auto bagItems = h.ReadList(h.vmRef, "Inventory");
    AERO_MW_CHECK_MSG(defaults.size() == 8u, "Expected 8 Defaults, got " + std::to_string(defaults.size()));
    AERO_MW_CHECK_MSG(equippedSlots.size() == 8u, "Expected 8 Player.Slots, got " + std::to_string(equippedSlots.size()));
    AERO_MW_CHECK_MSG(bagItems.size() == 37u, "Expected 37 bag items (45 total), got " + std::to_string(bagItems.size()));

    // Verify Player.Slots[0..7] indexer bindings populated SlotHead..SlotFeet Content
    if (slotHead && equippedSlots.size() == 8u) {
        const UnifiedValue slot0Content = slotHead->GetValue(PropId_Content);
        const auto* slot0Obj = std::get_if<ObjectRef>(&slot0Content);
        const auto* expected0 = std::get_if<ObjectRef>(&equippedSlots[0]);
        AERO_MW_CHECK_MSG(
            slot0Obj != nullptr && expected0 != nullptr && *slot0Obj == *expected0,
            "Player.Slots[0] indexer binding did not bind SlotHead Content to KnightHelmet");
    }
    if (slotFeet && equippedSlots.size() == 8u) {
        const UnifiedValue slot7Content = slotFeet->GetValue(PropId_Content);
        const auto* slot7Obj = std::get_if<ObjectRef>(&slot7Content);
        const auto* expected7 = std::get_if<ObjectRef>(&equippedSlots[7]);
        AERO_MW_CHECK_MSG(
            slot7Obj != nullptr && expected7 != nullptr && *slot7Obj == *expected7,
            "Player.Slots[7] indexer binding did not bind SlotFeet Content to PlateGreaves");
    }

    h.PumpSeconds(0.45f);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalLife") == 1588);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalMana") == 361);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalDps") == 2189);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalArmor") == 351);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "1588"));

    // Unequip Head (KnightHelmet, Life=25) -> bag becomes 38, TotalLife becomes 1563, SlotHead updates to Defaults[0]
    const auto unequipRes = h.InvokeMethod(h.mainWinRef, "UnequipSlot", {value::FromInt(0)});
    h.PumpSeconds(0.45f);
    AERO_MW_CHECK(unequipRes.has_value() && std::holds_alternative<bool>(*unequipRes) && std::get<bool>(*unequipRes));
    AERO_MW_CHECK(h.ReadList(h.vmRef, "Inventory").size() == 38u);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalLife") == 1563);
    AERO_MW_CHECK_MSG(
        SubtreeContainsText(h.root.Get(), "1563"),
        "LIFE MultiBinding over Player.Slots[0..7] did not update to 1563 after UnequipSlot(0)");

    // Re-equip KnightHelmet from Inventory[37] into Slot 0
    const auto equipRes = h.InvokeMethod(
        h.mainWinRef,
        "EquipFromInventory",
        {value::FromInt(37), value::FromInt(0)});
    h.PumpSeconds(0.45f);
    AERO_MW_CHECK(equipRes.has_value() && std::holds_alternative<bool>(*equipRes) && std::get<bool>(*equipRes));
    AERO_MW_CHECK(h.ReadList(h.vmRef, "Inventory").size() == 37u);
    AERO_MW_CHECK(h.ReadInt(playerRef, "TotalLife") == 1588);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "1588"));
}

// ============================================================================
// 5. Localization
// ============================================================================
void TestLocalizationSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Localization")) {
        return;
    }

    UIElement* layoutRoot = h.ResolveHandleField(h.mainWinRef, "LayoutRoot");
    UIElement* dialog = h.ResolveHandleField(h.mainWinRef, "Dialog");
    UIElement* langSelector = h.ResolveHandleField(h.mainWinRef, "LanguageSelector");
    UIElement* musicSlider = h.ResolveHandleField(h.mainWinRef, "MusicLevel");
    AERO_MW_CHECK(layoutRoot != nullptr);
    AERO_MW_CHECK(dialog != nullptr);
    AERO_MW_CHECK(langSelector != nullptr);
    AERO_MW_CHECK(musicSlider != nullptr);

    const auto languages = h.ReadList(h.vmRef, "Languages");
    AERO_MW_CHECK_MSG(languages.size() == 4u, "Expected 4 Languages (en, fr, jp, ar), got " + std::to_string(languages.size()));
    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "SoundLevel") - 100.0f) < 0.01f);
    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "MusicLevel") - 70.0f) < 0.01f);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Level") == 1);

    // Initial English localized text
    AERO_MW_CHECK_MSG(
        SubtreeContainsText(h.root.Get(), "LOCALIZATION SAMPLE"),
        "Initial English TitleLabel 'LOCALIZATION SAMPLE' missing");
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "Sound"));

    // Switch to French (index 1) -> live noesis:Loc.Source dictionary switch!
    (void)h.InvokeMethod(h.mainWinRef, "SelectLanguageByIndex", {value::FromInt(1)});
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedLanguage"), "Source") == "Language-fr.xaml");
    AERO_MW_CHECK_MSG(
        SubtreeContainsText(h.root.Get(), "EXEMPLE DE LOCALISATION"),
        "Live Loc.Source switch to French did not update TitleLabel to 'EXEMPLE DE LOCALISATION'");

    // Switch to Japanese (index 2)
    (void)h.InvokeMethod(h.mainWinRef, "SelectLanguageByIndex", {value::FromInt(2)});
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedLanguage"), "Source") == "Language-jp.xaml");
    AERO_MW_CHECK_MSG(
        SubtreeContainsText(h.root.Get(), "局地化 見本"),
        "Live Loc.Source switch to Japanese did not update TitleLabel to '局地化 見本'");

    // Switch to Arabic (index 3)
    (void)h.InvokeMethod(h.mainWinRef, "SelectLanguageByIndex", {value::FromInt(3)});
    AERO_MW_CHECK(h.ReadString(h.ReadObject(h.vmRef, "SelectedLanguage"), "Source") == "Language-ar.xaml");
    AERO_MW_CHECK_MSG(
        SubtreeContainsText(h.root.Get(), "نموذج الترجمة"),
        "Live Loc.Source switch to Arabic did not update TitleLabel to 'نموذج الترجمة'");
}

// ============================================================================
// 6. Login
// ============================================================================
void TestLoginSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Login")) {
        return;
    }

    UIElement* userElem = h.ResolveHandleField(h.mainWinRef, "UserBox");
    UIElement* passElem = h.ResolveHandleField(h.mainWinRef, "PassBox");
    UIElement* msgElem = h.ResolveHandleField(h.mainWinRef, "Message");
    UIElement* loginBtn = h.ResolveHandleField(h.mainWinRef, "LoginBtn");
    auto* userBox = userElem ? userElem->DynamicCast<TextBox>() : nullptr;
    auto* passBox = passElem ? passElem->DynamicCast<PasswordBox>() : nullptr;
    auto* msgBlock = msgElem ? msgElem->DynamicCast<TextBlock>() : nullptr;
    AERO_MW_CHECK(userBox != nullptr);
    AERO_MW_CHECK(passBox != nullptr);
    AERO_MW_CHECK(msgBlock != nullptr);
    AERO_MW_CHECK(loginBtn != nullptr && loginBtn->DynamicCast<Button>() != nullptr);
    if (!userBox || !passBox || !msgBlock || !loginBtn) {
        return;
    }

    // Settle Intro storyboard (0.6s) so LoginBtn IsEnabled unlocks to True
    h.PumpSeconds(0.75f);

    AERO_MW_CHECK(h.ReadString(h.vmRef, "AccountName").empty());
    AERO_MW_CHECK(!h.ReadBool(h.vmRef, "LoginSuccessful", true));

    // Case 1: Empty AccountName + non-empty Password -> "ACCOUNT NAME CANNOT BE EMPTY"
    (void)passBox->ReplaceAllTextInteractive("12345");
    h.PumpFrame();
    AERO_MW_CHECK(h.ClickButton(loginBtn));
    AERO_MW_CHECK(!h.ReadBool(h.vmRef, "LoginSuccessful", true));
    AERO_MW_CHECK_MSG(
        msgBlock->GetText() == "ACCOUNT NAME CANNOT BE EMPTY",
        "Expected 'ACCOUNT NAME CANNOT BE EMPTY', got '" + msgBlock->GetText() + "'");

    // Case 2: TwoWay UserBox input "noesis" + wrong password -> "ACCOUNT NAME OR PASSWORD IS INCORRECT"
    (void)userBox->ReplaceAllTextInteractive("noesis");
    (void)passBox->ReplaceAllTextInteractive("wrong");
    h.PumpFrame();
    AERO_MW_CHECK_MSG(
        h.ReadString(h.vmRef, "AccountName") == "noesis",
        "TwoWay UserBox.Text binding did not update ViewModel.AccountName to 'noesis'");
    AERO_MW_CHECK(h.ClickButton(loginBtn));
    AERO_MW_CHECK(!h.ReadBool(h.vmRef, "LoginSuccessful", true));
    AERO_MW_CHECK_MSG(
        msgBlock->GetText() == "ACCOUNT NAME OR PASSWORD IS INCORRECT",
        "Expected 'ACCOUNT NAME OR PASSWORD IS INCORRECT', got '" + msgBlock->GetText() + "'");

    // Case 3: Valid credentials "noesis" / "12345" -> "LOGIN SUCCESSFUL"
    (void)passBox->ReplaceAllTextInteractive("12345");
    h.PumpFrame();
    AERO_MW_CHECK(h.ClickButton(loginBtn));
    AERO_MW_CHECK_MSG(h.ReadBool(h.vmRef, "LoginSuccessful"), "LoginBtn click did not succeed with noesis/12345");
    AERO_MW_CHECK_MSG(
        msgBlock->GetText() == "LOGIN SUCCESSFUL",
        "Expected 'LOGIN SUCCESSFUL', got '" + msgBlock->GetText() + "'");
}

// ============================================================================
// 7. Menu3D
// ============================================================================
void TestMenu3DSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Menu3D")) {
        return;
    }

    UIElement* animBg = h.ResolveHandleField(h.mainWinRef, "AnimatedBackground");
    UIElement* navRoot = h.ResolveHandleField(h.mainWinRef, "NavigationRoot");
    AERO_MW_CHECK(animBg != nullptr);
    AERO_MW_CHECK(navRoot != nullptr);

    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Platform") == 0); // Platform.PC == 0
    const ObjectRef mainMenu = h.ReadObject(h.vmRef, "MainMenu");
    const ObjectRef startMenu = h.ReadObject(h.vmRef, "StartMenu");
    const ObjectRef settingsMenu = h.ReadObject(h.vmRef, "SettingsMenu");
    AERO_MW_CHECK(mainMenu.valid() && h.ReadString(h.ReadObject(mainMenu, "Description"), "Description") == "MAIN MENU");
    AERO_MW_CHECK(startMenu.valid() && h.ReadString(h.ReadObject(startMenu, "Description"), "Description") == "SELECT DIFFICULTY");
    AERO_MW_CHECK(settingsMenu.valid() && h.ReadString(h.ReadObject(settingsMenu, "Description"), "Description") == "SETTINGS");

    (void)h.InvokeMethod(h.mainWinRef, "StartCasual");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "CurrentDifficulty") == "Casual");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "StartCount") == 1);

    (void)h.InvokeMethod(h.mainWinRef, "StartNormal");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "CurrentDifficulty") == "Normal");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "StartCount") == 2);

    (void)h.InvokeMethod(h.mainWinRef, "StartExpert");
    AERO_MW_CHECK(h.ReadString(h.vmRef, "CurrentDifficulty") == "Expert");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "StartCount") == 3);

    (void)h.InvokeMethod(h.mainWinRef, "Exit");
    AERO_MW_CHECK(h.ReadBool(h.vmRef, "Exited"));
}

// ============================================================================
// 8. QuestLog
// ============================================================================
void TestQuestLogSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "QuestLog")) {
        return;
    }

    UIElement* questsList = h.ResolveHandleField(h.mainWinRef, "Quests");
    UIElement* questTitle = h.ResolveHandleField(h.mainWinRef, "QuestTitle");
    UIElement* questDesc = h.ResolveHandleField(h.mainWinRef, "QuestDescription");
    UIElement* questCompleted = h.ResolveHandleField(h.mainWinRef, "QuestCompleted");
    auto* titleTb = questTitle ? questTitle->DynamicCast<TextBlock>() : nullptr;
    auto* descTb = questDesc ? questDesc->DynamicCast<TextBlock>() : nullptr;
    AERO_MW_CHECK(questsList != nullptr && questsList->DynamicCast<ListBox>() != nullptr);
    AERO_MW_CHECK(titleTb != nullptr);
    AERO_MW_CHECK(descTb != nullptr);
    AERO_MW_CHECK(questCompleted != nullptr);
    if (!titleTb || !descTb || !questCompleted) {
        return;
    }

    const auto quests = h.ReadList(h.vmRef, "Quests");
    AERO_MW_CHECK_MSG(quests.size() == 12u, "Expected 12 Quests, got " + std::to_string(quests.size()));

    const ObjectRef sel0 = h.ReadObject(h.vmRef, "SelectedQuest");
    AERO_MW_CHECK(sel0.valid() && h.ReadString(sel0, "Title") == "Nature's Uprising");
    AERO_MW_CHECK(!h.ReadBool(sel0, "Completed", true));
    AERO_MW_CHECK(titleTb->GetText() == "Nature's Uprising");
    AERO_MW_CHECK(questCompleted->GetVisibility() == Visibility::Collapsed);

    // Select quest 1 ("Calming the Wake", Completed == true) via ListBox interactive selection
    if (auto* lb = questsList->DynamicCast<ListBox>()) {
        AERO_MW_CHECK(lb->SelectIndexInteractive(1));
        h.PumpFrame();
    } else {
        (void)h.InvokeMethod(h.mainWinRef, "SelectQuestByIndex", {value::FromInt(1)});
    }
    const ObjectRef sel1 = h.ReadObject(h.vmRef, "SelectedQuest");
    AERO_MW_CHECK(sel1.valid() && h.ReadString(sel1, "Title") == "Calming the Wake");
    AERO_MW_CHECK(h.ReadBool(sel1, "Completed"));
    AERO_MW_CHECK(titleTb->GetText() == "Calming the Wake");
    AERO_MW_CHECK(!descTb->GetText().empty());
    AERO_MW_CHECK(questCompleted->GetVisibility() == Visibility::Visible);
}

// ============================================================================
// 9. Scoreboard
// ============================================================================
void TestScoreboardSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "Scoreboard")) {
        return;
    }

    UIElement* layoutRoot = h.ResolveHandleField(h.mainWinRef, "LayoutRoot");
    UIElement* playersElem = h.ResolveHandleField(h.mainWinRef, "Players");
    UIElement* visibleTeamElem = h.ResolveHandleField(h.mainWinRef, "VisibleTeam");
    AERO_MW_CHECK(layoutRoot != nullptr);
    AERO_MW_CHECK(playersElem != nullptr);
    AERO_MW_CHECK(visibleTeamElem != nullptr);

    AERO_MW_CHECK(h.ReadString(h.vmRef, "Name") == "Silvershard Mines");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "ElapsedTime") == 16);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "AllianceScore") == 1982);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "HordeScore") == 1911);

    const auto players = h.ReadList(h.vmRef, "Players");
    const auto visibleTeams = h.ReadList(h.vmRef, "VisibleTeams");
    AERO_MW_CHECK_MSG(players.size() == 16u, "Expected 16 Scoreboard Players, got " + std::to_string(players.size()));
    AERO_MW_CHECK_MSG(visibleTeams.size() == 3u, "Expected 3 VisibleTeams, got " + std::to_string(visibleTeams.size()));

    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "Silvershard Mines"));
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "1982"));
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "1911"));

    // Mutate score via MainWindow and verify recalculated AllianceScore updates bound UI
    (void)h.InvokeMethod(h.mainWinRef, "AddPlayerScore", {value::FromInt(0), value::FromInt(18)});
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "AllianceScore") == 2000);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "2000"));
}

// ============================================================================
// 10. TicTacToe
// ============================================================================
void TestTicTacToeSample(const fs::path& samplesRoot) {
    static_assert(
        PropId_CustomIndexInt32 != PropId_CustomStateInt32,
        "Custom.Index_Int32 and Custom.State_Int32 must have distinct BuiltinPropertyIds");

    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "TicTacToe")) {
        return;
    }

    UIElement* cells[9]{};
    for (int i = 0; i < 9; ++i) {
        cells[i] = h.ResolveHandleField(h.mainWinRef, "Cell" + std::to_string(i));
        AERO_MW_CHECK_MSG(cells[i] != nullptr, "Missing handle Cell" + std::to_string(i));
        if (cells[i]) {
            std::int64_t idxProp = -1;
            std::int64_t stateProp = -1;
            AERO_MW_CHECK(cells[i]->GetValue(PropId_CustomIndexInt32).tryGetInt64(idxProp) && idxProp == i);
            AERO_MW_CHECK_MSG(
                cells[i]->GetValue(PropId_CustomStateInt32).tryGetInt64(stateProp) && stateProp == 1,
                "Expected ToggleButton style setter Custom.State_Int32 == State.Player1 (1)");
        }
    }

    UIElement* startBtn = h.ResolveHandleField(h.mainWinRef, "Start");
    UIElement* scoreP1 = h.ResolveHandleField(h.mainWinRef, "ScorePlayer1");
    UIElement* scoreTies = h.ResolveHandleField(h.mainWinRef, "ScoreTies");
    AERO_MW_CHECK(startBtn != nullptr);
    AERO_MW_CHECK(scoreP1 != nullptr);
    AERO_MW_CHECK(scoreTies != nullptr);

    AERO_MW_CHECK(h.ReadInt(h.vmRef, "State") == 1); // State.Player1
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "PLAYER 1 TURN"));

    // Play WinRow0 for Player 1: P1->0, P2->3, P1->1, P2->4, P1->2
    for (int moveIdx : {0, 3, 1, 4, 2}) {
        AERO_MW_CHECK(h.ClickButton(cells[moveIdx]));
    }
    AERO_MW_CHECK_MSG(
        h.ReadInt(h.vmRef, "State") == 4,
        "Expected State.WinRow0 (4), got " + std::to_string(h.ReadInt(h.vmRef, "State")));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "ScorePlayer1") == 1);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "PLAYER 1 WON!"));
    if (cells[1]) {
        std::int64_t idxProp = -1;
        std::int64_t stateProp = -1;
        AERO_MW_CHECK(cells[1]->GetValue(PropId_CustomIndexInt32).tryGetInt64(idxProp) && idxProp == 1);
        AERO_MW_CHECK(cells[1]->GetValue(PropId_CustomStateInt32).tryGetInt64(stateProp) && stateProp == 4);
    }

    // Start new game -> BoardFlip1Anim (0.1s) completes -> StoryboardCompletedTrigger invokes StartCommand -> State.Player2 (2)
    AERO_MW_CHECK(h.ClickButton(startBtn));
    h.PumpSeconds(0.25f);
    AERO_MW_CHECK_MSG(
        h.ReadInt(h.vmRef, "State") == 2,
        "Expected starter to alternate to State.Player2 (2), got " + std::to_string(h.ReadInt(h.vmRef, "State")));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Cell0") == 0);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "PLAYER 2 TURN"));

    // Play a 9-move Tie starting with Player 2:
    // P2: 0, 2, 3, 7, 8  |  P1: 1, 4, 5, 6
    // Row0: P2(0), P1(1), P2(2)
    // Row1: P2(3), P1(4), P1(5)
    // Row2: P1(6), P2(7), P2(8) -> no 3-in-a-row!
    for (int moveIdx : {0, 1, 2, 4, 3, 5, 7, 6, 8}) {
        AERO_MW_CHECK(h.ClickButton(cells[moveIdx]));
    }
    AERO_MW_CHECK_MSG(
        h.ReadInt(h.vmRef, "State") == 3,
        "Expected State.Tie (3), got " + std::to_string(h.ReadInt(h.vmRef, "State")));
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "ScoreTies") == 1);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "GAME TIED!"));
}

// ============================================================================
// 11. UserControl
// ============================================================================
void TestUserControlSample(const fs::path& samplesRoot) {
    MountedSampleHarness h;
    if (!h.LoadSample(samplesRoot, "UserControl")) {
        return;
    }

    UIElement* statStr = h.ResolveHandleField(h.mainWinRef, "StatStr");
    UIElement* statAgi = h.ResolveHandleField(h.mainWinRef, "StatAgi");
    UIElement* statInt = h.ResolveHandleField(h.mainWinRef, "StatInt");
    UIElement* statCha = h.ResolveHandleField(h.mainWinRef, "StatCha");
    AERO_MW_CHECK(statStr != nullptr);
    AERO_MW_CHECK(statAgi != nullptr);
    AERO_MW_CHECK(statInt != nullptr);
    AERO_MW_CHECK(statCha != nullptr);
    if (!statStr || !statAgi || !statInt || !statCha) {
        return;
    }

    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "Strength") - 10.0f) < 0.01f);
    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "Agility") - 14.0f) < 0.01f);
    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "Intelligence") - 8.0f) < 0.01f);
    AERO_MW_CHECK(std::fabs(h.ReadFloat(h.vmRef, "Charisma") - 12.0f) < 0.01f);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "PointsAvailable") == 6);

    float strVal = 0.0f, agiVal = 0.0f, intVal = 0.0f, chaVal = 0.0f;
    AERO_MW_CHECK(TryGetFloat(statStr->GetValue(PropId_Value), strVal) && std::fabs(strVal - 10.0f) < 0.01f);
    AERO_MW_CHECK(TryGetFloat(statAgi->GetValue(PropId_Value), agiVal) && std::fabs(agiVal - 14.0f) < 0.01f);
    AERO_MW_CHECK(TryGetFloat(statInt->GetValue(PropId_Value), intVal) && std::fabs(intVal - 8.0f) < 0.01f);
    AERO_MW_CHECK(TryGetFloat(statCha->GetValue(PropId_Value), chaVal) && std::fabs(chaVal - 12.0f) < 0.01f);
    AERO_MW_CHECK_MSG(SubtreeContainsText(h.root.Get(), "6"), "Initial POINTS AVAILABLE '6' missing from visual tree");

    // Click UpButton inside StatStr NumericUpDown -> Value 10 -> 11, POINTS AVAILABLE 6 -> 5
    UIElement* strUpBtn = FindDescendantByName(statStr, "UpButton");
    UIElement* agiDownBtn = FindDescendantByName(statAgi, "DownButton");
    AERO_MW_CHECK_MSG(strUpBtn != nullptr, "NumericUpDown UpButton not found in StatStr");
    AERO_MW_CHECK_MSG(agiDownBtn != nullptr, "NumericUpDown DownButton not found in StatAgi");
    if (strUpBtn) {
        AERO_MW_CHECK(h.ClickButton(strUpBtn));
        AERO_MW_CHECK(TryGetFloat(statStr->GetValue(PropId_Value), strVal) && std::fabs(strVal - 11.0f) < 0.01f);
        (void)h.InvokeMethod(h.mainWinRef, "SyncFromControls");
        AERO_MW_CHECK(h.ReadInt(h.vmRef, "Strength") == 11);
        AERO_MW_CHECK(h.ReadInt(h.vmRef, "PointsAvailable") == 5);
        AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "5"));
    }
    if (agiDownBtn) {
        AERO_MW_CHECK(h.ClickButton(agiDownBtn));
        AERO_MW_CHECK(TryGetFloat(statAgi->GetValue(PropId_Value), agiVal) && std::fabs(agiVal - 13.0f) < 0.01f);
        (void)h.InvokeMethod(h.mainWinRef, "SyncFromControls");
        AERO_MW_CHECK(h.ReadInt(h.vmRef, "Agility") == 13);
        AERO_MW_CHECK(h.ReadInt(h.vmRef, "PointsAvailable") == 6);
    }

    // Exercise MainWindow stat helper clamping within 50-point budget
    (void)h.InvokeMethod(h.mainWinRef, "SetStrength", {value::FromInt(20)});
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Strength") == 17);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "PointsAvailable") == 0);
    AERO_MW_CHECK(TryGetFloat(statStr->GetValue(PropId_Value), strVal) && std::fabs(strVal - 17.0f) < 0.01f);
    AERO_MW_CHECK(SubtreeContainsText(h.root.Get(), "0"));

    // ResetStats restores 10 / 14 / 8 / 12 and 6 available points
    (void)h.InvokeMethod(h.mainWinRef, "ResetStats");
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Strength") == 10);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Agility") == 14);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Intelligence") == 8);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "Charisma") == 12);
    AERO_MW_CHECK(h.ReadInt(h.vmRef, "PointsAvailable") == 6);
}

} // namespace

int main() {
    EnsureBuiltinTypesRegistered();
    EnsureControlTypesRegistered();

    const fs::path samplesRoot = DiscoverSamplesRoot();
    AERO_MW_CHECK_MSG(!samplesRoot.empty(), "Failed to locate Samples directory");
    if (samplesRoot.empty()) {
        return 1;
    }

    std::printf("[samples_mainwindow_tests] Running 11-sample MainWindow + ViewModel end-to-end suite...\n");

    TestButtonsSample(samplesRoot);
    TestDataBindingSample(samplesRoot);
    TestGallerySample(samplesRoot);
    TestInventorySample(samplesRoot);
    TestLocalizationSample(samplesRoot);
    TestLoginSample(samplesRoot);
    TestMenu3DSample(samplesRoot);
    TestQuestLogSample(samplesRoot);
    TestScoreboardSample(samplesRoot);
    TestTicTacToeSample(samplesRoot);
    TestUserControlSample(samplesRoot);

    std::printf(
        "[samples_mainwindow_tests] Completed: %d/%d checks passed (%d failed).\n",
        gTotalChecks - gFailedChecks,
        gTotalChecks,
        gFailedChecks);
    return (gFailedChecks == 0) ? 0 : 1;
}
