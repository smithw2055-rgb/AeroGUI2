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
    EasingFunctionKind defaultEasingKind = EasingFunctionKind::Cubic;
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

    std::uint32_t InternString(std::string_view text);
    [[nodiscard]] std::string_view GetString(std::uint32_t index) const noexcept;

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
[[nodiscard]] AxbMtsdfPathEntry BakeSvgPathToMtsdfEntry(
    std::string_view pathData,
    std::uint16_t cellSize = 32,
    float pxRange = 4.0f,
    std::uint16_t atlasSlotIndex = 0,
    std::uint16_t atlasSize = 512);

// Shared UTF-8 validation, CJK width classification & multi-line text metrics
[[nodiscard]] bool DecodeNextUtf8CodePoint(
    std::string_view utf8Text,
    std::size_t& ioCursor,
    std::uint32_t& outCodePoint) noexcept;
[[nodiscard]] bool IsWideCjkCodePoint(std::uint32_t codePoint) noexcept;
[[nodiscard]] Size MeasureUtf8TextRun(std::string_view utf8Text, float fontSize) noexcept;
[[nodiscard]] std::size_t CountVisibleUtf8Glyphs(std::string_view utf8Text) noexcept;

} // namespace aero
