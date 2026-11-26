#include "aero/render/RiveRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
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

[[nodiscard]] inline float FastHypot(float dx, float dy) noexcept {
    return std::sqrt(dx * dx + dy * dy);
}

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
        {129,0},{138,1},{140,2},{141,0},{149,0},{151,0},{152,0},{153,0},{155,0},{156,0},{157,2},{158,0},{160,0},
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

// ============================================================================
// Flat Pre-Indexed Rive Document Model (Built ONCE on file load, ZERO maps)
// ============================================================================

struct RivCubicInterp {
    float x1 = 0.42f, y1 = 0.0f, x2 = 0.58f, y2 = 1.0f;
};

struct PrebuiltTimingInfo {
    float fps = 60.0f;
    float speed = 1.0f;
    std::uint32_t loop = 0u;
    float fStart = 0.0f;
    float fEnd = 60.0f;
    float fSpan = 60.0f;
    float durationSec = 1.0f;
};

struct PrebuiltKfEntry {
    std::uint32_t tk = 0u;
    float frame = 0.0f;
    std::uint32_t interp = 0u; // 0=Linear, 1=Hold, 2=Cubic
    std::uint32_t interpId = 0u;
    float fVal = 0.0f;
    std::uint64_t uVal = 0u;
};

struct PrebuiltPropTrack {
    std::size_t targetObj = 0u;
    std::uint32_t propKey = 0u;
    std::vector<PrebuiltKfEntry> kfs;
};

struct PrebuiltAnimation {
    PrebuiltTimingInfo timing;
    std::vector<PrebuiltPropTrack> tracks;
};

struct PrebuiltSmInput {
    std::string name;
    float defaultVal = 0.0f;
};

struct PrebuiltSmTransition {
    std::uint32_t toState = 0u;
    std::uint32_t flags = 0u;
    float exitTime = 0.0f;
    std::vector<std::tuple<std::uint32_t, std::uint32_t, float>> conds;
};

struct PrebuiltSmLayerState {
    std::uint32_t tk = 0u;
    std::uint32_t animId = 0xFFFFFFFFu;
    std::vector<PrebuiltSmTransition> transitions;
};

struct PrebuiltSmLayer {
    std::size_t entryState = 0u;
    std::vector<PrebuiltSmLayerState> states;
};

struct PrebuiltStateMachine {
    std::vector<PrebuiltSmInput> inputs;
    std::vector<PrebuiltSmLayer> layers;
};

struct PrebuiltNodeBase {
    float x = 0.0f;
    float y = 0.0f;
    float rot = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    float opacity = 1.0f;
    float boneLen = 0.0f;
    std::uint8_t hidden = 0u;
    std::uint32_t targetAb = 0xFFFFFFFFu;
};

struct PrebuiltSkinBone {
    std::uint32_t boneId = 0u;
    RivMat2D invBindMulSkinBind{};
};

struct PrebuiltVertex {
    std::uint32_t objIdx = 0u;
    std::uint32_t vtk = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float p0 = 0.0f;
    float p1 = 0.0f;
    float p2 = 0.0f;
    float p3 = 0.0f;
    std::uint32_t weightObjIdx = 0u;
    std::uint32_t weightTk = 0u;
    std::uint32_t vVals = 255u;
    std::uint32_t vIdxs = 1u;
    std::uint32_t inVals = 255u;
    std::uint32_t inIdxs = 1u;
    std::uint32_t outVals = 255u;
    std::uint32_t outIdxs = 1u;
};

struct PrebuiltPathData {
    std::uint32_t objIdx = 0u;
    std::uint32_t tk = 0u;
    bool closed = false;
    float w = 0.0f;
    float h = 0.0f;
    float ox = 0.5f;
    float oy = 0.5f;
    float rTL = 0.0f;
    float rTR = 0.0f;
    float rBL = 0.0f;
    float rBR = 0.0f;
    bool linked = true;
    std::vector<PrebuiltVertex> verts;
    std::vector<PrebuiltSkinBone> skinBones;
};

struct PrebuiltClipRef {
    std::uint32_t clipObjIdx = 0u;
    std::uint32_t srcId = 0xFFFFFFFFu;
    bool enabled = true;
    bool evenOdd = false;
};

struct PrebuiltPaintStop {
    std::uint32_t stopObjIdx = 0u;
    float pos = 0.0f;
    std::uint32_t argb = 0xFFFFFFFFu;
};

struct PrebuiltPaintTrim {
    std::uint32_t trimObjIdx = 0u;
    float start = 0.0f;
    float end = 0.0f;
    float offset = 0.0f;
    std::uint32_t mode = 1u;
};

struct PrebuiltPaintDef {
    std::uint32_t paintObjIdx = 0u;
    std::uint32_t colorObjIdx = 0u;
    bool enabled = true;
    bool isStroke = false;
    bool evenOdd = false;
    bool transformAffectsStroke = true;
    RivPaintStyle style = RivPaintStyle::None;
    std::uint32_t solidArgb = 0xFFFFFFFFu;
    float strokeThickness = 1.0f;
    std::uint32_t strokeCap = 0u;
    std::uint32_t strokeJoin = 0u;
    float gx0 = 0.0f;
    float gy0 = 0.0f;
    float gx1 = 0.0f;
    float gy1 = 0.0f;
    float gradOpacity = 1.0f;
    std::vector<PrebuiltPaintStop> stops;
    std::vector<PrebuiltPaintTrim> trims;
};

struct PrebuiltArtboard {
    float natW = 500.0f;
    float natH = 200.0f;
    float abOx = 0.0f;
    float abOy = 0.0f;
    std::size_t compEnd = 0u;
    std::vector<std::uint32_t> typeKeys;
    std::vector<std::uint32_t> parentIds;
    std::vector<std::uint8_t> hasClipInAncestors;
    std::vector<PrebuiltNodeBase> nodes;
    std::vector<std::int32_t> pathDataIndex;
    std::vector<PrebuiltPathData> paths;
    std::vector<std::vector<PrebuiltClipRef>> nodeClips;
    std::vector<PrebuiltPaintDef> bgPaints;
    std::vector<std::int32_t> shapePaintIndex;
    std::vector<std::vector<PrebuiltPaintDef>> shapePaints;
    std::vector<std::size_t> renderOrder;
    std::vector<std::vector<std::size_t>> childrenOf;
    std::vector<std::pair<std::uint32_t, RivCubicInterp>> cubicInterps;
    std::vector<PrebuiltAnimation> animations;
    std::vector<PrebuiltStateMachine> stateMachines;
};

struct PrebuiltRiveDocument {
    std::vector<PrebuiltArtboard> artboards;
};

[[nodiscard]] float EvalCubicEase(
    const std::vector<std::pair<std::uint32_t, RivCubicInterp>>& cubicInterps,
    std::uint32_t interpId,
    float u) noexcept {
    u = std::clamp(u, 0.0f, 1.0f);
    if (u <= 0.0f || u >= 1.0f) return u;
    RivCubicInterp ci{};
    const auto it = std::lower_bound(
        cubicInterps.begin(), cubicInterps.end(), interpId,
        [](const std::pair<std::uint32_t, RivCubicInterp>& item, std::uint32_t key) {
            return item.first < key;
        });
    if (it != cubicInterps.end() && it->first == interpId) {
        ci = it->second;
    }
    float s = u;
    for (int iter = 0; iter < 6; ++iter) {
        const float inv = 1.0f - s;
        const float bx = 3.0f * inv * inv * s * ci.x1 + 3.0f * inv * s * s * ci.x2 + s * s * s - u;
        const float dbx = 3.0f * inv * inv * ci.x1 + 6.0f * inv * s * (ci.x2 - ci.x1) + 3.0f * s * s * (1.0f - ci.x2);
        if (std::fabs(dbx) < 1e-5f) break;
        s = std::clamp(s - bx / dbx, 0.0f, 1.0f);
    }
    const float inv = 1.0f - s;
    return 3.0f * inv * inv * s * ci.y1 + 3.0f * inv * s * s * ci.y2 + s * s * s;
}

[[nodiscard]] std::uint32_t LerpArgb(std::uint32_t c0, std::uint32_t c1, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    std::uint32_t out = 0u;
    for (unsigned sh = 0u; sh < 32u; sh += 8u) {
        const float a = static_cast<float>((c0 >> sh) & 0xFFu);
        const float b = static_cast<float>((c1 >> sh) & 0xFFu);
        const auto v = static_cast<std::uint32_t>(std::clamp(a + (b - a) * t + 0.5f, 0.0f, 255.0f));
        out |= (v << sh);
    }
    return out;
}

PrebuiltPaintDef BuildPrebuiltPaintDef(
    const std::vector<RivObject>& objs,
    const std::vector<std::vector<std::size_t>>& childrenOf,
    std::size_t pIdx) {
    PrebuiltPaintDef p{};
    const std::uint32_t ptk = objs[pIdx].typeKey;
    p.paintObjIdx = static_cast<std::uint32_t>(pIdx);
    p.enabled = (objs[pIdx].GetU(41u, 1u) != 0u);
    p.isStroke = (ptk == 24u);
    p.evenOdd = (ptk == 20u) && (objs[pIdx].GetU(40u, 0u) == 1u);
    p.strokeThickness = objs[pIdx].GetF(47u, 1.0f);
    p.strokeCap = objs[pIdx].GetU(48u, 0u);
    p.strokeJoin = objs[pIdx].GetU(49u, 0u);
    p.transformAffectsStroke = (objs[pIdx].GetU(50u, 1u) != 0u);

    for (std::size_t sc : childrenOf[pIdx]) {
        const std::uint32_t stk = objs[sc].typeKey;
        if (stk == 18u) {
            p.style = RivPaintStyle::Solid;
            p.colorObjIdx = static_cast<std::uint32_t>(sc);
            p.solidArgb = objs[sc].GetU(37u, 0xFFFFFFFFu);
        } else if (stk == 22u || stk == 17u) {
            p.style = (stk == 22u) ? RivPaintStyle::Linear : RivPaintStyle::Radial;
            p.colorObjIdx = static_cast<std::uint32_t>(sc);
            p.gx0 = objs[sc].GetF(42u, 0.0f);
            p.gy0 = objs[sc].GetF(33u, 0.0f);
            p.gx1 = objs[sc].GetF(34u, 0.0f);
            p.gy1 = objs[sc].GetF(35u, 0.0f);
            p.gradOpacity = std::clamp(objs[sc].GetF(46u, 1.0f), 0.0f, 1.0f);
            for (std::size_t stc : childrenOf[sc]) {
                if (objs[stc].typeKey == 19u) {
                    PrebuiltPaintStop stop{};
                    stop.stopObjIdx = static_cast<std::uint32_t>(stc);
                    stop.pos = objs[stc].GetF(39u, 0.0f);
                    stop.argb = objs[stc].GetU(38u, 0xFFFFFFFFu);
                    p.stops.push_back(stop);
                }
            }
            std::sort(p.stops.begin(), p.stops.end(),
                      [](const PrebuiltPaintStop& a, const PrebuiltPaintStop& b) { return a.pos < b.pos; });
        }
    }
    if (p.isStroke) {
        for (std::size_t tc : childrenOf[pIdx]) {
            if (objs[tc].typeKey == 47u) {
                PrebuiltPaintTrim tm{};
                tm.trimObjIdx = static_cast<std::uint32_t>(tc);
                tm.start = objs[tc].GetF(114u, 0.0f);
                tm.end = objs[tc].GetF(115u, 0.0f);
                tm.offset = objs[tc].GetF(116u, 0.0f);
                tm.mode = objs[tc].GetU(117u, 1u);
                p.trims.push_back(tm);
            }
        }
    }
    return p;
}

PrebuiltRiveDocument BuildPrebuiltDocument(const std::vector<RivObject>& allObjs) {
    PrebuiltRiveDocument doc;
    struct Slice {
        std::size_t startIdx = 0u;
        std::size_t endIdx = 0u;
    };
    std::vector<Slice> abSlices;
    for (std::size_t i = 0u; i < allObjs.size(); ++i) {
        if (allObjs[i].typeKey == 1u) {
            if (!abSlices.empty()) abSlices.back().endIdx = i;
            abSlices.push_back({i, allObjs.size()});
        }
    }
    doc.artboards.resize(abSlices.size());

    for (std::size_t ai = 0u; ai < abSlices.size(); ++ai) {
        const std::size_t s0 = abSlices[ai].startIdx;
        const std::size_t s1 = abSlices[ai].endIdx;
        if (s1 <= s0) continue;

        PrebuiltArtboard& ab = doc.artboards[ai];
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
            if (idx > s0 && tk == 28u) {
                RivCubicInterp ci{};
                ci.x1 = std::clamp(allObjs[idx].GetF(63u, 0.42f), 0.0f, 1.0f);
                ci.y1 = allObjs[idx].GetF(64u, 0.0f);
                ci.x2 = std::clamp(allObjs[idx].GetF(65u, 0.58f), 0.0f, 1.0f);
                ci.y2 = allObjs[idx].GetF(66u, 1.0f);
                ab.cubicInterps.emplace_back(static_cast<std::uint32_t>(idx - s0), ci);
                continue;
            }
            if (idx > s0 && tk == 8064u) {
                continue;
            }
            objs.push_back(allObjs[idx]);
        }
        std::sort(ab.cubicInterps.begin(), ab.cubicInterps.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t idx = rawCompEnd; idx < s1; ++idx) {
            objs.push_back(allObjs[idx]);
        }

        std::size_t compEnd = objs.size();
        for (std::size_t i = 1u; i < objs.size(); ++i) {
            if (objs[i].typeKey == 31u || objs[i].typeKey == 53u) {
                compEnd = i;
                break;
            }
        }
        ab.compEnd = compEnd;
        ab.natW = std::max(1.0f, objs[0].GetF(7u, 500.0f));
        ab.natH = std::max(1.0f, objs[0].GetF(8u, 200.0f));
        ab.abOx = objs[0].GetF(11u, 0.0f);
        ab.abOy = objs[0].GetF(12u, 0.0f);

        ab.typeKeys.resize(compEnd, 0u);
        ab.parentIds.resize(compEnd, 0u);
        ab.nodes.resize(compEnd);
        ab.childrenOf.resize(compEnd);
        ab.pathDataIndex.assign(compEnd, -1);
        ab.nodeClips.resize(compEnd);
        ab.shapePaintIndex.assign(compEnd, -1);

        for (std::size_t i = 0u; i < compEnd; ++i) {
            const RivObject& o = objs[i];
            const std::uint32_t tk = o.typeKey;
            ab.typeKeys[i] = tk;
            const std::uint32_t pid = (i == 0u) ? 0u : o.GetU(5u, 0u);
            ab.parentIds[i] = pid;
            if (i > 0u && pid < compEnd) {
                ab.childrenOf[pid].push_back(i);
            }
            PrebuiltNodeBase nb{};
            nb.x = (tk == 41u) ? o.GetF(90u, 0.0f) : o.GetF(13u, 0.0f);
            nb.y = (tk == 41u) ? o.GetF(91u, 0.0f) : o.GetF(14u, 0.0f);
            nb.rot = o.GetF(15u, 0.0f);
            nb.sx = o.GetF(16u, 1.0f);
            nb.sy = o.GetF(17u, 1.0f);
            nb.opacity = std::clamp(o.GetF(18u, 1.0f), 0.0f, 1.0f);
            nb.boneLen = (tk == 40u) ? o.GetF(89u, 0.0f) : 0.0f;
            nb.hidden = (o.GetU(129u, 0u) & 1u) != 0u ? 1u : 0u;
            nb.targetAb = (tk == 92u) ? o.GetU(197u, 0xFFFFFFFFu) : 0xFFFFFFFFu;
            ab.nodes[i] = nb;
        }

        // Precompute ancestor clip flags and per-node clips
        for (std::size_t i = 0u; i < compEnd; ++i) {
            for (std::size_t ch : ab.childrenOf[i]) {
                if (ab.typeKeys[ch] == 42u) {
                    PrebuiltClipRef cr{};
                    cr.clipObjIdx = static_cast<std::uint32_t>(ch);
                    cr.enabled = (objs[ch].GetU(94u, 1u) != 0u);
                    cr.srcId = objs[ch].GetU(92u, 0xFFFFFFFFu);
                    cr.evenOdd = (objs[ch].GetU(93u, 0u) != 0u);
                    ab.nodeClips[i].push_back(cr);
                }
            }
        }
        ab.hasClipInAncestors.assign(compEnd, 0u);
        auto propagateClips = [&](auto& self, std::size_t idx, std::uint8_t parentClip) -> void {
            const std::uint8_t hasHere = (parentClip || !ab.nodeClips[idx].empty()) ? 1u : 0u;
            ab.hasClipInAncestors[idx] = hasHere;
            for (std::size_t ch : ab.childrenOf[idx]) {
                self(self, ch, hasHere);
            }
        };
        if (compEnd > 0u) {
            propagateClips(propagateClips, 0u, 0u);
        }

        // Prebuild path geometry & skinning tables
        for (std::size_t i = 0u; i < compEnd; ++i) {
            const std::uint32_t tk = ab.typeKeys[i];
            if (tk != 16u && tk != 7u && tk != 4u && tk != 8u) {
                continue;
            }
            const std::int32_t pIndex = static_cast<std::int32_t>(ab.paths.size());
            ab.pathDataIndex[i] = pIndex;
            PrebuiltPathData pd{};
            pd.objIdx = static_cast<std::uint32_t>(i);
            pd.tk = tk;
            if (tk == 16u) {
                pd.closed = (objs[i].GetU(32u, 0u) != 0u);
                std::size_t skinIdx = 0u;
                for (std::size_t sc : ab.childrenOf[i]) {
                    if (ab.typeKeys[sc] == 43u) {
                        skinIdx = sc;
                        break;
                    }
                }
                if (skinIdx != 0u) {
                    RivMat2D skinBind{};
                    skinBind.xx = objs[skinIdx].GetF(104u, 1.0f);
                    skinBind.yx = objs[skinIdx].GetF(105u, 0.0f);
                    skinBind.xy = objs[skinIdx].GetF(106u, 0.0f);
                    skinBind.yy = objs[skinIdx].GetF(107u, 1.0f);
                    skinBind.tx = objs[skinIdx].GetF(108u, 0.0f);
                    skinBind.ty = objs[skinIdx].GetF(109u, 0.0f);
                    pd.skinBones.push_back(PrebuiltSkinBone{0u, RivMat2D{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}});
                    for (std::size_t tc : ab.childrenOf[skinIdx]) {
                        if (ab.typeKeys[tc] != 44u) continue;
                        const std::uint32_t boneId = objs[tc].GetU(95u, 0u);
                        RivMat2D tBind{};
                        tBind.xx = objs[tc].GetF(96u, 1.0f);
                        tBind.yx = objs[tc].GetF(97u, 0.0f);
                        tBind.xy = objs[tc].GetF(98u, 0.0f);
                        tBind.yy = objs[tc].GetF(99u, 1.0f);
                        tBind.tx = objs[tc].GetF(100u, 0.0f);
                        tBind.ty = objs[tc].GetF(101u, 0.0f);
                        pd.skinBones.push_back(PrebuiltSkinBone{
                            boneId,
                            RivMat2D::Multiply(RivMat2D::Invert(tBind), skinBind)});
                    }
                }
                for (std::size_t vc : ab.childrenOf[i]) {
                    const std::uint32_t vtk = ab.typeKeys[vc];
                    if (vtk != 5u && vtk != 6u && vtk != 34u && vtk != 35u) continue;
                    PrebuiltVertex pv{};
                    pv.objIdx = static_cast<std::uint32_t>(vc);
                    pv.vtk = vtk;
                    pv.x = objs[vc].GetF(24u, 0.0f);
                    pv.y = objs[vc].GetF(25u, 0.0f);
                    if (vtk == 5u) {
                        pv.p0 = objs[vc].GetF(26u, 0.0f);
                    } else if (vtk == 6u) {
                        pv.p0 = objs[vc].GetF(84u, 0.0f);
                        pv.p1 = objs[vc].GetF(85u, 0.0f);
                        pv.p2 = objs[vc].GetF(86u, 0.0f);
                        pv.p3 = objs[vc].GetF(87u, 0.0f);
                    } else if (vtk == 34u) {
                        pv.p0 = objs[vc].GetF(79u, 0.0f);
                        pv.p1 = objs[vc].GetF(80u, 0.0f);
                        pv.p2 = objs[vc].GetF(81u, 0.0f);
                    } else if (vtk == 35u) {
                        pv.p0 = objs[vc].GetF(82u, 0.0f);
                        pv.p1 = objs[vc].GetF(83u, 0.0f);
                    }
                    for (std::size_t wc : ab.childrenOf[vc]) {
                        const std::uint32_t wtk = ab.typeKeys[wc];
                        if (wtk == 45u || wtk == 46u) {
                            pv.weightObjIdx = static_cast<std::uint32_t>(wc);
                            pv.weightTk = wtk;
                            pv.vVals = objs[wc].GetU(102u, 255u);
                            pv.vIdxs = objs[wc].GetU(103u, 1u);
                            if (wtk == 46u) {
                                pv.inVals = objs[wc].GetU(110u, 255u);
                                pv.inIdxs = objs[wc].GetU(111u, 1u);
                                pv.outVals = objs[wc].GetU(112u, 255u);
                                pv.outIdxs = objs[wc].GetU(113u, 1u);
                            }
                            break;
                        }
                    }
                    pd.verts.push_back(pv);
                }
            } else {
                pd.w = objs[i].GetF(20u, 0.0f);
                pd.h = objs[i].GetF(21u, 0.0f);
                pd.ox = objs[i].GetF(123u, 0.5f);
                pd.oy = objs[i].GetF(124u, 0.5f);
                if (tk == 7u) {
                    pd.rTL = objs[i].GetF(31u, 0.0f);
                    pd.linked = (objs[i].GetU(164u, 1u) != 0u);
                    pd.rTR = pd.linked ? pd.rTL : objs[i].GetF(161u, pd.rTL);
                    pd.rBL = pd.linked ? pd.rTL : objs[i].GetF(162u, pd.rTL);
                    pd.rBR = pd.linked ? pd.rTL : objs[i].GetF(163u, pd.rTL);
                }
            }
            ab.paths.push_back(std::move(pd));
        }

        // Prebuild Artboard background paints and per-shape paints + descending renderOrder
        for (std::size_t bgPaintIdx : ab.childrenOf[0]) {
            if (ab.typeKeys[bgPaintIdx] == 20u) {
                ab.bgPaints.push_back(BuildPrebuiltPaintDef(objs, ab.childrenOf, bgPaintIdx));
            }
        }
        for (std::size_t i = compEnd; i-- > 1u;) {
            const std::uint32_t tk = ab.typeKeys[i];
            if (tk != 92u && tk != 3u) continue;
            ab.renderOrder.push_back(i);
            if (tk == 3u) {
                std::vector<PrebuiltPaintDef> sp;
                for (std::size_t pIdx : ab.childrenOf[i]) {
                    const std::uint32_t ptk = ab.typeKeys[pIdx];
                    if (ptk == 20u || ptk == 24u) {
                        sp.push_back(BuildPrebuiltPaintDef(objs, ab.childrenOf, pIdx));
                    }
                }
                ab.shapePaintIndex[i] = static_cast<std::int32_t>(ab.shapePaints.size());
                ab.shapePaints.push_back(std::move(sp));
            }
        }

        // Pre-parse animations (tk == 31u)
        for (std::size_t i = compEnd; i < objs.size(); ++i) {
            if (objs[i].typeKey == 31u) {
                PrebuiltAnimation anim{};
                const RivObject& ao = objs[i];
                anim.timing.fps = static_cast<float>(std::max(1u, ao.GetU(56u, 60u)));
                const std::uint32_t durFrames = std::max(1u, ao.GetU(57u, 60u));
                anim.timing.speed = ao.GetF(58u, 1.0f);
                if (std::fabs(anim.timing.speed) < 1e-4f) anim.timing.speed = 1.0f;
                anim.timing.loop = ao.GetU(59u, 0u);
                const bool enWork = ao.GetU(62u, 0u) != 0u;
                const std::uint32_t ws = ao.GetU(60u, 0u);
                const std::uint32_t we = ao.GetU(61u, 0u);
                anim.timing.fStart = enWork ? static_cast<float>(ws) : 0.0f;
                anim.timing.fEnd = (enWork && we > ws) ? static_cast<float>(we) : static_cast<float>(durFrames);
                anim.timing.fSpan = std::max(1.0f, anim.timing.fEnd - anim.timing.fStart);
                anim.timing.durationSec = anim.timing.fSpan / std::max(1.0f, anim.timing.fps * std::fabs(anim.timing.speed));

                std::size_t curTargetObj = 0xFFFFFFFFu;
                std::uint32_t curPropKey = 0u;
                std::vector<PrebuiltKfEntry> propKfs;
                auto flushKfs = [&]() {
                    if (!propKfs.empty() && curTargetObj < compEnd && curPropKey != 0u) {
                        PrebuiltPropTrack trk{};
                        trk.targetObj = curTargetObj;
                        trk.propKey = curPropKey;
                        trk.kfs = std::move(propKfs);
                        anim.tracks.push_back(std::move(trk));
                    }
                    propKfs.clear();
                };
                for (std::size_t k = i + 1u; k < objs.size(); ++k) {
                    const std::uint32_t tk = objs[k].typeKey;
                    if (tk == 31u || tk == 53u) break;
                    if (tk == 25u) {
                        flushKfs();
                        curTargetObj = objs[k].GetU(51u, 0xFFFFFFFFu);
                        curPropKey = 0u;
                    } else if (tk == 26u) {
                        flushKfs();
                        curPropKey = objs[k].GetU(53u, 0u);
                    } else if (tk == 30u || tk == 37u || tk == 50u || tk == 84u) {
                        PrebuiltKfEntry e{};
                        e.tk = tk;
                        e.frame = static_cast<float>(objs[k].GetU(67u, 0u));
                        e.interp = objs[k].GetU(68u, 0u);
                        e.interpId = objs[k].GetU(69u, 0u);
                        if (tk == 30u) e.fVal = objs[k].GetF(70u, 0.0f);
                        else if (tk == 37u) e.uVal = objs[k].GetU(88u, 0u);
                        else e.uVal = objs[k].GetU(122u, 0u);
                        propKfs.push_back(e);
                    }
                }
                flushKfs();
                ab.animations.push_back(std::move(anim));
            } else if (objs[i].typeKey == 53u) {
                PrebuiltStateMachine sm{};
                for (std::size_t k = i + 1u; k < objs.size(); ++k) {
                    const std::uint32_t tk = objs[k].typeKey;
                    if (tk == 53u) break;
                    if (tk == 56u || tk == 58u || tk == 59u) {
                        PrebuiltSmInput inp{};
                        inp.name = std::string(objs[k].GetS(138u));
                        if (tk == 56u) inp.defaultVal = objs[k].GetF(140u, 0.0f);
                        else if (tk == 59u) inp.defaultVal = objs[k].GetU(141u, 0u) != 0u ? 1.0f : 0.0f;
                        sm.inputs.push_back(std::move(inp));
                    }
                }
                for (std::size_t k = i + 1u; k < objs.size(); ++k) {
                    if (objs[k].typeKey == 53u) break;
                    if (objs[k].typeKey != 57u) continue;
                    PrebuiltSmLayer layer{};
                    std::size_t p = k + 1u;
                    for (; p < objs.size(); ++p) {
                        const std::uint32_t tk = objs[p].typeKey;
                        if (tk == 57u || tk == 53u) break;
                        if (tk == 61u || tk == 62u || tk == 63u || tk == 64u || tk == 73u || tk == 74u) {
                            PrebuiltSmLayerState st{};
                            st.tk = tk;
                            if (tk == 61u) st.animId = objs[p].GetU(149u, 0u);
                            if (tk == 63u && layer.entryState == 0u) layer.entryState = layer.states.size();
                            layer.states.push_back(std::move(st));
                        } else if ((tk == 65u || tk == 78u) && !layer.states.empty()) {
                            PrebuiltSmTransition tr{};
                            tr.toState = objs[p].GetU(151u, 0u);
                            tr.flags = objs[p].GetU(152u, 0u);
                            tr.exitTime = objs[p].GetF(160u, 0.0f);
                            layer.states.back().transitions.push_back(std::move(tr));
                        } else if ((tk == 67u || tk == 68u || tk == 69u || tk == 70u || tk == 71u) &&
                                   !layer.states.empty() && !layer.states.back().transitions.empty()) {
                            const std::uint32_t inpId = objs[p].GetU(155u, 0u);
                            const std::uint32_t op = objs[p].GetU(156u, 0u);
                            const float defV = (tk == 68u || tk == 71u) ? 1.0f : 0.0f;
                            const float cv = objs[p].Has(157u) ? objs[p].GetF(157u, 0.0f) : defV;
                            layer.states.back().transitions.back().conds.emplace_back(inpId, op, cv);
                        }
                    }
                    for (std::size_t si = 0u; si < layer.states.size(); ++si) {
                        if (layer.states[si].tk == 63u) {
                            layer.entryState = si;
                            break;
                        }
                    }
                    sm.layers.push_back(std::move(layer));
                    k = p - 1u;
                }
                ab.stateMachines.push_back(std::move(sm));
            }
        }
    }
    return doc;
}

// Flat zero-alloc intrusive override table (replaces std::unordered_map)
struct RivOverrideTable {
    struct OvEntry {
        std::uint32_t objIdx = 0u;
        std::uint32_t propKey = 0u;
        float f = 0.0f;
        std::uint64_t u = 0u;
        std::uint8_t kind = 0u;
        std::int32_t next = -1;
    };

    std::vector<std::int32_t> headOfObj;
    std::vector<std::uint32_t> dirtyObjs;
    std::vector<OvEntry> pool;

    void Reset(std::size_t compEnd) {
        if (headOfObj.size() != compEnd) {
            headOfObj.assign(compEnd, -1);
        } else {
            for (const std::uint32_t idx : dirtyObjs) {
                if (idx < headOfObj.size()) {
                    headOfObj[idx] = -1;
                }
            }
        }
        dirtyObjs.clear();
        pool.clear();
    }

    void Set(std::size_t objIdx, std::uint32_t propKey, const RivPropVal& pv) {
        if (objIdx >= headOfObj.size()) return;
        std::int32_t cur = headOfObj[objIdx];
        if (cur < 0) {
            dirtyObjs.push_back(static_cast<std::uint32_t>(objIdx));
        } else {
            while (cur >= 0) {
                OvEntry& e = pool[static_cast<std::size_t>(cur)];
                if (e.propKey == propKey) {
                    e.f = pv.f;
                    e.u = pv.u;
                    e.kind = pv.kind;
                    return;
                }
                cur = e.next;
            }
        }
        const auto newIdx = static_cast<std::int32_t>(pool.size());
        pool.push_back(OvEntry{
            static_cast<std::uint32_t>(objIdx),
            propKey,
            pv.f,
            pv.u,
            pv.kind,
            headOfObj[objIdx]});
        headOfObj[objIdx] = newIdx;
    }

    [[nodiscard]] bool IsDirty(std::size_t objIdx) const noexcept {
        return objIdx < headOfObj.size() && headOfObj[objIdx] >= 0;
    }

    [[nodiscard]] std::uint32_t GetU(std::size_t objIdx, std::uint32_t k, std::uint32_t defVal) const noexcept {
        if (objIdx < headOfObj.size()) {
            std::int32_t cur = headOfObj[objIdx];
            while (cur >= 0) {
                const OvEntry& e = pool[static_cast<std::size_t>(cur)];
                if (e.propKey == k) {
                    return static_cast<std::uint32_t>(e.u);
                }
                cur = e.next;
            }
        }
        return defVal;
    }

    [[nodiscard]] float GetF(std::size_t objIdx, std::uint32_t k, float defVal) const noexcept {
        if (objIdx < headOfObj.size()) {
            std::int32_t cur = headOfObj[objIdx];
            while (cur >= 0) {
                const OvEntry& e = pool[static_cast<std::size_t>(cur)];
                if (e.propKey == k) {
                    return e.f;
                }
                cur = e.next;
            }
        }
        return defVal;
    }
};

using RivInputPairs = std::vector<std::pair<std::string_view, float>>;

[[nodiscard]] float LookupInputVal(
    const RivInputPairs& inputs,
    std::string_view name,
    float defVal) noexcept {
    for (const auto& [k, v] : inputs) {
        if (k == name) return v;
    }
    return defVal;
}

void EvaluatePrebuiltAnimation(
    const PrebuiltArtboard& ab,
    std::size_t animIdx,
    float timeSec,
    RivOverrideTable& overrides) {
    if (animIdx >= ab.animations.size()) return;
    const PrebuiltAnimation& anim = ab.animations[animIdx];
    const PrebuiltTimingInfo& ti = anim.timing;
    const float rawFrames = std::max(0.0f, timeSec) * ti.fps * std::fabs(ti.speed);
    float curFrame = ti.fStart;
    if (ti.loop == 1u) {
        float m = std::fmod(rawFrames, ti.fSpan);
        if (m < 0.0f) m += ti.fSpan;
        curFrame = ti.fStart + m;
    } else if (ti.loop == 2u) {
        const float cyc = std::fmod(rawFrames, 2.0f * ti.fSpan);
        curFrame = ti.fStart + ((cyc <= ti.fSpan) ? cyc : (2.0f * ti.fSpan - cyc));
    } else {
        curFrame = std::clamp(ti.fStart + rawFrames, ti.fStart, ti.fEnd);
    }

    for (const PrebuiltPropTrack& trk : anim.tracks) {
        const auto& propKfs = trk.kfs;
        if (propKfs.empty()) continue;
        const PrebuiltKfEntry* k0 = &propKfs.front();
        const PrebuiltKfEntry* k1 = &propKfs.front();
        float localT = 0.0f;
        if (curFrame <= propKfs.front().frame || propKfs.size() == 1u) {
            k0 = k1 = &propKfs.front();
        } else if (curFrame >= propKfs.back().frame) {
            k0 = k1 = &propKfs.back();
        } else {
            std::size_t hitIdx = 0u;
            if (propKfs.size() > 8u) {
                const auto it = std::upper_bound(
                    propKfs.begin(), propKfs.end(), curFrame,
                    [](float fVal, const PrebuiltKfEntry& kf) { return fVal < kf.frame; });
                const std::size_t hi = static_cast<std::size_t>(it - propKfs.begin());
                hitIdx = (hi > 0u) ? std::min(hi - 1u, propKfs.size() - 2u) : 0u;
            } else {
                for (std::size_t i = 0u; i + 1u < propKfs.size(); ++i) {
                    if (curFrame >= propKfs[i].frame && curFrame < propKfs[i + 1u].frame) {
                        hitIdx = i;
                        break;
                    }
                }
            }
            k0 = &propKfs[hitIdx];
            k1 = &propKfs[hitIdx + 1u];
            const float span = k1->frame - k0->frame;
            if (span > 1e-4f && k0->interp != 1u) {
                const float u = (curFrame - k0->frame) / span;
                localT = (k0->interp == 2u) ? EvalCubicEase(ab.cubicInterps, k0->interpId, u) : u;
            }
        }
        RivPropVal pv{};
        if (k0->tk == 30u) {
            pv.kind = 2u;
            pv.f = k0->fVal + (k1->fVal - k0->fVal) * localT;
            pv.u = static_cast<std::uint64_t>(std::max(0.0f, pv.f));
        } else if (k0->tk == 37u) {
            pv.kind = 3u;
            pv.u = (k0 == k1 || k0->interp == 1u)
                ? k0->uVal
                : LerpArgb(static_cast<std::uint32_t>(k0->uVal), static_cast<std::uint32_t>(k1->uVal), localT);
        } else {
            pv.kind = 0u;
            pv.u = k0->uVal;
            pv.f = static_cast<float>(pv.u);
        }
        overrides.Set(trk.targetObj, trk.propKey, pv);
    }
}

void EvaluateArtboardOverrides(
    const PrebuiltArtboard& ab,
    float tLoopSec,
    float tStateSec,
    const RivInputPairs& inputs,
    RivOverrideTable& overrides) {
    overrides.Reset(ab.compEnd);
    if (!ab.stateMachines.empty()) {
        const PrebuiltStateMachine& sm = ab.stateMachines.front();
        float stackInputs[32];
        std::vector<float> heapInputs;
        float* smInputVals = stackInputs;
        const std::size_t inpCount = sm.inputs.size();
        if (inpCount > 32u) {
            heapInputs.resize(inpCount);
            smInputVals = heapInputs.data();
        }
        for (std::size_t ii = 0u; ii < inpCount; ++ii) {
            smInputVals[ii] = LookupInputVal(inputs, sm.inputs[ii].name, sm.inputs[ii].defaultVal);
        }
        auto evalConds = [&](const PrebuiltSmTransition& tr) -> bool {
            for (const auto& [inpId, op, cval] : tr.conds) {
                if (inpId >= inpCount) return false;
                const float iv = smInputVals[inpId];
                if (op == 0u && !(std::fabs(iv - cval) < 0.01f)) return false;
                if (op == 1u && !(std::fabs(iv - cval) >= 0.01f)) return false;
                if (op == 2u && !(iv < cval)) return false;
                if (op == 3u && !(iv <= cval)) return false;
                if (op == 4u && !(iv > cval)) return false;
                if (op == 5u && !(iv >= cval)) return false;
            }
            return true;
        };
        for (const PrebuiltSmLayer& layer : sm.layers) {
            const auto& states = layer.states;
            std::size_t curState = layer.entryState;
            float remStateTime = tStateSec;
            for (int hop = 0; hop < 8 && curState < states.size(); ++hop) {
                bool moved = false;
                const bool isEntry = (states[curState].tk == 63u);
                float curStateDur = 0.0f;
                if (!isEntry && states[curState].animId < ab.animations.size()) {
                    curStateDur = ab.animations[states[curState].animId].timing.durationSec;
                }
                for (const auto& tr : states[curState].transitions) {
                    if (tr.toState >= states.size() || tr.toState == curState) continue;
                    const bool needsExit = !isEntry && (tr.conds.empty() || (tr.flags & 4u) != 0u);
                    float reqSec = 0.0f;
                    if (needsExit) {
                        if (tr.exitTime <= 0.0f) reqSec = curStateDur;
                        else if ((tr.flags & 8u) != 0u) reqSec = curStateDur * std::clamp(tr.exitTime * 0.01f, 0.0f, 1.0f);
                        else reqSec = std::min(curStateDur, tr.exitTime * 0.001f);
                        if (remStateTime + 1e-4f < reqSec) continue;
                    }
                    if (evalConds(tr)) {
                        curState = tr.toState;
                        if (reqSec > 0.0f) remStateTime = std::max(0.0f, remStateTime - reqSec);
                        moved = true;
                        break;
                    }
                }
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
            if (curState < states.size() && states[curState].animId < ab.animations.size()) {
                const std::size_t aIdx = states[curState].animId;
                const float useTime = (ab.animations[aIdx].timing.loop != 0u) ? tLoopSec : remStateTime;
                EvaluatePrebuiltAnimation(ab, aIdx, useTime, overrides);
            }
        }
    } else if (!ab.animations.empty()) {
        const float useTime = (ab.animations[0].timing.loop != 0u) ? tLoopSec : tStateSec;
        EvaluatePrebuiltAnimation(ab, 0u, useTime, overrides);
    }
}

struct EvaluatedArtboardTree {
    std::size_t abIndex = 0u;
    RivOverrideTable overrides;
    std::vector<RivMat2D> worldMats;
    std::vector<float> worldOpacity;
    std::vector<std::uint8_t> worldHidden;
    std::vector<std::pair<std::size_t, EvaluatedArtboardTree>> nestedChildren; // (objectIdx, child)
};

inline void HashMix64(std::uint64_t& h, std::uint64_t v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6u) + (h >> 2u);
}

void BuildEvaluatedTreeRecursive(
    const PrebuiltRiveDocument& doc,
    std::size_t abIndex,
    const RivMat2D& rootTransform,
    float inheritedOpacity,
    float tLoopSec,
    float tStateSec,
    const RivInputPairs& inputs,
    int depth,
    EvaluatedArtboardTree& outNode,
    std::uint64_t& ioPoseHash) {
    if (abIndex >= doc.artboards.size() || depth > 5) return;
    const PrebuiltArtboard& ab = doc.artboards[abIndex];
    const std::size_t compEnd = ab.compEnd;
    if (compEnd == 0u) return;

    outNode.abIndex = abIndex;
    EvaluateArtboardOverrides(ab, tLoopSec, tStateSec, inputs, outNode.overrides);

    HashMix64(ioPoseHash, static_cast<std::uint64_t>(abIndex));
    if (!outNode.overrides.pool.empty()) {
        for (const auto& e : outNode.overrides.pool) {
            HashMix64(ioPoseHash, (static_cast<std::uint64_t>(e.objIdx) << 32u) | e.propKey);
            std::uint32_t fbits = 0u;
            std::memcpy(&fbits, &e.f, 4u);
            HashMix64(ioPoseHash, (static_cast<std::uint64_t>(e.kind) << 32u) | fbits);
            HashMix64(ioPoseHash, e.u);
        }
    } else {
        HashMix64(ioPoseHash, 0xA5A5A5A5ULL);
    }

    const auto& tks = ab.typeKeys;
    const auto& nodes = ab.nodes;
    const auto& ov = outNode.overrides;
    outNode.worldMats.resize(compEnd);
    outNode.worldOpacity.assign(compEnd, inheritedOpacity);
    outNode.worldHidden.assign(compEnd, 0u);
    outNode.worldMats[0] = (depth == 0)
        ? RivMat2D::Multiply(rootTransform, RivMat2D::FromTRS(ab.abOx * ab.natW, ab.abOy * ab.natH, 0.0f, 1.0f, 1.0f))
        : rootTransform;

    auto computeTransform = [&](auto& self, std::size_t idx) -> void {
        for (std::size_t ch : ab.childrenOf[idx]) {
            const std::uint32_t tk = tks[ch];
            const PrebuiltNodeBase& nb = nodes[ch];
            float lx = nb.x;
            float ly = nb.y;
            float rot = nb.rot;
            float sx = nb.sx;
            float sy = nb.sy;
            float op = nb.opacity;
            bool selfHidden = (nb.hidden != 0u);
            if (ov.IsDirty(ch)) {
                if (tk == 41u) {
                    lx = ov.GetF(ch, 90u, nb.x);
                    ly = ov.GetF(ch, 91u, nb.y);
                } else {
                    lx = ov.GetF(ch, 13u, nb.x);
                    ly = ov.GetF(ch, 14u, nb.y);
                }
                rot = ov.GetF(ch, 15u, nb.rot);
                sx = ov.GetF(ch, 16u, nb.sx);
                sy = ov.GetF(ch, 17u, nb.sy);
                op = std::clamp(ov.GetF(ch, 18u, nb.opacity), 0.0f, 1.0f);
                selfHidden = (ov.GetU(ch, 129u, nb.hidden) & 1u) != 0u;
            }
            if (tk == 40u && tks[idx] == 40u) {
                lx = ov.IsDirty(idx) ? ov.GetF(idx, 89u, nodes[idx].boneLen) : nodes[idx].boneLen;
                ly = 0.0f;
            }

            outNode.worldMats[ch] = RivMat2D::Multiply(outNode.worldMats[idx], RivMat2D::FromTRS(lx, ly, rot, sx, sy));
            outNode.worldOpacity[ch] = outNode.worldOpacity[idx] * op;
            outNode.worldHidden[ch] = (outNode.worldHidden[idx] || selfHidden) ? 1u : 0u;

            if (tk == 92u && !outNode.worldHidden[ch] && outNode.worldOpacity[ch] > 0.002f) {
                const std::uint32_t targetAb = ov.IsDirty(ch) ? ov.GetU(ch, 197u, nb.targetAb) : nb.targetAb;
                if (targetAb < doc.artboards.size() && targetAb != abIndex) {
                    const PrebuiltArtboard& childAb = doc.artboards[targetAb];
                    const RivMat2D nestedWorld = RivMat2D::Multiply(
                        outNode.worldMats[ch],
                        RivMat2D::FromTRS(-childAb.abOx * childAb.natW, -childAb.abOy * childAb.natH, 0.0f, 1.0f, 1.0f));
                    outNode.nestedChildren.emplace_back(ch, EvaluatedArtboardTree{});
                    BuildEvaluatedTreeRecursive(
                        doc, targetAb, nestedWorld, outNode.worldOpacity[ch],
                        tLoopSec, tStateSec, inputs, depth + 1,
                        outNode.nestedChildren.back().second, ioPoseHash);
                }
            }
            self(self, ch);
        }
    };
    computeTransform(computeTransform, 0u);
}

// ============================================================================
// Vector Geometry Helpers
// ============================================================================

void FlattenCubic(
    RivVec2 p0, RivVec2 p1, RivVec2 p2, RivVec2 p3,
    int depth, std::vector<RivVec2>& out) {
    const float dx = p3.x - p0.x;
    const float dy = p3.y - p0.y;
    const float d2 = std::fabs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    const float d3 = std::fabs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    if (depth >= 5 || (d2 + d3) * (d2 + d3) <= 0.55f * (dx * dx + dy * dy + 1.0f)) {
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
    if (FastHypot(pEnd.x - seg.pts.back().x, pEnd.y - seg.pts.back().y) > 1e-5f) {
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
    if (span <= 1e-4f) return;
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
            cd[s + 1u] = cd[s] + FastHypot(b.x - a.x, b.y - a.y);
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

// ============================================================================
// High-Speed Y-Bucketed AET Scanline Rasterizer + Gradient LUT + Clip Cache
// ============================================================================

inline void UnpackArgbPremul(std::uint32_t argb, float extraAlpha, float outPremul[4]) noexcept {
    const float a = std::clamp((static_cast<float>((argb >> 24u) & 0xFFu) / 255.0f) * extraAlpha, 0.0f, 1.0f);
    const float r = static_cast<float>((argb >> 16u) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((argb >> 8u) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(argb & 0xFFu) / 255.0f;
    outPremul[0] = r * a;
    outPremul[1] = g * a;
    outPremul[2] = b * a;
    outPremul[3] = a;
}

[[nodiscard]] inline std::uint32_t Div255(std::uint32_t x) noexcept {
    return (x + 128u + ((x + 128u) >> 8u)) >> 8u;
}

inline void UnpackArgbPremulU8(std::uint32_t argb, float extraAlpha, std::uint8_t outPremul[4]) noexcept {
    const float a = std::clamp((static_cast<float>((argb >> 24u) & 0xFFu) / 255.0f) * extraAlpha, 0.0f, 1.0f);
    const float r = static_cast<float>((argb >> 16u) & 0xFFu);
    const float g = static_cast<float>((argb >> 8u) & 0xFFu);
    const float b = static_cast<float>(argb & 0xFFu);
    outPremul[0] = static_cast<std::uint8_t>(std::clamp(r * a + 0.5f, 0.0f, 255.0f));
    outPremul[1] = static_cast<std::uint8_t>(std::clamp(g * a + 0.5f, 0.0f, 255.0f));
    outPremul[2] = static_cast<std::uint8_t>(std::clamp(b * a + 0.5f, 0.0f, 255.0f));
    outPremul[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
}

void BuildGradientLut256U8(
    const std::vector<RivGradStop>& stops,
    float extraAlpha,
    std::array<std::uint8_t, 256u * 4u>& outLut) noexcept {
    if (stops.empty()) {
        outLut.fill(0u);
        return;
    }
    auto storeFloat = [](const float c[4], std::uint8_t* dst) noexcept {
        dst[0] = static_cast<std::uint8_t>(std::clamp(c[0] * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[1] = static_cast<std::uint8_t>(std::clamp(c[1] * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[2] = static_cast<std::uint8_t>(std::clamp(c[2] * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[3] = static_cast<std::uint8_t>(std::clamp(c[3] * 255.0f + 0.5f, 0.0f, 255.0f));
    };
    for (std::size_t i = 0u; i < 256u; ++i) {
        const float ct = static_cast<float>(i) / 255.0f;
        std::uint8_t* dst = outLut.data() + i * 4u;
        if (ct <= stops.front().pos || stops.size() == 1u) {
            float c[4];
            UnpackArgbPremul(stops.front().argb, extraAlpha, c);
            storeFloat(c, dst);
            continue;
        }
        if (ct >= stops.back().pos) {
            float c[4];
            UnpackArgbPremul(stops.back().argb, extraAlpha, c);
            storeFloat(c, dst);
            continue;
        }
        bool matched = false;
        for (std::size_t s = 0u; s + 1u < stops.size(); ++s) {
            if (ct >= stops[s].pos && ct <= stops[s + 1u].pos) {
                const float span = std::max(1e-5f, stops[s + 1u].pos - stops[s].pos);
                const float f = (ct - stops[s].pos) / span;
                float c0[4], c1[4];
                UnpackArgbPremul(stops[s].argb, extraAlpha, c0);
                UnpackArgbPremul(stops[s + 1u].argb, extraAlpha, c1);
                const float cMix[4]{
                    c0[0] + (c1[0] - c0[0]) * f,
                    c0[1] + (c1[1] - c0[1]) * f,
                    c0[2] + (c1[2] - c0[2]) * f,
                    c0[3] + (c1[3] - c0[3]) * f
                };
                storeFloat(cMix, dst);
                matched = true;
                break;
            }
        }
        if (!matched) {
            float c[4];
            UnpackArgbPremul(stops.back().argb, extraAlpha, c);
            storeFloat(c, dst);
        }
    }
}

struct AetEdge {
    int firstSub = 0;
    int lastSub = -1; // inclusive
    float x = 0.0f;
    float stepX = 0.0f;
    int dir = 1;
};

struct ClipMaskBuf {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<std::uint8_t> mask;
};

struct RiveRasterScratch {
    std::vector<AetEdge> edges;
    std::vector<AetEdge> active;
    std::vector<std::pair<float, int>> xings;
    std::vector<float> rowCov;
    std::vector<std::uint8_t> surfPremulU8;
    std::vector<RivVertDesc> rawVerts;
    std::vector<RivVertDesc> skinnedVerts;
    std::vector<RivVertDesc> effVerts;
    std::vector<RivSubpath> shapePaths;
    std::vector<RivSubpath> strokeExpanded;
    std::vector<RivSubpath> trimmedPaths;
    std::vector<RivSubpath> nextTrimmed;
    std::vector<RivSubpath> clipPaths;
    std::vector<const ClipMaskBuf*> activeClips;
    std::vector<RivMat2D> skinDeformMats;
    std::vector<RivVec2> strokeClean;
    std::vector<RivVec2> strokeNormals;
    std::vector<RivVec2> strokeLeftSide;
    std::vector<RivVec2> strokeRightSide;
};

RiveRasterScratch& ThreadRasterScratch() {
    thread_local RiveRasterScratch scratch;
    return scratch;
}

void ExpandStrokeToSubpaths(
    const std::vector<RivSubpath>& src,
    float halfW,
    std::uint32_t cap,
    RiveRasterScratch& rsc,
    std::vector<RivSubpath>& out) {
    if (halfW <= 0.02f) return;
    auto& clean = rsc.strokeClean;
    auto& normals = rsc.strokeNormals;
    auto& leftSide = rsc.strokeLeftSide;
    auto& rightSide = rsc.strokeRightSide;

    for (const RivSubpath& sp : src) {
        if (sp.pts.size() < 2u) continue;
        clean.clear();
        for (const RivVec2& p : sp.pts) {
            if (clean.empty() || FastHypot(p.x - clean.back().x, p.y - clean.back().y) > 0.02f) {
                clean.push_back(p);
            }
        }
        if (sp.closed && clean.size() >= 2u &&
            FastHypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) <= 0.02f) {
            clean.pop_back();
        }
        if (clean.size() < 2u) continue;

        const std::size_t n = clean.size();
        const std::size_t segCount = sp.closed ? n : (n - 1u);
        normals.resize(segCount);
        for (std::size_t i = 0u; i < segCount; ++i) {
            const RivVec2 a = clean[i];
            const RivVec2 b = clean[(i + 1u) % n];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::max(1e-4f, FastHypot(dx, dy));
            normals[i] = {-dy / len, dx / len};
        }

        leftSide.clear();
        rightSide.clear();

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
            RivSubpath poly{};
            poly.closed = true;
            poly.pts = leftSide;
            out.push_back(std::move(poly));
            RivSubpath inner{};
            inner.closed = true;
            inner.pts.assign(rightSide.rbegin(), rightSide.rend());
            out.push_back(std::move(inner));
        } else {
            RivSubpath poly{};
            poly.closed = true;
            poly.pts = leftSide;
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

template <typename RowCallback>
void RasterizeScanlinesStreaming(
    int surfW, int surfH,
    const std::vector<RivSubpath>& subpaths,
    bool evenOdd,
    RowCallback&& onRow) {
    if (surfW <= 0 || surfH <= 0) return;

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const RivSubpath& sp : subpaths) {
        if (sp.pts.size() < 2u) continue;
        for (const RivVec2& pt : sp.pts) {
            minX = std::min(minX, pt.x);
            minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x);
            maxY = std::max(maxY, pt.y);
        }
    }
    if (minX >= maxX || minY >= maxY) return;

    const int outX0 = std::clamp(static_cast<int>(std::floor(minX)), 0, surfW);
    const int outY0 = std::clamp(static_cast<int>(std::floor(minY)), 0, surfH);
    const int x1 = std::clamp(static_cast<int>(std::ceil(maxX)), 0, surfW);
    const int y1 = std::clamp(static_cast<int>(std::ceil(maxY)), 0, surfH);
    const int outW = x1 - outX0;
    const int outH = y1 - outY0;
    if (outW <= 0 || outH <= 0) return;

    constexpr int kSubY = 2;
    static constexpr float kOffsetsY[kSubY] = {0.25f, 0.75f};
    const int totalSubs = outH * kSubY;

    auto& sc = ThreadRasterScratch();
    sc.edges.clear();

    for (const RivSubpath& sp : subpaths) {
        if (sp.pts.size() < 2u) continue;
        const std::size_t n = sp.pts.size();
        for (std::size_t i = 0u; i < n; ++i) {
            RivVec2 a = sp.pts[i];
            RivVec2 b = sp.pts[(i + 1u) % n];
            if (std::fabs(a.y - b.y) < 1e-5f) continue;
            int dir = 1;
            if (a.y > b.y) {
                std::swap(a, b);
                dir = -1;
            }
            if (b.y <= static_cast<float>(outY0) || a.y >= static_cast<float>(y1)) {
                continue;
            }
            auto subY = [&](int sub) noexcept -> float {
                return static_cast<float>(outY0) + static_cast<float>(sub >> 1) + kOffsetsY[sub & 1];
            };
            const int firstSub = std::max(0, static_cast<int>(std::ceil((a.y - static_cast<float>(outY0)) * 2.0f - 0.5f - 1e-5f)));
            int lastSub = std::min(totalSubs - 1, static_cast<int>(std::ceil((b.y - static_cast<float>(outY0)) * 2.0f - 0.5f - 1e-5f)) - 1);
            int s0 = firstSub;
            while (s0 > 0 && subY(s0 - 1) >= a.y) --s0;
            while (s0 < totalSubs && subY(s0) < a.y) ++s0;
            int s1 = std::min(totalSubs - 1, lastSub + 1);
            while (s1 >= s0 && subY(s1) >= b.y) --s1;
            if (s0 > s1) continue;

            const float invSlope = (b.x - a.x) / (b.y - a.y);
            const float startX = a.x + (subY(s0) - a.y) * invSlope;
            sc.edges.push_back(AetEdge{s0, s1, startX, invSlope * 0.5f, dir});
        }
    }
    if (sc.edges.empty()) return;

    std::sort(sc.edges.begin(), sc.edges.end(), [](const AetEdge& e1, const AetEdge& e2) {
        return e1.firstSub < e2.firstSub;
    });

    sc.active.clear();
    sc.rowCov.assign(static_cast<std::size_t>(outW), 0.0f);
    std::size_t edgeCursor = 0u;

    for (int py = outY0; py < y1; ++py) {
        const int rowBaseSub = (py - outY0) * kSubY;
        if (sc.active.empty() && edgeCursor < sc.edges.size() &&
            sc.edges[edgeCursor].firstSub >= rowBaseSub + kSubY) {
            continue;
        }

        int rowMinRx = outW;
        int rowMaxRx = -1;

        for (int sy = 0; sy < kSubY; ++sy) {
            const int sub = rowBaseSub + sy;
            while (edgeCursor < sc.edges.size() && sc.edges[edgeCursor].firstSub <= sub) {
                sc.active.push_back(sc.edges[edgeCursor++]);
            }
            for (std::size_t ai = sc.active.size(); ai > 0u; --ai) {
                if (sc.active[ai - 1u].lastSub < sub) {
                    sc.active[ai - 1u] = sc.active.back();
                    sc.active.pop_back();
                }
            }
            if (sc.active.size() < 2u) {
                for (AetEdge& ae : sc.active) {
                    ae.x += ae.stepX;
                }
                continue;
            }

            sc.xings.clear();
            for (AetEdge& ae : sc.active) {
                sc.xings.emplace_back(ae.x, ae.dir);
                ae.x += ae.stepX;
            }
            for (std::size_t i = 1u; i < sc.xings.size(); ++i) {
                const auto key = sc.xings[i];
                std::size_t j = i;
                while (j > 0u && sc.xings[j - 1u].first > key.first) {
                    sc.xings[j] = sc.xings[j - 1u];
                    --j;
                }
                sc.xings[j] = key;
            }

            int winding = 0;
            float spanStart = 0.0f;
            bool inside = false;
            for (const auto& [xv, dir] : sc.xings) {
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
                        const int rx0 = ix0 - outX0;
                        const int rx1 = ix1 - outX0;
                        rowMinRx = std::min(rowMinRx, rx0);
                        rowMaxRx = std::max(rowMaxRx, rx1);
                        if (ix0 == ix1) {
                            sc.rowCov[static_cast<std::size_t>(rx0)] += (s1 - s0) * 0.5f;
                        } else {
                            sc.rowCov[static_cast<std::size_t>(rx0)] += (static_cast<float>(ix0 + 1) - s0) * 0.5f;
                            for (int rx = rx0 + 1; rx < rx1; ++rx) {
                                sc.rowCov[static_cast<std::size_t>(rx)] += 0.5f;
                            }
                            sc.rowCov[static_cast<std::size_t>(rx1)] += (s1 - static_cast<float>(ix1)) * 0.5f;
                        }
                    }
                }
            }
        }

        if (rowMinRx <= rowMaxRx) {
            const int spanW = (rowMaxRx - rowMinRx) + 1;
            onRow(py, outX0 + rowMinRx, spanW, sc.rowCov.data() + rowMinRx);
            std::fill_n(sc.rowCov.data() + rowMinRx, static_cast<std::size_t>(spanW), 0.0f);
        }
    }
}

void RasterizeCoverageMaskIntoBuffer(
    int surfW, int surfH,
    const std::vector<RivSubpath>& subpaths,
    bool evenOdd,
    int& outX0, int& outY0, int& outW, int& outH,
    std::vector<std::uint8_t>& outMask) {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const RivSubpath& sp : subpaths) {
        for (const RivVec2& pt : sp.pts) {
            minX = std::min(minX, pt.x);
            minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x);
            maxY = std::max(maxY, pt.y);
        }
    }
    if (minX >= maxX || minY >= maxY) {
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
    outMask.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH), 0u);
    RasterizeScanlinesStreaming(surfW, surfH, subpaths, evenOdd,
        [&](int py, int rowX0, int rowW, const float* rowCov) {
            if (py < outY0 || py >= outY0 + outH) return;
            const int rxStart = std::max(0, rowX0 - outX0);
            const int rxEnd = std::min(outW, (rowX0 - outX0) + rowW);
            if (rxStart >= rxEnd) return;
            std::uint8_t* dst = outMask.data() + static_cast<std::size_t>(py - outY0) * static_cast<std::size_t>(outW);
            const int srcBase = rxStart - (rowX0 - outX0);
            for (int dstX = rxStart; dstX < rxEnd; ++dstX) {
                dst[dstX] = static_cast<std::uint8_t>(
                    std::clamp(rowCov[srcBase + (dstX - rxStart)], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        });
}

void ResolvePrebuiltPaint(
    const PrebuiltPaintDef& def,
    const RivOverrideTable& ov,
    const RivMat2D& mat,
    RivPaint& outPaint) {
    outPaint.isStroke = def.isStroke;
    outPaint.evenOdd = def.evenOdd;
    outPaint.strokeThickness = (def.isStroke && ov.IsDirty(def.paintObjIdx))
        ? ov.GetF(def.paintObjIdx, 47u, def.strokeThickness)
        : def.strokeThickness;
    outPaint.strokeCap = def.strokeCap;
    outPaint.strokeJoin = def.strokeJoin;
    outPaint.transformAffectsStroke = def.transformAffectsStroke;
    outPaint.style = def.style;
    if (def.style == RivPaintStyle::Solid) {
        outPaint.solidArgb = ov.IsDirty(def.colorObjIdx)
            ? ov.GetU(def.colorObjIdx, 37u, def.solidArgb)
            : def.solidArgb;
    } else if (def.style == RivPaintStyle::Linear || def.style == RivPaintStyle::Radial) {
        float gx0 = def.gx0, gy0 = def.gy0, gx1 = def.gx1, gy1 = def.gy1;
        outPaint.gradOpacity = def.gradOpacity;
        if (ov.IsDirty(def.colorObjIdx)) {
            gx0 = ov.GetF(def.colorObjIdx, 42u, gx0);
            gy0 = ov.GetF(def.colorObjIdx, 33u, gy0);
            gx1 = ov.GetF(def.colorObjIdx, 34u, gx1);
            gy1 = ov.GetF(def.colorObjIdx, 35u, gy1);
            outPaint.gradOpacity = std::clamp(ov.GetF(def.colorObjIdx, 46u, outPaint.gradOpacity), 0.0f, 1.0f);
        }
        mat.Transform(gx0, gy0, outPaint.gradStart.x, outPaint.gradStart.y);
        mat.Transform(gx1, gy1, outPaint.gradEnd.x, outPaint.gradEnd.y);
        outPaint.stops.clear();
        bool needSort = false;
        for (const auto& st : def.stops) {
            float pos = st.pos;
            std::uint32_t argb = st.argb;
            if (ov.IsDirty(st.stopObjIdx)) {
                pos = ov.GetF(st.stopObjIdx, 39u, pos);
                argb = ov.GetU(st.stopObjIdx, 38u, argb);
                needSort = true;
            }
            outPaint.stops.push_back({pos, argb});
        }
        if (needSort) {
            std::sort(outPaint.stops.begin(), outPaint.stops.end(),
                      [](const RivGradStop& a, const RivGradStop& b) { return a.pos < b.pos; });
        }
    }
}

void RenderArtboardEvaluated(
    const PrebuiltRiveDocument& doc,
    const EvaluatedArtboardTree& node,
    const RivMat2D& rootTransform,
    int surfW, int surfH,
    std::vector<std::uint8_t>& surfPremulU8,
    int depth) {
    if (node.abIndex >= doc.artboards.size() || depth > 5) return;
    const PrebuiltArtboard& ab = doc.artboards[node.abIndex];
    const auto& tks = ab.typeKeys;
    const auto& childrenOf = ab.childrenOf;
    const auto& ov = node.overrides;
    const auto& worldMats = node.worldMats;
    const auto& worldOpacity = node.worldOpacity;
    const auto& worldHidden = node.worldHidden;
    const std::size_t compEnd = ab.compEnd;
    if (compEnd == 0u) return;

    auto& rsc = ThreadRasterScratch();
    const float rScale = 0.5f * (FastHypot(rootTransform.xx, rootTransform.xy) + FastHypot(rootTransform.yx, rootTransform.yy));
    const float maxAllowedJump = 120.0f * std::max(0.25f, rScale);

    auto collectShapeSubpaths = [&](std::size_t shapeOrNodeIdx, std::vector<RivSubpath>& outPaths) {
        auto walk = [&](auto& self, std::size_t cur) -> void {
            const std::int32_t pIdx = ab.pathDataIndex[cur];
            if (pIdx >= 0) {
                const PrebuiltPathData& pd = ab.paths[static_cast<std::size_t>(pIdx)];
                const RivMat2D& pm = worldMats[cur];
                auto xformSubpath = [&](RivSubpath& sp) {
                    for (RivVec2& pt : sp.pts) {
                        pm.Transform(pt.x, pt.y, pt.x, pt.y);
                    }
                };
                if (pd.tk == 16u) {
                    const bool closed = ov.IsDirty(cur)
                        ? (ov.GetU(cur, 32u, pd.closed ? 1u : 0u) != 0u)
                        : pd.closed;
                    rsc.skinDeformMats.clear();
                    const bool hasSkin = (pd.skinBones.size() > 1u);
                    if (hasSkin) {
                        rsc.skinDeformMats.push_back(pd.skinBones[0].invBindMulSkinBind);
                        for (std::size_t bi = 1u; bi < pd.skinBones.size(); ++bi) {
                            const PrebuiltSkinBone& sb = pd.skinBones[bi];
                            const RivMat2D& bWorld = (sb.boneId < compEnd) ? worldMats[sb.boneId] : worldMats[0];
                            rsc.skinDeformMats.push_back(RivMat2D::Multiply(bWorld, sb.invBindMulSkinBind));
                        }
                    }
                    auto deformPt = [&](RivVec2 pt, std::uint32_t vals, std::uint32_t idxs) noexcept -> RivVec2 {
                        RivVec2 acc{0.0f, 0.0f};
                        float wSum = 0.0f;
                        for (unsigned b = 0u; b < 4u; ++b) {
                            const float w = static_cast<float>((vals >> (b * 8u)) & 0xFFu) / 255.0f;
                            const std::size_t bi = static_cast<std::size_t>((idxs >> (b * 8u)) & 0xFFu);
                            if (w > 0.0f && bi > 0u && bi < rsc.skinDeformMats.size()) {
                                float tx = 0.0f, ty = 0.0f;
                                rsc.skinDeformMats[bi].Transform(pt.x, pt.y, tx, ty);
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

                    rsc.rawVerts.clear();
                    rsc.skinnedVerts.clear();
                    bool canUseSkin = hasSkin;

                    for (const PrebuiltVertex& pv : pd.verts) {
                        RivVertDesc vd{};
                        const bool vDirty = ov.IsDirty(pv.objIdx);
                        vd.pos = {
                            vDirty ? ov.GetF(pv.objIdx, 24u, pv.x) : pv.x,
                            vDirty ? ov.GetF(pv.objIdx, 25u, pv.y) : pv.y
                        };
                        vd.inPt = vd.pos;
                        vd.outPt = vd.pos;
                        if (pv.vtk == 5u) {
                            vd.straightRadius = vDirty ? ov.GetF(pv.objIdx, 26u, pv.p0) : pv.p0;
                        } else if (pv.vtk == 6u) {
                            vd.isCubic = true;
                            const float inR = vDirty ? ov.GetF(pv.objIdx, 84u, pv.p0) : pv.p0;
                            const float inD = vDirty ? ov.GetF(pv.objIdx, 85u, pv.p1) : pv.p1;
                            const float outR = vDirty ? ov.GetF(pv.objIdx, 86u, pv.p2) : pv.p2;
                            const float outD = vDirty ? ov.GetF(pv.objIdx, 87u, pv.p3) : pv.p3;
                            vd.inPt = {vd.pos.x + std::cos(inR) * inD, vd.pos.y + std::sin(inR) * inD};
                            vd.outPt = {vd.pos.x + std::cos(outR) * outD, vd.pos.y + std::sin(outR) * outD};
                        } else if (pv.vtk == 34u) {
                            vd.isCubic = true;
                            const float r = vDirty ? ov.GetF(pv.objIdx, 79u, pv.p0) : pv.p0;
                            const float inD = vDirty ? ov.GetF(pv.objIdx, 80u, pv.p1) : pv.p1;
                            const float outD = vDirty ? ov.GetF(pv.objIdx, 81u, pv.p2) : pv.p2;
                            const float c = std::cos(r);
                            const float s = std::sin(r);
                            vd.inPt = {vd.pos.x - c * inD, vd.pos.y - s * inD};
                            vd.outPt = {vd.pos.x + c * outD, vd.pos.y + s * outD};
                        } else if (pv.vtk == 35u) {
                            vd.isCubic = true;
                            const float r = vDirty ? ov.GetF(pv.objIdx, 82u, pv.p0) : pv.p0;
                            const float d = vDirty ? ov.GetF(pv.objIdx, 83u, pv.p1) : pv.p1;
                            const float c = std::cos(r);
                            const float s = std::sin(r);
                            vd.inPt = {vd.pos.x - c * d, vd.pos.y - s * d};
                            vd.outPt = {vd.pos.x + c * d, vd.pos.y + s * d};
                        }
                        rsc.rawVerts.push_back(vd);

                        if (canUseSkin) {
                            if (pv.weightObjIdx != 0u) {
                                const bool wDirty = ov.IsDirty(pv.weightObjIdx);
                                const std::uint32_t vVals = wDirty ? ov.GetU(pv.weightObjIdx, 102u, pv.vVals) : pv.vVals;
                                const std::uint32_t vIdxs = wDirty ? ov.GetU(pv.weightObjIdx, 103u, pv.vIdxs) : pv.vIdxs;
                                RivVertDesc sv = vd;
                                sv.pos = deformPt(vd.pos, vVals, vIdxs);
                                if (pv.weightTk == 46u) {
                                    const std::uint32_t inV = wDirty ? ov.GetU(pv.weightObjIdx, 110u, pv.inVals) : pv.inVals;
                                    const std::uint32_t inI = wDirty ? ov.GetU(pv.weightObjIdx, 111u, pv.inIdxs) : pv.inIdxs;
                                    const std::uint32_t outV = wDirty ? ov.GetU(pv.weightObjIdx, 112u, pv.outVals) : pv.outVals;
                                    const std::uint32_t outI = wDirty ? ov.GetU(pv.weightObjIdx, 113u, pv.outIdxs) : pv.outIdxs;
                                    sv.inPt = deformPt(vd.inPt, inV, inI);
                                    sv.outPt = deformPt(vd.outPt, outV, outI);
                                } else {
                                    sv.inPt = deformPt(vd.inPt, vVals, vIdxs);
                                    sv.outPt = deformPt(vd.outPt, vVals, vIdxs);
                                }
                                RivVec2 unsk{};
                                pm.Transform(vd.pos.x, vd.pos.y, unsk.x, unsk.y);
                                if (FastHypot(sv.pos.x - unsk.x, sv.pos.y - unsk.y) > maxAllowedJump) {
                                    canUseSkin = false;
                                }
                                rsc.skinnedVerts.push_back(sv);
                            } else {
                                canUseSkin = false;
                            }
                        }
                    }

                    const bool alreadyWorld = canUseSkin && (rsc.skinnedVerts.size() == rsc.rawVerts.size());
                    const std::vector<RivVertDesc>& verts = alreadyWorld ? rsc.skinnedVerts : rsc.rawVerts;
                    if (verts.size() >= 2u) {
                        rsc.effVerts.clear();
                        rsc.effVerts.reserve(verts.size() * 2u);
                        const std::size_t vn = verts.size();
                        for (std::size_t vi = 0u; vi < vn; ++vi) {
                            const RivVertDesc& v = verts[vi];
                            const bool isEndpoint = !closed && (vi == 0u || vi + 1u == vn);
                            if (!v.isCubic && v.straightRadius > 0.1f && !isEndpoint && vn >= 3u) {
                                const RivVec2 prevP = verts[(vi + vn - 1u) % vn].pos;
                                const RivVec2 nextP = verts[(vi + 1u) % vn].pos;
                                const float dPrev = FastHypot(v.pos.x - prevP.x, v.pos.y - prevP.y);
                                const float dNext = FastHypot(nextP.x - v.pos.x, nextP.y - v.pos.y);
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
                                    rsc.effVerts.push_back(a);
                                    rsc.effVerts.push_back(b);
                                    continue;
                                }
                            }
                            rsc.effVerts.push_back(v);
                        }
                        RivSubpath sp{};
                        sp.closed = closed;
                        sp.pts.push_back(rsc.effVerts[0].pos);
                        const std::size_t segs = closed ? rsc.effVerts.size() : (rsc.effVerts.size() - 1u);
                        for (std::size_t si = 0u; si < segs; ++si) {
                            const RivVertDesc& a = rsc.effVerts[si];
                            const RivVertDesc& b = rsc.effVerts[(si + 1u) % rsc.effVerts.size()];
                            if (!a.isCubic && !b.isCubic) sp.pts.push_back(b.pos);
                            else FlattenCubic(a.pos, a.outPt, b.inPt, b.pos, 0, sp.pts);
                        }
                        if (!alreadyWorld) xformSubpath(sp);
                        outPaths.push_back(std::move(sp));
                    }
                } else if (pd.tk == 7u) {
                    const bool pdDirty = ov.IsDirty(cur);
                    const float w = pdDirty ? ov.GetF(cur, 20u, pd.w) : pd.w;
                    const float h = pdDirty ? ov.GetF(cur, 21u, pd.h) : pd.h;
                    const float ox = pdDirty ? ov.GetF(cur, 123u, pd.ox) : pd.ox;
                    const float oy = pdDirty ? ov.GetF(cur, 124u, pd.oy) : pd.oy;
                    const float rTL = pdDirty ? ov.GetF(cur, 31u, pd.rTL) : pd.rTL;
                    const bool linked = pdDirty ? (ov.GetU(cur, 164u, pd.linked ? 1u : 0u) != 0u) : pd.linked;
                    const float rTR = linked ? rTL : (pdDirty ? ov.GetF(cur, 161u, pd.rTR) : pd.rTR);
                    const float rBL = linked ? rTL : (pdDirty ? ov.GetF(cur, 162u, pd.rBL) : pd.rBL);
                    const float rBR = linked ? rTL : (pdDirty ? ov.GetF(cur, 163u, pd.rBR) : pd.rBR);
                    RivSubpath sp = BuildRoundedRectSubpath(ox, oy, w, h, rTL, rTR, rBR, rBL);
                    xformSubpath(sp);
                    outPaths.push_back(std::move(sp));
                } else if (pd.tk == 4u) {
                    const bool pdDirty = ov.IsDirty(cur);
                    const float w = pdDirty ? ov.GetF(cur, 20u, pd.w) : pd.w;
                    const float h = pdDirty ? ov.GetF(cur, 21u, pd.h) : pd.h;
                    const float ox = pdDirty ? ov.GetF(cur, 123u, pd.ox) : pd.ox;
                    const float oy = pdDirty ? ov.GetF(cur, 124u, pd.oy) : pd.oy;
                    RivSubpath sp = BuildEllipseSubpath(ox, oy, w, h);
                    xformSubpath(sp);
                    outPaths.push_back(std::move(sp));
                } else if (pd.tk == 8u) {
                    const bool pdDirty = ov.IsDirty(cur);
                    const float w = pdDirty ? ov.GetF(cur, 20u, pd.w) : pd.w;
                    const float h = pdDirty ? ov.GetF(cur, 21u, pd.h) : pd.h;
                    const float ox = pdDirty ? ov.GetF(cur, 123u, pd.ox) : pd.ox;
                    const float oy = pdDirty ? ov.GetF(cur, 124u, pd.oy) : pd.oy;
                    const float x0 = -ox * w, y0 = -oy * h;
                    RivSubpath sp{{{x0 + 0.5f * w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}}, true};
                    xformSubpath(sp);
                    outPaths.push_back(std::move(sp));
                }
            }
            for (std::size_t sub : childrenOf[cur]) {
                const std::uint32_t stk = tks[sub];
                if (stk != 3u && stk != 92u && stk != 20u && stk != 24u && stk != 42u) {
                    self(self, sub);
                }
            }
        };
        walk(walk, shapeOrNodeIdx);
    };

    std::vector<std::pair<std::uint64_t, ClipMaskBuf>> clipMaskCache;

    auto blendPathsWithPaint = [&](
        const std::vector<RivSubpath>& targetPaths,
        bool evenOdd,
        const RivPaint& paint,
        float effOp,
        const std::vector<const ClipMaskBuf*>& activeClips) {
        if (targetPaths.empty()) return;
        std::uint8_t solidPremulU8[4] = {0u, 0u, 0u, 0u};
        std::uint32_t solidPacked = 0u;
        std::array<std::uint8_t, 256u * 4u> gradLutU8{};
        if (paint.style == RivPaintStyle::Solid) {
            UnpackArgbPremulU8(paint.solidArgb, effOp, solidPremulU8);
            if (solidPremulU8[3] == 0u) return;
            std::memcpy(&solidPacked, solidPremulU8, 4u);
        } else {
            BuildGradientLut256U8(paint.stops, effOp, gradLutU8);
        }
        const float gdx = paint.gradEnd.x - paint.gradStart.x;
        const float gdy = paint.gradEnd.y - paint.gradStart.y;
        const float glen2 = std::max(1e-5f, gdx * gdx + gdy * gdy);
        const float invGlen2 = 1.0f / glen2;
        const float invGradRad = 1.0f / std::max(1e-3f, std::sqrt(glen2));

        RasterizeScanlinesStreaming(surfW, surfH, targetPaths, evenOdd,
            [&](int py, int mx0, int mw, const float* rowCov) {
                std::uint8_t* dstRow = surfPremulU8.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(surfW) * 4u;
                const bool hasClips = !activeClips.empty();
                if (!hasClips && paint.style == RivPaintStyle::Solid) {
                    const std::uint32_t sr = solidPremulU8[0];
                    const std::uint32_t sg = solidPremulU8[1];
                    const std::uint32_t sb = solidPremulU8[2];
                    const std::uint32_t sa = solidPremulU8[3];
                    std::uint8_t* dpx = dstRow + static_cast<std::size_t>(mx0) * 4u;
                    if (sa == 255u) {
                        for (int rx = 0; rx < mw; ++rx, dpx += 4) {
                            const float cov = rowCov[rx];
                            if (cov <= 0.002f) continue;
                            if (cov >= 0.996f) {
                                std::memcpy(dpx, &solidPacked, 4u);
                            } else {
                                const std::uint32_t cov255 = static_cast<std::uint32_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
                                const std::uint32_t invA = 255u - cov255;
                                dpx[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sr * cov255) + Div255(dpx[0] * invA)));
                                dpx[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sg * cov255) + Div255(dpx[1] * invA)));
                                dpx[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sb * cov255) + Div255(dpx[2] * invA)));
                                dpx[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, cov255 + Div255(dpx[3] * invA)));
                            }
                        }
                    } else {
                        for (int rx = 0; rx < mw; ++rx, dpx += 4) {
                            const float cov = rowCov[rx];
                            if (cov <= 0.002f) continue;
                            const std::uint32_t cov255 = static_cast<std::uint32_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
                            const std::uint32_t c3 = Div255(sa * cov255);
                            if (c3 == 0u) continue;
                            const std::uint32_t invA = 255u - c3;
                            dpx[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sr * cov255) + Div255(dpx[0] * invA)));
                            dpx[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sg * cov255) + Div255(dpx[1] * invA)));
                            dpx[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, Div255(sb * cov255) + Div255(dpx[2] * invA)));
                            dpx[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c3 + Div255(dpx[3] * invA)));
                        }
                    }
                    return;
                }
                if (!hasClips && paint.style == RivPaintStyle::Linear) {
                    const float dtDx = gdx * invGlen2;
                    float t = ((static_cast<float>(mx0) + 0.5f - paint.gradStart.x) * gdx +
                               (static_cast<float>(py) + 0.5f - paint.gradStart.y) * gdy) * invGlen2;
                    std::uint8_t* dpx = dstRow + static_cast<std::size_t>(mx0) * 4u;
                    for (int rx = 0; rx < mw; ++rx, t += dtDx, dpx += 4) {
                        const float cov = rowCov[rx];
                        if (cov <= 0.002f) continue;
                        const int lutIdx = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const std::uint8_t* lpx = gradLutU8.data() + static_cast<std::size_t>(lutIdx) * 4u;
                        if (cov >= 0.996f && lpx[3] == 255u) {
                            std::memcpy(dpx, lpx, 4u);
                            continue;
                        }
                        const std::uint32_t cov255 = static_cast<std::uint32_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const std::uint32_t c3 = Div255(static_cast<std::uint32_t>(lpx[3]) * cov255);
                        if (c3 == 0u && lpx[0] == 0u && lpx[1] == 0u && lpx[2] == 0u) continue;
                        const std::uint32_t c0 = Div255(static_cast<std::uint32_t>(lpx[0]) * cov255);
                        const std::uint32_t c1 = Div255(static_cast<std::uint32_t>(lpx[1]) * cov255);
                        const std::uint32_t c2 = Div255(static_cast<std::uint32_t>(lpx[2]) * cov255);
                        const std::uint32_t invA = 255u - c3;
                        dpx[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c0 + Div255(dpx[0] * invA)));
                        dpx[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c1 + Div255(dpx[1] * invA)));
                        dpx[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c2 + Div255(dpx[2] * invA)));
                        dpx[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c3 + Div255(dpx[3] * invA)));
                    }
                    return;
                }
                if (!hasClips && paint.style == RivPaintStyle::Radial) {
                    const float fy = static_cast<float>(py) + 0.5f - paint.gradStart.y;
                    const float fy2 = fy * fy;
                    float fx = static_cast<float>(mx0) + 0.5f - paint.gradStart.x;
                    std::uint8_t* dpx = dstRow + static_cast<std::size_t>(mx0) * 4u;
                    for (int rx = 0; rx < mw; ++rx, fx += 1.0f, dpx += 4) {
                        const float cov = rowCov[rx];
                        if (cov <= 0.002f) continue;
                        const float d2 = fx * fx + fy2;
                        const int lutIdx = (d2 >= glen2)
                            ? 255
                            : static_cast<int>(std::clamp(std::sqrt(d2) * invGradRad, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const std::uint8_t* lpx = gradLutU8.data() + static_cast<std::size_t>(lutIdx) * 4u;
                        if (cov >= 0.996f && lpx[3] == 255u) {
                            std::memcpy(dpx, lpx, 4u);
                            continue;
                        }
                        const std::uint32_t cov255 = static_cast<std::uint32_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const std::uint32_t c3 = Div255(static_cast<std::uint32_t>(lpx[3]) * cov255);
                        if (c3 == 0u && lpx[0] == 0u && lpx[1] == 0u && lpx[2] == 0u) continue;
                        const std::uint32_t c0 = Div255(static_cast<std::uint32_t>(lpx[0]) * cov255);
                        const std::uint32_t c1 = Div255(static_cast<std::uint32_t>(lpx[1]) * cov255);
                        const std::uint32_t c2 = Div255(static_cast<std::uint32_t>(lpx[2]) * cov255);
                        const std::uint32_t invA = 255u - c3;
                        dpx[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c0 + Div255(dpx[0] * invA)));
                        dpx[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c1 + Div255(dpx[1] * invA)));
                        dpx[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c2 + Div255(dpx[2] * invA)));
                        dpx[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c3 + Div255(dpx[3] * invA)));
                    }
                    return;
                }
                for (int rx = 0; rx < mw; ++rx) {
                    const float cov = rowCov[rx];
                    if (cov <= 0.002f) continue;
                    std::uint32_t cov255 = static_cast<std::uint32_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
                    if (cov255 == 0u) continue;
                    const int px = mx0 + rx;
                    if (hasClips) {
                        for (const ClipMaskBuf* cb : activeClips) {
                            if (px < cb->x0 || px >= cb->x0 + cb->w || py < cb->y0 || py >= cb->y0 + cb->h) {
                                cov255 = 0u;
                                break;
                            }
                            const std::uint32_t m = cb->mask[static_cast<std::size_t>(py - cb->y0) * static_cast<std::size_t>(cb->w) +
                                                             static_cast<std::size_t>(px - cb->x0)];
                            cov255 = Div255(cov255 * m);
                            if (cov255 == 0u) break;
                        }
                        if (cov255 == 0u) continue;
                    }
                    std::uint32_t c0, c1, c2, c3;
                    if (paint.style == RivPaintStyle::Solid) {
                        c0 = Div255(static_cast<std::uint32_t>(solidPremulU8[0]) * cov255);
                        c1 = Div255(static_cast<std::uint32_t>(solidPremulU8[1]) * cov255);
                        c2 = Div255(static_cast<std::uint32_t>(solidPremulU8[2]) * cov255);
                        c3 = Div255(static_cast<std::uint32_t>(solidPremulU8[3]) * cov255);
                    } else {
                        float t = 0.0f;
                        const float fx = static_cast<float>(px) + 0.5f - paint.gradStart.x;
                        const float fy = static_cast<float>(py) + 0.5f - paint.gradStart.y;
                        if (paint.style == RivPaintStyle::Linear) {
                            t = (fx * gdx + fy * gdy) * invGlen2;
                        } else {
                            const float d2 = fx * fx + fy * fy;
                            t = (d2 >= glen2) ? 1.0f : (std::sqrt(d2) * invGradRad);
                        }
                        const int lutIdx = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const std::uint8_t* lpx = gradLutU8.data() + static_cast<std::size_t>(lutIdx) * 4u;
                        c0 = Div255(static_cast<std::uint32_t>(lpx[0]) * cov255);
                        c1 = Div255(static_cast<std::uint32_t>(lpx[1]) * cov255);
                        c2 = Div255(static_cast<std::uint32_t>(lpx[2]) * cov255);
                        c3 = Div255(static_cast<std::uint32_t>(lpx[3]) * cov255);
                    }
                    if (c3 == 0u && c0 == 0u && c1 == 0u && c2 == 0u) continue;
                    std::uint8_t* dpx = dstRow + static_cast<std::size_t>(px) * 4u;
                    const std::uint32_t invA = 255u - c3;
                    dpx[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c0 + Div255(dpx[0] * invA)));
                    dpx[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c1 + Div255(dpx[1] * invA)));
                    dpx[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c2 + Div255(dpx[2] * invA)));
                    dpx[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, c3 + Div255(dpx[3] * invA)));
                }
            });
    };

    // Draw Artboard background Fill(s)
    RivPaint bgPaint{};
    for (const PrebuiltPaintDef& def : ab.bgPaints) {
        const bool enabled = ov.IsDirty(def.paintObjIdx)
            ? (ov.GetU(def.paintObjIdx, 41u, def.enabled ? 1u : 0u) != 0u)
            : def.enabled;
        if (!enabled || def.style == RivPaintStyle::None) continue;
        bgPaint.stops.clear();
        const RivMat2D abGradMat{
            worldMats[0].xx, worldMats[0].xy,
            worldMats[0].yx, worldMats[0].yy,
            worldMats[0].tx - (ab.abOx * ab.natW * worldMats[0].xx + ab.abOy * ab.natH * worldMats[0].yx),
            worldMats[0].ty - (ab.abOx * ab.natW * worldMats[0].xy + ab.abOy * ab.natH * worldMats[0].yy)
        };
        ResolvePrebuiltPaint(def, ov, abGradMat, bgPaint);
        RivSubpath abRect = BuildRoundedRectSubpath(ab.abOx, ab.abOy, ab.natW, ab.natH, 0.0f, 0.0f, 0.0f, 0.0f);
        for (RivVec2& pt : abRect.pts) {
            worldMats[0].Transform(pt.x, pt.y, pt.x, pt.y);
        }
        rsc.shapePaths.clear();
        rsc.shapePaths.push_back(std::move(abRect));
        blendPathsWithPaint(rsc.shapePaths, false, bgPaint, worldOpacity[0] * bgPaint.gradOpacity, {});
    }

    const float rootScale = 0.5f * (FastHypot(rootTransform.xx, rootTransform.xy) + FastHypot(rootTransform.yx, rootTransform.yy));
    std::size_t nestedCursor = node.nestedChildren.size();
    RivPaint shapePaint{};

    for (const std::size_t i : ab.renderOrder) {
        if (worldHidden[i] || worldOpacity[i] <= 0.002f) continue;
        if (tks[i] == 92u) {
            while (nestedCursor > 0u && node.nestedChildren[nestedCursor - 1u].first > i) {
                --nestedCursor;
            }
            if (nestedCursor > 0u && node.nestedChildren[nestedCursor - 1u].first == i) {
                const EvaluatedArtboardTree& childTree = node.nestedChildren[--nestedCursor].second;
                const PrebuiltArtboard& childAb = doc.artboards[childTree.abIndex];
                const RivMat2D nestedWorld = RivMat2D::Multiply(
                    worldMats[i],
                    RivMat2D::FromTRS(-childAb.abOx * childAb.natW, -childAb.abOy * childAb.natH, 0.0f, 1.0f, 1.0f));
                RenderArtboardEvaluated(doc, childTree, nestedWorld, surfW, surfH, surfPremulU8, depth + 1);
            }
            continue;
        }

        const std::int32_t spIdx = ab.shapePaintIndex[i];
        if (spIdx < 0) continue;
        const auto& paints = ab.shapePaints[static_cast<std::size_t>(spIdx)];
        if (paints.empty()) continue;

        rsc.shapePaths.clear();
        collectShapeSubpaths(i, rsc.shapePaths);
        if (rsc.shapePaths.empty()) continue;

        rsc.activeClips.clear();
        if (ab.hasClipInAncestors[i] != 0u) {
            for (std::size_t anc = i; anc != 0u && anc < compEnd; anc = ab.parentIds[anc]) {
                for (const PrebuiltClipRef& cr : ab.nodeClips[anc]) {
                    const bool enabled = ov.IsDirty(cr.clipObjIdx)
                        ? (ov.GetU(cr.clipObjIdx, 94u, cr.enabled ? 1u : 0u) != 0u)
                        : cr.enabled;
                    if (!enabled) continue;
                    const std::uint32_t srcId = ov.IsDirty(cr.clipObjIdx)
                        ? ov.GetU(cr.clipObjIdx, 92u, cr.srcId)
                        : cr.srcId;
                    if (srcId >= compEnd) continue;
                    const bool clipEo = ov.IsDirty(cr.clipObjIdx)
                        ? (ov.GetU(cr.clipObjIdx, 93u, cr.evenOdd ? 1u : 0u) != 0u)
                        : cr.evenOdd;
                    const std::uint64_t clipKey = (static_cast<std::uint64_t>(srcId) << 1u) | (clipEo ? 1u : 0u);
                    ClipMaskBuf* foundMask = nullptr;
                    for (auto& kv : clipMaskCache) {
                        if (kv.first == clipKey) {
                            foundMask = &kv.second;
                            break;
                        }
                    }
                    if (!foundMask) {
                        clipMaskCache.emplace_back(clipKey, ClipMaskBuf{});
                        foundMask = &clipMaskCache.back().second;
                        rsc.clipPaths.clear();
                        collectShapeSubpaths(srcId, rsc.clipPaths);
                        if (!rsc.clipPaths.empty()) {
                            RasterizeCoverageMaskIntoBuffer(
                                surfW, surfH, rsc.clipPaths, clipEo,
                                foundMask->x0, foundMask->y0,
                                foundMask->w, foundMask->h,
                                foundMask->mask);
                        }
                    }
                    if (foundMask->w > 0 && foundMask->h > 0) {
                        rsc.activeClips.push_back(foundMask);
                    }
                }
            }
        }

        const RivMat2D& shapeMat = worldMats[i];
        const float shapeScale = 0.5f * (FastHypot(shapeMat.xx, shapeMat.xy) + FastHypot(shapeMat.yx, shapeMat.yy));

        for (const PrebuiltPaintDef& def : paints) {
            const bool enabled = ov.IsDirty(def.paintObjIdx)
                ? (ov.GetU(def.paintObjIdx, 41u, def.enabled ? 1u : 0u) != 0u)
                : def.enabled;
            if (!enabled || def.style == RivPaintStyle::None) continue;

            shapePaint.stops.clear();
            ResolvePrebuiltPaint(def, ov, shapeMat, shapePaint);

            const std::vector<RivSubpath>* targetPaths = &rsc.shapePaths;
            bool useEvenOdd = shapePaint.evenOdd;
            if (shapePaint.isStroke) {
                bool trimmedOut = false;
                bool hasPartialTrim = false;
                rsc.trimmedPaths.clear();
                for (const PrebuiltPaintTrim& tm : def.trims) {
                    const bool tmDirty = ov.IsDirty(tm.trimObjIdx);
                    const float ts = tmDirty ? ov.GetF(tm.trimObjIdx, 114u, tm.start) : tm.start;
                    const float te = tmDirty ? ov.GetF(tm.trimObjIdx, 115u, tm.end) : tm.end;
                    const float toff = tmDirty ? ov.GetF(tm.trimObjIdx, 116u, tm.offset) : tm.offset;
                    const std::uint32_t tmode = tmDirty ? ov.GetU(tm.trimObjIdx, 117u, tm.mode) : tm.mode;
                    if (std::fabs(te - ts) <= 1e-4f) {
                        trimmedOut = true;
                        break;
                    }
                    if (!(std::min(ts, te) <= 1e-4f && std::max(ts, te) >= 1.0f - 1e-4f)) {
                        rsc.nextTrimmed.clear();
                        TrimRivSubpaths(
                            hasPartialTrim ? rsc.trimmedPaths : rsc.shapePaths,
                            ts, te, toff, tmode, rsc.nextTrimmed);
                        rsc.trimmedPaths.swap(rsc.nextTrimmed);
                        hasPartialTrim = true;
                        if (rsc.trimmedPaths.empty()) {
                            trimmedOut = true;
                            break;
                        }
                    }
                }
                if (trimmedOut) continue;
                const float effScale = shapePaint.transformAffectsStroke ? shapeScale : rootScale;
                const float halfW = 0.5f * shapePaint.strokeThickness * effScale;
                rsc.strokeExpanded.clear();
                ExpandStrokeToSubpaths(
                    hasPartialTrim ? rsc.trimmedPaths : rsc.shapePaths,
                    halfW, shapePaint.strokeCap, rsc, rsc.strokeExpanded);
                targetPaths = &rsc.strokeExpanded;
                useEvenOdd = false;
            }
            blendPathsWithPaint(
                *targetPaths, useEvenOdd, shapePaint,
                worldOpacity[i] * shapePaint.gradOpacity, rsc.activeClips);
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

bool RenderRiveParamsToImageIfChanged(
    const RiveRenderRequest& request,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    const std::uint64_t* prevPoseHash,
    std::uint64_t& outPoseHash,
    bool& outChanged,
    DecodedImage& outImage) {
    outPoseHash = 0u;
    outChanged = true;

    std::string_view filePart = request.assetPath;
    if (filePart.size() >= 7u && filePart.substr(0u, 7u) == "rive://") {
        filePart.remove_prefix(7u);
    }
    if (const std::size_t q = filePart.find('?'); q != std::string_view::npos) {
        filePart = filePart.substr(0u, q);
    }
    if (filePart.empty()) return false;

    RivInputPairs inputs;
    if (request.inputs != nullptr && request.inputCount > 0u) {
        inputs.reserve(request.inputCount);
        for (std::size_t i = 0u; i < request.inputCount; ++i) {
            inputs.emplace_back(request.inputs[i].first, request.inputs[i].second);
        }
    }
    const float tLoopSec = std::max(0.0f, request.tLoopSec);
    const float tStateSec = std::max(0.0f, request.tStateSec);
    const float reqW = request.reqW;
    const float reqH = request.reqH;

    const std::string resolvedPath = atlas.ResolveUriToPath(filePart, localBaseDir);
    if (resolvedPath.empty()) return false;

    static std::mutex sDocCacheMutex;
    static std::unordered_map<std::string, std::shared_ptr<const PrebuiltRiveDocument>> sDocCache;
    std::shared_ptr<const PrebuiltRiveDocument> docPtr;
    {
        std::lock_guard<std::mutex> lock(sDocCacheMutex);
        if (const auto it = sDocCache.find(resolvedPath); it != sDocCache.end()) {
            docPtr = it->second;
        }
    }
    if (!docPtr) {
        std::ifstream ifs(resolvedPath, std::ios::binary | std::ios::ate);
        if (!ifs) return false;
        const auto fsize = ifs.tellg();
        if (fsize <= 6 || fsize > 64 * 1024 * 1024) return false;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fsize));
        ifs.seekg(0, std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(bytes.data()), fsize)) return false;

        std::vector<RivObject> parsedObjs;
        if (!ParseRiveFileBytes(bytes, parsedObjs)) return false;
        auto prebuilt = std::make_shared<PrebuiltRiveDocument>(BuildPrebuiltDocument(parsedObjs));
        if (prebuilt->artboards.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(sDocCacheMutex);
            if (sDocCache.size() >= 32u) {
                sDocCache.clear();
            }
            sDocCache[resolvedPath] = prebuilt;
        }
        docPtr = std::move(prebuilt);
    }

    const PrebuiltRiveDocument& doc = *docPtr;
    if (doc.artboards.empty()) return false;

    const PrebuiltArtboard& mainAb = doc.artboards[0];
    const float natW = mainAb.natW;
    const float natH = mainAb.natH;
    float scale = 1.0f;
    const float maxDim = std::max(natW, natH);
    if (maxDim > 640.0f) {
        scale = 640.0f / maxDim;
    }
    if (reqW >= 16.0f && reqH >= 16.0f) {
        const float maxW = std::max(64.0f, reqW);
        const float maxH = std::max(64.0f, reqH);
        const float fitScale = std::min(maxW / natW, maxH / natH);
        scale = std::min(scale, fitScale);
    }
    const int surfW = std::clamp(static_cast<int>(std::lround(natW * scale)), 1, 640);
    const int surfH = std::clamp(static_cast<int>(std::lround(natH * scale)), 1, 640);

    const RivMat2D rootMat = RivMat2D::FromTRS(
        0.0f, 0.0f, 0.0f,
        static_cast<float>(surfW) / natW,
        static_cast<float>(surfH) / natH);

    std::uint64_t poseHash = 14695981039346656037ULL;
    HashMix64(poseHash, static_cast<std::uint64_t>(surfW));
    HashMix64(poseHash, static_cast<std::uint64_t>(surfH));
    for (const char ch : resolvedPath) {
        poseHash ^= static_cast<unsigned char>(ch);
        poseHash *= 1099511628211ULL;
    }

    EvaluatedArtboardTree evalRoot{};
    BuildEvaluatedTreeRecursive(
        doc, 0u, rootMat, 1.0f,
        tLoopSec, tStateSec, inputs, 0,
        evalRoot, poseHash);

    outPoseHash = poseHash;
    if (prevPoseHash != nullptr && *prevPoseHash == poseHash) {
        outChanged = false;
        return true;
    }

    auto& sc = ThreadRasterScratch();
    const std::size_t byteCount = static_cast<std::size_t>(surfW) * static_cast<std::size_t>(surfH) * 4u;
    sc.surfPremulU8.assign(byteCount, 0u);
    RenderArtboardEvaluated(doc, evalRoot, rootMat, surfW, surfH, sc.surfPremulU8, 0);

    static const std::array<std::uint16_t, 256u> kUnpremulScale = []() {
        std::array<std::uint16_t, 256u> lut{};
        lut[0] = 0u;
        for (std::uint32_t a = 1u; a < 256u; ++a) {
            lut[a] = static_cast<std::uint16_t>((255u * 256u + a / 2u) / a);
        }
        return lut;
    }();

    outImage.width = static_cast<std::uint32_t>(surfW);
    outImage.height = static_cast<std::uint32_t>(surfH);
    outImage.format = ImageFormat::RawRgba8;
    outImage.rgbaPixels.resize(byteCount);
    const std::uint8_t* __restrict src = sc.surfPremulU8.data();
    std::uint8_t* __restrict dst = outImage.rgbaPixels.data();
    const std::size_t totalPixels = static_cast<std::size_t>(surfW) * static_cast<std::size_t>(surfH);
    for (std::size_t p = 0u; p < totalPixels; ++p) {
        std::uint32_t px = 0u;
        std::memcpy(&px, src + p * 4u, 4u);
        const std::uint8_t a = static_cast<std::uint8_t>(px >> 24u);
        std::uint8_t* d = dst + p * 4u;
        if (a == 0u) {
            std::uint32_t zero = 0u;
            std::memcpy(d, &zero, 4u);
        } else if (a == 255u) {
            std::memcpy(d, &px, 4u);
        } else {
            const std::uint32_t scaleQ8 = kUnpremulScale[a];
            const std::uint32_t r = std::min<std::uint32_t>(255u, ((px & 0xFFu) * scaleQ8 + 128u) >> 8u);
            const std::uint32_t g = std::min<std::uint32_t>(255u, (((px >> 8u) & 0xFFu) * scaleQ8 + 128u) >> 8u);
            const std::uint32_t b = std::min<std::uint32_t>(255u, (((px >> 16u) & 0xFFu) * scaleQ8 + 128u) >> 8u);
            const std::uint32_t outPx = r | (g << 8u) | (b << 16u) | (static_cast<std::uint32_t>(a) << 24u);
            std::memcpy(d, &outPx, 4u);
        }
    }
    if (sc.surfPremulU8.capacity() > 1536u * 1024u) {
        std::vector<std::uint8_t>().swap(sc.surfPremulU8);
    }
    outChanged = true;
    return true;
}

bool RenderRiveUriToImageIfChanged(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    const std::uint64_t* prevPoseHash,
    std::uint64_t& outPoseHash,
    bool& outChanged,
    DecodedImage& outImage) {
    outPoseHash = 0u;
    outChanged = true;

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

    std::vector<std::pair<std::string, float>> parsedInputs;
    float tLoopSec = 0.0f;
    float tStateSec = 0.0f;
    bool hasTs = false;
    float reqW = 0.0f;
    float reqH = 0.0f;
    std::uint32_t instanceId = 0u;
    while (!queryPart.empty()) {
        const std::size_t amp = queryPart.find('&');
        const std::string_view kv = (amp != std::string_view::npos) ? queryPart.substr(0u, amp) : queryPart;
        if (const std::size_t eq = kv.find('='); eq != std::string_view::npos) {
            const std::string_view kView = kv.substr(0u, eq);
            const std::string_view vView = kv.substr(eq + 1u);
            float parsedVal = 0.0f;
            bool okNum = false;
            if (vView == "true" || vView == "True") {
                parsedVal = 1.0f;
                okNum = true;
            } else if (vView == "false" || vView == "False") {
                parsedVal = 0.0f;
                okNum = true;
            } else if (!vView.empty() && vView.size() < 63u) {
                char numBuf[64];
                std::memcpy(numBuf, vView.data(), vView.size());
                numBuf[vView.size()] = '\0';
                char* endp = nullptr;
                parsedVal = std::strtof(numBuf, &endp);
                okNum = (endp != numBuf);
            }
            if (okNum) {
                if (kView == "__t") {
                    tLoopSec = std::max(0.0f, parsedVal);
                } else if (kView == "__ts") {
                    tStateSec = std::max(0.0f, parsedVal);
                    hasTs = true;
                } else if (kView == "__w") {
                    reqW = parsedVal;
                } else if (kView == "__h") {
                    reqH = parsedVal;
                } else if (kView == "__inst") {
                    instanceId = static_cast<std::uint32_t>(std::max(0, static_cast<int>(parsedVal)));
                } else {
                    bool replaced = false;
                    for (auto& kvIn : parsedInputs) {
                        if (kvIn.first == kView) {
                            kvIn.second = parsedVal;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        parsedInputs.emplace_back(std::string(kView), parsedVal);
                    }
                }
            }
        }
        if (amp == std::string_view::npos) break;
        queryPart.remove_prefix(amp + 1u);
    }
    if (!hasTs) {
        tStateSec = tLoopSec;
    }

    RiveRenderRequest req{};
    req.assetPath = filePart;
    req.instanceId = instanceId;
    req.tLoopSec = tLoopSec;
    req.tStateSec = tStateSec;
    req.reqW = reqW;
    req.reqH = reqH;
    req.inputs = parsedInputs.data();
    req.inputCount = parsedInputs.size();
    return RenderRiveParamsToImageIfChanged(
        req, atlas, localBaseDir, prevPoseHash, outPoseHash, outChanged, outImage);
}

bool RenderRiveUriToImage(
    std::string_view uri,
    const ImageAtlas& atlas,
    std::string_view localBaseDir,
    DecodedImage& outImage) {
    std::uint64_t ignoredHash = 0u;
    bool ignoredChanged = true;
    return RenderRiveUriToImageIfChanged(
        uri, atlas, localBaseDir, nullptr, ignoredHash, ignoredChanged, outImage);
}

} // namespace aero
