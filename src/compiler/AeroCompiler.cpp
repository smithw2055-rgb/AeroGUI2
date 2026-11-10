#include "aero/compiler/AeroCompiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

AeroCompileResult AeroCompiler::Compile(const AeroCompileInput& input) const {
    AeroCompileResult result{};
    AxbPackage& pkg = result.axbPackage;
    pkg.InternString("");

    std::string rootDataType;
    const std::string_view src = input.xamlContent;
    std::size_t pos = 0;
    bool seenRoot = false;
    std::unordered_set<std::uint64_t> bakedPathHashes;

    auto ensurePathBakedIntoPackage = [&](std::string_view svgPathData) {
        svgPathData = Trim(svgPathData);
        if (svgPathData.empty()) {
            return;
        }
        const std::uint64_t h = ComputeSvgPathHash(svgPathData);
        if (h == 0 || !bakedPathHashes.insert(h).second) {
            return;
        }
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
        pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(svgPathData, 32u, 4.0f, slotIdx, 256u));
    };

    enum class XmlScopeKind : std::uint8_t {
        VisualElement = 0,
        TransparentContainer = 1,
        RunInline = 2,
        ResourceContainer = 3,
        KeyedResourceItem = 4,
        BrushPropertyElement = 5,
        NonVisualSkip = 6,
    };

    struct XmlScopeEntry {
        XmlScopeKind kind = XmlScopeKind::VisualElement;
        std::string tagName;
        std::string resourceKey;
        std::uint16_t targetPropId = PropId_Background;
        bool brushAssigned = false;
        std::string accumulatedText;
    };

    std::vector<XmlScopeEntry> scopeStack;
    std::size_t nonVisualDepth = 0;

    auto findLocalOrGlobalResource = [&](std::string_view key) -> const UnifiedValue* {
        for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
            if (it->first == key) {
                return &it->second;
            }
        }
        return AxbTemplateRegistry::Instance().FindResource(key);
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
               shortTag == "Setter" ||
               shortTag == "TransformGroup" ||
               shortTag == "ScaleTransform" ||
               shortTag == "RotateTransform" ||
               shortTag == "TranslateTransform" ||
               shortTag == "SkewTransform" ||
               shortTag == "MatrixTransform" ||
               shortTag == "DoubleAnimation" ||
               shortTag == "DoubleAnimationUsingKeyFrames" ||
               shortTag == "ColorAnimation" ||
               shortTag == "ColorAnimationUsingKeyFrames" ||
               shortTag == "BooleanAnimationUsingKeyFrames" ||
               shortTag == "ObjectAnimationUsingKeyFrames" ||
               shortTag == "PointAnimationUsingKeyFrames" ||
               shortTag == "EasingDoubleKeyFrame" ||
               shortTag == "LinearDoubleKeyFrame" ||
               shortTag == "DiscreteDoubleKeyFrame" ||
               shortTag == "SplineDoubleKeyFrame" ||
               shortTag == "EasingColorKeyFrame" ||
               shortTag == "LinearColorKeyFrame" ||
               shortTag == "DiscreteBooleanKeyFrame" ||
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
                        st == "CheckBox" || st == "RadioButton" || st == "Hyperlink") {
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
                XmlScopeEntry closed = std::move(scopeStack.back());
                scopeStack.pop_back();
                if (closed.kind == XmlScopeKind::ResourceContainer ||
                    closed.kind == XmlScopeKind::KeyedResourceItem ||
                    closed.kind == XmlScopeKind::BrushPropertyElement ||
                    closed.kind == XmlScopeKind::NonVisualSkip) {
                    if (nonVisualDepth > 0) {
                        --nonVisualDepth;
                    }
                }
                if (closed.kind == XmlScopeKind::KeyedResourceItem &&
                    !closed.resourceKey.empty()) {
                    const std::string_view st = StripXmlPrefix(closed.tagName);
                    if (st == "Color" && !closed.accumulatedText.empty()) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromColor(ParseXamlColorLiteral(closed.accumulatedText)));
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
                } else if (closed.kind == XmlScopeKind::ResourceContainer && scopeStack.empty()) {
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

        // Inside non-visual scope: handle resource extraction or brush property assignment
        if (nonVisualDepth > 0) {
            XmlScopeEntry& parentScope = scopeStack.back();
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
                                shortTag == "Color" || shortTag == "String" || shortTag == "FlowDirection")) {
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
                XmlScopeEntry skipScope{};
                skipScope.kind = XmlScopeKind::NonVisualSkip;
                skipScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(skipScope));
                ++nonVisualDepth;
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

                if (inner == "Binding" || inner == "Bind") {
                    inner = {};
                } else if (inner.rfind("Binding ", 0) == 0) {
                    inner = Trim(inner.substr(8));
                } else if (inner.rfind("Bind ", 0) == 0) {
                    inner = Trim(inner.substr(5));
                } else {
                    continue;
                }

                BindingMode mode = BindingMode::OneWay;
                std::string_view pathPart;
                std::string_view fallbackValue;

                std::size_t clausePos = 0;
                bool firstClause = true;
                while (clausePos < inner.size()) {
                    const std::size_t comma = inner.find(',', clausePos);
                    const std::string_view clause = Trim(
                        (comma == std::string_view::npos)
                            ? inner.substr(clausePos)
                            : inner.substr(clausePos, comma - clausePos));
                    if (clause.rfind("Path=", 0) == 0) {
                        pathPart = Trim(clause.substr(5));
                    } else if (clause.rfind("Mode=", 0) == 0) {
                        const std::string_view modeStr = Trim(clause.substr(5));
                        if (modeStr == "TwoWay") mode = BindingMode::TwoWay;
                        else if (modeStr == "OneTime") mode = BindingMode::OneTime;
                    } else if (clause.rfind("FallbackValue=", 0) == 0) {
                        fallbackValue = Trim(clause.substr(14));
                    } else if (firstClause && clause.find('=') == std::string_view::npos) {
                        pathPart = clause;
                    }
                    firstClause = false;
                    if (comma == std::string_view::npos) break;
                    clausePos = comma + 1;
                }

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

            const UnifiedValue litVal = ParseLiteralPropertyValue(propId, trimmedVal);
            if (propId == PropId_PathData) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    ensurePathBakedIntoPackage(sv->view());
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
            scopeStack.push_back(std::move(visScope));
        }
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
    } else {
        AxbTemplateRegistry::Instance().RegisterResources(pkg);
    }
    result.succeeded = true;
    return result;
}

namespace {

std::string FormatFloatLiteral(float v) {
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6ff", v);
    return std::string(buf);
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
    if (const auto* f4 = std::get_if<Float4Value>(&val)) {
        return "aero::UnifiedValue{aero::Float4Value{" +
               FormatFloatLiteral(f4->x) + ", " +
               FormatFloatLiteral(f4->y) + ", " +
               FormatFloatLiteral(f4->z) + ", " +
               FormatFloatLiteral(f4->w) + "}}";
    }
    if (const auto* s = std::get_if<StringValue>(&val)) {
        return "aero::value::FromString(\"" + rs_aot::escapeCppString(s->str()) + "\")";
    }
    return "aero::UnifiedValue{}";
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

    // 2. Emit C++17 Source (.cpp) - unrolled batch creation, static properties, MTSDF registration & pre-resolved bindings
    std::string src;
    src += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
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

    src += "    aero::NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;\n";
    src += "    aero::ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;\n";
    src += "    const aero::ObjectRef effectiveContext = dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();\n";
    src += "    const aero::ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid() ? rootControl->BoundScriptWrapper() : effectiveContext;\n\n";

    std::vector<std::string> elemVarStack;
    std::size_t nextElemId = 0;
    bool rootVisited = false;

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                const std::string varName = "elem_" + std::to_string(nextElemId++);

                if (!rootVisited) {
                    rootVisited = true;
                    src += "    aero::UIElement* " + varName + " = rootControl;\n";
                    if (package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        src += "    if (domains && registry && wrapperObj.valid()) {\n";
                        src += "        const auto rootH = rootControl->ToNativeHandle(*registry);\n";
                        src += "        (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                               std::to_string(package.rootHandleFieldIndex) + "u, rootH, nullptr);\n";
                        src += "    }\n";
                    }
                } else {
                    const std::string parentVar = elemVarStack.empty() ? "rootControl" : elemVarStack.back();
                    const std::string escapedType = rs_aot::escapeCppString(std::string(typeName));
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

                if (const auto probe = CreateElementByTypeName(typeName);
                    probe && probe->IsA<ItemsControl>()) {
                    src += "    if (auto* ic = " + varName + " ? " + varName + "->DynamicCast<aero::ItemsControl>() : nullptr) {\n";
                    src += "        ic->SetDomains(domains);\n";
                    src += "    }\n";
                }

                if (!elemName.empty()) {
                    src += "    if (" + varName + ") {\n";
                    src += "        " + varName + "->SetName(\"" + rs_aot::escapeCppString(std::string(elemName)) + "\");\n";
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
                    src += "    if (" + curVar + " && bindingEngine && effectiveContext.valid()) {\n";
                    src += "        aero::AxbCompiledBindingChain chain{};\n";
                    src += "        chain.rawPath = \"" + rs_aot::escapeCppString(chain.rawPath) + "\";\n";
                    src += "        chain.targetPropertyId = " + std::to_string(chain.targetPropertyId) + "u;\n";
                    src += "        chain.mode = static_cast<aero::BindingMode>(" +
                           std::to_string(static_cast<unsigned>(chain.mode)) + "u);\n";
                    if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                        src += "        chain.fallbackValue = " + EmitUnifiedValueLiteralCpp(chain.fallbackValue) + ";\n";
                    }
                    for (const auto& hop : chain.hops) {
                        src += "        {\n";
                        src += "            aero::AxbCompiledHop hop{};\n";
                        src += "            hop.memberName = \"" + rs_aot::escapeCppString(hop.memberName) + "\";\n";
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
                           rs_aot::escapeCppString(std::string(fieldName)) + "\", 0u, aero::IntrusivePtr<aero::UIElement>(" +
                           curVar + ", true), " + std::to_string(inst.propId) + "u);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + " && wrapperObj.valid() && " +
                           std::to_string(inst.symbolId) + "ULL != 0ULL) {\n";
                    src += "        if (auto* btn = " + curVar + "->DynamicCast<aero::Button>()) {\n";
                    src += "            if (eventInterpreter) {\n";
                    src += "                btn->BindScriptClickHandler(eventInterpreter, wrapperObj, " +
                           std::to_string(inst.symbolId) + "ULL);\n";
                    src += "            } else if (eventAotContext) {\n";
                    src += "                btn->BindAotClickHandler(eventAotContext, wrapperObj, " +
                           std::to_string(inst.symbolId) + "ULL);\n";
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
                    const std::string escapedKey = rs_aot::escapeCppString(std::string(package.GetString(inst.strIdx0)));
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

    src += "    return " + std::string(rootVisited ? "true" : "false") + ";\n";
    src += "}\n\n";
    src += "} // namespace " + out.cppNamespace + "\n";
    out.source = std::move(src);
    return out;
}

} // namespace aero
