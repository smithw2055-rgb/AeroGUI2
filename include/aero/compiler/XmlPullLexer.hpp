#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aero {

enum class XmlTokenKind : std::uint8_t {
    StartElement = 0,
    EndElement,
    Text,
    CData,
    Comment,
    ProcessingInstruction,
    EndOfFile,
    Error
};

struct XmlAttribute final {
    std::string rawName;
    std::string prefix;
    std::string localName;
    std::string namespaceUri;
    std::string value;
    int line = 1;
    int column = 1;
    bool isXmlns = false;
};

struct XmlToken final {
    XmlTokenKind kind = XmlTokenKind::EndOfFile;
    std::string rawName;
    std::string prefix;
    std::string localName;
    std::string namespaceUri;
    std::string text;
    std::vector<XmlAttribute> attributes;
    bool isEmptyElement = false;
    int line = 1;
    int column = 1;
};

/// Decodes XML predefined entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`)
/// and numeric character references (`&#...;`, `&#x...;`) into UTF-8 text.
[[nodiscard]] std::string DecodeXmlEntities(
    std::string_view raw,
    std::string* outError = nullptr);

/// Streaming pull lexer for XML / XAML documents with namespace scope resolution
/// and 1-based line/column diagnostics.
class XmlPullLexer final {
public:
    explicit XmlPullLexer(std::string_view input, bool emitCommentsAndPI = false);

    [[nodiscard]] XmlToken Next();
    [[nodiscard]] std::vector<XmlToken> LexAll();

    [[nodiscard]] int CurrentLine() const noexcept { return line_; }
    [[nodiscard]] int CurrentColumn() const noexcept { return column_; }
    [[nodiscard]] std::string ResolveNamespace(std::string_view prefix) const;

private:
    struct ElementFrame {
        std::string rawName;
        std::unordered_map<std::string, std::string> xmlnsBindings;
    };

    [[nodiscard]] bool AtEnd() const noexcept { return pos_ >= input_.size(); }
    [[nodiscard]] char Peek(std::size_t offset = 0) const noexcept;
    [[nodiscard]] bool StartsWith(std::string_view prefix) const noexcept;
    char Advance() noexcept;
    void SkipWhitespace() noexcept;
    [[nodiscard]] std::string ReadName();
    [[nodiscard]] XmlToken MakeError(int line, int col, std::string message);

    XmlToken LexLessThan();
    XmlToken LexComment(int startLine, int startCol);
    XmlToken LexCData(int startLine, int startCol);
    XmlToken LexProcessingInstruction(int startLine, int startCol);
    XmlToken LexDoctype(int startLine, int startCol);
    XmlToken LexEndElement(int startLine, int startCol);
    XmlToken LexStartElement(int startLine, int startCol);
    XmlToken LexText(int startLine, int startCol);

    std::string_view input_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    bool emitCommentsAndPI_ = false;
    bool failed_ = false;
    std::vector<ElementFrame> scopeStack_;
};

} // namespace aero
