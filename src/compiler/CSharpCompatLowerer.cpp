#include "aero/compiler/AeroCompilerV3.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] bool IsIdentStart(char ch) noexcept {
    const unsigned char u = static_cast<unsigned char>(ch);
    return std::isalpha(u) != 0 || ch == '_';
}

[[nodiscard]] bool IsIdentCont(char ch) noexcept {
    const unsigned char u = static_cast<unsigned char>(ch);
    return IsIdentStart(ch) || std::isdigit(u) != 0;
}

[[nodiscard]] bool IsHorizWs(char ch) noexcept {
    return ch == ' ' || ch == '\t';
}

[[nodiscard]] std::string StripBomAndCr(std::string_view src) {
    if (src.size() >= 3 &&
        static_cast<unsigned char>(src[0]) == 0xEF &&
        static_cast<unsigned char>(src[1]) == 0xBB &&
        static_cast<unsigned char>(src[2]) == 0xBF) {
        src.remove_prefix(3);
    }
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\r') {
            if (i + 1 < src.size() && src[i + 1] == '\n') {
                ++i;
            }
            out.push_back('\n');
        } else {
            out.push_back(src[i]);
        }
    }
    return out;
}

class PreprocessorEval final {
public:
    PreprocessorEval(std::string_view expr, const std::unordered_set<std::string>& defs)
        : expr_(expr), defs_(defs) {}

    [[nodiscard]] bool Eval() {
        const bool val = ParseOr();
        return val;
    }

private:
    void SkipWs() noexcept {
        while (pos_ < expr_.size() && (expr_[pos_] == ' ' || expr_[pos_] == '\t')) {
            ++pos_;
        }
    }

    [[nodiscard]] bool Match(std::string_view tok) noexcept {
        SkipWs();
        if (pos_ + tok.size() <= expr_.size() &&
            expr_.substr(pos_, tok.size()) == tok) {
            pos_ += tok.size();
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string ReadWord() {
        SkipWs();
        const std::size_t start = pos_;
        while (pos_ < expr_.size() && IsIdentCont(expr_[pos_])) {
            ++pos_;
        }
        return std::string(expr_.substr(start, pos_ - start));
    }

    bool ParseOr() {
        bool lhs = ParseAnd();
        while (Match("||")) {
            const bool rhs = ParseAnd();
            lhs = lhs || rhs;
        }
        return lhs;
    }

    bool ParseAnd() {
        bool lhs = ParseEq();
        while (Match("&&")) {
            const bool rhs = ParseEq();
            lhs = lhs && rhs;
        }
        return lhs;
    }

    bool ParseEq() {
        bool lhs = ParseUnary();
        for (;;) {
            if (Match("==")) {
                lhs = (lhs == ParseUnary());
            } else if (Match("!=")) {
                lhs = (lhs != ParseUnary());
            } else {
                break;
            }
        }
        return lhs;
    }

    bool ParseUnary() {
        if (Match("!")) {
            return !ParseUnary();
        }
        return ParsePrimary();
    }

    bool ParsePrimary() {
        SkipWs();
        if (Match("(")) {
            const bool v = ParseOr();
            (void)Match(")");
            return v;
        }
        const std::string word = ReadWord();
        if (word.empty()) {
            return false;
        }
        if (word == "true") return true;
        if (word == "false") return false;
        return defs_.count(word) > 0;
    }

    std::string_view expr_;
    const std::unordered_set<std::string>& defs_;
    std::size_t pos_ = 0;
};

[[nodiscard]] std::string PreprocessDirectives(std::string_view src) {
    struct BranchState {
        bool parentActive = true;
        bool currentActive = true;
        bool branchTaken = false;
    };

    std::unordered_set<std::string> defines = {"REALSCRIPT", "AERO_V3", "AERO_GUI"};
    std::vector<BranchState> stack;

    auto isActive = [&]() -> bool {
        return stack.empty() ? true : stack.back().currentActive;
    };

    auto stripTrailingComment = [](std::string_view line) -> std::string_view {
        const std::size_t slash = line.find("//");
        if (slash != std::string_view::npos) {
            line = line.substr(0, slash);
        }
        return aero::str::Trim(line);
    };

    std::string out;
    out.reserve(src.size());

    bool inBlockComment = false;
    bool inVerbatimString = false;

    std::size_t lineStart = 0;
    while (lineStart <= src.size()) {
        std::size_t lineEnd = src.find('\n', lineStart);
        const bool hasNewline = (lineEnd != std::string_view::npos);
        if (!hasNewline) {
            lineEnd = src.size();
        }
        const std::string_view line = src.substr(lineStart, lineEnd - lineStart);
        const std::string_view trimmed = aero::str::Trim(line);

        bool handledDirective = false;
        if (!inBlockComment && !inVerbatimString && !trimmed.empty() && trimmed[0] == '#') {
            handledDirective = true;
            const std::string_view afterHash = aero::str::Trim(trimmed.substr(1));

            auto startsWord = [&](std::string_view kw) -> bool {
                return afterHash.size() >= kw.size() &&
                       afterHash.substr(0, kw.size()) == kw &&
                       (afterHash.size() == kw.size() || !IsIdentCont(afterHash[kw.size()]));
            };

            if (startsWord("if")) {
                const bool parent = isActive();
                const std::string_view expr = stripTrailingComment(afterHash.substr(2));
                const bool cond = parent && PreprocessorEval(expr, defines).Eval();
                stack.push_back(BranchState{parent, cond, cond});
            } else if (startsWord("elif")) {
                if (!stack.empty()) {
                    auto& top = stack.back();
                    const std::string_view expr = stripTrailingComment(afterHash.substr(4));
                    if (top.parentActive && !top.branchTaken) {
                        const bool cond = PreprocessorEval(expr, defines).Eval();
                        top.currentActive = cond;
                        top.branchTaken = cond;
                    } else {
                        top.currentActive = false;
                    }
                }
            } else if (startsWord("else")) {
                if (!stack.empty()) {
                    auto& top = stack.back();
                    top.currentActive = top.parentActive && !top.branchTaken;
                    top.branchTaken = true;
                }
            } else if (startsWord("endif")) {
                if (!stack.empty()) {
                    stack.pop_back();
                }
            } else if (startsWord("define")) {
                if (isActive()) {
                    const std::string sym(stripTrailingComment(afterHash.substr(6)));
                    if (!sym.empty()) {
                        defines.insert(sym);
                    }
                }
            } else if (startsWord("undef")) {
                if (isActive()) {
                    const std::string sym(stripTrailingComment(afterHash.substr(5)));
                    if (!sym.empty()) {
                        defines.erase(sym);
                    }
                }
            }
            // #region, #endregion, #pragma, #nullable, #line, etc. are dropped cleanly
        }

        if (handledDirective || !isActive()) {
            if (hasNewline) {
                out.push_back('\n');
            }
        } else {
            // Update multiline comment / verbatim string state on emitted active lines
            bool inLineStr = false;
            bool inChar = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';
                if (inBlockComment) {
                    if (c == '*' && next == '/') {
                        inBlockComment = false;
                        ++i;
                    }
                    continue;
                }
                if (inVerbatimString) {
                    if (c == '"') {
                        if (next == '"') {
                            ++i;
                        } else {
                            inVerbatimString = false;
                        }
                    }
                    continue;
                }
                if (inLineStr) {
                    if (c == '\\') {
                        ++i;
                    } else if (c == '"') {
                        inLineStr = false;
                    }
                    continue;
                }
                if (inChar) {
                    if (c == '\\') {
                        ++i;
                    } else if (c == '\'') {
                        inChar = false;
                    }
                    continue;
                }
                if (c == '/' && next == '/') {
                    break;
                }
                if (c == '/' && next == '*') {
                    inBlockComment = true;
                    ++i;
                    continue;
                }
                if (c == '@' && next == '"') {
                    inVerbatimString = true;
                    ++i;
                    continue;
                }
                if (c == '$' && next == '@' && i + 2 < line.size() && line[i + 2] == '"') {
                    inVerbatimString = true;
                    i += 2;
                    continue;
                }
                if (c == '"') {
                    inLineStr = true;
                    continue;
                }
                if (c == '\'') {
                    inChar = true;
                    continue;
                }
            }

            out.append(line);
            if (hasNewline) {
                out.push_back('\n');
            }
        }

        if (!hasNewline) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return out;
}

enum class TokKind : std::uint8_t {
    Identifier = 0,
    Number,
    String,
    Char,
    Punct
};

struct CompatToken final {
    TokKind kind = TokKind::Punct;
    std::string trivia;
    std::string text;
};

[[nodiscard]] std::string NormalizeNumericLiteral(std::string_view raw) {
    if (raw.empty()) {
        return {};
    }
    const bool isHex = raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X');
    if (isHex) {
        return std::string(raw);
    }
    std::string s(raw);
    if (s.size() > 1) {
        const char back = s.back();
        if (back == 'd' || back == 'D' || back == 'm' || back == 'M') {
            s.pop_back();
        }
    }
    return s;
}

[[nodiscard]] std::vector<CompatToken> TokenizeCSharp(std::string_view src) {
    std::vector<CompatToken> tokens;
    std::string pendingTrivia;

    std::size_t i = 0;
    const std::size_t n = src.size();

    auto emit = [&](TokKind k, std::string txt) {
        CompatToken t;
        t.kind = k;
        t.trivia = std::move(pendingTrivia);
        pendingTrivia.clear();
        t.text = std::move(txt);
        tokens.push_back(std::move(t));
    };

    while (i < n) {
        const char c = src[i];
        const char next = (i + 1 < n) ? src[i + 1] : '\0';

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pendingTrivia.push_back(c);
            ++i;
            continue;
        }

        // Line comment
        if (c == '/' && next == '/') {
            const std::size_t start = i;
            i += 2;
            while (i < n && src[i] != '\n') {
                ++i;
            }
            pendingTrivia.append(src.substr(start, i - start));
            continue;
        }

        // Block comment
        if (c == '/' && next == '*') {
            const std::size_t start = i;
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
                ++i;
            }
            i = std::min(i + 2, n);
            pendingTrivia.append(src.substr(start, i - start));
            continue;
        }

        // Interpolated or verbatim strings: @"...", $"...", $@"...", @$"..."
        if ((c == '@' && next == '"') ||
            (c == '$' && next == '"') ||
            ((c == '$' && next == '@') && i + 2 < n && src[i + 2] == '"') ||
            ((c == '@' && next == '$') && i + 2 < n && src[i + 2] == '"')) {
            const std::size_t start = i;
            bool verbatim = false;
            bool interpolated = false;
            while (i < n && (src[i] == '@' || src[i] == '$')) {
                if (src[i] == '@') verbatim = true;
                if (src[i] == '$') interpolated = true;
                ++i;
            }
            if (i < n && src[i] == '"') {
                ++i;
            }
            int holeBraceDepth = 0;
            while (i < n) {
                if (interpolated && holeBraceDepth > 0) {
                    if (src[i] == '@' && i + 1 < n && src[i + 1] == '"') {
                        i += 2;
                        while (i < n) {
                            if (src[i] == '"') {
                                if (i + 1 < n && src[i + 1] == '"') {
                                    i += 2;
                                    continue;
                                }
                                ++i;
                                break;
                            }
                            ++i;
                        }
                        continue;
                    }
                    if (src[i] == '"' || src[i] == '\'') {
                        const char q = src[i++];
                        while (i < n) {
                            if (src[i] == '\\' && i + 1 < n) {
                                i += 2;
                                continue;
                            }
                            if (src[i] == q) {
                                ++i;
                                break;
                            }
                            ++i;
                        }
                        continue;
                    }
                    if (src[i] == '{') {
                        ++holeBraceDepth;
                        ++i;
                        continue;
                    }
                    if (src[i] == '}') {
                        --holeBraceDepth;
                        ++i;
                        continue;
                    }
                    ++i;
                    continue;
                }
                if (interpolated && src[i] == '{') {
                    if (i + 1 < n && src[i + 1] == '{') {
                        i += 2;
                        continue;
                    }
                    ++holeBraceDepth;
                    ++i;
                    continue;
                }
                if (verbatim) {
                    if (src[i] == '"') {
                        if (i + 1 < n && src[i + 1] == '"') {
                            i += 2;
                            continue;
                        }
                        ++i;
                        break;
                    }
                } else {
                    if (src[i] == '\\' && i + 1 < n) {
                        i += 2;
                        continue;
                    }
                    if (src[i] == '"') {
                        ++i;
                        break;
                    }
                }
                ++i;
            }
            emit(TokKind::String, std::string(src.substr(start, i - start)));
            continue;
        }

        // Standard string literal
        if (c == '"') {
            const std::size_t start = i++;
            while (i < n) {
                if (src[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (src[i] == '"') {
                    ++i;
                    break;
                }
                ++i;
            }
            emit(TokKind::String, std::string(src.substr(start, i - start)));
            continue;
        }

        // Char literal
        if (c == '\'') {
            const std::size_t start = i++;
            while (i < n) {
                if (src[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (src[i] == '\'') {
                    ++i;
                    break;
                }
                ++i;
            }
            emit(TokKind::Char, std::string(src.substr(start, i - start)));
            continue;
        }

        // Identifier (including @keyword escape)
        if (c == '@' && IsIdentStart(next)) {
            const std::size_t start = ++i;
            while (i < n && IsIdentCont(src[i])) {
                ++i;
            }
            emit(TokKind::Identifier, std::string(src.substr(start, i - start)));
            continue;
        }
        if (IsIdentStart(c)) {
            const std::size_t start = i++;
            while (i < n && IsIdentCont(src[i])) {
                ++i;
            }
            emit(TokKind::Identifier, std::string(src.substr(start, i - start)));
            continue;
        }

        // Numeric literal
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(next)) != 0)) {
            const std::size_t start = i;
            if (c == '0' && (next == 'x' || next == 'X')) {
                i += 2;
                while (i < n && (std::isxdigit(static_cast<unsigned char>(src[i])) != 0 || src[i] == '_')) {
                    ++i;
                }
            } else {
                while (i < n && (std::isdigit(static_cast<unsigned char>(src[i])) != 0 || src[i] == '_')) {
                    ++i;
                }
                if (i < n && src[i] == '.' && i + 1 < n &&
                    std::isdigit(static_cast<unsigned char>(src[i + 1])) != 0) {
                    ++i;
                    while (i < n && (std::isdigit(static_cast<unsigned char>(src[i])) != 0 || src[i] == '_')) {
                        ++i;
                    }
                }
                if (i < n && (src[i] == 'e' || src[i] == 'E')) {
                    ++i;
                    if (i < n && (src[i] == '+' || src[i] == '-')) {
                        ++i;
                    }
                    while (i < n && std::isdigit(static_cast<unsigned char>(src[i])) != 0) {
                        ++i;
                    }
                }
                while (i < n && std::isalpha(static_cast<unsigned char>(src[i])) != 0) {
                    ++i;
                }
            }
            emit(TokKind::Number, NormalizeNumericLiteral(src.substr(start, i - start)));
            continue;
        }

        // Two-character punctuators
        if ((c == ':' && next == ':') || (c == '=' && next == '>') ||
            (c == '?' && next == '?') || (c == '?' && next == '.')) {
            emit(TokKind::Punct, std::string(src.substr(i, 2)));
            i += 2;
            continue;
        }

        emit(TokKind::Punct, std::string(1, c));
        ++i;
    }

    if (!pendingTrivia.empty()) {
        CompatToken trailing;
        trailing.trivia = std::move(pendingTrivia);
        tokens.push_back(std::move(trailing));
    }
    return tokens;
}

[[nodiscard]] bool IsDroppedDotNetNamespace(std::string_view ns) {
    static constexpr std::string_view kDroppedPrefixes[] = {
        "System",
        "Microsoft",
        "Windows",
        "Noesis",
        "NoesisGUIExtensions",
        "Xamarin",
        "CommunityToolkit"
    };
    for (const auto prefix : kDroppedPrefixes) {
        if (ns == prefix) {
            return true;
        }
        if (ns.size() > prefix.size() &&
            ns.substr(0, prefix.size()) == prefix &&
            ns[prefix.size()] == '.') {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ShouldImportWpfBridge(std::string_view ns) {
    return ns == "System.Windows" ||
           aero::str::StartsWith(ns, "System.Windows.") ||
           ns == "System.Collections.ObjectModel" ||
           ns == "Noesis" ||
           aero::str::StartsWith(ns, "Noesis.") ||
           ns == "Aero.UI" ||
           aero::str::StartsWith(ns, "Aero.UI.");
}

[[nodiscard]] bool IsWpfOrUiCompatSymbol(std::string_view tok) {
    return tok == "ObservableCollection" ||
           tok == "ViewModelBase" ||
           tok == "DelegateCommand" ||
           tok == "RelayCommand" ||
           tok == "ICommand" ||
           tok == "IValueConverter" ||
           tok == "IMultiValueConverter" ||
           tok == "Window" ||
           tok == "Page" ||
           tok == "UserControl" ||
           tok == "DependencyObject" ||
           tok == "UIElement" ||
           tok == "FrameworkElement";
}

} // namespace

std::string LowerCSharpCompatSyntax(
    std::string_view source,
    const std::unordered_set<std::string>* availableModules) {
    const std::string normalized = StripBomAndCr(source);
    const std::string preprocessed = PreprocessDirectives(normalized);
    std::vector<CompatToken> tokens = TokenizeCSharp(preprocessed);

    // Count real tokens (excluding trailing trivia sentinel if empty text)
    const std::size_t n = tokens.size();
    auto hasText = [&](std::size_t idx) -> bool {
        return idx < n && !tokens[idx].text.empty();
    };

    std::string declaredModule;
    std::vector<std::string> imports;
    std::unordered_set<std::string> seenImports;
    bool sawWpfUsing = false;

    auto addImport = [&](std::string modName) {
        if (modName.empty()) {
            return;
        }
        if (seenImports.insert(modName).second) {
            imports.push_back(std::move(modName));
        }
    };

    // Track matching close braces of unwrapped `namespace X { ... }` blocks
    struct ScopeFrame {
        bool isUnwrappedNamespace = false;
    };
    std::vector<ScopeFrame> braceStack;
    std::vector<bool> keepToken(n, true);
    bool insideTopLevelHeader = true;

    for (std::size_t i = 0; i < n; ++i) {
        if (!hasText(i)) {
            continue;
        }
        const std::string& tok = tokens[i].text;

        // Track brace frames
        if (tok == "{") {
            braceStack.push_back(ScopeFrame{false});
            continue;
        }
        if (tok == "}") {
            if (!braceStack.empty()) {
                const bool unwrap = braceStack.back().isUnwrappedNamespace;
                braceStack.pop_back();
                if (unwrap) {
                    keepToken[i] = false;
                }
            }
            continue;
        }

        // Preserve leading doc/comments cleanly even if tokens are erased
        // 1. Existing `module Foo.Bar;`
        if (tok == "module" && braceStack.empty()) {
            std::size_t j = i + 1;
            std::string mod;
            while (j < n && hasText(j) && tokens[j].text != ";") {
                mod += tokens[j].text;
                keepToken[j] = false;
                ++j;
            }
            keepToken[i] = false;
            if (j < n && tokens[j].text == ";") {
                keepToken[j] = false;
            }
            if (declaredModule.empty() && !mod.empty()) {
                declaredModule = std::move(mod);
            }
            i = j;
            continue;
        }

        // 2. Existing `import Foo.Bar;`
        if (tok == "import" && braceStack.empty()) {
            std::size_t j = i + 1;
            std::string mod;
            while (j < n && hasText(j) && tokens[j].text != ";") {
                mod += tokens[j].text;
                keepToken[j] = false;
                ++j;
            }
            keepToken[i] = false;
            if (j < n && tokens[j].text == ";") {
                keepToken[j] = false;
            }
            addImport(std::move(mod));
            i = j;
            continue;
        }

        // 3. C# `using ...;` directive (outside parens: NEVER rewrite `using (var x = ...)`!)
        const bool canBeUsingDirective =
            braceStack.empty() ||
            std::all_of(braceStack.begin(), braceStack.end(), [](const ScopeFrame& f) {
                return f.isUnwrappedNamespace;
            });
        if (tok == "using" && canBeUsingDirective && i + 1 < n && hasText(i + 1) &&
            tokens[i + 1].text != "(") {
            std::size_t j = i + 1;
            if (j < n && (tokens[j].text == "static" || tokens[j].text == "global")) {
                keepToken[j] = false;
                ++j;
            }
            // Collect optional alias `Alias =`
            std::size_t scanEq = j;
            bool hasAlias = false;
            while (scanEq < n && hasText(scanEq) && tokens[scanEq].text != ";") {
                if (tokens[scanEq].text == "=") {
                    hasAlias = true;
                    break;
                }
                if (tokens[scanEq].text == "{" || tokens[scanEq].text == "}") {
                    break;
                }
                ++scanEq;
            }
            if (hasAlias) {
                while (j <= scanEq) {
                    keepToken[j] = false;
                    ++j;
                }
            }

            std::string nsName;
            while (j < n && hasText(j) && tokens[j].text != ";") {
                if (tokens[j].text == "global" && j + 1 < n && tokens[j + 1].text == "::") {
                    keepToken[j] = false;
                    keepToken[j + 1] = false;
                    j += 2;
                    continue;
                }
                nsName += tokens[j].text;
                keepToken[j] = false;
                ++j;
            }
            keepToken[i] = false;
            if (j < n && tokens[j].text == ";") {
                keepToken[j] = false;
            }

            if (ShouldImportWpfBridge(nsName)) {
                sawWpfUsing = true;
            }
            if (nsName == "Aero.UI" || nsName == "Aero.Native") {
                addImport(nsName);
            } else if (!IsDroppedDotNetNamespace(nsName)) {
                if (availableModules == nullptr || availableModules->count(nsName) > 0) {
                    addImport(std::move(nsName));
                }
            }
            i = j;
            continue;
        }

        // 4. `namespace Foo.Bar { ... }` or file-scoped `namespace Foo.Bar;`
        if (tok == "namespace" && canBeUsingDirective) {
            keepToken[i] = false;
            std::size_t j = i + 1;
            std::string nsName;
            while (j < n && hasText(j) && tokens[j].text != "{" && tokens[j].text != ";") {
                nsName += tokens[j].text;
                keepToken[j] = false;
                ++j;
            }
            if (declaredModule.empty() && !nsName.empty()) {
                declaredModule = std::move(nsName);
            }
            if (j < n && tokens[j].text == ";") {
                keepToken[j] = false;
                i = j;
            } else if (j < n && tokens[j].text == "{") {
                keepToken[j] = false;
                braceStack.push_back(ScopeFrame{true});
                i = j;
            }
            continue;
        }

        // 5. Top-level `[assembly: ...]` / `[module: ...]` attributes -> drop
        if (insideTopLevelHeader && tok == "[" && i + 2 < n &&
            (tokens[i + 1].text == "assembly" || tokens[i + 1].text == "module") &&
            tokens[i + 2].text == ":") {
            int depth = 1;
            keepToken[i] = false;
            std::size_t j = i + 1;
            while (j < n && depth > 0) {
                keepToken[j] = false;
                if (tokens[j].text == "[") ++depth;
                else if (tokens[j].text == "]") --depth;
                ++j;
            }
            i = (j > 0) ? (j - 1) : i;
            continue;
        }

        insideTopLevelHeader = false;

        // 6. Strip `global::`
        if (tok == "global" && i + 1 < n && tokens[i + 1].text == "::") {
            keepToken[i] = false;
            keepToken[i + 1] = false;
            ++i;
            continue;
        }

        // 7. Rewrite `[ObservableObject]` -> `[Observable]`
        if (tok == "ObservableObject" && i > 0 && tokens[i - 1].text == "[" &&
            i + 1 < n && tokens[i + 1].text == "]") {
            tokens[i].text = "Observable";
            continue;
        }

        // 8. Rewrite `nameof(a.b.Identifier)` -> `"Identifier"`
        if (tok == "nameof" && i + 1 < n && tokens[i + 1].text == "(") {
            std::size_t j = i + 2;
            int parens = 1;
            int angles = 0;
            std::string lastIdent;
            while (j < n && parens > 0) {
                if (tokens[j].text == "(") {
                    ++parens;
                } else if (tokens[j].text == ")") {
                    --parens;
                    if (parens == 0) {
                        break;
                    }
                } else if (parens == 1 && tokens[j].text == "<") {
                    ++angles;
                } else if (parens == 1 && tokens[j].text == ">" && angles > 0) {
                    --angles;
                } else if (parens == 1 && angles == 0 && tokens[j].kind == TokKind::Identifier) {
                    lastIdent = tokens[j].text;
                }
                ++j;
            }
            if (parens == 0 && !lastIdent.empty()) {
                tokens[i].kind = TokKind::String;
                tokens[i].text = "\"" + lastIdent + "\"";
                for (std::size_t k = i + 1; k <= j; ++k) {
                    keepToken[k] = false;
                }
                i = j;
                continue;
            }
        }

        // 9. Rewrite `string.Empty` / `String.Empty` -> `""`
        if ((tok == "string" || tok == "String") &&
            !(i > 0 && tokens[i - 1].text == ".") &&
            i + 2 < n && tokens[i + 1].text == "." && tokens[i + 2].text == "Empty") {
            tokens[i].kind = TokKind::String;
            tokens[i].text = "\"\"";
            keepToken[i + 1] = false;
            keepToken[i + 2] = false;
            i += 2;
            continue;
        }

        // 10. Lower `{ get; private set; }` / `{ get; protected set; }` / `{ get; internal set; }`
        // and `{ get; init; }` inside property blocks
        if ((tok == "private" || tok == "protected" || tok == "internal") &&
            i + 2 < n &&
            (tokens[i + 1].text == "get" || tokens[i + 1].text == "set" || tokens[i + 1].text == "init") &&
            (tokens[i + 2].text == ";" || tokens[i + 2].text == "{" || tokens[i + 2].text == "=>")) {
            keepToken[i] = false;
            continue;
        }
        if (tok == "init" && i + 1 < n &&
            (tokens[i + 1].text == ";" || tokens[i + 1].text == "{" || tokens[i + 1].text == "=>")) {
            tokens[i].text = "set";
            continue;
        }

        // 11. Strip `readonly` directly following `ref` in `ref readonly T`
        if (tok == "readonly" && i > 0 && tokens[i - 1].text == "ref") {
            keepToken[i] = false;
            continue;
        }

        // 12. Auto-import Aero.UI when WPF/MVVM collection or UI base types appear
        if (IsWpfOrUiCompatSymbol(tok)) {
            sawWpfUsing = true;
        }
    }

    if (sawWpfUsing && seenImports.count("Aero.UI") == 0) {
        addImport("Aero.UI");
    }

    // Reconstruct lowered source: module declaration first, then imports, then lowered declarations.
    // Comments belonging to stripped header tokens are preserved cleanly.
    std::string header;
    if (!declaredModule.empty()) {
        header += "module " + declaredModule + ";\n";
    }
    for (const auto& imp : imports) {
        if (!declaredModule.empty() && imp == declaredModule) {
            continue;
        }
        header += "import " + imp + ";\n";
    }
    if (!header.empty()) {
        header.push_back('\n');
    }

    std::string body;
    body.reserve(preprocessed.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!keepToken[i]) {
            // Preserve comments inside stripped trivia while avoiding piles of blank whitespace
            if (!tokens[i].trivia.empty()) {
                for (char ch : tokens[i].trivia) {
                    if (ch == '\n') {
                        body.push_back('\n');
                    }
                }
            }
            continue;
        }
        body.append(tokens[i].trivia);
        body.append(tokens[i].text);
    }

    // Trim excessive leading blank newlines on body before combining with header
    std::size_t trimStart = 0;
    while (trimStart < body.size() && body[trimStart] == '\n') {
        ++trimStart;
    }

    if (header.empty()) {
        return body;
    }
    return header + body.substr(trimStart);
}

} // namespace aero
