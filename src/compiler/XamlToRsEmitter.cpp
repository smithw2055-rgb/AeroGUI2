#include "aero/compiler/AeroCompilerV3.hpp"

namespace aero {

std::string AeroCompilerV3::EmitPartialRsStub(
    std::string_view xamlPath,
    std::string_view xamlContent,
    std::vector<std::string>* outDiagnostics) {
    (void)xamlPath;
    (void)xamlContent;
    (void)outDiagnostics;
    return {};
}

} // namespace aero
