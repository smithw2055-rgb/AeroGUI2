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
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s.append(".0");
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
    // HH:MM:SS.fff or MM:SS.fff
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
        "ShadowDepth", "Direction", "Angle", "AngleX", "AngleY", "ScaleX", "ScaleY"
    };
    return kFloatProps.count(prop) > 0;
}

[[nodiscard]] bool IsIntProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kIntProps = {
        "ZIndex", "MaxLines", "TabIndex", "SelectedIndex", "Rows", "Columns", "MaxLength"
    };
    return kIntProps.count(prop) > 0;
}

[[nodiscard]] bool IsBoolProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kBoolProps = {
        "IsEnabled", "IsHitTestVisible", "Focusable", "ClipToBounds", "IsChecked",
        "IsThreeState", "IsExpanded", "IsSelected", "IsReadOnly", "LastChildFill",
        "IsOpen", "IsCheckable", "IsDefault", "IsCancel", "ShowGridLines",
        "StaysOpen", "IsEditable"
    };
    return kBoolProps.count(prop) > 0;
}

[[nodiscard]] bool IsThicknessProperty(std::string_view prop) {
    return prop == "Margin" || prop == "Padding" || prop == "BorderThickness";
}

[[nodiscard]] bool IsBrushProperty(std::string_view prop) {
    return prop == "Background" || prop == "Foreground" || prop == "BorderBrush" ||
           prop == "Fill" || prop == "Stroke" || prop == "CaretBrush" ||
           prop == "SelectionBrush";
}

[[nodiscard]] bool IsEventAttribute(std::string_view prop) {
    static const std::unordered_set<std::string_view> kEvents = {
        "Click", "Loaded", "Unloaded", "Checked", "Unchecked", "Indeterminate",
        "SelectionChanged", "TextChanged", "ValueChanged", "GotFocus", "LostFocus",
        "KeyDown", "KeyUp", "MouseDown", "MouseUp", "MouseEnter", "MouseLeave",
        "MouseLeftButtonDown", "MouseLeftButtonUp", "MouseRightButtonDown",
        "MouseRightButtonUp", "MouseMove", "RequestNavigate", "Expanded",
        "Collapsed", "Opened", "Closed", "DragDelta"
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
        "BorderThickness", "CornerRadius", "Text", "FontSize", "FontFamily",
        "TextWrapping", "TextAlignment", "TextTrimming", "Orientation", "Spacing",
        "Stretch", "StretchDirection", "ExpandDirection", "Source", "Data",
        "Fill", "Stroke", "StrokeThickness", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "Minimum", "Maximum", "Value", "SmallChange",
        "LargeChange", "IsChecked", "IsThreeState", "IsExpanded", "IsSelected",
        "IsReadOnly", "LastChildFill", "SelectedIndex", "SelectionMode",
        "Placeholder", "IsCheckable", "TabStripPlacement",
        "HorizontalScrollBarVisibility", "VerticalScrollBarVisibility",
        "RenderTransform", "Effect", "Style", "ShowGridLines", "Rows", "Columns"
    };
    return kProps.count(prop) > 0;
}

[[nodiscard]] bool IsContentControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kCC = {
        "ContentControl", "HeaderedContentControl", "ButtonBase", "Button",
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
            shortClassName_ = SanitizeIdentifier(
                std::filesystem::path(opts_.xamlPath).stem().string(), "View") +
                "_" + root->localName;
            rootClassName_ = root->localName;
        }

        // Collect clr-namespace imports differing from current module
        for (const auto& attr : root->attributes) {
            if (!attr.isXmlns) {
                continue;
            }
            if (aero::str::StartsWith(attr.value, "clr-namespace:")) {
                std::string_view rest = attr.value.substr(14);
                const std::size_t semi = rest.find(';');
                if (semi != std::string_view::npos) {
                    rest = rest.substr(0, semi);
                }
                const std::string ns(aero::str::Trim(rest));
                if (!ns.empty() && ns != moduleName_ &&
                    !aero::str::StartsWith(ns, "System") &&
                    !aero::str::StartsWith(ns, "Noesis")) {
                    extraImports_.insert(ns);
                }
            }
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
    std::unordered_map<std::string, std::string> knownColors_;
    std::unordered_map<std::string, std::string> knownGeometries_;
    std::unordered_map<std::string, float> knownDoubles_;
    std::unordered_set<std::string> visitedExternalXamls_;
    std::vector<std::string> diagnostics_;
    int nextId_ = 0;
    bool inPartialClass_ = false;
    std::string currentTplParentVar_;

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
        if (!node.prefix.empty() && aero::str::StartsWith(node.namespaceUri, "clr-namespace:")) {
            std::string_view rest = node.namespaceUri.substr(14);
            const std::size_t semi = rest.find(';');
            if (semi != std::string_view::npos) {
                rest = rest.substr(0, semi);
            }
            const std::string ns(aero::str::Trim(rest));
            if (!ns.empty() && ns != moduleName_ &&
                !aero::str::StartsWith(ns, "System") &&
                !aero::str::StartsWith(ns, "Noesis")) {
                extraImports_.insert(ns);
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
            std::string key = child->GetKey();
            if (child->localName == "Style" && key.empty()) {
                if (const auto* tt = child->FindAttribute("TargetType")) {
                    key = StripTargetTypeName(tt->value);
                }
            }
            if (key.empty()) {
                continue;
            }

            if (child->localName == "Color") {
                const std::string hex = std::string(aero::str::Trim(child->textContent));
                knownColors_[key] = hex;
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                    << EscapeRsString(hex) << ");\n";
            } else if (child->localName == "SolidColorBrush") {
                const std::string expr = EmitBrushObject(*child, {}, dictVar, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << expr << ");\n";
            } else if (child->localName == "LinearGradientBrush" ||
                       child->localName == "RadialGradientBrush") {
                const std::string expr = EmitBrushObject(*child, {}, dictVar, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << expr << ");\n";
            } else if (child->localName == "Thickness") {
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                    << FormatThicknessExpr(child->textContent) << ");\n";
            } else if (child->localName == "CornerRadius") {
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                    << FormatCornerRadiusExpr(child->textContent) << ");\n";
            } else if (child->localName == "Double") {
                float val = 0.0f;
                (void)aero::str::TryParseFloat32(child->textContent, val);
                knownDoubles_[key] = val;
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                    << FormatFloatLiteral(val) << ");\n";
            } else if (child->localName == "StreamGeometry" ||
                       child->localName == "PathGeometry" ||
                       child->localName == "Geometry") {
                const std::string geom = std::string(aero::str::Trim(child->textContent));
                knownGeometries_[key] = geom;
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                    << EscapeRsString(geom) << ");\n";
            } else if (child->localName == "Style") {
                const std::string styVar = EmitStyle(*child, dictVar, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << styVar << ");\n";
            } else if (child->localName == "ControlTemplate") {
                const std::string tplVar = EmitControlTemplate(*child, dictVar, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << tplVar << ");\n";
            } else if (child->localName == "DataTemplate") {
                const std::string dtVar = EmitDataTemplate(*child, dictVar, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << dtVar << ");\n";
            } else if (child->localName == "Storyboard") {
                const std::string sbVar = EmitStoryboard(*child, out, indent);
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", " << sbVar << ");\n";
            } else if (child->localName == "BoolToVisibilityConverter" ||
                       child->localName == "BooleanToVisibilityConverter" ||
                       child->localName == "InverseBoolConverter" ||
                       child->localName == "MathConverter") {
                const std::string typeName = (child->localName == "BooleanToVisibilityConverter")
                    ? std::string("BoolToVisibilityConverter")
                    : child->localName;
                Indent(out, indent);
                out << dictVar << ".Set(" << EscapeRsString(key) << ", new " << typeName << "());\n";
            }
        }
    }

    void EmitStandaloneResourceDictionary(
        const XamlNode& root,
        std::string_view currentXamlPath,
        std::ostringstream& out) {
        out << "public class " << shortClassName_ << "\n{\n";
        Indent(out, 1);
        out << "public static ResourceDictionary Build()\n";
        Indent(out, 1);
        out << "{\n";
        Indent(out, 2);
        out << "var dict = new ResourceDictionary();\n";
        EmitResourceDictionaryEntries(root, currentXamlPath, "dict", out, 2);
        Indent(out, 2);
        out << "return dict;\n";
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
        inPartialClass_ = false;
        const std::string baseType = ResolveElementType(root);

        std::ostringstream buildBody;
        const std::string rootVar = EmitConstructedElement(
            root, xDataType, "", currentXamlPath, buildBody, 2);
        Indent(buildBody, 2);
        buildBody << "BindingEngine.Flush();\n";
        Indent(buildBody, 2);
        buildBody << "return " << rootVar << ";\n";

        out << "public class " << shortClassName_ << "\n{\n";
        Indent(out, 1);
        out << "public static " << baseType << " Build()\n";
        Indent(out, 1);
        out << "{\n";
        out << buildBody.str();
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
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << EscapeRsString(hex) << ");\n";
        } else if (child.localName == "SolidColorBrush" ||
                   child.localName == "LinearGradientBrush" ||
                   child.localName == "RadialGradientBrush") {
            const std::string expr = EmitBrushObject(child, {}, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << expr << ");\n";
        } else if (child.localName == "Style") {
            const std::string v = EmitStyle(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "ControlTemplate") {
            const std::string v = EmitControlTemplate(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "DataTemplate") {
            const std::string v = EmitDataTemplate(child, dictVar, out, indent);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "Storyboard") {
            const std::string v = EmitStoryboard(child, out, indent);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << v << ");\n";
        } else if (child.localName == "Thickness") {
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                << FormatThicknessExpr(child.textContent) << ");\n";
        } else if (child.localName == "CornerRadius") {
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", "
                << FormatCornerRadiusExpr(child.textContent) << ");\n";
        } else if (child.localName == "Double") {
            float val = 0.0f;
            (void)aero::str::TryParseFloat32(child.textContent, val);
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << FormatFloatLiteral(val) << ");\n";
        } else if (child.localName == "StreamGeometry" ||
                   child.localName == "PathGeometry" ||
                   child.localName == "Geometry") {
            const std::string geom = std::string(aero::str::Trim(child.textContent));
            knownGeometries_[key] = geom;
            Indent(out, indent);
            out << dictVar << ".Set(" << EscapeRsString(key) << ", " << EscapeRsString(geom) << ");\n";
        }
    }

    [[nodiscard]] std::string EmitBrushObject(
        const XamlNode& brushNode,
        std::string_view /*targetProp*/,
        std::string_view /*dictVar*/,
        std::ostringstream& out,
        int indent) {
        if (brushNode.localName == "SolidColorBrush") {
            std::string color = "#FFFFFFFF";
            if (const auto* colAttr = brushNode.FindAttribute("Color")) {
                color = ResolveColorHexOrString(colAttr->value);
            }
            const std::string bVar = FreshVar("brush");
            Indent(out, indent);
            out << "var " << bVar << " = new SolidColorBrush(" << EscapeRsString(color) << ");\n";
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
            EmitGradientStops(brushNode, bVar, out, indent);
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
            EmitGradientStops(brushNode, bVar, out, indent);
            return bVar;
        }

        return "new SolidColorBrush(\"#00000000\")";
    }

    void EmitGradientStops(
        const XamlNode& brushNode,
        std::string_view brushVar,
        std::ostringstream& out,
        int indent) {
        const XamlNode* stopContainer = brushNode.FindPropertyElement(brushNode.localName, "GradientStops");
        const auto& stops = (stopContainer != nullptr) ? stopContainer->children : brushNode.children;
        for (const auto& stop : stops) {
            if (stop->localName != "GradientStop") {
                continue;
            }
            std::string color = "#FFFFFFFF";
            if (const auto* cAttr = stop->FindAttribute("Color")) {
                color = ResolveColorHexOrString(cAttr->value);
            }
            float offset = 0.0f;
            if (const auto* oAttr = stop->FindAttribute("Offset")) {
                (void)aero::str::TryParseFloat32(oAttr->value, offset);
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
                if (const auto* k = ext.FindPositional(0)) {
                    Indent(out, indent);
                    out << "var __base_" << styVar << " = " << LookupResourceExpr(k->literal, dictVar) << ";\n";
                    Indent(out, indent);
                    out << "if (__base_" << styVar << " is Style __bs_" << styVar << ") { "
                        << styVar << ".BasedOn = __bs_" << styVar << "; }\n";
                }
            }
        }

        auto emitSetterInto = [&](const XamlNode& setterNode, std::string_view targetObjVar) {
            std::string propName;
            if (const auto* pAttr = setterNode.FindAttribute("Property")) {
                propName = pAttr->value;
                // Keep short property name unless attached prefix is meaningful
                const std::size_t dot = propName.find('.');
                if (dot != std::string::npos) {
                    const std::string_view owner = std::string_view(propName).substr(0, dot);
                    if (owner == "Control" || owner == "FrameworkElement" || owner == "UIElement" ||
                        owner == "TextBlock") {
                        propName = propName.substr(dot + 1);
                    }
                }
            }
            if (propName.empty()) {
                return;
            }
            std::string targetName;
            if (const auto* tnAttr = setterNode.FindAttribute("TargetName")) {
                targetName = tnAttr->value;
            }

            std::string valExpr;
            if (const auto* valEl = setterNode.FindPropertyElement("Setter", "Value")) {
                for (const auto& c : valEl->children) {
                    if (c->localName == "ControlTemplate") {
                        valExpr = EmitControlTemplate(*c, dictVar, out, indent);
                        break;
                    }
                    if (c->localName == "DataTemplate") {
                        valExpr = EmitDataTemplate(*c, dictVar, out, indent);
                        break;
                    }
                    if (c->localName == "SolidColorBrush" ||
                        c->localName == "LinearGradientBrush" ||
                        c->localName == "RadialGradientBrush") {
                        valExpr = EmitBrushObject(*c, propName, dictVar, out, indent);
                        break;
                    }
                }
                if (valExpr.empty() && !valEl->textContent.empty()) {
                    valExpr = FormatTypedValueLiteral(propName, valEl->textContent, dictVar);
                }
            } else if (const auto* valAttr = setterNode.FindAttribute("Value")) {
                valExpr = FormatTypedValueLiteral(propName, valAttr->value, dictVar);
            }

            if (valExpr.empty()) {
                return;
            }
            Indent(out, indent);
            out << targetObjVar << ".AddSetter(" << EscapeRsString(propName) << ", " << valExpr;
            if (!targetName.empty()) {
                out << ", " << EscapeRsString(targetName);
            }
            out << ");\n";
        };

        for (const auto& child : styleNode.children) {
            if (child->localName == "Setter") {
                emitSetterInto(*child, styVar);
            }
        }

        const XamlNode* trigProp = styleNode.FindPropertyElement("Style", "Triggers");
        if (trigProp != nullptr) {
            for (const auto& trig : trigProp->children) {
                if (trig->localName == "Trigger") {
                    std::string prop;
                    std::string val;
                    if (const auto* p = trig->FindAttribute("Property")) prop = p->value;
                    if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                    const std::string tVar = FreshVar("trig");
                    Indent(out, indent);
                    out << "var " << tVar << " = new Trigger(" << EscapeRsString(prop) << ", "
                        << FormatTypedValueLiteral(prop, val, dictVar) << ");\n";
                    for (const auto& s : trig->children) {
                        if (s->localName == "Setter") {
                            emitSetterInto(*s, tVar);
                        }
                    }
                    Indent(out, indent);
                    out << styVar << ".AddTrigger(" << tVar << ");\n";
                } else if (trig->localName == "DataTrigger") {
                    std::string path;
                    if (const auto* b = trig->FindAttribute("Binding")) {
                        const auto ext = ParseMarkupExtension(b->value);
                        ParsedBindingSpec spec;
                        if (TryExtractBindingSpec(ext, spec)) {
                            path = spec.path;
                        }
                    }
                    std::string val;
                    if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                    const std::string tVar = FreshVar("dtrig");
                    Indent(out, indent);
                    out << "var " << tVar << " = new DataTrigger(" << EscapeRsString(path) << ", "
                        << FormatGenericScalarLiteral(val) << ");\n";
                    for (const auto& s : trig->children) {
                        if (s->localName == "Setter") {
                            emitSetterInto(*s, tVar);
                        }
                    }
                    Indent(out, indent);
                    out << styVar << ".AddTrigger(" << tVar << ");\n";
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
                    out << styVar << ".AddTrigger(" << tVar << ");\n";
                }
            }
        }
        return styVar;
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
        out << "var " << tplVar << " = new ControlTemplate(" << EscapeRsString(targetType)
            << ", (Control " << tpParam << ") =>\n";
        Indent(out, indent);
        out << "{\n";

        const std::string prevTp = currentTplParentVar_;
        currentTplParentVar_ = tpParam;
        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;

        std::string returnedRoot;
        for (const auto& child : tplNode.children) {
            if (ShouldIgnoreXmlElement(*child) ||
                child->localName == "ControlTemplate.Triggers") {
                continue;
            }
            returnedRoot = EmitConstructedElement(*child, "", dictVar, opts_.xamlPath, out, indent + 1);
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
        out << "});\n";
        return tplVar;
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
        out << "var " << dtVar << " = new DataTemplate((object " << dcParam << ") =>\n";
        Indent(out, indent);
        out << "{\n";

        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;

        std::string returnedRoot;
        for (const auto& child : dtNode.children) {
            if (ShouldIgnoreXmlElement(*child)) {
                continue;
            }
            returnedRoot = EmitConstructedElement(
                *child, itemDataType, dictVar, opts_.xamlPath, out, indent + 1, dcParam);
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
        out << "});\n";
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
                targetProp = tp->value;
            } else if (const auto* tp2 = child->FindAttribute("TargetProperty")) {
                targetProp = tp2->value;
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

            if (child->localName == "DoubleAnimation") {
                float fromVal = 0.0f;
                float toVal = 1.0f;
                if (const auto* f = child->FindAttribute("From")) {
                    (void)aero::str::TryParseFloat32(f->value, fromVal);
                }
                if (const auto* t = child->FindAttribute("To")) {
                    (void)aero::str::TryParseFloat32(t->value, toVal);
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
                std::uint32_t fromRgba = 0xFFFFFFFFu;
                std::uint32_t toRgba = 0xFFFFFFFFu;
                if (const auto* f = child->FindAttribute("From")) {
                    fromRgba = aero::ParseColorRgba32(ResolveColorHexOrString(f->value));
                }
                if (const auto* t = child->FindAttribute("To")) {
                    toRgba = aero::ParseColorRgba32(ResolveColorHexOrString(t->value));
                }
                float durSec = 0.3f;
                if (const auto* d = child->FindAttribute("Duration")) {
                    durSec = ParseDurationSeconds(d->value);
                }
                Indent(out, indent);
                out << sbVar << ".AddColorAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatUintHexLiteral(fromRgba) << ", "
                    << FormatUintHexLiteral(toRgba) << ", "
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
                    if (havePrev && kt > prevTime) {
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
                std::uint32_t prevRgba = 0xFFFFFFFFu;
                bool havePrev = false;
                for (const auto& kf : child->children) {
                    float kt = prevTime;
                    if (const auto* ka = kf->FindAttribute("KeyTime")) {
                        kt = ParseDurationSeconds(ka->value);
                    }
                    std::uint32_t rgba = prevRgba;
                    if (const auto* va = kf->FindAttribute("Value")) {
                        rgba = aero::ParseColorRgba32(ResolveColorHexOrString(va->value));
                    }
                    const float segDur = std::max(0.001f, kt - prevTime);
                    if (havePrev && kt > prevTime) {
                        Indent(out, indent);
                        out << sbVar << ".AddColorAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << FormatUintHexLiteral(prevRgba) << ", "
                            << FormatUintHexLiteral(rgba) << ", "
                            << FormatFloatLiteral(segDur) << ", 0, 0, "
                            << FormatFloatLiteral(beginSec + prevTime) << ");\n";
                    }
                    prevTime = kt;
                    prevRgba = rgba;
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
        if (!dictVar.empty()) {
            return std::string(dictVar) + ".Get(" + EscapeRsString(key) + ")";
        }
        return "this.FindResource(" + EscapeRsString(key) + ")";
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
                if (const auto* k = ext.FindPositional(0)) {
                    if (IsBrushProperty(propName)) {
                        return "(Brush)" + LookupResourceExpr(k->literal, dictVar);
                    }
                    if (propName == "Style") {
                        return "(Style)" + LookupResourceExpr(k->literal, dictVar);
                    }
                    if (propName == "Template") {
                        return "(ControlTemplate)" + LookupResourceExpr(k->literal, dictVar);
                    }
                    if (propName == "ItemTemplate") {
                        return "(DataTemplate)" + LookupResourceExpr(k->literal, dictVar);
                    }
                    if (const auto it = knownGeometries_.find(k->literal); it != knownGeometries_.end()) {
                        return EscapeRsString(it->second);
                    }
                    return LookupResourceExpr(k->literal, dictVar);
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
        if (LooksLikeMarkupExtension(rawVal)) {
            const auto ext = ParseMarkupExtension(rawVal);
            if (!ext.IsOk()) {
                diagnostics_.push_back("Invalid markup extension '" + std::string(rawVal) + "': " + ext.errorMessage);
                return;
            }
            if (ext.kind == MarkupExtensionKind::EscapedLiteral) {
                rawVal = ext.literalValue;
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
                if (const auto* k = ext.FindPositional(0)) {
                    const std::string lookup = LookupResourceExpr(k->literal, dictVar);
                    if (propName == "Style") {
                        Indent(out, indent);
                        out << targetVar << ".Style = (Style)" << lookup << ";\n";
                    } else if (IsBrushProperty(propName)) {
                        Indent(out, indent);
                        out << targetVar << "." << propName << " = (Brush)" << lookup << ";\n";
                    } else if (propName == "Data" && knownGeometries_.count(k->literal) > 0) {
                        Indent(out, indent);
                        out << targetVar << ".Data = " << EscapeRsString(knownGeometries_.at(k->literal)) << ";\n";
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
                    } else if (propName == "Storyboard") {
                        // Handled at action level
                    } else if (IsDirectUIProperty(propName)) {
                        Indent(out, indent);
                        out << targetVar << ".SetProperty(" << EscapeRsString(propName)
                            << ", " << lookup << ");\n";
                    }
                }
                return;
            } else if (ext.kind == MarkupExtensionKind::NoesisLoc) {
                if (const auto* k = ext.FindPositional(0)) {
                    rawVal = k->literal;
                }
            }
        }

        // Attached property checks first
        if (propName == "Grid.Row") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridRow(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.Column") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridColumn(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.RowSpan") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridRowSpan(" << targetVar << ", " << v << ");\n";
            return;
        }
        if (propName == "Grid.ColumnSpan") {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            Indent(out, indent);
            out << "UIElement.SetGridColumnSpan(" << targetVar << ", " << v << ");\n";
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

        // Generic scalar metadata props (`Tag`, `ToolTip`, `Placeholder`, `CommandParameter`, etc.)
        if (propName == "Tag" || propName == "ToolTip" || propName == "Title" ||
            propName == "InputGestureText" || propName == "CommandParameter" ||
            propName == "Placeholder" || propName == "FontWeight" || propName == "DisplayMemberPath") {
            Indent(out, indent);
            out << targetVar << ".SetProperty(" << EscapeRsString(propName) << ", "
                << FormatGenericScalarLiteral(rawVal) << ");\n";
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
        std::string_view explicitSourceVar) {
        if (spec.hasFallbackValue && IsDirectUIProperty(propName)) {
            Indent(out, indent);
            out << targetVar << "." << propName << " = "
                << FormatTypedValueLiteral(propName, spec.fallbackValue, {}) << ";\n";
        }

        if (spec.isTemplateBinding ||
            (spec.hasRelativeSource && spec.relativeSourceMode == "TemplatedParent")) {
            if (!currentTplParentVar_.empty()) {
                Indent(out, indent);
                out << "BindingEngine.BindTemplateParent(" << targetVar << ", "
                    << EscapeRsString(propName) << ", " << currentTplParentVar_ << ", "
                    << EscapeRsString(spec.path) << ");\n";
            }
            return;
        }

        const int modeInt = MapBindingMode(spec.mode);
        if (!spec.elementName.empty()) {
            Indent(out, indent);
            out << "BindingEngine.BindElement(" << targetVar << ", "
                << EscapeRsString(propName) << ", "
                << EscapeRsString(spec.elementName) << ", "
                << EscapeRsString(spec.path) << ", " << modeInt << ");\n";
            return;
        }

        // Note: Run might be Inline rather than UIElement; cast or target safely
        const std::string bindTarget = (targetType == "Run")
            ? ("(UIElement)" + std::string(targetVar))
            : std::string(targetVar);

        std::string srcExpr = !explicitSourceVar.empty()
            ? std::string(explicitSourceVar)
            : std::string(targetVar);
        if (spec.hasRelativeSource) {
            if (spec.relativeSourceMode == "Self") {
                srcExpr = std::string(targetVar);
            } else if (inPartialClass_) {
                srcExpr = "this";
            }
        }

        // Strongly-typed x:DataType fast-path sync inside DataTemplate when source object is immediate
        if (!xDataType.empty() && !explicitSourceVar.empty() &&
            !spec.path.empty() && spec.path.find('.') == std::string::npos &&
            spec.converterKey.empty() && spec.stringFormat.empty()) {
            const std::string typedVar = FreshVar("__vm");
            Indent(out, indent);
            out << "if (" << explicitSourceVar << " is " << xDataType << " " << typedVar << ") { ";
            if (propName == "Content") {
                out << targetVar << ".SetContent(" << typedVar << "." << spec.path << ");";
            } else if (IsDirectUIProperty(propName)) {
                out << targetVar << "." << propName << " = " << typedVar << "." << spec.path << ";";
            } else {
                out << targetVar << ".SetProperty(" << EscapeRsString(propName) << ", "
                    << typedVar << "." << spec.path << ");";
            }
            out << " }\n";
        }

        Indent(out, indent);
        out << "BindingEngine.BindPath(" << bindTarget << ", "
            << EscapeRsString(propName) << ", "
            << srcExpr << ", "
            << EscapeRsString(spec.path) << ", "
            << modeInt << ", "
            << EscapeRsString(spec.converterKey) << ", "
            << EscapeRsString(spec.converterParam) << ", "
            << EscapeRsString(spec.stringFormat) << ");\n";
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

        // Property elements for brushes (`Border.Background`, `Rectangle.Fill`, etc.)
        for (const auto& [propKey, propNode] : node.propertyElements) {
            const std::size_t dotPos = propKey.find('.');
            const std::string shortProp = (dotPos != std::string::npos)
                ? propKey.substr(dotPos + 1)
                : propKey;
            if (shortProp == "Resources" || shortProp == "RowDefinitions" ||
                shortProp == "ColumnDefinitions" || shortProp == "InputBindings") {
                continue;
            }
            if (IsBrushProperty(shortProp) && !propNode->children.empty()) {
                const std::string bVar = EmitBrushObject(
                    *propNode->children.front(), shortProp, dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << "." << shortProp << " = " << bVar << ";\n";
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
                    out << targetVar << ".SetProperty(" << EscapeRsString(shortProp) << ", " << sub << ");\n";
                }
                continue;
            }
            if (shortProp == "View" && !propNode->children.empty() &&
                propNode->children.front()->localName == "GridView") {
                EmitGridView(*propNode->children.front(), targetVar, xDataType, out, indent);
                continue;
            }
            if (shortProp == "Triggers") {
                EmitElementTriggers(*propNode, targetVar, out, indent);
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

        // Regular visual / item children
        for (const auto& ch : node.children) {
            if (ShouldIgnoreXmlElement(*ch)) {
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

    void EmitElementTriggers(
        const XamlNode& triggersNode,
        std::string_view targetVar,
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

            for (const auto& act : trig->children) {
                if (act->localName == "BeginStoryboard") {
                    for (const auto& sbNode : act->children) {
                        if (sbNode->localName == "Storyboard") {
                            const std::string sbVar = EmitStoryboard(*sbNode, out, indent);
                            Indent(out, indent);
                            out << targetVar << "." << evName << " += (__s, __e) => { "
                                << sbVar << ".Begin(" << (inPartialClass_ ? "this" : targetVar) << "); };\n";
                        }
                    }
                } else if (act->localName == "ControlStoryboardAction") {
                    if (const auto* sbAttr = act->FindAttribute("Storyboard")) {
                        const auto ext = ParseMarkupExtension(sbAttr->value);
                        if (const auto* keyArg = ext.FindPositional(0)) {
                            const std::string scopeVar = inPartialClass_ ? "this" : std::string(targetVar);
                            Indent(out, indent);
                            out << targetVar << "." << evName << " += (__s, __e) => { "
                                << "var __res = " << scopeVar << ".FindResource(" << EscapeRsString(keyArg->literal)
                                << "); if (__res is Storyboard __sb) { __sb.Begin(" << scopeVar << "); } };\n";
                        }
                    }
                }
            }
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
