#include "aero/controls/AxbTemplateMounter.hpp"

#include "ControlsInternal.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"
#include "realscript/bytecode/Bytecode.h"

namespace aero {
namespace {

struct TemplateHooksStaticRegistrar {
    TemplateHooksStaticRegistrar() noexcept {
        RegisterUIElementTemplateHooks(&CreateElementByTypeName, &MountControlTemplatePackage);
        RegisterUIElementInteractionHooks(&MountAxbTemplate, &TryExecuteRoutedCommandBubble);
    }
};

const TemplateHooksStaticRegistrar gTemplateHooksStaticRegistrar{};

struct RegisteredCustomControlEntry {
    IntrusiveCustomControlFactory intrusiveFactory = nullptr;
    bool isTemplatedControl = false;
};

struct CustomControlRegistryState {
    std::mutex mutex;
    std::unordered_map<std::string, RegisteredCustomControlEntry> byExactName;
    std::unordered_map<std::string, RegisteredCustomControlEntry> byShortName;
};

CustomControlRegistryState& GetCustomControlRegistryState() noexcept {
    static CustomControlRegistryState sState;
    return sState;
}

void StoreCustomControlEntry(
    std::string_view typeName,
    RegisteredCustomControlEntry entry) {
    const std::string_view cleanType = str::Trim(typeName);
    if (cleanType.empty()) {
        return;
    }
    const std::string_view shortType = str::ShortTypeName(cleanType);
    auto& state = GetCustomControlRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.byExactName[std::string(cleanType)] = entry;
    if (!shortType.empty()) {
        state.byShortName[std::string(shortType)] = entry;
    }
}

[[nodiscard]] IntrusivePtr<UIElement> CreateCustomControlByTypeName(
    std::string_view typeName,
    std::string_view shortName) {
    RegisteredCustomControlEntry entry{};
    {
        auto& state = GetCustomControlRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::string_view cleanType = str::Trim(typeName);
        if (!cleanType.empty()) {
            if (const auto it = state.byExactName.find(std::string(cleanType));
                it != state.byExactName.end()) {
                entry = it->second;
            }
        }
        if (!entry.intrusiveFactory && !shortName.empty()) {
            if (const auto it = state.byShortName.find(std::string(shortName));
                it != state.byShortName.end()) {
                entry = it->second;
            }
        }
    }
    if (entry.intrusiveFactory) {
        return entry.intrusiveFactory();
    }
    return nullptr;
}

[[nodiscard]] bool IsClickableButtonOrMenu(const UIElement* elem) noexcept {
    return elem != nullptr && (elem->IsA<ButtonBase>() || elem->IsA<MenuItem>());
}

void SetElementClickCallback(UIElement* elem, std::function<void(UIElement&)> cb) {
    if (!elem || !cb) {
        return;
    }
    if (auto* bb = elem->DynamicCast<ButtonBase>()) {
        bb->SetClickCallback([fn = std::move(cb)](ButtonBase& b) { fn(b); });
    } else if (auto* mi = elem->DynamicCast<MenuItem>()) {
        mi->SetClickCallback([fn = std::move(cb)](MenuItem& m) { fn(m); });
    }
}

} // namespace

namespace detail {

IntrusivePtr<UIElement> CreateElementByTypeNameCore(
    std::string_view typeName,
    std::string_view shortName) {
    if (shortName == "ToolBarPanel") {
        auto p = MakeIntrusive<StackPanel>();
        (void)p->SetValue(PropId_Orientation, value::FromInt(1), ValueSourceRank::Default);
        return p;
    }
    if (!shortName.empty()) {
        if (const TypeClass* tc = TypeRegistry::Instance().FindByName(shortName);
            tc && tc->createInstance && tc->IsDerivedFrom(UIElement::StaticTypeClass())) {
            IntrusivePtr<AeroObject> instance = tc->createInstance();
            if (auto* elem = instance ? instance->DynamicCast<UIElement>() : nullptr) {
                return IntrusivePtr<UIElement>(elem, true);
            }
        }
    }
    if (IntrusivePtr<UIElement> customCtrl = CreateCustomControlByTypeName(typeName, shortName)) {
        return customCtrl;
    }
    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr) {
        return MakeIntrusive<UserControl>(typeName);
    }
    return MakeIntrusive<UIElement>();
}

const AxbStyleDefinition* LookupStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept {
    if (key.empty()) {
        return nullptr;
    }
    for (auto it = package.styles.rbegin(); it != package.styles.rend(); ++it) {
        if (it->key == key) {
            return &(*it);
        }
    }
    return AxbTemplateRegistry::Instance().FindStyle(key);
}

const AxbStyleDefinition* LookupImplicitStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view shortTypeName) noexcept {
    if (shortTypeName.empty()) {
        return nullptr;
    }
    for (auto it = package.styles.rbegin(); it != package.styles.rend(); ++it) {
        if (it->key.empty() && str::ShortTypeName(it->targetTypeName) == shortTypeName) {
            return &(*it);
        }
    }
    return AxbTemplateRegistry::Instance().FindImplicitStyle(shortTypeName);
}

const AxbControlTemplate* LookupControlTemplateInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept {
    if (const AxbControlTemplate* local = package.FindControlTemplate(key)) {
        return local;
    }
    return AxbTemplateRegistry::Instance().FindControlTemplate(key);
}

void AutoApplyControlStyleTemplate(
    UIElement* elem,
    std::string_view declaredTypeName,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    if (!elem) {
        return;
    }

    // 1. Explicit PropId_Template already set on element
    const std::string explicitTplKey = elem->GetControlTemplateKey();
    if (!explicitTplKey.empty() && !elem->HasControlTemplate()) {
        if (const AxbControlTemplate* ct = LookupControlTemplateInPackageOrRegistry(package, explicitTplKey)) {
            (void)elem->ApplyControlTemplate(
                *ct, domains, bindingEngine, dataContext, eventInterpreter, eventAotContext, animationEngine);
        }
    }

    // 2. Explicit Style="{StaticResource ...}" or implicit <Style TargetType="..."> carrying a ControlTemplate
    const std::string_view shortType = !declaredTypeName.empty()
        ? str::ShortTypeName(declaredTypeName)
        : (elem->GetTypeClass()->typeName
               ? str::ShortTypeName(elem->GetTypeClass()->typeName)
               : std::string_view{});

    const std::string explicitStyleKey = elem->GetStyleKey();
    const AxbStyleDefinition* leafStyle = !explicitStyleKey.empty()
        ? LookupStyleInPackageOrRegistry(package, explicitStyleKey)
        : LookupImplicitStyleInPackageOrRegistry(package, shortType);

    if (leafStyle) {
        // Walk BasedOn inheritance chain (root-to-leaf) to see if this Style defines a ControlTemplate
        std::vector<const AxbStyleDefinition*> chain;
        chain.push_back(leafStyle);
        std::string curBaseKey = leafStyle->basedOnKey;
        for (int depth = 0; depth < 8 && !curBaseKey.empty(); ++depth) {
            const AxbStyleDefinition* base = LookupStyleInPackageOrRegistry(package, curBaseKey);
            if (!base || std::find(chain.begin(), chain.end(), base) != chain.end()) {
                break;
            }
            chain.push_back(base);
            curBaseKey = base->basedOnKey;
        }

        std::string resolvedTplKey;
        std::shared_ptr<AxbControlTemplate> resolvedInlineCt;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const AxbStyleDefinition* s = *it;
            if (!s->controlTemplateKey.empty()) {
                resolvedTplKey = s->controlTemplateKey;
            }
            if (s->inlineControlTemplate) {
                resolvedInlineCt = s->inlineControlTemplate;
            }
            for (const auto& setter : s->setters) {
                if (setter.propId == PropId_Template && setter.targetName.empty()) {
                    if (const auto* sv = std::get_if<StringValue>(&setter.value)) {
                        resolvedTplKey = sv->str();
                    }
                }
            }
        }

        if (!resolvedTplKey.empty() || resolvedInlineCt) {
            if (!elem->HasControlTemplate()) {
                const AxbControlTemplate* ctToMount = !resolvedTplKey.empty()
                    ? LookupControlTemplateInPackageOrRegistry(package, resolvedTplKey)
                    : nullptr;
                if (!ctToMount && resolvedInlineCt) {
                    ctToMount = resolvedInlineCt.get();
                }
                if (ctToMount) {
                    (void)elem->ApplyControlTemplate(
                        *ctToMount,
                        domains,
                        bindingEngine,
                        dataContext,
                        eventInterpreter,
                        eventAotContext,
                        animationEngine);
                }
            }

            // Apply base theme setters at ValueSourceRank::ThemeStyle so TemplateBindings & ContentPresenters receive defaults
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                for (const auto& setter : (*it)->setters) {
                    if (setter.propId == PropId_Template || setter.hasBinding) {
                        continue;
                    }
                    UIElement* target = setter.targetName.empty()
                        ? elem
                        : elem->FindName(setter.targetName);
                    if (!target) {
                        continue;
                    }
                    if (IsFastInlinePropertyId(setter.propId)) {
                        if (target->GetPropertySourceRank(setter.propId) == ValueSourceRank::Default) {
                            (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
                        }
                    } else {
                        (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
                    }
                }
            }
            return;
        }
    }

    // 3. Implicit ControlTemplate by TargetType (if registered without x:Key)
    if (!elem->HasControlTemplate() && !shortType.empty()) {
        if (const AxbControlTemplate* impCt =
                AxbTemplateRegistry::Instance().FindImplicitControlTemplate(shortType)) {
            (void)elem->ApplyControlTemplate(
                *impCt, domains, bindingEngine, dataContext, eventInterpreter, eventAotContext, animationEngine);
        }
    }
}

bool MountAxbInstructionsInternal(
    UIElement* rootControl,
    UIElement* templatedHost,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine,
    bool structureOnly,
    bool bindingsOnly,
    std::vector<IntrusivePtr<UIElement>>* ioMountedElements) {
    if (!rootControl) {
        return false;
    }
    const std::shared_ptr<UIResourceContext> domainCtx =
        (!UIResourceContext::HasActiveThreadOverride() && domains != nullptr)
            ? UIResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUIResourceContext domainScope(domainCtx);

    if (!animationEngine && bindingEngine && !structureOnly) {
        animationEngine = bindingEngine->GetAnimationEngine();
    }

    if (!bindingsOnly) {
        if (!package.resources.empty()) {
            for (const auto& kv : package.resources) {
                if (!kv.first.empty()) {
                    AxbTemplateRegistry::Instance().RegisterResource(kv.first, kv.second);
                }
            }
        }
        if (!package.mtsdfPaths.empty()) {
            for (const auto& mp : package.mtsdfPaths) {
                AxbTemplateRegistry::Instance().RegisterMtsdfPath(mp);
            }
        }
        for (const auto& ct : package.controlTemplates) {
            AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);
        }
        for (const auto& st : package.styles) {
            AxbTemplateRegistry::Instance().RegisterStyle(st);
        }
        for (const auto& sb : package.storyboards) {
            AxbTemplateRegistry::Instance().RegisterStoryboard(sb);
        }
        for (const auto& dt : package.dataTemplates) {
            AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
        }
    }

    NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;
    const ObjectRef effectiveContext = dataContext.valid()
        ? dataContext
        : (templatedHost && templatedHost->BoundScriptWrapper().valid()
               ? templatedHost->BoundScriptWrapper()
               : rootControl->BoundScriptWrapper());

    if (!bindingsOnly) {
        for (const auto& ct : package.controlTemplates) {
            if (!ct.key.empty() && ct.templatePackage) {
                AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);
            }
        }
        for (const auto& dt : package.dataTemplates) {
            if ((!dt.key.empty() || !dt.dataTypeName.empty()) && dt.templatePackage) {
                AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
            }
        }
    }
    std::vector<UIElement*> elementStack;
    std::vector<UIElement*> localElements;
    std::vector<std::string_view> localDeclaredTypes;
    std::vector<std::function<void()>> deferredLoadedHandlers;
    bool rootVisited = false;
    std::size_t mountedElemCursor = 0u;
    if (structureOnly && ioMountedElements) {
        ioMountedElements->clear();
    }

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                UIElement* currentElem = nullptr;

                if (bindingsOnly && ioMountedElements) {
                    if (!rootVisited) {
                        rootVisited = true;
                        currentElem = rootControl;
                        if (mountedElemCursor < ioMountedElements->size()) {
                            ++mountedElemCursor;
                        }
                        if (!templatedHost) {
                            const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                                ? rootControl->BoundScriptWrapper()
                                : effectiveContext;
                            if (domains && registry && uiHeap && wrapperObj.valid() &&
                                uiHeap->isAlive(wrapperObj) &&
                                package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex &&
                                (package.fieldsOwnerTypeId == 0 ||
                                 uiHeap->objectTypeIdFast(wrapperObj) == package.fieldsOwnerTypeId)) {
                                const auto rootH = rootControl->ToNativeHandle(*registry);
                                (void)domains->writeObjectField(
                                    DomainKind::Ui,
                                    wrapperObj,
                                    package.rootHandleFieldIndex,
                                    rootH,
                                    nullptr);
                            }
                        }
                    } else if (mountedElemCursor < ioMountedElements->size()) {
                        currentElem = (*ioMountedElements)[mountedElemCursor++].Get();
                    }
                } else if (!rootVisited) {
                    rootVisited = true;
                    currentElem = rootControl;
                    if (templatedHost) {
                        currentElem->SetTemplatedParent(templatedHost);
                    } else {
                        const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                            ? rootControl->BoundScriptWrapper()
                            : effectiveContext;
                        if (domains && registry && uiHeap && wrapperObj.valid() &&
                            uiHeap->isAlive(wrapperObj) &&
                            package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex &&
                            (package.fieldsOwnerTypeId == 0 ||
                             uiHeap->objectTypeIdFast(wrapperObj) == package.fieldsOwnerTypeId)) {
                            const auto rootH = rootControl->ToNativeHandle(*registry);
                            (void)domains->writeObjectField(
                                DomainKind::Ui,
                                wrapperObj,
                                package.rootHandleFieldIndex,
                                rootH,
                               nullptr);
                        }
                    }
                } else {
                    IntrusivePtr<UIElement> createdChild;
                    bool isCustomTemplated = false;
                    const bool isRegisteredCustom = IsCustomControlRegistered(typeName, &isCustomTemplated);
                    const AxbPackage* subControlPkg = AxbTemplateRegistry::Instance().Find(typeName);
                    if (!isRegisteredCustom &&
                        subControlPkg != nullptr &&
                        str::ShortTypeName(typeName) != "UserControl" &&
                        str::ShortTypeName(typeName) != "Page") {
                        createdChild = MakeIntrusive<UserControl>(
                            typeName,
                            domains,
                            bindingEngine,
                            effectiveContext,
                            eventInterpreter,
                            eventAotContext,
                            animationEngine);
                    } else {
                        createdChild = CreateElementByTypeName(typeName);
                        if (isRegisteredCustom && !isCustomTemplated && subControlPkg != nullptr && createdChild) {
                            (void)MountAxbTemplate(
                                createdChild.Get(),
                                *subControlPkg,
                                domains,
                                bindingEngine,
                                effectiveContext,
                                eventInterpreter,
                                eventAotContext,
                                animationEngine);
                        }
                    }
                    if (!elementStack.empty() && elementStack.back() && createdChild) {
                        if (templatedHost) {
                            createdChild->SetTemplatedParent(templatedHost);
                        }
                        elementStack.back()->AddChild(createdChild.Get(), registry, uiHeap);
                        currentElem = createdChild.Get();
                        if ((inst.flags & 0x01u) != 0u) {
                            if (auto* hcc = elementStack.back()->DynamicCast<HeaderedContentControl>()) {
                                hcc->SetHeaderElement(currentElem);
                            }
                        }
                    }
                }

                if (structureOnly && ioMountedElements) {
                    ioMountedElements->push_back(IntrusivePtr<UIElement>(currentElem, true));
                }

                if (currentElem && bindingEngine) {
                    currentElem->BindTwoWayEngine(bindingEngine);
                }
                if (currentElem && animationEngine) {
                    currentElem->BindAnimationEngine(animationEngine);
                }

                if (auto* ic = currentElem ? currentElem->DynamicCast<ItemsControl>() : nullptr) {
                    ic->SetDomains(domains);
                    ic->SetBindingEngine(bindingEngine);
                    if (ic->IsA<TreeView>()) { ic->BindTwoWayEngine(bindingEngine); }
                    ic->SetAnimationEngine(animationEngine);
                } else if (auto* tc = currentElem ? currentElem->DynamicCast<TabControl>() : nullptr) {
                    tc->SetDomains(domains);
                    tc->SetBindingEngine(bindingEngine);
                    tc->SetAnimationEngine(animationEngine);
                }
                if (auto* rc = currentElem ? currentElem->DynamicCast<RiveControl>() : nullptr) {
                    rc->SetAnimationEngine(animationEngine);
                }
                if (!bindingsOnly && templatedHost && currentElem) {
                    if (auto* cp = currentElem->DynamicCast<ContentPresenter>()) {
                        templatedHost->RegisterTemplateContentPresenter(cp);
                    }
                }

                if (currentElem && !elemName.empty()) {
                    currentElem->SetName(std::string(elemName));
                    if (!bindingsOnly && templatedHost) {
                        templatedHost->RegisterTemplateNamedPart(elemName, currentElem);
                    } else if (!templatedHost) {
                        const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                            ? rootControl->BoundScriptWrapper()
                            : effectiveContext;
                        if (domains && registry && uiHeap && wrapperObj.valid() &&
                            uiHeap->isAlive(wrapperObj) &&
                            (package.fieldsOwnerTypeId == 0 ||
                             uiHeap->objectTypeIdFast(wrapperObj) == package.fieldsOwnerTypeId)) {
                            for (const auto& namedSlot : package.namedControls) {
                                if (namedSlot.name == elemName &&
                                    namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                                    const auto h = currentElem->ToNativeHandle(*registry);
                                    (void)domains->writeObjectField(
                                        DomainKind::Ui,
                                        wrapperObj,
                                        namedSlot.fieldIndexInClass,
                                        h,
                                        nullptr);
                                    break;
                                }
                            }
                        }
                    }
                }

                elementStack.push_back(currentElem);
                localElements.push_back(currentElem);
                localDeclaredTypes.push_back(typeName);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elementStack.empty()) {
                    UIElement* endedElem = elementStack.back();
                    elementStack.pop_back();
                    if (!structureOnly && endedElem) {
                        if (auto* tvi = endedElem->DynamicCast<TreeViewItem>()) {
                            if (tvi->IsSelected()) {
                                if (TreeView* tv = tvi->FindAncestorTreeView()) {
                                    tv->SelectContainer(tvi);
                                }
                            }
                        } else if (auto* lb = endedElem->DynamicCast<ListBox>()) {
                            if (lb->GetSelectedIndex() < 0) {
                                std::int32_t cIdx = 0;
                                for (UIElement* ch = lb->FirstChild(); ch != nullptr; ch = ch->NextSibling(), ++cIdx) {
                                    const UnifiedValue selV = ch->GetValue(PropId_IsChecked);
                                    if (const auto* b = std::get_if<bool>(&selV); b && *b) {
                                        lb->SetSelectedIndex(cIdx);
                                        break;
                                    }
                                }
                            }
                        } else if (auto* cb = endedElem->DynamicCast<ComboBox>()) {
                            if (cb->GetSelectedIndex() >= 0) {
                                cb->OnPropertyChanged(PropId_SelectedIndex, value::FromInt(cb->GetSelectedIndex()));
                            } else {
                                std::int32_t cIdx = 0;
                                for (UIElement* ch = cb->FirstChild(); ch != nullptr; ch = ch->NextSibling(), ++cIdx) {
                                    const UnifiedValue selV = ch->GetValue(PropId_IsChecked);
                                    if (const auto* b = std::get_if<bool>(&selV); b && *b) {
                                        cb->SetSelectedIndex(cIdx);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!bindingsOnly && !elementStack.empty() && elementStack.back()) {
                    if (inst.propId == PropId_KeyBindingsSpec || inst.propId == PropId_CommandBindingsSpec) {
                        const UnifiedValue prevKb = elementStack.back()->GetValue(inst.propId);
                        const auto* prevSv = std::get_if<StringValue>(&prevKb);
                        const auto* newSv = std::get_if<StringValue>(&inst.literalValue);
                        if (prevSv && !prevSv->view().empty() && newSv && !newSv->view().empty()) {
                            std::string merged(prevSv->view());
                            merged.push_back('\n');
                            merged.append(newSv->view());
                            elementStack.back()->SetValue(
                                inst.propId,
                                value::FromString(merged),
                                ValueSourceRank::Local);
                            break;
                        }
                    }
                    if (inst.propId == PropId_RoutedCommandName && bindingEngine) {
                        if (UIElement* btnPtr = elementStack.back(); IsClickableButtonOrMenu(btnPtr)) {
                            if (const auto* cmdSv = std::get_if<StringValue>(&inst.literalValue);
                                cmdSv && !cmdSv->view().empty()) {
                                const std::string rcmdCopy = cmdSv->str();
                                bindingEngine->RegisterCommandCanExecuteWatcher(
                                    btnPtr,
                                    [btnPtr, rcmdCopy]() {
                                        if (!btnPtr) return;
                                        const UnifiedValue cp = btnPtr->GetValue(PropId_CommandParameter);
                                        const bool hasCp = !std::holds_alternative<std::monostate>(cp);
                                        bool handled = false;
                                        const bool ok = QueryRoutedCommandCanExecuteBubble(
                                            btnPtr, rcmdCopy, cp, hasCp, handled);
                                        if (handled && btnPtr->IsEnabled() != ok) {
                                            btnPtr->SetIsEnabled(ok);
                                        }
                                    });
                            }
                        }
                    }
                    std::uint16_t effectivePropId = inst.propId;
                    UnifiedValue effectiveValue = inst.literalValue;
                    if (IsCustomPropertyId(effectivePropId) &&
                        !elementStack.back()->DeclaredTypeName().empty()) {
                        const std::string_view customName = GetCustomPropertyName(effectivePropId);
                        if (!customName.empty()) {
                            const std::uint16_t ownerPropId =
                                FindCustomProperty(elementStack.back()->DeclaredTypeName(), customName);
                            if (ownerPropId != 0xFFFFu && ownerPropId != effectivePropId) {
                                effectivePropId = ownerPropId;
                                if (const auto* rawSv = std::get_if<StringValue>(&inst.literalValue)) {
                                    effectiveValue = ParseBuiltinLiteralPropertyValue(
                                        effectivePropId, rawSv->view());
                                }
                            }
                        }
                    }
                    elementStack.back()->SetValue(
                        effectivePropId,
                        effectiveValue,
                        ValueSourceRank::Local);
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!structureOnly && !elementStack.empty() && elementStack.back() && bindingEngine &&
                    inst.strIdx0 < package.bindingChains.size()) {
                    const auto& chain = package.bindingChains[inst.strIdx0];
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    if (!chain.elementName.empty() ||
                        !chain.relativeAncestorType.empty() ||
                        chain.relativeSelf) {
                        (void)bindingEngine->BindElementProperty(chain, targetPtr);
                    } else if (!effectiveContext.valid() &&
                               std::holds_alternative<NativeHandle>(elementStack.back()->GetValue(PropId_DataContext))) {
                        const auto nh = std::get<NativeHandle>(elementStack.back()->GetValue(PropId_DataContext));
                        const UIElement* nhEl = domains ? UIElement::FromNativeHandle(domains->nativeHandles(), nh) : nullptr;
                        if (!nhEl) nhEl = UIElement::FromNativeHandle(FallbackVisualHandleRegistry(), nh);
                        if (nhEl) {
                            const std::string_view nhMember = !chain.hops.empty()
                                ? std::string_view(chain.hops.front().memberName)
                                : std::string_view(chain.rawPath);
                            const std::uint16_t spid = (nhMember == "Color")
                                ? static_cast<std::uint16_t>(PropId_EffectColor)
                                : ResolveTriggerConditionPropertyId(nhMember);
                            UnifiedValue v = nhEl->GetValue(spid);
                            if (spid == PropId_EffectColor &&
                                (inst.propId == PropId_Text || inst.propId == PropId_Content || inst.propId == PropId_Header)) {
                                if (const auto col = value::TryGetColor(v)) {
                                    v = value::FromString(FormatColorHexArgb(*col));
                                }
                            }
                            if (chain.converter != ValueConverterKind::None && bindingEngine) {
                                v = bindingEngine->ApplySingleValueConversion(
                                    chain.converter, chain.converterKey, chain.converterParameter,
                                    chain.converterSymbolId, chain.convertBackSymbolId, v, false);
                            }
                            if (!chain.stringFormat.empty() && !std::holds_alternative<std::monostate>(v)) {
                                v = value::FromString(FormatBindingString(chain.stringFormat, &v, 1u, domains));
                            } else if ((inst.propId == PropId_Text || inst.propId == PropId_Header) &&
                                       !std::holds_alternative<StringValue>(v) &&
                                       !std::holds_alternative<std::monostate>(v)) {
                                v = value::FromString(FormatBindingString({}, &v, 1u, domains));
                            }
                            if (!std::holds_alternative<std::monostate>(v)) {
                                targetPtr->SetValue(inst.propId, v, ValueSourceRank::Local);
                            }
                        }
                    } else if (effectiveContext.valid()) {
                        if (!chain.hops.empty()) {
                            (void)bindingEngine->BindChain(
                                effectiveContext,
                                chain,
                                targetPtr);
                        } else if (!chain.rawPath.empty()) {
                            (void)bindingEngine->BindPath(
                                effectiveContext,
                                chain.rawPath,
                                targetPtr,
                                inst.propId,
                                chain.mode);
                        }
                    }
                }
                break;
            }

            case AxbOpcode::BindMultiProperty: {
                if (!structureOnly && !elementStack.empty() && elementStack.back() && bindingEngine &&
                    inst.strIdx0 < package.multiBindings.size()) {
                    const auto& mb = package.multiBindings[inst.strIdx0];
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    (void)bindingEngine->BindMultiChain(
                        effectiveContext,
                        mb,
                        targetPtr);
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!structureOnly && !elementStack.empty() && elementStack.back() && bindingEngine) {
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    auto resolveCanonicalTypeId = [](std::string_view raw) -> SymbolId {
                        std::string canonical(raw);
                        if (canonical.find("::") == std::string::npos) {
                            if (const std::size_t lastDot = canonical.rfind('.'); lastDot != std::string::npos) {
                                canonical.replace(lastDot, 1, "::");
                            }
                        }
                        return rs_sem::stableTypeId(canonical);
                    };
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : resolveCanonicalTypeId(compName);
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    (void)bindingEngine->BindEcsColumn(
                        compId,
                        fieldName,
                        0,
                        targetPtr,
                        inst.propId);
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!structureOnly && !elementStack.empty() && elementStack.back()) {
                    UIElement* elem = elementStack.back();
                    const AxbEventBindingRecord* evRec =
                        (inst.strIdx0 < package.eventBindings.size())
                            ? &package.eventBindings[inst.strIdx0]
                            : nullptr;

                    auto findRootWrapper = [&](const UIElement* startEl) -> ObjectRef {
                        for (const UIElement* cur = startEl;
                             cur != nullptr;
                             cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
                            if (cur->BoundScriptWrapper().valid()) {
                                return cur->BoundScriptWrapper();
                            }
                        }
                        return ObjectRef{};
                    };

                    const ObjectRef rootWrapObj = findRootWrapper(rootControl);
                    ObjectRef targetObj{};
                    if (evRec && evRec->targetIsWrapper && rootWrapObj.valid()) {
                        targetObj = rootWrapObj;
                    } else if (evRec && !evRec->targetIsWrapper && effectiveContext.valid()) {
                        targetObj = effectiveContext;
                        if (!evRec->prefixHops.empty() && domains) {
                            for (const auto& phop : evRec->prefixHops) {
                                AxbCompiledHop tmpHop = phop;
                                UnifiedValue nextVal{};
                                bool okHop = false;
                                if (tmpHop.isAutoProperty || tmpHop.getterSymbolId == 0) {
                                    if (tmpHop.fieldIndex != PropertySlotHandle::InvalidIndex) {
                                        okHop = domains->readObjectField(DomainKind::Ui, targetObj, tmpHop.fieldIndex, nextVal);
                                    }
                                } else if (eventInterpreter || domains->hasDesktopJitInvoker()) {
                                    const UnifiedValue gArg0{targetObj};
                                    const auto gRes = domains->invokeMethod(
                                        DomainKind::UiPresentation, eventInterpreter, tmpHop.getterSymbolId, &gArg0, 1u);
                                    if (gRes.succeeded) {
                                        nextVal = gRes.value;
                                        okHop = true;
                                    }
                                }
                                const auto* nextRef = okHop ? std::get_if<ObjectRef>(&nextVal) : nullptr;
                                if (!nextRef || !nextRef->valid()) {
                                    targetObj = ObjectRef{};
                                    break;
                                }
                                targetObj = *nextRef;
                            }
                        }
                    } else {
                        targetObj = rootWrapObj.valid() ? rootWrapObj : effectiveContext;
                    }

                    const SymbolId methodId = (evRec && evRec->handlerSymbolId != 0)
                        ? evRec->handlerSymbolId
                        : inst.symbolId;
                    bool isSimCommand = ((inst.flags & 0x01u) != 0u);
                    if (!isSimCommand && methodId != 0 && domains &&
                        domains->isSimCommandFunction(methodId)) {
                        isSimCommand = true;
                    }
                    if ((!targetObj.valid() || methodId == 0) &&
                        evRec != nullptr &&
                        rootControl != nullptr) {
                        const std::string& hName = evRec->handlerMethodName;
                        if ((hName == "UpButton_Click" || hName == "DownButton_Click") &&
                            IsClickableButtonOrMenu(elem)) {
                            const float sign = (hName == "UpButton_Click") ? 1.0f : -1.0f;
                            SetElementClickCallback(elem, [sign](UIElement& btn) {
                                UIElement* hostCtrl = nullptr;
                                for (UIElement* anc = btn.Parent(); anc != nullptr; anc = anc->Parent()) {
                                    if (anc->IsA<UserControl>()) {
                                        hostCtrl = anc;
                                        break;
                                    }
                                }
                                if (hostCtrl == nullptr) {
                                    for (UIElement* anc = btn.Parent(); anc != nullptr; anc = anc->Parent()) {
                                        hostCtrl = anc;
                                    }
                                }
                                if (hostCtrl == nullptr) {
                                    return;
                                }
                                float curVal = 0.0f;
                                (void)TryGetFloat(hostCtrl->GetValue(PropId_Value), curVal);
                                float minVal = -1e9f;
                                (void)TryGetFloat(hostCtrl->GetValue(PropId_Minimum), minVal);
                                float maxVal = 1e9f;
                                (void)TryGetFloat(hostCtrl->GetValue(PropId_Maximum), maxVal);
                                float step = 1.0f;
                                if (!TryGetFloat(hostCtrl->GetValue(PropId_SmallChange), step) || step <= 0.0f) {
                                    step = 1.0f;
                                }
                                const float nextVal = std::clamp(curVal + sign * step, minVal, maxVal);
                                if (std::fabs(nextVal - curVal) > 1e-5f) {
                                    hostCtrl->SetValue(PropId_Value, MakeFloat(nextVal), ValueSourceRank::Local);
                                    if (hostCtrl->BoundTwoWayEngine() != nullptr) {
                                        (void)hostCtrl->NotifyTwoWayPropertyMutated(PropId_Value, nullptr);
                                    }
                                }
                            });
                        } else if (IsClickableButtonOrMenu(elem) && !hName.empty() && bindingEngine && domains) {
                            const std::string handlerCopy = hName;
                            SetElementClickCallback(
                                elem,
                                [domains, bindingEngine, eventInterpreter, eventAotContext, handlerCopy](UIElement& b) {
                                    const ProgramImage* prog = bindingEngine->Program();
                                    if (!prog) return;

                                    auto findMethodOn = [&](ObjectRef obj, std::string_view rawMethod, SymbolId& outSym, bool& outSim) -> bool {
                                        outSym = 0;
                                        outSim = false;
                                        if (!obj.valid()) return false;
                                        auto* heap = domains->heapForObject(obj);
                                        if (!heap || !heap->isAlive(obj)) return false;
                                        SymbolId curTid = heap->objectTypeId(obj).value_or(0);
                                        std::string cands[3];
                                        std::size_t nCands = 0u;
                                        cands[nCands++] = std::string(rawMethod);
                                        if (rawMethod.size() > 7u && rawMethod.substr(rawMethod.size() - 7u) == "Command") {
                                            cands[nCands++] = std::string(rawMethod.substr(0u, rawMethod.size() - 7u));
                                            cands[nCands++] = "On" + cands[1];
                                        } else {
                                            cands[nCands++] = std::string(rawMethod) + "Command";
                                            cands[nCands++] = "On" + std::string(rawMethod);
                                        }
                                        for (std::size_t ci = 0u; ci < nCands; ++ci) {
                                            SymbolId walkTid = curTid;
                                            for (int d = 0; d < 16 && walkTid != 0; ++d) {
                                                const auto* ts = prog->resolveType(walkTid);
                                                if (!ts) break;
                                                for (const auto& m : ts->methods) {
                                                    if (m.name == cands[ci] && !m.staticMethod && m.id != 0) {
                                                        outSym = m.id;
                                                        outSim = domains->isSimCommandFunction(m.id);
                                                        return true;
                                                    }
                                                }
                                                walkTid = ts->baseTypeId;
                                            }
                                        }
                                        return false;
                                    };

                                    ObjectRef wrapTarget{};
                                    ObjectRef dcTarget{};
                                    for (const UIElement* cur = &b;
                                         cur != nullptr;
                                         cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
                                        if (!dcTarget.valid()) {
                                            const UnifiedValue dcV = cur->GetValue(PropId_DataContext);
                                            if (const auto* r = std::get_if<ObjectRef>(&dcV); r && r->valid()) {
                                                dcTarget = *r;
                                            }
                                        }
                                        if (!wrapTarget.valid() && cur->BoundScriptWrapper().valid()) {
                                            wrapTarget = cur->BoundScriptWrapper();
                                        }
                                    }

                                    std::string leaf = handlerCopy;
                                    if (const auto dot = leaf.rfind('.'); dot != std::string::npos) {
                                        std::size_t pos = 0u;
                                        while (pos < dot && dcTarget.valid()) {
                                            const std::size_t nextDot = handlerCopy.find('.', pos);
                                            const std::size_t endSeg = (nextDot == std::string::npos || nextDot > dot) ? dot : nextDot;
                                            const std::string seg = handlerCopy.substr(pos, endSeg - pos);
                                            auto* h = domains->heapForObject(dcTarget);
                                            const SymbolId tid = (h && h->isAlive(dcTarget)) ? h->objectTypeId(dcTarget).value_or(0) : 0;
                                            if (auto slot = domains->resolvePropertySlot(*prog, tid, seg)) {
                                                UnifiedValue nv{};
                                                if (domains->readPropertySlotFast(DomainKind::Ui, dcTarget, *slot, nv, nullptr, eventInterpreter)) {
                                                    if (const auto* nr = std::get_if<ObjectRef>(&nv); nr && nr->valid()) {
                                                        dcTarget = *nr;
                                                    } else {
                                                        dcTarget = ObjectRef{};
                                                    }
                                                }
                                            }
                                            pos = endSeg + 1u;
                                        }
                                        leaf = handlerCopy.substr(dot + 1u);
                                    }

                                    UnifiedValue rawCmdParam = b.GetValue(PropId_CommandParameter);
                                    const bool hasCmdParam = !std::holds_alternative<std::monostate>(rawCmdParam);
                                    if (TryInvokeCommandObjectOnOwner(
                                            domains, prog, eventInterpreter, eventAotContext,
                                            dcTarget, leaf, rawCmdParam, hasCmdParam)) {
                                        if (DualTrackBindingEngine::IsLiveInstance(bindingEngine)) {
                                            (void)bindingEngine->FlushTrackA();
                                        }
                                        return;
                                    }
                                    if (handlerCopy.find('.') == std::string::npos && wrapTarget.valid() && wrapTarget != dcTarget &&
                                        TryInvokeCommandObjectOnOwner(
                                            domains, prog, eventInterpreter, eventAotContext,
                                            wrapTarget, leaf, rawCmdParam, hasCmdParam)) {
                                        if (DualTrackBindingEngine::IsLiveInstance(bindingEngine)) {
                                            (void)bindingEngine->FlushTrackA();
                                        }
                                        return;
                                    }

                                    ObjectRef callObj{};
                                    SymbolId callSym = 0;
                                    bool callSim = false;
                                    if (handlerCopy.find('.') == std::string::npos &&
                                        findMethodOn(wrapTarget, leaf, callSym, callSim)) {
                                        callObj = wrapTarget;
                                    } else if (findMethodOn(dcTarget, leaf, callSym, callSim)) {
                                        callObj = dcTarget;
                                    }
                                    if (!callObj.valid() || callSym == 0) return;

                                    std::vector<UnifiedValue> args{callObj};
                                    UnifiedValue cmdParam = b.GetValue(PropId_CommandParameter);
                                    if (!std::holds_alternative<std::monostate>(cmdParam)) {
                                        if (const auto* sv = std::get_if<StringValue>(&cmdParam)) {
                                            RuntimeError allocErr{};
                                            if (auto sref = domains->uiHeap().allocateString(std::string(sv->view()), &allocErr)) {
                                                cmdParam = *sref;
                                            }
                                        }
                                        args.push_back(cmdParam);
                                    }
                                    if (callSim) {
                                        std::vector<UnifiedValue> payload;
                                        if (args.size() > 1u) payload.push_back(args[1]);
                                        domains->enqueueSimCommand(SimCommandEnvelope{callSym, callObj, std::move(payload), 0ULL});
                                    } else if (eventInterpreter || domains->hasDesktopJitInvoker()) {
                                        auto res = domains->invokeMethod(
                                            DomainKind::UiPresentation, eventInterpreter, callSym, args);
                                        if (!res.succeeded &&
                                            res.error.code == ErrorCode::InvalidArguments &&
                                            res.statistics.instructionsExecuted == 0u &&
                                            args.size() > 1u) {
                                            args.resize(1u);
                                            (void)domains->invokeMethod(
                                                DomainKind::UiPresentation, eventInterpreter, callSym, args);
                                        }
                                    } else if (eventAotContext) {
                                        UnifiedValue ret{};
                                        if (!eventAotContext->invoke(callSym, args.data(), args.size(), ret) &&
                                            args.size() > 1u) {
                                            (void)eventAotContext->invoke(callSym, args.data(), 1u, ret);
                                        }
                                    }
                                    if (DualTrackBindingEngine::IsLiveInstance(bindingEngine)) {
                                        (void)bindingEngine->FlushTrackA();
                                    }
                                });

                            if (evRec->eventName == "Command") {
                                UIElement* btnPtr = elem;
                                auto evalCanExec = [domains, bindingEngine, eventInterpreter, eventAotContext, handlerCopy, btnPtr]() {
                                    if (!btnPtr || !DualTrackBindingEngine::IsLiveInstance(bindingEngine)) return;
                                    const ProgramImage* prog = bindingEngine->Program();
                                    if (!prog) return;
                                    ObjectRef wrapT{};
                                    ObjectRef dcT{};
                                    for (const UIElement* cur = btnPtr;
                                         cur != nullptr;
                                         cur = cur->Parent() ? cur->Parent() : cur->TemplatedParent()) {
                                        if (!dcT.valid()) {
                                            const UnifiedValue dcV = cur->GetValue(PropId_DataContext);
                                            if (const auto* r = std::get_if<ObjectRef>(&dcV); r && r->valid()) dcT = *r;
                                        }
                                        if (!wrapT.valid() && cur->BoundScriptWrapper().valid()) {
                                            wrapT = cur->BoundScriptWrapper();
                                        }
                                    }
                                    std::string leaf = handlerCopy;
                                    if (const auto dot = leaf.rfind('.'); dot != std::string::npos) {
                                        leaf = leaf.substr(dot + 1u);
                                    }
                                    const UnifiedValue rawCp = btnPtr->GetValue(PropId_CommandParameter);
                                    const bool hasCp = !std::holds_alternative<std::monostate>(rawCp);
                                    bool canExec = true;
                                    if (TryEvaluateCommandCanExecuteOnOwner(
                                            domains, prog, eventInterpreter, eventAotContext,
                                            dcT, leaf, rawCp, hasCp, canExec) ||
                                        (handlerCopy.find('.') == std::string::npos && wrapT.valid() && wrapT != dcT &&
                                         TryEvaluateCommandCanExecuteOnOwner(
                                             domains, prog, eventInterpreter, eventAotContext,
                                             wrapT, leaf, rawCp, hasCp, canExec))) {
                                        if (btnPtr->IsEnabled() != canExec) {
                                            btnPtr->SetIsEnabled(canExec);
                                        }
                                    }
                                };
                                evalCanExec();
                                bindingEngine->RegisterCommandCanExecuteWatcher(btnPtr, std::move(evalCanExec));
                            }
                        }
                    }
                    if (targetObj.valid() && methodId != 0) {
                        const ProgramImage* prog = bindingEngine ? bindingEngine->Program() : nullptr;
                        const std::string evNameCopy = evRec ? evRec->eventName : std::string();
                        IntrusivePtr<UIElement> senderGuard(elem, true);

                        std::int32_t expectedParamCount = -1;
                        const rs_sem::TypeSymbol* cachedSenderSym = nullptr;
                        const rs_sem::TypeSymbol* cachedEventArgsSym = nullptr;
                        if (prog) {
                            if (const auto* loc = prog->resolveFunction(methodId); loc && loc->function) {
                                expectedParamCount = static_cast<std::int32_t>(loc->function->parameterTypes.size());
                            }
                            if (expectedParamCount < 0 || expectedParamCount >= 3) {
                                auto findProgType = [&](std::string_view modWanted, std::string_view nameWanted) -> const rs_sem::TypeSymbol* {
                                    for (const auto& mod : prog->modules()) {
                                        if (!modWanted.empty() && mod.name != modWanted) continue;
                                        for (const auto& ts : mod.types) {
                                            if (ts.name == nameWanted) return &ts;
                                        }
                                    }
                                    return nullptr;
                                };
                                std::string shortTag = senderGuard->DeclaredTypeName();
                                if (shortTag.empty() && senderGuard->GetTypeClass() && senderGuard->GetTypeClass()->typeName) {
                                    std::string_view tn(senderGuard->GetTypeClass()->typeName);
                                    if (const auto dot = tn.rfind('.'); dot != std::string_view::npos) shortTag = std::string(tn.substr(dot + 1u));
                                    else shortTag = std::string(tn);
                                }
                                cachedSenderSym = findProgType("Aero.UI", shortTag);
                                if (!cachedSenderSym) cachedSenderSym = findProgType("Aero.UI", "FrameworkElement");
                                if (const auto* loc = prog->resolveFunction(methodId);
                                    loc && loc->function && loc->function->parameterTypeIds.size() >= 3u &&
                                    loc->function->parameterTypeIds[2] != 0) {
                                    cachedEventArgsSym = prog->resolveType(loc->function->parameterTypeIds[2]);
                                }
                                if (!cachedEventArgsSym) {
                                    if (evNameCopy == "SelectionChanged") cachedEventArgsSym = findProgType("Aero.UI", "SelectionChangedEventArgs");
                                    else if (evNameCopy == "TextChanged" || evNameCopy == "PasswordChanged") cachedEventArgsSym = findProgType("Aero.UI", "TextChangedEventArgs");
                                    else if (evNameCopy == "KeyDown" || evNameCopy == "KeyUp") cachedEventArgsSym = findProgType("Aero.UI", "KeyEventArgs");
                                    else if (evNameCopy == "RequestNavigate") cachedEventArgsSym = findProgType("Aero.UI", "RequestNavigateEventArgs");
                                }
                                if (!cachedEventArgsSym) cachedEventArgsSym = findProgType("Aero.UI", "RoutedEventArgs");
                            }
                        }

                        auto invokeBoundHandler = [eventInterpreter, eventAotContext, domains, prog, bindingEngine, targetObj, methodId, isSimCommand, evNameCopy, senderGuard, expectedParamCount, cachedSenderSym, cachedEventArgsSym](
                                                      const UnifiedValue& argVal,
                                                      bool hasArg) -> bool {
                            UnifiedValue invokedEventArgs{NullObject{}};
                            auto buildWpfSenderAndArgs = [&](UnifiedValue& outSender, UnifiedValue& outEvArgs) {
                                outSender = UnifiedValue(NullObject{});
                                outEvArgs = UnifiedValue(NullObject{});
                                if (!domains || !prog || !senderGuard) return;
                                const rs_sem::TypeSymbol* sSym = cachedSenderSym;
                                if (sSym) {
                                    RuntimeError aErr{};
                                    const NativeHandle nh = senderGuard->ToNativeHandle(domains->nativeHandles());
                                    std::vector<UnifiedValue> sFields(sSym->fields.size(), UnifiedValue(NullObject{}));
                                    std::vector<std::size_t> sRefFields;
                                    for (std::size_t fi = 0u; fi < sSym->fields.size(); ++fi) {
                                        if (sSym->fields[fi].name == "__nativeRootHandle") {
                                            sFields[fi] = UnifiedValue(nh);
                                        }
                                        if (sSym->fields[fi].type == rs_sem::PrimitiveType::Object ||
                                            sSym->fields[fi].type == rs_sem::PrimitiveType::String ||
                                            sSym->fields[fi].type == rs_sem::PrimitiveType::Array) {
                                            sRefFields.push_back(fi);
                                        }
                                    }
                                    if (auto sRef = domains->uiHeap().allocateObject(sSym->id, std::move(sFields), std::move(sRefFields), &aErr)) {
                                        outSender = UnifiedValue(*sRef);
                                    }
                                }
                                const rs_sem::TypeSymbol* eSym = cachedEventArgsSym;
                                if (eSym) {
                                    RuntimeError aErr{};
                                    const NativeHandle nh = senderGuard->ToNativeHandle(domains->nativeHandles());
                                    std::vector<UnifiedValue> eFields(eSym->fields.size(), UnifiedValue(NullObject{}));
                                    std::vector<std::size_t> eRefFields;
                                    for (std::size_t fi = 0u; fi < eSym->fields.size(); ++fi) {
                                        if (eSym->fields[fi].name == "OriginalSource") {
                                            eFields[fi] = UnifiedValue(nh);
                                        } else if (eSym->fields[fi].name == "EventName") {
                                            if (auto sr = domains->uiHeap().allocateString(evNameCopy, &aErr)) {
                                                eFields[fi] = UnifiedValue(*sr);
                                            }
                                        } else if (eSym->fields[fi].name == "Handled") {
                                            eFields[fi] = UnifiedValue(false);
                                        } else if (eSym->fields[fi].name == "SelectedIndex") {
                                            std::int64_t si = -1;
                                            if (hasArg && std::holds_alternative<std::int64_t>(argVal)) si = std::get<std::int64_t>(argVal);
                                            eFields[fi] = value::FromInt(si);
                                        } else if (eSym->fields[fi].name == "Text" || eSym->fields[fi].name == "NewText" || eSym->fields[fi].name == "Uri") {
                                            const std::string s = hasArg ? rs_rt::valueToString(argVal) : std::string();
                                            if (auto sr = domains->uiHeap().allocateString(s, &aErr)) eFields[fi] = UnifiedValue(*sr);
                                        } else if (eSym->fields[fi].name == "NewValue") {
                                            float fv = 0.0f;
                                            if (hasArg) (void)TryGetFloat(argVal, fv);
                                            eFields[fi] = MakeFloat(fv);
                                        } else if (eSym->fields[fi].name == "KeyCode") {
                                            std::int64_t kc = 0;
                                            if (hasArg && std::holds_alternative<std::int64_t>(argVal)) kc = std::get<std::int64_t>(argVal) & 0xFFFF;
                                            eFields[fi] = value::FromInt(kc);
                                        } else if (eSym->fields[fi].name == "Key") {
                                            std::int64_t kc = (hasArg && std::holds_alternative<std::int64_t>(argVal)) ? (std::get<std::int64_t>(argVal) & 0xFFFF) : 0;
                                            if (auto sr = domains->uiHeap().allocateString(KeyCodeToName(static_cast<KeyCode>(kc)), &aErr)) {
                                                eFields[fi] = UnifiedValue(*sr);
                                            }
                                        } else if (eSym->fields[fi].name == "IsCtrlDown") {
                                            const std::int64_t m = (hasArg && std::holds_alternative<std::int64_t>(argVal)) ? ((std::get<std::int64_t>(argVal) >> 16) & 0xFF) : 0;
                                            eFields[fi] = UnifiedValue((m & KeyMod_Ctrl) != 0);
                                        } else if (eSym->fields[fi].name == "IsShiftDown") {
                                            const std::int64_t m = (hasArg && std::holds_alternative<std::int64_t>(argVal)) ? ((std::get<std::int64_t>(argVal) >> 16) & 0xFF) : 0;
                                            eFields[fi] = UnifiedValue((m & KeyMod_Shift) != 0);
                                        } else if (eSym->fields[fi].name == "IsAltDown") {
                                            const std::int64_t m = (hasArg && std::holds_alternative<std::int64_t>(argVal)) ? ((std::get<std::int64_t>(argVal) >> 16) & 0xFF) : 0;
                                            eFields[fi] = UnifiedValue((m & KeyMod_Alt) != 0);
                                        }
                                        if (eSym->fields[fi].type == rs_sem::PrimitiveType::Object ||
                                            eSym->fields[fi].type == rs_sem::PrimitiveType::String ||
                                            eSym->fields[fi].type == rs_sem::PrimitiveType::Array) {
                                            eRefFields.push_back(fi);
                                        }
                                    }
                                    if (auto eRef = domains->uiHeap().allocateObject(eSym->id, std::move(eFields), std::move(eRefFields), &aErr)) {
                                        outEvArgs = UnifiedValue(*eRef);
                                    }
                                }
                                invokedEventArgs = outEvArgs;
                            };
                            if (isSimCommand && domains) {
                                std::vector<UnifiedValue> payload;
                                if (hasArg) {
                                    payload.push_back(argVal);
                                }
                                domains->enqueueSimCommand(SimCommandEnvelope{
                                    methodId,
                                    targetObj,
                                    std::move(payload),
                                    0ULL});
                                return false;
                            }
                            auto convertStringArgIfNeeded = [&](UnifiedValue& v) {
                                if (domains) {
                                    if (const auto* sv = std::get_if<StringValue>(&v)) {
                                        RuntimeError sErr{};
                                        if (auto sr = domains->uiHeap().allocateString(std::string(sv->view()), &sErr)) {
                                            v = *sr;
                                        }
                                    }
                                }
                            };
                            if (eventInterpreter) {
                                if (expectedParamCount == 1) {
                                    std::vector<UnifiedValue> args{targetObj};
                                    (void)eventInterpreter->invoke(methodId, args);
                                } else if (expectedParamCount == 2) {
                                    UnifiedValue callArg = hasArg ? argVal : UnifiedValue(NullObject{});
                                    convertStringArgIfNeeded(callArg);
                                    std::vector<UnifiedValue> args{targetObj, callArg};
                                    (void)eventInterpreter->invoke(methodId, args);
                                } else if (expectedParamCount == 3) {
                                    UnifiedValue sVal{}, eVal{};
                                    buildWpfSenderAndArgs(sVal, eVal);
                                    std::vector<UnifiedValue> args{targetObj, sVal, eVal};
                                    (void)eventInterpreter->invoke(methodId, args);
                                } else {
                                    std::vector<UnifiedValue> args{targetObj};
                                    if (hasArg) {
                                        UnifiedValue callArg = argVal;
                                        convertStringArgIfNeeded(callArg);
                                        args.push_back(callArg);
                                    }
                                    auto res = eventInterpreter->invoke(methodId, args);
                                    if (!res.succeeded &&
                                        res.error.code == ErrorCode::InvalidArguments &&
                                        res.statistics.instructionsExecuted == 0u) {
                                        if (args.size() > 1u) {
                                            args.resize(1u);
                                            res = eventInterpreter->invoke(methodId, args);
                                        }
                                        if (!res.succeeded &&
                                            res.error.code == ErrorCode::InvalidArguments &&
                                            res.statistics.instructionsExecuted == 0u) {
                                            UnifiedValue sVal{}, eVal{};
                                            buildWpfSenderAndArgs(sVal, eVal);
                                            args = {targetObj, sVal, eVal};
                                            (void)eventInterpreter->invoke(methodId, args);
                                        }
                                    }
                                }
                            } else if (eventAotContext) {
                                UnifiedValue ret{};
                                if (expectedParamCount == 1) {
                                    UnifiedValue args[1]{targetObj};
                                    (void)eventAotContext->invoke(methodId, args, 1u, ret);
                                } else if (expectedParamCount == 2) {
                                    UnifiedValue callArg = hasArg ? argVal : UnifiedValue(NullObject{});
                                    convertStringArgIfNeeded(callArg);
                                    UnifiedValue args[2]{targetObj, callArg};
                                    (void)eventAotContext->invoke(methodId, args, 2u, ret);
                                } else if (expectedParamCount == 3) {
                                    UnifiedValue sVal{}, eVal{};
                                    buildWpfSenderAndArgs(sVal, eVal);
                                    UnifiedValue args[3]{targetObj, sVal, eVal};
                                    (void)eventAotContext->invoke(methodId, args, 3u, ret);
                                } else {
                                    std::vector<UnifiedValue> args{targetObj};
                                    if (hasArg) {
                                        UnifiedValue callArg = argVal;
                                        convertStringArgIfNeeded(callArg);
                                        args.push_back(callArg);
                                    }
                                    if (!eventAotContext->invoke(
                                            methodId, args.data(), args.size(), ret)) {
                                        bool okFallback = false;
                                        if (args.size() > 1u) {
                                            okFallback = eventAotContext->invoke(
                                                methodId, args.data(), 1u, ret);
                                        }
                                        if (!okFallback) {
                                            UnifiedValue sVal{}, eVal{};
                                            buildWpfSenderAndArgs(sVal, eVal);
                                            UnifiedValue a3[3]{targetObj, sVal, eVal};
                                            (void)eventAotContext->invoke(methodId, a3, 3u, ret);
                                        }
                                    }
                                }
                            }
                            if (bindingEngine && DualTrackBindingEngine::IsLiveInstance(bindingEngine)) {
                                (void)bindingEngine->FlushTrackA();
                            } else if (senderGuard && senderGuard->BoundTwoWayEngine()) {
                                (void)senderGuard->BoundTwoWayEngine()->FlushTrackA();
                            }
                            if (domains && cachedEventArgsSym) {
                                if (const auto* eRef = std::get_if<ObjectRef>(&invokedEventArgs);
                                    eRef && eRef->valid() && domains->uiHeap().isAlive(*eRef)) {
                                    for (std::size_t fi = 0u; fi < cachedEventArgsSym->fields.size(); ++fi) {
                                        if (cachedEventArgsSym->fields[fi].name == "Handled") {
                                            if (const auto hv = domains->uiHeap().fieldGet(*eRef, fi)) {
                                                if (const auto* hb = std::get_if<bool>(&*hv)) {
                                                    return *hb;
                                                }
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                            return false;
                        };

                        if (evNameCopy == "Loaded") {
                            if (!structureOnly) {
                                deferredLoadedHandlers.push_back([invokeBoundHandler]() {
                                    (void)invokeBoundHandler(UnifiedValue{}, false);
                                });
                            }
                        } else if (evNameCopy == "KeyDown") {
                            elem->SetKeyDownCallback([invokeBoundHandler](UIElement&, KeyCode k, std::uint8_t mods) -> bool {
                                const std::int64_t packed = static_cast<std::int64_t>(static_cast<std::uint32_t>(k) | (static_cast<std::uint32_t>(mods) << 16u));
                                return invokeBoundHandler(value::FromInt(packed), true);
                            });
                        } else if (evNameCopy == "KeyUp") {
                            elem->SetKeyUpCallback([invokeBoundHandler](UIElement&, KeyCode k, std::uint8_t mods) -> bool {
                                const std::int64_t packed = static_cast<std::int64_t>(static_cast<std::uint32_t>(k) | (static_cast<std::uint32_t>(mods) << 16u));
                                return invokeBoundHandler(value::FromInt(packed), true);
                            });
                        } else if (evNameCopy == "MouseEnter") {
                            elem->SetMouseEnterCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (evNameCopy == "MouseLeave") {
                            elem->SetMouseLeaveCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (evNameCopy == "MouseDown" || evNameCopy == "MouseLeftButtonDown") {
                            elem->SetMouseDownCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (evNameCopy == "MouseUp" || evNameCopy == "MouseLeftButtonUp") {
                            elem->SetMouseUpCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (evNameCopy == "GotFocus") {
                            elem->SetGotFocusCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (evNameCopy == "LostFocus") {
                            elem->SetLostFocusCallback([invokeBoundHandler](UIElement&) -> bool {
                                return invokeBoundHandler(UnifiedValue{}, false);
                            });
                        } else if (auto* slider = elem->DynamicCast<Slider>()) {
                            slider->SetValueChangedCallback(
                                [invokeBoundHandler](Slider&, float val) {
                                    invokeBoundHandler(MakeFloat(val), true);
                                });
                        } else if (auto* pwd = elem->DynamicCast<PasswordBox>();
                                   pwd && evNameCopy == "PasswordChanged") {
                            pwd->SetPasswordChangedCallback(
                                [invokeBoundHandler](PasswordBox&, const std::string& secret) {
                                    invokeBoundHandler(value::FromString(secret), true);
                                });
                        } else if (auto* tb = elem->DynamicCast<TextBox>()) {
                            tb->SetTextChangedCallback(
                                [invokeBoundHandler](TextBox&, const std::string& text) {
                                    invokeBoundHandler(value::FromString(text), true);
                                });
                        } else if (auto* combo = elem->DynamicCast<ComboBox>()) {
                            combo->SetSelectionChangedCallback(
                                [invokeBoundHandler](ComboBox&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* listBox = elem->DynamicCast<ListBox>()) {
                            listBox->SetSelectionChangedCallback(
                                [invokeBoundHandler](ListBox&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* tabCtrl = elem->DynamicCast<TabControl>()) {
                            tabCtrl->SetSelectionChangedCallback(
                                [invokeBoundHandler](TabControl&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* expCtrl = elem->DynamicCast<Expander>()) {
                            const std::string wantExpEv = evNameCopy;
                            expCtrl->SetExpandedChangedCallback(
                                [invokeBoundHandler, wantExpEv](Expander&, bool expanded) {
                                    if ((wantExpEv == "Expanded" && expanded) ||
                                        (wantExpEv == "Collapsed" && !expanded) ||
                                        (wantExpEv != "Expanded" && wantExpEv != "Collapsed")) {
                                        invokeBoundHandler(value::FromBool(expanded), true);
                                    }
                                });
                        } else if (auto* treeCtrl = elem->DynamicCast<TreeView>()) {
                            treeCtrl->SetSelectedItemChangedCallback(
                                [invokeBoundHandler](TreeView& tv, TreeViewItem*) {
                                    invokeBoundHandler(value::FromString(tv.GetSelectedHeader()), true);
                                });
                        } else if (auto* chkCtrl = elem->DynamicCast<CheckBox>();
                                   chkCtrl && (evNameCopy == "Checked" || evNameCopy == "Unchecked" || evNameCopy == "Indeterminate")) {
                            const std::string wantEv = evNameCopy;
                            chkCtrl->AddCheckedChangedCallback(
                                [invokeBoundHandler, wantEv](CheckBox& cb, bool checked) {
                                    const bool isInd = std::holds_alternative<NullObject>(cb.GetValue(PropId_IsChecked));
                                    if ((wantEv == "Indeterminate" && isInd) ||
                                        (wantEv == "Checked" && !isInd && checked) ||
                                        (wantEv == "Unchecked" && !isInd && !checked)) {
                                        invokeBoundHandler(value::FromBool(checked), true);
                                    }
                                });
                        } else if (auto* radCtrl = elem->DynamicCast<RadioButton>();
                                   radCtrl && (evNameCopy == "Checked" || evNameCopy == "Unchecked")) {
                            const std::string wantEv = evNameCopy;
                            radCtrl->SetCheckedChangedCallback(
                                [invokeBoundHandler, wantEv](RadioButton&, bool checked) {
                                    if ((wantEv == "Checked" && checked) ||
                                        (wantEv == "Unchecked" && !checked)) {
                                        invokeBoundHandler(value::FromBool(checked), true);
                                    }
                                });
                        } else if (auto* hl = elem->DynamicCast<Hyperlink>();
                                   hl && evNameCopy == "RequestNavigate") {
                            hl->SetRequestNavigateCallback(
                                [invokeBoundHandler](Hyperlink&, const std::string& uri) {
                                    invokeBoundHandler(value::FromString(uri), true);
                                });
                        } else if (IsClickableButtonOrMenu(elem)) {
                            if (isSimCommand && domains) {
                                SetElementClickCallback(
                                    elem,
                                    [domains, targetObj, methodId](UIElement& b) {
                                        std::vector<UnifiedValue> payload;
                                        const UnifiedValue cmdParam = b.GetValue(PropId_CommandParameter);
                                        if (!std::holds_alternative<std::monostate>(cmdParam)) {
                                            payload.push_back(cmdParam);
                                        }
                                        domains->enqueueSimCommand(SimCommandEnvelope{
                                            methodId,
                                            targetObj,
                                            std::move(payload),
                                            0ULL});
                                    });
                            } else if (eventInterpreter) {
                                if (expectedParamCount == 3) {
                                    SetElementClickCallback(elem, [invokeBoundHandler](UIElement& b) {
                                        const UnifiedValue cp = b.GetValue(PropId_CommandParameter);
                                        const bool hasCp = !std::holds_alternative<std::monostate>(cp);
                                        invokeBoundHandler(cp, hasCp);
                                    });
                                } else if (auto* btn = elem->DynamicCast<ButtonBase>()) {
                                    btn->BindScriptClickHandler(eventInterpreter, targetObj, methodId);
                                } else if (auto* mi = elem->DynamicCast<MenuItem>()) {
                                    mi->BindScriptClickHandler(eventInterpreter, targetObj, methodId);
                                }
                            } else if (eventAotContext) {
                                if (auto* btn = elem->DynamicCast<ButtonBase>()) {
                                    btn->BindAotClickHandler(eventAotContext, targetObj, methodId);
                                } else if (auto* mi = elem->DynamicCast<MenuItem>()) {
                                    mi->BindAotClickHandler(eventAotContext, targetObj, methodId);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case AxbOpcode::AddRowDefinition: {
                if (!bindingsOnly && !elementStack.empty() && elementStack.back()) {
                    if (auto* grid = elementStack.back()->DynamicCast<Grid>()) {
                        const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                        grid->AddRowDefinition(static_cast<GridUnitType>(inst.flags), val);
                    }
                }
                break;
            }

            case AxbOpcode::AddColumnDefinition: {
                if (!bindingsOnly && !elementStack.empty() && elementStack.back()) {
                    if (auto* grid = elementStack.back()->DynamicCast<Grid>()) {
                        const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                        grid->AddColumnDefinition(static_cast<GridUnitType>(inst.flags), val);
                    }
                }
                break;
            }

            case AxbOpcode::SetResourceProperty: {
                if (!bindingsOnly && !elementStack.empty() && elementStack.back()) {
                    UIElement* targetElem = elementStack.back();
                    const std::string_view key = package.GetString(inst.strIdx0);
                    const bool isDynamicOrLoc =
                        (inst.flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) != 0u;

                    const UnifiedValue* resolved = nullptr;
                    if (isDynamicOrLoc) {
                        resolved = AxbTemplateRegistry::Instance().FindDynamicResourceForElement(
                            key,
                            targetElem);
                    }
                    if (!resolved) {
                        for (auto it = package.resources.rbegin(); it != package.resources.rend(); ++it) {
                            if (it->first == key) {
                                resolved = &it->second;
                                break;
                            }
                        }
                    }
                    if (!resolved) {
                        resolved = AxbTemplateRegistry::Instance().FindResourceForElement(
                            key,
                            targetElem);
                    }

                    if (resolved) {
                        AxbTemplateRegistry::ApplyResolvedResourceToElement(
                            targetElem, inst.propId, *resolved);
                    } else if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        AxbTemplateRegistry::ApplyResolvedResourceToElement(
                            targetElem, inst.propId, inst.literalValue);
                    }

                    if (isDynamicOrLoc) {
                        AxbTemplateRegistry::Instance().SubscribeDynamicResource(
                            targetElem,
                            rootControl,
                            inst.propId,
                            key,
                            inst.flags);
                    }
                }
                break;
            }

            case AxbOpcode::BindTemplateParent: {
                if (!bindingsOnly && !elementStack.empty() && elementStack.back()) {
                    UIElement* targetElem = elementStack.back();
                    UIElement* host = templatedHost ? templatedHost : targetElem->TemplatedParent();
                    if (host) {
                        std::uint16_t sourcePropId = static_cast<std::uint16_t>(inst.strIdx0);
                        if (sourcePropId == PropId_Tag || sourcePropId == 0xFFFFu ||
                            IsCustomPropertyId(sourcePropId)) {
                            const std::string_view strPath = package.GetString(inst.strIdx1);
                            const std::string_view propPath =
                                !strPath.empty() ? strPath
                                                 : (IsCustomPropertyId(sourcePropId)
                                                        ? GetCustomPropertyName(sourcePropId)
                                                        : std::string_view{});
                            if (!propPath.empty() && propPath != "Tag") {
                                if (!host->DeclaredTypeName().empty()) {
                                    const std::uint16_t ownerPropId =
                                        FindCustomProperty(host->DeclaredTypeName(), propPath);
                                    if (ownerPropId != 0xFFFFu) {
                                        sourcePropId = ownerPropId;
                                    }
                                }
                                if (sourcePropId == PropId_Tag || sourcePropId == 0xFFFFu) {
                                    const std::uint16_t latePropId = ResolveBuiltinPropertyId(propPath);
                                    if (latePropId != PropId_Tag) {
                                        sourcePropId = latePropId;
                                    }
                                }
                            }
                        }
                        host->AddTemplateBinding(
                            targetElem,
                            sourcePropId,
                            inst.propId,
                            static_cast<ValueConverterKind>(inst.flags));
                    }
                }
                break;
            }
        }
    }

    // Automatically resolve & mount ControlTemplates (via Template attribute, explicit Style, or implicit TargetType Style)
    const std::size_t startIdx = templatedHost ? 1u : 0u;
    if (!bindingsOnly) {
        for (std::size_t i = startIdx; i < localElements.size(); ++i) {
            AutoApplyControlStyleTemplate(
                localElements[i],
                localDeclaredTypes[i],
                package,
                domains,
                bindingEngine,
                effectiveContext,
                eventInterpreter,
                eventAotContext,
                animationEngine);
        }
    }

    // Instantiate ContentTemplates on ContentControl, ContentPresenter, and compatible template hosts
    for (std::size_t i = startIdx; i < localElements.size(); ++i) {
        if (UIElement* el = localElements[i]) {
            el->SyncTemplateContentHost();
        }
    }

    if (rootVisited && animationEngine && package.HasMotionSection()) {
        animationEngine->InstantiatePackageMotion(
            rootControl,
            localElements.data(),
            localElements.size(),
            package);
    }

    for (const auto& fn : deferredLoadedHandlers) {
        if (fn) {
            fn();
        }
    }

    return rootVisited;
}

} // namespace detail

void EnsureControlTypesRegistered() noexcept {
    static std::atomic<bool> sDone{false};
    if (sDone.load(std::memory_order_acquire)) {
        return;
    }

    static std::recursive_mutex sMutex;
    static bool sInitializing = false;
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (sDone.load(std::memory_order_relaxed) || sInitializing) {
        return;
    }
    sInitializing = true;

    EnsureBuiltinTypesRegistered();
    RegisterUIElementTemplateHooks(&CreateElementByTypeName, &MountControlTemplatePackage);
    RegisterUIElementInteractionHooks(&MountAxbTemplate, &TryExecuteRoutedCommandBubble);

    {
        ScopedTypeBatch batch;
        const TypeClass* const kControlClasses[] = {
            // Control.hpp
            Control::StaticTypeClass(),
            ContentControl::StaticTypeClass(),
            HeaderedContentControl::StaticTypeClass(),
            GroupBox::StaticTypeClass(),
            Expander::StaticTypeClass(),
            Thumb::StaticTypeClass(),
            Separator::StaticTypeClass(),
            // Decorators.hpp
            Decorator::StaticTypeClass(),
            Border::StaticTypeClass(),
            BoxShadow::StaticTypeClass(),
            Viewbox::StaticTypeClass(),
            LayoutScaler::StaticTypeClass(),
            UserControl::StaticTypeClass(),
            // Panels.hpp
            Panel::StaticTypeClass(),
            StackPanel::StaticTypeClass(),
            DockPanel::StaticTypeClass(),
            WrapPanel::StaticTypeClass(),
            Grid::StaticTypeClass(),
            Canvas::StaticTypeClass(),
            UniformGrid::StaticTypeClass(),
            ScrollViewer::StaticTypeClass(),
            Popup::StaticTypeClass(),
            ToolTip::StaticTypeClass(),
            Window::StaticTypeClass(),
            // Shapes.hpp
            Shape::StaticTypeClass(),
            Rectangle::StaticTypeClass(),
            Ellipse::StaticTypeClass(),
            Line::StaticTypeClass(),
            Path::StaticTypeClass(),
            Polygon::StaticTypeClass(),
            Polyline::StaticTypeClass(),
            // Buttons.hpp
            ButtonBase::StaticTypeClass(),
            ToggleButton::StaticTypeClass(),
            CheckBox::StaticTypeClass(),
            RadioButton::StaticTypeClass(),
            Button::StaticTypeClass(),
            RepeatButton::StaticTypeClass(),
            Hyperlink::StaticTypeClass(),
            // RangeControls.hpp
            RangeBase::StaticTypeClass(),
            Slider::StaticTypeClass(),
            ProgressBar::StaticTypeClass(),
            GridSplitter::StaticTypeClass(),
            // TextControls.hpp
            TextBlock::StaticTypeClass(),
            Label::StaticTypeClass(),
            TextBoxBase::StaticTypeClass(),
            TextBox::StaticTypeClass(),
            PasswordBox::StaticTypeClass(),
            // ItemsControls.hpp
            ContentPresenter::StaticTypeClass(),
            ItemsControl::StaticTypeClass(),
            HeaderedItemsControl::StaticTypeClass(),
            MenuBase::StaticTypeClass(),
            Selector::StaticTypeClass(),
            ListBox::StaticTypeClass(),
            ListView::StaticTypeClass(),
            ComboBox::StaticTypeClass(),
            ListBoxItem::StaticTypeClass(),
            ComboBoxItem::StaticTypeClass(),
            ListViewItem::StaticTypeClass(),
            TabItem::StaticTypeClass(),
            TabControl::StaticTypeClass(),
            StatusBar::StaticTypeClass(),
            StatusBarItem::StaticTypeClass(),
            ToolBar::StaticTypeClass(),
            Menu::StaticTypeClass(),
            ContextMenu::StaticTypeClass(),
            MenuItem::StaticTypeClass(),
            TreeViewItem::StaticTypeClass(),
            TreeView::StaticTypeClass(),
            // MediaControls.hpp
            Image::StaticTypeClass(),
            RiveControl::StaticTypeClass(),
            MediaElement::StaticTypeClass(),
        };

        auto& reg = TypeRegistry::Instance();
        struct TypeAliasEntry {
            const char* alias;
            const TypeClass* target;
        };
        const TypeAliasEntry kTypeAliases[] = {
            {"Page", UserControl::StaticTypeClass()},
            {"RichTextBox", TextBox::StaticTypeClass()},
            {"VirtualizingStackPanel", StackPanel::StaticTypeClass()},
            {"ResourceDictionary", Panel::StaticTypeClass()},
            {"Track", Panel::StaticTypeClass()},
            {"TabPanel", WrapPanel::StaticTypeClass()},
            {"ToolBarOverflowPanel", WrapPanel::StaticTypeClass()},
            {"TransformGroup", StackPanel::StaticTypeClass()},
            {"ToolBarTray", ToolBar::StaticTypeClass()},
            {"TransitionContentPresenter", ContentPresenter::StaticTypeClass()},
            {"ViewboxDecorator", Decorator::StaticTypeClass()},
            {"AdornerDecorator", Decorator::StaticTypeClass()},
            {"BulletDecorator", Decorator::StaticTypeClass()},
            {"ScrollContentPresenter", Decorator::StaticTypeClass()},
            {"ScrollBar", Control::StaticTypeClass()},
            {"TickBar", Control::StaticTypeClass()},
            {"ItemsPresenter", ItemsControl::StaticTypeClass()},
            {"TranslateTransform", Border::StaticTypeClass()},
            {"RotateTransform", Border::StaticTypeClass()},
            {"ScaleTransform", Border::StaticTypeClass()},
            {"SkewTransform", Border::StaticTypeClass()},
            {"GradientStop", Border::StaticTypeClass()},
        };
        for (const TypeAliasEntry& entry : kTypeAliases) {
            reg.RegisterAlias(entry.alias, entry.target);
        }

        for (const TypeClass* tc : kControlClasses) {
            if (tc && tc->typeName) {
                const std::string_view shortName = str::ShortTypeName(tc->typeName);
                if (!shortName.empty() && reg.FindByName(shortName) == nullptr) {
                    reg.RegisterAlias(shortName, tc);
                }
            }
        }
    }

    sInitializing = false;
    sDone.store(true, std::memory_order_release);
}

void RegisterCustomControlType(
    std::string_view typeName,
    IntrusiveCustomControlFactory factory,
    bool isTemplatedControl) {
    if (factory == nullptr) {
        return;
    }
    StoreCustomControlEntry(typeName, RegisteredCustomControlEntry{factory, isTemplatedControl});
}

bool IsCustomControlRegistered(
    std::string_view typeName,
    bool* outIsTemplatedControl) noexcept {
    const std::string_view cleanType = str::Trim(typeName);
    if (cleanType.empty()) {
        return false;
    }
    auto& state = GetCustomControlRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (const auto it = state.byExactName.find(std::string(cleanType));
        it != state.byExactName.end()) {
        if (outIsTemplatedControl) {
            *outIsTemplatedControl = it->second.isTemplatedControl;
        }
        return true;
    }
    const std::string_view shortType = str::ShortTypeName(cleanType);
    if (!shortType.empty()) {
        if (const auto it = state.byShortName.find(std::string(shortType));
            it != state.byShortName.end()) {
            if (outIsTemplatedControl) {
                *outIsTemplatedControl = it->second.isTemplatedControl;
            }
            return true;
        }
    }
    return false;
}

IntrusivePtr<UIElement> CreateElementByTypeName(std::string_view typeName) {
    EnsureControlTypesRegistered();
    const std::string_view shortName = str::ShortTypeName(typeName);
    IntrusivePtr<UIElement> elem = detail::CreateElementByTypeNameCore(typeName, shortName);
    if (elem && !shortName.empty()) {
        elem->SetDeclaredTypeName(std::string(shortName));
    }
    return elem;
}

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return detail::MountAxbInstructionsInternal(
        rootControl,
        nullptr,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
}

bool PreMountAxbTemplateStructure(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef wrapperContext,
    std::vector<IntrusivePtr<UIElement>>& outMountedElements,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return detail::MountAxbInstructionsInternal(
        rootControl,
        nullptr,
        package,
        domains,
        bindingEngine,
        wrapperContext,
        eventInterpreter,
        eventAotContext,
        animationEngine,
        true,
        false,
        &outMountedElements);
}

bool RebindMountedAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    std::vector<IntrusivePtr<UIElement>>& mountedElements,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return detail::MountAxbInstructionsInternal(
        rootControl,
        nullptr,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine,
        false,
        true,
        &mountedElements);
}

bool MountControlTemplatePackage(
    UIElement* hostControl,
    UIElement* templateRootControl,
    const AxbPackage& templatePackage,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return detail::MountAxbInstructionsInternal(
        templateRootControl,
        hostControl,
        templatePackage,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
}

} // namespace aero
