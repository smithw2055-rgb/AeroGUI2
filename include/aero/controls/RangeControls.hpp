#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/controls/Control.hpp"

namespace aero {

struct AxbPackage;

// ============================================================================
// RangeBase
// ============================================================================

class RangeBase : public Control {
    AERO_DECLARE_CLASS(RangeBase, Control)
public:
    RangeBase();
    ~RangeBase() override = default;

    void SetMinimum(float minVal);
    [[nodiscard]] float GetMinimum() const noexcept;

    void SetMaximum(float maxVal);
    [[nodiscard]] float GetMaximum() const noexcept;

    void SetRangeValue(float val);
    [[nodiscard]] float GetRangeValue() const noexcept;
    [[nodiscard]] float ClampValueToRange(float rawVal) const noexcept;

    void SetSmallChange(float step);
    [[nodiscard]] float GetSmallChange() const noexcept;

    void SetLargeChange(float step);
    [[nodiscard]] float GetLargeChange() const noexcept;

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;

protected:
    virtual void OnValueChanged(float oldVal, float newVal);

    float lastNotifiedValue_ = 0.0f;
};

// ============================================================================
// Slider
// ============================================================================

class Slider : public RangeBase {
    AERO_DECLARE_CLASS(Slider, RangeBase)
public:
    using ValueChangedCallback = std::function<void(Slider&, float)>;

    Slider();
    ~Slider() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetSliderValue(float val);
    [[nodiscard]] float GetSliderValue() const noexcept;
    bool SetSliderValueInteractive(float val);

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetIsDirectionReversed(bool reversed);
    [[nodiscard]] bool IsDirectionReversed() const noexcept;

    void SetIsSnapToTickEnabled(bool snap);
    [[nodiscard]] bool IsSnapToTickEnabled() const noexcept;

    void SetTickFrequency(float freq);
    [[nodiscard]] float GetTickFrequency() const noexcept;

    void SetTickPlacement(std::int32_t placement);
    [[nodiscard]] std::int32_t GetTickPlacement() const noexcept;

    void SetTicks(std::string_view ticksSpec);
    [[nodiscard]] std::string GetTicks() const;
    void CollectNormalizedTicks(std::vector<float>& outTicks) const;

    void SetIsMoveToPointEnabled(bool enabled);
    [[nodiscard]] bool IsMoveToPointEnabled() const noexcept;

    [[nodiscard]] float SnapValueToTicks(float rawVal) const noexcept;
    [[nodiscard]] float NormalizedPosition() const noexcept;
    [[nodiscard]] float VisualNormalizedPosition() const noexcept;
    [[nodiscard]] bool IsDraggingThumb() const noexcept { return isDragging_; }

    void SetValueChangedCallback(ValueChangedCallback cb) {
        valueChangedCallback_ = std::move(cb);
    }

    void SyncTemplateParts() noexcept;
    void OnTemplatePropertiesUpdated(std::uint16_t changedPropId) noexcept override {
        if (changedPropId == 0xFFFFu ||
            changedPropId == PropId_Value ||
            changedPropId == PropId_Minimum ||
            changedPropId == PropId_Maximum) {
            SyncTemplateParts();
        }
    }
    [[nodiscard]] bool ShouldSkipControlTemplatePackage(
        const AxbPackage& package) const noexcept override;

    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnPointerMove(Point localPoint, std::uint8_t modifiers) override;
    bool OnPointerUp(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool insideBounds) override;
    void OnPointerCaptureLost() noexcept override;
    void OnEnabledStateChanged(bool effectivelyEnabled) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
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

private:
    bool UpdateValueFromLocalPoint(Point localPt);

    ValueChangedCallback valueChangedCallback_;
    bool isDragging_ = false;
};

// ============================================================================
// ProgressBar
// ============================================================================

class ProgressBar : public RangeBase {
    AERO_DECLARE_CLASS(ProgressBar, RangeBase)
public:
    ProgressBar();
    ~ProgressBar() override = default;

    void SetProgressValue(float val);
    [[nodiscard]] float GetProgressValue() const noexcept;

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetIsIndeterminate(bool indeterminate);
    [[nodiscard]] bool IsIndeterminate() const noexcept;

    [[nodiscard]] float NormalizedProgress() const noexcept;
    [[nodiscard]] Rect ComputeFillBarLocalRect() const noexcept;

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
// GridSplitter
// ============================================================================

class GridSplitter : public Thumb {
    AERO_DECLARE_CLASS(GridSplitter, Thumb)
public:
    GridSplitter();
    ~GridSplitter() override = default;

    Size MeasureCore(Size availableSize) noexcept override;
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnPointerMove(Point localPoint, std::uint8_t modifiers) override;
    bool OnPointerUp(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool insideBounds) override;

private:
    bool dragging_ = false;
    Point dragStartScreen_{};
};

} // namespace aero
