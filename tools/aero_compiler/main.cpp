#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "realscript/bytecode/Bytecode.h"

namespace {

bool ReadTextFile(const std::string& path, std::string& outContent) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outContent = ss.str();
    return true;
}

bool WriteBinaryFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool WriteTextFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

int RunSelfTest() {
    aero::AeroCompiler compiler;
    aero::AeroCompileInput input{};
    input.xamlPath = "SelfTestHud.xaml";
    input.xamlContent = R"(
<UserControl x:Class="Game.UI.SelfTestHud" Width="400" Height="120">
    <StackPanel x:Name="RootStack" Spacing="8">
        <TextBlock x:Name="TitleLabel" Text="{Binding Title}" FontSize="18" />
        <Button x:Name="ActionBtn" Content="Confirm" Click="OnConfirm" Width="120" Height="36" />
    </StackPanel>
</UserControl>
)";
    input.scriptSources.push_back(aero::SourceFile{
        "SelfTestHud.rs",
        R"(
module Game.UI;

public partial class SelfTestHud
{
    [Observable]
    public string Title { get; set; }

    public int ConfirmClicks;

    public SelfTestHud()
    {
        Title = "Aero Compiler Ready";
        ConfirmClicks = 0;
    }

    public void OnConfirm()
    {
        ConfirmClicks = ConfirmClicks + 1;
    }
}
)"});

    input.emitAotCpp = true;
    const auto res = compiler.Compile(input);
    if (!res.succeeded || res.axbBytes.empty() || !res.programImage ||
        res.aotCpp.header.empty() || res.aotCpp.source.empty() ||
        res.aotCpp.scriptAotSource.empty()) {
        std::cerr << "[aero-compiler] Self-test failed\n";
        for (const auto& d : res.diagnostics) {
            std::cerr << "  " << d << "\n";
        }
        return 1;
    }

    aero::AxbPackage decoded{};
    std::string errMsg;
    if (!aero::AxbPackage::Deserialize(res.axbBytes, decoded, &errMsg)) {
        std::cerr << "[aero-compiler] AXB4 deserialize failed: " << errMsg << "\n";
        return 1;
    }

    std::cout << "[aero-compiler] Self-test passed: generated "
              << res.generatedPartialRsPath << ", " << res.axbBytes.size()
              << " bytes of .axb4, and " << res.aotCpp.source.size()
              << " bytes of C++17 AOT view builder\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string xamlPath;
    std::string inAxbPath;
    std::vector<std::string> rsPaths;
    std::string outAxbPath;
    std::string outGrsPath;
    std::string outRsbcPath;
    std::string outAotCppPrefix;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test") {
            return RunSelfTest();
        } else if (arg == "--xaml" && i + 1 < argc) {
            xamlPath = argv[++i];
        } else if (arg == "--axb" && i + 1 < argc) {
            inAxbPath = argv[++i];
        } else if (arg == "--rs" && i + 1 < argc) {
            rsPaths.push_back(argv[++i]);
        } else if (arg == "--out-axb" && i + 1 < argc) {
            outAxbPath = argv[++i];
        } else if (arg == "--out-grs" && i + 1 < argc) {
            outGrsPath = argv[++i];
        } else if (arg == "--out-rsbc" && i + 1 < argc) {
            outRsbcPath = argv[++i];
        } else if (arg == "--aot-cpp" && i + 1 < argc) {
            outAotCppPrefix = argv[++i];
        }
    }

    if (!inAxbPath.empty() && !outAotCppPrefix.empty() && xamlPath.empty()) {
        std::ifstream in(inAxbPath, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to read AXB4 file: " << inAxbPath << "\n";
            return 1;
        }
        const std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        aero::AxbPackage pkg{};
        std::string err;
        if (!aero::AxbPackage::Deserialize(bytes, pkg, &err)) {
            std::cerr << "Failed to decode AXB4 file: " << err << "\n";
            return 1;
        }
        const auto aotOut = aero::AeroCompiler::GenerateAotCppFromPackage(pkg);
        if (!WriteTextFile(outAotCppPrefix + ".hpp", aotOut.header) ||
            !WriteTextFile(outAotCppPrefix + ".cpp", aotOut.source)) {
            std::cerr << "Failed to write AOT C++ output: " << outAotCppPrefix << "\n";
            return 1;
        }
        return 0;
    }

    if (xamlPath.empty()) {
        std::cout << "Usage: aero-compiler --xaml <file.xaml> [--rs <file.rs> ...] "
                  << "[--out-axb <file.axb4>] [--out-grs <file.g.rs>] [--out-rsbc <file.rsbc>] "
                  << "[--aot-cpp <output_prefix>] [--self-test]\n";
        return 1;
    }

    aero::AeroCompileInput input{};
    input.xamlPath = xamlPath;
    input.emitAotCpp = !outAotCppPrefix.empty();
    if (!ReadTextFile(xamlPath, input.xamlContent)) {
        std::cerr << "Failed to read XAML file: " << xamlPath << "\n";
        return 1;
    }

    for (const auto& rsPath : rsPaths) {
        std::string code;
        if (!ReadTextFile(rsPath, code)) {
            std::cerr << "Failed to read RealScript file: " << rsPath << "\n";
            return 1;
        }
        input.scriptSources.push_back(aero::SourceFile{rsPath, std::move(code)});
    }

    aero::AeroCompiler compiler;
    const auto res = compiler.Compile(input);
    for (const auto& d : res.diagnostics) {
        std::cerr << d << "\n";
    }
    if (!res.succeeded) {
        return 1;
    }

    if (!outGrsPath.empty() && !res.generatedPartialRsCode.empty()) {
        if (!WriteTextFile(outGrsPath, res.generatedPartialRsCode)) {
            std::cerr << "Failed to write generated partial class: " << outGrsPath << "\n";
            return 1;
        }
    }
    if (!outAxbPath.empty()) {
        if (!WriteBinaryFile(outAxbPath, res.axbBytes)) {
            std::cerr << "Failed to write AXB4 package: " << outAxbPath << "\n";
            return 1;
        }
    }
    if (!outRsbcPath.empty() && !res.bytecodeModules.empty()) {
        const auto bytes = aero::rs_bc::encodeModule(res.bytecodeModules.back());
        if (!WriteBinaryFile(outRsbcPath, bytes)) {
            std::cerr << "Failed to write RSBC module: " << outRsbcPath << "\n";
            return 1;
        }
    }
    if (!outAotCppPrefix.empty()) {
        if (!WriteTextFile(outAotCppPrefix + ".hpp", res.aotCpp.header) ||
            !WriteTextFile(outAotCppPrefix + ".cpp", res.aotCpp.source)) {
            std::cerr << "Failed to write AOT C++ view files: " << outAotCppPrefix << "\n";
            return 1;
        }
        if (!res.aotCpp.scriptAotSource.empty()) {
            (void)WriteTextFile(outAotCppPrefix + "_script.hpp", res.aotCpp.scriptAotHeader);
            (void)WriteTextFile(outAotCppPrefix + "_script.cpp", res.aotCpp.scriptAotSource);
        }
    }

    return 0;
}
