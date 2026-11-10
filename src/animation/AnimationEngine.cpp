#include "aero/animation/AnimationEngine.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float EvaluateEaseInCore(EasingFunctionKind kind, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (kind) {
        case EasingFunctionKind::Linear:
            return t;
        case EasingFunctionKind::Quadratic:
            return t * t;
        case EasingFunctionKind::Cubic:
            return t * t * t;
        case EasingFunctionKind::Quartic:
            return t * t * t * t;
        case EasingFunctionKind::Quintic:
            return t * t * t * t * t;
        case EasingFunctionKind::Sine:
            return 1.0f - std::cos((t * kPi) * 0.5f);
        case EasingFunctionKind::Circle:
            return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
        case EasingFunctionKind::Exponential:
            return (t <= 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
        case EasingFunctionKind::Back: {
            constexpr float s = 1.70158f;
            return t * t * ((s + 1.0f) * t - s);
        }
        case EasingFunctionKind::Elastic: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return -std::pow(2.0f, 10.0f * t - 10.0f) *
                   std::sin((t * 10.0f - 10.75f) * ((2.0f * kPi) / 3.0f));
        }
        case EasingFunctionKind::Bounce: {
            const float inv = 1.0f - t;
            float b = 0.0f;
            if (inv < 1.0f / 2.75f) {
                b = 7.5625f * inv * inv;
            } else if (inv < 2.0f / 2.75f) {
                const float u = inv - 1.5f / 2.75f;
                b = 7.5625f * u * u + 0.75f;
            } else if (inv < 2.5f / 2.75f) {
                const float u = inv - 2.25f / 2.75f;
                b = 7.5625f * u * u + 0.9375f;
            } else {
                const float u = inv - 2.625f / 2.75f;
                b = 7.5625f * u * u + 0.984375f;
            }
            return 1.0f - b;
        }
    }
    return t;
}

[[nodiscard]] Float4Value LerpFloat4(const Float4Value& a, const Float4Value& b, float t) noexcept {
    return Float4Value{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t};
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
            return EvaluateEaseInCore(kind, t);
        case EasingMode::EaseOut:
            return 1.0f - EvaluateEaseInCore(kind, 1.0f - t);
        case EasingMode::EaseInOut:
            if (t < 0.5f) {
                return 0.5f * EvaluateEaseInCore(kind, t * 2.0f);
            }
            return 1.0f - 0.5f * EvaluateEaseInCore(kind, (1.0f - t) * 2.0f);
    }
    return t;
}

AnimationEngine::AnimationEngine(std::size_t initialReservedTracks) {
    tracks_.reserve(initialReservedTracks);
    freeTrackIds_.reserve(initialReservedTracks);
}

void AnimationEngine::Clear() {
    for (auto& track : tracks_) {
        if (track.kind != AnimationTrackKind::Inactive) {
            ReleaseTrackSlot(track, false);
        }
    }
    tracks_.clear();
    freeTrackIds_.clear();
    activeCount_ = 0u;
}

float AnimationEngine::DefaultScalarForProperty(std::uint16_t propId) noexcept {
    switch (propId) {
        case PropId_Opacity:
        case PropId_AnimScaleX:
        case PropId_AnimScaleY:
            return 1.0f;
        case PropId_FontSize:
            return 14.0f;
        default:
            return 0.0f;
    }
}

float AnimationEngine::ReadCurrentScalar(const UIElement* target, std::uint16_t propId) noexcept {
    if (!target) {
        return DefaultScalarForProperty(propId);
    }
    float out = DefaultScalarForProperty(propId);
    if (TryGetFloat(target->GetValue(propId), out)) {
        if ((propId == PropId_Width || propId == PropId_Height) && out < 0.0f) {
            const Rect r = target->FinalRect();
            return (propId == PropId_Width) ? r.width : r.height;
        }
        return out;
    }
    return DefaultScalarForProperty(propId);
}

Float4Value AnimationEngine::ReadCurrentFloat4(const UIElement* target, std::uint16_t propId) noexcept {
    if (!target) {
        return Float4Value{};
    }
    const UnifiedValue v = target->GetValue(propId);
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return *f4;
    }
    return Float4Value{};
}

std::uint16_t AnimationEngine::FindActiveTrackId(
    const UIElement* target,
    std::uint16_t propId) const noexcept {
    if (!target) {
        return InvalidTrackId;
    }
    // O(1) lookup via CompactPropertyEntry::animationTrackId for sparse properties!
    if (!IsFastInlinePropertyId(propId)) {
        if (const auto* entry = target->FindCompactEntry(propId)) {
            if (entry->animationTrackId < tracks_.size()) {
                const auto& cand = tracks_[entry->animationTrackId];
                if (cand.kind != AnimationTrackKind::Inactive &&
                    cand.target.Get() == target &&
                    cand.propId == propId) {
                    return entry->animationTrackId;
                }
            }
        }
    }
    for (std::size_t i = 0u; i < tracks_.size(); ++i) {
        const auto& tr = tracks_[i];
        if (tr.kind != AnimationTrackKind::Inactive &&
            tr.target.Get() == target &&
            tr.propId == propId) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return InvalidTrackId;
}

std::uint16_t AnimationEngine::AcquireTrackSlot(
    UIElement* target,
    std::uint16_t propId,
    bool preserveExistingSnapshotAndVelocity,
    Float4Value& outPreservedPos,
    Float4Value& outPreservedVel,
    bool& outWasActiveSpring) {
    outPreservedPos = Float4Value{};
    outPreservedVel = Float4Value{};
    outWasActiveSpring = false;

    const std::uint16_t existingId = FindActiveTrackId(target, propId);
    if (existingId != InvalidTrackId) {
        AnimationTrack& existing = tracks_[existingId];
        if (preserveExistingSnapshotAndVelocity) {
            outPreservedPos = existing.posF4;
            outPreservedVel = existing.velF4;
            outWasActiveSpring =
                (existing.kind == AnimationTrackKind::SpringFloat ||
                 existing.kind == AnimationTrackKind::SpringFloat4);
            // Reuse existing slot in-place while keeping its original baseSnapshot!
            const bool savedHadBase = existing.hadBaseSnapshot;
            const ValueSourceRank savedBaseRank = existing.baseRank;
            UnifiedValue savedSnapshot = std::move(existing.baseSnapshot);
            existing = AnimationTrack{};
            existing.trackId = existingId;
            existing.propId = propId;
            existing.target.Reset(target, true);
            existing.hadBaseSnapshot = savedHadBase;
            existing.baseRank = savedBaseRank;
            existing.baseSnapshot = std::move(savedSnapshot);
            return existingId;
        }
        ReleaseTrackSlot(existing, false);
    }

    std::uint16_t slotId = InvalidTrackId;
    if (!freeTrackIds_.empty()) {
        slotId = freeTrackIds_.back();
        freeTrackIds_.pop_back();
    } else {
        slotId = static_cast<std::uint16_t>(tracks_.size());
        tracks_.emplace_back();
    }

    AnimationTrack& tr = tracks_[slotId];
    tr = AnimationTrack{};
    tr.trackId = slotId;
    tr.propId = propId;
    tr.target.Reset(target, true);

    // Snapshot pre-animation base value & rank so FillBehavior::Stop or StopPropertyAnimation restores cleanly
    if (target) {
        if (IsFastInlinePropertyId(propId)) {
            const ValueSourceRank curRank = target->GetPropertySourceRank(propId);
            tr.hadBaseSnapshot = (curRank != ValueSourceRank::Default);
            tr.baseRank = curRank;
            tr.baseSnapshot = target->GetValue(propId);
        } else if (const auto* entry = target->FindCompactEntry(propId)) {
            if (!std::holds_alternative<std::monostate>(entry->effectiveValue) &&
                entry->sourceRank != static_cast<std::uint8_t>(ValueSourceRank::Default)) {
                tr.hadBaseSnapshot = true;
                tr.baseRank = static_cast<ValueSourceRank>(entry->sourceRank);
                tr.baseSnapshot = entry->effectiveValue;
            }
            (void)target->SetPropertyMetadata(propId, entry->bindingSlotId, slotId);
        } else {
            (void)target->SetPropertyMetadata(propId, 0xFFFFu, slotId);
        }
    }

    ++activeCount_;
    return slotId;
}

void AnimationEngine::ReleaseTrackSlot(AnimationTrack& track, bool restoreSnapshot) noexcept {
    if (track.kind == AnimationTrackKind::Inactive) {
        return;
    }
    UIElement* target = track.target.Get();
    const std::uint16_t propId = track.propId;
    const std::uint16_t id = track.trackId;

    if (target && !IsFastInlinePropertyId(propId)) {
        if (const auto* entry = target->FindCompactEntry(propId)) {
            if (entry->animationTrackId == id) {
                (void)target->SetPropertyMetadata(propId, entry->bindingSlotId, InvalidTrackId);
            }
        }
    }

    if (target && restoreSnapshot) {
        if (track.hadBaseSnapshot) {
            (void)target->ForceEffectiveValue(propId, track.baseSnapshot, track.baseRank);
        } else if (IsFastInlinePropertyId(propId)) {
            (void)target->ClearValue(propId, ValueSourceRank::Animation);
        } else {
            (void)target->ForceClearSparseEntry(propId);
        }
    }

    track.kind = AnimationTrackKind::Inactive;
    track.target.Reset();
    track.baseSnapshot = std::monostate{};
    freeTrackIds_.push_back(id);
    if (activeCount_ > 0u) {
        --activeCount_;
    }
}

std::uint16_t AnimationEngine::StartFloatTween(
    UIElement* target,
    std::uint16_t propId,
    float fromValue,
    float toValue,
    float durationSeconds,
    EasingFunctionKind easingKind,
    EasingMode easingMode,
    FillBehavior fillBehavior,
    float beginTimeSeconds,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!target) {
        return InvalidTrackId;
    }
    Float4Value dummyPos{}, dummyVel{};
    bool dummySpring = false;
    const std::uint16_t id = AcquireTrackSlot(target, propId, true, dummyPos, dummyVel, dummySpring);
    AnimationTrack& tr = tracks_[id];
    tr.kind = AnimationTrackKind::TweenFloat;
    tr.easingKind = easingKind;
    tr.easingMode = easingMode;
    tr.fillBehavior = fillBehavior;
    tr.autoReverse = autoReverse;
    tr.beginTime = std::max(0.0f, beginTimeSeconds);
    tr.duration = std::max(0.0f, durationSeconds);
    tr.elapsed = 0.0f;
    tr.repeatCount = repeatCount;
    tr.fromF4.x = fromValue;
    tr.toF4.x = toValue;
    tr.posF4.x = fromValue;

    if (!IsFastInlinePropertyId(propId)) {
        const std::uint16_t bindSlot = target->FindCompactEntry(propId)
            ? target->FindCompactEntry(propId)->bindingSlotId
            : 0xFFFFu;
        (void)target->SetPropertyMetadata(propId, bindSlot, id);
    }
    if (tr.beginTime <= 0.0f) {
        (void)target->SetValue(propId, MakeFloat(fromValue), ValueSourceRank::Animation);
    }
    return id;
}

std::uint16_t AnimationEngine::StartFloat4Tween(
    UIElement* target,
    std::uint16_t propId,
    Float4Value fromValue,
    Float4Value toValue,
    float durationSeconds,
    EasingFunctionKind easingKind,
    EasingMode easingMode,
    FillBehavior fillBehavior,
    float beginTimeSeconds,
    bool autoReverse,
    std::int32_t repeatCount) {
    if (!target) {
        return InvalidTrackId;
    }
    Float4Value dummyPos{}, dummyVel{};
    bool dummySpring = false;
    const std::uint16_t id = AcquireTrackSlot(target, propId, true, dummyPos, dummyVel, dummySpring);
    AnimationTrack& tr = tracks_[id];
    tr.kind = AnimationTrackKind::TweenFloat4;
    tr.easingKind = easingKind;
    tr.easingMode = easingMode;
    tr.fillBehavior = fillBehavior;
    tr.autoReverse = autoReverse;
    tr.beginTime = std::max(0.0f, beginTimeSeconds);
    tr.duration = std::max(0.0f, durationSeconds);
    tr.elapsed = 0.0f;
    tr.repeatCount = repeatCount;
    tr.fromF4 = fromValue;
    tr.toF4 = toValue;
    tr.posF4 = fromValue;

    if (!IsFastInlinePropertyId(propId)) {
        const std::uint16_t bindSlot = target->FindCompactEntry(propId)
            ? target->FindCompactEntry(propId)->bindingSlotId
            : 0xFFFFu;
        (void)target->SetPropertyMetadata(propId, bindSlot, id);
    }
    if (tr.beginTime <= 0.0f) {
        (void)target->SetValue(propId, UnifiedValue{fromValue}, ValueSourceRank::Animation);
    }
    return id;
}

std::uint16_t AnimationEngine::RetargetSpringFloat(
    UIElement* target,
    std::uint16_t propId,
    float targetValue,
    const SpringConfig& spring) {
    if (!target) {
        return InvalidTrackId;
    }
    const float currentScalar = ReadCurrentScalar(target, propId);
    Float4Value prevPos{}, prevVel{};
    bool wasSpring = false;
    const std::uint16_t id = AcquireTrackSlot(target, propId, true, prevPos, prevVel, wasSpring);
    AnimationTrack& tr = tracks_[id];
    tr.kind = AnimationTrackKind::SpringFloat;
    tr.fillBehavior = FillBehavior::HoldEnd;
    tr.spring = spring;
    tr.posF4.x = wasSpring ? prevPos.x : currentScalar;
    tr.velF4.x = wasSpring ? prevVel.x : 0.0f;
    tr.toF4.x = targetValue;

    if (!IsFastInlinePropertyId(propId)) {
        const std::uint16_t bindSlot = target->FindCompactEntry(propId)
            ? target->FindCompactEntry(propId)->bindingSlotId
            : 0xFFFFu;
        (void)target->SetPropertyMetadata(propId, bindSlot, id);
    }
    return id;
}

std::uint16_t AnimationEngine::RetargetSpringFloat4(
    UIElement* target,
    std::uint16_t propId,
    Float4Value targetValue,
    const SpringConfig& spring) {
    if (!target) {
        return InvalidTrackId;
    }
    const Float4Value currentF4 = ReadCurrentFloat4(target, propId);
    Float4Value prevPos{}, prevVel{};
    bool wasSpring = false;
    const std::uint16_t id = AcquireTrackSlot(target, propId, true, prevPos, prevVel, wasSpring);
    AnimationTrack& tr = tracks_[id];
    tr.kind = AnimationTrackKind::SpringFloat4;
    tr.fillBehavior = FillBehavior::HoldEnd;
    tr.spring = spring;
    tr.posF4 = wasSpring ? prevPos : currentF4;
    tr.velF4 = wasSpring ? prevVel : Float4Value{};
    tr.toF4 = targetValue;

    if (!IsFastInlinePropertyId(propId)) {
        const std::uint16_t bindSlot = target->FindCompactEntry(propId)
            ? target->FindCompactEntry(propId)->bindingSlotId
            : 0xFFFFu;
        (void)target->SetPropertyMetadata(propId, bindSlot, id);
    }
    return id;
}

std::uint16_t AnimationEngine::StartValueTransition(
    UIElement* target,
    std::uint16_t propId,
    const UnifiedValue& toValue,
    float durationSeconds,
    EasingFunctionKind easingKind,
    EasingMode easingMode) {
    if (!target) {
        return InvalidTrackId;
    }
    const bool isFloat4 =
        std::holds_alternative<Float4Value>(toValue) ||
        propId == PropId_Background ||
        propId == PropId_Foreground ||
        propId == PropId_BorderBrush ||
        propId == PropId_Margin ||
        propId == PropId_Padding ||
        propId == PropId_CornerRadius;

    if (isFloat4) {
        const Float4Value cur = ReadCurrentFloat4(target, propId);
        const Float4Value toV = std::holds_alternative<Float4Value>(toValue)
            ? std::get<Float4Value>(toValue)
            : cur;
        return StartFloat4Tween(
            target,
            propId,
            cur,
            toV,
            durationSeconds,
            easingKind,
            easingMode,
            FillBehavior::HoldEnd);
    }

    const float cur = ReadCurrentScalar(target, propId);
    float toS = cur;
    (void)TryGetFloat(toValue, toS);
    return StartFloatTween(
        target,
        propId,
        cur,
        toS,
        durationSeconds,
        easingKind,
        easingMode,
        FillBehavior::HoldEnd);
}

std::uint16_t AnimationEngine::StartTimeline(
    UIElement* scopeRoot,
    const AxbAnimationTimeline& timeline) {
    if (!scopeRoot) {
        return InvalidTrackId;
    }
    UIElement* target = timeline.targetName.empty()
        ? scopeRoot
        : scopeRoot->FindName(timeline.targetName);
    if (!target) {
        return InvalidTrackId;
    }

    if (!timeline.keyFrames.empty()) {
        Float4Value dummyPos{}, dummyVel{};
        bool dummySpring = false;
        const std::uint16_t id = AcquireTrackSlot(
            target, timeline.targetPropertyId, true, dummyPos, dummyVel, dummySpring);
        AnimationTrack& tr = tracks_[id];
        tr.kind = AnimationTrackKind::KeyFrames;
        tr.fillBehavior = timeline.fillBehavior;
        tr.autoReverse = timeline.autoReverse;
        tr.beginTime = std::max(0.0f, timeline.beginTimeSeconds);
        tr.repeatCount = timeline.repeatCount;
        tr.elapsed = 0.0f;

        const std::size_t kfCount = std::min(timeline.keyFrames.size(), kMaxInlineKeyFrames);
        tr.keyFrameCount = static_cast<std::uint8_t>(kfCount);
        for (std::size_t i = 0u; i < kfCount; ++i) {
            const auto& srcKf = timeline.keyFrames[i];
            float dummyScalar = 0.0f;
            const bool isInterpolable =
                std::holds_alternative<Float4Value>(srcKf.value) ||
                (!std::holds_alternative<bool>(srcKf.value) &&
                 !std::holds_alternative<std::int64_t>(srcKf.value) &&
                 TryGetFloat(srcKf.value, dummyScalar));
            tr.keyFrames[i] = InlineTrackKeyFrame{
                srcKf.timeSeconds,
                srcKf.value,
                srcKf.easingKind,
                srcKf.easingMode,
                srcKf.isDiscrete || !isInterpolable};
        }
        std::sort(
            tr.keyFrames.begin(),
            tr.keyFrames.begin() + static_cast<std::ptrdiff_t>(kfCount),
            [](const InlineTrackKeyFrame& a, const InlineTrackKeyFrame& b) {
                return a.timeSeconds < b.timeSeconds;
            });
        const float maxTime = (kfCount > 0u) ? tr.keyFrames[kfCount - 1u].timeSeconds : 0.0f;
        tr.duration = (maxTime > 0.0f) ? maxTime : std::max(0.0f, timeline.durationSeconds);

        // Seed initial From value for segment before keyframe[0] when keyframe[0].timeSeconds > 0
        if (kfCount > 0u && std::holds_alternative<Float4Value>(tr.keyFrames[0].value)) {
            tr.fromF4 = ReadCurrentFloat4(target, timeline.targetPropertyId);
        } else {
            tr.fromF4.x = ReadCurrentScalar(target, timeline.targetPropertyId);
        }

        if (!IsFastInlinePropertyId(timeline.targetPropertyId)) {
            const std::uint16_t bindSlot = target->FindCompactEntry(timeline.targetPropertyId)
                ? target->FindCompactEntry(timeline.targetPropertyId)->bindingSlotId
                : 0xFFFFu;
            (void)target->SetPropertyMetadata(timeline.targetPropertyId, bindSlot, id);
        }
        if (tr.beginTime <= 0.0f && kfCount > 0u && tr.keyFrames[0].timeSeconds <= 0.0f) {
            (void)target->SetValue(
                timeline.targetPropertyId,
                tr.keyFrames[0].value,
                ValueSourceRank::Animation);
        }
        return id;
    }

    // Simple From -> To Tween (Float4 or Float)
    const bool isFloat4 =
        (timeline.hasFrom && std::holds_alternative<Float4Value>(timeline.fromValue)) ||
        (timeline.hasTo && std::holds_alternative<Float4Value>(timeline.toValue)) ||
        timeline.targetPropertyId == PropId_Background ||
        timeline.targetPropertyId == PropId_Foreground ||
        timeline.targetPropertyId == PropId_BorderBrush ||
        timeline.targetPropertyId == PropId_Margin ||
        timeline.targetPropertyId == PropId_Padding ||
        timeline.targetPropertyId == PropId_CornerRadius;

    if (isFloat4) {
        const Float4Value cur = ReadCurrentFloat4(target, timeline.targetPropertyId);
        const Float4Value fromV = (timeline.hasFrom && std::holds_alternative<Float4Value>(timeline.fromValue))
            ? std::get<Float4Value>(timeline.fromValue)
            : cur;
        Float4Value toV = cur;
        if (timeline.hasTo && std::holds_alternative<Float4Value>(timeline.toValue)) {
            const Float4Value rawTo = std::get<Float4Value>(timeline.toValue);
            toV = timeline.isBy
                ? Float4Value{fromV.x + rawTo.x, fromV.y + rawTo.y, fromV.z + rawTo.z, fromV.w + rawTo.w}
                : rawTo;
        }
        return StartFloat4Tween(
            target,
            timeline.targetPropertyId,
            fromV,
            toV,
            timeline.durationSeconds,
            timeline.easingKind,
            timeline.easingMode,
            timeline.fillBehavior,
            timeline.beginTimeSeconds,
            timeline.autoReverse,
            timeline.repeatCount);
    }

    const float cur = ReadCurrentScalar(target, timeline.targetPropertyId);
    float fromS = cur;
    if (timeline.hasFrom) {
        (void)TryGetFloat(timeline.fromValue, fromS);
    }
    float toS = cur;
    if (timeline.hasTo && TryGetFloat(timeline.toValue, toS)) {
        if (timeline.isBy) {
            toS = fromS + toS;
        }
    }
    return StartFloatTween(
        target,
        timeline.targetPropertyId,
        fromS,
        toS,
        timeline.durationSeconds,
        timeline.easingKind,
        timeline.easingMode,
        timeline.fillBehavior,
        timeline.beginTimeSeconds,
        timeline.autoReverse,
        timeline.repeatCount);
}

std::size_t AnimationEngine::BeginStoryboard(
    UIElement* scopeRoot,
    const AxbStoryboard& storyboard) {
    if (!scopeRoot) {
        return 0u;
    }
    std::size_t started = 0u;
    for (const auto& tl : storyboard.timelines) {
        if (StartTimeline(scopeRoot, tl) != InvalidTrackId) {
            ++started;
        }
    }
    return started;
}

bool AnimationEngine::StopPropertyAnimation(
    UIElement* target,
    std::uint16_t propId,
    bool restoreBaseValue) noexcept {
    const std::uint16_t id = FindActiveTrackId(target, propId);
    if (id == InvalidTrackId) {
        return false;
    }
    ReleaseTrackSlot(tracks_[id], restoreBaseValue);
    return true;
}

float AnimationEngine::GetTrackVelocityX(std::uint16_t trackId) const noexcept {
    if (trackId < tracks_.size() && tracks_[trackId].kind != AnimationTrackKind::Inactive) {
        return tracks_[trackId].velF4.x;
    }
    return 0.0f;
}

std::size_t AnimationEngine::Tick(float dtSeconds) noexcept {
    if (activeCount_ == 0u || dtSeconds < 0.0f) {
        return 0u;
    }

    std::size_t updates = 0u;
    for (std::size_t i = 0u; i < tracks_.size(); ++i) {
        AnimationTrack& tr = tracks_[i];
        if (tr.kind == AnimationTrackKind::Inactive) {
            continue;
        }
        UIElement* target = tr.target.Get();
        if (!target) {
            ReleaseTrackSlot(tr, false);
            continue;
        }

        // 1. Second-Order Spring Physics Integration (sub-stepped for unconditional stability at any dt)
        if (tr.kind == AnimationTrackKind::SpringFloat ||
            tr.kind == AnimationTrackKind::SpringFloat4) {
            const float safeMass = std::max(0.01f, tr.spring.mass);
            const float k = std::max(0.0f, tr.spring.stiffness);
            const float c = std::max(0.0f, tr.spring.damping);
            float remDt = std::min(dtSeconds, 0.25f);
            constexpr float kMaxStep = 1.0f / 240.0f;

            while (remDt > 1e-7f) {
                const float h = std::min(remDt, kMaxStep);
                remDt -= h;
                auto stepAxis = [&](float& pos, float& vel, float goal) noexcept {
                    const float force = -k * (pos - goal) - c * vel;
                    const float acc = force / safeMass;
                    vel += acc * h;
                    pos += vel * h;
                };
                stepAxis(tr.posF4.x, tr.velF4.x, tr.toF4.x);
                if (tr.kind == AnimationTrackKind::SpringFloat4) {
                    stepAxis(tr.posF4.y, tr.velF4.y, tr.toF4.y);
                    stepAxis(tr.posF4.z, tr.velF4.z, tr.toF4.z);
                    stepAxis(tr.posF4.w, tr.velF4.w, tr.toF4.w);
                }
            }

            if (tr.kind == AnimationTrackKind::SpringFloat) {
                const bool atRest =
                    std::fabs(tr.posF4.x - tr.toF4.x) <= tr.spring.restPositionEpsilon &&
                    std::fabs(tr.velF4.x) <= tr.spring.restVelocityEpsilon;
                if (atRest) {
                    tr.posF4.x = tr.toF4.x;
                    tr.velF4.x = 0.0f;
                    (void)target->SetValue(tr.propId, MakeFloat(tr.toF4.x), ValueSourceRank::Animation);
                    ReleaseTrackSlot(tr, false);
                } else {
                    (void)target->SetValue(tr.propId, MakeFloat(tr.posF4.x), ValueSourceRank::Animation);
                }
                ++updates;
            } else {
                const float maxPosErr = std::max({
                    std::fabs(tr.posF4.x - tr.toF4.x),
                    std::fabs(tr.posF4.y - tr.toF4.y),
                    std::fabs(tr.posF4.z - tr.toF4.z),
                    std::fabs(tr.posF4.w - tr.toF4.w)});
                const float maxVel = std::max({
                    std::fabs(tr.velF4.x),
                    std::fabs(tr.velF4.y),
                    std::fabs(tr.velF4.z),
                    std::fabs(tr.velF4.w)});
                if (maxPosErr <= tr.spring.restPositionEpsilon && maxVel <= tr.spring.restVelocityEpsilon) {
                    tr.posF4 = tr.toF4;
                    tr.velF4 = Float4Value{};
                    (void)target->SetValue(tr.propId, UnifiedValue{tr.toF4}, ValueSourceRank::Animation);
                    ReleaseTrackSlot(tr, false);
                } else {
                    (void)target->SetValue(tr.propId, UnifiedValue{tr.posF4}, ValueSourceRank::Animation);
                }
                ++updates;
            }
            continue;
        }

        // 2. Timed Tween & Multi-KeyFrame Tracks
        tr.elapsed += dtSeconds;
        if (tr.elapsed < tr.beginTime) {
            continue;
        }

        const float activeTime = tr.elapsed - tr.beginTime;
        const float singleCycleDur = tr.duration;
        const float fullCycleDur = tr.autoReverse ? (singleCycleDur * 2.0f) : singleCycleDur;

        bool finished = false;
        float localCycleTime = 0.0f;

        if (singleCycleDur <= 1e-6f) {
            localCycleTime = singleCycleDur;
            finished = (tr.repeatCount > 0);
        } else {
            if (tr.repeatCount > 0) {
                const float totalDuration = fullCycleDur * static_cast<float>(tr.repeatCount);
                if (activeTime >= totalDuration) {
                    finished = true;
                    localCycleTime = tr.autoReverse ? 0.0f : singleCycleDur;
                }
            }
            if (!finished) {
                const float inCycle = std::fmod(activeTime, fullCycleDur);
                localCycleTime = (tr.autoReverse && inCycle > singleCycleDur)
                    ? (fullCycleDur - inCycle)
                    : inCycle;
            }
        }

        if (tr.kind == AnimationTrackKind::TweenFloat) {
            const float normT = (singleCycleDur > 1e-6f)
                ? std::clamp(localCycleTime / singleCycleDur, 0.0f, 1.0f)
                : 1.0f;
            const float eased = EvaluateEasing(tr.easingKind, tr.easingMode, normT);
            const float val = tr.fromF4.x + (tr.toF4.x - tr.fromF4.x) * eased;
            tr.posF4.x = val;
            (void)target->SetValue(tr.propId, MakeFloat(val), ValueSourceRank::Animation);
            ++updates;
        } else if (tr.kind == AnimationTrackKind::TweenFloat4) {
            const float normT = (singleCycleDur > 1e-6f)
                ? std::clamp(localCycleTime / singleCycleDur, 0.0f, 1.0f)
                : 1.0f;
            const float eased = EvaluateEasing(tr.easingKind, tr.easingMode, normT);
            const Float4Value val = LerpFloat4(tr.fromF4, tr.toF4, eased);
            tr.posF4 = val;
            (void)target->SetValue(tr.propId, UnifiedValue{val}, ValueSourceRank::Animation);
            ++updates;
        } else if (tr.kind == AnimationTrackKind::KeyFrames && tr.keyFrameCount > 0u) {
            // Locate active segment [prevTime .. kf[k].timeSeconds]
            std::size_t segIdx = 0u;
            while (segIdx + 1u < tr.keyFrameCount &&
                   localCycleTime > tr.keyFrames[segIdx].timeSeconds) {
                ++segIdx;
            }
            const InlineTrackKeyFrame& endKf = tr.keyFrames[segIdx];
            const float segStartT = (segIdx == 0u) ? 0.0f : tr.keyFrames[segIdx - 1u].timeSeconds;
            const float segSpan = endKf.timeSeconds - segStartT;
            const float segNorm = (segSpan > 1e-6f)
                ? std::clamp((localCycleTime - segStartT) / segSpan, 0.0f, 1.0f)
                : 1.0f;

            if (endKf.isDiscrete) {
                if (segNorm >= 1.0f || segSpan <= 1e-6f) {
                    (void)target->SetValue(tr.propId, endKf.value, ValueSourceRank::Animation);
                } else if (segIdx > 0u) {
                    (void)target->SetValue(
                        tr.propId, tr.keyFrames[segIdx - 1u].value, ValueSourceRank::Animation);
                }
            } else if (const auto* endF4 = std::get_if<Float4Value>(&endKf.value)) {
                Float4Value startF4 = tr.fromF4;
                if (segIdx > 0u) {
                    if (const auto* pf4 = std::get_if<Float4Value>(&tr.keyFrames[segIdx - 1u].value)) {
                        startF4 = *pf4;
                    }
                }
                const float eased = EvaluateEasing(endKf.easingKind, endKf.easingMode, segNorm);
                const Float4Value curVal = LerpFloat4(startF4, *endF4, eased);
                tr.posF4 = curVal;
                (void)target->SetValue(tr.propId, UnifiedValue{curVal}, ValueSourceRank::Animation);
            } else {
                float endS = 0.0f;
                (void)TryGetFloat(endKf.value, endS);
                float startS = tr.fromF4.x;
                if (segIdx > 0u) {
                    (void)TryGetFloat(tr.keyFrames[segIdx - 1u].value, startS);
                }
                const float eased = EvaluateEasing(endKf.easingKind, endKf.easingMode, segNorm);
                const float curVal = startS + (endS - startS) * eased;
                tr.posF4.x = curVal;
                (void)target->SetValue(tr.propId, MakeFloat(curVal), ValueSourceRank::Animation);
            }
            ++updates;
        }

        if (finished) {
            const bool restoreBase = (tr.fillBehavior == FillBehavior::Stop);
            ReleaseTrackSlot(tr, restoreBase);
        }
    }

    return updates;
}

} // namespace aero
