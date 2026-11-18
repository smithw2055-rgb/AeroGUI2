#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aero/drawing/Geometry.hpp"

namespace aero {

// Offline / on-demand 4-channel MTSDF (Multi-channel + True Signed Distance Field)
// atlas entry for SVG <Path Data="M..."/> & <Geometry> vector icons.
struct AxbMtsdfPathEntry {
    std::uint64_t pathHash = 0;
    float boundsX = 0.0f;
    float boundsY = 0.0f;
    float boundsW = 32.0f;
    float boundsH = 32.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    std::uint16_t atlasWidth = 256;
    std::uint16_t atlasHeight = 256;
    std::uint16_t cellWidth = 32;
    std::uint16_t cellHeight = 32;
    float pxRange = 4.0f;
    // 4-channel RGBA8 MTSDF cell (R,G,B = corner-preserving directional SDF, A = true Euclidean SDF)
    std::vector<std::uint8_t> mtsdfRgbaPixels;
    // Cached per-row non-zero RGB bounds (transient runtime cache; not serialized into .axb4)
    std::vector<std::int16_t> rowMinX;
    std::vector<std::int16_t> rowMaxX;

    void ComputeRowBounds() noexcept {
        if (cellWidth == 0u || cellHeight == 0u ||
            mtsdfRgbaPixels.size() < static_cast<std::size_t>(cellWidth) * cellHeight * 4u) {
            rowMinX.clear();
            rowMaxX.clear();
            return;
        }
        const int cw = static_cast<int>(cellWidth);
        const int ch = static_cast<int>(cellHeight);
        rowMinX.resize(static_cast<std::size_t>(ch));
        rowMaxX.resize(static_cast<std::size_t>(ch));
        const std::uint8_t* mBase = mtsdfRgbaPixels.data();
        for (int ry = 0; ry < ch; ++ry) {
            const std::uint8_t* rPtr = mBase + static_cast<std::size_t>(ry) * static_cast<std::size_t>(cw) * 4u;
            std::int16_t rMin = static_cast<std::int16_t>(cw);
            std::int16_t rMax = -1;
            for (int rx = 0; rx < cw; ++rx) {
                const std::uint8_t* p = rPtr + static_cast<std::size_t>(rx) * 4u;
                if ((p[0] | p[1] | p[2]) != 0u) {
                    if (rx < rMin) rMin = static_cast<std::int16_t>(rx);
                    rMax = static_cast<std::int16_t>(rx);
                }
            }
            rowMinX[static_cast<std::size_t>(ry)] = rMin;
            rowMaxX[static_cast<std::size_t>(ry)] = rMax;
        }
    }
};

struct SvgPathContour {
    std::vector<Point> points;
    std::vector<std::uint8_t> isSmoothVertex;
    bool closed = false;
};

struct SvgPathTessellationOptions {
    int cubicSteps = 8;
    int quadSteps = 6;
    float arcStepAngleRad = 3.14159265358979323846f / 10.0f;
    int arcMinSteps = 2;
    int arcMaxSteps = 48;
};

struct ParsedSvgGeometry {
    bool nonZeroFill = false; // Default XAML F0 = EvenOdd, F1 = NonZero
    std::vector<SvgPathContour> contours;
    Rect bounds{0.0f, 0.0f, 0.0f, 0.0f};
};

// W3C SVG 1.1 Appendix F.6.5 & F.6.6 endpoint-to-center elliptical arc tessellation
void AppendSvgArcPoints(
    Point p0,
    float rx,
    float ry,
    float xAxisRotDeg,
    bool largeArc,
    bool sweep,
    Point p1,
    std::vector<Point>& outPoints,
    float stepAngleRad = 3.14159265358979323846f / 10.0f,
    int minSteps = 2,
    int maxSteps = 48);

[[nodiscard]] ParsedSvgGeometry ParseSvgPathGeometry(
    std::string_view pathData,
    const SvgPathTessellationOptions& options = SvgPathTessellationOptions{});

[[nodiscard]] std::vector<std::vector<Point>> ParseSvgPathToSubpaths(
    std::string_view pathData);

[[nodiscard]] std::string ConvertPointsToSvgPath(
    std::string_view rawPoints,
    bool closePath);

[[nodiscard]] std::uint64_t ComputeSvgPathHash(std::string_view pathData) noexcept;
[[nodiscard]] Rect MeasureSvgPathBounds(std::string_view pathData);
[[nodiscard]] std::uint16_t SelectAdaptiveMtsdfCellSize(
    std::string_view pathData,
    float designMaxSpanPx = 32.0f,
    float* outRecommendedPxRange = nullptr) noexcept;
[[nodiscard]] AxbMtsdfPathEntry BakeSvgPathToMtsdfEntry(
    std::string_view pathData,
    std::uint16_t cellSize = 32,
    float pxRange = 4.0f,
    std::uint16_t atlasSlotIndex = 0,
    std::uint16_t atlasSize = 512);
void PackMtsdfAtlasSkyline(
    std::vector<AxbMtsdfPathEntry>& ioEntries,
    std::uint16_t atlasSize = 512) noexcept;

/// Rasterize an SVG path's fill geometry into an 8x supersampled analytical horizontal-coverage
/// RGBA8 mask (white RGB with anti-aliased alpha channel) surrounded by a `padPx` transparent border.
bool RasterizeSvgPathFillAlphaMask(
    std::string_view pathData,
    const Rect& pathBounds,
    std::uint32_t innerWidth,
    std::uint32_t innerHeight,
    std::uint32_t padPx,
    std::vector<std::uint8_t>& outRgba8);

namespace detail {

void SkipSvgDelimiters(std::string_view s, std::size_t& pos) noexcept;
[[nodiscard]] bool TryReadSvgFloat(std::string_view s, std::size_t& pos, float& outVal) noexcept;
[[nodiscard]] float ParseFloat32Token(std::string_view text, float fallback = 0.0f) noexcept;
[[nodiscard]] std::vector<float> ParseFloatList(std::string_view text);
void AppendCompactFloat(std::string& out, float v);

} // namespace detail

} // namespace aero
