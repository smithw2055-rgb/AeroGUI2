#include "aero/compiler/XamlDom.hpp"

#include <algorithm>
#include <cctype>

#include "aero/compiler/XmlPullLexer.hpp"

namespace aero {
namespace {

[[nodiscard]] bool IsSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

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

    auto setErr = [&](int ln, int cl, std::string msg) -> bool {
        if (outError != nullptr) {
            *outError = "Line " + std::to_string(ln) + ":" + std::to_string(cl) + ": " + std::move(msg);
        }
        return false;
    };

    XmlPullLexer lexer(src, /*emitCommentsAndPI=*/true);
    std::vector<std::int32_t> stack;

    for (;;) {
        XmlToken tok = lexer.Next();
        if (tok.kind == XmlTokenKind::EndOfFile) {
            break;
        }
        if (tok.kind == XmlTokenKind::Error) {
            return setErr(tok.line, tok.column, std::move(tok.text));
        }
        if (tok.kind == XmlTokenKind::ProcessingInstruction) {
            continue;
        }
        if (tok.kind == XmlTokenKind::Comment) {
            XamlDomComment cmt{};
            cmt.span = XamlTextSpan{
                tok.startOffset, tok.endOffset, tok.line, tok.column, tok.endLine, tok.endColumn};
            if (tok.endOffset >= tok.startOffset + 7u) {
                cmt.text.assign(src.substr(tok.startOffset + 4u, tok.endOffset - tok.startOffset - 7u));
            } else {
                cmt.text = std::move(tok.text);
            }
            outDoc.comments_.push_back(std::move(cmt));
            continue;
        }
        if (tok.kind == XmlTokenKind::CData) {
            if (!stack.empty()) {
                outDoc.elements_[static_cast<std::size_t>(stack.back())].innerText.append(tok.text);
            }
            continue;
        }
        if (tok.kind == XmlTokenKind::Text) {
            if (!stack.empty()) {
                const std::string_view rawTxt = src.substr(
                    tok.startOffset, tok.endOffset - tok.startOffset);
                bool allWs = true;
                for (char ch : rawTxt) {
                    if (!IsSpace(ch)) {
                        allWs = false;
                        break;
                    }
                }
                if (!allWs) {
                    outDoc.elements_[static_cast<std::size_t>(stack.back())].innerText.append(tok.text);
                }
            }
            continue;
        }
        if (tok.kind == XmlTokenKind::EndElement) {
            if (stack.empty()) {
                return setErr(tok.line, tok.column, "Unexpected closing tag '</" + tok.rawName + ">'");
            }
            const std::int32_t openId = stack.back();
            XamlDomElement& elem = outDoc.elements_[static_cast<std::size_t>(openId)];
            elem.childrenInsertOffset = tok.startOffset;
            elem.closeTagSpan = XamlTextSpan{
                tok.startOffset, tok.endOffset, tok.line, tok.column, tok.endLine, tok.endColumn};
            elem.fullSpan.endOffset = tok.endOffset;
            elem.fullSpan.endLine = tok.endLine;
            elem.fullSpan.endColumn = tok.endColumn;
            stack.pop_back();
            continue;
        }
        if (tok.kind == XmlTokenKind::StartElement) {
            const auto elemId = static_cast<std::int32_t>(outDoc.elements_.size());
            XamlDomElement elem{};
            elem.id = elemId;
            elem.parentId = stack.empty() ? -1 : stack.back();
            elem.rawName = std::move(tok.rawName);
            elem.prefix = std::move(tok.prefix);
            elem.localName = std::move(tok.localName);
            elem.isSelfClosing = tok.isEmptyElement;
            elem.leadingLineIndentSpaces = MeasureLeadingIndentSpaces(src, tok.startOffset);
            elem.openTagAttrInsertOffset = tok.openTagAttrInsertOffset;
            elem.childrenInsertOffset = tok.endOffset;
            elem.openTagSpan = XamlTextSpan{
                tok.startOffset, tok.endOffset, tok.line, tok.column, tok.endLine, tok.endColumn};
            elem.fullSpan = elem.openTagSpan;

            elem.attributes.reserve(tok.attributes.size());
            for (auto& a : tok.attributes) {
                XamlDomAttribute attr{};
                attr.rawName = std::move(a.rawName);
                attr.prefix = std::move(a.prefix);
                attr.localName = std::move(a.localName);
                attr.value = std::move(a.value);
                attr.rawValueLiteral.assign(src.substr(
                    a.valueInnerStartOffset, a.valueInnerEndOffset - a.valueInnerStartOffset));
                attr.quoteChar = a.quoteChar;
                attr.leadingTriviaOffset = a.leadingTriviaOffset;
                attr.fullSpan = XamlTextSpan{
                    a.startOffset, a.endOffset, a.line, a.column, a.endLine, a.endColumn};
                attr.nameSpan = XamlTextSpan{
                    a.startOffset, a.nameEndOffset, a.line, a.column, a.nameEndLine, a.nameEndColumn};
                attr.valueInnerSpan = XamlTextSpan{
                    a.valueInnerStartOffset,
                    a.valueInnerEndOffset,
                    a.valueStartLine,
                    a.valueStartColumn,
                    a.valueEndLine,
                    a.valueEndColumn};
                elem.attributes.push_back(std::move(attr));
            }

            if (elem.parentId >= 0) {
                outDoc.elements_[static_cast<std::size_t>(elem.parentId)].children.push_back(elemId);
            } else if (outDoc.rootId_ < 0) {
                outDoc.rootId_ = elemId;
            } else {
                return setErr(tok.line, tok.column, "Multiple root elements in XAML document");
            }

            const bool selfClosing = elem.isSelfClosing;
            outDoc.elements_.push_back(std::move(elem));
            if (!selfClosing) {
                stack.push_back(elemId);
            }
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
