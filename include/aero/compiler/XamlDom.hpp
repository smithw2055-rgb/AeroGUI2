#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

struct XamlTextSpan final {
    std::size_t startOffset = 0u;
    std::size_t endOffset = 0u;
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;

    [[nodiscard]] constexpr std::size_t Length() const noexcept {
        return endOffset >= startOffset ? (endOffset - startOffset) : 0u;
    }

    [[nodiscard]] constexpr bool ContainsOffset(std::size_t offset) const noexcept {
        return offset >= startOffset && offset <= endOffset;
    }

    [[nodiscard]] constexpr bool ContainsLineCol(int line, int column) const noexcept {
        if (line < startLine || line > endLine) {
            return false;
        }
        if (line == startLine && column < startColumn) {
            return false;
        }
        if (line == endLine && column > endColumn) {
            return false;
        }
        return true;
    }
};

struct XamlDomAttribute final {
    std::string rawName;
    std::string prefix;
    std::string localName;
    std::string value;
    std::string rawValueLiteral;
    char quoteChar = '"';
    std::size_t leadingTriviaOffset = 0u;
    XamlTextSpan fullSpan{};
    XamlTextSpan nameSpan{};
    XamlTextSpan valueInnerSpan{};
};

struct XamlDomComment final {
    XamlTextSpan span{};
    std::string text;
};

struct XamlDomElement final {
    std::int32_t id = -1;
    std::int32_t parentId = -1;
    std::string rawName;
    std::string prefix;
    std::string localName;
    bool isSelfClosing = false;
    std::size_t leadingLineIndentSpaces = 0u;
    std::size_t openTagAttrInsertOffset = 0u;
    std::size_t childrenInsertOffset = 0u;
    XamlTextSpan fullSpan{};
    XamlTextSpan openTagSpan{};
    XamlTextSpan closeTagSpan{};
    std::vector<XamlDomAttribute> attributes;
    std::vector<std::int32_t> children;
    std::string innerText;
};

struct XamlTextPatch final {
    std::size_t replaceStart = 0u;
    std::size_t replaceLength = 0u;
    std::string replacementText;
};

[[nodiscard]] std::string EncodeXmlAttributeValue(
    std::string_view value,
    char quoteChar = '"');

/// Lossless, span-preserving XAML CST/DOM for bidirectional visual UI designers.
/// Retains exact UTF-8 byte offsets, 1-based line/column spans, comments, attribute ordering,
/// and surrounding whitespace so visual canvas edits apply surgical text patches without
/// reformatting untouched XAML lines.
class XamlDomDocument final {
public:
    XamlDomDocument() = default;

    [[nodiscard]] static bool Parse(
        std::string_view sourceText,
        XamlDomDocument& outDoc,
        std::string* outError = nullptr);

    [[nodiscard]] const std::string& SourceText() const noexcept { return source_; }
    [[nodiscard]] std::int32_t RootElementId() const noexcept { return rootId_; }
    [[nodiscard]] std::size_t ElementCount() const noexcept { return elements_.size(); }
    [[nodiscard]] const std::vector<XamlDomElement>& Elements() const noexcept { return elements_; }
    [[nodiscard]] const std::vector<XamlDomComment>& Comments() const noexcept { return comments_; }

    [[nodiscard]] const XamlDomElement* GetElement(std::int32_t elementId) const noexcept;
    [[nodiscard]] std::int32_t FindElementAtOffset(std::size_t byteOffset) const noexcept;
    [[nodiscard]] std::int32_t FindElementAtLineCol(int line, int column) const noexcept;
    [[nodiscard]] std::int32_t FindElementByName(std::string_view xName) const noexcept;

    [[nodiscard]] const XamlDomAttribute* FindAttribute(
        std::int32_t elementId,
        std::string_view attrName) const noexcept;
    [[nodiscard]] std::optional<std::string_view> GetAttributeValue(
        std::int32_t elementId,
        std::string_view attrName) const noexcept;

    [[nodiscard]] std::optional<XamlTextPatch> BuildSetAttributePatch(
        std::int32_t elementId,
        std::string_view attrName,
        std::string_view newValue) const;

    [[nodiscard]] std::optional<XamlTextPatch> BuildRemoveAttributePatch(
        std::int32_t elementId,
        std::string_view attrName) const;

    [[nodiscard]] std::optional<XamlTextPatch> BuildInsertChildPatch(
        std::int32_t parentElementId,
        std::string_view childXamlSnippet,
        std::size_t indentStepSpaces = 4u) const;

    [[nodiscard]] std::optional<XamlTextPatch> BuildRemoveElementPatch(
        std::int32_t elementId) const;

    [[nodiscard]] static std::string ApplyPatches(
        std::string_view originalSource,
        std::vector<XamlTextPatch> patches);

    bool SetAttributeInPlace(
        std::int32_t elementId,
        std::string_view attrName,
        std::string_view newValue,
        std::string* outError = nullptr);

    bool RemoveAttributeInPlace(
        std::int32_t elementId,
        std::string_view attrName,
        std::string* outError = nullptr);

    bool InsertChildInPlace(
        std::int32_t parentElementId,
        std::string_view childXamlSnippet,
        std::size_t indentStepSpaces = 4u,
        std::string* outError = nullptr);

    bool RemoveElementInPlace(
        std::int32_t elementId,
        std::string* outError = nullptr);

private:
    std::string source_;
    std::int32_t rootId_ = -1;
    std::vector<XamlDomElement> elements_;
    std::vector<XamlDomComment> comments_;
};

} // namespace aero
