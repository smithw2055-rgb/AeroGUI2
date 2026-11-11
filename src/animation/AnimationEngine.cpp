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

[[nodiscard]] std::uint64_t HashNameFnv1a64(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char ch : text) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] bool IsInVisualSubtree(const UIElement* candidate, const UIElement* root) noexcept {
    for (const UIElement* cur = candidate; cur != nullptr; cur = cur->Parent()) {
        if (cur == root) {
            return true;
        }
    }
    return false;
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

float EvaluateCubicBezier1D(
    float x1,
    float y1,
    float x2,
    float y2,
    float normalizedTime01) noexcept {
    const float t = std::clamp(normalizedTime01, 0.0f, 1.0f);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float cx1 = std::clamp(x1, 0.0f, 1.0f);
    const float cx2 = std::clamp(x2, 0.0f, 1.0f);
    if (std::fabs(cx1 - y1) <= 1e-5f && std::fabs(cx2 - y2) <= 1e-5f) {
        return t;
    }

    // Solve B_x(u) = 3(1-u)^2 u x1 + 3(1-u) u^2 x2 + u^3 = t
    // Polynomial form: ((ax * u + bx) * u + cx) * u
    const float cx = 3.0f * cx1;
    const float bx = 3.0f * (cx2 - cx1) - cx;
    const float ax = 1.0f - cx - bx;

    auto sampleX = [&](float u) noexcept -> float {
        return ((ax * u + bx) * u + cx) * u;
    };
    auto sampleDerivativeX = [&](float u) noexcept -> float {
        return (3.0f * ax * u + 2.0f * bx) * u + cx;
    };

    float u = t;
    bool converged = false;
    for (int iter = 0; iter < 4; ++iter) {
        const float err = sampleX(u) - t;
        if (std::fabs(err) < 1e-5f) {
            converged = true;
            break;
        }
        const float d = sampleDerivativeX(u);
        if (std::fabs(d) < 1e-4f) {
            break;
        }
        u = std::clamp(u - err / d, 0.0f, 1.0f);
    }

    if (!converged) {
        float lo = 0.0f;
        float hi = 1.0f;
        u = t;
        for (int iter = 0; iter < 5; ++iter) {
            const float xVal = sampleX(u);
            if (std::fabs(xVal - t) < 1e-5f) {
                break;
            }
            if (t > xVal) {
                lo = u;
            } else {
                hi = u;
            }
            u = (lo + hi) * 0.5f;
        }
    }

    const float cy = 3.0f * y1;
    const float by = 3.0f * (y2 - y1) - cy;
    const float ay = 1.0f - cy - by;
    return ((ay * u + by) * u + cy) * u;
}

AnimationEngine::AnimationEngine(std::size_t initialReservedTracks) {
    tracks_.reserve(initialReservedTracks);
    freeTrackIds_.reserve(initialReservedTracks);
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkValuesInPools();
    });
}

AnimationEngine::~AnimationEngine() noexcept {
    rs_rt::unregisterExternalValuePoolRootMarker(this);
}

void AnimationEngine::MarkValuesInPools() const noexcept {
    for (const auto& tr : tracks_) {
        if (tr.kind == AnimationTrackKind::Inactive) {
            continue;
        }
        if (tr.hadBaseSnapshot && tr.baseSnapshot.isTraceable()) {
            rs_rt::markValueInPools(tr.baseSnapshot);
        }
        for (std::uint8_t k = 0u; k < tr.keyFrameCount && k < kMaxInlineKeyFrames; ++k) {
            if (tr.keyFrames[k].value.isTraceable()) {
                rs_rt::markValueInPools(tr.keyFrames[k].value);
            }
        }
    }
    for (const auto& sm : stateMachines_) {
        if (sm.motionData) {
            sm.motionData->MarkValuesInPools();
        }
        for (const auto& ch : sm.channelScratch) {
            if (ch.hadBaseSnapshot && ch.baseSnapshot.isTraceable()) {
                rs_rt::markValueInPools(ch.baseSnapshot);
            }
        }
    }
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
    stateMachines_.clear();
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
    Float4Value f4{};
    if (v.tryGetFloat4(f4)) {
        return f4;
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
            if (tr.baseSnapshot.isTraceable()) {
                rs_rt::pinValueInPools(tr.baseSnapshot);
            }
            target->SetFastPropertyAnimActive(propId, true);
        } else if (const auto* entry = target->FindCompactEntry(propId)) {
            if (!std::holds_alternative<std::monostate>(entry->effectiveValue) &&
                entry->sourceRank != static_cast<std::uint8_t>(ValueSourceRank::Default)) {
                tr.hadBaseSnapshot = true;
                tr.baseRank = static_cast<ValueSourceRank>(entry->sourceRank);
                tr.baseSnapshot = entry->effectiveValue;
                if (tr.baseSnapshot.isTraceable()) {
                    rs_rt::pinValueInPools(tr.baseSnapshot);
                }
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

    if (target) {
        if (IsFastInlinePropertyId(propId)) {
            target->SetFastPropertyAnimActive(propId, false);
        } else if (const auto* entry = target->FindCompactEntry(propId)) {
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
        (void)target->SetValue(propId, MakeFloat4InlineFast(fromValue), ValueSourceRank::Animation);
    }
    if (wakeHostCallback_) {
        wakeHostCallback_();
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
    if (! target) {
        return InvalidTrackId;
    }
    Float4Value toF4{};
    const bool hasToF4 = toValue.tryGetFloat4(toF4);
    const bool isFloat4 =
        hasToF4 ||
        propId == PropId_Background ||
        propId == PropId_Foreground ||
        propId == PropId_BorderBrush ||
        propId == PropId_Margin ||
        propId == PropId_Padding ||
        propId == PropId_CornerRadius;

    if (isFloat4) {
        const Float4Value cur = ReadCurrentFloat4(target, propId);
        const Float4Value toV = hasToF4 ? toF4 : cur;
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
    if (!target && !timeline.targetName.empty()) {
        UIElement* treeRoot = scopeRoot;
        while (treeRoot->Parent() != nullptr) {
            treeRoot = treeRoot->Parent();
        }
        if (treeRoot != scopeRoot) {
            target = treeRoot->FindName(timeline.targetName);
        }
    }
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
            Float4Value dummyF4{};
            const bool isInterpolable =
                srcKf.value.tryGetFloat4(dummyF4) ||
                (!std::holds_alternative<bool>(srcKf.value) &&
                 !std::holds_alternative<std::int64_t>(srcKf.value) &&
                 TryGetFloat(srcKf.value, dummyScalar));
            UnifiedValue kfVal = srcKf.value;
            if (kfVal.isTraceable()) {
                rs_rt::pinValueInPools(kfVal);
            }
            tr.keyFrames[i] = InlineTrackKeyFrame{
                srcKf.timeSeconds,
                std::move(kfVal),
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
        Float4Value firstKfF4{};
        if (kfCount > 0u && tr.keyFrames[0].value.tryGetFloat4(firstKfF4)) {
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
    Float4Value parsedFromF4{};
    Float4Value parsedToF4{};
    const bool hasFromF4 = timeline.hasFrom && timeline.fromValue.tryGetFloat4(parsedFromF4);
    const bool hasToF4 = timeline.hasTo && timeline.toValue.tryGetFloat4(parsedToF4);
    const bool isFloat4 =
        hasFromF4 ||
        hasToF4 ||
        timeline.targetPropertyId == PropId_Background ||
        timeline.targetPropertyId == PropId_Foreground ||
        timeline.targetPropertyId == PropId_BorderBrush ||
        timeline.targetPropertyId == PropId_Margin ||
        timeline.targetPropertyId == PropId_Padding ||
        timeline.targetPropertyId == PropId_CornerRadius;

    if (isFloat4) {
        const Float4Value cur = ReadCurrentFloat4(target, timeline.targetPropertyId);
        const Float4Value fromV = hasFromF4 ? parsedFromF4 : cur;
        Float4Value toV = cur;
        if (hasToF4) {
            toV = timeline.isBy
                ? Float4Value{
                      fromV.x + parsedToF4.x,
                      fromV.y + parsedToF4.y,
                      fromV.z + parsedToF4.z,
                      fromV.w + parsedToF4.w}
                : parsedToF4;
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

namespace {

[[nodiscard]] UIElement* ResolveStoryboardTimelineTarget(
    UIElement* scopeRoot,
    std::string_view targetName) noexcept {
    if (!scopeRoot) {
        return nullptr;
    }
    if (targetName.empty()) {
        return scopeRoot;
    }
    if (UIElement* local = scopeRoot->FindName(targetName)) {
        return local;
    }
    UIElement* treeRoot = scopeRoot;
    while (treeRoot->Parent() != nullptr) {
        treeRoot = treeRoot->Parent();
    }
    return (treeRoot != scopeRoot) ? treeRoot->FindName(targetName) : nullptr;
}

} // namespace

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

std::size_t AnimationEngine::StopStoryboard(
    UIElement* scopeRoot,
    const AxbStoryboard& storyboard,
    bool restoreBaseValue) noexcept {
    if (!scopeRoot) {
        return 0u;
    }
    std::size_t stopped = 0u;
    for (const auto& tl : storyboard.timelines) {
        UIElement* target = ResolveStoryboardTimelineTarget(scopeRoot, tl.targetName);
        if (target && StopPropertyAnimation(target, tl.targetPropertyId, restoreBaseValue)) {
            ++stopped;
        }
    }
    return stopped;
}

std::size_t AnimationEngine::TogglePlayPauseStoryboard(
    UIElement* scopeRoot,
    const AxbStoryboard& storyboard) {
    if (!scopeRoot) {
        return 0u;
    }
    bool anyActive = false;
    bool anyUnpaused = false;
    for (const auto& tl : storyboard.timelines) {
        UIElement* target = ResolveStoryboardTimelineTarget(scopeRoot, tl.targetName);
        if (!target) {
            continue;
        }
        const std::uint16_t id = FindActiveTrackId(target, tl.targetPropertyId);
        if (id != InvalidTrackId && id < tracks_.size() &&
            tracks_[id].kind != AnimationTrackKind::Inactive) {
            anyActive = true;
            if (!tracks_[id].paused) {
                anyUnpaused = true;
            }
        }
    }

    if (!anyActive) {
        return BeginStoryboard(scopeRoot, storyboard);
    }

    const bool nextPaused = anyUnpaused;
    std::size_t toggled = 0u;
    for (const auto& tl : storyboard.timelines) {
        UIElement* target = ResolveStoryboardTimelineTarget(scopeRoot, tl.targetName);
        if (!target) {
            continue;
        }
        const std::uint16_t id = FindActiveTrackId(target, tl.targetPropertyId);
        if (id != InvalidTrackId && id < tracks_.size() &&
            tracks_[id].kind != AnimationTrackKind::Inactive) {
            tracks_[id].paused = nextPaused;
            ++toggled;
        }
    }
    if (!nextPaused && toggled > 0u && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return toggled;
}

bool AnimationEngine::IsStoryboardActive(
    UIElement* scopeRoot,
    const AxbStoryboard& storyboard) const noexcept {
    if (!scopeRoot) {
        return false;
    }
    for (const auto& tl : storyboard.timelines) {
        UIElement* target = ResolveStoryboardTimelineTarget(scopeRoot, tl.targetName);
        if (!target) {
            continue;
        }
        const std::uint16_t id = FindActiveTrackId(target, tl.targetPropertyId);
        if (id != InvalidTrackId && id < tracks_.size() &&
            tracks_[id].kind != AnimationTrackKind::Inactive) {
            return true;
        }
    }
    return false;
}

bool AnimationEngine::IsStoryboardPaused(
    UIElement* scopeRoot,
    const AxbStoryboard& storyboard) const noexcept {
    if (!scopeRoot) {
        return false;
    }
    bool anyActive = false;
    for (const auto& tl : storyboard.timelines) {
        UIElement* target = ResolveStoryboardTimelineTarget(scopeRoot, tl.targetName);
        if (!target) {
            continue;
        }
        const std::uint16_t id = FindActiveTrackId(target, tl.targetPropertyId);
        if (id != InvalidTrackId && id < tracks_.size() &&
            tracks_[id].kind != AnimationTrackKind::Inactive) {
            anyActive = true;
            if (!tracks_[id].paused) {
                return false;
            }
        }
    }
    return anyActive;
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

std::size_t AnimationEngine::StopSubtreeAnimations(
    const UIElement* subtreeRoot,
    bool restoreBaseValue) noexcept {
    if (!subtreeRoot) {
        return 0u;
    }
    std::size_t stopped = 0u;
    for (auto& tr : tracks_) {
        if (tr.kind != AnimationTrackKind::Inactive &&
            tr.target && IsInVisualSubtree(tr.target.Get(), subtreeRoot)) {
            ReleaseTrackSlot(tr, restoreBaseValue);
            ++stopped;
        }
    }
    std::size_t smWriteIdx = 0u;
    for (std::size_t i = 0u; i < stateMachines_.size(); ++i) {
        auto& sm = stateMachines_[i];
        if (sm.rootControl && IsInVisualSubtree(sm.rootControl.Get(), subtreeRoot)) {
            if (restoreBaseValue) {
                for (const auto& ch : sm.channelScratch) {
                    if (ch.localElementIdx >= sm.localElements.size()) continue;
                    UIElement* elem = sm.localElements[ch.localElementIdx].Get();
                    if (!elem) continue;
                    if (IsFastInlinePropertyId(ch.propId)) {
                        elem->SetFastPropertyAnimActive(ch.propId, false);
                    }
                    if (ch.hadBaseSnapshot) {
                        (void)elem->ForceEffectiveValue(ch.propId, ch.baseSnapshot, ch.baseRank);
                    } else if (IsFastInlinePropertyId(ch.propId)) {
                        (void)elem->ClearValue(ch.propId, ValueSourceRank::Animation);
                    } else {
                        (void)elem->ForceClearSparseEntry(ch.propId);
                    }
                }
            }
            ++stopped;
            continue;
        }
        if (smWriteIdx != i) {
            stateMachines_[smWriteIdx] = std::move(sm);
        }
        ++smWriteIdx;
    }
    stateMachines_.resize(smWriteIdx);
    return stopped;
}

std::uint16_t AnimationEngine::StartCompiledClipTrack(
    UIElement* target,
    const AxbMotionTrackDesc& trackDesc,
    const AxbFlatKeyframe* keyframePool,
    std::size_t poolCount,
    float durationSeconds,
    bool isLooping,
    bool autoReverse,
    FillBehavior fillBehavior) {
    if (!target || !keyframePool || trackDesc.keyframeCount == 0u ||
        static_cast<std::size_t>(trackDesc.firstKeyframeIdx) + trackDesc.keyframeCount > poolCount) {
        return InvalidTrackId;
    }
    Float4Value dummyPos{}, dummyVel{};
    bool dummySpring = false;
    const std::uint16_t id = AcquireTrackSlot(
        target, trackDesc.targetPropId, true, dummyPos, dummyVel, dummySpring);
    AnimationTrack& tr = tracks_[id];
    tr.kind = AnimationTrackKind::CompiledClipSlice;
    tr.fillBehavior = fillBehavior;
    tr.autoReverse = autoReverse;
    tr.beginTime = 0.0f;
    tr.duration = std::max(0.0f, durationSeconds);
    tr.elapsed = 0.0f;
    tr.repeatCount = isLooping ? 0 : 1;
    tr.channelCount = trackDesc.channelCount;
    tr.quantizeInlineW = trackDesc.quantizeInlineW;
    tr.sliceFrames = keyframePool + trackDesc.firstKeyframeIdx;
    tr.sliceCount = trackDesc.keyframeCount;
    tr.cachedSegmentIdx = 0u;

    if (!IsFastInlinePropertyId(trackDesc.targetPropId)) {
        const std::uint16_t bindSlot = target->FindCompactEntry(trackDesc.targetPropId)
            ? target->FindCompactEntry(trackDesc.targetPropId)->bindingSlotId
            : 0xFFFFu;
        (void)target->SetPropertyMetadata(trackDesc.targetPropId, bindSlot, id);
    }

    const Float4Value initVal = tr.sliceFrames[0].value;
    tr.posF4 = initVal;
    if (tr.channelCount == 4u) {
        (void)target->SetValue(
            trackDesc.targetPropId,
            tr.quantizeInlineW ? MakeFloat4InlineFast(initVal) : UnifiedValue{initVal},
            ValueSourceRank::Animation);
    } else if (tr.channelCount == 0u) {
        (void)target->SetValue(
            trackDesc.targetPropId,
            value::FromInt(static_cast<std::int64_t>(std::llround(initVal.x))),
            ValueSourceRank::Animation);
    } else {
        (void)target->SetValue(
            trackDesc.targetPropId,
            MakeFloat(initVal.x),
            ValueSourceRank::Animation);
    }
    if (wakeHostCallback_) {
        wakeHostCallback_();
    }
    return id;
}

Float4Value AnimationEngine::EvaluateFlatTrackAtTime(
    const AxbMotionTrackDesc& trackDesc,
    const AxbFlatKeyframe* keyframePool,
    std::size_t poolCount,
    float localTime,
    std::uint32_t& ioCachedSegmentIdx) noexcept {
    if (!keyframePool || trackDesc.keyframeCount == 0u ||
        static_cast<std::size_t>(trackDesc.firstKeyframeIdx) + trackDesc.keyframeCount > poolCount) {
        return Float4Value{};
    }
    const AxbFlatKeyframe* frames = keyframePool + trackDesc.firstKeyframeIdx;
    const std::uint32_t count = trackDesc.keyframeCount;
    if (count == 1u || localTime <= frames[0].timeSec) {
        ioCachedSegmentIdx = 0u;
        return frames[0].value;
    }
    if (localTime >= frames[count - 1u].timeSec) {
        ioCachedSegmentIdx = count - 2u;
        return frames[count - 1u].value;
    }

    // O(1) amortized segment lookup exploiting monotonic time locality
    std::uint32_t seg = std::min(ioCachedSegmentIdx, count - 2u);
    if (localTime < frames[seg].timeSec) {
        while (seg > 0u && localTime < frames[seg].timeSec) {
            --seg;
        }
    } else {
        while (seg + 1u < count - 1u && localTime >= frames[seg + 1u].timeSec) {
            ++seg;
        }
    }
    ioCachedSegmentIdx = seg;

    const AxbFlatKeyframe& k0 = frames[seg];
    const AxbFlatKeyframe& k1 = frames[seg + 1u];
    const float span = k1.timeSec - k0.timeSec;
    if (span <= 1e-6f) {
        return k1.value;
    }
    const float normT = std::clamp((localTime - k0.timeSec) / span, 0.0f, 1.0f);
    const auto interp = static_cast<CurveInterpMode>(k0.interp);

    if (interp == CurveInterpMode::HoldStep || trackDesc.channelCount == 0u) {
        return (normT >= 1.0f) ? k1.value : k0.value;
    }

    float eased = normT;
    if (interp == CurveInterpMode::AnalyticalEase) {
        const auto ek = static_cast<EasingFunctionKind>(
            std::min<std::uint8_t>(k0.easeKindOrCtrlX1, static_cast<std::uint8_t>(EasingFunctionKind::Elastic)));
        const auto em = static_cast<EasingMode>(
            std::min<std::uint8_t>(k0.easeModeOrCtrlX2, static_cast<std::uint8_t>(EasingMode::EaseInOut)));
        eased = EvaluateEasing(ek, em, normT);
    } else if (interp == CurveInterpMode::CubicBezier) {
        const float x1 = static_cast<float>(k0.easeKindOrCtrlX1) / 255.0f;
        const float x2 = static_cast<float>(k0.easeModeOrCtrlX2) / 255.0f;
        const float y1 = static_cast<float>(k0.ctrlY1Fixed8) / 256.0f;
        const float y2 = static_cast<float>(k0.ctrlY2Fixed8) / 256.0f;
        eased = EvaluateCubicBezier1D(x1, y1, x2, y2, normT);
    }

    return LerpFloat4(k0.value, k1.value, eased);
}

bool AnimationEngine::InstantiatePackageMotion(
    UIElement* rootControl,
    UIElement* const* localElements,
    std::size_t localElementCount,
    const AxbPackage& package) {
    if (!rootControl || !package.HasMotionSection()) {
        return false;
    }

    auto pkgCopy = std::make_shared<AxbPackage>(package);
    pkgCopy->PinValuesInPools();

    // If the package has standalone motionClips without stateMachines, synthesize a default 1-layer state machine
    if (pkgCopy->stateMachines.empty() && !pkgCopy->motionClips.empty()) {
        AxbSmState st{};
        st.nameStrIdx = pkgCopy->motionClips[0].nameStrIdx;
        st.kind = SmStateKind::SingleClip;
        st.primaryClipIdx = 0u;
        st.speedMultiplier = 1.0f;
        pkgCopy->smStates.push_back(st);

        AxbSmLayerDesc ly{};
        ly.nameStrIdx = pkgCopy->InternString("DefaultClipLayer");
        ly.blendMode = SmLayerBlendMode::Override;
        ly.entryStateIdx = 0u;
        ly.layerWeight = 1.0f;
        ly.firstStateIdx = 0u;
        ly.stateCount = 1u;
        pkgCopy->smLayers.push_back(ly);

        AxbStateMachineDesc smDesc{};
        smDesc.nameStrIdx = pkgCopy->InternString("DefaultClipMachine");
        smDesc.firstLayerIdx = 0u;
        smDesc.layerCount = 1u;
        pkgCopy->stateMachines.push_back(smDesc);
    }

    bool anyInstantiated = false;
    for (std::size_t smIdx = 0u; smIdx < pkgCopy->stateMachines.size(); ++smIdx) {
        const auto& smDesc = pkgCopy->stateMachines[smIdx];
        MotionStateMachineInstance inst{};
        inst.rootControl.Reset(rootControl, true);
        inst.localElements.reserve(localElementCount);
        for (std::size_t i = 0u; i < localElementCount; ++i) {
            IntrusivePtr<UIElement> ptr;
            ptr.Reset(localElements ? localElements[i] : nullptr, true);
            inst.localElements.push_back(std::move(ptr));
        }
        if (inst.localElements.empty()) {
            inst.localElements.push_back(inst.rootControl);
        }
        inst.motionData = pkgCopy;
        inst.machineDescIdx = static_cast<std::uint32_t>(smIdx);
        inst.machineNameHash = HashNameFnv1a64(pkgCopy->GetString(smDesc.nameStrIdx));
        inst.isSettled = false;
        inst.needsInitialEval = true;
        inst.trackSegmentCache.assign(pkgCopy->motionTracks.size(), 0u);

        // Initialize ContractInputBank slots & bridge properties on rootControl
        inst.contractInputs.reserve(pkgCopy->contractInputs.size());
        for (std::size_t ciIdx = 0u; ciIdx < pkgCopy->contractInputs.size(); ++ciIdx) {
            const auto& ciDesc = pkgCopy->contractInputs[ciIdx];
            ContractInputRuntimeSlot slot{};
            slot.name = std::string(pkgCopy->GetString(ciDesc.nameStrIdx));
            slot.nameHash = HashNameFnv1a64(slot.name);
            slot.valueKind = ciDesc.valueKind;
            slot.isTrigger = (ciDesc.isTrigger != 0u);
            slot.dirtyThisFrame = true;
            slot.bridgePropId = (ciDesc.bridgePropId != 0xFFFFu)
                ? ciDesc.bridgePropId
                : static_cast<std::uint16_t>(PropId_FirstContractInput + ciIdx);
            if (!TryGetFloat(ciDesc.defaultValue, slot.scalarValue)) {
                if (const auto* b = std::get_if<bool>(&ciDesc.defaultValue)) {
                    slot.scalarValue = *b ? 1.0f : 0.0f;
                }
            }
            (void)ciDesc.defaultValue.tryGetFloat4(slot.float4Value);
            inst.contractInputs.push_back(std::move(slot));
        }

        // Initialize concurrent state machine layers
        const std::size_t layerEnd = std::min<std::size_t>(
            pkgCopy->smLayers.size(),
            static_cast<std::size_t>(smDesc.firstLayerIdx) + smDesc.layerCount);
        for (std::size_t lIdx = smDesc.firstLayerIdx; lIdx < layerEnd; ++lIdx) {
            const auto& lyDesc = pkgCopy->smLayers[lIdx];
            LayerRuntimeState ly{};
            ly.layerDescIdx = static_cast<std::uint32_t>(lIdx);
            ly.layerNameHash = HashNameFnv1a64(pkgCopy->GetString(lyDesc.nameStrIdx));
            ly.blendMode = lyDesc.blendMode;
            ly.layerWeight = lyDesc.layerWeight;
            ly.activeStateIdx = lyDesc.entryStateIdx;
            if (ly.activeStateIdx < pkgCopy->smStates.size()) {
                ly.activeStateNameHash = HashNameFnv1a64(
                    pkgCopy->GetString(pkgCopy->smStates[ly.activeStateIdx].nameStrIdx));
            }
            inst.layers.push_back(ly);
        }

        // Pre-allocate ChannelBlendScratch for all unique (localElementIdx, targetPropId) pairs across tracks
        for (const auto& trDesc : pkgCopy->motionTracks) {
            bool exists = false;
            for (const auto& existing : inst.channelScratch) {
                if (existing.localElementIdx == trDesc.localElementIdx &&
                    existing.propId == trDesc.targetPropId) {
                    exists = true;
                    break;
                }
            }
            if (exists || trDesc.localElementIdx >= inst.localElements.size()) {
                continue;
            }
            UIElement* target = inst.localElements[trDesc.localElementIdx].Get();
            if (!target) {
                continue;
            }
            ChannelBlendSlot ch{};
            ch.localElementIdx = trDesc.localElementIdx;
            ch.propId = trDesc.targetPropId;
            ch.channelCount = trDesc.channelCount;
            ch.quantizeInlineW = trDesc.quantizeInlineW;
            if (IsFastInlinePropertyId(ch.propId)) {
                const ValueSourceRank curRank = target->GetPropertySourceRank(ch.propId);
                ch.hadBaseSnapshot = (curRank != ValueSourceRank::Default);
                ch.baseRank = curRank;
                ch.baseSnapshot = target->GetValue(ch.propId);
                target->SetFastPropertyAnimActive(ch.propId, true);
            } else if (const auto* entry = target->FindCompactEntry(ch.propId)) {
                if (!std::holds_alternative<std::monostate>(entry->effectiveValue) &&
                    entry->sourceRank != static_cast<std::uint8_t>(ValueSourceRank::Default)) {
                    ch.hadBaseSnapshot = true;
                    ch.baseRank = static_cast<ValueSourceRank>(entry->sourceRank);
                    ch.baseSnapshot = entry->effectiveValue;
                }
            }
            if (ch.hadBaseSnapshot && ch.baseSnapshot.isTraceable()) {
                rs_rt::pinValueInPools(ch.baseSnapshot);
            }
            if (ch.channelCount == 4u) {
                ch.restValue = ReadCurrentFloat4(target, ch.propId);
            } else {
                ch.restValue.x = ReadCurrentScalar(target, ch.propId);
            }
            inst.channelScratch.push_back(std::move(ch));
        }

        // Remove any previous state machine instance with the same rootControl & machineNameHash
        for (std::size_t i = 0u; i < stateMachines_.size();) {
            if (stateMachines_[i].rootControl.Get() == rootControl &&
                stateMachines_[i].machineNameHash == inst.machineNameHash) {
                stateMachines_.erase(stateMachines_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }

        // Perform initial t=0 evaluation so entry state visuals are immediately applied
        SyncBridgePropertiesToContractInputs(inst);
        (void)EvaluateStateMachineTransitions(inst);
        (void)EvaluateAndBlendStateMachineInstance(inst, 0.0f, true);
        inst.isSettled = CheckStateMachineInstanceSettled(inst);
        for (auto& ci : inst.contractInputs) {
            ci.dirtyThisFrame = false;
        }
        inst.evaluatedTransitionsThisFrame = false;
        inst.needsInitialEval = false;

        stateMachines_.push_back(std::move(inst));
        anyInstantiated = true;
    }

    if (anyInstantiated && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return anyInstantiated;
}

void AnimationEngine::SyncBridgePropertiesToContractInputs(MotionStateMachineInstance& sm) noexcept {
    UIElement* root = sm.rootControl.Get();
    if (!root) {
        return;
    }
    for (auto& slot : sm.contractInputs) {
        if (slot.bridgePropId == 0xFFFFu) {
            continue;
        }
        const auto* entry = root->FindCompactEntry(slot.bridgePropId);
        if (!entry || std::holds_alternative<std::monostate>(entry->effectiveValue)) {
            continue;
        }
        if (slot.valueKind == 4u) { // Color / Float4
            Float4Value f4{};
            if (entry->effectiveValue.tryGetFloat4(f4) && !(f4 == slot.float4Value)) {
                slot.float4Value = f4;
                slot.dirtyThisFrame = true;
                sm.isSettled = false;
            }
        } else {
            float sVal = slot.scalarValue;
            if (const auto* b = std::get_if<bool>(&entry->effectiveValue)) {
                sVal = *b ? 1.0f : 0.0f;
            } else {
                (void)TryGetFloat(entry->effectiveValue, sVal);
            }
            if (std::fabs(sVal - slot.scalarValue) > 1e-6f) {
                slot.scalarValue = sVal;
                slot.dirtyThisFrame = true;
                sm.isSettled = false;
            }
        }
    }
}

bool AnimationEngine::SetStateMachineInputFloat(
    UIElement* rootControl,
    std::uint16_t slotIdx,
    float value) noexcept {
    if (!rootControl) return false;
    bool found = false;
    for (auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl || slotIdx >= sm.contractInputs.size()) {
            continue;
        }
        auto& slot = sm.contractInputs[slotIdx];
        if (std::fabs(slot.scalarValue - value) > 1e-6f || slot.isTrigger) {
            slot.scalarValue = value;
            slot.dirtyThisFrame = true;
            sm.isSettled = false;
            if (wakeHostCallback_) {
                wakeHostCallback_();
            }
        }
        found = true;
    }
    return found;
}

bool AnimationEngine::SetStateMachineInputFloatByName(
    UIElement* rootControl,
    std::string_view inputName,
    float value) noexcept {
    if (!rootControl || inputName.empty()) return false;
    const std::uint64_t targetHash = HashNameFnv1a64(inputName);
    bool found = false;
    for (auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl) continue;
        for (std::size_t i = 0u; i < sm.contractInputs.size(); ++i) {
            if (sm.contractInputs[i].nameHash == targetHash && sm.contractInputs[i].name == inputName) {
                (void)SetStateMachineInputFloat(rootControl, static_cast<std::uint16_t>(i), value);
                found = true;
            }
        }
    }
    return found;
}

bool AnimationEngine::FireStateMachineTrigger(UIElement* rootControl, std::uint16_t slotIdx) noexcept {
    if (!rootControl) return false;
    bool found = false;
    for (auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl || slotIdx >= sm.contractInputs.size()) {
            continue;
        }
        auto& slot = sm.contractInputs[slotIdx];
        slot.isTrigger = true;
        slot.scalarValue = 1.0f;
        slot.dirtyThisFrame = true;
        sm.isSettled = false;
        found = true;
    }
    if (found && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return found;
}

bool AnimationEngine::FireStateMachineTriggerByName(
    UIElement* rootControl,
    std::string_view triggerName) noexcept {
    if (!rootControl || triggerName.empty()) return false;
    const std::uint64_t targetHash = HashNameFnv1a64(triggerName);
    bool found = false;
    for (auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl) continue;
        for (std::size_t i = 0u; i < sm.contractInputs.size(); ++i) {
            if (sm.contractInputs[i].nameHash == targetHash && sm.contractInputs[i].name == triggerName) {
                (void)FireStateMachineTrigger(rootControl, static_cast<std::uint16_t>(i));
                found = true;
            }
        }
    }
    return found;
}

float AnimationEngine::GetStateMachineInputFloat(
    const UIElement* rootControl,
    std::uint16_t slotIdx) const noexcept {
    if (!rootControl) return 0.0f;
    for (const auto& sm : stateMachines_) {
        if (sm.rootControl.Get() == rootControl && slotIdx < sm.contractInputs.size()) {
            return sm.contractInputs[slotIdx].scalarValue;
        }
    }
    return 0.0f;
}

float AnimationEngine::GetStateMachineInputFloatByName(
    const UIElement* rootControl,
    std::string_view inputName) const noexcept {
    if (!rootControl || inputName.empty()) return 0.0f;
    const std::uint64_t targetHash = HashNameFnv1a64(inputName);
    for (const auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl) continue;
        for (const auto& slot : sm.contractInputs) {
            if (slot.nameHash == targetHash && slot.name == inputName) {
                return slot.scalarValue;
            }
        }
    }
    return 0.0f;
}

bool AnimationEngine::DispatchPointerListenerEvent(
    UIElement* hitElement,
    SmListenerEventKind eventKind) noexcept {
    if (!hitElement || stateMachines_.empty()) {
        return false;
    }
    bool anyHandled = false;
    for (auto& sm : stateMachines_) {
        if (!sm.motionData || sm.machineDescIdx >= sm.motionData->stateMachines.size()) {
            continue;
        }
        const auto& smDesc = sm.motionData->stateMachines[sm.machineDescIdx];
        const std::size_t endIdx = std::min<std::size_t>(
            sm.motionData->smListeners.size(),
            static_cast<std::size_t>(smDesc.firstListenerIdx) + smDesc.listenerCount);
        for (std::size_t i = smDesc.firstListenerIdx; i < endIdx; ++i) {
            const auto& ls = sm.motionData->smListeners[i];
            if (ls.eventKind != eventKind || ls.hitTargetElementIdx >= sm.localElements.size() ||
                ls.targetInputSlot >= sm.contractInputs.size()) {
                continue;
            }
            const UIElement* targetElem = sm.localElements[ls.hitTargetElementIdx].Get();
            if (!targetElem || !IsInVisualSubtree(hitElement, targetElem)) {
                continue;
            }
            auto& slot = sm.contractInputs[ls.targetInputSlot];
            switch (ls.actionKind) {
                case SmListenerActionKind::SetFloatOrBool:
                    slot.scalarValue = ls.actionValue;
                    break;
                case SmListenerActionKind::FireTrigger:
                    slot.isTrigger = true;
                    slot.scalarValue = 1.0f;
                    break;
                case SmListenerActionKind::AddDelta:
                    slot.scalarValue += ls.actionValue;
                    break;
            }
            slot.dirtyThisFrame = true;
            sm.isSettled = false;
            anyHandled = true;
        }
    }
    if (anyHandled && wakeHostCallback_) {
        wakeHostCallback_();
    }
    return anyHandled;
}

bool AnimationEngine::EvaluateStateMachineTransitions(MotionStateMachineInstance& sm) noexcept {
    if (!sm.motionData) {
        return false;
    }
    const auto& pkg = *sm.motionData;
    bool anyTransitioned = false;

    for (auto& ly : sm.layers) {
        if (ly.layerDescIdx >= pkg.smLayers.size()) {
            continue;
        }
        const auto& lyDesc = pkg.smLayers[ly.layerDescIdx];
        const std::size_t trEnd = std::min<std::size_t>(
            pkg.smTransitions.size(),
            static_cast<std::size_t>(lyDesc.firstTransitionIdx) + lyDesc.transitionCount);

        float curStateDuration = 0.0f;
        if (ly.activeStateIdx < pkg.smStates.size()) {
            const auto& curSt = pkg.smStates[ly.activeStateIdx];
            if (curSt.primaryClipIdx < pkg.motionClips.size()) {
                curStateDuration = pkg.motionClips[curSt.primaryClipIdx].durationSec;
            }
        }
        const float curNormalizedProgress = (curStateDuration > 1e-6f)
            ? (ly.activeTime / curStateDuration)
            : 1.0f;

        const AxbSmTransition* bestTransition = nullptr;
        for (std::size_t tIdx = lyDesc.firstTransitionIdx; tIdx < trEnd; ++tIdx) {
            const auto& tr = pkg.smTransitions[tIdx];
            if (tr.fromStateIdx != 0xFFFFu && tr.fromStateIdx != ly.activeStateIdx) {
                continue;
            }
            if (tr.toStateIdx == ly.activeStateIdx && !tr.allowSelfInterrupt) {
                continue;
            }
            if (tr.hasExitTime && curNormalizedProgress + 1e-5f < tr.exitTimeNormalized01) {
                continue;
            }

            const std::size_t condEnd = std::min<std::size_t>(
                pkg.smConditions.size(),
                static_cast<std::size_t>(tr.firstConditionIdx) + tr.conditionCount);
            if (tr.conditionCount == 0u && !tr.hasExitTime) {
                continue;
            }

            bool allMet = true;
            for (std::size_t cIdx = tr.firstConditionIdx; cIdx < condEnd; ++cIdx) {
                const auto& cond = pkg.smConditions[cIdx];
                if (cond.inputSlotIdx >= sm.contractInputs.size()) {
                    allMet = false;
                    break;
                }
                const float v = sm.contractInputs[cond.inputSlotIdx].scalarValue;
                const float th = cond.threshold;
                bool condMet = false;
                switch (cond.op) {
                    case SmCompareOp::Equal:
                        condMet = (std::fabs(v - th) <= 1e-4f);
                        break;
                    case SmCompareOp::NotEqual:
                        condMet = (std::fabs(v - th) > 1e-4f);
                        break;
                    case SmCompareOp::Greater:
                        condMet = (v > th);
                        break;
                    case SmCompareOp::GreaterOrEqual:
                        condMet = (v >= th - 1e-5f);
                        break;
                    case SmCompareOp::Less:
                        condMet = (v < th);
                        break;
                    case SmCompareOp::LessOrEqual:
                        condMet = (v <= th + 1e-5f);
                        break;
                    case SmCompareOp::BoolTrue:
                    case SmCompareOp::TriggerFired:
                        condMet = (v > 0.5f);
                        break;
                    case SmCompareOp::BoolFalse:
                        condMet = (v <= 0.5f);
                        break;
                }
                if (!condMet) {
                    allMet = false;
                    break;
                }
            }

            if (allMet) {
                if (!bestTransition || tr.priority > bestTransition->priority) {
                    bestTransition = &tr;
                }
            }
        }

        if (bestTransition && bestTransition->toStateIdx < pkg.smStates.size()) {
            if (bestTransition->crossfadeDurationSec > 1e-5f) {
                ly.prevStateIdx = ly.activeStateIdx;
                ly.prevStateTime = ly.activeTime;
                ly.crossfadeElapsed = 0.0f;
                ly.crossfadeDuration = bestTransition->crossfadeDurationSec;
                ly.crossfadeEaseKind = bestTransition->easeKind;
                ly.crossfadeEaseMode = bestTransition->easeMode;
            } else {
                ly.prevStateIdx = 0xFFFFu;
                ly.crossfadeElapsed = 0.0f;
                ly.crossfadeDuration = 0.0f;
            }
            ly.activeStateIdx = bestTransition->toStateIdx;
            ly.activeStateNameHash = HashNameFnv1a64(
                pkg.GetString(pkg.smStates[ly.activeStateIdx].nameStrIdx));
            ly.activeTime = 0.0f;
            ly.prevActiveTime = 0.0f;
            sm.isSettled = false;
            anyTransitioned = true;
        }
    }

    // Auto-reset all Trigger pulses at the end of state-machine transition evaluation
    for (auto& slot : sm.contractInputs) {
        if (slot.isTrigger && slot.scalarValue != 0.0f) {
            slot.scalarValue = 0.0f;
        }
    }

    sm.evaluatedTransitionsThisFrame = true;
    return anyTransitioned;
}

std::size_t AnimationEngine::EvaluateStateMachines(float dtSeconds) noexcept {
    std::size_t updates = 0u;
    for (auto& sm : stateMachines_) {
        if (!sm.rootControl) {
            continue;
        }
        SyncBridgePropertiesToContractInputs(sm);
        if (EvaluateStateMachineTransitions(sm)) {
            ++updates;
        }
        if (!sm.isSettled || sm.needsInitialEval || dtSeconds > 0.0f) {
            updates += EvaluateAndBlendStateMachineInstance(sm, std::max(0.0f, dtSeconds), false);
            sm.needsInitialEval = false;
            sm.isSettled = CheckStateMachineInstanceSettled(sm);
        }
        for (auto& ci : sm.contractInputs) {
            ci.dirtyThisFrame = false;
        }
        sm.evaluatedTransitionsThisFrame = true;
    }
    return updates;
}

std::size_t AnimationEngine::EvaluateAndBlendStateMachineInstance(
    MotionStateMachineInstance& sm,
    float dtSeconds,
    bool snapCrossfades) noexcept {
    if (!sm.motionData || !sm.rootControl) {
        return 0u;
    }
    const auto& pkg = *sm.motionData;

    for (auto& ch : sm.channelScratch) {
        ch.touchedThisFrame = false;
        ch.accumValue = ch.restValue;
    }

    auto findScratchSlot = [&](std::uint16_t localIdx, std::uint16_t propId) noexcept -> ChannelBlendSlot* {
        for (auto& ch : sm.channelScratch) {
            if (ch.localElementIdx == localIdx && ch.propId == propId) {
                return &ch;
            }
        }
        return nullptr;
    };

    auto computeClipLocalTime = [&](const AxbMotionClipDesc& clip, float rawTime) noexcept -> float {
        const float dur = clip.durationSec;
        if (dur <= 1e-6f) {
            return 0.0f;
        }
        if (!clip.isLooping) {
            return std::clamp(rawTime, 0.0f, dur);
        }
        const float cycle = clip.autoReverse ? (dur * 2.0f) : dur;
        float modT = std::fmod(std::max(0.0f, rawTime), cycle);
        if (clip.autoReverse && modT > dur) {
            modT = cycle - modT;
        }
        return modT;
    };

    auto dispatchClipEvents = [&](const AxbMotionClipDesc& clip, float t0, float t1) {
        if (!timelineEventCallback_ || clip.eventCount == 0u || dtSeconds <= 0.0f) {
            return;
        }
        const std::size_t evEnd = std::min<std::size_t>(
            pkg.motionEvents.size(),
            static_cast<std::size_t>(clip.firstEventIdx) + clip.eventCount);
        for (std::size_t eIdx = clip.firstEventIdx; eIdx < evEnd; ++eIdx) {
            const auto& ev = pkg.motionEvents[eIdx];
            const bool crossed = (t1 >= t0)
                ? (ev.timeSec > t0 && ev.timeSec <= t1)
                : (ev.timeSec > t0 || ev.timeSec <= t1);
            if (crossed) {
                TimelineEventInvocation inv{};
                inv.rootControl = sm.rootControl.Get();
                inv.eventName = pkg.GetString(ev.eventNameStrIdx);
                inv.handlerSymbolId = ev.handlerSymbolId;
                inv.timeSec = ev.timeSec;
                inv.floatPayload = ev.floatPayload;
                inv.intPayload = ev.intPayload;
                timelineEventCallback_(inv);
            }
        }
    };

    auto sampleStateIntoLayerScratch = [&](
        const AxbSmState& st,
        float stateRawTime,
        float prevRawTime,
        bool allowEvents,
        SmLayerBlendMode blendMode,
        float effectiveWeight) {
        if (effectiveWeight <= 1e-6f || st.kind == SmStateKind::ExitState) {
            return;
        }

        auto applyClipWithWeight = [&](std::uint16_t clipIdx, float clipLocalTime, float w) {
            if (clipIdx >= pkg.motionClips.size() || w <= 1e-6f) {
                return;
            }
            const auto& clip = pkg.motionClips[clipIdx];
            const std::size_t trEnd = std::min<std::size_t>(
                pkg.motionTracks.size(),
                static_cast<std::size_t>(clip.firstTrackIdx) + clip.trackCount);
            for (std::size_t trIdx = clip.firstTrackIdx; trIdx < trEnd; ++trIdx) {
                const auto& trDesc = pkg.motionTracks[trIdx];
                ChannelBlendSlot* ch = findScratchSlot(trDesc.localElementIdx, trDesc.targetPropId);
                if (!ch) {
                    continue;
                }
                const Float4Value sampled = EvaluateFlatTrackAtTime(
                    trDesc,
                    pkg.motionKeyframes.data(),
                    pkg.motionKeyframes.size(),
                    clipLocalTime,
                    sm.trackSegmentCache[trIdx]);
                if (blendMode == SmLayerBlendMode::Additive) {
                    // For Additive layers, delta is relative to the channel's rest baseline
                    const Float4Value baseRef = ch->restValue;
                    ch->accumValue.x += (sampled.x - baseRef.x) * w;
                    ch->accumValue.y += (sampled.y - baseRef.y) * w;
                    ch->accumValue.z += (sampled.z - baseRef.z) * w;
                    ch->accumValue.w += (sampled.w - baseRef.w) * w;
                } else {
                    if (!ch->touchedThisFrame && w >= 0.9999f) {
                        ch->accumValue = sampled;
                    } else {
                        ch->accumValue = LerpFloat4(ch->accumValue, sampled, w);
                    }
                }
                ch->touchedThisFrame = true;
            }
        };

        if (st.kind == SmStateKind::SingleClip) {
            if (st.primaryClipIdx >= pkg.motionClips.size()) return;
            const auto& clip = pkg.motionClips[st.primaryClipIdx];
            const float curT = computeClipLocalTime(clip, stateRawTime);
            if (allowEvents) {
                const float prevT = computeClipLocalTime(clip, prevRawTime);
                dispatchClipEvents(clip, prevT, curT);
            }
            applyClipWithWeight(st.primaryClipIdx, curT, effectiveWeight);
        } else if (st.kind == SmStateKind::ScrubbedClip) {
            if (st.primaryClipIdx >= pkg.motionClips.size()) return;
            const auto& clip = pkg.motionClips[st.primaryClipIdx];
            float normProgress = 0.0f;
            if (st.progressInputSlot < sm.contractInputs.size()) {
                const float rawIn = sm.contractInputs[st.progressInputSlot].scalarValue;
                const float span = st.blendRangeMax - st.blendRangeMin;
                normProgress = (std::fabs(span) > 1e-6f)
                    ? std::clamp((rawIn - st.blendRangeMin) / span, 0.0f, 1.0f)
                    : std::clamp(rawIn, 0.0f, 1.0f);
            }
            applyClipWithWeight(st.primaryClipIdx, normProgress * clip.durationSec, effectiveWeight);
        } else if (st.kind == SmStateKind::Blend1D) {
            float blendAlpha = 0.0f;
            if (st.progressInputSlot < sm.contractInputs.size()) {
                const float rawIn = sm.contractInputs[st.progressInputSlot].scalarValue;
                const float span = st.blendRangeMax - st.blendRangeMin;
                blendAlpha = (std::fabs(span) > 1e-6f)
                    ? std::clamp((rawIn - st.blendRangeMin) / span, 0.0f, 1.0f)
                    : std::clamp(rawIn, 0.0f, 1.0f);
            }
            if (st.primaryClipIdx < pkg.motionClips.size()) {
                const float tA = computeClipLocalTime(pkg.motionClips[st.primaryClipIdx], stateRawTime);
                applyClipWithWeight(st.primaryClipIdx, tA, effectiveWeight);
            }
            if (st.secondaryClipIdx < pkg.motionClips.size() && blendAlpha > 1e-5f) {
                const float tB = computeClipLocalTime(pkg.motionClips[st.secondaryClipIdx], stateRawTime);
                applyClipWithWeight(st.secondaryClipIdx, tB, effectiveWeight * blendAlpha);
            }
        }
    };

    for (auto& ly : sm.layers) {
        if (ly.activeStateIdx >= pkg.smStates.size()) {
            continue;
        }
        const auto& activeSt = pkg.smStates[ly.activeStateIdx];
        ly.prevActiveTime = ly.activeTime;
        if (activeSt.kind == SmStateKind::SingleClip || activeSt.kind == SmStateKind::Blend1D) {
            ly.activeTime += dtSeconds * activeSt.speedMultiplier;
        }

        if (snapCrossfades) {
            ly.prevStateIdx = 0xFFFFu;
            ly.crossfadeElapsed = 0.0f;
            ly.crossfadeDuration = 0.0f;
        }

        float crossfadeAlpha = 1.0f;
        if (ly.prevStateIdx < pkg.smStates.size() && ly.crossfadeDuration > 1e-5f) {
            const auto& prevSt = pkg.smStates[ly.prevStateIdx];
            ly.prevStateTime += dtSeconds * prevSt.speedMultiplier;
            ly.crossfadeElapsed += dtSeconds;
            if (ly.crossfadeElapsed >= ly.crossfadeDuration) {
                ly.prevStateIdx = 0xFFFFu;
                ly.crossfadeElapsed = 0.0f;
                ly.crossfadeDuration = 0.0f;
            } else {
                const float normCf = std::clamp(ly.crossfadeElapsed / ly.crossfadeDuration, 0.0f, 1.0f);
                crossfadeAlpha = EvaluateEasing(ly.crossfadeEaseKind, ly.crossfadeEaseMode, normCf);
                sampleStateIntoLayerScratch(
                    prevSt,
                    ly.prevStateTime,
                    ly.prevStateTime,
                    false,
                    ly.blendMode,
                    ly.layerWeight);
            }
        }

        sampleStateIntoLayerScratch(
            activeSt,
            ly.activeTime,
            ly.prevActiveTime,
            true,
            ly.blendMode,
            ly.layerWeight * crossfadeAlpha);
    }

    // Single-pass property writeback (each controlled property is written at most once per frame!)
    std::size_t writes = 0u;
    for (const auto& ch : sm.channelScratch) {
        if (!ch.touchedThisFrame || ch.localElementIdx >= sm.localElements.size()) {
            continue;
        }
        UIElement* target = sm.localElements[ch.localElementIdx].Get();
        if (!target) {
            continue;
        }
        if (ch.channelCount == 4u) {
            (void)target->SetValue(
                ch.propId,
                ch.quantizeInlineW ? MakeFloat4InlineFast(ch.accumValue) : UnifiedValue{ch.accumValue},
                ValueSourceRank::Animation);
        } else if (ch.channelCount == 0u) {
            (void)target->SetValue(
                ch.propId,
                value::FromInt(static_cast<std::int64_t>(std::llround(ch.accumValue.x))),
                ValueSourceRank::Animation);
        } else {
            (void)target->SetValue(
                ch.propId,
                MakeFloat(ch.accumValue.x),
                ValueSourceRank::Animation);
        }
        ++writes;
    }

    // Evaluate non-tree external parent transform constraints in topological order
    // (preserves independent child Opacity and visual draw order for Lottie parent chains!)
    for (const auto& tc : pkg.transformConstraints) {
        if (tc.childLocalElementIdx >= sm.localElements.size() ||
            tc.parentLocalElementIdx >= sm.localElements.size()) {
            continue;
        }
        UIElement* child = sm.localElements[tc.childLocalElementIdx].Get();
        const UIElement* parent = sm.localElements[tc.parentLocalElementIdx].Get();
        if (!child || !parent) {
            continue;
        }
        const Matrix2D parentMat = parent->ComputeLocalEffectiveTransform2D();
        (void)child->SetValue(
            PropId_RenderTransform,
            MakeFloat4InlineFast(Float4Value{parentMat.m00, parentMat.m01, parentMat.m10, parentMat.m11}),
            ValueSourceRank::Animation);
        (void)child->SetValue(
            PropId_RenderTransformTranslation,
            MakeFloat4InlineFast(Float4Value{parentMat.tx, parentMat.ty, 0.0f, 0.0f}),
            ValueSourceRank::Animation);
        ++writes;
    }

    return writes;
}

bool AnimationEngine::CheckStateMachineInstanceSettled(const MotionStateMachineInstance& sm) const noexcept {
    if (!sm.motionData || !sm.rootControl) {
        return true;
    }
    const auto& pkg = *sm.motionData;
    for (const auto& ci : sm.contractInputs) {
        if (ci.isTrigger && ci.scalarValue > 0.5f) {
            return false;
        }
    }
    for (const auto& ly : sm.layers) {
        if (ly.prevStateIdx != 0xFFFFu && ly.crossfadeElapsed < ly.crossfadeDuration) {
            return false;
        }
        if (ly.activeStateIdx >= pkg.smStates.size()) {
            continue;
        }
        const auto& st = pkg.smStates[ly.activeStateIdx];
        if (st.kind == SmStateKind::SingleClip) {
            if (st.primaryClipIdx >= pkg.motionClips.size()) {
                continue;
            }
            const auto& clip = pkg.motionClips[st.primaryClipIdx];
            if (std::fabs(st.speedMultiplier) <= 1e-6f) {
                continue;
            }
            if (clip.isLooping || ly.activeTime + 1e-5f < clip.durationSec) {
                return false;
            }
        } else if (st.kind == SmStateKind::ScrubbedClip || st.kind == SmStateKind::Blend1D) {
            if (st.progressInputSlot < sm.contractInputs.size() &&
                sm.contractInputs[st.progressInputSlot].dirtyThisFrame) {
                return false;
            }
            if (st.kind == SmStateKind::Blend1D) {
                if (st.primaryClipIdx < pkg.motionClips.size() &&
                    pkg.motionClips[st.primaryClipIdx].isLooping &&
                    std::fabs(st.speedMultiplier) > 1e-6f) {
                    return false;
                }
                if (st.secondaryClipIdx < pkg.motionClips.size() &&
                    pkg.motionClips[st.secondaryClipIdx].isLooping &&
                    std::fabs(st.speedMultiplier) > 1e-6f) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool AnimationEngine::IsStateMachineSettled(const UIElement* rootControl) const noexcept {
    if (!rootControl) {
        return true;
    }
    for (const auto& sm : stateMachines_) {
        if (sm.rootControl.Get() == rootControl && !sm.isSettled) {
            return false;
        }
    }
    return true;
}

std::size_t AnimationEngine::ActiveStateMachineCount() const noexcept {
    std::size_t active = 0u;
    for (const auto& sm : stateMachines_) {
        if (sm.rootControl && !sm.isSettled) {
            ++active;
        }
    }
    return active;
}

void AnimationEngine::OnSubtreeDataContextRebound(UIElement* subtreeRoot) noexcept {
    if (!subtreeRoot) {
        return;
    }
    for (auto& sm : stateMachines_) {
        if (!sm.rootControl || !IsInVisualSubtree(sm.rootControl.Get(), subtreeRoot)) {
            continue;
        }
        SyncBridgePropertiesToContractInputs(sm);
        (void)EvaluateStateMachineTransitions(sm);
        (void)EvaluateAndBlendStateMachineInstance(sm, 0.0f, true);
        sm.isSettled = CheckStateMachineInstanceSettled(sm);
        for (auto& ci : sm.contractInputs) {
            ci.dirtyThisFrame = false;
        }
        sm.evaluatedTransitionsThisFrame = false;
    }
}

std::size_t AnimationEngine::MigrateStateMachinesOnHotReload(
    UIElement* rootControl,
    UIElement* const* newLocalElements,
    std::size_t newLocalElementCount,
    const AxbPackage& newPackage) {
    if (!rootControl) {
        return 0u;
    }
    struct SavedLayerSnapshot {
        std::uint64_t machineHash = 0ULL;
        std::uint64_t layerHash = 0ULL;
        std::uint64_t stateHash = 0ULL;
        float activeTime = 0.0f;
    };
    struct SavedInputSnapshot {
        std::uint64_t machineHash = 0ULL;
        std::uint64_t inputHash = 0ULL;
        float scalarValue = 0.0f;
        Float4Value float4Value{};
    };
    std::vector<SavedLayerSnapshot> savedLayers;
    std::vector<SavedInputSnapshot> savedInputs;

    for (const auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl) {
            continue;
        }
        for (const auto& ly : sm.layers) {
            savedLayers.push_back(SavedLayerSnapshot{
                sm.machineNameHash,
                ly.layerNameHash,
                ly.activeStateNameHash,
                ly.activeTime});
        }
        for (const auto& ci : sm.contractInputs) {
            savedInputs.push_back(SavedInputSnapshot{
                sm.machineNameHash,
                ci.nameHash,
                ci.scalarValue,
                ci.float4Value});
        }
    }

    (void)StopSubtreeAnimations(rootControl, true);
    if (!InstantiatePackageMotion(rootControl, newLocalElements, newLocalElementCount, newPackage)) {
        return 0u;
    }

    std::size_t migrated = 0u;
    for (auto& sm : stateMachines_) {
        if (sm.rootControl.Get() != rootControl || !sm.motionData) {
            continue;
        }
        const auto& pkg = *sm.motionData;
        for (auto& ci : sm.contractInputs) {
            for (const auto& sIn : savedInputs) {
                if (sIn.machineHash == sm.machineNameHash && sIn.inputHash == ci.nameHash) {
                    ci.scalarValue = sIn.scalarValue;
                    ci.float4Value = sIn.float4Value;
                    ci.dirtyThisFrame = true;
                    break;
                }
            }
        }
        for (auto& ly : sm.layers) {
            if (ly.layerDescIdx >= pkg.smLayers.size()) continue;
            const auto& lyDesc = pkg.smLayers[ly.layerDescIdx];
            for (const auto& sLy : savedLayers) {
                if (sLy.machineHash == sm.machineNameHash && sLy.layerHash == ly.layerNameHash) {
                    const std::size_t stEnd = std::min<std::size_t>(
                        pkg.smStates.size(),
                        static_cast<std::size_t>(lyDesc.firstStateIdx) + lyDesc.stateCount);
                    for (std::size_t stIdx = lyDesc.firstStateIdx; stIdx < stEnd; ++stIdx) {
                        const std::uint64_t h = HashNameFnv1a64(
                            pkg.GetString(pkg.smStates[stIdx].nameStrIdx));
                        if (h == sLy.stateHash) {
                            ly.activeStateIdx = static_cast<std::uint16_t>(stIdx);
                            ly.activeStateNameHash = h;
                            ly.activeTime = sLy.activeTime;
                            ly.prevActiveTime = sLy.activeTime;
                            ++migrated;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        (void)EvaluateAndBlendStateMachineInstance(sm, 0.0f, true);
        sm.isSettled = CheckStateMachineInstanceSettled(sm);
        for (auto& ci : sm.contractInputs) {
            ci.dirtyThisFrame = false;
        }
    }
    return migrated;
}

float AnimationEngine::GetTrackVelocityX(std::uint16_t trackId) const noexcept {
    if (trackId < tracks_.size() && tracks_[trackId].kind != AnimationTrackKind::Inactive) {
        return tracks_[trackId].velF4.x;
    }
    return 0.0f;
}

std::size_t AnimationEngine::Tick(float dtSeconds) noexcept {
    if (dtSeconds < 0.0f) {
        return 0u;
    }
    if (activeCount_ == 0u && stateMachines_.empty()) {
        return 0u;
    }

    std::size_t updates = 0u;

    // 0. Advance multi-layer state machines (auto-evaluating transitions if UiPhase_MotionStateMachine wasn't run separately)
    for (auto& sm : stateMachines_) {
        if (!sm.rootControl) {
            continue;
        }
        if (sm.evaluatedTransitionsThisFrame) {
            sm.evaluatedTransitionsThisFrame = false;
            continue;
        }
        SyncBridgePropertiesToContractInputs(sm);
        (void)EvaluateStateMachineTransitions(sm);
        sm.evaluatedTransitionsThisFrame = false;

        if (!sm.isSettled || sm.needsInitialEval) {
            updates += EvaluateAndBlendStateMachineInstance(sm, dtSeconds, false);
            sm.needsInitialEval = false;
            sm.isSettled = CheckStateMachineInstanceSettled(sm);
        }
        for (auto& ci : sm.contractInputs) {
            ci.dirtyThisFrame = false;
        }
    }

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
        if (tr.paused) {
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
                    (void)target->SetValue(tr.propId, MakeFloat4InlineFast(tr.toF4), ValueSourceRank::Animation);
                    ReleaseTrackSlot(tr, false);
                } else {
                    (void)target->SetValue(tr.propId, MakeFloat4InlineFast(tr.posF4), ValueSourceRank::Animation);
                }
                ++updates;
            }
            continue;
        }

        // 2. Timed Tween, Multi-KeyFrame & CompiledClipSlice Tracks
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
            (void)target->SetValue(tr.propId, MakeFloat4InlineFast(val), ValueSourceRank::Animation);
            ++updates;
        } else if (tr.kind == AnimationTrackKind::CompiledClipSlice && tr.sliceFrames && tr.sliceCount > 0u) {
            AxbMotionTrackDesc sliceDesc{};
            sliceDesc.targetPropId = tr.propId;
            sliceDesc.channelCount = tr.channelCount;
            sliceDesc.quantizeInlineW = tr.quantizeInlineW;
            sliceDesc.firstKeyframeIdx = 0u;
            sliceDesc.keyframeCount = tr.sliceCount;
            const Float4Value val = EvaluateFlatTrackAtTime(
                sliceDesc,
                tr.sliceFrames,
                tr.sliceCount,
                localCycleTime,
                tr.cachedSegmentIdx);
            tr.posF4 = val;
            if (tr.channelCount == 4u) {
                (void)target->SetValue(
                    tr.propId,
                    tr.quantizeInlineW ? MakeFloat4InlineFast(val) : UnifiedValue{val},
                    ValueSourceRank::Animation);
            } else if (tr.channelCount == 0u) {
                (void)target->SetValue(
                    tr.propId,
                    value::FromInt(static_cast<std::int64_t>(std::llround(val.x))),
                    ValueSourceRank::Animation);
            } else {
                (void)target->SetValue(tr.propId, MakeFloat(val.x), ValueSourceRank::Animation);
            }
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

            Float4Value endF4{};
            if (endKf.isDiscrete) {
                if (segNorm >= 1.0f || segSpan <= 1e-6f) {
                    (void)target->SetValue(tr.propId, endKf.value, ValueSourceRank::Animation);
                } else if (segIdx > 0u) {
                    (void)target->SetValue(
                        tr.propId, tr.keyFrames[segIdx - 1u].value, ValueSourceRank::Animation);
                }
            } else if (endKf.value.tryGetFloat4(endF4)) {
                Float4Value startF4 = tr.fromF4;
                if (segIdx > 0u) {
                    Float4Value pf4{};
                    if (tr.keyFrames[segIdx - 1u].value.tryGetFloat4(pf4)) {
                        startF4 = pf4;
                    }
                }
                const float eased = EvaluateEasing(endKf.easingKind, endKf.easingMode, segNorm);
                const Float4Value curVal = LerpFloat4(startF4, endF4, eased);
                tr.posF4 = curVal;
                (void)target->SetValue(tr.propId, MakeFloat4InlineFast(curVal), ValueSourceRank::Animation);
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
