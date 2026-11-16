#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroCompilerV3.hpp"
#include "aero/core/StringUtils.hpp"

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

void PrintUsage() {
    std::printf(
        "aero-compiler v3.0 - AeroGUI XAML & RealScript AOT/Bytecode Compiler\n"
        "Usage: aero-compiler [options]\n"
        "  --xaml <file.xaml>        Input XAML file\n"
        "  --rs <file.rs|file.cs>    Input RealScript / C#-compat script file (repeatable)\n"
        "  --out-rs <file.g.rs>      Write generated partial .g.rs code to file\n"
        "  --out-dir <dir>           Output directory for bytecode bundle & generated files\n"
        "  --aot-cpp <stem|dir>      Emit C++17 AOT header/source (<stem>.h / <stem>.cpp)\n"
        "  --aot-ns <ns>             C++ namespace for AOT output (default: aero::aot_generated)\n"
        "  --O2                      Enable aggressive MIR optimization\n"
        "  --no-stdlib               Do not auto-load Aero.Native / Aero.UI stdlib pack\n"
        "  --stdlib <dir>            Path to stdlib root directory\n"
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

} // namespace

int main(int argc, char** argv) {
    aero::AeroV3CompileInput input;
    std::vector<std::string> scriptPaths;
    std::string xamlFilePath;
    std::string outRsPath;
    std::string outDirPath;
    std::string aotCppPath;

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
            xamlFilePath = v;
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
        } else if (arg == "--O2" || arg == "-O2") {
            input.optimizeBytecode = true;
        } else if (arg == "--stdlib") {
            const char* v = needNext(arg);
            if (!v) return 2;
            input.stdlibRootDir = v;
        } else if (arg == "--no-stdlib") {
            input.includeStdlib = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    if (xamlFilePath.empty() && scriptPaths.empty()) {
        PrintUsage();
        return 0;
    }

    if (!xamlFilePath.empty()) {
        const std::filesystem::path xp(xamlFilePath);
        input.xamlPath = xp.lexically_normal().generic_string();
        if (input.resourceSearchRoot.empty() && xp.has_parent_path()) {
            input.resourceSearchRoot = xp.parent_path().lexically_normal().generic_string();
        }
        if (!ReadFileText(xp, input.xamlContent)) {
            std::cerr << "Failed to read XAML file: " << xamlFilePath << "\n";
            return 1;
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

    aero::rs_comp::CompilerSession session;
    input.compilerSession = &session;

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
