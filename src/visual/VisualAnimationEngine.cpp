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

bool ApplyTextFgOpacityRecursive(
    VisualWorld& world,
    VisualNodeId id,
    float opacity,
    int depth) noexcept {
    if (depth > 8) return false;
    VisualNode* node = world.GetNode(id);
    if (!node) return false;
    if (node->kind == VisualKind::Text) {
        const float cur = node->Text().fgOpacity;
        if (std::fabs(cur - opacity) <= 1e-5f) {
            return false;
        }
        node->EnsureTextData().fgOpacity = opacity;
        world.MarkDirty(id, VisualDirty_Visual);
        return true;
    }
    bool changed = false;
    const std::vector<VisualNodeId> kids = node->children;
    for (VisualNodeId ch : kids) {
        changed |= ApplyTextFgOpacityRecursive(world, ch, opacity, depth + 1);
    }
    return changed;
}

bool ApplyTextFgColorRecursive(
    VisualWorld& world,
    VisualNodeId id,
    std::uint32_t rgba,
    int depth) noexcept {
    if (depth > 8) return false;
    VisualNode* node = world.GetNode(id);
    if (!node) return false;
    if (node->kind == VisualKind::Text) {
        const std::uint32_t cur = node->Text().fgRgba;
        if (cur == rgba) {
            return false;
        }
        node->EnsureTextData().fgRgba = rgba;
        world.MarkDirty(id, VisualDirty_Visual);
        return true;
    }
    bool changed = false;
    const std::vector<VisualNodeId> kids = node->children;
    for (VisualNodeId ch : kids) {
        changed |= ApplyTextFgColorRecursive(world, ch, rgba, depth + 1);
    }
    return changed;
}

[[nodiscard]] float EaseInBounceWpf(float t, float bouncesParam, float bouncinessParam) noexcept {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const double bounces = std::max(0.0, std::floor(static_cast<double>(bouncesParam)));
    const double bounciness = std::max(1.001, static_cast<double>(bouncinessParam));
    const double powN = std::pow(bounciness, bounces);
    const double oneMinusB = 1.0 - bounciness;
    const double sumOfUnits = (1.0 - powN) / oneMinusB + powN * 0.5;
    const double logArg = std::max(1e-12, -static_cast<double>(t) * sumOfUnits * oneMinusB + 1.0);
    const double unitAtT = std::clamp(
        std::floor(std::log(logArg) / std::log(bounciness)),
        0.0,
        bounces);
    const double start = (1.0 - std::pow(bounciness, unitAtT)) / (oneMinusB * sumOfUnits);
    const double end = (1.0 - std::pow(bounciness, unitAtT + 1.0)) / (oneMinusB * sumOfUnits);
    const double mid = (start + end) * 0.5;
    const double radius = end - mid;
    if (radius <= 1e-9) {
        return 1.0f;
    }
    const double rel = (static_cast<double>(t) - mid) / radius;
    const double peak = std::pow(1.0 / bounciness, bounces - unitAtT);
    return static_cast<float>((-peak / (radius * radius)) * (rel * radius - radius) * (rel * radius + radius));
}

[[nodiscard]] float EaseInCore(
    EasingFunctionKind kind,
    float t,
    float param0,
    float param1,
    std::uint8_t paramMask) noexcept {
    const bool hasP0 = (paramMask & 1u) != 0u || (paramMask == 0u && param0 != 0.0f);
    const bool hasP1 = (paramMask & 2u) != 0u || (paramMask == 0u && param1 != 0.0f);
    switch (kind) {
    case EasingFunctionKind::Linear: return t;
    case EasingFunctionKind::Quadratic: return t * t;
    case EasingFunctionKind::Cubic: return t * t * t;
    case EasingFunctionKind::Quartic: return t * t * t * t;
    case EasingFunctionKind::Quintic: return t * t * t * t * t;
    case EasingFunctionKind::Sine: return 1.0f - std::cos(t * 1.57079632679f);
    case EasingFunctionKind::Circle: return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
    case EasingFunctionKind::Exponential: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        const float exponent = hasP0 ? param0 : 2.0f;
        if (std::fabs(exponent) <= 1e-6f) return t;
        const double denom = std::exp(static_cast<double>(exponent)) - 1.0;
        if (std::fabs(denom) <= 1e-7) return t;
        return static_cast<float>((std::exp(static_cast<double>(exponent * t)) - 1.0) / denom);
    }
    case EasingFunctionKind::Back: {
        const float amp = hasP0 ? std::max(0.0f, param0) : 1.0f;
        return t * t * t - t * amp * std::sin(t * 3.14159265359f);
    }
    case EasingFunctionKind::Elastic: {
        if (t <= 0.0f || t >= 1.0f) return t;
        const float osc = hasP0 ? std::max(0.0f, param0) : 3.0f;
        const float spring = hasP1 ? std::max(0.0f, param1) : 3.0f;
        double expo = static_cast<double>(t);
        if (spring > 1e-4f) {
            expo = (std::exp(static_cast<double>(spring * t)) - 1.0) / (std::exp(static_cast<double>(spring)) - 1.0);
        }
        return static_cast<float>(expo * std::sin((6.28318530718 * osc + 1.57079632679) * static_cast<double>(t)));
    }
    case EasingFunctionKind::Bounce: {
        if (hasP0 || hasP1) {
            return EaseInBounceWpf(t, hasP0 ? param0 : 3.0f, hasP1 ? param1 : 2.0f);
        }
        const float inv = 1.0f - t;
        constexpr float kN1 = 7.5625f;
        constexpr float kD1 = 2.75f;
        float out = 0.0f;
        if (inv < 1.0f / kD1) {
            out = kN1 * inv * inv;
        } else if (inv < 2.0f / kD1) {
            const float x = inv - 1.5f / kD1;
            out = kN1 * x * x + 0.75f;
        } else if (inv < 2.5f / kD1) {
            const float x = inv - 2.25f / kD1;
            out = kN1 * x * x + 0.9375f;
        } else {
            const float x = inv - 2.625f / kD1;
            out = kN1 * x * x + 0.984375f;
        }
        return 1.0f - out;
    }
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
        auto& v = node->EnsureVectorData();
        return isStroke
            ? MutableGradSlot{&v.strokeGradId, &v.ownsMutableStrokeGrad}
            : MutableGradSlot{&v.fillGradId, &v.ownsMutableFillGrad};
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

[[nodiscard]] bool TryReadGradientStop(
    const VisualWorld& world,
    std::int32_t gradId,
    std::size_t stopIndex,
    float& outOffset,
    Color& outColor) noexcept {
    if (gradId <= 0) return false;
    const auto brushId = static_cast<std::uint32_t>(gradId);
    const UIResourceStore* store = world.ResourceStore();
    return (store != nullptr && store->GradientBrushes().TryGetStopValue(brushId, stopIndex, outOffset, outColor)) ||
           GradientBrushRegistry::Instance().TryGetStopValue(brushId, stopIndex, outOffset, outColor);
}

void RemoveTracksByNodeAndProp(
    std::vector<VisualAnimTrack>& tracks,
    VisualNodeId node,
    VisualAnimProp prop) noexcept {
    std::size_t writeIdx = 0u;
    for (std::size_t i = 0u; i < tracks.size(); ++i) {
        if (tracks[i].targetNode == node && tracks[i].prop == prop) {
            continue;
        }
        if (writeIdx != i) {
            tracks[writeIdx] = std::move(tracks[i]);
        }
        ++writeIdx;
    }
    tracks.resize(writeIdx);
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

[[nodiscard]] bool PropNeedsDefaultScriptSync(VisualAnimProp prop) noexcept {
    return prop == VisualAnimProp::ManagedCustom ||
           (prop >= VisualAnimProp::CustomScalar1 && prop <= VisualAnimProp::CustomScalar3);
}

bool ApplyScalarProp(VisualWorld& world, VisualNodeId id, VisualAnimProp prop, float value) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) return false;
    auto scalarSame = [](float a, float b) noexcept {
        return (std::isnan(a) && std::isnan(b)) || std::fabs(a - b) <= 1e-5f;
    };
    switch (prop) {
    case VisualAnimProp::Opacity: {
        const float next = std::clamp(value, 0.0f, 1.0f);
        if (scalarSame(node->layout.opacity, next)) return false;
        node->layout.opacity = next;
        break;
    }
    case VisualAnimProp::Width:
        if (scalarSame(node->layout.width, value)) return false;
        node->layout.width = value;
        world.MarkDirty(id, VisualDirty_Layout);
        return true;
    case VisualAnimProp::Height:
        if (scalarSame(node->layout.height, value)) return false;
        node->layout.height = value;
        world.MarkDirty(id, VisualDirty_Layout);
        return true;
    case VisualAnimProp::TranslateX:
        if (scalarSame(node->transform2D.tx, value)) return false;
        node->transform2D.tx = value;
        break;
    case VisualAnimProp::TranslateY:
        if (scalarSame(node->transform2D.ty, value)) return false;
        node->transform2D.ty = value;
        break;
    case VisualAnimProp::ScaleX:
        if (scalarSame(node->transform2D.sx, value)) return false;
        node->transform2D.sx = value;
        break;
    case VisualAnimProp::ScaleY:
        if (scalarSame(node->transform2D.sy, value)) return false;
        node->transform2D.sy = value;
        break;
    case VisualAnimProp::RotateDeg:
        if (scalarSame(node->transform2D.rotDeg, value)) return false;
        node->transform2D.rotDeg = value;
        break;
    case VisualAnimProp::SkewX:
        if (scalarSame(node->transform2D.skewX, value)) return false;
        node->transform2D.skewX = value;
        break;
    case VisualAnimProp::SkewY:
        if (scalarSame(node->transform2D.skewY, value)) return false;
        node->transform2D.skewY = value;
        break;
    case VisualAnimProp::Rot3DX:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().rx, value)) return false;
        node->EnsureSpatial3D().transform3D.rx = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DY:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().ry, value)) return false;
        node->EnsureSpatial3D().transform3D.ry = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DZ:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().rz, value)) return false;
        node->EnsureSpatial3D().transform3D.rz = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DX:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().tx, value)) return false;
        node->EnsureSpatial3D().transform3D.tx = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DY:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().ty, value)) return false;
        node->EnsureSpatial3D().transform3D.ty = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DZ:
        if (node->Transform3D().enabled && scalarSame(node->Transform3D().tz, value)) return false;
        node->EnsureSpatial3D().transform3D.tz = value;
        node->EnsureSpatial3D().transform3D.enabled = true;
        break;
    case VisualAnimProp::CornerRadius:
        if (scalarSame(node->box.cornerRadius.topLeft, value) &&
            scalarSame(node->box.cornerRadius.topRight, value) &&
            scalarSame(node->box.cornerRadius.bottomRight, value) &&
            scalarSame(node->box.cornerRadius.bottomLeft, value)) {
            return false;
        }
        node->box.cornerRadius = CornerRadius{value};
        break;
    case VisualAnimProp::PathTrimStart:
        if (scalarSame(node->Vector().trimStart, value)) return false;
        node->EnsureVectorData().trimStart = value;
        break;
    case VisualAnimProp::PathTrimEnd:
        if (scalarSame(node->Vector().trimEnd, value)) return false;
        node->EnsureVectorData().trimEnd = value;
        break;
    case VisualAnimProp::EffectRadius:
        if (node->HasEffectMask() && scalarSame(node->Effect().radius, value)) return false;
        node->EnsureEffectMask().effect.radius = value;
        break;
    case VisualAnimProp::EffectOpacity: {
        const float next = std::clamp(value, 0.0f, 1.0f);
        if (node->HasEffectMask() && scalarSame(node->Effect().opacity, next)) return false;
        node->EnsureEffectMask().effect.opacity = next;
        break;
    }
    case VisualAnimProp::BrushOpacity:
    case VisualAnimProp::FillBrushOpacity: {
        const float next = std::clamp(value, 0.0f, 1.0f);
        if (node->kind == VisualKind::Vector) {
            if (node->HasVectorData() && scalarSame(node->Vector().fillOpacity, next)) return false;
            node->EnsureVectorData().fillOpacity = next;
        } else {
            if (scalarSame(node->box.bgOpacity, next)) return false;
            node->box.bgOpacity = next;
        }
        break;
    }
    case VisualAnimProp::StrokeBrushOpacity: {
        const float next = std::clamp(value, 0.0f, 1.0f);
        if (node->kind == VisualKind::Vector) {
            if (node->HasVectorData() && scalarSame(node->Vector().strokeOpacity, next)) return false;
            node->EnsureVectorData().strokeOpacity = next;
        } else {
            return false;
        }
        break;
    }
    case VisualAnimProp::FgBrushOpacity:
        return ApplyTextFgOpacityRecursive(world, id, std::clamp(value, 0.0f, 1.0f), 0);
    case VisualAnimProp::CustomScalar0:
        if (scalarSame(node->customScalars[0], value)) return false;
        node->customScalars[0] = value;
        break;
    case VisualAnimProp::CustomScalar1:
        if (scalarSame(node->customScalars[1], value)) return false;
        node->customScalars[1] = value;
        if (node->layout.hasLayoutTransform || std::fabs(value - 1.0f) > 1e-6f) {
            const auto& lt = node->layout.layoutTransform2D;
            world.SetLayoutTransform2D(id, value, lt.sy, lt.rotDeg, lt.skewX, lt.skewY);
            return true;
        }
        break;
    case VisualAnimProp::CustomScalar2:
        if (scalarSame(node->customScalars[2], value)) return false;
        node->customScalars[2] = value;
        if (node->layout.hasLayoutTransform || std::fabs(value - 1.0f) > 1e-6f) {
            const auto& lt = node->layout.layoutTransform2D;
            world.SetLayoutTransform2D(id, lt.sx, value, lt.rotDeg, lt.skewX, lt.skewY);
            return true;
        }
        break;
    case VisualAnimProp::CustomScalar3:
        if (scalarSame(node->customScalars[3], value)) return false;
        node->customScalars[3] = value;
        if (node->layout.hasLayoutTransform || std::fabs(value) > 1e-6f) {
            const auto& lt = node->layout.layoutTransform2D;
            world.SetLayoutTransform2D(id, lt.sx, lt.sy, value, lt.skewX, lt.skewY);
            return true;
        }
        break;
    case VisualAnimProp::OpacityMaskStop0Offset:
    case VisualAnimProp::OpacityMaskStop1Offset:
    case VisualAnimProp::OpacityMaskStop2Offset:
    case VisualAnimProp::OpacityMaskStop3Offset: {
        const std::size_t stopIdx = static_cast<std::size_t>(
            static_cast<std::uint8_t>(prop) - static_cast<std::uint8_t>(VisualAnimProp::OpacityMaskStop0Offset));
        const float next = std::clamp(value, 0.0f, 1.0f);
        auto& offsets = node->EnsureEffectMask().opacityMask.stopOffsets;
        if (stopIdx < offsets.size()) {
            if (scalarSame(offsets[stopIdx], next)) return false;
            offsets[stopIdx] = next;
        } else {
            return false;
        }
        break;
    }
    case VisualAnimProp::ManagedCustom:
        return false;
    default: {
        const auto target = ParseGradientStopTarget(prop);
        if (target.isStopOffset) {
            float curOffset = 0.0f;
            Color curColor{};
            if (TryReadGradientStop(world, ReadGradId(node, target.isStroke), target.stopIndex, curOffset, curColor) &&
                scalarSame(curOffset, value)) {
                return false;
            }
            std::uint32_t brushId = 0u;
            if (GradientBrushRegistry* reg = EnsureMutableNodeGradient(world, node, target.isStroke, brushId)) {
                if (!reg->UpdateBrushStopOffset(brushId, target.stopIndex, value)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
        break;
    }
    }
    world.MarkDirty(id, DirtyFlagsForScalarProp(prop));
    return true;
}

bool ApplyColorProp(VisualWorld& world, VisualNodeId id, VisualAnimProp prop, std::uint32_t rgba) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) return false;
    if (const auto target = ParseGradientStopTarget(prop); target.isStopColor) {
        float curOffset = 0.0f;
        Color curColor{};
        if (TryReadGradientStop(world, ReadGradId(node, target.isStroke), target.stopIndex, curOffset, curColor) &&
            PackColor(curColor) == rgba) {
            return false;
        }
        std::uint32_t brushId = 0u;
        if (GradientBrushRegistry* reg = EnsureMutableNodeGradient(world, node, target.isStroke, brushId)) {
            if (reg->UpdateBrushStopColor(brushId, target.stopIndex, UnpackRgba(rgba))) {
                world.MarkDirty(id, VisualDirty_Visual);
                return true;
            }
        }
        return false;
    }
    switch (prop) {
    case VisualAnimProp::BgColor:
    case VisualAnimProp::FillColor:
        if (node->kind == VisualKind::Vector) {
            if (node->HasVectorData() && node->Vector().fillRgba == rgba) return false;
            node->EnsureVectorData().fillRgba = rgba;
        } else {
            if (node->box.bgRgba == rgba) return false;
            node->box.bgRgba = rgba;
        }
        break;
    case VisualAnimProp::FgColor:
        return ApplyTextFgColorRecursive(world, id, rgba, 0);
    case VisualAnimProp::BorderColor:
    case VisualAnimProp::StrokeColor:
        if (node->kind == VisualKind::Vector) {
            if (node->HasVectorData() && node->Vector().strokeRgba == rgba) return false;
            node->EnsureVectorData().strokeRgba = rgba;
        } else {
            if (node->box.borderRgba == rgba) return false;
            node->box.borderRgba = rgba;
        }
        break;
    case VisualAnimProp::ManagedCustom:
        return false;
    default:
        return false;
    }
    world.MarkDirty(id, VisualDirty_Visual);
    return true;
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

bool EvaluateKeyFramesAtTime(VisualAnimTrack& track, VisualWorld& world, float localTimeSec) noexcept {
    const auto& kfs = track.keyFrames;
    if (kfs.empty()) {
        return false;
    }
    const bool colorProp = IsColorProp(track.prop);
    auto applyKf = [&](const VisualKeyFrame& kf) -> bool {
        if (colorProp) {
            track.currentRgba = kf.colorRgba;
            return ApplyColorProp(world, track.targetNode, track.prop, kf.colorRgba);
        }
        track.currentVal = kf.scalarValue;
        return ApplyScalarProp(world, track.targetNode, track.prop, kf.scalarValue);
    };

    if (kfs.size() == 1u || localTimeSec <= kfs.front().timeSec) {
        return applyKf(kfs.front());
    }
    if (localTimeSec >= kfs.back().timeSec) {
        return applyKf(kfs.back());
    }

    for (std::size_t idx = 1u; idx < kfs.size(); ++idx) {
        const VisualKeyFrame& kf0 = kfs[idx - 1u];
        const VisualKeyFrame& kf1 = kfs[idx];
        if (localTimeSec <= kf1.timeSec) {
            const float span = kf1.timeSec - kf0.timeSec;
            if (kf1.discrete || span <= 1e-6f) {
                return applyKf((localTimeSec >= kf1.timeSec) ? kf1 : kf0);
            }
            const float u = std::clamp((localTimeSec - kf0.timeSec) / span, 0.0f, 1.0f);
            const float eased = EvaluateEasing(kf1.easingKind, kf1.easingMode, u, kf1.easeParam0, kf1.easeParam1, kf1.easeParamMask);
            if (colorProp) {
                const std::uint32_t col = LerpRgba(kf0.colorRgba, kf1.colorRgba, eased);
                track.currentRgba = col;
                return ApplyColorProp(world, track.targetNode, track.prop, col);
            }
            const float val = kf0.scalarValue + (kf1.scalarValue - kf0.scalarValue) * eased;
            track.currentVal = val;
            return ApplyScalarProp(world, track.targetNode, track.prop, val);
        }
    }
    return applyKf(kfs.back());
}

} // namespace

float EvaluateEasing(
    EasingFunctionKind kind,
    EasingMode mode,
    float normalizedTime01,
    float param0,
    float param1,
    std::uint8_t paramMask) noexcept {
    const float t = std::clamp(normalizedTime01, 0.0f, 1.0f);
    if (kind == EasingFunctionKind::Linear) {
        return t;
    }
    if (kind == EasingFunctionKind::Spline && (paramMask & 16u) != 0u) {
        if (t <= 0.0f || t >= 1.0f) {
            return t;
        }
        const float p0 = std::max(0.0f, param0);
        const float p1 = std::max(0.0f, param1);
        const float qx1 = std::floor(p0 / 4096.0f);
        const float qy1 = p0 - qx1 * 4096.0f;
        const float qx2 = std::floor(p1 / 4096.0f);
        const float qy2 = p1 - qx2 * 4096.0f;
        const float x1 = std::clamp(qx1 / 4000.0f, 0.0f, 1.0f);
        const float y1 = qy1 / 4000.0f;
        const float x2 = std::clamp(qx2 / 4000.0f, 0.0f, 1.0f);
        const float y2 = qy2 / 4000.0f;
        float lo = 0.0f;
        float hi = 1.0f;
        float s = t;
        for (int iter = 0; iter < 10; ++iter) {
            const float u = 1.0f - s;
            const float bx = 3.0f * u * u * s * x1 + 3.0f * u * s * s * x2 + s * s * s;
            const float err = bx - t;
            if (std::fabs(err) <= 1e-5f) {
                break;
            }
            if (err < 0.0f) {
                lo = s;
            } else {
                hi = s;
            }
            const float dbx = 3.0f * u * u * x1 + 6.0f * u * s * (x2 - x1) + 3.0f * s * s * (1.0f - x2);
            if (dbx > 1e-4f) {
                const float next = s - err / dbx;
                s = (next > lo && next < hi) ? next : (0.5f * (lo + hi));
            } else {
                s = 0.5f * (lo + hi);
            }
        }
        const float u = 1.0f - s;
        return 3.0f * u * u * s * y1 + 3.0f * u * s * s * y2 + s * s * s;
    }
    switch (mode) {
    case EasingMode::EaseIn:
        return EaseInCore(kind, t, param0, param1, paramMask);
    case EasingMode::EaseOut:
        return 1.0f - EaseInCore(kind, 1.0f - t, param0, param1, paramMask);
    case EasingMode::EaseInOut:
        return (t < 0.5f)
            ? 0.5f * EaseInCore(kind, t * 2.0f, param0, param1, paramMask)
            : 1.0f - 0.5f * EaseInCore(kind, (1.0f - t) * 2.0f, param0, param1, paramMask);
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
    if (clampedDelay <= 0.0f && prop != VisualAnimProp::ManagedCustom) {
        RemoveTracksByNodeAndProp(tracks_, node, prop);
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
    track.needsScriptSync = PropNeedsDefaultScriptSync(prop);
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
    if (clampedDelay <= 0.0f && prop != VisualAnimProp::ManagedCustom) {
        RemoveTracksByNodeAndProp(tracks_, node, prop);
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
    track.needsScriptSync = PropNeedsDefaultScriptSync(prop);
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
    case VisualAnimProp::CustomScalar1:
        return node->layout.hasLayoutTransform ? node->layout.layoutTransform2D.sx : node->customScalars[1];
    case VisualAnimProp::CustomScalar2:
        return node->layout.hasLayoutTransform ? node->layout.layoutTransform2D.sy : node->customScalars[2];
    case VisualAnimProp::CustomScalar3:
        return node->layout.hasLayoutTransform ? node->layout.layoutTransform2D.rotDeg : node->customScalars[3];
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
        float off = 0.0f;
        Color col{};
        if (TryReadGradientStop(world, node->OpacityMask().gradId, stopIdx, off, col)) {
            return off;
        }
        return 0.0f;
    }
    default: {
        const auto target = ParseGradientStopTarget(prop);
        if (target.isStopOffset) {
            float off = 0.0f;
            Color col{};
            if (TryReadGradientStop(world, ReadGradId(node, target.isStroke), target.stopIndex, off, col)) {
                return off;
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
        float off = 0.0f;
        Color col{};
        if (TryReadGradientStop(world, ReadGradId(node, target.isStroke), target.stopIndex, off, col)) {
            return PackColor(col);
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
    track.needsScriptSync = PropNeedsDefaultScriptSync(prop);
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
    std::int32_t repeatCount,
    float delaySec) {
    if (!node.IsValid() || keyFrames.empty()) return 0;
    const float clampedDelay = std::max(0.0f, delaySec);
    if (clampedDelay <= 0.0f && prop != VisualAnimProp::ManagedCustom) {
        RemoveTracksByNodeAndProp(tracks_, node, prop);
    }
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
    track.currentVal = keyFrames.front().scalarValue;
    track.currentRgba = keyFrames.front().colorRgba;
    track.durationSec = keyFrames.back().timeSec;
    track.delaySec = clampedDelay;
    track.autoReverse = autoReverse;
    track.repeatCount = repeatCount;
    track.needsScriptSync = PropNeedsDefaultScriptSync(prop);
    track.keyFrames = std::move(keyFrames);
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

std::int32_t VisualAnimationEngine::BeginKeyFramesTrack(
    VisualNodeId node,
    VisualAnimProp prop,
    float delaySec,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!node.IsValid()) return 0;
    const float clampedDelay = std::max(0.0f, delaySec);
    if (clampedDelay <= 0.0f && prop != VisualAnimProp::ManagedCustom) {
        RemoveTracksByNodeAndProp(tracks_, node, prop);
    }
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::KeyFrames;
    track.durationSec = 0.0f;
    track.delaySec = clampedDelay;
    track.autoReverse = autoReverse;
    track.repeatCount = repeatCount;
    track.needsScriptSync = PropNeedsDefaultScriptSync(prop);
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
}

void VisualAnimationEngine::AddKeyFrameFloat(
    std::int32_t trackId,
    float timeSec,
    float value,
    EasingFunctionKind easeKind,
    EasingMode easeMode,
    float param0,
    float param1,
    std::uint8_t paramMask,
    bool discrete) {
    if (trackId <= 0) return;
    for (VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId && tr.kind == VisualAnimTrackKind::KeyFrames) {
            VisualKeyFrame kf{};
            kf.timeSec = std::max(0.0f, timeSec);
            kf.scalarValue = value;
            kf.easingKind = easeKind;
            kf.easingMode = easeMode;
            kf.discrete = discrete;
            kf.easeParam0 = param0;
            kf.easeParam1 = param1;
            kf.easeParamMask = paramMask;
            const bool needSort = !tr.keyFrames.empty() && kf.timeSec < tr.keyFrames.back().timeSec;
            tr.keyFrames.push_back(kf);
            if (needSort) {
                std::stable_sort(
                    tr.keyFrames.begin(),
                    tr.keyFrames.end(),
                    [](const VisualKeyFrame& a, const VisualKeyFrame& b) {
                        return a.timeSec < b.timeSec;
                    });
            }
            tr.durationSec = tr.keyFrames.back().timeSec;
            tr.currentVal = tr.keyFrames.front().scalarValue;
            return;
        }
    }
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

void VisualAnimationEngine::SetTrackEasingParams(
    std::int32_t trackId,
    float param0,
    float param1,
    std::uint8_t paramMask) noexcept {
    if (trackId <= 0) return;
    for (VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            tr.easeParam0 = param0;
            tr.easeParam1 = param1;
            tr.easeParamMask = paramMask;
            return;
        }
    }
}

void VisualAnimationEngine::MarkTrackScriptSync(std::int32_t trackId) noexcept {
    if (trackId <= 0) return;
    for (VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            tr.needsScriptSync = true;
            return;
        }
    }
}

void VisualAnimationEngine::Clear() noexcept {
    tracks_.clear();
    lastTickNeededScriptSync_ = false;
}

bool VisualAnimationEngine::Tick(float dtSeconds, VisualWorld& world) {
    lastTickNeededScriptSync_ = false;
    if (tracks_.empty() || dtSeconds < 0.0f) {
        return false;
    }
    bool anyVisualMutated = false;
    std::size_t writeIdx = 0u;
    const std::size_t initialCount = tracks_.size();
    for (std::size_t i = 0u; i < initialCount; ++i) {
        VisualAnimTrack& track = tracks_[i];
        if (!world.IsAlive(track.targetNode)) {
            if (track.needsScriptSync) {
                lastTickNeededScriptSync_ = true;
            }
            continue;
        }
        float activeDt = dtSeconds;
        if (track.delaySec > 0.0f) {
            const float step = std::min(activeDt, track.delaySec);
            track.delaySec -= step;
            activeDt -= step;
            if (track.delaySec > 0.0f) {
                if (writeIdx != i) {
                    tracks_[writeIdx] = std::move(track);
                }
                ++writeIdx;
                continue;
            }
        }
        if (track.needsScriptSync) {
            lastTickNeededScriptSync_ = true;
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
            anyVisualMutated |= ApplyScalarProp(world, track.targetNode, track.prop, track.currentVal);
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
                const float eased = EvaluateEasing(track.easeKind, track.easeMode, normT, track.easeParam0, track.easeParam1, track.easeParamMask);
                const float val = track.fromVal + (track.toVal - track.fromVal) * eased;
                track.currentVal = val;
                anyVisualMutated |= ApplyScalarProp(world, track.targetNode, track.prop, val);
            } else if (track.kind == VisualAnimTrackKind::ColorTween) {
                const float eased = EvaluateEasing(track.easeKind, track.easeMode, normT, track.easeParam0, track.easeParam1, track.easeParamMask);
                const std::uint32_t col = LerpRgba(track.fromRgba, track.toRgba, eased);
                track.currentRgba = col;
                anyVisualMutated |= ApplyColorProp(world, track.targetNode, track.prop, col);
            } else if (track.kind == VisualAnimTrackKind::KeyFrames) {
                anyVisualMutated |= EvaluateKeyFramesAtTime(track, world, localTimeSec);
            }
        }

        if (finished) {
            lastTickNeededScriptSync_ = true;
        } else {
            if (writeIdx != i) {
                tracks_[writeIdx] = std::move(track);
            }
            ++writeIdx;
        }
    }
    tracks_.resize(writeIdx);
    return anyVisualMutated;
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

bool VisualAnimationEngine::IsTrackStarted(std::int32_t trackId) const noexcept {
    if (trackId <= 0) {
        return false;
    }
    for (const VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            return tr.delaySec <= 0.0f;
        }
    }
    return false;
}

float VisualAnimationEngine::ReadTrackFloat(std::int32_t trackId, float fallback) const noexcept {
    if (trackId <= 0) {
        return fallback;
    }
    for (const VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            return tr.currentVal;
        }
    }
    return fallback;
}

std::uint32_t VisualAnimationEngine::ReadTrackColor(std::int32_t trackId, std::uint32_t fallback) const noexcept {
    if (trackId <= 0) {
        return fallback;
    }
    for (const VisualAnimTrack& tr : tracks_) {
        if (tr.trackId == trackId) {
            return tr.currentRgba;
        }
    }
    return fallback;
}

bool VisualAnimationEngine::HasActiveAnimations() const noexcept {
    return !tracks_.empty();
}

std::size_t VisualAnimationEngine::ActiveTrackCount() const noexcept {
    return tracks_.size();
}

} // namespace aero
