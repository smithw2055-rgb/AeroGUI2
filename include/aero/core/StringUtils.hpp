#pragma once

#include <cstddef>
#include <cstdint>
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

[[nodiscard]] inline bool DecodeNextUtf8CodePoint(
    std::string_view utf8Text,
    std::size_t& ioCursor,
    std::uint32_t& outCodePoint) noexcept {
    if (ioCursor >= utf8Text.size()) {
        return false;
    }
    const auto b0 = static_cast<std::uint8_t>(utf8Text[ioCursor]);
    if ((b0 & 0x80u) == 0u) {
        outCodePoint = b0;
        ioCursor += 1u;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u && b0 >= 0xC2u && ioCursor + 1u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        if ((b1 & 0xC0u) == 0x80u) {
            outCodePoint = (static_cast<std::uint32_t>(b0 & 0x1Fu) << 6u) | (b1 & 0x3Fu);
            ioCursor += 2u;
            return true;
        }
    } else if ((b0 & 0xF0u) == 0xE0u && ioCursor + 2u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
            const std::uint32_t cp =
                (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12u) |
                (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6u) |
                (b2 & 0x3Fu);
            if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) {
                outCodePoint = cp;
                ioCursor += 3u;
                return true;
            }
        }
    } else if ((b0 & 0xF8u) == 0xF0u && b0 <= 0xF4u && ioCursor + 3u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
        const auto b3 = static_cast<std::uint8_t>(utf8Text[ioCursor + 3u]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
            const std::uint32_t cp =
                (static_cast<std::uint32_t>(b0 & 0x07u) << 18u) |
                (static_cast<std::uint32_t>(b1 & 0x3Fu) << 12u) |
                (static_cast<std::uint32_t>(b2 & 0x3Fu) << 6u) |
                (b3 & 0x3Fu);
            if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                outCodePoint = cp;
                ioCursor += 4u;
                return true;
            }
        }
    }
    outCodePoint = 0xFFFDu;
    ioCursor += 1u;
    return true;
}

[[nodiscard]] constexpr bool IsWideCjkCodePoint(std::uint32_t cp) noexcept {
    return (cp >= 0x2E80u && cp <= 0x9FFFu) ||
           (cp >= 0xAC00u && cp <= 0xD7AFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu) ||
           (cp >= 0xFF01u && cp <= 0xFF60u) ||
           (cp >= 0x20000u && cp <= 0x2FA1Fu);
}

[[nodiscard]] inline std::size_t CountVisibleUtf8Glyphs(std::string_view utf8Text) noexcept {
    std::size_t count = 0u;
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        if (cp != ' ' && cp != '\r' && cp != '\n') {
            ++count;
        }
    }
    return count;
}

} // namespace aero::str

namespace aero {
using str::DecodeNextUtf8CodePoint;
using str::IsWideCjkCodePoint;
using str::CountVisibleUtf8Glyphs;
} // namespace aero
