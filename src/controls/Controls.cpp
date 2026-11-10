#include "aero/controls/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aero {

namespace {

TypeClass gStackPanelTypeClass{};
TypeClass gDockPanelTypeClass{};
TypeClass gWrapPanelTypeClass{};
TypeClass gViewboxTypeClass{};
TypeClass gScrollViewerTypeClass{};
TypeClass gGridTypeClass{};
TypeClass gCanvasTypeClass{};
TypeClass gBorderTypeClass{};
TypeClass gTextBlockTypeClass{};
TypeClass gPathTypeClass{};
TypeClass gImageTypeClass{};
TypeClass gItemsControlTypeClass{};
TypeClass gUserControlTypeClass{};
bool gControlTypesInitialized = false;

constexpr float kUnconstrainedSpace = 1000000.0f;

std::string_view StripNamespacePrefix(std::string_view name) noexcept {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1);
    }
    if (const auto dot = name.rfind('.'); dot != std::string_view::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}

void MarkSubtreeRenderDirtyRecursive(UIElement* elem) noexcept {
    if (!elem) {
        return;
    }
    elem->InvalidateRender();
    for (UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        MarkSubtreeRenderDirtyRecursive(c);
    }
}

void ComputeViewboxScaleFactors(
    float contentW,
    float contentH,
    float availW,
    float availH,
    bool hasW,
    bool hasH,
    Stretch stretch,
    StretchDirection dir,
    float& outSx,
    float& outSy) noexcept {
    outSx = 1.0f;
    outSy = 1.0f;
    if (contentW <= 1e-5f || contentH <= 1e-5f || stretch == Stretch::None) {
        return;
    }

    switch (stretch) {
        case Stretch::None:
            outSx = 1.0f;
            outSy = 1.0f;
            break;
        case Stretch::Fill:
            outSx = hasW ? (availW / contentW) : 1.0f;
            outSy = hasH ? (availH / contentH) : 1.0f;
            break;
        case Stretch::Uniform: {
            float s = 1.0f;
            if (hasW && hasH) {
                s = std::min(availW / contentW, availH / contentH);
            } else if (hasW) {
                s = availW / contentW;
            } else if (hasH) {
                s = availH / contentH;
            }
            outSx = s;
            outSy = s;
            break;
        }
        case Stretch::UniformToFill: {
            float s = 1.0f;
            if (hasW && hasH) {
                s = std::max(availW / contentW, availH / contentH);
            } else if (hasW) {
                s = availW / contentW;
            } else if (hasH) {
                s = availH / contentH;
            }
            outSx = s;
            outSy = s;
            break;
        }
    }

    outSx = std::max(0.0f, outSx);
    outSy = std::max(0.0f, outSy);

    if (dir == StretchDirection::UpOnly) {
        outSx = std::max(1.0f, outSx);
        outSy = std::max(1.0f, outSy);
    } else if (dir == StretchDirection::DownOnly) {
        outSx = std::min(1.0f, outSx);
        outSy = std::min(1.0f, outSy);
    }
}

} // namespace

void EnsureControlTypesRegistered() noexcept {
    EnsureBuiltinTypesRegistered();
    if (gControlTypesInitialized) {
        return;
    }
    gControlTypesInitialized = true;
    auto& reg = TypeRegistry::Instance();
    const auto* uiElem = UIElement::StaticTypeClass();
    const auto* panel = Panel::StaticTypeClass();

    reg.RegisterType(gStackPanelTypeClass, "Aero.UI.StackPanel", panel);
    reg.RegisterType(gDockPanelTypeClass, "Aero.UI.DockPanel", panel);
    reg.RegisterType(gWrapPanelTypeClass, "Aero.UI.WrapPanel", panel);
    reg.RegisterType(gViewboxTypeClass, "Aero.UI.Viewbox", panel);
    reg.RegisterType(gScrollViewerTypeClass, "Aero.UI.ScrollViewer", panel);
    reg.RegisterType(gGridTypeClass, "Aero.UI.Grid", panel);
    reg.RegisterType(gCanvasTypeClass, "Aero.UI.Canvas", panel);
    reg.RegisterType(gBorderTypeClass, "Aero.UI.Border", panel);
    reg.RegisterType(gTextBlockTypeClass, "Aero.UI.TextBlock", uiElem);
    reg.RegisterType(gPathTypeClass, "Aero.UI.Path", uiElem);
    reg.RegisterType(gImageTypeClass, "Aero.UI.Image", uiElem);
    reg.RegisterType(gItemsControlTypeClass, "Aero.UI.ItemsControl", panel);
    reg.RegisterType(gUserControlTypeClass, "Aero.UI.UserControl", panel);
}

const TypeClass* StackPanel::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gStackPanelTypeClass;
}

const TypeClass* DockPanel::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gDockPanelTypeClass;
}

const TypeClass* WrapPanel::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gWrapPanelTypeClass;
}

const TypeClass* Viewbox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gViewboxTypeClass;
}

const TypeClass* ScrollViewer::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gScrollViewerTypeClass;
}

const TypeClass* Grid::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gGridTypeClass;
}

const TypeClass* Canvas::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gCanvasTypeClass;
}

const TypeClass* Border::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gBorderTypeClass;
}

const TypeClass* TextBlock::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gTextBlockTypeClass;
}

const TypeClass* Path::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gPathTypeClass;
}

const TypeClass* Image::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gImageTypeClass;
}

const TypeClass* ItemsControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gItemsControlTypeClass;
}

const TypeClass* UserControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gUserControlTypeClass;
}

// ============================================================================
// StackPanel
// ============================================================================

StackPanel::StackPanel() {
    EnsureControlTypesRegistered();
}

void StackPanel::SetOrientation(Orientation o) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(o)),
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
    std::size_t visibleCount = 0;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
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

    if (visibleCount > 1) {
        accumMain += spacing * static_cast<float>(visibleCount - 1);
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
            if (c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    float accumW = 0.0f;
    float accumH = 0.0f;
    float parentW = 0.0f;
    float parentH = 0.0f;

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
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
            if (c->GetVisibility() != Visibility::Collapsed) {
                lastVisible = c;
            }
        }
    }

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
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

void WrapPanel::SetOrientation(Orientation o) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(o)),
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
// Viewbox
// ============================================================================

Viewbox::Viewbox() {
    EnsureControlTypesRegistered();
}

void Viewbox::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Viewbox::GetStretch() const noexcept {
    const auto v = GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

void Viewbox::SetStretchDirection(StretchDirection dir) {
    SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection Viewbox::GetStretchDirection() const noexcept {
    const auto v = GetValue(PropId_StretchDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 2) {
            return static_cast<StretchDirection>(*i);
        }
    }
    return StretchDirection::Both;
}

Size Viewbox::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* child = FirstChild();
    if (!child) {
        return Size{padW, padH};
    }

    // Measure single child in unconstrained natural design space
    child->Measure(Size{kUnconstrainedSpace, kUnconstrainedSpace});
    for (UIElement* extra = child->NextSibling(); extra != nullptr; extra = extra->NextSibling()) {
        extra->Measure(Size{kUnconstrainedSpace, kUnconstrainedSpace});
    }

    if (child->GetVisibility() == Visibility::Collapsed) {
        return Size{padW, padH};
    }

    const Size natural = child->DesiredSize();
    const bool hasW = std::isfinite(availableSize.width) && (availableSize.width < kUnconstrainedSpace * 0.5f);
    const bool hasH = std::isfinite(availableSize.height) && (availableSize.height < kUnconstrainedSpace * 0.5f);

    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScaleFactors(
        natural.width,
        natural.height,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    return Size{
        natural.width * sx + padW,
        natural.height * sy + padH};
}

void Viewbox::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* child = FirstChild();
    if (!child || child->GetVisibility() == Visibility::Collapsed) {
        const bool resetChanged =
            (std::fabs(childScaleX_ - 1.0f) > 1e-5f) ||
            (std::fabs(childScaleY_ - 1.0f) > 1e-5f) ||
            (std::fabs(childOriginX_ - startX) > 1e-3f) ||
            (std::fabs(childOriginY_ - startY) > 1e-3f);
        childScaleX_ = 1.0f;
        childScaleY_ = 1.0f;
        childOriginX_ = startX;
        childOriginY_ = startY;
        for (UIElement* c = child; c != nullptr; c = c->NextSibling()) {
            c->Arrange(Rect{startX, startY, 0.0f, 0.0f});
        }
        if (resetChanged) {
            InvalidateRender();
            for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
                MarkSubtreeRenderDirtyRecursive(c);
            }
        }
        return;
    }

    const Size natural = child->DesiredSize();
    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScaleFactors(
        natural.width,
        natural.height,
        innerW,
        innerH,
        true,
        true,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    const float scaledW = natural.width * sx;
    const float scaledH = natural.height * sy;

    float offsetX = (innerW - scaledW) * 0.5f;
    switch (child->GetHorizontalAlignment()) {
        case HorizontalAlignment::Left:
            offsetX = 0.0f;
            break;
        case HorizontalAlignment::Right:
            offsetX = innerW - scaledW;
            break;
        case HorizontalAlignment::Center:
        case HorizontalAlignment::Stretch:
            offsetX = (innerW - scaledW) * 0.5f;
            break;
    }

    float offsetY = (innerH - scaledH) * 0.5f;
    switch (child->GetVerticalAlignment()) {
        case VerticalAlignment::Top:
            offsetY = 0.0f;
            break;
        case VerticalAlignment::Bottom:
            offsetY = innerH - scaledH;
            break;
        case VerticalAlignment::Center:
        case VerticalAlignment::Stretch:
            offsetY = (innerH - scaledH) * 0.5f;
            break;
    }

    const float newOriginX = startX + offsetX;
    const float newOriginY = startY + offsetY;
    const bool scaleChanged =
        (std::fabs(childScaleX_ - sx) > 1e-5f) ||
        (std::fabs(childScaleY_ - sy) > 1e-5f) ||
        (std::fabs(childOriginX_ - newOriginX) > 1e-3f) ||
        (std::fabs(childOriginY_ - newOriginY) > 1e-3f);

    childScaleX_ = sx;
    childScaleY_ = sy;
    childOriginX_ = newOriginX;
    childOriginY_ = newOriginY;

    child->Arrange(Rect{newOriginX, newOriginY, natural.width, natural.height});
    for (UIElement* extra = child->NextSibling(); extra != nullptr; extra = extra->NextSibling()) {
        const Size ed = extra->DesiredSize();
        extra->Arrange(Rect{newOriginX, newOriginY, ed.width, ed.height});
    }

    if (scaleChanged) {
        InvalidateRender();
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            MarkSubtreeRenderDirtyRecursive(c);
        }
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

Size ScrollViewer::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float viewportAvailW = std::max(0.0f, availableSize.width - padW);
    const float viewportAvailH = std::max(0.0f, availableSize.height - padH);

    const bool canScrollH = (GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
    const bool canScrollV = (GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);

    const Size childConstraint{
        canScrollH ? kUnconstrainedSpace : viewportAvailW,
        canScrollV ? kUnconstrainedSpace : viewportAvailH};

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
    if (!element) return 0;
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
    if (!element) return 0;
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
    if (!element) return 1;
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
    if (!element) return 1;
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

    const std::size_t rowCount = std::max<std::size_t>(1, rows_.size());
    const std::size_t colCount = std::max<std::size_t>(1, cols_.size());

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
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), rowCount - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), colCount - 1);
        const std::size_t rSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetRowSpan(child)), rowCount - rIdx);
        const std::size_t cSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumnSpan(child)), colCount - cIdx);
        const Size d = child->DesiredSize();
        if (rSpan == 1u) {
            if (workRows[rIdx].unit != GridUnitType::Pixel) {
                workRows[rIdx].computedHeight = std::max(workRows[rIdx].computedHeight, d.height);
            }
        } else {
            hasMultiSpan = true;
        }
        if (cSpan == 1u) {
            if (workCols[cIdx].unit != GridUnitType::Pixel) {
                workCols[cIdx].computedWidth = std::max(workCols[cIdx].computedWidth, d.width);
            }
        } else {
            hasMultiSpan = true;
        }
    }

    if (hasMultiSpan) {
        for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
            const std::size_t rIdx = std::min<std::size_t>(
                static_cast<std::size_t>(GetRow(child)), rowCount - 1);
            const std::size_t cIdx = std::min<std::size_t>(
                static_cast<std::size_t>(GetColumn(child)), colCount - 1);
            const std::size_t rSpan = std::min<std::size_t>(
                static_cast<std::size_t>(GetRowSpan(child)), rowCount - rIdx);
            const std::size_t cSpan = std::min<std::size_t>(
                static_cast<std::size_t>(GetColumnSpan(child)), colCount - cIdx);
            const Size d = child->DesiredSize();

            if (rSpan > 1u) {
                float spanH = 0.0f;
                float starWeight = 0.0f;
                std::size_t autoTracks = 0;
                for (std::size_t sr = 0; sr < rSpan; ++sr) {
                    const auto& tr = workRows[rIdx + sr];
                    spanH += tr.computedHeight;
                    if (tr.unit == GridUnitType::Star) starWeight += std::max(0.0f, tr.value);
                    else if (tr.unit == GridUnitType::Auto) ++autoTracks;
                }
                if (d.height > spanH) {
                    const float excess = d.height - spanH;
                    if (starWeight > 0.0f) {
                        for (std::size_t sr = 0; sr < rSpan; ++sr) {
                            auto& tr = workRows[rIdx + sr];
                            if (tr.unit == GridUnitType::Star) {
                                tr.computedHeight += excess * (std::max(0.0f, tr.value) / starWeight);
                            }
                        }
                    } else if (autoTracks > 0u) {
                        const float share = excess / static_cast<float>(autoTracks);
                        for (std::size_t sr = 0; sr < rSpan; ++sr) {
                            auto& tr = workRows[rIdx + sr];
                            if (tr.unit == GridUnitType::Auto) tr.computedHeight += share;
                        }
                    }
                }
            }

            if (cSpan > 1u) {
                float spanW = 0.0f;
                float starWeight = 0.0f;
                std::size_t autoTracks = 0;
                for (std::size_t sc = 0; sc < cSpan; ++sc) {
                    const auto& tc = workCols[cIdx + sc];
                    spanW += tc.computedWidth;
                    if (tc.unit == GridUnitType::Star) starWeight += std::max(0.0f, tc.value);
                    else if (tc.unit == GridUnitType::Auto) ++autoTracks;
                }
                if (d.width > spanW) {
                    const float excess = d.width - spanW;
                    if (starWeight > 0.0f) {
                        for (std::size_t sc = 0; sc < cSpan; ++sc) {
                            auto& tc = workCols[cIdx + sc];
                            if (tc.unit == GridUnitType::Star) {
                                tc.computedWidth += excess * (std::max(0.0f, tc.value) / starWeight);
                            }
                        }
                    } else if (autoTracks > 0u) {
                        const float share = excess / static_cast<float>(autoTracks);
                        for (std::size_t sc = 0; sc < cSpan; ++sc) {
                            auto& tc = workCols[cIdx + sc];
                            if (tc.unit == GridUnitType::Auto) tc.computedWidth += share;
                        }
                    }
                }
            }
        }
    }

    float totalW = 0.0f;
    for (const auto& c : workCols) totalW += c.computedWidth;
    float totalH = 0.0f;
    for (const auto& r : workRows) totalH += r.computedHeight;

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
            totalStarH += std::max(0.0f, r.value);
        }
    }

    bool hasMultiSpan = false;
    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), workRows.size() - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), workCols.size() - 1);
        const std::size_t rSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetRowSpan(child)), workRows.size() - rIdx);
        const std::size_t cSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumnSpan(child)), workCols.size() - cIdx);
        const Size d = child->DesiredSize();
        if (rSpan == 1u) {
            if (workRows[rIdx].unit == GridUnitType::Auto) {
                workRows[rIdx].computedHeight = std::max(workRows[rIdx].computedHeight, d.height);
            }
        } else {
            hasMultiSpan = true;
        }
        if (cSpan == 1u) {
            if (workCols[cIdx].unit == GridUnitType::Auto) {
                workCols[cIdx].computedWidth = std::max(workCols[cIdx].computedWidth, d.width);
            }
        } else {
            hasMultiSpan = true;
        }
    }

    if (hasMultiSpan) {
        for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
            const std::size_t rIdx = std::min<std::size_t>(
                static_cast<std::size_t>(GetRow(child)), workRows.size() - 1);
            const std::size_t cIdx = std::min<std::size_t>(
                static_cast<std::size_t>(GetColumn(child)), workCols.size() - 1);
            const std::size_t rSpan = std::min<std::size_t>(
                static_cast<std::size_t>(GetRowSpan(child)), workRows.size() - rIdx);
            const std::size_t cSpan = std::min<std::size_t>(
                static_cast<std::size_t>(GetColumnSpan(child)), workCols.size() - cIdx);
            const Size d = child->DesiredSize();

            if (rSpan > 1u) {
                float spanH = 0.0f;
                bool hasStar = false;
                std::size_t autoTracks = 0;
                for (std::size_t sr = 0; sr < rSpan; ++sr) {
                    const auto& tr = workRows[rIdx + sr];
                    spanH += tr.computedHeight;
                    if (tr.unit == GridUnitType::Star) hasStar = true;
                    else if (tr.unit == GridUnitType::Auto) ++autoTracks;
                }
                if (!hasStar && autoTracks > 0u && d.height > spanH) {
                    const float share = (d.height - spanH) / static_cast<float>(autoTracks);
                    for (std::size_t sr = 0; sr < rSpan; ++sr) {
                        auto& tr = workRows[rIdx + sr];
                        if (tr.unit == GridUnitType::Auto) tr.computedHeight += share;
                    }
                }
            }

            if (cSpan > 1u) {
                float spanW = 0.0f;
                bool hasStar = false;
                std::size_t autoTracks = 0;
                for (std::size_t sc = 0; sc < cSpan; ++sc) {
                    const auto& tc = workCols[cIdx + sc];
                    spanW += tc.computedWidth;
                    if (tc.unit == GridUnitType::Star) hasStar = true;
                    else if (tc.unit == GridUnitType::Auto) ++autoTracks;
                }
                if (!hasStar && autoTracks > 0u && d.width > spanW) {
                    const float share = (d.width - spanW) / static_cast<float>(autoTracks);
                    for (std::size_t sc = 0; sc < cSpan; ++sc) {
                        auto& tc = workCols[cIdx + sc];
                        if (tc.unit == GridUnitType::Auto) tc.computedWidth += share;
                    }
                }
            }
        }
    }

    for (const auto& c : workCols) {
        if (c.unit == GridUnitType::Auto) fixedW += c.computedWidth;
    }
    for (const auto& r : workRows) {
        if (r.unit == GridUnitType::Auto) fixedH += r.computedHeight;
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

    for (UIElement* child = FirstChild(); child != nullptr; child = child->NextSibling()) {
        const std::size_t rIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetRow(child)), workRows.size() - 1);
        const std::size_t cIdx = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumn(child)), workCols.size() - 1);
        const std::size_t rSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetRowSpan(child)), workRows.size() - rIdx);
        const std::size_t cSpan = std::min<std::size_t>(
            static_cast<std::size_t>(GetColumnSpan(child)), workCols.size() - cIdx);
        float spanW = 0.0f;
        for (std::size_t sc = 0; sc < cSpan; ++sc) {
            spanW += workCols[cIdx + sc].computedWidth;
        }
        float spanH = 0.0f;
        for (std::size_t sr = 0; sr < rSpan; ++sr) {
            spanH += workRows[rIdx + sr].computedHeight;
        }
        child->Arrange(Rect{
            workCols[cIdx].offsetX,
            workRows[rIdx].offsetY,
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
    if (!element) return 0.0f;
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
    if (!element) return 0.0f;
    float out = 0.0f;
    if (TryGetFloat(element->GetValue(PropId_CanvasTop), out)) {
        return out;
    }
    return 0.0f;
}

Size Canvas::MeasureCore(Size availableSize) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(availableSize);
    }
    return Size{0.0f, 0.0f};
}

void Canvas::ArrangeCore(Rect finalRect) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const float left = GetLeft(c);
        const float top = GetTop(c);
        const Size d = c->DesiredSize();
        c->Arrange(Rect{finalRect.x + left, finalRect.y + top, d.width, d.height});
    }
}

// ============================================================================
// Border
// ============================================================================

Border::Border() {
    EnsureControlTypesRegistered();
}

void Border::SetBackground(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Border::GetBackground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Border::SetBorderBrush(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Border::GetBorderBrush() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Border::SetBorderThickness(const Thickness& thickness) {
    SetValue(PropId_BorderThickness, value::FromThickness(thickness), ValueSourceRank::Local);
}

Thickness Border::GetBorderThickness() const noexcept {
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return *t;
    }
    return Thickness{};
}

void Border::SetCornerRadius(const CornerRadius& radius) {
    SetValue(PropId_CornerRadius, value::FromCornerRadius(radius), ValueSourceRank::Local);
}

CornerRadius Border::GetCornerRadius() const noexcept {
    if (auto r = value::TryGetCornerRadius(GetValue(PropId_CornerRadius))) {
        return *r;
    }
    return CornerRadius{};
}

Size Border::MeasureCore(Size availableSize) noexcept {
    const Thickness bt = GetBorderThickness();
    const float borderW = bt.left + bt.right;
    const float borderH = bt.top + bt.bottom;
    const Size innerAvail{
        std::max(0.0f, availableSize.width - borderW),
        std::max(0.0f, availableSize.height - borderH)};
    const Size contentSize = UIElement::MeasureCore(innerAvail);
    return Size{contentSize.width + borderW, contentSize.height + borderH};
}

void Border::ArrangeCore(Rect finalRect) noexcept {
    const Thickness bt = GetBorderThickness();
    const Rect innerRect{
        finalRect.x + bt.left,
        finalRect.y + bt.top,
        std::max(0.0f, finalRect.width - (bt.left + bt.right)),
        std::max(0.0f, finalRect.height - (bt.top + bt.bottom))};
    UIElement::ArrangeCore(innerRect);
}

// ============================================================================
// TextBlock
// ============================================================================

TextBlock::TextBlock() {
    EnsureControlTypesRegistered();
}

void TextBlock::SetText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string TextBlock::GetText() const {
    const auto v = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    return {};
}

void TextBlock::SetFontSize(float fontSize) {
    SetValue(PropId_FontSize, MakeFloat(fontSize), ValueSourceRank::Local);
}

float TextBlock::GetFontSize() const noexcept {
    float out = 14.0f;
    if (TryGetFloat(GetValue(PropId_FontSize), out)) {
        return out;
    }
    return 14.0f;
}

void TextBlock::SetForeground(const Color& color) {
    SetValue(PropId_Foreground, value::FromColor(color), ValueSourceRank::Local);
}

Color TextBlock::GetForeground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *c;
    }
    return Color{0.94f, 0.96f, 0.98f, 1.0f};
}

Size TextBlock::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float fontSize = GetFontSize();
    std::string text = GetText();
    if (text.empty()) {
        const auto ph = GetValue(PropId_Placeholder);
        if (const auto* s = std::get_if<StringValue>(&ph)) {
            text = s->str();
        }
    }
    const Size textMetrics = MeasureUtf8TextRun(text, fontSize);
    return Size{textMetrics.width + p.left + p.right, textMetrics.height + p.top + p.bottom};
}

// ============================================================================
// Path (4-Channel MTSDF Vector Icon Control)
// ============================================================================

Path::Path() {
    EnsureControlTypesRegistered();
}

void Path::SetData(std::string_view svgPathData) {
    SetValue(PropId_PathData, value::FromString(std::string(svgPathData)), ValueSourceRank::Local);
}

std::string Path::GetData() const {
    const auto v = GetValue(PropId_PathData);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    return {};
}

void Path::SetFill(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

bool Path::HasExplicitFill() const noexcept {
    return value::TryGetColor(GetValue(PropId_Background)).has_value() ||
           value::TryGetColor(GetValue(PropId_Foreground)).has_value();
}

Color Path::GetFill() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    if (auto fg = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *fg;
    }
    if (HasExplicitStroke() && GetStrokeThickness() > 0.0f) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    return Color{0.92f, 0.95f, 1.0f, 1.0f};
}

void Path::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

bool Path::HasExplicitStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return c->a > 0.0f;
    }
    return false;
}

Color Path::GetStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

void Path::SetStrokeThickness(float thickness) {
    SetValue(PropId_BorderThickness, value::FromThickness(Thickness(thickness)), ValueSourceRank::Local);
}

float Path::GetStrokeThickness() const noexcept {
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return t->left;
    }
    return 0.0f;
}

void Path::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Path::GetStretch() const noexcept {
    const auto v = GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

Size Path::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const std::string data = GetData();
    if (data.empty()) {
        return Size{p.left + p.right, p.top + p.bottom};
    }
    const Stretch stretch = GetStretch();
    float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
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
            std::max(1.0f, std::max(bw, bx + bw)) + p.left + p.right,
            std::max(1.0f, std::max(bh, by + bh)) + p.top + p.bottom};
    }
    return Size{
        std::max(1.0f, bw) + p.left + p.right,
        std::max(1.0f, bh) + p.top + p.bottom};
}

// ============================================================================
// Image
// ============================================================================

Image::Image() {
    EnsureControlTypesRegistered();
}

void Image::SetSource(std::string_view sourceUri) {
    SetValue(PropId_ImageSource, value::FromString(std::string(sourceUri)), ValueSourceRank::Local);
}

std::string Image::GetSource() const {
    const auto v = GetValue(PropId_ImageSource);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    return {};
}

void Image::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Image::GetStretch() const noexcept {
    const auto v = GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

Size Image::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const std::string src = GetSource();
    const float defaultSide = src.empty() ? 0.0f : 32.0f;
    return Size{defaultSide + p.left + p.right, defaultSide + p.top + p.bottom};
}

// ============================================================================
// ItemsControl
// ============================================================================

ItemsControl::ItemsControl() {
    EnsureControlTypesRegistered();
}

void ItemsControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    items_ = items;
    while (FirstChild()) {
        RemoveChild(FirstChild(), registry, uiHeap);
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        IntrusivePtr<UIElement> itemVisual;
        if (itemGenerator_) {
            itemVisual = itemGenerator_(i, items_[i]);
        } else {
            auto tb = MakeIntrusive<TextBlock>();
            tb->SetText(rs_rt::valueToString(items_[i]));
            itemVisual = tb;
        }
        if (itemVisual) {
            AddChild(itemVisual.Get(), registry, uiHeap);
        }
    }
}

void ItemsControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Panel::OnPropertyChanged(propId, newValue);
    if (propId == PropId_ItemsSource) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (const auto* arrRef = std::get_if<ObjectRef>(&newValue)) {
            if (arrRef->valid() && arrRef->kind == ObjectKind::Array && domains_) {
                if (auto* heap = domains_->heapForObject(*arrRef)) {
                    const std::size_t len = heap->arrayLength(*arrRef).value_or(0);
                    std::vector<UnifiedValue> extracted;
                    extracted.reserve(len);
                    for (std::size_t i = 0; i < len; ++i) {
                        UnifiedValue elem = heap->arrayGet(*arrRef, i).value_or(UnifiedValue{});
                        if (const auto* strRef = std::get_if<ObjectRef>(&elem)) {
                            if (strRef->kind == ObjectKind::String) {
                                if (const auto sv = domains_->stringViewAcrossDomains(*strRef)) {
                                    elem = value::FromString(std::string(*sv));
                                }
                            }
                        }
                        extracted.push_back(std::move(elem));
                    }
                    SetItemsSource(extracted, registry, uiHeap);
                    return;
                }
            }
        } else if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const std::string_view raw = sv->str();
            std::vector<UnifiedValue> extracted;
            std::size_t start = 0;
            while (start < raw.size()) {
                std::size_t sep = raw.find('|', start);
                if (sep == std::string_view::npos) {
                    sep = raw.size();
                }
                const std::string_view item = raw.substr(start, sep - start);
                if (!item.empty()) {
                    extracted.push_back(value::FromString(std::string(item)));
                }
                start = sep + 1;
            }
            SetItemsSource(extracted, registry, uiHeap);
            return;
        } else if (std::holds_alternative<std::monostate>(newValue) ||
                   std::holds_alternative<NullObject>(newValue)) {
            SetItemsSource({}, registry, uiHeap);
            return;
        }
    }
}

Size ItemsControl::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const Size childAvail{
        std::max(0.0f, availableSize.width - padW),
        std::max(0.0f, availableSize.height - padH)};

    float totalH = 0.0f;
    float maxW = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(childAvail);
        const Size d = c->DesiredSize();
        totalH += d.height;
        maxW = std::max(maxW, d.width);
    }
    return Size{maxW + padW, totalH + padH};
}

void ItemsControl::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    float cursorY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const Size d = c->DesiredSize();
        c->Arrange(Rect{startX, cursorY, innerW, d.height});
        cursorY += d.height;
    }
}

// ============================================================================
// AxbTemplateRegistry & UserControl Self-Contained Mounting
// ============================================================================

AxbTemplateRegistry& AxbTemplateRegistry::Instance() noexcept {
    static AxbTemplateRegistry instance;
    return instance;
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::InsertMtsdfPathLocked(AxbMtsdfPathEntry entry) {
    if (entry.pathHash == 0) {
        return nullptr;
    }
    if (const auto existing = mtsdfPaths_.find(entry.pathHash); existing != mtsdfPaths_.end()) {
        return &existing->second;
    }

    // Assign a globally unique, non-overlapping UV slot in the bottom half [0.5..1.0) of the unified UI/Glyph atlas
    const std::uint16_t safeCell = std::clamp<std::uint16_t>(
        entry.cellWidth == 0 ? 32u : entry.cellWidth, 8u, 128u);
    entry.cellWidth = safeCell;
    entry.cellHeight = safeCell;

    const std::uint32_t slot = nextMtsdfAtlasSlot_++;
    std::uint16_t atlasDim = 1024u;
    while (atlasDim < 4096u) {
        const std::uint32_t cols = std::max<std::uint32_t>(1u, atlasDim / safeCell);
        const std::uint32_t iconRows = std::max<std::uint32_t>(1u, cols / 2u);
        if (slot < cols * iconRows) {
            break;
        }
        atlasDim = static_cast<std::uint16_t>(atlasDim * 2u);
    }

    const std::uint32_t cols = std::max<std::uint32_t>(1u, atlasDim / safeCell);
    const std::uint32_t rowBase = std::max<std::uint32_t>(1u, cols / 2u);
    const std::uint32_t iconRows = std::max<std::uint32_t>(1u, cols - rowBase);
    const std::uint32_t slotCol = slot % cols;
    const std::uint32_t slotRow = rowBase + ((slot / cols) % iconRows);
    const float invAtlas = 1.0f / static_cast<float>(atlasDim);

    entry.atlasWidth = atlasDim;
    entry.atlasHeight = atlasDim;
    entry.u0 = (static_cast<float>(slotCol * safeCell) + 0.5f) * invAtlas;
    entry.v0 = (static_cast<float>(slotRow * safeCell) + 0.5f) * invAtlas;
    entry.u1 = (static_cast<float>((slotCol + 1u) * safeCell) - 0.5f) * invAtlas;
    entry.v1 = (static_cast<float>((slotRow + 1u) * safeCell) - 0.5f) * invAtlas;

    auto [insertedIt, _] = mtsdfPaths_.emplace(entry.pathHash, std::move(entry));
    return &insertedIt->second;
}

void AxbTemplateRegistry::Register(std::string className, AxbPackage package) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : package.resources) {
        if (!kv.first.empty()) {
            resources_[kv.first] = kv.second;
        }
    }
    for (const auto& mp : package.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
    if (className.empty()) {
        return;
    }
    const std::string shortName(StripNamespacePrefix(className));
    templates_[className] = package;
    if (shortName != className) {
        templates_[shortName] = std::move(package);
    }
}

void AxbTemplateRegistry::RegisterResources(const AxbPackage& package) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : package.resources) {
        if (!kv.first.empty()) {
            resources_[kv.first] = kv.second;
        }
    }
    for (const auto& mp : package.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
}

void AxbTemplateRegistry::RegisterResource(std::string key, UnifiedValue value) {
    if (!key.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_[std::move(key)] = std::move(value);
    }
}

void AxbTemplateRegistry::RegisterMtsdfPath(AxbMtsdfPathEntry entry) {
    if (entry.pathHash != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)InsertMtsdfPathLocked(std::move(entry));
    }
}

const UnifiedValue* AxbTemplateRegistry::FindResource(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(key));
    if (it != resources_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbPackage* AxbTemplateRegistry::Find(std::string_view className) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = templates_.find(std::string(className));
    if (it != templates_.end()) {
        return &it->second;
    }
    const std::string shortName(StripNamespacePrefix(className));
    it = templates_.find(shortName);
    if (it != templates_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::FindMtsdfPath(std::uint64_t pathHash) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = mtsdfPaths_.find(pathHash);
    if (it != mtsdfPaths_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::FindOrBakeMtsdfPath(std::string_view pathData) {
    if (pathData.empty()) {
        return nullptr;
    }
    const std::uint64_t hash = ComputeSvgPathHash(pathData);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = mtsdfPaths_.find(hash);
        if (it != mtsdfPaths_.end()) {
            return &it->second;
        }
    }
    AxbMtsdfPathEntry baked = BakeSvgPathToMtsdfEntry(pathData, 32u, 4.0f, 0u, 1024u);
    std::lock_guard<std::mutex> lock(mutex_);
    return InsertMtsdfPathLocked(std::move(baked));
}

UserControl::UserControl() {
    EnsureControlTypesRegistered();
}

UserControl::UserControl(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext)
    : controlClassName_(className) {
    EnsureControlTypesRegistered();
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext);
    }
}

bool UserControl::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext) {
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext);
    return templateMounted_;
}

Window::Window(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext)
    : windowClassName_(className) {
    EnsureControlTypesRegistered();
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext);
    }
}

bool Window::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext) {
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext);
    return templateMounted_;
}

IntrusivePtr<UIElement> CreateElementByTypeName(std::string_view typeName) {
    EnsureControlTypesRegistered();
    const std::string_view shortName = StripNamespacePrefix(typeName);
    if (shortName == "Window") return MakeIntrusive<Window>();
    if (shortName == "Panel" || shortName == "ResourceDictionary") return MakeIntrusive<Panel>();
    if (shortName == "DockPanel") return MakeIntrusive<DockPanel>();
    if (shortName == "WrapPanel") return MakeIntrusive<WrapPanel>();
    if (shortName == "Viewbox") return MakeIntrusive<Viewbox>();
    if (shortName == "ScrollViewer") return MakeIntrusive<ScrollViewer>();
    if (shortName == "StackPanel" ||
        shortName == "HeaderedContentControl" || shortName == "GroupBox" ||
        shortName == "Expander" || shortName == "ToolBar" ||
        shortName == "StatusBar" || shortName == "Menu" ||
        shortName == "ContextMenu" || shortName == "TabControl" ||
        shortName == "TabItem") {
        return MakeIntrusive<StackPanel>();
    }
    if (shortName == "Grid" || shortName == "UniformGrid") {
        return MakeIntrusive<Grid>();
    }
    if (shortName == "Canvas") return MakeIntrusive<Canvas>();
    if (shortName == "Path") return MakeIntrusive<Path>();
    if (shortName == "Image") return MakeIntrusive<Image>();
    if (shortName == "Border" ||
        shortName == "ContentControl" || shortName == "ContentPresenter" ||
        shortName == "Rectangle" || shortName == "Ellipse" ||
        shortName == "Line" || shortName == "Polygon" || shortName == "Polyline" ||
        shortName == "RiveControl" || shortName == "BoxShadow" ||
        shortName == "Slider" || shortName == "ProgressBar" ||
        shortName == "ComboBoxItem" || shortName == "ListBoxItem" ||
        shortName == "ListViewItem" || shortName == "TreeViewItem") {
        return MakeIntrusive<Border>();
    }
    if (shortName == "TextBlock" || shortName == "Label" ||
        shortName == "TextBox" || shortName == "PasswordBox" ||
        shortName == "RichTextBox") {
        return MakeIntrusive<TextBlock>();
    }
    if (shortName == "Button" || shortName == "RepeatButton" ||
        shortName == "ToggleButton" || shortName == "CheckBox" ||
        shortName == "RadioButton" || shortName == "Hyperlink" ||
        shortName == "MenuItem") {
        return MakeIntrusive<Button>();
    }
    if (shortName == "ItemsControl" || shortName == "ComboBox" ||
        shortName == "ListBox" || shortName == "ListView" ||
        shortName == "TreeView" || shortName == "ItemsPresenter") {
        return MakeIntrusive<ItemsControl>();
    }
    if (shortName == "UserControl" || shortName == "Page") {
        return MakeIntrusive<UserControl>();
    }
    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr) {
        return MakeIntrusive<UserControl>(typeName);
    }
    return MakeIntrusive<UIElement>();
}

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext) {
    if (!rootControl) {
        return false;
    }

    if (!package.mtsdfPaths.empty()) {
        for (const auto& mp : package.mtsdfPaths) {
            AxbTemplateRegistry::Instance().RegisterMtsdfPath(mp);
        }
    }

    NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;
    const ObjectRef effectiveContext =
        dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();

    std::vector<UIElement*> elementStack;
    bool rootVisited = false;

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                UIElement* currentElem = nullptr;

                if (!rootVisited) {
                    rootVisited = true;
                    currentElem = rootControl;
                    const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                        ? rootControl->BoundScriptWrapper()
                        : effectiveContext;
                    if (domains && registry && wrapperObj.valid() &&
                        package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        const auto rootH = rootControl->ToNativeHandle(*registry);
                        (void)domains->writeObjectField(
                            DomainKind::Ui,
                            wrapperObj,
                            package.rootHandleFieldIndex,
                            rootH,
                            nullptr);
                    }
                } else {
                    IntrusivePtr<UIElement> createdChild;
                    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr &&
                        StripNamespacePrefix(typeName) != "UserControl" &&
                        StripNamespacePrefix(typeName) != "Page") {
                        createdChild = MakeIntrusive<UserControl>(
                            typeName,
                            domains,
                            bindingEngine,
                            effectiveContext,
                            eventInterpreter,
                            eventAotContext);
                    } else {
                        createdChild = CreateElementByTypeName(typeName);
                    }
                    if (!elementStack.empty() && createdChild) {
                        elementStack.back()->AddChild(createdChild.Get(), registry, uiHeap);
                        currentElem = createdChild.Get();
                    }
                }

                if (auto* ic = currentElem ? currentElem->DynamicCast<ItemsControl>() : nullptr) {
                    ic->SetDomains(domains);
                }

                if (currentElem && !elemName.empty()) {
                    currentElem->SetName(std::string(elemName));

                    const ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid()
                        ? rootControl->BoundScriptWrapper()
                        : effectiveContext;
                    if (domains && registry && wrapperObj.valid()) {
                        for (const auto& namedSlot : package.namedControls) {
                            if (namedSlot.name == elemName &&
                                namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                                const auto h = currentElem->ToNativeHandle(*registry);
                                (void)domains->writeObjectField(
                                    DomainKind::Ui,
                                    wrapperObj,
                                    namedSlot.fieldIndexInClass,
                                    h,
                                    nullptr);
                                break;
                            }
                        }
                    }
                }

                elementStack.push_back(currentElem);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elementStack.empty()) {
                    elementStack.pop_back();
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!elementStack.empty() && elementStack.back()) {
                    elementStack.back()->SetValue(
                        inst.propId,
                        inst.literalValue,
                        ValueSourceRank::Local);
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!elementStack.empty() && elementStack.back() &&
                    bindingEngine && effectiveContext.valid()) {
                    if (inst.strIdx0 < package.bindingChains.size()) {
                        const auto& chain = package.bindingChains[inst.strIdx0];
                        IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                        if (!chain.hops.empty()) {
                            (void)bindingEngine->BindChain(
                                effectiveContext,
                                chain,
                                targetPtr);
                        } else if (!chain.rawPath.empty()) {
                            (void)bindingEngine->BindPath(
                                effectiveContext,
                                chain.rawPath,
                                targetPtr,
                                inst.propId,
                                chain.mode);
                        }
                    }
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!elementStack.empty() && elementStack.back() && bindingEngine) {
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    auto resolveCanonicalTypeId = [](std::string_view raw) -> SymbolId {
                        std::string canonical(raw);
                        if (canonical.find("::") == std::string::npos) {
                            if (const std::size_t lastDot = canonical.rfind('.'); lastDot != std::string::npos) {
                                canonical.replace(lastDot, 1, "::");
                            }
                        }
                        return rs_sem::stableTypeId(canonical);
                    };
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : resolveCanonicalTypeId(compName);
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    (void)bindingEngine->BindEcsColumn(
                        compId,
                        fieldName,
                        0,
                        targetPtr,
                        inst.propId);
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elementStack.empty() && elementStack.back()) {
                    if (auto* btn = elementStack.back()->DynamicCast<Button>()) {
                        const ObjectRef targetObj = rootControl->BoundScriptWrapper().valid()
                            ? rootControl->BoundScriptWrapper()
                            : effectiveContext;
                        if (targetObj.valid() && inst.symbolId != 0) {
                            if (eventInterpreter) {
                                btn->BindScriptClickHandler(
                                    eventInterpreter,
                                    targetObj,
                                    inst.symbolId);
                            } else if (eventAotContext) {
                                btn->BindAotClickHandler(
                                    eventAotContext,
                                    targetObj,
                                    inst.symbolId);
                            }
                        }
                    }
                }
                break;
            }

            case AxbOpcode::AddRowDefinition: {
                if (!elementStack.empty() && elementStack.back()) {
                    if (auto* grid = elementStack.back()->DynamicCast<Grid>()) {
                        const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                        grid->AddRowDefinition(static_cast<GridUnitType>(inst.flags), val);
                    }
                }
                break;
            }

            case AxbOpcode::AddColumnDefinition: {
                if (!elementStack.empty() && elementStack.back()) {
                    if (auto* grid = elementStack.back()->DynamicCast<Grid>()) {
                        const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                        grid->AddColumnDefinition(static_cast<GridUnitType>(inst.flags), val);
                    }
                }
                break;
            }

            case AxbOpcode::SetResourceProperty: {
                if (!elementStack.empty() && elementStack.back()) {
                    const std::string_view key = package.GetString(inst.strIdx0);
                    const UnifiedValue* resolved = nullptr;
                    for (auto it = package.resources.rbegin(); it != package.resources.rend(); ++it) {
                        if (it->first == key) {
                            resolved = &it->second;
                            break;
                        }
                    }
                    if (!resolved) {
                        resolved = AxbTemplateRegistry::Instance().FindResource(key);
                    }
                    if (resolved) {
                        elementStack.back()->SetValue(inst.propId, *resolved, ValueSourceRank::Local);
                    } else if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        elementStack.back()->SetValue(inst.propId, inst.literalValue, ValueSourceRank::Local);
                    }
                }
                break;
            }
        }
    }

    return rootVisited;
}

} // namespace aero
