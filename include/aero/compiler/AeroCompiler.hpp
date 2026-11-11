#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroSceneIR.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/RsAliases.hpp"
#include "realscript/compiler/Compilation.h"

namespace aero {

using SourceFile = rs_comp::SourceFile;

using ExternalXamlResolverFn = std::function<bool(
    std::string_view requestedSource,
    std::string_view currentXamlPath,
    std::string& outResolvedPath,
    std::string& outXamlContent)>;

struct AeroCompileInput {
    std::string xamlPath = "MainView.xaml";
    std::string xamlContent;
    std::vector<SourceFile> scriptSources;
    bool emitAotCpp = false;
    std::string aotCppNamespace = "aero::aot_generated";
    std::string aotViewBuilderName;

    // P1-2: Optional cross-file ResourceDictionary.MergedDictionaries resolver & search roots
    ExternalXamlResolverFn externalXamlResolver{};
    std::string resourceSearchRoot;
    std::vector<std::string> resourceSearchPaths;
    bool autoMergeGlobalResources = true;
};

struct AeroViewAotCppOutput {
    std::string cppNamespace = "aero::aot_generated";
    std::string builderFunctionName = "BuildAeroView";
    std::string header;
    std::string source;
    std::string scriptAotHeader;
    std::string scriptAotSource;
    std::uint64_t scriptAotContentHash = 0;
};

struct AeroCompileResult {
    bool succeeded = false;
    std::string generatedPartialRsPath;
    std::string generatedPartialRsCode;
    AeroSceneGraphIR sceneIr{};
    AxbPackage axbPackage;
    std::vector<std::uint8_t> axbBytes;
    std::vector<rs_bc::Module> bytecodeModules;
    std::shared_ptr<const ProgramImage> programImage;
    AeroViewAotCppOutput aotCpp;
    std::vector<std::string> diagnostics;
};

class AeroCompiler {
public:
    AeroCompiler();

    // Parse XAML in a single pass, synthesize the strongly-typed *.g.rs partial class stub,
    // compile all RealScript sources + *.g.rs into a ProgramImage, resolve multi-hop
    // PropertySlotHandles and event handler SymbolIds, and emit the binary .axb4 package
    // (plus optional zero-XML C++17 AOT visual tree & RealScript AOT C++17 source).
    [[nodiscard]] AeroCompileResult Compile(const AeroCompileInput& input) const;

    // Transpile a pre-compiled AxbPackage directly into zero-XML-parser pure C++17 batch
    // visual tree and dual-track binding construction code (--aot-cpp mode).
    [[nodiscard]] static AeroViewAotCppOutput GenerateAotCppFromPackage(
        const AxbPackage& package,
        std::string_view cppNamespace = "aero::aot_generated",
        std::string_view builderFunctionName = {});

    // Resolve or re-bind a pre-parsed AxbPackage against an updated ProgramImage (used in < 5ms Hot Reload).
    static bool BindPackageAgainstProgram(
        AxbPackage& package,
        const ProgramImage& program,
        std::string_view rootDataTypeName = {},
        const ExecutionDomainContext* domains = nullptr);

    // Map property name (e.g. "Width", "Text", "Grid.Row", "Canvas.Left") to BuiltinPropertyId.
    [[nodiscard]] static std::uint16_t ResolvePropertyId(std::string_view propertyName) noexcept;

    // Parse literal string attribute into UnifiedValue based on target property ID.
    [[nodiscard]] static UnifiedValue ParseLiteralPropertyValue(
        std::uint16_t propId,
        std::string_view rawValue);

private:
    [[nodiscard]] AeroCompileResult CompileInternal(
        const AeroCompileInput& input,
        std::vector<std::string>& activeIncludeStack) const;
};

} // namespace aero
