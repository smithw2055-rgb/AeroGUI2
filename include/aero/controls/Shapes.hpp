#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "aero/controls/Control.hpp"

namespace aero {

// ============================================================================
// Shape
// ============================================================================

class Shape : public UIElement {
    AERO_DECLARE_CLASS(Shape, UIElement)
public:
    Shape();
    ~Shape() override = default;

    virtual void SetFill(const Color& color);
    [[nodiscard]] virtual Color GetFill() const noexcept;
    [[nodiscard]] virtual bool HasFill() const noexcept;

    void SetFillGradientBrushId(std::uint32_t brushId);
    [[nodiscard]] std::uint32_t GetFillGradientBrushId() const noexcept;

    virtual void SetStroke(const Color& color);
    [[nodiscard]] virtual Color GetStroke() const noexcept;
    [[nodiscard]] virtual bool HasStroke() const noexcept;

    [[nodiscard]] std::uint32_t GetStrokeGradientBrushId() const noexcept;

    virtual void SetStrokeThickness(float thickness);
    [[nodiscard]] virtual float GetStrokeThickness() const noexcept;

    virtual void SetStretch(Stretch stretch);
    [[nodiscard]] virtual Stretch GetStretch() const noexcept;
};

// ============================================================================
// Rectangle
// ============================================================================

class Rectangle : public Shape {
    AERO_DECLARE_CLASS(Rectangle, Shape)
public:
    Rectangle();
    ~Rectangle() override = default;

    void SetRadiusX(float rx);
    [[nodiscard]] float GetRadiusX() const noexcept;

    void SetRadiusY(float ry);
    [[nodiscard]] float GetRadiusY() const noexcept;

    [[nodiscard]] float EffectiveCornerRadius() const noexcept;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool HasOwnOpaqueShadowSurfaceForRender() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
};

// ============================================================================
// Ellipse
// ============================================================================

class Ellipse : public Shape {
    AERO_DECLARE_CLASS(Ellipse, Shape)
public:
    Ellipse();
    ~Ellipse() override = default;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool HasOwnOpaqueShadowSurfaceForRender() const noexcept override {
        return !HasControlTemplate();
    }
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
};

// ============================================================================
// Line
// ============================================================================

class Line : public Shape {
    AERO_DECLARE_CLASS(Line, Shape)
public:
    Line();
    ~Line() override = default;

    void SetX1(float x1);
    [[nodiscard]] float GetX1() const noexcept;

    void SetY1(float y1);
    [[nodiscard]] float GetY1() const noexcept;

    void SetX2(float x2);
    [[nodiscard]] float GetX2() const noexcept;

    void SetY2(float y2);
    [[nodiscard]] float GetY2() const noexcept;

    [[nodiscard]] Color GetStroke() const noexcept override;
    [[nodiscard]] bool HasStroke() const noexcept override;
    [[nodiscard]] float GetStrokeThickness() const noexcept override;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
};

// ============================================================================
// Path
// ============================================================================

class Path : public Shape {
    AERO_DECLARE_CLASS(Path, Shape)
public:
    Path();
    ~Path() override = default;

    void SetData(std::string_view svgPathData);
    [[nodiscard]] std::string GetData() const;

    void SetFill(const Color& color) override;
    [[nodiscard]] bool HasExplicitFill() const noexcept;
    [[nodiscard]] Color GetFill() const noexcept override;

    void SetStroke(const Color& color) override;
    [[nodiscard]] bool HasExplicitStroke() const noexcept;
    [[nodiscard]] Color GetStroke() const noexcept override;

    void SetStrokeThickness(float thickness) override;
    [[nodiscard]] float GetStrokeThickness() const noexcept override;

    void SetStretch(Stretch stretch) override;
    [[nodiscard]] Stretch GetStretch() const noexcept override;

    void SetTrimStart(float trimStart);
    [[nodiscard]] float GetTrimStart() const noexcept;

    void SetTrimEnd(float trimEnd);
    [[nodiscard]] float GetTrimEnd() const noexcept;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool TryGetBackdropBlurVectorMask(
        std::string& outPathData,
        Stretch& outStretch) const override;
    bool EmitRenderDropShadowPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        float effOpacity,
        float sdx,
        float sdy,
        float localBlurR,
        const Color& shCol,
        const UIElement* shadowOwner,
        std::uint16_t& ioEmittedCount) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
};

// ============================================================================
// Polygon & Polyline
// ============================================================================

class Polygon : public Shape {
    AERO_DECLARE_CLASS(Polygon, Shape)
public:
    Polygon();
    ~Polygon() override = default;

    void SetPoints(std::string_view pointsText);
    [[nodiscard]] std::string GetPoints() const;

    void SetData(std::string_view svgPathData);
    [[nodiscard]] std::string GetData() const;

    [[nodiscard]] float GetStrokeThickness() const noexcept override;
    [[nodiscard]] Stretch GetStretch() const noexcept override;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool TryGetBackdropBlurVectorMask(
        std::string& outPathData,
        Stretch& outStretch) const override;
    bool EmitRenderDropShadowPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        float effOpacity,
        float sdx,
        float sdy,
        float localBlurR,
        const Color& shCol,
        const UIElement* shadowOwner,
        std::uint16_t& ioEmittedCount) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
};

class Polyline : public Shape {
    AERO_DECLARE_CLASS(Polyline, Shape)
public:
    Polyline();
    ~Polyline() override = default;

    void SetPoints(std::string_view pointsText);
    [[nodiscard]] std::string GetPoints() const;

    void SetData(std::string_view svgPathData);
    [[nodiscard]] std::string GetData() const;

    [[nodiscard]] float GetStrokeThickness() const noexcept override;
    [[nodiscard]] Stretch GetStretch() const noexcept override;

    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
    [[nodiscard]] bool TryGetBackdropBlurVectorMask(
        std::string& outPathData,
        Stretch& outStretch) const override;
    bool EmitRenderDropShadowPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        float effOpacity,
        float sdx,
        float sdy,
        float localBlurR,
        const Color& shCol,
        const UIElement* shadowOwner,
        std::uint16_t& ioEmittedCount) const override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
};

} // namespace aero
