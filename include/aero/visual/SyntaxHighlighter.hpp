#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace aero {

enum class SyntaxHighlightLang : std::uint8_t {
    Plain = 0,
    RealScript = 1,
    Xaml = 2,
    AeroScene = 3,
};

struct StyledTextSpan {
    std::uint32_t byteOffset = 0u;
    std::uint32_t byteLength = 0u;
    std::uint32_t colorRgba = 0xFFFFFFFFu;
};

void TokenizeSyntaxSpans(
    std::string_view line,
    SyntaxHighlightLang lang,
    std::uint32_t defaultRgba,
    std::vector<StyledTextSpan>& outSpans);

} // namespace aero
