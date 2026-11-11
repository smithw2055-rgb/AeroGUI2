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

// Parameters for Noesis/WPF CompositeTransform3D & PlaneProjection (pure 32-bit float)
struct CompositeTransform3DParams {
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float translateZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;
    float cameraDistance = 1000.0f;
    bool hasExplicitCenter = false;

    [[nodiscard]] inline bool IsIdentity(float eps = 1e-5f) const noexcept {
        return std::fabs(rotationX) <= eps &&
               std::fabs(rotationY) <= eps &&
               std::fabs(rotationZ) <= eps &&
               std::fabs(translateX) <= eps &&
               std::fabs(translateY) <= eps &&
               std::fabs(translateZ) <= eps &&
               std::fabs(scaleX - 1.0f) <= eps &&
               std::fabs(scaleY - 1.0f) <= eps &&
               std::fabs(scaleZ - 1.0f) <= eps;
    }
};

// Lightweight 32-bit float 4x4 homogeneous 3D & perspective projection matrix
// in column-vector convention: [X_h, Y_h, Z_h, W_h]^T = M * [x, y, z, 1]^T.
struct Matrix4x4 {
    float m[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}};

    constexpr bool operator==(const Matrix4x4& other) const noexcept {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                if (m[r][c] != other.m[r][c]) {
                    return false;
                }
            }
        }
        return true;
    }
    constexpr bool operator!=(const Matrix4x4& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] static constexpr Matrix4x4 Identity() noexcept {
        return Matrix4x4{};
    }

    [[nodiscard]] inline bool IsIdentity(float eps = 1e-6f) const noexcept {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const float expected = (r == c) ? 1.0f : 0.0f;
                if (std::fabs(m[r][c] - expected) > eps) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] inline bool HasPerspective(float eps = 1e-6f) const noexcept {
        return std::fabs(m[3][0]) > eps ||
               std::fabs(m[3][1]) > eps ||
               std::fabs(m[3][2]) > eps ||
               std::fabs(m[3][3] - 1.0f) > eps;
    }

    [[nodiscard]] static constexpr Matrix4x4 FromMatrix2D(const Matrix2D& m2d) noexcept {
        Matrix4x4 res{};
        res.m[0][0] = m2d.m00;
        res.m[0][1] = m2d.m01;
        res.m[0][3] = m2d.tx;
        res.m[1][0] = m2d.m10;
        res.m[1][1] = m2d.m11;
        res.m[1][3] = m2d.ty;
        return res;
    }

    [[nodiscard]] static constexpr Matrix4x4 Translation(float tx, float ty, float tz) noexcept {
        Matrix4x4 res{};
        res.m[0][3] = tx;
        res.m[1][3] = ty;
        res.m[2][3] = tz;
        return res;
    }

    [[nodiscard]] static constexpr Matrix4x4 Scaling(float sx, float sy, float sz) noexcept {
        Matrix4x4 res{};
        res.m[0][0] = sx;
        res.m[1][1] = sy;
        res.m[2][2] = sz;
        return res;
    }

    [[nodiscard]] static inline Matrix4x4 RotationX(float angleDeg) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = angleDeg * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        Matrix4x4 res{};
        res.m[1][1] = c;
        res.m[1][2] = -s;
        res.m[2][1] = s;
        res.m[2][2] = c;
        return res;
    }

    [[nodiscard]] static inline Matrix4x4 RotationY(float angleDeg) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = angleDeg * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        Matrix4x4 res{};
        res.m[0][0] = c;
        res.m[0][2] = s;
        res.m[2][0] = -s;
        res.m[2][2] = c;
        return res;
    }

    [[nodiscard]] static inline Matrix4x4 RotationZ(float angleDeg) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = angleDeg * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        Matrix4x4 res{};
        res.m[0][0] = c;
        res.m[0][1] = -s;
        res.m[1][0] = s;
        res.m[1][1] = c;
        return res;
    }

    // Perspective projection onto z=0 plane with camera eye at (cameraCenterX, cameraCenterY, +cameraDistance):
    // W' = 1 - z / d, X'/W' = cameraCenterX + (x - cameraCenterX) / (1 - z / d)
    [[nodiscard]] static inline Matrix4x4 PerspectiveCamera(
        float cameraDistance = 1000.0f,
        float cameraCenterX = 0.0f,
        float cameraCenterY = 0.0f) noexcept {
        const float d = std::max(cameraDistance, 10.0f);
        const float invD = 1.0f / d;
        Matrix4x4 res{};
        res.m[0][2] = -cameraCenterX * invD;
        res.m[1][2] = -cameraCenterY * invD;
        res.m[3][2] = -invD;
        return res;
    }

    [[nodiscard]] static constexpr Matrix4x4 Multiply(
        const Matrix4x4& after,
        const Matrix4x4& before) noexcept {
        Matrix4x4 out{};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out.m[r][c] =
                    after.m[r][0] * before.m[0][c] +
                    after.m[r][1] * before.m[1][c] +
                    after.m[r][2] * before.m[2][c] +
                    after.m[r][3] * before.m[3][c];
            }
        }
        return out;
    }

    // Composes two planar (z=0 -> projected 2D) homographies H(after) * H(before)
    [[nodiscard]] static constexpr Matrix4x4 ComposePlanarHomography(
        const Matrix4x4& after,
        const Matrix4x4& before) noexcept {
        constexpr int kIdx[3] = {0, 1, 3};
        Matrix4x4 out{};
        for (int ri = 0; ri < 3; ++ri) {
            const int r = kIdx[ri];
            for (int ci = 0; ci < 3; ++ci) {
                const int c = kIdx[ci];
                out.m[r][c] =
                    after.m[r][0] * before.m[0][c] +
                    after.m[r][1] * before.m[1][c] +
                    after.m[r][3] * before.m[3][c];
            }
        }
        return out;
    }

    [[nodiscard]] static inline Matrix4x4 FromCompositeTransform3D(
        const CompositeTransform3DParams& p,
        float pivotX,
        float pivotY,
        float pivotZ = 0.0f,
        float cameraCenterX = 0.0f,
        float cameraCenterY = 0.0f) noexcept {
        Matrix4x4 mat = Translation(-pivotX, -pivotY, -pivotZ);
        if (std::fabs(p.scaleX - 1.0f) > 1e-6f ||
            std::fabs(p.scaleY - 1.0f) > 1e-6f ||
            std::fabs(p.scaleZ - 1.0f) > 1e-6f) {
            mat = Multiply(Scaling(p.scaleX, p.scaleY, p.scaleZ), mat);
        }
        if (std::fabs(p.rotationX) > 1e-6f) {
            mat = Multiply(RotationX(p.rotationX), mat);
        }
        if (std::fabs(p.rotationY) > 1e-6f) {
            mat = Multiply(RotationY(p.rotationY), mat);
        }
        if (std::fabs(p.rotationZ) > 1e-6f) {
            mat = Multiply(RotationZ(p.rotationZ), mat);
        }
        mat = Multiply(
            Translation(pivotX + p.translateX, pivotY + p.translateY, pivotZ + p.translateZ),
            mat);
        const float camDist = (p.cameraDistance > 10.0f) ? p.cameraDistance : 1000.0f;
        mat = Multiply(PerspectiveCamera(camDist, cameraCenterX, cameraCenterY), mat);
        return mat;
    }

    [[nodiscard]] inline Point ProjectPoint2D(Point p, float* outW = nullptr) const noexcept {
        const float xh = m[0][0] * p.x + m[0][1] * p.y + m[0][3];
        const float yh = m[1][0] * p.x + m[1][1] * p.y + m[1][3];
        const float wh = m[3][0] * p.x + m[3][1] * p.y + m[3][3];
        if (outW) {
            *outW = wh;
        }
        const float safeW = (std::fabs(wh) > 1e-5f) ? wh : (wh >= 0.0f ? 1e-5f : -1e-5f);
        const float invW = 1.0f / safeW;
        return Point{xh * invW, yh * invW};
    }

    [[nodiscard]] inline Rect TransformRectBounds(const Rect& r) const noexcept {
        const Point p0 = ProjectPoint2D(Point{r.x, r.y});
        const Point p1 = ProjectPoint2D(Point{r.x + r.width, r.y});
        const Point p2 = ProjectPoint2D(Point{r.x + r.width, r.y + r.height});
        const Point p3 = ProjectPoint2D(Point{r.x, r.y + r.height});
        const float minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return Rect{minX, minY, maxX - minX, maxY - minY};
    }

    // Exact ray-plane (z=0) inverse homography mapping screenPt -> localPt on the 3D element plane
    [[nodiscard]] inline bool InverseProjectScreenToLocalPlane(
        Point screenPt,
        Point& outLocalPt) const noexcept {
        const float h00 = m[0][0];
        const float h01 = m[0][1];
        const float h02 = m[0][3];
        const float h10 = m[1][0];
        const float h11 = m[1][1];
        const float h12 = m[1][3];
        const float h20 = m[3][0];
        const float h21 = m[3][1];
        const float h22 = m[3][3];

        const float c00 = h11 * h22 - h12 * h21;
        const float c01 = h12 * h20 - h10 * h22;
        const float c02 = h10 * h21 - h11 * h20;
        const float det = h00 * c00 + h01 * c01 + h02 * c02;
        if (std::fabs(det) <= 1e-10f) {
            return false;
        }

        const float inv00 = c00;
        const float inv01 = h02 * h21 - h01 * h22;
        const float inv02 = h01 * h12 - h02 * h11;

        const float inv10 = c01;
        const float inv11 = h00 * h22 - h02 * h20;
        const float inv12 = h02 * h10 - h00 * h12;

        const float inv20 = c02;
        const float inv21 = h01 * h20 - h00 * h21;
        const float inv22 = h00 * h11 - h01 * h10;

        const float uH = inv00 * screenPt.x + inv01 * screenPt.y + inv02;
        const float vH = inv10 * screenPt.x + inv11 * screenPt.y + inv12;
        const float wH = inv20 * screenPt.x + inv21 * screenPt.y + inv22;
        if (std::fabs(wH) <= 1e-8f) {
            return false;
        }

        const float invW = 1.0f / wH;
        const float lx = uH * invW;
        const float ly = vH * invW;

        const float forwardClipW = h20 * lx + h21 * ly + h22;
        if (forwardClipW <= 1e-4f) {
            return false;
        }

        outLocalPt = Point{lx, ly};
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
    if (v.tryGetFloat4(f4)) {
        return Color::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Point> TryGetPoint(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetFloat4(f4)) {
        return Point::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Rect> TryGetRect(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetFloat4(f4)) {
        return Rect::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Thickness> TryGetThickness(const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetFloat4(f4)) {
        return Thickness::FromFloat4(f4);
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<CornerRadius> TryGetCornerRadius(
    const UnifiedValue& v) noexcept {
    Float4Value f4{};
    if (v.tryGetFloat4(f4)) {
        return CornerRadius::FromFloat4(f4);
    }
    return std::nullopt;
}

} // namespace value

} // namespace aero
