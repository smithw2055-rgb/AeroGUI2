#include "aero/controls/Controls.hpp"
#include "aero/animation/AnimationEngine.hpp"
#include "aero/style/StyleTriggerEngine.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

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
TypeClass gCheckBoxTypeClass{};
TypeClass gRadioButtonTypeClass{};
TypeClass gSliderTypeClass{};
TypeClass gTextBoxTypeClass{};
TypeClass gPasswordBoxTypeClass{};
TypeClass gItemsControlTypeClass{};
TypeClass gComboBoxTypeClass{};
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
    const auto* button = Button::StaticTypeClass();
    reg.RegisterType(gCheckBoxTypeClass, "Aero.UI.CheckBox", button);
    reg.RegisterType(gRadioButtonTypeClass, "Aero.UI.RadioButton", button);
    reg.RegisterType(gSliderTypeClass, "Aero.UI.Slider", uiElem);
    reg.RegisterType(gTextBoxTypeClass, "Aero.UI.TextBox", &gTextBlockTypeClass);
    reg.RegisterType(gPasswordBoxTypeClass, "Aero.UI.PasswordBox", &gTextBoxTypeClass);
    reg.RegisterType(gItemsControlTypeClass, "Aero.UI.ItemsControl", panel);
    reg.RegisterType(gComboBoxTypeClass, "Aero.UI.ComboBox", &gItemsControlTypeClass);
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

const TypeClass* ComboBox::StaticTypeClass() noexcept {
    EnsureControlTypesRegistered();
    return &gComboBoxTypeClass;
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

Size Slider::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    if (GetOrientation() == Orientation::Vertical) {
        return Size{24.0f + p.left + p.right, 120.0f + p.top + p.bottom};
    }
    return Size{120.0f + p.left + p.right, 24.0f + p.top + p.bottom};
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
    const Size prefixMetrics = MeasureUtf8TextRun(std::string_view(displayStr).substr(0, byteOff), fontSize);
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
    const Size prefixMetrics = MeasureUtf8TextRun(std::string_view(displayStr).substr(0, byteOff), fontSize);
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

    float penX = 0.0f;
    std::size_t cursor = 0u;
    std::size_t cpIdx = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(display, cursor, cp)) {
        const float adv = IsWideCjkCodePoint(cp) ? (fontSize * 0.95f) : (fontSize * 0.6f);
        if (relX < penX + adv * 0.5f) {
            break;
        }
        penX += adv;
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
    const Size metrics = MeasureUtf8TextRun(text, fontSize);
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

void ItemsControl::MarkExtraValuesInPools() const noexcept {
    for (const auto& item : items_) {
        if (item.isTraceable()) {
            rs_rt::markValueInPools(item);
        }
    }
}

void ItemsControl::RebuildAllMaterializedItems(NativeHandleRegistry* registry, ManagedHeap* uiHeap) {
    virtualSlots_.clear();
    firstRealizedIndex_ = 0u;
    realizedEndExclusive_ = items_.size();

    AnimationEngine* effAnimEngine = animationEngine_
        ? animationEngine_
        : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
    while (UIElement* ch = FirstChild()) {
        if (effAnimEngine) {
            effAnimEngine->StopSubtreeAnimations(ch, false);
        }
        if (bindingEngine_) {
            (void)bindingEngine_->UnbindSubtree(ch);
        }
        if (styleEngine_) {
            (void)styleEngine_->UnbindSubtreeStyles(ch);
        }
        RemoveChild(ch, registry, uiHeap);
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        IntrusivePtr<UIElement> itemVisual;
        if (itemGenerator_) {
            itemVisual = itemGenerator_(i, items_[i]);
        } else if (const AxbPackage* tplPkg = ResolveActiveItemTemplate(items_[i])) {
            itemVisual = InstantiateTemplatedItem(*tplPkg, items_[i]);
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

void ItemsControl::SyncVirtualizedWindow(
    std::size_t targetFirstIdx,
    std::size_t targetEndExclusive,
    Size measureConstraint) {
    const std::size_t total = items_.size();
    targetFirstIdx = std::min(targetFirstIdx, total);
    targetEndExclusive = std::clamp(targetEndExclusive, targetFirstIdx, total);

    NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
    ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;

    // Partition existing realized slots into kept (still inside window) vs recyclable (scrolled out of window)
    std::vector<RealizedVirtualSlot> kept;
    std::vector<RealizedVirtualSlot> recyclable;
    kept.reserve(targetEndExclusive - targetFirstIdx);
    recyclable.reserve(virtualSlots_.size());

    for (auto& slot : virtualSlots_) {
        if (slot.element &&
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
        if (!recyclable.empty()) {
            RealizedVirtualSlot recycledSlot = std::move(recyclable.back());
            recyclable.pop_back();
            IntrusivePtr<UIElement> reused = std::move(recycledSlot.element);
            if (reused->GetVisibility() != Visibility::Visible) {
                reused->SetVisibility(Visibility::Visible);
            }
            if (itemBinder_) {
                itemBinder_(idx, items_[idx], *reused);
                return reused;
            }
            if (!itemGenerator_) {
                if (tplPkg != nullptr && recycledSlot.appliedTemplate == tplPkg) {
                    reused->SetValue(PropId_DataContext, items_[idx], ValueSourceRank::Local);
                    ObjectRef itemObj{};
                    if (const auto* ref = std::get_if<ObjectRef>(&items_[idx])) {
                        itemObj = *ref;
                    }
                    if (bindingEngine_) {
                        (void)bindingEngine_->RebindSubtreeRootObject(reused.Get(), itemObj);
                    }
                    if (styleEngine_) {
                        (void)styleEngine_->EvaluateSubtree(reused.Get(), true);
                    }
                    return reused;
                }
                if (tplPkg == nullptr && recycledSlot.appliedTemplate == nullptr) {
                    if (auto* tb = reused->DynamicCast<TextBlock>()) {
                        tb->SetText(rs_rt::valueToString(items_[idx]));
                        return reused;
                    }
                }
            }
            // Custom itemGenerator without itemBinder or mismatched template: replace element cleanly
            if (auto* animEng = animationEngine_ ? animationEngine_ : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr)) {
                animEng->StopSubtreeAnimations(reused.Get(), false);
            }
            if (bindingEngine_) {
                (void)bindingEngine_->UnbindSubtree(reused.Get());
            }
            if (styleEngine_) {
                (void)styleEngine_->UnbindSubtreeStyles(reused.Get());
            }
            RemoveChild(reused.Get(), registry, uiHeap);
        }

        IntrusivePtr<UIElement> created;
        if (itemGenerator_) {
            created = itemGenerator_(idx, items_[idx]);
        } else if (tplPkg) {
            created = InstantiateTemplatedItem(*tplPkg, items_[idx]);
        } else {
            auto tb = MakeIntrusive<TextBlock>();
            tb->SetText(rs_rt::valueToString(items_[idx]));
            created = tb;
        }
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
            if (k.itemIndex == idx) {
                matched = std::move(k.element);
                matchedTpl = k.appliedTemplate;
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

    // Remove any leftover surplus containers if viewport shrank
    AnimationEngine* effAnimEngine = animationEngine_
        ? animationEngine_
        : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
    for (auto& surplus : recyclable) {
        if (surplus.element) {
            if (effAnimEngine) {
                effAnimEngine->StopSubtreeAnimations(surplus.element.Get(), false);
            }
            if (bindingEngine_) {
                (void)bindingEngine_->UnbindSubtree(surplus.element.Get());
            }
            if (styleEngine_) {
                (void)styleEngine_->UnbindSubtreeStyles(surplus.element.Get());
            }
            RemoveChild(surplus.element.Get(), registry, uiHeap);
        }
    }

    virtualSlots_ = std::move(nextWindow);
    firstRealizedIndex_ = targetFirstIdx;
    realizedEndExclusive_ = targetEndExclusive;
}

void ItemsControl::SetItemsSource(
    const std::vector<UnifiedValue>& items,
    NativeHandleRegistry* registry,
    ManagedHeap* uiHeap) {
    const bool alreadyRealized = IsVirtualizing() ? !virtualSlots_.empty() : (FirstChild() != nullptr);
    if (!IsA<ComboBox>() && alreadyRealized && items.size() == items_.size()) {
        bool allSame = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].index() != items_[i].index()) {
                allSame = false;
                break;
            }
            if (const auto* rA = std::get_if<ObjectRef>(&items[i])) {
                if (!(*rA == std::get<ObjectRef>(items_[i]))) {
                    allSame = false;
                    break;
                }
            } else if (rs_rt::valueToString(items[i]) != rs_rt::valueToString(items_[i])) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            return;
        }
    }
    items_ = items;
    bool hasTraceable = false;
    for (const auto& item : items_) {
        if (item.isTraceable()) {
            rs_rt::pinValueInPools(item);
            hasTraceable = true;
        }
    }
    if (hasTraceable) {
        EnsureRegisteredInValuePoolSet();
    }
    estimatedMaxItemWidth_ = 80.0f;

    if (IsVirtualizing() && !IsA<ComboBox>()) {
        virtualSlots_.clear();
        firstRealizedIndex_ = 0u;
        realizedEndExclusive_ = 0u;
        AnimationEngine* effAnimEngine = animationEngine_
            ? animationEngine_
            : (bindingEngine_ ? bindingEngine_->GetAnimationEngine() : nullptr);
        while (UIElement* ch = FirstChild()) {
            if (effAnimEngine) {
                effAnimEngine->StopSubtreeAnimations(ch, false);
            }
            if (bindingEngine_) {
                (void)bindingEngine_->UnbindSubtree(ch);
            }
            if (styleEngine_) {
                (void)styleEngine_->UnbindSubtreeStyles(ch);
            }
            RemoveChild(ch, registry, uiHeap);
        }
        InvalidateLayout();
        return;
    }

    RebuildAllMaterializedItems(registry, uiHeap);
}

void ItemsControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Panel::OnPropertyChanged(propId, newValue);
    if ((propId == PropId_IsVirtualizing || propId == PropId_ItemTemplate) &&
        !IsA<ComboBox>() && !items_.empty()) {
        NativeHandleRegistry* registry = domains_ ? &domains_->nativeHandles() : nullptr;
        ManagedHeap* uiHeap = domains_ ? &domains_->uiHeap() : nullptr;
        if (IsVirtualizing()) {
            virtualSlots_.clear();
            firstRealizedIndex_ = 0u;
            realizedEndExclusive_ = 0u;
            while (FirstChild()) {
                RemoveChild(FirstChild(), registry, uiHeap);
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

    if (IsVirtualizing() && !IsA<ComboBox>() && !items_.empty()) {
        const float itemH = GetVirtualItemHeight();
        const float totalLogicalH = static_cast<float>(items_.size()) * itemH;

        float viewTop = 0.0f;
        float viewH = 240.0f;
        if (const ScrollViewer* sv = FindAncestorScrollViewer()) {
            const Rect clip = sv->GetLocalClipBounds();
            if (clip.height > 0.0f && FinalRect().height > 0.0f) {
                viewTop = std::max(0.0f, clip.y - (FinalRect().y + p.top));
                viewH = clip.height;
            } else {
                viewTop = sv->GetVerticalOffset();
                if (sv->ViewportHeight() > 0.0f) {
                    viewH = sv->ViewportHeight();
                } else if (sv->GetHeight() > 0.0f) {
                    viewH = sv->GetHeight();
                }
            }
        } else if (childAvail.height < kUnconstrainedSpace * 0.5f && childAvail.height > 0.0f) {
            viewH = childAvail.height;
        }

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(viewTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((viewTop + viewH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));

        SyncVirtualizedWindow(firstIdx, endIdx, Size{childAvail.width, itemH});
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

    if (IsVirtualizing() && !IsA<ComboBox>() && !items_.empty()) {
        const float itemH = GetVirtualItemHeight();
        float visibleTop = 0.0f;
        float visibleH = std::max(itemH, finalRect.height - (p.top + p.bottom));
        if (const ScrollViewer* sv = FindAncestorScrollViewer()) {
            const Rect clip = sv->GetLocalClipBounds();
            if (clip.height > 0.0f) {
                visibleTop = std::max(0.0f, clip.y - startY);
                visibleH = clip.height;
            } else {
                visibleTop = sv->GetVerticalOffset();
                visibleH = std::max(itemH, sv->ViewportHeight());
            }
        }

        constexpr std::size_t kOverscan = 2u;
        const auto rawFirst = static_cast<std::size_t>(std::max(0.0f, std::floor(visibleTop / itemH)));
        const std::size_t firstIdx = (rawFirst > kOverscan) ? (rawFirst - kOverscan) : 0u;
        const auto rawEnd = static_cast<std::size_t>(std::ceil((visibleTop + visibleH) / itemH)) + kOverscan;
        const std::size_t endIdx = std::min(items_.size(), std::max(firstIdx + 1u, rawEnd));

        if (firstIdx != firstRealizedIndex_ || endIdx != realizedEndExclusive_ || virtualSlots_.empty()) {
            SyncVirtualizedWindow(firstIdx, endIdx, Size{innerW, itemH});
        }

        for (const auto& slot : virtualSlots_) {
            if (slot.element) {
                const float rowY = startY + static_cast<float>(slot.itemIndex) * itemH;
                slot.element->Arrange(Rect{startX, rowY, innerW, itemH});
            }
        }

        // Fixed-height virtualized window updates during Arrange do not alter total logical DesiredSize;
        // clear any transient MeasureDirty/SubtreeMeasureDirty caused by container recycling/rebinding.
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

AxbTemplateRegistry& AxbTemplateRegistry::Instance() noexcept {
    static AxbTemplateRegistry instance;
    return instance;
}

AxbTemplateRegistry::AxbTemplateRegistry() noexcept {
    rs_rt::registerExternalValuePoolRootMarker(
        this,
        [this]() noexcept {
            MarkValuesInPools();
        });
}

AxbTemplateRegistry::~AxbTemplateRegistry() {
    rs_rt::unregisterExternalValuePoolRootMarker(this);
}

void AxbTemplateRegistry::MarkValuesInPools() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : templates_) {
        kv.second.MarkValuesInPools();
    }
    for (const auto& kv : resources_) {
        if (kv.second.isTraceable()) {
            rs_rt::markValueInPools(kv.second);
        }
    }
    auto markStyle = [](const AxbStyleDefinition& st) noexcept {
        for (const auto& s : st.setters) {
            if (s.value.isTraceable()) rs_rt::markValueInPools(s.value);
        }
        for (const auto& tr : st.triggers) {
            for (const auto& cond : tr.conditions) {
                if (cond.expectedValue.isTraceable()) rs_rt::markValueInPools(cond.expectedValue);
            }
            for (const auto& s : tr.setters) {
                if (s.value.isTraceable()) rs_rt::markValueInPools(s.value);
            }
        }
        for (const auto& vsg : st.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& s : vs.setters) {
                    if (s.value.isTraceable()) rs_rt::markValueInPools(s.value);
                }
            }
        }
    };
    for (const auto& kv : styles_) {
        markStyle(kv.second);
    }
    for (const auto& kv : implicitStyles_) {
        markStyle(kv.second);
    }
    for (const auto& kv : storyboards_) {
        for (const auto& tl : kv.second.timelines) {
            if (tl.fromValue.isTraceable()) rs_rt::markValueInPools(tl.fromValue);
            if (tl.toValue.isTraceable()) rs_rt::markValueInPools(tl.toValue);
            for (const auto& kf : tl.keyFrames) {
                if (kf.value.isTraceable()) rs_rt::markValueInPools(kf.value);
            }
        }
    }
    for (const auto& kv : dataTemplates_) {
        if (kv.second) kv.second->MarkValuesInPools();
    }
    for (const auto& kv : implicitDataTemplates_) {
        if (kv.second) kv.second->MarkValuesInPools();
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
    package.PinValuesInPools();
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
    package.PinValuesInPools();
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
}

void AxbTemplateRegistry::RegisterStyle(AxbStyleDefinition style) {
    for (const auto& s : style.setters) {
        if (s.value.isTraceable()) rs_rt::pinValueInPools(s.value);
    }
    for (const auto& tr : style.triggers) {
        for (const auto& cond : tr.conditions) {
            if (cond.expectedValue.isTraceable()) rs_rt::pinValueInPools(cond.expectedValue);
        }
        for (const auto& s : tr.setters) {
            if (s.value.isTraceable()) rs_rt::pinValueInPools(s.value);
        }
    }
    for (const auto& vsg : style.visualStateGroups) {
        for (const auto& vs : vsg.states) {
            for (const auto& s : vs.setters) {
                if (s.value.isTraceable()) rs_rt::pinValueInPools(s.value);
            }
        }
    }
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
        for (const auto& tl : storyboard.timelines) {
            if (tl.fromValue.isTraceable()) rs_rt::pinValueInPools(tl.fromValue);
            if (tl.toValue.isTraceable()) rs_rt::pinValueInPools(tl.toValue);
            for (const auto& kf : tl.keyFrames) {
                if (kf.value.isTraceable()) rs_rt::pinValueInPools(kf.value);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        storyboards_[storyboard.key] = std::move(storyboard);
    }
}

void AxbTemplateRegistry::RegisterDataTemplate(AxbDataTemplate dataTemplate) {
    if (!dataTemplate.templatePackage) {
        return;
    }
    dataTemplate.templatePackage->PinValuesInPools();
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

void AxbTemplateRegistry::RegisterResource(std::string key, UnifiedValue value) {
    if (!key.empty()) {
        if (value.isTraceable()) {
            rs_rt::pinValueInPools(value);
        }
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
    if (shortName == "StackPanel" || shortName == "VirtualizingStackPanel" ||
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
    if (shortName == "Border" ||
        shortName == "ContentControl" || shortName == "ContentPresenter" ||
        shortName == "Rectangle" || shortName == "Ellipse" ||
        shortName == "Line" || shortName == "Polygon" || shortName == "Polyline" ||
        shortName == "RiveControl" || shortName == "BoxShadow" ||
        shortName == "ProgressBar" ||
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
    if (shortName == "ItemsControl" ||
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
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine) {
    if (!rootControl) {
        return false;
    }
    if (!animationEngine && bindingEngine) {
        animationEngine = bindingEngine->GetAnimationEngine();
    }

    if (!package.mtsdfPaths.empty()) {
        for (const auto& mp : package.mtsdfPaths) {
            AxbTemplateRegistry::Instance().RegisterMtsdfPath(mp);
        }
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
    const ObjectRef effectiveContext =
        dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();

    std::vector<UIElement*> elementStack;
    std::vector<UIElement*> localElements;
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
                            eventAotContext,
                            animationEngine);
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
                    ic->SetBindingEngine(bindingEngine);
                    ic->SetAnimationEngine(animationEngine);
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
                localElements.push_back(currentElem);
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
                            }
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

    if (rootVisited && animationEngine && package.HasMotionSection()) {
        animationEngine->InstantiatePackageMotion(
            rootControl,
            localElements.data(),
            localElements.size(),
            package);
    }

    return rootVisited;
}

} // namespace aero
