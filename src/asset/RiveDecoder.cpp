#include "aero/asset/RiveDecoder.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "aero/asset/AxbFormat.hpp"

namespace aero {

namespace {

// ============================================================================
// P1-5: Real Rive v7 LEB128 Binary .riv Stream Decoder -> AxbPackage + MOT6
// ============================================================================

enum class RivePropBacking : std::uint8_t {
    VarUint = 0,
    StringOrBytes = 1,
    Float32 = 2,
    Color32 = 3,
    Unknown = 0xFFu,
};

[[nodiscard]] RivePropBacking LookupRiveCoreBacking(std::uint32_t propKey) noexcept {
    switch (propKey) {
        // String / ByteSpan properties
        case 4u:
        case 55u:
        case 138u:
        case 203u:
        case 212u:
        case 246u:
        case 280u:
            return RivePropBacking::StringOrBytes;

        // 32-bit ARGB Color properties
        case 37u:
        case 38u:
        case 88u:
            return RivePropBacking::Color32;

        // 32-bit IEEE-754 Float properties
        case 7u: case 8u: case 9u: case 10u: case 11u: case 12u:
        case 13u: case 14u: case 15u: case 16u: case 17u: case 18u:
        case 20u: case 21u:
        case 24u: case 25u: case 26u:
        case 31u:
        case 33u: case 34u: case 35u: case 36u:
        case 39u:
        case 42u: case 43u: case 44u: case 45u: case 46u:
        case 47u:
        case 58u:
        case 63u: case 64u: case 65u: case 66u:
        case 70u:
        case 79u: case 80u: case 81u:
        case 82u: case 83u:
        case 84u: case 85u: case 86u: case 87u:
        case 89u: case 90u: case 91u:
        case 96u: case 97u: case 98u: case 99u: case 100u: case 101u:
        case 104u: case 105u: case 106u: case 107u: case 108u: case 109u:
        case 114u: case 115u: case 116u:
        case 123u: case 124u:
        case 126u: case 127u:
        case 140u:
        case 157u:
        case 161u: case 162u: case 163u:
        case 165u: case 166u:
        case 172u:
        case 177u:
        case 183u: case 184u: case 185u: case 193u: case 194u:
        case 202u:
        case 207u: case 208u:
        case 228u:
            return RivePropBacking::Float32;

        // LEB128 VarUint / Bool / Id / Enum properties
        case 3u: case 5u: case 6u:
        case 23u: case 32u: case 40u: case 41u:
        case 48u: case 49u: case 50u: case 51u: case 53u:
        case 56u: case 57u: case 59u: case 60u: case 61u: case 62u:
        case 67u: case 68u: case 69u:
        case 92u: case 93u: case 94u: case 95u:
        case 102u: case 103u:
        case 110u: case 111u: case 112u: case 113u:
        case 117u: case 118u: case 119u: case 120u: case 121u: case 122u:
        case 125u: case 128u: case 129u:
        case 131u: case 136u: case 141u: case 149u: case 150u:
        case 151u: case 152u: case 153u: case 154u: case 155u: case 156u:
        case 158u: case 160u: case 164u: case 167u: case 168u:
        case 173u: case 174u: case 175u:
        case 178u: case 179u: case 180u: case 181u: case 182u:
        case 186u: case 187u: case 188u: case 189u: case 190u: case 191u: case 192u:
        case 195u: case 196u: case 197u: case 198u: case 199u: case 200u: case 201u:
        case 204u: case 205u: case 206u:
        case 211u: case 224u: case 225u: case 226u: case 227u: case 229u:
        case 236u: case 237u: case 240u:
            return RivePropBacking::VarUint;

        default:
            return RivePropBacking::Unknown;
    }
}

struct RiveStreamReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;

    [[nodiscard]] bool ReadVarUint(std::uint64_t& out) noexcept {
        out = 0;
        unsigned shift = 0u;
        while (pos < size && shift < 64u) {
            const std::uint8_t b = data[pos++];
            out |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0u) {
                return true;
            }
            shift += 7u;
        }
        return false;
    }

    [[nodiscard]] bool ReadU32LE(std::uint32_t& out) noexcept {
        if (pos + 4u > size) {
            return false;
        }
        out = static_cast<std::uint32_t>(data[pos + 0u]) |
              (static_cast<std::uint32_t>(data[pos + 1u]) << 8u) |
              (static_cast<std::uint32_t>(data[pos + 2u]) << 16u) |
              (static_cast<std::uint32_t>(data[pos + 3u]) << 24u);
        pos += 4u;
        return true;
    }

    [[nodiscard]] bool ReadF32LE(float& out) noexcept {
        std::uint32_t bits = 0u;
        if (!ReadU32LE(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }

    [[nodiscard]] bool ReadSpan(const std::uint8_t*& outPtr, std::size_t& outLen) noexcept {
        std::uint64_t len64 = 0u;
        if (!ReadVarUint(len64) || len64 > (size - pos)) {
            return false;
        }
        outPtr = data + pos;
        outLen = static_cast<std::size_t>(len64);
        pos += outLen;
        return true;
    }
};

struct RiveDecodedProp {
    std::uint32_t key = 0u;
    RivePropBacking kind = RivePropBacking::VarUint;
    std::uint64_t uVal = 0u;
    float fVal = 0.0f;
    std::string sVal;
};

struct RiveDecodedObject {
    std::uint32_t typeKey = 0u;
    std::vector<RiveDecodedProp> props;

    [[nodiscard]] bool HasProp(std::uint32_t key) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return true;
        }
        return false;
    }

    [[nodiscard]] std::uint64_t GetU64(std::uint32_t key, std::uint64_t fallback = 0u) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return p.uVal;
        }
        return fallback;
    }

    [[nodiscard]] float GetF32(std::uint32_t key, float fallback = 0.0f) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) {
                if (p.kind == RivePropBacking::Float32) return p.fVal;
                return static_cast<float>(p.uVal);
            }
        }
        return fallback;
    }

    [[nodiscard]] std::string_view GetStr(std::uint32_t key) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return p.sVal;
        }
        return {};
    }

    [[nodiscard]] Color GetArgbColor(std::uint32_t key, Color fallback = Color{1.0f, 1.0f, 1.0f, 1.0f}) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) {
                const auto argb = static_cast<std::uint32_t>(p.uVal);
                const float a = static_cast<float>((argb >> 24u) & 0xFFu) * (1.0f / 255.0f);
                const float r = static_cast<float>((argb >> 16u) & 0xFFu) * (1.0f / 255.0f);
                const float g = static_cast<float>((argb >> 8u) & 0xFFu) * (1.0f / 255.0f);
                const float b = static_cast<float>(argb & 0xFFu) * (1.0f / 255.0f);
                return Color{r, g, b, a};
            }
        }
        return fallback;
    }
};

struct RiveDecodedArtboard {
    std::vector<RiveDecodedObject> objects;
};

void AppendSvgNumber(std::string& out, float v) {
    if (!std::isfinite(v)) {
        out.push_back('0');
        return;
    }
    if (v < 0.0f) {
        if (-v < 0.0005f) {
            out.push_back('0');
            return;
        }
        out.push_back('-');
        v = -v;
    } else if (v < 0.0005f) {
        out.push_back('0');
        return;
    }
    if (!(std::fabs(v) < 1.0e15f)) {
        char expBuf[32]{};
        const int n = std::snprintf(expBuf, sizeof(expBuf), "%.6g", static_cast<double>(v));
        if (n > 0) {
            out.append(expBuf, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof(expBuf) - 1))));
        } else {
            out.push_back('0');
        }
        return;
    }
    const auto scaled = static_cast<std::int64_t>(v * 1000.0f + 0.5f);
    const std::int64_t intPart = scaled / 1000;
    const std::int64_t fracPart = scaled % 1000;
    char buf[24]{};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), intPart);
    (void)ec;
    out.append(buf, static_cast<std::size_t>(ptr - buf));
    if (fracPart > 0) {
        out.push_back('.');
        char fbuf[3] = {
            static_cast<char>('0' + (fracPart / 100) % 10),
            static_cast<char>('0' + (fracPart / 10) % 10),
            static_cast<char>('0' + fracPart % 10)};
        std::size_t flen = 3u;
        while (flen > 0u && fbuf[flen - 1u] == '0') {
            --flen;
        }
        out.append(fbuf, flen);
    }
}

void AppendSvgPt(std::string& out, float x, float y) {
    AppendSvgNumber(out, x);
    out.push_back(' ');
    AppendSvgNumber(out, y);
}

struct RiveCubicVertex {
    float x = 0.0f;
    float y = 0.0f;
    float inX = 0.0f;
    float inY = 0.0f;
    float outX = 0.0f;
    float outY = 0.0f;
    bool hasIn = false;
    bool hasOut = false;
};

[[nodiscard]] std::string BuildRivePathSvg(
    const RiveDecodedArtboard& ab,
    std::size_t pathObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap) {
    const RiveDecodedObject& pathObj = ab.objects[pathObjIdx];
    const std::uint32_t tk = pathObj.typeKey;
    std::string svg;

    // PointsPath (typeKey = 16)
    if (tk == 16u) {
        const bool isClosed = (pathObj.GetU64(32u, 0u) != 0u);
        std::vector<RiveCubicVertex> verts;
        for (std::size_t childIdx : childrenMap[pathObjIdx]) {
            const RiveDecodedObject& vo = ab.objects[childIdx];
            const std::uint32_t vtk = vo.typeKey;
            if (vtk != 5u && vtk != 6u && vtk != 34u && vtk != 35u) {
                continue;
            }
            RiveCubicVertex cv{};
            cv.x = vo.GetF32(24u, 0.0f);
            cv.y = vo.GetF32(25u, 0.0f);
            cv.inX = cv.x;
            cv.inY = cv.y;
            cv.outX = cv.x;
            cv.outY = cv.y;

            if (vtk == 6u) {
                // CubicDetachedVertex
                const float inRot = vo.GetF32(84u, 0.0f);
                const float inDist = vo.GetF32(85u, 0.0f);
                const float outRot = vo.GetF32(86u, 0.0f);
                const float outDist = vo.GetF32(87u, 0.0f);
                if (std::fabs(inDist) > 1e-4f) {
                    cv.inX = cv.x + std::cos(inRot) * inDist;
                    cv.inY = cv.y + std::sin(inRot) * inDist;
                    cv.hasIn = true;
                }
                if (std::fabs(outDist) > 1e-4f) {
                    cv.outX = cv.x + std::cos(outRot) * outDist;
                    cv.outY = cv.y + std::sin(outRot) * outDist;
                    cv.hasOut = true;
                }
            } else if (vtk == 35u) {
                // CubicMirroredVertex
                const float rot = vo.GetF32(82u, 0.0f);
                const float dist = vo.GetF32(83u, 0.0f);
                if (std::fabs(dist) > 1e-4f) {
                    const float dx = std::cos(rot) * dist;
                    const float dy = std::sin(rot) * dist;
                    cv.outX = cv.x + dx;
                    cv.outY = cv.y + dy;
                    cv.inX = cv.x - dx;
                    cv.inY = cv.y - dy;
                    cv.hasIn = true;
                    cv.hasOut = true;
                }
            } else if (vtk == 34u) {
                // CubicAsymmetricVertex
                const float rot = vo.GetF32(79u, 0.0f);
                const float inDist = vo.GetF32(80u, 0.0f);
                const float outDist = vo.GetF32(81u, 0.0f);
                const float c = std::cos(rot);
                const float s = std::sin(rot);
                if (std::fabs(inDist) > 1e-4f) {
                    cv.inX = cv.x - c * inDist;
                    cv.inY = cv.y - s * inDist;
                    cv.hasIn = true;
                }
                if (std::fabs(outDist) > 1e-4f) {
                    cv.outX = cv.x + c * outDist;
                    cv.outY = cv.y + s * outDist;
                    cv.hasOut = true;
                }
            }
            verts.push_back(cv);
        }

        if (verts.size() < 2u) {
            return {};
        }
        svg.reserve(verts.size() * 28u);
        svg.push_back('M');
        AppendSvgPt(svg, verts[0].x, verts[0].y);

        const std::size_t segCount = isClosed ? verts.size() : (verts.size() - 1u);
        for (std::size_t i = 0u; i < segCount; ++i) {
            const RiveCubicVertex& a = verts[i];
            const RiveCubicVertex& b = verts[(i + 1u) % verts.size()];
            if (a.hasOut || b.hasIn) {
                svg.push_back('C');
                AppendSvgPt(svg, a.outX, a.outY);
                svg.push_back(' ');
                AppendSvgPt(svg, b.inX, b.inY);
                svg.push_back(' ');
                AppendSvgPt(svg, b.x, b.y);
            } else {
                svg.push_back('L');
                AppendSvgPt(svg, b.x, b.y);
            }
        }
        if (isClosed) {
            svg.push_back('Z');
        }
        return svg;
    }

    // Ellipse (typeKey = 4)
    if (tk == 4u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float cx = (0.5f - ox) * w;
        const float cy = (0.5f - oy) * h;
        const float rx = w * 0.5f;
        const float ry = h * 0.5f;
        constexpr float kKappa = 0.55228475f;
        const float kx = rx * kKappa;
        const float ky = ry * kKappa;

        svg.reserve(160u);
        svg.push_back('M');
        AppendSvgPt(svg, cx, cy - ry);
        svg.push_back('C');
        AppendSvgPt(svg, cx + kx, cy - ry); svg.push_back(' ');
        AppendSvgPt(svg, cx + rx, cy - ky); svg.push_back(' ');
        AppendSvgPt(svg, cx + rx, cy);
        svg.push_back('C');
        AppendSvgPt(svg, cx + rx, cy + ky); svg.push_back(' ');
        AppendSvgPt(svg, cx + kx, cy + ry); svg.push_back(' ');
        AppendSvgPt(svg, cx, cy + ry);
        svg.push_back('C');
        AppendSvgPt(svg, cx - kx, cy + ry); svg.push_back(' ');
        AppendSvgPt(svg, cx - rx, cy + ky); svg.push_back(' ');
        AppendSvgPt(svg, cx - rx, cy);
        svg.push_back('C');
        AppendSvgPt(svg, cx - rx, cy - ky); svg.push_back(' ');
        AppendSvgPt(svg, cx - kx, cy - ry); svg.push_back(' ');
        AppendSvgPt(svg, cx, cy - ry);
        svg.push_back('Z');
        return svg;
    }

    // Rectangle (typeKey = 7)
    if (tk == 7u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float x0 = -ox * w;
        const float y0 = -oy * h;
        const float x1 = x0 + w;
        const float y1 = y0 + h;

        const float maxR = 0.5f * std::min(w, h);
        const float tl = std::clamp(pathObj.GetF32(31u, 0.0f), 0.0f, maxR);
        const bool linked = (pathObj.GetU64(164u, 1u) != 0u);
        const float tr = std::clamp(linked ? tl : pathObj.GetF32(161u, tl), 0.0f, maxR);
        const float bl = std::clamp(linked ? tl : pathObj.GetF32(162u, tl), 0.0f, maxR);
        const float br = std::clamp(linked ? tl : pathObj.GetF32(163u, tl), 0.0f, maxR);

        svg.reserve(192u);
        if (tl <= 1e-3f && tr <= 1e-3f && br <= 1e-3f && bl <= 1e-3f) {
            svg.push_back('M'); AppendSvgPt(svg, x0, y0);
            svg.push_back('L'); AppendSvgPt(svg, x1, y0);
            svg.push_back('L'); AppendSvgPt(svg, x1, y1);
            svg.push_back('L'); AppendSvgPt(svg, x0, y1);
            svg.push_back('Z');
            return svg;
        }
        constexpr float kK = 0.55228475f;
        svg.push_back('M'); AppendSvgPt(svg, x0 + tl, y0);
        svg.push_back('L'); AppendSvgPt(svg, x1 - tr, y0);
        if (tr > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x1 - tr * (1.0f - kK), y0); svg.push_back(' ');
            AppendSvgPt(svg, x1, y0 + tr * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x1, y0 + tr);
        }
        svg.push_back('L'); AppendSvgPt(svg, x1, y1 - br);
        if (br > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x1, y1 - br * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x1 - br * (1.0f - kK), y1); svg.push_back(' ');
            AppendSvgPt(svg, x1 - br, y1);
        }
        svg.push_back('L'); AppendSvgPt(svg, x0 + bl, y1);
        if (bl > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x0 + bl * (1.0f - kK), y1); svg.push_back(' ');
            AppendSvgPt(svg, x0, y1 - bl * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x0, y1 - bl);
        }
        svg.push_back('L'); AppendSvgPt(svg, x0, y0 + tl);
        if (tl > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x0, y0 + tl * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x0 + tl * (1.0f - kK), y0); svg.push_back(' ');
            AppendSvgPt(svg, x0 + tl, y0);
        }
        svg.push_back('Z');
        return svg;
    }

    // Triangle (8), Polygon (51), Star (52)
    if (tk == 8u || tk == 51u || tk == 52u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float cx = (0.5f - ox) * w;
        const float cy = (0.5f - oy) * h;
        const float rx = w * 0.5f;
        const float ry = h * 0.5f;
        const std::uint64_t rawPts = pathObj.GetU64(125u, 5u);
        const std::uint32_t pts = (tk == 8u)
            ? 3u
            : static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rawPts, 3u, 512u));
        const bool isStar = (tk == 52u);
        const float rawInner = pathObj.GetF32(127u, 0.5f);
        const float innerRatio = isStar
            ? std::clamp(std::isfinite(rawInner) ? rawInner : 0.5f, 0.05f, 1.0f)
            : 1.0f;
        const std::uint32_t totalVerts = isStar ? (pts * 2u) : pts;
        svg.reserve(static_cast<std::size_t>(totalVerts) * 20u + 4u);
        constexpr float kHalfPi = 1.57079632679f;
        constexpr float kTwoPi = 6.28318530718f;
        for (std::uint32_t i = 0u; i < totalVerts; ++i) {
            const float angle = -kHalfPi + (kTwoPi * static_cast<float>(i)) / static_cast<float>(totalVerts);
            const float rScale = (isStar && (i & 1u) != 0u) ? innerRatio : 1.0f;
            const float px = cx + std::cos(angle) * rx * rScale;
            const float py = cy + std::sin(angle) * ry * rScale;
            svg.push_back(i == 0u ? 'M' : 'L');
            AppendSvgPt(svg, px, py);
        }
        svg.push_back('Z');
        return svg;
    }

    return {};
}

struct RiveResolvedPaint {
    bool hasFill = false;
    bool evenOddFill = false;
    Color fillColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool hasStroke = false;
    Color strokeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float strokeThickness = 0.0f;
};

[[nodiscard]] std::string TransformRiveAbsoluteSvgPath(
    std::string_view svg,
    float tx,
    float ty,
    float rotRad,
    float sx,
    float sy) {
    const bool isIdentity =
        (std::fabs(tx) <= 1e-4f) && (std::fabs(ty) <= 1e-4f) &&
        (std::fabs(rotRad) <= 1e-5f) && (std::fabs(sx - 1.0f) <= 1e-4f) &&
        (std::fabs(sy - 1.0f) <= 1e-4f);
    if (isIdentity || svg.empty()) {
        return std::string(svg);
    }
    const float cs = std::cos(rotRad);
    const float sn = std::sin(rotRad);
    const float m00 = cs * sx;
    const float m01 = -sn * sy;
    const float m10 = sn * sx;
    const float m11 = cs * sy;
    std::string out;
    out.reserve(svg.size() + 32u);
    std::size_t pos = 0u;
    while (pos < svg.size()) {
        detail::SkipSvgDelimiters(svg, pos);
        if (pos >= svg.size()) break;
        const char ch = svg[pos];
        if (ch == 'M' || ch == 'L') {
            ++pos;
            float x = 0.0f, y = 0.0f;
            if (detail::TryReadSvgFloat(svg, pos, x) && detail::TryReadSvgFloat(svg, pos, y)) {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                out.push_back(ch);
                AppendSvgPt(out, m00 * x + m01 * y + tx, m10 * x + m11 * y + ty);
            }
        } else if (ch == 'C') {
            ++pos;
            float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
            if (detail::TryReadSvgFloat(svg, pos, x1) && detail::TryReadSvgFloat(svg, pos, y1) &&
                detail::TryReadSvgFloat(svg, pos, x2) && detail::TryReadSvgFloat(svg, pos, y2) &&
                detail::TryReadSvgFloat(svg, pos, x) && detail::TryReadSvgFloat(svg, pos, y)) {
                out.push_back('C');
                AppendSvgPt(out, m00 * x1 + m01 * y1 + tx, m10 * x1 + m11 * y1 + ty); out.push_back(' ');
                AppendSvgPt(out, m00 * x2 + m01 * y2 + tx, m10 * x2 + m11 * y2 + ty); out.push_back(' ');
                AppendSvgPt(out, m00 * x + m01 * y + tx, m10 * x + m11 * y + ty);
            }
        } else if (ch == 'Z' || ch == 'z') {
            ++pos;
            out.push_back('Z');
        } else {
            ++pos;
        }
    }
    return out;
}

[[nodiscard]] bool ResolveRivePaintNodeColor(
    const RiveDecodedArtboard& ab,
    std::size_t paintObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap,
    Color& outColor) noexcept {
    if (ab.objects[paintObjIdx].GetU64(41u, 1u) == 0u) {
        return false;
    }
    for (std::size_t cIdx : childrenMap[paintObjIdx]) {
        const RiveDecodedObject& co = ab.objects[cIdx];
        if (co.typeKey == 18u) {
            // SolidColor
            Color col = co.GetArgbColor(37u, Color{1.0f, 1.0f, 1.0f, 1.0f});
            const float op = std::clamp(co.GetF32(18u, 1.0f), 0.0f, 1.0f);
            col.a *= op;
            if (col.a > 1e-4f) {
                outColor = col;
                return true;
            }
        } else if (co.typeKey == 22u || co.typeKey == 17u) {
            // LinearGradient (22) or RadialGradient (17): average stops weighted by alpha
            float sumR = 0.0f;
            float sumG = 0.0f;
            float sumB = 0.0f;
            float sumA = 0.0f;
            std::size_t stopCount = 0u;
            for (std::size_t sIdx : childrenMap[cIdx]) {
                const RiveDecodedObject& stopObj = ab.objects[sIdx];
                if (stopObj.typeKey == 19u) {
                    const Color sc = stopObj.GetArgbColor(38u, Color{1.0f, 1.0f, 1.0f, 1.0f});
                    sumR += sc.r;
                    sumG += sc.g;
                    sumB += sc.b;
                    sumA += sc.a;
                    ++stopCount;
                }
            }
            if (stopCount > 0u) {
                const float invN = 1.0f / static_cast<float>(stopCount);
                const float gradOp = std::clamp(
                    co.HasProp(46u) ? co.GetF32(46u, 1.0f) : co.GetF32(36u, 1.0f),
                    0.0f,
                    1.0f);
                Color avg{sumR * invN, sumG * invN, sumB * invN, (sumA * invN) * gradOp};
                if (avg.a > 1e-4f) {
                    outColor = avg;
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] RiveResolvedPaint ResolveRiveShapePaints(
    const RiveDecodedArtboard& ab,
    std::size_t shapeObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap) noexcept {
    RiveResolvedPaint res{};
    for (std::size_t cIdx : childrenMap[shapeObjIdx]) {
        const RiveDecodedObject& co = ab.objects[cIdx];
        if (co.typeKey == 20u) {
            Color fc{};
            if (ResolveRivePaintNodeColor(ab, cIdx, childrenMap, fc)) {
                res.hasFill = true;
                res.evenOddFill = (co.GetU64(40u, 0u) == 1u);
                res.fillColor = fc;
            }
        } else if (co.typeKey == 24u) {
            Color sc{};
            const float thick = co.GetF32(47u, 1.0f);
            if (thick > 1e-3f && ResolveRivePaintNodeColor(ab, cIdx, childrenMap, sc)) {
                res.hasStroke = true;
                res.strokeColor = sc;
                res.strokeThickness = thick;
            }
        }
    }
    return res;
}

[[nodiscard]] constexpr bool IsRiveHierarchyNodeType(std::uint32_t tk) noexcept {
    return tk == 2u || tk == 3u || tk == 40u || tk == 41u || tk == 92u || tk == 100u;
}

[[nodiscard]] constexpr bool IsRiveGeometryPathType(std::uint32_t tk) noexcept {
    return tk == 16u || tk == 4u || tk == 7u || tk == 8u || tk == 51u || tk == 52u;
}

} // namespace

bool IsRiveBinaryStream(
    const std::uint8_t* rawBytes,
    std::size_t byteLength) noexcept {
    if (!rawBytes || byteLength < 6u) {
        return false;
    }
    if (std::memcmp(rawBytes, "RIVE", 4u) != 0) {
        return false;
    }
    std::size_t idx = 4u;
    while (idx < byteLength &&
           (rawBytes[idx] == ' ' || rawBytes[idx] == '\t' ||
            rawBytes[idx] == '\r' || rawBytes[idx] == '\n')) {
        ++idx;
    }
    if (idx < byteLength && (rawBytes[idx] == '{' || rawBytes[idx] == '[')) {
        return false;
    }
    RiveStreamReader reader{rawBytes, byteLength, 4u};
    std::uint64_t majorVer = 0u;
    if (!reader.ReadVarUint(majorVer)) {
        return false;
    }
    return majorVer >= 6u && majorVer <= 32u;
}

bool DecodeRiveBinaryToAxbPackage(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AxbPackage& outPackage,
    std::vector<std::string>* outDiagnostics,
    std::string_view artboardNameHint) {
    if (!IsRiveBinaryStream(rawBytes, byteLength)) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_BAD_HEADER: Invalid Rive binary stream header");
        }
        return false;
    }

    RiveStreamReader r{rawBytes, byteLength, 4u};
    std::uint64_t majorVer = 0u;
    std::uint64_t minorVer = 0u;
    std::uint64_t fileId = 0u;
    if (!r.ReadVarUint(majorVer) || !r.ReadVarUint(minorVer) || !r.ReadVarUint(fileId)) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_TRUNCATED_HEADER: Truncated Rive version/fileId header");
        }
        return false;
    }

    // 1. Decode Table of Contents (ToC): LEB128 property keys terminated by 0,
    //    followed by 2-bit backing types packed 4 properties per uint32_t LE (Rive v7 currentBit == 8 rule).
    std::vector<std::uint32_t> tocKeys;
    while (true) {
        std::uint64_t key64 = 0u;
        if (!r.ReadVarUint(key64)) {
            if (outDiagnostics) {
                outDiagnostics->push_back("AERO_RIVE_TRUNCATED_TOC: Truncated Rive ToC property key table");
            }
            return false;
        }
        if (key64 == 0u) {
            break;
        }
        tocKeys.push_back(static_cast<std::uint32_t>(key64));
    }

    std::vector<std::pair<std::uint32_t, RivePropBacking>> tocBacking;
    tocBacking.reserve(tocKeys.size());
    std::uint32_t curBit = 8u;
    std::uint32_t curWord = 0u;
    for (std::uint32_t k : tocKeys) {
        if (curBit == 8u) {
            if (!r.ReadU32LE(curWord)) {
                if (outDiagnostics) {
                    outDiagnostics->push_back("AERO_RIVE_TRUNCATED_TOC_BITS: Truncated Rive ToC bit table");
                }
                return false;
            }
            curBit = 0u;
        }
        const auto btRaw = static_cast<std::uint8_t>((curWord >> curBit) & 0x3u);
        curBit += 2u;
        tocBacking.emplace_back(k, static_cast<RivePropBacking>(btRaw));
    }

    auto resolvePropBacking = [&](std::uint32_t propKey) noexcept -> RivePropBacking {
        for (const auto& kv : tocBacking) {
            if (kv.first == propKey) {
                return kv.second;
            }
        }
        return LookupRiveCoreBacking(propKey);
    };

    // 2. Decode Core object stream grouped into Artboards (typeKey = 1)
    std::vector<RiveDecodedArtboard> artboards;
    while (r.pos < r.size) {
        std::uint64_t typeKey64 = 0u;
        if (!r.ReadVarUint(typeKey64)) {
            if (outDiagnostics) {
                outDiagnostics->push_back("AERO_RIVE_TRUNCATED_OBJECT: Truncated object typeKey");
            }
            return false;
        }
        if (typeKey64 == 0u) {
            break;
        }

        RiveDecodedObject obj{};
        obj.typeKey = static_cast<std::uint32_t>(typeKey64);
        while (true) {
            std::uint64_t propKey64 = 0u;
            if (!r.ReadVarUint(propKey64)) {
                if (outDiagnostics) {
                    outDiagnostics->push_back("AERO_RIVE_TRUNCATED_PROP: Truncated property key");
                }
                return false;
            }
            if (propKey64 == 0u) {
                break;
            }
            const auto pk = static_cast<std::uint32_t>(propKey64);
            const RivePropBacking bt = resolvePropBacking(pk);
            if (bt == RivePropBacking::Unknown) {
                if (outDiagnostics) {
                    outDiagnostics->push_back(
                        "AERO_RIVE_UNKNOWN_PROP: Unknown Rive property key " + std::to_string(pk));
                }
                return false;
            }

            RiveDecodedProp dp{};
            dp.key = pk;
            dp.kind = bt;
            if (bt == RivePropBacking::VarUint) {
                if (!r.ReadVarUint(dp.uVal)) return false;
            } else if (bt == RivePropBacking::StringOrBytes) {
                const std::uint8_t* ptr = nullptr;
                std::size_t len = 0u;
                if (!r.ReadSpan(ptr, len)) return false;
                // Skip copying embedded binary image blobs (prop 212 = FileAssetContents.bytes)
                if (pk != 212u && len > 0u) {
                    dp.sVal.assign(reinterpret_cast<const char*>(ptr), len);
                }
            } else if (bt == RivePropBacking::Float32) {
                if (!r.ReadF32LE(dp.fVal)) return false;
            } else if (bt == RivePropBacking::Color32) {
                std::uint32_t c32 = 0u;
                if (!r.ReadU32LE(c32)) return false;
                dp.uVal = c32;
            }
            obj.props.push_back(std::move(dp));
        }

        if (obj.typeKey == 1u) {
            RiveDecodedArtboard ab{};
            ab.objects.push_back(std::move(obj));
            artboards.push_back(std::move(ab));
        } else if (!artboards.empty()) {
            artboards.back().objects.push_back(std::move(obj));
        }
    }

    if (artboards.empty()) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_NO_ARTBOARD: Rive stream contains no Artboard");
        }
        return false;
    }

    std::size_t mainAbIdx = 0u;
    if (!artboardNameHint.empty()) {
        for (std::size_t i = 0u; i < artboards.size(); ++i) {
            if (artboards[i].objects[0].GetStr(4u) == artboardNameHint) {
                mainAbIdx = i;
                break;
            }
        }
    }

    const RiveDecodedArtboard& mainAb = artboards[mainAbIdx];
    const RiveDecodedObject& abRootObj = mainAb.objects[0];
    const std::string_view abName = abRootObj.GetStr(4u);
    const float abW = std::max(1.0f, abRootObj.GetF32(7u, 500.0f));
    const float abH = std::max(1.0f, abRootObj.GetF32(8u, 500.0f));
    const float abOriginX = abRootObj.GetF32(11u, 0.0f);
    const float abOriginY = abRootObj.GetF32(12u, 0.0f);

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";
    if (!abName.empty()) {
        pkg.className = std::string(abName);
    }

    std::uint32_t nextLocalElemIdx = 0u;
    auto pushStaticProp = [&](std::uint16_t propId, UnifiedValue val) {
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::SetStaticProperty, propId, 0, 0, 0, 0, std::move(val)});
    };

    // Emit root Artboard Canvas (localElementIdx = 0)
    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(abName), 0, {}});
    ++nextLocalElemIdx;
    pushStaticProp(PropId_Width, MakeFloat(abW));
    pushStaticProp(PropId_Height, MakeFloat(abH));
    pushStaticProp(PropId_ClipToBounds, UnifiedValue{abRootObj.GetU64(196u, 1u) != 0u});

    if (abRootObj.GetU64(41u, 0u) != 0u) {
        // Optional Artboard solid fill child if present
        for (std::size_t oi = 1u; oi < mainAb.objects.size(); ++oi) {
            if (mainAb.objects[oi].typeKey == 20u && mainAb.objects[oi].GetU64(5u, 0xFFFFFFFFu) == 0u) {
                for (std::size_t ci = 1u; ci < mainAb.objects.size(); ++ci) {
                    if (mainAb.objects[ci].typeKey == 18u && mainAb.objects[ci].GetU64(5u, 0xFFFFFFFFu) == oi) {
                        const Color bg = mainAb.objects[ci].GetArgbColor(37u);
                        if (bg.a > 1e-4f) {
                            pushStaticProp(PropId_Background, value::FromColor(bg));
                        }
                    }
                }
            }
        }
    }

    // Count total candidate paths across artboards to pick optimal MTSDF cell size (fast baking on huge scenes)
    std::size_t estimatedPathCount = 0u;
    for (const auto& o : mainAb.objects) {
        if (IsRiveGeometryPathType(o.typeKey)) {
            ++estimatedPathCount;
        }
    }
    const bool compactBakeMode = (estimatedPathCount > 36u);

    std::vector<std::pair<std::uint64_t, Rect>> bakedPathBoundsCache;
    auto ensurePathBaked = [&](std::string_view svgData, Rect& outBounds) -> bool {
        if (svgData.empty()) {
            return false;
        }
        const std::uint64_t h = ComputeSvgPathHash(svgData);
        if (h == 0u) {
            return false;
        }
        for (const auto& kv : bakedPathBoundsCache) {
            if (kv.first == h) {
                outBounds = kv.second;
                return (outBounds.width > 1e-3f && outBounds.height > 1e-3f);
            }
        }
        float recRange = 4.0f;
        std::uint16_t cellDim = compactBakeMode
            ? static_cast<std::uint16_t>(24u)
            : SelectAdaptiveMtsdfCellSize(svgData, 32.0f, &recRange);
        if (compactBakeMode) {
            recRange = 3.0f;
        }
        if (pkg.mtsdfPaths.size() > 0xFFFFu) {
            return false;
        }
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
        AxbMtsdfPathEntry entry = BakeSvgPathToMtsdfEntry(svgData, cellDim, recRange, slotIdx, 512u);
        outBounds = Rect{entry.boundsX, entry.boundsY, entry.boundsW, entry.boundsH};
        pkg.mtsdfPaths.push_back(std::move(entry));
        bakedPathBoundsCache.emplace_back(h, outBounds);
        return (outBounds.width > 1e-3f && outBounds.height > 1e-3f);
    };

    // Map main artboard object index -> emitted AxbPackage localElementIdx (0xFFFFFFFFu if unmapped)
    std::vector<std::uint32_t> mainObjToElemIdx(mainAb.objects.size(), 0xFFFFFFFFu);
    mainObjToElemIdx[0] = 0u;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

    bool hierarchyOverflowOrCycle = false;
    std::vector<std::uint8_t> activeArtboardStack(artboards.size(), 0u);

    auto emitArtboardSubtree = [&](auto& selfRef,
                                   std::size_t abIndex,
                                   float rootOffsetX,
                                   float rootOffsetY,
                                   int nestingDepth,
                                   bool recordMainIndices) -> void {
        if (hierarchyOverflowOrCycle || abIndex >= artboards.size() || activeArtboardStack[abIndex] != 0u) {
            hierarchyOverflowOrCycle = true;
            return;
        }
        activeArtboardStack[abIndex] = 1u;

        const RiveDecodedArtboard& ab = artboards[abIndex];
        // Detect any cyclic parent chain across artboard objects in O(N)
        std::vector<std::uint8_t> parentState(ab.objects.size(), 0u);
        for (std::size_t startIdx = 1u; startIdx < ab.objects.size(); ++startIdx) {
            if (parentState[startIdx] == 2u) {
                continue;
            }
            std::size_t cur = startIdx;
            while (cur > 0u && cur < ab.objects.size() && parentState[cur] == 0u) {
                parentState[cur] = 1u;
                const std::uint64_t pid = ab.objects[cur].GetU64(5u, 0xFFFFFFFFULL);
                if (pid >= ab.objects.size()) {
                    cur = 0u;
                    break;
                }
                cur = static_cast<std::size_t>(pid);
            }
            if (cur > 0u && cur < ab.objects.size() && parentState[cur] == 1u) {
                hierarchyOverflowOrCycle = true;
                activeArtboardStack[abIndex] = 0u;
                if (outDiagnostics) {
                    outDiagnostics->push_back("AERO_RIVE_PARENT_CYCLE: Cyclic parent chain in Rive artboard");
                }
                return;
            }
            cur = startIdx;
            while (cur > 0u && cur < ab.objects.size() && parentState[cur] == 1u) {
                parentState[cur] = 2u;
                const std::uint64_t pid = ab.objects[cur].GetU64(5u, 0xFFFFFFFFULL);
                if (pid >= ab.objects.size()) {
                    break;
                }
                cur = static_cast<std::size_t>(pid);
            }
        }

        std::vector<std::vector<std::size_t>> childrenMap(ab.objects.size());
        std::vector<std::uint8_t> hasAnimatedOrConstrainedObj(ab.objects.size(), 0u);
        for (std::size_t i = 1u; i < ab.objects.size(); ++i) {
            const RiveDecodedObject& co = ab.objects[i];
            const std::uint64_t pid = co.GetU64(5u, 0xFFFFFFFFULL);
            if (pid < ab.objects.size()) {
                childrenMap[static_cast<std::size_t>(pid)].push_back(i);
            }
            if (co.typeKey == 25u) {
                const std::uint64_t keyedId = co.GetU64(51u, 0xFFFFFFFFULL);
                if (keyedId < ab.objects.size()) {
                    hasAnimatedOrConstrainedObj[static_cast<std::size_t>(keyedId)] = 1u;
                }
            } else if (co.typeKey == 87u || co.typeKey == 88u || co.typeKey == 89u) {
                if (pid < ab.objects.size()) {
                    hasAnimatedOrConstrainedObj[static_cast<std::size_t>(pid)] = 1u;
                }
                const std::uint64_t targetId = co.GetU64(173u, 0xFFFFFFFFULL);
                if (targetId < ab.objects.size()) {
                    hasAnimatedOrConstrainedObj[static_cast<std::size_t>(targetId)] = 1u;
                }
            }
        }

        std::vector<std::uint8_t> activeNodeStack(ab.objects.size(), 0u);

        auto emitNodeRecursive = [&](auto& nodeRec,
                                     std::size_t objIdx,
                                     bool parentIsArtboardRoot,
                                     std::uint32_t depth) -> void {
            constexpr std::uint32_t kMaxRiveHierarchyDepth = 256u;
            if (hierarchyOverflowOrCycle || objIdx >= ab.objects.size()) {
                return;
            }
            if (depth > kMaxRiveHierarchyDepth || activeNodeStack[objIdx] != 0u) {
                hierarchyOverflowOrCycle = true;
                if (outDiagnostics) {
                    outDiagnostics->push_back(
                        depth > kMaxRiveHierarchyDepth
                            ? "AERO_RIVE_MAX_DEPTH: Rive hierarchy depth exceeds maximum limit (256)"
                            : "AERO_RIVE_PARENT_CYCLE: Cyclic hierarchy node in Rive artboard");
                }
                return;
            }
            const RiveDecodedObject& obj = ab.objects[objIdx];
            const std::uint32_t tk = obj.typeKey;
            if (!IsRiveHierarchyNodeType(tk)) {
                return;
            }
            activeNodeStack[objIdx] = 1u;

            const std::string_view nodeName = recordMainIndices ? obj.GetStr(4u) : std::string_view{};
            const std::uint32_t myElemIdx = nextLocalElemIdx++;
            if (recordMainIndices && objIdx < mainObjToElemIdx.size()) {
                mainObjToElemIdx[objIdx] = myElemIdx;
            }

            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(nodeName), 0, {}});
            if (!nodeName.empty() && recordMainIndices) {
                pkg.namedControls.push_back(AxbNamedControlSlot{
                    std::string(nodeName), "Canvas", 0, PropertySlotHandle::InvalidIndex});
            }
            pushStaticProp(PropId_Width, MakeFloat(1.0f));
            pushStaticProp(PropId_Height, MakeFloat(1.0f));
            pushStaticProp(PropId_HorizontalAlignment, value::FromInt(1)); // Left
            pushStaticProp(PropId_VerticalAlignment, value::FromInt(1));   // Top

            float tx = (tk == 41u) ? obj.GetF32(90u, 0.0f) : obj.GetF32(13u, 0.0f);
            float ty = (tk == 41u) ? obj.GetF32(91u, 0.0f) : obj.GetF32(14u, 0.0f);
            if (tk == 40u && !obj.HasProp(13u)) {
                // Child Bone defaults x to parent Bone's length along +X
                const std::uint64_t pid = obj.GetU64(5u, 0xFFFFFFFFULL);
                if (pid < ab.objects.size() &&
                    (ab.objects[static_cast<std::size_t>(pid)].typeKey == 40u ||
                     ab.objects[static_cast<std::size_t>(pid)].typeKey == 41u)) {
                    tx = ab.objects[static_cast<std::size_t>(pid)].GetF32(89u, 0.0f);
                }
            }
            if (parentIsArtboardRoot) {
                tx += rootOffsetX;
                ty += rootOffsetY;
            }
            const float rotDeg = obj.GetF32(15u, 0.0f) * kRadToDeg;
            const float sx = obj.GetF32(16u, 1.0f);
            const float sy = obj.GetF32(17u, 1.0f);
            const float op = std::clamp(obj.GetF32(18u, 1.0f), 0.0f, 1.0f);

            if (std::fabs(tx) > 1e-4f) pushStaticProp(PropId_AnimTranslateX, MakeFloat(tx));
            if (std::fabs(ty) > 1e-4f) pushStaticProp(PropId_AnimTranslateY, MakeFloat(ty));
            if (std::fabs(rotDeg) > 1e-4f) pushStaticProp(PropId_AnimRotateAngle, MakeFloat(rotDeg));
            if (std::fabs(sx - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleX, MakeFloat(sx));
            if (std::fabs(sy - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleY, MakeFloat(sy));
            if (std::fabs(op - 1.0f) > 1e-4f) pushStaticProp(PropId_Opacity, MakeFloat(op));

            // If Shape (typeKey = 3), combine multi-subpath compound shapes (e.g. letter cutouts in logo.riv)
            if (tk == 3u) {
                const RiveResolvedPaint paint = ResolveRiveShapePaints(ab, objIdx, childrenMap);
                if (paint.hasFill || paint.hasStroke) {
                    std::vector<std::size_t> geomChildIndices;
                    bool anyAnimatedOrNamedSubpath = false;
                    for (std::size_t cIdx : childrenMap[objIdx]) {
                        if (IsRiveGeometryPathType(ab.objects[cIdx].typeKey)) {
                            geomChildIndices.push_back(cIdx);
                            if (hasAnimatedOrConstrainedObj[cIdx] != 0u ||
                                (recordMainIndices && !ab.objects[cIdx].GetStr(4u).empty())) {
                                anyAnimatedOrNamedSubpath = true;
                            }
                        }
                    }
                    bool mergedCompoundEmitted = false;
                    if (geomChildIndices.size() >= 2u && !anyAnimatedOrNamedSubpath) {
                        std::string compoundSvg = paint.evenOddFill ? "F0 " : "F1 ";
                        std::size_t validSubpaths = 0u;
                        for (std::size_t cIdx : geomChildIndices) {
                            const RiveDecodedObject& pathObj = ab.objects[cIdx];
                            const std::string subSvg = BuildRivePathSvg(ab, cIdx, childrenMap);
                            if (subSvg.empty()) continue;
                            const float px = pathObj.GetF32(13u, 0.0f);
                            const float py = pathObj.GetF32(14u, 0.0f);
                            const float pRotRad = pathObj.GetF32(15u, 0.0f);
                            const float psx = pathObj.GetF32(16u, 1.0f);
                            const float psy = pathObj.GetF32(17u, 1.0f);
                            const std::string tfSvg = TransformRiveAbsoluteSvgPath(subSvg, px, py, pRotRad, psx, psy);
                            if (!tfSvg.empty()) {
                                if (validSubpaths > 0u) compoundSvg.push_back(' ');
                                compoundSvg += tfSvg;
                                ++validSubpaths;
                            }
                        }
                        Rect compoundBounds{};
                        if (validSubpaths >= 2u && ensurePathBaked(compoundSvg, compoundBounds)) {
                            const std::uint32_t pathElemIdx = nextLocalElemIdx++;
                            for (std::size_t cIdx : geomChildIndices) {
                                if (recordMainIndices && cIdx < mainObjToElemIdx.size()) {
                                    mainObjToElemIdx[cIdx] = pathElemIdx;
                                }
                            }
                            pkg.instructions.push_back(AxbInstruction{
                                AxbOpcode::BeginElement, 0, 0, pkg.InternString("Path"), pkg.InternString(""), 0, {}});
                            pushStaticProp(PropId_CanvasLeft, MakeFloat(compoundBounds.x));
                            pushStaticProp(PropId_CanvasTop, MakeFloat(compoundBounds.y));
                            pushStaticProp(PropId_Width, MakeFloat(std::max(1.0f, compoundBounds.width)));
                            pushStaticProp(PropId_Height, MakeFloat(std::max(1.0f, compoundBounds.height)));
                            pushStaticProp(PropId_Stretch, value::FromInt(1));
                            pushStaticProp(PropId_PathData, value::FromString(compoundSvg));
                            if (paint.hasFill) {
                                pushStaticProp(PropId_Background, value::FromColor(paint.fillColor));
                            }
                            if (paint.hasStroke) {
                                pushStaticProp(PropId_BorderBrush, value::FromColor(paint.strokeColor));
                                pushStaticProp(PropId_BorderThickness, value::FromThickness(Thickness{paint.strokeThickness}));
                            }
                            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
                            mergedCompoundEmitted = true;
                        }
                    }
                    if (!mergedCompoundEmitted) for (std::size_t cIdx : childrenMap[objIdx]) {
                        const RiveDecodedObject& pathObj = ab.objects[cIdx];
                        if (!IsRiveGeometryPathType(pathObj.typeKey)) {
                            continue;
                        }
                        const std::string svgPath = BuildRivePathSvg(ab, cIdx, childrenMap);
                        Rect pathBounds{};
                        if (!ensurePathBaked(svgPath, pathBounds)) {
                            continue;
                        }

                        const float px = pathObj.GetF32(13u, 0.0f);
                        const float py = pathObj.GetF32(14u, 0.0f);
                        const float pRotDeg = pathObj.GetF32(15u, 0.0f) * kRadToDeg;
                        const float psx = pathObj.GetF32(16u, 1.0f);
                        const float psy = pathObj.GetF32(17u, 1.0f);
                        const bool hasPathLocalTf =
                            (std::fabs(px) > 1e-4f) || (std::fabs(py) > 1e-4f) ||
                            (std::fabs(pRotDeg) > 1e-4f) || (std::fabs(psx - 1.0f) > 1e-4f) ||
                            (std::fabs(psy - 1.0f) > 1e-4f);

                        if (hasPathLocalTf) {
                            const std::uint32_t wrapIdx = nextLocalElemIdx++;
                            if (recordMainIndices && cIdx < mainObjToElemIdx.size()) {
                                mainObjToElemIdx[cIdx] = wrapIdx;
                            }
                            pkg.instructions.push_back(AxbInstruction{
                                AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(""), 0, {}});
                            pushStaticProp(PropId_Width, MakeFloat(1.0f));
                            pushStaticProp(PropId_Height, MakeFloat(1.0f));
                            if (std::fabs(px) > 1e-4f) pushStaticProp(PropId_AnimTranslateX, MakeFloat(px));
                            if (std::fabs(py) > 1e-4f) pushStaticProp(PropId_AnimTranslateY, MakeFloat(py));
                            if (std::fabs(pRotDeg) > 1e-4f) pushStaticProp(PropId_AnimRotateAngle, MakeFloat(pRotDeg));
                            if (std::fabs(psx - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleX, MakeFloat(psx));
                            if (std::fabs(psy - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleY, MakeFloat(psy));
                        }

                        const std::uint32_t pathElemIdx = nextLocalElemIdx++;
                        if (recordMainIndices && cIdx < mainObjToElemIdx.size() && !hasPathLocalTf) {
                            mainObjToElemIdx[cIdx] = pathElemIdx;
                        }
                        const std::string_view pName = recordMainIndices ? pathObj.GetStr(4u) : std::string_view{};
                        pkg.instructions.push_back(AxbInstruction{
                            AxbOpcode::BeginElement, 0, 0, pkg.InternString("Path"), pkg.InternString(pName), 0, {}});
                        pushStaticProp(PropId_CanvasLeft, MakeFloat(pathBounds.x));
                        pushStaticProp(PropId_CanvasTop, MakeFloat(pathBounds.y));
                        pushStaticProp(PropId_Width, MakeFloat(std::max(1.0f, pathBounds.width)));
                        pushStaticProp(PropId_Height, MakeFloat(std::max(1.0f, pathBounds.height)));
                        pushStaticProp(PropId_Stretch, value::FromInt(1)); // Stretch::Fill (exact 1:1 with boundsW x boundsH)
                        pushStaticProp(PropId_PathData, value::FromString(svgPath));
                        if (paint.hasFill) {
                            pushStaticProp(PropId_Background, value::FromColor(paint.fillColor));
                        }
                        if (paint.hasStroke) {
                            pushStaticProp(PropId_BorderBrush, value::FromColor(paint.strokeColor));
                            pushStaticProp(PropId_BorderThickness, value::FromThickness(Thickness{paint.strokeThickness}));
                        }
                        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

                        if (hasPathLocalTf) {
                            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
                        }
                    }
                }
            }

            // If NestedArtboard (typeKey = 92), recursively inline target artboard visuals
            if (tk == 92u && nestingDepth < 2) {
                const auto nestedAbId = static_cast<std::size_t>(obj.GetU64(197u, 0xFFFFFFFFULL));
                if (nestedAbId < artboards.size() && nestedAbId != abIndex) {
                    const RiveDecodedObject& nestedRoot = artboards[nestedAbId].objects[0];
                    const float nw = std::max(1.0f, nestedRoot.GetF32(7u, 100.0f));
                    const float nh = std::max(1.0f, nestedRoot.GetF32(8u, 100.0f));
                    const float nox = nestedRoot.GetF32(11u, 0.0f);
                    const float noy = nestedRoot.GetF32(12u, 0.0f);
                    selfRef(selfRef, nestedAbId, -nox * nw, -noy * nh, nestingDepth + 1, false);
                }
            }

            // Recurse into child hierarchy nodes in exact draw order
            for (std::size_t cIdx : childrenMap[objIdx]) {
                if (IsRiveHierarchyNodeType(ab.objects[cIdx].typeKey)) {
                    nodeRec(nodeRec, cIdx, false, depth + 1u);
                    if (hierarchyOverflowOrCycle) {
                        break;
                    }
                }
            }

            activeNodeStack[objIdx] = 0u;
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        };

        for (std::size_t cIdx : childrenMap[0]) {
            if (IsRiveHierarchyNodeType(ab.objects[cIdx].typeKey)) {
                emitNodeRecursive(emitNodeRecursive, cIdx, true, 1u);
                if (hierarchyOverflowOrCycle) {
                    break;
                }
            }
        }
        activeArtboardStack[abIndex] = 0u;
    };

    emitArtboardSubtree(
        emitArtboardSubtree,
        mainAbIdx,
        abOriginX * abW,
        abOriginY * abH,
        0,
        true);
    if (hierarchyOverflowOrCycle) {
        return false;
    }

    // Close root Artboard Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    if (!pkg.mtsdfPaths.empty()) {
        const std::uint16_t targetAtlas = (pkg.mtsdfPaths.size() > 64u)
            ? static_cast<std::uint16_t>(1024u)
            : static_cast<std::uint16_t>(512u);
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, targetAtlas);
    }

    // 3. Extract TransformConstraints (typeKey = 87, 88, 89)
    for (std::size_t i = 1u; i < mainAb.objects.size(); ++i) {
        const RiveDecodedObject& co = mainAb.objects[i];
        if (co.typeKey == 87u || co.typeKey == 88u || co.typeKey == 89u) {
            const auto constrainedObjId = static_cast<std::size_t>(co.GetU64(5u, 0xFFFFFFFFULL));
            const auto targetObjId = static_cast<std::size_t>(co.GetU64(173u, 0xFFFFFFFFULL));
            if (constrainedObjId < mainObjToElemIdx.size() &&
                targetObjId < mainObjToElemIdx.size()) {
                const std::uint32_t cElem = mainObjToElemIdx[constrainedObjId];
                const std::uint32_t tElem = mainObjToElemIdx[targetObjId];
                if (cElem != 0xFFFFFFFFu && tElem != 0xFFFFFFFFu &&
                    cElem <= 0xFFFFu && tElem <= 0xFFFFu &&
                    cElem != tElem) {
                    AxbTransformConstraintDesc tc{};
                    tc.childLocalElementIdx = static_cast<std::uint16_t>(cElem);
                    tc.parentLocalElementIdx = static_cast<std::uint16_t>(tElem);
                    pkg.transformConstraints.push_back(tc);
                }
            }
        }
    }

    // 4. Decode LinearAnimations (typeKey = 31) -> MOT6 Clips, Tracks & Keyframes
    auto mapRiveKeyedProperty = [](std::uint32_t rivePropKey, bool& outConvertRadToDeg) noexcept -> std::uint16_t {
        outConvertRadToDeg = false;
        switch (rivePropKey) {
            case 13u:
            case 90u:
                return PropId_AnimTranslateX;
            case 14u:
            case 91u:
                return PropId_AnimTranslateY;
            case 15u:
                outConvertRadToDeg = true;
                return PropId_AnimRotateAngle;
            case 16u:
                return PropId_AnimScaleX;
            case 17u:
                return PropId_AnimScaleY;
            case 18u:
                return PropId_Opacity;
            case 20u:
                return PropId_Width;
            case 21u:
                return PropId_Height;
            case 37u:
            case 38u:
                return PropId_Background;
            case 47u:
                return PropId_BorderThickness;
            default:
                return 0xFFFFu;
        }
    };

    for (std::size_t idx = 1u; idx < mainAb.objects.size();) {
        if (mainAb.objects[idx].typeKey != 31u) {
            ++idx;
            continue;
        }
        const RiveDecodedObject& animObj = mainAb.objects[idx];
        const std::string_view animName = animObj.GetStr(55u);
        const float fps = std::max(1.0f, static_cast<float>(animObj.GetU64(56u, 60u)));
        const std::uint64_t numFrames = animObj.GetU64(57u, 60u);
        const std::uint64_t loopVal = animObj.GetU64(59u, 0u);
        float maxKfTime = (numFrames > 0u) ? (static_cast<float>(numFrames) / fps) : 1.0f;

        AxbMotionClipDesc clip{};
        clip.nameStrIdx = pkg.InternString(animName);
        clip.isLooping = (loopVal == 1u || loopVal == 2u);
        clip.autoReverse = (loopVal == 2u);
        clip.firstTrackIdx = static_cast<std::uint32_t>(pkg.motionTracks.size());
        clip.firstEventIdx = static_cast<std::uint32_t>(pkg.motionEvents.size());

        std::uint32_t currentTargetElemIdx = 0xFFFFFFFFu;
        AxbMotionTrackDesc activeTrack{};
        bool hasActiveTrack = false;
        bool activeConvertRad = false;

        auto flushActiveTrack = [&]() {
            if (!hasActiveTrack) return;
            activeTrack.keyframeCount =
                static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - activeTrack.firstKeyframeIdx;
            if (activeTrack.keyframeCount > 0u) {
                pkg.motionTracks.push_back(activeTrack);
            }
            hasActiveTrack = false;
        };

        ++idx;
        while (idx < mainAb.objects.size()) {
            const RiveDecodedObject& sub = mainAb.objects[idx];
            if (sub.typeKey == 31u || sub.typeKey == 53u || sub.typeKey == 1u) {
                break;
            }
            if (sub.typeKey == 25u) {
                // KeyedObject
                flushActiveTrack();
                const auto objId = static_cast<std::size_t>(sub.GetU64(51u, 0xFFFFFFFFULL));
                if (objId < mainObjToElemIdx.size()) {
                    currentTargetElemIdx = mainObjToElemIdx[objId];
                    // If paint child (SolidColor) is keyed, resolve to parent Shape/Path element
                    if (currentTargetElemIdx == 0xFFFFFFFFu && objId < mainAb.objects.size()) {
                        std::size_t walk = objId;
                        for (int hop = 0; hop < 4 && walk < mainAb.objects.size(); ++hop) {
                            if (mainObjToElemIdx[walk] != 0xFFFFFFFFu) {
                                currentTargetElemIdx = mainObjToElemIdx[walk];
                                break;
                            }
                            const auto p = static_cast<std::size_t>(mainAb.objects[walk].GetU64(5u, 0xFFFFFFFFULL));
                            if (p >= mainAb.objects.size()) break;
                            walk = p;
                        }
                    }
                } else {
                    currentTargetElemIdx = 0xFFFFFFFFu;
                }
            } else if (sub.typeKey == 26u) {
                // KeyedProperty
                flushActiveTrack();
                const auto rivePropKey = static_cast<std::uint32_t>(sub.GetU64(53u, 0u));
                bool convRad = false;
                const std::uint16_t mappedProp = mapRiveKeyedProperty(rivePropKey, convRad);
                if (currentTargetElemIdx != 0xFFFFFFFFu &&
                    currentTargetElemIdx <= 0xFFFFu &&
                    mappedProp != 0xFFFFu) {
                    activeTrack = AxbMotionTrackDesc{};
                    activeTrack.localElementIdx = static_cast<std::uint16_t>(currentTargetElemIdx);
                    activeTrack.targetPropId = mappedProp;
                    activeTrack.channelCount = (mappedProp == PropId_Background || mappedProp == PropId_BorderThickness) ? 4u : 1u;
                    activeTrack.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());
                    activeConvertRad = convRad;
                    hasActiveTrack = true;
                }
            } else if ((sub.typeKey == 30u || sub.typeKey == 37u) && hasActiveTrack) {
                // KeyFrameDouble (30) or KeyFrameColor (37)
                AxbFlatKeyframe kf{};
                const auto frameIdx = sub.GetU64(67u, 0u);
                kf.timeSec = static_cast<float>(frameIdx) / fps;
                maxKfTime = std::max(maxKfTime, kf.timeSec);

                if (sub.typeKey == 37u) {
                    const Color kc = sub.GetArgbColor(88u, Color{1.0f, 1.0f, 1.0f, 1.0f});
                    kf.value = kc.ToFloat4();
                } else {
                    float val = sub.GetF32(70u, 0.0f);
                    if (activeConvertRad) {
                        val *= kRadToDeg;
                    }
                    if (activeTrack.targetPropId == PropId_BorderThickness) {
                        kf.value = Float4Value{val, val, val, val};
                    } else {
                        kf.value = Float4Value{val, 0.0f, 0.0f, 0.0f};
                    }
                }

                const std::uint64_t interpType = sub.GetU64(68u, 1u);
                if (interpType == 0u) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::HoldStep);
                } else if (interpType == 2u) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                    float x1 = 0.42f, y1 = 0.0f, x2 = 0.58f, y2 = 1.0f;
                    const auto interpId = static_cast<std::size_t>(sub.GetU64(69u, 0xFFFFFFFFULL));
                    if (interpId < mainAb.objects.size() && mainAb.objects[interpId].typeKey == 28u) {
                        const RiveDecodedObject& eo = mainAb.objects[interpId];
                        const float rx1 = eo.GetF32(63u, 0.42f);
                        const float ry1 = eo.GetF32(64u, 0.0f);
                        const float rx2 = eo.GetF32(65u, 0.58f);
                        const float ry2 = eo.GetF32(66u, 1.0f);
                        x1 = std::clamp(std::isfinite(rx1) ? rx1 : 0.42f, 0.0f, 1.0f);
                        y1 = std::clamp(std::isfinite(ry1) ? ry1 : 0.0f, -4.0f, 4.0f);
                        x2 = std::clamp(std::isfinite(rx2) ? rx2 : 0.58f, 0.0f, 1.0f);
                        y2 = std::clamp(std::isfinite(ry2) ? ry2 : 1.0f, -4.0f, 4.0f);
                    }
                    kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(x1 * 255.0f + 0.5f);
                    kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(x2 * 255.0f + 0.5f);
                    kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::lround(y1 * 256.0f));
                    kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::lround(y2 * 256.0f));
                } else {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
                }
                pkg.motionKeyframes.push_back(kf);
            }
            ++idx;
        }
        flushActiveTrack();

        clip.durationSec = std::max(0.016f, maxKfTime);
        clip.trackCount = static_cast<std::uint32_t>(pkg.motionTracks.size()) - clip.firstTrackIdx;
        clip.eventCount = static_cast<std::uint32_t>(pkg.motionEvents.size()) - clip.firstEventIdx;
        pkg.motionClips.push_back(clip);
    }

    // 5. Decode StateMachines (typeKey = 53) -> MOT6 StateMachines, Inputs, Layers, States & Transitions
    for (std::size_t idx = 1u; idx < mainAb.objects.size();) {
        if (mainAb.objects[idx].typeKey != 53u) {
            ++idx;
            continue;
        }
        const RiveDecodedObject& smObj = mainAb.objects[idx];
        AxbStateMachineDesc sm{};
        sm.nameStrIdx = pkg.InternString(smObj.GetStr(55u));
        sm.firstLayerIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        sm.firstListenerIdx = static_cast<std::uint32_t>(pkg.smListeners.size());

        const std::uint32_t smFirstInputSlot = static_cast<std::uint32_t>(pkg.contractInputs.size());
        std::vector<std::uint8_t> smInputKinds; // 0=Bool, 1=Float, 5=Trigger

        ++idx;
        // Parse StateMachine inputs (56=Number, 58=Trigger, 59=Bool) and Layers (57)
        while (idx < mainAb.objects.size() && mainAb.objects[idx].typeKey != 53u && mainAb.objects[idx].typeKey != 1u) {
            const RiveDecodedObject& item = mainAb.objects[idx];
            if (item.typeKey == 56u || item.typeKey == 58u || item.typeKey == 59u) {
                if (pkg.contractInputs.size() >= 0x1000u) {
                    if (outDiagnostics) {
                        outDiagnostics->push_back(
                            "AERO_RIVE_TOO_MANY_INPUTS: Rive contract input count exceeds maximum slot limit (4096)");
                    }
                    return false;
                }
                const auto globalSlot = static_cast<std::uint16_t>(pkg.contractInputs.size());
                AxbContractInputDesc inDesc{};
                inDesc.nameStrIdx = pkg.InternString(item.GetStr(138u));
                inDesc.bridgePropId = static_cast<std::uint16_t>(PropId_FirstContractInput + globalSlot);
                if (item.typeKey == 59u) {
                    inDesc.valueKind = 0u;
                    inDesc.defaultValue = UnifiedValue{item.GetU64(141u, 0u) != 0u};
                    smInputKinds.push_back(0u);
                } else if (item.typeKey == 58u) {
                    inDesc.valueKind = 5u;
                    inDesc.isTrigger = 1u;
                    inDesc.defaultValue = MakeFloat(0.0f);
                    smInputKinds.push_back(5u);
                } else {
                    inDesc.valueKind = 1u;
                    inDesc.defaultValue = MakeFloat(item.GetF32(140u, 0.0f));
                    smInputKinds.push_back(1u);
                }
                pkg.contractInputs.push_back(std::move(inDesc));
                ++idx;
                continue;
            }

            if (item.typeKey == 57u) {
                // StateMachineLayer
                AxbSmLayerDesc layer{};
                layer.nameStrIdx = pkg.InternString(item.GetStr(138u));
                layer.blendMode = SmLayerBlendMode::Override;
                layer.layerWeight = 1.0f;
                layer.firstStateIdx = static_cast<std::uint32_t>(pkg.smStates.size());
                layer.firstTransitionIdx = static_cast<std::uint32_t>(pkg.smTransitions.size());

                struct RawRiveState {
                    std::uint32_t tk = 0u;
                    std::uint16_t emittedPlayableIdx = 0xFFFFu;
                    struct RawTransition {
                        std::size_t toRawStateIdx = 0u;
                        float durationSec = 0.1f;
                        bool hasExitTime = false;
                        float exitTime01 = 1.0f;
                        std::uint32_t firstCondIdx = 0u;
                        std::uint16_t condCount = 0u;
                    };
                    std::vector<RawTransition> transitions;
                };
                std::vector<RawRiveState> rawStates;

                ++idx;
                while (idx < mainAb.objects.size()) {
                    const RiveDecodedObject& lo = mainAb.objects[idx];
                    if (lo.typeKey == 57u || lo.typeKey == 53u || lo.typeKey == 1u ||
                        lo.typeKey == 114u || lo.typeKey == 115u) {
                        break;
                    }
                    // LayerState subclasses: 61=AnimationState, 62=AnyState, 63=EntryState, 64=ExitState, 73/76=BlendState
                    if (lo.typeKey == 61u || lo.typeKey == 62u || lo.typeKey == 63u ||
                        lo.typeKey == 64u || lo.typeKey == 73u || lo.typeKey == 76u) {
                        RawRiveState rs{};
                        rs.tk = lo.typeKey;
                        if (lo.typeKey == 61u || lo.typeKey == 73u || lo.typeKey == 76u) {
                            const std::uint64_t rawClipId = lo.GetU64(149u, 0u);
                            const bool validClip =
                                (rawClipId < pkg.motionClips.size()) && (rawClipId < 0xFFFFu);
                            const std::uint16_t clipId = validClip
                                ? static_cast<std::uint16_t>(rawClipId)
                                : static_cast<std::uint16_t>(0xFFFFu);
                            AxbSmState st{};
                            st.nameStrIdx = pkg.InternString(
                                validClip
                                    ? pkg.GetString(pkg.motionClips[clipId].nameStrIdx)
                                    : "State");
                            st.kind = SmStateKind::SingleClip;
                            st.primaryClipIdx = clipId;
                            st.speedMultiplier = 1.0f;
                            if (pkg.smStates.size() < 0xFFFFu) {
                                rs.emittedPlayableIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                                pkg.smStates.push_back(st);
                            }
                        } else if (lo.typeKey == 64u) {
                            AxbSmState st{};
                            st.nameStrIdx = pkg.InternString("Exit");
                            st.kind = SmStateKind::ExitState;
                            if (pkg.smStates.size() < 0xFFFFu) {
                                rs.emittedPlayableIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                                pkg.smStates.push_back(st);
                            }
                        }
                        rawStates.push_back(std::move(rs));
                        ++idx;
                        continue;
                    }

                    if (lo.typeKey == 65u && !rawStates.empty()) {
                        // StateTransition
                        RawRiveState::RawTransition rt{};
                        rt.toRawStateIdx = static_cast<std::size_t>(lo.GetU64(151u, 0u));
                        rt.durationSec = static_cast<float>(lo.GetU64(158u, 0u)) * 0.001f;
                        const std::uint64_t trFlags = lo.GetU64(152u, 0u);
                        if ((trFlags & 0x2u) != 0u) {
                            rt.hasExitTime = true;
                            rt.exitTime01 = std::clamp(static_cast<float>(lo.GetU64(160u, 100u)) * 0.01f, 0.0f, 1.0f);
                        }
                        rt.firstCondIdx = static_cast<std::uint32_t>(pkg.smConditions.size());

                        ++idx;
                        while (idx < mainAb.objects.size()) {
                            const RiveDecodedObject& co = mainAb.objects[idx];
                            if (co.typeKey != 68u && co.typeKey != 70u && co.typeKey != 71u) {
                                break;
                            }
                            const auto localInId = static_cast<std::size_t>(co.GetU64(155u, 0u));
                            const std::size_t resolvedSlot =
                                static_cast<std::size_t>(smFirstInputSlot) + localInId;
                            if (localInId < smInputKinds.size() &&
                                resolvedSlot < 0x1000u &&
                                rt.condCount < 0xFFFFu) {
                                AxbSmCondition cond{};
                                cond.inputSlotIdx = static_cast<std::uint16_t>(resolvedSlot);
                                const std::uint8_t ik = smInputKinds[localInId];
                                const std::uint64_t opVal = co.GetU64(156u, 0u);
                                if (ik == 5u || co.typeKey == 68u) {
                                    cond.op = SmCompareOp::TriggerFired;
                                } else if (ik == 0u || co.typeKey == 71u) {
                                    cond.op = (opVal == 1u) ? SmCompareOp::BoolFalse : SmCompareOp::BoolTrue;
                                } else {
                                    // Rive TransitionValueCondition opValue: 0==, 1!=, 2<, 3<=, 4>, 5>=
                                    switch (opVal) {
                                        case 0u: cond.op = SmCompareOp::Equal; break;
                                        case 1u: cond.op = SmCompareOp::NotEqual; break;
                                        case 2u: cond.op = SmCompareOp::Less; break;
                                        case 3u: cond.op = SmCompareOp::LessOrEqual; break;
                                        case 4u: cond.op = SmCompareOp::Greater; break;
                                        case 5u: cond.op = SmCompareOp::GreaterOrEqual; break;
                                        default: cond.op = SmCompareOp::Equal; break;
                                    }
                                    cond.threshold = co.GetF32(157u, 0.0f);
                                }
                                pkg.smConditions.push_back(cond);
                                ++rt.condCount;
                            }
                            ++idx;
                        }
                        rawStates.back().transitions.push_back(rt);
                        continue;
                    }
                    ++idx;
                }

                // Resolve EntryState default transition & playable state transitions
                std::uint16_t resolvedEntryGlobal = static_cast<std::uint16_t>(layer.firstStateIdx);
                for (const auto& rs : rawStates) {
                    if (rs.tk == 63u && !rs.transitions.empty()) {
                        const std::size_t targetRaw = rs.transitions[0].toRawStateIdx;
                        if (targetRaw < rawStates.size() && rawStates[targetRaw].emittedPlayableIdx != 0xFFFFu) {
                            resolvedEntryGlobal = rawStates[targetRaw].emittedPlayableIdx;
                        }
                    }
                }
                for (const auto& rs : rawStates) {
                    if (rs.tk == 63u) continue;
                    const std::uint16_t fromIdx = (rs.tk == 62u)
                        ? static_cast<std::uint16_t>(0xFFFFu)
                        : static_cast<std::uint16_t>(rs.emittedPlayableIdx - layer.firstStateIdx);
                    if (rs.tk != 62u && rs.emittedPlayableIdx == 0xFFFFu) continue;

                    for (const auto& rt : rs.transitions) {
                        if (rt.toRawStateIdx >= rawStates.size()) continue;
                        const std::uint16_t toGlobal = rawStates[rt.toRawStateIdx].emittedPlayableIdx;
                        if (toGlobal == 0xFFFFu) continue;
                        AxbSmTransition tr{};
                        tr.fromStateIdx = fromIdx;
                        tr.toStateIdx = static_cast<std::uint16_t>(toGlobal - layer.firstStateIdx);
                        tr.crossfadeDurationSec = rt.durationSec;
                        tr.hasExitTime = rt.hasExitTime;
                        tr.exitTimeNormalized01 = rt.exitTime01;
                        tr.firstConditionIdx = rt.firstCondIdx;
                        tr.conditionCount = rt.condCount;
                        pkg.smTransitions.push_back(tr);
                    }
                }

                layer.stateCount = static_cast<std::uint16_t>(pkg.smStates.size() - layer.firstStateIdx);
                layer.transitionCount = static_cast<std::uint16_t>(pkg.smTransitions.size() - layer.firstTransitionIdx);
                if (layer.stateCount > 0u) {
                    layer.entryStateIdx = (resolvedEntryGlobal >= layer.firstStateIdx &&
                                           resolvedEntryGlobal < layer.firstStateIdx + layer.stateCount)
                        ? static_cast<std::uint16_t>(resolvedEntryGlobal - layer.firstStateIdx)
                        : static_cast<std::uint16_t>(0u);
                    pkg.smLayers.push_back(layer);
                }
                continue;
            }

            ++idx;
        }

        sm.layerCount = static_cast<std::uint16_t>(pkg.smLayers.size() - sm.firstLayerIdx);
        sm.listenerCount = static_cast<std::uint16_t>(pkg.smListeners.size() - sm.firstListenerIdx);
        if (sm.layerCount > 0u) {
            pkg.stateMachines.push_back(sm);
        }
    }

    // If the .riv file contains timeline animations but no explicit StateMachine (e.g. leaves-*.riv),
    // synthesize a default single-layer StateMachine playing clip 0 automatically.
    if (pkg.stateMachines.empty() && !pkg.motionClips.empty()) {
        AxbSmState defState{};
        defState.nameStrIdx = pkg.motionClips[0].nameStrIdx;
        defState.kind = SmStateKind::SingleClip;
        defState.primaryClipIdx = 0u;
        defState.speedMultiplier = 1.0f;
        const auto firstStIdx = static_cast<std::uint32_t>(pkg.smStates.size());
        pkg.smStates.push_back(defState);

        AxbSmLayerDesc defLayer{};
        defLayer.nameStrIdx = pkg.InternString("DefaultTimeline");
        defLayer.blendMode = SmLayerBlendMode::Override;
        defLayer.entryStateIdx = 0u;
        defLayer.layerWeight = 1.0f;
        defLayer.firstStateIdx = firstStIdx;
        defLayer.stateCount = 1u;
        const auto firstLyIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        pkg.smLayers.push_back(defLayer);

        AxbStateMachineDesc defSm{};
        defSm.nameStrIdx = pkg.InternString("AutoPlayback");
        defSm.firstLayerIdx = firstLyIdx;
        defSm.layerCount = 1u;
        pkg.stateMachines.push_back(defSm);
    }

    pkg.PinValuesInPools();
    outPackage = std::move(pkg);
    return true;
}

} // namespace aero
