#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "aero/visual/VisualTree.hpp"

namespace aero {

enum class VisualAnimProp : std::uint8_t {
    Opacity = 0,
    Width = 1,
    Height = 2,
    TranslateX = 3,
    TranslateY = 4,
    ScaleX = 5,
    ScaleY = 6,
    RotateDeg = 7,
    SkewX = 8,
    SkewY = 9,
    Rot3DX = 10,
    Rot3DY = 11,
    Rot3DZ = 12,
    Trans3DX = 13,
    Trans3DY = 14,
    Trans3DZ = 15,
    BgColor = 16,
    FgColor = 17,
    BorderColor = 18,
    CornerRadius = 19,
    PathTrimStart = 20,
    PathTrimEnd = 21,
    EffectRadius = 22,
    CustomScalar0 = 23,
    CustomScalar1 = 24,
    CustomScalar2 = 25,
    CustomScalar3 = 26,
};

enum class EasingFunctionKind : std::uint8_t {
    Linear = 0,
    Quadratic = 1,
    Cubic = 2,
    Quartic = 3,
    Quintic = 4,
    Sine = 5,
    Circle = 6,
    Exponential = 7,
    Back = 8,
    Bounce = 9,
    Elastic = 10,
    Spline = 11,
};

enum class EasingMode : std::uint8_t {
    EaseOut = 0,
    EaseIn = 1,
    EaseInOut = 2,
};

struct VisualSpringConfig {
    float stiffness = 320.0f;
    float damping = 28.0f;
    float mass = 1.0f;
    float restPositionEpsilon = 0.0005f;
    float restVelocityEpsilon = 0.005f;
};

struct VisualKeyFrame {
    float timeSec = 0.0f;
    float scalarValue = 0.0f;
    std::uint32_t colorRgba = 0u;
    EasingFunctionKind easingKind = EasingFunctionKind::Linear;
    EasingMode easingMode = EasingMode::EaseOut;
    bool discrete = false;
};

enum class VisualAnimTrackKind : std::uint8_t {
    Inactive = 0,
    FloatTween = 1,
    ColorTween = 2,
    FloatSpring = 3,
    KeyFrames = 4,
};

struct VisualAnimTrack {
    std::int32_t trackId = 0;
    VisualNodeId targetNode{};
    VisualAnimProp prop = VisualAnimProp::Opacity;
    VisualAnimTrackKind kind = VisualAnimTrackKind::Inactive;

    float fromVal = 0.0f;
    float toVal = 0.0f;
    float currentVal = 0.0f;
    float velocity = 0.0f;

    std::uint32_t fromRgba = 0u;
    std::uint32_t toRgba = 0u;

    float durationSec = 0.0f;
    float delaySec = 0.0f;
    float elapsedSec = 0.0f;
    EasingFunctionKind easeKind = EasingFunctionKind::Linear;
    EasingMode easeMode = EasingMode::EaseOut;
    bool autoReverse = false;
    std::int32_t repeatCount = 1;
    std::int32_t completedCycles = 0;
    bool forwardDirection = true;

    VisualSpringConfig spring{};
    std::vector<VisualKeyFrame> keyFrames;
};

[[nodiscard]] float EvaluateEasing(
    EasingFunctionKind kind,
    EasingMode mode,
    float normalizedTime01) noexcept;

class VisualAnimationEngine final {
public:
    VisualAnimationEngine() = default;
    ~VisualAnimationEngine() = default;

    void SetWakeHostCallback(std::function<void()> callback) { wakeHostCallback_ = std::move(callback); }

    std::int32_t StartFloat(
        VisualNodeId node,
        VisualAnimProp prop,
        float fromVal,
        float toVal,
        float durationSec,
        EasingFunctionKind easeKind = EasingFunctionKind::Cubic,
        EasingMode easeMode = EasingMode::EaseOut,
        float delaySec = 0.0f,
        bool autoReverse = false,
        std::int32_t repeatCount = 1);

    std::int32_t StartColor(
        VisualNodeId node,
        VisualAnimProp prop,
        std::uint32_t fromRgba,
        std::uint32_t toRgba,
        float durationSec,
        EasingFunctionKind easeKind = EasingFunctionKind::Cubic,
        EasingMode easeMode = EasingMode::EaseOut);

    std::int32_t SpringFloat(
        VisualNodeId node,
        VisualAnimProp prop,
        float currentVal,
        float targetVal,
        float stiffness,
        float damping,
        float mass = 1.0f);

    std::int32_t StartKeyFrames(
        VisualNodeId node,
        VisualAnimProp prop,
        std::vector<VisualKeyFrame> keyFrames,
        bool autoReverse = false,
        std::int32_t repeatCount = 1);

    void StopAll(VisualNodeId node) noexcept;
    void StopTrack(std::int32_t trackId) noexcept;
    void Clear() noexcept;

    bool Tick(float dtSeconds, VisualWorld& world);
    [[nodiscard]] bool HasActiveAnimations() const noexcept;
    [[nodiscard]] std::size_t ActiveTrackCount() const noexcept;

private:
    std::vector<VisualAnimTrack> tracks_;
    std::int32_t nextTrackId_ = 1;
    std::function<void()> wakeHostCallback_;
};

} // namespace aero
