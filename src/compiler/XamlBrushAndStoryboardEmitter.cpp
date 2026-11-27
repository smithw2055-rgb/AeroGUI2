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

void XamlEmitter::EmitGradientBrushCommonProps(
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
    const XamlNode* relTrProp = brushNode.FindPropertyElement(brushNode.localName, "RelativeTransform");
    if (relTrProp == nullptr) {
        relTrProp = brushNode.FindPropertyElement("Brush", "RelativeTransform");
    }
    if (relTrProp != nullptr && !relTrProp->children.empty() &&
        relTrProp->children.front()->localName == "TranslateTransform") {
        const XamlNode& tr = *relTrProp->children.front();
        if (const auto* xa = tr.FindAttribute("X")) {
            float xVal = 0.0f;
            if (!LooksLikeMarkupExtension(xa->value) && aero::str::TryParseFloat32(xa->value, xVal)) {
                Indent(out, indent);
                out << bVar << ".RelativeTranslateX = " << FormatFloatLiteral(xVal) << ";\n";
            }
        }
        if (const auto* ya = tr.FindAttribute("Y")) {
            float yVal = 0.0f;
            if (!LooksLikeMarkupExtension(ya->value) && aero::str::TryParseFloat32(ya->value, yVal)) {
                Indent(out, indent);
                out << bVar << ".RelativeTranslateY = " << FormatFloatLiteral(yVal) << ";\n";
            }
        }
    }
}

[[nodiscard]] const XamlNode* XamlEmitter::FindBrushShaderElement(const XamlNode& brushNode) const {
    if (const auto* shProp = brushNode.FindPropertyElement(brushNode.localName, "Shader")) {
        return shProp;
    }
    return brushNode.FindPropertyElement("Brush", "Shader");
}

void XamlEmitter::EmitImageBrushPlacementAttrs(
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

[[nodiscard]] std::string XamlEmitter::EmitShaderFallbackBrush(
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

[[nodiscard]] std::string XamlEmitter::EmitBrushObject(
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

    if (brushNode.localName == "VisualBrush") {
        const std::string bVar = FreshVar("vbrush");
        Indent(out, indent);
        out << "var " << bVar << " = new VisualBrush();\n";
        EmitImageBrushPlacementAttrs(brushNode, bVar, out, indent);
        if (const auto* axAttr = brushNode.FindAttribute("AlignmentX")) {
            const std::string_view ax = aero::str::Trim(axAttr->value);
            if (!ax.empty()) {
                Indent(out, indent);
                out << bVar << ".AlignmentX = " << EscapeRsString(ax) << ";\n";
            }
        }
        if (const auto* ayAttr = brushNode.FindAttribute("AlignmentY")) {
            const std::string_view ay = aero::str::Trim(ayAttr->value);
            if (!ay.empty()) {
                Indent(out, indent);
                out << bVar << ".AlignmentY = " << EscapeRsString(ay) << ";\n";
            }
        }
        if (const auto* opAttr = brushNode.FindAttribute("Opacity")) {
            float op = 1.0f;
            if (aero::str::TryParseFloat32(opAttr->value, op)) {
                Indent(out, indent);
                out << bVar << ".Opacity = " << FormatFloatLiteral(op) << ";\n";
            }
        }
        if (const auto* visAttr = brushNode.FindAttribute("Visual");
            visAttr != nullptr && LooksLikeMarkupExtension(visAttr->value)) {
            const auto ext = ParseMarkupExtension(visAttr->value);
            ParsedBindingSpec bSpec;
            if (TryExtractBindingSpec(ext, bSpec) && !bSpec.elementName.empty()) {
                Indent(out, indent);
                out << bVar << ".VisualElementName = " << EscapeRsString(bSpec.elementName) << ";\n";
            } else if (ext.kind == MarkupExtensionKind::StaticResource ||
                       ext.kind == MarkupExtensionKind::DynamicResource) {
                const std::string rk = ResolveResourceKeyFromExtension(ext);
                if (!rk.empty()) {
                    Indent(out, indent);
                    out << "var __vis_" << bVar << " = " << LookupResourceExpr(rk, dictVar) << ";\n";
                    Indent(out, indent);
                    out << "if (__vis_" << bVar << " is UIElement __ve_" << bVar << ") { "
                        << bVar << ".Visual = __ve_" << bVar << "; }\n";
                }
            }
        }
        const XamlNode* visProp = brushNode.FindPropertyElement("VisualBrush", "Visual");
        const auto& visChildren = (visProp != nullptr) ? visProp->children : brushNode.children;
        for (const auto& vc : visChildren) {
            if (!vc || ShouldIgnoreXmlElement(*vc)) continue;
            const std::string childVar = EmitConstructedElement(
                *vc, {}, dictVar, opts_.xamlPath, out, indent);
            if (!childVar.empty()) {
                Indent(out, indent);
                out << bVar << ".Visual = " << childVar << ";\n";
            }
            break;
        }
        return bVar;
    }

    return "new SolidColorBrush(\"#00000000\")";
}

void XamlEmitter::EmitGradientStops(
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

[[nodiscard]] std::string XamlEmitter::EmitTransformObject(
    const XamlNode& node, 
    std::ostringstream& out, 
    int indent, 
    std::string_view hostUiVar, 
    std::string_view xDataType, 
    std::string_view dictVar, 
    std::string_view transformPropPrefix) {
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

[[nodiscard]] std::string XamlEmitter::EmitEffectObject(
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

[[nodiscard]] std::string XamlEmitter::FormatDiscreteKeyFrameBoxExpr(
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
                if (IsXamlBrushElementTag(ch->localName)) {
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

void XamlEmitter::ExtractEasingNodeParams(const XamlNode& easeNode,  float& outP0,  float& outP1,  int& outMask) {
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

void XamlEmitter::EmitScalarKeyFrameSegment(
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
    float easeP0, 
    float easeP1, 
    int easeMask) {
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

[[nodiscard]] std::string XamlEmitter::ExtractStaticResourceElementKey(const XamlNode& node) {
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

void XamlEmitter::EmitStoryboardTimelines(
    const XamlNode& sbNode, 
    std::string_view sbVar, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent, 
    std::unordered_set<std::string>& visitedSbKeys, 
    bool sbRepeatForever) {
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

[[nodiscard]] std::string XamlEmitter::EmitStoryboard(
    const XamlNode& sbNode, 
    std::ostringstream& out, 
    int indent, 
    std::string_view dictVar) {
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

void XamlEmitter::EmitProjection3D(
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

[[nodiscard]] std::string XamlEmitter::EmitCompositeTransform3DObject(
    const XamlNode& node, 
    std::ostringstream& out, 
    int indent, 
    std::string_view hostTargetVar) {
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

void XamlEmitter::EmitTransform3D(
    const XamlNode& node, 
    std::string_view targetVar, 
    std::ostringstream& out, 
    int indent) {
    const std::string tv = EmitCompositeTransform3DObject(node, out, indent, targetVar);
    Indent(out, indent);
    out << "Element.SetTransform3D(" << targetVar << ", " << tv << ");\n";
}

[[nodiscard]] bool XamlEmitter::StoryboardHasAnimatedTimelineImpl(
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

[[nodiscard]] bool XamlEmitter::StoryboardHasAnimatedTimeline(const XamlNode& sbNode) const {
    std::unordered_set<std::string> visitedSbKeys;
    const std::string ownKey = sbNode.GetKey();
    if (!ownKey.empty()) {
        visitedSbKeys.insert(ownKey);
    }
    return StoryboardHasAnimatedTimelineImpl(sbNode, visitedSbKeys);
}

void XamlEmitter::EmitVisualStateStoryboardSetters(
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

void XamlEmitter::EmitVisualStateStoryboardSettersImpl(
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

void XamlEmitter::EmitVisualStateGroups(
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

} // namespace aero::xaml_detail
