#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

struct AxbPackage;
class StyleTriggerEngine;
class AnimationEngine;

// Mount and wire a compiled x:Class MainWindow (or ViewModel fallback) package:
// 1) Binds mainWinRef via rootControl->BindScriptWrapper(...)
// 2) Extracts mainWinRef.ViewModel ObjectRef if present/alive (falling back to mainWinRef)
//    and assigns PropId_DataContext = vmRef
// 3) Mounts package against vmRef while BoundScriptWrapper() is mainWinRef (populating
//    __nativeRootHandle & namedControls on mainWinRef), wiring StyleTriggerEngine & DualTrackBindingEngine
// 4) Invokes zero-arg mainWinRef.OnInitialized() if defined, followed by initial dirty-binding & trigger sync.
bool MountAndWireMainWindow(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext& domains,
    DualTrackBindingEngine& bindingEngine,
    StyleTriggerEngine& styleEngine,
    ObjectRef mainWinRef,
    const Interpreter* uiInterpreter = nullptr,
    rs_aot::ExecutionContext* aotContext = nullptr,
    AnimationEngine* animationEngine = nullptr,
    ObjectRef* outViewModelRef = nullptr);

[[nodiscard]] IntrusivePtr<UIElement> MountAndWireMainWindow(
    const AxbPackage& package,
    ExecutionDomainContext& domains,
    DualTrackBindingEngine& bindingEngine,
    StyleTriggerEngine& styleEngine,
    ObjectRef mainWinRef,
    const Interpreter* uiInterpreter = nullptr,
    rs_aot::ExecutionContext* aotContext = nullptr,
    AnimationEngine* animationEngine = nullptr,
    ObjectRef* outViewModelRef = nullptr);

void SetPendingMainWindowMountTarget(
    const ExecutionDomainContext* domains,
    UIElement* rootControl,
    const AxbPackage* package);

void ClearPendingMainWindowMountTarget(const ExecutionDomainContext* domains) noexcept;
void ClearScriptUiDomainState(const ExecutionDomainContext* domains) noexcept;
void SetActiveApplicationObject(const ExecutionDomainContext* domains, ObjectRef appObj);
void DispatchLoadedToRegisteredDescendants(UIElement* root, bool skipNestedUserControls = true);

struct ReactivePropertyBinding {
    ObjectRef sourceObject{};
    PropertySlotHandle sourceSlot{};
    IntrusivePtr<UIElement> targetElement{};
    std::uint16_t targetPropertyId = 0;
};

// ============================================================================
// ScriptUiBridge (P2-1 Unified Compatibility Facade):
// Zero-duplicate-polling lightweight facade delegating all reactive & two-way
// property bindings directly to an owned or externally injected DualTrackBindingEngine.
// ============================================================================
class ScriptUiBridge {
public:
    explicit ScriptUiBridge(
        ExecutionDomainContext& domains,
        const ProgramImage* program = nullptr,
        const Interpreter* uiInterpreter = nullptr);

    explicit ScriptUiBridge(DualTrackBindingEngine& externalEngine) noexcept;

    ~ScriptUiBridge() noexcept;

    ScriptUiBridge(const ScriptUiBridge&) = delete;
    ScriptUiBridge& operator=(const ScriptUiBridge&) = delete;

    void SetUnderlyingEngine(DualTrackBindingEngine* externalEngine) noexcept;

    [[nodiscard]] DualTrackBindingEngine& UnderlyingEngine() noexcept {
        return *engine_;
    }
    [[nodiscard]] const DualTrackBindingEngine& UnderlyingEngine() const noexcept {
        return *engine_;
    }

    // Register zero-hash C++ UIElement / Button / Window native functions into BindingRegistry
    void RegisterCoreUiBindings(BindingRegistry& bindings);

    // Bind a [Observable] source property on SimHeap or UiHeap to a UIElement property
    bool BindObservableProperty(
        ObjectRef sourceObject,
        PropertySlotHandle sourceSlot,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        RuntimeError* error = nullptr);

    // Extended overload supporting TwoWay mode & single-value converters through DualTrackBindingEngine
    bool BindObservableProperty(
        ObjectRef sourceObject,
        PropertySlotHandle sourceSlot,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        BindingMode mode,
        ValueConverterKind converter = ValueConverterKind::None,
        std::string_view converterKey = {},
        std::string_view converterParameter = {},
        RuntimeError* error = nullptr);

    // Bind a multi-hop ViewModel path (e.g. "Player.Stats.Health") via DualTrackBindingEngine
    bool BindPath(
        ObjectRef rootObject,
        std::string_view propertyPath,
        const IntrusivePtr<UIElement>& targetElement,
        std::uint16_t targetPropertyId,
        BindingMode mode = BindingMode::OneWay,
        RuntimeError* error = nullptr);

    // Push a TwoWay-bound UIElement property mutation back to the ViewModel / SimCommand queue
    bool PushTwoWayToSource(
        const UIElement* targetElement,
        std::uint16_t targetPropertyId,
        RuntimeError* error = nullptr);

    // Flush all dirty [Observable] objects & ECS columns via DualTrackBindingEngine
    std::size_t FlushReactiveBindings(RuntimeError* error = nullptr);

    [[nodiscard]] std::size_t BindingCount() const noexcept;

    // On-demand compatibility snapshot projected from DualTrackBindingEngine::ReactiveBindings()
    [[nodiscard]] std::vector<ReactivePropertyBinding> Bindings() const;

private:
    ExecutionDomainContext& domains_;
    std::unique_ptr<DualTrackBindingEngine> ownedEngine_;
    DualTrackBindingEngine* engine_ = nullptr;
    std::shared_ptr<DualTrackBindingEngine*> sharedEngineSlot_;
};

} // namespace aero
