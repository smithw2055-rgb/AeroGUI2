#include "aero/render/PagedRenderTree.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "aero/controls/Controls.hpp"

namespace aero {

namespace {

std::atomic<std::uint32_t> gNextRenderTreeId{1u};

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
    if (!hasFillBrush && hasStrokeBrush) {
        outEmitFillNode = false;
        outEmitSeparateStrokeNode = true;
    } else if (hasFillBrush && hasStrokeBrush) {
        outEmitFillNode = true;
        outEmitSeparateStrokeNode = true;
    } else {
        outEmitFillNode = true;
        outEmitSeparateStrokeNode = false;
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

    const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(entry.cellWidth) - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(entry.cellHeight) - 1);
    const int x1 = std::min(x0 + 1, static_cast<int>(entry.cellWidth) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(entry.cellHeight) - 1);
    const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
    const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);

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

PagedRenderTree::PagedRenderTree()
    : treeId_(gNextRenderTreeId.fetch_add(1u, std::memory_order_relaxed)) {
    InitPreCachedDigitGlyphs();
}

void PagedRenderTree::InitPreCachedDigitGlyphs() noexcept {
    for (auto& g : preCachedGlyphs_) {
        g = GlyphUvRect{0.0f, 0.0f, 0.0f, 0.0f, 8.0f, false};
    }
    // Partition top half of atlas (v in [0.0..0.5), rows 0..7 of 16x16 grid) exclusively for font glyphs
    // so bottom half (v in [0.5..1.0)) never collides with MTSDF vector icon cells.
    constexpr char kHudChars[] = "0123456789.%+-/: ";
    for (std::size_t i = 0; i < sizeof(kHudChars) - 1; ++i) {
        const auto ch = static_cast<std::uint8_t>(kHudChars[i]) & 0x7Fu;
        const float col = static_cast<float>(i % 16u);
        const float row = static_cast<float>(i / 16u); // rows 0..1
        GlyphUvRect rect{};
        rect.u0 = col * 0.0625f;
        rect.v0 = row * 0.0625f;
        rect.u1 = rect.u0 + 0.055f;
        rect.v1 = rect.v0 + 0.055f;
        rect.advanceX = 9.0f;
        rect.valid = true;
        preCachedGlyphs_[ch] = rect;
    }
    std::size_t slot = sizeof(kHudChars) - 1u; // slots 17..94 -> rows 1..5
    for (std::uint32_t cp = 0x20u; cp <= 0x7Eu; ++cp) {
        if (preCachedGlyphs_[cp].valid) {
            continue;
        }
        const float col = static_cast<float>(slot % 16u);
        const float row = static_cast<float>((slot / 16u) % 6u);
        GlyphUvRect rect{};
        rect.u0 = col * 0.0625f;
        rect.v0 = row * 0.0625f;
        rect.u1 = rect.u0 + 0.055f;
        rect.v1 = rect.v0 + 0.055f;
        rect.advanceX = 8.4f;
        rect.valid = true;
        preCachedGlyphs_[cp] = rect;
        ++slot;
    }
}

GlyphUvRect PagedRenderTree::LookupCodePointGlyph(std::uint32_t codePoint) const noexcept {
    if (codePoint < 128u && preCachedGlyphs_[codePoint].valid) {
        return preCachedGlyphs_[codePoint];
    }
    // Deterministic hashed UV slot in rows 6..7 (v in [0.375..0.500)) of the top-half glyph region
    const std::uint32_t mixed = (codePoint * 2654435761u) >> 16u;
    const float col = static_cast<float>(mixed & 0x0Fu);
    const float row = static_cast<float>(6u + ((mixed >> 4u) & 0x01u));
    GlyphUvRect rect{};
    rect.u0 = col * 0.0625f;
    rect.v0 = row * 0.0625f;
    rect.u1 = rect.u0 + 0.058f;
    rect.v1 = rect.v0 + 0.058f;
    rect.advanceX = IsWideCjkCodePoint(codePoint) ? 14.0f : 8.4f;
    rect.valid = true;
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
    pendingComboPopups_.clear();
    activeComboPopupOwners_.clear();
    // Advance treeId_ so stale cached renderNodeId_ handles on UIElements never collide
    treeId_ = gNextRenderTreeId.fetch_add(1u, std::memory_order_relaxed);
}

std::uint32_t PagedRenderTree::EnsurePageForAllocation(
    std::uint32_t neededVertices,
    const Rect& clipRect,
    bool clipActive) {
    const bool needNewPage = pages_.empty() ||
        (pages_.back().header.vertexCount + neededVertices > kVerticesPerCommandPage) ||
        !SameClipRect(pages_.back().header, clipRect, clipActive);

    if (needNewPage) {
        const auto newPageIdx = static_cast<std::uint32_t>(pages_.size());
        CommandPage newPage{};
        newPage.header.pageIndex = newPageIdx;
        newPage.header.firstNodeId = static_cast<std::uint32_t>(nodes_.size());
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

    // Local corners (0,0), (w,0), (w,h), (0,h) transformed by 2D affine matrix + node origin
    const float ox = node.x + node.tx;
    const float oy = node.y + node.ty;

    const float x0 = ox;
    const float y0 = oy;
    const float x1 = ox + w * node.m00;
    const float y1 = oy + w * node.m10;
    const float x2 = ox + w * node.m00 + h * node.m01;
    const float y2 = oy + w * node.m10 + h * node.m11;
    const float x3 = ox + h * node.m01;
    const float y3 = oy + h * node.m11;

    const std::uint32_t packedColor = ScalePackedRgbaAlpha(node.baseRgba, node.opacity);
    const auto prim = static_cast<std::uint16_t>(node.primType);
    const std::uint16_t flags = node.encodedSdfParams;

    float uMin = node.uv0X;
    float vMin = node.uv0Y;
    float uMax = node.uv1X;
    float vMax = node.uv1Y;
    if (node.primType == AeroPrimType::SdfRoundedRect) {
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
    desc.primType = (cornerRadius > 0.0f || borderThickness > 0.0f)
        ? AeroPrimType::SdfRoundedRect
        : AeroPrimType::SolidQuad;
    desc.encodedSdfParams = PackSdfRoundedRectParams(cornerRadius, borderThickness);
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    desc.opacity = 1.0f;
    desc.cornerRadius = cornerRadius;
    desc.borderThickness = borderThickness;
    desc.baseRgba = PackColorRgba8(color, 1.0f);
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
    const UIElement* owner) {
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
    (void)UpdateTextRunNode(nodeId, x, y, fontSize, utf8Text, color, opacity);
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
    desc.sourceElement = owner;

    nodes_.push_back(desc);
    WriteQuadVertices(nodes_.back());
    MarkPageDirty(pageIdx, vOffset);
    return nodeId;
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

bool PagedRenderTree::UpdateTextRunNodeSlice(
    std::uint32_t nodeId,
    float x,
    float y,
    float fontSize,
    std::string_view utf8Text,
    std::size_t& ioUtf8Cursor,
    float& ioLocalPenX,
    float& ioLocalPenY,
    const Color& color,
    float opacity) noexcept {
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
    node.width = std::max(1.0f, fontSize);
    node.height = std::max(1.0f, fontSize * 1.2f);
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
    const float lineH = node.height;

    for (std::size_t slot = 0; slot < maxSlots; ++slot) {
        AeroVertex* __restrict q = vBase + (slot * 4u);
        std::uint32_t cp = 0;
        bool emitted = false;
        while (DecodeNextUtf8CodePoint(utf8Text, ioUtf8Cursor, cp)) {
            if (cp == '\n') {
                ioLocalPenX = 0.0f;
                ioLocalPenY += lineH;
                continue;
            }
            if (cp == '\r') {
                continue;
            }
            const bool wideCjk = IsWideCjkCodePoint(cp);
            const float gw = wideCjk ? (node.width * 0.95f) : (node.width * 0.6f);
            const float gh = lineH;
            if (cp == ' ') {
                ioLocalPenX += gw;
                continue;
            }
            const GlyphUvRect uv = LookupCodePointGlyph(cp);
            const float gx0 = originX + ioLocalPenX * node.m00 + ioLocalPenY * node.m01;
            const float gy0 = originY + ioLocalPenX * node.m10 + ioLocalPenY * node.m11;
            const float gx1 = gx0 + gw * node.m00;
            const float gy1 = gy0 + gw * node.m10;
            const float gx2 = gx1 + gh * node.m01;
            const float gy2 = gy1 + gh * node.m11;
            const float gx3 = gx0 + gh * node.m01;
            const float gy3 = gy0 + gh * node.m11;

            q[0] = AeroVertex{gx0, gy0, uv.u0, uv.v0, packedColor, prim, flags};
            q[1] = AeroVertex{gx1, gy1, uv.u1, uv.v0, packedColor, prim, flags};
            q[2] = AeroVertex{gx2, gy2, uv.u1, uv.v1, packedColor, prim, flags};
            q[3] = AeroVertex{gx3, gy3, uv.u0, uv.v1, packedColor, prim, flags};
            ioLocalPenX += gw;
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
    for (std::size_t i = 0; i < maxSlots; ++i) {
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
    float opacity) noexcept {
    if (nodeId >= nodes_.size()) {
        return false;
    }
    const RenderNodeDescriptor& node = nodes_[nodeId];
    if (node.pageIndex >= pages_.size() || node.primType != AeroPrimType::SdfTextGlyph) {
        return false;
    }

    const std::size_t maxSlots = node.vertexCount / 4u;
    const std::size_t neededGlyphs = CountVisibleUtf8Glyphs(utf8Text);
    if (neededGlyphs > maxSlots) {
        return false;
    }

    std::size_t utf8Cursor = 0;
    float localPenX = 0.0f;
    float localPenY = 0.0f;
    return UpdateTextRunNodeSlice(
        nodeId,
        x,
        y,
        fontSize,
        utf8Text,
        utf8Cursor,
        localPenX,
        localPenY,
        color,
        opacity);
}

std::uint16_t PagedRenderTree::EmitTextRunNodes(
    float x,
    float y,
    float fontSize,
    std::string_view utf8Text,
    const Color& color,
    float opacity,
    const RenderTraversalState& curState,
    const UIElement* owner) {
    const std::uint32_t maxQuadsPerPage = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4u);
    const std::size_t visibleGlyphs = CountVisibleUtf8Glyphs(utf8Text);
    const std::size_t totalDesiredSlots = std::max<std::size_t>(visibleGlyphs + 8u, 16u);
    const std::size_t chunkCount = std::max<std::size_t>(
        1u,
        (visibleGlyphs + maxQuadsPerPage - 1u) / maxQuadsPerPage);

    std::size_t remainingDesired = totalDesiredSlots;
    std::size_t utf8Cursor = 0;
    float localPenX = 0.0f;
    float localPenY = 0.0f;
    std::uint16_t emittedNodes = 0u;

    for (std::size_t c = 0; c < chunkCount; ++c) {
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
        desc.width = std::max(1.0f, fontSize);
        desc.height = std::max(1.0f, fontSize * 1.2f);
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
        desc.baseRgba = PackColorRgba8(color, 1.0f);
        desc.sourceElement = owner;

        nodes_.push_back(desc);
        (void)UpdateTextRunNodeSlice(
            nodeId,
            x,
            y,
            fontSize,
            utf8Text,
            utf8Cursor,
            localPenX,
            localPenY,
            color,
            opacity);
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

bool PagedRenderTree::UpdateDirtyElement(const UIElement* element) noexcept {
    if (!element) {
        return false;
    }

    // Reconstruct ancestor state along parent chain in O(depth) with 0 heap allocations
    const UIElement* chain[64]{};
    std::size_t depth = 0;
    for (const UIElement* anc = element->Parent(); anc != nullptr && depth < 64u; anc = anc->Parent()) {
        chain[depth++] = anc;
    }

    RenderTraversalState parentState{};
    for (std::size_t idx = depth; idx > 0u; --idx) {
        const UIElement* anc = chain[idx - 1u];
        const Visibility ancVis = anc->GetVisibility();
        if (ancVis != Visibility::Visible) {
            parentState.inheritedOpacity = 0.0f;
        } else {
            parentState.inheritedOpacity *= std::clamp(anc->GetOpacity(), 0.0f, 1.0f);
        }
        const Rect ar = anc->FinalRect();
        const float aw = (ar.width > 0.0f) ? ar.width : std::max(0.0f, anc->GetWidth());
        const float ah = (ar.height > 0.0f) ? ar.height : std::max(0.0f, anc->GetHeight());
        ApplyStateMatrix(
            parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty,
            anc->ComputeLocalEffectiveTransform2D());
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
        ApplyStateMatrix(
            parentState.m00, parentState.m01, parentState.m10, parentState.m11, parentState.tx, parentState.ty,
            anc->GetChildContainerTransform2D());
    }

    RenderTraversalState curState = parentState;
    const Visibility vis = element->GetVisibility();
    if (vis != Visibility::Visible) {
        curState.inheritedOpacity = 0.0f;
    } else {
        curState.inheritedOpacity *= std::clamp(element->GetOpacity(), 0.0f, 1.0f);
    }
    ApplyStateMatrix(
        curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty,
        element->ComputeLocalEffectiveTransform2D());

    return UpdateElementInPlace(element, curState);
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
    };

    // Specialized Path update
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

        bool emitFill = true;
        bool emitSeparateStroke = false;
        ClassifyPathEmission(*pathElem, emitFill, emitSeparateStroke);
        const std::uint16_t neededNodes = static_cast<std::uint16_t>((emitFill ? 1u : 0u) + (emitSeparateStroke ? 1u : 0u));
        if (neededNodes != span) {
            // Structural transition between 1-node and 2-node fill+stroke Path -> rebuild cleanly
            return false;
        }

        const Rect fitted = ComputeStretchedDestRect(
            Rect{r.x, r.y, boxW, boxH},
            entry->boundsW,
            entry->boundsH,
            pathElem->GetStretch());
        const float strokeW = pathElem->GetStrokeThickness();
        const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
        const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
        const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
        const MtsdfMappedQuad mapped = ComputeMtsdfMappedPlacement(fitted, *entry, strokeW);

        std::uint16_t k = 0;
        if (emitFill) {
            RenderNodeDescriptor& nd = nodes_[firstId + k];
            nd.x = mapped.quadRect.x;
            nd.y = mapped.quadRect.y;
            nd.width = mapped.quadRect.width;
            nd.height = mapped.quadRect.height;
            nd.opacity = effOpacity;
            nd.primType = AeroPrimType::MsdfVectorPath;
            nd.uv0X = mapped.u0;
            nd.uv0Y = mapped.v0;
            nd.uv1X = mapped.u1;
            nd.uv1Y = mapped.v1;
            const float inlineStrokeW = emitSeparateStroke ? 0.0f : strokeW;
            nd.borderThickness = inlineStrokeW;
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
            RenderNodeDescriptor& nd = nodes_[firstId + k];
            nd.x = mapped.quadRect.x;
            nd.y = mapped.quadRect.y;
            nd.width = mapped.quadRect.width;
            nd.height = mapped.quadRect.height;
            nd.opacity = effOpacity;
            nd.primType = AeroPrimType::MsdfVectorPath;
            nd.uv0X = mapped.u0;
            nd.uv0Y = mapped.v0;
            nd.uv1X = mapped.u1;
            nd.uv1Y = mapped.v1;
            nd.borderThickness = strokeW;
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
            nd.cornerRadius = cr;
            nd.borderThickness = bt;
            nd.encodedSdfParams = PackSdfRoundedRectParams(cr, bt);
            nd.primType = (cr > 0.0f || bt > 0.0f) ? AeroPrimType::SdfRoundedRect : AeroPrimType::SolidQuad;
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

    // Specialized TextBox / PasswordBox update (box quad + 1..N SdfTextGlyph nodes + caret quad)
    if (const auto* tbox = element->DynamicCast<TextBox>()) {
        if (span < 3u) {
            return false;
        }
        const std::string displayStr = tbox->GetDisplayString();
        const bool showingPlaceholder = displayStr.empty();
        const std::string renderedText = showingPlaceholder ? tbox->GetPlaceholder() : displayStr;
        const std::string_view activeText = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{renderedText};

        std::size_t totalGlyphCap = 0u;
        for (std::uint16_t k = 1u; k + 1u < span; ++k) {
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
        const float cr = value::TryGetCornerRadius(tbox->GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}).topLeft;
        const float bt = tbox->IsFocused()
            ? 2.0f
            : value::TryGetThickness(tbox->GetValue(PropId_BorderThickness)).value_or(Thickness{1.0f}).left;

        // Node 0: Input field box
        {
            RenderNodeDescriptor& boxNode = nodes_[firstId];
            boxNode.x = r.x;
            boxNode.y = r.y;
            boxNode.width = boxW;
            boxNode.height = boxH;
            boxNode.opacity = effAlpha;
            boxNode.cornerRadius = cr;
            boxNode.borderThickness = bt;
            boxNode.encodedSdfParams = PackSdfRoundedRectParams(cr, bt);
            boxNode.primType = AeroPrimType::SdfRoundedRect;
            boxNode.baseRgba = PackColorRgba8(boxBg, 1.0f);
            syncQuadTransform(boxNode);
            WriteQuadVertices(boxNode);
            MarkPageDirty(boxNode.pageIndex, boxNode.vertexOffsetInPage);
        }

        // Nodes [1 .. span-2]: UTF-8 text or muted placeholder
        const Thickness pad = tbox->GetPadding();
        const float padLeft = (pad.left > 0.0f) ? pad.left : 6.0f;
        const float scrollX = showingPlaceholder ? 0.0f : tbox->HorizontalTextScrollOffset();
        const float textX = r.x + padLeft - scrollX;
        const float fontSize = tbox->GetFontSize();
        const float textY = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
        const Color textColor = showingPlaceholder
            ? Color{0.55f, 0.60f, 0.68f, 0.85f}
            : tbox->GetForeground();

        std::size_t utf8Cursor = 0u;
        float localPenX = 0.0f;
        float localPenY = 0.0f;
        for (std::uint16_t k = 1u; k + 1u < span; ++k) {
            RenderNodeDescriptor& tNode = nodes_[firstId + k];
            tNode.m00 = curState.m00;
            tNode.m01 = curState.m01;
            tNode.m10 = curState.m10;
            tNode.m11 = curState.m11;
            tNode.baseTx = curState.tx;
            tNode.baseTy = curState.ty;
            tNode.inheritedOpacity = curState.inheritedOpacity;
            (void)UpdateTextRunNodeSlice(
                firstId + k,
                textX,
                textY,
                fontSize,
                activeText,
                utf8Cursor,
                localPenX,
                localPenY,
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
            caretNode.primType = AeroPrimType::SolidQuad;
            caretNode.encodedSdfParams = AeroVertexFlag_None;
            caretNode.baseRgba = PackColorRgba8(Color{0.38f, 0.76f, 1.0f, 1.0f}, 1.0f);
            syncQuadTransform(caretNode);
            WriteQuadVertices(caretNode);
            MarkPageDirty(caretNode.pageIndex, caretNode.vertexOffsetInPage);
        }

        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
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
        hdrNode.cornerRadius = 4.0f;
        hdrNode.borderThickness = bt;
        hdrNode.encodedSdfParams = PackSdfRoundedRectParams(4.0f, bt);
        hdrNode.primType = AeroPrimType::SdfRoundedRect;
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
        chevNode.primType = AeroPrimType::SdfRoundedRect;
        chevNode.encodedSdfParams = PackSdfRoundedRectParams(2.0f, 0.0f);
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
        std::size_t utf8Cursor = 0u;
        float penX = 0.0f;
        float penY = 0.0f;
        for (std::uint16_t k = 2u; k < span; ++k) {
            RenderNodeDescriptor& tn = nodes_[firstId + k];
            tn.m00 = curState.m00;
            tn.m01 = curState.m01;
            tn.m10 = curState.m10;
            tn.m11 = curState.m11;
            tn.baseTx = curState.tx;
            tn.baseTy = curState.ty;
            tn.inheritedOpacity = curState.inheritedOpacity;
            (void)UpdateTextRunNodeSlice(
                firstId + k, tx, ty, fontSize, activeSel, utf8Cursor, penX, penY, fg, effAlpha);
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
        outerNd.cornerRadius = outerCr;
        outerNd.borderThickness = outerBt;
        outerNd.encodedSdfParams = PackSdfRoundedRectParams(outerCr, outerBt);
        outerNd.primType = AeroPrimType::SdfRoundedRect;
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
        markNd.cornerRadius = markCr;
        markNd.borderThickness = 0.0f;
        markNd.encodedSdfParams = PackSdfRoundedRectParams(markCr, 0.0f);
        markNd.primType = AeroPrimType::SdfRoundedRect;
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
            std::size_t utf8Cursor = 0u;
            float penX = 0.0f;
            float penY = 0.0f;
            for (std::uint16_t k = 2u; k < span; ++k) {
                RenderNodeDescriptor& tn = nodes_[firstId + k];
                tn.m00 = curState.m00;
                tn.m01 = curState.m01;
                tn.m10 = curState.m10;
                tn.m11 = curState.m11;
                tn.baseTx = curState.tx;
                tn.baseTy = curState.ty;
                tn.inheritedOpacity = curState.inheritedOpacity;
                (void)UpdateTextRunNodeSlice(
                    firstId + k, lx, ly, fontSize, activeLabel, utf8Cursor, penX, penY, fg, effAlpha);
            }
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized TextBlock update (optional background quad + 1..N SdfTextGlyph run nodes)
    if (const auto* tb = element->DynamicCast<TextBlock>()) {
        const auto bgOpt = value::TryGetColor(element->GetValue(PropId_Background));
        const bool wantsBgNode = bgOpt.has_value() && bgOpt->a > 0.0f;
        const bool hasBgNode = (nodes_[firstId].primType != AeroPrimType::SdfTextGlyph);
        if (wantsBgNode && !hasBgNode) {
            // Newly added Background on previously unbacked TextBlock -> rebuild to preserve Z-order
            return false;
        }

        const Thickness pad = tb->GetPadding();
        std::string txt = tb->GetText();
        if (txt.empty()) {
            const UnifiedValue phVal = tb->GetValue(PropId_Placeholder);
            if (const auto* ph = std::get_if<StringValue>(&phVal)) {
                txt = ph->str();
            }
        }
        const std::string_view activeText = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{txt};
        const Color fgColor = tb->GetForeground();
        const float fontSize = tb->GetFontSize();

        std::size_t totalGlyphCap = 0;
        for (std::uint16_t k = 0; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                totalGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }

        if (CountVisibleUtf8Glyphs(activeText) > totalGlyphCap) {
            // Text grew past reserved span capacity -> zero entire old span and emit contiguous expanded span
            // while preserving exact parent scissor clip state!
            const Rect prevClip = activeClipRect_;
            const bool prevClipOn = activeClipEnabled_;
            activeClipRect_ = curState.clipRect;
            activeClipEnabled_ = curState.clipActive;

            for (std::uint16_t k = 0; k < span; ++k) {
                ZeroOutNodeVertices(nodes_[firstId + k]);
            }

            const std::uint32_t newSpanStart = static_cast<std::uint32_t>(nodes_.size());
            std::uint16_t newSpanCount = 0u;
            if (wantsBgNode) {
                const float cr = value::TryGetCornerRadius(tb->GetValue(PropId_CornerRadius))
                    .value_or(CornerRadius{}).topLeft;
                const std::uint32_t bgId = AllocateRenderNode(
                    r.x, r.y, boxW, boxH, *bgOpt, cr, 0.0f, element);
                nodes_[bgId].opacity = effOpacity;
                syncQuadTransform(nodes_[bgId]);
                WriteQuadVertices(nodes_[bgId]);
                MarkPageDirty(nodes_[bgId].pageIndex, nodes_[bgId].vertexOffsetInPage);
                ++newSpanCount;
            }
            newSpanCount = static_cast<std::uint16_t>(
                newSpanCount + EmitTextRunNodes(
                    r.x + pad.left,
                    r.y + pad.top,
                    fontSize,
                    activeText,
                    fgColor,
                    effOpacity,
                    curState,
                    element));

            activeClipRect_ = prevClip;
            activeClipEnabled_ = prevClipOn;

            element->BindRenderNodeSpan(treeId_, newSpanStart, newSpanCount);
            element->ClearLayoutDirtyFlags(RenderDirty);
            return true;
        }

        std::size_t utf8Cursor = 0;
        float localPenX = 0.0f;
        float localPenY = 0.0f;
        for (std::uint16_t k = 0; k < span; ++k) {
            RenderNodeDescriptor& node = nodes_[firstId + k];
            if (node.primType == AeroPrimType::SdfTextGlyph) {
                node.m00 = curState.m00;
                node.m01 = curState.m01;
                node.m10 = curState.m10;
                node.m11 = curState.m11;
                node.baseTx = curState.tx;
                node.baseTy = curState.ty;
                node.inheritedOpacity = curState.inheritedOpacity;
                (void)UpdateTextRunNodeSlice(
                    firstId + k,
                    r.x + pad.left,
                    r.y + pad.top,
                    fontSize,
                    activeText,
                    utf8Cursor,
                    localPenX,
                    localPenY,
                    fgColor,
                    effOpacity);
            } else {
                node.x = r.x;
                node.y = r.y;
                node.width = boxW;
                node.height = boxH;
                node.opacity = wantsBgNode ? effOpacity : 0.0f;
                if (wantsBgNode) {
                    node.baseRgba = PackColorRgba8(*bgOpt, 1.0f);
                }
                node.cornerRadius = value::TryGetCornerRadius(tb->GetValue(PropId_CornerRadius))
                    .value_or(CornerRadius{}).topLeft;
                node.encodedSdfParams = PackSdfRoundedRectParams(node.cornerRadius, 0.0f);
                node.primType = (node.cornerRadius > 0.0f) ? AeroPrimType::SdfRoundedRect : AeroPrimType::SolidQuad;
                syncQuadTransform(node);
                WriteQuadVertices(node);
                MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
            }
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Specialized Button update (box node + optional centered SdfTextGlyph label span)
    if (const auto* btn = element->DynamicCast<Button>()) {
        const std::string label = (!btn->FirstChild()) ? btn->GetContentText() : std::string{};
        const std::string_view activeLabel = (vis == Visibility::Collapsed)
            ? std::string_view{}
            : std::string_view{label};

        std::size_t totalLabelGlyphCap = 0;
        bool hasLabelNode = false;
        for (std::uint16_t k = 0; k < span; ++k) {
            if (nodes_[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
                hasLabelNode = true;
                totalLabelGlyphCap += nodes_[firstId + k].vertexCount / 4u;
            }
        }
        if (!label.empty() && !hasLabelNode) {
            // Button initially had empty Content="" and gained text -> rebuild cleanly to preserve Z-order
            return false;
        }

        Color fg{0.96f, 0.98f, 1.0f, 1.0f};
        if (auto fgOpt = value::TryGetColor(btn->GetValue(PropId_Foreground))) {
            fg = *fgOpt;
        }
        float fontSize = 14.0f;
        (void)TryGetFloat(btn->GetValue(PropId_FontSize), fontSize);
        const Size labelMetrics = MeasureUtf8TextRun(label, fontSize);
        const float lx = r.x + std::max(4.0f, (boxW - labelMetrics.width) * 0.5f);
        const float ly = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);

        if (CountVisibleUtf8Glyphs(activeLabel) > totalLabelGlyphCap) {
            const Rect prevClip = activeClipRect_;
            const bool prevClipOn = activeClipEnabled_;
            activeClipRect_ = curState.clipRect;
            activeClipEnabled_ = curState.clipActive;

            for (std::uint16_t k = 0; k < span; ++k) {
                ZeroOutNodeVertices(nodes_[firstId + k]);
            }

            const std::uint32_t newSpanStart = static_cast<std::uint32_t>(nodes_.size());
            Color bgColor{0.22f, 0.34f, 0.54f, 1.0f};
            if (auto bg = value::TryGetColor(btn->GetValue(PropId_Background))) {
                bgColor = *bg;
            }
            const float cr = value::TryGetCornerRadius(btn->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{4.0f}).topLeft;
            const float bt = value::TryGetThickness(btn->GetValue(PropId_BorderThickness))
                .value_or(Thickness{}).left;
            const std::uint32_t boxId = AllocateRenderNode(r.x, r.y, boxW, boxH, bgColor, cr, bt, element);
            nodes_[boxId].opacity = effOpacity;
            syncQuadTransform(nodes_[boxId]);
            WriteQuadVertices(nodes_[boxId]);
            MarkPageDirty(nodes_[boxId].pageIndex, nodes_[boxId].vertexOffsetInPage);

            const std::uint16_t textSpan = EmitTextRunNodes(
                lx, ly, fontSize, activeLabel, fg, effOpacity, curState, element);

            activeClipRect_ = prevClip;
            activeClipEnabled_ = prevClipOn;

            element->BindRenderNodeSpan(treeId_, newSpanStart, static_cast<std::uint16_t>(1u + textSpan));
            element->ClearLayoutDirtyFlags(RenderDirty);
            return true;
        }

        std::size_t utf8Cursor = 0;
        float localPenX = 0.0f;
        float localPenY = 0.0f;
        for (std::uint16_t k = 0; k < span; ++k) {
            RenderNodeDescriptor& node = nodes_[firstId + k];
            if (node.primType == AeroPrimType::SdfTextGlyph) {
                node.m00 = curState.m00;
                node.m01 = curState.m01;
                node.m10 = curState.m10;
                node.m11 = curState.m11;
                node.baseTx = curState.tx;
                node.baseTy = curState.ty;
                node.inheritedOpacity = curState.inheritedOpacity;
                (void)UpdateTextRunNodeSlice(
                    firstId + k,
                    lx,
                    ly,
                    fontSize,
                    activeLabel,
                    utf8Cursor,
                    localPenX,
                    localPenY,
                    fg,
                    effOpacity);
            } else {
                node.x = r.x;
                node.y = r.y;
                node.width = boxW;
                node.height = boxH;
                node.opacity = effOpacity;
                Color btnBg = value::TryGetColor(btn->GetValue(PropId_Background))
                    .value_or(Color{0.22f, 0.34f, 0.54f, 1.0f});
                btnBg = ModulateInteractiveSurfaceColor(btnBg, btn);
                node.baseRgba = PackColorRgba8(btnBg, 1.0f);
                node.opacity = btn->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
                node.cornerRadius = value::TryGetCornerRadius(btn->GetValue(PropId_CornerRadius))
                    .value_or(CornerRadius{4.0f}).topLeft;
                const float explicitBt = value::TryGetThickness(btn->GetValue(PropId_BorderThickness))
                    .value_or(Thickness{}).left;
                node.borderThickness = btn->IsFocused() ? std::max(2.0f, explicitBt) : explicitBt;
                node.encodedSdfParams = PackSdfRoundedRectParams(node.cornerRadius, node.borderThickness);
                node.primType = (node.cornerRadius > 0.0f || node.borderThickness > 0.0f)
                    ? AeroPrimType::SdfRoundedRect
                    : AeroPrimType::SolidQuad;
                syncQuadTransform(node);
                WriteQuadVertices(node);
                MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
            }
        }
        element->ClearLayoutDirtyFlags(RenderDirty);
        return true;
    }

    // Generic Quad / Border / Panel / Image update
    RenderNodeDescriptor& node = nodes_[firstId];
    node.x = r.x;
    node.y = r.y;
    node.width = boxW;
    node.height = boxH;
    node.opacity = effOpacity;
    if (element->IsA<Image>()) {
        node.primType = AeroPrimType::TexturedQuad;
        node.baseRgba = PackColorRgba8(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    } else {
        if (auto bg = value::TryGetColor(element->GetValue(PropId_Background))) {
            node.baseRgba = PackColorRgba8(*bg, 1.0f);
        } else if (auto fg = value::TryGetColor(element->GetValue(PropId_Foreground))) {
            node.baseRgba = PackColorRgba8(*fg, 1.0f);
        } else if (element->IsA<Viewbox>()) {
            node.baseRgba = PackColorRgba8(Color{0.0f, 0.0f, 0.0f, 0.0f}, 1.0f);
            node.opacity = 0.0f;
        } else {
            node.baseRgba = PackColorRgba8(Color{0.18f, 0.22f, 0.28f, 1.0f}, 1.0f);
        }
        if (auto cr = value::TryGetCornerRadius(element->GetValue(PropId_CornerRadius))) {
            node.cornerRadius = cr->topLeft;
        } else {
            node.cornerRadius = 0.0f;
        }
        if (auto bt = value::TryGetThickness(element->GetValue(PropId_BorderThickness))) {
            node.borderThickness = bt->left;
        } else {
            node.borderThickness = 0.0f;
        }
        node.encodedSdfParams = PackSdfRoundedRectParams(node.cornerRadius, node.borderThickness);
        node.primType = (node.cornerRadius > 0.0f || node.borderThickness > 0.0f)
            ? AeroPrimType::SdfRoundedRect
            : AeroPrimType::SolidQuad;
    }
    syncQuadTransform(node);
    WriteQuadVertices(node);
    MarkPageDirty(node.pageIndex, node.vertexOffsetInPage);
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
                if (node.primType == AeroPrimType::SdfRoundedRect) {
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

    if (elem->IsA<ComboBox>()) {
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

    ApplyStateMatrix(
        curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty,
        elem->ComputeLocalEffectiveTransform2D());

    // Apply current clip state for this element's own primitives
    activeClipRect_ = curState.clipRect;
    activeClipEnabled_ = curState.clipActive;

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
        if (nd.primType != AeroPrimType::SdfTextGlyph) {
            WriteQuadVertices(nd);
        }
    };

    if (const auto* pathElem = elem->DynamicCast<Path>()) {
        const std::string pathStr = pathElem->GetData();
        if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
            const Rect fitted = ComputeStretchedDestRect(
                Rect{r.x, r.y, w, h},
                mtsdf->boundsW,
                mtsdf->boundsH,
                pathElem->GetStretch());
            const float strokeW = pathElem->GetStrokeThickness();
            const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
            const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
            const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
            bool emitFill = true;
            bool emitSeparateStroke = false;
            ClassifyPathEmission(*pathElem, emitFill, emitSeparateStroke);

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
            emittedCount = 1u;
        }
    } else if (const auto* imgElem = elem->DynamicCast<Image>()) {
        (void)imgElem;
        const std::uint32_t nid = AllocateTexturedNode(
            r.x,
            r.y,
            w,
            h,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            Color{1.0f, 1.0f, 1.0f, 1.0f},
            effOpacity,
            elem);
        applyNodeTransform(nid);
        emittedCount = 1u;
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

        emittedCount = 3u;
    } else if (const auto* tbox = elem->DynamicCast<TextBox>()) {
        const float effAlpha = tbox->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color boxBg = value::TryGetColor(tbox->GetValue(PropId_Background))
            .value_or(Color{0.12f, 0.15f, 0.20f, 1.0f});
        boxBg = ModulateInteractiveSurfaceColor(boxBg, tbox);
        const float cr = value::TryGetCornerRadius(tbox->GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}).topLeft;
        const float bt = tbox->IsFocused()
            ? 2.0f
            : value::TryGetThickness(tbox->GetValue(PropId_BorderThickness)).value_or(Thickness{1.0f}).left;

        const std::uint32_t boxId = AllocateRenderNode(
            r.x, r.y, w, h, boxBg, cr, bt, elem);
        nodes_[boxId].opacity = effAlpha;
        applyNodeTransform(boxId);
        ++emittedCount;

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
                elem));

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
        if (auto bg = value::TryGetColor(tb->GetValue(PropId_Background)); bg && bg->a > 0.0f) {
            const float cr = value::TryGetCornerRadius(tb->GetValue(PropId_CornerRadius))
                .value_or(CornerRadius{}).topLeft;
            const std::uint32_t bgId = AllocateRenderNode(
                r.x, r.y, w, h, *bg, cr, 0.0f, elem);
            nodes_[bgId].opacity = effOpacity;
            applyNodeTransform(bgId);
            ++emittedCount;
        }
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
        emittedCount = static_cast<std::uint16_t>(
            emittedCount + EmitTextRunNodes(
                r.x + pad.left,
                r.y + pad.top,
                fontSize,
                text,
                fg,
                effOpacity,
                curState,
                elem));
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
                elem));

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
                        lx, ly, fontSize, label, fg, effAlpha, curState, elem));
            }
        }
    } else if (const auto* btn = elem->DynamicCast<Button>()) {
        const float effAlpha = btn->IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
        Color bgColor{0.22f, 0.34f, 0.54f, 1.0f};
        if (auto bg = value::TryGetColor(btn->GetValue(PropId_Background))) {
            bgColor = *bg;
        }
        bgColor = ModulateInteractiveSurfaceColor(bgColor, btn);
        const float cr = value::TryGetCornerRadius(btn->GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}).topLeft;
        const float explicitBt = value::TryGetThickness(btn->GetValue(PropId_BorderThickness))
            .value_or(Thickness{}).left;
        const float bt = btn->IsFocused() ? std::max(2.0f, explicitBt) : explicitBt;
        const std::uint32_t boxId = AllocateRenderNode(
            r.x, r.y, w, h, bgColor, cr, bt, elem);
        nodes_[boxId].opacity = effAlpha;
        applyNodeTransform(boxId);
        ++emittedCount;

        if (!btn->FirstChild()) {
            const std::string label = btn->GetContentText();
            if (!label.empty()) {
                Color fg{0.96f, 0.98f, 1.0f, 1.0f};
                if (auto fgOpt = value::TryGetColor(btn->GetValue(PropId_Foreground))) {
                    fg = *fgOpt;
                }
                float fontSize = 14.0f;
                (void)TryGetFloat(btn->GetValue(PropId_FontSize), fontSize);
                const Size labelMetrics = MeasureUtf8TextRun(label, fontSize);
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
                        elem));
            }
        }
    } else {
        Color color{0.18f, 0.22f, 0.28f, 1.0f};
        bool transparentContainer = false;
        if (auto bg = value::TryGetColor(elem->GetValue(PropId_Background))) {
            color = *bg;
        } else if (auto fg = value::TryGetColor(elem->GetValue(PropId_Foreground))) {
            color = *fg;
        } else if (elem->IsA<Viewbox>()) {
            color = Color{0.0f, 0.0f, 0.0f, 0.0f};
            transparentContainer = true;
        }

        float cornerRadius = 0.0f;
        if (auto cr = value::TryGetCornerRadius(elem->GetValue(PropId_CornerRadius))) {
            cornerRadius = cr->topLeft;
        }

        float borderThickness = 0.0f;
        if (auto bt = value::TryGetThickness(elem->GetValue(PropId_BorderThickness))) {
            borderThickness = bt->left;
        }

        const std::uint32_t nodeId = AllocateRenderNode(
            r.x,
            r.y,
            w,
            h,
            color,
            cornerRadius,
            borderThickness,
            elem);
        nodes_[nodeId].opacity = transparentContainer ? 0.0f : effOpacity;
        applyNodeTransform(nodeId);
        emittedCount = 1u;
    }

    elem->BindRenderNodeSpan(treeId_, spanStartId, emittedCount);
    elem->ClearLayoutDirtyFlags(static_cast<std::uint8_t>(RenderDirty | SubtreeRenderDirty));

    // If this container specifies ClipToBounds=true, intersect transformed child clip rect before descending
    RenderTraversalState childState = curState;
    const UnifiedValue clipVal = elem->GetValue(PropId_ClipToBounds);
    if (const auto* clipBool = std::get_if<bool>(&clipVal); clipBool && *clipBool) {
        const Matrix2D worldM = MatrixFromState(
            curState.m00, curState.m01, curState.m10, curState.m11, curState.tx, curState.ty);
        Rect rawClip = elem->GetLocalClipBounds();
        if (r.width <= 0.0f && r.height <= 0.0f && (w > 0.0f || h > 0.0f)) {
            rawClip = Rect{r.x, r.y, w, h};
        }
        const Rect localBounds = worldM.TransformRectBounds(rawClip);
        childState.clipRect = childState.clipActive
            ? IntersectRect(childState.clipRect, localBounds)
            : localBounds;
        childState.clipActive = true;
    }
    ApplyStateMatrix(
        childState.m00, childState.m01, childState.m10, childState.m11, childState.tx, childState.ty,
        elem->GetChildContainerTransform2D());

    if (!elem->IsA<ComboBox>()) {
        for (const UIElement* child = elem->FirstChild(); child != nullptr; child = child->NextSibling()) {
            TraverseAndEmitElement(child, childState);
        }
    }

    // Restore active clip state after visiting children
    activeClipRect_ = parentState.clipRect;
    activeClipEnabled_ = parentState.clipActive;
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
