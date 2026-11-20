#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"

namespace aero {

namespace {

struct TtfReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;

    [[nodiscard]] bool InBounds(std::size_t offset, std::size_t byteCount) const noexcept {
        return offset <= size && byteCount <= (size - offset);
    }

    [[nodiscard]] bool ReadU8(std::size_t offset, std::uint8_t& out) const noexcept {
        if (!InBounds(offset, 1u)) {
            return false;
        }
        out = data[offset];
        return true;
    }

    [[nodiscard]] bool ReadI8(std::size_t offset, std::int8_t& out) const noexcept {
        std::uint8_t u = 0u;
        if (!ReadU8(offset, u)) {
            return false;
        }
        out = static_cast<std::int8_t>(u);
        return true;
    }

    [[nodiscard]] bool ReadU16BE(std::size_t offset, std::uint16_t& out) const noexcept {
        if (!InBounds(offset, 2u)) {
            return false;
        }
        out = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[offset]) << 8u) |
            static_cast<std::uint16_t>(data[offset + 1u]));
        return true;
    }

    [[nodiscard]] bool ReadI16BE(std::size_t offset, std::int16_t& out) const noexcept {
        std::uint16_t u = 0u;
        if (!ReadU16BE(offset, u)) {
            return false;
        }
        out = static_cast<std::int16_t>(u);
        return true;
    }

    [[nodiscard]] bool ReadU32BE(std::size_t offset, std::uint32_t& out) const noexcept {
        if (!InBounds(offset, 4u)) {
            return false;
        }
        out = (static_cast<std::uint32_t>(data[offset + 0u]) << 24u) |
              (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
              (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
              static_cast<std::uint32_t>(data[offset + 3u]);
        return true;
    }

    [[nodiscard]] bool ReadF2Dot14BE(std::size_t offset, float& out) const noexcept {
        std::int16_t raw = 0;
        if (!ReadI16BE(offset, raw)) {
            return false;
        }
        out = static_cast<float>(raw) * (1.0f / 16384.0f);
        return true;
    }
};

constexpr std::uint32_t MakeTag(char a, char b, char c, char d) noexcept {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) << 24u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 8u) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(d));
}

void AppendUtf8CodePoint(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::string DecodeUtf16BeToUtf8(const std::uint8_t* bytes, std::size_t byteLen) {
    std::string out;
    out.reserve(byteLen);
    std::size_t i = 0u;
    while (i + 1u < byteLen) {
        const std::uint16_t w0 = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[i]) << 8u) | bytes[i + 1u]);
        i += 2u;
        if (w0 >= 0xD800u && w0 <= 0xDBFFu && i + 1u < byteLen) {
            const std::uint16_t w1 = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[i]) << 8u) | bytes[i + 1u]);
            if (w1 >= 0xDC00u && w1 <= 0xDFFFu) {
                i += 2u;
                const std::uint32_t cp =
                    0x10000u + ((static_cast<std::uint32_t>(w0 - 0xD800u) << 10u) |
                                static_cast<std::uint32_t>(w1 - 0xDC00u));
                AppendUtf8CodePoint(out, cp);
                continue;
            }
        }
        if (w0 != 0u) {
            AppendUtf8CodePoint(out, w0);
        }
    }
    return out;
}

// Little-endian helpers for "AGFS" binary shard serialization
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

[[nodiscard]] int EvaluateWindingNumber(
    const std::vector<YMonotonicSeg>& segs,
    float px,
    float py) noexcept {
    int winding = 0;
    for (const auto& s : segs) {
        if (py < s.minY || py >= s.maxY || px >= s.maxX) {
            continue;
        }
        const bool upward = (s.y0 <= py && s.y2 > py);
        const bool downward = (s.y0 > py && s.y2 <= py);
        if (!upward && !downward) {
            continue;
        }
        float xCross = 0.0f;
        if (!s.isQuadratic) {
            const float t = (py - s.y0) / (s.y2 - s.y0);
            xCross = s.x0 + t * (s.x2 - s.x0);
        } else {
            const float a = s.y0 - 2.0f * s.y1 + s.y2;
            const float b = s.y1 - s.y0;
            const float c = s.y0 - py;
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
        if (xCross > px) {
            winding += upward ? 1 : -1;
        }
    }
    return winding;
}

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

[[nodiscard]] bool IsWordBreakAfter(std::uint32_t cp) noexcept {
    return cp == ' ' || cp == '\t' || cp == '-' || IsWideCjkCodePoint(cp);
}

} // namespace

// ============================================================================
// TrueTypeFontFace Implementation
// ============================================================================

bool TrueTypeFontFace::LoadFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    std::string* outError) {
    valid_ = false;
    isCff_ = false;
    weightClass_ = 400u;
    isItalic_ = false;
    contentHash_ = 0u;
    data_.clear();
    familyName_.clear();
    fullName_.clear();
    cmap12Groups_.clear();
    cmap4Segments_.clear();
    cmap6GlyphIds_.clear();
    kernPairs_.clear();
    hasFormat12_ = false;
    hasFormat4_ = false;
    isSymbolCmap_ = false;

    if (!data || size < 12u) {
        if (outError) {
            *outError = "TTF buffer is null or smaller than 12-byte offset table";
        }
        return false;
    }

    std::uint64_t h = 14695981039346656037ULL;
    const std::size_t stride = (size > 65536u) ? (size / 16384u) : 1u;
    for (std::size_t i = 0u; i < size; i += stride) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= 1099511628211ULL;
    }
    h ^= static_cast<std::uint64_t>(size);
    h *= 1099511628211ULL;
    contentHash_ = (h == 0u) ? 1u : h;
    data_.assign(data, data + size);
    if (!ParseTables(outError)) {
        data_.clear();
        return false;
    }
    valid_ = true;
    return true;
}

bool TrueTypeFontFace::LoadFromFile(std::string_view filePath, std::string* outError) {
    std::ifstream ifs(std::string(filePath), std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        if (outError) {
            *outError = "Failed to open font file: " + std::string(filePath);
        }
        return false;
    }
    const std::streampos endPos = ifs.tellg();
    if (endPos <= 0) {
        if (outError) {
            *outError = "Font file is empty: " + std::string(filePath);
        }
        return false;
    }
    const auto fileSize = static_cast<std::size_t>(endPos);
    std::vector<std::uint8_t> bytes(fileSize);
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fileSize))) {
        if (outError) {
            *outError = "Failed to read font file bytes: " + std::string(filePath);
        }
        return false;
    }

    if (!LoadFromMemory(bytes.data(), bytes.size(), outError)) {
        return false;
    }

    if (familyName_.empty()) {
        // Fallback family name from file stem
        std::string_view stem = filePath;
        if (const auto slash = stem.find_last_of("/\\"); slash != std::string_view::npos) {
            stem = stem.substr(slash + 1u);
        }
        if (const auto dot = stem.rfind('.'); dot != std::string_view::npos) {
            stem = stem.substr(0, dot);
        }
        familyName_.assign(stem);
    }
    return true;
}

bool TrueTypeFontFace::ParseTables(std::string* outError) {
    TtfReader r{data_.data(), data_.size()};
    std::uint32_t sfntVersion = 0u;
    std::uint16_t numTables = 0u;
    if (!r.ReadU32BE(0u, sfntVersion) || !r.ReadU16BE(4u, numTables)) {
        if (outError) *outError = "Corrupted sfnt header";
        return false;
    }

    // Standard TrueType / OpenType sfnt versions: 0x00010000, 'true', or 'OTTO' (CFF)
    if (sfntVersion != 0x00010000u &&
        sfntVersion != MakeTag('t', 'r', 'u', 'e') &&
        sfntVersion != MakeTag('O', 'T', 'T', 'O')) {
        if (outError) *outError = "Unsupported sfnt version (requires TrueType glyf or OpenType CFF outlines)";
        return false;
    }
    if (numTables == 0u || !r.InBounds(12u, static_cast<std::size_t>(numTables) * 16u)) {
        if (outError) *outError = "Invalid TrueType table directory count";
        return false;
    }

    struct TableSpan {
        std::uint32_t offset = 0u;
        std::uint32_t length = 0u;
        bool present = false;
    };
    TableSpan head{}, hhea{}, maxp{}, cmap{}, hmtx{}, loca{}, glyf{}, cff{}, name{}, kern{}, os2{};

    for (std::uint16_t i = 0u; i < numTables; ++i) {
        const std::size_t recOff = 12u + static_cast<std::size_t>(i) * 16u;
        std::uint32_t tag = 0u, off = 0u, len = 0u;
        if (!r.ReadU32BE(recOff, tag) ||
            !r.ReadU32BE(recOff + 8u, off) ||
            !r.ReadU32BE(recOff + 12u, len)) {
            return false;
        }
        if (!r.InBounds(off, len)) {
            if (outError) *outError = "TrueType table exceeds file bounds";
            return false;
        }
        const TableSpan span{off, len, true};
        if (tag == MakeTag('h', 'e', 'a', 'd')) head = span;
        else if (tag == MakeTag('h', 'h', 'e', 'a')) hhea = span;
        else if (tag == MakeTag('m', 'a', 'x', 'p')) maxp = span;
        else if (tag == MakeTag('c', 'm', 'a', 'p')) cmap = span;
        else if (tag == MakeTag('h', 'm', 't', 'x')) hmtx = span;
        else if (tag == MakeTag('l', 'o', 'c', 'a')) loca = span;
        else if (tag == MakeTag('g', 'l', 'y', 'f')) glyf = span;
        else if (tag == MakeTag('C', 'F', 'F', ' ')) cff = span;
        else if (tag == MakeTag('n', 'a', 'm', 'e')) name = span;
        else if (tag == MakeTag('k', 'e', 'r', 'n')) kern = span;
        else if (tag == MakeTag('O', 'S', '/', '2')) os2 = span;
        else if (tag == MakeTag('G', 'S', 'U', 'B')) ParseGsubTable(off, len);
    }

    const bool hasGlyf = loca.present && glyf.present;
    const bool hasCff = cff.present;
    if (!head.present || !hhea.present || !maxp.present ||
        !cmap.present || !hmtx.present || (!hasGlyf && !hasCff)) {
        if (outError) *outError = "Missing mandatory font table (head/hhea/maxp/cmap/hmtx/glyf|CFF )";
        return false;
    }

    isCff_ = hasCff && !hasGlyf;

    if (head.length < 54u ||
        !r.ReadU16BE(head.offset + 18u, unitsPerEm_) ||
        !r.ReadI16BE(head.offset + 50u, indexToLocFormat_) ||
        unitsPerEm_ < 16u ||
        (!isCff_ && indexToLocFormat_ != 0 && indexToLocFormat_ != 1)) {
        if (outError) *outError = "Invalid TrueType head table";
        return false;
    }

    std::uint16_t macStyle = 0u;
    (void)r.ReadU16BE(head.offset + 44u, macStyle);
    if (os2.present && os2.length >= 6u) {
        std::uint16_t usWeight = 400u;
        if (r.ReadU16BE(os2.offset + 4u, usWeight) && usWeight >= 100u && usWeight <= 1000u) {
            weightClass_ = usWeight;
        }
        if (os2.length >= 64u) {
            std::uint16_t fsSelection = 0u;
            if (r.ReadU16BE(os2.offset + 62u, fsSelection)) {
                isItalic_ = ((fsSelection & 0x0001u) != 0u) || ((fsSelection & 0x0200u) != 0u);
                if ((fsSelection & 0x0020u) != 0u && weightClass_ <= 400u) {
                    weightClass_ = 700u;
                }
            }
        } else {
            isItalic_ = (macStyle & 0x0002u) != 0u;
            if ((macStyle & 0x0001u) != 0u && weightClass_ <= 400u) {
                weightClass_ = 700u;
            }
        }
    } else {
        isItalic_ = (macStyle & 0x0002u) != 0u;
        if ((macStyle & 0x0001u) != 0u) {
            weightClass_ = 700u;
        }
    }

    if (hhea.length < 36u ||
        !r.ReadI16BE(hhea.offset + 4u, ascent_) ||
        !r.ReadI16BE(hhea.offset + 6u, descent_) ||
        !r.ReadI16BE(hhea.offset + 8u, lineGap_) ||
        !r.ReadU16BE(hhea.offset + 34u, numberOfHMetrics_) ||
        numberOfHMetrics_ == 0u) {
        if (outError) *outError = "Invalid TrueType hhea table";
        return false;
    }

    if (maxp.length < 6u || !r.ReadU16BE(maxp.offset + 4u, numGlyphs_) || numGlyphs_ == 0u) {
        if (outError) *outError = "Invalid TrueType maxp table";
        return false;
    }

    hmtxOffset_ = hmtx.offset;
    hmtxLength_ = hmtx.length;
    locaOffset_ = loca.offset;
    locaLength_ = loca.length;
    glyfOffset_ = glyf.offset;
    glyfLength_ = glyf.length;

    if (static_cast<std::uint64_t>(hmtxOffset_) + static_cast<std::uint64_t>(hmtxLength_) > data_.size()) {
        if (outError) *outError = "TrueType hmtx table exceeds file bounds";
        return false;
    }
    if (numberOfHMetrics_ > numGlyphs_) {
        numberOfHMetrics_ = numGlyphs_;
    }
    if (hmtxLength_ < static_cast<std::size_t>(numberOfHMetrics_) * 4u) {
        if (outError) *outError = "Truncated TrueType hmtx table";
        return false;
    }

    if (isCff_) {
        if (!ParseCffTable(cff.offset, cff.length, outError)) {
            return false;
        }
    } else {
        const std::size_t requiredLocaBytes = (indexToLocFormat_ == 0)
            ? (static_cast<std::size_t>(numGlyphs_) + 1u) * 2u
            : (static_cast<std::size_t>(numGlyphs_) + 1u) * 4u;
        if (locaLength_ < requiredLocaBytes) {
            if (outError) *outError = "Truncated TrueType loca table";
            return false;
        }
    }

    if (!ParseCmapTable(cmap.offset, cmap.length)) {
        if (outError) *outError = "No supported Unicode cmap subtable (Format 4/12/6) found";
        return false;
    }

    if (name.present) {
        (void)ParseNameTable(name.offset, name.length);
    }
    if (kern.present) {
        ParseKernTable(kern.offset, kern.length);
    }
    return true;
}

bool TrueTypeFontFace::ParseNameTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 6u) {
        return false;
    }
    std::uint16_t count = 0u, stringOffset = 0u;
    if (!r.ReadU16BE(offset + 2u, count) || !r.ReadU16BE(offset + 4u, stringOffset)) {
        return false;
    }
    const std::size_t strStorageBase = static_cast<std::size_t>(offset) + stringOffset;
    if (!r.InBounds(offset + 6u, static_cast<std::size_t>(count) * 12u)) {
        return false;
    }

    std::string prefFamily;
    std::string winFamily;
    std::string macFamily;
    std::string winFull;
    std::string macFull;

    for (std::uint16_t i = 0u; i < count; ++i) {
        const std::size_t rec = static_cast<std::size_t>(offset) + 6u + static_cast<std::size_t>(i) * 12u;
        std::uint16_t platformId = 0u, encodingId = 0u, languageId = 0u, nameId = 0u, strLen = 0u, strOff = 0u;
        if (!r.ReadU16BE(rec + 0u, platformId) ||
            !r.ReadU16BE(rec + 2u, encodingId) ||
            !r.ReadU16BE(rec + 4u, languageId) ||
            !r.ReadU16BE(rec + 6u, nameId) ||
            !r.ReadU16BE(rec + 8u, strLen) ||
            !r.ReadU16BE(rec + 10u, strOff)) {
            continue;
        }
        if (strLen == 0u || !r.InBounds(strStorageBase + strOff, strLen)) {
            continue;
        }
        const std::uint8_t* rawStr = data_.data() + strStorageBase + strOff;

        if ((platformId == 3u && (encodingId == 1u || encodingId == 10u || encodingId == 0u)) ||
            platformId == 0u) {
            const std::string decoded = DecodeUtf16BeToUtf8(rawStr, strLen);
            if (decoded.empty()) continue;
            if (nameId == 16u && (prefFamily.empty() || languageId == 0x0409u)) {
                prefFamily = decoded;
            } else if (nameId == 1u && (winFamily.empty() || languageId == 0x0409u)) {
                winFamily = decoded;
            } else if (nameId == 4u && (winFull.empty() || languageId == 0x0409u)) {
                winFull = decoded;
            }
        } else if (platformId == 1u && encodingId == 0u) {
            std::string ascii;
            ascii.reserve(strLen);
            for (std::uint16_t k = 0u; k < strLen; ++k) {
                if (rawStr[k] >= 0x20u && rawStr[k] <= 0x7Eu) {
                    ascii.push_back(static_cast<char>(rawStr[k]));
                }
            }
            if (nameId == 1u && macFamily.empty()) macFamily = std::move(ascii);
            else if (nameId == 4u && macFull.empty()) macFull = std::move(ascii);
        }
    }

    if (!prefFamily.empty()) familyName_ = std::move(prefFamily);
    else if (!winFamily.empty()) familyName_ = std::move(winFamily);
    else if (!macFamily.empty()) familyName_ = std::move(macFamily);

    if (!winFull.empty()) fullName_ = std::move(winFull);
    else if (!macFull.empty()) fullName_ = std::move(macFull);
    else fullName_ = familyName_;
    return !familyName_.empty();
}

bool TrueTypeFontFace::ParseCmapTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 4u) {
        return false;
    }
    std::uint16_t numSubtables = 0u;
    if (!r.ReadU16BE(offset + 2u, numSubtables) ||
        !r.InBounds(offset + 4u, static_cast<std::size_t>(numSubtables) * 8u)) {
        return false;
    }

    std::size_t bestFmt12Off = 0u;
    int bestFmt12Score = -1;
    std::size_t bestFmt4Off = 0u;
    int bestFmt4Score = -1;
    bool bestFmt4Symbol = false;
    std::size_t bestFmt6Off = 0u;
    std::size_t bestFmt0Off = 0u;

    for (std::uint16_t i = 0u; i < numSubtables; ++i) {
        const std::size_t rec = static_cast<std::size_t>(offset) + 4u + static_cast<std::size_t>(i) * 8u;
        std::uint16_t plat = 0u, enc = 0u;
        std::uint32_t relOff = 0u;
        if (!r.ReadU16BE(rec, plat) || !r.ReadU16BE(rec + 2u, enc) || !r.ReadU32BE(rec + 4u, relOff)) {
            continue;
        }
        const std::size_t subOff = static_cast<std::size_t>(offset) + relOff;
        std::uint16_t fmt = 0u;
        if (!r.ReadU16BE(subOff, fmt)) {
            continue;
        }
        if (fmt == 12u) {
            const int score = (plat == 3u && enc == 10u) ? 100 : (plat == 0u ? 90 : 50);
            if (score > bestFmt12Score) {
                bestFmt12Score = score;
                bestFmt12Off = subOff;
            }
        } else if (fmt == 4u) {
            int score = 10;
            if (plat == 3u && enc == 1u) score = 100;
            else if (plat == 0u) score = 90;
            else if (plat == 3u && enc == 0u) score = 60;
            if (score > bestFmt4Score) {
                bestFmt4Score = score;
                bestFmt4Off = subOff;
                bestFmt4Symbol = (plat == 3u && enc == 0u);
            }
        } else if (fmt == 6u && bestFmt6Off == 0u) {
            bestFmt6Off = subOff;
        } else if (fmt == 0u && bestFmt0Off == 0u) {
            bestFmt0Off = subOff;
        }
    }

    if (bestFmt12Off != 0u) {
        std::uint32_t subLen = 0u, numGroups = 0u;
        if (r.ReadU32BE(bestFmt12Off + 4u, subLen) &&
            r.ReadU32BE(bestFmt12Off + 12u, numGroups) &&
            r.InBounds(bestFmt12Off + 16u, static_cast<std::size_t>(numGroups) * 12u)) {
            cmap12Groups_.resize(numGroups);
            cmap12Sorted_ = true;
            for (std::uint32_t g = 0u; g < numGroups; ++g) {
                const std::size_t gOff = bestFmt12Off + 16u + static_cast<std::size_t>(g) * 12u;
                (void)r.ReadU32BE(gOff + 0u, cmap12Groups_[g].startCharCode);
                (void)r.ReadU32BE(gOff + 4u, cmap12Groups_[g].endCharCode);
                (void)r.ReadU32BE(gOff + 8u, cmap12Groups_[g].startGlyphId);
                if (cmap12Groups_[g].startCharCode > cmap12Groups_[g].endCharCode ||
                    (g > 0u && cmap12Groups_[g].endCharCode <= cmap12Groups_[g - 1u].endCharCode)) {
                    cmap12Sorted_ = false;
                }
            }
            hasFormat12_ = !cmap12Groups_.empty();
        }
    }

    if (bestFmt4Off != 0u) {
        std::uint16_t subLen = 0u, segCountX2 = 0u;
        if (r.ReadU16BE(bestFmt4Off + 2u, subLen) &&
            r.ReadU16BE(bestFmt4Off + 6u, segCountX2) &&
            segCountX2 >= 2u && (segCountX2 % 2u) == 0u) {
            const std::uint16_t segCount = static_cast<std::uint16_t>(segCountX2 / 2u);
            const std::size_t endCodeBase = bestFmt4Off + 14u;
            const std::size_t startCodeBase = endCodeBase + static_cast<std::size_t>(segCount) * 2u + 2u;
            const std::size_t idDeltaBase = startCodeBase + static_cast<std::size_t>(segCount) * 2u;
            const std::size_t idRangeBase = idDeltaBase + static_cast<std::size_t>(segCount) * 2u;
            if (r.InBounds(idRangeBase, static_cast<std::size_t>(segCount) * 2u)) {
                cmap4Segments_.resize(segCount);
                cmap4Sorted_ = true;
                for (std::uint16_t s = 0u; s < segCount; ++s) {
                    const std::size_t wOff = static_cast<std::size_t>(s) * 2u;
                    (void)r.ReadU16BE(endCodeBase + wOff, cmap4Segments_[s].endCode);
                    (void)r.ReadU16BE(startCodeBase + wOff, cmap4Segments_[s].startCode);
                    (void)r.ReadI16BE(idDeltaBase + wOff, cmap4Segments_[s].idDelta);
                    (void)r.ReadU16BE(idRangeBase + wOff, cmap4Segments_[s].idRangeOffset);
                    cmap4Segments_[s].idRangeOffsetBytePos = static_cast<std::uint32_t>(idRangeBase + wOff);
                    if (cmap4Segments_[s].startCode > cmap4Segments_[s].endCode ||
                        (s > 0u && cmap4Segments_[s].endCode <= cmap4Segments_[s - 1u].endCode)) {
                        cmap4Sorted_ = false;
                    }
                }
                hasFormat4_ = !cmap4Segments_.empty();
                isSymbolCmap_ = bestFmt4Symbol;
            }
        }
    }

    if (!hasFormat12_ && !hasFormat4_) {
        if (bestFmt6Off != 0u) {
            std::uint16_t firstCode = 0u, entryCount = 0u;
            if (r.ReadU16BE(bestFmt6Off + 6u, firstCode) &&
                r.ReadU16BE(bestFmt6Off + 8u, entryCount) &&
                entryCount > 0u &&
                r.InBounds(bestFmt6Off + 10u, static_cast<std::size_t>(entryCount) * 2u)) {
                cmap6FirstCode_ = firstCode;
                cmap6GlyphIds_.resize(entryCount);
                for (std::uint16_t i = 0u; i < entryCount; ++i) {
                    (void)r.ReadU16BE(bestFmt6Off + 10u + static_cast<std::size_t>(i) * 2u, cmap6GlyphIds_[i]);
                }
            }
        } else if (bestFmt0Off != 0u && r.InBounds(bestFmt0Off + 6u, 256u)) {
            cmap6FirstCode_ = 0u;
            cmap6GlyphIds_.resize(256u);
            for (std::size_t i = 0u; i < 256u; ++i) {
                cmap6GlyphIds_[i] = data_[bestFmt0Off + 6u + i];
            }
        }
    }

    return hasFormat12_ || hasFormat4_ || !cmap6GlyphIds_.empty();
}

void TrueTypeFontFace::ParseKernTable(std::uint32_t offset, std::uint32_t length) {
    TtfReader r{data_.data(), data_.size()};
    if (length < 4u) {
        return;
    }
    std::uint16_t version16 = 0u;
    if (!r.ReadU16BE(offset, version16)) {
        return;
    }

    if (version16 == 0u) {
        // Microsoft/TrueType kern header: uint16 version(0), uint16 nTables
        std::uint16_t nTables = 0u;
        if (!r.ReadU16BE(offset + 2u, nTables)) {
            return;
        }
        std::size_t cur = static_cast<std::size_t>(offset) + 4u;
        const std::size_t tableEnd = static_cast<std::size_t>(offset) + length;
        for (std::uint16_t t = 0u; t < nTables && cur + 6u <= tableEnd; ++t) {
            std::uint16_t subLen = 0u, coverage = 0u;
            if (!r.ReadU16BE(cur + 2u, subLen) || !r.ReadU16BE(cur + 4u, coverage)) {
                break;
            }
            const std::uint8_t format = static_cast<std::uint8_t>((coverage >> 8u) & 0xFFu);
            const bool horizontal = (coverage & 0x0001u) != 0u;
            const bool minimum = (coverage & 0x0002u) != 0u;
            const bool crossStream = (coverage & 0x0004u) != 0u;
            if (format == 0u && horizontal && !minimum && !crossStream && cur + 14u <= tableEnd) {
                std::uint16_t nPairs = 0u;
                if (r.ReadU16BE(cur + 6u, nPairs) &&
                    r.InBounds(cur + 14u, static_cast<std::size_t>(nPairs) * 6u)) {
                    kernPairs_.reserve(kernPairs_.size() + nPairs);
                    for (std::uint16_t p = 0u; p < nPairs; ++p) {
                        const std::size_t pOff = cur + 14u + static_cast<std::size_t>(p) * 6u;
                        std::uint16_t left = 0u, right = 0u;
                        std::int16_t val = 0;
                        (void)r.ReadU16BE(pOff + 0u, left);
                        (void)r.ReadU16BE(pOff + 2u, right);
                        (void)r.ReadI16BE(pOff + 4u, val);
                        if (val != 0) {
                            kernPairs_.push_back(KernPairEntry{
                                (static_cast<std::uint32_t>(left) << 16u) | right,
                                val});
                        }
                    }
                }
            }
            if (subLen < 6u) {
                break;
            }
            cur += subLen;
        }
    }

    if (!kernPairs_.empty()) {
        std::sort(kernPairs_.begin(), kernPairs_.end(), [](const KernPairEntry& a, const KernPairEntry& b) {
            return a.packedPair < b.packedPair;
        });
    }
}


void TrueTypeFontFace::ParseGsubTable(std::uint32_t offset, std::uint32_t length) {
    gsubFeatures_.clear();
    hasArabicGsub_ = false;
    if (length < 10u || offset > data_.size() || length > data_.size() - offset) return;
    const std::uint8_t* g = data_.data() + offset;
    auto ru16 = [&](std::uint32_t p) noexcept -> std::uint16_t {
        if (p + 2u > length) return 0u;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(g[p]) << 8u) | g[p + 1u]);
    };
    auto ri16 = [&](std::uint32_t p) noexcept -> std::int16_t {
        return static_cast<std::int16_t>(ru16(p));
    };
    auto ru32 = [&](std::uint32_t p) noexcept -> std::uint32_t {
        if (p + 4u > length) return 0u;
        return (static_cast<std::uint32_t>(g[p]) << 24u) |
               (static_cast<std::uint32_t>(g[p + 1u]) << 16u) |
               (static_cast<std::uint32_t>(g[p + 2u]) << 8u) |
               static_cast<std::uint32_t>(g[p + 3u]);
    };
    auto readCoverage = [&](std::uint32_t covOff, std::vector<std::uint16_t>& outGlyphs) {
        outGlyphs.clear();
        if (covOff + 4u > length) return;
        const std::uint16_t fmt = ru16(covOff);
        if (fmt == 1u) {
            const std::uint16_t cnt = ru16(covOff + 2u);
            if (covOff + 4u + 2u * cnt > length) return;
            outGlyphs.reserve(cnt);
            for (std::uint16_t i = 0u; i < cnt; ++i) outGlyphs.push_back(ru16(covOff + 4u + 2u * i));
        } else if (fmt == 2u) {
            const std::uint16_t rcnt = ru16(covOff + 2u);
            if (covOff + 4u + 6u * rcnt > length) return;
            for (std::uint16_t i = 0u; i < rcnt; ++i) {
                const std::uint16_t s = ru16(covOff + 4u + 6u * i);
                const std::uint16_t e = ru16(covOff + 6u + 6u * i);
                for (std::uint32_t gid = s; gid <= e && outGlyphs.size() < 8192u; ++gid) {
                    outGlyphs.push_back(static_cast<std::uint16_t>(gid));
                }
            }
        }
    };

    const std::uint16_t featListOff = ru16(6u);
    const std::uint16_t lookupListOff = ru16(8u);
    if (featListOff < 10u || lookupListOff < 10u || featListOff + 2u > length || lookupListOff + 2u > length) return;

    const std::uint16_t lookupCount = ru16(lookupListOff);
    const std::uint16_t featCount = ru16(featListOff);
    if (featListOff + 2u + 6u * featCount > length) return;

    auto findOrAddFeat = [&](std::uint32_t tag) -> GsubFeatureRules& {
        for (auto& fr : gsubFeatures_) {
            if (fr.tag == tag) return fr;
        }
        gsubFeatures_.push_back(GsubFeatureRules{tag, {}, {}});
        return gsubFeatures_.back();
    };

    std::vector<std::uint16_t> cov;
    for (std::uint16_t fi = 0u; fi < featCount; ++fi) {
        const std::uint32_t ftag = ru32(featListOff + 2u + 6u * fi);
        const bool wanted =
            ftag == 0x69736F6Cu || // 'isol'
            ftag == 0x696E6974u || // 'init'
            ftag == 0x6D656469u || // 'medi'
            ftag == 0x66696E61u || // 'fina'
            ftag == 0x6C696761u || // 'liga'
            ftag == 0x726C6967u;   // 'rlig'
        if (!wanted) continue;
        const std::uint32_t fOff = featListOff + ru16(featListOff + 6u + 6u * fi);
        if (fOff + 4u > length) continue;
        const std::uint16_t lIdxCount = ru16(fOff + 2u);
        if (fOff + 4u + 2u * lIdxCount > length) continue;
        GsubFeatureRules& rules = findOrAddFeat(ftag);

        for (std::uint16_t li = 0u; li < lIdxCount; ++li) {
            const std::uint16_t lookupIdx = ru16(fOff + 4u + 2u * li);
            if (lookupIdx >= lookupCount || lookupListOff + 4u + 2u * lookupIdx > length) continue;
            const std::uint32_t lOff = lookupListOff + ru16(lookupListOff + 2u + 2u * lookupIdx);
            if (lOff + 6u > length) continue;
            const std::uint16_t lType = ru16(lOff);
            const std::uint16_t subCount = ru16(lOff + 4u);
            if (lOff + 6u + 2u * subCount > length) continue;
            for (std::uint16_t si = 0u; si < subCount; ++si) {
                std::uint16_t effType = lType;
                std::uint32_t sOff = lOff + ru16(lOff + 6u + 2u * si);
                if (effType == 7u) { // Extension Substitution
                    if (sOff + 8u > length || ru16(sOff) != 1u) continue;
                    effType = ru16(sOff + 2u);
                    const std::uint32_t extDelta = ru32(sOff + 4u);
                    if (extDelta > length - sOff) continue;
                    sOff += extDelta;
                }
                if (sOff + 6u > length) continue;
                const std::uint16_t substFmt = ru16(sOff);
                if (effType == 1u) { // SingleSubst
                    readCoverage(sOff + ru16(sOff + 2u), cov);
                    if (substFmt == 1u) {
                        const std::int16_t delta = ri16(sOff + 4u);
                        for (std::uint16_t fg : cov) {
                            rules.singles.push_back({fg, static_cast<std::uint16_t>((fg + delta) & 0xFFFF)});
                        }
                    } else if (substFmt == 2u) {
                        const std::uint16_t gCnt = ru16(sOff + 4u);
                        if (sOff + 6u + 2u * gCnt <= length) {
                            const std::size_t n = std::min<std::size_t>(cov.size(), gCnt);
                            for (std::size_t k = 0u; k < n; ++k) {
                                rules.singles.push_back({cov[k], ru16(sOff + 6u + 2u * static_cast<std::uint32_t>(k))});
                            }
                        }
                    }
                } else if (effType == 4u && substFmt == 1u) { // LigatureSubst
                    readCoverage(sOff + ru16(sOff + 2u), cov);
                    const std::uint16_t setCnt = ru16(sOff + 4u);
                    if (sOff + 6u + 2u * setCnt <= length) {
                        const std::size_t n = std::min<std::size_t>(cov.size(), setCnt);
                        for (std::size_t k = 0u; k < n; ++k) {
                            const std::uint32_t lsOff = sOff + ru16(sOff + 6u + 2u * static_cast<std::uint32_t>(k));
                            if (lsOff + 2u > length) continue;
                            const std::uint16_t ligCnt = ru16(lsOff);
                            if (lsOff + 2u + 2u * ligCnt > length) continue;
                            for (std::uint16_t m = 0u; m < ligCnt; ++m) {
                                const std::uint32_t lgOff = lsOff + ru16(lsOff + 2u + 2u * m);
                                if (lgOff + 4u > length) continue;
                                const std::uint16_t ligGlyph = ru16(lgOff);
                                const std::uint16_t compCnt = ru16(lgOff + 2u);
                                if (compCnt == 1u) {
                                    rules.singles.push_back({cov[k], ligGlyph});
                                } else if (compCnt == 2u && lgOff + 6u <= length) {
                                    rules.ligs.push_back({cov[k], ru16(lgOff + 4u), ligGlyph});
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!rules.singles.empty() || !rules.ligs.empty()) {
            hasArabicGsub_ = true;
        }
    }
}

std::uint32_t TrueTypeFontFace::ApplyArabicGsubFeature(std::uint32_t tag4, std::uint32_t glyphId) const noexcept {
    for (const auto& fr : gsubFeatures_) {
        if (fr.tag != tag4) continue;
        for (const auto& r : fr.singles) {
            if (r.fromGlyph == glyphId) return r.toGlyph;
        }
    }
    return glyphId;
}

std::uint32_t TrueTypeFontFace::ApplyArabicGsubLigature(std::uint32_t firstGlyphId, std::uint32_t secondGlyphId) const noexcept {
    for (const auto& fr : gsubFeatures_) {
        if (fr.tag != 0x6C696761u && fr.tag != 0x726C6967u) continue;
        for (const auto& r : fr.ligs) {
            if (r.firstGlyph == firstGlyphId && r.secondGlyph == secondGlyphId) return r.ligGlyph;
        }
    }
    return 0u;
}

std::uint32_t TrueTypeFontFace::FindGlyphIndex(std::uint32_t codePoint) const noexcept {
    if (!valid_) {
        return 0u;
    }

    if (hasFormat12_) {
        if (cmap12Sorted_) {
            auto it = std::lower_bound(
                cmap12Groups_.begin(),
                cmap12Groups_.end(),
                codePoint,
                [](const CmapFormat12Group& g, std::uint32_t cp) {
                    return g.endCharCode < cp;
                });
            if (it != cmap12Groups_.end() && codePoint >= it->startCharCode && codePoint <= it->endCharCode) {
                const std::uint32_t gid = it->startGlyphId + (codePoint - it->startCharCode);
                return (gid < numGlyphs_) ? gid : 0u;
            }
        } else {
            for (const auto& grp : cmap12Groups_) {
                if (codePoint >= grp.startCharCode && codePoint <= grp.endCharCode) {
                    const std::uint32_t gid = grp.startGlyphId + (codePoint - grp.startCharCode);
                    return (gid < numGlyphs_) ? gid : 0u;
                }
            }
        }
    }

    auto lookupFmt4 = [&](std::uint32_t cp) noexcept -> std::uint32_t {
        if (!hasFormat4_ || cp > 0xFFFFu) {
            return 0u;
        }
        const auto cp16 = static_cast<std::uint16_t>(cp);
        const CmapFormat4Segment* seg = nullptr;
        if (cmap4Sorted_) {
            auto it = std::lower_bound(
                cmap4Segments_.begin(),
                cmap4Segments_.end(),
                cp16,
                [](const CmapFormat4Segment& s, std::uint16_t target) {
                    return s.endCode < target;
                });
            if (it != cmap4Segments_.end() && cp16 >= it->startCode) {
                seg = &(*it);
            }
        } else {
            for (const auto& s : cmap4Segments_) {
                if (cp16 >= s.startCode && cp16 <= s.endCode) {
                    seg = &s;
                    break;
                }
            }
        }
        if (!seg) {
            return 0u;
        }
        if (seg->idRangeOffset == 0u) {
            const std::uint32_t gid = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(cp16) + static_cast<std::int32_t>(seg->idDelta));
            return (gid < numGlyphs_) ? gid : 0u;
        }
        const std::size_t wordAddr = static_cast<std::size_t>(seg->idRangeOffsetBytePos) +
            static_cast<std::size_t>(seg->idRangeOffset) +
            static_cast<std::size_t>(cp16 - seg->startCode) * 2u;
        TtfReader r{data_.data(), data_.size()};
        std::uint16_t rawGid = 0u;
        if (!r.ReadU16BE(wordAddr, rawGid) || rawGid == 0u) {
            return 0u;
        }
        const std::uint32_t gid = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(rawGid) + static_cast<std::int32_t>(seg->idDelta));
        return (gid < numGlyphs_) ? gid : 0u;
    };

    if (hasFormat4_) {
        const std::uint32_t direct = lookupFmt4(codePoint);
        if (direct != 0u) {
            return direct;
        }
        if (isSymbolCmap_ && codePoint >= 0x20u && codePoint <= 0xFFu) {
            return lookupFmt4(0xF000u + codePoint);
        }
    }

    if (!cmap6GlyphIds_.empty() && codePoint >= cmap6FirstCode_) {
        const std::uint32_t rel = codePoint - cmap6FirstCode_;
        if (rel < cmap6GlyphIds_.size()) {
            const std::uint32_t gid = cmap6GlyphIds_[rel];
            return (gid < numGlyphs_) ? gid : 0u;
        }
    }
    return 0u;
}

void TrueTypeFontFace::GetGlyphHorizontalMetrics(
    std::uint32_t glyphIndex,
    float& outAdvanceWidthFUnits,
    float& outLeftSideBearingFUnits) const noexcept {
    outAdvanceWidthFUnits = static_cast<float>(unitsPerEm_) * 0.6f;
    outLeftSideBearingFUnits = 0.0f;
    if (!valid_ || numberOfHMetrics_ == 0u) {
        return;
    }

    const std::size_t hmtxEnd = std::min<std::size_t>(
        data_.size(),
        static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(hmtxLength_));
    TtfReader r{data_.data(), hmtxEnd};
    const std::uint32_t clampedGlyph = (glyphIndex < numGlyphs_) ? glyphIndex : 0u;
    if (clampedGlyph < numberOfHMetrics_) {
        const std::size_t off = static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(clampedGlyph) * 4u;
        std::uint16_t adv = 0u;
        std::int16_t lsb = 0;
        if (r.ReadU16BE(off, adv) && r.ReadI16BE(off + 2u, lsb)) {
            outAdvanceWidthFUnits = static_cast<float>(adv);
            outLeftSideBearingFUnits = static_cast<float>(lsb);
        }
    } else {
        const std::size_t lastAdvOff =
            static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(numberOfHMetrics_ - 1u) * 4u;
        const std::size_t lsbOff =
            static_cast<std::size_t>(hmtxOffset_) + static_cast<std::size_t>(numberOfHMetrics_) * 4u +
            static_cast<std::size_t>(clampedGlyph - numberOfHMetrics_) * 2u;
        std::uint16_t adv = 0u;
        std::int16_t lsb = 0;
        if (r.ReadU16BE(lastAdvOff, adv)) {
            outAdvanceWidthFUnits = static_cast<float>(adv);
        }
        if (r.ReadI16BE(lsbOff, lsb)) {
            outLeftSideBearingFUnits = static_cast<float>(lsb);
        }
    }
}

float TrueTypeFontFace::GetKerningFUnits(
    std::uint32_t leftGlyphIndex,
    std::uint32_t rightGlyphIndex) const noexcept {
    if (kernPairs_.empty() || leftGlyphIndex > 0xFFFFu || rightGlyphIndex > 0xFFFFu) {
        return 0.0f;
    }
    const std::uint32_t key = (leftGlyphIndex << 16u) | rightGlyphIndex;
    auto it = std::lower_bound(
        kernPairs_.begin(),
        kernPairs_.end(),
        key,
        [](const KernPairEntry& e, std::uint32_t k) {
            return e.packedPair < k;
        });
    if (it != kernPairs_.end() && it->packedPair == key) {
        return static_cast<float>(it->valueFUnits);
    }
    return 0.0f;
}

bool TrueTypeFontFace::GetGlyphDataSpan(
    std::uint32_t glyphIndex,
    std::uint32_t& outOffset,
    std::uint32_t& outLength) const noexcept {
    outOffset = 0u;
    outLength = 0u;
    if (!valid_ || glyphIndex >= numGlyphs_) {
        return false;
    }
    TtfReader r{data_.data(), data_.size()};
    std::uint32_t g0 = 0u, g1 = 0u;
    if (indexToLocFormat_ == 0) {
        std::uint16_t s0 = 0u, s1 = 0u;
        const std::size_t base = static_cast<std::size_t>(locaOffset_) + static_cast<std::size_t>(glyphIndex) * 2u;
        if (!r.ReadU16BE(base, s0) || !r.ReadU16BE(base + 2u, s1)) {
            return false;
        }
        g0 = static_cast<std::uint32_t>(s0) * 2u;
        g1 = static_cast<std::uint32_t>(s1) * 2u;
    } else {
        const std::size_t base = static_cast<std::size_t>(locaOffset_) + static_cast<std::size_t>(glyphIndex) * 4u;
        if (!r.ReadU32BE(base, g0) || !r.ReadU32BE(base + 4u, g1)) {
            return false;
        }
    }
    if (g1 < g0 || g1 > glyfLength_) {
        return false;
    }
    outOffset = glyfOffset_ + g0;
    outLength = g1 - g0;
    return true;
}

bool TrueTypeFontFace::ExtractGlyphRecursive(
    std::uint32_t glyphIndex,
    std::uint32_t depth,
    std::uint32_t& ioTotalVisitedComponents,
    std::vector<TtfContourSegment>& outSegments,
    std::vector<TtfPoint>* outRawPoints) const {
    if (depth > 8u || ++ioTotalVisitedComponents > 128u || glyphIndex >= numGlyphs_) {
        return false;
    }

    std::uint32_t gOff = 0u, gLen = 0u;
    if (!GetGlyphDataSpan(glyphIndex, gOff, gLen)) {
        return false;
    }
    if (gLen == 0u) {
        // Valid whitespace glyph (e.g. U+0020 space)
        return true;
    }
    if (gLen < 10u) {
        return false;
    }

    const std::size_t glyphEnd = std::min<std::size_t>(
        data_.size(),
        static_cast<std::size_t>(gOff) + static_cast<std::size_t>(gLen));
    TtfReader r{data_.data(), glyphEnd};
    std::int16_t numContours = 0;
    if (!r.ReadI16BE(gOff, numContours)) {
        return false;
    }

    if (numContours == 0) {
        return true;
    }

    if (numContours > 0) {
        // Simple TrueType Glyph
        const auto contourCount = static_cast<std::size_t>(numContours);
        std::size_t pos = static_cast<std::size_t>(gOff) + 10u;
        if (!r.InBounds(pos, contourCount * 2u + 2u)) {
            return false;
        }

        std::vector<std::uint16_t> endPts(contourCount);
        for (std::size_t c = 0u; c < contourCount; ++c) {
            (void)r.ReadU16BE(pos, endPts[c]);
            pos += 2u;
            if (c > 0u && endPts[c] < endPts[c - 1u]) {
                return false;
            }
        }

        const std::size_t numPoints = static_cast<std::size_t>(endPts.back()) + 1u;
        std::uint16_t instructionLength = 0u;
        if (!r.ReadU16BE(pos, instructionLength)) {
            return false;
        }
        pos += 2u + instructionLength;
        if (pos > static_cast<std::size_t>(gOff) + gLen) {
            return false;
        }

        std::vector<std::uint8_t> flags(numPoints, 0u);
        for (std::size_t i = 0u; i < numPoints;) {
            std::uint8_t fl = 0u;
            if (!r.ReadU8(pos++, fl)) {
                return false;
            }
            flags[i++] = fl;
            if ((fl & 0x08u) != 0u) {
                std::uint8_t repeatCount = 0u;
                if (!r.ReadU8(pos++, repeatCount)) {
                    return false;
                }
                for (std::uint8_t rep = 0u; rep < repeatCount && i < numPoints; ++rep) {
                    flags[i++] = fl;
                }
            }
        }

        std::vector<TtfPoint> pts(numPoints);
        std::int32_t curX = 0;
        for (std::size_t i = 0u; i < numPoints; ++i) {
            const std::uint8_t fl = flags[i];
            pts[i].onCurve = (fl & 0x01u) != 0u;
            if ((fl & 0x02u) != 0u) {
                std::uint8_t dx = 0u;
                if (!r.ReadU8(pos++, dx)) return false;
                curX += ((fl & 0x10u) != 0u) ? static_cast<std::int32_t>(dx) : -static_cast<std::int32_t>(dx);
            } else if ((fl & 0x10u) == 0u) {
                std::int16_t dx = 0;
                if (!r.ReadI16BE(pos, dx)) return false;
                pos += 2u;
                curX += dx;
            }
            pts[i].x = static_cast<float>(curX);
        }

        std::int32_t curY = 0;
        for (std::size_t i = 0u; i < numPoints; ++i) {
            const std::uint8_t fl = flags[i];
            if ((fl & 0x04u) != 0u) {
                std::uint8_t dy = 0u;
                if (!r.ReadU8(pos++, dy)) return false;
                curY += ((fl & 0x20u) != 0u) ? static_cast<std::int32_t>(dy) : -static_cast<std::int32_t>(dy);
            } else if ((fl & 0x20u) == 0u) {
                std::int16_t dy = 0;
                if (!r.ReadI16BE(pos, dy)) return false;
                pos += 2u;
                curY += dy;
            }
            pts[i].y = static_cast<float>(curY);
        }

        if (outRawPoints) {
            outRawPoints->insert(outRawPoints->end(), pts.begin(), pts.end());
        }

        // Reconstruct closed TrueType quadratic Bezier + line segments per contour
        std::size_t startIdx = 0u;
        for (std::size_t c = 0u; c < contourCount; ++c) {
            const std::size_t endIdx = endPts[c];
            const std::size_t n = (endIdx >= startIdx) ? (endIdx - startIdx + 1u) : 0u;
            if (n >= 2u) {
                const TtfPoint* ring = pts.data() + startIdx;
                TtfPoint startOn{};
                std::size_t firstStep = 0u;
                std::size_t stepCount = n;

                if (ring[0].onCurve) {
                    startOn = ring[0];
                    firstStep = 1u;
                    stepCount = n; // Visits indices 1 .. n (where n % n == 0 closes back to startOn)
                } else if (ring[n - 1u].onCurve) {
                    startOn = ring[n - 1u];
                    firstStep = 0u;
                    stepCount = n - 1u; // Visits indices 0 .. n-1
                } else {
                    startOn = TtfPoint{
                        0.5f * (ring[n - 1u].x + ring[0].x),
                        0.5f * (ring[n - 1u].y + ring[0].y),
                        true};
                    firstStep = 0u;
                    stepCount = n; // Visits indices 0 .. n-1 and closes to implied startOn
                }

                TtfPoint curOn = startOn;
                std::size_t step = 0u;
                while (step < stepCount) {
                    const std::size_t idx = (firstStep + step) % n;
                    const TtfPoint pt = ring[idx];
                    if (pt.onCurve) {
                        if (std::fabs(pt.x - curOn.x) > 1e-4f || std::fabs(pt.y - curOn.y) > 1e-4f) {
                            outSegments.push_back(TtfContourSegment{
                                curOn.x, curOn.y,
                                0.5f * (curOn.x + pt.x), 0.5f * (curOn.y + pt.y),
                                pt.x, pt.y,
                                false});
                        }
                        curOn = pt;
                        ++step;
                    } else {
                        if (step + 1u < stepCount) {
                            const TtfPoint nextPt = ring[(firstStep + step + 1u) % n];
                            if (nextPt.onCurve) {
                                outSegments.push_back(TtfContourSegment{
                                    curOn.x, curOn.y, pt.x, pt.y, nextPt.x, nextPt.y, true});
                                curOn = nextPt;
                                step += 2u;
                            } else {
                                const TtfPoint mid{
                                    0.5f * (pt.x + nextPt.x),
                                    0.5f * (pt.y + nextPt.y),
                                    true};
                                outSegments.push_back(TtfContourSegment{
                                    curOn.x, curOn.y, pt.x, pt.y, mid.x, mid.y, true});
                                curOn = mid;
                                ++step;
                            }
                        } else {
                            outSegments.push_back(TtfContourSegment{
                                curOn.x, curOn.y, pt.x, pt.y, startOn.x, startOn.y, true});
                            curOn = startOn;
                            ++step;
                        }
                    }
                }
                if (std::fabs(curOn.x - startOn.x) > 1e-4f || std::fabs(curOn.y - startOn.y) > 1e-4f) {
                    outSegments.push_back(TtfContourSegment{
                        curOn.x, curOn.y,
                        0.5f * (curOn.x + startOn.x), 0.5f * (curOn.y + startOn.y),
                        startOn.x, startOn.y,
                        false});
                }
            }
            startIdx = endIdx + 1u;
        }
        return true;
    }

    // Compound TrueType Glyph (numContours < 0)
    constexpr std::uint16_t kArg1And2AreWords = 0x0001u;
    constexpr std::uint16_t kArgsAreXyValues = 0x0002u;
    constexpr std::uint16_t kWeHaveAScale = 0x0008u;
    constexpr std::uint16_t kMoreComponents = 0x0020u;
    constexpr std::uint16_t kWeHaveAnXyScale = 0x0040u;
    constexpr std::uint16_t kWeHaveATwoByTwo = 0x0080u;
    constexpr std::uint16_t kScaledComponentOffset = 0x0800u;

    std::size_t pos = static_cast<std::size_t>(gOff) + 10u;
    std::vector<TtfPoint> compositePoints;
    std::uint16_t compFlags = kMoreComponents;

    while ((compFlags & kMoreComponents) != 0u && pos + 4u <= glyphEnd) {
        std::uint16_t subGlyphIdx = 0u;
        if (!r.ReadU16BE(pos, compFlags) || !r.ReadU16BE(pos + 2u, subGlyphIdx)) {
            return false;
        }
        pos += 4u;

        std::int32_t arg1 = 0;
        std::int32_t arg2 = 0;
        if ((compFlags & kArg1And2AreWords) != 0u) {
            if ((compFlags & kArgsAreXyValues) != 0u) {
                std::int16_t a1 = 0, a2 = 0;
                if (!r.ReadI16BE(pos, a1) || !r.ReadI16BE(pos + 2u, a2)) return false;
                arg1 = a1;
                arg2 = a2;
            } else {
                std::uint16_t u1 = 0u, u2 = 0u;
                if (!r.ReadU16BE(pos, u1) || !r.ReadU16BE(pos + 2u, u2)) return false;
                arg1 = static_cast<std::int32_t>(u1);
                arg2 = static_cast<std::int32_t>(u2);
            }
            pos += 4u;
        } else {
            if ((compFlags & kArgsAreXyValues) != 0u) {
                std::int8_t a1 = 0, a2 = 0;
                if (!r.ReadI8(pos, a1) || !r.ReadI8(pos + 1u, a2)) return false;
                arg1 = a1;
                arg2 = a2;
            } else {
                std::uint8_t u1 = 0u, u2 = 0u;
                if (!r.ReadU8(pos, u1) || !r.ReadU8(pos + 1u, u2)) return false;
                arg1 = static_cast<std::int32_t>(u1);
                arg2 = static_cast<std::int32_t>(u2);
            }
            pos += 2u;
        }

        float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
        if ((compFlags & kWeHaveAScale) != 0u) {
            float s = 1.0f;
            if (!r.ReadF2Dot14BE(pos, s)) return false;
            pos += 2u;
            m00 = s;
            m11 = s;
        } else if ((compFlags & kWeHaveAnXyScale) != 0u) {
            if (!r.ReadF2Dot14BE(pos, m00) || !r.ReadF2Dot14BE(pos + 2u, m11)) return false;
            pos += 4u;
        } else if ((compFlags & kWeHaveATwoByTwo) != 0u) {
            // TrueType 2x2 order: xscale, scale01, scale10, yscale -> x' = m00*x + m01*y, y' = m10*x + m11*y
            if (!r.ReadF2Dot14BE(pos + 0u, m00) ||
                !r.ReadF2Dot14BE(pos + 2u, m10) ||
                !r.ReadF2Dot14BE(pos + 4u, m01) ||
                !r.ReadF2Dot14BE(pos + 6u, m11)) {
                return false;
            }
            pos += 8u;
        }

        std::vector<TtfContourSegment> childSegs;
        std::vector<TtfPoint> childPts;
        if (!ExtractGlyphRecursive(subGlyphIdx, depth + 1u, ioTotalVisitedComponents, childSegs, &childPts)) {
            return false;
        }

        float tx = 0.0f;
        float ty = 0.0f;
        if ((compFlags & kArgsAreXyValues) != 0u) {
            tx = static_cast<float>(arg1);
            ty = static_cast<float>(arg2);
            if ((compFlags & kScaledComponentOffset) != 0u) {
                const float sx = std::sqrt(m00 * m00 + m10 * m10);
                const float sy = std::sqrt(m01 * m01 + m11 * m11);
                tx *= sx;
                ty *= sy;
            }
        } else {
            const auto pIdxParent = static_cast<std::size_t>(arg1);
            const auto pIdxChild = static_cast<std::size_t>(arg2);
            if (pIdxParent < compositePoints.size() && pIdxChild < childPts.size()) {
                const float cx = m00 * childPts[pIdxChild].x + m01 * childPts[pIdxChild].y;
                const float cy = m10 * childPts[pIdxChild].x + m11 * childPts[pIdxChild].y;
                tx = compositePoints[pIdxParent].x - cx;
                ty = compositePoints[pIdxParent].y - cy;
            }
        }

        for (auto& pt : childPts) {
            const float nx = m00 * pt.x + m01 * pt.y + tx;
            const float ny = m10 * pt.x + m11 * pt.y + ty;
            pt.x = nx;
            pt.y = ny;
        }
        compositePoints.insert(compositePoints.end(), childPts.begin(), childPts.end());

        for (auto& seg : childSegs) {
            const float x0 = m00 * seg.p0x + m01 * seg.p0y + tx;
            const float y0 = m10 * seg.p0x + m11 * seg.p0y + ty;
            const float x1 = m00 * seg.p1x + m01 * seg.p1y + tx;
            const float y1 = m10 * seg.p1x + m11 * seg.p1y + ty;
            const float x2 = m00 * seg.p2x + m01 * seg.p2y + tx;
            const float y2 = m10 * seg.p2x + m11 * seg.p2y + ty;
            seg.p0x = x0; seg.p0y = y0;
            seg.p1x = x1; seg.p1y = y1;
            seg.p2x = x2; seg.p2y = y2;
            outSegments.push_back(seg);
        }
    }

    if (outRawPoints) {
        outRawPoints->insert(outRawPoints->end(), compositePoints.begin(), compositePoints.end());
    }
    return true;
}

namespace {

[[nodiscard]] bool ReadCffIndex(
    const TtfReader& r,
    std::size_t indexOffset,
    std::size_t tableEnd,
    std::uint32_t& outEndOffset,
    std::vector<std::pair<std::uint32_t, std::uint32_t>>& outItems) {
    outItems.clear();
    if (indexOffset + 2u > tableEnd) {
        return false;
    }
    std::uint16_t count = 0u;
    if (!r.ReadU16BE(indexOffset, count)) {
        return false;
    }
    if (count == 0u) {
        outEndOffset = static_cast<std::uint32_t>(indexOffset + 2u);
        return true;
    }
    if (indexOffset + 3u > tableEnd) {
        return false;
    }
    std::uint8_t offSize = 0u;
    if (!r.ReadU8(indexOffset + 2u, offSize) || offSize < 1u || offSize > 4u) {
        return false;
    }
    const std::size_t offTableStart = indexOffset + 3u;
    const std::size_t offTableBytes = static_cast<std::size_t>(count + 1u) * static_cast<std::size_t>(offSize);
    if (offTableStart + offTableBytes > tableEnd) {
        return false;
    }
    const std::size_t dataBase = offTableStart + offTableBytes - 1u;
    auto readOff = [&](std::size_t idx, std::uint32_t& outOff) noexcept -> bool {
        const std::size_t p = offTableStart + idx * static_cast<std::size_t>(offSize);
        std::uint32_t val = 0u;
        for (std::uint8_t k = 0u; k < offSize; ++k) {
            std::uint8_t b = 0u;
            if (!r.ReadU8(p + k, b)) return false;
            val = (val << 8u) | static_cast<std::uint32_t>(b);
        }
        outOff = val;
        return true;
    };
    std::uint32_t prevOff = 0u;
    if (!readOff(0u, prevOff) || prevOff == 0u) {
        return false;
    }
    outItems.reserve(count);
    for (std::uint16_t i = 0u; i < count; ++i) {
        std::uint32_t nextOff = 0u;
        if (!readOff(static_cast<std::size_t>(i) + 1u, nextOff) || nextOff < prevOff) {
            return false;
        }
        const std::size_t absStart = dataBase + static_cast<std::size_t>(prevOff);
        const std::size_t len = static_cast<std::size_t>(nextOff - prevOff);
        if (absStart + len > tableEnd) {
            return false;
        }
        outItems.push_back({static_cast<std::uint32_t>(absStart), static_cast<std::uint32_t>(len)});
        prevOff = nextOff;
    }
    outEndOffset = static_cast<std::uint32_t>(dataBase + static_cast<std::size_t>(prevOff));
    return true;
}

} // namespace

bool TrueTypeFontFace::ParseCffTable(std::uint32_t offset, std::uint32_t length, std::string* outError) {
    cffCharStrings_.items.clear();
    cffLocalSubrs_.items.clear();
    cffGlobalSubrs_.items.clear();

    TtfReader r{data_.data(), data_.size()};
    const std::size_t tableStart = offset;
    const std::size_t tableEnd = tableStart + length;
    if (length < 4u || !r.InBounds(tableStart, length)) {
        if (outError) *outError = "Invalid CFF table bounds";
        return false;
    }
    std::uint8_t major = 0u, minor = 0u, hdrSize = 0u;
    if (!r.ReadU8(tableStart, major) || !r.ReadU8(tableStart + 1u, minor) ||
        !r.ReadU8(tableStart + 2u, hdrSize) || major != 1u || hdrSize < 4u ||
        static_cast<std::size_t>(hdrSize) >= length) {
        if (outError) *outError = "Unsupported CFF version/header";
        return false;
    }

    auto toIndexTable = [&](std::size_t idxPos, CffIndexTable& outIdx) -> bool {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> raw;
        if (!ReadCffIndex(r, idxPos, tableEnd, outIdx.endOffset, raw)) {
            return false;
        }
        outIdx.items.reserve(raw.size());
        for (const auto& kv : raw) {
            outIdx.items.push_back(CffIndexTable::Slice{kv.first, kv.second});
        }
        return true;
    };

    CffIndexTable nameIdx{}, topDictIdx{}, strIdx{};
    if (!toIndexTable(tableStart + hdrSize, nameIdx) ||
        !toIndexTable(nameIdx.endOffset, topDictIdx) ||
        topDictIdx.items.empty() ||
        !toIndexTable(topDictIdx.endOffset, strIdx) ||
        !toIndexTable(strIdx.endOffset, cffGlobalSubrs_)) {
        if (outError) *outError = "Truncated CFF top headers";
        return false;
    }

    auto parseDict = [&](std::uint32_t dOff, std::uint32_t dLen,
                         std::int32_t& outCharStringsRel,
                         std::int32_t& outPrivSize,
                         std::int32_t& outPrivRel,
                         std::int32_t& outSubrsRel) {
        std::size_t p = dOff;
        const std::size_t endP = static_cast<std::size_t>(dOff) + dLen;
        std::vector<std::int32_t> stk;
        while (p < endP) {
            std::uint8_t b0 = 0u;
            if (!r.ReadU8(p++, b0)) break;
            if (b0 <= 21u) {
                std::uint16_t op = b0;
                if (b0 == 12u) {
                    std::uint8_t b1 = 0u;
                    if (!r.ReadU8(p++, b1)) break;
                    op = static_cast<std::uint16_t>(0x0C00u | b1);
                }
                if (op == 17u && !stk.empty()) {
                    outCharStringsRel = stk.back();
                } else if (op == 18u && stk.size() >= 2u) {
                    outPrivSize = stk[stk.size() - 2u];
                    outPrivRel = stk.back();
                } else if (op == 19u && !stk.empty()) {
                    outSubrsRel = stk.back();
                }
                stk.clear();
            } else if (b0 == 28u) {
                std::int16_t v = 0;
                if (!r.ReadI16BE(p, v)) break;
                p += 2u;
                stk.push_back(static_cast<std::int32_t>(v));
            } else if (b0 == 29u) {
                std::uint32_t u = 0u;
                if (!r.ReadU32BE(p, u)) break;
                p += 4u;
                stk.push_back(static_cast<std::int32_t>(u));
            } else if (b0 == 30u) {
                while (p < endP) {
                    std::uint8_t nb = 0u;
                    if (!r.ReadU8(p++, nb)) break;
                    if ((nb & 0x0Fu) == 0x0Fu || ((nb >> 4u) & 0x0Fu) == 0x0Fu) break;
                }
                stk.push_back(0);
            } else if (b0 >= 32u && b0 <= 246u) {
                stk.push_back(static_cast<std::int32_t>(b0) - 139);
            } else if (b0 >= 247u && b0 <= 250u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) break;
                stk.push_back((static_cast<std::int32_t>(b0) - 247) * 256 + static_cast<std::int32_t>(b1) + 108);
            } else if (b0 >= 251u && b0 <= 254u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) break;
                stk.push_back(-(static_cast<std::int32_t>(b0) - 251) * 256 - static_cast<std::int32_t>(b1) - 108);
            }
        }
    };

    std::int32_t charStringsRel = -1, privSize = 0, privRel = -1, unusedSubr = -1;
    parseDict(topDictIdx.items[0].offset, topDictIdx.items[0].length,
              charStringsRel, privSize, privRel, unusedSubr);
    if (charStringsRel <= 0 || tableStart + static_cast<std::size_t>(charStringsRel) >= tableEnd) {
        if (outError) *outError = "CFF missing CharStrings INDEX";
        return false;
    }
    if (!toIndexTable(tableStart + static_cast<std::size_t>(charStringsRel), cffCharStrings_) ||
        cffCharStrings_.items.empty()) {
        if (outError) *outError = "Invalid CFF CharStrings INDEX";
        return false;
    }

    if (privRel > 0 && privSize > 0 &&
        tableStart + static_cast<std::size_t>(privRel) + static_cast<std::size_t>(privSize) <= tableEnd) {
        std::int32_t u1 = -1, u2 = 0, u3 = -1, subrsRel = -1;
        parseDict(
            static_cast<std::uint32_t>(tableStart + static_cast<std::size_t>(privRel)),
            static_cast<std::uint32_t>(privSize),
            u1, u2, u3, subrsRel);
        if (subrsRel > 0) {
            const std::size_t subrsPos = tableStart + static_cast<std::size_t>(privRel) + static_cast<std::size_t>(subrsRel);
            if (subrsPos < tableEnd) {
                (void)toIndexTable(subrsPos, cffLocalSubrs_);
            }
        }
    }
    return true;
}

bool TrueTypeFontFace::ExtractCffGlyphOutline(
    std::uint32_t glyphIndex,
    std::vector<TtfContourSegment>& outSegments) const {
    if (glyphIndex >= cffCharStrings_.items.size()) {
        return false;
    }

    TtfReader r{data_.data(), data_.size()};
    std::vector<float> stack;
    stack.reserve(64u);

    float curX = 0.0f, curY = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    bool hasOpenContour = false;
    bool widthChecked = false;
    std::uint32_t numHints = 0u;

    auto closeContour = [&]() {
        if (hasOpenContour) {
            if (std::fabs(curX - startX) > 1e-4f || std::fabs(curY - startY) > 1e-4f) {
                outSegments.push_back(TtfContourSegment{
                    curX, curY,
                    0.5f * (curX + startX), 0.5f * (curY + startY),
                    startX, startY,
                    false});
            }
            hasOpenContour = false;
        }
    };

    auto moveTo = [&](float nx, float ny) {
        closeContour();
        curX = nx;
        curY = ny;
        startX = nx;
        startY = ny;
        hasOpenContour = true;
    };

    auto lineTo = [&](float nx, float ny) {
        if (!hasOpenContour) {
            startX = curX;
            startY = curY;
            hasOpenContour = true;
        }
        outSegments.push_back(TtfContourSegment{
            curX, curY,
            0.5f * (curX + nx), 0.5f * (curY + ny),
            nx, ny,
            false});
        curX = nx;
        curY = ny;
    };

    auto emitCubicSegment = [&](float p0x, float p0y, float c1x, float c1y,
                                float c2x, float c2y, float p3x, float p3y) {
        auto emitSub = [&](float ax, float ay, float bx, float by,
                           float cx, float cy, float dx, float dy) {
            const float qx = 0.75f * (bx + cx) - 0.25f * (ax + dx);
            const float qy = 0.75f * (by + cy) - 0.25f * (ay + dy);
            outSegments.push_back(TtfContourSegment{ax, ay, qx, qy, dx, dy, true});
        };
        auto splitHalf = [](float a0, float a1, float a2, float a3,
                            float& l1, float& l2, float& mid, float& r1, float& r2) {
            const float m01 = 0.5f * (a0 + a1);
            const float m12 = 0.5f * (a1 + a2);
            const float m23 = 0.5f * (a2 + a3);
            l1 = m01;
            l2 = 0.5f * (m01 + m12);
            r2 = m23;
            r1 = 0.5f * (m12 + m23);
            mid = 0.5f * (l2 + r1);
        };
        float xL1, xL2, xM, xR1, xR2, yL1, yL2, yM, yR1, yR2;
        splitHalf(p0x, c1x, c2x, p3x, xL1, xL2, xM, xR1, xR2);
        splitHalf(p0y, c1y, c2y, p3y, yL1, yL2, yM, yR1, yR2);

        float q1L1, q1L2, q1M, q1R1, q1R2, y1L1, y1L2, y1M, y1R1, y1R2;
        splitHalf(p0x, xL1, xL2, xM, q1L1, q1L2, q1M, q1R1, q1R2);
        splitHalf(p0y, yL1, yL2, yM, y1L1, y1L2, y1M, y1R1, y1R2);
        emitSub(p0x, p0y, q1L1, y1L1, q1L2, y1L2, q1M, y1M);
        emitSub(q1M, y1M, q1R1, y1R1, q1R2, y1R2, xM, yM);

        float q2L1, q2L2, q2M, q2R1, q2R2, y2L1, y2L2, y2M, y2R1, y2R2;
        splitHalf(xM, xR1, xR2, p3x, q2L1, q2L2, q2M, q2R1, q2R2);
        splitHalf(yM, yR1, yR2, p3y, y2L1, y2L2, y2M, y2R1, y2R2);
        emitSub(xM, yM, q2L1, y2L1, q2L2, y2L2, q2M, y2M);
        emitSub(q2M, y2M, q2R1, y2R1, q2R2, y2R2, p3x, p3y);
    };

    auto curveTo = [&](float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        if (!hasOpenContour) {
            startX = curX;
            startY = curY;
            hasOpenContour = true;
        }
        const float c1x = curX + dx1;
        const float c1y = curY + dy1;
        const float c2x = c1x + dx2;
        const float c2y = c1y + dy2;
        const float p3x = c2x + dx3;
        const float p3y = c2y + dy3;
        emitCubicSegment(curX, curY, c1x, c1y, c2x, c2y, p3x, p3y);
        curX = p3x;
        curY = p3y;
    };

    auto subrBias = [](std::size_t n) noexcept -> std::int32_t {
        if (n < 1240u) return 107;
        if (n < 33900u) return 1131;
        return 32768;
    };

    auto consumeOptionalWidth = [&](bool hasExtraWidth) {
        if (!widthChecked) {
            widthChecked = true;
            if (hasExtraWidth && !stack.empty()) {
                stack.erase(stack.begin());
            }
        }
    };

    bool ended = false;
    auto runCharstring = [&](auto& self, const CffIndexTable::Slice& slice, std::uint32_t depth) -> bool {
        if (depth > 16u) {
            return false;
        }
        std::size_t p = slice.offset;
        const std::size_t endP = static_cast<std::size_t>(slice.offset) + slice.length;
        while (p < endP && !ended) {
            std::uint8_t b0 = 0u;
            if (!r.ReadU8(p++, b0)) return false;
            if (b0 >= 32u && b0 <= 246u) {
                stack.push_back(static_cast<float>(static_cast<std::int32_t>(b0) - 139));
                continue;
            }
            if (b0 >= 247u && b0 <= 250u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) return false;
                stack.push_back(static_cast<float>((static_cast<std::int32_t>(b0) - 247) * 256 + static_cast<std::int32_t>(b1) + 108));
                continue;
            }
            if (b0 >= 251u && b0 <= 254u) {
                std::uint8_t b1 = 0u;
                if (!r.ReadU8(p++, b1)) return false;
                stack.push_back(static_cast<float>(-(static_cast<std::int32_t>(b0) - 251) * 256 - static_cast<std::int32_t>(b1) - 108));
                continue;
            }
            if (b0 == 28u) {
                std::int16_t v = 0;
                if (!r.ReadI16BE(p, v)) return false;
                p += 2u;
                stack.push_back(static_cast<float>(v));
                continue;
            }
            if (b0 == 255u) {
                std::uint32_t u = 0u;
                if (!r.ReadU32BE(p, u)) return false;
                p += 4u;
                const auto s = static_cast<std::int32_t>(u);
                stack.push_back(static_cast<float>(s) / 65536.0f);
                continue;
            }

            switch (b0) {
                case 1u:  // hstem
                case 3u:  // vstem
                case 18u: // hstemhm
                case 23u: // vstemhm
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    numHints += static_cast<std::uint32_t>(stack.size() / 2u);
                    stack.clear();
                    break;
                case 19u: // hintmask
                case 20u: // cntrmask
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    if (!stack.empty()) {
                        numHints += static_cast<std::uint32_t>(stack.size() / 2u);
                        stack.clear();
                    }
                    p += static_cast<std::size_t>((numHints + 7u) / 8u);
                    if (p > endP) return false;
                    break;
                case 10u: { // callsubr
                    if (stack.empty()) return false;
                    const std::int32_t idx = static_cast<std::int32_t>(std::lround(stack.back())) +
                                             subrBias(cffLocalSubrs_.items.size());
                    stack.pop_back();
                    if (idx < 0 || static_cast<std::size_t>(idx) >= cffLocalSubrs_.items.size()) return false;
                    if (!self(self, cffLocalSubrs_.items[static_cast<std::size_t>(idx)], depth + 1u)) return false;
                    break;
                }
                case 29u: { // callgsubr
                    if (stack.empty()) return false;
                    const std::int32_t idx = static_cast<std::int32_t>(std::lround(stack.back())) +
                                             subrBias(cffGlobalSubrs_.items.size());
                    stack.pop_back();
                    if (idx < 0 || static_cast<std::size_t>(idx) >= cffGlobalSubrs_.items.size()) return false;
                    if (!self(self, cffGlobalSubrs_.items[static_cast<std::size_t>(idx)], depth + 1u)) return false;
                    break;
                }
                case 11u: // return
                    return true;
                case 14u: // endchar
                    consumeOptionalWidth((stack.size() % 2u) == 1u);
                    stack.clear();
                    closeContour();
                    ended = true;
                    return true;
                case 21u: // rmoveto
                    consumeOptionalWidth(stack.size() > 2u);
                    if (stack.size() >= 2u) {
                        moveTo(curX + stack[0], curY + stack[1]);
                    }
                    stack.clear();
                    break;
                case 22u: // hmoveto
                    consumeOptionalWidth(stack.size() > 1u);
                    if (!stack.empty()) {
                        moveTo(curX + stack[0], curY);
                    }
                    stack.clear();
                    break;
                case 4u: // vmoveto
                    consumeOptionalWidth(stack.size() > 1u);
                    if (!stack.empty()) {
                        moveTo(curX, curY + stack[0]);
                    }
                    stack.clear();
                    break;
                case 5u: // rlineto
                    for (std::size_t i = 0u; i + 1u < stack.size(); i += 2u) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                    }
                    stack.clear();
                    break;
                case 6u: { // hlineto
                    bool horiz = true;
                    for (std::size_t i = 0u; i < stack.size(); ++i) {
                        if (horiz) lineTo(curX + stack[i], curY);
                        else lineTo(curX, curY + stack[i]);
                        horiz = !horiz;
                    }
                    stack.clear();
                    break;
                }
                case 7u: { // vlineto
                    bool vert = true;
                    for (std::size_t i = 0u; i < stack.size(); ++i) {
                        if (vert) lineTo(curX, curY + stack[i]);
                        else lineTo(curX + stack[i], curY);
                        vert = !vert;
                    }
                    stack.clear();
                    break;
                }
                case 8u: // rrcurveto
                    for (std::size_t i = 0u; i + 5u < stack.size(); i += 6u) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                    }
                    stack.clear();
                    break;
                case 24u: { // rcurveline
                    std::size_t i = 0u;
                    while (i + 7u < stack.size()) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                        i += 6u;
                    }
                    if (i + 1u < stack.size()) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                    }
                    stack.clear();
                    break;
                }
                case 25u: { // rlinecurve
                    std::size_t i = 0u;
                    while (i + 7u < stack.size()) {
                        lineTo(curX + stack[i], curY + stack[i + 1u]);
                        i += 2u;
                    }
                    if (i + 5u < stack.size()) {
                        curveTo(stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], stack[i + 4u], stack[i + 5u]);
                    }
                    stack.clear();
                    break;
                }
                case 26u: { // vvcurveto
                    std::size_t i = 0u;
                    float dx1 = 0.0f;
                    if ((stack.size() % 4u) == 1u) {
                        dx1 = stack[i++];
                    }
                    while (i + 3u < stack.size()) {
                        curveTo(dx1, stack[i], stack[i + 1u], stack[i + 2u], 0.0f, stack[i + 3u]);
                        dx1 = 0.0f;
                        i += 4u;
                    }
                    stack.clear();
                    break;
                }
                case 27u: { // hhcurveto
                    std::size_t i = 0u;
                    float dy1 = 0.0f;
                    if ((stack.size() % 4u) == 1u) {
                        dy1 = stack[i++];
                    }
                    while (i + 3u < stack.size()) {
                        curveTo(stack[i], dy1, stack[i + 1u], stack[i + 2u], stack[i + 3u], 0.0f);
                        dy1 = 0.0f;
                        i += 4u;
                    }
                    stack.clear();
                    break;
                }
                case 30u:   // vhcurveto
                case 31u: { // hvcurveto
                    bool verticalStart = (b0 == 30u);
                    std::size_t i = 0u;
                    const std::size_t n = stack.size();
                    while (i + 3u < n) {
                        const bool isLast = (i + 4u == n) || (i + 5u == n);
                        const float dLast = (isLast && i + 5u == n) ? stack[i + 4u] : 0.0f;
                        if (verticalStart) {
                            // vhcurveto: dy1 dx2 dy2 dx3 [dyf]
                            curveTo(0.0f, stack[i], stack[i + 1u], stack[i + 2u], stack[i + 3u], dLast);
                        } else {
                            // hvcurveto: dx1 dx2 dy2 dy3 [dxf]
                            curveTo(stack[i], 0.0f, stack[i + 1u], stack[i + 2u], dLast, stack[i + 3u]);
                        }
                        i += (isLast && i + 5u == n) ? 5u : 4u;
                        verticalStart = !verticalStart;
                    }
                    stack.clear();
                    break;
                }
                case 12u: {
                    std::uint8_t b1 = 0u;
                    if (!r.ReadU8(p++, b1)) return false;
                    if (b1 == 34u && stack.size() >= 7u) { // hflex
                        curveTo(stack[0], 0.0f, stack[1], stack[2], stack[3], 0.0f);
                        curveTo(stack[4], 0.0f, stack[5], -stack[2], stack[6], 0.0f);
                    } else if (b1 == 35u && stack.size() >= 13u) { // flex
                        curveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                        curveTo(stack[6], stack[7], stack[8], stack[9], stack[10], stack[11]);
                    } else if (b1 == 36u && stack.size() >= 9u) { // hflex1
                        const float dy1 = stack[1], dy2 = stack[3], dy5 = stack[7];
                        curveTo(stack[0], dy1, stack[2], dy2, stack[4], 0.0f);
                        curveTo(stack[5], 0.0f, stack[6], dy5, stack[8], -(dy1 + dy2 + dy5));
                    } else if (b1 == 37u && stack.size() >= 11u) { // flex1
                        const float dxSum = stack[0] + stack[2] + stack[4] + stack[6] + stack[8];
                        const float dySum = stack[1] + stack[3] + stack[5] + stack[7] + stack[9];
                        const bool dxDom = std::fabs(dxSum) > std::fabs(dySum);
                        curveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                        curveTo(stack[6], stack[7], stack[8], stack[9],
                                dxDom ? stack[10] : -dxSum, dxDom ? -dySum : stack[10]);
                    }
                    stack.clear();
                    break;
                }
                default:
                    stack.clear();
                    break;
            }
        }
        return true;
    };

    const bool ok = runCharstring(runCharstring, cffCharStrings_.items[glyphIndex], 0u);
    closeContour();
    return ok;
}

bool TrueTypeFontFace::ExtractGlyphOutline(
    std::uint32_t glyphIndex,
    TtfGlyphOutline& outOutline) const {
    outOutline = TtfGlyphOutline{};
    outOutline.glyphIndex = glyphIndex;
    if (!valid_ || glyphIndex >= numGlyphs_) {
        return false;
    }

    GetGlyphHorizontalMetrics(glyphIndex, outOutline.advanceWidth, outOutline.leftSideBearing);

    std::uint32_t totalVisitedComponents = 0u;
    const bool extracted = isCff_
        ? ExtractCffGlyphOutline(glyphIndex, outOutline.segments)
        : ExtractGlyphRecursive(glyphIndex, 0u, totalVisitedComponents, outOutline.segments, nullptr);
    if (!extracted) {
        return false;
    }

    if (outOutline.segments.empty()) {
        outOutline.isEmptyOutline = true;
        return true;
    }

    outOutline.isEmptyOutline = false;
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    auto expandBounds = [&](float x, float y) noexcept {
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };
    for (const auto& s : outOutline.segments) {
        expandBounds(s.p0x, s.p0y);
        expandBounds(s.p2x, s.p2y);
        if (s.isQuadratic) {
            // Check exact quadratic Bezier X/Y extrema in (0, 1) rather than over-expanding to control hull
            const float dxDenom = s.p0x - 2.0f * s.p1x + s.p2x;
            if (std::fabs(dxDenom) > 1e-5f) {
                const float tx = (s.p0x - s.p1x) / dxDenom;
                if (tx > 0.0f && tx < 1.0f) {
                    const float u = 1.0f - tx;
                    expandBounds(u * u * s.p0x + 2.0f * u * tx * s.p1x + tx * tx * s.p2x, s.p0y);
                }
            }
            const float dyDenom = s.p0y - 2.0f * s.p1y + s.p2y;
            if (std::fabs(dyDenom) > 1e-5f) {
                const float ty = (s.p0y - s.p1y) / dyDenom;
                if (ty > 0.0f && ty < 1.0f) {
                    const float u = 1.0f - ty;
                    expandBounds(s.p0x, u * u * s.p0y + 2.0f * u * ty * s.p1y + ty * ty * s.p2y);
                }
            }
        }
    }
    outOutline.xMin = minX;
    outOutline.yMin = minY;
    outOutline.xMax = std::max(minX + 1.0f, maxX);
    outOutline.yMax = std::max(minY + 1.0f, maxY);
    return true;
}

// ============================================================================
// FontAtlas Implementation
// ============================================================================

namespace {

void EnsureFontAtlasRegisteredWithStore() noexcept {
    UIResourceStore::RegisterFontAtlasHooks(
        [](std::uint16_t size) { return std::make_shared<FontAtlas>(size); },
        [](FontAtlas& atlas) noexcept { atlas.ClearCustomFonts(); });
}

const bool gFontAtlasStoreHooksInit = []() noexcept {
    EnsureFontAtlasRegisteredWithStore();
    return true;
}();

} // namespace

FontAtlas& FontAtlas::Instance() noexcept {
    EnsureFontAtlasRegisteredWithStore();
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
        sdfPixels_.assign(static_cast<std::size_t>(atlasWidth_) * atlasHeight_, 0u);
    }
    for (std::uint16_t r = 0u; r < cellH; ++r) {
        if (dstY + r >= atlasHeight_) {
            break;
        }
        const std::size_t copyW = std::min<std::size_t>(
            cellW,
            atlasWidth_ - std::min<std::uint16_t>(dstX, atlasWidth_));
        std::memcpy(
            sdfPixels_.data() + static_cast<std::size_t>(dstY + r) * atlasWidth_ + dstX,
            srcPixels + static_cast<std::size_t>(r) * cellW,
            copyW);
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
    revision_.fetch_add(1u, std::memory_order_release);
    InitBuiltInFallbackAscii();
    builtInAsciiReady_.store(true, std::memory_order_release);
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

    std::vector<YMonotonicSeg> rowWindSegs;
    rowWindSegs.reserve(windSegs.size());
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
        for (std::uint16_t px = 0u; px < cellW; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            const bool inside = !rowWindSegs.empty() && (EvaluateWindingNumber(rowWindSegs, sx, sy) != 0);

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
        if (!parsed->LoadFromMemory(data, size, outError)) {
            return false;
        }
        face = std::move(parsed);
        if (blobKey != 0u) {
            auto& fc = ProcessFontFaceCache::Instance();
            std::lock_guard<std::mutex> g(fc.mtx);
            if (fc.faces.size() >= 64u) fc.faces.clear();
            fc.faces.emplace(blobKey, face);
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
    return LoadTrueTypeFontFromMemory(buf.data(), buf.size(), derivedAlias, setAsDefault, outError);
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
        if (entry && entry->face && entry->face->IsValid()) {
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
                        if (pgc.entries.size() >= 8192u) pgc.entries.clear();
                        pgc.entries.emplace(procGlyphKey, ProcessBakedGlyphEntry{meta, cellPixels});
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

TextLayoutResult FontAtlas::MeasureLayout(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing,
    int fontWeight,
    bool fontItalic,
    float explicitLineHeight,
    bool baseRtl) {
    RichTextRunInput singleRun{};
    singleRun.text = utf8Text;
    singleRun.fontFamily = fontFamily;
    singleRun.fontSize = fontSize;
    singleRun.fontWeight = (fontWeight > 0) ? fontWeight : 400;
    singleRun.fontItalic = fontItalic ? 1 : 0;
    std::vector<RichTextRunInput> runs;
    if (!utf8Text.empty()) {
        runs.push_back(singleRun);
    }
    return MeasureRichLayout(
        runs,
        fontFamily,
        fontSize,
        (fontWeight > 0) ? fontWeight : 400,
        fontItalic,
        maxWidth,
        wrapMode,
        charSpacing,
        explicitLineHeight,
        baseRtl);
}

namespace {

enum class ArabicJoinType : std::uint8_t {
    NonJoining = 0,
    RightJoining = 1,
    DualJoining = 2,
    JoinCausing = 3,
    Transparent = 4,
};

ArabicJoinType ClassifyArabicJoining(std::uint32_t cp) noexcept {
    if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp == 0x0670u || (cp >= 0x06D6u && cp <= 0x06EDu)) {
        return ArabicJoinType::Transparent;
    }
    if (cp == 0x0640u || cp == 0x200Du) return ArabicJoinType::JoinCausing;
    switch (cp) {
        case 0x0622u: case 0x0623u: case 0x0624u: case 0x0625u: case 0x0627u:
        case 0x0629u: case 0x062Fu: case 0x0630u: case 0x0631u: case 0x0632u:
        case 0x0648u: case 0x0671u:
            return ArabicJoinType::RightJoining;
        default:
            break;
    }
    if ((cp >= 0x0626u && cp <= 0x063Au) || (cp >= 0x0641u && cp <= 0x064Au) ||
        cp == 0x067Eu || cp == 0x0686u || cp == 0x0698u || cp == 0x06A9u ||
        cp == 0x06AFu || cp == 0x06CCu) {
        if (cp == 0x0621u) return ArabicJoinType::NonJoining;
        // Note: U+0649 (Alef Maksura) is DualJoining in Unicode/GSUB or RightJoining when final; in Arabic it only occurs final unless Kashmiri, but check:
        if (cp == 0x0649u) return ArabicJoinType::RightJoining;
        return ArabicJoinType::DualJoining;
    }
    return ArabicJoinType::NonJoining;
}

bool JoinsToLeft(ArabicJoinType jt) noexcept {
    return jt == ArabicJoinType::DualJoining || jt == ArabicJoinType::JoinCausing;
}
bool JoinsToRight(ArabicJoinType jt) noexcept {
    return jt == ArabicJoinType::RightJoining || jt == ArabicJoinType::DualJoining || jt == ArabicJoinType::JoinCausing;
}

std::uint32_t FallbackArabicPresentationFormB(std::uint32_t cp, int form /*0=isol,1=init,2=medi,3=fina*/) noexcept {
    struct Row { std::uint32_t base, isol, fina, init, medi; };
    static constexpr Row kRows[] = {
        {0x0621, 0xFE80, 0xFE80, 0xFE80, 0xFE80},
        {0x0622, 0xFE81, 0xFE82, 0xFE81, 0xFE82},
        {0x0623, 0xFE83, 0xFE84, 0xFE83, 0xFE84},
        {0x0624, 0xFE85, 0xFE86, 0xFE85, 0xFE86},
        {0x0625, 0xFE87, 0xFE88, 0xFE87, 0xFE88},
        {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},
        {0x0627, 0xFE8D, 0xFE8E, 0xFE8D, 0xFE8E},
        {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},
        {0x0629, 0xFE93, 0xFE94, 0xFE93, 0xFE94},
        {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},
        {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},
        {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},
        {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
        {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},
        {0x062F, 0xFEA9, 0xFEAA, 0xFEA9, 0xFEAA},
        {0x0630, 0xFEAB, 0xFEAC, 0xFEAB, 0xFEAC},
        {0x0631, 0xFEAD, 0xFEAE, 0xFEAD, 0xFEAE},
        {0x0632, 0xFEAF, 0xFEB0, 0xFEAF, 0xFEB0},
        {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
        {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},
        {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},
        {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},
        {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},
        {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},
        {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
        {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0},
        {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4},
        {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
        {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},
        {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},
        {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
        {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
        {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},
        {0x0648, 0xFEED, 0xFEEE, 0xFEED, 0xFEEE},
        {0x0649, 0xFEEF, 0xFEF0, 0xFEEF, 0xFEF0},
        {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
    };
    for (const auto& r : kRows) {
        if (r.base == cp) {
            if (form == 1) return r.init;
            if (form == 2) return r.medi;
            if (form == 3) return r.fina;
            return r.isol;
        }
    }
    return cp;
}

enum class BidiClass : std::uint8_t {
    L = 0, R = 1, AL = 2, EN = 3, AN = 4, ES = 5, ET = 6, CS = 7, NSM = 8, WS = 9, ON = 10
};

BidiClass ClassifyBidi(std::uint32_t cp) noexcept {
    if ((cp >= 0x0600u && cp <= 0x06FFu) || (cp >= 0x0750u && cp <= 0x077Fu) ||
        (cp >= 0x08A0u && cp <= 0x08FFu) || (cp >= 0xFB50u && cp <= 0xFDFFu) ||
        (cp >= 0xFE70u && cp <= 0xFEFFu)) {
        if (cp >= 0x0660u && cp <= 0x0669u) return BidiClass::AN;
        if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp == 0x0670u) return BidiClass::NSM;
        if (cp == 0x060Cu) return BidiClass::CS;
        return BidiClass::AL;
    }
    if (cp >= 0x0590u && cp <= 0x05FFu) return BidiClass::R;
    if (cp >= '0' && cp <= '9') return BidiClass::EN;
    if (cp == '+' || cp == '-') return BidiClass::ES;
    if (cp == '%' || cp == '#' || cp == '$') return BidiClass::ET;
    if (cp == ',' || cp == '.' || cp == ':' || cp == '/') return BidiClass::CS;
    if (cp == ' ' || cp == '\t') return BidiClass::WS;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0x00C0u && cp <= 0x02AFu) || (cp >= 0x3040u && cp <= 0x9FFFu)) {
        return BidiClass::L;
    }
    return BidiClass::ON;
}

std::uint32_t MirrorBracketCodePoint(std::uint32_t cp) noexcept {
    switch (cp) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        default: return cp;
    }
}

void ComputeUax9LineVisualOrder(
    const std::vector<std::uint32_t>& lineCps,
    bool paragraphRtl,
    std::vector<std::size_t>& outOrder,
    std::vector<std::uint8_t>& outLevels) {
    const std::size_t n = lineCps.size();
    outOrder.resize(n);
    for (std::size_t i = 0u; i < n; ++i) outOrder[i] = i;
    const std::uint8_t baseLevel = paragraphRtl ? 1u : 0u;
    outLevels.assign(n, baseLevel);
    if (n == 0u) return;

    std::vector<BidiClass> cls(n);
    std::vector<bool> isNsm(n, false);
    bool hasRtlOrNum = paragraphRtl;
    for (std::size_t i = 0u; i < n; ++i) {
        cls[i] = ClassifyBidi(lineCps[i]);
        isNsm[i] = (cls[i] == BidiClass::NSM);
        if (cls[i] == BidiClass::R || cls[i] == BidiClass::AL) hasRtlOrNum = true;
    }
    if (!hasRtlOrNum) return;

    const BidiClass sos = paragraphRtl ? BidiClass::R : BidiClass::L;
    const BidiClass eos = sos;

    // W1: NSM -> preceding class
    BidiClass prev = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::NSM) cls[i] = prev;
        else prev = cls[i];
    }
    // W2: EN -> AN if last strong was AL
    BidiClass lastStrong = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::L || cls[i] == BidiClass::R || cls[i] == BidiClass::AL) lastStrong = cls[i];
        else if (cls[i] == BidiClass::EN && lastStrong == BidiClass::AL) cls[i] = BidiClass::AN;
    }
    // W3: AL -> R
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::AL) cls[i] = BidiClass::R;
    }
    // W4: ES/CS between numbers
    for (std::size_t i = 1u; i + 1u < n; ++i) {
        if (cls[i] == BidiClass::ES && cls[i - 1u] == BidiClass::EN && cls[i + 1u] == BidiClass::EN) {
            cls[i] = BidiClass::EN;
        } else if (cls[i] == BidiClass::CS) {
            if (cls[i - 1u] == BidiClass::EN && cls[i + 1u] == BidiClass::EN) cls[i] = BidiClass::EN;
            else if (cls[i - 1u] == BidiClass::AN && cls[i + 1u] == BidiClass::AN) cls[i] = BidiClass::AN;
        }
    }
    // W5: ET adjacent to EN -> EN
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] != BidiClass::ET) continue;
        std::size_t j = i;
        while (j + 1u < n && cls[j + 1u] == BidiClass::ET) ++j;
        const bool adjEn = (i > 0u && cls[i - 1u] == BidiClass::EN) || (j + 1u < n && cls[j + 1u] == BidiClass::EN);
        if (adjEn) {
            for (std::size_t k = i; k <= j; ++k) cls[k] = BidiClass::EN;
        }
        i = j;
    }
    // W6: remaining ES, ET, CS -> ON
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::ES || cls[i] == BidiClass::ET || cls[i] == BidiClass::CS) cls[i] = BidiClass::ON;
    }
    // W7: EN -> L if last strong was L
    lastStrong = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::L || cls[i] == BidiClass::R) lastStrong = cls[i];
        else if (cls[i] == BidiClass::EN && lastStrong == BidiClass::L) cls[i] = BidiClass::L;
    }
    // N1 & N2: resolve neutral/whitespace sequences
    auto strongForNeutral = [](BidiClass c) noexcept -> BidiClass {
        if (c == BidiClass::EN || c == BidiClass::AN) return BidiClass::R;
        return c;
    };
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] != BidiClass::ON && cls[i] != BidiClass::WS) continue;
        std::size_t j = i;
        while (j + 1u < n && (cls[j + 1u] == BidiClass::ON || cls[j + 1u] == BidiClass::WS)) ++j;
        const BidiClass leftDir = (i > 0u) ? strongForNeutral(cls[i - 1u]) : sos;
        const BidiClass rightDir = (j + 1u < n) ? strongForNeutral(cls[j + 1u]) : eos;
        const BidiClass resolved = (leftDir == rightDir) ? leftDir : (paragraphRtl ? BidiClass::R : BidiClass::L);
        for (std::size_t k = i; k <= j; ++k) cls[k] = resolved;
        i = j;
    }
    // I1 / I2: implicit levels
    for (std::size_t i = 0u; i < n; ++i) {
        if ((baseLevel & 1u) == 0u) {
            if (cls[i] == BidiClass::R) outLevels[i] = baseLevel + 1u;
            else if (cls[i] == BidiClass::EN || cls[i] == BidiClass::AN) outLevels[i] = baseLevel + 2u;
        } else {
            if (cls[i] == BidiClass::L || cls[i] == BidiClass::EN || cls[i] == BidiClass::AN) {
                outLevels[i] = baseLevel + 1u;
            }
        }
    }
    // L1: trailing whitespace on line resets to paragraph embedding level
    for (std::size_t k = n; k > 0u; --k) {
        const std::uint32_t cp = lineCps[k - 1u];
        if (cp == ' ' || cp == '\t') outLevels[k - 1u] = baseLevel;
        else break;
    }
    // Preserve base-before-NSM cluster ordering inside odd-level runs so L2 reversal leaves
    // combining marks logically after (to the right of / attached to) their base character.
    for (std::size_t i = 0u; i < n; ) {
        if ((outLevels[outOrder[i]] & 1u) != 0u && !isNsm[outOrder[i]]) {
            std::size_t j = i;
            while (j + 1u < n && isNsm[outOrder[j + 1u]] &&
                   outLevels[outOrder[j + 1u]] == outLevels[outOrder[i]]) {
                ++j;
            }
            if (j > i) {
                std::reverse(outOrder.begin() + static_cast<std::ptrdiff_t>(i),
                             outOrder.begin() + static_cast<std::ptrdiff_t>(j + 1u));
            }
            i = j + 1u;
        } else {
            ++i;
        }
    }
    // L2: reverse contiguous sequences from maxLevel down to minOddLevel
    std::uint8_t maxLev = 0u;
    std::uint8_t minOdd = 255u;
    for (std::uint8_t lv : outLevels) {
        maxLev = std::max(maxLev, lv);
        if ((lv & 1u) != 0u) minOdd = std::min(minOdd, lv);
    }
    if (minOdd <= maxLev) {
        for (int lv = static_cast<int>(maxLev); lv >= static_cast<int>(minOdd); --lv) {
            std::size_t i = 0u;
            while (i < n) {
                if (outLevels[outOrder[i]] < static_cast<std::uint8_t>(lv)) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j + 1u < n && outLevels[outOrder[j + 1u]] >= static_cast<std::uint8_t>(lv)) ++j;
                std::reverse(outOrder.begin() + static_cast<std::ptrdiff_t>(i),
                             outOrder.begin() + static_cast<std::ptrdiff_t>(j + 1u));
                i = j + 1u;
            }
        }
    }
}

} // namespace

TextLayoutResult FontAtlas::MeasureRichLayout(
    const std::vector<RichTextRunInput>& runs,
    std::string_view defaultFontFamily,
    float defaultFontSize,
    int defaultFontWeight,
    bool defaultItalic,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing,
    float explicitLineHeight,
    bool baseRtl) {
    TextLayoutResult result{};
    result.baseRtl = baseRtl;
    const float baseFont = std::max(1.0f, defaultFontSize);
    const int baseWeight = (defaultFontWeight > 0) ? defaultFontWeight : 400;
    const float validExplicitLineH =
        (std::isfinite(explicitLineHeight) && explicitLineHeight > 0.0f)
            ? explicitLineHeight
            : 0.0f;

    std::unique_lock<std::mutex> lock(mutex_);
    std::vector<const RegisteredFontEntry*> defaultChain;
    ResolveFontChainLocked(defaultFontFamily, baseWeight, defaultItalic, defaultChain);

    const RegisteredFontEntry* primaryEntry = defaultChain.empty() ? nullptr : defaultChain.front();
    std::shared_ptr<TrueTypeFontFace> primaryFace =
        (primaryEntry && primaryEntry->face && primaryEntry->face->IsValid())
            ? primaryEntry->face
            : nullptr;

    const bool hasDefaultCustomFont = (primaryEntry != nullptr);
    result.usedTrueTypeMetrics = hasDefaultCustomFont;

    if (primaryFace) {
        result.ascent = primaryFace->AscentEm() * baseFont;
        result.descent = primaryFace->DescentEm() * baseFont;
        result.lineGap = primaryFace->LineGapEm() * baseFont;
    } else if (primaryEntry && primaryEntry->isShardOnly) {
        result.ascent = primaryEntry->shardAscentEm * baseFont;
        result.descent = primaryEntry->shardDescentEm * baseFont;
        result.lineGap = primaryEntry->shardLineGapEm * baseFont;
    } else {
        // Built-in fallback vertical metrics preserving 100% existing unit-test layout invariants
        result.ascent = baseFont * 0.95f;
        result.descent = baseFont * 0.25f;
        result.lineGap = 0.0f;
    }
    const float natBaseLineH = std::max(1.0f, result.ascent + result.descent + result.lineGap);
    result.lineHeight = (validExplicitLineH > 0.0f)
        ? std::max(natBaseLineH, validExplicitLineH)
        : natBaseLineH;

    if (runs.empty()) {
        return result;
    }

    struct TokenGlyph {
        std::uint32_t cp = 0u;
        std::uint32_t sourceCodePointIndex = 0u;
        FontAtlasGlyph meta{};
        FontAtlasGlyph mirroredMeta{};
        const TrueTypeFontFace* kernFace = nullptr;
        std::uint32_t fontId = 0u;
        float fontSize = 14.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        float upemInvScale = 0.0f;
        std::uint32_t customRgba = 0u;
        bool hasCustomRgba = false;
        bool hasCustomFont = false;
        bool hasMirroredMeta = false;
        bool syntheticBold = false;
        bool syntheticItalic = false;
        bool underline = false;
        bool strikethrough = false;
        bool explicitNewline = false;
    };

    std::vector<TokenGlyph> decoded;
    std::vector<std::shared_ptr<TrueTypeFontFace>> keepAliveFaces;
    if (primaryFace) {
        keepAliveFaces.push_back(primaryFace);
    }

    auto retainFace = [&](const std::shared_ptr<TrueTypeFontFace>& sp) {
        if (!sp) return;
        for (const auto& existing : keepAliveFaces) {
            if (existing.get() == sp.get()) return;
        }
        keepAliveFaces.push_back(sp);
    };

    std::vector<const RegisteredFontEntry*> runChain;
    std::uint32_t rawCodePointIdx = 0u;
    for (const RichTextRunInput& run : runs) {
        if (run.text.empty()) {
            continue;
        }
        const float runFontSize = (run.fontSize > 0.0f && std::isfinite(run.fontSize))
            ? std::max(1.0f, run.fontSize)
            : baseFont;
        const int runWeight = (run.fontWeight > 0) ? run.fontWeight : baseWeight;
        const bool runItalic = (run.fontItalic >= 0) ? (run.fontItalic != 0) : defaultItalic;
        const std::string_view runFamily = !run.fontFamily.empty() ? run.fontFamily : defaultFontFamily;

        ResolveFontChainLocked(runFamily, runWeight, runItalic, runChain);
        const bool wantBold = (runWeight >= 600);
        const bool runHasCustom = !runChain.empty();
        if (runHasCustom) {
            result.usedTrueTypeMetrics = true;
        }

        const RegisteredFontEntry* runPrimary = runChain.empty() ? nullptr : runChain.front();
        float runAscent = result.ascent;
        float runDescent = result.descent;
        float runLineGap = result.lineGap;
        if (runPrimary && runPrimary->face && runPrimary->face->IsValid()) {
            runAscent = runPrimary->face->AscentEm() * runFontSize;
            runDescent = runPrimary->face->DescentEm() * runFontSize;
            runLineGap = runPrimary->face->LineGapEm() * runFontSize;
            retainFace(runPrimary->face);
        } else if (runPrimary && runPrimary->isShardOnly) {
            runAscent = runPrimary->shardAscentEm * runFontSize;
            runDescent = runPrimary->shardDescentEm * runFontSize;
            runLineGap = runPrimary->shardLineGapEm * runFontSize;
        } else if (!hasDefaultCustomFont) {
            runAscent = runFontSize * 0.95f;
            runDescent = runFontSize * 0.25f;
            runLineGap = 0.0f;
        }

        std::size_t cursor = 0u;
        std::uint32_t cp = 0u;
        while (DecodeNextUtf8CodePoint(run.text, cursor, cp)) {
            const std::uint32_t curCpIdx = rawCodePointIdx++;
            if (cp == '\r') {
                continue;
            }
            if (cp == '\n') {
                TokenGlyph nl{};
                nl.cp = '\n';
                nl.sourceCodePointIndex = curCpIdx;
                nl.fontSize = runFontSize;
                nl.ascent = runAscent;
                nl.descent = runDescent;
                nl.lineGap = runLineGap;
                nl.explicitNewline = true;
                decoded.push_back(nl);
                continue;
            }
            const RegisteredFontEntry* chosen = nullptr;
            TokenGlyph tg{};
            tg.cp = cp;
            tg.sourceCodePointIndex = curCpIdx;
            tg.meta = EnsureGlyphFromChainLocked(runChain, cp, &chosen);
            const std::uint32_t mcp = MirrorBracketCodePoint(cp);
            if (mcp != cp) {
                tg.mirroredMeta = EnsureGlyphFromChainLocked(runChain, mcp, nullptr);
                tg.hasMirroredMeta = tg.mirroredMeta.valid;
            }
            tg.fontSize = runFontSize;
            tg.ascent = runAscent;
            tg.descent = runDescent;
            tg.lineGap = runLineGap;
            tg.customRgba = run.fgRgba;
            tg.hasCustomRgba = run.hasFg;
            tg.underline = (run.decorations & 1) != 0;
            tg.strikethrough = (run.decorations & 2) != 0;
            tg.hasCustomFont = (chosen != nullptr);
            if (chosen) {
                tg.fontId = chosen->fontId;
                tg.syntheticBold = wantBold && (chosen->weightClass < 600);
                tg.syntheticItalic = runItalic && !chosen->isItalic;
                if (chosen->face && chosen->face->IsValid()) {
                    retainFace(chosen->face);
                    tg.kernFace = chosen->face.get();
                    tg.upemInvScale = runFontSize / static_cast<float>(chosen->face->UnitsPerEm());
                }
            } else {
                tg.syntheticBold = wantBold;
                tg.syntheticItalic = runItalic;
            }
            decoded.push_back(tg);
        }
    }

    // Arabic contextual joining + GSUB / Presentation Forms-B shaping across decoded tokens
    bool anyArabic = false;
    for (const TokenGlyph& tg : decoded) {
        if (!tg.explicitNewline && tg.cp >= 0x0600u && tg.cp <= 0x06FFu) {
            anyArabic = true;
            break;
        }
    }
    if (anyArabic) {
        const std::size_t dn = decoded.size();
        std::vector<ArabicJoinType> jtypes(dn, ArabicJoinType::NonJoining);
        for (std::size_t i = 0u; i < dn; ++i) {
            if (!decoded[i].explicitNewline) jtypes[i] = ClassifyArabicJoining(decoded[i].cp);
        }
        auto prevJoinType = [&](std::size_t idx) noexcept -> ArabicJoinType {
            for (std::size_t k = idx; k > 0u; --k) {
                if (decoded[k - 1u].explicitNewline) return ArabicJoinType::NonJoining;
                if (jtypes[k - 1u] != ArabicJoinType::Transparent) return jtypes[k - 1u];
            }
            return ArabicJoinType::NonJoining;
        };
        auto nextJoinType = [&](std::size_t idx) noexcept -> ArabicJoinType {
            for (std::size_t k = idx + 1u; k < dn; ++k) {
                if (decoded[k].explicitNewline) return ArabicJoinType::NonJoining;
                if (jtypes[k] != ArabicJoinType::Transparent) return jtypes[k];
            }
            return ArabicJoinType::NonJoining;
        };
        auto findFontById = [&](std::uint32_t fid) -> const RegisteredFontEntry* {
            for (const auto& f : fonts_) {
                if (f.fontId == fid) return &f;
            }
            return nullptr;
        };

        std::vector<std::uint32_t> shapedGids(dn, 0u);
        for (std::size_t i = 0u; i < dn; ++i) {
            TokenGlyph& tg = decoded[i];
            if (tg.explicitNewline || tg.cp < 0x0600u || tg.cp > 0x06FFu) continue;
            const ArabicJoinType curJ = jtypes[i];
            if (curJ == ArabicJoinType::NonJoining || curJ == ArabicJoinType::Transparent || curJ == ArabicJoinType::JoinCausing) {
                shapedGids[i] = tg.meta.glyphIndex;
                continue;
            }
            const bool joinPrev = JoinsToLeft(prevJoinType(i)) && JoinsToRight(curJ);
            const bool joinNext = JoinsToLeft(curJ) && JoinsToRight(nextJoinType(i));
            int form = 0; // 0=isol, 1=init, 2=medi, 3=fina
            if (joinPrev && joinNext) form = 2;
            else if (joinPrev) form = 3;
            else if (joinNext) form = 1;

            const RegisteredFontEntry* fe = findFontById(tg.fontId);
            if (fe && fe->face && fe->face->HasArabicGsub()) {
                static constexpr std::uint32_t kFeatTags[4] = {
                    0x69736F6Cu, // isol
                    0x696E6974u, // init
                    0x6D656469u, // medi
                    0x66696E61u  // fina
                };
                const std::uint32_t baseGid = fe->face->FindGlyphIndex(tg.cp);
                const std::uint32_t subGid = fe->face->ApplyArabicGsubFeature(kFeatTags[form], baseGid);
                shapedGids[i] = subGid;
                if (subGid != baseGid && subGid != 0u) {
                    tg.meta = EnsureGlyphByIndexLocked(*fe, subGid, tg.cp);
                }
            } else if (fe && fe->face) {
                const std::uint32_t presCp = FallbackArabicPresentationFormB(tg.cp, form);
                if (presCp != tg.cp && fe->face->FindGlyphIndex(presCp) != 0u) {
                    tg.meta = EnsureGlyphLocked(fe, presCp);
                    shapedGids[i] = tg.meta.glyphIndex;
                }
            }
        }

        // Apply Lam-Alef & GSUB 2-glyph ligatures ('liga' / 'rlig')
        std::vector<TokenGlyph> compacted;
        compacted.reserve(dn);
        for (std::size_t i = 0u; i < dn; ++i) {
            if (i + 1u < dn && !decoded[i].explicitNewline && !decoded[i + 1u].explicitNewline &&
                decoded[i].fontId == decoded[i + 1u].fontId && decoded[i].fontId != 0u) {
                const RegisteredFontEntry* fe = findFontById(decoded[i].fontId);
                if (fe && fe->face && fe->face->HasArabicGsub() && shapedGids[i] != 0u && shapedGids[i + 1u] != 0u) {
                    const std::uint32_t ligGid = fe->face->ApplyArabicGsubLigature(shapedGids[i], shapedGids[i + 1u]);
                    if (ligGid != 0u) {
                        TokenGlyph ligTg = decoded[i];
                        ligTg.meta = EnsureGlyphByIndexLocked(*fe, ligGid, decoded[i].cp);
                        compacted.push_back(ligTg);
                        ++i;
                        continue;
                    }
                }
                if (decoded[i].cp == 0x0644u &&
                    (decoded[i + 1u].cp == 0x0622u || decoded[i + 1u].cp == 0x0623u ||
                     decoded[i + 1u].cp == 0x0625u || decoded[i + 1u].cp == 0x0627u) &&
                    fe && fe->face) {
                    const bool joinPrev = JoinsToLeft(prevJoinType(i));
                    std::uint32_t lamAlefCp = 0u;
                    switch (decoded[i + 1u].cp) {
                        case 0x0622u: lamAlefCp = joinPrev ? 0xFEF6u : 0xFEF5u; break;
                        case 0x0623u: lamAlefCp = joinPrev ? 0xFEF8u : 0xFEF7u; break;
                        case 0x0625u: lamAlefCp = joinPrev ? 0xFEFAu : 0xFEF9u; break;
                        case 0x0627u: lamAlefCp = joinPrev ? 0xFEFCu : 0xFEFBu; break;
                        default: break;
                    }
                    if (lamAlefCp != 0u && fe->face->FindGlyphIndex(lamAlefCp) != 0u) {
                        TokenGlyph ligTg = decoded[i];
                        ligTg.meta = EnsureGlyphLocked(fe, lamAlefCp);
                        compacted.push_back(ligTg);
                        ++i;
                        continue;
                    }
                }
            }
            compacted.push_back(decoded[i]);
        }
        decoded = std::move(compacted);
    }

    lock.unlock();

    if (decoded.empty()) {
        return result;
    }

    const bool wrapEnabled = (wrapMode != TextWrapping::NoWrap) &&
        std::isfinite(maxWidth) && (maxWidth > 0.0f) && (maxWidth < 500000.0f);
    // Small float tolerance prevents re-wrapping an auto-sized TextBlock arranged to its own DesiredSize.width
    const float wrapLimit = maxWidth + 0.05f;

    result.glyphs.reserve(decoded.size());

    std::uint32_t curLineIdx = 0u;
    float curPenX = 0.0f;
    float curTopY = 0.0f;
    std::size_t lineStartGlyphIdx = 0u;
    std::size_t lastBreakDecodedIdx = static_cast<std::size_t>(-1);
    std::size_t lastBreakGlyphOutIdx = 0u;
    std::uint32_t prevGlyphIdOnLine = 0xFFFFFFFFu;
    std::uint32_t prevFontIdOnLine = 0u;
    std::vector<std::size_t> lineTokenIndices;
    lineTokenIndices.reserve(64u);

    auto finishLine = [&](std::size_t endGlyphIdx, bool endedByNewline) {
        float lineAscent = result.ascent;
        float lineDescent = result.descent;
        float lineGap = result.lineGap;
        if (endGlyphIdx > lineStartGlyphIdx) {
            lineAscent = 0.0f;
            lineDescent = 0.0f;
            lineGap = 0.0f;
            for (std::size_t k = lineStartGlyphIdx; k < endGlyphIdx; ++k) {
                const TokenGlyph& ltg = decoded[lineTokenIndices[k]];
                lineAscent = std::max(lineAscent, ltg.ascent);
                lineDescent = std::max(lineDescent, ltg.descent);
                lineGap = std::max(lineGap, ltg.lineGap);
            }
        }
        const float natLineH = std::max(1.0f, lineAscent + lineDescent + lineGap);
        const float effectiveLineH = (validExplicitLineH > 0.0f)
            ? std::max(natLineH, validExplicitLineH)
            : natLineH;
        const float extraLeading = std::max(0.0f, effectiveLineH - natLineH);
        const float lineBaselineY = curTopY + lineAscent + extraLeading * 0.5f;

        if ((baseRtl || anyArabic) && endGlyphIdx > lineStartGlyphIdx) {
            // Trim trailing wrap spaces before BiDi visual reordering so right edge stays flush
            std::size_t visEnd = endGlyphIdx;
            if (wrapEnabled) {
                while (visEnd > lineStartGlyphIdx && result.glyphs[visEnd - 1u].isWhitespace) {
                    --visEnd;
                }
            }
            const std::size_t visCnt = visEnd - lineStartGlyphIdx;
            if (visCnt > 0u) {
                std::vector<std::uint32_t> lineCps(visCnt);
                for (std::size_t m = 0u; m < visCnt; ++m) {
                    lineCps[m] = decoded[lineTokenIndices[lineStartGlyphIdx + m]].cp;
                }
                std::vector<std::size_t> visOrder;
                std::vector<std::uint8_t> visLevels;
                ComputeUax9LineVisualOrder(lineCps, baseRtl, visOrder, visLevels);

                std::vector<PositionedGlyph> origG(
                    result.glyphs.begin() + static_cast<std::ptrdiff_t>(lineStartGlyphIdx),
                    result.glyphs.begin() + static_cast<std::ptrdiff_t>(visEnd));
                std::vector<std::size_t> origTok(
                    lineTokenIndices.begin() + static_cast<std::ptrdiff_t>(lineStartGlyphIdx),
                    lineTokenIndices.begin() + static_cast<std::ptrdiff_t>(visEnd));

                float rePenX = 0.0f;
                for (std::size_t m = 0u; m < visCnt; ++m) {
                    const std::size_t srcOff = visOrder[m];
                    PositionedGlyph pg = origG[srcOff];
                    const std::size_t tokIdx = origTok[srcOff];
                    const TokenGlyph& ltg = decoded[tokIdx];
                    FontAtlasGlyph gmeta = ltg.meta;
                    if ((visLevels[srcOff] & 1u) != 0u) {
                        const std::uint32_t mcp = MirrorBracketCodePoint(ltg.cp);
                        if (mcp != ltg.cp) {
                            pg.codePoint = mcp;
                            if (ltg.hasMirroredMeta) {
                                gmeta = ltg.mirroredMeta;
                                pg.glyphIndex = gmeta.glyphIndex;
                                pg.u0 = gmeta.u0; pg.v0 = gmeta.v0;
                                pg.u1 = gmeta.u1; pg.v1 = gmeta.v1;
                                pg.cellWidth = gmeta.cellWidth;
                                pg.cellHeight = gmeta.cellHeight;
                                pg.pxRange = gmeta.pxRange;
                                pg.advanceX = gmeta.advanceEm * ltg.fontSize;
                            }
                        }
                    }
                    float trackPx = 0.0f;
                    if (m > 0u && std::isfinite(charSpacing) && charSpacing != 0.0f &&
                        ClassifyBidi(ltg.cp) != BidiClass::NSM) {
                        trackPx = charSpacing * 0.001f * ltg.fontSize;
                    }
                    const float pStart = rePenX + trackPx;
                    pg.penX = pStart;
                    if (ltg.hasCustomFont) {
                        pg.x0 = pStart + gmeta.bearingXEm * ltg.fontSize;
                        pg.x1 = pg.x0 + gmeta.quadWidthEm * ltg.fontSize;
                    } else {
                        pg.x0 = pStart;
                        pg.x1 = pStart + pg.advanceX;
                    }
                    result.glyphs[lineStartGlyphIdx + m] = pg;
                    lineTokenIndices[lineStartGlyphIdx + m] = tokIdx;
                    rePenX = pStart + pg.advanceX;
                }
                for (std::size_t k = visEnd; k < endGlyphIdx; ++k) {
                    result.glyphs[k].penX = rePenX;
                    result.glyphs[k].x0 = rePenX;
                    result.glyphs[k].x1 = rePenX;
                }
            }
        }

        float trimmedW = 0.0f;
        for (std::size_t k = lineStartGlyphIdx; k < endGlyphIdx; ++k) {
            PositionedGlyph& gpg = result.glyphs[k];
            const TokenGlyph& ltg = decoded[lineTokenIndices[k]];
            gpg.baselineY = lineBaselineY;
            if (ltg.hasCustomFont) {
                gpg.y0 = lineBaselineY - ltg.meta.bearingYEm * ltg.fontSize;
                gpg.y1 = gpg.y0 + ltg.meta.quadHeightEm * ltg.fontSize;
            } else {
                gpg.y0 = curTopY;
                gpg.y1 = curTopY + effectiveLineH;
            }
            if (!gpg.isWhitespace || !wrapEnabled) {
                trimmedW = std::max(trimmedW, gpg.penX + gpg.advanceX);
            }
        }
        if (!wrapEnabled && endGlyphIdx > lineStartGlyphIdx) {
            trimmedW = curPenX;
        }
        TextLineMetrics lm{};
        lm.lineIndex = curLineIdx;
        lm.width = trimmedW;
        lm.topY = curTopY;
        lm.baselineY = lineBaselineY;
        lm.ascent = lineAscent;
        lm.descent = lineDescent;
        lm.lineGap = lineGap;
        lm.lineHeight = effectiveLineH;
        lm.firstGlyphIndex = lineStartGlyphIdx;
        lm.glyphCount = endGlyphIdx - lineStartGlyphIdx;
        lm.endedByNewline = endedByNewline;
        result.lines.push_back(lm);
        result.totalWidth = std::max(result.totalWidth, lm.width);

        ++curLineIdx;
        curTopY += effectiveLineH;
        curPenX = 0.0f;
        lineStartGlyphIdx = endGlyphIdx;
        lastBreakDecodedIdx = static_cast<std::size_t>(-1);
        prevGlyphIdOnLine = 0xFFFFFFFFu;
        prevFontIdOnLine = 0u;
    };

    for (std::size_t i = 0u; i < decoded.size(); ++i) {
        const TokenGlyph& tg = decoded[i];
        if (tg.explicitNewline) {
            finishLine(result.glyphs.size(), true);
            continue;
        }

        const bool isSpace = (tg.cp == ' ' || tg.cp == '\t');
        float kernPx = 0.0f;
        float trackingPx = 0.0f;
        if (prevGlyphIdOnLine != 0xFFFFFFFFu) {
            if (tg.kernFace && tg.fontId == prevFontIdOnLine) {
                kernPx = tg.kernFace->GetKerningFUnits(prevGlyphIdOnLine, tg.meta.glyphIndex) * tg.upemInvScale;
            }
            if (std::isfinite(charSpacing) && charSpacing != 0.0f) {
                trackingPx = charSpacing * 0.001f * tg.fontSize;
            }
        }
        const float advPx = tg.meta.advanceEm * tg.fontSize;
        const float nextPenX = curPenX + kernPx + trackingPx + advPx;

        // CJK characters can also break BEFORE the ideograph itself
        if (wrapEnabled && IsWideCjkCodePoint(tg.cp) && result.glyphs.size() > lineStartGlyphIdx) {
            lastBreakDecodedIdx = i;
            lastBreakGlyphOutIdx = result.glyphs.size();
        }

        if (wrapEnabled && !isSpace &&
            (result.glyphs.size() > lineStartGlyphIdx) &&
            (nextPenX > wrapLimit)) {
            if (lastBreakDecodedIdx != static_cast<std::size_t>(-1) &&
                lastBreakGlyphOutIdx > lineStartGlyphIdx) {
                // Rewind back to last word/CJK break boundary (save break index before finishLine resets it)
                const std::size_t savedBreakDecodedIdx = lastBreakDecodedIdx;
                result.glyphs.resize(lastBreakGlyphOutIdx);
                lineTokenIndices.resize(lastBreakGlyphOutIdx);
                finishLine(result.glyphs.size(), false);
                // Skip leading spaces at the start of the wrapped line
                std::size_t resumeIdx = savedBreakDecodedIdx;
                while (resumeIdx < decoded.size() &&
                       !decoded[resumeIdx].explicitNewline &&
                       (decoded[resumeIdx].cp == ' ' || decoded[resumeIdx].cp == '\t')) {
                    ++resumeIdx;
                }
                i = resumeIdx - 1u;
                continue;
            }
            if (wrapMode == TextWrapping::Wrap) {
                // Emergency character-level break for unbroken words exceeding maxWidth
                finishLine(result.glyphs.size(), false);
                kernPx = 0.0f;
                trackingPx = 0.0f;
            }
        }

        const float penStart = curPenX + kernPx + trackingPx;
        const float baselineY = curTopY + tg.ascent;
        PositionedGlyph pg{};
        pg.codePoint = tg.cp;
        pg.glyphIndex = tg.meta.glyphIndex;
        pg.lineIndex = curLineIdx;
        pg.sourceCodePointIndex = tg.sourceCodePointIndex;
        pg.penX = penStart;
        pg.baselineY = baselineY;
        pg.advanceX = advPx;
        pg.pxRange = tg.meta.pxRange;
        pg.cellWidth = tg.meta.cellWidth;
        pg.cellHeight = tg.meta.cellHeight;
        pg.u0 = tg.meta.u0;
        pg.v0 = tg.meta.v0;
        pg.u1 = tg.meta.u1;
        pg.v1 = tg.meta.v1;
        pg.isWhitespace = tg.meta.isWhitespace || isSpace;
        pg.customRgba = tg.customRgba;
        pg.hasCustomRgba = tg.hasCustomRgba;
        pg.syntheticBold = tg.syntheticBold;
        pg.syntheticItalic = tg.syntheticItalic;
        pg.underline = tg.underline;
        pg.strikethrough = tg.strikethrough;

        if (tg.hasCustomFont) {
            pg.x0 = penStart + tg.meta.bearingXEm * tg.fontSize;
            pg.y0 = baselineY - tg.meta.bearingYEm * tg.fontSize;
            pg.x1 = pg.x0 + tg.meta.quadWidthEm * tg.fontSize;
            pg.y1 = pg.y0 + tg.meta.quadHeightEm * tg.fontSize;
        } else {
            pg.x0 = penStart;
            pg.y0 = curTopY;
            pg.x1 = penStart + advPx;
            pg.y1 = curTopY + result.lineHeight;
        }

        result.glyphs.push_back(pg);
        lineTokenIndices.push_back(i);
        curPenX = penStart + advPx;
        prevGlyphIdOnLine = tg.meta.glyphIndex;
        prevFontIdOnLine = tg.fontId;

        if (wrapEnabled && IsWordBreakAfter(tg.cp)) {
            lastBreakDecodedIdx = i + 1u;
            lastBreakGlyphOutIdx = result.glyphs.size();
        }
    }

    finishLine(result.glyphs.size(), true);

    if (!result.usedTrueTypeMetrics && result.lines.size() == 1u && validExplicitLineH <= 0.0f) {
        // Exact single-line height compatibility with MeasureUtf8TextRun (1.25f * fontSize)
        result.totalHeight = baseFont * 1.25f;
    } else {
        result.totalHeight = curTopY;
    }
    return result;
}

void FontAtlas::CopyGlyphRegionRgba8(
    std::uint32_t maxRows,
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outRows,
    std::uint64_t* outRevision) const {
    std::lock_guard<std::mutex> lock(mutex_);
    outWidth = atlasWidth_;
    const std::uint32_t limitRows = std::min<std::uint32_t>(
        maxRows,
        std::min<std::uint32_t>(glyphRegionHeight_, atlasHeight_));
    outRows = limitRows;
    if (outRevision) {
        *outRevision = revision_.load(std::memory_order_relaxed);
    }
    const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outRows;
    outRgba8.resize(pixelCount * 4u);
    const std::size_t avail = std::min(pixelCount, sdfPixels_.size());
    std::uint8_t* __restrict dst = outRgba8.data();
    const std::uint8_t* __restrict src = sdfPixels_.data();
    for (std::size_t i = 0u; i < avail; ++i) {
        const std::uint8_t sd = src[i];
        const std::size_t base = i * 4u;
        dst[base + 0u] = sd;
        dst[base + 1u] = sd;
        dst[base + 2u] = sd;
        dst[base + 3u] = 255u;
    }
    for (std::size_t i = avail; i < pixelCount; ++i) {
        const std::size_t base = i * 4u;
        dst[base + 0u] = 0u;
        dst[base + 1u] = 0u;
        dst[base + 2u] = 0u;
        dst[base + 3u] = 255u;
    }
}

float FontAtlas::SampleSdfBilinear(float u, float v) const noexcept {
    struct TlsFontView {
        std::uint64_t instanceId = 0u;
        std::uint64_t rev = 0u;
        std::uint16_t w = 0u;
        std::uint16_t h = 0u;
        const std::uint8_t* data = nullptr;
    };
    thread_local TlsFontView tlsView{};
    const std::uint64_t curRev = revision_.load(std::memory_order_acquire);
    if (tlsView.instanceId != instanceId_ || tlsView.rev != curRev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sdfPixels_.empty() || atlasWidth_ == 0u || atlasHeight_ == 0u) {
            return 0.0f;
        }
        tlsView.instanceId = instanceId_;
        tlsView.rev = curRev;
        tlsView.w = atlasWidth_;
        tlsView.h = atlasHeight_;
        tlsView.data = sdfPixels_.data();
    }
    const std::uint16_t atlasWidth_ = tlsView.w;
    const std::uint16_t atlasHeight_ = tlsView.h;
    const std::uint8_t* const sdfPixels_ = tlsView.data;
    const float tx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(atlasWidth_) - 0.5f;
    const float ty = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(atlasHeight_) - 0.5f;

    const int x0 = std::clamp(static_cast<int>(std::floor(tx)), 0, static_cast<int>(atlasWidth_) - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(ty)), 0, static_cast<int>(atlasHeight_) - 1);
    const int x1 = std::min(x0 + 1, static_cast<int>(atlasWidth_) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(atlasHeight_) - 1);
    const float fx = std::clamp(tx - std::floor(tx), 0.0f, 1.0f);
    const float fy = std::clamp(ty - std::floor(ty), 0.0f, 1.0f);

    const auto at = [&](int x, int y) noexcept -> float {
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

TextLayoutResult MeasureTextLayout(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing) {
    return FontAtlas::Instance().MeasureLayout(
        utf8Text, fontFamily, fontSize, maxWidth, wrapMode, charSpacing);
}

} // namespace aero
