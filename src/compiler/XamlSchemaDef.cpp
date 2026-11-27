#include "aero/compiler/XamlSchemaDef.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "aero/core/StringUtils.hpp"

namespace aero {

float ParseXamlDurationSeconds(std::string_view raw) {
    std::string_view s = aero::str::Trim(raw);
    if (s.empty() || aero::str::EqualsIgnoreCase(s, "Automatic") ||
        aero::str::EqualsIgnoreCase(s, "Forever")) {
        return 0.3f;
    }
    if (s.size() >= 2u &&
        (s[s.size() - 2u] == 'm' || s[s.size() - 2u] == 'M') &&
        (s[s.size() - 1u] == 's' || s[s.size() - 1u] == 'S')) {
        float ms = 0.0f;
        if (aero::str::TryParseFloat32(s.substr(0, s.size() - 2u), ms)) {
            return ms * 0.001f;
        }
    }
    if (s.back() == 's' || s.back() == 'S') {
        float sec = 0.0f;
        if (aero::str::TryParseFloat32(s.substr(0, s.size() - 1u), sec)) {
            return sec;
        }
    }
    float daySeconds = 0.0f;
    const std::size_t firstColon = s.find(':');
    if (firstColon != std::string_view::npos) {
        const std::size_t dotBeforeColon = s.substr(0, firstColon).find('.');
        if (dotBeforeColon != std::string_view::npos) {
            float days = 0.0f;
            if (aero::str::TryParseFloat32(s.substr(0, dotBeforeColon), days)) {
                daySeconds = days * 86400.0f;
            }
            s = s.substr(dotBeforeColon + 1u);
        }
    }
    std::vector<float> parts;
    std::size_t start = 0u;
    while (start <= s.size()) {
        std::size_t colon = s.find(':', start);
        if (colon == std::string_view::npos) {
            colon = s.size();
        }
        float v = 0.0f;
        if (!aero::str::TryParseFloat32(s.substr(start, colon - start), v)) {
            return 0.3f;
        }
        parts.push_back(v);
        if (colon == s.size()) {
            break;
        }
        start = colon + 1u;
    }
    if (parts.size() == 1u) return daySeconds + parts[0];
    if (parts.size() == 2u) return daySeconds + parts[0] * 60.0f + parts[1];
    if (parts.size() >= 3u) return daySeconds + parts[0] * 3600.0f + parts[1] * 60.0f + parts[2];
    return 0.3f;
}

std::string NormalizeGradientStopPath(std::string_view s) {
    int stopIdx = -1;
    if (s.find("[0]") != std::string_view::npos) stopIdx = 0;
    else if (s.find("[1]") != std::string_view::npos) stopIdx = 1;
    else if (s.find("[2]") != std::string_view::npos) stopIdx = 2;
    else if (s.find("[3]") != std::string_view::npos) stopIdx = 3;
    if (stopIdx < 0) {
        return "Unsupported.GradientStop";
    }
    const std::string idxStr = std::to_string(stopIdx);
    if (s.find("OpacityMask") != std::string_view::npos) {
        if (s.find("Offset") != std::string_view::npos) {
            return "OpacityMask.Stop" + idxStr + ".Offset";
        }
        return "Unsupported.GradientStop";
    }
    const bool isStroke =
        s.find("Shape.Stroke") != std::string_view::npos ||
        s.find("Stroke.") != std::string_view::npos ||
        s.find("BorderBrush") != std::string_view::npos;
    const std::string prefix = isStroke ? "Stroke.Stop" : "Fill.Stop";
    if (s.find("Color") != std::string_view::npos) {
        return prefix + idxStr + "Color";
    }
    if (s.find("Offset") != std::string_view::npos) {
        return prefix + idxStr + "Offset";
    }
    return "Unsupported.GradientStop";
}

std::string NormalizeAnimationTargetPropertyPath(std::string_view raw) {
    std::string_view s = aero::str::Trim(raw);
    if (s.empty()) { return "Opacity"; }
    if (s == "Opacity" || s == "Width" || s == "Height" || s == "MinWidth" ||
        s == "MinHeight" || s == "Fill.Color" || s == "Stroke.Color" ||
        s == "Background.Color" || s == "Foreground.Color" ||
        s == "Foreground.Opacity" || s == "Fill.Opacity" || s == "Stroke.Opacity") {
        return std::string(s);
    }
    if (s.find("SolidColorBrush.Color") != std::string_view::npos) {
        if (s.find("Fill") != std::string_view::npos) return "Fill.Color";
        if (s.find("Stroke") != std::string_view::npos) return "Stroke.Color";
        if (s.find("Foreground") != std::string_view::npos) return "Foreground.Color";
        if (s.find("BorderBrush") != std::string_view::npos) return "BorderBrush.Color";
        return "Background.Color";
    }
    if (s.find("DropShadowEffect.Opacity") != std::string_view::npos ||
        s.find("Effect.Opacity") != std::string_view::npos ||
        s.find("Effect).(DropShadowEffect.Opacity") != std::string_view::npos ||
        s == "EffectOpacity") {
        return "EffectOpacity";
    }
    if (s.find("Brush.Opacity") != std::string_view::npos || s == "BrushOpacity") {
        if (s.find("Foreground") != std::string_view::npos) {
            return "Foreground.Opacity";
        }
        if (s.find("BorderBrush") != std::string_view::npos) {
            return "BorderBrush.Opacity";
        }
        if (s.find("Shape.Stroke") != std::string_view::npos ||
            s.find("(Stroke)") != std::string_view::npos ||
            s.find("Stroke.") != std::string_view::npos) {
            return "Stroke.Opacity";
        }
        if (s.find("Shape.Fill") != std::string_view::npos ||
            s.find("(Fill)") != std::string_view::npos ||
            s.find("Fill.") != std::string_view::npos) {
            return "Fill.Opacity";
        }
        return "BrushOpacity";
    }
    if (s.find("BlurEffect.Radius") != std::string_view::npos || s == "Radius" || s == "BlurRadius") {
        return "EffectRadius";
    }
    if (s.find("RelativeTransform") != std::string_view::npos) {
        const std::string prefix = (s.find("Fill") != std::string_view::npos)
            ? "Fill."
            : ((s.find("Stroke") != std::string_view::npos)
                ? "Stroke."
                : ((s.find("Foreground") != std::string_view::npos) ? "Foreground." : "Background."));
        if (s.find(".X") != std::string_view::npos || s.find("TranslateX") != std::string_view::npos) {
            return prefix + "RelativeTransform.X";
        }
        if (s.find(".Y") != std::string_view::npos || s.find("TranslateY") != std::string_view::npos) {
            return prefix + "RelativeTransform.Y";
        }
    }
    if (s.find("LayoutTransform") != std::string_view::npos) {
        if (s.find("ScaleX") != std::string_view::npos) return "LayoutTransform.ScaleX";
        if (s.find("ScaleY") != std::string_view::npos) return "LayoutTransform.ScaleY";
        if (s.find("AngleX") != std::string_view::npos || s.find("SkewX") != std::string_view::npos) {
            return "LayoutTransform.SkewX";
        }
        if (s.find("AngleY") != std::string_view::npos || s.find("SkewY") != std::string_view::npos) {
            return "LayoutTransform.SkewY";
        }
        if (s.find("Angle") != std::string_view::npos || s.find("Rotation") != std::string_view::npos) {
            return "LayoutTransform.Angle";
        }
    }
    if (s.find("PlasmaBrush.") != std::string_view::npos) {
        const std::string prefix = (s.find("Background") != std::string_view::npos) ? "Background." : "Fill.";
        if (s.find("PlasmaBrush.Time") != std::string_view::npos) return prefix + "PlasmaTime";
        if (s.find("PlasmaBrush.Seed") != std::string_view::npos) return prefix + "PlasmaSeed";
        if (s.find("PlasmaBrush.Color") != std::string_view::npos) return prefix + "PlasmaColor";
        if (s.find("PlasmaBrush.ScaleX") != std::string_view::npos) return prefix + "PlasmaScaleX";
        if (s.find("PlasmaBrush.ScaleY") != std::string_view::npos) return prefix + "PlasmaScaleY";
    }
    if (s.find("Transform3D") != std::string_view::npos || s.find("CompositeTransform3D") != std::string_view::npos) {
        if (s.find("RotationX") != std::string_view::npos) return "Rot3DX";
        if (s.find("RotationY") != std::string_view::npos) return "Rot3DY";
        if (s.find("RotationZ") != std::string_view::npos) return "Rot3DZ";
        if (s.find("TranslateX") != std::string_view::npos || s.find("Translation3DX") != std::string_view::npos) return "Trans3DX";
        if (s.find("TranslateY") != std::string_view::npos || s.find("Translation3DY") != std::string_view::npos) return "Trans3DY";
        if (s.find("TranslateZ") != std::string_view::npos || s.find("Translation3DZ") != std::string_view::npos) return "Trans3DZ";
        if (s.find("ScaleX") != std::string_view::npos || s.find("Scale3DX") != std::string_view::npos) return "Scale3DX";
        if (s.find("ScaleY") != std::string_view::npos || s.find("Scale3DY") != std::string_view::npos) return "Scale3DY";
        if (s.find("ScaleZ") != std::string_view::npos || s.find("Scale3DZ") != std::string_view::npos) return "Scale3DZ";
        if (s.find("CenterX") != std::string_view::npos) return "Transform3D.CenterX";
        if (s.find("CenterY") != std::string_view::npos) return "Transform3D.CenterY";
        if (s.find("CenterZ") != std::string_view::npos) return "Transform3D.CenterZ";
    }
    if (s.find("ScaleTransform.ScaleX") != std::string_view::npos || s.find("ScaleX") != std::string_view::npos) {
        return "ScaleX";
    }
    if (s.find("ScaleTransform.ScaleY") != std::string_view::npos || s.find("ScaleY") != std::string_view::npos) {
        return "ScaleY";
    }
    if (s.find("RotateTransform.Angle") != std::string_view::npos || s == "Angle") {
        return "Rotation";
    }
    if (s.find("SkewTransform.AngleX") != std::string_view::npos || s.find("AngleX") != std::string_view::npos) {
        return "SkewX";
    }
    if (s.find("SkewTransform.AngleY") != std::string_view::npos || s.find("AngleY") != std::string_view::npos) {
        return "SkewY";
    }
    if (s.find("TranslateTransform.X") != std::string_view::npos || aero::str::EndsWith(s, ".X)")) {
        return "TranslateX";
    }
    if (s.find("TranslateTransform.Y") != std::string_view::npos || aero::str::EndsWith(s, ".Y)")) {
        return "TranslateY";
    }
    if (s.find("PlaneProjection.RotationX") != std::string_view::npos || s.find("RotationX") != std::string_view::npos) {
        return "Rot3DX";
    }
    if (s.find("PlaneProjection.RotationY") != std::string_view::npos || s.find("RotationY") != std::string_view::npos) {
        return "Rot3DY";
    }
    if (s.find("PlaneProjection.RotationZ") != std::string_view::npos || s.find("RotationZ") != std::string_view::npos) {
        return "Rot3DZ";
    }
    if (s.find("GlobalOffsetX") != std::string_view::npos || s.find("LocalOffsetX") != std::string_view::npos ||
        s.find("Translation3DX") != std::string_view::npos) {
        return "Trans3DX";
    }
    if (s.find("GlobalOffsetY") != std::string_view::npos || s.find("LocalOffsetY") != std::string_view::npos ||
        s.find("Translation3DY") != std::string_view::npos) {
        return "Trans3DY";
    }
    if (s.find("GlobalOffsetZ") != std::string_view::npos || s.find("LocalOffsetZ") != std::string_view::npos ||
        s.find("TranslateZ") != std::string_view::npos || s.find("Translation3DZ") != std::string_view::npos) {
        return "Trans3DZ";
    }
    if (s.find("PathFigure.Segments") != std::string_view::npos && s.find(".Point") != std::string_view::npos) {
        int figIdx = 0;
        if (const std::size_t fp = s.find("Figures)["); fp != std::string_view::npos) {
            figIdx = std::max(0, static_cast<int>(std::strtol(std::string(s.substr(fp + 9u)).c_str(), nullptr, 10)));
        }
        int segIdx = 0;
        if (const std::size_t sp = s.find("Segments)["); sp != std::string_view::npos) {
            segIdx = std::max(0, static_cast<int>(std::strtol(std::string(s.substr(sp + 10u)).c_str(), nullptr, 10)));
        }
        return "Data.Fig" + std::to_string(figIdx) + ".Seg" + std::to_string(segIdx) + ".Point";
    }
    if (s.find("GradientStops") != std::string_view::npos ||
        s.find("GradientStop.") != std::string_view::npos ||
        s.find("OpacityMask") != std::string_view::npos) {
        return NormalizeGradientStopPath(s);
    }
    if (s.front() == '(' && s.back() == ')' && s.size() > 2u) {
        std::string_view inner = s.substr(1, s.size() - 2u);
        const std::size_t dot = inner.rfind('.');
        if (dot != std::string_view::npos) {
            return std::string(inner.substr(dot + 1u));
        }
        return std::string(inner);
    }
    return std::string(s);
}

int MapXamlEasingKind(std::string_view name) noexcept {
    if (aero::str::StartsWith(name, "Quadratic")) return 1;
    if (aero::str::StartsWith(name, "Cubic")) return 2;
    if (aero::str::StartsWith(name, "Quartic")) return 3;
    if (aero::str::StartsWith(name, "Quintic")) return 4;
    if (aero::str::StartsWith(name, "Sine")) return 5;
    if (aero::str::StartsWith(name, "Circle")) return 6;
    if (aero::str::StartsWith(name, "Exponential")) return 7;
    if (aero::str::StartsWith(name, "Back")) return 8;
    if (aero::str::StartsWith(name, "Bounce")) return 9;
    if (aero::str::StartsWith(name, "Elastic")) return 10;
    if (aero::str::StartsWith(name, "Power")) return 2;
    return 0;
}

int MapXamlEasingMode(std::string_view mode) noexcept {
    if (aero::str::EqualsIgnoreCase(mode, "EaseIn")) return 1;
    if (aero::str::EqualsIgnoreCase(mode, "EaseInOut")) return 2;
    return 0;
}

int MapXamlBindingMode(std::string_view mode) noexcept {
    if (aero::str::EqualsIgnoreCase(mode, "TwoWay")) return 1;
    if (aero::str::EqualsIgnoreCase(mode, "OneTime")) return 2;
    if (aero::str::EqualsIgnoreCase(mode, "OneWayToSource")) return 3;
    return 0;
}

int ResolveDefaultBindingMode(std::string_view targetType, std::string_view propName) noexcept {
    if ((targetType == "TextBox" && propName == "Text") ||
        (targetType == "PasswordBox" && propName == "Password") ||
        (targetType == "Slider" && propName == "Value") ||
        ((targetType == "CheckBox" || targetType == "ToggleButton" || targetType == "RadioButton") &&
         propName == "IsChecked") ||
        ((targetType == "ComboBox" || targetType == "ListBox" || targetType == "Selector" ||
          targetType == "ListView" || targetType == "TabControl") &&
         (propName == "SelectedItem" || propName == "SelectedIndex" || propName == "SelectedValue"))) {
        return 1;
    }
    return 0;
}

const char* LookupXamlEnumType(std::string_view propName) {
    static const std::unordered_map<std::string_view, const char*> kEnumProps = {
        {"Visibility", "Visibility"},
        {"Orientation", "Orientation"},
        {"HorizontalAlignment", "HorizontalAlignment"},
        {"VerticalAlignment", "VerticalAlignment"},
        {"HorizontalContentAlignment", "HorizontalAlignment"},
        {"VerticalContentAlignment", "VerticalAlignment"},
        {"Stretch", "Stretch"},
        {"StretchDirection", "StretchDirection"},
        {"ExpandDirection", "ExpandDirection"},
        {"Dock", "Dock"},
        {"ClickMode", "ClickMode"},
        {"SelectionMode", "SelectionMode"},
        {"TabStripPlacement", "TabStripPlacement"},
        {"HorizontalScrollBarVisibility", "ScrollBarVisibility"},
        {"VerticalScrollBarVisibility", "ScrollBarVisibility"},
        {"TextWrapping", "TextWrapping"},
        {"TextAlignment", "TextAlignment"},
        {"TextTrimming", "TextTrimming"}
    };
    const auto it = kEnumProps.find(propName);
    return (it != kEnumProps.end()) ? it->second : nullptr;
}

bool IsXamlFloatProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kFloatProps = {
        "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight",
        "FontSize", "Opacity", "SelectionOpacity", "Spacing", "Minimum", "Maximum", "Value",
        "SmallChange", "LargeChange", "StrokeThickness", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "CenterX", "CenterY", "Radius", "BlurRadius",
        "ShadowDepth", "Direction", "Angle", "AngleX", "AngleY", "ScaleX", "ScaleY",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Volume",
        "LineHeight", "CharacterSpacing",
        "ViewportSize", "MaxDropDownHeight", "HorizontalOffset", "VerticalOffset",
        "OffsetX", "OffsetY", "Spread", "ItemWidth", "ItemHeight"
    };
    return kFloatProps.count(prop) > 0;
}

bool IsXamlIntProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kIntProps = {
        "ZIndex", "MaxLines", "TabIndex", "SelectedIndex", "Rows", "Columns", "FirstColumn", "MaxLength",
        "Year", "Month", "Day", "Hour", "Minute", "Second"
    };
    return kIntProps.count(prop) > 0;
}

bool IsXamlBoolProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kBoolProps = {
        "IsEnabled", "IsHitTestVisible", "Focusable", "ClipToBounds", "IsChecked",
        "IsThreeState", "IsExpanded", "IsSelected", "IsReadOnly", "LastChildFill",
        "IsOpen", "IsCheckable", "IsDefault", "IsCancel", "ShowGridLines",
        "StaysOpen", "IsEditable", "IsMuted", "ShowDate", "ShowTime", "UseSystemTime",
        "IsDirectionReversed", "IsTabStop", "IsMouseOver", "IsPressed", "IsDragging",
        "IsFocused", "IsKeyboardFocused", "HasItems", "IsDropDownOpen", "CanContentScroll",
        "Inside", "IsItemsHost"
    };
    return kBoolProps.count(prop) > 0;
}

bool IsXamlThicknessProperty(std::string_view prop) noexcept {
    return prop == "Margin" || prop == "Padding" || prop == "BorderThickness";
}

bool IsXamlBrushProperty(std::string_view prop) noexcept {
    return prop == "Background" || prop == "Foreground" || prop == "BorderBrush" ||
           prop == "Fill" || prop == "Stroke" || prop == "CaretBrush" ||
           prop == "SelectionBrush" || prop == "Brush" || prop == "OpacityMask";
}

bool IsXamlBrushElementTag(std::string_view tagName) noexcept {
    return tagName == "SolidColorBrush" ||
           tagName == "LinearGradientBrush" ||
           tagName == "RadialGradientBrush" ||
           tagName == "ImageBrush" ||
           tagName == "VisualBrush";
}

bool IsXamlEventAttribute(std::string_view prop) {
    static const std::unordered_set<std::string_view> kEvents = {
        "Click", "Loaded", "Unloaded", "Checked", "Unchecked", "Indeterminate",
        "SelectionChanged", "TextChanged", "ValueChanged", "GotFocus", "LostFocus",
        "KeyDown", "KeyUp", "MouseDown", "MouseUp", "MouseEnter", "MouseLeave",
        "MouseLeftButtonDown", "MouseLeftButtonUp", "MouseRightButtonDown",
        "MouseRightButtonUp", "MouseMove", "RequestNavigate", "Expanded",
        "Collapsed", "Opened", "Closed", "DragDelta", "MediaEnded", "MediaOpened",
        "PasswordChanged"
    };
    return kEvents.count(prop) > 0;
}

bool IsXamlDirectUIProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kProps = {
        "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight",
        "Margin", "Padding", "HorizontalAlignment", "VerticalAlignment",
        "HorizontalContentAlignment", "VerticalContentAlignment",
        "Visibility", "Opacity", "IsEnabled", "IsHitTestVisible", "Focusable",
        "ClipToBounds", "ZIndex", "Background", "Foreground", "BorderBrush",
        "BorderThickness", "CornerRadius", "Text", "FontSize", "FontFamily", "FontWeight",
        "TextWrapping", "TextAlignment", "TextTrimming", "Orientation", "Spacing",
        "Stretch", "StretchDirection", "ExpandDirection", "Source", "Data",
        "Fill", "Stroke", "StrokeThickness", "StrokeDashArray", "StrokeDashCap",
        "StrokeStartLineCap", "StrokeEndLineCap", "CaretBrush", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "Minimum", "Maximum", "Value", "SmallChange",
        "LargeChange", "IsChecked", "IsThreeState", "IsExpanded", "IsSelected",
        "IsReadOnly", "LastChildFill", "SelectedIndex", "SelectionMode",
        "Placeholder", "IsCheckable", "TabStripPlacement",
        "HorizontalScrollBarVisibility", "VerticalScrollBarVisibility",
        "RenderTransform", "LayoutTransform", "RenderTransformOrigin", "Effect", "OpacityMask", "Style",
        "ShowGridLines", "Rows", "Columns", "FirstColumn",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Brush",
        "Volume", "IsMuted", "LoadedBehavior", "Password", "PasswordChar",
        "Year", "Month", "Day", "Hour", "Minute", "Second", "Format", "UseSystemTime",
        "LineHeight", "CharacterSpacing", "MaxLines",
        "IsDirectionReversed", "IsTabStop", "TabIndex", "ViewportSize", "MaxDropDownHeight",
        "FontStyle", "IsItemsHost", "FlowDirection", "ItemWidth", "ItemHeight"
    };
    return kProps.count(prop) > 0;
}

bool IsXamlContentControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kCC = {
        "ContentControl", "HeaderedContentControl", "Window", "Page",
        "UserControl", "ScrollViewer", "ButtonBase", "Button",
        "RepeatButton", "ToggleButton", "CheckBox", "RadioButton", "GroupBox",
        "Expander", "TabItem", "MenuItem", "ListBoxItem", "ComboBoxItem",
        "ListViewItem", "StatusBarItem", "ToolTip", "Label", "GridViewColumnHeader",
        "ViewportSurface", "DockPane"
    };
    return kCC.count(typeName) > 0;
}

bool IsXamlItemsControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kIC = {
        "ItemsControl", "HeaderedItemsControl", "Selector", "ListBox", "ListView",
        "ComboBox", "TabControl", "Menu", "MenuItem", "ContextMenu",
        "ToolBar", "StatusBar", "TreeView", "TreeViewItem"
    };
    return kIC.count(typeName) > 0;
}

bool IsXamlPanelType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kPanels = {
        "Panel", "Grid", "StackPanel", "VirtualizingStackPanel", "VirtualizingUniformGrid",
        "VirtualizingWrapGrid", "TabPanel", "ToolBarPanel", "DockPanel", "WrapPanel",
        "ToolBarOverflowPanel", "UniformGrid", "Canvas"
    };
    return kPanels.count(typeName) > 0;
}

bool IsXamlShapeType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kShapes = {
        "Shape", "Rectangle", "Ellipse", "Line", "Path", "Polygon", "Polyline"
    };
    return kShapes.count(typeName) > 0;
}

bool IsXamlRangeBaseType(std::string_view typeName) noexcept {
    return typeName == "RangeBase" || typeName == "Slider" ||
           typeName == "ProgressBar" || typeName == "ScrollBar";
}

bool IsXamlToggleButtonType(std::string_view typeName) noexcept {
    return typeName == "ToggleButton" || typeName == "CheckBox" || typeName == "RadioButton";
}

bool IsXamlSelectableItemType(std::string_view typeName) noexcept {
    return typeName == "SelectableItem" || typeName == "ListBoxItem" ||
           typeName == "ComboBoxItem" || typeName == "ListViewItem" ||
           typeName == "TabItem" || typeName == "TreeViewItem";
}

bool IsXamlSelectorType(std::string_view typeName) noexcept {
    return typeName == "Selector" || typeName == "ListBox" ||
           typeName == "ListView" || typeName == "ComboBox" || typeName == "TabControl";
}

bool IsXamlControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kControls = {
        "Control", "ContentControl", "HeaderedContentControl", "GroupBox", "Expander",
        "Label", "ToolTip", "Separator", "UserControl", "Page", "Window", "ScrollViewer",
        "ButtonBase", "Button", "RepeatButton", "Hyperlink", "ToggleButton", "CheckBox",
        "RadioButton", "Thumb", "GridSplitter", "RangeBase", "Slider", "ProgressBar",
        "ScrollBar", "TextBoxBase", "TextBox", "PasswordBox", "ItemsControl",
        "HeaderedItemsControl", "SelectableItem", "ListBoxItem", "ComboBoxItem",
        "ListViewItem", "Selector", "ListBox", "ListView", "ComboBox", "TabControl",
        "TabItem", "MenuBase", "Menu", "ContextMenu", "MenuItem", "ToolBar",
        "StatusBar", "StatusBarItem", "TreeView", "TreeViewItem", "GridViewColumnHeader",
        "DateTimeControl", "RiveControl",
        "NumericScrubber", "Vector2Field", "Vector3Field", "ColorPicker", "ViewportSurface",
        "DockPane", "DockTabGroup", "DockSplitNode", "DockingWorkspace",
        "CodeEditorView", "PropertyGrid", "NodeGraphCanvas"
    };
    return kControls.count(typeName) > 0;
}

bool IsXamlNonUIElementType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kNonUi = {
        "Inline", "Run", "LineBreak", "GridViewColumn", "GridView",
        "RowDefinition", "ColumnDefinition", "Transform", "TranslateTransform",
        "ScaleTransform", "RotateTransform", "SkewTransform", "TransformGroup",
        "Brush", "SolidColorBrush", "LinearGradientBrush", "RadialGradientBrush",
        "ImageBrush", "VisualBrush", "GradientStop", "Effect", "UIEffect", "BlurEffect",
        "DropShadowEffect", "BackdropBlurEffect", "TintEffect", "PixelateEffect",
        "ChromaticAberrationEffect", "DirectionalBlurEffect"
    };
    return kNonUi.count(typeName) > 0;
}

bool IsXamlKnownUIElementType(std::string_view typeName) {
    if (IsXamlControlType(typeName) || IsXamlPanelType(typeName) || IsXamlShapeType(typeName)) {
        return true;
    }
    static const std::unordered_set<std::string_view> kOtherUi = {
        "UIElement", "FrameworkElement", "Decorator", "Border", "BoxShadow",
        "Viewbox", "LayoutScaler", "TextBlock", "Image", "ContentPresenter",
        "ScrollContentPresenter", "Track", "TickBar", "Popup", "CustomDrawElement",
        "RadialTicks", "RadialNeedle", "MediaElement", "GridViewHeaderRowPresenter",
        "GridViewRowPresenter"
    };
    return kOtherUi.count(typeName) > 0;
}

bool IsXamlCommonUIElementDirectProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kBaseProps = {
        "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight",
        "Margin", "Padding", "HorizontalAlignment", "VerticalAlignment",
        "Visibility", "Opacity", "IsEnabled", "IsHitTestVisible", "Focusable",
        "ClipToBounds", "ZIndex", "TabIndex", "RenderTransform", "LayoutTransform", "RenderTransformOrigin",
        "Effect", "OpacityMask", "Style", "Background", "BorderBrush",
        "BorderThickness", "CornerRadius", "FlowDirection"
    };
    return kBaseProps.count(prop) > 0;
}

bool IsXamlDirectUIPropertyFor(std::string_view targetType, std::string_view prop) {
    if (!IsXamlDirectUIProperty(prop)) {
        return false;
    }
    if (IsXamlNonUIElementType(targetType)) {
        if (targetType == "Inline" || targetType == "Run" || targetType == "LineBreak") {
            return prop == "Text" || prop == "Foreground" || prop == "FontFamily" ||
                   prop == "FontSize" || prop == "FontWeight" || prop == "FontStyle";
        }
        if (targetType == "GridViewColumn") {
            return prop == "Width";
        }
        return false;
    }
    if (IsXamlCommonUIElementDirectProperty(prop)) {
        return true;
    }
    if (targetType.empty() || !IsXamlKnownUIElementType(targetType)) {
        return false;
    }

    if (prop == "Foreground" || prop == "FontFamily" || prop == "FontSize" || prop == "FontWeight") {
        return IsXamlControlType(targetType) || targetType == "TextBlock" ||
               targetType == "ContentPresenter" || targetType == "ScrollContentPresenter";
    }
    if (prop == "FontStyle") {
        return IsXamlControlType(targetType) || targetType == "TextBlock";
    }
    if (prop == "HorizontalContentAlignment" || prop == "VerticalContentAlignment" ||
        prop == "IsTabStop") {
        return IsXamlControlType(targetType);
    }
    if (prop == "IsItemsHost") return IsXamlPanelType(targetType);
    if (prop == "ShowGridLines") return targetType == "Grid";
    if (prop == "Orientation") {
        return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
               targetType == "VirtualizingWrapGrid" ||
               targetType == "TabPanel" || targetType == "ToolBarPanel" ||
               targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
               targetType == "ToolBar" || targetType == "Slider" ||
               targetType == "ScrollBar" || targetType == "ProgressBar" ||
               targetType == "Track";
    }
    if (prop == "Spacing") {
        return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
               targetType == "VirtualizingUniformGrid" || targetType == "VirtualizingWrapGrid" ||
               targetType == "TabPanel" || targetType == "ToolBarPanel" ||
               targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
               targetType == "UniformGrid";
    }
    if (prop == "LastChildFill") return targetType == "DockPanel";
    if (prop == "Rows" || prop == "Columns" || prop == "FirstColumn") {
        return targetType == "UniformGrid" || targetType == "VirtualizingUniformGrid";
    }
    if (prop == "ItemWidth" || prop == "ItemHeight") {
        return targetType == "WrapPanel" || targetType == "VirtualizingWrapGrid";
    }
    if (prop == "Stretch") {
        return targetType == "Viewbox" || targetType == "LayoutScaler" ||
               targetType == "Image" || IsXamlShapeType(targetType) ||
               targetType == "RiveControl" || targetType == "MediaElement";
    }
    if (prop == "StretchDirection") {
        return targetType == "Viewbox" || targetType == "LayoutScaler" || targetType == "Image";
    }
    if (prop == "Text" || prop == "TextWrapping" || prop == "TextAlignment" || prop == "MaxLines") {
        return targetType == "TextBlock" || targetType == "TextBox";
    }
    if (prop == "TextTrimming" || prop == "LineHeight" || prop == "CharacterSpacing") {
        return targetType == "TextBlock";
    }
    if (prop == "IsReadOnly" || prop == "SelectionOpacity") {
        return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
    }
    if (prop == "Placeholder") {
        return targetType == "TextBoxBase" || targetType == "TextBox" ||
               targetType == "PasswordBox" || targetType == "ComboBox";
    }
    if (prop == "Password" || prop == "PasswordChar") return targetType == "PasswordBox";
    if (prop == "Source") {
        return targetType == "Image" || targetType == "RiveControl" || targetType == "MediaElement";
    }
    if (prop == "Data") return targetType == "Path";
    if (prop == "Fill") {
        return IsXamlShapeType(targetType) || targetType == "TickBar" || targetType == "RadialNeedle";
    }
    if (prop == "Stroke") {
        return IsXamlShapeType(targetType) || targetType == "TextBlock" || targetType == "RadialTicks";
    }
    if (prop == "StrokeThickness") {
        return IsXamlShapeType(targetType) || targetType == "TextBlock";
    }
    if (prop == "StrokeDashArray" || prop == "StrokeDashCap" ||
        prop == "StrokeStartLineCap" || prop == "StrokeEndLineCap") {
        return IsXamlShapeType(targetType);
    }
    if (prop == "CaretBrush") {
        return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
    }
    if (prop == "RadiusX" || prop == "RadiusY") return targetType == "Rectangle";
    if (prop == "X1" || prop == "Y1" || prop == "X2" || prop == "Y2") return targetType == "Line";
    if (prop == "Minimum" || prop == "Maximum") {
        return IsXamlRangeBaseType(targetType) || targetType == "Track" || targetType == "TickBar" ||
               targetType == "RadialTicks" || targetType == "RadialNeedle";
    }
    if (prop == "Value") {
        return IsXamlRangeBaseType(targetType) || targetType == "Track" || targetType == "RadialNeedle";
    }
    if (prop == "SmallChange" || prop == "LargeChange") return IsXamlRangeBaseType(targetType);
    if (prop == "IsDirectionReversed") {
        return targetType == "Slider" || targetType == "ScrollBar" ||
               targetType == "Track" || targetType == "TickBar";
    }
    if (prop == "ViewportSize") return targetType == "ScrollBar" || targetType == "Track";
    if (prop == "IsChecked") return IsXamlToggleButtonType(targetType) || targetType == "MenuItem";
    if (prop == "IsThreeState") return IsXamlToggleButtonType(targetType);
    if (prop == "IsExpanded") return targetType == "Expander" || targetType == "TreeViewItem";
    if (prop == "ExpandDirection") return targetType == "Expander";
    if (prop == "IsSelected") return IsXamlSelectableItemType(targetType);
    if (prop == "SelectedIndex" || prop == "SelectionMode") return IsXamlSelectorType(targetType);
    if (prop == "TabStripPlacement") return targetType == "TabControl";
    if (prop == "HorizontalScrollBarVisibility" || prop == "VerticalScrollBarVisibility") {
        return targetType == "ScrollViewer";
    }
    if (prop == "IsCheckable") return targetType == "MenuItem";
    if (prop == "MaxDropDownHeight") return targetType == "ComboBox";
    if (prop == "Volume" || prop == "IsMuted" || prop == "LoadedBehavior") {
        return targetType == "MediaElement";
    }
    if (prop == "TickLength" || prop == "TickWidth" || prop == "MinAngle" || prop == "MaxAngle") {
        return targetType == "RadialTicks" || targetType == "RadialNeedle";
    }
    if (prop == "Brush") return targetType == "RadialTicks";
    if (prop == "Year" || prop == "Month" || prop == "Day" ||
        prop == "Hour" || prop == "Minute" || prop == "Second" ||
        prop == "Format" || prop == "UseSystemTime") {
        return targetType == "DateTimeControl";
    }
    return false;
}

bool IsXamlFloatPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal) {
    if (!IsXamlFloatProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsXamlKnownUIElementType(targetType) || IsXamlNonUIElementType(targetType))) {
        if (IsXamlNonUIElementType(targetType)) {
            if (targetType == "Inline" || targetType == "Run" || targetType == "LineBreak") {
                return prop == "FontSize";
            }
            if (targetType == "GridViewColumn") return prop == "Width";
            if (targetType == "RowDefinition") return prop == "MinHeight" || prop == "MaxHeight";
            if (targetType == "ColumnDefinition") return prop == "MinWidth" || prop == "MaxWidth";
            if (targetType == "TranslateTransform") return false;
            if (targetType == "ScaleTransform") {
                return prop == "ScaleX" || prop == "ScaleY" || prop == "CenterX" || prop == "CenterY";
            }
            if (targetType == "RotateTransform") {
                return prop == "Angle" || prop == "CenterX" || prop == "CenterY";
            }
            if (targetType == "SkewTransform") {
                return prop == "AngleX" || prop == "AngleY" || prop == "CenterX" || prop == "CenterY";
            }
            if (targetType == "Effect" || targetType == "BlurEffect" ||
                targetType == "DropShadowEffect" || targetType == "BackdropBlurEffect" ||
                targetType == "TintEffect" || targetType == "PixelateEffect" ||
                targetType == "ChromaticAberrationEffect" || targetType == "DirectionalBlurEffect") {
                return prop == "Radius" || prop == "Direction" || prop == "Opacity" ||
                       prop == "BlurRadius" || prop == "ShadowDepth";
            }
            if (targetType == "RadialGradientBrush") {
                return prop == "CenterX" || prop == "CenterY" || prop == "RadiusX" ||
                       prop == "RadiusY" || prop == "Opacity";
            }
            return prop == "Opacity";
        }
        if (prop == "Width" || prop == "Height" || prop == "MinWidth" ||
            prop == "MaxWidth" || prop == "MinHeight" || prop == "MaxHeight" || prop == "Opacity") {
            return true;
        }
        if (prop == "FontSize") {
            return IsXamlControlType(targetType) || targetType == "TextBlock" ||
                   targetType == "ContentPresenter" || targetType == "ScrollContentPresenter";
        }
        if (prop == "Spacing") {
            return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
                   targetType == "VirtualizingUniformGrid" || targetType == "VirtualizingWrapGrid" ||
                   targetType == "TabPanel" || targetType == "ToolBarPanel" ||
                   targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
                   targetType == "UniformGrid" || targetType == "RadialTicks";
        }
        if (prop == "ItemWidth" || prop == "ItemHeight") {
            return targetType == "WrapPanel" || targetType == "VirtualizingWrapGrid";
        }
        if (prop == "Minimum" || prop == "Maximum") {
            return IsXamlRangeBaseType(targetType) || targetType == "Track" || targetType == "TickBar" ||
                   targetType == "RadialTicks" || targetType == "RadialNeedle";
        }
        if (prop == "Value") {
            return IsXamlRangeBaseType(targetType) || targetType == "Track" || targetType == "RadialNeedle";
        }
        if (prop == "SmallChange" || prop == "LargeChange") return IsXamlRangeBaseType(targetType);
        if (prop == "StrokeThickness") return IsXamlShapeType(targetType) || targetType == "TextBlock";
        if (prop == "RadiusX" || prop == "RadiusY") return targetType == "Rectangle";
        if (prop == "X1" || prop == "Y1" || prop == "X2" || prop == "Y2") return targetType == "Line";
        if (prop == "TickLength" || prop == "TickWidth" || prop == "MinAngle" || prop == "MaxAngle") {
            return targetType == "RadialTicks" || targetType == "RadialNeedle";
        }
        if (prop == "Volume") return targetType == "MediaElement";
        if (prop == "LineHeight" || prop == "CharacterSpacing") return targetType == "TextBlock";
        if (prop == "ViewportSize") return targetType == "ScrollBar" || targetType == "Track";
        if (prop == "MaxDropDownHeight") return targetType == "ComboBox";
        if (prop == "HorizontalOffset" || prop == "VerticalOffset") {
            return targetType == "Popup" || targetType == "ScrollViewer";
        }
        if (prop == "BlurRadius" || prop == "ShadowDepth" || prop == "Direction" ||
            prop == "OffsetX" || prop == "OffsetY" || prop == "Spread") {
            return targetType == "BoxShadow";
        }
        return false;
    }

    if (prop == "Value" || prop == "Direction" || prop == "Radius" ||
        prop == "Angle" || prop == "Volume" || prop == "Spacing") {
        if (!rawVal.empty()) {
            float parsed = 0.0f;
            return aero::str::TryParseFloat32(rawVal, parsed);
        }
        return false;
    }
    return true;
}

bool IsXamlIntPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal) {
    if (!IsXamlIntProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsXamlKnownUIElementType(targetType) || IsXamlNonUIElementType(targetType))) {
        if (prop == "ZIndex") return !IsXamlNonUIElementType(targetType);
        if (prop == "Rows" || prop == "Columns" || prop == "FirstColumn") {
            return targetType == "UniformGrid" || targetType == "VirtualizingUniformGrid";
        }
        if (prop == "MaxLines") return targetType == "TextBlock" || targetType == "TextBox";
        if (prop == "MaxLength") {
            return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
        }
        if (prop == "TabIndex") return !IsXamlNonUIElementType(targetType);
        if (prop == "SelectedIndex") return IsXamlSelectorType(targetType);
        if (prop == "Year" || prop == "Month" || prop == "Day" ||
            prop == "Hour" || prop == "Minute" || prop == "Second") {
            return targetType == "DateTimeControl";
        }
        return false;
    }
    if ((prop == "Rows" || prop == "Columns" || prop == "FirstColumn") && !rawVal.empty()) {
        const std::string_view s = aero::str::Trim(rawVal);
        if (s.empty()) return false;
        std::size_t idx = (s.front() == '-') ? 1u : 0u;
        if (idx >= s.size()) return false;
        for (; idx < s.size(); ++idx) {
            if (!std::isdigit(static_cast<unsigned char>(s[idx]))) return false;
        }
    }
    return true;
}

bool IsXamlBoolPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal) {
    if (!IsXamlBoolProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsXamlKnownUIElementType(targetType) || IsXamlNonUIElementType(targetType))) {
        if (IsXamlNonUIElementType(targetType)) return false;
        if (prop == "IsEnabled" || prop == "IsHitTestVisible" || prop == "Focusable" ||
            prop == "ClipToBounds" || prop == "IsMouseOver" || prop == "IsPressed" ||
            prop == "IsFocused" || prop == "IsKeyboardFocused") {
            return true;
        }
        if (prop == "IsChecked") return IsXamlToggleButtonType(targetType) || targetType == "MenuItem";
        if (prop == "IsThreeState") return IsXamlToggleButtonType(targetType);
        if (prop == "IsExpanded") return targetType == "Expander" || targetType == "TreeViewItem";
        if (prop == "IsSelected") return IsXamlSelectableItemType(targetType);
        if (prop == "IsReadOnly") {
            return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
        }
        if (prop == "LastChildFill") return targetType == "DockPanel";
        if (prop == "IsOpen") return targetType == "Popup" || targetType == "ContextMenu";
        if (prop == "StaysOpen") return targetType == "Popup";
        if (prop == "IsCheckable") return targetType == "MenuItem";
        if (prop == "IsDefault" || prop == "IsCancel") {
            return targetType == "Button" || targetType == "RepeatButton";
        }
        if (prop == "ShowGridLines") return targetType == "Grid";
        if (prop == "IsEditable" || prop == "IsDropDownOpen") return targetType == "ComboBox";
        if (prop == "IsMuted") return targetType == "MediaElement";
        if (prop == "ShowDate" || prop == "ShowTime" || prop == "UseSystemTime") {
            return targetType == "DateTimeControl";
        }
        if (prop == "IsDirectionReversed") {
            return targetType == "Slider" || targetType == "ScrollBar" ||
                   targetType == "Track" || targetType == "TickBar";
        }
        if (prop == "IsTabStop") return IsXamlControlType(targetType);
        if (prop == "IsDragging") return targetType == "Thumb" || targetType == "GridSplitter";
        if (prop == "HasItems") return IsXamlItemsControlType(targetType);
        if (prop == "CanContentScroll") {
            return targetType == "ScrollViewer" || targetType == "ScrollContentPresenter";
        }
        if (prop == "Inside") return targetType == "BoxShadow";
        if (prop == "IsItemsHost") return IsXamlPanelType(targetType);
        return false;
    }
    if (!rawVal.empty()) {
        const std::string_view s = aero::str::Trim(rawVal);
        return aero::str::EqualsIgnoreCase(s, "True") || aero::str::EqualsIgnoreCase(s, "False");
    }
    return true;
}

void ExpandXamlControlHierarchy(std::unordered_set<std::string>& usedTypes) {
    auto add = [&](std::string_view name) -> bool {
        return usedTypes.emplace(name).second;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        const auto has = [&](std::string_view n) -> bool {
            return usedTypes.count(std::string(n)) > 0u;
        };
        if (has("Button") || has("RepeatButton") || has("ToggleButton") ||
            has("CheckBox") || has("RadioButton")) {
            changed |= add("ButtonBase");
        }
        if (has("Window") || has("Page") || has("UserControl") || has("ButtonBase") ||
            has("Label") || has("ToolTip") || has("ScrollViewer") || has("Frame") ||
            has("ListBoxItem") || has("ListViewItem") || has("ComboBoxItem") ||
            has("TabItem") || has("StatusBarItem") || has("GridViewColumnHeader")) {
            changed |= add("ContentControl");
        }
        if (has("GroupBox") || has("Expander")) {
            changed |= add("HeaderedContentControl");
            changed |= add("ContentControl");
        }
        if (has("TreeViewItem") || has("MenuItem") || has("ToolBar")) {
            changed |= add("HeaderedItemsControl");
            changed |= add("ItemsControl");
        }
        if (has("ListBox") || has("ListView") || has("ComboBox") || has("TabControl")) {
            changed |= add("Selector");
            changed |= add("ItemsControl");
        }
        if (has("TreeView") || has("Menu") || has("ContextMenu") || has("StatusBar")) {
            changed |= add("ItemsControl");
        }
        if (has("Slider") || has("ProgressBar") || has("ScrollBar")) {
            changed |= add("RangeBase");
            changed |= add("Control");
        }
        if (has("TextBox") || has("RichTextBox") || has("PasswordBox")) {
            changed |= add("TextBoxBase");
            changed |= add("Control");
        }
        if (has("ContentControl") || has("ItemsControl") || has("Thumb") || has("Separator")) {
            changed |= add("Control");
            changed |= add("FrameworkElement");
            changed |= add("UIElement");
        }
        if (has("Expander")) {
            changed |= add("ToggleButton");
        }
        if (has("ListBox")) {
            changed |= add("ListBoxItem");
            changed |= add("ScrollViewer");
        }
        if (has("ListView")) {
            changed |= add("ListBoxItem");
            changed |= add("ListViewItem");
            changed |= add("ScrollViewer");
            changed |= add("GridViewColumnHeader");
            changed |= add("Thumb");
        }
        if (has("ComboBox")) {
            changed |= add("ComboBoxItem");
            changed |= add("ToggleButton");
            changed |= add("Popup");
            changed |= add("ScrollViewer");
            changed |= add("TextBox");
        }
        if (has("TreeView")) {
            changed |= add("TreeViewItem");
            changed |= add("ScrollViewer");
            changed |= add("ToggleButton");
        }
        if (has("TabControl")) {
            changed |= add("TabItem");
        }
        if (has("Menu") || has("ContextMenu")) {
            changed |= add("MenuItem");
            changed |= add("Separator");
            changed |= add("Popup");
        }
        if (has("ToolBar")) {
            changed |= add("ToggleButton");
            changed |= add("Button");
            changed |= add("Separator");
            changed |= add("Thumb");
        }
        if (has("StatusBar")) {
            changed |= add("StatusBarItem");
            changed |= add("Separator");
        }
        if (has("ScrollViewer") || has("TextBox") || has("PasswordBox")) {
            changed |= add("ScrollViewer");
            changed |= add("ScrollBar");
            changed |= add("Thumb");
            changed |= add("RepeatButton");
        }
        if (has("Slider") || has("ScrollBar")) {
            changed |= add("Thumb");
            changed |= add("RepeatButton");
        }
        if (has("ViewportSurface") || has("DockPane")) {
            changed |= add("ContentControl");
            changed |= add("Grid");
            changed |= add("Border");
            changed |= add("Image");
            changed |= add("TextBlock");
            changed |= add("CustomDrawElement");
        }
        if (has("DockingWorkspace") || has("DockSplitNode") || has("DockTabGroup")) {
            changed |= add("DockSplitNode");
            changed |= add("DockTabGroup");
            changed |= add("DockPane");
            changed |= add("Control");
            changed |= add("Grid");
            changed |= add("Border");
            changed |= add("StackPanel");
            changed |= add("TextBlock");
            changed |= add("Thumb");
        }
        if (has("PropertyGrid") || has("ColorPicker") || has("Vector3Field") || has("Vector2Field") || has("NumericScrubber")) {
            changed |= add("NumericScrubber");
            changed |= add("Vector2Field");
            changed |= add("Vector3Field");
            changed |= add("ColorPicker");
            changed |= add("Control");
            changed |= add("Grid");
            changed |= add("Border");
            changed |= add("StackPanel");
            changed |= add("ScrollViewer");
            changed |= add("TextBlock");
            changed |= add("TextBox");
            changed |= add("CheckBox");
            changed |= add("Button");
        }
        if (has("CodeEditorView") || has("NodeGraphCanvas")) {
            changed |= add("Control");
            changed |= add("CustomDrawElement");
        }
    }
}

} // namespace aero
