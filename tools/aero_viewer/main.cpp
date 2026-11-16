#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/app/AeroAppHost.hpp"
#include "aero/core/StringUtils.hpp"

namespace {

void PrintUsage() {
    std::printf(
        "aero-viewer v3.0 - AeroGUI 3.0 Sample & Composition Viewer\n"
        "Usage: aero-viewer [options]\n"
        "  --dir <sample_dir>      Load sample directory (.xaml + .rs/.cs files)\n"
        "  --xaml <file.xaml>      Input XAML file (optional; repeatable via script inputs)\n"
        "  --rs <file.rs|file.cs>  Input RealScript / C#-compat file (repeatable)\n"
        "  --entry <ClassOrFn>     Optional entry class name or factory function\n"
        "  --headless              Run headlessly using SoftwareRasterizer\n"
        "  --frames <N>            Step N frames before exit/screenshot (headless default: 1)\n"
        "  --width <W>             Viewport width (default: 1280)\n"
        "  --height <H>            Viewport height (default: 800)\n"
        "  --out <screenshot.ppm>  Write rendered RGBA8 frame as binary PPM (P6)\n"
        "  --O2                    Enable aggressive MIR optimization\n"
        "  --stdlib <dir>          Path to stdlib root directory\n"
        "  -h, --help              Show this help\n");
}

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

[[nodiscard]] bool WritePpmFile(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<char> rgb(pixelCount * 3u);
    for (std::size_t i = 0u; i < pixelCount; ++i) {
        rgb[i * 3u + 0u] = static_cast<char>(rgba[i * 4u + 0u]);
        rgb[i * 3u + 1u] = static_cast<char>(rgba[i * 4u + 1u]);
        rgb[i * 3u + 2u] = static_cast<char>(rgba[i * 4u + 2u]);
    }
    out.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    return out.good();
}

[[nodiscard]] bool HasNonUniformRenderedPixels(const std::vector<std::uint8_t>& rgba) noexcept {
    if (rgba.size() < 4u) {
        return false;
    }
    const std::uint8_t r0 = rgba[0];
    const std::uint8_t g0 = rgba[1];
    const std::uint8_t b0 = rgba[2];
    const std::uint8_t a0 = rgba[3];
    bool anyNonZero = (r0 | g0 | b0 | a0) != 0u;
    bool anyDiff = false;
    for (std::size_t i = 4u; i + 3u < rgba.size(); i += 4u) {
        if ((rgba[i] | rgba[i + 1u] | rgba[i + 2u] | rgba[i + 3u]) != 0u) {
            anyNonZero = true;
        }
        if (rgba[i] != r0 || rgba[i + 1u] != g0 || rgba[i + 2u] != b0 || rgba[i + 3u] != a0) {
            anyDiff = true;
            break;
        }
    }
    return anyNonZero && anyDiff;
}

[[nodiscard]] bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    return aero::str::EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] std::string ResolveDefaultStdlibDir(const char* argv0) {
#ifdef AERO_STDLIB_DIR
    {
        const std::filesystem::path p(AERO_STDLIB_DIR);
        if (std::filesystem::exists(p / "Aero.UI" / "Core.rs")) {
            return p.lexically_normal().generic_string();
        }
    }
#endif
    std::vector<std::filesystem::path> probes{
        std::filesystem::current_path() / "stdlib",
        std::filesystem::current_path().parent_path() / "stdlib",
        std::filesystem::current_path().parent_path().parent_path() / "stdlib",
    };
    if (argv0 && argv0[0] != '\0') {
        const std::filesystem::path exeDir = std::filesystem::path(argv0).parent_path();
        if (!exeDir.empty()) {
            probes.push_back(exeDir / "stdlib");
            probes.push_back(exeDir.parent_path() / "stdlib");
            probes.push_back(exeDir.parent_path().parent_path() / "stdlib");
        }
    }
    for (const auto& candidate : probes) {
        std::error_code ec;
        if (std::filesystem::exists(candidate / "Aero.UI" / "Core.rs", ec)) {
            return candidate.lexically_normal().generic_string();
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    std::string sampleDir;
    std::string xamlPath;
    std::vector<std::string> scriptPaths;
    std::string entryName;
    bool headless = false;
    std::uint64_t frames = 0u;
    bool framesSet = false;
    std::uint32_t width = 1280u;
    std::uint32_t height = 800u;
    std::string outPpmPath;
    bool optimizeO2 = false;
    std::string stdlibDir;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto needNext = [&](std::string_view opt) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing argument for " << opt << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
        if (arg == "--dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            sampleDir = v;
        } else if (arg == "--xaml") {
            const char* v = needNext(arg);
            if (!v) return 2;
            xamlPath = v;
        } else if (arg == "--rs") {
            const char* v = needNext(arg);
            if (!v) return 2;
            scriptPaths.emplace_back(v);
        } else if (arg == "--entry") {
            const char* v = needNext(arg);
            if (!v) return 2;
            entryName = v;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--frames") {
            const char* v = needNext(arg);
            if (!v) return 2;
            frames = static_cast<std::uint64_t>(std::strtoull(v, nullptr, 10));
            framesSet = true;
        } else if (arg == "--width") {
            const char* v = needNext(arg);
            if (!v) return 2;
            width = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)));
        } else if (arg == "--height") {
            const char* v = needNext(arg);
            if (!v) return 2;
            height = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)));
        } else if (arg == "--out") {
            const char* v = needNext(arg);
            if (!v) return 2;
            outPpmPath = v;
        } else if (arg == "--O2" || arg == "-O2") {
            optimizeO2 = true;
        } else if (arg == "--stdlib") {
            const char* v = needNext(arg);
            if (!v) return 2;
            stdlibDir = v;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    if (sampleDir.empty() && xamlPath.empty() && scriptPaths.empty()) {
        PrintUsage();
        return 0;
    }

#ifndef AERO_HAS_COMPILER_V3
    std::cerr << "aero-viewer was built without AERO_HAS_COMPILER_V3 compiler support.\n";
    return 1;
#else
    if (!sampleDir.empty()) {
        const std::filesystem::path dirPath(sampleDir);
        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec)) {
            std::cerr << "Sample directory not found: " << sampleDir << "\n";
            return 1;
        }
        std::vector<std::filesystem::path> xamls;
        std::vector<std::filesystem::path> scripts;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path p = entry.path();
            const std::string ext = p.extension().string();
            const std::string name = p.filename().string();
            if (aero::str::EqualsIgnoreCase(ext, ".xaml")) {
                xamls.push_back(p);
            } else if ((aero::str::EqualsIgnoreCase(ext, ".rs") || aero::str::EqualsIgnoreCase(ext, ".cs")) &&
                       !EndsWithIgnoreCase(name, ".g.rs")) {
                scripts.push_back(p);
            }
        }
        std::sort(xamls.begin(), xamls.end());
        std::sort(scripts.begin(), scripts.end());

        if (xamlPath.empty() && !xamls.empty()) {
            // Prefer MainWindow.xaml or first XAML with x:Class
            for (const auto& xp : xamls) {
                if (aero::str::EqualsIgnoreCase(xp.filename().string(), "MainWindow.xaml")) {
                    xamlPath = xp.lexically_normal().generic_string();
                    break;
                }
            }
            if (xamlPath.empty()) {
                for (const auto& xp : xamls) {
                    std::string text;
                    if (ReadFileText(xp, text) && text.find("x:Class") != std::string::npos) {
                        xamlPath = xp.lexically_normal().generic_string();
                        break;
                    }
                }
            }
            if (xamlPath.empty()) {
                xamlPath = xamls.front().lexically_normal().generic_string();
            }
        }
        for (const auto& sp : scripts) {
            const std::string norm = sp.lexically_normal().generic_string();
            if (std::find(scriptPaths.begin(), scriptPaths.end(), norm) == scriptPaths.end()) {
                scriptPaths.push_back(norm);
            }
        }
    }

    aero::AeroV3CompileInput compileInput;
    compileInput.includeStdlib = true;
    compileInput.optimizeBytecode = optimizeO2;
    if (!stdlibDir.empty()) {
        compileInput.stdlibRootDir = stdlibDir;
    } else {
        compileInput.stdlibRootDir = ResolveDefaultStdlibDir(argc > 0 ? argv[0] : nullptr);
    }

    if (!sampleDir.empty()) {
        compileInput.resourceSearchRoot = std::filesystem::path(sampleDir).lexically_normal().generic_string();
    }
    if (!xamlPath.empty()) {
        const std::filesystem::path xp(xamlPath);
        compileInput.xamlPath = xp.lexically_normal().generic_string();
        if (compileInput.resourceSearchRoot.empty() && xp.has_parent_path()) {
            compileInput.resourceSearchRoot = xp.parent_path().lexically_normal().generic_string();
        }
        if (!ReadFileText(xp, compileInput.xamlContent)) {
            std::cerr << "Failed to read XAML file: " << xamlPath << "\n";
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
        compileInput.scriptSources.push_back(std::move(sf));
    }

    aero::AeroAppHostConfig hostCfg;
    hostCfg.viewportWidth = width;
    hostCfg.viewportHeight = height;
    hostCfg.headless = headless;
    hostCfg.optimizeBytecode = optimizeO2;
    hostCfg.entryClassName = entryName;
    hostCfg.entryFunctionName = entryName;
    if (!compileInput.resourceSearchRoot.empty()) {
        hostCfg.assetSearchDirs.push_back(compileInput.resourceSearchRoot);
    }

    aero::AeroAppHost host(hostCfg);
    if (!host.InitializeRuntime()) {
        std::cerr << "Host runtime init failed: " << host.LastError() << "\n";
        return 1;
    }

    std::string compileErr;
    if (!host.CompileAndLoad(compileInput, &compileErr)) {
        std::cerr << compileErr << "\n";
        return 1;
    }

    if (headless) {
        const std::uint64_t stepCount = framesSet ? std::max<std::uint64_t>(1u, frames) : 1u;
        for (std::uint64_t f = 0u; f < stepCount; ++f) {
            host.StepFrame(1.0f / 60.0f, (f + 1u) == stepCount);
        }

        std::vector<std::uint8_t> rgba;
        if (!host.RenderHeadlessRgba(width, height, rgba)) {
            std::cerr << "RenderHeadlessRgba failed.\n";
            return 1;
        }
        if (!HasNonUniformRenderedPixels(rgba)) {
            std::cerr << "Headless render assertion failed: output image is blank or uniform.\n";
            return 1;
        }
        if (!outPpmPath.empty() && !WritePpmFile(outPpmPath, width, height, rgba)) {
            std::cerr << "Failed to write PPM output: " << outPpmPath << "\n";
            return 1;
        }
        std::printf(
            "aero-viewer headless OK (%ux%u, %llu frames, %zu nodes)\n",
            width,
            height,
            static_cast<unsigned long long>(stepCount),
            host.World().LiveNodeCount());
        return 0;
    }

    const std::uint64_t rendered = host.RunWindowedLoop(framesSet ? frames : 0u);
    if (!outPpmPath.empty()) {
        if (!host.SaveScreenshotPpm(outPpmPath, width, height)) {
            std::cerr << "Failed to write PPM screenshot: " << outPpmPath << "\n";
            return 1;
        }
    }
    std::printf("aero-viewer exited cleanly after %llu frames.\n", static_cast<unsigned long long>(rendered));
    return 0;
#endif
}
