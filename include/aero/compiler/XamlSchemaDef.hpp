#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace aero {

[[nodiscard]] float ParseXamlDurationSeconds(std::string_view raw);
[[nodiscard]] std::string NormalizeGradientStopPath(std::string_view raw);
[[nodiscard]] std::string NormalizeAnimationTargetPropertyPath(std::string_view raw);

[[nodiscard]] int MapXamlEasingKind(std::string_view name) noexcept;
[[nodiscard]] int MapXamlEasingMode(std::string_view mode) noexcept;
[[nodiscard]] int MapXamlBindingMode(std::string_view mode) noexcept;
[[nodiscard]] int ResolveDefaultBindingMode(
    std::string_view targetType,
    std::string_view propName) noexcept;

[[nodiscard]] const char* LookupXamlEnumType(std::string_view propName);
[[nodiscard]] bool IsXamlFloatProperty(std::string_view prop);
[[nodiscard]] bool IsXamlIntProperty(std::string_view prop);
[[nodiscard]] bool IsXamlBoolProperty(std::string_view prop);
[[nodiscard]] bool IsXamlThicknessProperty(std::string_view prop) noexcept;
[[nodiscard]] bool IsXamlBrushProperty(std::string_view prop) noexcept;
[[nodiscard]] bool IsXamlBrushElementTag(std::string_view tagName) noexcept;
[[nodiscard]] bool IsXamlEventAttribute(std::string_view prop);
[[nodiscard]] bool IsXamlDirectUIProperty(std::string_view prop);

[[nodiscard]] bool IsXamlContentControlType(std::string_view typeName);
[[nodiscard]] bool IsXamlItemsControlType(std::string_view typeName);
[[nodiscard]] bool IsXamlPanelType(std::string_view typeName);
[[nodiscard]] bool IsXamlShapeType(std::string_view typeName);
[[nodiscard]] bool IsXamlRangeBaseType(std::string_view typeName) noexcept;
[[nodiscard]] bool IsXamlToggleButtonType(std::string_view typeName) noexcept;
[[nodiscard]] bool IsXamlSelectableItemType(std::string_view typeName) noexcept;
[[nodiscard]] bool IsXamlSelectorType(std::string_view typeName) noexcept;
[[nodiscard]] bool IsXamlControlType(std::string_view typeName);
[[nodiscard]] bool IsXamlNonUIElementType(std::string_view typeName);
[[nodiscard]] bool IsXamlKnownUIElementType(std::string_view typeName);
[[nodiscard]] bool IsXamlCommonUIElementDirectProperty(std::string_view prop);

[[nodiscard]] bool IsXamlDirectUIPropertyFor(std::string_view targetType, std::string_view prop);
[[nodiscard]] bool IsXamlFloatPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});
[[nodiscard]] bool IsXamlIntPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});
[[nodiscard]] bool IsXamlBoolPropertyFor(
    std::string_view targetType,
    std::string_view prop,
    std::string_view rawVal = {});

void ExpandXamlControlHierarchy(std::unordered_set<std::string>& usedTypes);

} // namespace aero
