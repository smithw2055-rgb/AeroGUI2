#include "aero/compiler/AeroCompilerV3.hpp"

namespace aero {

std::string LowerCSharpCompatSyntax(
    std::string_view source,
    const std::unordered_set<std::string>* availableModules) {
    (void)availableModules;
    return std::string(source);
}

} // namespace aero
