#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

inline constexpr std::uint32_t kAxb4Magic = 0x34425841u; // "AXB4"
inline constexpr std::uint32_t kAxb4Version = 0x00020000u;
inline constexpr std::uint32_t kAxb4MotionSectionMagic = 0x36544F4Du;     // "MOT6"
inline constexpr std::uint32_t kAxb4MergedDictSectionMagic = 0x3231524Du; // "MR12"
inline constexpr std::uint32_t kAxb4ControlTemplateSectionMagic = 0x31505443u; // "CTP1"
inline constexpr std::uint32_t kAxb4P14SectionMagic = 0x34315041u;    // "AP14"

enum class BindingMode : std::uint8_t {
    OneWay = 0,
    TwoWay = 1,
    OneTime = 2,
};

enum class ValueConverterKind : std::uint8_t {
    None = 0,
    BooleanToVisibility = 1,
    InverseBooleanToVisibility = 2,
    InverseBoolean = 3,
    NullToCollapsed = 4,
    NullToBoolean = 5,
    MathExpression = 6,
    CustomScriptOrHost = 7,
};

enum class AxbOpcode : std::uint8_t {
    BeginElement = 1,
    EndElement = 2,
    SetStaticProperty = 3,
    BindProperty = 4,
    BindEcsColumn = 5,
    BindEvent = 6,
    AddRowDefinition = 7,
    AddColumnDefinition = 8,
    SetResourceProperty = 9,
    BindTemplateParent = 10,
    BindMultiProperty = 11,
};

// P1-2: Exclusive AxbInstruction::flags encoding on AxbOpcode::SetResourceProperty (opcode 9)
enum AxbResourceLookupFlags : std::uint8_t {
    AxbResourceFlag_StaticResource = 0x00u,
    AxbResourceFlag_DynamicResource = 0x02u,
    AxbResourceFlag_Loc = 0x04u,
};

// ============================================================================
// P1-3: Multi-Stop LinearGradientBrush & RadialGradientBrush Registry + 1D LUT
// ============================================================================

enum class GradientBrushKind : std::uint8_t {
    Linear = 0,
    Radial = 1,
};

enum class BrushMappingMode : std::uint8_t {
    RelativeToBoundingBox = 0,
    Absolute = 1,
};

enum class GradientSpreadMethod : std::uint8_t {
    Pad = 0,
    Reflect = 1,
    Repeat = 2,
};

struct GradientStop {
    float offset = 0.0f;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct GradientBrushDesc {
    static constexpr std::uint32_t kLutWidth = 256u;

    std::uint32_t brushId = 0u;
    GradientBrushKind kind = GradientBrushKind::Linear;
    BrushMappingMode mappingMode = BrushMappingMode::RelativeToBoundingBox;
    GradientSpreadMethod spreadMethod = GradientSpreadMethod::Pad;
    float opacity = 1.0f;
    // LinearGradientBrush endpoints (default XAML diagonal 0,0 -> 1,1)
    Point startPoint{0.0f, 0.0f};
    Point endPoint{1.0f, 1.0f};
    // RadialGradientBrush ellipse center, focal origin & radii (default 0.5,0.5, rx=0.5, ry=0.5)
    Point center{0.5f, 0.5f};
    Point gradientOrigin{0.5f, 0.5f};
    float radiusX = 0.5f;
    float radiusY = 0.5f;
    std::vector<GradientStop> stops;
    // Pre-baked 256-texel RGBA8 1D ramp for O(1) GPU LUT row upload & CPU bilinear evaluation
    std::array<std::uint8_t, kLutWidth * 4u> bakedRampRgba8{};
};

void BakeGradientBrushRamp(GradientBrushDesc& ioBrush) noexcept;
[[nodiscard]] Color SampleGradientRampAtT(
    const GradientBrushDesc& brush,
    float rawT) noexcept;
[[nodiscard]] Color EvaluateGradientBrushAtNormalizedUv(
    const GradientBrushDesc& brush,
    float normU,
    float normV,
    float quadWidthPx = 1.0f,
    float quadHeightPx = 1.0f) noexcept;
[[nodiscard]] std::string SerializeGradientBrushSpec(const GradientBrushDesc& brush);
[[nodiscard]] bool IsGradientBrushSpec(std::string_view raw) noexcept;
[[nodiscard]] bool TryDeserializeGradientBrushSpec(
    std::string_view raw,
    GradientBrushDesc& outBrush);

class GradientBrushRegistry {
public:
    static constexpr std::uint32_t kMaxBrushId = 4095u;

    static GradientBrushRegistry& Instance() noexcept;

    GradientBrushRegistry() noexcept = default;
    ~GradientBrushRegistry() = default;

    GradientBrushRegistry(const GradientBrushRegistry&) = delete;
    GradientBrushRegistry& operator=(const GradientBrushRegistry&) = delete;

    std::uint32_t RegisterBrush(GradientBrushDesc desc);
    std::uint32_t RegisterOrResolveSpec(std::string_view serializedSpec);
    [[nodiscard]] const GradientBrushDesc* Find(std::uint32_t brushId) const noexcept;
    [[nodiscard]] std::size_t BrushCount() const noexcept;
    [[nodiscard]] std::uint32_t Revision() const noexcept;
    void Clear() noexcept;

private:
    mutable std::mutex mutex_;
    std::vector<GradientBrushDesc> brushes_;
    std::unordered_map<std::string, std::uint32_t> specToId_;
    std::uint32_t revision_ = 1u;
};

[[nodiscard]] std::string ConvertPointsToSvgPath(
    std::string_view rawPoints,
    bool closePath);

// ============================================================================
// Phase 5C & 5D: Easing Curves, Storyboards, Data/Event Triggers & Templates
// ============================================================================

enum class EasingFunctionKind : std::uint8_t {
    Linear = 0,
    Quadratic = 1,
    Cubic = 2,
    Quartic = 3,
    Quintic = 4,
    Sine = 5,
    Circle = 6,
    Exponential = 7,
    Back = 8,
    Bounce = 9,
    Elastic = 10,
    Spline = 11,
};

enum class EasingMode : std::uint8_t {
    EaseOut = 0,
    EaseIn = 1,
    EaseInOut = 2,
};

enum class FillBehavior : std::uint8_t {
    HoldEnd = 0,
    Stop = 1,
};

struct AxbKeyFrame {
    float timeSeconds = 0.0f;
    UnifiedValue value{};
    EasingFunctionKind easingKind = EasingFunctionKind::Linear;
    EasingMode easingMode = EasingMode::EaseOut;
    bool isDiscrete = false;
    bool hasKeySpline = false;
    float splineX1 = 0.0f;
    float splineY1 = 0.0f;
    float splineX2 = 1.0f;
    float splineY2 = 1.0f;
    std::string bindingElementName;
    std::uint16_t bindingPropId = 0xFFFFu;
};

struct AxbAnimationTimeline {
    std::string targetName;
    std::uint16_t targetPropertyId = 0;
    float beginTimeSeconds = 0.0f;
    float durationSeconds = 0.25f;
    float speedRatio = 1.0f;
    float accelerationRatio = 0.0f;
    float decelerationRatio = 0.0f;
    bool hasFrom = false;
    UnifiedValue fromValue{};
    bool hasTo = false;
    UnifiedValue toValue{};
    bool isBy = false;
    EasingFunctionKind easingKind = EasingFunctionKind::Linear;
    EasingMode easingMode = EasingMode::EaseOut;
    FillBehavior fillBehavior = FillBehavior::HoldEnd;
    bool autoReverse = false;
    std::int32_t repeatCount = 1; // <= 0 denotes Forever loop
    std::vector<AxbKeyFrame> keyFrames;
};

struct AxbStoryboard {
    std::string key;
    std::vector<AxbAnimationTimeline> timelines;
};

struct AxbCompiledBindingChain;
struct AxbCompiledMultiBinding;

struct AxbPropertySetter {
    std::string targetName;
    std::uint16_t propId = 0;
    UnifiedValue value{};
    bool hasBinding = false;
    std::shared_ptr<AxbCompiledBindingChain> binding{};
};

struct AxbCompiledHop {
    std::string memberName;
    SymbolId declaringTypeId = 0;
    std::uint32_t fieldIndex = PropertySlotHandle::InvalidIndex;
    std::uint8_t observableBitIndex = 0xFFu;
    SymbolId getterSymbolId = 0;
    SymbolId setterSymbolId = 0;
    bool isAutoProperty = false;
    bool isObservable = false;
    bool isReadOnly = false;
    bool isSimState = false;
    bool isSimCommand = false;
    std::int32_t collectionIndex = -1;
    SymbolId targetTypeId = 0;
    bool targetIsEnum = false;

    [[nodiscard]] PropertySlotHandle ToSlotHandle() const noexcept {
        PropertySlotHandle slot;
        slot.typeId = declaringTypeId;
        slot.backingFieldIndex = fieldIndex;
        slot.getterSymbolId = getterSymbolId;
        slot.setterSymbolId = setterSymbolId;
        slot.observableBitIndex = observableBitIndex;
        slot.isAutoProperty = isAutoProperty;
        slot.isObservable = isObservable;
        slot.isReadOnly = isReadOnly;
        slot.isSimState = isSimState;
        slot.isSimCommand = isSimCommand;
        return slot;
    }
};

struct AxbCompiledBindingChain {
    std::string rawPath;
    std::uint16_t targetPropertyId = 0;
    BindingMode mode = BindingMode::OneWay;
    UnifiedValue fallbackValue{};
    UnifiedValue targetNullValue{};
    std::vector<AxbCompiledHop> hops;
    // Phase 5D: ElementName / RelativeSource & zero-allocation built-in value converter
    std::string elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    ValueConverterKind converter = ValueConverterKind::None;
    // P1-4: StringFormat, MathConverter expression, ConverterParameter & custom converter key/symbols
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    std::shared_ptr<AxbCompiledMultiBinding> nestedMultiBinding{};
};

struct AxbCompiledMultiBinding {
    std::uint16_t targetPropertyId = 0;
    BindingMode mode = BindingMode::OneWay;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    UnifiedValue fallbackValue{};
    SymbolId converterSymbolId = 0;
    SymbolId convertBackSymbolId = 0;
    std::vector<AxbCompiledBindingChain> sources;
};

enum class TriggerConditionSource : std::uint8_t {
    SelfProperty = 0,
    DataContextBinding = 1,
    ElementNameProperty = 2,
    AncestorProperty = 3,
};

enum class TriggerComparisonOp : std::uint8_t {
    Equal = 0,
    NotEqual = 1,
    LessThan = 2,
    LessThanOrEqual = 3,
    GreaterThan = 4,
    GreaterThanOrEqual = 5,
};

enum class ControlStoryboardOption : std::uint8_t {
    Play = 0,
    Stop = 1,
    TogglePlayPause = 2,
};

enum class InteractionActionKind : std::uint8_t {
    ChangeProperty = 0,
    ControlStoryboard = 1,
    InvokeCommand = 2,
    PlaySound = 3,
    RemoveElement = 4,
};

struct AxbInteractionAction {
    InteractionActionKind kind = InteractionActionKind::ChangeProperty;
    std::string actionName;
    bool isEnabled = true;
    // ChangePropertyAction (empty targetName targets the host element itself)
    std::string targetName;
    std::uint16_t propId = 0;
    std::string propertyName;
    UnifiedValue value{};
    bool isNullValue = false;
    bool hasValueBinding = false;
    std::shared_ptr<AxbCompiledBindingChain> valueBinding{};
    std::shared_ptr<AxbCompiledMultiBinding> valueMultiBinding{};
    // ControlStoryboardAction
    std::string storyboardKey;
    AxbStoryboard inlineStoryboard{};
    ControlStoryboardOption storyboardOption = ControlStoryboardOption::Play;
    // InvokeCommandAction
    std::string commandPath;
    std::vector<AxbCompiledHop> commandHops;
    SymbolId commandMethodSymbolId = 0;
    bool commandTargetsWrapper = false;
    bool isSimCommand = false;
    bool hasCommandParameter = false;
    UnifiedValue commandParameter{};
    AxbCompiledBindingChain commandParameterBinding{};
    bool hasCommandParameterBinding = false;
    // PlaySoundAction
    std::string soundSource;
    float volume = 1.0f;
    // RemoveElementAction TargetObject binding
    std::shared_ptr<AxbCompiledBindingChain> targetObjectBinding{};
};

struct AxbTriggerCondition {
    AxbTriggerCondition() = default;
    AxbTriggerCondition(std::uint16_t id, UnifiedValue expected)
        : propId(id), expectedValue(std::move(expected)) {}

    std::uint16_t propId = 0;
    UnifiedValue expectedValue{};
    TriggerConditionSource sourceKind = TriggerConditionSource::SelfProperty;
    ValueConverterKind converter = ValueConverterKind::None;
    bool expectNull = false;
    std::string sourceName;
    std::string bindingPath;
    std::vector<AxbCompiledHop> compiledHops;
    TriggerComparisonOp comparison = TriggerComparisonOp::Equal;
    std::string converterKey;
    std::string converterParameter;
    SymbolId converterSymbolId = 0;
};

struct AxbStyleTrigger {
    std::vector<AxbTriggerCondition> conditions;
    std::vector<AxbPropertySetter> setters;
    std::vector<AxbStoryboard> enterStoryboards;
    std::vector<AxbStoryboard> exitStoryboards;
    bool overrideLocalValues = false;
    bool isInteractionTrigger = false;
    std::vector<AxbInteractionAction> enterActions;
    std::vector<AxbInteractionAction> exitActions;
};

enum class RoutedTriggerEvent : std::uint8_t {
    Loaded = 0,
    Click = 1,
    MouseEnter = 2,
    MouseLeave = 3,
    Checked = 4,
    Unchecked = 5,
    SelectionChanged = 6,
    ValueChanged = 7,
    StoryboardCompleted = 8,
    PropertyChanged = 9,
    Indeterminate = 10,
};

struct AxbEventTrigger {
    RoutedTriggerEvent eventKind = RoutedTriggerEvent::Loaded;
    std::string sourceName;
    std::vector<AxbTriggerCondition> conditions;
    std::vector<AxbStoryboard> storyboards;
    std::vector<AxbInteractionAction> actions;
};

struct AxbVisualState {
    std::string name;
    std::vector<AxbPropertySetter> setters;
    AxbStoryboard storyboard{};
};

struct AxbVisualStateGroup {
    std::string name;
    float defaultTransitionDuration = 0.0f;
    EasingFunctionKind defaultEasingKind = EasingFunctionKind::Linear;
    EasingMode defaultEasingMode = EasingMode::EaseOut;
    std::vector<AxbVisualState> states;
};

struct AxbPackage;

struct AxbControlTemplate {
    std::string key;
    std::string targetTypeName;
    std::shared_ptr<AxbPackage> templatePackage;
    std::vector<AxbStyleTrigger> triggers;
    std::vector<AxbEventTrigger> eventTriggers;
    std::vector<AxbVisualStateGroup> visualStateGroups;
};

struct AxbStyleDefinition {
    std::string key;
    std::string targetTypeName;
    std::string basedOnKey;
    std::string controlTemplateKey;
    std::shared_ptr<AxbControlTemplate> inlineControlTemplate;
    std::vector<AxbPropertySetter> setters;
    std::vector<AxbStyleTrigger> triggers;
    std::vector<AxbEventTrigger> eventTriggers;
    std::vector<AxbVisualStateGroup> visualStateGroups;
};

struct AxbNamedControlSlot {
    std::string name;
    std::string typeName;
    SymbolId typeSymbolId = 0;
    std::uint32_t fieldIndexInClass = PropertySlotHandle::InvalidIndex;
};

struct AxbEventBindingRecord {
    std::string controlName;
    std::string eventName;
    std::string handlerMethodName;
    SymbolId handlerSymbolId = 0;
    bool targetIsWrapper = false;
    std::vector<AxbCompiledHop> prefixHops;
};

struct AxbInstruction {
    AxbOpcode opcode = AxbOpcode::BeginElement;
    std::uint16_t propId = 0;
    std::uint8_t flags = 0;
    std::uint32_t strIdx0 = 0;
    std::uint32_t strIdx1 = 0;
    SymbolId symbolId = 0;
    UnifiedValue literalValue{};
};

// Offline / on-demand 4-channel MTSDF (Multi-channel + True Signed Distance Field)
// atlas entry for SVG <Path Data="M..."/> & <Geometry> vector icons.
struct AxbMtsdfPathEntry {
    std::uint64_t pathHash = 0;
    float boundsX = 0.0f;
    float boundsY = 0.0f;
    float boundsW = 32.0f;
    float boundsH = 32.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    std::uint16_t atlasWidth = 256;
    std::uint16_t atlasHeight = 256;
    std::uint16_t cellWidth = 32;
    std::uint16_t cellHeight = 32;
    float pxRange = 4.0f;
    // 4-channel RGBA8 MTSDF cell (R,G,B = corner-preserving directional SDF, A = true Euclidean SDF)
    std::vector<std::uint8_t> mtsdfRgbaPixels;
    // Cached per-row non-zero RGB bounds (transient runtime cache; not serialized into .axb4)
    std::vector<std::int16_t> rowMinX;
    std::vector<std::int16_t> rowMaxX;

    void ComputeRowBounds() noexcept {
        if (cellWidth == 0u || cellHeight == 0u ||
            mtsdfRgbaPixels.size() < static_cast<std::size_t>(cellWidth) * cellHeight * 4u) {
            rowMinX.clear();
            rowMaxX.clear();
            return;
        }
        const int cw = static_cast<int>(cellWidth);
        const int ch = static_cast<int>(cellHeight);
        rowMinX.resize(static_cast<std::size_t>(ch));
        rowMaxX.resize(static_cast<std::size_t>(ch));
        const std::uint8_t* mBase = mtsdfRgbaPixels.data();
        for (int ry = 0; ry < ch; ++ry) {
            const std::uint8_t* rPtr = mBase + static_cast<std::size_t>(ry) * static_cast<std::size_t>(cw) * 4u;
            std::int16_t rMin = static_cast<std::int16_t>(cw);
            std::int16_t rMax = -1;
            for (int rx = 0; rx < cw; ++rx) {
                const std::uint8_t* p = rPtr + static_cast<std::size_t>(rx) * 4u;
                if ((p[0] | p[1] | p[2]) != 0u) {
                    if (rx < rMin) rMin = static_cast<std::int16_t>(rx);
                    rMax = static_cast<std::int16_t>(rx);
                }
            }
            rowMinX[static_cast<std::size_t>(ry)] = rMin;
            rowMaxX[static_cast<std::size_t>(ry)] = rMax;
        }
    }
};

struct AxbDataTemplate {
    std::string key;
    std::string dataTypeName;
    std::string itemsSourcePath;
    std::string childItemTemplateKey;
    std::shared_ptr<AxbPackage> templatePackage;
};

// ============================================================================
// Phase 6: Multi-Frontend Motion Clips & Multi-Layer State Machine ("MOT6")
// ============================================================================

enum class CurveInterpMode : std::uint8_t {
    HoldStep = 0,       // Discrete step (Bool / Int / Enum / cut)
    Linear = 1,         // Linear interpolation
    AnalyticalEase = 2, // Reuses 11x3 EasingFunctionKind + EasingMode
    CubicBezier = 3,    // Rive / Lottie normalized 1D time Bezier tangents (x1, y1, x2, y2)
};

// 28-byte compact flat keyframe record stored contiguously in AxbPackage::motionKeyframes
struct AxbFlatKeyframe {
    float timeSec = 0.0f;
    Float4Value value{};               // Scalar uses value.x; Color/Thickness/CornerRadius uses value.x..w
    std::uint8_t interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
    std::uint8_t easeKindOrCtrlX1 = 0; // AnalyticalEase: EasingFunctionKind; Bezier: quantized UNORM8 x1 (0..255 -> 0..1)
    std::uint8_t easeModeOrCtrlX2 = 0; // AnalyticalEase: EasingMode; Bezier: quantized UNORM8 x2 (0..255 -> 0..1)
    std::uint8_t flags = 0;
    std::int16_t ctrlY1Fixed8 = 0;     // Bezier y1 in Q7.8 fixed-point (supports [-128.0, +127.996] overshoot tangents)
    std::int16_t ctrlY2Fixed8 = 256;   // Bezier y2 in Q7.8 fixed-point (1.0f = 256)
};
static_assert(sizeof(AxbFlatKeyframe) == 28, "Compact flat keyframe record");

// Deterministic node track descriptor: localElementIdx matches pre-order BeginElement index (0 = root control)
struct AxbMotionTrackDesc {
    std::uint16_t localElementIdx = 0;
    std::uint16_t targetPropId = 0;
    std::uint8_t channelCount = 1;     // 1 = Float, 4 = Float4, 0 = Discrete Int/Bool
    std::uint8_t quantizeInlineW = 1;  // 1 = continuous Color/CornerRadius interpolation uses MakeFloat4InlineFast
    std::uint16_t reserved = 0;
    std::uint32_t firstKeyframeIdx = 0;
    std::uint32_t keyframeCount = 0;
};

// Timeline frame event marker (audio cue, particle hook, or [SimCommand] deterministic signal)
struct AxbTimelineEventMarker {
    float timeSec = 0.0f;
    std::uint32_t eventNameStrIdx = 0;
    SymbolId handlerSymbolId = 0;
    float floatPayload = 0.0f;
    std::int32_t intPayload = 0;
};

struct AxbMotionClipDesc {
    std::uint32_t nameStrIdx = 0;
    float durationSec = 0.0f;
    bool isLooping = false;
    bool autoReverse = false;
    std::uint32_t firstTrackIdx = 0;
    std::uint32_t trackCount = 0;
    std::uint32_t firstEventIdx = 0;
    std::uint32_t eventCount = 0;
};

// Non-tree parent transform constraint rule (for Lottie parent chains & Rive bone attachments)
struct AxbTransformConstraintDesc {
    std::uint16_t childLocalElementIdx = 0;
    std::uint16_t parentLocalElementIdx = 0;
};

enum class SmCompareOp : std::uint8_t {
    Equal = 0,
    NotEqual = 1,
    Greater = 2,
    GreaterOrEqual = 3,
    Less = 4,
    LessOrEqual = 5,
    BoolTrue = 6,
    BoolFalse = 7,
    TriggerFired = 8, // Pulse trigger: auto-resets to 0 at end of state machine evaluation frame
};

// Conjunction (AND) condition item for state machine transitions
struct AxbSmCondition {
    std::uint16_t inputSlotIdx = 0; // Points into component instance's ContractInputBank slot
    SmCompareOp op = SmCompareOp::Equal;
    std::uint8_t reserved = 0;
    float threshold = 0.0f;
};

struct AxbSmTransition {
    std::uint16_t fromStateIdx = 0xFFFFu; // 0xFFFFu denotes AnyState
    std::uint16_t toStateIdx = 0;
    float crossfadeDurationSec = 0.15f;
    EasingFunctionKind easeKind = EasingFunctionKind::Cubic;
    EasingMode easeMode = EasingMode::EaseOut;
    bool hasExitTime = false;             // When true, requires normalized state progress >= exitTimeNormalized01
    bool allowSelfInterrupt = false;
    float exitTimeNormalized01 = 1.0f;
    std::uint32_t firstConditionIdx = 0;
    std::uint16_t conditionCount = 0;     // Fires when all conditions are satisfied (AND)
    std::uint16_t priority = 0;
};

enum class SmStateKind : std::uint8_t {
    SingleClip = 0,   // Advances normally by speedMultiplier * dt
    ScrubbedClip = 1, // Normalized timeline progress [0..1] driven directly by progressInputSlot
    Blend1D = 2,      // Continuous weight blend between [primaryClipIdx, secondaryClipIdx] via progressInputSlot
    ExitState = 3,
};

struct AxbSmState {
    std::uint32_t nameStrIdx = 0;
    SmStateKind kind = SmStateKind::SingleClip;
    std::uint8_t reserved = 0;
    std::uint16_t primaryClipIdx = 0xFFFFu;
    std::uint16_t secondaryClipIdx = 0xFFFFu;  // Blend1D secondary clip
    std::uint16_t progressInputSlot = 0xFFFFu; // ScrubbedClip / Blend1D bound input slot
    float speedMultiplier = 1.0f;
    float blendRangeMin = 0.0f;
    float blendRangeMax = 1.0f;
};

enum class SmLayerBlendMode : std::uint8_t {
    Override = 0, // Overrides lower-layer property values (interpolated by layerWeight)
    Additive = 1, // Adds delta (value - baseOrRestValue) * layerWeight (e.g. HitShake, pulse)
};

struct AxbSmLayerDesc {
    std::uint32_t nameStrIdx = 0;
    SmLayerBlendMode blendMode = SmLayerBlendMode::Override;
    std::uint8_t reserved = 0;
    std::uint16_t entryStateIdx = 0;
    float layerWeight = 1.0f;
    std::uint32_t firstStateIdx = 0;
    std::uint16_t stateCount = 0;
    std::uint32_t firstTransitionIdx = 0;
    std::uint16_t transitionCount = 0;
};

// Component built-in pointer/focus listener (sets ContractInputBank slot directly without VM roundtrip)
enum class SmListenerEventKind : std::uint8_t {
    PointerEnter = 0,
    PointerExit = 1,
    PointerDown = 2,
    PointerUpInside = 3,
    FocusGained = 4,
    FocusLost = 5,
};

enum class SmListenerActionKind : std::uint8_t {
    SetFloatOrBool = 0,
    FireTrigger = 1,
    AddDelta = 2,
};

struct AxbSmPointerListener {
    std::uint16_t hitTargetElementIdx = 0;
    SmListenerEventKind eventKind = SmListenerEventKind::PointerEnter;
    SmListenerActionKind actionKind = SmListenerActionKind::SetFloatOrBool;
    std::uint16_t targetInputSlot = 0;
    float actionValue = 1.0f;
};

struct AxbContractInputDesc {
    std::uint32_t nameStrIdx = 0;
    std::uint8_t valueKind = 0;           // Matches aero::ir::ContractValueKind
    std::uint8_t isTrigger = 0;
    std::uint16_t bridgePropId = 0xFFFFu; // Maps to PropId_FirstContractInput + slotIdx for external DataContext binding
    UnifiedValue defaultValue{};
};

struct AxbStateMachineDesc {
    std::uint32_t nameStrIdx = 0;
    std::uint32_t firstLayerIdx = 0;
    std::uint16_t layerCount = 0;
    std::uint32_t firstListenerIdx = 0;
    std::uint16_t listenerCount = 0;
};

struct AxbPackage {
    std::string className;
    std::string hostWrapperClassName;
    std::string rootTypeName;
    std::string rootDataTypeName;
    std::string startupUri;
    SymbolId fieldsOwnerTypeId = 0;
    std::uint32_t rootHandleFieldIndex = PropertySlotHandle::InvalidIndex;
    std::vector<std::string> stringTable;
    std::vector<AxbNamedControlSlot> namedControls;
    std::vector<AxbCompiledBindingChain> bindingChains;
    std::vector<AxbCompiledMultiBinding> multiBindings;
    std::vector<AxbEventBindingRecord> eventBindings;
    std::vector<AxbInstruction> instructions;
    std::vector<std::pair<std::string, UnifiedValue>> resources;
    std::vector<AxbMtsdfPathEntry> mtsdfPaths;
    std::vector<AxbStyleDefinition> styles;
    std::vector<AxbStoryboard> storyboards;
    std::vector<AxbDataTemplate> dataTemplates;
    std::vector<AxbControlTemplate> controlTemplates;

    // Phase 6 MOT6 flat motion & state-machine buffers
    std::vector<AxbFlatKeyframe> motionKeyframes;
    std::vector<AxbMotionTrackDesc> motionTracks;
    std::vector<AxbTimelineEventMarker> motionEvents;
    std::vector<AxbMotionClipDesc> motionClips;
    std::vector<AxbTransformConstraintDesc> transformConstraints;
    std::vector<AxbSmCondition> smConditions;
    std::vector<AxbSmTransition> smTransitions;
    std::vector<AxbSmState> smStates;
    std::vector<AxbSmLayerDesc> smLayers;
    std::vector<AxbSmPointerListener> smListeners;
    std::vector<AxbContractInputDesc> contractInputs;
    std::vector<AxbStateMachineDesc> stateMachines;

    // P1-2: Ordered list of merged ResourceDictionary Source URIs resolved into this package
    std::vector<std::string> mergedDictionarySources;

    std::uint32_t InternString(std::string_view text);
    [[nodiscard]] std::string_view GetString(std::uint32_t index) const noexcept;

    [[nodiscard]] bool HasMotionSection() const noexcept {
        return !motionKeyframes.empty() || !motionTracks.empty() || !motionEvents.empty() ||
               !motionClips.empty() || !transformConstraints.empty() || !smConditions.empty() ||
               !smTransitions.empty() || !smStates.empty() || !smLayers.empty() ||
               !smListeners.empty() || !contractInputs.empty() || !stateMachines.empty();
    }

    [[nodiscard]] const AxbControlTemplate* FindControlTemplate(std::string_view key) const noexcept {
        if (key.empty()) {
            return nullptr;
        }
        for (auto it = controlTemplates.rbegin(); it != controlTemplates.rend(); ++it) {
            if (it->key == key) {
                return &(*it);
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool HasP14Section() const noexcept;
    [[nodiscard]] bool HasP15Section() const noexcept;

    void PinValuesInPools() const noexcept;
    void MarkValuesInPools() const noexcept;

    // Runtime-compatible re-binding against an updated ProgramImage (< 5ms Hot Reload, 0 compiler frontend needed)
    bool BindAgainstProgram(
        const ProgramImage& program,
        std::string_view rootDataTypeName = {},
        const ExecutionDomainContext* domains = nullptr);

    [[nodiscard]] std::vector<std::uint8_t> Serialize() const;
    static bool Deserialize(
        const std::uint8_t* data,
        std::size_t size,
        AxbPackage& outPackage,
        std::string* errorMessage = nullptr);
    static bool Deserialize(
        const std::vector<std::uint8_t>& bytes,
        AxbPackage& outPackage,
        std::string* errorMessage = nullptr);
};

// Runtime-accessible property literal helpers & SVG Path -> 4-channel MTSDF baker
[[nodiscard]] std::uint16_t ResolveBuiltinPropertyId(std::string_view propertyName) noexcept;
[[nodiscard]] std::uint16_t ResolveTriggerConditionPropertyId(std::string_view propertyName) noexcept;
[[nodiscard]] std::uint16_t ResolveAnimationTargetPropertyId(std::string_view targetPropertyPath) noexcept;
[[nodiscard]] float ParseXamlTimeSpanSeconds(std::string_view rawTimeSpan, float fallbackSeconds = 0.0f) noexcept;
[[nodiscard]] EasingFunctionKind ParseEasingFunctionKind(std::string_view tagName) noexcept;
[[nodiscard]] EasingMode ParseEasingMode(std::string_view rawMode) noexcept;
[[nodiscard]] ValueConverterKind ParseValueConverterKind(std::string_view rawConverterKey) noexcept;
[[nodiscard]] TriggerComparisonOp ParseTriggerComparisonOp(std::string_view rawOp) noexcept;
[[nodiscard]] ControlStoryboardOption ParseControlStoryboardOption(std::string_view rawOpt) noexcept;
[[nodiscard]] UnifiedValue ApplyBuiltinValueConverter(
    ValueConverterKind kind,
    const UnifiedValue& rawVal,
    bool inverseForTwoWay = false) noexcept;

// P1-4: Zero-exception 32-bit float recursive-descent MathConverter expression evaluator
// Supports {0}..{8}, @VALUE, @PARAMETER, + - * / %, unary + - !, (), < <= > >= == != && || ? :,
// and min/max/abs/clamp/floor/ceil/round/pow/sqrt/sin/cos/tan/lerp/sign.
[[nodiscard]] bool EvaluateMathConverterExpression(
    std::string_view expression,
    const UnifiedValue* args,
    std::size_t argCount,
    float parameterValue,
    float& outResult) noexcept;

// P1-4: Zero-exception StringFormat engine supporting {0}..{8} placeholders & numeric specifiers
// (0, 00, 000, F0, F1, F2, P0, G, C2, #,.##, standalone specifiers like "F1", and escaped {{ / }}).
[[nodiscard]] std::string FormatBindingString(
    std::string_view formatSpec,
    const UnifiedValue* args,
    std::size_t argCount,
    const ExecutionDomainContext* domains = nullptr);

[[nodiscard]] RoutedTriggerEvent ParseRoutedTriggerEvent(
    std::string_view rawRoutedEvent,
    bool& outRecognized) noexcept;
[[nodiscard]] UnifiedValue ParseBuiltinLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue);
[[nodiscard]] Color ParseXamlColorLiteral(std::string_view raw) noexcept;
[[nodiscard]] std::string FormatColorHexArgb(const Color& c);
[[nodiscard]] KeyCode ParseXamlKeyCode(std::string_view raw) noexcept;
[[nodiscard]] std::uint8_t ParseXamlKeyModifiers(std::string_view raw) noexcept;
[[nodiscard]] std::string KeyCodeToName(KeyCode key);
[[nodiscard]] rs_rt::NativeHandleRegistry& FallbackVisualHandleRegistry() noexcept;
[[nodiscard]] Matrix2D ParseXamlMatrix2DLiteral(std::string_view raw) noexcept;
[[nodiscard]] std::string DecodeXamlXmlEntities(std::string_view raw);

[[nodiscard]] std::uint64_t ComputeSvgPathHash(std::string_view pathData) noexcept;
[[nodiscard]] Rect MeasureSvgPathBounds(std::string_view pathData);
[[nodiscard]] std::uint16_t SelectAdaptiveMtsdfCellSize(
    std::string_view pathData,
    float designMaxSpanPx = 32.0f,
    float* outRecommendedPxRange = nullptr) noexcept;
[[nodiscard]] AxbMtsdfPathEntry BakeSvgPathToMtsdfEntry(
    std::string_view pathData,
    std::uint16_t cellSize = 32,
    float pxRange = 4.0f,
    std::uint16_t atlasSlotIndex = 0,
    std::uint16_t atlasSize = 512);
void PackMtsdfAtlasSkyline(
    std::vector<AxbMtsdfPathEntry>& ioEntries,
    std::uint16_t atlasSize = 512) noexcept;

// P1-5: Real Rive v7 LEB128 binary .riv stream detection & runtime/compiler decoding to AxbPackage
[[nodiscard]] bool IsRiveBinaryStream(
    const std::uint8_t* rawBytes,
    std::size_t byteLength) noexcept;

[[nodiscard]] bool DecodeRiveBinaryToAxbPackage(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AxbPackage& outPackage,
    std::vector<std::string>* outDiagnostics = nullptr,
    std::string_view artboardNameHint = {});

struct DecomposedTransform2D {
    float translateX = 0.0f;
    float translateY = 0.0f;
    float rotateDeg = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float skewXDeg = 0.0f;
    float skewYDeg = 0.0f;
};
[[nodiscard]] DecomposedTransform2D PolarDecomposeMatrix2D(const Matrix2D& m) noexcept;

// Shared UTF-8 validation, CJK width classification & multi-line text metrics
[[nodiscard]] bool DecodeNextUtf8CodePoint(
    std::string_view utf8Text,
    std::size_t& ioCursor,
    std::uint32_t& outCodePoint) noexcept;
[[nodiscard]] bool IsWideCjkCodePoint(std::uint32_t codePoint) noexcept;
[[nodiscard]] Size MeasureUtf8TextRun(std::string_view utf8Text, float fontSize) noexcept;
[[nodiscard]] Size MeasureUtf8TextRun(
    std::string_view utf8Text,
    float fontSize,
    std::string_view fontFamily,
    float maxWidth = 1000000.0f,
    TextWrapping wrapMode = TextWrapping::NoWrap) noexcept;
[[nodiscard]] std::size_t CountVisibleUtf8Glyphs(std::string_view utf8Text) noexcept;

// RealScript identifier validation, indexed collection hop helpers & enum metadata helpers
[[nodiscard]] bool IsValidRealScriptFieldIdentifier(std::string_view name) noexcept;
[[nodiscard]] bool SplitIndexedMemberName(
    std::string_view raw,
    std::string& outBaseMember,
    std::int32_t& outIndex);
[[nodiscard]] bool TryResolveEnumMemberByName(
    const ProgramImage* program,
    SymbolId enumTypeId,
    std::string_view rawName,
    std::int64_t& outValue) noexcept;
[[nodiscard]] bool TryFormatEnumValueName(
    const ProgramImage* program,
    SymbolId enumTypeId,
    std::int64_t intVal,
    std::string& outName);

// Optional compiler hook registered by aerogui_compiler so runtime helpers (ContentControl_LoadXaml,
// live Loc.Source dictionary switching, theme loading) can compile XAML on demand without linking
// AeroCompiler into slim aerogui_runtime targets.
using RuntimeXamlCompileHook = bool (*)(
    std::string_view uriOrPath,
    const std::vector<std::string>& searchDirs,
    AxbPackage& outPackage);
void RegisterRuntimeXamlCompileHook(RuntimeXamlCompileHook hook) noexcept;
[[nodiscard]] RuntimeXamlCompileHook GetRuntimeXamlCompileHook() noexcept;

} // namespace aero
