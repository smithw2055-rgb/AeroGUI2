#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

inline constexpr std::uint32_t kAxb4Magic = 0x34425841u; // "AXB4"
inline constexpr std::uint32_t kAxb4Version = 0x00020000u;
inline constexpr std::uint32_t kAxb4MotionSectionMagic = 0x36544F4Du; // "MOT6"

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
};

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
};

struct AxbAnimationTimeline {
    std::string targetName;
    std::uint16_t targetPropertyId = 0;
    float beginTimeSeconds = 0.0f;
    float durationSeconds = 0.25f;
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

struct AxbPropertySetter {
    std::string targetName;
    std::uint16_t propId = 0;
    UnifiedValue value{};
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

enum class TriggerConditionSource : std::uint8_t {
    SelfProperty = 0,
    DataContextBinding = 1,
    ElementNameProperty = 2,
    AncestorProperty = 3,
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
};

struct AxbStyleTrigger {
    std::vector<AxbTriggerCondition> conditions;
    std::vector<AxbPropertySetter> setters;
    std::vector<AxbStoryboard> enterStoryboards;
    std::vector<AxbStoryboard> exitStoryboards;
    bool overrideLocalValues = false;
};

enum class RoutedTriggerEvent : std::uint8_t {
    Loaded = 0,
    Click = 1,
    MouseEnter = 2,
    MouseLeave = 3,
    Checked = 4,
    Unchecked = 5,
};

struct AxbEventTrigger {
    RoutedTriggerEvent eventKind = RoutedTriggerEvent::Loaded;
    std::string sourceName;
    std::vector<AxbStoryboard> storyboards;
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

struct AxbStyleDefinition {
    std::string key;
    std::string targetTypeName;
    std::string basedOnKey;
    std::vector<AxbPropertySetter> setters;
    std::vector<AxbStyleTrigger> triggers;
    std::vector<AxbEventTrigger> eventTriggers;
    std::vector<AxbVisualStateGroup> visualStateGroups;
};

struct AxbCompiledBindingChain {
    std::string rawPath;
    std::uint16_t targetPropertyId = 0;
    BindingMode mode = BindingMode::OneWay;
    UnifiedValue fallbackValue{};
    std::vector<AxbCompiledHop> hops;
    // Phase 5D: ElementName / RelativeSource & zero-allocation built-in value converter
    std::string elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    ValueConverterKind converter = ValueConverterKind::None;
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
};

struct AxbPackage;

struct AxbDataTemplate {
    std::string key;
    std::string dataTypeName;
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
    std::string rootTypeName;
    std::uint32_t rootHandleFieldIndex = PropertySlotHandle::InvalidIndex;
    std::vector<std::string> stringTable;
    std::vector<AxbNamedControlSlot> namedControls;
    std::vector<AxbCompiledBindingChain> bindingChains;
    std::vector<AxbEventBindingRecord> eventBindings;
    std::vector<AxbInstruction> instructions;
    std::vector<std::pair<std::string, UnifiedValue>> resources;
    std::vector<AxbMtsdfPathEntry> mtsdfPaths;
    std::vector<AxbStyleDefinition> styles;
    std::vector<AxbStoryboard> storyboards;
    std::vector<AxbDataTemplate> dataTemplates;

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

    std::uint32_t InternString(std::string_view text);
    [[nodiscard]] std::string_view GetString(std::uint32_t index) const noexcept;

    [[nodiscard]] bool HasMotionSection() const noexcept {
        return !motionKeyframes.empty() || !motionTracks.empty() || !motionEvents.empty() ||
               !motionClips.empty() || !transformConstraints.empty() || !smConditions.empty() ||
               !smTransitions.empty() || !smStates.empty() || !smLayers.empty() ||
               !smListeners.empty() || !contractInputs.empty() || !stateMachines.empty();
    }

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
[[nodiscard]] UnifiedValue ApplyBuiltinValueConverter(
    ValueConverterKind kind,
    const UnifiedValue& rawVal,
    bool inverseForTwoWay = false) noexcept;
[[nodiscard]] RoutedTriggerEvent ParseRoutedTriggerEvent(
    std::string_view rawRoutedEvent,
    bool& outRecognized) noexcept;
[[nodiscard]] UnifiedValue ParseBuiltinLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue);
[[nodiscard]] Color ParseXamlColorLiteral(std::string_view raw) noexcept;
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
[[nodiscard]] std::size_t CountVisibleUtf8Glyphs(std::string_view utf8Text) noexcept;

} // namespace aero
