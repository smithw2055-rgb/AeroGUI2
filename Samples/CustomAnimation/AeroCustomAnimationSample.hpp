#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "aero/compiler/AxbFormat.hpp"

namespace aero::samples {

namespace detail {

inline constexpr float kTwoPi = 6.28318530717958647692f;

enum class SampleEdgeBehavior : std::uint32_t {
    Unspecified = 0u,
    EaseIn = 1u,
    EaseOut = 2u,
    EaseInOut = 3u
};

[[nodiscard]] inline bool AsciiEqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool TryParseFloatStrict(std::string_view text, float& outVal) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    const std::string buf(text);
    char* endPtr = nullptr;
    const float parsed = std::strtof(buf.c_str(), &endPtr);
    if (endPtr == buf.c_str() || *endPtr != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    outVal = parsed;
    return true;
}

[[nodiscard]] inline bool TryParseEdgeBehaviorAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) noexcept {
    if (!AsciiEqualsIgnoreCase(attrName, "EdgeBehavior")) {
        return false;
    }
    if (AsciiEqualsIgnoreCase(attrValue, "EaseIn")) {
        inOutParams.flags = static_cast<std::uint32_t>(SampleEdgeBehavior::EaseIn);
        return true;
    }
    if (AsciiEqualsIgnoreCase(attrValue, "EaseOut")) {
        inOutParams.flags = static_cast<std::uint32_t>(SampleEdgeBehavior::EaseOut);
        return true;
    }
    if (AsciiEqualsIgnoreCase(attrValue, "EaseInOut")) {
        inOutParams.flags = static_cast<std::uint32_t>(SampleEdgeBehavior::EaseInOut);
        return true;
    }
    return false;
}

[[nodiscard]] inline SampleEdgeBehavior ResolveEdgeBehavior(
    std::uint32_t flags,
    EasingMode fallbackMode) noexcept {
    if (flags == static_cast<std::uint32_t>(SampleEdgeBehavior::EaseIn)) {
        return SampleEdgeBehavior::EaseIn;
    }
    if (flags == static_cast<std::uint32_t>(SampleEdgeBehavior::EaseOut)) {
        return SampleEdgeBehavior::EaseOut;
    }
    if (flags == static_cast<std::uint32_t>(SampleEdgeBehavior::EaseInOut)) {
        return SampleEdgeBehavior::EaseInOut;
    }
    switch (fallbackMode) {
        case EasingMode::EaseIn: return SampleEdgeBehavior::EaseIn;
        case EasingMode::EaseInOut: return SampleEdgeBehavior::EaseInOut;
        case EasingMode::EaseOut:
        default:
            return SampleEdgeBehavior::EaseOut;
    }
}

[[nodiscard]] inline float BounceEaseOutCore(
    float t,
    float start,
    float delta,
    float bounces,
    float bounciness) noexcept {
    const float val = std::fabs(
        std::pow(std::max(0.0f, 1.0f - t), bounciness) * std::cos(kTwoPi * t * bounces));
    return start + delta - val * delta;
}

[[nodiscard]] inline float BounceEaseInCore(
    float t,
    float start,
    float delta,
    float bounces,
    float bounciness) noexcept {
    const float val = std::fabs(
        std::pow(std::max(0.0f, t), bounciness) * std::cos(kTwoPi * t * bounces));
    return start + val * delta;
}

[[nodiscard]] inline float ElasticEaseOutCore(
    float t,
    float start,
    float delta,
    float springiness,
    float oscillations) noexcept {
    const float val =
        std::pow(std::max(0.0f, 1.0f - t), springiness) * std::cos(kTwoPi * t * oscillations);
    return start + delta - val * delta;
}

[[nodiscard]] inline float ElasticEaseInCore(
    float t,
    float start,
    float delta,
    float springiness,
    float oscillations) noexcept {
    const float val =
        std::pow(std::max(0.0f, t), springiness) * std::cos(kTwoPi * t * oscillations);
    return start + val * delta;
}

[[nodiscard]] inline float BackEaseInUnit(
    float t,
    float amplitude,
    float suppression) noexcept {
    constexpr float kBackFrequency = 0.5f;
    return std::pow(std::max(0.0f, t), suppression) -
           t * amplitude * std::sin(kTwoPi * t * kBackFrequency);
}

[[nodiscard]] inline float ExpEaseOutCore(
    float t,
    float start,
    float delta,
    float power) noexcept {
    const float invPower = (std::fabs(power) > 1e-6f) ? (1.0f / power) : 1.0f;
    return std::pow(std::max(0.0f, t), invPower) * delta + start;
}

[[nodiscard]] inline float ExpEaseInCore(
    float t,
    float start,
    float delta,
    float power) noexcept {
    return std::pow(std::max(0.0f, t), power) * delta + start;
}

} // namespace detail

// ============================================================================
// BounceAnimation (params[0] = Bounces = 5, params[1] = Bounciness = 3, default = EaseInOut)
// ============================================================================
inline bool ParseBounceAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    if (detail::TryParseEdgeBehaviorAttr(attrName, attrValue, inOutParams)) {
        return true;
    }
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Bounces") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    if (detail::AsciiEqualsIgnoreCase(attrName, "Bounciness") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[1] = v;
        return true;
    }
    return false;
}

inline float EvaluateBounceAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)defaultOriginValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float delta = endValue - startValue;
    const float bounces = params.params[0];
    const float bounciness = std::max(0.0f, params.params[1]);
    switch (detail::ResolveEdgeBehavior(params.flags, easingMode)) {
        case detail::SampleEdgeBehavior::EaseIn:
            return detail::BounceEaseInCore(t, startValue, delta, bounces, bounciness);
        case detail::SampleEdgeBehavior::EaseOut:
            return detail::BounceEaseOutCore(t, startValue, delta, bounces, bounciness);
        case detail::SampleEdgeBehavior::EaseInOut:
        case detail::SampleEdgeBehavior::Unspecified:
        default:
            if (t <= 0.5f) {
                return detail::BounceEaseInCore(
                    t * 2.0f, startValue, delta * 0.5f, bounces, bounciness);
            }
            return detail::BounceEaseOutCore(
                (t - 0.5f) * 2.0f, startValue + delta * 0.5f, delta * 0.5f, bounces, bounciness);
    }
}

// ============================================================================
// ElasticAnimation (params[0] = Springiness = 3, params[1] = Oscillations = 10, default = EaseIn)
// ============================================================================
inline bool ParseElasticAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    if (detail::TryParseEdgeBehaviorAttr(attrName, attrValue, inOutParams)) {
        return true;
    }
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Springiness") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    if (detail::AsciiEqualsIgnoreCase(attrName, "Oscillations") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[1] = v;
        return true;
    }
    return false;
}

inline float EvaluateElasticAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)defaultOriginValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float delta = endValue - startValue;
    const float springiness = std::max(0.0f, params.params[0]);
    const float oscillations = params.params[1];
    switch (detail::ResolveEdgeBehavior(params.flags, easingMode)) {
        case detail::SampleEdgeBehavior::EaseOut:
            return detail::ElasticEaseOutCore(t, startValue, delta, springiness, oscillations);
        case detail::SampleEdgeBehavior::EaseInOut:
            if (t <= 0.5f) {
                return detail::ElasticEaseInCore(
                    t * 2.0f, startValue, delta * 0.5f, springiness, oscillations);
            }
            return detail::ElasticEaseOutCore(
                (t - 0.5f) * 2.0f,
                startValue + delta * 0.5f,
                delta * 0.5f,
                springiness,
                oscillations);
        case detail::SampleEdgeBehavior::EaseIn:
        case detail::SampleEdgeBehavior::Unspecified:
        default:
            return detail::ElasticEaseInCore(t, startValue, delta, springiness, oscillations);
    }
}

// ============================================================================
// BackAnimation (params[0] = Amplitude = 4, params[1] = Suppression = 2, default = EaseIn)
// ============================================================================
inline bool ParseBackAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    if (detail::TryParseEdgeBehaviorAttr(attrName, attrValue, inOutParams)) {
        return true;
    }
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Amplitude") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    if (detail::AsciiEqualsIgnoreCase(attrName, "Suppression") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[1] = v;
        return true;
    }
    return false;
}

inline float EvaluateBackAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)defaultOriginValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float delta = endValue - startValue;
    const float amplitude = params.params[0];
    const float suppression = std::max(0.0f, params.params[1]);
    switch (detail::ResolveEdgeBehavior(params.flags, easingMode)) {
        case detail::SampleEdgeBehavior::EaseOut:
            return startValue + delta * (1.0f - detail::BackEaseInUnit(1.0f - t, amplitude, suppression));
        case detail::SampleEdgeBehavior::EaseInOut:
            if (t <= 0.5f) {
                return startValue + (delta * 0.5f) * detail::BackEaseInUnit(t * 2.0f, amplitude, suppression);
            }
            return startValue + delta * 0.5f +
                   (delta * 0.5f) *
                       (1.0f - detail::BackEaseInUnit(1.0f - (t - 0.5f) * 2.0f, amplitude, suppression));
        case detail::SampleEdgeBehavior::EaseIn:
        case detail::SampleEdgeBehavior::Unspecified:
        default:
            return startValue + delta * detail::BackEaseInUnit(t, amplitude, suppression);
    }
}

// ============================================================================
// ExponentialAnimation (params[0] = Power = 2, default = EaseIn)
// ============================================================================
inline bool ParseExponentialAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    if (detail::TryParseEdgeBehaviorAttr(attrName, attrValue, inOutParams)) {
        return true;
    }
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Power") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    return false;
}

inline float EvaluateExponentialAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)defaultOriginValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float delta = endValue - startValue;
    const float power = params.params[0];
    switch (detail::ResolveEdgeBehavior(params.flags, easingMode)) {
        case detail::SampleEdgeBehavior::EaseOut:
            return detail::ExpEaseOutCore(t, startValue, delta, power);
        case detail::SampleEdgeBehavior::EaseInOut:
            if (t <= 0.5f) {
                return detail::ExpEaseOutCore(t * 2.0f, startValue, delta * 0.5f, power);
            }
            return detail::ExpEaseInCore((t - 0.5f) * 2.0f, startValue, delta * 0.5f, power) +
                   delta * 0.5f;
        case detail::SampleEdgeBehavior::EaseIn:
        case detail::SampleEdgeBehavior::Unspecified:
        default:
            return detail::ExpEaseInCore(t, startValue, delta, power);
    }
}

// ============================================================================
// CircleAnimation (params[0] = Radius = 10, flags = 0 (XDirection) | 1 (YDirection))
// ============================================================================
inline bool ParseCircleAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    if (detail::AsciiEqualsIgnoreCase(attrName, "Direction")) {
        if (detail::AsciiEqualsIgnoreCase(attrValue, "XDirection") ||
            detail::AsciiEqualsIgnoreCase(attrValue, "X")) {
            inOutParams.flags = 0u;
            return true;
        }
        if (detail::AsciiEqualsIgnoreCase(attrValue, "YDirection") ||
            detail::AsciiEqualsIgnoreCase(attrValue, "Y")) {
            inOutParams.flags = 1u;
            return true;
        }
        return false;
    }
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Radius") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    return false;
}

inline float EvaluateCircleAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)easingMode;
    (void)startValue;
    (void)endValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float radius = params.params[0];
    const float offset = (params.flags == 0u)
        ? (1.0f - std::cos(detail::kTwoPi * t))
        : (-std::sin(detail::kTwoPi * t));
    return offset * radius + defaultOriginValue;
}

// ============================================================================
// ShakeAnimation (params[0] = Amplitude = 15, params[1] = Oscillations = 5)
// ============================================================================
inline bool ParseShakeAnimationAttr(
    std::string_view attrName,
    std::string_view attrValue,
    CustomAnimationParams& inOutParams) {
    float v = 0.0f;
    if (detail::AsciiEqualsIgnoreCase(attrName, "Amplitude") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[0] = v;
        return true;
    }
    if (detail::AsciiEqualsIgnoreCase(attrName, "Oscillations") &&
        detail::TryParseFloatStrict(attrValue, v)) {
        inOutParams.params[1] = v;
        return true;
    }
    return false;
}

inline float EvaluateShakeAnimation(
    const CustomAnimationParams& params,
    EasingMode easingMode,
    float normalizedT,
    float startValue,
    float endValue,
    float defaultOriginValue) {
    (void)easingMode;
    (void)endValue;
    (void)defaultOriginValue;
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float amplitude = params.params[0];
    const float oscillations = params.params[1];
    return startValue + std::sin(detail::kTwoPi * t * oscillations) * amplitude * (1.0f - t);
}

inline void RegisterCustomAnimationSampleTypes() {
    {
        CustomAnimationSpec spec{};
        spec.typeName = "BounceAnimation";
        spec.parseAttr = &ParseBounceAnimationAttr;
        spec.evaluate = &EvaluateBounceAnimation;
        spec.defaultParams.flags = static_cast<std::uint32_t>(detail::SampleEdgeBehavior::EaseInOut);
        spec.defaultParams.params[0] = 5.0f;
        spec.defaultParams.params[1] = 3.0f;
        spec.requiresFromTo = true;
        (void)RegisterCustomAnimationType(spec);
    }
    {
        CustomAnimationSpec spec{};
        spec.typeName = "ElasticAnimation";
        spec.parseAttr = &ParseElasticAnimationAttr;
        spec.evaluate = &EvaluateElasticAnimation;
        spec.defaultParams.flags = static_cast<std::uint32_t>(detail::SampleEdgeBehavior::EaseIn);
        spec.defaultParams.params[0] = 3.0f;
        spec.defaultParams.params[1] = 10.0f;
        spec.requiresFromTo = true;
        (void)RegisterCustomAnimationType(spec);
    }
    {
        CustomAnimationSpec spec{};
        spec.typeName = "BackAnimation";
        spec.parseAttr = &ParseBackAnimationAttr;
        spec.evaluate = &EvaluateBackAnimation;
        spec.defaultParams.flags = static_cast<std::uint32_t>(detail::SampleEdgeBehavior::EaseIn);
        spec.defaultParams.params[0] = 4.0f;
        spec.defaultParams.params[1] = 2.0f;
        spec.requiresFromTo = true;
        (void)RegisterCustomAnimationType(spec);
    }
    {
        CustomAnimationSpec spec{};
        spec.typeName = "ExponentialAnimation";
        spec.parseAttr = &ParseExponentialAnimationAttr;
        spec.evaluate = &EvaluateExponentialAnimation;
        spec.defaultParams.flags = static_cast<std::uint32_t>(detail::SampleEdgeBehavior::EaseIn);
        spec.defaultParams.params[0] = 2.0f;
        spec.requiresFromTo = true;
        (void)RegisterCustomAnimationType(spec);
    }
    {
        CustomAnimationSpec spec{};
        spec.typeName = "CircleAnimation";
        spec.parseAttr = &ParseCircleAnimationAttr;
        spec.evaluate = &EvaluateCircleAnimation;
        spec.defaultParams.flags = 0u;
        spec.defaultParams.params[0] = 10.0f;
        spec.requiresFromTo = false;
        (void)RegisterCustomAnimationType(spec);
    }
    {
        CustomAnimationSpec spec{};
        spec.typeName = "ShakeAnimation";
        spec.parseAttr = &ParseShakeAnimationAttr;
        spec.evaluate = &EvaluateShakeAnimation;
        spec.defaultParams.flags = 0u;
        spec.defaultParams.params[0] = 15.0f;
        spec.defaultParams.params[1] = 5.0f;
        spec.requiresFromTo = false;
        (void)RegisterCustomAnimationType(spec);
    }
}

} // namespace aero::samples

namespace aero {
using samples::RegisterCustomAnimationSampleTypes;
} // namespace aero
