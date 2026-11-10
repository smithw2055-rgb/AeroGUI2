#include "aero/compiler/AeroCompiler.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>

#include "aero/controls/Controls.hpp"
#include "aero/core/TypeClass.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"

namespace aero {

namespace {

std::string_view Trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

float ParseFloat32(std::string_view text, float fallback = 0.0f) noexcept {
    text = Trim(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        text.remove_suffix(1);
    }
    std::string buf(text);
    char* end = nullptr;
    const float val = std::strtof(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return fallback;
    }
    return val;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::pair<GridUnitType, float> ParseGridLength(std::string_view raw) noexcept {
    raw = Trim(raw);
    if (raw.empty() || raw == "*") {
        return {GridUnitType::Star, 1.0f};
    }
    if (EqualsIgnoreCase(raw, "Auto")) {
        return {GridUnitType::Auto, 1.0f};
    }
    if (raw.back() == '*') {
        const std::string_view numPart = Trim(raw.substr(0, raw.size() - 1));
        const float starWeight = numPart.empty() ? 1.0f : ParseFloat32(numPart, 1.0f);
        return {GridUnitType::Star, starWeight};
    }
    return {GridUnitType::Pixel, ParseFloat32(raw, 0.0f)};
}

std::string_view StripXmlPrefix(std::string_view name) noexcept {
    if (const std::size_t colon = name.find(':'); colon != std::string_view::npos) {
        return name.substr(colon + 1);
    }
    return name;
}

bool EndsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string_view ExtractMarkupResourceKey(std::string_view rawExpr) noexcept {
    std::string_view v = Trim(rawExpr);
    if (v.size() > 2u && v.front() == '{' && v.back() == '}') {
        std::string_view inner = Trim(v.substr(1u, v.size() - 2u));
        if (inner.rfind("StaticResource ", 0) == 0) {
            inner = Trim(inner.substr(15u));
        } else if (inner.rfind("DynamicResource ", 0) == 0) {
            inner = Trim(inner.substr(16u));
        } else {
            return {};
        }
        if (inner.rfind("ResourceKey=", 0) == 0) {
            inner = Trim(inner.substr(12u));
        }
        return inner;
    }
    return {};
}

std::string ExtractTargetTypeShortName(std::string_view rawTypeExpr) {
    std::string_view v = Trim(rawTypeExpr);
    if (v.size() > 2u && v.front() == '{' && v.back() == '}') {
        v = Trim(v.substr(1u, v.size() - 2u));
        if (v.rfind("x:Type ", 0) == 0) {
            v = Trim(v.substr(7u));
        } else if (v.rfind("Type ", 0) == 0) {
            v = Trim(v.substr(5u));
        }
        if (v.rfind("TypeName=", 0) == 0) {
            v = Trim(v.substr(9u));
        }
    }
    v = StripXmlPrefix(v);
    if (const std::size_t dot = v.rfind('.'); dot != std::string_view::npos) {
        v = v.substr(dot + 1u);
    }
    return std::string(v);
}

constexpr std::uint16_t kUnmappedPropId = 0xFFFFu;

std::uint16_t ResolveMappedSetterPropertyId(std::string_view rawProp) noexcept {
    rawProp = Trim(rawProp);
    if (rawProp.empty()) {
        return kUnmappedPropId;
    }
    if (rawProp == "Tag") {
        return PropId_Tag;
    }
    const std::uint16_t direct = ResolveBuiltinPropertyId(rawProp);
    if (direct != PropId_Tag) {
        return direct;
    }
    if (const std::size_t dot = rawProp.rfind('.'); dot != std::string_view::npos) {
        const std::string_view leaf = rawProp.substr(dot + 1u);
        if (leaf == "Tag") {
            return PropId_Tag;
        }
        const std::uint16_t stripped = ResolveBuiltinPropertyId(leaf);
        if (stripped != PropId_Tag) {
            return stripped;
        }
    }
    return kUnmappedPropId;
}

std::uint16_t ResolveMappedTriggerConditionId(std::string_view rawProp) noexcept {
    rawProp = Trim(rawProp);
    if (rawProp.empty()) {
        return kUnmappedPropId;
    }
    if (rawProp == "Tag" || EndsWith(rawProp, ".Tag")) {
        return PropId_Tag;
    }
    const std::uint16_t id = ResolveTriggerConditionPropertyId(rawProp);
    return (id == PropId_Tag) ? kUnmappedPropId : id;
}

std::uint16_t ResolveMappedAnimationPropertyId(std::string_view rawPath) noexcept {
    rawPath = Trim(rawPath);
    if (rawPath.empty()) {
        return kUnmappedPropId;
    }
    if (rawPath == "Tag" || EndsWith(rawPath, ".Tag") || EndsWith(rawPath, ".Tag)")) {
        return PropId_Tag;
    }
    const std::uint16_t id = ResolveAnimationTargetPropertyId(rawPath);
    return (id == PropId_Tag) ? kUnmappedPropId : id;
}

bool IsAnimationTimelineTag(std::string_view shortTag) noexcept {
    return shortTag == "DoubleAnimation" ||
           shortTag == "SingleAnimation" ||
           shortTag == "ColorAnimation" ||
           shortTag == "ThicknessAnimation" ||
           shortTag == "PointAnimation" ||
           shortTag == "DoubleAnimationUsingKeyFrames" ||
           shortTag == "SingleAnimationUsingKeyFrames" ||
           shortTag == "ColorAnimationUsingKeyFrames" ||
           shortTag == "ThicknessAnimationUsingKeyFrames" ||
           shortTag == "BooleanAnimationUsingKeyFrames" ||
           shortTag == "ObjectAnimationUsingKeyFrames" ||
           shortTag == "PointAnimationUsingKeyFrames";
}

bool IsAnimationKeyFrameTag(std::string_view shortTag) noexcept {
    return EndsWith(shortTag, "DoubleKeyFrame") ||
           EndsWith(shortTag, "SingleKeyFrame") ||
           EndsWith(shortTag, "ColorKeyFrame") ||
           EndsWith(shortTag, "ThicknessKeyFrame") ||
           EndsWith(shortTag, "BooleanKeyFrame") ||
           EndsWith(shortTag, "ObjectKeyFrame") ||
           EndsWith(shortTag, "PointKeyFrame");
}

bool IsEasingFunctionTag(std::string_view shortTag) noexcept {
    return shortTag.size() > 4u && EndsWith(shortTag, "Ease");
}

std::int32_t ParseRepeatCountAttr(std::string_view rawVal) noexcept {
    rawVal = Trim(rawVal);
    if (rawVal.empty()) {
        return 1;
    }
    if (EqualsIgnoreCase(rawVal, "Forever")) {
        return 0;
    }
    if (rawVal.back() == 'x' || rawVal.back() == 'X') {
        rawVal.remove_suffix(1);
    }
    const float f = ParseFloat32(rawVal, 1.0f);
    return (f <= 0.0f) ? 0 : std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(f)));
}

struct ParsedBindingClause {
    bool isValidBinding = false;
    BindingMode mode = BindingMode::OneWay;
    std::string_view pathPart;
    std::string_view fallbackValue;
    std::string_view elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    ValueConverterKind converter = ValueConverterKind::None;
};

ParsedBindingClause ParseBindingMarkupExpression(std::string_view rawExpr) {
    ParsedBindingClause out{};
    std::string_view v = Trim(rawExpr);
    if (v.size() < 2u || v.front() != '{' || v.back() != '}') {
        return out;
    }
    std::string_view inner = Trim(v.substr(1u, v.size() - 2u));
    if (inner == "Binding" || inner == "Bind") {
        out.isValidBinding = true;
        return out;
    }
    if (inner.rfind("Binding ", 0) == 0) {
        inner = Trim(inner.substr(8u));
    } else if (inner.rfind("Bind ", 0) == 0) {
        inner = Trim(inner.substr(5u));
    } else {
        return out;
    }
    out.isValidBinding = true;

    std::size_t pos = 0u;
    bool firstClause = true;
    while (pos < inner.size()) {
        int braceDepth = 0;
        std::size_t end = pos;
        while (end < inner.size()) {
            if (inner[end] == '{') ++braceDepth;
            else if (inner[end] == '}' && braceDepth > 0) --braceDepth;
            else if (inner[end] == ',' && braceDepth == 0) break;
            ++end;
        }
        const std::string_view clause = Trim(inner.substr(pos, end - pos));
        if (clause.rfind("Path=", 0) == 0) {
            out.pathPart = Trim(clause.substr(5u));
        } else if (clause.rfind("Mode=", 0) == 0) {
            const std::string_view mStr = Trim(clause.substr(5u));
            if (mStr == "TwoWay") out.mode = BindingMode::TwoWay;
            else if (mStr == "OneTime") out.mode = BindingMode::OneTime;
        } else if (clause.rfind("FallbackValue=", 0) == 0) {
            out.fallbackValue = Trim(clause.substr(14u));
        } else if (clause.rfind("ElementName=", 0) == 0) {
            out.elementName = Trim(clause.substr(12u));
        } else if (clause.rfind("Converter=", 0) == 0) {
            const std::string_view convExpr = Trim(clause.substr(10u));
            const std::string_view convKey = ExtractMarkupResourceKey(convExpr);
            out.converter = ParseValueConverterKind(!convKey.empty() ? convKey : convExpr);
        } else if (clause.rfind("RelativeSource=", 0) == 0) {
            std::string_view rsExpr = Trim(clause.substr(15u));
            if (rsExpr.size() > 2u && rsExpr.front() == '{' && rsExpr.back() == '}') {
                rsExpr = Trim(rsExpr.substr(1u, rsExpr.size() - 2u));
            }
            if (rsExpr.find("Self") != std::string_view::npos) {
                out.relativeSelf = true;
            } else if (const auto ancPos = rsExpr.find("AncestorType="); ancPos != std::string_view::npos) {
                std::string_view ancVal = Trim(rsExpr.substr(ancPos + 13u));
                int bd = 0;
                std::size_t ae = 0u;
                while (ae < ancVal.size()) {
                    if (ancVal[ae] == '{') ++bd;
                    else if (ancVal[ae] == '}' && bd > 0) --bd;
                    else if (ancVal[ae] == ',' && bd == 0) break;
                    ++ae;
                }
                out.relativeAncestorType = ExtractTargetTypeShortName(ancVal.substr(0, ae));
            }
        } else if (firstClause && clause.find('=') == std::string_view::npos) {
            out.pathPart = clause;
        }
        firstClause = false;
        if (end >= inner.size()) break;
        pos = end + 1u;
    }

    // Strip parentheses around attached property paths like "(noesis:DragDrop.IsContentEmpty)"
    if (out.pathPart.size() > 2u && out.pathPart.front() == '(' && out.pathPart.back() == ')') {
        out.pathPart = Trim(out.pathPart.substr(1u, out.pathPart.size() - 2u));
    }
    return out;
}

bool FindMatchingClosingTag(
    std::string_view xml,
    std::size_t innerStartPos,
    std::string_view targetShortTag,
    std::size_t& outInnerEnd,
    std::size_t& outAfterCloseTag) noexcept {
    std::size_t cur = innerStartPos;
    int depth = 1;
    while (cur < xml.size()) {
        const std::size_t lt = xml.find('<', cur);
        if (lt == std::string_view::npos) {
            return false;
        }
        if (xml.substr(lt + 1u, 3u) == "!--") {
            const std::size_t endC = xml.find("-->", lt + 4u);
            if (endC == std::string_view::npos) return false;
            cur = endC + 3u;
            continue;
        }
        if (lt + 1u < xml.size() && xml[lt + 1u] == '?') {
            const std::size_t endP = xml.find("?>", lt + 2u);
            if (endP == std::string_view::npos) return false;
            cur = endP + 2u;
            continue;
        }
        const bool isClose = (lt + 1u < xml.size() && xml[lt + 1u] == '/');
        const std::size_t nameStart = isClose ? (lt + 2u) : (lt + 1u);
        std::size_t nameEnd = nameStart;
        while (nameEnd < xml.size() &&
               !std::isspace(static_cast<unsigned char>(xml[nameEnd])) &&
               xml[nameEnd] != '>' && xml[nameEnd] != '/') {
            ++nameEnd;
        }
        const std::string_view st = StripXmlPrefix(xml.substr(nameStart, nameEnd - nameStart));
        // Advance to end of tag '>' respecting quotes
        std::size_t scan = nameEnd;
        bool selfClose = false;
        while (scan < xml.size()) {
            if (xml[scan] == '"' || xml[scan] == '\'') {
                const char q = xml[scan++];
                while (scan < xml.size() && xml[scan] != q) ++scan;
                if (scan < xml.size()) ++scan;
                continue;
            }
            if (xml[scan] == '>') {
                std::size_t prev = scan;
                while (prev > nameEnd && std::isspace(static_cast<unsigned char>(xml[prev - 1u]))) {
                    --prev;
                }
                if (prev > nameEnd && xml[prev - 1u] == '/') {
                    selfClose = true;
                }
                ++scan;
                break;
            }
            ++scan;
        }
        if (st == targetShortTag) {
            if (isClose) {
                --depth;
                if (depth == 0) {
                    outInnerEnd = lt;
                    outAfterCloseTag = scan;
                    return true;
                }
            } else if (!selfClose) {
                ++depth;
            }
        }
        cur = scan;
    }
    return false;
}

enum class XmlScopeKind : std::uint8_t {
    VisualElement = 0,
    TransparentContainer = 1,
    RunInline = 2,
    ResourceContainer = 3,
    KeyedResourceItem = 4,
    BrushPropertyElement = 5,
    TransformPropertyElement = 6,
    TransformChildNode = 7,
    NonVisualSkip = 8,
};

struct XmlScopeEntry {
    XmlScopeKind kind = XmlScopeKind::VisualElement;
    std::string tagName;
    std::string resourceKey;
    std::string inlineStyleKey;
    std::uint16_t targetPropId = PropId_Background;
    bool brushAssigned = false;
    bool isItemTemplateContainer = false;
    Matrix2D accumulatedTransform = Matrix2D::Identity();
    std::string accumulatedText;
};

enum class SetterOwnerKind : std::uint8_t {
    Style = 0,
    Trigger = 1,
    VisualState = 2,
};

enum class StoryboardOwnerKind : std::uint8_t {
    StandaloneResource = 0,
    TriggerEnter = 1,
    TriggerExit = 2,
    VisualState = 3,
    EventTrigger = 4,
    IgnoredEventTrigger = 5,
};

struct ActiveStyleScope {
    AxbStyleDefinition def{};
    std::size_t scopeDepth = 0;
    std::size_t controlTemplateTriggersDepth = 0;
    bool isInlinePropertyElement = false;
};

struct ActiveTriggerScope {
    AxbStyleTrigger trig{};
    std::size_t scopeDepth = 0;
    std::size_t enterActionsDepth = 0;
    std::size_t exitActionsDepth = 0;
    bool isDataTrigger = false;
    bool hasUnsupportedCondition = false;
};

struct ActiveEventTriggerScope {
    AxbEventTrigger et{};
    std::size_t scopeDepth = 0;
    bool recognized = false;
};

struct ActiveVsgScope {
    AxbVisualStateGroup group{};
    std::size_t scopeDepth = 0;
    std::size_t transitionsDepth = 0;
};

struct ActiveVisualStateScope {
    AxbVisualState state{};
    std::size_t scopeDepth = 0;
};

struct ActiveStoryboardScope {
    AxbStoryboard sb{};
    std::string defaultTargetName;
    std::string defaultTargetProperty;
    StoryboardOwnerKind owner = StoryboardOwnerKind::StandaloneResource;
    std::size_t scopeDepth = 0;
};

struct ActiveTimelineScope {
    AxbAnimationTimeline tl{};
    std::size_t scopeDepth = 0;
};

struct ActiveKeyFrameScope {
    AxbKeyFrame kf{};
    std::size_t scopeDepth = 0;
};

struct ActiveSetterScope {
    AxbPropertySetter setter{"", kUnmappedPropId, {}};
    SetterOwnerKind owner = SetterOwnerKind::Style;
    bool hasValue = false;
    std::size_t scopeDepth = 0;
};

} // namespace

// ============================================================================
// AeroCompiler Single-Pass XAML + RealScript Unified Compiler
// ============================================================================

AeroCompiler::AeroCompiler() {
    EnsureControlTypesRegistered();
}

std::uint16_t AeroCompiler::ResolvePropertyId(std::string_view propertyName) noexcept {
    return ResolveBuiltinPropertyId(propertyName);
}

UnifiedValue AeroCompiler::ParseLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue) {
    return ParseBuiltinLiteralPropertyValue(propId, rawValue);
}

bool AeroCompiler::BindPackageAgainstProgram(
    AxbPackage& package,
    const ProgramImage& program,
    std::string_view rootDataTypeName,
    const ExecutionDomainContext* domains) {
    return package.BindAgainstProgram(program, rootDataTypeName, domains);
}

namespace {

void PopulateSceneIrFromPackage(
    const AxbPackage& pkg,
    FrontendSourceFormat format,
    std::string_view sourcePath,
    AeroSceneGraphIR& outIr) {
    outIr = AeroSceneGraphIR{};
    outIr.sourceFormat = format;
    outIr.sourcePath = std::string(sourcePath);
    outIr.className = pkg.className;
    outIr.loweredPackage = pkg;

    std::vector<std::uint32_t> parentStack;
    for (const auto& inst : pkg.instructions) {
        if (inst.opcode == AxbOpcode::BeginElement) {
            IrSceneNode node{};
            node.localElementIdx = static_cast<std::uint32_t>(outIr.nodes.size());
            node.parentElementIdx = parentStack.empty() ? 0xFFFFFFFFu : parentStack.back();
            node.typeName = std::string(pkg.GetString(inst.strIdx0));
            node.name = std::string(pkg.GetString(inst.strIdx1));
            if (!parentStack.empty() && parentStack.back() < outIr.nodes.size()) {
                outIr.nodes[parentStack.back()].childIndices.push_back(node.localElementIdx);
            }
            parentStack.push_back(node.localElementIdx);
            outIr.nodes.push_back(std::move(node));
        } else if (inst.opcode == AxbOpcode::EndElement) {
            if (!parentStack.empty()) {
                parentStack.pop_back();
            }
        } else if (inst.opcode == AxbOpcode::SetStaticProperty && !parentStack.empty()) {
            IrSceneNode& cur = outIr.nodes[parentStack.back()];
            cur.staticProperties.emplace_back(inst.propId, inst.literalValue);
            if (inst.propId == PropId_Width) {
                (void)TryGetFloat(inst.literalValue, cur.width);
            } else if (inst.propId == PropId_Height) {
                (void)TryGetFloat(inst.literalValue, cur.height);
            } else if (inst.propId == PropId_AnimTranslateX || inst.propId == PropId_CanvasLeft) {
                (void)TryGetFloat(inst.literalValue, cur.translateX);
            } else if (inst.propId == PropId_AnimTranslateY || inst.propId == PropId_CanvasTop) {
                (void)TryGetFloat(inst.literalValue, cur.translateY);
            } else if (inst.propId == PropId_AnimScaleX) {
                (void)TryGetFloat(inst.literalValue, cur.scaleX);
            } else if (inst.propId == PropId_AnimScaleY) {
                (void)TryGetFloat(inst.literalValue, cur.scaleY);
            } else if (inst.propId == PropId_AnimRotateAngle) {
                (void)TryGetFloat(inst.literalValue, cur.rotateDeg);
            } else if (inst.propId == PropId_AnimSkewX) {
                (void)TryGetFloat(inst.literalValue, cur.skewXDeg);
            } else if (inst.propId == PropId_AnimSkewY) {
                (void)TryGetFloat(inst.literalValue, cur.skewYDeg);
            } else if (inst.propId == PropId_Opacity) {
                (void)TryGetFloat(inst.literalValue, cur.opacity);
            } else if (inst.propId == PropId_PathData) {
                if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                    cur.svgPathData = std::string(sv->view());
                }
            }
        }
    }

    if (!outIr.nodes.empty()) {
        outIr.designWidth = outIr.nodes[0].width > 0.0f ? outIr.nodes[0].width : 256.0f;
        outIr.designHeight = outIr.nodes[0].height > 0.0f ? outIr.nodes[0].height : 256.0f;
    }

    for (const auto& mp : pkg.mtsdfPaths) {
        IrVectorShapeAsset shape{};
        shape.pathHash = mp.pathHash;
        shape.adaptiveCellSize = mp.cellWidth;
        shape.pxRange = mp.pxRange;
        outIr.vectorShapes.push_back(std::move(shape));
    }

    for (std::size_t ciIdx = 0; ciIdx < pkg.contractInputs.size(); ++ciIdx) {
        const auto& cp = pkg.contractInputs[ciIdx];
        IrContractPort port{};
        port.name = std::string(pkg.GetString(cp.nameStrIdx));
        port.direction = cp.isTrigger ? IrContractPortDirection::OutputTrigger : IrContractPortDirection::Input;
        port.valueType = cp.isTrigger ? IrContractPortValueType::Trigger
                                      : static_cast<IrContractPortValueType>(cp.valueKind);
        port.assignedPropId = (cp.bridgePropId != 0xFFFFu)
            ? cp.bridgePropId
            : static_cast<std::uint16_t>(PropId_FirstContractInput + ciIdx);
        port.smInputIndex = static_cast<std::uint16_t>(ciIdx);
        port.defaultValue = cp.defaultValue;
        (void)TryGetFloat(cp.defaultValue, port.defaultFloat);
        outIr.contractPorts.push_back(std::move(port));
    }
}

void EmitDecomposedSvgMatrixInstructions(const Matrix2D& m, AxbPackage& pkg) {
    const DecomposedTransform2D d = PolarDecomposeMatrix2D(m);
    auto pushFloatProp = [&](std::uint16_t propId, float v) {
        AxbInstruction inst{};
        inst.opcode = AxbOpcode::SetStaticProperty;
        inst.propId = propId;
        inst.literalValue = MakeFloat(v);
        pkg.instructions.push_back(std::move(inst));
    };
    if (std::fabs(d.translateX) > 1e-4f) pushFloatProp(PropId_AnimTranslateX, d.translateX);
    if (std::fabs(d.translateY) > 1e-4f) pushFloatProp(PropId_AnimTranslateY, d.translateY);
    if (std::fabs(d.rotateDeg) > 1e-4f) pushFloatProp(PropId_AnimRotateAngle, d.rotateDeg);
    if (std::fabs(d.scaleX - 1.0f) > 1e-4f) pushFloatProp(PropId_AnimScaleX, d.scaleX);
    if (std::fabs(d.scaleY - 1.0f) > 1e-4f) pushFloatProp(PropId_AnimScaleY, d.scaleY);
    if (std::fabs(d.skewXDeg) > 1e-4f) pushFloatProp(PropId_AnimSkewX, d.skewXDeg);
    if (std::fabs(d.skewYDeg) > 1e-4f) pushFloatProp(PropId_AnimSkewY, d.skewYDeg);
}

Matrix2D ParseSvgTransformAttr(std::string_view raw) noexcept {
    raw = Trim(raw);
    if (raw.rfind("matrix(", 0) == 0 && raw.back() == ')') {
        return ParseXamlMatrix2DLiteral(raw.substr(7, raw.size() - 8));
    }
    if (raw.rfind("translate(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(10, raw.size() - 11);
        const std::size_t sep = inner.find_first_of(", ");
        const float tx = ParseFloat32(sep == std::string_view::npos ? inner : inner.substr(0, sep), 0.0f);
        const float ty = sep == std::string_view::npos ? 0.0f : ParseFloat32(Trim(inner.substr(sep + 1)), 0.0f);
        return Matrix2D::Translation(tx, ty);
    }
    if (raw.rfind("scale(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(6, raw.size() - 7);
        const std::size_t sep = inner.find_first_of(", ");
        const float sx = ParseFloat32(sep == std::string_view::npos ? inner : inner.substr(0, sep), 1.0f);
        const float sy = sep == std::string_view::npos ? sx : ParseFloat32(Trim(inner.substr(sep + 1)), sx);
        return Matrix2D::Scaling(sx, sy);
    }
    if (raw.rfind("rotate(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(7, raw.size() - 8);
        return Matrix2D::Rotation(ParseFloat32(inner, 0.0f));
    }
    return Matrix2D::Identity();
}

// Simple JSON token helpers for Lottie & Rive structured JSON/RIV descriptions
std::string_view FindJsonFieldValue(std::string_view obj, std::string_view key) noexcept {
    const std::string pattern = "\"" + std::string(key) + "\"";
    int depth = 0;
    for (std::size_t i = 0; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '\"') {
            std::size_t endQuote = i + 1;
            while (endQuote < obj.size() && !(obj[endQuote] == '\"' && obj[endQuote - 1] != '\\')) {
                ++endQuote;
            }
            if (endQuote >= obj.size()) break;
            if (depth <= 1 && (endQuote - i + 1) == pattern.size() &&
                obj.substr(i, pattern.size()) == pattern) {
                std::size_t pos = endQuote + 1;
                while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) {
                    ++pos;
                }
                if (pos < obj.size() && obj[pos] == ':') {
                    ++pos;
                    while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) {
                        ++pos;
                    }
                    if (pos >= obj.size()) return {};
                    const char startChar = obj[pos];
                    if (startChar == '\"') {
                        std::size_t valEnd = pos + 1;
                        while (valEnd < obj.size() && !(obj[valEnd] == '\"' && obj[valEnd - 1] != '\\')) {
                            ++valEnd;
                        }
                        return obj.substr(pos + 1, valEnd - (pos + 1));
                    }
                    if (startChar == '{' || startChar == '[') {
                        const char closeChar = (startChar == '{') ? '}' : ']';
                        int subDepth = 0;
                        bool subStr = false;
                        for (std::size_t j = pos; j < obj.size(); ++j) {
                            if (obj[j] == '\"' && (j == pos || obj[j - 1] != '\\')) {
                                subStr = !subStr;
                                continue;
                            }
                            if (subStr) continue;
                            if (obj[j] == startChar) ++subDepth;
                            else if (obj[j] == closeChar) {
                                --subDepth;
                                if (subDepth == 0) return obj.substr(pos, j - pos + 1);
                            }
                        }
                        return {};
                    }
                    std::size_t endVal = pos;
                    while (endVal < obj.size() && obj[endVal] != ',' && obj[endVal] != '}' &&
                           obj[endVal] != ']' && !std::isspace(static_cast<unsigned char>(obj[endVal]))) {
                        ++endVal;
                    }
                    return obj.substr(pos, endVal - pos);
                }
            }
            i = endQuote;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        }
    }
    return {};
}

std::vector<std::string_view> SplitJsonTopLevelArray(std::string_view arr) {
    std::vector<std::string_view> items;
    arr = Trim(arr);
    if (arr.size() < 2 || arr.front() != '[' || arr.back() != ']') return items;
    arr = Trim(arr.substr(1, arr.size() - 2));
    int depth = 0;
    bool inStr = false;
    std::size_t itemStart = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const char c = arr[i];
        if (c == '\"' && (i == 0 || arr[i - 1] != '\\')) inStr = !inStr;
        if (inStr) continue;
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            const auto piece = Trim(arr.substr(itemStart, i - itemStart));
            if (!piece.empty()) items.push_back(piece);
            itemStart = i + 1;
        }
    }
    const auto tail = Trim(arr.substr(itemStart));
    if (!tail.empty()) items.push_back(tail);
    return items;
}

float ExtractFirstFloatFromJsonToken(std::string_view tok, float fallback) noexcept {
    tok = Trim(tok);
    if (tok.empty()) return fallback;
    if (tok.front() == '[') {
        auto elems = SplitJsonTopLevelArray(tok);
        if (!elems.empty()) return ParseFloat32(elems[0], fallback);
        return fallback;
    }
    return ParseFloat32(tok, fallback);
}

bool IsSvgAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4 && EqualsIgnoreCase(path.substr(path.size() - 4), ".svg")) {
        return true;
    }
    const std::string_view trimmed = Trim(content);
    return trimmed.rfind("<svg", 0) == 0;
}

bool IsLottieAssetInput(std::string_view path, std::string_view content) noexcept {
    if ((path.size() >= 5 && EqualsIgnoreCase(path.substr(path.size() - 5), ".json")) ||
        (path.size() >= 7 && EqualsIgnoreCase(path.substr(path.size() - 7), ".lottie"))) {
        const std::string_view trimmed = Trim(content);
        if (!trimmed.empty() && trimmed.front() == '{' &&
            trimmed.find("\"layers\"") != std::string_view::npos) {
            return true;
        }
    }
    const std::string_view trimmed = Trim(content);
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"fr\"") != std::string_view::npos &&
           trimmed.find("\"layers\"") != std::string_view::npos;
}

bool IsRiveAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4 && EqualsIgnoreCase(path.substr(path.size() - 4), ".riv")) {
        return true;
    }
    const std::string_view trimmed = Trim(content);
    if (trimmed.size() >= 4 && trimmed.substr(0, 4) == "RIVE") {
        return true;
    }
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"artboard\"") != std::string_view::npos &&
           trimmed.find("\"stateMachines\"") != std::string_view::npos;
}

} // namespace

bool SvgTokenFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_SVG_EMPTY: Empty SVG input");
        return false;
    }
    const std::string_view src(reinterpret_cast<const char*>(rawBytes), byteLength);
    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";

    float viewW = 256.0f;
    float viewH = 256.0f;
    std::unordered_set<std::uint64_t> bakedHashes;
    bool rootEmitted = false;
    std::size_t openContainers = 0u;

    std::size_t pos = 0;
    while (pos < src.size()) {
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) break;
        pos = lt + 1;
        if (pos >= src.size()) break;
        if (src.substr(pos, 3) == "!--") {
            const std::size_t endC = src.find("-->", pos + 3);
            pos = (endC == std::string_view::npos) ? src.size() : (endC + 3);
            continue;
        }
        if (src[pos] == '?') {
            const std::size_t endD = src.find("?>", pos + 1);
            pos = (endD == std::string_view::npos) ? src.size() : (endD + 2);
            continue;
        }
        if (src[pos] == '/') {
            const std::size_t gt = src.find('>', pos + 1);
            const std::string_view closeTag = StripXmlPrefix(Trim(src.substr(pos + 1, (gt == std::string_view::npos ? src.size() : gt) - (pos + 1))));
            if ((closeTag == "svg" || closeTag == "g") && openContainers > 0u) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
                --openContainers;
            }
            pos = (gt == std::string_view::npos) ? src.size() : (gt + 1);
            continue;
        }

        std::size_t tagEnd = pos;
        while (tagEnd < src.size() && !std::isspace(static_cast<unsigned char>(src[tagEnd])) &&
               src[tagEnd] != '>' && src[tagEnd] != '/') {
            ++tagEnd;
        }
        const std::string_view shortTag = StripXmlPrefix(src.substr(pos, tagEnd - pos));
        pos = tagEnd;

        std::vector<std::pair<std::string_view, std::string_view>> attrs;
        bool selfClosing = false;
        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
            if (pos >= src.size()) break;
            if (src[pos] == '>') { ++pos; break; }
            if (src.substr(pos, 2) == "/>") { selfClosing = true; pos += 2; break; }
            const std::size_t aStart = pos;
            while (pos < src.size() && src[pos] != '=' &&
                   !std::isspace(static_cast<unsigned char>(src[pos])) &&
                   src[pos] != '>' && src[pos] != '/') {
                ++pos;
            }
            const std::string_view aName = src.substr(aStart, pos - aStart);
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
            std::string_view aVal;
            if (pos < src.size() && src[pos] == '=') {
                ++pos;
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
                if (pos < src.size() && (src[pos] == '\"' || src[pos] == '\'')) {
                    const char q = src[pos++];
                    const std::size_t vStart = pos;
                    while (pos < src.size() && src[pos] != q) ++pos;
                    aVal = src.substr(vStart, pos - vStart);
                    if (pos < src.size() && src[pos] == q) ++pos;
                }
            }
            if (!aName.empty()) attrs.emplace_back(aName, aVal);
        }

        if (shortTag == "svg") {
            std::string_view elemId;
            for (const auto& kv : attrs) {
                if (kv.first == "x:Class") pkg.className = std::string(Trim(kv.second));
                else if (kv.first == "id" || kv.first == "x:Name") elemId = Trim(kv.second);
                else if (kv.first == "width") viewW = ParseFloat32(kv.second, viewW);
                else if (kv.first == "height") viewH = ParseFloat32(kv.second, viewH);
                else if (kv.first == "viewBox") {
                    std::string_view vb = Trim(kv.second);
                    float vals[4]{0.0f, 0.0f, viewW, viewH};
                    for (int vi = 0; vi < 4 && !vb.empty(); ++vi) {
                        const std::size_t sep = vb.find_first_of(", ");
                        vals[vi] = ParseFloat32(sep == std::string_view::npos ? vb : vb.substr(0, sep), vals[vi]);
                        if (sep == std::string_view::npos) break;
                        vb = Trim(vb.substr(sep + 1));
                    }
                    if (vals[2] > 0.0f) viewW = vals[2];
                    if (vals[3] > 0.0f) viewH = vals[3];
                }
            }
            AxbInstruction beginRoot{};
            beginRoot.opcode = AxbOpcode::BeginElement;
            beginRoot.strIdx0 = pkg.InternString("Canvas");
            beginRoot.strIdx1 = pkg.InternString(elemId);
            pkg.instructions.push_back(std::move(beginRoot));

            AxbInstruction wInst{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(viewW)};
            AxbInstruction hInst{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(viewH)};
            pkg.instructions.push_back(std::move(wInst));
            pkg.instructions.push_back(std::move(hInst));
            rootEmitted = true;
            if (selfClosing) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
            } else {
                ++openContainers;
            }
            continue;
        }

        if (!rootEmitted) continue;

        if (shortTag == "g") {
            std::string_view gName;
            Matrix2D gMat = Matrix2D::Identity();
            bool hasMat = false;
            float gOpacity = 1.0f;
            for (const auto& kv : attrs) {
                if (kv.first == "id" || kv.first == "x:Name") gName = Trim(kv.second);
                else if (kv.first == "transform") { gMat = ParseSvgTransformAttr(kv.second); hasMat = true; }
                else if (kv.first == "opacity") gOpacity = ParseFloat32(kv.second, 1.0f);
            }
            AxbInstruction beginG{};
            beginG.opcode = AxbOpcode::BeginElement;
            beginG.strIdx0 = pkg.InternString("Canvas");
            beginG.strIdx1 = pkg.InternString(gName);
            pkg.instructions.push_back(std::move(beginG));
            if (!gName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{std::string(gName), "Canvas", 0, PropertySlotHandle::InvalidIndex});
            }
            if (std::fabs(gOpacity - 1.0f) > 1e-4f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(gOpacity)});
            }
            if (hasMat) {
                EmitDecomposedSvgMatrixInstructions(gMat, pkg);
            }
            if (selfClosing) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
            } else {
                ++openContainers;
            }
            continue;
        }

        if (shortTag == "path") {
            std::string_view pName;
            std::string_view dPath;
            std::string_view fillStr = "#FFFFFFFF";
            std::string_view strokeStr;
            float strokeW = 0.0f;
            float opacity = 1.0f;
            float pathW = viewW;
            float pathH = viewH;
            Matrix2D pMat = Matrix2D::Identity();
            bool hasMat = false;

            for (const auto& kv : attrs) {
                if (kv.first == "id" || kv.first == "x:Name") pName = Trim(kv.second);
                else if (kv.first == "d" || kv.first == "Data") dPath = Trim(kv.second);
                else if (kv.first == "fill" || kv.first == "Fill") fillStr = Trim(kv.second);
                else if (kv.first == "stroke" || kv.first == "Stroke") strokeStr = Trim(kv.second);
                else if (kv.first == "stroke-width" || kv.first == "StrokeThickness") strokeW = ParseFloat32(kv.second, 0.0f);
                else if (kv.first == "opacity" || kv.first == "Opacity") opacity = ParseFloat32(kv.second, 1.0f);
                else if (kv.first == "width" || kv.first == "Width") pathW = ParseFloat32(kv.second, viewW);
                else if (kv.first == "height" || kv.first == "Height") pathH = ParseFloat32(kv.second, viewH);
                else if (kv.first == "transform") { pMat = ParseSvgTransformAttr(kv.second); hasMat = true; }
            }

            if (!dPath.empty()) {
                const std::uint64_t h = ComputeSvgPathHash(dPath);
                if (h != 0 && bakedHashes.insert(h).second) {
                    const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                    const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                    pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
                }
            }

            AxbInstruction beginP{};
            beginP.opcode = AxbOpcode::BeginElement;
            beginP.strIdx0 = pkg.InternString("Path");
            beginP.strIdx1 = pkg.InternString(pName);
            pkg.instructions.push_back(std::move(beginP));
            if (!pName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{std::string(pName), "Path", 0, PropertySlotHandle::InvalidIndex});
            }
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(pathW)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(pathH)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            if (!dPath.empty()) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            }
            if (!fillStr.empty() && !EqualsIgnoreCase(fillStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillStr)});
            }
            if (!strokeStr.empty() && !EqualsIgnoreCase(strokeStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_BorderBrush, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_BorderBrush, strokeStr)});
            }
            if (strokeW > 0.0f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_BorderThickness, 0, 0, 0, 0, value::FromThickness(Thickness(strokeW))});
            }
            if (std::fabs(opacity - 1.0f) > 1e-4f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(opacity)});
            }
            if (hasMat) {
                EmitDecomposedSvgMatrixInstructions(pMat, pkg);
            }
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        }
    }

    while (openContainers > 0u) {
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        --openContainers;
    }

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::SvgVectorToken, sourcePath, outIr);
    return !pkg.instructions.empty();
}

bool LottieFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_LOTTIE_EMPTY: Empty Lottie JSON input");
        return false;
    }
    const std::string_view json(reinterpret_cast<const char*>(rawBytes), byteLength);
    const float fps = ParseFloat32(FindJsonFieldValue(json, "fr"), 60.0f);
    const float inFrame = ParseFloat32(FindJsonFieldValue(json, "ip"), 0.0f);
    const float outFrame = ParseFloat32(FindJsonFieldValue(json, "op"), 60.0f);
    const float compW = ParseFloat32(FindJsonFieldValue(json, "w"), 256.0f);
    const float compH = ParseFloat32(FindJsonFieldValue(json, "h"), 256.0f);
    const std::string_view compName = FindJsonFieldValue(json, "nm");
    const std::string_view classAttr = FindJsonFieldValue(json, "x:Class");
    const float durationSec = std::max(0.001f, (outFrame - inFrame) / std::max(1.0f, fps));

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";
    if (!classAttr.empty()) {
        pkg.className = std::string(classAttr);
    }

    // Root Canvas (localElementIdx = 0)
    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(compName), 0, {}});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(compW)});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(compH)});

    AxbMotionClipDesc mainClip{};
    mainClip.nameStrIdx = pkg.InternString(compName.empty() ? "LottieMain" : compName);
    mainClip.durationSec = durationSec;
    mainClip.firstTrackIdx = 0u;
    mainClip.trackCount = 0u;
    mainClip.isLooping = true;

    struct LayerIndMap {
        int lottieInd = -1;
        int parentInd = -1;
        std::uint32_t localElemIdx = 0u;
    };
    std::vector<LayerIndMap> layerMappings;
    std::unordered_set<std::uint64_t> bakedHashes;

    auto parseAnimOrStaticChannel = [&](std::string_view propObj,
                                        std::uint32_t targetElemIdx,
                                        std::uint16_t propId,
                                        float scaleFactor,
                                        float defaultStaticVal,
                                        int componentIdx) -> float {
        if (propObj.empty()) return defaultStaticVal;
        const std::string_view aFlag = FindJsonFieldValue(propObj, "a");
        const std::string_view kVal = FindJsonFieldValue(propObj, "k");
        if (aFlag != "1") {
            if (kVal.front() == '[') {
                const auto comps = SplitJsonTopLevelArray(kVal);
                if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                    return ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], defaultStaticVal) * scaleFactor;
                }
            }
            return ExtractFirstFloatFromJsonToken(kVal, defaultStaticVal) * scaleFactor;
        }

        const auto kfObjs = SplitJsonTopLevelArray(kVal);
        if (kfObjs.empty()) return defaultStaticVal;

        AxbMotionTrackDesc track{};
        track.localElementIdx = static_cast<std::uint16_t>(targetElemIdx);
        track.targetPropId = propId;
        track.channelCount = 1u;
        track.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());

        float firstVal = defaultStaticVal;
        float prevEndVal = defaultStaticVal;
        bool hasPrevEnd = false;
        for (std::size_t ki = 0; ki < kfObjs.size(); ++ki) {
            const std::string_view kfObj = kfObjs[ki];
            const float tFrame = ParseFloat32(FindJsonFieldValue(kfObj, "t"), inFrame);
            const std::string_view sArr = FindJsonFieldValue(kfObj, "s");
            const std::string_view eArr = FindJsonFieldValue(kfObj, "e");
            float val = hasPrevEnd ? prevEndVal : defaultStaticVal;
            if (!sArr.empty()) {
                if (sArr.front() == '[') {
                    const auto comps = SplitJsonTopLevelArray(sArr);
                    if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                        val = ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], val) * scaleFactor;
                    } else if (!comps.empty()) {
                        val = ParseFloat32(comps[0], val) * scaleFactor;
                    }
                } else {
                    val = ParseFloat32(sArr, val) * scaleFactor;
                }
            }
            if (!eArr.empty()) {
                if (eArr.front() == '[') {
                    const auto comps = SplitJsonTopLevelArray(eArr);
                    if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                        prevEndVal = ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], val) * scaleFactor;
                        hasPrevEnd = true;
                    } else if (!comps.empty()) {
                        prevEndVal = ParseFloat32(comps[0], val) * scaleFactor;
                        hasPrevEnd = true;
                    }
                } else {
                    prevEndVal = ParseFloat32(eArr, val) * scaleFactor;
                    hasPrevEnd = true;
                }
            }
            if (ki == 0) firstVal = val;

            AxbFlatKeyframe kf{};
            kf.timeSec = std::max(0.0f, (tFrame - inFrame) / std::max(1.0f, fps));
            kf.value = Float4Value{val, 0.0f, 0.0f, 0.0f};
            const std::string_view oObj = FindJsonFieldValue(kfObj, "o");
            const std::string_view iObj = FindJsonFieldValue(kfObj, "i");
            if (!oObj.empty() && !iObj.empty()) {
                kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                const float bx1 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "x"), 0.25f), 0.0f, 1.0f);
                const float by1 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "y"), 0.25f), -4.0f, 4.0f);
                const float bx2 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "x"), 0.75f), 0.0f, 1.0f);
                const float by2 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "y"), 0.75f), -4.0f, 4.0f);
                kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(std::round(bx1 * 255.0f));
                kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(std::round(bx2 * 255.0f));
                kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::round(by1 * 256.0f));
                kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::round(by2 * 256.0f));
            } else {
                kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
            }
            pkg.motionKeyframes.push_back(std::move(kf));
        }

        track.keyframeCount = static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - track.firstKeyframeIdx;
        if (track.keyframeCount > 0u) {
            pkg.motionTracks.push_back(std::move(track));
            ++mainClip.trackCount;
        }
        return firstVal;
    };

    const std::string_view layersArr = FindJsonFieldValue(json, "layers");
    const auto layerObjs = SplitJsonTopLevelArray(layersArr);
    std::uint32_t nextLocalElemIdx = 1u;
    for (const auto& layerObj : layerObjs) {
        const std::string_view layerName = FindJsonFieldValue(layerObj, "nm");
        const int ind = static_cast<int>(ParseFloat32(FindJsonFieldValue(layerObj, "ind"), -1.0f));
        const int parentInd = static_cast<int>(ParseFloat32(FindJsonFieldValue(layerObj, "parent"), -1.0f));
        const std::string_view dPath = FindJsonFieldValue(layerObj, "d");
        const std::string_view fillHex = FindJsonFieldValue(layerObj, "fill");
        const float lw = ParseFloat32(FindJsonFieldValue(layerObj, "w"), compW);
        const float lh = ParseFloat32(FindJsonFieldValue(layerObj, "h"), compH);

        const std::uint32_t elemIdx = nextLocalElemIdx++;
        layerMappings.push_back(LayerIndMap{ind, parentInd, elemIdx});

        const std::string_view elemType = dPath.empty() ? "Border" : "Path";
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::BeginElement, 0, 0, pkg.InternString(elemType), pkg.InternString(layerName), 0, {}});
        if (!layerName.empty()) {
            pkg.namedControls.push_back(AxbNamedControlSlot{
                std::string(layerName), std::string(elemType), 0, PropertySlotHandle::InvalidIndex});
        }
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(lw)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(lh)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});

        if (!dPath.empty()) {
            const std::uint64_t h = ComputeSvgPathHash(dPath);
            if (h != 0 && bakedHashes.insert(h).second) {
                const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            if (!fillHex.empty()) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
            }
        } else if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
        }

        const std::string_view ksObj = FindJsonFieldValue(layerObj, "ks");
        if (!ksObj.empty()) {
            const float op = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "o"), elemIdx, PropId_Opacity, 0.01f, 1.0f, 0);
            const float rot = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "r"), elemIdx, PropId_AnimRotateAngle, 1.0f, 0.0f, 0);
            const float tx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "p"), elemIdx, PropId_AnimTranslateX, 1.0f, 0.0f, 0);
            const float ty = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "p"), elemIdx, PropId_AnimTranslateY, 1.0f, 0.0f, 1);
            const float sx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "s"), elemIdx, PropId_AnimScaleX, 0.01f, 1.0f, 0);
            const float sy = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "s"), elemIdx, PropId_AnimScaleY, 0.01f, 1.0f, 1);
            const float skx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "sk"), elemIdx, PropId_AnimSkewX, 1.0f, 0.0f, 0);

            if (std::fabs(op - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(op)});
            if (std::fabs(rot) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimRotateAngle, 0, 0, 0, 0, MakeFloat(rot)});
            if (std::fabs(tx) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateX, 0, 0, 0, 0, MakeFloat(tx)});
            if (std::fabs(ty) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateY, 0, 0, 0, 0, MakeFloat(ty)});
            if (std::fabs(sx - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleX, 0, 0, 0, 0, MakeFloat(sx)});
            if (std::fabs(sy - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleY, 0, 0, 0, 0, MakeFloat(sy)});
            if (std::fabs(skx) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimSkewX, 0, 0, 0, 0, MakeFloat(skx)});
        }

        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
    }

    // Close Root Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    // Resolve Lottie `parent` layer indices into non-tree AxbTransformConstraintDesc records
    for (const auto& lm : layerMappings) {
        if (lm.parentInd >= 0) {
            for (const auto& targetLm : layerMappings) {
                if (targetLm.lottieInd == lm.parentInd && targetLm.localElemIdx != lm.localElemIdx) {
                    AxbTransformConstraintDesc tc{};
                    tc.childLocalElementIdx = static_cast<std::uint16_t>(lm.localElemIdx);
                    tc.parentLocalElementIdx = static_cast<std::uint16_t>(targetLm.localElemIdx);
                    pkg.transformConstraints.push_back(tc);
                    break;
                }
            }
        }
    }

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    // Add MOT6 clip + default "Progress" contract input + playback state machine
    pkg.motionClips.push_back(mainClip);

    AxbContractInputDesc progressIn{};
    progressIn.nameStrIdx = pkg.InternString("Progress");
    progressIn.valueKind = 1u; // Float
    progressIn.isTrigger = 0u;
    progressIn.bridgePropId = PropId_FirstContractInput;
    progressIn.defaultValue = MakeFloat(0.0f);
    pkg.contractInputs.push_back(progressIn);

    AxbSmState playState{};
    playState.nameStrIdx = pkg.InternString("Playing");
    playState.kind = SmStateKind::SingleClip;
    playState.primaryClipIdx = 0u;
    playState.speedMultiplier = 1.0f;
    pkg.smStates.push_back(playState);

    AxbSmState scrubState{};
    scrubState.nameStrIdx = pkg.InternString("Scrub");
    scrubState.kind = SmStateKind::ScrubbedClip;
    scrubState.primaryClipIdx = 0u;
    scrubState.progressInputSlot = 0u;
    scrubState.speedMultiplier = 1.0f;
    pkg.smStates.push_back(scrubState);

    AxbSmLayerDesc layer{};
    layer.nameStrIdx = pkg.InternString("MainLayer");
    layer.blendMode = SmLayerBlendMode::Override;
    layer.layerWeight = 1.0f;
    layer.firstStateIdx = 0u;
    layer.stateCount = 2u;
    layer.entryStateIdx = 0u;
    pkg.smLayers.push_back(layer);

    AxbStateMachineDesc sm{};
    sm.nameStrIdx = pkg.InternString("LottiePlayback");
    sm.firstLayerIdx = 0u;
    sm.layerCount = 1u;
    pkg.stateMachines.push_back(sm);

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::LottieJson, sourcePath, outIr);
    return true;
}

bool RiveFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_RIVE_EMPTY: Empty Rive .riv input");
        return false;
    }
    std::string_view content(reinterpret_cast<const char*>(rawBytes), byteLength);
    if (content.size() >= 4 && content.substr(0, 4) == "RIVE") {
        content = Trim(content.substr(4));
    }

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";

    const std::string_view classAttr = FindJsonFieldValue(content, "x:Class");
    if (!classAttr.empty()) {
        pkg.className = std::string(classAttr);
    }

    const std::string_view abObj = FindJsonFieldValue(content, "artboard");
    const std::string_view abSource = abObj.empty() ? content : abObj;
    const std::string_view abName = FindJsonFieldValue(abSource, "name");
    const float abW = ParseFloat32(FindJsonFieldValue(abSource, "width"), 320.0f);
    const float abH = ParseFloat32(FindJsonFieldValue(abSource, "height"), 320.0f);

    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(abName), 0, {}});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(abW)});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(abH)});

    std::unordered_map<std::string, std::uint32_t> elemIndexByName;
    if (!abName.empty()) {
        elemIndexByName.emplace(std::string(abName), 0u);
    }
    std::unordered_set<std::uint64_t> bakedHashes;

    const auto nodeObjs = SplitJsonTopLevelArray(FindJsonFieldValue(abSource, "nodes"));
    std::uint32_t nextElemIdx = 1u;
    for (const auto& nodeObj : nodeObjs) {
        const std::string_view nName = FindJsonFieldValue(nodeObj, "name");
        const std::string_view nType = FindJsonFieldValue(nodeObj, "type");
        const std::string_view dPath = FindJsonFieldValue(nodeObj, "path");
        const std::string_view fillHex = FindJsonFieldValue(nodeObj, "fill");
        const float nw = ParseFloat32(FindJsonFieldValue(nodeObj, "width"), abW);
        const float nh = ParseFloat32(FindJsonFieldValue(nodeObj, "height"), abH);
        const float nx = ParseFloat32(FindJsonFieldValue(nodeObj, "x"), 0.0f);
        const float ny = ParseFloat32(FindJsonFieldValue(nodeObj, "y"), 0.0f);
        const float nRot = ParseFloat32(FindJsonFieldValue(nodeObj, "rotation"), 0.0f);
        const float nsx = ParseFloat32(FindJsonFieldValue(nodeObj, "scaleX"), 1.0f);
        const float nsy = ParseFloat32(FindJsonFieldValue(nodeObj, "scaleY"), 1.0f);
        const float nOp = ParseFloat32(FindJsonFieldValue(nodeObj, "opacity"), 1.0f);

        const std::uint32_t curIdx = nextElemIdx++;
        if (!nName.empty()) {
            elemIndexByName.emplace(std::string(nName), curIdx);
        }

        const std::string_view runtimeType = (!dPath.empty() || EqualsIgnoreCase(nType, "Path")) ? "Path"
                                           : (EqualsIgnoreCase(nType, "TextBlock") ? "TextBlock" : "Border");
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::BeginElement, 0, 0, pkg.InternString(runtimeType), pkg.InternString(nName), 0, {}});
        if (!nName.empty()) {
            pkg.namedControls.push_back(AxbNamedControlSlot{
                std::string(nName), std::string(runtimeType), 0, PropertySlotHandle::InvalidIndex});
        }
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(nw)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(nh)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        if (std::fabs(nx) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateX, 0, 0, 0, 0, MakeFloat(nx)});
        if (std::fabs(ny) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateY, 0, 0, 0, 0, MakeFloat(ny)});
        if (std::fabs(nRot) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimRotateAngle, 0, 0, 0, 0, MakeFloat(nRot)});
        if (std::fabs(nsx - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleX, 0, 0, 0, 0, MakeFloat(nsx)});
        if (std::fabs(nsy - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleY, 0, 0, 0, 0, MakeFloat(nsy)});
        if (std::fabs(nOp - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(nOp)});

        if (!dPath.empty()) {
            const std::uint64_t h = ComputeSvgPathHash(dPath);
            if (h != 0 && bakedHashes.insert(h).second) {
                const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            if (!fillHex.empty()) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
            }
        } else if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
        }

        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
    }

    // Close Artboard Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    // Parse non-tree bone/transform constraints
    const auto constraintObjs = SplitJsonTopLevelArray(FindJsonFieldValue(abSource, "constraints"));
    for (const auto& cObj : constraintObjs) {
        const std::string constrainedName(FindJsonFieldValue(cObj, "constrained"));
        const std::string targetName(FindJsonFieldValue(cObj, "target"));
        const auto itC = elemIndexByName.find(constrainedName);
        const auto itT = elemIndexByName.find(targetName);
        if (itC != elemIndexByName.end() && itT != elemIndexByName.end()) {
            AxbTransformConstraintDesc tc{};
            tc.childLocalElementIdx = static_cast<std::uint16_t>(itC->second);
            tc.parentLocalElementIdx = static_cast<std::uint16_t>(itT->second);
            pkg.transformConstraints.push_back(tc);
        }
    }

    // Parse clips
    std::unordered_map<std::string, std::uint32_t> clipIndexByName;
    const auto clipObjs = SplitJsonTopLevelArray(FindJsonFieldValue(content, "clips"));
    for (const auto& clObj : clipObjs) {
        const std::string_view clName = FindJsonFieldValue(clObj, "name");
        AxbMotionClipDesc clip{};
        clip.nameStrIdx = pkg.InternString(clName);
        clip.durationSec = std::max(0.001f, ParseFloat32(FindJsonFieldValue(clObj, "duration"), 1.0f));
        clip.isLooping = EqualsIgnoreCase(FindJsonFieldValue(clObj, "loop"), "true");
        clip.firstTrackIdx = static_cast<std::uint32_t>(pkg.motionTracks.size());
        clip.firstEventIdx = static_cast<std::uint32_t>(pkg.motionEvents.size());

        const auto trObjs = SplitJsonTopLevelArray(FindJsonFieldValue(clObj, "tracks"));
        for (const auto& trObj : trObjs) {
            const std::string targetName(FindJsonFieldValue(trObj, "target"));
            const std::string_view propName = FindJsonFieldValue(trObj, "property");
            const auto itElem = elemIndexByName.find(targetName);
            const std::uint32_t targetIdx = (itElem != elemIndexByName.end()) ? itElem->second : 0u;
            const std::uint16_t propId = ResolveBuiltinPropertyId(propName);
            if (propId == kUnmappedPropId) continue;

            AxbMotionTrackDesc tr{};
            tr.localElementIdx = static_cast<std::uint16_t>(targetIdx);
            tr.targetPropId = propId;
            tr.channelCount = 1u;
            tr.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());
            const auto kfObjs = SplitJsonTopLevelArray(FindJsonFieldValue(trObj, "keyframes"));
            for (const auto& kfObj : kfObjs) {
                AxbFlatKeyframe kf{};
                kf.timeSec = ParseFloat32(FindJsonFieldValue(kfObj, "t"), 0.0f);
                const std::string_view vRaw = FindJsonFieldValue(kfObj, "v");
                const UnifiedValue parsedV = ParseBuiltinLiteralPropertyValue(propId, vRaw);
                if (! parsedV.tryGetFloat4(kf.value)) {
                    float sf = 0.0f;
                    (void)TryGetFloat(parsedV, sf);
                    kf.value = Float4Value{sf, 0.0f, 0.0f, 0.0f};
                } else {
                    tr.channelCount = 4u;
                }
                const std::string_view easeStr = FindJsonFieldValue(kfObj, "easing");
                if (EqualsIgnoreCase(easeStr, "CubicBezier")) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                    const float bx1 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "bx1"), 0.25f), 0.0f, 1.0f);
                    const float by1 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "by1"), 0.1f), -4.0f, 4.0f);
                    const float bx2 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "bx2"), 0.25f), 0.0f, 1.0f);
                    const float by2 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "by2"), 1.0f), -4.0f, 4.0f);
                    kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(std::round(bx1 * 255.0f));
                    kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(std::round(bx2 * 255.0f));
                    kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::round(by1 * 256.0f));
                    kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::round(by2 * 256.0f));
                } else {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
                }
                pkg.motionKeyframes.push_back(std::move(kf));
            }
            tr.keyframeCount = static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - tr.firstKeyframeIdx;
            if (tr.keyframeCount > 0u) {
                pkg.motionTracks.push_back(std::move(tr));
            }
        }
        clip.trackCount = static_cast<std::uint32_t>(pkg.motionTracks.size()) - clip.firstTrackIdx;

        const auto evObjs = SplitJsonTopLevelArray(FindJsonFieldValue(clObj, "events"));
        for (const auto& evObj : evObjs) {
            AxbTimelineEventMarker ev{};
            ev.timeSec = ParseFloat32(FindJsonFieldValue(evObj, "t"), 0.0f);
            ev.eventNameStrIdx = pkg.InternString(FindJsonFieldValue(evObj, "name"));
            pkg.motionEvents.push_back(ev);
        }
        clip.eventCount = static_cast<std::uint32_t>(pkg.motionEvents.size() - clip.firstEventIdx);

        const auto clipIdx = static_cast<std::uint32_t>(pkg.motionClips.size());
        if (!clName.empty()) clipIndexByName.emplace(std::string(clName), clipIdx);
        pkg.motionClips.push_back(clip);
    }

    // Parse StateMachines
    const auto smObjs = SplitJsonTopLevelArray(FindJsonFieldValue(content, "stateMachines"));
    for (const auto& smObj : smObjs) {
        AxbStateMachineDesc sm{};
        sm.nameStrIdx = pkg.InternString(FindJsonFieldValue(smObj, "name"));
        sm.firstLayerIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        sm.firstListenerIdx = static_cast<std::uint32_t>(pkg.smListeners.size());

        std::unordered_map<std::string, std::uint32_t> inputIdxByName;
        const auto inObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "inputs"));
        for (const auto& inObj : inObjs) {
            const std::string_view inName = FindJsonFieldValue(inObj, "name");
            const std::string_view inType = FindJsonFieldValue(inObj, "type");
            const auto slotIdx = static_cast<std::uint32_t>(pkg.contractInputs.size());
            const auto portPropId = static_cast<std::uint16_t>(PropId_FirstContractInput + slotIdx);

            AxbContractInputDesc inDesc{};
            inDesc.nameStrIdx = pkg.InternString(inName);
            inDesc.bridgePropId = portPropId;
            if (EqualsIgnoreCase(inType, "Boolean") || EqualsIgnoreCase(inType, "Bool")) {
                inDesc.valueKind = 0u;
                inDesc.defaultValue = UnifiedValue{EqualsIgnoreCase(FindJsonFieldValue(inObj, "default"), "true")};
            } else if (EqualsIgnoreCase(inType, "Trigger")) {
                inDesc.valueKind = 5u;
                inDesc.isTrigger = 1u;
                inDesc.defaultValue = MakeFloat(0.0f);
            } else {
                inDesc.valueKind = 1u;
                inDesc.defaultValue = MakeFloat(ParseFloat32(FindJsonFieldValue(inObj, "default"), 0.0f));
            }
            pkg.contractInputs.push_back(inDesc);
            if (!inName.empty()) inputIdxByName.emplace(std::string(inName), slotIdx);
        }

        const auto layerObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "layers"));
        for (const auto& lyObj : layerObjs) {
            AxbSmLayerDesc ly{};
            ly.nameStrIdx = pkg.InternString(FindJsonFieldValue(lyObj, "name"));
            ly.blendMode = EqualsIgnoreCase(FindJsonFieldValue(lyObj, "blendMode"), "Additive")
                ? SmLayerBlendMode::Additive
                : SmLayerBlendMode::Override;
            ly.layerWeight = ParseFloat32(FindJsonFieldValue(lyObj, "weight"), 1.0f);
            ly.firstStateIdx = static_cast<std::uint32_t>(pkg.smStates.size());
            ly.firstTransitionIdx = static_cast<std::uint32_t>(pkg.smTransitions.size());

            std::unordered_map<std::string, std::uint16_t> stateIdxByName;
            const auto stObjs = SplitJsonTopLevelArray(FindJsonFieldValue(lyObj, "states"));
            for (const auto& stObj : stObjs) {
                const std::string_view stName = FindJsonFieldValue(stObj, "name");
                const std::string_view stKind = FindJsonFieldValue(stObj, "kind");
                const auto globalStIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                if (!stName.empty()) stateIdxByName.emplace(std::string(stName), globalStIdx);

                AxbSmState st{};
                st.nameStrIdx = pkg.InternString(stName);
                st.speedMultiplier = ParseFloat32(FindJsonFieldValue(stObj, "speed"), 1.0f);

                if (EqualsIgnoreCase(stKind, "Blend1D")) {
                    st.kind = SmStateKind::Blend1D;
                    const std::string paramName(FindJsonFieldValue(stObj, "input"));
                    const auto itIn = inputIdxByName.find(paramName);
                    st.progressInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
                    const auto bItems = SplitJsonTopLevelArray(FindJsonFieldValue(stObj, "thresholds"));
                    if (!bItems.empty()) {
                        st.blendRangeMin = ParseFloat32(FindJsonFieldValue(bItems.front(), "threshold"), 0.0f);
                        const std::string cl0(FindJsonFieldValue(bItems.front(), "clip"));
                        const auto it0 = clipIndexByName.find(cl0);
                        st.primaryClipIdx = (it0 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it0->second) : 0u;
                        if (bItems.size() >= 2u) {
                            st.blendRangeMax = ParseFloat32(FindJsonFieldValue(bItems.back(), "threshold"), 100.0f);
                            const std::string cl1(FindJsonFieldValue(bItems.back(), "clip"));
                            const auto it1 = clipIndexByName.find(cl1);
                            st.secondaryClipIdx = (it1 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it1->second) : st.primaryClipIdx;
                        } else {
                            st.blendRangeMax = st.blendRangeMin + 1.0f;
                            st.secondaryClipIdx = st.primaryClipIdx;
                        }
                    }
                } else if (EqualsIgnoreCase(stKind, "ScrubbedClip")) {
                    st.kind = SmStateKind::ScrubbedClip;
                    const std::string clName(FindJsonFieldValue(stObj, "clip"));
                    const auto itCl = clipIndexByName.find(clName);
                    st.primaryClipIdx = (itCl != clipIndexByName.end()) ? static_cast<std::uint16_t>(itCl->second) : 0u;
                    const std::string paramName(FindJsonFieldValue(stObj, "input"));
                    const auto itIn = inputIdxByName.find(paramName);
                    st.progressInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
                } else {
                    st.kind = SmStateKind::SingleClip;
                    const std::string clName(FindJsonFieldValue(stObj, "clip"));
                    const auto itCl = clipIndexByName.find(clName);
                    st.primaryClipIdx = (itCl != clipIndexByName.end()) ? static_cast<std::uint16_t>(itCl->second) : 0u;
                }
                pkg.smStates.push_back(st);
            }
            ly.stateCount = static_cast<std::uint16_t>(pkg.smStates.size() - ly.firstStateIdx);
            ly.entryStateIdx = static_cast<std::uint16_t>(ly.firstStateIdx);

            // Parse transitions in layer
            const auto trObjs = SplitJsonTopLevelArray(FindJsonFieldValue(lyObj, "transitions"));
            for (const auto& trObj : trObjs) {
                const std::string fromName(FindJsonFieldValue(trObj, "from"));
                const std::string toName(FindJsonFieldValue(trObj, "to"));
                const auto itTo = stateIdxByName.find(toName);
                if (itTo == stateIdxByName.end()) continue;

                AxbSmTransition tr{};
                if (fromName == "*" || fromName.empty()) {
                    tr.fromStateIdx = 0xFFFFu;
                } else {
                    const auto itFrom = stateIdxByName.find(fromName);
                    if (itFrom == stateIdxByName.end()) continue;
                    tr.fromStateIdx = itFrom->second;
                }
                tr.toStateIdx = itTo->second;
                tr.crossfadeDurationSec = ParseFloat32(FindJsonFieldValue(trObj, "duration"), 0.15f);
                tr.exitTimeNormalized01 = ParseFloat32(FindJsonFieldValue(trObj, "exitTime"), 0.0f);
                tr.hasExitTime = (tr.exitTimeNormalized01 > 0.0f);
                tr.firstConditionIdx = static_cast<std::uint32_t>(pkg.smConditions.size());

                const auto condObjs = SplitJsonTopLevelArray(FindJsonFieldValue(trObj, "conditions"));
                for (const auto& cObj : condObjs) {
                    const std::string inName(FindJsonFieldValue(cObj, "input"));
                    const auto itIn = inputIdxByName.find(inName);
                    if (itIn == inputIdxByName.end()) continue;
                    AxbSmCondition cond{};
                    cond.inputSlotIdx = static_cast<std::uint16_t>(itIn->second);
                    const std::string_view opStr = FindJsonFieldValue(cObj, "op");
                    if (opStr == "==" || EqualsIgnoreCase(opStr, "Equals")) cond.op = SmCompareOp::Equal;
                    else if (opStr == "!=" || EqualsIgnoreCase(opStr, "NotEquals")) cond.op = SmCompareOp::NotEqual;
                    else if (opStr == ">" || EqualsIgnoreCase(opStr, "Greater")) cond.op = SmCompareOp::Greater;
                    else if (opStr == ">=" || EqualsIgnoreCase(opStr, "GreaterOrEqual")) cond.op = SmCompareOp::GreaterOrEqual;
                    else if (opStr == "<" || EqualsIgnoreCase(opStr, "Less")) cond.op = SmCompareOp::Less;
                    else if (opStr == "<=" || EqualsIgnoreCase(opStr, "LessOrEqual")) cond.op = SmCompareOp::LessOrEqual;
                    else if (EqualsIgnoreCase(opStr, "TriggerFired")) cond.op = SmCompareOp::TriggerFired;
                    else if (EqualsIgnoreCase(opStr, "IsTrue")) cond.op = SmCompareOp::BoolTrue;
                    else if (EqualsIgnoreCase(opStr, "IsFalse")) cond.op = SmCompareOp::BoolFalse;
                    cond.threshold = ParseFloat32(FindJsonFieldValue(cObj, "value"), 0.0f);
                    pkg.smConditions.push_back(cond);
                }
                tr.conditionCount = static_cast<std::uint16_t>(pkg.smConditions.size() - tr.firstConditionIdx);
                pkg.smTransitions.push_back(tr);
            }
            ly.transitionCount = static_cast<std::uint16_t>(pkg.smTransitions.size() - ly.firstTransitionIdx);

            pkg.smLayers.push_back(ly);
        }
        sm.layerCount = static_cast<std::uint16_t>(pkg.smLayers.size() - sm.firstLayerIdx);

        const auto lstObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "listeners"));
        for (const auto& lObj : lstObjs) {
            AxbSmPointerListener lst{};
            const std::string targetName(FindJsonFieldValue(lObj, "target"));
            const auto itEl = elemIndexByName.find(targetName);
            lst.hitTargetElementIdx = (itEl != elemIndexByName.end()) ? static_cast<std::uint16_t>(itEl->second) : 0u;
            const std::string_view evStr = FindJsonFieldValue(lObj, "event");
            if (EqualsIgnoreCase(evStr, "PointerExit")) lst.eventKind = SmListenerEventKind::PointerExit;
            else if (EqualsIgnoreCase(evStr, "PointerDown")) lst.eventKind = SmListenerEventKind::PointerDown;
            else if (EqualsIgnoreCase(evStr, "PointerUp") || EqualsIgnoreCase(evStr, "Click")) lst.eventKind = SmListenerEventKind::PointerUpInside;
            else if (EqualsIgnoreCase(evStr, "FocusGained")) lst.eventKind = SmListenerEventKind::FocusGained;
            else if (EqualsIgnoreCase(evStr, "FocusLost")) lst.eventKind = SmListenerEventKind::FocusLost;
            else lst.eventKind = SmListenerEventKind::PointerEnter;

            const std::string inName(FindJsonFieldValue(lObj, "input"));
            const auto itIn = inputIdxByName.find(inName);
            lst.targetInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
            const std::string_view actStr = FindJsonFieldValue(lObj, "action");
            if (EqualsIgnoreCase(actStr, "FireTrigger")) lst.actionKind = SmListenerActionKind::FireTrigger;
            else if (EqualsIgnoreCase(actStr, "AddFloat") || EqualsIgnoreCase(actStr, "AddDelta")) lst.actionKind = SmListenerActionKind::AddDelta;
            else lst.actionKind = SmListenerActionKind::SetFloatOrBool;
            lst.actionValue = ParseFloat32(FindJsonFieldValue(lObj, "value"), 1.0f);
            pkg.smListeners.push_back(lst);
        }
        sm.listenerCount = static_cast<std::uint16_t>(pkg.smListeners.size() - sm.firstListenerIdx);

        pkg.stateMachines.push_back(sm);
    }

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::RiveBinary, sourcePath, outIr);
    return true;
}

bool XamlFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    AeroCompiler compiler;
    AeroCompileInput in{};
    in.xamlPath = std::string(sourcePath);
    if (rawBytes && byteLength > 0) {
        in.xamlContent.assign(reinterpret_cast<const char*>(rawBytes), byteLength);
    }
    AeroCompileResult res = compiler.Compile(in);
    outDiagnostics.insert(outDiagnostics.end(), res.diagnostics.begin(), res.diagnostics.end());
    outIr = std::move(res.sceneIr);
    return res.succeeded;
}

AeroCompileResult AeroCompiler::Compile(const AeroCompileInput& input) const {
    AeroCompileResult result{};
    AxbPackage& pkg = result.axbPackage;
    pkg.InternString("");

    std::string rootDataType;
    const std::string_view src = input.xamlContent;

    // Multi-Frontend Importer Dispatch (.svg, Lottie .json/.lottie, Rive .riv)
    const bool isSvgInput = IsSvgAssetInput(input.xamlPath, src);
    const bool isLottieInput = !isSvgInput && IsLottieAssetInput(input.xamlPath, src);
    const bool isRiveInput = !isSvgInput && !isLottieInput && IsRiveAssetInput(input.xamlPath, src);
    if (isSvgInput || isLottieInput || isRiveInput) {
        bool importedOk = false;
        const auto* rawPtr = reinterpret_cast<const std::uint8_t*>(src.data());
        if (isSvgInput) {
            SvgTokenFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(input.xamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        } else if (isLottieInput) {
            LottieFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(input.xamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        } else {
            RiveFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(input.xamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        }
        if (!importedOk) {
            result.succeeded = false;
            return result;
        }
        pkg = result.sceneIr.loweredPackage;
    }

    std::size_t pos = (isSvgInput || isLottieInput || isRiveInput) ? src.size() : 0;
    bool seenRoot = false;
    std::unordered_set<std::uint64_t> bakedPathHashes;
    std::uint32_t nextInlineVsmStyleId = 0u;

    auto ensurePathBakedIntoPackage = [&](std::string_view svgPathData) {
        svgPathData = Trim(svgPathData);
        if (svgPathData.empty()) {
            return;
        }
        const std::uint64_t h = ComputeSvgPathHash(svgPathData);
        if (h == 0 || !bakedPathHashes.insert(h).second) {
            return;
        }
        const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(svgPathData);
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
        pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(svgPathData, adaptiveDim, 4.0f, slotIdx, 256u));
    };

    auto emitStaticRenderTransform = [&](const Matrix2D& m) {
        AxbInstruction linInst{};
        linInst.opcode = AxbOpcode::SetStaticProperty;
        linInst.propId = PropId_RenderTransform;
        linInst.literalValue = UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        pkg.instructions.push_back(std::move(linInst));

        AxbInstruction trInst{};
        trInst.opcode = AxbOpcode::SetStaticProperty;
        trInst.propId = PropId_RenderTransformTranslation;
        trInst.literalValue = UnifiedValue{Float4Value{m.tx, m.ty, 0.0f, 0.0f}};
        pkg.instructions.push_back(std::move(trInst));
    };

    auto emitStaticLayoutTransform = [&](const Matrix2D& m) {
        AxbInstruction ltInst{};
        ltInst.opcode = AxbOpcode::SetStaticProperty;
        ltInst.propId = PropId_LayoutTransform;
        ltInst.literalValue = UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        pkg.instructions.push_back(std::move(ltInst));
    };

    std::vector<XmlScopeEntry> scopeStack;
    std::vector<ActiveStyleScope> styleStack;
    std::vector<ActiveTriggerScope> triggerStack;
    std::vector<ActiveEventTriggerScope> eventTriggerStack;
    std::uint32_t nextInlineDataTplId = 0u;
    std::string dtRootTriggersStyleKey;
    std::vector<ActiveVsgScope> vsgStack;
    std::vector<ActiveVisualStateScope> visualStateStack;
    std::vector<ActiveStoryboardScope> storyboardStack;
    std::vector<ActiveTimelineScope> timelineStack;
    std::vector<ActiveKeyFrameScope> keyFrameStack;
    std::vector<ActiveSetterScope> setterStack;
    std::size_t nonVisualDepth = 0;

    auto findEnclosingTransformScope = [&]() noexcept -> XmlScopeEntry* {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::TransformPropertyElement) {
                return &(*it);
            }
            if (it->kind != XmlScopeKind::TransformChildNode) {
                break;
            }
        }
        return nullptr;
    };

    auto findLocalOrGlobalResource = [&](std::string_view key) -> const UnifiedValue* {
        for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
            if (it->first == key) {
                return &it->second;
            }
        }
        return AxbTemplateRegistry::Instance().FindResource(key);
    };

    auto findLocalOrGlobalStoryboard = [&](std::string_view key) -> const AxbStoryboard* {
        for (auto it = pkg.storyboards.rbegin(); it != pkg.storyboards.rend(); ++it) {
            if (it->key == key) {
                return &(*it);
            }
        }
        return AxbTemplateRegistry::Instance().FindStoryboard(key);
    };

    auto parseStyleOrAnimationValue = [&](std::uint16_t propId, std::string_view rawVal, bool& outValid) -> UnifiedValue {
        outValid = false;
        if (propId == kUnmappedPropId) {
            return UnifiedValue{};
        }
        const std::string_view trimmed = Trim(rawVal);
        if (trimmed.size() > 2u && trimmed.front() == '{' && trimmed.back() == '}') {
            const std::string_view resKey = ExtractMarkupResourceKey(trimmed);
            if (!resKey.empty()) {
                if (const UnifiedValue* rv = findLocalOrGlobalResource(resKey)) {
                    outValid = true;
                    if (propId == PropId_PathData) {
                        if (const auto* sv = std::get_if<StringValue>(rv)) {
                            ensurePathBakedIntoPackage(sv->view());
                        }
                    }
                    return *rv;
                }
            }
            return UnifiedValue{};
        }
        outValid = true;
        UnifiedValue parsed = ParseLiteralPropertyValue(propId, trimmed);
        if (propId == PropId_PathData) {
            if (const auto* sv = std::get_if<StringValue>(&parsed)) {
                ensurePathBakedIntoPackage(sv->view());
            }
        }
        return parsed;
    };

    auto commitSetterToOwner = [&](AxbPropertySetter&& s, SetterOwnerKind owner) {
        if (s.propId == kUnmappedPropId || std::holds_alternative<std::monostate>(s.value)) {
            return;
        }
        if (owner == SetterOwnerKind::VisualState && !visualStateStack.empty()) {
            visualStateStack.back().state.setters.push_back(std::move(s));
        } else if (owner == SetterOwnerKind::Trigger && !triggerStack.empty()) {
            triggerStack.back().trig.setters.push_back(std::move(s));
        } else if (owner == SetterOwnerKind::Style && !styleStack.empty()) {
            styleStack.back().def.setters.push_back(std::move(s));
        }
    };

    auto buildDataTriggerCondition = [&](std::string_view bindingAttr,
                                         std::string_view valAttr,
                                         bool hasValAttr,
                                         AxbTriggerCondition& outCond) -> bool {
        if (!hasValAttr) {
            return false;
        }
        const ParsedBindingClause parsed = ParseBindingMarkupExpression(bindingAttr);
        if (!parsed.isValidBinding) {
            return false;
        }
        const std::string_view trimmedVal = Trim(valAttr);
        outCond = AxbTriggerCondition{};
        outCond.converter = parsed.converter;
        if (trimmedVal.size() > 2u && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
            const std::string_view innerV = Trim(trimmedVal.substr(1u, trimmedVal.size() - 2u));
            if (innerV.rfind("x:Null", 0) == 0) {
                outCond.expectNull = true;
            } else {
                const std::string_view resK = ExtractMarkupResourceKey(trimmedVal);
                if (!resK.empty()) {
                    if (const UnifiedValue* rv = findLocalOrGlobalResource(resK)) {
                        outCond.expectedValue = *rv;
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }

        if (parsed.relativeSelf) {
            outCond.sourceKind = TriggerConditionSource::SelfProperty;
            outCond.propId = ResolveMappedTriggerConditionId(parsed.pathPart);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                bool ok = false;
                outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            }
            return true;
        }

        if (!parsed.elementName.empty()) {
            outCond.sourceKind = TriggerConditionSource::ElementNameProperty;
            outCond.sourceName = std::string(parsed.elementName);
            outCond.propId = ResolveMappedTriggerConditionId(parsed.pathPart);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                if (outCond.propId == PropId_SelectedItem || outCond.propId == PropId_Text ||
                    outCond.propId == PropId_Content || outCond.propId == PropId_Tag) {
                    outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                } else {
                    bool ok = false;
                    outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                    if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                }
            }
            return true;
        }

        if (!parsed.relativeAncestorType.empty()) {
            outCond.sourceKind = TriggerConditionSource::AncestorProperty;
            outCond.sourceName = parsed.relativeAncestorType;
            outCond.propId = ResolveMappedTriggerConditionId(parsed.pathPart);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                bool ok = false;
                outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            }
            return true;
        }

        // DataContext Binding (single-hop, multi-hop, or empty "{Binding}" to DataContext itself)
        outCond.sourceKind = TriggerConditionSource::DataContextBinding;
        outCond.bindingPath = std::string(parsed.pathPart);
        if (!parsed.pathPart.empty() && parsed.pathPart != ".") {
            std::size_t segPos = 0u;
            while (segPos < parsed.pathPart.size()) {
                const std::size_t dot = parsed.pathPart.find('.', segPos);
                const std::string_view seg = (dot == std::string_view::npos)
                    ? parsed.pathPart.substr(segPos)
                    : parsed.pathPart.substr(segPos, dot - segPos);
                if (!seg.empty()) {
                    AxbCompiledHop hop{};
                    hop.memberName = std::string(seg);
                    outCond.compiledHops.push_back(std::move(hop));
                }
                if (dot == std::string_view::npos) break;
                segPos = dot + 1u;
            }
        }
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
        }
        return true;
    };

    auto isNonVisualElementTag = [](std::string_view shortTag) noexcept -> bool {
        return shortTag == "Style" ||
               shortTag == "ControlTemplate" ||
               shortTag == "DataTemplate" ||
               shortTag == "ItemsPanelTemplate" ||
               shortTag == "Storyboard" ||
               shortTag == "BeginStoryboard" ||
               shortTag == "VisualStateGroup" ||
               shortTag == "VisualState" ||
               shortTag == "VisualTransition" ||
               shortTag == "EventTrigger" ||
               shortTag == "Trigger" ||
               shortTag == "DataTrigger" ||
               shortTag == "MultiTrigger" ||
               shortTag == "MultiDataTrigger" ||
               shortTag == "Condition" ||
               shortTag == "Setter" ||
               shortTag == "TransformGroup" ||
               shortTag == "ScaleTransform" ||
               shortTag == "RotateTransform" ||
               shortTag == "TranslateTransform" ||
               shortTag == "SkewTransform" ||
               shortTag == "MatrixTransform" ||
               shortTag == "Matrix" ||
               IsAnimationTimelineTag(shortTag) ||
               IsAnimationKeyFrameTag(shortTag) ||
               IsEasingFunctionTag(shortTag) ||
               shortTag == "Geometry" ||
               shortTag == "PathGeometry" ||
               shortTag == "StreamGeometry" ||
               shortTag == "RectangleGeometry" ||
               shortTag == "EllipseGeometry" ||
               shortTag == "CombinedGeometry" ||
               shortTag == "ImageBrush" ||
               shortTag == "LinearGradientBrush" ||
               shortTag == "RadialGradientBrush" ||
               shortTag == "SolidColorBrush" ||
               shortTag == "GradientStop" ||
               shortTag == "Binding" ||
               shortTag == "MultiBinding";
    };

    while (pos < src.size()) {
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) {
            break;
        }

        // Capture inner text between previous '>' and current '<'
        if (lt > pos && !scopeStack.empty()) {
            const std::string_view rawInner = Trim(src.substr(pos, lt - pos));
            if (!rawInner.empty()) {
                XmlScopeEntry& top = scopeStack.back();
                if (top.kind == XmlScopeKind::KeyedResourceItem ||
                    top.kind == XmlScopeKind::RunInline) {
                    if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                    top.accumulatedText += DecodeXamlXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::TransparentContainer &&
                           top.targetPropId != 0 && nonVisualDepth == 0) {
                    if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                    top.accumulatedText += DecodeXamlXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::VisualElement && nonVisualDepth == 0) {
                    const std::string_view st = StripXmlPrefix(top.tagName);
                    if (st == "TextBlock" || st == "Label" || st == "Button" ||
                        st == "RepeatButton" || st == "ToggleButton" ||
                        st == "CheckBox" || st == "RadioButton" || st == "Hyperlink" ||
                        st == "ComboBoxItem" || st == "ListBoxItem") {
                        if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                        top.accumulatedText += DecodeXamlXmlEntities(rawInner);
                    }
                }
            }
        }

        pos = lt + 1;
        if (pos >= src.size()) {
            break;
        }

        // Skip XML comments <!-- ... --> or declarations <? ... ?>
        if (src.substr(pos, 3) == "!--") {
            const std::size_t endComment = src.find("-->", pos + 3);
            pos = (endComment == std::string_view::npos) ? src.size() : (endComment + 3);
            continue;
        }
        if (src[pos] == '?') {
            const std::size_t endDecl = src.find("?>", pos + 1);
            pos = (endDecl == std::string_view::npos) ? src.size() : (endDecl + 2);
            continue;
        }

        // Closing tag </Tag>
        if (src[pos] == '/') {
            const std::size_t gt = src.find('>', pos + 1);
            if (!scopeStack.empty()) {
                const std::size_t closingDepth = scopeStack.size();

                if (!keyFrameStack.empty() && keyFrameStack.back().scopeDepth == closingDepth) {
                    ActiveKeyFrameScope finishedKf = std::move(keyFrameStack.back());
                    keyFrameStack.pop_back();
                    if (!timelineStack.empty() &&
                        !std::holds_alternative<std::monostate>(finishedKf.kf.value)) {
                        timelineStack.back().tl.keyFrames.push_back(std::move(finishedKf.kf));
                    }
                }

                if (!timelineStack.empty() && timelineStack.back().scopeDepth == closingDepth) {
                    ActiveTimelineScope finishedTl = std::move(timelineStack.back());
                    timelineStack.pop_back();
                    if (!storyboardStack.empty() && finishedTl.tl.targetPropertyId != kUnmappedPropId) {
                        std::sort(
                            finishedTl.tl.keyFrames.begin(),
                            finishedTl.tl.keyFrames.end(),
                            [](const AxbKeyFrame& a, const AxbKeyFrame& b) {
                                return a.timeSeconds < b.timeSeconds;
                            });
                        storyboardStack.back().sb.timelines.push_back(std::move(finishedTl.tl));
                    }
                }

                if (!storyboardStack.empty() && storyboardStack.back().scopeDepth == closingDepth) {
                    ActiveStoryboardScope finishedSb = std::move(storyboardStack.back());
                    storyboardStack.pop_back();
                    switch (finishedSb.owner) {
                        case StoryboardOwnerKind::StandaloneResource:
                            if (!finishedSb.sb.key.empty() || !finishedSb.sb.timelines.empty()) {
                                pkg.storyboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::TriggerEnter:
                            if (!triggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                triggerStack.back().trig.enterStoryboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::TriggerExit:
                            if (!triggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                triggerStack.back().trig.exitStoryboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::VisualState:
                            if (!visualStateStack.empty()) {
                                visualStateStack.back().state.storyboard = std::move(finishedSb.sb);
                            }
                            break;
                        case StoryboardOwnerKind::EventTrigger:
                            if (!eventTriggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                eventTriggerStack.back().et.storyboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::IgnoredEventTrigger:
                            break;
                    }
                }

                if (!setterStack.empty() && setterStack.back().scopeDepth == closingDepth) {
                    ActiveSetterScope finishedSetter = std::move(setterStack.back());
                    setterStack.pop_back();
                    if (finishedSetter.hasValue) {
                        commitSetterToOwner(std::move(finishedSetter.setter), finishedSetter.owner);
                    }
                }

                if (!visualStateStack.empty() && visualStateStack.back().scopeDepth == closingDepth) {
                    ActiveVisualStateScope finishedVs = std::move(visualStateStack.back());
                    visualStateStack.pop_back();
                    if (!vsgStack.empty() && !finishedVs.state.name.empty()) {
                        vsgStack.back().group.states.push_back(std::move(finishedVs.state));
                    }
                }

                if (!vsgStack.empty() && vsgStack.back().transitionsDepth == closingDepth) {
                    vsgStack.back().transitionsDepth = 0u;
                }
                if (!vsgStack.empty() && vsgStack.back().scopeDepth == closingDepth) {
                    ActiveVsgScope finishedVsg = std::move(vsgStack.back());
                    vsgStack.pop_back();
                    if (!styleStack.empty() && !finishedVsg.group.states.empty()) {
                        styleStack.back().def.visualStateGroups.push_back(std::move(finishedVsg.group));
                    }
                }

                if (!triggerStack.empty()) {
                    if (triggerStack.back().enterActionsDepth == closingDepth) {
                        triggerStack.back().enterActionsDepth = 0u;
                    }
                    if (triggerStack.back().exitActionsDepth == closingDepth) {
                        triggerStack.back().exitActionsDepth = 0u;
                    }
                    if (triggerStack.back().scopeDepth == closingDepth) {
                        ActiveTriggerScope finishedTrig = std::move(triggerStack.back());
                        triggerStack.pop_back();
                        if (!styleStack.empty() &&
                            !finishedTrig.hasUnsupportedCondition &&
                            !finishedTrig.trig.conditions.empty()) {
                            styleStack.back().def.triggers.push_back(std::move(finishedTrig.trig));
                        }
                    }
                }

                if (!eventTriggerStack.empty() && eventTriggerStack.back().scopeDepth == closingDepth) {
                    ActiveEventTriggerScope finishedEt = std::move(eventTriggerStack.back());
                    eventTriggerStack.pop_back();
                    if (!styleStack.empty() && finishedEt.recognized && !finishedEt.et.storyboards.empty()) {
                        styleStack.back().def.eventTriggers.push_back(std::move(finishedEt.et));
                    }
                }

                if (!styleStack.empty()) {
                    if (styleStack.back().controlTemplateTriggersDepth == closingDepth) {
                        styleStack.back().controlTemplateTriggersDepth = 0u;
                    }
                    if (styleStack.back().scopeDepth == closingDepth) {
                        ActiveStyleScope finishedStyle = std::move(styleStack.back());
                        styleStack.pop_back();
                        if (!finishedStyle.def.key.empty() || !finishedStyle.def.targetTypeName.empty()) {
                            bool mergedExisting = false;
                            if (!finishedStyle.def.key.empty()) {
                                for (auto& existing : pkg.styles) {
                                    if (existing.key == finishedStyle.def.key) {
                                        if (existing.targetTypeName.empty() && !finishedStyle.def.targetTypeName.empty()) {
                                            existing.targetTypeName = finishedStyle.def.targetTypeName;
                                        }
                                        if (existing.basedOnKey.empty() && !finishedStyle.def.basedOnKey.empty()) {
                                            existing.basedOnKey = finishedStyle.def.basedOnKey;
                                        }
                                        existing.setters.insert(
                                            existing.setters.end(),
                                            finishedStyle.def.setters.begin(),
                                            finishedStyle.def.setters.end());
                                        existing.triggers.insert(
                                            existing.triggers.end(),
                                            finishedStyle.def.triggers.begin(),
                                            finishedStyle.def.triggers.end());
                                        existing.eventTriggers.insert(
                                            existing.eventTriggers.end(),
                                            finishedStyle.def.eventTriggers.begin(),
                                            finishedStyle.def.eventTriggers.end());
                                        existing.visualStateGroups.insert(
                                            existing.visualStateGroups.end(),
                                            finishedStyle.def.visualStateGroups.begin(),
                                            finishedStyle.def.visualStateGroups.end());
                                        mergedExisting = true;
                                        break;
                                    }
                                }
                            }
                            if (!mergedExisting) {
                                pkg.styles.push_back(std::move(finishedStyle.def));
                            }
                        }
                    }
                }

                XmlScopeEntry closed = std::move(scopeStack.back());
                scopeStack.pop_back();
                if (closed.kind == XmlScopeKind::ResourceContainer ||
                    closed.kind == XmlScopeKind::KeyedResourceItem ||
                    closed.kind == XmlScopeKind::BrushPropertyElement ||
                    closed.kind == XmlScopeKind::TransformPropertyElement ||
                    closed.kind == XmlScopeKind::TransformChildNode ||
                    closed.kind == XmlScopeKind::NonVisualSkip) {
                    if (nonVisualDepth > 0) {
                        --nonVisualDepth;
                    }
                }
                if (closed.kind == XmlScopeKind::TransformPropertyElement) {
                    if (closed.targetPropId == PropId_LayoutTransform) {
                        emitStaticLayoutTransform(closed.accumulatedTransform);
                    } else {
                        emitStaticRenderTransform(closed.accumulatedTransform);
                    }
                } else if (closed.kind == XmlScopeKind::KeyedResourceItem &&
                           !closed.resourceKey.empty()) {
                    const std::string_view st = StripXmlPrefix(closed.tagName);
                    if (st == "Color" && !closed.accumulatedText.empty()) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromColor(ParseXamlColorLiteral(closed.accumulatedText)));
                    } else if ((st == "Double" || st == "Single") && !closed.accumulatedText.empty()) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            MakeFloat(ParseFloat32(closed.accumulatedText, 0.0f)));
                    } else if ((st == "Geometry" || st == "StreamGeometry" || st == "PathGeometry") &&
                               !closed.accumulatedText.empty()) {
                        ensurePathBakedIntoPackage(closed.accumulatedText);
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromString(closed.accumulatedText));
                    } else if (st == "String" || (!closed.brushAssigned && !closed.accumulatedText.empty())) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromString(closed.accumulatedText));
                    }
                } else if (closed.kind == XmlScopeKind::TransparentContainer &&
                           closed.targetPropId != 0 && !closed.accumulatedText.empty()) {
                    if (closed.targetPropId == PropId_PathData) {
                        ensurePathBakedIntoPackage(closed.accumulatedText);
                    }
                    AxbInstruction propTextInst{};
                    propTextInst.opcode = AxbOpcode::SetStaticProperty;
                    propTextInst.propId = closed.targetPropId;
                    propTextInst.literalValue = value::FromString(std::move(closed.accumulatedText));
                    pkg.instructions.push_back(std::move(propTextInst));
                } else if (closed.kind == XmlScopeKind::RunInline && !closed.accumulatedText.empty()) {
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::VisualElement) {
                            if (!it->accumulatedText.empty()) it->accumulatedText.push_back(' ');
                            it->accumulatedText += closed.accumulatedText;
                            break;
                        }
                    }
                } else if (closed.kind == XmlScopeKind::VisualElement) {
                    if (!closed.accumulatedText.empty()) {
                        AxbInstruction textInst{};
                        textInst.opcode = AxbOpcode::SetStaticProperty;
                        textInst.propId = PropId_Text;
                        textInst.literalValue = value::FromString(std::move(closed.accumulatedText));
                        pkg.instructions.push_back(std::move(textInst));
                    }
                    AxbInstruction endInst{};
                    endInst.opcode = AxbOpcode::EndElement;
                    pkg.instructions.push_back(std::move(endInst));
                } else if (closed.kind == XmlScopeKind::ResourceContainer && scopeStack.empty() &&
                           pkg.rootTypeName == "ResourceDictionary") {
                    AxbInstruction endInst{};
                    endInst.opcode = AxbOpcode::EndElement;
                    pkg.instructions.push_back(std::move(endInst));
                }
            }
            pos = (gt == std::string_view::npos) ? src.size() : (gt + 1);
            continue;
        }

        // Opening tag <Tag attr="val" ...> or <Tag ... />
        std::size_t tagEnd = pos;
        while (tagEnd < src.size() &&
               !std::isspace(static_cast<unsigned char>(src[tagEnd])) &&
               src[tagEnd] != '>' && src[tagEnd] != '/') {
            ++tagEnd;
        }
        const std::string_view tagName = src.substr(pos, tagEnd - pos);
        const std::string_view shortTag = StripXmlPrefix(tagName);
        pos = tagEnd;

        struct ParsedAttr {
            std::string_view name;
            std::string_view value;
        };
        std::vector<ParsedAttr> attrs;
        std::string_view xName;
        std::string_view xKey;
        bool selfClosing = false;

        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            if (pos >= src.size()) break;
            if (src[pos] == '>') {
                ++pos;
                break;
            }
            if (src.substr(pos, 2) == "/>") {
                selfClosing = true;
                pos += 2;
                break;
            }

            const std::size_t attrStart = pos;
            while (pos < src.size() && src[pos] != '=' &&
                   !std::isspace(static_cast<unsigned char>(src[pos])) &&
                   src[pos] != '>' && src[pos] != '/') {
                ++pos;
            }
            const std::string_view attrName = src.substr(attrStart, pos - attrStart);
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            std::string_view attrVal;
            if (pos < src.size() && src[pos] == '=') {
                ++pos;
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                    ++pos;
                }
                if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
                    const char quote = src[pos++];
                    const std::size_t valStart = pos;
                    while (pos < src.size() && src[pos] != quote) {
                        ++pos;
                    }
                    attrVal = src.substr(valStart, pos - valStart);
                    if (pos < src.size() && src[pos] == quote) {
                        ++pos;
                    }
                }
            }
            if (!attrName.empty()) {
                if (attrName == "x:Class") {
                    if (!seenRoot && nonVisualDepth == 0) {
                        pkg.className = std::string(attrVal);
                    }
                } else if (attrName == "x:DataType") {
                    if (!seenRoot && nonVisualDepth == 0) {
                        rootDataType = std::string(attrVal);
                    }
                } else if (attrName == "x:Name" || attrName == "Name") {
                    xName = attrVal;
                } else if (attrName == "x:Key") {
                    xKey = attrVal;
                } else if (attrName.rfind("xmlns", 0) == 0 || attrName == "mc:Ignorable") {
                    // Ignore XML namespace declarations
                } else {
                    attrs.push_back(ParsedAttr{attrName, attrVal});
                }
            }
        }

        // Helper to push a NonVisualSkip scope and return the new scopeStack depth
        auto pushNonVisualScope = [&](XmlScopeKind sk = XmlScopeKind::NonVisualSkip) -> std::size_t {
            XmlScopeEntry entry{};
            entry.kind = sk;
            entry.tagName = std::string(tagName);
            scopeStack.push_back(std::move(entry));
            ++nonVisualDepth;
            return scopeStack.size();
        };

        // Inside non-visual scope: handle Style/Trigger/Storyboard/VSM, resource extraction, 2D transforms, or brushes
        if (nonVisualDepth > 0) {
            XmlScopeEntry& parentScope = scopeStack.back();

            // 1. Active <Element.RenderTransform> / <Element.LayoutTransform> subtree
            if (XmlScopeEntry* tfScope = findEnclosingTransformScope()) {
                auto resolveTransformResource = [&](std::string_view rawExpr, std::string_view attrName) -> const UnifiedValue* {
                    const std::string_view v = Trim(rawExpr);
                    if (v.size() <= 2 || v.front() != '{' || v.back() != '}') {
                        return nullptr;
                    }
                    const std::string_view inner = Trim(v.substr(1, v.size() - 2));
                    if (inner.rfind("StaticResource ", 0) == 0 || inner.rfind("DynamicResource ", 0) == 0) {
                        std::string_view keyPart = (inner.rfind("StaticResource ", 0) == 0)
                            ? Trim(inner.substr(15))
                            : Trim(inner.substr(16));
                        if (keyPart.rfind("ResourceKey=", 0) == 0) {
                            keyPart = Trim(keyPart.substr(12));
                        }
                        if (const UnifiedValue* rv = findLocalOrGlobalResource(keyPart)) {
                            return rv;
                        }
                        result.diagnostics.push_back(
                            "AERO_XAML_TRANSFORM_RESOURCE: Unresolved resource '" +
                            std::string(keyPart) + "' on <" + std::string(shortTag) + " " +
                            std::string(attrName) + ">");
                        return nullptr;
                    }
                    if (inner == "Binding" || inner == "Bind" ||
                        inner.rfind("Binding ", 0) == 0 ||
                        inner.rfind("Bind ", 0) == 0 ||
                        inner.rfind("EcsBind ", 0) == 0) {
                        result.diagnostics.push_back(
                            "AERO_XAML_TRANSFORM_BINDING: Dynamic binding on child transform element '<" +
                            std::string(shortTag) + " " + std::string(attrName) +
                            ">' is not supported; bind RenderTransform or LayoutTransform directly on the UIElement.");
                    }
                    return nullptr;
                };

                auto evalFloatAttr = [&](const ParsedAttr& a, float fallback, bool& outResolved) -> float {
                    const std::string_view v = Trim(a.value);
                    if (v.empty()) {
                        outResolved = false;
                        return fallback;
                    }
                    if (v.front() != '{') {
                        outResolved = true;
                        return ParseFloat32(v, fallback);
                    }
                    if (const UnifiedValue* rv = resolveTransformResource(v, a.name)) {
                        float fv = fallback;
                        if (TryGetFloat(*rv, fv)) {
                            outResolved = true;
                            return fv;
                        }
                        if (const auto* sv = std::get_if<StringValue>(rv)) {
                            outResolved = true;
                            return ParseFloat32(sv->view(), fallback);
                        }
                    }
                    outResolved = false;
                    return fallback;
                };

                Matrix2D step = Matrix2D::Identity();
                bool hasStep = false;
                if (shortTag == "ScaleTransform") {
                    float sx = 1.0f, sy = 1.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "ScaleX") sx = evalFloatAttr(a, 1.0f, ok);
                        else if (a.name == "ScaleY") sy = evalFloatAttr(a, 1.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Scaling(sx, sy, cx, cy);
                    hasStep = true;
                } else if (shortTag == "RotateTransform") {
                    float angle = 0.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "Angle") angle = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Rotation(angle, cx, cy);
                    hasStep = true;
                } else if (shortTag == "TranslateTransform") {
                    float dx = 0.0f, dy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "X") dx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "Y") dy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Translation(dx, dy);
                    hasStep = true;
                } else if (shortTag == "SkewTransform") {
                    float ax = 0.0f, ay = 0.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "AngleX") ax = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "AngleY") ay = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Skew(ax, ay, cx, cy);
                    hasStep = true;
                } else if (shortTag == "MatrixTransform" || shortTag == "Matrix") {
                    bool parsedMatrixStr = false;
                    float m11 = 1.0f, m12 = 0.0f, m21 = 0.0f, m22 = 1.0f, ox = 0.0f, oy = 0.0f;
                    bool anyElemAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Matrix") {
                            const std::string_view mv = Trim(a.value);
                            if (!mv.empty() && mv.front() != '{') {
                                step = ParseXamlMatrix2DLiteral(mv);
                                parsedMatrixStr = true;
                                hasStep = true;
                                break;
                            }
                            if (const UnifiedValue* rv = resolveTransformResource(mv, a.name)) {
                                if (const auto* sv = std::get_if<StringValue>(rv)) {
                                    step = ParseXamlMatrix2DLiteral(sv->view());
                                    parsedMatrixStr = true;
                                    hasStep = true;
                                    break;
                                }
                            }
                            continue;
                        }
                        bool ok = false;
                        if (a.name == "M11") { m11 = evalFloatAttr(a, 1.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M12") { m12 = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M21") { m21 = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M22") { m22 = evalFloatAttr(a, 1.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "OffsetX") { ox = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "OffsetY") { oy = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                    }
                    if (!parsedMatrixStr && anyElemAttr) {
                        step = Matrix2D::FromWpfMatrix(m11, m12, m21, m22, ox, oy);
                        hasStep = true;
                    }
                }

                if (hasStep) {
                    tfScope->accumulatedTransform = Matrix2D::Multiply(step, tfScope->accumulatedTransform);
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope(XmlScopeKind::TransformChildNode);
                }
                continue;
            }

            // 2. Phase 5C: <Style>, <Trigger>, <MultiTrigger>, <VisualStateGroup>, <Storyboard>, <Setter>, <*Ease>
            if (shortTag == "Style") {
                if (!styleStack.empty() && styleStack.back().isInlinePropertyElement) {
                    styleStack.back().isInlinePropertyElement = false;
                    if (!xKey.empty()) {
                        styleStack.back().def.key = std::string(xKey);
                    }
                    for (const auto& a : attrs) {
                        if (a.name == "TargetType") {
                            styleStack.back().def.targetTypeName = ExtractTargetTypeShortName(a.value);
                        } else if (a.name == "BasedOn") {
                            const std::string_view bKey = ExtractMarkupResourceKey(a.value);
                            styleStack.back().def.basedOnKey = !bKey.empty()
                                ? std::string(bKey)
                                : std::string(Trim(a.value));
                        }
                    }
                    if (!selfClosing) {
                        (void)pushNonVisualScope();
                    }
                    continue;
                }

                AxbStyleDefinition styleDef{};
                styleDef.key = std::string(xKey);
                for (const auto& a : attrs) {
                    if (a.name == "TargetType") {
                        styleDef.targetTypeName = ExtractTargetTypeShortName(a.value);
                    } else if (a.name == "BasedOn") {
                        const std::string_view bKey = ExtractMarkupResourceKey(a.value);
                        styleDef.basedOnKey = !bKey.empty()
                            ? std::string(bKey)
                            : std::string(Trim(a.value));
                    }
                }
                if (selfClosing) {
                    if (!styleDef.key.empty() || !styleDef.targetTypeName.empty()) {
                        pkg.styles.push_back(std::move(styleDef));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveStyleScope sc{};
                    sc.def = std::move(styleDef);
                    sc.scopeDepth = depth;
                    styleStack.push_back(std::move(sc));
                }
                continue;
            }

            if (shortTag == "ControlTemplate.Triggers") {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    if (!styleStack.empty()) {
                        styleStack.back().controlTemplateTriggersDepth = depth;
                    }
                }
                continue;
            }

            // Phase 5D: Compile <DataTemplate> inside ResourceDictionary or <ItemsControl.ItemTemplate>
            if (shortTag == "DataTemplate" &&
                (parentScope.kind == XmlScopeKind::ResourceContainer || parentScope.isItemTemplateContainer) &&
                styleStack.empty()) {
                std::string dtDataType;
                for (const auto& a : attrs) {
                    if (a.name == "DataType") {
                        dtDataType = ExtractTargetTypeShortName(a.value);
                    }
                }
                if (!selfClosing) {
                    std::size_t innerEnd = 0u;
                    std::size_t afterClose = 0u;
                    if (FindMatchingClosingTag(src, pos, "DataTemplate", innerEnd, afterClose)) {
                        const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                        const std::string outerScopeName = !pkg.className.empty()
                            ? pkg.className
                            : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                        std::string dtKey;
                        if (parentScope.isItemTemplateContainer) {
                            dtKey = "__item_tpl_" + outerScopeName + "_" +
                                    std::to_string(nextInlineDataTplId++);
                        } else if (!xKey.empty()) {
                            dtKey = std::string(xKey);
                        } else {
                            dtKey = "__implicit_dt_" + outerScopeName + "_" +
                                    (dtDataType.empty() ? std::to_string(nextInlineDataTplId++) : dtDataType);
                        }
                        AxbTemplateRegistry::Instance().RegisterResources(pkg);
                        AeroCompileInput subIn{};
                        subIn.xamlPath = dtKey;
                        subIn.xamlContent = std::string(innerXml);
                        AeroCompileResult subRes = this->Compile(subIn);
                        if (subRes.succeeded && !subRes.axbPackage.instructions.empty()) {
                            AxbDataTemplate dt{};
                            if (parentScope.isItemTemplateContainer) {
                                dt.key = dtKey;
                                AxbInstruction setTplInst{};
                                setTplInst.opcode = AxbOpcode::SetStaticProperty;
                                setTplInst.propId = PropId_ItemTemplate;
                                setTplInst.literalValue = value::FromString(dt.key);
                                pkg.instructions.push_back(std::move(setTplInst));
                            } else {
                                dt.key = std::string(xKey);
                            }
                            dt.dataTypeName = std::move(dtDataType);
                            dt.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                            pkg.dataTemplates.push_back(std::move(dt));
                        }
                        pos = afterClose;
                        continue;
                    }
                } else {
                    continue;
                }
            }

            if (shortTag == "EventTrigger" && !styleStack.empty()) {
                AxbEventTrigger et{};
                bool rec = false;
                for (const auto& a : attrs) {
                    if (a.name == "RoutedEvent") {
                        et.eventKind = ParseRoutedTriggerEvent(a.value, rec);
                    } else if (a.name == "SourceName") {
                        et.sourceName = std::string(Trim(a.value));
                    }
                }
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveEventTriggerScope ets{};
                    ets.et = std::move(et);
                    ets.scopeDepth = depth;
                    ets.recognized = rec;
                    eventTriggerStack.push_back(std::move(ets));
                }
                continue;
            }

            if ((shortTag == "Trigger" || shortTag == "MultiTrigger" ||
                 shortTag == "DataTrigger" || shortTag == "MultiDataTrigger") &&
                !styleStack.empty()) {
                AxbStyleTrigger trig{};
                trig.overrideLocalValues = (styleStack.back().controlTemplateTriggersDepth != 0u);
                const bool isData = (shortTag == "DataTrigger" || shortTag == "MultiDataTrigger");
                bool unsupportedCond = false;

                if (shortTag == "Trigger") {
                    std::string_view propAttr;
                    std::string_view valAttr;
                    bool hasValAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Property") {
                            propAttr = a.value;
                        } else if (a.name == "Value") {
                            valAttr = a.value;
                            hasValAttr = true;
                        }
                    }
                    const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr);
                    if (condPropId == kUnmappedPropId || !hasValAttr) {
                        unsupportedCond = true;
                    } else {
                        bool ok = false;
                        UnifiedValue expVal = parseStyleOrAnimationValue(condPropId, valAttr, ok);
                        if (!ok) {
                            unsupportedCond = true;
                        } else {
                            AxbTriggerCondition c{};
                            c.propId = condPropId;
                            c.expectedValue = std::move(expVal);
                            trig.conditions.push_back(std::move(c));
                        }
                    }
                } else if (shortTag == "DataTrigger") {
                    std::string_view bindAttr;
                    std::string_view valAttr;
                    bool hasValAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Binding") {
                            bindAttr = a.value;
                        } else if (a.name == "Value") {
                            valAttr = a.value;
                            hasValAttr = true;
                        }
                    }
                    AxbTriggerCondition dtCond{};
                    if (buildDataTriggerCondition(bindAttr, valAttr, hasValAttr, dtCond)) {
                        trig.conditions.push_back(std::move(dtCond));
                    } else {
                        unsupportedCond = true;
                    }
                }

                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveTriggerScope ts{};
                    ts.trig = std::move(trig);
                    ts.scopeDepth = depth;
                    ts.isDataTrigger = isData;
                    ts.hasUnsupportedCondition = unsupportedCond;
                    triggerStack.push_back(std::move(ts));
                }
                continue;
            }

            if (shortTag == "Condition" && !triggerStack.empty()) {
                std::string_view propAttr;
                std::string_view bindAttr;
                std::string_view valAttr;
                bool hasBindAttr = false;
                bool hasValAttr = false;
                for (const auto& a : attrs) {
                    if (a.name == "Property") propAttr = a.value;
                    else if (a.name == "Binding") {
                        bindAttr = a.value;
                        hasBindAttr = true;
                    } else if (a.name == "Value") {
                        valAttr = a.value;
                        hasValAttr = true;
                    }
                }
                if (!triggerStack.back().hasUnsupportedCondition) {
                    if (hasBindAttr || triggerStack.back().isDataTrigger) {
                        AxbTriggerCondition dtCond{};
                        if (buildDataTriggerCondition(bindAttr, valAttr, hasValAttr, dtCond)) {
                            triggerStack.back().trig.conditions.push_back(std::move(dtCond));
                        } else {
                            triggerStack.back().hasUnsupportedCondition = true;
                        }
                    } else {
                        const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr);
                        if (condPropId == kUnmappedPropId || !hasValAttr) {
                            triggerStack.back().hasUnsupportedCondition = true;
                        } else {
                            bool ok = false;
                            UnifiedValue expVal = parseStyleOrAnimationValue(condPropId, valAttr, ok);
                            if (!ok) {
                                triggerStack.back().hasUnsupportedCondition = true;
                            } else {
                                AxbTriggerCondition c{};
                                c.propId = condPropId;
                                c.expectedValue = std::move(expVal);
                                triggerStack.back().trig.conditions.push_back(std::move(c));
                            }
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if ((EndsWith(shortTag, ".EnterActions") || EndsWith(shortTag, ".ExitActions")) &&
                !triggerStack.empty()) {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    if (EndsWith(shortTag, ".EnterActions")) {
                        triggerStack.back().enterActionsDepth = depth;
                    } else {
                        triggerStack.back().exitActionsDepth = depth;
                    }
                }
                continue;
            }

            if (shortTag == "BeginStoryboard") {
                for (const auto& a : attrs) {
                    if (a.name == "Storyboard") {
                        const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                        if (!sbKey.empty()) {
                            if (const AxbStoryboard* foundSb = findLocalOrGlobalStoryboard(sbKey)) {
                                if (!eventTriggerStack.empty()) {
                                    eventTriggerStack.back().et.storyboards.push_back(*foundSb);
                                } else if (!triggerStack.empty()) {
                                    if (triggerStack.back().exitActionsDepth != 0u) {
                                        triggerStack.back().trig.exitStoryboards.push_back(*foundSb);
                                    } else if (triggerStack.back().enterActionsDepth != 0u) {
                                        triggerStack.back().trig.enterStoryboards.push_back(*foundSb);
                                    }
                                }
                            }
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "VisualStateGroup" && !styleStack.empty()) {
                AxbVisualStateGroup group{};
                group.name = std::string(xName);
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveVsgScope gs{};
                    gs.group = std::move(group);
                    gs.scopeDepth = depth;
                    vsgStack.push_back(std::move(gs));
                }
                continue;
            }

            if (shortTag == "VisualStateGroup.Transitions" && !vsgStack.empty()) {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    vsgStack.back().transitionsDepth = depth;
                }
                continue;
            }

            if (shortTag == "VisualTransition" && !vsgStack.empty()) {
                std::string_view fromState;
                std::string_view toState;
                for (const auto& a : attrs) {
                    if (a.name == "From") fromState = Trim(a.value);
                    else if (a.name == "To") toState = Trim(a.value);
                    else if (a.name == "GeneratedDuration") {
                        const float dur = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                        if ((fromState.empty() && toState.empty()) ||
                            vsgStack.back().group.defaultTransitionDuration <= 0.0f) {
                            vsgStack.back().group.defaultTransitionDuration = dur;
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "VisualState" && !vsgStack.empty()) {
                AxbVisualState vs{};
                vs.name = std::string(xName);
                if (selfClosing) {
                    if (!vs.name.empty()) {
                        vsgStack.back().group.states.push_back(std::move(vs));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveVisualStateScope vss{};
                    vss.state = std::move(vs);
                    vss.scopeDepth = depth;
                    visualStateStack.push_back(std::move(vss));
                }
                continue;
            }

            if (shortTag == "Storyboard") {
                StoryboardOwnerKind sbOwner = StoryboardOwnerKind::IgnoredEventTrigger;
                if (!visualStateStack.empty()) {
                    sbOwner = StoryboardOwnerKind::VisualState;
                } else if (!eventTriggerStack.empty()) {
                    sbOwner = StoryboardOwnerKind::EventTrigger;
                } else if (!triggerStack.empty() && triggerStack.back().exitActionsDepth != 0u) {
                    sbOwner = StoryboardOwnerKind::TriggerExit;
                } else if (!triggerStack.empty() && triggerStack.back().enterActionsDepth != 0u) {
                    sbOwner = StoryboardOwnerKind::TriggerEnter;
                } else if (parentScope.kind == XmlScopeKind::ResourceContainer) {
                    sbOwner = StoryboardOwnerKind::StandaloneResource;
                }

                AxbStoryboard sb{};
                sb.key = std::string(xKey);
                std::string defTargetName;
                std::string defTargetProp;
                for (const auto& a : attrs) {
                    if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                        defTargetName = std::string(Trim(a.value));
                    } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                        defTargetProp = std::string(Trim(a.value));
                    }
                }

                if (selfClosing) {
                    if (sbOwner == StoryboardOwnerKind::StandaloneResource && !sb.key.empty()) {
                        pkg.storyboards.push_back(std::move(sb));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveStoryboardScope sbs{};
                    sbs.sb = std::move(sb);
                    sbs.defaultTargetName = std::move(defTargetName);
                    sbs.defaultTargetProperty = std::move(defTargetProp);
                    sbs.owner = sbOwner;
                    sbs.scopeDepth = depth;
                    storyboardStack.push_back(std::move(sbs));
                }
                continue;
            }

            if (IsAnimationTimelineTag(shortTag) && !storyboardStack.empty()) {
                AxbAnimationTimeline tl{};
                tl.targetName = storyboardStack.back().defaultTargetName;
                std::string rawTargetProp = storyboardStack.back().defaultTargetProperty;
                std::string_view rawFrom;
                std::string_view rawTo;
                std::string_view rawBy;
                bool hasFromAttr = false;
                bool hasToAttr = false;
                bool hasByAttr = false;

                for (const auto& a : attrs) {
                    if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                        tl.targetName = std::string(Trim(a.value));
                    } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                        rawTargetProp = std::string(Trim(a.value));
                    } else if (a.name == "BeginTime") {
                        tl.beginTimeSeconds = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                    } else if (a.name == "Duration") {
                        tl.durationSeconds = ParseXamlTimeSpanSeconds(a.value, 0.25f);
                    } else if (a.name == "From") {
                        rawFrom = a.value;
                        hasFromAttr = true;
                    } else if (a.name == "To") {
                        rawTo = a.value;
                        hasToAttr = true;
                    } else if (a.name == "By") {
                        rawBy = a.value;
                        hasByAttr = true;
                    } else if (a.name == "AutoReverse") {
                        tl.autoReverse = EqualsIgnoreCase(Trim(a.value), "true") || Trim(a.value) == "1";
                    } else if (a.name == "RepeatBehavior") {
                        tl.repeatCount = ParseRepeatCountAttr(a.value);
                    } else if (a.name == "FillBehavior") {
                        tl.fillBehavior = EqualsIgnoreCase(Trim(a.value), "Stop")
                            ? FillBehavior::Stop
                            : FillBehavior::HoldEnd;
                    }
                }

                tl.targetPropertyId = ResolveMappedAnimationPropertyId(rawTargetProp);
                if (tl.targetPropertyId != kUnmappedPropId) {
                    if (hasFromAttr) {
                        bool ok = false;
                        tl.fromValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawFrom, ok);
                        tl.hasFrom = ok;
                    }
                    if (hasToAttr) {
                        bool ok = false;
                        tl.toValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawTo, ok);
                        tl.hasTo = ok;
                        tl.isBy = false;
                    } else if (hasByAttr) {
                        bool ok = false;
                        tl.toValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawBy, ok);
                        tl.hasTo = ok;
                        tl.isBy = ok;
                    }
                }

                if (selfClosing) {
                    if (tl.targetPropertyId != kUnmappedPropId) {
                        storyboardStack.back().sb.timelines.push_back(std::move(tl));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveTimelineScope tls{};
                    tls.tl = std::move(tl);
                    tls.scopeDepth = depth;
                    timelineStack.push_back(std::move(tls));
                }
                continue;
            }

            if (IsAnimationKeyFrameTag(shortTag) && !timelineStack.empty()) {
                AxbKeyFrame kf{};
                kf.isDiscrete =
                    (shortTag.rfind("Discrete", 0) == 0) ||
                    EndsWith(shortTag, "BooleanKeyFrame") ||
                    EndsWith(shortTag, "ObjectKeyFrame");
                if (shortTag.rfind("Spline", 0) == 0) {
                    kf.easingKind = EasingFunctionKind::Cubic;
                    kf.easingMode = EasingMode::EaseInOut;
                }
                const std::uint16_t targetPropId = timelineStack.back().tl.targetPropertyId;
                for (const auto& a : attrs) {
                    if (a.name == "KeyTime") {
                        kf.timeSeconds = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                    } else if (a.name == "Value" && targetPropId != kUnmappedPropId) {
                        bool ok = false;
                        UnifiedValue parsed = parseStyleOrAnimationValue(targetPropId, a.value, ok);
                        if (ok) {
                            kf.value = std::move(parsed);
                        }
                    }
                }
                if (selfClosing) {
                    if (!std::holds_alternative<std::monostate>(kf.value)) {
                        timelineStack.back().tl.keyFrames.push_back(std::move(kf));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveKeyFrameScope kfs{};
                    kfs.kf = std::move(kf);
                    kfs.scopeDepth = depth;
                    keyFrameStack.push_back(std::move(kfs));
                }
                continue;
            }

            if (IsEasingFunctionTag(shortTag)) {
                EasingFunctionKind ek = ParseEasingFunctionKind(shortTag);
                if (shortTag == "PowerEase") {
                    ek = EasingFunctionKind::Quadratic;
                    for (const auto& a : attrs) {
                        if (a.name == "Power") {
                            const int p = static_cast<int>(std::lround(ParseFloat32(a.value, 2.0f)));
                            if (p <= 1) ek = EasingFunctionKind::Linear;
                            else if (p == 2) ek = EasingFunctionKind::Quadratic;
                            else if (p == 3) ek = EasingFunctionKind::Cubic;
                            else if (p == 4) ek = EasingFunctionKind::Quartic;
                            else ek = EasingFunctionKind::Quintic;
                        }
                    }
                }
                EasingMode em = EasingMode::EaseOut;
                for (const auto& a : attrs) {
                    if (a.name == "EasingMode") {
                        em = ParseEasingMode(a.value);
                    }
                }
                if (!keyFrameStack.empty()) {
                    keyFrameStack.back().kf.easingKind = ek;
                    keyFrameStack.back().kf.easingMode = em;
                } else if (!timelineStack.empty()) {
                    timelineStack.back().tl.easingKind = ek;
                    timelineStack.back().tl.easingMode = em;
                } else if (!vsgStack.empty()) {
                    vsgStack.back().group.defaultEasingKind = ek;
                    vsgStack.back().group.defaultEasingMode = em;
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "Setter" &&
                (!visualStateStack.empty() || !triggerStack.empty() || !styleStack.empty())) {
                SetterOwnerKind owner = SetterOwnerKind::Style;
                if (!visualStateStack.empty()) {
                    owner = SetterOwnerKind::VisualState;
                } else if (!triggerStack.empty()) {
                    owner = SetterOwnerKind::Trigger;
                }

                AxbPropertySetter setter{};
                setter.propId = kUnmappedPropId;
                std::string_view rawVal;
                bool hasValAttr = false;
                for (const auto& a : attrs) {
                    if (a.name == "Property") {
                        setter.propId = ResolveMappedSetterPropertyId(a.value);
                    } else if (a.name == "TargetName") {
                        setter.targetName = std::string(Trim(a.value));
                    } else if (a.name == "Value") {
                        rawVal = a.value;
                        hasValAttr = true;
                    }
                }

                bool hasValidVal = false;
                if (setter.propId != kUnmappedPropId && hasValAttr) {
                    setter.value = parseStyleOrAnimationValue(setter.propId, rawVal, hasValidVal);
                }

                if (selfClosing) {
                    if (hasValidVal) {
                        commitSetterToOwner(std::move(setter), owner);
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveSetterScope ss{};
                    ss.setter = std::move(setter);
                    ss.owner = owner;
                    ss.hasValue = hasValidVal;
                    ss.scopeDepth = depth;
                    setterStack.push_back(std::move(ss));
                }
                continue;
            }

            if (!setterStack.empty() && !setterStack.back().hasValue &&
                setterStack.back().setter.propId != kUnmappedPropId &&
                (shortTag == "SolidColorBrush" || shortTag == "GradientStop")) {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        setterStack.back().setter.value = value::FromColor(ParseXamlColorLiteral(a.value));
                        setterStack.back().hasValue = true;
                        break;
                    }
                }
            }

            // 3. Keyed ResourceDictionary items (Brushes, Colors, Doubles, Strings, SVG Geometries)
            if (parentScope.kind == XmlScopeKind::ResourceContainer) {
                if (!xKey.empty()) {
                    if (shortTag == "SolidColorBrush") {
                        for (const auto& a : attrs) {
                            if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                                pkg.resources.emplace_back(
                                    std::string(xKey),
                                    value::FromColor(ParseXamlColorLiteral(a.value)));
                                break;
                            }
                        }
                    } else if ((shortTag == "Geometry" || shortTag == "StreamGeometry" || shortTag == "PathGeometry")) {
                        bool handledAttr = false;
                        for (const auto& a : attrs) {
                            if (a.name == "Figures" || a.name == "Data") {
                                const std::string decoded = DecodeXamlXmlEntities(a.value);
                                ensurePathBakedIntoPackage(decoded);
                                pkg.resources.emplace_back(std::string(xKey), value::FromString(decoded));
                                handledAttr = true;
                                break;
                            }
                        }
                        if (!selfClosing && !handledAttr) {
                            XmlScopeEntry itemScope{};
                            itemScope.kind = XmlScopeKind::KeyedResourceItem;
                            itemScope.tagName = std::string(tagName);
                            itemScope.resourceKey = std::string(xKey);
                            scopeStack.push_back(std::move(itemScope));
                            ++nonVisualDepth;
                            continue;
                        }
                    } else if (!selfClosing &&
                               (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush" ||
                                shortTag == "Color" || shortTag == "String" ||
                                shortTag == "Double" || shortTag == "Single" ||
                                shortTag == "FlowDirection")) {
                        XmlScopeEntry itemScope{};
                        itemScope.kind = XmlScopeKind::KeyedResourceItem;
                        itemScope.tagName = std::string(tagName);
                        itemScope.resourceKey = std::string(xKey);
                        scopeStack.push_back(std::move(itemScope));
                        ++nonVisualDepth;
                        continue;
                    }
                }
                if (!selfClosing) {
                    XmlScopeEntry childScope{};
                    childScope.kind = (shortTag == "ResourceDictionary")
                        ? XmlScopeKind::ResourceContainer
                        : XmlScopeKind::NonVisualSkip;
                    childScope.tagName = std::string(tagName);
                    scopeStack.push_back(std::move(childScope));
                    ++nonVisualDepth;
                }
                continue;
            }
            if (parentScope.kind == XmlScopeKind::KeyedResourceItem && !parentScope.brushAssigned &&
                shortTag == "GradientStop") {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        pkg.resources.emplace_back(
                            parentScope.resourceKey,
                            value::FromColor(ParseXamlColorLiteral(a.value)));
                        parentScope.brushAssigned = true;
                        break;
                    }
                }
            }
            if (shortTag == "VirtualizingStackPanel") {
                // If <VirtualizingStackPanel/> appears inside <ItemsControl.ItemsPanel> on a live visual element
                // (not inside a ResourceDictionary/Style/ControlTemplate), enable PropId_IsVirtualizing on that control!
                bool insideItemsPanel = false;
                bool blockedByTemplateOrRes = false;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) {
                        break;
                    }
                    if (it->kind == XmlScopeKind::ResourceContainer ||
                        it->kind == XmlScopeKind::KeyedResourceItem) {
                        blockedByTemplateOrRes = true;
                        break;
                    }
                    const std::string_view st = StripXmlPrefix(it->tagName);
                    if (st == "Style" || st == "ControlTemplate" || st == "DataTemplate") {
                        blockedByTemplateOrRes = true;
                        break;
                    }
                    if (EndsWith(st, ".ItemsPanel")) {
                        insideItemsPanel = true;
                    }
                }
                if (insideItemsPanel && !blockedByTemplateOrRes) {
                    AxbInstruction virtInst{};
                    virtInst.opcode = AxbOpcode::SetStaticProperty;
                    virtInst.propId = PropId_IsVirtualizing;
                    virtInst.literalValue = UnifiedValue{true};
                    pkg.instructions.push_back(std::move(virtInst));
                }
            }
            if (parentScope.kind == XmlScopeKind::BrushPropertyElement && !parentScope.brushAssigned &&
                (shortTag == "SolidColorBrush" || shortTag == "GradientStop")) {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        AxbInstruction setInst{};
                        setInst.opcode = AxbOpcode::SetStaticProperty;
                        setInst.propId = parentScope.targetPropId;
                        setInst.literalValue = value::FromColor(ParseXamlColorLiteral(a.value));
                        pkg.instructions.push_back(std::move(setInst));
                        parentScope.brushAssigned = true;
                        break;
                    }
                }
            }
            if (!selfClosing) {
                XmlScopeEntry skipScope{};
                skipScope.kind = (parentScope.kind == XmlScopeKind::BrushPropertyElement)
                    ? XmlScopeKind::BrushPropertyElement
                    : XmlScopeKind::NonVisualSkip;
                skipScope.tagName = std::string(tagName);
                skipScope.targetPropId = parentScope.targetPropId;
                skipScope.brushAssigned = parentScope.brushAssigned;
                scopeStack.push_back(std::move(skipScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Top-level ResourceDictionary root file
        if (!seenRoot && shortTag == "ResourceDictionary") {
            seenRoot = true;
            pkg.rootTypeName = "ResourceDictionary";
            AxbInstruction beginInst{};
            beginInst.opcode = AxbOpcode::BeginElement;
            beginInst.strIdx0 = pkg.InternString("ResourceDictionary");
            beginInst.strIdx1 = pkg.InternString("");
            pkg.instructions.push_back(std::move(beginInst));
            if (selfClosing) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
            } else {
                XmlScopeEntry resRoot{};
                resRoot.kind = XmlScopeKind::ResourceContainer;
                resRoot.tagName = std::string(tagName);
                scopeStack.push_back(std::move(resRoot));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <DataTemplate.Triggers> at top level of a DataTemplate sub-package!
        if (shortTag == "DataTemplate.Triggers" && scopeStack.empty()) {
            if (!selfClosing) {
                if (dtRootTriggersStyleKey.empty()) {
                    const std::string scopePrefix = !pkg.className.empty()
                        ? pkg.className
                        : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                    dtRootTriggersStyleKey = "__dt_root_triggers_" +
                        scopePrefix + "_" +
                        std::to_string(nextInlineVsmStyleId++);
                }
                const std::size_t depth = pushNonVisualScope();
                ActiveStyleScope dtTrigStyle{};
                dtTrigStyle.def.key = dtRootTriggersStyleKey;
                dtTrigStyle.def.targetTypeName = !pkg.rootTypeName.empty()
                    ? std::string(StripXmlPrefix(pkg.rootTypeName))
                    : std::string("UIElement");
                dtTrigStyle.controlTemplateTriggersDepth = depth;
                dtTrigStyle.scopeDepth = depth;
                styleStack.push_back(std::move(dtTrigStyle));
            }
            continue;
        }

        // Element-level <VisualStateManager.VisualStateGroups>, <Element.Style>, or <Element.Triggers>!
        if ((shortTag == "VisualStateManager.VisualStateGroups" ||
             EndsWith(shortTag, ".Style") ||
             EndsWith(shortTag, ".Triggers")) &&
            !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry& ownerElemScope = scopeStack.back();
                if (ownerElemScope.inlineStyleKey.empty()) {
                    const std::string scopePrefix = !pkg.className.empty()
                        ? pkg.className
                        : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                    ownerElemScope.inlineStyleKey = "__inline_style_" +
                        scopePrefix + "_" +
                        std::to_string(nextInlineVsmStyleId++);
                    AxbInstruction styleRefInst{};
                    styleRefInst.opcode = AxbOpcode::SetStaticProperty;
                    styleRefInst.propId = PropId_Style;
                    styleRefInst.literalValue = value::FromString(ownerElemScope.inlineStyleKey);
                    pkg.instructions.push_back(std::move(styleRefInst));
                }
                const std::string synthKey = ownerElemScope.inlineStyleKey;
                const std::string ownerType = std::string(StripXmlPrefix(ownerElemScope.tagName));
                const std::string ownerBasedOnKey = ownerElemScope.resourceKey;
                const std::size_t depth = pushNonVisualScope();
                ActiveStyleScope synthStyle{};
                synthStyle.def.key = synthKey;
                synthStyle.def.targetTypeName = ownerType;
                synthStyle.def.basedOnKey = ownerBasedOnKey;
                synthStyle.scopeDepth = depth;
                synthStyle.isInlinePropertyElement = EndsWith(shortTag, ".Style");
                styleStack.push_back(std::move(synthStyle));
            }
            continue;
        }

        // Inline <ItemsControl.ItemTemplate><DataTemplate>...</DataTemplate></ItemsControl.ItemTemplate>
        if (EndsWith(shortTag, ".ItemTemplate") && !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry itScope{};
                itScope.kind = XmlScopeKind::NonVisualSkip;
                itScope.tagName = std::string(tagName);
                itScope.isItemTemplateContainer = true;
                scopeStack.push_back(std::move(itScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Property-element tags (<Type.Property>)
        if (shortTag == "Grid.RowDefinitions" || shortTag == "Grid.ColumnDefinitions" ||
            EndsWith(shortTag, ".Header") ||
            EndsWith(shortTag, ".Content") ||
            EndsWith(shortTag, ".Data")) {
            if (!selfClosing) {
                XmlScopeEntry trans{};
                trans.kind = XmlScopeKind::TransparentContainer;
                trans.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Header")) {
                    trans.targetPropId = PropId_Title;
                } else if (EndsWith(shortTag, ".Content")) {
                    trans.targetPropId = PropId_Text;
                } else if (EndsWith(shortTag, ".Data")) {
                    trans.targetPropId = PropId_PathData;
                } else {
                    trans.targetPropId = 0;
                }
                scopeStack.push_back(std::move(trans));
            }
            continue;
        }

        if (shortTag == "RowDefinition" || shortTag == "ColumnDefinition") {
            std::string_view lenStr;
            for (const auto& a : attrs) {
                if ((shortTag == "RowDefinition" && a.name == "Height") ||
                    (shortTag == "ColumnDefinition" && a.name == "Width")) {
                    lenStr = a.value;
                    break;
                }
            }
            const auto [unit, val] = ParseGridLength(lenStr);
            AxbInstruction defInst{};
            defInst.opcode = (shortTag == "RowDefinition")
                ? AxbOpcode::AddRowDefinition
                : AxbOpcode::AddColumnDefinition;
            defInst.flags = static_cast<std::uint8_t>(unit);
            defInst.literalValue = MakeFloat(val);
            pkg.instructions.push_back(std::move(defInst));
            if (!selfClosing) {
                XmlScopeEntry trans{};
                trans.kind = XmlScopeKind::TransparentContainer;
                trans.tagName = std::string(tagName);
                trans.targetPropId = 0;
                scopeStack.push_back(std::move(trans));
            }
            continue;
        }

        if (shortTag == "Run") {
            std::string runText;
            for (const auto& a : attrs) {
                if (a.name == "Text") {
                    runText = DecodeXamlXmlEntities(a.value);
                    break;
                }
            }
            if (selfClosing) {
                if (!runText.empty()) {
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::VisualElement) {
                            if (!it->accumulatedText.empty()) it->accumulatedText.push_back(' ');
                            it->accumulatedText += runText;
                            break;
                        }
                    }
                }
            } else {
                XmlScopeEntry runScope{};
                runScope.kind = XmlScopeKind::RunInline;
                runScope.tagName = "Run";
                runScope.accumulatedText = std::move(runText);
                scopeStack.push_back(std::move(runScope));
            }
            continue;
        }

        if (shortTag == "ResourceDictionary" || EndsWith(shortTag, ".Resources")) {
            if (!selfClosing) {
                XmlScopeEntry resScope{};
                resScope.kind = XmlScopeKind::ResourceContainer;
                resScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(resScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".RenderTransform") || EndsWith(shortTag, ".LayoutTransform")) {
            if (!selfClosing) {
                XmlScopeEntry tfScope{};
                tfScope.kind = XmlScopeKind::TransformPropertyElement;
                tfScope.tagName = std::string(tagName);
                tfScope.targetPropId = EndsWith(shortTag, ".LayoutTransform")
                    ? PropId_LayoutTransform
                    : PropId_RenderTransform;
                tfScope.accumulatedTransform = Matrix2D::Identity();
                scopeStack.push_back(std::move(tfScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".Background") ||
            EndsWith(shortTag, ".Fill") ||
            EndsWith(shortTag, ".Foreground") ||
            EndsWith(shortTag, ".BorderBrush") ||
            EndsWith(shortTag, ".Stroke")) {
            if (!selfClosing) {
                XmlScopeEntry brushScope{};
                brushScope.kind = XmlScopeKind::BrushPropertyElement;
                brushScope.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Foreground")) {
                    brushScope.targetPropId = PropId_Foreground;
                } else if (EndsWith(shortTag, ".BorderBrush") ||
                           EndsWith(shortTag, ".Stroke")) {
                    brushScope.targetPropId = PropId_BorderBrush;
                } else {
                    brushScope.targetPropId = PropId_Background;
                }
                scopeStack.push_back(std::move(brushScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (shortTag.find('.') != std::string_view::npos || isNonVisualElementTag(shortTag)) {
            if (!selfClosing) {
                (void)pushNonVisualScope();
            }
            continue;
        }

        const bool isRootElement = !seenRoot;
        if (!seenRoot) {
            seenRoot = true;
            pkg.rootTypeName = std::string(tagName);
        }

        if (!xName.empty()) {
            AxbNamedControlSlot slot{};
            slot.name = std::string(xName);
            slot.typeName = std::string(tagName);
            slot.typeSymbolId = rs_sem::stableTypeId("Aero.UI." + std::string(shortTag));
            pkg.namedControls.push_back(std::move(slot));
        }

        AxbInstruction beginInst{};
        beginInst.opcode = AxbOpcode::BeginElement;
        beginInst.strIdx0 = pkg.InternString(tagName);
        beginInst.strIdx1 = pkg.InternString(xName);
        pkg.instructions.push_back(std::move(beginInst));

        bool hasExplicitWidth = false;
        bool hasExplicitHeight = false;
        std::string attachedStyleKey;
        for (const auto& a : attrs) {
            if (a.name == "Width") hasExplicitWidth = true;
            if (a.name == "Height") hasExplicitHeight = true;
        }

        for (const auto& attr : attrs) {
            if (attr.name == "d:DesignWidth" && (!isRootElement || hasExplicitWidth)) {
                continue;
            }
            if (attr.name == "d:DesignHeight" && (!isRootElement || hasExplicitHeight)) {
                continue;
            }
            if (attr.name == "Click" || attr.name == "OnClick") {
                const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                AxbEventBindingRecord ev{};
                ev.controlName = std::string(xName);
                ev.eventName = "Click";
                ev.handlerMethodName = std::string(Trim(attr.value));
                pkg.eventBindings.push_back(std::move(ev));

                AxbInstruction evInst{};
                evInst.opcode = AxbOpcode::BindEvent;
                evInst.strIdx0 = evIdx;
                pkg.instructions.push_back(std::move(evInst));
                continue;
            }

            const std::string_view trimmedVal = Trim(attr.value);

            if (attr.name == "Command" && trimmedVal.size() > 2 &&
                trimmedVal.front() == '{' && trimmedVal.back() == '}') {
                std::string_view cmdInner = Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
                if (cmdInner.rfind("Binding ", 0) == 0) cmdInner = Trim(cmdInner.substr(8));
                else if (cmdInner.rfind("Bind ", 0) == 0) cmdInner = Trim(cmdInner.substr(5));
                if (const std::size_t comma = cmdInner.find(','); comma != std::string_view::npos) {
                    cmdInner = Trim(cmdInner.substr(0, comma));
                }
                if (cmdInner.rfind("Path=", 0) == 0) cmdInner = Trim(cmdInner.substr(5));
                if (!cmdInner.empty()) {
                    const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                    AxbEventBindingRecord ev{};
                    ev.controlName = std::string(xName);
                    ev.eventName = "Command";
                    ev.handlerMethodName = std::string(cmdInner);
                    pkg.eventBindings.push_back(std::move(ev));

                    AxbInstruction evInst{};
                    evInst.opcode = AxbOpcode::BindEvent;
                    evInst.strIdx0 = evIdx;
                    pkg.instructions.push_back(std::move(evInst));
                }
                continue;
            }

            const std::uint16_t propId = ResolvePropertyId(attr.name);

            if (trimmedVal.size() > 2 && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
                std::string_view inner = Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
                if (inner.rfind("EcsBind ", 0) == 0) {
                    inner = Trim(inner.substr(8));
                    const std::size_t dot = inner.rfind('.');
                    const std::string_view compName =
                        (dot != std::string_view::npos) ? inner.substr(0, dot) : inner;
                    const std::string_view fieldName =
                        (dot != std::string_view::npos) ? inner.substr(dot + 1) : std::string_view{};

                    std::string canonicalComp(compName);
                    if (canonicalComp.find("::") == std::string::npos) {
                        if (const std::size_t lastDot = canonicalComp.rfind('.'); lastDot != std::string::npos) {
                            canonicalComp.replace(lastDot, 1, "::");
                        }
                    }
                    AxbInstruction ecsInst{};
                    ecsInst.opcode = AxbOpcode::BindEcsColumn;
                    ecsInst.propId = propId;
                    ecsInst.strIdx0 = pkg.InternString(compName);
                    ecsInst.strIdx1 = pkg.InternString(fieldName);
                    ecsInst.symbolId = rs_sem::stableTypeId(canonicalComp);
                    pkg.instructions.push_back(std::move(ecsInst));
                    continue;
                }

                // Handle {StaticResource ...}, {DynamicResource ...}, {noesis:Loc ...}
                if (inner.rfind("StaticResource ", 0) == 0 ||
                    inner.rfind("DynamicResource ", 0) == 0 ||
                    inner.rfind("noesis:Loc ", 0) == 0) {
                    std::string_view keyPart;
                    if (inner.rfind("StaticResource ", 0) == 0) keyPart = Trim(inner.substr(15));
                    else if (inner.rfind("DynamicResource ", 0) == 0) keyPart = Trim(inner.substr(16));
                    else keyPart = Trim(inner.substr(11));

                    if (keyPart.rfind("ResourceKey=", 0) == 0) {
                        keyPart = Trim(keyPart.substr(12));
                    }
                    if (!keyPart.empty()) {
                        if (propId == PropId_Style || propId == PropId_ItemTemplate) {
                            if (propId == PropId_Style) {
                                attachedStyleKey = std::string(keyPart);
                            }
                            AxbInstruction keyInst{};
                            keyInst.opcode = AxbOpcode::SetStaticProperty;
                            keyInst.propId = propId;
                            keyInst.literalValue = value::FromString(std::string(keyPart));
                            pkg.instructions.push_back(std::move(keyInst));
                            continue;
                        }

                        AxbInstruction resInst{};
                        resInst.opcode = AxbOpcode::SetResourceProperty;
                        resInst.propId = propId;
                        resInst.strIdx0 = pkg.InternString(keyPart);
                        if (const UnifiedValue* resVal = findLocalOrGlobalResource(keyPart)) {
                            resInst.literalValue = *resVal;
                            if (propId == PropId_PathData) {
                                if (const auto* sv = std::get_if<StringValue>(resVal)) {
                                    ensurePathBakedIntoPackage(sv->view());
                                }
                            }
                        } else if (inner.rfind("noesis:Loc ", 0) == 0 &&
                                   (propId == PropId_Text || propId == PropId_Title ||
                                    propId == PropId_Content || propId == PropId_Placeholder)) {
                            resInst.literalValue = value::FromString(std::string(keyPart));
                        }
                        pkg.instructions.push_back(std::move(resInst));
                    }
                    continue;
                }

                if (inner.rfind("TemplateBinding", 0) == 0 ||
                    inner.rfind("x:Type", 0) == 0 ||
                    inner.rfind("x:Static", 0) == 0 ||
                    inner.rfind("x:Null", 0) == 0 ||
                    inner.rfind("RelativeSource", 0) == 0) {
                    continue;
                }

                const ParsedBindingClause parsedBind = ParseBindingMarkupExpression(trimmedVal);
                if (!parsedBind.isValidBinding) {
                    continue;
                }

                const BindingMode mode = parsedBind.mode;
                const std::string_view pathPart = parsedBind.pathPart;
                const std::string_view fallbackValue = parsedBind.fallbackValue;

                UnifiedValue parsedFallback{};
                if (!fallbackValue.empty()) {
                    parsedFallback = ParseLiteralPropertyValue(propId, fallbackValue);
                    AxbInstruction fbInst{};
                    fbInst.opcode = AxbOpcode::SetStaticProperty;
                    fbInst.propId = propId;
                    fbInst.literalValue = parsedFallback;
                    pkg.instructions.push_back(std::move(fbInst));
                }

                if (pathPart.empty() || pathPart == ".") {
                    continue;
                }

                AxbCompiledBindingChain chain{};
                chain.rawPath = std::string(pathPart);
                chain.targetPropertyId = propId;
                chain.mode = mode;
                chain.fallbackValue = std::move(parsedFallback);
                chain.elementName = std::string(parsedBind.elementName);
                chain.relativeAncestorType = parsedBind.relativeAncestorType;
                chain.relativeSelf = parsedBind.relativeSelf;
                chain.converter = parsedBind.converter;

                std::size_t segPos = 0;
                while (segPos < pathPart.size()) {
                    const std::size_t dot = pathPart.find('.', segPos);
                    const std::string_view seg = (dot == std::string_view::npos)
                        ? pathPart.substr(segPos)
                        : pathPart.substr(segPos, dot - segPos);
                    if (!seg.empty()) {
                        AxbCompiledHop hop{};
                        hop.memberName = std::string(seg);
                        chain.hops.push_back(std::move(hop));
                    }
                    if (dot == std::string_view::npos) break;
                    segPos = dot + 1;
                }

                const auto chainIdx = static_cast<std::uint32_t>(pkg.bindingChains.size());
                pkg.bindingChains.push_back(std::move(chain));

                AxbInstruction bindInst{};
                bindInst.opcode = AxbOpcode::BindProperty;
                bindInst.propId = propId;
                bindInst.flags = static_cast<std::uint8_t>(mode);
                bindInst.strIdx0 = chainIdx;
                pkg.instructions.push_back(std::move(bindInst));
                continue;
            }

            // Inline 6-float XAML Matrix syntax: RenderTransform="m11, m12, m21, m22, offsetX, offsetY"
            if (propId == PropId_RenderTransform) {
                emitStaticRenderTransform(ParseXamlMatrix2DLiteral(trimmedVal));
                continue;
            }
            if (propId == PropId_LayoutTransform) {
                emitStaticLayoutTransform(ParseXamlMatrix2DLiteral(trimmedVal));
                continue;
            }

            const UnifiedValue litVal = ParseLiteralPropertyValue(propId, trimmedVal);
            if (propId == PropId_PathData) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    ensurePathBakedIntoPackage(sv->view());
                }
            }
            if (propId == PropId_Style) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    attachedStyleKey = std::string(sv->view());
                }
            }

            AxbInstruction setInst{};
            setInst.opcode = AxbOpcode::SetStaticProperty;
            setInst.propId = propId;
            setInst.literalValue = litVal;
            pkg.instructions.push_back(std::move(setInst));
        }

        if (selfClosing) {
            AxbInstruction endInst{};
            endInst.opcode = AxbOpcode::EndElement;
            pkg.instructions.push_back(std::move(endInst));
        } else {
            XmlScopeEntry visScope{};
            visScope.kind = XmlScopeKind::VisualElement;
            visScope.tagName = std::string(tagName);
            visScope.resourceKey = std::move(attachedStyleKey);
            scopeStack.push_back(std::move(visScope));
        }
    }

    // If <DataTemplate.Triggers> was parsed at top level of a DataTemplate, attach it to the root element
    // and chain BasedOn to any existing root Style!
    if (!dtRootTriggersStyleKey.empty() && !pkg.instructions.empty() &&
        pkg.instructions[0].opcode == AxbOpcode::BeginElement) {
        for (auto& st : pkg.styles) {
            if (st.key == dtRootTriggersStyleKey) {
                if ((st.targetTypeName.empty() || st.targetTypeName == "UIElement") &&
                    !pkg.rootTypeName.empty()) {
                    st.targetTypeName = std::string(StripXmlPrefix(pkg.rootTypeName));
                }
                bool updatedExistingStyleInst = false;
                for (std::size_t idx = 1u; idx < pkg.instructions.size(); ++idx) {
                    auto& inst = pkg.instructions[idx];
                    if (inst.opcode == AxbOpcode::BeginElement ||
                        inst.opcode == AxbOpcode::EndElement) {
                        break;
                    }
                    if (inst.opcode == AxbOpcode::SetStaticProperty &&
                        inst.propId == PropId_Style) {
                        if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                            const std::string existingStyleKey(sv->view());
                            if (!existingStyleKey.empty() && existingStyleKey != dtRootTriggersStyleKey) {
                                if (st.basedOnKey.empty()) {
                                    st.basedOnKey = existingStyleKey;
                                }
                            }
                        }
                        inst.literalValue = value::FromString(dtRootTriggersStyleKey);
                        updatedExistingStyleInst = true;
                    }
                }
                if (!updatedExistingStyleInst) {
                    AxbInstruction dtStyleInst{};
                    dtStyleInst.opcode = AxbOpcode::SetStaticProperty;
                    dtStyleInst.propId = PropId_Style;
                    dtStyleInst.literalValue = value::FromString(dtRootTriggersStyleKey);
                    pkg.instructions.insert(pkg.instructions.begin() + 1, std::move(dtStyleInst));
                }
                break;
            }
        }
    }

    if (!pkg.mtsdfPaths.empty() && !isSvgInput && !isLottieInput && !isRiveInput) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    if (!isSvgInput && !isLottieInput && !isRiveInput) {
        PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::Xaml, input.xamlPath, result.sceneIr);
    }

    // Generate *.g.rs partial class stub when x:Class is specified
    std::vector<SourceFile> allSources = input.scriptSources;
    if (!pkg.className.empty()) {
        std::string moduleName = "Aero.UI";
        std::string shortClassName = pkg.className;
        if (const auto sep = pkg.className.find("::"); sep != std::string::npos) {
            moduleName = pkg.className.substr(0, sep);
            shortClassName = pkg.className.substr(sep + 2);
        } else if (const auto dot = pkg.className.rfind('.'); dot != std::string::npos) {
            moduleName = pkg.className.substr(0, dot);
            shortClassName = pkg.className.substr(dot + 1);
        }

        result.generatedPartialRsPath = shortClassName + ".g.rs";
        std::string stub = "// Auto-generated by aero-compiler from " + input.xamlPath + "\n";
        stub += "module " + moduleName + ";\n\n";
        stub += "public partial class " + shortClassName + "\n{\n";
        stub += "    public handle __nativeRootHandle;\n";
        for (const auto& nc : pkg.namedControls) {
            stub += "    public handle " + nc.name + ";\n";
        }
        stub += "}\n";
        result.generatedPartialRsCode = stub;

        if (!input.scriptSources.empty()) {
            bool alreadyHasPartialStub = false;
            for (const auto& sf : allSources) {
                if (sf.path == result.generatedPartialRsPath ||
                    (sf.path.size() >= result.generatedPartialRsPath.size() &&
                     sf.path.compare(
                         sf.path.size() - result.generatedPartialRsPath.size(),
                         result.generatedPartialRsPath.size(),
                         result.generatedPartialRsPath) == 0)) {
                    alreadyHasPartialStub = true;
                    break;
                }
            }
            if (!alreadyHasPartialStub) {
                allSources.push_back(SourceFile{result.generatedPartialRsPath, result.generatedPartialRsCode});
            }
        }
    }

    // If RealScript sources were supplied, run single-pass Compilation + Bytecode Lowering + Link
    if (!allSources.empty()) {
        rs_comp::Compilation compilation(std::move(allSources));
        const auto build = compilation.build();
        for (const auto& d : build.diagnostics.items()) {
            result.diagnostics.push_back(d.code + ": " + d.message);
        }
        if (build.diagnostics.hasErrors()) {
            result.succeeded = false;
            return result;
        }

        rs_bc::Lowerer lowerer;
        for (const auto& mir : build.modules) {
            auto bc = lowerer.lower(mir);
            rs_diag::DiagnosticBag verifyDiag;
            if (!rs_bc::verifyModule(bc, verifyDiag)) {
                for (const auto& d : verifyDiag.items()) {
                    result.diagnostics.push_back(d.code + ": " + d.message);
                }
                result.succeeded = false;
                return result;
            }
            result.bytecodeModules.push_back(bc);
        }

        RuntimeError linkErr{};
        auto linked = ProgramImage::link(result.bytecodeModules, linkErr);
        if (!linked) {
            result.diagnostics.push_back("Link error: " + linkErr.message);
            result.succeeded = false;
            return result;
        }
        result.programImage = std::make_shared<const ProgramImage>(std::move(*linked));

        BindPackageAgainstProgram(pkg, *result.programImage, rootDataType);

        if (input.emitAotCpp) {
            rs_aot::CppGenerator aotGen;
            rs_diag::DiagnosticBag aotDiag;
            rs_aot::GenerationOptions genOpts{};
            genOpts.programName = !pkg.className.empty()
                ? rs_aot::sanitizeCppIdentifier(pkg.className)
                : "AeroScriptProgram";
            genOpts.cppNamespace = !input.aotCppNamespace.empty()
                ? input.aotCppNamespace
                : "aero::aot_generated";
            genOpts.emitLineDirectives = false;
            const auto generatedScriptAot = aotGen.generate(build.modules, aotDiag, genOpts);
            for (const auto& d : aotDiag.items()) {
                result.diagnostics.push_back(d.code + ": " + d.message);
            }
            if (aotDiag.hasErrors()) {
                result.succeeded = false;
                return result;
            }
            result.aotCpp = GenerateAotCppFromPackage(
                pkg,
                input.aotCppNamespace,
                input.aotViewBuilderName);
            result.aotCpp.scriptAotHeader = generatedScriptAot.header;
            std::string inlineHeader = generatedScriptAot.header;
            if (const std::size_t pragmaPos = inlineHeader.find("#pragma once"); pragmaPos != std::string::npos) {
                inlineHeader.erase(pragmaPos, 12);
            }
            std::string selfContainedSource = generatedScriptAot.source;
            const std::string includeDirective = "#include \"realscript_aot_generated.h\"";
            if (const std::size_t incPos = selfContainedSource.find(includeDirective); incPos != std::string::npos) {
                selfContainedSource.replace(incPos, includeDirective.size(), inlineHeader);
            }
            result.aotCpp.scriptAotSource = std::move(selfContainedSource);
            result.aotCpp.scriptAotContentHash = generatedScriptAot.contentHash;
        }
    }

    if (input.emitAotCpp && result.aotCpp.source.empty()) {
        result.aotCpp = GenerateAotCppFromPackage(
            pkg,
            input.aotCppNamespace,
            input.aotViewBuilderName);
    }

    result.axbBytes = pkg.Serialize();
    if (!pkg.className.empty()) {
        AxbTemplateRegistry::Instance().Register(pkg.className, pkg);
    } else if (!input.xamlPath.empty() || pkg.rootTypeName == "ResourceDictionary") {
        AxbTemplateRegistry::Instance().RegisterResources(pkg);
    }
    result.succeeded = true;
    return result;
}

namespace {

std::string FormatFloatLiteral(float v) {
    if (std::isinf(v)) {
        return (v < 0.0f) ? "-std::numeric_limits<float>::infinity()" : "std::numeric_limits<float>::infinity()";
    }
    if (std::isnan(v)) {
        return "-1.0f";
    }
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6ff", v);
    return std::string(buf);
}

std::string EscapeAotCppString(std::string_view sv) {
    std::string out;
    out.reserve(sv.size() + 8u);
    for (std::size_t i = 0u; i < sv.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(sv[i]);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '?':
                if (i + 1u < sv.size() && sv[i + 1u] == '?') {
                    out += "\\?";
                } else {
                    out.push_back('?');
                }
                break;
            default:
                if (c < 0x20u || c >= 0x7Fu) {
                    // Use fixed 3-digit octal escape so following [0-9a-fA-F] chars are never eaten as hex!
                    char oct[5];
                    oct[0] = '\\';
                    oct[1] = static_cast<char>('0' + ((c >> 6u) & 0x07u));
                    oct[2] = static_cast<char>('0' + ((c >> 3u) & 0x07u));
                    oct[3] = static_cast<char>('0' + (c & 0x07u));
                    oct[4] = '\0';
                    out.append(oct, 4u);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string EmitUnifiedValueLiteralCpp(const UnifiedValue& val) {
    if (const auto* b = std::get_if<bool>(&val)) {
        return *b ? "aero::UnifiedValue{true}" : "aero::UnifiedValue{false}";
    }
    if (const auto* i = std::get_if<std::int64_t>(&val)) {
        return "aero::value::FromInt(" + std::to_string(*i) + "LL)";
    }
    if (const auto* f = std::get_if<FloatValue>(&val)) {
        return "aero::MakeFloat(" + FormatFloatLiteral(f->value) + ")";
    }
    Float4Value f4{};
    if (val.tryGetFloat4(f4)) {
        return "aero::MakeFloat4InlineFast(" +
               FormatFloatLiteral(f4.x) + ", " +
               FormatFloatLiteral(f4.y) + ", " +
               FormatFloatLiteral(f4.z) + ", " +
               FormatFloatLiteral(f4.w) + ")";
    }
    if (const auto* s = std::get_if<StringValue>(&val)) {
        return "aero::value::FromString(\"" + EscapeAotCppString(s->str()) + "\")";
    }
    return "aero::UnifiedValue{}";
}

void AppendStoryboardPopulateCpp(
    std::string& src,
    const AxbStoryboard& sb,
    const std::string& sbVar,
    const std::string& indent) {
    if (!sb.key.empty()) {
        src += indent + sbVar + ".key = \"" + EscapeAotCppString(sb.key) + "\";\n";
    }
    for (const auto& tl : sb.timelines) {
        src += indent + "{\n";
        src += indent + "    aero::AxbAnimationTimeline tl{};\n";
        if (!tl.targetName.empty()) {
            src += indent + "    tl.targetName = \"" + EscapeAotCppString(tl.targetName) + "\";\n";
        }
        src += indent + "    tl.targetPropertyId = " + std::to_string(tl.targetPropertyId) + "u;\n";
        src += indent + "    tl.beginTimeSeconds = " + FormatFloatLiteral(tl.beginTimeSeconds) + ";\n";
        src += indent + "    tl.durationSeconds = " + FormatFloatLiteral(tl.durationSeconds) + ";\n";
        src += indent + "    tl.hasFrom = " + std::string(tl.hasFrom ? "true" : "false") + ";\n";
        if (tl.hasFrom) {
            src += indent + "    tl.fromValue = " + EmitUnifiedValueLiteralCpp(tl.fromValue) + ";\n";
        }
        src += indent + "    tl.hasTo = " + std::string(tl.hasTo ? "true" : "false") + ";\n";
        src += indent + "    tl.isBy = " + std::string(tl.isBy ? "true" : "false") + ";\n";
        if (tl.hasTo) {
            src += indent + "    tl.toValue = " + EmitUnifiedValueLiteralCpp(tl.toValue) + ";\n";
        }
        src += indent + "    tl.easingKind = static_cast<aero::EasingFunctionKind>(" +
               std::to_string(static_cast<unsigned>(tl.easingKind)) + "u);\n";
        src += indent + "    tl.easingMode = static_cast<aero::EasingMode>(" +
               std::to_string(static_cast<unsigned>(tl.easingMode)) + "u);\n";
        src += indent + "    tl.fillBehavior = static_cast<aero::FillBehavior>(" +
               std::to_string(static_cast<unsigned>(tl.fillBehavior)) + "u);\n";
        src += indent + "    tl.autoReverse = " + std::string(tl.autoReverse ? "true" : "false") + ";\n";
        src += indent + "    tl.repeatCount = " + std::to_string(tl.repeatCount) + ";\n";
        for (const auto& kf : tl.keyFrames) {
            src += indent + "    {\n";
            src += indent + "        aero::AxbKeyFrame kf{};\n";
            src += indent + "        kf.timeSeconds = " + FormatFloatLiteral(kf.timeSeconds) + ";\n";
            src += indent + "        kf.value = " + EmitUnifiedValueLiteralCpp(kf.value) + ";\n";
            src += indent + "        kf.easingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(kf.easingKind)) + "u);\n";
            src += indent + "        kf.easingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(kf.easingMode)) + "u);\n";
            src += indent + "        kf.isDiscrete = " + std::string(kf.isDiscrete ? "true" : "false") + ";\n";
            src += indent + "        tl.keyFrames.push_back(std::move(kf));\n";
            src += indent + "    }\n";
        }
        src += indent + "    " + sbVar + ".timelines.push_back(std::move(tl));\n";
        src += indent + "}\n";
    }
}

void AppendSetterListPopulateCpp(
    std::string& src,
    const std::vector<AxbPropertySetter>& setters,
    const std::string& targetVecExpr,
    const std::string& indent) {
    for (const auto& s : setters) {
        src += indent + "{\n";
        src += indent + "    aero::AxbPropertySetter ps{};\n";
        if (!s.targetName.empty()) {
            src += indent + "    ps.targetName = \"" + EscapeAotCppString(s.targetName) + "\";\n";
        }
        src += indent + "    ps.propId = " + std::to_string(s.propId) + "u;\n";
        src += indent + "    ps.value = " + EmitUnifiedValueLiteralCpp(s.value) + ";\n";
        src += indent + "    " + targetVecExpr + ".push_back(std::move(ps));\n";
        src += indent + "}\n";
    }
}

} // namespace

AeroViewAotCppOutput AeroCompiler::GenerateAotCppFromPackage(
    const AxbPackage& package,
    std::string_view cppNamespace,
    std::string_view builderFunctionName) {
    AeroViewAotCppOutput out{};
    out.cppNamespace = cppNamespace.empty()
        ? "aero::aot_generated"
        : std::string(cppNamespace);

    if (!builderFunctionName.empty()) {
        out.builderFunctionName = std::string(builderFunctionName);
    } else if (!package.className.empty()) {
        out.builderFunctionName = "BuildView_" + rs_aot::sanitizeCppIdentifier(package.className);
    } else {
        out.builderFunctionName = "BuildAeroView";
    }

    // 1. Emit C++17 Header (.hpp)
    std::string hdr;
    hdr += "#pragma once\n\n";
    hdr += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    hdr += "#include <cstdint>\n";
    hdr += "#include <limits>\n";
    hdr += "#include \"aero/controls/Controls.hpp\"\n";
    hdr += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n\n";
    hdr += "namespace " + out.cppNamespace + " {\n\n";
    hdr += "bool " + out.builderFunctionName + "(\n";
    hdr += "    aero::UIElement* rootControl,\n";
    hdr += "    aero::ExecutionDomainContext* domains = nullptr,\n";
    hdr += "    aero::DualTrackBindingEngine* bindingEngine = nullptr,\n";
    hdr += "    aero::ObjectRef dataContext = {},\n";
    hdr += "    const aero::Interpreter* eventInterpreter = nullptr,\n";
    hdr += "    aero::rs_aot::ExecutionContext* eventAotContext = nullptr);\n\n";
    hdr += "} // namespace " + out.cppNamespace + "\n";
    out.header = std::move(hdr);

    // 2. Emit C++17 Source (.cpp) - unrolled batch creation, static properties, MTSDF, Styles/Storyboards & bindings
    std::string src;
    src += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    src += "#include <limits>\n";
    src += "#include \"aero/controls/Controls.hpp\"\n";
    src += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n\n";
    src += "namespace " + out.cppNamespace + " {\n\n";
    src += "bool " + out.builderFunctionName + "(\n";
    src += "    aero::UIElement* rootControl,\n";
    src += "    aero::ExecutionDomainContext* domains,\n";
    src += "    aero::DualTrackBindingEngine* bindingEngine,\n";
    src += "    aero::ObjectRef dataContext,\n";
    src += "    const aero::Interpreter* eventInterpreter,\n";
    src += "    aero::rs_aot::ExecutionContext* eventAotContext) {\n";
    src += "    if (!rootControl) {\n";
    src += "        return false;\n";
    src += "    }\n";
    src += "    aero::EnsureControlTypesRegistered();\n";

    // Emit pre-baked MTSDF vector path entries so pure C++17 AOT views have 0 runtime SVG parsing
    for (std::size_t mpIdx = 0; mpIdx < package.mtsdfPaths.size(); ++mpIdx) {
        const auto& mp = package.mtsdfPaths[mpIdx];
        if (mp.pathHash == 0 || mp.mtsdfRgbaPixels.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindMtsdfPath(" +
               std::to_string(mp.pathHash) + "ULL)) {\n";
        const std::string arrName = "kMtsdfPixels_" + std::to_string(mpIdx);
        src += "        static constexpr std::uint8_t " + arrName + "[] = {";
        for (std::size_t b = 0; b < mp.mtsdfRgbaPixels.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(mp.mtsdfRgbaPixels[b])) + "u";
        }
        src += "};\n";
        src += "        aero::AxbMtsdfPathEntry entry{};\n";
        src += "        entry.pathHash = " + std::to_string(mp.pathHash) + "ULL;\n";
        src += "        entry.boundsX = " + FormatFloatLiteral(mp.boundsX) + ";\n";
        src += "        entry.boundsY = " + FormatFloatLiteral(mp.boundsY) + ";\n";
        src += "        entry.boundsW = " + FormatFloatLiteral(mp.boundsW) + ";\n";
        src += "        entry.boundsH = " + FormatFloatLiteral(mp.boundsH) + ";\n";
        src += "        entry.u0 = " + FormatFloatLiteral(mp.u0) + ";\n";
        src += "        entry.v0 = " + FormatFloatLiteral(mp.v0) + ";\n";
        src += "        entry.u1 = " + FormatFloatLiteral(mp.u1) + ";\n";
        src += "        entry.v1 = " + FormatFloatLiteral(mp.v1) + ";\n";
        src += "        entry.atlasWidth = " + std::to_string(mp.atlasWidth) + "u;\n";
        src += "        entry.atlasHeight = " + std::to_string(mp.atlasHeight) + "u;\n";
        src += "        entry.cellWidth = " + std::to_string(mp.cellWidth) + "u;\n";
        src += "        entry.cellHeight = " + std::to_string(mp.cellHeight) + "u;\n";
        src += "        entry.pxRange = " + FormatFloatLiteral(mp.pxRange) + ";\n";
        src += "        entry.mtsdfRgbaPixels.assign(" + arrName + ", " + arrName + " + sizeof(" + arrName + "));\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterMtsdfPath(std::move(entry));\n";
        src += "    }\n";
    }

    // Emit keyed scalar/color/string resources into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& resPair : package.resources) {
        if (resPair.first.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindResource(\"" +
               EscapeAotCppString(resPair.first) + "\")) {\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterResource(\"" +
               EscapeAotCppString(resPair.first) + "\", " +
               EmitUnifiedValueLiteralCpp(resPair.second) + ");\n";
        src += "    }\n";
    }

    // Emit pre-compiled Storyboards & Styles into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& sb : package.storyboards) {
        if (sb.key.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindStoryboard(\"" +
               EscapeAotCppString(sb.key) + "\")) {\n";
        src += "        aero::AxbStoryboard sbDef{};\n";
        AppendStoryboardPopulateCpp(src, sb, "sbDef", "        ");
        src += "        aero::AxbTemplateRegistry::Instance().RegisterStoryboard(std::move(sbDef));\n";
        src += "    }\n";
    }

    for (const auto& st : package.styles) {
        if (!st.key.empty()) {
            src += "    if (!aero::AxbTemplateRegistry::Instance().FindStyle(\"" +
                   EscapeAotCppString(st.key) + "\")) {\n";
        } else {
            src += "    {\n";
        }
        src += "        aero::AxbStyleDefinition stDef{};\n";
        if (!st.key.empty()) {
            src += "        stDef.key = \"" + EscapeAotCppString(st.key) + "\";\n";
        }
        if (!st.targetTypeName.empty()) {
            src += "        stDef.targetTypeName = \"" + EscapeAotCppString(st.targetTypeName) + "\";\n";
        }
        if (!st.basedOnKey.empty()) {
            src += "        stDef.basedOnKey = \"" + EscapeAotCppString(st.basedOnKey) + "\";\n";
        }
        AppendSetterListPopulateCpp(src, st.setters, "stDef.setters", "        ");
        for (const auto& trig : st.triggers) {
            src += "        {\n";
            src += "            aero::AxbStyleTrigger trigDef{};\n";
            src += "            trigDef.overrideLocalValues = " +
                   std::string(trig.overrideLocalValues ? "true" : "false") + ";\n";
            for (const auto& cond : trig.conditions) {
                src += "            {\n";
                src += "                aero::AxbTriggerCondition tc{};\n";
                src += "                tc.propId = " + std::to_string(cond.propId) + "u;\n";
                src += "                tc.expectedValue = " + EmitUnifiedValueLiteralCpp(cond.expectedValue) + ";\n";
                src += "                tc.sourceKind = static_cast<aero::TriggerConditionSource>(" +
                       std::to_string(static_cast<unsigned>(cond.sourceKind)) + "u);\n";
                src += "                tc.converter = static_cast<aero::ValueConverterKind>(" +
                       std::to_string(static_cast<unsigned>(cond.converter)) + "u);\n";
                src += "                tc.expectNull = " + std::string(cond.expectNull ? "true" : "false") + ";\n";
                if (!cond.sourceName.empty()) {
                    src += "                tc.sourceName = \"" + EscapeAotCppString(cond.sourceName) + "\";\n";
                }
                if (!cond.bindingPath.empty()) {
                    src += "                tc.bindingPath = \"" + EscapeAotCppString(cond.bindingPath) + "\";\n";
                }
                for (const auto& chop : cond.compiledHops) {
                    src += "                {\n";
                    src += "                    aero::AxbCompiledHop ch{};\n";
                    src += "                    ch.memberName = \"" + EscapeAotCppString(chop.memberName) + "\";\n";
                    src += "                    ch.declaringTypeId = " + std::to_string(chop.declaringTypeId) + "ULL;\n";
                    src += "                    ch.fieldIndex = " + std::to_string(chop.fieldIndex) + "u;\n";
                    src += "                    ch.observableBitIndex = " + std::to_string(static_cast<unsigned>(chop.observableBitIndex)) + "u;\n";
                    src += "                    ch.getterSymbolId = " + std::to_string(chop.getterSymbolId) + "ULL;\n";
                    src += "                    ch.setterSymbolId = " + std::to_string(chop.setterSymbolId) + "ULL;\n";
                    src += "                    ch.isAutoProperty = " + std::string(chop.isAutoProperty ? "true" : "false") + ";\n";
                    src += "                    ch.isObservable = " + std::string(chop.isObservable ? "true" : "false") + ";\n";
                    src += "                    ch.isReadOnly = " + std::string(chop.isReadOnly ? "true" : "false") + ";\n";
                    src += "                    ch.isSimState = " + std::string(chop.isSimState ? "true" : "false") + ";\n";
                    src += "                    ch.isSimCommand = " + std::string(chop.isSimCommand ? "true" : "false") + ";\n";
                    src += "                    tc.compiledHops.push_back(std::move(ch));\n";
                    src += "                }\n";
                }
                src += "                trigDef.conditions.push_back(std::move(tc));\n";
                src += "            }\n";
            }
            AppendSetterListPopulateCpp(src, trig.setters, "trigDef.setters", "            ");
            for (const auto& esb : trig.enterStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard esbDef{};\n";
                AppendStoryboardPopulateCpp(src, esb, "esbDef", "                ");
                src += "                trigDef.enterStoryboards.push_back(std::move(esbDef));\n";
                src += "            }\n";
            }
            for (const auto& xsb : trig.exitStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard xsbDef{};\n";
                AppendStoryboardPopulateCpp(src, xsb, "xsbDef", "                ");
                src += "                trigDef.exitStoryboards.push_back(std::move(xsbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.triggers.push_back(std::move(trigDef));\n";
            src += "        }\n";
        }
        for (const auto& vsg : st.visualStateGroups) {
            src += "        {\n";
            src += "            aero::AxbVisualStateGroup vsgDef{};\n";
            src += "            vsgDef.name = \"" + EscapeAotCppString(vsg.name) + "\";\n";
            src += "            vsgDef.defaultTransitionDuration = " +
                   FormatFloatLiteral(vsg.defaultTransitionDuration) + ";\n";
            src += "            vsgDef.defaultEasingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingKind)) + "u);\n";
            src += "            vsgDef.defaultEasingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingMode)) + "u);\n";
            for (const auto& vs : vsg.states) {
                src += "            {\n";
                src += "                aero::AxbVisualState vsDef{};\n";
                src += "                vsDef.name = \"" + EscapeAotCppString(vs.name) + "\";\n";
                AppendSetterListPopulateCpp(src, vs.setters, "vsDef.setters", "                ");
                AppendStoryboardPopulateCpp(src, vs.storyboard, "vsDef.storyboard", "                ");
                src += "                vsgDef.states.push_back(std::move(vsDef));\n";
                src += "            }\n";
            }
            src += "            stDef.visualStateGroups.push_back(std::move(vsgDef));\n";
            src += "        }\n";
        }
        for (const auto& et : st.eventTriggers) {
            src += "        {\n";
            src += "            aero::AxbEventTrigger etDef{};\n";
            src += "            etDef.eventKind = static_cast<aero::RoutedTriggerEvent>(" +
                   std::to_string(static_cast<unsigned>(et.eventKind)) + "u);\n";
            if (!et.sourceName.empty()) {
                src += "            etDef.sourceName = \"" + EscapeAotCppString(et.sourceName) + "\";\n";
            }
            for (const auto& etSb : et.storyboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard etSbDef{};\n";
                AppendStoryboardPopulateCpp(src, etSb, "etSbDef", "                ");
                src += "                etDef.storyboards.push_back(std::move(etSbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.eventTriggers.push_back(std::move(etDef));\n";
            src += "        }\n";
        }
        src += "        aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
        src += "    }\n";
    }

    for (std::size_t dtIdx = 0; dtIdx < package.dataTemplates.size(); ++dtIdx) {
        const auto& dt = package.dataTemplates[dtIdx];
        if (!dt.templatePackage) {
            continue;
        }
        const std::vector<std::uint8_t> dtBytes = dt.templatePackage->Serialize();
        if (dtBytes.empty()) {
            continue;
        }
        if (!dt.key.empty()) {
            src += "    if (!aero::AxbTemplateRegistry::Instance().FindDataTemplate(\"" +
                   EscapeAotCppString(dt.key) + "\")) {\n";
        } else {
            src += "    {\n";
        }
        const std::string dtArr = "kDataTplBytes_" + std::to_string(dtIdx);
        src += "        static constexpr std::uint8_t " + dtArr + "[] = {";
        for (std::size_t b = 0; b < dtBytes.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(dtBytes[b])) + "u";
        }
        src += "};\n";
        src += "        auto subPkg = std::make_shared<aero::AxbPackage>();\n";
        src += "        if (aero::AxbPackage::Deserialize(" + dtArr + ", sizeof(" + dtArr + "), *subPkg)) {\n";
        src += "            aero::AxbDataTemplate dtDef{};\n";
        if (!dt.key.empty()) {
            src += "            dtDef.key = \"" + EscapeAotCppString(dt.key) + "\";\n";
        }
        if (!dt.dataTypeName.empty()) {
            src += "            dtDef.dataTypeName = \"" + EscapeAotCppString(dt.dataTypeName) + "\";\n";
        }
        src += "            dtDef.templatePackage = std::move(subPkg);\n";
        src += "            aero::AxbTemplateRegistry::Instance().RegisterDataTemplate(std::move(dtDef));\n";
        src += "        }\n";
        src += "    }\n";
    }

    src += "    aero::NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;\n";
    src += "    aero::ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;\n";
    const std::string_view unusedNote = ""; (void)unusedNote;
    src += "    const aero::ObjectRef effectiveContext = dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();\n";
    src += "    const aero::ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid() ? rootControl->BoundScriptWrapper() : effectiveContext;\n\n";
    src += "    (void)registry; (void)uiHeap; (void)effectiveContext; (void)wrapperObj;\n";
    src += "    (void)bindingEngine; (void)eventInterpreter; (void)eventAotContext;\n\n";

    std::vector<std::string> elemVarStack;
    std::size_t nextElemId = 0;
    bool rootVisited = false;
    src += "    std::vector<aero::UIElement*> localElements;\n";
    src += "    localElements.reserve(" + std::to_string(package.instructions.size() / 2u + 1u) + "u);\n";

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                const std::string varName = "elem_" + std::to_string(nextElemId++);

                if (!rootVisited) {
                    rootVisited = true;
                    src += "    aero::UIElement* " + varName + " = rootControl; (void)" + varName + ";\n";
                    if (package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        src += "    if (domains && registry && wrapperObj.valid()) {\n";
                        src += "        const auto rootH = rootControl->ToNativeHandle(*registry);\n";
                        src += "        (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                               std::to_string(package.rootHandleFieldIndex) + "u, rootH, nullptr);\n";
                        src += "    }\n";
                    }
                } else {
                    const std::string parentVar = elemVarStack.empty() ? "rootControl" : elemVarStack.back();
                    const std::string escapedType = EscapeAotCppString(std::string(typeName));
                    const std::string_view shortChild = StripXmlPrefix(typeName);
                    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr &&
                        shortChild != "UserControl" && shortChild != "Page") {
                        src += "    aero::IntrusivePtr<aero::UIElement> " + varName +
                               "_ptr = aero::MakeIntrusive<aero::UserControl>(\"" + escapedType +
                               "\", domains, bindingEngine, effectiveContext, eventInterpreter, eventAotContext);\n";
                    } else {
                        src += "    auto " + varName + "_ptr = aero::CreateElementByTypeName(\"" +
                               escapedType + "\");\n";
                    }
                    src += "    aero::UIElement* " + varName + " = " + varName + "_ptr.Get();\n";
                    src += "    if (" + parentVar + " && " + varName + ") {\n";
                    src += "        " + parentVar + "->AddChild(" + varName + ", registry, uiHeap);\n";
                    src += "    }\n";
                }
                src += "    localElements.push_back(" + varName + ");\n";

                if (const auto probe = CreateElementByTypeName(typeName);
                    probe && probe->IsA<ItemsControl>()) {
                    src += "    if (auto* ic = " + varName + " ? " + varName + "->DynamicCast<aero::ItemsControl>() : nullptr) {\n";
                    src += "        ic->SetDomains(domains);\n";
                    src += "        ic->SetBindingEngine(bindingEngine);\n";
                    src += "        if (bindingEngine) {\n";
                    src += "            ic->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                    src += "        }\n";
                    src += "    }\n";
                }

                if (!elemName.empty()) {
                    src += "    if (" + varName + ") {\n";
                    src += "        " + varName + "->SetName(\"" + EscapeAotCppString(std::string(elemName)) + "\");\n";
                    for (const auto& namedSlot : package.namedControls) {
                        if (namedSlot.name == elemName &&
                            namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                            src += "        if (domains && registry && wrapperObj.valid()) {\n";
                            src += "            const auto h = " + varName + "->ToNativeHandle(*registry);\n";
                            src += "            (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                                   std::to_string(namedSlot.fieldIndexInClass) + "u, h, nullptr);\n";
                            src += "        }\n";
                            break;
                        }
                    }
                    src += "    }\n";
                }

                elemVarStack.push_back(varName);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elemVarStack.empty()) {
                    elemVarStack.pop_back();
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + ") {\n";
                    src += "        " + curVar + "->SetValue(" + std::to_string(inst.propId) + "u, " +
                           EmitUnifiedValueLiteralCpp(inst.literalValue) + ", aero::ValueSourceRank::Local);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!elemVarStack.empty() && inst.strIdx0 < package.bindingChains.size()) {
                    const std::string& curVar = elemVarStack.back();
                    const auto& chain = package.bindingChains[inst.strIdx0];
                    const bool isElemBind = !chain.elementName.empty() ||
                                            !chain.relativeAncestorType.empty() ||
                                            chain.relativeSelf;
                    src += "    if (" + curVar + " && bindingEngine" +
                           std::string(isElemBind ? "" : " && effectiveContext.valid()") + ") {\n";
                    src += "        aero::AxbCompiledBindingChain chain{};\n";
                    src += "        chain.rawPath = \"" + EscapeAotCppString(chain.rawPath) + "\";\n";
                    src += "        chain.targetPropertyId = " + std::to_string(chain.targetPropertyId) + "u;\n";
                    src += "        chain.mode = static_cast<aero::BindingMode>(" +
                           std::to_string(static_cast<unsigned>(chain.mode)) + "u);\n";
                    src += "        chain.converter = static_cast<aero::ValueConverterKind>(" +
                           std::to_string(static_cast<unsigned>(chain.converter)) + "u);\n";
                    if (!chain.elementName.empty()) {
                        src += "        chain.elementName = \"" + EscapeAotCppString(chain.elementName) + "\";\n";
                    }
                    if (!chain.relativeAncestorType.empty()) {
                        src += "        chain.relativeAncestorType = \"" + EscapeAotCppString(chain.relativeAncestorType) + "\";\n";
                    }
                    src += "        chain.relativeSelf = " + std::string(chain.relativeSelf ? "true" : "false") + ";\n";
                    if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                        src += "        chain.fallbackValue = " + EmitUnifiedValueLiteralCpp(chain.fallbackValue) + ";\n";
                    }
                    for (const auto& hop : chain.hops) {
                        src += "        {\n";
                        src += "            aero::AxbCompiledHop hop{};\n";
                        src += "            hop.memberName = \"" + EscapeAotCppString(hop.memberName) + "\";\n";
                        src += "            hop.declaringTypeId = " + std::to_string(hop.declaringTypeId) + "ULL;\n";
                        src += "            hop.fieldIndex = " + std::to_string(hop.fieldIndex) + "u;\n";
                        src += "            hop.observableBitIndex = " + std::to_string(static_cast<unsigned>(hop.observableBitIndex)) + "u;\n";
                        src += "            hop.getterSymbolId = " + std::to_string(hop.getterSymbolId) + "ULL;\n";
                        src += "            hop.setterSymbolId = " + std::to_string(hop.setterSymbolId) + "ULL;\n";
                        src += "            hop.isAutoProperty = " + std::string(hop.isAutoProperty ? "true" : "false") + ";\n";
                        src += "            hop.isObservable = " + std::string(hop.isObservable ? "true" : "false") + ";\n";
                        src += "            hop.isReadOnly = " + std::string(hop.isReadOnly ? "true" : "false") + ";\n";
                        src += "            hop.isSimState = " + std::string(hop.isSimState ? "true" : "false") + ";\n";
                        src += "            hop.isSimCommand = " + std::string(hop.isSimCommand ? "true" : "false") + ";\n";
                        src += "            chain.hops.push_back(std::move(hop));\n";
                        src += "        }\n";
                    }
                    src += "        (void)bindingEngine->BindChain(effectiveContext, chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    std::string canonicalComp(compName);
                    if (canonicalComp.find("::") == std::string::npos) {
                        if (const std::size_t lastDot = canonicalComp.rfind('.'); lastDot != std::string::npos) {
                            canonicalComp.replace(lastDot, 1, "::");
                        }
                    }
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : rs_sem::stableTypeId(canonicalComp);
                    src += "    if (" + curVar + " && bindingEngine) {\n";
                    src += "        (void)bindingEngine->BindEcsColumn(" + std::to_string(compId) + "ULL, \"" +
                           EscapeAotCppString(std::string(fieldName)) + "\", 0u, aero::IntrusivePtr<aero::UIElement>(" +
                           curVar + ", true), " + std::to_string(inst.propId) + "u);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    SymbolId handlerSym = inst.symbolId;
                    if (handlerSym == 0 && inst.strIdx0 < package.eventBindings.size()) {
                        handlerSym = package.eventBindings[inst.strIdx0].handlerSymbolId;
                    }
                    src += "    if (" + curVar + " && wrapperObj.valid() && " +
                           std::to_string(handlerSym) + "ULL != 0ULL) {\n";
                    src += "        if (auto* btn = " + curVar + "->DynamicCast<aero::Button>()) {\n";
                    src += "            if (eventInterpreter) {\n";
                    src += "                btn->BindScriptClickHandler(eventInterpreter, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            } else if (eventAotContext) {\n";
                    src += "                btn->BindAotClickHandler(eventAotContext, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            }\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::AddRowDefinition: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (auto* grid = " + curVar + "->DynamicCast<aero::Grid>()) {\n";
                    src += "            grid->AddRowDefinition(static_cast<aero::GridUnitType>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u), " +
                           FormatFloatLiteral(val) + ");\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::AddColumnDefinition: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (auto* grid = " + curVar + "->DynamicCast<aero::Grid>()) {\n";
                    src += "            grid->AddColumnDefinition(static_cast<aero::GridUnitType>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u), " +
                           FormatFloatLiteral(val) + ");\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::SetResourceProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string escapedKey = EscapeAotCppString(std::string(package.GetString(inst.strIdx0)));
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                           escapedKey + "\")) {\n";
                    src += "            " + curVar + "->SetValue(" + std::to_string(inst.propId) +
                           "u, *rv, aero::ValueSourceRank::Local);\n";
                    if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        src += "        } else {\n";
                        src += "            " + curVar + "->SetValue(" + std::to_string(inst.propId) + "u, " +
                               EmitUnifiedValueLiteralCpp(inst.literalValue) + ", aero::ValueSourceRank::Local);\n";
                    }
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }
        }
    }

    if (package.HasMotionSection()) {
        const std::vector<std::uint8_t> motPkgBytes = package.Serialize();
        if (!motPkgBytes.empty()) {
            src += "    if (bindingEngine && bindingEngine->GetAnimationEngine()) {\n";
            src += "        static constexpr std::uint8_t kAotMotionPkgBytes[] = {";
            for (std::size_t b = 0; b < motPkgBytes.size(); ++b) {
                if (b > 0) src += ",";
                src += std::to_string(static_cast<unsigned>(motPkgBytes[b])) + "u";
            }
            src += "};\n";
            src += "        static const aero::AxbPackage kAotMotionPkg = []() {\n";
            src += "            aero::AxbPackage p{};\n";
            src += "            (void)aero::AxbPackage::Deserialize(kAotMotionPkgBytes, sizeof(kAotMotionPkgBytes), p);\n";
            src += "            p.PinValuesInPools();\n";
            src += "            return p;\n";
            src += "        }();\n";
            src += "        (void)bindingEngine->GetAnimationEngine()->InstantiatePackageMotion(\n";
            src += "            rootControl,\n";
            src += "            localElements.data(),\n";
            src += "            localElements.size(),\n";
            src += "            kAotMotionPkg);\n";
            src += "    }\n";
        }
    }

    src += "    return " + std::string(rootVisited ? "true" : "false") + ";\n";
    src += "}\n\n";
    src += "} // namespace " + out.cppNamespace + "\n";
    out.source = std::move(src);
    return out;
}

} // namespace aero
