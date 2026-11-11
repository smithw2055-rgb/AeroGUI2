#include "aero/controls/Controls.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/animation/AnimationEngine.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
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
TypeClass gContentPresenterTypeClass{};
TypeClass gPathTypeClass{};
TypeClass gImageTypeClass{};
TypeClass gCheckBoxTypeClass{};
TypeClass gRadioButtonTypeClass{};
TypeClass gSliderTypeClass{};
TypeClass gTextBoxTypeClass{};
TypeClass gPasswordBoxTypeClass{};
TypeClass gItemsControlTypeClass{};
TypeClass gListBoxTypeClass{};
TypeClass gComboBoxTypeClass{};
TypeClass gUserControlTypeClass{};
TypeClass gRectangleTypeClass{};
TypeClass gEllipseTypeClass{};
TypeClass gLineTypeClass{};
TypeClass gPolygonTypeClass{};
TypeClass gPolylineTypeClass{};
TypeClass gUniformGridTypeClass{};
TypeClass gProgressBarTypeClass{};
TypeClass gHeaderedContentControlTypeClass{};
TypeClass gGroupBoxTypeClass{};
TypeClass gExpanderTypeClass{};
TypeClass gTabItemTypeClass{};
TypeClass gTabControlTypeClass{};
TypeClass gRiveControlTypeClass{};
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
    reg.RegisterType(gContentPresenterTypeClass, "Aero.UI.ContentPresenter", panel);
    reg.RegisterType(gPathTypeClass, "Aero.UI.Path", uiElem);
    reg.RegisterType(gImageTypeClass, "Aero.UI.Image", uiElem);
    const auto* button = Button::StaticTypeClass();
    reg.RegisterType(gCheckBoxTypeClass, "Aero.UI.CheckBox", button);
    reg.RegisterType(gRadioButtonTypeClass, "Aero.UI.RadioButton", button);
    reg.RegisterType(gSliderTypeClass, "Aero.UI.Slider", uiElem);
    reg.RegisterType(gTextBoxTypeClass, "Aero.UI.TextBox", &gTextBlockTypeClass);
    reg.RegisterType(gPasswordBoxTypeClass, "Aero.UI.PasswordBox", &gTextBoxTypeClass);
    reg.RegisterType(gItemsControlTypeClass, "Aero.UI.ItemsControl", panel);
    reg.RegisterType(gListBoxTypeClass, "Aero.UI.ListBox", &gItemsControlTypeClass);
    reg.RegisterType(gComboBoxTypeClass, "Aero.UI.ComboBox", &gItemsControlTypeClass);
    reg.RegisterType(gUserControlTypeClass, "Aero.UI.UserControl", panel);
    reg.RegisterType(gRectangleTypeClass, "Aero.UI.Rectangle", uiElem);
    reg.RegisterType(gEllipseTypeClass, "Aero.UI.Ellipse", uiElem);
    reg.RegisterType(gLineTypeClass, "Aero.UI.Line", uiElem);
    reg.RegisterType(gPolygonTypeClass, "Aero.UI.Polygon", &gPathTypeClass);
    reg.RegisterType(gPolylineTypeClass, "Aero.UI.Polyline", &gPathTypeClass);
    reg.RegisterType(gUniformGridTypeClass, "Aero.UI.UniformGrid", panel);
    reg.RegisterType(gProgressBarTypeClass, "Aero.UI.ProgressBar", uiElem);
    reg.RegisterType(gHeaderedContentControlTypeClass, "Aero.UI.HeaderedContentControl", panel);
    reg.RegisterType(gGroupBoxTypeClass, "Aero.UI.GroupBox", &gHeaderedContentControlTypeClass);
    reg.RegisterType(gExpanderTypeClass, "Aero.UI.Expander", &gHeaderedContentControlTypeClass);
    reg.RegisterType(gTabItemTypeClass, "Aero.UI.TabItem", &gHeaderedContentControlTypeClass);
    reg.RegisterType(gTabControlTypeClass, "Aero.UI.TabControl", panel);
    reg.RegisterType(gRiveControlTypeClass, "Aero.UI.RiveControl", &gUserControlTypeClass);
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

const TypeClass* ContentPresenter::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gContentPresenterTypeClass;
}

const TypeClass* Path::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gPathTypeClass;
}

const TypeClass* Image::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gImageTypeClass;
}

const TypeClass* CheckBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gCheckBoxTypeClass;
}

const TypeClass* RadioButton::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gRadioButtonTypeClass;
}

const TypeClass* Slider::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gSliderTypeClass;
}

const TypeClass* TextBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gTextBoxTypeClass;
}

const TypeClass* PasswordBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gPasswordBoxTypeClass;
}

const TypeClass* ItemsControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gItemsControlTypeClass;
}

const TypeClass* ListBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gListBoxTypeClass;
}

const TypeClass* ComboBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gComboBoxTypeClass;
}

const TypeClass* UserControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gUserControlTypeClass;
}

const TypeClass* Rectangle::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gRectangleTypeClass;
}

const TypeClass* Ellipse::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gEllipseTypeClass;
}

const TypeClass* Line::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gLineTypeClass;
}

const TypeClass* Polygon::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gPolygonTypeClass;
}

const TypeClass* Polyline::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gPolylineTypeClass;
}

const TypeClass* UniformGrid::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gUniformGridTypeClass;
}

const TypeClass* ProgressBar::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gProgressBarTypeClass;
}

const TypeClass* HeaderedContentControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gHeaderedContentControlTypeClass;
}

const TypeClass* GroupBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gGroupBoxTypeClass;
}

const TypeClass* Expander::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gExpanderTypeClass;
}

const TypeClass* TabItem::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gTabItemTypeClass;
}

const TypeClass* TabControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gTabControlTypeClass;
}

const TypeClass* RiveControl::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gRiveControlTypeClass;
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
    if (!std::holds_alternative<std::monostate>(v) &&
        !std::holds_alternative<NullObject>(v)) {
        return rs_rt::valueToString(v);
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
    if (const UIElement* tp = TemplatedParent()) {
        if (TryGetFloat(tp->GetValue(PropId_FontSize), out)) {
            return out;
        }
    }
    for (const UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (anc->IsA<ContentPresenter>()) {
            if (TryGetFloat(anc->GetValue(PropId_FontSize), out)) {
                return out;
            }
            if (const UIElement* ancTp = anc->TemplatedParent()) {
                if (TryGetFloat(ancTp->GetValue(PropId_FontSize), out)) {
                    return out;
                }
            }
        }
    }
    return 14.0f;
}

void TextBlock::SetFontFamily(std::string_view fontFamily) {
    if (fontFamily.empty()) {
        (void)ClearValue(PropId_FontFamily, ValueSourceRank::Local);
    } else {
        SetValue(PropId_FontFamily, value::FromString(std::string(fontFamily)), ValueSourceRank::Local);
    }
}

std::string TextBlock::GetFontFamily() const {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
        if (cur == this || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                const UnifiedValue tpVal = tp->GetValue(PropId_FontFamily);
                if (const auto* s = std::get_if<StringValue>(&tpVal)) {
                    if (!s->view().empty()) {
                        return s->str();
                    }
                }
            }
        }
    }
    return {};
}

void TextBlock::SetTextWrapping(TextWrapping wrapping) {
    SetValue(
        PropId_TextWrapping,
        value::FromInt(static_cast<std::int64_t>(wrapping)),
        ValueSourceRank::Local);
}

TextWrapping TextBlock::GetTextWrapping() const noexcept {
    const UnifiedValue v = GetValue(PropId_TextWrapping);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i == 1) return TextWrapping::Wrap;
        if (*i == 2) return TextWrapping::WrapWithOverflow;
        return TextWrapping::NoWrap;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_TextWrapping, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi == 1) return TextWrapping::Wrap;
            if (*pi == 2) return TextWrapping::WrapWithOverflow;
        }
    }
    return TextWrapping::NoWrap;
}

void TextBlock::SetForeground(const Color& color) {
    SetValue(PropId_Foreground, value::FromColor(color), ValueSourceRank::Local);
}

Color TextBlock::GetForeground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *c;
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (auto c = value::TryGetColor(tp->GetValue(PropId_Foreground))) {
            return *c;
        }
    }
    for (const UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (anc->IsA<ContentPresenter>()) {
            if (auto c = value::TryGetColor(anc->GetValue(PropId_Foreground))) {
                return *c;
            }
            if (const UIElement* ancTp = anc->TemplatedParent()) {
                if (auto c = value::TryGetColor(ancTp->GetValue(PropId_Foreground))) {
                    return *c;
                }
            }
        }
    }
    return Color{0.94f, 0.96f, 0.98f, 1.0f};
}

Size TextBlock::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float fontSize = GetFontSize();
    std::string text = GetText();
    if (text.empty()) {
        const auto ph = GetValue(PropId_Placeholder);
        if (const auto* s = std::get_if<StringValue>(&ph)) {
            text = s->str();
        }
    }
    const std::string family = GetFontFamily();
    const TextWrapping wrapMode = GetTextWrapping();
    const float maxTextW = (wrapMode != TextWrapping::NoWrap)
        ? std::max(1.0f, availableSize.width - padW)
        : std::max(0.0f, availableSize.width - padW);
    const Size textMetrics = MeasureUtf8TextRun(text, fontSize, family, maxTextW, wrapMode);
    return Size{textMetrics.width + padW, textMetrics.height + padH};
}


// ============================================================================
// ContentPresenter (P1-1 ControlTemplate Content Host)
// ============================================================================

ContentPresenter::ContentPresenter() {
    EnsureControlTypesRegistered();
}

std::string ContentPresenter::GetEffectiveText() const {
    if (FirstChild() != nullptr) {
        return {};
    }
    auto tryExtractScalarOrString = [](const UnifiedValue& val, std::string& out) -> bool {
        if (const auto* sv = std::get_if<StringValue>(&val)) {
            out = sv->str();
            return true;
        }
        if (std::holds_alternative<std::int64_t>(val) ||
            std::holds_alternative<FloatValue>(val) ||
            std::holds_alternative<bool>(val)) {
            out = rs_rt::valueToString(val);
            return true;
        }
        return false;
    };
    std::string resolved;
    for (const std::uint16_t selfProp : {PropId_Content, PropId_Text}) {
        if (const auto* entry = FindCompactEntry(selfProp)) {
            if (tryExtractScalarOrString(entry->effectiveValue, resolved)) {
                return resolved;
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        for (const std::uint16_t hostProp : {PropId_Content, PropId_Text, PropId_Title}) {
            if (const auto* entry = tp->FindCompactEntry(hostProp)) {
                if (tryExtractScalarOrString(entry->effectiveValue, resolved)) {
                    return resolved;
                }
            }
        }
    }
    return {};
}

Color ContentPresenter::GetEffectiveForeground() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Foreground))) {
        return *c;
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (auto c = value::TryGetColor(tp->GetValue(PropId_Foreground))) {
            return *c;
        }
    }
    return Color{0.96f, 0.98f, 1.0f, 1.0f};
}

float ContentPresenter::GetEffectiveFontSize() const noexcept {
    float out = 14.0f;
    if (TryGetFloat(GetValue(PropId_FontSize), out) && out > 0.0f) {
        return out;
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (TryGetFloat(tp->GetValue(PropId_FontSize), out) && out > 0.0f) {
            return out;
        }
    }
    return 14.0f;
}

std::string ContentPresenter::GetEffectiveFontFamily() const {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        const UnifiedValue v = tp->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
    }
    return {};
}

TextWrapping ContentPresenter::GetEffectiveTextWrapping() const noexcept {
    auto tryReadWrap = [](const UIElement* el, TextWrapping& outWrap) noexcept -> bool {
        if (!el) return false;
        const UnifiedValue v = el->GetValue(PropId_TextWrapping);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i == 1) outWrap = TextWrapping::Wrap;
            else if (*i == 2) outWrap = TextWrapping::WrapWithOverflow;
            else outWrap = TextWrapping::NoWrap;
            return true;
        }
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_TextWrapping, sv->view());
            if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
                if (*pi == 1) outWrap = TextWrapping::Wrap;
                else if (*pi == 2) outWrap = TextWrapping::WrapWithOverflow;
                else outWrap = TextWrapping::NoWrap;
                return true;
            }
        }
        return false;
    };
    TextWrapping w = TextWrapping::NoWrap;
    if (tryReadWrap(this, w)) return w;
    if (tryReadWrap(TemplatedParent(), w)) return w;
    return TextWrapping::NoWrap;
}

HorizontalAlignment ContentPresenter::GetEffectiveHorizontalContentAlignment() const noexcept {
    for (const UIElement* src : {static_cast<const UIElement*>(this),
                                 static_cast<const UIElement*>(TemplatedParent())}) {
        if (!src) continue;
        const UnifiedValue v = src->GetValue(PropId_HorizontalContentAlignment);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i >= 0 && *i <= 3) {
                return static_cast<HorizontalAlignment>(*i);
            }
        }
    }
    if (const UIElement* tp = TemplatedParent()) {
        if (tp->IsA<Button>() && !tp->IsA<CheckBox>() && !tp->IsA<RadioButton>()) {
            return HorizontalAlignment::Center;
        }
    }
    return HorizontalAlignment::Left;
}

VerticalAlignment ContentPresenter::GetEffectiveVerticalContentAlignment() const noexcept {
    for (const UIElement* src : {static_cast<const UIElement*>(this),
                                 static_cast<const UIElement*>(TemplatedParent())}) {
        if (!src) continue;
        const UnifiedValue v = src->GetValue(PropId_VerticalContentAlignment);
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            if (*i >= 0 && *i <= 3) {
                return static_cast<VerticalAlignment>(*i);
            }
        }
    }
    return VerticalAlignment::Center;
}

Point ContentPresenter::ComputeTextOrigin(const Rect& boxRect, Size textSize) const noexcept {
    const Thickness pad = GetPadding();
    const float innerX = boxRect.x + pad.left;
    const float innerY = boxRect.y + pad.top;
    const float innerW = std::max(0.0f, boxRect.width - (pad.left + pad.right));
    const float innerH = std::max(0.0f, boxRect.height - (pad.top + pad.bottom));

    float tx = innerX;
    switch (GetEffectiveHorizontalContentAlignment()) {
        case HorizontalAlignment::Left:
        case HorizontalAlignment::Stretch:
            tx = innerX;
            break;
        case HorizontalAlignment::Center:
            tx = innerX + std::max(0.0f, (innerW - textSize.width) * 0.5f);
            break;
        case HorizontalAlignment::Right:
            tx = innerX + std::max(0.0f, innerW - textSize.width);
            break;
    }

    float ty = innerY;
    switch (GetEffectiveVerticalContentAlignment()) {
        case VerticalAlignment::Top:
        case VerticalAlignment::Stretch:
            ty = innerY;
            break;
        case VerticalAlignment::Center:
            ty = innerY + std::max(0.0f, (innerH - textSize.height) * 0.5f);
            break;
        case VerticalAlignment::Bottom:
            ty = innerY + std::max(0.0f, innerH - textSize.height);
            break;
    }
    return Point{tx, ty};
}

Size ContentPresenter::MeasureCore(Size availableSize) noexcept {
    if (FirstChild() != nullptr) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const std::string text = GetEffectiveText();
    if (text.empty()) {
        return Size{padW, padH};
    }
    const float fontSize = GetEffectiveFontSize();
    const std::string family = GetEffectiveFontFamily();
    const TextWrapping wrapMode = GetEffectiveTextWrapping();
    const bool hasFiniteW = std::isfinite(availableSize.width) && (availableSize.width < kUnconstrainedSpace * 0.5f);
    const float maxTextW = (wrapMode != TextWrapping::NoWrap && hasFiniteW)
        ? std::max(1.0f, availableSize.width - padW)
        : kUnconstrainedSpace;
    const Size metrics = MeasureUtf8TextRun(text, fontSize, family, maxTextW, wrapMode);
    return Size{metrics.width + padW, metrics.height + padH};
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
    SetValue(PropId_StrokeThickness, MakeFloat(thickness), ValueSourceRank::Local);
}

float Path::GetStrokeThickness() const noexcept {
    float st = 0.0f;
    if (TryGetFloat(GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    if ((IsA(Polygon::StaticTypeClass()) || IsA(Polyline::StaticTypeClass())) && HasExplicitStroke()) {
        return 1.0f;
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
    if (IsA(Polygon::StaticTypeClass()) || IsA(Polyline::StaticTypeClass())) {
        return Stretch::None;
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
// P1-3 Geometric Primitives: Rectangle, Ellipse, Line, Polygon, Polyline
// ============================================================================

namespace {

[[nodiscard]] std::int32_t CoerceInt32Property(const UnifiedValue& v, std::int32_t fallbackVal) noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    float f = 0.0f;
    if (TryGetFloat(v, f) && std::isfinite(f)) {
        return static_cast<std::int32_t>(std::lround(f));
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_UniformColumns, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            return static_cast<std::int32_t>(*pi);
        }
    }
    return fallbackVal;
}

[[nodiscard]] bool CoerceBoolProperty(const UnifiedValue& v, bool fallbackVal) noexcept {
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return *i != 0;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_IsExpanded, sv->view());
        if (const auto* pb = std::get_if<bool>(&parsed)) {
            return *pb;
        }
    }
    return fallbackVal;
}

[[nodiscard]] std::uint32_t ExtractGradientBrushId(const UnifiedValue& v) noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i > 0) {
            return static_cast<std::uint32_t>(*i);
        }
    }
    return 0u;
}

[[nodiscard]] bool PointInRect(const Rect& r, Point p) noexcept {
    return r.width > 0.0f && r.height > 0.0f &&
           p.x >= r.x && p.x <= r.x + r.width &&
           p.y >= r.y && p.y <= r.y + r.height;
}

} // namespace

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

void Rectangle::SetFill(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Rectangle::GetFill() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Rectangle::HasFill() const noexcept {
    if (GetFillGradientBrushId() != 0u) {
        return true;
    }
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return c->a > 0.0f;
    }
    return false;
}

void Rectangle::SetFillGradientBrushId(std::uint32_t brushId) {
    if (brushId == 0u) {
        (void)ClearValue(PropId_BackgroundGradientId, ValueSourceRank::Local);
    } else {
        SetValue(PropId_BackgroundGradientId, value::FromInt(static_cast<std::int64_t>(brushId)), ValueSourceRank::Local);
    }
}

std::uint32_t Rectangle::GetFillGradientBrushId() const noexcept {
    return ExtractGradientBrushId(GetValue(PropId_BackgroundGradientId));
}

void Rectangle::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Rectangle::GetStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Rectangle::HasStroke() const noexcept {
    if (GetStrokeGradientBrushId() != 0u) {
        return true;
    }
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return c->a > 0.0f;
    }
    return false;
}

std::uint32_t Rectangle::GetStrokeGradientBrushId() const noexcept {
    return ExtractGradientBrushId(GetValue(PropId_BorderGradientId));
}

void Rectangle::SetStrokeThickness(float thickness) {
    SetValue(PropId_StrokeThickness, MakeFloat(std::max(0.0f, thickness)), ValueSourceRank::Local);
}

float Rectangle::GetStrokeThickness() const noexcept {
    float st = 0.0f;
    if (TryGetFloat(GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    return HasStroke() ? 1.0f : 0.0f;
}

Size Rectangle::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    return Size{0.0f, 0.0f};
}

Ellipse::Ellipse() {
    EnsureControlTypesRegistered();
}

void Ellipse::SetFill(const Color& color) {
    SetValue(PropId_Background, value::FromColor(color), ValueSourceRank::Local);
}

Color Ellipse::GetFill() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Ellipse::HasFill() const noexcept {
    if (GetFillGradientBrushId() != 0u) {
        return true;
    }
    if (auto c = value::TryGetColor(GetValue(PropId_Background))) {
        return c->a > 0.0f;
    }
    return false;
}

void Ellipse::SetFillGradientBrushId(std::uint32_t brushId) {
    if (brushId == 0u) {
        (void)ClearValue(PropId_BackgroundGradientId, ValueSourceRank::Local);
    } else {
        SetValue(PropId_BackgroundGradientId, value::FromInt(static_cast<std::int64_t>(brushId)), ValueSourceRank::Local);
    }
}

std::uint32_t Ellipse::GetFillGradientBrushId() const noexcept {
    return ExtractGradientBrushId(GetValue(PropId_BackgroundGradientId));
}

void Ellipse::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
}

Color Ellipse::GetStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return *c;
    }
    return Color{0.0f, 0.0f, 0.0f, 0.0f};
}

bool Ellipse::HasStroke() const noexcept {
    if (auto c = value::TryGetColor(GetValue(PropId_BorderBrush))) {
        return c->a > 0.0f;
    }
    return false;
}

void Ellipse::SetStrokeThickness(float thickness) {
    SetValue(PropId_StrokeThickness, MakeFloat(std::max(0.0f, thickness)), ValueSourceRank::Local);
}

float Ellipse::GetStrokeThickness() const noexcept {
    float st = 0.0f;
    if (TryGetFloat(GetValue(PropId_StrokeThickness), st)) {
        return std::max(0.0f, st);
    }
    if (auto t = value::TryGetThickness(GetValue(PropId_BorderThickness))) {
        return std::max(0.0f, t->left);
    }
    return HasStroke() ? 1.0f : 0.0f;
}

Size Ellipse::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    return Size{0.0f, 0.0f};
}

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

void Line::SetStroke(const Color& color) {
    SetValue(PropId_BorderBrush, value::FromColor(color), ValueSourceRank::Local);
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

void Line::SetStrokeThickness(float thickness) {
    SetValue(PropId_StrokeThickness, MakeFloat(std::max(0.0f, thickness)), ValueSourceRank::Local);
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

Polygon::Polygon() {
    EnsureControlTypesRegistered();
}

void Polygon::SetPoints(std::string_view pointsText) {
    SetValue(PropId_ShapePoints, value::FromString(std::string(pointsText)), ValueSourceRank::Local);
}

std::string Polygon::GetPoints() const {
    const UnifiedValue v = GetValue(PropId_ShapePoints);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void Polygon::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Path::OnPropertyChanged(propId, newValue);
    if (propId == PropId_ShapePoints) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const std::string svg = ConvertPointsToSvgPath(sv->view(), true);
            if (!svg.empty()) {
                (void)AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(svg);
            }
            SetData(svg);
        } else if (std::holds_alternative<std::monostate>(newValue)) {
            SetData({});
        }
    }
}

Polyline::Polyline() {
    EnsureControlTypesRegistered();
}

void Polyline::SetPoints(std::string_view pointsText) {
    SetValue(PropId_ShapePoints, value::FromString(std::string(pointsText)), ValueSourceRank::Local);
}

std::string Polyline::GetPoints() const {
    const UnifiedValue v = GetValue(PropId_ShapePoints);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void Polyline::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Path::OnPropertyChanged(propId, newValue);
    if (propId == PropId_ShapePoints) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const std::string svg = ConvertPointsToSvgPath(sv->view(), false);
            if (!svg.empty()) {
                (void)AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(svg);
            }
            SetData(svg);
        } else if (std::holds_alternative<std::monostate>(newValue)) {
            SetData({});
        }
    }
}

// ============================================================================
// P1-3 Composite Layout & Interactive Controls
// ============================================================================

UniformGrid::UniformGrid() {
    EnsureControlTypesRegistered();
}

void UniformGrid::SetRows(std::int32_t rows) {
    SetValue(PropId_UniformRows, value::FromInt(std::max<std::int32_t>(0, rows)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetRows() const noexcept {
    return std::max<std::int32_t>(0, CoerceInt32Property(GetValue(PropId_UniformRows), 0));
}

void UniformGrid::SetColumns(std::int32_t cols) {
    SetValue(PropId_UniformColumns, value::FromInt(std::max<std::int32_t>(0, cols)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetColumns() const noexcept {
    return std::max<std::int32_t>(0, CoerceInt32Property(GetValue(PropId_UniformColumns), 0));
}

void UniformGrid::SetFirstColumn(std::int32_t firstCol) {
    SetValue(PropId_FirstColumn, value::FromInt(std::max<std::int32_t>(0, firstCol)), ValueSourceRank::Local);
}

std::int32_t UniformGrid::GetFirstColumn() const noexcept {
    return std::max<std::int32_t>(0, CoerceInt32Property(GetValue(PropId_FirstColumn), 0));
}

void UniformGrid::ComputeEffectiveGridDimensions(
    std::size_t nonCollapsedChildCount,
    std::int32_t& outRows,
    std::int32_t& outCols,
    std::int32_t& outFirstCol) const noexcept {
    std::int32_t rows = GetRows();
    std::int32_t cols = GetColumns();
    std::int32_t firstCol = GetFirstColumn();

    if (cols > 0 && firstCol >= cols) {
        firstCol = 0;
    }

    if (nonCollapsedChildCount == 0u) {
        outRows = std::max<std::int32_t>(1, rows);
        outCols = std::max<std::int32_t>(1, cols);
        outFirstCol = 0;
        return;
    }

    const std::size_t totalSlots = nonCollapsedChildCount + static_cast<std::size_t>(firstCol);
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
        const std::size_t adjustedSlots = nonCollapsedChildCount + static_cast<std::size_t>(firstCol);
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

ProgressBar::ProgressBar() {
    EnsureControlTypesRegistered();
}

void ProgressBar::SetMinimum(float minVal) {
    SetValue(PropId_Minimum, MakeFloat(minVal), ValueSourceRank::Local);
}

float ProgressBar::GetMinimum() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_Minimum), v) ? v : 0.0f;
}

void ProgressBar::SetMaximum(float maxVal) {
    SetValue(PropId_Maximum, MakeFloat(maxVal), ValueSourceRank::Local);
}

float ProgressBar::GetMaximum() const noexcept {
    float v = 100.0f;
    return TryGetFloat(GetValue(PropId_Maximum), v) ? v : 100.0f;
}

void ProgressBar::SetProgressValue(float val) {
    SetValue(PropId_Value, MakeFloat(val), ValueSourceRank::Local);
}

float ProgressBar::GetProgressValue() const noexcept {
    float v = 0.0f;
    return TryGetFloat(GetValue(PropId_Value), v) ? v : 0.0f;
}

void ProgressBar::SetOrientation(Orientation o) {
    SetValue(PropId_Orientation, value::FromInt(static_cast<std::int64_t>(o)), ValueSourceRank::Local);
}

Orientation ProgressBar::GetOrientation() const noexcept {
    const UnifiedValue v = GetValue(PropId_Orientation);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_Orientation, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            return (*pi == 0) ? Orientation::Vertical : Orientation::Horizontal;
        }
    }
    return Orientation::Horizontal;
}

void ProgressBar::SetIsIndeterminate(bool indeterminate) {
    SetValue(PropId_IsIndeterminate, value::FromBool(indeterminate), ValueSourceRank::Local);
}

bool ProgressBar::IsIndeterminate() const noexcept {
    return CoerceBoolProperty(GetValue(PropId_IsIndeterminate), false);
}

float ProgressBar::NormalizedProgress() const noexcept {
    if (IsIndeterminate()) {
        return 0.35f;
    }
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float curV = GetProgressValue();
    if (maxV <= minV) {
        return 0.0f;
    }
    return std::clamp((curV - minV) / (maxV - minV), 0.0f, 1.0f);
}

Rect ProgressBar::ComputeFillBarLocalRect() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float innerX = r.x + p.left;
    const float innerY = r.y + p.top;
    const float innerW = std::max(0.0f, r.width - (p.left + p.right));
    const float innerH = std::max(0.0f, r.height - (p.top + p.bottom));
    const float ratio = NormalizedProgress();

    if (IsIndeterminate()) {
        if (GetOrientation() == Orientation::Vertical) {
            const float segH = innerH * 0.35f;
            return Rect{innerX, innerY + (innerH - segH) * 0.5f, innerW, segH};
        }
        const float segW = innerW * 0.35f;
        return Rect{innerX + (innerW - segW) * 0.5f, innerY, segW, innerH};
    }

    if (GetOrientation() == Orientation::Vertical) {
        const float fillH = innerH * ratio;
        return Rect{innerX, innerY + (innerH - fillH), innerW, fillH};
    }
    return Rect{innerX, innerY, innerW * ratio, innerH};
}

Size ProgressBar::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    if (GetOrientation() == Orientation::Vertical) {
        return Size{16.0f + p.left + p.right, 120.0f + p.top + p.bottom};
    }
    return Size{120.0f + p.left + p.right, 16.0f + p.top + p.bottom};
}

HeaderedContentControl::HeaderedContentControl() {
    EnsureControlTypesRegistered();
}

void HeaderedContentControl::SetHeader(std::string_view header) {
    SetValue(PropId_Header, value::FromString(std::string(header)), ValueSourceRank::Local);
}

std::string HeaderedContentControl::GetHeader() const {
    const UnifiedValue v = GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    const UnifiedValue tv = GetValue(PropId_Title);
    if (const auto* sv = std::get_if<StringValue>(&tv)) {
        return sv->str();
    }
    return {};
}

void HeaderedContentControl::SetHeaderElement(UIElement* headerElem) noexcept {
    if (headerElement_ != headerElem) {
        headerElement_ = headerElem;
        InvalidateLayout();
        InvalidateRender();
    }
}

UIElement* HeaderedContentControl::GetHeaderElement() const noexcept {
    if (headerElement_ && headerElement_->Parent() == this) {
        return headerElement_;
    }
    return nullptr;
}

bool HeaderedContentControl::HasHeader() const noexcept {
    return GetHeaderElement() != nullptr || !GetHeader().empty();
}

Size HeaderedContentControl::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* hdrElem = GetHeaderElement();
    float hdrW = 0.0f;
    float hdrH = 0.0f;
    if (hdrElem) {
        hdrElem->Measure(Size{innerAvailW, innerAvailH});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = hdrElem->DesiredSize().width;
            hdrH = std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = ts.width + 8.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, ts.height + 8.0f);
        }
    }

    const float bodyAvailH = std::max(0.0f, innerAvailH - hdrH);
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{innerAvailW, bodyAvailH});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width);
            bodyH = std::max(bodyH, c->DesiredSize().height);
        }
    }

    return Size{
        std::max(hdrW, bodyW) + padW,
        hdrH + bodyH + padH};
}

void HeaderedContentControl::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* hdrElem = GetHeaderElement();
    float hdrH = 0.0f;
    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrH = std::min(innerH, std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height));
            headerLocalRect_ = Rect{startX, startY, innerW, hdrH};
            hdrElem->Arrange(headerLocalRect_);
        } else {
            headerLocalRect_ = Rect{startX, startY, 0.0f, 0.0f};
            hdrElem->Arrange(headerLocalRect_);
        }
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        hdrH = std::min(innerH, std::max(kDefaultHeaderBarHeight, fontSize * 1.25f + 8.0f));
        headerLocalRect_ = Rect{startX, startY, innerW, hdrH};
    } else {
        headerLocalRect_ = Rect{startX, startY, 0.0f, 0.0f};
    }

    contentLocalRect_ = Rect{
        startX,
        startY + hdrH,
        innerW,
        std::max(0.0f, innerH - hdrH)};
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(contentLocalRect_);
    }
}

GroupBox::GroupBox() {
    EnsureControlTypesRegistered();
}

Size GroupBox::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float frameInsetX = (p.left + p.right > 0.0f) ? (p.left + p.right) : 16.0f;
    const float frameInsetY = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 16.0f;
    const float innerAvailW = std::max(0.0f, availableSize.width - frameInsetX);

    UIElement* hdrElem = GetHeaderElement();
    float hdrW = 0.0f;
    float hdrH = 0.0f;
    if (hdrElem) {
        hdrElem->Measure(Size{std::max(0.0f, availableSize.width - 8.0f), availableSize.height});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = hdrElem->DesiredSize().width + 12.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height + 4.0f);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = ts.width + 16.0f;
            hdrH = std::max(kDefaultHeaderBarHeight, ts.height + 8.0f);
        }
    }

    const float bodyAvailH = std::max(0.0f, availableSize.height - hdrH - frameInsetY);
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{innerAvailW, bodyAvailH});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width);
            bodyH = std::max(bodyH, c->DesiredSize().height);
        }
    }

    return Size{
        std::max(hdrW, bodyW + frameInsetX),
        hdrH + bodyH + frameInsetY};
}

void GroupBox::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float padL = (p.left > 0.0f) ? p.left : 8.0f;
    const float padT = (p.top > 0.0f) ? p.top : 8.0f;
    const float padR = (p.right > 0.0f) ? p.right : 8.0f;
    const float padB = (p.bottom > 0.0f) ? p.bottom : 8.0f;

    UIElement* hdrElem = GetHeaderElement();
    float hdrH = 0.0f;
    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrH = std::min(finalRect.height, std::max(kDefaultHeaderBarHeight, hdrElem->DesiredSize().height + 4.0f));
            headerLocalRect_ = Rect{finalRect.x + 6.0f, finalRect.y, std::max(0.0f, finalRect.width - 12.0f), hdrH};
            hdrElem->Arrange(headerLocalRect_);
        } else {
            headerLocalRect_ = Rect{finalRect.x, finalRect.y, 0.0f, 0.0f};
            hdrElem->Arrange(headerLocalRect_);
        }
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        hdrH = std::min(finalRect.height, std::max(kDefaultHeaderBarHeight, fontSize * 1.25f + 8.0f));
        headerLocalRect_ = Rect{finalRect.x + 6.0f, finalRect.y, std::max(0.0f, finalRect.width - 12.0f), hdrH};
    } else {
        headerLocalRect_ = Rect{finalRect.x, finalRect.y, 0.0f, 0.0f};
    }

    bodyBorderLocalRect_ = Rect{
        finalRect.x,
        finalRect.y + hdrH,
        finalRect.width,
        std::max(0.0f, finalRect.height - hdrH)};
    contentLocalRect_ = Rect{
        bodyBorderLocalRect_.x + padL,
        bodyBorderLocalRect_.y + padT,
        std::max(0.0f, bodyBorderLocalRect_.width - (padL + padR)),
        std::max(0.0f, bodyBorderLocalRect_.height - (padT + padB))};

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(contentLocalRect_);
    }
}

Expander::Expander() {
    EnsureControlTypesRegistered();
}

void Expander::SetIsExpanded(bool expanded) {
    SetValue(PropId_IsExpanded, value::FromBool(expanded), ValueSourceRank::Local);
}

bool Expander::IsExpanded() const noexcept {
    return CoerceBoolProperty(GetValue(PropId_IsExpanded), false);
}

void Expander::ToggleExpanded() {
    const bool next = !IsExpanded();
    SetIsExpanded(next);
    (void)NotifyTwoWayPropertyMutated(PropId_IsExpanded);
    if (expandedChangedCallback_) {
        expandedChangedCallback_(*this, next);
    }
}

void Expander::SetExpandDirection(ExpandDirection dir) {
    SetValue(PropId_ExpandDirection, value::FromInt(static_cast<std::int64_t>(dir)), ValueSourceRank::Local);
}

ExpandDirection Expander::GetExpandDirection() const noexcept {
    const UnifiedValue v = GetValue(PropId_ExpandDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<ExpandDirection>(*i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_ExpandDirection, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi >= 0 && *pi <= 3) {
                return static_cast<ExpandDirection>(*pi);
            }
        }
    }
    return ExpandDirection::Down;
}

bool Expander::IsBodyChildHidden(const UIElement* child) const noexcept {
    if (!child || child->Parent() != this) {
        return false;
    }
    if (child == GetHeaderElement()) {
        return false;
    }
    return !IsExpanded();
}

void Expander::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    HeaderedContentControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsExpanded || propId == PropId_ExpandDirection) {
        InvalidateLayout();
        MarkSubtreeRenderDirtyRecursive(this);
    }
}

bool Expander::ActivateFromInput(RuntimeError* /*error*/) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    ToggleExpanded();
    return true;
}

bool Expander::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    if (PointInRect(headerLocalRect_, localPoint)) {
        ToggleExpanded();
        return true;
    }
    return false;
}

bool Expander::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        ToggleExpanded();
        return true;
    }
    return false;
}

Size Expander::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    UIElement* hdrElem = GetHeaderElement();
    const ExpandDirection dir = GetExpandDirection();
    const bool horizontalExpand = (dir == ExpandDirection::Left || dir == ExpandDirection::Right);

    float hdrW = kDefaultExpanderHeaderHeight;
    float hdrH = kDefaultExpanderHeaderHeight;
    if (hdrElem) {
        hdrElem->Measure(Size{std::max(0.0f, innerAvailW - 22.0f), innerAvailH});
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrW = std::max(hdrW, hdrElem->DesiredSize().width + 26.0f);
            hdrH = std::max(hdrH, hdrElem->DesiredSize().height + 6.0f);
        }
    } else {
        const std::string hdrText = GetHeader();
        if (!hdrText.empty()) {
            float fontSize = 14.0f;
            (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
            const Size ts = MeasureUtf8TextRun(hdrText, fontSize);
            hdrW = std::max(hdrW, ts.width + 28.0f);
            hdrH = std::max(hdrH, ts.height + 10.0f);
        }
    }

    if (!IsExpanded()) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c != hdrElem) {
                c->Measure(Size{0.0f, 0.0f});
            }
        }
        return Size{hdrW + padW, hdrH + padH};
    }

    const Size bodyAvail = horizontalExpand
        ? Size{std::max(0.0f, innerAvailW - hdrW), innerAvailH}
        : Size{innerAvailW, std::max(0.0f, innerAvailH - hdrH)};
    float bodyW = 0.0f;
    float bodyH = 0.0f;
    const float contentInset = 8.0f;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Measure(Size{
            std::max(0.0f, bodyAvail.width - contentInset),
            std::max(0.0f, bodyAvail.height - contentInset)});
        if (c->GetVisibility() != Visibility::Collapsed) {
            bodyW = std::max(bodyW, c->DesiredSize().width + contentInset);
            bodyH = std::max(bodyH, c->DesiredSize().height + contentInset);
        }
    }

    if (horizontalExpand) {
        return Size{hdrW + bodyW + padW, std::max(hdrH, bodyH) + padH};
    }
    return Size{std::max(hdrW, bodyW) + padW, hdrH + bodyH + padH};
}

void Expander::ArrangeCore(Rect finalRect) noexcept {
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    UIElement* hdrElem = GetHeaderElement();
    const ExpandDirection dir = GetExpandDirection();
    const bool expanded = IsExpanded();

    float hdrBarH = kDefaultExpanderHeaderHeight;
    float hdrBarW = kDefaultExpanderHeaderHeight;
    if (hdrElem && hdrElem->GetVisibility() != Visibility::Collapsed) {
        hdrBarH = std::max(hdrBarH, hdrElem->DesiredSize().height + 6.0f);
        hdrBarW = std::max(hdrBarW, hdrElem->DesiredSize().width + 26.0f);
    } else if (!GetHeader().empty()) {
        float fontSize = 14.0f;
        (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
        const Size ts = MeasureUtf8TextRun(GetHeader(), fontSize);
        hdrBarH = std::max(hdrBarH, ts.height + 10.0f);
        hdrBarW = std::max(hdrBarW, ts.width + 28.0f);
    }

    switch (dir) {
        case ExpandDirection::Down: {
            const float hh = std::min(innerH, hdrBarH);
            headerLocalRect_ = Rect{startX, startY, innerW, hh};
            contentLocalRect_ = expanded
                ? Rect{startX, startY + hh, innerW, std::max(0.0f, innerH - hh)}
                : Rect{startX, startY + hh, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Up: {
            const float hh = std::min(innerH, hdrBarH);
            headerLocalRect_ = Rect{startX, startY + innerH - hh, innerW, hh};
            contentLocalRect_ = expanded
                ? Rect{startX, startY, innerW, std::max(0.0f, innerH - hh)}
                : Rect{startX, startY, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Right: {
            const float hw = std::min(innerW, hdrBarW);
            headerLocalRect_ = Rect{startX, startY, hw, innerH};
            contentLocalRect_ = expanded
                ? Rect{startX + hw, startY, std::max(0.0f, innerW - hw), innerH}
                : Rect{startX + hw, startY, 0.0f, 0.0f};
            break;
        }
        case ExpandDirection::Left: {
            const float hw = std::min(innerW, hdrBarW);
            headerLocalRect_ = Rect{startX + innerW - hw, startY, hw, innerH};
            contentLocalRect_ = expanded
                ? Rect{startX, startY, std::max(0.0f, innerW - hw), innerH}
                : Rect{startX, startY, 0.0f, 0.0f};
            break;
        }
    }

    if (hdrElem) {
        if (hdrElem->GetVisibility() != Visibility::Collapsed) {
            hdrElem->Arrange(Rect{
                headerLocalRect_.x + 22.0f,
                headerLocalRect_.y + 3.0f,
                std::max(0.0f, headerLocalRect_.width - 26.0f),
                std::max(0.0f, headerLocalRect_.height - 6.0f)});
        } else {
            hdrElem->Arrange(Rect{headerLocalRect_.x, headerLocalRect_.y, 0.0f, 0.0f});
        }
    }

    const Rect bodySlot = expanded
        ? Rect{
              contentLocalRect_.x + 4.0f,
              contentLocalRect_.y + 4.0f,
              std::max(0.0f, contentLocalRect_.width - 8.0f),
              std::max(0.0f, contentLocalRect_.height - 8.0f)}
        : Rect{contentLocalRect_.x, contentLocalRect_.y, 0.0f, 0.0f};

    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c == hdrElem) {
            continue;
        }
        c->Arrange(bodySlot);
    }
}

TabItem::TabItem() {
    EnsureControlTypesRegistered();
}

void TabItem::SetIsSelected(bool selected) {
    SetValue(PropId_IsChecked, value::FromBool(selected), ValueSourceRank::Local);
}

bool TabItem::IsSelected() const noexcept {
    return CoerceBoolProperty(GetValue(PropId_IsChecked), false);
}

Size TabItem::MeasureCore(Size availableSize) noexcept {
    if (Parent() && Parent()->IsA(TabControl::StaticTypeClass())) {
        const Thickness p = GetPadding();
        const float padW = p.left + p.right;
        const float padH = p.top + p.bottom;
        const Size childAvail{
            std::max(0.0f, availableSize.width - padW),
            std::max(0.0f, availableSize.height - padH)};
        UIElement* hdrElem = GetHeaderElement();
        float maxW = 0.0f;
        float maxH = 0.0f;
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c == hdrElem) {
                continue;
            }
            c->Measure(childAvail);
            if (c->GetVisibility() != Visibility::Collapsed) {
                maxW = std::max(maxW, c->DesiredSize().width);
                maxH = std::max(maxH, c->DesiredSize().height);
            }
        }
        return Size{maxW + padW, maxH + padH};
    }
    return HeaderedContentControl::MeasureCore(availableSize);
}

void TabItem::ArrangeCore(Rect finalRect) noexcept {
    if (Parent() && Parent()->IsA(TabControl::StaticTypeClass())) {
        const Thickness p = GetPadding();
        contentLocalRect_ = Rect{
            finalRect.x + p.left,
            finalRect.y + p.top,
            std::max(0.0f, finalRect.width - (p.left + p.right)),
            std::max(0.0f, finalRect.height - (p.top + p.bottom))};
        UIElement* hdrElem = GetHeaderElement();
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            if (c == hdrElem) {
                continue;
            }
            c->Arrange(contentLocalRect_);
        }
        return;
    }
    HeaderedContentControl::ArrangeCore(finalRect);
}

TabControl::TabControl() {
    EnsureControlTypesRegistered();
}

std::size_t TabControl::TabCount() const noexcept {
    std::size_t count = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() != Visibility::Collapsed) {
            ++count;
        }
    }
    return count;
}

UIElement* TabControl::GetTabElement(std::size_t index) const noexcept {
    std::size_t i = 0u;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (i == index) {
            return c;
        }
        ++i;
    }
    return nullptr;
}

std::string TabControl::GetTabHeaderText(std::size_t index) const {
    const UIElement* elem = GetTabElement(index);
    if (!elem) {
        return {};
    }
    if (const auto* ti = elem->DynamicCast<HeaderedContentControl>()) {
        const std::string h = ti->GetHeader();
        if (!h.empty()) {
            return h;
        }
    }
    const UnifiedValue hv = elem->GetValue(PropId_Header);
    if (const auto* sv = std::get_if<StringValue>(&hv); sv && !sv->view().empty()) {
        return sv->str();
    }
    const UnifiedValue tv = elem->GetValue(PropId_Title);
    if (const auto* sv = std::get_if<StringValue>(&tv); sv && !sv->view().empty()) {
        return sv->str();
    }
    return "Tab " + std::to_string(index + 1u);
}

void TabControl::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t TabControl::GetSelectedIndex() const noexcept {
    const std::size_t count = TabCount();
    if (count == 0u) {
        return CoerceInt32Property(GetValue(PropId_SelectedIndex), -1);
    }
    const std::int32_t raw = CoerceInt32Property(GetValue(PropId_SelectedIndex), 0);
    return std::clamp<std::int32_t>(raw, 0, static_cast<std::int32_t>(count - 1u));
}

bool TabControl::SelectTabInteractive(std::int32_t index) {
    const std::size_t count = TabCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    SyncTabSelectionStates();
    (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
    if (clamped != prev && selectionChangedCallback_) {
        selectionChangedCallback_(*this, clamped);
    }
    return true;
}

void TabControl::SetTabStripPlacement(TabStripPlacement placement) {
    SetValue(PropId_TabStripPlacement, value::FromInt(static_cast<std::int64_t>(placement)), ValueSourceRank::Local);
}

TabStripPlacement TabControl::GetTabStripPlacement() const noexcept {
    const UnifiedValue v = GetValue(PropId_TabStripPlacement);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<TabStripPlacement>(*i);
        }
    }
    return TabStripPlacement::Top;
}

Rect TabControl::GetTabHeaderLocalRect(std::size_t index) const noexcept {
    if (index < tabHeaderRects_.size()) {
        return tabHeaderRects_[index];
    }
    return Rect{};
}

std::int32_t TabControl::HitTestTabHeaderIndex(Point localPoint) const noexcept {
    for (std::size_t i = 0u; i < tabHeaderRects_.size(); ++i) {
        if (PointInRect(tabHeaderRects_[i], localPoint)) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

bool TabControl::IsChildInactiveTabPage(const UIElement* child) const noexcept {
    if (!child || child->Parent() != this) {
        return false;
    }
    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t idx = 0;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (c == child) {
            return idx != activeIdx;
        }
        ++idx;
    }
    return true;
}

void TabControl::SyncTabSelectionStates() noexcept {
    if (syncingTabs_) {
        return;
    }
    syncingTabs_ = true;
    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t idx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        if (auto* ti = c->DynamicCast<TabItem>()) {
            ti->SetIsSelected(idx == activeIdx);
        }
        ++idx;
    }
    syncingTabs_ = false;
}

void TabControl::MarkExtraValuesInPools() const noexcept {
    for (const auto& item : items_) {
        if (item.isTraceable()) {
            rs_rt::markValueInPools(item);
        }
    }
}

UnifiedValue TabControl::NormalizeTabItemValue(const UnifiedValue& raw) {
    UnifiedValue out = raw;
    if (const auto* strRef = std::get_if<ObjectRef>(&out)) {
        if (strRef->kind == ObjectKind::String && domains_) {
            if (const auto sv = domains_->stringViewAcrossDomains(*strRef)) {
                out = value::FromString(std::string(*sv));
            }
        }
    }
    if (out.isTraceable()) {
        rs_rt::pinValueInPools(out);
        EnsureRegisteredInValuePoolSet();
    }
    return out;
}

void TabControl::ReleaseTabChildResources(UIElement* child) {
    if (!child) return;
    AnimationEngine* effAnim = animationEngine_
        ? animationEngine_
        : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
    if (effAnim) {
        effAnim->StopSubtreeAnimations(child, false);
    }
    if (bindingEngine_) {
        (void)bindingEngine_->UnbindSubtree(child);
    }
    if (styleEngine_) {
        (void)styleEngine_->UnbindSubtreeStyles(child);
    }
}

IntrusivePtr<UIElement> TabControl::InstantiateTabItemForValue(const UnifiedValue& itemVal) {
    auto tab = MakeIntrusive<TabItem>();
    tab->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
    const UnifiedValue tplKeyVal = GetValue(PropId_ItemTemplate);
    const AxbPackage* tplPkg = nullptr;
    if (const auto* sv = std::get_if<StringValue>(&tplKeyVal); sv && !sv->view().empty()) {
        tplPkg = AxbTemplateRegistry::Instance().FindDataTemplate(sv->view());
    }
    if (!tplPkg) {
        const UnifiedValue ctKeyVal = GetValue(PropId_ContentTemplate);
        if (const auto* sv = std::get_if<StringValue>(&ctKeyVal); sv && !sv->view().empty()) {
            tplPkg = AxbTemplateRegistry::Instance().FindDataTemplate(sv->view());
        }
    }
    if (tplPkg) {
        tab->SetNamescopeBoundary(true);
        ObjectRef itemObj{};
        if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
            itemObj = *ref;
        }
        AnimationEngine* effAnim = animationEngine_
            ? animationEngine_
            : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
        (void)MountAxbTemplate(tab.Get(), *tplPkg, domains_, bindingEngine_, itemObj, nullptr, nullptr, effAnim);
        if (styleEngine_) {
            styleEngine_->RegisterPackageStyles(*tplPkg);
            (void)styleEngine_->BindSubtreeStyles(tab.Get());
            (void)styleEngine_->EvaluateSubtree(tab.Get());
        }
    } else {
        tab->SetHeader(rs_rt::valueToString(itemVal));
    }
    return tab;
}

void TabControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    items_.clear();
    items_.reserve(items.size());
    for (const auto& item : items) {
        items_.push_back(NormalizeTabItemValue(item));
    }
    while (UIElement* ch = FirstChild()) {
        ReleaseTabChildResources(ch);
        RemoveChild(ch, registry, uiHeap);
    }
    for (const auto& item : items_) {
        IntrusivePtr<UIElement> tab = InstantiateTabItemForValue(item);
        if (tab) {
            AddChild(tab.Get(), registry, uiHeap);
        }
    }
    if (items_.empty()) {
        SetSelectedIndex(-1);
    } else {
        const std::int32_t rawSel = CoerceInt32Property(GetValue(PropId_SelectedIndex), 0);
        if (rawSel >= static_cast<std::int32_t>(items_.size())) {
            SetSelectedIndex(static_cast<std::int32_t>(items_.size() - 1u));
        } else if (rawSel < 0) {
            SetSelectedIndex(0);
        }
    }
    SyncTabSelectionStates();
    InvalidateLayout();
}

bool TabControl::ApplyIncrementalChanges(
    const CollectionChangeRecord* changes,
    std::size_t changeCount) {
    if (!changes || changeCount == 0u) {
        return false;
    }
    for (std::size_t c = 0u; c < changeCount; ++c) {
        if (changes[c].kind == CollectionChangeKind::Reset) {
            return false;
        }
    }
    NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
    std::int32_t selIdx = CoerceInt32Property(GetValue(PropId_SelectedIndex), -1);

    for (std::size_t c = 0u; c < changeCount; ++c) {
        const auto& rec = changes[c];
        switch (rec.kind) {
            case CollectionChangeKind::Add: {
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_.push_back(norm);
                if (auto tab = InstantiateTabItemForValue(norm)) {
                    AddChild(tab.Get(), registry, uiHeap);
                }
                break;
            }
            case CollectionChangeKind::Insert: {
                const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), norm);
                if (auto tab = InstantiateTabItemForValue(norm)) {
                    InsertChildAt(tab.Get(), idx, registry, uiHeap);
                }
                if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                    ++selIdx;
                }
                break;
            }
            case CollectionChangeKind::RemoveAt: {
                if (rec.index >= items_.size()) break;
                const std::size_t idx = rec.index;
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                UIElement* targetChild = FirstChild();
                for (std::size_t k = 0u; k < idx && targetChild != nullptr; ++k) {
                    targetChild = targetChild->NextSibling();
                }
                if (targetChild) {
                    ReleaseTabChildResources(targetChild);
                    RemoveChild(targetChild, registry, uiHeap);
                }
                if (selIdx >= 0) {
                    if (idx < static_cast<std::size_t>(selIdx)) {
                        --selIdx;
                    } else if (idx == static_cast<std::size_t>(selIdx)) {
                        selIdx = items_.empty() ? -1 : std::min<std::int32_t>(selIdx, static_cast<std::int32_t>(items_.size() - 1u));
                    }
                }
                break;
            }
            case CollectionChangeKind::Replace: {
                if (rec.index >= items_.size()) break;
                const std::size_t idx = rec.index;
                const UnifiedValue norm = NormalizeTabItemValue(rec.newValue);
                items_[idx] = norm;
                UIElement* targetChild = FirstChild();
                for (std::size_t k = 0u; k < idx && targetChild != nullptr; ++k) {
                    targetChild = targetChild->NextSibling();
                }
                if (targetChild) {
                    targetChild->SetValue(PropId_DataContext, norm, ValueSourceRank::Local);
                    if (bindingEngine_) {
                        ObjectRef itemObj{};
                        if (const auto* ref = std::get_if<ObjectRef>(&norm)) itemObj = *ref;
                        (void)bindingEngine_->RebindSubtreeRootObject(targetChild, itemObj);
                    }
                    if (auto* ti = targetChild->DynamicCast<TabItem>()) {
                        if (ti->ChildCount() == 0u) {
                            ti->SetHeader(rs_rt::valueToString(norm));
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Reset:
                return false;
            case CollectionChangeKind::Clear: {
                items_.clear();
                while (UIElement* ch = FirstChild()) {
                    ReleaseTabChildResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
                selIdx = -1;
                break;
            }
        }
    }

    SetSelectedIndex(selIdx);
    SyncTabSelectionStates();
    InvalidateLayout();
    MarkSubtreeRenderDirtyRecursive(this);
    return true;
}

void TabControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Panel::OnPropertyChanged(propId, newValue);
    if (propId == PropId_ItemsSource) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (const auto* objRef = std::get_if<ObjectRef>(&newValue)) {
            if (objRef->valid() && domains_) {
                if (auto* heap = domains_->heapForObject(*objRef)) {
                    const ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(*heap, *objRef);
                    if (arrRef.valid()) {
                        const std::size_t len = heap->arrayLength(arrRef).value_or(0);
                        std::vector<UnifiedValue> extracted;
                        extracted.reserve(len);
                        for (std::size_t i = 0; i < len; ++i) {
                            extracted.push_back(heap->arrayGet(arrRef, i).value_or(UnifiedValue{}));
                        }
                        SetItemsSource(extracted, registry, uiHeap);
                        return;
                    }
                }
            }
        } else if (std::holds_alternative<std::monostate>(newValue) ||
                   std::holds_alternative<NullObject>(newValue) ||
                   std::holds_alternative<NullArray>(newValue)) {
            SetItemsSource({}, registry, uiHeap);
            return;
        }
    }
    if (propId == PropId_SelectedIndex || propId == PropId_TabStripPlacement) {
        SyncTabSelectionStates();
        InvalidateLayout();
        MarkSubtreeRenderDirtyRecursive(this);
    }
}

bool TabControl::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::int32_t hitTab = HitTestTabHeaderIndex(localPoint);
    if (hitTab >= 0) {
        return SelectTabInteractive(hitTab);
    }
    return false;
}

bool TabControl::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TabCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t cur = GetSelectedIndex();
    if (key == KeyCode::Right || key == KeyCode::Down) {
        const std::int32_t next = std::min<std::int32_t>(
            static_cast<std::int32_t>(count - 1u),
            cur + 1);
        return SelectTabInteractive(next);
    }
    if (key == KeyCode::Left || key == KeyCode::Up) {
        const std::int32_t prev = std::max<std::int32_t>(0, cur - 1);
        return SelectTabInteractive(prev);
    }
    return false;
}

Size TabControl::MeasureCore(Size availableSize) noexcept {
    SyncTabSelectionStates();
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    float stripW = 0.0f;
    float stripH = kDefaultTabStripHeight;
    const std::size_t count = TabCount();
    for (std::size_t i = 0u; i < count; ++i) {
        UIElement* tabElem = GetTabElement(i);
        UIElement* hdrElem = nullptr;
        if (auto* hcc = tabElem ? tabElem->DynamicCast<HeaderedContentControl>() : nullptr) {
            hdrElem = hcc->GetHeaderElement();
        }
        if (hdrElem) {
            hdrElem->Measure(Size{innerAvailW, kDefaultTabStripHeight});
            stripW += std::max(64.0f, hdrElem->DesiredSize().width + 20.0f);
            stripH = std::max(stripH, hdrElem->DesiredSize().height + 8.0f);
        } else {
            const Size ts = MeasureUtf8TextRun(GetTabHeaderText(i), fontSize);
            stripW += std::max(64.0f, ts.width + 24.0f);
            stripH = std::max(stripH, ts.height + 12.0f);
        }
    }
    if (count == 0u) {
        stripH = 0.0f;
    }

    const std::int32_t activeIdx = GetSelectedIndex();
    const Size pageAvail{
        std::max(0.0f, innerAvailW - 8.0f),
        std::max(0.0f, innerAvailH - stripH - 8.0f)};

    float activePageW = 0.0f;
    float activePageH = 0.0f;
    std::int32_t visIdx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Measure(Size{0.0f, 0.0f});
            continue;
        }
        if (visIdx == activeIdx) {
            c->Measure(pageAvail);
            activePageW = c->DesiredSize().width + 8.0f;
            activePageH = c->DesiredSize().height + 8.0f;
        } else {
            c->Measure(Size{0.0f, 0.0f});
        }
        ++visIdx;
    }

    return Size{
        std::max(stripW, activePageW) + padW,
        stripH + activePageH + padH};
}

void TabControl::ArrangeCore(Rect finalRect) noexcept {
    SyncTabSelectionStates();
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const std::size_t count = TabCount();
    tabHeaderRects_.clear();
    tabHeaderRects_.reserve(count);

    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    float stripH = (count > 0u) ? std::min(innerH, kDefaultTabStripHeight) : 0.0f;
    std::vector<float> desiredTabWidths(count, 64.0f);
    float totalDesiredTabW = 0.0f;
    for (std::size_t i = 0u; i < count; ++i) {
        UIElement* tabElem = GetTabElement(i);
        UIElement* hdrElem = nullptr;
        if (auto* hcc = tabElem ? tabElem->DynamicCast<HeaderedContentControl>() : nullptr) {
            hdrElem = hcc->GetHeaderElement();
        }
        if (hdrElem) {
            desiredTabWidths[i] = std::max(64.0f, hdrElem->DesiredSize().width + 20.0f);
            stripH = std::min(innerH, std::max(stripH, hdrElem->DesiredSize().height + 8.0f));
        } else {
            const Size ts = MeasureUtf8TextRun(GetTabHeaderText(i), fontSize);
            desiredTabWidths[i] = std::max(64.0f, ts.width + 24.0f);
        }
        totalDesiredTabW += desiredTabWidths[i];
    }

    const TabStripPlacement placement = GetTabStripPlacement();
    const float stripY = (placement == TabStripPlacement::Bottom)
        ? (startY + std::max(0.0f, innerH - stripH))
        : startY;
    tabStripLocalRect_ = Rect{startX, stripY, innerW, stripH};

    const float widthScale = (totalDesiredTabW > innerW && totalDesiredTabW > 1e-4f)
        ? (innerW / totalDesiredTabW)
        : 1.0f;
    float cursorX = startX;
    for (std::size_t i = 0u; i < count; ++i) {
        const float tw = desiredTabWidths[i] * widthScale;
        const Rect tabRect{cursorX, stripY, tw, stripH};
        tabHeaderRects_.push_back(tabRect);
        cursorX += tw;

        if (auto* hcc = GetTabElement(i) ? GetTabElement(i)->DynamicCast<HeaderedContentControl>() : nullptr) {
            if (UIElement* hdrElem = hcc->GetHeaderElement()) {
                hdrElem->Arrange(Rect{
                    tabRect.x + 8.0f,
                    tabRect.y + 4.0f,
                    std::max(0.0f, tabRect.width - 16.0f),
                    std::max(0.0f, tabRect.height - 8.0f)});
            }
        }
    }

    const float pageY = (placement == TabStripPlacement::Bottom) ? startY : (startY + stripH);
    const float pageH = std::max(0.0f, innerH - stripH);
    contentPanelLocalRect_ = Rect{startX, pageY, innerW, pageH};
    const Rect innerPageSlot{
        contentPanelLocalRect_.x + 4.0f,
        contentPanelLocalRect_.y + 4.0f,
        std::max(0.0f, contentPanelLocalRect_.width - 8.0f),
        std::max(0.0f, contentPanelLocalRect_.height - 8.0f)};

    const std::int32_t activeIdx = GetSelectedIndex();
    std::int32_t visIdx = 0;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            c->Arrange(Rect{startX, pageY, 0.0f, 0.0f});
            continue;
        }
        if (visIdx == activeIdx) {
            c->Arrange(innerPageSlot);
        } else {
            c->Arrange(Rect{startX, pageY, 0.0f, 0.0f});
        }
        ++visIdx;
    }
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

void Image::SetStretchDirection(StretchDirection dir) {
    SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection Image::GetStretchDirection() const noexcept {
    const auto v = GetValue(PropId_StretchDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 2) {
            return static_cast<StretchDirection>(*i);
        }
    }
    return StretchDirection::Both;
}

Size Image::GetNaturalImageSize() const {
    const std::string src = GetSource();
    if (src.empty()) {
        return Size{0.0f, 0.0f};
    }
    if (const ImageAtlasEntry* entry = ImageAtlas::Instance().LoadOrGet(src)) {
        return Size{static_cast<float>(entry->width), static_cast<float>(entry->height)};
    }
    return Size{0.0f, 0.0f};
}

Size Image::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const std::string src = GetSource();
    if (src.empty()) {
        return Size{padW, padH};
    }

    const ImageAtlasEntry* entry = ImageAtlas::Instance().LoadOrGet(src);
    if (!entry || entry->width == 0u || entry->height == 0u) {
        return Size{padW, padH};
    }

    const float natW = static_cast<float>(entry->width);
    const float natH = static_cast<float>(entry->height);
    const Stretch stretch = GetStretch();
    const StretchDirection dir = GetStretchDirection();

    const bool hasW = std::isfinite(availableSize.width) && (availableSize.width < kUnconstrainedSpace * 0.5f);
    const bool hasH = std::isfinite(availableSize.height) && (availableSize.height < kUnconstrainedSpace * 0.5f);
    if (stretch == Stretch::None || (!hasW && !hasH)) {
        return Size{natW + padW, natH + padH};
    }

    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);
    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScaleFactors(
        natW,
        natH,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        stretch,
        dir,
        sx,
        sy);

    float desiredW = natW * sx;
    float desiredH = natH * sy;
    if (hasW) {
        desiredW = std::min(desiredW, innerAvailW);
    }
    if (hasH) {
        desiredH = std::min(desiredH, innerAvailH);
    }
    return Size{desiredW + padW, desiredH + padH};
}

namespace {

[[nodiscard]] std::size_t CountUtf8CodePointsSafe(std::string_view s) noexcept {
    std::size_t count = 0u;
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(s, cursor, cp)) {
        ++count;
    }
    return count;
}

[[nodiscard]] std::size_t Utf8ByteOffsetAtCodePoint(std::string_view s, std::size_t cpTarget) noexcept {
    std::size_t count = 0u;
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (count < cpTarget && DecodeNextUtf8CodePoint(s, cursor, cp)) {
        ++count;
    }
    return cursor;
}

[[nodiscard]] std::string ExtractElementDisplayText(const UIElement* elem) {
    if (!elem) {
        return {};
    }
    for (const auto propId : {PropId_Text, PropId_Content, PropId_Title}) {
        const UnifiedValue v = elem->GetValue(propId);
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            if (!sv->view().empty()) {
                return sv->str();
            }
        }
    }
    for (const UIElement* c = elem->FirstChild(); c != nullptr; c = c->NextSibling()) {
        std::string sub = ExtractElementDisplayText(c);
        if (!sub.empty()) {
            return sub;
        }
    }
    return {};
}

void UncheckNamedRadioGroupRecursive(
    UIElement* node,
    const RadioButton* activeRadio,
    std::string_view groupName,
    bool pushTwoWay) {
    if (!node) {
        return;
    }
    if (auto* rb = node->DynamicCast<RadioButton>()) {
        if (rb != activeRadio && rb->GetGroupName() == groupName && rb->IsChecked()) {
            rb->SetIsChecked(false);
            if (pushTwoWay) {
                (void)rb->NotifyTwoWayPropertyMutated(PropId_IsChecked);
            }
        }
    }
    for (UIElement* c = node->FirstChild(); c != nullptr; c = c->NextSibling()) {
        UncheckNamedRadioGroupRecursive(c, activeRadio, groupName, pushTwoWay);
    }
}

} // namespace

// ============================================================================
// CheckBox
// ============================================================================

CheckBox::CheckBox() {
    EnsureControlTypesRegistered();
}

void CheckBox::SetIsChecked(bool checked) {
    SetValue(PropId_IsChecked, UnifiedValue{checked}, ValueSourceRank::Local);
}

bool CheckBox::IsChecked() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsChecked);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return *i != 0;
    }
    return false;
}

void CheckBox::Toggle() {
    const bool next = !IsChecked();
    SetIsChecked(next);
    if (checkedChangedCallback_) {
        checkedChangedCallback_(*this, next);
    }
    (void)NotifyTwoWayPropertyMutated(PropId_IsChecked);
}

bool CheckBox::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    Toggle();
    return PerformClick(error);
}

Size CheckBox::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    if (FirstChild()) {
        const Size childAvail{
            std::max(0.0f, availableSize.width - (26.0f + p.left + p.right)),
            std::max(0.0f, availableSize.height - (p.top + p.bottom))};
        const Size childSize = UIElement::MeasureCore(childAvail);
        return Size{
            childSize.width + 26.0f + p.left + p.right,
            std::max(22.0f, childSize.height) + p.top + p.bottom};
    }
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const std::string label = GetContentText();
    const Size labelMetrics = MeasureUtf8TextRun(label, fontSize);
    const float indicatorW = 18.0f;
    const float gap = label.empty() ? 0.0f : 8.0f;
    return Size{
        indicatorW + gap + labelMetrics.width + p.left + p.right,
        std::max(22.0f, labelMetrics.height) + p.top + p.bottom};
}

void CheckBox::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    if (!FirstChild()) {
        return;
    }
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left + 26.0f;
    const float startY = finalRect.y + p.top;
    const float contentW = std::max(0.0f, finalRect.width - (p.left + p.right + 26.0f));
    const float contentH = std::max(0.0f, finalRect.height - (p.top + p.bottom));
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{startX, startY, contentW, contentH});
    }
}

// ============================================================================
// RadioButton
// ============================================================================

RadioButton::RadioButton() {
    EnsureControlTypesRegistered();
}

void RadioButton::SetIsChecked(bool checked) {
    SetValue(PropId_IsChecked, UnifiedValue{checked}, ValueSourceRank::Local);
}

bool RadioButton::IsChecked() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsChecked);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return *i != 0;
    }
    return false;
}

void RadioButton::SetGroupName(std::string_view groupName) {
    SetValue(PropId_GroupName, value::FromString(std::string(groupName)), ValueSourceRank::Local);
}

std::string RadioButton::GetGroupName() const {
    const UnifiedValue v = GetValue(PropId_GroupName);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void RadioButton::UncheckMutualExclusionPeers(bool pushTwoWayOnUncheckedPeers) {
    if (uncheckingPeers_) {
        return;
    }
    uncheckingPeers_ = true;
    const std::string group = GetGroupName();
    if (group.empty()) {
        if (UIElement* parent = Parent()) {
            for (UIElement* sib = parent->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                if (auto* rb = sib->DynamicCast<RadioButton>()) {
                    if (rb != this && rb->GetGroupName().empty() && rb->IsChecked()) {
                        rb->SetIsChecked(false);
                        if (pushTwoWayOnUncheckedPeers) {
                            (void)rb->NotifyTwoWayPropertyMutated(PropId_IsChecked);
                        }
                    }
                }
            }
        }
    } else {
        UIElement* root = this;
        while (root->Parent() != nullptr) {
            root = root->Parent();
        }
        UncheckNamedRadioGroupRecursive(root, this, group, pushTwoWayOnUncheckedPeers);
    }
    uncheckingPeers_ = false;
}

void RadioButton::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Button::OnPropertyChanged(propId, newValue);
    if (propId == PropId_IsChecked && IsChecked() && !uncheckingPeers_) {
        UncheckMutualExclusionPeers(false);
    }
}

bool RadioButton::ActivateFromInput(RuntimeError* error) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    if (!IsChecked()) {
        uncheckingPeers_ = true;
        SetIsChecked(true);
        uncheckingPeers_ = false;
        UncheckMutualExclusionPeers(true);
        if (checkedChangedCallback_) {
            checkedChangedCallback_(*this, true);
        }
        (void)NotifyTwoWayPropertyMutated(PropId_IsChecked);
    }
    return PerformClick(error);
}

Size RadioButton::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        return UIElement::MeasureCore(availableSize);
    }
    const Thickness p = GetPadding();
    if (FirstChild()) {
        const Size childAvail{
            std::max(0.0f, availableSize.width - (26.0f + p.left + p.right)),
            std::max(0.0f, availableSize.height - (p.top + p.bottom))};
        const Size childSize = UIElement::MeasureCore(childAvail);
        return Size{
            childSize.width + 26.0f + p.left + p.right,
            std::max(22.0f, childSize.height) + p.top + p.bottom};
    }
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);
    const std::string label = GetContentText();
    const Size labelMetrics = MeasureUtf8TextRun(label, fontSize);
    const float indicatorW = 18.0f;
    const float gap = label.empty() ? 0.0f : 8.0f;
    return Size{
        indicatorW + gap + labelMetrics.width + p.left + p.right,
        std::max(22.0f, labelMetrics.height) + p.top + p.bottom};
}

void RadioButton::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        return;
    }
    if (!FirstChild()) {
        return;
    }
    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left + 26.0f;
    const float startY = finalRect.y + p.top;
    const float contentW = std::max(0.0f, finalRect.width - (p.left + p.right + 26.0f));
    const float contentH = std::max(0.0f, finalRect.height - (p.top + p.bottom));
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{startX, startY, contentW, contentH});
    }
}

// ============================================================================
// Slider
// ============================================================================

Slider::Slider() {
    EnsureControlTypesRegistered();
}

void Slider::SetMinimum(float minVal) {
    SetValue(PropId_Minimum, MakeFloat(minVal), ValueSourceRank::Local);
}

float Slider::GetMinimum() const noexcept {
    float out = 0.0f;
    if (TryGetFloat(GetValue(PropId_Minimum), out) && std::isfinite(out)) {
        return out;
    }
    return 0.0f;
}

void Slider::SetMaximum(float maxVal) {
    SetValue(PropId_Maximum, MakeFloat(maxVal), ValueSourceRank::Local);
}

float Slider::GetMaximum() const noexcept {
    float out = 100.0f;
    if (TryGetFloat(GetValue(PropId_Maximum), out) && std::isfinite(out)) {
        return std::max(GetMinimum(), out);
    }
    return std::max(GetMinimum(), 100.0f);
}

void Slider::SetSliderValue(float val) {
    SetValue(PropId_Value, MakeFloat(val), ValueSourceRank::Local);
}

float Slider::GetSliderValue() const noexcept {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    float out = minV;
    if (TryGetFloat(GetValue(PropId_Value), out) && std::isfinite(out)) {
        return std::clamp(out, minV, maxV);
    }
    return minV;
}

bool Slider::SetSliderValueInteractive(float val) {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float clamped = std::clamp(val, minV, maxV);
    const float prev = GetSliderValue();
    SetSliderValue(clamped);
    if (std::fabs(clamped - prev) > 1e-6f) {
        if (valueChangedCallback_) {
            valueChangedCallback_(*this, clamped);
        }
        (void)NotifyTwoWayPropertyMutated(PropId_Value);
        return true;
    }
    return false;
}

void Slider::SetSmallChange(float step) {
    SetValue(PropId_SmallChange, MakeFloat(std::max(1e-5f, step)), ValueSourceRank::Local);
}

float Slider::GetSmallChange() const noexcept {
    float out = 1.0f;
    if (TryGetFloat(GetValue(PropId_SmallChange), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    const float span = GetMaximum() - GetMinimum();
    return (span > 0.0f && span <= 2.0f) ? 0.05f : 1.0f;
}

void Slider::SetLargeChange(float step) {
    SetValue(PropId_LargeChange, MakeFloat(std::max(1e-5f, step)), ValueSourceRank::Local);
}

float Slider::GetLargeChange() const noexcept {
    float out = 10.0f;
    if (TryGetFloat(GetValue(PropId_LargeChange), out) && out > 0.0f && std::isfinite(out)) {
        return out;
    }
    return GetSmallChange() * 10.0f;
}

void Slider::SetOrientation(Orientation o) {
    SetValue(
        PropId_Orientation,
        value::FromInt(static_cast<std::int64_t>(o)),
        ValueSourceRank::Local);
}

Orientation Slider::GetOrientation() const noexcept {
    if (const auto* entry = FindCompactEntry(PropId_Orientation)) {
        if (const auto* i = std::get_if<std::int64_t>(&entry->effectiveValue)) {
            return (*i == 0) ? Orientation::Vertical : Orientation::Horizontal;
        }
    }
    return Orientation::Horizontal;
}

float Slider::NormalizedPosition() const noexcept {
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    const float span = maxV - minV;
    if (span <= 1e-6f) {
        return 0.0f;
    }
    return std::clamp((GetSliderValue() - minV) / span, 0.0f, 1.0f);
}

bool Slider::UpdateValueFromLocalPoint(Point localPt) {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const Orientation orient = GetOrientation();
    float t = 0.0f;
    if (orient == Orientation::Horizontal) {
        const float trackX = r.x + p.left;
        const float trackW = std::max(1.0f, r.width - (p.left + p.right));
        t = std::clamp((localPt.x - trackX) / trackW, 0.0f, 1.0f);
    } else {
        const float trackY = r.y + p.top;
        const float trackH = std::max(1.0f, r.height - (p.top + p.bottom));
        // Standard vertical slider: bottom = Minimum (t=0), top = Maximum (t=1)
        t = std::clamp(1.0f - ((localPt.y - trackY) / trackH), 0.0f, 1.0f);
    }
    const float minV = GetMinimum();
    const float maxV = GetMaximum();
    return SetSliderValueInteractive(minV + t * (maxV - minV));
}

bool Slider::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        outRequestCapture = false;
        return false;
    }
    SetPressed(true);
    isDragging_ = true;
    outRequestCapture = true;
    (void)UpdateValueFromLocalPoint(localPoint);
    InvalidateRender();
    return true;
}

bool Slider::OnPointerMove(Point localPoint, std::uint8_t modifiers) {
    (void)modifiers;
    if (!isDragging_ || !IsPressed() || !IsEffectivelyEnabled()) {
        isDragging_ = false;
        return false;
    }
    (void)UpdateValueFromLocalPoint(localPoint);
    return true;
}

bool Slider::OnPointerUp(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool insideBounds) {
    (void)modifiers;
    (void)insideBounds;
    if (button == PointerButton::Left && isDragging_) {
        (void)UpdateValueFromLocalPoint(localPoint);
        isDragging_ = false;
        InvalidateRender();
        return true;
    }
    return false;
}

void Slider::OnPointerCaptureLost() noexcept {
    if (isDragging_) {
        isDragging_ = false;
        InvalidateRender();
    }
}

void Slider::OnEnabledStateChanged(bool effectivelyEnabled) {
    if (!effectivelyEnabled && isDragging_) {
        isDragging_ = false;
        InvalidateRender();
    }
}

bool Slider::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const float cur = GetSliderValue();
    const Orientation orient = GetOrientation();
    switch (key) {
        case KeyCode::Right:
            if (orient == Orientation::Horizontal) {
                (void)SetSliderValueInteractive(cur + GetSmallChange());
                return true;
            }
            return false;
        case KeyCode::Left:
            if (orient == Orientation::Horizontal) {
                (void)SetSliderValueInteractive(cur - GetSmallChange());
                return true;
            }
            return false;
        case KeyCode::Up:
            if (orient == Orientation::Vertical) {
                (void)SetSliderValueInteractive(cur + GetSmallChange());
                return true;
            }
            return false;
        case KeyCode::Down:
            if (orient == Orientation::Vertical) {
                (void)SetSliderValueInteractive(cur - GetSmallChange());
                return true;
            }
            return false;
        case KeyCode::PageUp:
            (void)SetSliderValueInteractive(cur + GetLargeChange());
            return true;
        case KeyCode::PageDown:
            (void)SetSliderValueInteractive(cur - GetLargeChange());
            return true;
        case KeyCode::Home:
            (void)SetSliderValueInteractive(GetMinimum());
            return true;
        case KeyCode::End:
            (void)SetSliderValueInteractive(GetMaximum());
            return true;
        default:
            return false;
    }
}

void Slider::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    UIElement::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Value || propId == PropId_Minimum || propId == PropId_Maximum) {
        SyncTemplateParts();
    }
}

void Slider::SyncTemplateParts() noexcept {
    if (!HasControlTemplate()) {
        return;
    }
    UIElement* indicator = GetTemplateChild("PART_Indicator");
    UIElement* thumb = GetTemplateChild("PART_Thumb");
    if (!indicator && !thumb) {
        return;
    }
    UIElement* track = GetTemplateChild("PART_Track");
    const float t = NormalizedPosition();
    if (GetOrientation() == Orientation::Vertical) {
        float fullH = 0.0f;
        if (track && track->FinalRect().height > 0.0f) fullH = track->FinalRect().height;
        else if (track && track->GetHeight() > 0.0f) fullH = track->GetHeight();
        else if (FinalRect().height > 0.0f) fullH = FinalRect().height;
        else if (GetHeight() > 0.0f) fullH = GetHeight();
        if (fullH > 0.0f) {
            if (indicator) {
                (void)indicator->SetValue(
                    PropId_VerticalAlignment,
                    value::FromInt(static_cast<std::int64_t>(VerticalAlignment::Bottom)),
                    ValueSourceRank::ThemeStyle);
                if (!indicator->SetValue(PropId_Height, MakeFloat(fullH * t), ValueSourceRank::ThemeStyle)) {
                    (void)indicator->SetValue(PropId_Height, MakeFloat(fullH * t), ValueSourceRank::Local);
                }
            }
            if (thumb) {
                float thumbH = 0.0f;
                if (thumb->GetHeight() > 0.0f) thumbH = thumb->GetHeight();
                else if (thumb->FinalRect().height > 0.0f) thumbH = thumb->FinalRect().height;
                else thumbH = thumb->DesiredSize().height;

                const float trackOffsetY =
                    (track && track->FinalRect().height > 0.0f &&
                     thumb->Parent() && thumb->Parent() != track && thumb->Parent()->FinalRect().height > 0.0f)
                        ? std::max(0.0f, track->FinalRect().y - thumb->Parent()->FinalRect().y)
                        : 0.0f;
                const float offsetTop = trackOffsetY + (1.0f - t) * std::max(0.0f, fullH - thumbH);

                if (thumb->Parent() && thumb->Parent()->IsA<Canvas>()) {
                    if (!thumb->SetValue(PropId_CanvasTop, MakeFloat(offsetTop), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_CanvasTop, MakeFloat(offsetTop), ValueSourceRank::Local);
                    }
                } else {
                    (void)thumb->SetValue(
                        PropId_VerticalAlignment,
                        value::FromInt(static_cast<std::int64_t>(VerticalAlignment::Top)),
                        ValueSourceRank::ThemeStyle);
                    Thickness m = thumb->GetMargin();
                    m.top = offsetTop;
                    m.bottom = 0.0f;
                    if (!thumb->SetValue(PropId_Margin, value::FromThickness(m), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_AnimTranslateY, MakeFloat(offsetTop), ValueSourceRank::ThemeStyle);
                    }
                }
            }
        }
    } else {
        float fullW = 0.0f;
        if (track && track->FinalRect().width > 0.0f) fullW = track->FinalRect().width;
        else if (track && track->GetWidth() > 0.0f) fullW = track->GetWidth();
        else if (FinalRect().width > 0.0f) fullW = FinalRect().width;
        else if (GetWidth() > 0.0f) fullW = GetWidth();
        if (fullW > 0.0f) {
            if (indicator) {
                (void)indicator->SetValue(
                    PropId_HorizontalAlignment,
                    value::FromInt(static_cast<std::int64_t>(HorizontalAlignment::Left)),
                    ValueSourceRank::ThemeStyle);
                if (!indicator->SetValue(PropId_Width, MakeFloat(fullW * t), ValueSourceRank::ThemeStyle)) {
                    (void)indicator->SetValue(PropId_Width, MakeFloat(fullW * t), ValueSourceRank::Local);
                }
            }
            if (thumb) {
                float thumbW = 0.0f;
                if (thumb->GetWidth() > 0.0f) thumbW = thumb->GetWidth();
                else if (thumb->FinalRect().width > 0.0f) thumbW = thumb->FinalRect().width;
                else thumbW = thumb->DesiredSize().width;

                const float trackOffsetX =
                    (track && track->FinalRect().width > 0.0f &&
                     thumb->Parent() && thumb->Parent() != track && thumb->Parent()->FinalRect().width > 0.0f)
                        ? std::max(0.0f, track->FinalRect().x - thumb->Parent()->FinalRect().x)
                        : 0.0f;
                const float offsetLeft = trackOffsetX + t * std::max(0.0f, fullW - thumbW);

                if (thumb->Parent() && thumb->Parent()->IsA<Canvas>()) {
                    if (!thumb->SetValue(PropId_CanvasLeft, MakeFloat(offsetLeft), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_CanvasLeft, MakeFloat(offsetLeft), ValueSourceRank::Local);
                    }
                } else {
                    (void)thumb->SetValue(
                        PropId_HorizontalAlignment,
                        value::FromInt(static_cast<std::int64_t>(HorizontalAlignment::Left)),
                        ValueSourceRank::ThemeStyle);
                    Thickness m = thumb->GetMargin();
                    m.left = offsetLeft;
                    m.right = 0.0f;
                    if (!thumb->SetValue(PropId_Margin, value::FromThickness(m), ValueSourceRank::ThemeStyle)) {
                        (void)thumb->SetValue(PropId_AnimTranslateX, MakeFloat(offsetLeft), ValueSourceRank::ThemeStyle);
                    }
                }
            }
        }
    }
}

Size Slider::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate()) {
        const Size tplSize = UIElement::MeasureCore(availableSize);
        if (tplSize.width > 0.0f || tplSize.height > 0.0f) {
            return tplSize;
        }
    }
    const Thickness p = GetPadding();
    if (GetOrientation() == Orientation::Vertical) {
        return Size{24.0f + p.left + p.right, 120.0f + p.top + p.bottom};
    }
    return Size{120.0f + p.left + p.right, 24.0f + p.top + p.bottom};
}

void Slider::ArrangeCore(Rect finalRect) noexcept {
    if (HasControlTemplate()) {
        UIElement::ArrangeCore(finalRect);
        SyncTemplateParts();
        if (UIElement* tplRoot = TemplateRoot()) {
            tplRoot->Measure(Size{finalRect.width, finalRect.height});
            tplRoot->Arrange(finalRect);
        }
    }
}

// ============================================================================
// TextBox & PasswordBox
// ============================================================================

TextBox::TextBox() {
    EnsureControlTypesRegistered();
}

PasswordBox::PasswordBox() {
    EnsureControlTypesRegistered();
    SetIsPassword(true);
}

void TextBox::SetPlaceholder(std::string_view placeholder) {
    SetValue(PropId_Placeholder, value::FromString(std::string(placeholder)), ValueSourceRank::Local);
}

std::string TextBox::GetPlaceholder() const {
    const UnifiedValue v = GetValue(PropId_Placeholder);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void TextBox::SetIsReadOnly(bool readOnly) {
    SetValue(PropId_IsReadOnly, UnifiedValue{readOnly}, ValueSourceRank::Local);
}

bool TextBox::IsReadOnly() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsReadOnly);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void TextBox::SetMaxLength(std::int32_t maxLen) {
    SetValue(PropId_MaxLength, value::FromInt(std::max<std::int32_t>(0, maxLen)), ValueSourceRank::Local);
}

std::int32_t TextBox::GetMaxLength() const noexcept {
    const UnifiedValue v = GetValue(PropId_MaxLength);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i > 0) ? static_cast<std::int32_t>(*i) : 0;
    }
    return 0;
}

void TextBox::SetCaretIndex(std::size_t codePointIndex) {
    const std::size_t maxCp = CountUtf8CodePointsSafe(GetText());
    const std::size_t clamped = std::min(codePointIndex, maxCp);
    SetValue(PropId_CaretIndex, value::FromInt(static_cast<std::int64_t>(clamped)), ValueSourceRank::Local);
}

std::size_t TextBox::GetCaretIndex() const noexcept {
    const std::size_t maxCp = CountUtf8CodePointsSafe(GetText());
    const UnifiedValue v = GetValue(PropId_CaretIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i < 0) return 0u;
        return std::min(static_cast<std::size_t>(*i), maxCp);
    }
    return maxCp;
}

void TextBox::SetIsPassword(bool isPassword) {
    SetValue(PropId_IsPassword, UnifiedValue{isPassword}, ValueSourceRank::Local);
}

bool TextBox::IsPassword() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsPassword);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

std::string TextBox::GetDisplayString() const {
    const std::string raw = GetText();
    if (raw.empty() || !IsPassword()) {
        return raw;
    }
    return std::string(CountUtf8CodePointsSafe(raw), '*');
}

float TextBox::HorizontalTextScrollOffset() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const float padRight = (p.right > 0.0f) ? p.right : 6.0f;
    const float innerW = std::max(0.0f, r.width - (padLeft + padRight));
    if (innerW <= 4.0f) {
        return 0.0f;
    }
    const float fontSize = GetFontSize();
    const std::string displayStr = GetDisplayString();
    const std::size_t caretCp = std::min(GetCaretIndex(), CountUtf8CodePointsSafe(displayStr));
    const std::size_t byteOff = Utf8ByteOffsetAtCodePoint(displayStr, caretCp);
    const Size prefixMetrics = MeasureUtf8TextRun(
        std::string_view(displayStr).substr(0, byteOff),
        fontSize,
        GetFontFamily());
    const float maxCaretRelX = std::max(0.0f, innerW - 2.0f);
    return (prefixMetrics.width > maxCaretRelX) ? (prefixMetrics.width - maxCaretRelX) : 0.0f;
}

Rect TextBox::CaretLocalRect() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float fontSize = GetFontSize();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const float caretH = std::max(12.0f, fontSize * 1.2f);
    const std::string displayStr = GetDisplayString();
    const std::size_t caretCp = std::min(GetCaretIndex(), CountUtf8CodePointsSafe(displayStr));
    const std::size_t byteOff = Utf8ByteOffsetAtCodePoint(displayStr, caretCp);
    const Size prefixMetrics = MeasureUtf8TextRun(
        std::string_view(displayStr).substr(0, byteOff),
        fontSize,
        GetFontFamily());
    const float scrollX = HorizontalTextScrollOffset();
    const float boxH = (r.height > 0.0f) ? r.height : std::max(28.0f, caretH + 8.0f);
    const float cy = r.y + std::max(2.0f, (boxH - caretH) * 0.5f);
    return Rect{r.x + padLeft + prefixMetrics.width - scrollX, cy, 2.0f, caretH};
}

Rect TextBox::CaretScreenRect() const noexcept {
    return LocalToScreenBounds(CaretLocalRect());
}

void TextBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    TextBlock::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Text) {
        const std::size_t cpLen = CountUtf8CodePointsSafe(GetText());
        if (const auto* entry = FindCompactEntry(PropId_CaretIndex)) {
            if (const auto* ci = std::get_if<std::int64_t>(&entry->effectiveValue)) {
                if (*ci > static_cast<std::int64_t>(cpLen)) {
                    SetCaretIndex(cpLen);
                }
            }
        }
    }
}

bool TextBox::CommitEditedText(std::string newText, std::size_t newCaretCodePoint) {
    const std::string prevText = GetText();
    SetText(newText);
    SetCaretIndex(newCaretCodePoint);
    if (newText != prevText) {
        if (textChangedCallback_) {
            textChangedCallback_(*this, newText);
        }
        (void)NotifyTwoWayPropertyMutated(PropId_Text);
        return true;
    }
    return false;
}

bool TextBox::InsertText(std::string_view utf8Text) {
    if (IsReadOnly() || !IsEffectivelyEnabled() || utf8Text.empty()) {
        return false;
    }

    const std::string cur = GetText();
    const std::size_t curCpLen = CountUtf8CodePointsSafe(cur);
    const std::int32_t maxLen = GetMaxLength();
    const std::size_t remainingAllowed = (maxLen > 0)
        ? ((static_cast<std::size_t>(maxLen) > curCpLen) ? (static_cast<std::size_t>(maxLen) - curCpLen) : 0u)
        : 1000000u;
    if (remainingAllowed == 0u) {
        return false;
    }

    // Filter single-line printable UTF-8 codepoints up to remainingAllowed
    std::string filtered;
    filtered.reserve(utf8Text.size());
    std::size_t cursor = 0u;
    std::size_t insertedCpCount = 0u;
    std::uint32_t cp = 0u;
    while (insertedCpCount < remainingAllowed) {
        const std::size_t cpStart = cursor;
        if (!DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
            break;
        }
        if (cp < 0x20u || cp == 0x7Fu) {
            continue;
        }
        filtered.append(utf8Text.substr(cpStart, cursor - cpStart));
        ++insertedCpCount;
    }
    if (insertedCpCount == 0u) {
        return false;
    }

    const std::size_t caretCp = GetCaretIndex();
    const std::size_t bytePos = Utf8ByteOffsetAtCodePoint(cur, caretCp);
    std::string updated = cur.substr(0, bytePos) + filtered + cur.substr(bytePos);
    return CommitEditedText(std::move(updated), caretCp + insertedCpCount);
}

bool TextBox::ReplaceAllTextInteractive(std::string_view utf8Text) {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::int32_t maxLen = GetMaxLength();
    const std::size_t limit = (maxLen > 0) ? static_cast<std::size_t>(maxLen) : 1000000u;
    std::string filtered;
    std::size_t cursor = 0u;
    std::size_t cpCount = 0u;
    std::uint32_t cp = 0u;
    while (cpCount < limit) {
        const std::size_t start = cursor;
        if (!DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
            break;
        }
        if (cp < 0x20u || cp == 0x7Fu) {
            continue;
        }
        filtered.append(utf8Text.substr(start, cursor - start));
        ++cpCount;
    }
    return CommitEditedText(std::move(filtered), cpCount);
}

bool TextBox::Backspace() {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t caretCp = GetCaretIndex();
    if (caretCp == 0u) {
        return false;
    }
    const std::string cur = GetText();
    const std::size_t prevByte = Utf8ByteOffsetAtCodePoint(cur, caretCp - 1u);
    const std::size_t curByte = Utf8ByteOffsetAtCodePoint(cur, caretCp);
    std::string updated = cur.substr(0, prevByte) + cur.substr(curByte);
    return CommitEditedText(std::move(updated), caretCp - 1u);
}

bool TextBox::DeleteForward() {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::string cur = GetText();
    const std::size_t totalCp = CountUtf8CodePointsSafe(cur);
    const std::size_t caretCp = GetCaretIndex();
    if (caretCp >= totalCp) {
        return false;
    }
    const std::size_t curByte = Utf8ByteOffsetAtCodePoint(cur, caretCp);
    const std::size_t nextByte = Utf8ByteOffsetAtCodePoint(cur, caretCp + 1u);
    std::string updated = cur.substr(0, curByte) + cur.substr(nextByte);
    return CommitEditedText(std::move(updated), caretCp);
}

bool TextBox::MoveCaretLeft() {
    const std::size_t cur = GetCaretIndex();
    if (cur == 0u) {
        return false;
    }
    SetCaretIndex(cur - 1u);
    return true;
}

bool TextBox::MoveCaretRight() {
    const std::size_t cur = GetCaretIndex();
    const std::size_t total = CountUtf8CodePointsSafe(GetText());
    if (cur >= total) {
        return false;
    }
    SetCaretIndex(cur + 1u);
    return true;
}

bool TextBox::MoveCaretHome() {
    if (GetCaretIndex() == 0u) {
        return false;
    }
    SetCaretIndex(0u);
    return true;
}

bool TextBox::MoveCaretEnd() {
    const std::size_t total = CountUtf8CodePointsSafe(GetText());
    if (GetCaretIndex() == total) {
        return false;
    }
    SetCaretIndex(total);
    return true;
}

bool TextBox::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const float relX = std::max(0.0f, localPoint.x - (r.x + padLeft) + HorizontalTextScrollOffset());
    const float fontSize = std::max(1.0f, GetFontSize());
    const std::string display = GetDisplayString();
    const TextLayoutResult layout = FontAtlas::Instance().MeasureLayout(
        display,
        GetFontFamily(),
        fontSize,
        kUnconstrainedSpace,
        TextWrapping::NoWrap);

    std::size_t cursor = 0u;
    std::size_t cpIdx = 0u;
    std::size_t glyphIdx = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(display, cursor, cp)) {
        if (cp == '\r' || cp == '\n') {
            ++cpIdx;
            continue;
        }
        if (glyphIdx < layout.glyphs.size()) {
            const PositionedGlyph& g = layout.glyphs[glyphIdx++];
            if (relX < g.penX + g.advanceX * 0.5f) {
                break;
            }
        }
        ++cpIdx;
    }
    SetCaretIndex(cpIdx);
    return true;
}

bool TextBox::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    switch (key) {
        case KeyCode::Backspace:
            return Backspace();
        case KeyCode::Delete:
            return DeleteForward();
        case KeyCode::Left:
            (void)MoveCaretLeft();
            return true;
        case KeyCode::Right:
            (void)MoveCaretRight();
            return true;
        case KeyCode::Home:
            (void)MoveCaretHome();
            return true;
        case KeyCode::End:
            (void)MoveCaretEnd();
            return true;
        default:
            return false;
    }
}

bool TextBox::OnTextInput(std::string_view utf8Text) {
    return InsertText(utf8Text);
}

Size TextBox::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float padW = (p.left + p.right > 0.0f) ? (p.left + p.right) : 12.0f;
    const float padH = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 8.0f;
    const float fontSize = GetFontSize();
    std::string text = GetDisplayString();
    if (text.empty()) {
        text = GetPlaceholder();
    }
    const Size metrics = MeasureUtf8TextRun(text, fontSize, GetFontFamily());
    return Size{
        std::max(80.0f, metrics.width + padW),
        std::max(28.0f, std::max(fontSize * 1.25f, metrics.height) + padH)};
}

// ============================================================================
// ItemsControl (with Viewport-Aware VirtualizingStackPanel Recycling)
// ============================================================================

ItemsControl::ItemsControl() {
    EnsureControlTypesRegistered();
}

ItemsControl::~ItemsControl() {
    ClearRecyclePool();
}

void ItemsControl::SetIsVirtualizing(bool enabled) {
    SetValue(PropId_IsVirtualizing, UnifiedValue{enabled}, ValueSourceRank::Local);
}

bool ItemsControl::IsVirtualizing() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsVirtualizing);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void ItemsControl::SetVirtualItemHeight(float itemHeight) {
    if (itemHeight > 0.0f && std::isfinite(itemHeight)) {
        SetValue(PropId_ItemHeight, MakeFloat(itemHeight), ValueSourceRank::Local);
    } else {
        (void)ClearValue(PropId_ItemHeight, ValueSourceRank::Local);
    }
}

float ItemsControl::GetVirtualItemHeight() const noexcept {
    float h = 24.0f;
    if (TryGetFloat(GetValue(PropId_ItemHeight), h) && h > 1.0f && std::isfinite(h)) {
        return h;
    }
    return 24.0f;
}

const ScrollViewer* ItemsControl::FindAncestorScrollViewer() const noexcept {
    for (const UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (const auto* sv = anc->DynamicCast<ScrollViewer>()) {
            return sv;
        }
    }
    return nullptr;
}

bool ItemsControl::ComputeLiveScrollViewport(float& outViewTop, float& outViewH) const noexcept {
    const ScrollViewer* sv = FindAncestorScrollViewer();
    if (!sv) {
        return false;
    }
    const Thickness p = GetPadding();
    const Rect clip = sv->GetLocalClipBounds();
    if (clip.height > 0.0f) {
        outViewH = clip.height;
    } else if (sv->ViewportHeight() > 0.0f) {
        outViewH = sv->ViewportHeight();
    } else if (sv->GetHeight() > 0.0f) {
        const Thickness svPad = sv->GetPadding();
        outViewH = std::max(0.0f, sv->GetHeight() - (svPad.top + svPad.bottom));
    } else {
        outViewH = 240.0f;
    }

    const UIElement* svChild = this;
    while (svChild->Parent() != nullptr && svChild->Parent() != sv) {
        svChild = svChild->Parent();
    }

    float staticRowStartInContent = GetMargin().top + p.top;
    if (this != svChild) {
        if (FinalRect().height > 0.0f || svChild->FinalRect().height > 0.0f) {
            staticRowStartInContent =
                (FinalRect().y - (svChild->FinalRect().y - svChild->GetMargin().top)) + p.top;
        } else {
            for (const UIElement* cur = Parent(); cur != nullptr && cur != sv; cur = cur->Parent()) {
                staticRowStartInContent += cur->GetMargin().top + cur->GetPadding().top;
            }
        }
    }

    const float liveScrollY = std::max(0.0f, sv->GetVerticalOffset());
    outViewTop = std::max(0.0f, liveScrollY - staticRowStartInContent);
    return true;
}

void ItemsControl::SetStyleEngine(StyleTriggerEngine* styleEngine) noexcept {
    if (styleEngine_ == styleEngine) {
        return;
    }
    styleEngine_ = styleEngine;
    if (styleEngine_) {
        for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
            (void)styleEngine_->BindSubtreeStyles(c);
            (void)styleEngine_->EvaluateSubtree(c);
        }
    }
}

void ItemsControl::SetItemTemplateKey(std::string_view key) {
    if (key.empty()) {
        (void)ClearValue(PropId_ItemTemplate, ValueSourceRank::Local);
    } else {
        SetValue(PropId_ItemTemplate, value::FromString(std::string(key)), ValueSourceRank::Local);
    }
}

std::string ItemsControl::GetItemTemplateKey() const {
    const UnifiedValue v = GetValue(PropId_ItemTemplate);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

const AxbPackage* ItemsControl::ResolveActiveItemTemplate(const UnifiedValue& sampleItem) const noexcept {
    const std::string key = GetItemTemplateKey();
    if (!key.empty()) {
        return AxbTemplateRegistry::Instance().FindDataTemplate(key);
    }
    if (const auto* objRef = std::get_if<ObjectRef>(&sampleItem)) {
        if (objRef->valid() && domains_ && bindingEngine_ && bindingEngine_->Program()) {
            if (auto* heap = domains_->heapForObject(*objRef)) {
                if (const auto tid = heap->objectTypeId(*objRef)) {
                    if (const auto* tSym = bindingEngine_->Program()->resolveType(*tid)) {
                        return AxbTemplateRegistry::Instance().FindImplicitDataTemplate(tSym->name);
                    }
                }
            }
        }
    }
    return nullptr;
}

IntrusivePtr<UIElement> ItemsControl::InstantiateTemplatedItem(
    const AxbPackage& tplPkg,
    const UnifiedValue& itemVal) {
    const std::string_view rootType = !tplPkg.rootTypeName.empty()
        ? std::string_view(tplPkg.rootTypeName)
        : std::string_view("Border");
    IntrusivePtr<UIElement> created = CreateElementByTypeName(rootType);
    if (!created) {
        return {};
    }
    created->SetNamescopeBoundary(true);
    created->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
    ObjectRef itemObj{};
    if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
        itemObj = *ref;
    }
    AnimationEngine* effAnimEngine = animationEngine_
        ? animationEngine_
        : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
    (void)MountAxbTemplate(created.Get(), tplPkg, domains_, bindingEngine_, itemObj, nullptr, nullptr, effAnimEngine);
    if (styleEngine_) {
        styleEngine_->RegisterPackageStyles(tplPkg);
        (void)styleEngine_->BindSubtreeStyles(created.Get());
        (void)styleEngine_->EvaluateSubtree(created.Get());
    }
    return created;
}

namespace {

constexpr std::size_t kInvalidVirtualSlotIndex = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMaxVirtualRecyclePoolSize = 64u;

[[nodiscard]] bool CollectionItemValuesEqual(
    const UnifiedValue& a,
    const UnifiedValue& b) noexcept {
    if (a.rawTag() != b.rawTag()) {
        return false;
    }
    if (const auto* rA = std::get_if<ObjectRef>(&a)) {
        return *rA == std::get<ObjectRef>(b);
    }
    return a == b;
}

} // namespace

void ItemsControl::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t ItemsControl::GetSelectedIndex() const noexcept {
    const UnifiedValue v = GetValue(PropId_SelectedIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    return -1;
}

UnifiedValue ItemsControl::GetSelectedItem() const {
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < items_.size()) {
        return items_[static_cast<std::size_t>(idx)];
    }
    const UnifiedValue explicitItem = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(explicitItem)) {
        return explicitItem;
    }
    return UnifiedValue{};
}

UnifiedValue ItemsControl::NormalizeItemValue(const UnifiedValue& raw) {
    UnifiedValue out = raw;
    if (const auto* strRef = std::get_if<ObjectRef>(&out)) {
        if (strRef->kind == ObjectKind::String && domains_) {
            if (const auto sv = domains_->stringViewAcrossDomains(*strRef)) {
                out = value::FromString(std::string(*sv));
            }
        }
    }
    if (out.isTraceable()) {
        rs_rt::pinValueInPools(out);
        EnsureRegisteredInValuePoolSet();
    }
    return out;
}

std::int32_t ItemsControl::FindMatchingItemIndex(const UnifiedValue& target) const {
    if (std::holds_alternative<std::monostate>(target) ||
        std::holds_alternative<NullObject>(target)) {
        return -1;
    }
    UnifiedValue comparable = target;
    if (const auto* strRef = std::get_if<ObjectRef>(&comparable)) {
        if (strRef->kind == ObjectKind::String && domains_) {
            if (const auto sv = domains_->stringViewAcrossDomains(*strRef)) {
                comparable = value::FromString(std::string(*sv));
            }
        }
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (CollectionItemValuesEqual(items_[i], comparable)) {
            return static_cast<std::int32_t>(i);
        }
    }
    if (const auto* sv = std::get_if<StringValue>(&comparable)) {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (rs_rt::valueToString(items_[i]) == sv->view()) {
                return static_cast<std::int32_t>(i);
            }
        }
    }
    return -1;
}

void ItemsControl::ReleaseChildSubtreeResources(UIElement* ch) {
    if (!ch) return;
    AnimationEngine* effAnimEngine = animationEngine_
        ? animationEngine_
        : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
    if (effAnimEngine) {
        effAnimEngine->StopSubtreeAnimations(ch, false);
    }
    if (bindingEngine_) {
        (void)bindingEngine_->UnbindSubtree(ch);
    }
    if (styleEngine_) {
        (void)styleEngine_->UnbindSubtreeStyles(ch);
    }
}

void ItemsControl::ClearRecyclePool(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    NativeHandleRegistry* effReg = registry ? registry : (domains_ ? &domains_->nativeHandles() : nullptr);
    ManagedHeap* effHeap = uiHeap ? uiHeap : (domains_ ? &domains_->uiHeap() : nullptr);
    for (auto& slot : recyclePool_) {
        if (slot.element) {
            ReleaseChildSubtreeResources(slot.element.Get());
            if (slot.element->Parent() == this) {
                RemoveChild(slot.element.Get(), effReg, effHeap);
            }
        }
    }
    recyclePool_.clear();
}

void ItemsControl::MoveVirtualSlotsToRecyclePool(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    NativeHandleRegistry* effReg = registry ? registry : (domains_ ? &domains_->nativeHandles() : nullptr);
    ManagedHeap* effHeap = uiHeap ? uiHeap : (domains_ ? &domains_->uiHeap() : nullptr);
    for (auto& slot : virtualSlots_) {
        if (!slot.element) {
            continue;
        }
        if (slot.element->Parent() == this) {
            RemoveChild(slot.element.Get(), effReg, effHeap);
        }
        if (recyclePool_.size() < kMaxVirtualRecyclePoolSize) {
            (void)slot.element->ClearValue(PropId_DataContext, ValueSourceRank::Local);
            if (bindingEngine_) {
                (void)bindingEngine_->RebindSubtreeRootObject(slot.element.Get(), ObjectRef{});
            }
            slot.itemIndex = kInvalidVirtualSlotIndex;
            recyclePool_.push_back(std::move(slot));
        } else {
            ReleaseChildSubtreeResources(slot.element.Get());
        }
    }
    virtualSlots_.clear();
    firstRealizedIndex_ = 0u;
    realizedEndExclusive_ = 0u;
    while (UIElement* ch = FirstChild()) {
        ReleaseChildSubtreeResources(ch);
        RemoveChild(ch, effReg, effHeap);
    }
}

IntrusivePtr<UIElement> ItemsControl::CreateMaterializedItemVisual(
    std::size_t index,
    const UnifiedValue& itemVal,
    const AxbPackage*& outAppliedTemplate) {
    outAppliedTemplate = nullptr;
    if (itemGenerator_) {
        return itemGenerator_(index, itemVal);
    }
    if (const AxbPackage* tplPkg = ResolveActiveItemTemplate(itemVal)) {
        outAppliedTemplate = tplPkg;
        return InstantiateTemplatedItem(*tplPkg, itemVal);
    }
    auto tb = MakeIntrusive<TextBlock>();
    tb->SetText(rs_rt::valueToString(itemVal));
    return tb;
}

bool ItemsControl::RebindMaterializedItemInPlace(
    UIElement* elem,
    const AxbPackage* appliedTpl,
    std::size_t index,
    const UnifiedValue& itemVal) {
    if (!elem) return false;
    if (elem->GetVisibility() != Visibility::Visible) {
        elem->SetVisibility(Visibility::Visible);
    }
    if (itemBinder_) {
        itemBinder_(index, itemVal, *elem);
        return true;
    }
    if (!itemGenerator_) {
        const AxbPackage* tplPkg = ResolveActiveItemTemplate(itemVal);
        if (tplPkg != nullptr && appliedTpl == tplPkg) {
            elem->SetValue(PropId_DataContext, itemVal, ValueSourceRank::Local);
            ObjectRef itemObj{};
            if (const auto* ref = std::get_if<ObjectRef>(&itemVal)) {
                itemObj = *ref;
            }
            if (bindingEngine_) {
                (void)bindingEngine_->RebindSubtreeRootObject(elem, itemObj);
            }
            if (styleEngine_) {
                (void)styleEngine_->EvaluateSubtree(elem, true);
            }
            return true;
        }
        if (tplPkg == nullptr && appliedTpl == nullptr) {
            if (auto* tb = elem->DynamicCast<TextBlock>()) {
                tb->SetText(rs_rt::valueToString(itemVal));
                return true;
            }
        }
    }
    return false;
}

void ItemsControl::SyncSelectionMetadataAfterMutation(std::int32_t nextSelectedIndex) {
    if (syncingItemsSelection_) {
        return;
    }
    const std::int32_t curSel = GetSelectedIndex();
    const UnifiedValue prevItemVal = GetValue(PropId_SelectedItem);
    UnifiedValue nextItemVal{};
    const bool hasNextItem =
        (nextSelectedIndex >= 0 && static_cast<std::size_t>(nextSelectedIndex) < items_.size());
    if (hasNextItem) {
        nextItemVal = items_[static_cast<std::size_t>(nextSelectedIndex)];
    }

    syncingItemsSelection_ = true;
    if (nextSelectedIndex != curSel) {
        deferSelectionNotify_ = true;
        if (hasNextItem) {
            SetValue(PropId_SelectedItem, nextItemVal, ValueSourceRank::Local);
        } else if (nextSelectedIndex < 0) {
            (void)ClearValue(PropId_SelectedItem, ValueSourceRank::Local);
        }
        deferSelectionNotify_ = false;
        SetSelectedIndex(nextSelectedIndex);
    } else {
        if (hasNextItem) {
            SetValue(PropId_SelectedItem, nextItemVal, ValueSourceRank::Local);
        } else if (nextSelectedIndex < 0) {
            (void)ClearValue(PropId_SelectedItem, ValueSourceRank::Local);
        }
    }
    syncingItemsSelection_ = false;

    if (nextSelectedIndex != curSel) {
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
    }
    if (!CollectionItemValuesEqual(nextItemVal, prevItemVal)) {
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
    }
}

void ItemsControl::ComputeCurrentVirtualWindow(
    Size fallbackChildAvail,
    std::size_t& outFirstIdx,
    std::size_t& outEndExclusive,
    Size& outMeasureConstraint) const noexcept {
    const float itemH = GetVirtualItemHeight();
    const Thickness p = GetPadding();
    const float availW = (fallbackChildAvail.width > 0.0f && fallbackChildAvail.width < kUnconstrainedSpace * 0.5f)
        ? fallbackChildAvail.width
        : std::max(0.0f, FinalRect().width - (p.left + p.right));

    float viewTop = 0.0f;
    float viewH = 240.0f;
    if (!ComputeLiveScrollViewport(viewTop, viewH)) {
        if (fallbackChildAvail.height < kUnconstrainedSpace * 0.5f && fallbackChildAvail.height > 0.0f) {
            viewH = fallbackChildAvail.height;
        } else if (FinalRect().height > 0.0f) {
            viewH = std::max(itemH, FinalRect().height - (p.top + p.bottom));
        }
    }

    // Clamp viewTop against updated logical height in case items shrank near bottom
    const float totalLogicalH = static_cast<float>(items_.size()) * itemH;
    const float maxScrollTop = std::max(0.0f, totalLogicalH - viewH);
    viewTop = std::min(viewTop, maxScrollTop);

    constexpr std::size_t kOverscan = 2u;
    const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(viewTop / itemH)));
    outFirstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
    const auto rawEnd = static_cast<std::size_t>(std::ceil((viewTop + viewH) / itemH)) + kOverscan;
    outEndExclusive = items_.empty()
        ? 0u
        : std::min(items_.size(), std::max(outFirstIdx + 1u, rawEnd));
    outMeasureConstraint = Size{availW, itemH};
}

void ItemsControl::MarkExtraValuesInPools() const noexcept {
    for (const auto& item : items_) {
        if (item.isTraceable()) {
            rs_rt::markValueInPools(item);
        }
    }
}

void ItemsControl::RebuildAllMaterializedItems(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    ClearRecyclePool(registry, uiHeap);
    virtualSlots_.clear();
    firstRealizedIndex_ = 0u;
    realizedEndExclusive_ = items_.size();

    while (UIElement* ch = FirstChild()) {
        ReleaseChildSubtreeResources(ch);
        RemoveChild(ch, registry, uiHeap);
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const AxbPackage* appliedTpl = nullptr;
        IntrusivePtr<UIElement> itemVisual = CreateMaterializedItemVisual(i, items_[i], appliedTpl);
        if (itemVisual) {
            AddChild(itemVisual.Get(), registry, uiHeap);
        }
    }
}

void ItemsControl::SyncVirtualizedWindow(
    std::size_t targetFirstIdx,
    std::size_t targetEndExclusive,
    Size measureConstraint) {
    const std::size_t total = items_.size();
    targetFirstIdx = std::min(targetFirstIdx, total);
    targetEndExclusive = std::clamp(targetEndExclusive, targetFirstIdx, total);

    NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;

    // Partition existing realized slots into kept (still inside window) vs recyclable (scrolled out or invalidated)
    std::vector<RealizedVirtualSlot> kept;
    std::vector<RealizedVirtualSlot> recyclable;
    kept.reserve(targetEndExclusive - targetFirstIdx);
    recyclable.reserve(virtualSlots_.size());

    for (auto& slot : virtualSlots_) {
        if (slot.element &&
            slot.itemIndex != kInvalidVirtualSlotIndex &&
            slot.itemIndex >= targetFirstIdx &&
            slot.itemIndex < targetEndExclusive) {
            kept.push_back(std::move(slot));
        } else if (slot.element) {
            recyclable.push_back(std::move(slot));
        }
    }

    auto bindOrCreateSlot = [&](std::size_t idx, const AxbPackage*& outTpl) -> IntrusivePtr<UIElement> {
        const AxbPackage* tplPkg = !itemGenerator_ ? ResolveActiveItemTemplate(items_[idx]) : nullptr;
        outTpl = tplPkg;
        while (!recyclable.empty()) {
            RealizedVirtualSlot recycledSlot = std::move(recyclable.back());
            recyclable.pop_back();
            IntrusivePtr<UIElement> reused = std::move(recycledSlot.element);
            if (RebindMaterializedItemInPlace(
                    reused.Get(), recycledSlot.appliedTemplate, idx, items_[idx])) {
                outTpl = tplPkg;
                if (reused->Parent() != this) {
                    AddChild(reused.Get(), registry, uiHeap);
                }
                return reused;
            }
            ReleaseChildSubtreeResources(reused.Get());
            RemoveChild(reused.Get(), registry, uiHeap);
        }

        while (!recyclePool_.empty()) {
            RealizedVirtualSlot pooledSlot = std::move(recyclePool_.back());
            recyclePool_.pop_back();
            IntrusivePtr<UIElement> reused = std::move(pooledSlot.element);
            if (!reused) {
                continue;
            }
            if (RebindMaterializedItemInPlace(
                    reused.Get(), pooledSlot.appliedTemplate, idx, items_[idx])) {
                outTpl = tplPkg;
                if (reused->Parent() != this) {
                    AddChild(reused.Get(), registry, uiHeap);
                }
                return reused;
            }
            ReleaseChildSubtreeResources(reused.Get());
            if (reused->Parent() == this) {
                RemoveChild(reused.Get(), registry, uiHeap);
            }
        }

        IntrusivePtr<UIElement> created = CreateMaterializedItemVisual(idx, items_[idx], outTpl);
        if (created) {
            AddChild(created.Get(), registry, uiHeap);
        }
        return created;
    };

    std::vector<RealizedVirtualSlot> nextWindow;
    nextWindow.reserve(targetEndExclusive - targetFirstIdx);

    for (std::size_t idx = targetFirstIdx; idx < targetEndExclusive; ++idx) {
        IntrusivePtr<UIElement> matched{};
        const AxbPackage* matchedTpl = nullptr;
        for (auto& k : kept) {
            if (k.itemIndex == idx && k.element) {
                matched = std::move(k.element);
                matchedTpl = k.appliedTemplate;
                if (itemBinder_) {
                    itemBinder_(idx, items_[idx], *matched);
                }
                break;
            }
        }
        if (!matched) {
            matched = bindOrCreateSlot(idx, matchedTpl);
        }
        if (matched) {
            matched->Measure(measureConstraint);
            estimatedMaxItemWidth_ = std::max(estimatedMaxItemWidth_, matched->DesiredSize().width);
            nextWindow.push_back(RealizedVirtualSlot{idx, std::move(matched), matchedTpl});
        }
    }

    // Move surplus containers to recyclePool_ (up to cap) or release cleanly
    for (auto& surplus : recyclable) {
        if (surplus.element) {
            if (surplus.element->Parent() == this) {
                RemoveChild(surplus.element.Get(), registry, uiHeap);
            }
            if (recyclePool_.size() < kMaxVirtualRecyclePoolSize) {
                (void)surplus.element->ClearValue(PropId_DataContext, ValueSourceRank::Local);
                if (bindingEngine_) {
                    (void)bindingEngine_->RebindSubtreeRootObject(surplus.element.Get(), ObjectRef{});
                }
                surplus.itemIndex = kInvalidVirtualSlotIndex;
                recyclePool_.push_back(std::move(surplus));
            } else {
                ReleaseChildSubtreeResources(surplus.element.Get());
            }
        }
    }

    if (nextWindow.size() > 1u) {
        std::vector<UIElement*> orderedPtrs;
        orderedPtrs.reserve(nextWindow.size());
        for (const auto& s : nextWindow) {
            orderedPtrs.push_back(s.element.Get());
        }
        ReorderExistingChildren(orderedPtrs.data(), orderedPtrs.size());
    }

    virtualSlots_ = std::move(nextWindow);
    firstRealizedIndex_ = targetFirstIdx;
    realizedEndExclusive_ = targetEndExclusive;
}

void ItemsControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    hasBoundItemsSource_ = true;
    const bool alreadyRealized = IsVirtualizing() ? !virtualSlots_.empty() : (FirstChild() != nullptr);
    if (!IsA<ComboBox>() && alreadyRealized && items.size() == items_.size()) {
        bool allSame = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (!CollectionItemValuesEqual(items[i], items_[i])) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            return;
        }
    }

    const std::size_t oldCount = items_.size();
    const std::int32_t prevSel = GetSelectedIndex();
    const bool hadValidPrevSel =
        (oldCount > 0u && prevSel >= 0 && static_cast<std::size_t>(prevSel) < oldCount);
    const UnifiedValue prevSelectedItem = hadValidPrevSel
        ? items_[static_cast<std::size_t>(prevSel)]
        : UnifiedValue{};

    items_.resize(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        items_[i] = NormalizeItemValue(items[i]);
    }
    estimatedMaxItemWidth_ = 80.0f;

    // Reconcile SelectedIndex & SelectedItem consistently with incremental mutations
    if (oldCount > 0u) {
        if (items_.empty()) {
            if (prevSel >= 0) {
                SyncSelectionMetadataAfterMutation(-1);
            }
        } else if (hadValidPrevSel) {
            std::int32_t resolvedSel = -1;
            if (static_cast<std::size_t>(prevSel) < items_.size() &&
                CollectionItemValuesEqual(items_[static_cast<std::size_t>(prevSel)], prevSelectedItem)) {
                resolvedSel = prevSel;
            } else {
                for (std::size_t i = 0; i < items_.size(); ++i) {
                    if (CollectionItemValuesEqual(items_[i], prevSelectedItem)) {
                        resolvedSel = static_cast<std::int32_t>(i);
                        break;
                    }
                }
                if (resolvedSel < 0 && items_.size() == oldCount) {
                    resolvedSel = prevSel;
                }
            }
            SyncSelectionMetadataAfterMutation(resolvedSel);
        }
    } else if (prevSel >= 0 && static_cast<std::size_t>(prevSel) < items_.size()) {
        SyncSelectionMetadataAfterMutation(prevSel);
    } else if (prevSel < 0 && !items_.empty()) {
        const std::int32_t matchedFromItem = FindMatchingItemIndex(GetValue(PropId_SelectedItem));
        if (matchedFromItem >= 0) {
            SyncSelectionMetadataAfterMutation(matchedFromItem);
        }
    }

    if (IsVirtualizing() && !IsA<ComboBox>()) {
        MoveVirtualSlotsToRecyclePool(registry, uiHeap);
        InvalidateLayout();
        return;
    }

    RebuildAllMaterializedItems(registry, uiHeap);
}

bool ItemsControl::ApplyIncrementalChanges(
    const CollectionChangeRecord* changes,
    std::size_t changeCount) {
    if (!changes || changeCount == 0u) {
        return false;
    }
    for (std::size_t c = 0u; c < changeCount; ++c) {
        if (changes[c].kind == CollectionChangeKind::Reset) {
            return false;
        }
    }
    hasBoundItemsSource_ = true;
    NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;

    std::int32_t selIdx = GetSelectedIndex();

    // 1. ComboBox incremental fast path (no child row visual containers)
    if (IsA<ComboBox>()) {
        for (std::size_t c = 0u; c < changeCount; ++c) {
            const auto& rec = changes[c];
            switch (rec.kind) {
                case CollectionChangeKind::Add:
                    items_.push_back(NormalizeItemValue(rec.newValue));
                    break;
                case CollectionChangeKind::Insert: {
                    const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), NormalizeItemValue(rec.newValue));
                    if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                        ++selIdx;
                    }
                    break;
                }
                case CollectionChangeKind::RemoveAt: {
                    if (rec.index >= items_.size()) break;
                    const std::size_t idx = rec.index;
                    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                    if (selIdx >= 0) {
                        if (idx < static_cast<std::size_t>(selIdx)) --selIdx;
                        else if (idx == static_cast<std::size_t>(selIdx)) selIdx = -1;
                    }
                    break;
                }
                case CollectionChangeKind::Replace: {
                    if (rec.index < items_.size()) {
                        items_[rec.index] = NormalizeItemValue(rec.newValue);
                    }
                    break;
                }
                case CollectionChangeKind::Reset:
                    return false;
                case CollectionChangeKind::Clear:
                    items_.clear();
                    selIdx = -1;
                    break;
            }
        }
        SyncSelectionMetadataAfterMutation(selIdx);
        InvalidateLayout();
        InvalidateRender();
        return true;
    }

    // 2. Virtualized ItemsControl / ListBox O(1) incremental path
    if (IsVirtualizing()) {
        if (virtualSlots_.empty()) {
            // Window not yet realized by initial layout: apply deltas directly to items_
            for (std::size_t c = 0u; c < changeCount; ++c) {
                const auto& rec = changes[c];
                switch (rec.kind) {
                    case CollectionChangeKind::Add:
                        items_.push_back(NormalizeItemValue(rec.newValue));
                        break;
                    case CollectionChangeKind::Insert: {
                        const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), NormalizeItemValue(rec.newValue));
                        if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) ++selIdx;
                        break;
                    }
                    case CollectionChangeKind::RemoveAt: {
                        if (rec.index >= items_.size()) break;
                        const std::size_t idx = rec.index;
                        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                        if (selIdx >= 0) {
                            if (idx < static_cast<std::size_t>(selIdx)) --selIdx;
                            else if (idx == static_cast<std::size_t>(selIdx)) selIdx = -1;
                        }
                        break;
                    }
                    case CollectionChangeKind::Replace:
                        if (rec.index < items_.size()) {
                            items_[rec.index] = NormalizeItemValue(rec.newValue);
                        }
                        break;
                    case CollectionChangeKind::Reset:
                        return false;
                    case CollectionChangeKind::Clear:
                        items_.clear();
                        selIdx = -1;
                        break;
                }
            }
            SyncSelectionMetadataAfterMutation(selIdx);
            InvalidateLayout();
            return true;
        }

        bool totalCountChanged = false;
        bool windowStructureDirty = false;
        bool visibleSlotReboundInPlace = false;

        for (std::size_t c = 0u; c < changeCount; ++c) {
            const auto& rec = changes[c];
            switch (rec.kind) {
                case CollectionChangeKind::Add: {
                    const std::size_t appendIdx = items_.size();
                    items_.push_back(NormalizeItemValue(rec.newValue));
                    totalCountChanged = true;

                    std::size_t targetFirst = 0u;
                    std::size_t targetEnd = 0u;
                    Size dummyConstraint{};
                    ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, dummyConstraint);
                    if (appendIdx < targetEnd || targetFirst != firstRealizedIndex_ || targetEnd != realizedEndExclusive_) {
                        windowStructureDirty = true;
                    }
                    break;
                }
                case CollectionChangeKind::Insert: {
                    const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), NormalizeItemValue(rec.newValue));
                    if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                        ++selIdx;
                    }
                    totalCountChanged = true;

                    if (idx < realizedEndExclusive_) {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex != kInvalidVirtualSlotIndex && slot.itemIndex >= idx) {
                                ++slot.itemIndex;
                            }
                        }
                        windowStructureDirty = true;
                    } else {
                        std::size_t targetFirst = 0u;
                        std::size_t targetEnd = 0u;
                        Size dummyConstraint{};
                        ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, dummyConstraint);
                        if (idx < targetEnd || targetFirst != firstRealizedIndex_ || targetEnd != realizedEndExclusive_) {
                            windowStructureDirty = true;
                        }
                    }
                    break;
                }
                case CollectionChangeKind::RemoveAt: {
                    if (rec.index >= items_.size()) {
                        break;
                    }
                    const std::size_t idx = rec.index;
                    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                    if (selIdx >= 0) {
                        if (idx < static_cast<std::size_t>(selIdx)) {
                            --selIdx;
                        } else if (idx == static_cast<std::size_t>(selIdx)) {
                            selIdx = -1;
                        }
                    }
                    totalCountChanged = true;

                    if (items_.empty()) {
                        windowStructureDirty = true;
                    } else if (idx < realizedEndExclusive_) {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex == idx) {
                                slot.itemIndex = kInvalidVirtualSlotIndex;
                            } else if (slot.itemIndex != kInvalidVirtualSlotIndex && slot.itemIndex > idx) {
                                --slot.itemIndex;
                            }
                        }
                        windowStructureDirty = true;
                    } else {
                        std::size_t targetFirst = 0u;
                        std::size_t targetEnd = 0u;
                        Size dummyConstraint{};
                        ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, dummyConstraint);
                        if (targetFirst != firstRealizedIndex_ || targetEnd != realizedEndExclusive_) {
                            windowStructureDirty = true;
                        }
                    }
                    break;
                }
                case CollectionChangeKind::Replace: {
                    if (rec.index >= items_.size()) {
                        break;
                    }
                    const std::size_t idx = rec.index;
                    items_[idx] = NormalizeItemValue(rec.newValue);
                    if (!windowStructureDirty) {
                        if (idx >= firstRealizedIndex_ && idx < realizedEndExclusive_) {
                            for (auto& slot : virtualSlots_) {
                                if (slot.itemIndex == idx && slot.element) {
                                    if (RebindMaterializedItemInPlace(
                                            slot.element.Get(), slot.appliedTemplate, idx, items_[idx])) {
                                        const float itemH = GetVirtualItemHeight();
                                        const Thickness p = GetPadding();
                                        const float innerW = hasValidMeasureConstraint_
                                            ? lastChildAvailableSize_.width
                                            : std::max(0.0f, FinalRect().width - (p.left + p.right));
                                        const float prevEstW = estimatedMaxItemWidth_;
                                        slot.element->Measure(Size{innerW, itemH});
                                        estimatedMaxItemWidth_ =
                                            std::max(estimatedMaxItemWidth_, slot.element->DesiredSize().width);
                                        if (FinalRect().width > 0.0f) {
                                            const float startX = FinalRect().x + p.left;
                                            const float startY = FinalRect().y + p.top;
                                            const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                                            slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
                                        }
                                        // Fixed-height virtual slot update does not alter total DesiredSize unless max width grew
                                        if (estimatedMaxItemWidth_ <= prevEstW + 1e-3f) {
                                            ClearLayoutDirtyFlags(
                                                static_cast<std::uint8_t>(MeasureDirty | SubtreeMeasureDirty));
                                        } else {
                                            InvalidateLayout();
                                        }
                                        visibleSlotReboundInPlace = true;
                                    } else {
                                        slot.itemIndex = kInvalidVirtualSlotIndex;
                                        windowStructureDirty = true;
                                    }
                                    break;
                                }
                            }
                        }
                        // Out-of-viewport Replace: O(1) zero UIElement work, zero layout invalidation!
                    } else {
                        for (auto& slot : virtualSlots_) {
                            if (slot.itemIndex == idx) {
                                slot.itemIndex = kInvalidVirtualSlotIndex;
                                break;
                            }
                        }
                    }
                    break;
                }
                case CollectionChangeKind::Reset:
                    return false;
                case CollectionChangeKind::Clear: {
                    items_.clear();
                    selIdx = -1;
                    totalCountChanged = true;
                    windowStructureDirty = true;
                    break;
                }
            }
        }

        SyncSelectionMetadataAfterMutation(selIdx);

        if (items_.empty()) {
            estimatedMaxItemWidth_ = 80.0f;
            MoveVirtualSlotsToRecyclePool(registry, uiHeap);
            InvalidateLayout();
            return true;
        }

        if (windowStructureDirty) {
            std::size_t targetFirst = 0u;
            std::size_t targetEnd = 0u;
            Size itemConstraint{};
            ComputeCurrentVirtualWindow(lastChildAvailableSize_, targetFirst, targetEnd, itemConstraint);
            SyncVirtualizedWindow(targetFirst, targetEnd, itemConstraint);
            if (FinalRect().width > 0.0f) {
                const Thickness p = GetPadding();
                const float startX = FinalRect().x + p.left;
                const float startY = FinalRect().y + p.top;
                const float innerW = std::max(0.0f, FinalRect().width - (p.left + p.right));
                const float itemH = GetVirtualItemHeight();
                for (const auto& slot : virtualSlots_) {
                    if (slot.element) {
                        const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                        slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
                    }
                }
            }
            InvalidateLayout();
        } else if (totalCountChanged) {
            // Out-of-viewport Add / RemoveAt: 0 UIElements created/destroyed, only update total extent height!
            InvalidateLayout();
        } else if (visibleSlotReboundInPlace) {
            InvalidateRender();
        }
        return true;
    }

    // 3. Non-virtualized materialized ItemsControl incremental path
    bool needsGeneratorFullRefresh = false;
    for (std::size_t c = 0u; c < changeCount; ++c) {
        const auto& rec = changes[c];
        switch (rec.kind) {
            case CollectionChangeKind::Add: {
                const std::size_t idx = items_.size();
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_.push_back(norm);
                const AxbPackage* appliedTpl = nullptr;
                if (auto visual = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                    AddChild(visual.Get(), registry, uiHeap);
                }
                break;
            }
            case CollectionChangeKind::Insert: {
                const std::size_t idx = std::min<std::size_t>(rec.index, items_.size());
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), norm);
                if (selIdx >= 0 && idx <= static_cast<std::size_t>(selIdx)) {
                    ++selIdx;
                }
                if (itemGenerator_ && !itemBinder_ && idx + 1u < items_.size()) {
                    needsGeneratorFullRefresh = true;
                } else {
                    const AxbPackage* appliedTpl = nullptr;
                    if (auto visual = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                        InsertChildAt(visual.Get(), idx, registry, uiHeap);
                    }
                    if (itemBinder_) {
                        UIElement* cur = FirstChild();
                        for (std::size_t k = 0u; cur != nullptr && k < items_.size(); ++k, cur = cur->NextSibling()) {
                            if (k > idx) itemBinder_(k, items_[k], *cur);
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::RemoveAt: {
                if (rec.index >= items_.size()) break;
                const std::size_t idx = rec.index;
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
                if (selIdx >= 0) {
                    if (idx < static_cast<std::size_t>(selIdx)) --selIdx;
                    else if (idx == static_cast<std::size_t>(selIdx)) selIdx = -1;
                }
                if (itemGenerator_ && !itemBinder_ && idx < items_.size()) {
                    needsGeneratorFullRefresh = true;
                } else {
                    UIElement* targetCh = FirstChild();
                    for (std::size_t k = 0u; k < idx && targetCh != nullptr; ++k) {
                        targetCh = targetCh->NextSibling();
                    }
                    if (targetCh) {
                        ReleaseChildSubtreeResources(targetCh);
                        RemoveChild(targetCh, registry, uiHeap);
                    }
                    if (itemBinder_) {
                        UIElement* cur = FirstChild();
                        for (std::size_t k = 0u; cur != nullptr && k < items_.size(); ++k, cur = cur->NextSibling()) {
                            if (k >= idx) itemBinder_(k, items_[k], *cur);
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Replace: {
                if (rec.index >= items_.size()) break;
                const std::size_t idx = rec.index;
                const UnifiedValue norm = NormalizeItemValue(rec.newValue);
                items_[idx] = norm;
                if (!needsGeneratorFullRefresh) {
                    UIElement* targetCh = FirstChild();
                    for (std::size_t k = 0u; k < idx && targetCh != nullptr; ++k) {
                        targetCh = targetCh->NextSibling();
                    }
                    const AxbPackage* expectedTpl = !itemGenerator_ ? ResolveActiveItemTemplate(norm) : nullptr;
                    if (targetCh && RebindMaterializedItemInPlace(targetCh, expectedTpl, idx, norm)) {
                        // Rebound existing visual in-place
                    } else if (targetCh) {
                        ReleaseChildSubtreeResources(targetCh);
                        RemoveChild(targetCh, registry, uiHeap);
                        const AxbPackage* appliedTpl = nullptr;
                        if (auto replacement = CreateMaterializedItemVisual(idx, norm, appliedTpl)) {
                            InsertChildAt(replacement.Get(), idx, registry, uiHeap);
                        }
                    }
                }
                break;
            }
            case CollectionChangeKind::Reset:
                return false;
            case CollectionChangeKind::Clear: {
                items_.clear();
                selIdx = -1;
                needsGeneratorFullRefresh = false;
                while (UIElement* ch = FirstChild()) {
                    ReleaseChildSubtreeResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
                break;
            }
        }
    }

    if (needsGeneratorFullRefresh) {
        RebuildAllMaterializedItems(registry, uiHeap);
    }
    realizedEndExclusive_ = items_.size();
    SyncSelectionMetadataAfterMutation(selIdx);
    InvalidateLayout();
    return true;
}

void ItemsControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Panel::OnPropertyChanged(propId, newValue);
    if (propId == PropId_SelectedIndex && !IsA<ComboBox>() && !syncingItemsSelection_) {
        SyncSelectionMetadataAfterMutation(GetSelectedIndex());
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
        InvalidateRender();
        return;
    }

    if (propId == PropId_SelectedItem && !IsA<ComboBox>() && !syncingItemsSelection_) {
        if (std::holds_alternative<std::monostate>(newValue) ||
            std::holds_alternative<NullObject>(newValue)) {
            if (GetSelectedIndex() != -1) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(-1);
                syncingItemsSelection_ = false;
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        } else {
            const std::int32_t matchedIdx = FindMatchingItemIndex(newValue);
            if (matchedIdx >= 0 && matchedIdx != GetSelectedIndex()) {
                syncingItemsSelection_ = true;
                SetSelectedIndex(matchedIdx);
                syncingItemsSelection_ = false;
                (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
            }
        }
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
        InvalidateRender();
        return;
    }

    if ((propId == PropId_IsVirtualizing || propId == PropId_ItemTemplate) &&
        !IsA<ComboBox>()) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (propId == PropId_ItemTemplate) {
            ClearRecyclePool(registry, uiHeap);
        }
        if (items_.empty()) {
            if (!IsVirtualizing()) {
                ClearRecyclePool(registry, uiHeap);
            }
            return;
        }
        if (IsVirtualizing()) {
            if (propId == PropId_ItemTemplate) {
                virtualSlots_.clear();
                firstRealizedIndex_ = 0u;
                realizedEndExclusive_ = 0u;
                while (UIElement* ch = FirstChild()) {
                    ReleaseChildSubtreeResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
            } else {
                virtualSlots_.clear();
                firstRealizedIndex_ = 0u;
                realizedEndExclusive_ = 0u;
                while (UIElement* ch = FirstChild()) {
                    ReleaseChildSubtreeResources(ch);
                    RemoveChild(ch, registry, uiHeap);
                }
            }
            InvalidateLayout();
        } else {
            RebuildAllMaterializedItems(registry, uiHeap);
        }
        return;
    }

    if (propId == PropId_ItemsSource) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (const auto* objRef = std::get_if<ObjectRef>(&newValue)) {
            if (objRef->valid() && domains_) {
                if (auto* heap = domains_->heapForObject(*objRef)) {
                    const ObjectRef arrRef = ObservableListHelper::ResolveBackingArray(*heap, *objRef);
                    if (arrRef.valid()) {
                        const std::size_t len = heap->arrayLength(arrRef).value_or(0);
                        std::vector<UnifiedValue> extracted;
                        extracted.reserve(len);
                        for (std::size_t i = 0; i < len; ++i) {
                            UnifiedValue elem = heap->arrayGet(arrRef, i).value_or(UnifiedValue{});
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
                   std::holds_alternative<NullObject>(newValue) ||
                   std::holds_alternative<NullArray>(newValue)) {
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
    lastChildAvailableSize_ = childAvail;
    hasValidMeasureConstraint_ = true;

    if (IsVirtualizing() && !IsA<ComboBox>()) {
        if (items_.empty()) {
            return Size{padW, padH};
        }
        const float itemH = GetVirtualItemHeight();
        const float totalLogicalH = static_cast<float>(items_.size()) * itemH;

        float viewTop = 0.0f;
        float viewH = 240.0f;
        if (!ComputeLiveScrollViewport(viewTop, viewH)) {
            if (childAvail.height < kUnconstrainedSpace * 0.5f && childAvail.height > 0.0f) {
                viewH = childAvail.height;
            } else if (FinalRect().height > 0.0f) {
                viewH = std::max(itemH, FinalRect().height - (p.top + p.bottom));
            }
        }

        const float maxScrollTop = std::max(0.0f, totalLogicalH - viewH);
        viewTop = std::min(viewTop, maxScrollTop);

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(viewTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((viewTop + viewH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));
        const Size slotConstraint{childAvail.width, itemH};

        // O(1) zero-allocation fast path when realized virtual window indices are already intact!
        if (!virtualSlots_.empty() &&
            firstIdx == firstRealizedIndex_ &&
            endIdx == realizedEndExclusive_ &&
            virtualSlots_.size() == (endIdx - firstIdx)) {
            bool contiguous = true;
            for (std::size_t i = 0u; i < virtualSlots_.size(); ++i) {
                if (!virtualSlots_[i].element || virtualSlots_[i].itemIndex != firstIdx + i) {
                    contiguous = false;
                    break;
                }
            }
            if (contiguous) {
                for (auto& slot : virtualSlots_) {
                    slot.element->Measure(slotConstraint);
                    estimatedMaxItemWidth_ =
                        std::max(estimatedMaxItemWidth_, slot.element->DesiredSize().width);
                }
                return Size{estimatedMaxItemWidth_ + padW, totalLogicalH + padH};
            }
        }

        SyncVirtualizedWindow(firstIdx, endIdx, slotConstraint);
        return Size{estimatedMaxItemWidth_ + padW, totalLogicalH + padH};
    }

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
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));

    if (IsVirtualizing() && !IsA<ComboBox>()) {
        if (items_.empty()) {
            return;
        }
        const float itemH = GetVirtualItemHeight();
        float visibleTop = 0.0f;
        float visibleH = std::max(itemH, finalRect.height - (p.top + p.bottom));
        if (ComputeLiveScrollViewport(visibleTop, visibleH)) {
            visibleH = std::max(itemH, visibleH);
        }

        const float totalLogicalH = static_cast<float>(items_.size()) * itemH;
        const float maxScrollTop = std::max(0.0f, totalLogicalH - visibleH);
        visibleTop = std::min(visibleTop, maxScrollTop);

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(visibleTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((visibleTop + visibleH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));

        const float prevEstimatedMaxW = estimatedMaxItemWidth_;
        if (firstIdx != firstRealizedIndex_ || endIdx != realizedEndExclusive_ || virtualSlots_.empty()) {
            SyncVirtualizedWindow(firstIdx, endIdx, Size{innerW, itemH});
        }

        for (const auto& slot : virtualSlots_) {
            if (slot.element) {
                const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
            }
        }

        // Preserve MeasureDirty if estimatedMaxItemWidth_ grew; otherwise clear transient MeasureDirty.
        if (estimatedMaxItemWidth_ > prevEstimatedMaxW + 1e-3f) {
            InvalidateLayout();
        } else {
            ClearLayoutDirtyFlags(static_cast<std::uint8_t>(MeasureDirty | SubtreeMeasureDirty));
            for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
                bool anyChildMeasureDirty = false;
                for (const UIElement* sib = anc->FirstChild(); sib != nullptr; sib = sib->NextSibling()) {
                    if ((sib->GetLayoutDirtyFlags() & (MeasureDirty | SubtreeMeasureDirty)) != 0u) {
                        anyChildMeasureDirty = true;
                        break;
                    }
                }
                if (!anyChildMeasureDirty) {
                    anc->ClearLayoutDirtyFlags(SubtreeMeasureDirty);
                } else {
                    break;
                }
            }
        }
        return;
    }

    float cursorY = startY;
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        const Size d = c->DesiredSize();
        c->Arrange(Rect{startX, cursorY, innerW, d.height});
        cursorY += d.height;
    }
}

// ============================================================================
// ListBox
// ============================================================================

ListBox::ListBox() {
    EnsureControlTypesRegistered();
}

void ListBox::MarkExtraValuesInPools() const noexcept {
    ItemsControl::MarkExtraValuesInPools();
    if (lastNotifiedSelectedItem_.isTraceable()) {
        rs_rt::markValueInPools(lastNotifiedSelectedItem_);
    }
}

void ListBox::ScrollItemIntoView(std::int32_t index) noexcept {
    if (index < 0) {
        return;
    }
    ScrollViewer* sv = nullptr;
    for (UIElement* anc = Parent(); anc != nullptr; anc = anc->Parent()) {
        if (auto* s = anc->DynamicCast<ScrollViewer>()) {
            sv = s;
            break;
        }
    }
    if (!sv) {
        return;
    }

    const Thickness p = GetPadding();
    const UIElement* svChild = this;
    while (svChild->Parent() != nullptr && svChild->Parent() != sv) {
        svChild = svChild->Parent();
    }
    float staticRowStartInContent = GetMargin().top + p.top;
    if (this != svChild) {
        if (FinalRect().height > 0.0f || svChild->FinalRect().height > 0.0f) {
            staticRowStartInContent =
                (FinalRect().y - (svChild->FinalRect().y - svChild->GetMargin().top)) + p.top;
        } else {
            for (const UIElement* cur = Parent(); cur != nullptr && cur != sv; cur = cur->Parent()) {
                staticRowStartInContent += cur->GetMargin().top + cur->GetPadding().top;
            }
        }
    }

    float relTop = 0.0f;
    float rowH = GetVirtualItemHeight();
    if (IsVirtualizing()) {
        relTop = static_cast<float>(index) * rowH;
    } else {
        std::int32_t curIdx = 0;
        float accumY = 0.0f;
        bool foundChild = false;
        for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++curIdx) {
            const float chH = (c->FinalRect().height > 0.0f)
                ? c->FinalRect().height
                : ((c->DesiredSize().height > 0.0f) ? c->DesiredSize().height : rowH);
            if (curIdx == index) {
                if (c->FinalRect().height > 0.0f && FinalRect().height > 0.0f) {
                    relTop = c->FinalRect().y - (FinalRect().y + p.top);
                } else {
                    relTop = accumY;
                }
                rowH = chH;
                foundChild = true;
                break;
            }
            accumY += chH;
        }
        if (!foundChild) {
            relTop = static_cast<float>(index) * rowH;
        }
    }

    float dummyViewTop = 0.0f;
    float viewH = 0.0f;
    (void)ComputeLiveScrollViewport(dummyViewTop, viewH);
    if (viewH <= 0.0f) {
        return;
    }

    const float itemTop = staticRowStartInContent + relTop;
    const float itemBottom = itemTop + rowH;
    const float curScrollY = std::max(0.0f, sv->GetVerticalOffset());
    if (itemTop < curScrollY) {
        sv->SetVerticalOffset(std::max(0.0f, itemTop));
    } else if (itemBottom > curScrollY + viewH) {
        sv->SetVerticalOffset(std::max(0.0f, itemBottom - viewH));
    }
}

bool ListBox::SelectIndexInteractive(std::int32_t index) {
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    ScrollItemIntoView(clamped);
    return clamped != prev;
}

bool ListBox::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }

    // 1. Check arranged realized child container bounds first
    std::size_t childOffset = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++childOffset) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const Rect cr = c->FinalRect();
        if (cr.height > 0.0f && localPoint.y >= cr.y && localPoint.y < cr.y + cr.height) {
            const std::size_t logicalIdx = IsVirtualizing()
                ? (FirstRealizedItemIndex() + childOffset)
                : childOffset;
            if (logicalIdx < count) {
                (void)SelectIndexInteractive(static_cast<std::int32_t>(logicalIdx));
                return true;
            }
        }
    }

    // 2. Fallback to (localPoint.y - (FinalRect().y + Padding.top)) / GetVirtualItemHeight() or unarranged child heights
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float relY = localPoint.y - (r.y + p.top);
    if (relY < 0.0f) {
        return false;
    }
    if (IsVirtualizing() || FirstChild() == nullptr) {
        const float itemH = std::max(1.0f, GetVirtualItemHeight());
        const auto hitIdx = static_cast<std::int32_t>(std::floor(relY / itemH));
        if (hitIdx >= 0 && static_cast<std::size_t>(hitIdx) < count) {
            (void)SelectIndexInteractive(hitIdx);
            return true;
        }
        return false;
    }

    float accumY = 0.0f;
    std::size_t idx = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr && idx < count; c = c->NextSibling(), ++idx) {
        if (c->GetVisibility() == Visibility::Collapsed) {
            continue;
        }
        const float chH = (c->DesiredSize().height > 0.0f) ? c->DesiredSize().height : GetVirtualItemHeight();
        if (relY >= accumY && relY < accumY + chH) {
            (void)SelectIndexInteractive(static_cast<std::int32_t>(idx));
            return true;
        }
        accumY += chH;
    }
    return false;
}

bool ListBox::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = TotalItemCount();
    if (count == 0u) {
        return false;
    }
    const std::int32_t cur = GetSelectedIndex();
    switch (key) {
        case KeyCode::Down: {
            const std::int32_t next = (cur < 0)
                ? 0
                : std::min<std::int32_t>(static_cast<std::int32_t>(count - 1u), cur + 1);
            (void)SelectIndexInteractive(next);
            return true;
        }
        case KeyCode::Up: {
            const std::int32_t prev = (cur <= 0) ? 0 : (cur - 1);
            (void)SelectIndexInteractive(prev);
            return true;
        }
        case KeyCode::Home:
            (void)SelectIndexInteractive(0);
            return true;
        case KeyCode::End:
            (void)SelectIndexInteractive(static_cast<std::int32_t>(count - 1u));
            return true;
        default:
            return false;
    }
}

void ListBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ItemsControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_SelectedIndex || propId == PropId_SelectedItem) {
        if (IsDeferringSelectionNotify()) {
            return;
        }
        const std::int32_t curIdx = GetSelectedIndex();
        const UnifiedValue curItem = GetSelectedItem();
        const bool indexChanged = (!hasNotifiedSelection_ && curIdx != lastNotifiedSelectedIndex_) ||
                                  (hasNotifiedSelection_ && curIdx != lastNotifiedSelectedIndex_);
        const bool itemChanged = hasNotifiedSelection_ &&
                                 !CollectionItemValuesEqual(curItem, lastNotifiedSelectedItem_);
        if (indexChanged || itemChanged) {
            hasNotifiedSelection_ = true;
            lastNotifiedSelectedIndex_ = curIdx;
            lastNotifiedSelectedItem_ = curItem;
            if (lastNotifiedSelectedItem_.isTraceable()) {
                rs_rt::pinValueInPools(lastNotifiedSelectedItem_);
                EnsureRegisteredInValuePoolSet();
            }
            if (selectionChangedCallback_) {
                selectionChangedCallback_(*this, curIdx);
            }
        }
    }
}

// ============================================================================
// ComboBox
// ============================================================================

ComboBox::ComboBox() {
    EnsureControlTypesRegistered();
}

std::size_t ComboBox::ComboItemCount() const noexcept {
    if (!Items().empty()) {
        return Items().size();
    }
    return ChildCount();
}

UnifiedValue ComboBox::GetComboItemValue(std::size_t index) const {
    if (!Items().empty()) {
        return (index < Items().size()) ? Items()[index] : UnifiedValue{};
    }
    std::size_t cur = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
        if (cur == index) {
            return value::FromString(ExtractElementDisplayText(c));
        }
    }
    return UnifiedValue{};
}

std::string ComboBox::GetComboItemText(std::size_t index) const {
    if (!Items().empty()) {
        if (index < Items().size()) {
            return rs_rt::valueToString(Items()[index]);
        }
        return {};
    }
    std::size_t cur = 0u;
    for (const UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling(), ++cur) {
        if (cur == index) {
            return ExtractElementDisplayText(c);
        }
    }
    return {};
}

void ComboBox::SetSelectedIndex(std::int32_t index) {
    SetValue(PropId_SelectedIndex, value::FromInt(index), ValueSourceRank::Local);
}

std::int32_t ComboBox::GetSelectedIndex() const noexcept {
    const UnifiedValue v = GetValue(PropId_SelectedIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<std::int32_t>(*i);
    }
    return -1;
}

UnifiedValue ComboBox::GetSelectedItem() const {
    const UnifiedValue explicitItem = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(explicitItem)) {
        return explicitItem;
    }
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < ComboItemCount()) {
        return GetComboItemValue(static_cast<std::size_t>(idx));
    }
    return UnifiedValue{};
}

std::string ComboBox::GetSelectedItemText() const {
    const std::int32_t idx = GetSelectedIndex();
    if (idx >= 0 && static_cast<std::size_t>(idx) < ComboItemCount()) {
        return GetComboItemText(static_cast<std::size_t>(idx));
    }
    const UnifiedValue itemVal = GetValue(PropId_SelectedItem);
    if (!std::holds_alternative<std::monostate>(itemVal)) {
        return rs_rt::valueToString(itemVal);
    }
    return {};
}

void ComboBox::SetIsDropDownOpen(bool open) {
    SetValue(PropId_IsDropDownOpen, UnifiedValue{open}, ValueSourceRank::Local);
}

bool ComboBox::IsDropDownOpen() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsDropDownOpen);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void ComboBox::SyncSelectedItemFromIndex() {
    if (syncingSelection_) {
        return;
    }
    const std::int32_t idx = GetSelectedIndex();
    const std::size_t count = ComboItemCount();
    if (idx >= 0 && static_cast<std::size_t>(idx) < count) {
        syncingSelection_ = true;
        SetValue(PropId_SelectedItem, GetComboItemValue(static_cast<std::size_t>(idx)), ValueSourceRank::Local);
        syncingSelection_ = false;
    }
}

bool ComboBox::SelectIndexInteractive(std::int32_t index) {
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        SetIsDropDownOpen(false);
        return false;
    }
    const std::int32_t clamped = std::clamp<std::int32_t>(
        index,
        0,
        static_cast<std::int32_t>(count - 1u));
    const std::int32_t prev = GetSelectedIndex();
    SetSelectedIndex(clamped);
    SyncSelectedItemFromIndex();
    SetIsDropDownOpen(false);
    highlightedPopupIndex_ = -1;

    if (clamped != prev) {
        if (selectionChangedCallback_) {
            selectionChangedCallback_(*this, clamped);
        }
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedIndex);
        (void)NotifyTwoWayPropertyMutated(PropId_SelectedItem);
        return true;
    }
    return false;
}

Rect ComboBox::DropDownPopupLocalRect() const noexcept {
    if (!IsDropDownOpen()) {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const Rect r = FinalRect();
    const float w = (r.width > 0.0f) ? r.width : std::max(100.0f, GetWidth());
    const float h = (r.height > 0.0f) ? r.height : std::max(28.0f, GetHeight());
    return Rect{
        r.x,
        r.y + h + 2.0f,
        w,
        static_cast<float>(count) * kPopupRowHeight};
}

Rect ComboBox::DropDownPopupScreenRect() const noexcept {
    return LocalToScreenBounds(DropDownPopupLocalRect());
}

std::int32_t ComboBox::HitTestPopupItemIndex(Point screenPoint) const noexcept {
    if (!IsDropDownOpen()) {
        return -1;
    }
    const std::size_t count = ComboItemCount();
    if (count == 0u) {
        return -1;
    }
    const Point localPt = ScreenToLocal(screenPoint);
    const Rect popup = DropDownPopupLocalRect();
    if (popup.width <= 0.0f || popup.height <= 0.0f ||
        localPt.x < popup.x || localPt.x > popup.x + popup.width ||
        localPt.y < popup.y || localPt.y > popup.y + popup.height) {
        return -1;
    }
    const float relY = localPt.y - popup.y;
    const auto idx = static_cast<std::int32_t>(std::floor(relY / kPopupRowHeight));
    return std::clamp<std::int32_t>(idx, 0, static_cast<std::int32_t>(count - 1u));
}

void ComboBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ItemsControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Visibility && GetVisibility() != Visibility::Visible && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    } else if (propId == PropId_ItemsSource || propId == PropId_SelectedIndex) {
        SyncSelectedItemFromIndex();
    } else if (propId == PropId_SelectedItem && !syncingSelection_) {
        const std::string targetStr = rs_rt::valueToString(newValue);
        const std::size_t count = ComboItemCount();
        for (std::size_t i = 0; i < count; ++i) {
            if (GetComboItemText(i) == targetStr) {
                syncingSelection_ = true;
                SetSelectedIndex(static_cast<std::int32_t>(i));
                syncingSelection_ = false;
                break;
            }
        }
    }
}

void ComboBox::OnFocusChanged(bool focused) {
    if (!focused && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    }
}

void ComboBox::OnEnabledStateChanged(bool effectivelyEnabled) {
    if (!effectivelyEnabled && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
    }
}

bool ComboBox::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    (void)localPoint;
    (void)modifiers;
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const bool nextOpen = !IsDropDownOpen();
    SetIsDropDownOpen(nextOpen);
    highlightedPopupIndex_ = nextOpen ? GetSelectedIndex() : -1;
    return true;
}

bool ComboBox::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    (void)modifiers;
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const std::size_t count = ComboItemCount();
    if (key == KeyCode::Enter || key == KeyCode::Space) {
        if (IsDropDownOpen()) {
            const std::int32_t pick = (highlightedPopupIndex_ >= 0)
                ? highlightedPopupIndex_
                : std::max<std::int32_t>(0, GetSelectedIndex());
            (void)SelectIndexInteractive(pick);
        } else {
            SetIsDropDownOpen(true);
            highlightedPopupIndex_ = GetSelectedIndex();
        }
        return true;
    }
    if (key == KeyCode::Escape && IsDropDownOpen()) {
        SetIsDropDownOpen(false);
        highlightedPopupIndex_ = -1;
        return true;
    }
    if (count > 0u && (key == KeyCode::Up || key == KeyCode::Down)) {
        if (IsDropDownOpen()) {
            std::int32_t cur = (highlightedPopupIndex_ >= 0)
                ? highlightedPopupIndex_
                : std::max<std::int32_t>(0, GetSelectedIndex());
            cur += (key == KeyCode::Down) ? 1 : -1;
            cur = std::clamp<std::int32_t>(cur, 0, static_cast<std::int32_t>(count - 1u));
            SetHighlightedPopupIndex(cur);
        } else {
            const std::int32_t cur = std::max<std::int32_t>(0, GetSelectedIndex());
            const std::int32_t next = std::clamp<std::int32_t>(
                cur + ((key == KeyCode::Down) ? 1 : -1),
                0,
                static_cast<std::int32_t>(count - 1u));
            (void)SelectIndexInteractive(next);
        }
        return true;
    }
    return false;
}

Size ComboBox::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float padW = (p.left + p.right > 0.0f) ? (p.left + p.right) : 12.0f;
    const float padH = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 6.0f;
    float fontSize = 14.0f;
    (void)TryGetFloat(GetValue(PropId_FontSize), fontSize);

    float maxTextW = 64.0f;
    const std::size_t count = ComboItemCount();
    for (std::size_t i = 0; i < count; ++i) {
        maxTextW = std::max(maxTextW, MeasureUtf8TextRun(GetComboItemText(i), fontSize).width);
    }
    // Collapse inline item visuals so static <ComboBoxItem> children don't render in-flow or intercept hits when closed
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Measure(Size{0.0f, 0.0f});
    }
    return Size{
        std::max(100.0f, maxTextW + 28.0f + padW),
        std::max(28.0f, fontSize * 1.25f + padH)};
}

void ComboBox::ArrangeCore(Rect finalRect) noexcept {
    for (UIElement* c = FirstChild(); c != nullptr; c = c->NextSibling()) {
        c->Arrange(Rect{finalRect.x, finalRect.y, 0.0f, 0.0f});
    }
}

// ============================================================================
// AxbTemplateRegistry & UserControl Self-Contained Mounting
// ============================================================================

namespace {

const UIElement* NextResourceScopeAncestor(const UIElement* elem) noexcept {
    if (!elem) {
        return nullptr;
    }
    if (const UIElement* p = elem->Parent()) {
        return p;
    }
    return elem->TemplatedParent();
}

bool IsShadowedByCloserScopeLocked(
    const std::unordered_map<const UIElement*, std::unordered_map<std::string, UnifiedValue>>& scopedMap,
    const UIElement* target,
    const UIElement* outerScope,
    const std::string& key) noexcept {
    if (!target || !outerScope || scopedMap.empty()) {
        return false;
    }
    for (const UIElement* cur = target;
         cur != nullptr && cur != outerScope;
         cur = NextResourceScopeAncestor(cur)) {
        const auto scopeIt = scopedMap.find(cur);
        if (scopeIt != scopedMap.end() && scopeIt->second.find(key) != scopeIt->second.end()) {
            return true;
        }
    }
    return false;
}

void VisitPoolValue(const UnifiedValue& val, bool pinMode) noexcept {
    if (val.isTraceable()) {
        if (pinMode) {
            rs_rt::pinValueInPools(val);
        } else {
            rs_rt::markValueInPools(val);
        }
    }
}

void VisitStoryboardPoolValues(const AxbStoryboard& sb, bool pinMode) noexcept {
    for (const auto& tl : sb.timelines) {
        VisitPoolValue(tl.fromValue, pinMode);
        VisitPoolValue(tl.toValue, pinMode);
        for (const auto& kf : tl.keyFrames) {
            VisitPoolValue(kf.value, pinMode);
        }
    }
}

void VisitMultiBindingPoolValues(const AxbCompiledMultiBinding& mb, bool pinMode, int depth) noexcept;

void VisitBindingChainPoolValues(const AxbCompiledBindingChain& chain, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    VisitPoolValue(chain.fallbackValue, pinMode);
    if (chain.nestedMultiBinding) {
        VisitMultiBindingPoolValues(*chain.nestedMultiBinding, pinMode, depth + 1);
    }
}

void VisitMultiBindingPoolValues(const AxbCompiledMultiBinding& mb, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    VisitPoolValue(mb.fallbackValue, pinMode);
    for (const auto& src : mb.sources) {
        VisitBindingChainPoolValues(src, pinMode, depth + 1);
    }
}

void VisitInteractionActionPoolValues(const AxbInteractionAction& act, bool pinMode) noexcept {
    VisitPoolValue(act.value, pinMode);
    VisitStoryboardPoolValues(act.inlineStoryboard, pinMode);
    VisitPoolValue(act.commandParameter, pinMode);
    VisitBindingChainPoolValues(act.commandParameterBinding, pinMode, 0);
}

void VisitStyleTriggerPoolValues(const AxbStyleTrigger& tr, bool pinMode) noexcept {
    for (const auto& cond : tr.conditions) {
        VisitPoolValue(cond.expectedValue, pinMode);
    }
    for (const auto& s : tr.setters) {
        VisitPoolValue(s.value, pinMode);
    }
    for (const auto& sb : tr.enterStoryboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& sb : tr.exitStoryboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& act : tr.enterActions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
    for (const auto& act : tr.exitActions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
}

void VisitEventTriggerPoolValues(const AxbEventTrigger& et, bool pinMode) noexcept {
    for (const auto& sb : et.storyboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& act : et.actions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
}

void VisitVisualStateGroupPoolValues(const AxbVisualStateGroup& vsg, bool pinMode) noexcept {
    for (const auto& vs : vsg.states) {
        for (const auto& s : vs.setters) {
            VisitPoolValue(s.value, pinMode);
        }
        VisitStoryboardPoolValues(vs.storyboard, pinMode);
    }
}

void VisitPackagePoolValues(const AxbPackage& pkg, bool pinMode, int depth = 0) noexcept;

void VisitControlTemplatePoolValues(
    const AxbControlTemplate& ct,
    bool pinMode,
    int depth = 0) noexcept {
    if (depth > 32) {
        return;
    }
    if (ct.templatePackage) {
        VisitPackagePoolValues(*ct.templatePackage, pinMode, depth + 1);
    }
    for (const auto& tr : ct.triggers) {
        VisitStyleTriggerPoolValues(tr, pinMode);
    }
    for (const auto& et : ct.eventTriggers) {
        VisitEventTriggerPoolValues(et, pinMode);
    }
    for (const auto& vsg : ct.visualStateGroups) {
        VisitVisualStateGroupPoolValues(vsg, pinMode);
    }
}

void VisitStylePoolValues(
    const AxbStyleDefinition& st,
    bool pinMode,
    int depth = 0) noexcept {
    if (depth > 32) {
        return;
    }
    if (st.inlineControlTemplate) {
        VisitControlTemplatePoolValues(*st.inlineControlTemplate, pinMode, depth + 1);
    }
    for (const auto& s : st.setters) {
        VisitPoolValue(s.value, pinMode);
    }
    for (const auto& tr : st.triggers) {
        VisitStyleTriggerPoolValues(tr, pinMode);
    }
    for (const auto& et : st.eventTriggers) {
        VisitEventTriggerPoolValues(et, pinMode);
    }
    for (const auto& vsg : st.visualStateGroups) {
        VisitVisualStateGroupPoolValues(vsg, pinMode);
    }
}

void VisitPackagePoolValues(const AxbPackage& pkg, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    if (pinMode) {
        pkg.PinValuesInPools();
    } else {
        pkg.MarkValuesInPools();
    }
    for (const auto& kv : pkg.resources) {
        VisitPoolValue(kv.second, pinMode);
    }
    for (const auto& st : pkg.styles) {
        VisitStylePoolValues(st, pinMode, depth + 1);
    }
    for (const auto& sb : pkg.storyboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& dt : pkg.dataTemplates) {
        if (dt.templatePackage) {
            VisitPackagePoolValues(*dt.templatePackage, pinMode, depth + 1);
        }
    }
    for (const auto& ct : pkg.controlTemplates) {
        VisitControlTemplatePoolValues(ct, pinMode, depth + 1);
    }
}

struct LiveAxbTemplateRegistrySet {
    std::mutex mutex;
    std::vector<AxbTemplateRegistry*> registries;

    static LiveAxbTemplateRegistrySet& Instance() noexcept {
        static LiveAxbTemplateRegistrySet sSet;
        return sSet;
    }

    void Add(AxbTemplateRegistry* reg) {
        if (!reg) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* existing : registries) {
            if (existing == reg) return;
        }
        registries.push_back(reg);
    }

    bool Remove(AxbTemplateRegistry* reg) noexcept {
        if (!reg) return false;
        std::lock_guard<std::mutex> lock(mutex);
        registries.erase(
            std::remove(registries.begin(), registries.end(), reg),
            registries.end());
        return registries.empty();
    }

    bool Contains(const AxbTemplateRegistry* reg) noexcept {
        if (!reg) return false;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto* existing : registries) {
            if (existing == reg) return true;
        }
        return false;
    }

    void UnsubscribeElementAll(const UIElement* elem) noexcept {
        if (!elem) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* reg : registries) {
            if (reg && reg->HasAnyDynamicSubscriptions()) {
                reg->UnsubscribeElement(elem);
            }
        }
    }

    template <typename Fn>
    void ForEachLocked(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex);
        fn(registries);
    }
};

} // namespace

AxbTemplateRegistry& AxbTemplateRegistry::Instance() noexcept {
    return UiResourceContext::Current().Templates();
}

bool AxbTemplateRegistry::IsLiveRegistry(const AxbTemplateRegistry* registry) noexcept {
    return LiveAxbTemplateRegistrySet::Instance().Contains(registry);
}

AxbTemplateRegistry::AxbTemplateRegistry() noexcept {
    LiveAxbTemplateRegistrySet::Instance().Add(this);
    rs_rt::registerExternalValuePoolRootMarker(
        this,
        [this]() noexcept {
            MarkValuesInPools();
        });
    static std::once_flag sResourceHooksOnce;
    std::call_once(sResourceHooksOnce, []() noexcept {
        RegisterUIElementResourceHooks(
            [](const UIElement* elem) noexcept {
                if (!elem) {
                    return;
                }
                LiveAxbTemplateRegistrySet::Instance().UnsubscribeElementAll(elem);
            },
            [](UIElement* elem, std::string_view locSourceUri) {
                if (!elem || locSourceUri.empty()) {
                    return;
                }
                if (UiResourceContext::HasActiveThreadOverride()) {
                    (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(locSourceUri, elem);
                    return;
                }
                for (const UIElement* cur = elem;
                     cur != nullptr;
                     cur = NextResourceScopeAncestor(cur)) {
                    if (DualTrackBindingEngine* engine = cur->BoundTwoWayEngine()) {
                        if (std::shared_ptr<UiResourceContext> domainCtx =
                                UiResourceContext::FindForDomains(&engine->Domains())) {
                            ScopedUiResourceContext scope(std::move(domainCtx));
                            (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(locSourceUri, elem);
                            return;
                        }
                    }
                }

                std::vector<PendingResourceUpdate> updates;
                std::vector<HostWakeEntry> wakeList;
                bool handledUnderSetLock = false;
                LiveAxbTemplateRegistrySet::Instance().ForEachLocked(
                    [&](const std::vector<AxbTemplateRegistry*>& liveRegs) {
                        AxbTemplateRegistry* targetReg = nullptr;
                        for (auto* reg : liveRegs) {
                            if (reg && reg->HasSubscriptionInSubtree(elem) &&
                                reg->FindDictionary(locSourceUri) != nullptr) {
                                targetReg = reg;
                                break;
                            }
                        }
                        if (!targetReg) {
                            AxbTemplateRegistry& fallbackReg = AxbTemplateRegistry::Instance();
                            if (fallbackReg.FindDictionary(locSourceUri) != nullptr) {
                                targetReg = &fallbackReg;
                            }
                        }
                        if (!targetReg) {
                            for (auto* reg : liveRegs) {
                                if (reg && reg->FindDictionary(locSourceUri) != nullptr) {
                                    targetReg = reg;
                                    break;
                                }
                            }
                        }
                        if (!targetReg) {
                            return;
                        }
                        const AxbPackage* dictPtr = targetReg->FindDictionary(locSourceUri);
                        if (!dictPtr) {
                            return;
                        }
                        const AxbPackage dictCopy = *dictPtr;
                        targetReg->CollectDynamicDictionaryUpdates(dictCopy, elem, updates, wakeList);
                        for (const auto& upd : updates) {
                            if (upd.propId == PropId_PathData) {
                                if (const auto* sv = std::get_if<StringValue>(&upd.newValue)) {
                                    (void)targetReg->FindOrBakeMtsdfPath(sv->view());
                                }
                            }
                        }
                        handledUnderSetLock = true;
                    });
                if (!handledUnderSetLock) {
                    return;
                }
                for (const auto& upd : updates) {
                    ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
                }
                for (const auto& wake : wakeList) {
                    if (wake.callback) {
                        wake.callback(wake.userData);
                    }
                }
            });
    });
}

AxbTemplateRegistry::~AxbTemplateRegistry() {
    (void)LiveAxbTemplateRegistrySet::Instance().Remove(this);
    rs_rt::unregisterExternalValuePoolRootMarker(this);
}

void AxbTemplateRegistry::MarkValuesInPools() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : templates_) {
        VisitPackagePoolValues(kv.second, false);
    }
    for (const auto& kv : registeredDictionaries_) {
        VisitPackagePoolValues(kv.second, false);
    }
    for (const auto& kv : resources_) {
        VisitPoolValue(kv.second, false);
    }
    for (const auto& kv : dynamicOverrides_) {
        VisitPoolValue(kv.second, false);
    }
    for (const auto& scopeKv : scopedDynamicOverrides_) {
        for (const auto& resKv : scopeKv.second) {
            VisitPoolValue(resKv.second, false);
        }
    }
    for (const auto& kv : styles_) {
        VisitStylePoolValues(kv.second, false);
    }
    for (const auto& kv : implicitStyles_) {
        VisitStylePoolValues(kv.second, false);
    }
    for (const auto& kv : storyboards_) {
        VisitStoryboardPoolValues(kv.second, false);
    }
    for (const auto& kv : dataTemplates_) {
        if (kv.second) VisitPackagePoolValues(*kv.second, false);
    }
    for (const auto& kv : implicitDataTemplates_) {
        if (kv.second) VisitPackagePoolValues(*kv.second, false);
    }
    for (const auto& kv : controlTemplates_) {
        VisitControlTemplatePoolValues(kv.second, false);
    }
    for (const auto& kv : implicitControlTemplates_) {
        VisitControlTemplatePoolValues(kv.second, false);
    }
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
    VisitPackagePoolValues(package, true);
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
    for (const auto& st : package.styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : package.storyboards) {
        if (!sb.key.empty()) {
            storyboards_[sb.key] = sb;
        }
    }
    for (const auto& dt : package.dataTemplates) {
        if (!dt.templatePackage) continue;
        if (!dt.key.empty()) {
            dataTemplates_[dt.key] = dt.templatePackage;
        }
        if (!dt.dataTypeName.empty()) {
            implicitDataTemplates_[std::string(StripNamespacePrefix(dt.dataTypeName))] = dt.templatePackage;
        }
        for (const auto& kv : dt.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : dt.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : dt.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dt.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
    }
    for (const auto& ct : package.controlTemplates) {
        if (!ct.templatePackage) continue;
        for (const auto& kv : ct.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : ct.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : ct.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : ct.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        if (!ct.targetTypeName.empty() && ct.key.empty()) {
            implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
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
    VisitPackagePoolValues(package, true);
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
    for (const auto& st : package.styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : package.storyboards) {
        if (!sb.key.empty()) {
            storyboards_[sb.key] = sb;
        }
    }
    for (const auto& dt : package.dataTemplates) {
        if (!dt.templatePackage) continue;
        if (!dt.key.empty()) {
            dataTemplates_[dt.key] = dt.templatePackage;
        }
        if (!dt.dataTypeName.empty()) {
            implicitDataTemplates_[std::string(StripNamespacePrefix(dt.dataTypeName))] = dt.templatePackage;
        }
        for (const auto& kv : dt.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : dt.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : dt.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dt.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
    }
    for (const auto& ct : package.controlTemplates) {
        if (!ct.templatePackage) continue;
        for (const auto& kv : ct.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : ct.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : ct.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : ct.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        if (!ct.targetTypeName.empty() && ct.key.empty()) {
            implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
        }
    }
}

void AxbTemplateRegistry::RegisterStyle(AxbStyleDefinition style) {
    VisitStylePoolValues(style, true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!style.key.empty()) {
        styles_[style.key] = std::move(style);
    } else if (!style.targetTypeName.empty()) {
        const std::string shortType(StripNamespacePrefix(style.targetTypeName));
        implicitStyles_[shortType] = std::move(style);
    }
}

void AxbTemplateRegistry::RegisterStoryboard(AxbStoryboard storyboard) {
    if (!storyboard.key.empty()) {
        VisitStoryboardPoolValues(storyboard, true);
        std::lock_guard<std::mutex> lock(mutex_);
        storyboards_[storyboard.key] = std::move(storyboard);
    }
}

void AxbTemplateRegistry::RegisterDataTemplate(AxbDataTemplate dataTemplate) {
    if (!dataTemplate.templatePackage) {
        return;
    }
    VisitPackagePoolValues(*dataTemplate.templatePackage, true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dataTemplate.key.empty()) {
        dataTemplates_[dataTemplate.key] = dataTemplate.templatePackage;
    }
    if (!dataTemplate.dataTypeName.empty()) {
        implicitDataTemplates_[std::string(StripNamespacePrefix(dataTemplate.dataTypeName))] =
            dataTemplate.templatePackage;
    }
    for (const auto& kv : dataTemplate.templatePackage->resources) {
        if (!kv.first.empty()) resources_[kv.first] = kv.second;
    }
    for (const auto& mp : dataTemplate.templatePackage->mtsdfPaths) {
        if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
    }
    for (const auto& st : dataTemplate.templatePackage->styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : dataTemplate.templatePackage->storyboards) {
        if (!sb.key.empty()) storyboards_[sb.key] = sb;
    }
}

const AxbPackage* AxbTemplateRegistry::FindDataTemplate(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = dataTemplates_.find(std::string(key));
    if (it != dataTemplates_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

const AxbPackage* AxbTemplateRegistry::FindImplicitDataTemplate(
    std::string_view shortDataTypeName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string shortName(StripNamespacePrefix(shortDataTypeName));
    const auto it = implicitDataTemplates_.find(shortName);
    if (it != implicitDataTemplates_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

const AxbStyleDefinition* AxbTemplateRegistry::FindStyle(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = styles_.find(std::string(key));
    if (it != styles_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbStyleDefinition* AxbTemplateRegistry::FindImplicitStyle(
    std::string_view targetTypeName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string shortType(StripNamespacePrefix(targetTypeName));
    const auto it = implicitStyles_.find(shortType);
    if (it != implicitStyles_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbStoryboard* AxbTemplateRegistry::FindStoryboard(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = storyboards_.find(std::string(key));
    if (it != storyboards_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AxbStyleDefinition> AxbTemplateRegistry::RegisteredStyles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbStyleDefinition> out;
    out.reserve(styles_.size() + implicitStyles_.size());
    for (const auto& kv : styles_) out.push_back(kv.second);
    for (const auto& kv : implicitStyles_) out.push_back(kv.second);
    return out;
}

std::vector<AxbStoryboard> AxbTemplateRegistry::RegisteredStoryboards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbStoryboard> out;
    out.reserve(storyboards_.size());
    for (const auto& kv : storyboards_) out.push_back(kv.second);
    return out;
}

void AxbTemplateRegistry::RegisterControlTemplate(AxbControlTemplate controlTemplate) {
    if (!controlTemplate.templatePackage) {
        return;
    }
    VisitControlTemplatePoolValues(controlTemplate, true);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : controlTemplate.templatePackage->resources) {
        if (!kv.first.empty()) resources_[kv.first] = kv.second;
    }
    for (const auto& mp : controlTemplate.templatePackage->mtsdfPaths) {
        if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
    }
    for (const auto& st : controlTemplate.templatePackage->styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : controlTemplate.templatePackage->storyboards) {
        if (!sb.key.empty()) storyboards_[sb.key] = sb;
    }
    if (!controlTemplate.key.empty()) {
        controlTemplates_[controlTemplate.key] = std::move(controlTemplate);
    } else if (!controlTemplate.targetTypeName.empty()) {
        const std::string shortType(StripNamespacePrefix(controlTemplate.targetTypeName));
        implicitControlTemplates_[shortType] = std::move(controlTemplate);
    }
}

const AxbControlTemplate* AxbTemplateRegistry::FindControlTemplate(std::string_view key) const noexcept {
    if (key.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = controlTemplates_.find(std::string(key));
    if (it != controlTemplates_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbControlTemplate* AxbTemplateRegistry::FindImplicitControlTemplate(
    std::string_view targetTypeName) const noexcept {
    const std::string shortName(StripNamespacePrefix(targetTypeName));
    if (shortName.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = implicitControlTemplates_.find(shortName);
    if (it != implicitControlTemplates_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AxbControlTemplate> AxbTemplateRegistry::RegisteredControlTemplates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbControlTemplate> out;
    out.reserve(controlTemplates_.size() + implicitControlTemplates_.size());
    for (const auto& kv : controlTemplates_) out.push_back(kv.second);
    for (const auto& kv : implicitControlTemplates_) out.push_back(kv.second);
    return out;
}

void AxbTemplateRegistry::RegisterResource(std::string key, UnifiedValue value) {
    if (!key.empty()) {
        VisitPoolValue(value, true);
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

const UnifiedValue* AxbTemplateRegistry::FindScopedOverrideLocked(
    std::string_view key,
    const UIElement* target) const noexcept {
    if (!target || key.empty() || scopedDynamicOverrides_.empty()) {
        return nullptr;
    }
    const std::string k(key);
    for (const UIElement* cur = target; cur != nullptr; cur = NextResourceScopeAncestor(cur)) {
        const auto scopeIt = scopedDynamicOverrides_.find(cur);
        if (scopeIt != scopedDynamicOverrides_.end()) {
            const auto resIt = scopeIt->second.find(k);
            if (resIt != scopeIt->second.end()) {
                return &resIt->second;
            }
        }
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindResource(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string k(key);
    const auto dynIt = dynamicOverrides_.find(k);
    if (dynIt != dynamicOverrides_.end()) {
        return &dynIt->second;
    }
    const auto it = resources_.find(k);
    if (it != resources_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindDynamicResource(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = dynamicOverrides_.find(std::string(key));
    if (it != dynamicOverrides_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindResourceForElement(
    std::string_view key,
    const UIElement* target) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const UnifiedValue* scoped = FindScopedOverrideLocked(key, target)) {
        return scoped;
    }
    const std::string k(key);
    const auto dynIt = dynamicOverrides_.find(k);
    if (dynIt != dynamicOverrides_.end()) {
        return &dynIt->second;
    }
    const auto it = resources_.find(k);
    if (it != resources_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindDynamicResourceForElement(
    std::string_view key,
    const UIElement* target) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const UnifiedValue* scoped = FindScopedOverrideLocked(key, target)) {
        return scoped;
    }
    const auto it = dynamicOverrides_.find(std::string(key));
    if (it != dynamicOverrides_.end()) {
        return &it->second;
    }
    return nullptr;
}

namespace {

bool IsPotentialColorLiteralString(std::string_view raw) noexcept {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    if (raw.empty()) {
        return false;
    }
    if (raw.front() == '#') {
        return true;
    }
    if (raw.size() > 3u &&
        (raw[0] == 's' || raw[0] == 'S') &&
        (raw[1] == 'c' || raw[1] == 'C') &&
        raw[2] == '#') {
        return true;
    }
    // File paths / URIs contain '.', '/', '\', or ':'
    if (raw.find('.') != std::string_view::npos ||
        raw.find('/') != std::string_view::npos ||
        raw.find('\\') != std::string_view::npos ||
        raw.find(':') != std::string_view::npos) {
        return false;
    }
    return true;
}

std::string NormalizeDictLookupKey(std::string_view uri) {
    while (!uri.empty() && std::isspace(static_cast<unsigned char>(uri.front()))) {
        uri.remove_prefix(1);
    }
    while (!uri.empty() && std::isspace(static_cast<unsigned char>(uri.back()))) {
        uri.remove_suffix(1);
    }
    std::string norm;
    norm.reserve(uri.size());
    for (char ch : uri) {
        norm.push_back(ch == '\\' ? '/' : ch);
    }
    if (const auto semi = norm.find(";component/"); semi != std::string::npos) {
        norm = norm.substr(semi + 11u);
    } else if (norm.rfind("pack://application:,,,/", 0) == 0) {
        norm = norm.substr(23u);
    } else if (norm.rfind("pack://siteoforigin:,,,/", 0) == 0) {
        norm = norm.substr(24u);
    }
    while (norm.rfind("./", 0) == 0) {
        norm.erase(0, 2u);
    }
    return norm;
}

std::string ExtractBasenameKey(std::string_view normPath) {
    const auto slash = normPath.rfind('/');
    return std::string(slash == std::string_view::npos ? normPath : normPath.substr(slash + 1u));
}

} // namespace

void AxbTemplateRegistry::ApplyResolvedResourceToElement(
    UIElement* target,
    std::uint16_t propId,
    const UnifiedValue& rawVal) {
    if (!target || std::holds_alternative<std::monostate>(rawVal)) {
        return;
    }

    if (propId == PropId_PathData) {
        if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
            (void)AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(sv->view());
        }
        target->SetValue(propId, rawVal, ValueSourceRank::Local);
        target->InvalidateLayout();
        target->InvalidateRender();
        return;
    }

    if (propId == PropId_Background || propId == PropId_Foreground || propId == PropId_BorderBrush) {
        if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
            const std::string_view text = sv->view();
            if (IsPotentialColorLiteralString(text)) {
                target->SetValue(
                    propId,
                    value::FromColor(ParseXamlColorLiteral(text)),
                    ValueSourceRank::Local);
            } else {
                // ImageBrush / BitmapImage URI assigned to Background (e.g. Localization Flag ImageBrush)
                target->SetValue(propId, rawVal, ValueSourceRank::Local);
                if (propId == PropId_Background) {
                    target->SetValue(PropId_ImageSource, rawVal, ValueSourceRank::Local);
                }
            }
        } else {
            target->SetValue(propId, rawVal, ValueSourceRank::Local);
        }
        target->InvalidateRender();
        return;
    }

    if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
        switch (propId) {
            case PropId_Text:
            case PropId_Title:
            case PropId_Content:
            case PropId_Placeholder:
            case PropId_ImageSource:
            case PropId_FontFamily:
            case PropId_GroupName:
            case PropId_Style:
            case PropId_ItemTemplate:
            case PropId_MergedDictionarySource:
            case PropId_LocSource:
            case PropId_FlowDirection:
            case PropId_Tag:
                target->SetValue(propId, rawVal, ValueSourceRank::Local);
                break;
            default:
                target->SetValue(
                    propId,
                    ParseBuiltinLiteralPropertyValue(propId, sv->view()),
                    ValueSourceRank::Local);
                break;
        }
    } else {
        target->SetValue(propId, rawVal, ValueSourceRank::Local);
    }

    if (propId == PropId_Text || propId == PropId_Title || propId == PropId_Content ||
        propId == PropId_ImageSource || propId == PropId_FontSize ||
        propId == PropId_FontFamily || propId == PropId_FlowDirection ||
        propId == PropId_Width || propId == PropId_Height ||
        propId == PropId_Margin || propId == PropId_Padding ||
        propId == PropId_Visibility) {
        target->InvalidateLayout();
    }
    target->InvalidateRender();
}

bool AxbTemplateRegistry::IsSubscriptionAttachedLocked(const DynamicResourceSubscription& sub) noexcept {
    if (!sub.target) {
        return false;
    }
    if (sub.mountRoot != nullptr && sub.target != sub.mountRoot) {
        bool attachedToMountRoot = false;
        for (const UIElement* cur = NextResourceScopeAncestor(sub.target);
             cur != nullptr;
             cur = NextResourceScopeAncestor(cur)) {
            if (cur == sub.mountRoot) {
                attachedToMountRoot = true;
                break;
            }
        }
        if (!attachedToMountRoot) {
            return false;
        }
    } else if (sub.hadAncestorWhenBound &&
               sub.target->Parent() == nullptr &&
               !sub.target->IsConnectedToRoot()) {
        return false;
    }
    return true;
}

bool AxbTemplateRegistry::IsWithinScopeRoot(
    const UIElement* target,
    const UIElement* scopeRoot) noexcept {
    if (!scopeRoot) {
        return true;
    }
    for (const UIElement* cur = target; cur != nullptr; cur = NextResourceScopeAncestor(cur)) {
        if (cur == scopeRoot) {
            return true;
        }
    }
    return false;
}

void AxbTemplateRegistry::SnapshotWakeListenersLocked(std::vector<HostWakeEntry>& outListeners) const {
    outListeners.clear();
    outListeners.reserve(hostWakeListeners_.size() + (customWakeCallback_ ? 1u : 0u));
    for (const auto& entry : hostWakeListeners_) {
        if (entry.callback) {
            outListeners.push_back(entry);
        }
    }
    if (customWakeCallback_) {
        outListeners.push_back(HostWakeEntry{nullptr, customWakeCallback_, customWakeUserData_});
    }
}

void AxbTemplateRegistry::SubscribeDynamicResource(
    UIElement* target,
    UIElement* mountRoot,
    std::uint16_t propId,
    std::string_view resourceKey,
    std::uint8_t flags) {
    if (!target || resourceKey.empty() ||
        (flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) == 0u) {
        return;
    }
    const bool hadParent = (target->Parent() != nullptr) || target->IsConnectedToRoot();
    UnifiedValue immediateScopedValue{};
    bool hasImmediateScoped = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const UnifiedValue* scopedVal = FindScopedOverrideLocked(resourceKey, target)) {
            immediateScopedValue = *scopedVal;
            hasImmediateScoped = true;
        }
        bool updatedExisting = false;
        for (auto& existing : dynamicSubscriptions_) {
            if (existing.target == target && existing.propId == propId) {
                existing.mountRoot = mountRoot;
                existing.flags = flags;
                existing.hadAncestorWhenBound = hadParent;
                existing.resourceKey.assign(resourceKey);
                hasSubscriptions_.store(true, std::memory_order_release);
                updatedExisting = true;
                break;
            }
        }
        if (!updatedExisting) {
            DynamicResourceSubscription sub{};
            sub.target = target;
            sub.mountRoot = mountRoot;
            sub.propId = propId;
            sub.flags = flags;
            sub.hadAncestorWhenBound = hadParent;
            sub.resourceKey.assign(resourceKey);
            dynamicSubscriptions_.push_back(std::move(sub));
            hasSubscriptions_.store(true, std::memory_order_release);
        }
    }
    if (hasImmediateScoped) {
        ScopedUiResourceContext ownerScope(
            UiResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        ApplyResolvedResourceToElement(target, propId, immediateScopedValue);
    }
}

bool AxbTemplateRegistry::HasSubscriptionInSubtree(const UIElement* scopeRoot) const noexcept {
    if (!scopeRoot || !hasSubscriptions_.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (scopedDynamicOverrides_.find(scopeRoot) != scopedDynamicOverrides_.end()) {
        return true;
    }
    for (const auto& sub : dynamicSubscriptions_) {
        if (IsSubscriptionAttachedLocked(sub) && IsWithinScopeRoot(sub.target, scopeRoot)) {
            return true;
        }
    }
    return false;
}

void AxbTemplateRegistry::UnsubscribeElement(const UIElement* element) noexcept {
    if (!element || !hasSubscriptions_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    scopedDynamicOverrides_.erase(element);
    dynamicSubscriptions_.erase(
        std::remove_if(
            dynamicSubscriptions_.begin(),
            dynamicSubscriptions_.end(),
            [element](const DynamicResourceSubscription& sub) noexcept {
                return sub.target == element || sub.mountRoot == element;
            }),
        dynamicSubscriptions_.end());
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }
}

std::size_t AxbTemplateRegistry::PruneStaleDynamicSubscriptions() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = dynamicSubscriptions_.size();
    dynamicSubscriptions_.erase(
        std::remove_if(
            dynamicSubscriptions_.begin(),
            dynamicSubscriptions_.end(),
            [](const DynamicResourceSubscription& sub) noexcept {
                return !IsSubscriptionAttachedLocked(sub);
            }),
        dynamicSubscriptions_.end());
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }
    return before - dynamicSubscriptions_.size();
}

std::size_t AxbTemplateRegistry::ActiveDynamicSubscriptionCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t active = 0u;
    for (const auto& sub : dynamicSubscriptions_) {
        if (IsSubscriptionAttachedLocked(sub)) {
            ++active;
        }
    }
    return active;
}

std::size_t AxbTemplateRegistry::SetDynamicResource(
    std::string key,
    UnifiedValue value,
    UIElement* scopeRoot) {
    if (key.empty()) {
        return 0u;
    }
    VisitPoolValue(value, true);

    std::vector<PendingResourceUpdate> updates;
    std::vector<HostWakeEntry> wakeList;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scopeRoot == nullptr) {
            dynamicOverrides_[key] = value;
            resources_[key] = value;
        } else {
            scopedDynamicOverrides_[scopeRoot][key] = value;
            hasSubscriptions_.store(true, std::memory_order_release);
        }

        std::size_t writeIdx = 0u;
        for (std::size_t readIdx = 0u; readIdx < dynamicSubscriptions_.size(); ++readIdx) {
            DynamicResourceSubscription& sub = dynamicSubscriptions_[readIdx];
            if (!IsSubscriptionAttachedLocked(sub)) {
                continue;
            }
            if (sub.resourceKey == key &&
                IsWithinScopeRoot(sub.target, scopeRoot) &&
                (scopeRoot == nullptr ||
                 !IsShadowedByCloserScopeLocked(scopedDynamicOverrides_, sub.target, scopeRoot, key))) {
                updates.push_back(PendingResourceUpdate{
                    IntrusivePtr<UIElement>(sub.target, true),
                    sub.propId,
                    value});
            }
            if (writeIdx != readIdx) {
                dynamicSubscriptions_[writeIdx] = std::move(sub);
            }
            ++writeIdx;
        }
        dynamicSubscriptions_.resize(writeIdx);
        if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
            hasSubscriptions_.store(false, std::memory_order_release);
        }

        if (!updates.empty()) {
            SnapshotWakeListenersLocked(wakeList);
        }
    }

    {
        ScopedUiResourceContext ownerScope(
            UiResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        for (const auto& upd : updates) {
            ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
        }
    }
    for (const auto& wake : wakeList) {
        if (wake.callback) {
            wake.callback(wake.userData);
        }
    }
    return updates.size();
}

void AxbTemplateRegistry::CollectDynamicDictionaryUpdates(
    const AxbPackage& dictPkg,
    UIElement* scopeRoot,
    std::vector<PendingResourceUpdate>& outUpdates,
    std::vector<HostWakeEntry>& outWakeList) {
    VisitPackagePoolValues(dictPkg, true);

    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, UnifiedValue> incomingMap;
    incomingMap.reserve(dictPkg.resources.size());
    std::unordered_map<std::string, UnifiedValue>* scopedBucket =
        (scopeRoot != nullptr) ? &scopedDynamicOverrides_[scopeRoot] : nullptr;

    for (const auto& kv : dictPkg.resources) {
        if (!kv.first.empty()) {
            incomingMap[kv.first] = kv.second;
            if (scopeRoot == nullptr) {
                dynamicOverrides_[kv.first] = kv.second;
                resources_[kv.first] = kv.second;
            } else {
                (*scopedBucket)[kv.first] = kv.second;
            }
        }
    }
    if (scopedBucket != nullptr && !scopedBucket->empty()) {
        hasSubscriptions_.store(true, std::memory_order_release);
    }

    for (const auto& mp : dictPkg.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
    if (scopeRoot == nullptr) {
        for (const auto& st : dictPkg.styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dictPkg.storyboards) {
            if (!sb.key.empty()) {
                storyboards_[sb.key] = sb;
            }
        }
        for (const auto& dt : dictPkg.dataTemplates) {
            if (!dt.templatePackage) continue;
            if (!dt.key.empty()) {
                dataTemplates_[dt.key] = dt.templatePackage;
            }
            if (!dt.dataTypeName.empty()) {
                implicitDataTemplates_[std::string(StripNamespacePrefix(dt.dataTypeName))] = dt.templatePackage;
            }
            for (const auto& kv : dt.templatePackage->resources) {
                if (!kv.first.empty()) resources_[kv.first] = kv.second;
            }
            for (const auto& mp : dt.templatePackage->mtsdfPaths) {
                if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
            }
            for (const auto& st : dt.templatePackage->styles) {
                if (!st.key.empty()) {
                    styles_[st.key] = st;
                } else if (!st.targetTypeName.empty()) {
                    implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
                }
            }
            for (const auto& sb : dt.templatePackage->storyboards) {
                if (!sb.key.empty()) storyboards_[sb.key] = sb;
            }
        }
        for (const auto& ct : dictPkg.controlTemplates) {
            if (!ct.templatePackage) continue;
            for (const auto& kv : ct.templatePackage->resources) {
                if (!kv.first.empty()) resources_[kv.first] = kv.second;
            }
            for (const auto& mp : ct.templatePackage->mtsdfPaths) {
                if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
            }
            for (const auto& st : ct.templatePackage->styles) {
                if (!st.key.empty()) {
                    styles_[st.key] = st;
                } else if (!st.targetTypeName.empty()) {
                    implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
                }
            }
            for (const auto& sb : ct.templatePackage->storyboards) {
                if (!sb.key.empty()) storyboards_[sb.key] = sb;
            }
            if (!ct.key.empty()) {
                controlTemplates_[ct.key] = ct;
            } else if (!ct.targetTypeName.empty()) {
                implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
            }
        }
    }

    std::size_t writeIdx = 0u;
    for (std::size_t readIdx = 0u; readIdx < dynamicSubscriptions_.size(); ++readIdx) {
        DynamicResourceSubscription& sub = dynamicSubscriptions_[readIdx];
        if (!IsSubscriptionAttachedLocked(sub)) {
            continue;
        }
        if (IsWithinScopeRoot(sub.target, scopeRoot) &&
            (scopeRoot == nullptr ||
             !IsShadowedByCloserScopeLocked(
                 scopedDynamicOverrides_, sub.target, scopeRoot, sub.resourceKey))) {
            const auto it = incomingMap.find(sub.resourceKey);
            if (it != incomingMap.end()) {
                outUpdates.push_back(PendingResourceUpdate{
                    IntrusivePtr<UIElement>(sub.target, true),
                    sub.propId,
                    it->second});
            }
        }
        if (writeIdx != readIdx) {
            dynamicSubscriptions_[writeIdx] = std::move(sub);
        }
        ++writeIdx;
    }
    dynamicSubscriptions_.resize(writeIdx);
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }

    if (!outUpdates.empty() ||
        (scopeRoot == nullptr && (!dictPkg.styles.empty() || !dictPkg.controlTemplates.empty()))) {
        SnapshotWakeListenersLocked(outWakeList);
    }
}

std::size_t AxbTemplateRegistry::ApplyDynamicDictionary(
    const AxbPackage& dictPkg,
    UIElement* scopeRoot) {
    std::vector<PendingResourceUpdate> updates;
    std::vector<HostWakeEntry> wakeList;
    CollectDynamicDictionaryUpdates(dictPkg, scopeRoot, updates, wakeList);

    {
        ScopedUiResourceContext ownerScope(
            UiResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        for (const auto& upd : updates) {
            ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
        }
    }
    for (const auto& wake : wakeList) {
        if (wake.callback) {
            wake.callback(wake.userData);
        }
    }
    return updates.size();
}

void AxbTemplateRegistry::RegisterDictionary(std::string sourceUri, AxbPackage dictPkg) {
    if (sourceUri.empty()) {
        return;
    }
    VisitPackagePoolValues(dictPkg, true);
    const std::string norm = NormalizeDictLookupKey(sourceUri);
    const std::string base = ExtractBasenameKey(norm);
    std::lock_guard<std::mutex> lock(mutex_);
    registeredDictionaries_[sourceUri] = dictPkg;
    if (!norm.empty() && norm != sourceUri) {
        registeredDictionaries_[norm] = dictPkg;
    }
    if (!base.empty() && base != norm && base != sourceUri) {
        registeredDictionaries_[base] = std::move(dictPkg);
    }
}

const AxbPackage* AxbTemplateRegistry::FindDictionary(std::string_view sourceUri) const noexcept {
    if (sourceUri.empty()) {
        return nullptr;
    }
    const std::string raw(sourceUri);
    const std::string norm = NormalizeDictLookupKey(sourceUri);
    const std::string base = ExtractBasenameKey(norm);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registeredDictionaries_.find(raw);
    if (it != registeredDictionaries_.end()) {
        return &it->second;
    }
    if (!norm.empty()) {
        it = registeredDictionaries_.find(norm);
        if (it != registeredDictionaries_.end()) {
            return &it->second;
        }
    }
    if (!base.empty()) {
        it = registeredDictionaries_.find(base);
        if (it != registeredDictionaries_.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::size_t AxbTemplateRegistry::ApplyRegisteredDictionary(
    std::string_view sourceUri,
    UIElement* scopeRoot) {
    AxbPackage dictCopy{};
    bool found = false;
    {
        const std::string raw(sourceUri);
        const std::string norm = NormalizeDictLookupKey(sourceUri);
        const std::string base = ExtractBasenameKey(norm);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registeredDictionaries_.find(raw);
        if (it == registeredDictionaries_.end() && !norm.empty()) {
            it = registeredDictionaries_.find(norm);
        }
        if (it == registeredDictionaries_.end() && !base.empty()) {
            it = registeredDictionaries_.find(base);
        }
        if (it != registeredDictionaries_.end()) {
            dictCopy = it->second;
            found = true;
        }
    }
    if (!found) {
        return 0u;
    }
    return ApplyDynamicDictionary(dictCopy, scopeRoot);
}

void AxbTemplateRegistry::SetWakeHostCallback(
    WakeHostCallback callback,
    void* userData) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    customWakeCallback_ = callback;
    customWakeUserData_ = userData;
}

void AxbTemplateRegistry::RegisterHostWakeListener(
    const void* ownerKey,
    WakeHostCallback callback,
    void* userData) {
    if (!ownerKey || !callback) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : hostWakeListeners_) {
        if (entry.ownerKey == ownerKey) {
            entry.callback = callback;
            entry.userData = userData;
            return;
        }
    }
    hostWakeListeners_.push_back(HostWakeEntry{ownerKey, callback, userData});
}

void AxbTemplateRegistry::UnregisterHostWakeListener(const void* ownerKey) noexcept {
    if (!ownerKey) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    hostWakeListeners_.erase(
        std::remove_if(
            hostWakeListeners_.begin(),
            hostWakeListeners_.end(),
            [ownerKey](const HostWakeEntry& entry) noexcept {
                return entry.ownerKey == ownerKey;
            }),
        hostWakeListeners_.end());
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
    const std::uint16_t adaptiveCell = SelectAdaptiveMtsdfCellSize(pathData);
    AxbMtsdfPathEntry baked = BakeSvgPathToMtsdfEntry(pathData, adaptiveCell, 4.0f, 0u, 1024u);
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
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine)
    : controlClassName_(className) {
    EnsureControlTypesRegistered();
    const std::shared_ptr<UiResourceContext> domainCtx =
        (UiResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UiResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUiResourceContext domainScope(domainCtx);
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext,
            animationEngine);
    }
}

bool UserControl::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    const std::shared_ptr<UiResourceContext> domainCtx =
        (UiResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UiResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUiResourceContext domainScope(domainCtx);
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
    return templateMounted_;
}

Window::Window(
    std::string_view className,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine)
    : windowClassName_(className) {
    EnsureControlTypesRegistered();
    const std::shared_ptr<UiResourceContext> domainCtx =
        (UiResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UiResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUiResourceContext domainScope(domainCtx);
    if (const AxbPackage* pkg = AxbTemplateRegistry::Instance().Find(className)) {
        templateMounted_ = MountTemplate(
            *pkg,
            domains,
            bindingEngine,
            dataContext,
            eventInterpreter,
            eventAotContext,
            animationEngine);
    }
}

bool Window::MountTemplate(
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    const std::shared_ptr<UiResourceContext> domainCtx =
        (UiResourceContext::ActiveOverride() == nullptr && domains != nullptr)
            ? UiResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUiResourceContext domainScope(domainCtx);
    templateMounted_ = MountAxbTemplate(
        this,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
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
    if (shortName == "HeaderedContentControl") return MakeIntrusive<HeaderedContentControl>();
    if (shortName == "GroupBox") return MakeIntrusive<GroupBox>();
    if (shortName == "Expander") return MakeIntrusive<Expander>();
    if (shortName == "TabControl") return MakeIntrusive<TabControl>();
    if (shortName == "TabItem") return MakeIntrusive<TabItem>();
    if (shortName == "UniformGrid") return MakeIntrusive<UniformGrid>();
    if (shortName == "ProgressBar") return MakeIntrusive<ProgressBar>();
    if (shortName == "Rectangle") return MakeIntrusive<Rectangle>();
    if (shortName == "Ellipse") return MakeIntrusive<Ellipse>();
    if (shortName == "Line") return MakeIntrusive<Line>();
    if (shortName == "Polygon") return MakeIntrusive<Polygon>();
    if (shortName == "Polyline") return MakeIntrusive<Polyline>();
    if (shortName == "StackPanel" || shortName == "VirtualizingStackPanel" ||
        shortName == "ToolBar" || shortName == "StatusBar" ||
        shortName == "Menu" || shortName == "ContextMenu") {
        return MakeIntrusive<StackPanel>();
    }
    if (shortName == "Grid") {
        return MakeIntrusive<Grid>();
    }
    if (shortName == "Canvas") return MakeIntrusive<Canvas>();
    if (shortName == "Path") return MakeIntrusive<Path>();
    if (shortName == "Image") return MakeIntrusive<Image>();
    if (shortName == "CheckBox" || shortName == "ToggleButton") {
        return MakeIntrusive<CheckBox>();
    }
    if (shortName == "RadioButton") {
        return MakeIntrusive<RadioButton>();
    }
    if (shortName == "Slider") {
        return MakeIntrusive<Slider>();
    }
    if (shortName == "PasswordBox") {
        return MakeIntrusive<PasswordBox>();
    }
    if (shortName == "TextBox" || shortName == "RichTextBox") {
        return MakeIntrusive<TextBox>();
    }
    if (shortName == "ComboBox") {
        return MakeIntrusive<ComboBox>();
    }
    if (shortName == "ContentPresenter") {
        return MakeIntrusive<ContentPresenter>();
    }
    if (shortName == "RiveControl") {
        return MakeIntrusive<RiveControl>();
    }
    if (shortName == "Border" ||
        shortName == "ContentControl" ||
        shortName == "BoxShadow" ||
        shortName == "ComboBoxItem" || shortName == "ListBoxItem" ||
        shortName == "ListViewItem" || shortName == "TreeViewItem") {
        return MakeIntrusive<Border>();
    }
    if (shortName == "TextBlock" || shortName == "Label") {
        return MakeIntrusive<TextBlock>();
    }
    if (shortName == "Button" || shortName == "RepeatButton" ||
        shortName == "Hyperlink" || shortName == "MenuItem") {
        return MakeIntrusive<Button>();
    }
    if (shortName == "ListBox" || shortName == "ListView") {
        return MakeIntrusive<ListBox>();
    }
    if (shortName == "ItemsControl" ||
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

namespace {

const AxbStyleDefinition* LookupStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept {
    if (key.empty()) {
        return nullptr;
    }
    for (auto it = package.styles.rbegin(); it != package.styles.rend(); ++it) {
        if (it->key == key) {
            return &(*it);
        }
    }
    return AxbTemplateRegistry::Instance().FindStyle(key);
}

const AxbStyleDefinition* LookupImplicitStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view shortTypeName) noexcept {
    if (shortTypeName.empty()) {
        return nullptr;
    }
    for (auto it = package.styles.rbegin(); it != package.styles.rend(); ++it) {
        if (it->key.empty() && StripNamespacePrefix(it->targetTypeName) == shortTypeName) {
            return &(*it);
        }
    }
    return AxbTemplateRegistry::Instance().FindImplicitStyle(shortTypeName);
}

const AxbControlTemplate* LookupControlTemplateInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept {
    if (const AxbControlTemplate* local = package.FindControlTemplate(key)) {
        return local;
    }
    return AxbTemplateRegistry::Instance().FindControlTemplate(key);
}

void AutoApplyControlStyleTemplate(
    UIElement* elem,
    std::string_view declaredTypeName,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    if (!elem) {
        return;
    }

    // 1. Explicit PropId_Template already set on element
    const std::string explicitTplKey = elem->GetControlTemplateKey();
    if (!explicitTplKey.empty() && !elem->HasControlTemplate()) {
        if (const AxbControlTemplate* ct = LookupControlTemplateInPackageOrRegistry(package, explicitTplKey)) {
            (void)elem->ApplyControlTemplate(
                *ct, domains, bindingEngine, dataContext, eventInterpreter, eventAotContext, animationEngine);
        }
    }

    // 2. Explicit Style="{StaticResource ...}" or implicit <Style TargetType="..."> carrying a ControlTemplate
    const std::string_view shortType = !declaredTypeName.empty()
        ? StripNamespacePrefix(declaredTypeName)
        : (elem->GetTypeClass()->typeName
               ? StripNamespacePrefix(elem->GetTypeClass()->typeName)
               : std::string_view{});

    const std::string explicitStyleKey = elem->GetStyleKey();
    const AxbStyleDefinition* leafStyle = !explicitStyleKey.empty()
        ? LookupStyleInPackageOrRegistry(package, explicitStyleKey)
        : LookupImplicitStyleInPackageOrRegistry(package, shortType);

    if (leafStyle) {
        // Walk BasedOn inheritance chain (root-to-leaf) to see if this Style defines a ControlTemplate
        std::vector<const AxbStyleDefinition*> chain;
        chain.push_back(leafStyle);
        std::string curBaseKey = leafStyle->basedOnKey;
        for (int depth = 0; depth < 8 && !curBaseKey.empty(); ++depth) {
            const AxbStyleDefinition* base = LookupStyleInPackageOrRegistry(package, curBaseKey);
            if (!base || std::find(chain.begin(), chain.end(), base) != chain.end()) {
                break;
            }
            chain.push_back(base);
            curBaseKey = base->basedOnKey;
        }

        std::string resolvedTplKey;
        std::shared_ptr<AxbControlTemplate> resolvedInlineCt;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const AxbStyleDefinition* s = *it;
            if (!s->controlTemplateKey.empty()) {
                resolvedTplKey = s->controlTemplateKey;
            }
            if (s->inlineControlTemplate) {
                resolvedInlineCt = s->inlineControlTemplate;
            }
            for (const auto& setter : s->setters) {
                if (setter.propId == PropId_Template && setter.targetName.empty()) {
                    if (const auto* sv = std::get_if<StringValue>(&setter.value)) {
                        resolvedTplKey = sv->str();
                    }
                }
            }
        }

        if (!resolvedTplKey.empty() || resolvedInlineCt) {
            // Mount the ControlTemplate first if no explicit Local Template overrode it
            if (!elem->HasControlTemplate()) {
                const AxbControlTemplate* ctToMount = !resolvedTplKey.empty()
                    ? LookupControlTemplateInPackageOrRegistry(package, resolvedTplKey)
                    : nullptr;
                if (!ctToMount && resolvedInlineCt) {
                    ctToMount = resolvedInlineCt.get();
                }
                if (ctToMount) {
                    (void)elem->ApplyControlTemplate(
                        *ctToMount,
                        domains,
                        bindingEngine,
                        dataContext,
                        eventInterpreter,
                        eventAotContext,
                        animationEngine);
                }
            }

            // Apply base theme setters (Background, Foreground, FontSize, FontFamily, Padding, Margin, etc.)
            // at ValueSourceRank::ThemeStyle so TemplateBindings & ContentPresenters receive style defaults!
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                for (const auto& setter : (*it)->setters) {
                    if (setter.propId == PropId_Template) {
                        continue;
                    }
                    UIElement* target = setter.targetName.empty()
                        ? elem
                        : elem->FindName(setter.targetName);
                    if (!target) {
                        continue;
                    }
                    if (IsFastInlinePropertyId(setter.propId)) {
                        if (target->GetPropertySourceRank(setter.propId) == ValueSourceRank::Default) {
                            (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
                        }
                    } else {
                        (void)target->SetValue(setter.propId, setter.value, ValueSourceRank::ThemeStyle);
                    }
                }
            }
            return;
        }
    }

    // 3. Implicit ControlTemplate by TargetType (if registered without x:Key)
    if (!elem->HasControlTemplate() && !shortType.empty()) {
        if (const AxbControlTemplate* impCt =
                AxbTemplateRegistry::Instance().FindImplicitControlTemplate(shortType)) {
            (void)elem->ApplyControlTemplate(
                *impCt, domains, bindingEngine, dataContext, eventInterpreter, eventAotContext, animationEngine);
        }
    }
}

bool MountAxbInstructionsInternal(
    UIElement* rootControl,
    UIElement* templatedHost,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    if (!rootControl) {
        return false;
    }
    const std::shared_ptr<UiResourceContext> domainCtx =
        (!UiResourceContext::HasActiveThreadOverride() && domains != nullptr)
            ? UiResourceContext::FindForDomains(domains)
            : nullptr;
    ScopedUiResourceContext domainScope(domainCtx);

    if (!animationEngine && bindingEngine) {
        animationEngine = bindingEngine->GetAnimationEngine();
    }

    if (!package.resources.empty()) {
        for (const auto& kv : package.resources) {
            if (!kv.first.empty()) {
                AxbTemplateRegistry::Instance().RegisterResource(kv.first, kv.second);
            }
        }
    }
    if (!package.mtsdfPaths.empty()) {
        for (const auto& mp : package.mtsdfPaths) {
            AxbTemplateRegistry::Instance().RegisterMtsdfPath(mp);
        }
    }
    for (const auto& ct : package.controlTemplates) {
        AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);
    }
    for (const auto& st : package.styles) {
        AxbTemplateRegistry::Instance().RegisterStyle(st);
    }
    for (const auto& sb : package.storyboards) {
        AxbTemplateRegistry::Instance().RegisterStoryboard(sb);
    }
    for (const auto& dt : package.dataTemplates) {
        AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
    }

    NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;
    const ObjectRef effectiveContext = dataContext.valid()
        ? dataContext
        : (templatedHost && templatedHost->BoundScriptWrapper().valid()
               ? templatedHost->BoundScriptWrapper()
               : rootControl->BoundScriptWrapper());

    std::vector<UIElement*> elementStack;
    std::vector<UIElement*> localElements;
    std::vector<std::string_view> localDeclaredTypes;
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
                    if (templatedHost) {
                        currentElem->SetTemplatedParent(templatedHost);
                    } else {
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
                            eventAotContext,
                            animationEngine);
                    } else {
                        createdChild = CreateElementByTypeName(typeName);
                    }
                    if (!elementStack.empty() && elementStack.back() && createdChild) {
                        if (templatedHost) {
                            createdChild->SetTemplatedParent(templatedHost);
                        }
                        elementStack.back()->AddChild(createdChild.Get(), registry, uiHeap);
                        currentElem = createdChild.Get();
                        if ((inst.flags & 0x01u) != 0u) {
                            if (auto* hcc = elementStack.back()->DynamicCast<HeaderedContentControl>()) {
                                hcc->SetHeaderElement(currentElem);
                            }
                        }
                    }
                }

                if (currentElem && bindingEngine) {
                    currentElem->BindTwoWayEngine(bindingEngine);
                }
                if (currentElem && animationEngine) {
                    currentElem->BindAnimationEngine(animationEngine);
                }

                if (auto* ic = currentElem ? currentElem->DynamicCast<ItemsControl>() : nullptr) {
                    ic->SetDomains(domains);
                    ic->SetBindingEngine(bindingEngine);
                    ic->SetAnimationEngine(animationEngine);
                } else if (auto* tc = currentElem ? currentElem->DynamicCast<TabControl>() : nullptr) {
                    tc->SetDomains(domains);
                    tc->SetBindingEngine(bindingEngine);
                    tc->SetAnimationEngine(animationEngine);
                }
                if (auto* rc = currentElem ? currentElem->DynamicCast<RiveControl>() : nullptr) {
                    rc->SetAnimationEngine(animationEngine);
                }
                if (templatedHost && currentElem) {
                    if (auto* cp = currentElem->DynamicCast<ContentPresenter>()) {
                        templatedHost->RegisterTemplateContentPresenter(cp);
                    }
                }

                if (currentElem && !elemName.empty()) {
                    currentElem->SetName(std::string(elemName));
                    if (templatedHost) {
                        templatedHost->RegisterTemplateNamedPart(elemName, currentElem);
                    } else {
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
                }

                elementStack.push_back(currentElem);
                localElements.push_back(currentElem);
                localDeclaredTypes.push_back(typeName);
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
                if (!elementStack.empty() && elementStack.back() && bindingEngine &&
                    inst.strIdx0 < package.bindingChains.size()) {
                    const auto& chain = package.bindingChains[inst.strIdx0];
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    if (!chain.elementName.empty() ||
                        !chain.relativeAncestorType.empty() ||
                        chain.relativeSelf) {
                        (void)bindingEngine->BindElementProperty(chain, targetPtr);
                    } else if (effectiveContext.valid()) {
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

            case AxbOpcode::BindMultiProperty: {
                if (!elementStack.empty() && elementStack.back() && bindingEngine &&
                    inst.strIdx0 < package.multiBindings.size()) {
                    const auto& mb = package.multiBindings[inst.strIdx0];
                    IntrusivePtr<UIElement> targetPtr(elementStack.back(), true);
                    (void)bindingEngine->BindMultiChain(
                        effectiveContext,
                        mb,
                        targetPtr);
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
                    UIElement* elem = elementStack.back();
                    const ObjectRef targetObj = rootControl->BoundScriptWrapper().valid()
                        ? rootControl->BoundScriptWrapper()
                        : effectiveContext;
                    const SymbolId methodId = inst.symbolId;
                    const bool isSimCommand = (inst.flags & 0x01u) != 0u;
                    if (targetObj.valid() && methodId != 0) {
                        auto invokeBoundHandler = [eventInterpreter, eventAotContext, domains, targetObj, methodId, isSimCommand](
                                                      const UnifiedValue& argVal,
                                                      bool hasArg) {
                            if (isSimCommand && domains) {
                                std::vector<UnifiedValue> payload;
                                if (hasArg) {
                                    payload.push_back(argVal);
                                }
                                domains->enqueueSimCommand(SimCommandEnvelope{
                                    methodId,
                                    targetObj,
                                    std::move(payload),
                                    0ULL});
                                return;
                            }
                            if (eventInterpreter) {
                                std::vector<UnifiedValue> args{targetObj};
                                if (hasArg) {
                                    args.push_back(argVal);
                                }
                                auto res = eventInterpreter->invoke(methodId, args);
                                if (!res.succeeded &&
                                    res.error.code == ErrorCode::InvalidArguments &&
                                    res.statistics.instructionsExecuted == 0u &&
                                    args.size() > 1u) {
                                    args.resize(1u);
                                    (void)eventInterpreter->invoke(methodId, args);
                                }
                            } else if (eventAotContext) {
                                std::vector<UnifiedValue> args{targetObj};
                                if (hasArg) {
                                    args.push_back(argVal);
                                }
                                UnifiedValue ret{};
                                if (!eventAotContext->invoke(
                                        methodId, args.data(), args.size(), ret) &&
                                    args.size() > 1u) {
                                    (void)eventAotContext->invoke(
                                        methodId, args.data(), 1u, ret);
                                }
                            }
                        };

                        if (auto* slider = elem->DynamicCast<Slider>()) {
                            slider->SetValueChangedCallback(
                                [invokeBoundHandler](Slider&, float val) {
                                    invokeBoundHandler(MakeFloat(val), true);
                                });
                        } else if (auto* tb = elem->DynamicCast<TextBox>()) {
                            tb->SetTextChangedCallback(
                                [invokeBoundHandler](TextBox&, const std::string& text) {
                                    invokeBoundHandler(value::FromString(text), true);
                                });
                        } else if (auto* combo = elem->DynamicCast<ComboBox>()) {
                            combo->SetSelectionChangedCallback(
                                [invokeBoundHandler](ComboBox&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* listBox = elem->DynamicCast<ListBox>()) {
                            listBox->SetSelectionChangedCallback(
                                [invokeBoundHandler](ListBox&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* tabCtrl = elem->DynamicCast<TabControl>()) {
                            tabCtrl->SetSelectionChangedCallback(
                                [invokeBoundHandler](TabControl&, std::int32_t idx) {
                                    invokeBoundHandler(value::FromInt(idx), true);
                                });
                        } else if (auto* expCtrl = elem->DynamicCast<Expander>()) {
                            expCtrl->SetExpandedChangedCallback(
                                [invokeBoundHandler](Expander&, bool expanded) {
                                    invokeBoundHandler(value::FromBool(expanded), true);
                                });
                        } else if (auto* btn = elem->DynamicCast<Button>()) {
                            if (isSimCommand && domains) {
                                btn->SetClickCallback(
                                    [domains, targetObj, methodId](Button& b) {
                                        std::vector<UnifiedValue> payload;
                                        const UnifiedValue cmdParam = b.GetValue(PropId_CommandParameter);
                                        if (!std::holds_alternative<std::monostate>(cmdParam)) {
                                            payload.push_back(cmdParam);
                                        }
                                        domains->enqueueSimCommand(SimCommandEnvelope{
                                            methodId,
                                            targetObj,
                                            std::move(payload),
                                            0ULL});
                                    });
                            } else {
                                if (eventInterpreter) {
                                    btn->BindScriptClickHandler(
                                        eventInterpreter,
                                        targetObj,
                                        methodId);
                                } else if (eventAotContext) {
                                    btn->BindAotClickHandler(
                                        eventAotContext,
                                        targetObj,
                                        methodId);
                                }
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
                    UIElement* targetElem = elementStack.back();
                    const std::string_view key = package.GetString(inst.strIdx0);
                    const bool isDynamicOrLoc =
                        (inst.flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) != 0u;

                    const UnifiedValue* resolved = nullptr;
                    if (isDynamicOrLoc) {
                        resolved = AxbTemplateRegistry::Instance().FindDynamicResourceForElement(
                            key,
                            targetElem);
                    }
                    if (!resolved) {
                        for (auto it = package.resources.rbegin(); it != package.resources.rend(); ++it) {
                            if (it->first == key) {
                                resolved = &it->second;
                                break;
                            }
                        }
                    }
                    if (!resolved) {
                        resolved = AxbTemplateRegistry::Instance().FindResourceForElement(
                            key,
                            targetElem);
                    }

                    if (resolved) {
                        AxbTemplateRegistry::ApplyResolvedResourceToElement(
                            targetElem, inst.propId, *resolved);
                    } else if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        AxbTemplateRegistry::ApplyResolvedResourceToElement(
                            targetElem, inst.propId, inst.literalValue);
                    }

                    if (isDynamicOrLoc) {
                        AxbTemplateRegistry::Instance().SubscribeDynamicResource(
                            targetElem,
                            rootControl,
                            inst.propId,
                            key,
                            inst.flags);
                    }
                }
                break;
            }

            case AxbOpcode::BindTemplateParent: {
                if (!elementStack.empty() && elementStack.back()) {
                    UIElement* targetElem = elementStack.back();
                    UIElement* host = templatedHost ? templatedHost : targetElem->TemplatedParent();
                    if (host) {
                        host->AddTemplateBinding(
                            targetElem,
                            static_cast<std::uint16_t>(inst.strIdx0),
                            inst.propId,
                            static_cast<ValueConverterKind>(inst.flags));
                    }
                }
                break;
            }
        }
    }

    // Automatically resolve & mount ControlTemplates (via Template attribute, explicit Style, or implicit TargetType Style)
    const std::size_t startIdx = templatedHost ? 1u : 0u;
    for (std::size_t i = startIdx; i < localElements.size(); ++i) {
        AutoApplyControlStyleTemplate(
            localElements[i],
            localDeclaredTypes[i],
            package,
            domains,
            bindingEngine,
            effectiveContext,
            eventInterpreter,
            eventAotContext,
            animationEngine);
    }

    if (rootVisited && animationEngine && package.HasMotionSection()) {
        animationEngine->InstantiatePackageMotion(
            rootControl,
            localElements.data(),
            localElements.size(),
            package);
    }

    return rootVisited;
}

} // namespace

bool MountAxbTemplate(
    UIElement* rootControl,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return MountAxbInstructionsInternal(
        rootControl,
        nullptr,
        package,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
}

bool MountControlTemplatePackage(
    UIElement* hostControl,
    UIElement* templateRootControl,
    const AxbPackage& templatePackage,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    return MountAxbInstructionsInternal(
        templateRootControl,
        hostControl,
        templatePackage,
        domains,
        bindingEngine,
        dataContext,
        eventInterpreter,
        eventAotContext,
        animationEngine);
}


namespace {



[[nodiscard]] bool ReadBinaryFileBytes(const std::string& path, std::vector<std::uint8_t>& outBytes) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return false;
    }
    const std::streamsize len = ifs.tellg();
    if (len <= 0) {
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(len));
    ifs.seekg(0, std::ios::beg);
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(outBytes.data()), len));
}

[[nodiscard]] bool ResolveAndReadRiveAsset(
    std::string_view rawUri,
    std::vector<std::uint8_t>& outBytes) {
    std::string uri(rawUri);
    while (!uri.empty() && (uri.front() == ' ' || uri.front() == '\t')) uri.erase(uri.begin());
    while (!uri.empty() && (uri.back() == ' ' || uri.back() == '\t')) uri.pop_back();
    if (uri.empty()) {
        return false;
    }
    for (char& ch : uri) {
        if (ch == '\\') ch = '/';
    }
    if (uri.rfind("pack://", 0) == 0) {
        const std::size_t semi = uri.find(";component/");
        if (semi != std::string::npos) {
            uri = uri.substr(semi + 11u);
        }
    }
    if (!uri.empty() && uri.front() == '/') {
        if (ReadBinaryFileBytes(uri, outBytes)) {
            return true;
        }
        uri.erase(uri.begin());
        if (!uri.empty() && ReadBinaryFileBytes(uri, outBytes)) {
            return true;
        }
    } else if (ReadBinaryFileBytes(uri, outBytes)) {
        return true;
    }

    {
        const auto dirs = UiResourceContext::Current().RiveSearchDirectoriesSnapshot();
        for (const auto& dir : dirs) {
            if (dir.empty()) continue;
            std::string full = dir;
            if (full.back() != '/') full.push_back('/');
            full += uri;
            if (ReadBinaryFileBytes(full, outBytes)) {
                return true;
            }
        }
    }

    static constexpr std::string_view kFallbackPrefixes[] = {
        "Data/",
        "Samples/Rive/Data/",
        "../Samples/Rive/Data/",
    };
    for (std::string_view pfx : kFallbackPrefixes) {
        std::string candidate(pfx);
        candidate += uri;
        if (ReadBinaryFileBytes(candidate, outBytes)) {
            return true;
        }
    }
    return false;
}

} // namespace

RiveControl::RiveControl() {
    EnsureControlTypesRegistered();
}

RiveControl::RiveControl(
    std::string_view sourceUri,
    AnimationEngine* animationEngine)
    : externalAnimationEngine_(animationEngine) {
    EnsureControlTypesRegistered();
    if (!sourceUri.empty()) {
        SetSource(sourceUri);
    }
}

RiveControl::~RiveControl() {
    if (ownedAnimationEngine_ && artboardRoot_) {
        (void)ownedAnimationEngine_->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }
}

void RiveControl::AddAssetSearchDirectory(std::string_view directory) {
    UiResourceContext::Current().AddRiveSearchDirectory(directory);
}

void RiveControl::ClearAssetSearchDirectories() {
    UiResourceContext::Current().ClearRiveSearchDirectories();
}

void RiveControl::SetSource(std::string_view sourceUri) {
    (void)SetValue(PropId_RiveSource, value::FromString(std::string(sourceUri)), ValueSourceRank::Local);
}

std::string RiveControl::GetSource() const {
    const UnifiedValue rv = GetValue(PropId_RiveSource);
    if (const auto* sv = std::get_if<StringValue>(&rv)) {
        if (!sv->view().empty()) {
            return sv->str();
        }
    }
    const UnifiedValue iv = GetValue(PropId_ImageSource);
    if (const auto* sv = std::get_if<StringValue>(&iv)) {
        return sv->str();
    }
    return {};
}

void RiveControl::SetStretch(Stretch stretch) {
    (void)SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch RiveControl::GetStretch() const noexcept {
    const auto v = GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

void RiveControl::SetStretchDirection(StretchDirection dir) {
    (void)SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection RiveControl::GetStretchDirection() const noexcept {
    const auto v = GetValue(PropId_StretchDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 2) {
            return static_cast<StretchDirection>(*i);
        }
    }
    return StretchDirection::Both;
}

void RiveControl::SetAnimationEngine(AnimationEngine* engine) noexcept {
    if (externalAnimationEngine_ == engine) {
        return;
    }
    if (externalAnimationEngine_ && artboardRoot_) {
        (void)externalAnimationEngine_->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }
    externalAnimationEngine_ = engine;
    if (mountedPackage_ && artboardRoot_) {
        std::shared_ptr<AxbPackage> pkgCopy = mountedPackage_;
        (void)MountDecodedRivePackage(std::move(pkgCopy));
    }
}

AnimationEngine* RiveControl::ActiveAnimationEngine() const noexcept {
    return externalAnimationEngine_ ? externalAnimationEngine_ : ownedAnimationEngine_.get();
}

bool RiveControl::LoadFromMemory(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    std::string_view artboardNameHint) {
    if (!rawBytes || byteLength == 0u) {
        return false;
    }
    auto pkg = std::make_shared<AxbPackage>();
    if (!DecodeRiveBinaryToAxbPackage(rawBytes, byteLength, *pkg, nullptr, artboardNameHint)) {
        return false;
    }
    loadedSourceUri_.clear();
    return MountDecodedRivePackage(std::move(pkg));
}

bool RiveControl::ReloadFromSourceUri(std::string_view sourceUri) {
    if (reloadingSource_) {
        return false;
    }
    if (!sourceUri.empty() && sourceUri == loadedSourceUri_ && IsRiveLoaded()) {
        return true;
    }
    reloadingSource_ = true;

    // Check pre-registered AxbTemplateRegistry first, then disk .riv file
    if (const AxbPackage* cachedPkg = AxbTemplateRegistry::Instance().Find(sourceUri)) {
        auto pkgCopy = std::make_shared<AxbPackage>(*cachedPkg);
        const bool ok = MountDecodedRivePackage(std::move(pkgCopy));
        if (ok) loadedSourceUri_ = std::string(sourceUri);
        reloadingSource_ = false;
        return ok;
    }

    std::vector<std::uint8_t> rawBytes;
    if (!ResolveAndReadRiveAsset(sourceUri, rawBytes)) {
        reloadingSource_ = false;
        return false;
    }

    auto decodedPkg = std::make_shared<AxbPackage>();
    if (!DecodeRiveBinaryToAxbPackage(rawBytes.data(), rawBytes.size(), *decodedPkg, nullptr)) {
        reloadingSource_ = false;
        return false;
    }

    const bool ok = MountDecodedRivePackage(std::move(decodedPkg));
    if (ok) {
        loadedSourceUri_ = std::string(sourceUri);
    }
    reloadingSource_ = false;
    return ok;
}

bool RiveControl::MountDecodedRivePackage(std::shared_ptr<AxbPackage> pkg) {
    if (!pkg || pkg->instructions.empty()) {
        return false;
    }

    AnimationEngine* activeAnim = externalAnimationEngine_;
    if (!activeAnim) {
        if (!ownedAnimationEngine_) {
            ownedAnimationEngine_ = std::make_unique<AnimationEngine>(64u);
        } else {
            ownedAnimationEngine_->Clear();
        }
        activeAnim = ownedAnimationEngine_.get();
    } else if (artboardRoot_) {
        (void)activeAnim->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }

    while (FirstChild() != nullptr) {
        (void)RemoveChild(FirstChild());
    }
    artboardRoot_.Reset();

    mountedPackage_ = std::move(pkg);
    artboardRoot_ = MakeIntrusive<Canvas>();
    (void)AddChild(artboardRoot_.Get());

    const bool mounted = MountAxbTemplate(
        artboardRoot_.Get(),
        *mountedPackage_,
        nullptr,
        nullptr,
        {},
        nullptr,
        nullptr,
        activeAnim);
    if (!mounted) {
        artboardRoot_.Reset();
        mountedPackage_.reset();
        return false;
    }

    const float abW = (artboardRoot_->GetWidth() > 0.0f) ? artboardRoot_->GetWidth() : 256.0f;
    const float abH = (artboardRoot_->GetHeight() > 0.0f) ? artboardRoot_->GetHeight() : 256.0f;
    artboardSize_ = Size{abW, abH};

    // Prime initial keyframe pose at t=0 so visual properties match entry state immediately
    if (activeAnim && mountedPackage_->HasMotionSection()) {
        (void)activeAnim->EvaluateStateMachines(0.0f);
        (void)activeAnim->Tick(0.0f);
    }

    InvalidateLayout();
    InvalidateRender();
    return true;
}

bool RiveControl::SetInputFloat(std::string_view inputName, float value) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return false;
    }
    const bool ok = anim->SetStateMachineInputFloatByName(artboardRoot_.Get(), inputName, value);
    if (ok) {
        (void)anim->EvaluateStateMachines(0.0f);
        InvalidateRender();
    }
    return ok;
}

bool RiveControl::SetInputBool(std::string_view inputName, bool value) noexcept {
    return SetInputFloat(inputName, value ? 1.0f : 0.0f);
}

bool RiveControl::FireTrigger(std::string_view triggerName) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return false;
    }
    const bool ok = anim->FireStateMachineTriggerByName(artboardRoot_.Get(), triggerName);
    if (ok) {
        (void)anim->EvaluateStateMachines(0.0f);
        InvalidateRender();
    }
    return ok;
}

float RiveControl::GetInputFloat(std::string_view inputName) const noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return 0.0f;
    }
    return anim->GetStateMachineInputFloatByName(artboardRoot_.Get(), inputName);
}

std::size_t RiveControl::Advance(float dtSeconds) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return 0u;
    }
    (void)anim->EvaluateStateMachines(dtSeconds);
    return anim->Tick(dtSeconds);
}

void RiveControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    UserControl::OnPropertyChanged(propId, newValue);
    if (propId == PropId_RiveSource || propId == PropId_ImageSource) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            if (!sv->view().empty()) {
                (void)ReloadFromSourceUri(sv->view());
            }
        }
        return;
    }
    if (propId >= PropId_FirstContractInput &&
        propId < static_cast<std::uint16_t>(PropId_FirstContractInput + 0x1000u) &&
        artboardRoot_) {
        const auto slotIdx = static_cast<std::uint16_t>(propId - PropId_FirstContractInput);
        float fv = 0.0f;
        if (TryGetFloat(newValue, fv)) {
            if (AnimationEngine* anim = ActiveAnimationEngine()) {
                (void)anim->SetStateMachineInputFloat(artboardRoot_.Get(), slotIdx, fv);
                (void)anim->EvaluateStateMachines(0.0f);
            }
        } else if (const auto* bv = std::get_if<bool>(&newValue)) {
            if (AnimationEngine* anim = ActiveAnimationEngine()) {
                (void)anim->SetStateMachineInputBool(artboardRoot_.Get(), slotIdx, *bv);
                (void)anim->EvaluateStateMachines(0.0f);
            }
        } else if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const float parsed = (sv->view() == "true" || sv->view() == "True" || sv->view() == "1")
                ? 1.0f
                : 0.0f;
            if (AnimationEngine* anim = ActiveAnimationEngine()) {
                (void)anim->SetStateMachineInputFloat(artboardRoot_.Get(), slotIdx, parsed);
                (void)anim->EvaluateStateMachines(0.0f);
            }
        }
    }
}

Size RiveControl::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    if (!artboardRoot_) {
        return UserControl::MeasureCore(availableSize);
    }

    artboardRoot_->Measure(Size{kUnconstrainedSpace, kUnconstrainedSpace});
    const float natW = (artboardSize_.width > 0.0f) ? artboardSize_.width : artboardRoot_->DesiredSize().width;
    const float natH = (artboardSize_.height > 0.0f) ? artboardSize_.height : artboardRoot_->DesiredSize().height;
    const bool hasW = std::isfinite(availableSize.width) && (availableSize.width < kUnconstrainedSpace * 0.5f);
    const bool hasH = std::isfinite(availableSize.height) && (availableSize.height < kUnconstrainedSpace * 0.5f);

    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScaleFactors(
        natW,
        natH,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    return Size{natW * sx + padW, natH * sy + padH};
}

void RiveControl::ArrangeCore(Rect finalRect) noexcept {
    if (!artboardRoot_) {
        UserControl::ArrangeCore(finalRect);
        return;
    }

    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const float natW = (artboardSize_.width > 0.0f) ? artboardSize_.width : artboardRoot_->DesiredSize().width;
    const float natH = (artboardSize_.height > 0.0f) ? artboardSize_.height : artboardRoot_->DesiredSize().height;

    float sx = 1.0f;
    float sy = 1.0f;
    ComputeViewboxScaleFactors(
        natW,
        natH,
        innerW,
        innerH,
        true,
        true,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    const float scaledW = natW * sx;
    const float scaledH = natH * sy;
    const float newOriginX = startX + (innerW - scaledW) * 0.5f;
    const float newOriginY = startY + (innerH - scaledH) * 0.5f;

    const bool scaleChanged =
        (std::fabs(childScaleX_ - sx) > 1e-5f) ||
        (std::fabs(childScaleY_ - sy) > 1e-5f) ||
        (std::fabs(childOriginX_ - newOriginX) > 1e-3f) ||
        (std::fabs(childOriginY_ - newOriginY) > 1e-3f);

    childScaleX_ = sx;
    childScaleY_ = sy;
    childOriginX_ = newOriginX;
    childOriginY_ = newOriginY;

    artboardRoot_->Arrange(Rect{newOriginX, newOriginY, natW, natH});

    if (scaleChanged) {
        InvalidateRender();
        MarkSubtreeRenderDirtyRecursive(artboardRoot_.Get());
    }
}

} // namespace aero
