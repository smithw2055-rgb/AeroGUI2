#include "aero/app/AeroAppHost.hpp"

#ifdef AERO_HAS_COMPILER_V3

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#endif

#include "aero/compiler/StdlibModulePack.hpp"
#include "aero/core/StringUtils.hpp"

namespace aero {
namespace {

[[nodiscard]] bool ReadFileUtf8Text(const std::filesystem::path& path, std::string& outText) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    if (outText.size() >= 3u &&
        static_cast<unsigned char>(outText[0]) == 0xEFu &&
        static_cast<unsigned char>(outText[1]) == 0xBBu &&
        static_cast<unsigned char>(outText[2]) == 0xBFu) {
        outText.erase(0u, 3u);
    }
    return true;
}

[[nodiscard]] std::string ToLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

[[nodiscard]] bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    return str::EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) {
        return false;
    }
    return str::EqualsIgnoreCase(text.substr(0u, prefix.size()), prefix);
}

[[nodiscard]] bool HasPathSegment(std::string_view lowerRel, std::string_view segment) noexcept {
    if (lowerRel.rfind(segment, 0u) == 0u) {
        return true;
    }
    std::string needle = "/";
    needle.append(segment);
    return lowerRel.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool HasViewRootTag(std::string_view xamlText) noexcept {
    std::size_t pos = 0u;
    while (pos < xamlText.size()) {
        const std::size_t lt = xamlText.find('<', pos);
        if (lt == std::string_view::npos || lt + 1u >= xamlText.size()) {
            return false;
        }
        const char next = xamlText[lt + 1u];
        if (next == '?' || next == '!') {
            if (xamlText.compare(lt, 4u, "<!--") == 0) {
                const std::size_t endComment = xamlText.find("-->", lt + 4u);
                pos = (endComment == std::string_view::npos) ? xamlText.size() : (endComment + 3u);
            } else {
                const std::size_t gt = xamlText.find('>', lt + 2u);
                pos = (gt == std::string_view::npos) ? xamlText.size() : (gt + 1u);
            }
            continue;
        }
        std::size_t endTag = lt + 1u;
        while (endTag < xamlText.size() &&
               xamlText[endTag] != ' ' && xamlText[endTag] != '\t' &&
               xamlText[endTag] != '\r' && xamlText[endTag] != '\n' &&
               xamlText[endTag] != '>' && xamlText[endTag] != '/') {
            ++endTag;
        }
        std::string_view rootTag = xamlText.substr(lt + 1u, endTag - (lt + 1u));
        if (const std::size_t colon = rootTag.find(':'); colon != std::string_view::npos) {
            rootTag = rootTag.substr(colon + 1u);
        }
        return rootTag == "Window" || rootTag == "Page" || rootTag == "UserControl";
    }
    return false;
}

[[nodiscard]] std::string NormalizePathGenericUpperDrive(const std::filesystem::path& p) {
    std::string out = p.lexically_normal().generic_string();
    if (out.size() >= 2u && out[1] == ':' && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

} // namespace

bool DiscoverSampleProject(
    const std::filesystem::path& sampleDir,
    AeroV3CompileInput& outCompileInput,
    AeroAppHostConfig& outHostConfig) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(sampleDir, ec) || !fs::is_directory(sampleDir, ec)) {
        return false;
    }

    const fs::path sampleRoot = sampleDir.lexically_normal();

    struct DiscoveredXaml {
        fs::path absPath;
        std::string normPath;
        std::string lowerRel;
        std::string filename;
        bool isResources = false;
        bool isApp = false;
        bool isLanguage = false;
        bool isDataSamples = false;
        bool isDataTheme = false;
        bool isDataPagePreviews = false;
        bool isDataControls = false;
        bool eligibleEntry = false;
        std::string content;
    };

    std::vector<DiscoveredXaml> xamls;
    std::vector<fs::path> scripts;

    for (const auto& entry : fs::recursive_directory_iterator(sampleRoot, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path p = entry.path().lexically_normal();
        const std::string ext = p.extension().string();
        const std::string fname = p.filename().string();
        if (str::EqualsIgnoreCase(ext, ".xaml") || str::EqualsIgnoreCase(ext, ".axaml")) {
            DiscoveredXaml item;
            item.absPath = p;
            item.normPath = NormalizePathGenericUpperDrive(p);
            const fs::path rel = p.lexically_relative(sampleRoot);
            item.lowerRel = ToLowerAscii(rel.generic_string());
            item.filename = fname;
            item.isResources = str::EqualsIgnoreCase(fname, "Resources.xaml");
            item.isApp = str::EqualsIgnoreCase(fname, "App.xaml");
            item.isLanguage = StartsWithIgnoreCase(fname, "Language-");
            item.isDataSamples = HasPathSegment(item.lowerRel, "data/samples/") ||
                                 HasPathSegment(item.lowerRel, "samples/");
            item.isDataTheme = HasPathSegment(item.lowerRel, "data/theme/") ||
                               HasPathSegment(item.lowerRel, "theme/");
            item.isDataPagePreviews = HasPathSegment(item.lowerRel, "data/pagepreviews/") ||
                                      HasPathSegment(item.lowerRel, "pagepreviews/");
            item.isDataControls = HasPathSegment(item.lowerRel, "data/controls/") ||
                                  HasPathSegment(item.lowerRel, "controls/");
            item.eligibleEntry = !(item.isResources || item.isApp || item.isLanguage ||
                                   item.isDataSamples || item.isDataTheme ||
                                   item.isDataPagePreviews || item.isDataControls);
            xamls.push_back(std::move(item));
        } else if ((str::EqualsIgnoreCase(ext, ".rs") || str::EqualsIgnoreCase(ext, ".cs")) &&
                   !EndsWithIgnoreCase(fname, ".g.rs")) {
            scripts.push_back(p);
        }
    }

    std::sort(xamls.begin(), xamls.end(),
        [](const DiscoveredXaml& a, const DiscoveredXaml& b) {
            return a.normPath < b.normPath;
        });
    std::sort(scripts.begin(), scripts.end(),
        [](const fs::path& a, const fs::path& b) {
            return a.generic_string() < b.generic_string();
        });

    std::size_t entryIndex = xamls.size();
    for (auto& item : xamls) {
        if (item.eligibleEntry) {
            (void)ReadFileUtf8Text(item.absPath, item.content);
        }
    }

    // 1. Prefer MainWindow.xaml
    for (std::size_t i = 0u; i < xamls.size(); ++i) {
        if (xamls[i].eligibleEntry && str::EqualsIgnoreCase(xamls[i].filename, "MainWindow.xaml")) {
            entryIndex = i;
            break;
        }
    }
    // 2. Fallback MainPage.xaml
    if (entryIndex == xamls.size()) {
        for (std::size_t i = 0u; i < xamls.size(); ++i) {
            if (xamls[i].eligibleEntry && str::EqualsIgnoreCase(xamls[i].filename, "MainPage.xaml")) {
                entryIndex = i;
                break;
            }
        }
    }
    // 3. Fallback first primary XAML with x:Class
    if (entryIndex == xamls.size()) {
        for (std::size_t i = 0u; i < xamls.size(); ++i) {
            if (xamls[i].eligibleEntry &&
                xamls[i].content.find("x:Class") != std::string::npos) {
                entryIndex = i;
                break;
            }
        }
    }
    // 4. Fallback first primary XAML with Window/Page/UserControl root
    if (entryIndex == xamls.size()) {
        for (std::size_t i = 0u; i < xamls.size(); ++i) {
            if (xamls[i].eligibleEntry && HasViewRootTag(xamls[i].content)) {
                entryIndex = i;
                break;
            }
        }
    }
    // 5. Final fallback: first eligible XAML
    if (entryIndex == xamls.size()) {
        for (std::size_t i = 0u; i < xamls.size(); ++i) {
            if (xamls[i].eligibleEntry) {
                entryIndex = i;
                break;
            }
        }
    }

    if (entryIndex < xamls.size()) {
        outCompileInput.xamlPath = xamls[entryIndex].normPath;
        outCompileInput.xamlContent = xamls[entryIndex].content;
        if (outCompileInput.resourceSearchRoot.empty() && xamls[entryIndex].absPath.has_parent_path()) {
            outCompileInput.resourceSearchRoot =
                NormalizePathGenericUpperDrive(xamls[entryIndex].absPath.parent_path());
        }
    } else if (outCompileInput.resourceSearchRoot.empty()) {
        outCompileInput.resourceSearchRoot = NormalizePathGenericUpperDrive(sampleRoot);
    }

    std::unordered_set<std::string> seenAdditionalXamls;
    for (const auto& existing : outCompileInput.additionalXamlSources) {
        seenAdditionalXamls.insert(existing.path);
    }
    for (std::size_t i = 0u; i < xamls.size(); ++i) {
        if (i == entryIndex) {
            continue;
        }
        const auto& item = xamls[i];
        if (item.isDataSamples || item.isDataTheme || item.isDataPagePreviews ||
            item.isResources || item.isLanguage) {
            continue;
        }
        if (!seenAdditionalXamls.insert(item.normPath).second) {
            continue;
        }
        std::string text = item.content;
        if (text.empty() && !ReadFileUtf8Text(item.absPath, text)) {
            continue;
        }
        if (!text.empty()) {
            outCompileInput.additionalXamlSources.push_back(SourceFile{item.normPath, std::move(text)});
        }
    }

    std::unordered_set<std::string> seenScripts;
    for (const auto& existing : outCompileInput.scriptSources) {
        seenScripts.insert(existing.path);
    }
    for (const auto& sp : scripts) {
        const std::string norm = NormalizePathGenericUpperDrive(sp);
        if (!seenScripts.insert(norm).second) {
            continue;
        }
        std::string text;
        if (ReadFileUtf8Text(sp, text)) {
            outCompileInput.scriptSources.push_back(SourceFile{norm, std::move(text)});
        }
    }

    std::unordered_set<std::string> seenResourcePaths;
    for (const auto& p : outCompileInput.resourceSearchPaths) {
        seenResourcePaths.insert(p);
    }
    std::unordered_set<std::string> seenAssetDirs;
    for (const auto& p : outHostConfig.assetSearchDirs) {
        seenAssetDirs.insert(p);
    }
    auto registerDirIfExists = [&](const fs::path& dir) {
        std::error_code dirEc;
        if (!fs::is_directory(dir, dirEc)) {
            return;
        }
        const std::string norm = NormalizePathGenericUpperDrive(dir);
        if (seenResourcePaths.insert(norm).second) {
            outCompileInput.resourceSearchPaths.push_back(norm);
        }
        if (seenAssetDirs.insert(norm).second) {
            outHostConfig.assetSearchDirs.push_back(norm);
        }
    };

    const fs::path dataDir = sampleRoot / "Data";
    const fs::path baseDirs[] = {sampleRoot, dataDir};
    const char* kSubDirs[] = {
        "",
        "Images",
        "Images/Items",
        "Fonts",
        "Sounds",
        "Samples",
        "Samples/Images",
        "Samples/Fonts",
        "Theme",
        "Theme/Fonts",
    };
    for (const fs::path& base : baseDirs) {
        for (const char* sub : kSubDirs) {
            registerDirIfExists(sub[0] == '\0' ? base : (base / sub));
        }
    }

    // Walk ancestors so packaged /Noesis.GUI.Extensions;component/Theme/*.xaml resolves
    std::error_code probeEc;
    fs::path probe = sampleRoot.is_absolute() ? sampleRoot : fs::absolute(sampleRoot, probeEc);
    if (probeEc || probe.empty()) {
        probe = sampleRoot;
    }
    for (int up = 0; up < 5 && !probe.empty(); ++up) {
        registerDirIfExists(probe / "Theme" / "Data");
        registerDirIfExists(probe / "Theme" / "Data" / "Theme");
        registerDirIfExists(probe / "Theme" / "Data" / "Theme" / "Fonts");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data" / "Theme");
        registerDirIfExists(probe / "Samples" / "Theme" / "Data" / "Theme" / "Fonts");
        const fs::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    return !outCompileInput.xamlContent.empty() || !outCompileInput.scriptSources.empty();
}

bool AeroAppHost::CompileAndLoad(
    const AeroV3CompileInput& input,
    std::string* outError) {
    AeroV3CompileInput effective = input;
    effective.skipRawBytecodeOutput = true;
    effective.retainBytecodeModules = false;
    effective.emitAotCpp = false;
    if (config_.optimizeBytecode && !effective.optimizeBytecode) {
        effective.optimizeBytecode = true;
    }
    if (!effective.resourceSearchRoot.empty()) {
        RegisterAssetDirectory(effective.resourceSearchRoot);
    }
    for (const auto& dir : effective.resourceSearchPaths) {
        RegisterAssetDirectory(dir);
    }
    std::shared_ptr<AeroV3CompileResult> compiled;
    {
        const AeroCompilerV3 compiler;
        compiled = std::make_shared<AeroV3CompileResult>(compiler.Compile(effective));
    }
    if (!compiled->succeeded || !compiled->programImage) {
        devCompileResultOpaque_ = compiled;
        std::string msg = "AeroCompilerV3 compilation failed";
        for (const auto& d : compiled->diagnostics) {
            msg.append("\n  ");
            msg.append(d);
        }
        SetLastErrorMessage(msg);
        if (outError) {
            *outError = std::move(msg);
        }
        return false;
    }

    if (!IsInitialized() && !InitializeRuntime()) {
        devCompileResultOpaque_ = std::move(compiled);
        if (outError) {
            *outError = LastError();
        }
        return false;
    }
    devCompileResultOpaque_ = compiled;

    std::string entryHint = config_.entryFunctionName;
    if (entryHint.empty() && !config_.entryClassName.empty()) {
        entryHint = config_.entryClassName;
    }
    if (!LoadProgramImageWithPreferredEntry(
            compiled->programImage,
            entryHint,
            compiled->rootClassName)) {
        if (outError) {
            *outError = LastError();
        }
        return false;
    }
    std::vector<rs_bc::Module>().swap(compiled->bytecodeModules);
    std::vector<std::uint8_t>().swap(compiled->programImageBytes);
    std::string().swap(compiled->generatedPartialRsCode);
#ifdef _WIN32
    (void)_heapmin();
    (void)HeapCompact(GetProcessHeap(), 0u);
#endif
    return true;
}

const AeroV3CompileResult* AeroAppHost::LastCompileResult() const noexcept {
    return static_cast<const AeroV3CompileResult*>(DevCompileResultOpaque());
}

void DiscoverCatalogXamlSources(
    const std::filesystem::path& sampleDir,
    std::vector<CatalogXamlEntry>& outCatalogEntries) {
    CollectCatalogXamlEntries(sampleDir, outCatalogEntries);
}

} // namespace aero

#endif // AERO_HAS_COMPILER_V3
