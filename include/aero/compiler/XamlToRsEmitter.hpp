#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroCompilerV3.hpp"

namespace aero {

struct XamlEmitOptions final {
    std::string xamlPath = "MainView.xaml";
    std::string defaultModuleName;
    ExternalXamlResolverFn externalXamlResolver{};
    std::string resourceSearchRoot;
    std::vector<std::string> resourceSearchPaths;
};

struct XamlEmitResult final {
    bool succeeded = false;
    std::string generatedFilePath;
    std::string generatedRsCode;
    std::string moduleName;
    std::string rootClassName;
    std::string dataType;
    std::vector<std::string> diagnostics;
};

/// Parses a XAML document (plus any merged external ResourceDictionaries)
/// and emits deterministic, strongly-typed RealScript `.g.rs` code targeting `Aero.UI`.
[[nodiscard]] XamlEmitResult EmitXamlToRealScript(
    std::string_view xamlContent,
    const XamlEmitOptions& options = {});

} // namespace aero
