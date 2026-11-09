#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "aero/core/RsAliases.hpp"

namespace aero {

// P0-2: Directly alias RealScript's 24-byte Value variant (via RsAliases.hpp) to avoid
// custom struct layout conflicts at offset 16 (std::variant index discriminator).
static_assert(
    sizeof(UnifiedValue) == 24,
    "UnifiedValue must remain exactly 24 bytes across C++ and RealScript ABI");
static_assert(
    std::is_trivially_copyable_v<Float4Value>,
    "Float4Value payload must be trivially copyable");

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr bool operator==(const Color& other) const noexcept {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    constexpr bool operator!=(const Color& other) const noexcept {
        return !(*this == other);
    }
    [[nodiscard]] constexpr Float4Value ToFloat4() const noexcept {
        return Float4Value{r, g, b, a};
    }
    [[nodiscard]] static constexpr Color FromFloat4(const Float4Value& v) noexcept {
        return Color{v.x, v.y, v.z, v.w};
    }
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;

    constexpr bool operator==(const Size& other) const noexcept {
        return width == other.width && height == other.height;
    }
    constexpr bool operator!=(const Size& other) const noexcept {
        return !(*this == other);
    }
};

struct Point {
    float x = 0.0f;
    float y = 0.0f;

    constexpr bool operator==(const Point& other) const noexcept {
        return x == other.x && y == other.y;
    }
    constexpr bool operator!=(const Point& other) const noexcept {
        return !(*this == other);
    }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr bool operator==(const Rect& other) const noexcept {
        return x == other.x && y == other.y &&
               width == other.width && height == other.height;
    }
    constexpr bool operator!=(const Rect& other) const noexcept {
        return !(*this == other);
    }
    [[nodiscard]] constexpr Float4Value ToFloat4() const noexcept {
        return Float4Value{x, y, width, height};
    }
    [[nodiscard]] static constexpr Rect FromFloat4(const Float4Value& v) noexcept {
        return Rect{v.x, v.y, v.z, v.w};
    }
};

struct Thickness {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Thickness() noexcept = default;
    constexpr explicit Thickness(float uniform) noexcept
        : left(uniform), top(uniform), right(uniform), bottom(uniform) {}
    constexpr Thickness(float horizontal, float vertical) noexcept
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    constexpr Thickness(float l, float t, float r, float b) noexcept
        : left(l), top(t), right(r), bottom(b) {}

    constexpr bool operator==(const Thickness& other) const noexcept {
        return left == other.left && top == other.top &&
               right == other.right && bottom == other.bottom;
    }
    constexpr bool operator!=(const Thickness& other) const noexcept {
        return !(*this == other);
    }
    [[nodiscard]] constexpr Float4Value ToFloat4() const noexcept {
        return Float4Value{left, top, right, bottom};
    }
    [[nodiscard]] static constexpr Thickness FromFloat4(const Float4Value& v) noexcept {
        return Thickness{v.x, v.y, v.z, v.w};
    }
};

struct CornerRadius {
    float topLeft = 0.0f;
    float topRight = 0.0f;
    float bottomRight = 0.0f;
    float bottomLeft = 0.0f;

    constexpr CornerRadius() noexcept = default;
    constexpr explicit CornerRadius(float uniform) noexcept
        : topLeft(uniform), topRight(uniform), bottomRight(uniform), bottomLeft(uniform) {}
    constexpr CornerRadius(float tl, float tr, float br, float bl) noexcept
        : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

    constexpr bool operator==(const CornerRadius& other) const noexcept {
        return topLeft == other.topLeft && topRight == other.topRight &&
               bottomRight == other.bottomRight && bottomLeft == other.bottomLeft;
    }
    constexpr bool operator!=(const CornerRadius& other) const noexcept {
        return !(*this == other);
    }
    [[nodiscard]] constexpr Float4Value ToFloat4() const noexcept {
        return Float4Value{topLeft, topRight, bottomRight, bottomLeft};
    }
    [[nodiscard]] static constexpr CornerRadius FromFloat4(const Float4Value& v) noexcept {
        return CornerRadius{v.x, v.y, v.z, v.w};
    }
};

[[nodiscard]] inline UnifiedValue MakeFloat(float v) noexcept {
    return FloatValue{v};
}

[[nodiscard]] inline bool TryGetFloat(const UnifiedValue& v, float& out) noexcept {
    if (const auto* f = std::get_if<FloatValue>(&v)) {
        out = f->value;
        return true;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        out = static_cast<float>(*i);
        return true;
    }
    return false;
}

namespace value {

[[nodiscard]] inline UnifiedValue Null() noexcept {
    return std::monostate{};
}

[[nodiscard]] inline UnifiedValue FromBool(bool v) noexcept {
    return v;
}

[[nodiscard]] inline UnifiedValue FromInt(std::int64_t v) noexcept {
    return v;
}

[[nodiscard]] inline UnifiedValue MakeFloat(float v) noexcept {
    return FloatValue{v};
}

[[nodiscard]] inline UnifiedValue FromFloat(float v) noexcept {
    return FloatValue{v};
}

[[nodiscard]] inline bool TryGetFloat(const UnifiedValue& v, float& out) noexcept {
    return ::aero::TryGetFloat(v, out);
}

[[nodiscard]] inline std::optional<float> TryGetFloat(const UnifiedValue& v) noexcept {
    float out = 0.0f;
    if (::aero::TryGetFloat(v, out)) {
        return out;
    }
    return std::nullopt;
}

[[nodiscard]] inline UnifiedValue FromString(std::string v) {
    return StringValue{std::move(v)};
}

[[nodiscard]] inline UnifiedValue FromColor(const Color& c) noexcept {
    return c.ToFloat4();
}

[[nodiscard]] inline UnifiedValue FromRect(const Rect& r) noexcept {
    return r.ToFloat4();
}

[[nodiscard]] inline UnifiedValue FromThickness(const Thickness& t) noexcept {
    return t.ToFloat4();
}

[[nodiscard]] inline UnifiedValue FromCornerRadius(const CornerRadius& cr) noexcept {
    return cr.ToFloat4();
}

[[nodiscard]] inline std::optional<Color> TryGetColor(const UnifiedValue& v) noexcept {
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return Color::FromFloat4(*f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Rect> TryGetRect(const UnifiedValue& v) noexcept {
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return Rect::FromFloat4(*f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Thickness> TryGetThickness(const UnifiedValue& v) noexcept {
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return Thickness::FromFloat4(*f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<CornerRadius> TryGetCornerRadius(
    const UnifiedValue& v) noexcept {
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return CornerRadius::FromFloat4(*f4);
    }
    return std::nullopt;
}

} // namespace value

} // namespace aero
