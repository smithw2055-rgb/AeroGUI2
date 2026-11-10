#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

// Zero-allocation Style, Property/MultiTrigger, DataTrigger/MultiDataTrigger, EventTrigger & VisualStateManager engine:
// - Resolves keyed Style="{StaticResource ...}", BasedOn inheritance chains, and implicit TargetType styles
// - Pre-allocates per-element property override backup slots & EventTrigger state slots on style attachment (0 heap allocs per frame!)
// - Evaluates inline interactive state flags, DataContext ObjectRef multi-hop slots, ElementName, and RelativeSource conditions
//   into a 64-bit activeTriggerMask with O(1) clean-frame short-circuiting
// - Dispatches RoutedEvent EventTriggers (Loaded, Click, MouseEnter, MouseLeave, Checked, Unchecked) and VSM transitions
class StyleTriggerEngine {
public:
    static constexpr std::size_t kMaxVisualStateGroupsPerElement = 4u;
    static constexpr std::size_t kMaxEventTriggersPerElement = 8u;

    explicit StyleTriggerEngine(
        AnimationEngine* animationEngine = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr);

    void SetAnimationEngine(AnimationEngine* animationEngine) noexcept {
        animationEngine_ = animationEngine;
    }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept {
        return animationEngine_;
    }

    void SetBindingEngine(DualTrackBindingEngine* bindingEngine) noexcept {
        bindingEngine_ = bindingEngine;
        if (bindingEngine) {
            domains_ = &bindingEngine->Domains();
            program_ = bindingEngine->Program();
            uiInterpreter_ = bindingEngine->UiInterpreter();
        }
    }
    [[nodiscard]] DualTrackBindingEngine* GetBindingEngine() const noexcept {
        return bindingEngine_;
    }

    void SetDomains(
        ExecutionDomainContext* domains,
        const ProgramImage* program = nullptr,
        const Interpreter* uiInterpreter = nullptr) noexcept {
        domains_ = domains;
        if (program) program_ = program;
        if (uiInterpreter) uiInterpreter_ = uiInterpreter;
    }

    void Clear();

    // Register style & storyboard definitions (automatically resolves BasedOn inheritance)
    void RegisterStyle(AxbStyleDefinition style);
    void RegisterStoryboard(AxbStoryboard storyboard);
    void RegisterPackageStyles(const AxbPackage& package);
    void ImportRegistryStyles();

    [[nodiscard]] const AxbStyleDefinition* FindStyle(std::string_view key) const noexcept;
    [[nodiscard]] const AxbStyleDefinition* FindImplicitStyle(std::string_view shortTypeName) const noexcept;
    [[nodiscard]] const AxbStoryboard* FindStoryboard(std::string_view key) const noexcept;

    // Attach a style to a single element or recursively across a visual subtree
    bool AttachStyle(UIElement* element, const AxbStyleDefinition& rawStyle);
    bool AttachStyleByKey(UIElement* element, std::string_view styleKey);
    std::size_t BindSubtreeStyles(UIElement* rootElement, bool applyImplicitTypeStyles = true);
    std::size_t UnbindSubtreeStyles(const UIElement* subtreeRoot) noexcept;

    // Imperative / script VisualStateManager transition (e.g. GoToState(card, "Expanded", true))
    bool GoToVisualState(
        UIElement* element,
        std::string_view stateName,
        bool useTransitions = true);

    // Evaluate all bound Property/Data/Event triggers & automatic CommonStates/FocusStates/CheckStates (0 heap allocs!)
    // Returns number of elements whose active trigger mask, visual state, or routed event fired this pass.
    std::size_t EvaluateAndApply();
    std::size_t EvaluateSubtree(UIElement* subtreeRoot, bool resetEventStates = false);

    [[nodiscard]] std::size_t BoundElementCount() const noexcept { return instances_.size(); }

private:
    struct PropertySnapshotSlot {
        UIElement* resolvedTarget = nullptr;
        std::uint16_t propId = 0;
        bool captured = false;
        bool hadBaseValue = false;
        bool touchedThisPass = false;
        ValueSourceRank baseRank = ValueSourceRank::ThemeStyle;
        UnifiedValue baseValue{};
    };

    struct VisualGroupRuntimeState {
        std::int16_t activeStateIndex = -1;
        bool manualOverride = false;
    };

    struct EventTriggerRuntimeState {
        UIElement* cachedSource = nullptr;
        std::uint32_t lastClickGen = 0u;
        bool prevHovered = false;
        bool prevChecked = false;
        bool initialized = false;
    };

    struct StyledElementInstance {
        IntrusivePtr<UIElement> element{};
        AxbStyleDefinition resolvedStyle{};
        std::uint64_t activeTriggerMask = 0ULL;
        bool initialized = false;
        std::array<VisualGroupRuntimeState, kMaxVisualStateGroupsPerElement> groupStates{};
        std::array<EventTriggerRuntimeState, kMaxEventTriggersPerElement> eventStates{};
        std::vector<PropertySnapshotSlot> propertySnapshots;
    };

    [[nodiscard]] AxbStyleDefinition FlattenStyleChain(const AxbStyleDefinition& leaf) const;
    [[nodiscard]] bool EvaluateSingleCondition(
        UIElement* elem,
        AxbTriggerCondition& cond);
    [[nodiscard]] bool EvaluateEventTriggers(StyledElementInstance& inst);
    [[nodiscard]] static bool HasFocusedDescendant(const UIElement* elem) noexcept;
    [[nodiscard]] static bool IsFastInlineAtDefault(const UIElement* elem, std::uint16_t propId) noexcept;

    void EnsurePropertySnapshotSlot(
        StyledElementInstance& inst,
        UIElement* target,
        std::uint16_t propId);
    void CaptureSnapshotIfNeeded(PropertySnapshotSlot& slot);
    void RestoreAllCapturedSnapshots(StyledElementInstance& inst);
    void ReapplyActiveOverrides(
        StyledElementInstance& inst,
        std::uint64_t newTriggerMask,
        bool useTransitions,
        bool isInitialPass);

    AnimationEngine* animationEngine_ = nullptr;
    DualTrackBindingEngine* bindingEngine_ = nullptr;
    ExecutionDomainContext* domains_ = nullptr;
    const ProgramImage* program_ = nullptr;
    const Interpreter* uiInterpreter_ = nullptr;
    std::unordered_map<std::string, AxbStyleDefinition> keyedStyles_;
    std::unordered_map<std::string, AxbStyleDefinition> implicitTypeStyles_;
    std::unordered_map<std::string, AxbStoryboard> keyedStoryboards_;
    std::vector<StyledElementInstance> instances_;
};

} // namespace aero
