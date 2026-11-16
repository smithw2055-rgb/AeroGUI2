#include "ControlsInternal.hpp"

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/controls/Buttons.hpp"
#include "aero/controls/Control.hpp"
#include "aero/controls/Decorators.hpp"
#include "aero/controls/ItemsControls.hpp"
#include "aero/controls/MediaControls.hpp"
#include "aero/controls/Panels.hpp"
#include "aero/controls/RangeControls.hpp"
#include "aero/controls/Shapes.hpp"
#include "aero/controls/TextControls.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTreeInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace aero {

namespace {

[[nodiscard]] bool ResolveElementRenderNodeSpan(
    PagedRenderTree& tree,
    const UIElement* element,
    std::uint32_t& outFirstId,
    std::uint16_t& outSpan) noexcept {
    outFirstId = UIElement::InvalidRenderNodeId;
    outSpan = 0u;
    if (!element) {
        return false;
    }
    const auto& nodes = ControlRenderEmitter::Nodes(tree);
    const std::uint32_t treeId = tree.TreeId();
    if (element->RenderTreeId() == treeId &&
        element->RenderNodeId() < nodes.size() &&
        element->RenderNodeSpan() > 0u &&
        nodes[element->RenderNodeId()].sourceElement == element) {
        outFirstId = element->RenderNodeId();
        outSpan = element->RenderNodeSpan();
        return true;
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(nodes.size()); ++i) {
        if (nodes[i].sourceElement == element) {
            outFirstId = i;
            outSpan = 1u;
            while (outFirstId + outSpan < nodes.size() &&
                   nodes[outFirstId + outSpan].sourceElement == element) {
                ++outSpan;
            }
            element->BindRenderNodeSpan(treeId, outFirstId, outSpan);
            return true;
        }
    }
    return false;
}

void SyncQuadAndWriteDirty(
    PagedRenderTree& tree,
    RenderNodeDescriptor& nd,
    const PagedRenderTraversalState& curState) noexcept {
    ControlRenderEmitter::SyncQuadTransform(nd, curState);
    ControlRenderEmitter::WriteQuadVertices(tree, nd);
    ControlRenderEmitter::MarkPageDirty(tree, nd.pageIndex, nd.vertexOffsetInPage);
}

[[nodiscard]] bool HasAnyRenderGradientId(const UIElement& elem) noexcept {
    return ExtractGradBrushId(elem.GetValue(PropId_BackgroundGradientId)) != 0u ||
           ExtractGradBrushId(elem.GetValue(PropId_ForegroundGradientId)) != 0u ||
           ExtractGradBrushId(elem.GetValue(PropId_BorderGradientId)) != 0u;
}

[[nodiscard]] std::string ExtractVectorPathData(const Shape& shapeElem) {
    if (const auto* pathElem = shapeElem.DynamicCast<Path>()) {
        std::string pathStr = pathElem->GetData();
        if (pathStr.empty()) {
            const UnifiedValue ptsVal = shapeElem.GetValue(PropId_ShapePoints);
            if (const auto* sv = std::get_if<StringValue>(&ptsVal); sv && !sv->view().empty()) {
                pathStr = ConvertPointsToSvgPath(sv->view(), false);
            }
        }
        return pathStr;
    }
    if (const auto* poly = shapeElem.DynamicCast<Polygon>()) {
        std::string pathStr = poly->GetData();
        if (pathStr.empty()) {
            const std::string pts = poly->GetPoints();
            if (!pts.empty()) {
                pathStr = ConvertPointsToSvgPath(pts, true);
            }
        }
        return pathStr;
    }
    if (const auto* pl = shapeElem.DynamicCast<Polyline>()) {
        std::string pathStr = pl->GetData();
        if (pathStr.empty()) {
            const std::string pts = pl->GetPoints();
            if (!pts.empty()) {
                pathStr = ConvertPointsToSvgPath(pts, false);
            }
        }
        return pathStr;
    }
    return {};
}

void ClassifyPathEmission(
    const Shape& shapeElem,
    bool& outEmitFillNode,
    bool& outEmitSeparateStrokeNode) noexcept {
    const float strokeW = shapeElem.GetStrokeThickness();
    if (const auto* pathElem = shapeElem.DynamicCast<Path>()) {
        outEmitFillNode = pathElem->HasExplicitFill();
        outEmitSeparateStrokeNode = pathElem->HasExplicitStroke() && strokeW > 0.0f;
        return;
    }
    outEmitFillNode = shapeElem.HasFill() ||
                      value::TryGetColor(shapeElem.GetValue(PropId_Foreground)).has_value();
    outEmitSeparateStrokeNode = shapeElem.HasStroke() && strokeW > 0.0f;
}

struct TextBlockRenderView {
    bool hasRichText = false;
    std::string rawRichText;
    std::string text;
    float fontSize = 14.0f;
    std::string fontFamily;
    Color foreground{0.96f, 0.98f, 1.0f, 1.0f};
    TextWrapping wrapping = TextWrapping::NoWrap;
    TextAlignment alignment = TextAlignment::Left;
};

[[nodiscard]] bool TryExtractTextBlockRenderView(
    const UIElement* elem,
    TextBlockRenderView& out) {
    if (!elem) {
        return false;
    }
    if (const auto* tb = elem->DynamicCast<TextBlock>()) {
        out.hasRichText = tb->HasRichText();
        out.rawRichText = out.hasRichText ? tb->GetRawRichText() : std::string{};
        out.text = tb->GetText();
        out.fontSize = tb->GetFontSize();
        out.fontFamily = tb->GetFontFamily();
        out.foreground = tb->GetForeground();
        out.wrapping = tb->GetTextWrapping();
        out.alignment = tb->GetTextAlignment();
        return true;
    }
    if (const auto* lbl = elem->DynamicCast<Label>()) {
        out.hasRichText = lbl->HasRichText();
        out.rawRichText = out.hasRichText ? lbl->GetRawRichText() : std::string{};
        out.text = lbl->GetText();
        if (out.text.empty()) {
            out.text = lbl->GetContentText();
        }
        out.fontSize = lbl->GetFontSize();
        out.fontFamily = lbl->GetFontFamily();
        out.foreground = lbl->GetForeground();
        out.wrapping = lbl->GetTextWrapping();
        out.alignment = lbl->GetTextAlignment();
        return true;
    }
    return false;
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
    const float t = slider.VisualNormalizedPosition();
    const bool rev = slider.IsDirectionReversed();

    SliderVisualGeometry g{};
    if (slider.GetOrientation() == Orientation::Vertical) {
        const float thumbSize = std::min(16.0f, std::max(8.0f, iw));
        const float trackThickness = std::min(6.0f, std::max(2.0f, iw * 0.3f));
        const float trackX = ix + (iw - trackThickness) * 0.5f;
        const float usableH = std::max(0.0f, ih - thumbSize);
        const float thumbY = iy + (1.0f - t) * usableH;
        const float centerThumbY = thumbY + thumbSize * 0.5f;
        g.trackRect = Rect{trackX, iy, trackThickness, ih};
        g.fillRect = rev
            ? Rect{trackX, iy, trackThickness, std::max(0.0f, centerThumbY - iy)}
            : Rect{trackX, centerThumbY, trackThickness, std::max(0.0f, (iy + ih) - centerThumbY)};
        g.thumbRect = Rect{ix + (iw - thumbSize) * 0.5f, thumbY, thumbSize, thumbSize};
    } else {
        const float thumbSize = std::min(16.0f, std::max(8.0f, ih));
        const float trackThickness = std::min(6.0f, std::max(2.0f, ih * 0.3f));
        const float trackY = iy + (ih - trackThickness) * 0.5f;
        const float usableW = std::max(0.0f, iw - thumbSize);
        const float thumbX = ix + t * usableW;
        const float centerThumbX = thumbX + thumbSize * 0.5f;
        g.trackRect = Rect{ix, trackY, iw, trackThickness};
        g.fillRect = rev
            ? Rect{centerThumbX, trackY, std::max(0.0f, (ix + iw) - centerThumbX), trackThickness}
            : Rect{ix, trackY, std::max(0.0f, centerThumbX - ix), trackThickness};
        g.thumbRect = Rect{thumbX, iy + (ih - thumbSize) * 0.5f, thumbSize, thumbSize};
    }
    return g;
}

[[nodiscard]] std::uint16_t EmitProceduralMediaFrame(
    PagedRenderTree& tree,
    const MediaElement& mediaElem,
    const Rect& bounds,
    float effOpacity,
    const PagedRenderTraversalState& curState) {
    const float w = bounds.width;
    const float h = bounds.height;
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    if (w <= 0.0f || h <= 0.0f) {
        const std::uint32_t anchorId = ControlRenderEmitter::AllocateRenderNode(
            tree, bounds.x, bounds.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, &mediaElem);
        nodes[anchorId].opacity = 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, anchorId, curState);
        return 1u;
    }

    const std::string srcUri = mediaElem.GetSource();
    const ImageAtlasEntry* atlasEntry = srcUri.empty()
        ? nullptr
        : ImageAtlas::Instance().LoadOrGet(srcUri);
    if (atlasEntry != nullptr && atlasEntry->width > 0u && atlasEntry->height > 0u) {
        const Color tintColor = value::TryGetColor(mediaElem.GetValue(PropId_Foreground))
            .value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});
        const ImageStretchPlacement placement = ComputeImageStretchPlacement(
            bounds,
            static_cast<float>(atlasEntry->width),
            static_cast<float>(atlasEntry->height),
            mediaElem.GetStretch(),
            StretchDirection::Both,
            atlasEntry->u0,
            atlasEntry->v0,
            atlasEntry->u1,
            atlasEntry->v1);
        if (placement.valid) {
            const std::uint32_t nid = ControlRenderEmitter::AllocateTexturedNode(
                tree,
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
                &mediaElem,
                atlasEntry->textureId);
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            return 1u;
        }
        const std::uint32_t nid = ControlRenderEmitter::AllocateTexturedNode(
            tree,
            bounds.x,
            bounds.y,
            0.0f,
            0.0f,
            atlasEntry->u0,
            atlasEntry->v0,
            atlasEntry->u1,
            atlasEntry->v1,
            tintColor,
            0.0f,
            &mediaElem,
            atlasEntry->textureId);
        ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
        return 1u;
    }

    // Procedural animated video frame when source is a video path/stream or empty
    std::uint16_t emitted = 0u;
    auto emitRect = [&](float rx, float ry, float rw, float rh, const Color& col, float cr = 0.0f, float opScale = 1.0f) {
        if (rw <= 0.0f || rh <= 0.0f) return;
        const std::uint32_t nid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, rx, ry, rw, rh, col, CornerRadius{cr}, &mediaElem);
        ControlRenderEmitter::Nodes(tree)[nid].opacity = effOpacity * opScale;
        ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
        ++emitted;
    };

    const double posSec = std::max(0.0, static_cast<double>(mediaElem.PositionSeconds()));
    const double durSec = static_cast<double>(mediaElem.DurationSeconds());
    const bool isPlaying = mediaElem.IsPlaying();

    // Dark backing frame
    emitRect(bounds.x, bounds.y, w, h, Color{0.07f, 0.09f, 0.14f, 1.0f}, 0.0f);

    // High-contrast vertical SMPTE-inspired color bars driven by PositionSeconds()
    const float fieldH = std::max(2.0f, h * 0.74f);
    static constexpr std::array<Color, 6> kBaseBars{{
        Color{0.92f, 0.22f, 0.26f, 1.0f},
        Color{0.96f, 0.84f, 0.18f, 1.0f},
        Color{0.18f, 0.86f, 0.44f, 1.0f},
        Color{0.16f, 0.82f, 0.94f, 1.0f},
        Color{0.26f, 0.46f, 0.96f, 1.0f},
        Color{0.88f, 0.28f, 0.84f, 1.0f},
    }};
    const float barW = w / static_cast<float>(kBaseBars.size());
    for (std::size_t i = 0u; i < kBaseBars.size(); ++i) {
        const float pulse = 0.84f + 0.16f * static_cast<float>(
            std::sin(posSec * 2.4 + static_cast<double>(i) * 1.05));
        const Color base = kBaseBars[i];
        const Color modulated{
            std::clamp(base.r * pulse, 0.0f, 1.0f),
            std::clamp(base.g * pulse, 0.0f, 1.0f),
            std::clamp(base.b * pulse, 0.0f, 1.0f),
            1.0f};
        const float bx0 = bounds.x + static_cast<float>(i) * barW;
        const float bx1 = (i + 1u == kBaseBars.size()) ? (bounds.x + w) : (bx0 + barW);
        emitRect(bx0, bounds.y, std::max(0.0f, bx1 - bx0), fieldH, modulated, 0.0f);
    }

    // Animated sweeping vertical scanline + horizontal raster indicator
    const double scanPhaseX = std::fmod(posSec * 0.5, 1.0);
    const float scanW = std::clamp(w * 0.03f, 2.0f, std::max(2.0f, w));
    const float scanX = bounds.x + static_cast<float>(scanPhaseX) * std::max(0.0f, w - scanW);
    emitRect(scanX, bounds.y, scanW, fieldH, Color{0.98f, 0.99f, 1.0f, 0.92f}, 0.0f);

    const double scanPhaseY = std::fmod(posSec * 0.35 + (isPlaying ? 0.12 : 0.0), 1.0);
    const float hBarH = std::clamp(fieldH * 0.05f, 1.5f, 6.0f);
    const float hBarY = bounds.y + static_cast<float>(scanPhaseY) * std::max(0.0f, fieldH - hBarH);
    emitRect(bounds.x, hBarY, w, hBarH, Color{0.98f, 0.99f, 1.0f, 0.55f}, 0.0f);

    // Transport bar + play/pause indicator strip at bottom
    const float stripY = bounds.y + fieldH;
    const float stripH = std::max(0.0f, h - fieldH);
    if (stripH >= 4.0f) {
        const float padX = std::clamp(w * 0.04f, 4.0f, 16.0f);
        const float dotSz = std::clamp(stripH * 0.55f, 4.0f, 18.0f);
        const float dotX = bounds.x + padX;
        const float dotY = stripY + (stripH - dotSz) * 0.5f;
        const Color badgeCol = isPlaying
            ? Color{0.20f, 0.88f, 0.52f, 1.0f}
            : Color{0.96f, 0.64f, 0.20f, 1.0f};
        const std::uint32_t dotId = ControlRenderEmitter::AllocateEllipseNode(
            tree, dotX, dotY, dotSz, dotSz, badgeCol, 0.0f, false, effOpacity, &mediaElem);
        ControlRenderEmitter::ApplyNodeTransform(tree, dotId, curState);
        ++emitted;

        const float trackX = dotX + dotSz + padX * 0.6f;
        const float trackW = std::max(0.0f, (bounds.x + w - padX) - trackX);
        if (trackW >= 4.0f) {
            const float trackH = std::clamp(stripH * 0.28f, 3.0f, 8.0f);
            const float trackY = stripY + (stripH - trackH) * 0.5f;
            emitRect(trackX, trackY, trackW, trackH, Color{0.20f, 0.25f, 0.34f, 1.0f}, trackH * 0.5f);
            const float progress01 = (durSec > 1e-4)
                ? static_cast<float>(std::clamp(posSec / durSec, 0.0, 1.0))
                : static_cast<float>(scanPhaseX);
            const float fillW = std::max(2.0f, trackW * progress01);
            emitRect(
                trackX,
                trackY,
                std::min(trackW, fillW),
                trackH,
                isPlaying ? Color{0.24f, 0.78f, 0.98f, 1.0f} : Color{0.94f, 0.66f, 0.24f, 1.0f},
                trackH * 0.5f);
        }
    }
    return emitted;
}

void EmitContainerFillFromPlan(
    PagedRenderTree& tree,
    const UIElement* elem,
    const SurfaceFillBorderPlan& plan,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
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
        const std::uint32_t fillId = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree,
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
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;
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
        const std::uint32_t fillId = ControlRenderEmitter::AllocateTexturedNode(
            tree,
            dr.x, dr.y, dr.width, dr.height,
            pl.valid ? pl.u0 : bgAtlas->u0,
            pl.valid ? pl.v0 : bgAtlas->v0,
            pl.valid ? pl.u1 : bgAtlas->u1,
            pl.valid ? pl.v1 : bgAtlas->v1,
            Color{1.0f, 1.0f, 1.0f, 1.0f},
            effOpacity * bgGradOp,
            elem,
            bgAtlas->textureId);
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t fillId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x,
            plan.fillRect.y,
            plan.fillRect.width,
            plan.fillRect.height,
            plan.fillColor,
            plan.fillRadii,
            elem);
        ControlRenderEmitter::Nodes(tree)[fillId].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;
    }
}

void EmitContainerBorderFromPlan(
    PagedRenderTree& tree,
    const UIElement* elem,
    const SurfaceFillBorderPlan& plan,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
    if (plan.reserveBorderNode) {
        const std::uint32_t borderId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            elem);
        ControlRenderEmitter::Nodes(tree)[borderId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, borderId, curState);
        ++ioEmittedCount;
    }
}

// ============================================================================
// Shared Shape Helpers (Path / Polygon / Polyline)
// ============================================================================

[[nodiscard]] RenderInPlaceResult TryUpdateVectorPathShapeInPlace(
    const Shape& shapePath,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) {
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, &shapePath, firstId, span)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const auto* pathElem = shapePath.DynamicCast<Path>();
    const float trimStart = pathElem ? std::clamp(pathElem->GetTrimStart(), 0.0f, 1.0f) : 0.0f;
    const float trimEnd = pathElem ? std::clamp(pathElem->GetTrimEnd(), 0.0f, 1.0f) : 1.0f;
    if (trimStart > 0.0f || trimEnd < 1.0f || nodes[firstId].primType == AeroPrimType::SdfRoundedRect) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    const Visibility vis = shapePath.GetVisibility();
    const std::string pathStr = ExtractVectorPathData(shapePath);
    const AxbMtsdfPathEntry* entry = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr);
    if (!entry || vis == Visibility::Collapsed) {
        for (std::uint16_t k = 0; k < span; ++k) {
            RenderNodeDescriptor& nd = nodes[firstId + k];
            nd.width = 0.0f;
            nd.height = 0.0f;
            nd.opacity = 0.0f;
            SyncQuadAndWriteDirty(tree, nd, curState);
        }
        shapePath.ClearLayoutDirtyFlags(RenderDirty);
        return RenderInPlaceResult::UpdatedInPlace;
    }

    ControlRenderEmitter::RegisterMtsdfInTreeAtlas(tree, *entry);

    bool emitFill = true;
    bool emitSeparateStroke = false;
    ClassifyPathEmission(shapePath, emitFill, emitSeparateStroke);
    const std::uint16_t neededNodes = std::max<std::uint16_t>(
        1u,
        static_cast<std::uint16_t>((emitFill ? 1u : 0u) + (emitSeparateStroke ? 1u : 0u)));
    if (neededNodes != span) {
        // Structural transition between 1-node and 2-node fill+stroke Path -> rebuild cleanly
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    if (!emitFill && !emitSeparateStroke) {
        RenderNodeDescriptor& nd = nodes[firstId];
        nd.x = r.x;
        nd.y = r.y;
        nd.width = 0.0f;
        nd.height = 0.0f;
        nd.opacity = 0.0f;
        SyncQuadAndWriteDirty(tree, nd, curState);
        shapePath.ClearLayoutDirtyFlags(RenderDirty);
        return RenderInPlaceResult::UpdatedInPlace;
    }

    Rect fitted = ComputeStretchedDestRect(
        Rect{r.x, r.y, boxW, boxH},
        entry->boundsW,
        entry->boundsH,
        shapePath.GetStretch());
    if (shapePath.GetStretch() == Stretch::None) {
        fitted.x += entry->boundsX;
        fitted.y += entry->boundsY;
    }
    const float strokeW = shapePath.GetStrokeThickness();
    const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
    const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
    const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
    const std::uint32_t fillGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BackgroundGradientId));
    const std::uint32_t strokeGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BorderGradientId));

    std::uint16_t k = 0;
    if (emitFill) {
        const float inlineStrokeW = emitSeparateStroke ? 0.0f : strokeW;
        const MtsdfMappedQuad fillMapped = ComputeMtsdfMappedPlacement(fitted, *entry, inlineStrokeW);
        RenderNodeDescriptor& nd = nodes[firstId + k];
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
        nd.baseRgba = PackColorRgba8(shapePath.GetFill(), 1.0f);
        nd.mtsdfPathHash = entry->pathHash;
        SyncQuadAndWriteDirty(tree, nd, curState);
        ++k;
    }
    if (emitSeparateStroke && k < span) {
        const MtsdfMappedQuad strokeMapped = ComputeMtsdfMappedPlacement(fitted, *entry, strokeW);
        RenderNodeDescriptor& nd = nodes[firstId + k];
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
        nd.baseRgba = PackColorRgba8(shapePath.GetStroke(), 1.0f);
        nd.mtsdfPathHash = entry->pathHash;
        SyncQuadAndWriteDirty(tree, nd, curState);
    }
    shapePath.ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool EmitVectorPathShapeDropShadow(
    const Shape& shapePath,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) {
    const Rect r = shapePath.FinalRect();
    const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, shapePath.GetWidth());
    const float h = (r.height > 0.0f) ? r.height : std::max(0.0f, shapePath.GetHeight());
    const float worldBlurR = curState.shadowOnlyPass
        ? curState.shadowWorldBlurR
        : ([&]() noexcept {
              const float sx = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
              const float sy = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
              const float avg = std::max(1e-4f, 0.5f * (sx + sy));
              return std::max(1.0f, shapePath.GetEffectDescriptor().blurRadius) * avg;
          }());
    const float screenBlurR = std::max(1.0f, worldBlurR);

    const auto* pathElem = shapePath.DynamicCast<Path>();
    const std::string pathStr = ExtractVectorPathData(shapePath);
    const float shTrimStart = pathElem ? std::clamp(pathElem->GetTrimStart(), 0.0f, 1.0f) : 0.0f;
    const float shTrimEnd = pathElem ? std::clamp(pathElem->GetTrimEnd(), 0.0f, 1.0f) : 1.0f;
    if (shTrimStart > 0.0f || shTrimEnd < 1.0f) {
        const float shStrokeW = std::max(shapePath.GetStrokeThickness(), localBlurR * 0.25f);
        const bool hasShPaint = pathElem
            ? (pathElem->HasExplicitStroke() || pathElem->HasExplicitFill())
            : (shapePath.HasStroke() || shapePath.HasFill());
        if (hasShPaint) {
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTrimmedPathSubpaths(
                    tree,
                    pathStr,
                    Rect{r.x + sdx, r.y + sdy, w, h},
                    shapePath.GetStretch(),
                    shStrokeW,
                    shCol,
                    0u,
                    effOpacity,
                    curState,
                    shadowOwner,
                    shTrimStart,
                    shTrimEnd));
        }
    } else if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
        Rect fitted = ComputeStretchedDestRect(
            Rect{r.x + sdx, r.y + sdy, w, h},
            mtsdf->boundsW,
            mtsdf->boundsH,
            shapePath.GetStretch());
        if (shapePath.GetStretch() == Stretch::None) {
            fitted.x += mtsdf->boundsX;
            fitted.y += mtsdf->boundsY;
        }
        const std::uint32_t shId = ControlRenderEmitter::AllocateMtsdfPathNode(
            tree,
            fitted.x,
            fitted.y,
            fitted.width,
            fitted.height,
            *mtsdf,
            shCol,
            std::max(shapePath.GetStrokeThickness(), localBlurR * 0.25f),
            effOpacity,
            shadowOwner);
        ControlRenderEmitter::Nodes(tree)[shId].softFeatherPx = screenBlurR;
        ControlRenderEmitter::ApplyNodeTransform(tree, shId, curState);
        ++ioEmittedCount;
    }
    return true;
}

bool EmitVectorPathShapeVisual(
    const Shape& shapePath,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
    const UIElement* elem = &shapePath;
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const auto* pathElem = shapePath.DynamicCast<Path>();
    const std::string pathStr = ExtractVectorPathData(shapePath);
    const float trimStart = pathElem ? std::clamp(pathElem->GetTrimStart(), 0.0f, 1.0f) : 0.0f;
    const float trimEnd = pathElem ? std::clamp(pathElem->GetTrimEnd(), 0.0f, 1.0f) : 1.0f;
    const bool isTrimmed = (trimStart > 0.0f) || (trimEnd < 1.0f);
    if (isTrimmed) {
        const float strokeW = shapePath.GetStrokeThickness();
        bool emitFill = true;
        bool emitSeparateStroke = false;
        ClassifyPathEmission(shapePath, emitFill, emitSeparateStroke);
        const std::uint32_t fillGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BackgroundGradientId));
        const std::uint32_t strokeGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BorderGradientId));
        const bool hasShapeStroke = pathElem ? pathElem->HasExplicitStroke() : shapePath.HasStroke();
        std::uint16_t trimmedEmitted = 0u;
        if (emitSeparateStroke) {
            trimmedEmitted = ControlRenderEmitter::EmitTrimmedPathSubpaths(
                tree,
                pathStr,
                Rect{r.x, r.y, w, h},
                shapePath.GetStretch(),
                strokeW,
                shapePath.GetStroke(),
                strokeGradId,
                effOpacity,
                curState,
                elem,
                trimStart,
                trimEnd);
        } else if (emitFill && hasShapeStroke) {
            trimmedEmitted = ControlRenderEmitter::EmitTrimmedPathSubpaths(
                tree,
                pathStr,
                Rect{r.x, r.y, w, h},
                shapePath.GetStretch(),
                1.0f,
                shapePath.GetStroke(),
                strokeGradId,
                effOpacity,
                curState,
                elem,
                trimStart,
                trimEnd);
        } else if (emitFill) {
            trimmedEmitted = ControlRenderEmitter::EmitTrimmedPathSubpaths(
                tree,
                pathStr,
                Rect{r.x, r.y, w, h},
                shapePath.GetStretch(),
                (strokeW > 0.0f) ? strokeW : 2.0f,
                shapePath.GetFill(),
                fillGradId,
                effOpacity,
                curState,
                elem,
                trimStart,
                trimEnd);
        }
        if (trimmedEmitted > 0u) {
            ioEmittedCount = static_cast<std::uint16_t>(ioEmittedCount + trimmedEmitted);
        } else {
            const std::uint32_t nid = ControlRenderEmitter::AllocateRenderNode(
                tree, r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
            nodes[nid].opacity = 0.0f;
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            ++ioEmittedCount;
        }
    } else if (const AxbMtsdfPathEntry* mtsdf = AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(pathStr)) {
        Rect fitted = ComputeStretchedDestRect(
            Rect{r.x, r.y, w, h},
            mtsdf->boundsW,
            mtsdf->boundsH,
            shapePath.GetStretch());
        if (shapePath.GetStretch() == Stretch::None) {
            fitted.x += mtsdf->boundsX;
            fitted.y += mtsdf->boundsY;
        }
        const float strokeW = shapePath.GetStrokeThickness();
        const float worldScaleX = std::sqrt(curState.m00 * curState.m00 + curState.m10 * curState.m10);
        const float worldScaleY = std::sqrt(curState.m01 * curState.m01 + curState.m11 * curState.m11);
        const float worldStrokeScale = std::max(0.0f, 0.5f * (worldScaleX + worldScaleY));
        bool emitFill = true;
        bool emitSeparateStroke = false;
        ClassifyPathEmission(shapePath, emitFill, emitSeparateStroke);
        const std::uint32_t fillGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BackgroundGradientId));
        const std::uint32_t strokeGradId = ExtractGradBrushId(shapePath.GetValue(PropId_BorderGradientId));

        if (!emitFill && !emitSeparateStroke) {
            const std::uint32_t nid = ControlRenderEmitter::AllocateRenderNode(
                tree, r.x, r.y, w, h, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, elem);
            nodes[nid].opacity = 0.0f;
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            ++ioEmittedCount;
        }
        if (emitFill) {
            const float inlineStrokeW = emitSeparateStroke ? 0.0f : strokeW;
            const std::uint32_t nid = ControlRenderEmitter::AllocateMtsdfPathNode(
                tree,
                fitted.x,
                fitted.y,
                fitted.width,
                fitted.height,
                *mtsdf,
                shapePath.GetFill(),
                inlineStrokeW,
                effOpacity,
                elem);
            nodes[nid].gradientBrushId = fillGradId;
            nodes[nid].encodedPrimExtra = (fillGradId != 0u)
                ? static_cast<std::uint16_t>((std::min<std::uint32_t>(fillGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                : 0u;
            nodes[nid].encodedSdfParams = PackMtsdfParams(
                mtsdf->pxRange,
                mtsdf->atlasWidth,
                inlineStrokeW * worldStrokeScale,
                false);
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            ++ioEmittedCount;
        }
        if (emitSeparateStroke) {
            const std::uint32_t strokeNid = ControlRenderEmitter::AllocateMtsdfPathNode(
                tree,
                fitted.x,
                fitted.y,
                fitted.width,
                fitted.height,
                *mtsdf,
                shapePath.GetStroke(),
                strokeW,
                effOpacity,
                elem);
            nodes[strokeNid].gradientBrushId = strokeGradId;
            nodes[strokeNid].encodedPrimExtra = (strokeGradId != 0u)
                ? static_cast<std::uint16_t>((std::min<std::uint32_t>(strokeGradId, GradientBrushRegistry::kMaxBrushId) & 0x0FFFu) << 4u)
                : 0u;
            nodes[strokeNid].encodedSdfParams = PackMtsdfParams(
                mtsdf->pxRange,
                mtsdf->atlasWidth,
                strokeW * worldStrokeScale,
                true);
            ControlRenderEmitter::ApplyNodeTransform(tree, strokeNid, curState);
            ++ioEmittedCount;
        }
    } else {
        const std::uint32_t nid = ControlRenderEmitter::AllocateRenderNode(
            tree, r.x, r.y, 0.0f, 0.0f, shapePath.GetFill(), 0.0f, 0.0f, elem);
        nodes[nid].opacity = 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
        ++ioEmittedCount;
    }
    return true;
}

// ============================================================================
// Shared TextBlock & Label Helpers
// ============================================================================

[[nodiscard]] bool TryGetTextBlockOrLabelGlyphScanText(
    const UIElement& elem,
    std::string& outText) {
    TextBlockRenderView tbView{};
    if (!TryExtractTextBlockRenderView(&elem, tbView)) {
        return false;
    }
    outText = std::move(tbView.text);
    if (outText.empty()) {
        const UnifiedValue phVal = elem.GetValue(PropId_Placeholder);
        if (const auto* sv = std::get_if<StringValue>(&phVal)) {
            outText = sv->str();
        }
    }
    return true;
}

[[nodiscard]] RenderInPlaceResult TryUpdateTextBlockOrLabelInPlace(
    const UIElement& elem,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) {
    if (HasAnyRenderGradientId(elem)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    TextBlockRenderView tbView{};
    if (!TryExtractTextBlockRenderView(&elem, tbView)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    if (tbView.hasRichText || elem.HasControlTemplate() || elem.FirstChild() != nullptr) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, &elem, firstId, span)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = elem.GetVisibility();

    const auto bgOpt = value::TryGetColor(elem.GetValue(PropId_Background));
    const bool wantsBgNode = bgOpt.has_value() && bgOpt->a > 0.0f;
    const auto bbOpt = value::TryGetColor(elem.GetValue(PropId_BorderBrush));
    const Thickness bt = SanitizeThickness(
        value::TryGetThickness(elem.GetValue(PropId_BorderThickness)).value_or(Thickness{}));
    const bool wantsBorderNode = bbOpt.has_value() && (bbOpt->a > 0.0f) && HasPositiveThickness(bt);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(elem.GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));

    std::uint16_t prefixQuads = 0u;
    while (prefixQuads < span && nodes[firstId + prefixQuads].primType != AeroPrimType::SdfTextGlyph) {
        ++prefixQuads;
    }
    const std::uint16_t neededPrefix =
        static_cast<std::uint16_t>((wantsBgNode ? 1u : 0u) + (bbOpt.has_value() ? 1u : 0u));
    if (neededPrefix > prefixQuads) {
        // Newly added Background or BorderBrush on TextBlock -> rebuild to preserve Z-order
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const Thickness pad = elem.GetPadding();
    const Color fgColor = tbView.foreground;
    std::string txt = std::move(tbView.text);
    std::vector<RichGlyphStyle> richStyles;
    const std::vector<RichGlyphStyle>* richPtr = nullptr;
    if (tbView.hasRichText && !tbView.rawRichText.empty()) {
        ParseRichTextToPlainAndStyles(tbView.rawRichText, fgColor, &elem, txt, richStyles);
        richPtr = &richStyles;
    }
    if (txt.empty()) {
        const UnifiedValue phVal = elem.GetValue(PropId_Placeholder);
        if (const auto* ph = std::get_if<StringValue>(&phVal)) {
            txt = ph->str();
            richPtr = nullptr;
        }
    }
    const std::string_view activeText = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{txt};
    const float fontSize = tbView.fontSize;
    const std::string& fontFamily = tbView.fontFamily;
    const TextWrapping wrapMode = tbView.wrapping;
    const float effBoxW = (boxW > 0.0f) ? boxW : elem.DesiredSize().width;
    const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
        ? std::max(1.0f, effBoxW - (pad.left + pad.right))
        : 1000000.0f;
    TextLayoutResult tbLayout = MeasureTextLayout(
        activeText, fontFamily, fontSize, maxTextW, wrapMode);
    const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
    ApplyTextAlignmentToLayout(tbLayout, tbView.alignment, innerAlignW);

    std::size_t totalGlyphCap = 0;
    for (std::uint16_t k = prefixQuads; k < span; ++k) {
        if (nodes[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
            totalGlyphCap += nodes[firstId + k].vertexCount / 4u;
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
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    std::uint16_t quadSlot = 0u;
    if (quadSlot < prefixQuads && !nodes[firstId + quadSlot].isHollowBorder) {
        RenderNodeDescriptor& node = nodes[firstId + quadSlot];
        node.x = plan.fillRect.x;
        node.y = plan.fillRect.y;
        node.width = plan.fillRect.width;
        node.height = plan.fillRect.height;
        node.opacity = wantsBgNode ? effOpacity : 0.0f;
        if (wantsBgNode) {
            node.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
        }
        ConfigureRoundedRectDescriptor(node, plan.fillRadii, Thickness{}, false);
        SyncQuadAndWriteDirty(tree, node, curState);
        ++quadSlot;
    }
    while (quadSlot < prefixQuads) {
        RenderNodeDescriptor& brdNode = nodes[firstId + quadSlot];
        brdNode.x = plan.borderRect.x;
        brdNode.y = plan.borderRect.y;
        brdNode.width = wantsBorderNode ? plan.borderRect.width : 0.0f;
        brdNode.height = wantsBorderNode ? plan.borderRect.height : 0.0f;
        brdNode.opacity = wantsBorderNode ? effOpacity : 0.0f;
        brdNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
        ConfigureRoundedRectDescriptor(brdNode, plan.borderRadii, plan.borderThickness, true);
        SyncQuadAndWriteDirty(tree, brdNode, curState);
        ++quadSlot;
    }

    std::size_t glyphCursor = 0u;
    for (std::uint16_t k = prefixQuads; k < span; ++k) {
        RenderNodeDescriptor& node = nodes[firstId + k];
        node.m00 = curState.m00;
        node.m01 = curState.m01;
        node.m10 = curState.m10;
        node.m11 = curState.m11;
        node.baseTx = curState.tx;
        node.baseTy = curState.ty;
        node.inheritedOpacity = curState.inheritedOpacity;
        node.hasPerspective3D = curState.hasPerspective3D;
        node.worldMatrix3D = curState.world3D;
        (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
            tree,
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
    elem.ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool EmitTextBlockOrLabelDropShadow(
    const UIElement& elem,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) {
    TextBlockRenderView tbShadowView{};
    if (elem.HasControlTemplate() || elem.FirstChild() != nullptr ||
        !TryExtractTextBlockRenderView(&elem, tbShadowView)) {
        return false;
    }
    if (!tbShadowView.text.empty()) {
        const Rect r = elem.FinalRect();
        const float w = (r.width > 0.0f) ? r.width : std::max(0.0f, elem.GetWidth());
        const Thickness pad = elem.GetPadding();
        const float effBoxW = (w > 0.0f) ? w : elem.DesiredSize().width;
        const float maxTW = (tbShadowView.wrapping != TextWrapping::NoWrap && effBoxW > 0.0f)
            ? std::max(1.0f, effBoxW - (pad.left + pad.right))
            : 1000000.0f;
        const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
        ioEmittedCount = static_cast<std::uint16_t>(
            ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                tree,
                r.x + pad.left + sdx,
                r.y + pad.top + sdy,
                tbShadowView.fontSize,
                tbShadowView.text,
                shCol,
                effOpacity,
                curState,
                shadowOwner,
                tbShadowView.fontFamily,
                maxTW,
                tbShadowView.wrapping,
                tbShadowView.alignment,
                innerAlignW));
    }
    return true;
}

bool EmitTextBlockOrLabelVisual(
    const UIElement& elem,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
    TextBlockRenderView tbView{};
    if (!TryExtractTextBlockRenderView(&elem, tbView)) {
        return false;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const auto bgOpt = value::TryGetColor(elem.GetValue(PropId_Background));
    const bool hasBg = bgOpt.has_value() && bgOpt->a > 0.0f;
    const auto bbOpt = value::TryGetColor(elem.GetValue(PropId_BorderBrush));
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(elem.GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));
    const Thickness bt = SanitizeThickness(
        value::TryGetThickness(elem.GetValue(PropId_BorderThickness)).value_or(Thickness{}));
    const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
        Rect{r.x, r.y, w, h},
        cr,
        hasBg,
        bgOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
        bbOpt.has_value(),
        bbOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}),
        bt);
    if (hasBg) {
        const std::uint32_t bgId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x,
            plan.fillRect.y,
            plan.fillRect.width,
            plan.fillRect.height,
            plan.fillColor,
            plan.fillRadii,
            &elem);
        nodes[bgId].opacity = effOpacity;
        ControlRenderEmitter::ApplyNodeTransform(tree, bgId, curState);
        ++ioEmittedCount;
    }
    if (bbOpt.has_value()) {
        const std::uint32_t brdId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            &elem);
        nodes[brdId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, brdId, curState);
        ++ioEmittedCount;
    }
    if (elem.FirstChild() == nullptr) {
        std::string text = std::move(tbView.text);
        if (text.empty()) {
            const UnifiedValue phVal = elem.GetValue(PropId_Placeholder);
            if (const auto* ph = std::get_if<StringValue>(&phVal)) {
                text = ph->str();
            }
        }
        const Thickness pad = elem.GetPadding();
        const float fontSize = tbView.fontSize;
        const Color fg = tbView.foreground;
        const std::string& fontFamily = tbView.fontFamily;
        const TextWrapping wrapMode = tbView.wrapping;
        const float effBoxW = (w > 0.0f) ? w : elem.DesiredSize().width;
        const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
            ? std::max(1.0f, effBoxW - (pad.left + pad.right))
            : 1000000.0f;
        const float innerAlignW = std::max(0.0f, effBoxW - (pad.left + pad.right));
        std::vector<RichGlyphStyle> richStyles;
        const std::vector<RichGlyphStyle>* richPtr = nullptr;
        if (tbView.hasRichText && !tbView.rawRichText.empty()) {
            ParseRichTextToPlainAndStyles(tbView.rawRichText, fg, &elem, text, richStyles);
            richPtr = &richStyles;
        }
        ioEmittedCount = static_cast<std::uint16_t>(
            ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                tree,
                r.x + pad.left,
                r.y + pad.top,
                fontSize,
                text,
                fg,
                effOpacity,
                curState,
                &elem,
                fontFamily,
                maxTextW,
                wrapMode,
                tbView.alignment,
                innerAlignW,
                richPtr));

        std::int64_t textDeco = 0;
        const UnifiedValue tdVal = elem.GetValue(PropId_TextDecorations);
        if (const auto* tdi = std::get_if<std::int64_t>(&tdVal)) {
            textDeco = *tdi;
        } else if (const auto* tds = std::get_if<StringValue>(&tdVal)) {
            const UnifiedValue ptd = ParseBuiltinLiteralPropertyValue(PropId_TextDecorations, tds->view());
            if (const auto* pi = std::get_if<std::int64_t>(&ptd)) textDeco = *pi;
        }
        bool anyRichLine = false;
        if (richPtr) {
            for (const auto& rs : *richPtr) {
                if (rs.underline || rs.strikethrough) {
                    anyRichLine = true;
                    break;
                }
            }
        }
        if (!text.empty() && (textDeco != 0 || anyRichLine)) {
            TextLayoutResult decLayout = MeasureTextLayout(text, fontFamily, fontSize, maxTextW, wrapMode);
            ApplyTextAlignmentToLayout(decLayout, tbView.alignment, innerAlignW);
            const float lineThick = std::max(1.0f, fontSize * 0.075f);
            if (!decLayout.glyphs.empty()) {
                const std::uint8_t baseMask = static_cast<std::uint8_t>(textDeco & 0x7);
                std::size_t cpIdx = 0u;
                std::size_t gIdx = 0u;
                std::size_t curByte = 0u;
                std::uint32_t cp = 0u;
                bool inSpan = false;
                std::uint8_t curSpanMask = 0u;
                float spanX0 = 0.0f, spanX1 = 0.0f, spanBaseY = 0.0f;
                auto flushSpan = [&]() {
                    if (inSpan && curSpanMask != 0u && spanX1 > spanX0 + 0.5f) {
                        auto emitDecoLine = [&](float ly) {
                            const std::uint32_t ulId = ControlRenderEmitter::AllocateRenderNode(
                                tree, r.x + pad.left + spanX0, ly, spanX1 - spanX0, lineThick, fg, 0.0f, 0.0f, &elem);
                            ControlRenderEmitter::Nodes(tree)[ulId].opacity = effOpacity;
                            ControlRenderEmitter::ApplyNodeTransform(tree, ulId, curState);
                            ++ioEmittedCount;
                        };
                        if ((curSpanMask & 1u) != 0u) {
                            emitDecoLine(r.y + pad.top + spanBaseY + fontSize * 0.12f);
                        }
                        if ((curSpanMask & 2u) != 0u) {
                            emitDecoLine(r.y + pad.top + spanBaseY - fontSize * 0.30f);
                        }
                        if ((curSpanMask & 4u) != 0u) {
                            emitDecoLine(r.y + pad.top + spanBaseY - fontSize * 0.82f);
                        }
                    }
                    inSpan = false;
                    curSpanMask = 0u;
                };
                while (DecodeNextUtf8CodePoint(text, curByte, cp)) {
                    if (cp == '\r' || cp == '\n') {
                        flushSpan();
                        ++cpIdx;
                        continue;
                    }
                    if (gIdx < decLayout.glyphs.size()) {
                        const auto& gl = decLayout.glyphs[gIdx++];
                        const bool richUl = richPtr && cpIdx < richPtr->size() && (*richPtr)[cpIdx].underline;
                        const bool richSt = richPtr && cpIdx < richPtr->size() && (*richPtr)[cpIdx].strikethrough;
                        const std::uint8_t gMask = static_cast<std::uint8_t>(
                            baseMask | (richUl ? 1u : 0u) | (richSt ? 2u : 0u));
                        if (gMask != 0u) {
                            if (!inSpan || std::fabs(gl.baselineY - spanBaseY) > 1.0f || gMask != curSpanMask) {
                                flushSpan();
                                inSpan = true;
                                curSpanMask = gMask;
                                spanX0 = gl.penX;
                                spanBaseY = gl.baselineY;
                            }
                            spanX1 = gl.penX + gl.advanceX;
                        } else {
                            flushSpan();
                        }
                    }
                    ++cpIdx;
                }
                flushSpan();
            } else if (textDeco != 0 && decLayout.totalWidth > 0.5f) {
                auto emitFallbackDeco = [&](float lineY) {
                    const std::uint32_t ulId = ControlRenderEmitter::AllocateRenderNode(
                        tree, r.x + pad.left, lineY, decLayout.totalWidth, lineThick, fg, 0.0f, 0.0f, &elem);
                    ControlRenderEmitter::Nodes(tree)[ulId].opacity = effOpacity;
                    ControlRenderEmitter::ApplyNodeTransform(tree, ulId, curState);
                    ++ioEmittedCount;
                };
                if ((textDeco & 1) != 0) emitFallbackDeco(r.y + pad.top + fontSize * 1.05f);
                if ((textDeco & 2) != 0) emitFallbackDeco(r.y + pad.top + fontSize * 0.58f);
                if ((textDeco & 4) != 0) emitFallbackDeco(r.y + pad.top + fontSize * 0.08f);
            }
        }
    } else if (ioEmittedCount == 0u) {
        const std::uint32_t anchorId = ControlRenderEmitter::AllocateRenderNode(
            tree, r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, &elem);
        nodes[anchorId].opacity = 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, anchorId, curState);
        ++ioEmittedCount;
    }
    return true;
}

// ============================================================================
// Shared CheckBox & RadioButton Helpers
// ============================================================================

[[nodiscard]] RenderInPlaceResult TryUpdateCheckOrRadioInPlace(
    const ButtonBase& elem,
    bool isRadio,
    bool checked,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) {
    if (HasAnyRenderGradientId(elem)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, &elem, firstId, span) || span < 2u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = elem.GetVisibility();

    const std::string label = (!elem.FirstChild()) ? elem.GetContentText() : std::string{};
    const std::string_view activeLabel = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{label};

    std::size_t glyphCap = 0u;
    bool hasLabelNodes = false;
    for (std::uint16_t k = 2u; k < span; ++k) {
        if (nodes[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
            hasLabelNodes = true;
            glyphCap += nodes[firstId + k].vertexCount / 4u;
        }
    }
    if ((!label.empty() && !hasLabelNodes) || CountVisibleUtf8Glyphs(activeLabel) > glyphCap) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const float effAlpha = elem.IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    const Thickness pad = elem.GetPadding();
    const float boxSize = 18.0f;
    const float ix = r.x + pad.left;
    const float iy = r.y + pad.top + std::max(0.0f, (boxH - (pad.top + pad.bottom) - boxSize) * 0.5f);

    const Color outerColor = ModulateInteractiveSurfaceColor(
        Color{0.16f, 0.22f, 0.30f, 1.0f},
        &elem);
    const float outerCr = isRadio ? 9.0f : 4.0f;
    const float outerBt = elem.IsFocused() ? 2.0f : 1.5f;

    RenderNodeDescriptor& outerNd = nodes[firstId];
    outerNd.x = ix;
    outerNd.y = iy;
    outerNd.width = (vis == Visibility::Collapsed) ? 0.0f : boxSize;
    outerNd.height = (vis == Visibility::Collapsed) ? 0.0f : boxSize;
    outerNd.opacity = effAlpha;
    ConfigureRoundedRectDescriptor(outerNd, CornerRadius{outerCr}, Thickness{outerBt}, false);
    outerNd.baseRgba = PackColorRgba8(outerColor, 1.0f);
    SyncQuadAndWriteDirty(tree, outerNd, curState);

    const float markSize = isRadio ? 8.0f : 10.0f;
    const float markInset = (boxSize - markSize) * 0.5f;
    const float markCr = isRadio ? 4.0f : 2.0f;
    RenderNodeDescriptor& markNd = nodes[firstId + 1u];
    markNd.x = ix + markInset;
    markNd.y = iy + markInset;
    markNd.width = (vis == Visibility::Collapsed || !checked) ? 0.0f : markSize;
    markNd.height = (vis == Visibility::Collapsed || !checked) ? 0.0f : markSize;
    markNd.opacity = checked ? effAlpha : 0.0f;
    ConfigureRoundedRectDescriptor(markNd, CornerRadius{markCr}, Thickness{}, false);
    markNd.baseRgba = PackColorRgba8(Color{0.26f, 0.66f, 1.0f, 1.0f}, 1.0f);
    SyncQuadAndWriteDirty(tree, markNd, curState);

    if (hasLabelNodes) {
        float fontSize = 14.0f;
        (void)TryGetFloat(elem.GetValue(PropId_FontSize), fontSize);
        const Color fg = value::TryGetColor(elem.GetValue(PropId_Foreground))
            .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
        const float lx = ix + boxSize + 8.0f;
        const float ly = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
        const std::string chkFont = ResolveElementFontFamily(&elem);
        const TextLayoutResult chkLayout = MeasureTextLayout(
            activeLabel, chkFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
        std::size_t glyphCursor = 0u;
        for (std::uint16_t k = 2u; k < span; ++k) {
            RenderNodeDescriptor& tn = nodes[firstId + k];
            tn.m00 = curState.m00;
            tn.m01 = curState.m01;
            tn.m10 = curState.m10;
            tn.m11 = curState.m11;
            tn.baseTx = curState.tx;
            tn.baseTy = curState.ty;
            tn.inheritedOpacity = curState.inheritedOpacity;
            tn.hasPerspective3D = curState.hasPerspective3D;
            tn.worldMatrix3D = curState.world3D;
            (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
                tree, firstId + k, lx, ly, fontSize, chkLayout, glyphCursor, fg, effAlpha);
        }
    }
    elem.ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool EmitCheckOrRadioVisual(
    const ButtonBase& elem,
    bool isRadio,
    bool checked,
    bool indeterminate,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = elem.IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);

    const Thickness pad = elem.GetPadding();
    const float boxSize = 18.0f;
    const float ix = r.x + pad.left;
    const float iy = r.y + pad.top + std::max(0.0f, (h - (pad.top + pad.bottom) - boxSize) * 0.5f);

    const Color outerColor = ModulateInteractiveSurfaceColor(
        Color{0.16f, 0.22f, 0.30f, 1.0f},
        &elem);
    const float outerCr = isRadio ? 9.0f : 4.0f;
    const float outerBt = elem.IsFocused() ? 2.0f : 1.5f;
    const std::uint32_t outerId = ControlRenderEmitter::AllocateRenderNode(
        tree, ix, iy, boxSize, boxSize, outerColor, outerCr, outerBt, &elem);
    nodes[outerId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, outerId, curState);
    ++ioEmittedCount;

    const float markSize = isRadio ? 8.0f : 10.0f;
    const float markInset = (boxSize - markSize) * 0.5f;
    const float markCr = isRadio ? 4.0f : (indeterminate ? 1.5f : 2.0f);
    const float markW = (checked || indeterminate) ? markSize : 0.0f;
    const float markH = checked ? markSize : (indeterminate ? 3.0f : 0.0f);
    const float markY = indeterminate ? (iy + (boxSize - 3.0f) * 0.5f) : (iy + markInset);
    const std::uint32_t markId = ControlRenderEmitter::AllocateRenderNode(
        tree,
        ix + markInset,
        markY,
        markW,
        markH,
        Color{0.26f, 0.66f, 1.0f, 1.0f},
        markCr,
        0.0f,
        &elem);
    nodes[markId].opacity = (checked || indeterminate) ? effAlpha : 0.0f;
    ControlRenderEmitter::ApplyNodeTransform(tree, markId, curState);
    ++ioEmittedCount;

    if (!elem.FirstChild()) {
        const std::string label = elem.GetContentText();
        if (!label.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(elem.GetValue(PropId_FontSize), fontSize);
            const Color fg = value::TryGetColor(elem.GetValue(PropId_Foreground))
                .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
            const float lx = ix + boxSize + 8.0f;
            const float ly = r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f);
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree, lx, ly, fontSize, label, fg, effAlpha, curState, &elem, ResolveElementFontFamily(&elem)));
        }
    }
    return true;
}

// ============================================================================
// Shared ButtonBase & MenuItem Helpers
// ============================================================================

[[nodiscard]] RenderInPlaceResult TryUpdateButtonOrMenuInPlace(
    const UIElement& elem,
    bool isMenuItem,
    const std::string& label,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) {
    if (HasAnyRenderGradientId(elem)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, &elem, firstId, span)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = elem.GetVisibility();

    bool menuHasIcon = false;
    if (isMenuItem) {
        for (const UIElement* mc = elem.FirstChild(); mc != nullptr; mc = mc->NextSibling()) {
            if (!mc->IsA<MenuItem>() && !mc->IsA<Separator>()) {
                menuHasIcon = true;
                break;
            }
        }
    }
    const std::string_view activeLabel = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{label};

    std::uint16_t boxNodeCount = 0u;
    while (boxNodeCount < span && nodes[firstId + boxNodeCount].primType != AeroPrimType::SdfTextGlyph) {
        ++boxNodeCount;
    }
    const auto bbOpt = value::TryGetColor(elem.GetValue(PropId_BorderBrush));
    if (bbOpt.has_value() && boxNodeCount < 2u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    if (boxNodeCount == 0u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    std::size_t totalLabelGlyphCap = 0;
    bool hasLabelNode = false;
    for (std::uint16_t k = boxNodeCount; k < span; ++k) {
        if (nodes[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
            hasLabelNode = true;
            totalLabelGlyphCap += nodes[firstId + k].vertexCount / 4u;
        }
    }
    if (!label.empty() && !hasLabelNode) {
        // Button initially had empty Content="" and gained text -> rebuild cleanly to preserve Z-order
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const float btnAlpha = elem.IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color btnBg = value::TryGetColor(elem.GetValue(PropId_Background))
        .value_or(Color{0.22f, 0.34f, 0.54f, 1.0f});
    btnBg = ModulateInteractiveSurfaceColor(btnBg, &elem);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(elem.GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    Thickness btRaw = SanitizeThickness(
        value::TryGetThickness(elem.GetValue(PropId_BorderThickness))
            .value_or(Thickness{}));
    if (elem.IsFocused()) {
        btRaw = Thickness{
            std::max(2.0f, btRaw.left),
            std::max(2.0f, btRaw.top),
            std::max(2.0f, btRaw.right),
            std::max(2.0f, btRaw.bottom)};
    }

    Color fg{0.96f, 0.98f, 1.0f, 1.0f};
    if (auto fgOpt = value::TryGetColor(elem.GetValue(PropId_Foreground))) {
        fg = *fgOpt;
    }
    float fontSize = 14.0f;
    (void)TryGetFloat(elem.GetValue(PropId_FontSize), fontSize);
    const std::string btnFont = ResolveElementFontFamily(&elem);
    const TextLayoutResult btnLayout = MeasureTextLayout(
        activeLabel, btnFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
    const float lx = isMenuItem
        ? (r.x + (menuHasIcon ? 28.0f : 10.0f))
        : (r.x + std::max(4.0f, (boxW - btnLayout.totalWidth) * 0.5f));
    const float rowBoxH = isMenuItem ? std::min(boxH, 26.0f) : boxH;
    const float ly = r.y + std::max(2.0f, (rowBoxH - fontSize * 1.2f) * 0.5f);

    if (CountLayoutVisibleGlyphs(btnLayout) > totalLabelGlyphCap) {
        return RenderInPlaceResult::NeedsFullRebuild;
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
        RenderNodeDescriptor& fillNode = nodes[firstId];
        fillNode.x = plan.fillRect.x;
        fillNode.y = plan.fillRect.y;
        fillNode.width = plan.fillRect.width;
        fillNode.height = plan.fillRect.height;
        fillNode.opacity = plan.hasVisibleFill ? btnAlpha : 0.0f;
        fillNode.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
        ConfigureRoundedRectDescriptor(fillNode, plan.fillRadii, Thickness{}, false);
        SyncQuadAndWriteDirty(tree, fillNode, curState);

        RenderNodeDescriptor& brdNode = nodes[firstId + 1u];
        brdNode.x = plan.borderRect.x;
        brdNode.y = plan.borderRect.y;
        brdNode.width = plan.hasVisibleBorder ? plan.borderRect.width : 0.0f;
        brdNode.height = plan.hasVisibleBorder ? plan.borderRect.height : 0.0f;
        brdNode.opacity = plan.hasVisibleBorder ? btnAlpha : 0.0f;
        brdNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
        ConfigureRoundedRectDescriptor(brdNode, plan.borderRadii, plan.borderThickness, true);
        SyncQuadAndWriteDirty(tree, brdNode, curState);
    } else {
        RenderNodeDescriptor& boxNode = nodes[firstId];
        boxNode.x = r.x;
        boxNode.y = r.y;
        boxNode.width = boxW;
        boxNode.height = boxH;
        boxNode.opacity = (btnBg.a > 0.0f) ? btnAlpha : 0.0f;
        boxNode.baseRgba = PackColorRgba8(btnBg, 1.0f);
        const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
        ConfigureRoundedRectDescriptor(boxNode, cr, Thickness{legacyBt}, false);
        SyncQuadAndWriteDirty(tree, boxNode, curState);
    }

    std::size_t glyphCursor = 0u;
    for (std::uint16_t k = boxNodeCount; k < span; ++k) {
        RenderNodeDescriptor& node = nodes[firstId + k];
        node.m00 = curState.m00;
        node.m01 = curState.m01;
        node.m10 = curState.m10;
        node.m11 = curState.m11;
        node.baseTx = curState.tx;
        node.baseTy = curState.ty;
        node.inheritedOpacity = curState.inheritedOpacity;
        node.hasPerspective3D = curState.hasPerspective3D;
        node.worldMatrix3D = curState.world3D;
        (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
            tree,
            firstId + k,
            lx,
            ly,
            fontSize,
            btnLayout,
            glyphCursor,
            fg,
            btnAlpha);
    }
    elem.ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool EmitButtonOrMenuVisual(
    const UIElement& elem,
    bool isMenuItem,
    const std::string& label,
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = elem.IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color bgColor{0.22f, 0.34f, 0.54f, 1.0f};
    if (auto bg = value::TryGetColor(elem.GetValue(PropId_Background))) {
        bgColor = *bg;
    }
    bgColor = ModulateInteractiveSurfaceColor(bgColor, &elem);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(elem.GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    Thickness btRaw = SanitizeThickness(
        value::TryGetThickness(elem.GetValue(PropId_BorderThickness))
            .value_or(Thickness{}));
    if (elem.IsFocused()) {
        btRaw = Thickness{
            std::max(2.0f, btRaw.left),
            std::max(2.0f, btRaw.top),
            std::max(2.0f, btRaw.right),
            std::max(2.0f, btRaw.bottom)};
    }
    const auto bbOpt = value::TryGetColor(elem.GetValue(PropId_BorderBrush));
    if (bbOpt.has_value()) {
        const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
            Rect{r.x, r.y, w, h},
            cr,
            bgColor.a > 0.0f,
            bgColor,
            true,
            *bbOpt,
            btRaw);
        const std::uint32_t fillId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x,
            plan.fillRect.y,
            plan.fillRect.width,
            plan.fillRect.height,
            plan.fillColor,
            plan.fillRadii,
            &elem);
        nodes[fillId].opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;

        const std::uint32_t brdId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            &elem);
        nodes[brdId].opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, brdId, curState);
        ++ioEmittedCount;
    } else {
        const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
        const std::uint32_t boxId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, r.x, r.y, w, h, bgColor, cr, &elem);
        ConfigureRoundedRectDescriptor(nodes[boxId], cr, Thickness{legacyBt}, false);
        nodes[boxId].opacity = (bgColor.a > 0.0f) ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, boxId, curState);
        ++ioEmittedCount;
    }

    bool menuHasIcon = false;
    if (isMenuItem) {
        for (const UIElement* mc = elem.FirstChild(); mc != nullptr; mc = mc->NextSibling()) {
            if (!mc->IsA<MenuItem>() && !mc->IsA<Separator>()) {
                menuHasIcon = true;
                break;
            }
        }
    }
    if (!label.empty()) {
        Color fg{0.96f, 0.98f, 1.0f, 1.0f};
        if (auto fgOpt = value::TryGetColor(elem.GetValue(PropId_Foreground))) {
            fg = *fgOpt;
        }
        float fontSize = 14.0f;
        (void)TryGetFloat(elem.GetValue(PropId_FontSize), fontSize);
        const std::string btnFont = ResolveElementFontFamily(&elem);
        const Size labelMetrics = MeasureUtf8TextRun(label, fontSize, btnFont);
        const float lx = isMenuItem
            ? (r.x + (menuHasIcon ? 28.0f : 10.0f))
            : (r.x + std::max(4.0f, (w - labelMetrics.width) * 0.5f));
        const float rowBoxH = isMenuItem ? std::min(h, 26.0f) : h;
        const float ly = r.y + std::max(2.0f, (rowBoxH - fontSize * 1.2f) * 0.5f);
        ioEmittedCount = static_cast<std::uint16_t>(
            ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                tree,
                lx,
                ly,
                fontSize,
                label,
                fg,
                effAlpha,
                curState,
                &elem,
                btnFont));
    }
    return true;
}

} // namespace

// ============================================================================
// Control.hpp: HeaderedContentControl, GroupBox, Expander
// ============================================================================

RenderInPlaceResult HeaderedContentControl::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool HeaderedContentControl::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(this, Rect{r.x, r.y, w, h});
    const std::uint32_t bgGradId = ExtractGradBrushId(GetValue(PropId_BackgroundGradientId));
    if (bgGradId != 0u) {
        const std::uint32_t fid = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree,
            plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
            bgGradId, plan.fillRadii, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effOpacity, this);
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t fid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
            plan.fillColor, plan.fillRadii, this);
        nodes[fid].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    }
    if (plan.reserveBorderNode) {
        const std::uint32_t bid = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            this);
        nodes[bid].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, bid, curState);
        ++ioEmittedCount;
    }
    if (GetHeaderElement() == nullptr) {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            const Rect hdrR = HeaderLocalRect();
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
                .value_or(Color{0.94f, 0.96f, 0.99f, 1.0f});
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree,
                    hdrR.x + 2.0f,
                    hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                    fontSize,
                    hdrText,
                    fg,
                    effOpacity,
                    curState,
                    this,
                    ResolveElementFontFamily(this)));
        }
    }
    return true;
}

bool GroupBox::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    (void)r;
    (void)w;
    (void)h;
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Rect brdR = BodyBorderLocalRect();
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    const Thickness bt = SanitizeThickness(
        value::TryGetThickness(GetValue(PropId_BorderThickness))
            .value_or(Thickness{1.0f}));
    const auto bgOpt = value::TryGetColor(GetValue(PropId_Background));
    const bool hasBg = bgOpt.has_value() && (bgOpt->a > 0.0f);
    const Color brdCol = value::TryGetColor(GetValue(PropId_BorderBrush))
        .value_or(Color{0.30f, 0.38f, 0.48f, 1.0f});

    const std::uint32_t bgGradId = ExtractGradBrushId(GetValue(PropId_BackgroundGradientId));
    if (bgGradId != 0u) {
        const std::uint32_t fid = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree, brdR.x, brdR.y, brdR.width, brdR.height, bgGradId, cr, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effOpacity, this);
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t fid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, brdR.x, brdR.y, brdR.width, brdR.height,
            bgOpt.value_or(Color{0.0f, 0.0f, 0.0f, 0.0f}), cr, this);
        nodes[fid].opacity = hasBg ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    }

    const std::uint32_t bid = ControlRenderEmitter::AllocateBorderRingNode(
        tree, brdR.x, brdR.y, brdR.width, brdR.height, brdCol, cr, bt, this);
    nodes[bid].opacity = (brdCol.a > 0.0f && HasPositiveThickness(bt)) ? effOpacity : 0.0f;
    ControlRenderEmitter::ApplyNodeTransform(tree, bid, curState);
    ++ioEmittedCount;

    if (GetHeaderElement() == nullptr) {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            const Rect hdrR = HeaderLocalRect();
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
                .value_or(Color{0.92f, 0.96f, 1.0f, 1.0f});
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree,
                    hdrR.x + 2.0f,
                    hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                    fontSize,
                    hdrText,
                    fg,
                    effOpacity,
                    curState,
                    this,
                    ResolveElementFontFamily(this)));
        }
    }
    return true;
}

bool Expander::ShouldEmitRenderChild(
    const UIElement* child,
    bool& outCollapseSubtree,
    const UIElement*& outFallbackHeaderOnly) const noexcept {
    outFallbackHeaderOnly = nullptr;
    if (IsBodyChildHidden(child)) {
        outCollapseSubtree = true;
        return false;
    }
    outCollapseSubtree = false;
    return true;
}

bool Expander::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    (void)r;
    (void)w;
    (void)h;
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    const Rect hdrR = HeaderLocalRect();
    Color hdrBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
    hdrBg = ModulateInteractiveSurfaceColor(hdrBg, this);

    const std::uint32_t hdrId = ControlRenderEmitter::AllocateRoundedRectNode(
        tree, hdrR.x, hdrR.y, hdrR.width, hdrR.height, hdrBg, CornerRadius{4.0f}, this);
    nodes[hdrId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, hdrId, curState);
    ++ioEmittedCount;

    // Chevron indicator (expanded vs collapsed cue)
    const float chevW = IsExpanded() ? 8.0f : 6.0f;
    const float chevH = IsExpanded() ? 6.0f : 8.0f;
    const std::uint32_t chevId = ControlRenderEmitter::AllocateRoundedRectNode(
        tree,
        hdrR.x + 8.0f,
        hdrR.y + std::max(0.0f, (hdrR.height - chevH) * 0.5f),
        chevW,
        chevH,
        Color{0.78f, 0.86f, 0.98f, 1.0f},
        CornerRadius{1.5f},
        this);
    nodes[chevId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, chevId, curState);
    ++ioEmittedCount;

    if (IsExpanded()) {
        const Rect bodyR = ContentLocalRect();
        const Color brdCol = value::TryGetColor(GetValue(PropId_BorderBrush))
            .value_or(Color{0.26f, 0.34f, 0.45f, 0.85f});
        const std::uint32_t bodyBrdId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            bodyR.x,
            bodyR.y,
            bodyR.width,
            bodyR.height,
            brdCol,
            CornerRadius{0.0f, 0.0f, 4.0f, 4.0f},
            Thickness{1.0f},
            this);
        nodes[bodyBrdId].opacity = effAlpha;
        ControlRenderEmitter::ApplyNodeTransform(tree, bodyBrdId, curState);
        ++ioEmittedCount;
    }

    if (GetHeaderElement() == nullptr) {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
                .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree,
                    hdrR.x + 22.0f,
                    hdrR.y + std::max(2.0f, (hdrR.height - fontSize * 1.2f) * 0.5f),
                    fontSize,
                    hdrText,
                    fg,
                    effAlpha,
                    curState,
                    this,
                    ResolveElementFontFamily(this)));
        }
    }
    return true;
}

// ============================================================================
// Decorators.hpp: BoxShadow
// ============================================================================

RenderInPlaceResult BoxShadow::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool BoxShadow::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    float boxBlurR = 8.0f;
    (void)TryGetFloat(GetValue(PropId_EffectBlurRadius), boxBlurR);
    boxBlurR = std::max(1.0f, boxBlurR);
    const Color boxCol = value::TryGetColor(GetValue(PropId_EffectColor))
        .value_or(value::TryGetColor(GetValue(PropId_Background))
            .value_or(Color{0.0f, 0.0f, 0.0f, 0.5f}));
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(GetValue(PropId_CornerRadius)).value_or(CornerRadius{}));
    const float pad = std::ceil(boxBlurR);
    const std::uint32_t shId = ControlRenderEmitter::AllocateRoundedRectNode(
        tree, r.x - pad, r.y - pad, w + 2.0f * pad, h + 2.0f * pad, boxCol, cr, this);
    nodes[shId].primType = AeroPrimType::SdfRoundedRect;
    nodes[shId].opacity = (w > 0.0f && h > 0.0f) ? effOpacity : 0.0f;
    nodes[shId].softFeatherPx = boxBlurR;
    nodes[shId].sdfPadPx = pad;
    if (IsUniformCornerRadius(cr)) {
        const auto qPad = static_cast<std::uint16_t>(std::clamp(pad * 2.0f + 0.5f, 0.0f, 2047.0f)) & 0x07FFu;
        nodes[shId].encodedPrimExtra = static_cast<std::uint16_t>(qPad << 5u);
    }
    ControlRenderEmitter::ApplyNodeTransform(tree, shId, curState);
    ++ioEmittedCount;
    return true;
}

// ============================================================================
// Shapes.hpp: Rectangle, Ellipse, Line, Path, Polygon, Polyline
// ============================================================================

RenderInPlaceResult Rectangle::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool Rectangle::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const std::uint32_t fillGradId = GetFillGradientBrushId();
    const bool hasFill = HasFill();
    const Color fillCol = GetFill();
    const float strokeW = GetStrokeThickness();
    const bool hasStroke = HasStroke() && (strokeW > 0.0f);
    const Color strokeCol = GetStroke();
    const CornerRadius cr{EffectiveCornerRadius()};
    const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
        Rect{r.x, r.y, w, h},
        cr,
        hasFill,
        fillCol,
        hasStroke,
        strokeCol,
        Thickness{strokeW});

    if (fillGradId != 0u) {
        const std::uint32_t fillId = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree,
            plan.fillRect.x,
            plan.fillRect.y,
            plan.fillRect.width,
            plan.fillRect.height,
            fillGradId,
            plan.fillRadii,
            false,
            Color{1.0f, 1.0f, 1.0f, 1.0f},
            plan.hasVisibleFill ? effOpacity : 0.0f,
            this);
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t fillId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x,
            plan.fillRect.y,
            plan.fillRect.width,
            plan.fillRect.height,
            plan.fillColor,
            plan.fillRadii,
            this);
        nodes[fillId].opacity = plan.hasVisibleFill ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;
    }
    if (plan.reserveBorderNode) {
        const std::uint32_t brdId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            this);
        nodes[brdId].opacity = plan.hasVisibleBorder ? effOpacity : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, brdId, curState);
        ++ioEmittedCount;
    }
    return true;
}

RenderInPlaceResult Ellipse::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool Ellipse::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    float ellTrimStart = 0.0f;
    float ellTrimEnd = 1.0f;
    (void)TryGetFloat(GetValue(PropId_PathTrimStart), ellTrimStart);
    (void)TryGetFloat(GetValue(PropId_PathTrimEnd), ellTrimEnd);
    ellTrimStart = std::clamp(ellTrimStart, 0.0f, 1.0f);
    ellTrimEnd = std::clamp(ellTrimEnd, 0.0f, 1.0f);
    const bool isTrimmedEll = (ellTrimStart > 0.0f) || (ellTrimEnd < 1.0f);
    const std::uint32_t fillGradId = GetFillGradientBrushId();
    const bool hasFill = HasFill();
    const Color fillCol = GetFill();
    const float strokeW = GetStrokeThickness();
    const bool hasStroke = HasStroke() && (strokeW > 0.0f);
    const Color strokeCol = GetStroke();
    if (isTrimmedEll) {
        const float sweepFrac = ellTrimEnd - ellTrimStart;
        if (hasStroke && sweepFrac > 1e-4f && w > 0.0f && h > 0.0f) {
            const float minDim = std::min(w, h);
            const float innerRatio = (minDim > 1e-3f)
                ? std::clamp(1.0f - (2.0f * strokeW) / minDim, 0.0f, 0.995f)
                : 0.0f;
            const std::uint32_t arcId = ControlRenderEmitter::AllocateRadialArcNode(
                tree,
                r.x,
                r.y,
                w,
                h,
                ellTrimStart * 360.0f,
                sweepFrac * 360.0f,
                innerRatio,
                strokeCol,
                effOpacity,
                this);
            ControlRenderEmitter::ApplyNodeTransform(tree, arcId, curState);
            ++ioEmittedCount;
        } else {
            const std::uint32_t anchorId = ControlRenderEmitter::AllocateRenderNode(
                tree, r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, this);
            nodes[anchorId].opacity = 0.0f;
            ControlRenderEmitter::ApplyNodeTransform(tree, anchorId, curState);
            ++ioEmittedCount;
        }
    } else {
        const float fillInset = (hasFill && hasStroke)
            ? ((strokeCol.a >= 0.999f && strokeW >= 1.0f) ? (0.5f * strokeW) : strokeW)
            : 0.0f;
        const Rect fillRect{
            r.x + fillInset,
            r.y + fillInset,
            std::max(0.0f, w - 2.0f * fillInset),
            std::max(0.0f, h - 2.0f * fillInset)};

        if (fillGradId != 0u) {
            const std::uint32_t fillId = ControlRenderEmitter::AllocateGradientRoundedRectNode(
                tree,
                fillRect.x,
                fillRect.y,
                fillRect.width,
                fillRect.height,
                fillGradId,
                CornerRadius{},
                true,
                Color{1.0f, 1.0f, 1.0f, 1.0f},
                hasFill ? effOpacity : 0.0f,
                this);
            ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
            ++ioEmittedCount;
        } else {
            const std::uint32_t fillId = ControlRenderEmitter::AllocateEllipseNode(
                tree,
                fillRect.x,
                fillRect.y,
                fillRect.width,
                fillRect.height,
                fillCol,
                0.0f,
                false,
                hasFill ? effOpacity : 0.0f,
                this);
            ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
            ++ioEmittedCount;
        }
        if (hasStroke) {
            const std::uint32_t strokeId = ControlRenderEmitter::AllocateEllipseNode(
                tree,
                r.x,
                r.y,
                w,
                h,
                strokeCol,
                strokeW,
                true,
                effOpacity,
                this);
            ControlRenderEmitter::ApplyNodeTransform(tree, strokeId, curState);
            ++ioEmittedCount;
        }
    }
    return true;
}

RenderInPlaceResult Line::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool Line::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    (void)w;
    (void)h;
    const std::uint16_t lineEmitted = ControlRenderEmitter::EmitCapsuleLine(
        tree,
        r.x + GetX1(),
        r.y + GetY1(),
        r.x + GetX2(),
        r.y + GetY2(),
        std::max(1.0f, GetStrokeThickness()),
        GetStroke(),
        HasStroke() ? effOpacity : 0.0f,
        true,
        false,
        curState,
        this);
    ioEmittedCount = static_cast<std::uint16_t>(ioEmittedCount + lineEmitted);
    return true;
}

RenderInPlaceResult Path::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateVectorPathShapeInPlace(*this, tree, curState, r, boxW, boxH, effOpacity);
}

bool Path::TryGetBackdropBlurVectorMask(
    std::string& outPathData,
    Stretch& outStretch) const {
    outPathData = ExtractVectorPathData(*this);
    outStretch = GetStretch();
    return true;
}

bool Path::EmitRenderDropShadowPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeDropShadow(
        *this, tree, curState, effOpacity, sdx, sdy, localBlurR, shCol, shadowOwner, ioEmittedCount);
}

bool Path::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeVisual(*this, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

RenderInPlaceResult Polygon::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateVectorPathShapeInPlace(*this, tree, curState, r, boxW, boxH, effOpacity);
}

bool Polygon::TryGetBackdropBlurVectorMask(
    std::string& outPathData,
    Stretch& outStretch) const {
    outPathData = ExtractVectorPathData(*this);
    outStretch = GetStretch();
    return true;
}

bool Polygon::EmitRenderDropShadowPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeDropShadow(
        *this, tree, curState, effOpacity, sdx, sdy, localBlurR, shCol, shadowOwner, ioEmittedCount);
}

bool Polygon::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeVisual(*this, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

RenderInPlaceResult Polyline::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateVectorPathShapeInPlace(*this, tree, curState, r, boxW, boxH, effOpacity);
}

bool Polyline::TryGetBackdropBlurVectorMask(
    std::string& outPathData,
    Stretch& outStretch) const {
    outPathData = ExtractVectorPathData(*this);
    outStretch = GetStretch();
    return true;
}

bool Polyline::EmitRenderDropShadowPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeDropShadow(
        *this, tree, curState, effOpacity, sdx, sdy, localBlurR, shCol, shadowOwner, ioEmittedCount);
}

bool Polyline::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitVectorPathShapeVisual(*this, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

// ============================================================================
// Buttons.hpp: ButtonBase, CheckBox, RadioButton
// ============================================================================

bool ButtonBase::TryGetRenderGlyphScanText(std::string& outText) const {
    if (!FirstChild()) {
        outText = GetContentText();
    } else {
        outText.clear();
    }
    return true;
}

RenderInPlaceResult ButtonBase::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    const std::string label = (!FirstChild()) ? GetContentText() : std::string{};
    return TryUpdateButtonOrMenuInPlace(
        *this, false, label, tree, curState, r, boxW, boxH, effOpacity);
}

bool ButtonBase::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    const std::string label = (!FirstChild()) ? GetContentText() : std::string{};
    return EmitButtonOrMenuVisual(
        *this, false, label, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

RenderInPlaceResult CheckBox::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateCheckOrRadioInPlace(
        *this, false, IsChecked(), tree, curState, r, boxW, boxH, effOpacity);
}

bool CheckBox::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    const bool indeterminate = std::holds_alternative<NullObject>(GetValue(PropId_IsChecked));
    return EmitCheckOrRadioVisual(
        *this, false, IsChecked(), indeterminate, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

RenderInPlaceResult RadioButton::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateCheckOrRadioInPlace(
        *this, true, IsChecked(), tree, curState, r, boxW, boxH, effOpacity);
}

bool RadioButton::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitCheckOrRadioVisual(
        *this, true, IsChecked(), false, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

// ============================================================================
// RangeControls.hpp: Slider, ProgressBar
// ============================================================================

RenderInPlaceResult Slider::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, this, firstId, span) || span != 3u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = GetVisibility();
    const SliderVisualGeometry geom = ComputeSliderVisualGeometry(*this, r, boxW, boxH);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);

    const Color trackColor = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.16f, 0.20f, 0.26f, 1.0f});
    const Color fillColor = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.24f, 0.56f, 0.96f, 1.0f});
    const Color thumbColor = ModulateInteractiveSurfaceColor(
        Color{0.92f, 0.96f, 1.0f, 1.0f},
        this);
    const float thumbBorder = IsFocused() ? 2.0f : 1.0f;

    auto updateSliderQuad = [&](std::uint32_t nid, const Rect& qr, const Color& col, float cr, float bt) {
        RenderNodeDescriptor& nd = nodes[nid];
        nd.x = qr.x;
        nd.y = qr.y;
        nd.width = (vis == Visibility::Collapsed) ? 0.0f : qr.width;
        nd.height = (vis == Visibility::Collapsed) ? 0.0f : qr.height;
        nd.opacity = effAlpha;
        ConfigureRoundedRectDescriptor(nd, CornerRadius{cr}, Thickness{bt}, false);
        nd.baseRgba = PackColorRgba8(col, 1.0f);
        SyncQuadAndWriteDirty(tree, nd, curState);
    };

    updateSliderQuad(firstId + 0u, geom.trackRect, trackColor, 3.0f, 0.0f);
    updateSliderQuad(firstId + 1u, geom.fillRect, fillColor, 3.0f, 0.0f);
    updateSliderQuad(firstId + 2u, geom.thumbRect, thumbColor, geom.thumbRect.width * 0.5f, thumbBorder);
    ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool Slider::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const SliderVisualGeometry geom = ComputeSliderVisualGeometry(*this, r, w, h);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    const Color trackColor = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.16f, 0.20f, 0.26f, 1.0f});
    const Color fillColor = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.24f, 0.56f, 0.96f, 1.0f});
    const Color thumbColor = ModulateInteractiveSurfaceColor(
        Color{0.92f, 0.96f, 1.0f, 1.0f},
        this);
    const float thumbBorder = IsFocused() ? 2.0f : 1.0f;

    const std::uint32_t trackId = ControlRenderEmitter::AllocateRenderNode(
        tree, geom.trackRect.x, geom.trackRect.y, geom.trackRect.width, geom.trackRect.height,
        trackColor, 3.0f, 0.0f, this);
    nodes[trackId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, trackId, curState);

    const std::uint32_t fillId = ControlRenderEmitter::AllocateRenderNode(
        tree, geom.fillRect.x, geom.fillRect.y, geom.fillRect.width, geom.fillRect.height,
        fillColor, 3.0f, 0.0f, this);
    nodes[fillId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);

    const std::int32_t tickPlace = GetTickPlacement();
    if (tickPlace != 0) {
        std::vector<float> normTicks;
        CollectNormalizedTicks(normTicks);
        const Color tickColor{0.68f, 0.74f, 0.84f, 0.85f};
        const bool isVert = (GetOrientation() == Orientation::Vertical);
        for (const float nt : normTicks) {
            if (isVert) {
                const float usableH = std::max(0.0f, geom.trackRect.height - geom.thumbRect.height);
                const float ty = geom.trackRect.y + geom.thumbRect.height * 0.5f + (1.0f - nt) * usableH;
                if ((tickPlace & 1) != 0) {
                    const std::uint32_t tid = ControlRenderEmitter::AllocateRenderNode(
                        tree, geom.trackRect.x - 6.0f, ty - 0.5f, 4.0f, 1.0f, tickColor, 0.0f, 0.0f, this);
                    nodes[tid].opacity = effAlpha;
                    ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
                    ++ioEmittedCount;
                }
                if ((tickPlace & 2) != 0) {
                    const std::uint32_t tid = ControlRenderEmitter::AllocateRenderNode(
                        tree, geom.trackRect.x + geom.trackRect.width + 2.0f, ty - 0.5f, 4.0f, 1.0f, tickColor, 0.0f, 0.0f, this);
                    nodes[tid].opacity = effAlpha;
                    ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
                    ++ioEmittedCount;
                }
            } else {
                const float usableW = std::max(0.0f, geom.trackRect.width - geom.thumbRect.width);
                const float tx = geom.trackRect.x + geom.thumbRect.width * 0.5f + nt * usableW;
                if ((tickPlace & 1) != 0) {
                    const std::uint32_t tid = ControlRenderEmitter::AllocateRenderNode(
                        tree, tx - 0.5f, geom.trackRect.y - 6.0f, 1.0f, 4.0f, tickColor, 0.0f, 0.0f, this);
                    nodes[tid].opacity = effAlpha;
                    ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
                    ++ioEmittedCount;
                }
                if ((tickPlace & 2) != 0) {
                    const std::uint32_t tid = ControlRenderEmitter::AllocateRenderNode(
                        tree, tx - 0.5f, geom.trackRect.y + geom.trackRect.height + 2.0f, 1.0f, 4.0f, tickColor, 0.0f, 0.0f, this);
                    nodes[tid].opacity = effAlpha;
                    ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
                    ++ioEmittedCount;
                }
            }
        }
    }

    const std::uint32_t thumbId = ControlRenderEmitter::AllocateRenderNode(
        tree, geom.thumbRect.x, geom.thumbRect.y, geom.thumbRect.width, geom.thumbRect.height,
        thumbColor, geom.thumbRect.width * 0.5f, thumbBorder, this);
    nodes[thumbId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, thumbId, curState);

    ioEmittedCount = static_cast<std::uint16_t>(ioEmittedCount + 3u);
    return true;
}

RenderInPlaceResult ProgressBar::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool ProgressBar::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    const Color trackColor = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.15f, 0.19f, 0.26f, 1.0f});
    const std::uint32_t trackGradId = ExtractGradBrushId(GetValue(PropId_BackgroundGradientId));
    if (trackGradId != 0u) {
        const std::uint32_t tid = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree, r.x, r.y, w, h, trackGradId, cr, false, Color{1.0f, 1.0f, 1.0f, 1.0f}, effAlpha, this);
        ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t tid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, r.x, r.y, w, h, trackColor, cr, this);
        nodes[tid].opacity = (trackColor.a > 0.0f) ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
        ++ioEmittedCount;
    }

    const Rect fillRect = ComputeFillBarLocalRect();
    const bool hasFill = (fillRect.width > 0.01f && fillRect.height > 0.01f);
    const Color fillColor = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.18f, 0.68f, 0.42f, 1.0f});
    const std::uint32_t fgGradId = ExtractGradBrushId(GetValue(PropId_ForegroundGradientId));
    if (fgGradId != 0u) {
        const std::uint32_t fid = ControlRenderEmitter::AllocateGradientRoundedRectNode(
            tree,
            fillRect.x,
            fillRect.y,
            hasFill ? fillRect.width : 0.0f,
            hasFill ? fillRect.height : 0.0f,
            fgGradId,
            cr,
            false,
            Color{1.0f, 1.0f, 1.0f, 1.0f},
            hasFill ? effAlpha : 0.0f,
            this);
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    } else {
        const std::uint32_t fid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            fillRect.x,
            fillRect.y,
            hasFill ? fillRect.width : 0.0f,
            hasFill ? fillRect.height : 0.0f,
            fillColor,
            cr,
            this);
        nodes[fid].opacity = hasFill ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fid, curState);
        ++ioEmittedCount;
    }
    return true;
}

// ============================================================================
// TextControls.hpp: TextBlock, Label, TextBoxBase
// ============================================================================

bool TextBlock::TryGetRenderGlyphScanText(std::string& outText) const {
    return TryGetTextBlockOrLabelGlyphScanText(*this, outText);
}

RenderInPlaceResult TextBlock::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateTextBlockOrLabelInPlace(*this, tree, curState, r, boxW, boxH, effOpacity);
}

bool TextBlock::EmitRenderDropShadowPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) const {
    (void)localBlurR;
    return EmitTextBlockOrLabelDropShadow(
        *this, tree, curState, effOpacity, sdx, sdy, shCol, shadowOwner, ioEmittedCount);
}

bool TextBlock::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitTextBlockOrLabelVisual(*this, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

bool Label::TryGetRenderGlyphScanText(std::string& outText) const {
    return TryGetTextBlockOrLabelGlyphScanText(*this, outText);
}

RenderInPlaceResult Label::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    return TryUpdateTextBlockOrLabelInPlace(*this, tree, curState, r, boxW, boxH, effOpacity);
}

bool Label::EmitRenderDropShadowPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    float effOpacity,
    float sdx,
    float sdy,
    float localBlurR,
    const Color& shCol,
    const UIElement* shadowOwner,
    std::uint16_t& ioEmittedCount) const {
    (void)localBlurR;
    return EmitTextBlockOrLabelDropShadow(
        *this, tree, curState, effOpacity, sdx, sdy, shCol, shadowOwner, ioEmittedCount);
}

bool Label::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    return EmitTextBlockOrLabelVisual(*this, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

bool TextBoxBase::TryGetRenderGlyphScanText(std::string& outText) const {
    outText = GetDisplayString();
    if (outText.empty()) {
        outText = GetPlaceholder();
    }
    return true;
}

RenderInPlaceResult TextBoxBase::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, this, firstId, span) ||
        span < 3u || GetSelectionLength() > 0u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = GetVisibility();

    const bool hasSeparateBorderNode =
        (span >= 4u) && (nodes[firstId + 1u].primType != AeroPrimType::SdfTextGlyph);
    const std::uint16_t boxSpan = hasSeparateBorderNode ? 2u : 1u;
    const auto bbOpt = value::TryGetColor(GetValue(PropId_BorderBrush));
    if (bbOpt.has_value() && !hasSeparateBorderNode) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const std::string displayStr = GetDisplayString();
    const bool showingPlaceholder = displayStr.empty();
    const std::string renderedText = showingPlaceholder ? GetPlaceholder() : displayStr;
    const std::string_view activeText = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{renderedText};

    std::size_t totalGlyphCap = 0u;
    for (std::uint16_t k = boxSpan; k + 1u < span; ++k) {
        if (nodes[firstId + k].primType != AeroPrimType::SdfTextGlyph) {
            return RenderInPlaceResult::NeedsFullRebuild;
        }
        totalGlyphCap += nodes[firstId + k].vertexCount / 4u;
    }
    if (CountVisibleUtf8Glyphs(activeText) > totalGlyphCap) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color boxBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.12f, 0.15f, 0.20f, 1.0f});
    boxBg = ModulateInteractiveSurfaceColor(boxBg, this);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    Thickness btRaw = SanitizeThickness(
        value::TryGetThickness(GetValue(PropId_BorderThickness))
            .value_or(Thickness{1.0f}));
    if (IsFocused()) {
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
        RenderNodeDescriptor& fillNode = nodes[firstId];
        fillNode.x = plan.fillRect.x;
        fillNode.y = plan.fillRect.y;
        fillNode.width = plan.fillRect.width;
        fillNode.height = plan.fillRect.height;
        fillNode.opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
        fillNode.baseRgba = PackColorRgba8(plan.fillColor, 1.0f);
        ConfigureRoundedRectDescriptor(fillNode, plan.fillRadii, Thickness{}, false);
        SyncQuadAndWriteDirty(tree, fillNode, curState);

        RenderNodeDescriptor& borderNode = nodes[firstId + 1u];
        borderNode.x = plan.borderRect.x;
        borderNode.y = plan.borderRect.y;
        borderNode.width = plan.hasVisibleBorder ? plan.borderRect.width : 0.0f;
        borderNode.height = plan.hasVisibleBorder ? plan.borderRect.height : 0.0f;
        borderNode.opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
        borderNode.baseRgba = PackColorRgba8(plan.borderColor, 1.0f);
        ConfigureRoundedRectDescriptor(borderNode, plan.borderRadii, plan.borderThickness, true);
        SyncQuadAndWriteDirty(tree, borderNode, curState);
    } else {
        RenderNodeDescriptor& boxNode = nodes[firstId];
        boxNode.x = r.x;
        boxNode.y = r.y;
        boxNode.width = boxW;
        boxNode.height = boxH;
        boxNode.opacity = (boxBg.a > 0.0f) ? effAlpha : 0.0f;
        boxNode.baseRgba = PackColorRgba8(boxBg, 1.0f);
        const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
        ConfigureRoundedRectDescriptor(boxNode, cr, Thickness{legacyBt}, false);
        SyncQuadAndWriteDirty(tree, boxNode, curState);
    }

    // Nodes [boxSpan .. span-2]: UTF-8 text or muted placeholder
    const Thickness pad = GetPadding();
    const float padLeft = (pad.left > 0.0f) ? pad.left : 6.0f;
    const float padTop = (pad.top > 0.0f) ? pad.top : 4.0f;
    const float scrollX = showingPlaceholder ? 0.0f : HorizontalTextScrollOffset();
    const float textX = r.x + padLeft - scrollX;
    const float fontSize = GetFontSize();
    const float textY = (renderedText.find('\n') != std::string::npos)
        ? (r.y + padTop)
        : (r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f));
    const Color textColor = showingPlaceholder
        ? Color{0.55f, 0.60f, 0.68f, 0.85f}
        : GetForeground();

    const TextLayoutResult tboxLayout = MeasureTextLayout(
        activeText, GetFontFamily(), fontSize, 1000000.0f, TextWrapping::NoWrap);
    std::size_t glyphCursor = 0u;
    for (std::uint16_t k = boxSpan; k + 1u < span; ++k) {
        RenderNodeDescriptor& tNode = nodes[firstId + k];
        tNode.m00 = curState.m00;
        tNode.m01 = curState.m01;
        tNode.m10 = curState.m10;
        tNode.m11 = curState.m11;
        tNode.baseTx = curState.tx;
        tNode.baseTy = curState.ty;
        tNode.inheritedOpacity = curState.inheritedOpacity;
        tNode.hasPerspective3D = curState.hasPerspective3D;
        tNode.worldMatrix3D = curState.world3D;
        (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
            tree,
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
        const Rect caretR = CaretLocalRect();
        const bool showCaret = (vis == Visibility::Visible) && IsFocused() &&
                               !IsReadOnly() && IsEffectivelyEnabled();
        RenderNodeDescriptor& caretNode = nodes[firstId + span - 1u];
        caretNode.x = caretR.x;
        caretNode.y = caretR.y;
        caretNode.width = showCaret ? caretR.width : 0.0f;
        caretNode.height = showCaret ? caretR.height : 0.0f;
        caretNode.opacity = showCaret ? effAlpha : 0.0f;
        ConfigureRoundedRectDescriptor(caretNode, CornerRadius{}, Thickness{}, false);
        caretNode.baseRgba = PackColorRgba8(Color{0.38f, 0.76f, 1.0f, 1.0f}, 1.0f);
        SyncQuadAndWriteDirty(tree, caretNode, curState);
    }

    ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool TextBoxBase::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color boxBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.12f, 0.15f, 0.20f, 1.0f});
    boxBg = ModulateInteractiveSurfaceColor(boxBg, this);
    const CornerRadius cr = SanitizeCornerRadius(
        value::TryGetCornerRadius(GetValue(PropId_CornerRadius))
            .value_or(CornerRadius{4.0f}));
    Thickness btRaw = SanitizeThickness(
        value::TryGetThickness(GetValue(PropId_BorderThickness))
            .value_or(Thickness{1.0f}));
    if (IsFocused()) {
        btRaw = Thickness{
            std::max(2.0f, btRaw.left),
            std::max(2.0f, btRaw.top),
            std::max(2.0f, btRaw.right),
            std::max(2.0f, btRaw.bottom)};
    }
    const auto bbOpt = value::TryGetColor(GetValue(PropId_BorderBrush));
    if (bbOpt.has_value()) {
        const SurfaceFillBorderPlan plan = BuildSurfaceFillBorderPlan(
            Rect{r.x, r.y, w, h},
            cr,
            boxBg.a > 0.0f,
            boxBg,
            true,
            *bbOpt,
            btRaw);
        const std::uint32_t fillId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            plan.fillRect.x, plan.fillRect.y, plan.fillRect.width, plan.fillRect.height,
            plan.fillColor, plan.fillRadii, this);
        nodes[fillId].opacity = plan.hasVisibleFill ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, fillId, curState);
        ++ioEmittedCount;

        const std::uint32_t borderId = ControlRenderEmitter::AllocateBorderRingNode(
            tree,
            plan.borderRect.x,
            plan.borderRect.y,
            plan.hasVisibleBorder ? plan.borderRect.width : 0.0f,
            plan.hasVisibleBorder ? plan.borderRect.height : 0.0f,
            plan.borderColor,
            plan.borderRadii,
            plan.borderThickness,
            this);
        nodes[borderId].opacity = plan.hasVisibleBorder ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, borderId, curState);
        ++ioEmittedCount;
    } else {
        const float legacyBt = IsUniformCornerRadius(cr) ? btRaw.left : 0.0f;
        const std::uint32_t boxId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, r.x, r.y, w, h, boxBg, cr, this);
        ConfigureRoundedRectDescriptor(nodes[boxId], cr, Thickness{legacyBt}, false);
        nodes[boxId].opacity = (boxBg.a > 0.0f) ? effAlpha : 0.0f;
        ControlRenderEmitter::ApplyNodeTransform(tree, boxId, curState);
        ++ioEmittedCount;
    }

    const std::string displayStr = GetDisplayString();
    const bool showingPlaceholder = displayStr.empty();
    const std::string renderedText = showingPlaceholder ? GetPlaceholder() : displayStr;
    const Thickness pad = GetPadding();
    const float padLeft = (pad.left > 0.0f) ? pad.left : 6.0f;
    const float padTop = (pad.top > 0.0f) ? pad.top : 4.0f;
    const float scrollX = showingPlaceholder ? 0.0f : HorizontalTextScrollOffset();
    const float textX = r.x + padLeft - scrollX;
    const float fontSize = GetFontSize();
    const float textY = (renderedText.find('\n') != std::string::npos)
        ? (r.y + padTop)
        : (r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f));
    const Color textColor = showingPlaceholder
        ? Color{0.55f, 0.60f, 0.68f, 0.85f}
        : GetForeground();

    if (!showingPlaceholder && GetSelectionLength() > 0u) {
        auto byteOffAtCp = [](std::string_view s, std::size_t cpTarget) noexcept -> std::size_t {
            std::size_t cnt = 0u, cur = 0u;
            std::uint32_t cp = 0u;
            while (cnt < cpTarget && DecodeNextUtf8CodePoint(s, cur, cp)) ++cnt;
            return cur;
        };
        const std::size_t selS = GetSelectionStart();
        const std::size_t selE = selS + GetSelectionLength();
        const std::string fontFam = GetFontFamily();
        const float padRight = (pad.right > 0.0f) ? pad.right : 6.0f;
        const float clipL = r.x + padLeft;
        const float clipR = std::max(clipL, r.x + w - padRight);
        if (displayStr.find('\n') != std::string::npos) {
            const float lineH = std::max(
                12.0f,
                MeasureTextLayout(renderedText, fontFam, std::max(1.0f, fontSize), 1000000.0f, TextWrapping::NoWrap).lineHeight);
            std::size_t scanCursor = 0u;
            std::size_t totalCp = 0u;
            std::size_t lineStartCp = 0u;
            std::size_t lineByteStart = 0u;
            std::size_t lineIdx = 0u;
            std::uint32_t cp = 0u;
            auto emitLineSel = [&](std::size_t lIdx, std::size_t lStartCp, std::size_t lEndCp, std::size_t lByteS, std::size_t lByteE) {
                const std::size_t overlapS = std::max(selS, lStartCp);
                const std::size_t overlapE = std::min(selE, lEndCp);
                if (overlapE <= overlapS) return;
                const std::string_view lv(displayStr.data() + lByteS, lByteE - lByteS);
                const std::size_t lb0 = byteOffAtCp(lv, overlapS - lStartCp);
                const std::size_t lb1 = byteOffAtCp(lv, overlapE - lStartCp);
                const float x0 = textX + MeasureUtf8TextRun(lv.substr(0u, lb0), fontSize, fontFam).width;
                const float x1 = textX + MeasureUtf8TextRun(lv.substr(0u, lb1), fontSize, fontFam).width;
                const float sx0 = std::clamp(x0, clipL, clipR);
                const float sx1 = std::clamp(x1, clipL, clipR);
                if (sx1 > sx0 + 0.5f) {
                    const std::uint32_t selId = ControlRenderEmitter::AllocateRoundedRectNode(
                        tree,
                        sx0,
                        textY + static_cast<float>(lIdx) * lineH,
                        sx1 - sx0,
                        lineH,
                        Color{0.24f, 0.52f, 0.88f, 0.44f},
                        CornerRadius{2.0f},
                        this);
                    ControlRenderEmitter::Nodes(tree)[selId].opacity = effAlpha;
                    ControlRenderEmitter::ApplyNodeTransform(tree, selId, curState);
                    ++ioEmittedCount;
                }
            };
            while (scanCursor < displayStr.size()) {
                const std::size_t prevCursor = scanCursor;
                if (!DecodeNextUtf8CodePoint(displayStr, scanCursor, cp)) break;
                if (cp == '\n') {
                    emitLineSel(lineIdx, lineStartCp, totalCp, lineByteStart, prevCursor);
                    ++lineIdx;
                    ++totalCp;
                    lineStartCp = totalCp;
                    lineByteStart = scanCursor;
                } else {
                    ++totalCp;
                }
            }
            emitLineSel(lineIdx, lineStartCp, totalCp, lineByteStart, displayStr.size());
        } else {
            const std::size_t b0 = byteOffAtCp(displayStr, selS);
            const std::size_t b1 = byteOffAtCp(displayStr, selE);
            const float x0 = textX + MeasureUtf8TextRun(std::string_view(displayStr).substr(0u, b0), fontSize, fontFam).width;
            const float x1 = textX + MeasureUtf8TextRun(std::string_view(displayStr).substr(0u, b1), fontSize, fontFam).width;
            const float sx0 = std::clamp(x0, clipL, clipR);
            const float sx1 = std::clamp(x1, clipL, clipR);
            if (sx1 > sx0 + 0.5f) {
                const Rect cRect = CaretLocalRect();
                const std::uint32_t selId = ControlRenderEmitter::AllocateRoundedRectNode(
                    tree,
                    sx0,
                    cRect.y,
                    sx1 - sx0,
                    cRect.height,
                    Color{0.24f, 0.52f, 0.88f, 0.44f},
                    CornerRadius{2.0f},
                    this);
                nodes[selId].opacity = effAlpha;
                ControlRenderEmitter::ApplyNodeTransform(tree, selId, curState);
                ++ioEmittedCount;
            }
        }
    }

    ioEmittedCount = static_cast<std::uint16_t>(
        ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
            tree,
            textX,
            textY,
            fontSize,
            renderedText,
            textColor,
            effAlpha,
            curState,
            this,
            GetFontFamily()));

    const Rect caretR = CaretLocalRect();
    const bool showCaret = IsFocused() && !IsReadOnly() && IsEffectivelyEnabled();
    const std::uint32_t caretId = ControlRenderEmitter::AllocateRenderNode(
        tree,
        caretR.x,
        caretR.y,
        showCaret ? caretR.width : 0.0f,
        showCaret ? caretR.height : 0.0f,
        Color{0.38f, 0.76f, 1.0f, 1.0f},
        0.0f,
        0.0f,
        this);
    nodes[caretId].opacity = showCaret ? effAlpha : 0.0f;
    ControlRenderEmitter::ApplyNodeTransform(tree, caretId, curState);
    ++ioEmittedCount;
    return true;
}

// ============================================================================
// MediaControls.hpp: Image, MediaElement
// ============================================================================

RenderInPlaceResult Image::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    if (HasAnyRenderGradientId(*this)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, this, firstId, span) || span != 1u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    auto& pages = ControlRenderEmitter::Pages(tree);
    const Visibility vis = GetVisibility();

    RenderNodeDescriptor& imgNode = nodes[firstId];
    if (imgNode.pageIndex >= pages.size()) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    const Thickness pad = GetPadding();
    const Rect contentBox{
        r.x + pad.left,
        r.y + pad.top,
        std::max(0.0f, boxW - (pad.left + pad.right)),
        std::max(0.0f, boxH - (pad.top + pad.bottom))};
    const std::string srcUri = GetSource();
    const ImageAtlasEntry* atlasEntry = (vis == Visibility::Collapsed || srcUri.empty())
        ? nullptr
        : ImageAtlas::Instance().LoadOrGet(srcUri);
    const std::uint32_t desiredTexId = atlasEntry ? atlasEntry->textureId : imgNode.textureId;

    CommandPage& owningPage = pages[imgNode.pageIndex];
    if (atlasEntry && owningPage.header.reserved[0] != desiredTexId) {
        if (owningPage.header.nodeCount == 1u) {
            owningPage.header.reserved[0] = desiredTexId;
        } else {
            return RenderInPlaceResult::NeedsFullRebuild;
        }
    }

    const Color tintColor = value::TryGetColor(GetValue(PropId_Foreground))
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
            GetStretch(),
            GetStretchDirection(),
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

    SyncQuadAndWriteDirty(tree, imgNode, curState);
    ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool Image::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    const Thickness pad = GetPadding();
    const Rect contentBox{
        r.x + pad.left,
        r.y + pad.top,
        std::max(0.0f, w - (pad.left + pad.right)),
        std::max(0.0f, h - (pad.top + pad.bottom))};
    const std::string srcUri = GetSource();
    const ImageAtlasEntry* atlasEntry = srcUri.empty()
        ? nullptr
        : ImageAtlas::Instance().LoadOrGet(srcUri);
    const Color tintColor = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});

    if (atlasEntry && atlasEntry->width > 0u && atlasEntry->height > 0u) {
        const ImageStretchPlacement placement = ComputeImageStretchPlacement(
            contentBox,
            static_cast<float>(atlasEntry->width),
            static_cast<float>(atlasEntry->height),
            GetStretch(),
            GetStretchDirection(),
            atlasEntry->u0,
            atlasEntry->v0,
            atlasEntry->u1,
            atlasEntry->v1);
        if (placement.valid) {
            const std::uint32_t nid = ControlRenderEmitter::AllocateTexturedNode(
                tree,
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
                this,
                atlasEntry->textureId);
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            ++ioEmittedCount;
        } else {
            const std::uint32_t nid = ControlRenderEmitter::AllocateTexturedNode(
                tree,
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
                this,
                atlasEntry->textureId);
            ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
            ++ioEmittedCount;
        }
    } else {
        const std::uint32_t nid = ControlRenderEmitter::AllocateTexturedNode(
            tree,
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
            this,
            0u);
        ControlRenderEmitter::ApplyNodeTransform(tree, nid, curState);
        ++ioEmittedCount;
    }
    return true;
}

RenderInPlaceResult MediaElement::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool MediaElement::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    const Rect contentBox{r.x, r.y, std::max(0.0f, w), std::max(0.0f, h)};
    const std::uint16_t mediaEmitted = EmitProceduralMediaFrame(
        tree, *this, contentBox, effOpacity, curState);
    ioEmittedCount = static_cast<std::uint16_t>(ioEmittedCount + mediaEmitted);
    return true;
}

// ============================================================================
// ItemsControls.hpp: ContentPresenter, ListView, ComboBox, TabControl,
//                    ToolBar, MenuItem, TreeViewItem
// ============================================================================

RenderInPlaceResult ContentPresenter::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, this, firstId, span) || span < 1u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = GetVisibility();

    if (FirstChild() != nullptr) {
        if (span != 1u) {
            return RenderInPlaceResult::NeedsFullRebuild;
        }
        RenderNodeDescriptor& anchorNode = nodes[firstId];
        anchorNode.x = r.x;
        anchorNode.y = r.y;
        anchorNode.width = 0.0f;
        anchorNode.height = 0.0f;
        anchorNode.opacity = 0.0f;
        anchorNode.baseRgba = 0u;
        ConfigureRoundedRectDescriptor(anchorNode, CornerRadius{}, Thickness{}, false);
        SyncQuadAndWriteDirty(tree, anchorNode, curState);
        ClearLayoutDirtyFlags(RenderDirty);
        return RenderInPlaceResult::UpdatedInPlace;
    }

    const std::string txt = GetEffectiveText();
    const std::string_view activeText = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{txt};
    const Color fgColor = GetEffectiveForeground();
    const float fontSize = GetEffectiveFontSize();
    const std::string fontFamily = GetEffectiveFontFamily();
    const TextWrapping wrapMode = GetEffectiveTextWrapping();
    const Thickness pad = GetPadding();
    const float effBoxW = (boxW > 0.0f) ? boxW : DesiredSize().width;
    const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
        ? std::max(1.0f, effBoxW - (pad.left + pad.right))
        : 1000000.0f;
    const TextLayoutResult cpLayout = MeasureTextLayout(
        activeText, fontFamily, fontSize, maxTextW, wrapMode);
    const Point textOrigin = ComputeTextOrigin(
        Rect{r.x, r.y, boxW, boxH},
        Size{cpLayout.totalWidth, cpLayout.totalHeight});

    std::size_t totalGlyphCap = 0u;
    for (std::uint16_t k = 1u; k < span; ++k) {
        if (nodes[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
            totalGlyphCap += nodes[firstId + k].vertexCount / 4u;
        }
    }

    if (CountLayoutVisibleGlyphs(cpLayout) > totalGlyphCap) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    {
        RenderNodeDescriptor& anchorNode = nodes[firstId];
        anchorNode.x = r.x;
        anchorNode.y = r.y;
        anchorNode.width = 0.0f;
        anchorNode.height = 0.0f;
        anchorNode.opacity = 0.0f;
        anchorNode.baseRgba = 0u;
        ConfigureRoundedRectDescriptor(anchorNode, CornerRadius{}, Thickness{}, false);
        SyncQuadAndWriteDirty(tree, anchorNode, curState);
    }

    std::size_t glyphCursor = 0u;
    for (std::uint16_t k = 1u; k < span; ++k) {
        RenderNodeDescriptor& node = nodes[firstId + k];
        node.m00 = curState.m00;
        node.m01 = curState.m01;
        node.m10 = curState.m10;
        node.m11 = curState.m11;
        node.baseTx = curState.tx;
        node.baseTy = curState.ty;
        node.inheritedOpacity = curState.inheritedOpacity;
        node.hasPerspective3D = curState.hasPerspective3D;
        node.worldMatrix3D = curState.world3D;
        (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
            tree,
            firstId + k,
            textOrigin.x,
            textOrigin.y,
            fontSize,
            cpLayout,
            glyphCursor,
            fgColor,
            effOpacity);
    }
    ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool ContentPresenter::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const std::uint32_t anchorId = ControlRenderEmitter::AllocateRenderNode(
        tree, r.x, r.y, 0.0f, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, this);
    nodes[anchorId].opacity = 0.0f;
    ControlRenderEmitter::ApplyNodeTransform(tree, anchorId, curState);
    ++ioEmittedCount;

    if (FirstChild() == nullptr) {
        const std::string text = GetEffectiveText();
        if (!text.empty()) {
            const float fontSize = GetEffectiveFontSize();
            const Color fg = GetEffectiveForeground();
            const std::string fontFamily = GetEffectiveFontFamily();
            const TextWrapping wrapMode = GetEffectiveTextWrapping();
            const Thickness pad = GetPadding();
            const float effBoxW = (w > 0.0f) ? w : DesiredSize().width;
            const float maxTextW = (wrapMode != TextWrapping::NoWrap && effBoxW > 0.0f)
                ? std::max(1.0f, effBoxW - (pad.left + pad.right))
                : 1000000.0f;
            const Size textMetrics = MeasureUtf8TextRun(text, fontSize, fontFamily, maxTextW, wrapMode);
            const Point textOrigin = ComputeTextOrigin(Rect{r.x, r.y, w, h}, textMetrics);
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree,
                    textOrigin.x,
                    textOrigin.y,
                    fontSize,
                    text,
                    fg,
                    effOpacity,
                    curState,
                    this,
                    fontFamily,
                    maxTextW,
                    wrapMode));
        }
    }
    return true;
}

RenderInPlaceResult ListView::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool ListView::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    if (!HasGridViewHeader()) {
        return false;
    }
    const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(this, Rect{r.x, r.y, w, h});
    EmitContainerFillFromPlan(tree, this, plan, curState, effOpacity, ioEmittedCount);

    const float hdrH = ListView::kHeaderBarHeight;
    const std::uint32_t hdrBgId = ControlRenderEmitter::AllocateRoundedRectNode(
        tree,
        r.x + 1.0f, r.y + 1.0f, std::max(0.0f, w - 2.0f), std::min(hdrH, h),
        Color{0.20f, 0.28f, 0.38f, 0.85f}, CornerRadius{2.0f}, this);
    ControlRenderEmitter::Nodes(tree)[hdrBgId].opacity = effOpacity;
    ControlRenderEmitter::ApplyNodeTransform(tree, hdrBgId, curState);
    ++ioEmittedCount;

    const UnifiedValue gv = GetValue(PropId_GridViewColumnsSpec);
    if (const auto* sv = std::get_if<StringValue>(&gv)) {
        const std::string_view allCols = sv->view();
        float colX = r.x + 6.0f;
        std::size_t pos = 0u;
        while (pos <= allCols.size()) {
            const std::size_t recEnd = allCols.find('\x1e', pos);
            const std::string_view rec = (recEnd == std::string_view::npos) ? allCols.substr(pos) : allCols.substr(pos, recEnd - pos);
            const std::size_t f0 = rec.find('\x1f');
            const std::string_view colTitle = (f0 == std::string_view::npos) ? rec : rec.substr(0u, f0);
            float colW = 100.0f;
            if (f0 != std::string_view::npos) {
                const std::size_t f1 = rec.find('\x1f', f0 + 1u);
                const std::string_view wStr = (f1 == std::string_view::npos) ? rec.substr(f0 + 1u) : rec.substr(f0 + 1u, f1 - (f0 + 1u));
                if (!wStr.empty() && wStr != "Auto" && wStr != "NaN") {
                    float parsedW = 0.0f;
                    if (TryGetFloat(ParseBuiltinLiteralPropertyValue(PropId_Width, wStr), parsedW) && parsedW > 0.0f) {
                        colW = parsedW;
                    }
                }
            }
            if (!colTitle.empty()) {
                ioEmittedCount = static_cast<std::uint16_t>(
                    ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                        tree,
                        colX, r.y + 5.0f, 12.5f, std::string(colTitle),
                        Color{0.92f, 0.96f, 1.0f, 1.0f}, effOpacity, curState, this));
            }
            colX += std::max(24.0f, colW);
            if (recEnd == std::string_view::npos) break;
            pos = recEnd + 1u;
        }
    }

    EmitContainerBorderFromPlan(tree, this, plan, curState, effOpacity, ioEmittedCount);
    return true;
}

bool ComboBox::TryGetRenderGlyphScanText(std::string& outText) const {
    outText = GetSelectedItemText();
    return true;
}

RenderInPlaceResult ComboBox::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    if (HasAnyRenderGradientId(*this)) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    const auto& activePopups = ControlRenderEmitter::ActiveComboPopupOwners(tree);
    const bool hadActivePopup = std::find(
        activePopups.begin(),
        activePopups.end(),
        static_cast<const UIElement*>(this)) != activePopups.end();
    if (IsDropDownOpen() || hadActivePopup) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    std::uint32_t firstId = UIElement::InvalidRenderNodeId;
    std::uint16_t span = 0u;
    if (!ResolveElementRenderNodeSpan(tree, this, firstId, span) || span < 3u) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Visibility vis = GetVisibility();

    // Closed ComboBox header in-place update (header box + chevron + text run)
    const std::string selText = GetSelectedItemText();
    const std::string_view activeSel = (vis == Visibility::Collapsed)
        ? std::string_view{}
        : std::string_view{selText};
    std::size_t glyphCap = 0u;
    for (std::uint16_t k = 2u; k < span; ++k) {
        if (nodes[firstId + k].primType == AeroPrimType::SdfTextGlyph) {
            glyphCap += nodes[firstId + k].vertexCount / 4u;
        }
    }
    if (CountVisibleUtf8Glyphs(activeSel) > glyphCap) {
        return RenderInPlaceResult::NeedsFullRebuild;
    }

    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color hdrBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
    hdrBg = ModulateInteractiveSurfaceColor(hdrBg, this);
    const float bt = IsFocused() ? 2.0f : 1.0f;

    RenderNodeDescriptor& hdrNode = nodes[firstId];
    hdrNode.x = r.x;
    hdrNode.y = r.y;
    hdrNode.width = boxW;
    hdrNode.height = boxH;
    hdrNode.opacity = effAlpha;
    ConfigureRoundedRectDescriptor(hdrNode, CornerRadius{4.0f}, Thickness{bt}, false);
    hdrNode.baseRgba = PackColorRgba8(hdrBg, 1.0f);
    SyncQuadAndWriteDirty(tree, hdrNode, curState);

    RenderNodeDescriptor& chevNode = nodes[firstId + 1u];
    chevNode.x = r.x + std::max(0.0f, boxW - 18.0f);
    chevNode.y = r.y + std::max(0.0f, (boxH - 6.0f) * 0.5f);
    chevNode.width = (vis == Visibility::Collapsed) ? 0.0f : 10.0f;
    chevNode.height = (vis == Visibility::Collapsed) ? 0.0f : 6.0f;
    chevNode.opacity = effAlpha;
    ConfigureRoundedRectDescriptor(chevNode, CornerRadius{2.0f}, Thickness{}, false);
    chevNode.baseRgba = PackColorRgba8(Color{0.78f, 0.86f, 0.98f, 1.0f}, 1.0f);
    SyncQuadAndWriteDirty(tree, chevNode, curState);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
    const float tx = r.x + 8.0f;
    const float ty = r.y + std::max(2.0f, (boxH - fontSize * 1.2f) * 0.5f);
    const std::string comboFont = ResolveElementFontFamily(this);
    const TextLayoutResult selLayout = MeasureTextLayout(
        activeSel, comboFont, fontSize, 1000000.0f, TextWrapping::NoWrap);
    std::size_t glyphCursor = 0u;
    for (std::uint16_t k = 2u; k < span; ++k) {
        RenderNodeDescriptor& tn = nodes[firstId + k];
        tn.m00 = curState.m00;
        tn.m01 = curState.m01;
        tn.m10 = curState.m10;
        tn.m11 = curState.m11;
        tn.baseTx = curState.tx;
        tn.baseTy = curState.ty;
        tn.inheritedOpacity = curState.inheritedOpacity;
        tn.hasPerspective3D = curState.hasPerspective3D;
        tn.worldMatrix3D = curState.world3D;
        (void)ControlRenderEmitter::WriteLayoutGlyphSlice(
            tree, firstId + k, tx, ty, fontSize, selLayout, glyphCursor, fg, effAlpha);
    }
    ClearLayoutDirtyFlags(RenderDirty);
    return RenderInPlaceResult::UpdatedInPlace;
}

bool ComboBox::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    Color hdrBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.16f, 0.21f, 0.29f, 1.0f});
    hdrBg = ModulateInteractiveSurfaceColor(hdrBg, this);
    const float bt = IsFocused() ? 2.0f : 1.0f;

    const std::uint32_t hdrId = ControlRenderEmitter::AllocateRenderNode(
        tree, r.x, r.y, w, h, hdrBg, 4.0f, bt, this);
    nodes[hdrId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, hdrId, curState);
    ++ioEmittedCount;

    const std::uint32_t chevId = ControlRenderEmitter::AllocateRenderNode(
        tree,
        r.x + std::max(0.0f, w - 18.0f),
        r.y + std::max(0.0f, (h - 6.0f) * 0.5f),
        10.0f,
        6.0f,
        Color{0.78f, 0.86f, 0.98f, 1.0f},
        2.0f,
        0.0f,
        this);
    nodes[chevId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, chevId, curState);
    ++ioEmittedCount;

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.95f, 0.97f, 1.0f, 1.0f});
    std::string selText = GetDisplayText();
    float textAlpha = effAlpha;
    if (selText.empty()) {
        const UnifiedValue phVal = GetValue(PropId_Placeholder);
        if (const auto* ph = std::get_if<StringValue>(&phVal); ph && !ph->view().empty()) {
            selText = ph->str();
            textAlpha = effAlpha * 0.52f;
        }
    }
    ioEmittedCount = static_cast<std::uint16_t>(
        ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
            tree,
            r.x + 8.0f,
            r.y + std::max(2.0f, (h - fontSize * 1.2f) * 0.5f),
            fontSize,
            selText,
            fg,
            textAlpha,
            curState,
            this,
            ResolveElementFontFamily(this)));

    if (IsDropDownOpen() && IsEffectivelyEnabled() &&
        GetVisibility() == Visibility::Visible && curState.inheritedOpacity > 0.0f &&
        ComboItemCount() > 0u) {
        ControlRenderEmitter::QueueDeferredComboPopup(tree, this, curState);
    }
    return true;
}

void ComboBox::EmitDeferredOverlayPopupPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& st) const {
    if (!IsDropDownOpen() || !IsEffectivelyEnabled() ||
        GetVisibility() != Visibility::Visible || st.inheritedOpacity <= 0.0f) {
        return;
    }
    const std::size_t itemCount = ComboItemCount();
    if (itemCount == 0u) {
        return;
    }
    ControlRenderEmitter::ActiveComboPopupOwners(tree).push_back(this);
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const Rect popupR = DropDownPopupLocalRect();
    const float effAlpha = st.inheritedOpacity;

    auto syncPopupQuad = [&](std::uint32_t nid) {
        RenderNodeDescriptor& nd = nodes[nid];
        nd.m00 = st.m00;
        nd.m01 = st.m01;
        nd.m10 = st.m10;
        nd.m11 = st.m11;
        nd.baseTx = st.tx;
        nd.baseTy = st.ty;
        nd.tx = st.tx + nd.x * (st.m00 - 1.0f) + nd.y * st.m01;
        nd.ty = st.ty + nd.x * st.m10 + nd.y * (st.m11 - 1.0f);
        nd.inheritedOpacity = st.inheritedOpacity;
        ControlRenderEmitter::WriteQuadVertices(tree, nd);
    };

    // Popup backdrop panel
    const std::uint32_t bgNid = ControlRenderEmitter::AllocateRenderNode(
        tree,
        popupR.x,
        popupR.y,
        popupR.width,
        popupR.height,
        Color{0.11f, 0.15f, 0.22f, 0.98f},
        4.0f,
        1.0f,
        nullptr);
    nodes[bgNid].opacity = effAlpha;
    syncPopupQuad(bgNid);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const std::int32_t selIdx = GetSelectedIndex();
    const std::int32_t hiIdx = HighlightedPopupIndex();

    for (std::size_t i = 0u; i < itemCount; ++i) {
        const float rowY = popupR.y + static_cast<float>(i) * ComboBox::kPopupRowHeight;
        const auto iSigned = static_cast<std::int32_t>(i);
        if (iSigned == hiIdx || iSigned == selIdx) {
            const Color rowHighlight = (iSigned == hiIdx)
                ? Color{0.25f, 0.52f, 0.92f, 0.92f}
                : Color{0.20f, 0.34f, 0.56f, 0.75f};
            const std::uint32_t rowId = ControlRenderEmitter::AllocateRenderNode(
                tree,
                popupR.x + 2.0f,
                rowY + 1.0f,
                std::max(0.0f, popupR.width - 4.0f),
                ComboBox::kPopupRowHeight - 2.0f,
                rowHighlight,
                3.0f,
                0.0f,
                nullptr);
            nodes[rowId].opacity = effAlpha;
            syncPopupQuad(rowId);
        }
        const std::string itemLabel = GetComboItemText(i);
        if (!itemLabel.empty()) {
            (void)ControlRenderEmitter::EmitTextRunNodes(
                tree,
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

RenderInPlaceResult TabControl::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool TabControl::ShouldEmitRenderChild(
    const UIElement* child,
    bool& outCollapseSubtree,
    const UIElement*& outFallbackHeaderOnly) const noexcept {
    outFallbackHeaderOnly = nullptr;
    if (IsChildInactiveTabPage(child)) {
        outCollapseSubtree = true;
        if (const auto* childHcc = child ? child->DynamicCast<HeaderedContentControl>() : nullptr) {
            outFallbackHeaderOnly = childHcc->GetHeaderElement();
        }
        return false;
    }
    outCollapseSubtree = false;
    return true;
}

bool TabControl::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    (void)r;
    (void)w;
    (void)h;
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float effAlpha = IsEffectivelyEnabled() ? effOpacity : (effOpacity * 0.55f);
    const Rect pageR = ContentPanelLocalRect();
    const Color pageBg = value::TryGetColor(GetValue(PropId_Background))
        .value_or(Color{0.12f, 0.15f, 0.21f, 1.0f});
    const Color pageBrd = value::TryGetColor(GetValue(PropId_BorderBrush))
        .value_or(Color{0.28f, 0.36f, 0.48f, 1.0f});

    const TabStripPlacement tabPlacement = GetTabStripPlacement();
    CornerRadius pageCr{0.0f, 4.0f, 4.0f, 4.0f};
    CornerRadius tabCr{5.0f, 5.0f, 0.0f, 0.0f};
    if (tabPlacement == TabStripPlacement::Bottom) {
        pageCr = CornerRadius{4.0f, 4.0f, 4.0f, 0.0f};
        tabCr = CornerRadius{0.0f, 0.0f, 5.0f, 5.0f};
    } else if (tabPlacement == TabStripPlacement::Left) {
        pageCr = CornerRadius{0.0f, 4.0f, 4.0f, 4.0f};
        tabCr = CornerRadius{5.0f, 0.0f, 0.0f, 5.0f};
    } else if (tabPlacement == TabStripPlacement::Right) {
        pageCr = CornerRadius{4.0f, 0.0f, 4.0f, 4.0f};
        tabCr = CornerRadius{0.0f, 5.0f, 5.0f, 0.0f};
    }

    const std::uint32_t pageBgId = ControlRenderEmitter::AllocateRoundedRectNode(
        tree, pageR.x, pageR.y, pageR.width, pageR.height, pageBg, pageCr, this);
    nodes[pageBgId].opacity = (pageBg.a > 0.0f) ? effAlpha : 0.0f;
    ControlRenderEmitter::ApplyNodeTransform(tree, pageBgId, curState);
    ++ioEmittedCount;

    const std::uint32_t pageBrdId = ControlRenderEmitter::AllocateBorderRingNode(
        tree, pageR.x, pageR.y, pageR.width, pageR.height, pageBrd, pageCr, Thickness{1.0f}, this);
    nodes[pageBrdId].opacity = effAlpha;
    ControlRenderEmitter::ApplyNodeTransform(tree, pageBrdId, curState);
    ++ioEmittedCount;

    const std::size_t tabCount = TabCount();
    const std::int32_t activeIdx = GetSelectedIndex();
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const std::string tabFont = ResolveElementFontFamily(this);
    const bool isVertTabs =
        (tabPlacement == TabStripPlacement::Left || tabPlacement == TabStripPlacement::Right);

    for (std::size_t i = 0u; i < tabCount; ++i) {
        const Rect tr = GetTabHeaderLocalRect(i);
        const bool isActive = (static_cast<std::int32_t>(i) == activeIdx);
        const Color tabBg = isActive
            ? Color{0.22f, 0.42f, 0.72f, 1.0f}
            : Color{0.15f, 0.19f, 0.26f, 1.0f};
        const std::uint32_t tid = ControlRenderEmitter::AllocateRoundedRectNode(
            tree,
            tr.x,
            tr.y,
            isVertTabs ? tr.width : std::max(0.0f, tr.width - 2.0f),
            isVertTabs ? std::max(0.0f, tr.height - 2.0f) : tr.height,
            tabBg,
            tabCr,
            this);
        nodes[tid].opacity = effAlpha;
        ControlRenderEmitter::ApplyNodeTransform(tree, tid, curState);
        ++ioEmittedCount;

        const UIElement* tabElem = GetTabElement(i);
        const auto* hcc = tabElem ? tabElem->DynamicCast<HeaderedContentControl>() : nullptr;
        if (!hcc || hcc->GetHeaderElement() == nullptr) {
            const std::string hdrLabel = GetTabHeaderText(i);
            if (!hdrLabel.empty()) {
                const Size lm = MeasureUtf8TextRun(hdrLabel, fontSize, tabFont);
                const float tx = tr.x + std::max(6.0f, (tr.width - lm.width) * 0.5f);
                const float ty = tr.y + std::max(2.0f, (tr.height - fontSize * 1.2f) * 0.5f);
                const Color fg = isActive
                    ? Color{0.98f, 0.99f, 1.0f, 1.0f}
                    : Color{0.76f, 0.82f, 0.90f, 1.0f};
                ioEmittedCount = static_cast<std::uint16_t>(
                    ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                        tree, tx, ty, fontSize, hdrLabel, fg, effAlpha, curState, this, tabFont));
            }
        }
    }
    return true;
}

RenderInPlaceResult ToolBar::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    (void)tree;
    (void)curState;
    (void)r;
    (void)boxW;
    (void)boxH;
    (void)effOpacity;
    return RenderInPlaceResult::NeedsFullRebuild;
}

bool ToolBar::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    const std::string tbHdr = GetHeaderText();
    if (tbHdr.empty()) {
        return false;
    }
    const SurfaceFillBorderPlan plan = ComputeContainerFillBorderPlan(this, Rect{r.x, r.y, w, h});
    EmitContainerFillFromPlan(tree, this, plan, curState, effOpacity, ioEmittedCount);

    ioEmittedCount = static_cast<std::uint16_t>(
        ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
            tree,
            r.x + 6.0f, r.y + std::max(2.0f, (h - 15.0f) * 0.5f), 13.0f, tbHdr,
            Color{0.82f, 0.88f, 0.96f, 1.0f}, effOpacity, curState, this));

    EmitContainerBorderFromPlan(tree, this, plan, curState, effOpacity, ioEmittedCount);
    return true;
}

bool MenuItem::TryGetRenderGlyphScanText(std::string& outText) const {
    outText = GetContentText();
    if (outText.empty()) {
        const UnifiedValue hv = GetValue(PropId_Header);
        if (const auto* sv = std::get_if<StringValue>(&hv)) {
            outText = sv->str();
        }
    }
    return true;
}

RenderInPlaceResult MenuItem::TryUpdateRenderElementInPlace(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float boxW,
    float boxH,
    float effOpacity) const {
    std::string label;
    (void)TryGetRenderGlyphScanText(label);
    return TryUpdateButtonOrMenuInPlace(
        *this, true, label, tree, curState, r, boxW, boxH, effOpacity);
}

bool MenuItem::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    std::string label;
    (void)TryGetRenderGlyphScanText(label);
    return EmitButtonOrMenuVisual(
        *this, true, label, tree, curState, r, w, h, effOpacity, ioEmittedCount);
}

bool TreeViewItem::ShouldEmitRenderChild(
    const UIElement* child,
    bool& outCollapseSubtree,
    const UIElement*& outFallbackHeaderOnly) const noexcept {
    outFallbackHeaderOnly = nullptr;
    if (!HasControlTemplate() && !IsExpanded() && child != nullptr) {
        const UnifiedValue chv = child->GetValue(PropId_Header);
        if (!(std::holds_alternative<bool>(chv) && std::get<bool>(chv))) {
            outCollapseSubtree = true;
            return false;
        }
    }
    outCollapseSubtree = false;
    return true;
}

bool TreeViewItem::EmitRenderVisualPrimitives(
    PagedRenderTree& tree,
    const PagedRenderTraversalState& curState,
    Rect r,
    float w,
    float h,
    float effOpacity,
    std::uint16_t& ioEmittedCount) const {
    if (HasControlTemplate()) {
        return false;
    }
    auto& nodes = ControlRenderEmitter::Nodes(tree);
    const float hdrH = 24.0f;
    if (IsSelected() || IsHovered()) {
        const Color selBg = IsSelected()
            ? Color{0.20f, 0.44f, 0.82f, 0.42f}
            : Color{1.0f, 1.0f, 1.0f, 0.08f};
        const std::uint32_t bgId = ControlRenderEmitter::AllocateRoundedRectNode(
            tree, r.x, r.y, w, std::min(h, hdrH), selBg, CornerRadius{4.0f}, this);
        nodes[bgId].opacity = effOpacity;
        ControlRenderEmitter::ApplyNodeTransform(tree, bgId, curState);
        ++ioEmittedCount;
    }
    const UnifiedValue hiVal = GetValue(PropId_HasItems);
    const bool hasSub = std::holds_alternative<bool>(hiVal) && std::get<bool>(hiVal);
    const Color fg = value::TryGetColor(GetValue(PropId_Foreground))
        .value_or(Color{0.90f, 0.93f, 0.98f, 1.0f});
    float fs = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fs);
    if (hasSub) {
        const std::string glyph = IsExpanded() ? "v" : ">";
        ioEmittedCount = static_cast<std::uint16_t>(
            ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                tree,
                r.x + 6.0f, r.y + 4.0f, fs * 0.85f, glyph, fg, effOpacity * 0.85f, curState, this));
    }
    bool hasHeaderElem = false;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const UnifiedValue chv = c->GetValue(PropId_Header);
        if (std::holds_alternative<bool>(chv) && std::get<bool>(chv)) {
            hasHeaderElem = true;
            break;
        }
    }
    if (!hasHeaderElem) {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            ioEmittedCount = static_cast<std::uint16_t>(
                ioEmittedCount + ControlRenderEmitter::EmitTextRunNodes(
                    tree,
                    r.x + 22.0f, r.y + 3.0f, fs, hdrText, fg, effOpacity, curState, this));
        }
    }
    return true;
}

} // namespace aero
