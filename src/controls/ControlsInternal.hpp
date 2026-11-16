#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aero/controls/Controls.hpp"

namespace aero {

class AnimationEngine;
class AxbPackage;
struct AxbControlTemplate;
struct AxbStyleDefinition;
class DualTrackBindingEngine;
class ExecutionDomainContext;
class StyleTriggerEngine;

namespace detail {

// ============================================================================
// Shared Constants & Helpers across src/controls/*.cpp (Step 2B)
// ============================================================================

inline constexpr float kUnconstrainedSpace = 1000000.0f;
inline constexpr std::size_t kInvalidVirtualSlotIndex = std::numeric_limits<std::size_t>::max();
inline constexpr std::size_t kMaxVirtualRecyclePoolSize = 64u;

void MarkSubtreeRenderDirtyRecursive(UIElement* elem) noexcept;
[[nodiscard]] bool DispatchClickCommandAndScript(
    UIElement& owner,
    const ScriptClickBinding& clickBinding,
    RuntimeError* error);

[[nodiscard]] inline bool IsFloatingPopupOverlay(const UIElement* elem) noexcept {
    return elem != nullptr && (elem->IsA<ToolTip>() || elem->IsA<ContextMenu>());
}

[[nodiscard]] std::int32_t CoerceInt32Property(const UnifiedValue& v, std::int32_t fallbackVal) noexcept;
[[nodiscard]] bool CoerceBoolProperty(const UnifiedValue& v, bool fallbackVal) noexcept;
[[nodiscard]] std::uint32_t ExtractGradientBrushId(const UnifiedValue& v) noexcept;
[[nodiscard]] bool PointInRect(const Rect& r, Point p) noexcept;

void ComputeViewboxScaleFactors(
    float contentW,
    float contentH,
    float availW,
    float availH,
    bool hasW,
    bool hasH,
    Stretch stretch,
    StretchDirection dir,
    float& outSx,
    float& outSy) noexcept;

void SyncHostContentTemplateChildImpl(
    UIElement* hostContainer,
    const UIElement* contentSourceHost,
    IntrusivePtr<UIElement>& trackedChild,
    std::string& activeKey,
    ObjectRef& activeObj,
    bool& hasActiveObj,
    bool& syncingGuard);

[[nodiscard]] std::string ResolveRichTextBindToken(
    std::string_view propPath,
    std::string_view formatSpec,
    const UIElement* owner);

[[nodiscard]] std::size_t CountUtf8CodePointsSafe(std::string_view s) noexcept;
[[nodiscard]] std::size_t Utf8ByteOffsetAtCodePoint(std::string_view s, std::size_t cpTarget) noexcept;
[[nodiscard]] bool IsWordCodePoint(std::uint32_t cp) noexcept;
[[nodiscard]] std::size_t PrevWordBoundaryCp(std::string_view text, std::size_t fromCp) noexcept;
[[nodiscard]] std::size_t NextWordBoundaryCp(std::string_view text, std::size_t fromCp) noexcept;
std::mutex& SharedClipboardMutex() noexcept;
std::string& SharedClipboardBuffer() noexcept;

[[nodiscard]] std::string ExtractElementDisplayText(const UIElement* elem);
[[nodiscard]] std::string ExtractSubtreeDisplayText(const UIElement* el);
[[nodiscard]] bool IsTreeHeaderSlotChild(const UIElement* c) noexcept;

void UncheckNamedRadioGroupRecursive(
    UIElement* node,
    const RadioButton* activeRadio,
    std::string_view groupName,
    bool pushTwoWay);

[[nodiscard]] bool CollectionItemValuesEqual(
    const UnifiedValue& a,
    const UnifiedValue& b) noexcept;

[[nodiscard]] std::string ResolveSmartObjectDisplayText(
    const UIElement* hostCtrl,
    ExecutionDomainContext* dom,
    DualTrackBindingEngine* bindEng,
    const UnifiedValue& itemVal);

enum class ItemsPanelMode : std::uint8_t {
    VerticalStack = 0,
    HorizontalStack = 1,
    UniformGrid = 2,
    WrapHorizontal = 3,
    WrapVertical = 4,
    Canvas = 5,
};

struct ParsedItemsPanelConfig {
    ItemsPanelMode mode = ItemsPanelMode::VerticalStack;
    std::int32_t rows = 0;
    std::int32_t cols = 0;
    std::int32_t firstCol = 0;
    float spacing = 0.0f;
    float itemWidth = -1.0f;
    float itemHeight = -1.0f;
};

[[nodiscard]] ParsedItemsPanelConfig ResolveItemsPanelConfig(const ItemsControl* ic) noexcept;

[[nodiscard]] UnifiedValue ResolveSelectedValueForItem(
    const UIElement* selector,
    ExecutionDomainContext* domains,
    const DualTrackBindingEngine* bindingEngine,
    const UnifiedValue& itemVal);

[[nodiscard]] std::string_view RoutedCommandLeafName(std::string_view raw) noexcept;
[[nodiscard]] DualTrackBindingEngine* FindEngineNear(const UIElement* start) noexcept;
[[nodiscard]] StyleTriggerEngine* FindStyleEngineNear(const UIElement* start) noexcept;
[[nodiscard]] SymbolId FindMethodIdByNameOnObj(
    ExecutionDomainContext& domains,
    const ProgramImage& prog,
    ObjectRef obj,
    std::string_view methodName) noexcept;

[[nodiscard]] IntrusivePtr<UIElement> CreateElementByTypeNameCore(
    std::string_view typeName,
    std::string_view shortName);

[[nodiscard]] const AxbStyleDefinition* LookupStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept;

[[nodiscard]] const AxbStyleDefinition* LookupImplicitStyleInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view shortTypeName) noexcept;

[[nodiscard]] const AxbControlTemplate* LookupControlTemplateInPackageOrRegistry(
    const AxbPackage& package,
    std::string_view key) noexcept;

void AutoApplyControlStyleTemplate(
    UIElement* elem,
    std::string_view declaredTypeName,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine);

bool MountAxbInstructionsInternal(
    UIElement* rootControl,
    UIElement* templatedHost,
    const AxbPackage& package,
    ExecutionDomainContext* domains,
    DualTrackBindingEngine* bindingEngine,
    ObjectRef dataContext,
    const Interpreter* eventInterpreter,
    rs_aot::ExecutionContext* eventAotContext,
    AnimationEngine* animationEngine,
    bool structureOnly = false,
    bool bindingsOnly = false,
    std::vector<IntrusivePtr<UIElement>>* ioMountedElements = nullptr);

[[nodiscard]] bool ReadBinaryFileBytes(
    const std::string& path,
    std::vector<std::uint8_t>& outBytes);

[[nodiscard]] bool ResolveAndReadRiveAsset(
    std::string_view rawUri,
    std::vector<std::uint8_t>& outBytes);

} // namespace detail
} // namespace aero
