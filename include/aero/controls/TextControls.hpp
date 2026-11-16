#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "aero/controls/Control.hpp"

namespace aero {

[[nodiscard]] std::string StripAndResolveRichTextBbCode(
    std::string_view rawBbCode,
    const UIElement* owner = nullptr);

// ============================================================================
// TextBlock
// ============================================================================

class TextBlock : public UIElement {
    AERO_DECLARE_CLASS(TextBlock, UIElement)
public:
    TextBlock();
    ~TextBlock() override = default;

    [[nodiscard]] bool ParticipatesInTemplateTextMetrics() const noexcept override {
        return true;
    }

    void SetText(std::string_view text);
    [[nodiscard]] bool HasRichText() const noexcept;
    [[nodiscard]] std::string GetRawRichText() const;
    [[nodiscard]] std::string GetText() const;

    void SetFontSize(float fontSize);
    [[nodiscard]] float GetFontSize() const noexcept;

    void SetFontFamily(std::string_view fontFamily);
    [[nodiscard]] std::string GetFontFamily() const;

    void SetTextWrapping(TextWrapping wrapping);
    [[nodiscard]] TextWrapping GetTextWrapping() const noexcept;

    void SetForeground(const Color& color);
    [[nodiscard]] Color GetForeground() const noexcept;

    void SetTextAlignment(TextAlignment alignment);
    [[nodiscard]] TextAlignment GetTextAlignment() const noexcept;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool TryGetRichTextDisplayString(std::string& outText) const override;
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
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

// ============================================================================
// Label
// ============================================================================

class Label : public ContentControl {
    AERO_DECLARE_CLASS(Label, ContentControl)
public:
    Label();
    ~Label() override = default;

    [[nodiscard]] bool HostsDirectContentTemplateChild() const noexcept override { return false; }
    [[nodiscard]] bool ParticipatesInTemplateTextMetrics() const noexcept override {
        return true;
    }

    void SetText(std::string_view text);
    [[nodiscard]] bool HasRichText() const noexcept;
    [[nodiscard]] std::string GetRawRichText() const;
    [[nodiscard]] std::string GetText() const;

    void SetTextWrapping(TextWrapping wrapping);
    [[nodiscard]] TextWrapping GetTextWrapping() const noexcept;

    void SetTextAlignment(TextAlignment alignment);
    [[nodiscard]] TextAlignment GetTextAlignment() const noexcept;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool TryGetRichTextDisplayString(std::string& outText) const override;
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
    [[nodiscard]] RenderInPlaceResult TryUpdateRenderElementInPlace(
        PagedRenderTree& tree,
        const PagedRenderTraversalState& curState,
        Rect r,
        float boxW,
        float boxH,
        float effOpacity) const override;
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

// ============================================================================
// TextBoxBase, TextBox, PasswordBox
// ============================================================================

class TextBoxBase : public Control {
    AERO_DECLARE_CLASS(TextBoxBase, Control)
public:
    TextBoxBase();
    ~TextBoxBase() override = default;

    [[nodiscard]] bool DefaultFocusable() const noexcept override { return true; }
    [[nodiscard]] bool ParticipatesInTemplateTextMetrics() const noexcept override {
        return true;
    }

    void SetText(std::string_view text);
    [[nodiscard]] std::string GetText() const;

    void SetTextWrapping(TextWrapping wrapping);
    [[nodiscard]] TextWrapping GetTextWrapping() const noexcept;

    void SetTextAlignment(TextAlignment alignment);
    [[nodiscard]] TextAlignment GetTextAlignment() const noexcept;

    void SetPlaceholder(std::string_view placeholder);
    [[nodiscard]] std::string GetPlaceholder() const;

    void SetIsReadOnly(bool readOnly);
    [[nodiscard]] bool IsReadOnly() const noexcept;

    void SetMaxLength(std::int32_t maxLen);
    [[nodiscard]] std::int32_t GetMaxLength() const noexcept;

    static void SetClipboardText(std::string_view utf8Text);
    [[nodiscard]] static std::string GetClipboardText();

    [[nodiscard]] bool HasAllSelected() const noexcept { return hasAllSelected_; }

    void SetSelectionStart(std::size_t codePointIndex);
    [[nodiscard]] std::size_t GetSelectionStart() const noexcept;

    void SetSelectionLength(std::size_t codePointLength);
    [[nodiscard]] std::size_t GetSelectionLength() const noexcept;

    void Select(std::size_t startCodePoint, std::size_t lengthCodePoints);
    [[nodiscard]] std::string GetSelectedText() const;
    void SetSelectedText(std::string_view replacement);

    bool Copy() const;
    bool Cut();
    bool Paste();

    void SetCaretIndex(std::size_t codePointIndex);
    [[nodiscard]] std::size_t GetCaretIndex() const noexcept;

    void SetIsPassword(bool isPassword);
    [[nodiscard]] bool IsPassword() const noexcept;

    void SetAcceptsReturn(bool accepts);
    [[nodiscard]] bool AcceptsReturn() const noexcept;

    [[nodiscard]] std::int32_t GetMaxLines() const noexcept;

    void Clear();
    void SelectAll();

    [[nodiscard]] virtual std::string GetDisplayString() const;
    [[nodiscard]] float HorizontalTextScrollOffset() const noexcept;
    [[nodiscard]] Rect CaretLocalRect() const noexcept;
    [[nodiscard]] Rect CaretScreenRect() const noexcept;

    void PopulateAriaInfo(AriaElementInfo& ioInfo) const override;
    [[nodiscard]] bool IsEditableTextInput() const noexcept override { return true; }
    [[nodiscard]] bool AcceptsActiveImeInput() const noexcept override {
        return !IsReadOnly() && !IsPassword();
    }
    [[nodiscard]] std::string GetEditableText() const override { return GetText(); }
    void SetEditableText(std::string text) override { SetText(text); }
    [[nodiscard]] std::size_t GetEditableCaretIndex() const noexcept override {
        return GetCaretIndex();
    }
    void SetEditableCaretIndex(std::size_t idx) noexcept override { SetCaretIndex(idx); }
    [[nodiscard]] Rect GetEditableCaretScreenRect() const noexcept override {
        return CaretScreenRect();
    }
    bool InsertEditableText(std::string_view utf8Text) override { return InsertText(utf8Text); }
    void SelectAllEditableText() override { SelectAll(); }
    [[nodiscard]] bool TryGetRenderGlyphScanText(std::string& outText) const override;
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

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;
    void OnFocusChanged(bool focused) override;

    bool CommitEditedText(std::string newText, std::size_t newCaretCodePoint);
    bool InsertText(std::string_view utf8Text);
    bool ReplaceAllTextInteractive(std::string_view utf8Text);
    bool Backspace();
    bool DeleteForward();
    bool MoveCaretLeft();
    bool MoveCaretRight();
    bool MoveCaretHome();
    bool MoveCaretEnd();

    bool OnPointerDown(
        Point localPoint,
        PointerButton button,
        std::uint8_t modifiers,
        bool& outRequestCapture) override;
    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override;
    bool OnTextInput(std::string_view utf8Text) override;
    Size MeasureCore(Size availableSize) noexcept override;

protected:
    virtual void OnEditedTextCommitted(const std::string& newText);

    std::size_t selectionAnchorCp_ = 0u;
    bool hasAllSelected_ = false;
};

class TextBox : public TextBoxBase {
    AERO_DECLARE_CLASS(TextBox, TextBoxBase)
public:
    using TextChangedCallback = std::function<void(TextBox&, const std::string&)>;

    TextBox();
    ~TextBox() override = default;

    void SetTextChangedCallback(TextChangedCallback cb) {
        textChangedCallback_ = std::move(cb);
    }

protected:
    void OnEditedTextCommitted(const std::string& newText) override;

private:
    TextChangedCallback textChangedCallback_;
};

class PasswordBox : public TextBoxBase {
    AERO_DECLARE_CLASS(PasswordBox, TextBoxBase)
public:
    using PasswordChangedCallback = std::function<void(PasswordBox&, const std::string&)>;

    PasswordBox();
    ~PasswordBox() override = default;

    void SetPassword(std::string_view password);
    [[nodiscard]] std::string GetPassword() const { return GetText(); }

    void SetPasswordChar(std::string_view ch);
    [[nodiscard]] std::string GetPasswordChar() const;

    void SetPasswordChangedCallback(PasswordChangedCallback cb) {
        passwordChangedCallback_ = std::move(cb);
    }

    void OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) override;

protected:
    void OnEditedTextCommitted(const std::string& newText) override;

private:
    PasswordChangedCallback passwordChangedCallback_;
    std::string lastNotifiedPassword_;
};

} // namespace aero
