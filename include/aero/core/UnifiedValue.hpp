#pragma once

#include <algorithm>
#include <cmath>
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
    [[nodiscard]] constexpr Float4Value ToFloat4() const noexcept {
        return Float4Value{x, y, 0.0f, 0.0f};
    }
    [[nodiscard]] static constexpr Point FromFloat4(const Float4Value& v) noexcept {
        return Point{v.x, v.y};
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

// Full 2D affine transform in column-vector convention:
//   x' = m00 * x + m01 * y + tx
//   y' = m10 * x + m11 * y + ty
struct Matrix2D {
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;

    constexpr bool operator==(const Matrix2D& other) const noexcept {
        return m00 == other.m00 && m01 == other.m01 &&
               m10 == other.m10 && m11 == other.m11 &&
               tx == other.tx && ty == other.ty;
    }
    constexpr bool operator!=(const Matrix2D& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] static constexpr Matrix2D Identity() noexcept {
        return Matrix2D{};
    }

    [[nodiscard]] constexpr bool IsIdentity(float eps = 1e-6f) const noexcept {
        return (m00 - 1.0f <= eps && m00 - 1.0f >= -eps) &&
               (m01 <= eps && m01 >= -eps) &&
               (m10 <= eps && m10 >= -eps) &&
               (m11 - 1.0f <= eps && m11 - 1.0f >= -eps) &&
               (tx <= eps && tx >= -eps) &&
               (ty <= eps && ty >= -eps);
    }

    [[nodiscard]] static constexpr Matrix2D Translation(float dx, float dy) noexcept {
        return Matrix2D{1.0f, 0.0f, 0.0f, 1.0f, dx, dy};
    }

    [[nodiscard]] static constexpr Matrix2D Scaling(
        float sx,
        float sy,
        float centerX = 0.0f,
        float centerY = 0.0f) noexcept {
        return Matrix2D{
            sx,
            0.0f,
            0.0f,
            sy,
            centerX * (1.0f - sx),
            centerY * (1.0f - sy)};
    }

    [[nodiscard]] static inline Matrix2D Rotation(
        float angleDeg,
        float centerX = 0.0f,
        float centerY = 0.0f) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = angleDeg * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        return Matrix2D{
            c,
            -s,
            s,
            c,
            centerX - centerX * c + centerY * s,
            centerY - centerX * s - centerY * c};
    }

    [[nodiscard]] static inline Matrix2D Skew(
        float angleXDeg,
        float angleYDeg,
        float centerX = 0.0f,
        float centerY = 0.0f) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float kx = std::tan(angleXDeg * kDegToRad);
        const float ky = std::tan(angleYDeg * kDegToRad);
        return Matrix2D{
            1.0f,
            kx,
            ky,
            1.0f,
            -centerY * kx,
            -centerX * ky};
    }

    // Converts XAML/WPF row-vector Matrix(M11, M12, M21, M22, OffsetX, OffsetY)
    // where x' = x*M11 + y*M21 + OffsetX, y' = x*M12 + y*M22 + OffsetY.
    [[nodiscard]] static constexpr Matrix2D FromWpfMatrix(
        float m11,
        float m12,
        float m21,
        float m22,
        float offsetX,
        float offsetY) noexcept {
        return Matrix2D{m11, m21, m12, m22, offsetX, offsetY};
    }

    // Returns after * before (applies `before` first, then `after`)
    [[nodiscard]] static constexpr Matrix2D Multiply(
        const Matrix2D& after,
        const Matrix2D& before) noexcept {
        return Matrix2D{
            after.m00 * before.m00 + after.m01 * before.m10,
            after.m00 * before.m01 + after.m01 * before.m11,
            after.m10 * before.m00 + after.m11 * before.m10,
            after.m10 * before.m01 + after.m11 * before.m11,
            after.m00 * before.tx + after.m01 * before.ty + after.tx,
            after.m10 * before.tx + after.m11 * before.ty + after.ty};
    }

    // Conjugates this transform around world/layout pivot (pivotX, pivotY): T(P) * (*this) * T(-P)
    [[nodiscard]] constexpr Matrix2D AroundPivot(float pivotX, float pivotY) const noexcept {
        return Matrix2D{
            m00,
            m01,
            m10,
            m11,
            pivotX - (m00 * pivotX + m01 * pivotY) + tx,
            pivotY - (m10 * pivotX + m11 * pivotY) + ty};
    }

    [[nodiscard]] constexpr Point TransformPoint(Point p) const noexcept {
        return Point{
            m00 * p.x + m01 * p.y + tx,
            m10 * p.x + m11 * p.y + ty};
    }

    [[nodiscard]] inline Rect TransformRectBounds(const Rect& r) const noexcept {
        const Point p0 = TransformPoint(Point{r.x, r.y});
        const Point p1 = TransformPoint(Point{r.x + r.width, r.y});
        const Point p2 = TransformPoint(Point{r.x + r.width, r.y + r.height});
        const Point p3 = TransformPoint(Point{r.x, r.y + r.height});
        const float minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return Rect{minX, minY, maxX - minX, maxY - minY};
    }

    [[nodiscard]] inline bool TryInvert(Matrix2D& outInv) const noexcept {
        const float det = m00 * m11 - m01 * m10;
        if (std::fabs(det) <= 1e-8f) {
            return false;
        }
        const float invDet = 1.0f / det;
        outInv.m00 = m11 * invDet;
        outInv.m01 = -m01 * invDet;
        outInv.m10 = -m10 * invDet;
        outInv.m11 = m00 * invDet;
        outInv.tx = -(outInv.m00 * tx + outInv.m01 * ty);
        outInv.ty = -(outInv.m10 * tx + outInv.m11 * ty);
        return true;
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

[[nodiscard]] inline UnifiedValue FromPoint(Point p) noexcept {
    return p.ToFloat4();
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

[[nodiscard]] inline std::optional<Point> TryGetPoint(const UnifiedValue& v) noexcept {
    if (const auto* f4 = std::get_if<Float4Value>(&v)) {
        return Point::FromFloat4(*f4);
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
