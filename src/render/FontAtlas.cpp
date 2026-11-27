#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aero/core/StringUtils.hpp"
#include "aero/render/UIResourceStore.hpp"

namespace aero {

namespace {

void WriteShardU16LE(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
}

void WriteShardU32LE(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
}

void WriteShardF32LE(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(float));
    WriteShardU32LE(out, bits);
}

struct ShardReaderLE {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
    std::size_t offset = 0u;

    bool ReadU16(std::uint16_t& out) noexcept {
        if (offset > size || 2u > (size - offset)) {
            return false;
        }
        out = static_cast<std::uint16_t>(data[offset]) |
              static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1u]) << 8u);
        offset += 2u;
        return true;
    }

    bool ReadU32(std::uint32_t& out) noexcept {
        if (offset > size || 4u > (size - offset)) {
            return false;
        }
        out = static_cast<std::uint32_t>(data[offset + 0u]) |
              (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
              (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
              (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
        offset += 4u;
        return true;
    }

    bool ReadF32(float& out) noexcept {
        std::uint32_t bits = 0u;
        if (!ReadU32(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }
};

[[nodiscard]] std::uint8_t QuantizeSdfByte(float signedDistTexels, float pxRange) noexcept {
    const float safeRange = std::max(pxRange, 1.0f);
    const float norm = std::clamp(0.5f + (signedDistTexels / (2.0f * safeRange)), 0.0f, 1.0f);
    return static_cast<std::uint8_t>(norm * 255.0f + 0.5f);
}

[[nodiscard]] float DistSqPointToLine(
    float px, float py,
    float ax, float ay,
    float bx, float by) noexcept {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float lenSq = abx * abx + aby * aby;
    if (lenSq <= 1e-10f) {
        const float dx = px - ax;
        const float dy = py - ay;
        return dx * dx + dy * dy;
    }
    const float t = std::clamp(((px - ax) * abx + (py - ay) * aby) / lenSq, 0.0f, 1.0f);
    const float dx = px - (ax + t * abx);
    const float dy = py - (ay + t * aby);
    return dx * dx + dy * dy;
}

// Analytical squared distance from P(px, py) to quadratic Bezier B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2
[[nodiscard]] float DistSqPointToQuadBezier(
    float px, float py,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2) noexcept {
    const float ax = x1 - x0;
    const float ay = y1 - y0;
    const float bx = x0 - 2.0f * x1 + x2;
    const float by = y0 - 2.0f * y1 + y2;
    const float dx0 = x0 - px;
    const float dy0 = y0 - py;
    const float dx2 = x2 - px;
    const float dy2 = y2 - py;

    float minDistSq = std::min(dx0 * dx0 + dy0 * dy0, dx2 * dx2 + dy2 * dy2);

    const float bb = bx * bx + by * by;
    const float aa = ax * ax + ay * ay;
    if (bb <= 1e-5f * (aa + 1.0f)) {
        // Nearly colinear control point: subdivide into 4 chords for guaranteed 32-bit float stability
        float prevX = x0;
        float prevY = y0;
        for (int s = 1; s <= 4; ++s) {
            const float t = static_cast<float>(s) * 0.25f;
            const float u = 1.0f - t;
            const float curX = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
            const float curY = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
            minDistSq = std::min(minDistSq, DistSqPointToLine(px, py, prevX, prevY, curX, curY));
            prevX = curX;
            prevY = curY;
        }
        return minDistSq;
    }

    const float kk = 1.0f / bb;
    const float kx = kk * (ax * bx + ay * by);
    const float ky = kk * (2.0f * aa + dx0 * bx + dy0 * by) * (1.0f / 3.0f);
    const float kz = kk * (dx0 * ax + dy0 * ay);

    const float p = ky - kx * kx;
    const float p3 = p * p * p;
    const float q = kx * (2.0f * kx * kx - 3.0f * ky) + kz;
    const float h = q * q + 4.0f * p3;

    auto evalAt = [&](float t) noexcept {
        const float tc = std::clamp(t, 0.0f, 1.0f);
        const float u = 1.0f - tc;
        const float qx = u * u * x0 + 2.0f * u * tc * x1 + tc * tc * x2 - px;
        const float qy = u * u * y0 + 2.0f * u * tc * y1 + tc * tc * y2 - py;
        minDistSq = std::min(minDistSq, qx * qx + qy * qy);
    };

    if (h >= 0.0f) {
        const float sh = std::sqrt(h);
        const float u = std::cbrt((-q + sh) * 0.5f) + std::cbrt((-q - sh) * 0.5f);
        evalAt(u - kx);
    } else {
        const float z = std::sqrt(std::max(0.0f, -p));
        const float cosArg = std::clamp(q / std::max(1e-12f, -p * z * 2.0f), -1.0f, 1.0f);
        // Note: q / (p * z * 2) = -q / (-p * z * 2)
        const float v = std::acos(std::clamp(-cosArg, -1.0f, 1.0f)) * (1.0f / 3.0f);
        const float m = std::cos(v);
        const float n = std::sin(v) * 1.732050808f;
        evalAt((m + m) * z - kx);
        evalAt((-n - m) * z - kx);
        evalAt((n - m) * z - kx);
    }
    return minDistSq;
}

struct CellDistCurve {
    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float minX = 0.0f, minY = 0.0f;
    float maxX = 0.0f, maxY = 0.0f;
    bool isQuadratic = false;
};

// Y-monotonic curve piece (line or De Casteljau split quadratic Bezier) for exact ray-crossing winding
struct YMonotonicSeg {
    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float minY = 0.0f, maxY = 0.0f;
    float maxX = 0.0f;
    bool isQuadratic = false;
};

void AppendYMonotonicQuad(
    std::vector<YMonotonicSeg>& out,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2) {
    if (std::fabs(y2 - y0) <= 1e-6f && std::fabs(y1 - y0) <= 1e-6f) {
        return;
    }
    const float denom = y0 - 2.0f * y1 + y2;
    if (std::fabs(denom) > 1e-5f) {
        const float tExt = (y0 - y1) / denom;
        if (tExt > 1e-4f && tExt < 1.0f - 1e-4f) {
            // Split at Y-extremum via De Casteljau into two strictly Y-monotonic quadratic curves
            const float u = 1.0f - tExt;
            const float q0x = u * x0 + tExt * x1;
            const float q0y = u * y0 + tExt * y1;
            const float q1x = u * x1 + tExt * x2;
            const float q1y = u * y1 + tExt * y2;
            const float mx = u * q0x + tExt * q1x;
            const float my = u * q0y + tExt * q1y;

            if (std::fabs(my - y0) > 1e-6f) {
                out.push_back(YMonotonicSeg{
                    x0, y0, q0x, q0y, mx, my,
                    std::min(y0, my), std::max(y0, my),
                    std::max({x0, q0x, mx}),
                    true});
            }
            if (std::fabs(y2 - my) > 1e-6f) {
                out.push_back(YMonotonicSeg{
                    mx, my, q1x, q1y, x2, y2,
                    std::min(my, y2), std::max(my, y2),
                    std::max({mx, q1x, x2}),
                    true});
            }
            return;
        }
    }
    if (std::fabs(y2 - y0) > 1e-6f) {
        out.push_back(YMonotonicSeg{
            x0, y0, x1, y1, x2, y2,
            std::min(y0, y2), std::max(y0, y2),
            std::max({x0, x1, x2}),
            true});
    }
}

// EvaluateWindingNumber precomputed per-row in BakeOutlineToSdfCell.

// Compact crisp 8x12 printable ASCII bitmap font (U+0020 .. U+007E, 95 glyphs * 12 bytes = 1,140 bytes)
// Used ONLY as built-in zero-file fallback so headless tests without .ttf render crisp readable ASCII SDF text.
alignas(4) constexpr std::uint8_t kBuiltInAscii8x12[95][12] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 0x21 '!'
    {0x00,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x22 '"'
    {0x00,0x36,0x36,0x7F,0x36,0x36,0x7F,0x36,0x36,0x00,0x00,0x00}, // 0x23 '#'
    {0x18,0x3E,0x63,0x60,0x3E,0x03,0x63,0x3E,0x18,0x00,0x00,0x00}, // 0x24 '$'
    {0x00,0x63,0x66,0x0C,0x18,0x30,0x66,0xC6,0x00,0x00,0x00,0x00}, // 0x25 '%'
    {0x00,0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x26 '&'
    {0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x27 '\''
    {0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00}, // 0x28 '('
    {0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00}, // 0x29 ')'
    {0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, // 0x2A '*'
    {0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, // 0x2B '+'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00}, // 0x2C ','
    {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x2D '-'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00}, // 0x2E '.'
    {0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x00,0x00,0x00}, // 0x2F '/'
    {0x00,0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x30 '0'
    {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00}, // 0x31 '1'
    {0x00,0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0x00,0x00,0x00,0x00}, // 0x32 '2'
    {0x00,0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x33 '3'
    {0x00,0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x1E,0x00,0x00,0x00,0x00}, // 0x34 '4'
    {0x00,0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x35 '5'
    {0x00,0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x36 '6'
    {0x00,0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x37 '7'
    {0x00,0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x38 '8'
    {0x00,0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00,0x00,0x00,0x00}, // 0x39 '9'
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x3A ':'
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00}, // 0x3B ';'
    {0x00,0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00}, // 0x3C '<'
    {0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x3D '='
    {0x00,0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00}, // 0x3E '>'
    {0x00,0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x18,0x00,0x00,0x00}, // 0x3F '?'
    {0x00,0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00,0x00,0x00,0x00}, // 0x40 '@'
    {0x00,0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x41 'A'
    {0x00,0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, // 0x42 'B'
    {0x00,0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x43 'C'
    {0x00,0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00,0x00,0x00,0x00}, // 0x44 'D'
    {0x00,0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00,0x00,0x00,0x00}, // 0x45 'E'
    {0x00,0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00,0x00,0x00,0x00}, // 0x46 'F'
    {0x00,0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00,0x00,0x00,0x00}, // 0x47 'G'
    {0x00,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x48 'H'
    {0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x49 'I'
    {0x00,0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00,0x00,0x00,0x00}, // 0x4A 'J'
    {0x00,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00,0x00,0x00,0x00}, // 0x4B 'K'
    {0x00,0x60,0x60,0x60,0x60,0x60,0x66,0x7E,0x00,0x00,0x00,0x00}, // 0x4C 'L'
    {0x00,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00,0x00,0x00,0x00}, // 0x4D 'M'
    {0x00,0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x4E 'N'
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x4F 'O'
    {0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,0x00,0x00,0x00}, // 0x50 'P'
    {0x00,0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00,0x00,0x00,0x00}, // 0x51 'Q'
    {0x00,0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x52 'R'
    {0x00,0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x53 'S'
    {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x54 'T'
    {0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x55 'U'
    {0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00}, // 0x56 'V'
    {0x00,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00,0x00,0x00,0x00}, // 0x57 'W'
    {0x00,0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x58 'X'
    {0x00,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x59 'Y'
    {0x00,0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00,0x00}, // 0x5A 'Z'
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00}, // 0x5B '['
    {0x00,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x00,0x00,0x00,0x00}, // 0x5C '\\'
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00}, // 0x5D ']'
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x5E '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00}, // 0x5F '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x60 '`'
    {0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00,0x00,0x00,0x00}, // 0x61 'a'
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, // 0x62 'b'
    {0x00,0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x63 'c'
    {0x00,0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00}, // 0x64 'd'
    {0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00,0x00,0x00,0x00}, // 0x65 'e'
    {0x00,0x1C,0x36,0x30,0x78,0x30,0x30,0x30,0x00,0x00,0x00,0x00}, // 0x66 'f'
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00,0x00}, // 0x67 'g'
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x68 'h'
    {0x00,0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x69 'i'
    {0x00,0x06,0x00,0x0E,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00}, // 0x6A 'j'
    {0x00,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00,0x00,0x00,0x00}, // 0x6B 'k'
    {0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x6C 'l'
    {0x00,0x00,0x00,0x66,0x7F,0x6B,0x63,0x63,0x00,0x00,0x00,0x00}, // 0x6D 'm'
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x6E 'n'
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x6F 'o'
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,0x00}, // 0x70 'p'
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06,0x06,0x00,0x00}, // 0x71 'q'
    {0x00,0x00,0x00,0x6E,0x76,0x60,0x60,0x60,0x00,0x00,0x00,0x00}, // 0x72 'r'
    {0x00,0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,0x00,0x00,0x00}, // 0x73 's'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00}, // 0x74 't'
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00}, // 0x75 'u'
    {0x00,0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00}, // 0x76 'v'
    {0x00,0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00,0x00,0x00,0x00}, // 0x77 'w'
    {0x00,0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00,0x00,0x00}, // 0x78 'x'
    {0x00,0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00,0x00}, // 0x79 'y'
    {0x00,0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00,0x00,0x00,0x00}, // 0x7A 'z'
    {0x00,0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00,0x00,0x00,0x00}, // 0x7B '{'
    {0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x7C '|'
    {0x00,0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00,0x00,0x00,0x00}, // 0x7D '}'
    {0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7E '~'
};

// Bake an 8x12 crisp bitmap glyph into a smooth 20x28 Euclidean SDF cell (pxRange = 3.0f)
void BakeBitmap8x12ToSdfCell(
    const std::uint8_t rows[12],
    std::uint16_t cellW,
    std::uint16_t cellH,
    float pxRange,
    std::vector<std::uint8_t>& outPixels) {
    outPixels.assign(static_cast<std::size_t>(cellW) * cellH, 0u);
    constexpr float kScale = 2.0f; // 8x12 -> 16x24 inside 20x28 (2px outer SDF gutter on all sides)
    constexpr float kPadX = 2.0f;
    constexpr float kPadY = 2.0f;

    struct Box2D {
        float x0, y0, x1, y1;
    };
    std::vector<Box2D> activeBoxes;
    activeBoxes.reserve(48u);
    for (int r = 0; r < 12; ++r) {
        const std::uint8_t mask = rows[r];
        if (mask == 0u) {
            continue;
        }
        int c = 0;
        while (c < 8) {
            if ((mask & (0x80u >> c)) == 0u) {
                ++c;
                continue;
            }
            int cEnd = c + 1;
            while (cEnd < 8 && (mask & (0x80u >> cEnd)) != 0u) {
                ++cEnd;
            }
            activeBoxes.push_back(Box2D{
                kPadX + static_cast<float>(c) * kScale,
                kPadY + static_cast<float>(r) * kScale,
                kPadX + static_cast<float>(cEnd) * kScale,
                kPadY + static_cast<float>(r + 1) * kScale});
            c = cEnd;
        }
    }

    if (activeBoxes.empty()) {
        return;
    }

    auto isInside = [&](float x, float y) noexcept -> bool {
        const int col = static_cast<int>(std::floor((x - kPadX) / kScale));
        const int row = static_cast<int>(std::floor((y - kPadY) / kScale));
        if (col < 0 || col >= 8 || row < 0 || row >= 12) {
            return false;
        }
        return (rows[row] & (0x80u >> col)) != 0u;
    };

    for (std::uint16_t py = 0u; py < cellH; ++py) {
        for (std::uint16_t px = 0u; px < cellW; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            const float sy = static_cast<float>(py) + 0.5f;
            const bool inside = isInside(sx, sy);

            float minBoundaryDist = 1e6f;
            if (!inside) {
                for (const auto& b : activeBoxes) {
                    const float dx = std::max({b.x0 - sx, 0.0f, sx - b.x1});
                    const float dy = std::max({b.y0 - sy, 0.0f, sy - b.y1});
                    minBoundaryDist = std::min(minBoundaryDist, std::sqrt(dx * dx + dy * dy));
                }
                outPixels[static_cast<std::size_t>(py) * cellW + px] =
                    QuantizeSdfByte(-minBoundaryDist, pxRange);
            } else {
                // Inside active pixel: find shortest distance to nearest empty neighbor cell edge
                const int baseCol = static_cast<int>(std::floor((sx - kPadX) / kScale));
                const int baseRow = static_cast<int>(std::floor((sy - kPadY) / kScale));
                for (int nr = baseRow - 2; nr <= baseRow + 2; ++nr) {
                    for (int nc = baseCol - 2; nc <= baseCol + 2; ++nc) {
                        const bool neighborOn = (nc >= 0 && nc < 8 && nr >= 0 && nr < 12) &&
                            ((rows[nr] & (0x80u >> nc)) != 0u);
                        if (!neighborOn) {
                            const float bx0 = kPadX + static_cast<float>(nc) * kScale;
                            const float by0 = kPadY + static_cast<float>(nr) * kScale;
                            const float bx1 = bx0 + kScale;
                            const float by1 = by0 + kScale;
                            const float dx = std::max({bx0 - sx, 0.0f, sx - bx1});
                            const float dy = std::max({by0 - sy, 0.0f, sy - by1});
                            minBoundaryDist = std::min(minBoundaryDist, std::sqrt(dx * dx + dy * dy));
                        }
                    }
                }
                outPixels[static_cast<std::size_t>(py) * cellW + px] =
                    QuantizeSdfByte(minBoundaryDist, pxRange);
            }
        }
    }
}

} // namespace

// ============================================================================
// FontAtlas Implementation
// ============================================================================

FontAtlas& FontAtlas::Instance() noexcept {
    return UIResourceStore::Default().Fonts();
}

namespace {

std::atomic<std::uint64_t> gNextFontAtlasInstanceId{1u};

} // namespace

FontAtlas::FontAtlas(std::uint16_t atlasSize)
    : atlasWidth_(std::max<std::uint16_t>(atlasSize, 512u))
    , atlasHeight_(std::max<std::uint16_t>(atlasSize, 512u))
    , glyphRegionHeight_(static_cast<std::uint16_t>(std::max<std::uint16_t>(atlasSize, 512u) / 2u))
    , skyline_(
          static_cast<std::uint32_t>(std::max<std::uint16_t>(atlasSize, 512u)) - 4u,
          static_cast<std::uint32_t>(std::max<std::uint16_t>(atlasSize, 512u) / 2u) - 4u,
          2u,
          2u)
    , sdfPixels_()
    , instanceId_(gNextFontAtlasInstanceId.fetch_add(1u, std::memory_order_relaxed)) {
    // Lazy init: defer 1MB sdfPixels_ allocation and 95-glyph ASCII SDF baking until first glyph use.
}

std::string FontAtlas::NormalizeFamilyKey(std::string_view rawFamily) {
    // Support CSS/WPF fallback chains ("./#PT Sans, ./#FontopoNIHONGO") by taking first valid entry
    while (!rawFamily.empty() && (rawFamily.front() == ' ' || rawFamily.front() == '\t' || rawFamily.front() == ',')) {
        rawFamily.remove_prefix(1u);
    }
    if (const auto commaPos = rawFamily.find(','); commaPos != std::string_view::npos) {
        rawFamily = rawFamily.substr(0u, commaPos);
    }
    while (!rawFamily.empty() && (rawFamily.back() == ' ' || rawFamily.back() == '\t')) {
        rawFamily.remove_suffix(1u);
    }
    // Strip leading path components & NoesisGUI/WPF "./#FamilyName" URI prefix
    if (const auto hashPos = rawFamily.rfind('#'); hashPos != std::string_view::npos) {
        rawFamily = rawFamily.substr(hashPos + 1u);
    } else {
        if (const auto slashPos = rawFamily.find_last_of("/\\"); slashPos != std::string_view::npos) {
            rawFamily = rawFamily.substr(slashPos + 1u);
        }
        if (rawFamily.size() > 4u) {
            const std::string_view ext = rawFamily.substr(rawFamily.size() - 4u);
            if ((ext[0] == '.') &&
                (std::tolower(static_cast<unsigned char>(ext[1])) == 't' ||
                 std::tolower(static_cast<unsigned char>(ext[1])) == 'o') &&
                std::tolower(static_cast<unsigned char>(ext[2])) == 't' &&
                std::tolower(static_cast<unsigned char>(ext[3])) == 'f') {
                rawFamily = rawFamily.substr(0, rawFamily.size() - 4u);
            }
        }
    }

    std::string key;
    key.reserve(rawFamily.size());
    for (char ch : rawFamily) {
        const auto uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc)) {
            key.push_back(static_cast<char>(std::tolower(uc)));
        }
    }
    return key;
}

bool FontAtlas::AllocateSkylineRectLocked(
    std::uint16_t cellW,
    std::uint16_t cellH,
    std::uint16_t& outX,
    std::uint16_t& outY) const noexcept {
    // Add 1-texel right/bottom safety gutter inside the [2, atlasWidth_-2) x [2, glyphRegionHeight_-2) packer bounds.
    const auto paddedW = static_cast<std::uint32_t>(cellW) + 1u;
    const auto paddedH = static_cast<std::uint32_t>(cellH) + 1u;
    std::uint32_t placedX = 0u;
    std::uint32_t placedY = 0u;
    if (!skyline_.TryAllocate(paddedW, paddedH, placedX, placedY)) {
        return false;
    }
    outX = static_cast<std::uint16_t>(placedX);
    outY = static_cast<std::uint16_t>(placedY);
    return true;
}

void FontAtlas::BlitCellR8Locked(
    std::uint16_t dstX,
    std::uint16_t dstY,
    std::uint16_t cellW,
    std::uint16_t cellH,
    const std::uint8_t* srcPixels) const {
    if (!srcPixels) {
        return;
    }
    if (sdfPixels_.empty()) {
        sdfPixels_.assign(static_cast<std::size_t>(atlasWidth_) * glyphRegionHeight_, 0u);
    }
    const std::uint32_t maxRow = std::min<std::uint32_t>(
        glyphRegionHeight_,
        static_cast<std::uint32_t>(dstY) + static_cast<std::uint32_t>(cellH));
    for (std::uint32_t absY = dstY; absY < maxRow; ++absY) {
        const std::uint32_t r = absY - dstY;
        const std::size_t copyW = std::min<std::size_t>(
            cellW,
            atlasWidth_ - std::min<std::uint16_t>(dstX, atlasWidth_));
        std::memcpy(
            sdfPixels_.data() + static_cast<std::size_t>(absY) * atlasWidth_ + dstX,
            srcPixels + static_cast<std::size_t>(r) * cellW,
            copyW);
    }
    if (maxRow > dstY) {
        if (!hasDirtyRowSpan_) {
            dirtyBaseRevision_ = revision_.load(std::memory_order_relaxed);
            dirtyRowMin_ = dstY;
            dirtyRowMax_ = maxRow;
            hasDirtyRowSpan_ = true;
        } else {
            dirtyRowMin_ = std::min<std::uint32_t>(dirtyRowMin_, dstY);
            dirtyRowMax_ = std::max<std::uint32_t>(dirtyRowMax_, maxRow);
        }
    }
    revision_.fetch_add(1u, std::memory_order_release);
}

void FontAtlas::EnsureBuiltInFallbackAscii() const {
    if (builtInAsciiReady_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureBuiltInFallbackAsciiLocked();
}

void FontAtlas::EnsureBuiltInFallbackAsciiLocked() const {
    if (builtInAsciiReady_.load(std::memory_order_relaxed)) {
        return;
    }
    InitBuiltInFallbackAscii();
    builtInAsciiReady_.store(true, std::memory_order_release);
}

void FontAtlas::InitBuiltInFallbackAscii() const {
    constexpr std::uint16_t kCellW = 20u;
    constexpr std::uint16_t kCellH = 28u;
    constexpr float kFallbackRange = 3.0f;
    const float invW = 1.0f / static_cast<float>(atlasWidth_);
    const float invH = 1.0f / static_cast<float>(atlasHeight_);

    std::vector<std::uint8_t> cellPixels;
    for (std::uint32_t cp = 0u; cp < 128u; ++cp) {
        FontAtlasGlyph g{};
        g.codePoint = cp;
        g.glyphIndex = cp;
        g.advanceEm = 0.6f;
        g.bearingXEm = 0.0f;
        g.bearingYEm = 0.85f;
        g.quadWidthEm = 0.6f;
        g.quadHeightEm = 1.15f;
        g.pxRange = kFallbackRange;
        g.isFallback = true;
        g.valid = true;

        if (cp <= 0x20u || cp == 0x7Fu) {
            g.isWhitespace = true;
            // Assign non-zero top-half UV slot so LookupPreCachedGlyph(' ') remains valid
            g.u0 = 2.5f * invW;
            g.v0 = 2.5f * invH;
            g.u1 = 6.5f * invW;
            g.v1 = 6.5f * invH;
            builtInAscii_[cp] = g;
            continue;
        }

        BakeBitmap8x12ToSdfCell(kBuiltInAscii8x12[cp - 0x20u], kCellW, kCellH, kFallbackRange, cellPixels);
        std::uint16_t ax = 0u, ay = 0u;
        if (AllocateSkylineRectLocked(kCellW, kCellH, ax, ay)) {
            BlitCellR8Locked(ax, ay, kCellW, kCellH, cellPixels.data());
            g.atlasX = ax;
            g.atlasY = ay;
            g.cellWidth = kCellW;
            g.cellHeight = kCellH;
            g.u0 = (static_cast<float>(ax) + 0.5f) * invW;
            g.v0 = (static_cast<float>(ay) + 0.5f) * invH;
            g.u1 = (static_cast<float>(ax + kCellW) - 0.5f) * invW;
            g.v1 = (static_cast<float>(ay + kCellH) - 0.5f) * invH;
        }
        builtInAscii_[cp] = g;
    }

    // Bake a clean geometric CJK ideograph placeholder glyph (framed cross-window) for unsharded non-ASCII fallback
    static constexpr std::uint8_t kCjkPlaceholderRows[12] = {
        0x00, 0x7E, 0x42, 0x5A, 0x5A, 0x7E, 0x5A, 0x5A, 0x42, 0x7E, 0x00, 0x00};
    BakeBitmap8x12ToSdfCell(kCjkPlaceholderRows, 24u, 28u, kFallbackRange, cellPixels);
    std::uint16_t cx = 0u, cy = 0u;
    builtInCjkBoxGlyph_.codePoint = 0xFFFDu;
    builtInCjkBoxGlyph_.advanceEm = 0.95f;
    builtInCjkBoxGlyph_.bearingXEm = 0.02f;
    builtInCjkBoxGlyph_.bearingYEm = 0.85f;
    builtInCjkBoxGlyph_.quadWidthEm = 0.90f;
    builtInCjkBoxGlyph_.quadHeightEm = 1.15f;
    builtInCjkBoxGlyph_.pxRange = kFallbackRange;
    builtInCjkBoxGlyph_.isFallback = true;
    builtInCjkBoxGlyph_.valid = true;
    if (AllocateSkylineRectLocked(24u, 28u, cx, cy)) {
        BlitCellR8Locked(cx, cy, 24u, 28u, cellPixels.data());
        builtInCjkBoxGlyph_.atlasX = cx;
        builtInCjkBoxGlyph_.atlasY = cy;
        builtInCjkBoxGlyph_.cellWidth = 24u;
        builtInCjkBoxGlyph_.cellHeight = 28u;
        builtInCjkBoxGlyph_.u0 = (static_cast<float>(cx) + 0.5f) * invW;
        builtInCjkBoxGlyph_.v0 = (static_cast<float>(cy) + 0.5f) * invH;
        builtInCjkBoxGlyph_.u1 = (static_cast<float>(cx + 24u) - 0.5f) * invW;
        builtInCjkBoxGlyph_.v1 = (static_cast<float>(cy + 28u) - 0.5f) * invH;
    }
}

void FontAtlas::ClearCustomFonts() {
    std::lock_guard<std::mutex> lock(mutex_);
    fonts_.clear();
    familyAliasToIndex_.clear();
    familyAliasToEntries_.clear();
    glyphCache_.clear();
    defaultFamilyKey_.clear();
    nextFontId_ = 1u;
    skyline_.Clear();
    std::fill(sdfPixels_.begin(), sdfPixels_.end(), 0u);
    hasDirtyRowSpan_ = true;
    dirtyRowMin_ = 0u;
    dirtyRowMax_ = glyphRegionHeight_;
    dirtyBaseRevision_ = 0u;
    revision_.fetch_add(1u, std::memory_order_release);
    InitBuiltInFallbackAscii();
    builtInAsciiReady_.store(true, std::memory_order_release);
}

std::size_t FontAtlas::EstimatedMemoryBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = sdfPixels_.capacity();
    total += fonts_.capacity() * sizeof(RegisteredFontEntry);
    for (const auto& f : fonts_) {
        if (f.face) {
            total += f.face->RawByteSize();
        }
    }
    total += glyphCache_.size() * (sizeof(std::uint64_t) + sizeof(FontAtlasGlyph) + 16u);
    return total;
}

void FontAtlas::BakeOutlineToSdfCell(
    const TtfGlyphOutline& outline,
    std::uint16_t unitsPerEm,
    float bakeEmPixels,
    float pxRange,
    FontAtlasGlyph& outMeta,
    std::vector<std::uint8_t>& outCellPixels) {
    const float safeUpem = static_cast<float>(std::max<std::uint16_t>(unitsPerEm, 16u));
    const float safeBakeEm = std::clamp(bakeEmPixels, 12.0f, 96.0f);
    const float safeRange = std::max(pxRange, 1.0f);
    const float invUpem = 1.0f / safeUpem;

    outMeta.glyphIndex = outline.glyphIndex;
    outMeta.advanceEm = outline.advanceWidth * invUpem;
    outMeta.pxRange = safeRange;
    outMeta.isFallback = false;
    outMeta.valid = true;

    if (outline.isEmptyOutline || outline.segments.empty()) {
        outMeta.isWhitespace = true;
        outMeta.cellWidth = 0u;
        outMeta.cellHeight = 0u;
        outMeta.bearingXEm = outline.leftSideBearing * invUpem;
        outMeta.bearingYEm = 0.0f;
        outMeta.quadWidthEm = 0.0f;
        outMeta.quadHeightEm = 0.0f;
        outCellPixels.clear();
        return;
    }

    outMeta.isWhitespace = false;
    const float padTexels = std::ceil(safeRange) + 1.0f;
    float scale = safeBakeEm * invUpem;

    const float rawOutlineW = std::max(1.0f, (outline.xMax - outline.xMin) * scale);
    const float rawOutlineH = std::max(1.0f, (outline.yMax - outline.yMin) * scale);
    const float maxAllowedSpan = 120.0f - 2.0f * padTexels;
    if (rawOutlineW > maxAllowedSpan || rawOutlineH > maxAllowedSpan) {
        scale *= maxAllowedSpan / std::max(rawOutlineW, rawOutlineH);
    }

    const auto cellW = static_cast<std::uint16_t>(
        std::clamp(static_cast<int>(std::ceil((outline.xMax - outline.xMin) * scale + 2.0f * padTexels)), 6, 124));
    const auto cellH = static_cast<std::uint16_t>(
        std::clamp(static_cast<int>(std::ceil((outline.yMax - outline.yMin) * scale + 2.0f * padTexels)), 6, 124));

    outMeta.cellWidth = cellW;
    outMeta.cellHeight = cellH;
    // Exact sub-texel em mapping:
    // cellX = (fx - outline.xMin) * scale + padTexels
    // cellY = (outline.yMax - fy) * scale + padTexels
    const float x0FUnit = outline.xMin - (padTexels / scale);
    const float y1FUnit = outline.yMax + (padTexels / scale);
    outMeta.bearingXEm = x0FUnit * invUpem;
    outMeta.bearingYEm = y1FUnit * invUpem;
    outMeta.quadWidthEm = (static_cast<float>(cellW) / scale) * invUpem;
    outMeta.quadHeightEm = (static_cast<float>(cellH) / scale) * invUpem;

    std::vector<CellDistCurve> distCurves;
    distCurves.reserve(outline.segments.size());
    std::vector<YMonotonicSeg> windSegs;
    windSegs.reserve(outline.segments.size() * 2u);

    auto mapX = [&](float fx) noexcept -> float {
        return (fx - outline.xMin) * scale + padTexels;
    };
    auto mapY = [&](float fy) noexcept -> float {
        return (outline.yMax - fy) * scale + padTexels;
    };

    for (const auto& seg : outline.segments) {
        const float x0 = mapX(seg.p0x);
        const float y0 = mapY(seg.p0y);
        const float x1 = mapX(seg.p1x);
        const float y1 = mapY(seg.p1y);
        const float x2 = mapX(seg.p2x);
        const float y2 = mapY(seg.p2y);

        if (!seg.isQuadratic) {
            const float dx = x2 - x0;
            const float dy = y2 - y0;
            if (dx * dx + dy * dy <= 1e-8f) {
                continue;
            }
            distCurves.push_back(CellDistCurve{
                x0, y0, x1, y1, x2, y2,
                std::min(x0, x2), std::min(y0, y2),
                std::max(x0, x2), std::max(y0, y2),
                false});
            if (std::fabs(dy) > 1e-6f) {
                windSegs.push_back(YMonotonicSeg{
                    x0, y0, x1, y1, x2, y2,
                    std::min(y0, y2), std::max(y0, y2),
                    std::max(x0, x2),
                    false});
            }
        } else {
            distCurves.push_back(CellDistCurve{
                x0, y0, x1, y1, x2, y2,
                std::min({x0, x1, x2}), std::min({y0, y1, y2}),
                std::max({x0, x1, x2}), std::max({y0, y1, y2}),
                true});
            AppendYMonotonicQuad(windSegs, x0, y0, x1, y1, x2, y2);
        }
    }

    outCellPixels.assign(static_cast<std::size_t>(cellW) * cellH, 0u);
    if (distCurves.empty()) {
        return;
    }

    const float maxCutoffDist = safeRange + 1.5f;
    const float maxCutoffSq = maxCutoffDist * maxCutoffDist;

    struct RowWindingCrossing {
        float xCross = 0.0f;
        int delta = 0;
    };
    std::vector<YMonotonicSeg> rowWindSegs;
    rowWindSegs.reserve(windSegs.size());
    std::vector<RowWindingCrossing> rowCrossings;
    rowCrossings.reserve(windSegs.size());
    std::vector<int> rowSuffixWinding;
    rowSuffixWinding.reserve(windSegs.size() + 1u);
    std::vector<const CellDistCurve*> rowDistCurves;
    rowDistCurves.reserve(distCurves.size());

    for (std::uint16_t py = 0u; py < cellH; ++py) {
        const float sy = static_cast<float>(py) + 0.5f;
        rowWindSegs.clear();
        for (const auto& ws : windSegs) {
            if (sy >= ws.minY && sy <= ws.maxY) {
                rowWindSegs.push_back(ws);
            }
        }
        rowDistCurves.clear();
        for (const auto& c : distCurves) {
            const float ady = std::max({c.minY - sy, 0.0f, sy - c.maxY});
            if (ady * ady < maxCutoffSq) {
                rowDistCurves.push_back(&c);
            }
        }
        if (rowWindSegs.empty() && rowDistCurves.empty()) {
            // Entire row is outside and beyond maxCutoffDist -> QuantizeSdfByte(-maxCutoffDist, safeRange) == 0u
            continue;
        }
        rowCrossings.clear();
        for (const auto& s : rowWindSegs) {
            if (sy < s.minY || sy >= s.maxY) {
                continue;
            }
            const bool upward = (s.y0 <= sy && s.y2 > sy);
            const bool downward = (s.y0 > sy && s.y2 <= sy);
            if (!upward && !downward) {
                continue;
            }
            float xCross = 0.0f;
            if (!s.isQuadratic) {
                const float t = (sy - s.y0) / (s.y2 - s.y0);
                xCross = s.x0 + t * (s.x2 - s.x0);
            } else {
                const float a = s.y0 - 2.0f * s.y1 + s.y2;
                const float b = s.y1 - s.y0;
                const float c = s.y0 - sy;
                float t = 0.0f;
                if (std::fabs(a) <= 1e-5f) {
                    const float dy = s.y2 - s.y0;
                    t = (std::fabs(dy) > 1e-8f) ? (-c / dy) : 0.0f;
                } else {
                    const float disc = std::max(0.0f, b * b - a * c);
                    const float root = std::sqrt(disc);
                    if (upward) {
                        const float denom = b + root;
                        t = (std::fabs(denom) > 1e-8f) ? (-c / denom) : ((-b + root) / a);
                    } else {
                        const float denom = -b + root;
                        t = (std::fabs(denom) > 1e-8f) ? (c / denom) : ((-b - root) / a);
                    }
                }
                t = std::clamp(t, 0.0f, 1.0f);
                const float u = 1.0f - t;
                xCross = u * u * s.x0 + 2.0f * u * t * s.x1 + t * t * s.x2;
            }
            rowCrossings.push_back(RowWindingCrossing{xCross, upward ? 1 : -1});
        }
        std::sort(
            rowCrossings.begin(),
            rowCrossings.end(),
            [](const RowWindingCrossing& lhs, const RowWindingCrossing& rhs) noexcept {
                return lhs.xCross < rhs.xCross;
            });
        const std::size_t crossCount = rowCrossings.size();
        rowSuffixWinding.resize(crossCount + 1u);
        rowSuffixWinding[crossCount] = 0;
        for (std::size_t ci = crossCount; ci > 0u; --ci) {
            rowSuffixWinding[ci - 1u] = rowSuffixWinding[ci] + rowCrossings[ci - 1u].delta;
        }
        std::size_t crossCursor = 0u;

        for (std::uint16_t px = 0u; px < cellW; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            while (crossCursor < crossCount && rowCrossings[crossCursor].xCross <= sx) {
                ++crossCursor;
            }
            const bool inside = (rowSuffixWinding[crossCursor] != 0);

            float bestDistSq = maxCutoffSq;
            for (const CellDistCurve* cp : rowDistCurves) {
                const auto& c = *cp;
                const float adx = std::max({c.minX - sx, 0.0f, sx - c.maxX});
                if (adx * adx >= bestDistSq) {
                    continue;
                }
                const float ady = std::max({c.minY - sy, 0.0f, sy - c.maxY});
                if (adx * adx + ady * ady >= bestDistSq) {
                    continue;
                }
                const float dSq = c.isQuadratic
                    ? DistSqPointToQuadBezier(sx, sy, c.x0, c.y0, c.x1, c.y1, c.x2, c.y2)
                    : DistSqPointToLine(sx, sy, c.x0, c.y0, c.x2, c.y2);
                bestDistSq = std::min(bestDistSq, dSq);
            }

            const float signedDist = (inside ? 1.0f : -1.0f) * std::sqrt(bestDistSq);
            outCellPixels[static_cast<std::size_t>(py) * cellW + px] =
                QuantizeSdfByte(signedDist, safeRange);
        }
    }
}

namespace {

struct ProcessFontFaceCache {
    std::mutex mtx;
    std::unordered_map<std::uint64_t, std::shared_ptr<TrueTypeFontFace>> faces;
    std::size_t totalBytes = 0u;

    static ProcessFontFaceCache& Instance() noexcept {
        static ProcessFontFaceCache inst;
        return inst;
    }
};

struct ProcessBakedGlyphEntry {
    FontAtlasGlyph meta{};
    std::vector<std::uint8_t> pixels;
};

struct ProcessBakedGlyphCache {
    std::mutex mtx;
    std::unordered_map<std::uint64_t, ProcessBakedGlyphEntry> entries;
    std::size_t totalBytes = 0u;

    static ProcessBakedGlyphCache& Instance() noexcept {
        static ProcessBakedGlyphCache inst;
        return inst;
    }
};

[[nodiscard]] std::uint64_t ComputeFontBlobKey(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size == 0u) return 0u;
    std::uint64_t h = 14695981039346656037ULL;
    const std::size_t stride = (size > 65536u) ? (size / 16384u) : 1u;
    for (std::size_t i = 0u; i < size; i += stride) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= 1099511628211ULL;
    }
    h ^= static_cast<std::uint64_t>(size);
    h *= 1099511628211ULL;
    return (h == 0u) ? 1u : h;
}

} // namespace

bool FontAtlas::LoadTrueTypeFontFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    std::string_view familyAlias,
    bool setAsDefault,
    std::string* outError) {
    return LoadTrueTypeFontFromMemoryImpl(data, size, nullptr, familyAlias, setAsDefault, outError);
}

bool FontAtlas::LoadTrueTypeFontFromMemoryImpl(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<std::uint8_t>* optOwnedBytes,
    std::string_view familyAlias,
    bool setAsDefault,
    std::string* outError) {
    const std::uint64_t blobKey = ComputeFontBlobKey(data, size);
    std::shared_ptr<TrueTypeFontFace> face;
    if (blobKey != 0u) {
        auto& fc = ProcessFontFaceCache::Instance();
        std::lock_guard<std::mutex> g(fc.mtx);
        auto it = fc.faces.find(blobKey);
        if (it != fc.faces.end()) {
            face = it->second;
        }
    }
    if (!face) {
        auto parsed = std::make_shared<TrueTypeFontFace>();
        const bool ok = (optOwnedBytes != nullptr)
            ? parsed->LoadFromMemory(std::move(*optOwnedBytes), outError)
            : parsed->LoadFromMemory(data, size, outError);
        if (!ok) {
            return false;
        }
        face = std::move(parsed);
        if (blobKey != 0u) {
            auto& fc = ProcessFontFaceCache::Instance();
            std::lock_guard<std::mutex> g(fc.mtx);
            const std::size_t faceBytes = face->RawByteSize();
            if (fc.faces.size() >= 16u || fc.totalBytes + faceBytes > 8u * 1024u * 1024u) {
                fc.faces.clear();
                fc.totalBytes = 0u;
            }
            if (faceBytes <= 8u * 1024u * 1024u) {
                fc.totalBytes += faceBytes;
                fc.faces.emplace(blobKey, face);
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string primaryKey = NormalizeFamilyKey(familyAlias);
    if (primaryKey.empty()) {
        primaryKey = NormalizeFamilyKey(face->FamilyName());
    }
    if (primaryKey.empty()) {
        primaryKey = "defaultfont";
    }

    const int faceWeight = face->WeightClass();
    const bool faceItalic = face->IsItalic();

    std::size_t entryIndex = fonts_.size();
    auto existingIt = familyAliasToIndex_.find(primaryKey);
    if (existingIt != familyAliasToIndex_.end() &&
        existingIt->second < fonts_.size() &&
        (fonts_[existingIt->second].isShardOnly ||
         (fonts_[existingIt->second].weightClass == faceWeight &&
          fonts_[existingIt->second].isItalic == faceItalic))) {
        entryIndex = existingIt->second;
        fonts_[entryIndex].face = face;
        fonts_[entryIndex].weightClass = faceWeight;
        fonts_[entryIndex].isItalic = faceItalic;
        fonts_[entryIndex].isShardOnly = false;
    } else {
        RegisteredFontEntry entry{};
        entry.fontId = nextFontId_++;
        entry.canonicalKey = primaryKey;
        entry.face = face;
        entry.weightClass = faceWeight;
        entry.isItalic = faceItalic;
        fonts_.push_back(std::move(entry));
        if (familyAliasToIndex_.find(primaryKey) == familyAliasToIndex_.end()) {
            familyAliasToIndex_[primaryKey] = entryIndex;
        }
    }

    auto scoreDefaultPreference = [](const RegisteredFontEntry& e) noexcept -> int {
        int s = e.isShardOnly ? 10000 : 0;
        if (e.isItalic) {
            s += 2000;
        }
        s += std::abs(e.weightClass - 400);
        return s;
    };

    auto addAliasKey = [&](const std::string& k) {
        if (k.empty()) {
            return;
        }
        auto& list = familyAliasToEntries_[k];
        if (std::find(list.begin(), list.end(), entryIndex) == list.end()) {
            list.push_back(entryIndex);
        }
        const auto idxIt = familyAliasToIndex_.find(k);
        if (idxIt == familyAliasToIndex_.end() ||
            idxIt->second >= fonts_.size() ||
            scoreDefaultPreference(fonts_[entryIndex]) < scoreDefaultPreference(fonts_[idxIt->second])) {
            familyAliasToIndex_[k] = entryIndex;
        }
    };

    auto registerAlias = [&](std::string_view raw) {
        const std::string k = NormalizeFamilyKey(raw);
        if (k.empty()) {
            return;
        }
        addAliasKey(k);
        static constexpr std::string_view kStyleSuffixes[] = {
            "bolditalic", "boldoblique", "semibold", "demibold", "extrabold",
            "ultrabold", "extralight", "ultralight", "regular", "italic",
            "oblique", "bold", "light", "medium", "thin", "black", "heavy"
        };
        std::string stripped = k;
        for (int pass = 0; pass < 3; ++pass) {
            bool trimmed = false;
            for (std::string_view suf : kStyleSuffixes) {
                if (stripped.size() > suf.size() + 2u &&
                    stripped.compare(stripped.size() - suf.size(), suf.size(), suf) == 0) {
                    stripped.resize(stripped.size() - suf.size());
                    trimmed = true;
                    break;
                }
            }
            if (!trimmed) {
                break;
            }
            addAliasKey(stripped);
        }
    };

    registerAlias(familyAlias);
    registerAlias(face->FamilyName());
    registerAlias(face->FullName());

    if (setAsDefault) {
        defaultFamilyKey_ = primaryKey;
    }

    return true;
}

bool FontAtlas::LoadTrueTypeFontFromFile(
    std::string_view filePath,
    std::string_view familyAlias,
    bool setAsDefault,
    std::string* outError) {
    std::ifstream ifs(std::string(filePath), std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        if (outError) {
            *outError = "Failed to open TTF file: " + std::string(filePath);
        }
        return false;
    }
    const std::streampos endPos = ifs.tellg();
    if (endPos <= 0) {
        if (outError) {
            *outError = "Empty TTF file: " + std::string(filePath);
        }
        return false;
    }
    const auto sz = static_cast<std::size_t>(endPos);
    std::vector<std::uint8_t> buf(sz);
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz))) {
        if (outError) {
            *outError = "Failed to read TTF file: " + std::string(filePath);
        }
        return false;
    }

    std::string derivedAlias(familyAlias);
    if (derivedAlias.empty()) {
        std::string_view stem = filePath;
        if (const auto sl = stem.find_last_of("/\\"); sl != std::string_view::npos) {
            stem = stem.substr(sl + 1u);
        }
        if (const auto dot = stem.rfind('.'); dot != std::string_view::npos) {
            stem = stem.substr(0, dot);
        }
        derivedAlias.assign(stem);
    }
    const auto bufSize = buf.size();
    return LoadTrueTypeFontFromMemoryImpl(buf.data(), bufSize, &buf, derivedAlias, setAsDefault, outError);
}

void FontAtlas::SetDefaultFontFamily(std::string_view familyName) {
    std::lock_guard<std::mutex> lock(mutex_);
    defaultFamilyKey_ = NormalizeFamilyKey(familyName);
}

std::string FontAtlas::DefaultFontFamily() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return defaultFamilyKey_;
}

const FontAtlas::RegisteredFontEntry* FontAtlas::ResolveBestFaceForKeyLocked(
    const std::string& key,
    int weight,
    bool italic) const noexcept {
    if (key.empty()) {
        return nullptr;
    }
    const int targetWeight = (weight > 0) ? weight : 400;
    const auto mapIt = familyAliasToEntries_.find(key);
    if (mapIt != familyAliasToEntries_.end() && !mapIt->second.empty()) {
        const RegisteredFontEntry* best = nullptr;
        int bestScore = 0x7FFFFFFF;
        for (std::size_t idx : mapIt->second) {
            if (idx >= fonts_.size()) {
                continue;
            }
            const RegisteredFontEntry& candidate = fonts_[idx];
            int score = std::abs(candidate.weightClass - targetWeight);
            if (candidate.isItalic != italic) {
                score += candidate.isItalic ? 2000 : 1000;
            }
            if (candidate.isShardOnly && !candidate.face) {
                score += 5000;
            }
            if (!best || score < bestScore) {
                best = &candidate;
                bestScore = score;
            }
        }
        if (best) {
            return best;
        }
    }
    const auto idxIt = familyAliasToIndex_.find(key);
    if (idxIt != familyAliasToIndex_.end() && idxIt->second < fonts_.size()) {
        return &fonts_[idxIt->second];
    }
    return nullptr;
}

void FontAtlas::ResolveFontChainLocked(
    std::string_view fontFamily,
    int weight,
    bool italic,
    std::vector<const RegisteredFontEntry*>& outChain) const {
    outChain.clear();
    std::size_t pos = 0u;
    while (pos < fontFamily.size()) {
        const std::size_t comma = fontFamily.find(',', pos);
        const std::size_t end = (comma == std::string_view::npos) ? fontFamily.size() : comma;
        std::string_view seg = fontFamily.substr(pos, end - pos);
        while (!seg.empty() && (seg.front() == ' ' || seg.front() == '\t')) {
            seg.remove_prefix(1u);
        }
        while (!seg.empty() && (seg.back() == ' ' || seg.back() == '\t')) {
            seg.remove_suffix(1u);
        }
        if (!seg.empty()) {
            const std::string key = NormalizeFamilyKey(seg);
            if (const RegisteredFontEntry* entry = ResolveBestFaceForKeyLocked(key, weight, italic)) {
                if (std::find(outChain.begin(), outChain.end(), entry) == outChain.end()) {
                    outChain.push_back(entry);
                }
            }
        }
        pos = (comma == std::string_view::npos) ? fontFamily.size() : (comma + 1u);
    }
    if (outChain.empty() && !defaultFamilyKey_.empty()) {
        if (const RegisteredFontEntry* defEntry = ResolveBestFaceForKeyLocked(defaultFamilyKey_, weight, italic)) {
            outChain.push_back(defEntry);
        }
    }
}

const FontAtlas::RegisteredFontEntry* FontAtlas::ResolveFontEntryLocked(
    std::string_view fontFamily) const noexcept {
    if (!fontFamily.empty()) {
        std::size_t pos = 0u;
        while (pos < fontFamily.size()) {
            const std::size_t comma = fontFamily.find(',', pos);
            const std::size_t end = (comma == std::string_view::npos) ? fontFamily.size() : comma;
            std::string_view seg = fontFamily.substr(pos, end - pos);
            const std::string key = NormalizeFamilyKey(seg);
            if (!key.empty()) {
                if (const RegisteredFontEntry* entry = ResolveBestFaceForKeyLocked(key, 400, false)) {
                    return entry;
                }
            }
            pos = (comma == std::string_view::npos) ? fontFamily.size() : (comma + 1u);
        }
    }
    if (!defaultFamilyKey_.empty()) {
        return ResolveBestFaceForKeyLocked(defaultFamilyKey_, 400, false);
    }
    return nullptr;
}

bool FontAtlas::HasFontFamily(std::string_view familyName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t pos = 0u;
    while (pos < familyName.size()) {
        const std::size_t comma = familyName.find(',', pos);
        const std::size_t end = (comma == std::string_view::npos) ? familyName.size() : comma;
        const std::string key = NormalizeFamilyKey(familyName.substr(pos, end - pos));
        if (!key.empty() &&
            (familyAliasToIndex_.find(key) != familyAliasToIndex_.end() ||
             familyAliasToEntries_.find(key) != familyAliasToEntries_.end())) {
            return true;
        }
        pos = (comma == std::string_view::npos) ? familyName.size() : (comma + 1u);
    }
    return false;
}

const TrueTypeFontFace* FontAtlas::FindFontFace(std::string_view familyName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const RegisteredFontEntry* entry = ResolveFontEntryLocked(familyName);
    return (entry && entry->face) ? entry->face.get() : nullptr;
}

FontAtlasGlyph FontAtlas::EnsureGlyphFromChainLocked(
    const std::vector<const RegisteredFontEntry*>& chain,
    std::uint32_t codePoint,
    const RegisteredFontEntry** outChosenEntry) {
    if (chain.empty()) {
        if (outChosenEntry) {
            *outChosenEntry = nullptr;
        }
        return EnsureGlyphLocked(nullptr, codePoint);
    }
    if (codePoint == ' ' || codePoint == '\t') {
        if (outChosenEntry) {
            *outChosenEntry = chain.front();
        }
        return EnsureGlyphLocked(chain.front(), codePoint);
    }
    for (const RegisteredFontEntry* entry : chain) {
        if (!entry) {
            continue;
        }
        const std::uint64_t cacheKey = (static_cast<std::uint64_t>(entry->fontId) << 32u) | codePoint;
        if (const auto it = glyphCache_.find(cacheKey); it != glyphCache_.end()) {
            if (outChosenEntry) {
                *outChosenEntry = entry;
            }
            return it->second;
        }
        if (entry->face && entry->face->IsValid()) {
            if (entry->face->FindGlyphIndex(codePoint) != 0u) {
                if (outChosenEntry) {
                    *outChosenEntry = entry;
                }
                return EnsureGlyphLocked(entry, codePoint);
            }
        }
    }
    if (outChosenEntry) {
        *outChosenEntry = chain.front();
    }
    return EnsureGlyphLocked(chain.front(), codePoint);
}

FontAtlasGlyph FontAtlas::EnsureGlyphByIndexLocked(
    const RegisteredFontEntry& fontEntry,
    std::uint32_t gid,
    std::uint32_t sourceCodePoint) {
    if (!fontEntry.face || !fontEntry.face->IsValid() || gid == 0u) {
        return EnsureGlyphLocked(&fontEntry, sourceCodePoint);
    }
    const std::uint64_t cacheKey =
        (static_cast<std::uint64_t>(fontEntry.fontId) << 32u) | 0x80000000ULL | static_cast<std::uint64_t>(gid);
    if (const auto it = glyphCache_.find(cacheKey); it != glyphCache_.end()) {
        return it->second;
    }
    const std::uint64_t faceHash = fontEntry.face->ContentHash();
    const std::uint64_t procGlyphKey = (faceHash != 0u)
        ? ((faceHash ^ (static_cast<std::uint64_t>(gid) * 0x9E3779B97F4A7C15ULL)) ^ 0xA5A55A5AULL)
        : 0u;
    FontAtlasGlyph meta{};
    std::vector<std::uint8_t> cellPixels;
    bool bakedOk = false;
    if (procGlyphKey != 0u) {
        auto& pgc = ProcessBakedGlyphCache::Instance();
        std::lock_guard<std::mutex> g(pgc.mtx);
        const auto hit = pgc.entries.find(procGlyphKey);
        if (hit != pgc.entries.end()) {
            meta = hit->second.meta;
            meta.codePoint = sourceCodePoint;
            cellPixels = hit->second.pixels;
            bakedOk = true;
        }
    }
    if (!bakedOk) {
        TtfGlyphOutline outline{};
        if (fontEntry.face->ExtractGlyphOutline(gid, outline)) {
            meta = FontAtlasGlyph{};
            meta.codePoint = sourceCodePoint;
            BakeOutlineToSdfCell(
                outline,
                fontEntry.face->UnitsPerEm(),
                kDefaultBakeEmPixels,
                kDefaultSdfPxRange,
                meta,
                cellPixels);
            bakedOk = true;
            if (procGlyphKey != 0u) {
                auto& pgc = ProcessBakedGlyphCache::Instance();
                std::lock_guard<std::mutex> g(pgc.mtx);
                if (pgc.entries.size() >= 8192u) pgc.entries.clear();
                pgc.entries.emplace(procGlyphKey, ProcessBakedGlyphEntry{meta, cellPixels});
            }
        }
    }
    if (!bakedOk) {
        return EnsureGlyphLocked(&fontEntry, sourceCodePoint);
    }
    const float invW = 1.0f / static_cast<float>(atlasWidth_);
    const float invH = 1.0f / static_cast<float>(atlasHeight_);
    if (!meta.isWhitespace && meta.cellWidth > 0u && meta.cellHeight > 0u) {
        std::uint16_t ax = 0u, ay = 0u;
        if (AllocateSkylineRectLocked(meta.cellWidth, meta.cellHeight, ax, ay)) {
            BlitCellR8Locked(ax, ay, meta.cellWidth, meta.cellHeight, cellPixels.data());
            meta.atlasX = ax;
            meta.atlasY = ay;
            meta.u0 = (static_cast<float>(ax) + 0.5f) * invW;
            meta.v0 = (static_cast<float>(ay) + 0.5f) * invH;
            meta.u1 = (static_cast<float>(ax + meta.cellWidth) - 0.5f) * invW;
            meta.v1 = (static_cast<float>(ay + meta.cellHeight) - 0.5f) * invH;
        }
    } else {
        meta.u0 = 2.5f * invW;
        meta.v0 = 2.5f * invH;
        meta.u1 = 6.5f * invW;
        meta.v1 = 6.5f * invH;
    }
    glyphCache_[cacheKey] = meta;
    return meta;
}

FontAtlasGlyph FontAtlas::EnsureGlyphLocked(
    const RegisteredFontEntry* fontEntry,
    std::uint32_t codePoint) {
    const std::uint32_t fontId = fontEntry ? fontEntry->fontId : 0u;
    const std::uint64_t cacheKey = (static_cast<std::uint64_t>(fontId) << 32u) | codePoint;

    if (const auto it = glyphCache_.find(cacheKey); it != glyphCache_.end()) {
        return it->second;
    }

    // Also check global unpacked WebFont shard cache (fontId == 0) before falling back to placeholder
    if (fontId != 0u) {
        const std::uint64_t globalShardKey = static_cast<std::uint64_t>(codePoint);
        if (const auto sIt = glyphCache_.find(globalShardKey); sIt != glyphCache_.end()) {
            return sIt->second;
        }
    }

    if (fontEntry && fontEntry->face && fontEntry->face->IsValid()) {
        const std::uint32_t gid = fontEntry->face->FindGlyphIndex(codePoint);
        if (gid != 0u || codePoint == 0x20u || codePoint == 0x09u) {
            const std::uint64_t faceHash = fontEntry->face->ContentHash();
            const std::uint64_t procGlyphKey = (faceHash != 0u)
                ? ((faceHash ^ (static_cast<std::uint64_t>(gid) * 0x9E3779B97F4A7C15ULL)) ^
                   (static_cast<std::uint64_t>(codePoint) << 1u))
                : 0u;
            FontAtlasGlyph meta{};
            std::vector<std::uint8_t> cellPixels;
            bool bakedOk = false;
            if (procGlyphKey != 0u) {
                auto& pgc = ProcessBakedGlyphCache::Instance();
                std::lock_guard<std::mutex> g(pgc.mtx);
                const auto hit = pgc.entries.find(procGlyphKey);
                if (hit != pgc.entries.end()) {
                    meta = hit->second.meta;
                    meta.codePoint = codePoint;
                    cellPixels = hit->second.pixels;
                    bakedOk = true;
                }
            }
            if (!bakedOk) {
                TtfGlyphOutline outline{};
                if (fontEntry->face->ExtractGlyphOutline(gid, outline)) {
                    meta = FontAtlasGlyph{};
                    meta.codePoint = codePoint;
                    BakeOutlineToSdfCell(
                        outline,
                        fontEntry->face->UnitsPerEm(),
                        kDefaultBakeEmPixels,
                        kDefaultSdfPxRange,
                        meta,
                        cellPixels);
                    bakedOk = true;
                    if (procGlyphKey != 0u) {
                        auto& pgc = ProcessBakedGlyphCache::Instance();
                        std::lock_guard<std::mutex> g(pgc.mtx);
                        const std::size_t cellBytes = cellPixels.size();
                        if (pgc.entries.size() >= 2048u || pgc.totalBytes + cellBytes > 2u * 1024u * 1024u) {
                            pgc.entries.clear();
                            pgc.totalBytes = 0u;
                        }
                        if (cellBytes <= 2u * 1024u * 1024u) {
                            pgc.totalBytes += cellBytes;
                            pgc.entries.emplace(procGlyphKey, ProcessBakedGlyphEntry{meta, cellPixels});
                        }
                    }
                }
            }
            if (bakedOk) {

                const float invW = 1.0f / static_cast<float>(atlasWidth_);
                const float invH = 1.0f / static_cast<float>(atlasHeight_);
                if (!meta.isWhitespace && meta.cellWidth > 0u && meta.cellHeight > 0u) {
                    std::uint16_t ax = 0u, ay = 0u;
                    if (AllocateSkylineRectLocked(meta.cellWidth, meta.cellHeight, ax, ay)) {
                        BlitCellR8Locked(ax, ay, meta.cellWidth, meta.cellHeight, cellPixels.data());
                        meta.atlasX = ax;
                        meta.atlasY = ay;
                        meta.u0 = (static_cast<float>(ax) + 0.5f) * invW;
                        meta.v0 = (static_cast<float>(ay) + 0.5f) * invH;
                        meta.u1 = (static_cast<float>(ax + meta.cellWidth) - 0.5f) * invW;
                        meta.v1 = (static_cast<float>(ay + meta.cellHeight) - 0.5f) * invH;
                    } else {
                        // meta.advanceEm already computed by BakeOutlineToSdfCell
                        const float savedAdv = meta.advanceEm;
                        EnsureBuiltInFallbackAsciiLocked();
                        meta = (codePoint < 128u) ? builtInAscii_[codePoint] : builtInCjkBoxGlyph_;
                        meta.codePoint = codePoint;
                        meta.advanceEm = savedAdv;
                        return meta;
                    }
                } else {
                    meta.u0 = 2.5f * invW;
                    meta.v0 = 2.5f * invH;
                    meta.u1 = 6.5f * invW;
                    meta.v1 = 6.5f * invH;
                }
                glyphCache_[cacheKey] = meta;
                return meta;
            }
        }
    }

    // Fallback when no .ttf is loaded or codePoint is unmapped (e.g. unloaded CJK shard):
    // Allocate a unique collision-free Skyline SDF cell per distinct Unicode codePoint!
    if (codePoint < 128u) {
        EnsureBuiltInFallbackAsciiLocked();
        return builtInAscii_[codePoint];
    }
    const std::uint64_t fallbackKey = (static_cast<std::uint64_t>(0xFFFFu) << 32u) | codePoint;
    if (const auto fbIt = glyphCache_.find(fallbackKey); fbIt != glyphCache_.end()) {
        return fbIt->second;
    }

    EnsureBuiltInFallbackAsciiLocked();
    FontAtlasGlyph fallback = builtInCjkBoxGlyph_;
    fallback.codePoint = codePoint;
    fallback.glyphIndex = codePoint;
    fallback.advanceEm = IsWideCjkCodePoint(codePoint) ? 0.95f : 0.6f;
    fallback.quadWidthEm = IsWideCjkCodePoint(codePoint) ? 0.90f : 0.6f;

    std::uint8_t rows[12] = {
        0x00, 0x7E, 0x42,
        static_cast<std::uint8_t>(0x42u | (((codePoint >> 0u) & 0x0Fu) << 1u)),
        static_cast<std::uint8_t>(0x42u | (((codePoint >> 4u) & 0x0Fu) << 1u)),
        0x7E,
        static_cast<std::uint8_t>(0x42u | (((codePoint >> 8u) & 0x0Fu) << 1u)),
        static_cast<std::uint8_t>(0x42u | (((codePoint >> 12u) & 0x0Fu) << 1u)),
        0x42, 0x7E, 0x00, 0x00
    };
    std::vector<std::uint8_t> cellPixels;
    BakeBitmap8x12ToSdfCell(rows, 20u, 24u, 3.0f, cellPixels);
    std::uint16_t fx = 0u, fy = 0u;
    if (AllocateSkylineRectLocked(20u, 24u, fx, fy)) {
        const float invW = 1.0f / static_cast<float>(atlasWidth_);
        const float invH = 1.0f / static_cast<float>(atlasHeight_);
        BlitCellR8Locked(fx, fy, 20u, 24u, cellPixels.data());
        fallback.atlasX = fx;
        fallback.atlasY = fy;
        fallback.cellWidth = 20u;
        fallback.cellHeight = 24u;
        fallback.u0 = (static_cast<float>(fx) + 0.5f) * invW;
        fallback.v0 = (static_cast<float>(fy) + 0.5f) * invH;
        fallback.u1 = (static_cast<float>(fx + 20u) - 0.5f) * invW;
        fallback.v1 = (static_cast<float>(fy + 24u) - 0.5f) * invH;
        glyphCache_[fallbackKey] = fallback;
    }
    return fallback;
}

FontAtlasGlyph FontAtlas::EnsureGlyph(
    std::string_view fontFamily,
    std::uint32_t codePoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    const RegisteredFontEntry* entry = ResolveFontEntryLocked(fontFamily);
    return EnsureGlyphLocked(entry, codePoint);
}

bool FontAtlas::ExportGlyphShard(
    std::string_view fontFamily,
    std::uint32_t rangeStart,
    std::uint32_t rangeEnd,
    std::vector<std::uint8_t>& outShardBytes,
    float bakeEmPixels) const {
    outShardBytes.clear();
    if (rangeEnd < rangeStart) {
        return false;
    }

    std::shared_ptr<TrueTypeFontFace> face;
    std::string canonicalName;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const RegisteredFontEntry* entry = ResolveFontEntryLocked(fontFamily);
        if (!entry || !entry->face || !entry->face->IsValid()) {
            return false;
        }
        face = entry->face;
        canonicalName = !fontFamily.empty() ? std::string(fontFamily) : face->FamilyName();
    }

    struct BakedShardGlyph {
        FontAtlasGlyph meta{};
        std::vector<std::uint8_t> pixels;
    };
    std::vector<BakedShardGlyph> glyphs;

    for (std::uint32_t cp = rangeStart; cp <= rangeEnd; ++cp) {
        const std::uint32_t gid = face->FindGlyphIndex(cp);
        if (gid == 0u && cp != 0x20u) {
            if (cp == 0xFFFFFFFFu) break;
            continue;
        }
        TtfGlyphOutline outline{};
        if (!face->ExtractGlyphOutline(gid, outline)) {
            if (cp == 0xFFFFFFFFu) break;
            continue;
        }
        BakedShardGlyph item{};
        item.meta.codePoint = cp;
        BakeOutlineToSdfCell(
            outline,
            face->UnitsPerEm(),
            bakeEmPixels,
            kDefaultSdfPxRange,
            item.meta,
            item.pixels);
        glyphs.push_back(std::move(item));
        if (cp == 0xFFFFFFFFu) break;
    }

    const auto nameLen = static_cast<std::uint16_t>(std::min<std::size_t>(canonicalName.size(), 1024u));
    outShardBytes.reserve(32u + nameLen + glyphs.size() * 256u);

    WriteShardU32LE(outShardBytes, kAeroFontShardMagic);
    WriteShardU16LE(outShardBytes, kAeroFontShardVersion);
    WriteShardU16LE(outShardBytes, nameLen);
    WriteShardU32LE(outShardBytes, rangeStart);
    WriteShardU32LE(outShardBytes, rangeEnd);
    WriteShardU32LE(outShardBytes, static_cast<std::uint32_t>(glyphs.size()));
    WriteShardF32LE(outShardBytes, face->AscentEm());
    WriteShardF32LE(outShardBytes, face->DescentEm());
    WriteShardF32LE(outShardBytes, face->LineGapEm());

    outShardBytes.insert(
        outShardBytes.end(),
        canonicalName.begin(),
        canonicalName.begin() + nameLen);

    for (const auto& item : glyphs) {
        const auto& m = item.meta;
        const std::uint16_t flags =
            (m.isWhitespace ? 0x0001u : 0x0000u) |
            (!item.pixels.empty() ? 0x0002u : 0x0000u);
        WriteShardU32LE(outShardBytes, m.codePoint);
        WriteShardU16LE(outShardBytes, static_cast<std::uint16_t>(m.glyphIndex & 0xFFFFu));
        WriteShardU16LE(outShardBytes, flags);
        WriteShardU16LE(outShardBytes, m.cellWidth);
        WriteShardU16LE(outShardBytes, m.cellHeight);
        WriteShardF32LE(outShardBytes, m.advanceEm);
        WriteShardF32LE(outShardBytes, m.bearingXEm);
        WriteShardF32LE(outShardBytes, m.bearingYEm);
        WriteShardF32LE(outShardBytes, m.quadWidthEm);
        WriteShardF32LE(outShardBytes, m.quadHeightEm);
        WriteShardF32LE(outShardBytes, m.pxRange);
        if (!item.pixels.empty()) {
            outShardBytes.insert(outShardBytes.end(), item.pixels.begin(), item.pixels.end());
        }
    }
    return true;
}

bool FontAtlas::UnpackGlyphShard(
    const std::uint8_t* shardBytes,
    std::size_t byteSize,
    std::uint32_t* outRangeStart,
    std::uint32_t* outRangeEnd,
    std::size_t* outUnpackedGlyphCount) {
    if (!shardBytes || byteSize < 32u) {
        return false;
    }

    ShardReaderLE r{shardBytes, byteSize, 0u};
    std::uint32_t magic = 0u, rangeStart = 0u, rangeEnd = 0u, glyphCount = 0u;
    std::uint16_t version = 0u, familyLen = 0u;
    float ascentEm = 0.8f, descentEm = 0.2f, lineGapEm = 0.0f;

    if (!r.ReadU32(magic) || magic != kAeroFontShardMagic ||
        !r.ReadU16(version) || version != kAeroFontShardVersion ||
        !r.ReadU16(familyLen) ||
        !r.ReadU32(rangeStart) ||
        !r.ReadU32(rangeEnd) ||
        !r.ReadU32(glyphCount) ||
        !r.ReadF32(ascentEm) ||
        !r.ReadF32(descentEm) ||
        !r.ReadF32(lineGapEm)) {
        return false;
    }
    if (rangeEnd < rangeStart || familyLen > (r.size - r.offset) ||
        !std::isfinite(ascentEm) || !std::isfinite(descentEm) || !std::isfinite(lineGapEm) ||
        ascentEm < -16.0f || ascentEm > 16.0f ||
        descentEm < -16.0f || descentEm > 16.0f ||
        lineGapEm < -16.0f || lineGapEm > 16.0f) {
        return false;
    }

    std::string familyName(reinterpret_cast<const char*>(shardBytes + r.offset), familyLen);
    r.offset += familyLen;

    // Pre-validate all glyph records before mutating atlas state
    const std::size_t recordsStartOffset = r.offset;
    if (glyphCount > (r.size - r.offset) / 36u) {
        return false;
    }
    for (std::uint32_t i = 0u; i < glyphCount; ++i) {
        std::uint32_t cp = 0u;
        std::uint16_t gid = 0u, flags = 0u, cw = 0u, ch = 0u;
        float adv = 0.0f, bx = 0.0f, by = 0.0f, qw = 0.0f, qh = 0.0f, pxr = 4.0f;
        if (!r.ReadU32(cp) || !r.ReadU16(gid) || !r.ReadU16(flags) ||
            !r.ReadU16(cw) || !r.ReadU16(ch) ||
            !r.ReadF32(adv) || !r.ReadF32(bx) || !r.ReadF32(by) ||
            !r.ReadF32(qw) || !r.ReadF32(qh) || !r.ReadF32(pxr)) {
            return false;
        }
        if (!std::isfinite(adv) || !std::isfinite(bx) || !std::isfinite(by) ||
            !std::isfinite(qw) || !std::isfinite(qh) || !std::isfinite(pxr) ||
            adv < -32.0f || adv > 64.0f ||
            bx < -32.0f || bx > 32.0f ||
            by < -32.0f || by > 32.0f ||
            qw < 0.0f || qw > 64.0f ||
            qh < 0.0f || qh > 64.0f ||
            pxr <= 0.0f || pxr > 128.0f) {
            return false;
        }
        const std::size_t pixBytes = ((flags & 0x0002u) != 0u)
            ? (static_cast<std::size_t>(cw) * ch)
            : 0u;
        if (cw > 256u || ch > 256u || pixBytes > (r.size - r.offset)) {
            return false;
        }
        r.offset += pixBytes;
    }

    // Validation succeeded — unpack into Skyline atlas under lock
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t targetFontId = 0u;
    const std::string normKey = NormalizeFamilyKey(familyName);
    if (!normKey.empty()) {
        auto it = familyAliasToIndex_.find(normKey);
        if (it != familyAliasToIndex_.end()) {
            targetFontId = fonts_[it->second].fontId;
        } else {
            const std::size_t newIdx = fonts_.size();
            RegisteredFontEntry shardFont{};
            shardFont.fontId = nextFontId_++;
            shardFont.canonicalKey = normKey;
            shardFont.shardAscentEm = ascentEm;
            shardFont.shardDescentEm = descentEm;
            shardFont.shardLineGapEm = lineGapEm;
            shardFont.isShardOnly = true;
            targetFontId = shardFont.fontId;
            fonts_.push_back(std::move(shardFont));
            familyAliasToIndex_[normKey] = newIdx;
            familyAliasToEntries_[normKey].push_back(newIdx);
        }
    }

    const float invW = 1.0f / static_cast<float>(atlasWidth_);
    const float invH = 1.0f / static_cast<float>(atlasHeight_);

    r.offset = recordsStartOffset;
    std::size_t unpackedCount = 0u;
    for (std::uint32_t i = 0u; i < glyphCount; ++i) {
        FontAtlasGlyph g{};
        std::uint16_t gid16 = 0u, flags = 0u;
        (void)r.ReadU32(g.codePoint);
        (void)r.ReadU16(gid16);
        (void)r.ReadU16(flags);
        (void)r.ReadU16(g.cellWidth);
        (void)r.ReadU16(g.cellHeight);
        (void)r.ReadF32(g.advanceEm);
        (void)r.ReadF32(g.bearingXEm);
        (void)r.ReadF32(g.bearingYEm);
        (void)r.ReadF32(g.quadWidthEm);
        (void)r.ReadF32(g.quadHeightEm);
        (void)r.ReadF32(g.pxRange);
        g.glyphIndex = gid16;
        g.isWhitespace = (flags & 0x0001u) != 0u;
        g.isFallback = false;
        g.valid = true;

        const std::size_t pixBytes = ((flags & 0x0002u) != 0u)
            ? (static_cast<std::size_t>(g.cellWidth) * g.cellHeight)
            : 0u;
        const std::uint8_t* srcPix = shardBytes + r.offset;
        r.offset += pixBytes;

        if (!g.isWhitespace && pixBytes > 0u && g.cellWidth > 0u && g.cellHeight > 0u) {
            std::uint16_t ax = 0u, ay = 0u;
            if (AllocateSkylineRectLocked(g.cellWidth, g.cellHeight, ax, ay)) {
                BlitCellR8Locked(ax, ay, g.cellWidth, g.cellHeight, srcPix);
                g.atlasX = ax;
                g.atlasY = ay;
                g.u0 = (static_cast<float>(ax) + 0.5f) * invW;
                g.v0 = (static_cast<float>(ay) + 0.5f) * invH;
                g.u1 = (static_cast<float>(ax + g.cellWidth) - 0.5f) * invW;
                g.v1 = (static_cast<float>(ay + g.cellHeight) - 0.5f) * invH;
            } else {
                // Atlas full — do not cache empty tofu entry permanently
                continue;
            }
        } else {
            g.u0 = 2.5f * invW;
            g.v0 = 2.5f * invH;
            g.u1 = 6.5f * invW;
            g.v1 = 6.5f * invH;
        }

        // Register under both targetFontId and global shard slot (fontId == 0) so any streaming CJK lookup resolves it!
        if (targetFontId != 0u) {
            glyphCache_[(static_cast<std::uint64_t>(targetFontId) << 32u) | g.codePoint] = g;
        }
        glyphCache_[static_cast<std::uint64_t>(g.codePoint)] = g;
        ++unpackedCount;
    }

    if (outRangeStart) *outRangeStart = rangeStart;
    if (outRangeEnd) *outRangeEnd = rangeEnd;
    if (outUnpackedGlyphCount) *outUnpackedGlyphCount = unpackedCount;
    return true;
}

bool FontAtlas::AcquireSdfView(
    const std::uint8_t*& outData,
    std::uint16_t& outAtlasW,
    std::uint16_t& outAtlasH,
    std::uint16_t& outGlyphH) const noexcept {
    struct TlsFontView {
        std::uint64_t instanceId = 0u;
        std::uint64_t rev = 0u;
        std::uint16_t w = 0u;
        std::uint16_t h = 0u;
        std::uint16_t glyphH = 0u;
        const std::uint8_t* data = nullptr;
    };
    thread_local TlsFontView tlsView{};
    const std::uint64_t curRev = revision_.load(std::memory_order_acquire);
    if (tlsView.instanceId != instanceId_ || tlsView.rev != curRev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sdfPixels_.empty() || atlasWidth_ == 0u || atlasHeight_ == 0u) {
            return false;
        }
        tlsView.instanceId = instanceId_;
        tlsView.rev = revision_.load(std::memory_order_acquire);
        tlsView.w = atlasWidth_;
        tlsView.h = atlasHeight_;
        tlsView.glyphH = std::min<std::uint16_t>(
            glyphRegionHeight_,
            static_cast<std::uint16_t>(sdfPixels_.size() / atlasWidth_));
        tlsView.data = sdfPixels_.data();
    }
    outData = tlsView.data;
    outAtlasW = tlsView.w;
    outAtlasH = tlsView.h;
    outGlyphH = tlsView.glyphH;
    return outData != nullptr && outAtlasW > 0u && outAtlasH > 0u;
}

float FontAtlas::SampleSdfBilinear(float u, float v) const noexcept {
    const std::uint8_t* sdfPixels_ = nullptr;
    std::uint16_t atlasWidth_ = 0u;
    std::uint16_t atlasHeight_ = 0u;
    std::uint16_t glyphH = 0u;
    if (!AcquireSdfView(sdfPixels_, atlasWidth_, atlasHeight_, glyphH)) {
        return 0.0f;
    }
    const float tx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(atlasWidth_) - 0.5f;
    const float ty = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(atlasHeight_) - 0.5f;

    const float flX = std::floor(tx);
    const float flY = std::floor(ty);
    const int x0 = std::clamp(static_cast<int>(flX), 0, static_cast<int>(atlasWidth_) - 1);
    const int y0 = std::clamp(static_cast<int>(flY), 0, static_cast<int>(atlasHeight_) - 1);
    const int x1 = std::min(x0 + 1, static_cast<int>(atlasWidth_) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(atlasHeight_) - 1);
    const float fx = std::clamp(tx - flX, 0.0f, 1.0f);
    const float fy = std::clamp(ty - flY, 0.0f, 1.0f);

    if (y0 >= static_cast<int>(glyphH)) {
        return 0.0f;
    }
    auto at = [&](int x, int y) noexcept -> float {
        if (y >= static_cast<int>(glyphH)) {
            return 0.0f;
        }
        return static_cast<float>(
                   sdfPixels_[static_cast<std::size_t>(y) * atlasWidth_ + static_cast<std::size_t>(x)]) *
               (1.0f / 255.0f);
    };

    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const float bot = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return top + (bot - top) * fy;
}

float FontAtlas::EvaluateGlyphSdfCoverage(
    float u,
    float v,
    float uSpan,
    float vSpan,
    float screenQuadW,
    float screenQuadH,
    float pxRange) const noexcept {
    const float sd = SampleSdfBilinear(u, v);
    const float texW = std::max(1.0f, std::fabs(uSpan) * static_cast<float>(atlasWidth_));
    const float texH = std::max(1.0f, std::fabs(vSpan) * static_cast<float>(atlasHeight_));
    const float scaleX = std::max(1.0f, screenQuadW) / texW;
    const float scaleY = std::max(1.0f, screenQuadH) / texH;
    const float avgScale = 0.5f * (scaleX + scaleY);
    const float screenPxRange = std::max(2.0f * std::max(pxRange, 1.0f) * avgScale, 1.25f);
    constexpr float kEdgeMidpoint = 128.0f / 255.0f;
    const float screenDist = (sd - kEdgeMidpoint) * screenPxRange;
    const float cov = std::clamp(screenDist + 0.5f, 0.0f, 1.0f);
    return cov * cov * (3.0f - 2.0f * cov);
}

} // namespace aero
