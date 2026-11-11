#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/UiResourceContext.hpp"

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

    // Standard TrueType sfnt versions: 0x00010000 or 'true' (0x74727565)
    if (sfntVersion != 0x00010000u && sfntVersion != MakeTag('t', 'r', 'u', 'e')) {
        if (outError) *outError = "Unsupported sfnt version (requires TrueType glyf outlines)";
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
    TableSpan head{}, hhea{}, maxp{}, cmap{}, hmtx{}, loca{}, glyf{}, name{}, kern{};

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
        else if (tag == MakeTag('n', 'a', 'm', 'e')) name = span;
        else if (tag == MakeTag('k', 'e', 'r', 'n')) kern = span;
    }

    if (!head.present || !hhea.present || !maxp.present ||
        !cmap.present || !hmtx.present || !loca.present || !glyf.present) {
        if (outError) *outError = "Missing mandatory TrueType table (head/hhea/maxp/cmap/hmtx/loca/glyf)";
        return false;
    }

    if (head.length < 54u ||
        !r.ReadU16BE(head.offset + 18u, unitsPerEm_) ||
        !r.ReadI16BE(head.offset + 50u, indexToLocFormat_) ||
        unitsPerEm_ < 16u ||
        (indexToLocFormat_ != 0 && indexToLocFormat_ != 1)) {
        if (outError) *outError = "Invalid TrueType head table";
        return false;
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

    const std::size_t requiredLocaBytes = (indexToLocFormat_ == 0)
        ? (static_cast<std::size_t>(numGlyphs_) + 1u) * 2u
        : (static_cast<std::size_t>(numGlyphs_) + 1u) * 4u;
    if (locaLength_ < requiredLocaBytes) {
        if (outError) *outError = "Truncated TrueType loca table";
        return false;
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
    if (!ExtractGlyphRecursive(glyphIndex, 0u, totalVisitedComponents, outOutline.segments, nullptr)) {
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

FontAtlas& FontAtlas::Instance() noexcept {
    return UiResourceContext::Current().Fonts();
}

FontAtlas::FontAtlas(std::uint16_t atlasSize)
    : atlasWidth_(std::max<std::uint16_t>(atlasSize, 512u))
    , atlasHeight_(std::max<std::uint16_t>(atlasSize, 512u))
    , glyphRegionHeight_(static_cast<std::uint16_t>(std::max<std::uint16_t>(atlasSize, 512u) / 2u))
    , skyline_(atlasWidth_, 2u)
    , sdfPixels_(static_cast<std::size_t>(atlasWidth_) * atlasHeight_, 0u) {
    InitBuiltInFallbackAscii();
}

std::string FontAtlas::NormalizeFamilyKey(std::string_view rawFamily) {
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
    std::uint16_t& outY) noexcept {
    // Add 1-texel right/bottom safety gutter and start at x=2, y=2 so u0 > 0.0f and v1 <= 0.5f strictly
    const auto paddedW = static_cast<std::uint16_t>(cellW + 1u);
    const auto paddedH = static_cast<std::uint16_t>(cellH + 1u);
    if (paddedW + 4u > atlasWidth_ || paddedH + 4u > glyphRegionHeight_) {
        return false;
    }

    std::uint16_t bestX = 0xFFFFu;
    std::uint16_t bestY = 0xFFFFu;
    const auto xLimit = static_cast<std::uint16_t>(atlasWidth_ - 2u - paddedW);

    for (std::uint16_t x = 2u; x <= xLimit; x = static_cast<std::uint16_t>(x + 2u)) {
        std::uint16_t maxY = 2u;
        for (std::uint16_t k = 0u; k < paddedW; ++k) {
            maxY = std::max(maxY, skyline_[x + k]);
        }
        if (maxY + paddedH <= glyphRegionHeight_ && maxY < bestY) {
            bestY = maxY;
            bestX = x;
            if (bestY == 2u) {
                break;
            }
        }
    }

    if (bestX == 0xFFFFu) {
        return false;
    }

    for (std::uint16_t k = 0u; k < paddedW; ++k) {
        skyline_[bestX + k] = static_cast<std::uint16_t>(bestY + paddedH);
    }
    outX = bestX;
    outY = bestY;
    return true;
}

void FontAtlas::BlitCellR8Locked(
    std::uint16_t dstX,
    std::uint16_t dstY,
    std::uint16_t cellW,
    std::uint16_t cellH,
    const std::uint8_t* srcPixels) noexcept {
    if (!srcPixels) {
        return;
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

void FontAtlas::InitBuiltInFallbackAscii() {
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
    glyphCache_.clear();
    defaultFamilyKey_.clear();
    nextFontId_ = 1u;
    std::fill(skyline_.begin(), skyline_.end(), 2u);
    std::fill(sdfPixels_.begin(), sdfPixels_.end(), 0u);
    InitBuiltInFallbackAscii();
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

    for (std::uint16_t py = 0u; py < cellH; ++py) {
        const float sy = static_cast<float>(py) + 0.5f;
        for (std::uint16_t px = 0u; px < cellW; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            const bool inside = (EvaluateWindingNumber(windSegs, sx, sy) != 0);

            float bestDistSq = inside ? maxCutoffSq : maxCutoffSq;
            for (const auto& c : distCurves) {
                // Fast AABB lower-bound cull
                const float adx = std::max({c.minX - sx, 0.0f, sx - c.maxX});
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

bool FontAtlas::LoadTrueTypeFontFromMemory(
    const std::uint8_t* data,
    std::size_t size,
    std::string_view familyAlias,
    bool setAsDefault,
    std::string* outError) {
    auto face = std::make_shared<TrueTypeFontFace>();
    if (!face->LoadFromMemory(data, size, outError)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string primaryKey = NormalizeFamilyKey(familyAlias);
    if (primaryKey.empty()) {
        primaryKey = NormalizeFamilyKey(face->FamilyName());
    }
    if (primaryKey.empty()) {
        primaryKey = "defaultfont";
    }

    std::size_t entryIndex = fonts_.size();
    auto existingIt = familyAliasToIndex_.find(primaryKey);
    if (existingIt != familyAliasToIndex_.end()) {
        entryIndex = existingIt->second;
        fonts_[entryIndex].face = face;
        fonts_[entryIndex].isShardOnly = false;
    } else {
        RegisteredFontEntry entry{};
        entry.fontId = nextFontId_++;
        entry.canonicalKey = primaryKey;
        entry.face = face;
        fonts_.push_back(std::move(entry));
        familyAliasToIndex_[primaryKey] = entryIndex;
    }

    auto registerAlias = [&](std::string_view raw) {
        const std::string k = NormalizeFamilyKey(raw);
        if (!k.empty()) {
            familyAliasToIndex_[k] = entryIndex;
            // Also register base family without trailing "regular" (e.g. "Roboto-Regular" -> "roboto")
            if (k.size() > 7u && k.substr(k.size() - 7u) == "regular") {
                const std::string stripped = k.substr(0, k.size() - 7u);
                if (!stripped.empty() && familyAliasToIndex_.find(stripped) == familyAliasToIndex_.end()) {
                    familyAliasToIndex_[stripped] = entryIndex;
                }
            }
        }
    };

    registerAlias(familyAlias);
    registerAlias(face->FamilyName());
    registerAlias(face->FullName());

    if (setAsDefault) {
        defaultFamilyKey_ = primaryKey;
    }

    // Warm-bake printable ASCII glyphs (0x20..0x7E) so common UI text has zero first-frame miss
    for (std::uint32_t cp = 0x20u; cp <= 0x7Eu; ++cp) {
        (void)EnsureGlyphLocked(&fonts_[entryIndex], cp);
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

const FontAtlas::RegisteredFontEntry* FontAtlas::ResolveFontEntryLocked(
    std::string_view fontFamily) const noexcept {
    if (!fontFamily.empty()) {
        const std::string key = NormalizeFamilyKey(fontFamily);
        const auto it = familyAliasToIndex_.find(key);
        if (it != familyAliasToIndex_.end() && it->second < fonts_.size()) {
            return &fonts_[it->second];
        }
    }
    if (!defaultFamilyKey_.empty()) {
        const auto it = familyAliasToIndex_.find(defaultFamilyKey_);
        if (it != familyAliasToIndex_.end() && it->second < fonts_.size()) {
            return &fonts_[it->second];
        }
    }
    return nullptr;
}

bool FontAtlas::HasFontFamily(std::string_view familyName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = NormalizeFamilyKey(familyName);
    return !key.empty() && familyAliasToIndex_.find(key) != familyAliasToIndex_.end();
}

const TrueTypeFontFace* FontAtlas::FindFontFace(std::string_view familyName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const RegisteredFontEntry* entry = ResolveFontEntryLocked(familyName);
    return (entry && entry->face) ? entry->face.get() : nullptr;
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
            TtfGlyphOutline outline{};
            if (fontEntry->face->ExtractGlyphOutline(gid, outline)) {
                FontAtlasGlyph meta{};
                meta.codePoint = codePoint;
                std::vector<std::uint8_t> cellPixels;
                BakeOutlineToSdfCell(
                    outline,
                    fontEntry->face->UnitsPerEm(),
                    kDefaultBakeEmPixels,
                    kDefaultSdfPxRange,
                    meta,
                    cellPixels);

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
                        // Atlas full fallback — do NOT insert permanent tofu entry into glyphCache_
                        meta = (codePoint < 128u) ? builtInAscii_[codePoint] : builtInCjkBoxGlyph_;
                        meta.codePoint = codePoint;
                        meta.advanceEm = outline.advanceWidth / static_cast<float>(fontEntry->face->UnitsPerEm());
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
        return builtInAscii_[codePoint];
    }
    const std::uint64_t fallbackKey = (static_cast<std::uint64_t>(0xFFFFu) << 32u) | codePoint;
    if (const auto fbIt = glyphCache_.find(fallbackKey); fbIt != glyphCache_.end()) {
        return fbIt->second;
    }

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
    TextWrapping wrapMode) {
    TextLayoutResult result{};
    const float safeFont = std::max(1.0f, fontSize);

    std::unique_lock<std::mutex> lock(mutex_);
    const RegisteredFontEntry* fontEntry = ResolveFontEntryLocked(fontFamily);
    std::shared_ptr<TrueTypeFontFace> faceHolder =
        (fontEntry && fontEntry->face && fontEntry->face->IsValid())
        ? fontEntry->face
        : nullptr;
    const TrueTypeFontFace* face = faceHolder.get();
    const bool hasCustomFont = (fontEntry != nullptr);
    result.usedTrueTypeMetrics = hasCustomFont;

    if (face) {
        result.ascent = face->AscentEm() * safeFont;
        result.descent = face->DescentEm() * safeFont;
        result.lineGap = face->LineGapEm() * safeFont;
        result.lineHeight = std::max(1.0f, result.ascent + result.descent + result.lineGap);
    } else if (fontEntry && fontEntry->isShardOnly) {
        result.ascent = fontEntry->shardAscentEm * safeFont;
        result.descent = fontEntry->shardDescentEm * safeFont;
        result.lineGap = fontEntry->shardLineGapEm * safeFont;
        result.lineHeight = std::max(1.0f, result.ascent + result.descent + result.lineGap);
    } else {
        // Built-in fallback vertical metrics preserving 100% existing unit-test layout invariants
        result.ascent = safeFont * 0.95f;
        result.descent = safeFont * 0.25f;
        result.lineGap = 0.0f;
        result.lineHeight = safeFont * 1.2f;
    }

    if (utf8Text.empty()) {
        return result;
    }

    struct TokenGlyph {
        std::uint32_t cp = 0u;
        FontAtlasGlyph meta{};
        bool explicitNewline = false;
    };

    std::vector<TokenGlyph> decoded;
    decoded.reserve(utf8Text.size());
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        if (cp == '\r') {
            continue;
        }
        if (cp == '\n') {
            TokenGlyph nl{};
            nl.cp = '\n';
            nl.explicitNewline = true;
            decoded.push_back(nl);
            continue;
        }
        TokenGlyph tg{};
        tg.cp = cp;
        tg.meta = EnsureGlyphLocked(fontEntry, cp);
        decoded.push_back(tg);
    }
    lock.unlock();

    if (decoded.empty()) {
        return result;
    }

    const bool wrapEnabled = (wrapMode != TextWrapping::NoWrap) &&
        std::isfinite(maxWidth) && (maxWidth > 0.0f) && (maxWidth < 500000.0f);
    // Small float tolerance prevents re-wrapping an auto-sized TextBlock arranged to its own DesiredSize.width
    const float wrapLimit = maxWidth + 0.05f;
    const float upemInvScale = face
        ? (safeFont / static_cast<float>(face->UnitsPerEm()))
        : 0.0f;

    result.glyphs.reserve(decoded.size());

    std::uint32_t curLineIdx = 0u;
    float curPenX = 0.0f;
    float curTopY = 0.0f;
    std::size_t lineStartGlyphIdx = 0u;
    std::size_t lastBreakDecodedIdx = static_cast<std::size_t>(-1);
    std::size_t lastBreakGlyphOutIdx = 0u;
    std::uint32_t prevGlyphIdOnLine = 0xFFFFFFFFu;

    auto finishLine = [&](std::size_t endGlyphIdx) {
        float trimmedW = 0.0f;
        for (std::size_t k = lineStartGlyphIdx; k < endGlyphIdx; ++k) {
            if (!result.glyphs[k].isWhitespace || !wrapEnabled) {
                trimmedW = std::max(trimmedW, result.glyphs[k].penX + result.glyphs[k].advanceX);
            }
        }
        if (!wrapEnabled && endGlyphIdx > lineStartGlyphIdx) {
            trimmedW = curPenX;
        }
        TextLineMetrics lm{};
        lm.lineIndex = curLineIdx;
        lm.width = trimmedW;
        lm.topY = curTopY;
        lm.baselineY = curTopY + result.ascent;
        lm.ascent = result.ascent;
        lm.descent = result.descent;
        lm.lineGap = result.lineGap;
        lm.lineHeight = result.lineHeight;
        lm.firstGlyphIndex = lineStartGlyphIdx;
        lm.glyphCount = endGlyphIdx - lineStartGlyphIdx;
        result.lines.push_back(lm);
        result.totalWidth = std::max(result.totalWidth, lm.width);

        ++curLineIdx;
        curTopY += result.lineHeight;
        curPenX = 0.0f;
        lineStartGlyphIdx = endGlyphIdx;
        lastBreakDecodedIdx = static_cast<std::size_t>(-1);
        prevGlyphIdOnLine = 0xFFFFFFFFu;
    };

    for (std::size_t i = 0u; i < decoded.size(); ++i) {
        const TokenGlyph& tg = decoded[i];
        if (tg.explicitNewline) {
            finishLine(result.glyphs.size());
            continue;
        }

        const bool isSpace = (tg.cp == ' ' || tg.cp == '\t');
        float kernPx = 0.0f;
        if (face && prevGlyphIdOnLine != 0xFFFFFFFFu) {
            kernPx = face->GetKerningFUnits(prevGlyphIdOnLine, tg.meta.glyphIndex) * upemInvScale;
        }
        const float advPx = tg.meta.advanceEm * safeFont;
        const float nextPenX = curPenX + kernPx + advPx;

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
                finishLine(result.glyphs.size());
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
                finishLine(result.glyphs.size());
                kernPx = 0.0f;
            }
        }

        const float penStart = curPenX + kernPx;
        const float baselineY = curTopY + result.ascent;
        PositionedGlyph pg{};
        pg.codePoint = tg.cp;
        pg.glyphIndex = tg.meta.glyphIndex;
        pg.lineIndex = curLineIdx;
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

        if (hasCustomFont) {
            pg.x0 = penStart + tg.meta.bearingXEm * safeFont;
            pg.y0 = baselineY - tg.meta.bearingYEm * safeFont;
            pg.x1 = pg.x0 + tg.meta.quadWidthEm * safeFont;
            pg.y1 = pg.y0 + tg.meta.quadHeightEm * safeFont;
        } else {
            pg.x0 = penStart;
            pg.y0 = curTopY;
            pg.x1 = penStart + advPx;
            pg.y1 = curTopY + result.lineHeight;
        }

        result.glyphs.push_back(pg);
        curPenX = penStart + advPx;
        prevGlyphIdOnLine = tg.meta.glyphIndex;

        if (wrapEnabled && IsWordBreakAfter(tg.cp)) {
            lastBreakDecodedIdx = i + 1u;
            lastBreakGlyphOutIdx = result.glyphs.size();
        }
    }

    finishLine(result.glyphs.size());

    if (!hasCustomFont && result.lines.size() == 1u) {
        // Exact single-line height compatibility with MeasureUtf8TextRun (1.25f * fontSize)
        result.totalHeight = safeFont * 1.25f;
    } else {
        result.totalHeight = static_cast<float>(result.lines.size()) * result.lineHeight;
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (sdfPixels_.empty() || atlasWidth_ == 0u || atlasHeight_ == 0u) {
        return 0.0f;
    }
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
    TextWrapping wrapMode) {
    return FontAtlas::Instance().MeasureLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
}

} // namespace aero
