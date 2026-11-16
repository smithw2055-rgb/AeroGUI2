#include "aero/compiler/StdlibModulePack.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] bool HasStdlibMarker(const std::filesystem::path& candidate) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec)) {
        return false;
    }
    const auto nativeVisual = candidate / "Aero.Native" / "NativeVisual.rs";
    const auto coreUi = candidate / "Aero.UI" / "Core.rs";
    return std::filesystem::is_regular_file(nativeVisual, ec) ||
           std::filesystem::is_regular_file(coreUi, ec) ||
           std::filesystem::is_directory(candidate / "Aero.UI", ec);
}

[[nodiscard]] std::string ReadFileUtf8(const std::filesystem::path& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

void AppendModuleDirectorySources(
    const std::filesystem::path& dir,
    std::string_view preferredFirstFilename,
    std::vector<SourceFile>& outSources) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }

    std::vector<std::filesystem::path> rsFiles;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".rs") {
            rsFiles.push_back(entry.path());
        }
    }

    std::sort(rsFiles.begin(), rsFiles.end(),
        [preferredFirstFilename](const std::filesystem::path& a, const std::filesystem::path& b) {
            const std::string aName = a.filename().generic_string();
            const std::string bName = b.filename().generic_string();
            const bool aFirst = !preferredFirstFilename.empty() && aName == preferredFirstFilename;
            const bool bFirst = !preferredFirstFilename.empty() && bName == preferredFirstFilename;
            if (aFirst != bFirst) {
                return aFirst;
            }
            return aName < bName;
        });

    for (const auto& path : rsFiles) {
        std::string content = ReadFileUtf8(path);
        if (content.empty()) {
            continue;
        }
        SourceFile sf;
        sf.path = path.lexically_normal().generic_string();
        sf.content = std::move(content);
        outSources.push_back(std::move(sf));
    }
}

[[nodiscard]] std::string ExtractDeclaredModuleFromText(std::string_view content) {
    std::size_t pos = 0;
    const std::size_t len = content.size();
    while (pos < len) {
        while (pos < len && (content[pos] == ' ' || content[pos] == '\t' ||
                             content[pos] == '\r' || content[pos] == '\n')) {
            ++pos;
        }
        if (pos + 1 < len && content[pos] == '/' && content[pos + 1] == '/') {
            while (pos < len && content[pos] != '\n') {
                ++pos;
            }
            continue;
        }
        if (pos + 1 < len && content[pos] == '/' && content[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(content[pos] == '*' && content[pos + 1] == '/')) {
                ++pos;
            }
            pos = std::min(pos + 2, len);
            continue;
        }
        break;
    }

    constexpr std::string_view kModuleKw = "module";
    if (pos + kModuleKw.size() >= len ||
        content.substr(pos, kModuleKw.size()) != kModuleKw) {
        return {};
    }
    const char afterKw = content[pos + kModuleKw.size()];
    if (afterKw != ' ' && afterKw != '\t' && afterKw != '\r' && afterKw != '\n') {
        return {};
    }

    pos += kModuleKw.size();
    while (pos < len && (content[pos] == ' ' || content[pos] == '\t')) {
        ++pos;
    }
    const std::size_t semi = content.find(';', pos);
    if (semi == std::string_view::npos) {
        return {};
    }
    std::string rawMod(aero::str::Trim(content.substr(pos, semi - pos)));
    rawMod.erase(
        std::remove_if(rawMod.begin(), rawMod.end(), [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }),
        rawMod.end());
    return rawMod;
}

} // namespace

std::filesystem::path ResolveStdlibRootDirectory(std::string_view explicitDir) {
    std::vector<std::filesystem::path> probes;
    if (!explicitDir.empty()) {
        probes.emplace_back(explicitDir);
    }
#ifdef _MSC_VER
    char* envBuffer = nullptr;
    std::size_t envLen = 0;
    if (_dupenv_s(&envBuffer, &envLen, "AERO_STDLIB_DIR") == 0 && envBuffer != nullptr) {
        if (envBuffer[0] != '\0') {
            probes.emplace_back(envBuffer);
        }
        std::free(envBuffer);
    }
#else
    if (const char* envDir = std::getenv("AERO_STDLIB_DIR"); envDir != nullptr && envDir[0] != '\0') {
        probes.emplace_back(envDir);
    }
#endif
#ifdef AERO_STDLIB_DIR
    probes.emplace_back(AERO_STDLIB_DIR);
#endif
    probes.emplace_back("stdlib");
    probes.emplace_back("../stdlib");
    probes.emplace_back("../../stdlib");
    probes.emplace_back("../../../stdlib");

    for (const auto& candidate : probes) {
        if (!candidate.empty() && HasStdlibMarker(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::vector<SourceFile> LoadStdlibModulePack(std::string_view stdlibRootDir) {
    const std::filesystem::path resolvedRoot = ResolveStdlibRootDirectory(stdlibRootDir);
    if (resolvedRoot.empty()) {
        return {};
    }

    std::vector<SourceFile> sources;
    AppendModuleDirectorySources(resolvedRoot / "Aero.Native", "NativeVisual.rs", sources);
    AppendModuleDirectorySources(resolvedRoot / "Aero.UI", "Core.rs", sources);
    return sources;
}

std::unordered_set<std::string> CollectDeclaredModuleNames(
    const std::vector<SourceFile>& sources) {
    std::unordered_set<std::string> modules;
    for (const auto& sf : sources) {
        std::string modName = ExtractDeclaredModuleFromText(sf.content);
        if (!modName.empty()) {
            modules.insert(std::move(modName));
        }
    }
    return modules;
}

void StripAeroNativeMirBodies(std::vector<rs_mir::Module>& modules) {
    for (auto& mod : modules) {
        if (mod.name == "Aero.Native") {
            mod.functions.clear();
        }
    }
}

} // namespace aero
