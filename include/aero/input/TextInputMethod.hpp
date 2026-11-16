#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

class ITextInputMethodHost {
public:
    virtual ~ITextInputMethodHost() = default;

    virtual void FocusTextInput(UIElement* focusedElement, Rect caretScreenRect) = 0;
    virtual void BlurTextInput() = 0;
    virtual void OnCompositionStart() = 0;
    virtual void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) = 0;
    virtual void OnCompositionEnd(std::string_view committedText) = 0;
};

// Shared IME composition state & bounded committed-text history used by DesktopImeAdapter and WebImeAdapter.
class ImeCompositionController {
public:
    static constexpr std::size_t kMaxCommittedHistoryBytes = 4096u;

    static void AppendBoundedCommittedText(std::string& ioHistory, std::string_view committed) {
        if (committed.empty()) {
            return;
        }
        if (committed.size() >= kMaxCommittedHistoryBytes) {
            std::size_t start = committed.size() - kMaxCommittedHistoryBytes;
            while (start < committed.size() &&
                   (static_cast<std::uint8_t>(committed[start]) & 0xC0u) == 0x80u) {
                ++start;
            }
            ioHistory.assign(committed.substr(start));
            return;
        }
        if (ioHistory.size() + committed.size() > kMaxCommittedHistoryBytes) {
            std::size_t drop = (ioHistory.size() + committed.size()) - kMaxCommittedHistoryBytes;
            while (drop < ioHistory.size() &&
                   (static_cast<std::uint8_t>(ioHistory[drop]) & 0xC0u) == 0x80u) {
                ++drop;
            }
            ioHistory.erase(0u, drop);
        }
        ioHistory.append(committed);
    }

    void BeginFocus(UIElement* focusedElement) {
        if (focusedElement_ != focusedElement) {
            if (isComposing_) {
                if (focusedElement_ && focusedElement_->IsEditableTextInput()) {
                    focusedElement_->SetEditableText(compositionBaseText_);
                    focusedElement_->SetEditableCaretIndex(compositionBaseCaret_);
                }
            }
            isComposing_ = false;
            preeditText_.clear();
            committedText_.clear();
            caretOffset_ = 0u;
            if (focusedElement && focusedElement->IsEditableTextInput()) {
                compositionBaseText_ = focusedElement->GetEditableText();
                compositionBaseCaret_ = focusedElement->GetEditableCaretIndex();
            } else {
                compositionBaseText_.clear();
                compositionBaseCaret_ = 0u;
            }
        }
        focusedElement_ = focusedElement;
        imeFocused_ = (focusedElement != nullptr);
    }

    void EndBlur() {
        if (isComposing_) {
            if (focusedElement_ && focusedElement_->IsEditableTextInput()) {
                focusedElement_->SetEditableText(compositionBaseText_);
                focusedElement_->SetEditableCaretIndex(compositionBaseCaret_);
            }
        }
        focusedElement_ = nullptr;
        imeFocused_ = false;
        isComposing_ = false;
        caretOffset_ = 0u;
        preeditText_.clear();
        committedText_.clear();
        compositionBaseText_.clear();
        compositionBaseCaret_ = 0u;
    }

    bool StartComposition(Rect* outCaretScreenRect = nullptr) {
        isComposing_ = true;
        preeditText_.clear();
        caretOffset_ = 0u;
        if (focusedElement_ && focusedElement_->IsEditableTextInput()) {
            compositionBaseText_ = focusedElement_->GetEditableText();
            compositionBaseCaret_ = focusedElement_->GetEditableCaretIndex();
            if (outCaretScreenRect) {
                *outCaretScreenRect = focusedElement_->GetEditableCaretScreenRect();
            }
            return true;
        }
        return false;
    }

    bool BuildPreviewAndApply(
        std::string_view preeditText,
        std::uint32_t caretOffset,
        Rect* outPreUpdateCaretRect = nullptr,
        Rect* outPostUpdateCaretRect = nullptr) {
        if (focusedElement_ && focusedElement_->IsEditableTextInput()) {
            if (!isComposing_) {
                compositionBaseText_ = focusedElement_->GetEditableText();
                compositionBaseCaret_ = focusedElement_->GetEditableCaretIndex();
                if (outPreUpdateCaretRect) {
                    *outPreUpdateCaretRect = focusedElement_->GetEditableCaretScreenRect();
                }
            }
            isComposing_ = true;
            preeditText_.assign(preeditText);
            caretOffset_ = caretOffset;

            std::size_t bytePos = 0u;
            std::size_t cpCount = 0u;
            while (cpCount < compositionBaseCaret_ &&
                   AdvanceUtf8CodePoint(compositionBaseText_, bytePos)) {
                ++cpCount;
            }
            const std::string preview =
                compositionBaseText_.substr(0u, bytePos) + preeditText_ + compositionBaseText_.substr(bytePos);
            focusedElement_->SetEditableText(preview);
            if (outPostUpdateCaretRect) {
                *outPostUpdateCaretRect = focusedElement_->GetEditableCaretScreenRect();
            }
            return true;
        }

        isComposing_ = true;
        preeditText_.assign(preeditText);
        caretOffset_ = caretOffset;
        return false;
    }

    bool CommitComposition(
        std::string_view committedText,
        Rect* outCaretScreenRect = nullptr) {
        const bool wasComposing = isComposing_;
        isComposing_ = false;
        preeditText_.clear();
        AppendBoundedCommittedText(committedText_, committedText);
        caretOffset_ = 0u;

        if (focusedElement_ && focusedElement_->IsEditableTextInput()) {
            if (wasComposing) {
                focusedElement_->SetEditableText(compositionBaseText_);
                focusedElement_->SetEditableCaretIndex(compositionBaseCaret_);
            }
            (void)focusedElement_->InsertEditableText(committedText);
            compositionBaseText_ = focusedElement_->GetEditableText();
            compositionBaseCaret_ = focusedElement_->GetEditableCaretIndex();
            if (outCaretScreenRect) {
                *outCaretScreenRect = focusedElement_->GetEditableCaretScreenRect();
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool IsImeActive() const noexcept { return imeFocused_; }
    [[nodiscard]] bool IsComposing() const noexcept { return isComposing_; }
    [[nodiscard]] UIElement* FocusedElement() const noexcept { return focusedElement_; }
    [[nodiscard]] std::uint32_t CaretOffset() const noexcept { return caretOffset_; }
    [[nodiscard]] const std::string& PreeditString() const noexcept { return preeditText_; }
    [[nodiscard]] const std::string& CommittedString() const noexcept { return committedText_; }
    [[nodiscard]] const std::string& CompositionBaseText() const noexcept { return compositionBaseText_; }
    [[nodiscard]] std::size_t CompositionBaseCaret() const noexcept { return compositionBaseCaret_; }

private:
    [[nodiscard]] static bool AdvanceUtf8CodePoint(
        std::string_view utf8Text,
        std::size_t& ioCursor) noexcept {
        if (ioCursor >= utf8Text.size()) {
            return false;
        }
        const auto b0 = static_cast<std::uint8_t>(utf8Text[ioCursor]);
        if ((b0 & 0x80u) == 0u) {
            ioCursor += 1u;
            return true;
        }
        if ((b0 & 0xE0u) == 0xC0u && b0 >= 0xC2u && ioCursor + 1u < utf8Text.size()) {
            const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
            if ((b1 & 0xC0u) == 0x80u) {
                ioCursor += 2u;
                return true;
            }
        } else if ((b0 & 0xF0u) == 0xE0u && ioCursor + 2u < utf8Text.size()) {
            const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
            const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                const std::uint32_t cp =
                    (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12u) |
                    (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6u) |
                    (b2 & 0x3Fu);
                if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) {
                    ioCursor += 3u;
                    return true;
                }
            }
        } else if ((b0 & 0xF8u) == 0xF0u && b0 <= 0xF4u && ioCursor + 3u < utf8Text.size()) {
            const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
            const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
            const auto b3 = static_cast<std::uint8_t>(utf8Text[ioCursor + 3u]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                const std::uint32_t cp =
                    (static_cast<std::uint32_t>(b0 & 0x07u) << 18u) |
                    (static_cast<std::uint32_t>(b1 & 0x3Fu) << 12u) |
                    (static_cast<std::uint32_t>(b2 & 0x3Fu) << 6u) |
                    (b3 & 0x3Fu);
                if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                    ioCursor += 4u;
                    return true;
                }
            }
        }
        ioCursor += 1u;
        return true;
    }

    UIElement* focusedElement_ = nullptr;
    bool imeFocused_ = false;
    bool isComposing_ = false;
    std::uint32_t caretOffset_ = 0u;
    std::string preeditText_;
    std::string committedText_;
    std::string compositionBaseText_;
    std::size_t compositionBaseCaret_ = 0u;
};

} // namespace aero
