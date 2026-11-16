#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/controls/Control.hpp"

namespace aero {

[[nodiscard]] bool QueryRoutedCommandCanExecuteBubble(
    const UIElement* source,
    std::string_view commandName,
    const UnifiedValue& parameter,
    bool hasParameter,
    bool& outHandled);

bool TryExecuteRoutedCommandBubble(
    UIElement* source,
    std::string_view commandName,
    const UnifiedValue& parameter = {},
    bool hasParameter = false);

// ============================================================================
// ButtonBase
// ============================================================================

class ButtonBase : public ContentControl {
    AERO_DECLARE_CLASS(ButtonBase, ContentControl)
public:
    using ClickCallback = std::function<void(ButtonBase&)>;

    ButtonBase();
    ~ButtonBase() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }

    void SetClickCallback(ClickCallback callback) { clickCallback_ = std::move(callback); }
    void BindScriptClickHandler(
        const Interpreter* interp,
        ObjectRef targetObject,
        SymbolId methodId) noexcept {
        clickBinding_.BindScript(interp, targetObject, methodId);
    }
    void BindAotClickHandler(
        rs_aot::ExecutionContext* aotCtx,
        ObjectRef targetObject,
        SymbolId methodId) noexcept {
        clickBinding_.BindAot(aotCtx, targetObject, methodId);
    }

    bool PerformClick(RuntimeError* error = nullptr);
    virtual bool ActivateFromInput(RuntimeError* error = nullptr);
    [[nodiscard]] std::uint32_t ClickGeneration() const noexcept { return clickGeneration_; }

    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnPointerUp(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool insideBounds) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    Size MeasureCore(Size availableSize) noexcept override;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool TracksPressedWithinBoundsDuringCapture() const noexcept override {
        return true;
    }
    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return false; }
    [[nodiscard]] bool MatchesBindingAncestorLegacyFallback(std::string_view targetType) const noexcept override;
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
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

protected:
    ClickCallback clickCallback_;
    ScriptClickBinding clickBinding_;
    std::uint32_t clickGeneration_ = 0u;
};

// ============================================================================
// ToggleButton, CheckBox, RadioButton
// ============================================================================

class ToggleButton : public ButtonBase {
    AERO_DECLARE_CLASS(ToggleButton, ButtonBase)
public:
    ToggleButton();
    ~ToggleButton() override = default;

    virtual void SetIsChecked(bool checked);
    [[nodiscard]] bool IsChecked() const noexcept;

    void SetIsThreeState(bool threeState);
    [[nodiscard]] bool IsThreeState() const noexcept;

    virtual void Toggle();
    bool ActivateFromInput(RuntimeError* error = nullptr) override;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
};

class CheckBox : public ToggleButton {
    AERO_DECLARE_CLASS(CheckBox, ToggleButton)
public:
    using CheckedChangedCallback = std::function<void(CheckBox&, bool)>;

    CheckBox();
    ~CheckBox() override = default;

    void Toggle() override;

    void SetCheckedChangedCallback(CheckedChangedCallback cb) {
        checkedChangedCallback_ = std::move(cb);
        extraCheckedCallbacks_.clear();
    }
    void AddCheckedChangedCallback(CheckedChangedCallback cb) {
        if (!checkedChangedCallback_) {
            checkedChangedCallback_ = std::move(cb);
        } else {
            extraCheckedCallbacks_.push_back(std::move(cb));
        }
    }

    bool ActivateFromInput(RuntimeError* error = nullptr) override;
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
    CheckedChangedCallback checkedChangedCallback_;
    std::vector<CheckedChangedCallback> extraCheckedCallbacks_;
};

class RadioButton : public ToggleButton {
    AERO_DECLARE_CLASS(RadioButton, ToggleButton)
public:
    using CheckedChangedCallback = std::function<void(RadioButton&, bool)>;

    RadioButton();
    ~RadioButton() override = default;

    void SetIsChecked(bool checked) override;

    void SetGroupName(std::string_view groupName);
    [[nodiscard]] std::string GetGroupName() const;

    void SetCheckedChangedCallback(CheckedChangedCallback cb) {
        checkedChangedCallback_ = std::move(cb);
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    bool ActivateFromInput(RuntimeError* error = nullptr) override;
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
    void UncheckMutualExclusionPeers(bool pushTwoWayOnUncheckedPeers);

    CheckedChangedCallback checkedChangedCallback_;
    bool uncheckingPeers_ = false;
};

// ============================================================================
// Button, RepeatButton, Hyperlink
// ============================================================================

class Button : public ButtonBase {
    AERO_DECLARE_CLASS(Button, ButtonBase)
public:
    Button();
    ~Button() override = default;
};

class RepeatButton : public ButtonBase {
    AERO_DECLARE_CLASS(RepeatButton, ButtonBase)
public:
    RepeatButton();
    ~RepeatButton() override = default;

    bool ActivateFromInput(RuntimeError* error = nullptr) override;
    bool OnPointerMove(Point localPoint, std::uint8_t modifiers) override;

    [[nodiscard]] bool SupportsAutoRepeatPress() const noexcept override { return true; }
    bool ActivateAutoRepeatFromInput() override { return ActivateFromInput(); }
};

class Hyperlink : public ButtonBase {
    AERO_DECLARE_CLASS(Hyperlink, ButtonBase)
public:
    using RequestNavigateCallback = std::function<void(Hyperlink&, const std::string&)>;

    Hyperlink();
    ~Hyperlink() override = default;

    void SetNavigateUri(std::string_view uri);
    [[nodiscard]] std::string GetNavigateUri() const;

    void SetRequestNavigateCallback(RequestNavigateCallback cb) {
        requestNavigateCallback_ = std::move(cb);
    }

    bool ActivateFromInput(RuntimeError* error = nullptr) override;

private:
    RequestNavigateCallback requestNavigateCallback_;
};

} // namespace aero
