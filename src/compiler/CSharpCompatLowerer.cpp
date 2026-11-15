#include "aero/compiler/CSharpCompatLowerer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace aero {
namespace {

bool isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isIdentStart(char c) {
    return isAsciiAlpha(c) || c == '_';
}

bool isIdentContinue(char c) {
    return isIdentStart(c) || isAsciiDigit(c);
}

bool isHorizontalSpace(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool hasPrefixSegment(std::string_view value, std::string_view root) {
    return value == root || (value.size() > root.size() && startsWith(value, root) &&
                             value[root.size()] == '.');
}

bool isFilteredExternalNamespace(std::string_view name) {
    if (hasPrefixSegment(name, "System") ||
        hasPrefixSegment(name, "Microsoft") ||
        hasPrefixSegment(name, "Windows") ||
        hasPrefixSegment(name, "CommunityToolkit") ||
        hasPrefixSegment(name, "Prism") ||
        hasPrefixSegment(name, "ReactiveUI")) {
        return true;
    }
    if (startsWith(name, "Noesis")) {
        return true;
    }
    return false;
}

std::size_t countNewlines(std::string_view text) {
    std::size_t n = 0;
    for (char c : text) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

void appendBlankPreservingNewlines(std::string& out, std::string_view span) {
    for (char c : span) {
        if (c == '\n' || c == '\r') {
            out.push_back(c);
        } else {
            out.push_back(' ');
        }
    }
}

struct TextEdit {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string replacement;
};

std::string applyTextEdits(std::string_view source, std::vector<TextEdit> edits) {
    if (edits.empty()) {
        return std::string(source);
    }
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.end < b.end;
    });

    std::string out;
    out.reserve(source.size());
    std::size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.start < cursor || edit.end > source.size() || edit.end < edit.start) {
            continue;
        }
        out.append(source.substr(cursor, edit.start - cursor));
        out.append(edit.replacement);
        cursor = edit.end;
    }
    out.append(source.substr(cursor));
    return out;
}

std::size_t skipLineComment(std::string_view s, std::size_t i) {
    i += 2;
    while (i < s.size() && s[i] != '\n') {
        ++i;
    }
    return i;
}

std::size_t skipBlockComment(std::string_view s, std::size_t i) {
    i += 2;
    while (i + 1 < s.size()) {
        if (s[i] == '*' && s[i + 1] == '/') {
            return i + 2;
        }
        ++i;
    }
    return s.size();
}

std::size_t skipCharLiteral(std::string_view s, std::size_t i) {
    ++i;  // skip opening '
    while (i < s.size()) {
        if (s[i] == '\\') {
            i = (i + 1 < s.size()) ? i + 2 : s.size();
            continue;
        }
        if (s[i] == '\'') {
            return i + 1;
        }
        if (s[i] == '\n') {
            return i;
        }
        ++i;
    }
    return s.size();
}

std::size_t skipNormalStringLiteral(std::string_view s, std::size_t i) {
    ++i;  // skip opening "
    while (i < s.size()) {
        if (s[i] == '\\') {
            i = (i + 1 < s.size()) ? i + 2 : s.size();
            continue;
        }
        if (s[i] == '"') {
            return i + 1;
        }
        if (s[i] == '\n') {
            return i;
        }
        ++i;
    }
    return s.size();
}

std::size_t skipVerbatimStringLiteral(std::string_view s, std::size_t i) {
    // s[i] == '@' && s[i + 1] == '"'
    i += 2;
    while (i < s.size()) {
        if (s[i] == '"') {
            if (i + 1 < s.size() && s[i + 1] == '"') {
                i += 2;
                continue;
            }
            return i + 1;
        }
        ++i;
    }
    return s.size();
}

std::size_t countQuotesAt(std::string_view s, std::size_t i) {
    std::size_t n = 0;
    while (i + n < s.size() && s[i + n] == '"') {
        ++n;
    }
    return n;
}

std::size_t skipRawStringLiteral(std::string_view s, std::size_t i, std::size_t quoteCount) {
    i += quoteCount;
    while (i < s.size()) {
        if (s[i] == '"') {
            std::size_t run = countQuotesAt(s, i);
            if (run >= quoteCount) {
                return i + quoteCount;
            }
            i += run;
            continue;
        }
        ++i;
    }
    return s.size();
}

// Skips any trivia (whitespace + comments) starting at `i`.
std::size_t skipTrivia(std::string_view s, std::size_t i) {
    while (i < s.size()) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < s.size()) {
            if (s[i + 1] == '/') {
                i = skipLineComment(s, i);
                continue;
            }
            if (s[i + 1] == '*') {
                i = skipBlockComment(s, i);
                continue;
            }
        }
        break;
    }
    return i;
}

// Extracts an identifier (handling optional C# `@` prefix, stripping `@` from output).
// Advances `i` to the end of the identifier. Returns empty string_view if not at identifier.
std::string_view readIdentifier(std::string_view s, std::size_t& i) {
    if (i >= s.size()) {
        return {};
    }
    std::size_t start = i;
    if (s[i] == '@') {
        if (i + 1 < s.size() && isIdentStart(s[i + 1])) {
            start = i + 1;
            ++i;
        } else {
            return {};
        }
    }
    if (!isIdentStart(s[i])) {
        i = start;
        return {};
    }
    while (i < s.size() && isIdentContinue(s[i])) {
        ++i;
    }
    return s.substr(start, i - start);
}

// Checks if `[i, ...)` starts a `#region`, `#endregion`, `#pragma`, or `#nullable` directive.
bool matchesDroppedDirective(std::string_view s, std::size_t hashPos) {
    std::size_t pos = hashPos + 1;
    while (pos < s.size() && isHorizontalSpace(s[pos])) {
        ++pos;
    }
    static constexpr std::string_view kDirectives[] = {
        "region", "endregion", "pragma", "nullable"};
    for (std::string_view kw : kDirectives) {
        if (pos + kw.size() <= s.size() && s.compare(pos, kw.size(), kw) == 0) {
            std::size_t after = pos + kw.size();
            if (after == s.size() || !isIdentContinue(s[after])) {
                return true;
            }
        }
    }
    return false;
}

// Forward declaration for recursive interpolated-string hole scanning in Pass 1.
void scanInterpolatedStringPass1(
    std::string_view s,
    std::size_t& i,
    bool isVerbatim,
    std::size_t rawQuoteCount,
    std::size_t dollarCount,
    std::vector<TextEdit>& edits);

// Attempts to parse `nameof ( expr )` starting at `nameofPos`.
// On success, appends replacement edit and advances `i` past closing `)`.
// On failure, leaves `i` untouched and returns false.
bool tryLowerNameofAt(
    std::string_view s,
    std::size_t nameofPos,
    std::size_t afterKeywordPos,
    std::vector<TextEdit>& edits,
    std::size_t& outNextPos) {
    std::size_t openPos = skipTrivia(s, afterKeywordPos);
    if (openPos >= s.size() || s[openPos] != '(') {
        return false;
    }

    std::size_t i = openPos + 1;
    int parenDepth = 1;
    int bracketDepth = 0;
    int braceDepth = 0;
    int angleDepth = 0;
    std::string_view lastTopIdent;

    while (i < s.size() && parenDepth > 0) {
        char c = s[i];
        if (c == '/' && i + 1 < s.size()) {
            if (s[i + 1] == '/') {
                i = skipLineComment(s, i);
                continue;
            }
            if (s[i + 1] == '*') {
                i = skipBlockComment(s, i);
                continue;
            }
        }
        if (c == '\'') {
            i = skipCharLiteral(s, i);
            continue;
        }
        if (c == '@') {
            if (i + 1 < s.size() && s[i + 1] == '"') {
                i = skipVerbatimStringLiteral(s, i);
                continue;
            }
            if (i + 2 < s.size() && s[i + 1] == '$' && s[i + 2] == '"') {
                std::size_t p = i + 3;
                std::vector<TextEdit> ignored;
                scanInterpolatedStringPass1(s, p, true, 0, 1, ignored);
                i = p;
                continue;
            }
        }
        if (c == '$') {
            std::size_t d = 0;
            while (i + d < s.size() && s[i + d] == '$') {
                ++d;
            }
            if (d == 1 && i + 1 < s.size() && s[i + 1] == '@' &&
                i + 2 < s.size() && s[i + 2] == '"') {
                std::size_t p = i + 3;
                std::vector<TextEdit> ignored;
                scanInterpolatedStringPass1(s, p, true, 0, 1, ignored);
                i = p;
                continue;
            }
            std::size_t q = countQuotesAt(s, i + d);
            if (q >= 3) {
                std::size_t p = i + d + q;
                std::vector<TextEdit> ignored;
                scanInterpolatedStringPass1(s, p, true, q, d, ignored);
                i = p;
                continue;
            }
            if (d == 1 && q >= 1) {
                std::size_t p = i + 2;
                std::vector<TextEdit> ignored;
                scanInterpolatedStringPass1(s, p, false, 0, 1, ignored);
                i = p;
                continue;
            }
        }
        if (c == '"') {
            std::size_t q = countQuotesAt(s, i);
            if (q >= 3) {
                i = skipRawStringLiteral(s, i, q);
            } else {
                i = skipNormalStringLiteral(s, i);
            }
            continue;
        }
        if (c == '(') {
            ++parenDepth;
            ++i;
            continue;
        }
        if (c == ')') {
            --parenDepth;
            ++i;
            continue;
        }
        if (c == '[') {
            ++bracketDepth;
            ++i;
            continue;
        }
        if (c == ']') {
            if (bracketDepth > 0) {
                --bracketDepth;
            }
            ++i;
            continue;
        }
        if (c == '{') {
            ++braceDepth;
            ++i;
            continue;
        }
        if (c == '}') {
            if (braceDepth > 0) {
                --braceDepth;
            }
            ++i;
            continue;
        }
        if (c == '<') {
            ++angleDepth;
            ++i;
            continue;
        }
        if (c == '>') {
            if (angleDepth > 0) {
                --angleDepth;
            }
            ++i;
            continue;
        }

        if (isIdentStart(c) || (c == '@' && i + 1 < s.size() && isIdentStart(s[i + 1]))) {
            std::size_t idCursor = i;
            std::string_view ident = readIdentifier(s, idCursor);
            if (!ident.empty() && parenDepth == 1 && bracketDepth == 0 &&
                braceDepth == 0 && angleDepth == 0) {
                lastTopIdent = ident;
            }
            i = idCursor;
            continue;
        }
        ++i;
    }

    if (parenDepth != 0 || lastTopIdent.empty()) {
        return false;
    }

    std::size_t endPos = i;
    std::size_t nl = countNewlines(s.substr(nameofPos, endPos - nameofPos));
    std::string replacement;
    replacement.reserve(lastTopIdent.size() + 2 + nl);
    replacement.push_back('"');
    replacement.append(lastTopIdent);
    replacement.push_back('"');
    replacement.append(nl, '\n');
    edits.push_back(TextEdit{nameofPos, endPos, std::move(replacement)});
    outNextPos = endPos;
    return true;
}

// Scans an interpolated string expression hole `{ ... }` in Pass 1 so `nameof(...)`
// inside interpolation expressions is lowered as well.
void scanInterpolatedHolePass1(
    std::string_view s,
    std::size_t& i,
    std::size_t closeBraceCount,
    bool isRawOrVerbatim,
    std::vector<TextEdit>& edits) {
    int braceDepth = 1;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool sawQuestion = false;
    char prevSigChar = '\0';

    while (i < s.size() && braceDepth > 0) {
        char c = s[i];
        if (isHorizontalSpace(c) || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < s.size()) {
            if (s[i + 1] == '/') {
                i = skipLineComment(s, i);
                continue;
            }
            if (s[i + 1] == '*') {
                i = skipBlockComment(s, i);
                continue;
            }
        }
        if (c == '\'') {
            prevSigChar = '\'';
            i = skipCharLiteral(s, i);
            continue;
        }
        if (c == '@') {
            if (i + 1 < s.size() && s[i + 1] == '"') {
                prevSigChar = '"';
                i = skipVerbatimStringLiteral(s, i);
                continue;
            }
            if (i + 2 < s.size() && s[i + 1] == '$' && s[i + 2] == '"') {
                prevSigChar = '"';
                i += 3;
                scanInterpolatedStringPass1(s, i, true, 0, 1, edits);
                continue;
            }
        }
        if (c == '$') {
            std::size_t d = 0;
            while (i + d < s.size() && s[i + d] == '$') {
                ++d;
            }
            if (d == 1 && i + 1 < s.size() && s[i + 1] == '@' &&
                i + 2 < s.size() && s[i + 2] == '"') {
                prevSigChar = '"';
                i += 3;
                scanInterpolatedStringPass1(s, i, true, 0, 1, edits);
                continue;
            }
            std::size_t q = countQuotesAt(s, i + d);
            if (q >= 3) {
                prevSigChar = '"';
                i += d + q;
                scanInterpolatedStringPass1(s, i, true, q, d, edits);
                continue;
            }
            if (d == 1 && q >= 1) {
                prevSigChar = '"';
                i += 2;
                scanInterpolatedStringPass1(s, i, false, 0, 1, edits);
                continue;
            }
        }
        if (c == '"') {
            prevSigChar = '"';
            std::size_t q = countQuotesAt(s, i);
            if (q >= 3) {
                i = skipRawStringLiteral(s, i, q);
            } else {
                i = skipNormalStringLiteral(s, i);
            }
            continue;
        }
        if (c == '(') {
            ++parenDepth;
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == ')') {
            if (parenDepth > 0) {
                --parenDepth;
            }
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == '[') {
            ++bracketDepth;
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == ']') {
            if (bracketDepth > 0) {
                --bracketDepth;
            }
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == '?') {
            if (braceDepth == 1 && parenDepth == 0 && bracketDepth == 0) {
                sawQuestion = true;
            }
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == ':') {
            if (braceDepth == 1 && parenDepth == 0 && bracketDepth == 0) {
                if (sawQuestion) {
                    sawQuestion = false;
                } else {
                    // Format specifier inside interpolation hole: consume up to closing brace(s).
                    ++i;
                    while (i < s.size()) {
                        if (!isRawOrVerbatim && s[i] == '\n') {
                            return;
                        }
                        if (s[i] == '}') {
                            std::size_t r = 0;
                            while (i + r < s.size() && s[i + r] == '}') {
                                ++r;
                            }
                            if (r >= closeBraceCount) {
                                i += closeBraceCount;
                                return;
                            }
                            i += r;
                            continue;
                        }
                        ++i;
                    }
                    return;
                }
            }
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == '{') {
            ++braceDepth;
            prevSigChar = c;
            ++i;
            continue;
        }
        if (c == '}') {
            if (braceDepth == 1) {
                if (closeBraceCount == 1) {
                    ++i;
                    return;
                }
                std::size_t r = 0;
                while (i + r < s.size() && s[i + r] == '}') {
                    ++r;
                }
                if (r >= closeBraceCount) {
                    i += closeBraceCount;
                    return;
                }
                i += r;
                continue;
            }
            --braceDepth;
            prevSigChar = c;
            ++i;
            continue;
        }

        if (isIdentStart(c) || (c == '@' && i + 1 < s.size() && isIdentStart(s[i + 1]))) {
            std::size_t tokStart = i;
            std::size_t tokEnd = i;
            std::string_view ident = readIdentifier(s, tokEnd);
            if (s[tokStart] != '@' && ident == "nameof" && prevSigChar != '.') {
                std::size_t nextPos = tokEnd;
                if (tryLowerNameofAt(s, tokStart, tokEnd, edits, nextPos)) {
                    prevSigChar = '"';
                    i = nextPos;
                    continue;
                }
            }
            prevSigChar = 'A';
            i = tokEnd;
            continue;
        }

        prevSigChar = c;
        ++i;
    }
}

void scanInterpolatedStringPass1(
    std::string_view s,
    std::size_t& i,
    bool isVerbatim,
    std::size_t rawQuoteCount,
    std::size_t dollarCount,
    std::vector<TextEdit>& edits) {
    if (rawQuoteCount >= 3) {
        while (i < s.size()) {
            if (s[i] == '"') {
                std::size_t run = countQuotesAt(s, i);
                if (run >= rawQuoteCount) {
                    i += rawQuoteCount;
                    return;
                }
                i += run;
                continue;
            }
            if (s[i] == '{') {
                std::size_t run = 0;
                while (i + run < s.size() && s[i + run] == '{') {
                    ++run;
                }
                if (run >= dollarCount) {
                    i += run;
                    scanInterpolatedHolePass1(s, i, dollarCount, true, edits);
                    continue;
                }
                i += run;
                continue;
            }
            ++i;
        }
        return;
    }

    while (i < s.size()) {
        char c = s[i];
        if (!isVerbatim && c == '\\') {
            i = (i + 1 < s.size()) ? i + 2 : s.size();
            continue;
        }
        if (isVerbatim && c == '"') {
            if (i + 1 < s.size() && s[i + 1] == '"') {
                i += 2;
                continue;
            }
            ++i;
            return;
        }
        if (!isVerbatim && c == '"') {
            ++i;
            return;
        }
        if (!isVerbatim && c == '\n') {
            return;
        }
        if (c == '{') {
            if (i + 1 < s.size() && s[i + 1] == '{') {
                i += 2;
                continue;
            }
            ++i;
            scanInterpolatedHolePass1(s, i, 1, isVerbatim, edits);
            continue;
        }
        if (c == '}') {
            if (i + 1 < s.size() && s[i + 1] == '}') {
                i += 2;
                continue;
            }
            ++i;
            continue;
        }
        ++i;
    }
}

// Pass 1:
// - Strip leading UTF-8 BOM.
// - Blank #region / #endregion / #pragma / #nullable lines preserving newlines.
// - Lower contextual nameof(...) to string literals preserving newlines.
std::string runPreprocessorAndNameofPass(std::string_view input) {
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF) {
        input.remove_prefix(3);
    }

    std::vector<TextEdit> edits;
    std::size_t i = 0;
    bool atLineStart = true;
    char prevSigChar = '\0';

    while (i < input.size()) {
        char c = input[i];
        if (c == '\n') {
            atLineStart = true;
            ++i;
            continue;
        }
        if (c == '\r' || isHorizontalSpace(c)) {
            ++i;
            continue;
        }
        if (atLineStart && c == '#') {
            if (matchesDroppedDirective(input, i)) {
                std::size_t lineEnd = i;
                while (lineEnd < input.size() && input[lineEnd] != '\n' &&
                       input[lineEnd] != '\r') {
                    ++lineEnd;
                }
                std::string spaces(lineEnd - i, ' ');
                edits.push_back(TextEdit{i, lineEnd, std::move(spaces)});
                i = lineEnd;
                atLineStart = false;
                continue;
            }
        }
        atLineStart = false;

        if (c == '/' && i + 1 < input.size()) {
            if (input[i + 1] == '/') {
                i = skipLineComment(input, i);
                continue;
            }
            if (input[i + 1] == '*') {
                i = skipBlockComment(input, i);
                continue;
            }
        }
        if (c == '\'') {
            prevSigChar = '\'';
            i = skipCharLiteral(input, i);
            continue;
        }
        if (c == '@') {
            if (i + 1 < input.size() && input[i + 1] == '"') {
                prevSigChar = '"';
                i = skipVerbatimStringLiteral(input, i);
                continue;
            }
            if (i + 2 < input.size() && input[i + 1] == '$' && input[i + 2] == '"') {
                prevSigChar = '"';
                i += 3;
                scanInterpolatedStringPass1(input, i, true, 0, 1, edits);
                continue;
            }
        }
        if (c == '$') {
            std::size_t d = 0;
            while (i + d < input.size() && input[i + d] == '$') {
                ++d;
            }
            if (d == 1 && i + 1 < input.size() && input[i + 1] == '@' &&
                i + 2 < input.size() && input[i + 2] == '"') {
                prevSigChar = '"';
                i += 3;
                scanInterpolatedStringPass1(input, i, true, 0, 1, edits);
                continue;
            }
            std::size_t q = countQuotesAt(input, i + d);
            if (q >= 3) {
                prevSigChar = '"';
                i += d + q;
                scanInterpolatedStringPass1(input, i, true, q, d, edits);
                continue;
            }
            if (d == 1 && q >= 1) {
                prevSigChar = '"';
                i += 2;
                scanInterpolatedStringPass1(input, i, false, 0, 1, edits);
                continue;
            }
        }
        if (c == '"') {
            prevSigChar = '"';
            std::size_t q = countQuotesAt(input, i);
            if (q >= 3) {
                i = skipRawStringLiteral(input, i, q);
            } else {
                i = skipNormalStringLiteral(input, i);
            }
            continue;
        }

        if (isIdentStart(c) ||
            (c == '@' && i + 1 < input.size() && isIdentStart(input[i + 1]))) {
            std::size_t tokStart = i;
            std::size_t tokEnd = i;
            std::string_view ident = readIdentifier(input, tokEnd);
            if (input[tokStart] != '@' && ident == "nameof" && prevSigChar != '.') {
                std::size_t nextPos = tokEnd;
                if (tryLowerNameofAt(input, tokStart, tokEnd, edits, nextPos)) {
                    prevSigChar = '"';
                    i = nextPos;
                    continue;
                }
            }
            prevSigChar = 'A';
            i = tokEnd;
            continue;
        }

        prevSigChar = c;
        ++i;
    }

    return applyTextEdits(input, std::move(edits));
}

// Skips any trivia or non-code literal starting at `i` in Pass 2.
// Note Pass 2 only cares about top-level / namespace-level braces, parens, brackets,
// and keywords (`using`, `global`, `namespace`, `module`, `import`), so interpolated
// strings can simply be skipped cleanly using Pass 1's hole scanner with an empty vector
// (Wait: here we pass nullptr/unused — let's provide a lightweight hole/string skipper).
void skipInterpolatedStringPass2(
    std::string_view s,
    std::size_t& i,
    bool isVerbatim,
    std::size_t rawQuoteCount,
    std::size_t dollarCount) {
    std::vector<TextEdit> unused;
    scanInterpolatedStringPass1(s, i, isVerbatim, rawQuoteCount, dollarCount, unused);
}

// Parses `Ident (. Ident)*` starting after skipTrivia.
// Returns false if no valid qualified identifier was found.
bool parseQualifiedName(std::string_view s, std::size_t& pos, std::string& outName) {
    pos = skipTrivia(s, pos);
    std::size_t cur = pos;
    std::string_view first = readIdentifier(s, cur);
    if (first.empty()) {
        return false;
    }
    outName.assign(first);
    while (true) {
        std::size_t dotCheck = skipTrivia(s, cur);
        if (dotCheck < s.size() && s[dotCheck] == '.') {
            std::size_t nextIdPos = skipTrivia(s, dotCheck + 1);
            std::string_view nextPart = readIdentifier(s, nextIdPos);
            if (nextPart.empty()) {
                return false;
            }
            outName.push_back('.');
            outName.append(nextPart);
            cur = nextIdPos;
            continue;
        }
        break;
    }
    pos = cur;
    return true;
}

// Advances to the first `;` at brace/paren/bracket depth 0 relative to start.
// Returns `std::string::npos` if unbalanced or EOF.
std::size_t scanToStatementSemicolon(std::string_view s, std::size_t i) {
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == '/' && i + 1 < s.size()) {
            if (s[i + 1] == '/') {
                i = skipLineComment(s, i);
                continue;
            }
            if (s[i + 1] == '*') {
                i = skipBlockComment(s, i);
                continue;
            }
        }
        if (c == '\'') {
            i = skipCharLiteral(s, i);
            continue;
        }
        if (c == '"') {
            std::size_t q = countQuotesAt(s, i);
            i = (q >= 3) ? skipRawStringLiteral(s, i, q) : skipNormalStringLiteral(s, i);
            continue;
        }
        if (c == '@' && i + 1 < s.size() && s[i + 1] == '"') {
            i = skipVerbatimStringLiteral(s, i);
            continue;
        }
        if (c == '(') {
            ++paren;
        } else if (c == ')') {
            if (paren > 0) --paren;
        } else if (c == '[') {
            ++bracket;
        } else if (c == ']') {
            if (bracket > 0) --bracket;
        } else if (c == '{') {
            ++brace;
        } else if (c == '}') {
            if (brace == 0) {
                return std::string::npos;
            }
            --brace;
        } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
            return i + 1;
        }
        ++i;
    }
    return std::string::npos;
}

struct UsingEntry {
    std::size_t start = 0;
    std::size_t end = 0;
    bool blankOnly = false;
    bool isExplicitImport = false;
    bool beforeAnyDeclaration = true;
    std::string target;
};

enum class HeaderKind {
    None,
    ExplicitModule,
    FileScopedNamespace,
    BlockScopedNamespace
};

bool shouldKeepTarget(
    const std::string& target,
    const std::string& effectiveModule,
    const std::unordered_set<std::string>* availableModules) {
    if (target.empty() || target == effectiveModule) {
        return false;
    }
    if (availableModules != nullptr) {
        return availableModules->count(target) > 0;
    }
    return !isFilteredExternalNamespace(target);
}

// Pass 2: Lower `using` and `namespace` into RealScript `module` + `import`.
std::string runNamespaceAndUsingPass(
    std::string_view s,
    const std::unordered_set<std::string>* availableModules) {
    HeaderKind headerKind = HeaderKind::None;
    std::size_t headerStart = 0;
    std::size_t headerEnd = 0;
    std::string headerModuleName;
    std::vector<std::pair<std::size_t, std::size_t>> extraBlankSpans;

    std::vector<UsingEntry> directives;

    int braceDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    int activeNamespaceBlocks = 0;
    bool sawBodyDeclaration = false;

    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < s.size()) {
            if (s[i + 1] == '/') {
                i = skipLineComment(s, i);
                continue;
            }
            if (s[i + 1] == '*') {
                i = skipBlockComment(s, i);
                continue;
            }
        }
        if (c == '\'') {
            i = skipCharLiteral(s, i);
            continue;
        }
        if (c == '@') {
            if (i + 1 < s.size() && s[i + 1] == '"') {
                i = skipVerbatimStringLiteral(s, i);
                continue;
            }
            if (i + 2 < s.size() && s[i + 1] == '$' && s[i + 2] == '"') {
                i += 3;
                skipInterpolatedStringPass2(s, i, true, 0, 1);
                continue;
            }
        }
        if (c == '$') {
            std::size_t d = 0;
            while (i + d < s.size() && s[i + d] == '$') {
                ++d;
            }
            if (d == 1 && i + 1 < s.size() && s[i + 1] == '@' &&
                i + 2 < s.size() && s[i + 2] == '"') {
                i += 3;
                skipInterpolatedStringPass2(s, i, true, 0, 1);
                continue;
            }
            std::size_t q = countQuotesAt(s, i + d);
            if (q >= 3) {
                i += d + q;
                skipInterpolatedStringPass2(s, i, true, q, d);
                continue;
            }
            if (d == 1 && q >= 1) {
                i += 2;
                skipInterpolatedStringPass2(s, i, false, 0, 1);
                continue;
            }
        }
        if (c == '"') {
            std::size_t q = countQuotesAt(s, i);
            i = (q >= 3) ? skipRawStringLiteral(s, i, q) : skipNormalStringLiteral(s, i);
            continue;
        }

        if (c == '(') {
            ++parenDepth;
            ++i;
            continue;
        }
        if (c == ')') {
            if (parenDepth > 0) --parenDepth;
            ++i;
            continue;
        }
        if (c == '[') {
            ++bracketDepth;
            ++i;
            continue;
        }
        if (c == ']') {
            if (bracketDepth > 0) --bracketDepth;
            ++i;
            continue;
        }
        if (c == '{') {
            ++braceDepth;
            ++i;
            continue;
        }
        if (c == '}') {
            if (activeNamespaceBlocks > 0 && braceDepth == activeNamespaceBlocks &&
                parenDepth == 0 && bracketDepth == 0) {
                std::size_t closeEnd = i + 1;
                std::size_t afterClose = skipTrivia(s, closeEnd);
                if (afterClose < s.size() && s[afterClose] == ';') {
                    closeEnd = afterClose + 1;
                }
                extraBlankSpans.emplace_back(i, closeEnd);
                --activeNamespaceBlocks;
                --braceDepth;
                i = closeEnd;
                continue;
            }
            if (braceDepth > 0) --braceDepth;
            ++i;
            continue;
        }

        const bool atDirectiveScope =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == activeNamespaceBlocks;

        if (atDirectiveScope &&
            (isIdentStart(c) || (c == '@' && i + 1 < s.size() && isIdentStart(s[i + 1])))) {
            std::size_t tokStart = i;
            std::size_t tokEnd = i;
            std::string_view ident = readIdentifier(s, tokEnd);
            bool unescapedKw = (s[tokStart] != '@');

            // Handle `global using ...;`
            if (unescapedKw && ident == "global") {
                std::size_t nextPos = skipTrivia(s, tokEnd);
                std::size_t kwEnd = nextPos;
                if (nextPos < s.size() && isIdentStart(s[nextPos])) {
                    std::string_view nextKw = readIdentifier(s, kwEnd);
                    if (nextKw == "using") {
                        tokEnd = kwEnd;
                        ident = nextKw;
                    }
                }
            }

            if (unescapedKw && ident == "using") {
                std::size_t p = skipTrivia(s, tokEnd);
                if (p < s.size() && s[p] != '(') {
                    std::size_t peekEnd = p;
                    std::string_view firstWord =
                        isIdentStart(s[p]) ? readIdentifier(s, peekEnd) : std::string_view{};
                    if (firstWord == "static") {
                        std::size_t semiEnd = scanToStatementSemicolon(s, peekEnd);
                        if (semiEnd != std::string::npos) {
                            directives.push_back(UsingEntry{
                                tokStart, semiEnd, true, false, !sawBodyDeclaration, {}});
                            i = semiEnd;
                            continue;
                        }
                    } else if (!firstWord.empty()) {
                        // Distinguish:
                        //   1) `using A.B.C;` -> namespace import directive
                        //   2) `using Alias = ...;` -> alias directive (blank)
                        //   3) `using Type name = ...;` -> variable declaration (keep!)
                        std::size_t afterFirst = skipTrivia(s, peekEnd);
                        if (afterFirst < s.size() && s[afterFirst] == '=') {
                            std::size_t semiEnd = scanToStatementSemicolon(s, afterFirst + 1);
                            if (semiEnd != std::string::npos) {
                                directives.push_back(UsingEntry{
                                    tokStart, semiEnd, true, false, !sawBodyDeclaration, {}});
                                i = semiEnd;
                                continue;
                            }
                        } else if (afterFirst < s.size() && s[afterFirst] == ':' &&
                                   afterFirst + 1 < s.size() && s[afterFirst + 1] == ':') {
                            // e.g. `using global::System;`
                            std::size_t semiEnd = scanToStatementSemicolon(s, afterFirst + 2);
                            if (semiEnd != std::string::npos) {
                                directives.push_back(UsingEntry{
                                    tokStart, semiEnd, true, false, !sawBodyDeclaration, {}});
                                i = semiEnd;
                                continue;
                            }
                        } else {
                            std::size_t qPos = p;
                            std::string qName;
                            if (parseQualifiedName(s, qPos, qName)) {
                                std::size_t afterName = skipTrivia(s, qPos);
                                if (afterName < s.size() && s[afterName] == ';') {
                                    directives.push_back(UsingEntry{
                                        tokStart,
                                        afterName + 1,
                                        false,
                                        false,
                                        !sawBodyDeclaration,
                                        std::move(qName)});
                                    i = afterName + 1;
                                    continue;
                                }
                                if (afterName < s.size() && s[afterName] == '=' &&
                                    firstWord != "var") {
                                    // Qualified alias unlikely, ignore unless needed.
                                }
                            }
                        }
                    }
                }
                sawBodyDeclaration = true;
                i = tokEnd;
                continue;
            }

            if (unescapedKw && ident == "import") {
                std::size_t qPos = tokEnd;
                std::string qName;
                if (parseQualifiedName(s, qPos, qName)) {
                    std::size_t afterName = skipTrivia(s, qPos);
                    if (afterName < s.size() && s[afterName] == ';') {
                        directives.push_back(UsingEntry{
                            tokStart,
                            afterName + 1,
                            false,
                            true,
                            !sawBodyDeclaration,
                            std::move(qName)});
                        i = afterName + 1;
                        continue;
                    }
                }
                sawBodyDeclaration = true;
                i = tokEnd;
                continue;
            }

            if (unescapedKw && braceDepth == 0 && headerKind == HeaderKind::None &&
                !sawBodyDeclaration && ident == "module") {
                std::size_t qPos = tokEnd;
                std::string qName;
                if (parseQualifiedName(s, qPos, qName)) {
                    std::size_t afterName = skipTrivia(s, qPos);
                    if (afterName < s.size() && s[afterName] == ';') {
                        headerKind = HeaderKind::ExplicitModule;
                        headerStart = tokStart;
                        headerEnd = afterName + 1;
                        headerModuleName = std::move(qName);
                        i = afterName + 1;
                        continue;
                    }
                }
                sawBodyDeclaration = true;
                i = tokEnd;
                continue;
            }

            if (unescapedKw && ident == "namespace") {
                std::size_t qPos = tokEnd;
                std::string qName;
                if (parseQualifiedName(s, qPos, qName)) {
                    std::size_t afterName = skipTrivia(s, qPos);
                    if (headerKind == HeaderKind::None && braceDepth == 0 &&
                        !sawBodyDeclaration) {
                        if (afterName < s.size() && s[afterName] == ';') {
                            headerKind = HeaderKind::FileScopedNamespace;
                            headerStart = tokStart;
                            headerEnd = afterName + 1;
                            headerModuleName = std::move(qName);
                            i = afterName + 1;
                            continue;
                        }
                        if (afterName < s.size() && s[afterName] == '{') {
                            headerKind = HeaderKind::BlockScopedNamespace;
                            headerStart = tokStart;
                            headerEnd = afterName + 1;
                            headerModuleName = std::move(qName);
                            activeNamespaceBlocks = 1;
                            braceDepth = 1;
                            i = afterName + 1;
                            continue;
                        }
                    } else if (headerKind != HeaderKind::ExplicitModule) {
                        if (afterName < s.size() && s[afterName] == ';') {
                            extraBlankSpans.emplace_back(tokStart, afterName + 1);
                            i = afterName + 1;
                            continue;
                        }
                        if (afterName < s.size() && s[afterName] == '{') {
                            if (headerKind == HeaderKind::BlockScopedNamespace &&
                                activeNamespaceBlocks > 0 && !sawBodyDeclaration) {
                                headerModuleName.push_back('.');
                                headerModuleName.append(qName);
                            }
                            extraBlankSpans.emplace_back(tokStart, afterName + 1);
                            ++activeNamespaceBlocks;
                            ++braceDepth;
                            i = afterName + 1;
                            continue;
                        }
                    }
                }
                sawBodyDeclaration = true;
                i = tokEnd;
                continue;
            }

            sawBodyDeclaration = true;
            i = tokEnd;
            continue;
        }

        ++i;
    }

    if (directives.empty() && headerKind == HeaderKind::None && extraBlankSpans.empty()) {
        return std::string(s);
    }

    std::vector<TextEdit> edits;
    std::unordered_set<std::string> seenImports;

    if (headerKind == HeaderKind::None) {
        // No module/namespace declaration in file: rewrite `using X;` in place to `import X;`.
        for (const auto& d : directives) {
            std::string rep;
            if (!d.blankOnly && d.isExplicitImport) {
                seenImports.insert(d.target);
                continue;
            }
            std::string_view span = s.substr(d.start, d.end - d.start);
            if (!d.blankOnly &&
                shouldKeepTarget(d.target, "", availableModules) &&
                seenImports.insert(d.target).second) {
                std::size_t nl = countNewlines(span);
                rep = "import " + d.target + ";";
                rep.append(nl, '\n');
            } else {
                appendBlankPreservingNewlines(rep, span);
            }
            edits.push_back(TextEdit{d.start, d.end, std::move(rep)});
        }
        for (const auto& [bStart, bEnd] : extraBlankSpans) {
            std::string blank;
            appendBlankPreservingNewlines(blank, s.substr(bStart, bEnd - bStart));
            edits.push_back(TextEdit{bStart, bEnd, std::move(blank)});
        }
        return applyTextEdits(s, std::move(edits));
    }

    // We have a `module` or `namespace` header.
    // All `using` directives (and any `import` appearing BEFORE the header or after a declaration)
    // are blanked and emitted right after `module <Name>;` on the header line so `module` ALWAYS
    // precedes all `import`s while keeping exact line numbers.
    std::vector<std::string> headerImports;
    for (const auto& d : directives) {
        if (d.isExplicitImport && d.start >= headerEnd && d.beforeAnyDeclaration) {
            seenImports.insert(d.target);
        }
    }

    for (const auto& d : directives) {
        if (d.isExplicitImport && d.start >= headerEnd && d.beforeAnyDeclaration) {
            // Already located after `module <Name>;` — leave untouched.
            continue;
        }
        std::string blank;
        appendBlankPreservingNewlines(blank, s.substr(d.start, d.end - d.start));
        edits.push_back(TextEdit{d.start, d.end, std::move(blank)});

        if (!d.blankOnly) {
            const bool keep =
                d.isExplicitImport
                    ? (d.target != headerModuleName && seenImports.insert(d.target).second)
                    : (shouldKeepTarget(d.target, headerModuleName, availableModules) &&
                       seenImports.insert(d.target).second);
            if (keep) {
                headerImports.push_back(d.target);
            }
        }
    }

    std::string headerRep = "module " + headerModuleName + ";";
    for (const auto& imp : headerImports) {
        headerRep.append(" import ");
        headerRep.append(imp);
        headerRep.push_back(';');
    }
    std::size_t headerNewlines = countNewlines(s.substr(headerStart, headerEnd - headerStart));
    if (headerNewlines > 0) {
        headerRep.append(headerNewlines, '\n');
    } else if (headerEnd < s.size() && !isHorizontalSpace(s[headerEnd]) &&
               s[headerEnd] != '\r' && s[headerEnd] != '\n') {
        headerRep.push_back(' ');
    }
    edits.push_back(TextEdit{headerStart, headerEnd, std::move(headerRep)});

    for (const auto& [bStart, bEnd] : extraBlankSpans) {
        std::string blank;
        appendBlankPreservingNewlines(blank, s.substr(bStart, bEnd - bStart));
        edits.push_back(TextEdit{bStart, bEnd, std::move(blank)});
    }

    return applyTextEdits(s, std::move(edits));
}

}  // namespace

std::string LowerCSharpCompatSyntax(
    std::string_view source,
    const std::unordered_set<std::string>* availableModules) {
    std::string pass1 = runPreprocessorAndNameofPass(source);
    return runNamespaceAndUsingPass(pass1, availableModules);
}

}  // namespace aero
