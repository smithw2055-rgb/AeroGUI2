#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "aero/core/RsAliases.hpp"
#include "aero/drawing/Geometry.hpp"

namespace aero {

// RealScript RuntimeAbiMajor = 2: Directly alias RealScript's 16-byte alignas(16) Value.
static_assert(
    sizeof(UnifiedValue) == 16,
    "UnifiedValue must remain exactly 16 bytes across C++ and RealScript ABI v2.0");
static_assert(
    alignof(UnifiedValue) == 16,
    "UnifiedValue must remain 16-byte aligned across C++ and RealScript ABI v2.0");
static_assert(
    std::is_trivially_copyable_v<Float4Value>,
    "Float4Value payload must be trivially copyable");

[[nodiscard]] inline Float4Value Color::ToFloat4() const noexcept {
    return Float4Value{r, g, b, a};
}

[[nodiscard]] inline Color Color::FromFloat4(const Float4Value& v) noexcept {
    return Color{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline Float4Value Point::ToFloat4() const noexcept {
    return Float4Value{x, y, 0.0f, 0.0f};
}

[[nodiscard]] inline Point Point::FromFloat4(const Float4Value& v) noexcept {
    return Point{v.x, v.y};
}

[[nodiscard]] inline Float4Value Rect::ToFloat4() const noexcept {
    return Float4Value{x, y, width, height};
}

[[nodiscard]] inline Rect Rect::FromFloat4(const Float4Value& v) noexcept {
    return Rect{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline Float4Value Thickness::ToFloat4() const noexcept {
    return Float4Value{left, top, right, bottom};
}

[[nodiscard]] inline Thickness Thickness::FromFloat4(const Float4Value& v) noexcept {
    return Thickness{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline Float4Value CornerRadius::ToFloat4() const noexcept {
    return Float4Value{topLeft, topRight, bottomRight, bottomLeft};
}

[[nodiscard]] inline CornerRadius CornerRadius::FromFloat4(const Float4Value& v) noexcept {
    return CornerRadius{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline UnifiedValue MakeFloat(float v) noexcept {
    return FloatValue{v};
}

// Rounds the 4th component w to top 24 bits of IEEE-754 float (sign + 8-bit exp + top 15-bit mantissa,
// clearing low 8 mantissa bits) so that Value::storeFloat4 100% hits ValueTag::Float4Inline (zero pool alloc,
// zero mutex lock, zero GC trace overhead) during high-frequency Color/CornerRadius/Thickness animations.
[[nodiscard]] inline UnifiedValue MakeFloat4InlineFast(Float4Value v) noexcept {
    std::uint32_t wBits = 0;
    std::memcpy(&wBits, &v.w, sizeof(wBits));
    if ((wBits & 0xFFu) != 0u) {
        if ((wBits & 0x7F800000u) != 0x7F800000u) {
            const std::uint32_t roundBias = 0x7Fu + ((wBits >> 8u) & 1u);
            wBits = (wBits + roundBias) & ~0xFFu;
            std::memcpy(&v.w, &wBits, sizeof(v.w));
        } else {
            wBits &= ~0xFFu;
            std::memcpy(&v.w, &wBits, sizeof(v.w));
        }
    }
    return UnifiedValue{v};
}

[[nodiscard]] inline UnifiedValue MakeFloat4InlineFast(float x, float y, float z, float w) noexcept {
    return MakeFloat4InlineFast(Float4Value{x, y, z, w});
}

[[nodiscard]] inline bool TryGetFloat(const UnifiedValue& v, float& out) noexcept {
    if (v.tryGetFloat(out)) {
        return true;
    }
    double d = 0.0;
    if (v.tryGetDouble(d)) {
        out = static_cast<float>(d);
        return true;
    }
    std::int64_t i = 0;
    if (v.tryGetInt64(i)) {
        out = static_cast<float>(i);
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

[[nodiscard]] inline UnifiedValue MakeFloat4InlineFast(Float4Value v) noexcept {
    return ::aero::MakeFloat4InlineFast(v);
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
    Float4Value f4{};
    if (v.tryGetQuatf(f4)) {
        return Color::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Point> TryGetPoint(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetQuatf(f4)) {
        return Point::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Rect> TryGetRect(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetQuatf(f4)) {
        return Rect::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Thickness> TryGetThickness(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetQuatf(f4)) {
        return Thickness::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<CornerRadius> TryGetCornerRadius(
    const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetQuatf(f4)) {
        return CornerRadius::FromFloat4(f4);
    }
    return std::nullopt;
}

} // namespace value

[[nodiscard]] inline bool UnifiedValueEquals(
    const UnifiedValue& a,
    const UnifiedValue& b,
    float floatEps = 1e-5f) noexcept {
    if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<std::monostate>(b)) {
        return std::holds_alternative<std::monostate>(a) && std::holds_alternative<std::monostate>(b);
    }
    if (std::holds_alternative<NullObject>(a) || std::holds_alternative<NullObject>(b)) {
        return std::holds_alternative<NullObject>(a) && std::holds_alternative<NullObject>(b);
    }
    if (std::holds_alternative<NullString>(a) || std::holds_alternative<NullString>(b)) {
        return std::holds_alternative<NullString>(a) && std::holds_alternative<NullString>(b);
    }
    if (std::holds_alternative<NullArray>(a) || std::holds_alternative<NullArray>(b)) {
        return std::holds_alternative<NullArray>(a) && std::holds_alternative<NullArray>(b);
    }
    if (const auto* ab = std::get_if<bool>(&a)) {
        if (const auto* bb = std::get_if<bool>(&b)) {
            return *ab == *bb;
        }
        return false;
    }
    if (std::holds_alternative<bool>(b)) {
        return false;
    }
    EnumValue ae{};
    EnumValue be{};
    if (a.tryGetEnum(ae) && b.tryGetEnum(be)) {
        return ae == be;
    }
    if (const auto* ai = std::get_if<std::int64_t>(&a)) {
        if (const auto* bi = std::get_if<std::int64_t>(&b)) {
            return *ai == *bi;
        }
        return false;
    }
    if (std::holds_alternative<std::int64_t>(b)) {
        return false;
    }
    NativeHandle ah{};
    NativeHandle bh{};
    if (a.tryGetNativeHandle(ah) && b.tryGetNativeHandle(bh)) {
        return ah == bh;
    }
    float af = 0.0f;
    float bf = 0.0f;
    if (value::TryGetFloat(a, af) && value::TryGetFloat(b, bf) &&
        !std::holds_alternative<bool>(a) && !std::holds_alternative<bool>(b) &&
        !std::holds_alternative<std::int64_t>(a) && !std::holds_alternative<std::int64_t>(b) &&
        !std::holds_alternative<EnumValue>(a) && !std::holds_alternative<EnumValue>(b)) {
        return std::fabs(af - bf) <= floatEps;
    }
    Float4Value af4{};
    Float4Value bf4{};
    if (a.tryGetQuatf(af4) && b.tryGetQuatf(bf4)) {
        return af4 == bf4;
    }
    if (const auto* as = std::get_if<StringValue>(&a)) {
        if (const auto* bs = std::get_if<StringValue>(&b)) {
            return as->view() == bs->view();
        }
        return false;
    }
    if (const auto* ao = std::get_if<ObjectRef>(&a)) {
        if (const auto* bo = std::get_if<ObjectRef>(&b)) {
            return *ao == *bo;
        }
        return false;
    }
    return false;
}

} // namespace aero
