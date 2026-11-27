#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
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

inline void AppendUtf8CodePoint(std::string& dst, std::uint32_t cp) {
    if (cp == 0u || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return;
    }
    if (cp <= 0x7Fu) {
        dst.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        dst.push_back(static_cast<char>(0xC0u | ((cp >> 6u) & 0x1Fu)));
        dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        dst.push_back(static_cast<char>(0xE0u | ((cp >> 12u) & 0x0Fu)));
        dst.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        dst.push_back(static_cast<char>(0xF0u | ((cp >> 18u) & 0x07u)));
        dst.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
        dst.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        dst.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

[[nodiscard]] inline std::string Utf8FromCodePoint(std::int64_t cpRaw) {
    if (cpRaw <= 0 || cpRaw > 0x10FFFF || (cpRaw >= 0xD800 && cpRaw <= 0xDFFF)) {
        return {};
    }
    std::string out;
    AppendUtf8CodePoint(out, static_cast<std::uint32_t>(cpRaw));
    return out;
}

[[nodiscard]] inline std::int64_t CountUtf8CodePoints(std::string_view s) noexcept {
    std::int64_t count = 0;
    for (std::size_t i = 0u; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t adv = 1u;
        if ((c & 0x80u) == 0u) adv = 1u;
        else if ((c & 0xE0u) == 0xC0u) adv = 2u;
        else if ((c & 0xF0u) == 0xE0u) adv = 3u;
        else if ((c & 0xF8u) == 0xF0u) adv = 4u;
        i += std::min(adv, s.size() - i);
        ++count;
    }
    return count;
}

[[nodiscard]] inline std::string_view SliceUtf8CodePoints(
    std::string_view s,
    std::int32_t startCp,
    std::int32_t countCp) noexcept {
    if (countCp <= 0 || s.empty()) {
        return {};
    }
    startCp = std::max<std::int32_t>(0, startCp);
    const std::int64_t endCp = static_cast<std::int64_t>(startCp) + static_cast<std::int64_t>(countCp);
    std::size_t byteStart = s.size();
    std::size_t byteEnd = s.size();
    std::int64_t cpIdx = 0;
    for (std::size_t i = 0u; i < s.size();) {
        if (cpIdx == startCp) {
            byteStart = i;
        }
        if (cpIdx == endCp) {
            byteEnd = i;
            break;
        }
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t adv = 1u;
        if ((c & 0x80u) == 0u) adv = 1u;
        else if ((c & 0xE0u) == 0xC0u) adv = 2u;
        else if ((c & 0xF0u) == 0xE0u) adv = 3u;
        else if ((c & 0xF8u) == 0xF0u) adv = 4u;
        i += std::min(adv, s.size() - i);
        ++cpIdx;
    }
    if (byteStart >= s.size() || byteEnd <= byteStart) {
        return {};
    }
    return s.substr(byteStart, byteEnd - byteStart);
}

} // namespace aero::str

namespace aero {
using str::DecodeNextUtf8CodePoint;
using str::IsWideCjkCodePoint;
using str::CountVisibleUtf8Glyphs;
} // namespace aero
