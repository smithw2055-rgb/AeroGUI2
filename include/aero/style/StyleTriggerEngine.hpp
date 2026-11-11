#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// Zero-allocation Style, Property/MultiTrigger, DataTrigger/MultiDataTrigger, EventTrigger,
// XAML Behaviors (<b:Interaction.Triggers>) & VisualStateManager engine
class StyleTriggerEngine {
public:
    using SoundActionCallback = std::function<void(
        UIElement* sourceElement,
        std::string_view soundUri,
        float volume)>;

    static constexpr std::size_t kMaxVisualStateGroupsPerElement = 4u;
    static constexpr std::size_t kMaxEventTriggersPerElement = 16u;

    explicit StyleTriggerEngine(
        AnimationEngine* animationEngine = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr);
    ~StyleTriggerEngine() noexcept;

    void MarkValuesInPools() const noexcept;

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

    void SetAotExecutionContext(rs_aot::ExecutionContext* aotContext) noexcept {
        aotContext_ = aotContext;
    }
    [[nodiscard]] ExecutionDomainContext* GetDomains() const noexcept { return domains_; }
    [[nodiscard]] const Interpreter* GetUiInterpreter() const noexcept { return uiInterpreter_; }
    [[nodiscard]] rs_aot::ExecutionContext* GetAotExecutionContext() const noexcept { return aotContext_; }

    void SetSoundActionCallback(SoundActionCallback callback) {
        soundActionCallback_ = std::move(callback);
    }
    [[nodiscard]] const SoundActionCallback& GetSoundActionCallback() const noexcept {
        return soundActionCallback_;
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
    void OnElementControlTemplateChanging(UIElement* host, UIElement* oldTemplateRoot) noexcept;
    void OnElementControlTemplateCleared(UIElement* host);
    void OnElementControlTemplateApplied(UIElement* host);

    // Imperative / script VisualStateManager transition (e.g. GoToState(card, "Expanded", true))
    bool GoToVisualState(
        UIElement* element,
        std::string_view stateName,
        bool useTransitions = true);

    // Evaluate all bound Property/Data/Event/Interaction triggers & automatic CommonStates/FocusStates/CheckStates
    std::size_t EvaluateAndApply();
    std::size_t EvaluateSubtree(UIElement* subtreeRoot, bool resetEventStates = false);

    [[nodiscard]] std::size_t BoundElementCount() const noexcept { return instances_.size(); }

private:
    struct PropertySnapshotSlot {
        IntrusivePtr<UIElement> resolvedTarget{};
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
        IntrusivePtr<UIElement> cachedSource{};
        std::uint32_t lastClickGen = 0u;
        std::int64_t prevSelectedIndex = -1;
        float prevNumericValue = 0.0f;
        bool prevHovered = false;
        bool prevChecked = false;
        bool initialized = false;
    };

    struct StyledElementInstance {
        IntrusivePtr<UIElement> element{};
        AxbStyleDefinition baseFlattenedStyle{};
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
    void ExecuteInteractionActions(
        StyledElementInstance& inst,
        std::vector<AxbInteractionAction>& actions);
    void ExecuteSingleInteractionAction(
        StyledElementInstance& inst,
        AxbInteractionAction& action);
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
    rs_aot::ExecutionContext* aotContext_ = nullptr;
    SoundActionCallback soundActionCallback_{};
    std::unordered_map<std::string, AxbStyleDefinition> keyedStyles_;
    std::unordered_map<std::string, AxbStyleDefinition> implicitTypeStyles_;
    std::unordered_map<std::string, AxbStoryboard> keyedStoryboards_;
    std::vector<StyledElementInstance> instances_;
    bool inAttachStyle_ = false;
};

} // namespace aero
