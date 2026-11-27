#include "XamlEmitterInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace aero::xaml_detail {

[[nodiscard]] bool IsDesignOrMarkupCompatPrefix(std::string_view prefix,  std::string_view nsUri) {
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

[[nodiscard]] bool TryParseFloatList(std::string_view raw,  std::vector<float>& outValues) {
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

[[nodiscard]] std::string FormatThickness4Args(std::string_view raw) {
    std::vector<float> vals;
    if (!TryParseFloatList(raw, vals)) {
        return "0.0f, 0.0f, 0.0f, 0.0f";
    }
    if (vals.size() == 1) {
        const std::string v = FormatFloatLiteral(vals[0]);
        return v + ", " + v + ", " + v + ", " + v;
    }
    if (vals.size() == 2) {
        const std::string h = FormatFloatLiteral(vals[0]);
        const std::string v = FormatFloatLiteral(vals[1]);
        return h + ", " + v + ", " + h + ", " + v;
    }
    if (vals.size() >= 4) {
        return FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ", " +
               FormatFloatLiteral(vals[2]) + ", " +
               FormatFloatLiteral(vals[3]);
    }
    return "0.0f, 0.0f, 0.0f, 0.0f";
}

[[nodiscard]] std::string FormatCornerRadius4Args(std::string_view raw) {
    std::vector<float> vals;
    if (!TryParseFloatList(raw, vals)) {
        return "0.0f, 0.0f, 0.0f, 0.0f";
    }
    if (vals.size() == 1) {
        const std::string v = FormatFloatLiteral(vals[0]);
        return v + ", " + v + ", " + v + ", " + v;
    }
    if (vals.size() >= 4) {
        return FormatFloatLiteral(vals[0]) + ", " +
               FormatFloatLiteral(vals[1]) + ", " +
               FormatFloatLiteral(vals[2]) + ", " +
               FormatFloatLiteral(vals[3]);
    }
    const std::string v = FormatFloatLiteral(vals[0]);
    return v + ", " + v + ", " + v + ", " + v;
}

[[nodiscard]] std::string FormatOrigin2Args(std::string_view raw) {
    std::vector<float> pts;
    if (TryParseFloatList(raw, pts) && pts.size() >= 2) {
        return FormatFloatLiteral(pts[0]) + ", " + FormatFloatLiteral(pts[1]);
    }
    return "0.5f, 0.5f";
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
    return ParseXamlDurationSeconds(raw);
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
    return NormalizeAnimationTargetPropertyPath(raw);
}

[[nodiscard]] int MapEasingKind(std::string_view name) {
    return MapXamlEasingKind(name);
}

[[nodiscard]] int MapEasingMode(std::string_view mode) {
    return MapXamlEasingMode(mode);
}

[[nodiscard]] int MapBindingMode(std::string_view mode) {
    return MapXamlBindingMode(mode);
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

[[nodiscard]] std::string SanitizeIdentifier(std::string_view raw,  std::string_view fallback) {
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
    return LookupXamlEnumType(propName);
}

[[nodiscard]] bool IsThicknessProperty(std::string_view prop) {
    return IsXamlThicknessProperty(prop);
}

[[nodiscard]] bool IsBrushProperty(std::string_view prop) {
    return IsXamlBrushProperty(prop);
}

[[nodiscard]] bool IsEventAttribute(std::string_view prop) {
    return IsXamlEventAttribute(prop);
}

[[nodiscard]] bool IsContentControlType(std::string_view typeName) {
    return IsXamlContentControlType(typeName);
}

[[nodiscard]] bool IsItemsControlType(std::string_view typeName) {
    return IsXamlItemsControlType(typeName);
}

[[nodiscard]] bool IsPanelType(std::string_view typeName) {
    return IsXamlPanelType(typeName);
}

[[nodiscard]] bool IsShapeType(std::string_view typeName) {
    return IsXamlShapeType(typeName);
}

[[nodiscard]] bool IsControlType(std::string_view typeName) {
    return IsXamlControlType(typeName);
}

[[nodiscard]] bool IsNonUIElementType(std::string_view typeName) {
    return IsXamlNonUIElementType(typeName);
}

[[nodiscard]] bool IsKnownUIElementType(std::string_view typeName) {
    return IsXamlKnownUIElementType(typeName);
}

[[nodiscard]] bool IsDirectUIPropertyFor(std::string_view targetType,  std::string_view prop) {
    return IsXamlDirectUIPropertyFor(targetType, prop);
}

[[nodiscard]] bool IsFloatPropertyFor(
    std::string_view targetType, 
    std::string_view prop, 
    std::string_view rawVal) {
    return IsXamlFloatPropertyFor(targetType, prop, rawVal);
}

[[nodiscard]] bool IsIntPropertyFor(
    std::string_view targetType, 
    std::string_view prop, 
    std::string_view rawVal) {
    return IsXamlIntPropertyFor(targetType, prop, rawVal);
}

[[nodiscard]] bool IsBoolPropertyFor(
    std::string_view targetType, 
    std::string_view prop, 
    std::string_view rawVal) {
    return IsXamlBoolPropertyFor(targetType, prop, rawVal);
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

[[nodiscard]] bool CanEmitLazyToolTipNode(const XamlNode& node,  bool isRoot) noexcept {
    if (!node.GetName().empty()) {
        return false;
    }
    if (IsPrunableExternalResourceTag(node.localName)) {
        return false;
    }
    if (isRoot &&
        (IsXamlBrushElementTag(node.localName) ||
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
    ExpandXamlControlHierarchy(usedTypes);
}

[[nodiscard]] XamlEmitResult XamlEmitter::Run(std::string_view xamlContent) {
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

    if (opts_.forceStandaloneClass && !opts_.classNameOverride.empty()) {
        xClass.clear();
        moduleName_ = !opts_.defaultModuleName.empty()
            ? opts_.defaultModuleName
            : "__AeroCompiledCatalog";
        shortClassName_ = opts_.classNameOverride;
        rootClassName_ = moduleName_ + "." + shortClassName_;
    } else if (!xClass.empty()) {
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

void XamlEmitter::AddError(std::string msg) {
    hasErrors_ = true;
    diagnostics_.push_back(std::move(msg));
}

void XamlEmitter::AddWarning(std::string_view msg) {
    std::string formatted = aero::str::StartsWith(msg, "[Warning]")
        ? std::string(msg)
        : ("[Warning] " + std::string(msg));
    if (emittedWarnings_.insert(formatted).second) {
        diagnostics_.push_back(std::move(formatted));
    }
}

void XamlEmitter::MaybeImportClrNamespace(std::string_view ns) {
    if (ns.empty() || ns == moduleName_ || IsIgnoredExternalClrNamespace(ns)) {
        return;
    }
    extraImports_.insert(std::string(ns));
}

void XamlEmitter::MaybeImportPrefixedType(std::string_view raw) {
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

[[nodiscard]] std::string XamlEmitter::FreshVar(std::string_view prefix) {
    return std::string(prefix) + "_" + std::to_string(++nextId_);
}

[[nodiscard]] std::string XamlEmitter::MakeGeneratedRsPath(std::string_view xamlPath) {
    std::string p(xamlPath);
    if (aero::str::EndsWith(p, ".xaml")) {
        p.resize(p.size() - 5);
    }
    return p + ".g.rs";
}

[[nodiscard]] std::string XamlEmitter::DeriveModuleNameFromPath(std::string_view xamlPath) {
    const std::string stem = std::filesystem::path(xamlPath).stem().string();
    return "Aero.Generated." + SanitizeIdentifier(stem, "View");
}

void XamlEmitter::Indent(std::ostringstream& out,  int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "    ";
    }
}

void XamlEmitter::RegisterNamedField(const std::string& name,  const std::string& typeName) {
    if (!inPartialClass_ || name.empty()) {
        return;
    }
    if (seenFieldNames_.insert(name).second) {
        namedFields_.emplace_back(name, typeName);
    }
}

[[nodiscard]] std::string XamlEmitter::ResolveElementType(const XamlNode& node) {
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

[[nodiscard]] std::string XamlEmitter::ResolveColorHexOrString(std::string_view raw) const {
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

void XamlEmitter::EmitApplicationRoot(
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
    if (EmitResourcesPropertyElement(root, baseClass, "this", currentXamlPath, initBody, 2)) {
        Indent(initBody, 2);
        initBody << "ResourceDictionary.PopAmbient(this.Resources);\n";
    }

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

void XamlEmitter::EmitPartialViewRoot(
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

void XamlEmitter::EmitStandaloneViewFactory(
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

[[nodiscard]] std::string XamlEmitter::EmitStyle(
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

std::string XamlEmitter::NormalizeSetterPropertyName(std::string_view rawProp) {
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

[[nodiscard]] bool XamlEmitter::IsAttachedBindableProperty(std::string_view propKey) {
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

[[nodiscard]] std::string XamlEmitter::NormalizeBindingPath(std::string_view rawPath) {
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

[[nodiscard]] std::string XamlEmitter::BoxSetterValueExpr(
    std::string_view propName, 
    std::string_view typedExpr, 
    std::string_view targetType) const {
    if (typedExpr == "null") {
        return "null";
    }
    if (typedExpr == "ParseUtil.NaN()") {
        return "DependencyObject.BoxFloat(ParseUtil.NaN())";
    }
    if (aero::str::StartsWith(typedExpr, "GridLength.Parse(")) {
        return std::string(typedExpr);
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

void XamlEmitter::EmitSetterInto(
    const XamlNode& setterNode, 
    std::string_view targetObjVar, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::string_view styleTargetType) {
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
            if (IsXamlBrushElementTag(c->localName)) {
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
        aero::str::StartsWith(valExpr, "GridLength.Parse(") ||
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

void XamlEmitter::EmitTriggerEnterExitActions(
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

[[nodiscard]] std::string XamlEmitter::FormatTriggerValueBoxExpr(
    std::string_view propOrPath, 
    std::string_view rawVal, 
    std::string_view dictVar) const {
    if (LooksLikeMarkupExtension(rawVal) || LookupEnumType(propOrPath) != nullptr) {
        return BoxSetterValueExpr(propOrPath, FormatTypedValueLiteral(propOrPath, rawVal, dictVar));
    }
    return BoxSetterValueExpr(propOrPath, FormatGenericScalarLiteral(rawVal));
}

void XamlEmitter::EmitTriggerCollectionInto(
    const XamlNode& trigProp, 
    std::string_view ownerObjVar, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::string_view styleTargetType) {
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

[[nodiscard]] std::string XamlEmitter::EmitControlTemplate(
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
    if (EmitResourcesPropertyElement(tplNode, "ControlTemplate", tplVar, opts_.xamlPath, out, indent)) {
        Indent(out, indent);
        out << "ResourceDictionary.PopAmbient(" << tplVar << ".Resources);\n";
    }
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

[[nodiscard]] std::string XamlEmitter::EmitItemsPanelTemplate(
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

[[nodiscard]] std::string XamlEmitter::EmitDataTemplate(
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
        if (EmitResourcesPropertyElement(dtNode, dtNode.localName, dtVar, opts_.xamlPath, out, indent)) {
            Indent(out, indent);
            out << "ResourceDictionary.PopAmbient(" << dtVar << ".Resources);\n";
        }
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

[[nodiscard]] bool XamlEmitter::IsComplexGeometryPointPath(std::string_view rawProp) {
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

[[nodiscard]] std::string XamlEmitter::LookupResourceExpr(
    std::string_view key, 
    std::string_view dictVar) const {
    if (inPartialClass_) {
        return "ResourceDictionary.FindScopedResource(this, " + EscapeRsString(key) + ")";
    }
    if (!currentTplParentVar_.empty()) {
        return currentTplParentVar_ + ".FindResource(" + EscapeRsString(key) + ")";
    }
    if (!dictVar.empty()) {
        return "ResourceDictionary.FindScopedResource(" + std::string(dictVar) + ", " + EscapeRsString(key) + ")";
    }
    return "ResourceDictionary.FindAmbientOrGlobal(" + EscapeRsString(key) + ")";
}

[[nodiscard]] std::string XamlEmitter::FormatGenericScalarLiteral(std::string_view val) const {
    const std::string_view s = aero::str::Trim(val);
    if (aero::str::EqualsIgnoreCase(s, "True")) return "true";
    if (aero::str::EqualsIgnoreCase(s, "False")) return "false";
    float f = 0.0f;
    if (aero::str::TryParseFloat32(s, f)) {
        return FormatFloatLiteral(f);
    }
    return EscapeRsString(val);
}

[[nodiscard]] std::string XamlEmitter::FormatTypedValueLiteral(
    std::string_view propName, 
    std::string_view rawVal, 
    std::string_view dictVar, 
    std::string_view targetType) const {
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
                    return "ItemsPanelTemplate.GetFactoryOrNull(" + LookupResourceExpr(key, dictVar) + ")";
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
    if ((propName == "Width" || propName == "Height") &&
        rawVal.find('*') != std::string_view::npos) {
        return "GridLength.Parse(" + EscapeRsString(aero::str::Trim(rawVal)) + ")";
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

[[nodiscard]] std::string XamlEmitter::EmitConverterNode(
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

void XamlEmitter::TryParseBindingNode(
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

[[nodiscard]] std::string XamlEmitter::EmitBindingObjectFromSpec(
    const ParsedBindingSpec& subSpec, 
    std::string_view propName, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::string_view subConv, 
    std::string_view sourceResKey) {
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

[[nodiscard]] std::string XamlEmitter::EmitDynamicBindingObject(
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

[[nodiscard]] std::string XamlEmitter::EmitMultiBindingObject(
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

void XamlEmitter::EmitMultiBindingNode(
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

void XamlEmitter::EmitPropertyAttributeOrBinding(
    std::string_view targetVar, 
    std::string_view targetType, 
    std::string_view propName, 
    std::string_view rawVal, 
    std::string_view xDataType, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::string_view explicitBindSourceVar, 
    bool isUiElementTarget) {
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
    auto emitMarkLocal = [&](std::string_view key) {
        if (!isUiElementTarget || key.empty()) return;
        std::string_view k = key;
        if (k == "TextElement.Foreground" || k == "TextBlock.Foreground") k = "Foreground";
        else if (k == "TextElement.FontSize" || k == "TextBlock.FontSize") k = "FontSize";
        else if (k == "TextElement.FontFamily" || k == "TextBlock.FontFamily") k = "FontFamily";
        else if (k == "TextElement.FontWeight" || k == "TextBlock.FontWeight") k = "FontWeight";
        Indent(out, indent);
        out << targetVar << ".MarkLocalProperty(" << EscapeRsString(k) << ");\n";
    };
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
                emitMarkLocal(propName);
                EmitBindingSpec(
                    targetVar, targetType, propName, spec, xDataType, out, indent, explicitBindSourceVar);
            }
            return;
        } else if (ext.kind == MarkupExtensionKind::XNull) {
            if (propName == "Template") {
                emitMarkLocal("Template");
            }
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
                if (ext.kind == MarkupExtensionKind::StaticResource &&
                    propName != "Style" && propName != "ItemContainerStyle" && propName != "Storyboard") {
                    emitMarkLocal(propName);
                }
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
                    out << targetVar << ".ItemsPanel = ItemsPanelTemplate.GetFactoryOrNull(" << lookup << ");\n";
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

    // Events (`Click="Handler"`)
    if (IsEventAttribute(propName)) {
        if (inPartialClass_) {
            Indent(out, indent);
            out << targetVar << "." << propName << " += this." << aero::str::Trim(rawVal) << ";\n";
        }
        return;
    }

    // Skip WPF/Noesis Width="Auto" / Height="Auto" on non-Grid-track UIElements
    if ((propName == "Width" || propName == "Height") &&
        aero::str::EqualsIgnoreCase(aero::str::Trim(rawVal), "Auto")) {
        return;
    }

    if (!(propName == "Title" && targetType == "Window") &&
        targetType != "BoxShadow" &&
        !(targetType == "Grid" && (propName == "RowDefinitions" || propName == "ColumnDefinitions"))) {
        emitMarkLocal(propName);
    }

    // Attached & Grid shorthand property checks
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

    if (isUiElementTarget && propName == "Margin") {
        Indent(out, indent);
        out << targetVar << ".SetMargin4(" << FormatThickness4Args(rawVal) << ");\n";
        return;
    }
    if (isUiElementTarget && propName == "Padding") {
        Indent(out, indent);
        out << targetVar << ".SetPadding4(" << FormatThickness4Args(rawVal) << ");\n";
        return;
    }
    if (isUiElementTarget && propName == "BorderThickness") {
        Indent(out, indent);
        out << targetVar << ".SetBorderThickness4(" << FormatThickness4Args(rawVal) << ");\n";
        return;
    }
    if (isUiElementTarget && propName == "CornerRadius") {
        Indent(out, indent);
        out << targetVar << ".SetCornerRadius4(" << FormatCornerRadius4Args(rawVal) << ");\n";
        return;
    }
    if (isUiElementTarget && propName == "RenderTransformOrigin") {
        Indent(out, indent);
        out << targetVar << ".SetRenderTransformOrigin2(" << FormatOrigin2Args(rawVal) << ");\n";
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

void XamlEmitter::EmitSiblingLanguageDictionaries(std::ostringstream& out,  int indent) {
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

void XamlEmitter::EmitBindingSpec(
    std::string_view targetVar, 
    std::string_view targetType, 
    std::string_view propName, 
    const ParsedBindingSpec& spec, 
    std::string_view xDataType, 
    std::ostringstream& out, 
    int indent, 
    std::string_view explicitSourceVar, 
    std::string_view inlineConvExpr) {
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
        modeInt = ResolveDefaultBindingMode(targetType, propName);
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
    if (!spec.sourceResourceKey.empty()) {
        srcExpr = LookupResourceExpr(spec.sourceResourceKey, {});
    }
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

void XamlEmitter::EmitSingleGridTrackDef(
    std::string_view targetVar, 
    std::string_view method, 
    std::string_view rawLen, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::string_view defName) {
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

[[nodiscard]] std::string XamlEmitter::EmitConstructedElement(
    const XamlNode& node, 
    std::string_view xDataType, 
    std::string_view dictVar, 
    std::string_view currentXamlPath, 
    std::ostringstream& out, 
    int indent, 
    std::string_view explicitBindSourceVar) {
    const std::string typeName = ResolveElementType(node);
    const std::string varName = FreshVar("el");
    Indent(out, indent);
    out << "var " << varName << " = new " << typeName << "();\n";

    EmitElementBody(
        node, varName, typeName, typeName, xDataType, dictVar, currentXamlPath, out, indent,
        explicitBindSourceVar);
    return varName;
}

void XamlEmitter::EmitElementBody(
    const XamlNode& node, 
    std::string_view targetVar, 
    std::string_view xmlLocalName, 
    std::string_view resolvedTypeName, 
    std::string_view xDataType, 
    std::string_view dictVar, 
    std::string_view currentXamlPath, 
    std::ostringstream& out, 
    int indent, 
    std::string_view explicitBindSourceVar) {
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
    const bool pushedLocalResources = EmitResourcesPropertyElement(
        node, xmlLocalName, targetVar, currentXamlPath, out, indent);
    auto popLocalResourcesIfNeeded = [&]() {
        if (pushedLocalResources) {
            Indent(out, indent);
            out << "ResourceDictionary.PopAmbient(" << targetVar << ".Resources);\n";
        }
    };

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
        if (attr.rawName == "Name" || attr.rawName == "StartupUri" ||
            attr.rawName == "Style" || attr.rawName == "ItemContainerStyle") {
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
            if (!IsNonUIElementType(resolvedTypeName)) {
                Indent(out, indent);
                out << targetVar << ".MarkLocalProperty(" << EscapeRsString(shortProp) << ");\n";
            }
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
            } else if (brushChild.localName == "LinearGradientBrush" ||
                       brushChild.localName == "RadialGradientBrush") {
                const XamlNode* stopContainer = brushChild.FindPropertyElement(brushChild.localName, "GradientStops");
                const auto& stops = (stopContainer != nullptr) ? stopContainer->children : brushChild.children;
                int stopIdx = 0;
                for (const auto& stop : stops) {
                    if (stop->localName != "GradientStop") continue;
                    if (const auto* cAttr = stop->FindAttribute("Color");
                        cAttr != nullptr && LooksLikeMarkupExtension(cAttr->value)) {
                        const auto ext = ParseMarkupExtension(cAttr->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName,
                                shortProp + ".Stop" + std::to_string(stopIdx) + "Color",
                                bSpec, xDataType, out, indent, explicitBindSourceVar);
                        }
                    }
                    if (const auto* oAttr = stop->FindAttribute("Offset");
                        oAttr != nullptr && LooksLikeMarkupExtension(oAttr->value)) {
                        const auto ext = ParseMarkupExtension(oAttr->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName,
                                shortProp + ".Stop" + std::to_string(stopIdx) + "Offset",
                                bSpec, xDataType, out, indent, explicitBindSourceVar);
                        }
                    }
                    ++stopIdx;
                }
                const XamlNode* relTrProp = brushChild.FindPropertyElement(brushChild.localName, "RelativeTransform");
                if (relTrProp == nullptr) {
                    relTrProp = brushChild.FindPropertyElement("Brush", "RelativeTransform");
                }
                if (relTrProp != nullptr && !relTrProp->children.empty() &&
                    relTrProp->children.front()->localName == "TranslateTransform") {
                    const XamlNode& tr = *relTrProp->children.front();
                    if (const auto* xa = tr.FindAttribute("X");
                        xa != nullptr && LooksLikeMarkupExtension(xa->value)) {
                        const auto ext = ParseMarkupExtension(xa->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName, shortProp + ".RelativeTransform.X",
                                bSpec, xDataType, out, indent, explicitBindSourceVar);
                        }
                    }
                    if (const auto* ya = tr.FindAttribute("Y");
                        ya != nullptr && LooksLikeMarkupExtension(ya->value)) {
                        const auto ext = ParseMarkupExtension(ya->value);
                        ParsedBindingSpec bSpec;
                        if (TryExtractBindingSpec(ext, bSpec)) {
                            EmitBindingSpec(
                                targetVar, resolvedTypeName, shortProp + ".RelativeTransform.Y",
                                bSpec, xDataType, out, indent, explicitBindSourceVar);
                        }
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
                if (!IsNonUIElementType(resolvedTypeName)) {
                    Indent(out, indent);
                    out << targetVar << ".MarkLocalProperty(\"RenderTransform\");\n";
                }
                Indent(out, indent);
                out << targetVar << ".RenderTransform = " << trVar << ";\n";
            }
            continue;
        }
        if (shortProp == "LayoutTransform" && !propNode->children.empty()) {
            const std::string trVar = EmitTransformObject(
                *propNode->children.front(), out, indent, targetVar, xDataType, dictVar, "LayoutTransform");
            if (!trVar.empty()) {
                if (!IsNonUIElementType(resolvedTypeName)) {
                    Indent(out, indent);
                    out << targetVar << ".MarkLocalProperty(\"LayoutTransform\");\n";
                }
                Indent(out, indent);
                out << targetVar << ".LayoutTransform = " << trVar << ";\n";
            }
            continue;
        }
        if (shortProp == "Effect" && !propNode->children.empty()) {
            const std::string efVar = EmitEffectObject(
                *propNode->children.front(), targetVar, out, indent);
            if (!efVar.empty()) {
                if (!IsNonUIElementType(resolvedTypeName)) {
                    Indent(out, indent);
                    out << targetVar << ".MarkLocalProperty(\"Effect\");\n";
                }
                Indent(out, indent);
                out << targetVar << ".Effect = " << efVar << ";\n";
            }
            continue;
        }
        if (shortProp == "Template" && !propNode->children.empty()) {
            const std::string tplVar = EmitControlTemplate(*propNode->children.front(), dictVar, out, indent);
            if (!IsNonUIElementType(resolvedTypeName)) {
                Indent(out, indent);
                out << targetVar << ".MarkLocalProperty(\"Template\");\n";
            }
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

    // Apply Style/ItemContainerStyle AFTER local attributes & property elements so MarkLocalProperty protects them.
    for (const auto& attr : node.attributes) {
        if (attr.isXmlns || IsDesignOrMarkupCompatPrefix(attr.prefix, attr.namespaceUri)) {
            continue;
        }
        if (attr.rawName == "Style" || attr.rawName == "ItemContainerStyle") {
            EmitPropertyAttributeOrBinding(
                targetVar, resolvedTypeName, attr.rawName, attr.value,
                xDataType, dictVar, out, indent, explicitBindSourceVar,
                !IsNonUIElementType(resolvedTypeName));
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
        popLocalResourcesIfNeeded();
        return;
    }
    if ((resolvedTypeName == "Path" || resolvedTypeName == "Geometry") &&
        !node.textContent.empty() && node.FindAttribute("Data") == nullptr) {
        Indent(out, indent);
        out << targetVar << ".Data = " << EscapeRsString(aero::str::Trim(node.textContent)) << ";\n";
        popLocalResourcesIfNeeded();
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
    popLocalResourcesIfNeeded();
}

[[nodiscard]] std::string XamlEmitter::EmitRunElement(
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

void XamlEmitter::EmitGridView(
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

void XamlEmitter::EmitRiveInput(
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

void XamlEmitter::EmitInputBindings(
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

[[nodiscard]] std::string XamlEmitter::EmitTriggerActionNode(
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

[[nodiscard]] std::string XamlEmitter::EmitBehaviorTriggerObject(
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

void XamlEmitter::EmitInteractionTriggers(
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

void XamlEmitter::EmitInteractionBehaviors(
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

void XamlEmitter::EmitElementTriggers(
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

} // namespace aero::xaml_detail

namespace aero {

XamlEmitResult EmitXamlToRealScript(
    std::string_view xamlContent,
    const XamlEmitOptions& options) {
    xaml_detail::XamlEmitter emitter(options);
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
