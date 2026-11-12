#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

struct SpringConfig {
    float stiffness = 320.0f;
    float damping = 28.0f;
    float mass = 1.0f;
    float restPositionEpsilon = 0.0005f;
    float restVelocityEpsilon = 0.005f;
};

// Analytical 32-bit float evaluation for all 11 standard easing curves x 3 EasingModes
[[nodiscard]] float EvaluateEasing(
    EasingFunctionKind kind,
    EasingMode mode,
    float normalizedTime01) noexcept;

// 4-iteration 32-bit float Newton-Raphson + bisection fallback 1D Cubic Bezier solver
// (supports Q7.8 overshoot tangents y1, y2 outside [0, 1])
[[nodiscard]] float EvaluateCubicBezier1D(
    float x1,
    float y1,
    float x2,
    float y2,
    float normalizedTime01) noexcept;

enum class AnimationTrackKind : std::uint8_t {
    Inactive = 0,
    TweenFloat = 1,
    TweenFloat4 = 2,
    KeyFrames = 3,
    SpringFloat = 4,
    SpringFloat4 = 5,
    CompiledClipSlice = 6,
};

struct TimelineEventInvocation {
    UIElement* rootControl = nullptr;
    std::string_view eventName{};
    SymbolId handlerSymbolId = 0;
    float timeSec = 0.0f;
    float floatPayload = 0.0f;
    std::int32_t intPayload = 0;
};

// Zero-allocation fixed-size inline keyframe entry (avoids per-track std::vector heap allocations)
struct InlineTrackKeyFrame {
    float timeSeconds = 0.0f;
    UnifiedValue value{};
    EasingFunctionKind easingKind = EasingFunctionKind::Linear;
    EasingMode easingMode = EasingMode::EaseOut;
    bool isDiscrete = false;
};

// Zero-allocation AnimationEngine backed by CompactPropertyEntry::animationTrackId & MotionStateMachineBank
class AnimationEngine {
public:
    static constexpr std::uint16_t InvalidTrackId = 0xFFFFu;
    static constexpr std::size_t kMaxInlineKeyFrames = 8u;

    explicit AnimationEngine(std::size_t initialReservedTracks = 64u);
    ~AnimationEngine() noexcept;

    [[nodiscard]] static bool IsLiveInstance(const AnimationEngine* engine) noexcept;
    void Clear();
    void MarkValuesInPools() const noexcept;
    void SetWakeHostCallback(std::function<void()> callback) { wakeHostCallback_ = std::move(callback); }
    void SetTimelineEventCallback(std::function<void(const TimelineEventInvocation&)> callback) {
        timelineEventCallback_ = std::move(callback);
    }

    // Start or retarget a scalar float tween (Opacity, Width, AnimScaleX, AnimRotateAngle, etc.)
    std::uint16_t StartFloatTween(
        UIElement* target,
        std::uint16_t propId,
        float fromValue,
        float toValue,
        float durationSeconds,
        EasingFunctionKind easingKind = EasingFunctionKind::Cubic,
        EasingMode easingMode = EasingMode::EaseOut,
        FillBehavior fillBehavior = FillBehavior::HoldEnd,
        float beginTimeSeconds = 0.0f,
        bool autoReverse = false,
        std::int32_t repeatCount = 1);

    // Start or retarget a 4-channel Float4 tween (Color Background/Foreground/BorderBrush, Thickness, CornerRadius)
    std::uint16_t StartFloat4Tween(
        UIElement* target,
        std::uint16_t propId,
        Float4Value fromValue,
        Float4Value toValue,
        float durationSeconds,
        EasingFunctionKind easingKind = EasingFunctionKind::Cubic,
        EasingMode easingMode = EasingMode::EaseOut,
        FillBehavior fillBehavior = FillBehavior::HoldEnd,
        float beginTimeSeconds = 0.0f,
        bool autoReverse = false,
        std::int32_t repeatCount = 1);

    std::uint16_t StartColorTween(
        UIElement* target,
        std::uint16_t propId,
        const Color& fromColor,
        const Color& toColor,
        float durationSeconds,
        EasingFunctionKind easingKind = EasingFunctionKind::Cubic,
        EasingMode easingMode = EasingMode::EaseOut,
        FillBehavior fillBehavior = FillBehavior::HoldEnd) {
        return StartFloat4Tween(
            target,
            propId,
            fromColor.ToFloat4(),
            toColor.ToFloat4(),
            durationSeconds,
            easingKind,
            easingMode,
            fillBehavior);
    }

    // Start or retarget a C1 velocity-continuous second-order spring on a scalar float property
    std::uint16_t RetargetSpringFloat(
        UIElement* target,
        std::uint16_t propId,
        float targetValue,
        const SpringConfig& spring = SpringConfig{});

    // Start or retarget a 4-channel spring (e.g. smooth interruptible Color or Thickness transitions)
    std::uint16_t RetargetSpringFloat4(
        UIElement* target,
        std::uint16_t propId,
        Float4Value targetValue,
        const SpringConfig& spring = SpringConfig{});

    // Start a zero-allocation transition from target's current property value to toValue
    std::uint16_t StartValueTransition(
        UIElement* target,
        std::uint16_t propId,
        const UnifiedValue& toValue,
        float durationSeconds,
        EasingFunctionKind easingKind = EasingFunctionKind::Cubic,
        EasingMode easingMode = EasingMode::EaseOut);

    // Start, stop, or toggle play/pause of a compiled AxbAnimationTimeline or full AxbStoryboard against scopeRoot
    std::uint16_t StartTimeline(UIElement* scopeRoot, const AxbAnimationTimeline& timeline);
    std::size_t BeginStoryboard(UIElement* scopeRoot, const AxbStoryboard& storyboard);
    std::size_t StopStoryboard(UIElement* scopeRoot, const AxbStoryboard& storyboard, bool restoreBaseValue = true) noexcept;
    std::size_t TogglePlayPauseStoryboard(UIElement* scopeRoot, const AxbStoryboard& storyboard);
    [[nodiscard]] bool IsStoryboardActive(UIElement* scopeRoot, const AxbStoryboard& storyboard) const noexcept;
    [[nodiscard]] bool IsStoryboardKeyActive(UIElement* scopeRoot, std::string_view storyboardKey, const AxbStoryboard& storyboard) const noexcept;
    [[nodiscard]] bool IsStoryboardPaused(UIElement* scopeRoot, const AxbStoryboard& storyboard) const noexcept;
    bool StopPropertyAnimation(UIElement* target, std::uint16_t propId, bool restoreBaseValue = true) noexcept;
    std::size_t StopSubtreeAnimations(const UIElement* subtreeRoot, bool restoreBaseValue = false) noexcept;

    // Start a zero-copy flat CompiledClipSlice track pointing directly into AxbPackage::motionKeyframes
    std::uint16_t StartCompiledClipTrack(
        UIElement* target,
        const AxbMotionTrackDesc& trackDesc,
        const AxbFlatKeyframe* keyframePool,
        std::size_t poolCount,
        float durationSeconds,
        bool isLooping = false,
        bool autoReverse = false,
        FillBehavior fillBehavior = FillBehavior::HoldEnd);

    // =========================================================================
    // Phase 6: Multi-Layer Motion State Machine & ContractInputBank API
    // =========================================================================
    bool InstantiatePackageMotion(
        UIElement* rootControl,
        UIElement* const* localElements,
        std::size_t localElementCount,
        const AxbPackage& package);

    bool SetStateMachineInputFloat(UIElement* rootControl, std::uint16_t slotIdx, float value) noexcept;
    bool SetStateMachineInputFloatByName(UIElement* rootControl, std::string_view inputName, float value) noexcept;
    bool SetStateMachineInputBool(UIElement* rootControl, std::uint16_t slotIdx, bool value) noexcept {
        return SetStateMachineInputFloat(rootControl, slotIdx, value ? 1.0f : 0.0f);
    }
    bool SetStateMachineInputBoolByName(UIElement* rootControl, std::string_view inputName, bool value) noexcept {
        return SetStateMachineInputFloatByName(rootControl, inputName, value ? 1.0f : 0.0f);
    }
    bool FireStateMachineTrigger(UIElement* rootControl, std::uint16_t slotIdx) noexcept;
    bool FireStateMachineTriggerByName(UIElement* rootControl, std::string_view triggerName) noexcept;
    [[nodiscard]] float GetStateMachineInputFloat(const UIElement* rootControl, std::uint16_t slotIdx) const noexcept;
    [[nodiscard]] float GetStateMachineInputFloatByName(
        const UIElement* rootControl,
        std::string_view inputName) const noexcept;

    // Built-in pointer/focus listener dispatch (invoked by InputEventRouter during UiPhase_Input)
    bool DispatchPointerListenerEvent(UIElement* hitElement, SmListenerEventKind eventKind) noexcept;

    // UiPhase_MotionStateMachine: evaluate state machine transition conditions, ExitTime guards, and reset Triggers
    std::size_t EvaluateStateMachines(float dtSeconds = 0.0f) noexcept;

    // Virtualized DataTemplate scroll support: sync bridge inputs & snap state machines to steady state (0 allocs)
    void OnSubtreeDataContextRebound(UIElement* subtreeRoot) noexcept;

    // Sub-5ms Stateful Motion Hot Reload: migrate active state machine layer times & ContractInputBank by hash
    std::size_t MigrateStateMachinesOnHotReload(
        UIElement* rootControl,
        UIElement* const* newLocalElements,
        std::size_t newLocalElementCount,
        const AxbPackage& newPackage);

    [[nodiscard]] bool IsStateMachineSettled(const UIElement* rootControl) const noexcept;
    [[nodiscard]] std::size_t StateMachineCount() const noexcept { return stateMachines_.size(); }
    [[nodiscard]] std::size_t ActiveStateMachineCount() const noexcept;

    // Advance all active tracks & unsettled state machines by dt seconds (0 heap allocations in steady state).
    // Returns number of property writes performed during this tick.
    std::size_t Tick(float dtSeconds) noexcept;

    [[nodiscard]] std::size_t ActiveTrackCount() const noexcept { return activeCount_; }
    [[nodiscard]] bool HasActiveAnimations() const noexcept {
        return (activeCount_ > pausedCount_) || ActiveStateMachineCount() > 0u;
    }
    [[nodiscard]] float GetTrackVelocityX(std::uint16_t trackId) const noexcept;
    [[nodiscard]] std::uint16_t FindActiveTrackId(
        const UIElement* target,
        std::uint16_t propId) const noexcept;

private:
    struct AnimationTrack {
        AnimationTrackKind kind = AnimationTrackKind::Inactive;
        std::uint16_t trackId = InvalidTrackId;
        std::uint16_t propId = 0;
        EasingFunctionKind easingKind = EasingFunctionKind::Linear;
        EasingMode easingMode = EasingMode::EaseOut;
        FillBehavior fillBehavior = FillBehavior::HoldEnd;
        bool autoReverse = false;
        bool paused = false;
        bool hadBaseSnapshot = false;
        ValueSourceRank baseRank = ValueSourceRank::Local;
        std::uint8_t keyFrameCount = 0;
        std::uint8_t channelCount = 1;
        std::uint8_t quantizeInlineW = 1;

        IntrusivePtr<UIElement> target{};
        UnifiedValue baseSnapshot{};

        float beginTime = 0.0f;
        float duration = 0.25f;
        float elapsed = 0.0f;
        std::int32_t repeatCount = 1;

        Float4Value fromF4{};
        Float4Value toF4{};
        Float4Value posF4{};
        Float4Value velF4{};
        SpringConfig spring{};

        const AxbFlatKeyframe* sliceFrames = nullptr;
        std::uint32_t sliceCount = 0u;
        std::uint32_t cachedSegmentIdx = 0u;
        std::uint64_t storyboardHash = 0ULL;

        std::array<InlineTrackKeyFrame, kMaxInlineKeyFrames> keyFrames{};
    };

    struct ContractInputRuntimeSlot {
        std::string name;
        std::uint64_t nameHash = 0ULL;
        std::uint8_t valueKind = 0u;
        bool isTrigger = false;
        bool dirtyThisFrame = true;
        std::uint16_t bridgePropId = 0xFFFFu;
        float scalarValue = 0.0f;
        Float4Value float4Value{};
    };

    struct LayerRuntimeState {
        std::uint32_t layerDescIdx = 0u;
        std::uint64_t layerNameHash = 0ULL;
        SmLayerBlendMode blendMode = SmLayerBlendMode::Override;
        float layerWeight = 1.0f;
        std::uint16_t activeStateIdx = 0u;
        std::uint64_t activeStateNameHash = 0ULL;
        float activeTime = 0.0f;
        float prevActiveTime = 0.0f;
        std::uint16_t prevStateIdx = 0xFFFFu;
        float prevStateTime = 0.0f;
        float crossfadeElapsed = 0.0f;
        float crossfadeDuration = 0.0f;
        EasingFunctionKind crossfadeEaseKind = EasingFunctionKind::Cubic;
        EasingMode crossfadeEaseMode = EasingMode::EaseOut;
    };

    struct ChannelBlendSlot {
        std::uint16_t localElementIdx = 0u;
        std::uint16_t propId = 0u;
        std::uint8_t channelCount = 1u;
        std::uint8_t quantizeInlineW = 1u;
        bool hadBaseSnapshot = false;
        bool touchedThisFrame = false;
        ValueSourceRank baseRank = ValueSourceRank::Local;
        UnifiedValue baseSnapshot{};
        Float4Value restValue{};
        Float4Value accumValue{};
    };

    struct MotionStateMachineInstance {
        IntrusivePtr<UIElement> rootControl{};
        std::vector<IntrusivePtr<UIElement>> localElements;
        std::shared_ptr<AxbPackage> motionData;
        std::uint32_t machineDescIdx = 0u;
        std::uint64_t machineNameHash = 0ULL;
        bool isSettled = false;
        bool evaluatedTransitionsThisFrame = false;
        bool needsInitialEval = true;
        std::vector<ContractInputRuntimeSlot> contractInputs;
        std::vector<LayerRuntimeState> layers;
        std::vector<ChannelBlendSlot> channelScratch;
        std::vector<std::uint32_t> trackSegmentCache;
    };

    [[nodiscard]] static float DefaultScalarForProperty(std::uint16_t propId) noexcept;
    [[nodiscard]] static float ReadCurrentScalar(const UIElement* target, std::uint16_t propId) noexcept;
    [[nodiscard]] static Float4Value ReadCurrentFloat4(const UIElement* target, std::uint16_t propId) noexcept;
    [[nodiscard]] static Float4Value EvaluateFlatTrackAtTime(
        const AxbMotionTrackDesc& trackDesc,
        const AxbFlatKeyframe* keyframePool,
        std::size_t poolCount,
        float localTime,
        std::uint32_t& ioCachedSegmentIdx) noexcept;

    void SyncBridgePropertiesToContractInputs(MotionStateMachineInstance& sm) noexcept;
    bool EvaluateStateMachineTransitions(MotionStateMachineInstance& sm) noexcept;
    std::size_t EvaluateAndBlendStateMachineInstance(
        MotionStateMachineInstance& sm,
        float dtSeconds,
        bool snapCrossfades) noexcept;
    [[nodiscard]] bool CheckStateMachineInstanceSettled(const MotionStateMachineInstance& sm) const noexcept;

    std::uint16_t AcquireTrackSlot(
        UIElement* target,
        std::uint16_t propId,
        bool preserveExistingSnapshotAndVelocity,
        Float4Value& outPreservedPos,
        Float4Value& outPreservedVel,
        bool& outWasActiveSpring);
    void ReleaseTrackSlot(AnimationTrack& track, bool restoreSnapshot) noexcept;

    std::vector<AnimationTrack> tracks_;
    std::vector<std::uint16_t> freeTrackIds_;
    std::size_t activeCount_ = 0u;
    std::size_t pausedCount_ = 0u;
    std::vector<MotionStateMachineInstance> stateMachines_;
    std::function<void()> wakeHostCallback_{};
    std::function<void(const TimelineEventInvocation&)> timelineEventCallback_{};
};

} // namespace aero
