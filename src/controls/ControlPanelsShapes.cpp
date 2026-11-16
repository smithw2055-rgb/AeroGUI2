#include "ControlsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/controls/AxbTemplateMounter.hpp"

namespace aero {

namespace {

struct GridCellSpan {
    std::size_t row = 0;
    std::size_t col = 0;
    std::size_t rowSpan = 1;
    std::size_t colSpan = 1;
};

/// Clamps an element's Grid attached Row/Column/Span values to valid track bounds.
[[nodiscard]] GridCellSpan ResolveGridCellSpan(
    const UIElement* child,
    std::size_t rowCount,
    std::size_t colCount) noexcept {
    const std::size_t r = std::min<std::size_t>(
        static_cast<std::size_t>(Grid::GetRow(child)), rowCount - 1u);
    const std::size_t c = std::min<std::size_t>(
        static_cast<std::size_t>(Grid::GetColumn(child)), colCount - 1u);
    const std::size_t rSpan = std::min<std::size_t>(
        static_cast<std::size_t>(Grid::GetRowSpan(child)), rowCount - r);
    const std::size_t cSpan = std::min<std::size_t>(
        static_cast<std::size_t>(Grid::GetColumnSpan(child)), colCount - c);
    return GridCellSpan{r, c, rSpan, cSpan};
}

/// Distributes multi-span desired size excess during Grid MeasureCore across Star tracks first, then Auto tracks.
template <typename TrackT, typename SizeMember>
void DistributeMultiSpanMeasureExcess(
    std::vector<TrackT>& tracks,
    SizeMember sizeMember,
    std::size_t startIdx,
    std::size_t span,
    float desiredExtent) noexcept {
    float spanExtent = 0.0f;
    float starWeight = 0.0f;
    std::size_t autoTracks = 0u;
    for (std::size_t i = 0u; i < span; ++i) {
        const auto& tr = tracks[startIdx + i];
        spanExtent += tr.*sizeMember;
        if (tr.unit == GridUnitType::Star) {
            starWeight += std::max(0.0f, tr.value);
        } else if (tr.unit == GridUnitType::Auto) {
            ++autoTracks;
        }
    }
    if (desiredExtent <= spanExtent) {
        return;
    }
    const float excess = desiredExtent - spanExtent;
    if (starWeight > 0.0f) {
        for (std::size_t i = 0u; i < span; ++i) {
            auto& tr = tracks[startIdx + i];
            if (tr.unit == GridUnitType::Star) {
                tr.*sizeMember += excess * (std::max(0.0f, tr.value) / starWeight);
            }
        }
    } else if (autoTracks > 0u) {
        const float share = excess / static_cast<float>(autoTracks);
        for (std::size_t i = 0u; i < span; ++i) {
            auto& tr = tracks[startIdx + i];
            if (tr.unit == GridUnitType::Auto) {
                tr.*sizeMember += share;
            }
        }
    }
}

/// Distributes multi-span desired size excess during Grid ArrangeCore when all spanned tracks are non-Star.
template <typename TrackT, typename SizeMember>
void DistributeMultiSpanArrangeAutoExcess(
    std::vector<TrackT>& tracks,
    SizeMember sizeMember,
    std::size_t startIdx,
    std::size_t span,
    float desiredExtent) noexcept {
    float spanExtent = 0.0f;
    bool hasStar = false;
    std::size_t autoTracks = 0u;
    for (std::size_t i = 0u; i < span; ++i) {
        const auto& tr = tracks[startIdx + i];
        spanExtent += tr.*sizeMember;
        if (tr.unit == GridUnitType::Star) {
            hasStar = true;
        } else if (tr.unit == GridUnitType::Auto) {
            ++autoTracks;
        }
    }
    if (!hasStar && autoTracks > 0u && desiredExtent > spanExtent) {
        const float share = (desiredExtent - spanExtent) / static_cast<float>(autoTracks);
        for (std::size_t i = 0u; i < span; ++i) {
            auto& tr = tracks[startIdx + i];
            if (tr.unit == GridUnitType::Auto) {
                tr.*sizeMember += share;
            }
        }
    }
}

/// Reads PropId_Stretch or falls back to `defaultStretch`.
[[nodiscard]] Stretch ReadStretchProperty(const UIElement& elem, Stretch defaultStretch) noexcept {
    const UnifiedValue v = elem.GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= static_cast<std::int64_t>(Stretch::None) &&
            *i <= static_cast<std::int64_t>(Stretch::UniformToFill)) {
            return static_cast<Stretch>(*i);
        }
    }
    return defaultStretch;
}

/// Reads SVG path string from PropId_PathData.
[[nodiscard]] std::string ReadSvgPathData(const UIElement& elem) {
    const UnifiedValue v = elem.GetValue(PropId_PathData);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    return {};
}

/// Writes SVG path string to PropId_PathData with local rank.
void WriteSvgPathData(UIElement& elem, std::string_view svgPathData) {
    elem.SetValue(PropId_PathData, value::FromString(std::string(svgPathData)), ValueSourceRank::Local);
}

/// Reads raw point list string from PropId_ShapePoints.
[[nodiscard]] std::string ReadShapePoints(const UIElement& elem) {
    const UnifiedValue v = elem.GetValue(PropId_ShapePoints);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

/// Writes point list string to PropId_ShapePoints with local rank.
void WriteShapePoints(UIElement& elem, std::string_view pointsText) {
    elem.SetValue(PropId_ShapePoints, value::FromString(std::string(pointsText)), ValueSourceRank::Local);
}

/// Returns true when either BorderGradientId or BorderBrush is explicitly set.
[[nodiscard]] bool HasExplicitSvgStroke(const UIElement& elem) noexcept {
    if (detail::ExtractGradientBrushId(elem.GetValue(PropId_BorderGradientId)) != 0u) {
        return true;
    }
    return value::TryGetColor(elem.GetValue(PropId_BorderBrush)).has_value();
}

/// Resolves stroke thickness for SVG-backed shapes (Path, Polygon, Polyline).
[[nodiscard]] float ResolveSvgStrokeThickness(
    const UIElement& elem,
    bool defaultOneWhenExplicitStroke) noexcept {
    float st = 0.0f;
    if (TryGetFloat(elem.GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(elem.GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    if (defaultOneWhenExplicitStroke && HasExplicitSvgStroke(elem)) {
        return 1.0f;
    }
    return 0.0f;
}

/// Converts updated PropId_ShapePoints into SVG PathData and bakes MTSDF geometry if non-empty.
void HandleShapePointsPropertyChanged(
    UIElement& elem,
    std::uint16_t propId,
    const UnifiedValue& newValue,
    bool closed) {
    if (propId != PropId_ShapePoints) {
        return;
    }
    if (const auto* sv = std::get_if<StringValue>(&newValue)) {
        const std::string svg = ConvertPointsToSvgPath(sv->view(), closed);
        if (!svg.empty()) {
            (void)AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(svg);
        }
        WriteSvgPathData(elem, svg);
    } else if (std::holds_alternative<std::monostate>(newValue)) {
        WriteSvgPathData(elem, {});
    }
}

/// Measures an SVG-backed shape using baked MTSDF bounds or parsed SVG bounds and its Stretch mode.
[[nodiscard]] Size MeasureSvgPathCore(
    const UIElement& elem,
    std::string_view data,
    Stretch stretch,
    Size availableSize) noexcept {
    const Thickness p = elem.GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    if (data.empty()) {
        return Size{padW, padH};
    }

    float bx = 0.0f;
    float by = 0.0f;
    float bw = 0.0f;
    float bh = 0.0f;
    const std::uint64_t h = ComputeSvgPathHash(data);
    if (const AxbMtsdfPathEntry* baked = AxbTemplateRegistry::Instance().FindMtsdfPath(h)) {
        bx = baked->boundsX;
        by = baked->boundsY;
        bw = baked->boundsW;
        bh = baked->boundsH;
    } else {
        const Rect b = MeasureSvgPathBounds(data);
        bx = b.x;
        by = b.y;
        bw = b.width;
        bh = b.height;
    }

    if (stretch == Stretch::None) {
        return Size{
            std::max(1.0f, std::max(bw, bx + bw)) + padW,
            std::max(1.0f, std::max(bh, by + bh)) + padH};
    }
    if ((stretch == Stretch::Uniform || stretch == Stretch::UniformToFill) &&
        bw > 1e-4f && bh > 1e-4f) {
        if (elem.GetWidth() >= 0.0f && elem.GetHeight() < 0.0f &&
            std::isfinite(availableSize.width) && availableSize.width < detail::kUnconstrainedSpace * 0.5f) {
            const float wAvail = std::max(0.0f, availableSize.width - padW);
            return Size{wAvail + padW, std::max(1.0f, bh * (wAvail / bw)) + padH};
        }
        if (elem.GetHeight() >= 0.0f && elem.GetWidth() < 0.0f &&
            std::isfinite(availableSize.height) && availableSize.height < detail::kUnconstrainedSpace * 0.5f) {
            const float hAvail = std::max(0.0f, availableSize.height - padH);
            return Size{std::max(1.0f, bw * (hAvail / bh)) + padW, hAvail + padH};
        }
    }
    return Size{
        std::max(1.0f, bw) + padW,
        std::max(1.0f, bh) + padH};
}

} // namespace

// ============================================================================
// Type Registrations (Panels & Shapes)
// ============================================================================

AERO_IMPLEMENT_CLASS(Panel, UIElement, "Aero.UI.Panel")
AERO_IMPLEMENT_CLASS(StackPanel, Panel, "Aero.UI.StackPanel")
AERO_IMPLEMENT_CLASS(DockPanel, Panel, "Aero.UI.DockPanel")
AERO_IMPLEMENT_CLASS(WrapPanel, Panel, "Aero.UI.WrapPanel")
AERO_IMPLEMENT_CLASS(Grid, Panel, "Aero.UI.Grid")
AERO_IMPLEMENT_CLASS(Canvas, Panel, "Aero.UI.Canvas")
AERO_IMPLEMENT_CLASS(UniformGrid, Panel, "Aero.UI.UniformGrid")
AERO_IMPLEMENT_CLASS(ScrollViewer, ContentControl, "Aero.UI.ScrollViewer")
AERO_IMPLEMENT_CLASS(Popup, Control, "Aero.UI.Popup")
AERO_IMPLEMENT_CLASS(ToolTip, ContentControl, "Aero.UI.ToolTip")

AERO_IMPLEMENT_CLASS(Shape, UIElement, "Aero.UI.Shape")
AERO_IMPLEMENT_CLASS(Rectangle, Shape, "Aero.UI.Rectangle")
AERO_IMPLEMENT_CLASS(Ellipse, Shape, "Aero.UI.Ellipse")
AERO_IMPLEMENT_CLASS(Line, Shape, "Aero.UI.Line")
AERO_IMPLEMENT_CLASS(Path, Shape, "Aero.UI.Path")
AERO_IMPLEMENT_CLASS(Polygon, Shape, "Aero.UI.Polygon")
AERO_IMPLEMENT_CLASS(Polyline, Shape, "Aero.UI.Polyline")

// ============================================================================
// Panel
// ============================================================================

Panel::Panel() {
    EnsureControlTypesRegistered();
}

// ============================================================================
// StackPanel
// ============================================================================

StackPanel::StackPanel() {
    EnsureControlTypesRegistered();
}

void StackPanel::SetOrientation(Orientation orientation) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(orientation)),
        ValueSourceRank::Local);
}

Orientation StackPanel::GetOrientation() const noexcept {
    const auto v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 1) ? Orientation::Horizontal : Orientation::Vertical;
    }
    return Orientation::Vertical;
}

void StackPanel::SetSpacing(float spacing) {
    SetValue(PropId_Spacing, MakeFloat(spacing), ValueSourceRank::Local);
}

float StackPanel::GetSpacing() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Spacing), out)) {
        return out;
    }
    return 0.0f;
}

Size StackPanel::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    const Orientation orient = GetOrientation();
    const float spacing = GetSpacing();

    float accumMain = 0.0f;
    float maxCross = 0.0f;
    std::size_t visibleCount = 0u;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Measure(childAvail);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Measure(childAvail);
            continue;
        }
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            accumMain += d.height;
            maxCross = std::max(maxCross, d.width);
        } else {
            accumMain += d.width;
            maxCross = std::max(maxCross, d.height);
        }
        ++visibleCount;
    }

    if (visibleCount > 1u) {
        accumMain += spacing * static_cast<float>(visibleCount - 1u);
    }

    if (orient == Orientation::Vertical) {
        return Size{maxCross + padW, accumMain + padH};
    }
    return Size{accumMain + padW, maxCross + padH};
}

void StackPanel::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const Orientation orient = GetOrientation();
    const float spacing = GetSpacing();

    float cursor = (orient == Orientation::Vertical) ? startY : startX;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Arrange(finalRect);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
            continue;
        }
        const Size d = c->DesiredSize();
        if (orient == Orientation::Vertical) {
            c->Arrange(Rect{startX, cursor, innerW, d.height});
            cursor += d.height + spacing;
        } else {
            c->Arrange(Rect{cursor, startY, d.width, innerH});
            cursor += d.width + spacing;
        }
    }
}

// ============================================================================
// DockPanel
// ============================================================================

DockPanel::DockPanel() {
    EnsureControlTypesRegistered();
}

void DockPanel::SetDock(UIElement* element, Dock dock) {
    if (element) {
        element->SetValue(
            PropId_Dock,
            value::FromInt(static_cast<std::int64_t>(dock)),
            ValueSourceRank::Local);
    }
}

Dock DockPanel::GetDock(const UIElement* element) noexcept {
    if (!element) {
        return Dock::Left;
    }
    const auto v = element->GetValue(PropId_Dock);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Dock>(*i);
        }
    }
    return Dock::Left;
}

void DockPanel::SetLastChildFill(bool fill) {
    SetValue(PropId_LastChildFill, value::FromBool(fill), ValueSourceRank::Local);
}

bool DockPanel::GetLastChildFill() const noexcept {
    const auto v = GetValue(PropId_LastChildFill);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return true;
}

Size DockPanel::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    const bool lastFill = GetLastChildFill();
    UIElement* lastVisible = nullptr;
    if (lastFill) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (!detail::IsFloatingPopupOverlay(c) && c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    float accumW = 0.0f;
    float accumH = 0.0f;
    float parentW = 0.0f;
    float parentH = 0.0f;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Measure(Size{innerAvailW, innerAvailH});
            continue;
        }
        const Size childConstraint{
            std::max(0.0f, innerAvailW - accumW),
            std::max(0.0f, innerAvailH - accumH)};
        c->Measure(childConstraint);
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Size d = c->DesiredSize();
        if (lastFill && c == lastVisible) {
            parentW = std::max(parentW, accumW + d.width);
            parentH = std::max(parentH, accumH + d.height);
        } else {
            switch (GetDock(c)) {
                case Dock::Left:
                case Dock::Right:
                    parentH = std::max(parentH, accumH + d.height);
                    accumW += d.width;
                    break;
                case Dock::Top:
                case Dock::Bottom:
                    parentW = std::max(parentW, accumW + d.width);
                    accumH += d.height;
                    break;
            }
        }
    }

    parentW = std::max(parentW, accumW);
    parentH = std::max(parentH, accumH);
    return Size{parentW + padW, parentH + padH};
}

void DockPanel::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    float left = finalRect.x + p.left;
    float top = finalRect.y + p.top;
    float right = left + std::max(0.0f, finalRect.width - (p.left + p.right));
    float bottom = top + std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const bool lastFill = GetLastChildFill();
    UIElement* lastVisible = nullptr;
    if (lastFill) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (!detail::IsFloatingPopupOverlay(c) && c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Arrange(finalRect);
            continue;
        }
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{left, top, 0.0f, 0.0f});
            continue;
        }
        const float remW = std::max(0.0f, right - left);
        const float remH = std::max(0.0f, bottom - top);
        if (lastFill && c == lastVisible) {
            c->Arrange(Rect{left, top, remW, remH});
        } else {
            const Size d = c->DesiredSize();
            switch (GetDock(c)) {
                case Dock::Left: {
                    const float w = std::min(d.width, remW);
                    c->Arrange(Rect{left, top, w, remH});
                    left += w;
                    break;
                }
                case Dock::Right: {
                    const float w = std::min(d.width, remW);
                    c->Arrange(Rect{right - w, top, w, remH});
                    right -= w;
                    break;
                }
                case Dock::Top: {
                    const float h = std::min(d.height, remH);
                    c->Arrange(Rect{left, top, remW, h});
                    top += h;
                    break;
                }
                case Dock::Bottom: {
                    const float h = std::min(d.height, remH);
                    c->Arrange(Rect{left, bottom - h, remW, h});
                    bottom -= h;
                    break;
                }
            }
        }
    }
}

// ============================================================================
// WrapPanel
// ============================================================================

WrapPanel::WrapPanel() {
    EnsureControlTypesRegistered();
}

void WrapPanel::SetOrientation(Orientation orientation) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(orientation)),
        ValueSourceRank::Local);
}

Orientation WrapPanel::GetOrientation() const noexcept {
    const auto v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
    }
    return Orientation::Horizontal;
}

void WrapPanel::SetItemWidth(float width) {
    if (std::isnan(width) || width <= 0.0f || std::isinf(width)) {
        (void)ClearValue(PropId_ItemWidth, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_ItemWidth, MakeFloat(width), ValueSourceRank::Local);
    }
}

float WrapPanel::GetItemWidth() const noexcept {
    float out = -1.0f;
    if (TryGetFloat(GetValue(PropId_ItemWidth), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    return -1.0f;
}

void WrapPanel::SetItemHeight(float height) {
    if (std::isnan(height) || height <= 0.0f || std::isinf(height)) {
        (void)ClearValue(PropId_ItemHeight, ValueSourceRank::Local);
    } else {
        (void)SetValue(PropId_ItemHeight, MakeFloat(height), ValueSourceRank::Local);
    }
}

float WrapPanel::GetItemHeight() const noexcept {
    float out = -1.0f;
    if (TryGetFloat(GetValue(PropId_ItemHeight), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    return -1.0f;
}

void WrapPanel::SetSpacing(float spacing) {
    SetValue(PropId_Spacing, MakeFloat(spacing), ValueSourceRank::Local);
}

float WrapPanel::GetSpacing() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Spacing), out)) {
        return std::max(0.0f, out);
    }
    return 0.0f;
}

Size WrapPanel::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerW = std::max(0.0f, availableSize.width - padW);
    const float innerH = std::max(0.0f, availableSize.height - padH);

    const Orientation orient = GetOrientation();
    const float itemW = GetItemWidth();
    const float itemH = GetItemHeight();
    const float spacing = GetSpacing();

    const Size childConstraint{
        (itemW > 0.0f) ? itemW : innerW,
        (itemH > 0.0f) ? itemH : innerH};

    const float lineLimit = (orient == Orientation::Horizontal) ? innerW : innerH;
    float curLineMain = 0.0f;
    float curLineCross = 0.0f;
    float panelMain = 0.0f;
    float panelCross = 0.0f;
    std::size_t itemsInCurLine = 0u;
    std::size_t completedLines = 0u;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c)) {
            c->Measure(Size{innerW, innerH});
            continue;
        }
        c->Measure(childConstraint);
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Size d = c->DesiredSize();
        const float cw = (itemW > 0.0f) ? itemW : d.width;
        const float ch = (itemH > 0.0f) ? itemH : d.height;
        const float itemMain = (orient == Orientation::Horizontal) ? cw : ch;
        const float itemCross = (orient == Orientation::Horizontal) ? ch : cw;

        const float gap = (itemsInCurLine > 0u) ? spacing : 0.0f;
        if (itemsInCurLine > 0u && (curLineMain + gap + itemMain > lineLimit + 1e-4f)) {
            panelMain = std::max(panelMain, curLineMain);
            panelCross += curLineCross;
            ++completedLines;
            curLineMain = itemMain;
            curLineCross = itemCross;
            itemsInCurLine = 1u;
        } else {
            curLineMain += gap + itemMain;
            curLineCross = std::max(curLineCross, itemCross);
            ++itemsInCurLine;
        }
    }

    if (itemsInCurLine > 0u) {
        panelMain = std::max(panelMain, curLineMain);
        panelCross += curLineCross;
        if (completedLines > 0u) {
            panelCross += spacing * static_cast<float>(completedLines);
        }
    }

    if (orient == Orientation::Horizontal) {
        return Size{panelMain + padW, panelCross + padH};
    }
    return Size{panelCross + padW, panelMain + padH};
}

void WrapPanel::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const Orientation orient = GetOrientation();
    const float itemW = GetItemWidth();
    const float itemH = GetItemHeight();
    const float spacing = GetSpacing();
    const float lineLimit = (orient == Orientation::Horizontal) ? innerW : innerH;

    auto flushLine = [&](UIElement* lineFirst, UIElement* lineEndExclusive, float crossOffset, float lineCross) noexcept {
        float mainCursor = 0.0f;
        for (UIElement* it = lineFirst; it != lineEndExclusive; it = it->NextSibling()) {
            if (detail::IsFloatingPopupOverlay(it)) {
                it->Arrange(finalRect);
                continue;
            }
            if (it->GetVisibility() == Visibility::Collapsed) {
                it->Arrange(Rect{startX, startY, 0.0f, 0.0f});
                continue;
            }
            const Size d = it->DesiredSize();
            const float cw = (itemW > 0.0f) ? itemW : d.width;
            const float ch = (itemH > 0.0f) ? itemH : d.height;
            if (orient == Orientation::Horizontal) {
                it->Arrange(Rect{startX + mainCursor, startY + crossOffset, cw, (itemH > 0.0f) ? itemH : lineCross});
                mainCursor += cw + spacing;
            } else {
                it->Arrange(Rect{startX + crossOffset, startY + mainCursor, (itemW > 0.0f) ? itemW : lineCross, ch});
                mainCursor += ch + spacing;
            }
        }
    };

    UIElement* lineFirst = FirstChild();
    float curLineMain = 0.0f;
    float curLineCross = 0.0f;
    float crossCursor = 0.0f;
    std::size_t itemsInCurLine = 0u;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(c) || c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Size d = c->DesiredSize();
        const float cw = (itemW > 0.0f) ? itemW : d.width;
        const float ch = (itemH > 0.0f) ? itemH : d.height;
        const float itemMain = (orient == Orientation::Horizontal) ? cw : ch;
        const float itemCross = (orient == Orientation::Horizontal) ? ch : cw;
        const float gap = (itemsInCurLine > 0u) ? spacing : 0.0f;

        if (itemsInCurLine > 0u && (curLineMain + gap + itemMain > lineLimit + 1e-4f)) {
            flushLine(lineFirst, c, crossCursor, curLineCross);
            crossCursor += curLineCross + spacing;
            lineFirst = c;
            curLineMain = itemMain;
            curLineCross = itemCross;
            itemsInCurLine = 1u;
        } else {
            curLineMain += gap + itemMain;
            curLineCross = std::max(curLineCross, itemCross);
            ++itemsInCurLine;
        }
    }

    if (lineFirst != nullptr) {
        flushLine(lineFirst, nullptr, crossCursor, curLineCross);
    }
}

// ============================================================================
// Grid
// ============================================================================

Grid::Grid() {
    EnsureControlTypesRegistered();
}

void Grid::AddRowDefinition(GridUnitType unit, float value) {
    rows_.push_back(RowDefinition{unit, value, 0.0f, 0.0f});
    InvalidateLayout();
}

void Grid::AddColumnDefinition(GridUnitType unit, float value) {
    cols_.push_back(ColumnDefinition{unit, value, 0.0f, 0.0f});
    InvalidateLayout();
}

void Grid::SetRow(UIElement* element, std::int32_t row) {
    if (element) {
        element->SetValue(PropId_GridRow, value::FromInt(row), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetRow(const UIElement* element) noexcept {
    if (!element) {
        return 0;
    }
    const auto v = element->GetValue(PropId_GridRow);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, *i));
    }
    return 0;
}

void Grid::SetColumn(UIElement* element, std::int32_t col) {
    if (element) {
        element->SetValue(PropId_GridColumn, value::FromInt(col), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetColumn(const UIElement* element) noexcept {
    if (!element) {
        return 0;
    }
    const auto v = element->GetValue(PropId_GridColumn);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, *i));
    }
    return 0;
}

void Grid::SetRowSpan(UIElement* element, std::int32_t span) {
    if (element) {
        element->SetValue(PropId_GridRowSpan, value::FromInt(std::max<std::int32_t>(1, span)), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetRowSpan(const UIElement* element) noexcept {
    if (!element) {
        return 1;
    }
    const auto v = element->GetValue(PropId_GridRowSpan);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(1, *i));
    }
    return 1;
}

void Grid::SetColumnSpan(UIElement* element, std::int32_t span) {
    if (element) {
        element->SetValue(PropId_GridColumnSpan, value::FromInt(std::max<std::int32_t>(1, span)), ValueSourceRank::Local);
    }
}

std::int32_t Grid::GetColumnSpan(const UIElement* element) noexcept {
    if (!element) {
        return 1;
    }
    const auto v = element->GetValue(PropId_GridColumnSpan);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(1, *i));
    }
    return 1;
}

Size Grid::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size innerAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    if (rows_.empty() && cols_.empty()) {
        return UIElement::MeasureCore(availableSize);
    }

    const std::size_t rowCount = std::max<std::size_t>(1u, rows_.size());
    const std::size_t colCount = std::max<std::size_t>(1u, cols_.size());

    std::vector<RowDefinition> workRows = rows_;
    std::vector<ColumnDefinition> workCols = cols_;
    if (workRows.empty()) {
        workRows.push_back(RowDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }
    if (workCols.empty()) {
        workCols.push_back(ColumnDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }

    for (auto& r : workRows) {
        r.computedHeight = (r.unit == GridUnitType::Pixel) ? r.value : 0.0f;
    }
    for (auto& c : workCols) {
        c.computedWidth = (c.unit == GridUnitType::Pixel) ? c.value : 0.0f;
    }

    bool hasMultiSpan = false;
    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        child->Measure(innerAvail);
        if (detail::IsFloatingPopupOverlay(child)) {
            continue;
        }
        const GridCellSpan cell = ResolveGridCellSpan(child, rowCount, colCount);
        const Size d = child->DesiredSize();
        if (cell.rowSpan == 1u) {
            if (workRows[cell.row].unit != GridUnitType::Pixel) {
                workRows[cell.row].computedHeight = std::max(workRows[cell.row].computedHeight, d.height);
            }
        } else {
            hasMultiSpan = true;
        }
        if (cell.colSpan == 1u) {
            if (workCols[cell.col].unit != GridUnitType::Pixel) {
                workCols[cell.col].computedWidth = std::max(workCols[cell.col].computedWidth, d.width);
            }
        } else {
            hasMultiSpan = true;
        }
    }

    if (hasMultiSpan) {
        for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (detail::IsFloatingPopupOverlay(child)) {
                continue;
            }
            const GridCellSpan cell = ResolveGridCellSpan(child, rowCount, colCount);
            const Size d = child->DesiredSize();
            if (cell.rowSpan > 1u) {
                DistributeMultiSpanMeasureExcess(
                    workRows, &RowDefinition::computedHeight, cell.row, cell.rowSpan, d.height);
            }
            if (cell.colSpan > 1u) {
                DistributeMultiSpanMeasureExcess(
                    workCols, &ColumnDefinition::computedWidth, cell.col, cell.colSpan, d.width);
            }
        }
    }

    for (std::size_t i = 0u; i < cols_.size() && i < workCols.size(); ++i) {
        cols_[i].computedWidth = workCols[i].computedWidth;
    }
    for (std::size_t i = 0u; i < rows_.size() && i < workRows.size(); ++i) {
        rows_[i].computedHeight = workRows[i].computedHeight;
    }

    float totalW = 0.0f;
    for (const auto& c : workCols) {
        totalW += c.computedWidth;
    }
    float totalH = 0.0f;
    for (const auto& r : workRows) {
        totalH += r.computedHeight;
    }

    return Size{totalW + padW, totalH + padH};
}

void Grid::ArrangeCore(Rect finalRect) noexcept {
    if (rows_.empty() && cols_.empty()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }

    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    std::vector<RowDefinition> workRows = rows_;
    std::vector<ColumnDefinition> workCols = cols_;
    if (workRows.empty()) {
        workRows.push_back(RowDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }
    if (workCols.empty()) {
        workCols.push_back(ColumnDefinition{GridUnitType::Star, 1.0f, 0.0f, 0.0f});
    }

    float fixedW = 0.0f;
    float totalStarW = 0.0f;
    for (auto& c : workCols) {
        if (c.unit == GridUnitType::Pixel) {
            c.computedWidth = c.value;
            fixedW += c.computedWidth;
        } else if (c.unit == GridUnitType::Auto) {
            c.computedWidth = 0.0f;
        } else {
            c.computedWidth = 0.0f;
            totalStarW += std::max(0.0f, c.value);
        }
    }

    float fixedH = 0.0f;
    float totalStarH = 0.0f;
    for (auto& r : workRows) {
        if (r.unit == GridUnitType::Pixel) {
            r.computedHeight = r.value;
            fixedH += r.computedHeight;
        } else if (r.unit == GridUnitType::Auto) {
            r.computedHeight = 0.0f;
        } else {
            r.computedHeight = 0.0f;
            totalStarH += std::max(0.0f, r.value);
        }
    }

    bool hasMultiSpan = false;
    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(child)) {
            continue;
        }
        const GridCellSpan cell = ResolveGridCellSpan(child, workRows.size(), workCols.size());
        const Size d = child->DesiredSize();
        if (cell.rowSpan == 1u) {
            if (workRows[cell.row].unit == GridUnitType::Auto) {
                workRows[cell.row].computedHeight = std::max(workRows[cell.row].computedHeight, d.height);
            }
        } else {
            hasMultiSpan = true;
        }
        if (cell.colSpan == 1u) {
            if (workCols[cell.col].unit == GridUnitType::Auto) {
                workCols[cell.col].computedWidth = std::max(workCols[cell.col].computedWidth, d.width);
            }
        } else {
            hasMultiSpan = true;
        }
    }

    if (hasMultiSpan) {
        for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (detail::IsFloatingPopupOverlay(child)) {
                continue;
            }
            const GridCellSpan cell = ResolveGridCellSpan(child, workRows.size(), workCols.size());
            const Size d = child->DesiredSize();
            if (cell.rowSpan > 1u) {
                DistributeMultiSpanArrangeAutoExcess(
                    workRows, &RowDefinition::computedHeight, cell.row, cell.rowSpan, d.height);
            }
            if (cell.colSpan > 1u) {
                DistributeMultiSpanArrangeAutoExcess(
                    workCols, &ColumnDefinition::computedWidth, cell.col, cell.colSpan, d.width);
            }
        }
    }

    for (const auto& c : workCols) {
        if (c.unit == GridUnitType::Auto) {
            fixedW += c.computedWidth;
        }
    }
    for (const auto& r : workRows) {
        if (r.unit == GridUnitType::Auto) {
            fixedH += r.computedHeight;
        }
    }

    const float remW = std::max(0.0f, innerW - fixedW);
    const float remH = std::max(0.0f, innerH - fixedH);

    float offsetX = startX;
    for (auto& c : workCols) {
        if (c.unit == GridUnitType::Star) {
            c.computedWidth = (totalStarW > 0.0f) ? (remW * (c.value / totalStarW)) : 0.0f;
        }
        c.offsetX = offsetX;
        offsetX += c.computedWidth;
    }

    float offsetY = startY;
    for (auto& r : workRows) {
        if (r.unit == GridUnitType::Star) {
            r.computedHeight = (totalStarH > 0.0f) ? (remH * (r.value / totalStarH)) : 0.0f;
        }
        r.offsetY = offsetY;
        offsetY += r.computedHeight;
    }

    for (std::size_t i = 0u; i < cols_.size() && i < workCols.size(); ++i) {
        cols_[i].computedWidth = workCols[i].computedWidth;
        cols_[i].offsetX = workCols[i].offsetX;
    }
    for (std::size_t i = 0u; i < rows_.size() && i < workRows.size(); ++i) {
        rows_[i].computedHeight = workRows[i].computedHeight;
        rows_[i].offsetY = workRows[i].offsetY;
    }

    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        if (detail::IsFloatingPopupOverlay(child)) {
            child->Arrange(finalRect);
            continue;
        }
        const GridCellSpan cell = ResolveGridCellSpan(child, workRows.size(), workCols.size());
        float spanW = 0.0f;
        for (std::size_t sc = 0u; sc < cell.colSpan; ++sc) {
            spanW += workCols[cell.col + sc].computedWidth;
        }
        float spanH = 0.0f;
        for (std::size_t sr = 0u; sr < cell.rowSpan; ++sr) {
            spanH += workRows[cell.row + sr].computedHeight;
        }
        child->Arrange(Rect{
            workCols[cell.col].offsetX,
            workRows[cell.row].offsetY,
            spanW,
            spanH});
    }
}

// ============================================================================
// Canvas
// ============================================================================

Canvas::Canvas() {
    EnsureControlTypesRegistered();
}

void Canvas::SetLeft(UIElement* element, float left) {
    if (element) {
        element->SetValue(PropId_CanvasLeft, MakeFloat(left), ValueSourceRank::Local);
    }
}

float Canvas::GetLeft(const UIElement* element) noexcept {
    if (!element) {
        return 0.0f;
    }
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasLeft), out)) {
        return out;
    }
    return 0.0f;
}

void Canvas::SetTop(UIElement* element, float top) {
    if (element) {
        element->SetValue(PropId_CanvasTop, MakeFloat(top), ValueSourceRank::Local);
    }
}

float Canvas::GetTop(const UIElement* element) noexcept {
    if (!element) {
        return 0.0f;
    }
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasTop), out)) {
        return out;
    }
    return 0.0f;
}

void Canvas::SetRight(UIElement* element, float right) {
    if (element) {
        element->SetValue(PropId_CanvasRight, MakeFloat(right), ValueSourceRank::Local);
    }
}

float Canvas::GetRight(const UIElement* element) noexcept {
    if (!element) {
        return 0.0f;
    }
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasRight), out)) {
        return out;
    }
    return 0.0f;
}

void Canvas::SetBottom(UIElement* element, float bottom) {
    if (element) {
        element->SetValue(PropId_CanvasBottom, MakeFloat(bottom), ValueSourceRank::Local);
    }
}

float Canvas::GetBottom(const UIElement* element) noexcept {
    if (!element) {
        return 0.0f;
    }
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasBottom), out)) {
        return out;
    }
    return 0.0f;
}

Size Canvas::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(Size{detail::kUnconstrainedSpace, detail::kUnconstrainedSpace});
    }
    return Size{0.0f, 0.0f};
}

void Canvas::ArrangeCore(Rect finalRect) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const Size d = c->DesiredSize();
        float x = finalRect.x;
        if (c->GetPropertySourceRank(PropId_CanvasLeft) != ValueSourceRank::Default) {
            x = finalRect.x + GetLeft(c);
        } else if (c->GetPropertySourceRank(PropId_CanvasRight) != ValueSourceRank::Default) {
            x = finalRect.x + finalRect.width - d.width - GetRight(c);
        }
        float y = finalRect.y;
        if (c->GetPropertySourceRank(PropId_CanvasTop) != ValueSourceRank::Default) {
            y = finalRect.y + GetTop(c);
        } else if (c->GetPropertySourceRank(PropId_CanvasBottom) != ValueSourceRank::Default) {
            y = finalRect.y + finalRect.height - d.height - GetBottom(c);
        }
        c->Arrange(Rect{x, y, d.width, d.height});
    }
}

// ============================================================================
// UniformGrid
// ============================================================================

UniformGrid::UniformGrid() {
    EnsureControlTypesRegistered();
}

void UniformGrid::SetRows(std::int32_t rows) {
    SetValue(PropId_UniformRows, value::FromInt(std::max<std::int32_t>(0, rows)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetRows() const noexcept {
    return std::max<std::int32_t>(0, detail::CoerceInt32Property(GetValue(PropId_UniformRows), 0));
}

void UniformGrid::SetColumns(std::int32_t cols) {
    SetValue(PropId_UniformColumns, value::FromInt(std::max<std::int32_t>(0, cols)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetColumns() const noexcept {
    return std::max<std::int32_t>(0, detail::CoerceInt32Property(GetValue(PropId_UniformColumns), 0));
}

void UniformGrid::SetFirstColumn(std::int32_t firstCol) {
    SetValue(PropId_FirstColumn, value::FromInt(std::max<std::int32_t>(0, firstCol)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetFirstColumn() const noexcept {
    return std::max<std::int32_t>(0, detail::CoerceInt32Property(GetValue(PropId_FirstColumn), 0));
}

void UniformGrid::ComputeEffectiveGridDimensions(
    std::size_t visibleCount,
    std::int32_t& outRows,
    std::int32_t& outCols,
    std::int32_t& outFirstCol) const noexcept {
    std::int32_t rows = GetRows();
    std::int32_t cols = GetColumns();
    std::int32_t firstCol = GetFirstColumn();

    if (cols > 0 && firstCol >= cols) {
        firstCol = 0;
    }

    if (visibleCount == 0u) {
        outRows = std::max<std::int32_t>(1, rows);
        outCols = std::max<std::int32_t>(1, cols);
        outFirstCol = 0;
        return;
    }

    const std::size_t totalSlots = visibleCount + static_cast<std::size_t>(firstCol);
    if (rows == 0 || cols == 0) {
        if (cols == 0) {
            if (rows > 0) {
                cols = static_cast<std::int32_t>(
                    (totalSlots + static_cast<std::size_t>(rows) - 1u) / static_cast<std::size_t>(rows));
            } else {
                cols = static_cast<std::int32_t>(
                    std::ceil(std::sqrt(static_cast<float>(totalSlots))));
                cols = std::max<std::int32_t>(1, cols);
            }
        }
        if (firstCol >= cols) {
            firstCol = 0;
        }
        const std::size_t adjustedSlots = visibleCount + static_cast<std::size_t>(firstCol);
        if (rows == 0) {
            rows = static_cast<std::int32_t>(
                (adjustedSlots + static_cast<std::size_t>(cols) - 1u) / static_cast<std::size_t>(cols));
        }
    }

    outRows = std::max<std::int32_t>(1, rows);
    outCols = std::max<std::int32_t>(1, cols);
    outFirstCol = std::clamp<std::int32_t>(firstCol, 0, outCols - 1);
}

Size UniformGrid::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;

    std::size_t visibleCount = 0u;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            ++visibleCount;
        }
    }

    std::int32_t effRows = 1;
    std::int32_t effCols = 1;
    std::int32_t effFirstCol = 0;
    ComputeEffectiveGridDimensions(visibleCount, effRows, effCols, effFirstCol);

    const Size cellAvail{
        std::max(0.0f, (availableSize.width - padW) / static_cast<float>(effCols)),
        std::max(0.0f, (availableSize.height - padH) / static_cast<float>(effRows))};

    float maxChildW = 0.0f;
    float maxChildH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(cellAvail);
        if (c->GetVisibility() != Visibility::Collapsed) {
            maxChildW = std::max(maxChildW, c->DesiredSize().width);
            maxChildH = std::max(maxChildH, c->DesiredSize().height);
        }
    }

    return Size{
        maxChildW * static_cast<float>(effCols) + padW,
        maxChildH * static_cast<float>(effRows) + padH};
}

void UniformGrid::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    std::size_t visibleCount = 0u;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            ++visibleCount;
        }
    }

    std::int32_t effRows = 1;
    std::int32_t effCols = 1;
    std::int32_t effFirstCol = 0;
    ComputeEffectiveGridDimensions(visibleCount, effRows, effCols, effFirstCol);

    const float cellW = innerW / static_cast<float>(effCols);
    const float cellH = innerH / static_cast<float>(effRows);

    std::int32_t slotIdx = effFirstCol;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
            continue;
        }
        const std::int32_t row = slotIdx / effCols;
        const std::int32_t col = slotIdx % effCols;
        if (row < effRows) {
            c->Arrange(Rect{
                startX + static_cast<float>(col) * cellW,
                startY + static_cast<float>(row) * cellH,
                cellW,
                cellH});
        } else {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
        }
        ++slotIdx;
    }
}

// ============================================================================
// ScrollViewer
// ============================================================================

ScrollViewer::ScrollViewer() {
    EnsureControlTypesRegistered();
    SetValue(PropId_ClipToBounds, UnifiedValue{true}, ValueSourceRank::ThemeStyle);
}

void ScrollViewer::SetHorizontalScrollBarVisibility(ScrollBarVisibility vis) {
    SetValue(
        PropId_HorizontalScrollBarVisibility,
        value::FromInt(static_cast<std::int64_t>(vis)),
        ValueSourceRank::Local);
}

ScrollBarVisibility ScrollViewer::GetHorizontalScrollBarVisibility() const noexcept {
    const auto v = GetValue(PropId_HorizontalScrollBarVisibility);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<ScrollBarVisibility>(*i);
        }
    }
    return ScrollBarVisibility::Disabled;
}

void ScrollViewer::SetVerticalScrollBarVisibility(ScrollBarVisibility vis) {
    SetValue(
        PropId_VerticalScrollBarVisibility,
        value::FromInt(static_cast<std::int64_t>(vis)),
        ValueSourceRank::Local);
}

ScrollBarVisibility ScrollViewer::GetVerticalScrollBarVisibility() const noexcept {
    const auto v = GetValue(PropId_VerticalScrollBarVisibility);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<ScrollBarVisibility>(*i);
        }
    }
    return ScrollBarVisibility::Auto;
}

void ScrollViewer::SetHorizontalOffset(float offset) {
    const float target = hasValidViewport_
        ? std::clamp(offset, 0.0f, ScrollableWidth())
        : std::max(0.0f, offset);
    SetValue(PropId_HorizontalOffset, MakeFloat(target), ValueSourceRank::Local);
}

float ScrollViewer::GetHorizontalOffset() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_HorizontalOffset), out)) {
        return hasValidViewport_
            ? std::clamp(out, 0.0f, ScrollableWidth())
            : std::max(0.0f, out);
    }
    return 0.0f;
}

void ScrollViewer::SetVerticalOffset(float offset) {
    const float target = hasValidViewport_
        ? std::clamp(offset, 0.0f, ScrollableHeight())
        : std::max(0.0f, offset);
    SetValue(PropId_VerticalOffset, MakeFloat(target), ValueSourceRank::Local);
}

float ScrollViewer::GetVerticalOffset() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_VerticalOffset), out)) {
        return hasValidViewport_
            ? std::clamp(out, 0.0f, ScrollableHeight())
            : std::max(0.0f, out);
    }
    return 0.0f;
}

void ScrollViewer::ScrollToHorizontalOffset(float offset) {
    SetHorizontalOffset(offset);
}

void ScrollViewer::ScrollToVerticalOffset(float offset) {
    SetVerticalOffset(offset);
}

void ScrollViewer::ScrollBy(float deltaX, float deltaY) {
    if (deltaX != 0.0f) {
        ScrollToHorizontalOffset(GetHorizontalOffset() + deltaX);
    }
    if (deltaY != 0.0f) {
        ScrollToVerticalOffset(GetVerticalOffset() + deltaY);
    }
}

bool ScrollViewer::ShouldSkipControlTemplatePackage(const AxbPackage& package) const noexcept {
    if (FirstChild() != nullptr) {
        return true;
    }
    for (const std::string& s : package.stringTable) {
        if (s == "ScrollContentPresenter" || s == "PART_ScrollContentPresenter") {
            return true;
        }
    }
    return false;
}

bool ScrollViewer::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const bool allowH = (GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const bool allowV = (GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const float pageW = std::max(48.0f, viewportSize_.width * 0.85f);
    const float pageH = std::max(48.0f, viewportSize_.height * 0.85f);
    const float prevX = GetHorizontalOffset();
    const float prevY = GetVerticalOffset();
    switch (key) {
        case KeyCode::Up:
            if (!allowV) return false;
            ScrollBy(0.0f, -24.0f);
            break;
        case KeyCode::Down:
            if (!allowV) return false;
            ScrollBy(0.0f, 24.0f);
            break;
        case KeyCode::Left:
            if (!allowH) return false;
            ScrollBy(-24.0f, 0.0f);
            break;
        case KeyCode::Right:
            if (!allowH) return false;
            ScrollBy(24.0f, 0.0f);
            break;
        case KeyCode::PageUp:
            if (!allowV) return false;
            ScrollBy(0.0f, -pageH);
            break;
        case KeyCode::PageDown:
            if (!allowV) return false;
            ScrollBy(0.0f, pageH);
            break;
        case KeyCode::Home:
            if (allowV) ScrollToVerticalOffset(0.0f);
            else if (allowH) ScrollToHorizontalOffset(0.0f);
            else return false;
            break;
        case KeyCode::End:
            if (allowV) ScrollToVerticalOffset(std::max(0.0f, extentSize_.height - viewportSize_.height));
            else if (allowH) ScrollToHorizontalOffset(std::max(0.0f, extentSize_.width - viewportSize_.width));
            else return false;
            break;
        default:
            (void)pageW;
            return false;
    }
    return std::fabs(GetHorizontalOffset() - prevX) > 1e-4f ||
           std::fabs(GetVerticalOffset() - prevY) > 1e-4f;
}

Size ScrollViewer::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float viewportAvailW = std::max(0.0f, availableSize.width - padW);
    const float viewportAvailH = std::max(0.0f, availableSize.height - padH);

    const bool canScrollH = (GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const bool canScrollV = (GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);

    const Size childConstraint{
        canScrollH ? detail::kUnconstrainedSpace : viewportAvailW,
        canScrollV ? detail::kUnconstrainedSpace : viewportAvailH};

    float maxChildW = 0.0f;
    float maxChildH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(childConstraint);
        if (c->GetVisibility() != Visibility::Collapsed) {
            maxChildW = std::max(maxChildW, c->DesiredSize().width);
            maxChildH = std::max(maxChildH, c->DesiredSize().height);
        }
    }

    extentSize_ = Size{maxChildW, maxChildH};
    return Size{
        std::min(maxChildW, viewportAvailW) + padW,
        std::min(maxChildH, viewportAvailH) + padH};
}

void ScrollViewer::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float viewportW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float viewportH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    viewportSize_ = Size{viewportW, viewportH};
    hasValidViewport_ = true;

    float maxChildW = 0.0f;
    float maxChildH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            maxChildW = std::max(maxChildW, c->DesiredSize().width);
            maxChildH = std::max(maxChildH, c->DesiredSize().height);
        }
    }
    extentSize_ = Size{maxChildW, maxChildH};

    const bool canScrollH = (GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const bool canScrollV = (GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const float maxScrollX = std::max(0.0f, extentSize_.width - viewportW);
    const float maxScrollY = std::max(0.0f, extentSize_.height - viewportH);

    float rawX = 0.0f;
    float rawY = 0.0f;
    const bool hasRawX = TryGetFloat(UIElement::GetValue(PropId_HorizontalOffset), rawX);
    const bool hasRawY = TryGetFloat(UIElement::GetValue(PropId_VerticalOffset), rawY);
    const float clampedX = std::clamp(hasRawX ? rawX : 0.0f, 0.0f, maxScrollX);
    const float clampedY = std::clamp(hasRawY ? rawY : 0.0f, 0.0f, maxScrollY);
    if (hasRawX && rawX != clampedX) {
        CoerceSparseFloatInPlace(PropId_HorizontalOffset, clampedX);
    }
    if (hasRawY && rawY != clampedY) {
        CoerceSparseFloatInPlace(PropId_VerticalOffset, clampedY);
    }

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
            continue;
        }
        const Size d = c->DesiredSize();
        const float slotW = (canScrollH || d.width > viewportW) ? std::max(viewportW, d.width) : viewportW;
        const float slotH = (canScrollV || d.height > viewportH) ? std::max(viewportH, d.height) : viewportH;
        c->Arrange(Rect{
            startX - clampedX,
            startY - clampedY,
            slotW,
            slotH});
    }
}

// ============================================================================
// Popup
// ============================================================================

Popup::Popup() {
    EnsureControlTypesRegistered();
}

void Popup::SetIsOpen(bool open) {
    SetValue(PropId_IsOpen, value::FromBool(open), ValueSourceRank::Local);
}

bool Popup::IsOpen() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsOpen), false);
}

void Popup::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsOpen) {
        InvalidateLayout();
        detail::MarkSubtreeRenderDirtyRecursive(this);
    }
}

Size Popup::MeasureCore(Size availableSize) noexcept {
    if (!IsOpen()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Measure(Size{0.0f, 0.0f});
        }
        return Size{0.0f, 0.0f};
    }
    return Control::MeasureCore(availableSize);
}

void Popup::ArrangeCore(Rect finalRect) noexcept {
    if (!IsOpen()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            c->Arrange(Rect{finalRect.x, finalRect.y, 0.0f, 0.0f});
        }
        return;
    }
    Control::ArrangeCore(finalRect);
}

// ============================================================================
// ToolTip
// ============================================================================

ToolTip::ToolTip() {
    EnsureControlTypesRegistered();
    SetIsHitTestVisible(false);
}

void ToolTip::SetIsOpen(bool open) {
    SetValue(PropId_IsOpen, value::FromBool(open), ValueSourceRank::Local);
}

bool ToolTip::IsOpen() const noexcept {
    return detail::CoerceBoolProperty(GetValue(PropId_IsOpen), false);
}

void ToolTip::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ContentControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsOpen) {
        InvalidateLayout();
        detail::MarkSubtreeRenderDirtyRecursive(this);
    }
    if ((propId == PropId_Content || propId == PropId_Text) && Parent()) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            Parent()->SetValue(PropId_ToolTipText, value::FromString(sv->str()), ValueSourceRank::Local);
        }
    }
}

Size ToolTip::MeasureCore(Size availableSize) noexcept {
    if (IsOpen()) {
        return ContentControl::MeasureCore(availableSize);
    }
    if (Parent()) {
        const UnifiedValue cv = GetValue(PropId_Content);
        if (const auto* sv = std::get_if<StringValue>(&cv)) {
            Parent()->SetValue(PropId_ToolTipText, value::FromString(sv->str()), ValueSourceRank::Default);
        } else if (const auto* tb = FirstChild() ? FirstChild()->DynamicCast<TextBlock>() : nullptr) {
            Parent()->SetValue(PropId_ToolTipText, value::FromString(tb->GetText()), ValueSourceRank::Default);
        } else if (const auto* lbl = FirstChild() ? FirstChild()->DynamicCast<Label>() : nullptr) {
            Parent()->SetValue(PropId_ToolTipText, value::FromString(lbl->GetText()), ValueSourceRank::Default);
        }
    }
    return Size{0.0f, 0.0f};
}

void ToolTip::ArrangeCore(Rect finalRect) noexcept {
    if (IsOpen()) {
        ContentControl::ArrangeCore(finalRect);
        return;
    }
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{0.0f, 0.0f, 0.0f, 0.0f});
    }
}

// ============================================================================
// Shape
// ============================================================================

Shape::Shape() {
    EnsureControlTypesRegistered();
}

void Shape::SetFill(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Shape::GetFill() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Shape::HasFill() const noexcept {
    if (GetFillGradientBrushId() != 0u) {
        return true;
    }
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return c->a > 0.0f;
    }
    return false;
}

void Shape::SetFillGradientBrushId(std::uint32_t brushId) {
    if (brushId == 0u) {
        (void)ClearValue(PropId_BackgroundGradientId, ValueSourceRank::Local);
    } else {
        SetValue(
            PropId_BackgroundGradientId,
            value::FromInt(static_cast<std::int64_t>(brushId)),
            ValueSourceRank::Local);
    }
}

std::uint32_t Shape::GetFillGradientBrushId() const noexcept {
    return detail::ExtractGradientBrushId(GetValue(PropId_BackgroundGradientId));
}

void Shape::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Shape::GetStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Shape::HasStroke() const noexcept {
    if (GetStrokeGradientBrushId() != 0u) {
        return true;
    }
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return c->a > 0.0f;
    }
    return false;
}

std::uint32_t Shape::GetStrokeGradientBrushId() const noexcept {
    return detail::ExtractGradientBrushId(GetValue(PropId_BorderGradientId));
}

void Shape::SetStrokeThickness(float thickness) {
    SetValue(PropId_StrokeThickness, MakeFloat(std::max(0.0f, thickness)), ValueSourceRank::Local);
}

float Shape::GetStrokeThickness() const noexcept {
    float st = 0.0f;
    if (TryGetFloat(GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    return HasStroke() ? 1.0f : 0.0f;
}

void Shape::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Shape::GetStretch() const noexcept {
    return ReadStretchProperty(*this, Stretch::Fill);
}

// ============================================================================
// Rectangle
// ============================================================================

Rectangle::Rectangle() {
    EnsureControlTypesRegistered();
}

void Rectangle::SetRadiusX(float rx) {
    SetValue(PropId_RadiusX, MakeFloat(std::max(0.0f, rx)), ValueSourceRank::Local);
}

float Rectangle::GetRadiusX() const noexcept {
    float rx = 0.0f;
    if (TryGetFloat(GetValue(PropId_RadiusX), rx)) {
        return std::max(0.0f, rx);
    }
    if (auto cr = value::TryGetCornerRadius(GetValue(PropId_CornerRadius))) {
        return std::max(0.0f, cr->topLeft);
    }
    return 0.0f;
}

void Rectangle::SetRadiusY(float ry) {
    SetValue(PropId_RadiusY, MakeFloat(std::max(0.0f, ry)), ValueSourceRank::Local);
}

float Rectangle::GetRadiusY() const noexcept {
    float ry = 0.0f;
    if (TryGetFloat(GetValue(PropId_RadiusY), ry)) {
        return std::max(0.0f, ry);
    }
    if (auto cr = value::TryGetCornerRadius(GetValue(PropId_CornerRadius))) {
        return std::max(0.0f, cr->topLeft);
    }
    return 0.0f;
}

float Rectangle::EffectiveCornerRadius() const noexcept {
    float rx = 0.0f;
    float ry = 0.0f;
    const bool hasRx = TryGetFloat(GetValue(PropId_RadiusX), rx);
    const bool hasRy = TryGetFloat(GetValue(PropId_RadiusY), ry);
    if (hasRx && hasRy) {
        if (rx <= 0.0f || ry <= 0.0f) {
            return 0.0f;
        }
        return 0.5f * (rx + ry);
    }
    if (hasRx) {
        return std::max(0.0f, rx);
    }
    if (hasRy) {
        return std::max(0.0f, ry);
    }
    if (auto cr = value::TryGetCornerRadius(GetValue(PropId_CornerRadius))) {
        return std::max(0.0f, cr->topLeft);
    }
    return 0.0f;
}

Size Rectangle::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    return Size{0.0f, 0.0f};
}

// ============================================================================
// Ellipse
// ============================================================================

Ellipse::Ellipse() {
    EnsureControlTypesRegistered();
}

Size Ellipse::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    return Size{0.0f, 0.0f};
}

// ============================================================================
// Line
// ============================================================================

Line::Line() {
    EnsureControlTypesRegistered();
}

void Line::SetX1(float x1) {
    SetValue(PropId_LineX1, MakeFloat(x1), ValueSourceRank::Local);
}

float Line::GetX1() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_LineX1), v) ? v : 0.0f;
}

void Line::SetY1(float y1) {
    SetValue(PropId_LineY1, MakeFloat(y1), ValueSourceRank::Local);
}

float Line::GetY1() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_LineY1), v) ? v : 0.0f;
}

void Line::SetX2(float x2) {
    SetValue(PropId_LineX2, MakeFloat(x2), ValueSourceRank::Local);
}

float Line::GetX2() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_LineX2), v) ? v : 0.0f;
}

void Line::SetY2(float y2) {
    SetValue(PropId_LineY2, MakeFloat(y2), ValueSourceRank::Local);
}

float Line::GetY2() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_LineY2), v) ? v : 0.0f;
}

Color Line::GetStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    if (auto bg = value::TryGetColor(GetValue(PropId_Background))) {
        return *bg;
    }
    if (auto fg = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *fg;
    }
    return Color{0.92f, 0.95f, 1.0f, 1.0f};
}

bool Line::HasStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return c->a > 0.0f;
    }
    if (auto bg = value::TryGetColor(GetValue(PropId_Background))) {
        return bg->a > 0.0f;
    }
    if (auto fg = value::TryGetColor(GetValue(PropId_Foreground))) {
        return fg->a > 0.0f;
    }
    return false;
}

float Line::GetStrokeThickness() const noexcept {
    float st = 0.0f;
    if (TryGetFloat(GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    return 1.0f;
}

Size Line::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const float halfStroke = GetStrokeThickness() * 0.5f;
    const float maxX = std::max(0.0f, std::max(GetX1(), GetX2()) + halfStroke);
    const float maxY = std::max(0.0f, std::max(GetY1(), GetY2()) + halfStroke);
    return Size{maxX, maxY};
}

// ============================================================================
// Path
// ============================================================================

Path::Path() {
    EnsureControlTypesRegistered();
}

void Path::SetData(std::string_view svgPathData) {
    WriteSvgPathData(*this, svgPathData);
}

std::string Path::GetData() const {
    return ReadSvgPathData(*this);
}

void Path::SetFill(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

bool Path::HasExplicitFill() const noexcept {
    if (detail::ExtractGradientBrushId(GetValue(PropId_BackgroundGradientId)) != 0u) {
        return true;
    }
    return value::TryGetColor(GetValue(PropId_Background)).has_value() ||
           value::TryGetColor(GetValue(PropId_Foreground)).has_value();
}

Color Path::GetFill() const noexcept {
    float bgOp = 1.0f;
    (void)TryGetFloat(GetValue(PropId_BackgroundOpacity), bgOp);
    bgOp = std::clamp(bgOp, 0.0f, 1.0f);
    if (detail::ExtractGradientBrushId(GetValue(PropId_BackgroundGradientId)) != 0u) {
        return Color{1.0f, 1.0f, 1.0f, bgOp};
    }
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        Color out = *c;
        out.a = std::clamp(out.a * bgOp, 0.0f, 1.0f);
        return out;
    }
    if (auto fg = value::TryGetColor(GetValue(PropId_Foreground))) {
        Color out = *fg;
        out.a = std::clamp(out.a * bgOp, 0.0f, 1.0f);
        return out;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Path::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

bool Path::HasExplicitStroke() const noexcept {
    return HasExplicitSvgStroke(*this);
}

Color Path::GetStroke() const noexcept {
    float brdOp = 1.0f;
    (void)TryGetFloat(GetValue(PropId_BorderOpacity), brdOp);
    brdOp = std::clamp(brdOp, 0.0f, 1.0f);
    if (detail::ExtractGradientBrushId(GetValue(PropId_BorderGradientId)) != 0u) {
        return Color{1.0f, 1.0f, 1.0f, brdOp};
    }
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        Color out = *c;
        out.a = std::clamp(out.a * brdOp, 0.0f, 1.0f);
        return out;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Path::SetStrokeThickness(float thickness) {
    SetValue(PropId_StrokeThickness, MakeFloat(thickness), ValueSourceRank::Local);
}

float Path::GetStrokeThickness() const noexcept {
    return ResolveSvgStrokeThickness(*this, false);
}

void Path::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Path::GetStretch() const noexcept {
    return ReadStretchProperty(*this, Stretch::None);
}

void Path::SetTrimStart(float trimStart) {
    SetValue(PropId_PathTrimStart, MakeFloat(trimStart), ValueSourceRank::Local);
}

float Path::GetTrimStart() const noexcept {
    float v = 0.0f;
    (void)TryGetFloat(GetValue(PropId_PathTrimStart), v);
    return v;
}

void Path::SetTrimEnd(float trimEnd) {
    SetValue(PropId_PathTrimEnd, MakeFloat(trimEnd), ValueSourceRank::Local);
}

float Path::GetTrimEnd() const noexcept {
    float v = 1.0f;
    (void)TryGetFloat(GetValue(PropId_PathTrimEnd), v);
    return v;
}

Size Path::MeasureCore(Size availableSize) noexcept {
    const std::string data = GetData();
    return MeasureSvgPathCore(*this, data, GetStretch(), availableSize);
}

// ============================================================================
// Polygon & Polyline
// ============================================================================

Polygon::Polygon() {
    EnsureControlTypesRegistered();
}

void Polygon::SetPoints(std::string_view pointsText) {
    WriteShapePoints(*this, pointsText);
}

std::string Polygon::GetPoints() const {
    return ReadShapePoints(*this);
}

void Polygon::SetData(std::string_view svgPathData) {
    WriteSvgPathData(*this, svgPathData);
}

std::string Polygon::GetData() const {
    return ReadSvgPathData(*this);
}

float Polygon::GetStrokeThickness() const noexcept {
    return ResolveSvgStrokeThickness(*this, true);
}

Stretch Polygon::GetStretch() const noexcept {
    return ReadStretchProperty(*this, Stretch::None);
}

void Polygon::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Shape::OnPropertyChanged(propId, newValue);
    HandleShapePointsPropertyChanged(*this, propId, newValue, true);
}

Size Polygon::MeasureCore(Size availableSize) noexcept {
    const std::string data = GetData();
    return MeasureSvgPathCore(*this, data, GetStretch(), availableSize);
}

Polyline::Polyline() {
    EnsureControlTypesRegistered();
}

void Polyline::SetPoints(std::string_view pointsText) {
    WriteShapePoints(*this, pointsText);
}

std::string Polyline::GetPoints() const {
    return ReadShapePoints(*this);
}

void Polyline::SetData(std::string_view svgPathData) {
    WriteSvgPathData(*this, svgPathData);
}

std::string Polyline::GetData() const {
    return ReadSvgPathData(*this);
}

float Polyline::GetStrokeThickness() const noexcept {
    return ResolveSvgStrokeThickness(*this, true);
}

Stretch Polyline::GetStretch() const noexcept {
    return ReadStretchProperty(*this, Stretch::None);
}

void Polyline::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Shape::OnPropertyChanged(propId, newValue);
    HandleShapePointsPropertyChanged(*this, propId, newValue, false);
}

Size Polyline::MeasureCore(Size availableSize) noexcept {
    const std::string data = GetData();
    return MeasureSvgPathCore(*this, data, GetStretch(), availableSize);
}

bool ScrollViewer::TryHandleMouseWheelScroll(float scrollDeltaX, float scrollDeltaY, bool shiftHeld) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const bool allowH = (GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const bool allowV = (GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    float effDx = allowH ? scrollDeltaX : 0.0f;
    float effDy = allowV ? scrollDeltaY : 0.0f;
    if (shiftHeld && scrollDeltaX == 0.0f && scrollDeltaY != 0.0f && allowH) {
        effDx = scrollDeltaY;
        effDy = 0.0f;
    }
    if (effDx == 0.0f && effDy == 0.0f) {
        return false;
    }
    const float beforeX = GetHorizontalOffset();
    const float beforeY = GetVerticalOffset();
    ScrollBy(effDx, effDy);
    const float afterX = GetHorizontalOffset();
    const float afterY = GetVerticalOffset();
    return std::fabs(afterX - beforeX) > 1e-4f || std::fabs(afterY - beforeY) > 1e-4f;
}

} // namespace aero
