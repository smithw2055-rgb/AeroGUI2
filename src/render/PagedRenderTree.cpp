#include "aero/render/PagedRenderTree.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>

#include "aero/core/StringUtils.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

struct MtsdfMappedQuad {
    Rect quadRect{};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

[[nodiscard]] std::size_t CountLayoutVisibleGlyphs(const TextLayoutResult& layout) noexcept;
[[nodiscard]] MtsdfMappedQuad ComputeMtsdfMappedPlacement(
    const Rect& fitted,
    const AxbMtsdfPathEntry& entry,
    float strokeThickness) noexcept;
void ApplyTextAlignmentToLayout(
    TextLayoutResult& layout,
    TextAlignment align,
    float alignBoxWidth) noexcept;
[[nodiscard]] CornerRadius SanitizeCornerRadius(const CornerRadius& r) noexcept;
[[nodiscard]] Thickness SanitizeThickness(const Thickness& t) noexcept;
[[nodiscard]] bool HasPositiveThickness(const Thickness& t) noexcept;
void ConfigureRoundedRectDescriptor(
    RenderNodeDescriptor& desc,
    const CornerRadius& rawRadii,
    const Thickness& rawBorder,
    bool hollowBorder) noexcept;

namespace {

std::atomic<std::uint32_t> gNextRenderTreeId{1u};

} // namespace

namespace {

[[nodiscard]] bool TextContainsCodePointInRange(
    std::string_view utf8Text,
    std::uint32_t rangeStart,
    std::uint32_t rangeEnd) noexcept {
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        if (cp >= rangeStart && cp <= rangeEnd) {
            return true;
        }
    }
    return false;
}

} // namespace

std::size_t CountLayoutVisibleGlyphs(const TextLayoutResult& layout) noexcept {
    std::size_t count = 0u;
    for (const auto& g : layout.glyphs) {
        if (!g.isWhitespace) {
            ++count;
        }
    }
    return count;
}

namespace {

[[nodiscard]] std::uint8_t ClampToByte(float v) noexcept {
    if (v <= 0.0f) return 0u;
    if (v >= 1.0f) return 255u;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

[[nodiscard]] std::uint32_t ScalePackedRgbaAlpha(std::uint32_t baseRgba, float opacity) noexcept {
    if (opacity >= 0.999f) {
        return baseRgba;
    }
    const std::uint32_t a = (baseRgba >> 24u) & 0xFFu;
    const float clamped = (opacity <= 0.0f) ? 0.0f : (opacity >= 1.0f ? 1.0f : opacity);
    const auto scaledA = static_cast<std::uint32_t>(static_cast<float>(a) * clamped + 0.5f);
    return (baseRgba & 0x00FFFFFFu) | ((scaledA & 0xFFu) << 24u);
}

[[nodiscard]] bool SameClipRect(const CommandPageHeader& hdr, const Rect& clip, bool clipActive) noexcept {
    const bool hdrClipped = (hdr.flags & CommandPageFlag_ScissorClipActive) != 0u;
    if (hdrClipped != clipActive) {
        return false;
    }
    if (!clipActive) {
        return true;
    }
    return std::fabs(hdr.clipX - clip.x) < 0.01f &&
           std::fabs(hdr.clipY - clip.y) < 0.01f &&
           std::fabs(hdr.clipW - clip.width) < 0.01f &&
           std::fabs(hdr.clipH - clip.height) < 0.01f;
}

[[nodiscard]] Rect IntersectRect(const Rect& a, const Rect& b) noexcept {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

} // namespace

// Map the geometry sub-region [cellBoxX0..cellBoxX1] x [cellBoxY0..cellBoxY1] inside the uniform-scaled
// MTSDF cell onto screen `fitted`, preserving exact non-square aspect ratio and outer AA/stroke padding.
MtsdfMappedQuad ComputeMtsdfMappedPlacement(
    const Rect& fitted,
    const AxbMtsdfPathEntry& entry,
    float strokeThickness) noexcept {
    if (fitted.width <= 0.0f || fitted.height <= 0.0f ||
        entry.cellWidth == 0 || entry.cellHeight == 0) {
        return MtsdfMappedQuad{fitted, entry.u0, entry.v0, entry.u1, entry.v1};
    }

    const float cellW = static_cast<float>(entry.cellWidth);
    const float cellH = static_cast<float>(entry.cellHeight);
    const float safeRange = std::max(entry.pxRange, 1.0f);
    const float padTexels = std::ceil(safeRange) + 1.0f;
    const float usableW = std::max(2.0f, cellW - 2.0f * padTexels);
    const float usableH = std::max(2.0f, cellH - 2.0f * padTexels);
    const float uniformScale = std::min(
        usableW / std::max(1e-3f, entry.boundsW),
        usableH / std::max(1e-3f, entry.boundsH));

    const float scaledW = std::max(1e-3f, entry.boundsW * uniformScale);
    const float scaledH = std::max(1e-3f, entry.boundsH * uniformScale);
    const float cellBoxX0 = (cellW - scaledW) * 0.5f;
    const float cellBoxY0 = (cellH - scaledH) * 0.5f;

    const float screenPadPx = 1.5f + std::max(0.0f, strokeThickness) * 0.5f;
    const float marginTexX = std::max(1.0f, screenPadPx * (scaledW / std::max(1.0f, fitted.width)));
    const float marginTexY = std::max(1.0f, screenPadPx * (scaledH / std::max(1.0f, fitted.height)));

    const float tx0 = std::clamp(cellBoxX0 - marginTexX, 0.5f, cellBoxX0);
    const float tx1 = std::clamp(cellBoxX0 + scaledW + marginTexX, cellBoxX0 + scaledW, cellW - 0.5f);
    const float ty0 = std::clamp(cellBoxY0 - marginTexY, 0.5f, cellBoxY0);
    const float ty1 = std::clamp(cellBoxY0 + scaledH + marginTexY, cellBoxY0 + scaledH, cellH - 0.5f);

    const float pxPerTexX = fitted.width / scaledW;
    const float pxPerTexY = fitted.height / scaledH;
    const Rect quadRect{
        fitted.x - (cellBoxX0 - tx0) * pxPerTexX,
        fitted.y - (cellBoxY0 - ty0) * pxPerTexY,
        (tx1 - tx0) * pxPerTexX,
        (ty1 - ty0) * pxPerTexY};

    const float invCellSpanX = 1.0f / std::max(1.0f, cellW - 1.0f);
    const float invCellSpanY = 1.0f / std::max(1.0f, cellH - 1.0f);
    const float u0 = entry.u0 + (tx0 - 0.5f) * invCellSpanX * (entry.u1 - entry.u0);
    const float u1 = entry.u0 + (tx1 - 0.5f) * invCellSpanX * (entry.u1 - entry.u0);
    const float v0 = entry.v0 + (ty0 - 0.5f) * invCellSpanY * (entry.v1 - entry.v0);
    const float v1 = entry.v0 + (ty1 - 0.5f) * invCellSpanY * (entry.v1 - entry.v0);

    return MtsdfMappedQuad{quadRect, u0, v0, u1, v1};
}

void ApplyTextAlignmentToLayout(
    TextLayoutResult& layout,
    TextAlignment align,
    float alignBoxWidth) noexcept {
    if (align == TextAlignment::Left || alignBoxWidth <= 0.0f || layout.lines.empty()) {
        return;
    }
    for (PositionedGlyph& pg : layout.glyphs) {
        if (pg.lineIndex >= layout.lines.size()) continue;
        const float lw = layout.lines[pg.lineIndex].width;
        float dx = 0.0f;
        if (align == TextAlignment::Center) {
            dx = (alignBoxWidth - lw) * 0.5f;
        } else if (align == TextAlignment::Right) {
            dx = (alignBoxWidth - lw);
        }
        pg.penX += dx;
        pg.x0 += dx;
        pg.x1 += dx;
    }
}

namespace {

[[nodiscard]] std::vector<Point> TrimSubpathPolyline(
    const std::vector<Point>& pts,
    float trimStart01,
    float trimEnd01) {
    std::vector<Point> out;
    if (pts.size() < 2u || trimEnd01 <= trimStart01) {
        return out;
    }
    std::vector<float> cum(pts.size(), 0.0f);
    for (std::size_t i = 1u; i < pts.size(); ++i) {
        const float seg = std::hypot(pts[i].x - pts[i - 1u].x, pts[i].y - pts[i - 1u].y);
        cum[i] = cum[i - 1u] + seg;
    }
    const float total = cum.back();
    if (total <= 1e-5f) {
        return out;
    }
    const float d0 = std::clamp(trimStart01, 0.0f, 1.0f) * total;
    const float d1 = std::clamp(trimEnd01, 0.0f, 1.0f) * total;
    if (d1 <= d0 + 1e-5f) {
        return out;
    }
    for (std::size_t i = 0u; i + 1u < pts.size(); ++i) {
        const float a = cum[i];
        const float b = cum[i + 1u];
        const float segLen = b - a;
        if (segLen <= 1e-6f || b < d0 || a > d1) {
            continue;
        }
        const float tA = (a < d0) ? std::clamp((d0 - a) / segLen, 0.0f, 1.0f) : 0.0f;
        const float tB = (b > d1) ? std::clamp((d1 - a) / segLen, 0.0f, 1.0f) : 1.0f;
        if (tB <= tA + 1e-6f) {
            continue;
        }
        const Point pA{
            pts[i].x + (pts[i + 1u].x - pts[i].x) * tA,
            pts[i].y + (pts[i + 1u].y - pts[i].y) * tA};
        const Point pB{
            pts[i].x + (pts[i + 1u].x - pts[i].x) * tB,
            pts[i].y + (pts[i + 1u].y - pts[i].y) * tB};
        if (out.empty()) {
            out.push_back(pA);
        } else if (std::hypot(pA.x - out.back().x, pA.y - out.back().y) > 1e-4f) {
            out.push_back(pA);
        }
        out.push_back(pB);
    }
    if (out.size() < 2u) {
        out.clear();
    }
    return out;
}

[[nodiscard]] bool HasPositiveCornerRadius(const CornerRadius& r) noexcept {
    return r.topLeft > 0.0f || r.topRight > 0.0f || r.bottomRight > 0.0f || r.bottomLeft > 0.0f;
}

} // namespace

CornerRadius SanitizeCornerRadius(const CornerRadius& r) noexcept {
    return CornerRadius{
        std::max(0.0f, r.topLeft),
        std::max(0.0f, r.topRight),
        std::max(0.0f, r.bottomRight),
        std::max(0.0f, r.bottomLeft)};
}

Thickness SanitizeThickness(const Thickness& t) noexcept {
    return Thickness{
        std::max(0.0f, t.left),
        std::max(0.0f, t.top),
        std::max(0.0f, t.right),
        std::max(0.0f, t.bottom)};
}

bool HasPositiveThickness(const Thickness& t) noexcept {
    return t.left > 0.0f || t.top > 0.0f || t.right > 0.0f || t.bottom > 0.0f;
}

void ConfigureRoundedRectDescriptor(
    RenderNodeDescriptor& desc,
    const CornerRadius& rawRadii,
    const Thickness& rawBorder,
    bool hollowBorder) noexcept {
    const CornerRadius cr = SanitizeCornerRadius(rawRadii);
    const Thickness bt = SanitizeThickness(rawBorder);
    const bool anyRadius = HasPositiveCornerRadius(cr);
    const bool anyBorder = HasPositiveThickness(bt);

    desc.cornerRadius = cr.topLeft;
    desc.cornerRadii = cr;
    desc.borderThickness = bt.left;
    desc.borderThicknesses = bt;
    desc.isHollowBorder = hollowBorder;

    if (!hollowBorder && !anyRadius && !anyBorder) {
        desc.primType = AeroPrimType::SolidQuad;
        desc.encodedPrimExtra = 0u;
        desc.encodedSdfParams = AeroVertexFlag_None;
    } else {
        desc.primType = AeroPrimType::SdfRoundedRect;
        PackSdfRoundedRectVertexParams(
            cr,
            bt,
            hollowBorder,
            desc.encodedPrimExtra,
            desc.encodedSdfParams);
    }
}

std::uint32_t PackColorRgba8(const Color& color, float opacityMultiplier) noexcept {
    const std::uint32_t r = ClampToByte(color.r);
    const std::uint32_t g = ClampToByte(color.g);
    const std::uint32_t b = ClampToByte(color.b);
    const std::uint32_t a = ClampToByte(color.a * opacityMultiplier);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

std::uint16_t PackSdfRoundedRectParams(float cornerRadius, float borderThickness) noexcept {
    const float clampedR = (cornerRadius < 0.0f) ? 0.0f : (cornerRadius > 127.5f ? 127.5f : cornerRadius);
    const float clampedB = (borderThickness < 0.0f) ? 0.0f : (borderThickness > 127.5f ? 127.5f : borderThickness);
    const auto quantR = static_cast<std::uint16_t>(clampedR * 2.0f + 0.5f) & 0xFFu;
    const auto quantB = static_cast<std::uint16_t>(clampedB * 2.0f + 0.5f) & 0xFFu;
    return static_cast<std::uint16_t>(quantR | (quantB << 8u));
}

void UnpackSdfRoundedRectParams(
    std::uint16_t packed,
    float& outCornerRadius,
    float& outBorderThickness) noexcept {
    outCornerRadius = static_cast<float>(packed & 0xFFu) * 0.5f;
    outBorderThickness = static_cast<float>((packed >> 8u) & 0xFFu) * 0.5f;
}

std::uint16_t PackMtsdfParams(
    float pxRange,
    std::uint16_t atlasSize,
    float strokeThickness,
    bool strokeOnly) noexcept {
    const float safeAtlas = static_cast<float>(std::max<std::uint16_t>(atlasSize, 16u));
    const float uvRange = std::max(pxRange, 1.0f) / safeAtlas;
    const auto quantUvRange = static_cast<std::uint16_t>(
        std::clamp(uvRange * 4096.0f + 0.5f, 1.0f, 255.0f)) & 0xFFu;
    const auto quantStroke = static_cast<std::uint16_t>(
        std::clamp(strokeThickness * 2.0f + 0.5f, 0.0f, 127.0f)) & 0x7Fu;
    const std::uint16_t strokeOnlyBit = strokeOnly ? 0x8000u : 0x0000u;
    return static_cast<std::uint16_t>(quantUvRange | (quantStroke << 8u) | strokeOnlyBit);
}

void UnpackMtsdfParams(
    std::uint16_t packed,
    float& outUvRange,
    float& outStrokeThickness) noexcept {
    outUvRange = static_cast<float>(std::max<std::uint16_t>(1u, packed & 0xFFu)) / 4096.0f;
    outStrokeThickness = static_cast<float>((packed >> 8u) & 0x7Fu) * 0.5f;
}

float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float cornerRadius,
    float aaFeatherWidth) noexcept {
    const float maxR = std::min(halfWidth, halfHeight);
    const float r = std::clamp(cornerRadius, 0.0f, maxR);
    const float qx = std::fabs(sampleLocalX) - (halfWidth - r);
    const float qy = std::fabs(sampleLocalY) - (halfHeight - r);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float outsideDist = std::sqrt(ox * ox + oy * oy);
    const float insideDist = std::min(std::max(qx, qy), 0.0f);
    const float signedDist = outsideDist + insideDist - r;

    const float feather = std::max(aaFeatherWidth, 0.001f);
    const float t = std::clamp(0.5f - (signedDist / feather), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void PackSdfRoundedRectVertexParams(
    const CornerRadius& cornerRadii,
    const Thickness& borderThickness,
    bool hollowBorder,
    std::uint16_t& outPrimExtra,
    std::uint16_t& outFlags) noexcept {
    const CornerRadius cr = SanitizeCornerRadius(cornerRadii);
    const Thickness bt = SanitizeThickness(borderThickness);

    auto writeModePayload = [&](std::uint16_t sdfMode, std::uint32_t payload27) noexcept {
        outFlags = static_cast<std::uint16_t>(payload27 & 0xFFFFu);
        const auto hi11 = static_cast<std::uint16_t>((payload27 >> 16u) & 0x07FFu);
        outPrimExtra = static_cast<std::uint16_t>(((sdfMode & 0x3u) << 3u) | (hi11 << 5u));
    };

    if (!hollowBorder && IsUniformCornerRadius(cr)) {
        outPrimExtra = 0u;
        outFlags = PackSdfRoundedRectParams(cr.topLeft, bt.left);
        return;
    }

    if (hollowBorder && IsUniformCornerRadius(cr)) {
        if (IsUniformThickness(bt)) {
            const auto packed = static_cast<std::uint32_t>(PackSdfRoundedRectParams(cr.topLeft, bt.left));
            writeModePayload(AeroSdfPrimMode_UniformHollowBorder, packed);
        } else {
            const auto qR = static_cast<std::uint32_t>(std::clamp(cr.topLeft + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
            const auto qL = static_cast<std::uint32_t>(std::clamp(bt.left + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
            const auto qT = static_cast<std::uint32_t>(std::clamp(bt.top + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
            const auto qRi = static_cast<std::uint32_t>(std::clamp(bt.right + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
            const auto qB = static_cast<std::uint32_t>(std::clamp(bt.bottom + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
            const std::uint32_t payload27 =
                qR | (qL << 6u) | (qT << 11u) | (qRi << 16u) | (qB << 21u) | (1u << 26u);
            writeModePayload(AeroSdfPrimMode_UniformHollowBorder, payload27);
        }
        return;
    }

    const float maxR = std::max(
        std::max(cr.topLeft, cr.topRight),
        std::max(cr.bottomRight, cr.bottomLeft));

    if (!hollowBorder) {
        std::uint32_t scaleSel = 0u;
        float invStep = 2.0f;
        if (maxR > 126.0f) {
            scaleSel = 3u;
            invStep = 0.25f;
        } else if (maxR > 63.0f) {
            scaleSel = 2u;
            invStep = 0.5f;
        } else if (maxR > 31.5f) {
            scaleSel = 1u;
            invStep = 1.0f;
        }
        const auto qTL = static_cast<std::uint32_t>(std::clamp(cr.topLeft * invStep + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
        const auto qTR = static_cast<std::uint32_t>(std::clamp(cr.topRight * invStep + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
        const auto qBR = static_cast<std::uint32_t>(std::clamp(cr.bottomRight * invStep + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
        const auto qBL = static_cast<std::uint32_t>(std::clamp(cr.bottomLeft * invStep + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
        const std::uint32_t payload27 =
            qTL | (qTR << 6u) | (qBR << 12u) | (qBL << 18u) | (scaleSel << 24u);
        writeModePayload(AeroSdfPrimMode_AsymmetricFill, payload27);
        return;
    }

    const std::uint32_t scaleBit = (maxR > 31.0f) ? 1u : 0u;
    const float invStep = (scaleBit != 0u) ? 0.5f : 1.0f;
    const auto qTL = static_cast<std::uint32_t>(std::clamp(cr.topLeft * invStep + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
    const auto qTR = static_cast<std::uint32_t>(std::clamp(cr.topRight * invStep + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
    const auto qBR = static_cast<std::uint32_t>(std::clamp(cr.bottomRight * invStep + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
    const auto qBL = static_cast<std::uint32_t>(std::clamp(cr.bottomLeft * invStep + 0.5f, 0.0f, 31.0f)) & 0x1Fu;
    const float avgBt = 0.25f * (bt.left + bt.top + bt.right + bt.bottom);
    const auto qBt = static_cast<std::uint32_t>(std::clamp(avgBt * 2.0f + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
    const std::uint32_t payload27 =
        qTL | (qTR << 5u) | (qBR << 10u) | (qBL << 15u) | (qBt << 20u) | (scaleBit << 26u);
    writeModePayload(AeroSdfPrimMode_AsymmetricHollowBorder, payload27);
}

void UnpackSdfRoundedRectVertexParams(
    std::uint16_t vertexPrimType,
    std::uint16_t vertexFlags,
    CornerRadius& outCornerRadii,
    Thickness& outBorderThickness,
    bool& outHollowBorder) noexcept {
    const auto sdfMode = static_cast<std::uint16_t>((vertexPrimType >> 3u) & 0x3u);
    const std::uint32_t payload27 =
        static_cast<std::uint32_t>(vertexFlags) |
        ((static_cast<std::uint32_t>(vertexPrimType >> 5u) & 0x07FFu) << 16u);

    if (sdfMode == AeroSdfPrimMode_UniformFast) {
        float r = 0.0f;
        float b = 0.0f;
        UnpackSdfRoundedRectParams(vertexFlags, r, b);
        outCornerRadii = CornerRadius{r};
        outBorderThickness = Thickness{b};
        outHollowBorder = false;
        return;
    }

    if (sdfMode == AeroSdfPrimMode_UniformHollowBorder) {
        outHollowBorder = true;
        const bool asymBt = ((payload27 >> 26u) & 1u) != 0u;
        if (!asymBt) {
            float r = 0.0f;
            float b = 0.0f;
            UnpackSdfRoundedRectParams(static_cast<std::uint16_t>(payload27 & 0xFFFFu), r, b);
            outCornerRadii = CornerRadius{r};
            outBorderThickness = Thickness{b};
        } else {
            const float r = static_cast<float>(payload27 & 0x3Fu);
            const float bL = static_cast<float>((payload27 >> 6u) & 0x1Fu);
            const float bT = static_cast<float>((payload27 >> 11u) & 0x1Fu);
            const float bR = static_cast<float>((payload27 >> 16u) & 0x1Fu);
            const float bB = static_cast<float>((payload27 >> 21u) & 0x1Fu);
            outCornerRadii = CornerRadius{r};
            outBorderThickness = Thickness{bL, bT, bR, bB};
        }
        return;
    }

    if (sdfMode == AeroSdfPrimMode_AsymmetricFill) {
        outHollowBorder = false;
        outBorderThickness = Thickness{};
        const std::uint32_t scaleSel = (payload27 >> 24u) & 0x3u;
        const float rStep = 0.5f * static_cast<float>(1u << scaleSel);
        outCornerRadii = CornerRadius{
            static_cast<float>((payload27 >> 0u) & 0x3Fu) * rStep,
            static_cast<float>((payload27 >> 6u) & 0x3Fu) * rStep,
            static_cast<float>((payload27 >> 12u) & 0x3Fu) * rStep,
            static_cast<float>((payload27 >> 18u) & 0x3Fu) * rStep};
        return;
    }

    outHollowBorder = true;
    const float rStep = (((payload27 >> 26u) & 1u) != 0u) ? 2.0f : 1.0f;
    outCornerRadii = CornerRadius{
        static_cast<float>((payload27 >> 0u) & 0x1Fu) * rStep,
        static_cast<float>((payload27 >> 5u) & 0x1Fu) * rStep,
        static_cast<float>((payload27 >> 10u) & 0x1Fu) * rStep,
        static_cast<float>((payload27 >> 15u) & 0x1Fu) * rStep};
    const float bt = static_cast<float>((payload27 >> 20u) & 0x3Fu) * 0.5f;
    outBorderThickness = Thickness{bt};
}

float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    const CornerRadius& cornerRadii,
    float aaFeatherWidth) noexcept {
    if (IsUniformCornerRadius(cornerRadii)) {
        return EvaluateSdfRoundedRectCoverage(
            sampleLocalX,
            sampleLocalY,
            halfWidth,
            halfHeight,
            cornerRadii.topLeft,
            aaFeatherWidth);
    }
    // Quadrant selection matching sdRoundedBox4(p, b, vec4(tr, br, tl, bl)):
    // p.x > 0 ? (p.y > 0 ? br : tr) : (p.y > 0 ? bl : tl)
    const float rRight = (sampleLocalY > 0.0f) ? cornerRadii.bottomRight : cornerRadii.topRight;
    const float rLeft = (sampleLocalY > 0.0f) ? cornerRadii.bottomLeft : cornerRadii.topLeft;
    const float rawR = (sampleLocalX > 0.0f) ? rRight : rLeft;
    const float maxR = std::max(0.0f, std::min(halfWidth, halfHeight));
    const float r = std::clamp(rawR, 0.0f, maxR);
    const float qx = std::fabs(sampleLocalX) - (halfWidth - r);
    const float qy = std::fabs(sampleLocalY) - (halfHeight - r);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float outsideDist = std::sqrt(ox * ox + oy * oy);
    const float insideDist = std::min(std::max(qx, qy), 0.0f);
    const float signedDist = outsideDist + insideDist - r;

    const float feather = std::max(aaFeatherWidth, 0.001f);
    const float t = std::clamp(0.5f - (signedDist / feather), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EvaluateSdfRoundedBorderCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    const CornerRadius& cornerRadii,
    const Thickness& borderThickness,
    float aaFeatherWidth) noexcept {
    const float btL = std::max(0.0f, borderThickness.left);
    const float btT = std::max(0.0f, borderThickness.top);
    const float btR = std::max(0.0f, borderThickness.right);
    const float btB = std::max(0.0f, borderThickness.bottom);
    if (btL <= 0.0f && btT <= 0.0f && btR <= 0.0f && btB <= 0.0f) {
        return 0.0f;
    }

    const float outerCov = EvaluateSdfRoundedRectCoverage(
        sampleLocalX,
        sampleLocalY,
        halfWidth,
        halfHeight,
        cornerRadii,
        aaFeatherWidth);
    if (outerCov <= 0.0f) {
        return 0.0f;
    }

    const float innerHalfW = halfWidth - 0.5f * (btL + btR);
    const float innerHalfH = halfHeight - 0.5f * (btT + btB);
    if (innerHalfW <= 0.0f || innerHalfH <= 0.0f) {
        return outerCov;
    }

    const float innerCenterX = 0.5f * (btL - btR);
    const float innerCenterY = 0.5f * (btT - btB);
    const CornerRadius innerRadii{
        std::max(0.0f, cornerRadii.topLeft - std::max(btL, btT)),
        std::max(0.0f, cornerRadii.topRight - std::max(btR, btT)),
        std::max(0.0f, cornerRadii.bottomRight - std::max(btR, btB)),
        std::max(0.0f, cornerRadii.bottomLeft - std::max(btL, btB))};
    const float innerCov = EvaluateSdfRoundedRectCoverage(
        sampleLocalX - innerCenterX,
        sampleLocalY - innerCenterY,
        innerHalfW,
        innerHalfH,
        innerRadii,
        aaFeatherWidth);
    return std::clamp(outerCov - innerCov, 0.0f, 1.0f);
}

float EvaluateMtsdfCoverage(
    const AxbMtsdfPathEntry& entry,
    float normX01,
    float normY01,
    float renderedWidthPx,
    float renderedHeightPx,
    bool singleChannelTrueSdfOnly) noexcept {
    if (entry.cellWidth == 0 || entry.cellHeight == 0 ||
        entry.mtsdfRgbaPixels.size() < static_cast<std::size_t>(entry.cellWidth) * entry.cellHeight * 4u) {
        return 0.0f;
    }

    const float u = std::clamp(normX01, 0.0f, 1.0f) * static_cast<float>(entry.cellWidth) - 0.5f;
    const float v = std::clamp(normY01, 0.0f, 1.0f) * static_cast<float>(entry.cellHeight) - 0.5f;

    const int maxX = static_cast<int>(entry.cellWidth) - 1;
    const int maxY = static_cast<int>(entry.cellHeight) - 1;
    const float fu = std::floor(u);
    const float fv = std::floor(v);
    const int x0 = std::clamp(static_cast<int>(fu), 0, maxX);
    const int y0 = std::clamp(static_cast<int>(fv), 0, maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);

    const std::uint8_t* basePtr = entry.mtsdfRgbaPixels.data();
    const std::size_t rowStride = static_cast<std::size_t>(entry.cellWidth) * 4u;
    const std::uint8_t* p00 = basePtr + static_cast<std::size_t>(y0) * rowStride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p10 = basePtr + static_cast<std::size_t>(y0) * rowStride + static_cast<std::size_t>(x1) * 4u;
    const std::uint8_t* p01 = basePtr + static_cast<std::size_t>(y1) * rowStride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p11 = basePtr + static_cast<std::size_t>(y1) * rowStride + static_cast<std::size_t>(x1) * 4u;

    if (!singleChannelTrueSdfOnly) {
        const std::uint8_t orRgb =
            p00[0] | p00[1] | p00[2] |
            p10[0] | p10[1] | p10[2] |
            p01[0] | p01[1] | p01[2] |
            p11[0] | p11[1] | p11[2];
        if (orRgb == 0u) {
            return 0.0f;
        }
        const std::uint8_t andRgb =
            p00[0] & p00[1] & p00[2] &
            p10[0] & p10[1] & p10[2] &
            p01[0] & p01[1] & p01[2] &
            p11[0] & p11[1] & p11[2];
        if (andRgb == 255u) {
            return 1.0f;
        }
    } else {
        if ((p00[3] | p10[3] | p01[3] | p11[3]) == 0u) {
            return 0.0f;
        }
        if ((p00[3] & p10[3] & p01[3] & p11[3]) == 255u) {
            return 1.0f;
        }
    }

    const float fx = std::clamp(u - fu, 0.0f, 1.0f);
    const float fy = std::clamp(v - fv, 0.0f, 1.0f);

    auto sampleChannel = [&](int cx, int cy, std::size_t ch) noexcept -> float {
        const std::size_t idx = (static_cast<std::size_t>(cy) * entry.cellWidth + static_cast<std::size_t>(cx)) * 4u + ch;
        return static_cast<float>(entry.mtsdfRgbaPixels[idx]) / 255.0f;
    };

    auto bilerpChannel = [&](std::size_t ch) noexcept -> float {
        const float c00 = sampleChannel(x0, y0, ch);
        const float c10 = sampleChannel(x1, y0, ch);
        const float c01 = sampleChannel(x0, y1, ch);
        const float c11 = sampleChannel(x1, y1, ch);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return top + (bot - top) * fy;
    };

    float sd = 0.0f;
    if (singleChannelTrueSdfOnly) {
        sd = bilerpChannel(3u);
    } else {
        const float r = bilerpChannel(0u);
        const float g = bilerpChannel(1u);
        const float b = bilerpChannel(2u);
        sd = std::max(std::min(r, g), std::min(std::max(r, g), b));
    }

    const float scaleX = std::max(1.0f, renderedWidthPx) / static_cast<float>(entry.cellWidth);
    const float scaleY = std::max(1.0f, renderedHeightPx) / static_cast<float>(entry.cellHeight);
    const float screenPxRange = std::max(0.5f * entry.pxRange * (scaleX + scaleY), 1.0f);
    const float screenPxDistance = screenPxRange * (sd - 0.5f);
    return std::clamp(screenPxDistance + 0.5f, 0.0f, 1.0f);
}

std::uint16_t PackSdfRadialArcParams(
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01) noexcept {
    float normStart = std::fmod(startAngleDeg, 360.0f);
    if (normStart < 0.0f) {
        normStart += 360.0f;
    }
    const float clampedSweep = std::clamp(sweepAngleDeg, 0.0f, 360.0f);
    const float clampedInner = std::clamp(innerRadiusRatio01, 0.0f, 0.9375f);
    const auto qStart = static_cast<std::uint16_t>(
        std::clamp(normStart * (64.0f / 360.0f) + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
    const auto qSweep = static_cast<std::uint16_t>(
        std::clamp(clampedSweep * (63.0f / 360.0f) + 0.5f, 0.0f, 63.0f)) & 0x3Fu;
    const auto qInner = static_cast<std::uint16_t>(
        std::clamp(clampedInner * 16.0f + 0.5f, 0.0f, 15.0f)) & 0x0Fu;
    return static_cast<std::uint16_t>(qStart | (qSweep << 6u) | (qInner << 12u));
}

void UnpackSdfRadialArcParams(
    std::uint16_t packed,
    float& outStartAngleDeg,
    float& outSweepAngleDeg,
    float& outInnerRadiusRatio01) noexcept {
    outStartAngleDeg = static_cast<float>(packed & 0x3Fu) * (360.0f / 64.0f);
    outSweepAngleDeg = static_cast<float>((packed >> 6u) & 0x3Fu) * (360.0f / 63.0f);
    outInnerRadiusRatio01 = static_cast<float>((packed >> 12u) & 0x0Fu) * (1.0f / 16.0f);
}

float EvaluateSdfRadialArcCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01,
    float aaFeatherWidth) noexcept {
    if (sweepAngleDeg <= 0.01f) {
        return 0.0f;
    }
    const float outerR = std::max(1.0f, std::min(halfWidth, halfHeight));
    const float innerR = std::clamp(innerRadiusRatio01, 0.0f, 0.95f) * outerR;
    const float midR = 0.5f * (outerR + innerR);
    const float halfThick = std::max(0.5f, 0.5f * (outerR - innerR));

    float signedDist = 0.0f;
    if (sweepAngleDeg >= 359.0f) {
        const float r = std::sqrt(sampleLocalX * sampleLocalX + sampleLocalY * sampleLocalY);
        signedDist = std::fabs(r - midR) - halfThick;
    } else {
        constexpr float kDegToRad = 3.1415926535f / 180.0f;
        const float midAngleRad = (startAngleDeg + 0.5f * sweepAngleDeg) * kDegToRad;
        const float halfSweepRad = std::clamp(0.5f * sweepAngleDeg * kDegToRad, 0.001f, 3.14159265f);
        const float csMid = std::cos(midAngleRad);
        const float snMid = std::sin(midAngleRad);
        // Rotate sample point by -midAngleRad so the arc is symmetric about +X axis
        const float rx = sampleLocalX * csMid + sampleLocalY * snMid;
        const float ry = std::fabs(-sampleLocalX * snMid + sampleLocalY * csMid);
        const float scX = std::cos(halfSweepRad);
        const float scY = std::sin(halfSweepRad);
        if (scY * rx > scX * ry) {
            const float len = std::sqrt(rx * rx + ry * ry);
            signedDist = std::fabs(len - midR) - halfThick;
        } else {
            const float dx = rx - scX * midR;
            const float dy = ry - scY * midR;
            signedDist = std::sqrt(dx * dx + dy * dy) - halfThick;
        }
    }

    const float feather = std::max(aaFeatherWidth, 0.001f);
    const float t = std::clamp(0.5f - (signedDist / feather), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::uint16_t PackSdfEllipseParams(float strokeThickness, bool hollowStroke) noexcept {
    const float clamped = std::clamp(strokeThickness, 0.0f, 16383.5f);
    const auto qStroke = static_cast<std::uint16_t>(clamped * 2.0f + 0.5f) & 0x7FFFu;
    return static_cast<std::uint16_t>(qStroke | (hollowStroke ? 0x8000u : 0x0000u));
}

void UnpackSdfEllipseParams(
    std::uint16_t packedFlags,
    float& outStrokeThickness,
    bool& outHollowStroke) noexcept {
    outStrokeThickness = static_cast<float>(packedFlags & 0x7FFFu) * 0.5f;
    outHollowStroke = (packedFlags & 0x8000u) != 0u;
}

float EvaluateSdfEllipseCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float strokeThickness,
    bool hollowStroke,
    float aaFeatherWidth) noexcept {
    if (halfWidth <= 0.01f || halfHeight <= 0.01f) {
        return 0.0f;
    }
    auto sdEllipse = [](float px, float py, float a, float b) noexcept -> float {
        const float sa = std::max(a, 0.5f);
        const float sb = std::max(b, 0.5f);
        const float qx = px / sa;
        const float qy = py / sb;
        const float k0 = std::sqrt(qx * qx + qy * qy);
        if (k0 < 1e-4f) {
            return -std::min(sa, sb);
        }
        const float gx = qx / sa;
        const float gy = qy / sb;
        const float k1 = std::sqrt(gx * gx + gy * gy);
        return k0 * (k0 - 1.0f) / std::max(k1, 1e-5f);
    };

    const float feather = std::max(aaFeatherWidth, 0.001f);
    auto smoothCov = [&](float signedDist) noexcept -> float {
        const float t = std::clamp(0.5f - (signedDist / feather), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    const float outerDist = sdEllipse(sampleLocalX, sampleLocalY, halfWidth, halfHeight);
    const float outerCov = smoothCov(outerDist);
    if (!hollowStroke) {
        return outerCov;
    }
    const float st = std::max(0.0f, strokeThickness);
    if (st <= 0.0f || outerCov <= 0.0f) {
        return 0.0f;
    }
    const float innerW = halfWidth - st;
    const float innerH = halfHeight - st;
    if (innerW <= 0.0f || innerH <= 0.0f) {
        return outerCov;
    }
    const float innerCov = smoothCov(sdEllipse(sampleLocalX, sampleLocalY, innerW, innerH));
    return std::clamp(outerCov - innerCov, 0.0f, 1.0f);
}

void PackGradientRoundedRectVertexParams(
    std::uint32_t gradientBrushId,
    float cornerRadius,
    bool isEllipseShape,
    std::uint16_t& outPrimExtra,
    std::uint16_t& outFlags) noexcept {
    const auto clampedId = static_cast<std::uint16_t>(
        std::clamp<std::uint32_t>(gradientBrushId, 1u, GradientBrushRegistry::kMaxBrushId));
    outPrimExtra = static_cast<std::uint16_t>((clampedId & 0x0FFFu) << 4u);
    const float clampedR = std::clamp(cornerRadius, 0.0f, 16383.5f);
    const auto qRad = static_cast<std::uint16_t>(clampedR * 2.0f + 0.5f) & 0x7FFFu;
    outFlags = static_cast<std::uint16_t>(qRad | (isEllipseShape ? 0x8000u : 0x0000u));
}

void UnpackGradientRoundedRectVertexParams(
    std::uint16_t vertexPrimWord,
    std::uint16_t vertexFlags,
    std::uint32_t& outGradientBrushId,
    float& outCornerRadius,
    bool& outIsEllipseShape) noexcept {
    outGradientBrushId = static_cast<std::uint32_t>((vertexPrimWord >> 4u) & 0x0FFFu);
    outCornerRadius = static_cast<float>(vertexFlags & 0x7FFFu) * 0.5f;
    outIsEllipseShape = (vertexFlags & 0x8000u) != 0u;
}

PagedRenderTree::PagedRenderTree()
    : treeId_(gNextRenderTreeId.fetch_add(1u, std::memory_order_relaxed)),
      mtsdfOwnerId_(treeId_) {
    InitPreCachedDigitGlyphs();
}

void PagedRenderTree::RegisterMtsdfInTreeAtlas(const AxbMtsdfPathEntry& entry) {
    if (entry.pathHash == 0u ||
        entry.cellWidth == 0u ||
        entry.cellHeight == 0u ||
        entry.mtsdfRgbaPixels.size() < static_cast<std::size_t>(entry.cellWidth) * entry.cellHeight * 4u) {
        return;
    }
    if (mtsdfEntries_.find(entry.pathHash) != mtsdfEntries_.end()) {
        return;
    }

    constexpr std::uint32_t kAtlasWidth = 1024u;
    constexpr std::uint32_t kMtsdfBaseY = 512u;
    constexpr std::uint32_t kMtsdfMaxRows = 448u; // y in [512 .. 959], leaving [960 .. 1023] for gradients
    constexpr std::uint32_t kGutter = 1u;
    const std::size_t totalBytes = static_cast<std::size_t>(kAtlasWidth) * kMtsdfMaxRows * 4u;
    if (mtsdfAtlasRgba8_.size() != totalBytes) {
        mtsdfAtlasRgba8_.assign(totalBytes, 0u);
    }

    const std::uint32_t cw = entry.cellWidth;
    const std::uint32_t ch = entry.cellHeight;
    if (cw > kAtlasWidth || ch > kMtsdfMaxRows) {
        return;
    }

    if (mtsdfShelfX_ + cw > kAtlasWidth) {
        mtsdfShelfX_ = 0u;
        mtsdfShelfY_ += mtsdfShelfRowH_ + kGutter;
        mtsdfShelfRowH_ = 0u;
    }
    if (mtsdfShelfY_ + ch > kMtsdfBaseY + kMtsdfMaxRows) {
        mtsdfShelfX_ = 0u;
        mtsdfShelfY_ = kMtsdfBaseY;
        mtsdfShelfRowH_ = 0u;
    }

    const std::uint32_t dstX = mtsdfShelfX_;
    const std::uint32_t dstY = mtsdfShelfY_;
    for (std::uint32_t cy = 0u; cy < ch; ++cy) {
        const std::size_t localRow = static_cast<std::size_t>((dstY - kMtsdfBaseY) + cy);
        std::uint8_t* dstRow = mtsdfAtlasRgba8_.data() +
            (localRow * kAtlasWidth + static_cast<std::size_t>(dstX)) * 4u;
        const std::uint8_t* srcRow = entry.mtsdfRgbaPixels.data() +
            static_cast<std::size_t>(cy) * cw * 4u;
        std::memcpy(dstRow, srcRow, static_cast<std::size_t>(cw) * 4u);
    }

    mtsdfShelfX_ += cw + kGutter;
    mtsdfShelfRowH_ = std::max(mtsdfShelfRowH_, ch);

    AxbMtsdfPathEntry packed = entry;
    packed.atlasWidth = static_cast<std::uint16_t>(kAtlasWidth);
    packed.atlasHeight = static_cast<std::uint16_t>(kAtlasWidth);
    constexpr float kInvAtlas = 1.0f / static_cast<float>(kAtlasWidth);
    packed.u0 = (static_cast<float>(dstX) + 0.5f) * kInvAtlas;
    packed.v0 = (static_cast<float>(dstY) + 0.5f) * kInvAtlas;
    packed.u1 = (static_cast<float>(dstX + cw) - 0.5f) * kInvAtlas;
    packed.v1 = (static_cast<float>(dstY + ch) - 0.5f) * kInvAtlas;

    mtsdfRegisteredHashes_.insert(entry.pathHash);
    mtsdfEntries_[entry.pathHash] = packed;
    (resourceStore_ ? resourceStore_ : &UIResourceStore::Default())->RegisterMtsdfPath(entry);
    const std::uint32_t endLocalRow = (dstY - kMtsdfBaseY) + ch;
    mtsdfPopulatedRows_ = std::max(mtsdfPopulatedRows_, std::min(kMtsdfMaxRows, endLocalRow));
    ++mtsdfAtlasRevision_;
}

const AxbMtsdfPathEntry* PagedRenderTree::FindMtsdfEntry(std::uint64_t pathHash) const noexcept {
    if (pathHash == 0ull) {
        return nullptr;
    }
    const auto it = mtsdfEntries_.find(pathHash);
    return (it != mtsdfEntries_.end()) ? &it->second : nullptr;
}

void PagedRenderTree::InitPreCachedDigitGlyphs() noexcept {
    const FontAtlas& atlas = (resourceStore_ && resourceStore_->TryFonts())
        ? *resourceStore_->TryFonts()
        : FontAtlas::Instance();
    for (std::uint32_t cp = 0u; cp < 128u; ++cp) {
        if (cp >= 0x20u && cp <= 0x7Eu) {
            const FontAtlasGlyph& fg = atlas.GetBuiltInAsciiGlyph(cp);
            preCachedGlyphs_[cp] = GlyphUvRect{
                fg.u0,
                fg.v0,
                fg.u1,
                fg.v1,
                fg.advanceEm * 14.0f,
                true};
        } else {
            preCachedGlyphs_[cp] = GlyphUvRect{0.0f, 0.0f, 0.0f, 0.0f, 8.0f, false};
        }
    }
}

TextLayoutResult PagedRenderTree::MeasureTextWithStore(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrapMode) const {
    if (resourceStore_ != nullptr) {
        if (FontAtlas* storeFonts = resourceStore_->TryFonts(); storeFonts != nullptr) {
            return storeFonts->MeasureLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
        }
    }
    return MeasureTextLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
}

GlyphUvRect PagedRenderTree::LookupCodePointGlyph(std::uint32_t codePoint) const noexcept {
    return LookupFontGlyph({}, codePoint, 14.0f);
}

GlyphUvRect PagedRenderTree::LookupFontGlyph(
    std::string_view fontFamily,
    std::uint32_t codePoint,
    float fontSize) const noexcept {
    FontAtlas& atlas = (resourceStore_ && resourceStore_->TryFonts())
        ? *resourceStore_->TryFonts()
        : FontAtlas::Instance();
    const FontAtlasGlyph fg = atlas.EnsureGlyph(fontFamily, codePoint);
    GlyphUvRect rect{};
    rect.u0 = fg.u0;
    rect.v0 = fg.v0;
    rect.u1 = fg.u1;
    rect.v1 = fg.v1;
    rect.advanceX = fg.advanceEm * std::max(1.0f, fontSize);
    rect.valid = fg.valid;
    return rect;
}

void PagedRenderTree::SetRootDpiScale(float scale) noexcept {
    const float clamped = (std::isfinite(scale) && scale > 0.0f)
        ? std::clamp(scale, 0.25f, 8.0f)
        : 1.0f;
    if (std::fabs(clamped - rootDpiScale_) > 1e-4f) {
        rootDpiScale_ = clamped;
    }
}

void PagedRenderTree::Clear() noexcept {
    activeClipRect_ = Rect{0.0f, 0.0f, 16384.0f, 16384.0f};
    activeClipEnabled_ = false;
    pages_.clear();
    nodes_.clear();
    pageDirtyFlags_.clear();
    dirtyPageList_.clear();
    blurLayerSpans_.clear();
    standaloneTextRuns_.clear();
    treeId_ = gNextRenderTreeId.fetch_add(1u, std::memory_order_relaxed);
}

std::uint32_t PagedRenderTree::EnsurePageForAllocation(
    std::uint32_t neededVertices,
    const Rect& clipRect,
    bool clipActive,
    std::uint32_t textureId) {
    const auto desiredBlend = static_cast<std::uint32_t>(activeBlendMode_);
    const bool needNewPage = pages_.empty() ||
        (pages_.back().header.vertexCount + neededVertices > kVerticesPerCommandPage) ||
        !SameClipRect(pages_.back().header, clipRect, clipActive) ||
        (pages_.back().header.reserved[0] != textureId) ||
        (pages_.back().header.reserved[1] != desiredBlend);

    if (needNewPage) {
        const auto newPageIdx = static_cast<std::uint32_t>(pages_.size());
        CommandPage newPage{};
        newPage.header.pageIndex = newPageIdx;
        newPage.header.firstNodeId = static_cast<std::uint32_t>(nodes_.size());
        newPage.header.reserved[0] = textureId;
        newPage.header.reserved[1] = desiredBlend;
        if (clipActive) {
            newPage.header.clipX = clipRect.x;
            newPage.header.clipY = clipRect.y;
            newPage.header.clipW = clipRect.width;
            newPage.header.clipH = clipRect.height;
            newPage.header.flags |= CommandPageFlag_ScissorClipActive;
        }
        pages_.push_back(newPage);
        pageDirtyFlags_.push_back(0u);
        dirtyPageList_.reserve(pages_.size());
    }
    return static_cast<std::uint32_t>(pages_.size() - 1u);
}

void PagedRenderTree::MarkPageDirty(
    std::uint32_t pageIndex,
    std::uint16_t vertexOffsetInPage) noexcept {
    if (pageIndex >= pages_.size()) {
        return;
    }
    CommandPage& page = pages_[pageIndex];
    page.header.dirtyRevision += 1u;
    const std::uint32_t quadIdx = static_cast<std::uint32_t>(vertexOffsetInPage / 4u);
    if (quadIdx < 64u) {
        page.header.dirtyQuadMask |= (1ULL << quadIdx);
    }
    if (pageDirtyFlags_[pageIndex] == 0u) {
        pageDirtyFlags_[pageIndex] = 1u;
        dirtyPageList_.push_back(pageIndex);
    }
}

void PagedRenderTree::ZeroOutNodeVertices(RenderNodeDescriptor& node) noexcept {
    if (node.pageIndex >= pages_.size()) {
        return;
    }
    CommandPage& page = pages_[node.pageIndex];
    const std::size_t vOffset = node.vertexOffsetInPage;
    const std::size_t count = std::min<std::size_t>(
        node.vertexCount,
        kVerticesPerCommandPage - std::min<std::size_t>(vOffset, kVerticesPerCommandPage));
    AeroVertex* __restrict v = page.vertices.data() + vOffset;
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = AeroVertex{};
        v[i].rgba = 0u;
    }
    const std::uint32_t firstQuad = static_cast<std::uint32_t>(vOffset / 4u);
    const std::uint32_t quadCount = static_cast<std::uint32_t>(count / 4u);
    for (std::uint32_t q = 0; q < quadCount; ++q) {
        if (firstQuad + q < 64u) {
            page.header.dirtyQuadMask |= (1ULL << (firstQuad + q));
        }
    }
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
}

void PagedRenderTree::WriteQuadVertices(RenderNodeDescriptor& node) noexcept {
    if (node.pageIndex >= pages_.size()) {
        return;
    }
    CommandPage& page = pages_[node.pageIndex];
    const std::size_t vOffset = node.vertexOffsetInPage;
    if (vOffset + 4u > kVerticesPerCommandPage) {
        return;
    }

    AeroVertex* __restrict v = page.vertices.data() + vOffset;
    const float w = std::max(0.0f, node.width);
    const float h = std::max(0.0f, node.height);

    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float x3 = 0.0f, y3 = 0.0f;
    if (node.hasPerspective3D) {
        const Point p0 = node.worldMatrix3D.ProjectPoint2D(Point{node.x, node.y});
        const Point p1 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + w, node.y});
        const Point p2 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + w, node.y + h});
        const Point p3 = node.worldMatrix3D.ProjectPoint2D(Point{node.x, node.y + h});
        x0 = p0.x; y0 = p0.y;
        x1 = p1.x; y1 = p1.y;
        x2 = p2.x; y2 = p2.y;
        x3 = p3.x; y3 = p3.y;
    } else {
        // Local corners (0,0), (w,0), (w,h), (0,h) transformed by 2D affine matrix + node origin
        const float ox = node.x + node.tx;
        const float oy = node.y + node.ty;
        x0 = ox;
        y0 = oy;
        x1 = ox + w * node.m00;
        y1 = oy + w * node.m10;
        x2 = ox + w * node.m00 + h * node.m01;
        y2 = oy + w * node.m10 + h * node.m11;
        x3 = ox + h * node.m01;
        y3 = oy + h * node.m11;
    }

    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    std::uint16_t prim = 0u;
    if (node.primType == AeroPrimType::GradientRoundedRect) {
        prim = static_cast<std::uint16_t>(0x0008u | (node.encodedPrimExtra & 0xFFF0u));
    } else {
        prim = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(node.primType) & 0x0007u) |
            (node.encodedPrimExtra & 0xFFF8u));
    }
    const std::uint16_t flags = node.encodedSdfParams;

    float uMin = node.uv0X;
    float vMin = node.uv0Y;
    float uMax = node.uv1X;
    float vMax = node.uv1Y;
    if (node.primType == AeroPrimType::SdfRoundedRect ||
        node.primType == AeroPrimType::SdfRadialArc ||
        node.primType == AeroPrimType::SdfEllipse ||
        node.primType == AeroPrimType::GradientRoundedRect) {
        uMin = -w * 0.5f;
        vMin = -h * 0.5f;
        uMax = w * 0.5f;
        vMax = h * 0.5f;
    } else if (node.primType == AeroPrimType::SolidQuad) {
        uMin = 0.0f;
        vMin = 0.0f;
        uMax = 1.0f;
        vMax = 1.0f;
    }

    v[0] = AeroVertex{x0, y0, uMin, vMin, packedColor, prim, flags};
    v[1] = AeroVertex{x1, y1, uMax, vMin, packedColor, prim, flags};
    v[2] = AeroVertex{x2, y2, uMax, vMax, packedColor, prim, flags};
    v[3] = AeroVertex{x3, y3, uMin, vMax, packedColor, prim, flags};
}

std::uint32_t PagedRenderTree::AllocateRenderNode(
    float x,
    float y,
    float width,
    float height,
    const Color& color,
    float cornerRadius,
    float borderThickness,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = 1.0f;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;
    ConfigureRoundedRectDescriptor(desc, CornerRadius{cornerRadius}, Thickness{borderThickness}, false);

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateRoundedRectNode(
    float x,
    float y,
    float width,
    float height,
    const Color& color,
    const CornerRadius& cornerRadii,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = 1.0f;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;
    ConfigureRoundedRectDescriptor(desc, cornerRadii, Thickness{}, false);

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateBorderRingNode(
    float x,
    float y,
    float width,
    float height,
    const Color& borderColor,
    const CornerRadius& cornerRadii,
    const Thickness& borderThickness,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = 1.0f;
    desc.baseRgba = PackColorRgba8(borderColor, 1.0f);
    desc.sourceElement = owner;
    ConfigureRoundedRectDescriptor(desc, cornerRadii, borderThickness, true);

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateEllipseNode(
    float x,
    float y,
    float width,
    float height,
    const Color& color,
    float strokeThickness,
    bool hollowStroke,
    float opacity,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::SdfEllipse;
    desc.encodedSdfParams = PackSdfEllipseParams(strokeThickness, hollowStroke);
    desc.encodedPrimExtra = 0u;
    desc.isHollowBorder = hollowStroke;
    desc.isEllipseShape = true;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = opacity;
    desc.borderThickness = strokeThickness;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateGradientRoundedRectNode(
    float x,
    float y,
    float width,
    float height,
    std::uint32_t gradientBrushId,
    const CornerRadius& cornerRadii,
    bool isEllipseShape,
    const Color& tintModulator,
    float opacity,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const CornerRadius cr = SanitizeCornerRadius(cornerRadii);
    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::GradientRoundedRect;
    desc.gradientBrushId = gradientBrushId;
    desc.isEllipseShape = isEllipseShape;
    desc.cornerRadius = cr.topLeft;
    desc.cornerRadii = cr;
    PackGradientRoundedRectVertexParams(
        gradientBrushId,
        cr.topLeft,
        isEllipseShape,
        desc.encodedPrimExtra,
        desc.encodedSdfParams);
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = opacity;
    desc.baseRgba = PackColorRgba8(tintModulator, 1.0f);
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateHudDigitsNode(
    float x,
    float y,
    float glyphWidth,
    float glyphHeight,
    std::uint32_t maxDigits,
    const Color& color) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    const std::uint32_t clampedDigits = std::clamp(maxDigits, 1u, maxQuadsPerPage);
    const std::uint32_t neededVertices = clampedDigits * 4u;
    const std::uint32_t pageIdx = EnsurePageForAllocation(neededVertices, activeClipRect_, activeClipEnabled_);

    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += neededVertices;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = static_cast<std::uint16_t>(neededVertices);
    desc.primType = AeroPrimType::SdfTextGlyph;
    desc.encodedSdfParams = AeroVertexFlag_GrayscaleSdfText;
    desc.x = x;
    desc.y = y;
    desc.width = glyphWidth;
    desc.height = glyphHeight;
    desc.opacity = 1.0f;
    desc.baseRgba = PackColorRgba8(color, 1.0f);

    nodes_.push_back(desc);
    (void)UpdateHudDigitsText(nodeId, "0");
    return nodeId;
}

std::uint32_t PagedRenderTree::AllocateTextRunNode(
    float x,
    float y,
    float fontSize,
    std::string_view utf8Text,
    const Color& color,
    float opacity,
    std::uint32_t reservedGlyphCapacity,
    const void* owner,
    std::string_view fontFamily,
    float maxWidth,
    TextWrapping wrapMode) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    const std::size_t visibleGlyphs = CountVisibleUtf8Glyphs(utf8Text);
    const std::uint32_t desiredSlots = (reservedGlyphCapacity > 0u)
        ? reservedGlyphCapacity
        : static_cast<std::uint32_t>(std::max<std::size_t>(visibleGlyphs + 8u, 16u));
    const std::uint32_t clampedSlots = std::clamp(desiredSlots, 1u, maxQuadsPerPage);
    const std::uint32_t neededVertices = clampedSlots * 4u;

    const std::uint32_t pageIdx = EnsurePageForAllocation(neededVertices, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += neededVertices;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = static_cast<std::uint16_t>(neededVertices);
    desc.primType = AeroPrimType::SdfTextGlyph;
    desc.encodedSdfParams = AeroVertexFlag_GrayscaleSdfText;
    desc.x = x;
    desc.y = y;
    desc.width = std::max(1.0f, fontSize);
    desc.height = std::max(1.0f, fontSize * 1.2f);
    desc.opacity = opacity;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    (void)UpdateTextRunNode(nodeId, x, y, fontSize, utf8Text, color, opacity, fontFamily, maxWidth, wrapMode);
    return nodeId;
}

std::uint32_t PagedRenderTree::AllocateMtsdfPathNode(
    float x,
    float y,
    float width,
    float height,
    const AxbMtsdfPathEntry& mtsdfEntry,
    const Color& fillColor,
    float strokeThickness,
    float opacity,
    const void* owner) {
    RegisterMtsdfInTreeAtlas(mtsdfEntry);
    const AxbMtsdfPathEntry* packedEntry = (mtsdfEntry.pathHash != 0u) ? FindMtsdfEntry(mtsdfEntry.pathHash) : nullptr;
    const AxbMtsdfPathEntry& activeEntry = packedEntry ? *packedEntry : mtsdfEntry;
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const MtsdfMappedQuad mapped = ComputeMtsdfMappedPlacement(
        Rect{x, y, width, height},
        activeEntry,
        strokeThickness);

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::MsdfVectorPath;
    desc.encodedSdfParams = PackMtsdfParams(
        activeEntry.pxRange,
        activeEntry.atlasWidth,
        strokeThickness,
        false);
    desc.x = mapped.quadRect.x;
    desc.y = mapped.quadRect.y;
    desc.width = mapped.quadRect.width;
    desc.height = mapped.quadRect.height;
    desc.opacity = opacity;
    desc.borderThickness = strokeThickness;
    desc.uv0X = mapped.u0;
    desc.uv0Y = mapped.v0;
    desc.uv1X = mapped.u1;
    desc.uv1Y = mapped.v1;
    desc.baseRgba = PackColorRgba8(fillColor, 1.0f);
    desc.mtsdfPathHash = activeEntry.pathHash;
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}

std::uint16_t PagedRenderTree::AllocateSvgPathStrokeNodes(
    std::string_view pathStr,
    const Rect& box,
    Stretch stretch,
    float strokeWidth,
    const Color& strokeColor,
    std::uint32_t strokeGradId,
    float opacity,
    const void* owner,
    float trimStart01,
    float trimEnd01) {
    if (pathStr.empty() || strokeWidth <= 0.0f || opacity <= 0.0f ||
        (strokeGradId == 0u && strokeColor.a <= 0.0f) ||
        box.width <= 0.0f || box.height <= 0.0f) {
        return 0u;
    }

    const std::vector<std::vector<Point>> subpaths = ParseSvgPathToSubpaths(pathStr);
    if (subpaths.empty()) {
        return 0u;
    }

    float minX = 1e30f;
    float minY = 1e30f;
    float maxX = -1e30f;
    float maxY = -1e30f;
    for (const auto& sp : subpaths) {
        for (const Point& pt : sp) {
            minX = std::min(minX, pt.x);
            minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x);
            maxY = std::max(maxY, pt.y);
        }
    }
    if (maxX < minX || maxY < minY) {
        return 0u;
    }

    const float natW = std::max(1e-3f, maxX - minX);
    const float natH = std::max(1e-3f, maxY - minY);
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (stretch == Stretch::Fill) {
        scaleX = box.width / natW;
        scaleY = box.height / natH;
    } else if (stretch == Stretch::Uniform) {
        const float s = std::min(box.width / natW, box.height / natH);
        scaleX = s;
        scaleY = s;
    } else if (stretch == Stretch::UniformToFill) {
        const float s = std::max(box.width / natW, box.height / natH);
        scaleX = s;
        scaleY = s;
    }
    const float dstW = natW * scaleX;
    const float dstH = natH * scaleY;
    const float ox = (stretch == Stretch::None)
        ? (box.x - minX)
        : (box.x + (box.width - dstW) * 0.5f - minX * scaleX);
    const float oy = (stretch == Stretch::None)
        ? (box.y - minY)
        : (box.y + (box.height - dstH) * 0.5f - minY * scaleY);

    std::uint16_t emitted = 0u;
    for (const auto& sp : subpaths) {
        std::vector<Point> transformed;
        transformed.reserve(sp.size());
        for (const Point& pt : sp) {
            transformed.push_back(Point{ox + pt.x * scaleX, oy + pt.y * scaleY});
        }
        const std::vector<Point> segPts = (trimStart01 > 0.0f || trimEnd01 < 1.0f)
            ? TrimSubpathPolyline(transformed, trimStart01, trimEnd01)
            : transformed;
        if (segPts.size() < 2u) {
            continue;
        }
        for (std::size_t i = 0u; i + 1u < segPts.size(); ++i) {
            const float dx = segPts[i + 1u].x - segPts[i].x;
            const float dy = segPts[i + 1u].y - segPts[i].y;
            const float len = std::hypot(dx, dy);
            if (len <= 1e-4f) {
                continue;
            }
            const float midX = 0.5f * (segPts[i].x + segPts[i + 1u].x);
            const float midY = 0.5f * (segPts[i].y + segPts[i + 1u].y);
            const float cosA = dx / len;
            const float sinA = dy / len;
            const float rx = midX - (0.5f * len) * cosA + (0.5f * strokeWidth) * sinA;
            const float ry = midY - (0.5f * len) * sinA - (0.5f * strokeWidth) * cosA;
            const std::uint32_t nodeId = (strokeGradId > 0u)
                ? AllocateGradientRoundedRectNode(
                      0.0f,
                      0.0f,
                      len,
                      strokeWidth,
                      strokeGradId,
                      CornerRadius{strokeWidth * 0.5f},
                      false,
                      strokeColor,
                      opacity,
                      owner)
                : AllocateRenderNode(
                      0.0f,
                      0.0f,
                      len,
                      strokeWidth,
                      Color{strokeColor.r, strokeColor.g, strokeColor.b, strokeColor.a * opacity},
                      strokeWidth * 0.5f,
                      0.0f,
                      owner);
            (void)UpdateNodeTransform2D(nodeId, cosA, sinA, -sinA, cosA, rx, ry);
            ++emitted;
        }
    }
    return emitted;
}

std::uint32_t PagedRenderTree::AllocateTexturedNode(
    float x,
    float y,
    float width,
    float height,
    float u0,
    float v0,
    float u1,
    float v1,
    const Color& tintColor,
    float opacity,
    const void* owner) {
    return AllocateTexturedNode(x, y, width, height, u0, v0, u1, v1, tintColor, opacity, owner, 0u);
}

std::uint32_t PagedRenderTree::AllocateTexturedNode(
    float x,
    float y,
    float width,
    float height,
    float u0,
    float v0,
    float u1,
    float v1,
    const Color& tintColor,
    float opacity,
    const void* owner,
    std::uint32_t textureId) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(
        4u,
        activeClipRect_,
        activeClipEnabled_,
        textureId);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::TexturedQuad;
    desc.encodedSdfParams = AeroVertexFlag_None;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = opacity;
    desc.uv0X = u0;
    desc.uv0Y = v0;
    desc.uv1X = u1;
    desc.uv1Y = v1;
    desc.baseRgba = PackColorRgba8(tintColor, 1.0f);
    desc.textureId = textureId;
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


std::uint32_t PagedRenderTree::AllocateRadialArcNode(
    float x,
    float y,
    float width,
    float height,
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01,
    const Color& color,
    float opacity,
    const void* owner) {
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::SdfRadialArc;
    desc.encodedSdfParams = PackSdfRadialArcParams(startAngleDeg, sweepAngleDeg, innerRadiusRatio01);
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = opacity;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
}


bool PagedRenderTree::UpdateRadialArcNode(
    std::uint32_t nodeId,
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    const std::uint16_t packed = PackSdfRadialArcParams(startAngleDeg, sweepAngleDeg, innerRadiusRatio01);
    if (node.primType == AeroPrimType::SdfRadialArc && node.encodedSdfParams == packed) {
        return false;
    }
    node.primType = AeroPrimType::SdfRadialArc;
    node.encodedSdfParams = packed;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

std::uint32_t PagedRenderTree::AllocateHudBarBatch(
    std::size_t count,
    float startX,
    float startY,
    float barWidth,
    float barHeight,
    float verticalSpacing,
    const Color& color,
    float cornerRadius) {
    if (count == 0) {
        return 0;
    }
    nodes_.reserve(nodes_.size() + count);
    const std::size_t estimatedPages = (count + 41u) / 42u + 2u;
    pages_.reserve(pages_.size() + estimatedPages);
    pageDirtyFlags_.reserve(pageDirtyFlags_.size() + estimatedPages);
    dirtyPageList_.reserve(pages_.size() + estimatedPages);

    std::uint32_t firstId = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float y = startY + static_cast<float>(i) * (barHeight + verticalSpacing);
        const std::uint32_t id = AllocateRenderNode(
            startX,
            y,
            barWidth,
            barHeight,
            color,
            cornerRadius,
            0.0f,
            nullptr);
        if (i == 0) {
            firstId = id;
        }
    }
    return firstId;
}

bool PagedRenderTree::UpdateNodeWidth(std::uint32_t nodeId, float newWidth) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.width == newWidth) {
        return false;
    }
    node.width = newWidth;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateNodeOpacity(std::uint32_t nodeId, float newOpacity) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.opacity == newOpacity) {
        return false;
    }
    node.opacity = newOpacity;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateNodeTransform2D(
    std::uint32_t nodeId,
    float m00,
    float m01,
    float m10,
    float m11,
    float tx,
    float ty) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    node.m00 = m00;
    node.m01 = m01;
    node.m10 = m10;
    node.m11 = m11;
    node.tx = tx;
    node.ty = ty;
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateHudDigitsText(
    std::uint32_t nodeId,
    std::string_view asciiDigits) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.pageIndex >= pages_.size()) {
        return false;
    }

    CommandPage& page = pages_[node.pageIndex];
    const std::size_t maxSlots = node.vertexCount / 4u;
    AeroVertex* __restrict vBase = page.vertices.data() + node.vertexOffsetInPage;
    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    const auto prim = static_cast<std::uint16_t>(AeroPrimType::SdfTextGlyph);
    const std::uint16_t flags = node.encodedSdfParams;

    float cursorX = node.x + node.tx;
    const float cursorY = node.y + node.ty;
    const float gw = node.width;
    const float gh = node.height;

    for (std::size_t i = 0; i < maxSlots; ++i) {
        AeroVertex* __restrict q = vBase + (i * 4u);
        if (i < asciiDigits.size()) {
            const auto chIdx = static_cast<std::uint8_t>(asciiDigits[i]) & 0x7Fu;
            const GlyphUvRect& uv = preCachedGlyphs_[chIdx];
            const float x0 = cursorX;
            const float y0 = cursorY;
            const float x1 = cursorX + gw;
            const float y1 = cursorY + gh;
            q[0] = AeroVertex{x0, y0, uv.u0, uv.v0, packedColor, prim, flags};
            q[1] = AeroVertex{x1, y0, uv.u1, uv.v0, packedColor, prim, flags};
            q[2] = AeroVertex{x1, y1, uv.u1, uv.v1, packedColor, prim, flags};
            q[3] = AeroVertex{x0, y1, uv.u0, uv.v1, packedColor, prim, flags};
            cursorX += gw;
        } else {
            q[0] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[1] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[2] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
            q[3] = AeroVertex{cursorX, cursorY, 0.0f, 0.0f, 0u, prim, flags};
        }
    }

    const std::uint32_t firstQuadIdx = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
    for (std::size_t i = 0; i < maxSlots; ++i) {
        const std::uint32_t qIdx = firstQuadIdx + static_cast<std::uint32_t>(i);
        if (qIdx < 64u) {
            page.header.dirtyQuadMask |= (1ULL << qIdx);
        }
    }
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::WriteLayoutGlyphSlice(
    std::uint32_t nodeId,
    float x,
    float y,
    float fontSize,
    const TextLayoutResult& layout,
    std::size_t& ioGlyphCursor,
    const Color& color,
    float opacity,
    const std::vector<RichGlyphStyle>* optRichStyles) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.pageIndex >= pages_.size() || node.primType != AeroPrimType::SdfTextGlyph) {
        return false;
    }

    const std::size_t maxSlots = node.vertexCount / 4u;
    node.x = x;
    node.y = y;
    node.width = std::max(1.0f, layout.totalWidth > 0.0f ? layout.totalWidth : fontSize);
    node.height = std::max(1.0f, layout.totalHeight > 0.0f ? layout.totalHeight : (fontSize * 1.2f));
    node.opacity = opacity;
    node.baseRgba = PackColorRgba8(color, 1.0f);

    // Recompute node.tx / node.ty from baseTx / baseTy so moving (x, y) under full 2D affine + Projection3D stays exact
    node.tx = node.baseTx + node.x * (node.m00 - 1.0f) + node.y * node.m01;
    node.ty = node.baseTy + node.x * node.m10 + node.y * (node.m11 - 1.0f);

    CommandPage& page = pages_[node.pageIndex];
    AeroVertex* __restrict vBase = page.vertices.data() + node.vertexOffsetInPage;
    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    const auto prim = static_cast<std::uint16_t>(AeroPrimType::SdfTextGlyph);
    const std::uint16_t flags = node.encodedSdfParams;

    const float originX = node.x + node.tx;
    const float originY = node.y + node.ty;

    for (std::size_t slot = 0u; slot < maxSlots; ++slot) {
        AeroVertex* __restrict q = vBase + (slot * 4u);
        bool emitted = false;
        while (ioGlyphCursor < layout.glyphs.size()) {
            const std::size_t gIndex = ioGlyphCursor++;
            const PositionedGlyph& pg = layout.glyphs[gIndex];
            if (pg.isWhitespace) {
                continue;
            }
            std::uint32_t glyphPackedColor = packedColor;
            float italicTopShear = 0.0f;
            float boldPadX = 0.0f;
            if (optRichStyles != nullptr && pg.sourceCodePointIndex < optRichStyles->size()) {
                const RichGlyphStyle& st = (*optRichStyles)[pg.sourceCodePointIndex];
                if (st.hasCustomColor) {
                    glyphPackedColor = ScalePackedRgbaAlpha(PackColorRgba8(st.color, 1.0f), node.opacity);
                }
                if (st.italic) {
                    italicTopShear = 0.18f * std::max(0.0f, pg.y1 - pg.y0);
                }
                if (st.bold) {
                    boldPadX = 0.35f;
                }
            }
            const float lx0 = pg.x0 - boldPadX * 0.5f;
            const float lx1 = pg.x1 + boldPadX * 0.5f;
            const float gw = std::max(0.0f, lx1 - lx0);
            const float gh = std::max(0.0f, pg.y1 - pg.y0);
            float gx0 = 0.0f, gy0 = 0.0f, gx1 = 0.0f, gy1 = 0.0f;
            float gx2 = 0.0f, gy2 = 0.0f, gx3 = 0.0f, gy3 = 0.0f;
            if (node.hasPerspective3D) {
                const Point p0 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + lx0 + italicTopShear, node.y + pg.y0});
                const Point p1 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + lx1 + italicTopShear, node.y + pg.y0});
                const Point p2 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + lx1, node.y + pg.y1});
                const Point p3 = node.worldMatrix3D.ProjectPoint2D(Point{node.x + lx0, node.y + pg.y1});
                gx0 = p0.x; gy0 = p0.y;
                gx1 = p1.x; gy1 = p1.y;
                gx2 = p2.x; gy2 = p2.y;
                gx3 = p3.x; gy3 = p3.y;
            } else {
                gx0 = originX + (lx0 + italicTopShear) * node.m00 + pg.y0 * node.m01;
                gy0 = originY + (lx0 + italicTopShear) * node.m10 + pg.y0 * node.m11;
                gx1 = gx0 + gw * node.m00;
                gy1 = gy0 + gw * node.m10;
                const float botX0 = originX + lx0 * node.m00 + pg.y1 * node.m01;
                const float botY0 = originY + lx0 * node.m10 + pg.y1 * node.m11;
                gx3 = botX0;
                gy3 = botY0;
                gx2 = botX0 + gw * node.m00;
                gy2 = botY0 + gw * node.m10;
            }

            q[0] = AeroVertex{gx0, gy0, pg.u0, pg.v0, glyphPackedColor, prim, flags};
            q[1] = AeroVertex{gx1, gy1, pg.u1, pg.v0, glyphPackedColor, prim, flags};
            q[2] = AeroVertex{gx2, gy2, pg.u1, pg.v1, glyphPackedColor, prim, flags};
            q[3] = AeroVertex{gx3, gy3, pg.u0, pg.v1, glyphPackedColor, prim, flags};
            emitted = true;
            break;
        }

        if (!emitted) {
            q[0] = AeroVertex{originX, originY, 0.0f, 0.0f, 0u, prim, flags};
            q[1] = AeroVertex{originX, originY, 0.0f, 0.0f, 0u, prim, flags};
            q[2] = AeroVertex{originX, originY, 0.0f, 0.0f, 0u, prim, flags};
            q[3] = AeroVertex{originX, originY, 0.0f, 0.0f, 0u, prim, flags};
        }
    }

    const std::uint32_t firstQuadIdx = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
    for (std::size_t i = 0u; i < maxSlots; ++i) {
        const std::uint32_t qIdx = firstQuadIdx + static_cast<std::uint32_t>(i);
        if (qIdx < 64u) {
            page.header.dirtyQuadMask |= (1ULL << qIdx);
        }
    }
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::UpdateTextRunNode(
    std::uint32_t nodeId,
    float x,
    float y,
    float fontSize,
    std::string_view utf8Text,
    const Color& color,
    float opacity,
    std::string_view fontFamily,
    float maxWidth,
    TextWrapping wrapMode) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    const RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.pageIndex >= pages_.size() || node.primType != AeroPrimType::SdfTextGlyph) {
        return false;
    }

    const TextLayoutResult layout = MeasureTextWithStore(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
    const std::size_t maxSlots = node.vertexCount / 4u;
    const std::size_t neededGlyphs = CountLayoutVisibleGlyphs(layout);
    if (neededGlyphs > maxSlots) {
        return false;
    }

    if (node.sourceElement == nullptr) {
        StandaloneTextRunRecord rec{};
        rec.x = x;
        rec.y = y;
        rec.fontSize = fontSize;
        rec.utf8Text.assign(utf8Text);
        rec.fontFamily.assign(fontFamily);
        rec.color = color;
        rec.opacity = opacity;
        rec.maxWidth = maxWidth;
        rec.wrapMode = wrapMode;
        standaloneTextRuns_[nodeId] = std::move(rec);
    }

    std::size_t glyphCursor = 0u;
    return WriteLayoutGlyphSlice(nodeId, x, y, fontSize, layout, glyphCursor, color, opacity);
}

std::uint16_t PagedRenderTree::EmitTextRunNodes(
    float x,
    float y,
    float fontSize,
    std::string_view utf8Text,
    const Color& color,
    float opacity,
    const RenderTraversalState& curState,
    const void* owner,
    std::string_view fontFamily,
    float maxWidth,
    TextWrapping wrapMode,
    TextAlignment textAlign,
    float alignBoxWidth,
    const std::vector<RichGlyphStyle>* optRichStyles) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    TextLayoutResult layout = MeasureTextWithStore(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
    ApplyTextAlignmentToLayout(layout, textAlign, alignBoxWidth);
    const std::size_t visibleGlyphs = CountLayoutVisibleGlyphs(layout);
    const std::size_t totalDesiredSlots = std::max<std::size_t>(visibleGlyphs + 8u, 16u);
    const std::size_t chunkCount = std::max<std::size_t>(
        1u,
        (visibleGlyphs + maxQuadsPerPage - 1u) / maxQuadsPerPage);

    std::size_t remainingDesired = totalDesiredSlots;
    std::size_t glyphCursor = 0u;
    std::uint16_t emittedNodes = 0u;

    for (std::size_t c = 0u; c < chunkCount; ++c) {
        const std::uint32_t slotCap = (c + 1u < chunkCount)
            ? maxQuadsPerPage
            : static_cast<std::uint32_t>(std::clamp<std::size_t>(remainingDesired, 1u, maxQuadsPerPage));
        if (remainingDesired > slotCap) {
            remainingDesired -= slotCap;
        } else {
            remainingDesired = 0u;
        }

        const std::uint32_t neededVertices = slotCap * 4u;
        const std::uint32_t pageIdx = EnsurePageForAllocation(neededVertices, activeClipRect_, activeClipEnabled_);
        CommandPage& page = pages_[pageIdx];
        const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
        page.header.vertexCount += neededVertices;
        page.header.nodeCount += 1u;

        const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
        RenderNodeDescriptor desc{};
        desc.nodeId = nodeId;
        desc.pageIndex = pageIdx;
        desc.vertexOffsetInPage = vOffset;
        desc.vertexCount = static_cast<std::uint16_t>(neededVertices);
        desc.primType = AeroPrimType::SdfTextGlyph;
        desc.encodedSdfParams = AeroVertexFlag_GrayscaleSdfText;
        desc.x = x;
        desc.y = y;
        desc.width = std::max(1.0f, layout.totalWidth > 0.0f ? layout.totalWidth : fontSize);
        desc.height = std::max(1.0f, layout.totalHeight > 0.0f ? layout.totalHeight : (fontSize * 1.2f));
        desc.opacity = opacity;
        desc.m00 = curState.m00;
        desc.m01 = curState.m01;
        desc.m10 = curState.m10;
        desc.m11 = curState.m11;
        desc.baseTx = curState.tx;
        desc.baseTy = curState.ty;
        desc.tx = curState.tx + x * (curState.m00 - 1.0f) + y * curState.m01;
        desc.ty = curState.ty + x * curState.m10 + y * (curState.m11 - 1.0f);
        desc.inheritedOpacity = curState.inheritedOpacity;
        desc.hasPerspective3D = curState.hasPerspective3D;
        desc.blendMode = curState.blendMode;
        desc.worldMatrix3D = curState.world3D;
        desc.baseRgba = PackColorRgba8(color, 1.0f);
        desc.sourceElement = owner;

        nodes_.push_back(desc);
        (void)WriteLayoutGlyphSlice(
            nodeId,
            x,
            y,
            fontSize,
            layout,
            glyphCursor,
            color,
            opacity,
            optRichStyles);
        ++emittedNodes;
    }
    return emittedNodes;
}

bool PagedRenderTree::InvalidateNodePage(std::uint32_t nodeId) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.primType != AeroPrimType::SdfTextGlyph) {
        WriteQuadVertices(node);
    } else if (node.pageIndex < pages_.size()) {
        CommandPage& page = pages_[node.pageIndex];
        const std::uint32_t firstQuad = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
        const std::uint32_t quadCount = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(node.vertexCount / 4u));
        for (std::uint32_t i = 0; i < quadCount; ++i) {
            if (firstQuad + i < 64u) {
                page.header.dirtyQuadMask |= (1ULL << (firstQuad + i));
            }
        }
    }
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
    return true;
}

bool PagedRenderTree::RefreshTextNode(std::uint32_t nodeId) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    if (const auto it = standaloneTextRuns_.find(nodeId); it != standaloneTextRuns_.end()) {
        const StandaloneTextRunRecord& rec = it->second;
        const TextLayoutResult layout = MeasureTextWithStore(
            rec.utf8Text, rec.fontFamily, rec.fontSize, rec.maxWidth, rec.wrapMode);
        std::size_t glyphCursor = 0u;
        return WriteLayoutGlyphSlice(
            nodeId, rec.x, rec.y, rec.fontSize, layout, glyphCursor, rec.color, rec.opacity);
    }
    return InvalidateNodePage(nodeId);
}

std::size_t PagedRenderTree::RefreshGlyphRange(
    std::uint32_t rangeStart,
    std::uint32_t rangeEnd) noexcept {
    if (rangeEnd < rangeStart) {
        return 0u;
    }
    std::size_t refreshed = 0u;
    for (std::uint32_t nid = 0u; nid < static_cast<std::uint32_t>(nodes_.size()); ++nid) {
        const RenderNodeDescriptor& nd = nodes_[nid];
        if (nd.primType != AeroPrimType::SdfTextGlyph) {
            continue;
        }
        if (const auto it = standaloneTextRuns_.find(nid); it != standaloneTextRuns_.end()) {
            if (TextContainsCodePointInRange(it->second.utf8Text, rangeStart, rangeEnd)) {
                if (RefreshTextNode(nid)) {
                    ++refreshed;
                }
            }
        }
    }
    return refreshed;
}

std::size_t PagedRenderTree::BatchUpdateWidthsFromRawColumn(
    std::uint32_t firstNodeId,
    std::size_t nodeCount,
    const void* rawColumnBase,
    std::size_t elementStrideBytes,
    std::size_t fieldByteOffset,
    float scaleFactor) noexcept {
    if (!rawColumnBase || elementStrideBytes == 0 || firstNodeId >= nodes_.size()) {
        return 0;
    }
    const std::size_t availNodes = nodes_.size() - firstNodeId;
    const std::size_t count = (nodeCount < availNodes) ? nodeCount : availNodes;
    if (count == 0) {
        return 0;
    }

    const auto* __restrict srcBytes = static_cast<const std::uint8_t*>(rawColumnBase) + fieldByteOffset;
    RenderNodeDescriptor* __restrict nodePtr = nodes_.data() + firstNodeId;
    CommandPage* __restrict pagesBase = pages_.data();
    std::uint8_t* __restrict dirtyFlagsBase = pageDirtyFlags_.data();

    std::size_t dirtyListCount = dirtyPageList_.size();
    dirtyPageList_.resize(pages_.size());
    std::uint32_t* __restrict dirtyListBase = dirtyPageList_.data();

    std::size_t updatedCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        float rawVal = 0.0f;
        std::memcpy(&rawVal, srcBytes + i * elementStrideBytes, sizeof(float));
        const float w = rawVal * scaleFactor;

        RenderNodeDescriptor& node = nodePtr[i];
        if (node.width != w) {
            const float oldW = node.width;
            node.width = w;
            CommandPage& page = pagesBase[node.pageIndex];
            AeroVertex* __restrict v = page.vertices.data() + node.vertexOffsetInPage;

            if (node.m00 == 1.0f && node.m10 == 0.0f && node.m01 == 0.0f && node.m11 == 1.0f) {
                const float ox = node.x + node.tx;
                const float rightX = ox + w;
                v[1].x = rightX;
                v[2].x = rightX;
                if (node.primType == AeroPrimType::SdfRoundedRect ||
                    node.primType == AeroPrimType::SdfEllipse ||
                    node.primType == AeroPrimType::GradientRoundedRect) {
                    const float halfW = w * 0.5f;
                    v[0].u = -halfW;
                    v[1].u = halfW;
                    v[2].u = halfW;
                    v[3].u = -halfW;
                }
            } else {
                WriteQuadVertices(node);
            }

            const std::uint32_t quadIdx = static_cast<std::uint32_t>(node.vertexOffsetInPage / 4u);
            if (quadIdx < 64u) {
                page.header.dirtyQuadMask |= (1ULL << quadIdx);
            }
            page.header.dirtyRevision += 1u;
            if (dirtyFlagsBase[node.pageIndex] == 0u) {
                dirtyFlagsBase[node.pageIndex] = 1u;
                dirtyListBase[dirtyListCount++] = node.pageIndex;
            }
            ++updatedCount;
        }
    }

    dirtyPageList_.resize(dirtyListCount);
    return updatedCount;
}

bool PagedRenderTree::IsPageDirty(std::size_t pageIndex) const noexcept {
    return (pageIndex < pageDirtyFlags_.size()) && (pageDirtyFlags_[pageIndex] != 0u);
}

std::size_t PagedRenderTree::DirtyPageCount() const noexcept {
    return dirtyPageList_.size();
}

void PagedRenderTree::ClearDirtyPages() noexcept {
    for (const std::uint32_t idx : dirtyPageList_) {
        if (idx < pageDirtyFlags_.size()) {
            pageDirtyFlags_[idx] = 0u;
        }
        if (idx < pages_.size()) {
            pages_[idx].header.dirtyQuadMask = 0ULL;
        }
    }
    dirtyPageList_.clear();
}

} // namespace aero
