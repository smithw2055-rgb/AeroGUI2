#include "aero/visual/VisualTree.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kLayoutEps = 1e-4f;

[[nodiscard]] bool SameFloatOrBothNaN(float a, float b) noexcept {
    if (std::isnan(a) && std::isnan(b)) {
        return true;
    }
    if (std::isinf(a) && std::isinf(b)) {
        return (a > 0.0f) == (b > 0.0f);
    }
    if (!std::isfinite(a) || !std::isfinite(b)) {
        return false;
    }
    return std::fabs(a - b) <= kLayoutEps;
}

[[nodiscard]] bool SameSizeConstraint(const Size& a, const Size& b) noexcept {
    return SameFloatOrBothNaN(a.width, b.width) && SameFloatOrBothNaN(a.height, b.height);
}

[[nodiscard]] bool SameRectExact(const Rect& a, const Rect& b) noexcept {
    return SameFloatOrBothNaN(a.x, b.x) &&
           SameFloatOrBothNaN(a.y, b.y) &&
           SameFloatOrBothNaN(a.width, b.width) &&
           SameFloatOrBothNaN(a.height, b.height);
}

void ComputeAxisConstraint(
    float explicitDim,
    float minDim,
    float maxDim,
    float padTotal,
    float marginTotal,
    float availOuter,
    bool allowInfinity,
    float& outMinContent,
    float& outMaxContent,
    float& outInnerAvail) noexcept {
    const float safeMin = std::max(0.0f, std::isfinite(minDim) ? minDim : 0.0f);
    const float rawMax = (std::isnan(maxDim) || maxDim < 0.0f) ? kInf : maxDim;
    const float safeMax = std::max(safeMin, rawMax);

    outMinContent = safeMin;
    outMaxContent = safeMax;

    if (std::isfinite(explicitDim) && explicitDim >= 0.0f) {
        const float clampedExplicit = std::clamp(explicitDim, safeMin, safeMax);
        outMinContent = clampedExplicit;
        outMaxContent = clampedExplicit;
        outInnerAvail = std::max(0.0f, clampedExplicit - padTotal);
        return;
    }

    if (std::isfinite(availOuter)) {
        const float availBox = std::max(0.0f, availOuter - marginTotal);
        const float boundedBox = std::clamp(availBox, safeMin, safeMax);
        outInnerAvail = std::max(0.0f, boundedBox - padTotal);
        return;
    }

    if (allowInfinity) {
        outInnerAvail = std::isfinite(safeMax) ? std::max(0.0f, safeMax - padTotal) : kInf;
    } else {
        outInnerAvail = std::max(0.0f, safeMin - padTotal);
    }
}

[[nodiscard]] Matrix2D ComputeLayoutTransformLinearMatrix(const VisualLayoutTransform2D& lt) noexcept {
    Matrix2D m = Matrix2D::Identity();
    if (std::fabs(lt.sx - 1.0f) > 1e-6f || std::fabs(lt.sy - 1.0f) > 1e-6f) {
        m = Matrix2D::Multiply(Matrix2D::Scaling(lt.sx, lt.sy), m);
    }
    if (std::fabs(lt.skewX) > 1e-6f || std::fabs(lt.skewY) > 1e-6f) {
        m = Matrix2D::Multiply(Matrix2D::Skew(lt.skewX, lt.skewY), m);
    }
    if (std::fabs(lt.rotDeg) > 1e-6f) {
        m = Matrix2D::Multiply(Matrix2D::Rotation(lt.rotDeg), m);
    }
    return m;
}

void InverseBoxConstraintUnderLayoutTransform(
    const Matrix2D& m,
    float outerBoxW,
    float outerBoxH,
    float singularFallbackW,
    float singularFallbackH,
    float& outLocalW,
    float& outLocalH) noexcept {
    const float a = std::fabs(m.m00);
    const float b = std::fabs(m.m01);
    const float c = std::fabs(m.m10);
    const float d = std::fabs(m.m11);
    const bool finiteW = std::isfinite(outerBoxW);
    const bool finiteH = std::isfinite(outerBoxH);

    if (a <= 1e-5f && c <= 1e-5f) {
        outLocalW = singularFallbackW;
    } else {
        const float wFromW = (a > 1e-5f && finiteW) ? (std::max(0.0f, outerBoxW) / a) : kInf;
        const float wFromH = (c > 1e-5f && finiteH) ? (std::max(0.0f, outerBoxH) / c) : kInf;
        outLocalW = std::min(wFromW, wFromH);
    }

    if (b <= 1e-5f && d <= 1e-5f) {
        outLocalH = singularFallbackH;
    } else {
        const float hFromW = (b > 1e-5f && finiteW) ? (std::max(0.0f, outerBoxW) / b) : kInf;
        const float hFromH = (d > 1e-5f && finiteH) ? (std::max(0.0f, outerBoxH) / d) : kInf;
        outLocalH = std::min(hFromW, hFromH);
    }

    if (std::isfinite(outLocalW) && std::isfinite(outLocalH) &&
        ((a > 1e-5f && b > 1e-5f && finiteW) || (c > 1e-5f && d > 1e-5f && finiteH))) {
        const float needW = a * outLocalW + b * outLocalH;
        const float needH = c * outLocalW + d * outLocalH;
        float ratio = 1.0f;
        if (finiteW && needW > 1e-5f) {
            ratio = std::min(ratio, std::max(0.0f, outerBoxW) / needW);
        }
        if (finiteH && needH > 1e-5f) {
            ratio = std::min(ratio, std::max(0.0f, outerBoxH) / needH);
        }
        outLocalW *= ratio;
        outLocalH *= ratio;
    }
}

void ComputeViewboxScale(
    float availW,
    float availH,
    float contentW,
    float contentH,
    Stretch stretch,
    StretchDirection dir,
    float& outSx,
    float& outSy) noexcept {
    if (contentW <= 1e-5f || contentH <= 1e-5f) {
        outSx = 1.0f;
        outSy = 1.0f;
        return;
    }

    const bool infW = !std::isfinite(availW);
    const bool infH = !std::isfinite(availH);
    if (infW && infH) {
        outSx = 1.0f;
        outSy = 1.0f;
        return;
    }

    float sx = infW ? 1.0f : (std::max(0.0f, availW) / contentW);
    float sy = infH ? 1.0f : (std::max(0.0f, availH) / contentH);

    switch (stretch) {
        case Stretch::None:
            sx = 1.0f;
            sy = 1.0f;
            break;
        case Stretch::Fill:
            if (infW && !infH) {
                sx = sy;
            } else if (infH && !infW) {
                sy = sx;
            }
            break;
        case Stretch::Uniform: {
            if (infW) {
                sx = sy;
            } else if (infH) {
                sy = sx;
            }
            const float u = std::min(sx, sy);
            sx = u;
            sy = u;
            break;
        }
        case Stretch::UniformToFill: {
            if (infW) {
                sx = sy;
            } else if (infH) {
                sy = sx;
            }
            const float u = std::max(sx, sy);
            sx = u;
            sy = u;
            break;
        }
    }

    if (stretch != Stretch::None) {
        if (dir == StretchDirection::UpOnly) {
            sx = std::max(1.0f, sx);
            sy = std::max(1.0f, sy);
        } else if (dir == StretchDirection::DownOnly) {
            sx = std::min(1.0f, sx);
            sy = std::min(1.0f, sy);
        }
    }

    outSx = std::isfinite(sx) ? std::max(0.0f, sx) : 1.0f;
    outSy = std::isfinite(sy) ? std::max(0.0f, sy) : 1.0f;
}

[[nodiscard]] float ResolveStretchedExtent(
    float naturalW,
    float naturalH,
    float availW,
    float availH,
    Stretch stretch,
    StretchDirection dir,
    Size& outSize) noexcept {
    if (naturalW <= 0.0f || naturalH <= 0.0f) {
        outSize = Size{0.0f, 0.0f};
        return 0.0f;
    }
    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScale(availW, availH, naturalW, naturalH, stretch, dir, sx, sy);
    outSize = Size{naturalW * sx, naturalH * sy};
    return sx;
}

[[nodiscard]] Size MeasureIntrinsicContent(
    VisualWorld& world,
    const VisualNode& node,
    float innerAvailW,
    float innerAvailH) {
    switch (node.kind) {
        case VisualKind::Text: {
            if (node.Text().text.empty() && node.Text().richSpans.empty()) {
                return Size{0.0f, std::max(1.0f, node.Text().fontSize) * 1.25f};
            }
            const float maxW = (node.Text().wrap == TextWrapping::NoWrap || !std::isfinite(innerAvailW))
                ? 1000000.0f
                : std::max(1.0f, innerAvailW);
            return world.MeasureTextNode(node, maxW);
        }
        case VisualKind::Image: {
            if (node.Image().uri.empty()) {
                return Size{0.0f, 0.0f};
            }
            UIResourceStore* store = world.ResourceStore();
            if (!store) {
                return Size{0.0f, 0.0f};
            }
            const ImageAtlasEntry* entry = store->Images().LoadOrGet(node.Image().uri);
            if (!entry || entry->width == 0u || entry->height == 0u) {
                return Size{0.0f, 0.0f};
            }
            Size out{};
            (void)ResolveStretchedExtent(
                static_cast<float>(entry->width),
                static_cast<float>(entry->height),
                innerAvailW,
                innerAvailH,
                node.Image().stretch,
                node.Image().stretchDir,
                out);
            return out;
        }
        case VisualKind::Vector: {
            if (node.Vector().shapeKind == VectorShapeKind::SvgPath && !node.Vector().pathOrParams.empty()) {
                const Rect bounds = MeasureSvgPathBounds(node.Vector().pathOrParams);
                if (node.Vector().stretch == Stretch::None) {
                    if (bounds.width > 0.0f || bounds.height > 0.0f) {
                        return Size{
                            std::max(0.0f, bounds.x + bounds.width),
                            std::max(0.0f, bounds.y + bounds.height)};
                    }
                    return Size{0.0f, 0.0f};
                }
                if (bounds.width > 0.0f && bounds.height > 0.0f) {
                    Size out{};
                    (void)ResolveStretchedExtent(
                        bounds.width,
                        bounds.height,
                        innerAvailW,
                        innerAvailH,
                        node.Vector().stretch,
                        StretchDirection::Both,
                        out);
                    return out;
                }
            }
            return Size{0.0f, 0.0f};
        }
        case VisualKind::Box:
        case VisualKind::CanvasDraw:
        default:
            return Size{0.0f, 0.0f};
    }
}

Size MeasureNode(VisualWorld& world, VisualNodeId id, Size availSize);
void ArrangeNode(
    VisualWorld& world,
    VisualNodeId id,
    Rect finalSlot,
    bool parentWasViewbox,
    float viewboxSx,
    float viewboxSy,
    float viewboxOx,
    float viewboxOy);

struct GridTrackSolved {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
    float size = 0.0f;
    float offset = 0.0f;
};

void BuildGridTrackDefs(
    const std::vector<VisualGridLength>& defs,
    std::vector<GridTrackSolved>& outTracks) {
    outTracks.clear();
    if (defs.empty()) {
        outTracks.push_back(GridTrackSolved{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
        return;
    }
    outTracks.reserve(defs.size());
    for (const VisualGridLength& d : defs) {
        outTracks.push_back(GridTrackSolved{
            d.unit,
            (std::isfinite(d.value) && d.value >= 0.0f) ? d.value : 0.0f,
            0.0f,
            0.0f});
    }
}

void FinalizeGridTrackOffsets(std::vector<GridTrackSolved>& tracks, float origin) noexcept {
    float cur = origin;
    for (GridTrackSolved& t : tracks) {
        t.offset = cur;
        cur += t.size;
    }
}

[[nodiscard]] float SumTrackSpan(
    const std::vector<GridTrackSolved>& tracks,
    std::size_t start,
    std::size_t span) noexcept {
    const std::size_t end = std::min(tracks.size(), start + span);
    float sum = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        sum += tracks[i].size;
    }
    return sum;
}

[[nodiscard]] bool SpanHasStar(
    const std::vector<GridTrackSolved>& tracks,
    std::size_t start,
    std::size_t span) noexcept {
    const std::size_t end = std::min(tracks.size(), start + span);
    for (std::size_t i = start; i < end; ++i) {
        if (tracks[i].unit == GridUnitType::Star) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] float SpanAvailExtent(
    const std::vector<GridTrackSolved>& tracks,
    std::size_t start,
    std::size_t span) noexcept {
    const std::size_t end = std::min(tracks.size(), start + span);
    float sum = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        if (tracks[i].unit == GridUnitType::Auto || !std::isfinite(tracks[i].size)) {
            return kInf;
        }
        sum += tracks[i].size;
    }
    return sum;
}

void DistributeGridSpanDemand(
    std::vector<GridTrackSolved>& tracks,
    std::size_t start,
    std::size_t span,
    GridUnitType targetUnit,
    float neededSize) noexcept {
    const std::size_t end = std::min(tracks.size(), start + span);
    float existing = 0.0f;
    std::size_t matchCount = 0u;
    float weightSum = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        existing += tracks[i].size;
        if (tracks[i].unit == targetUnit) {
            ++matchCount;
            weightSum += std::max(0.0f, tracks[i].value);
        }
    }
    const float deficit = neededSize - existing;
    if (deficit <= 0.0f || matchCount == 0u) {
        return;
    }
    for (std::size_t i = start; i < end; ++i) {
        if (tracks[i].unit != targetUnit) {
            continue;
        }
        const float share = (targetUnit == GridUnitType::Star && weightSum > 0.0f)
            ? (deficit * (tracks[i].value / weightSum))
            : (deficit / static_cast<float>(matchCount));
        tracks[i].size += share;
    }
}

void ResolveStarTracks(
    std::vector<GridTrackSolved>& tracks,
    float totalAvail) noexcept {
    float fixedSum = 0.0f;
    float totalStarWeight = 0.0f;
    for (const GridTrackSolved& t : tracks) {
        if (t.unit == GridUnitType::Star) {
            totalStarWeight += std::max(0.0f, t.value);
        } else {
            fixedSum += t.size;
        }
    }
    if (totalStarWeight <= 0.0f) {
        return;
    }
    if (std::isfinite(totalAvail)) {
        const float remaining = std::max(0.0f, totalAvail - fixedSum);
        const float unitSize = remaining / totalStarWeight;
        for (GridTrackSolved& t : tracks) {
            if (t.unit == GridUnitType::Star) {
                t.size = std::max(0.0f, t.value) * unitSize;
            }
        }
    }
}

void ResolveStarTracksFromInfinite(
    std::vector<GridTrackSolved>& tracks,
    std::size_t start,
    std::size_t span,
    float childDesired) noexcept {
    const std::size_t end = std::min(tracks.size(), start + span);
    float nonStar = 0.0f;
    float starWeights = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        if (tracks[i].unit == GridUnitType::Star) {
            starWeights += std::max(0.0f, tracks[i].value);
        } else {
            nonStar += tracks[i].size;
        }
    }
    if (starWeights <= 0.0f) {
        return;
    }
    const float neededStar = std::max(0.0f, childDesired - nonStar);
    const float minUnit = neededStar / starWeights;
    for (GridTrackSolved& t : tracks) {
        if (t.unit == GridUnitType::Star) {
            t.size = std::max(t.size, std::max(0.0f, t.value) * minUnit);
        }
    }
}

[[nodiscard]] Size MeasureGridPanel(
    VisualWorld& world,
    const VisualNode& parent,
    float availW,
    float availH) {
    std::vector<GridTrackSolved> rows;
    std::vector<GridTrackSolved> cols;
    BuildGridTrackDefs(parent.RowDefs(), rows);
    BuildGridTrackDefs(parent.ColDefs(), cols);

    for (GridTrackSolved& r : rows) {
        if (r.unit == GridUnitType::Pixel) {
            r.size = r.value;
        }
    }
    for (GridTrackSolved& c : cols) {
        if (c.unit == GridUnitType::Pixel) {
            c.size = c.value;
        }
    }

    const std::size_t rCount = rows.size();
    const std::size_t cCount = cols.size();
    struct GridChildCell {
        VisualNodeId id{};
        std::size_t r0 = 0u;
        std::size_t c0 = 0u;
        std::size_t rSpan = 1u;
        std::size_t cSpan = 1u;
        bool hasStarRow = false;
        bool hasStarCol = false;
    };
    std::vector<GridChildCell> items;
    items.reserve(parent.children.size());

    for (VisualNodeId cid : parent.children) {
        const VisualNode* child = world.GetNode(cid);
        if (!child || child->layout.visibility == Visibility::Collapsed) {
            continue;
        }
        GridChildCell cell{};
        cell.id = cid;
        cell.r0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRow, 0, static_cast<std::int32_t>(rCount - 1u)));
        cell.c0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridCol, 0, static_cast<std::int32_t>(cCount - 1u)));
        cell.rSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRowSpan, 1, static_cast<std::int32_t>(rCount - cell.r0)));
        cell.cSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridColSpan, 1, static_cast<std::int32_t>(cCount - cell.c0)));
        cell.hasStarRow = SpanHasStar(rows, cell.r0, cell.rSpan);
        cell.hasStarCol = SpanHasStar(cols, cell.c0, cell.cSpan);
        items.push_back(cell);
    }

    // Group 1: Pixel/Auto cells (no Star in row or col), plus multi-span Auto expansion
    for (const GridChildCell& item : items) {
        if (item.hasStarRow || item.hasStarCol) {
            continue;
        }
        const float cw = SpanAvailExtent(cols, item.c0, item.cSpan);
        const float ch = SpanAvailExtent(rows, item.r0, item.rSpan);
        const Size d = MeasureNode(world, item.id, Size{cw, ch});
        if (item.cSpan == 1u) {
            if (cols[item.c0].unit == GridUnitType::Auto) {
                cols[item.c0].size = std::max(cols[item.c0].size, d.width);
            }
        }
        if (item.rSpan == 1u) {
            if (rows[item.r0].unit == GridUnitType::Auto) {
                rows[item.r0].size = std::max(rows[item.r0].size, d.height);
            }
        }
    }
    for (const GridChildCell& item : items) {
        if (item.hasStarRow || item.hasStarCol) {
            continue;
        }
        const VisualNode* child = world.GetNode(item.id);
        if (!child) continue;
        if (item.cSpan > 1u) {
            DistributeGridSpanDemand(cols, item.c0, item.cSpan, GridUnitType::Auto, child->layout.desiredSize.width);
        }
        if (item.rSpan > 1u) {
            DistributeGridSpanDemand(rows, item.r0, item.rSpan, GridUnitType::Auto, child->layout.desiredSize.height);
        }
    }

    // Group 2a: Auto/Star cross cells (one axis has Star, the other has Pixel/Auto).
    // Measure the non-Star axis with SpanAvailExtent (kInf for Auto) so Auto tracks expand before Star-Star cells.
    ResolveStarTracks(cols, availW);
    ResolveStarTracks(rows, availH);

    for (const GridChildCell& item : items) {
        if (item.hasStarRow == item.hasStarCol) {
            continue;
        }
        const float cw = !item.hasStarCol
            ? SpanAvailExtent(cols, item.c0, item.cSpan)
            : (std::isfinite(availW) ? SumTrackSpan(cols, item.c0, item.cSpan) : kInf);
        const float ch = !item.hasStarRow
            ? SpanAvailExtent(rows, item.r0, item.rSpan)
            : (std::isfinite(availH) ? SumTrackSpan(rows, item.r0, item.rSpan) : kInf);
        const Size d = MeasureNode(world, item.id, Size{cw, ch});
        if (!item.hasStarCol) {
            DistributeGridSpanDemand(cols, item.c0, item.cSpan, GridUnitType::Auto, d.width);
        } else if (!std::isfinite(availW)) {
            ResolveStarTracksFromInfinite(cols, item.c0, item.cSpan, d.width);
        }
        if (!item.hasStarRow) {
            DistributeGridSpanDemand(rows, item.r0, item.rSpan, GridUnitType::Auto, d.height);
        } else if (!std::isfinite(availH)) {
            ResolveStarTracksFromInfinite(rows, item.r0, item.rSpan, d.height);
        }
    }

    // Re-resolve Star after Auto growth, and measure all Star-participating cells with final track constraints
    if (std::isfinite(availW)) {
        ResolveStarTracks(cols, availW);
    }
    if (std::isfinite(availH)) {
        ResolveStarTracks(rows, availH);
    }
    for (const GridChildCell& item : items) {
        if (!item.hasStarRow && !item.hasStarCol) {
            continue;
        }
        const float cw = (!std::isfinite(availW) && item.hasStarCol) ? kInf : SumTrackSpan(cols, item.c0, item.cSpan);
        const float ch = (!std::isfinite(availH) && item.hasStarRow) ? kInf : SumTrackSpan(rows, item.r0, item.rSpan);
        const Size d = MeasureNode(world, item.id, Size{cw, ch});
        if (!std::isfinite(availW) && item.hasStarCol) {
            ResolveStarTracksFromInfinite(cols, item.c0, item.cSpan, d.width);
        }
        if (!std::isfinite(availH) && item.hasStarRow) {
            ResolveStarTracksFromInfinite(rows, item.r0, item.rSpan, d.height);
        }
    }

    std::vector<GridTrackSolved> desiredCols = cols;
    std::vector<GridTrackSolved> desiredRows = rows;
    for (GridTrackSolved& c : desiredCols) {
        if (c.unit == GridUnitType::Star) {
            c.size = 0.0f;
        }
    }
    for (GridTrackSolved& r : desiredRows) {
        if (r.unit == GridUnitType::Star) {
            r.size = 0.0f;
        }
    }
    for (const GridChildCell& item : items) {
        const VisualNode* child = world.GetNode(item.id);
        if (!child) {
            continue;
        }
        if (item.hasStarCol) {
            ResolveStarTracksFromInfinite(desiredCols, item.c0, item.cSpan, child->layout.desiredSize.width);
        }
        if (item.hasStarRow) {
            ResolveStarTracksFromInfinite(desiredRows, item.r0, item.rSpan, child->layout.desiredSize.height);
        }
    }
    if (std::isfinite(availW)) {
        for (std::size_t i = 0u; i < desiredCols.size(); ++i) {
            if (desiredCols[i].unit == GridUnitType::Star) {
                desiredCols[i].size = std::min(desiredCols[i].size, cols[i].size);
            }
        }
    }
    if (std::isfinite(availH)) {
        for (std::size_t i = 0u; i < desiredRows.size(); ++i) {
            if (desiredRows[i].unit == GridUnitType::Star) {
                desiredRows[i].size = std::min(desiredRows[i].size, rows[i].size);
            }
        }
    }

    float totalW = 0.0f;
    for (const GridTrackSolved& c : desiredCols) {
        totalW += c.size;
    }
    float totalH = 0.0f;
    for (const GridTrackSolved& r : desiredRows) {
        totalH += r.size;
    }
    return Size{totalW, totalH};
}

void ArrangeGridPanel(
    VisualWorld& world,
    const VisualNode& parent,
    const Rect& innerRect) {
    std::vector<GridTrackSolved> rows;
    std::vector<GridTrackSolved> cols;
    BuildGridTrackDefs(parent.RowDefs(), rows);
    BuildGridTrackDefs(parent.ColDefs(), cols);

    for (GridTrackSolved& r : rows) {
        if (r.unit == GridUnitType::Pixel) {
            r.size = r.value;
        }
    }
    for (GridTrackSolved& c : cols) {
        if (c.unit == GridUnitType::Pixel) {
            c.size = c.value;
        }
    }

    const std::size_t rCount = rows.size();
    const std::size_t cCount = cols.size();
    for (VisualNodeId cid : parent.children) {
        const VisualNode* child = world.GetNode(cid);
        if (!child || child->layout.visibility == Visibility::Collapsed) {
            continue;
        }
        const auto r0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRow, 0, static_cast<std::int32_t>(rCount - 1u)));
        const auto c0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridCol, 0, static_cast<std::int32_t>(cCount - 1u)));
        const auto rSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRowSpan, 1, static_cast<std::int32_t>(rCount - r0)));
        const auto cSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridColSpan, 1, static_cast<std::int32_t>(cCount - c0)));

        if (!SpanHasStar(cols, c0, cSpan)) {
            DistributeGridSpanDemand(cols, c0, cSpan, GridUnitType::Auto, child->layout.desiredSize.width);
        }
        if (!SpanHasStar(rows, r0, rSpan)) {
            DistributeGridSpanDemand(rows, r0, rSpan, GridUnitType::Auto, child->layout.desiredSize.height);
        }
    }

    ResolveStarTracks(cols, innerRect.width);
    ResolveStarTracks(rows, innerRect.height);
    FinalizeGridTrackOffsets(cols, innerRect.x);
    FinalizeGridTrackOffsets(rows, innerRect.y);

    const auto childrenCopy = parent.children;
    for (VisualNodeId cid : childrenCopy) {
        const VisualNode* child = world.GetNode(cid);
        if (!child) {
            continue;
        }
        if (child->layout.visibility == Visibility::Collapsed) {
            ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
            continue;
        }
        const auto r0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRow, 0, static_cast<std::int32_t>(rCount - 1u)));
        const auto c0 = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridCol, 0, static_cast<std::int32_t>(cCount - 1u)));
        const auto rSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridRowSpan, 1, static_cast<std::int32_t>(rCount - r0)));
        const auto cSpan = static_cast<std::size_t>(std::clamp<std::int32_t>(child->layout.gridColSpan, 1, static_cast<std::int32_t>(cCount - c0)));

        const Rect cellSlot{
            cols[c0].offset,
            rows[r0].offset,
            SumTrackSpan(cols, c0, cSpan),
            SumTrackSpan(rows, r0, rSpan)};
        ArrangeNode(world, cid, cellSlot, false, 1.0f, 1.0f, 0.0f, 0.0f);
    }
}

void ComputeUniformGridDims(
    const VisualNode& parent,
    std::size_t visibleCount,
    int& outRows,
    int& outCols,
    int& outFirstCol) noexcept {
    int rows = std::max(0, parent.layout.layoutInt0);
    int cols = std::max(0, parent.layout.layoutInt1);
    int firstCol = (std::isfinite(parent.layout.layoutParam0) && parent.layout.layoutParam0 > 0.0f)
        ? static_cast<int>(parent.layout.layoutParam0)
        : 0;
    if (cols > 0) {
        firstCol = std::clamp(firstCol, 0, cols - 1);
    }
    const int n = static_cast<int>(visibleCount);
    if (rows == 0 && cols == 0) {
        const int total = std::max(1, n + firstCol);
        cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(total)))));
        rows = std::max(1, (total + cols - 1) / cols);
    } else if (rows == 0) {
        cols = std::max(1, cols);
        const int total = std::max(1, n + firstCol);
        rows = std::max(1, (total + cols - 1) / cols);
    } else if (cols == 0) {
        rows = std::max(1, rows);
        const int total = std::max(1, n + firstCol);
        cols = std::max(1, (total + rows - 1) / rows);
        if (firstCol >= cols) {
            firstCol = cols - 1;
        }
    }
    outRows = std::max(1, rows);
    outCols = std::max(1, cols);
    outFirstCol = firstCol;
}

[[nodiscard]] Size MeasurePanelChildren(
    VisualWorld& world,
    VisualNode& parent,
    float availW,
    float availH) {
    const auto childrenCopy = parent.children;

    switch (parent.layout.layoutMode) {
        case LayoutMode::Stack: {
            const bool isHoriz = (static_cast<Orientation>(parent.layout.layoutInt0) == Orientation::Horizontal);
            const float spacing = std::isfinite(parent.layout.layoutParam0)
                ? std::max(0.0f, parent.layout.layoutParam0)
                : 0.0f;
            float primarySum = 0.0f;
            float maxCross = 0.0f;
            std::size_t visCount = 0u;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size cAvail = isHoriz ? Size{kInf, availH} : Size{availW, kInf};
                const Size d = MeasureNode(world, cid, cAvail);
                if (visCount > 0u) {
                    primarySum += spacing;
                }
                primarySum += isHoriz ? d.width : d.height;
                maxCross = std::max(maxCross, isHoriz ? d.height : d.width);
                ++visCount;
            }
            return isHoriz ? Size{primarySum, maxCross} : Size{maxCross, primarySum};
        }

        case LayoutMode::Dock: {
            const bool lastChildFill = (parent.layout.layoutInt0 != 0);
            VisualNodeId lastVisible{};
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (child && child->layout.visibility != Visibility::Collapsed) {
                    lastVisible = cid;
                }
            }
            float usedW = 0.0f;
            float usedH = 0.0f;
            float maxTotalW = 0.0f;
            float maxTotalH = 0.0f;
            float remW = availW;
            float remH = availH;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const bool isFill = lastChildFill && (cid == lastVisible);
                Size childAvail{remW, remH};
                if (!isFill) {
                    if (child->layout.dock == Dock::Top || child->layout.dock == Dock::Bottom) {
                        childAvail.height = kInf;
                    } else {
                        childAvail.width = kInf;
                    }
                }
                const Size d = MeasureNode(world, cid, childAvail);
                if (isFill) {
                    maxTotalW = std::max(maxTotalW, usedW + d.width);
                    maxTotalH = std::max(maxTotalH, usedH + d.height);
                    continue;
                }
                switch (child->layout.dock) {
                    case Dock::Left:
                    case Dock::Right:
                        maxTotalH = std::max(maxTotalH, usedH + d.height);
                        usedW += d.width;
                        if (std::isfinite(remW)) {
                            remW = std::max(0.0f, remW - d.width);
                        }
                        break;
                    case Dock::Top:
                    case Dock::Bottom:
                        maxTotalW = std::max(maxTotalW, usedW + d.width);
                        usedH += d.height;
                        if (std::isfinite(remH)) {
                            remH = std::max(0.0f, remH - d.height);
                        }
                        break;
                }
            }
            return Size{std::max(maxTotalW, usedW), std::max(maxTotalH, usedH)};
        }

        case LayoutMode::Wrap: {
            const bool isHoriz = (static_cast<Orientation>(parent.layout.layoutInt0) == Orientation::Horizontal);
            const bool hasItemW = std::isfinite(parent.layout.layoutParam0) && parent.layout.layoutParam0 > 0.0f;
            const bool hasItemH = std::isfinite(parent.layout.layoutParam1) && parent.layout.layoutParam1 > 0.0f;
            const float itemW = hasItemW ? parent.layout.layoutParam0 : availW;
            const float itemH = hasItemH ? parent.layout.layoutParam1 : availH;
            const float spacing = (parent.layout.layoutInt1 > 0)
                ? (static_cast<float>(parent.layout.layoutInt1) * 0.001f)
                : 0.0f;
            const float maxPrimary = isHoriz ? availW : availH;

            float curPrimary = 0.0f;
            float curSecondary = 0.0f;
            float totalSecondary = 0.0f;
            float maxLinePrimary = 0.0f;
            bool firstLine = true;
            std::size_t itemsInLine = 0u;

            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size measured = MeasureNode(world, cid, Size{itemW, itemH});
                const float cw = hasItemW ? itemW : measured.width;
                const float ch = hasItemH ? itemH : measured.height;
                const float cPrimary = isHoriz ? cw : ch;
                const float cSecondary = isHoriz ? ch : cw;

                const float addPrimary = (itemsInLine > 0u) ? (spacing + cPrimary) : cPrimary;
                if (itemsInLine > 0u && std::isfinite(maxPrimary) &&
                    curPrimary + addPrimary > maxPrimary + kLayoutEps) {
                    maxLinePrimary = std::max(maxLinePrimary, curPrimary);
                    if (!firstLine) {
                        totalSecondary += spacing;
                    }
                    totalSecondary += curSecondary;
                    firstLine = false;
                    curPrimary = cPrimary;
                    curSecondary = cSecondary;
                    itemsInLine = 1u;
                } else {
                    curPrimary += addPrimary;
                    curSecondary = std::max(curSecondary, cSecondary);
                    ++itemsInLine;
                }
            }
            if (itemsInLine > 0u) {
                maxLinePrimary = std::max(maxLinePrimary, curPrimary);
                if (!firstLine) {
                    totalSecondary += spacing;
                }
                totalSecondary += curSecondary;
            }
            return isHoriz ? Size{maxLinePrimary, totalSecondary} : Size{totalSecondary, maxLinePrimary};
        }

        case LayoutMode::Grid:
            return MeasureGridPanel(world, parent, availW, availH);

        case LayoutMode::UniformGrid: {
            std::size_t visCount = 0u;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (child && child->layout.visibility != Visibility::Collapsed) {
                    ++visCount;
                }
            }
            int rows = 1, cols = 1, firstCol = 0;
            ComputeUniformGridDims(parent, visCount, rows, cols, firstCol);
            const Size cellAvail{
                std::isfinite(availW) ? (availW / static_cast<float>(cols)) : kInf,
                std::isfinite(availH) ? (availH / static_cast<float>(rows)) : kInf};
            float maxCellW = 0.0f;
            float maxCellH = 0.0f;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size d = MeasureNode(world, cid, cellAvail);
                maxCellW = std::max(maxCellW, d.width);
                maxCellH = std::max(maxCellH, d.height);
            }
            return Size{maxCellW * static_cast<float>(cols), maxCellH * static_cast<float>(rows)};
        }

        case LayoutMode::Canvas: {
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                (void)MeasureNode(world, cid, Size{kInf, kInf});
            }
            return Size{0.0f, 0.0f};
        }

        case LayoutMode::ScrollViewport: {
            const Size childAvail{
                (parent.layout.layoutInt0 == 1) ? availW : kInf,
                (parent.layout.layoutInt0 == 2) ? availH : kInf};
            float extW = 0.0f;
            float extH = 0.0f;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size d = MeasureNode(world, cid, childAvail);
                extW = std::max(extW, d.width);
                extH = std::max(extH, d.height);
            }
            parent.layout.scrollExtent = Size{extW, extH};
            return Size{
                std::isfinite(availW) ? std::min(availW, extW) : extW,
                std::isfinite(availH) ? std::min(availH, extH) : extH};
        }

        case LayoutMode::Viewbox: {
            const float refW = parent.layout.layoutParam0;
            const float refH = parent.layout.layoutParam1;
            const auto stretch = static_cast<Stretch>(std::clamp(parent.layout.layoutInt0, 0, 3));
            const bool expandViewport = (parent.layout.layoutInt1 & 0x100) != 0;
            const auto stretchDir = static_cast<StretchDirection>(std::clamp(parent.layout.layoutInt1 & 0xFF, 0, 2));
            const bool hasRef = std::isfinite(refW) && refW > 0.0f && std::isfinite(refH) && refH > 0.0f;

            if (expandViewport && hasRef) {
                float sx = 1.0f;
                float sy = 1.0f;
                ComputeViewboxScale(availW, availH, refW, refH, stretch, stretchDir, sx, sy);
                const float childAvailW = (std::isfinite(availW) && sx > 1e-5f) ? (availW / sx) : refW;
                const float childAvailH = (std::isfinite(availH) && sy > 1e-5f) ? (availH / sy) : refH;
                float contentW = 0.0f;
                float contentH = 0.0f;
                for (VisualNodeId cid : childrenCopy) {
                    const VisualNode* child = world.GetNode(cid);
                    if (!child || child->layout.visibility == Visibility::Collapsed) {
                        continue;
                    }
                    const Size d = MeasureNode(world, cid, Size{childAvailW, childAvailH});
                    contentW = std::max(contentW, d.width);
                    contentH = std::max(contentH, d.height);
                }
                return Size{
                    std::isfinite(availW) ? availW : (contentW * sx),
                    std::isfinite(availH) ? availH : (contentH * sy)};
            }

            float contentW = hasRef ? refW : 0.0f;
            float contentH = hasRef ? refH : 0.0f;
            const Size childAvail = hasRef ? Size{refW, refH} : Size{kInf, kInf};
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size d = MeasureNode(world, cid, childAvail);
                if (!hasRef) {
                    contentW = std::max(contentW, d.width);
                    contentH = std::max(contentH, d.height);
                }
            }
            float sx = 1.0f;
            float sy = 1.0f;
            ComputeViewboxScale(availW, availH, contentW, contentH, stretch, stretchDir, sx, sy);
            return Size{contentW * sx, contentH * sy};
        }

        case LayoutMode::CustomCallback: {
            // Note: CustomMeasureCallback is checked at MeasureNode level; fallback here handles child measurement.
            float maxW = 0.0f;
            float maxH = 0.0f;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size d = MeasureNode(world, cid, Size{availW, availH});
                maxW = std::max(maxW, d.width);
                maxH = std::max(maxH, d.height);
            }
            return Size{maxW, maxH};
        }

        case LayoutMode::Manual:
        default: {
            float maxW = 0.0f;
            float maxH = 0.0f;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child || child->layout.visibility == Visibility::Collapsed) {
                    continue;
                }
                const Size d = MeasureNode(world, cid, Size{availW, availH});
                maxW = std::max(maxW, d.width);
                maxH = std::max(maxH, d.height);
            }
            return Size{maxW, maxH};
        }
    }
}

void ArrangePanelChildren(
    VisualWorld& world,
    VisualNode& parent,
    const Rect& innerRect) {
    const auto childrenCopy = parent.children;

    switch (parent.layout.layoutMode) {
        case LayoutMode::Stack: {
            const bool isHoriz = (static_cast<Orientation>(parent.layout.layoutInt0) == Orientation::Horizontal);
            const float spacing = std::isfinite(parent.layout.layoutParam0)
                ? std::max(0.0f, parent.layout.layoutParam0)
                : 0.0f;
            float cursor = isHoriz ? innerRect.x : innerRect.y;
            bool first = true;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                if (!first) {
                    cursor += spacing;
                }
                first = false;
                const Size d = child->layout.desiredSize;
                if (isHoriz) {
                    ArrangeNode(world, cid, Rect{cursor, innerRect.y, d.width, innerRect.height}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    cursor += d.width;
                } else {
                    ArrangeNode(world, cid, Rect{innerRect.x, cursor, innerRect.width, d.height}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    cursor += d.height;
                }
            }
            break;
        }

        case LayoutMode::Dock: {
            const bool lastChildFill = (parent.layout.layoutInt0 != 0);
            VisualNodeId lastVisible{};
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (child && child->layout.visibility != Visibility::Collapsed) {
                    lastVisible = cid;
                }
            }
            Rect rem = innerRect;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{rem.x, rem.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                if (lastChildFill && cid == lastVisible) {
                    ArrangeNode(world, cid, rem, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                const Size d = child->layout.desiredSize;
                Rect slot = rem;
                switch (child->layout.dock) {
                    case Dock::Left: {
                        const float w = std::min(rem.width, d.width);
                        slot = Rect{rem.x, rem.y, w, rem.height};
                        rem.x += w;
                        rem.width = std::max(0.0f, rem.width - w);
                        break;
                    }
                    case Dock::Right: {
                        const float w = std::min(rem.width, d.width);
                        slot = Rect{rem.x + rem.width - w, rem.y, w, rem.height};
                        rem.width = std::max(0.0f, rem.width - w);
                        break;
                    }
                    case Dock::Top: {
                        const float h = std::min(rem.height, d.height);
                        slot = Rect{rem.x, rem.y, rem.width, h};
                        rem.y += h;
                        rem.height = std::max(0.0f, rem.height - h);
                        break;
                    }
                    case Dock::Bottom: {
                        const float h = std::min(rem.height, d.height);
                        slot = Rect{rem.x, rem.y + rem.height - h, rem.width, h};
                        rem.height = std::max(0.0f, rem.height - h);
                        break;
                    }
                }
                ArrangeNode(world, cid, slot, false, 1.0f, 1.0f, 0.0f, 0.0f);
            }
            break;
        }

        case LayoutMode::Wrap: {
            const bool isHoriz = (static_cast<Orientation>(parent.layout.layoutInt0) == Orientation::Horizontal);
            const bool hasItemW = std::isfinite(parent.layout.layoutParam0) && parent.layout.layoutParam0 > 0.0f;
            const bool hasItemH = std::isfinite(parent.layout.layoutParam1) && parent.layout.layoutParam1 > 0.0f;
            const float spacing = (parent.layout.layoutInt1 > 0)
                ? (static_cast<float>(parent.layout.layoutInt1) * 0.001f)
                : 0.0f;
            const float maxPrimary = isHoriz ? innerRect.width : innerRect.height;

            struct WrapItem {
                VisualNodeId id{};
                float w = 0.0f;
                float h = 0.0f;
            };
            std::vector<WrapItem> line;
            line.reserve(childrenCopy.size());

            float linePrimary = 0.0f;
            float lineSecondary = 0.0f;
            float secCursor = isHoriz ? innerRect.y : innerRect.x;

            auto flushLine = [&]() {
                if (line.empty()) {
                    return;
                }
                float primCursor = isHoriz ? innerRect.x : innerRect.y;
                for (std::size_t i = 0u; i < line.size(); ++i) {
                    if (i > 0u) {
                        primCursor += spacing;
                    }
                    const WrapItem& it = line[i];
                    const Rect slot = isHoriz
                        ? Rect{primCursor, secCursor, it.w, lineSecondary}
                        : Rect{secCursor, primCursor, lineSecondary, it.h};
                    ArrangeNode(world, it.id, slot, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    primCursor += isHoriz ? it.w : it.h;
                }
                secCursor += lineSecondary + spacing;
                line.clear();
                linePrimary = 0.0f;
                lineSecondary = 0.0f;
            };

            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                const float cw = hasItemW ? parent.layout.layoutParam0 : child->layout.desiredSize.width;
                const float ch = hasItemH ? parent.layout.layoutParam1 : child->layout.desiredSize.height;
                const float cPrimary = isHoriz ? cw : ch;
                const float cSecondary = isHoriz ? ch : cw;
                const float addPrimary = line.empty() ? cPrimary : (spacing + cPrimary);

                if (!line.empty() && linePrimary + addPrimary > maxPrimary + kLayoutEps) {
                    flushLine();
                }
                linePrimary += line.empty() ? cPrimary : (spacing + cPrimary);
                lineSecondary = std::max(lineSecondary, cSecondary);
                line.push_back(WrapItem{cid, cw, ch});
            }
            flushLine();
            break;
        }

        case LayoutMode::Grid:
            ArrangeGridPanel(world, parent, innerRect);
            break;

        case LayoutMode::UniformGrid: {
            std::size_t visCount = 0u;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (child && child->layout.visibility != Visibility::Collapsed) {
                    ++visCount;
                }
            }
            int rows = 1, cols = 1, firstCol = 0;
            ComputeUniformGridDims(parent, visCount, rows, cols, firstCol);
            const float cellW = innerRect.width / static_cast<float>(cols);
            const float cellH = innerRect.height / static_cast<float>(rows);
            int slotIndex = firstCol;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                const int r = slotIndex / cols;
                const int c = slotIndex % cols;
                const Rect cellSlot{
                    innerRect.x + static_cast<float>(c) * cellW,
                    innerRect.y + static_cast<float>(r) * cellH,
                    cellW,
                    cellH};
                ArrangeNode(world, cid, cellSlot, false, 1.0f, 1.0f, 0.0f, 0.0f);
                ++slotIndex;
            }
            break;
        }

        case LayoutMode::Canvas: {
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                const Size d = child->layout.desiredSize;
                float x = innerRect.x;
                if (!std::isnan(child->layout.canvasLeft)) {
                    x = innerRect.x + child->layout.canvasLeft;
                } else if (!std::isnan(child->layout.canvasRight)) {
                    x = innerRect.x + innerRect.width - child->layout.canvasRight - d.width;
                }
                float y = innerRect.y;
                if (!std::isnan(child->layout.canvasTop)) {
                    y = innerRect.y + child->layout.canvasTop;
                } else if (!std::isnan(child->layout.canvasBottom)) {
                    y = innerRect.y + innerRect.height - child->layout.canvasBottom - d.height;
                }
                ArrangeNode(world, cid, Rect{x, y, d.width, d.height}, false, 1.0f, 1.0f, 0.0f, 0.0f);
            }
            break;
        }

        case LayoutMode::ScrollViewport: {
            float extW = 0.0f;
            float extH = 0.0f;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (child && child->layout.visibility != Visibility::Collapsed) {
                    extW = std::max(extW, child->layout.desiredSize.width);
                    extH = std::max(extH, child->layout.desiredSize.height);
                }
            }
            parent.layout.scrollExtent = Size{extW, extH};
            const float maxOx = std::max(0.0f, extW - innerRect.width);
            const float maxOy = std::max(0.0f, extH - innerRect.height);
            const float ox = std::clamp(
                std::isfinite(parent.layout.scrollOffsetX) ? parent.layout.scrollOffsetX : 0.0f,
                0.0f,
                maxOx);
            const float oy = std::clamp(
                std::isfinite(parent.layout.scrollOffsetY) ? parent.layout.scrollOffsetY : 0.0f,
                0.0f,
                maxOy);
            parent.layout.scrollOffsetX = ox;
            parent.layout.scrollOffsetY = oy;
            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, false, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                const float cw = std::max(innerRect.width, child->layout.desiredSize.width);
                const float ch = std::max(innerRect.height, child->layout.desiredSize.height);
                ArrangeNode(
                    world,
                    cid,
                    Rect{innerRect.x - ox, innerRect.y - oy, cw, ch},
                    false,
                    1.0f,
                    1.0f,
                    0.0f,
                    0.0f);
            }
            break;
        }

        case LayoutMode::Viewbox: {
            const float refW = parent.layout.layoutParam0;
            const float refH = parent.layout.layoutParam1;
            const auto stretch = static_cast<Stretch>(std::clamp(parent.layout.layoutInt0, 0, 3));
            const bool expandViewport = (parent.layout.layoutInt1 & 0x100) != 0;
            const auto stretchDir = static_cast<StretchDirection>(std::clamp(parent.layout.layoutInt1 & 0xFF, 0, 2));
            const bool hasRef = std::isfinite(refW) && refW > 0.0f && std::isfinite(refH) && refH > 0.0f;

            if (expandViewport && hasRef) {
                float sx = 1.0f;
                float sy = 1.0f;
                ComputeViewboxScale(innerRect.width, innerRect.height, refW, refH, stretch, stretchDir, sx, sy);
                const float arrangeW = (sx > 1e-5f) ? (innerRect.width / sx) : refW;
                const float arrangeH = (sy > 1e-5f) ? (innerRect.height / sy) : refH;
                for (VisualNodeId cid : childrenCopy) {
                    const VisualNode* child = world.GetNode(cid);
                    if (!child) continue;
                    if (child->layout.visibility == Visibility::Collapsed) {
                        ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, true, 1.0f, 1.0f, 0.0f, 0.0f);
                        continue;
                    }
                    ArrangeNode(
                        world,
                        cid,
                        Rect{innerRect.x, innerRect.y, arrangeW, arrangeH},
                        true,
                        sx,
                        sy,
                        innerRect.x,
                        innerRect.y);
                }
                break;
            }

            float contentW = hasRef ? refW : 0.0f;
            float contentH = hasRef ? refH : 0.0f;
            if (!hasRef) {
                for (VisualNodeId cid : childrenCopy) {
                    const VisualNode* child = world.GetNode(cid);
                    if (child && child->layout.visibility != Visibility::Collapsed) {
                        contentW = std::max(contentW, child->layout.desiredSize.width);
                        contentH = std::max(contentH, child->layout.desiredSize.height);
                    }
                }
            }
            float sx = 1.0f;
            float sy = 1.0f;
            ComputeViewboxScale(innerRect.width, innerRect.height, contentW, contentH, stretch, stretchDir, sx, sy);
            const float destX = innerRect.x + (innerRect.width - contentW * sx) * 0.5f;
            const float destY = innerRect.y + (innerRect.height - contentH * sy) * 0.5f;

            for (VisualNodeId cid : childrenCopy) {
                const VisualNode* child = world.GetNode(cid);
                if (!child) continue;
                if (child->layout.visibility == Visibility::Collapsed) {
                    ArrangeNode(world, cid, Rect{innerRect.x, innerRect.y, 0.0f, 0.0f}, true, 1.0f, 1.0f, 0.0f, 0.0f);
                    continue;
                }
                ArrangeNode(
                    world,
                    cid,
                    Rect{destX, destY, contentW, contentH},
                    true,
                    sx,
                    sy,
                    destX,
                    destY);
            }
            break;
        }

        case LayoutMode::CustomCallback:
        case LayoutMode::Manual:
        default: {
            for (VisualNodeId cid : childrenCopy) {
                ArrangeNode(world, cid, innerRect, false, 1.0f, 1.0f, 0.0f, 0.0f);
            }
            break;
        }
    }
}

Size MeasureNode(VisualWorld& world, VisualNodeId id, Size availSize) {
    VisualNode* node = world.GetNode(id);
    if (!node) {
        return Size{0.0f, 0.0f};
    }

    if (node->layout.visibility == Visibility::Collapsed) {
        node->layout.desiredSize = Size{0.0f, 0.0f};
        node->layout.lastMeasureConstraint = availSize;
        node->layout.hasValidMeasure = true;
        return Size{0.0f, 0.0f};
    }

    if (node->layout.hasValidMeasure &&
        SameSizeConstraint(node->layout.lastMeasureConstraint, availSize)) {
        return node->layout.desiredSize;
    }

    if (const auto& customMeasure = world.CustomMeasureHook();
        customMeasure &&
        (node->layout.layoutMode == LayoutMode::CustomCallback ||
         (node->callbackMask & VisualCallback_CustomMeasure) != 0u)) {
        const Size customDesired = customMeasure(world, id, availSize);
        node->layout.desiredSize = customDesired;
        node->layout.lastMeasureConstraint = availSize;
        node->layout.hasValidMeasure = true;
        node->layout.hasValidArrange = false;
        return customDesired;
    }

    const float marginH =
        (std::isfinite(node->layout.margin.left) ? node->layout.margin.left : 0.0f) +
        (std::isfinite(node->layout.margin.right) ? node->layout.margin.right : 0.0f);
    const float marginV =
        (std::isfinite(node->layout.margin.top) ? node->layout.margin.top : 0.0f) +
        (std::isfinite(node->layout.margin.bottom) ? node->layout.margin.bottom : 0.0f);
    const float padH = std::max(0.0f, node->layout.padding.left) + std::max(0.0f, node->layout.padding.right);
    const float padV = std::max(0.0f, node->layout.padding.top) + std::max(0.0f, node->layout.padding.bottom);

    const bool hasLayoutTx = !node->layout.layoutTransform2D.IsIdentity();
    float localAvailOuterW = availSize.width;
    float localAvailOuterH = availSize.height;
    Matrix2D layoutTxLin = Matrix2D::Identity();
    if (hasLayoutTx) {
        layoutTxLin = ComputeLayoutTransformLinearMatrix(node->layout.layoutTransform2D);
        const float outerBoxAvailW = std::isfinite(availSize.width)
            ? std::max(0.0f, availSize.width - marginH)
            : kInf;
        const float outerBoxAvailH = std::isfinite(availSize.height)
            ? std::max(0.0f, availSize.height - marginV)
            : kInf;
        float invLocalW = kInf;
        float invLocalH = kInf;
        InverseBoxConstraintUnderLayoutTransform(
            layoutTxLin,
            outerBoxAvailW,
            outerBoxAvailH,
            outerBoxAvailW,
            outerBoxAvailH,
            invLocalW,
            invLocalH);
        localAvailOuterW = std::isfinite(invLocalW) ? (invLocalW + marginH) : kInf;
        localAvailOuterH = std::isfinite(invLocalH) ? (invLocalH + marginV) : kInf;
    }

    float minW = 0.0f, maxW = kInf, innerAvailW = kInf;
    float minH = 0.0f, maxH = kInf, innerAvailH = kInf;
    ComputeAxisConstraint(
        node->layout.width,
        node->layout.minWidth,
        node->layout.maxWidth,
        padH,
        marginH,
        localAvailOuterW,
        true,
        minW,
        maxW,
        innerAvailW);
    ComputeAxisConstraint(
        node->layout.height,
        node->layout.minHeight,
        node->layout.maxHeight,
        padV,
        marginV,
        localAvailOuterH,
        true,
        minH,
        maxH,
        innerAvailH);

    Size contentDesired = MeasureIntrinsicContent(world, *node, innerAvailW, innerAvailH);
    if (!node->children.empty()) {
        const Size panelDesired = MeasurePanelChildren(world, *node, innerAvailW, innerAvailH);
        contentDesired.width = std::max(contentDesired.width, panelDesired.width);
        contentDesired.height = std::max(contentDesired.height, panelDesired.height);
    }

    const float desiredBoxW = std::clamp(contentDesired.width + padH, minW, maxW);
    const float desiredBoxH = std::clamp(contentDesired.height + padV, minH, maxH);
    node->layout.untransformedDesiredBox = Size{
        std::isfinite(desiredBoxW) ? desiredBoxW : 0.0f,
        std::isfinite(desiredBoxH) ? desiredBoxH : 0.0f};

    float finalBoxW = desiredBoxW;
    float finalBoxH = desiredBoxH;
    if (hasLayoutTx && std::isfinite(desiredBoxW) && std::isfinite(desiredBoxH)) {
        finalBoxW = std::fabs(layoutTxLin.m00) * desiredBoxW + std::fabs(layoutTxLin.m01) * desiredBoxH;
        finalBoxH = std::fabs(layoutTxLin.m10) * desiredBoxW + std::fabs(layoutTxLin.m11) * desiredBoxH;
    }

    node->layout.desiredSize = Size{
        std::max(0.0f, std::isfinite(finalBoxW) ? (finalBoxW + marginH) : marginH),
        std::max(0.0f, std::isfinite(finalBoxH) ? (finalBoxH + marginV) : marginV)};
    node->layout.lastMeasureConstraint = availSize;
    node->layout.hasValidMeasure = true;
    node->layout.hasValidArrange = false;
    return node->layout.desiredSize;
}

void ArrangeNode(
    VisualWorld& world,
    VisualNodeId id,
    Rect finalSlot,
    bool parentWasViewbox,
    float viewboxSx,
    float viewboxSy,
    float viewboxOx,
    float viewboxOy) {
    VisualNode* node = world.GetNode(id);
    if (!node) {
        return;
    }

    if (node->layout.visibility == Visibility::Collapsed) {
        const Rect collapsedRect{finalSlot.x, finalSlot.y, 0.0f, 0.0f};
        const bool rectChanged = !SameRectExact(node->layout.layoutRect, collapsedRect);
        node->layout.layoutRect = collapsedRect;
        node->layout.lastArrangeSlot = finalSlot;
        node->layout.hasValidArrange = true;
        if (rectChanged) {
            node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags | VisualDirty_Transform | VisualDirty_Visual);
            world.MarkStructureDirty();
        }
        return;
    }

    const float nextVbSx = parentWasViewbox ? viewboxSx : 1.0f;
    const float nextVbSy = parentWasViewbox ? viewboxSy : 1.0f;
    const float nextVbOx = parentWasViewbox ? viewboxOx : 0.0f;
    const float nextVbOy = parentWasViewbox ? viewboxOy : 0.0f;
    const bool vbChanged =
        !SameFloatOrBothNaN(node->layout.viewboxScaleX, nextVbSx) ||
        !SameFloatOrBothNaN(node->layout.viewboxScaleY, nextVbSy) ||
        !SameFloatOrBothNaN(node->layout.viewboxOriginX, nextVbOx) ||
        !SameFloatOrBothNaN(node->layout.viewboxOriginY, nextVbOy);
    node->layout.viewboxScaleX = nextVbSx;
    node->layout.viewboxScaleY = nextVbSy;
    node->layout.viewboxOriginX = nextVbOx;
    node->layout.viewboxOriginY = nextVbOy;

    if (!vbChanged &&
        node->layout.hasValidArrange &&
        SameRectExact(node->layout.lastArrangeSlot, finalSlot)) {
        return;
    }

    if (const auto& customArrange = world.CustomArrangeHook();
        customArrange &&
        (node->layout.layoutMode == LayoutMode::CustomCallback ||
         (node->callbackMask & VisualCallback_CustomArrange) != 0u)) {
        const bool rectChanged = !SameRectExact(node->layout.layoutRect, finalSlot) || vbChanged;
        node->layout.layoutRect = finalSlot;
        node->layout.lastArrangeSlot = finalSlot;
        node->layout.hasValidArrange = true;
        if (rectChanged) {
            node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags | VisualDirty_Transform | VisualDirty_Visual);
            world.MarkStructureDirty();
        }
        customArrange(world, id, finalSlot);
        return;
    }

    const float mL = std::isfinite(node->layout.margin.left) ? node->layout.margin.left : 0.0f;
    const float mT = std::isfinite(node->layout.margin.top) ? node->layout.margin.top : 0.0f;
    const float mR = std::isfinite(node->layout.margin.right) ? node->layout.margin.right : 0.0f;
    const float mB = std::isfinite(node->layout.margin.bottom) ? node->layout.margin.bottom : 0.0f;
    const float marginH = mL + mR;
    const float marginV = mT + mB;

    const float availBoxW = std::max(0.0f, finalSlot.width - marginH);
    const float availBoxH = std::max(0.0f, finalSlot.height - marginV);

    const float minW = std::max(0.0f, std::isfinite(node->layout.minWidth) ? node->layout.minWidth : 0.0f);
    const float maxW = std::max(minW, (std::isnan(node->layout.maxWidth) || node->layout.maxWidth < 0.0f) ? kInf : node->layout.maxWidth);
    const float minH = std::max(0.0f, std::isfinite(node->layout.minHeight) ? node->layout.minHeight : 0.0f);
    const float maxH = std::max(minH, (std::isnan(node->layout.maxHeight) || node->layout.maxHeight < 0.0f) ? kInf : node->layout.maxHeight);

    const bool hasLayoutTx = !node->layout.layoutTransform2D.IsIdentity();
    const float desiredContentW = hasLayoutTx
        ? std::max(0.0f, node->layout.untransformedDesiredBox.width)
        : std::max(0.0f, node->layout.desiredSize.width - marginH);
    const float desiredContentH = hasLayoutTx
        ? std::max(0.0f, node->layout.untransformedDesiredBox.height)
        : std::max(0.0f, node->layout.desiredSize.height - marginV);

    Matrix2D layoutTxLin = Matrix2D::Identity();
    float stretchBoxW = availBoxW;
    float stretchBoxH = availBoxH;
    if (hasLayoutTx) {
        layoutTxLin = ComputeLayoutTransformLinearMatrix(node->layout.layoutTransform2D);
        InverseBoxConstraintUnderLayoutTransform(
            layoutTxLin,
            availBoxW,
            availBoxH,
            desiredContentW,
            desiredContentH,
            stretchBoxW,
            stretchBoxH);
    }

    float targetW = 0.0f;
    if (std::isfinite(node->layout.width) && node->layout.width >= 0.0f) {
        targetW = std::clamp(node->layout.width, minW, maxW);
    } else if (node->layout.hAlign == HorizontalAlignment::Stretch) {
        targetW = std::clamp(stretchBoxW, minW, maxW);
    } else {
        targetW = std::clamp(desiredContentW, minW, maxW);
    }

    float targetH = 0.0f;
    if (std::isfinite(node->layout.height) && node->layout.height >= 0.0f) {
        targetH = std::clamp(node->layout.height, minH, maxH);
    } else if (node->layout.vAlign == VerticalAlignment::Stretch) {
        targetH = std::clamp(stretchBoxH, minH, maxH);
    } else {
        targetH = std::clamp(desiredContentH, minH, maxH);
    }

    const float arrangedBoxW = hasLayoutTx
        ? (std::fabs(layoutTxLin.m00) * targetW + std::fabs(layoutTxLin.m01) * targetH)
        : targetW;
    const float arrangedBoxH = hasLayoutTx
        ? (std::fabs(layoutTxLin.m10) * targetW + std::fabs(layoutTxLin.m11) * targetH)
        : targetH;

    float posX = finalSlot.x + mL;
    switch (node->layout.hAlign) {
        case HorizontalAlignment::Left:
            posX = finalSlot.x + mL;
            break;
        case HorizontalAlignment::Center:
        case HorizontalAlignment::Stretch:
            posX = finalSlot.x + mL + (availBoxW - arrangedBoxW) * 0.5f;
            break;
        case HorizontalAlignment::Right:
            posX = finalSlot.x + mL + (availBoxW - arrangedBoxW);
            break;
    }

    float posY = finalSlot.y + mT;
    switch (node->layout.vAlign) {
        case VerticalAlignment::Top:
            posY = finalSlot.y + mT;
            break;
        case VerticalAlignment::Center:
        case VerticalAlignment::Stretch:
            posY = finalSlot.y + mT + (availBoxH - arrangedBoxH) * 0.5f;
            break;
        case VerticalAlignment::Bottom:
            posY = finalSlot.y + mT + (availBoxH - arrangedBoxH);
            break;
    }

    const Rect newLayoutRect{posX, posY, targetW, targetH};
    const bool rectChanged = !SameRectExact(node->layout.layoutRect, newLayoutRect) || vbChanged;
    node->layout.layoutRect = newLayoutRect;
    node->layout.lastArrangeSlot = finalSlot;
    node->layout.hasValidArrange = true;
    if (rectChanged) {
        node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags | VisualDirty_Transform | VisualDirty_Visual);
        world.MarkStructureDirty();
    }

    const float pL = std::max(0.0f, node->layout.padding.left);
    const float pT = std::max(0.0f, node->layout.padding.top);
    const float pR = std::max(0.0f, node->layout.padding.right);
    const float pB = std::max(0.0f, node->layout.padding.bottom);
    const Rect innerRect{
        posX + pL,
        posY + pT,
        std::max(0.0f, targetW - (pL + pR)),
        std::max(0.0f, targetH - (pT + pB))};

    if (!node->children.empty()) {
        ArrangePanelChildren(world, *node, innerRect);
    }
}

[[nodiscard]] Matrix2D ComputeNodeLocalMatrix2D(const VisualNode& node) noexcept {
    const Rect& rect = node.layout.layoutRect;
    const VisualTransform2D& t = node.transform2D;
    const bool hasViewboxScale =
        std::fabs(node.layout.viewboxScaleX - 1.0f) > 1e-6f ||
        std::fabs(node.layout.viewboxScaleY - 1.0f) > 1e-6f;
    Matrix2D viewboxM = Matrix2D::Identity();
    if (hasViewboxScale) {
        viewboxM = Matrix2D::Scaling(node.layout.viewboxScaleX, node.layout.viewboxScaleY)
                       .AroundPivot(node.layout.viewboxOriginX, node.layout.viewboxOriginY);
    }

    const bool hasLayoutTx = !node.layout.layoutTransform2D.IsIdentity();
    Matrix2D layoutTxM = Matrix2D::Identity();
    float boxW = rect.width;
    float boxH = rect.height;
    if (hasLayoutTx) {
        const Matrix2D lLin = ComputeLayoutTransformLinearMatrix(node.layout.layoutTransform2D);
        const float minRelX = std::min(0.0f, lLin.m00 * rect.width) + std::min(0.0f, lLin.m01 * rect.height);
        const float minRelY = std::min(0.0f, lLin.m10 * rect.width) + std::min(0.0f, lLin.m11 * rect.height);
        boxW = std::fabs(lLin.m00) * rect.width + std::fabs(lLin.m01) * rect.height;
        boxH = std::fabs(lLin.m10) * rect.width + std::fabs(lLin.m11) * rect.height;
        layoutTxM = lLin;
        layoutTxM.tx = rect.x - minRelX - (lLin.m00 * rect.x + lLin.m01 * rect.y);
        layoutTxM.ty = rect.y - minRelY - (lLin.m10 * rect.x + lLin.m11 * rect.y);
    }

    const bool hasT2 =
        t.tx != 0.0f || t.ty != 0.0f ||
        std::fabs(t.sx - 1.0f) > 1e-6f || std::fabs(t.sy - 1.0f) > 1e-6f ||
        std::fabs(t.rotDeg) > 1e-6f ||
        std::fabs(t.skewX) > 1e-6f || std::fabs(t.skewY) > 1e-6f;
    if (!hasT2) {
        if (!hasLayoutTx) {
            return viewboxM;
        }
        return hasViewboxScale ? Matrix2D::Multiply(viewboxM, layoutTxM) : layoutTxM;
    }

    Matrix2D localM = Matrix2D::Identity();
    if (std::fabs(t.sx - 1.0f) > 1e-6f || std::fabs(t.sy - 1.0f) > 1e-6f) {
        localM = Matrix2D::Multiply(Matrix2D::Scaling(t.sx, t.sy), localM);
    }
    if (std::fabs(t.skewX) > 1e-6f || std::fabs(t.skewY) > 1e-6f) {
        localM = Matrix2D::Multiply(Matrix2D::Skew(t.skewX, t.skewY), localM);
    }
    if (std::fabs(t.rotDeg) > 1e-6f) {
        localM = Matrix2D::Multiply(Matrix2D::Rotation(t.rotDeg), localM);
    }
    const float pivotX = rect.x + boxW * t.originX;
    const float pivotY = rect.y + boxH * t.originY;
    localM = localM.AroundPivot(pivotX, pivotY);
    localM.tx += t.tx;
    localM.ty += t.ty;
    if (hasLayoutTx) {
        localM = Matrix2D::Multiply(localM, layoutTxM);
    }
    return hasViewboxScale ? Matrix2D::Multiply(viewboxM, localM) : localM;
}

void UpdateWorldTransformsRecursive(
    VisualWorld& world,
    VisualNodeId id,
    const Matrix2D& parentWorld2D,
    const Matrix4x4& parentWorld3D,
    bool parentHas3D) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node) {
        return;
    }

    const Matrix2D prevWorld2D = node->layout.worldTransform;
    const Matrix2D local2D = ComputeNodeLocalMatrix2D(*node);
    const Matrix2D world2D = local2D.IsIdentity()
        ? parentWorld2D
        : Matrix2D::Multiply(parentWorld2D, local2D);
    node->layout.worldTransform = world2D;

    bool transformChanged =
        std::fabs(prevWorld2D.m00 - world2D.m00) > 1e-5f ||
        std::fabs(prevWorld2D.m01 - world2D.m01) > 1e-5f ||
        std::fabs(prevWorld2D.m10 - world2D.m10) > 1e-5f ||
        std::fabs(prevWorld2D.m11 - world2D.m11) > 1e-5f ||
        std::fabs(prevWorld2D.tx - world2D.tx) > 1e-5f ||
        std::fabs(prevWorld2D.ty - world2D.ty) > 1e-5f;

    const VisualTransform3D& t3 = node->Transform3D();
    const bool hasLocal3D = t3.enabled && (
        std::fabs(t3.rx) > 1e-6f || std::fabs(t3.ry) > 1e-6f || std::fabs(t3.rz) > 1e-6f ||
        std::fabs(t3.tx) > 1e-6f || std::fabs(t3.ty) > 1e-6f || std::fabs(t3.tz) > 1e-6f ||
        t3.camDist > 0.0f);

    Matrix4x4 nextWorld3D{};
    const bool prevHas3D = node->layout.hasPerspective3D;
    const bool nextHas3D = parentHas3D || hasLocal3D;
    node->layout.hasPerspective3D = nextHas3D;
    if (prevHas3D != nextHas3D) {
        transformChanged = true;
    }

    if (nextHas3D) {
        Matrix4x4 base3D = parentHas3D ? parentWorld3D : Matrix4x4::FromMatrix2D(parentWorld2D);
        if (!local2D.IsIdentity()) {
            base3D = Matrix4x4::Multiply(base3D, Matrix4x4::FromMatrix2D(local2D));
        }
        if (hasLocal3D) {
            CompositeTransform3DParams params{};
            params.rotationX = t3.rx;
            params.rotationY = t3.ry;
            params.rotationZ = t3.rz;
            params.translateX = t3.tx;
            params.translateY = t3.ty;
            params.translateZ = t3.tz;
            params.cameraDistance = t3.camDist;
            const float pivotX = node->layout.layoutRect.x + node->layout.layoutRect.width * node->transform2D.originX;
            const float pivotY = node->layout.layoutRect.y + node->layout.layoutRect.height * node->transform2D.originY;
            const Matrix4x4 local3D = Matrix4x4::FromCompositeTransform3D(params, pivotX, pivotY, 0.0f, pivotX, pivotY);
            nextWorld3D = Matrix4x4::Multiply(base3D, local3D);
        } else {
            nextWorld3D = base3D;
        }
        for (int mi = 0; mi < 16; ++mi) {
            if (std::fabs(node->WorldMatrix3D().m[mi] - nextWorld3D.m[mi]) > 1e-5f) {
                transformChanged = true;
                break;
            }
        }
        node->EnsureExtras().worldMatrix3D = nextWorld3D;
    } else {
        if (node->extras) node->extras->worldMatrix3D = Matrix4x4{};
    }

    if (transformChanged) {
        node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags | VisualDirty_Visual);
        world.MarkStructureDirty();
    }
    node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags & ~VisualDirty_Transform);

    for (VisualNodeId cid : node->children) {
        UpdateWorldTransformsRecursive(world, cid, world2D, nextWorld3D, nextHas3D);
    }
}

} // namespace

void VisualWorld::UpdateLayout(float viewportWidth, float viewportHeight) {
    if (!rootId_.IsValid()) {
        return;
    }
    VisualNode* rootNode = GetNode(rootId_);
    if (!rootNode) {
        return;
    }

    const float safeW = std::max(0.0f, std::isfinite(viewportWidth) ? viewportWidth : 0.0f);
    const float safeH = std::max(0.0f, std::isfinite(viewportHeight) ? viewportHeight : 0.0f);
    const Size viewportAvail{safeW, safeH};
    const Rect viewportSlot{0.0f, 0.0f, safeW, safeH};

    const bool viewportChanged =
        !SameFloatOrBothNaN(lastViewportSize_.width, safeW) ||
        !SameFloatOrBothNaN(lastViewportSize_.height, safeH);
    const bool rootChanged = (lastLaidOutRoot_ != rootId_);
    if (viewportChanged || rootChanged || !rootNode->layout.hasValidMeasure || !rootNode->layout.hasValidArrange) {
        RefreshSubtreeDepths(rootId_, 0u);
        rootNode->layout.hasValidMeasure = false;
        rootNode->layout.hasValidArrange = false;
        rootNode->dirtyFlags = static_cast<std::uint8_t>(rootNode->dirtyFlags | VisualDirty_Layout);
        MarkStructureDirty();
        lastViewportSize_ = viewportAvail;
        lastLaidOutRoot_ = rootId_;
    }

    for (auto& bucket : measureDepthBuckets_) {
        bucket.clear();
    }
    for (auto& bucket : arrangeDepthBuckets_) {
        bucket.clear();
    }
    for (auto& bucket : transformDepthBuckets_) {
        bucket.clear();
    }

    auto ensureMeasureArrangeBuckets = [&](std::size_t depth) {
        const std::size_t needed = depth + 1u;
        if (measureDepthBuckets_.size() < needed) {
            measureDepthBuckets_.resize(needed);
        }
        if (arrangeDepthBuckets_.size() < needed) {
            arrangeDepthBuckets_.resize(needed);
        }
    };
    auto ensureTransformBuckets = [&](std::size_t depth) {
        const std::size_t needed = depth + 1u;
        if (transformDepthBuckets_.size() < needed) {
            transformDepthBuckets_.resize(needed);
        }
    };

    const std::size_t slots = ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        VisualNode* n = NodeAtSlot(s);
        if (!n) {
            continue;
        }
        const bool customChangedLayoutTx =
            n->layout.hasLayoutTransform
                ? (std::fabs(n->customScalars[1] - n->layout.layoutTransform2D.sx) > 1e-6f ||
                   std::fabs(n->customScalars[2] - n->layout.layoutTransform2D.sy) > 1e-6f ||
                   std::fabs(n->customScalars[3] - n->layout.layoutTransform2D.rotDeg) > 1e-6f)
                : (std::fabs(n->customScalars[1] - 1.0f) > 1e-6f ||
                   std::fabs(n->customScalars[2] - 1.0f) > 1e-6f ||
                   std::fabs(n->customScalars[3]) > 1e-6f);
        if (customChangedLayoutTx) {
            n->layout.layoutTransform2D.sx = std::isfinite(n->customScalars[1]) ? n->customScalars[1] : 1.0f;
            n->layout.layoutTransform2D.sy = std::isfinite(n->customScalars[2]) ? n->customScalars[2] : 1.0f;
            n->layout.layoutTransform2D.rotDeg = std::isfinite(n->customScalars[3]) ? n->customScalars[3] : 0.0f;
            n->layout.hasLayoutTransform = true;
            n->layout.hasValidMeasure = false;
            n->layout.hasValidArrange = false;
            n->dirtyFlags = static_cast<std::uint8_t>(
                n->dirtyFlags | VisualDirty_Layout | VisualDirty_Transform | VisualDirty_Visual);
            MarkStructureDirty();
        }
        const std::uint16_t depth = n->layout.treeDepth;
        if ((n->dirtyFlags & (VisualDirty_Layout | VisualDirty_Structure)) != 0u) {
            if ((n->dirtyFlags & VisualDirty_Structure) != 0u) {
                RefreshSubtreeDepths(n->id, depth);
            }
            if (!n->layout.hasValidMeasure || std::isnan(n->layout.lastMeasureConstraint.width)) {
                // Never measured before: bubble invalidation to nearest ancestor that has a valid constraint (or root)
                VisualNode* cur = n;
                while (cur) {
                    cur->layout.hasValidMeasure = false;
                    cur->layout.hasValidArrange = false;
                    cur->dirtyFlags = static_cast<std::uint8_t>(cur->dirtyFlags | VisualDirty_Layout);
                    if (!cur->parent.IsValid() || !std::isnan(cur->layout.lastMeasureConstraint.width)) {
                        const std::uint16_t cd = cur->layout.treeDepth;
                        ensureMeasureArrangeBuckets(cd);
                        measureDepthBuckets_[cd].push_back(cur->id);
                        arrangeDepthBuckets_[cd].push_back(cur->id);
                        break;
                    }
                    cur = GetNode(cur->parent);
                }
            } else {
                n->layout.hasValidMeasure = false;
                n->layout.hasValidArrange = false;
                ensureMeasureArrangeBuckets(depth);
                measureDepthBuckets_[depth].push_back(n->id);
                arrangeDepthBuckets_[depth].push_back(n->id);
            }
        }
        if ((n->dirtyFlags & VisualDirty_Transform) != 0u) {
            ensureTransformBuckets(depth);
            transformDepthBuckets_[depth].push_back(n->id);
        }
    }

    // Phase 1: Dynamic depth-bucket bottom-up Measure (maxDepth -> 0)
    // Only re-measures dirty nodes with cached constraints and bubbles upward only when desiredSize actually changes.
    for (std::size_t pass = 0u; pass < 4u; ++pass) {
        bool anyMeasuredInPass = false;
        for (std::ptrdiff_t d = static_cast<std::ptrdiff_t>(measureDepthBuckets_.size()) - 1; d >= 0; --d) {
            const std::size_t ud = static_cast<std::size_t>(d);
            if (measureDepthBuckets_[ud].empty()) {
                continue;
            }
            for (std::size_t i = 0u; i < measureDepthBuckets_[ud].size(); ++i) {
                const VisualNodeId nid = measureDepthBuckets_[ud][i];
                VisualNode* node = GetNode(nid);
                if (!node || node->layout.hasValidMeasure) {
                    continue;
                }
                const Size constraint = (nid == rootId_)
                    ? viewportAvail
                    : node->layout.lastMeasureConstraint;
                if (std::isnan(constraint.width)) {
                    continue;
                }
                const Size prevDesired = node->layout.desiredSize;
                const Size newDesired = MeasureNode(*this, nid, constraint);
                anyMeasuredInPass = true;

                ensureMeasureArrangeBuckets(ud);
                arrangeDepthBuckets_[ud].push_back(nid);
                if (nid != rootId_ && node->parent.IsValid() &&
                    (!SameFloatOrBothNaN(prevDesired.width, newDesired.width) ||
                     !SameFloatOrBothNaN(prevDesired.height, newDesired.height))) {
                    if (VisualNode* p = GetNode(node->parent)) {
                        p->layout.hasValidMeasure = false;
                        p->layout.hasValidArrange = false;
                        p->dirtyFlags = static_cast<std::uint8_t>(p->dirtyFlags | VisualDirty_Layout);
                        const std::uint16_t pd = p->layout.treeDepth;
                        ensureMeasureArrangeBuckets(pd);
                        measureDepthBuckets_[pd].push_back(p->id);
                        arrangeDepthBuckets_[pd].push_back(p->id);
                    }
                }
            }
            measureDepthBuckets_[ud].clear();
        }
        if (!anyMeasuredInPass) {
            break;
        }
    }

    // Phase 2: Dynamic depth-bucket top-down Arrange (0 -> maxDepth)
    for (std::size_t d = 0u; d < arrangeDepthBuckets_.size(); ++d) {
        auto& bucket = arrangeDepthBuckets_[d];
        for (const VisualNodeId nid : bucket) {
            VisualNode* node = GetNode(nid);
            if (!node || node->layout.hasValidArrange) {
                continue;
            }
            const Rect slot = (nid == rootId_) ? viewportSlot : node->layout.lastArrangeSlot;
            if (std::isnan(slot.width)) {
                continue;
            }
            const VisualNode* parentNode =
                (nid != rootId_ && node->parent.IsValid()) ? GetNode(node->parent) : nullptr;
            const bool parentIsVb =
                parentNode && parentNode->layout.layoutMode == LayoutMode::Viewbox;
            ArrangeNode(
                *this,
                nid,
                slot,
                parentIsVb,
                parentIsVb ? node->layout.viewboxScaleX : 1.0f,
                parentIsVb ? node->layout.viewboxScaleY : 1.0f,
                parentIsVb ? node->layout.viewboxOriginX : 0.0f,
                parentIsVb ? node->layout.viewboxOriginY : 0.0f);
            if ((node->dirtyFlags & VisualDirty_Transform) != 0u) {
                const std::uint16_t td = node->layout.treeDepth;
                ensureTransformBuckets(td);
                transformDepthBuckets_[td].push_back(nid);
            }
        }
        bucket.clear();
    }

    // Phase 3: Incremental top-down 2D + 3D world transform propagation (0 -> maxDepth)
    for (std::size_t d = 0u; d < transformDepthBuckets_.size(); ++d) {
        auto& bucket = transformDepthBuckets_[d];
        for (const VisualNodeId nid : bucket) {
            VisualNode* node = GetNode(nid);
            if (!node || (node->dirtyFlags & VisualDirty_Transform) == 0u) {
                continue;
            }
            Matrix2D pWorld2D = Matrix2D::Identity();
            Matrix4x4 pWorld3D{};
            bool pHas3D = false;
            if (nid != rootId_ && node->parent.IsValid()) {
                if (const VisualNode* p = GetNode(node->parent)) {
                    pWorld2D = p->layout.worldTransform;
                    pWorld3D = p->WorldMatrix3D();
                    pHas3D = p->layout.hasPerspective3D;
                }
            }
            UpdateWorldTransformsRecursive(*this, nid, pWorld2D, pWorld3D, pHas3D);
        }
        bucket.clear();
    }

    // Clear geometry dirty bits; leave VisualDirty_Visual intact for VisualRenderAdapter::SyncPagedRenderTree.
    bool stillVisualDirty = false;
    for (std::size_t s = 0u; s < slots; ++s) {
        VisualNode* n = NodeAtSlot(s);
        if (!n) {
            continue;
        }
        n->dirtyFlags = static_cast<std::uint8_t>(
            n->dirtyFlags & ~(VisualDirty_Layout | VisualDirty_Structure | VisualDirty_Transform));
        if (n->dirtyFlags != VisualDirty_Clean || n->drawDirty) {
            stillVisualDirty = true;
        }
    }
    anyDirty_ = stillVisualDirty || structureDirty_;
}

} // namespace aero
