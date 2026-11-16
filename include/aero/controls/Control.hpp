#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "aero/core/DependencyProperty.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

// ============================================================================
// Control (base class for templated / chrome controls)
// ============================================================================

class Control : public UIElement {
    AERO_DECLARE_CLASS(Control, UIElement)
public:
    Control();
    ~Control() override = default;

    void SetBackground(const Color& c);
    [[nodiscard]] Color GetBackground() const noexcept;

    void SetBorderBrush(const Color& c);
    [[nodiscard]] Color GetBorderBrush() const noexcept;

    void SetBorderThickness(const Thickness& t);
    [[nodiscard]] Thickness GetBorderThickness() const noexcept;

    void SetCornerRadius(const CornerRadius& cr);
    [[nodiscard]] CornerRadius GetCornerRadius() const noexcept;

    void SetFontFamily(std::string_view fontFamily);
    [[nodiscard]] std::string GetFontFamily() const;

    void SetFontSize(float fontSize);
    [[nodiscard]] float GetFontSize() const noexcept;

    void SetForeground(const Color& c);
    [[nodiscard]] Color GetForeground() const noexcept;

    void SetHorizontalContentAlignment(HorizontalAlignment align);
    [[nodiscard]] HorizontalAlignment GetHorizontalContentAlignment() const noexcept;

    void SetVerticalContentAlignment(VerticalAlignment align);
    [[nodiscard]] VerticalAlignment GetVerticalContentAlignment() const noexcept;

    void SetIsTabStop(bool isTabStop) noexcept { isTabStop_ = isTabStop; }
    [[nodiscard]] bool IsTabStop() const noexcept { return isTabStop_; }

    void SetTabIndex(std::int32_t tabIndex);
    [[nodiscard]] std::int32_t GetTabIndex() const noexcept;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

    [[nodiscard]] bool SupportsImplicitChromeStyleProperties() const noexcept override { return true; }

private:
    bool isTabStop_ = true;
};

// ============================================================================
// Shared script/AOT click-handler binding (used by ButtonBase and MenuItem)
// ============================================================================

struct ScriptClickBinding {
    const Interpreter* interpreter = nullptr;
    rs_aot::ExecutionContext* aotContext = nullptr;
    ObjectRef targetObject{};
    SymbolId methodId = 0;

    void BindScript(
        const Interpreter* interp,
        ObjectRef targetObj,
        SymbolId clickMethodId) noexcept {
        interpreter = interp;
        aotContext = nullptr;
        targetObject = targetObj;
        methodId = clickMethodId;
    }

    void BindAot(
        rs_aot::ExecutionContext* aotCtx,
        ObjectRef targetObj,
        SymbolId clickMethodId) noexcept {
        interpreter = nullptr;
        aotContext = aotCtx;
        targetObject = targetObj;
        methodId = clickMethodId;
    }

    [[nodiscard]] bool Invoke(const UIElement& owner, RuntimeError* error) const;
};

// ============================================================================
// ContentControl
// ============================================================================

class ContentControl : public Control {
    AERO_DECLARE_CLASS(ContentControl, Control)
public:
    ContentControl();
    ~ContentControl() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return false; }
    [[nodiscard]] virtual bool HostsDirectContentTemplateChild() const noexcept { return true; }

    void SetContentText(std::string_view text);
    [[nodiscard]] std::string GetContentText() const;

    void SyncContentTemplateChild();
    void SyncTemplateContentHost() override { SyncContentTemplateChild(); }
    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void MarkExtraValuesInPools() const noexcept override;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    IntrusivePtr<UIElement> contentTemplateChild_;
    std::string activeContentTemplateKey_;
    ObjectRef activeContentTemplateObj_{};
    bool hasActiveContentTemplateObj_ = false;
    bool syncingContentTemplate_ = false;
};

// ============================================================================
// HeaderedContentControl, GroupBox, Expander
// ============================================================================

class HeaderedContentControl : public ContentControl {
    AERO_DECLARE_CLASS(HeaderedContentControl, ContentControl)
public:
    static constexpr float kDefaultHeaderBarHeight = 26.0f;

    HeaderedContentControl();
    ~HeaderedContentControl() override = default;

    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return false; }

    void SetHeader(std::string_view text);
    [[nodiscard]] std::string GetHeader() const;

    void SetHeaderElement(UIElement* element);
    [[nodiscard]] UIElement* GetHeaderElement() const noexcept;
    [[nodiscard]] UIElement* GetRawHeaderElement() const noexcept { return headerElement_.Get(); }
    [[nodiscard]] bool HasHeader() const noexcept;

    [[nodiscard]] const UIElement* GetCompositeHeaderElement() const noexcept override {
        return GetHeaderElement();
    }
    [[nodiscard]] UIElement* GetLogicalHeaderElementForTemplate() const noexcept override {
        return GetRawHeaderElement();
    }

    [[nodiscard]] Rect HeaderLocalRect() const noexcept { return headerLocalRect_; }
    [[nodiscard]] Rect ContentLocalRect() const noexcept { return contentLocalRect_; }

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
    void ArrangeCore(Rect finalRect) noexcept override;

protected:
    Rect headerLocalRect_{};
    Rect contentLocalRect_{};
    IntrusivePtr<UIElement> headerElement_;
};

class GroupBox : public HeaderedContentControl {
    AERO_DECLARE_CLASS(GroupBox, HeaderedContentControl)
public:
    GroupBox();
    ~GroupBox() override = default;

    [[nodiscard]] Rect BodyBorderLocalRect() const noexcept { return bodyBorderLocalRect_; }

    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    Rect bodyBorderLocalRect_{};
};

class Expander : public HeaderedContentControl {
    AERO_DECLARE_CLASS(Expander, HeaderedContentControl)
public:
    using ExpandedChangedCallback = std::function<void(Expander&, bool)>;
    static constexpr float kDefaultExpanderHeaderHeight = 28.0f;

    Expander();
    ~Expander() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetIsExpanded(bool expanded);
    [[nodiscard]] bool IsExpanded() const noexcept;
    void ToggleExpanded();

    void SetExpandDirection(ExpandDirection dir);
    [[nodiscard]] ExpandDirection GetExpandDirection() const noexcept;

    [[nodiscard]] bool IsBodyChildHidden(const UIElement* child) const noexcept;
    [[nodiscard]] bool IsChildHiddenByLayout(const UIElement* child) const noexcept override {
        return IsBodyChildHidden(child);
    }

    [[nodiscard]] bool RequiresFullRenderRebuildOnDirty() const noexcept override { return true; }
    [[nodiscard]] bool ShouldEmitRenderChild(
        const UIElement* child,
        bool& outCollapseSubtree,
        const UIElement*& outFallbackHeaderOnly) const noexcept override;
    bool EmitRenderVisualPrimitives(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount) const override;

    void SetExpandedChangedCallback(ExpandedChangedCallback cb) {
        expandedChangedCallback_ = std::move(cb);
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    virtual bool ActivateFromInput(RuntimeError* error = nullptr);
    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    Size MeasureCore(Size availableSize) noexcept override;
    void ArrangeCore(Rect finalRect) noexcept override;

private:
    ExpandedChangedCallback expandedChangedCallback_;
};

// ============================================================================
// Thumb & Separator
// ============================================================================

class Thumb : public Control {
    AERO_DECLARE_CLASS(Thumb, Control)
public:
    Thumb();
    ~Thumb() override = default;

    [[nodiscard]] bool IsInteractiveControl() const noexcept override { return true; }
};

class Separator : public Control {
    AERO_DECLARE_CLASS(Separator, Control)
public:
    Separator();
    ~Separator() override = default;

    Size MeasureCore(Size availableSize) noexcept override;
};

} // namespace aero
