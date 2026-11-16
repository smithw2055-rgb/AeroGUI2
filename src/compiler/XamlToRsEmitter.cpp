#include "aero/compiler/XamlToRsEmitter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "aero/compiler/MarkupExtensionParser.hpp"
#include "aero/compiler/XmlPullLexer.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/drawing/Geometry.hpp"

namespace aero {
namespace {

struct XamlNode final {
    std::string rawName;
    std::string prefix;
    std::string localName;
    std::string namespaceUri;
    std::vector<XmlAttribute> attributes;
    std::vector<std::unique_ptr<XamlNode>> children;
    std::unordered_map<std::string, std::unique_ptr<XamlNode>> propertyElements;
    std::string textContent;
    int line = 1;
    int column = 1;

    [[nodiscard]] const XmlAttribute* FindAttribute(std::string_view name) const noexcept {
        for (const auto& attr : attributes) {
            if (attr.isXmlns) {
                continue;
            }
            if (attr.rawName == name || attr.localName == name) {
                return &attr;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const XmlAttribute* FindXAttribute(std::string_view local) const noexcept {
        for (const auto& attr : attributes) {
            if (attr.isXmlns) {
                continue;
            }
            if (attr.prefix == "x" && attr.localName == local) {
                return &attr;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string GetKey() const {
        if (const auto* xk = FindXAttribute("Key")) {
            return xk->value;
        }
        if (const auto* k = FindAttribute("Key")) {
            return k->value;
        }
        return {};
    }

    [[nodiscard]] std::string GetName() const {
        if (const auto* xn = FindXAttribute("Name")) {
            return xn->value;
        }
        if (const auto* n = FindAttribute("Name")) {
            return n->value;
        }
        return {};
    }

    [[nodiscard]] const XamlNode* FindPropertyElement(
        std::string_view ownerType,
        std::string_view propName) const noexcept {
        const std::string qualified = std::string(ownerType) + "." + std::string(propName);
        if (const auto it = propertyElements.find(qualified); it != propertyElements.end()) {
            return it->second.get();
        }
        if (const auto it = propertyElements.find(std::string(propName)); it != propertyElements.end()) {
            return it->second.get();
        }
        return nullptr;
    }
};

[[nodiscard]] bool IsDesignOrMarkupCompatPrefix(std::string_view prefix, std::string_view nsUri) {
    if (nsUri == "http://schemas.microsoft.com/expression/blend/2008" ||
        nsUri == "http://schemas.openxmlformats.org/markup-compatibility/2006") {
        return true;
    }
    return prefix == "d" || prefix == "mc";
}

[[nodiscard]] bool ShouldIgnoreXmlElement(const XamlNode& node) {
    return IsDesignOrMarkupCompatPrefix(node.prefix, node.namespaceUri);
}

[[nodiscard]] bool BuildXamlDom(
    std::string_view xmlContent,
    std::unique_ptr<XamlNode>& outRoot,
    std::vector<std::string>& outDiagnostics) {
    XmlPullLexer lexer(xmlContent, false);
    std::vector<XamlNode*> openStack;
    outRoot.reset();

    for (;;) {
        XmlToken tok = lexer.Next();
        if (tok.kind == XmlTokenKind::Error) {
            outDiagnostics.push_back(
                "[" + std::to_string(tok.line) + ":" + std::to_string(tok.column) + "] " + tok.text);
            return false;
        }
        if (tok.kind == XmlTokenKind::EndOfFile) {
            break;
        }
        if (tok.kind == XmlTokenKind::Text || tok.kind == XmlTokenKind::CData) {
            if (!openStack.empty()) {
                XamlNode* cur = openStack.back();
                const std::string_view trimmed = aero::str::Trim(tok.text);
                if (!trimmed.empty() || cur->localName == "Run" || cur->localName == "TextBlock") {
                    if (tok.kind == XmlTokenKind::CData) {
                        cur->textContent += tok.text;
                    } else if (!trimmed.empty()) {
                        if (!cur->textContent.empty()) {
                            cur->textContent.push_back(' ');
                        }
                        cur->textContent.append(trimmed);
                    }
                }
            }
            continue;
        }
        if (tok.kind == XmlTokenKind::StartElement) {
            auto node = std::make_unique<XamlNode>();
            node->rawName = std::move(tok.rawName);
            node->prefix = std::move(tok.prefix);
            node->localName = std::move(tok.localName);
            node->namespaceUri = std::move(tok.namespaceUri);
            node->attributes = std::move(tok.attributes);
            node->line = tok.line;
            node->column = tok.column;

            XamlNode* rawPtr = node.get();
            if (openStack.empty()) {
                outRoot = std::move(node);
            } else {
                XamlNode* parent = openStack.back();
                const std::size_t dotPos = rawPtr->localName.find('.');
                if (dotPos != std::string::npos) {
                    const std::string propOnly = rawPtr->localName.substr(dotPos + 1);
                    const std::string fullKey = rawPtr->localName;
                    parent->propertyElements[fullKey] = std::move(node);
                    // Preserve unprefixed prop lookup too when owner matches parent
                    if (rawPtr->localName.substr(0, dotPos) == parent->localName &&
                        parent->propertyElements.count(propOnly) == 0) {
                        // Lookup checks both Owner.Prop and Prop directly via raw key
                    }
                } else {
                    parent->children.push_back(std::move(node));
                }
            }

            if (!tok.isEmptyElement) {
                openStack.push_back(rawPtr);
            }
            continue;
        }
        if (tok.kind == XmlTokenKind::EndElement) {
            if (!openStack.empty()) {
                openStack.pop_back();
            }
        }
    }

    if (!outRoot) {
        outDiagnostics.push_back("Empty XAML document: no root element found");
        return false;
    }
    return true;
}

[[nodiscard]] std::string EscapeRsString(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char ch : s) {
        switch (ch) {
        case '\\': out.append("\\\\"); break;
        case '"': out.append("\\\""); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default: out.push_back(ch); break;
        }
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string FormatFloatLiteral(float value) {
    char buf[96]{};
    const double d = static_cast<double>(value);
    if (!(d > -1e15 && d < 1e15)) {
        return "0.0f";
    }
    std::snprintf(buf, sizeof(buf), "%.6f", d);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.size() >= 2 && s.back() == '0' && s[s.size() - 2] != '.') {
            s.pop_back();
        }
    }
    if (s == "-0.0") {
        s = "0.0";
    }
    s.push_back('f');
    return s;
}

[[nodiscard]] bool TryParseFloatList(std::string_view raw, std::vector<float>& outValues) {
    outValues.clear();
    std::size_t i = 0;
    const std::size_t n = raw.size();
    while (i < n) {
        while (i < n && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == ',' ||
                         raw[i] == '\r' || raw[i] == '\n')) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        const std::size_t start = i;
        while (i < n && raw[i] != ' ' && raw[i] != '\t' && raw[i] != ',' &&
               raw[i] != '\r' && raw[i] != '\n') {
            ++i;
        }
        float v = 0.0f;
        if (!aero::str::TryParseFloat32(raw.substr(start, i - start), v)) {
            return false;
        }
        outValues.push_back(v);
    }
    return !outValues.empty();
}

[[nodiscard]] std::string FormatThicknessExpr(std::string_view raw) {
    std::vector<float> vals;
    if (!TryParseFloatList(raw, vals)) {
        return "new Thickness(0.0f)";
    }
    if (vals.size() == 1) {
        return "new Thickness(" + FormatFloatLiteral(vals[0]) + ")";
    }
    if (vals.size() == 2) {
        return "new Thickness(" + FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ", " + FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ")";
    }
    if (vals.size() >= 4) {
        return "new Thickness(" + FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ", " + FormatFloatLiteral(vals[2]) + ", " +
               FormatFloatLiteral(vals[3]) + ")";
    }
    return "new Thickness(0.0f)";
}

[[nodiscard]] std::string FormatCornerRadiusExpr(std::string_view raw) {
    std::vector<float> vals;
    if (!TryParseFloatList(raw, vals)) {
        return "new CornerRadius(0.0f)";
    }
    if (vals.size() == 1) {
        return "new CornerRadius(" + FormatFloatLiteral(vals[0]) + ")";
    }
    if (vals.size() >= 4) {
        return "new CornerRadius(" + FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ", " + FormatFloatLiteral(vals[2]) + ", " +
               FormatFloatLiteral(vals[3]) + ")";
    }
    return "new CornerRadius(" + FormatFloatLiteral(vals[0]) + ")";
}

[[nodiscard]] std::pair<std::string, std::string> ParseGridLength(std::string_view raw) {
    const std::string_view trimmed = aero::str::Trim(raw);
    if (trimmed.empty() || aero::str::EqualsIgnoreCase(trimmed, "Auto")) {
        return {"GridUnitType.Auto", "1.0f"};
    }
    if (trimmed.back() == '*') {
        const std::string_view numPart = aero::str::Trim(trimmed.substr(0, trimmed.size() - 1));
        float factor = 1.0f;
        if (!numPart.empty()) {
            (void)aero::str::TryParseFloat32(numPart, factor);
        }
        return {"GridUnitType.Star", FormatFloatLiteral(factor)};
    }
    float px = 0.0f;
    if (aero::str::TryParseFloat32(trimmed, px)) {
        return {"GridUnitType.Pixel", FormatFloatLiteral(px)};
    }
    return {"GridUnitType.Auto", "1.0f"};
}

[[nodiscard]] float ParseDurationSeconds(std::string_view raw) {
    std::string_view s = aero::str::Trim(raw);
    if (s.empty() || aero::str::EqualsIgnoreCase(s, "Automatic") ||
        aero::str::EqualsIgnoreCase(s, "Forever")) {
        return 0.3f;
    }
    if (aero::str::EndsWith(s, "ms")) {
        float ms = 0.0f;
        if (aero::str::TryParseFloat32(s.substr(0, s.size() - 2), ms)) {
            return ms * 0.001f;
        }
    }
    if (s.back() == 's' || s.back() == 'S') {
        float sec = 0.0f;
        if (aero::str::TryParseFloat32(s.substr(0, s.size() - 1), sec)) {
            return sec;
        }
    }
    // Handle optional [d.]hh:mm:ss.fff (e.g. KeyTime="100.0:0:0")
    const std::size_t firstColon = s.find(':');
    if (firstColon != std::string_view::npos) {
        const std::size_t dotBeforeColon = s.substr(0, firstColon).find('.');
        if (dotBeforeColon != std::string_view::npos) {
            s = s.substr(dotBeforeColon + 1);
        }
    }
    std::vector<float> parts;
    std::size_t start = 0;
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
        start = colon + 1;
    }
    if (parts.size() == 1) return parts[0];
    if (parts.size() == 2) return parts[0] * 60.0f + parts[1];
    if (parts.size() >= 3) return parts[0] * 3600.0f + parts[1] * 60.0f + parts[2];
    return 0.3f;
}

[[nodiscard]] std::string NormalizeTargetPropertyPath(std::string_view raw) {
    std::string_view s = aero::str::Trim(raw);
    if (s.empty()) { return "Opacity"; }
    if (s == "Opacity" || s == "Width" || s == "Height" || s == "MinWidth" ||
        s == "MinHeight" || s == "Fill.Color" || s == "Stroke.Color" ||
        s == "Background.Color" || s == "Foreground.Color") {
        return std::string(s);
    }
    if (s.find("SolidColorBrush.Color") != std::string_view::npos) {
        if (s.find("Fill") != std::string_view::npos) return "Fill.Color";
        if (s.find("Stroke") != std::string_view::npos) return "Stroke.Color";
        if (s.find("Foreground") != std::string_view::npos) return "Foreground.Color";
        if (s.find("BorderBrush") != std::string_view::npos) return "BorderBrush.Color";
        return "Background.Color";
    }
    if (s.find("Brush.Opacity") != std::string_view::npos) {
        return "Opacity";
    }
    if (s.find("BlurEffect.Radius") != std::string_view::npos || s == "Radius" || s == "BlurRadius") {
        return "EffectRadius";
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
    // Strip wrapping parentheses such as `(FrameworkElement.Width)` or `(UIElement.Opacity)`
    if (s.front() == '(' && s.back() == ')' && s.size() > 2) {
        std::string_view inner = s.substr(1, s.size() - 2);
        const std::size_t dot = inner.rfind('.');
        if (dot != std::string_view::npos) {
            return std::string(inner.substr(dot + 1));
        }
        return std::string(inner);
    }
    return std::string(s);
}

[[nodiscard]] int MapEasingKind(std::string_view name) {
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
    return 0; // Linear
}

[[nodiscard]] int MapEasingMode(std::string_view mode) {
    if (aero::str::EqualsIgnoreCase(mode, "EaseIn")) return 1;
    if (aero::str::EqualsIgnoreCase(mode, "EaseInOut")) return 2;
    return 0; // EaseOut
}

[[nodiscard]] int MapBindingMode(std::string_view mode) {
    if (aero::str::EqualsIgnoreCase(mode, "TwoWay")) return 1;
    if (aero::str::EqualsIgnoreCase(mode, "OneTime")) return 2;
    if (aero::str::EqualsIgnoreCase(mode, "OneWayToSource")) return 3;
    return 0; // Default / OneWay
}

[[nodiscard]] std::string StripTargetTypeName(std::string_view raw) {
    std::string s(aero::str::Trim(raw));
    if (LooksLikeMarkupExtension(s)) {
        const auto ext = ParseMarkupExtension(s);
        if (ext.kind == MarkupExtensionKind::XType) {
            if (const auto* pos0 = ext.FindPositional(0)) {
                s = pos0->literal;
            } else if (const auto* tn = ext.FindNamed("TypeName")) {
                s = tn->literal;
            }
        }
    }
    const std::size_t colon = s.find(':');
    if (colon != std::string::npos) {
        s = s.substr(colon + 1);
    }
    return s;
}

[[nodiscard]] std::string ResolveResourceKeyFromExtension(const MarkupExtensionNode& ext) {
    auto unwrap = [](const MarkupArgValue& val) -> std::string {
        if (!val.isExtension || !val.extension) {
            if (LooksLikeMarkupExtension(val.literal)) {
                return StripTargetTypeName(val.literal);
            }
            return val.literal;
        }
        const auto& nested = *val.extension;
        if (nested.kind == MarkupExtensionKind::XType) {
            if (const auto* t0 = nested.FindPositional(0)) {
                return StripTargetTypeName(t0->literal);
            }
            if (const auto* tn = nested.FindNamed("TypeName")) {
                return StripTargetTypeName(tn->literal);
            }
        }
        if (const auto* n0 = nested.FindPositional(0)) {
            return n0->literal;
        }
        return {};
    };
    if (const auto* p0 = ext.FindPositional(0)) {
        return unwrap(*p0);
    }
    if (const auto* rk = ext.FindNamed("ResourceKey")) {
        return unwrap(*rk);
    }
    return {};
}

[[nodiscard]] std::string NormalizePackSourcePath(std::string_view raw) {
    std::string_view s = aero::str::Trim(raw);
    constexpr std::string_view kComp = ";component/";
    const std::size_t compPos = s.find(kComp);
    if (compPos != std::string_view::npos) {
        s = s.substr(compPos + kComp.size());
    } else if (aero::str::StartsWith(s, "/")) {
        s.remove_prefix(1);
    }
    std::string clean(s);
    std::replace(clean.begin(), clean.end(), '\\', '/');
    return clean;
}

[[nodiscard]] std::string SanitizeIdentifier(std::string_view raw, std::string_view fallback) {
    std::string out;
    for (unsigned char ch : raw) {
        if (std::isalnum(ch) != 0 || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0) {
        out = std::string(fallback) + (out.empty() ? std::string{} : "_" + out);
    }
    return out;
}

[[nodiscard]] std::string FormatUintHexLiteral(std::uint32_t rgba) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "0x%08Xu", rgba);
    return std::string(buf);
}

[[nodiscard]] const char* LookupEnumType(std::string_view propName) {
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

[[nodiscard]] bool IsFloatProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kFloatProps = {
        "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight",
        "FontSize", "Opacity", "Spacing", "Minimum", "Maximum", "Value",
        "SmallChange", "LargeChange", "StrokeThickness", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "CenterX", "CenterY", "Radius", "BlurRadius",
        "ShadowDepth", "Direction", "Angle", "AngleX", "AngleY", "ScaleX", "ScaleY",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Volume",
        "LineHeight", "CharacterSpacing"
    };
    return kFloatProps.count(prop) > 0;
}

[[nodiscard]] bool IsIntProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kIntProps = {
        "ZIndex", "MaxLines", "TabIndex", "SelectedIndex", "Rows", "Columns", "MaxLength",
        "Year", "Month", "Day", "Hour", "Minute", "Second"
    };
    return kIntProps.count(prop) > 0;
}

[[nodiscard]] bool IsBoolProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kBoolProps = {
        "IsEnabled", "IsHitTestVisible", "Focusable", "ClipToBounds", "IsChecked",
        "IsThreeState", "IsExpanded", "IsSelected", "IsReadOnly", "LastChildFill",
        "IsOpen", "IsCheckable", "IsDefault", "IsCancel", "ShowGridLines",
        "StaysOpen", "IsEditable", "IsMuted", "ShowDate", "ShowTime", "UseSystemTime"
    };
    return kBoolProps.count(prop) > 0;
}

[[nodiscard]] bool IsThicknessProperty(std::string_view prop) {
    return prop == "Margin" || prop == "Padding" || prop == "BorderThickness";
}

[[nodiscard]] bool IsBrushProperty(std::string_view prop) {
    return prop == "Background" || prop == "Foreground" || prop == "BorderBrush" ||
           prop == "Fill" || prop == "Stroke" || prop == "CaretBrush" ||
           prop == "SelectionBrush" || prop == "Brush";
}

[[nodiscard]] bool IsEventAttribute(std::string_view prop) {
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

[[nodiscard]] bool IsDirectUIProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kProps = {
        "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight",
        "Margin", "Padding", "HorizontalAlignment", "VerticalAlignment",
        "HorizontalContentAlignment", "VerticalContentAlignment",
        "Visibility", "Opacity", "IsEnabled", "IsHitTestVisible", "Focusable",
        "ClipToBounds", "ZIndex", "Background", "Foreground", "BorderBrush",
        "BorderThickness", "CornerRadius", "Text", "FontSize", "FontFamily", "FontWeight",
        "TextWrapping", "TextAlignment", "TextTrimming", "Orientation", "Spacing",
        "Stretch", "StretchDirection", "ExpandDirection", "Source", "Data",
        "Fill", "Stroke", "StrokeThickness", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "Minimum", "Maximum", "Value", "SmallChange",
        "LargeChange", "IsChecked", "IsThreeState", "IsExpanded", "IsSelected",
        "IsReadOnly", "LastChildFill", "SelectedIndex", "SelectionMode",
        "Placeholder", "IsCheckable", "TabStripPlacement",
        "HorizontalScrollBarVisibility", "VerticalScrollBarVisibility",
        "RenderTransform", "Effect", "Style", "ShowGridLines", "Rows", "Columns",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Brush",
        "Volume", "IsMuted", "LoadedBehavior", "Password", "PasswordChar",
        "Year", "Month", "Day", "Hour", "Minute", "Second", "Format", "UseSystemTime",
        "LineHeight", "CharacterSpacing", "MaxLines"
    };
    return kProps.count(prop) > 0;
}

[[nodiscard]] bool IsContentControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kCC = {
        "ContentControl", "HeaderedContentControl", "Window", "Page",
        "UserControl", "ScrollViewer", "ButtonBase", "Button",
        "RepeatButton", "ToggleButton", "CheckBox", "RadioButton", "GroupBox",
        "Expander", "TabItem", "MenuItem", "ListBoxItem", "ComboBoxItem",
        "ListViewItem", "StatusBarItem", "ToolTip", "Label"
    };
    return kCC.count(typeName) > 0;
}

[[nodiscard]] bool IsItemsControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kIC = {
        "ItemsControl", "HeaderedItemsControl", "Selector", "ListBox", "ListView",
        "ComboBox", "TabControl", "Menu", "MenuItem", "ContextMenu",
        "ToolBar", "StatusBar", "TreeView", "TreeViewItem"
    };
    return kIC.count(typeName) > 0;
}

[[nodiscard]] std::string ReadFileTextUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

[[nodiscard]] std::string ExtractClrNamespace(std::string_view uri) {
    if (!aero::str::StartsWith(uri, "clr-namespace:")) {
        return {};
    }
    std::string_view rest = uri.substr(14);
    const std::size_t semi = rest.find(';');
    if (semi != std::string_view::npos) {
        rest = rest.substr(0, semi);
    }
    return std::string(aero::str::Trim(rest));
}

[[nodiscard]] bool IsIgnoredExternalClrNamespace(std::string_view ns) {
    return ns.empty() ||
           aero::str::StartsWith(ns, "System") ||
           aero::str::StartsWith(ns, "Microsoft") ||
           aero::str::StartsWith(ns, "Windows") ||
           aero::str::StartsWith(ns, "Noesis");
}

class XamlEmitter final {
public:
    explicit XamlEmitter(const XamlEmitOptions& opts) : opts_(opts) {}

    [[nodiscard]] XamlEmitResult Run(std::string_view xamlContent) {
        XamlEmitResult res;
        res.generatedFilePath = MakeGeneratedRsPath(opts_.xamlPath);

        std::unique_ptr<XamlNode> root;
        if (!BuildXamlDom(xamlContent, root, diagnostics_)) {
            res.diagnostics = diagnostics_;
            return res;
        }

        // Determine class & module
        std::string xClass;
        if (const auto* xc = root->FindXAttribute("Class")) {
            xClass = xc->value;
        }
        std::string xDataType;
        if (const auto* xdt = root->FindXAttribute("DataType")) {
            xDataType = StripTargetTypeName(xdt->value);
        } else if (const auto* ddc = root->FindAttribute("DataContext")) {
            if (ddc->prefix == "d" && !LooksLikeMarkupExtension(ddc->value)) {
                xDataType = StripTargetTypeName(ddc->value);
            }
        }
        res.dataType = xDataType;

        if (!xClass.empty()) {
            const std::size_t dot = xClass.rfind('.');
            if (dot != std::string::npos) {
                moduleName_ = xClass.substr(0, dot);
                shortClassName_ = xClass.substr(dot + 1);
            } else {
                moduleName_ = !opts_.defaultModuleName.empty()
                    ? opts_.defaultModuleName
                    : DeriveModuleNameFromPath(opts_.xamlPath);
                shortClassName_ = xClass;
            }
            rootClassName_ = xClass;
        } else {
            moduleName_ = !opts_.defaultModuleName.empty()
                ? opts_.defaultModuleName
                : DeriveModuleNameFromPath(opts_.xamlPath);
            const std::string stem = SanitizeIdentifier(
                std::filesystem::path(opts_.xamlPath).stem().string(),
                root->localName == "ResourceDictionary" ? "Resources" : "View");
            shortClassName_ = (root->localName == "ResourceDictionary")
                ? (stem + "_Resources")
                : (stem + "View");
            rootClassName_ = moduleName_ + "." + shortClassName_;
        }

        // Collect clr-namespace prefix mappings; import lazily when referenced
        for (const auto& attr : root->attributes) {
            if (!attr.isXmlns || attr.localName.empty()) {
                continue;
            }
            const std::string ns = ExtractClrNamespace(attr.value);
            if (!ns.empty()) {
                prefixToNamespace_[attr.localName] = ns;
            }
        }
        if (const auto* xdt = root->FindXAttribute("DataType")) {
            MaybeImportPrefixedType(xdt->value);
        }

        // Emit body into bodyStream_ first so x:Name fields & imports are collected
        std::ostringstream body;
        if (root->localName == "ResourceDictionary") {
            EmitStandaloneResourceDictionary(*root, opts_.xamlPath, body);
        } else if (root->localName == "Application") {
            EmitApplicationRoot(*root, opts_.xamlPath, body);
        } else if (!xClass.empty()) {
            EmitPartialViewRoot(*root, xDataType, opts_.xamlPath, body);
        } else {
            EmitStandaloneViewFactory(*root, xDataType, opts_.xamlPath, body);
        }

        // Assemble final deterministic .g.rs file
        std::ostringstream out;
        out << "// Auto-generated by AeroCompiler v3.0 from " << opts_.xamlPath << "\n";
        out << "module " << moduleName_ << ";\n";
        out << "import Aero.UI;\n";
        out << "import Aero.Native;\n";
        for (const auto& imp : extraImports_) {
            out << "import " << imp << ";\n";
        }
        out << "\n";
        out << body.str();

        res.succeeded = diagnostics_.empty();
        res.moduleName = moduleName_;
        res.rootClassName = rootClassName_;
        res.generatedRsCode = out.str();
        res.diagnostics = diagnostics_;
        return res;
    }

private:
    const XamlEmitOptions& opts_;
    std::string moduleName_;
    std::string shortClassName_;
    std::string rootClassName_;
    std::set<std::string> extraImports_;
    std::vector<std::pair<std::string, std::string>> namedFields_; // (fieldName, typeName)
    std::unordered_set<std::string> seenFieldNames_;
    std::unordered_map<std::string, std::string> prefixToNamespace_;
    std::unordered_map<std::string, std::string> knownColors_;
    std::unordered_map<std::string, std::string> knownGeometries_;
    std::unordered_map<std::string, float> knownDoubles_;
    std::unordered_set<std::string> visitedExternalXamls_;
    std::vector<std::string> diagnostics_;
    int nextId_ = 0;
    bool inPartialClass_ = false;
    bool emittedLocSources_ = false;
    std::string currentTplParentVar_;

    void MaybeImportClrNamespace(std::string_view ns) {
        if (ns.empty() || ns == moduleName_ || IsIgnoredExternalClrNamespace(ns)) {
            return;
        }
        extraImports_.insert(std::string(ns));
    }

    void MaybeImportPrefixedType(std::string_view raw) {
        std::string_view s = aero::str::Trim(raw);
        if (LooksLikeMarkupExtension(s)) {
            const auto ext = ParseMarkupExtension(s);
            if (ext.kind == MarkupExtensionKind::XType) {
                if (const auto* p0 = ext.FindPositional(0)) s = p0->literal;
            }
        }
        const std::size_t colon = s.find(':');
        if (colon != std::string_view::npos) {
            const std::string prefix(s.substr(0, colon));
            if (const auto it = prefixToNamespace_.find(prefix); it != prefixToNamespace_.end()) {
                MaybeImportClrNamespace(it->second);
            }
        }
    }

    [[nodiscard]] std::string FreshVar(std::string_view prefix) {
        return std::string(prefix) + "_" + std::to_string(++nextId_);
    }

    [[nodiscard]] static std::string MakeGeneratedRsPath(std::string_view xamlPath) {
        std::string p(xamlPath);
        if (aero::str::EndsWith(p, ".xaml")) {
            p.resize(p.size() - 5);
        }
        return p + ".g.rs";
    }

    [[nodiscard]] static std::string DeriveModuleNameFromPath(std::string_view xamlPath) {
        const std::string stem = std::filesystem::path(xamlPath).stem().string();
        return "Aero.Generated." + SanitizeIdentifier(stem, "View");
    }

    static void Indent(std::ostringstream& out, int depth) {
        for (int i = 0; i < depth; ++i) {
            out << "    ";
        }
    }

    void RegisterNamedField(const std::string& name, const std::string& typeName) {
        if (!inPartialClass_ || name.empty()) {
            return;
        }
        if (seenFieldNames_.insert(name).second) {
            namedFields_.emplace_back(name, typeName);
        }
    }

    [[nodiscard]] std::string ResolveElementType(const XamlNode& node) {
        std::string typeName = node.localName;
        if (typeName == "TransitionContentPresenter" || typeName == "ContentPresenter") {
            typeName = "ContentPresenter";
        } else if (typeName == "RichText") {
            typeName = "TextBlock";
        }
        if (!node.prefix.empty() && aero::str::StartsWith(node.namespaceUri, "clr-namespace:")) {
            MaybeImportClrNamespace(ExtractClrNamespace(node.namespaceUri));
        } else if (!node.prefix.empty()) {
            if (const auto it = prefixToNamespace_.find(node.prefix); it != prefixToNamespace_.end()) {
                MaybeImportClrNamespace(it->second);
            }
        }
        return typeName;
    }

    [[nodiscard]] std::string ResolveColorHexOrString(std::string_view raw) const {
        if (LooksLikeMarkupExtension(raw)) {
            const auto ext = ParseMarkupExtension(raw);
            if (ext.kind == MarkupExtensionKind::StaticResource ||
                ext.kind == MarkupExtensionKind::DynamicResource) {
                if (const auto* k = ext.FindPositional(0)) {
                    const auto it = knownColors_.find(k->literal);
                    if (it != knownColors_.end()) {
                        return it->second;
                    }
                }
            }
        }
        return std::string(raw);
    }

    [[nodiscard]] std::string EmitExternalDictionaryInclude(
        std::string_view requestedSource,
        std::string_view currentXamlPath,
        std::string_view targetDictVar,
        std::ostringstream& out,
        int indent) {
        const std::string cleanRel = NormalizePackSourcePath(requestedSource);
        if (cleanRel.empty()) {
            return {};
        }

        std::string resolvedPath;
        std::string xamlContent;
        bool loaded = false;

        if (opts_.externalXamlResolver) {
            loaded = opts_.externalXamlResolver(cleanRel, currentXamlPath, resolvedPath, xamlContent);
        }
        if (!loaded) {
            std::vector<std::filesystem::path> candidates;
            const std::filesystem::path curDir = std::filesystem::path(currentXamlPath).parent_path();
            if (!curDir.empty()) {
                candidates.push_back(curDir / cleanRel);
            }
            if (!opts_.resourceSearchRoot.empty()) {
                candidates.push_back(std::filesystem::path(opts_.resourceSearchRoot) / cleanRel);
            }
            for (const auto& sp : opts_.resourceSearchPaths) {
                candidates.push_back(std::filesystem::path(sp) / cleanRel);
            }
            candidates.push_back(std::filesystem::path(cleanRel));
            // Fallback for packaged NoesisTheme references (`Theme/NoesisTheme.*.xaml`)
            if (aero::str::StartsWith(cleanRel, "Theme/")) {
                std::filesystem::path probe = curDir;
                for (int up = 0; up < 4 && !probe.empty(); ++up) {
                    candidates.push_back(probe / "Theme" / "Data" / cleanRel);
                    candidates.push_back(probe / "Samples" / "Theme" / "Data" / cleanRel);
                    probe = probe.parent_path();
                }
                candidates.push_back(std::filesystem::path("Samples/Theme/Data") / cleanRel);
            }

            std::error_code ec;
            for (const auto& cand : candidates) {
                if (std::filesystem::is_regular_file(cand, ec)) {
                    resolvedPath = cand.lexically_normal().generic_string();
                    xamlContent = ReadFileTextUtf8(cand);
                    loaded = !xamlContent.empty();
                    if (loaded) {
                        break;
                    }
                }
            }
        }

        if (!loaded) {
            diagnostics_.push_back(
                "Could not resolve MergedDictionary Source '" + std::string(requestedSource) +
                "' referenced from '" + std::string(currentXamlPath) + "'");
            return {};
        }
        if (resolvedPath.empty()) {
            resolvedPath = cleanRel;
        }
        if (!visitedExternalXamls_.insert(resolvedPath).second) {
            diagnostics_.push_back("Circular ResourceDictionary Source detected: " + resolvedPath);
            return {};
        }

        std::unique_ptr<XamlNode> mergedRoot;
        if (BuildXamlDom(xamlContent, mergedRoot, diagnostics_)) {
            EmitResourceDictionaryEntries(*mergedRoot, resolvedPath, targetDictVar, out, indent);
        }
        visitedExternalXamls_.erase(resolvedPath);
        return resolvedPath;
    }

    void EmitResourceDictionaryEntries(
        const XamlNode& dictNode,
        std::string_view currentXamlPath,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        if (const auto* rootSrc = dictNode.FindAttribute("Source")) {
            (void)EmitExternalDictionaryInclude(
                rootSrc->value, currentXamlPath, dictVar, out, indent);
        }
        if (const auto* mergedEl = dictNode.FindPropertyElement("ResourceDictionary", "MergedDictionaries")) {
            for (const auto& child : mergedEl->children) {
                if (ShouldIgnoreXmlElement(*child)) {
                    continue;
                }
                if (child->localName == "ResourceDictionary") {
                    if (const auto* srcAttr = child->FindAttribute("Source")) {
                        (void)EmitExternalDictionaryInclude(
                            srcAttr->value, currentXamlPath, dictVar, out, indent);
                    } else {
                        EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
                    }
                }
            }
        }

        for (const auto& child : dictNode.children) {
            if (ShouldIgnoreXmlElement(*child)) {
                continue;
            }
            if (child->localName == "ResourceDictionary") {
                if (const auto* srcAttr = child->FindAttribute("Source")) {
                    (void)EmitExternalDictionaryInclude(
                        srcAttr->value, currentXamlPath, dictVar, out, indent);
                } else {
                    EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
                }
                continue;
            }
            EmitSingleDirectResource(*child, dictVar, out, indent);
        }
    }

    void EmitStandaloneResourceDictionary(
        const XamlNode& root,
        std::string_view currentXamlPath,
        std::ostringstream& out) {
        inPartialClass_ = false;
        out << "public class " << shortClassName_ << "\n{\n";
        Indent(out, 1);
        out << "public static void Register(ResourceDictionary dict)\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "if (dict == null) { return; }\n";
        EmitResourceDictionaryEntries(root, currentXamlPath, "dict", out, 2);
        Indent(out, 1);
        out << "}\n\n";
        Indent(out, 1);
        out << "public static ResourceDictionary Build()\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "var dict = new ResourceDictionary();\n";
        Indent(out, 2);
        out << "Register(dict);\n";
        Indent(out, 2);
        out << "return dict;\n";
        Indent(out, 1);
        out << "}\n\n";
        Indent(out, 1);
        out << "public " << shortClassName_ << "()\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "Register(ResourceDictionary.GlobalApplicationResources());\n";
        Indent(out, 1);
        out << "}\n}\n";
    }

    void EmitApplicationRoot(
        const XamlNode& root,
        std::string_view currentXamlPath,
        std::ostringstream& out) {
        const std::string baseClass = root.localName.empty() ? std::string("Application") : root.localName;
        if (root.FindXAttribute("Class") != nullptr) {
            inPartialClass_ = true;
        }
        std::ostringstream initBody;
        Indent(initBody, 2);
        initBody << "if (this._contentLoaded) { return; }\n";
        Indent(initBody, 2);
        initBody << "this._contentLoaded = true;\n";
        if (const auto* startup = root.FindAttribute("StartupUri")) {
            Indent(initBody, 2);
            initBody << "// StartupUri: " << startup->value << "\n";
        }
        EmitResourcesPropertyElement(root, baseClass, "this", currentXamlPath, initBody, 2);

        if (inPartialClass_) {
            out << "[Observable]\n";
            out << "public partial class " << shortClassName_ << " : Application\n{\n";
        } else {
            out << "public class " << shortClassName_ << " : Application\n{\n";
        }
        Indent(out, 1);
        out << "private bool _contentLoaded = false;\n";
        for (const auto& [fn, tn] : namedFields_) {
            Indent(out, 1);
            out << "public " << tn << " " << fn << ";\n";
        }
        out << "\n";
        Indent(out, 1);
        out << "public void InitializeComponent()\n";
        Indent(out, 1);
        out << "{\n";
        out << initBody.str();
        Indent(out, 1);
        out << "}\n}\n";
    }

    void EmitPartialViewRoot(
        const XamlNode& root,
        std::string_view xDataType,
        std::string_view currentXamlPath,
        std::ostringstream& out) {
        inPartialClass_ = true;
        const std::string baseType = ResolveElementType(root);

        std::ostringstream initBody;
        Indent(initBody, 2);
        initBody << "if (this._contentLoaded) { return; }\n";
        Indent(initBody, 2);
        initBody << "this._contentLoaded = true;\n";

        EmitElementBody(root, "this", baseType, baseType, xDataType, "this.Resources", currentXamlPath, initBody, 2);

        Indent(initBody, 2);
        initBody << "BindingEngine.Flush();\n";

        out << "[Observable]\n";
        out << "public partial class " << shortClassName_ << " : " << baseType << "\n{\n";
        Indent(out, 1);
        out << "private bool _contentLoaded = false;\n";
        for (const auto& [fn, tn] : namedFields_) {
            Indent(out, 1);
            out << "public " << tn << " " << fn << ";\n";
        }
        out << "\n";
        Indent(out, 1);
        out << "public void InitializeComponent()\n";
        Indent(out, 1);
        out << "{\n";
        out << initBody.str();
        Indent(out, 1);
        out << "}\n}\n";
    }

    void EmitStandaloneViewFactory(
        const XamlNode& root,
        std::string_view xDataType,
        std::string_view currentXamlPath,
        std::ostringstream& out) {
        inPartialClass_ = true;
        const std::string rootType = ResolveElementType(root);
        const bool isHostRoot =
            rootType == "Page" || rootType == "Window" ||
            rootType == "UserControl" || rootType == "ContentControl";
        const std::string baseType = isHostRoot ? rootType : std::string("Page");

        std::ostringstream initBody;
        Indent(initBody, 2);
        initBody << "if (this._contentLoaded) { return; }\n";
        Indent(initBody, 2);
        initBody << "this._contentLoaded = true;\n";
        if (isHostRoot) {
            EmitElementBody(
                root, "this", root.localName, baseType, xDataType, "this.Resources",
                currentXamlPath, initBody, 2);
        } else {
            const std::string inner = EmitConstructedElement(
                root, xDataType, "this.Resources", currentXamlPath, initBody, 2);
            Indent(initBody, 2);
            initBody << "this.SetContent(" << inner << ");\n";
        }
        Indent(initBody, 2);
        initBody << "BindingEngine.Flush();\n";

        out << "[Observable]\n";
        out << "public partial class " << shortClassName_ << " : " << baseType << "\n{\n";
        Indent(out, 1);
        out << "private bool _contentLoaded = false;\n";
        for (const auto& [fn, tn] : namedFields_) {
            Indent(out, 1);
            out << "public " << tn << " " << fn << ";\n";
        }
        out << "\n";
        Indent(out, 1);
        out << "public " << shortClassName_ << "()\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "this.InitializeComponent();\n";
        Indent(out, 1);
        out << "}\n\n";
        Indent(out, 1);
        out << "public static " << baseType << " Build()\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "return new " << shortClassName_ << "();\n";
        Indent(out, 1);
        out << "}\n\n";
        Indent(out, 1);
        out << "public void InitializeComponent()\n";
        Indent(out, 1);
        out << "{\n";
        out << initBody.str();
        Indent(out, 1);
        out << "}\n}\n";
    }

    void EmitResourcesPropertyElement(
        const XamlNode& ownerNode,
        std::string_view ownerType,
        std::string_view ownerVar,
        std::string_view currentXamlPath,
        std::ostringstream& out,
        int indent) {
        const XamlNode* resProp = ownerNode.FindPropertyElement(ownerType, "Resources");
        if (resProp == nullptr) {
            resProp = ownerNode.FindPropertyElement("FrameworkElement", "Resources");
        }
        if (resProp == nullptr) {
            return;
        }

        const std::string dictVar = std::string(ownerVar) + ".Resources";
        for (const auto& child : resProp->children) {
            if (ShouldIgnoreXmlElement(*child)) {
                continue;
            }
            if (child->localName == "ResourceDictionary") {
                EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
            } else {
                EmitSingleDirectResource(*child, dictVar, out, indent);
            }
        }
    }

    void EmitSingleDirectResource(
        const XamlNode& child,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::string key = child.GetKey();
        if (child.localName == "Style" && key.empty()) {
            if (const auto* tt = child.FindAttribute("TargetType")) {
                key = StripTargetTypeName(tt->value);
            }
        }
        if (key.empty()) {
            return;
        }
        if (child.localName == "Color") {
            const std::string hex = std::string(aero::str::Trim(child.textContent));
            knownColors_[key] = hex;
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
                << EscapeRsString(hex) << "));\n";
        } else if (child.localName == "SolidColorBrush" ||
                   child.localName == "LinearGradientBrush" ||
                   child.localName == "RadialGradientBrush" ||
                   child.localName == "ImageBrush") {
            const std::string expr = EmitBrushObject(child, {}, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << expr << ");\n";
        } else if (child.localName == "Style") {
            const std::string v = EmitStyle(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "ControlTemplate") {
            const std::string v = EmitControlTemplate(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "DataTemplate" || child.localName == "HierarchicalDataTemplate") {
            const std::string v = EmitDataTemplate(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "ItemsPanelTemplate") {
            const std::string v = EmitItemsPanelTemplate(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "Storyboard") {
            const std::string v = EmitStoryboard(child, out, indent);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "Thickness") {
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", "
                << FormatThicknessExpr(child.textContent) << ");\n";
        } else if (child.localName == "CornerRadius") {
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", "
                << FormatCornerRadiusExpr(child.textContent) << ");\n";
        } else if (child.localName == "Double" || child.localName == "Single") {
            float val = 0.0f;
            (void)aero::str::TryParseFloat32(child.textContent, val);
            knownDoubles_[key] = val;
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxFloat("
                << FormatFloatLiteral(val) << "));\n";
        } else if (child.localName == "Int32" || child.localName == "Int64") {
            const long iv = std::strtol(std::string(child.textContent).c_str(), nullptr, 10);
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxInt(" << iv << "));\n";
        } else if (child.localName == "Boolean") {
            const bool bv = aero::str::EqualsIgnoreCase(aero::str::Trim(child.textContent), "True");
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxBool("
                << (bv ? "true" : "false") << "));\n";
        } else if (child.localName == "String" || child.localName == "FontFamily" ||
                   child.localName == "FlowDirection") {
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
                << EscapeRsString(aero::str::Trim(child.textContent)) << "));\n";
        } else if (child.localName == "BitmapImage") {
            std::string uri;
            if (const auto* u = child.FindAttribute("UriSource")) uri = u->value;
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
                << EscapeRsString(uri) << "));\n";
        } else if (child.localName == "StreamGeometry" ||
                   child.localName == "PathGeometry" ||
                   child.localName == "Geometry") {
            const std::string geom = std::string(aero::str::Trim(child.textContent));
            knownGeometries_[key] = geom;
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
                << EscapeRsString(geom) << "));\n";
        } else if (child.localName == "BlurEffect" || child.localName == "DropShadowEffect" ||
                   child.localName == "BackdropBlurEffect" || child.localName == "TintEffect" ||
                   child.localName == "PixelateEffect" || child.localName == "ChromaticAberrationEffect" ||
                   child.localName == "DirectionalBlurEffect" || child.localName == "SaturationEffect") {
            const std::string efVar = EmitEffectObject(child, {}, out, indent);
            if (!efVar.empty()) {
                Indent(out, indent);
                out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << efVar << ");\n";
            }
        } else if (child.localName == "BoolToVisibilityConverter" ||
                   child.localName == "BooleanToVisibilityConverter" ||
                   child.localName == "InverseBoolConverter" ||
                   child.localName == "NullToVisibilityConverter" ||
                   child.localName == "AlternationConverter" ||
                   child.localName == "MathConverter") {
            const std::string typeName = (child.localName == "BooleanToVisibilityConverter")
                ? std::string("BoolToVisibilityConverter")
                : child.localName;
            const std::string cVar = FreshVar("conv");
            Indent(out, indent);
            out << "var " << cVar << " = new " << typeName << "();\n";
            if (typeName == "MathConverter") {
                if (const auto* ex = child.FindAttribute("Expression")) {
                    Indent(out, indent);
                    out << cVar << ".Expression = " << EscapeRsString(ex->value) << ";\n";
                }
            }
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << cVar << ");\n";
        }
    }

    [[nodiscard]] std::string EmitBrushObject(
        const XamlNode& brushNode,
        std::string_view /*targetProp*/,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        if (brushNode.localName == "SolidColorBrush") {
            std::string color = "#FFFFFFFF";
            std::string dynamicColorKey;
            if (const auto* colAttr = brushNode.FindAttribute("Color")) {
                if (LooksLikeMarkupExtension(colAttr->value)) {
                    const auto ext = ParseMarkupExtension(colAttr->value);
                    if (ext.kind == MarkupExtensionKind::StaticResource ||
                        ext.kind == MarkupExtensionKind::DynamicResource) {
                        const std::string rk = ResolveResourceKeyFromExtension(ext);
                        if (!rk.empty() && knownColors_.count(rk) > 0) {
                            color = knownColors_.at(rk);
                        } else if (!rk.empty()) {
                            dynamicColorKey = rk;
                        }
                    }
                } else {
                    color = ResolveColorHexOrString(colAttr->value);
                }
            }
            // Check shader child tint override (Brush.Shader -> SaturationBrush / ConicGradientBrush / PlasmaBrush)
            if (const auto* shProp = brushNode.FindPropertyElement("SolidColorBrush", "Shader")) {
                if (!shProp->children.empty() && dynamicColorKey.empty()) {
                    const std::string& shName = shProp->children.front()->localName;
                    if (shName == "PlasmaBrush" || shName == "ConicGradientBrush" || shName == "SaturationBrush") {
                        const std::string bVar = FreshVar("shgrad");
                        Indent(out, indent);
                        out << "var " << bVar << " = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);\n";
                        Indent(out, indent);
                        out << bVar << ".AddStop(\"#FF38BDF8\", 0.0f);\n";
                        Indent(out, indent);
                        out << bVar << ".AddStop(\"#FF8B5CF6\", 0.5f);\n";
                        Indent(out, indent);
                        out << bVar << ".AddStop(\"#FFEC4899\", 1.0f);\n";
                        return bVar;
                    }
                }
            }
            const std::string bVar = FreshVar("brush");
            Indent(out, indent);
            if (!dynamicColorKey.empty()) {
                out << "var " << bVar << " = new SolidColorBrush(\"#FFFFFFFF\");\n";
                Indent(out, indent);
                out << "var __c_" << bVar << " = " << LookupResourceExpr(dynamicColorKey, dictVar) << ";\n";
                Indent(out, indent);
                out << "if (__c_" << bVar << " is string __cs_" << bVar << ") { "
                    << bVar << " = new SolidColorBrush(__cs_" << bVar << "); }\n";
            } else {
                out << "var " << bVar << " = new SolidColorBrush(" << EscapeRsString(color) << ");\n";
            }
            if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
                float op = 1.0f;
                if (aero::str::TryParseFloat32(opAttr->value, op)) {
                    Indent(out, indent);
                    out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
                }
            }
            return bVar;
        }

        if (brushNode.localName == "ImageBrush") {
            const std::string bVar = FreshVar("ibrush");
            std::string src;
            if (const auto* sa = brushNode.FindAttribute("ImageSource")) {
                if (!LooksLikeMarkupExtension(sa->value)) src = sa->value;
            }
            Indent(out, indent);
            out << "var " << bVar << " = new ImageBrush(" << EscapeRsString(src) << ");\n";
            if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
                float op = 1.0f;
                if (aero::str::TryParseFloat32(opAttr->value, op)) {
                    Indent(out, indent);
                    out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
                }
            }
            return bVar;
        }

        if (brushNode.localName == "LinearGradientBrush") {
            float x0 = 0.0f, y0 = 0.0f, x1 = 1.0f, y1 = 1.0f;
            if (const auto* sp = brushNode.FindAttribute("StartPoint")) {
                std::vector<float> pts;
                if (TryParseFloatList(sp->value, pts) && pts.size() >= 2) {
                    x0 = pts[0];
                    y0 = pts[1];
                }
            }
            if (const auto* ep = brushNode.FindAttribute("EndPoint")) {
                std::vector<float> pts;
                if (TryParseFloatList(ep->value, pts) && pts.size() >= 2) {
                    x1 = pts[0];
                    y1 = pts[1];
                }
            }
            const std::string bVar = FreshVar("lgrad");
            Indent(out, indent);
            out << "var " << bVar << " = new LinearGradientBrush("
                << FormatFloatLiteral(x0) << ", " << FormatFloatLiteral(y0) << ", "
                << FormatFloatLiteral(x1) << ", " << FormatFloatLiteral(y1) << ");\n";
            if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
                float op = 1.0f;
                if (aero::str::TryParseFloat32(opAttr->value, op)) {
                    Indent(out, indent);
                    out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
                }
            }
            EmitGradientStops(brushNode, bVar, dictVar, out, indent);
            return bVar;
        }

        if (brushNode.localName == "RadialGradientBrush") {
            float cx = 0.5f, cy = 0.5f, ox = 0.5f, oy = 0.5f, rx = 0.5f, ry = 0.5f;
            if (const auto* ca = brushNode.FindAttribute("Center")) {
                std::vector<float> pts;
                if (TryParseFloatList(ca->value, pts) && pts.size() >= 2) {
                    cx = pts[0];
                    cy = pts[1];
                }
            }
            if (const auto* oa = brushNode.FindAttribute("GradientOrigin")) {
                std::vector<float> pts;
                if (TryParseFloatList(oa->value, pts) && pts.size() >= 2) {
                    ox = pts[0];
                    oy = pts[1];
                }
            } else {
                ox = cx;
                oy = cy;
            }
            if (const auto* rxa = brushNode.FindAttribute("RadiusX")) {
                (void)aero::str::TryParseFloat32(rxa->value, rx);
            }
            if (const auto* rya = brushNode.FindAttribute("RadiusY")) {
                (void)aero::str::TryParseFloat32(rya->value, ry);
            }
            const std::string bVar = FreshVar("rgrad");
            Indent(out, indent);
            out << "var " << bVar << " = new RadialGradientBrush("
                << FormatFloatLiteral(cx) << ", " << FormatFloatLiteral(cy) << ", "
                << FormatFloatLiteral(ox) << ", " << FormatFloatLiteral(oy) << ", "
                << FormatFloatLiteral(rx) << ", " << FormatFloatLiteral(ry) << ");\n";
            if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
                float op = 1.0f;
                if (aero::str::TryParseFloat32(opAttr->value, op)) {
                    Indent(out, indent);
                    out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
                }
            }
            EmitGradientStops(brushNode, bVar, dictVar, out, indent);
            return bVar;
        }

        return "new SolidColorBrush(\"#00000000\")";
    }

    void EmitGradientStops(
        const XamlNode& brushNode,
        std::string_view brushVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const XamlNode* stopContainer = brushNode.FindPropertyElement(brushNode.localName, "GradientStops");
        const auto& stops = (stopContainer != nullptr) ? stopContainer->children : brushNode.children;
        for (const auto& stop : stops) {
            if (stop->localName != "GradientStop") {
                continue;
            }
            float offset = 0.0f;
            if (const auto* oAttr = stop->FindAttribute("Offset")) {
                (void)aero::str::TryParseFloat32(oAttr->value, offset);
            }
            std::string color = "#FFFFFFFF";
            if (const auto* cAttr = stop->FindAttribute("Color")) {
                if (LooksLikeMarkupExtension(cAttr->value)) {
                    const auto ext = ParseMarkupExtension(cAttr->value);
                    if (ext.kind == MarkupExtensionKind::StaticResource ||
                        ext.kind == MarkupExtensionKind::DynamicResource) {
                        const std::string rk = ResolveResourceKeyFromExtension(ext);
                        if (!rk.empty() && knownColors_.count(rk) > 0) {
                            color = knownColors_.at(rk);
                        } else if (!rk.empty()) {
                            Indent(out, indent);
                            out << brushVar << ".AddStopObj(" << LookupResourceExpr(rk, dictVar)
                                << ", " << FormatFloatLiteral(offset) << ");\n";
                            continue;
                        }
                    }
                } else {
                    color = ResolveColorHexOrString(cAttr->value);
                }
            }
            Indent(out, indent);
            out << brushVar << ".AddStop(" << EscapeRsString(color) << ", "
                << FormatFloatLiteral(offset) << ");\n";
        }
    }

    [[nodiscard]] std::string EmitTransformObject(
        const XamlNode& node,
        std::ostringstream& out,
        int indent) {
        auto getF = [&](std::string_view name, float def) -> float {
            if (const auto* a = node.FindAttribute(name)) {
                float v = def;
                if (aero::str::TryParseFloat32(a->value, v)) return v;
            }
            return def;
        };

        if (node.localName == "TranslateTransform") {
            const std::string v = FreshVar("tr");
            Indent(out, indent);
            out << "var " << v << " = new TranslateTransform("
                << FormatFloatLiteral(getF("X", 0.0f)) << ", "
                << FormatFloatLiteral(getF("Y", 0.0f)) << ");\n";
            return v;
        }
        if (node.localName == "ScaleTransform") {
            const std::string v = FreshVar("sc");
            Indent(out, indent);
            out << "var " << v << " = new ScaleTransform("
                << FormatFloatLiteral(getF("ScaleX", 1.0f)) << ", "
                << FormatFloatLiteral(getF("ScaleY", 1.0f)) << ", "
                << FormatFloatLiteral(getF("CenterX", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterY", 0.0f)) << ");\n";
            return v;
        }
        if (node.localName == "RotateTransform") {
            const std::string v = FreshVar("rot");
            Indent(out, indent);
            out << "var " << v << " = new RotateTransform("
                << FormatFloatLiteral(getF("Angle", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterX", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterY", 0.0f)) << ");\n";
            return v;
        }
        if (node.localName == "SkewTransform") {
            const std::string v = FreshVar("sk");
            Indent(out, indent);
            out << "var " << v << " = new SkewTransform("
                << FormatFloatLiteral(getF("AngleX", 0.0f)) << ", "
                << FormatFloatLiteral(getF("AngleY", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterX", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterY", 0.0f)) << ");\n";
            return v;
        }
        if (node.localName == "TransformGroup") {
            const std::string tg = FreshVar("tg");
            Indent(out, indent);
            out << "var " << tg << " = new TransformGroup();\n";
            const XamlNode* childProp = node.FindPropertyElement("TransformGroup", "Children");
            const auto& items = (childProp != nullptr) ? childProp->children : node.children;
            for (const auto& item : items) {
                const std::string sub = EmitTransformObject(*item, out, indent);
                if (!sub.empty()) {
                    Indent(out, indent);
                    out << tg << ".Add(" << sub << ");\n";
                }
            }
            return tg;
        }
        return {};
    }

    [[nodiscard]] std::string EmitEffectObject(
        const XamlNode& node,
        std::string_view hostUiVar,
        std::ostringstream& out,
        int indent) {
        auto getStaticOrFloat = [&](std::string_view attrName, float def, std::string& outBindExpr) -> float {
            if (const auto* a = node.FindAttribute(attrName)) {
                if (LooksLikeMarkupExtension(a->value)) {
                    outBindExpr = a->value;
                    return def;
                }
                float v = def;
                (void)aero::str::TryParseFloat32(a->value, v);
                return v;
            }
            return def;
        };

        if (node.localName == "BlurEffect") {
            std::string bindExpr;
            const float radius = getStaticOrFloat("Radius", 5.0f, bindExpr);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new BlurEffect(" << FormatFloatLiteral(radius) << ");\n";
            if (!bindExpr.empty() && !hostUiVar.empty()) {
                EmitPropertyAttributeOrBinding(hostUiVar, "BlurEffect", "Effect.Radius", bindExpr, "", {}, out, indent);
            }
            return v;
        }
        if (node.localName == "BackdropBlurEffect") {
            std::string bindExpr;
            const float radius = getStaticOrFloat("Radius", 16.0f, bindExpr);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new BackdropBlurEffect(" << FormatFloatLiteral(radius) << ");\n";
            return v;
        }
        if (node.localName == "TintEffect") {
            std::string bOp;
            const float op = getStaticOrFloat("Opacity", 0.5f, bOp);
            std::string color = "#FF2266FF";
            if (const auto* cAttr = node.FindAttribute("Color")) {
                if (!LooksLikeMarkupExtension(cAttr->value)) color = ResolveColorHexOrString(cAttr->value);
            }
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new TintEffect(" << EscapeRsString(color) << ", " << FormatFloatLiteral(op) << ");\n";
            return v;
        }
        if (node.localName == "PixelateEffect") {
            std::string bPx;
            float sz = getStaticOrFloat("Size", 6.0f, bPx);
            if (node.FindAttribute("PixelSize") != nullptr) sz = getStaticOrFloat("PixelSize", sz, bPx);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new PixelateEffect(" << FormatFloatLiteral(sz) << ");\n";
            return v;
        }
        if (node.localName == "ChromaticAberrationEffect") {
            std::string bIn;
            float intensity = getStaticOrFloat("Intensity", 3.0f, bIn);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new ChromaticAberrationEffect(" << FormatFloatLiteral(intensity) << ");\n";
            return v;
        }
        if (node.localName == "DirectionalBlurEffect") {
            std::string bR, bD;
            const float r = getStaticOrFloat("Radius", 8.0f, bR);
            const float d = getStaticOrFloat("Angle", getStaticOrFloat("Direction", 0.0f, bD), bD);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new DirectionalBlurEffect(" << FormatFloatLiteral(r) << ", " << FormatFloatLiteral(d) << ");\n";
            return v;
        }
        if (node.localName == "SaturationEffect") {
            std::string bS;
            float s = getStaticOrFloat("Saturation", 1.0f, bS);
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new SaturationEffect(" << FormatFloatLiteral(s) << ");\n";
            return v;
        }
        if (node.localName == "DropShadowEffect") {
            std::string bRad, bDep, bDir, bOp;
            const float radius = getStaticOrFloat("BlurRadius", 5.0f, bRad);
            const float depth = getStaticOrFloat("ShadowDepth", 5.0f, bDep);
            const float dir = getStaticOrFloat("Direction", 315.0f, bDir);
            const float op = getStaticOrFloat("Opacity", 1.0f, bOp);
            std::string color = "#FF000000";
            if (const auto* cAttr = node.FindAttribute("Color")) {
                if (!LooksLikeMarkupExtension(cAttr->value)) {
                    color = ResolveColorHexOrString(cAttr->value);
                }
            }
            const std::string v = FreshVar("eff");
            Indent(out, indent);
            out << "var " << v << " = new DropShadowEffect("
                << FormatFloatLiteral(radius) << ", "
                << FormatFloatLiteral(depth) << ", "
                << FormatFloatLiteral(dir) << ", "
                << EscapeRsString(color) << ", "
                << FormatFloatLiteral(op) << ");\n";
            return v;
        }
        return {};
    }

    [[nodiscard]] std::string EmitStyle(
        const XamlNode& styleNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::string targetType = "Control";
        if (const auto* tt = styleNode.FindAttribute("TargetType")) {
            targetType = StripTargetTypeName(tt->value);
        }
        const std::string styVar = FreshVar("style");
        Indent(out, indent);
        out << "var " << styVar << " = new Style(" << EscapeRsString(targetType) << ");\n";

        if (const auto* basedOn = styleNode.FindAttribute("BasedOn")) {
            if (LooksLikeMarkupExtension(basedOn->value)) {
                const auto ext = ParseMarkupExtension(basedOn->value);
                const std::string rk = ResolveResourceKeyFromExtension(ext);
                if (!rk.empty()) {
                    Indent(out, indent);
                    out << "var __base_" << styVar << " = " << LookupResourceExpr(rk, dictVar) << ";\n";
                    Indent(out, indent);
                    out << "if (__base_" << styVar << " is Style __bs_" << styVar << ") { "
                        << styVar << ".BasedOn = __bs_" << styVar << "; }\n";
                }
            }
        }

        for (const auto& child : styleNode.children) {
            if (child->localName == "Setter") {
                EmitSetterInto(*child, styVar, dictVar, out, indent);
            }
        }

        if (const XamlNode* trigProp = styleNode.FindPropertyElement("Style", "Triggers")) {
            EmitTriggerCollectionInto(*trigProp, styVar, dictVar, out, indent);
        }
        return styVar;
    }

    static std::string NormalizeSetterPropertyName(std::string_view rawProp) {
        std::string propName(rawProp);
        const std::size_t dot = propName.find('.');
        if (dot != std::string::npos) {
            const std::string_view owner = std::string_view(propName).substr(0, dot);
            if (owner == "Control" || owner == "FrameworkElement" || owner == "UIElement" ||
                owner == "TextBlock" || owner == "Panel") {
                propName = propName.substr(dot + 1);
            }
        }
        return propName;
    }

    [[nodiscard]] static bool IsAttachedBindableProperty(std::string_view propKey) {
        return propKey == "Canvas.Left" || propKey == "Canvas.Top" ||
               propKey == "Canvas.Right" || propKey == "Canvas.Bottom" ||
               propKey == "Grid.Row" || propKey == "Grid.Column" ||
               propKey == "Grid.RowSpan" || propKey == "Grid.ColumnSpan" ||
               propKey == "DockPanel.Dock" || propKey == "Panel.ZIndex" ||
               propKey == "TextElement.FontSize" || propKey == "TextElement.FontFamily" ||
               propKey == "TextElement.FontWeight" || propKey == "TextElement.Foreground" ||
               propKey == "TextBlock.FontSize" || propKey == "TextBlock.FontFamily" ||
               propKey == "TextBlock.FontWeight" || propKey == "TextBlock.Foreground" ||
               propKey == "Element.Projection3D" || propKey == "Element.Transform3D" ||
               propKey == "Element.BlendingMode" || propKey == "RiveControl.Source";
    }

    [[nodiscard]] static std::string NormalizeBindingPath(std::string_view rawPath) {
        std::string s(aero::str::Trim(rawPath));
        if (s.size() >= 2 && s.front() == '(' && s.back() == ')' &&
            s.find(").(") == std::string::npos) {
            s = s.substr(1, s.size() - 2);
        }
        const std::size_t dotPos = s.find('.');
        const std::size_t colPos = s.find(':');
        if (colPos != std::string::npos && (dotPos == std::string::npos || colPos < dotPos)) {
            s = s.substr(colPos + 1);
        }
        return s;
    }

    [[nodiscard]] std::string BoxSetterValueExpr(
        std::string_view propName,
        std::string_view typedExpr) const {
        if (typedExpr == "null") {
            return "null";
        }
        if (typedExpr.find(".GetBox(") != std::string_view::npos ||
            typedExpr.find(".FindResourceBox(") != std::string_view::npos) {
            return std::string(typedExpr);
        }
        if (typedExpr.find(".FindResource(") != std::string_view::npos ||
            typedExpr.find(".Get(") != std::string_view::npos ||
            (!typedExpr.empty() && typedExpr.front() == '(')) {
            return "DependencyObject.BoxObject(" + std::string(typedExpr) + ")";
        }
        if (!typedExpr.empty() && typedExpr.front() == '"') {
            return "DependencyObject.BoxString(" + std::string(typedExpr) + ")";
        }
        if (LookupEnumType(propName) != nullptr) {
            return "DependencyObject.BoxInt(EnumConvert.ToInt(" + std::string(typedExpr) + "))";
        }
        if (IsBoolProperty(propName) || typedExpr == "true" || typedExpr == "false") {
            return "DependencyObject.BoxBool(" + std::string(typedExpr) + ")";
        }
        if (IsIntProperty(propName)) {
            return "DependencyObject.BoxInt(" + std::string(typedExpr) + ")";
        }
        if (IsFloatProperty(propName)) {
            return "DependencyObject.BoxFloat(" + std::string(typedExpr) + ")";
        }
        if (typedExpr.size() >= 2 && typedExpr.back() == 'f' &&
            (std::isdigit(static_cast<unsigned char>(typedExpr.front())) || typedExpr.front() == '-')) {
            return "DependencyObject.BoxFloat(" + std::string(typedExpr) + ")";
        }
        bool allDigits = !typedExpr.empty();
        for (std::size_t i = (typedExpr.front() == '-' && typedExpr.size() > 1) ? 1 : 0; i < typedExpr.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(typedExpr[i]))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            return "DependencyObject.BoxInt(" + std::string(typedExpr) + ")";
        }
        return "DependencyObject.BoxObject(" + std::string(typedExpr) + ")";
    }

    void EmitSetterInto(
        const XamlNode& setterNode,
        std::string_view targetObjVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const auto* pAttr = setterNode.FindAttribute("Property");
        if (pAttr == nullptr) {
            return;
        }
        // StyleInteraction.Triggers -> Style.AddBehaviorTrigger(...)
        if (pAttr->value == "noesis:StyleInteraction.Triggers" ||
            aero::str::EndsWith(pAttr->value, "StyleInteraction.Triggers")) {
            if (const auto* valEl = setterNode.FindPropertyElement("Setter", "Value")) {
                for (const auto& c : valEl->children) {
                    if (c->localName == "StyleTriggerCollection") {
                        for (const auto& trig : c->children) {
                            const std::string tv = EmitBehaviorTriggerObject(*trig, {}, dictVar, out, indent);
                            if (!tv.empty()) {
                                Indent(out, indent);
                                out << targetObjVar << ".AddBehaviorTrigger(" << tv << ");\n";
                            }
                        }
                    } else {
                        const std::string tv = EmitBehaviorTriggerObject(*c, {}, dictVar, out, indent);
                        if (!tv.empty()) {
                            Indent(out, indent);
                            out << targetObjVar << ".AddBehaviorTrigger(" << tv << ");\n";
                        }
                    }
                }
            }
            return;
        }

        const std::string propName = NormalizeSetterPropertyName(pAttr->value);
        if (propName.empty()) {
            return;
        }
        std::string targetName;
        if (const auto* tnAttr = setterNode.FindAttribute("TargetName")) {
            targetName = tnAttr->value;
        }

        if (const auto* valEl = setterNode.FindPropertyElement("Setter", "Value")) {
            for (const auto& c : valEl->children) {
                if (c->localName == "Binding") {
                    ParsedBindingSpec spec;
                    std::string convExpr;
                    TryParseBindingNode(*c, spec, convExpr, dictVar, out, indent);
                    Indent(out, indent);
                    out << targetObjVar << ".AddSetter(Setter.FromBinding("
                        << EscapeRsString(propName) << ", "
                        << EscapeRsString(spec.path) << ", "
                        << (convExpr.empty() ? "null" : convExpr) << ", "
                        << EscapeRsString(spec.converterKey) << ", "
                        << EscapeRsString(spec.converterParam) << ", "
                        << EscapeRsString(spec.stringFormat) << ", "
                        << MapBindingMode(spec.mode) << ", "
                        << EscapeRsString(spec.elementName) << ", "
                        << EscapeRsString(spec.relativeSourceMode) << ", "
                        << EscapeRsString(spec.ancestorType) << ", "
                        << EscapeRsString(targetName) << "));\n";
                    return;
                }
            }
        }

        std::string valExpr;
        if (const auto* valEl = setterNode.FindPropertyElement("Setter", "Value")) {
            for (const auto& c : valEl->children) {
                if (c->localName == "ControlTemplate") {
                    valExpr = EmitControlTemplate(*c, dictVar, out, indent);
                    break;
                }
                if (c->localName == "DataTemplate" || c->localName == "HierarchicalDataTemplate") {
                    valExpr = EmitDataTemplate(*c, dictVar, out, indent);
                    break;
                }
                if (c->localName == "ItemsPanelTemplate") {
                    valExpr = EmitItemsPanelTemplate(*c, dictVar, out, indent);
                    break;
                }
                if (c->localName == "SolidColorBrush" ||
                    c->localName == "LinearGradientBrush" ||
                    c->localName == "RadialGradientBrush" ||
                    c->localName == "ImageBrush") {
                    valExpr = EmitBrushObject(*c, propName, dictVar, out, indent);
                    break;
                }
            }
            if (valExpr.empty() && !valEl->textContent.empty()) {
                valExpr = FormatTypedValueLiteral(propName, valEl->textContent, dictVar);
            }
        } else if (const auto* valAttr = setterNode.FindAttribute("Value")) {
            if (LooksLikeMarkupExtension(valAttr->value)) {
                const auto ext = ParseMarkupExtension(valAttr->value);
                if (ext.kind == MarkupExtensionKind::Binding) {
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec)) {
                        Indent(out, indent);
                        out << targetObjVar << ".AddSetter(Setter.FromBinding("
                            << EscapeRsString(propName) << ", "
                            << EscapeRsString(spec.path) << ", null, "
                            << EscapeRsString(spec.converterKey) << ", "
                            << EscapeRsString(spec.converterParam) << ", "
                            << EscapeRsString(spec.stringFormat) << ", "
                            << MapBindingMode(spec.mode) << ", "
                            << EscapeRsString(spec.elementName) << ", "
                            << EscapeRsString(spec.relativeSourceMode) << ", "
                            << EscapeRsString(spec.ancestorType) << ", "
                            << EscapeRsString(targetName) << "));\n";
                        return;
                    }
                } else if (propName == "ItemsPanel" &&
                           (ext.kind == MarkupExtensionKind::StaticResource ||
                            ext.kind == MarkupExtensionKind::DynamicResource)) {
                    const std::string key = ResolveResourceKeyFromExtension(ext);
                    if (!key.empty()) {
                        valExpr = LookupResourceExpr(key, dictVar);
                    }
                }
            }
            if (valExpr.empty()) {
                valExpr = FormatTypedValueLiteral(propName, valAttr->value, dictVar);
            }
        }

        if (valExpr.empty()) {
            return;
        }
        Indent(out, indent);
        if (!targetName.empty() || valExpr == "null" ||
            LookupEnumType(propName) != nullptr ||
            propName == "Template" || propName == "ItemTemplate" ||
            propName == "ContentTemplate" || propName == "Style" ||
            propName == "ItemContainerStyle" || propName == "ItemsPanel" ||
            aero::str::StartsWith(valExpr, "this.FindResource(") ||
            valExpr.find(".FindResource(") != std::string::npos ||
            valExpr.find(".Get(") != std::string::npos ||
            valExpr.find(".GetBox(") != std::string::npos) {
            out << targetObjVar << ".AddSetterBox(" << EscapeRsString(propName) << ", "
                << BoxSetterValueExpr(propName, valExpr) << ", "
                << EscapeRsString(targetName) << ");\n";
        } else {
            out << targetObjVar << ".AddSetter(" << EscapeRsString(propName) << ", " << valExpr << ");\n";
        }
    }

    void EmitTriggerCollectionInto(
        const XamlNode& trigProp,
        std::string_view ownerObjVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        for (const auto& trig : trigProp.children) {
            if (trig->localName == "Trigger") {
                std::string prop;
                std::string val;
                std::string sourceName;
                if (const auto* p = trig->FindAttribute("Property")) prop = NormalizeSetterPropertyName(p->value);
                if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                if (const auto* sn = trig->FindAttribute("SourceName")) sourceName = sn->value;
                const std::string tVar = FreshVar("trig");
                Indent(out, indent);
                out << "var " << tVar << " = Trigger.CreateBox(" << EscapeRsString(prop) << ", "
                    << BoxSetterValueExpr(prop, FormatTypedValueLiteral(prop, val, dictVar)) << ", "
                    << EscapeRsString(sourceName) << ");\n";
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent);
                    }
                }
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            } else if (trig->localName == "DataTrigger") {
                ParsedBindingSpec bSpec;
                if (const auto* b = trig->FindAttribute("Binding")) {
                    const auto ext = ParseMarkupExtension(b->value);
                    (void)TryExtractBindingSpec(ext, bSpec);
                }
                std::string val;
                if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                const std::string tVar = FreshVar("dtrig");
                Indent(out, indent);
                out << "var " << tVar << " = DataTrigger.CreateBox("
                    << EscapeRsString(NormalizeBindingPath(bSpec.path)) << ", "
                    << BoxSetterValueExpr(bSpec.path, FormatGenericScalarLiteral(val)) << ", "
                    << EscapeRsString(bSpec.relativeSourceMode) << ", "
                    << EscapeRsString(bSpec.ancestorType) << ");\n";
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent);
                    }
                }
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            } else if (trig->localName == "EventTrigger") {
                std::string evName = "Loaded";
                if (const auto* re = trig->FindAttribute("RoutedEvent")) {
                    evName = re->value;
                    const std::size_t dot = evName.rfind('.');
                    if (dot != std::string::npos) evName = evName.substr(dot + 1);
                }
                const std::string tVar = FreshVar("etrig");
                Indent(out, indent);
                out << "var " << tVar << " = new EventTrigger(" << EscapeRsString(evName) << ");\n";
                for (const auto& act : trig->children) {
                    if (act->localName == "BeginStoryboard") {
                        for (const auto& sbNode : act->children) {
                            if (sbNode->localName == "Storyboard") {
                                const std::string sb = EmitStoryboard(*sbNode, out, indent);
                                Indent(out, indent);
                                out << tVar << ".AddAction(" << sb << ");\n";
                            }
                        }
                    }
                }
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            }
        }
    }

    [[nodiscard]] std::string EmitControlTemplate(
        const XamlNode& tplNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::string targetType = "Control";
        if (const auto* tt = tplNode.FindAttribute("TargetType")) {
            targetType = StripTargetTypeName(tt->value);
        }
        const std::string tplVar = FreshVar("ctpl");
        const std::string tpParam = FreshVar("__tp");

        Indent(out, indent);
        out << "var " << tplVar << " = new ControlTemplate();\n";
        Indent(out, indent);
        out << tplVar << ".TargetType = " << EscapeRsString(targetType) << ";\n";
        EmitResourcesPropertyElement(tplNode, "ControlTemplate", tplVar, opts_.xamlPath, out, indent);
        Indent(out, indent);
        out << tplVar << ".Factory = (Control " << tpParam << ") =>\n";
        Indent(out, indent);
        out << "{\n";

        const std::string prevTp = currentTplParentVar_;
        currentTplParentVar_ = tpParam;
        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;
        const std::string_view safeDictVar =
            aero::str::StartsWith(dictVar, "this.") ? dictVar : std::string_view{};

        std::string returnedRoot;
        for (const auto& child : tplNode.children) {
            if (ShouldIgnoreXmlElement(*child) ||
                child->localName.find('.') != std::string::npos) {
                continue;
            }
            returnedRoot = EmitConstructedElement(*child, "", safeDictVar, opts_.xamlPath, out, indent + 1);
            break;
        }
        if (returnedRoot.empty()) {
            returnedRoot = FreshVar("empty");
            Indent(out, indent + 1);
            out << "var " << returnedRoot << " = new Border();\n";
        }
        Indent(out, indent + 1);
        out << "return " << returnedRoot << ";\n";

        inPartialClass_ = prevPartial;
        currentTplParentVar_ = prevTp;

        Indent(out, indent);
        out << "};\n";

        if (const XamlNode* trigProp = tplNode.FindPropertyElement("ControlTemplate", "Triggers")) {
            EmitTriggerCollectionInto(*trigProp, tplVar, dictVar, out, indent);
        }
        return tplVar;
    }

    [[nodiscard]] std::string EmitItemsPanelTemplate(
        const XamlNode& iptNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string iptVar = FreshVar("iptpl");
        Indent(out, indent);
        out << "var " << iptVar << " = new ItemsPanelTemplate(() =>\n";
        Indent(out, indent);
        out << "{\n";
        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;
        const std::string_view safeDictVar =
            aero::str::StartsWith(dictVar, "this.") ? dictVar : std::string_view{};
        std::string panelVar;
        for (const auto& child : iptNode.children) {
            if (ShouldIgnoreXmlElement(*child)) continue;
            panelVar = EmitConstructedElement(*child, "", safeDictVar, opts_.xamlPath, out, indent + 1);
            break;
        }
        if (panelVar.empty()) {
            panelVar = FreshVar("panel");
            Indent(out, indent + 1);
            out << "var " << panelVar << " = new StackPanel();\n";
        }
        Indent(out, indent + 1);
        out << "return (Panel)" << panelVar << ";\n";
        inPartialClass_ = prevPartial;
        Indent(out, indent);
        out << "});\n";
        return iptVar;
    }

    [[nodiscard]] std::string EmitDataTemplate(
        const XamlNode& dtNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::string itemDataType;
        if (const auto* dt = dtNode.FindAttribute("DataType")) {
            itemDataType = StripTargetTypeName(dt->value);
        } else if (const auto* xdt = dtNode.FindXAttribute("DataType")) {
            itemDataType = StripTargetTypeName(xdt->value);
        }

        const std::string dtVar = FreshVar("dtpl");
        const std::string dcParam = FreshVar("__dc");

        Indent(out, indent);
        out << "var " << dtVar << " = new DataTemplate();\n";
        if (!itemDataType.empty()) {
            Indent(out, indent);
            out << dtVar << ".DataType = " << EscapeRsString(itemDataType) << ";\n";
        }
        EmitResourcesPropertyElement(dtNode, "DataTemplate", dtVar, opts_.xamlPath, out, indent);
        Indent(out, indent);
        out << dtVar << ".Factory = (object " << dcParam << ") =>\n";
        Indent(out, indent);
        out << "{\n";

        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;
        const std::string_view safeDictVar =
            aero::str::StartsWith(dictVar, "this.") ? dictVar : std::string_view{};

        std::string returnedRoot;
        for (const auto& child : dtNode.children) {
            if (ShouldIgnoreXmlElement(*child) ||
                child->localName.find('.') != std::string::npos) {
                continue;
            }
            returnedRoot = EmitConstructedElement(
                *child, itemDataType, safeDictVar, opts_.xamlPath, out, indent + 1, dcParam);
            break;
        }
        if (returnedRoot.empty()) {
            returnedRoot = FreshVar("empty");
            Indent(out, indent + 1);
            out << "var " << returnedRoot << " = new Border();\n";
        }
        Indent(out, indent + 1);
        out << returnedRoot << ".DataContext = " << dcParam << ";\n";
        Indent(out, indent + 1);
        out << "return " << returnedRoot << ";\n";

        inPartialClass_ = prevPartial;

        Indent(out, indent);
        out << "};\n";

        if (const XamlNode* trigProp = dtNode.FindPropertyElement("DataTemplate", "Triggers")) {
            EmitTriggerCollectionInto(*trigProp, dtVar, dictVar, out, indent);
        }
        return dtVar;
    }

    [[nodiscard]] std::string EmitStoryboard(
        const XamlNode& sbNode,
        std::ostringstream& out,
        int indent) {
        const std::string sbVar = FreshVar("sb");
        Indent(out, indent);
        out << "var " << sbVar << " = new Storyboard();\n";

        for (const auto& child : sbNode.children) {
            std::string targetName;
            std::string targetProp;
            if (const auto* tn = child->FindAttribute("Storyboard.TargetName")) {
                targetName = tn->value;
            } else if (const auto* tn2 = child->FindAttribute("TargetName")) {
                targetName = tn2->value;
            }
            if (const auto* tp = child->FindAttribute("Storyboard.TargetProperty")) {
                targetProp = NormalizeTargetPropertyPath(tp->value);
            } else if (const auto* tp2 = child->FindAttribute("TargetProperty")) {
                targetProp = NormalizeTargetPropertyPath(tp2->value);
            }

            float beginSec = 0.0f;
            if (const auto* bt = child->FindAttribute("BeginTime")) {
                beginSec = ParseDurationSeconds(bt->value);
            }

            int easeKind = 0;
            int easeMode = 0;
            const XamlNode* easeProp = child->FindPropertyElement(child->localName, "EasingFunction");
            if (easeProp != nullptr && !easeProp->children.empty()) {
                const auto& easeNode = *easeProp->children.front();
                easeKind = MapEasingKind(easeNode.localName);
                if (const auto* em = easeNode.FindAttribute("EasingMode")) {
                    easeMode = MapEasingMode(em->value);
                }
            }
            if (child->localName == "BounceAnimation") easeKind = 9;
            else if (child->localName == "ElasticAnimation") easeKind = 10;
            else if (child->localName == "BackAnimation") easeKind = 8;
            if (const auto* eb = child->FindAttribute("EdgeBehavior")) {
                easeMode = MapEasingMode(eb->value);
            }

            if (child->localName == "DoubleAnimation" ||
                child->localName == "BounceAnimation" ||
                child->localName == "ElasticAnimation" ||
                child->localName == "BackAnimation" ||
                child->localName == "CircleAnimation" ||
                child->localName == "ShakeAnimation") {
                float fromVal = 0.0f;
                float toVal = 1.0f;
                if (const auto* f = child->FindAttribute("From")) {
                    (void)aero::str::TryParseFloat32(f->value, fromVal);
                }
                if (const auto* t = child->FindAttribute("To")) {
                    (void)aero::str::TryParseFloat32(t->value, toVal);
                } else if (child->localName == "CircleAnimation") {
                    if (const auto* rad = child->FindAttribute("Radius")) {
                        (void)aero::str::TryParseFloat32(rad->value, toVal);
                    }
                }
                float durSec = 0.3f;
                if (const auto* d = child->FindAttribute("Duration")) {
                    durSec = ParseDurationSeconds(d->value);
                }
                Indent(out, indent);
                out << sbVar << ".AddDoubleAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatFloatLiteral(fromVal) << ", "
                    << FormatFloatLiteral(toVal) << ", "
                    << FormatFloatLiteral(durSec) << ", "
                    << easeKind << ", " << easeMode << ", "
                    << FormatFloatLiteral(beginSec) << ");\n";
            } else if (child->localName == "ColorAnimation") {
                std::string fromHex = "#FFFFFFFF";
                std::string toHex = "#FFFFFFFF";
                if (const auto* f = child->FindAttribute("From")) {
                    fromHex = ResolveColorHexOrString(f->value);
                }
                if (const auto* t = child->FindAttribute("To")) {
                    toHex = ResolveColorHexOrString(t->value);
                }
                float durSec = 0.3f;
                if (const auto* d = child->FindAttribute("Duration")) {
                    durSec = ParseDurationSeconds(d->value);
                }
                Indent(out, indent);
                out << sbVar << ".AddColorAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << EscapeRsString(fromHex) << ", "
                    << EscapeRsString(toHex) << ", "
                    << FormatFloatLiteral(durSec) << ", "
                    << easeKind << ", " << easeMode << ", "
                    << FormatFloatLiteral(beginSec) << ");\n";
            } else if (child->localName == "PointAnimation") {
                float fx = 0.0f, fy = 0.0f, tx = 0.0f, ty = 0.0f;
                std::vector<float> pts;
                if (const auto* f = child->FindAttribute("From")) {
                    if (TryParseFloatList(f->value, pts) && pts.size() >= 2) {
                        fx = pts[0];
                        fy = pts[1];
                    }
                }
                if (const auto* t = child->FindAttribute("To")) {
                    if (TryParseFloatList(t->value, pts) && pts.size() >= 2) {
                        tx = pts[0];
                        ty = pts[1];
                    }
                }
                float durSec = 0.3f;
                if (const auto* d = child->FindAttribute("Duration")) {
                    durSec = ParseDurationSeconds(d->value);
                }
                Indent(out, indent);
                out << sbVar << ".AddDoubleAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp + ".X") << ", "
                    << FormatFloatLiteral(fx) << ", " << FormatFloatLiteral(tx) << ", "
                    << FormatFloatLiteral(durSec) << ", " << easeKind << ", " << easeMode
                    << ", " << FormatFloatLiteral(beginSec) << ");\n";
                Indent(out, indent);
                out << sbVar << ".AddDoubleAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp + ".Y") << ", "
                    << FormatFloatLiteral(fy) << ", " << FormatFloatLiteral(ty) << ", "
                    << FormatFloatLiteral(durSec) << ", " << easeKind << ", " << easeMode
                    << ", " << FormatFloatLiteral(beginSec) << ");\n";
            } else if (child->localName == "DoubleAnimationUsingKeyFrames") {
                float prevTime = 0.0f;
                float prevVal = 0.0f;
                bool havePrev = false;
                for (const auto& kf : child->children) {
                    float kt = prevTime;
                    if (const auto* ka = kf->FindAttribute("KeyTime")) {
                        kt = ParseDurationSeconds(ka->value);
                    }
                    float val = prevVal;
                    if (const auto* va = kf->FindAttribute("Value")) {
                        (void)aero::str::TryParseFloat32(va->value, val);
                    }
                    int kfEase = 0;
                    int kfMode = 0;
                    const XamlNode* kfEaseProp = kf->FindPropertyElement(kf->localName, "EasingFunction");
                    if (kfEaseProp != nullptr && !kfEaseProp->children.empty()) {
                        kfEase = MapEasingKind(kfEaseProp->children.front()->localName);
                        if (const auto* em = kfEaseProp->children.front()->FindAttribute("EasingMode")) {
                            kfMode = MapEasingMode(em->value);
                        }
                    }
                    const float segDur = std::max(0.001f, kt - prevTime);
                    if ((havePrev && kt > prevTime) || (!havePrev && child->children.size() == 1 && kt > 0.0f)) {
                        Indent(out, indent);
                        out << sbVar << ".AddDoubleAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << FormatFloatLiteral(prevVal) << ", "
                            << FormatFloatLiteral(val) << ", "
                            << FormatFloatLiteral(segDur) << ", "
                            << kfEase << ", " << kfMode << ", "
                            << FormatFloatLiteral(beginSec + prevTime) << ");\n";
                    }
                    prevTime = kt;
                    prevVal = val;
                    havePrev = true;
                }
            } else if (child->localName == "ColorAnimationUsingKeyFrames") {
                float prevTime = 0.0f;
                std::string prevHex = "#FFFFFFFF";
                bool havePrev = false;
                for (const auto& kf : child->children) {
                    float kt = prevTime;
                    if (const auto* ka = kf->FindAttribute("KeyTime")) {
                        kt = ParseDurationSeconds(ka->value);
                    }
                    std::string hex = prevHex;
                    if (const auto* va = kf->FindAttribute("Value")) {
                        hex = ResolveColorHexOrString(va->value);
                    }
                    const float segDur = std::max(0.001f, kt - prevTime);
                    if ((havePrev && kt > prevTime) || (!havePrev && child->children.size() == 1 && kt > 0.0f)) {
                        Indent(out, indent);
                        out << sbVar << ".AddColorAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << EscapeRsString(prevHex) << ", "
                            << EscapeRsString(hex) << ", "
                            << FormatFloatLiteral(segDur) << ", 0, 0, "
                            << FormatFloatLiteral(beginSec + prevTime) << ");\n";
                    }
                    prevTime = kt;
                    prevHex = hex;
                    havePrev = true;
                }
            }
        }
        return sbVar;
    }

    [[nodiscard]] std::string LookupResourceExpr(
        std::string_view key,
        std::string_view dictVar) const {
        if (inPartialClass_) {
            return "this.FindResource(" + EscapeRsString(key) + ")";
        }
        if (!currentTplParentVar_.empty()) {
            return currentTplParentVar_ + ".FindResource(" + EscapeRsString(key) + ")";
        }
        if (!dictVar.empty()) {
            return std::string(dictVar) + ".Get(" + EscapeRsString(key) + ")";
        }
        return "ResourceDictionary.GlobalApplicationResources().Get(" + EscapeRsString(key) + ")";
    }

    [[nodiscard]] std::string FormatGenericScalarLiteral(std::string_view val) const {
        const std::string_view s = aero::str::Trim(val);
        if (aero::str::EqualsIgnoreCase(s, "True")) return "true";
        if (aero::str::EqualsIgnoreCase(s, "False")) return "false";
        float f = 0.0f;
        if (aero::str::TryParseFloat32(s, f)) {
            return FormatFloatLiteral(f);
        }
        return EscapeRsString(val);
    }

    [[nodiscard]] std::string FormatTypedValueLiteral(
        std::string_view propName,
        std::string_view rawVal,
        std::string_view dictVar) const {
        if (LooksLikeMarkupExtension(rawVal)) {
            const auto ext = ParseMarkupExtension(rawVal);
            if (ext.kind == MarkupExtensionKind::XNull) {
                return "null";
            }
            if (ext.kind == MarkupExtensionKind::StaticResource ||
                ext.kind == MarkupExtensionKind::DynamicResource) {
                const std::string key = ResolveResourceKeyFromExtension(ext);
                if (!key.empty()) {
                    if (IsBrushProperty(propName)) {
                        return "(Brush)" + LookupResourceExpr(key, dictVar);
                    }
                    if (propName == "Style" || propName == "ItemContainerStyle") {
                        return "(Style)" + LookupResourceExpr(key, dictVar);
                    }
                    if (propName == "Template") {
                        return "(ControlTemplate)" + LookupResourceExpr(key, dictVar);
                    }
                    if (propName == "ItemTemplate") {
                        return "(DataTemplate)" + LookupResourceExpr(key, dictVar);
                    }
                    if (propName == "ItemsPanel") {
                        return "((ItemsPanelTemplate)" + LookupResourceExpr(key, dictVar) + ").GetFactory()";
                    }
                    if (const auto it = knownGeometries_.find(key); it != knownGeometries_.end()) {
                        return EscapeRsString(it->second);
                    }
                    return LookupResourceExpr(key, dictVar);
                }
            }
            if (ext.kind == MarkupExtensionKind::XType) {
                return EscapeRsString(StripTargetTypeName(rawVal));
            }
            if (ext.kind == MarkupExtensionKind::XStatic) {
                if (const auto* p0 = ext.FindPositional(0)) {
                    return EscapeRsString(p0->literal);
                }
            }
            if (ext.kind == MarkupExtensionKind::NoesisLoc) {
                if (const auto* p0 = ext.FindPositional(0)) {
                    return EscapeRsString(p0->literal);
                }
                if (const auto* rk = ext.FindNamed("ResourceKey")) {
                    return EscapeRsString(rk->literal);
                }
            }
            if (ext.kind == MarkupExtensionKind::EscapedLiteral) {
                return EscapeRsString(ext.literalValue);
            }
            return EscapeRsString(rawVal);
        }

        if (const char* enumType = LookupEnumType(propName)) {
            return std::string(enumType) + "." + std::string(aero::str::Trim(rawVal));
        }
        if (IsBoolProperty(propName)) {
            return aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "True") ? "true" : "false";
        }
        if (IsIntProperty(propName)) {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            return std::to_string(v);
        }
        if (IsFloatProperty(propName)) {
            float f = 0.0f;
            if (aero::str::TryParseFloat32(rawVal, f)) {
                return FormatFloatLiteral(f);
            }
            return "0.0f";
        }
        if (IsThicknessProperty(propName)) {
            return FormatThicknessExpr(rawVal);
        }
        if (propName == "CornerRadius") {
            return FormatCornerRadiusExpr(rawVal);
        }
        if (IsBrushProperty(propName)) {
            return "new SolidColorBrush(" + EscapeRsString(ResolveColorHexOrString(rawVal)) + ")";
        }
        if (propName == "FontWeight" && aero::str::EqualsIgnoreCase(rawVal, "Bold")) {
            return "\"Bold\"";
        }
        return EscapeRsString(rawVal);
    }

    [[nodiscard]] std::string EmitConverterNode(
        const XamlNode& convNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::string typeName = ResolveElementType(convNode);
        if (typeName == "BooleanToVisibilityConverter") typeName = "BoolToVisibilityConverter";
        const std::string cVar = FreshVar("conv");
        Indent(out, indent);
        out << "var " << cVar << " = new " << typeName << "();\n";
        if (typeName == "MathConverter") {
            if (const auto* ex = convNode.FindAttribute("Expression")) {
                std::string expr = ex->value;
                if (LooksLikeMarkupExtension(expr)) {
                    const auto ext = ParseMarkupExtension(expr);
                    if (ext.kind == MarkupExtensionKind::EscapedLiteral) {
                        expr = ext.literalValue;
                    }
                }
                Indent(out, indent);
                out << cVar << ".Expression = " << EscapeRsString(expr) << ";\n";
            }
        } else {
            for (const auto& attr : convNode.attributes) {
                if (attr.isXmlns || attr.prefix == "x") continue;
                EmitPropertyAttributeOrBinding(cVar, typeName, attr.rawName, attr.value, "", dictVar, out, indent);
            }
        }
        return cVar;
    }

    void TryParseBindingNode(
        const XamlNode& bindNode,
        ParsedBindingSpec& outSpec,
        std::string& outConvVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        if (const auto* p = bindNode.FindAttribute("Path")) outSpec.path = p->value;
        if (const auto* en = bindNode.FindAttribute("ElementName")) outSpec.elementName = en->value;
        if (const auto* md = bindNode.FindAttribute("Mode")) outSpec.mode = md->value;
        if (const auto* sf = bindNode.FindAttribute("StringFormat")) {
            outSpec.stringFormat = sf->value;
            if (LooksLikeMarkupExtension(outSpec.stringFormat)) {
                const auto ext = ParseMarkupExtension(outSpec.stringFormat);
                if (ext.kind == MarkupExtensionKind::EscapedLiteral) outSpec.stringFormat = ext.literalValue;
            }
        }
        if (const auto* cp = bindNode.FindAttribute("ConverterParameter")) outSpec.converterParam = cp->value;
        if (const auto* fb = bindNode.FindAttribute("FallbackValue")) {
            outSpec.hasFallbackValue = true;
            outSpec.fallbackValue = fb->value;
        }
        if (const auto* tn = bindNode.FindAttribute("TargetNullValue")) {
            outSpec.hasTargetNullValue = true;
            outSpec.targetNullValue = tn->value;
        }
        if (const auto* ca = bindNode.FindAttribute("Converter")) {
            if (LooksLikeMarkupExtension(ca->value)) {
                const auto ext = ParseMarkupExtension(ca->value);
                if (ext.kind == MarkupExtensionKind::StaticResource || ext.kind == MarkupExtensionKind::DynamicResource) {
                    outSpec.converterKey = ResolveResourceKeyFromExtension(ext);
                }
            }
        }
        if (const auto* rs = bindNode.FindAttribute("RelativeSource")) {
            if (LooksLikeMarkupExtension(rs->value)) {
                const auto ext = ParseMarkupExtension(rs->value);
                if (ext.kind == MarkupExtensionKind::RelativeSource) {
                    outSpec.hasRelativeSource = true;
                    if (const auto* m = ext.FindNamed("Mode")) outSpec.relativeSourceMode = m->literal;
                    else if (const auto* p0 = ext.FindPositional(0)) outSpec.relativeSourceMode = p0->literal;
                    if (const auto* at = ext.FindNamed("AncestorType")) {
                        outSpec.relativeSourceMode = "FindAncestor";
                        outSpec.ancestorType = StripTargetTypeName(at->literal);
                    }
                }
            }
        }
        if (const XamlNode* rsProp = bindNode.FindPropertyElement("Binding", "RelativeSource")) {
            for (const auto& c : rsProp->children) {
                if (c->localName == "RelativeSource") {
                    outSpec.hasRelativeSource = true;
                    if (const auto* m = c->FindAttribute("Mode")) outSpec.relativeSourceMode = m->value;
                    if (const auto* at = c->FindAttribute("AncestorType")) {
                        outSpec.relativeSourceMode = "FindAncestor";
                        outSpec.ancestorType = StripTargetTypeName(at->value);
                    }
                }
            }
        }
        if (const XamlNode* convProp = bindNode.FindPropertyElement("Binding", "Converter")) {
            if (!convProp->children.empty()) {
                outConvVar = EmitConverterNode(*convProp->children.front(), dictVar, out, indent);
            }
        }
    }

    void EmitMultiBindingNode(
        std::string_view targetVar,
        std::string_view targetType,
        std::string_view propName,
        const XamlNode& mbNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string mbVar = FreshVar("mb");
        Indent(out, indent);
        out << "var " << mbVar << " = new MultiBinding();\n";
        if (const auto* sf = mbNode.FindAttribute("StringFormat")) {
            std::string fmt = sf->value;
            if (LooksLikeMarkupExtension(fmt)) {
                const auto ext = ParseMarkupExtension(fmt);
                if (ext.kind == MarkupExtensionKind::EscapedLiteral) fmt = ext.literalValue;
            }
            Indent(out, indent);
            out << mbVar << ".StringFormat = " << EscapeRsString(fmt) << ";\n";
        }
        if (const auto* cp = mbNode.FindAttribute("ConverterParameter")) {
            Indent(out, indent);
            out << mbVar << ".ConverterParameter = " << EscapeRsString(cp->value) << ";\n";
        }
        if (const auto* ca = mbNode.FindAttribute("Converter")) {
            if (LooksLikeMarkupExtension(ca->value)) {
                const auto ext = ParseMarkupExtension(ca->value);
                const std::string rk = ResolveResourceKeyFromExtension(ext);
                if (!rk.empty()) {
                    Indent(out, indent);
                    out << mbVar << ".Converter = (IMultiValueConverter)" << LookupResourceExpr(rk, dictVar) << ";\n";
                }
            }
        }
        if (const XamlNode* convProp = mbNode.FindPropertyElement("MultiBinding", "Converter")) {
            if (!convProp->children.empty()) {
                const std::string cVar = EmitConverterNode(*convProp->children.front(), dictVar, out, indent);
                Indent(out, indent);
                out << mbVar << ".Converter = (IMultiValueConverter)" << cVar << ";\n";
            }
        }
        for (const auto& ch : mbNode.children) {
            if (ch->localName != "Binding") continue;
            ParsedBindingSpec subSpec;
            std::string subConv;
            TryParseBindingNode(*ch, subSpec, subConv, dictVar, out, indent);
            const std::string bVar = FreshVar("subb");
            Indent(out, indent);
            out << "var " << bVar << " = new Binding(" << EscapeRsString(subSpec.path) << ");\n";
            if (!subSpec.elementName.empty()) {
                Indent(out, indent);
                out << bVar << ".ElementName = " << EscapeRsString(subSpec.elementName) << ";\n";
            }
            if (!subSpec.relativeSourceMode.empty()) {
                Indent(out, indent);
                out << bVar << ".RelativeSourceMode = " << EscapeRsString(subSpec.relativeSourceMode) << ";\n";
            }
            if (!subConv.empty()) {
                Indent(out, indent);
                out << bVar << ".Converter = " << subConv << ";\n";
            }
            if (subSpec.hasFallbackValue) {
                Indent(out, indent);
                out << bVar << ".FallbackBox = " << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, subSpec.fallbackValue, dictVar)) << ";\n";
            }
            Indent(out, indent);
            out << mbVar << ".AddBinding(" << bVar << ");\n";
        }
        const std::string bindTarget = (targetType == "Run")
            ? ("(UIElement)" + std::string(targetVar))
            : std::string(targetVar);
        Indent(out, indent);
        out << "BindingEngine.BindMultiBinding(" << bindTarget << ", " << EscapeRsString(propName) << ", " << mbVar << ");\n";
    }

    void EmitPropertyAttributeOrBinding(
        std::string_view targetVar,
        std::string_view targetType,
        std::string_view propName,
        std::string_view rawVal,
        std::string_view xDataType,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::string_view explicitBindSourceVar = {}) {
        if (targetType == "TextBlock") {
            if (aero::str::EndsWith(propName, "Text.Stroke")) {
                propName = "Stroke";
            } else if (aero::str::EndsWith(propName, "Text.StrokeThickness")) {
                propName = "StrokeThickness";
            } else if (propName == "noesis:RichText.Text" || propName == "RichText.Text") {
                propName = "RichText.Text";
            }
        }
        if (aero::str::EndsWith(propName, "Text.Placeholder")) {
            propName = "Placeholder";
        }
        if (propName == "noesis:Loc.Source" || propName == "Loc.Source") {
            propName = "Loc.Source";
            EmitSiblingLanguageDictionaries(out, indent);
        }
        std::string ownedRawVal;
        if (LooksLikeMarkupExtension(rawVal)) {
            const auto ext = ParseMarkupExtension(rawVal);
            if (!ext.IsOk()) {
                diagnostics_.push_back("Invalid markup extension '" + std::string(rawVal) + "': " + ext.errorMessage);
                return;
            }
            if (ext.kind == MarkupExtensionKind::EscapedLiteral) {
                ownedRawVal = ext.literalValue;
                rawVal = ownedRawVal;
            } else if (ext.kind == MarkupExtensionKind::Binding ||
                       ext.kind == MarkupExtensionKind::TemplateBinding ||
                       ext.kind == MarkupExtensionKind::MultiBinding) {
                ParsedBindingSpec spec;
                if (TryExtractBindingSpec(ext, spec)) {
                    EmitBindingSpec(
                        targetVar, targetType, propName, spec, xDataType, out, indent, explicitBindSourceVar);
                }
                return;
            } else if (ext.kind == MarkupExtensionKind::XNull) {
                if (propName == "FocusVisualStyle" || propName == "Style" || propName == "Template") {
                    Indent(out, indent);
                    out << targetVar << "." << propName << " = null;\n";
                }
                return;
            } else if (ext.kind == MarkupExtensionKind::StaticResource ||
                       ext.kind == MarkupExtensionKind::DynamicResource) {
                const std::string key = ResolveResourceKeyFromExtension(ext);
                if (!key.empty()) {
                    const std::string lookup = LookupResourceExpr(key, dictVar);
                    if (propName == "Style" || propName == "ItemContainerStyle") {
                        Indent(out, indent);
                        out << targetVar << "." << propName << " = (Style)" << lookup << ";\n";
                    } else if (IsBrushProperty(propName)) {
                        Indent(out, indent);
                        out << targetVar << "." << propName << " = (Brush)" << lookup << ";\n";
                    } else if (propName == "Data" && knownGeometries_.count(key) > 0) {
                        Indent(out, indent);
                        out << targetVar << ".Data = " << EscapeRsString(knownGeometries_.at(key)) << ";\n";
                    } else if (propName == "Content") {
                        Indent(out, indent);
                        out << targetVar << ".SetContent(" << lookup << ");\n";
                    } else if (propName == "Header") {
                        Indent(out, indent);
                        out << targetVar << ".SetHeader(" << lookup << ");\n";
                    } else if (propName == "Template") {
                        Indent(out, indent);
                        out << targetVar << ".Template = (ControlTemplate)" << lookup << ";\n";
                    } else if (propName == "ItemTemplate") {
                        Indent(out, indent);
                        out << targetVar << ".ItemTemplate = (DataTemplate)" << lookup << ";\n";
                    } else if (propName == "ItemsPanel") {
                        Indent(out, indent);
                        out << targetVar << ".ItemsPanel = ((ItemsPanelTemplate)" << lookup << ").GetFactory();\n";
                    } else if (propName == "Storyboard") {
                        // Handled at action level
                    } else if (IsDirectUIProperty(propName)) {
                        Indent(out, indent);
                        out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                            << ", DependencyObject.BoxObject(" << lookup << "));\n";
                    }
                    if (ext.kind == MarkupExtensionKind::DynamicResource && targetType != "Run") {
                        Indent(out, indent);
                        out << targetVar << ".SetResourceReference(" << EscapeRsString(propName)
                            << ", " << EscapeRsString(key) << ");\n";
                    }
                }
                return;
            } else if (ext.kind == MarkupExtensionKind::XType) {
                ownedRawVal = StripTargetTypeName(rawVal);
                rawVal = ownedRawVal;
            } else if (ext.kind == MarkupExtensionKind::XStatic) {
                if (const auto* k = ext.FindPositional(0)) {
                    ownedRawVal = k->literal;
                    rawVal = ownedRawVal;
                }
            } else if (ext.kind == MarkupExtensionKind::NoesisLoc) {
                std::string key;
                if (const auto* k = ext.FindPositional(0)) {
                    key = k->literal;
                } else if (const auto* rk = ext.FindNamed("ResourceKey")) {
                    key = rk->literal;
                }
                if (!key.empty()) {
                    if (targetType == "Run") {
                        Indent(out, indent);
                        out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                            << ", DependencyObject.BoxObject(" << LookupResourceExpr(key, dictVar) << "));\n";
                    } else {
                        Indent(out, indent);
                        out << targetVar << ".SetResourceReference(" << EscapeRsString(propName)
                            << ", " << EscapeRsString(key) << ");\n";
                    }
                }
                return;
            }
        }

        // Attached & Grid shorthand property checks first
        if (propName == "Grid.Row" || propName == "Row") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridRow(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.Column" || propName == "Column") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridColumn(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.RowSpan" || propName == "RowSpan") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridRowSpan(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.ColumnSpan" || propName == "ColumnSpan") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridColumnSpan(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Panel.ZIndex") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetZIndex(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "DockPanel.Dock") {
            Indent(out, indent);
            out << "UIElement.SetDock(" << targetVar << ", Dock." << rawVal << ");\n";
            return;
        }
        if (propName == "Canvas.Left" || propName == "Canvas.Top" ||
            propName == "Canvas.Right" || propName == "Canvas.Bottom") {
            float f = 0.0f;
            (void)aero::str::TryParseFloat32(rawVal, f);
            const std::string method = "SetCanvas" + std::string(propName.substr(7));
            Indent(out, indent);
            out << "UIElement." << method << "(" << targetVar << ", " << FormatFloatLiteral(f) << ");\n";
            return;
        }
        if (propName == "TextElement.FontSize" || propName == "TextBlock.FontSize") {
            float f = 14.0f;
            (void)aero::str::TryParseFloat32(rawVal, f);
            Indent(out, indent);
            out << "TextElement.SetFontSize(" << targetVar << ", " << FormatFloatLiteral(f) << ");\n";
            return;
        }
        if (propName == "TextElement.FontFamily" || propName == "TextBlock.FontFamily") {
            Indent(out, indent);
            out << "TextElement.SetFontFamily(" << targetVar << ", " << EscapeRsString(rawVal) << ");\n";
            return;
        }
        if (propName == "TextElement.FontWeight" || propName == "TextBlock.FontWeight") {
            Indent(out, indent);
            out << "TextElement.SetFontWeight(" << targetVar << ", " << EscapeRsString(rawVal) << ");\n";
            return;
        }
        if (propName == "TextElement.Foreground" || propName == "TextBlock.Foreground") {
            Indent(out, indent);
            out << "TextElement.SetForeground(" << targetVar << ", "
                << FormatTypedValueLiteral("Foreground", rawVal, dictVar) << ");\n";
            return;
        }
        if (aero::str::EndsWith(propName, "Element.BlendingMode")) {
            Indent(out, indent);
            out << "Element.SetBlendingMode(" << targetVar << ", EnumConvert.ParseBlendingMode("
                << EscapeRsString(rawVal) << "));\n";
            return;
        }
        if (aero::str::EndsWith(propName, "RiveControl.Source")) {
            Indent(out, indent);
            out << "RiveControl.SetSource(" << targetVar << ", " << EscapeRsString(rawVal) << ");\n";
            return;
        }

        // Events (`Click="Handler"`)
        if (IsEventAttribute(propName)) {
            if (inPartialClass_) {
                Indent(out, indent);
                out << targetVar << "." << propName << " += this." << aero::str::Trim(rawVal) << ";\n";
            }
            return;
        }

        // Content & Header use typed overloads on Aero.UI controls (`RS5151` safe)
        if (propName == "Content") {
            Indent(out, indent);
            out << targetVar << ".SetContent(" << FormatGenericScalarLiteral(rawVal) << ");\n";
            return;
        }
        if (propName == "Header") {
            Indent(out, indent);
            out << targetVar << ".SetHeader(" << FormatGenericScalarLiteral(rawVal) << ");\n";
            return;
        }

        // Skip WPF/Noesis Width="Auto" / Height="Auto" on non-Grid-track UIElements
        if ((propName == "Width" || propName == "Height") &&
            aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "Auto")) {
            return;
        }

        if (propName == "Title" && targetType == "Window") {
            Indent(out, indent);
            out << targetVar << ".Title = " << EscapeRsString(rawVal) << ";\n";
            return;
        }

        if (IsDirectUIProperty(propName) && propName != "Title") {
            Indent(out, indent);
            out << targetVar << "." << propName << " = "
                << FormatTypedValueLiteral(propName, rawVal, dictVar) << ";\n";
            return;
        }

        // Grid compact shorthand attributes `RowDefinitions="Auto,*"`
        if (targetType == "Grid" &&
            (propName == "RowDefinitions" || propName == "ColumnDefinitions")) {
            const std::string method = (propName == "RowDefinitions")
                ? "AddRowDefinition"
                : "AddColumnDefinition";
            std::size_t start = 0;
            while (start <= rawVal.size()) {
                std::size_t comma = rawVal.find(',', start);
                if (comma == std::string_view::npos) comma = rawVal.size();
                const auto [u, fv] = ParseGridLength(rawVal.substr(start, comma - start));
                Indent(out, indent);
                out << targetVar << "." << method << "(" << u << ", " << fv << ");\n";
                if (comma == rawVal.size()) break;
                start = comma + 1;
            }
            return;
        }

        // Generic scalar metadata props (`Tag`, `ToolTip`, `Placeholder`, `CommandParameter`, `Custom.*`, etc.)
        if (aero::str::StartsWith(propName, "Custom.")) {
            Indent(out, indent);
            if (aero::str::EndsWith(propName, "_Int32") || aero::str::EndsWith(propName, "_Int")) {
                const long iv = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxInt(" << iv << "));\n";
            } else {
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxString(" << EscapeRsString(rawVal) << "));\n";
            }
            return;
        }
        if (propName == "Tag" || propName == "ToolTip" || propName == "Title" ||
            propName == "InputGestureText" || propName == "CommandParameter" ||
            propName == "Placeholder" || propName == "FontWeight" || propName == "DisplayMemberPath" ||
            propName == "Loc.Source") {
            Indent(out, indent);
            out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName) << ", "
                << BoxSetterValueExpr(propName, FormatGenericScalarLiteral(rawVal)) << ");\n";
        }
    }

    void EmitSiblingLanguageDictionaries(std::ostringstream& out, int indent) {
        if (emittedLocSources_) {
            return;
        }
        emittedLocSources_ = true;

        std::vector<std::filesystem::path> dirs;
        if (!opts_.xamlPath.empty()) {
            const auto p = std::filesystem::path(opts_.xamlPath).parent_path();
            if (!p.empty()) dirs.push_back(p);
        }
        if (!opts_.resourceSearchRoot.empty()) {
            dirs.emplace_back(opts_.resourceSearchRoot);
        }
        for (const auto& sp : opts_.resourceSearchPaths) {
            if (!sp.empty()) dirs.emplace_back(sp);
        }

        std::vector<std::filesystem::path> langFiles;
        std::unordered_set<std::string> seenNames;
        std::error_code ec;
        for (const auto& dir : dirs) {
            if (!std::filesystem::is_directory(dir, ec)) {
                continue;
            }
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                const std::string fname = entry.path().filename().string();
                std::string lower = fname;
                for (char& ch : lower) {
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                }
                if (aero::str::StartsWith(lower, "language-") && aero::str::EndsWith(lower, ".xaml")) {
                    if (seenNames.insert(lower).second) {
                        langFiles.push_back(entry.path());
                    }
                }
            }
        }
        std::sort(langFiles.begin(), langFiles.end());

        for (const auto& lp : langFiles) {
            const std::string content = ReadFileTextUtf8(lp);
            if (content.empty()) {
                continue;
            }
            std::unique_ptr<XamlNode> langRoot;
            if (!BuildXamlDom(content, langRoot, diagnostics_) || !langRoot) {
                continue;
            }
            const std::string dv = FreshVar("locDict");
            Indent(out, indent);
            out << "var " << dv << " = new ResourceDictionary();\n";
            EmitResourceDictionaryEntries(*langRoot, lp.generic_string(), dv, out, indent);
            Indent(out, indent);
            out << "ResourceDictionary.RegisterLocSource(" << EscapeRsString(lp.filename().string())
                << ", " << dv << ");\n";
        }
    }

    void EmitBindingSpec(
        std::string_view targetVar,
        std::string_view targetType,
        std::string_view propName,
        const ParsedBindingSpec& spec,
        std::string_view xDataType,
        std::ostringstream& out,
        int indent,
        std::string_view explicitSourceVar,
        std::string_view inlineConvExpr = {}) {
        if (spec.hasFallbackValue && IsDirectUIProperty(propName)) {
            Indent(out, indent);
            out << targetVar << "." << propName << " = "
                << FormatTypedValueLiteral(propName, spec.fallbackValue, {}) << ";\n";
        }

        const std::string cleanPath = NormalizeBindingPath(spec.path);
        const bool needExtended =
            !inlineConvExpr.empty() || spec.hasFallbackValue || spec.hasTargetNullValue ||
            !spec.ancestorType.empty() ||
            (spec.hasRelativeSource && spec.relativeSourceMode != "TemplatedParent");

        const std::string convArg = inlineConvExpr.empty() ? std::string("null") : std::string(inlineConvExpr);
        const std::string fbHas = spec.hasFallbackValue ? "true" : "false";
        const std::string fbBox = spec.hasFallbackValue
            ? BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, spec.fallbackValue, {}))
            : std::string("null");
        const std::string tnHas = spec.hasTargetNullValue ? "true" : "false";
        const std::string tnBox = spec.hasTargetNullValue
            ? BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, spec.targetNullValue, {}))
            : std::string("null");

        int modeInt = MapBindingMode(spec.mode);
        if (spec.mode.empty()) {
            if ((targetType == "TextBox" && propName == "Text") ||
                (targetType == "Slider" && propName == "Value") ||
                ((targetType == "CheckBox" || targetType == "ToggleButton" || targetType == "RadioButton") &&
                 propName == "IsChecked") ||
                ((targetType == "ComboBox" || targetType == "ListBox" || targetType == "Selector" ||
                  targetType == "ListView" || targetType == "TabControl") &&
                 (propName == "SelectedItem" || propName == "SelectedIndex" || propName == "SelectedValue"))) {
                modeInt = 1; // TwoWay default for interactive input/selection properties
            }
        }
        if (targetType == "Run") {
            Indent(out, indent);
            out << "BindingEngine.BindInlineEx(" << targetVar << ", "
                << EscapeRsString(propName) << ", "
                << EscapeRsString(cleanPath) << ", "
                << EscapeRsString(spec.elementName) << ", "
                << modeInt << ", "
                << convArg << ", "
                << EscapeRsString(spec.converterKey) << ", "
                << EscapeRsString(spec.converterParam) << ", "
                << EscapeRsString(spec.stringFormat) << ", "
                << fbHas << ", " << fbBox << ", "
                << tnHas << ", " << tnBox << ", "
                << EscapeRsString(spec.relativeSourceMode) << ", "
                << EscapeRsString(spec.ancestorType) << ");\n";
            return;
        }

        if (spec.isTemplateBinding ||
            (spec.hasRelativeSource && spec.relativeSourceMode == "TemplatedParent")) {
            if (!currentTplParentVar_.empty()) {
                Indent(out, indent);
                if (!needExtended && spec.converterKey.empty() && spec.stringFormat.empty() && spec.converterParam.empty()) {
                    out << "BindingEngine.BindTemplateParent(" << targetVar << ", "
                        << EscapeRsString(propName) << ", " << currentTplParentVar_ << ", "
                        << EscapeRsString(cleanPath) << ");\n";
                } else {
                    out << "BindingEngine.BindTemplateParentEx(" << targetVar << ", "
                        << EscapeRsString(propName) << ", " << currentTplParentVar_ << ", "
                        << EscapeRsString(cleanPath) << ", "
                        << convArg << ", "
                        << EscapeRsString(spec.converterKey) << ", "
                        << EscapeRsString(spec.converterParam) << ", "
                        << EscapeRsString(spec.stringFormat) << ", "
                        << fbHas << ", " << fbBox << ", "
                        << tnHas << ", " << tnBox << ");\n";
                }
            }
            return;
        }

        if (!spec.elementName.empty()) {
            Indent(out, indent);
            if (!needExtended && spec.converterKey.empty() && spec.stringFormat.empty() && spec.converterParam.empty()) {
                out << "BindingEngine.BindElement(" << targetVar << ", "
                    << EscapeRsString(propName) << ", "
                    << EscapeRsString(spec.elementName) << ", "
                    << EscapeRsString(cleanPath) << ", " << modeInt << ");\n";
            } else {
                out << "BindingEngine.BindElementEx(" << targetVar << ", "
                    << EscapeRsString(propName) << ", "
                    << EscapeRsString(spec.elementName) << ", "
                    << EscapeRsString(cleanPath) << ", " << modeInt << ", "
                    << convArg << ", "
                    << EscapeRsString(spec.converterKey) << ", "
                    << EscapeRsString(spec.converterParam) << ", "
                    << EscapeRsString(spec.stringFormat) << ", "
                    << fbHas << ", " << fbBox << ", "
                    << tnHas << ", " << tnBox << ");\n";
            }
            return;
        }

        const std::string bindTarget = std::string(targetVar);

        std::string srcExpr = !explicitSourceVar.empty()
            ? std::string(explicitSourceVar)
            : std::string("null");
        if (spec.hasRelativeSource && spec.ancestorType.empty()) {
            if (spec.relativeSourceMode == "Self") {
                srcExpr = std::string(targetVar);
            } else if (inPartialClass_) {
                srcExpr = "this";
            }
        }

        if (!xDataType.empty() && !explicitSourceVar.empty() &&
            !cleanPath.empty() && cleanPath.find('.') == std::string::npos &&
            spec.converterKey.empty() && spec.stringFormat.empty() && inlineConvExpr.empty()) {
            const std::string typedVar = FreshVar("__vm");
            Indent(out, indent);
            out << "if (" << explicitSourceVar << " is " << xDataType << " " << typedVar << ") { ";
            if (propName == "Content") {
                out << targetVar << ".SetContent(" << typedVar << "." << cleanPath << ");";
            } else if (IsDirectUIProperty(propName)) {
                out << targetVar << "." << propName << " = " << typedVar << "." << cleanPath << ";";
            } else {
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxObject(" << typedVar << "." << cleanPath << "));";
            }
            out << " }\n";
        }

        Indent(out, indent);
        if (!needExtended) {
            out << "BindingEngine.BindPath(" << bindTarget << ", "
                << EscapeRsString(propName) << ", "
                << srcExpr << ", "
                << EscapeRsString(cleanPath) << ", "
                << modeInt << ", "
                << EscapeRsString(spec.converterKey) << ", "
                << EscapeRsString(spec.converterParam) << ", "
                << EscapeRsString(spec.stringFormat) << ");\n";
        } else {
            out << "BindingEngine.BindPathEx(" << bindTarget << ", "
                << EscapeRsString(propName) << ", "
                << srcExpr << ", "
                << EscapeRsString(cleanPath) << ", "
                << modeInt << ", "
                << convArg << ", "
                << EscapeRsString(spec.converterKey) << ", "
                << EscapeRsString(spec.converterParam) << ", "
                << EscapeRsString(spec.stringFormat) << ", "
                << fbHas << ", " << fbBox << ", "
                << tnHas << ", " << tnBox << ", "
                << EscapeRsString(spec.relativeSourceMode) << ", "
                << EscapeRsString(spec.ancestorType) << ");\n";
        }
    }

    [[nodiscard]] std::string EmitConstructedElement(
        const XamlNode& node,
        std::string_view xDataType,
        std::string_view dictVar,
        std::string_view currentXamlPath,
        std::ostringstream& out,
        int indent,
        std::string_view explicitBindSourceVar = {}) {
        const std::string typeName = ResolveElementType(node);
        const std::string varName = FreshVar("el");
        Indent(out, indent);
        out << "var " << varName << " = new " << typeName << "();\n";

        EmitElementBody(
            node, varName, typeName, typeName, xDataType, dictVar, currentXamlPath, out, indent,
            explicitBindSourceVar);
        return varName;
    }

    void EmitElementBody(
        const XamlNode& node,
        std::string_view targetVar,
        std::string_view xmlLocalName,
        std::string_view resolvedTypeName,
        std::string_view xDataType,
        std::string_view dictVar,
        std::string_view currentXamlPath,
        std::ostringstream& out,
        int indent,
        std::string_view explicitBindSourceVar = {}) {
        // Assign x:Name / Name and typed partial class field
        const std::string elemName = node.GetName();
        if (!elemName.empty()) {
            Indent(out, indent);
            out << targetVar << ".Name = " << EscapeRsString(elemName) << ";\n";
            RegisterNamedField(elemName, std::string(resolvedTypeName));
            if (inPartialClass_ && targetVar != "this") {
                Indent(out, indent);
                out << "this." << elemName << " = " << targetVar << ";\n";
            }
        }

        // Resources property element before child properties so StaticResource references resolve
        EmitResourcesPropertyElement(node, xmlLocalName, targetVar, currentXamlPath, out, indent);

        // Emit regular attributes
        for (const auto& attr : node.attributes) {
            if (attr.isXmlns || IsDesignOrMarkupCompatPrefix(attr.prefix, attr.namespaceUri)) {
                continue;
            }
            if (attr.prefix == "x" && (attr.localName == "Class" || attr.localName == "Name" ||
                                       attr.localName == "Key" || attr.localName == "DataType" ||
                                       attr.localName == "TypeArguments")) {
                continue;
            }
            if (attr.rawName == "Name" || attr.rawName == "StartupUri") {
                continue;
            }
            EmitPropertyAttributeOrBinding(
                targetVar, resolvedTypeName, attr.rawName, attr.value,
                xDataType, dictVar, out, indent, explicitBindSourceVar);
        }

        // Grid RowDefinitions / ColumnDefinitions
        if (const auto* rDefs = node.FindPropertyElement(xmlLocalName, "RowDefinitions")) {
            for (const auto& rd : rDefs->children) {
                if (rd->localName != "RowDefinition") continue;
                std::string rawH = "*";
                if (const auto* h = rd->FindAttribute("Height")) rawH = h->value;
                const auto [unit, val] = ParseGridLength(rawH);
                Indent(out, indent);
                out << targetVar << ".AddRowDefinition(" << unit << ", " << val << ");\n";
            }
        }
        if (const auto* cDefs = node.FindPropertyElement(xmlLocalName, "ColumnDefinitions")) {
            for (const auto& cd : cDefs->children) {
                if (cd->localName != "ColumnDefinition") continue;
                std::string rawW = "*";
                if (const auto* w = cd->FindAttribute("Width")) rawW = w->value;
                const auto [unit, val] = ParseGridLength(rawW);
                Indent(out, indent);
                out << targetVar << ".AddColumnDefinition(" << unit << ", " << val << ");\n";
            }
        }

        // Property elements for brushes, transforms, projections, triggers, behaviors, bindings
        for (const auto& [propKey, propNode] : node.propertyElements) {
            const std::size_t dotPos = propKey.find('.');
            const std::string shortProp = (dotPos != std::string::npos)
                ? propKey.substr(dotPos + 1)
                : propKey;
            if (shortProp == "Resources" || shortProp == "RowDefinitions" ||
                shortProp == "ColumnDefinitions" || shortProp == "OpacityMask" ||
                propKey == "Navigation.Destinations") {
                continue;
            }
            if (shortProp == "InputBindings") {
                EmitInputBindings(*propNode, targetVar, out, indent);
                continue;
            }
            if (propKey == "Interaction.Triggers") {
                EmitInteractionTriggers(*propNode, targetVar, dictVar, out, indent);
                continue;
            }
            if (propKey == "Interaction.Behaviors") {
                EmitInteractionBehaviors(*propNode, targetVar, dictVar, out, indent);
                continue;
            }
            if (shortProp == "Inputs" && resolvedTypeName == "RiveControl") {
                for (const auto& riNode : propNode->children) {
                    if (riNode->localName == "RiveInput") {
                        EmitRiveInput(*riNode, targetVar, out, indent);
                    }
                }
                continue;
            }
            if ((propKey == "Element.Projection3D" || shortProp == "Projection3D") &&
                !propNode->children.empty()) {
                EmitProjection3D(*propNode->children.front(), targetVar, out, indent);
                continue;
            }
            if ((propKey == "Element.Transform3D" || shortProp == "Transform3D") &&
                !propNode->children.empty()) {
                EmitTransform3D(*propNode->children.front(), targetVar, out, indent);
                continue;
            }
            // Property-element <Binding> or <MultiBinding> on ANY property
            if (!propNode->children.empty()) {
                const std::string& bindPropName =
                    IsAttachedBindableProperty(propKey) ? propKey : shortProp;
                const auto& firstChild = *propNode->children.front();
                if (firstChild.localName == "Binding") {
                    ParsedBindingSpec spec;
                    std::string convVar;
                    TryParseBindingNode(firstChild, spec, convVar, dictVar, out, indent);
                    EmitBindingSpec(
                        targetVar, resolvedTypeName, bindPropName, spec, xDataType, out, indent,
                        explicitBindSourceVar, convVar);
                    continue;
                }
                if (firstChild.localName == "MultiBinding") {
                    EmitMultiBindingNode(
                        targetVar, resolvedTypeName, bindPropName, firstChild, dictVar, out, indent);
                    continue;
                }
            }
            if (IsBrushProperty(shortProp) && !propNode->children.empty()) {
                const auto& brushChild = *propNode->children.front();
                const std::string bVar = EmitBrushObject(
                    brushChild, shortProp, dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << "." << shortProp << " = " << bVar << ";\n";
                if (brushChild.localName == "SolidColorBrush") {
                    if (const auto* colAttr = brushChild.FindAttribute("Color");
                        colAttr != nullptr && LooksLikeMarkupExtension(colAttr->value)) {
                        const auto ext = ParseMarkupExtension(colAttr->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName, shortProp + ".Color", bSpec,
                                xDataType, out, indent, explicitBindSourceVar);
                        }
                    }
                }
                continue;
            }
            if (shortProp == "RenderTransform" && !propNode->children.empty()) {
                const std::string trVar = EmitTransformObject(*propNode->children.front(), out, indent);
                if (!trVar.empty()) {
                    Indent(out, indent);
                    out << targetVar << ".RenderTransform = " << trVar << ";\n";
                }
                continue;
            }
            if (shortProp == "Effect" && !propNode->children.empty()) {
                const std::string efVar = EmitEffectObject(
                    *propNode->children.front(), targetVar, out, indent);
                if (!efVar.empty()) {
                    Indent(out, indent);
                    out << targetVar << ".Effect = " << efVar << ";\n";
                }
                continue;
            }
            if (shortProp == "Template" && !propNode->children.empty()) {
                const std::string tplVar = EmitControlTemplate(*propNode->children.front(), dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << ".Template = " << tplVar << ";\n";
                continue;
            }
            if (shortProp == "ItemTemplate" && !propNode->children.empty()) {
                const std::string dtVar = EmitDataTemplate(*propNode->children.front(), dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << ".ItemTemplate = " << dtVar << ";\n";
                continue;
            }
            if (shortProp == "ItemsPanel" && !propNode->children.empty() &&
                propNode->children.front()->localName == "ItemsPanelTemplate") {
                const std::string iptVar = EmitItemsPanelTemplate(*propNode->children.front(), dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << ".ItemsPanel = " << iptVar << ".GetFactory();\n";
                continue;
            }
            if ((shortProp == "DecreaseRepeatButton" || shortProp == "IncreaseRepeatButton" ||
                 shortProp == "Thumb") && !propNode->children.empty()) {
                const std::string sub = EmitConstructedElement(
                    *propNode->children.front(), xDataType, dictVar, currentXamlPath, out, indent,
                    explicitBindSourceVar);
                Indent(out, indent);
                out << targetVar << "." << shortProp << " = " << sub << ";\n";
                continue;
            }
            if ((shortProp == "Content" || shortProp == "Header" || shortProp == "Icon" ||
                 shortProp == "ToolTip") && !propNode->children.empty()) {
                const std::string sub = EmitConstructedElement(
                    *propNode->children.front(), xDataType, dictVar, currentXamlPath, out, indent,
                    explicitBindSourceVar);
                Indent(out, indent);
                if (shortProp == "Content") {
                    out << targetVar << ".SetContent(" << sub << ");\n";
                } else if (shortProp == "Header") {
                    out << targetVar << ".SetHeader(" << sub << ");\n";
                } else {
                    out << targetVar << ".SetPropertyObject(" << EscapeRsString(shortProp) << ", " << sub << ");\n";
                }
                continue;
            }
            if (shortProp == "View" && !propNode->children.empty() &&
                propNode->children.front()->localName == "GridView") {
                EmitGridView(*propNode->children.front(), targetVar, xDataType, out, indent);
                continue;
            }
            if (shortProp == "Triggers") {
                EmitElementTriggers(*propNode, targetVar, dictVar, out, indent);
            }
        }

        // Text / Runs vs Child elements
        if (resolvedTypeName == "TextBlock") {
            bool hasRunChildren = false;
            for (const auto& ch : node.children) {
                if (ch->localName == "Run") {
                    hasRunChildren = true;
                    break;
                }
            }
            if (hasRunChildren) {
                for (const auto& ch : node.children) {
                    if (ch->localName != "Run") continue;
                    const std::string rVar = EmitRunElement(*ch, xDataType, dictVar, out, indent);
                    Indent(out, indent);
                    out << targetVar << ".AddRun(" << rVar << ");\n";
                }
            } else if (!node.textContent.empty() && node.FindAttribute("Text") == nullptr) {
                Indent(out, indent);
                out << targetVar << ".Text = " << EscapeRsString(node.textContent) << ";\n";
            }
            return;
        }
        if ((resolvedTypeName == "Path" || resolvedTypeName == "Geometry") &&
            !node.textContent.empty() && node.FindAttribute("Data") == nullptr) {
            Indent(out, indent);
            out << targetVar << ".Data = " << EscapeRsString(aero::str::Trim(node.textContent)) << ";\n";
            return;
        }

        // Regular visual / item children (including direct RiveInput children on RiveControl)
        for (const auto& ch : node.children) {
            if (ShouldIgnoreXmlElement(*ch)) {
                continue;
            }
            if (resolvedTypeName == "RiveControl" && ch->localName == "RiveInput") {
                EmitRiveInput(*ch, targetVar, out, indent);
                continue;
            }
            if (ch->localName == "String" || ch->localName == "Color" ||
                ch->localName == "Double" || ch->localName == "Single" ||
                ch->localName == "Int32" || ch->localName == "Int64" ||
                ch->localName == "Boolean") {
                std::string boxExpr;
                const std::string_view rawTxt = aero::str::Trim(ch->textContent);
                if (ch->localName == "Double" || ch->localName == "Single") {
                    float fv = 0.0f;
                    (void)aero::str::TryParseFloat32(rawTxt, fv);
                    boxExpr = "DependencyObject.BoxFloat(" + FormatFloatLiteral(fv) + ")";
                } else if (ch->localName == "Int32" || ch->localName == "Int64") {
                    const long iv = std::strtol(std::string(rawTxt).c_str(), nullptr, 10);
                    boxExpr = "DependencyObject.BoxInt(" + std::to_string(iv) + ")";
                } else if (ch->localName == "Boolean") {
                    const bool bv = aero::str::EqualsIgnoreCase(rawTxt, "True");
                    boxExpr = std::string("DependencyObject.BoxBool(") + (bv ? "true" : "false") + ")";
                } else {
                    boxExpr = "DependencyObject.BoxString(" + EscapeRsString(rawTxt) + ")";
                }
                if (IsItemsControlType(resolvedTypeName)) {
                    Indent(out, indent);
                    out << targetVar << ".AddItemBox(" << boxExpr << ");\n";
                } else if (IsContentControlType(resolvedTypeName)) {
                    Indent(out, indent);
                    out << targetVar << ".SetContentBox(" << boxExpr << ");\n";
                }
                continue;
            }
            const std::string childVar = EmitConstructedElement(
                *ch, xDataType, dictVar, currentXamlPath, out, indent, explicitBindSourceVar);
            Indent(out, indent);
            if (IsItemsControlType(resolvedTypeName)) {
                out << targetVar << ".AddItem(" << childVar << ");\n";
            } else if (IsContentControlType(resolvedTypeName)) {
                out << targetVar << ".SetContent(" << childVar << ");\n";
            } else {
                out << targetVar << ".AddChild(" << childVar << ");\n";
            }
        }

        // Direct text inside ContentControl (<Button>OK</Button>)
        if (node.children.empty() && !node.textContent.empty() &&
            IsContentControlType(resolvedTypeName) && node.FindAttribute("Content") == nullptr) {
            Indent(out, indent);
            out << targetVar << ".SetContent(" << EscapeRsString(node.textContent) << ");\n";
        }
    }

    [[nodiscard]] std::string EmitRunElement(
        const XamlNode& runNode,
        std::string_view xDataType,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string rVar = FreshVar("run");
        Indent(out, indent);
        out << "var " << rVar << " = new Run();\n";
        if (const auto* tAttr = runNode.FindAttribute("Text")) {
            if (LooksLikeMarkupExtension(tAttr->value)) {
                EmitPropertyAttributeOrBinding(rVar, "Run", "Text", tAttr->value, xDataType, dictVar, out, indent);
            } else {
                Indent(out, indent);
                out << rVar << ".Text = " << EscapeRsString(tAttr->value) << ";\n";
            }
        } else if (!runNode.textContent.empty()) {
            Indent(out, indent);
            out << rVar << ".Text = " << EscapeRsString(runNode.textContent) << ";\n";
        }
        if (const auto* fw = runNode.FindAttribute("FontWeight")) {
            Indent(out, indent);
            out << rVar << ".FontWeight = " << EscapeRsString(fw->value) << ";\n";
        }
        if (const auto* fs = runNode.FindAttribute("FontSize")) {
            float sz = 14.0f;
            if (aero::str::TryParseFloat32(fs->value, sz)) {
                Indent(out, indent);
                out << rVar << ".FontSize = " << FormatFloatLiteral(sz) << ";\n";
            }
        }
        if (const auto* fg = runNode.FindAttribute("Foreground")) {
            Indent(out, indent);
            out << rVar << ".Foreground = " << FormatTypedValueLiteral("Foreground", fg->value, dictVar) << ";\n";
        }
        return rVar;
    }

    void EmitGridView(
        const XamlNode& gvNode,
        std::string_view listViewVar,
        std::string_view /*xDataType*/,
        std::ostringstream& out,
        int indent) {
        const std::string gvVar = FreshVar("gv");
        Indent(out, indent);
        out << "var " << gvVar << " = new GridView();\n";

        const XamlNode* colsProp = gvNode.FindPropertyElement("GridView", "Columns");
        const auto& cols = (colsProp != nullptr) ? colsProp->children : gvNode.children;
        for (const auto& col : cols) {
            if (col->localName != "GridViewColumn") continue;
            const std::string colVar = FreshVar("gvc");
            Indent(out, indent);
            out << "var " << colVar << " = new GridViewColumn();\n";
            if (const auto* h = col->FindAttribute("Header")) {
                Indent(out, indent);
                out << colVar << ".Header = " << EscapeRsString(h->value) << ";\n";
            }
            if (const auto* w = col->FindAttribute("Width")) {
                float wf = 100.0f;
                if (aero::str::TryParseFloat32(w->value, wf)) {
                    Indent(out, indent);
                    out << colVar << ".Width = " << FormatFloatLiteral(wf) << ";\n";
                }
            }
            if (const auto* dmb = col->FindAttribute("DisplayMemberBinding")) {
                const auto ext = ParseMarkupExtension(dmb->value);
                ParsedBindingSpec spec;
                if (TryExtractBindingSpec(ext, spec) && !spec.path.empty()) {
                    Indent(out, indent);
                    out << colVar << ".DisplayMemberPath = " << EscapeRsString(spec.path) << ";\n";
                }
            }
            Indent(out, indent);
            out << gvVar << ".AddColumn(" << colVar << ");\n";
        }
        Indent(out, indent);
        out << listViewVar << ".View = " << gvVar << ";\n";
    }

    void EmitRiveInput(
        const XamlNode& node,
        std::string_view riveVar,
        std::ostringstream& out,
        int indent) {
        const std::string rv = FreshVar("rin");
        Indent(out, indent);
        out << "var " << rv << " = new RiveInput();\n";
        if (const auto* n = node.FindAttribute("InputName")) {
            Indent(out, indent);
            out << rv << ".InputName = " << EscapeRsString(n->value) << ";\n";
        }
        if (const auto* v = node.FindAttribute("InputValue")) {
            const std::string_view tv = aero::str::Trim(v->value);
            if (aero::str::EqualsIgnoreCase(tv, "True") || aero::str::EqualsIgnoreCase(tv, "False")) {
                Indent(out, indent);
                out << rv << ".SetInputValueBool(" << (aero::str::EqualsIgnoreCase(tv, "True") ? "true" : "false") << ");\n";
            } else {
                float fv = 0.0f;
                if (aero::str::TryParseFloat32(tv, fv)) {
                    Indent(out, indent);
                    out << rv << ".SetInputValueFloat(" << FormatFloatLiteral(fv) << ");\n";
                } else {
                    Indent(out, indent);
                    out << rv << ".SetInputValueString(" << EscapeRsString(v->value) << ");\n";
                }
            }
        }
        Indent(out, indent);
        out << riveVar << ".AddInput(" << rv << ");\n";
    }

    void EmitInputBindings(
        const XamlNode& ibNode,
        std::string_view ownerVar,
        std::ostringstream& out,
        int indent) {
        for (const auto& child : ibNode.children) {
            if (child->localName != "KeyBinding") continue;
            const std::string kb = FreshVar("kb");
            Indent(out, indent);
            out << "var " << kb << " = new KeyBinding();\n";
            if (const auto* k = child->FindAttribute("Key")) {
                Indent(out, indent);
                out << kb << ".Key = EnumConvert.ParseKeyCode(" << EscapeRsString(k->value) << ");\n";
            }
            if (const auto* m = child->FindAttribute("Modifiers")) {
                Indent(out, indent);
                out << kb << ".Modifiers = KeyModifiers.Parse(" << EscapeRsString(m->value) << ");\n";
            }
            if (const auto* cmd = child->FindAttribute("Command")) {
                if (LooksLikeMarkupExtension(cmd->value)) {
                    const auto ext = ParseMarkupExtension(cmd->value);
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec) && !spec.path.empty()) {
                        Indent(out, indent);
                        out << kb << ".CommandPath = " << EscapeRsString(spec.path) << ";\n";
                    }
                }
            }
            if (const auto* cp = child->FindAttribute("CommandParameter")) {
                if (LooksLikeMarkupExtension(cp->value)) {
                    const auto ext = ParseMarkupExtension(cp->value);
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec) && !spec.path.empty()) {
                        Indent(out, indent);
                        out << kb << ".CommandParameterPath = " << EscapeRsString(spec.path) << ";\n";
                    }
                } else {
                    Indent(out, indent);
                    out << kb << ".SetCommandParameter(" << EscapeRsString(cp->value) << ");\n";
                }
            }
            Indent(out, indent);
            out << ownerVar << ".AddInputBinding(" << kb << ");\n";
        }
    }

    void EmitProjection3D(
        const XamlNode& node,
        std::string_view targetVar,
        std::ostringstream& out,
        int indent) {
        const std::string pv = FreshVar("proj");
        Indent(out, indent);
        out << "var " << pv << " = new PlaneProjection();\n";
        auto emitScalarOrBind = [&](std::string_view attrName, std::string_view target3dProp) {
            if (const auto* a = node.FindAttribute(attrName)) {
                if (LooksLikeMarkupExtension(a->value)) {
                    const auto ext = ParseMarkupExtension(a->value);
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec)) {
                        EmitBindingSpec(targetVar, "UIElement", target3dProp, spec, "", out, indent, {});
                    }
                } else {
                    float f = 0.0f;
                    (void)aero::str::TryParseFloat32(a->value, f);
                    Indent(out, indent);
                    out << pv << "." << attrName << " = " << FormatFloatLiteral(f) << ";\n";
                }
            }
        };
        emitScalarOrBind("RotationX", "Rotation3DX");
        emitScalarOrBind("RotationY", "Rotation3DY");
        emitScalarOrBind("RotationZ", "Rotation3DZ");
        emitScalarOrBind("LocalOffsetX", "Translation3DX");
        emitScalarOrBind("LocalOffsetY", "Translation3DY");
        emitScalarOrBind("LocalOffsetZ", "Translation3DZ");
        emitScalarOrBind("GlobalOffsetX", "Translation3DX");
        emitScalarOrBind("GlobalOffsetY", "Translation3DY");
        emitScalarOrBind("GlobalOffsetZ", "Translation3DZ");
        Indent(out, indent);
        out << "Element.SetProjection3D(" << targetVar << ", " << pv << ");\n";
    }

    void EmitTransform3D(
        const XamlNode& node,
        std::string_view targetVar,
        std::ostringstream& out,
        int indent) {
        const std::string tv = FreshVar("t3d");
        Indent(out, indent);
        out << "var " << tv << " = new CompositeTransform3D();\n";
        auto emitFloat = [&](std::string_view attrName) {
            if (const auto* a = node.FindAttribute(attrName)) {
                float f = 0.0f;
                if (aero::str::TryParseFloat32(a->value, f)) {
                    Indent(out, indent);
                    out << tv << "." << attrName << " = " << FormatFloatLiteral(f) << ";\n";
                }
            }
        };
        emitFloat("RotationX");
        emitFloat("RotationY");
        emitFloat("RotationZ");
        emitFloat("TranslateX");
        emitFloat("TranslateY");
        emitFloat("TranslateZ");
        Indent(out, indent);
        out << "Element.SetTransform3D(" << targetVar << ", " << tv << ");\n";
    }

    [[nodiscard]] std::string EmitTriggerActionNode(
        const XamlNode& act,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string av = FreshVar("act");
        std::string targetName;
        if (const auto* tn = act.FindAttribute("TargetName")) targetName = tn->value;
        if (act.localName == "ControlStoryboardAction") {
            Indent(out, indent);
            out << "var " << av << " = new ControlStoryboardAction();\n";
            if (const auto* opt = act.FindAttribute("ControlStoryboardOption")) {
                Indent(out, indent);
                out << av << ".ControlStoryboardOption = " << EscapeRsString(opt->value) << ";\n";
            }
            if (const auto* sbAttr = act.FindAttribute("Storyboard")) {
                const auto ext = ParseMarkupExtension(sbAttr->value);
                const std::string rk = ResolveResourceKeyFromExtension(ext);
                if (!rk.empty()) {
                    Indent(out, indent);
                    out << av << ".StoryboardKey = " << EscapeRsString(rk) << ";\n";
                }
            }
            for (const auto& c : act.children) {
                if (c->localName == "Storyboard") {
                    const std::string sbVar = EmitStoryboard(*c, out, indent);
                    Indent(out, indent);
                    out << av << ".Storyboard = " << sbVar << ";\n";
                    break;
                }
            }
        } else if (act.localName == "InvokeCommandAction") {
            Indent(out, indent);
            out << "var " << av << " = new InvokeCommandAction();\n";
            if (const auto* ca = act.FindAttribute("Command")) {
                if (LooksLikeMarkupExtension(ca->value)) {
                    const auto ext = ParseMarkupExtension(ca->value);
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec) && !spec.path.empty()) {
                        Indent(out, indent);
                        out << av << ".CommandPath = " << EscapeRsString(spec.path) << ";\n";
                    }
                }
            }
            if (const auto* cp = act.FindAttribute("CommandParameter")) {
                if (LooksLikeMarkupExtension(cp->value)) {
                    const auto ext = ParseMarkupExtension(cp->value);
                    ParsedBindingSpec spec;
                    if (TryExtractBindingSpec(ext, spec) && !spec.path.empty()) {
                        Indent(out, indent);
                        out << av << ".CommandParameterPath = " << EscapeRsString(spec.path) << ";\n";
                        if (!spec.relativeSourceMode.empty()) {
                            Indent(out, indent);
                            out << av << ".CommandParameterRelativeMode = "
                                << EscapeRsString(spec.relativeSourceMode) << ";\n";
                        }
                        if (!spec.ancestorType.empty()) {
                            Indent(out, indent);
                            out << av << ".CommandParameterAncestorType = "
                                << EscapeRsString(spec.ancestorType) << ";\n";
                        }
                        if (!spec.elementName.empty()) {
                            Indent(out, indent);
                            out << av << ".CommandParameterElementName = "
                                << EscapeRsString(spec.elementName) << ";\n";
                        }
                    }
                } else {
                    Indent(out, indent);
                    out << av << ".SetCommandParameter(" << EscapeRsString(cp->value) << ");\n";
                }
            }
        } else if (act.localName == "ChangePropertyAction") {
            Indent(out, indent);
            out << "var " << av << " = new ChangePropertyAction();\n";
            std::string propName;
            if (const auto* pn = act.FindAttribute("PropertyName")) {
                propName = NormalizeSetterPropertyName(pn->value);
                Indent(out, indent);
                out << av << ".PropertyName = " << EscapeRsString(propName) << ";\n";
            }
            if (const auto* val = act.FindAttribute("Value")) {
                Indent(out, indent);
                out << av << ".SetValueBox("
                    << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, val->value, dictVar))
                    << ");\n";
            }
        } else if (act.localName == "SetFocusAction" ||
                   act.localName == "SelectAction" ||
                   act.localName == "SelectAllAction" ||
                   act.localName == "PlaySoundAction" ||
                   act.localName == "NavigateToAction" ||
                   act.localName == "NavigateBackAction" ||
                   act.localName == "PlayMediaAction" ||
                   act.localName == "PauseMediaAction" ||
                   act.localName == "StopMediaAction" ||
                   act.localName == "RewindMediaAction") {
            Indent(out, indent);
            out << "var " << av << " = new " << act.localName << "();\n";
            if (act.localName == "PlaySoundAction" || act.localName == "NavigateToAction") {
                if (const auto* src = act.FindAttribute("Source")) {
                    Indent(out, indent);
                    out << av << ".Source = " << EscapeRsString(src->value) << ";\n";
                }
            }
        } else if (act.localName == "BeginStoryboard") {
            for (const auto& sbNode : act.children) {
                if (sbNode->localName == "Storyboard") {
                    const std::string sbVar = EmitStoryboard(*sbNode, out, indent);
                    Indent(out, indent);
                    out << "var " << av << " = new ControlStoryboardAction(" << sbVar << ");\n";
                    break;
                }
            }
        } else {
            return {};
        }

        if (!targetName.empty()) {
            Indent(out, indent);
            out << av << ".TargetName = " << EscapeRsString(targetName) << ";\n";
        }
        return av;
    }

    [[nodiscard]] std::string EmitBehaviorTriggerObject(
        const XamlNode& trig,
        std::string_view /*scopeVar*/,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string tv = FreshVar("btrig");
        if (trig.localName == "EventTrigger") {
            std::string evName = "Loaded";
            if (const auto* en = trig.FindAttribute("EventName")) evName = en->value;
            else if (const auto* re = trig.FindAttribute("RoutedEvent")) {
                evName = re->value;
                const std::size_t dot = evName.rfind('.');
                if (dot != std::string::npos) evName = evName.substr(dot + 1);
            }
            Indent(out, indent);
            out << "var " << tv << " = new BehaviorEventTrigger(" << EscapeRsString(evName) << ");\n";
            if (const auto* sn = trig.FindAttribute("SourceName")) {
                Indent(out, indent);
                out << tv << ".SourceName = " << EscapeRsString(sn->value) << ";\n";
            }
        } else if (trig.localName == "KeyTrigger") {
            std::string key = "None";
            std::string mods;
            if (const auto* ka = trig.FindAttribute("Key")) key = ka->value;
            if (const auto* ma = trig.FindAttribute("Modifiers")) mods = ma->value;
            Indent(out, indent);
            out << "var " << tv << " = new KeyTrigger(EnumConvert.ParseKeyCode(" << EscapeRsString(key)
                << "), KeyModifiers.Parse(" << EscapeRsString(mods) << "));\n";
        } else if (trig.localName == "DataTrigger") {
            ParsedBindingSpec spec;
            if (const auto* ba = trig.FindAttribute("Binding")) {
                const auto ext = ParseMarkupExtension(ba->value);
                (void)TryExtractBindingSpec(ext, spec);
            }
            Indent(out, indent);
            out << "var " << tv << " = new BehaviorDataTrigger();\n";
            Indent(out, indent);
            out << tv << ".BindingPath = " << EscapeRsString(spec.path) << ";\n";
            if (!spec.relativeSourceMode.empty()) {
                Indent(out, indent);
                out << tv << ".RelativeSourceMode = " << EscapeRsString(spec.relativeSourceMode) << ";\n";
            }
            if (!spec.ancestorType.empty()) {
                Indent(out, indent);
                out << tv << ".RelativeSourceAncestorType = " << EscapeRsString(spec.ancestorType) << ";\n";
            }
            if (const auto* comp = trig.FindAttribute("Comparison")) {
                Indent(out, indent);
                out << tv << ".Comparison = " << EscapeRsString(comp->value) << ";\n";
            }
            if (const auto* val = trig.FindAttribute("Value")) {
                Indent(out, indent);
                out << tv << ".SetValueBox(" << BoxSetterValueExpr(spec.path, FormatGenericScalarLiteral(val->value)) << ");\n";
            }
        } else if (trig.localName == "StoryboardCompletedTrigger") {
            Indent(out, indent);
            out << "var " << tv << " = new StoryboardCompletedTrigger();\n";
            if (const auto* sbAttr = trig.FindAttribute("Storyboard")) {
                if (LooksLikeMarkupExtension(sbAttr->value)) {
                    const auto ext = ParseMarkupExtension(sbAttr->value);
                    const std::string rk = ResolveResourceKeyFromExtension(ext);
                    if (!rk.empty()) {
                        Indent(out, indent);
                        out << "var __sb_" << tv << " = " << LookupResourceExpr(rk, dictVar) << ";\n";
                        Indent(out, indent);
                        out << "if (__sb_" << tv << " is Storyboard __sbo_" << tv << ") { "
                            << tv << ".Storyboard = __sbo_" << tv << "; }\n";
                    }
                }
            }
        } else {
            return {};
        }

        if (const auto* condProp = trig.FindPropertyElement("Interaction", "Behaviors")) {
            for (const auto& bNode : condProp->children) {
                if (bNode->localName == "ConditionBehavior") {
                    for (const auto& ceNode : bNode->children) {
                        for (const auto& ccNode : ceNode->children) {
                            if (ccNode->localName == "ComparisonCondition") {
                                std::string lhs, op = "Equal", rhs;
                                if (const auto* la = ccNode->FindAttribute("LeftOperand")) {
                                    const auto ext = ParseMarkupExtension(la->value);
                                    ParsedBindingSpec sp;
                                    if (TryExtractBindingSpec(ext, sp)) lhs = sp.path;
                                    else lhs = la->value;
                                }
                                if (const auto* oa = ccNode->FindAttribute("Operator")) op = oa->value;
                                if (const auto* ra = ccNode->FindAttribute("RightOperand")) rhs = ra->value;
                                Indent(out, indent);
                                out << tv << ".AddCondition(new ComparisonCondition("
                                    << EscapeRsString(lhs) << ", "
                                    << EscapeRsString(op) << ", "
                                    << BoxSetterValueExpr(lhs, FormatGenericScalarLiteral(rhs)) << "));\n";
                            }
                        }
                    }
                }
            }
        }

        for (const auto& act : trig.children) {
            const std::string av = EmitTriggerActionNode(*act, dictVar, out, indent);
            if (!av.empty()) {
                Indent(out, indent);
                out << tv << ".AddAction(" << av << ");\n";
            }
        }
        return tv;
    }

    void EmitInteractionTriggers(
        const XamlNode& trigNode,
        std::string_view targetVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string scopeVar = inPartialClass_ ? "this" : std::string(targetVar);
        for (const auto& trig : trigNode.children) {
            const std::string tv = EmitBehaviorTriggerObject(*trig, scopeVar, dictVar, out, indent);
            if (!tv.empty()) {
                Indent(out, indent);
                out << "Interaction.AddTrigger(" << targetVar << ", " << tv << ");\n";
            }
        }
    }

    void EmitInteractionBehaviors(
        const XamlNode& behNode,
        std::string_view targetVar,
        std::string_view /*dictVar*/,
        std::ostringstream& out,
        int indent) {
        for (const auto& beh : behNode.children) {
            if (beh->localName == "MouseDragElementBehavior") {
                const std::string bv = FreshVar("beh");
                Indent(out, indent);
                out << "var " << bv << " = new MouseDragElementBehavior();\n";
                if (const auto* c = beh->FindAttribute("ConstrainToParentBounds")) {
                    Indent(out, indent);
                    out << bv << ".ConstrainToParentBounds = "
                        << (aero::str::EqualsIgnoreCase(c->value, "True") ? "true" : "false") << ";\n";
                }
                Indent(out, indent);
                out << "Interaction.AddBehavior(" << targetVar << ", " << bv << ");\n";
            } else if (beh->localName == "TranslateZoomRotateBehavior") {
                const std::string bv = FreshVar("beh");
                Indent(out, indent);
                out << "var " << bv << " = new TranslateZoomRotateBehavior();\n";
                Indent(out, indent);
                out << "Interaction.AddBehavior(" << targetVar << ", " << bv << ");\n";
            } else if (beh->localName == "BackgroundEffectBehavior") {
                const std::string bv = FreshVar("beh");
                Indent(out, indent);
                out << "var " << bv << " = new BackgroundEffectBehavior();\n";
                if (!beh->children.empty()) {
                    const std::string efVar = EmitEffectObject(*beh->children.front(), {}, out, indent);
                    if (!efVar.empty()) {
                        Indent(out, indent);
                        out << bv << ".Effect = " << efVar << ";\n";
                    }
                }
                Indent(out, indent);
                out << "Interaction.AddBehavior(" << targetVar << ", " << bv << ");\n";
            }
        }
    }

    void EmitElementTriggers(
        const XamlNode& triggersNode,
        std::string_view targetVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        for (const auto& trig : triggersNode.children) {
            if (trig->localName != "EventTrigger") continue;
            std::string evName = "Loaded";
            if (const auto* re = trig->FindAttribute("RoutedEvent")) {
                evName = re->value;
                const std::size_t dot = evName.rfind('.');
                if (dot != std::string::npos) evName = evName.substr(dot + 1);
            } else if (const auto* en = trig->FindAttribute("EventName")) {
                evName = en->value;
            }

            const std::string etVar = FreshVar("evtrig");
            Indent(out, indent);
            out << "var " << etVar << " = new BehaviorEventTrigger(" << EscapeRsString(evName) << ");\n";
            if (const auto* sn = trig->FindAttribute("SourceName")) {
                Indent(out, indent);
                out << etVar << ".SourceName = " << EscapeRsString(sn->value) << ";\n";
            }

            for (const auto& act : trig->children) {
                if (act->localName == "BeginStoryboard") {
                    for (const auto& sbNode : act->children) {
                        if (sbNode->localName == "Storyboard") {
                            const std::string sbVar = EmitStoryboard(*sbNode, out, indent);
                            Indent(out, indent);
                            out << etVar << ".AddAction(new ControlStoryboardAction(" << sbVar << ", \"Play\"));\n";
                        }
                    }
                } else {
                    const std::string av = EmitTriggerActionNode(*act, dictVar, out, indent);
                    if (!av.empty()) {
                        Indent(out, indent);
                        out << etVar << ".AddAction(" << av << ");\n";
                    }
                }
            }

            Indent(out, indent);
            out << "Interaction.AddTrigger(" << targetVar << ", " << etVar << ");\n";
        }
    }
};

} // namespace

XamlEmitResult EmitXamlToRealScript(
    std::string_view xamlContent,
    const XamlEmitOptions& options) {
    XamlEmitter emitter(options);
    return emitter.Run(xamlContent);
}

std::string AeroCompilerV3::EmitPartialRsStub(
    std::string_view xamlPath,
    std::string_view xamlContent,
    std::vector<std::string>* outDiagnostics) {
    XamlEmitOptions opts;
    opts.xamlPath = std::string(xamlPath);
    const XamlEmitResult result = EmitXamlToRealScript(xamlContent, opts);
    if (outDiagnostics != nullptr) {
        *outDiagnostics = result.diagnostics;
    }
    return result.generatedRsCode;
}

} // namespace aero
