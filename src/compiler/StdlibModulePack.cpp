#include "aero/compiler/StdlibModulePack.hpp"

#include <algorithm>
#include <array>
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

namespace {

template <std::size_t N>
constexpr bool IsStrictlySortedTokens(const std::array<std::string_view, N>& arr) noexcept {
    for (std::size_t i = 1u; i < N; ++i) {
        if (!(arr[i - 1u] < arr[i])) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] bool ContainsSortedToken(
    const std::array<std::string_view, N>& arr,
    std::string_view token) noexcept {
    return std::binary_search(arr.begin(), arr.end(), token);
}

constexpr std::array<std::string_view, 3> kRuntimeXamlIdentTokens = {
    "CompiledXamlCatalog",
    "XamlReader",
    "__AeroCompiledXamlCatalogInit",
};
static_assert(IsStrictlySortedTokens(kRuntimeXamlIdentTokens));

constexpr std::array<std::string_view, 38> kWorkbenchIdentTokens = {
    "AssetBrowser",
    "CodeDiagnosticItem",
    "CodeEditor",
    "CodeEditorView",
    "ColorPicker",
    "CurveEditor",
    "DataGrid",
    "DockLayout",
    "DockPane",
    "DockSplitNode",
    "DockTabGroup",
    "DockZone",
    "DockingWorkspace",
    "EditLifecyclePhase",
    "EditorMath",
    "InspectorPanel",
    "InspectorPropertyItem",
    "NodeGraphCanvas",
    "NodeGraphEditor",
    "NodeGraphLinkData",
    "NodeGraphNodeData",
    "NumericScrubber",
    "PropertyAssetActionEventArgs",
    "PropertyAssetActionEventHandler",
    "PropertyCardActionEventArgs",
    "PropertyCardActionEventHandler",
    "PropertyGrid",
    "TimelineEditor",
    "TreeListView",
    "ValueEditEventHandler",
    "ValueEditLifecycleEventArgs",
    "Vector2Field",
    "Vector3Field",
    "Viewport3D",
    "ViewportInputEventArgs",
    "ViewportInputEventHandler",
    "ViewportSurface",
    "__RegisterWorkbenchXamlFactories",
};
static_assert(IsStrictlySortedTokens(kWorkbenchIdentTokens));

constexpr std::array<std::string_view, 103> kControlsIdentTokens = {
    "AeroFluent",
    "BackgroundEffectBehavior",
    "BehaviorBase",
    "BehaviorDataTrigger",
    "BehaviorEventTrigger",
    "BehaviorTriggerBase",
    "Button",
    "ButtonBase",
    "Calendar",
    "ChangePropertyAction",
    "CheckBox",
    "ComboBox",
    "ComboBoxItem",
    "CommandTriggerHelper",
    "ComparisonCondition",
    "ConditionBehavior",
    "ConditionalExpression",
    "ContextMenu",
    "ControlStoryboardAction",
    "DatePicker",
    "DateTimeControl",
    "DragDrop",
    "EventTrigger",
    "FluidMoveBehavior",
    "FluidMoveSetTagBehavior",
    "GoToStateAction",
    "GridSplitter",
    "GridView",
    "GridViewColumn",
    "GridViewColumnHeader",
    "GridViewHeaderRowPresenter",
    "GridViewRowPresenter",
    "HeaderedItemsControl",
    "Hyperlink",
    "Interaction",
    "InvokeCommandAction",
    "ItemsControl",
    "ItemsPanelTemplate",
    "ItemsPresenter",
    "KeyTrigger",
    "ListBox",
    "ListBoxItem",
    "ListView",
    "ListViewItem",
    "MediaElement",
    "Menu",
    "MenuBase",
    "MenuItem",
    "MouseDragElementBehavior",
    "NavDestination",
    "NavigateBackAction",
    "NavigateToAction",
    "Navigation",
    "NavigationService",
    "PasswordBox",
    "PauseMediaAction",
    "PlayMediaAction",
    "PlaySoundAction",
    "Popup",
    "PopupService",
    "ProgressBar",
    "PropertyChangedTrigger",
    "RadialNeedle",
    "RadialTicks",
    "RadioButton",
    "RangeBase",
    "RemoveElementAction",
    "RepeatButton",
    "RewindMediaAction",
    "RichText",
    "RichTextBox",
    "RiveControl",
    "RiveInput",
    "ScrollBar",
    "ScrollViewer",
    "SelectAction",
    "SelectAllAction",
    "SelectableItem",
    "Selector",
    "SetFocusAction",
    "Slider",
    "StatusBar",
    "StatusBarItem",
    "StopMediaAction",
    "StoryboardCompletedTrigger",
    "StyleInteraction",
    "TabControl",
    "TabItem",
    "TextBox",
    "TextBoxBase",
    "Thumb",
    "TickBar",
    "TimerTrigger",
    "ToggleButton",
    "ToolBar",
    "ToolTip",
    "ToolTipService",
    "Track",
    "TransitionContentPresenter",
    "TranslateZoomRotateBehavior",
    "TreeView",
    "TreeViewItem",
    "TriggerAction",
};
static_assert(IsStrictlySortedTokens(kControlsIdentTokens));

constexpr std::array<std::string_view, 30> kControlsFluentCallTokens = {
    "Box",
    "Btn",
    "CanvasBox",
    "Card",
    "Check",
    "Divider",
    "DockBox",
    "DropDown",
    "GridBox",
    "HStack",
    "Heading",
    "InputBox",
    "Label",
    "ListPanel",
    "MutedLabel",
    "Pic",
    "PrimaryBtn",
    "Progress",
    "Radio",
    "ScrollBox",
    "SecretBox",
    "Slide",
    "SvgPath",
    "Tabs",
    "ToggleBtn",
    "Tree",
    "VStack",
    "Win",
    "WrapBox",
    "ZStack",
};
static_assert(IsStrictlySortedTokens(kControlsFluentCallTokens));

[[nodiscard]] constexpr bool IsIdentStartChar(char ch) noexcept {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

[[nodiscard]] constexpr bool IsIdentContChar(char ch) noexcept {
    return IsIdentStartChar(ch) || (ch >= '0' && ch <= '9');
}

[[nodiscard]] constexpr bool IsXmlNameChar(char ch) noexcept {
    return IsIdentContChar(ch) || ch == ':' || ch == '.' || ch == '-';
}

void MatchPackageIdentifierToken(std::string_view tok, std::uint32_t& mask) noexcept {
    if (tok.empty()) {
        return;
    }
    if (ContainsSortedToken(kRuntimeXamlIdentTokens, tok)) {
        mask |= AeroStdlibPkg_RuntimeXaml | AeroStdlibPkg_Controls;
    }
    if (ContainsSortedToken(kWorkbenchIdentTokens, tok)) {
        mask |= AeroStdlibPkg_Workbench | AeroStdlibPkg_Controls;
    }
    if (ContainsSortedToken(kControlsIdentTokens, tok)) {
        mask |= AeroStdlibPkg_Controls;
    }
}

void CheckResourceUriTriggers(std::string_view sv, std::uint32_t& mask) noexcept {
    if (sv.find("Data/Samples/") != std::string_view::npos ||
        sv.find("NoesisTheme.Brushes") != std::string_view::npos) {
        mask |= AeroStdlibPkg_RuntimeXaml | AeroStdlibPkg_Controls;
    }
}

void ScanIdentTokensInText(std::string_view text, std::uint32_t& mask) noexcept {
    std::size_t i = 0u;
    const std::size_t n = text.size();
    while (i < n) {
        while (i < n && !IsIdentStartChar(text[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < n && IsIdentContChar(text[i])) {
            ++i;
        }
        if (i > start) {
            MatchPackageIdentifierToken(text.substr(start, i - start), mask);
        }
    }
}

void ScanXamlForPackages(std::string_view xaml, std::uint32_t& mask) noexcept {
    const std::size_t n = xaml.size();
    std::size_t i = 0u;
    while (i < n) {
        const std::size_t lt = xaml.find('<', i);
        if (lt == std::string_view::npos) {
            break;
        }
        i = lt;
        if (i + 3u < n && xaml.substr(i, 4u) == "<!--") {
            const std::size_t endComment = xaml.find("-->", i + 4u);
            if (endComment == std::string_view::npos) {
                break;
            }
            i = endComment + 3u;
            continue;
        }
        if (i + 8u < n && xaml.substr(i, 9u) == "<![CDATA[") {
            const std::size_t endCdata = xaml.find("]]>", i + 9u);
            if (endCdata == std::string_view::npos) {
                break;
            }
            i = endCdata + 3u;
            continue;
        }
        if (i + 1u < n && (xaml[i + 1u] == '?' || xaml[i + 1u] == '!')) {
            const std::size_t gt = xaml.find('>', i + 2u);
            if (gt == std::string_view::npos) {
                break;
            }
            i = gt + 1u;
            continue;
        }

        ++i; // skip '<'
        if (i < n && xaml[i] == '/') {
            ++i;
        }
        while (i < n && (xaml[i] == ' ' || xaml[i] == '\t' || xaml[i] == '\r' || xaml[i] == '\n')) {
            ++i;
        }
        const std::size_t tagStart = i;
        while (i < n && IsXmlNameChar(xaml[i])) {
            ++i;
        }
        if (i > tagStart) {
            std::string_view tagName = xaml.substr(tagStart, i - tagStart);
            if (const std::size_t colon = tagName.find(':'); colon != std::string_view::npos) {
                tagName = tagName.substr(colon + 1u);
            }
            if (const std::size_t dot = tagName.find('.'); dot != std::string_view::npos) {
                MatchPackageIdentifierToken(tagName.substr(0u, dot), mask);
                MatchPackageIdentifierToken(tagName.substr(dot + 1u), mask);
            } else {
                MatchPackageIdentifierToken(tagName, mask);
            }
        }

        // Scan attributes inside the tag until closing '>'
        while (i < n) {
            while (i < n && (xaml[i] == ' ' || xaml[i] == '\t' || xaml[i] == '\r' ||
                             xaml[i] == '\n' || xaml[i] == '/')) {
                ++i;
            }
            if (i >= n) {
                break;
            }
            if (xaml[i] == '>') {
                ++i;
                break;
            }
            const std::size_t attrStart = i;
            while (i < n && IsXmlNameChar(xaml[i])) {
                ++i;
            }
            std::string_view attrLocal;
            if (i > attrStart) {
                attrLocal = xaml.substr(attrStart, i - attrStart);
                if (const std::size_t colon = attrLocal.find(':'); colon != std::string_view::npos) {
                    attrLocal = attrLocal.substr(colon + 1u);
                }
                if (const std::size_t dot = attrLocal.find('.'); dot != std::string_view::npos) {
                    MatchPackageIdentifierToken(attrLocal.substr(0u, dot), mask);
                } else if (attrLocal == "ToolTip" || attrLocal == "ContextMenu") {
                    MatchPackageIdentifierToken(attrLocal, mask);
                }
            } else {
                // Unexpected character inside tag; advance safely to avoid stalling.
                ++i;
                continue;
            }

            while (i < n && (xaml[i] == ' ' || xaml[i] == '\t' || xaml[i] == '\r' || xaml[i] == '\n')) {
                ++i;
            }
            if (i < n && xaml[i] == '=') {
                ++i;
                while (i < n && (xaml[i] == ' ' || xaml[i] == '\t' || xaml[i] == '\r' || xaml[i] == '\n')) {
                    ++i;
                }
                if (i < n && (xaml[i] == '"' || xaml[i] == '\'')) {
                    const char quote = xaml[i++];
                    const std::size_t valStart = i;
                    while (i < n && xaml[i] != quote) {
                        ++i;
                    }
                    const std::string_view attrVal = xaml.substr(valStart, i - valStart);
                    if (i < n && xaml[i] == quote) {
                        ++i;
                    }
                    CheckResourceUriTriggers(attrVal, mask);
                    if (attrLocal == "TargetType" ||
                        attrLocal == "DataType" ||
                        attrLocal == "Property" ||
                        attrVal.find('{') != std::string_view::npos ||
                        attrVal.find("x:Type") != std::string_view::npos) {
                        ScanIdentTokensInText(attrVal, mask);
                    }
                }
            }
        }
    }
}

void ScanScriptForPackages(std::string_view script, std::uint32_t& mask) noexcept {
    const std::size_t n = script.size();
    std::size_t i = 0u;
    std::string_view prevIdent;
    char prevPunct = '\0';

    while (i < n) {
        const char ch = script[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++i;
            continue;
        }
        // Line comment //...
        if (ch == '/' && i + 1u < n && script[i + 1u] == '/') {
            i += 2u;
            while (i < n && script[i] != '\n') {
                ++i;
            }
            continue;
        }
        // Block comment /*...*/
        if (ch == '/' && i + 1u < n && script[i + 1u] == '*') {
            i += 2u;
            while (i + 1u < n && !(script[i] == '*' && script[i + 1u] == '/')) {
                ++i;
            }
            i = (i + 1u < n) ? (i + 2u) : n;
            continue;
        }
        // Verbatim string @"..." or $@"..." / @$"..."
        const bool isVerbatim =
            (ch == '@' && i + 1u < n && script[i + 1u] == '"') ||
            ((ch == '$' || ch == '@') && i + 2u < n &&
             ((script[i] == '$' && script[i + 1u] == '@') ||
              (script[i] == '@' && script[i + 1u] == '$')) &&
             script[i + 2u] == '"');
        if (isVerbatim) {
            const bool interpolated = (ch == '$' || script[i + 1u] == '$');
            i += (script[i + 1u] == '"') ? 2u : 3u;
            const std::size_t strStart = i;
            while (i < n) {
                if (script[i] == '"') {
                    if (i + 1u < n && script[i + 1u] == '"') {
                        i += 2u;
                        continue;
                    }
                    break;
                }
                if (interpolated && script[i] == '{') {
                    if (i + 1u < n && script[i + 1u] == '{') {
                        i += 2u;
                        continue;
                    }
                    ++i;
                    const std::size_t holeStart = i;
                    int braceDepth = 1;
                    while (i < n && braceDepth > 0) {
                        if (script[i] == '{') {
                            ++braceDepth;
                        } else if (script[i] == '}') {
                            --braceDepth;
                        }
                        if (braceDepth > 0) {
                            ++i;
                        }
                    }
                    ScanScriptForPackages(script.substr(holeStart, i - holeStart), mask);
                    if (i < n && script[i] == '}') {
                        ++i;
                    }
                    continue;
                }
                ++i;
            }
            CheckResourceUriTriggers(script.substr(strStart, i - strStart), mask);
            if (i < n && script[i] == '"') {
                ++i;
            }
            prevIdent = {};
            prevPunct = '"';
            continue;
        }
        // Interpolated string $"..."
        if (ch == '$' && i + 1u < n && script[i + 1u] == '"') {
            i += 2u;
            const std::size_t strStart = i;
            while (i < n && script[i] != '"') {
                if (script[i] == '\\' && i + 1u < n) {
                    i += 2u;
                    continue;
                }
                if (script[i] == '{') {
                    if (i + 1u < n && script[i + 1u] == '{') {
                        i += 2u;
                        continue;
                    }
                    ++i;
                    const std::size_t holeStart = i;
                    int braceDepth = 1;
                    while (i < n && braceDepth > 0) {
                        if (script[i] == '\\' && i + 1u < n) {
                            i += 2u;
                            continue;
                        }
                        if (script[i] == '{') {
                            ++braceDepth;
                        } else if (script[i] == '}') {
                            --braceDepth;
                        }
                        if (braceDepth > 0) {
                            ++i;
                        }
                    }
                    ScanScriptForPackages(script.substr(holeStart, i - holeStart), mask);
                    if (i < n && script[i] == '}') {
                        ++i;
                    }
                    continue;
                }
                ++i;
            }
            CheckResourceUriTriggers(script.substr(strStart, i - strStart), mask);
            if (i < n && script[i] == '"') {
                ++i;
            }
            prevIdent = {};
            prevPunct = '"';
            continue;
        }
        // Regular string literal "..."
        if (ch == '"') {
            ++i;
            const std::size_t strStart = i;
            while (i < n && script[i] != '"') {
                if (script[i] == '\\' && i + 1u < n) {
                    i += 2u;
                    continue;
                }
                ++i;
            }
            CheckResourceUriTriggers(script.substr(strStart, i - strStart), mask);
            if (i < n && script[i] == '"') {
                ++i;
            }
            prevIdent = {};
            prevPunct = '"';
            continue;
        }
        // Character literal '...'
        if (ch == '\'') {
            ++i;
            while (i < n && script[i] != '\'') {
                if (script[i] == '\\' && i + 1u < n) {
                    i += 2u;
                    continue;
                }
                ++i;
            }
            if (i < n && script[i] == '\'') {
                ++i;
            }
            prevIdent = {};
            prevPunct = '\'';
            continue;
        }
        // Identifier token [A-Za-z_][A-Za-z0-9_]*
        if (IsIdentStartChar(ch)) {
            const std::size_t start = i++;
            while (i < n && IsIdentContChar(script[i])) {
                ++i;
            }
            const std::string_view tok = script.substr(start, i - start);
            MatchPackageIdentifierToken(tok, mask);

            if ((mask & AeroStdlibPkg_Controls) == 0u &&
                ContainsSortedToken(kControlsFluentCallTokens, tok)) {
                std::size_t look = i;
                while (look < n && (script[look] == ' ' || script[look] == '\t' ||
                                    script[look] == '\r' || script[look] == '\n')) {
                    ++look;
                }
                const bool isCallSyntax = (look < n && script[look] == '(');
                const bool isQualifiedCall = (prevPunct == '.' && prevIdent != "UI");
                const bool isMethodDecl =
                    prevPunct == '\0' && !prevIdent.empty() &&
                    prevIdent != "return" && prevIdent != "else";
                if (isCallSyntax && !isQualifiedCall && !isMethodDecl) {
                    mask |= AeroStdlibPkg_Controls;
                }
            }
            prevIdent = tok;
            prevPunct = '\0';
            continue;
        }

        prevPunct = ch;
        if (ch != '.') {
            prevIdent = {};
        }
        ++i;
    }
}

} // namespace

std::uint32_t InferRequiredStdlibPackages(const AeroV3CompileInput& input) noexcept {
    std::uint32_t mask = AeroStdlibPkg_Core;
    if ((input.stdlibPackages & AeroStdlibPkg_GameHud) != 0u) {
        mask |= AeroStdlibPkg_GameHud;
    }
    if (!input.catalogXamlSources.empty()) {
        mask |= AeroStdlibPkg_Controls | AeroStdlibPkg_RuntimeXaml;
    }
    if (!input.xamlContent.empty()) {
        ScanXamlForPackages(input.xamlContent, mask);
    }
    for (const auto& sf : input.additionalXamlSources) {
        if (!sf.content.empty()) {
            ScanXamlForPackages(sf.content, mask);
        }
    }
    for (const auto& sf : input.scriptSources) {
        if (!sf.content.empty()) {
            ScanScriptForPackages(sf.content, mask);
        }
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
