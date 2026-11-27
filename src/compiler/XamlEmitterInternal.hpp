#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/MarkupExtensionParser.hpp"
#include "aero/compiler/XamlSchemaDef.hpp"
#include "aero/compiler/XamlToRsEmitter.hpp"
#include "aero/compiler/XmlPullLexer.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/drawing/Geometry.hpp"

namespace aero::xaml_detail {

[[nodiscard]] inline bool IsDirectUIProperty(std::string_view prop) {
    return IsXamlDirectUIProperty(prop);
}

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

[[nodiscard]] bool IsDesignOrMarkupCompatPrefix(std::string_view prefix, std::string_view nsUri);
[[nodiscard]] bool ShouldIgnoreXmlElement(const XamlNode& node);
[[nodiscard]] bool BuildXamlDom(
    std::string_view xmlContent,
    std::unique_ptr<XamlNode>& outRoot,
    std::vector<std::string>& outDiagnostics);
[[nodiscard]] std::string EscapeRsString(std::string_view s);
[[nodiscard]] std::string FormatFloatLiteral(float value);
[[nodiscard]] bool TryParseFloatList(std::string_view raw, std::vector<float>& outValues);
[[nodiscard]] std::string FormatThickness4Args(std::string_view raw);
[[nodiscard]] std::string FormatCornerRadius4Args(std::string_view raw);
[[nodiscard]] std::string FormatOrigin2Args(std::string_view raw);
[[nodiscard]] std::string FormatThicknessExpr(std::string_view raw);
[[nodiscard]] std::string FormatCornerRadiusExpr(std::string_view raw);
[[nodiscard]] std::string ConvertPathGeometryToSvgData(const XamlNode& pgNode);
[[nodiscard]] std::pair<std::string, std::string> ParseGridLength(std::string_view raw);
[[nodiscard]] float ParseDurationSeconds(std::string_view raw);
[[nodiscard]] bool ParseTimelineAutoReverse(const XamlNode& node);
[[nodiscard]] int ParseTimelineRepeatCount(const XamlNode& node);
[[nodiscard]] std::string NormalizeTargetPropertyPath(std::string_view raw);
[[nodiscard]] int MapEasingKind(std::string_view name);
[[nodiscard]] int MapEasingMode(std::string_view mode);
[[nodiscard]] int MapBindingMode(std::string_view mode);
[[nodiscard]] std::string StripTargetTypeName(std::string_view raw);
[[nodiscard]] std::string NormalizeResourceKey(std::string_view raw);
[[nodiscard]] std::string ResolveResourceKeyFromExtension(const MarkupExtensionNode& ext);
[[nodiscard]] std::string NormalizePackSourcePath(std::string_view raw);
[[nodiscard]] std::string SanitizeIdentifier(std::string_view raw, std::string_view fallback);
[[nodiscard]] std::string FormatUintHexLiteral(std::uint32_t rgba);
[[nodiscard]] const char* LookupEnumType(std::string_view propName);
[[nodiscard]] bool IsThicknessProperty(std::string_view prop);
[[nodiscard]] bool IsBrushProperty(std::string_view prop);
[[nodiscard]] bool IsEventAttribute(std::string_view prop);
[[nodiscard]] bool IsContentControlType(std::string_view typeName);
[[nodiscard]] bool IsItemsControlType(std::string_view typeName);
[[nodiscard]] bool IsPanelType(std::string_view typeName);
[[nodiscard]] bool IsShapeType(std::string_view typeName);
[[nodiscard]] bool IsControlType(std::string_view typeName);
[[nodiscard]] bool IsNonUIElementType(std::string_view typeName);
[[nodiscard]] bool IsKnownUIElementType(std::string_view typeName);
[[nodiscard]] bool IsDirectUIPropertyFor(std::string_view targetType, std::string_view prop);
[[nodiscard]] bool IsFloatPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});
[[nodiscard]] bool IsIntPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});
[[nodiscard]] bool IsBoolPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});
[[nodiscard]] std::uint64_t FastHashBytes64(const void* data, std::size_t size) noexcept;
[[nodiscard]] std::string ReadFileTextUtf8(
    const std::filesystem::path& path,
    std::uint64_t* outRawHash = nullptr);
[[nodiscard]] std::string ExtractClrNamespace(std::string_view uri);
[[nodiscard]] bool IsIgnoredExternalClrNamespace(std::string_view ns);
[[nodiscard]] bool IsPrunableExternalResourceTag(std::string_view localName) noexcept;
[[nodiscard]] bool CanEmitLazyToolTipNode(const XamlNode& node, bool isRoot) noexcept;
void ExpandControlHierarchy(std::unordered_set<std::string>& usedTypes);
void ExtractKeysFromExtension(
    const MarkupExtensionNode& ext,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys);
void ScanXamlSubtreeForSeeds(
    const XamlNode& node,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys,
    bool skipDefinitionOwnKey,
    bool skipExternalSourceDicts);
void ScanHintTextForSeeds(
    std::string_view text,
    std::unordered_set<std::string>& usedTypes,
    std::unordered_set<std::string>& liveKeys);

class XamlEmitter final {
public:
explicit XamlEmitter(const XamlEmitOptions& opts) : opts_(opts) {}
    [[nodiscard]] XamlEmitResult Run(std::string_view xamlContent);

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

    void AddError(std::string msg);
    void AddWarning(std::string_view msg);
    void RecordExternalDependency(std::string_view normPath, std::uint64_t contentHash);
    [[nodiscard]] const ParsedExternalDoc* ResolveAndLoadExternalDoc(
    std::string_view requestedSource,
    std::string_view currentXamlPath,
    bool reportErrors);
    [[nodiscard]] static bool IsThemeResourceDictPath(std::string_view path) noexcept;
    [[nodiscard]] static bool HasDynamicXamlLoadingHint(
    const std::vector<std::string>& hints) noexcept;
    void CollectExternalDictNodes(
    const XamlNode& dictNode,
    std::string_view currentXamlPath);
    void CollectExternalDictInclude(
    std::string_view requestedSource,
    std::string_view currentXamlPath);
    void WalkTreeForExternalDictRefs(const XamlNode& node, std::string_view currentXamlPath);
    void PrecomputeLiveExternalStyleNodes(
    const XamlNode& primaryRoot,
    std::string_view currentXamlPath);
    void MaybeImportClrNamespace(std::string_view ns);
    void MaybeImportPrefixedType(std::string_view raw);
    [[nodiscard]] std::string FreshVar(std::string_view prefix);
    [[nodiscard]] static std::string MakeGeneratedRsPath(std::string_view xamlPath);
    [[nodiscard]] static std::string DeriveModuleNameFromPath(std::string_view xamlPath);
    static void Indent(std::ostringstream& out, int depth);
    void RegisterNamedField(const std::string& name, const std::string& typeName);
    [[nodiscard]] std::string ResolveElementType(const XamlNode& node);
    [[nodiscard]] std::string ResolveColorHexOrString(std::string_view raw) const;
    [[nodiscard]] std::string EmitExternalDictionaryInclude(
    std::string_view requestedSource,
    std::string_view currentXamlPath,
    std::string_view targetDictVar,
    std::ostringstream& out,
    int indent);
    void EmitResourceDictionaryEntries(
    const XamlNode& dictNode,
    std::string_view currentXamlPath,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitStandaloneResourceDictionary(
    const XamlNode& root,
    std::string_view currentXamlPath,
    std::ostringstream& out);
    void EmitApplicationRoot(
    const XamlNode& root,
    std::string_view currentXamlPath,
    std::ostringstream& out);
    void EmitPartialViewRoot(
    const XamlNode& root,
    std::string_view xDataType,
    std::string_view currentXamlPath,
    std::ostringstream& out);
    void EmitStandaloneViewFactory(
    const XamlNode& root,
    std::string_view xDataType,
    std::string_view currentXamlPath,
    std::ostringstream& out);
    bool EmitResourcesPropertyElement(
    const XamlNode& ownerNode,
    std::string_view ownerType,
    std::string_view ownerVar,
    std::string_view currentXamlPath,
    std::ostringstream& out,
    int indent);
    void EmitSingleDirectResource(
    const XamlNode& child,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitGradientBrushCommonProps(
    const XamlNode& brushNode,
    std::string_view bVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] const XamlNode* FindBrushShaderElement(const XamlNode& brushNode) const;
    void EmitImageBrushPlacementAttrs(
    const XamlNode& brushNode,
    std::string_view bVar,
    std::ostringstream& out,
    int indent) const;
    [[nodiscard]] std::string EmitShaderFallbackBrush(
    const XamlNode& brushNode,
    const XamlNode& shaderNode,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitBrushObject(
    const XamlNode& brushNode,
    std::string_view /*targetProp*/,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitGradientStops(
    const XamlNode& brushNode,
    std::string_view brushVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitTransformObject(
    const XamlNode& node,
    std::ostringstream& out,
    int indent,
    std::string_view hostUiVar = {},
    std::string_view xDataType = {},
    std::string_view dictVar = {},
    std::string_view transformPropPrefix = "RenderTransform");
    [[nodiscard]] std::string EmitEffectObject(
    const XamlNode& node,
    std::string_view hostUiVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitStyle(
    const XamlNode& styleNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    static std::string NormalizeSetterPropertyName(std::string_view rawProp);
    [[nodiscard]] static bool IsAttachedBindableProperty(std::string_view propKey);
    [[nodiscard]] static std::string NormalizeBindingPath(std::string_view rawPath);
    [[nodiscard]] std::string BoxSetterValueExpr(
    std::string_view propName,
    std::string_view typedExpr,
    std::string_view targetType = {}) const;
    void EmitSetterInto(
    const XamlNode& setterNode,
    std::string_view targetObjVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::string_view styleTargetType = {});
    void EmitTriggerEnterExitActions(
    const XamlNode& trigNode,
    std::string_view trigTypeName,
    std::string_view trigVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string FormatTriggerValueBoxExpr(
    std::string_view propOrPath,
    std::string_view rawVal,
    std::string_view dictVar) const;
    void EmitTriggerCollectionInto(
    const XamlNode& trigProp,
    std::string_view ownerObjVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::string_view styleTargetType = {});
    [[nodiscard]] std::string EmitControlTemplate(
    const XamlNode& tplNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitItemsPanelTemplate(
    const XamlNode& iptNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitDataTemplate(
    const XamlNode& dtNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] static bool IsComplexGeometryPointPath(std::string_view rawProp);
    [[nodiscard]] std::string FormatDiscreteKeyFrameBoxExpr(
    const XamlNode& kf,
    std::string_view targetProp,
    std::string_view dictVar,
    bool isBooleanTimeline,
    std::ostringstream& out,
    int indent);
    static void ExtractEasingNodeParams(const XamlNode& easeNode, float& outP0, float& outP1, int& outMask);
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
    int easeMask = 0);
    [[nodiscard]] static std::string ExtractStaticResourceElementKey(const XamlNode& node);
    void EmitStoryboardTimelines(
    const XamlNode& sbNode,
    std::string_view sbVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::unordered_set<std::string>& visitedSbKeys,
    bool sbRepeatForever = false);
    [[nodiscard]] std::string EmitStoryboard(
    const XamlNode& sbNode,
    std::ostringstream& out,
    int indent,
    std::string_view dictVar = {});
    [[nodiscard]] std::string LookupResourceExpr(
    std::string_view key,
    std::string_view dictVar) const;
    [[nodiscard]] std::string FormatGenericScalarLiteral(std::string_view val) const;
    [[nodiscard]] std::string FormatTypedValueLiteral(
    std::string_view propName,
    std::string_view rawVal,
    std::string_view dictVar,
    std::string_view targetType = {}) const;
    [[nodiscard]] std::string EmitConverterNode(
    const XamlNode& convNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void TryParseBindingNode(
    const XamlNode& bindNode,
    ParsedBindingSpec& outSpec,
    std::string& outConvVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitBindingObjectFromSpec(
    const ParsedBindingSpec& subSpec,
    std::string_view propName,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::string_view subConv = {},
    std::string_view sourceResKey = {});
    [[nodiscard]] std::string EmitDynamicBindingObject(
    const XamlNode& bindNode,
    std::string_view propName,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitMultiBindingObject(
    const XamlNode& mbNode,
    std::string_view propName,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitMultiBindingNode(
    std::string_view targetVar,
    std::string_view targetType,
    std::string_view propName,
    const XamlNode& mbNode,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
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
    bool isUiElementTarget = false);
    void EmitSiblingLanguageDictionaries(std::ostringstream& out, int indent);
    void EmitBindingSpec(
    std::string_view targetVar,
    std::string_view targetType,
    std::string_view propName,
    const ParsedBindingSpec& spec,
    std::string_view xDataType,
    std::ostringstream& out,
    int indent,
    std::string_view explicitSourceVar,
    std::string_view inlineConvExpr = {});
    void EmitSingleGridTrackDef(
    std::string_view targetVar,
    std::string_view method,
    std::string_view rawLen,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::string_view defName = {});
    [[nodiscard]] std::string EmitConstructedElement(
    const XamlNode& node,
    std::string_view xDataType,
    std::string_view dictVar,
    std::string_view currentXamlPath,
    std::ostringstream& out,
    int indent,
    std::string_view explicitBindSourceVar = {});
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
    std::string_view explicitBindSourceVar = {});
    [[nodiscard]] std::string EmitRunElement(
    const XamlNode& runNode,
    std::string_view xDataType,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitGridView(
    const XamlNode& gvNode,
    std::string_view listViewVar,
    std::string_view /*xDataType*/,
    std::ostringstream& out,
    int indent);
    void EmitRiveInput(
    const XamlNode& node,
    std::string_view riveVar,
    std::ostringstream& out,
    int indent);
    void EmitInputBindings(
    const XamlNode& ibNode,
    std::string_view ownerVar,
    std::ostringstream& out,
    int indent);
    void EmitProjection3D(
    const XamlNode& node,
    std::string_view targetVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitCompositeTransform3DObject(
    const XamlNode& node,
    std::ostringstream& out,
    int indent,
    std::string_view hostTargetVar = {});
    void EmitTransform3D(
    const XamlNode& node,
    std::string_view targetVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitTriggerActionNode(
    const XamlNode& act,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] std::string EmitBehaviorTriggerObject(
    const XamlNode& trig,
    std::string_view /*scopeVar*/,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitInteractionTriggers(
    const XamlNode& trigNode,
    std::string_view targetVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitInteractionBehaviors(
    const XamlNode& behNode,
    std::string_view targetVar,
    std::string_view /*dictVar*/,
    std::ostringstream& out,
    int indent);
    void EmitElementTriggers(
    const XamlNode& triggersNode,
    std::string_view targetVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    [[nodiscard]] bool StoryboardHasAnimatedTimelineImpl(
    const XamlNode& sbNode,
    std::unordered_set<std::string>& visitedSbKeys) const;
    [[nodiscard]] bool StoryboardHasAnimatedTimeline(const XamlNode& sbNode) const;
    void EmitVisualStateStoryboardSetters(
    const XamlNode& sbNode,
    std::string_view vsVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
    void EmitVisualStateStoryboardSettersImpl(
    const XamlNode& sbNode,
    std::string_view vsVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent,
    std::unordered_set<std::string>& visitedSbKeys);
    void EmitVisualStateGroups(
    const XamlNode& vsgPropNode,
    std::string_view targetVar,
    std::string_view dictVar,
    std::ostringstream& out,
    int indent);
};

} // namespace aero::xaml_detail
