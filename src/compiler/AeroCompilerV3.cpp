#include "aero/compiler/AeroCompilerV3.hpp"

namespace aero {

AeroV3CompileResult AeroCompilerV3::Compile(const AeroV3CompileInput& input) const {
    AeroV3CompileResult res{};
    res.diagnostics.push_back(
        "AeroCompilerV3::Compile is a Phase-0 stub; full compilation will land in Phase-2.");
    (void)input;
    return res;
}

} // namespace aero
