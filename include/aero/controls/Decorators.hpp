#pragma once

#include <string>
#include <string_view>

#include "aero/controls/Control.hpp"

namespace aero {

struct AxbPackage;
class AnimationEngine;
class DualTrackBindingEngine;
class ExecutionDomainContext;

// ============================================================================
// Decorator
// ============================================================================

class Decorator : public UIElement {
    AERO_DECLARE_CLASS(Decorator, UIElement)
public:
    Decorator();
    ~Decorator() override = default;

    void SetChild(
        UIElement* child,
        NativeHandleRegistry* registry = nullptr,
        ManagedHeap* uiHeap = nullptr);
    [[nodiscard]] UIElement* GetChild() const noexcept { return FirstChild(); }

    void SetBackground(const Color& color);
    [[nodiscard]] Color GetBackground() const noexcept;

    void SetBorderBrush(const Color& color);
    [[nodiscard]] Color GetBorderBrush() const noexcept;

    void SetBorderThickness(const Thickness& thickness);
    [[nodiscard]] Thickness GetBorderThickness() const noexcept;

    void SetCornerRadius(const CornerRadius& radius);
    [[nodiscard]] CornerRadius GetCornerRadius() const noexcept;

    [[nodiscard]] bool SupportsImplicitChromeStyleProperties() const noexcept override { return true; }

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// Border
// ============================================================================

class Border : public Decorator {
    AERO_DECLARE_CLASS(Border, Decorator)
public:
    Border();
    ~Border() override = default;
};

// ============================================================================
// BoxShadow
// ============================================================================

class BoxShadow : public Decorator {
    AERO_DECLARE_CLASS(BoxShadow, Decorator)
public:
    BoxShadow();
    ~BoxShadow() override = default;

    [[nodiscard]] bool SuppressImplicitBlurEffectKind() const noexcept override { return true; }
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
};

// ============================================================================
// Viewbox & LayoutScaler
// ============================================================================

class Viewbox : public Decorator {
    AERO_DECLARE_CLASS(Viewbox, Decorator)
public:
    Viewbox();
    ~Viewbox() override = default;

    void SetStretch(Stretch stretch);
    [[nodiscard]] Stretch GetStretch() const noexcept;

    void SetStretchDirection(StretchDirection dir);
    [[nodiscard]] StretchDirection GetStretchDirection() const noexcept;

    [[nodiscard]] float ChildScaleX() const noexcept { return childScaleX_; }
    [[nodiscard]] float ChildScaleY() const noexcept { return childScaleY_; }
    [[nodiscard]] float ChildOriginX() const noexcept { return childOriginX_; }
    [[nodiscard]] float ChildOriginY() const noexcept { return childOriginY_; }

    [[nodiscard]] Matrix2D GetChildContainerTransform2D() const noexcept override {
        return Matrix2D::Scaling(childScaleX_, childScaleY_, childOriginX_, childOriginY_);
    }

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

protected:
    float childScaleX_ = 1.0f;
    float childScaleY_ = 1.0f;
    float childOriginX_ = 0.0f;
    float childOriginY_ = 0.0f;
};

class LayoutScaler : public Viewbox {
    AERO_DECLARE_CLASS(LayoutScaler, Viewbox)
public:
    LayoutScaler();
    ~LayoutScaler() override = default;

    void SetRefResolution(Size refRes);
    [[nodiscard]] Size GetRefResolution() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// UserControl
// ============================================================================

class UserControl : public ContentControl {
    AERO_DECLARE_CLASS(UserControl, ContentControl)
public:
    UserControl();
    explicit UserControl(
        std::string_view className,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = ObjectRef{},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);
    ~UserControl() override = default;

    [[nodiscard]] const std::string& ControlClassName() const noexcept { return controlClassName_; }
    [[nodiscard]] bool IsTemplateMounted() const noexcept { return templateMounted_; }
    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return !templateMounted_; }
    [[nodiscard]] bool IsSubtreeNamescopeBoundary() const noexcept override { return true; }

    [[nodiscard]] bool MatchesXamlAncestorTypeAlias(std::string_view wantedType) const noexcept override;
    [[nodiscard]] bool MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept override;

    bool MountTemplate(
        const AxbPackage& package,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = ObjectRef{},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);

private:
    std::string controlClassName_;
    bool templateMounted_ = false;
};

} // namespace aero
