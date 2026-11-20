#include "aero/render/RiveRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aero {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct RivPropVal {
    std::uint64_t u = 0u;
    float f = 0.0f;
    std::string s;
    std::uint8_t kind = 0u; // 0=uint, 1=str, 2=float, 3=color
};

struct RivObject {
    std::uint32_t typeKey = 0u;
    std::unordered_map<std::uint32_t, RivPropVal> props;

    [[nodiscard]] bool Has(std::uint32_t k) const noexcept {
        return props.find(k) != props.end();
    }
    [[nodiscard]] std::uint32_t GetU(std::uint32_t k, std::uint32_t def = 0u) const noexcept {
        const auto it = props.find(k);
        return (it != props.end()) ? static_cast<std::uint32_t>(it->second.u) : def;
    }
    [[nodiscard]] float GetF(std::uint32_t k, float def = 0.0f) const noexcept {
        const auto it = props.find(k);
        return (it != props.end()) ? it->second.f : def;
    }
    [[nodiscard]] std::string_view GetS(std::uint32_t k) const noexcept {
        const auto it = props.find(k);
        return (it != props.end()) ? std::string_view(it->second.s) : std::string_view{};
    }
};

struct RivMat2D {
    float xx = 1.0f, xy = 0.0f;
    float yx = 0.0f, yy = 1.0f;
    float tx = 0.0f, ty = 0.0f;

    static RivMat2D Multiply(const RivMat2D& a, const RivMat2D& b) noexcept {
        RivMat2D r{};
        r.xx = a.xx * b.xx + a.yx * b.xy;
        r.xy = a.xy * b.xx + a.yy * b.xy;
        r.yx = a.xx * b.yx + a.yx * b.yy;
        r.yy = a.xy * b.yx + a.yy * b.yy;
        r.tx = a.xx * b.tx + a.yx * b.ty + a.tx;
        r.ty = a.xy * b.tx + a.yy * b.ty + a.ty;
        return r;
    }

    static RivMat2D FromTRS(float x, float y, float rotRad, float sx, float sy) noexcept {
        const float c = std::cos(rotRad);
        const float s = std::sin(rotRad);
        RivMat2D m{};
        m.xx = c * sx;
        m.xy = s * sx;
        m.yx = -s * sy;
        m.yy = c * sy;
        m.tx = x;
        m.ty = y;
        return m;
    }

    static RivMat2D Invert(const RivMat2D& m) noexcept {
        const float det = m.xx * m.yy - m.xy * m.yx;
        if (std::fabs(det) < 1e-8f) {
            return RivMat2D{};
        }
        const float id = 1.0f / det;
        RivMat2D r{};
        r.xx = m.yy * id;
        r.xy = -m.xy * id;
        r.yx = -m.yx * id;
        r.yy = m.xx * id;
        r.tx = -(r.xx * m.tx + r.yx * m.ty);
        r.ty = -(r.xy * m.tx + r.yy * m.ty);
        return r;
    }

    void Transform(float px, float py, float& ox, float& oy) const noexcept {
        ox = xx * px + yx * py + tx;
        oy = xy * px + yy * py + ty;
    }
};

struct RivVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct RivSubpath {
    std::vector<RivVec2> pts;
    bool closed = true;
};

struct RivGradStop {
    float pos = 0.0f;
    std::uint32_t argb = 0xFFFFFFFFu;
};

enum class RivPaintStyle : std::uint8_t {
    None = 0,
    Solid = 1,
    Linear = 2,
    Radial = 3,
};

struct RivPaint {
    bool isStroke = false;
    bool evenOdd = false;
    float strokeThickness = 1.0f;
    std::uint32_t strokeCap = 0u;
    std::uint32_t strokeJoin = 0u;
    bool transformAffectsStroke = true;
    RivPaintStyle style = RivPaintStyle::None;
    std::uint32_t solidArgb = 0xFFFFFFFFu;
    RivVec2 gradStart{};
    RivVec2 gradEnd{};
    float gradOpacity = 1.0f;
    std::vector<RivGradStop> stops;
};

bool ReadVarUint(const std::uint8_t* data, std::size_t size, std::size_t& pos, std::uint64_t& out) noexcept {
    out = 0u;
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

const std::unordered_map<std::uint32_t, std::uint8_t>& CorePropBackingTypes() {
    static const std::unordered_map<std::uint32_t, std::uint8_t> kMap = {
        {4,1},{5,0},{7,2},{8,2},{9,2},{10,2},{11,2},{12,2},{13,2},{14,2},{15,2},{16,2},{17,2},{18,2},
        {20,2},{21,2},{23,0},{24,2},{25,2},{26,2},{27,2},{28,2},{29,2},{31,2},{32,0},{33,2},{34,2},{35,2},
        {37,3},{38,3},{39,2},{40,0},{41,0},{42,2},{46,2},{47,2},{48,0},{49,0},{50,0},{51,0},{53,0},{55,1},
        {56,0},{57,0},{58,2},{59,0},{60,0},{61,0},{62,0},{63,2},{64,2},{65,2},{66,2},{67,0},{68,0},{69,0},
        {70,2},{78,2},{79,2},{80,2},{81,2},{82,2},{83,2},{84,2},{85,2},{86,2},{87,2},{88,3},{89,2},{90,2},
        {91,2},{92,0},{93,0},{94,0},{95,0},{96,2},{97,2},{98,2},{99,2},{100,2},{101,2},{102,0},{103,0},
        {104,2},{105,2},{106,2},{107,2},{108,2},{109,2},{110,0},{111,0},{112,0},{113,0},{114,2},{115,2},
        {116,2},{117,0},{119,0},{120,0},{121,0},{122,0},{123,2},{124,2},{125,0},{126,2},{127,2},{128,0},
        {129,0},{138,1},{140,2},{141,0},{149,0},{151,0},{152,0},{153,0},{155,0},{156,0},{157,2},{158,0},
        {161,2},{162,2},{163,2},{164,0},{181,0},{196,0},{197,0},{198,0},{236,0}
    };
    return kMap;
}

bool ParseRiveFileBytes(const std::vector<std::uint8_t>& bytes, std::vector<RivObject>& outObjects) {
    outObjects.clear();
    if (bytes.size() < 6u || std::memcmp(bytes.data(), "RIVE", 4u) != 0) {
        return false;
    }
    std::size_t pos = 4u;
    std::uint64_t major = 0u, minor = 0u, fileId = 0u;
    if (!ReadVarUint(bytes.data(), bytes.size(), pos, major) || major != 7u ||
        !ReadVarUint(bytes.data(), bytes.size(), pos, minor) ||
        !ReadVarUint(bytes.data(), bytes.size(), pos, fileId)) {
        return false;
    }
    std::vector<std::uint32_t> tocKeys;
    while (pos < bytes.size()) {
        std::uint64_t k = 0u;
        if (!ReadVarUint(bytes.data(), bytes.size(), pos, k)) return false;
        if (k == 0u) break;
        tocKeys.push_back(static_cast<std::uint32_t>(k));
    }
    std::unordered_map<std::uint32_t, std::uint8_t> propTypes = CorePropBackingTypes();
    std::uint32_t curBits = 0u;
    for (std::size_t i = 0u; i < tocKeys.size(); ++i) {
        if ((i % 16u) == 0u) {
            if (pos + 4u > bytes.size()) return false;
            std::memcpy(&curBits, bytes.data() + pos, 4u);
            pos += 4u;
        }
        const std::uint8_t bt = static_cast<std::uint8_t>((curBits >> ((i % 16u) * 2u)) & 3u);
        propTypes[tocKeys[i]] = bt;
    }

    while (pos < bytes.size()) {
        std::uint64_t tk = 0u;
        if (!ReadVarUint(bytes.data(), bytes.size(), pos, tk)) break;
        RivObject obj{};
        obj.typeKey = static_cast<std::uint32_t>(tk);
        while (pos < bytes.size()) {
            std::uint64_t pk = 0u;
            if (!ReadVarUint(bytes.data(), bytes.size(), pos, pk)) return false;
            if (pk == 0u) break;
            const auto it = propTypes.find(static_cast<std::uint32_t>(pk));
            if (it == propTypes.end()) {
                return !outObjects.empty();
            }
            RivPropVal pv{};
            pv.kind = it->second;
            if (pv.kind == 0u) {
                if (!ReadVarUint(bytes.data(), bytes.size(), pos, pv.u)) return false;
                pv.f = static_cast<float>(pv.u);
            } else if (pv.kind == 1u) {
                std::uint64_t slen = 0u;
                if (!ReadVarUint(bytes.data(), bytes.size(), pos, slen) ||
                    slen > bytes.size() - pos ||
                    slen > 16u * 1024u * 1024u) {
                    return false;
                }
                pv.s.assign(reinterpret_cast<const char*>(bytes.data() + pos), static_cast<std::size_t>(slen));
                pos += static_cast<std::size_t>(slen);
            } else if (pv.kind == 2u) {
                if (pos + 4u > bytes.size()) return false;
                std::memcpy(&pv.f, bytes.data() + pos, 4u);
                pos += 4u;
            } else {
                if (pos + 4u > bytes.size()) return false;
                std::uint32_t col = 0u;
                std::memcpy(&col, bytes.data() + pos, 4u);
                pos += 4u;
                pv.u = col;
            }
            obj.props[static_cast<std::uint32_t>(pk)] = std::move(pv);
        }
        outObjects.push_back(std::move(obj));
    }
    return !outObjects.empty();
}

void FlattenCubic(
    RivVec2 p0, RivVec2 p1, RivVec2 p2, RivVec2 p3,
    int depth, std::vector<RivVec2>& out) {
    const float dx = p3.x - p0.x;
    const float dy = p3.y - p0.y;
    const float d2 = std::fabs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    const float d3 = std::fabs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    if (depth >= 7 || (d2 + d3) * (d2 + d3) <= 0.16f * (dx * dx + dy * dy + 1.0f)) {
        out.push_back(p3);
        return;
    }
    const RivVec2 p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    const RivVec2 p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
    const RivVec2 p23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
    const RivVec2 p012{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
    const RivVec2 p123{(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f};
    const RivVec2 p0123{(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f};
    FlattenCubic(p0, p01, p012, p0123, depth + 1, out);
    FlattenCubic(p0123, p123, p23, p3, depth + 1, out);
}

struct RivVertDesc {
    RivVec2 pos{};
    RivVec2 inPt{};
    RivVec2 outPt{};
    float straightRadius = 0.0f;
    bool isCubic = false;
};

RivSubpath BuildRoundedRectSubpath(float ox, float oy, float w, float h, float rTL, float rTR, float rBR, float rBL) {
    RivSubpath sp{};
    sp.closed = true;
    const float x0 = -ox * w;
    const float y0 = -oy * h;
    const float x1 = x0 + w;
    const float y1 = y0 + h;
    const float maxR = 0.5f * std::min(std::fabs(w), std::fabs(h));
    rTL = std::clamp(rTL, 0.0f, maxR);
    rTR = std::clamp(rTR, 0.0f, maxR);
    rBR = std::clamp(rBR, 0.0f, maxR);
    rBL = std::clamp(rBL, 0.0f, maxR);
    constexpr float kK = 0.55228475f;
    sp.pts.push_back({x0 + rTL, y0});
    sp.pts.push_back({x1 - rTR, y0});
    if (rTR > 0.05f) FlattenCubic({x1 - rTR, y0}, {x1 - rTR * (1.0f - kK), y0}, {x1, y0 + rTR * (1.0f - kK)}, {x1, y0 + rTR}, 0, sp.pts);
    sp.pts.push_back({x1, y1 - rBR});
    if (rBR > 0.05f) FlattenCubic({x1, y1 - rBR}, {x1, y1 - rBR * (1.0f - kK)}, {x1 - rBR * (1.0f - kK), y1}, {x1 - rBR, y1}, 0, sp.pts);
    sp.pts.push_back({x0 + rBL, y1});
    if (rBL > 0.05f) FlattenCubic({x0 + rBL, y1}, {x0 + rBL * (1.0f - kK), y1}, {x0, y1 - rBL * (1.0f - kK)}, {x0, y1 - rBL}, 0, sp.pts);
    sp.pts.push_back({x0, y0 + rTL});
    if (rTL > 0.05f) FlattenCubic({x0, y0 + rTL}, {x0, y0 + rTL * (1.0f - kK)}, {x0 + rTL * (1.0f - kK), y0}, {x0 + rTL, y0}, 0, sp.pts);
    return sp;
}

RivSubpath BuildEllipseSubpath(float ox, float oy, float w, float h) {
    RivSubpath sp{};
    sp.closed = true;
    const float cx = (0.5f - ox) * w;
    const float cy = (0.5f - oy) * h;
    const float rx = 0.5f * w;
    const float ry = 0.5f * h;
    constexpr float kK = 0.55228475f;
    sp.pts.push_back({cx + rx, cy});
    FlattenCubic({cx + rx, cy}, {cx + rx, cy + ry * kK}, {cx + rx * kK, cy + ry}, {cx, cy + ry}, 0, sp.pts);
    FlattenCubic({cx, cy + ry}, {cx - rx * kK, cy + ry}, {cx - rx, cy + ry * kK}, {cx - rx, cy}, 0, sp.pts);
    FlattenCubic({cx - rx, cy}, {cx - rx, cy - ry * kK}, {cx - rx * kK, cy - ry}, {cx, cy - ry}, 0, sp.pts);
    FlattenCubic({cx, cy - ry}, {cx + rx * kK, cy - ry}, {cx + rx, cy - ry * kK}, {cx + rx, cy}, 0, sp.pts);
    return sp;
}

void ExpandStrokeToSubpaths(
    const std::vector<RivSubpath>& src,
    float halfW,
    std::uint32_t cap,
    std::vector<RivSubpath>& out) {
    if (halfW <= 0.02f) return;
    for (const RivSubpath& sp : src) {
        if (sp.pts.size() < 2u) continue;
        std::vector<RivVec2> clean;
        clean.reserve(sp.pts.size());
        for (const RivVec2& p : sp.pts) {
            if (clean.empty() || std::hypot(p.x - clean.back().x, p.y - clean.back().y) > 0.02f) {
                clean.push_back(p);
            }
        }
        if (sp.closed && clean.size() >= 2u &&
            std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) <= 0.02f) {
            clean.pop_back();
        }
        if (clean.size() < 2u) continue;

        const std::size_t n = clean.size();
        const std::size_t segCount = sp.closed ? n : (n - 1u);
        std::vector<RivVec2> normals(segCount);
        for (std::size_t i = 0u; i < segCount; ++i) {
            const RivVec2 a = clean[i];
            const RivVec2 b = clean[(i + 1u) % n];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::max(1e-4f, std::hypot(dx, dy));
            normals[i] = {-dy / len, dx / len};
        }

        RivSubpath poly{};
        poly.closed = true;
        std::vector<RivVec2> leftSide;
        std::vector<RivVec2> rightSide;
        leftSide.reserve(n * 3u);
        rightSide.reserve(n * 3u);

        auto addJoin = [&](RivVec2 pt, RivVec2 n0, RivVec2 n1, std::vector<RivVec2>& side, float sign) {
            RivVec2 m{n0.x + n1.x, n0.y + n1.y};
            const float mlen2 = m.x * m.x + m.y * m.y;
            if (mlen2 > 0.25f) {
                const float scale = std::min(2.2f, 2.0f / mlen2);
                side.push_back({pt.x + sign * m.x * scale * halfW, pt.y + sign * m.y * scale * halfW});
            } else {
                side.push_back({pt.x + sign * n0.x * halfW, pt.y + sign * n0.y * halfW});
                side.push_back({pt.x + sign * n1.x * halfW, pt.y + sign * n1.y * halfW});
            }
        };

        for (std::size_t i = 0u; i < n; ++i) {
            if (!sp.closed && i == 0u) {
                const RivVec2 n0 = normals[0];
                leftSide.push_back({clean[0].x + n0.x * halfW, clean[0].y + n0.y * halfW});
                rightSide.push_back({clean[0].x - n0.x * halfW, clean[0].y - n0.y * halfW});
            } else if (!sp.closed && i == n - 1u) {
                const RivVec2 n0 = normals[segCount - 1u];
                leftSide.push_back({clean[i].x + n0.x * halfW, clean[i].y + n0.y * halfW});
                rightSide.push_back({clean[i].x - n0.x * halfW, clean[i].y - n0.y * halfW});
            } else {
                const RivVec2 nPrev = normals[(i + segCount - 1u) % segCount];
                const RivVec2 nNext = normals[i % segCount];
                addJoin(clean[i], nPrev, nNext, leftSide, 1.0f);
                addJoin(clean[i], nPrev, nNext, rightSide, -1.0f);
            }
        }

        if (sp.closed) {
            poly.pts = std::move(leftSide);
            out.push_back(std::move(poly));
            RivSubpath inner{};
            inner.closed = true;
            std::reverse(rightSide.begin(), rightSide.end());
            inner.pts = std::move(rightSide);
            out.push_back(std::move(inner));
        } else {
            poly.pts = std::move(leftSide);
            if (cap == 1u) {
                const RivVec2 pEnd = clean.back();
                const RivVec2 nEnd = normals.back();
                const float baseAng = std::atan2(nEnd.y, nEnd.x);
                for (int s = 1; s < 6; ++s) {
                    const float a = baseAng - kPi * (static_cast<float>(s) / 6.0f);
                    poly.pts.push_back({pEnd.x + std::cos(a) * halfW, pEnd.y + std::sin(a) * halfW});
                }
            }
            for (std::size_t k = rightSide.size(); k > 0u; --k) {
                poly.pts.push_back(rightSide[k - 1u]);
            }
            if (cap == 1u) {
                const RivVec2 pStart = clean.front();
                const RivVec2 nStart = normals.front();
                const float baseAng = std::atan2(-nStart.y, -nStart.x);
                for (int s = 1; s < 6; ++s) {
                    const float a = baseAng - kPi * (static_cast<float>(s) / 6.0f);
                    poly.pts.push_back({pStart.x + std::cos(a) * halfW, pStart.y + std::sin(a) * halfW});
                }
            }
            out.push_back(std::move(poly));
        }
    }
}

void AppendTrimmedSubpathSegment(
    const RivSubpath& sp,
    const std::vector<float>& cumDist,
    float d0,
    float d1,
    std::vector<RivSubpath>& out) {
    const std::size_t n = sp.pts.size();
    const std::size_t segCount = sp.closed ? n : (n - 1u);
    if (segCount == 0u || cumDist.size() != segCount + 1u) return;
    const float totalLen = cumDist.back();
    const float c0 = std::clamp(d0, 0.0f, totalLen);
    const float c1 = std::clamp(d1, 0.0f, totalLen);
    if (c1 - c0 <= 1e-4f) return;

    auto evalAt = [&](float dist, std::size_t& outSegIdx) noexcept -> RivVec2 {
        for (std::size_t s = 0u; s < segCount; ++s) {
            if (dist <= cumDist[s + 1u] || s + 1u == segCount) {
                outSegIdx = s;
                const float s0 = cumDist[s];
                const float s1 = cumDist[s + 1u];
                const float segLen = s1 - s0;
                const float t = (segLen > 1e-6f) ? std::clamp((dist - s0) / segLen, 0.0f, 1.0f) : 0.0f;
                const RivVec2 a = sp.pts[s];
                const RivVec2 b = sp.pts[(s + 1u) % n];
                return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            }
        }
        outSegIdx = segCount - 1u;
        return sp.pts[sp.closed ? 0u : (n - 1u)];
    };

    std::size_t segStart = 0u;
    std::size_t segEnd = 0u;
    const RivVec2 pStart = evalAt(c0, segStart);
    const RivVec2 pEnd = evalAt(c1, segEnd);

    RivSubpath seg{};
    seg.closed = false;
    seg.pts.push_back(pStart);
    for (std::size_t v = segStart + 1u; v <= segEnd && v < n; ++v) {
        if (cumDist[v] > c0 + 1e-5f && cumDist[v] < c1 - 1e-5f) {
            seg.pts.push_back(sp.pts[v]);
        }
    }
    if (std::hypot(pEnd.x - seg.pts.back().x, pEnd.y - seg.pts.back().y) > 1e-5f) {
        seg.pts.push_back(pEnd);
    }
    if (seg.pts.size() >= 2u) {
        out.push_back(std::move(seg));
    }
}

void TrimRivSubpaths(
    const std::vector<RivSubpath>& src,
    float trimStart,
    float trimEnd,
    float trimOffset,
    std::uint32_t trimMode,
    std::vector<RivSubpath>& out) {
    out.clear();
    const float lo = std::min(trimStart, trimEnd);
    const float hi = std::max(trimStart, trimEnd);
    const float span = hi - lo;
    if (span <= 1e-4f) {
        return;
    }
    if (span >= 1.0f - 1e-4f) {
        out = src;
        return;
    }

    const float shiftedStart = lo + trimOffset;
    const float ws = shiftedStart - std::floor(shiftedStart);
    const float we = ws + span;
    std::pair<float, float> intervals[2];
    int intervalCount = 0;
    intervals[intervalCount++] = {ws, std::min(1.0f, we)};
    if (we > 1.0f + 1e-4f) {
        intervals[intervalCount++] = {0.0f, std::min(1.0f, we - 1.0f)};
    }

    std::vector<std::vector<float>> allCumDist(src.size());
    float grandTotal = 0.0f;
    for (std::size_t idx = 0u; idx < src.size(); ++idx) {
        const RivSubpath& sp = src[idx];
        const std::size_t n = sp.pts.size();
        const std::size_t segCount = sp.closed ? n : (n > 0u ? (n - 1u) : 0u);
        auto& cd = allCumDist[idx];
        cd.assign(segCount + 1u, 0.0f);
        for (std::size_t s = 0u; s < segCount; ++s) {
            const RivVec2 a = sp.pts[s];
            const RivVec2 b = sp.pts[(s + 1u) % n];
            cd[s + 1u] = cd[s] + std::hypot(b.x - a.x, b.y - a.y);
        }
        grandTotal += cd.back();
    }

    if (trimMode == 2u) {
        if (grandTotal <= 1e-4f) return;
        for (int ii = 0; ii < intervalCount; ++ii) {
            const float targetStart = intervals[ii].first * grandTotal;
            const float targetEnd = intervals[ii].second * grandTotal;
            float cursor = 0.0f;
            for (std::size_t idx = 0u; idx < src.size(); ++idx) {
                const float subLen = allCumDist[idx].back();
                const float subEnd = cursor + subLen;
                const float ov0 = std::max(targetStart, cursor);
                const float ov1 = std::min(targetEnd, subEnd);
                if (ov1 - ov0 > 1e-4f) {
                    AppendTrimmedSubpathSegment(src[idx], allCumDist[idx], ov0 - cursor, ov1 - cursor, out);
                }
                cursor = subEnd;
            }
        }
    } else {
        for (std::size_t idx = 0u; idx < src.size(); ++idx) {
            const float subLen = allCumDist[idx].back();
            if (subLen <= 1e-4f) continue;
            for (int ii = 0; ii < intervalCount; ++ii) {
                AppendTrimmedSubpathSegment(
                    src[idx], allCumDist[idx],
                    intervals[ii].first * subLen,
                    intervals[ii].second * subLen,
                    out);
            }
        }
    }
}

void UnpackArgbPremul(std::uint32_t argb, float extraAlpha, float outPremul[4]) noexcept {
    const float a = std::clamp((static_cast<float>((argb >> 24u) & 0xFFu) / 255.0f) * extraAlpha, 0.0f, 1.0f);
    const float r = static_cast<float>((argb >> 16u) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((argb >> 8u) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(argb & 0xFFu) / 255.0f;
    outPremul[0] = r * a;
    outPremul[1] = g * a;
    outPremul[2] = b * a;
    outPremul[3] = a;
}

void SampleStopsPremul(const std::vector<RivGradStop>& stops, float t, float extraAlpha, float outPremul[4]) noexcept {
    if (stops.empty()) {
        outPremul[0] = outPremul[1] = outPremul[2] = outPremul[3] = 0.0f;
        return;
    }
    const float ct = std::clamp(t, 0.0f, 1.0f);
    if (ct <= stops.front().pos || stops.size() == 1u) {
        UnpackArgbPremul(stops.front().argb, extraAlpha, outPremul);
        return;
    }
    if (ct >= stops.back().pos) {
        UnpackArgbPremul(stops.back().argb, extraAlpha, outPremul);
        return;
    }
    for (std::size_t i = 0u; i + 1u < stops.size(); ++i) {
        if (ct >= stops[i].pos && ct <= stops[i + 1u].pos) {
            const float span = std::max(1e-5f, stops[i + 1u].pos - stops[i].pos);
            const float f = (ct - stops[i].pos) / span;
            float c0[4], c1[4];
            UnpackArgbPremul(stops[i].argb, extraAlpha, c0);
            UnpackArgbPremul(stops[i + 1u].argb, extraAlpha, c1);
            for (int k = 0; k < 4; ++k) {
                outPremul[k] = c0[k] + (c1[k] - c0[k]) * f;
            }
            return;
        }
    }
    UnpackArgbPremul(stops.back().argb, extraAlpha, outPremul);
}

struct RasterEdge {
    float x0, y0, x1, y1;
    int dir;
};

void BuildEdges(const std::vector<RivSubpath>& subpaths, std::vector<RasterEdge>& edges, float& minX, float& minY, float& maxX, float& maxY) {
    edges.clear();
    minX = minY = 1e9f;
    maxX = maxY = -1e9f;
    for (const RivSubpath& sp : subpaths) {
        if (sp.pts.size() < 2u) continue;
        const std::size_t n = sp.pts.size();
        for (std::size_t i = 0u; i < n; ++i) {
            const RivVec2 a = sp.pts[i];
            const RivVec2 b = sp.pts[(i + 1u) % n];
            minX = std::min(minX, std::min(a.x, b.x));
            minY = std::min(minY, std::min(a.y, b.y));
            maxX = std::max(maxX, std::max(a.x, b.x));
            maxY = std::max(maxY, std::max(a.y, b.y));
            if (std::fabs(a.y - b.y) < 1e-5f) continue;
            if (a.y < b.y) {
                edges.push_back({a.x, a.y, b.x, b.y, 1});
            } else {
                edges.push_back({b.x, b.y, a.x, a.y, -1});
            }
        }
    }
}

void RasterizeCoverageMask(
    int surfW, int surfH,
    const std::vector<RivSubpath>& subpaths,
    bool evenOdd,
    int& outX0, int& outY0, int& outW, int& outH,
    std::vector<float>& outMask) {
    std::vector<RasterEdge> edges;
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    BuildEdges(subpaths, edges, minX, minY, maxX, maxY);
    if (edges.empty()) {
        outW = outH = 0;
        return;
    }
    outX0 = std::clamp(static_cast<int>(std::floor(minX)), 0, surfW);
    outY0 = std::clamp(static_cast<int>(std::floor(minY)), 0, surfH);
    const int x1 = std::clamp(static_cast<int>(std::ceil(maxX)), 0, surfW);
    const int y1 = std::clamp(static_cast<int>(std::ceil(maxY)), 0, surfH);
    outW = x1 - outX0;
    outH = y1 - outY0;
    if (outW <= 0 || outH <= 0) {
        outW = outH = 0;
        return;
    }
    outMask.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH), 0.0f);

    constexpr int kSubY = 4;
    static constexpr float kOffsetsY[kSubY] = {0.125f, 0.375f, 0.625f, 0.875f};
    std::vector<std::pair<float, int>> xings;
    xings.reserve(64u);
    std::vector<float> rowCov(static_cast<std::size_t>(outW), 0.0f);

    for (int py = outY0; py < y1; ++py) {
        std::fill(rowCov.begin(), rowCov.end(), 0.0f);
        for (int sy = 0; sy < kSubY; ++sy) {
            const float sampleY = static_cast<float>(py) + kOffsetsY[sy];
            xings.clear();
            for (const RasterEdge& e : edges) {
                if (sampleY >= e.y0 && sampleY < e.y1) {
                    const float t = (sampleY - e.y0) / (e.y1 - e.y0);
                    xings.emplace_back(e.x0 + (e.x1 - e.x0) * t, e.dir);
                }
            }
            if (xings.size() < 2u) continue;
            std::sort(xings.begin(), xings.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            int winding = 0;
            float spanStart = 0.0f;
            bool inside = false;
            for (const auto& [xv, dir] : xings) {
                const bool wasInside = inside;
                winding += dir;
                inside = evenOdd ? ((winding & 1) != 0) : (winding != 0);
                if (!wasInside && inside) {
                    spanStart = xv;
                } else if (wasInside && !inside) {
                    const float s0 = std::max(spanStart, static_cast<float>(outX0));
                    const float s1 = std::min(xv, static_cast<float>(x1));
                    if (s1 > s0) {
                        const int ix0 = std::clamp(static_cast<int>(std::floor(s0)), outX0, x1 - 1);
                        const int ix1 = std::clamp(static_cast<int>(std::floor(s1)), outX0, x1 - 1);
                        if (ix0 == ix1) {
                            rowCov[static_cast<std::size_t>(ix0 - outX0)] += (s1 - s0) * (1.0f / kSubY);
                        } else {
                            rowCov[static_cast<std::size_t>(ix0 - outX0)] += (static_cast<float>(ix0 + 1) - s0) * (1.0f / kSubY);
                            for (int ix = ix0 + 1; ix < ix1; ++ix) {
                                rowCov[static_cast<std::size_t>(ix - outX0)] += (1.0f / kSubY);
                            }
                            rowCov[static_cast<std::size_t>(ix1 - outX0)] += (s1 - static_cast<float>(ix1)) * (1.0f / kSubY);
                        }
                    }
                }
            }
        }
        float* dst = outMask.data() + static_cast<std::size_t>(py - outY0) * static_cast<std::size_t>(outW);
        std::memcpy(dst, rowCov.data(), static_cast<std::size_t>(outW) * sizeof(float));
    }
}

struct RivArtboardSlice {
    std::size_t startIdx = 0u;
    std::size_t endIdx = 0u;
};

void RenderArtboardRecursive(
    const std::vector<RivObject>& allObjs,
    const std::vector<RivArtboardSlice>& abSlices,
    std::size_t abIndex,
    const RivMat2D& rootTransform,
    float inheritedOpacity,
    const std::unordered_map<std::string, float>& inputs,
    int surfW, int surfH,
    std::vector<float>& surfPremul,
    int depth) {
    if (abIndex >= abSlices.size() || depth > 5) return;
    const std::size_t s0 = abSlices[abIndex].startIdx;
    const std::size_t s1 = abSlices[abIndex].endIdx;
    if (s1 <= s0) return;

    std::size_t rawCompEnd = s1;
    for (std::size_t idx = s0 + 1u; idx < s1; ++idx) {
        if (allObjs[idx].typeKey == 31u || allObjs[idx].typeKey == 53u) {
            rawCompEnd = idx;
            break;
        }
    }
    std::vector<RivObject> objs;
    objs.reserve(s1 - s0);
    for (std::size_t idx = s0; idx < rawCompEnd; ++idx) {
        const std::uint32_t tk = allObjs[idx].typeKey;
        if (idx > s0 && (tk == 8064u || tk == 28u)) {
            continue;
        }
        objs.push_back(allObjs[idx]);
    }
    for (std::size_t idx = rawCompEnd; idx < s1; ++idx) {
        objs.push_back(allObjs[idx]);
    }

    // Evaluate StateMachine inputs -> active AnimationStates -> initial keyframes (frame 0)
    std::vector<std::size_t> animIndices;
    std::vector<std::size_t> smIndices;
    for (std::size_t i = 0u; i < objs.size(); ++i) {
        if (objs[i].typeKey == 31u) animIndices.push_back(i);
        else if (objs[i].typeKey == 53u) smIndices.push_back(i);
    }

    auto applyAnimationFrame0 = [&](std::size_t animObjIdx) {
        if (animObjIdx >= objs.size()) return;
        std::size_t curTargetObj = 0xFFFFFFFFu;
        std::uint32_t curPropKey = 0u;
        bool appliedFirstKf = false;
        for (std::size_t k = animObjIdx + 1u; k < objs.size(); ++k) {
            const std::uint32_t tk = objs[k].typeKey;
            if (tk == 31u || tk == 53u) break;
            if (tk == 25u) { // KeyedObject
                curTargetObj = objs[k].GetU(51u, 0xFFFFFFFFu);
                curPropKey = 0u;
            } else if (tk == 26u) { // KeyedProperty
                curPropKey = objs[k].GetU(53u, 0u);
                appliedFirstKf = false;
            } else if (!appliedFirstKf && curTargetObj < objs.size() && curPropKey != 0u) {
                if (tk == 30u) { // KeyFrameDouble: Rive clamps t <= firstKeyframe to firstKeyframe.value (omitted 70 => 0.0f)
                    appliedFirstKf = true;
                    RivPropVal pv{};
                    pv.kind = 2u;
                    pv.f = objs[k].GetF(70u, 0.0f);
                    pv.u = static_cast<std::uint64_t>(std::max(0.0f, pv.f));
                    objs[curTargetObj].props[curPropKey] = pv;
                } else if (tk == 37u) { // KeyFrameColor
                    appliedFirstKf = true;
                    RivPropVal pv{};
                    pv.kind = 3u;
                    pv.u = objs[k].GetU(88u, 0u);
                    objs[curTargetObj].props[curPropKey] = pv;
                } else if (tk == 50u || tk == 84u) { // KeyFrameBool / Id
                    appliedFirstKf = true;
                    RivPropVal pv{};
                    pv.kind = 0u;
                    pv.u = objs[k].GetU(122u, 0u);
                    objs[curTargetObj].props[curPropKey] = pv;
                }
            }
        }
    };

    if (!smIndices.empty()) {
        const std::size_t smStart = smIndices.front();
        struct SmInput {
            std::string name;
            float val = 0.0f;
        };
        std::vector<SmInput> smInputs;
        for (std::size_t k = smStart + 1u; k < objs.size(); ++k) {
            const std::uint32_t tk = objs[k].typeKey;
            if (tk == 53u) break;
            if (tk == 56u || tk == 58u || tk == 59u) {
                SmInput inp{};
                inp.name = std::string(objs[k].GetS(138u));
                if (tk == 56u) inp.val = objs[k].GetF(140u, 0.0f);
                else if (tk == 59u) inp.val = objs[k].GetU(141u, 0u) != 0u ? 1.0f : 0.0f;
                if (const auto it = inputs.find(inp.name); it != inputs.end()) {
                    inp.val = it->second;
                }
                smInputs.push_back(std::move(inp));
            }
        }
        // Evaluate each StateMachineLayer (tk=57): EntryState is state 0, AnyState is state 1, first AnimationState is state 2
        for (std::size_t k = smStart + 1u; k < objs.size(); ++k) {
            if (objs[k].typeKey == 53u) break;
            if (objs[k].typeKey != 57u) continue;
            struct LayerState {
                std::uint32_t tk = 0u;
                std::uint32_t animId = 0xFFFFFFFFu;
                struct Trans {
                    std::uint32_t toState = 0u;
                    std::vector<std::tuple<std::uint32_t, std::uint32_t, float>> conds; // inputIdx, op, val
                };
                std::vector<Trans> transitions;
            };
            std::vector<LayerState> states;
            std::size_t p = k + 1u;
            for (; p < objs.size(); ++p) {
                const std::uint32_t tk = objs[p].typeKey;
                if (tk == 57u || tk == 53u) break;
                if (tk == 61u || tk == 62u || tk == 63u || tk == 64u || tk == 73u || tk == 74u) {
                    // AnimationState(61), AnyState(62), EntryState(63), ExitState(64), BlendState(73/74)
                    LayerState st{};
                    st.tk = tk;
                    if (tk == 61u) st.animId = objs[p].GetU(149u, 0u);
                    states.push_back(std::move(st));
                } else if ((tk == 65u || tk == 78u) && !states.empty()) { // StateTransition(65) / BlendStateTransition(78)
                    LayerState::Trans tr{};
                    tr.toState = objs[p].GetU(151u, 0u);
                    states.back().transitions.push_back(std::move(tr));
                } else if ((tk == 67u || tk == 68u || tk == 69u || tk == 70u || tk == 71u) &&
                           !states.empty() && !states.back().transitions.empty()) {
                    const std::uint32_t inpId = objs[p].GetU(155u, 0u);
                    const std::uint32_t op = objs[p].GetU(156u, 0u);
                    const float defaultVal = (tk == 68u || tk == 71u) ? 1.0f : 0.0f;
                    const float cval = objs[p].Has(157u) ? objs[p].GetF(157u, 0.0f) : defaultVal;
                    states.back().transitions.back().conds.emplace_back(inpId, op, cval);
                }
            }
            auto evalConds = [&](const LayerState::Trans& tr) -> bool {
                for (const auto& [inpId, op, cval] : tr.conds) {
                    if (inpId >= smInputs.size()) return false;
                    const float iv = smInputs[inpId].val;
                    if (op == 0u && !( std::fabs(iv - cval) < 0.01f )) return false;
                    if (op == 1u && !( std::fabs(iv - cval) >= 0.01f )) return false;
                    if (op == 2u && !( iv < cval )) return false;
                    if (op == 3u && !( iv <= cval )) return false;
                    if (op == 4u && !( iv > cval )) return false;
                    if (op == 5u && !( iv >= cval )) return false;
                }
                return true;
            };
            std::size_t curState = 0u;
            for (std::size_t si = 0u; si < states.size(); ++si) {
                if (states[si].tk == 63u) { curState = si; break; } // EntryState(63)
            }
            for (int hop = 0; hop < 6 && curState < states.size(); ++hop) {
                bool moved = false;
                const bool isEntry = (states[curState].tk == 63u);
                for (const auto& tr : states[curState].transitions) {
                    if (!isEntry && tr.conds.empty()) continue; // Wait for animation exitTime when unconditioned
                    if (tr.toState < states.size() && tr.toState != curState && evalConds(tr)) {
                        curState = tr.toState;
                        moved = true;
                        break;
                    }
                }
                // Also check AnyState(62) transitions
                if (!moved) {
                    for (const auto& st : states) {
                        if (st.tk != 62u) continue;
                        for (const auto& tr : st.transitions) {
                            if (!tr.conds.empty() && tr.toState < states.size() && tr.toState != curState && evalConds(tr)) {
                                curState = tr.toState;
                                moved = true;
                                break;
                            }
                        }
                        if (moved) break;
                    }
                }
                if (!moved) break;
            }
            if (curState < states.size() && states[curState].animId < animIndices.size()) {
                applyAnimationFrame0(animIndices[states[curState].animId]);
            }
            k = p - 1u;
        }
    } else if (!animIndices.empty()) {
        applyAnimationFrame0(animIndices.front());
    }

    // Component slice ends before first LinearAnimation(31) or StateMachine(53)
    std::size_t compEnd = objs.size();
    for (std::size_t i = 1u; i < objs.size(); ++i) {
        if (objs[i].typeKey == 31u || objs[i].typeKey == 53u) {
            compEnd = i;
            break;
        }
    }

    const float abW = objs[0].GetF(7u, 500.0f);
    const float abH = objs[0].GetF(8u, 200.0f);
    const float abOx = objs[0].GetF(11u, 0.0f);
    const float abOy = objs[0].GetF(12u, 0.0f);

    std::vector<RivMat2D> worldMats(compEnd);
    std::vector<float> worldOpacity(compEnd, inheritedOpacity);
    std::vector<bool> worldHidden(compEnd, false);
    std::vector<std::vector<std::size_t>> childrenOf(compEnd);

    worldMats[0] = (depth == 0)
        ? RivMat2D::Multiply(rootTransform, RivMat2D::FromTRS(abOx * abW, abOy * abH, 0.0f, 1.0f, 1.0f))
        : rootTransform;

    for (std::size_t i = 1u; i < compEnd; ++i) {
        const std::uint32_t pid = objs[i].GetU(5u, 0u);
        if (pid < compEnd) {
            childrenOf[pid].push_back(i);
        }
    }

    auto computeTransform = [&](auto& self, std::size_t idx) -> void {
        for (std::size_t ch : childrenOf[idx]) {
            const RivObject& o = objs[ch];
            const std::uint32_t tk = o.typeKey;
            float lx = o.GetF(13u, 0.0f);
            float ly = o.GetF(14u, 0.0f);
            if (tk == 41u) { // RootBone
                lx = o.GetF(90u, 0.0f);
                ly = o.GetF(91u, 0.0f);
            } else if (tk == 40u && objs[idx].typeKey == 40u) { // Bone child of Bone
                lx = objs[idx].GetF(89u, 0.0f);
                ly = 0.0f;
            }
            const float rot = o.GetF(15u, 0.0f);
            const float sx = o.GetF(16u, 1.0f);
            const float sy = o.GetF(17u, 1.0f);
            const float op = std::clamp(o.GetF(18u, 1.0f), 0.0f, 1.0f);
            const bool selfHidden = (o.GetU(129u, 0u) & 1u) != 0u;

            worldMats[ch] = RivMat2D::Multiply(worldMats[idx], RivMat2D::FromTRS(lx, ly, rot, sx, sy));
            worldOpacity[ch] = worldOpacity[idx] * op;
            worldHidden[ch] = worldHidden[idx] || selfHidden;
            self(self, ch);
        }
    };
    computeTransform(computeTransform, 0u);

    auto collectShapeSubpaths = [&](std::size_t shapeOrNodeIdx, std::vector<RivSubpath>& outPaths) {
        auto walk = [&](auto& self, std::size_t cur) -> void {
            const RivObject& po = objs[cur];
            const std::uint32_t ptk = po.typeKey;
            const RivMat2D& pm = worldMats[cur];
            auto xformSubpath = [&](RivSubpath& sp) {
                for (RivVec2& pt : sp.pts) {
                    pm.Transform(pt.x, pt.y, pt.x, pt.y);
                }
            };
            if (ptk == 16u) { // PointsPath
                const bool closed = po.GetU(32u, 0u) != 0u;
                std::size_t skinIdx = 0u;
                for (std::size_t sc : childrenOf[cur]) {
                    if (objs[sc].typeKey == 43u) {
                        skinIdx = sc;
                        break;
                    }
                }
                std::vector<RivMat2D> skinDeformMats;
                if (skinIdx != 0u) {
                    const RivObject& skObj = objs[skinIdx];
                    RivMat2D skinBind{};
                    skinBind.xx = skObj.GetF(104u, 1.0f);
                    skinBind.yx = skObj.GetF(105u, 0.0f);
                    skinBind.xy = skObj.GetF(106u, 0.0f);
                    skinBind.yy = skObj.GetF(107u, 1.0f);
                    skinBind.tx = skObj.GetF(108u, 0.0f);
                    skinBind.ty = skObj.GetF(109u, 0.0f);
                    RivMat2D zeroMat{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    skinDeformMats.push_back(zeroMat);
                    for (std::size_t tc : childrenOf[skinIdx]) {
                        const RivObject& tdObj = objs[tc];
                        if (tdObj.typeKey != 44u) continue;
                        const std::uint32_t boneId = tdObj.GetU(95u, 0u);
                        RivMat2D tBind{};
                        tBind.xx = tdObj.GetF(96u, 1.0f);
                        tBind.yx = tdObj.GetF(97u, 0.0f);
                        tBind.xy = tdObj.GetF(98u, 0.0f);
                        tBind.yy = tdObj.GetF(99u, 1.0f);
                        tBind.tx = tdObj.GetF(100u, 0.0f);
                        tBind.ty = tdObj.GetF(101u, 0.0f);
                        const RivMat2D bWorld = (boneId < compEnd) ? worldMats[boneId] : worldMats[0];
                        skinDeformMats.push_back(RivMat2D::Multiply(bWorld, RivMat2D::Multiply(RivMat2D::Invert(tBind), skinBind)));
                    }
                }
                auto deformPt = [&](RivVec2 pt, std::uint32_t vals, std::uint32_t idxs) -> RivVec2 {
                    RivVec2 acc{0.0f, 0.0f};
                    float wSum = 0.0f;
                    for (unsigned b = 0u; b < 4u; ++b) {
                        const float w = static_cast<float>((vals >> (b * 8u)) & 0xFFu) / 255.0f;
                        const std::size_t bi = static_cast<std::size_t>((idxs >> (b * 8u)) & 0xFFu);
                        if (w > 0.0f && bi > 0u && bi < skinDeformMats.size()) {
                            float tx = 0.0f, ty = 0.0f;
                            skinDeformMats[bi].Transform(pt.x, pt.y, tx, ty);
                            acc.x += tx * w;
                            acc.y += ty * w;
                            wSum += w;
                        }
                    }
                    if (wSum <= 1e-4f) {
                        RivVec2 wpt{};
                        pm.Transform(pt.x, pt.y, wpt.x, wpt.y);
                        return wpt;
                    }
                    return acc;
                };

                std::vector<RivVertDesc> rawVerts;
                std::vector<RivVertDesc> skinnedVerts;
                bool canUseSkin = (skinDeformMats.size() > 1u);
                const float rScale = 0.5f * (std::hypot(rootTransform.xx, rootTransform.xy) + std::hypot(rootTransform.yx, rootTransform.yy));
                const float maxAllowedJump = 120.0f * std::max(0.25f, rScale);

                for (std::size_t vc : childrenOf[cur]) {
                    const RivObject& vo = objs[vc];
                    const std::uint32_t vtk = vo.typeKey;
                    if (vtk != 5u && vtk != 6u && vtk != 34u && vtk != 35u) continue;
                    RivVertDesc vd{};
                    vd.pos = {vo.GetF(24u, 0.0f), vo.GetF(25u, 0.0f)};
                    vd.inPt = vd.pos;
                    vd.outPt = vd.pos;
                    if (vtk == 5u) { // StraightVertex
                        vd.straightRadius = vo.GetF(26u, 0.0f);
                    } else if (vtk == 6u) { // CubicDetachedVertex
                        vd.isCubic = true;
                        const float inR = vo.GetF(84u, 0.0f), inD = vo.GetF(85u, 0.0f);
                        const float outR = vo.GetF(86u, 0.0f), outD = vo.GetF(87u, 0.0f);
                        vd.inPt = {vd.pos.x + std::cos(inR) * inD, vd.pos.y + std::sin(inR) * inD};
                        vd.outPt = {vd.pos.x + std::cos(outR) * outD, vd.pos.y + std::sin(outR) * outD};
                    } else if (vtk == 34u) { // CubicAsymmetricVertex
                        vd.isCubic = true;
                        const float r = vo.GetF(79u, 0.0f), inD = vo.GetF(80u, 0.0f), outD = vo.GetF(81u, 0.0f);
                        vd.inPt = {vd.pos.x - std::cos(r) * inD, vd.pos.y - std::sin(r) * inD};
                        vd.outPt = {vd.pos.x + std::cos(r) * outD, vd.pos.y + std::sin(r) * outD};
                    } else if (vtk == 35u) { // CubicMirroredVertex
                        vd.isCubic = true;
                        const float r = vo.GetF(82u, 0.0f), d = vo.GetF(83u, 0.0f);
                        vd.inPt = {vd.pos.x - std::cos(r) * d, vd.pos.y - std::sin(r) * d};
                        vd.outPt = {vd.pos.x + std::cos(r) * d, vd.pos.y + std::sin(r) * d};
                    }
                    rawVerts.push_back(vd);

                    if (canUseSkin) {
                        std::size_t wObjIdx = 0u;
                        for (std::size_t wc : childrenOf[vc]) {
                            if (objs[wc].typeKey == 45u || objs[wc].typeKey == 46u) {
                                wObjIdx = wc;
                                break;
                            }
                        }
                        if (wObjIdx != 0u) {
                            const RivObject& wo = objs[wObjIdx];
                            const std::uint32_t vVals = wo.GetU(102u, 255u);
                            const std::uint32_t vIdxs = wo.GetU(103u, 1u);
                            RivVertDesc sv = vd;
                            sv.pos = deformPt(vd.pos, vVals, vIdxs);
                            if (wo.typeKey == 46u) {
                                sv.inPt = deformPt(vd.inPt, wo.GetU(110u, 255u), wo.GetU(111u, 1u));
                                sv.outPt = deformPt(vd.outPt, wo.GetU(112u, 255u), wo.GetU(113u, 1u));
                            } else {
                                sv.inPt = deformPt(vd.inPt, vVals, vIdxs);
                                sv.outPt = deformPt(vd.outPt, vVals, vIdxs);
                            }
                            RivVec2 unsk{};
                            pm.Transform(vd.pos.x, vd.pos.y, unsk.x, unsk.y);
                            if (std::hypot(sv.pos.x - unsk.x, sv.pos.y - unsk.y) > maxAllowedJump) {
                                canUseSkin = false;
                            }
                            skinnedVerts.push_back(sv);
                        } else {
                            canUseSkin = false;
                        }
                    }
                }
                const bool alreadyWorld = canUseSkin && (skinnedVerts.size() == rawVerts.size());
                const std::vector<RivVertDesc>& verts = alreadyWorld ? skinnedVerts : rawVerts;
                if (verts.size() >= 2u) {
                    // Expand straight-vertex corner radii if any
                    std::vector<RivVertDesc> effVerts;
                    effVerts.reserve(verts.size() * 2u);
                    const std::size_t vn = verts.size();
                    for (std::size_t vi = 0u; vi < vn; ++vi) {
                        const RivVertDesc& v = verts[vi];
                        const bool isEndpoint = !closed && (vi == 0u || vi + 1u == vn);
                        if (!v.isCubic && v.straightRadius > 0.1f && !isEndpoint && vn >= 3u) {
                            const RivVec2 prevP = verts[(vi + vn - 1u) % vn].pos;
                            const RivVec2 nextP = verts[(vi + 1u) % vn].pos;
                            const float dPrev = std::hypot(v.pos.x - prevP.x, v.pos.y - prevP.y);
                            const float dNext = std::hypot(nextP.x - v.pos.x, nextP.y - v.pos.y);
                            const float rr = std::min(v.straightRadius, 0.5f * std::min(dPrev, dNext));
                            if (rr > 0.1f && dPrev > 1e-3f && dNext > 1e-3f) {
                                const RivVec2 t1{v.pos.x + (prevP.x - v.pos.x) * (rr / dPrev),
                                                 v.pos.y + (prevP.y - v.pos.y) * (rr / dPrev)};
                                const RivVec2 t2{v.pos.x + (nextP.x - v.pos.x) * (rr / dNext),
                                                 v.pos.y + (nextP.y - v.pos.y) * (rr / dNext)};
                                constexpr float kK = 0.55228475f;
                                RivVertDesc a{};
                                a.pos = t1; a.inPt = t1;
                                a.outPt = {t1.x + (v.pos.x - t1.x) * kK, t1.y + (v.pos.y - t1.y) * kK};
                                a.isCubic = true;
                                RivVertDesc b{};
                                b.pos = t2; b.outPt = t2;
                                b.inPt = {t2.x + (v.pos.x - t2.x) * kK, t2.y + (v.pos.y - t2.y) * kK};
                                b.isCubic = true;
                                effVerts.push_back(a);
                                effVerts.push_back(b);
                                continue;
                            }
                        }
                        effVerts.push_back(v);
                    }
                    RivSubpath sp{};
                    sp.closed = closed;
                    sp.pts.push_back(effVerts[0].pos);
                    const std::size_t segs = closed ? effVerts.size() : (effVerts.size() - 1u);
                    for (std::size_t si = 0u; si < segs; ++si) {
                        const RivVertDesc& a = effVerts[si];
                        const RivVertDesc& b = effVerts[(si + 1u) % effVerts.size()];
                        if (!a.isCubic && !b.isCubic) {
                            sp.pts.push_back(b.pos);
                        } else {
                            FlattenCubic(a.pos, a.outPt, b.inPt, b.pos, 0, sp.pts);
                        }
                    }
                    if (!alreadyWorld) {
                        xformSubpath(sp);
                    }
                    outPaths.push_back(std::move(sp));
                }
            } else if (ptk == 7u) { // Rectangle
                const float w = po.GetF(20u, 0.0f), h = po.GetF(21u, 0.0f);
                const float ox = po.GetF(123u, 0.5f), oy = po.GetF(124u, 0.5f);
                const float rTL = po.GetF(31u, 0.0f);
                const bool linked = po.GetU(164u, 1u) != 0u;
                const float rTR = linked ? rTL : po.GetF(161u, rTL);
                const float rBL = linked ? rTL : po.GetF(162u, rTL);
                const float rBR = linked ? rTL : po.GetF(163u, rTL);
                RivSubpath sp = BuildRoundedRectSubpath(ox, oy, w, h, rTL, rTR, rBR, rBL);
                xformSubpath(sp);
                outPaths.push_back(std::move(sp));
            } else if (ptk == 4u) { // Ellipse
                const float w = po.GetF(20u, 0.0f), h = po.GetF(21u, 0.0f);
                const float ox = po.GetF(123u, 0.5f), oy = po.GetF(124u, 0.5f);
                RivSubpath sp = BuildEllipseSubpath(ox, oy, w, h);
                xformSubpath(sp);
                outPaths.push_back(std::move(sp));
            } else if (ptk == 8u) { // Triangle
                const float w = po.GetF(20u, 0.0f), h = po.GetF(21u, 0.0f);
                const float ox = po.GetF(123u, 0.5f), oy = po.GetF(124u, 0.5f);
                const float x0 = -ox * w, y0 = -oy * h;
                RivSubpath sp{{{x0 + 0.5f * w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}}, true};
                xformSubpath(sp);
                outPaths.push_back(std::move(sp));
            }
            for (std::size_t sub : childrenOf[cur]) {
                const std::uint32_t stk = objs[sub].typeKey;
                if (stk != 3u && stk != 92u) {
                    self(self, sub);
                }
            }
        };
        walk(walk, shapeOrNodeIdx);
    };

    // Draw Artboard background Fill(s) (childrenOf[0] with tk == 20u, e.g. sky gradient on Background artboard)
    for (std::size_t bgPaintIdx : childrenOf[0]) {
        const RivObject& paintObj = objs[bgPaintIdx];
        if (paintObj.typeKey != 20u || paintObj.GetU(41u, 1u) == 0u) continue;
        RivPaint paint{};
        for (std::size_t sc : childrenOf[bgPaintIdx]) {
            const RivObject& so = objs[sc];
            if (so.typeKey == 18u) {
                paint.style = RivPaintStyle::Solid;
                paint.solidArgb = so.GetU(37u, 0xFFFFFFFFu);
            } else if (so.typeKey == 22u || so.typeKey == 17u) {
                paint.style = (so.typeKey == 22u) ? RivPaintStyle::Linear : RivPaintStyle::Radial;
                const float gx0 = so.GetF(42u, 0.0f), gy0 = so.GetF(33u, 0.0f);
                const float gx1 = so.GetF(34u, 0.0f), gy1 = so.GetF(35u, 0.0f);
                worldMats[0].Transform(gx0 - abOx * abW, gy0 - abOy * abH, paint.gradStart.x, paint.gradStart.y);
                worldMats[0].Transform(gx1 - abOx * abW, gy1 - abOy * abH, paint.gradEnd.x, paint.gradEnd.y);
                paint.gradOpacity = std::clamp(so.GetF(46u, 1.0f), 0.0f, 1.0f);
                for (std::size_t stc : childrenOf[sc]) {
                    if (objs[stc].typeKey == 19u) {
                        paint.stops.push_back({objs[stc].GetF(39u, 0.0f), objs[stc].GetU(38u, 0xFFFFFFFFu)});
                    }
                }
                std::sort(paint.stops.begin(), paint.stops.end(),
                          [](const RivGradStop& a, const RivGradStop& b) { return a.pos < b.pos; });
            }
        }
        if (paint.style == RivPaintStyle::None) continue;
        RivSubpath abRect = BuildRoundedRectSubpath(abOx, abOy, abW, abH, 0.0f, 0.0f, 0.0f, 0.0f);
        for (RivVec2& pt : abRect.pts) {
            worldMats[0].Transform(pt.x, pt.y, pt.x, pt.y);
        }
        std::vector<RivSubpath> abPaths{std::move(abRect)};
        int mx0 = 0, my0 = 0, mw = 0, mh = 0;
        std::vector<float> covMask;
        RasterizeCoverageMask(surfW, surfH, abPaths, false, mx0, my0, mw, mh, covMask);
        if (mw <= 0 || mh <= 0) continue;
        const float effOp = worldOpacity[0] * paint.gradOpacity;
        float solidPremul[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (paint.style == RivPaintStyle::Solid) {
            UnpackArgbPremul(paint.solidArgb, effOp, solidPremul);
            if (solidPremul[3] <= 0.001f) continue;
        }
        const float gdx = paint.gradEnd.x - paint.gradStart.x;
        const float gdy = paint.gradEnd.y - paint.gradStart.y;
        const float glen2 = std::max(1e-5f, gdx * gdx + gdy * gdy);
        const float gradRad = std::max(1e-3f, std::sqrt(glen2));
        for (int ry = 0; ry < mh; ++ry) {
            const int py = my0 + ry;
            const float* mRow = covMask.data() + static_cast<std::size_t>(ry) * static_cast<std::size_t>(mw);
            float* dstRow = surfPremul.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(surfW) * 4u;
            for (int rx = 0; rx < mw; ++rx) {
                const float cov = mRow[rx];
                if (cov <= 0.002f) continue;
                const int px = mx0 + rx;
                float col[4];
                if (paint.style == RivPaintStyle::Solid) {
                    col[0] = solidPremul[0] * cov;
                    col[1] = solidPremul[1] * cov;
                    col[2] = solidPremul[2] * cov;
                    col[3] = solidPremul[3] * cov;
                } else if (paint.style == RivPaintStyle::Linear) {
                    const float t = (((static_cast<float>(px) + 0.5f) - paint.gradStart.x) * gdx +
                                     ((static_cast<float>(py) + 0.5f) - paint.gradStart.y) * gdy) / glen2;
                    SampleStopsPremul(paint.stops, t, effOp * cov, col);
                } else {
                    const float d = std::hypot((static_cast<float>(px) + 0.5f) - paint.gradStart.x,
                                               (static_cast<float>(py) + 0.5f) - paint.gradStart.y) / gradRad;
                    SampleStopsPremul(paint.stops, d, effOp * cov, col);
                }
                float* dpx = dstRow + static_cast<std::size_t>(px) * 4u;
                const float invA = 1.0f - col[3];
                dpx[0] = col[0] + dpx[0] * invA;
                dpx[1] = col[1] + dpx[1] * invA;
                dpx[2] = col[2] + dpx[2] * invA;
                dpx[3] = col[3] + dpx[3] * invA;
            }
        }
    }

    // Rive v7 serializes hierarchy drawables front-to-back; iterate back-to-front for painter's order
    for (std::size_t i = compEnd; i-- > 1u;) {
        if (worldHidden[i] || worldOpacity[i] <= 0.002f) continue;
        const RivObject& o = objs[i];
        if (o.typeKey == 92u) { // NestedArtboard
            const std::uint32_t targetAb = o.GetU(197u, 0xFFFFFFFFu);
            if (targetAb < abSlices.size() && targetAb != abIndex) {
                const RivObject& childAbObj = allObjs[abSlices[targetAb].startIdx];
                const float cW = childAbObj.GetF(7u, 0.0f);
                const float cH = childAbObj.GetF(8u, 0.0f);
                const float cOx = childAbObj.GetF(11u, 0.0f);
                const float cOy = childAbObj.GetF(12u, 0.0f);
                const RivMat2D nestedWorld = RivMat2D::Multiply(
                    worldMats[i],
                    RivMat2D::FromTRS(-cOx * cW, -cOy * cH, 0.0f, 1.0f, 1.0f));
                RenderArtboardRecursive(
                    allObjs, abSlices, targetAb, nestedWorld, worldOpacity[i],
                    inputs, surfW, surfH, surfPremul, depth + 1);
            }
            continue;
        }
        if (o.typeKey != 3u) continue; // Shape

        std::vector<RivSubpath> shapePaths;
        collectShapeSubpaths(i, shapePaths);
        if (shapePaths.empty()) continue;

        // Check ClippingShape children (tk=42) on this Shape or ancestors
        struct ClipMaskBuf {
            int x0 = 0, y0 = 0, w = 0, h = 0;
            std::vector<float> mask;
        };
        std::vector<ClipMaskBuf> activeClips;
        for (std::size_t anc = i; anc != 0u && anc < compEnd; anc = objs[anc].GetU(5u, 0u)) {
            for (std::size_t ch : childrenOf[anc]) {
                if (objs[ch].typeKey == 42u && objs[ch].GetU(94u, 1u) != 0u) {
                    const std::uint32_t srcId = objs[ch].GetU(92u, 0xFFFFFFFFu);
                    if (srcId < compEnd) {
                        std::vector<RivSubpath> clipPaths;
                        collectShapeSubpaths(srcId, clipPaths);
                        if (!clipPaths.empty()) {
                            ClipMaskBuf cb{};
                            RasterizeCoverageMask(
                                surfW, surfH, clipPaths, objs[ch].GetU(93u, 0u) != 0u,
                                cb.x0, cb.y0, cb.w, cb.h, cb.mask);
                            if (cb.w > 0 && cb.h > 0) {
                                activeClips.push_back(std::move(cb));
                            }
                        }
                    }
                }
            }
        }

        const RivMat2D& shapeMat = worldMats[i];
        const float shapeScale = 0.5f * (std::hypot(shapeMat.xx, shapeMat.xy) + std::hypot(shapeMat.yx, shapeMat.yy));
        const float rootScale = 0.5f * (std::hypot(rootTransform.xx, rootTransform.xy) + std::hypot(rootTransform.yx, rootTransform.yy));

        for (std::size_t pIdx : childrenOf[i]) {
            const RivObject& paintObj = objs[pIdx];
            const std::uint32_t ptk = paintObj.typeKey;
            if ((ptk != 20u && ptk != 24u) || paintObj.GetU(41u, 1u) == 0u) continue;

            RivPaint paint{};
            paint.isStroke = (ptk == 24u);
            paint.evenOdd = (ptk == 20u) && (paintObj.GetU(40u, 0u) == 1u);
            paint.strokeThickness = paintObj.GetF(47u, 1.0f);
            paint.strokeCap = paintObj.GetU(48u, 0u);
            paint.strokeJoin = paintObj.GetU(49u, 0u);
            paint.transformAffectsStroke = paintObj.GetU(50u, 1u) != 0u;

            for (std::size_t sc : childrenOf[pIdx]) {
                const RivObject& so = objs[sc];
                if (so.typeKey == 18u) { // SolidColor
                    paint.style = RivPaintStyle::Solid;
                    paint.solidArgb = so.GetU(37u, 0xFFFFFFFFu);
                } else if (so.typeKey == 22u || so.typeKey == 17u) { // Linear / Radial Gradient
                    paint.style = (so.typeKey == 22u) ? RivPaintStyle::Linear : RivPaintStyle::Radial;
                    const float gx0 = so.GetF(42u, 0.0f), gy0 = so.GetF(33u, 0.0f);
                    const float gx1 = so.GetF(34u, 0.0f), gy1 = so.GetF(35u, 0.0f);
                    shapeMat.Transform(gx0, gy0, paint.gradStart.x, paint.gradStart.y);
                    shapeMat.Transform(gx1, gy1, paint.gradEnd.x, paint.gradEnd.y);
                    paint.gradOpacity = std::clamp(so.GetF(46u, 1.0f), 0.0f, 1.0f);
                    for (std::size_t stc : childrenOf[sc]) {
                        if (objs[stc].typeKey == 19u) {
                            paint.stops.push_back({objs[stc].GetF(39u, 0.0f), objs[stc].GetU(38u, 0xFFFFFFFFu)});
                        }
                    }
                    std::sort(paint.stops.begin(), paint.stops.end(),
                              [](const RivGradStop& a, const RivGradStop& b) { return a.pos < b.pos; });
                }
            }
            if (paint.style == RivPaintStyle::None) continue;

            std::vector<RivSubpath> strokeExpanded;
            const std::vector<RivSubpath>* targetPaths = &shapePaths;
            bool useEvenOdd = paint.evenOdd;
            if (paint.isStroke) {
                bool trimmedOut = false;
                bool hasPartialTrim = false;
                std::vector<RivSubpath> trimmedPaths;
                for (std::size_t tc : childrenOf[pIdx]) {
                    if (objs[tc].typeKey == 47u) { // TrimPath (114=start, 115=end, 116=offset, 117=mode)
                        const float ts = objs[tc].GetF(114u, 0.0f);
                        const float te = objs[tc].GetF(115u, 0.0f);
                        const float toff = objs[tc].GetF(116u, 0.0f);
                        const std::uint32_t tmode = objs[tc].GetU(117u, 1u);
                        if (std::fabs(te - ts) <= 1e-4f) {
                            trimmedOut = true;
                            break;
                        }
                        if (!(std::min(ts, te) <= 1e-4f && std::max(ts, te) >= 1.0f - 1e-4f)) {
                            std::vector<RivSubpath> nextTrimmed;
                            TrimRivSubpaths(
                                hasPartialTrim ? trimmedPaths : shapePaths,
                                ts, te, toff, tmode, nextTrimmed);
                            trimmedPaths = std::move(nextTrimmed);
                            hasPartialTrim = true;
                            if (trimmedPaths.empty()) {
                                trimmedOut = true;
                                break;
                            }
                        }
                    }
                }
                if (trimmedOut) continue;
                const float effScale = paint.transformAffectsStroke ? shapeScale : rootScale;
                const float halfW = 0.5f * paint.strokeThickness * effScale;
                ExpandStrokeToSubpaths(
                    hasPartialTrim ? trimmedPaths : shapePaths,
                    halfW, paint.strokeCap, strokeExpanded);
                targetPaths = &strokeExpanded;
                useEvenOdd = false;
            }
            if (targetPaths->empty()) continue;

            int mx0 = 0, my0 = 0, mw = 0, mh = 0;
            std::vector<float> covMask;
            RasterizeCoverageMask(surfW, surfH, *targetPaths, useEvenOdd, mx0, my0, mw, mh, covMask);
            if (mw <= 0 || mh <= 0) continue;

            const float effOp = worldOpacity[i] * paint.gradOpacity;
            float solidPremul[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (paint.style == RivPaintStyle::Solid) {
                UnpackArgbPremul(paint.solidArgb, effOp, solidPremul);
                if (solidPremul[3] <= 0.001f) continue;
            }

            const float gdx = paint.gradEnd.x - paint.gradStart.x;
            const float gdy = paint.gradEnd.y - paint.gradStart.y;
            const float glen2 = std::max(1e-5f, gdx * gdx + gdy * gdy);
            const float gradRad = std::max(1e-3f, std::sqrt(glen2));

            for (int ry = 0; ry < mh; ++ry) {
                const int py = my0 + ry;
                const float* mRow = covMask.data() + static_cast<std::size_t>(ry) * static_cast<std::size_t>(mw);
                float* dstRow = surfPremul.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(surfW) * 4u;
                for (int rx = 0; rx < mw; ++rx) {
                    float cov = mRow[rx];
                    if (cov <= 0.002f) continue;
                    const int px = mx0 + rx;
                    for (const ClipMaskBuf& cb : activeClips) {
                        if (px < cb.x0 || px >= cb.x0 + cb.w || py < cb.y0 || py >= cb.y0 + cb.h) {
                            cov = 0.0f;
                            break;
                        }
                        cov *= cb.mask[static_cast<std::size_t>(py - cb.y0) * static_cast<std::size_t>(cb.w) +
                                       static_cast<std::size_t>(px - cb.x0)];
                        if (cov <= 0.002f) break;
                    }
                    if (cov <= 0.002f) continue;

                    float col[4];
                    if (paint.style == RivPaintStyle::Solid) {
                        col[0] = solidPremul[0] * cov;
                        col[1] = solidPremul[1] * cov;
                        col[2] = solidPremul[2] * cov;
                        col[3] = solidPremul[3] * cov;
                    } else if (paint.style == RivPaintStyle::Linear) {
                        const float t = (((static_cast<float>(px) + 0.5f) - paint.gradStart.x) * gdx +
                                         ((static_cast<float>(py) + 0.5f) - paint.gradStart.y) * gdy) / glen2;
                        SampleStopsPremul(paint.stops, t, effOp * cov, col);
                    } else {
                        const float d = std::hypot((static_cast<float>(px) + 0.5f) - paint.gradStart.x,
                                                   (static_cast<float>(py) + 0.5f) - paint.gradStart.y) / gradRad;
                        SampleStopsPremul(paint.stops, d, effOp * cov, col);
                    }

                    float* dpx = dstRow + static_cast<std::size_t>(px) * 4u;
                    const float invA = 1.0f - col[3];
                    dpx[0] = col[0] + dpx[0] * invA;
                    dpx[1] = col[1] + dpx[1] * invA;
                    dpx[2] = col[2] + dpx[2] * invA;
                    dpx[3] = col[3] + dpx[3] * invA;
                }
            }
        }
    }
}

} // namespace

bool IsRiveAssetUri(std::string_view uri) noexcept {
    if (uri.size() >= 7u && uri.substr(0u, 7u) == "rive://") {
        return true;
    }
    const std::size_t q = uri.find('?');
    const std::string_view pathPart = (q != std::string_view::npos) ? uri.substr(0u, q) : uri;
    if (pathPart.size() >= 4u) {
        const std::string_view ext = pathPart.substr(pathPart.size() - 4u);
        return (ext == ".riv" || ext == ".RIV");
    }
    return false;
}

bool RenderRiveUriToImage(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    DecodedImage& outImage) {
    std::string_view rest = uri;
    if (rest.size() >= 7u && rest.substr(0u, 7u) == "rive://") {
        rest.remove_prefix(7u);
    }
    std::string_view filePart = rest;
    std::string_view queryPart;
    if (const std::size_t q = rest.find('?'); q != std::string_view::npos) {
        filePart = rest.substr(0u, q);
        queryPart = rest.substr(q + 1u);
    }
    if (filePart.empty()) return false;

    std::unordered_map<std::string, float> inputs;
    while (!queryPart.empty()) {
        const std::size_t amp = queryPart.find('&');
        const std::string_view kv = (amp != std::string_view::npos) ? queryPart.substr(0u, amp) : queryPart;
        if (const std::size_t eq = kv.find('='); eq != std::string_view::npos) {
            const std::string key(kv.substr(0u, eq));
            const std::string valStr(kv.substr(eq + 1u));
            if (valStr == "true" || valStr == "True") inputs[key] = 1.0f;
            else if (valStr == "false" || valStr == "False") inputs[key] = 0.0f;
            else {
                char* endp = nullptr;
                const float fv = std::strtof(valStr.c_str(), &endp);
                if (endp != valStr.c_str()) inputs[key] = fv;
            }
        }
        if (amp == std::string_view::npos) break;
        queryPart.remove_prefix(amp + 1u);
    }

    const std::string resolvedPath = atlas.ResolveUriToPath(filePart, localBaseDir);
    if (resolvedPath.empty()) return false;

    std::ifstream ifs(resolvedPath, std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    const auto fsize = ifs.tellg();
    if (fsize <= 6 || fsize > 64 * 1024 * 1024) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fsize));
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(bytes.data()), fsize)) return false;

    std::vector<RivObject> allObjs;
    if (!ParseRiveFileBytes(bytes, allObjs)) return false;

    std::vector<RivArtboardSlice> abSlices;
    for (std::size_t i = 0u; i < allObjs.size(); ++i) {
        if (allObjs[i].typeKey == 1u) {
            if (!abSlices.empty()) abSlices.back().endIdx = i;
            abSlices.push_back({i, allObjs.size()});
        }
    }
    if (abSlices.empty()) return false;

    const RivObject& mainAb = allObjs[abSlices[0].startIdx];
    const float natW = std::max(1.0f, mainAb.GetF(7u, 500.0f));
    const float natH = std::max(1.0f, mainAb.GetF(8u, 200.0f));
    float scale = 1.0f;
    const float maxDim = std::max(natW, natH);
    if (maxDim > 1280.0f) {
        scale = 1280.0f / maxDim;
    }
    const int surfW = std::clamp(static_cast<int>(std::lround(natW * scale)), 1, 2048);
    const int surfH = std::clamp(static_cast<int>(std::lround(natH * scale)), 1, 2048);

    std::vector<float> surfPremul(static_cast<std::size_t>(surfW) * static_cast<std::size_t>(surfH) * 4u, 0.0f);
    const RivMat2D rootMat = RivMat2D::FromTRS(0.0f, 0.0f, 0.0f,
                                               static_cast<float>(surfW) / natW,
                                               static_cast<float>(surfH) / natH);
    RenderArtboardRecursive(allObjs, abSlices, 0u, rootMat, 1.0f, inputs, surfW, surfH, surfPremul, 0);

    outImage.width = static_cast<std::uint32_t>(surfW);
    outImage.height = static_cast<std::uint32_t>(surfH);
    outImage.format = ImageFormat::RawRgba8;
    outImage.rgbaPixels.resize(static_cast<std::size_t>(surfW) * static_cast<std::size_t>(surfH) * 4u);
    for (std::size_t p = 0u, total = static_cast<std::size_t>(surfW) * static_cast<std::size_t>(surfH); p < total; ++p) {
        const float pa = std::clamp(surfPremul[p * 4u + 3u], 0.0f, 1.0f);
        std::uint8_t* dst = outImage.rgbaPixels.data() + p * 4u;
        if (pa <= 0.001f) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0u;
        } else {
            const float invA = 1.0f / pa;
            dst[0] = static_cast<std::uint8_t>(std::clamp(surfPremul[p * 4u + 0u] * invA * 255.0f + 0.5f, 0.0f, 255.0f));
            dst[1] = static_cast<std::uint8_t>(std::clamp(surfPremul[p * 4u + 1u] * invA * 255.0f + 0.5f, 0.0f, 255.0f));
            dst[2] = static_cast<std::uint8_t>(std::clamp(surfPremul[p * 4u + 2u] * invA * 255.0f + 0.5f, 0.0f, 255.0f));
            dst[3] = static_cast<std::uint8_t>(std::clamp(pa * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    return true;
}

} // namespace aero
