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
        "  --cache-dir <dir>       Disk stdlib cache directory\n"
        "  --no-cache              Disable stdlib disk cache\n"
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

[[nodiscard]] std::string ResolveDefaultCacheDir(const char* argv0) {
    if (const char* envCache = std::getenv("AERO_CACHE_DIR"); envCache && envCache[0] != '\0') {
        return std::filesystem::path(envCache).lexically_normal().generic_string();
    }
    if (argv0 && argv0[0] != '\0') {
        const std::filesystem::path exeDir = std::filesystem::path(argv0).parent_path();
        if (!exeDir.empty()) {
            return (exeDir / "aero_stdlib_cache").lexically_normal().generic_string();
        }
    }
    return {};
}

void AppendUniqueDir(std::vector<std::string>& list, const std::filesystem::path& dir) {
    if (dir.empty()) {
        return;
    }
    const std::string norm = dir.lexically_normal().generic_string();
    if (std::find(list.begin(), list.end(), norm) == list.end()) {
        list.push_back(norm);
    }
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
    std::string cacheDir;
    bool allowCache = true;

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
        } else if (arg == "--cache-dir") {
            const char* v = needNext(arg);
            if (!v) return 2;
            cacheDir = v;
        } else if (arg == "--no-cache") {
            allowCache = false;
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
    aero::AeroV3CompileInput compileInput;
    aero::AeroAppHostConfig hostCfg;

    if (!sampleDir.empty()) {
        if (!aero::DiscoverSampleProject(std::filesystem::path(sampleDir), compileInput, hostCfg)) {
            std::cerr << "Failed to discover sample project: " << sampleDir << "\n";
            return 1;
        }
    }

    compileInput.includeStdlib = true;
    compileInput.optimizeBytecode = optimizeO2;
    compileInput.allowDiskCache = allowCache;
    if (allowCache) {
        compileInput.cacheDir = !cacheDir.empty() ? cacheDir : ResolveDefaultCacheDir(argc > 0 ? argv[0] : nullptr);
    }
    if (!stdlibDir.empty()) {
        compileInput.stdlibRootDir = stdlibDir;
    } else if (compileInput.stdlibRootDir.empty()) {
        compileInput.stdlibRootDir = ResolveDefaultStdlibDir(argc > 0 ? argv[0] : nullptr);
    }

    if (!xamlPath.empty()) {
        const std::filesystem::path xp = std::filesystem::path(xamlPath).lexically_normal();
        compileInput.xamlPath = xp.generic_string();
        if (!ReadFileText(xp, compileInput.xamlContent)) {
            std::cerr << "Failed to read XAML file: " << xamlPath << "\n";
            return 1;
        }
        compileInput.additionalXamlSources.erase(
            std::remove_if(
                compileInput.additionalXamlSources.begin(),
                compileInput.additionalXamlSources.end(),
                [&](const aero::SourceFile& sf) {
                    return aero::str::EqualsIgnoreCase(
                        std::filesystem::path(sf.path).lexically_normal().generic_string(),
                        compileInput.xamlPath);
                }),
            compileInput.additionalXamlSources.end());

        if (xp.has_parent_path()) {
            const std::filesystem::path xpDir = xp.parent_path();
            if (compileInput.resourceSearchRoot.empty()) {
                compileInput.resourceSearchRoot = xpDir.generic_string();
            }
            AppendUniqueDir(compileInput.resourceSearchPaths, xpDir);
            AppendUniqueDir(hostCfg.assetSearchDirs, xpDir);
        }
    }

    for (const auto& sp : scriptPaths) {
        const std::filesystem::path spPath = std::filesystem::path(sp).lexically_normal();
        const std::string norm = spPath.generic_string();
        const bool alreadyAdded = std::any_of(
            compileInput.scriptSources.begin(),
            compileInput.scriptSources.end(),
            [&](const aero::SourceFile& sf) {
                return aero::str::EqualsIgnoreCase(
                    std::filesystem::path(sf.path).lexically_normal().generic_string(),
                    norm);
            });
        if (alreadyAdded) {
            continue;
        }
        std::string text;
        if (!ReadFileText(spPath, text)) {
            std::cerr << "Failed to read script file: " << sp << "\n";
            return 1;
        }
        aero::SourceFile sf;
        sf.path = norm;
        sf.content = std::move(text);
        compileInput.scriptSources.push_back(std::move(sf));
    }

    hostCfg.viewportWidth = width;
    hostCfg.viewportHeight = height;
    hostCfg.headless = headless;
    hostCfg.optimizeBytecode = optimizeO2;
    if (!entryName.empty()) {
        hostCfg.entryClassName = entryName;
        hostCfg.entryFunctionName = entryName;
    }
    if (!compileInput.resourceSearchRoot.empty()) {
        AppendUniqueDir(hostCfg.assetSearchDirs, std::filesystem::path(compileInput.resourceSearchRoot));
    }

    aero::AeroAppHost host(hostCfg);

    std::string compileErr;
    if (!host.CompileAndLoad(compileInput, &compileErr)) {
        std::cerr << compileErr << "\n";
        return 1;
    }

    if (headless) {
        const std::uint64_t stepCount = framesSet ? std::max<std::uint64_t>(1u, frames) : 1u;
        for (std::uint64_t f = 0u; f < stepCount; ++f) {
            host.StepFrame(1.0f / 60.0f, false);
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
        if (!host.SaveScreenshotPpm(outPpmPath, 0u, 0u)) {
            std::cerr << "Failed to write PPM screenshot: " << outPpmPath << "\n";
            return 1;
        }
    }
    std::printf("aero-viewer exited cleanly after %llu frames.\n", static_cast<unsigned long long>(rendered));
    return 0;
#endif
}
