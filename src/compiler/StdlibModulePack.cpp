#include "aero/compiler/StdlibModulePack.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "aero/core/StringUtils.hpp"
#include "realscript/semantic/Semantic.h"

namespace aero {
namespace {

[[nodiscard]] bool HasStdlibMarker(const std::filesystem::path& candidate) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec)) {
        return false;
    }
    const auto nativeVisual = candidate / "Aero.Native" / "NativeVisual.rs";
    const auto splitCore = candidate / "Aero.UI.Core" / "Core.rs";
    const auto legacyCore = candidate / "Aero.UI" / "Core.rs";
    return std::filesystem::is_regular_file(nativeVisual, ec) ||
           std::filesystem::is_regular_file(splitCore, ec) ||
           std::filesystem::is_regular_file(legacyCore, ec) ||
           std::filesystem::is_directory(candidate / "Aero.UI.Core", ec) ||
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

std::uint32_t ResolveEffectiveStdlibPackageMask(std::uint32_t requestedMask) noexcept {
    if (requestedMask == AeroStdlibPkg_Auto) {
        return AeroStdlibPkg_All;
    }
    std::uint32_t mask = requestedMask | AeroStdlibPkg_Core;
    if ((mask & AeroStdlibPkg_RuntimeXaml) != 0u ||
        (mask & AeroStdlibPkg_Workbench) != 0u) {
        mask |= AeroStdlibPkg_Controls;
    }
    return mask & (AeroStdlibPkg_All | AeroStdlibPkg_GameHud);
}

std::uint32_t InferRequiredStdlibPackages(const AeroV3CompileInput& input) noexcept {
    std::uint32_t mask = AeroStdlibPkg_Core;
    if ((input.stdlibPackages & AeroStdlibPkg_GameHud) != 0u) {
        mask |= AeroStdlibPkg_GameHud;
    }

    auto scanText = [&](std::string_view text) noexcept {
        if (text.empty()) {
            return;
        }
        auto hasAny = [&](std::initializer_list<std::string_view> tokens) noexcept -> bool {
            for (const std::string_view tok : tokens) {
                if (text.find(tok) != std::string_view::npos) {
                    return true;
                }
            }
            return false;
        };
        if (hasAny({
                "XamlReader",
                "LoadUri(",
                "LoadResourceDictionary(",
                "Data/Samples/",
                "NoesisTheme.Brushes",
                "__RegisterWorkbenchXamlFactories",
                "__AeroCompiledXamlCatalogInit",
            })) {
            mask |= AeroStdlibPkg_RuntimeXaml | AeroStdlibPkg_Controls;
        }
        if (hasAny({
                "PropertyGrid",
                "DataGrid",
                "InspectorPanel",
                "CodeEditor",
                "Viewport3D",
                "TimelineEditor",
                "NodeGraphEditor",
                "CurveEditor",
                "AssetBrowser",
                "DockLayout",
                "TreeListView",
            })) {
            mask |= AeroStdlibPkg_Workbench | AeroStdlibPkg_Controls;
        }
        if (hasAny({
                "Button",
                "ToggleButton",
                "CheckBox",
                "RadioButton",
                "Slider",
                "ProgressBar",
                "ScrollBar",
                "ScrollViewer",
                "ListBox",
                "ListView",
                "ComboBox",
                "TabControl",
                "TabItem",
                "Menu",
                "MenuItem",
                "ContextMenu",
                "TreeView",
                "Popup",
                "ToolTip",
                "TextBox",
                "PasswordBox",
                "RichTextBox",
                "StatusBar",
                "Expander",
                "GroupBox",
                "Calendar",
                "DatePicker",
                "WrapPanel",
                "UniformGrid",
                "Virtualizing",
                "DragDrop",
                "RoutedCommand",
                "NavigationService",
                "ItemsControl",
                "Selector",
                "AeroFluent",
                "MutedLabel(",
                "Heading(",
                "MathConverter",
                "BehaviorEventTrigger",
                "BehaviorDataTrigger",
                "KeyTrigger",
                "StoryboardCompletedTrigger",
                "PropertyChangedTrigger",
                "Interaction.",
                "FluidMove",
                "RiveControl",
                "RiveInput",
                "DataGrid",
                "DateTimeControl",
            })) {
            mask |= AeroStdlibPkg_Controls;
        }
    };

    if (!input.catalogXamlSources.empty()) {
        mask |= AeroStdlibPkg_Controls | AeroStdlibPkg_RuntimeXaml;
    }
    scanText(input.xamlContent);
    for (const auto& sf : input.additionalXamlSources) {
        scanText(sf.content);
    }
    for (const auto& sf : input.scriptSources) {
        scanText(sf.content);
    }
    return ResolveEffectiveStdlibPackageMask(mask);
}

std::vector<SourceFile> LoadStdlibModulePack(std::string_view stdlibRootDir) {
    return LoadStdlibModulePack(stdlibRootDir, AeroStdlibPkg_All);
}

std::vector<SourceFile> LoadStdlibModulePack(
    std::string_view stdlibRootDir,
    std::uint32_t packageMask) {
    const std::filesystem::path resolvedRoot = ResolveStdlibRootDirectory(stdlibRootDir);
    if (resolvedRoot.empty()) {
        return {};
    }

    const std::uint32_t effectiveMask = ResolveEffectiveStdlibPackageMask(packageMask);
    std::vector<SourceFile> sources;
    AppendModuleDirectorySources(resolvedRoot / "Aero.Native", "NativeVisual.rs", sources);

    std::error_code ec;
    if (std::filesystem::is_directory(resolvedRoot / "Aero.UI.Core", ec)) {
        AppendModuleDirectorySources(resolvedRoot / "Aero.UI.Core", "Core.rs", sources);
        if ((effectiveMask & AeroStdlibPkg_Controls) != 0u) {
            AppendModuleDirectorySources(resolvedRoot / "Aero.UI.Controls", {}, sources);
        }
        if ((effectiveMask & AeroStdlibPkg_RuntimeXaml) != 0u) {
            AppendModuleDirectorySources(resolvedRoot / "Aero.UI.RuntimeXaml", {}, sources);
        }
        if ((effectiveMask & AeroStdlibPkg_Workbench) != 0u) {
            AppendModuleDirectorySources(resolvedRoot / "Aero.UI.Workbench", {}, sources);
        }
    } else {
        AppendModuleDirectorySources(resolvedRoot / "Aero.UI", "Core.rs", sources);
    }
    return sources;
}

std::uint64_t ComputeStdlibPackHash(
    const std::vector<SourceFile>& sources,
    bool enableReactiveMacros) noexcept {
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    auto mixBytes = [&](const void* data, std::size_t size, std::uint64_t& state) noexcept {
        const auto* ptr = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0u; i < size; ++i) {
            state ^= static_cast<std::uint64_t>(ptr[i]);
            state *= kFnvPrime;
        }
    };
    auto mixSize = [&](std::size_t value, std::uint64_t& state) noexcept {
        const std::uint64_t v = static_cast<std::uint64_t>(value);
        mixBytes(&v, sizeof(v), state);
    };
    auto mixString = [&](std::string_view s, std::uint64_t& state) noexcept {
        mixSize(s.size(), state);
        mixBytes(s.data(), s.size(), state);
    };

    std::uint64_t hash = kFnvOffset;
    const std::uint8_t macroFlag = enableReactiveMacros ? 1u : 0u;
    mixBytes(&macroFlag, sizeof(macroFlag), hash);
    mixSize(sources.size(), hash);
    for (const auto& sf : sources) {
        mixString(sf.path, hash);
        mixString(sf.content, hash);
    }
    return hash;
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

void CollectCatalogXamlEntries(
    const std::filesystem::path& sampleDir,
    std::vector<CatalogXamlEntry>& outCatalogEntries) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (sampleDir.empty() || !fs::exists(sampleDir, ec) || !fs::is_directory(sampleDir, ec)) {
        return;
    }

    const fs::path sampleRoot = sampleDir.lexically_normal();
    auto toLower = [](std::string_view s) {
        std::string out(s);
        for (char& ch : out) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        return out;
    };

    std::unordered_set<std::string> seenNormUris;
    for (const auto& existing : outCatalogEntries) {
        if (!existing.uri.empty()) {
            seenNormUris.insert(toLower(existing.uri));
        }
    }

    struct CandidateFile final {
        std::string uri;
        std::vector<std::string> aliasUris;
        std::string normPath;
        bool isResourceDictionary = false;
        fs::path absPath;
    };
    std::vector<CandidateFile> candidates;
    bool sampleHasDataSamples = false;

    for (const auto& entry : fs::recursive_directory_iterator(sampleRoot, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path p = entry.path().lexically_normal();
        const std::string ext = p.extension().string();
        if (!aero::str::EqualsIgnoreCase(ext, ".xaml")) {
            continue;
        }
        const std::string fname = p.filename().string();
        const std::string relGeneric = p.lexically_relative(sampleRoot).generic_string();
        const std::string lowerRel = toLower(relGeneric);
        const std::string lowerName = toLower(fname);

        if (aero::str::StartsWith(lowerRel, "data/samples/") ||
            aero::str::StartsWith(lowerRel, "samples/")) {
            sampleHasDataSamples = true;
            std::string relFromData = aero::str::StartsWith(lowerRel, "data/")
                ? relGeneric
                : ("Data/" + relGeneric);
            CandidateFile cf;
            cf.uri = relFromData;
            cf.aliasUris = {"/" + relFromData, relGeneric, fname};
            cf.normPath = p.generic_string();
            cf.isResourceDictionary = false;
            cf.absPath = p;
            candidates.push_back(std::move(cf));
        } else if (aero::str::StartsWith(lowerName, "noesistheme.brushes.") &&
                   aero::str::EndsWith(lowerName, ".xaml")) {
            CandidateFile cf;
            cf.uri = "Theme/" + fname;
            cf.aliasUris = {
                "/Theme/" + fname,
                "Data/Theme/" + fname,
                "/Data/Theme/" + fname,
                "/Noesis.GUI.Extensions;component/Theme/" + fname,
                fname,
            };
            cf.normPath = p.generic_string();
            cf.isResourceDictionary = true;
            cf.absPath = p;
            candidates.push_back(std::move(cf));
        } else if (aero::str::StartsWith(lowerName, "language-") &&
                   aero::str::EndsWith(lowerName, ".xaml")) {
            CandidateFile cf;
            cf.uri = fname;
            cf.aliasUris = {"Data/" + fname, "/" + fname, "/Data/" + fname};
            cf.normPath = p.generic_string();
            cf.isResourceDictionary = true;
            cf.absPath = p;
            candidates.push_back(std::move(cf));
        }
    }

    if (sampleHasDataSamples) {
        fs::path probe = sampleRoot.is_absolute() ? sampleRoot : fs::absolute(sampleRoot, ec);
        if (ec || probe.empty()) {
            probe = sampleRoot;
        }
        auto scanThemeDir = [&](const fs::path& dir) {
            std::error_code dirEc;
            if (!fs::is_directory(dir, dirEc)) {
                return;
            }
            for (const auto& entry : fs::directory_iterator(dir, dirEc)) {
                if (dirEc || !entry.is_regular_file(dirEc)) {
                    continue;
                }
                const fs::path p = entry.path().lexically_normal();
                const std::string fname = p.filename().string();
                const std::string lowerName = toLower(fname);
                if (aero::str::StartsWith(lowerName, "noesistheme.brushes.") &&
                    aero::str::EndsWith(lowerName, ".xaml")) {
                    CandidateFile cf;
                    cf.uri = "Theme/" + fname;
                    cf.aliasUris = {
                        "/Theme/" + fname,
                        "Data/Theme/" + fname,
                        "/Data/Theme/" + fname,
                        "/Noesis.GUI.Extensions;component/Theme/" + fname,
                        fname,
                    };
                    cf.normPath = p.generic_string();
                    cf.isResourceDictionary = true;
                    cf.absPath = p;
                    candidates.push_back(std::move(cf));
                }
            }
        };
        for (int up = 0; up < 5 && !probe.empty(); ++up) {
            scanThemeDir(probe / "Theme" / "Data" / "Theme");
            scanThemeDir(probe / "Samples" / "Theme" / "Data" / "Theme");
            const fs::path parent = probe.parent_path();
            if (parent == probe) {
                break;
            }
            probe = parent;
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const CandidateFile& a, const CandidateFile& b) {
            return a.uri < b.uri;
        });

    for (auto& cf : candidates) {
        const std::string key = toLower(cf.uri);
        if (!seenNormUris.insert(key).second) {
            continue;
        }
        std::string content = ReadFileUtf8(cf.absPath);
        if (content.empty()) {
            continue;
        }
        CatalogXamlEntry item;
        item.uri = std::move(cf.uri);
        item.aliasUris = std::move(cf.aliasUris);
        item.xamlPath = std::move(cf.normPath);
        item.xamlContent = std::move(content);
        item.isResourceDictionary = cf.isResourceDictionary;
        outCatalogEntries.push_back(std::move(item));
    }
}

void StripAeroNativeMirBodies(std::vector<rs_mir::Module>& modules) {
    for (auto& mod : modules) {
        if (mod.name == "Aero.Native") {
            mod.functions.erase(
                std::remove_if(
                    mod.functions.begin(),
                    mod.functions.end(),
                    [](const rs_mir::Function& fn) {
                        return fn.returnType != rs_sem::PrimitiveType::Void;
                    }),
                mod.functions.end());
        }
    }
}

} // namespace aero
