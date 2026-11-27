#include "aero/drawing/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] int HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

[[nodiscard]] int HexByte(char hi, char lo) noexcept {
    return (HexNibble(hi) << 4) | HexNibble(lo);
}

[[nodiscard]] bool IsHexDigit(char c) noexcept {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

[[nodiscard]] Color ParseHexColor(std::string_view hex) noexcept {
    for (char c : hex) {
        if (!IsHexDigit(c)) {
            return Color{0.0f, 0.0f, 0.0f, 1.0f};
        }
    }
    if (hex.size() == 3u) {
        return Color{
            static_cast<float>(HexNibble(hex[0]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[1]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[2]) * 17) / 255.0f,
            1.0f};
    }
    if (hex.size() == 4u) {
        return Color{
            static_cast<float>(HexNibble(hex[1]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[2]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[3]) * 17) / 255.0f,
            static_cast<float>(HexNibble(hex[0]) * 17) / 255.0f};
    }
    if (hex.size() == 6u) {
        return Color{
            static_cast<float>(HexByte(hex[0], hex[1])) / 255.0f,
            static_cast<float>(HexByte(hex[2], hex[3])) / 255.0f,
            static_cast<float>(HexByte(hex[4], hex[5])) / 255.0f,
            1.0f};
    }
    if (hex.size() == 8u) {
        return Color{
            static_cast<float>(HexByte(hex[2], hex[3])) / 255.0f,
            static_cast<float>(HexByte(hex[4], hex[5])) / 255.0f,
            static_cast<float>(HexByte(hex[6], hex[7])) / 255.0f,
            static_cast<float>(HexByte(hex[0], hex[1])) / 255.0f};
    }
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

} // namespace

Color ParseColorLiteral(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (!raw.empty() && raw.front() == '#') {
        raw.remove_prefix(1u);
        return ParseHexColor(raw);
    }
    struct NamedColorEntry {
        std::string_view name;
        std::string_view hex8;
    };
    static constexpr NamedColorEntry kNamedColors[] = {
        {"Transparent", "00000000"},
        {"AliceBlue", "FFF0F8FF"},
        {"AntiqueWhite", "FFFAEBD7"},
        {"Aqua", "FF00FFFF"},
        {"Aquamarine", "FF7FFFD4"},
        {"Azure", "FFF0FFFF"},
        {"Beige", "FFF5F5DC"},
        {"Bisque", "FFFFE4C4"},
        {"Black", "FF000000"},
        {"BlanchedAlmond", "FFFFEBCD"},
        {"Blue", "FF0000FF"},
        {"BlueViolet", "FF8A2BE2"},
        {"Brown", "FFA52A2A"},
        {"BurlyWood", "FFDEB887"},
        {"CadetBlue", "FF5F9EA0"},
        {"Chartreuse", "FF7FFF00"},
        {"Chocolate", "FFD2691E"},
        {"Coral", "FFFF7F50"},
        {"CornflowerBlue", "FF6495ED"},
        {"Cornsilk", "FFFFF8DC"},
        {"Crimson", "FFDC143C"},
        {"Cyan", "FF00FFFF"},
        {"DarkBlue", "FF00008B"},
        {"DarkCyan", "FF008B8B"},
        {"DarkGoldenrod", "FFB8860B"},
        {"DarkGray", "FFA9A9A9"},
        {"DarkGrey", "FFA9A9A9"},
        {"DarkGreen", "FF006400"},
        {"DarkKhaki", "FFBDB76B"},
        {"DarkMagenta", "FF8B008B"},
        {"DarkOliveGreen", "FF556B2F"},
        {"DarkOrange", "FFFF8C00"},
        {"DarkOrchid", "FF9932CC"},
        {"DarkRed", "FF8B0000"},
        {"DarkSalmon", "FFE9967A"},
        {"DarkSeaGreen", "FF8FBC8F"},
        {"DarkSlateBlue", "FF483D8B"},
        {"DarkSlateGray", "FF2F4F4F"},
        {"DarkSlateGrey", "FF2F4F4F"},
        {"DarkTurquoise", "FF00CED1"},
        {"DarkViolet", "FF9400D3"},
        {"DeepPink", "FFFF1493"},
        {"DeepSkyBlue", "FF00BFFF"},
        {"DimGray", "FF696969"},
        {"DimGrey", "FF696969"},
        {"DodgerBlue", "FF1E90FF"},
        {"Firebrick", "FFB22222"},
        {"FloralWhite", "FFFFFAF0"},
        {"ForestGreen", "FF228B22"},
        {"Fuchsia", "FFFF00FF"},
        {"Gainsboro", "FFDCDCDC"},
        {"GhostWhite", "FFF8F8FF"},
        {"Gold", "FFFFD700"},
        {"Goldenrod", "FFDAA520"},
        {"Gray", "FF808080"},
        {"Grey", "FF808080"},
        {"Green", "FF008000"},
        {"GreenYellow", "FFADFF2F"},
        {"Honeydew", "FFF0FFF0"},
        {"HotPink", "FFFF69B4"},
        {"IndianRed", "FFCD5C5C"},
        {"Indigo", "FF4B0082"},
        {"Ivory", "FFFFFFF0"},
        {"Khaki", "FFF0E68C"},
        {"Lavender", "FFE6E6FA"},
        {"LavenderBlush", "FFFFF0F5"},
        {"LawnGreen", "FF7CFC00"},
        {"LemonChiffon", "FFFFFACD"},
        {"LightBlue", "FFADD8E6"},
        {"LightCoral", "FFF08080"},
        {"LightCyan", "FFE0FFFF"},
        {"LightGoldenrodYellow", "FFFAFAD2"},
        {"LightGray", "FFD3D3D3"},
        {"LightGrey", "FFD3D3D3"},
        {"LightGreen", "FF90EE90"},
        {"LightPink", "FFFFB6C1"},
        {"LightSalmon", "FFFFA07A"},
        {"LightSeaGreen", "FF20B2AA"},
        {"LightSkyBlue", "FF87CEFA"},
        {"LightSlateGray", "FF778899"},
        {"LightSlateGrey", "FF778899"},
        {"LightSteelBlue", "FFB0C4DE"},
        {"LightYellow", "FFFFFFE0"},
        {"Lime", "FF00FF00"},
        {"LimeGreen", "FF32CD32"},
        {"Linen", "FFFAF0E6"},
        {"Magenta", "FFFF00FF"},
        {"Maroon", "FF800000"},
        {"MediumAquamarine", "FF66CDAA"},
        {"MediumBlue", "FF0000CD"},
        {"MediumOrchid", "FFBA55D3"},
        {"MediumPurple", "FF9370DB"},
        {"MediumSeaGreen", "FF3CB371"},
        {"MediumSlateBlue", "FF7B68EE"},
        {"MediumSpringGreen", "FF00FA9A"},
        {"MediumTurquoise", "FF48D1CC"},
        {"MediumVioletRed", "FFC71585"},
        {"MidnightBlue", "FF191970"},
        {"MintCream", "FFF5FFFA"},
        {"MistyRose", "FFFFE4E1"},
        {"Moccasin", "FFFFE4B5"},
        {"NavajoWhite", "FFFFDEAD"},
        {"Navy", "FF000080"},
        {"OldLace", "FFFDF5E6"},
        {"Olive", "FF808000"},
        {"OliveDrab", "FF6B8E23"},
        {"Orange", "FFFFA500"},
        {"OrangeRed", "FFFF4500"},
        {"Orchid", "FFDA70D6"},
        {"PaleGoldenrod", "FFEEE8AA"},
        {"PaleGreen", "FF98FB98"},
        {"PaleTurquoise", "FFAFEEEE"},
        {"PaleVioletRed", "FFDB7093"},
        {"PapayaWhip", "FFFFEFD5"},
        {"PeachPuff", "FFFFDAB9"},
        {"Peru", "FFCD853F"},
        {"Pink", "FFFFC0CB"},
        {"Plum", "FFDDA0DD"},
        {"PowderBlue", "FFB0E0E6"},
        {"Purple", "FF800080"},
        {"RebeccaPurple", "FF663399"},
        {"Red", "FFFF0000"},
        {"RosyBrown", "FFBC8F8F"},
        {"RoyalBlue", "FF4169E1"},
        {"SaddleBrown", "FF8B4513"},
        {"Salmon", "FFFA8072"},
        {"SandyBrown", "FFF4A460"},
        {"SeaGreen", "FF2E8B57"},
        {"SeaShell", "FFFFF5EE"},
        {"Sienna", "FFA0522D"},
        {"Silver", "FFC0C0C0"},
        {"SkyBlue", "FF87CEEB"},
        {"SlateBlue", "FF6A5ACD"},
        {"SlateGray", "FF708090"},
        {"SlateGrey", "FF708090"},
        {"Snow", "FFFFFAFA"},
        {"SpringGreen", "FF00FF7F"},
        {"SteelBlue", "FF4682B4"},
        {"Tan", "FFD2B48C"},
        {"Teal", "FF008080"},
        {"Thistle", "FFD8BFD8"},
        {"Tomato", "FFFF6347"},
        {"Turquoise", "FF40E0D0"},
        {"Violet", "FFEE82EE"},
        {"Wheat", "FFF5DEB3"},
        {"White", "FFFFFFFF"},
        {"WhiteSmoke", "FFF5F5F5"},
        {"Yellow", "FFFFFF00"},
        {"YellowGreen", "FF9ACD32"},
    };
    for (const auto& entry : kNamedColors) {
        if (str::EqualsIgnoreCase(raw, entry.name)) {
            return ParseHexColor(entry.hex8);
        }
    }
    const auto vals = detail::ParseFloatList(raw);
    if (vals.size() >= 4u) return Color{vals[0], vals[1], vals[2], vals[3]};
    if (vals.size() == 3u) return Color{vals[0], vals[1], vals[2], 1.0f};
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

std::uint32_t ParseColorRgba32(std::string_view raw) noexcept {
    return ParseColorLiteral(raw).PackRgba8();
}

} // namespace aero
