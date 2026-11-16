#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aero/controls/Control.hpp"

namespace aero {

struct AxbPackage;
class AnimationEngine;
class DualTrackBindingEngine;
class ExecutionDomainContext;

enum class GridUnitType : std::uint8_t {
    Auto = 0,
    Pixel = 1,
    Star = 2
};

enum class Dock : std::uint8_t {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3
};

enum class ScrollBarVisibility : std::uint8_t {
    Disabled = 0,
    Auto = 1,
    Hidden = 2,
    Visible = 3
};

struct RowDefinition {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
    float computedHeight = 0.0f;
    float offsetY = 0.0f;
};

struct ColumnDefinition {
    GridUnitType unit = GridUnitType::Star;
    float value = 1.0f;
    float computedWidth = 0.0f;
    float offsetX = 0.0f;
};

// ============================================================================
// Panel
// ============================================================================

class Panel : public UIElement {
    AERO_DECLARE_CLASS(Panel, UIElement)
public:
    Panel();
    ~Panel() override = default;
};

// ============================================================================
// StackPanel
// ============================================================================

class StackPanel : public Panel {
    AERO_DECLARE_CLASS(StackPanel, Panel)
public:
    StackPanel();
    ~StackPanel() override = default;

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetSpacing(float spacing);
    [[nodiscard]] float GetSpacing() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// DockPanel
// ============================================================================

class DockPanel : public Panel {
    AERO_DECLARE_CLASS(DockPanel, Panel)
public:
    DockPanel();
    ~DockPanel() override = default;

    static void SetDock(UIElement* element, Dock dock);
    [[nodiscard]] static Dock GetDock(const UIElement* element) noexcept;

    void SetLastChildFill(bool fill);
    [[nodiscard]] bool GetLastChildFill() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// WrapPanel
// ============================================================================

class WrapPanel : public Panel {
    AERO_DECLARE_CLASS(WrapPanel, Panel)
public:
    WrapPanel();
    ~WrapPanel() override = default;

    void SetOrientation(Orientation orientation);
    [[nodiscard]] Orientation GetOrientation() const noexcept;

    void SetItemWidth(float width);
    [[nodiscard]] float GetItemWidth() const noexcept;

    void SetItemHeight(float height);
    [[nodiscard]] float GetItemHeight() const noexcept;

    void SetSpacing(float spacing);
    [[nodiscard]] float GetSpacing() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// Grid
// ============================================================================

class Grid : public Panel {
    AERO_DECLARE_CLASS(Grid, Panel)
public:
    Grid();
    ~Grid() override = default;

    void AddRowDefinition(GridUnitType unit, float value = 1.0f);
    void AddColumnDefinition(GridUnitType unit, float value = 1.0f);
    void ClearDefinitions() noexcept {
        if (!rows_.empty() || !cols_.empty()) {
            rows_.clear();
            cols_.clear();
            InvalidateLayout();
        }
    }

    static void SetRow(UIElement* element, std::int32_t row);
    [[nodiscard]] static std::int32_t GetRow(const UIElement* element) noexcept;

    static void SetColumn(UIElement* element, std::int32_t col);
    [[nodiscard]] static std::int32_t GetColumn(const UIElement* element) noexcept;

    static void SetRowSpan(UIElement* element, std::int32_t span);
    [[nodiscard]] static std::int32_t GetRowSpan(const UIElement* element) noexcept;

    static void SetColumnSpan(UIElement* element, std::int32_t span);
    [[nodiscard]] static std::int32_t GetColumnSpan(const UIElement* element) noexcept;

    [[nodiscard]] std::size_t RowDefinitionCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t ColumnDefinitionCount() const noexcept { return cols_.size(); }
    [[nodiscard]] const std::vector<RowDefinition>& Rows() const noexcept { return rows_; }
    [[nodiscard]] const std::vector<ColumnDefinition>& Columns() const noexcept { return cols_; }

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    std::vector<RowDefinition> rows_;
    std::vector<ColumnDefinition> cols_;
};

// ============================================================================
// Canvas
// ============================================================================

class Canvas : public Panel {
    AERO_DECLARE_CLASS(Canvas, Panel)
public:
    Canvas();
    ~Canvas() override = default;

    static void SetLeft(UIElement* element, float left);
    [[nodiscard]] static float GetLeft(const UIElement* element) noexcept;

    static void SetTop(UIElement* element, float top);
    [[nodiscard]] static float GetTop(const UIElement* element) noexcept;

    static void SetRight(UIElement* element, float right);
    [[nodiscard]] static float GetRight(const UIElement* element) noexcept;

    static void SetBottom(UIElement* element, float bottom);
    [[nodiscard]] static float GetBottom(const UIElement* element) noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

// ============================================================================
// UniformGrid
// ============================================================================

class UniformGrid : public Panel {
    AERO_DECLARE_CLASS(UniformGrid, Panel)
public:
    UniformGrid();
    ~UniformGrid() override = default;

    void SetRows(std::int32_t rows);
    [[nodiscard]] std::int32_t GetRows() const noexcept;

    void SetColumns(std::int32_t cols);
    [[nodiscard]] std::int32_t GetColumns() const noexcept;

    void SetFirstColumn(std::int32_t firstCol);
    [[nodiscard]] std::int32_t GetFirstColumn() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    void ComputeEffectiveGridDimensions(
        std::size_t visibleCount,
        std::int32_t& outRows,
        std::int32_t& outCols,
        std::int32_t& outFirstCol) const noexcept;
};

// ============================================================================
// ScrollViewer
// ============================================================================

class ScrollViewer : public ContentControl {
    AERO_DECLARE_CLASS(ScrollViewer, ContentControl)
public:
    ScrollViewer();
    ~ScrollViewer() override = default;

    void SetHorizontalScrollBarVisibility(ScrollBarVisibility vis);
    [[nodiscard]] ScrollBarVisibility GetHorizontalScrollBarVisibility() const noexcept;

    void SetVerticalScrollBarVisibility(ScrollBarVisibility vis);
    [[nodiscard]] ScrollBarVisibility GetVerticalScrollBarVisibility() const noexcept;

    void SetHorizontalOffset(float offset);
    [[nodiscard]] float GetHorizontalOffset() const noexcept;

    void SetVerticalOffset(float offset);
    [[nodiscard]] float GetVerticalOffset() const noexcept;

    void ScrollToHorizontalOffset(float offset);
    void ScrollToVerticalOffset(float offset);
    void ScrollBy(float deltaX, float deltaY);

    [[nodiscard]] float ExtentWidth() const noexcept { return extentSize_.width; }
    [[nodiscard]] float ExtentHeight() const noexcept { return extentSize_.height; }
    [[nodiscard]] float ViewportWidth() const noexcept { return viewportSize_.width; }
    [[nodiscard]] float ViewportHeight() const noexcept { return viewportSize_.height; }
    [[nodiscard]] float ScrollableWidth() const noexcept {
        return extentSize_.width > viewportSize_.width ? (extentSize_.width - viewportSize_.width) : 0.0f;
    }
    [[nodiscard]] float ScrollableHeight() const noexcept {
        return extentSize_.height > viewportSize_.height ? (extentSize_.height - viewportSize_.height) : 0.0f;
    }

    [[nodiscard]] bool ShouldSkipControlTemplatePackage(
        const AxbPackage& package) const noexcept override;

    bool TryHandleMouseWheelScroll(float scrollDeltaX, float scrollDeltaY, bool shiftHeld) override;
    [[nodiscard]] bool HandlesBubbledScrollKeys() const noexcept override { return true; }

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return false; }
    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return false; }
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

    [[nodiscard]] Rect GetLocalClipBounds() const noexcept override {
        const Rect r = FinalRect();
        const Thickness p = GetPadding();
        const float padW = p.left + p.right;
        const float padH = p.top + p.bottom;
        return Rect{
            r.x + p.left,
            r.y + p.top,
            (r.width > padW) ? (r.width - padW) : 0.0f,
            (r.height > padH) ? (r.height - padH) : 0.0f};
    }

private:
    Size extentSize_{};
    Size viewportSize_{};
    bool hasValidViewport_ = false;
};

// ============================================================================
// Popup, ToolTip, Window
// ============================================================================

class Popup : public Control {
    AERO_DECLARE_CLASS(Popup, Control)
public:
    Popup();
    ~Popup() override = default;

    void SetIsOpen(bool open);
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsOpenForHitTest() const noexcept override { return IsOpen(); }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class ToolTip : public ContentControl {
    AERO_DECLARE_CLASS(ToolTip, ContentControl)
public:
    ToolTip();
    ~ToolTip() override = default;

    void SetIsOpen(bool open);
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsOpenForHitTest() const noexcept override { return IsOpen(); }
    [[nodiscard]] bool IsToolTipOverlayElement() const noexcept override { return true; }
    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return false; }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;
};

class Window : public ContentControl {
    AERO_DECLARE_CLASS(Window, ContentControl)
public:
    Window();
    explicit Window(
        std::string_view className,
        ExecutionDomainContext* domains = nullptr,
        DualTrackBindingEngine* bindingEngine = nullptr,
        ObjectRef dataContext = ObjectRef{},
        const Interpreter* eventInterpreter = nullptr,
        rs_aot::ExecutionContext* eventAotContext = nullptr,
        AnimationEngine* animationEngine = nullptr);
    ~Window() override = default;

    [[nodiscard]] const std::string& WindowClassName() const noexcept { return windowClassName_; }
    [[nodiscard]] bool IsTemplateMounted() const noexcept { return templateMounted_; }
    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return !templateMounted_; }

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
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
    std::string windowClassName_;
    bool templateMounted_ = false;
};

} // namespace aero
