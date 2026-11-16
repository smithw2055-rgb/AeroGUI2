#include "aero/compiler/AeroCompilerV3.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/compiler/XamlToRsEmitter.hpp"
#include "aero/core/StringUtils.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/diagnostics/Diagnostic.h"
#include "realscript/optimization/Optimizer.h"
#include "realscript/runtime/Runtime.h"

namespace aero {
namespace {

struct SessionSnapshotCache final {
    std::mutex mutex;
    std::unordered_map<const rs_comp::CompilerSession*, rs_comp::BuildSnapshot> snapshots;
};

[[nodiscard]] SessionSnapshotCache& GetSessionSnapshotCache() {
    static SessionSnapshotCache cache;
    return cache;
}

void AppendDiagnostics(
    const rs_diag::DiagnosticBag& bag,
    std::vector<std::string>& out) {
    for (const auto& entry : bag.items()) {
        const char* sev = (entry.severity == rs_diag::DiagnosticSeverity::Error)
            ? "error"
            : "warning";
        std::string line;
        if (!entry.sourceName.empty()) {
            line += entry.sourceName + ": ";
        }
        line += std::string(sev) + " " + entry.code + ": " + entry.message;
        out.push_back(std::move(line));
    }
}

[[nodiscard]] std::string SanitizeCppIdent(std::string_view raw, std::string_view fallback) {
    std::string out;
    for (unsigned char ch : raw) {
        if (std::isalnum(ch) != 0 || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0) {
        out = std::string(fallback);
    }
    return out;
}

void AppendUint32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

[[nodiscard]] std::vector<std::uint8_t> EncodeProgramBundle(
    const std::vector<rs_bc::Module>& modules) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(4096);
    bytes.push_back('A');
    bytes.push_back('R');
    bytes.push_back('B');
    bytes.push_back('C');
    AppendUint32Le(bytes, static_cast<std::uint32_t>(modules.size()));
    for (const auto& mod : modules) {
        const auto modBytes = rs_bc::encodeModule(mod);
        AppendUint32Le(bytes, static_cast<std::uint32_t>(modBytes.size()));
        bytes.insert(bytes.end(), modBytes.begin(), modBytes.end());
    }
    return bytes;
}

} // namespace

AeroV3CompileResult AeroCompilerV3::Compile(const AeroV3CompileInput& input) const {
    AeroV3CompileResult result;

    std::vector<SourceFile> stdlibSources;
    if (input.includeStdlib) {
        stdlibSources = LoadStdlibModulePack(input.stdlibRootDir);
        if (stdlibSources.empty()) {
            result.diagnostics.emplace_back(
                "StdlibModulePack: failed to locate stdlib Aero.Native / Aero.UI sources");
            return result;
        }
    }

    // Emit XAML -> .g.rs
    XamlEmitResult xamlRes;
    if (!input.xamlContent.empty()) {
        XamlEmitOptions emitOpts;
        emitOpts.xamlPath = input.xamlPath.empty() ? "MainView.xaml" : input.xamlPath;
        emitOpts.externalXamlResolver = input.externalXamlResolver;
        emitOpts.resourceSearchRoot = input.resourceSearchRoot;
        emitOpts.resourceSearchPaths = input.resourceSearchPaths;
        xamlRes = EmitXamlToRealScript(input.xamlContent, emitOpts);
        result.generatedPartialRsPath = xamlRes.generatedFilePath;
        result.generatedPartialRsCode = xamlRes.generatedRsCode;
        result.rootClassName = xamlRes.rootClassName;
        result.diagnostics.insert(
            result.diagnostics.end(),
            xamlRes.diagnostics.begin(),
            xamlRes.diagnostics.end());
        if (!xamlRes.succeeded) {
            return result;
        }
    }

    // Assemble declared modules before C# compatibility lowering
    std::unordered_set<std::string> availableModules = CollectDeclaredModuleNames(stdlibSources);
    const auto scriptDeclared = CollectDeclaredModuleNames(input.scriptSources);
    availableModules.insert(scriptDeclared.begin(), scriptDeclared.end());
    if (!xamlRes.moduleName.empty()) {
        availableModules.insert(xamlRes.moduleName);
    }

    std::vector<SourceFile> allSources;
    allSources.reserve(stdlibSources.size() + input.scriptSources.size() + 1);
    std::unordered_set<std::string> seenPaths;

    auto pushUniqueSource = [&](SourceFile sf) {
        if (sf.path.empty()) {
            sf.path = "unit_" + std::to_string(allSources.size()) + ".rs";
        }
        if (seenPaths.insert(sf.path).second) {
            allSources.push_back(std::move(sf));
        }
    };

    for (auto& sf : stdlibSources) {
        pushUniqueSource(std::move(sf));
    }
    if (!xamlRes.generatedRsCode.empty()) {
        pushUniqueSource(SourceFile{xamlRes.generatedFilePath, xamlRes.generatedRsCode});
    }
    for (const auto& sf : input.scriptSources) {
        SourceFile lowered;
        lowered.path = sf.path;
        lowered.content = LowerCSharpCompatSyntax(sf.content, &availableModules);
        pushUniqueSource(std::move(lowered));
    }

    if (allSources.empty()) {
        result.diagnostics.emplace_back("No XAML or script sources supplied to AeroCompilerV3::Compile");
        return result;
    }

    // Configure RealScript compilation
    rs_comp::CompilationOptions compOpts;
    compOpts.allowDefaultBuiltinPacks = true;
    if (input.enableReactiveMacros) {
        compOpts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
    }

    rs_comp::BuildSnapshot previousSnapshot;
    bool usePreviousSnapshot = false;
    if (input.compilerSession != nullptr) {
        compOpts.session = std::shared_ptr<rs_comp::CompilerSession>(
            input.compilerSession, [](rs_comp::CompilerSession*) {});
        if (input.compilerSession->parsedUnitCacheSize() > 0) {
            auto& cache = GetSessionSnapshotCache();
            std::lock_guard<std::mutex> lock(cache.mutex);
            const auto it = cache.snapshots.find(input.compilerSession);
            if (it != cache.snapshots.end()) {
                previousSnapshot = it->second;
                usePreviousSnapshot = true;
            }
        }
    }

    rs_comp::Compilation compilation(std::move(allSources), std::move(compOpts));
    rs_comp::BuildResult buildRes = compilation.build(
        usePreviousSnapshot ? &previousSnapshot : nullptr);
    AppendDiagnostics(buildRes.diagnostics, result.diagnostics);

    if (buildRes.diagnostics.hasErrors()) {
        return result;
    }

    if (input.compilerSession != nullptr) {
        auto& cache = GetSessionSnapshotCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.snapshots[input.compilerSession] = buildRes.snapshot;
    }

    // CRITICAL: Strip Aero.Native dummy bodies BEFORE optimizer, bytecode lowerer, and AOT emitter!
    std::vector<rs_mir::Module> mirModules = std::move(buildRes.modules);
    StripAeroNativeMirBodies(mirModules);

    if (input.optimizeBytecode) {
        rs_opt::Options optOpts;
        optOpts.level = rs_opt::Level::Aggressive;
        optOpts.maximumIterations = 4;
        optOpts.preserveDebugInfo = true;
        optOpts.enableInlining = true;
        optOpts.enableLicm = true;
        optOpts.enableStructFieldForwarding = true;
        optOpts.enableDevirtualization = true;

        rs_diag::DiagnosticBag optDiag;
        const rs_opt::Optimizer optimizer;
        auto optRes = optimizer.optimize(std::move(mirModules), optDiag, optOpts);
        mirModules = std::move(optRes.modules);
        AppendDiagnostics(optDiag, result.diagnostics);
        if (optDiag.hasErrors()) {
            return result;
        }
    }

    // Lower MIR -> Bytecode modules and verify
    rs_bc::Lowerer lowerer;
    result.bytecodeModules.reserve(mirModules.size());
    for (const auto& mirMod : mirModules) {
        rs_bc::Module bcMod = lowerer.lower(mirMod);
        rs_diag::DiagnosticBag bcDiag;
        if (!rs_bc::verifyModule(bcMod, bcDiag)) {
            AppendDiagnostics(bcDiag, result.diagnostics);
            return result;
        }
        result.bytecodeModules.push_back(std::move(bcMod));
    }

    result.programImageBytes = EncodeProgramBundle(result.bytecodeModules);

    // Link ProgramImage
    rs_rt::RuntimeError linkErr;
    auto linked = ProgramImage::link(result.bytecodeModules, linkErr);
    if (!linked.has_value()) {
        result.diagnostics.push_back(
            std::string("ProgramImage link error ") + rs_rt::errorCodeName(linkErr.code) +
            ": " + linkErr.message);
        return result;
    }
    result.programImage = std::make_shared<const ProgramImage>(std::move(*linked));

    // Optional C++17 AOT emission
    if (input.emitAotCpp) {
        const std::string builderName = !input.aotViewBuilderName.empty()
            ? input.aotViewBuilderName
            : (!result.rootClassName.empty()
                   ? ("Build" + SanitizeCppIdent(result.rootClassName, "AeroView"))
                   : std::string("BuildAeroView"));
        const std::string progName = rs_aot::sanitizeCppIdentifier(builderName);
        const std::string cppNs = input.aotCppNamespace.empty()
            ? std::string("aero::aot_generated")
            : input.aotCppNamespace;

        rs_aot::GenerationOptions aotOpts;
        aotOpts.programName = progName;
        aotOpts.cppNamespace = cppNs;
        aotOpts.emitLineDirectives = false;
        aotOpts.enableSharedReferenceGenerics = true;

        rs_diag::DiagnosticBag aotDiag;
        const rs_aot::CppGenerator cppGenerator;
        auto generated = cppGenerator.generate(mirModules, aotDiag, aotOpts);
        AppendDiagnostics(aotDiag, result.diagnostics);
        if (aotDiag.hasErrors()) {
            return result;
        }

        result.aotCpp.cppNamespace = cppNs;
        result.aotCpp.builderFunctionName = builderName;
        result.aotCpp.scriptAotHeader = std::move(generated.header);
        result.aotCpp.scriptAotSource = std::move(generated.source);
        result.aotCpp.scriptAotContentHash = generated.contentHash;
    }

    result.succeeded = true;
    return result;
}

} // namespace aero
