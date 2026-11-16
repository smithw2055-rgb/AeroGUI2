#include "aero/visual/VisualAnimationEngine.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

namespace {

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
    case EasingFunctionKind::Bounce: {
        return 1.0f - EaseInCore(EasingFunctionKind::Linear, 1.0f - t);
    }
    case EasingFunctionKind::Spline: return t;
    }
    return t;
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
        node->transform3D.rx = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DY:
        node->transform3D.ry = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::Rot3DZ:
        node->transform3D.rz = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DX:
        node->transform3D.tx = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DY:
        node->transform3D.ty = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::Trans3DZ:
        node->transform3D.tz = value;
        node->transform3D.enabled = true;
        break;
    case VisualAnimProp::CornerRadius:
        node->box.cornerRadius = CornerRadius{value};
        break;
    case VisualAnimProp::PathTrimStart:
        node->vector.trimStart = value;
        break;
    case VisualAnimProp::PathTrimEnd:
        node->vector.trimEnd = value;
        break;
    case VisualAnimProp::EffectRadius:
        node->effect.radius = value;
        break;
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
    default:
        break;
    }
    world.MarkDirty(id, VisualDirty_Visual);
}

void ApplyColorProp(VisualWorld& world, VisualNodeId id, VisualAnimProp prop, std::uint32_t rgba) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) return;
    switch (prop) {
    case VisualAnimProp::BgColor:
        node->box.bgRgba = rgba;
        break;
    case VisualAnimProp::FgColor:
        node->text.fgRgba = rgba;
        break;
    case VisualAnimProp::BorderColor:
        node->box.borderRgba = rgba;
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
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::FloatTween;
    track.fromVal = fromVal;
    track.toVal = toVal;
    track.currentVal = fromVal;
    track.durationSec = std::max(0.0f, durationSec);
    track.delaySec = std::max(0.0f, delaySec);
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
    EasingMode easeMode) {
    if (!node.IsValid()) return 0;
    VisualAnimTrack track{};
    track.trackId = nextTrackId_++;
    track.targetNode = node;
    track.prop = prop;
    track.kind = VisualAnimTrackKind::ColorTween;
    track.fromRgba = fromRgba;
    track.toRgba = toRgba;
    track.durationSec = std::max(0.0f, durationSec);
    track.easeKind = easeKind;
    track.easeMode = easeMode;
    tracks_.push_back(std::move(track));
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return tracks_.back().trackId;
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
    for (std::size_t i = tracks_.size(); i > 0u; --i) {
        VisualAnimTrack& track = tracks_[i - 1u];
        if (!world.IsAlive(track.targetNode)) {
            tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i - 1u));
            continue;
        }
        anyUpdated = true;
        if (track.delaySec > 0.0f) {
            const float step = std::min(dtSeconds, track.delaySec);
            track.delaySec -= step;
            if (track.delaySec > 0.0f) {
                continue;
            }
        }
        track.elapsedSec += dtSeconds;

        bool finished = false;
        if (track.kind == VisualAnimTrackKind::FloatSpring) {
            const float force = -track.spring.stiffness * (track.currentVal - track.toVal);
            const float accel = force / track.spring.mass;
            track.velocity = (track.velocity + accel * dtSeconds) * std::max(0.0f, 1.0f - (track.spring.damping / track.spring.mass) * dtSeconds * 0.1f);
            track.currentVal += track.velocity * dtSeconds;
            if (std::abs(track.currentVal - track.toVal) <= track.spring.restPositionEpsilon &&
                std::abs(track.velocity) <= track.spring.restVelocityEpsilon) {
                track.currentVal = track.toVal;
                finished = true;
            }
            ApplyScalarProp(world, track.targetNode, track.prop, track.currentVal);
        } else {
            const float dur = std::max(1e-5f, track.durationSec);
            float t = std::clamp(track.elapsedSec / dur, 0.0f, 1.0f);
            if (!track.forwardDirection) {
                t = 1.0f - t;
            }
            const float eased = EvaluateEasing(track.easeKind, track.easeMode, t);
            if (track.kind == VisualAnimTrackKind::FloatTween) {
                const float val = track.fromVal + (track.toVal - track.fromVal) * eased;
                track.currentVal = val;
                ApplyScalarProp(world, track.targetNode, track.prop, val);
            } else if (track.kind == VisualAnimTrackKind::ColorTween) {
                ApplyColorProp(world, track.targetNode, track.prop, LerpRgba(track.fromRgba, track.toRgba, eased));
            }
            if (track.elapsedSec >= track.durationSec) {
                finished = true;
            }
        }
        if (finished) {
            tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i - 1u));
        }
    }
    return anyUpdated;
}

bool VisualAnimationEngine::HasActiveAnimations() const noexcept {
    return !tracks_.empty();
}

std::size_t VisualAnimationEngine::ActiveTrackCount() const noexcept {
    return tracks_.size();
}

} // namespace aero
