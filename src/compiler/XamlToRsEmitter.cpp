#include "aero/compiler/XamlToRsEmitter.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    bool precededByXmlSpace = false;
    bool pendingInlineWs = false;

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
                if (cur->localName == "TextBlock") {
                    const bool hasLeadWs = !tok.text.empty() && aero::str::Trim(tok.text.substr(0, 1)).empty();
                    const bool hasTrailWs = !tok.text.empty() && aero::str::Trim(tok.text.substr(tok.text.size() - 1)).empty();
                    if (tok.kind == XmlTokenKind::CData) {
                        cur->textContent += tok.text;
                        auto txtNode = std::make_unique<XamlNode>();
                        txtNode->rawName = "#text";
                        txtNode->localName = "#text";
                        txtNode->textContent = tok.text;
                        cur->children.push_back(std::move(txtNode));
                        cur->pendingInlineWs = false;
                    } else if (trimmed.empty()) {
                        if (!tok.text.empty()) {
                            cur->pendingInlineWs = true;
                        }
                    } else {
                        std::string chunk;
                        if ((hasLeadWs || cur->pendingInlineWs) && !cur->children.empty() &&
                            cur->children.back()->localName != "LineBreak") {
                            chunk.push_back(' ');
                        }
                        chunk.append(trimmed);
                        if (!cur->textContent.empty()) {
                            cur->textContent.push_back(' ');
                        }
                        cur->textContent.append(trimmed);

                        auto txtNode = std::make_unique<XamlNode>();
                        txtNode->rawName = "#text";
                        txtNode->localName = "#text";
                        txtNode->textContent = std::move(chunk);
                        cur->children.push_back(std::move(txtNode));
                        cur->pendingInlineWs = hasTrailWs;
                    }
                } else if (!trimmed.empty() || cur->localName == "Run") {
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
                    rawPtr->precededByXmlSpace = parent->pendingInlineWs;
                    parent->pendingInlineWs = false;
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

[[nodiscard]] std::string ConvertPathGeometryToSvgData(const XamlNode& pgNode) {
    if (const auto* figsAttr = pgNode.FindAttribute("Figures")) {
        const std::string_view v = aero::str::Trim(figsAttr->value);
        if (!v.empty()) {
            return std::string(v);
        }
    }
    std::vector<const XamlNode*> figures;
    for (const auto& ch : pgNode.children) {
        if (ch->localName == "PathFigure") {
            figures.push_back(ch.get());
        } else if (ch->localName == "PathGeometry.Figures") {
            for (const auto& fc : ch->children) {
                if (fc->localName == "PathFigure") {
                    figures.push_back(fc.get());
                }
            }
        }
    }
    if (figures.empty()) {
        return std::string(aero::str::Trim(pgNode.textContent));
    }

    auto appendSegment = [](std::ostringstream& d, const XamlNode& seg) {
        if (seg.localName == "LineSegment") {
            if (const auto* p = seg.FindAttribute("Point")) {
                d << " L " << aero::str::Trim(p->value);
            }
        } else if (seg.localName == "PolyLineSegment") {
            if (const auto* pts = seg.FindAttribute("Points")) {
                d << " L " << aero::str::Trim(pts->value);
            }
        } else if (seg.localName == "BezierSegment") {
            const auto* p1 = seg.FindAttribute("Point1");
            const auto* p2 = seg.FindAttribute("Point2");
            const auto* p3 = seg.FindAttribute("Point3");
            if (p1 && p2 && p3) {
                d << " C " << aero::str::Trim(p1->value) << " "
                  << aero::str::Trim(p2->value) << " " << aero::str::Trim(p3->value);
            }
        } else if (seg.localName == "PolyBezierSegment") {
            if (const auto* pts = seg.FindAttribute("Points")) {
                d << " C " << aero::str::Trim(pts->value);
            }
        } else if (seg.localName == "QuadraticBezierSegment") {
            const auto* p1 = seg.FindAttribute("Point1");
            const auto* p2 = seg.FindAttribute("Point2");
            if (p1 && p2) {
                d << " Q " << aero::str::Trim(p1->value) << " " << aero::str::Trim(p2->value);
            }
        } else if (seg.localName == "PolyQuadraticBezierSegment") {
            if (const auto* pts = seg.FindAttribute("Points")) {
                d << " Q " << aero::str::Trim(pts->value);
            }
        } else if (seg.localName == "ArcSegment") {
            const auto* sz = seg.FindAttribute("Size");
            const auto* pt = seg.FindAttribute("Point");
            if (pt) {
                std::vector<float> szVals;
                float rx = 0.0f;
                float ry = 0.0f;
                if (sz && TryParseFloatList(sz->value, szVals) && szVals.size() >= 2) {
                    rx = szVals[0];
                    ry = szVals[1];
                }
                float rot = 0.0f;
                if (const auto* ra = seg.FindAttribute("RotationAngle")) {
                    (void)aero::str::TryParseFloat32(ra->value, rot);
                }
                const auto* la = seg.FindAttribute("IsLargeArc");
                const int largeArc =
                    (la && aero::str::EqualsIgnoreCase(aero::str::Trim(la->value), "True")) ? 1 : 0;
                // WPF/Noesis SweepDirection defaults to Counterclockwise (SVG sweep = 0)
                const auto* sd = seg.FindAttribute("SweepDirection");
                const int sweep =
                    (sd && aero::str::EqualsIgnoreCase(aero::str::Trim(sd->value), "Clockwise")) ? 1 : 0;
                d << " A " << rx << "," << ry << " " << rot << " "
                  << largeArc << "," << sweep << " " << aero::str::Trim(pt->value);
            }
        }
    };

    std::ostringstream d;
    for (std::size_t fi = 0; fi < figures.size(); ++fi) {
        const XamlNode& fig = *figures[fi];
        if (fi > 0) d << " ";
        const auto* sp = fig.FindAttribute("StartPoint");
        d << "M " << (sp ? std::string(aero::str::Trim(sp->value)) : std::string("0,0"));
        for (const auto& ch : fig.children) {
            if (ch->localName == "PathFigure.Segments") {
                for (const auto& seg : ch->children) {
                    appendSegment(d, *seg);
                }
            } else if (ch->localName.find('.') == std::string::npos) {
                appendSegment(d, *ch);
            }
        }
        if (const auto* ic = fig.FindAttribute("IsClosed");
            ic && aero::str::EqualsIgnoreCase(aero::str::Trim(ic->value), "True")) {
            d << " Z";
        }
    }
    return d.str();
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
    float daySeconds = 0.0f;
    const std::size_t firstColon = s.find(':');
    if (firstColon != std::string_view::npos) {
        const std::size_t dotBeforeColon = s.substr(0, firstColon).find('.');
        if (dotBeforeColon != std::string_view::npos) {
            float days = 0.0f;
            if (aero::str::TryParseFloat32(s.substr(0, dotBeforeColon), days)) {
                daySeconds = days * 86400.0f;
            }
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
    if (parts.size() == 1) return daySeconds + parts[0];
    if (parts.size() == 2) return daySeconds + parts[0] * 60.0f + parts[1];
    if (parts.size() >= 3) return daySeconds + parts[0] * 3600.0f + parts[1] * 60.0f + parts[2];
    return 0.3f;
}

[[nodiscard]] std::string NormalizeGradientStopPath(std::string_view s) {
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

[[nodiscard]] bool ParseTimelineAutoReverse(const XamlNode& node) {
    if (const auto* ar = node.FindAttribute("AutoReverse")) {
        return aero::str::EqualsIgnoreCase(aero::str::Trim(ar->value), "True");
    }
    return false;
}

[[nodiscard]] int ParseTimelineRepeatCount(const XamlNode& node) {
    const auto* rb = node.FindAttribute("RepeatBehavior");
    if (!rb) return 1;
    const std::string_view raw = aero::str::Trim(rb->value);
    if (raw.empty()) return 1;
    if (aero::str::EqualsIgnoreCase(raw, "Forever")) return -1;
    std::string_view numStr = raw;
    if (!numStr.empty() && (numStr.back() == 'x' || numStr.back() == 'X')) {
        numStr.remove_suffix(1);
    }
    float count = 1.0f;
    if (aero::str::TryParseFloat32(numStr, count)) {
        return std::max(1, static_cast<int>(std::lround(count)));
    }
    return 1;
}

[[nodiscard]] std::string NormalizeTargetPropertyPath(std::string_view raw) {
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

[[nodiscard]] std::string NormalizeResourceKey(std::string_view raw) {
    std::string s(aero::str::Trim(raw));
    if (!LooksLikeMarkupExtension(s)) {
        return s;
    }
    const auto ext = ParseMarkupExtension(s);
    if (ext.kind == MarkupExtensionKind::XType) {
        std::string t;
        if (const auto* pos0 = ext.FindPositional(0)) {
            t = pos0->literal;
        } else if (const auto* tn = ext.FindNamed("TypeName")) {
            t = tn->literal;
        }
        return t.empty() ? s : StripTargetTypeName(t);
    }
    if (ext.kind == MarkupExtensionKind::XStatic) {
        std::string m;
        if (const auto* pos0 = ext.FindPositional(0)) {
            m = pos0->literal;
        } else if (const auto* mn = ext.FindNamed("Member")) {
            m = mn->literal;
        }
        m = std::string(aero::str::Trim(m));
        const std::size_t dot = m.find('.');
        const std::size_t colon = m.find(':');
        if (colon != std::string::npos && (dot == std::string::npos || colon < dot)) {
            m = m.substr(colon + 1);
        }
        return m.empty() ? s : m;
    }
    return s;
}

[[nodiscard]] std::string ResolveResourceKeyFromExtension(const MarkupExtensionNode& ext) {
    auto unwrap = [](const MarkupArgValue& val) -> std::string {
        if (!val.isExtension || !val.extension) {
            if (LooksLikeMarkupExtension(val.literal)) {
                return NormalizeResourceKey(val.literal);
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
        if (nested.kind == MarkupExtensionKind::XStatic) {
            std::string m;
            if (const auto* m0 = nested.FindPositional(0)) {
                m = m0->literal;
            } else if (const auto* mn = nested.FindNamed("Member")) {
                m = mn->literal;
            }
            m = std::string(aero::str::Trim(m));
            const std::size_t dot = m.find('.');
            const std::size_t colon = m.find(':');
            if (colon != std::string::npos && (dot == std::string::npos || colon < dot)) {
                m = m.substr(colon + 1);
            }
            return m;
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
        "FontSize", "Opacity", "SelectionOpacity", "Spacing", "Minimum", "Maximum", "Value",
        "SmallChange", "LargeChange", "StrokeThickness", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "CenterX", "CenterY", "Radius", "BlurRadius",
        "ShadowDepth", "Direction", "Angle", "AngleX", "AngleY", "ScaleX", "ScaleY",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Volume",
        "LineHeight", "CharacterSpacing",
        "ViewportSize", "MaxDropDownHeight", "HorizontalOffset", "VerticalOffset",
        "OffsetX", "OffsetY", "Spread"
    };
    return kFloatProps.count(prop) > 0;
}

[[nodiscard]] bool IsIntProperty(std::string_view prop) {
    static const std::unordered_set<std::string_view> kIntProps = {
        "ZIndex", "MaxLines", "TabIndex", "SelectedIndex", "Rows", "Columns", "FirstColumn", "MaxLength",
        "Year", "Month", "Day", "Hour", "Minute", "Second"
    };
    return kIntProps.count(prop) > 0;
}

[[nodiscard]] bool IsBoolProperty(std::string_view prop) {
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

[[nodiscard]] bool IsThicknessProperty(std::string_view prop) {
    return prop == "Margin" || prop == "Padding" || prop == "BorderThickness";
}

[[nodiscard]] bool IsBrushProperty(std::string_view prop) {
    return prop == "Background" || prop == "Foreground" || prop == "BorderBrush" ||
           prop == "Fill" || prop == "Stroke" || prop == "CaretBrush" ||
           prop == "SelectionBrush" || prop == "Brush" || prop == "OpacityMask";
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
        "Fill", "Stroke", "StrokeThickness", "StrokeDashArray", "StrokeDashCap",
        "StrokeStartLineCap", "StrokeEndLineCap", "CaretBrush", "RadiusX", "RadiusY",
        "X1", "Y1", "X2", "Y2", "Minimum", "Maximum", "Value", "SmallChange",
        "LargeChange", "IsChecked", "IsThreeState", "IsExpanded", "IsSelected",
        "IsReadOnly", "LastChildFill", "SelectedIndex", "SelectionMode",
        "Placeholder", "IsCheckable", "TabStripPlacement",
        "HorizontalScrollBarVisibility", "VerticalScrollBarVisibility",
        "RenderTransform", "LayoutTransform", "RenderTransformOrigin", "Effect", "OpacityMask", "Style", "ShowGridLines", "Rows", "Columns", "FirstColumn",
        "TickLength", "TickWidth", "MinAngle", "MaxAngle", "Brush",
        "Volume", "IsMuted", "LoadedBehavior", "Password", "PasswordChar",
        "Year", "Month", "Day", "Hour", "Minute", "Second", "Format", "UseSystemTime",
        "LineHeight", "CharacterSpacing", "MaxLines",
        "IsDirectionReversed", "IsTabStop", "TabIndex", "ViewportSize", "MaxDropDownHeight",
        "FontStyle", "IsItemsHost", "FlowDirection"
    };
    return kProps.count(prop) > 0;
}

[[nodiscard]] bool IsContentControlType(std::string_view typeName) {
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

[[nodiscard]] bool IsItemsControlType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kIC = {
        "ItemsControl", "HeaderedItemsControl", "Selector", "ListBox", "ListView",
        "ComboBox", "TabControl", "Menu", "MenuItem", "ContextMenu",
        "ToolBar", "StatusBar", "TreeView", "TreeViewItem"
    };
    return kIC.count(typeName) > 0;
}

[[nodiscard]] bool IsPanelType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kPanels = {
        "Panel", "Grid", "StackPanel", "VirtualizingStackPanel", "TabPanel",
        "ToolBarPanel", "DockPanel", "WrapPanel", "ToolBarOverflowPanel",
        "UniformGrid", "Canvas"
    };
    return kPanels.count(typeName) > 0;
}

[[nodiscard]] bool IsShapeType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kShapes = {
        "Shape", "Rectangle", "Ellipse", "Line", "Path", "Polygon", "Polyline"
    };
    return kShapes.count(typeName) > 0;
}

[[nodiscard]] bool IsRangeBaseType(std::string_view typeName) {
    return typeName == "RangeBase" || typeName == "Slider" ||
           typeName == "ProgressBar" || typeName == "ScrollBar";
}

[[nodiscard]] bool IsToggleButtonType(std::string_view typeName) {
    return typeName == "ToggleButton" || typeName == "CheckBox" || typeName == "RadioButton";
}

[[nodiscard]] bool IsSelectableItemType(std::string_view typeName) {
    return typeName == "SelectableItem" || typeName == "ListBoxItem" ||
           typeName == "ComboBoxItem" || typeName == "ListViewItem" ||
           typeName == "TabItem" || typeName == "TreeViewItem";
}

[[nodiscard]] bool IsSelectorType(std::string_view typeName) {
    return typeName == "Selector" || typeName == "ListBox" ||
           typeName == "ListView" || typeName == "ComboBox" || typeName == "TabControl";
}

[[nodiscard]] bool IsControlType(std::string_view typeName) {
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
        "NumericScrubber", "Vector2Field", "Vector3Field", "ViewportSurface",
        "DockPane", "DockTabGroup", "DockSplitNode", "DockingWorkspace",
        "CodeEditorView", "PropertyGrid", "NodeGraphCanvas"
    };
    return kControls.count(typeName) > 0;
}

[[nodiscard]] bool IsNonUIElementType(std::string_view typeName) {
    static const std::unordered_set<std::string_view> kNonUi = {
        "Inline", "Run", "LineBreak", "GridViewColumn", "GridView",
        "RowDefinition", "ColumnDefinition", "Transform", "TranslateTransform",
        "ScaleTransform", "RotateTransform", "SkewTransform", "TransformGroup",
        "Brush", "SolidColorBrush", "LinearGradientBrush", "RadialGradientBrush",
        "ImageBrush", "GradientStop", "Effect", "UIEffect", "BlurEffect",
        "DropShadowEffect", "BackdropBlurEffect", "TintEffect", "PixelateEffect",
        "ChromaticAberrationEffect", "DirectionalBlurEffect"
    };
    return kNonUi.count(typeName) > 0;
}

[[nodiscard]] bool IsKnownUIElementType(std::string_view typeName) {
    if (IsControlType(typeName) || IsPanelType(typeName) || IsShapeType(typeName)) {
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

[[nodiscard]] bool IsCommonUIElementDirectProperty(std::string_view prop) {
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

[[nodiscard]] bool IsDirectUIPropertyFor(std::string_view targetType, std::string_view prop) {
    if (!IsDirectUIProperty(prop)) {
        return false;
    }
    if (IsNonUIElementType(targetType)) {
        if (targetType == "Inline" || targetType == "Run" || targetType == "LineBreak") {
            return prop == "Text" || prop == "Foreground" || prop == "FontFamily" ||
                   prop == "FontSize" || prop == "FontWeight" || prop == "FontStyle";
        }
        if (targetType == "GridViewColumn") {
            return prop == "Width";
        }
        return false;
    }
    if (IsCommonUIElementDirectProperty(prop)) {
        return true;
    }
    if (targetType.empty() || !IsKnownUIElementType(targetType)) {
        return false;
    }

    if (prop == "Foreground" || prop == "FontFamily" || prop == "FontSize" || prop == "FontWeight") {
        return IsControlType(targetType) || targetType == "TextBlock" ||
               targetType == "ContentPresenter" || targetType == "ScrollContentPresenter";
    }
    if (prop == "FontStyle") {
        return IsControlType(targetType) || targetType == "TextBlock";
    }
    if (prop == "HorizontalContentAlignment" || prop == "VerticalContentAlignment" ||
        prop == "IsTabStop") {
        return IsControlType(targetType);
    }
    if (prop == "IsItemsHost") return IsPanelType(targetType);
    if (prop == "ShowGridLines") return targetType == "Grid";
    if (prop == "Orientation") {
        return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
               targetType == "TabPanel" || targetType == "ToolBarPanel" ||
               targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
               targetType == "ToolBar" || targetType == "Slider" ||
               targetType == "ScrollBar" || targetType == "ProgressBar" ||
               targetType == "Track";
    }
    if (prop == "Spacing") {
        return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
               targetType == "TabPanel" || targetType == "ToolBarPanel" ||
               targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
               targetType == "UniformGrid";
    }
    if (prop == "LastChildFill") return targetType == "DockPanel";
    if (prop == "Rows" || prop == "Columns" || prop == "FirstColumn") return targetType == "UniformGrid";
    if (prop == "Stretch") {
        return targetType == "Viewbox" || targetType == "LayoutScaler" ||
               targetType == "Image" || IsShapeType(targetType) ||
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
        return IsShapeType(targetType) || targetType == "TickBar" || targetType == "RadialNeedle";
    }
    if (prop == "Stroke") {
        return IsShapeType(targetType) || targetType == "TextBlock" || targetType == "RadialTicks";
    }
    if (prop == "StrokeThickness") {
        return IsShapeType(targetType) || targetType == "TextBlock";
    }
    if (prop == "StrokeDashArray" || prop == "StrokeDashCap" ||
        prop == "StrokeStartLineCap" || prop == "StrokeEndLineCap") {
        return IsShapeType(targetType);
    }
    if (prop == "CaretBrush") {
        return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
    }
    if (prop == "RadiusX" || prop == "RadiusY") return targetType == "Rectangle";
    if (prop == "X1" || prop == "Y1" || prop == "X2" || prop == "Y2") return targetType == "Line";
    if (prop == "Minimum" || prop == "Maximum") {
        return IsRangeBaseType(targetType) || targetType == "Track" || targetType == "TickBar" ||
               targetType == "RadialTicks" || targetType == "RadialNeedle";
    }
    if (prop == "Value") {
        return IsRangeBaseType(targetType) || targetType == "Track" || targetType == "RadialNeedle";
    }
    if (prop == "SmallChange" || prop == "LargeChange") return IsRangeBaseType(targetType);
    if (prop == "IsDirectionReversed") {
        return targetType == "Slider" || targetType == "ScrollBar" ||
               targetType == "Track" || targetType == "TickBar";
    }
    if (prop == "ViewportSize") return targetType == "ScrollBar" || targetType == "Track";
    if (prop == "IsChecked") return IsToggleButtonType(targetType) || targetType == "MenuItem";
    if (prop == "IsThreeState") return IsToggleButtonType(targetType);
    if (prop == "IsExpanded") return targetType == "Expander" || targetType == "TreeViewItem";
    if (prop == "ExpandDirection") return targetType == "Expander";
    if (prop == "IsSelected") return IsSelectableItemType(targetType);
    if (prop == "SelectedIndex" || prop == "SelectionMode") return IsSelectorType(targetType);
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

[[nodiscard]] bool IsFloatPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {}) {
    if (!IsFloatProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsKnownUIElementType(targetType) || IsNonUIElementType(targetType))) {
        if (IsNonUIElementType(targetType)) {
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
            return IsControlType(targetType) || targetType == "TextBlock" ||
                   targetType == "ContentPresenter" || targetType == "ScrollContentPresenter";
        }
        if (prop == "Spacing") {
            return targetType == "StackPanel" || targetType == "VirtualizingStackPanel" ||
                   targetType == "TabPanel" || targetType == "ToolBarPanel" ||
                   targetType == "WrapPanel" || targetType == "ToolBarOverflowPanel" ||
                   targetType == "UniformGrid" || targetType == "RadialTicks";
        }
        if (prop == "Minimum" || prop == "Maximum") {
            return IsRangeBaseType(targetType) || targetType == "Track" || targetType == "TickBar" ||
                   targetType == "RadialTicks" || targetType == "RadialNeedle";
        }
        if (prop == "Value") {
            return IsRangeBaseType(targetType) || targetType == "Track" || targetType == "RadialNeedle";
        }
        if (prop == "SmallChange" || prop == "LargeChange") return IsRangeBaseType(targetType);
        if (prop == "StrokeThickness") return IsShapeType(targetType) || targetType == "TextBlock";
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

    // Unknown/untyped context (e.g. untyped Setter/ChangePropertyAction): avoid coercing ambiguous string values
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

[[nodiscard]] bool IsIntPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {}) {
    if (!IsIntProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsKnownUIElementType(targetType) || IsNonUIElementType(targetType))) {
        if (prop == "ZIndex") return !IsNonUIElementType(targetType);
        if (prop == "Rows" || prop == "Columns" || prop == "FirstColumn") return targetType == "UniformGrid";
        if (prop == "MaxLines") return targetType == "TextBlock" || targetType == "TextBox";
        if (prop == "MaxLength") {
            return targetType == "TextBoxBase" || targetType == "TextBox" || targetType == "PasswordBox";
        }
        if (prop == "TabIndex") return !IsNonUIElementType(targetType);
        if (prop == "SelectedIndex") return IsSelectorType(targetType);
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

[[nodiscard]] bool IsBoolPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {}) {
    if (!IsBoolProperty(prop)) {
        return false;
    }
    if (!targetType.empty() && (IsKnownUIElementType(targetType) || IsNonUIElementType(targetType))) {
        if (IsNonUIElementType(targetType)) return false;
        if (prop == "IsEnabled" || prop == "IsHitTestVisible" || prop == "Focusable" ||
            prop == "ClipToBounds" || prop == "IsMouseOver" || prop == "IsPressed" ||
            prop == "IsFocused" || prop == "IsKeyboardFocused") {
            return true;
        }
        if (prop == "IsChecked") return IsToggleButtonType(targetType) || targetType == "MenuItem";
        if (prop == "IsThreeState") return IsToggleButtonType(targetType);
        if (prop == "IsExpanded") return targetType == "Expander" || targetType == "TreeViewItem";
        if (prop == "IsSelected") return IsSelectableItemType(targetType);
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
        if (prop == "IsTabStop") return IsControlType(targetType);
        if (prop == "IsDragging") return targetType == "Thumb" || targetType == "GridSplitter";
        if (prop == "HasItems") return IsItemsControlType(targetType);
        if (prop == "CanContentScroll") {
            return targetType == "ScrollViewer" || targetType == "ScrollContentPresenter";
        }
        if (prop == "Inside") return targetType == "BoxShadow";
        if (prop == "IsItemsHost") return IsPanelType(targetType);
        return false;
    }
    if (!rawVal.empty()) {
        const std::string_view s = aero::str::Trim(rawVal);
        return aero::str::EqualsIgnoreCase(s, "True") || aero::str::EqualsIgnoreCase(s, "False");
    }
    return true;
}

[[nodiscard]] std::uint64_t FastHashBytes64(const void* data, std::size_t size) noexcept {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(size);
    const auto* ptr = static_cast<const unsigned char*>(data);
    std::size_t i = 0u;
    while (i + 8u <= size) {
        std::uint64_t word = 0u;
        std::memcpy(&word, ptr + i, sizeof(word));
        h ^= word;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= (h >> 30u);
        i += 8u;
    }
    if (i < size) {
        std::uint64_t tail = 0u;
        std::memcpy(&tail, ptr + i, size - i);
        h ^= tail;
        h *= 0x94D049BB133111EBULL;
        h ^= (h >> 31u);
    }
    return h;
}

[[nodiscard]] std::string ReadFileTextUtf8(
    const std::filesystem::path& path,
    std::uint64_t* outRawHash = nullptr) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        if (outRawHash) {
            *outRawHash = 0u;
        }
        return {};
    }
    const std::streampos sz = in.tellg();
    std::string text;
    if (sz > 0) {
        text.resize(static_cast<std::size_t>(sz));
        in.seekg(0, std::ios::beg);
        if (!in.read(text.data(), sz)) {
            if (outRawHash) {
                *outRawHash = 0u;
            }
            return {};
        }
    }
    if (outRawHash) {
        *outRawHash = FastHashBytes64(text.data(), text.size());
    }
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEFu &&
        static_cast<unsigned char>(text[1]) == 0xBBu &&
        static_cast<unsigned char>(text[2]) == 0xBFu) {
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

[[nodiscard]] bool IsPrunableExternalResourceTag(std::string_view localName) noexcept {
    return localName == "Style" ||
           localName == "ControlTemplate" ||
           localName == "DataTemplate" ||
           localName == "HierarchicalDataTemplate" ||
           localName == "ItemsPanelTemplate";
}

[[nodiscard]] bool CanEmitLazyToolTipNode(const XamlNode& node, bool isRoot) noexcept {
    if (!node.GetName().empty()) {
        return false;
    }
    if (IsPrunableExternalResourceTag(node.localName)) {
        return false;
    }
    if (isRoot &&
        (node.localName == "SolidColorBrush" ||
         node.localName == "LinearGradientBrush" ||
         node.localName == "RadialGradientBrush" ||
         node.localName == "ImageBrush" ||
         node.localName == "String")) {
        return false;
    }
    for (const auto& attr : node.attributes) {
        if (IsEventAttribute(attr.localName)) {
            return false;
        }
        if (attr.value.find("ElementName") != std::string::npos ||
            attr.value.find("TemplatedParent") != std::string::npos ||
            attr.value.find("TemplateBinding") != std::string::npos ||
            attr.value.find("FindAncestor") != std::string::npos ||
            attr.value.find("AncestorType") != std::string::npos ||
            attr.value.find("x:Reference") != std::string::npos) {
            return false;
        }
    }
    for (const auto& child : node.children) {
        if (child && !CanEmitLazyToolTipNode(*child, false)) {
            return false;
        }
    }
    return true;
}

void ExpandControlHierarchy(std::unordered_set<std::string>& usedTypes) {
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
        if (has("PropertyGrid") || has("Vector3Field") || has("Vector2Field") || has("NumericScrubber")) {
            changed |= add("NumericScrubber");
            changed |= add("Vector2Field");
            changed |= add("Vector3Field");
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

void ExtractKeysFromExtension(
    const MarkupExtensionNode& ext,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys) {
    if (ext.kind == MarkupExtensionKind::StaticResource ||
        ext.kind == MarkupExtensionKind::DynamicResource) {
        const std::string rk = ResolveResourceKeyFromExtension(ext);
        if (!rk.empty()) {
            liveKeys.insert(rk);
            const auto* p0 = ext.FindPositional(0);
            const auto* rka = ext.FindNamed("ResourceKey");
            const MarkupArgValue* av = p0 ? p0 : rka;
            if (av && av->isExtension && av->extension &&
                av->extension->kind == MarkupExtensionKind::XType) {
                usedTypes.insert(rk);
            }
        }
    } else if (ext.kind == MarkupExtensionKind::XType) {
        if (const auto* p0 = ext.FindPositional(0)) {
            const std::string t = StripTargetTypeName(p0->literal);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
        } else if (const auto* tn = ext.FindNamed("TypeName")) {
            const std::string t = StripTargetTypeName(tn->literal);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
        }
    }
    for (const auto& arg : ext.arguments) {
        if (arg.value.isExtension && arg.value.extension) {
            ExtractKeysFromExtension(*arg.value.extension, usedTypes, liveKeys);
        } else if (!arg.value.literal.empty() && LooksLikeMarkupExtension(arg.value.literal)) {
            const auto sub = ParseMarkupExtension(arg.value.literal);
            if (sub.IsOk()) {
                ExtractKeysFromExtension(sub, usedTypes, liveKeys);
            }
        }
    }
}

void ScanXamlSubtreeForSeeds(
    const XamlNode& node,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys,
    bool skipDefinitionOwnKey,
    bool skipExternalSourceDicts) {
    if (skipExternalSourceDicts && node.localName == "ResourceDictionary" &&
        node.FindAttribute("Source") != nullptr) {
        return;
    }

    if (!node.localName.empty() && node.localName.find('.') == std::string::npos) {
        usedTypes.insert(node.localName);
        if (node.localName == "TransitionContentPresenter") {
            usedTypes.insert("ContentPresenter");
        } else if (node.localName == "RichText") {
            usedTypes.insert("TextBlock");
        }
    }

    for (const auto& attr : node.attributes) {
        if (attr.isXmlns || IsDesignOrMarkupCompatPrefix(attr.prefix, attr.namespaceUri)) {
            continue;
        }
        if (skipDefinitionOwnKey && attr.prefix == "x" && attr.localName == "Key") {
            continue;
        }
        if (attr.localName == "TargetType" || attr.localName == "DataType" ||
            attr.localName == "AncestorType") {
            const std::string t = StripTargetTypeName(attr.value);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
            continue;
        }
        if (LooksLikeMarkupExtension(attr.value)) {
            const auto ext = ParseMarkupExtension(attr.value);
            if (ext.IsOk()) {
                ExtractKeysFromExtension(ext, usedTypes, liveKeys);
            }
            continue;
        }
        if (node.localName == "StaticResource" &&
            (attr.localName == "ResourceKey" || attr.localName == "Key")) {
            const std::string val = StripTargetTypeName(attr.value);
            if (!val.empty()) {
                liveKeys.insert(val);
            }
            continue;
        }
        if (attr.localName == "BasedOn" || attr.localName == "Style" ||
            attr.localName == "Template" || attr.localName == "ItemContainerStyle" ||
            attr.localName == "ItemTemplate" || attr.localName == "ContentTemplate" ||
            attr.localName == "ItemsPanel" || attr.localName == "Storyboard") {
            const std::string val = StripTargetTypeName(attr.value);
            if (!val.empty()) {
                liveKeys.insert(val);
            }
        }
    }

    for (const auto& ch : node.children) {
        if (ch && !ShouldIgnoreXmlElement(*ch)) {
            ScanXamlSubtreeForSeeds(*ch, usedTypes, liveKeys, false, skipExternalSourceDicts);
        }
    }
    for (const auto& [k, prop] : node.propertyElements) {
        (void)k;
        if (prop) {
            ScanXamlSubtreeForSeeds(*prop, usedTypes, liveKeys, false, skipExternalSourceDicts);
        }
    }
}

void ScanHintTextForSeeds(
    std::string_view text,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys) {
    const std::size_t n = text.size();
    std::size_t i = 0u;
    while (i < n) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isalpha(ch) || ch == '_') {
            const std::size_t start = i++;
            while (i < n) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (std::isalnum(c) || c == '_') {
                    ++i;
                } else {
                    break;
                }
            }
            const std::string tok(text.substr(start, i - start));
            usedTypes.insert(tok);
            liveKeys.insert(tok);
            continue;
        }
        if (ch == '"') {
            std::string lit;
            ++i;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1u < n) {
                    ++i;
                }
                lit.push_back(text[i++]);
            }
            if (i < n && text[i] == '"') {
                ++i;
            }
            if (!lit.empty()) {
                liveKeys.insert(lit);
                if (LooksLikeMarkupExtension(lit)) {
                    const auto ext = ParseMarkupExtension(lit);
                    if (ext.IsOk()) {
                        ExtractKeysFromExtension(ext, usedTypes, liveKeys);
                    }
                }
            }
            continue;
        }
        if (ch == '{') {
            int depth = 1;
            std::size_t j = i + 1u;
            const std::size_t limit = std::min(n, i + 256u);
            while (j < limit && depth > 0 && text[j] != '\n' && text[j] != '\r') {
                if (text[j] == '{') ++depth;
                else if (text[j] == '}') --depth;
                ++j;
            }
            if (depth == 0) {
                const std::string_view snippet = text.substr(i, j - i);
                if (LooksLikeMarkupExtension(snippet)) {
                    const auto ext = ParseMarkupExtension(snippet);
                    if (ext.IsOk()) {
                        ExtractKeysFromExtension(ext, usedTypes, liveKeys);
                    }
                }
            }
        }
        ++i;
    }
}

class XamlEmitter final {
public:
    explicit XamlEmitter(const XamlEmitOptions& opts) : opts_(opts) {}

    [[nodiscard]] XamlEmitResult Run(std::string_view xamlContent) {
        hasErrors_ = false;
        XamlEmitResult res;
        res.generatedFilePath = MakeGeneratedRsPath(opts_.xamlPath);

        std::unique_ptr<XamlNode> root;
        if (!BuildXamlDom(xamlContent, root, diagnostics_)) {
            hasErrors_ = true;
            res.succeeded = false;
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

        if (opts_.pruneUnreferencedExternalStyles && root->localName != "ResourceDictionary") {
            PrecomputeLiveExternalStyleNodes(*root, opts_.xamlPath);
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

        res.succeeded = !hasErrors_;
        res.moduleName = moduleName_;
        res.rootClassName = rootClassName_;
        res.generatedRsCode = out.str();
        res.externalDependencies = std::move(externalDependencies_);
        res.diagnostics = diagnostics_;
        return res;
    }

private:
    struct ParsedExternalDoc final {
        std::string resolvedPath;
        std::uint64_t hash = 0u;
        std::unique_ptr<XamlNode> root;
    };

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
    std::unordered_map<std::string, const XamlNode*> knownStoryboards_;
    std::unordered_set<std::string> visitedExternalXamls_;
    std::unordered_map<std::string, ParsedExternalDoc> externalDocCache_;
    std::unordered_map<std::string, std::string> externalPathLookup_;
    std::vector<ExternalXamlDep> externalDependencies_;
    std::unordered_set<std::string> recordedExternalDeps_;
    std::vector<const XamlNode*> externalPrunableList_;
    std::unordered_set<const XamlNode*> externalPrunableCandidates_;
    std::unordered_set<const XamlNode*> liveExternalNodes_;
    std::vector<const XamlNode*> externalNonPrunableNodes_;
    bool hasLiveExternalFilter_ = false;
    int externalDictDepth_ = 0;
    std::vector<std::string> diagnostics_;
    bool hasErrors_ = false;
    std::unordered_set<std::string> emittedWarnings_;
    int nextId_ = 0;
    bool inPartialClass_ = false;
    bool emittedLocSources_ = false;
    std::string currentTplParentVar_;

    void AddError(std::string msg) {
        hasErrors_ = true;
        diagnostics_.push_back(std::move(msg));
    }

    void AddWarning(std::string_view msg) {
        std::string formatted = aero::str::StartsWith(msg, "[Warning]")
            ? std::string(msg)
            : ("[Warning] " + std::string(msg));
        if (emittedWarnings_.insert(formatted).second) {
            diagnostics_.push_back(std::move(formatted));
        }
    }

    void RecordExternalDependency(std::string_view normPath, std::uint64_t contentHash) {
        if (normPath.empty()) {
            return;
        }
        const std::string key(normPath);
        if (recordedExternalDeps_.insert(key).second) {
            externalDependencies_.push_back(ExternalXamlDep{key, contentHash});
        }
    }

    [[nodiscard]] const ParsedExternalDoc* ResolveAndLoadExternalDoc(
        std::string_view requestedSource,
        std::string_view currentXamlPath,
        bool reportErrors) {
        const std::string cleanRel = NormalizePackSourcePath(requestedSource);
        if (cleanRel.empty()) {
            return nullptr;
        }

        const std::string pairKey = std::string(currentXamlPath) + "|" + cleanRel;
        if (const auto lookupIt = externalPathLookup_.find(pairKey); lookupIt != externalPathLookup_.end()) {
            if (const auto docIt = externalDocCache_.find(lookupIt->second); docIt != externalDocCache_.end()) {
                return &docIt->second;
            }
        }

        std::string resolvedPath;
        std::string xamlContent;
        std::uint64_t rawHash = 0u;
        bool loaded = false;

        if (opts_.externalXamlResolver) {
            loaded = opts_.externalXamlResolver(cleanRel, currentXamlPath, resolvedPath, xamlContent);
            if (loaded) {
                rawHash = FastHashBytes64(xamlContent.data(), xamlContent.size());
            }
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
                    if (const auto existingIt = externalDocCache_.find(resolvedPath);
                        existingIt != externalDocCache_.end()) {
                        externalPathLookup_[pairKey] = resolvedPath;
                        return &existingIt->second;
                    }
                    xamlContent = ReadFileTextUtf8(cand, &rawHash);
                    loaded = !xamlContent.empty();
                    if (loaded) {
                        break;
                    }
                }
            }
        }

        if (!loaded) {
            if (reportErrors) {
                AddError(
                    "Could not resolve MergedDictionary Source '" + std::string(requestedSource) +
                    "' referenced from '" + std::string(currentXamlPath) + "'");
            }
            return nullptr;
        }
        if (resolvedPath.empty()) {
            resolvedPath = cleanRel;
        }
        externalPathLookup_[pairKey] = resolvedPath;
        if (const auto existingIt = externalDocCache_.find(resolvedPath);
            existingIt != externalDocCache_.end()) {
            return &existingIt->second;
        }

        std::vector<std::string> tempDiag;
        std::unique_ptr<XamlNode> mergedRoot;
        if (!BuildXamlDom(xamlContent, mergedRoot, reportErrors ? diagnostics_ : tempDiag) || !mergedRoot) {
            if (reportErrors) {
                hasErrors_ = true;
            }
            return nullptr;
        }
        RecordExternalDependency(resolvedPath, rawHash);
        ParsedExternalDoc doc;
        doc.resolvedPath = resolvedPath;
        doc.hash = rawHash;
        doc.root = std::move(mergedRoot);
        const auto [insertedIt, unused] = externalDocCache_.emplace(resolvedPath, std::move(doc));
        (void)unused;
        return &insertedIt->second;
    }

    [[nodiscard]] static bool IsThemeResourceDictPath(std::string_view path) noexcept {
        if (path.empty()) {
            return false;
        }
        return path.find("NoesisTheme") != std::string_view::npos ||
               path.find("/Theme/") != std::string_view::npos ||
               path.find("\\Theme\\") != std::string_view::npos ||
               path.find("/Themes/") != std::string_view::npos ||
               path.find("\\Themes\\") != std::string_view::npos;
    }

    [[nodiscard]] static bool HasDynamicXamlLoadingHint(
        const std::vector<std::string>& hints) noexcept {
        for (const std::string& hint : hints) {
            if (hint.find("XamlReader.Load") != std::string::npos ||
                hint.find("XamlReader.Parse") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void CollectExternalDictNodes(
        const XamlNode& dictNode,
        std::string_view currentXamlPath) {
        if (const auto* rootSrc = dictNode.FindAttribute("Source")) {
            CollectExternalDictInclude(rootSrc->value, currentXamlPath);
        }
        if (const auto* mergedEl = dictNode.FindPropertyElement("ResourceDictionary", "MergedDictionaries")) {
            for (const auto& child : mergedEl->children) {
                if (!child || ShouldIgnoreXmlElement(*child)) continue;
                if (child->localName == "ResourceDictionary") {
                    if (const auto* srcAttr = child->FindAttribute("Source")) {
                        CollectExternalDictInclude(srcAttr->value, currentXamlPath);
                    } else {
                        CollectExternalDictNodes(*child, currentXamlPath);
                    }
                }
            }
        }
        for (const auto& child : dictNode.children) {
            if (!child || ShouldIgnoreXmlElement(*child)) continue;
            if (child->localName == "ResourceDictionary") {
                if (const auto* srcAttr = child->FindAttribute("Source")) {
                    CollectExternalDictInclude(srcAttr->value, currentXamlPath);
                } else {
                    CollectExternalDictNodes(*child, currentXamlPath);
                }
                continue;
            }
            if (externalDictDepth_ > 0) {
                if (IsThemeResourceDictPath(currentXamlPath) &&
                    IsPrunableExternalResourceTag(child->localName)) {
                    if (externalPrunableCandidates_.insert(child.get()).second) {
                        externalPrunableList_.push_back(child.get());
                    }
                } else {
                    externalNonPrunableNodes_.push_back(child.get());
                }
            }
        }
    }

    void CollectExternalDictInclude(
        std::string_view requestedSource,
        std::string_view currentXamlPath) {
        const ParsedExternalDoc* doc = ResolveAndLoadExternalDoc(requestedSource, currentXamlPath, false);
        if (!doc || !doc->root) {
            return;
        }
        if (!visitedExternalXamls_.insert(doc->resolvedPath).second) {
            return;
        }
        ++externalDictDepth_;
        CollectExternalDictNodes(*doc->root, doc->resolvedPath);
        --externalDictDepth_;
        visitedExternalXamls_.erase(doc->resolvedPath);
    }

    void WalkTreeForExternalDictRefs(const XamlNode& node, std::string_view currentXamlPath) {
        if (node.localName == "ResourceDictionary") {
            if (const auto* src = node.FindAttribute("Source")) {
                CollectExternalDictInclude(src->value, currentXamlPath);
            }
        }
        for (const auto& ch : node.children) {
            if (ch && !ShouldIgnoreXmlElement(*ch)) {
                WalkTreeForExternalDictRefs(*ch, currentXamlPath);
            }
        }
        for (const auto& [k, prop] : node.propertyElements) {
            (void)k;
            if (prop) {
                WalkTreeForExternalDictRefs(*prop, currentXamlPath);
            }
        }
    }

    void PrecomputeLiveExternalStyleNodes(
        const XamlNode& primaryRoot,
        std::string_view currentXamlPath) {
        externalPrunableList_.clear();
        externalPrunableCandidates_.clear();
        liveExternalNodes_.clear();
        externalNonPrunableNodes_.clear();
        hasLiveExternalFilter_ = false;

        if (HasDynamicXamlLoadingHint(opts_.usageHintTexts)) {
            return;
        }

        WalkTreeForExternalDictRefs(primaryRoot, currentXamlPath);
        if (externalPrunableList_.empty()) {
            return;
        }

        hasLiveExternalFilter_ = true;
        std::unordered_set<std::string> usedTypes;
        std::unordered_set<std::string> liveKeys;

        ScanXamlSubtreeForSeeds(primaryRoot, usedTypes, liveKeys, false, true);
        for (const XamlNode* nonPrune : externalNonPrunableNodes_) {
            if (nonPrune) {
                ScanXamlSubtreeForSeeds(*nonPrune, usedTypes, liveKeys, false, false);
            }
        }
        for (const std::string& hint : opts_.usageHintTexts) {
            ScanHintTextForSeeds(hint, usedTypes, liveKeys);
        }
        ExpandControlHierarchy(usedTypes);

        bool changed = true;
        while (changed) {
            changed = false;
            for (const XamlNode* cand : externalPrunableList_) {
                if (!cand || liveExternalNodes_.count(cand) > 0u) {
                    continue;
                }
                const std::string rawKey = cand->GetKey();
                bool isLive = false;
                if (!rawKey.empty()) {
                    std::string effKey = rawKey;
                    if (LooksLikeMarkupExtension(effKey)) {
                        effKey = NormalizeResourceKey(effKey);
                    }
                    if (liveKeys.count(rawKey) > 0u || liveKeys.count(effKey) > 0u) {
                        isLive = true;
                    } else if (cand->localName == "Style") {
                        if (const auto* tt = cand->FindAttribute("TargetType")) {
                            const std::string ttName = StripTargetTypeName(tt->value);
                            if (!ttName.empty() && effKey == ttName && usedTypes.count(ttName) > 0u) {
                                isLive = true;
                            }
                        }
                    }
                } else {
                    if (cand->localName == "Style") {
                        if (const auto* tt = cand->FindAttribute("TargetType")) {
                            const std::string ttName = StripTargetTypeName(tt->value);
                            if (ttName.empty() || usedTypes.count(ttName) > 0u) {
                                isLive = true;
                            }
                        } else {
                            isLive = true;
                        }
                    } else {
                        isLive = true;
                    }
                }
                if (isLive) {
                    liveExternalNodes_.insert(cand);
                    ScanXamlSubtreeForSeeds(*cand, usedTypes, liveKeys, true, false);
                    ExpandControlHierarchy(usedTypes);
                    changed = true;
                }
            }
        }
    }

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
        const ParsedExternalDoc* doc = ResolveAndLoadExternalDoc(
            requestedSource, currentXamlPath, true);
        if (!doc || !doc->root) {
            return {};
        }
        if (!visitedExternalXamls_.insert(doc->resolvedPath).second) {
            AddError("Circular ResourceDictionary Source detected: " + doc->resolvedPath);
            return {};
        }
        ++externalDictDepth_;
        EmitResourceDictionaryEntries(*doc->root, doc->resolvedPath, targetDictVar, out, indent);
        --externalDictDepth_;
        visitedExternalXamls_.erase(doc->resolvedPath);
        return doc->resolvedPath;
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
        if (hasLiveExternalFilter_ &&
            externalDictDepth_ > 0 &&
            externalPrunableCandidates_.count(&child) > 0u &&
            liveExternalNodes_.count(&child) == 0u) {
            return;
        }
        std::string key = NormalizeResourceKey(child.GetKey());
        if (child.localName == "Style" && key.empty()) {
            if (const auto* tt = child.FindAttribute("TargetType")) {
                key = StripTargetTypeName(tt->value);
            }
        } else if ((child.localName == "DataTemplate" || child.localName == "HierarchicalDataTemplate") && key.empty()) {
            if (const auto* dt = child.FindAttribute("DataType")) {
                key = StripTargetTypeName(dt->value);
            } else if (const auto* xdt = child.FindXAttribute("DataType")) {
                key = StripTargetTypeName(xdt->value);
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
            knownStoryboards_[key] = &child;
            const std::string v = EmitStoryboard(child, out, indent, dictVar);
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
        } else if (child.localName == "ImageSource") {
            const std::string uri = std::string(aero::str::Trim(child.textContent));
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
                << EscapeRsString(uri) << "));\n";
        } else if (child.localName == "StreamGeometry" ||
                   child.localName == "PathGeometry" ||
                   child.localName == "Geometry") {
            const std::string geom = (child.localName == "PathGeometry")
                ? ConvertPathGeometryToSvgData(child)
                : std::string(aero::str::Trim(child.textContent));
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

    void EmitGradientBrushCommonProps(
        const XamlNode& brushNode,
        std::string_view bVar,
        std::ostringstream& out,
        int indent) {
        if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
            float op = 1.0f;
            if (aero::str::TryParseFloat32(opAttr->value, op)) {
                Indent(out, indent);
                out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
            }
        }
        if (const auto* mmAttr = brushNode.FindAttribute("MappingMode")) {
            const std::string_view mm = aero::str::Trim(mmAttr->value);
            if (aero::str::EqualsIgnoreCase(mm, "Absolute")) {
                Indent(out, indent);
                out << bVar << ".MappingMode = 1;\n";
            } else if (aero::str::EqualsIgnoreCase(mm, "RelativeToBoundingBox")) {
                Indent(out, indent);
                out << bVar << ".MappingMode = 0;\n";
            }
        }
        if (const auto* smAttr = brushNode.FindAttribute("SpreadMethod")) {
            const std::string_view sm = aero::str::Trim(smAttr->value);
            if (aero::str::EqualsIgnoreCase(sm, "Reflect")) {
                Indent(out, indent);
                out << bVar << ".SpreadMethod = 1;\n";
            } else if (aero::str::EqualsIgnoreCase(sm, "Repeat")) {
                Indent(out, indent);
                out << bVar << ".SpreadMethod = 2;\n";
            } else if (aero::str::EqualsIgnoreCase(sm, "Pad")) {
                Indent(out, indent);
                out << bVar << ".SpreadMethod = 0;\n";
            }
        }
    }

    [[nodiscard]] const XamlNode* FindBrushShaderElement(const XamlNode& brushNode) const {
        if (const auto* shProp = brushNode.FindPropertyElement(brushNode.localName, "Shader")) {
            return shProp;
        }
        return brushNode.FindPropertyElement("Brush", "Shader");
    }

    void EmitImageBrushPlacementAttrs(
        const XamlNode& brushNode,
        std::string_view bVar,
        std::ostringstream& out,
        int indent) const {
        if (const auto* vbAttr = brushNode.FindAttribute("Viewbox")) {
            const std::string_view vb = aero::str::Trim(vbAttr->value);
            if (!vb.empty()) {
                Indent(out, indent);
                out << bVar << ".Viewbox = " << EscapeRsString(vb) << ";\n";
            }
        }
        if (const auto* vpAttr = brushNode.FindAttribute("Viewport")) {
            const std::string_view vp = aero::str::Trim(vpAttr->value);
            if (!vp.empty()) {
                Indent(out, indent);
                out << bVar << ".Viewport = " << EscapeRsString(vp) << ";\n";
            }
        }
        if (const auto* vpuAttr = brushNode.FindAttribute("ViewportUnits")) {
            const std::string_view vpu = aero::str::Trim(vpuAttr->value);
            if (!vpu.empty()) {
                Indent(out, indent);
                out << bVar << ".ViewportUnits = " << EscapeRsString(vpu) << ";\n";
            }
        }
        if (const auto* stAttr = brushNode.FindAttribute("Stretch")) {
            const std::string_view st = aero::str::Trim(stAttr->value);
            if (!st.empty()) {
                Indent(out, indent);
                out << bVar << ".Stretch = Stretch." << st << ";\n";
            }
        }
        if (const auto* tmAttr = brushNode.FindAttribute("TileMode")) {
            const std::string_view tmVal = aero::str::Trim(tmAttr->value);
            if (!tmVal.empty()) {
                Indent(out, indent);
                out << bVar << ".TileMode = " << EscapeRsString(tmVal) << ";\n";
            }
        }
    }

    [[nodiscard]] std::string EmitShaderFallbackBrush(
        const XamlNode& brushNode,
        const XamlNode& shaderNode,
        std::ostringstream& out,
        int indent) {
        const std::string& shName = shaderNode.localName;
        const std::string bVar = FreshVar("shgrad");
        Indent(out, indent);

        auto emitOpacity = [&]() {
            if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
                float op = 1.0f;
                if (aero::str::TryParseFloat32(opAttr->value, op)) {
                    Indent(out, indent);
                    out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
                }
            }
        };

        if (shName == "PlasmaBrush") {
            float sx = 64.0f, sy = 64.0f, tm = 0.0f, seed = 100.0f, vw = 300.0f, vh = 90.0f;
            std::string col = "#FF01BCFF";
            if (const auto* a = shaderNode.FindAttribute("ScaleX")) (void)aero::str::TryParseFloat32(a->value, sx);
            if (const auto* a = shaderNode.FindAttribute("ScaleY")) (void)aero::str::TryParseFloat32(a->value, sy);
            if (const auto* a = shaderNode.FindAttribute("Time")) (void)aero::str::TryParseFloat32(a->value, tm);
            if (const auto* a = shaderNode.FindAttribute("Seed"); a && !LooksLikeMarkupExtension(a->value)) {
                (void)aero::str::TryParseFloat32(a->value, seed);
            }
            if (const auto* a = shaderNode.FindAttribute("Color"); a && !LooksLikeMarkupExtension(a->value)) {
                col = ResolveColorHexOrString(a->value);
            }
            if (const auto* vpAttr = brushNode.FindAttribute("Viewport")) {
                std::vector<float> vpts;
                if (TryParseFloatList(vpAttr->value, vpts) && vpts.size() >= 4u) {
                    vw = vpts[2];
                    vh = vpts[3];
                }
            }
            out << "var " << bVar << " = new ImageBrush(\"shader://plasma\");\n";
            EmitImageBrushPlacementAttrs(brushNode, bVar, out, indent);
            Indent(out, indent);
            out << bVar << ".EnablePlasmaShader("
                << FormatFloatLiteral(sx) << ", " << FormatFloatLiteral(sy) << ", "
                << FormatFloatLiteral(tm) << ", " << FormatFloatLiteral(seed) << ", "
                << EscapeRsString(col) << ", "
                << FormatFloatLiteral(vw) << ", " << FormatFloatLiteral(vh) << ");\n";
            emitOpacity();
            return bVar;
        }

        if (shName == "ConicGradientBrush") {
            out << "var " << bVar << " = new ConicGradientBrush();\n";
            std::vector<std::pair<std::string, float>> stops;
            const XamlNode* stopContainer = shaderNode.FindPropertyElement("ConicGradientBrush", "GradientStops");
            const auto& stopNodes = (stopContainer != nullptr) ? stopContainer->children : shaderNode.children;
            for (const auto& st : stopNodes) {
                if (!st || st->localName != "GradientStop") continue;
                std::string cHex = "#FFFFFFFF";
                if (const auto* cAttr = st->FindAttribute("Color")) {
                    cHex = ResolveColorHexOrString(cAttr->value);
                }
                float off = 0.0f;
                if (const auto* oAttr = st->FindAttribute("Offset")) {
                    (void)aero::str::TryParseFloat32(oAttr->value, off);
                }
                stops.emplace_back(std::move(cHex), off);
            }
            if (stops.size() >= 2u) {
                for (const auto& [cHex, off] : stops) {
                    Indent(out, indent);
                    out << bVar << ".AddStop(" << EscapeRsString(cHex) << ", "
                        << FormatFloatLiteral(off) << ");\n";
                }
            } else {
                Indent(out, indent);
                out << bVar << ".AddStop(\"#FF06B6D4\", 0.0f);\n";
                Indent(out, indent);
                out << bVar << ".AddStop(\"#FF3B82F6\", 0.25f);\n";
                Indent(out, indent);
                out << bVar << ".AddStop(\"#FFA855F7\", 0.5f);\n";
                Indent(out, indent);
                out << bVar << ".AddStop(\"#FFEC4899\", 0.75f);\n";
                Indent(out, indent);
                out << bVar << ".AddStop(\"#FFF59E0B\", 1.0f);\n";
            }
            emitOpacity();
            return bVar;
        }

        if (shName == "SaturationBrush") {
            std::string baseImgSrc;
            if (const auto* sa = brushNode.FindAttribute("ImageSource")) {
                if (!LooksLikeMarkupExtension(sa->value)) baseImgSrc = sa->value;
            }
            if (!baseImgSrc.empty()) {
                float satVal = 1.0f;
                if (const auto* satAttr = shaderNode.FindAttribute("Saturation")) {
                    (void)aero::str::TryParseFloat32(satAttr->value, satVal);
                }
                std::string satLit = FormatFloatLiteral(satVal);
                if (!satLit.empty() && (satLit.back() == 'f' || satLit.back() == 'F')) {
                    satLit.pop_back();
                }
                const std::string satUri = "shader://saturation?sat=" + satLit + "&src=" + baseImgSrc;
                out << "var " << bVar << " = new ImageBrush(" << EscapeRsString(satUri) << ");\n";
                EmitImageBrushPlacementAttrs(brushNode, bVar, out, indent);
                emitOpacity();
                return bVar;
            }
        }

        out << "var " << bVar << " = new LinearGradientBrush(0.0f, 0.0f, 1.0f, 1.0f);\n";
        Indent(out, indent);
        out << bVar << ".AddStop(\"#FF64748B\", 0.0f);\n";
        Indent(out, indent);
        out << bVar << ".AddStop(\"#FF94A3B8\", 0.5f);\n";
        Indent(out, indent);
        out << bVar << ".AddStop(\"#FFCBD5E1\", 1.0f);\n";
        emitOpacity();
        return bVar;
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
            } else if (!aero::str::Trim(brushNode.textContent).empty()) {
                color = ResolveColorHexOrString(aero::str::Trim(brushNode.textContent));
            }
            // Check shader child tint override (Brush.Shader -> SaturationBrush / ConicGradientBrush / PlasmaBrush)
            if (const auto* shProp = FindBrushShaderElement(brushNode)) {
                if (!shProp->children.empty() && dynamicColorKey.empty()) {
                    const std::string& shName = shProp->children.front()->localName;
                    if (shName == "PlasmaBrush" || shName == "ConicGradientBrush" || shName == "SaturationBrush") {
                        return EmitShaderFallbackBrush(brushNode, *shProp->children.front(), out, indent);
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
            std::string src;
            if (const auto* sa = brushNode.FindAttribute("ImageSource")) {
                if (!LooksLikeMarkupExtension(sa->value)) src = sa->value;
            }
            if (const auto* shProp = FindBrushShaderElement(brushNode);
                shProp != nullptr && !shProp->children.empty()) {
                return EmitShaderFallbackBrush(brushNode, *shProp->children.front(), out, indent);
            }
            const std::string bVar = FreshVar("ibrush");
            Indent(out, indent);
            out << "var " << bVar << " = new ImageBrush(" << EscapeRsString(src) << ");\n";
            EmitImageBrushPlacementAttrs(brushNode, bVar, out, indent);
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
            EmitGradientBrushCommonProps(brushNode, bVar, out, indent);
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
            EmitGradientBrushCommonProps(brushNode, bVar, out, indent);
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
        int indent,
        std::string_view hostUiVar = {},
        std::string_view xDataType = {},
        std::string_view dictVar = {},
        std::string_view transformPropPrefix = "RenderTransform") {
        auto getF = [&](std::string_view name, float def) -> float {
            if (const auto* a = node.FindAttribute(name)) {
                float v = def;
                if (aero::str::TryParseFloat32(a->value, v)) return v;
            }
            return def;
        };
        auto emitTransformPropBindings = [&](std::initializer_list<const char*> props) {
            if (hostUiVar.empty()) {
                return;
            }
            for (const char* prop : props) {
                const std::string targetProp = std::string(transformPropPrefix) + "." + prop;
                if (const auto* a = node.FindAttribute(prop);
                    a != nullptr && LooksLikeMarkupExtension(a->value)) {
                    EmitPropertyAttributeOrBinding(
                        hostUiVar, "UIElement", targetProp, a->value, xDataType, dictVar, out, indent);
                }
                if (const XamlNode* propEl = node.FindPropertyElement(node.localName, prop);
                    propEl != nullptr && !propEl->children.empty()) {
                    const auto& firstChild = *propEl->children.front();
                    if (firstChild.localName == "Binding") {
                        ParsedBindingSpec spec;
                        std::string convVar;
                        TryParseBindingNode(firstChild, spec, convVar, dictVar, out, indent);
                        EmitBindingSpec(
                            hostUiVar, "UIElement", targetProp, spec, xDataType, out, indent, {}, convVar);
                    } else if (firstChild.localName == "MultiBinding") {
                        EmitMultiBindingNode(
                            hostUiVar, "UIElement", targetProp, firstChild, dictVar, out, indent);
                    }
                }
            }
        };

        if (node.localName == "TranslateTransform") {
            const std::string v = FreshVar("tr");
            Indent(out, indent);
            out << "var " << v << " = new TranslateTransform("
                << FormatFloatLiteral(getF("X", 0.0f)) << ", "
                << FormatFloatLiteral(getF("Y", 0.0f)) << ");\n";
            emitTransformPropBindings({"X", "Y"});
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
            emitTransformPropBindings({"ScaleX", "ScaleY"});
            return v;
        }
        if (node.localName == "RotateTransform") {
            const std::string v = FreshVar("rot");
            Indent(out, indent);
            out << "var " << v << " = new RotateTransform("
                << FormatFloatLiteral(getF("Angle", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterX", 0.0f)) << ", "
                << FormatFloatLiteral(getF("CenterY", 0.0f)) << ");\n";
            emitTransformPropBindings({"Angle"});
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
            emitTransformPropBindings({"AngleX", "AngleY"});
            return v;
        }
        if (node.localName == "TransformGroup") {
            const std::string tg = FreshVar("tg");
            Indent(out, indent);
            out << "var " << tg << " = new TransformGroup();\n";
            const XamlNode* childProp = node.FindPropertyElement("TransformGroup", "Children");
            const auto& items = (childProp != nullptr) ? childProp->children : node.children;
            for (const auto& item : items) {
                const std::string sub = EmitTransformObject(
                    *item, out, indent, hostUiVar, xDataType, dictVar, transformPropPrefix);
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

        if (const XamlNode* resProp = styleNode.FindPropertyElement("Style", "Resources")) {
            const std::string styleDictVar = styVar + ".Resources";
            for (const auto& rc : resProp->children) {
                if (!rc || ShouldIgnoreXmlElement(*rc)) {
                    continue;
                }
                if (rc->localName == "ResourceDictionary") {
                    EmitResourceDictionaryEntries(*rc, "", styleDictVar, out, indent);
                } else {
                    EmitSingleDirectResource(*rc, styleDictVar, out, indent);
                }
            }
        }

        for (const auto& child : styleNode.children) {
            if (child->localName == "Setter") {
                EmitSetterInto(*child, styVar, dictVar, out, indent, targetType);
            }
        }

        if (const XamlNode* trigProp = styleNode.FindPropertyElement("Style", "Triggers")) {
            EmitTriggerCollectionInto(*trigProp, styVar, dictVar, out, indent, targetType);
        }
        return styVar;
    }

    static std::string NormalizeSetterPropertyName(std::string_view rawProp) {
        std::string propName(aero::str::Trim(rawProp));
        if (propName.size() >= 2 && propName.front() == '(' && propName.back() == ')') {
            propName = propName.substr(1, propName.size() - 2);
        }
        const std::size_t dot = propName.find('.');
        const std::size_t colon = propName.find(':');
        if (colon != std::string::npos && (dot == std::string::npos || colon < dot)) {
            propName = propName.substr(colon + 1);
        }
        const std::size_t cleanDot = propName.find('.');
        if (cleanDot != std::string::npos) {
            const std::string_view owner = std::string_view(propName).substr(0, cleanDot);
            if (owner == "Control" || owner == "FrameworkElement" || owner == "UIElement" ||
                owner == "TextBlock" || owner == "Panel") {
                propName = propName.substr(cleanDot + 1);
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
               propKey == "Element.BlendingMode" || propKey == "RiveControl.Source" ||
               aero::str::StartsWith(propKey, "Custom.") ||
               aero::str::StartsWith(propKey, "DragDrop.") ||
               aero::str::StartsWith(propKey, "ToolTipService.");
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
        std::string_view typedExpr,
        std::string_view targetType = {}) const {
        if (typedExpr == "null") {
            return "null";
        }
        if (typedExpr == "ParseUtil.NaN()") {
            return "DependencyObject.BoxFloat(ParseUtil.NaN())";
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
        if (typedExpr == "true" || typedExpr == "false") {
            return "DependencyObject.BoxBool(" + std::string(typedExpr) + ")";
        }
        const bool looksNumeric = !typedExpr.empty() &&
            (std::isdigit(static_cast<unsigned char>(typedExpr.front())) || typedExpr.front() == '-');
        if (looksNumeric && IsIntPropertyFor(targetType, propName)) {
            if (typedExpr.back() == 'f' || typedExpr.find('.') != std::string_view::npos) {
                return "DependencyObject.BoxInt((int)(" + std::string(typedExpr) + "))";
            }
            return "DependencyObject.BoxInt(" + std::string(typedExpr) + ")";
        }
        if (looksNumeric && IsFloatPropertyFor(targetType, propName)) {
            return "DependencyObject.BoxFloat(" + std::string(typedExpr) + ")";
        }
        if (typedExpr.size() >= 2 && typedExpr.back() == 'f' && looksNumeric) {
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
        int indent,
        std::string_view styleTargetType = {}) {
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

        std::string propName = NormalizeSetterPropertyName(pAttr->value);
        if (propName.empty()) {
            return;
        }
        if (propName == "noesis:RichText.Text" || propName == "RichText.Text") {
            propName = "RichText.Text";
            Indent(out, indent);
            out << "RichText.Register();\n";
        }
        if (aero::str::StartsWith(propName, "DragDrop.")) {
            Indent(out, indent);
            out << "DragDrop.EnsureInstalled();\n";
        }
        if (propName == "ToolTip" || propName == "ToolTipService.ToolTip" ||
            aero::str::StartsWith(propName, "ToolTipService.")) {
            Indent(out, indent);
            out << "ToolTipService.EnsureInstalled();\n";
        }
        std::string targetName;
        if (const auto* tnAttr = setterNode.FindAttribute("TargetName")) {
            targetName = tnAttr->value;
        }
        const std::string_view effTargetType = targetName.empty() ? styleTargetType : std::string_view{};

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
                if (c->localName == "TranslateTransform" ||
                    c->localName == "ScaleTransform" ||
                    c->localName == "RotateTransform" ||
                    c->localName == "SkewTransform" ||
                    c->localName == "TransformGroup") {
                    valExpr = EmitTransformObject(*c, out, indent, {}, {}, dictVar);
                    break;
                }
                if (c->localName == "CompositeTransform3D") {
                    valExpr = EmitCompositeTransform3DObject(*c, out, indent);
                    break;
                }
            }
            if (valExpr.empty() && !valEl->textContent.empty()) {
                valExpr = FormatTypedValueLiteral(propName, valEl->textContent, dictVar, effTargetType);
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
                } else if (ext.kind == MarkupExtensionKind::DynamicResource) {
                    const std::string key = ResolveResourceKeyFromExtension(ext);
                    if (!key.empty()) {
                        Indent(out, indent);
                        out << targetObjVar << ".AddSetter(Setter.FromDynamicResource("
                            << EscapeRsString(propName) << ", "
                            << EscapeRsString(key) << ", "
                            << EscapeRsString(targetName) << "));\n";
                        return;
                    }
                }
            }
            if (valExpr.empty()) {
                valExpr = FormatTypedValueLiteral(propName, valAttr->value, dictVar, effTargetType);
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
            propName == "RenderTransform" || propName == "Element.Transform3D" ||
            propName == "Transform3D" || aero::str::StartsWith(propName, "DragDrop.") ||
            aero::str::StartsWith(propName, "ToolTipService.") ||
            aero::str::StartsWith(valExpr, "this.FindResource(") ||
            valExpr.find(".FindResource(") != std::string::npos ||
            valExpr.find(".Get(") != std::string::npos ||
            valExpr.find(".GetBox(") != std::string::npos) {
            out << targetObjVar << ".AddSetterBox(" << EscapeRsString(propName) << ", "
                << BoxSetterValueExpr(propName, valExpr, effTargetType) << ", "
                << EscapeRsString(targetName) << ");\n";
        } else {
            out << targetObjVar << ".AddSetter(" << EscapeRsString(propName) << ", " << valExpr << ");\n";
        }
    }

    void EmitTriggerEnterExitActions(
        const XamlNode& trigNode,
        std::string_view trigTypeName,
        std::string_view trigVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        auto emitActionList = [&](std::string_view propName, std::string_view methodPrefix) {
            const XamlNode* propEl = trigNode.FindPropertyElement(trigTypeName, propName);
            if (!propEl) {
                return;
            }
            for (const auto& act : propEl->children) {
                if (!act || act->localName != "BeginStoryboard") {
                    continue;
                }
                if (const auto* sbAttr = act->FindAttribute("Storyboard");
                    sbAttr != nullptr && LooksLikeMarkupExtension(sbAttr->value)) {
                    const std::string rk = ResolveResourceKeyFromExtension(ParseMarkupExtension(sbAttr->value));
                    if (!rk.empty()) {
                        Indent(out, indent);
                        out << trigVar << "." << methodPrefix << "ActionKey(" << EscapeRsString(rk) << ");\n";
                    }
                }
                for (const auto& sbNode : act->children) {
                    if (sbNode && sbNode->localName == "Storyboard") {
                        const std::string sb = EmitStoryboard(*sbNode, out, indent, dictVar);
                        Indent(out, indent);
                        out << trigVar << "." << methodPrefix << "Action(" << sb << ");\n";
                    }
                }
            }
        };
        emitActionList("EnterActions", "AddEnter");
        emitActionList("ExitActions", "AddExit");
    }

    [[nodiscard]] std::string FormatTriggerValueBoxExpr(
        std::string_view propOrPath,
        std::string_view rawVal,
        std::string_view dictVar) const {
        if (LooksLikeMarkupExtension(rawVal) || LookupEnumType(propOrPath) != nullptr) {
            return BoxSetterValueExpr(propOrPath, FormatTypedValueLiteral(propOrPath, rawVal, dictVar));
        }
        return BoxSetterValueExpr(propOrPath, FormatGenericScalarLiteral(rawVal));
    }

    void EmitTriggerCollectionInto(
        const XamlNode& trigProp,
        std::string_view ownerObjVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::string_view styleTargetType = {}) {
        for (const auto& trig : trigProp.children) {
            if (trig->localName == "Trigger") {
                std::string prop;
                std::string val;
                std::string sourceName;
                if (const auto* p = trig->FindAttribute("Property")) prop = NormalizeSetterPropertyName(p->value);
                if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                if (const auto* sn = trig->FindAttribute("SourceName")) sourceName = sn->value;
                const std::string_view trigTargetType = sourceName.empty() ? styleTargetType : std::string_view{};
                const std::string tVar = FreshVar("trig");
                Indent(out, indent);
                out << "var " << tVar << " = Trigger.CreateBox(" << EscapeRsString(prop) << ", "
                    << BoxSetterValueExpr(prop, FormatTypedValueLiteral(prop, val, dictVar, trigTargetType), trigTargetType) << ", "
                    << EscapeRsString(sourceName) << ");\n";
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent, styleTargetType);
                    }
                }
                EmitTriggerEnterExitActions(*trig, "Trigger", tVar, dictVar, out, indent);
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            } else if (trig->localName == "DataTrigger") {
                ParsedBindingSpec bSpec;
                if (const auto* b = trig->FindAttribute("Binding")) {
                    const auto ext = ParseMarkupExtension(b->value);
                    (void)TryExtractBindingSpec(ext, bSpec);
                }
                std::string mbVar;
                std::string dynBindVar;
                if (const XamlNode* bindProp = trig->FindPropertyElement("DataTrigger", "Binding")) {
                    for (const auto& bc : bindProp->children) {
                        if (!bc) continue;
                        if (bc->localName == "MultiBinding") {
                            mbVar = EmitMultiBindingObject(*bc, "", dictVar, out, indent);
                            break;
                        }
                        if (bc->localName == "Binding") {
                            dynBindVar = EmitDynamicBindingObject(*bc, "", dictVar, out, indent);
                            break;
                        }
                    }
                }
                std::string val;
                if (const auto* v = trig->FindAttribute("Value")) val = v->value;
                const std::string cleanPath = NormalizeBindingPath(bSpec.path);
                const std::string tVar = FreshVar("dtrig");
                Indent(out, indent);
                out << "var " << tVar << " = DataTrigger.CreateBox("
                    << EscapeRsString(cleanPath) << ", "
                    << FormatTriggerValueBoxExpr(cleanPath, val, dictVar) << ", "
                    << EscapeRsString(bSpec.relativeSourceMode) << ", "
                    << EscapeRsString(bSpec.ancestorType) << ");\n";
                if (!bSpec.elementName.empty()) {
                    Indent(out, indent);
                    out << tVar << ".ElementName = " << EscapeRsString(bSpec.elementName) << ";\n";
                }
                if (!mbVar.empty()) {
                    Indent(out, indent);
                    out << tVar << ".SetMultiBinding(" << mbVar << ");\n";
                } else if (!dynBindVar.empty()) {
                    Indent(out, indent);
                    out << tVar << ".SetDynamicBinding(" << dynBindVar << ");\n";
                }
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent, styleTargetType);
                    }
                }
                EmitTriggerEnterExitActions(*trig, "DataTrigger", tVar, dictVar, out, indent);
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            } else if (trig->localName == "MultiTrigger") {
                const std::string tVar = FreshVar("mtrig");
                Indent(out, indent);
                out << "var " << tVar << " = new MultiDataTrigger();\n";
                if (const auto* condProp = trig->FindPropertyElement("MultiTrigger", "Conditions")) {
                    for (const auto& cond : condProp->children) {
                        if (cond->localName != "Condition") continue;
                        std::string prop;
                        std::string val;
                        std::string srcName;
                        if (const auto* p = cond->FindAttribute("Property")) prop = NormalizeSetterPropertyName(p->value);
                        if (const auto* v = cond->FindAttribute("Value")) val = v->value;
                        if (const auto* sn = cond->FindAttribute("SourceName")) srcName = sn->value;
                        const std::string_view condTargetType = srcName.empty() ? styleTargetType : std::string_view{};
                        Indent(out, indent);
                        out << tVar << ".AddCondition("
                            << EscapeRsString(prop) << ", "
                            << BoxSetterValueExpr(prop, FormatTypedValueLiteral(prop, val, dictVar, condTargetType), condTargetType) << ", "
                            << EscapeRsString(srcName) << ", \"Self\", \"\");\n";
                    }
                }
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent, styleTargetType);
                    }
                }
                EmitTriggerEnterExitActions(*trig, "MultiTrigger", tVar, dictVar, out, indent);
                Indent(out, indent);
                out << ownerObjVar << ".AddTrigger(" << tVar << ");\n";
            } else if (trig->localName == "MultiDataTrigger") {
                const std::string tVar = FreshVar("mdtrig");
                Indent(out, indent);
                out << "var " << tVar << " = new MultiDataTrigger();\n";
                if (const auto* condProp = trig->FindPropertyElement("MultiDataTrigger", "Conditions")) {
                    for (const auto& cond : condProp->children) {
                        if (cond->localName != "Condition") continue;
                        ParsedBindingSpec bSpec;
                        if (const auto* b = cond->FindAttribute("Binding")) {
                            const auto ext = ParseMarkupExtension(b->value);
                            (void)TryExtractBindingSpec(ext, bSpec);
                        }
                        std::string val;
                        if (const auto* v = cond->FindAttribute("Value")) val = v->value;
                        const std::string cleanPath = NormalizeBindingPath(bSpec.path);
                        Indent(out, indent);
                        out << tVar << ".AddCondition("
                            << EscapeRsString(cleanPath) << ", "
                            << FormatTriggerValueBoxExpr(cleanPath, val, dictVar) << ", "
                            << EscapeRsString(bSpec.elementName) << ", "
                            << EscapeRsString(bSpec.relativeSourceMode) << ", "
                            << EscapeRsString(bSpec.ancestorType) << ");\n";
                    }
                }
                for (const auto& s : trig->children) {
                    if (s->localName == "Setter") {
                        EmitSetterInto(*s, tVar, dictVar, out, indent, styleTargetType);
                    }
                }
                EmitTriggerEnterExitActions(*trig, "MultiDataTrigger", tVar, dictVar, out, indent);
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
                if (const auto* sn = trig->FindAttribute("SourceName")) {
                    Indent(out, indent);
                    out << tVar << ".SourceName = " << EscapeRsString(sn->value) << ";\n";
                }
                for (const auto& act : trig->children) {
                    if (act->localName == "BeginStoryboard") {
                        if (const auto* sbAttr = act->FindAttribute("Storyboard");
                            sbAttr != nullptr && LooksLikeMarkupExtension(sbAttr->value)) {
                            const std::string rk = ResolveResourceKeyFromExtension(ParseMarkupExtension(sbAttr->value));
                            if (!rk.empty()) {
                                Indent(out, indent);
                                out << tVar << ".AddActionKey(" << EscapeRsString(rk) << ");\n";
                            }
                        }
                        for (const auto& sbNode : act->children) {
                            if (sbNode->localName == "Storyboard") {
                                const std::string sb = EmitStoryboard(*sbNode, out, indent, dictVar);
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
        const auto prevStoryboards = knownStoryboards_;
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
        if (!dictVar.empty()) {
            Indent(out, indent);
            out << tplVar << ".AmbientResources = " << dictVar << ";\n";
        }
        EmitResourcesPropertyElement(tplNode, "ControlTemplate", tplVar, opts_.xamlPath, out, indent);
        Indent(out, indent);
        out << tplVar << ".Factory = (Control " << tpParam << ") =>\n";
        Indent(out, indent);
        out << "{\n";

        const std::string prevTp = currentTplParentVar_;
        currentTplParentVar_ = tpParam;
        const bool prevPartial = inPartialClass_;
        inPartialClass_ = false;
        const std::string_view safeDictVar{};

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
        knownStoryboards_ = prevStoryboards;
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
        const std::string_view safeDictVar{};
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
        if (!dictVar.empty()) {
            Indent(out, indent);
            out << iptVar << ".AmbientResources = " << dictVar << ";\n";
        }
        return iptVar;
    }

    [[nodiscard]] std::string EmitDataTemplate(
        const XamlNode& dtNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const auto prevStoryboards = knownStoryboards_;
        std::string itemDataType;
        if (const auto* dt = dtNode.FindAttribute("DataType")) {
            itemDataType = StripTargetTypeName(dt->value);
        } else if (const auto* xdt = dtNode.FindXAttribute("DataType")) {
            itemDataType = StripTargetTypeName(xdt->value);
        }

        const bool isHierarchical = (dtNode.localName == "HierarchicalDataTemplate");
        std::string itemsSourcePath;
        std::string itemTemplateKey;
        if (isHierarchical) {
            if (const auto* isAttr = dtNode.FindAttribute("ItemsSource")) {
                if (LooksLikeMarkupExtension(isAttr->value)) {
                    const auto ext = ParseMarkupExtension(isAttr->value);
                    ParsedBindingSpec bSpec;
                    if (TryExtractBindingSpec(ext, bSpec)) {
                        itemsSourcePath = NormalizeBindingPath(bSpec.path);
                    }
                } else {
                    itemsSourcePath = std::string(aero::str::Trim(isAttr->value));
                }
            }
            if (const auto* itAttr = dtNode.FindAttribute("ItemTemplate")) {
                if (LooksLikeMarkupExtension(itAttr->value)) {
                    const auto ext = ParseMarkupExtension(itAttr->value);
                    if (ext.kind == MarkupExtensionKind::StaticResource ||
                        ext.kind == MarkupExtensionKind::DynamicResource) {
                        itemTemplateKey = ResolveResourceKeyFromExtension(ext);
                    }
                }
            }
        }

        const std::string dtVar = FreshVar("dtpl");
        const std::string dcParam = FreshVar("__dc");

        auto emitMetaAndResources = [&]() {
            if (!itemDataType.empty()) {
                Indent(out, indent);
                out << dtVar << ".DataType = " << EscapeRsString(itemDataType) << ";\n";
            }
            if (!dictVar.empty()) {
                Indent(out, indent);
                out << dtVar << ".AmbientResources = " << dictVar << ";\n";
            }
            EmitResourcesPropertyElement(dtNode, dtNode.localName, dtVar, opts_.xamlPath, out, indent);
        };

        auto emitFactoryLambdaBody = [&]() {
            Indent(out, indent);
            out << "{\n";

            const bool prevPartial = inPartialClass_;
            inPartialClass_ = false;
            const std::string_view safeDictVar{};

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
        };

        if (isHierarchical) {
            Indent(out, indent);
            out << "var " << dtVar << " = new HierarchicalDataTemplate((object " << dcParam << ") =>\n";
            emitFactoryLambdaBody();
            Indent(out, indent);
            out << "}, " << EscapeRsString(itemsSourcePath) << ");\n";
            emitMetaAndResources();
            if (!itemTemplateKey.empty()) {
                Indent(out, indent);
                out << dtVar << ".ItemTemplateKey = " << EscapeRsString(itemTemplateKey) << ";\n";
                Indent(out, indent);
                out << dtVar << ".ItemTemplate = " << LookupResourceExpr(itemTemplateKey, dictVar)
                    << " as DataTemplate;\n";
            } else if (const XamlNode* itemTplEl = dtNode.FindPropertyElement("HierarchicalDataTemplate", "ItemTemplate")) {
                for (const auto& ch : itemTplEl->children) {
                    if (ch && (ch->localName == "DataTemplate" || ch->localName == "HierarchicalDataTemplate")) {
                        const std::string subDt = EmitDataTemplate(*ch, dictVar, out, indent);
                        Indent(out, indent);
                        out << dtVar << ".ItemTemplate = " << subDt << ";\n";
                        break;
                    }
                }
            }
        } else {
            Indent(out, indent);
            out << "var " << dtVar << " = new DataTemplate();\n";
            emitMetaAndResources();
            Indent(out, indent);
            out << dtVar << ".Factory = (object " << dcParam << ") =>\n";
            emitFactoryLambdaBody();
            Indent(out, indent);
            out << "};\n";
        }

        const XamlNode* trigProp = dtNode.FindPropertyElement(dtNode.localName, "Triggers");
        if (trigProp == nullptr && isHierarchical) {
            trigProp = dtNode.FindPropertyElement("DataTemplate", "Triggers");
        }
        if (trigProp != nullptr) {
            EmitTriggerCollectionInto(*trigProp, dtVar, dictVar, out, indent);
        }
        knownStoryboards_ = prevStoryboards;
        return dtVar;
    }

    [[nodiscard]] static bool IsComplexGeometryPointPath(std::string_view rawProp) {
        return rawProp.find("PathGeometry") != std::string_view::npos ||
               rawProp.find("PathFigure") != std::string_view::npos ||
               rawProp.find("LineSegment") != std::string_view::npos ||
               rawProp.find("BezierSegment") != std::string_view::npos ||
               rawProp.find("QuadraticBezierSegment") != std::string_view::npos ||
               rawProp.find("ArcSegment") != std::string_view::npos ||
               rawProp.find("PolyLineSegment") != std::string_view::npos ||
               rawProp.find("PathSegment") != std::string_view::npos ||
               rawProp.find(".Figures") != std::string_view::npos ||
               rawProp.find(".Segments") != std::string_view::npos ||
               rawProp.find("Path.Data") != std::string_view::npos;
    }

    [[nodiscard]] std::string FormatDiscreteKeyFrameBoxExpr(
        const XamlNode& kf,
        std::string_view targetProp,
        std::string_view dictVar,
        bool isBooleanTimeline,
        std::ostringstream& out,
        int indent) {
        std::string rawVal;
        bool hasVal = false;
        std::string typedChildKind;

        if (const auto* va = kf.FindAttribute("Value")) {
            rawVal = va->value;
            hasVal = true;
        } else {
            const XamlNode* valEl = kf.FindPropertyElement(kf.localName, "Value");
            if (valEl == nullptr) {
                valEl = kf.FindPropertyElement("DiscreteObjectKeyFrame", "Value");
            }
            if (valEl != nullptr) {
                for (const auto& ch : valEl->children) {
                    if (!ch || ShouldIgnoreXmlElement(*ch)) continue;
                    if (ch->localName == "SolidColorBrush" ||
                        ch->localName == "LinearGradientBrush" ||
                        ch->localName == "RadialGradientBrush" ||
                        ch->localName == "ImageBrush") {
                        const std::string bVar = EmitBrushObject(*ch, targetProp, dictVar, out, indent);
                        return "DependencyObject.BoxObject(" + bVar + ")";
                    }
                    typedChildKind = ch->localName;
                    rawVal = ch->textContent;
                    hasVal = true;
                    break;
                }
                if (!hasVal && !valEl->textContent.empty()) {
                    rawVal = valEl->textContent;
                    hasVal = true;
                }
            }
        }

        if (!hasVal) {
            return "null";
        }

        if (LooksLikeMarkupExtension(rawVal)) {
            const auto ext = ParseMarkupExtension(rawVal);
            if (!ext.IsOk()) {
                AddError("Invalid markup extension '" + rawVal + "': " + ext.errorMessage);
                return "null";
            }
            if (ext.kind == MarkupExtensionKind::XNull) {
                return "null";
            }
            if (ext.kind == MarkupExtensionKind::StaticResource ||
                ext.kind == MarkupExtensionKind::DynamicResource) {
                const std::string key = ResolveResourceKeyFromExtension(ext);
                if (!key.empty()) {
                    return "DependencyObject.BoxObject(" + LookupResourceExpr(key, dictVar) + ")";
                }
                return "null";
            }
            if (ext.kind == MarkupExtensionKind::XStatic) {
                if (const auto* p0 = ext.FindPositional(0)) {
                    const std::string_view lit = aero::str::Trim(p0->literal);
                    if (const char* enumType = LookupEnumType(targetProp)) {
                        const std::size_t dot = lit.rfind('.');
                        const std::string_view member = (dot != std::string_view::npos) ? lit.substr(dot + 1) : lit;
                        return "DependencyObject.BoxInt(EnumConvert.ToInt(" + std::string(enumType) + "." +
                               std::string(aero::str::Trim(member)) + "))";
                    }
                    const std::size_t dot = lit.rfind('.');
                    if (dot != std::string_view::npos) {
                        const std::string_view enumName = lit.substr(0, dot);
                        const std::string_view member = aero::str::Trim(lit.substr(dot + 1));
                        if (enumName == "Visibility" || enumName == "HorizontalAlignment" ||
                            enumName == "VerticalAlignment" || enumName == "Orientation" ||
                            enumName == "Stretch" || enumName == "Dock") {
                            return "DependencyObject.BoxInt(EnumConvert.ToInt(" +
                                   std::string(enumName) + "." + std::string(member) + "))";
                        }
                    }
                    return "DependencyObject.BoxString(" + EscapeRsString(lit) + ")";
                }
            }
            if (ext.kind == MarkupExtensionKind::XType) {
                return "DependencyObject.BoxString(" + EscapeRsString(StripTargetTypeName(rawVal)) + ")";
            }
            if (ext.kind == MarkupExtensionKind::EscapedLiteral) {
                return "DependencyObject.BoxString(" + EscapeRsString(ext.literalValue) + ")";
            }
        }

        const std::string_view trimmed = aero::str::Trim(rawVal);
        if (isBooleanTimeline || typedChildKind == "Boolean") {
            const bool bv = aero::str::EqualsIgnoreCase(trimmed, "True");
            return std::string("DependencyObject.BoxBool(") + (bv ? "true" : "false") + ")";
        }
        if (typedChildKind == "Int32" || typedChildKind == "Int64") {
            const long iv = std::strtol(std::string(trimmed).c_str(), nullptr, 10);
            return "DependencyObject.BoxInt(" + std::to_string(iv) + ")";
        }
        if (typedChildKind == "Double" || typedChildKind == "Single") {
            float fv = 0.0f;
            (void)aero::str::TryParseFloat32(trimmed, fv);
            return "DependencyObject.BoxFloat(" + FormatFloatLiteral(fv) + ")";
        }
        if (LookupEnumType(targetProp) != nullptr) {
            return BoxSetterValueExpr(
                targetProp, FormatTypedValueLiteral(targetProp, trimmed, dictVar));
        }
        if (targetProp != "Text" && targetProp != "Title" && targetProp != "Placeholder" &&
            typedChildKind != "String") {
            if (aero::str::EqualsIgnoreCase(trimmed, "True")) {
                return "DependencyObject.BoxBool(true)";
            }
            if (aero::str::EqualsIgnoreCase(trimmed, "False")) {
                return "DependencyObject.BoxBool(false)";
            }
            if (!trimmed.empty() && trimmed.find('.') == std::string_view::npos) {
                bool allDigits = true;
                for (std::size_t i = (trimmed.front() == '-' && trimmed.size() > 1) ? 1 : 0;
                     i < trimmed.size(); ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    if (IsFloatPropertyFor({}, targetProp, trimmed)) {
                        float fv = 0.0f;
                        (void)aero::str::TryParseFloat32(trimmed, fv);
                        return "DependencyObject.BoxFloat(" + FormatFloatLiteral(fv) + ")";
                    }
                    const long iv = std::strtol(std::string(trimmed).c_str(), nullptr, 10);
                    return "DependencyObject.BoxInt(" + std::to_string(iv) + ")";
                }
            }
            float fv = 0.0f;
            if (aero::str::TryParseFloat32(trimmed, fv)) {
                return "DependencyObject.BoxFloat(" + FormatFloatLiteral(fv) + ")";
            }
        }
        return BoxSetterValueExpr(targetProp, FormatTypedValueLiteral(targetProp, rawVal, dictVar));
    }

    static void ExtractEasingNodeParams(const XamlNode& easeNode, float& outP0, float& outP1, int& outMask) {
        outP0 = 0.0f;
        outP1 = 0.0f;
        outMask = 0;
        if (easeNode.localName == "BounceEase") {
            if (const auto* a = easeNode.FindAttribute("Bounces")) {
                if (aero::str::TryParseFloat32(a->value, outP0)) outMask |= 1;
            }
            if (const auto* b = easeNode.FindAttribute("Bounciness")) {
                if (aero::str::TryParseFloat32(b->value, outP1)) outMask |= 2;
            }
        } else if (easeNode.localName == "ElasticEase") {
            if (const auto* a = easeNode.FindAttribute("Oscillations")) {
                if (aero::str::TryParseFloat32(a->value, outP0)) outMask |= 1;
            }
            if (const auto* b = easeNode.FindAttribute("Springiness")) {
                if (aero::str::TryParseFloat32(b->value, outP1)) outMask |= 2;
            }
        } else if (easeNode.localName == "BackEase") {
            if (const auto* a = easeNode.FindAttribute("Amplitude")) {
                if (aero::str::TryParseFloat32(a->value, outP0)) outMask |= 1;
            }
        } else if (easeNode.localName == "ExponentialEase") {
            if (const auto* a = easeNode.FindAttribute("Exponent")) {
                if (aero::str::TryParseFloat32(a->value, outP0)) outMask |= 1;
            }
        }
    }

    void EmitScalarKeyFrameSegment(
        std::string_view sbVar,
        std::string_view targetName,
        std::string_view targetProp,
        float prevTime,
        float prevVal,
        float kt,
        float val,
        bool havePrev,
        std::size_t keyFrameCount,
        int kfEase,
        int kfMode,
        float beginSec,
        bool hasLoopParams,
        bool autoRev,
        int repeatCount,
        std::ostringstream& out,
        int indent,
        float easeP0 = 0.0f,
        float easeP1 = 0.0f,
        int easeMask = 0) {
        const float segDur = std::max(0.001f, kt - prevTime);
        const bool segLoop = hasLoopParams && keyFrameCount <= 2;
        if (havePrev && kt > prevTime) {
            Indent(out, indent);
            if (segLoop) {
                out << sbVar << ".AddDoubleAnimationEx("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatFloatLiteral(prevVal) << ", "
                    << FormatFloatLiteral(val) << ", "
                    << FormatFloatLiteral(segDur) << ", "
                    << kfEase << ", " << kfMode << ", "
                    << FormatFloatLiteral(beginSec + prevTime) << ", "
                    << (autoRev ? "true" : "false") << ", "
                    << repeatCount << ");\n";
            } else {
                out << sbVar << ".AddDoubleAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatFloatLiteral(prevVal) << ", "
                    << FormatFloatLiteral(val) << ", "
                    << FormatFloatLiteral(segDur) << ", "
                    << kfEase << ", " << kfMode << ", "
                    << FormatFloatLiteral(beginSec + prevTime) << ");\n";
            }
        } else if (!havePrev && kt > 0.0001f) {
            Indent(out, indent);
            if (hasLoopParams) {
                out << sbVar << ".AddDoubleToAnimationEx("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatFloatLiteral(val) << ", "
                    << FormatFloatLiteral(kt) << ", "
                    << kfEase << ", " << kfMode << ", "
                    << FormatFloatLiteral(beginSec) << ", "
                    << (autoRev ? "true" : "false") << ", "
                    << repeatCount << ");\n";
            } else {
                out << sbVar << ".AddDoubleToAnimation("
                    << EscapeRsString(targetName) << ", "
                    << EscapeRsString(targetProp) << ", "
                    << FormatFloatLiteral(val) << ", "
                    << FormatFloatLiteral(kt) << ", "
                    << kfEase << ", " << kfMode << ", "
                    << FormatFloatLiteral(beginSec) << ");\n";
            }
        } else if (!havePrev && keyFrameCount == 1) {
            Indent(out, indent);
            out << sbVar << ".AddDoubleAnimation("
                << EscapeRsString(targetName) << ", "
                << EscapeRsString(targetProp) << ", "
                << FormatFloatLiteral(val) << ", "
                << FormatFloatLiteral(val) << ", "
                << FormatFloatLiteral(kt > 0.0001f ? kt : 0.0f) << ", "
                << kfEase << ", " << kfMode << ", "
                << FormatFloatLiteral(beginSec + prevTime) << ");\n";
        } else {
            return;
        }
        if (easeMask != 0 || easeP0 != 0.0f || easeP1 != 0.0f) {
            Indent(out, indent);
            out << sbVar << ".SetLastEasingParams("
                << FormatFloatLiteral(easeP0) << ", " << FormatFloatLiteral(easeP1)
                << ", " << (easeMask != 0 ? easeMask : 3) << ");\n";
        }
    }

    [[nodiscard]] static std::string ExtractStaticResourceElementKey(const XamlNode& node) {
        if (const auto* rk = node.FindAttribute("ResourceKey")) {
            return std::string(aero::str::Trim(rk->value));
        }
        if (const auto* k = node.FindAttribute("Key")) {
            return std::string(aero::str::Trim(k->value));
        }
        if (const auto* xk = node.FindXAttribute("Key")) {
            return std::string(aero::str::Trim(xk->value));
        }
        return {};
    }

    void EmitStoryboardTimelines(
        const XamlNode& sbNode,
        std::string_view sbVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::unordered_set<std::string>& visitedSbKeys,
        bool sbRepeatForever = false) {
        for (const auto& child : sbNode.children) {
            if (!child || ShouldIgnoreXmlElement(*child)) {
                continue;
            }
            if (child->localName == "StaticResource") {
                const std::string refKey = ExtractStaticResourceElementKey(*child);
                if (!refKey.empty()) {
                    if (const auto sbIt = knownStoryboards_.find(refKey);
                        sbIt != knownStoryboards_.end() && sbIt->second != nullptr) {
                        if (visitedSbKeys.insert(refKey).second) {
                            EmitStoryboardTimelines(*sbIt->second, sbVar, dictVar, out, indent, visitedSbKeys, sbRepeatForever);
                            visitedSbKeys.erase(refKey);
                        }
                    } else {
                        const std::string refVar = FreshVar("sbRef");
                        const std::string childSbVar = FreshVar("sbInc");
                        Indent(out, indent);
                        if (!dictVar.empty() && dictVar != "this.Resources") {
                            out << "var " << refVar << " = " << dictVar << ".Get(" << EscapeRsString(refKey) << ");\n";
                            Indent(out, indent);
                            out << "if (" << refVar << " == null) { " << refVar << " = "
                                << LookupResourceExpr(refKey, dictVar) << "; }\n";
                        } else {
                            out << "var " << refVar << " = " << LookupResourceExpr(refKey, dictVar) << ";\n";
                        }
                        Indent(out, indent);
                        out << "if (" << refVar << " is Storyboard " << childSbVar << ") { "
                            << sbVar << ".MergeFrom(" << childSbVar << "); }\n";
                    }
                }
                continue;
            }
            std::string targetName;
            std::string rawTargetProp;
            std::string targetProp;
            if (const auto* tn = child->FindAttribute("Storyboard.TargetName")) {
                targetName = tn->value;
            } else if (const auto* tn2 = child->FindAttribute("TargetName")) {
                targetName = tn2->value;
            }
            if (const auto* tp = child->FindAttribute("Storyboard.TargetProperty")) {
                rawTargetProp = tp->value;
                targetProp = NormalizeTargetPropertyPath(tp->value);
            } else if (const auto* tp2 = child->FindAttribute("TargetProperty")) {
                rawTargetProp = tp2->value;
                targetProp = NormalizeTargetPropertyPath(tp2->value);
            }

            float beginSec = 0.0f;
            if (const auto* bt = child->FindAttribute("BeginTime")) {
                beginSec = ParseDurationSeconds(bt->value);
            }
            const bool autoRev = ParseTimelineAutoReverse(*child);
            const int repeatCount = ParseTimelineRepeatCount(*child);
            const bool hasLoopParams = autoRev || (repeatCount != 1);

            int easeKind = 0;
            int easeMode = 0;
            float easeP0 = 0.0f;
            float easeP1 = 0.0f;
            int easeMask = 0;
            const XamlNode* easeProp = child->FindPropertyElement(child->localName, "EasingFunction");
            if (easeProp != nullptr && !easeProp->children.empty()) {
                const auto& easeNode = *easeProp->children.front();
                easeKind = MapEasingKind(easeNode.localName);
                if (const auto* em = easeNode.FindAttribute("EasingMode")) {
                    easeMode = MapEasingMode(em->value);
                }
                ExtractEasingNodeParams(easeNode, easeP0, easeP1, easeMask);
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
                bool hasFrom = false;
                if (const auto* f = child->FindAttribute("From")) {
                    hasFrom = aero::str::TryParseFloat32(f->value, fromVal);
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
                if (hasFrom) {
                    if (hasLoopParams) {
                        out << sbVar << ".AddDoubleAnimationEx("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << FormatFloatLiteral(fromVal) << ", "
                            << FormatFloatLiteral(toVal) << ", "
                            << FormatFloatLiteral(durSec) << ", "
                            << easeKind << ", " << easeMode << ", "
                            << FormatFloatLiteral(beginSec) << ", "
                            << (autoRev ? "true" : "false") << ", "
                            << repeatCount << ");\n";
                    } else {
                        out << sbVar << ".AddDoubleAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << FormatFloatLiteral(fromVal) << ", "
                            << FormatFloatLiteral(toVal) << ", "
                            << FormatFloatLiteral(durSec) << ", "
                            << easeKind << ", " << easeMode << ", "
                            << FormatFloatLiteral(beginSec) << ");\n";
                    }
                } else if (hasLoopParams) {
                    out << sbVar << ".AddDoubleToAnimationEx("
                        << EscapeRsString(targetName) << ", "
                        << EscapeRsString(targetProp) << ", "
                        << FormatFloatLiteral(toVal) << ", "
                        << FormatFloatLiteral(durSec) << ", "
                        << easeKind << ", " << easeMode << ", "
                        << FormatFloatLiteral(beginSec) << ", "
                        << (autoRev ? "true" : "false") << ", "
                        << repeatCount << ");\n";
                } else {
                    out << sbVar << ".AddDoubleToAnimation("
                        << EscapeRsString(targetName) << ", "
                        << EscapeRsString(targetProp) << ", "
                        << FormatFloatLiteral(toVal) << ", "
                        << FormatFloatLiteral(durSec) << ", "
                        << easeKind << ", " << easeMode << ", "
                        << FormatFloatLiteral(beginSec) << ");\n";
                }
                if (easeMask != 0 || easeP0 != 0.0f || easeP1 != 0.0f) {
                    Indent(out, indent);
                    out << sbVar << ".SetLastEasingParams("
                        << FormatFloatLiteral(easeP0) << ", " << FormatFloatLiteral(easeP1)
                        << ", " << (easeMask != 0 ? easeMask : 3) << ");\n";
                }
            } else if (child->localName == "ColorAnimation") {
                std::string fromHex = "#FFFFFFFF";
                std::string toHex = "#FFFFFFFF";
                bool hasFrom = false;
                if (const auto* f = child->FindAttribute("From")) {
                    fromHex = ResolveColorHexOrString(f->value);
                    hasFrom = true;
                }
                if (const auto* t = child->FindAttribute("To")) {
                    toHex = ResolveColorHexOrString(t->value);
                }
                float durSec = 0.3f;
                if (const auto* d = child->FindAttribute("Duration")) {
                    durSec = ParseDurationSeconds(d->value);
                }
                Indent(out, indent);
                if (hasLoopParams) {
                    out << sbVar << ".AddColorAnimationEx("
                        << EscapeRsString(targetName) << ", "
                        << EscapeRsString(targetProp) << ", "
                        << EscapeRsString(fromHex) << ", "
                        << EscapeRsString(toHex) << ", "
                        << FormatFloatLiteral(durSec) << ", "
                        << easeKind << ", " << easeMode << ", "
                        << FormatFloatLiteral(beginSec) << ", "
                        << (hasFrom ? "true" : "false") << ", "
                        << (autoRev ? "true" : "false") << ", "
                        << repeatCount << ");\n";
                } else {
                    out << sbVar << ".AddColorAnimation("
                        << EscapeRsString(targetName) << ", "
                        << EscapeRsString(targetProp) << ", "
                        << EscapeRsString(fromHex) << ", "
                        << EscapeRsString(toHex) << ", "
                        << FormatFloatLiteral(durSec) << ", "
                        << easeKind << ", " << easeMode << ", "
                        << FormatFloatLiteral(beginSec) << ");\n";
                }
            } else if (child->localName == "PointAnimation") {
                if (IsComplexGeometryPointPath(rawTargetProp) && !aero::str::StartsWith(targetProp, "Data.Fig")) {
                    AddWarning(
                        "PointAnimation target property '" + rawTargetProp +
                        "' on complex Geometry/Path sub-property is not supported on visual animation tracks.");
                    continue;
                }
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
            } else if (child->localName == "PointAnimationUsingKeyFrames") {
                if (IsComplexGeometryPointPath(rawTargetProp) && !aero::str::StartsWith(targetProp, "Data.Fig")) {
                    AddWarning(
                        "PointAnimationUsingKeyFrames target property '" + rawTargetProp +
                        "' on complex Geometry/Path sub-property is not supported on visual animation tracks.");
                    continue;
                }
                float prevTime = 0.0f;
                float prevX = 0.0f;
                float prevY = 0.0f;
                bool havePrev = false;
                for (const auto& kf : child->children) {
                    if (!kf || ShouldIgnoreXmlElement(*kf)) continue;
                    float kt = prevTime;
                    if (const auto* ka = kf->FindAttribute("KeyTime")) {
                        kt = ParseDurationSeconds(ka->value);
                    }
                    float px = prevX;
                    float py = prevY;
                    if (const auto* va = kf->FindAttribute("Value")) {
                        std::vector<float> pts;
                        if (TryParseFloatList(va->value, pts) && pts.size() >= 2) {
                            px = pts[0];
                            py = pts[1];
                        }
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
                    if (kf->localName == "DiscretePointKeyFrame") {
                        Indent(out, indent);
                        out << sbVar << ".AddDoubleAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp + ".X") << ", "
                            << FormatFloatLiteral(px) << ", " << FormatFloatLiteral(px) << ", "
                            << "0.0f, 0, 0, " << FormatFloatLiteral(beginSec + kt) << ");\n";
                        Indent(out, indent);
                        out << sbVar << ".AddDoubleAnimation("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp + ".Y") << ", "
                            << FormatFloatLiteral(py) << ", " << FormatFloatLiteral(py) << ", "
                            << "0.0f, 0, 0, " << FormatFloatLiteral(beginSec + kt) << ");\n";
                    } else {
                        EmitScalarKeyFrameSegment(
                            sbVar, targetName, targetProp + ".X", prevTime, prevX, kt, px,
                            havePrev, child->children.size(), kfEase, kfMode, beginSec,
                            hasLoopParams, autoRev, repeatCount, out, indent);
                        EmitScalarKeyFrameSegment(
                            sbVar, targetName, targetProp + ".Y", prevTime, prevY, kt, py,
                            havePrev, child->children.size(), kfEase, kfMode, beginSec,
                            hasLoopParams, autoRev, repeatCount, out, indent);
                    }
                    prevTime = kt;
                    prevX = px;
                    prevY = py;
                    havePrev = true;
                }
            } else if (child->localName == "ObjectAnimationUsingKeyFrames" ||
                       child->localName == "BooleanAnimationUsingKeyFrames") {
                const bool isBoolTl = (child->localName == "BooleanAnimationUsingKeyFrames");
                float prevTime = 0.0f;
                for (const auto& kf : child->children) {
                    if (!kf || ShouldIgnoreXmlElement(*kf)) continue;
                    float kt = prevTime;
                    if (const auto* ka = kf->FindAttribute("KeyTime")) {
                        kt = ParseDurationSeconds(ka->value);
                    }
                    const std::string boxedExpr = FormatDiscreteKeyFrameBoxExpr(
                        *kf, targetProp, dictVar, isBoolTl, out, indent);
                    Indent(out, indent);
                    out << sbVar << ".AddDiscreteKeyFrame("
                        << EscapeRsString(targetName) << ", "
                        << EscapeRsString(targetProp) << ", "
                        << FormatFloatLiteral(beginSec + kt) << ", "
                        << boxedExpr << ");\n";
                    prevTime = kt;
                }
            } else if (child->localName == "DoubleAnimationUsingKeyFrames") {
                bool anyDynamicKf = false;
                std::size_t kfCount = 0u;
                for (const auto& kf : child->children) {
                    if (!kf || ShouldIgnoreXmlElement(*kf)) continue;
                    ++kfCount;
                    if (const auto* va = kf->FindAttribute("Value");
                        va != nullptr && LooksLikeMarkupExtension(va->value)) {
                        anyDynamicKf = true;
                    }
                }
                if (anyDynamicKf || kfCount > 2u || hasLoopParams || sbRepeatForever) {
                    const std::string dakfVar = FreshVar("dakf");
                    Indent(out, indent);
                    out << "var " << dakfVar << " = new DoubleAnimationUsingKeyFrames();\n";
                    if (!targetName.empty()) {
                        Indent(out, indent);
                        out << dakfVar << ".TargetName = " << EscapeRsString(targetName) << ";\n";
                    }
                    Indent(out, indent);
                    out << dakfVar << ".TargetProperty = " << EscapeRsString(targetProp) << ";\n";
                    if (beginSec > 0.0001f) {
                        Indent(out, indent);
                        out << dakfVar << ".DelaySec = " << FormatFloatLiteral(beginSec) << ";\n";
                    }
                    if (autoRev) {
                        Indent(out, indent);
                        out << dakfVar << ".AutoReverse = true;\n";
                    }
                    if (repeatCount != 1) {
                        Indent(out, indent);
                        out << dakfVar << ".RepeatCount = " << repeatCount << ";\n";
                    }
                    float prevTime = 0.0f;
                    float prevVal = 0.0f;
                    for (const auto& kf : child->children) {
                        if (!kf || ShouldIgnoreXmlElement(*kf)) continue;
                        float kt = prevTime;
                        if (const auto* ka = kf->FindAttribute("KeyTime")) {
                            kt = ParseDurationSeconds(ka->value);
                        }
                        float val = prevVal;
                        ParsedBindingSpec kfBindSpec;
                        bool hasKfBind = false;
                        if (const auto* va = kf->FindAttribute("Value")) {
                            if (LooksLikeMarkupExtension(va->value)) {
                                const auto ext = ParseMarkupExtension(va->value);
                                hasKfBind = TryExtractBindingSpec(ext, kfBindSpec);
                            } else {
                                (void)aero::str::TryParseFloat32(va->value, val);
                            }
                        }
                        int kfEase = (kf->localName == "SplineDoubleKeyFrame") ? 11 : 0;
                        int kfMode = 0;
                        float kfP0 = 0.0f;
                        float kfP1 = 0.0f;
                        int kfMask = 0;
                        const XamlNode* kfEaseProp = kf->FindPropertyElement(kf->localName, "EasingFunction");
                        if (kfEaseProp != nullptr && !kfEaseProp->children.empty()) {
                            const auto& en = *kfEaseProp->children.front();
                            kfEase = MapEasingKind(en.localName);
                            if (const auto* em = en.FindAttribute("EasingMode")) {
                                kfMode = MapEasingMode(em->value);
                            }
                            ExtractEasingNodeParams(en, kfP0, kfP1, kfMask);
                        }
                        const std::string kfVar = FreshVar("dkf");
                        Indent(out, indent);
                        out << "var " << kfVar << " = new EasingDoubleKeyFrame("
                            << FormatFloatLiteral(val) << ", "
                            << FormatFloatLiteral(kt) << ", Storyboard.IntToEasingKind("
                            << kfEase << "), Storyboard.IntToEasingMode(" << kfMode << "));\n";
                        if (kfMask != 0 || kfP0 != 0.0f || kfP1 != 0.0f) {
                            Indent(out, indent);
                            out << kfVar << ".EaseParam0 = " << FormatFloatLiteral(kfP0) << ";\n";
                            Indent(out, indent);
                            out << kfVar << ".EaseParam1 = " << FormatFloatLiteral(kfP1) << ";\n";
                            Indent(out, indent);
                            out << kfVar << ".EaseParamMask = " << (kfMask != 0 ? kfMask : 3) << ";\n";
                        }
                        if (hasKfBind) {
                            const std::string bv = EmitBindingObjectFromSpec(
                                kfBindSpec, targetProp, dictVar, out, indent);
                            Indent(out, indent);
                            out << kfVar << ".ValueBinding = " << bv << ";\n";
                        }
                        Indent(out, indent);
                        out << dakfVar << ".AddKeyFrame(" << kfVar << ");\n";
                        prevTime = kt;
                        prevVal = val;
                    }
                    Indent(out, indent);
                    out << sbVar << ".Add(" << dakfVar << ");\n";
                } else {
                    float prevTime = 0.0f;
                    float prevVal = 0.0f;
                    bool havePrev = false;
                    for (const auto& kf : child->children) {
                        if (!kf || ShouldIgnoreXmlElement(*kf)) continue;
                        float kt = prevTime;
                        if (const auto* ka = kf->FindAttribute("KeyTime")) {
                            kt = ParseDurationSeconds(ka->value);
                        }
                        float val = prevVal;
                        if (const auto* va = kf->FindAttribute("Value")) {
                            (void)aero::str::TryParseFloat32(va->value, val);
                        }
                        int kfEase = (kf->localName == "SplineDoubleKeyFrame") ? 11 : 0;
                        int kfMode = 0;
                        float kfP0 = 0.0f;
                        float kfP1 = 0.0f;
                        int kfMask = 0;
                        const XamlNode* kfEaseProp = kf->FindPropertyElement(kf->localName, "EasingFunction");
                        if (kfEaseProp != nullptr && !kfEaseProp->children.empty()) {
                            const auto& en = *kfEaseProp->children.front();
                            kfEase = MapEasingKind(en.localName);
                            if (const auto* em = en.FindAttribute("EasingMode")) {
                                kfMode = MapEasingMode(em->value);
                            }
                            ExtractEasingNodeParams(en, kfP0, kfP1, kfMask);
                        }
                        EmitScalarKeyFrameSegment(
                            sbVar, targetName, targetProp, prevTime, prevVal, kt, val,
                            havePrev, kfCount, kfEase, kfMode, beginSec,
                            hasLoopParams, autoRev, repeatCount, out, indent, kfP0, kfP1, kfMask);
                        prevTime = kt;
                        prevVal = val;
                        havePrev = true;
                    }
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
                    const bool segLoop = hasLoopParams && child->children.size() <= 2;
                    if ((havePrev && kt > prevTime) || (!havePrev && child->children.size() == 1)) {
                        const std::string& startHex = havePrev ? prevHex : hex;
                        const float emitDur = havePrev ? segDur : (kt > 0.0001f ? kt : 0.0f);
                        Indent(out, indent);
                        if (segLoop) {
                            out << sbVar << ".AddColorAnimationEx("
                                << EscapeRsString(targetName) << ", "
                                << EscapeRsString(targetProp) << ", "
                                << EscapeRsString(startHex) << ", "
                                << EscapeRsString(hex) << ", "
                                << FormatFloatLiteral(emitDur) << ", "
                                << kfEase << ", " << kfMode << ", "
                                << FormatFloatLiteral(beginSec + prevTime) << ", true, "
                                << (autoRev ? "true" : "false") << ", "
                                << repeatCount << ");\n";
                        } else {
                            out << sbVar << ".AddColorAnimation("
                                << EscapeRsString(targetName) << ", "
                                << EscapeRsString(targetProp) << ", "
                                << EscapeRsString(startHex) << ", "
                                << EscapeRsString(hex) << ", "
                                << FormatFloatLiteral(emitDur) << ", "
                                << kfEase << ", " << kfMode << ", "
                                << FormatFloatLiteral(beginSec + prevTime) << ");\n";
                        }
                    } else if (!havePrev && kt > 0.0001f) {
                        Indent(out, indent);
                        out << sbVar << ".AddColorAnimationEx("
                            << EscapeRsString(targetName) << ", "
                            << EscapeRsString(targetProp) << ", "
                            << EscapeRsString(hex) << ", "
                            << EscapeRsString(hex) << ", "
                            << FormatFloatLiteral(kt) << ", "
                            << kfEase << ", " << kfMode << ", "
                            << FormatFloatLiteral(beginSec) << ", false, "
                            << (autoRev ? "true" : "false") << ", "
                            << repeatCount << ");\n";
                    }
                    prevTime = kt;
                    prevHex = hex;
                    havePrev = true;
                }
            }
        }
    }

    [[nodiscard]] std::string EmitStoryboard(
        const XamlNode& sbNode,
        std::ostringstream& out,
        int indent,
        std::string_view dictVar = {}) {
        const std::string sbVar = FreshVar("sb");
        Indent(out, indent);
        out << "var " << sbVar << " = new Storyboard();\n";
        if (const auto* srAttr = sbNode.FindAttribute("SpeedRatio")) {
            float sr = 1.0f;
            if (aero::str::TryParseFloat32(srAttr->value, sr) && sr > 0.0f) {
                Indent(out, indent);
                out << sbVar << ".SpeedRatio = " << FormatFloatLiteral(sr) << ";\n";
            }
        }
        bool sbRepeatForever = false;
        if (const auto* rbAttr = sbNode.FindAttribute("RepeatBehavior")) {
            if (aero::str::EqualsIgnoreCase(aero::str::Trim(rbAttr->value), "Forever")) {
                sbRepeatForever = true;
                Indent(out, indent);
                out << sbVar << ".RepeatForever = true;\n";
            }
        }

        std::unordered_set<std::string> visitedSbKeys;
        const std::string ownKey = sbNode.GetKey();
        if (!ownKey.empty()) {
            visitedSbKeys.insert(ownKey);
        }
        EmitStoryboardTimelines(sbNode, sbVar, dictVar, out, indent, visitedSbKeys, sbRepeatForever);
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
        return "ResourceDictionary.FindAmbientOrGlobal(" + EscapeRsString(key) + ")";
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
        std::string_view dictVar,
        std::string_view targetType = {}) const {
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
                    if (const char* enumType = LookupEnumType(propName)) {
                        const std::size_t dot = p0->literal.rfind('.');
                        const std::string member = (dot != std::string::npos)
                            ? p0->literal.substr(dot + 1)
                            : p0->literal;
                        return std::string(enumType) + "." + std::string(aero::str::Trim(member));
                    }
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
        if (IsBoolPropertyFor(targetType, propName, rawVal)) {
            return aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "True") ? "true" : "false";
        }
        if (IsIntPropertyFor(targetType, propName, rawVal)) {
            const long v = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
            return std::to_string(v);
        }
        if ((propName == "Width" || propName == "Height") &&
            aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "Auto")) {
            return "ParseUtil.NaN()";
        }
        if (IsFloatPropertyFor(targetType, propName, rawVal)) {
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
        if (propName == "RenderTransformOrigin") {
            std::vector<float> pts;
            if (TryParseFloatList(rawVal, pts) && pts.size() >= 2) {
                return "new Point(" + FormatFloatLiteral(pts[0]) + ", " + FormatFloatLiteral(pts[1]) + ")";
            }
            return "new Point(0.5f, 0.5f)";
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
        if (const auto* src = bindNode.FindAttribute("Source")) {
            outSpec.hasSourceLiteral = true;
            outSpec.sourceLiteral = src->value;
        }
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

    [[nodiscard]] std::string EmitBindingObjectFromSpec(
        const ParsedBindingSpec& subSpec,
        std::string_view propName,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::string_view subConv = {},
        std::string_view sourceResKey = {}) {
        const std::string bVar = FreshVar("subb");
        Indent(out, indent);
        out << "var " << bVar << " = new Binding(" << EscapeRsString(NormalizeBindingPath(subSpec.path)) << ");\n";
        if (!subSpec.elementName.empty()) {
            Indent(out, indent);
            out << bVar << ".ElementName = " << EscapeRsString(subSpec.elementName) << ";\n";
        }
        if (!subSpec.mode.empty()) {
            const int modeInt = MapBindingMode(subSpec.mode);
            const char* modeEnum = "BindingMode.OneWay";
            if (modeInt == 1) modeEnum = "BindingMode.TwoWay";
            else if (modeInt == 2) modeEnum = "BindingMode.OneTime";
            else if (modeInt == 3) modeEnum = "BindingMode.OneWayToSource";
            Indent(out, indent);
            out << bVar << ".Mode = " << modeEnum << ";\n";
        }
        if (!subSpec.relativeSourceMode.empty()) {
            Indent(out, indent);
            out << bVar << ".RelativeSourceMode = " << EscapeRsString(subSpec.relativeSourceMode) << ";\n";
        }
        if (!subSpec.ancestorType.empty()) {
            Indent(out, indent);
            out << bVar << ".RelativeSourceAncestorType = " << EscapeRsString(subSpec.ancestorType) << ";\n";
        }
        if (!sourceResKey.empty()) {
            Indent(out, indent);
            out << bVar << ".SourceBox = DependencyObject.BoxObject(" << LookupResourceExpr(sourceResKey, dictVar) << ");\n";
        } else if (subSpec.hasSourceLiteral) {
            Indent(out, indent);
            out << bVar << ".SourceBox = DependencyObject.BoxString(" << EscapeRsString(subSpec.sourceLiteral) << ");\n";
        }
        if (!subConv.empty()) {
            Indent(out, indent);
            out << bVar << ".Converter = " << subConv << ";\n";
        } else if (!subSpec.converterKey.empty()) {
            Indent(out, indent);
            out << bVar << ".ConverterKey = " << EscapeRsString(subSpec.converterKey) << ";\n";
            Indent(out, indent);
            out << "var __c_" << bVar << " = " << LookupResourceExpr(subSpec.converterKey, dictVar) << ";\n";
            Indent(out, indent);
            out << "if (__c_" << bVar << " is IValueConverter) { "
                << bVar << ".Converter = (__c_" << bVar << " as IValueConverter); }\n";
        }
        if (!subSpec.converterParam.empty()) {
            Indent(out, indent);
            out << bVar << ".ConverterParameter = " << EscapeRsString(subSpec.converterParam) << ";\n";
        }
        if (!subSpec.stringFormat.empty()) {
            Indent(out, indent);
            out << bVar << ".StringFormat = " << EscapeRsString(subSpec.stringFormat) << ";\n";
        }
        if (subSpec.hasFallbackValue) {
            Indent(out, indent);
            out << bVar << ".FallbackBox = "
                << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, subSpec.fallbackValue, dictVar)) << ";\n";
        }
        if (subSpec.hasTargetNullValue) {
            Indent(out, indent);
            out << bVar << ".TargetNullBox = "
                << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, subSpec.targetNullValue, dictVar)) << ";\n";
        }
        return bVar;
    }

    [[nodiscard]] std::string EmitDynamicBindingObject(
        const XamlNode& bindNode,
        std::string_view propName,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        ParsedBindingSpec subSpec;
        std::string subConv;
        TryParseBindingNode(bindNode, subSpec, subConv, dictVar, out, indent);
        return EmitBindingObjectFromSpec(subSpec, propName, dictVar, out, indent, subConv);
    }

    [[nodiscard]] std::string EmitMultiBindingObject(
        const XamlNode& mbNode,
        std::string_view propName,
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
            const std::string bVar = EmitDynamicBindingObject(*ch, propName, dictVar, out, indent);
            Indent(out, indent);
            out << mbVar << ".AddBinding(" << bVar << ");\n";
        }
        return mbVar;
    }

    void EmitMultiBindingNode(
        std::string_view targetVar,
        std::string_view targetType,
        std::string_view propName,
        const XamlNode& mbNode,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string mbVar = EmitMultiBindingObject(mbNode, propName, dictVar, out, indent);
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
        std::string_view explicitBindSourceVar = {},
        bool isUiElementTarget = false) {
        if (targetType == "TextBlock") {
            if (aero::str::EndsWith(propName, "Text.Stroke")) {
                propName = "Stroke";
            } else if (aero::str::EndsWith(propName, "Text.StrokeThickness")) {
                propName = "StrokeThickness";
            } else if (propName == "noesis:RichText.Text" || propName == "RichText.Text") {
                propName = "RichText.Text";
                Indent(out, indent);
                out << "RichText.Register();\n";
            }
        }
        if (aero::str::EndsWith(propName, "Text.Placeholder")) {
            propName = "Placeholder";
        }
        std::string normalizedAttachedName;
        {
            const std::size_t dotPos = propName.find('.');
            const std::size_t colPos = propName.find(':');
            if (colPos != std::string_view::npos && (dotPos == std::string_view::npos || colPos < dotPos)) {
                const std::string_view stripped = propName.substr(colPos + 1);
                if (aero::str::StartsWith(stripped, "DragDrop.") ||
                    aero::str::StartsWith(stripped, "ToolTipService.") ||
                    aero::str::StartsWith(stripped, "Element.")) {
                    normalizedAttachedName = std::string(stripped);
                    propName = normalizedAttachedName;
                }
            }
        }
        if (propName == "noesis:Loc.Source" || propName == "Loc.Source") {
            propName = "Loc.Source";
            EmitSiblingLanguageDictionaries(out, indent);
        }
        if (propName == "noesis:TransitionContentPresenter.Enter" ||
            propName == "TransitionContentPresenter.Enter" ||
            propName == "noesis:TransitionContentPresenter.Exit" ||
            propName == "TransitionContentPresenter.Exit") {
            const bool isEnter = aero::str::EndsWith(propName, ".Enter");
            std::string key;
            if (LooksLikeMarkupExtension(rawVal)) {
                key = ResolveResourceKeyFromExtension(ParseMarkupExtension(rawVal));
            } else {
                key = std::string(aero::str::Trim(rawVal));
            }
            if (!key.empty()) {
                Indent(out, indent);
                out << "TransitionContentPresenter." << (isEnter ? "SetEnterKey" : "SetExitKey")
                    << "(" << targetVar << ", " << EscapeRsString(key) << ");\n";
            }
            return;
        }
        const bool isCustomUiElementAttr =
            isUiElementTarget &&
            !IsKnownUIElementType(targetType) &&
            !IsNonUIElementType(targetType) &&
            propName.find('.') == std::string_view::npos &&
            propName.find(':') == std::string_view::npos;
        std::string ownedRawVal;
        if (LooksLikeMarkupExtension(rawVal)) {
            const auto ext = ParseMarkupExtension(rawVal);
            if (!ext.IsOk()) {
                AddError("Invalid markup extension '" + std::string(rawVal) + "': " + ext.errorMessage);
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
                    } else if (propName == "ContentTemplate") {
                        Indent(out, indent);
                        out << targetVar << ".ContentTemplate = (DataTemplate)" << lookup << ";\n";
                    } else if (propName == "HeaderTemplate") {
                        Indent(out, indent);
                        out << targetVar << ".HeaderTemplate = (DataTemplate)" << lookup << ";\n";
                    } else if (propName == "ItemsPanel") {
                        Indent(out, indent);
                        out << targetVar << ".ItemsPanel = ((ItemsPanelTemplate)" << lookup << ").GetFactory();\n";
                    } else if (propName == "Storyboard") {
                        // Handled at action level
                    } else if (IsDirectUIPropertyFor(targetType, propName) ||
                               propName == "Tag" || propName == "ToolTip" ||
                               propName == "Placeholder" || aero::str::StartsWith(propName, "Custom.") ||
                               aero::str::StartsWith(propName, "DragDrop.") ||
                               aero::str::StartsWith(propName, "ToolTipService.")) {
                        if (propName == "ToolTip" || aero::str::StartsWith(propName, "ToolTipService.")) {
                            Indent(out, indent);
                            out << "ToolTipService.EnsureInstalled();\n";
                        }
                        Indent(out, indent);
                        out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                            << ", DependencyObject.BoxObject(" << lookup << "));\n";
                    } else if ((IsDirectUIProperty(propName) || isCustomUiElementAttr) && !IsNonUIElementType(targetType)) {
                        if (propName == "ToolTip" || aero::str::StartsWith(propName, "ToolTipService.")) {
                            Indent(out, indent);
                            out << "ToolTipService.EnsureInstalled();\n";
                        }
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

        if (targetType == "LayoutScaler" && propName == "ReferenceResolution") {
            std::vector<float> vals;
            if (TryParseFloatList(rawVal, vals) && vals.size() >= 2) {
                Indent(out, indent);
                out << targetVar << ".ReferenceResolution = new Size("
                    << FormatFloatLiteral(vals[0]) << ", " << FormatFloatLiteral(vals[1]) << ");\n";
                return;
            }
        }

        if (targetType == "BoxShadow" &&
            (propName == "BlurRadius" || propName == "OffsetX" || propName == "OffsetY" ||
             propName == "Spread" || propName == "Inside" || propName == "Color")) {
            Indent(out, indent);
            if (propName == "Color") {
                out << targetVar << ".Color = " << EscapeRsString(ResolveColorHexOrString(rawVal)) << ";\n";
            } else {
                out << targetVar << "." << propName << " = "
                    << FormatTypedValueLiteral(propName, rawVal, dictVar, targetType) << ";\n";
            }
            return;
        }

        if (IsDirectUIPropertyFor(targetType, propName) && propName != "Title") {
            Indent(out, indent);
            out << targetVar << "." << propName << " = "
                << FormatTypedValueLiteral(propName, rawVal, dictVar, targetType) << ";\n";
            return;
        }

        // Grid compact shorthand attributes `RowDefinitions="Auto,*"`
        if (targetType == "Grid" &&
            (propName == "RowDefinitions" || propName == "ColumnDefinitions")) {
            const std::string method = (propName == "RowDefinitions")
                ? "AddRowDefinition"
                : "AddColumnDefinition";
            if (LooksLikeMarkupExtension(rawVal)) {
                EmitSingleGridTrackDef(targetVar, method, rawVal, dictVar, out, indent);
                return;
            }
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
        if (aero::str::StartsWith(propName, "Custom.") ||
            aero::str::StartsWith(propName, "DragDrop.") ||
            aero::str::StartsWith(propName, "ToolTipService.")) {
            if (aero::str::StartsWith(propName, "ToolTipService.")) {
                Indent(out, indent);
                out << "ToolTipService.EnsureInstalled();\n";
            }
            Indent(out, indent);
            if (aero::str::EndsWith(propName, "_Int32") || aero::str::EndsWith(propName, "_Int")) {
                const long iv = std::strtol(std::string(rawVal).c_str(), nullptr, 10);
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxInt(" << iv << "));\n";
            } else if (aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "True") ||
                       aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "False")) {
                const bool bv = aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "True");
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxBool(" << (bv ? "true" : "false") << "));\n";
            } else {
                out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxString(" << EscapeRsString(rawVal) << "));\n";
            }
            return;
        }
        if (propName == "Tag" || propName == "ToolTip" || propName == "Title" ||
            propName == "InputGestureText" || propName == "CommandParameter" || propName == "Command" ||
            propName == "Placeholder" || propName == "FontWeight" || propName == "DisplayMemberPath" ||
            propName == "RichText.Text" || propName == "Loc.Source" || propName == "ContentSource" ||
            propName == "Placement" || propName == "HorizontalOffset" || propName == "VerticalOffset" ||
            propName == "IsOpen" || propName == "StaysOpen" ||
            propName == "Delay" || propName == "Interval" || propName == "CanContentScroll" ||
            (IsDirectUIProperty(propName) && !IsNonUIElementType(targetType) && propName != "Title")) {
            if (propName == "ToolTip") {
                Indent(out, indent);
                out << "ToolTipService.EnsureInstalled();\n";
            }
            Indent(out, indent);
            out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName) << ", "
                << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, rawVal, dictVar, targetType), targetType)
                << ");\n";
        } else if (isCustomUiElementAttr) {
            const std::string_view trimmedVal = aero::str::Trim(rawVal);
            std::string boxExpr;
            if (aero::str::EqualsIgnoreCase(trimmedVal, "True") ||
                aero::str::EqualsIgnoreCase(trimmedVal, "False")) {
                const bool bv = aero::str::EqualsIgnoreCase(trimmedVal, "True");
                boxExpr = std::string("DependencyObject.BoxBool(") + (bv ? "true" : "false") + ")";
            } else {
                bool isPureInt = !trimmedVal.empty();
                std::size_t idx = (trimmedVal.front() == '+' || trimmedVal.front() == '-') ? 1u : 0u;
                if (idx >= trimmedVal.size()) {
                    isPureInt = false;
                }
                for (; idx < trimmedVal.size(); ++idx) {
                    if (!std::isdigit(static_cast<unsigned char>(trimmedVal[idx]))) {
                        isPureInt = false;
                        break;
                    }
                }
                if (isPureInt) {
                    const std::string numStr(trimmedVal);
                    errno = 0;
                    char* endPtr = nullptr;
                    const long long iv = std::strtoll(numStr.c_str(), &endPtr, 10);
                    if (errno == 0 && endPtr == numStr.c_str() + numStr.size() &&
                        iv >= -2147483647LL && iv <= 2147483647LL) {
                        boxExpr = "DependencyObject.BoxInt(" + std::to_string(iv) + ")";
                    }
                }
                if (boxExpr.empty()) {
                    float fv = 0.0f;
                    if (aero::str::TryParseFloat32(trimmedVal, fv)) {
                        boxExpr = "DependencyObject.BoxFloat(" + FormatFloatLiteral(fv) + ")";
                    } else {
                        boxExpr = "DependencyObject.BoxString(" + EscapeRsString(rawVal) + ")";
                    }
                }
            }
            Indent(out, indent);
            out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName) << ", " << boxExpr << ");\n";
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
            std::uint64_t rawHash = 0u;
            const std::string content = ReadFileTextUtf8(lp, &rawHash);
            if (content.empty()) {
                continue;
            }
            RecordExternalDependency(lp.lexically_normal().generic_string(), rawHash);
            std::unique_ptr<XamlNode> langRoot;
            if (!BuildXamlDom(content, langRoot, diagnostics_) || !langRoot) {
                hasErrors_ = true;
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
        if (propName == "ToolTip" || propName == "ToolTipService.ToolTip" ||
            aero::str::StartsWith(propName, "ToolTipService.")) {
            Indent(out, indent);
            out << "ToolTipService.EnsureInstalled();\n";
        }
        if (spec.hasFallbackValue && IsDirectUIPropertyFor(targetType, propName)) {
            Indent(out, indent);
            out << targetVar << "." << propName << " = "
                << FormatTypedValueLiteral(propName, spec.fallbackValue, {}, targetType) << ";\n";
        }

        const std::string cleanPath = NormalizeBindingPath(spec.path);
        const bool needExtended =
            !inlineConvExpr.empty() || spec.hasFallbackValue || spec.hasTargetNullValue ||
            !spec.ancestorType.empty() ||
            (spec.hasRelativeSource && spec.relativeSourceMode != "TemplatedParent");

        const std::string convArg = inlineConvExpr.empty() ? std::string("null") : std::string(inlineConvExpr);
        const std::string fbHas = spec.hasFallbackValue ? "true" : "false";
        const std::string fbBox = spec.hasFallbackValue
            ? BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, spec.fallbackValue, {}, targetType), targetType)
            : std::string("null");
        const std::string tnHas = spec.hasTargetNullValue ? "true" : "false";
        const std::string tnBox = spec.hasTargetNullValue
            ? BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, spec.targetNullValue, {}, targetType), targetType)
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
                        << EscapeRsString(cleanPath);
                    if (modeInt != 0) {
                        out << ", " << modeInt;
                    }
                    out << ");\n";
                } else {
                    out << "BindingEngine.BindTemplateParentEx(" << targetVar << ", "
                        << EscapeRsString(propName) << ", " << currentTplParentVar_ << ", "
                        << EscapeRsString(cleanPath) << ", "
                        << convArg << ", "
                        << EscapeRsString(spec.converterKey) << ", "
                        << EscapeRsString(spec.converterParam) << ", "
                        << EscapeRsString(spec.stringFormat) << ", "
                        << fbHas << ", " << fbBox << ", "
                        << tnHas << ", " << tnBox;
                    if (modeInt != 0) {
                        out << ", " << modeInt;
                    }
                    out << ");\n";
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
            spec.converterKey.empty() && spec.stringFormat.empty() && inlineConvExpr.empty() &&
            !needExtended && !targetType.empty() && (modeInt == 0 || modeInt == 2)) {
            if (modeInt == 2) {
                const std::string typedVar = FreshVar("__vm");
                Indent(out, indent);
                out << "if (" << explicitSourceVar << " is " << xDataType << " " << typedVar << ") { ";
                if (propName == "Content") {
                    out << targetVar << ".SetContent(" << typedVar << "." << cleanPath << ");";
                } else if (IsDirectUIPropertyFor(targetType, propName)) {
                    out << targetVar << "." << propName << " = " << typedVar << "." << cleanPath << ";";
                } else {
                    out << targetVar << ".SetPropertyBox(" << EscapeRsString(propName)
                        << ", DependencyObject.BoxObject(" << typedVar << "." << cleanPath << "));";
                }
                out << " }\n";
                return;
            }
            const std::string tArg = FreshVar("__tgt");
            const std::string sArg = FreshVar("__src");
            const std::string tEl = FreshVar("__tel");
            const std::string vmVar = FreshVar("__vm");
            Indent(out, indent);
            out << "BindingEngine.BindCompiledAction(" << bindTarget << ", "
                << EscapeRsString(propName) << ", " << srcExpr << ", 0, "
                << "(UIElement " << tArg << ", object " << sArg << ") => { "
                << "if (" << tArg << " is " << targetType << " " << tEl << " && "
                << sArg << " is " << xDataType << " " << vmVar << ") { ";
            if (propName == "Content") {
                out << tEl << ".SetContent(" << vmVar << "." << cleanPath << ");";
            } else if (IsDirectUIPropertyFor(targetType, propName)) {
                out << tEl << "." << propName << " = " << vmVar << "." << cleanPath << ";";
            } else {
                out << tEl << ".SetPropertyBox(" << EscapeRsString(propName)
                    << ", DependencyObject.BoxObject(" << vmVar << "." << cleanPath << "));";
            }
            out << " } });\n";
            return;
        }

        if (!xDataType.empty() && !explicitSourceVar.empty() &&
            !cleanPath.empty() && cleanPath.find('.') == std::string::npos &&
            spec.converterKey.empty() && spec.stringFormat.empty() && inlineConvExpr.empty()) {
            const std::string typedVar = FreshVar("__vm");
            Indent(out, indent);
            out << "if (" << explicitSourceVar << " is " << xDataType << " " << typedVar << ") { ";
            if (propName == "Content") {
                out << targetVar << ".SetContent(" << typedVar << "." << cleanPath << ");";
            } else if (IsDirectUIPropertyFor(targetType, propName)) {
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

    void EmitSingleGridTrackDef(
        std::string_view targetVar,
        std::string_view method,
        std::string_view rawLen,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::string_view defName = {}) {
        const std::string nameSuffix = defName.empty() ? std::string{} : (", " + EscapeRsString(defName));
        if (LooksLikeMarkupExtension(rawLen)) {
            const auto ext = ParseMarkupExtension(rawLen);
            if (!ext.IsOk()) {
                AddError("Invalid markup extension '" + std::string(rawLen) + "': " + ext.errorMessage);
                Indent(out, indent);
                out << targetVar << "." << method << "(GridUnitType.Auto, 1.0f" << nameSuffix << ");\n";
                return;
            }
            std::string resKey;
            std::string subPath;
            bool isStaticSrc = false;
            if (ext.kind == MarkupExtensionKind::StaticResource ||
                ext.kind == MarkupExtensionKind::DynamicResource) {
                resKey = ResolveResourceKeyFromExtension(ext);
                isStaticSrc = (ext.kind == MarkupExtensionKind::StaticResource);
            } else if (ext.kind == MarkupExtensionKind::Binding) {
                if (const auto* srcArg = ext.FindNamed("Source");
                    srcArg != nullptr && srcArg->isExtension && srcArg->extension &&
                    (srcArg->extension->kind == MarkupExtensionKind::StaticResource ||
                     srcArg->extension->kind == MarkupExtensionKind::DynamicResource)) {
                    resKey = ResolveResourceKeyFromExtension(*srcArg->extension);
                    isStaticSrc = (srcArg->extension->kind == MarkupExtensionKind::StaticResource);
                }
                ParsedBindingSpec spec;
                if (TryExtractBindingSpec(ext, spec)) {
                    subPath = NormalizeBindingPath(spec.path);
                }
            }
            if (!resKey.empty()) {
                if (isStaticSrc && subPath.empty()) {
                    if (const auto it = knownDoubles_.find(resKey); it != knownDoubles_.end()) {
                        Indent(out, indent);
                        out << targetVar << "." << method << "(GridUnitType.Pixel, "
                            << FormatFloatLiteral(it->second) << nameSuffix << ");\n";
                        return;
                    }
                }
                const std::string srcVar = FreshVar("glSrc");
                const std::string glVar = FreshVar("glVal");
                Indent(out, indent);
                out << "var " << srcVar << " = " << LookupResourceExpr(resKey, dictVar) << ";\n";
                if (!subPath.empty()) {
                    Indent(out, indent);
                    out << srcVar << " = BindingEngine.ReadPathBox(" << srcVar << ", "
                        << EscapeRsString(subPath) << ");\n";
                }
                Indent(out, indent);
                out << "var " << glVar << " = GridLength.Auto();\n";
                Indent(out, indent);
                out << "if (" << srcVar << " is GridLength) { " << glVar << " = (" << srcVar << " as GridLength); }\n";
                Indent(out, indent);
                out << "else if (" << srcVar << " is string) { " << glVar
                    << " = GridLength.Parse(" << srcVar << " as string); }\n";
                Indent(out, indent);
                out << "else if (" << srcVar << " is BoxedString) { " << glVar
                    << " = GridLength.Parse((" << srcVar << " as BoxedString).Value); }\n";
                Indent(out, indent);
                out << "else if (" << srcVar << " is DependencyObject) { " << glVar
                    << " = GridLength.FromPixel(DependencyObject.UnboxFloat(" << srcVar
                    << " as DependencyObject, 0.0f)); }\n";
                Indent(out, indent);
                out << targetVar << "." << method << "(" << glVar << ".GridUnitType, " << glVar << ".Value" << nameSuffix << ");\n";
                return;
            }
        }
        const auto [unit, val] = ParseGridLength(rawLen);
        Indent(out, indent);
        out << targetVar << "." << method << "(" << unit << ", " << val << nameSuffix << ");\n";
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
            if (targetVar == "this") {
                Indent(out, indent);
                out << "this._internalRootName = " << EscapeRsString(elemName) << ";\n";
            }
            RegisterNamedField(elemName, std::string(resolvedTypeName));
            if (inPartialClass_ && targetVar != "this") {
                Indent(out, indent);
                out << "this." << elemName << " = " << targetVar << ";\n";
            }
        }

        // Resources property element before child properties so StaticResource references resolve
        EmitResourcesPropertyElement(node, xmlLocalName, targetVar, currentXamlPath, out, indent);

        // Emit regular attributes
        bool hasDragDropAttr = false;
        for (const auto& attr : node.attributes) {
            if (attr.isXmlns || IsDesignOrMarkupCompatPrefix(attr.prefix, attr.namespaceUri)) {
                continue;
            }
            if (aero::str::StartsWith(attr.rawName, "DragDrop.") ||
                attr.rawName.find(":DragDrop.") != std::string::npos) {
                hasDragDropAttr = true;
                break;
            }
        }
        if (hasDragDropAttr) {
            Indent(out, indent);
            out << "DragDrop.EnsureInstalled();\n";
        }
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
                xDataType, dictVar, out, indent, explicitBindSourceVar,
                !IsNonUIElementType(resolvedTypeName));
        }

        // Grid RowDefinitions / ColumnDefinitions
        if (const auto* rDefs = node.FindPropertyElement(xmlLocalName, "RowDefinitions")) {
            for (const auto& rd : rDefs->children) {
                if (rd->localName != "RowDefinition") continue;
                std::string rawH = "*";
                if (const auto* h = rd->FindAttribute("Height")) rawH = h->value;
                EmitSingleGridTrackDef(targetVar, "AddRowDefinition", rawH, dictVar, out, indent, rd->GetName());
            }
        }
        if (const auto* cDefs = node.FindPropertyElement(xmlLocalName, "ColumnDefinitions")) {
            for (const auto& cd : cDefs->children) {
                if (cd->localName != "ColumnDefinition") continue;
                std::string rawW = "*";
                if (const auto* w = cd->FindAttribute("Width")) rawW = w->value;
                EmitSingleGridTrackDef(targetVar, "AddColumnDefinition", rawW, dictVar, out, indent, cd->GetName());
            }
        }

        // Property elements for brushes, transforms, projections, triggers, behaviors, bindings
        for (const auto& [propKey, propNode] : node.propertyElements) {
            const std::size_t dotPos = propKey.find('.');
            const std::string shortProp = (dotPos != std::string::npos)
                ? propKey.substr(dotPos + 1)
                : propKey;
            if (shortProp == "Resources" || shortProp == "RowDefinitions" ||
                shortProp == "ColumnDefinitions") {
                continue;
            }
            if (propKey == "Navigation.Destinations") {
                for (const auto& destNode : propNode->children) {
                    if (destNode->localName != "NavDestination") continue;
                    const std::string ndVar = FreshVar("navd");
                    Indent(out, indent);
                    out << "var " << ndVar << " = new NavDestination();\n";
                    if (const auto* idAttr = destNode->FindAttribute("Id")) {
                        Indent(out, indent);
                        out << ndVar << ".Id = " << EscapeRsString(idAttr->value) << ";\n";
                    }
                    if (const auto* cAttr = destNode->FindAttribute("Content")) {
                        if (LooksLikeMarkupExtension(cAttr->value)) {
                            const auto ext = ParseMarkupExtension(cAttr->value);
                            ParsedBindingSpec bSpec;
                            if (TryExtractBindingSpec(ext, bSpec)) {
                                Indent(out, indent);
                                out << ndVar << ".BindingPath = " << EscapeRsString(bSpec.path) << ";\n";
                            }
                        } else {
                            Indent(out, indent);
                            out << ndVar << ".ContentBox = DependencyObject.BoxString("
                                << EscapeRsString(cAttr->value) << ");\n";
                        }
                    }
                    if (const auto* ctAttr = destNode->FindAttribute("ContentTemplate")) {
                        if (LooksLikeMarkupExtension(ctAttr->value)) {
                            const auto ext = ParseMarkupExtension(ctAttr->value);
                            const std::string rk = ResolveResourceKeyFromExtension(ext);
                            if (!rk.empty()) {
                                Indent(out, indent);
                                out << "var __nt_" << ndVar << " = " << LookupResourceExpr(rk, dictVar) << ";\n";
                                Indent(out, indent);
                                out << "if (__nt_" << ndVar << " is DataTemplate) { "
                                    << ndVar << ".ContentTemplate = (__nt_" << ndVar << " as DataTemplate); }\n";
                            }
                        }
                    }
                    Indent(out, indent);
                    out << "Navigation.RegisterDestination(" << targetVar << ", " << ndVar << ");\n";
                }
                continue;
            }
            if (shortProp == "InputBindings") {
                EmitInputBindings(*propNode, targetVar, out, indent);
                continue;
            }
            if (propKey == "VisualStateManager.VisualStateGroups" || shortProp == "VisualStateGroups") {
                EmitVisualStateGroups(*propNode, targetVar, dictVar, out, indent);
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
                } else if (brushChild.localName == "ImageBrush") {
                    if (const auto* srcAttr = brushChild.FindAttribute("ImageSource");
                        srcAttr != nullptr && LooksLikeMarkupExtension(srcAttr->value)) {
                        const auto ext = ParseMarkupExtension(srcAttr->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName, shortProp + ".ImageSource", bSpec,
                                xDataType, out, indent, explicitBindSourceVar);
                        }
                    }
                    if (const auto* shProp = FindBrushShaderElement(brushChild);
                        shProp != nullptr && !shProp->children.empty() &&
                        shProp->children.front()->localName == "PlasmaBrush") {
                        const XamlNode& pb = *shProp->children.front();
                        auto emitPbBind = [&](std::string_view xmlAttr, std::string_view propSuffix) {
                            if (const auto* pa = pb.FindAttribute(xmlAttr);
                                pa != nullptr && LooksLikeMarkupExtension(pa->value)) {
                                const auto ext = ParseMarkupExtension(pa->value);
                                ParsedBindingSpec bSpec;
                                if (TryExtractBindingSpec(ext, bSpec)) {
                                    EmitBindingSpec(
                                        targetVar, resolvedTypeName,
                                        std::string(shortProp) + "." + std::string(propSuffix),
                                        bSpec, xDataType, out, indent, explicitBindSourceVar);
                                }
                            }
                        };
                        emitPbBind("Seed", "PlasmaSeed");
                        emitPbBind("Color", "PlasmaColor");
                        emitPbBind("Time", "PlasmaTime");
                    }
                }
                continue;
            }
            if (shortProp == "RenderTransform" && !propNode->children.empty()) {
                const std::string trVar = EmitTransformObject(
                    *propNode->children.front(), out, indent, targetVar, xDataType, dictVar, "RenderTransform");
                if (!trVar.empty()) {
                    Indent(out, indent);
                    out << targetVar << ".RenderTransform = " << trVar << ";\n";
                }
                continue;
            }
            if (shortProp == "LayoutTransform" && !propNode->children.empty()) {
                const std::string trVar = EmitTransformObject(
                    *propNode->children.front(), out, indent, targetVar, xDataType, dictVar, "LayoutTransform");
                if (!trVar.empty()) {
                    Indent(out, indent);
                    out << targetVar << ".LayoutTransform = " << trVar << ";\n";
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
            if ((shortProp == "ContentTemplate" || shortProp == "HeaderTemplate") &&
                !propNode->children.empty()) {
                const std::string dtVar = EmitDataTemplate(*propNode->children.front(), dictVar, out, indent);
                Indent(out, indent);
                out << targetVar << "." << shortProp << " = " << dtVar << ";\n";
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
            if (shortProp == "ToolTip" && !propNode->children.empty() &&
                currentTplParentVar_.empty() &&
                CanEmitLazyToolTipNode(*propNode->children.front(), true)) {
                Indent(out, indent);
                out << "ToolTipService.EnsureInstalled();\n";
                const std::string dcParam = FreshVar("__tipDc");
                Indent(out, indent);
                out << targetVar << ".SetLazyToolTip((object " << dcParam << ") =>\n";
                Indent(out, indent);
                out << "{\n";
                const bool prevPartial = inPartialClass_;
                inPartialClass_ = false;
                const std::string sub = EmitConstructedElement(
                    *propNode->children.front(), xDataType, {}, currentXamlPath, out, indent + 1,
                    dcParam);
                inPartialClass_ = prevPartial;
                Indent(out, indent + 1);
                out << sub << ".DataContext = " << dcParam << ";\n";
                Indent(out, indent + 1);
                out << "return " << sub << ";\n";
                Indent(out, indent);
                out << "});\n";
                continue;
            }
            if ((shortProp == "Content" || shortProp == "Header" || shortProp == "Icon" ||
                 shortProp == "ToolTip") && !propNode->children.empty()) {
                if (shortProp == "ToolTip") {
                    Indent(out, indent);
                    out << "ToolTipService.EnsureInstalled();\n";
                }
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
            if (shortProp == "Data" && !propNode->children.empty()) {
                const auto& geomNode = *propNode->children.front();
                const std::string svgData = (geomNode.localName == "PathGeometry")
                    ? ConvertPathGeometryToSvgData(geomNode)
                    : std::string(aero::str::Trim(geomNode.textContent));
                if (!svgData.empty()) {
                    Indent(out, indent);
                    out << targetVar << ".Data = " << EscapeRsString(svgData) << ";\n";
                }
                continue;
            }
            if (shortProp == "Triggers") {
                EmitElementTriggers(*propNode, targetVar, dictVar, out, indent);
            }
        }

        // Text / Runs vs Child elements
        if (resolvedTypeName == "TextBlock") {
            auto isInlineTag = [](std::string_view ln) noexcept {
                return ln == "Run" || ln == "Hyperlink" || ln == "Bold" ||
                       ln == "Italic" || ln == "Underline" || ln == "Span" || ln == "LineBreak";
            };
            bool hasRunChildren = false;
            for (const auto& ch : node.children) {
                if (isInlineTag(ch->localName)) {
                    hasRunChildren = true;
                    break;
                }
            }
            if (hasRunChildren) {
                for (const auto& ch : node.children) {
                    if (ch->localName == "#text") {
                        if (!ch->textContent.empty()) {
                            const std::string tVar = FreshVar("run");
                            Indent(out, indent);
                            out << "var " << tVar << " = new Run(" << EscapeRsString(ch->textContent) << ");\n";
                            Indent(out, indent);
                            out << targetVar << ".AddRun(" << tVar << ");\n";
                        }
                        continue;
                    }
                    if (!isInlineTag(ch->localName)) continue;
                    if (ch->localName == "LineBreak") {
                        Indent(out, indent);
                        out << targetVar << ".AddInline(new LineBreak());\n";
                        continue;
                    }
                    const std::string rVar = EmitRunElement(*ch, xDataType, dictVar, out, indent);
                    if (ch->localName == "Hyperlink" && ch->FindAttribute("Foreground") == nullptr) {
                        Indent(out, indent);
                        out << rVar << ".Foreground = new SolidColorBrush(\"#FF1098DF\");\n";
                    } else if (ch->localName == "Bold") {
                        Indent(out, indent);
                        out << rVar << ".FontWeight = \"Bold\";\n";
                    } else if (ch->localName == "Italic") {
                        Indent(out, indent);
                        out << rVar << ".FontStyle = \"Italic\";\n";
                    } else if (ch->localName == "Underline") {
                        Indent(out, indent);
                        out << rVar << ".Decorations = 1;\n";
                    }
                    Indent(out, indent);
                    if (ch->precededByXmlSpace) {
                        out << targetVar << ".AddRunSpaced(" << rVar << ");\n";
                    } else {
                        out << targetVar << ".AddRun(" << rVar << ");\n";
                    }
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
            const bool wrapClosedPopupChild = [&]() noexcept -> bool {
                if (resolvedTypeName != "Popup") {
                    return false;
                }
                if (const auto* openAttr = node.FindAttribute("IsOpen")) {
                    if (aero::str::EqualsIgnoreCase(aero::str::Trim(openAttr->value), "true")) {
                        return false;
                    }
                }
                return true;
            }();
            if (wrapClosedPopupChild) {
                Indent(out, indent);
                out << "UIElement._closedPopupBuildDepth += 1;\n";
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
            if (wrapClosedPopupChild) {
                Indent(out, indent);
                out << "UIElement._closedPopupBuildDepth -= 1;\n";
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
        const std::string rName = node.GetName();
        if (!rName.empty()) {
            Indent(out, indent);
            out << rv << ".Name = " << EscapeRsString(rName) << ";\n";
            
        }
        if (const auto* n = node.FindAttribute("InputName")) {
            Indent(out, indent);
            out << rv << ".InputName = " << EscapeRsString(n->value) << ";\n";
        }
        Indent(out, indent);
        out << riveVar << ".AddInput(" << rv << ");\n";
        if (const auto* v = node.FindAttribute("InputValue")) {
            if (LooksLikeMarkupExtension(v->value)) {
                const auto ext = ParseMarkupExtension(v->value);
                ParsedBindingSpec bSpec;
                if (TryExtractBindingSpec(ext, bSpec)) {
                    EmitBindingSpec(rv, "RiveInput", "InputValue", bSpec, {}, out, indent, {});
                }
            } else {
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
        }
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

    [[nodiscard]] std::string EmitCompositeTransform3DObject(
        const XamlNode& node,
        std::ostringstream& out,
        int indent,
        std::string_view hostTargetVar = {}) {
        const std::string tv = FreshVar("t3d");
        Indent(out, indent);
        out << "var " << tv << " = new CompositeTransform3D();\n";
        auto emitScalarOrBind = [&](std::string_view attrName, float defVal) {
            if (const auto* a = node.FindAttribute(attrName)) {
                if (LooksLikeMarkupExtension(a->value)) {
                    if (!hostTargetVar.empty()) {
                        const auto ext = ParseMarkupExtension(a->value);
                        ParsedBindingSpec spec;
                        if (TryExtractBindingSpec(ext, spec)) {
                            EmitBindingSpec(
                                hostTargetVar, "UIElement",
                                "Transform3D." + std::string(attrName),
                                spec, "", out, indent, {});
                        }
                    }
                } else {
                    float f = defVal;
                    if (aero::str::TryParseFloat32(a->value, f)) {
                        Indent(out, indent);
                        out << tv << "." << attrName << " = " << FormatFloatLiteral(f) << ";\n";
                    }
                }
            }
        };
        emitScalarOrBind("RotationX", 0.0f);
        emitScalarOrBind("RotationY", 0.0f);
        emitScalarOrBind("RotationZ", 0.0f);
        emitScalarOrBind("TranslateX", 0.0f);
        emitScalarOrBind("TranslateY", 0.0f);
        emitScalarOrBind("TranslateZ", 0.0f);
        emitScalarOrBind("ScaleX", 1.0f);
        emitScalarOrBind("ScaleY", 1.0f);
        emitScalarOrBind("ScaleZ", 1.0f);
        emitScalarOrBind("CenterX", 0.0f);
        emitScalarOrBind("CenterY", 0.0f);
        emitScalarOrBind("CenterZ", 0.0f);
        return tv;
    }

    void EmitTransform3D(
        const XamlNode& node,
        std::string_view targetVar,
        std::ostringstream& out,
        int indent) {
        const std::string tv = EmitCompositeTransform3DObject(node, out, indent, targetVar);
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
                    if (TryExtractBindingSpec(ext, spec) &&
                        (!spec.path.empty() || !spec.elementName.empty() ||
                         !spec.relativeSourceMode.empty() || !spec.ancestorType.empty())) {
                        const std::string effPath = spec.path.empty() ? std::string(".") : spec.path;
                        Indent(out, indent);
                        out << av << ".CommandParameterPath = " << EscapeRsString(effPath) << ";\n";
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
                bool handledMarkup = false;
                if (LooksLikeMarkupExtension(val->value)) {
                    const auto ext = ParseMarkupExtension(val->value);
                    if (!ext.IsOk()) {
                        AddError("Invalid markup extension '" + val->value + "': " + ext.errorMessage);
                        handledMarkup = true;
                    } else if (ext.kind == MarkupExtensionKind::Binding) {
                        ParsedBindingSpec spec;
                        if (TryExtractBindingSpec(ext, spec)) {
                            std::string srcResKey;
                            if (const auto* srcArg = ext.FindNamed("Source");
                                srcArg != nullptr && srcArg->isExtension && srcArg->extension &&
                                (srcArg->extension->kind == MarkupExtensionKind::StaticResource ||
                                 srcArg->extension->kind == MarkupExtensionKind::DynamicResource)) {
                                srcResKey = ResolveResourceKeyFromExtension(*srcArg->extension);
                            }
                            const std::string bv = EmitBindingObjectFromSpec(
                                spec, propName, dictVar, out, indent, {}, srcResKey);
                            Indent(out, indent);
                            out << av << ".ValueBinding = " << bv << ";\n";
                            handledMarkup = true;
                        }
                    } else if (ext.kind == MarkupExtensionKind::StaticResource ||
                               ext.kind == MarkupExtensionKind::DynamicResource) {
                        const std::string key = ResolveResourceKeyFromExtension(ext);
                        if (!key.empty()) {
                            Indent(out, indent);
                            out << av << ".SetValueBox(DependencyObject.BoxObject("
                                << LookupResourceExpr(key, dictVar) << "));\n";
                            handledMarkup = true;
                        }
                    }
                }
                if (!handledMarkup) {
                    Indent(out, indent);
                    out << av << ".SetValueBox("
                        << BoxSetterValueExpr(propName, FormatTypedValueLiteral(propName, val->value, dictVar))
                        << ");\n";
                }
            }
            if (const XamlNode* valProp = act.FindPropertyElement("ChangePropertyAction", "Value")) {
                for (const auto& ch : valProp->children) {
                    if (ch->localName == "Binding") {
                        const std::string bv = EmitDynamicBindingObject(*ch, propName, dictVar, out, indent);
                        Indent(out, indent);
                        out << av << ".ValueBinding = " << bv << ";\n";
                        break;
                    }
                    if (ch->localName == "MultiBinding") {
                        const std::string mbv = EmitMultiBindingObject(*ch, propName, dictVar, out, indent);
                        Indent(out, indent);
                        out << av << ".ValueMultiBinding = " << mbv << ";\n";
                        break;
                    }
                }
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
                if (act.localName == "NavigateToAction") {
                    if (const auto* did = act.FindAttribute("DestinationId")) {
                        Indent(out, indent);
                        out << av << ".DestinationId = " << EscapeRsString(did->value) << ";\n";
                    }
                }
                if (act.localName == "PlaySoundAction") {
                    if (const auto* vol = act.FindAttribute("Volume")) {
                        float v = 1.0f;
                        if (aero::str::TryParseFloat32(vol->value, v)) {
                            Indent(out, indent);
                            out << av << ".Volume = " << FormatFloatLiteral(v) << ";\n";
                        }
                    }
                }
            }
        } else if (act.localName == "GoToStateAction") {
            Indent(out, indent);
            out << "var " << av << " = new GoToStateAction();\n";
            if (const auto* sn = act.FindAttribute("StateName")) {
                Indent(out, indent);
                out << av << ".StateName = " << EscapeRsString(sn->value) << ";\n";
            }
            if (const auto* ut = act.FindAttribute("UseTransitions")) {
                const bool useTr = !aero::str::EqualsIgnoreCase(aero::str::Trim(ut->value), "False");
                Indent(out, indent);
                out << av << ".UseTransitions = " << (useTr ? "true" : "false") << ";\n";
            }
        } else if (act.localName == "BeginStoryboard") {
            std::string sbKey;
            std::string sbVar;
            if (const auto* sbAttr = act.FindAttribute("Storyboard");
                sbAttr != nullptr && LooksLikeMarkupExtension(sbAttr->value)) {
                sbKey = ResolveResourceKeyFromExtension(ParseMarkupExtension(sbAttr->value));
            }
            for (const auto& sbNode : act.children) {
                if (sbNode->localName == "Storyboard") {
                    sbVar = EmitStoryboard(*sbNode, out, indent, dictVar);
                    break;
                }
            }
            if (!sbVar.empty()) {
                Indent(out, indent);
                out << "var " << av << " = new ControlStoryboardAction(" << sbVar << ");\n";
            } else if (!sbKey.empty()) {
                Indent(out, indent);
                out << "var " << av << " = new ControlStoryboardAction();\n";
                Indent(out, indent);
                out << av << ".StoryboardKey = " << EscapeRsString(sbKey) << ";\n";
            } else {
                return {};
            }
        } else {
            return {};
        }

        const std::string actionName = act.GetName();
        if (!actionName.empty()) {
            Indent(out, indent);
            out << av << ".ActionName = " << EscapeRsString(actionName) << ";\n";
        }
        if (const auto* enabledAttr = act.FindAttribute("IsEnabled")) {
            const bool isEn = aero::str::EqualsIgnoreCase(aero::str::Trim(enabledAttr->value), "True");
            Indent(out, indent);
            out << av << ".IsEnabled = " << (isEn ? "true" : "false") << ";\n";
        }
        if (!targetName.empty()) {
            Indent(out, indent);
            out << av << ".TargetName = " << EscapeRsString(targetName) << ";\n";
        }
        if (const auto* to = act.FindAttribute("TargetObject");
            to != nullptr && LooksLikeMarkupExtension(to->value)) {
            const auto ext = ParseMarkupExtension(to->value);
            ParsedBindingSpec toSpec;
            if (TryExtractBindingSpec(ext, toSpec)) {
                Indent(out, indent);
                out << av << ".SetTargetObjectBinding("
                    << EscapeRsString(toSpec.path) << ", "
                    << EscapeRsString(toSpec.relativeSourceMode) << ", "
                    << EscapeRsString(toSpec.ancestorType) << ", "
                    << EscapeRsString(toSpec.elementName) << ");\n";
            }
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
        if (trig.localName == "EventTrigger" || trig.localName == "TimerTrigger") {
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
            if (!spec.elementName.empty()) {
                Indent(out, indent);
                out << tv << ".ElementName = " << EscapeRsString(spec.elementName) << ";\n";
            }
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
            bool emittedDynamicValue = false;
            if (const XamlNode* valProp = trig.FindPropertyElement("DataTrigger", "Value")) {
                for (const auto& ch : valProp->children) {
                    if (ch->localName == "Binding") {
                        const std::string bv = EmitDynamicBindingObject(*ch, spec.path, dictVar, out, indent);
                        Indent(out, indent);
                        out << tv << ".ValueBinding = " << bv << ";\n";
                        emittedDynamicValue = true;
                        break;
                    }
                }
            }
            if (!emittedDynamicValue) {
                if (const auto* val = trig.FindAttribute("Value")) {
                    Indent(out, indent);
                    out << tv << ".SetValueBox("
                        << FormatTriggerValueBoxExpr(NormalizeBindingPath(spec.path), val->value, dictVar)
                        << ");\n";
                } else {
                    Indent(out, indent);
                    out << tv << ".SetValueBox(DependencyObject.BoxBool(true));\n";
                }
            }
        } else if (trig.localName == "PropertyChangedTrigger") {
            Indent(out, indent);
            out << "var " << tv << " = new PropertyChangedTrigger();\n";
            if (const auto* ba = trig.FindAttribute("Binding");
                ba != nullptr && LooksLikeMarkupExtension(ba->value)) {
                const auto ext = ParseMarkupExtension(ba->value);
                ParsedBindingSpec spec;
                if (TryExtractBindingSpec(ext, spec)) {
                    const std::string bv = EmitBindingObjectFromSpec(spec, "", dictVar, out, indent);
                    Indent(out, indent);
                    out << tv << ".Binding = " << bv << ";\n";
                }
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
                                std::string lhs, elName, op = "Equal", rhs;
                                if (const auto* la = ccNode->FindAttribute("LeftOperand")) {
                                    if (LooksLikeMarkupExtension(la->value)) {
                                        const auto ext = ParseMarkupExtension(la->value);
                                        ParsedBindingSpec sp;
                                        if (TryExtractBindingSpec(ext, sp)) {
                                            lhs = NormalizeBindingPath(sp.path);
                                            elName = sp.elementName;
                                        } else {
                                            lhs = la->value;
                                        }
                                    } else {
                                        lhs = la->value;
                                    }
                                }
                                if (const auto* oa = ccNode->FindAttribute("Operator")) op = oa->value;
                                if (const auto* ra = ccNode->FindAttribute("RightOperand")) rhs = ra->value;
                                const std::string rightExpr =
                                    FormatTriggerValueBoxExpr(lhs, rhs, dictVar);
                                if (elName.empty()) {
                                    Indent(out, indent);
                                    out << tv << ".AddCondition(new ComparisonCondition("
                                        << EscapeRsString(lhs) << ", "
                                        << EscapeRsString(op) << ", "
                                        << rightExpr << "));\n";
                                } else {
                                    const std::string ccVar = FreshVar("cc");
                                    Indent(out, indent);
                                    out << "var " << ccVar << " = new ComparisonCondition("
                                        << EscapeRsString(lhs) << ", "
                                        << EscapeRsString(op) << ", "
                                        << rightExpr << ");\n";
                                    Indent(out, indent);
                                    out << ccVar << ".ElementName = " << EscapeRsString(elName) << ";\n";
                                    Indent(out, indent);
                                    out << tv << ".AddCondition(" << ccVar << ");\n";
                                }
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
                    const std::string efVar = EmitEffectObject(*beh->children.front(), targetVar, out, indent);
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
                const std::string av = EmitTriggerActionNode(*act, dictVar, out, indent);
                if (!av.empty()) {
                    Indent(out, indent);
                    out << etVar << ".AddAction(" << av << ");\n";
                }
            }

            Indent(out, indent);
            out << "Interaction.AddTrigger(" << targetVar << ", " << etVar << ");\n";
        }
    }

    [[nodiscard]] bool StoryboardHasAnimatedTimelineImpl(
        const XamlNode& sbNode,
        std::unordered_set<std::string>& visitedSbKeys) const {
        if (ParseTimelineAutoReverse(sbNode) || ParseTimelineRepeatCount(sbNode) != 1) {
            return true;
        }
        for (const auto& tl : sbNode.children) {
            if (!tl || ShouldIgnoreXmlElement(*tl)) continue;
            if (tl->localName == "StaticResource") {
                const std::string refKey = ExtractStaticResourceElementKey(*tl);
                if (!refKey.empty()) {
                    if (const auto sbIt = knownStoryboards_.find(refKey);
                        sbIt != knownStoryboards_.end() && sbIt->second != nullptr &&
                        visitedSbKeys.insert(refKey).second) {
                        const bool subAnim = StoryboardHasAnimatedTimelineImpl(*sbIt->second, visitedSbKeys);
                        visitedSbKeys.erase(refKey);
                        if (subAnim) return true;
                    }
                }
                continue;
            }
            if (ParseTimelineAutoReverse(*tl) || ParseTimelineRepeatCount(*tl) != 1) {
                return true;
            }
            if (tl->children.size() > 1u) {
                return true;
            }
            if (const auto* d = tl->FindAttribute("Duration")) {
                if (ParseDurationSeconds(d->value) > 0.001f) {
                    return true;
                }
            }
            if (const auto* bt = tl->FindAttribute("BeginTime")) {
                if (ParseDurationSeconds(bt->value) > 0.001f) {
                    return true;
                }
            }
            if (!tl->children.empty()) {
                if (const auto* kt = tl->children.back()->FindAttribute("KeyTime")) {
                    if (ParseDurationSeconds(kt->value) > 0.001f) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool StoryboardHasAnimatedTimeline(const XamlNode& sbNode) const {
        std::unordered_set<std::string> visitedSbKeys;
        const std::string ownKey = sbNode.GetKey();
        if (!ownKey.empty()) {
            visitedSbKeys.insert(ownKey);
        }
        return StoryboardHasAnimatedTimelineImpl(sbNode, visitedSbKeys);
    }

    void EmitVisualStateStoryboardSetters(
        const XamlNode& sbNode,
        std::string_view vsVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        std::unordered_set<std::string> visitedSbKeys;
        const std::string ownKey = sbNode.GetKey();
        if (!ownKey.empty()) {
            visitedSbKeys.insert(ownKey);
        }
        EmitVisualStateStoryboardSettersImpl(sbNode, vsVar, dictVar, out, indent, visitedSbKeys);
    }

    void EmitVisualStateStoryboardSettersImpl(
        const XamlNode& sbNode,
        std::string_view vsVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent,
        std::unordered_set<std::string>& visitedSbKeys) {
        for (const auto& tl : sbNode.children) {
            if (!tl || ShouldIgnoreXmlElement(*tl)) continue;
            if (tl->localName == "StaticResource") {
                const std::string refKey = ExtractStaticResourceElementKey(*tl);
                if (!refKey.empty()) {
                    if (const auto sbIt = knownStoryboards_.find(refKey);
                        sbIt != knownStoryboards_.end() && sbIt->second != nullptr &&
                        visitedSbKeys.insert(refKey).second) {
                        EmitVisualStateStoryboardSettersImpl(
                            *sbIt->second, vsVar, dictVar, out, indent, visitedSbKeys);
                        visitedSbKeys.erase(refKey);
                    }
                }
                continue;
            }
            std::string targetName;
            std::string targetProp;
            if (const auto* tn = tl->FindAttribute("Storyboard.TargetName")) {
                targetName = tn->value;
            } else if (const auto* tn2 = tl->FindAttribute("TargetName")) {
                targetName = tn2->value;
            }
            if (const auto* tp = tl->FindAttribute("Storyboard.TargetProperty")) {
                targetProp = NormalizeTargetPropertyPath(tp->value);
            } else if (const auto* tp2 = tl->FindAttribute("TargetProperty")) {
                targetProp = NormalizeTargetPropertyPath(tp2->value);
            }
            if (targetProp.empty() || targetProp == "Unsupported.GradientStop") continue;

            const bool useInitialAnchor =
                ParseTimelineAutoReverse(*tl) || (ParseTimelineRepeatCount(*tl) != 1);

            if (tl->localName == "DoubleAnimation" ||
                tl->localName == "BounceAnimation" ||
                tl->localName == "ElasticAnimation" ||
                tl->localName == "BackAnimation") {
                const auto* valAttr = useInitialAnchor ? tl->FindAttribute("From") : tl->FindAttribute("To");
                if (!valAttr) valAttr = tl->FindAttribute("To");
                if (valAttr) {
                    float toVal = 0.0f;
                    if (aero::str::TryParseFloat32(valAttr->value, toVal)) {
                        Indent(out, indent);
                        out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                            << ", DependencyObject.BoxFloat(" << FormatFloatLiteral(toVal) << "), "
                            << EscapeRsString(targetName) << ");\n";
                    }
                }
            } else if (tl->localName == "DoubleAnimationUsingKeyFrames" && !tl->children.empty()) {
                const auto& anchorKf = useInitialAnchor ? *tl->children.front() : *tl->children.back();
                if (const auto* va = anchorKf.FindAttribute("Value")) {
                    float val = 0.0f;
                    if (aero::str::TryParseFloat32(va->value, val)) {
                        Indent(out, indent);
                        out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                            << ", DependencyObject.BoxFloat(" << FormatFloatLiteral(val) << "), "
                            << EscapeRsString(targetName) << ");\n";
                    }
                }
            } else if (tl->localName == "ColorAnimation") {
                const auto* colAttr = useInitialAnchor ? tl->FindAttribute("From") : tl->FindAttribute("To");
                if (!colAttr) colAttr = tl->FindAttribute("To");
                if (colAttr) {
                    const std::string hex = ResolveColorHexOrString(colAttr->value);
                    Indent(out, indent);
                    out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                        << ", DependencyObject.BoxString(" << EscapeRsString(hex) << "), "
                        << EscapeRsString(targetName) << ");\n";
                }
            } else if (tl->localName == "ColorAnimationUsingKeyFrames" && !tl->children.empty()) {
                const auto& anchorKf = useInitialAnchor ? *tl->children.front() : *tl->children.back();
                if (const auto* va = anchorKf.FindAttribute("Value")) {
                    const std::string hex = ResolveColorHexOrString(va->value);
                    Indent(out, indent);
                    out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                        << ", DependencyObject.BoxString(" << EscapeRsString(hex) << "), "
                        << EscapeRsString(targetName) << ");\n";
                }
            } else if (tl->localName == "BooleanAnimationUsingKeyFrames" && !tl->children.empty()) {
                const auto& lastKf = *tl->children.back();
                const std::string boxed = FormatDiscreteKeyFrameBoxExpr(lastKf, targetProp, dictVar, true, out, indent);
                Indent(out, indent);
                out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                    << ", " << boxed << ", "
                    << EscapeRsString(targetName) << ");\n";
            } else if (tl->localName == "ObjectAnimationUsingKeyFrames" && !tl->children.empty()) {
                const auto& lastKf = *tl->children.back();
                const std::string boxed = FormatDiscreteKeyFrameBoxExpr(lastKf, targetProp, dictVar, false, out, indent);
                Indent(out, indent);
                out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp)
                    << ", " << boxed << ", "
                    << EscapeRsString(targetName) << ");\n";
            } else if (tl->localName == "PointAnimationUsingKeyFrames" && !tl->children.empty()) {
                const auto& anchorKf = useInitialAnchor ? *tl->children.front() : *tl->children.back();
                if (const auto* va = anchorKf.FindAttribute("Value")) {
                    std::vector<float> pts;
                    if (TryParseFloatList(va->value, pts) && pts.size() >= 2u) {
                        Indent(out, indent);
                        out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp + ".X")
                            << ", DependencyObject.BoxFloat(" << FormatFloatLiteral(pts[0]) << "), "
                            << EscapeRsString(targetName) << ");\n";
                        Indent(out, indent);
                        out << vsVar << ".AddSetterBox(" << EscapeRsString(targetProp + ".Y")
                            << ", DependencyObject.BoxFloat(" << FormatFloatLiteral(pts[1]) << "), "
                            << EscapeRsString(targetName) << ");\n";
                    }
                }
            }
        }
    }

    void EmitVisualStateGroups(
        const XamlNode& vsgPropNode,
        std::string_view targetVar,
        std::string_view dictVar,
        std::ostringstream& out,
        int indent) {
        const std::string arrVar = FreshVar("vsgList");
        Indent(out, indent);
        out << "var " << arrVar << " = new VisualStateGroup[0];\n";

        for (const auto& grpNode : vsgPropNode.children) {
            if (!grpNode || grpNode->localName != "VisualStateGroup") continue;
            const std::string gVar = FreshVar("vsg");
            Indent(out, indent);
            out << "var " << gVar << " = new VisualStateGroup(" << EscapeRsString(grpNode->GetName()) << ");\n";

            const XamlNode* statesProp = grpNode->FindPropertyElement("VisualStateGroup", "States");
            const auto& stateChildren = (statesProp != nullptr) ? statesProp->children : grpNode->children;
            for (const auto& stNode : stateChildren) {
                if (!stNode || stNode->localName != "VisualState") continue;
                const std::string stName = stNode->GetName();
                if (stName.empty()) continue;
                const std::string vsVar = FreshVar("vs");
                Indent(out, indent);
                out << "var " << vsVar << " = new VisualState(" << EscapeRsString(stName) << ");\n";

                if (const auto* sbAttr = stNode->FindAttribute("Storyboard")) {
                    if (LooksLikeMarkupExtension(sbAttr->value)) {
                        const auto ext = ParseMarkupExtension(sbAttr->value);
                        const std::string rk = ResolveResourceKeyFromExtension(ext);
                        if (!rk.empty()) {
                            Indent(out, indent);
                            out << vsVar << ".StoryboardKey = " << EscapeRsString(rk) << ";\n";
                        }
                    }
                }
                auto emitInlineSb = [&](const XamlNode& sbNode) {
                    EmitVisualStateStoryboardSetters(sbNode, vsVar, dictVar, out, indent);
                    if (StoryboardHasAnimatedTimeline(sbNode)) {
                        const std::string sbVar = EmitStoryboard(sbNode, out, indent, dictVar);
                        Indent(out, indent);
                        out << vsVar << ".Storyboard = " << sbVar << ";\n";
                    }
                };
                if (const auto* sbProp = stNode->FindPropertyElement("VisualState", "Storyboard")) {
                    for (const auto& c : sbProp->children) {
                        if (c && c->localName == "Storyboard") {
                            emitInlineSb(*c);
                        }
                    }
                }
                for (const auto& c : stNode->children) {
                    if (c && c->localName == "Storyboard") {
                        emitInlineSb(*c);
                    }
                }

                Indent(out, indent);
                out << gVar << ".AddState(" << vsVar << ");\n";
            }

            const XamlNode* trProp = grpNode->FindPropertyElement("VisualStateGroup", "Transitions");
            const auto emitTransitionNode = [&](const XamlNode& trNode) {
                const std::string vtVar = FreshVar("vt");
                Indent(out, indent);
                out << "var " << vtVar << " = new VisualTransition();\n";
                if (const auto* fAttr = trNode.FindAttribute("From")) {
                    Indent(out, indent);
                    out << vtVar << ".From = " << EscapeRsString(fAttr->value) << ";\n";
                }
                if (const auto* tAttr = trNode.FindAttribute("To")) {
                    Indent(out, indent);
                    out << vtVar << ".To = " << EscapeRsString(tAttr->value) << ";\n";
                }
                if (const auto* dAttr = trNode.FindAttribute("GeneratedDuration")) {
                    const float durSec = ParseDurationSeconds(dAttr->value);
                    Indent(out, indent);
                    out << vtVar << ".GeneratedDurationSec = " << FormatFloatLiteral(durSec) << ";\n";
                }
                int easeKind = 0;
                int easeMode = 0;
                float easeP0 = 0.0f;
                float easeP1 = 0.0f;
                int easeMask = 0;
                bool hasEase = false;
                if (const auto* eAttr = trNode.FindAttribute("GeneratedEasingFunction")) {
                    easeKind = MapEasingKind(eAttr->value);
                    hasEase = true;
                }
                const XamlNode* eProp = trNode.FindPropertyElement("VisualTransition", "GeneratedEasingFunction");
                if (eProp != nullptr && !eProp->children.empty() && eProp->children.front() != nullptr) {
                    const auto& eNode = *eProp->children.front();
                    easeKind = MapEasingKind(eNode.localName);
                    if (const auto* em = eNode.FindAttribute("EasingMode")) {
                        easeMode = MapEasingMode(em->value);
                    }
                    ExtractEasingNodeParams(eNode, easeP0, easeP1, easeMask);
                    hasEase = true;
                }
                if (hasEase) {
                    Indent(out, indent);
                    out << vtVar << ".SetEasing(" << easeKind << ", " << easeMode << ");\n";
                    if (easeMask != 0 || easeP0 != 0.0f || easeP1 != 0.0f) {
                        Indent(out, indent);
                        out << vtVar << ".SetEasingParams("
                            << FormatFloatLiteral(easeP0) << ", " << FormatFloatLiteral(easeP1)
                            << ", " << (easeMask != 0 ? easeMask : 3) << ");\n";
                    }
                }
                Indent(out, indent);
                out << gVar << ".AddTransition(" << vtVar << ");\n";
            };
            if (trProp != nullptr) {
                for (const auto& trNode : trProp->children) {
                    if (trNode && trNode->localName == "VisualTransition") {
                        emitTransitionNode(*trNode);
                    }
                }
            }
            for (const auto& trNode : grpNode->children) {
                if (trNode && trNode->localName == "VisualTransition") {
                    emitTransitionNode(*trNode);
                }
            }

            Indent(out, indent);
            out << arrVar << ".Push(" << gVar << ");\n";
        }

        Indent(out, indent);
        out << "VisualStateManager.SetVisualStateGroups(" << targetVar << ", " << arrVar << ");\n";
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
