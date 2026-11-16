#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

enum class MarkupExtensionKind : std::uint8_t {
    None = 0,
    EscapedLiteral,
    Binding,
    MultiBinding,
    TemplateBinding,
    StaticResource,
    DynamicResource,
    RelativeSource,
    XType,
    XNull,
    XStatic,
    NoesisLoc,
    Generic
};

struct MarkupExtensionNode;

struct MarkupArgValue final {
    bool isExtension = false;
    std::string literal;
    std::shared_ptr<MarkupExtensionNode> extension;
};

struct MarkupExtensionArg final {
    std::string name; // Empty for positional arguments
    MarkupArgValue value;
};

struct MarkupExtensionNode final {
    MarkupExtensionKind kind = MarkupExtensionKind::None;
    std::string extensionName;
    std::string literalValue;
    std::vector<MarkupExtensionArg> arguments;
    std::string errorMessage;

    [[nodiscard]] bool IsOk() const noexcept { return errorMessage.empty(); }
    [[nodiscard]] const MarkupArgValue* FindNamed(std::string_view name) const noexcept;
    [[nodiscard]] const MarkupArgValue* FindPositional(std::size_t index = 0) const noexcept;
};

struct ParsedBindingSpec final {
    std::string path;
    std::string elementName;
    std::string mode;
    std::string updateSourceTrigger;
    std::string converterKey;
    std::string converterParam;
    std::string stringFormat;
    std::string fallbackValue;
    bool hasFallbackValue = false;
    std::string targetNullValue;
    bool hasTargetNullValue = false;
    bool isTemplateBinding = false;
    bool isMultiBinding = false;
    bool hasRelativeSource = false;
    std::string relativeSourceMode;
    std::string ancestorType;
    int ancestorLevel = 1;
    std::vector<ParsedBindingSpec> childBindings;
};

[[nodiscard]] bool LooksLikeMarkupExtension(std::string_view text) noexcept;

/// Parses a XAML attribute string as a markup extension (or plain literal / `{}` escape).
[[nodiscard]] MarkupExtensionNode ParseMarkupExtension(std::string_view text);

/// Extracts structured binding properties from a parsed `{Binding}`, `{TemplateBinding}`,
/// or `{MultiBinding}` AST node.
[[nodiscard]] bool TryExtractBindingSpec(
    const MarkupExtensionNode& node,
    ParsedBindingSpec& outSpec);

} // namespace aero
