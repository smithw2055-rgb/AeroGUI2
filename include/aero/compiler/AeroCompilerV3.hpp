#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "realscript/compiler/Compilation.h"

namespace aero {

using SourceFile = rs_comp::SourceFile;

[[nodiscard]] std::string LowerCSharpCompatSyntax(
    std::string_view source,
    const std::unordered_set<std::string>* availableModules = nullptr);

[[nodiscard]] std::vector<SourceFile> LoadStdlibModulePack(
    std::string_view stdlibRootDir = {});

using ExternalXamlResolverFn = std::function<bool(
    std::string_view requestedSource,
    std::string_view currentXamlPath,
    std::string& outResolvedPath,
    std::string& outXamlContent)>;

struct AeroV3CompileInput {
    std::string xamlPath = "MainView.xaml";
    std::string xamlContent;
    std::vector<SourceFile> additionalXamlSources;
    std::vector<SourceFile> scriptSources;
    std::string stdlibRootDir;
    bool includeStdlib = true;
    bool useStdlibDll = false;
    std::string stdlibDllPath;
    bool retainStdlibDllCache = true;
    bool treeShakeStdlib = true;
    std::string cacheDir;
    bool allowDiskCache = true;
    bool allowStdlibCache = true;
    bool emitAotCpp = false;
    bool aotTrustedReleaseOnly = true;
    std::string aotCppNamespace = "aero::aot_generated";
    std::string aotViewBuilderName;

    ExternalXamlResolverFn externalXamlResolver{};
    std::string resourceSearchRoot;
    std::vector<std::string> resourceSearchPaths;

    bool optimizeBytecode = false;
    bool skipRawBytecodeOutput = false;
    bool retainBytecodeModules = true;
    rs_comp::CompilerSession* compilerSession = nullptr;
    bool enableReactiveMacros = true;
};

struct AeroV3AotCppOutput {
    std::string cppNamespace = "aero::aot_generated";
    std::string builderFunctionName = "BuildAeroView";
    std::string scriptAotHeader;
    std::string scriptAotSource;
    std::uint64_t scriptAotContentHash = 0u;
};

struct AeroV3CompileResult {
    bool succeeded = false;
    std::string generatedPartialRsPath;
    std::string generatedPartialRsCode;
    std::string rootClassName;
    std::vector<rs_bc::Module> bytecodeModules;
    std::vector<std::uint8_t> programImageBytes;
    std::shared_ptr<const ProgramImage> programImage;
    AeroV3AotCppOutput aotCpp;
    bool usedStdlibCache = false;
    bool usedStdlibDll = false;
    double stdlibDllLoadMs = 0.0;
    double compileFrontendAndMirMs = 0.0;
    double linkMs = 0.0;
    std::size_t retainedMirTypes = 0u;
    std::size_t prunedMirTypes = 0u;
    std::size_t retainedMirFunctions = 0u;
    std::size_t prunedMirFunctions = 0u;
    std::vector<std::string> diagnostics;
};

class AeroCompilerV3 final {
public:
    AeroCompilerV3() = default;
    ~AeroCompilerV3() = default;

    [[nodiscard]] AeroV3CompileResult Compile(const AeroV3CompileInput& input) const;

    [[nodiscard]] static std::string EmitPartialRsStub(
        std::string_view xamlPath,
        std::string_view xamlContent,
        std::vector<std::string>* outDiagnostics = nullptr);

    static void ClearProcessCaches() noexcept;
};

} // namespace aero
