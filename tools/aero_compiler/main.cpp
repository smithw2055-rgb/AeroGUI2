#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/compiler/StdlibDllAbi.h"
#include "aero/compiler/StdlibDllLoader.hpp"
#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/core/StringUtils.hpp"
#include "realscript/Version.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/Compilation.h"
#include "realscript/compiler/PrecompiledModule.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/diagnostics/Diagnostic.h"
#include "realscript/optimization/Optimizer.h"

namespace {

[[nodiscard]] bool ReadFileText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

[[nodiscard]] bool WriteFileBytes(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

[[nodiscard]] std::string ReadEnvString(const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }
#ifdef _MSC_VER
    char* buf = nullptr;
    std::size_t len = 0u;
    std::string out;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        if (buf[0] != '\0') {
            out = buf;
        }
        std::free(buf);
    }
    return out;
#else
    if (const char* val = std::getenv(name); val != nullptr && val[0] != '\0') {
        return std::string(val);
    }
    return {};
#endif
}

[[nodiscard]] std::filesystem::path ResolveExecutableDir(const char* argv0) {
#if defined(_WIN32)
    wchar_t modulePath[MAX_PATH * 4]{};
    const DWORD len = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (len > 0u && len < std::size(modulePath)) {
        return std::filesystem::path(modulePath).parent_path();
    }
#endif
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        const auto absPath = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec && absPath.has_parent_path()) {
            return absPath.parent_path();
        }
        const std::filesystem::path fallback(argv0);
        if (fallback.has_parent_path()) {
            return fallback.parent_path();
        }
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

void PrintUsage() {
    std::printf(
        "aero-compiler v3.0 - AeroGUI XAML & RealScript AOT/Bytecode Compiler\n"
        "Usage: aero-compiler [options]\n"
        "  --xaml <file.xaml>        Input XAML file (repeatable; first or MainWindow is primary)\n"
        "  --rs <file.rs|file.cs>    Input RealScript / C#-compat script file (repeatable)\n"
        "  --out-rs <file.g.rs>      Write generated partial .g.rs code to file\n"
        "  --out-dir <dir>           Output directory for bytecode bundle & generated files\n"
        "  --aot-cpp <stem|dir>      Emit C++17 AOT header/source (<stem>.h / <stem>.cpp)\n"
        "  --aot-ns <ns>             C++ namespace for AOT output (default: aero::aot_generated)\n"
        "  --emit-stdlib-dll-cpp <f> Emit self-contained stdlib DLL C++ data unit\n"
        "  --O2                      Enable aggressive MIR optimization\n"
        "  --O0                      Disable MIR optimization (for --emit-stdlib-dll-cpp)\n"
        "  --no-stdlib               Do not auto-load Aero.Native / Aero.UI stdlib pack\n"
        "  --stdlib <dir>            Path to stdlib root directory\n"
        "  --cache-dir <dir>         Directory for persistent stdlib module cache\n"
        "  --no-cache                Disable persistent disk cache (overrides AERO_CACHE_DIR)\n"
        "  -h, --help                Show this help\n");
}

[[nodiscard]] std::string RewriteAotHeaderInclude(
    std::string source,
    std::string_view headerFilename) {
    const std::string target = "#include \"realscript_aot_generated.h\"";
    const std::size_t pos = source.find(target);
    if (pos != std::string::npos) {
        source.replace(pos, target.size(), "#include \"" + std::string(headerFilename) + "\"");
    }
    return source;
}

void AppendWordArrayDefinition(
    std::ostringstream& out,
    const char* varName,
    const std::vector<std::uint8_t>& bytes) {
    const std::size_t wordCount = (bytes.size() + 7u) / 8u;
    out << "alignas(8) static const std::uint64_t " << varName << "["
        << (wordCount > 0u ? wordCount : 1u) << "] = {";
    if (wordCount == 0u) {
        out << "0ULL";
    } else {
        for (std::size_t w = 0u; w < wordCount; ++w) {
            if (w % 6u == 0u) {
                out << "\n    ";
            } else {
                out << " ";
            }
            std::uint64_t word = 0u;
            for (std::size_t b = 0u; b < 8u; ++b) {
                const std::size_t idx = w * 8u + b;
                if (idx < bytes.size()) {
                    word |= static_cast<std::uint64_t>(bytes[idx]) << (b * 8u);
                }
            }
            char hexBuf[32]{};
            std::snprintf(hexBuf, sizeof(hexBuf), "0x%016llxULL,",
                static_cast<unsigned long long>(word));
            out << hexBuf;
        }
        out << "\n";
    }
    out << "};\n\n";
}

[[nodiscard]] int EmitStdlibDllCppFile(
    std::string_view outCppPath,
    std::string_view explicitStdlibDir,
    bool enableO2) {
    const std::vector<aero::SourceFile> stdlibSources =
        aero::LoadStdlibModulePack(explicitStdlibDir);
    if (stdlibSources.empty()) {
        std::cerr << "aero-compiler: failed to locate stdlib Aero.Native / Aero.UI sources\n";
        return 1;
    }

    aero::rs_comp::CompilationOptions compOpts;
    compOpts.allowDefaultBuiltinPacks = true;
    compOpts.capturePrecompiledFrontend = true;
    compOpts.populateSnapshot = false;
    compOpts.collectSymbolOccurrences = false;
    compOpts.emitDebugInfo = false;
    compOpts.macroPlugins.push_back(aero::rs_comp::createStandardReactiveMacroPlugin());

    const aero::rs_comp::Compilation compilation(stdlibSources, std::move(compOpts));
    aero::rs_comp::BuildResult buildRes = compilation.build(nullptr);
    for (const auto& d : buildRes.diagnostics.items()) {
        const char* sev = d.severity == aero::rs_diag::DiagnosticSeverity::Error ? "error" : "warning";
        std::cerr << (d.sourceName.empty() ? std::string("<stdlib>") : d.sourceName)
                  << ": " << sev << " " << d.code << ": " << d.message << "\n";
    }
    if (buildRes.diagnostics.hasErrors() || !buildRes.precompiledFrontend) {
        std::cerr << "aero-compiler: stdlib compilation failed for DLL emission\n";
        return 1;
    }

    const auto hasModule = [&](std::string_view modName) {
        return buildRes.precompiledFrontend->findModule(modName) != nullptr;
    };
    if (!hasModule("RealScript.Math") || !hasModule("Aero.Native") || !hasModule("Aero.UI")) {
        std::cerr << "aero-compiler: stdlib pack missing required module (RealScript.Math, Aero.Native, or Aero.UI)\n";
        return 1;
    }

    // CRITICAL: strip Aero.Native dummy MIR bodies BEFORE O2 optimizer so calls keep native hooks intact.
    aero::StripAeroNativeMirBodies(buildRes.modules);

    if (enableO2) {
        aero::rs_opt::Options optOpts;
        optOpts.level = aero::rs_opt::Level::Aggressive;
        optOpts.maximumIterations = 4;
        optOpts.preserveDebugInfo = false;
        optOpts.enableInlining = true;
        optOpts.enableLicm = true;
        optOpts.enableStructFieldForwarding = true;
        optOpts.enableDevirtualization = true;

        aero::rs_diag::DiagnosticBag optDiag;
        const aero::rs_opt::Optimizer optimizer;
        auto optRes = optimizer.optimize(std::move(buildRes.modules), optDiag, optOpts);
        for (const auto& d : optDiag.items()) {
            const char* sev = d.severity == aero::rs_diag::DiagnosticSeverity::Error ? "error" : "warning";
            std::cerr << d.sourceName << ": " << sev << " " << d.code << ": " << d.message << "\n";
        }
        if (optDiag.hasErrors()) {
            std::cerr << "aero-compiler: MIR O2 optimization failed for stdlib DLL\n";
            return 1;
        }
        buildRes.modules = std::move(optRes.modules);
    }

    for (auto& mirMod : buildRes.modules) {
        mirMod.sourceFiles.clear();
        for (auto& fn : mirMod.functions) {
            fn.debugInfo.sourceName.clear();
            fn.debugInfo.sourceFileId = 0u;
            fn.debugInfo.declaration = {};
            fn.debugInfo.body = {};
            fn.debugInfo.sequencePoints.clear();
            fn.debugInfo.locals.clear();
        }
    }

    aero::rs_bc::Lowerer lowerer;
    std::vector<aero::rs_bc::Module> bcModules;
    bcModules.reserve(buildRes.modules.size());
    std::string moduleListCsv;
    for (const auto& mirMod : buildRes.modules) {
        auto bcMod = lowerer.lower(mirMod);
        aero::rs_diag::DiagnosticBag bcDiag;
        if (!aero::rs_bc::verifyModule(bcMod, bcDiag) || bcDiag.hasErrors()) {
            for (const auto& d : bcDiag.items()) {
                std::cerr << d.sourceName << ": " << d.code << ": " << d.message << "\n";
            }
            std::cerr << "aero-compiler: bytecode verification failed for stdlib module "
                      << mirMod.name << "\n";
            return 1;
        }
        if (!moduleListCsv.empty()) {
            moduleListCsv.push_back(',');
        }
        moduleListCsv.append(bcMod.name);
        bcModules.push_back(std::move(bcMod));
    }

    const std::vector<std::uint8_t> frontendBytes =
        aero::rs_comp::encodePrecompiledFrontendPack(*buildRes.precompiledFrontend);
    const std::vector<std::uint8_t> lazyBytes =
        aero::rs_bc::encodeLazyBytecodeBundle(bcModules, 24576u);

    const std::uint64_t stdlibHash = aero::ComputeStdlibPackHash(stdlibSources, true);
    const std::uint64_t frontendChecksum =
        aero::ComputeStdlibDllChecksum64(frontendBytes.data(), frontendBytes.size());
    const std::uint64_t lazyChecksum =
        aero::ComputeStdlibDllChecksum64(lazyBytes.data(), lazyBytes.size());
    const std::uint32_t flags =
        AERO_STDLIB_FLAG_REACTIVE_MACROS | (enableO2 ? AERO_STDLIB_FLAG_OPTIMIZED_O2 : 0u);

    std::ostringstream cpp;
    cpp << "// Auto-generated by aero-compiler --emit-stdlib-dll-cpp. Do not edit manually.\n"
        << "#include \"aero/compiler/StdlibDllAbi.h\"\n"
        << "#include <cstdint>\n\n"
        << "namespace {\n\n";
    AppendWordArrayDefinition(cpp, "kFrontendPackWords", frontendBytes);
    AppendWordArrayDefinition(cpp, "kLazyBytecodeWords", lazyBytes);
    cpp << "const char kModuleListCsv[] = \"" << moduleListCsv << "\";\n\n"
        << "const AeroStdlibDllHeader kStdlibHeader = {\n"
        << "    AERO_STDLIB_DLL_MAGIC,\n"
        << "    AERO_STDLIB_DLL_ABI_VERSION,\n"
        << "    static_cast<std::uint32_t>(sizeof(AeroStdlibDllHeader)),\n"
        << "    " << flags << "u,\n"
        << "    " << realscript::kVersionMajor << "u,\n"
        << "    " << realscript::kVersionMinor << "u,\n"
        << "    " << realscript::kVersionPatch << "u,\n"
        << "    " << realscript::kSdkCompatibilityVersion << "u,\n"
        << "    " << stdlibHash << "ULL,\n"
        << "    " << frontendChecksum << "ULL,\n"
        << "    " << lazyChecksum << "ULL,\n"
        << "    reinterpret_cast<const std::uint8_t*>(kFrontendPackWords),\n"
        << "    " << frontendBytes.size() << "ULL,\n"
        << "    reinterpret_cast<const std::uint8_t*>(kLazyBytecodeWords),\n"
        << "    " << lazyBytes.size() << "ULL,\n"
        << "    kModuleListCsv\n"
        << "};\n\n"
        << "} // namespace\n\n"
        << "extern \"C\" AERO_STDLIB_API const AeroStdlibDllHeader* aero_stdlib_get_header(void) {\n"
        << "    return &kStdlibHeader;\n"
        << "}\n";

    const std::string cppText = cpp.str();
    const std::filesystem::path outPath(outCppPath);
    std::string existing;
    if (ReadFileText(outPath, existing) && existing == cppText) {
        std::printf(
            "aero-compiler: stdlib DLL C++ unchanged (%zu frontend B, %zu lazy bytecode B, opt=%s)\n",
            frontendBytes.size(),
            lazyBytes.size(),
            enableO2 ? "O2" : "O0");
        return 0;
    }

    if (!WriteFileBytes(outPath, cppText.data(), cppText.size())) {
        std::cerr << "Failed to write stdlib DLL C++ file: " << outCppPath << "\n";
        return 1;
    }
    std::printf(
        "aero-compiler: emitted stdlib DLL C++ (%zu modules, %zu frontend B, %zu lazy bytecode B, opt=%s)\n",
        bcModules.size(),
        frontendBytes.size(),
        lazyBytes.size(),
        enableO2 ? "O2" : "O0");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    aero::AeroV3CompileInput input;
    std::vector<std::string> xamlFilePaths;
    std::vector<std::string> scriptPaths;
    std::string outRsPath;
    std::string outDirPath;
    std::string aotCppPath;
    std::string emitStdlibDllCppPath;
    std::string explicitCacheDir;
    bool noCache = false;
    bool optO0 = false;
    bool optO2 = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto needNext = [&](std::string_view opt) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for option " << opt << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
        if (arg == "--xaml") {
            const char* v = needNext(arg);
            if (!v) return 2;
            xamlFilePaths.emplace_back(v);
        } else if (arg == "--rs") {
            const char* v = needNext(arg);
            if (!v) return 2;
            scriptPaths.emplace_back(v);
        } else if (arg == "--out-rs") {
            const char* v = needNext(arg);
            if (!v) return 2;
            outRsPath = v;
        } else if (arg == "--out-dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            outDirPath = v;
        } else if (arg == "--aot-cpp") {
            const char* v = needNext(arg);
            if (!v) return 2;
            aotCppPath = v;
            input.emitAotCpp = true;
        } else if (arg == "--aot-ns") {
            const char* v = needNext(arg);
            if (!v) return 2;
            input.aotCppNamespace = v;
        } else if (arg == "--emit-stdlib-dll-cpp") {
            const char* v = needNext(arg);
            if (!v) return 2;
            emitStdlibDllCppPath = v;
        } else if (arg == "--O2" || arg == "-O2") {
            optO2 = true;
            optO0 = false;
            input.optimizeBytecode = true;
        } else if (arg == "--O0" || arg == "-O0") {
            optO0 = true;
            optO2 = false;
            input.optimizeBytecode = false;
        } else if (arg == "--stdlib") {
            const char* v = needNext(arg);
            if (!v) return 2;
            input.stdlibRootDir = v;
        } else if (arg == "--no-stdlib") {
            input.includeStdlib = false;
        } else if (arg == "--cache-dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            explicitCacheDir = v;
        } else if (arg == "--no-cache") {
            noCache = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    if (!emitStdlibDllCppPath.empty()) {
        const bool enableStdlibO2 = optO0 ? false : (optO2 ? true : true);
        return EmitStdlibDllCppFile(emitStdlibDllCppPath, input.stdlibRootDir, enableStdlibO2);
    }

    if (xamlFilePaths.empty() && scriptPaths.empty()) {
        PrintUsage();
        return 0;
    }

    if (noCache) {
        input.allowDiskCache = false;
        input.cacheDir.clear();
    } else if (!explicitCacheDir.empty()) {
        input.cacheDir = explicitCacheDir;
    } else {
        const std::string envCache = ReadEnvString("AERO_CACHE_DIR");
        if (!envCache.empty()) {
            input.cacheDir = envCache;
        } else {
            const std::filesystem::path exeDir = ResolveExecutableDir(argc > 0 ? argv[0] : nullptr);
            if (!exeDir.empty()) {
                input.cacheDir = (exeDir / "aero_stdlib_cache").lexically_normal().generic_string();
            }
        }
    }

    if (!xamlFilePaths.empty()) {
        std::size_t primaryIdx = 0u;
        for (std::size_t i = 0u; i < xamlFilePaths.size(); ++i) {
            const std::string fname = std::filesystem::path(xamlFilePaths[i]).filename().string();
            if (aero::str::EqualsIgnoreCase(fname, "MainWindow.xaml")) {
                primaryIdx = i;
                break;
            }
        }
        for (std::size_t i = 0u; i < xamlFilePaths.size(); ++i) {
            const std::filesystem::path xp(xamlFilePaths[i]);
            std::string content;
            if (!ReadFileText(xp, content)) {
                std::cerr << "Failed to read XAML file: " << xamlFilePaths[i] << "\n";
                return 1;
            }
            const std::string normPath = xp.lexically_normal().generic_string();
            if (i == primaryIdx) {
                input.xamlPath = normPath;
                if (input.resourceSearchRoot.empty() && xp.has_parent_path()) {
                    input.resourceSearchRoot = xp.parent_path().lexically_normal().generic_string();
                }
                input.xamlContent = std::move(content);
            } else {
                input.additionalXamlSources.push_back(aero::SourceFile{normPath, std::move(content)});
            }
        }
    }

    for (const auto& sp : scriptPaths) {
        std::string text;
        if (!ReadFileText(sp, text)) {
            std::cerr << "Failed to read script file: " << sp << "\n";
            return 1;
        }
        aero::SourceFile sf;
        sf.path = std::filesystem::path(sp).lexically_normal().generic_string();
        sf.content = std::move(text);
        input.scriptSources.push_back(std::move(sf));
    }

    input.compilerSession = nullptr;

    const aero::AeroCompilerV3 compiler;
    const aero::AeroV3CompileResult result = compiler.Compile(input);

    for (const auto& diag : result.diagnostics) {
        std::cerr << diag << "\n";
    }

    if (!result.succeeded) {
        return 1;
    }

    if (!outRsPath.empty() && !result.generatedPartialRsCode.empty()) {
        if (!WriteFileBytes(outRsPath, result.generatedPartialRsCode.data(), result.generatedPartialRsCode.size())) {
            std::cerr << "Failed to write generated .g.rs: " << outRsPath << "\n";
            return 1;
        }
    }

    if (!outDirPath.empty()) {
        const std::filesystem::path outDir(outDirPath);
        if (!result.generatedPartialRsCode.empty()) {
            const auto rsName = std::filesystem::path(result.generatedPartialRsPath).filename();
            (void)WriteFileBytes(
                outDir / rsName,
                result.generatedPartialRsCode.data(),
                result.generatedPartialRsCode.size());
        }
        if (!result.programImageBytes.empty()) {
            const std::string stem = !result.rootClassName.empty()
                ? result.rootClassName
                : std::string("program");
            (void)WriteFileBytes(
                outDir / (stem + ".rsb"),
                result.programImageBytes.data(),
                result.programImageBytes.size());
        }
    }

    if (input.emitAotCpp) {
        std::filesystem::path baseStem(aotCppPath);
        std::error_code ec;
        if (std::filesystem::is_directory(baseStem, ec)) {
            baseStem /= "aero_aot_generated";
        } else if (!outDirPath.empty() && !baseStem.has_parent_path()) {
            baseStem = std::filesystem::path(outDirPath) / baseStem;
        }
        const std::string ext = baseStem.extension().string();
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
            baseStem.replace_extension();
        }
        const std::filesystem::path headerPath = baseStem.string() + ".h";
        const std::filesystem::path sourcePath = baseStem.string() + ".cpp";
        const std::string headerFilename = headerPath.filename().generic_string();
        const std::string cppText = RewriteAotHeaderInclude(
            result.aotCpp.scriptAotSource, headerFilename);

        if (!WriteFileBytes(headerPath, result.aotCpp.scriptAotHeader.data(), result.aotCpp.scriptAotHeader.size()) ||
            !WriteFileBytes(sourcePath, cppText.data(), cppText.size())) {
            std::cerr << "Failed to write AOT C++ outputs at " << baseStem.string() << "\n";
            return 1;
        }
    }

    std::printf("aero-compiler: succeeded (%zu bytecode modules, %zu bytes)\n",
        result.bytecodeModules.size(), result.programImageBytes.size());
    return 0;
}
