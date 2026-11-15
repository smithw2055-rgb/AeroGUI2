#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace aero {

// Line-preserving C# compatibility lowering pass executed at AeroGUI2 L2 before
// handing script sources to the clean RealScript L0 compiler:
//   1. Strips UTF-8 BOM and blanks #region / #endregion / #pragma / #nullable lines.
//   2. Rewrites contextual nameof(...) expressions into string literals of the
//      rightmost identifier (preserving internal newlines).
//   3. Rewrites file-/block-scoped namespace declarations and non-BCL using
//      directives into RealScript `module` + `import` headers (ensuring `module`
//      precedes all `import`s) and blanks .NET BCL / non-existent module usings.
std::string LowerCSharpCompatSyntax(
    std::string_view source,
    const std::unordered_set<std::string>* availableModules = nullptr);

}  // namespace aero
