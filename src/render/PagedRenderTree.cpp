#include "aero/render/PagedRenderTree.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

namespace {

std::atomic<std::uint32_t> gNextRenderTreeId{1u};

[[nodiscard]] std::string ResolveElementFontFamily(const UIElement* elem) {
    for (const UIElement* cur = elem; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            if (!sv->view().empty()) {
                return sv->str();
            }
        }
    }
    return {};
}

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

[[nodiscard]] std::size_t CountLayoutVisibleGlyphs(const TextLayoutResult& layout) noexcept {
    std::size_t count = 0u;
    for (const auto& g : layout.glyphs) {
        if (!g.isWhitespace) {
            ++count;
        }
    }
    return count;
}

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

[[nodiscard]] Matrix2D MatrixFromState(
    float m00, float m01, float m10, float m11, float tx, float ty) noexcept {
    return Matrix2D{m00, m01, m10, m11, tx, ty};
}

void ApplyStateMatrix(
    float& ioM00, float& ioM01, float& ioM10, float& ioM11, float& ioTx, float& ioTy,
    const Matrix2D& localTransform) noexcept {
    if (localTransform.IsIdentity()) {
        return;
    }
    const Matrix2D parentM = MatrixFromState(ioM00, ioM01, ioM10, ioM11, ioTx, ioTy);
    const Matrix2D composed = Matrix2D::Multiply(parentM, localTransform);
    ioM00 = composed.m00;
    ioM01 = composed.m01;
    ioM10 = composed.m10;
    ioM11 = composed.m11;
    ioTx = composed.tx;
    ioTy = composed.ty;
}

struct MtsdfMappedQuad {
    Rect quadRect{};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

// Map the geometry sub-region [cellBoxX0..cellBoxX1] x [cellBoxY0..cellBoxY1] inside the uniform-scaled
// MTSDF cell onto screen `fitted`, preserving exact non-square aspect ratio and outer AA/stroke padding.
[[nodiscard]] MtsdfMappedQuad ComputeMtsdfMappedPlacement(
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

void ClassifyPathEmission(
    const Path& pathElem,
    bool& outEmitFillNode,
    bool& outEmitSeparateStrokeNode) noexcept {
    const float strokeW = pathElem.GetStrokeThickness();
    const bool hasStrokeBrush = pathElem.HasExplicitStroke() && strokeW > 0.0f;
    const bool hasFillBrush = pathElem.HasExplicitFill();
    outEmitFillNode = hasFillBrush;
    outEmitSeparateStrokeNode = hasStrokeBrush;
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

[[nodiscard]] Rect ComputeStretchedDestRect(
    const Rect& box,
    float naturalW,
    float naturalH,
    Stretch stretch) noexcept {
    if (box.width <= 0.0f || box.height <= 0.0f || naturalW <= 0.0f || naturalH <= 0.0f) {
        return box;
    }
    switch (stretch) {
        case Stretch::None:
            return Rect{box.x, box.y, naturalW, naturalH};
        case Stretch::Fill:
            return box;
        case Stretch::UniformToFill: {
            const float s = std::max(box.width / naturalW, box.height / naturalH);
            const float dw = naturalW * s;
            const float dh = naturalH * s;
            return Rect{
                box.x + (box.width - dw) * 0.5f,
                box.y + (box.height - dh) * 0.5f,
                dw,
                dh};
        }
        case Stretch::Uniform:
        default: {
            const float s = std::min(box.width / naturalW, box.height / naturalH);
            const float dw = naturalW * s;
            const float dh = naturalH * s;
            return Rect{
                box.x + (box.width - dw) * 0.5f,
                box.y + (box.height - dh) * 0.5f,
                dw,
                dh};
        }
    }
}

[[nodiscard]] Color ModulateInteractiveSurfaceColor(Color base, const UIElement* elem) noexcept {
    if (!elem) {
        return base;
    }
    if (!elem->IsEffectivelyEnabled()) {
        const float luma = 0.299f * base.r + 0.587f * base.g + 0.114f * base.b;
        return Color{
            0.5f * base.r + 0.5f * luma,
            0.5f * base.g + 0.5f * luma,
            0.5f * base.b + 0.5f * luma,
            base.a * 0.55f};
    }
    if (elem->IsPressed()) {
        return Color{
            std::clamp(base.r * 0.78f, 0.0f, 1.0f),
            std::clamp(base.g * 0.80f, 0.0f, 1.0f),
            std::clamp(base.b * 0.86f, 0.0f, 1.0f),
            base.a};
    }
    if (elem->IsHovered()) {
        return Color{
            std::clamp(base.r + 0.08f, 0.0f, 1.0f),
            std::clamp(base.g + 0.09f, 0.0f, 1.0f),
            std::clamp(base.b + 0.11f, 0.0f, 1.0f),
            base.a};
    }
    return base;
}

struct SliderVisualGeometry {
    Rect trackRect{};
    Rect fillRect{};
    Rect thumbRect{};
};

[[nodiscard]] SliderVisualGeometry ComputeSliderVisualGeometry(
    const Slider& slider,
    const Rect& r,
    float boxW,
    float boxH) noexcept {
    const Thickness pad = slider.GetPadding();
    const float ix = r.x + pad.left;
    const float iy = r.y + pad.top;
    const float iw = std::max(0.0f, boxW - (pad.left + pad.right));
    const float ih = std::max(0.0f, boxH - (pad.top + pad.bottom));
    const float t = slider.NormalizedPosition();

    SliderVisualGeometry g{};
    if (slider.GetOrientation() == Orientation::Vertical) {
        const float thumbSize = std::min(16.0f, std::max(8.0f, iw));
        const float trackThickness = std::min(6.0f, std::max(2.0f, iw * 0.3f));
        const float trackX = ix + (iw - trackThickness) * 0.5f;
        const float usableH = std::max(0.0f, ih - thumbSize);
        const float thumbY = iy + (1.0f - t) * usableH;
        const float centerThumbY = thumbY + thumbSize * 0.5f;
        g.trackRect = Rect{trackX, iy, trackThickness, ih};
        g.fillRect = Rect{trackX, centerThumbY, trackThickness, std::max(0.0f, (iy + ih) - centerThumbY)};
        g.thumbRect = Rect{ix + (iw - thumbSize) * 0.5f, thumbY, thumbSize, thumbSize};
    } else {
        const float thumbSize = std::min(16.0f, std::max(8.0f, ih));
        const float trackThickness = std::min(6.0f, std::max(2.0f, ih * 0.3f));
        const float trackY = iy + (ih - trackThickness) * 0.5f;
        const float usableW = std::max(0.0f, iw - thumbSize);
        const float thumbX = ix + t * usableW;
        const float centerThumbX = thumbX + thumbSize * 0.5f;
        g.trackRect = Rect{ix, trackY, iw, trackThickness};
        g.fillRect = Rect{ix, trackY, std::max(0.0f, centerThumbX - ix), trackThickness};
        g.thumbRect = Rect{thumbX, iy + (ih - thumbSize) * 0.5f, thumbSize, thumbSize};
    }
    return g;
}

[[nodiscard]] CornerRadius SanitizeCornerRadius(const CornerRadius& r) noexcept {
    return CornerRadius{
        std::max(0.0f, r.topLeft),
        std::max(0.0f, r.topRight),
        std::max(0.0f, r.bottomRight),
        std::max(0.0f, r.bottomLeft)};
}

[[nodiscard]] Thickness SanitizeThickness(const Thickness& t) noexcept {
    return Thickness{
        std::max(0.0f, t.left),
        std::max(0.0f, t.top),
        std::max(0.0f, t.right),
        std::max(0.0f, t.bottom)};
}

[[nodiscard]] bool HasPositiveCornerRadius(const CornerRadius& r) noexcept {
    return r.topLeft > 0.0f || r.topRight > 0.0f || r.bottomRight > 0.0f || r.bottomLeft > 0.0f;
}

[[nodiscard]] bool HasPositiveThickness(const Thickness& t) noexcept {
    return t.left > 0.0f || t.top > 0.0f || t.right > 0.0f || t.bottom > 0.0f;
}

struct SurfaceFillBorderPlan {
    Rect fillRect{};
    CornerRadius fillRadii{};
    Color fillColor{0.0f, 0.0f, 0.0f, 0.0f};
    bool hasVisibleFill = false;

    Rect borderRect{};
    CornerRadius borderRadii{};
    Thickness borderThickness{};
    Color borderColor{0.0f, 0.0f, 0.0f, 0.0f};
    bool reserveBorderNode = false;
    bool hasVisibleBorder = false;
};

[[nodiscard]] SurfaceFillBorderPlan BuildSurfaceFillBorderPlan(
    const Rect& outerRect,
    const CornerRadius& rawRadii,
    bool hasVisibleFill,
    const Color& fillColor,
    bool reserveBorderNode,
    const Color& borderColor,
    const Thickness& rawBorderThickness) noexcept {
    SurfaceFillBorderPlan plan{};
    const CornerRadius cr = SanitizeCornerRadius(rawRadii);
    const Thickness bt = SanitizeThickness(rawBorderThickness);
    const bool hasVisBorder = reserveBorderNode && (borderColor.a > 0.0f) && HasPositiveThickness(bt);

    plan.hasVisibleFill = hasVisibleFill;
    plan.fillColor = hasVisibleFill ? fillColor : Color{0.0f, 0.0f, 0.0f, 0.0f};
    plan.reserveBorderNode = reserveBorderNode;
    plan.hasVisibleBorder = hasVisBorder;
    plan.borderColor = borderColor;
    plan.borderRect = outerRect;
    plan.borderRadii = cr;
    plan.borderThickness = bt;

    if (hasVisibleFill && hasVisBorder) {
        auto computeSideInset = [&](float sideBt) noexcept -> float {
            if (sideBt <= 0.0f) {
                return 0.0f;
            }
            return (borderColor.a >= 0.999f && sideBt >= 1.0f) ? (0.5f * sideBt) : sideBt;
        };
        const float inL = computeSideInset(bt.left);
        const float inT = computeSideInset(bt.top);
        const float inR = computeSideInset(bt.right);
        const float inB = computeSideInset(bt.bottom);
        plan.fillRect = Rect{
            outerRect.x + inL,
            outerRect.y + inT,
            std::max(0.0f, outerRect.width - (inL + inR)),
            std::max(0.0f, outerRect.height - (inT + inB))};
        plan.fillRadii = CornerRadius{
            std::max(0.0f, cr.topLeft - std::max(inL, inT)),
            std::max(0.0f, cr.topRight - std::max(inR, inT)),
            std::max(0.0f, cr.bottomRight - std::max(inR, inB)),
            std::max(0.0f, cr.bottomLeft - std::max(inL, inB))};
    } else {
        plan.fillRect = outerRect;
        plan.fillRadii = cr;
    }
    return plan;
}

[[nodiscard]] std::uint32_t ExtractGradBrushId(const UnifiedValue& v) noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i > 0) {
            return static_cast<std::uint32_t>(*i);
        }
    }
    return 0u;
}

[[nodiscard]] SurfaceFillBorderPlan ComputeContainerFillBorderPlan(
    const UIElement* elem,
    const Rect& outerRect) noexcept {
    if (!elem) {
        return SurfaceFillBorderPlan{};
    }
    // Containers (Panel/Grid/StackPanel/DockPanel/WrapPanel/Canvas/ScrollViewer/ItemsControl/
    // UserControl/Window/Border) strictly use PropId_Background only — never PropId_Foreground,
    // and never fall back to an opaque default backdrop when unbacked.
    float bgOpMul = 1.0f;
    (void)TryGetFloat(elem->GetValue(PropId_BackgroundOpacity), bgOpMul);
    bgOpMul = std::clamp(bgOpMul, 0.0f, 1.0f);
    float brdOpMul = 1.0f;
    (void)TryGetFloat(elem->GetValue(PropId_BorderOpacity), brdOpMul);
    brdOpMul = std::clamp(brdOpMul, 0.0f, 1.0f);
    auto bgOpt = value::TryGetColor(elem->GetValue(PropId_Background));
    if (bgOpt) bgOpt->a = std::clamp(bgOpt->a * bgOpMul, 0.0f, 1.0f);
    const bool hasVisibleBg = bgOpt.has_value() && (bgOpt->a > 0.0f);
    const bool hasGradientBg = (ExtractGradBrushId(elem->GetValue(PropId_BackgroundGradientId)) != 0u) && (bgOpMul > 0.0f);
    const Color fillCol = hasVisibleBg ? *bgOpt : Color{0.0f, 0.0f, 0.0f, 0.0f};

    const CornerRadius cr = value::TryGetCornerRadius(elem->GetValue(PropId_CornerRadius))
        .value_or(CornerRadius{});
    const Thickness bt = value::TryGetThickness(elem->GetValue(PropId_BorderThickness))
        .value_or(Thickness{});
    auto bbOpt = value::TryGetColor(elem->GetValue(PropId_BorderBrush));
    if (bbOpt) bbOpt->a = std::clamp(bbOpt->a * brdOpMul, 0.0f, 1.0f);

    return BuildSurfaceFillBorderPlan(
        outerRect,
        cr,
        hasVisibleBg || hasGradientBg,
        fillCol,
        bbOpt.has_value(),
        bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
        bt);
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

} // namespace

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
    if (mtsdfRegisteredHashes_.find(entry.pathHash) != mtsdfRegisteredHashes_.end()) {
        return;
    }

    constexpr std::uint32_t kAtlasWidth = 1024u;
    constexpr std::uint32_t kMtsdfBaseY = 512u;
    constexpr std::uint32_t kMtsdfMaxRows = 448u; // y in [512 .. 959], leaving [960 .. 1023] for gradients
    const std::size_t totalBytes = static_cast<std::size_t>(kAtlasWidth) * kMtsdfMaxRows * 4u;
    if (mtsdfAtlasRgba8_.size() != totalBytes) {
        mtsdfAtlasRgba8_.assign(totalBytes, 0u);
    }

    const int rawOriginX = static_cast<int>(std::lround(entry.u0 * static_cast<float>(kAtlasWidth) - 0.5f));
    const int rawOriginY = static_cast<int>(std::lround(entry.v0 * static_cast<float>(kAtlasWidth) - 0.5f));
    const int rawEndX = static_cast<int>(std::lround(entry.u1 * static_cast<float>(kAtlasWidth) + 0.5f));
    const int rawEndY = static_cast<int>(std::lround(entry.v1 * static_cast<float>(kAtlasWidth) + 0.5f));

    const int dstX0 = std::clamp(rawOriginX, 0, static_cast<int>(kAtlasWidth));
    const int dstY0 = std::clamp(rawOriginY, static_cast<int>(kMtsdfBaseY), static_cast<int>(kMtsdfBaseY + kMtsdfMaxRows));
    const int dstX1 = std::clamp(std::max(dstX0, rawEndX), 0, static_cast<int>(kAtlasWidth));
    const int dstY1 = std::clamp(std::max(dstY0, rawEndY), static_cast<int>(kMtsdfBaseY), static_cast<int>(kMtsdfBaseY + kMtsdfMaxRows));

    const int dstW = dstX1 - dstX0;
    const int dstH = dstY1 - dstY0;
    if (dstW <= 0 || dstH <= 0) {
        return;
    }

    if (dstW == static_cast<int>(entry.cellWidth) && dstH == static_cast<int>(entry.cellHeight)) {
        for (int cy = 0; cy < dstH; ++cy) {
            const std::size_t localRow = static_cast<std::size_t>(dstY0 - static_cast<int>(kMtsdfBaseY) + cy);
            std::uint8_t* dstRow = mtsdfAtlasRgba8_.data() +
                (localRow * kAtlasWidth + static_cast<std::size_t>(dstX0)) * 4u;
            const std::uint8_t* srcRow = entry.mtsdfRgbaPixels.data() +
                static_cast<std::size_t>(cy) * entry.cellWidth * 4u;
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(dstW) * 4u);
        }
    } else {
        for (int cy = 0; cy < dstH; ++cy) {
            const float v = (static_cast<float>(cy) + 0.5f) / static_cast<float>(dstH) *
                static_cast<float>(entry.cellHeight) - 0.5f;
            const int sy0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(entry.cellHeight) - 1);
            const int sy1 = std::min(sy0 + 1, static_cast<int>(entry.cellHeight) - 1);
            const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);
            const std::size_t localRow = static_cast<std::size_t>(dstY0 - static_cast<int>(kMtsdfBaseY) + cy);
            for (int cx = 0; cx < dstW; ++cx) {
                const float u = (static_cast<float>(cx) + 0.5f) / static_cast<float>(dstW) *
                    static_cast<float>(entry.cellWidth) - 0.5f;
                const int sx0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(entry.cellWidth) - 1);
                const int sx1 = std::min(sx0 + 1, static_cast<int>(entry.cellWidth) - 1);
                const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
                std::uint8_t* dstPx = mtsdfAtlasRgba8_.data() +
                    (localRow * kAtlasWidth + static_cast<std::size_t>(dstX0 + cx)) * 4u;
                for (std::size_t ch = 0; ch < 4u; ++ch) {
                    const float c00 = static_cast<float>(
                        entry.mtsdfRgbaPixels[(static_cast<std::size_t>(sy0) * entry.cellWidth + sx0) * 4u + ch]);
                    const float c10 = static_cast<float>(
                        entry.mtsdfRgbaPixels[(static_cast<std::size_t>(sy0) * entry.cellWidth + sx1) * 4u + ch]);
                    const float c01 = static_cast<float>(
                        entry.mtsdfRgbaPixels[(static_cast<std::size_t>(sy1) * entry.cellWidth + sx0) * 4u + ch]);
                    const float c11 = static_cast<float>(
                        entry.mtsdfRgbaPixels[(static_cast<std::size_t>(sy1) * entry.cellWidth + sx1) * 4u + ch]);
                    const float top = c00 + (c10 - c00) * fx;
                    const float bot = c01 + (c11 - c01) * fx;
                    dstPx[ch] = static_cast<std::uint8_t>(std::clamp(top + (bot - top) * fy + 0.5f, 0.0f, 255.0f));
                }
            }
        }
    }

    mtsdfRegisteredHashes_.insert(entry.pathHash);
    const std::uint32_t endLocalRow = static_cast<std::uint32_t>(dstY1 - static_cast<int>(kMtsdfBaseY));
    mtsdfPopulatedRows_ = std::max(mtsdfPopulatedRows_, std::min(kMtsdfMaxRows, endLocalRow));
    ++mtsdfAtlasRevision_;
}

void PagedRenderTree::InitPreCachedDigitGlyphs() noexcept {
    const FontAtlas& atlas = FontAtlas::Instance();
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

GlyphUvRect PagedRenderTree::LookupCodePointGlyph(std::uint32_t codePoint) const noexcept {
    return LookupFontGlyph({}, codePoint, 14.0f);
}

GlyphUvRect PagedRenderTree::LookupFontGlyph(
    std::string_view fontFamily,
    std::uint32_t codePoint,
    float fontSize) const noexcept {
    const FontAtlasGlyph fg = FontAtlas::Instance().EnsureGlyph(fontFamily, codePoint);
    GlyphUvRect rect{};
    rect.u0 = fg.u0;
    rect.v0 = fg.v0;
    rect.u1 = fg.u1;
    rect.v1 = fg.v1;
    rect.advanceX = fg.advanceEm * std::max(1.0f, fontSize);
    rect.valid = fg.valid;
    return rect;
}

void PagedRenderTree::Clear() noexcept {
    boundRootElement_ = nullptr;
    activeClipRect_ = Rect{0.0f, 0.0f, 16384.0f, 16384.0f};
    activeClipEnabled_ = false;
    pages_.clear();
    nodes_.clear();
    pageDirtyFlags_.clear();
    dirtyPageList_.clear();
    blurLayerSpans_.clear();
    pendingComboPopups_.clear();
    activeComboPopupOwners_.clear();
    standaloneTextRuns_.clear();
    // Advance treeId_ so stale cached renderNodeId_ handles on UIElements never collide
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
    const UIElement* owner) {
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
    const UIElement* owner) {
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
    const UIElement* owner) {
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
    const UIElement* owner) {
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
    const UIElement* owner) {
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
    const UIElement* owner,
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
    const UIElement* owner) {
    RegisterMtsdfInTreeAtlas(mtsdfEntry);
    const std::uint32_t pageIdx = EnsurePageForAllocation(4u, activeClipRect_, activeClipEnabled_);
    CommandPage& page = pages_[pageIdx];
    const auto vOffset = static_cast<std::uint16_t>(page.header.vertexCount);
    page.header.vertexCount += 4u;
    page.header.nodeCount += 1u;

    const MtsdfMappedQuad mapped = ComputeMtsdfMappedPlacement(
        Rect{x, y, width, height},
        mtsdfEntry,
        strokeThickness);

    const auto nodeId = static_cast<std::uint32_t>(nodes_.size());
    RenderNodeDescriptor desc{};
    desc.nodeId = nodeId;
    desc.pageIndex = pageIdx;
    desc.vertexOffsetInPage = vOffset;
    desc.vertexCount = 4u;
    desc.primType = AeroPrimType::MsdfVectorPath;
    desc.encodedSdfParams = PackMtsdfParams(
        mtsdfEntry.pxRange,
        mtsdfEntry.atlasWidth,
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
    desc.mtsdfPathHash = mtsdfEntry.pathHash;
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
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
    const UIElement* owner) {
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
    const UIElement* owner,
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
    const UIElement* owner) {
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


namespace {

std::string ExtractContainerImageBrushUri(const UIElement* elem, Stretch* outStretch = nullptr) {
    if (outStretch != nullptr) {
        *outStretch = Stretch::Fill;
    }
    if (!elem) return {};
    auto hasImageExtCI = [](std::string_view sv) noexcept -> bool {
        const std::size_t dot = sv.rfind('.');
        if (dot == std::string_view::npos || dot + 1u >= sv.size()) {
            return false;
        }
        std::string_view ext = sv.substr(dot + 1u);
        const std::size_t q = ext.find_first_of("?#");
        if (q != std::string_view::npos) {
            ext = ext.substr(0u, q);
        }
        auto eqCI = [](std::string_view a, std::string_view b) noexcept -> bool {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0u; i < a.size(); ++i) {
                const char ca = (a[i] >= 'A' && a[i] <= 'Z')
                    ? static_cast<char>(a[i] - 'A' + 'a')
                    : a[i];
                if (ca != b[i]) return false;
            }
            return true;
        };
        return eqCI(ext, "png") || eqCI(ext, "jpg") || eqCI(ext, "jpeg") ||
               eqCI(ext, "bmp") || eqCI(ext, "tga") || eqCI(ext, "webp");
    };
    for (const std::uint16_t pid : {PropId_ImageSource, PropId_Background}) {
        const UnifiedValue v = elem->GetValue(pid);
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            const std::string_view raw = sv->view();
            if (raw.empty()) continue;
            if (raw.front() == '#' || raw.rfind("AERO_GRAD:", 0) == 0) {
                continue;
            }
            if (raw.size() > 16u && raw.rfind("AERO_IMGBRUSH:", 0) == 0 &&
                raw[15] == ':' && raw[14] >= '0' && raw[14] <= '3') {
                if (outStretch != nullptr) {
                    *outStretch = static_cast<Stretch>(raw[14] - '0');
                }
                return std::string(raw.substr(16u));
            }
            if (hasImageExtCI(raw)) {
                return std::string(raw);
            }
        }
    }
    return {};
}

void ParseRichTextToPlainAndStyles(
    std::string_view rawBbCode,
    const Color& baseColor,
    const UIElement* owner,
    std::string& outPlainUtf8,
    std::vector<RichGlyphStyle>& outCodePointStyles) {
    outPlainUtf8.clear();
    outCodePointStyles.clear();

    struct StyleFrame {
        Color color;
        bool hasCustomColor = false;
        bool bold = false;
        bool italic = false;
    };
    std::vector<StyleFrame> stack;
    stack.push_back(StyleFrame{baseColor, false, false, false});

    auto appendPlainSegment = [&](std::string_view seg) {
        const StyleFrame& cur = stack.back();
        outPlainUtf8.append(seg);
        std::size_t k = 0u;
        std::uint32_t dummyCp = 0u;
        while (DecodeNextUtf8CodePoint(seg, k, dummyCp)) {
            outCodePointStyles.push_back(RichGlyphStyle{
                cur.color, cur.hasCustomColor, cur.bold, cur.italic});
        }
    };

    std::size_t i = 0u;
    while (i < rawBbCode.size()) {
        if (rawBbCode[i] == '[') {
            const std::size_t closeBracket = rawBbCode.find(']', i + 1u);
            if (closeBracket != std::string_view::npos) {
                const std::string_view tagContent = rawBbCode.substr(i + 1u, closeBracket - i - 1u);
                if (tagContent == "br" || tagContent == "br/") {
                    appendPlainSegment("\n");
                    i = closeBracket + 1u;
                    continue;
                }
                if (tagContent.rfind("bind", 0) == 0 &&
                    (tagContent.size() == 4u || tagContent[4] == ' ' || tagContent[4] == '=')) {
                    const std::size_t endBind = rawBbCode.find("[/bind]", closeBracket + 1u);
                    if (endBind != std::string_view::npos) {
                        const std::string resolved = StripAndResolveRichTextBbCode(
                            rawBbCode.substr(i, (endBind + 7u) - i),
                            owner);
                        appendPlainSegment(resolved);
                        i = endBind + 7u;
                        continue;
                    }
                }
                if (tagContent.rfind("img", 0) == 0) {
                    const std::size_t endImg = rawBbCode.find("[/img]", closeBracket + 1u);
                    if (endImg != std::string_view::npos) {
                        i = endImg + 6u;
                        continue;
                    }
                }
                const bool isClosing = !tagContent.empty() && tagContent.front() == '/';
                const std::string_view bare = isClosing ? tagContent.substr(1u) : tagContent;
                const std::size_t eqPos = bare.find('=');
                const std::string_view tagName = (eqPos != std::string_view::npos) ? bare.substr(0u, eqPos) : bare;
                std::string_view tagArg = (eqPos != std::string_view::npos) ? bare.substr(eqPos + 1u) : std::string_view{};
                if (tagArg.size() >= 2u &&
                    ((tagArg.front() == '\'' && tagArg.back() == '\'') ||
                     (tagArg.front() == '"' && tagArg.back() == '"'))) {
                    tagArg = tagArg.substr(1u, tagArg.size() - 2u);
                }
                if (tagName == "b" || tagName == "i" || tagName == "u" || tagName == "s" ||
                    tagName == "style" || tagName == "color" || tagName == "size" || tagName == "url") {
                    if (isClosing) {
                        if (stack.size() > 1u) stack.pop_back();
                    } else {
                        StyleFrame next = stack.back();
                        if (tagName == "b") {
                            next.bold = true;
                        } else if (tagName == "i") {
                            next.italic = true;
                        } else if (tagName == "color" && !tagArg.empty()) {
                            next.color = ParseXamlColorLiteral(tagArg);
                            next.hasCustomColor = true;
                        } else if (tagName == "style" && !tagArg.empty()) {
                            bool matchedStyle = false;
                            if (const AxbStyleDefinition* st = AxbTemplateRegistry::Instance().FindStyle(tagArg)) {
                                for (const auto& setter : st->setters) {
                                    if (setter.propId == PropId_Foreground) {
                                        if (auto cOpt = value::TryGetColor(setter.value)) {
                                            next.color = *cOpt;
                                            next.hasCustomColor = true;
                                            matchedStyle = true;
                                        }
                                        if (tagArg == "ColoredText") next.bold = true;
                                    }
                                }
                            }
                            if (!matchedStyle && tagArg == "ColoredText") {
                                next.color = Color{0.165f, 0.651f, 0.886f, 1.0f}; // #FF2AA6E2
                                next.hasCustomColor = true;
                                next.bold = true;
                            }
                        }
                        stack.push_back(next);
                    }
                    i = closeBracket + 1u;
                    continue;
                }
            }
        }
        std::size_t nextBracket = rawBbCode.find('[', i + 1u);
        if (nextBracket == std::string_view::npos) nextBracket = rawBbCode.size();
        appendPlainSegment(rawBbCode.substr(i, nextBracket - i));
        i = nextBracket;
    }
}

} // namespace

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

    const TextLayoutResult layout = MeasureTextLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
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
    const UIElement* owner,
    std::string_view fontFamily,
    float maxWidth,
    TextWrapping wrapMode,
    TextAlignment textAlign,
    float alignBoxWidth,
    const std::vector<RichGlyphStyle>* optRichStyles) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    TextLayoutResult layout = MeasureTextLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
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
    const RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.sourceElement != nullptr) {
        if (UpdateDirtyElement(node.sourceElement)) {
            return true;
        }
    }
    if (const auto it = standaloneTextRuns_.find(nodeId); it != standaloneTextRuns_.end()) {
        const StandaloneTextRunRecord& rec = it->second;
        const TextLayoutResult layout = MeasureTextLayout(
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
    const UIElement* lastElement = nullptr;

    for (std::uint32_t nid = 0u; nid < static_cast<std::uint32_t>(nodes_.size()); ++nid) {
        const RenderNodeDescriptor& nd = nodes_[nid];
        if (nd.primType != AeroPrimType::SdfTextGlyph) {
            continue;
        }
        if (nd.sourceElement != nullptr) {
            if (nd.sourceElement == lastElement) {
                continue;
            }
            std::string elemText;
            if (const auto* tb = nd.sourceElement->DynamicCast<TextBlock>()) {
                elemText = tb->GetText();
                if (elemText.empty()) {
                    const UnifiedValue phVal = tb->GetValue(PropId_Placeholder);
                    if (const auto* sv = std::get_if<StringValue>(&phVal)) {
                        elemText = sv->str();
                    }
                }
            } else if (const auto* combo = nd.sourceElement->DynamicCast<ComboBox>()) {
                elemText = combo->GetSelectedItemText();
            } else if (const auto* btn = nd.sourceElement->DynamicCast<Button>()) {
                elemText = btn->GetContentText();
            }
            if (TextContainsCodePointInRange(elemText, rangeStart, rangeEnd)) {
                lastElement = nd.sourceElement;
                if (UpdateDirtyElement(nd.sourceElement)) {
                    ++refreshed;
                }
            }
        } else if (const auto it = standaloneTextRuns_.find(nid); it != standaloneTextRuns_.end()) {
            if (TextContainsCodePointInRange(it->second.utf8Text, rangeStart, rangeEnd)) {
                if (RefreshTextNode(nid)) {
                    ++refreshed;
                }
            }
        }
    }
    return refreshed;
}

bool PagedRenderTree::UpdateDirtyElement(const UIElement* element) noexcept {
    if (!element) {
        return false;
    }

    try {
        // Reconstruct ancestor state along parent chain in O(depth) with 0 heap allocations
        const UIElement* chain[64]{};
        std::size_t depth = 0;
        for (const UIElement* anc = element->Parent(); anc != nullptr && depth < 64u; anc = anc->Parent()) {
            chain[depth++] = anc;
        }

        RenderTraversalState parentState{};
        bool ancestorHasDropShadow = false;
        for (std::size_t idx = depth; idx > 0u; --idx) {
            const UIElement* anc = chain[idx - 1u];
            if (anc->GetEffectDescriptor().kind == UIEffectKind::DropShadow) {
                ancestorHasDropShadow = true;
            }
            const Visibility ancVis = anc->GetVisibility();
            if (ancVis != Visibility::Visible) {
                parentState.inheritedOpacity = 0.0f;
            } else {
                parentState.inheritedOpacity *= std::clamp(anc->GetOpacity(), 0.0f, 1.0f);
            }
            const Rect ar = anc->FinalRect();
            const float aw = (ar.width > 0.0f) ? ar.width : std::max(0.0f, anc->GetWidth());
            const float ah = (ar.height > 0.0f) ? ar.height : std::max(0.0f, anc->GetHeight());
            const Matrix2D ancLocal2D = anc->ComputeLocalEffectiveTransform2D();
            ApplyStateMatrix(
                parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty,
                ancLocal2D);
            if (anc->HasActiveTransform3D()) {
                parentState.world3D = Matrix4x4::ComposePlanarHomography(
                    parentState.world3D,
                    anc->ComputeLocalEffectiveTransform3D());
                parentState.hasPerspective3D = true;
            } else if (parentState.hasPerspective3D) {
                if (!ancLocal2D.IsIdentity()) {
                    parentState.world3D = Matrix4x4::ComposePlanarHomography(
                        parentState.world3D,
                        Matrix4x4::FromMatrix2D(ancLocal2D));
                }
            } else {
                parentState.world3D = Matrix4x4::FromMatrix2D(
                    MatrixFromState(
                        parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty));
            }

            const UnifiedValue clipVal = anc->GetValue(PropId_ClipToBounds);
            if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
                const Matrix2D ancWorldM = MatrixFromState(
                    parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty);
                Rect rawClip = anc->GetLocalClipBounds();
                if (ar.width <= 0.0f && ar.height <= 0.0f && (aw > 0.0f || ah > 0.0f)) {
                    rawClip = Rect{ar.x, ar.y, aw, ah};
                }
                const Rect bounds = ancWorldM.TransformRectBounds(rawClip);
                parentState.clipRect = parentState.clipActive ? IntersectRect(parentState.clipRect, bounds) : bounds;
                parentState.clipActive = true;
            }

            const Matrix2D childContainer2D = anc->GetChildContainerTransform2D();
            ApplyStateMatrix(
                parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty,
                childContainer2D);
            if (parentState.hasPerspective3D) {
                if (!childContainer2D.IsIdentity()) {
                    parentState.world3D = Matrix4x4::ComposePlanarHomography(
                        parentState.world3D,
                        Matrix4x4::FromMatrix2D(childContainer2D));
                }
            } else {
                parentState.world3D = Matrix4x4::FromMatrix2D(
                    MatrixFromState(
                        parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty));
            }
        }

        RenderTraversalState curState = parentState;
        const Visibility vis = element->GetVisibility();
        if (vis != Visibility::Visible) {
            curState.inheritedOpacity = 0.0f;
        } else {
            curState.inheritedOpacity *= std::clamp(element->GetOpacity(), 0.0f, 1.0f);
        }
        const Matrix2D elemLocal2D = element->ComputeLocalEffectiveTransform2D();
        ApplyStateMatrix(
            curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty,
            elemLocal2D);
        if (element->HasActiveTransform3D()) {
            curState.world3D = Matrix4x4::ComposePlanarHomography(
                curState.world3D,
                element->ComputeLocalEffectiveTransform3D());
            curState.hasPerspective3D = true;
        } else if (curState.hasPerspective3D) {
            if (!elemLocal2D.IsIdentity()) {
                curState.world3D = Matrix4x4::ComposePlanarHomography(
                    curState.world3D,
                    Matrix4x4::FromMatrix2D(elemLocal2D));
            }
        } else {
            curState.world3D = Matrix4x4::FromMatrix2D(
                MatrixFromState(curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty));
        }

        if (!ancestorHasDropShadow && UpdateElementInPlace(element, curState)) {
            return true;
        }

        // Element grew beyond its in-page span capacity or structurally changed -> rebuild cleanly
        // to preserve 100% exact Painter's Algorithm Z-order with zero leaked dead nodes.
        const UIElement* rebuildRoot = (boundRootElement_ != nullptr)
            ? boundRootElement_
            : ((depth > 0u) ? chain[depth - 1u] : element);
        if (rebuildRoot != nullptr && !nodes_.empty()) {
            (void)BuildFromVisualTree(rebuildRoot);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool PagedRenderTree::UpdateElementInPlace(
    const UIElement* element,
    const RenderTraversalState& curState) {
    if (!element) {
        return false;
    }

    // O(1) direct span lookup via UIElement::RenderNodeId(), with O(N) fallback only if unbound
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (element->RenderTreeId() == treeId_ &&
        element->RenderNodeId() < nodes_.size() &&
        element->RenderNodeSpan() > 0u &&
        nodes_[element->RenderNodeId()].sourceElement == element) {
        firstId = element->RenderNodeId();
        span = element->RenderNodeSpan();
    } else {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(nodes_.size()); ++i) {
            if (nodes_[i].sourceElement == element) {
                firstId = i;
                span = 1u;
                while (firstId + span < nodes_.size() &&
                       nodes_[firstId + span].sourceElement == element) {
                    ++span;
                }
                element->BindRenderNodeSpan(treeId_, firstId, span);
                break;
            }
        }
    }

    if (firstId == UIElement::InvalidRenderNodeId || span == 0u) {
        return false;
    }
    if (element->GetEffectDescriptor().kind != UIEffectKind::None ||
        element->IsA<BoxShadow>() ||
        element->FindCompactEntry(PropId_BlendingMode) != nullptr) {
        return false;
    }

    const Visibility vis = element->GetVisibility();
    const Rect r = element->FinalRect();
    const float boxW = (vis == Visibility::Collapsed)
        ? 0.0f
        : ((r.width > 0.0f) ? r.width : std::max(0.0f, element->GetWidth()));
    const float boxH = (vis == Visibility::Collapsed)
        ? 0.0f
        : ((r.height > 0.0f) ? r.height : std::max(0.0f, element->GetHeight()));
    const float effOpacity = (vis == Visibility::Visible) ? curState.inheritedOpacity : 0.0f;

    auto syncQuadTransform = [&](RenderNodeDescriptor& nd) noexcept {
        nd.m00 = curState.m00;
        nd.m01 = curState.m01;
        nd.m10 = curState.m10;
        nd.m11 = curState.m11;
        nd.baseTx = curState.tx;
        nd.baseTy = curState.ty;
        nd.tx = curState.tx + nd.x * (curState.m00 - 1.0f) + nd.y * curState.m01;
        nd.ty = curState.ty + nd.x * curState.m10 + nd.y * (curState.m11 - 1.0f);
        nd.inheritedOpacity = curState.inheritedOpacity;
        nd.hasPerspective3D = curState.hasPerspective3D;
        nd.worldMatrix3D = curState.world3D;
    };

    // Specialized Path update
    if (element->HasControlTemplate()) {
        if (span != 1u) {
            return false;
        }
        RenderNodeDescriptor& anchorNode = nodes_[firstId];
        anchorNode.x = r.x;
        anchorNode.y = r.y;
        anchorNode.width = 0.0f;
        anchorNode.height = 0.0f;
        anchorNode.opacity = 0.0f;
        anchorNode.baseRgba = 0u;
        ConfigureRoundedRectDescriptor(anchorNode, CornerRadius{}, Thickness{}, false);
        syncQuadTransform(anchorNode);
        WriteQuadVertices(anchorNode);
        MarkPageDirty(anchorNode.pageIndex, anchorNode.vertexOffsetInPage);
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    if (const auto* cp = element->DynamicCast<ContentPresenter>()) {
        if (span < 1u) {
            return false;
        }
        if (cp->FirstChild() != nullptr) {
            if (span != 1u) {
                return false;
            }
            RenderNodeDescriptor& anchorNode = nodes_[firstId];
            anchorNode.x = r.x;
            anchorNode.y = r.y;
            anchorNode.width = 0.0f;
            anchorNode.height = 0.0f;
            anchorNode.opacity = 0.0f;
            anchorNode.baseRgba = 0u;
            ConfigureRoundedRectDescriptor(anchorNode, CornerRadius{}, Thickness{}, false);
            syncQuadTransform(anchorNode);
            WriteQuadVertices(anchorNode);
            MarkPageDirty(anchorNode.pageIndex, anchorNode.vertexOffsetInPage);
            element->ClearLayoutDirtyFlags(RenderDirty);
            return true;
        }

        const std::string txt = cp->GetEffectiveText();
        const std::string_view activeText = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{txt};
        const Color fgColor = cp->GetEffectiveForeground();
        const float fontSize = cp->GetEffectiveFontSize();
        const std::string fontFamily = cp->GetEffectiveFontFamily();
        const TextWrapping wrapMode = cp->GetEffectiveTextWrapping();
        const Thickness pad = cp->GetPadding();
        const float effBoxW = (boxW > 0.0f) ? boxW : cp->DesiredSize().width;
        const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
            ? std::max(1.0f, effBoxW - (pad.left + pad.right))
            : 1000000.0f;
        const TextLayoutResult cpLayout = MeasureTextLayout(
            activeText, fontFamily, fontSize, maxTextW, wrapMode);
        const Point textOrigin = cp->ComputeTextOrigin(
            Rect{r.x, r.y, boxW, boxH},
            Size{cpLayout.totalWidth, cpLayout.totalHeight});

        std::size_t totalGlyphCap = 0u;
        for (std::uint16_t k = 1u; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                totalGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }

        if (CountLayoutVisibleGlyphs(cpLayout) > totalGlyphCap) {
            return false;
        }

        {
            RenderNodeDescriptor& anchorNode = nodes_[firstId];
            anchorNode.x = r.x;
            anchorNode.y = r.y;
            anchorNode.width = 0.0f;
            anchorNode.height = 0.0f;
            anchorNode.opacity = 0.0f;
            anchorNode.baseRgba = 0u;
            ConfigureRoundedRectDescriptor(anchorNode, CornerRadius{}, Thickness{}, false);
            syncQuadTransform(anchorNode);
            WriteQuadVertices(anchorNode);
            MarkPageDirty(anchorNode.pageIndex, anchorNode.vertexOffsetInPage);
        }

        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = 1u; k < span; ++k) {
            RenderNodeDescriptor& node = nodes_[firstId + k];
            node.m00 = curState.m00;
            node.m01 = curState.m01;
            node.m10 = curState.m10;
            node.m11 = curState.m11;
            node.baseTx = curState.tx;
            node.baseTy = curState.ty;
            node.inheritedOpacity = curState.inheritedOpacity;
            node.hasPerspective3D = curState.hasPerspective3D;
            node.worldMatrix3D = curState.world3D;
            (void)WriteLayoutGlyphSlice(
                firstId + k,
                textOrigin.x,
                textOrigin.y,
                fontSize,
                cpLayout,
                glyphCursor,
                fgColor,
                effOpacity);
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    if (const auto* pathElem = element->DynamicCast<Path>()) {
        const std::string pathStr = pathElem->GetData();
        const AxbMtsdfPathEntry* entry = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr);
        if (!entry || vis == Visibility::Collapsed) {
            for (std::uint16_t k = 0; k < span; ++k) {
                RenderNodeDescriptor& nd = nodes_[firstId + k];
                nd.width = 0.0f;
                nd.height = 0.0f;
                nd.opacity = 0.0f;
                syncQuadTransform(nd);
                WriteQuadVertices(nd);
                MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
            }
            element->ClearLayoutDirtyFlags(RenderDirty);
            return true;
        }

        RegisterMtsdfInTreeAtlas(*entry);

        bool emitFill = true;
        bool emitSeparateStroke = false;
        ClassifyPathEmission(*pathElem, emitFill, emitSeparateStroke);
        const std::uint16_t neededNodes = std::max<std::uint16_t>(
            1u,
            static_cast<std::uint16_t>((emitFill ? 1u : 0u) + (emitSeparateStroke ? 1u : 0u)));
        if (neededNodes != span) {
            // Structural transition between 1-node and 2-node fill+stroke Path -> rebuild cleanly
            return false;
        }
        if (!emitFill && !emitSeparateStroke) {
            RenderNodeDescriptor& nd = nodes_[firstId];
            nd.x = r.x;
            nd.y = r.y;
            nd.width = 0.0f;
            nd.height = 0.0f;
            nd.opacity = 0.0f;
            syncQuadTransform(nd);
            WriteQuadVertices(nd);
            MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
            element->ClearLayoutDirtyFlags(RenderDirty);
            return true;
        }

        Rect fitted = ComputeStretchedDestRect(
            Rect{r.x, r.y, boxW, boxH},
            entry->boundsW,
            entry->boundsH,
            pathElem->GetStretch());
        if (pathElem->GetStretch() == Stretch::None) {
            fitted.x += entry->boundsX;
            fitted.y += entry->boundsY;
        }
        const float strokeW = pathElem->GetStrokeThickness();
        const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
        const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
        const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
        const std::uint32_t fillGradId = ExtractGradBrushId(pathElem->GetValue(PropId_BackgroundGradientId));
        const std::uint32_t strokeGradId = ExtractGradBrushId(pathElem->GetValue(PropId_BorderGradientId));

        std::uint16_t k = 0;
        if (emitFill) {
            const float inlineStrokeW = emitSeparateStroke ? 0.0f : strokeW;
            const MtsdfMappedQuad fillMapped = ComputeMtsdfMappedPlacement(fitted, *entry, inlineStrokeW);
            RenderNodeDescriptor& nd = nodes_[firstId + k];
            nd.x = fillMapped.quadRect.x;
            nd.y = fillMapped.quadRect.y;
            nd.width = fillMapped.quadRect.width;
            nd.height = fillMapped.quadRect.height;
            nd.opacity = effOpacity;
            nd.primType = AeroPrimType::MsdfVectorPath;
            nd.uv0X = fillMapped.u0;
            nd.uv0Y = fillMapped.v0;
            nd.uv1X = fillMapped.u1;
            nd.uv1Y = fillMapped.v1;
            nd.borderThickness = inlineStrokeW;
            nd.gradientBrushId = fillGradId;
            nd.encodedPrimExtra = (fillGradId != 0u)
                ? static_cast<std::uint16_t>((std::min<std::uint32_t>(fillGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                : 0u;
            nd.encodedSdfParams = PackMtsdfParams(
                entry->pxRange,
                entry->atlasWidth,
                inlineStrokeW * worldStrokeScale,
                false);
            nd.baseRgba = PackColorRgba8(pathElem->GetFill(), 1.0f);
            nd.mtsdfPathHash = entry->pathHash;
            syncQuadTransform(nd);
            WriteQuadVertices(nd);
            MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
            ++k;
        }
        if (emitSeparateStroke && k < span) {
            const MtsdfMappedQuad strokeMapped = ComputeMtsdfMappedPlacement(fitted, *entry, strokeW);
            RenderNodeDescriptor& nd = nodes_[firstId + k];
            nd.x = strokeMapped.quadRect.x;
            nd.y = strokeMapped.quadRect.y;
            nd.width = strokeMapped.quadRect.width;
            nd.height = strokeMapped.quadRect.height;
            nd.opacity = effOpacity;
            nd.primType = AeroPrimType::MsdfVectorPath;
            nd.uv0X = strokeMapped.u0;
            nd.uv0Y = strokeMapped.v0;
            nd.uv1X = strokeMapped.u1;
            nd.uv1Y = strokeMapped.v1;
            nd.borderThickness = strokeW;
            nd.gradientBrushId = strokeGradId;
            nd.encodedPrimExtra = (strokeGradId != 0u)
                ? static_cast<std::uint16_t>((std::min<std::uint32_t>(strokeGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                : 0u;
            nd.encodedSdfParams = PackMtsdfParams(
                entry->pxRange,
                entry->atlasWidth,
                strokeW * worldStrokeScale,
                true);
            nd.baseRgba = PackColorRgba8(pathElem->GetStroke(), 1.0f);
            nd.mtsdfPathHash = entry->pathHash;
            syncQuadTransform(nd);
            WriteQuadVertices(nd);
            MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized Slider update (constant 3-node span: track + active fill + thumb -> O(1) drag updates!)
    if (const auto* slider = element->DynamicCast<Slider>()) {
        if (span != 3u) {
            return false;
        }
        const SliderVisualGeometry geom = ComputeSliderVisualGeometry(*slider, r, boxW, boxH);
        const float effAlpha = slider->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);

        Color trackColor = value::TryGetColor(slider->GetValue(PropId_Background))
            .value_or(Color{0.16f, 0.20f, 0.26f, 1.0f});
        Color fillColor = value::TryGetColor(slider->GetValue(PropId_Foreground))
            .value_or(Color{0.24f, 0.56f, 0.96f, 1.0f});
        Color thumbColor = ModulateInteractiveSurfaceColor(
            Color{0.92f, 0.96f, 1.0f, 1.0f},
            slider);
        const float thumbBorder = slider->IsFocused() ? 2.0f : 1.0f;

        auto updateSliderQuad = [&](std::uint32_t nid, const Rect& qr, const Color& col, float cr, float bt) {
            RenderNodeDescriptor& nd = nodes_[nid];
            nd.x = qr.x;
            nd.y = qr.y;
            nd.width = (vis == Visibility::Collapsed) ? 0.0f : qr.width;
            nd.height = (vis == Visibility::Collapsed) ? 0.0f : qr.height;
            nd.opacity = effAlpha;
            ConfigureRoundedRectDescriptor(nd, CornerRadius{cr}, Thickness{bt}, false);
            nd.baseRgba = PackColorRgba8(col, 1.0f);
            syncQuadTransform(nd);
            WriteQuadVertices(nd);
            MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
        };

        updateSliderQuad(firstId + 0u, geom.trackRect, trackColor, 3.0f, 0.0f);
        updateSliderQuad(firstId + 1u, geom.fillRect, fillColor, 3.0f, 0.0f);
        updateSliderQuad(firstId + 2u, geom.thumbRect, thumbColor, geom.thumbRect.width * 0.5f, thumbBorder);
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized TextBox / PasswordBox update (box quad + optional border ring + 1..N SdfTextGlyph nodes + caret quad)
    if (const auto* tbox = element->DynamicCast<TextBox>()) {
        if (span < 3u) {
            return false;
        }
        const bool hasSeparateBorderNode =
            (span >= 4u) && (nodes_[firstId + 1u].primType != AeroPrimType::SdfTextGlyph);
        const std::uint16_t boxSpan = hasSeparateBorderNode ? 2u : 1u;
        const auto bbOpt = value::TryGetColor(tbox->GetValue(PropId_BorderBrush));
        if (bbOpt.has_value() && !hasSeparateBorderNode) {
            return false;
        }

        const std::string displayStr = tbox->GetDisplayString();
        const bool showingPlaceholder = displayStr.empty();
        const std::string renderedText = showingPlaceholder ? tbox->GetPlaceholder() : displayStr;
        const std::string_view activeText = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{renderedText};

        std::size_t totalGlyphCap = 0u;
        for (std::uint16_t k = boxSpan; k + 1u < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                totalGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }
        if (CountVisibleUtf8Glyphs(activeText) > totalGlyphCap) {
            return false;
        }

        const float effAlpha = tbox->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color boxBg = value::TryGetColor(tbox->GetValue(PropId_Background))
            .value_or(Color{0.12f, 0.15f, 0.20f, 1.0f});
        boxBg = ModulateInteractiveSurfaceColor(boxBg, tbox);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(tbox->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        Thickness btRaw = SanitizeThickness(
            value::TryGetThickness(tbox->GetValue(PropId_BorderThickness))
                .value_or(Thickness{1.0f}));
        if (tbox->IsFocused()) {
            btRaw = Thickness{
                std::max(2.0f, btRaw.left),
                std::max(2.0f, btRaw.top),
                std::max(2.0f, btRaw.right),
                std::max(2.0f, btRaw.bottom)};
        }

        if (hasSeparateBorderNode) {
            const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
                Rect{r.x, r.y, boxW, boxH},
                cr,
                boxBg.a > 0.0f,
                boxBg,
                bbOpt.has_value(),
                bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
                btRaw);
            RenderNodeDescriptor& fillNode = nodes_[firstId];
            fillNode.x = plan.fillRect.x;
            fillNode.y = plan.fillRect.y;
            fillNode.width = plan.fillRect.width;
            fillNode.height = plan.fillRect.height;
            fillNode.opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
            fillNode.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
            ConfigureRoundedRectDescriptor(fillNode, plan.fillRadii, Thickness{}, false);
            syncQuadTransform(fillNode);
            WriteQuadVertices(fillNode);
            MarkPageDirty(fillNode.pageIndex, fillNode.vertexOffsetInPage);

            RenderNodeDescriptor& borderNode = nodes_[firstId + 1u];
            borderNode.x = plan.borderRect.x;
            borderNode.y = plan.borderRect.y;
            borderNode.width = plan.hasVisibleBorder ? plan.borderRect.width : 0.0f;
            borderNode.height = plan.hasVisibleBorder ? plan.borderRect.height : 0.0f;
            borderNode.opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
            borderNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
            ConfigureRoundedRectDescriptor(borderNode, plan.borderRadii, plan.borderThickness, true);
            syncQuadTransform(borderNode);
            WriteQuadVertices(borderNode);
            MarkPageDirty(borderNode.pageIndex, borderNode.vertexOffsetInPage);
        } else {
            RenderNodeDescriptor& boxNode = nodes_[firstId];
            boxNode.x = r.x;
            boxNode.y = r.y;
            boxNode.width = boxW;
            boxNode.height = boxH;
            boxNode.opacity = (boxBg.a > 0.0f) ? effAlpha : 0.0f;
            boxNode.baseRgba = PackColorRgba8(boxBg, 1.0f);
            const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
            ConfigureRoundedRectDescriptor(boxNode, cr, Thickness{legacyBt}, false);
            syncQuadTransform(boxNode);
            WriteQuadVertices(boxNode);
            MarkPageDirty(boxNode.pageIndex, boxNode.vertexOffsetInPage);
        }

        // Nodes [boxSpan .. span-2]: UTF-8 text or muted placeholder
        const Thickness pad = tbox->GetPadding();
        const float padLeft = (pad.left > 0.0f) ? pad.left : 6.0f;
        const float scrollX = showingPlaceholder ? 0.0f : tbox->HorizontalTextScrollOffset();
        const float textX = r.x + padLeft - scrollX;
        const float fontSize = tbox->GetFontSize();
        const float textY = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
        const Color textColor = showingPlaceholder
            ? Color{0.55f, 0.60f, 0.68f, 0.85f}
            : tbox->GetForeground();

        const TextLayoutResult tboxLayout = MeasureTextLayout(
            activeText, tbox->GetFontFamily(), fontSize, 1000000.0f, TextWrapping::NoWrap);
        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = boxSpan; k + 1u < span; ++k) {
            RenderNodeDescriptor& tNode = nodes_[firstId + k];
            tNode.m00 = curState.m00;
            tNode.m01 = curState.m01;
            tNode.m10 = curState.m10;
            tNode.m11 = curState.m11;
            tNode.baseTx = curState.tx;
            tNode.baseTy = curState.ty;
            tNode.inheritedOpacity = curState.inheritedOpacity;
            tNode.hasPerspective3D = curState.hasPerspective3D;
            tNode.worldMatrix3D = curState.world3D;
            (void)WriteLayoutGlyphSlice(
                firstId + k,
                textX,
                textY,
                fontSize,
                tboxLayout,
                glyphCursor,
                textColor,
                effAlpha);
        }

        // Node [span-1]: Caret quad (visible when focused & editable)
        {
            const Rect caretR = tbox->CaretLocalRect();
            const bool showCaret = (vis == Visibility::Visible) && tbox->IsFocused() &&
                                   !tbox->IsReadOnly() && tbox->IsEffectivelyEnabled();
            RenderNodeDescriptor& caretNode = nodes_[firstId + span - 1u];
            caretNode.x = caretR.x;
            caretNode.y = caretR.y;
            caretNode.width = showCaret ? caretR.width : 0.0f;
            caretNode.height = showCaret ? caretR.height : 0.0f;
            caretNode.opacity = showCaret ? effAlpha : 0.0f;
            ConfigureRoundedRectDescriptor(caretNode, CornerRadius{}, Thickness{}, false);
            caretNode.baseRgba = PackColorRgba8(Color{0.38f, 0.76f, 1.0f, 1.0f}, 1.0f);
            syncQuadTransform(caretNode);
            WriteQuadVertices(caretNode);
            MarkPageDirty(caretNode.pageIndex, caretNode.vertexOffsetInPage);
        }

        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // P1-3 shapes, composite controls & gradient-backed elements rebuild cleanly to preserve exact multi-node spans & Z-order
    if (element->IsA<Rectangle>() || element->IsA<Ellipse>() || element->IsA<Line>() ||
        element->IsA<Polygon>() || element->IsA<Polyline>() ||
        element->IsA<ProgressBar>() || element->IsA<HeaderedContentControl>() ||
        element->IsA<TabControl>() ||
        ExtractGradBrushId(element->GetValue(PropId_BackgroundGradientId)) != 0u ||
        ExtractGradBrushId(element->GetValue(PropId_ForegroundGradientId)) != 0u ||
        ExtractGradBrushId(element->GetValue(PropId_BorderGradientId)) != 0u) {
        return false;
    }

    // Specialized ComboBox update: rebuild cleanly whenever popup is open or closing so top-layer overlay syncs
    if (const auto* combo = element->DynamicCast<ComboBox>()) {
        const bool hadActivePopup = std::find(
            activeComboPopupOwners_.begin(),
            activeComboPopupOwners_.end(),
            element) != activeComboPopupOwners_.end();
        if (combo->IsDropDownOpen() || hadActivePopup) {
            return false;
        }
        // Closed ComboBox header in-place update (header box + chevron + text run)
        if (span < 3u) {
            return false;
        }
        const std::string selText = combo->GetSelectedItemText();
        const std::string_view activeSel = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{selText};
        std::size_t glyphCap = 0u;
        for (std::uint16_t k = 2u; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                glyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }
        if (CountVisibleUtf8Glyphs(activeSel) > glyphCap) {
            return false;
        }

        const float effAlpha = combo->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color hdrBg = value::TryGetColor(combo->GetValue(PropId_Background))
            .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
        hdrBg = ModulateInteractiveSurfaceColor(hdrBg, combo);
        const float bt = combo->IsFocused() ? 2.0f : 1.0f;

        RenderNodeDescriptor& hdrNode = nodes_[firstId];
        hdrNode.x = r.x;
        hdrNode.y = r.y;
        hdrNode.width = boxW;
        hdrNode.height = boxH;
        hdrNode.opacity = effAlpha;
        ConfigureRoundedRectDescriptor(hdrNode, CornerRadius{4.0f}, Thickness{bt}, false);
        hdrNode.baseRgba = PackColorRgba8(hdrBg, 1.0f);
        syncQuadTransform(hdrNode);
        WriteQuadVertices(hdrNode);
        MarkPageDirty(hdrNode.pageIndex, hdrNode.vertexOffsetInPage);

        RenderNodeDescriptor& chevNode = nodes_[firstId + 1u];
        chevNode.x = r.x + std::max(0.0f, boxW - 18.0f);
        chevNode.y = r.y + std::max(0.0f, (boxH - 6.0f) * 0.5f);
        chevNode.width = (vis == Visibility::Collapsed) ? 0.0f : 10.0f;
        chevNode.height = (vis == Visibility::Collapsed) ? 0.0f : 6.0f;
        chevNode.opacity = effAlpha;
        ConfigureRoundedRectDescriptor(chevNode, CornerRadius{2.0f}, Thickness{}, false);
        chevNode.baseRgba = PackColorRgba8(Color{0.78f, 0.86f, 0.98f, 1.0f}, 1.0f);
        syncQuadTransform(chevNode);
        WriteQuadVertices(chevNode);
        MarkPageDirty(chevNode.pageIndex, chevNode.vertexOffsetInPage);

        float fontSize = 14.0f;
        (void)TryGetFloat(combo->GetValue(PropId_FontSize), fontSize);
        const Color fg = value::TryGetColor(combo->GetValue(PropId_Foreground))
            .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
        const float tx = r.x + 8.0f;
        const float ty = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
        const std::string comboFont = ResolveElementFontFamily(combo);
        const TextLayoutResult selLayout = MeasureTextLayout(
            activeSel, comboFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = 2u; k < span; ++k) {
            RenderNodeDescriptor& tn = nodes_[firstId + k];
            tn.m00 = curState.m00;
            tn.m01 = curState.m01;
            tn.m10 = curState.m10;
            tn.m11 = curState.m11;
            tn.baseTx = curState.tx;
            tn.baseTy = curState.ty;
            tn.inheritedOpacity = curState.inheritedOpacity;
            tn.hasPerspective3D = curState.hasPerspective3D;
            tn.worldMatrix3D = curState.world3D;
            (void)WriteLayoutGlyphSlice(
                firstId + k, tx, ty, fontSize, selLayout, glyphCursor, fg, effAlpha);
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized CheckBox & RadioButton update (outer indicator + inner check/dot + optional label)
    if (element->IsA<CheckBox>() || element->IsA<RadioButton>()) {
        if (span < 2u) {
            return false;
        }
        const auto* cb = element->DynamicCast<CheckBox>();
        const auto* rb = element->DynamicCast<RadioButton>();
        const bool isRadio = (rb != nullptr);
        const bool checked = isRadio ? rb->IsChecked() : cb->IsChecked();
        const auto* asBtn = static_cast<const Button*>(element);
        const std::string label = (!element->FirstChild()) ? asBtn->GetContentText() : std::string{};
        const std::string_view activeLabel = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{label};

        std::size_t glyphCap = 0u;
        bool hasLabelNodes = false;
        for (std::uint16_t k = 2u; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                hasLabelNodes = true;
                glyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }
        if ((!label.empty() && !hasLabelNodes) || CountVisibleUtf8Glyphs(activeLabel) > glyphCap) {
            return false;
        }

        const float effAlpha = element->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        const Thickness pad = element->GetPadding();
        const float boxSize = 18.0f;
        const float ix = r.x + pad.left;
        const float iy = r.y + pad.top + std::max(0.0f, (boxH - (pad.top + pad.bottom) - boxSize) * 0.5f);

        Color outerColor = ModulateInteractiveSurfaceColor(
            Color{0.16f, 0.22f, 0.30f, 1.0f},
            element);
        const float outerCr = isRadio ? 9.0f : 4.0f;
        const float outerBt = element->IsFocused() ? 2.0f : 1.5f;

        RenderNodeDescriptor& outerNd = nodes_[firstId];
        outerNd.x = ix;
        outerNd.y = iy;
        outerNd.width = (vis == Visibility::Collapsed) ? 0.0f : boxSize;
        outerNd.height = (vis == Visibility::Collapsed) ? 0.0f : boxSize;
        outerNd.opacity = effAlpha;
        ConfigureRoundedRectDescriptor(outerNd, CornerRadius{outerCr}, Thickness{outerBt}, false);
        outerNd.baseRgba = PackColorRgba8(outerColor, 1.0f);
        syncQuadTransform(outerNd);
        WriteQuadVertices(outerNd);
        MarkPageDirty(outerNd.pageIndex, outerNd.vertexOffsetInPage);

        const float markSize = isRadio ? 8.0f : 10.0f;
        const float markInset = (boxSize - markSize) * 0.5f;
        const float markCr = isRadio ? 4.0f : 2.0f;
        RenderNodeDescriptor& markNd = nodes_[firstId + 1u];
        markNd.x = ix + markInset;
        markNd.y = iy + markInset;
        markNd.width = (vis == Visibility::Collapsed || !checked) ? 0.0f : markSize;
        markNd.height = (vis == Visibility::Collapsed || !checked) ? 0.0f : markSize;
        markNd.opacity = checked ? effAlpha : 0.0f;
        ConfigureRoundedRectDescriptor(markNd, CornerRadius{markCr}, Thickness{}, false);
        markNd.baseRgba = PackColorRgba8(Color{0.26f, 0.66f, 1.0f, 1.0f}, 1.0f);
        syncQuadTransform(markNd);
        WriteQuadVertices(markNd);
        MarkPageDirty(markNd.pageIndex, markNd.vertexOffsetInPage);

        if (hasLabelNodes) {
            float fontSize = 14.0f;
            (void)TryGetFloat(element->GetValue(PropId_FontSize), fontSize);
            const Color fg = value::TryGetColor(element->GetValue(PropId_Foreground))
                .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
            const float lx = ix + boxSize + 8.0f;
            const float ly = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
            const std::string chkFont = ResolveElementFontFamily(element);
            const TextLayoutResult chkLayout = MeasureTextLayout(
                activeLabel, chkFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
            std::size_t glyphCursor = 0u;
            for (std::uint16_t k = 2u; k < span; ++k) {
                RenderNodeDescriptor& tn = nodes_[firstId + k];
                tn.m00 = curState.m00;
                tn.m01 = curState.m01;
                tn.m10 = curState.m10;
                tn.m11 = curState.m11;
                tn.baseTx = curState.tx;
                tn.baseTy = curState.ty;
                tn.inheritedOpacity = curState.inheritedOpacity;
                tn.hasPerspective3D = curState.hasPerspective3D;
                tn.worldMatrix3D = curState.world3D;
                (void)WriteLayoutGlyphSlice(
                    firstId + k, lx, ly, fontSize, chkLayout, glyphCursor, fg, effAlpha);
            }
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized TextBlock update (optional background quad + optional border ring + 1..N SdfTextGlyph run nodes)
    if (const auto* tb = element->DynamicCast<TextBlock>()) {
        if (tb->HasRichText() || tb->HasControlTemplate() || tb->FirstChild() != nullptr) {
            return false;
        }
        const auto bgOpt = value::TryGetColor(element->GetValue(PropId_Background));
        const bool wantsBgNode = bgOpt.has_value() && bgOpt->a > 0.0f;
        const auto bbOpt = value::TryGetColor(element->GetValue(PropId_BorderBrush));
        const Thickness bt = SanitizeThickness(
            value::TryGetThickness(element->GetValue(PropId_BorderThickness)).value_or(Thickness{}));
        const bool wantsBorderNode = bbOpt.has_value() && (bbOpt->a > 0.0f) && HasPositiveThickness(bt);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(tb->GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));

        std::uint16_t prefixQuads = 0u;
        while (prefixQuads < span && nodes_[firstId + prefixQuads].primType != AeroPrimType::SdfTextGlyph) {
            ++prefixQuads;
        }
        const std::uint16_t neededPrefix =
            static_cast<std::uint16_t>((wantsBgNode ? 1u : 0u) + (bbOpt.has_value() ? 1u : 0u));
        if (neededPrefix > prefixQuads) {
            // Newly added Background or BorderBrush on TextBlock -> rebuild to preserve Z-order
            return false;
        }

        const Thickness pad = tb->GetPadding();
        const Color fgColor = tb->GetForeground();
        std::string txt = tb->GetText();
        std::vector<RichGlyphStyle> richStyles;
        const std::vector<RichGlyphStyle>* richPtr = nullptr;
        if (tb->HasRichText()) {
            const std::string rawRich = tb->GetRawRichText();
            if (!rawRich.empty()) {
                ParseRichTextToPlainAndStyles(rawRich, fgColor, element, txt, richStyles);
                richPtr = &richStyles;
            }
        }
        if (txt.empty()) {
            const UnifiedValue phVal = tb->GetValue(PropId_Placeholder);
            if (const auto* ph = std::get_if<StringValue>(&phVal)) {
                txt = ph->str();
                richPtr = nullptr;
            }
        }
        const std::string_view activeText = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{txt};
        const float fontSize = tb->GetFontSize();
        const std::string fontFamily = tb->GetFontFamily();
        const TextWrapping wrapMode = tb->GetTextWrapping();
        const float effBoxW = (boxW > 0.0f) ? boxW : tb->DesiredSize().width;
        const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
            ? std::max(1.0f, effBoxW - (pad.left + pad.right))
            : 1000000.0f;
        TextLayoutResult tbLayout = MeasureTextLayout(
            activeText, fontFamily, fontSize, maxTextW, wrapMode);
        const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
        ApplyTextAlignmentToLayout(tbLayout, tb->GetTextAlignment(), innerAlignW);

        std::size_t totalGlyphCap = 0;
        for (std::uint16_t k = prefixQuads; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                totalGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }

        const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
            Rect{r.x, r.y, boxW, boxH},
            cr,
            wantsBgNode,
            bgOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
            bbOpt.has_value(),
            bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
            bt);

        if (CountLayoutVisibleGlyphs(tbLayout) > totalGlyphCap) {
            return false;
        }

        std::uint16_t quadSlot = 0u;
        if (quadSlot < prefixQuads && !nodes_[firstId + quadSlot].isHollowBorder) {
            RenderNodeDescriptor& node = nodes_[firstId + quadSlot];
            node.x = plan.fillRect.x;
            node.y = plan.fillRect.y;
            node.width = plan.fillRect.width;
            node.height = plan.fillRect.height;
            node.opacity = wantsBgNode ? effOpacity : 0.0f;
            if (wantsBgNode) {
                node.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
            }
            ConfigureRoundedRectDescriptor(node, plan.fillRadii, Thickness{}, false);
            syncQuadTransform(node);
            WriteQuadVertices(node);
            MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
            ++quadSlot;
        }
        while (quadSlot < prefixQuads) {
            RenderNodeDescriptor& brdNode = nodes_[firstId + quadSlot];
            brdNode.x = plan.borderRect.x;
            brdNode.y = plan.borderRect.y;
            brdNode.width = wantsBorderNode ? plan.borderRect.width : 0.0f;
            brdNode.height = wantsBorderNode ? plan.borderRect.height : 0.0f;
            brdNode.opacity = wantsBorderNode ? effOpacity : 0.0f;
            brdNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
            ConfigureRoundedRectDescriptor(brdNode, plan.borderRadii, plan.borderThickness, true);
            syncQuadTransform(brdNode);
            WriteQuadVertices(brdNode);
            MarkPageDirty(brdNode.pageIndex, brdNode.vertexOffsetInPage);
            ++quadSlot;
        }

        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = prefixQuads; k < span; ++k) {
            RenderNodeDescriptor& node = nodes_[firstId + k];
            node.m00 = curState.m00;
            node.m01 = curState.m01;
            node.m10 = curState.m10;
            node.m11 = curState.m11;
            node.baseTx = curState.tx;
            node.baseTy = curState.ty;
            node.inheritedOpacity = curState.inheritedOpacity;
            node.hasPerspective3D = curState.hasPerspective3D;
            node.worldMatrix3D = curState.world3D;
            (void)WriteLayoutGlyphSlice(
                firstId + k,
                r.x + pad.left,
                r.y + pad.top,
                fontSize,
                tbLayout,
                glyphCursor,
                fgColor,
                effOpacity,
                (vis == Visibility::Collapsed) ? nullptr : richPtr);
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized Button update (fill box + optional BorderBrush ring + optional centered SdfTextGlyph label span)
    if (const auto* btn = element->DynamicCast<Button>()) {
        const std::string label = (!btn->FirstChild()) ? btn->GetContentText() : std::string{};
        const std::string_view activeLabel = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{label};

        std::uint16_t boxNodeCount = 0u;
        while (boxNodeCount < span && nodes_[firstId + boxNodeCount].primType != AeroPrimType::SdfTextGlyph) {
            ++boxNodeCount;
        }
        const auto bbOpt = value::TryGetColor(btn->GetValue(PropId_BorderBrush));
        if (bbOpt.has_value() && boxNodeCount < 2u) {
            return false;
        }
        if (boxNodeCount == 0u) {
            return false;
        }

        std::size_t totalLabelGlyphCap = 0;
        bool hasLabelNode = false;
        for (std::uint16_t k = boxNodeCount; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                hasLabelNode = true;
                totalLabelGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }
        if (!label.empty() && !hasLabelNode) {
            // Button initially had empty Content="" and gained text -> rebuild cleanly to preserve Z-order
            return false;
        }

        const float btnAlpha = btn->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color btnBg = value::TryGetColor(btn->GetValue(PropId_Background))
            .value_or(Color{0.22f, 0.34f, 0.54f, 1.0f});
        btnBg = ModulateInteractiveSurfaceColor(btnBg, btn);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(btn->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        Thickness btRaw = SanitizeThickness(
            value::TryGetThickness(btn->GetValue(PropId_BorderThickness))
                .value_or(Thickness{}));
        if (btn->IsFocused()) {
            btRaw = Thickness{
                std::max(2.0f, btRaw.left),
                std::max(2.0f, btRaw.top),
                std::max(2.0f, btRaw.right),
                std::max(2.0f, btRaw.bottom)};
        }

        Color fg{0.96f, 0.98f, 1.0f, 1.0f};
        if (auto fgOpt = value::TryGetColor(btn->GetValue(PropId_Foreground))) {
            fg = *fgOpt;
        }
        float fontSize = 14.0f;
        (void)TryGetFloat(btn->GetValue(PropId_FontSize), fontSize);
        const std::string btnFont = ResolveElementFontFamily(btn);
        const TextLayoutResult btnLayout = MeasureTextLayout(
            activeLabel, btnFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
        const float lx = r.x + std::max(4.0f, (boxW - btnLayout.totalWidth) * 0.5f);
        const float ly = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);

        if (CountLayoutVisibleGlyphs(btnLayout) > totalLabelGlyphCap) {
            return false;
        }

        if (boxNodeCount >= 2u) {
            const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
                Rect{r.x, r.y, boxW, boxH},
                cr,
                btnBg.a > 0.0f,
                btnBg,
                bbOpt.has_value(),
                bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
                btRaw);
            RenderNodeDescriptor& fillNode = nodes_[firstId];
            fillNode.x = plan.fillRect.x;
            fillNode.y = plan.fillRect.y;
            fillNode.width = plan.fillRect.width;
            fillNode.height = plan.fillRect.height;
            fillNode.opacity = plan.hasVisibleFill ? btnAlpha : 0.0f;
            fillNode.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
            ConfigureRoundedRectDescriptor(fillNode, plan.fillRadii, Thickness{}, false);
            syncQuadTransform(fillNode);
            WriteQuadVertices(fillNode);
            MarkPageDirty(fillNode.pageIndex, fillNode.vertexOffsetInPage);

            RenderNodeDescriptor& brdNode = nodes_[firstId + 1u];
            brdNode.x = plan.borderRect.x;
            brdNode.y = plan.borderRect.y;
            brdNode.width = plan.hasVisibleBorder ? plan.borderRect.width : 0.0f;
            brdNode.height = plan.hasVisibleBorder ? plan.borderRect.height : 0.0f;
            brdNode.opacity = plan.hasVisibleBorder ? btnAlpha : 0.0f;
            brdNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
            ConfigureRoundedRectDescriptor(brdNode, plan.borderRadii, plan.borderThickness, true);
            syncQuadTransform(brdNode);
            WriteQuadVertices(brdNode);
            MarkPageDirty(brdNode.pageIndex, brdNode.vertexOffsetInPage);
        } else {
            RenderNodeDescriptor& boxNode = nodes_[firstId];
            boxNode.x = r.x;
            boxNode.y = r.y;
            boxNode.width = boxW;
            boxNode.height = boxH;
            boxNode.opacity = (btnBg.a > 0.0f) ? btnAlpha : 0.0f;
            boxNode.baseRgba = PackColorRgba8(btnBg, 1.0f);
            const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
            ConfigureRoundedRectDescriptor(boxNode, cr, Thickness{legacyBt}, false);
            syncQuadTransform(boxNode);
            WriteQuadVertices(boxNode);
            MarkPageDirty(boxNode.pageIndex, boxNode.vertexOffsetInPage);
        }

        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = boxNodeCount; k < span; ++k) {
            RenderNodeDescriptor& node = nodes_[firstId + k];
            node.m00 = curState.m00;
            node.m01 = curState.m01;
            node.m10 = curState.m10;
            node.m11 = curState.m11;
            node.baseTx = curState.tx;
            node.baseTy = curState.ty;
            node.inheritedOpacity = curState.inheritedOpacity;
            node.hasPerspective3D = curState.hasPerspective3D;
            node.worldMatrix3D = curState.world3D;
            (void)WriteLayoutGlyphSlice(
                firstId + k,
                lx,
                ly,
                fontSize,
                btnLayout,
                glyphCursor,
                fg,
                btnAlpha);
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized Image update (Stretch geometry + UV crop + multi-texture page compatibility)
    if (const auto* imgElem = element->DynamicCast<Image>()) {
        if (span != 1u) {
            return false;
        }
        RenderNodeDescriptor& imgNode = nodes_[firstId];
        if (imgNode.pageIndex >= pages_.size()) {
            return false;
        }
        const Thickness pad = imgElem->GetPadding();
        const Rect contentBox{
            r.x + pad.left,
            r.y + pad.top,
            std::max(0.0f, boxW - (pad.left + pad.right)),
            std::max(0.0f, boxH - (pad.top + pad.bottom))};
        const std::string srcUri = imgElem->GetSource();
        const ImageAtlasEntry* atlasEntry = (vis == Visibility::Collapsed || srcUri.empty())
            ? nullptr
            : ImageAtlas::Instance().LoadOrGet(srcUri);
        const std::uint32_t desiredTexId = atlasEntry ? atlasEntry->textureId : imgNode.textureId;

        CommandPage& owningPage = pages_[imgNode.pageIndex];
        if (atlasEntry && owningPage.header.reserved[0] != desiredTexId) {
            if (owningPage.header.nodeCount == 1u) {
                owningPage.header.reserved[0] = desiredTexId;
            } else {
                return false;
            }
        }

        const Color tintColor = value::TryGetColor(imgElem->GetValue(PropId_Foreground))
            .value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});
        imgNode.primType = AeroPrimType::TexturedQuad;
        imgNode.encodedSdfParams = AeroVertexFlag_None;
        imgNode.encodedPrimExtra = 0u;
        imgNode.isHollowBorder = false;
        imgNode.baseRgba = PackColorRgba8(tintColor, 1.0f);
        imgNode.textureId = desiredTexId;

        if (atlasEntry && atlasEntry->width > 0u && atlasEntry->height > 0u && vis != Visibility::Collapsed) {
            const ImageStretchPlacement placement = ComputeImageStretchPlacement(
                contentBox,
                static_cast<float>(atlasEntry->width),
                static_cast<float>(atlasEntry->height),
                imgElem->GetStretch(),
                imgElem->GetStretchDirection(),
                atlasEntry->u0,
                atlasEntry->v0,
                atlasEntry->u1,
                atlasEntry->v1);
            if (placement.valid) {
                imgNode.x = placement.destRect.x;
                imgNode.y = placement.destRect.y;
                imgNode.width = placement.destRect.width;
                imgNode.height = placement.destRect.height;
                imgNode.uv0X = placement.u0;
                imgNode.uv0Y = placement.v0;
                imgNode.uv1X = placement.u1;
                imgNode.uv1Y = placement.v1;
                imgNode.opacity = effOpacity;
            } else {
                imgNode.x = contentBox.x;
                imgNode.y = contentBox.y;
                imgNode.width = 0.0f;
                imgNode.height = 0.0f;
                imgNode.opacity = 0.0f;
            }
        } else {
            imgNode.x = r.x;
            imgNode.y = r.y;
            imgNode.width = 0.0f;
            imgNode.height = 0.0f;
            imgNode.opacity = 0.0f;
        }

        syncQuadTransform(imgNode);
        WriteQuadVertices(imgNode);
        MarkPageDirty(imgNode.pageIndex, imgNode.vertexOffsetInPage);
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Generic Quad / Border / Panel update
    if (ExtractGradBrushId(element->GetValue(PropId_BackgroundGradientId)) != 0u ||
        !ExtractContainerImageBrushUri(element).empty() ||
        nodes_[firstId].primType == AeroPrimType::GradientRoundedRect ||
        nodes_[firstId].primType == AeroPrimType::TexturedQuad) {
        return false;
    }
    const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(
        element,
        Rect{r.x, r.y, boxW, boxH});
    const std::uint16_t neededSpan = plan.reserveBorderNode ? 2u : 1u;

    if (span < neededSpan) {
        return false;
    }

    {
        RenderNodeDescriptor& fillNode = nodes_[firstId];
        fillNode.x = plan.fillRect.x;
        fillNode.y = plan.fillRect.y;
        fillNode.width = plan.fillRect.width;
        fillNode.height = plan.fillRect.height;
        fillNode.opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
        fillNode.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
        ConfigureRoundedRectDescriptor(fillNode, plan.fillRadii, Thickness{}, false);
        syncQuadTransform(fillNode);
        WriteQuadVertices(fillNode);
        MarkPageDirty(fillNode.pageIndex, fillNode.vertexOffsetInPage);
    }

    if (span >= 2u) {
        const bool activeBorder = plan.hasVisibleBorder && (vis == Visibility::Visible);
        RenderNodeDescriptor& borderNode = nodes_[firstId + 1u];
        borderNode.x = plan.borderRect.x;
        borderNode.y = plan.borderRect.y;
        borderNode.width = activeBorder ? plan.borderRect.width : 0.0f;
        borderNode.height = activeBorder ? plan.borderRect.height : 0.0f;
        borderNode.opacity = activeBorder ? effOpacity : 0.0f;
        borderNode.baseRgba = activeBorder ? PackColorRgba8(plan.borderColor, 1.0f) : 0u;
        ConfigureRoundedRectDescriptor(borderNode, plan.borderRadii, plan.borderThickness, true);
        syncQuadTransform(borderNode);
        WriteQuadVertices(borderNode);
        MarkPageDirty(borderNode.pageIndex, borderNode.vertexOffsetInPage);

        for (std::uint16_t k = 2u; k < span; ++k) {
            ZeroOutNodeVertices(nodes_[firstId + k]);
        }
    }

    element->ClearLayoutDirtyFlags(RenderDirty);
    return true;
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
                if (node.sourceElement != nullptr) {
                    const Matrix2D oldLocal = node.sourceElement->ComputeLocalEffectiveTransform2DForRect(
                        Rect{node.x, node.y, oldW, node.height});
                    const Matrix2D newLocal = node.sourceElement->ComputeLocalEffectiveTransform2DForRect(
                        Rect{node.x, node.y, w, node.height});
                    if (oldLocal != newLocal) {
                        Matrix2D invOld{};
                        if (oldLocal.TryInvert(invOld)) {
                            const Matrix2D prevWorld = MatrixFromState(
                                node.m00, node.m01, node.m10, node.m11, node.baseTx, node.baseTy);
                            const Matrix2D parentM = Matrix2D::Multiply(prevWorld, invOld);
                            const Matrix2D nextWorld = Matrix2D::Multiply(parentM, newLocal);
                            node.m00 = nextWorld.m00;
                            node.m01 = nextWorld.m01;
                            node.m10 = nextWorld.m10;
                            node.m11 = nextWorld.m11;
                            node.baseTx = nextWorld.tx;
                            node.baseTy = nextWorld.ty;
                            node.tx = nextWorld.tx + node.x * (nextWorld.m00 - 1.0f) + node.y * nextWorld.m01;
                            node.ty = nextWorld.ty + node.x * nextWorld.m10 + node.y * (nextWorld.m11 - 1.0f);
                        }
                    }
                }
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

std::size_t PagedRenderTree::BatchUpdateFromEcsTrackB(
    const DualTrackBindingEngine& bindingEngine,
    std::uint32_t firstNodeId) noexcept {
    std::size_t totalUpdated = 0;
    std::uint32_t cursorNodeId = firstNodeId;
    for (const auto& channel : bindingEngine.EcsChannels()) {
        const NativeRawColumnView* view = bindingEngine.ResolveChannelView(channel);
        const std::size_t reservedSpan = !channel.targetElements.empty()
            ? channel.targetElements.size()
            : (view ? view->count : 0u);
        if (view && view->basePtr) {
            const std::size_t stride = (channel.elementStride != 0)
                ? channel.elementStride
                : view->strideBytes;
            if (stride != 0) {
                const std::size_t count = !channel.targetElements.empty()
                    ? std::min(channel.targetElements.size(), view->count)
                    : view->count;
                totalUpdated += BatchUpdateWidthsFromRawColumn(
                    cursorNodeId,
                    count,
                    view->basePtr,
                    stride,
                    channel.fieldByteOffset,
                    channel.scaleFactor);
            }
        }
        cursorNodeId += static_cast<std::uint32_t>(reservedSpan);
    }
    return totalUpdated;
}

bool PagedRenderTree::SubtreeContainsActiveComboPopup(const UIElement* elem) const noexcept {
    if (!elem || activeComboPopupOwners_.empty()) {
        return false;
    }
    for (const UIElement* owner : activeComboPopupOwners_) {
        for (const UIElement* cur = owner; cur != nullptr; cur = cur->Parent()) {
            if (cur == elem) {
                return true;
            }
        }
    }
    return false;
}

void PagedRenderTree::EmitDeferredComboPopups() {
    if (pendingComboPopups_.empty()) {
        return;
    }
    // Render open ComboBox dropdown menus unclipped on top of sibling controls
    activeClipRect_ = Rect{0.0f, 0.0f, 16384.0f, 16384.0f};
    activeClipEnabled_ = false;

    for (const auto& entry : pendingComboPopups_) {
        const ComboBox* combo = entry.combo;
        if (!combo || !combo->IsDropDownOpen() || !combo->IsEffectivelyEnabled() ||
            combo->GetVisibility() != Visibility::Visible || entry.state.inheritedOpacity <= 0.0f) {
            continue;
        }
        const std::size_t itemCount = combo->ComboItemCount();
        if (itemCount == 0u) {
            continue;
        }
        activeComboPopupOwners_.push_back(combo);
        const RenderTraversalState& st = entry.state;
        const Rect popupR = combo->DropDownPopupLocalRect();
        const float effAlpha = st.inheritedOpacity;

        auto syncPopupQuad = [&](std::uint32_t nid) {
            RenderNodeDescriptor& nd = nodes_[nid];
            nd.m00 = st.m00;
            nd.m01 = st.m01;
            nd.m10 = st.m10;
            nd.m11 = st.m11;
            nd.baseTx = st.tx;
            nd.baseTy = st.ty;
            nd.tx = st.tx + nd.x * (st.m00 - 1.0f) + nd.y * st.m01;
            nd.ty = st.ty + nd.x * st.m10 + nd.y * (st.m11 - 1.0f);
            nd.inheritedOpacity = st.inheritedOpacity;
            WriteQuadVertices(nd);
        };

        // Popup backdrop panel
        const std::uint32_t bgNid = AllocateRenderNode(
            popupR.x,
            popupR.y,
            popupR.width,
            popupR.height,
            Color{0.11f, 0.15f, 0.22f, 0.98f},
            4.0f,
            1.0f,
            nullptr);
        nodes_[bgNid].opacity = effAlpha;
        syncPopupQuad(bgNid);

        float fontSize = 14.0f;
        (void)TryGetFloat(combo->GetValue(PropId_FontSize), fontSize);
        const std::int32_t selIdx = combo->GetSelectedIndex();
        const std::int32_t hiIdx = combo->HighlightedPopupIndex();

        for (std::size_t i = 0u; i < itemCount; ++i) {
            const float rowY = popupR.y + static_cast<float>(i) * ComboBox::kPopupRowHeight;
            const auto iSigned = static_cast<std::int32_t>(i);
            if (iSigned == hiIdx || iSigned == selIdx) {
                const Color rowHighlight = (iSigned == hiIdx)
                    ? Color{0.25f, 0.52f, 0.92f, 0.92f}
                    : Color{0.20f, 0.34f, 0.56f, 0.75f};
                const std::uint32_t rowId = AllocateRenderNode(
                    popupR.x + 2.0f,
                    rowY + 1.0f,
                    std::max(0.0f, popupR.width - 4.0f),
                    ComboBox::kPopupRowHeight - 2.0f,
                    rowHighlight,
                    3.0f,
                    0.0f,
                    nullptr);
                nodes_[rowId].opacity = effAlpha;
                syncPopupQuad(rowId);
            }
            const std::string itemLabel = combo->GetComboItemText(i);
            if (!itemLabel.empty()) {
                (void)EmitTextRunNodes(
                    popupR.x + 8.0f,
                    rowY + std::max(2.0f, (ComboBox::kPopupRowHeight - fontSize * 1.2f) * 0.5f),
                    fontSize,
                    itemLabel,
                    Color{0.95f, 0.97f, 1.0f, 1.0f},
                    effAlpha,
                    st,
                    nullptr);
            }
        }
    }
    pendingComboPopups_.clear();
}

std::size_t PagedRenderTree::BuildFromVisualTree(const UIElement* rootElement) {
    Clear();
    if (!rootElement) {
        return 0;
    }
    boundRootElement_ = rootElement;
    RenderTraversalState rootState{};
    TraverseAndEmitElement(rootElement, rootState);
    EmitDeferredComboPopups();
    return nodes_.size();
}

void PagedRenderTree::CollapseSubtreeRenderNodesRecursive(
    const UIElement* elem,
    std::size_t& ioUpdatedNodes) noexcept {
    if (!elem) {
        return;
    }
    if (elem->RenderTreeId() == treeId_ &&
        elem->RenderNodeId() < nodes_.size() &&
        elem->RenderNodeSpan() > 0u) {
        const std::uint32_t firstId = elem->RenderNodeId();
        const std::uint16_t span = elem->RenderNodeSpan();
        bool anyZeroed = false;
        for (std::uint16_t k = 0; k < span && firstId + k < nodes_.size(); ++k) {
            RenderNodeDescriptor& nd = nodes_[firstId + k];
            if (nd.sourceElement == elem) {
                nd.width = 0.0f;
                nd.height = 0.0f;
                nd.opacity = 0.0f;
                ZeroOutNodeVertices(nd);
                anyZeroed = true;
            }
        }
        if (anyZeroed) {
            ++ioUpdatedNodes;
        }
    }
    elem->DetachRenderNodeSpan();
    elem->ClearLayoutDirtyFlags(static_cast<std::uint8_t>(RenderDirty | SubtreeRenderDirty));
    for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
        CollapseSubtreeRenderNodesRecursive(child, ioUpdatedNodes);
    }
}

std::size_t PagedRenderTree::SyncVisualTree(const UIElement* rootElement) {
    if (!rootElement) {
        Clear();
        return 0;
    }
    if (boundRootElement_ != rootElement ||
        nodes_.empty() ||
        (rootElement->GetVisibility() != Visibility::Collapsed && rootElement->RenderTreeId() != treeId_)) {
        return BuildFromVisualTree(rootElement);
    }

    // Fast O(1) clean-frame short-circuit!
    const std::uint8_t rootFlags = rootElement->GetLayoutDirtyFlags();
    if ((rootFlags & (RenderDirty | SubtreeRenderDirty)) == 0u) {
        return 0;
    }

    std::size_t updatedNodes = 0;
    bool needsRebuild = false;
    RenderTraversalState rootState{};
    (void)SyncDirtySubtreeRecursive(rootElement, rootState, false, updatedNodes, needsRebuild);
    if (needsRebuild) {
        return BuildFromVisualTree(rootElement);
    }
    return updatedNodes;
}

bool PagedRenderTree::SyncDirtySubtreeRecursive(
    const UIElement* elem,
    const RenderTraversalState& parentState,
    bool forceSubtreeSync,
    std::size_t& ioUpdatedNodes,
    bool& outNeedsFullRebuild) {
    if (!elem) {
        return true;
    }

    const std::uint8_t flags = elem->GetLayoutDirtyFlags();
    if (!forceSubtreeSync && (flags & (RenderDirty | SubtreeRenderDirty)) == 0u) {
        return true;
    }

    // If a container or leaf transitioned to Collapsed, zero out its entire subtree's GPU vertices in-place (0 allocs!)
    if (elem->GetVisibility() == Visibility::Collapsed) {
        if (SubtreeContainsActiveComboPopup(elem)) {
            outNeedsFullRebuild = true;
            return false;
        }
        CollapseSubtreeRenderNodesRecursive(elem, ioUpdatedNodes);
        return true;
    }
    if (const auto* pop = elem->DynamicCast<Popup>(); pop && !pop->IsOpen()) {
        CollapseSubtreeRenderNodesRecursive(elem, ioUpdatedNodes);
        return true;
    }

    if (elem->HasActiveTransform3D() || parentState.hasPerspective3D ||
        elem->GetEffectKind() != UIEffectKind::None || !blurLayerSpans_.empty()) {
        outNeedsFullRebuild = true;
        return false;
    }

    // If a new child was dynamically added, a sibling was removed (parent detached), or Visibility transitioned
    // out of Collapsed/unemitted state, trigger a clean structural rebuild once.
    if (elem->RenderTreeId() != treeId_ ||
        elem->RenderNodeId() >= nodes_.size() ||
        elem->RenderNodeSpan() == 0u ||
        nodes_[elem->RenderNodeId()].sourceElement != elem) {
        outNeedsFullRebuild = true;
        return false;
    }

    const Rect r = elem->FinalRect();
    const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, elem->GetWidth());
    const float h = (r.height > 0.0f) ? r.height : std::max(0.0f, elem->GetHeight());

    RenderTraversalState curState = parentState;
    if (elem->GetVisibility() != Visibility::Visible) {
        curState.inheritedOpacity = 0.0f;
    } else {
        curState.inheritedOpacity *= std::clamp(elem->GetOpacity(), 0.0f, 1.0f);
    }

    if ((elem->GetVisibility() != Visibility::Visible || curState.inheritedOpacity <= 0.0f ||
         !elem->IsEffectivelyEnabled()) &&
        SubtreeContainsActiveComboPopup(elem)) {
        outNeedsFullRebuild = true;
        return false;
    }

    ApplyStateMatrix(
        curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty,
        elem->ComputeLocalEffectiveTransform2D());

    // Check whether this container's inherited opacity or full 2D/3D transform changed so descendants also sync
    bool propagateToAllChildren = forceSubtreeSync;
    if (!propagateToAllChildren && elem->FirstChild() != nullptr) {
        const RenderNodeDescriptor& prevNode = nodes_[elem->RenderNodeId()];
        if (std::fabs(prevNode.inheritedOpacity - curState.inheritedOpacity) > 1e-5f ||
            std::fabs(prevNode.m00 - curState.m00) > 1e-5f ||
            std::fabs(prevNode.m01 - curState.m01) > 1e-5f ||
            std::fabs(prevNode.m10 - curState.m10) > 1e-5f ||
            std::fabs(prevNode.m11 - curState.m11) > 1e-5f ||
            std::fabs(prevNode.baseTx - curState.tx) > 1e-3f ||
            std::fabs(prevNode.baseTy - curState.ty) > 1e-3f) {
            propagateToAllChildren = true;
        }
    }

    if (forceSubtreeSync || (flags & RenderDirty) != 0u) {
        if (!UpdateElementInPlace(elem, curState)) {
            outNeedsFullRebuild = true;
            return false;
        }
        ++ioUpdatedNodes;
        elem->ClearLayoutDirtyFlags(RenderDirty);
    }

    if (elem->IsA<Expander>() || elem->IsA<TabControl>()) {
        outNeedsFullRebuild = true;
        return false;
    }
    if (elem->IsA<ComboBox>() && !elem->HasControlTemplate()) {
        elem->ClearLayoutDirtyFlags(SubtreeRenderDirty);
        return true;
    }

    RenderTraversalState childState = curState;
    const UnifiedValue clipVal = elem->GetValue(PropId_ClipToBounds);
    if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
        const Matrix2D worldM = MatrixFromState(
            curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty);
        Rect rawClip = elem->GetLocalClipBounds();
        if (r.width <= 0.0f && r.height <= 0.0f && (w > 0.0f || h > 0.0f)) {
            rawClip = Rect{r.x, r.y, w, h};
        }
        const Rect transformedBounds = worldM.TransformRectBounds(rawClip);
        childState.clipRect = childState.clipActive
            ? IntersectRect(childState.clipRect, transformedBounds)
            : transformedBounds;
        childState.clipActive = true;
    }
    ApplyStateMatrix(
        childState.m00, childState.m01, childState.m10, childState.m11, childState.tx, childState.ty,
        elem->GetChildContainerTransform2D());

    // Verify child CommandPage scissor state matches childState when container or clip changed
    if (elem->FirstChild() != nullptr && (forceSubtreeSync || (flags & RenderDirty) != 0u)) {
        for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (child->GetVisibility() != Visibility::Collapsed &&
                child->RenderTreeId() == treeId_ &&
                child->RenderNodeId() < nodes_.size()) {
                const std::uint32_t cPageIdx = nodes_[child->RenderNodeId()].pageIndex;
                if (cPageIdx < pages_.size() &&
                    !SameClipRect(pages_[cPageIdx].header, childState.clipRect, childState.clipActive)) {
                    outNeedsFullRebuild = true;
                    return false;
                }
                break;
            }
        }
    }

    if (propagateToAllChildren || (flags & SubtreeRenderDirty) != 0u) {
        for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (!SyncDirtySubtreeRecursive(
                    child, childState, propagateToAllChildren, ioUpdatedNodes, outNeedsFullRebuild)) {
                return false;
            }
        }
        elem->ClearLayoutDirtyFlags(SubtreeRenderDirty);
    }
    return true;
}

void PagedRenderTree::TraverseAndEmitElement(
    const UIElement* elem,
    const RenderTraversalState& parentState) {
    if (!elem) {
        return;
    }
    if (elem->GetVisibility() == Visibility::Collapsed) {
        std::size_t dummy = 0;
        CollapseSubtreeRenderNodesRecursive(elem, dummy);
        return;
    }
    if (const auto* pop = elem->DynamicCast<Popup>(); pop && !pop->IsOpen()) {
        std::size_t dummy = 0;
        CollapseSubtreeRenderNodesRecursive(elem, dummy);
        return;
    }

    const Rect r = elem->FinalRect();
    const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, elem->GetWidth());
    const float h = (r.height > 0.0f) ? r.height : std::max(0.0f, elem->GetHeight());

    // Compose inherited opacity and local 2D/3D foreshortening transform with parentState
    RenderTraversalState curState = parentState;
    if (elem->GetVisibility() != Visibility::Visible) {
        curState.inheritedOpacity = 0.0f;
    } else {
        curState.inheritedOpacity *= std::clamp(elem->GetOpacity(), 0.0f, 1.0f);
    }
    const float effOpacity = curState.inheritedOpacity;

    const Matrix2D local2D = elem->ComputeLocalEffectiveTransform2D();
    ApplyStateMatrix(
        curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty,
        local2D);

    if (elem->HasActiveTransform3D()) {
        curState.world3D = Matrix4x4::ComposePlanarHomography(
            curState.world3D,
            elem->ComputeLocalEffectiveTransform3D());
        curState.hasPerspective3D = true;
    } else if (curState.hasPerspective3D) {
        if (!local2D.IsIdentity()) {
            curState.world3D = Matrix4x4::ComposePlanarHomography(
                curState.world3D,
                Matrix4x4::FromMatrix2D(local2D));
        }
    } else {
        curState.world3D = Matrix4x4::FromMatrix2D(
            MatrixFromState(curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty));
    }

    // Resolve BlendingMode (element override or inherited)
    {
        const UnifiedValue bmVal = elem->GetValue(PropId_BlendingMode);
        if (const auto* bmi = std::get_if<std::int64_t>(&bmVal)) {
            if (*bmi >= 0 && *bmi <= 3) {
                curState.blendMode = static_cast<BlendingMode>(*bmi);
            }
        } else if (const auto* bms = std::get_if<StringValue>(&bmVal)) {
            const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_BlendingMode, bms->view());
            if (const auto* pi = std::get_if<std::int64_t>(&parsed); pi && *pi >= 0 && *pi <= 3) {
                curState.blendMode = static_cast<BlendingMode>(*pi);
            }
        }
    }

    // Apply current clip & blend state for this element's own primitives
    activeClipRect_ = curState.clipRect;
    activeClipEnabled_ = curState.clipActive;
    activeBlendMode_ = curState.blendMode;

    const UIEffectDescriptor fx = elem->GetEffectDescriptor();
    const bool wantsBlurSpan =
        !curState.shadowOnlyPass &&
        ((fx.kind == UIEffectKind::Blur && fx.blurRadius > 0.25f) ||
         (fx.kind == UIEffectKind::Pixelate && fx.blurRadius >= 1.5f) ||
         (fx.kind == UIEffectKind::BackdropBlur && fx.blurRadius > 0.25f)) &&
        effOpacity > 0.001f;
    const std::size_t blurSpanSlot = wantsBlurSpan ? blurLayerSpans_.size() : static_cast<std::size_t>(-1);
    if (wantsBlurSpan) {
        BlurLayerSpan bSpan{};
        bSpan.firstNodeId = static_cast<std::uint32_t>(nodes_.size());
        bSpan.endNodeId = static_cast<std::uint32_t>(nodes_.size());
        bSpan.radiusPx = fx.blurRadius;
        bSpan.clipRect = curState.clipRect;
        bSpan.clipActive = curState.clipActive;
        bSpan.effectMode = fx.kind;
        bSpan.maskOpacity = effOpacity;
        if (fx.kind == UIEffectKind::BackdropBlur) {
            const Matrix2D worldM = MatrixFromState(
                curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty);
            if (const auto* pathElem = elem->DynamicCast<Path>()) {
                const std::string pathStr = pathElem->GetData();
                if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
                    RegisterMtsdfInTreeAtlas(*mtsdf);
                    Rect fitted = ComputeStretchedDestRect(
                        Rect{r.x, r.y, w, h}, mtsdf->boundsW, mtsdf->boundsH, pathElem->GetStretch());
                    if (pathElem->GetStretch() == Stretch::None) {
                        fitted.x += mtsdf->boundsX;
                        fitted.y += mtsdf->boundsY;
                    }
                    bSpan.maskMtsdfHash = mtsdf->pathHash;
                    bSpan.maskDestRect = curState.hasPerspective3D
                        ? curState.world3D.TransformRectBounds(fitted)
                        : worldM.TransformRectBounds(fitted);
                }
            } else {
                bSpan.maskDestRect = curState.hasPerspective3D
                    ? curState.world3D.TransformRectBounds(Rect{r.x, r.y, w, h})
                    : worldM.TransformRectBounds(Rect{r.x, r.y, w, h});
            }
        }
        blurLayerSpans_.push_back(bSpan);
    }

    const std::uint32_t spanStartId = static_cast<std::uint32_t>(nodes_.size());
    std::uint16_t emittedCount = 0u;

    auto applyNodeTransform = [&](std::uint32_t nid) noexcept {
        RenderNodeDescriptor& nd = nodes_[nid];
        nd.m00 = curState.m00;
        nd.m01 = curState.m01;
        nd.m10 = curState.m10;
        nd.m11 = curState.m11;
        nd.baseTx = curState.tx;
        nd.baseTy = curState.ty;
        nd.tx = curState.tx + nd.x * (curState.m00 - 1.0f) + nd.y * curState.m01;
        nd.ty = curState.ty + nd.x * curState.m10 + nd.y * (curState.m11 - 1.0f);
        nd.inheritedOpacity = curState.inheritedOpacity;
        nd.hasPerspective3D = curState.hasPerspective3D;
        nd.blendMode = curState.blendMode;
        nd.worldMatrix3D = curState.world3D;
        if (nd.primType != AeroPrimType::SdfTextGlyph) {
            WriteQuadVertices(nd);
        }
    };

    // Determine effective DropShadow (own DropShadowEffect or inherited shadow-only pass from a transparent container)
    bool hasActiveShadow = false;
    float worldSdx = 0.0f, worldSdy = 0.0f, worldBlurR = 1.0f;
    Color shCol{0.0f, 0.0f, 0.0f, 0.0f};
    bool propagateShadowToChildren = false;

    if (curState.shadowOnlyPass) {
        if (curState.hasInheritedDropShadow && effOpacity > 0.001f) {
            worldSdx = curState.shadowWorldDx;
            worldSdy = curState.shadowWorldDy;
            worldBlurR = curState.shadowWorldBlurR;
            shCol = curState.shadowColor;
            hasActiveShadow = true;
        }
    } else if (fx.kind == UIEffectKind::DropShadow &&
               fx.opacity > 0.001f &&
               fx.color.a > 0.001f &&
               effOpacity > 0.001f) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float dirRad = fx.directionDeg * kDegToRad;
        const float rawSdx = std::cos(dirRad) * fx.shadowDepth;
        const float rawSdy = -std::sin(dirRad) * fx.shadowDepth;
        worldSdx = curState.m00 * rawSdx + curState.m01 * rawSdy;
        worldSdy = curState.m10 * rawSdx + curState.m11 * rawSdy;
        const float elemScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
        const float elemScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
        const float elemAvgScale = std::max(1e-4f, 0.5f * (elemScaleX + elemScaleY));
        worldBlurR = std::max(1.0f, fx.blurRadius) * elemAvgScale;
        shCol = Color{
            fx.color.r,
            fx.color.g,
            fx.color.b,
            std::clamp(fx.color.a * fx.opacity, 0.0f, 1.0f)};
        hasActiveShadow = true;
    }

    if (hasActiveShadow) {
        const float leafScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
        const float leafScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
        const float leafAvgScale = std::max(1e-4f, 0.5f * (leafScaleX + leafScaleY));
        const float det = curState.m00 * curState.m11 - curState.m01 * curState.m10;
        float sdx = worldSdx;
        float sdy = worldSdy;
        if (std::fabs(det) > 1e-6f) {
            const float invDet = 1.0f / det;
            sdx = (curState.m11 * worldSdx - curState.m01 * worldSdy) * invDet;
            sdy = (-curState.m10 * worldSdx + curState.m00 * worldSdy) * invDet;
        }
        const float localBlurR = std::max(0.5f, worldBlurR / leafAvgScale);
        const float screenBlurR = std::max(1.0f, worldBlurR);
        const UIElement* shadowOwner = curState.shadowOnlyPass ? nullptr : elem;

        if (const auto* pathElem = elem->DynamicCast<Path>()) {
            std::string pathStr = pathElem->GetData();
            if (pathStr.empty()) {
                const UnifiedValue ptsVal = pathElem->GetValue(PropId_ShapePoints);
                if (const auto* sv = std::get_if<StringValue>(&ptsVal); sv && !sv->view().empty()) {
                    pathStr = ConvertPointsToSvgPath(sv->view(), elem->IsA<Polygon>());
                }
            }
            if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
                Rect fitted = ComputeStretchedDestRect(
                    Rect{r.x + sdx, r.y + sdy, w, h},
                    mtsdf->boundsW,
                    mtsdf->boundsH,
                    pathElem->GetStretch());
                if (pathElem->GetStretch() == Stretch::None) {
                    fitted.x += mtsdf->boundsX;
                    fitted.y += mtsdf->boundsY;
                }
                const std::uint32_t shId = AllocateMtsdfPathNode(
                    fitted.x,
                    fitted.y,
                    fitted.width,
                    fitted.height,
                    *mtsdf,
                    shCol,
                    std::max(pathElem->GetStrokeThickness(), localBlurR * 0.25f),
                    effOpacity,
                    shadowOwner);
                nodes_[shId].softFeatherPx = screenBlurR;
                applyNodeTransform(shId);
                ++emittedCount;
            }
        } else if (const auto* tbShadow = elem->DynamicCast<TextBlock>();
                   tbShadow != nullptr && !tbShadow->HasControlTemplate() && tbShadow->FirstChild() == nullptr) {
            const std::string shText = tbShadow->GetText();
            if (!shText.empty()) {
                const Thickness pad = tbShadow->GetPadding();
                const float fs = tbShadow->GetFontSize();
                const std::string ff = tbShadow->GetFontFamily();
                const TextWrapping wm = tbShadow->GetTextWrapping();
                const float effBoxW = (w > 0.0f) ? w : tbShadow->DesiredSize().width;
                const float maxTW = (wm != TextWrapping::NoWrap && effBoxW > 0.0f)
                    ? std::max(1.0f, effBoxW - (pad.left + pad.right))
                    : 1000000.0f;
                const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        r.x + pad.left + sdx,
                        r.y + pad.top + sdy,
                        fs,
                        shText,
                        shCol,
                        effOpacity,
                        curState,
                        shadowOwner,
                        ff,
                        maxTW,
                        wm,
                        tbShadow->GetTextAlignment(),
                        innerAlignW));
            }
        } else {
            const bool hasOwnOpaqueSurface =
                !elem->HasControlTemplate() &&
                (elem->IsA<Rectangle>() || elem->IsA<Ellipse>() || elem->IsA<Image>() || elem->IsA<Button>() ||
                 (ExtractGradBrushId(elem->GetValue(PropId_BackgroundGradientId)) != 0u) ||
                 !ExtractContainerImageBrushUri(elem).empty() ||
                 (value::TryGetColor(elem->GetValue(PropId_Background)).value_or(Color{0, 0, 0, 0}).a > 0.01f));
            if (hasOwnOpaqueSurface && w > 0.0f && h > 0.0f) {
                const float pad = std::ceil(localBlurR);
                const CornerRadius cr = SanitizeCornerRadius(
                    value::TryGetCornerRadius(elem->GetValue(PropId_CornerRadius))
                        .value_or(CornerRadius{}));
                const std::uint32_t shId = AllocateRoundedRectNode(
                    r.x + sdx - pad,
                    r.y + sdy - pad,
                    w + 2.0f * pad,
                    h + 2.0f * pad,
                    shCol,
                    cr,
                    shadowOwner);
                nodes_[shId].primType = AeroPrimType::SdfRoundedRect;
                nodes_[shId].opacity = effOpacity;
                nodes_[shId].softFeatherPx = localBlurR;
                nodes_[shId].sdfPadPx = pad;
                if (IsUniformCornerRadius(cr)) {
                    const auto qPad = static_cast<std::uint16_t>(std::clamp(pad * 2.0f + 0.5f, 0.0f, 2047.0f)) & 0x07FFu;
                    nodes_[shId].encodedPrimExtra = static_cast<std::uint16_t>(qPad << 5u);
                }
                applyNodeTransform(shId);
                ++emittedCount;
            } else {
                // Transparent container (e.g. Viewbox/Canvas/StackPanel/templated Control): propagate shadow to child shapes!
                propagateShadowToChildren = true;
            }
        }
    }

    if (curState.shadowOnlyPass) {
        if (propagateShadowToChildren) {
            RenderTraversalState childShadowState = curState;
            const UnifiedValue clipVal = elem->GetValue(PropId_ClipToBounds);
            if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
                const Matrix2D worldM = MatrixFromState(
                    curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty);
                Rect rawClip = elem->GetLocalClipBounds();
                if (r.width <= 0.0f && r.height <= 0.0f && (w > 0.0f || h > 0.0f)) {
                    rawClip = Rect{r.x, r.y, w, h};
                }
                const Rect localBounds = curState.hasPerspective3D
                    ? curState.world3D.TransformRectBounds(rawClip)
                    : worldM.TransformRectBounds(rawClip);
                childShadowState.clipRect = childShadowState.clipActive
                    ? IntersectRect(childShadowState.clipRect, localBounds)
                    : localBounds;
                childShadowState.clipActive = true;
            }
            const Matrix2D childContainerTf = elem->GetChildContainerTransform2D();
            ApplyStateMatrix(
                childShadowState.m00, childShadowState.m01, childShadowState.m10, childShadowState.m11,
                childShadowState.tx, childShadowState.ty,
                childContainerTf);
            if (!childContainerTf.IsIdentity()) {
                if (childShadowState.hasPerspective3D) {
                    childShadowState.world3D = Matrix4x4::ComposePlanarHomography(
                        childShadowState.world3D,
                        Matrix4x4::FromMatrix2D(childContainerTf));
                } else {
                    childShadowState.world3D = Matrix4x4::FromMatrix2D(
                        MatrixFromState(
                            childShadowState.m00, childShadowState.m01,
                            childShadowState.m10, childShadowState.m11,
                            childShadowState.tx, childShadowState.ty));
                }
            }
            if (!elem->IsA<ComboBox>() || elem->HasControlTemplate()) {
                const auto* selfExpander = elem->DynamicCast<Expander>();
                const auto* selfTabControl = elem->DynamicCast<TabControl>();
                for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
                    if (selfExpander && selfExpander->IsBodyChildHidden(child)) continue;
                    if (selfTabControl && selfTabControl->IsChildInactiveTabPage(child)) {
                        if (const auto* childHcc = child->DynamicCast<HeaderedContentControl>()) {
                            if (const UIElement* hdrElem = childHcc->GetHeaderElement()) {
                                TraverseAndEmitElement(hdrElem, childShadowState);
                            }
                        }
                        continue;
                    }
                    TraverseAndEmitElement(child, childShadowState);
                }
            }
        }
        activeClipRect_ = parentState.clipRect;
        activeClipEnabled_ = parentState.clipActive;
        activeBlendMode_ = parentState.blendMode;
        return;
    }

    if (elem->HasControlTemplate()) {
        const std::uint32_t anchorId = AllocateRenderNode(
            r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
        nodes_[anchorId].opacity = 0.0f;
        applyNodeTransform(anchorId);
        ++emittedCount;
    } else if (const auto* cp = elem->DynamicCast<ContentPresenter>()) {
        const std::uint32_t anchorId = AllocateRenderNode(
            r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
        nodes_[anchorId].opacity = 0.0f;
        applyNodeTransform(anchorId);
        ++emittedCount;

        if (cp->FirstChild() == nullptr) {
            const std::string text = cp->GetEffectiveText();
            if (!text.empty()) {
                const float fontSize = cp->GetEffectiveFontSize();
                const Color fg = cp->GetEffectiveForeground();
                const std::string fontFamily = cp->GetEffectiveFontFamily();
                const TextWrapping wrapMode = cp->GetEffectiveTextWrapping();
                const Thickness pad = cp->GetPadding();
                const float effBoxW = (w > 0.0f) ? w : cp->DesiredSize().width;
                const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
                    ? std::max(1.0f, effBoxW - (pad.left + pad.right))
                    : 1000000.0f;
                const Size textMetrics = MeasureUtf8TextRun(text, fontSize, fontFamily, maxTextW, wrapMode);
                const Point textOrigin = cp->ComputeTextOrigin(Rect{r.x, r.y, w, h}, textMetrics);
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        textOrigin.x,
                        textOrigin.y,
                        fontSize,
                        text,
                        fg,
                        effOpacity,
                        curState,
                        elem,
                        fontFamily,
                        maxTextW,
                        wrapMode));
            }
        }
    } else if (const auto* rectElem = elem->DynamicCast<Rectangle>()) {
        const std::uint32_t fillGradId = rectElem->GetFillGradientBrushId();
        const bool hasFill = rectElem->HasFill();
        const Color fillCol = rectElem->GetFill();
        const float strokeW = rectElem->GetStrokeThickness();
        const bool hasStroke = rectElem->HasStroke() && (strokeW > 0.0f);
        const Color strokeCol = rectElem->GetStroke();
        const CornerRadius cr{rectElem->EffectiveCornerRadius()};
        const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
            Rect{r.x, r.y, w, h},
            cr,
            hasFill,
            fillCol,
            hasStroke,
            strokeCol,
            Thickness{strokeW});

        if (fillGradId != 0u) {
            const std::uint32_t fillId = AllocateGradientRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                fillGradId,
                plan.fillRadii,
                false,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                plan.hasVisibleFill ? effOpacity : 0.0f,
                elem);
            applyNodeTransform(fillId);
            ++emittedCount;
        } else {
            const std::uint32_t fillId = AllocateRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                plan.fillColor,
                plan.fillRadii,
                elem);
            nodes_[fillId].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
            applyNodeTransform(fillId);
            ++emittedCount;
        }
        if (plan.reserveBorderNode) {
            const std::uint32_t brdId = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[brdId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
            applyNodeTransform(brdId);
            ++emittedCount;
        }
    } else if (const auto* ellElem = elem->DynamicCast<Ellipse>()) {
        const std::uint32_t fillGradId = ellElem->GetFillGradientBrushId();
        const bool hasFill = ellElem->HasFill();
        const Color fillCol = ellElem->GetFill();
        const float strokeW = ellElem->GetStrokeThickness();
        const bool hasStroke = ellElem->HasStroke() && (strokeW > 0.0f);
        const Color strokeCol = ellElem->GetStroke();
        const float fillInset = (hasFill && hasStroke)
            ? ((strokeCol.a >= 0.999f && strokeW >= 1.0f) ? (0.5f * strokeW) : strokeW)
            : 0.0f;
        const Rect fillRect{
            r.x + fillInset,
            r.y + fillInset,
            std::max(0.0f, w - 2.0f * fillInset),
            std::max(0.0f, h - 2.0f * fillInset)};

        if (fillGradId != 0u) {
            const std::uint32_t fillId = AllocateGradientRoundedRectNode(
                fillRect.x,
                fillRect.y,
                fillRect.width,
                fillRect.height,
                fillGradId,
                CornerRadius{},
                true,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                hasFill ? effOpacity : 0.0f,
                elem);
            applyNodeTransform(fillId);
            ++emittedCount;
        } else {
            const std::uint32_t fillId = AllocateEllipseNode(
                fillRect.x,
                fillRect.y,
                fillRect.width,
                fillRect.height,
                fillCol,
                0.0f,
                false,
                hasFill ? effOpacity : 0.0f,
                elem);
            applyNodeTransform(fillId);
            ++emittedCount;
        }
        if (hasStroke) {
            const std::uint32_t strokeId = AllocateEllipseNode(
                r.x,
                r.y,
                w,
                h,
                strokeCol,
                strokeW,
                true,
                effOpacity,
                elem);
            applyNodeTransform(strokeId);
            ++emittedCount;
        }
    } else if (const auto* lineElem = elem->DynamicCast<Line>()) {
        float x1 = r.x + lineElem->GetX1();
        float y1 = r.y + lineElem->GetY1();
        float x2 = r.x + lineElem->GetX2();
        float y2 = r.y + lineElem->GetY2();
        const bool projected3D = curState.hasPerspective3D;
        if (projected3D) {
            const Point p1 = curState.world3D.ProjectPoint2D(Point{x1, y1});
            const Point p2 = curState.world3D.ProjectPoint2D(Point{x2, y2});
            x1 = p1.x;
            y1 = p1.y;
            x2 = p2.x;
            y2 = p2.y;
        }
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float segLen = std::sqrt(dx * dx + dy * dy);
        const float strokeW = std::max(1.0f, lineElem->GetStrokeThickness());
        const bool hasVis = lineElem->HasStroke() && std::isfinite(segLen) && (segLen > 1e-3f);
        const Color strokeCol = lineElem->GetStroke();

        const float ux = (segLen > 1e-4f) ? (dx / segLen) : 1.0f;
        const float uy = (segLen > 1e-4f) ? (dy / segLen) : 0.0f;
        const float px0 = x1 + 0.5f * strokeW * uy;
        const float py0 = y1 - 0.5f * strokeW * ux;

        const std::uint32_t segId = AllocateRoundedRectNode(
            px0,
            py0,
            hasVis ? segLen : 0.0f,
            hasVis ? strokeW : 0.0f,
            strokeCol,
            CornerRadius{strokeW * 0.5f},
            elem);
        RenderNodeDescriptor& nd = nodes_[segId];
        nd.opacity = hasVis ? effOpacity : 0.0f;
        const Matrix2D segM{
            ux,
            -uy,
            uy,
            ux,
            px0 - (ux * px0 - uy * py0),
            py0 - (uy * px0 + ux * py0)};
        const Matrix2D worldM = projected3D
            ? segM
            : Matrix2D::Multiply(
                  MatrixFromState(curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty),
                  segM);
        nd.m00 = worldM.m00;
        nd.m01 = worldM.m01;
        nd.m10 = worldM.m10;
        nd.m11 = worldM.m11;
        nd.baseTx = worldM.tx;
        nd.baseTy = worldM.ty;
        nd.tx = worldM.tx + nd.x * (worldM.m00 - 1.0f) + nd.y * worldM.m01;
        nd.ty = worldM.ty + nd.x * worldM.m10 + nd.y * (worldM.m11 - 1.0f);
        nd.inheritedOpacity = curState.inheritedOpacity;
        nd.hasPerspective3D = false;
        WriteQuadVertices(nd);
        MarkPageDirty(nd.pageIndex, nd.vertexOffsetInPage);
        ++emittedCount;
    } else if (const auto* pb = elem->DynamicCast<ProgressBar>()) {
        const float effAlpha = pb->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(pb->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        const Color trackColor = value::TryGetColor(pb->GetValue(PropId_Background))
            .value_or(Color{0.15f, 0.19f, 0.26f, 1.0f});
        const std::uint32_t trackGradId = ExtractGradBrushId(pb->GetValue(PropId_BackgroundGradientId));
        if (trackGradId != 0u) {
            const std::uint32_t tid = AllocateGradientRoundedRectNode(
                r.x, r.y, w, h, trackGradId, cr, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effAlpha, elem);
            applyNodeTransform(tid);
            ++emittedCount;
        } else {
            const std::uint32_t tid = AllocateRoundedRectNode(r.x, r.y, w, h, trackColor, cr, elem);
            nodes_[tid].opacity = (trackColor.a > 0.0f) ? effAlpha : 0.0f;
            applyNodeTransform(tid);
            ++emittedCount;
        }

        const Rect fillRect = pb->ComputeFillBarLocalRect();
        const bool hasFill = (fillRect.width > 0.01f && fillRect.height > 0.01f);
        const Color fillColor = value::TryGetColor(pb->GetValue(PropId_Foreground))
            .value_or(Color{0.18f, 0.68f, 0.42f, 1.0f});
        const std::uint32_t fgGradId = ExtractGradBrushId(pb->GetValue(PropId_ForegroundGradientId));
        if (fgGradId != 0u) {
            const std::uint32_t fid = AllocateGradientRoundedRectNode(
                fillRect.x,
                fillRect.y,
                hasFill ? fillRect.width : 0.0f,
                hasFill ? fillRect.height : 0.0f,
                fgGradId,
                cr,
                false,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                hasFill ? effAlpha : 0.0f,
                elem);
            applyNodeTransform(fid);
            ++emittedCount;
        } else {
            const std::uint32_t fid = AllocateRoundedRectNode(
                fillRect.x,
                fillRect.y,
                hasFill ? fillRect.width : 0.0f,
                hasFill ? fillRect.height : 0.0f,
                fillColor,
                cr,
                elem);
            nodes_[fid].opacity = hasFill ? effAlpha : 0.0f;
            applyNodeTransform(fid);
            ++emittedCount;
        }
    } else if (const auto* expElem = elem->DynamicCast<Expander>()) {
        const float effAlpha = expElem->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        const Rect hdrR = expElem->HeaderLocalRect();
        Color hdrBg = value::TryGetColor(expElem->GetValue(PropId_Background))
            .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
        hdrBg = ModulateInteractiveSurfaceColor(hdrBg, expElem);

        const std::uint32_t hdrId = AllocateRoundedRectNode(
            hdrR.x, hdrR.y, hdrR.width, hdrR.height, hdrBg, CornerRadius{4.0f}, elem);
        nodes_[hdrId].opacity = effAlpha;
        applyNodeTransform(hdrId);
        ++emittedCount;

        // Chevron indicator (expanded vs collapsed cue)
        const float chevW = expElem->IsExpanded() ? 8.0f : 6.0f;
        const float chevH = expElem->IsExpanded() ? 6.0f : 8.0f;
        const std::uint32_t chevId = AllocateRoundedRectNode(
            hdrR.x + 8.0f,
            hdrR.y + std::max(0.0f, (hdrR.height - chevH) * 0.5f),
            chevW,
            chevH,
            Color{0.78f, 0.86f, 0.98f, 1.0f},
            CornerRadius{1.5f},
            elem);
        nodes_[chevId].opacity = effAlpha;
        applyNodeTransform(chevId);
        ++emittedCount;

        if (expElem->IsExpanded()) {
            const Rect bodyR = expElem->ContentLocalRect();
            const Color brdCol = value::TryGetColor(expElem->GetValue(PropId_BorderBrush))
                .value_or(Color{0.26f, 0.34f, 0.45f, 0.85f});
            const std::uint32_t bodyBrdId = AllocateBorderRingNode(
                bodyR.x,
                bodyR.y,
                bodyR.width,
                bodyR.height,
                brdCol,
                CornerRadius{0.0f, 0.0f, 4.0f, 4.0f},
                Thickness{1.0f},
                elem);
            nodes_[bodyBrdId].opacity = effAlpha;
            applyNodeTransform(bodyBrdId);
            ++emittedCount;
        }

        if (expElem->GetHeaderElement() == nullptr) {
            const std::string hdrText = expElem->GetHeader();
            if (!hdrText.empty()) {
                float fontSize = 14.0f;
                (void)TryGetFloat(expElem->GetValue(PropId_FontSize), fontSize);
                const Color fg = value::TryGetColor(expElem->GetValue(PropId_Foreground))
                    .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        hdrR.x + 22.0f,
                        hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                        fontSize,
                        hdrText,
                        fg,
                        effAlpha,
                        curState,
                        elem,
                        ResolveElementFontFamily(expElem)));
            }
        }
    } else if (const auto* tabCtrl = elem->DynamicCast<TabControl>()) {
        const float effAlpha = tabCtrl->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        const Rect pageR = tabCtrl->ContentPanelLocalRect();
        const Color pageBg = value::TryGetColor(tabCtrl->GetValue(PropId_Background))
            .value_or(Color{0.12f, 0.15f, 0.21f, 1.0f});
        const Color pageBrd = value::TryGetColor(tabCtrl->GetValue(PropId_BorderBrush))
            .value_or(Color{0.28f, 0.36f, 0.48f, 1.0f});

        const std::uint32_t pageBgId = AllocateRoundedRectNode(
            pageR.x, pageR.y, pageR.width, pageR.height, pageBg, CornerRadius{0.0f, 4.0f, 4.0f, 4.0f}, elem);
        nodes_[pageBgId].opacity = (pageBg.a > 0.0f) ? effAlpha : 0.0f;
        applyNodeTransform(pageBgId);
        ++emittedCount;

        const std::uint32_t pageBrdId = AllocateBorderRingNode(
            pageR.x, pageR.y, pageR.width, pageR.height, pageBrd, CornerRadius{0.0f, 4.0f, 4.0f, 4.0f}, Thickness{1.0f}, elem);
        nodes_[pageBrdId].opacity = effAlpha;
        applyNodeTransform(pageBrdId);
        ++emittedCount;

        const std::size_t tabCount = tabCtrl->TabCount();
        const std::int32_t activeIdx = tabCtrl->GetSelectedIndex();
        float fontSize = 14.0f;
        (void)TryGetFloat(tabCtrl->GetValue(PropId_FontSize), fontSize);
        const std::string tabFont = ResolveElementFontFamily(tabCtrl);

        for (std::size_t i = 0u; i < tabCount; ++i) {
            const Rect tr = tabCtrl->GetTabHeaderLocalRect(i);
            const bool isActive = (static_cast<std::int32_t>(i) == activeIdx);
            const Color tabBg = isActive
                ? Color{0.22f, 0.42f, 0.72f, 1.0f}
                : Color{0.15f, 0.19f, 0.26f, 1.0f};
            const std::uint32_t tid = AllocateRoundedRectNode(
                tr.x,
                tr.y,
                std::max(0.0f, tr.width - 2.0f),
                tr.height,
                tabBg,
                CornerRadius{5.0f, 5.0f, 0.0f, 0.0f},
                elem);
            nodes_[tid].opacity = effAlpha;
            applyNodeTransform(tid);
            ++emittedCount;

            const UIElement* tabElem = tabCtrl->GetTabElement(i);
            const auto* hcc = tabElem ? tabElem->DynamicCast<HeaderedContentControl>() : nullptr;
            if (!hcc || hcc->GetHeaderElement() == nullptr) {
                const std::string hdrLabel = tabCtrl->GetTabHeaderText(i);
                if (!hdrLabel.empty()) {
                    const Size lm = MeasureUtf8TextRun(hdrLabel, fontSize, tabFont);
                    const float tx = tr.x + std::max(6.0f, (tr.width - lm.width) * 0.5f);
                    const float ty = tr.y + std::max(2.0f, (tr.height - fontSize * 1.2f) * 0.5f);
                    const Color fg = isActive
                        ? Color{0.98f, 0.99f, 1.0f, 1.0f}
                        : Color{0.76f, 0.82f, 0.90f, 1.0f};
                    emittedCount = static_cast<std::uint16_t>(
                        emittedCount + EmitTextRunNodes(
                            tx, ty, fontSize, hdrLabel, fg, effAlpha, curState, elem, tabFont));
                }
            }
        }
    } else if (const auto* grpBox = elem->DynamicCast<GroupBox>()) {
        const Rect brdR = grpBox->BodyBorderLocalRect();
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(grpBox->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        const Thickness bt = SanitizeThickness(
            value::TryGetThickness(grpBox->GetValue(PropId_BorderThickness))
                .value_or(Thickness{1.0f}));
        const auto bgOpt = value::TryGetColor(grpBox->GetValue(PropId_Background));
        const bool hasBg = bgOpt.has_value() && (bgOpt->a > 0.0f);
        const Color brdCol = value::TryGetColor(grpBox->GetValue(PropId_BorderBrush))
            .value_or(Color{0.30f, 0.38f, 0.48f, 1.0f});

        const std::uint32_t bgGradId = ExtractGradBrushId(grpBox->GetValue(PropId_BackgroundGradientId));
        if (bgGradId != 0u) {
            const std::uint32_t fid = AllocateGradientRoundedRectNode(
                brdR.x, brdR.y, brdR.width, brdR.height, bgGradId, cr, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effOpacity, elem);
            applyNodeTransform(fid);
            ++emittedCount;
        } else {
            const std::uint32_t fid = AllocateRoundedRectNode(
                brdR.x, brdR.y, brdR.width, brdR.height,
                bgOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}), cr, elem);
            nodes_[fid].opacity = hasBg ? effOpacity : 0.0f;
            applyNodeTransform(fid);
            ++emittedCount;
        }

        const std::uint32_t bid = AllocateBorderRingNode(
            brdR.x, brdR.y, brdR.width, brdR.height, brdCol, cr, bt, elem);
        nodes_[bid].opacity = (brdCol.a > 0.0f && HasPositiveThickness(bt)) ? effOpacity : 0.0f;
        applyNodeTransform(bid);
        ++emittedCount;

        if (grpBox->GetHeaderElement() == nullptr) {
            const std::string hdrText = grpBox->GetHeader();
            if (!hdrText.empty()) {
                const Rect hdrR = grpBox->HeaderLocalRect();
                float fontSize = 14.0f;
                (void)TryGetFloat(grpBox->GetValue(PropId_FontSize), fontSize);
                const Color fg = value::TryGetColor(grpBox->GetValue(PropId_Foreground))
                    .value_or(Color{0.92f, 0.96f, 1.0f, 1.0f});
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        hdrR.x + 2.0f,
                        hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                        fontSize,
                        hdrText,
                        fg,
                        effOpacity,
                        curState,
                        elem,
                        ResolveElementFontFamily(grpBox)));
            }
        }
    } else if (const auto* hcc = elem->DynamicCast<HeaderedContentControl>();
               hcc != nullptr && !elem->IsA<TabItem>()) {
        const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(elem, Rect{r.x, r.y, w, h});
        const std::uint32_t bgGradId = ExtractGradBrushId(hcc->GetValue(PropId_BackgroundGradientId));
        if (bgGradId != 0u) {
            const std::uint32_t fid = AllocateGradientRoundedRectNode(
                plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
                bgGradId, plan.fillRadii, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effOpacity, elem);
            applyNodeTransform(fid);
            ++emittedCount;
        } else {
            const std::uint32_t fid = AllocateRoundedRectNode(
                plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
                plan.fillColor, plan.fillRadii, elem);
            nodes_[fid].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
            applyNodeTransform(fid);
            ++emittedCount;
        }
        if (plan.reserveBorderNode) {
            const std::uint32_t bid = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[bid].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
            applyNodeTransform(bid);
            ++emittedCount;
        }
        if (hcc->GetHeaderElement() == nullptr) {
            const std::string hdrText = hcc->GetHeader();
            if (!hdrText.empty()) {
                const Rect hdrR = hcc->HeaderLocalRect();
                float fontSize = 14.0f;
                (void)TryGetFloat(hcc->GetValue(PropId_FontSize), fontSize);
                const Color fg = value::TryGetColor(hcc->GetValue(PropId_Foreground))
                    .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        hdrR.x + 2.0f,
                        hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                        fontSize,
                        hdrText,
                        fg,
                        effOpacity,
                        curState,
                        elem,
                        ResolveElementFontFamily(hcc)));
            }
        }
    } else if (const auto* pathElem = elem->DynamicCast<Path>()) {
        std::string pathStr = pathElem->GetData();
        if (pathStr.empty()) {
            const UnifiedValue ptsVal = pathElem->GetValue(PropId_ShapePoints);
            if (const auto* sv = std::get_if<StringValue>(&ptsVal); sv && !sv->view().empty()) {
                pathStr = ConvertPointsToSvgPath(sv->view(), elem->IsA<Polygon>());
            }
        }
        if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
            Rect fitted = ComputeStretchedDestRect(
                Rect{r.x, r.y, w, h},
                mtsdf->boundsW,
                mtsdf->boundsH,
                pathElem->GetStretch());
            if (pathElem->GetStretch() == Stretch::None) {
                fitted.x += mtsdf->boundsX;
                fitted.y += mtsdf->boundsY;
            }
            const float strokeW = pathElem->GetStrokeThickness();
            const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
            const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
            const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
            bool emitFill = true;
            bool emitSeparateStroke = false;
            ClassifyPathEmission(*pathElem, emitFill, emitSeparateStroke);
            const std::uint32_t fillGradId = ExtractGradBrushId(pathElem->GetValue(PropId_BackgroundGradientId));
            const std::uint32_t strokeGradId = ExtractGradBrushId(pathElem->GetValue(PropId_BorderGradientId));

            if (!emitFill && !emitSeparateStroke) {
                const std::uint32_t nid = AllocateRenderNode(
                    r.x, r.y, w, h, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
                nodes_[nid].opacity = 0.0f;
                applyNodeTransform(nid);
                ++emittedCount;
            }
            if (emitFill) {
                const float inlineStrokeW = emitSeparateStroke ? 0.0f : strokeW;
                const std::uint32_t nid = AllocateMtsdfPathNode(
                    fitted.x,
                    fitted.y,
                    fitted.width,
                    fitted.height,
                    *mtsdf,
                    pathElem->GetFill(),
                    inlineStrokeW,
                    effOpacity,
                    elem);
                nodes_[nid].gradientBrushId = fillGradId;
                nodes_[nid].encodedPrimExtra = (fillGradId != 0u)
                    ? static_cast<std::uint16_t>((std::min<std::uint32_t>(fillGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                    : 0u;
                nodes_[nid].encodedSdfParams = PackMtsdfParams(
                    mtsdf->pxRange,
                    mtsdf->atlasWidth,
                    inlineStrokeW * worldStrokeScale,
                    false);
                applyNodeTransform(nid);
                ++emittedCount;
            }
            if (emitSeparateStroke) {
                const std::uint32_t strokeNid = AllocateMtsdfPathNode(
                    fitted.x,
                    fitted.y,
                    fitted.width,
                    fitted.height,
                    *mtsdf,
                    pathElem->GetStroke(),
                    strokeW,
                    effOpacity,
                    elem);
                nodes_[strokeNid].gradientBrushId = strokeGradId;
                nodes_[strokeNid].encodedPrimExtra = (strokeGradId != 0u)
                    ? static_cast<std::uint16_t>((std::min<std::uint32_t>(strokeGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                    : 0u;
                nodes_[strokeNid].encodedSdfParams = PackMtsdfParams(
                    mtsdf->pxRange,
                    mtsdf->atlasWidth,
                    strokeW * worldStrokeScale,
                    true);
                applyNodeTransform(strokeNid);
                ++emittedCount;
            }
        } else {
            const std::uint32_t nid = AllocateRenderNode(
                r.x, r.y, 0.0f, 0.0f, pathElem->GetFill(), 0.0f, 0.0f, elem);
            nodes_[nid].opacity = 0.0f;
            applyNodeTransform(nid);
            ++emittedCount;
        }
    } else if (const auto* imgElem = elem->DynamicCast<Image>()) {
        const Thickness pad = imgElem->GetPadding();
        const Rect contentBox{
            r.x + pad.left,
            r.y + pad.top,
            std::max(0.0f, w - (pad.left + pad.right)),
            std::max(0.0f, h - (pad.top + pad.bottom))};
        const std::string srcUri = imgElem->GetSource();
        const ImageAtlasEntry* atlasEntry = srcUri.empty()
            ? nullptr
            : ImageAtlas::Instance().LoadOrGet(srcUri);
        const Color tintColor = value::TryGetColor(imgElem->GetValue(PropId_Foreground))
            .value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});

        if (atlasEntry && atlasEntry->width > 0u && atlasEntry->height > 0u) {
            const ImageStretchPlacement placement = ComputeImageStretchPlacement(
                contentBox,
                static_cast<float>(atlasEntry->width),
                static_cast<float>(atlasEntry->height),
                imgElem->GetStretch(),
                imgElem->GetStretchDirection(),
                atlasEntry->u0,
                atlasEntry->v0,
                atlasEntry->u1,
                atlasEntry->v1);
            if (placement.valid) {
                const std::uint32_t nid = AllocateTexturedNode(
                    placement.destRect.x,
                    placement.destRect.y,
                    placement.destRect.width,
                    placement.destRect.height,
                    placement.u0,
                    placement.v0,
                    placement.u1,
                    placement.v1,
                    tintColor,
                    effOpacity,
                    elem,
                    atlasEntry->textureId);
                applyNodeTransform(nid);
                ++emittedCount;
            } else {
                const std::uint32_t nid = AllocateTexturedNode(
                    contentBox.x,
                    contentBox.y,
                    0.0f,
                    0.0f,
                    atlasEntry->u0,
                    atlasEntry->v0,
                    atlasEntry->u1,
                    atlasEntry->v1,
                    tintColor,
                    0.0f,
                    elem,
                    atlasEntry->textureId);
                applyNodeTransform(nid);
                ++emittedCount;
            }
        } else {
            const std::uint32_t nid = AllocateTexturedNode(
                r.x,
                r.y,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                1.0f,
                tintColor,
                0.0f,
                elem,
                0u);
            applyNodeTransform(nid);
            ++emittedCount;
        }
    } else if (const auto* slider = elem->DynamicCast<Slider>()) {
        const SliderVisualGeometry geom = ComputeSliderVisualGeometry(*slider, r, w, h);
        const float effAlpha = slider->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        const Color trackColor = value::TryGetColor(slider->GetValue(PropId_Background))
            .value_or(Color{0.16f, 0.20f, 0.26f, 1.0f});
        const Color fillColor = value::TryGetColor(slider->GetValue(PropId_Foreground))
            .value_or(Color{0.24f, 0.56f, 0.96f, 1.0f});
        const Color thumbColor = ModulateInteractiveSurfaceColor(
            Color{0.92f, 0.96f, 1.0f, 1.0f},
            slider);
        const float thumbBorder = slider->IsFocused() ? 2.0f : 1.0f;

        const std::uint32_t trackId = AllocateRenderNode(
            geom.trackRect.x, geom.trackRect.y, geom.trackRect.width, geom.trackRect.height,
            trackColor, 3.0f, 0.0f, elem);
        nodes_[trackId].opacity = effAlpha;
        applyNodeTransform(trackId);

        const std::uint32_t fillId = AllocateRenderNode(
            geom.fillRect.x, geom.fillRect.y, geom.fillRect.width, geom.fillRect.height,
            fillColor, 3.0f, 0.0f, elem);
        nodes_[fillId].opacity = effAlpha;
        applyNodeTransform(fillId);

        const std::uint32_t thumbId = AllocateRenderNode(
            geom.thumbRect.x, geom.thumbRect.y, geom.thumbRect.width, geom.thumbRect.height,
            thumbColor, geom.thumbRect.width * 0.5f, thumbBorder, elem);
        nodes_[thumbId].opacity = effAlpha;
        applyNodeTransform(thumbId);

        emittedCount = static_cast<std::uint16_t>(emittedCount + 3u);
    } else if (const auto* tbox = elem->DynamicCast<TextBox>()) {
        const float effAlpha = tbox->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color boxBg = value::TryGetColor(tbox->GetValue(PropId_Background))
            .value_or(Color{0.12f, 0.15f, 0.20f, 1.0f});
        boxBg = ModulateInteractiveSurfaceColor(boxBg, tbox);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(tbox->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        Thickness btRaw = SanitizeThickness(
            value::TryGetThickness(tbox->GetValue(PropId_BorderThickness))
                .value_or(Thickness{1.0f}));
        if (tbox->IsFocused()) {
            btRaw = Thickness{
                std::max(2.0f, btRaw.left),
                std::max(2.0f, btRaw.top),
                std::max(2.0f, btRaw.right),
                std::max(2.0f, btRaw.bottom)};
        }
        const auto bbOpt = value::TryGetColor(tbox->GetValue(PropId_BorderBrush));
        if (bbOpt.has_value()) {
            const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
                Rect{r.x, r.y, w, h},
                cr,
                boxBg.a > 0.0f,
                boxBg,
                true,
                *bbOpt,
                btRaw);
            const std::uint32_t fillId = AllocateRoundedRectNode(
                plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
                plan.fillColor, plan.fillRadii, elem);
            nodes_[fillId].opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
            applyNodeTransform(fillId);
            ++emittedCount;

            const std::uint32_t borderId = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[borderId].opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
            applyNodeTransform(borderId);
            ++emittedCount;
        } else {
            const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
            const std::uint32_t boxId = AllocateRoundedRectNode(
                r.x, r.y, w, h, boxBg, cr, elem);
            ConfigureRoundedRectDescriptor(nodes_[boxId], cr, Thickness{legacyBt}, false);
            nodes_[boxId].opacity = (boxBg.a > 0.0f) ? effAlpha : 0.0f;
            applyNodeTransform(boxId);
            ++emittedCount;
        }

        const std::string displayStr = tbox->GetDisplayString();
        const bool showingPlaceholder = displayStr.empty();
        const std::string renderedText = showingPlaceholder ? tbox->GetPlaceholder() : displayStr;
        const Thickness pad = tbox->GetPadding();
        const float padLeft = (pad.left > 0.0f) ? pad.left : 6.0f;
        const float scrollX = showingPlaceholder ? 0.0f : tbox->HorizontalTextScrollOffset();
        const float textX = r.x + padLeft - scrollX;
        const float fontSize = tbox->GetFontSize();
        const float textY = r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f);
        const Color textColor = showingPlaceholder
            ? Color{0.55f, 0.60f, 0.68f, 0.85f}
            : tbox->GetForeground();

        emittedCount = static_cast<std::uint16_t>(
            emittedCount + EmitTextRunNodes(
                textX,
                textY,
                fontSize,
                renderedText,
                textColor,
                effAlpha,
                curState,
                elem,
                tbox->GetFontFamily()));

        const Rect caretR = tbox->CaretLocalRect();
        const bool showCaret = tbox->IsFocused() && !tbox->IsReadOnly() && tbox->IsEffectivelyEnabled();
        const std::uint32_t caretId = AllocateRenderNode(
            caretR.x,
            caretR.y,
            showCaret ? caretR.width : 0.0f,
            showCaret ? caretR.height : 0.0f,
            Color{0.38f, 0.76f, 1.0f, 1.0f},
            0.0f,
            0.0f,
            elem);
        nodes_[caretId].opacity = showCaret ? effAlpha : 0.0f;
        applyNodeTransform(caretId);
        ++emittedCount;
    } else if (const auto* tb = elem->DynamicCast<TextBlock>()) {
        const auto bgOpt = value::TryGetColor(tb->GetValue(PropId_Background));
        const bool hasBg = bgOpt.has_value() && bgOpt->a > 0.0f;
        const auto bbOpt = value::TryGetColor(tb->GetValue(PropId_BorderBrush));
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(tb->GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));
        const Thickness bt = SanitizeThickness(
            value::TryGetThickness(tb->GetValue(PropId_BorderThickness)).value_or(Thickness{}));
        const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
            Rect{r.x, r.y, w, h},
            cr,
            hasBg,
            bgOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
            bbOpt.has_value(),
            bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
            bt);
        if (hasBg) {
            const std::uint32_t bgId = AllocateRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                plan.fillColor,
                plan.fillRadii,
                elem);
            nodes_[bgId].opacity = effOpacity;
            applyNodeTransform(bgId);
            ++emittedCount;
        }
        if (bbOpt.has_value()) {
            const std::uint32_t brdId = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[brdId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
            applyNodeTransform(brdId);
            ++emittedCount;
        }
        if (tb->FirstChild() == nullptr) {
            std::string text = tb->GetText();
            if (text.empty()) {
                const UnifiedValue phVal = tb->GetValue(PropId_Placeholder);
                if (const auto* ph = std::get_if<StringValue>(&phVal)) {
                    text = ph->str();
                }
            }
            const Thickness pad = tb->GetPadding();
            const float fontSize = tb->GetFontSize();
            const Color fg = tb->GetForeground();
            const std::string fontFamily = tb->GetFontFamily();
            const TextWrapping wrapMode = tb->GetTextWrapping();
            const float effBoxW = (w > 0.0f) ? w : tb->DesiredSize().width;
            const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
                ? std::max(1.0f, effBoxW - (pad.left + pad.right))
                : 1000000.0f;
            const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
            std::vector<RichGlyphStyle> richStyles;
            const std::vector<RichGlyphStyle>* richPtr = nullptr;
            if (tb->HasRichText()) {
                const std::string rawRich = tb->GetRawRichText();
                if (!rawRich.empty()) {
                    ParseRichTextToPlainAndStyles(rawRich, fg, elem, text, richStyles);
                    richPtr = &richStyles;
                }
            }
            emittedCount = static_cast<std::uint16_t>(
                emittedCount + EmitTextRunNodes(
                    r.x + pad.left,
                    r.y + pad.top,
                    fontSize,
                    text,
                    fg,
                    effOpacity,
                    curState,
                    elem,
                    fontFamily,
                    maxTextW,
                    wrapMode,
                    tb->GetTextAlignment(),
                    innerAlignW,
                    richPtr));
        } else if (emittedCount == 0u) {
            const std::uint32_t anchorId = AllocateRenderNode(
                r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
            nodes_[anchorId].opacity = 0.0f;
            applyNodeTransform(anchorId);
            ++emittedCount;
        }
    } else if (const auto* combo = elem->DynamicCast<ComboBox>()) {
        const float effAlpha = combo->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color hdrBg = value::TryGetColor(combo->GetValue(PropId_Background))
            .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
        hdrBg = ModulateInteractiveSurfaceColor(hdrBg, combo);
        const float bt = combo->IsFocused() ? 2.0f : 1.0f;

        const std::uint32_t hdrId = AllocateRenderNode(
            r.x, r.y, w, h, hdrBg, 4.0f, bt, elem);
        nodes_[hdrId].opacity = effAlpha;
        applyNodeTransform(hdrId);
        ++emittedCount;

        const std::uint32_t chevId = AllocateRenderNode(
            r.x + std::max(0.0f, w - 18.0f),
            r.y + std::max(0.0f, (h - 6.0f) * 0.5f),
            10.0f,
            6.0f,
            Color{0.78f, 0.86f, 0.98f, 1.0f},
            2.0f,
            0.0f,
            elem);
        nodes_[chevId].opacity = effAlpha;
        applyNodeTransform(chevId);
        ++emittedCount;

        float fontSize = 14.0f;
        (void)TryGetFloat(combo->GetValue(PropId_FontSize), fontSize);
        const Color fg = value::TryGetColor(combo->GetValue(PropId_Foreground))
            .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
        const std::string selText = combo->GetSelectedItemText();
        emittedCount = static_cast<std::uint16_t>(
            emittedCount + EmitTextRunNodes(
                r.x + 8.0f,
                r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f),
                fontSize,
                selText,
                fg,
                effAlpha,
                curState,
                elem,
                ResolveElementFontFamily(combo)));

        if (combo->IsDropDownOpen() && combo->IsEffectivelyEnabled() &&
            combo->GetVisibility() == Visibility::Visible && curState.inheritedOpacity > 0.0f &&
            combo->ComboItemCount() > 0u) {
            pendingComboPopups_.push_back(DeferredComboPopup{combo, curState});
        }
    } else if (elem->IsA<CheckBox>() || elem->IsA<RadioButton>()) {
        const auto* cb = elem->DynamicCast<CheckBox>();
        const auto* rb = elem->DynamicCast<RadioButton>();
        const bool isRadio = (rb != nullptr);
        const bool checked = isRadio ? rb->IsChecked() : cb->IsChecked();
        const float effAlpha = elem->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);

        const Thickness pad = elem->GetPadding();
        const float boxSize = 18.0f;
        const float ix = r.x + pad.left;
        const float iy = r.y + pad.top + std::max(0.0f, (h - (pad.top + pad.bottom) - boxSize) * 0.5f);

        const Color outerColor = ModulateInteractiveSurfaceColor(
            Color{0.16f, 0.22f, 0.30f, 1.0f},
            elem);
        const float outerCr = isRadio ? 9.0f : 4.0f;
        const float outerBt = elem->IsFocused() ? 2.0f : 1.5f;
        const std::uint32_t outerId = AllocateRenderNode(
            ix, iy, boxSize, boxSize, outerColor, outerCr, outerBt, elem);
        nodes_[outerId].opacity = effAlpha;
        applyNodeTransform(outerId);
        ++emittedCount;

        const float markSize = isRadio ? 8.0f : 10.0f;
        const float markInset = (boxSize - markSize) * 0.5f;
        const float markCr = isRadio ? 4.0f : 2.0f;
        const std::uint32_t markId = AllocateRenderNode(
            ix + markInset,
            iy + markInset,
            checked ? markSize : 0.0f,
            checked ? markSize : 0.0f,
            Color{0.26f, 0.66f, 1.0f, 1.0f},
            markCr,
            0.0f,
            elem);
        nodes_[markId].opacity = checked ? effAlpha : 0.0f;
        applyNodeTransform(markId);
        ++emittedCount;

        if (!elem->FirstChild()) {
            const auto* asBtn = static_cast<const Button*>(elem);
            const std::string label = asBtn->GetContentText();
            if (!label.empty()) {
                float fontSize = 14.0f;
                (void)TryGetFloat(elem->GetValue(PropId_FontSize), fontSize);
                const Color fg = value::TryGetColor(elem->GetValue(PropId_Foreground))
                    .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
                const float lx = ix + boxSize + 8.0f;
                const float ly = r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f);
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        lx, ly, fontSize, label, fg, effAlpha, curState, elem, ResolveElementFontFamily(elem)));
            }
        }
    } else if (const auto* btn = elem->DynamicCast<Button>()) {
        const float effAlpha = btn->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color bgColor{0.22f, 0.34f, 0.54f, 1.0f};
        if (auto bg = value::TryGetColor(btn->GetValue(PropId_Background))) {
            bgColor = *bg;
        }
        bgColor = ModulateInteractiveSurfaceColor(bgColor, btn);
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(btn->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}));
        Thickness btRaw = SanitizeThickness(
            value::TryGetThickness(btn->GetValue(PropId_BorderThickness))
                .value_or(Thickness{}));
        if (btn->IsFocused()) {
            btRaw = Thickness{
                std::max(2.0f, btRaw.left),
                std::max(2.0f, btRaw.top),
                std::max(2.0f, btRaw.right),
                std::max(2.0f, btRaw.bottom)};
        }
        const auto bbOpt = value::TryGetColor(btn->GetValue(PropId_BorderBrush));
        if (bbOpt.has_value()) {
            const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
                Rect{r.x, r.y, w, h},
                cr,
                bgColor.a > 0.0f,
                bgColor,
                true,
                *bbOpt,
                btRaw);
            const std::uint32_t fillId = AllocateRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                plan.fillColor,
                plan.fillRadii,
                elem);
            nodes_[fillId].opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
            applyNodeTransform(fillId);
            ++emittedCount;

            const std::uint32_t brdId = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[brdId].opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
            applyNodeTransform(brdId);
            ++emittedCount;
        } else {
            const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
            const std::uint32_t boxId = AllocateRoundedRectNode(
                r.x, r.y, w, h, bgColor, cr, elem);
            ConfigureRoundedRectDescriptor(nodes_[boxId], cr, Thickness{legacyBt}, false);
            nodes_[boxId].opacity = (bgColor.a > 0.0f) ? effAlpha : 0.0f;
            applyNodeTransform(boxId);
            ++emittedCount;
        }

        if (!btn->FirstChild()) {
            const std::string label = btn->GetContentText();
            if (!label.empty()) {
                Color fg{0.96f, 0.98f, 1.0f, 1.0f};
                if (auto fgOpt = value::TryGetColor(btn->GetValue(PropId_Foreground))) {
                    fg = *fgOpt;
                }
                float fontSize = 14.0f;
                (void)TryGetFloat(btn->GetValue(PropId_FontSize), fontSize);
                const std::string btnFont = ResolveElementFontFamily(btn);
                const Size labelMetrics = MeasureUtf8TextRun(label, fontSize, btnFont);
                const float lx = r.x + std::max(4.0f, (w - labelMetrics.width) * 0.5f);
                const float ly = r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f);
                emittedCount = static_cast<std::uint16_t>(
                    emittedCount + EmitTextRunNodes(
                        lx,
                        ly,
                        fontSize,
                        label,
                        fg,
                        effAlpha,
                        curState,
                        elem,
                        btnFont));
            }
        }
    } else if (elem->IsA<BoxShadow>()) {
        float boxBlurR = 8.0f;
        (void)TryGetFloat(elem->GetValue(PropId_EffectBlurRadius), boxBlurR);
        boxBlurR = std::max(1.0f, boxBlurR);
        const Color boxCol = value::TryGetColor(elem->GetValue(PropId_EffectColor))
            .value_or(value::TryGetColor(elem->GetValue(PropId_Background))
                .value_or(Color{0.0f, 0.0f, 0.0f, 0.5f}));
        const CornerRadius cr = SanitizeCornerRadius(
            value::TryGetCornerRadius(elem->GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));
        const float pad = std::ceil(boxBlurR);
        const std::uint32_t shId = AllocateRoundedRectNode(
            r.x - pad, r.y - pad, w + 2.0f * pad, h + 2.0f * pad, boxCol, cr, elem);
        nodes_[shId].primType = AeroPrimType::SdfRoundedRect;
        nodes_[shId].opacity = (w > 0.0f && h > 0.0f) ? effOpacity : 0.0f;
        nodes_[shId].softFeatherPx = boxBlurR;
        nodes_[shId].sdfPadPx = pad;
        if (IsUniformCornerRadius(cr)) {
            const auto qPad = static_cast<std::uint16_t>(std::clamp(pad * 2.0f + 0.5f, 0.0f, 2047.0f)) & 0x07FFu;
            nodes_[shId].encodedPrimExtra = static_cast<std::uint16_t>(qPad << 5u);
        }
        applyNodeTransform(shId);
        ++emittedCount;
    } else {
        const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(
            elem,
            Rect{r.x, r.y, w, h});
        const std::uint32_t bgGradId = ExtractGradBrushId(elem->GetValue(PropId_BackgroundGradientId));
        Stretch imgStretch = Stretch::Fill;
        const std::string bgImgUri = (bgGradId == 0u)
            ? ExtractContainerImageBrushUri(elem, &imgStretch)
            : std::string{};
        const ImageAtlasEntry* bgAtlas = bgImgUri.empty() ? nullptr : ImageAtlas::Instance().LoadOrGet(bgImgUri);
        float bgGradOp = 1.0f;
        (void)TryGetFloat(elem->GetValue(PropId_BackgroundOpacity), bgGradOp);
        bgGradOp = std::clamp(bgGradOp, 0.0f, 1.0f);
        if (bgGradId != 0u) {
            const std::uint32_t fillId = AllocateGradientRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                bgGradId,
                plan.fillRadii,
                false,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                effOpacity * bgGradOp,
                elem);
            applyNodeTransform(fillId);
            ++emittedCount;
        } else if (bgAtlas && bgAtlas->width > 0u && bgAtlas->height > 0u &&
                   plan.fillRect.width > 0.0f && plan.fillRect.height > 0.0f) {
            const ImageStretchPlacement pl = ComputeImageStretchPlacement(
                plan.fillRect,
                static_cast<float>(bgAtlas->width),
                static_cast<float>(bgAtlas->height),
                imgStretch,
                StretchDirection::Both,
                bgAtlas->u0, bgAtlas->v0, bgAtlas->u1, bgAtlas->v1);
            const Rect dr = pl.valid ? pl.destRect : plan.fillRect;
            const std::uint32_t fillId = AllocateTexturedNode(
                dr.x, dr.y, dr.width, dr.height,
                pl.valid ? pl.u0 : bgAtlas->u0,
                pl.valid ? pl.v0 : bgAtlas->v0,
                pl.valid ? pl.u1 : bgAtlas->u1,
                pl.valid ? pl.v1 : bgAtlas->v1,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                effOpacity * bgGradOp,
                elem,
                bgAtlas->textureId);
            applyNodeTransform(fillId);
            ++emittedCount;
        } else {
            const std::uint32_t fillId = AllocateRoundedRectNode(
                plan.fillRect.x,
                plan.fillRect.y,
                plan.fillRect.width,
                plan.fillRect.height,
                plan.fillColor,
                plan.fillRadii,
                elem);
            nodes_[fillId].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
            applyNodeTransform(fillId);
            ++emittedCount;
        }

        if (plan.reserveBorderNode) {
            const std::uint32_t borderId = AllocateBorderRingNode(
                plan.borderRect.x,
                plan.borderRect.y,
                plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
                plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
                plan.borderColor,
                plan.borderRadii,
                plan.borderThickness,
                elem);
            nodes_[borderId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
            applyNodeTransform(borderId);
            ++emittedCount;
        }
    }

    elem->BindRenderNodeSpan(treeId_, spanStartId, emittedCount);
    elem->ClearLayoutDirtyFlags(static_cast<std::uint8_t>(RenderDirty | SubtreeRenderDirty));

    // If this container specifies ClipToBounds=true, intersect transformed child clip rect before descending
    RenderTraversalState childState = curState;
    childState.hasInheritedDropShadow = false;
    childState.shadowOnlyPass = false;
    const UnifiedValue clipVal = elem->GetValue(PropId_ClipToBounds);
    if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
        const Matrix2D worldM = MatrixFromState(
            curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty);
        Rect rawClip = elem->GetLocalClipBounds();
        if (r.width <= 0.0f && r.height <= 0.0f && (w > 0.0f || h > 0.0f)) {
            rawClip = Rect{r.x, r.y, w, h};
        }
        const Rect localBounds = curState.hasPerspective3D
            ? curState.world3D.TransformRectBounds(rawClip)
            : worldM.TransformRectBounds(rawClip);
        childState.clipRect = childState.clipActive
            ? IntersectRect(childState.clipRect, localBounds)
            : localBounds;
        childState.clipActive = true;
    }
    const Matrix2D childContainerTf = elem->GetChildContainerTransform2D();
    ApplyStateMatrix(
        childState.m00, childState.m01, childState.m10, childState.m11, childState.tx, childState.ty,
        childContainerTf);
    if (!childContainerTf.IsIdentity()) {
        if (childState.hasPerspective3D) {
            childState.world3D = Matrix4x4::ComposePlanarHomography(
                childState.world3D,
                Matrix4x4::FromMatrix2D(childContainerTf));
        } else {
            childState.world3D = Matrix4x4::FromMatrix2D(
                MatrixFromState(
                    childState.m00, childState.m01, childState.m10, childState.m11, childState.tx, childState.ty));
        }
    }

    if (!elem->IsA<ComboBox>() || elem->HasControlTemplate()) {
        const auto* selfExpander = elem->DynamicCast<Expander>();
        const auto* selfTabControl = elem->DynamicCast<TabControl>();
        const auto* selfTabItem = elem->DynamicCast<TabItem>();
        const bool tabItemHostedInTabControl =
            (selfTabItem != nullptr && elem->Parent() != nullptr && elem->Parent()->IsA<TabControl>());
        const UIElement* tabItemHeaderElem = tabItemHostedInTabControl
            ? selfTabItem->GetHeaderElement()
            : nullptr;

        if (propagateShadowToChildren) {
            RenderTraversalState shadowPassState = childState;
            shadowPassState.hasInheritedDropShadow = true;
            shadowPassState.shadowOnlyPass = true;
            shadowPassState.shadowWorldDx = worldSdx;
            shadowPassState.shadowWorldDy = worldSdy;
            shadowPassState.shadowWorldBlurR = worldBlurR;
            shadowPassState.shadowColor = shCol;
            for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
                if (selfExpander && selfExpander->IsBodyChildHidden(child)) {
                    continue;
                }
                if (selfTabControl && selfTabControl->IsChildInactiveTabPage(child)) {
                    if (const auto* childHcc = child->DynamicCast<HeaderedContentControl>()) {
                        if (const UIElement* hdrElem = childHcc->GetHeaderElement()) {
                            TraverseAndEmitElement(hdrElem, shadowPassState);
                        }
                    }
                    continue;
                }
                TraverseAndEmitElement(child, shadowPassState);
            }
        }

        auto readChildZ = [](const UIElement* ch) noexcept -> std::int64_t {
            if (const auto* ze = ch->FindCompactEntry(PropId_ZIndex)) {
                if (const auto* zi = std::get_if<std::int64_t>(&ze->effectiveValue)) return *zi;
                float zf = 0.0f;
                if (TryGetFloat(ze->effectiveValue, zf)) return static_cast<std::int64_t>(std::lround(zf));
            }
            return 0;
        };
        bool hasNonZeroZ = false;
        for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (readChildZ(child) != 0) {
                hasNonZeroZ = true;
                break;
            }
        }
        auto emitOneChild = [&](const UIElement* child) {
            if (selfExpander && selfExpander->IsBodyChildHidden(child)) {
                std::size_t dummy = 0u;
                CollapseSubtreeRenderNodesRecursive(child, dummy);
                return;
            }
            if (selfTabControl && selfTabControl->IsChildInactiveTabPage(child)) {
                std::size_t dummy = 0u;
                CollapseSubtreeRenderNodesRecursive(child, dummy);
                if (const auto* childHcc = child->DynamicCast<HeaderedContentControl>()) {
                    if (const UIElement* hdrElem = childHcc->GetHeaderElement()) {
                        TraverseAndEmitElement(hdrElem, childState);
                    }
                }
                return;
            }
            if (tabItemHostedInTabControl && child == tabItemHeaderElem) {
                TraverseAndEmitElement(child, childState);
                return;
            }
            TraverseAndEmitElement(child, childState);
        };
        if (!hasNonZeroZ) {
            for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
                emitOneChild(child);
            }
        } else {
            std::vector<const UIElement*> zSorted;
            for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
                zSorted.push_back(child);
            }
            std::stable_sort(zSorted.begin(), zSorted.end(), [&](const UIElement* a, const UIElement* b) noexcept {
                return readChildZ(a) < readChildZ(b);
            });
            for (const UIElement* child : zSorted) {
                emitOneChild(child);
            }
        }
    }

    if (wantsBlurSpan && blurSpanSlot < blurLayerSpans_.size()) {
        blurLayerSpans_[blurSpanSlot].endNodeId = static_cast<std::uint32_t>(nodes_.size());
    }

    // Restore active clip & blend state after visiting children
    activeClipRect_ = parentState.clipRect;
    activeClipEnabled_ = parentState.clipActive;
    activeBlendMode_ = parentState.blendMode;
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
