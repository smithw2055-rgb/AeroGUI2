#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/drawing/Geometry.hpp"

namespace aero {

// ============================================================================
// P1-3: Multi-Stop LinearGradientBrush & RadialGradientBrush Registry + 1D LUT
// ============================================================================

enum class GradientBrushKind : std::uint8_t {
    Linear = 0,
    Radial = 1,
    Conic = 2,
};

enum class BrushMappingMode : std::uint8_t {
    RelativeToBoundingBox = 0,
    Absolute = 1,
};

enum class GradientSpreadMethod : std::uint8_t {
    Pad = 0,
    Reflect = 1,
    Repeat = 2,
};

struct GradientStop {
    float offset = 0.0f;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct GradientBrushDesc {
    static constexpr std::uint32_t kLutWidth = 256u;

    std::uint32_t brushId = 0u;
    GradientBrushKind kind = GradientBrushKind::Linear;
    BrushMappingMode mappingMode = BrushMappingMode::RelativeToBoundingBox;
    GradientSpreadMethod spreadMethod = GradientSpreadMethod::Pad;
    float opacity = 1.0f;
    // LinearGradientBrush endpoints (default XAML diagonal 0,0 -> 1,1)
    Point startPoint{0.0f, 0.0f};
    Point endPoint{1.0f, 1.0f};
    // RadialGradientBrush ellipse center, focal origin & radii (default 0.5,0.5, rx=0.5, ry=0.5)
    Point center{0.5f, 0.5f};
    Point gradientOrigin{0.5f, 0.5f};
    float radiusX = 0.5f;
    float radiusY = 0.5f;
    std::vector<GradientStop> stops;
    // Pre-baked 256-texel RGBA8 1D ramp for O(1) GPU LUT row upload & CPU bilinear evaluation
    std::array<std::uint8_t, kLutWidth * 4u> bakedRampRgba8{};
};

void BakeGradientBrushRamp(GradientBrushDesc& ioBrush) noexcept;
[[nodiscard]] Color SampleGradientRampAtT(
    const GradientBrushDesc& brush,
    float rawT) noexcept;
[[nodiscard]] Color EvaluateGradientBrushAtNormalizedUv(
    const GradientBrushDesc& brush,
    float normU,
    float normV,
    float quadWidthPx = 1.0f,
    float quadHeightPx = 1.0f) noexcept;
[[nodiscard]] std::string SerializeGradientBrushSpec(const GradientBrushDesc& brush);
[[nodiscard]] bool IsGradientBrushSpec(std::string_view raw) noexcept;
[[nodiscard]] bool TryDeserializeGradientBrushSpec(
    std::string_view raw,
    GradientBrushDesc& outBrush);

class GradientBrushRegistry {
public:
    static constexpr std::uint32_t kMaxBrushId = 4095u;

    static GradientBrushRegistry& Instance() noexcept;

    GradientBrushRegistry() noexcept = default;
    ~GradientBrushRegistry() = default;

    GradientBrushRegistry(const GradientBrushRegistry&) = delete;
    GradientBrushRegistry& operator=(const GradientBrushRegistry&) = delete;

    std::uint32_t RegisterBrush(GradientBrushDesc desc);
    std::uint32_t RegisterOrResolveSpec(std::string_view serializedSpec);
    [[nodiscard]] const GradientBrushDesc* Find(std::uint32_t brushId) const noexcept;
    [[nodiscard]] bool TryGetBrush(std::uint32_t brushId, GradientBrushDesc& outBrush) const noexcept;
    [[nodiscard]] std::size_t BrushCount() const noexcept;
    [[nodiscard]] std::uint32_t Revision() const noexcept;
    void Clear() noexcept;

private:
    mutable std::mutex mutex_;
    std::deque<GradientBrushDesc> brushes_;
    std::unordered_map<std::string, std::uint32_t> specToId_;
    std::uint32_t revision_ = 1u;
};

} // namespace aero
