#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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

enum class AnimationTrackKind : std::uint8_t {
    Inactive = 0,
    TweenFloat = 1,
    TweenFloat4 = 2,
    KeyFrames = 3,
    SpringFloat = 4,
    SpringFloat4 = 5,
};

// Zero-allocation fixed-size inline keyframe entry (avoids per-track std::vector heap allocations)
struct InlineTrackKeyFrame {
    float timeSeconds = 0.0f;
    UnifiedValue value{};
    EasingFunctionKind easingKind = EasingFunctionKind::Linear;
    EasingMode easingMode = EasingMode::EaseOut;
    bool isDiscrete = false;
};

// Zero-allocation AnimationEngine backed by CompactPropertyEntry::animationTrackId:
// - Contiguous pool of reusable AnimationTrack slots (free-list recycled -> 0 heap allocs in steady state)
// - Tween & multi-keyframe timelines (Float, Color/Thickness/CornerRadius Float4, Discrete Bool/Int)
// - Second-order C1 velocity-continuous Spring physics (interruptible mid-flight retargeting)
// - Automatic animationTrackId linkage & pre-animation base value restoration on FillBehavior::Stop
class AnimationEngine {
public:
    static constexpr std::uint16_t InvalidTrackId = 0xFFFFu;
    static constexpr std::size_t kMaxInlineKeyFrames = 8u;

    explicit AnimationEngine(std::size_t initialReservedTracks = 64u);

    void Clear();

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

    // Start or retarget a C1 velocity-continuous second-order spring on a scalar float property!
    // If an active spring track already exists on (target, propId), retargets in-place preserving current velocity!
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

    // Start a compiled AxbAnimationTimeline or full AxbStoryboard against scopeRoot
    std::uint16_t StartTimeline(UIElement* scopeRoot, const AxbAnimationTimeline& timeline);
    std::size_t BeginStoryboard(UIElement* scopeRoot, const AxbStoryboard& storyboard);
    bool StopPropertyAnimation(UIElement* target, std::uint16_t propId, bool restoreBaseValue = true) noexcept;

    // Advance all active tracks by dt seconds (0 heap allocations in steady state).
    // Returns number of property writes performed during this tick.
    std::size_t Tick(float dtSeconds) noexcept;

    [[nodiscard]] std::size_t ActiveTrackCount() const noexcept { return activeCount_; }
    [[nodiscard]] bool HasActiveAnimations() const noexcept { return activeCount_ > 0u; }
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
        bool hadBaseSnapshot = false;
        ValueSourceRank baseRank = ValueSourceRank::Local;
        std::uint8_t keyFrameCount = 0;

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

        std::array<InlineTrackKeyFrame, kMaxInlineKeyFrames> keyFrames{};
    };

    [[nodiscard]] static float DefaultScalarForProperty(std::uint16_t propId) noexcept;
    [[nodiscard]] static float ReadCurrentScalar(const UIElement* target, std::uint16_t propId) noexcept;
    [[nodiscard]] static Float4Value ReadCurrentFloat4(const UIElement* target, std::uint16_t propId) noexcept;

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
};

} // namespace aero
