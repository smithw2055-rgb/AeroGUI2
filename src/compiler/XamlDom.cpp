#include "aero/compiler/XamlDom.hpp"

#include <algorithm>
#include <cctype>

#include "aero/compiler/XmlPullLexer.hpp"

namespace aero {
namespace {

[[nodiscard]] bool IsSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] bool IsNameStart(unsigned char c) noexcept {
    return std::isalpha(c) != 0 || c == '_' || c == ':' || c >= 0x80u;
}

[[nodiscard]] bool IsNameChar(unsigned char c) noexcept {
    return IsNameStart(c) || std::isdigit(c) != 0 || c == '-' || c == '.';
}

void SplitQName(std::string_view raw, std::string& outPrefix, std::string& outLocal) {
    const std::size_t colon = raw.find(':');
    if (colon == std::string_view::npos) {
        outPrefix.clear();
        outLocal.assign(raw);
    } else {
        outPrefix.assign(raw.substr(0u, colon));
        outLocal.assign(raw.substr(colon + 1u));
    }
}

struct SourceCursor final {
    std::string_view text;
    std::size_t pos = 0u;
    int line = 1;
    int col = 1;

    explicit SourceCursor(std::string_view src) noexcept : text(src) {
        if (text.size() >= 3u &&
            static_cast<unsigned char>(text[0]) == 0xEFu &&
            static_cast<unsigned char>(text[1]) == 0xBBu &&
            static_cast<unsigned char>(text[2]) == 0xBFu) {
            pos = 3u;
        }
    }

    [[nodiscard]] bool AtEnd() const noexcept { return pos >= text.size(); }
    [[nodiscard]] char Peek(std::size_t off = 0u) const noexcept {
        return (pos + off < text.size()) ? text[pos + off] : '\0';
    }
    [[nodiscard]] bool StartsWith(std::string_view pfx) const noexcept {
        return pos + pfx.size() <= text.size() && text.substr(pos, pfx.size()) == pfx;
    }

    char Advance() noexcept {
        if (pos >= text.size()) {
            return '\0';
        }
        const char c = text[pos++];
        if (c == '\r') {
            if (pos < text.size() && text[pos] == '\n') {
                ++pos;
            }
            ++line;
            col = 1;
            return '\n';
        }
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }

    void SkipSpaces() noexcept {
        while (!AtEnd() && IsSpace(Peek())) {
            (void)Advance();
        }
    }
};

[[nodiscard]] std::size_t MeasureLeadingIndentSpaces(std::string_view src, std::size_t tagOffset) noexcept {
    std::size_t lineStart = tagOffset;
    while (lineStart > 0u && src[lineStart - 1u] != '\n' && src[lineStart - 1u] != '\r') {
        --lineStart;
    }
    std::size_t spaces = 0u;
    for (std::size_t i = lineStart; i < tagOffset; ++i) {
        if (src[i] == ' ') {
            ++spaces;
        } else if (src[i] == '\t') {
            spaces += 4u;
        } else {
            break;
        }
    }
    return spaces;
}

[[nodiscard]] std::string DetectNewline(std::string_view src) {
    const std::size_t nl = src.find('\n');
    if (nl != std::string_view::npos && nl > 0u && src[nl - 1u] == '\r') {
        return "\r\n";
    }
    return "\n";
}

} // namespace

std::string EncodeXmlAttributeValue(std::string_view value, char quoteChar) {
    std::string out;
    out.reserve(value.size() + 8u);
    for (char c : value) {
        switch (c) {
            case '&': out.append("&amp;"); break;
            case '<': out.append("&lt;"); break;
            case '>': out.append("&gt;"); break;
            case '"':
                if (quoteChar == '"') out.append("&quot;");
                else out.push_back(c);
                break;
            case '\'':
                if (quoteChar == '\'') out.append("&apos;");
                else out.push_back(c);
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

bool XamlDomDocument::Parse(
    std::string_view sourceText,
    XamlDomDocument& outDoc,
    std::string* outError) {
    outDoc = XamlDomDocument{};
    outDoc.source_.assign(sourceText);
    const std::string_view src(outDoc.source_);

    SourceCursor cur(src);
    std::vector<std::int32_t> stack;

    auto setErr = [&](int ln, int cl, std::string msg) -> bool {
        if (outError != nullptr) {
            *outError = "Line " + std::to_string(ln) + ":" + std::to_string(cl) + ": " + std::move(msg);
        }
        return false;
    };

    while (!cur.AtEnd()) {
        if (cur.Peek() != '<') {
            const std::size_t textStart = cur.pos;
            while (!cur.AtEnd() && cur.Peek() != '<') {
                (void)cur.Advance();
            }
            if (!stack.empty()) {
                const std::string_view rawTxt = src.substr(textStart, cur.pos - textStart);
                bool allWs = true;
                for (char ch : rawTxt) {
                    if (!IsSpace(ch)) {
                        allWs = false;
                        break;
                    }
                }
                if (!allWs) {
                    std::string entityErr;
                    outDoc.elements_[static_cast<std::size_t>(stack.back())].innerText.append(
                        DecodeXmlEntities(rawTxt, &entityErr));
                }
            }
            continue;
        }

        const std::size_t tagStartOff = cur.pos;
        const int tagStartLine = cur.line;
        const int tagStartCol = cur.col;

        // Comment <!-- ... -->
        if (cur.StartsWith("<!--")) {
            cur.Advance(); cur.Advance(); cur.Advance(); cur.Advance();
            const std::size_t bodyStart = cur.pos;
            bool closed = false;
            while (!cur.AtEnd()) {
                if (cur.StartsWith("-->")) {
                    const std::size_t bodyEnd = cur.pos;
                    cur.Advance(); cur.Advance(); cur.Advance();
                    XamlDomComment cmt{};
                    cmt.span = XamlTextSpan{tagStartOff, cur.pos, tagStartLine, tagStartCol, cur.line, cur.col};
                    cmt.text.assign(src.substr(bodyStart, bodyEnd - bodyStart));
                    outDoc.comments_.push_back(std::move(cmt));
                    closed = true;
                    break;
                }
                (void)cur.Advance();
            }
            if (!closed) {
                return setErr(tagStartLine, tagStartCol, "Unterminated XML comment");
            }
            continue;
        }

        // CDATA <![CDATA[ ... ]]>
        if (cur.StartsWith("<![CDATA[")) {
            for (int k = 0; k < 9; ++k) (void)cur.Advance();
            const std::size_t bodyStart = cur.pos;
            bool closed = false;
            while (!cur.AtEnd()) {
                if (cur.StartsWith("]]>")) {
                    if (!stack.empty()) {
                        outDoc.elements_[static_cast<std::size_t>(stack.back())].innerText.append(
                            src.substr(bodyStart, cur.pos - bodyStart));
                    }
                    cur.Advance(); cur.Advance(); cur.Advance();
                    closed = true;
                    break;
                }
                (void)cur.Advance();
            }
            if (!closed) {
                return setErr(tagStartLine, tagStartCol, "Unterminated CDATA section");
            }
            continue;
        }

        // Processing instruction <? ... ?> or <!DOCTYPE ...>
        if (cur.Peek(1u) == '?' || cur.Peek(1u) == '!') {
            const bool isPi = (cur.Peek(1u) == '?');
            cur.Advance();
            cur.Advance();
            bool closed = false;
            while (!cur.AtEnd()) {
                if (isPi && cur.StartsWith("?>")) {
                    cur.Advance();
                    cur.Advance();
                    closed = true;
                    break;
                }
                if (!isPi && cur.Peek() == '>') {
                    cur.Advance();
                    closed = true;
                    break;
                }
                (void)cur.Advance();
            }
            if (!closed) {
                return setErr(tagStartLine, tagStartCol, "Unterminated XML declaration or directive");
            }
            continue;
        }

        // Closing tag </Name>
        if (cur.Peek(1u) == '/') {
            cur.Advance(); // '<'
            cur.Advance(); // '/'
            cur.SkipSpaces();
            const std::size_t closeNameStart = cur.pos;
            while (!cur.AtEnd() && IsNameChar(static_cast<unsigned char>(cur.Peek()))) {
                (void)cur.Advance();
            }
            const std::string_view closeName = src.substr(closeNameStart, cur.pos - closeNameStart);
            cur.SkipSpaces();
            if (cur.Peek() != '>') {
                return setErr(tagStartLine, tagStartCol, "Expected '>' in closing tag");
            }
            cur.Advance();

            if (stack.empty()) {
                return setErr(tagStartLine, tagStartCol, "Unexpected closing tag '</" + std::string(closeName) + ">'");
            }
            const std::int32_t openId = stack.back();
            XamlDomElement& elem = outDoc.elements_[static_cast<std::size_t>(openId)];
            if (elem.rawName != closeName) {
                return setErr(
                    tagStartLine,
                    tagStartCol,
                    "Mismatched closing tag '</" + std::string(closeName) + ">' for '<" + elem.rawName + ">'");
            }
            elem.childrenInsertOffset = tagStartOff;
            elem.closeTagSpan = XamlTextSpan{tagStartOff, cur.pos, tagStartLine, tagStartCol, cur.line, cur.col};
            elem.fullSpan.endOffset = cur.pos;
            elem.fullSpan.endLine = cur.line;
            elem.fullSpan.endColumn = cur.col;
            stack.pop_back();
            continue;
        }

        // Opening tag <Name ...> or <Name ... />
        cur.Advance(); // '<'
        if (cur.AtEnd() || !IsNameStart(static_cast<unsigned char>(cur.Peek()))) {
            return setErr(tagStartLine, tagStartCol, "Expected element tag name after '<'");
        }
        const std::size_t nameStart = cur.pos;
        while (!cur.AtEnd() && IsNameChar(static_cast<unsigned char>(cur.Peek()))) {
            (void)cur.Advance();
        }
        const std::string_view rawTagName = src.substr(nameStart, cur.pos - nameStart);

        const auto elemId = static_cast<std::int32_t>(outDoc.elements_.size());
        XamlDomElement elem{};
        elem.id = elemId;
        elem.parentId = stack.empty() ? -1 : stack.back();
        elem.rawName.assign(rawTagName);
        SplitQName(rawTagName, elem.prefix, elem.localName);
        elem.leadingLineIndentSpaces = MeasureLeadingIndentSpaces(src, tagStartOff);
        elem.fullSpan.startOffset = tagStartOff;
        elem.fullSpan.startLine = tagStartLine;
        elem.fullSpan.startColumn = tagStartCol;
        elem.openTagSpan.startOffset = tagStartOff;
        elem.openTagSpan.startLine = tagStartLine;
        elem.openTagSpan.startColumn = tagStartCol;

        // Parse attributes until '>' or '/>'
        while (!cur.AtEnd()) {
            const std::size_t triviaStart = cur.pos;
            cur.SkipSpaces();
            if (cur.AtEnd()) {
                return setErr(tagStartLine, tagStartCol, "Unterminated opening tag '<" + elem.rawName + ">'");
            }
            if (cur.StartsWith("/>")) {
                elem.isSelfClosing = true;
                elem.openTagAttrInsertOffset = triviaStart;
                cur.Advance();
                cur.Advance();
                break;
            }
            if (cur.Peek() == '>') {
                elem.isSelfClosing = false;
                elem.openTagAttrInsertOffset = cur.pos;
                cur.Advance();
                break;
            }

            if (!IsNameStart(static_cast<unsigned char>(cur.Peek()))) {
                return setErr(cur.line, cur.col, "Invalid attribute name in '<" + elem.rawName + ">'");
            }

            XamlDomAttribute attr{};
            attr.leadingTriviaOffset = triviaStart;
            attr.fullSpan.startOffset = cur.pos;
            attr.fullSpan.startLine = cur.line;
            attr.fullSpan.startColumn = cur.col;
            attr.nameSpan.startOffset = cur.pos;
            attr.nameSpan.startLine = cur.line;
            attr.nameSpan.startColumn = cur.col;

            while (!cur.AtEnd() && IsNameChar(static_cast<unsigned char>(cur.Peek()))) {
                (void)cur.Advance();
            }
            attr.nameSpan.endOffset = cur.pos;
            attr.nameSpan.endLine = cur.line;
            attr.nameSpan.endColumn = cur.col;

            const std::string_view attrRawName = src.substr(
                attr.nameSpan.startOffset,
                attr.nameSpan.endOffset - attr.nameSpan.startOffset);
            attr.rawName.assign(attrRawName);
            SplitQName(attrRawName, attr.prefix, attr.localName);

            cur.SkipSpaces();
            if (cur.Peek() != '=') {
                return setErr(cur.line, cur.col, "Expected '=' after attribute '" + attr.rawName + "'");
            }
            cur.Advance(); // '='
            cur.SkipSpaces();

            const char q = cur.Peek();
            if (q != '"' && q != '\'') {
                return setErr(cur.line, cur.col, "Expected quoted value for attribute '" + attr.rawName + "'");
            }
            attr.quoteChar = q;
            cur.Advance(); // opening quote

            attr.valueInnerSpan.startOffset = cur.pos;
            attr.valueInnerSpan.startLine = cur.line;
            attr.valueInnerSpan.startColumn = cur.col;

            while (!cur.AtEnd() && cur.Peek() != q) {
                (void)cur.Advance();
            }
            if (cur.AtEnd()) {
                return setErr(
                    attr.valueInnerSpan.startLine,
                    attr.valueInnerSpan.startColumn,
                    "Unterminated string literal for attribute '" + attr.rawName + "'");
            }
            attr.valueInnerSpan.endOffset = cur.pos;
            attr.valueInnerSpan.endLine = cur.line;
            attr.valueInnerSpan.endColumn = cur.col;

            attr.rawValueLiteral.assign(src.substr(
                attr.valueInnerSpan.startOffset,
                attr.valueInnerSpan.endOffset - attr.valueInnerSpan.startOffset));
            std::string entityErr;
            attr.value = DecodeXmlEntities(attr.rawValueLiteral, &entityErr);

            cur.Advance(); // closing quote
            attr.fullSpan.endOffset = cur.pos;
            attr.fullSpan.endLine = cur.line;
            attr.fullSpan.endColumn = cur.col;

            elem.attributes.push_back(std::move(attr));
        }

        elem.openTagSpan.endOffset = cur.pos;
        elem.openTagSpan.endLine = cur.line;
        elem.openTagSpan.endColumn = cur.col;
        elem.fullSpan.endOffset = cur.pos;
        elem.fullSpan.endLine = cur.line;
        elem.fullSpan.endColumn = cur.col;
        elem.childrenInsertOffset = cur.pos;

        if (elem.parentId >= 0) {
            outDoc.elements_[static_cast<std::size_t>(elem.parentId)].children.push_back(elemId);
        } else if (outDoc.rootId_ < 0) {
            outDoc.rootId_ = elemId;
        } else {
            return setErr(tagStartLine, tagStartCol, "Multiple root elements in XAML document");
        }

        const bool selfClosing = elem.isSelfClosing;
        outDoc.elements_.push_back(std::move(elem));
        if (!selfClosing) {
            stack.push_back(elemId);
        }
    }

    if (!stack.empty()) {
        const auto& unclosed = outDoc.elements_[static_cast<std::size_t>(stack.back())];
        return setErr(
            unclosed.openTagSpan.startLine,
            unclosed.openTagSpan.startColumn,
            "Unclosed element '<" + unclosed.rawName + ">'");
    }
    if (outDoc.rootId_ < 0) {
        return setErr(1, 1, "Empty XAML document: no root element found");
    }
    return true;
}

const XamlDomElement* XamlDomDocument::GetElement(std::int32_t elementId) const noexcept {
    if (elementId < 0 || static_cast<std::size_t>(elementId) >= elements_.size()) {
        return nullptr;
    }
    return &elements_[static_cast<std::size_t>(elementId)];
}

std::int32_t XamlDomDocument::FindElementAtOffset(std::size_t byteOffset) const noexcept {
    std::int32_t bestId = -1;
    std::size_t bestLen = static_cast<std::size_t>(-1);
    for (const XamlDomElement& el : elements_) {
        if (el.fullSpan.ContainsOffset(byteOffset)) {
            const std::size_t len = el.fullSpan.Length();
            if (len <= bestLen) {
                bestLen = len;
                bestId = el.id;
            }
        }
    }
    return bestId;
}

std::int32_t XamlDomDocument::FindElementAtLineCol(int line, int column) const noexcept {
    std::int32_t bestId = -1;
    std::size_t bestLen = static_cast<std::size_t>(-1);
    for (const XamlDomElement& el : elements_) {
        if (el.fullSpan.ContainsLineCol(line, column)) {
            const std::size_t len = el.fullSpan.Length();
            if (len <= bestLen) {
                bestLen = len;
                bestId = el.id;
            }
        }
    }
    return bestId;
}

std::int32_t XamlDomDocument::FindElementByName(std::string_view xName) const noexcept {
    if (xName.empty()) {
        return -1;
    }
    for (const XamlDomElement& el : elements_) {
        for (const XamlDomAttribute& attr : el.attributes) {
            if ((attr.rawName == "x:Name" || attr.rawName == "Name") && attr.value == xName) {
                return el.id;
            }
        }
    }
    return -1;
}

const XamlDomAttribute* XamlDomDocument::FindAttribute(
    std::int32_t elementId,
    std::string_view attrName) const noexcept {
    const XamlDomElement* el = GetElement(elementId);
    if (!el || attrName.empty()) {
        return nullptr;
    }
    for (const XamlDomAttribute& attr : el->attributes) {
        if (attr.rawName == attrName || (attr.prefix.empty() && attr.localName == attrName)) {
            return &attr;
        }
    }
    return nullptr;
}

std::optional<std::string_view> XamlDomDocument::GetAttributeValue(
    std::int32_t elementId,
    std::string_view attrName) const noexcept {
    if (const XamlDomAttribute* attr = FindAttribute(elementId, attrName)) {
        return std::string_view(attr->value);
    }
    return std::nullopt;
}

std::optional<XamlTextPatch> XamlDomDocument::BuildSetAttributePatch(
    std::int32_t elementId,
    std::string_view attrName,
    std::string_view newValue) const {
    const XamlDomElement* el = GetElement(elementId);
    if (!el || attrName.empty()) {
        return std::nullopt;
    }
    if (const XamlDomAttribute* existing = FindAttribute(elementId, attrName)) {
        XamlTextPatch patch{};
        patch.replaceStart = existing->valueInnerSpan.startOffset;
        patch.replaceLength = existing->valueInnerSpan.Length();
        patch.replacementText = EncodeXmlAttributeValue(newValue, existing->quoteChar);
        return patch;
    }

    XamlTextPatch patch{};
    patch.replaceStart = el->openTagAttrInsertOffset;
    patch.replaceLength = 0u;
    patch.replacementText = " " + std::string(attrName) + "=\"" + EncodeXmlAttributeValue(newValue, '"') + "\"";
    return patch;
}

std::optional<XamlTextPatch> XamlDomDocument::BuildRemoveAttributePatch(
    std::int32_t elementId,
    std::string_view attrName) const {
    const XamlDomAttribute* existing = FindAttribute(elementId, attrName);
    if (!existing) {
        return std::nullopt;
    }
    XamlTextPatch patch{};
    patch.replaceStart = existing->leadingTriviaOffset;
    patch.replaceLength = existing->fullSpan.endOffset - existing->leadingTriviaOffset;
    return patch;
}

std::optional<XamlTextPatch> XamlDomDocument::BuildInsertChildPatch(
    std::int32_t parentElementId,
    std::string_view childXamlSnippet,
    std::size_t indentStepSpaces) const {
    const XamlDomElement* parent = GetElement(parentElementId);
    if (!parent || childXamlSnippet.empty()) {
        return std::nullopt;
    }
    const std::string nl = DetectNewline(source_);
    const std::string parentIndent(parent->leadingLineIndentSpaces, ' ');
    const std::string childIndent(parent->leadingLineIndentSpaces + indentStepSpaces, ' ');

    // Normalize snippet indentation
    std::string trimmedChild(childXamlSnippet);
    while (!trimmedChild.empty() && IsSpace(trimmedChild.front())) {
        trimmedChild.erase(trimmedChild.begin());
    }
    while (!trimmedChild.empty() && IsSpace(trimmedChild.back())) {
        trimmedChild.pop_back();
    }
    if (trimmedChild.empty()) {
        return std::nullopt;
    }

    if (parent->isSelfClosing) {
        // Expand `<Grid ... />` into `<Grid ...>\n    <Child />\n</Grid>`
        const std::size_t slashPos = parent->openTagAttrInsertOffset;
        const std::size_t tagEnd = parent->openTagSpan.endOffset;
        if (slashPos >= tagEnd) {
            return std::nullopt;
        }
        XamlTextPatch patch{};
        patch.replaceStart = slashPos;
        patch.replaceLength = tagEnd - slashPos;
        patch.replacementText = ">" + nl + childIndent + trimmedChild + nl + parentIndent + "</" + parent->rawName + ">";
        return patch;
    }

    // Insert right before closing tag `</Parent>` preserving parent closing indent
    std::size_t insertAt = parent->childrenInsertOffset;
    while (insertAt > parent->openTagSpan.endOffset &&
           (source_[insertAt - 1u] == ' ' || source_[insertAt - 1u] == '\t')) {
        --insertAt;
    }
    const bool hasPrecedingNewline =
        insertAt > parent->openTagSpan.endOffset &&
        (source_[insertAt - 1u] == '\n' || source_[insertAt - 1u] == '\r');

    XamlTextPatch patch{};
    patch.replaceStart = insertAt;
    patch.replaceLength = parent->childrenInsertOffset - insertAt;
    if (hasPrecedingNewline) {
        patch.replacementText = childIndent + trimmedChild + nl + parentIndent;
    } else {
        patch.replacementText = nl + childIndent + trimmedChild + nl + parentIndent;
    }
    return patch;
}

std::optional<XamlTextPatch> XamlDomDocument::BuildRemoveElementPatch(
    std::int32_t elementId) const {
    const XamlDomElement* el = GetElement(elementId);
    if (!el || elementId == rootId_) {
        return std::nullopt;
    }
    std::size_t start = el->fullSpan.startOffset;
    while (start > 0u && (source_[start - 1u] == ' ' || source_[start - 1u] == '\t')) {
        --start;
    }
    std::size_t end = el->fullSpan.endOffset;
    if (end < source_.size() && source_[end] == '\r') {
        ++end;
    }
    if (end < source_.size() && source_[end] == '\n') {
        ++end;
    }
    XamlTextPatch patch{};
    patch.replaceStart = start;
    patch.replaceLength = end - start;
    return patch;
}

std::string XamlDomDocument::ApplyPatches(
    std::string_view originalSource,
    std::vector<XamlTextPatch> patches) {
    if (patches.empty()) {
        return std::string(originalSource);
    }
    std::sort(
        patches.begin(),
        patches.end(),
        [](const XamlTextPatch& a, const XamlTextPatch& b) {
            return a.replaceStart > b.replaceStart;
        });

    std::string result(originalSource);
    for (const XamlTextPatch& p : patches) {
        if (p.replaceStart > result.size()) {
            continue;
        }
        const std::size_t clampedLen = std::min(p.replaceLength, result.size() - p.replaceStart);
        result.replace(p.replaceStart, clampedLen, p.replacementText);
    }
    return result;
}

bool XamlDomDocument::SetAttributeInPlace(
    std::int32_t elementId,
    std::string_view attrName,
    std::string_view newValue,
    std::string* outError) {
    const auto patch = BuildSetAttributePatch(elementId, attrName, newValue);
    if (!patch.has_value()) {
        if (outError) *outError = "Failed to build SetAttribute patch";
        return false;
    }
    const std::string updated = ApplyPatches(source_, {*patch});
    return Parse(updated, *this, outError);
}

bool XamlDomDocument::RemoveAttributeInPlace(
    std::int32_t elementId,
    std::string_view attrName,
    std::string* outError) {
    const auto patch = BuildRemoveAttributePatch(elementId, attrName);
    if (!patch.has_value()) {
        if (outError) *outError = "Attribute not found for removal";
        return false;
    }
    const std::string updated = ApplyPatches(source_, {*patch});
    return Parse(updated, *this, outError);
}

bool XamlDomDocument::InsertChildInPlace(
    std::int32_t parentElementId,
    std::string_view childXamlSnippet,
    std::size_t indentStepSpaces,
    std::string* outError) {
    const auto patch = BuildInsertChildPatch(parentElementId, childXamlSnippet, indentStepSpaces);
    if (!patch.has_value()) {
        if (outError) *outError = "Failed to build InsertChild patch";
        return false;
    }
    const std::string updated = ApplyPatches(source_, {*patch});
    return Parse(updated, *this, outError);
}

bool XamlDomDocument::RemoveElementInPlace(
    std::int32_t elementId,
    std::string* outError) {
    const auto patch = BuildRemoveElementPatch(elementId);
    if (!patch.has_value()) {
        if (outError) *outError = "Cannot remove root or invalid element";
        return false;
    }
    const std::string updated = ApplyPatches(source_, {*patch});
    return Parse(updated, *this, outError);
}

} // namespace aero
