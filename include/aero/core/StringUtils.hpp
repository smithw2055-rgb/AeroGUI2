#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace aero::str {

namespace detail {

[[nodiscard]] constexpr bool IsAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

[[nodiscard]] constexpr char ToLowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

} // namespace detail

[[nodiscard]] constexpr std::string_view Trim(std::string_view s) noexcept {
    while (!s.empty() && detail::IsAsciiSpace(s.front())) {
        s.remove_prefix(1u);
    }
    while (!s.empty() && detail::IsAsciiSpace(s.back())) {
        s.remove_suffix(1u);
    }
    return s;
}

[[nodiscard]] inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0u; i < a.size(); ++i) {
        if (detail::ToLowerAscii(a[i]) != detail::ToLowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool StartsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.substr(0u, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool EndsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

[[nodiscard]] constexpr std::string_view StripNamespacePrefix(std::string_view name) noexcept {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1u);
    }
    return name;
}

[[nodiscard]] constexpr std::string_view ShortTypeName(std::string_view raw) noexcept {
    raw = StripNamespacePrefix(raw);
    if (const auto dot = raw.rfind('.'); dot != std::string_view::npos) {
        raw = raw.substr(dot + 1u);
    }
    return raw;
}

[[nodiscard]] inline bool TryParseFloat32(std::string_view text, float& outVal) noexcept {
    text = Trim(text);
    char buf[128]{};
    if (text.empty() || text.size() >= sizeof(buf)) {
        return false;
    }
    std::memcpy(buf, text.data(), text.size());
    char* end = nullptr;
    const float parsed = std::strtof(buf, &end);
    if (end != buf + text.size()) {
        return false;
    }
    outVal = parsed;
    return true;
}

[[nodiscard]] inline float ParseFloat32Or(std::string_view text, float fallback = 0.0f) noexcept {
    float val = fallback;
    return TryParseFloat32(text, val) ? val : fallback;
}

} // namespace aero::str
