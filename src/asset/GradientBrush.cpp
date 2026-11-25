#include "aero/asset/GradientBrush.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"

namespace aero {

namespace {

constexpr std::string_view kGradientSpecPrefix = "@aero.grad:";

[[nodiscard]] float ApplySpreadMethod01(float rawT, GradientSpreadMethod spread) noexcept {
    switch (spread) {
        case GradientSpreadMethod::Repeat:
            return std::clamp(rawT - std::floor(rawT), 0.0f, 1.0f);
        case GradientSpreadMethod::Reflect: {
            const float m = std::fmod(std::fabs(rawT), 2.0f);
            return std::clamp((m > 1.0f) ? (2.0f - m) : m, 0.0f, 1.0f);
        }
        case GradientSpreadMethod::Pad:
        default:
            return std::clamp(rawT, 0.0f, 1.0f);
    }
}

[[nodiscard]] Color EvaluateStopsDirect01(
    const std::vector<GradientStop>& sortedStops,
    float clampedT01,
    float brushOpacity) noexcept {
    const float op = std::clamp(brushOpacity, 0.0f, 1.0f);
    if (sortedStops.empty()) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (sortedStops.size() == 1u || clampedT01 <= sortedStops.front().offset) {
        Color c = sortedStops.front().color;
        c.a = std::clamp(c.a * op, 0.0f, 1.0f);
        return c;
    }
    if (clampedT01 >= sortedStops.back().offset) {
        Color c = sortedStops.back().color;
        c.a = std::clamp(c.a * op, 0.0f, 1.0f);
        return c;
    }
    for (std::size_t i = 0u; i + 1u < sortedStops.size(); ++i) {
        const GradientStop& s0 = sortedStops[i];
        const GradientStop& s1 = sortedStops[i + 1u];
        if (clampedT01 >= s0.offset && clampedT01 <= s1.offset) {
            const float span = s1.offset - s0.offset;
            const float f = (span > 1e-6f) ? std::clamp((clampedT01 - s0.offset) / span, 0.0f, 1.0f) : 0.0f;
            return Color{
                s0.color.r + (s1.color.r - s0.color.r) * f,
                s0.color.g + (s1.color.g - s0.color.g) * f,
                s0.color.b + (s1.color.b - s0.color.b) * f,
                std::clamp((s0.color.a + (s1.color.a - s0.color.a) * f) * op, 0.0f, 1.0f)};
        }
    }
    Color c = sortedStops.back().color;
    c.a = std::clamp(c.a * op, 0.0f, 1.0f);
    return c;
}

void AppendHexByte(std::string& out, float norm01) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const auto b = static_cast<std::uint8_t>(std::clamp(norm01 * 255.0f + 0.5f, 0.0f, 255.0f));
    out.push_back(kHex[(b >> 4u) & 0x0Fu]);
    out.push_back(kHex[b & 0x0Fu]);
}

} // namespace

void BakeGradientBrushRamp(GradientBrushDesc& ioBrush) noexcept {
    std::vector<GradientStop> sorted = ioBrush.stops;
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [](const GradientStop& a, const GradientStop& b) noexcept {
            return a.offset < b.offset;
        });
    for (std::uint32_t i = 0u; i < GradientBrushDesc::kLutWidth; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(GradientBrushDesc::kLutWidth - 1u);
        const Color c = EvaluateStopsDirect01(sorted, t, ioBrush.opacity);
        const std::size_t base = static_cast<std::size_t>(i) * 4u;
        ioBrush.bakedRampRgba8[base + 0u] =
            static_cast<std::uint8_t>(std::clamp(c.r * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 1u] =
            static_cast<std::uint8_t>(std::clamp(c.g * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 2u] =
            static_cast<std::uint8_t>(std::clamp(c.b * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 3u] =
            static_cast<std::uint8_t>(std::clamp(c.a * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

namespace detail_eval {

template <typename BrushT>
[[nodiscard]] Color SampleGradientRampImpl(const BrushT& brush, float rawT) noexcept {
    const float t01 = ApplySpreadMethod01(rawT, brush.spreadMethod);
    const float coord = t01 * static_cast<float>(GradientBrushDesc::kLutWidth - 1u);
    const auto idx0 = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::floor(coord)), 0, static_cast<int>(GradientBrushDesc::kLutWidth - 1u)));
    const std::uint32_t idx1 = std::min<std::uint32_t>(idx0 + 1u, GradientBrushDesc::kLutWidth - 1u);
    const float frac = std::clamp(coord - static_cast<float>(idx0), 0.0f, 1.0f);

    const std::size_t b0 = static_cast<std::size_t>(idx0) * 4u;
    const std::size_t b1 = static_cast<std::size_t>(idx1) * 4u;
    constexpr float kInv255 = 1.0f / 255.0f;
    const float r0 = static_cast<float>(brush.bakedRampRgba8[b0 + 0u]) * kInv255;
    const float g0 = static_cast<float>(brush.bakedRampRgba8[b0 + 1u]) * kInv255;
    const float bl0 = static_cast<float>(brush.bakedRampRgba8[b0 + 2u]) * kInv255;
    const float a0 = static_cast<float>(brush.bakedRampRgba8[b0 + 3u]) * kInv255;

    const float r1 = static_cast<float>(brush.bakedRampRgba8[b1 + 0u]) * kInv255;
    const float g1 = static_cast<float>(brush.bakedRampRgba8[b1 + 1u]) * kInv255;
    const float bl1 = static_cast<float>(brush.bakedRampRgba8[b1 + 2u]) * kInv255;
    const float a1 = static_cast<float>(brush.bakedRampRgba8[b1 + 3u]) * kInv255;

    return Color{
        r0 + (r1 - r0) * frac,
        g0 + (g1 - g0) * frac,
        bl0 + (bl1 - bl0) * frac,
        a0 + (a1 - a0) * frac};
}

template <typename BrushT>
[[nodiscard]] Color EvaluateGradientAtNormalizedUvImpl(
    const BrushT& brush,
    float normU,
    float normV,
    float quadWidthPx,
    float quadHeightPx) noexcept {
    const float sampleX = (brush.mappingMode == BrushMappingMode::Absolute)
        ? (normU * std::max(1.0f, quadWidthPx))
        : normU;
    const float sampleY = (brush.mappingMode == BrushMappingMode::Absolute)
        ? (normV * std::max(1.0f, quadHeightPx))
        : normV;

    float t = 0.0f;
    if (brush.kind == GradientBrushKind::Linear) {
        const float dx = brush.endPoint.x - brush.startPoint.x;
        const float dy = brush.endPoint.y - brush.startPoint.y;
        const float lenSq = dx * dx + dy * dy;
        if (lenSq > 1e-8f) {
            t = ((sampleX - brush.startPoint.x) * dx + (sampleY - brush.startPoint.y) * dy) / lenSq;
        }
    } else if (brush.kind == GradientBrushKind::Conic) {
        constexpr float kTwoPi = 6.2831853071795864769f;
        constexpr float kHalfPi = 1.5707963267948966192f;
        const float dx = sampleX - brush.center.x;
        const float dy = sampleY - brush.center.y;
        float angle = std::atan2(dy, dx) + kHalfPi;
        if (angle < 0.0f) {
            angle += kTwoPi;
        } else if (angle >= kTwoPi) {
            angle -= kTwoPi;
        }
        t = angle / kTwoPi;
    } else {
        const float rx = std::max(1e-4f, std::fabs(brush.radiusX));
        const float ry = std::max(1e-4f, std::fabs(brush.radiusY));
        const float qx = (sampleX - brush.center.x) / rx;
        const float qy = (sampleY - brush.center.y) / ry;
        float fx = (brush.gradientOrigin.x - brush.center.x) / rx;
        float fy = (brush.gradientOrigin.y - brush.center.y) / ry;
        const float fLenSq = fx * fx + fy * fy;
        if (fLenSq < 1e-6f) {
            t = std::sqrt(qx * qx + qy * qy);
        } else {
            if (fLenSq > 0.9801f) {
                const float invFLen = 0.99f / std::sqrt(fLenSq);
                fx *= invFLen;
                fy *= invFLen;
            }
            const float dx = qx - fx;
            const float dy = qy - fy;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= 1e-10f) {
                t = 0.0f;
            } else {
                const float a = 1.0f - (fx * fx + fy * fy);
                const float b = fx * dx + fy * dy;
                const float denom = b + std::sqrt(std::max(0.0f, b * b + a * d2));
                t = (denom > 1e-8f) ? (d2 / denom) : 1.0f;
            }
        }
    }

    return SampleGradientRampImpl(brush, t);
}

void FillGradientEvalPod(const GradientBrushDesc& src, GradientEvalPod& dst) noexcept {
    dst.brushId = src.brushId;
    dst.kind = src.kind;
    dst.mappingMode = src.mappingMode;
    dst.spreadMethod = src.spreadMethod;
    dst.opacity = src.opacity;
    dst.startPoint = src.startPoint;
    dst.endPoint = src.endPoint;
    dst.center = src.center;
    dst.gradientOrigin = src.gradientOrigin;
    dst.radiusX = src.radiusX;
    dst.radiusY = src.radiusY;
    dst.bakedRampRgba8 = src.bakedRampRgba8;
}

} // namespace detail_eval

Color SampleGradientRampAtT(const GradientBrushDesc& brush, float rawT) noexcept {
    return detail_eval::SampleGradientRampImpl(brush, rawT);
}

Color EvaluateGradientBrushAtNormalizedUv(
    const GradientBrushDesc& brush,
    float normU,
    float normV,
    float quadWidthPx,
    float quadHeightPx) noexcept {
    return detail_eval::EvaluateGradientAtNormalizedUvImpl(brush, normU, normV, quadWidthPx, quadHeightPx);
}

Color SampleGradientPodAtT(const GradientEvalPod& brush, float rawT) noexcept {
    return detail_eval::SampleGradientRampImpl(brush, rawT);
}

Color EvaluateGradientPodAtNormalizedUv(
    const GradientEvalPod& brush,
    float normU,
    float normV,
    float quadWidthPx,
    float quadHeightPx) noexcept {
    return detail_eval::EvaluateGradientAtNormalizedUvImpl(brush, normU, normV, quadWidthPx, quadHeightPx);
}

bool IsGradientBrushSpec(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    return raw.size() > kGradientSpecPrefix.size() &&
           str::StartsWith(raw, kGradientSpecPrefix);
}

std::string SerializeGradientBrushSpec(const GradientBrushDesc& brush) {
    std::string out(kGradientSpecPrefix);
    char kindChar = 'L';
    if (brush.kind == GradientBrushKind::Radial) kindChar = 'R';
    else if (brush.kind == GradientBrushKind::Conic) kindChar = 'C';
    out.push_back(kindChar);
    out.push_back(';');
    out.push_back(brush.mappingMode == BrushMappingMode::Absolute ? '1' : '0');
    out.push_back(',');
    out.push_back(static_cast<char>('0' + (static_cast<int>(brush.spreadMethod) % 3)));
    out.push_back(',');
    detail::AppendCompactFloat(out, brush.opacity);
    out.push_back(';');
    if (brush.kind == GradientBrushKind::Linear) {
        detail::AppendCompactFloat(out, brush.startPoint.x);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.startPoint.y);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.endPoint.x);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.endPoint.y);
    } else {
        detail::AppendCompactFloat(out, brush.center.x);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.center.y);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.gradientOrigin.x);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.gradientOrigin.y);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.radiusX);
        out.push_back(',');
        detail::AppendCompactFloat(out, brush.radiusY);
    }
    for (const auto& s : brush.stops) {
        out.push_back(';');
        detail::AppendCompactFloat(out, s.offset);
        out.push_back(':');
        out.push_back('#');
        AppendHexByte(out, s.color.a);
        AppendHexByte(out, s.color.r);
        AppendHexByte(out, s.color.g);
        AppendHexByte(out, s.color.b);
    }
    return out;
}

bool TryDeserializeGradientBrushSpec(std::string_view raw, GradientBrushDesc& outBrush) {
    raw = str::Trim(raw);
    if (!IsGradientBrushSpec(raw)) {
        return false;
    }
    std::string_view body = raw.substr(kGradientSpecPrefix.size());
    std::vector<std::string_view> tokens;
    std::size_t pos = 0u;
    while (pos <= body.size()) {
        const std::size_t semi = body.find(';', pos);
        if (semi == std::string_view::npos) {
            tokens.push_back(body.substr(pos));
            break;
        }
        tokens.push_back(body.substr(pos, semi - pos));
        pos = semi + 1u;
    }
    if (tokens.size() < 3u) {
        return false;
    }

    GradientBrushDesc parsed{};
    if (!tokens[0].empty() && (tokens[0][0] == 'R' || tokens[0][0] == 'r')) {
        parsed.kind = GradientBrushKind::Radial;
    } else if (!tokens[0].empty() && (tokens[0][0] == 'C' || tokens[0][0] == 'c')) {
        parsed.kind = GradientBrushKind::Conic;
    } else {
        parsed.kind = GradientBrushKind::Linear;
    }

    const auto hdrNums = detail::ParseFloatList(tokens[1]);
    if (hdrNums.size() >= 1u) {
        parsed.mappingMode = (static_cast<int>(std::lround(hdrNums[0])) != 0)
            ? BrushMappingMode::Absolute
            : BrushMappingMode::RelativeToBoundingBox;
    }
    if (hdrNums.size() >= 2u) {
        const int sp = std::clamp(static_cast<int>(std::lround(hdrNums[1])), 0, 2);
        parsed.spreadMethod = static_cast<GradientSpreadMethod>(sp);
    }
    if (hdrNums.size() >= 3u) {
        parsed.opacity = std::clamp(hdrNums[2], 0.0f, 1.0f);
    }

    const auto geomNums = detail::ParseFloatList(tokens[2]);
    if (parsed.kind == GradientBrushKind::Linear) {
        if (geomNums.size() >= 4u) {
            parsed.startPoint = Point{geomNums[0], geomNums[1]};
            parsed.endPoint = Point{geomNums[2], geomNums[3]};
        }
    } else {
        if (geomNums.size() >= 6u) {
            parsed.center = Point{geomNums[0], geomNums[1]};
            parsed.gradientOrigin = Point{geomNums[2], geomNums[3]};
            parsed.radiusX = geomNums[4];
            parsed.radiusY = geomNums[5];
        }
    }

    for (std::size_t i = 3u; i < tokens.size(); ++i) {
        const std::string_view st = str::Trim(tokens[i]);
        const std::size_t colon = st.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        GradientStop stop{};
        stop.offset = detail::ParseFloat32Token(st.substr(0u, colon), 0.0f);
        stop.color = ParseColorLiteral(st.substr(colon + 1u));
        parsed.stops.push_back(stop);
    }

    BakeGradientBrushRamp(parsed);
    outBrush = std::move(parsed);
    return true;
}

GradientBrushRegistry& GradientBrushRegistry::Instance() noexcept {
    return UIResourceStore::Default().GradientBrushes();
}

std::uint32_t GradientBrushRegistry::RegisterBrush(GradientBrushDesc desc) {
    BakeGradientBrushRamp(desc);
    const std::string canonicalSpec = SerializeGradientBrushSpec(desc);
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = specToId_.find(canonicalSpec); it != specToId_.end()) {
        return it->second;
    }
    if (brushes_.size() >= kMaxBrushId) {
        return brushes_.back().brushId;
    }
    const auto newId = static_cast<std::uint32_t>(brushes_.size() + 1u);
    desc.brushId = newId;
    desc.isMutableClone = false;
    brushes_.push_back(std::move(desc));
    specToId_.emplace(canonicalSpec, newId);
    revision_.fetch_add(1u, std::memory_order_release);
    return newId;
}

std::uint32_t GradientBrushRegistry::RegisterOrResolveSpec(std::string_view serializedSpec) {
    const std::string key(str::Trim(serializedSpec));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = specToId_.find(key); it != specToId_.end()) {
            return it->second;
        }
    }
    GradientBrushDesc desc{};
    if (!TryDeserializeGradientBrushSpec(key, desc)) {
        return 0u;
    }
    return RegisterBrush(std::move(desc));
}

std::uint32_t GradientBrushRegistry::CloneMutableBrush(std::uint32_t sourceBrushId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sourceBrushId == 0u || sourceBrushId > brushes_.size()) {
        return 0u;
    }
    if (brushes_[sourceBrushId - 1u].brushId == 0u) {
        return 0u;
    }
    GradientBrushDesc copy = brushes_[sourceBrushId - 1u];
    copy.isMutableClone = true;
    if (!freeMutableBrushIds_.empty()) {
        const std::uint32_t reuseId = freeMutableBrushIds_.back();
        freeMutableBrushIds_.pop_back();
        copy.brushId = reuseId;
        brushes_[reuseId - 1u] = std::move(copy);
        revision_.fetch_add(1u, std::memory_order_release);
        return reuseId;
    }
    if (brushes_.size() >= kMaxBrushId) {
        return 0u;
    }
    const auto newId = static_cast<std::uint32_t>(brushes_.size() + 1u);
    copy.brushId = newId;
    brushes_.push_back(std::move(copy));
    revision_.fetch_add(1u, std::memory_order_release);
    return newId;
}

bool GradientBrushRegistry::ReleaseMutableBrush(std::uint32_t brushId) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size()) {
        return false;
    }
    GradientBrushDesc& brush = brushes_[brushId - 1u];
    if (brush.brushId == 0u || !brush.isMutableClone) {
        return false;
    }
    std::vector<GradientStop>().swap(brush.stops);
    brush.brushId = 0u;
    brush.isMutableClone = false;
    freeMutableBrushIds_.push_back(brushId);
    revision_.fetch_add(1u, std::memory_order_release);
    return true;
}

std::uint32_t GradientBrushRegistry::RegisterOrUpdateMutableLinearGradient(
    std::uint32_t reuseBrushId,
    Point startPoint,
    Point endPoint,
    GradientSpreadMethod spreadMethod,
    BrushMappingMode mappingMode,
    const std::vector<GradientStop>& stops) {
    GradientBrushDesc desc{};
    desc.kind = GradientBrushKind::Linear;
    desc.startPoint = startPoint;
    desc.endPoint = endPoint;
    desc.spreadMethod = spreadMethod;
    desc.mappingMode = mappingMode;
    desc.stops = stops;
    desc.isMutableClone = true;
    BakeGradientBrushRamp(desc);

    std::lock_guard<std::mutex> lock(mutex_);
    if (reuseBrushId > 0u && reuseBrushId <= brushes_.size()) {
        GradientBrushDesc& existing = brushes_[reuseBrushId - 1u];
        if (existing.brushId == reuseBrushId && existing.isMutableClone) {
            desc.brushId = reuseBrushId;
            existing = std::move(desc);
            revision_.fetch_add(1u, std::memory_order_release);
            return reuseBrushId;
        }
    }
    if (!freeMutableBrushIds_.empty()) {
        const std::uint32_t id = freeMutableBrushIds_.back();
        freeMutableBrushIds_.pop_back();
        desc.brushId = id;
        brushes_[id - 1u] = std::move(desc);
        revision_.fetch_add(1u, std::memory_order_release);
        return id;
    }
    if (brushes_.size() >= kMaxBrushId) {
        return brushes_.back().brushId;
    }
    const auto newId = static_cast<std::uint32_t>(brushes_.size() + 1u);
    desc.brushId = newId;
    brushes_.push_back(std::move(desc));
    revision_.fetch_add(1u, std::memory_order_release);
    return newId;
}

bool GradientBrushRegistry::UpdateBrushStopColor(
    std::uint32_t brushId,
    std::size_t stopIndex,
    const Color& color) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size()) {
        return false;
    }
    GradientBrushDesc& brush = brushes_[brushId - 1u];
    if (brush.brushId == 0u || stopIndex >= brush.stops.size()) {
        return false;
    }
    brush.stops[stopIndex].color = color;
    BakeGradientBrushRamp(brush);
    revision_.fetch_add(1u, std::memory_order_release);
    return true;
}

bool GradientBrushRegistry::UpdateBrushStopOffset(
    std::uint32_t brushId,
    std::size_t stopIndex,
    float offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size()) {
        return false;
    }
    GradientBrushDesc& brush = brushes_[brushId - 1u];
    if (brush.brushId == 0u || stopIndex >= brush.stops.size()) {
        return false;
    }
    brush.stops[stopIndex].offset = std::clamp(offset, 0.0f, 1.0f);
    BakeGradientBrushRamp(brush);
    revision_.fetch_add(1u, std::memory_order_release);
    return true;
}

const GradientBrushDesc* GradientBrushRegistry::Find(std::uint32_t brushId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size() || brushes_[brushId - 1u].brushId == 0u) {
        return nullptr;
    }
    return &brushes_[brushId - 1u];
}

bool GradientBrushRegistry::TryGetBrush(std::uint32_t brushId, GradientBrushDesc& outBrush) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size() || brushes_[brushId - 1u].brushId == 0u) {
        return false;
    }
    outBrush = brushes_[brushId - 1u];
    return true;
}

bool GradientBrushRegistry::TryGetStopValue(
    std::uint32_t brushId,
    std::size_t stopIndex,
    float& outOffset,
    Color& outColor) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size()) {
        return false;
    }
    const GradientBrushDesc& brush = brushes_[brushId - 1u];
    if (brush.brushId == 0u || stopIndex >= brush.stops.size()) {
        return false;
    }
    outOffset = brush.stops[stopIndex].offset;
    outColor = brush.stops[stopIndex].color;
    return true;
}

bool GradientBrushRegistry::TryGetEvalPod(std::uint32_t brushId, GradientEvalPod& outPod) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size() || brushes_[brushId - 1u].brushId == 0u) {
        return false;
    }
    detail_eval::FillGradientEvalPod(brushes_[brushId - 1u], outPod);
    return true;
}

bool GradientBrushRegistry::TryGetEvalPodWithRevision(
    std::uint32_t brushId,
    GradientEvalPod& outPod,
    std::uint32_t& outRevision) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size() || brushes_[brushId - 1u].brushId == 0u) {
        return false;
    }
    detail_eval::FillGradientEvalPod(brushes_[brushId - 1u], outPod);
    outRevision = revision_.load(std::memory_order_acquire);
    return true;
}

std::size_t GradientBrushRegistry::BrushCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return brushes_.size();
}

void GradientBrushRegistry::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    brushes_.clear();
    freeMutableBrushIds_.clear();
    specToId_.clear();
    revision_.fetch_add(1u, std::memory_order_release);
}

} // namespace aero
