#include "aero/compiler/XmlPullLexer.hpp"

#include <algorithm>
#include <cctype>

#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] bool IsXmlWhitespace(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

[[nodiscard]] bool IsXmlNameStartChar(unsigned char ch) noexcept {
    return std::isalpha(ch) != 0 || ch == '_' || ch == ':' || ch >= 0x80u;
}

[[nodiscard]] bool IsXmlNameChar(unsigned char ch) noexcept {
    return IsXmlNameStartChar(ch) || std::isdigit(ch) != 0 || ch == '-' || ch == '.';
}

void SplitQualifiedName(
    std::string_view rawName,
    std::string& outPrefix,
    std::string& outLocalName) {
    const std::size_t colon = rawName.find(':');
    if (colon == std::string_view::npos) {
        outPrefix.clear();
        outLocalName = std::string(rawName);
        return;
    }
    outPrefix = std::string(rawName.substr(0, colon));
    outLocalName = std::string(rawName.substr(colon + 1));
}

} // namespace

std::string DecodeXmlEntities(std::string_view raw, std::string* outError) {
    if (raw.find('&') == std::string_view::npos) {
        return std::string(raw);
    }

    std::string result;
    result.reserve(raw.size());

    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] != '&') {
            result.push_back(raw[i]);
            ++i;
            continue;
        }

        const std::size_t semi = raw.find(';', i + 1);
        if (semi == std::string_view::npos) {
            if (outError != nullptr && outError->empty()) {
                *outError = "Unterminated XML character entity reference";
            }
            result.push_back(raw[i]);
            ++i;
            continue;
        }

        const std::string_view entity = raw.substr(i + 1, semi - (i + 1));
        if (entity == "amp") {
            result.push_back('&');
        } else if (entity == "lt") {
            result.push_back('<');
        } else if (entity == "gt") {
            result.push_back('>');
        } else if (entity == "quot") {
            result.push_back('"');
        } else if (entity == "apos") {
            result.push_back('\'');
        } else if (!entity.empty() && entity[0] == '#') {
            bool hex = false;
            std::string_view digits = entity.substr(1);
            if (!digits.empty() && (digits[0] == 'x' || digits[0] == 'X')) {
                hex = true;
                digits.remove_prefix(1);
            }
            std::uint32_t cp = 0;
            bool ok = !digits.empty();
            for (char ch : digits) {
                std::uint32_t d = 0;
                if (ch >= '0' && ch <= '9') {
                    d = static_cast<std::uint32_t>(ch - '0');
                } else if (hex && ch >= 'a' && ch <= 'f') {
                    d = static_cast<std::uint32_t>(10 + (ch - 'a'));
                } else if (hex && ch >= 'A' && ch <= 'F') {
                    d = static_cast<std::uint32_t>(10 + (ch - 'A'));
                } else {
                    ok = false;
                    break;
                }
                const std::uint32_t base = hex ? 16u : 10u;
                if (cp > (0x10FFFFu - d) / base) {
                    ok = false;
                    break;
                }
                cp = cp * base + d;
            }
            if (ok && cp > 0u && cp <= 0x10FFFFu) {
                str::AppendUtf8CodePoint(result, cp);
            } else {
                if (outError != nullptr && outError->empty()) {
                    *outError = "Invalid numeric XML character reference '&" + std::string(entity) + ";'";
                }
                result.append(raw.substr(i, semi - i + 1));
            }
        } else {
            if (outError != nullptr && outError->empty()) {
                *outError = "Unknown XML entity reference '&" + std::string(entity) + ";'";
            }
            result.append(raw.substr(i, semi - i + 1));
        }

        i = semi + 1;
    }

    return result;
}

XmlPullLexer::XmlPullLexer(std::string_view input, bool emitCommentsAndPI)
    : input_(input), emitCommentsAndPI_(emitCommentsAndPI) {
    if (input_.size() >= 3 &&
        static_cast<unsigned char>(input_[0]) == 0xEF &&
        static_cast<unsigned char>(input_[1]) == 0xBB &&
        static_cast<unsigned char>(input_[2]) == 0xBF) {
        pos_ = 3;
    }
}

char XmlPullLexer::Peek(std::size_t offset) const noexcept {
    if (pos_ + offset >= input_.size()) {
        return '\0';
    }
    return input_[pos_ + offset];
}

bool XmlPullLexer::StartsWith(std::string_view prefix) const noexcept {
    return pos_ + prefix.size() <= input_.size() &&
           input_.substr(pos_, prefix.size()) == prefix;
}

char XmlPullLexer::Advance() noexcept {
    if (pos_ >= input_.size()) {
        return '\0';
    }
    const char ch = input_[pos_++];
    if (ch == '\r') {
        if (pos_ < input_.size() && input_[pos_] == '\n') {
            ++pos_;
        }
        ++line_;
        column_ = 1;
        return '\n';
    }
    if (ch == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return ch;
}

void XmlPullLexer::SkipWhitespace() noexcept {
    while (!AtEnd() && IsXmlWhitespace(Peek())) {
        (void)Advance();
    }
}

std::string XmlPullLexer::ReadName() {
    if (AtEnd() || !IsXmlNameStartChar(static_cast<unsigned char>(Peek()))) {
        return {};
    }
    std::string name;
    while (!AtEnd() && IsXmlNameChar(static_cast<unsigned char>(Peek()))) {
        name.push_back(Advance());
    }
    return name;
}

std::string XmlPullLexer::ResolveNamespace(std::string_view prefix) const {
    if (prefix == "xml") {
        return "http://www.w3.org/XML/1998/namespace";
    }
    if (prefix == "xmlns") {
        return "http://www.w3.org/2000/xmlns/";
    }
    const std::string key(prefix);
    for (auto it = scopeStack_.rbegin(); it != scopeStack_.rend(); ++it) {
        const auto found = it->xmlnsBindings.find(key);
        if (found != it->xmlnsBindings.end()) {
            return found->second;
        }
    }
    return {};
}

XmlToken XmlPullLexer::MakeError(std::size_t startOffset, int line, int col, std::string message) {
    failed_ = true;
    XmlToken tok;
    tok.kind = XmlTokenKind::Error;
    tok.startOffset = startOffset;
    tok.endOffset = pos_;
    tok.line = line;
    tok.column = col;
    tok.endLine = line_;
    tok.endColumn = column_;
    tok.text = std::move(message);
    return tok;
}

XmlToken XmlPullLexer::Next() {
    while (!AtEnd() && !failed_) {
        if (Peek() == '<') {
            XmlToken tok = LexLessThan();
            if (!emitCommentsAndPI_ &&
                (tok.kind == XmlTokenKind::Comment ||
                 tok.kind == XmlTokenKind::ProcessingInstruction)) {
                continue;
            }
            return tok;
        }
        XmlToken textTok = LexText(pos_, line_, column_);
        if (!textTok.text.empty() || textTok.kind == XmlTokenKind::Error) {
            if (textTok.kind == XmlTokenKind::Text && scopeStack_.empty()) {
                bool onlyWs = true;
                for (char ch : textTok.text) {
                    if (!IsXmlWhitespace(ch)) {
                        onlyWs = false;
                        break;
                    }
                }
                if (onlyWs) {
                    continue;
                }
            }
            return textTok;
        }
    }

    XmlToken eof;
    eof.kind = XmlTokenKind::EndOfFile;
    eof.startOffset = pos_;
    eof.endOffset = pos_;
    eof.line = line_;
    eof.column = column_;
    eof.endLine = line_;
    eof.endColumn = column_;
    return eof;
}

std::vector<XmlToken> XmlPullLexer::LexAll() {
    std::vector<XmlToken> tokens;
    for (;;) {
        XmlToken tok = Next();
        const bool stop = (tok.kind == XmlTokenKind::EndOfFile || tok.kind == XmlTokenKind::Error);
        tokens.push_back(std::move(tok));
        if (stop) {
            break;
        }
    }
    return tokens;
}

XmlToken XmlPullLexer::LexLessThan() {
    const std::size_t startOffset = pos_;
    const int startLine = line_;
    const int startCol = column_;
    (void)Advance(); // consume '<'

    if (StartsWith("!--")) {
        return LexComment(startOffset, startLine, startCol);
    }
    if (StartsWith("![CDATA[")) {
        return LexCData(startOffset, startLine, startCol);
    }
    if (Peek() == '?') {
        return LexProcessingInstruction(startOffset, startLine, startCol);
    }
    if (Peek() == '!') {
        return LexDoctype(startOffset, startLine, startCol);
    }
    if (Peek() == '/') {
        return LexEndElement(startOffset, startLine, startCol);
    }
    return LexStartElement(startOffset, startLine, startCol);
}

XmlToken XmlPullLexer::LexComment(std::size_t startOffset, int startLine, int startCol) {
    pos_ += 3;
    column_ += 3;
    std::string text;
    while (!AtEnd()) {
        if (StartsWith("-->")) {
            pos_ += 3;
            column_ += 3;
            XmlToken tok;
            tok.kind = XmlTokenKind::Comment;
            tok.startOffset = startOffset;
            tok.endOffset = pos_;
            tok.line = startLine;
            tok.column = startCol;
            tok.endLine = line_;
            tok.endColumn = column_;
            tok.text = std::move(text);
            return tok;
        }
        text.push_back(Advance());
    }
    return MakeError(startOffset, startLine, startCol, "Unterminated XML comment '<!--'");
}

XmlToken XmlPullLexer::LexCData(std::size_t startOffset, int startLine, int startCol) {
    pos_ += 8;
    column_ += 8;
    std::string text;
    while (!AtEnd()) {
        if (StartsWith("]]>")) {
            pos_ += 3;
            column_ += 3;
            XmlToken tok;
            tok.kind = XmlTokenKind::CData;
            tok.startOffset = startOffset;
            tok.endOffset = pos_;
            tok.line = startLine;
            tok.column = startCol;
            tok.endLine = line_;
            tok.endColumn = column_;
            tok.text = std::move(text);
            return tok;
        }
        text.push_back(Advance());
    }
    return MakeError(startOffset, startLine, startCol, "Unterminated XML CDATA section");
}

XmlToken XmlPullLexer::LexProcessingInstruction(std::size_t startOffset, int startLine, int startCol) {
    (void)Advance(); // consume '?'
    std::string target = ReadName();
    SkipWhitespace();
    std::string data;
    while (!AtEnd()) {
        if (StartsWith("?>")) {
            pos_ += 2;
            column_ += 2;
            XmlToken tok;
            tok.kind = XmlTokenKind::ProcessingInstruction;
            tok.rawName = std::move(target);
            tok.text = std::move(data);
            tok.startOffset = startOffset;
            tok.endOffset = pos_;
            tok.line = startLine;
            tok.column = startCol;
            tok.endLine = line_;
            tok.endColumn = column_;
            return tok;
        }
        data.push_back(Advance());
    }
    return MakeError(startOffset, startLine, startCol, "Unterminated XML processing instruction '<?'");
}

XmlToken XmlPullLexer::LexDoctype(std::size_t startOffset, int startLine, int startCol) {
    (void)Advance(); // consume '!'
    int depth = 1;
    char quote = '\0';
    while (!AtEnd() && depth > 0) {
        const char ch = Advance();
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '<') {
            ++depth;
        } else if (ch == '>') {
            --depth;
        }
    }
    if (depth > 0) {
        return MakeError(startOffset, startLine, startCol, "Unterminated XML <!DOCTYPE> declaration");
    }
    XmlToken tok;
    tok.kind = XmlTokenKind::ProcessingInstruction;
    tok.rawName = "!DOCTYPE";
    tok.startOffset = startOffset;
    tok.endOffset = pos_;
    tok.line = startLine;
    tok.column = startCol;
    tok.endLine = line_;
    tok.endColumn = column_;
    return tok;
}

XmlToken XmlPullLexer::LexEndElement(std::size_t startOffset, int startLine, int startCol) {
    (void)Advance(); // consume '/'
    SkipWhitespace();
    const std::string rawName = ReadName();
    if (rawName.empty()) {
        return MakeError(startOffset, startLine, startCol, "Expected element name after '</'");
    }
    SkipWhitespace();
    if (AtEnd() || Peek() != '>') {
        return MakeError(startOffset, line_, column_, "Expected '>' after closing element '</" + rawName + "'");
    }
    (void)Advance();

    XmlToken tok;
    tok.kind = XmlTokenKind::EndElement;
    tok.rawName = rawName;
    SplitQualifiedName(rawName, tok.prefix, tok.localName);
    tok.namespaceUri = ResolveNamespace(tok.prefix);
    tok.startOffset = startOffset;
    tok.endOffset = pos_;
    tok.line = startLine;
    tok.column = startCol;
    tok.endLine = line_;
    tok.endColumn = column_;

    if (scopeStack_.empty()) {
        return MakeError(startOffset, startLine, startCol, "Unexpected closing element '</" + rawName + ">'");
    }
    if (scopeStack_.back().rawName != rawName) {
        return MakeError(
            startOffset,
            startLine,
            startCol,
            "Mismatched closing element '</" + rawName + ">', expected '</" + scopeStack_.back().rawName + ">'");
    }
    scopeStack_.pop_back();
    return tok;
}

XmlToken XmlPullLexer::LexStartElement(std::size_t startOffset, int startLine, int startCol) {
    const std::string rawName = ReadName();
    if (rawName.empty()) {
        return MakeError(startOffset, startLine, startCol, "Expected element name after '<'");
    }

    ElementFrame frame;
    frame.rawName = rawName;

    std::vector<XmlAttribute> attrs;
    std::size_t openTagAttrInsertOffset = pos_;
    for (;;) {
        const std::size_t triviaStart = pos_;
        SkipWhitespace();
        if (AtEnd()) {
            return MakeError(startOffset, startLine, startCol, "Unterminated start tag '<" + rawName + "'");
        }
        if (StartsWith("/>")) {
            openTagAttrInsertOffset = triviaStart;
            break;
        }
        if (Peek() == '>') {
            openTagAttrInsertOffset = pos_;
            break;
        }

        const std::size_t attrStartOffset = pos_;
        const int attrLine = line_;
        const int attrCol = column_;
        const std::string attrRawName = ReadName();
        if (attrRawName.empty()) {
            return MakeError(attrStartOffset, attrLine, attrCol, "Expected attribute name inside '<" + rawName + "'");
        }
        const std::size_t nameEndOffset = pos_;
        const int nameEndLine = line_;
        const int nameEndCol = column_;

        SkipWhitespace();
        if (AtEnd() || Peek() != '=') {
            return MakeError(attrStartOffset, line_, column_, "Expected '=' after attribute '" + attrRawName + "'");
        }
        (void)Advance(); // consume '='
        SkipWhitespace();

        if (AtEnd() || (Peek() != '"' && Peek() != '\'')) {
            return MakeError(attrStartOffset, line_, column_, "Expected quoted value for attribute '" + attrRawName + "'");
        }
        const char quote = Advance();
        const std::size_t valInnerStartOffset = pos_;
        const int valStartLine = line_;
        const int valStartCol = column_;

        std::string rawValue;
        bool closed = false;
        std::size_t valInnerEndOffset = pos_;
        int valEndLine = line_;
        int valEndCol = column_;
        while (!AtEnd()) {
            if (Peek() == quote) {
                valInnerEndOffset = pos_;
                valEndLine = line_;
                valEndCol = column_;
                (void)Advance();
                closed = true;
                break;
            }
            rawValue.push_back(Advance());
        }
        if (!closed) {
            return MakeError(attrStartOffset, attrLine, attrCol, "Unterminated quoted value for attribute '" + attrRawName + "'");
        }
        const std::size_t attrEndOffset = pos_;
        const int attrEndLine = line_;
        const int attrEndCol = column_;

        std::string entityErr;
        XmlAttribute attr;
        attr.rawName = attrRawName;
        SplitQualifiedName(attrRawName, attr.prefix, attr.localName);
        attr.value = DecodeXmlEntities(rawValue, &entityErr);
        attr.line = attrLine;
        attr.column = attrCol;
        attr.nameEndLine = nameEndLine;
        attr.nameEndColumn = nameEndCol;
        attr.valueStartLine = valStartLine;
        attr.valueStartColumn = valStartCol;
        attr.valueEndLine = valEndLine;
        attr.valueEndColumn = valEndCol;
        attr.endLine = attrEndLine;
        attr.endColumn = attrEndCol;
        attr.leadingTriviaOffset = triviaStart;
        attr.startOffset = attrStartOffset;
        attr.nameEndOffset = nameEndOffset;
        attr.valueInnerStartOffset = valInnerStartOffset;
        attr.valueInnerEndOffset = valInnerEndOffset;
        attr.endOffset = attrEndOffset;
        attr.quoteChar = quote;

        if (!entityErr.empty()) {
            return MakeError(attrStartOffset, attrLine, attrCol, entityErr);
        }

        if (attr.rawName == "xmlns") {
            attr.isXmlns = true;
            frame.xmlnsBindings[""] = attr.value;
        } else if (attr.prefix == "xmlns") {
            attr.isXmlns = true;
            frame.xmlnsBindings[attr.localName] = attr.value;
        }

        attrs.push_back(std::move(attr));
    }

    bool isEmpty = false;
    if (StartsWith("/>")) {
        pos_ += 2;
        column_ += 2;
        isEmpty = true;
    } else if (Peek() == '>') {
        (void)Advance();
    } else {
        return MakeError(startOffset, startLine, startCol, "Expected '>' closing '<" + rawName + "'");
    }

    scopeStack_.push_back(std::move(frame));

    XmlToken tok;
    tok.kind = XmlTokenKind::StartElement;
    tok.rawName = rawName;
    SplitQualifiedName(rawName, tok.prefix, tok.localName);
    tok.namespaceUri = ResolveNamespace(tok.prefix);
    tok.isEmptyElement = isEmpty;
    tok.line = startLine;
    tok.column = startCol;
    tok.endLine = line_;
    tok.endColumn = column_;
    tok.startOffset = startOffset;
    tok.endOffset = pos_;
    tok.openTagAttrInsertOffset = openTagAttrInsertOffset;

    for (auto& attr : attrs) {
        if (!attr.isXmlns && !attr.prefix.empty()) {
            attr.namespaceUri = ResolveNamespace(attr.prefix);
        }
    }
    tok.attributes = std::move(attrs);

    if (isEmpty) {
        scopeStack_.pop_back();
    }
    return tok;
}

XmlToken XmlPullLexer::LexText(std::size_t startOffset, int startLine, int startCol) {
    std::string raw;
    while (!AtEnd() && Peek() != '<') {
        raw.push_back(Advance());
    }

    std::string entityErr;
    std::string decoded = DecodeXmlEntities(raw, &entityErr);
    if (!entityErr.empty()) {
        return MakeError(startOffset, startLine, startCol, entityErr);
    }

    XmlToken tok;
    tok.kind = XmlTokenKind::Text;
    tok.startOffset = startOffset;
    tok.endOffset = pos_;
    tok.line = startLine;
    tok.column = startCol;
    tok.endLine = line_;
    tok.endColumn = column_;
    tok.text = std::move(decoded);
    return tok;
}

} // namespace aero
