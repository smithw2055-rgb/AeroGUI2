#include "aero/compiler/MarkupExtensionParser.hpp"

#include <cctype>
#include <cstdlib>

#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] bool IsWs(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

[[nodiscard]] MarkupExtensionKind ClassifyExtensionName(std::string_view name) {
    if (name == "Binding") return MarkupExtensionKind::Binding;
    if (name == "MultiBinding") return MarkupExtensionKind::MultiBinding;
    if (name == "TemplateBinding") return MarkupExtensionKind::TemplateBinding;
    if (name == "StaticResource") return MarkupExtensionKind::StaticResource;
    if (name == "DynamicResource") return MarkupExtensionKind::DynamicResource;
    if (name == "RelativeSource") return MarkupExtensionKind::RelativeSource;
    if (name == "x:Type" || name == "Type") return MarkupExtensionKind::XType;
    if (name == "x:Null" || name == "Null") return MarkupExtensionKind::XNull;
    if (name == "x:Static" || name == "Static") return MarkupExtensionKind::XStatic;
    if (name == "noesis:Loc" || name == "Loc") return MarkupExtensionKind::NoesisLoc;
    return MarkupExtensionKind::Generic;
}

class ExtensionReader final {
public:
    explicit ExtensionReader(std::string_view text) : text_(text) {}

    [[nodiscard]] MarkupExtensionNode ParseTopLevel() {
        const std::string_view trimmed = aero::str::Trim(text_);
        if (trimmed.empty() || trimmed[0] != '{') {
            MarkupExtensionNode plain;
            plain.kind = MarkupExtensionKind::None;
            plain.literalValue = std::string(text_);
            return plain;
        }
        if (trimmed.size() >= 2 && trimmed[0] == '{' && trimmed[1] == '}') {
            MarkupExtensionNode escaped;
            escaped.kind = MarkupExtensionKind::EscapedLiteral;
            escaped.literalValue = std::string(trimmed.substr(2));
            return escaped;
        }

        text_ = trimmed;
        pos_ = 0;
        return ParseExtensionObject();
    }

private:
    [[nodiscard]] bool AtEnd() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char Peek(std::size_t off = 0) const noexcept {
        return (pos_ + off < text_.size()) ? text_[pos_ + off] : '\0';
    }
    char Advance() noexcept { return AtEnd() ? '\0' : text_[pos_++]; }

    void SkipWhitespace() noexcept {
        while (!AtEnd() && IsWs(Peek())) {
            ++pos_;
        }
    }

    [[nodiscard]] std::string ReadIdentifier() {
        std::string id;
        while (!AtEnd()) {
            const char ch = Peek();
            if (IsWs(ch) || ch == ',' || ch == '}' || ch == '=' || ch == '{') {
                break;
            }
            id.push_back(Advance());
        }
        return id;
    }

    [[nodiscard]] std::string ReadQuotedString(char quote, std::string& outErr) {
        std::string out;
        bool closed = false;
        while (!AtEnd()) {
            const char ch = Advance();
            if (ch == '\\' && !AtEnd()) {
                out.push_back(Advance());
                continue;
            }
            if (ch == quote) {
                closed = true;
                break;
            }
            out.push_back(ch);
        }
        if (!closed) {
            outErr = "Unterminated quoted string inside markup extension";
        }
        return out;
    }

    [[nodiscard]] MarkupArgValue ReadArgValue(std::string& outErr) {
        SkipWhitespace();
        MarkupArgValue val;
        if (AtEnd()) {
            return val;
        }

        if (Peek() == '\'' || Peek() == '"') {
            const char quote = Advance();
            val.literal = ReadQuotedString(quote, outErr);
            return val;
        }

        if (Peek() == '{') {
            if (Peek(1) == '}') {
                pos_ += 2;
                std::string raw;
                int braceDepth = 0;
                int parenDepth = 0;
                int bracketDepth = 0;
                while (!AtEnd()) {
                    const char ch = Peek();
                    if (ch == '{') {
                        ++braceDepth;
                    } else if (ch == '}') {
                        if (braceDepth == 0 && parenDepth == 0 && bracketDepth == 0) {
                            break;
                        }
                        if (braceDepth > 0) --braceDepth;
                    } else if (ch == '(') {
                        ++parenDepth;
                    } else if (ch == ')' && parenDepth > 0) {
                        --parenDepth;
                    } else if (ch == '[') {
                        ++bracketDepth;
                    } else if (ch == ']' && bracketDepth > 0) {
                        --bracketDepth;
                    } else if (ch == ',' && braceDepth == 0 && parenDepth == 0 && bracketDepth == 0) {
                        break;
                    }
                    raw.push_back(Advance());
                }
                val.literal = std::string(aero::str::Trim(raw));
                return val;
            }
            auto child = std::make_shared<MarkupExtensionNode>(ParseExtensionObject());
            if (!child->errorMessage.empty() && outErr.empty()) {
                outErr = child->errorMessage;
            }
            val.isExtension = true;
            val.extension = std::move(child);
            return val;
        }

        std::string raw;
        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        while (!AtEnd()) {
            const char ch = Peek();
            if (ch == '{') ++braceDepth;
            else if (ch == '}' && braceDepth > 0) --braceDepth;
            else if (ch == '(') ++parenDepth;
            else if (ch == ')' && parenDepth > 0) --parenDepth;
            else if (ch == '[') ++bracketDepth;
            else if (ch == ']' && bracketDepth > 0) --bracketDepth;
            else if (braceDepth == 0 && parenDepth == 0 && bracketDepth == 0 && (ch == ',' || ch == '}')) {
                break;
            }
            raw.push_back(Advance());
        }
        val.literal = std::string(aero::str::Trim(raw));
        return val;
    }

    [[nodiscard]] bool LooksLikeNamedAssignmentHere() const noexcept {
        std::size_t scan = pos_;
        while (scan < text_.size()) {
            const char ch = text_[scan];
            if (ch == '=') {
                return true;
            }
            if (ch == ',' || ch == '}' || ch == '{' || ch == '\'' || ch == '"' || IsWs(ch)) {
                return false;
            }
            ++scan;
        }
        return false;
    }

    MarkupExtensionNode ParseExtensionObject() {
        MarkupExtensionNode node;
        if (AtEnd() || Advance() != '{') {
            node.errorMessage = "Expected '{' starting markup extension";
            return node;
        }

        SkipWhitespace();
        node.extensionName = ReadIdentifier();
        if (node.extensionName.empty()) {
            node.errorMessage = "Expected markup extension type name after '{'";
            return node;
        }
        node.kind = ClassifyExtensionName(node.extensionName);

        for (;;) {
            SkipWhitespace();
            if (AtEnd()) {
                node.errorMessage = "Unterminated markup extension '{" + node.extensionName + "'";
                return node;
            }
            if (Peek() == '}') {
                (void)Advance();
                break;
            }

            MarkupExtensionArg arg;
            std::string err;
            if (LooksLikeNamedAssignmentHere()) {
                arg.name = ReadIdentifier();
                SkipWhitespace();
                if (Peek() == '=') {
                    (void)Advance();
                }
                arg.value = ReadArgValue(err);
            } else {
                arg.value = ReadArgValue(err);
            }

            if (!err.empty()) {
                node.errorMessage = std::move(err);
                return node;
            }
            node.arguments.push_back(std::move(arg));

            SkipWhitespace();
            if (Peek() == ',') {
                (void)Advance();
                continue;
            }
            if (Peek() == '}') {
                (void)Advance();
                break;
            }
            if (AtEnd()) {
                node.errorMessage = "Unterminated markup extension '{" + node.extensionName + "'";
                return node;
            }
            node.errorMessage = "Unexpected character inside markup extension '{" +
                                node.extensionName + "'";
            return node;
        }

        return node;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

[[nodiscard]] std::string ArgToString(const MarkupArgValue& val) {
    if (!val.isExtension || !val.extension) {
        return val.literal;
    }
    const auto& ext = *val.extension;
    if (ext.kind == MarkupExtensionKind::StaticResource ||
        ext.kind == MarkupExtensionKind::DynamicResource ||
        ext.kind == MarkupExtensionKind::XType ||
        ext.kind == MarkupExtensionKind::XStatic ||
        ext.kind == MarkupExtensionKind::NoesisLoc) {
        if (const auto* pos0 = ext.FindPositional(0)) {
            return ArgToString(*pos0);
        }
        if (const auto* rk = ext.FindNamed("ResourceKey")) {
            return ArgToString(*rk);
        }
        if (const auto* tn = ext.FindNamed("TypeName")) {
            return ArgToString(*tn);
        }
    }
    return {};
}

void PopulateRelativeSourceSpec(
    const MarkupExtensionNode& relNode,
    ParsedBindingSpec& outSpec) {
    outSpec.hasRelativeSource = true;
    if (const auto* pos0 = relNode.FindPositional(0)) {
        outSpec.relativeSourceMode = pos0->literal;
    }
    if (const auto* modeArg = relNode.FindNamed("Mode")) {
        outSpec.relativeSourceMode = modeArg->literal;
    }
    if (const auto* ancType = relNode.FindNamed("AncestorType")) {
        outSpec.ancestorType = ArgToString(*ancType);
    }
    if (const auto* ancLvl = relNode.FindNamed("AncestorLevel")) {
        const int parsed = std::atoi(ancLvl->literal.c_str());
        if (parsed > 0) {
            outSpec.ancestorLevel = parsed;
        }
    }
    if (outSpec.relativeSourceMode.empty() && !outSpec.ancestorType.empty()) {
        outSpec.relativeSourceMode = "FindAncestor";
    }
}

} // namespace

const MarkupArgValue* MarkupExtensionNode::FindNamed(std::string_view name) const noexcept {
    for (const auto& arg : arguments) {
        if (!arg.name.empty() && aero::str::EqualsIgnoreCase(arg.name, name)) {
            return &arg.value;
        }
    }
    return nullptr;
}

const MarkupArgValue* MarkupExtensionNode::FindPositional(std::size_t index) const noexcept {
    std::size_t current = 0;
    for (const auto& arg : arguments) {
        if (arg.name.empty()) {
            if (current == index) {
                return &arg.value;
            }
            ++current;
        }
    }
    return nullptr;
}

bool LooksLikeMarkupExtension(std::string_view text) noexcept {
    const std::string_view trimmed = aero::str::Trim(text);
    return !trimmed.empty() && trimmed.front() == '{';
}

MarkupExtensionNode ParseMarkupExtension(std::string_view text) {
    ExtensionReader reader(text);
    return reader.ParseTopLevel();
}

bool TryExtractBindingSpec(
    const MarkupExtensionNode& node,
    ParsedBindingSpec& outSpec) {
    outSpec = ParsedBindingSpec{};
    if (!node.IsOk()) {
        return false;
    }

    if (node.kind == MarkupExtensionKind::TemplateBinding) {
        outSpec.isTemplateBinding = true;
        if (const auto* pos0 = node.FindPositional(0)) {
            outSpec.path = ArgToString(*pos0);
        }
        if (const auto* propArg = node.FindNamed("Property")) {
            outSpec.path = ArgToString(*propArg);
        }
        if (const auto* convArg = node.FindNamed("Converter")) {
            outSpec.converterKey = ArgToString(*convArg);
        }
        if (const auto* paramArg = node.FindNamed("ConverterParameter")) {
            outSpec.converterParam = ArgToString(*paramArg);
        }
        return !outSpec.path.empty();
    }

    if (node.kind == MarkupExtensionKind::MultiBinding) {
        outSpec.isMultiBinding = true;
        if (const auto* convArg = node.FindNamed("Converter")) {
            outSpec.converterKey = ArgToString(*convArg);
        }
        if (const auto* fmtArg = node.FindNamed("StringFormat")) {
            outSpec.stringFormat = ArgToString(*fmtArg);
        }
        if (const auto* modeArg = node.FindNamed("Mode")) {
            outSpec.mode = modeArg->literal;
        }
        return true;
    }

    if (node.kind != MarkupExtensionKind::Binding) {
        return false;
    }

    if (const auto* pos0 = node.FindPositional(0)) {
        outSpec.path = ArgToString(*pos0);
    }
    if (const auto* pathArg = node.FindNamed("Path")) {
        outSpec.path = ArgToString(*pathArg);
    }
    if (const auto* elArg = node.FindNamed("ElementName")) {
        outSpec.elementName = ArgToString(*elArg);
    }
    if (const auto* srcArg = node.FindNamed("Source")) {
        outSpec.hasSourceLiteral = true;
        outSpec.sourceLiteral = ArgToString(*srcArg);
        if (srcArg->isExtension && srcArg->extension &&
            (srcArg->extension->kind == MarkupExtensionKind::StaticResource ||
             srcArg->extension->kind == MarkupExtensionKind::DynamicResource)) {
            outSpec.sourceResourceKey = ArgToString(*srcArg);
        }
    }
    if (const auto* modeArg = node.FindNamed("Mode")) {
        outSpec.mode = modeArg->literal;
    }
    if (const auto* ustArg = node.FindNamed("UpdateSourceTrigger")) {
        outSpec.updateSourceTrigger = ustArg->literal;
    }
    if (const auto* convArg = node.FindNamed("Converter")) {
        outSpec.converterKey = ArgToString(*convArg);
    }
    if (const auto* paramArg = node.FindNamed("ConverterParameter")) {
        outSpec.converterParam = ArgToString(*paramArg);
    }
    if (const auto* fmtArg = node.FindNamed("StringFormat")) {
        outSpec.stringFormat = ArgToString(*fmtArg);
    }
    if (const auto* fbArg = node.FindNamed("FallbackValue")) {
        outSpec.hasFallbackValue = true;
        outSpec.fallbackValue = ArgToString(*fbArg);
    }
    if (const auto* tnvArg = node.FindNamed("TargetNullValue")) {
        outSpec.hasTargetNullValue = true;
        outSpec.targetNullValue = ArgToString(*tnvArg);
    }
    if (const auto* relArg = node.FindNamed("RelativeSource")) {
        if (relArg->isExtension && relArg->extension &&
            relArg->extension->kind == MarkupExtensionKind::RelativeSource) {
            PopulateRelativeSourceSpec(*relArg->extension, outSpec);
        } else {
            outSpec.hasRelativeSource = true;
            outSpec.relativeSourceMode = relArg->literal;
        }
    }

    return true;
}

} // namespace aero
