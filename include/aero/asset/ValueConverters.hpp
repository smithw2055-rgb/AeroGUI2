#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "aero/core/UnifiedValue.hpp"

namespace aero {

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

[[nodiscard]] ValueConverterKind ParseValueConverterKind(std::string_view rawConverterKey) noexcept;

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

} // namespace aero
