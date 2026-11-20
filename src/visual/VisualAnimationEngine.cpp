#include "aero/visual/VisualAnimationEngine.hpp"

#include <algorithm>
#include <cmath>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/UIResourceStore.hpp"

namespace aero {

namespace {

[[nodiscard]] const VisualNode* FindFirstDescendantTextNode(
    const VisualWorld& world,
    VisualNodeId id,
    int depth) noexcept {
    if (depth > 8) return nullptr;
    const VisualNode* node = world.GetNode(id);
    if (!node) return nullptr;
    if (node->kind == VisualKind::Text) return node;
    for (VisualNodeId ch : node->children) {
        if (const VisualNode* hit = FindFirstDescendantTextNode(world, ch, depth + 1)) {
            return hit;
        }
    }
    return nullptr;
}

void ApplyTextFgOpacityRecursive(
    VisualWorld& world,
    VisualNodeId id,
    float opacity,
    int depth) noexcept {
    if (depth > 8) return;
    VisualNode* node = world.GetNode(id);
    if (!node) return;
    if (node->kind == VisualKind::Text) {
        node->EnsureExtras().text.fgOpacity = opacity;
        world.MarkDirty(id, VisualDirty_Visual);
        return;
    }
    const std::vector<VisualNodeId> kids = node->children;
    for (VisualNodeId ch : kids) {
        ApplyTextFgOpacityRecursive(world, ch, opacity, depth + 1);
    }
}

void ApplyTextFgColorRecursive(
    VisualWorld& world,
    VisualNodeId id,
    std::uint32_t rgba,
    int depth) noexcept {
    if (depth > 8) return;
    VisualNode* node = world.GetNode(id);
    if (!node) return;
    if (node->kind == VisualKind::Text) {
        node->EnsureExtras().text.fgRgba = rgba;
        world.MarkDirty(id, VisualDirty_Visual);
        return;
    }
    const std::vector<VisualNodeId> kids = node->children;
    for (VisualNodeId ch : kids) {
        ApplyTextFgColorRecursive(world, ch, rgba, depth + 1);
    }
}

[[nodiscard]] float EaseOutBounceCore(float u) noexcept {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if (u < 1.0f / d1) {
        return n1 * u * u;
    }
    if (u < 2.0f / d1) {
        const float x = u - 1.5f / d1;
        return n1 * x * x + 0.75f;
    }
    if (u < 2.5f / d1) {
        const float x = u - 2.25f / d1;
        return n1 * x * x + 0.9375f;
    }
    const float x = u - 2.625f / d1;
    return n1 * x * x + 0.984375f;
}

[[nodiscard]] float EaseInCore(EasingFunctionKind kind, float t) noexcept {
    switch (kind) {
    case EasingFunctionKind::Linear: return t;
    case EasingFunctionKind::Quadratic: return t * t;
    case EasingFunctionKind::Cubic: return t * t * t;
    case EasingFunctionKind::Quartic: return t * t * t * t;
    case EasingFunctionKind::Quintic: return t * t * t * t * t;
    case EasingFunctionKind::Sine: return 1.0f - std::cos(t * 1.57079632679f);
    case EasingFunctionKind::Circle: return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
    case EasingFunctionKind::Exponential: return (t <= 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
    case EasingFunctionKind::Back: {
        constexpr float s = 1.70158f;
        return t * t * ((s + 1.0f) * t - s);
    }
    case EasingFunctionKind::Elastic: {
        if (t <= 0.0f || t >= 1.0f) return t;
        constexpr float p = 0.3f;
        return -std::pow(2.0f, 10.0f * (t - 1.0f)) * std::sin((t - 1.0f - p * 0.25f) * (6.28318530718f / p));
    }
    case EasingFunctionKind::Bounce:
        return 1.0f - EaseOutBounceCore(1.0f - t);
    case EasingFunctionKind::Spline:
        return t * t * (3.0f - 2.0f * t);
    }
    return t;
}

struct GradientStopTarget {
    bool isStopColor = false;
    bool isStopOffset = false;
    bool isStroke = false;
    std::size_t stopIndex = 0u;
};

[[nodiscard]] GradientStopTarget ParseGradientStopTarget(VisualAnimProp prop) noexcept {
    const auto v = static_cast<std::uint8_t>(prop);
    if (v >= static_cast<std::uint8_t>(VisualAnimProp::FillStop0Color) &&
        v <= static_cast<std::uint8_t>(VisualAnimProp::FillStop3Color)) {
        return {true, false, false, static_cast<std::size_t>(v - static_cast<std::uint8_t>(VisualAnimProp::FillStop0Color))};
    }
    if (v >= static_cast<std::uint8_t>(VisualAnimProp::FillStop0Offset) &&
        v <= static_cast<std::uint8_t>(VisualAnimProp::FillStop3Offset)) {
        return {false, true, false, static_cast<std::size_t>(v - static_cast<std::uint8_t>(VisualAnimProp::FillStop0Offset))};
    }
    if (v >= static_cast<std::uint8_t>(VisualAnimProp::StrokeStop0Color) &&
        v <= static_cast<std::uint8_t>(VisualAnimProp::StrokeStop3Color)) {
        return {true, false, true, static_cast<std::size_t>(v - static_cast<std::uint8_t>(VisualAnimProp::StrokeStop0Color))};
    }
    if (v >= static_cast<std::uint8_t>(VisualAnimProp::StrokeStop0Offset) &&
        v <= static_cast<std::uint8_t>(VisualAnimProp::StrokeStop3Offset)) {
        return {false, true, true, static_cast<std::size_t>(v - static_cast<std::uint8_t>(VisualAnimProp::StrokeStop0Offset))};
    }
    return {};
}

[[nodiscard]] Color UnpackRgba(std::uint32_t rgba) noexcept {
    constexpr float kInv255 = 1.0f / 255.0f;
    return Color{
        static_cast<float>(rgba & 0xFFu) * kInv255,
        static_cast<float>((rgba >> 8u) & 0xFFu) * kInv255,
        static_cast<float>((rgba >> 16u) & 0xFFu) * kInv255,
        static_cast<float>((rgba >> 24u) & 0xFFu) * kInv255};
}

[[nodiscard]] std::uint32_t PackColor(const Color& c) noexcept {
    const auto r = static_cast<std::uint32_t>(std::clamp(c.r * 255.0f + 0.5f, 0.0f, 255.0f)) & 0xFFu;
    const auto g = static_cast<std::uint32_t>(std::clamp(c.g * 255.0f + 0.5f, 0.0f, 255.0f)) & 0xFFu;
    const auto b = static_cast<std::uint32_t>(std::clamp(c.b * 255.0f + 0.5f, 0.0f, 255.0f)) & 0xFFu;
    const auto a = static_cast<std::uint32_t>(std::clamp(c.a * 255.0f + 0.5f, 0.0f, 255.0f)) & 0xFFu;
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

struct MutableGradSlot {
    std::int32_t* gradId = nullptr;
    bool* ownsMutable = nullptr;
};

[[nodiscard]] MutableGradSlot ResolveGradSlot(VisualNode* node, bool isStroke) noexcept {
    if (!node) return {};
    if (node->kind == VisualKind::Vector) {
        return isStroke
            ? MutableGradSlot{&node->EnsureExtras().vector.strokeGradId, &node->EnsureExtras().vector.ownsMutableStrokeGrad}
            : MutableGradSlot{&node->EnsureExtras().vector.fillGradId, &node->EnsureExtras().vector.ownsMutableFillGrad};
    }
    return isStroke
        ? MutableGradSlot{&node->box.borderGradId, &node->box.ownsMutableBorderGrad}
        : MutableGradSlot{&node->box.bgGradId, &node->box.ownsMutableBgGrad};
}

[[nodiscard]] std::int32_t ReadGradId(const VisualNode* node, bool isStroke) noexcept {
    if (!node) return -1;
    if (node->kind == VisualKind::Vector) {
        return isStroke ? node->Vector().strokeGradId : node->Vector().fillGradId;
    }
    return isStroke ? node->box.borderGradId : node->box.bgGradId;
}

[[nodiscard]] GradientBrushRegistry* ResolveGradRegistryForBrush(
    VisualWorld& world,
    std::uint32_t brushId) noexcept {
    if (UIResourceStore* store = world.ResourceStore();
        store != nullptr && store->GradientBrushes().Find(brushId) != nullptr) {
        return &store->GradientBrushes();
    }
    if (GradientBrushRegistry::Instance().Find(brushId) != nullptr) {
        return &GradientBrushRegistry::Instance();
    }
    return nullptr;
}

[[nodiscard]] bool TryReadGradientBrush(
    const VisualWorld& world,
    std::int32_t gradId,
    GradientBrushDesc& outDesc) noexcept {
    if (gradId <= 0) return false;
    const auto brushId = static_cast<std::uint32_t>(gradId);
    const UIResourceStore* store = world.ResourceStore();
    return (store != nullptr && store->GradientBrushes().TryGetBrush(brushId, outDesc)) ||
           GradientBrushRegistry::Instance().TryGetBrush(brushId, outDesc);
}

[[nodiscard]] GradientBrushRegistry* EnsureMutableNodeGradient(
    VisualWorld& world,
    VisualNode* node,
    bool isStroke,
    std::uint32_t& outBrushId) noexcept {
    outBrushId = 0u;
    const MutableGradSlot slot = ResolveGradSlot(node, isStroke);
    if (!slot.gradId || *slot.gradId <= 0) return nullptr;
    GradientBrushRegistry* reg = ResolveGradRegistryForBrush(world, static_cast<std::uint32_t>(*slot.gradId));
    if (!reg) return nullptr;
    if (slot.ownsMutable && !*slot.ownsMutable) {
        const std::uint32_t cloned = reg->CloneMutableBrush(static_cast<std::uint32_t>(*slot.gradId));
        if (cloned == 0u) return nullptr;
        *slot.gradId = static_cast<std::int32_t>(cloned);
        *slot.ownsMutable = true;
    }
    outBrushId = static_cast<std::uint32_t>(*slot.gradId);
    return reg;
}

[[nodiscard]] bool IsColorProp(VisualAnimProp prop) noexcept {
    const auto target = ParseGradientStopTarget(prop);
    if (target.isStopColor) return true;
    return prop == VisualAnimProp::BgColor ||
           prop == VisualAnimProp::FgColor ||
           prop == VisualAnimProp::BorderColor ||
           prop == VisualAnimProp::FillColor ||
           prop == VisualAnimProp::StrokeColor;
}

[[nodiscard]] VisualDirtyFlags DirtyFlagsForScalarProp(VisualAnimProp prop) noexcept {
    switch (prop) {
    case VisualAnimProp::Width:
    case VisualAnimProp::Height:
        return static_cast<VisualDirtyFlags>(VisualDirty_Layout | VisualDirty_Visual);
    case VisualAnimProp::TranslateX:
    case VisualAnimProp::TranslateY:
    case VisualAnimProp::ScaleX:
    case VisualAnimProp::ScaleY:
    case VisualAnimProp::RotateDeg:
    case VisualAnimProp::SkewX:
    case VisualAnimProp::SkewY:
    case VisualAnimProp::Rot3DX:
    case VisualAnimProp::Rot3DY:
    case VisualAnimProp::Rot3DZ:
    case VisualAnimProp::Trans3DX:
    case VisualAnimProp::Trans3DY:
    case VisualAnimProp::Trans3DZ:
        return static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual);
    default:
        return VisualDirty_Visual;
    }
}

void ApplyScalarProp(VisualWorld& world, VisualNodeId id, VisualAnimProp prop, float value) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) return;
    switch (prop) {
    case VisualAnimProp::Opacity:
        node->layout.opacity = std::clamp(value, 0.0f, 1.0f);
        break;
    case VisualAnimProp::Width:
        node->layout.width = value;
        world.MarkDirty(id, VisualDirty_Layout);
        return;
    case VisualAnimProp::Height:
        node->layout.height = value;
        world.MarkDirty(id, VisualDirty_Layout);
        return;
    case VisualAnimProp::TranslateX:
        node->transform2D.tx = value;
        break;
    case VisualAnimProp::TranslateY:
        node->transform2D.ty = value;
        break;
    case VisualAnimProp::ScaleX:
        node->transform2D.sx = value;
        break;
    case VisualAnimProp::ScaleY:
        node->transform2D.sy = value;
        break;
    case VisualAnimProp::RotateDeg:
        node->transform2D.rotDeg = value;
        break;
    case VisualAnimProp::SkewX:
        node->transform2D.skewX = value;
        break;
    case VisualAnimProp::SkewY:
        node->transform2D.skewY = value;
        break;
    case VisualAnimProp::Rot3DX:
        node->EnsureExtras().transform3D.rx = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DY:
        node->EnsureExtras().transform3D.ry = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DZ:
        node->EnsureExtras().transform3D.rz = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DX:
        node->EnsureExtras().transform3D.tx = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DY:
        node->EnsureExtras().transform3D.ty = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DZ:
        node->EnsureExtras().transform3D.tz = value;
        node->EnsureExtras().transform3D.enabled = true;
        break;
    case VisualAnimProp::CornerRadius:
        node->box.cornerRadius = CornerRadius{value};
        break;
    case VisualAnimProp::PathTrimStart:
        node->EnsureExtras().vector.trimStart = value;
        break;
    case VisualAnimProp::PathTrimEnd:
        node->EnsureExtras().vector.trimEnd = value;
        break;
    case VisualAnimProp::EffectRadius:
        node->EnsureExtras().effect.radius = value;
        break;
    case VisualAnimProp::EffectOpacity:
        node->EnsureExtras().effect.opacity = std::clamp(value, 0.0f, 1.0f);
        break;
    case VisualAnimProp::BrushOpacity:
        if (node->kind == VisualKind::Vector) {
            node->EnsureExtras().vector.fillOpacity = std::clamp(value, 0.0f, 1.0f);
        } else {
            node->box.bgOpacity = std::clamp(value, 0.0f, 1.0f);
        }
        break;
    case VisualAnimProp::FillBrushOpacity:
        if (node->kind == VisualKind::Vector) {
            node->EnsureExtras().vector.fillOpacity = std::clamp(value, 0.0f, 1.0f);
        } else {
            node->box.bgOpacity = std::clamp(value, 0.0f, 1.0f);
        }
        break;
    case VisualAnimProp::StrokeBrushOpacity:
        if (node->kind == VisualKind::Vector) {
            node->EnsureExtras().vector.strokeOpacity = std::clamp(value, 0.0f, 1.0f);
        }
        break;
    case VisualAnimProp::FgBrushOpacity:
        ApplyTextFgOpacityRecursive(world, id, std::clamp(value, 0.0f, 1.0f), 0);
        return;
    case VisualAnimProp::CustomScalar0:
        node->customScalars[0] = value;
        break;
    case VisualAnimProp::CustomScalar1:
        node->customScalars[1] = value;
        break;
    case VisualAnimProp::CustomScalar2:
        node->customScalars[2] = value;
        break;
    case VisualAnimProp::CustomScalar3:
        node->customScalars[3] = value;
        break;
    case VisualAnimProp::OpacityMaskStop0Offset:
    case VisualAnimProp::OpacityMaskStop1Offset:
    case VisualAnimProp::OpacityMaskStop2Offset:
    case VisualAnimProp::OpacityMaskStop3Offset: {
        const std::size_t stopIdx = static_cast<std::size_t>(
            static_cast<std::uint8_t>(prop) - static_cast<std::uint8_t>(VisualAnimProp::OpacityMaskStop0Offset));
        if (stopIdx < node->EnsureExtras().opacityMask.stopOffsets.size()) {
            node->EnsureExtras().opacityMask.stopOffsets[stopIdx] = std::clamp(value, 0.0f, 1.0f);
        }
        break;
    }
    default: {
        const auto target = ParseGradientStopTarget(prop);
        if (target.isStopOffset) {
            std::uint32_t brushId = 0u;
            if (GradientBrushRegistry* reg = EnsureMutableNodeGradient(world, node, target.isStroke, brushId)) {
                (void)reg->UpdateBrushStopOffset(brushId, target.stopIndex, value);
            }
        }
        break;
    }
    }
    world.MarkDirty(id, DirtyFlagsForScalarProp(prop));
}

void ApplyColorProp(VisualWorld& world, VisualNodeId id, VisualAnimProp prop, std::uint32_t rgba) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) return;
    if (const auto target = ParseGradientStopTarget(prop); target.isStopColor) {
        std::uint32_t brushId = 0u;
        if (GradientBrushRegistry* reg = EnsureMutableNodeGradient(world, node, target.isStroke, brushId)) {
            (void)reg->UpdateBrushStopColor(brushId, target.stopIndex, UnpackRgba(rgba));
            world.MarkDirty(id, VisualDirty_Visual);
        }
        return;
    }
    switch (prop) {
    case VisualAnimProp::BgColor:
    case VisualAnimProp::FillColor:
        if (node->kind == VisualKind::Vector) {
            node->EnsureExtras().vector.fillRgba = rgba;
        } else {
            node->box.bgRgba = rgba;
        }
        break;
    case VisualAnimProp::FgColor:
        ApplyTextFgColorRecursive(world, id, rgba, 0);
        return;
    case VisualAnimProp::BorderColor:
    case VisualAnimProp::StrokeColor:
        if (node->kind == VisualKind::Vector) {
            node->EnsureExtras().vector.strokeRgba = rgba;
        } else {
            node->box.borderRgba = rgba;
        }
        break;
    default:
        return;
    }
    world.MarkDirty(id, VisualDirty_Visual);
}

[[nodiscard]] std::uint32_t LerpRgba(std::uint32_t a, std::uint32_t b, float t) noexcept {
    const float u = std::clamp(t, 0.0f, 1.0f);
    std::uint32_t out = 0u;
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        const float c0 = static_cast<float>((a >> shift) & 0xFFu);
        const float c1 = static_cast<float>((b >> shift) & 0xFFu);
        const auto ch = static_cast<std::uint32_t>(c0 + (c1 - c0) * u + 0.5f) & 0xFFu;
        out |= (ch << shift);
    }
    return out;
}

void StepSpringAnalytical(VisualAnimTrack& track, float dtSeconds) noexcept {
    if (dtSeconds <= 0.0f) {
        return;
    }
    const float k = std::max(1.0f, track.spring.stiffness);
    const float c = std::max(0.0f, track.spring.damping);
    const float m = std::max(0.01f, track.spring.mass);

    const float x0 = track.currentVal - track.toVal;
    const float v0 = track.velocity;
    const float omega0 = std::sqrt(k / m);
    const float zeta = c / (2.0f * std::sqrt(k * m));

    float xNext = 0.0f;
    float vNext = 0.0f;
    if (zeta < 1.0f - 1e-4f) {
        const float omegaD = omega0 * std::sqrt(std::max(1e-8f, 1.0f - zeta * zeta));
        const float alpha = zeta * omega0;
        const float decay = std::exp(-alpha * dtSeconds);
        const float A = x0;
        const float B = (v0 + alpha * x0) / omegaD;
        const float cosW = std::cos(omegaD * dtSeconds);
        const float sinW = std::sin(omegaD * dtSeconds);
        xNext = decay * (A * cosW + B * sinW);
        vNext = decay * ((B * omegaD - A * alpha) * cosW - (A * omegaD + B * alpha) * sinW);
    } else if (zeta <= 1.0f + 1e-4f) {
        const float decay = std::exp(-omega0 * dtSeconds);
        const float c1 = x0;
        const float c2 = v0 + omega0 * x0;
        xNext = decay * (c1 + c2 * dtSeconds);
        vNext = decay * (c2 - omega0 * (c1 + c2 * dtSeconds));
    } else {
        const float gamma = omega0 * std::sqrt(std::max(1e-8f, zeta * zeta - 1.0f));
        const float r1 = -omega0 * zeta + gamma;
        const float r2 = -omega0 * zeta - gamma;
        const float denom = r1 - r2;
        const float c1 = (std::fabs(denom) > 1e-6f) ? ((v0 - r2 * x0) / denom) : 0.0f;
        const float c2 = x0 - c1;
        const float e1 = std::exp(r1 * dtSeconds);
        const float e2 = std::exp(r2 * dtSeconds);
        xNext = c1 * e1 + c2 * e2;
        vNext = c1 * r1 * e1 + c2 * r2 * e2;
    }

    track.currentVal = track.toVal + xNext;
    track.velocity = vNext;
}

void EvaluateKeyFramesAtTime(VisualAnimTrack& track, VisualWorld& world, float localTimeSec) noexcept {
    const auto& kfs = track.keyFrames;
    if (kfs.empty()) {
        return;
    }
    const bool colorProp = IsColorProp(track.prop);
    auto applyKf = [&](const VisualKeyFrame& kf) {
        if (colorProp) {
            track.currentRgba = kf.colorRgba;
            ApplyColorProp(world, track.targetNode, track.prop, kf.colorRgba);
        } else {
            track.currentVal = kf.scalarValue;
            ApplyScalarProp(world, track.targetNode, track.prop, kf.scalarValue);
        }
    };

    if (kfs.size() == 1u || localTimeSec <= kfs.front().timeSec) {
        applyKf(kfs.front());
        return;
    }
    if (localTimeSec >= kfs.back().timeSec) {
        applyKf(kfs.back());
        return;
    }

    for (std::size_t idx = 1u; idx < kfs.size(); ++idx) {
        const VisualKeyFrame& kf0 = kfs[idx - 1u];
        const VisualKeyFrame& kf1 = kfs[idx];
        if (localTimeSec <= kf1.timeSec) {
            const float span = kf1.timeSec - kf0.timeSec;
            if (kf1.discrete || span <= 1e-6f) {
                applyKf((localTimeSec >= kf1.timeSec) ? kf1 : kf0);
                return;
            }
            const float u = std::clamp((localTimeSec - kf0.timeSec) / span, 0.0f, 1.0f);
            const float eased = EvaluateEasing(kf1.easingKind, kf1.easingMode, u);
            if (colorProp) {
                const std::uint32_t col = LerpRgba(kf0.colorRgba, kf1.colorRgba, eased);
                track.currentRgba = col;
                ApplyColorProp(world, track.targetNode, track.prop, col);
            } else {
                const float val = kf0.scalarValue + (kf1.scalarValue - kf0.scalarValue) * eased;
                track.currentVal = val;
                ApplyScalarProp(world, track.targetNode, track.prop, val);
            }
            return;
        }
    }
    applyKf(kfs.back());
}

} // namespace

float EvaluateEasing(
    EasingFunctionKind kind,
    EasingMode mode,
    float normalizedTime01) noexcept {
    const float t = std::clamp(normalizedTime01, 0.0f, 1.0f);
    if (kind == EasingFunctionKind::Linear) {
        return t;
    }
    switch (mode) {
    case EasingMode::EaseIn:
        return EaseInCore(kind, t);
    case EasingMode::EaseOut:
        return 1.0f - EaseInCore(kind, 1.0f - t);
    case EasingMode::EaseInOut:
        return (t < 0.5f)
            ? 0.5f * EaseInCore(kind, t * 2.0f)
            : 1.0f - 0.5f * EaseInCore(kind, (1.0f - t) * 2.0f);
    }
    return t;
}

std::int32_t VisualAnimationEngine::StartFloat(
    VisualNodeId node,
    VisualAnimProp prop,
    float fromVal,
    float toVal,
    float durationSec,
    EasingFunctionKind easeKind,
    EasingMode easeMode,
    float delaySec,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!node.IsValid()) return 0;
    const float clampedDelay = std::max(0.0f, delaySec);
    if (clampedDelay <= 0.0f) {
        tracks_.erase(
            std::remove_if(
                tracks_.begin(),
                tracks_.end(),
                [node, prop](const VisualAnimTrack& existing) noexcept {
                    return existing.targetNode == node && existing.prop == prop;
                }),
            tracks_.end());
    }
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::FloatTween;
    track.fromVal = fromVal;
    track.toVal = toVal;
    track.currentVal = fromVal;
    track.durationSec = std::max(0.0f, durationSec);
    track.delaySec = clampedDelay;
    track.easeKind = easeKind;
    track.easeMode = easeMode;
    track.autoReverse = autoReverse;
    track.repeatCount = repeatCount;
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

std::int32_t VisualAnimationEngine::StartColor(
    VisualNodeId node,
    VisualAnimProp prop,
    std::uint32_t fromRgba,
    std::uint32_t toRgba,
    float durationSec,
    EasingFunctionKind easeKind,
    EasingMode easeMode,
    float delaySec,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!node.IsValid()) return 0;
    const float clampedDelay = std::max(0.0f, delaySec);
    if (clampedDelay <= 0.0f) {
        tracks_.erase(
            std::remove_if(
                tracks_.begin(),
                tracks_.end(),
                [node, prop](const VisualAnimTrack& existing) noexcept {
                    return existing.targetNode == node && existing.prop == prop;
                }),
            tracks_.end());
    }
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::ColorTween;
    track.fromRgba = fromRgba;
    track.toRgba = toRgba;
    track.currentRgba = fromRgba;
    track.durationSec = std::max(0.0f, durationSec);
    track.delaySec = clampedDelay;
    track.easeKind = easeKind;
    track.easeMode = easeMode;
    track.autoReverse = autoReverse;
    track.repeatCount = repeatCount;
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

float VisualAnimationEngine::ReadCurrentScalarProp(
    const VisualWorld& world,
    VisualNodeId id,
    VisualAnimProp prop) noexcept {
    const VisualNode* node = world.GetNode(id);
    if (!node) return 0.0f;
    switch (prop) {
    case VisualAnimProp::Opacity: return node->layout.opacity;
    case VisualAnimProp::Width: return std::isfinite(node->layout.width) ? node->layout.width : node->layout.layoutRect.width;
    case VisualAnimProp::Height: return std::isfinite(node->layout.height) ? node->layout.height : node->layout.layoutRect.height;
    case VisualAnimProp::TranslateX: return node->transform2D.tx;
    case VisualAnimProp::TranslateY: return node->transform2D.ty;
    case VisualAnimProp::ScaleX: return node->transform2D.sx;
    case VisualAnimProp::ScaleY: return node->transform2D.sy;
    case VisualAnimProp::RotateDeg: return node->transform2D.rotDeg;
    case VisualAnimProp::SkewX: return node->transform2D.skewX;
    case VisualAnimProp::SkewY: return node->transform2D.skewY;
    case VisualAnimProp::Rot3DX: return node->Transform3D().rx;
    case VisualAnimProp::Rot3DY: return node->Transform3D().ry;
    case VisualAnimProp::Rot3DZ: return node->Transform3D().rz;
    case VisualAnimProp::Trans3DX: return node->Transform3D().tx;
    case VisualAnimProp::Trans3DY: return node->Transform3D().ty;
    case VisualAnimProp::Trans3DZ: return node->Transform3D().tz;
    case VisualAnimProp::CornerRadius: return node->box.cornerRadius.topLeft;
    case VisualAnimProp::PathTrimStart: return node->Vector().trimStart;
    case VisualAnimProp::PathTrimEnd: return node->Vector().trimEnd;
    case VisualAnimProp::EffectRadius: return node->Effect().radius;
    case VisualAnimProp::EffectOpacity: return node->Effect().opacity;
    case VisualAnimProp::BrushOpacity:
        return (node->kind == VisualKind::Vector) ? node->Vector().fillOpacity : node->box.bgOpacity;
    case VisualAnimProp::FillBrushOpacity:
        return (node->kind == VisualKind::Vector) ? node->Vector().fillOpacity : node->box.bgOpacity;
    case VisualAnimProp::StrokeBrushOpacity:
        return (node->kind == VisualKind::Vector) ? node->Vector().strokeOpacity : 1.0f;
    case VisualAnimProp::FgBrushOpacity: {
        if (node->kind == VisualKind::Text) return node->Text().fgOpacity;
        if (const VisualNode* txt = FindFirstDescendantTextNode(world, id, 0)) {
            return txt->Text().fgOpacity;
        }
        return node->Text().fgOpacity;
    }
    case VisualAnimProp::CustomScalar0: return node->customScalars[0];
    case VisualAnimProp::CustomScalar1: return node->customScalars[1];
    case VisualAnimProp::CustomScalar2: return node->customScalars[2];
    case VisualAnimProp::CustomScalar3: return node->customScalars[3];
    case VisualAnimProp::OpacityMaskStop0Offset:
    case VisualAnimProp::OpacityMaskStop1Offset:
    case VisualAnimProp::OpacityMaskStop2Offset:
    case VisualAnimProp::OpacityMaskStop3Offset: {
        const std::size_t stopIdx = static_cast<std::size_t>(
            static_cast<std::uint8_t>(prop) - static_cast<std::uint8_t>(VisualAnimProp::OpacityMaskStop0Offset));
        if (stopIdx < node->OpacityMask().stopOffsets.size() &&
            std::isfinite(node->OpacityMask().stopOffsets[stopIdx])) {
            return node->OpacityMask().stopOffsets[stopIdx];
        }
        GradientBrushDesc desc{};
        if (TryReadGradientBrush(world, node->OpacityMask().gradId, desc) && stopIdx < desc.stops.size()) {
            return desc.stops[stopIdx].offset;
        }
        return 0.0f;
    }
    default: {
        const auto target = ParseGradientStopTarget(prop);
        if (target.isStopOffset) {
            GradientBrushDesc desc{};
            if (TryReadGradientBrush(world, ReadGradId(node, target.isStroke), desc) &&
                target.stopIndex < desc.stops.size()) {
                return desc.stops[target.stopIndex].offset;
            }
        }
        return 0.0f;
    }
    }
}

std::uint32_t VisualAnimationEngine::ReadCurrentColorProp(
    const VisualWorld& world,
    VisualNodeId id,
    VisualAnimProp prop) noexcept {
    const VisualNode* node = world.GetNode(id);
    if (!node) return 0u;
    if (const auto target = ParseGradientStopTarget(prop); target.isStopColor) {
        GradientBrushDesc desc{};
        if (TryReadGradientBrush(world, ReadGradId(node, target.isStroke), desc) &&
            target.stopIndex < desc.stops.size()) {
            return PackColor(desc.stops[target.stopIndex].color);
        }
        return 0u;
    }
    switch (prop) {
    case VisualAnimProp::BgColor:
    case VisualAnimProp::FillColor:
        return (node->kind == VisualKind::Vector) ? node->Vector().fillRgba : node->box.bgRgba;
    case VisualAnimProp::FgColor: {
        if (node->kind == VisualKind::Text) return node->Text().fgRgba;
        if (const VisualNode* txt = FindFirstDescendantTextNode(world, id, 0)) {
            return txt->Text().fgRgba;
        }
        return node->Text().fgRgba;
    }
    case VisualAnimProp::BorderColor:
    case VisualAnimProp::StrokeColor:
        return (node->kind == VisualKind::Vector) ? node->Vector().strokeRgba : node->box.borderRgba;
    default:
        return 0u;
    }
}

void VisualAnimationEngine::ApplyScalarPropImmediate(
    VisualWorld& world,
    VisualNodeId node,
    VisualAnimProp prop,
    float value) noexcept {
    ApplyScalarProp(world, node, prop, value);
}

void VisualAnimationEngine::ApplyColorPropImmediate(
    VisualWorld& world,
    VisualNodeId node,
    VisualAnimProp prop,
    std::uint32_t rgba) noexcept {
    ApplyColorProp(world, node, prop, rgba);
}

std::int32_t VisualAnimationEngine::SpringFloat(
    VisualNodeId node,
    VisualAnimProp prop,
    float currentVal,
    float targetVal,
    float stiffness,
    float damping,
    float mass) {
    if (!node.IsValid()) return 0;
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::FloatSpring;
    track.currentVal = currentVal;
    track.toVal = targetVal;
    track.spring.stiffness = std::max(1.0f, stiffness);
    track.spring.damping = std::max(0.1f, damping);
    track.spring.mass = std::max(0.01f, mass);
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

std::int32_t VisualAnimationEngine::StartKeyFrames(
    VisualNodeId node,
    VisualAnimProp prop,
    std::vector<VisualKeyFrame> keyFrames,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!node.IsValid() || keyFrames.empty()) return 0;
    for (VisualKeyFrame& kf : keyFrames) {
        kf.timeSec = std::max(0.0f, kf.timeSec);
    }
    std::stable_sort(
        keyFrames.begin(),
        keyFrames.end(),
        [](const VisualKeyFrame& a, const VisualKeyFrame& b) {
            return a.timeSec < b.timeSec;
        });
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::KeyFrames;
    track.durationSec = keyFrames.back().timeSec;
    track.autoReverse = autoReverse;
    track.repeatCount = repeatCount;
    track.keyFrames = std::move(keyFrames);
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

void VisualAnimationEngine::StopAll(VisualNodeId node) noexcept {
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(), [node](const VisualAnimTrack& tr) {
            return tr.targetNode == node;
        }),
        tracks_.end());
}

void VisualAnimationEngine::StopTrack(std::int32_t trackId) noexcept {
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(), [trackId](const VisualAnimTrack& tr) {
            return tr.trackId == trackId;
        }),
        tracks_.end());
}

void VisualAnimationEngine::Clear() noexcept {
    tracks_.clear();
}

bool VisualAnimationEngine::Tick(float dtSeconds, VisualWorld& world) {
    if (tracks_.empty() || dtSeconds < 0.0f) {
        return false;
    }
    bool anyUpdated = false;
    for (std::size_t i = 0u; i < tracks_.size(); ) {
        VisualAnimTrack& track = tracks_[i];
        if (!world.IsAlive(track.targetNode)) {
            tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        anyUpdated = true;
        float activeDt = dtSeconds;
        if (track.delaySec > 0.0f) {
            const float step = std::min(activeDt, track.delaySec);
            track.delaySec -= step;
            activeDt -= step;
            if (track.delaySec > 0.0f) {
                ++i;
                continue;
            }
        }
        track.elapsedSec += activeDt;

        bool finished = false;
        if (track.kind == VisualAnimTrackKind::FloatSpring) {
            StepSpringAnalytical(track, activeDt);
            if (std::abs(track.currentVal - track.toVal) <= track.spring.restPositionEpsilon &&
                std::abs(track.velocity) <= track.spring.restVelocityEpsilon) {
                track.currentVal = track.toVal;
                track.velocity = 0.0f;
                finished = true;
            }
            ApplyScalarProp(world, track.targetNode, track.prop, track.currentVal);
        } else {
            const float rawDur = track.durationSec;
            float normT = 1.0f;
            float localTimeSec = rawDur;
            if (rawDur <= 1e-5f) {
                if (track.autoReverse) {
                    normT = 0.0f;
                    localTimeSec = 0.0f;
                } else {
                    normT = 1.0f;
                    localTimeSec = rawDur;
                }
                ++track.completedCycles;
                finished = (track.repeatCount > 0 && track.completedCycles >= track.repeatCount);
            } else {
                while (track.elapsedSec >= rawDur && !finished) {
                    if (track.autoReverse) {
                        if (track.forwardDirection) {
                            track.forwardDirection = false;
                            track.elapsedSec -= rawDur;
                        } else {
                            ++track.completedCycles;
                            if (track.repeatCount > 0 && track.completedCycles >= track.repeatCount) {
                                finished = true;
                            } else {
                                track.forwardDirection = true;
                                track.elapsedSec -= rawDur;
                            }
                        }
                    } else {
                        ++track.completedCycles;
                        if (track.repeatCount > 0 && track.completedCycles >= track.repeatCount) {
                            finished = true;
                        } else {
                            track.elapsedSec -= rawDur;
                        }
                    }
                }
                if (finished) {
                    normT = track.autoReverse ? 0.0f : 1.0f;
                    localTimeSec = track.autoReverse ? 0.0f : rawDur;
                } else {
                    const float frac = std::clamp(track.elapsedSec / rawDur, 0.0f, 1.0f);
                    normT = track.forwardDirection ? frac : (1.0f - frac);
                    localTimeSec = normT * rawDur;
                }
            }

            if (track.kind == VisualAnimTrackKind::FloatTween) {
                const float eased = EvaluateEasing(track.easeKind, track.easeMode, normT);
                const float val = track.fromVal + (track.toVal - track.fromVal) * eased;
                track.currentVal = val;
                ApplyScalarProp(world, track.targetNode, track.prop, val);
            } else if (track.kind == VisualAnimTrackKind::ColorTween) {
                const float eased = EvaluateEasing(track.easeKind, track.easeMode, normT);
                const std::uint32_t col = LerpRgba(track.fromRgba, track.toRgba, eased);
                track.currentRgba = col;
                ApplyColorProp(world, track.targetNode, track.prop, col);
            } else if (track.kind == VisualAnimTrackKind::KeyFrames) {
                EvaluateKeyFramesAtTime(track, world, localTimeSec);
            }
        }

        if (finished) {
            tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    return anyUpdated;
}

bool VisualAnimationEngine::HasTrack(std::int32_t trackId) const noexcept {
    if (trackId <= 0) {
        return false;
    }
    for (const VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            return true;
        }
    }
    return false;
}

bool VisualAnimationEngine::HasActiveAnimations() const noexcept {
    return !tracks_.empty();
}

std::size_t VisualAnimationEngine::ActiveTrackCount() const noexcept {
    return tracks_.size();
}

} // namespace aero
