#include "aero/bridge/ScriptUiBridge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

namespace aero {

namespace {

[[nodiscard]] bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0u; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

struct PendingScriptUiState {
    struct DcEntry {
        const ExecutionDomainContext* domains = nullptr;
        std::uint32_t ownerSlot = 0;
        std::uint32_t ownerGen = 0;
        ObjectRef dataContext{};
    };
    struct PendingMountTarget {
        IntrusivePtr<UIElement> rootControl{};
        const AxbPackage* package = nullptr;
    };
    struct PreMountedWindowEntry {
        const ExecutionDomainContext* domains = nullptr;
        std::uint32_t ownerSlot = 0;
        std::uint32_t ownerGen = 0;
        IntrusivePtr<UIElement> rootControl{};
        std::vector<IntrusivePtr<UIElement>> mountedElements{};
    };
    std::mutex mutex;
    std::vector<DcEntry> dcEntries;
    std::unordered_map<const ExecutionDomainContext*, std::vector<std::string>> pendingDictionaries;
    std::unordered_map<const ExecutionDomainContext*, PendingMountTarget> pendingMountTargets;
    std::vector<PreMountedWindowEntry> preMountedWindows;

    PendingScriptUiState() {
        rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
            MarkValuesInPools();
        });
    }

    ~PendingScriptUiState() noexcept {
        rs_rt::unregisterExternalValuePoolRootMarker(this);
    }

    static PendingScriptUiState& Instance() noexcept {
        static PendingScriptUiState s;
        return s;
    }

    void MarkValuesInPools() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : dcEntries) {
            if (e.dataContext.valid()) {
                rs_rt::markValueInPools(UnifiedValue{e.dataContext});
            }
        }
    }

    void SetDataContext(const ExecutionDomainContext* dom, ObjectRef owner, ObjectRef dc) {
        if (!dom || !owner.valid()) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& e : dcEntries) {
            if (e.domains == dom && e.ownerSlot == owner.slot && e.ownerGen == owner.generation) {
                e.dataContext = dc;
                return;
            }
        }
        dcEntries.push_back(DcEntry{dom, owner.slot, owner.generation, dc});
    }

    [[nodiscard]] ObjectRef FindDataContext(const ExecutionDomainContext* dom, ObjectRef owner) noexcept {
        if (!dom || !owner.valid()) return ObjectRef{};
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = dcEntries.rbegin(); it != dcEntries.rend(); ++it) {
            if (it->domains == dom && it->ownerSlot == owner.slot && it->ownerGen == owner.generation) {
                return it->dataContext;
            }
        }
        return ObjectRef{};
    }

    void SetPendingMountTarget(
        const ExecutionDomainContext* dom,
        UIElement* rootControl,
        const AxbPackage* package) {
        if (!dom) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (!rootControl && !package) {
            pendingMountTargets.erase(dom);
            return;
        }
        pendingMountTargets[dom] = PendingMountTarget{
            IntrusivePtr<UIElement>(rootControl, true),
            package};
    }

    [[nodiscard]] PendingMountTarget FindPendingMountTarget(const ExecutionDomainContext* dom) noexcept {
        if (!dom) return {};
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = pendingMountTargets.find(dom);
        return (it != pendingMountTargets.end()) ? it->second : PendingMountTarget{};
    }

    [[nodiscard]] bool HasPreMountedWindow(const ExecutionDomainContext* dom, ObjectRef owner) noexcept {
        if (!dom || !owner.valid()) return false;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : preMountedWindows) {
            if (e.domains == dom && e.ownerSlot == owner.slot && e.ownerGen == owner.generation) {
                return true;
            }
        }
        return false;
    }

    void RecordPreMountedWindow(
        const ExecutionDomainContext* dom,
        ObjectRef owner,
        IntrusivePtr<UIElement> rootControl,
        std::vector<IntrusivePtr<UIElement>> mountedElements) {
        if (!dom || !owner.valid() || !rootControl) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& e : preMountedWindows) {
            if (e.domains == dom && e.ownerSlot == owner.slot && e.ownerGen == owner.generation) {
                e.rootControl = std::move(rootControl);
                e.mountedElements = std::move(mountedElements);
                return;
            }
        }
        preMountedWindows.push_back(PreMountedWindowEntry{
            dom,
            owner.slot,
            owner.generation,
            std::move(rootControl),
            std::move(mountedElements)});
    }

    [[nodiscard]] IntrusivePtr<UIElement> PeekPreMountedRoot(
        const ExecutionDomainContext* dom,
        ObjectRef owner) noexcept {
        if (!dom || !owner.valid()) return {};
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = preMountedWindows.rbegin(); it != preMountedWindows.rend(); ++it) {
            if (it->domains == dom && it->ownerSlot == owner.slot && it->ownerGen == owner.generation) {
                return it->rootControl;
            }
        }
        return {};
    }

    [[nodiscard]] bool ConsumePreMountedWindow(
        const ExecutionDomainContext* dom,
        ObjectRef owner,
        IntrusivePtr<UIElement>& outRootControl,
        std::vector<IntrusivePtr<UIElement>>& outMountedElements) {
        if (!dom || !owner.valid()) return false;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = preMountedWindows.begin(); it != preMountedWindows.end(); ++it) {
            if (it->domains == dom && it->ownerSlot == owner.slot && it->ownerGen == owner.generation) {
                outRootControl = std::move(it->rootControl);
                outMountedElements = std::move(it->mountedElements);
                preMountedWindows.erase(it);
                return true;
            }
        }
        return false;
    }

    void RecordDictionary(const ExecutionDomainContext* dom, const std::string& uri) {
        if (!dom || uri.empty()) return;
        std::lock_guard<std::mutex> lock(mutex);
        auto& vec = pendingDictionaries[dom];
        if (std::find(vec.begin(), vec.end(), uri) == vec.end()) {
            vec.push_back(uri);
        }
    }

    [[nodiscard]] std::vector<std::string> ConsumeDictionaries(const ExecutionDomainContext* dom) {
        std::vector<std::string> out;
        if (!dom) return out;
        std::lock_guard<std::mutex> lock(mutex);
        if (auto it = pendingDictionaries.find(dom); it != pendingDictionaries.end()) {
            out = std::move(it->second);
            pendingDictionaries.erase(it);
        }
        return out;
    }

    void ClearForDomain(const ExecutionDomainContext* dom) noexcept {
        if (!dom) return;
        std::lock_guard<std::mutex> lock(mutex);
        dcEntries.erase(
            std::remove_if(
                dcEntries.begin(),
                dcEntries.end(),
                [dom](const DcEntry& e) noexcept { return e.domains == dom; }),
            dcEntries.end());
        preMountedWindows.erase(
            std::remove_if(
                preMountedWindows.begin(),
                preMountedWindows.end(),
                [dom](const PreMountedWindowEntry& e) noexcept { return e.domains == dom; }),
            preMountedWindows.end());
        pendingMountTargets.erase(dom);
        pendingDictionaries.erase(dom);
    }
};

[[nodiscard]] std::uint16_t ResolveSdkPropertyIdByName(std::string_view propName) noexcept {
    while (!propName.empty() && std::isspace(static_cast<unsigned char>(propName.front()))) {
        propName.remove_prefix(1u);
    }
    while (!propName.empty() && std::isspace(static_cast<unsigned char>(propName.back()))) {
        propName.remove_suffix(1u);
    }
    if (propName.empty()) {
        return 0xFFFFu;
    }
    if (EqualsAsciiIgnoreCase(propName, "Width")) return PropId_Width;
    if (EqualsAsciiIgnoreCase(propName, "Height")) return PropId_Height;
    if (EqualsAsciiIgnoreCase(propName, "Value")) return PropId_Value;
    if (EqualsAsciiIgnoreCase(propName, "Minimum") || EqualsAsciiIgnoreCase(propName, "MinValue")) return PropId_Minimum;
    if (EqualsAsciiIgnoreCase(propName, "Maximum") || EqualsAsciiIgnoreCase(propName, "MaxValue")) return PropId_Maximum;
    if (EqualsAsciiIgnoreCase(propName, "SmallChange") || EqualsAsciiIgnoreCase(propName, "StepValue")) return PropId_SmallChange;
    const std::uint16_t builtin = ResolveBuiltinPropertyId(propName);
    if (builtin != 0xFFFFu && builtin != 0u) {
        return builtin;
    }
    return ResolveTriggerConditionPropertyId(propName);
}

void AttachStyleEngineAcrossSubtree(UIElement* subtreeRoot, StyleTriggerEngine* styleEngine) noexcept {
    if (!subtreeRoot || !styleEngine) {
        return;
    }
    std::vector<UIElement*> stack{subtreeRoot};
    while (!stack.empty()) {
        UIElement* cur = stack.back();
        stack.pop_back();
        if (!cur) {
            continue;
        }
        cur->BindStyleEngine(styleEngine);
        if (auto* ic = cur->DynamicCast<ItemsControl>()) {
            ic->SetStyleEngine(styleEngine);
        }
        if (auto* tc = cur->DynamicCast<TabControl>()) {
            tc->AssignStyleEnginePointer(styleEngine);
        }
        for (UIElement* ch = cur->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
            stack.push_back(ch);
        }
    }
}

[[nodiscard]] ObjectRef ExtractViewModelFromMainWindow(
    ExecutionDomainContext& domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter,
    rs_aot::ExecutionContext* aotContext,
    ObjectRef mainWinRef) {
    if (!mainWinRef.valid()) {
        return ObjectRef{};
    }
    auto* heap = domains.heapForObject(mainWinRef);
    if (!heap || !heap->isAlive(mainWinRef)) {
        return mainWinRef;
    }

    auto acceptIfAliveObject = [&](const UnifiedValue& val, ObjectRef& outRef) -> bool {
        if (const auto* r = std::get_if<ObjectRef>(&val); r && r->valid()) {
            if (auto* rh = domains.heapForObject(*r); rh && rh->isAlive(*r)) {
                outRef = *r;
                return true;
            }
        }
        return false;
    };

    // 0. Check SetDataContext(vm) recorded via Aero.UI::GUI_SetDataContext during MainWindow construction!
    {
        const ObjectRef explicitDc = PendingScriptUiState::Instance().FindDataContext(&domains, mainWinRef);
        if (explicitDc.valid()) {
            if (auto* rh = domains.heapForObject(explicitDc); rh && rh->isAlive(explicitDc)) {
                return explicitDc;
            }
        }
    }

    const SymbolId typeId = heap->objectTypeId(mainWinRef).value_or(0);
    if (program && typeId != 0) {
        if (const auto dcSlot = domains.resolvePropertySlot(*program, typeId, "DataContext")) {
            UnifiedValue propVal{};
            if (domains.readPropertySlotFast(DomainKind::Ui, mainWinRef, *dcSlot, propVal, nullptr, uiInterpreter)) {
                ObjectRef vmRef{};
                if (acceptIfAliveObject(propVal, vmRef)) {
                    return vmRef;
                }
            }
        }
        if (const auto slot = domains.resolvePropertySlot(*program, typeId, "ViewModel")) {
            UnifiedValue propVal{};
            if (domains.readPropertySlotFast(DomainKind::Ui, mainWinRef, *slot, propVal, nullptr, uiInterpreter)) {
                ObjectRef vmRef{};
                if (acceptIfAliveObject(propVal, vmRef)) {
                    return vmRef;
                }
            }
        }
    }

    if (program && typeId != 0) {
        SymbolId walkTid = typeId;
        for (int depth = 0; depth < 16 && walkTid != 0; ++depth) {
            const auto* ts = program->resolveType(walkTid);
            if (!ts) {
                break;
            }
            for (const auto& f : ts->fields) {
                if (f.name == "DataContext" ||
                    f.name == "<DataContext>k__BackingField" ||
                    f.name == "_dataContext" ||
                    f.name == "ViewModel" ||
                    f.name == "<ViewModel>k__BackingField" ||
                    f.name == "_ViewModel" ||
                    f.name == "_viewModel") {
                    UnifiedValue fieldVal{};
                    if (domains.readObjectField(DomainKind::Ui, mainWinRef, f.index, fieldVal)) {
                        ObjectRef vmRef{};
                        if (acceptIfAliveObject(fieldVal, vmRef)) {
                            return vmRef;
                        }
                    }
                }
            }
            for (const auto& m : ts->methods) {
                if (m.name == "get_ViewModel" && !m.staticMethod && m.id != 0) {
                    if (uiInterpreter) {
                        rs_rt::ExecutionOptions opts{};
                        opts.limits.instructionBudget = 1'000'000;
                        const auto res = uiInterpreter->invoke(m.id, {mainWinRef}, opts);
                        if (res.succeeded) {
                            ObjectRef vmRef{};
                            if (acceptIfAliveObject(res.value, vmRef)) {
                                return vmRef;
                            }
                        }
                    } else if (aotContext) {
                        UnifiedValue arg0 = mainWinRef;
                        UnifiedValue ret{};
                        if (aotContext->invoke(m.id, &arg0, 1u, ret)) {
                            ObjectRef vmRef{};
                            if (acceptIfAliveObject(ret, vmRef)) {
                                return vmRef;
                            }
                        }
                    }
                }
            }
            walkTid = ts->baseTypeId;
        }
    }

    return mainWinRef;
}

[[nodiscard]] SymbolId FindZeroArgMethodOnHierarchy(
    const ProgramImage* program,
    SymbolId startTypeId,
    std::string_view methodName) noexcept {
    if (!program || startTypeId == 0 || methodName.empty()) {
        return 0;
    }
    SymbolId walkTid = startTypeId;
    for (int depth = 0; depth < 16 && walkTid != 0; ++depth) {
        const auto* ts = program->resolveType(walkTid);
        if (!ts) {
            break;
        }
        for (const auto& m : ts->methods) {
            if (m.name == methodName && !m.staticMethod && m.parameters.empty() && m.id != 0) {
                return m.id;
            }
        }
        walkTid = ts->baseTypeId;
    }
    return 0;
}

[[nodiscard]] constexpr Color MakeHexColor(std::uint32_t rgba) noexcept {
    return Color{
        static_cast<float>((rgba >> 24u) & 0xFFu) / 255.0f,
        static_cast<float>((rgba >> 16u) & 0xFFu) / 255.0f,
        static_cast<float>((rgba >> 8u) & 0xFFu) / 255.0f,
        static_cast<float>(rgba & 0xFFu) / 255.0f,
    };
}

[[nodiscard]] std::optional<Color> ResolveThemeAccentColor(std::string_view accentName) noexcept {
    if (EqualsAsciiIgnoreCase(accentName, "Red")) return MakeHexColor(0xDE2B10FFu);
    if (EqualsAsciiIgnoreCase(accentName, "Green")) return MakeHexColor(0x2AA60CFFu);
    if (EqualsAsciiIgnoreCase(accentName, "Blue")) return MakeHexColor(0x1098DFFFu);
    if (EqualsAsciiIgnoreCase(accentName, "Orange") || EqualsAsciiIgnoreCase(accentName, "Amber")) {
        return MakeHexColor(0xDF9910FFu);
    }
    if (EqualsAsciiIgnoreCase(accentName, "Emerald")) return MakeHexColor(0x10DE62FFu);
    if (EqualsAsciiIgnoreCase(accentName, "Purple") ||
        EqualsAsciiIgnoreCase(accentName, "Violet") ||
        EqualsAsciiIgnoreCase(accentName, "Indigo")) {
        return MakeHexColor(0x894EEDFFu);
    }
    if (EqualsAsciiIgnoreCase(accentName, "Crimson")) return MakeHexColor(0xDE1043FFu);
    if (EqualsAsciiIgnoreCase(accentName, "Lime")) return MakeHexColor(0x99DF10FFu);
    if (EqualsAsciiIgnoreCase(accentName, "Aqua") || EqualsAsciiIgnoreCase(accentName, "Cyan")) {
        return MakeHexColor(0x10DDDFFFu);
    }
    return std::nullopt;
}

} // namespace

void SetPendingMainWindowMountTarget(
    const ExecutionDomainContext* domains,
    UIElement* rootControl,
    const AxbPackage* package) {
    PendingScriptUiState::Instance().SetPendingMountTarget(domains, rootControl, package);
}

void ClearPendingMainWindowMountTarget(const ExecutionDomainContext* domains) noexcept {
    PendingScriptUiState::Instance().SetPendingMountTarget(domains, nullptr, nullptr);
}

bool MountAndWireMainWindow(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext& domains,
    DualTrackBindingEngine& bindingEngine,
    StyleTriggerEngine& styleEngine,
    ObjectRef mainWinRef,
    const Interpreter* uiInterpreter,
    rs_aot::ExecutionContext* aotContext,
    AnimationEngine* animationEngine,
    ObjectRef* outViewModelRef) {
    if (!rootControl) {
        return false;
    }

    std::shared_ptr<UiResourceContext> domainCtx =
        !UiResourceContext::HasActiveThreadOverride()
            ? UiResourceContext::FindForDomains(&domains)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));

    const ProgramImage* program = bindingEngine.Program();
    if (program) {
        domains.registerProgramObservableMetadata(*program);
    }

    const Interpreter* effInterp = uiInterpreter ? uiInterpreter : bindingEngine.UiInterpreter();
    if (effInterp && bindingEngine.UiInterpreter() == nullptr) {
        bindingEngine.SetUiInterpreter(effInterp);
    }

    AnimationEngine* effAnim = animationEngine
        ? animationEngine
        : (styleEngine.GetAnimationEngine()
               ? styleEngine.GetAnimationEngine()
               : bindingEngine.GetAnimationEngine());
    if (effAnim) {
        bindingEngine.SetAnimationEngine(effAnim);
        styleEngine.SetAnimationEngine(effAnim);
    }

    styleEngine.SetBindingEngine(&bindingEngine);
    styleEngine.SetDomains(&domains, program, effInterp);
    styleEngine.SetAotExecutionContext(aotContext);

    rootControl->SetAsRootVisual(true, &domains.nativeHandles(), &domains.uiHeap());

    if (mainWinRef.valid()) {
        (void)rootControl->BindScriptWrapper(domains.nativeHandles(), domains.uiHeap(), mainWinRef);
    }

    const ObjectRef vmRef = ExtractViewModelFromMainWindow(
        domains,
        program,
        effInterp,
        aotContext,
        mainWinRef);
    if (outViewModelRef) {
        *outViewModelRef = vmRef;
    }
    if (vmRef.valid()) {
        (void)rootControl->SetValue(PropId_DataContext, vmRef, ValueSourceRank::Local);
    }

    // Apply any application/component resource dictionaries queued during MainWindow construction
    const std::vector<std::string> pendingDicts = PendingScriptUiState::Instance().ConsumeDictionaries(&domains);
    for (const std::string& dictUri : pendingDicts) {
        (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(dictUri, nullptr);
    }

    AxbPackage boundPkg;
    const AxbPackage* effPkg = &package;
    if (program) {
        boundPkg = package;
        std::string actualVmTypeName;
        if (vmRef.valid()) {
            if (auto* vh = domains.heapForObject(vmRef); vh && vh->isAlive(vmRef)) {
                const SymbolId vmTid = vh->objectTypeId(vmRef).value_or(0);
                if (const auto* vmTs = (vmTid != 0) ? program->resolveType(vmTid) : nullptr) {
                    actualVmTypeName = vmTs->moduleName.empty()
                        ? vmTs->name
                        : (vmTs->moduleName + "::" + vmTs->name);
                }
            }
        }
        (void)boundPkg.BindAgainstProgram(*program, actualVmTypeName, &domains);
        boundPkg.PinValuesInPools();
        effPkg = &boundPkg;
    }

    AxbTemplateRegistry::Instance().RegisterResources(*effPkg);
    styleEngine.ImportRegistryStyles();
    styleEngine.RegisterPackageStyles(*effPkg);

    IntrusivePtr<UIElement> preMountedRoot{};
    std::vector<IntrusivePtr<UIElement>> preMountedElements{};
    if (mainWinRef.valid() &&
        PendingScriptUiState::Instance().ConsumePreMountedWindow(
            &domains,
            mainWinRef,
            preMountedRoot,
            preMountedElements)) {
        if (preMountedRoot && preMountedRoot.Get() != rootControl) {
            constexpr std::uint16_t kTransferProps[] = {
                PropId_Title,
                PropId_Width,
                PropId_Height,
                PropId_MinWidth,
                PropId_MinHeight,
                PropId_MaxWidth,
                PropId_MaxHeight,
                PropId_Background,
                PropId_Foreground,
                PropId_Padding,
                PropId_Margin,
                PropId_BorderBrush,
                PropId_BorderThickness,
                PropId_CornerRadius,
                PropId_Opacity,
                PropId_Visibility,
                PropId_FontSize,
                PropId_Tag,
                PropId_ToolTipText,
            };
            for (const std::uint16_t pid : kTransferProps) {
                const UnifiedValue v = preMountedRoot->GetValue(pid);
                if (!std::holds_alternative<std::monostate>(v)) {
                    (void)rootControl->SetValue(pid, v, ValueSourceRank::Local);
                }
            }
            if (auto* srcGrid = preMountedRoot->DynamicCast<Grid>()) {
                if (auto* dstGrid = rootControl->DynamicCast<Grid>()) {
                    if (dstGrid->Rows().empty()) {
                        for (const auto& r : srcGrid->Rows()) {
                            dstGrid->AddRowDefinition(r.unit, r.value);
                        }
                    }
                    if (dstGrid->Columns().empty()) {
                        for (const auto& c : srcGrid->Columns()) {
                            dstGrid->AddColumnDefinition(c.unit, c.value);
                        }
                    }
                }
            }
            while (UIElement* ch = preMountedRoot->FirstChild()) {
                IntrusivePtr<UIElement> keepAlive(ch, true);
                preMountedRoot->RemoveChild(ch, &domains.nativeHandles(), &domains.uiHeap());
                rootControl->AddChild(keepAlive.Get(), &domains.nativeHandles(), &domains.uiHeap());
            }
            if (!preMountedElements.empty()) {
                preMountedElements[0] = IntrusivePtr<UIElement>(rootControl, true);
            }
        }
        if (!RebindMountedAxbTemplate(
                rootControl,
                *effPkg,
                preMountedElements,
                &domains,
                &bindingEngine,
                vmRef,
                effInterp,
                aotContext,
                effAnim)) {
            return false;
        }
    } else {
        if (!MountAxbTemplate(
                rootControl,
                *effPkg,
                &domains,
                &bindingEngine,
                vmRef,
                effInterp,
                aotContext,
                effAnim)) {
            return false;
        }
    }

    (void)styleEngine.BindSubtreeStyles(rootControl, true);
    AttachStyleEngineAcrossSubtree(rootControl, &styleEngine);

    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();

    if (mainWinRef.valid() && program) {
        if (auto* heap = domains.heapForObject(mainWinRef); heap && heap->isAlive(mainWinRef)) {
            const SymbolId mainWinTid = heap->objectTypeId(mainWinRef).value_or(0);
            const SymbolId onInitSym = FindZeroArgMethodOnHierarchy(program, mainWinTid, "OnInitialized");
            if (onInitSym != 0) {
                if (effInterp) {
                    rs_rt::ExecutionOptions execOpts{};
                    execOpts.limits.instructionBudget = 5'000'000;
                    (void)effInterp->invoke(onInitSym, {mainWinRef}, execOpts);
                } else if (aotContext) {
                    UnifiedValue arg0 = mainWinRef;
                    UnifiedValue ret{};
                    (void)aotContext->invoke(onInitSym, &arg0, 1u, ret);
                }
            }
        }
    }

    for (const std::string& dictUri : pendingDicts) {
        (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(dictUri, rootControl);
    }
    if (!pendingDicts.empty()) {
        styleEngine.ImportRegistryStyles();
        (void)styleEngine.BindSubtreeStyles(rootControl, true);
    }
    AttachStyleEngineAcrossSubtree(rootControl, &styleEngine);
    (void)bindingEngine.FlushTrackA();
    (void)styleEngine.EvaluateAndApply();
    (void)bindingEngine.FlushTrackA();
    return true;
}

IntrusivePtr<UIElement> MountAndWireMainWindow(
    const AxbPackage& package,
    ExecutionDomainContext& domains,
    DualTrackBindingEngine& bindingEngine,
    StyleTriggerEngine& styleEngine,
    ObjectRef mainWinRef,
    const Interpreter* uiInterpreter,
    rs_aot::ExecutionContext* aotContext,
    AnimationEngine* animationEngine,
    ObjectRef* outViewModelRef) {
    IntrusivePtr<UIElement> root =
        PendingScriptUiState::Instance().PeekPreMountedRoot(&domains, mainWinRef);
    if (!root) {
        root = CreateElementByTypeName(package.rootTypeName);
    }
    if (!root) {
        root = MakeIntrusive<UserControl>();
    }
    if (!MountAndWireMainWindow(
            root.Get(),
            package,
            domains,
            bindingEngine,
            styleEngine,
            mainWinRef,
            uiInterpreter,
            aotContext,
            animationEngine,
            outViewModelRef)) {
        return {};
    }
    return root;
}

ScriptUiBridge::ScriptUiBridge(
    ExecutionDomainContext& domains,
    const ProgramImage* program,
    const Interpreter* uiInterpreter)
    : domains_(domains)
    , ownedEngine_(std::make_unique<DualTrackBindingEngine>(domains, program, uiInterpreter))
    , engine_(ownedEngine_.get())
    , sharedEngineSlot_(std::make_shared<DualTrackBindingEngine*>(engine_)) {}

ScriptUiBridge::ScriptUiBridge(DualTrackBindingEngine& externalEngine) noexcept
    : domains_(externalEngine.Domains())
    , ownedEngine_(nullptr)
    , engine_(&externalEngine)
    , sharedEngineSlot_(std::make_shared<DualTrackBindingEngine*>(engine_)) {}

ScriptUiBridge::~ScriptUiBridge() noexcept {
    PendingScriptUiState::Instance().ClearForDomain(&domains_);
    if (sharedEngineSlot_) {
        *sharedEngineSlot_ = nullptr;
    }
}

void ScriptUiBridge::SetUnderlyingEngine(DualTrackBindingEngine* externalEngine) noexcept {
    if (externalEngine != nullptr) {
        engine_ = externalEngine;
    } else {
        if (!ownedEngine_) {
            ownedEngine_ = std::make_unique<DualTrackBindingEngine>(domains_);
        }
        engine_ = ownedEngine_.get();
    }
    if (sharedEngineSlot_) {
        *sharedEngineSlot_ = engine_;
    }
}

void ScriptUiBridge::RegisterCoreUiBindings(BindingRegistry& bindings) {
    auto* handlesPtr = &domains_.nativeHandles();
    auto* uiHeapPtr = &domains_.uiHeap();
    auto* domPtr = &domains_;
    auto engineSlot = sharedEngineSlot_;

    auto bindSdkFn = [&bindings](std::string_view shortName, rs_rt::ExternalFunction fn) {
        std::string qualified = "Aero.UI::";
        qualified.append(shortName);
        (void)bindings.bind(qualified, fn);
        (void)bindings.bind(std::string(shortName), std::move(fn));
    };

    // UIElement_GetWidth(handle) -> float
    bindSdkFn(
        "UIElement_GetWidth",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing NativeHandle argument";
                return std::nullopt;
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle)) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            return MakeFloat(elem->GetWidth());
        });

    // UIElement_SetWidth(handle, width) -> bool
    bindSdkFn(
        "UIElement_SetWidth",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_SetWidth";
                return std::nullopt;
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle)) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected NativeHandle";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            float w = 0.0f;
            if (TryGetFloat(args[1], w)) {
                elem->SetWidth(w);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(PropId_Width, &error);
                }
            } else {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected float width";
                return std::nullopt;
            }
            return UnifiedValue{true};
        });

    // UIElement_GetProperty(handle, propId) -> Value
    bindSdkFn(
        "UIElement_GetProperty",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_GetProperty";
                return std::nullopt;
            }
            NativeHandle handle{};
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!args[0].tryGetNativeHandle(handle) || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            return elem->GetValue(static_cast<std::uint16_t>(*propId));
        });

    // UIElement_SetProperty(handle, propId, value) -> bool
    bindSdkFn(
        "UIElement_SetProperty",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                error.code = ErrorCode::InvalidArguments;
                error.message = "missing arguments for UIElement_SetProperty";
                return std::nullopt;
            }
            NativeHandle handle{};
            const auto* propId = std::get_if<std::int64_t>(&args[1]);
            if (!args[0].tryGetNativeHandle(handle) || !propId) {
                error.code = ErrorCode::TypeMismatch;
                error.message = "expected (NativeHandle, int, Value)";
                return std::nullopt;
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "invalid or expired UIElement NativeHandle";
                return std::nullopt;
            }
            const auto targetProp = static_cast<std::uint16_t>(*propId);
            const bool ok = elem->SetValue(
                targetProp,
                args[2],
                ValueSourceRank::Local);
            if (ok && elem->BoundTwoWayEngine() != nullptr) {
                (void)elem->NotifyTwoWayPropertyMutated(targetProp, &error);
            }
            return UnifiedValue{ok};
        });

    // UIElement_GetDouble(handle, string propName) -> double
    bindSdkFn(
        "UIElement_GetDouble",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{0.0};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{0.0};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{0.0};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{0.0};
            }
            const UnifiedValue raw = elem->GetValue(propId);
            double d = 0.0;
            if (raw.tryGetDouble(d)) {
                return UnifiedValue{d};
            }
            float f = 0.0f;
            if (TryGetFloat(raw, f)) {
                return UnifiedValue{static_cast<double>(f)};
            }
            return UnifiedValue{0.0};
        });

    // UIElement_SetDouble(handle, string propName, double val) -> bool
    bindSdkFn(
        "UIElement_SetDouble",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            double d = 0.0;
            if (!args[2].tryGetDouble(d)) {
                float f = 0.0f;
                if (!TryGetFloat(args[2], f)) {
                    return UnifiedValue{false};
                }
                d = static_cast<double>(f);
            }
            const bool ok = elem->SetValue(
                propId,
                MakeFloat(static_cast<float>(d)),
                ValueSourceRank::Local);
            if (ok) {
                DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                    ? elem->BoundTwoWayEngine()
                    : (engineSlot ? *engineSlot : nullptr);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(propId, &error);
                }
                if (bindEng != nullptr) {
                    (void)bindEng->FlushTrackA();
                }
            }
            return UnifiedValue{ok};
        });

    // PasswordBox_GetPassword(handle) -> string
    bindSdkFn(
        "PasswordBox_GetPassword",
        [handlesPtr, uiHeapPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (!uiHeapPtr) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "UiHeap unavailable for PasswordBox_GetPassword";
                return std::nullopt;
            }
            if (args.empty() || !handlesPtr) {
                return uiHeapPtr->allocateString("", &error);
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return uiHeapPtr->allocateString("", &error);
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            std::string pwd;
            if (const auto* tb = elem ? elem->DynamicCast<TextBox>() : nullptr) {
                pwd = tb->GetText();
            }
            return uiHeapPtr->allocateString(pwd, &error);
        });

    // PasswordBox_ClearPassword(handle) -> bool
    bindSdkFn(
        "PasswordBox_ClearPassword",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            auto* tb = elem ? elem->DynamicCast<TextBox>() : nullptr;
            if (!tb) {
                return UnifiedValue{false};
            }
            tb->SetText("");
            tb->SetCaretIndex(0u);
            if (tb->BoundTwoWayEngine() != nullptr) {
                (void)tb->NotifyTwoWayPropertyMutated(PropId_Text, &error);
            }
            return UnifiedValue{true};
        });

    // ContentControl_LoadXaml(handle container, string xamlUri) -> bool
    bindSdkFn(
        "ContentControl_LoadXaml",
        [handlesPtr, uiHeapPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr || !domPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* host = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!host) {
                return UnifiedValue{false};
            }

            std::shared_ptr<UiResourceContext> domainCtx =
                !UiResourceContext::HasActiveThreadOverride()
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));

            DualTrackBindingEngine* bindEng = host->BoundTwoWayEngine();
            StyleTriggerEngine* styleEng = host->BoundStyleEngine();
            AnimationEngine* animEng = host->BoundAnimationEngine();
            ObjectRef inheritedDc{};
            for (const UIElement* anc = host; anc != nullptr; anc = anc->Parent()) {
                if (!bindEng && anc->BoundTwoWayEngine()) bindEng = anc->BoundTwoWayEngine();
                if (!styleEng && anc->BoundStyleEngine()) styleEng = anc->BoundStyleEngine();
                if (!animEng && anc->BoundAnimationEngine()) animEng = anc->BoundAnimationEngine();
                if (!inheritedDc.valid()) {
                    const UnifiedValue dcv = anc->GetValue(PropId_DataContext);
                    if (const auto* r = std::get_if<ObjectRef>(&dcv); r && r->valid()) {
                        inheritedDc = *r;
                    }
                }
            }
            if (!bindEng && engineSlot) {
                bindEng = *engineSlot;
            }
            if (!animEng) {
                if (styleEng && styleEng->GetAnimationEngine()) {
                    animEng = styleEng->GetAnimationEngine();
                } else if (bindEng && bindEng->GetAnimationEngine()) {
                    animEng = bindEng->GetAnimationEngine();
                }
            }

            std::vector<UIElement*> oldChildren;
            for (UIElement* ch = host->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
                if (ch != host->TemplateRoot()) {
                    oldChildren.push_back(ch);
                }
            }
            for (UIElement* ch : oldChildren) {
                if (animEng) {
                    (void)animEng->StopSubtreeAnimations(ch, false);
                }
                if (styleEng) {
                    (void)styleEng->UnbindSubtreeStyles(ch);
                }
                if (bindEng) {
                    (void)bindEng->UnbindSubtree(ch);
                }
                (void)host->RemoveChild(ch, handlesPtr, uiHeapPtr);
            }

            const std::string uri = FormatBindingString({}, &args[1], 1u, domPtr);
            if (uri.empty()) {
                (void)host->ForceClearSparseEntry(PropId_Content);
                (void)host->ForceClearSparseEntry(PropId_Text);
                return UnifiedValue{true};
            }

            AxbPackage subPkg{};
            bool haveSubPkg = false;
            if (const AxbPackage* cached = AxbTemplateRegistry::Instance().Find(uri)) {
                subPkg = *cached;
                haveSubPkg = true;
            } else if (const auto compileFn = GetRuntimeXamlCompileHook()) {
                const auto searchDirs = UiResourceContext::Current().XamlSearchDirectoriesSnapshot();
                if (compileFn(uri, searchDirs, subPkg)) {
                    AxbTemplateRegistry::Instance().Register(uri, subPkg);
                    haveSubPkg = true;
                }
            }
            if (!haveSubPkg) {
                return UnifiedValue{false};
            }

            if (bindEng && bindEng->Program()) {
                std::string inheritedDcType;
                if (inheritedDc.valid()) {
                    if (auto* ih = domPtr->heapForObject(inheritedDc); ih && ih->isAlive(inheritedDc)) {
                        const SymbolId dcTid = ih->objectTypeId(inheritedDc).value_or(0);
                        if (const auto* dcTs = (dcTid != 0) ? bindEng->Program()->resolveType(dcTid) : nullptr) {
                            inheritedDcType = dcTs->moduleName.empty()
                                ? dcTs->name
                                : (dcTs->moduleName + "::" + dcTs->name);
                        }
                    }
                }
                (void)subPkg.BindAgainstProgram(*bindEng->Program(), inheritedDcType, domPtr);
                subPkg.PinValuesInPools();
            }
            AxbTemplateRegistry::Instance().RegisterResources(subPkg);
            if (styleEng) {
                styleEng->ImportRegistryStyles();
                styleEng->RegisterPackageStyles(subPkg);
            }

            IntrusivePtr<UIElement> child = CreateElementByTypeName(subPkg.rootTypeName);
            if (!child) {
                child = MakeIntrusive<UserControl>();
            }
            if (!MountAxbTemplate(
                    child.Get(),
                    subPkg,
                    domPtr,
                    bindEng,
                    inheritedDc,
                    bindEng ? bindEng->UiInterpreter() : nullptr,
                    styleEng ? styleEng->GetAotExecutionContext() : nullptr,
                    animEng)) {
                return UnifiedValue{false};
            }

            (void)host->AddChild(child.Get(), handlesPtr, uiHeapPtr);
            if (styleEng) {
                (void)styleEng->BindSubtreeStyles(child.Get(), true);
                AttachStyleEngineAcrossSubtree(child.Get(), styleEng);
            }

            const NativeHandle childHandle = child->ToNativeHandle(*handlesPtr);
            (void)host->SetValue(PropId_Content, UnifiedValue{childHandle}, ValueSourceRank::Local);
            (void)host->SetValue(PropId_Text, UnifiedValue{childHandle}, ValueSourceRank::Local);
            host->SetVisibility(Visibility::Visible);

            if (bindEng) {
                (void)bindEng->FlushTrackA();
            }
            if (styleEng) {
                (void)styleEng->EvaluateSubtree(child.Get(), false);
            }
            return UnifiedValue{true};
        });

    // GUI_LoadXaml(handle container, string xamlUri) -> bool (C++ Noesis::GUI::LoadXaml equivalent)
    if (const auto existingLoadXaml = bindings.resolve(rs_bc::FunctionReference{0, "Aero.UI::ContentControl_LoadXaml"})) {
        if (existingLoadXaml->function) {
            bindSdkFn("GUI_LoadXaml", *existingLoadXaml->function);
        }
    }

    // GUI_LoadComponent<T>(T component, string uri) -> bool (WPF / C++ InitializeComponent equivalent)
    bindSdkFn(
        "GUI_LoadComponent",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !domPtr) {
                return UnifiedValue{false};
            }
            const std::string uri = FormatBindingString({}, &args[1], 1u, domPtr);
            std::shared_ptr<UiResourceContext> domainCtx =
                !UiResourceContext::HasActiveThreadOverride()
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));

            DualTrackBindingEngine* liveEng = (engineSlot && *engineSlot) ? *engineSlot : nullptr;
            const ProgramImage* prog = liveEng ? liveEng->Program() : nullptr;

            std::string strippedUri = uri;
            if (const auto slash = strippedUri.find_last_of("/\\"); slash != std::string::npos) {
                strippedUri = strippedUri.substr(slash + 1u);
            }
            if (strippedUri.size() > 5u &&
                EqualsAsciiIgnoreCase(strippedUri.substr(strippedUri.size() - 5u), ".xaml")) {
                strippedUri.resize(strippedUri.size() - 5u);
            }

            if (const auto* ownerRef = std::get_if<ObjectRef>(&args[0]); ownerRef && ownerRef->valid()) {
                if (PendingScriptUiState::Instance().HasPreMountedWindow(domPtr, *ownerRef)) {
                    return UnifiedValue{true};
                }

                std::string ownerShortTypeName;
                std::string ownerFullTypeName;
                std::uint32_t rootHandleFieldIdx = PropertySlotHandle::InvalidIndex;
                if (prog) {
                    if (auto* ownerHeap = domPtr->heapForObject(*ownerRef); ownerHeap && ownerHeap->isAlive(*ownerRef)) {
                        const SymbolId ownerTid = ownerHeap->objectTypeId(*ownerRef).value_or(0);
                        if (const auto* ts = (ownerTid != 0) ? prog->resolveType(ownerTid) : nullptr) {
                            ownerShortTypeName = ts->name;
                            ownerFullTypeName = ts->moduleName.empty() ? ts->name : (ts->moduleName + "." + ts->name);
                            for (std::size_t fIdx = 0; fIdx < ts->fields.size(); ++fIdx) {
                                if (ts->fields[fIdx].name == "__nativeRootHandle" &&
                                    ts->fields[fIdx].type == rs_sem::PrimitiveType::Handle) {
                                    rootHandleFieldIdx = static_cast<std::uint32_t>(fIdx);
                                    break;
                                }
                            }
                        }
                    }
                }

                const auto pendingTarget = PendingScriptUiState::Instance().FindPendingMountTarget(domPtr);
                const AxbPackage* pkg = pendingTarget.package;
                if (!pkg && !uri.empty()) {
                    pkg = AxbTemplateRegistry::Instance().Find(uri);
                }
                if (!pkg && !strippedUri.empty()) {
                    pkg = AxbTemplateRegistry::Instance().Find(strippedUri);
                }
                if (!pkg && !ownerFullTypeName.empty()) {
                    pkg = AxbTemplateRegistry::Instance().Find(ownerFullTypeName);
                }
                if (!pkg && !ownerShortTypeName.empty()) {
                    pkg = AxbTemplateRegistry::Instance().Find(ownerShortTypeName);
                }
                if (!pkg && !uri.empty()) {
                    if (const auto compileFn = GetRuntimeXamlCompileHook()) {
                        AxbPackage compiledPkg{};
                        const auto searchDirs = UiResourceContext::Current().XamlSearchDirectoriesSnapshot();
                        if (compileFn(uri, searchDirs, compiledPkg)) {
                            AxbTemplateRegistry::Instance().Register(
                                !compiledPkg.className.empty() ? compiledPkg.className : uri,
                                compiledPkg);
                            AxbTemplateRegistry::Instance().RegisterResources(compiledPkg);
                            pkg = AxbTemplateRegistry::Instance().Find(
                                !compiledPkg.className.empty() ? compiledPkg.className : uri);
                        }
                    }
                }

                if (pkg) {
                    if (rootHandleFieldIdx != PropertySlotHandle::InvalidIndex && handlesPtr) {
                        UnifiedValue rootHandleVal{};
                        if (domPtr->readObjectField(DomainKind::Ui, *ownerRef, rootHandleFieldIdx, rootHandleVal)) {
                            if (const auto* nh = std::get_if<NativeHandle>(&rootHandleVal); nh && nh->valid()) {
                                if (UIElement::FromNativeHandle(*handlesPtr, *nh) != nullptr) {
                                    return UnifiedValue{true};
                                }
                            }
                        }
                    }
                    IntrusivePtr<UIElement> targetRoot = pendingTarget.rootControl;
                    if (!targetRoot) {
                        const std::string_view rootType = pkg->rootTypeName.empty()
                            ? std::string_view("Grid")
                            : std::string_view(pkg->rootTypeName);
                        targetRoot = CreateElementByTypeName(rootType);
                    }
                    if (targetRoot) {
                        targetRoot->SetAsRootVisual(true, &domPtr->nativeHandles(), &domPtr->uiHeap());
                        (void)targetRoot->BindScriptWrapper(domPtr->nativeHandles(), domPtr->uiHeap(), *ownerRef);
                        AxbPackage boundPkg = *pkg;
                        if (prog) {
                            (void)boundPkg.BindAgainstProgram(*prog, "", domPtr);
                            boundPkg.PinValuesInPools();
                        }
                        std::vector<IntrusivePtr<UIElement>> mountedElements;
                        if (PreMountAxbTemplateStructure(
                                targetRoot.Get(),
                                boundPkg,
                                domPtr,
                                liveEng,
                                *ownerRef,
                                mountedElements,
                                liveEng ? liveEng->UiInterpreter() : nullptr,
                                nullptr,
                                liveEng ? liveEng->GetAnimationEngine() : nullptr)) {
                            PendingScriptUiState::Instance().RecordPreMountedWindow(
                                domPtr,
                                *ownerRef,
                                targetRoot,
                                std::move(mountedElements));
                            return UnifiedValue{true};
                        }
                    }
                }
            }

            if (!uri.empty() && AxbTemplateRegistry::Instance().Find(uri) != nullptr) {
                return UnifiedValue{true};
            }
            if (!uri.empty()) {
                if (const auto compileFn = GetRuntimeXamlCompileHook()) {
                    AxbPackage compiledPkg{};
                    const auto searchDirs = UiResourceContext::Current().XamlSearchDirectoriesSnapshot();
                    if (compileFn(uri, searchDirs, compiledPkg)) {
                        AxbTemplateRegistry::Instance().Register(uri, compiledPkg);
                        AxbTemplateRegistry::Instance().RegisterResources(compiledPkg);
                        return UnifiedValue{true};
                    }
                }
            }
            return UnifiedValue{true};
        });

    if (const auto existingLoadComp = bindings.resolve(rs_bc::FunctionReference{0, "Aero.UI::GUI_LoadComponent"})) {
        if (existingLoadComp->function) {
            bindSdkFn("Aero_LoadComponent", *existingLoadComp->function);
        }
    }

    // GUI_SetDataContext<TOwner, TContext>(TOwner owner, TContext dataContext) -> bool (WPF / C++ FrameworkElement::SetDataContext)
    bindSdkFn(
        "GUI_SetDataContext",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !domPtr) {
                return UnifiedValue{false};
            }
            ObjectRef dcRef{};
            if (const auto* r1 = std::get_if<ObjectRef>(&args[1]); r1 && r1->valid()) {
                dcRef = *r1;
            }
            if (const auto* ownerRef = std::get_if<ObjectRef>(&args[0]); ownerRef && ownerRef->valid()) {
                PendingScriptUiState::Instance().SetDataContext(domPtr, *ownerRef, dcRef);
                std::uint32_t rootHandleFieldIdx = PropertySlotHandle::InvalidIndex;
                const DualTrackBindingEngine* liveEng = (engineSlot && *engineSlot) ? *engineSlot : nullptr;
                const ProgramImage* prog = liveEng ? liveEng->Program() : nullptr;
                if (prog) {
                    if (auto* ownerHeap = domPtr->heapForObject(*ownerRef); ownerHeap && ownerHeap->isAlive(*ownerRef)) {
                        const SymbolId ownerTid = ownerHeap->objectTypeId(*ownerRef).value_or(0);
                        if (const auto* ts = (ownerTid != 0) ? prog->resolveType(ownerTid) : nullptr) {
                            for (std::size_t fIdx = 0; fIdx < ts->fields.size(); ++fIdx) {
                                if (ts->fields[fIdx].name == "__nativeRootHandle" &&
                                    ts->fields[fIdx].type == rs_sem::PrimitiveType::Handle) {
                                    rootHandleFieldIdx = static_cast<std::uint32_t>(fIdx);
                                    break;
                                }
                            }
                        }
                    }
                }
                UnifiedValue rootHandleVal{};
                bool gotHandle = false;
                if (rootHandleFieldIdx != PropertySlotHandle::InvalidIndex) {
                    gotHandle = domPtr->readObjectField(DomainKind::Ui, *ownerRef, rootHandleFieldIdx, rootHandleVal);
                } else if (handlesPtr) {
                    for (std::uint32_t fIdx = 0u; fIdx < 64u; ++fIdx) {
                        UnifiedValue cand{};
                        if (!domPtr->readObjectField(DomainKind::Ui, *ownerRef, fIdx, cand)) {
                            break;
                        }
                        NativeHandle candNh{};
                        if (cand.tryGetNativeHandle(candNh) && candNh.valid()) {
                            if (auto* candElem = UIElement::FromNativeHandle(*handlesPtr, candNh);
                                candElem && candElem->BoundScriptWrapper() == *ownerRef) {
                                rootHandleVal = cand;
                                gotHandle = true;
                                break;
                            }
                        }
                    }
                }
                if (gotHandle) {
                    NativeHandle nh{};
                    if (handlesPtr && rootHandleVal.tryGetNativeHandle(nh) && nh.valid()) {
                        if (auto* elem = UIElement::FromNativeHandle(*handlesPtr, nh)) {
                            (void)elem->SetValue(PropId_DataContext, dcRef, ValueSourceRank::Local);
                            DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                                ? elem->BoundTwoWayEngine()
                                : (engineSlot ? *engineSlot : nullptr);
                            if (bindEng) {
                                (void)bindEng->RebindSubtreeRootObject(elem, dcRef);
                                (void)bindEng->FlushTrackA();
                            }
                        }
                    }
                }
                return UnifiedValue{true};
            }
            NativeHandle directHandle{};
            if (handlesPtr && args[0].tryGetNativeHandle(directHandle) && directHandle.valid()) {
                if (auto* elem = UIElement::FromNativeHandle(*handlesPtr, directHandle)) {
                    (void)elem->SetValue(PropId_DataContext, dcRef, ValueSourceRank::Local);
                    DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                        ? elem->BoundTwoWayEngine()
                        : (engineSlot ? *engineSlot : nullptr);
                    if (bindEng) {
                        (void)bindEng->RebindSubtreeRootObject(elem, dcRef);
                        (void)bindEng->FlushTrackA();
                    }
                    return UnifiedValue{true};
                }
            }
            return UnifiedValue{false};
        });

    if (const auto existingSetDc = bindings.resolve(rs_bc::FunctionReference{0, "Aero.UI::GUI_SetDataContext"})) {
        if (existingSetDc->function) {
            bindSdkFn("__Aero_SetDataContext", *existingSetDc->function);
            bindSdkFn("Aero_SetDataContext", *existingSetDc->function);
        }
    }

    // Application_LoadResources(handle root, string uri) -> bool
    bindSdkFn(
        "Application_LoadResources",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !domPtr) {
                return UnifiedValue{false};
            }
            UIElement* scopeRoot = nullptr;
            NativeHandle handle{};
            if (handlesPtr && args[0].tryGetNativeHandle(handle) && handle.valid()) {
                scopeRoot = UIElement::FromNativeHandle(*handlesPtr, handle);
            }
            const std::string uri = FormatBindingString({}, &args[1], 1u, domPtr);
            if (uri.empty()) {
                return UnifiedValue{false};
            }
            std::shared_ptr<UiResourceContext> domainCtx =
                !UiResourceContext::HasActiveThreadOverride()
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));
            const std::size_t applied = AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(uri, scopeRoot);
            if (!scopeRoot) {
                PendingScriptUiState::Instance().RecordDictionary(domPtr, uri);
            }
            if (scopeRoot) {
                if (auto* se = scopeRoot->BoundStyleEngine()) {
                    se->ImportRegistryStyles();
                    (void)se->BindSubtreeStyles(scopeRoot, true);
                    (void)se->EvaluateSubtree(scopeRoot, false);
                }
            }
            return UnifiedValue{applied > 0u || !scopeRoot};
        });

    if (const auto existingLoadRes = bindings.resolve(rs_bc::FunctionReference{0, "Aero.UI::Application_LoadResources"})) {
        if (existingLoadRes->function) {
            bindSdkFn("Aero_LoadResources", *existingLoadRes->function);
        }
    }

    // Application_SetResources(string uri) -> bool (C++ ApplicationLauncher::SetApplicationResources)
    bindSdkFn(
        "Application_SetResources",
        [domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !domPtr) {
                return UnifiedValue{false};
            }
            const std::string uri = FormatBindingString({}, &args[0], 1u, domPtr);
            if (uri.empty()) {
                return UnifiedValue{false};
            }
            std::shared_ptr<UiResourceContext> domainCtx =
                !UiResourceContext::HasActiveThreadOverride()
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));
            (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(uri, nullptr);
            PendingScriptUiState::Instance().RecordDictionary(domPtr, uri);
            return UnifiedValue{true};
        });

    // UIElement_GetInt(handle elem, string propName) -> int
    bindSdkFn(
        "UIElement_GetInt",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return value::FromInt(0);
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return value::FromInt(0);
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return value::FromInt(0);
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return value::FromInt(0);
            }
            const UnifiedValue raw = elem->GetValue(propId);
            std::int64_t iv = 0;
            if (raw.tryGetInt64(iv)) {
                return value::FromInt(iv);
            }
            EnumValue ev{};
            if (raw.tryGetEnum(ev)) {
                return value::FromInt(ev.value);
            }
            float f = 0.0f;
            if (TryGetFloat(raw, f)) {
                return value::FromInt(static_cast<std::int64_t>(std::llround(f)));
            }
            return value::FromInt(0);
        });

    // UIElement_SetInt(handle elem, string propName, int val) -> bool
    bindSdkFn(
        "UIElement_SetInt",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            std::int64_t iv = 0;
            if (!args[2].tryGetInt64(iv)) {
                float f = 0.0f;
                if (!TryGetFloat(args[2], f)) {
                    return UnifiedValue{false};
                }
                iv = static_cast<std::int64_t>(std::llround(f));
            }
            const UnifiedValue v = (propId == PropId_Value || propId == PropId_Minimum ||
                                    propId == PropId_Maximum || propId == PropId_SmallChange)
                ? MakeFloat(static_cast<float>(iv))
                : value::FromInt(iv);
            const bool ok = elem->SetValue(propId, v, ValueSourceRank::Local);
            if (ok) {
                DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                    ? elem->BoundTwoWayEngine()
                    : (engineSlot ? *engineSlot : nullptr);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(propId, &error);
                }
                if (bindEng != nullptr) {
                    (void)bindEng->FlushTrackA();
                }
            }
            return UnifiedValue{ok};
        });

    // Application_ApplyDictionary(string uri) -> bool
    bindSdkFn(
        "Application_ApplyDictionary",
        [domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.empty()) {
                return UnifiedValue{false};
            }
            std::string uri = FormatBindingString({}, &args[0], 1u, domPtr);
            if (uri.empty()) {
                return UnifiedValue{false};
            }
            std::shared_ptr<UiResourceContext> domainCtx =
                (domPtr && !UiResourceContext::HasActiveThreadOverride())
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));
            const std::size_t applied = AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(uri, nullptr);
            return UnifiedValue{applied > 0u};
        });

    // Application_LoadTheme(handle root, string colorName, string accentName) -> bool
    bindSdkFn(
        "Application_LoadTheme",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3) {
                return UnifiedValue{false};
            }
            UIElement* rootElem = nullptr;
            NativeHandle handle{};
            if (handlesPtr && args[0].tryGetNativeHandle(handle) && handle.valid()) {
                rootElem = UIElement::FromNativeHandle(*handlesPtr, handle);
            }
            const std::string colorName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::string accentName = FormatBindingString({}, &args[2], 1u, domPtr);
            if (colorName.empty() && accentName.empty() && !rootElem) {
                return UnifiedValue{false};
            }

            std::shared_ptr<UiResourceContext> domainCtx =
                (domPtr && !UiResourceContext::HasActiveThreadOverride())
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));

            const std::string normColor = EqualsAsciiIgnoreCase(colorName, "Dark") ? "Dark" : "Light";
            const std::string normAccent = accentName.empty() ? "Blue" : accentName;
            const std::string exactDict = "NoesisTheme." + normColor + normAccent + ".xaml";
            const std::string fallbackDict = "NoesisTheme." + normColor + "Blue.xaml";

            std::size_t applied = AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(exactDict, rootElem);
            if (applied == 0u) {
                applied = AxbTemplateRegistry::Instance().ApplyRegisteredDictionary("Theme/" + exactDict, rootElem);
            }
            if (applied == 0u) {
                applied = AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(fallbackDict, rootElem);
                if (applied == 0u) {
                    (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary("Theme/" + fallbackDict, rootElem);
                }
            }

            if (const auto accentColor = ResolveThemeAccentColor(normAccent)) {
                const UnifiedValue accentVal = MakeFloat4InlineFast(accentColor->ToFloat4());
                (void)AxbTemplateRegistry::Instance().SetDynamicResource("Brush.Primary.Normal", accentVal, rootElem);
                (void)AxbTemplateRegistry::Instance().SetDynamicResource("Color.Primary.Normal", accentVal, rootElem);
                (void)AxbTemplateRegistry::Instance().SetDynamicResource("Theme.Accent", accentVal, rootElem);
            }

            if (rootElem) {
                StyleTriggerEngine* styleEng = rootElem->BoundStyleEngine();
                for (const UIElement* anc = rootElem; !styleEng && anc != nullptr; anc = anc->Parent()) {
                    styleEng = anc->BoundStyleEngine();
                }
                if (styleEng) {
                    styleEng->ImportRegistryStyles();
                }
            }
            return UnifiedValue{true};
        });

    // UIElement_SetVisibility(handle elem, int visibility) -> bool
    bindSdkFn(
        "UIElement_SetVisibility",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            std::int64_t rawVis = 0;
            if (!args[1].tryGetInt64(rawVis)) {
                float fv = 0.0f;
                if (!TryGetFloat(args[1], fv)) {
                    return UnifiedValue{false};
                }
                rawVis = static_cast<std::int64_t>(std::lround(fv));
            }
            Visibility vis = Visibility::Visible;
            if (rawVis == 1) {
                vis = Visibility::Hidden;
            } else if (rawVis >= 2) {
                vis = Visibility::Collapsed;
            }
            elem->SetVisibility(vis);
            if (elem->BoundTwoWayEngine() != nullptr) {
                (void)elem->NotifyTwoWayPropertyMutated(PropId_Visibility, &error);
            }
            return UnifiedValue{true};
        });

    // UIElement_GetVisibility(handle elem) -> int
    bindSdkFn(
        "UIElement_GetVisibility",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                return UnifiedValue{std::int64_t{0}};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{std::int64_t{0}};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{std::int64_t{0}};
            }
            return UnifiedValue{static_cast<std::int64_t>(elem->GetVisibility())};
        });

    // UIElement_Focus(handle elem) -> bool
    bindSdkFn(
        "UIElement_Focus",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            UIElement* treeRoot = elem;
            while (treeRoot->Parent() != nullptr) {
                treeRoot = treeRoot->Parent();
            }
            std::vector<UIElement*> stack{treeRoot};
            while (!stack.empty()) {
                UIElement* cur = stack.back();
                stack.pop_back();
                if (!cur) continue;
                if (cur != elem && cur->IsFocused()) {
                    cur->SetFocused(false);
                }
                for (UIElement* ch = cur->FirstChild(); ch != nullptr; ch = ch->NextSibling()) {
                    stack.push_back(ch);
                }
            }
            elem->SetFocused(true);
            return UnifiedValue{true};
        });

    // UIElement_FindName(handle root, string name) -> handle
    bindSdkFn(
        "UIElement_FindName",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{NativeHandle{}};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{NativeHandle{}};
            }
            auto* rootElem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!rootElem) {
                return UnifiedValue{NativeHandle{}};
            }
            const std::string name = FormatBindingString({}, &args[1], 1u, domPtr);
            if (name.empty()) {
                return UnifiedValue{NativeHandle{}};
            }
            UIElement* found = rootElem->FindName(name);
            if (!found) {
                UIElement* nsRoot = UIElement::FindNamescopeRoot(rootElem);
                if (nsRoot && nsRoot != rootElem) {
                    found = nsRoot->FindName(name);
                }
            }
            return found ? UnifiedValue{found->ToNativeHandle(*handlesPtr)} : UnifiedValue{NativeHandle{}};
        });

    // Storyboard_Begin(handle root, string storyboardKey) -> bool
    bindSdkFn(
        "Storyboard_Begin",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* scopeRoot = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!scopeRoot) {
                return UnifiedValue{false};
            }
            const std::string sbKey = FormatBindingString({}, &args[1], 1u, domPtr);
            if (sbKey.empty()) {
                return UnifiedValue{false};
            }

            std::shared_ptr<UiResourceContext> domainCtx =
                (domPtr && !UiResourceContext::HasActiveThreadOverride())
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));

            const AxbStoryboard* sb = nullptr;
            AnimationEngine* animEng = scopeRoot->BoundAnimationEngine();
            for (const UIElement* anc = scopeRoot; anc != nullptr; anc = anc->Parent()) {
                if (auto* se = anc->BoundStyleEngine()) {
                    if (!sb) {
                        sb = se->FindStoryboard(sbKey);
                    }
                    if (!animEng && se->GetAnimationEngine()) {
                        animEng = se->GetAnimationEngine();
                    }
                }
                if (!animEng && anc->BoundAnimationEngine()) {
                    animEng = anc->BoundAnimationEngine();
                }
            }
            if (!sb) {
                sb = AxbTemplateRegistry::Instance().FindStoryboard(sbKey);
            }
            if (!animEng && engineSlot && *engineSlot) {
                animEng = (*engineSlot)->GetAnimationEngine();
            }
            if (!sb || !animEng) {
                return UnifiedValue{false};
            }
            const std::size_t started = animEng->BeginStoryboard(scopeRoot, *sb);
            return UnifiedValue{started > 0u || sb->timelines.empty()};
        });

    // Storyboard_Stop(handle root, string storyboardKey) -> bool
    bindSdkFn(
        "Storyboard_Stop",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* scopeRoot = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!scopeRoot) {
                return UnifiedValue{false};
            }
            const std::string sbKey = FormatBindingString({}, &args[1], 1u, domPtr);
            if (sbKey.empty()) {
                return UnifiedValue{false};
            }

            std::shared_ptr<UiResourceContext> domainCtx =
                (domPtr && !UiResourceContext::HasActiveThreadOverride())
                    ? UiResourceContext::FindForDomains(domPtr)
                    : nullptr;
            ScopedUiResourceContext ctxGuard(std::move(domainCtx));

            const AxbStoryboard* sb = nullptr;
            AnimationEngine* animEng = scopeRoot->BoundAnimationEngine();
            for (const UIElement* anc = scopeRoot; anc != nullptr; anc = anc->Parent()) {
                if (auto* se = anc->BoundStyleEngine()) {
                    if (!sb) {
                        sb = se->FindStoryboard(sbKey);
                    }
                    if (!animEng && se->GetAnimationEngine()) {
                        animEng = se->GetAnimationEngine();
                    }
                }
                if (!animEng && anc->BoundAnimationEngine()) {
                    animEng = anc->BoundAnimationEngine();
                }
            }
            if (!sb) {
                sb = AxbTemplateRegistry::Instance().FindStoryboard(sbKey);
            }
            if (!animEng && engineSlot && *engineSlot) {
                animEng = (*engineSlot)->GetAnimationEngine();
            }
            if (!sb || !animEng) {
                return UnifiedValue{false};
            }
            const std::size_t stopped = animEng->StopStoryboard(scopeRoot, *sb, true);
            return UnifiedValue{stopped > 0u};
        });

    // UIElement_GetString(handle elem, string propName) -> string
    bindSdkFn(
        "UIElement_GetString",
        [handlesPtr, uiHeapPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (!uiHeapPtr) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "UiHeap unavailable for UIElement_GetString";
                return std::nullopt;
            }
            if (args.size() < 2 || !handlesPtr) {
                return uiHeapPtr->allocateString("", &error);
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return uiHeapPtr->allocateString("", &error);
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return uiHeapPtr->allocateString("", &error);
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            std::string outStr;
            if (propId != 0xFFFFu) {
                const UnifiedValue v = elem->GetValue(propId);
                if (const auto* sv = std::get_if<StringValue>(&v)) {
                    outStr = sv->str();
                } else if (propId == PropId_Content || propId == PropId_Text) {
                    if (const auto* btn = elem->DynamicCast<Button>()) {
                        outStr = btn->GetContentText();
                    } else if (const auto* tb = elem->DynamicCast<TextBlock>()) {
                        outStr = tb->GetText();
                    }
                }
            }
            return uiHeapPtr->allocateString(outStr, &error);
        });

    // UIElement_SetString(handle elem, string propName, string val) -> bool
    bindSdkFn(
        "UIElement_SetString",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            const std::string valStr = FormatBindingString({}, &args[2], 1u, domPtr);
            const bool ok = elem->SetValue(propId, value::FromString(valStr), ValueSourceRank::Local);
            if (ok) {
                DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                    ? elem->BoundTwoWayEngine()
                    : (engineSlot ? *engineSlot : nullptr);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(propId, &error);
                }
                if (bindEng != nullptr) {
                    (void)bindEng->FlushTrackA();
                }
            }
            return UnifiedValue{ok};
        });

    // UIElement_GetBool(handle elem, string propName) -> bool
    bindSdkFn(
        "UIElement_GetBool",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            const UnifiedValue raw = elem->GetValue(propId);
            if (const auto* b = std::get_if<bool>(&raw)) {
                return UnifiedValue{*b};
            }
            std::int64_t iv = 0;
            if (raw.tryGetInt64(iv)) {
                return UnifiedValue{iv != 0};
            }
            return UnifiedValue{false};
        });

    // UIElement_SetBool(handle elem, string propName, bool val) -> bool
    bindSdkFn(
        "UIElement_SetBool",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            bool bv = false;
            if (const auto* bp = std::get_if<bool>(&args[2])) {
                bv = *bp;
            } else {
                std::int64_t iv = 0;
                if (args[2].tryGetInt64(iv)) bv = (iv != 0);
            }
            const bool ok = elem->SetValue(propId, UnifiedValue{bv}, ValueSourceRank::Local);
            if (ok) {
                DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                    ? elem->BoundTwoWayEngine()
                    : (engineSlot ? *engineSlot : nullptr);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(propId, &error);
                }
                if (bindEng != nullptr) {
                    (void)bindEng->FlushTrackA();
                }
            }
            return UnifiedValue{ok};
        });

    // UIElement_GetFloat(handle elem, string propName) -> float
    bindSdkFn(
        "UIElement_GetFloat",
        [handlesPtr, domPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.size() < 2 || !handlesPtr) {
                return MakeFloat(0.0f);
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return MakeFloat(0.0f);
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return MakeFloat(0.0f);
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return MakeFloat(0.0f);
            }
            float f = 0.0f;
            (void)TryGetFloat(elem->GetValue(propId), f);
            return MakeFloat(f);
        });

    // UIElement_SetFloat(handle elem, string propName, float val) -> bool
    bindSdkFn(
        "UIElement_SetFloat",
        [handlesPtr, domPtr, engineSlot](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (args.size() < 3 || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            const std::string propName = FormatBindingString({}, &args[1], 1u, domPtr);
            const std::uint16_t propId = ResolveSdkPropertyIdByName(propName);
            if (propId == 0xFFFFu) {
                return UnifiedValue{false};
            }
            float f = 0.0f;
            if (!TryGetFloat(args[2], f)) {
                return UnifiedValue{false};
            }
            const bool ok = elem->SetValue(propId, MakeFloat(f), ValueSourceRank::Local);
            if (ok) {
                DualTrackBindingEngine* bindEng = elem->BoundTwoWayEngine()
                    ? elem->BoundTwoWayEngine()
                    : (engineSlot ? *engineSlot : nullptr);
                if (elem->BoundTwoWayEngine() != nullptr) {
                    (void)elem->NotifyTwoWayPropertyMutated(propId, &error);
                }
                if (bindEng != nullptr) {
                    (void)bindEng->FlushTrackA();
                }
            }
            return UnifiedValue{ok};
        });

    // TreeView_GetSelectedHeader(handle elem) -> string
    bindSdkFn(
        "TreeView_GetSelectedHeader",
        [handlesPtr, uiHeapPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError& error)
            -> std::optional<UnifiedValue> {
            if (!uiHeapPtr) {
                error.code = ErrorCode::InvalidObjectReference;
                error.message = "UiHeap unavailable";
                return std::nullopt;
            }
            if (args.empty() || !handlesPtr) {
                return uiHeapPtr->allocateString("", &error);
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return uiHeapPtr->allocateString("", &error);
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            std::string hdr;
            if (const auto* tv = elem ? elem->DynamicCast<TreeView>() : nullptr) {
                hdr = tv->GetSelectedHeader();
            }
            return uiHeapPtr->allocateString(hdr, &error);
        });

    // Window_Close(handle elem) -> bool
    bindSdkFn(
        "Window_Close",
        [handlesPtr](
            const rs_bc::FunctionReference&,
            ValueSpan args,
            RuntimeError&)
            -> std::optional<UnifiedValue> {
            if (args.empty() || !handlesPtr) {
                return UnifiedValue{false};
            }
            NativeHandle handle{};
            if (!args[0].tryGetNativeHandle(handle) || !handle.valid()) {
                return UnifiedValue{false};
            }
            auto* elem = UIElement::FromNativeHandle(*handlesPtr, handle);
            if (!elem) {
                return UnifiedValue{false};
            }
            elem->SetVisibility(Visibility::Collapsed);
            return UnifiedValue{true};
        });
}

bool ScriptUiBridge::BindObservableProperty(
    ObjectRef sourceObject,
    PropertySlotHandle sourceSlot,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    return BindObservableProperty(
        sourceObject,
        sourceSlot,
        targetElement,
        targetPropertyId,
        BindingMode::OneWay,
        ValueConverterKind::None,
        {},
        {},
        error);
}

bool ScriptUiBridge::BindObservableProperty(
    ObjectRef sourceObject,
    PropertySlotHandle sourceSlot,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    BindingMode mode,
    ValueConverterKind converter,
    std::string_view converterKey,
    std::string_view converterParameter,
    RuntimeError* error) {
    if (!engine_ || !sourceSlot.valid() || !targetElement) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "invalid source slot or null target UIElement";
        }
        return false;
    }

    auto* heap = engine_->Domains().heapForObject(sourceObject);
    if (!heap || !heap->isAlive(sourceObject)) {
        if (error) {
            error->code = ErrorCode::InvalidObjectReference;
            error->message = "observable source object is not alive in SimHeap or UiHeap";
        }
        return false;
    }

    AxbCompiledHop hop{};
    hop.declaringTypeId = sourceSlot.typeId;
    hop.fieldIndex = sourceSlot.backingFieldIndex;
    hop.observableBitIndex = sourceSlot.observableBitIndex;
    hop.getterSymbolId = sourceSlot.getterSymbolId;
    hop.setterSymbolId = sourceSlot.setterSymbolId;
    hop.isAutoProperty = sourceSlot.isAutoProperty;
    hop.isObservable = sourceSlot.isObservable;
    hop.isReadOnly = sourceSlot.isReadOnly;
    hop.isSimState = sourceSlot.isSimState;
    hop.isSimCommand = sourceSlot.isSimCommand;

    AxbCompiledBindingChain chain{};
    chain.targetPropertyId = targetPropertyId;
    chain.mode = mode;
    chain.converter = converter;
    chain.converterKey = std::string(converterKey);
    chain.converterParameter = std::string(converterParameter);
    chain.hops.push_back(std::move(hop));

    RuntimeError localErr{};
    RuntimeError* errOut = error ? error : &localErr;
    errOut->code = ErrorCode::None;
    errOut->message.clear();

    const std::uint16_t slotId = engine_->BindChain(sourceObject, chain, targetElement, errOut);
    if (slotId == 0u || errOut->code != ErrorCode::None) {
        if (slotId != 0u) {
            (void)engine_->UnbindSlot(slotId);
        }
        return false;
    }
    return true;
}

bool ScriptUiBridge::BindPath(
    ObjectRef rootObject,
    std::string_view propertyPath,
    const IntrusivePtr<UIElement>& targetElement,
    std::uint16_t targetPropertyId,
    BindingMode mode,
    RuntimeError* error) {
    if (!engine_) {
        if (error) {
            error->code = ErrorCode::InvalidArguments;
            error->message = "ScriptUiBridge has no underlying DualTrackBindingEngine";
        }
        return false;
    }
    RuntimeError localErr{};
    RuntimeError* errOut = error ? error : &localErr;
    errOut->code = ErrorCode::None;
    errOut->message.clear();
    const std::uint16_t slotId = engine_->BindPath(
        rootObject,
        propertyPath,
        targetElement,
        targetPropertyId,
        mode,
        errOut);
    if (slotId == 0u || errOut->code != ErrorCode::None) {
        if (slotId != 0u) {
            (void)engine_->UnbindSlot(slotId);
        }
        return false;
    }
    return true;
}

bool ScriptUiBridge::PushTwoWayToSource(
    const UIElement* targetElement,
    std::uint16_t targetPropertyId,
    RuntimeError* error) {
    if (!engine_) {
        return false;
    }
    return engine_->PushTwoWayToSource(targetElement, targetPropertyId, error);
}

std::size_t ScriptUiBridge::FlushReactiveBindings(RuntimeError* error) {
    if (!engine_) {
        return 0u;
    }
    std::shared_ptr<UiResourceContext> domainCtx =
        !UiResourceContext::HasActiveThreadOverride()
            ? UiResourceContext::FindForDomains(&domains_)
            : nullptr;
    ScopedUiResourceContext ctxGuard(std::move(domainCtx));
    return engine_->FlushTrackA(error) + engine_->FlushTrackB();
}

std::size_t ScriptUiBridge::BindingCount() const noexcept {
    if (!engine_) {
        return 0u;
    }
    return engine_->ReactiveBindings().size() +
           engine_->ElementBindings().size() +
           engine_->MultiBindings().size();
}

std::vector<ReactivePropertyBinding> ScriptUiBridge::Bindings() const {
    std::vector<ReactivePropertyBinding> out;
    if (!engine_) {
        return out;
    }
    const auto& reactive = engine_->ReactiveBindings();
    out.reserve(reactive.size());
    for (const auto& inst : reactive) {
        ReactivePropertyBinding item{};
        item.sourceObject = (!inst.resolvedHopObjects.empty() && inst.resolvedHopObjects.back().valid())
            ? inst.resolvedHopObjects.back()
            : inst.rootObject;
        if (!inst.hops.empty()) {
            item.sourceSlot = inst.hops.back().ToSlotHandle();
        }
        item.targetElement = inst.targetElement;
        item.targetPropertyId = inst.targetPropertyId;
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace aero
