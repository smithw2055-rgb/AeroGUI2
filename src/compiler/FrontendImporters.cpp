#include "aero/compiler/AeroSceneIR.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "AeroCompilerInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

namespace {

void EmitStaticFloatIfChanged(
    AxbPackage& pkg,
    std::uint16_t propId,
    float value,
    float defaultValue) {
    if (std::fabs(value - defaultValue) > 1e-4f) {
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::SetStaticProperty, propId, 0, 0, 0, 0, MakeFloat(value)});
    }
}

void BakePathIntoPackageIfNew(
    std::string_view dPath,
    std::unordered_set<std::uint64_t>& bakedHashes,
    AxbPackage& pkg) {
    if (dPath.empty()) {
        return;
    }
    const std::uint64_t h = ComputeSvgPathHash(dPath);
    if (h != 0 && bakedHashes.insert(h).second) {
        const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
        pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
    }
}

void EmitDecomposedSvgMatrixInstructions(const Matrix2D& m, AxbPackage& pkg) {
    const DecomposedTransform2D d = PolarDecomposeMatrix2D(m);
    EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateX, d.translateX, 0.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateY, d.translateY, 0.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimRotateAngle, d.rotateDeg, 0.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimScaleX, d.scaleX, 1.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimScaleY, d.scaleY, 1.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimSkewX, d.skewXDeg, 0.0f);
    EmitStaticFloatIfChanged(pkg, PropId_AnimSkewY, d.skewYDeg, 0.0f);
}

Matrix2D ParseSvgTransformAttr(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (str::StartsWith(raw, "matrix(") && raw.back() == ')') {
        return ParseXamlMatrix2DLiteral(raw.substr(7u, raw.size() - 8u));
    }
    if (str::StartsWith(raw, "translate(") && raw.back() == ')') {
        const auto inner = raw.substr(10u, raw.size() - 11u);
        const std::size_t sep = inner.find_first_of(", ");
        const float tx = compiler_detail::ParseFloat32(
            sep == std::string_view::npos ? inner : inner.substr(0u, sep), 0.0f);
        const float ty = sep == std::string_view::npos
            ? 0.0f
            : compiler_detail::ParseFloat32(str::Trim(inner.substr(sep + 1u)), 0.0f);
        return Matrix2D::Translation(tx, ty);
    }
    if (str::StartsWith(raw, "scale(") && raw.back() == ')') {
        const auto inner = raw.substr(6u, raw.size() - 7u);
        const std::size_t sep = inner.find_first_of(", ");
        const float sx = compiler_detail::ParseFloat32(
            sep == std::string_view::npos ? inner : inner.substr(0u, sep), 1.0f);
        const float sy = sep == std::string_view::npos
            ? sx
            : compiler_detail::ParseFloat32(str::Trim(inner.substr(sep + 1u)), sx);
        return Matrix2D::Scaling(sx, sy);
    }
    if (str::StartsWith(raw, "rotate(") && raw.back() == ')') {
        const auto inner = raw.substr(7u, raw.size() - 8u);
        return Matrix2D::Rotation(compiler_detail::ParseFloat32(inner, 0.0f));
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
            std::size_t endQuote = i + 1u;
            while (endQuote < obj.size() && !(obj[endQuote] == '\"' && obj[endQuote - 1u] != '\\')) {
                ++endQuote;
            }
            if (endQuote >= obj.size()) break;
            if (depth <= 1 && (endQuote - i + 1u) == pattern.size() &&
                obj.substr(i, pattern.size()) == pattern) {
                std::size_t pos = endQuote + 1u;
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
                        std::size_t valEnd = pos + 1u;
                        while (valEnd < obj.size() && !(obj[valEnd] == '\"' && obj[valEnd - 1u] != '\\')) {
                            ++valEnd;
                        }
                        return obj.substr(pos + 1u, valEnd - (pos + 1u));
                    }
                    if (startChar == '{' || startChar == '[') {
                        const char closeChar = (startChar == '{') ? '}' : ']';
                        int subDepth = 0;
                        bool subStr = false;
                        for (std::size_t j = pos; j < obj.size(); ++j) {
                            if (obj[j] == '\"' && (j == pos || obj[j - 1u] != '\\')) {
                                subStr = !subStr;
                                continue;
                            }
                            if (subStr) continue;
                            if (obj[j] == startChar) ++subDepth;
                            else if (obj[j] == closeChar) {
                                --subDepth;
                                if (subDepth == 0) return obj.substr(pos, j - pos + 1u);
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
    arr = str::Trim(arr);
    if (arr.size() < 2u || arr.front() != '[' || arr.back() != ']') return items;
    arr = str::Trim(arr.substr(1u, arr.size() - 2u));
    int depth = 0;
    bool inStr = false;
    std::size_t itemStart = 0u;
    for (std::size_t i = 0u; i < arr.size(); ++i) {
        const char c = arr[i];
        if (c == '\"' && (i == 0u || arr[i - 1u] != '\\')) inStr = !inStr;
        if (inStr) continue;
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            const auto piece = str::Trim(arr.substr(itemStart, i - itemStart));
            if (!piece.empty()) items.push_back(piece);
            itemStart = i + 1u;
        }
    }
    const auto tail = str::Trim(arr.substr(itemStart));
    if (!tail.empty()) items.push_back(tail);
    return items;
}

float ExtractFirstFloatFromJsonToken(std::string_view tok, float fallback) noexcept {
    tok = str::Trim(tok);
    if (tok.empty()) return fallback;
    if (tok.front() == '[') {
        const auto elems = SplitJsonTopLevelArray(tok);
        if (!elems.empty()) return compiler_detail::ParseFloat32(elems[0], fallback);
        return fallback;
    }
    return compiler_detail::ParseFloat32(tok, fallback);
}

void EncodeCubicBezierKeyframe(
    AxbFlatKeyframe& kf,
    float bx1,
    float by1,
    float bx2,
    float by2) noexcept {
    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
    bx1 = std::clamp(bx1, 0.0f, 1.0f);
    by1 = std::clamp(by1, -4.0f, 4.0f);
    bx2 = std::clamp(bx2, 0.0f, 1.0f);
    by2 = std::clamp(by2, -4.0f, 4.0f);
    kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(std::round(bx1 * 255.0f));
    kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(std::round(bx2 * 255.0f));
    kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::round(by1 * 256.0f));
    kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::round(by2 * 256.0f));
}

} // namespace

namespace compiler_detail {

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

bool IsSvgAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4u && str::EqualsIgnoreCase(path.substr(path.size() - 4u), ".svg")) {
        return true;
    }
    const std::string_view trimmed = str::Trim(content);
    return str::StartsWith(trimmed, "<svg");
}

bool IsLottieAssetInput(std::string_view path, std::string_view content) noexcept {
    if ((path.size() >= 5u && str::EqualsIgnoreCase(path.substr(path.size() - 5u), ".json")) ||
        (path.size() >= 7u && str::EqualsIgnoreCase(path.substr(path.size() - 7u), ".lottie"))) {
        const std::string_view trimmed = str::Trim(content);
        if (!trimmed.empty() && trimmed.front() == '{' &&
            trimmed.find("\"layers\"") != std::string_view::npos) {
            return true;
        }
    }
    const std::string_view trimmed = str::Trim(content);
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"fr\"") != std::string_view::npos &&
           trimmed.find("\"layers\"") != std::string_view::npos;
}

bool IsRiveAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4u && str::EqualsIgnoreCase(path.substr(path.size() - 4u), ".riv")) {
        return true;
    }
    const std::string_view trimmed = str::Trim(content);
    if (str::StartsWith(trimmed, "RIVE")) {
        return true;
    }
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"artboard\"") != std::string_view::npos &&
           trimmed.find("\"stateMachines\"") != std::string_view::npos;
}

} // namespace compiler_detail

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
        pos = lt + 1u;
        if (pos >= src.size()) break;
        if (str::StartsWith(src.substr(pos), "!--")) {
            const std::size_t endC = src.find("-->", pos + 3u);
            pos = (endC == std::string_view::npos) ? src.size() : (endC + 3u);
            continue;
        }
        if (src[pos] == '?') {
            const std::size_t endD = src.find("?>", pos + 1u);
            pos = (endD == std::string_view::npos) ? src.size() : (endD + 2u);
            continue;
        }
        if (src[pos] == '/') {
            const std::size_t gt = src.find('>', pos + 1u);
            const std::string_view closeTag = str::StripNamespacePrefix(
                str::Trim(src.substr(pos + 1u, (gt == std::string_view::npos ? src.size() : gt) - (pos + 1u))));
            if ((closeTag == "svg" || closeTag == "g") && openContainers > 0u) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
                --openContainers;
            }
            pos = (gt == std::string_view::npos) ? src.size() : (gt + 1u);
            continue;
        }

        std::size_t tagEnd = pos;
        while (tagEnd < src.size() && !std::isspace(static_cast<unsigned char>(src[tagEnd])) &&
               src[tagEnd] != '>' && src[tagEnd] != '/') {
            ++tagEnd;
        }
        const std::string_view shortTag = str::StripNamespacePrefix(src.substr(pos, tagEnd - pos));
        pos = tagEnd;

        std::vector<std::pair<std::string_view, std::string_view>> attrs;
        bool selfClosing = false;
        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
            if (pos >= src.size()) break;
            if (src[pos] == '>') { ++pos; break; }
            if (str::StartsWith(src.substr(pos), "/>")) { selfClosing = true; pos += 2u; break; }
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
                if (kv.first == "x:Class") pkg.className = std::string(str::Trim(kv.second));
                else if (kv.first == "id" || kv.first == "x:Name") elemId = str::Trim(kv.second);
                else if (kv.first == "width") viewW = compiler_detail::ParseFloat32(kv.second, viewW);
                else if (kv.first == "height") viewH = compiler_detail::ParseFloat32(kv.second, viewH);
                else if (kv.first == "viewBox") {
                    std::string_view vb = str::Trim(kv.second);
                    float vals[4]{0.0f, 0.0f, viewW, viewH};
                    for (int vi = 0; vi < 4 && !vb.empty(); ++vi) {
                        const std::size_t sep = vb.find_first_of(", ");
                        vals[vi] = compiler_detail::ParseFloat32(
                            sep == std::string_view::npos ? vb : vb.substr(0u, sep), vals[vi]);
                        if (sep == std::string_view::npos) break;
                        vb = str::Trim(vb.substr(sep + 1u));
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

            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(viewW)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(viewH)});
            rootEmitted = true;
            if (selfClosing) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
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
                if (kv.first == "id" || kv.first == "x:Name") gName = str::Trim(kv.second);
                else if (kv.first == "transform") { gMat = ParseSvgTransformAttr(kv.second); hasMat = true; }
                else if (kv.first == "opacity") gOpacity = compiler_detail::ParseFloat32(kv.second, 1.0f);
            }
            AxbInstruction beginG{};
            beginG.opcode = AxbOpcode::BeginElement;
            beginG.strIdx0 = pkg.InternString("Canvas");
            beginG.strIdx1 = pkg.InternString(gName);
            pkg.instructions.push_back(std::move(beginG));
            if (!gName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{
                    std::string(gName), "Canvas", 0, PropertySlotHandle::InvalidIndex});
            }
            EmitStaticFloatIfChanged(pkg, PropId_Opacity, gOpacity, 1.0f);
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
                if (kv.first == "id" || kv.first == "x:Name") pName = str::Trim(kv.second);
                else if (kv.first == "d" || kv.first == "Data") dPath = str::Trim(kv.second);
                else if (kv.first == "fill" || kv.first == "Fill") fillStr = str::Trim(kv.second);
                else if (kv.first == "stroke" || kv.first == "Stroke") strokeStr = str::Trim(kv.second);
                else if (kv.first == "stroke-width" || kv.first == "StrokeThickness") {
                    strokeW = compiler_detail::ParseFloat32(kv.second, 0.0f);
                } else if (kv.first == "opacity" || kv.first == "Opacity") {
                    opacity = compiler_detail::ParseFloat32(kv.second, 1.0f);
                } else if (kv.first == "width" || kv.first == "Width") {
                    pathW = compiler_detail::ParseFloat32(kv.second, viewW);
                } else if (kv.first == "height" || kv.first == "Height") {
                    pathH = compiler_detail::ParseFloat32(kv.second, viewH);
                } else if (kv.first == "transform") {
                    pMat = ParseSvgTransformAttr(kv.second);
                    hasMat = true;
                }
            }

            BakePathIntoPackageIfNew(dPath, bakedHashes, pkg);

            AxbInstruction beginP{};
            beginP.opcode = AxbOpcode::BeginElement;
            beginP.strIdx0 = pkg.InternString("Path");
            beginP.strIdx1 = pkg.InternString(pName);
            pkg.instructions.push_back(std::move(beginP));
            if (!pName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{
                    std::string(pName), "Path", 0, PropertySlotHandle::InvalidIndex});
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(pathW)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(pathH)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            if (!dPath.empty()) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            }
            if (!fillStr.empty() && !str::EqualsIgnoreCase(fillStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0,
                    ParseBuiltinLiteralPropertyValue(PropId_Background, fillStr)});
            }
            if (!strokeStr.empty() && !str::EqualsIgnoreCase(strokeStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_BorderBrush, 0, 0, 0, 0,
                    ParseBuiltinLiteralPropertyValue(PropId_BorderBrush, strokeStr)});
            }
            if (strokeW > 0.0f) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_BorderThickness, 0, 0, 0, 0,
                    value::FromThickness(Thickness(strokeW))});
            }
            EmitStaticFloatIfChanged(pkg, PropId_Opacity, opacity, 1.0f);
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

    compiler_detail::PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::SvgVectorToken, sourcePath, outIr);
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
    const float fps = compiler_detail::ParseFloat32(FindJsonFieldValue(json, "fr"), 60.0f);
    const float inFrame = compiler_detail::ParseFloat32(FindJsonFieldValue(json, "ip"), 0.0f);
    const float outFrame = compiler_detail::ParseFloat32(FindJsonFieldValue(json, "op"), 60.0f);
    const float compW = compiler_detail::ParseFloat32(FindJsonFieldValue(json, "w"), 256.0f);
    const float compH = compiler_detail::ParseFloat32(FindJsonFieldValue(json, "h"), 256.0f);
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

    auto extractCompFloat = [](std::string_view arrOrScalar, float fallback, int componentIdx, float scale) -> float {
        if (!arrOrScalar.empty() && arrOrScalar.front() == '[') {
            const auto comps = SplitJsonTopLevelArray(arrOrScalar);
            if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                return compiler_detail::ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], fallback) * scale;
            }
            if (!comps.empty()) {
                return compiler_detail::ParseFloat32(comps[0], fallback) * scale;
            }
            return fallback;
        }
        return compiler_detail::ParseFloat32(arrOrScalar, fallback) * scale;
    };

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
            if (!kVal.empty() && kVal.front() == '[') {
                const auto comps = SplitJsonTopLevelArray(kVal);
                if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                    return compiler_detail::ParseFloat32(
                        comps[static_cast<std::size_t>(componentIdx)], defaultStaticVal) * scaleFactor;
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
            const float tFrame = compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "t"), inFrame);
            const std::string_view sArr = FindJsonFieldValue(kfObj, "s");
            const std::string_view eArr = FindJsonFieldValue(kfObj, "e");
            float val = hasPrevEnd ? prevEndVal : defaultStaticVal;
            if (!sArr.empty()) {
                val = extractCompFloat(sArr, val, componentIdx, scaleFactor);
            }
            if (!eArr.empty()) {
                prevEndVal = extractCompFloat(eArr, val, componentIdx, scaleFactor);
                hasPrevEnd = true;
            }
            if (ki == 0) firstVal = val;

            AxbFlatKeyframe kf{};
            kf.timeSec = std::max(0.0f, (tFrame - inFrame) / std::max(1.0f, fps));
            kf.value = Float4Value{val, 0.0f, 0.0f, 0.0f};
            const std::string_view oObj = FindJsonFieldValue(kfObj, "o");
            const std::string_view iObj = FindJsonFieldValue(kfObj, "i");
            if (!oObj.empty() && !iObj.empty()) {
                EncodeCubicBezierKeyframe(
                    kf,
                    ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "x"), 0.25f),
                    ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "y"), 0.25f),
                    ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "x"), 0.75f),
                    ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "y"), 0.75f));
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
        const int ind = static_cast<int>(compiler_detail::ParseFloat32(FindJsonFieldValue(layerObj, "ind"), -1.0f));
        const int parentInd = static_cast<int>(compiler_detail::ParseFloat32(FindJsonFieldValue(layerObj, "parent"), -1.0f));
        const std::string_view dPath = FindJsonFieldValue(layerObj, "d");
        const std::string_view fillHex = FindJsonFieldValue(layerObj, "fill");
        const float lw = compiler_detail::ParseFloat32(FindJsonFieldValue(layerObj, "w"), compW);
        const float lh = compiler_detail::ParseFloat32(FindJsonFieldValue(layerObj, "h"), compH);

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
            BakePathIntoPackageIfNew(dPath, bakedHashes, pkg);
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
        }
        if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0,
                ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
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

            EmitStaticFloatIfChanged(pkg, PropId_Opacity, op, 1.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimRotateAngle, rot, 0.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateX, tx, 0.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateY, ty, 0.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimScaleX, sx, 1.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimScaleY, sy, 1.0f);
            EmitStaticFloatIfChanged(pkg, PropId_AnimSkewX, skx, 0.0f);
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

    compiler_detail::PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::LottieJson, sourcePath, outIr);
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
    if (IsRiveBinaryStream(rawBytes, byteLength)) {
        AxbPackage binPkg{};
        if (!DecodeRiveBinaryToAxbPackage(rawBytes, byteLength, binPkg, &outDiagnostics)) {
            return false;
        }
        compiler_detail::PopulateSceneIrFromPackage(binPkg, FrontendSourceFormat::RiveBinary, sourcePath, outIr);
        return true;
    }
    std::string_view content(reinterpret_cast<const char*>(rawBytes), byteLength);
    if (str::StartsWith(content, "RIVE")) {
        content = str::Trim(content.substr(4u));
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
    const float abW = compiler_detail::ParseFloat32(FindJsonFieldValue(abSource, "width"), 320.0f);
    const float abH = compiler_detail::ParseFloat32(FindJsonFieldValue(abSource, "height"), 320.0f);

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
        const float nw = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "width"), abW);
        const float nh = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "height"), abH);
        const float nx = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "x"), 0.0f);
        const float ny = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "y"), 0.0f);
        const float nRot = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "rotation"), 0.0f);
        const float nsx = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "scaleX"), 1.0f);
        const float nsy = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "scaleY"), 1.0f);
        const float nOp = compiler_detail::ParseFloat32(FindJsonFieldValue(nodeObj, "opacity"), 1.0f);

        const std::uint32_t curIdx = nextElemIdx++;
        if (!nName.empty()) {
            elemIndexByName.emplace(std::string(nName), curIdx);
        }

        const std::string_view runtimeType = (!dPath.empty() || str::EqualsIgnoreCase(nType, "Path")) ? "Path"
                                           : (str::EqualsIgnoreCase(nType, "TextBlock") ? "TextBlock" : "Border");
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
        EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateX, nx, 0.0f);
        EmitStaticFloatIfChanged(pkg, PropId_AnimTranslateY, ny, 0.0f);
        EmitStaticFloatIfChanged(pkg, PropId_AnimRotateAngle, nRot, 0.0f);
        EmitStaticFloatIfChanged(pkg, PropId_AnimScaleX, nsx, 1.0f);
        EmitStaticFloatIfChanged(pkg, PropId_AnimScaleY, nsy, 1.0f);
        EmitStaticFloatIfChanged(pkg, PropId_Opacity, nOp, 1.0f);

        if (!dPath.empty()) {
            BakePathIntoPackageIfNew(dPath, bakedHashes, pkg);
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
        }
        if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0,
                ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
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
        clip.durationSec = std::max(0.001f, compiler_detail::ParseFloat32(FindJsonFieldValue(clObj, "duration"), 1.0f));
        clip.isLooping = str::EqualsIgnoreCase(FindJsonFieldValue(clObj, "loop"), "true");
        clip.firstTrackIdx = static_cast<std::uint32_t>(pkg.motionTracks.size());
        clip.firstEventIdx = static_cast<std::uint32_t>(pkg.motionEvents.size());

        const auto trObjs = SplitJsonTopLevelArray(FindJsonFieldValue(clObj, "tracks"));
        for (const auto& trObj : trObjs) {
            const std::string targetName(FindJsonFieldValue(trObj, "target"));
            const std::string_view propName = FindJsonFieldValue(trObj, "property");
            const auto itElem = elemIndexByName.find(targetName);
            const std::uint32_t targetIdx = (itElem != elemIndexByName.end()) ? itElem->second : 0u;
            const std::uint16_t propId = ResolveBuiltinPropertyId(propName);
            if (propId == compiler_detail::kUnmappedPropId) continue;

            AxbMotionTrackDesc tr{};
            tr.localElementIdx = static_cast<std::uint16_t>(targetIdx);
            tr.targetPropId = propId;
            tr.channelCount = 1u;
            tr.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());
            const auto kfObjs = SplitJsonTopLevelArray(FindJsonFieldValue(trObj, "keyframes"));
            for (const auto& kfObj : kfObjs) {
                AxbFlatKeyframe kf{};
                kf.timeSec = compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "t"), 0.0f);
                const std::string_view vRaw = FindJsonFieldValue(kfObj, "v");
                const UnifiedValue parsedV = ParseBuiltinLiteralPropertyValue(propId, vRaw);
                if (!parsedV.tryGetQuatf(kf.value)) {
                    float sf = 0.0f;
                    (void)TryGetFloat(parsedV, sf);
                    kf.value = Float4Value{sf, 0.0f, 0.0f, 0.0f};
                } else {
                    tr.channelCount = 4u;
                }
                const std::string_view easeStr = FindJsonFieldValue(kfObj, "easing");
                if (str::EqualsIgnoreCase(easeStr, "CubicBezier")) {
                    EncodeCubicBezierKeyframe(
                        kf,
                        compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "bx1"), 0.25f),
                        compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "by1"), 0.1f),
                        compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "bx2"), 0.25f),
                        compiler_detail::ParseFloat32(FindJsonFieldValue(kfObj, "by2"), 1.0f));
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
            ev.timeSec = compiler_detail::ParseFloat32(FindJsonFieldValue(evObj, "t"), 0.0f);
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
            if (str::EqualsIgnoreCase(inType, "Boolean") || str::EqualsIgnoreCase(inType, "Bool")) {
                inDesc.valueKind = 0u;
                inDesc.defaultValue = UnifiedValue{str::EqualsIgnoreCase(FindJsonFieldValue(inObj, "default"), "true")};
            } else if (str::EqualsIgnoreCase(inType, "Trigger")) {
                inDesc.valueKind = 5u;
                inDesc.isTrigger = 1u;
                inDesc.defaultValue = MakeFloat(0.0f);
            } else {
                inDesc.valueKind = 1u;
                inDesc.defaultValue = MakeFloat(compiler_detail::ParseFloat32(FindJsonFieldValue(inObj, "default"), 0.0f));
            }
            pkg.contractInputs.push_back(inDesc);
            if (!inName.empty()) inputIdxByName.emplace(std::string(inName), slotIdx);
        }

        const auto layerObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "layers"));
        for (const auto& lyObj : layerObjs) {
            AxbSmLayerDesc ly{};
            ly.nameStrIdx = pkg.InternString(FindJsonFieldValue(lyObj, "name"));
            ly.blendMode = str::EqualsIgnoreCase(FindJsonFieldValue(lyObj, "blendMode"), "Additive")
                ? SmLayerBlendMode::Additive
                : SmLayerBlendMode::Override;
            ly.layerWeight = compiler_detail::ParseFloat32(FindJsonFieldValue(lyObj, "weight"), 1.0f);
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
                st.speedMultiplier = compiler_detail::ParseFloat32(FindJsonFieldValue(stObj, "speed"), 1.0f);

                if (str::EqualsIgnoreCase(stKind, "Blend1D")) {
                    st.kind = SmStateKind::Blend1D;
                    const std::string paramName(FindJsonFieldValue(stObj, "input"));
                    const auto itIn = inputIdxByName.find(paramName);
                    st.progressInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
                    const auto bItems = SplitJsonTopLevelArray(FindJsonFieldValue(stObj, "thresholds"));
                    if (!bItems.empty()) {
                        st.blendRangeMin = compiler_detail::ParseFloat32(FindJsonFieldValue(bItems.front(), "threshold"), 0.0f);
                        const std::string cl0(FindJsonFieldValue(bItems.front(), "clip"));
                        const auto it0 = clipIndexByName.find(cl0);
                        st.primaryClipIdx = (it0 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it0->second) : 0u;
                        if (bItems.size() >= 2u) {
                            st.blendRangeMax = compiler_detail::ParseFloat32(FindJsonFieldValue(bItems.back(), "threshold"), 100.0f);
                            const std::string cl1(FindJsonFieldValue(bItems.back(), "clip"));
                            const auto it1 = clipIndexByName.find(cl1);
                            st.secondaryClipIdx = (it1 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it1->second) : st.primaryClipIdx;
                        } else {
                            st.blendRangeMax = st.blendRangeMin + 1.0f;
                            st.secondaryClipIdx = st.primaryClipIdx;
                        }
                    }
                } else if (str::EqualsIgnoreCase(stKind, "ScrubbedClip")) {
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
                tr.crossfadeDurationSec = compiler_detail::ParseFloat32(FindJsonFieldValue(trObj, "duration"), 0.15f);
                tr.exitTimeNormalized01 = compiler_detail::ParseFloat32(FindJsonFieldValue(trObj, "exitTime"), 0.0f);
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
                    if (opStr == "==" || str::EqualsIgnoreCase(opStr, "Equals")) cond.op = SmCompareOp::Equal;
                    else if (opStr == "!=" || str::EqualsIgnoreCase(opStr, "NotEquals")) cond.op = SmCompareOp::NotEqual;
                    else if (opStr == ">" || str::EqualsIgnoreCase(opStr, "Greater")) cond.op = SmCompareOp::Greater;
                    else if (opStr == ">=" || str::EqualsIgnoreCase(opStr, "GreaterOrEqual")) cond.op = SmCompareOp::GreaterOrEqual;
                    else if (opStr == "<" || str::EqualsIgnoreCase(opStr, "Less")) cond.op = SmCompareOp::Less;
                    else if (opStr == "<=" || str::EqualsIgnoreCase(opStr, "LessOrEqual")) cond.op = SmCompareOp::LessOrEqual;
                    else if (str::EqualsIgnoreCase(opStr, "TriggerFired")) cond.op = SmCompareOp::TriggerFired;
                    else if (str::EqualsIgnoreCase(opStr, "IsTrue")) cond.op = SmCompareOp::BoolTrue;
                    else if (str::EqualsIgnoreCase(opStr, "IsFalse")) cond.op = SmCompareOp::BoolFalse;
                    cond.threshold = compiler_detail::ParseFloat32(FindJsonFieldValue(cObj, "value"), 0.0f);
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
            if (str::EqualsIgnoreCase(evStr, "PointerExit")) lst.eventKind = SmListenerEventKind::PointerExit;
            else if (str::EqualsIgnoreCase(evStr, "PointerDown")) lst.eventKind = SmListenerEventKind::PointerDown;
            else if (str::EqualsIgnoreCase(evStr, "PointerUp") || str::EqualsIgnoreCase(evStr, "Click")) {
                lst.eventKind = SmListenerEventKind::PointerUpInside;
            } else if (str::EqualsIgnoreCase(evStr, "FocusGained")) lst.eventKind = SmListenerEventKind::FocusGained;
            else if (str::EqualsIgnoreCase(evStr, "FocusLost")) lst.eventKind = SmListenerEventKind::FocusLost;
            else lst.eventKind = SmListenerEventKind::PointerEnter;

            const std::string inName(FindJsonFieldValue(lObj, "input"));
            const auto itIn = inputIdxByName.find(inName);
            lst.targetInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
            const std::string_view actStr = FindJsonFieldValue(lObj, "action");
            if (str::EqualsIgnoreCase(actStr, "FireTrigger")) lst.actionKind = SmListenerActionKind::FireTrigger;
            else if (str::EqualsIgnoreCase(actStr, "AddFloat") || str::EqualsIgnoreCase(actStr, "AddDelta")) {
                lst.actionKind = SmListenerActionKind::AddDelta;
            } else {
                lst.actionKind = SmListenerActionKind::SetFloatOrBool;
            }
            lst.actionValue = compiler_detail::ParseFloat32(FindJsonFieldValue(lObj, "value"), 1.0f);
            pkg.smListeners.push_back(lst);
        }
        sm.listenerCount = static_cast<std::uint16_t>(pkg.smListeners.size() - sm.firstListenerIdx);

        pkg.stateMachines.push_back(sm);
    }

    compiler_detail::PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::RiveBinary, sourcePath, outIr);
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

} // namespace aero
