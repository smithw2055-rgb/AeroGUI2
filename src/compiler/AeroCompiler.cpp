#include <thread>
#include "aero/compiler/AeroCompiler.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

#include "aero/controls/Controls.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"

namespace aero {

namespace {

std::string_view Trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

float ParseFloat32(std::string_view text, float fallback = 0.0f) noexcept {
    text = Trim(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        text.remove_suffix(1);
    }
    std::string buf(text);
    char* end = nullptr;
    const float val = std::strtof(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return fallback;
    }
    return val;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::pair<GridUnitType, float> ParseGridLength(std::string_view raw) noexcept {
    raw = Trim(raw);
    if (raw.empty() || raw == "*") {
        return {GridUnitType::Star, 1.0f};
    }
    if (EqualsIgnoreCase(raw, "Auto")) {
        return {GridUnitType::Auto, 1.0f};
    }
    if (raw.back() == '*') {
        const std::string_view numPart = Trim(raw.substr(0, raw.size() - 1));
        const float starWeight = numPart.empty() ? 1.0f : ParseFloat32(numPart, 1.0f);
        return {GridUnitType::Star, starWeight};
    }
    return {GridUnitType::Pixel, ParseFloat32(raw, 0.0f)};
}

std::string_view StripXmlPrefix(std::string_view name) noexcept {
    if (const std::size_t colon = name.find(':'); colon != std::string_view::npos) {
        return name.substr(colon + 1);
    }
    return name;
}

bool EndsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool TryParseResourceMarkupExtension(
    std::string_view rawExpr,
    std::string_view& outKey,
    std::uint8_t& outFlags) noexcept {
    outKey = {};
    outFlags = AxbResourceFlag_StaticResource;
    std::string_view v = Trim(rawExpr);
    if (v.size() <= 2u || v.front() != '{' || v.back() != '}') {
        return false;
    }
    std::string_view inner = Trim(v.substr(1u, v.size() - 2u));
    if (inner.rfind("StaticResource ", 0) == 0) {
        outFlags = AxbResourceFlag_StaticResource;
        inner = Trim(inner.substr(15u));
    } else if (inner.rfind("DynamicResource ", 0) == 0) {
        outFlags = AxbResourceFlag_DynamicResource;
        inner = Trim(inner.substr(16u));
    } else if (inner.rfind("noesis:Loc ", 0) == 0) {
        outFlags = AxbResourceFlag_Loc;
        inner = Trim(inner.substr(11u));
    } else if (inner.rfind("Loc ", 0) == 0) {
        outFlags = AxbResourceFlag_Loc;
        inner = Trim(inner.substr(4u));
    } else {
        return false;
    }

    if (const std::size_t comma = inner.find(','); comma != std::string_view::npos) {
        inner = Trim(inner.substr(0u, comma));
    }
    if (inner.rfind("ResourceKey=", 0) == 0) {
        inner = Trim(inner.substr(12u));
    } else if (inner.rfind("Key=", 0) == 0) {
        inner = Trim(inner.substr(4u));
    }
    if (inner.size() >= 2u &&
        ((inner.front() == '\'' && inner.back() == '\'') ||
         (inner.front() == '"' && inner.back() == '"'))) {
        inner = Trim(inner.substr(1u, inner.size() - 2u));
    }
    outKey = inner;
    return !outKey.empty();
}

std::string_view ExtractMarkupResourceKey(std::string_view rawExpr) noexcept {
    std::string_view key{};
    std::uint8_t flags = 0u;
    if (TryParseResourceMarkupExtension(rawExpr, key, flags)) {
        return key;
    }
    return {};
}

std::string StripUtf8Bom(std::string_view raw) {
    if (raw.size() >= 3u &&
        static_cast<unsigned char>(raw[0]) == 0xEFu &&
        static_cast<unsigned char>(raw[1]) == 0xBBu &&
        static_cast<unsigned char>(raw[2]) == 0xBFu) {
        raw.remove_prefix(3u);
    }
    return std::string(raw);
}

bool ReadFileBytesNoThrow(const std::string& pathStr, std::string& outContent) {
    if (pathStr.empty()) {
        return false;
    }
    std::FILE* fp = std::fopen(pathStr.c_str(), "rb");
    if (!fp) {
        return false;
    }
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    const long sz = std::ftell(fp);
    if (sz < 0) {
        std::fclose(fp);
        return false;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0) {
        std::fclose(fp);
        return false;
    }
    outContent.resize(static_cast<std::size_t>(sz));
    if (sz > 0) {
        const std::size_t nRead = std::fread(outContent.data(), 1u, static_cast<std::size_t>(sz), fp);
        outContent.resize(nRead);
    }
    std::fclose(fp);
    outContent = StripUtf8Bom(outContent);
    return true;
}

std::string NormalizeSlashes(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw) {
        out.push_back(ch == '\\' ? '/' : ch);
    }
    return out;
}

std::string CanonicalizeCycleKeyNoThrow(std::string_view rawPath) {
    const std::string norm = NormalizeSlashes(Trim(rawPath));
    if (norm.empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path fsPath(norm);
    if (std::filesystem::exists(fsPath, ec) && !ec) {
        const std::filesystem::path canon = std::filesystem::weakly_canonical(fsPath, ec);
        if (!ec && !canon.empty()) {
            return canon.generic_string();
        }
    }
    return fsPath.lexically_normal().generic_string();
}

struct ParsedPackResourceUri {
    std::string assemblyName;
    std::string subPath;
    bool hadPackOrComponentPrefix = false;
};

ParsedPackResourceUri ParsePackOrRelativeUri(std::string_view rawUri) {
    ParsedPackResourceUri out{};
    std::string s = NormalizeSlashes(Trim(rawUri));
    if (s.rfind("pack://application:,,,/", 0) == 0) {
        s.erase(0, 23u);
        out.hadPackOrComponentPrefix = true;
    } else if (s.rfind("pack://application:,,,", 0) == 0) {
        s.erase(0, 22u);
        out.hadPackOrComponentPrefix = true;
    } else if (s.rfind("pack://siteoforigin:,,,/", 0) == 0) {
        s.erase(0, 24u);
        out.hadPackOrComponentPrefix = true;
    }

    if (const std::size_t compPos = s.find(";component/"); compPos != std::string::npos) {
        out.hadPackOrComponentPrefix = true;
        std::string asmPart = s.substr(0, compPos);
        while (!asmPart.empty() && asmPart.front() == '/') {
            asmPart.erase(0, 1u);
        }
        if (const std::size_t semiVer = asmPart.find(';'); semiVer != std::string::npos) {
            asmPart = asmPart.substr(0, semiVer);
        }
        out.assemblyName = std::move(asmPart);
        out.subPath = s.substr(compPos + 11u);
    } else {
        if (out.hadPackOrComponentPrefix) {
            while (!s.empty() && s.front() == '/') {
                s.erase(0, 1u);
            }
        }
        out.subPath = std::move(s);
    }
    return out;
}

bool ResolveExternalXamlSource(
    const AeroCompileInput& input,
    std::string_view currentXamlPath,
    std::string_view requestedSource,
    std::string& outResolvedPath,
    std::string& outContent) {
    requestedSource = Trim(requestedSource);
    if (requestedSource.empty()) {
        return false;
    }

    if (input.externalXamlResolver) {
        std::string cbResolved;
        std::string cbContent;
        if (input.externalXamlResolver(requestedSource, currentXamlPath, cbResolved, cbContent)) {
            outResolvedPath = cbResolved.empty() ? std::string(requestedSource) : std::move(cbResolved);
            outContent = StripUtf8Bom(cbContent);
            return true;
        }
    }

    const ParsedPackResourceUri parsed = ParsePackOrRelativeUri(requestedSource);
    std::string relSub = parsed.subPath;
    while (!relSub.empty() && relSub.front() == '/') {
        relSub.erase(0, 1u);
    }

    std::vector<std::filesystem::path> candidates;
    auto addCandidate = [&](const std::filesystem::path& p) {
        if (!p.empty()) {
            candidates.push_back(p);
        }
    };

    const std::string rawNorm = NormalizeSlashes(requestedSource);
    if (!parsed.hadPackOrComponentPrefix) {
        addCandidate(std::filesystem::path(rawNorm));
    }

    std::filesystem::path curDir;
    if (!currentXamlPath.empty()) {
        curDir = std::filesystem::path(NormalizeSlashes(currentXamlPath)).parent_path();
    }

    auto appendUnderBase = [&](const std::filesystem::path& baseDir) {
        if (baseDir.empty()) {
            return;
        }
        if (!parsed.subPath.empty() && parsed.subPath.front() != '/') {
            addCandidate(baseDir / parsed.subPath);
        }
        if (!relSub.empty()) {
            addCandidate(baseDir / relSub);
        }
        if (!parsed.assemblyName.empty() && !relSub.empty()) {
            addCandidate(baseDir / parsed.assemblyName / relSub);
            addCandidate(baseDir / parsed.assemblyName / "Data" / relSub);
        }
        if (!relSub.empty()) {
            addCandidate(baseDir / "Data" / relSub);
            addCandidate(baseDir / "Theme" / "Data" / relSub);
        }
    };

    appendUnderBase(curDir);
    if (!input.resourceSearchRoot.empty()) {
        appendUnderBase(std::filesystem::path(NormalizeSlashes(input.resourceSearchRoot)));
    }
    for (const auto& sp : input.resourceSearchPaths) {
        if (!sp.empty()) {
            appendUnderBase(std::filesystem::path(NormalizeSlashes(sp)));
        }
    }
    if (!curDir.empty() && !parsed.assemblyName.empty() && !relSub.empty()) {
        const std::filesystem::path parentUp = curDir.parent_path();
        if (!parentUp.empty()) {
            addCandidate(parentUp / parsed.assemblyName / relSub);
            addCandidate(parentUp / parsed.assemblyName / "Data" / relSub);
            addCandidate(parentUp / "Theme" / "Data" / relSub);
            const std::filesystem::path parentUp2 = parentUp.parent_path();
            if (!parentUp2.empty()) {
                addCandidate(parentUp2 / parsed.assemblyName / relSub);
                addCandidate(parentUp2 / parsed.assemblyName / "Data" / relSub);
                addCandidate(parentUp2 / "Theme" / "Data" / relSub);
                const std::filesystem::path parentUp3 = parentUp2.parent_path();
                if (!parentUp3.empty()) {
                    addCandidate(parentUp3 / "Theme" / "Data" / relSub);
                }
            }
        }
    }
    if (!relSub.empty()) {
        addCandidate(std::filesystem::path(relSub));
    }

    std::error_code ec;
    for (const auto& cand : candidates) {
        ec.clear();
        if (!std::filesystem::exists(cand, ec) || ec) {
            continue;
        }
        ec.clear();
        if (!std::filesystem::is_regular_file(cand, ec) || ec) {
            continue;
        }
        const std::string candStr = cand.string();
        if (ReadFileBytesNoThrow(candStr, outContent)) {
            ec.clear();
            const std::filesystem::path canon = std::filesystem::weakly_canonical(cand, ec);
            outResolvedPath = (!ec && !canon.empty()) ? canon.generic_string() : NormalizeSlashes(candStr);
            return true;
        }
    }
    return false;
}

void UpsertPackageResource(AxbPackage& pkg, std::string_view key, UnifiedValue val) {
    if (key.empty()) {
        return;
    }
    for (auto it = pkg.resources.begin(); it != pkg.resources.end(); ++it) {
        if (it->first == key) {
            pkg.resources.erase(it);
            break;
        }
    }
    pkg.resources.emplace_back(std::string(key), std::move(val));
}

void MergeDictionaryPackageInto(
    AxbPackage& dstPkg,
    const AxbPackage& srcDictPkg,
    std::string_view resolvedSource,
    std::unordered_set<std::uint64_t>& bakedPathHashes) {
    auto recordMergedSource = [&](std::string_view srcName) {
        if (srcName.empty()) {
            return;
        }
        for (const auto& existing : dstPkg.mergedDictionarySources) {
            if (existing == srcName) {
                return;
            }
        }
        dstPkg.mergedDictionarySources.emplace_back(srcName);
    };

    for (const auto& transSrc : srcDictPkg.mergedDictionarySources) {
        recordMergedSource(transSrc);
    }
    recordMergedSource(resolvedSource);

    for (const auto& kv : srcDictPkg.resources) {
        if (!kv.first.empty()) {
            UpsertPackageResource(dstPkg, kv.first, kv.second);
        }
    }

    for (const auto& st : srcDictPkg.styles) {
        bool replaced = false;
        if (!st.key.empty()) {
            for (auto& dstSt : dstPkg.styles) {
                if (dstSt.key == st.key) {
                    dstSt = st;
                    replaced = true;
                    break;
                }
            }
        } else if (!st.targetTypeName.empty()) {
            for (auto& dstSt : dstPkg.styles) {
                if (dstSt.key.empty() && dstSt.targetTypeName == st.targetTypeName) {
                    dstSt = st;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            dstPkg.styles.push_back(st);
        }
    }

    for (const auto& sb : srcDictPkg.storyboards) {
        bool replaced = false;
        if (!sb.key.empty()) {
            for (auto& dstSb : dstPkg.storyboards) {
                if (dstSb.key == sb.key) {
                    dstSb = sb;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            dstPkg.storyboards.push_back(sb);
        }
    }

    for (const auto& dt : srcDictPkg.dataTemplates) {
        bool replaced = false;
        if (!dt.key.empty()) {
            for (auto& dstDt : dstPkg.dataTemplates) {
                if (dstDt.key == dt.key) {
                    dstDt = dt;
                    replaced = true;
                    break;
                }
            }
        } else if (!dt.dataTypeName.empty()) {
            for (auto& dstDt : dstPkg.dataTemplates) {
                if (dstDt.key.empty() && dstDt.dataTypeName == dt.dataTypeName) {
                    dstDt = dt;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            dstPkg.dataTemplates.push_back(dt);
        }
    }

    for (const auto& ct : srcDictPkg.controlTemplates) {
        bool replaced = false;
        if (!ct.key.empty()) {
            for (auto& dstCt : dstPkg.controlTemplates) {
                if (dstCt.key == ct.key) {
                    dstCt = ct;
                    replaced = true;
                    break;
                }
            }
        } else if (!ct.targetTypeName.empty()) {
            for (auto& dstCt : dstPkg.controlTemplates) {
                if (dstCt.key.empty() && dstCt.targetTypeName == ct.targetTypeName) {
                    dstCt = ct;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            dstPkg.controlTemplates.push_back(ct);
        }
    }

    for (auto& dstSt : dstPkg.styles) {
        if (dstSt.controlTemplateKey.empty()) {
            for (const auto& setter : dstSt.setters) {
                if (setter.propId == PropId_Template) {
                    if (const auto* sv = std::get_if<StringValue>(&setter.value)) {
                        dstSt.controlTemplateKey = sv->str();
                    }
                }
            }
        }
        if (!dstSt.controlTemplateKey.empty()) {
            if (const AxbControlTemplate* foundCt = dstPkg.FindControlTemplate(dstSt.controlTemplateKey)) {
                dstSt.inlineControlTemplate = std::make_shared<AxbControlTemplate>(*foundCt);
            }
        }
    }

    for (const auto& mp : srcDictPkg.mtsdfPaths) {
        if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
            dstPkg.mtsdfPaths.push_back(mp);
        }
    }
}

std::string DiscoverGlobalResourcesPathForDirectory(const std::filesystem::path& dirPath) {
    if (dirPath.empty()) {
        return {};
    }
    std::error_code ec;
    if (!std::filesystem::exists(dirPath, ec) || ec || !std::filesystem::is_directory(dirPath, ec) || ec) {
        return {};
    }

    // 1. Check *.noesis project descriptor in dirPath for <GlobalResources>...</GlobalResources>
    std::filesystem::directory_iterator it(dirPath, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code entryEc;
            if (!entry.is_regular_file(entryEc) || entryEc) {
                continue;
            }
            const std::filesystem::path p = entry.path();
            if (p.extension() == ".noesis") {
                std::string noesisXml;
                if (ReadFileBytesNoThrow(p.string(), noesisXml)) {
                    const std::size_t openTag = noesisXml.find("<GlobalResources>");
                    const std::size_t closeTag = noesisXml.find("</GlobalResources>");
                    if (openTag != std::string::npos && closeTag != std::string::npos &&
                        closeTag > openTag + 17u) {
                        const std::string_view rel = Trim(
                            std::string_view(noesisXml).substr(openTag + 17u, closeTag - (openTag + 17u)));
                        if (!rel.empty()) {
                            return std::string(rel);
                        }
                    }
                }
            }
        }
    }

    // 2. Fallback: check co-located Resources.xaml in the same directory
    const std::filesystem::path siblingRes = dirPath / "Resources.xaml";
    ec.clear();
    if (std::filesystem::exists(siblingRes, ec) && !ec &&
        std::filesystem::is_regular_file(siblingRes, ec) && !ec) {
        return "Resources.xaml";
    }
    return {};
}

std::string ExtractTargetTypeShortName(std::string_view rawTypeExpr) {
    std::string_view v = Trim(rawTypeExpr);
    if (v.size() > 2u && v.front() == '{' && v.back() == '}') {
        v = Trim(v.substr(1u, v.size() - 2u));
        if (v.rfind("x:Type ", 0) == 0) {
            v = Trim(v.substr(7u));
        } else if (v.rfind("Type ", 0) == 0) {
            v = Trim(v.substr(5u));
        }
        if (v.rfind("TypeName=", 0) == 0) {
            v = Trim(v.substr(9u));
        }
    }
    v = StripXmlPrefix(v);
    if (const std::size_t dot = v.rfind('.'); dot != std::string_view::npos) {
        v = v.substr(dot + 1u);
    }
    return std::string(v);
}

constexpr std::uint16_t kUnmappedPropId = 0xFFFFu;

[[nodiscard]] bool IsInstanceSubHopPropertyPath(std::string_view path) noexcept {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view prefix = path.substr(0u, dot);
    return prefix == "SelectedItem" || prefix == "DataContext" || prefix == "Content" ||
           prefix == "PlacementTarget" || prefix == "Header" || prefix == "Tag";
}

std::uint16_t ResolveMappedSetterPropertyId(std::string_view rawProp) noexcept {
    rawProp = Trim(rawProp);
    if (rawProp.empty()) {
        return kUnmappedPropId;
    }
    if (rawProp == "Tag") {
        return PropId_Tag;
    }
    const std::uint16_t direct = ResolveBuiltinPropertyId(rawProp);
    if (direct != PropId_Tag) {
        return direct;
    }
    if (const std::size_t dot = rawProp.rfind('.'); dot != std::string_view::npos) {
        const std::string_view leaf = rawProp.substr(dot + 1u);
        if (leaf == "Tag") {
            return PropId_Tag;
        }
        const std::uint16_t stripped = ResolveBuiltinPropertyId(leaf);
        if (stripped != PropId_Tag) {
            return stripped;
        }
    }
    return kUnmappedPropId;
}

std::uint16_t ResolveMappedTriggerConditionId(std::string_view rawProp) noexcept {
    rawProp = Trim(rawProp);
    if (rawProp.empty()) {
        return kUnmappedPropId;
    }
    if (rawProp == "Tag" || EndsWith(rawProp, ".Tag")) {
        return PropId_Tag;
    }
    const std::uint16_t id = ResolveTriggerConditionPropertyId(rawProp);
    return (id == PropId_Tag) ? kUnmappedPropId : id;
}

std::uint16_t ResolveMappedAnimationPropertyId(std::string_view rawPath) noexcept {
    rawPath = Trim(rawPath);
    if (rawPath.empty()) {
        return kUnmappedPropId;
    }
    if (rawPath == "Tag" || EndsWith(rawPath, ".Tag") || EndsWith(rawPath, ".Tag)")) {
        return PropId_Tag;
    }
    const std::uint16_t id = ResolveAnimationTargetPropertyId(rawPath);
    return (id == PropId_Tag) ? kUnmappedPropId : id;
}

bool IsAnimationTimelineTag(std::string_view shortTag) noexcept {
    return shortTag == "DoubleAnimation" ||
           shortTag == "SingleAnimation" ||
           shortTag == "ColorAnimation" ||
           shortTag == "ThicknessAnimation" ||
           shortTag == "PointAnimation" ||
           shortTag == "DoubleAnimationUsingKeyFrames" ||
           shortTag == "SingleAnimationUsingKeyFrames" ||
           shortTag == "ColorAnimationUsingKeyFrames" ||
           shortTag == "ThicknessAnimationUsingKeyFrames" ||
           shortTag == "BooleanAnimationUsingKeyFrames" ||
           shortTag == "ObjectAnimationUsingKeyFrames" ||
           shortTag == "PointAnimationUsingKeyFrames";
}

bool IsAnimationKeyFrameTag(std::string_view shortTag) noexcept {
    return EndsWith(shortTag, "DoubleKeyFrame") ||
           EndsWith(shortTag, "SingleKeyFrame") ||
           EndsWith(shortTag, "ColorKeyFrame") ||
           EndsWith(shortTag, "ThicknessKeyFrame") ||
           EndsWith(shortTag, "BooleanKeyFrame") ||
           EndsWith(shortTag, "ObjectKeyFrame") ||
           EndsWith(shortTag, "PointKeyFrame");
}

bool IsEasingFunctionTag(std::string_view shortTag) noexcept {
    return shortTag.size() > 4u && EndsWith(shortTag, "Ease");
}

std::int32_t ParseRepeatCountAttr(std::string_view rawVal) noexcept {
    rawVal = Trim(rawVal);
    if (rawVal.empty()) {
        return 1;
    }
    if (EqualsIgnoreCase(rawVal, "Forever")) {
        return 0;
    }
    if (rawVal.back() == 'x' || rawVal.back() == 'X') {
        rawVal.remove_suffix(1);
    }
    const float f = ParseFloat32(rawVal, 1.0f);
    return (f <= 0.0f) ? 0 : std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(f)));
}

std::string NormalizeXamlFormatOrExpr(std::string_view raw) {
    std::string_view s = Trim(raw);
    if (s.size() >= 2u && s.front() == '\'' && s.back() == '\'') {
        s = s.substr(1u, s.size() - 2u);
    }
    if (s.size() >= 2u && s[0] == '{' && s[1] == '}') {
        s.remove_prefix(2u);
    }
    return DecodeXamlXmlEntities(s);
}

struct ParsedBindingClause {
    bool isValidBinding = false;
    bool hasExplicitMode = false;
    bool isAttachedPropertyPath = false;
    BindingMode mode = BindingMode::OneWay;
    std::string_view pathPart;
    std::string_view fallbackValue;
    bool hasTargetNullValue = false;
    std::string_view targetNullValue;
    std::string_view elementName;
    std::string relativeAncestorType;
    bool relativeSelf = false;
    bool relativeTemplatedParent = false;
    ValueConverterKind converter = ValueConverterKind::None;
    std::string converterKey;
    std::string converterParameter;
    std::string stringFormat;
    std::string staticResourceSourceKey;
};

struct ParsedTemplateBindingClause {
    bool isValid = false;
    std::string_view sourcePath;
    ValueConverterKind converter = ValueConverterKind::None;
};

std::uint16_t ResolveTemplateBindingSourcePropId(std::string_view rawProp) noexcept {
    rawProp = Trim(rawProp);
    if (rawProp.size() > 2u && rawProp.front() == '(' && rawProp.back() == ')') {
        rawProp = Trim(rawProp.substr(1u, rawProp.size() - 2u));
    }
    if (rawProp.empty()) {
        return kUnmappedPropId;
    }
    if (rawProp == "Content" || EndsWith(rawProp, ".Content")) {
        return PropId_Content;
    }
    if (rawProp == "Tag" || EndsWith(rawProp, ".Tag")) {
        return PropId_Tag;
    }
    const std::uint16_t direct = ResolveBuiltinPropertyId(rawProp);
    if (direct != PropId_Tag) {
        return direct;
    }
    if (const std::size_t dot = rawProp.rfind('.'); dot != std::string_view::npos) {
        const std::string_view leaf = rawProp.substr(dot + 1u);
        if (leaf == "Content") return PropId_Content;
        if (leaf == "Tag") return PropId_Tag;
        const std::uint16_t stripped = ResolveBuiltinPropertyId(leaf);
        if (stripped != PropId_Tag) {
            return stripped;
        }
    }
    return PropId_Tag;
}

ParsedTemplateBindingClause ParseTemplateBindingMarkupExpression(std::string_view rawExpr) {
    ParsedTemplateBindingClause out{};
    std::string_view v = Trim(rawExpr);
    if (v.size() <= 2u || v.front() != '{' || v.back() != '}') {
        return out;
    }
    std::string_view inner = Trim(v.substr(1u, v.size() - 2u));
    if (inner == "TemplateBinding") {
        return out;
    }
    if (inner.rfind("TemplateBinding ", 0) == 0 || inner.rfind("TemplateBinding	", 0) == 0) {
        inner = Trim(inner.substr(15u));
    } else {
        return out;
    }
    std::size_t pos = 0u;
    bool firstClause = true;
    while (pos < inner.size()) {
        int braceDepth = 0;
        std::size_t end = pos;
        while (end < inner.size()) {
            if (inner[end] == '{') ++braceDepth;
            else if (inner[end] == '}' && braceDepth > 0) --braceDepth;
            else if (inner[end] == ',' && braceDepth == 0) break;
            ++end;
        }
        const std::string_view clause = Trim(inner.substr(pos, end - pos));
        if (clause.rfind("Property=", 0) == 0) {
            out.sourcePath = Trim(clause.substr(9u));
        } else if (clause.rfind("Path=", 0) == 0) {
            out.sourcePath = Trim(clause.substr(5u));
        } else if (clause.rfind("Converter=", 0) == 0) {
            const std::string_view convExpr = Trim(clause.substr(10u));
            const std::string_view convKey = ExtractMarkupResourceKey(convExpr);
            out.converter = ParseValueConverterKind(!convKey.empty() ? convKey : convExpr);
        } else if (firstClause && clause.find('=') == std::string_view::npos) {
            out.sourcePath = clause;
        }
        firstClause = false;
        if (end >= inner.size()) break;
        pos = end + 1u;
    }
    if (out.sourcePath.size() > 2u && out.sourcePath.front() == '(' && out.sourcePath.back() == ')') {
        out.sourcePath = Trim(out.sourcePath.substr(1u, out.sourcePath.size() - 2u));
    }
    out.isValid = !out.sourcePath.empty();
    return out;
}

ParsedBindingClause ParseBindingMarkupExpression(std::string_view rawExpr) {
    ParsedBindingClause out{};
    std::string_view v = Trim(rawExpr);
    if (v.size() < 2u || v.front() != '{' || v.back() != '}') {
        return out;
    }
    std::string_view inner = Trim(v.substr(1u, v.size() - 2u));
    if (inner == "Binding" || inner == "Bind") {
        out.isValidBinding = true;
        return out;
    }
    if (inner.rfind("Binding ", 0) == 0) {
        inner = Trim(inner.substr(8u));
    } else if (inner.rfind("Bind ", 0) == 0) {
        inner = Trim(inner.substr(5u));
    } else {
        return out;
    }
    out.isValidBinding = true;

    std::size_t pos = 0u;
    bool firstClause = true;
    while (pos < inner.size()) {
        int braceDepth = 0;
        bool inSingleQuote = false;
        std::size_t end = pos;
        while (end < inner.size()) {
            const char ch = inner[end];
            if (ch == '\'') {
                inSingleQuote = !inSingleQuote;
            } else if (!inSingleQuote) {
                if (ch == '{') ++braceDepth;
                else if (ch == '}' && braceDepth > 0) --braceDepth;
                else if (ch == ',' && braceDepth == 0) break;
            }
            ++end;
        }
        const std::string_view clause = Trim(inner.substr(pos, end - pos));
        if (clause.rfind("Path=", 0) == 0) {
            out.pathPart = Trim(clause.substr(5u));
        } else if (clause.rfind("Mode=", 0) == 0) {
            out.hasExplicitMode = true;
            const std::string_view mStr = Trim(clause.substr(5u));
            if (mStr == "TwoWay") out.mode = BindingMode::TwoWay;
            else if (mStr == "OneTime") out.mode = BindingMode::OneTime;
        } else if (clause.rfind("FallbackValue=", 0) == 0) {
            std::string_view fv = Trim(clause.substr(14u));
            if (fv.size() >= 2u && fv.front() == '\'' && fv.back() == '\'') {
                fv = fv.substr(1u, fv.size() - 2u);
            }
            out.fallbackValue = fv;
        } else if (clause.rfind("TargetNullValue=", 0) == 0) {
            std::string_view tnv = Trim(clause.substr(16u));
            if (tnv.size() >= 2u && tnv.front() == '\'' && tnv.back() == '\'') {
                tnv = tnv.substr(1u, tnv.size() - 2u);
            }
            out.hasTargetNullValue = true;
            out.targetNullValue = tnv;
        } else if (clause.rfind("ElementName=", 0) == 0) {
            out.elementName = Trim(clause.substr(12u));
        } else if (clause.rfind("Source=", 0) == 0) {
            const std::string_view srcExpr = Trim(clause.substr(7u));
            const std::string_view srcKey = ExtractMarkupResourceKey(srcExpr);
            if (!srcKey.empty()) {
                out.staticResourceSourceKey = std::string(srcKey);
            }
        } else if (clause.rfind("Converter=", 0) == 0) {
            const std::string_view convExpr = Trim(clause.substr(10u));
            const std::string_view convKey = ExtractMarkupResourceKey(convExpr);
            const std::string_view effKey = !convKey.empty() ? convKey : convExpr;
            out.converter = ParseValueConverterKind(effKey);
            out.converterKey = std::string(effKey);
        } else if (clause.rfind("ConverterParameter=", 0) == 0) {
            out.converterParameter = NormalizeXamlFormatOrExpr(clause.substr(19u));
        } else if (clause.rfind("StringFormat=", 0) == 0) {
            out.stringFormat = NormalizeXamlFormatOrExpr(clause.substr(13u));
        } else if (clause.rfind("RelativeSource=", 0) == 0) {
            std::string_view rsExpr = Trim(clause.substr(15u));
            if (rsExpr.size() > 2u && rsExpr.front() == '{' && rsExpr.back() == '}') {
                rsExpr = Trim(rsExpr.substr(1u, rsExpr.size() - 2u));
            }
            if (rsExpr.find("TemplatedParent") != std::string_view::npos) {
                out.relativeTemplatedParent = true;
            } else if (rsExpr.find("Self") != std::string_view::npos) {                out.relativeSelf = true;
            } else if (const auto ancPos = rsExpr.find("AncestorType="); ancPos != std::string_view::npos) {
                std::string_view ancVal = Trim(rsExpr.substr(ancPos + 13u));
                int bd = 0;
                std::size_t ae = 0u;
                while (ae < ancVal.size()) {
                    if (ancVal[ae] == '{') ++bd;
                    else if (ancVal[ae] == '}' && bd > 0) --bd;
                    else if (ancVal[ae] == ',' && bd == 0) break;
                    ++ae;
                }
                out.relativeAncestorType = ExtractTargetTypeShortName(ancVal.substr(0, ae));
            }
        } else if (firstClause && clause.find('=') == std::string_view::npos) {
            out.pathPart = clause;
        }
        firstClause = false;
        if (end >= inner.size()) break;
        pos = end + 1u;
    }

    // Strip parentheses around attached property paths like "(noesis:DragDrop.IsContentEmpty)"
    if (out.pathPart.size() > 2u && out.pathPart.front() == '(' && out.pathPart.back() == ')') {
        out.pathPart = Trim(out.pathPart.substr(1u, out.pathPart.size() - 2u));
        out.isAttachedPropertyPath = true;
    }
    return out;
}

bool FindMatchingClosingTag(
    std::string_view xml,
    std::size_t innerStartPos,
    std::string_view targetShortTag,
    std::size_t& outInnerEnd,
    std::size_t& outAfterCloseTag) noexcept {
    std::size_t cur = innerStartPos;
    int depth = 1;
    while (cur < xml.size()) {
        const std::size_t lt = xml.find('<', cur);
        if (lt == std::string_view::npos) {
            return false;
        }
        if (xml.substr(lt + 1u, 3u) == "!--") {
            const std::size_t endC = xml.find("-->", lt + 4u);
            if (endC == std::string_view::npos) return false;
            cur = endC + 3u;
            continue;
        }
        if (lt + 1u < xml.size() && xml[lt + 1u] == '?') {
            const std::size_t endP = xml.find("?>", lt + 2u);
            if (endP == std::string_view::npos) return false;
            cur = endP + 2u;
            continue;
        }
        const bool isClose = (lt + 1u < xml.size() && xml[lt + 1u] == '/');
        const std::size_t nameStart = isClose ? (lt + 2u) : (lt + 1u);
        std::size_t nameEnd = nameStart;
        while (nameEnd < xml.size() &&
               !std::isspace(static_cast<unsigned char>(xml[nameEnd])) &&
               xml[nameEnd] != '>' && xml[nameEnd] != '/') {
            ++nameEnd;
        }
        const std::string_view st = StripXmlPrefix(xml.substr(nameStart, nameEnd - nameStart));
        // Advance to end of tag '>' respecting quotes
        std::size_t scan = nameEnd;
        bool selfClose = false;
        while (scan < xml.size()) {
            if (xml[scan] == '"' || xml[scan] == '\'') {
                const char q = xml[scan++];
                while (scan < xml.size() && xml[scan] != q) ++scan;
                if (scan < xml.size()) ++scan;
                continue;
            }
            if (xml[scan] == '>') {
                std::size_t prev = scan;
                while (prev > nameEnd && std::isspace(static_cast<unsigned char>(xml[prev - 1u]))) {
                    --prev;
                }
                if (prev > nameEnd && xml[prev - 1u] == '/') {
                    selfClose = true;
                }
                ++scan;
                break;
            }
            ++scan;
        }
        if (st == targetShortTag) {
            if (isClose) {
                --depth;
                if (depth == 0) {
                    outInnerEnd = lt;
                    outAfterCloseTag = scan;
                    return true;
                }
            } else if (!selfClose) {
                ++depth;
            }
        }
        cur = scan;
    }
    return false;
}

enum class XmlScopeKind : std::uint8_t {
    VisualElement = 0,
    TransparentContainer = 1,
    RunInline = 2,
    ResourceContainer = 3,
    KeyedResourceItem = 4,
    BrushPropertyElement = 5,
    TransformPropertyElement = 6,
    TransformChildNode = 7,
    NonVisualSkip = 8,
    BindingPropertyElement = 9,
    Transform3DPropertyElement = 10,
    EffectPropertyElement = 11,
};

struct XmlScopeEntry {
    XmlScopeKind kind = XmlScopeKind::VisualElement;
    std::string tagName;
    std::string resourceKey;
    std::string inlineStyleKey;
    std::uint16_t targetPropId = PropId_Background;
    bool brushAssigned = false;
    bool isImageBrushScope = false;
    std::uint8_t imageBrushStretch = 1u;
    bool isItemTemplateContainer = false;
    bool isControlTemplateContainer = false;
    bool isContextMenuContainer = false;
    bool isItemsPanelContainer = false;
    bool isListViewViewContainer = false;
    bool isMergedDictSourceElement = false;
    bool hasInlineRichFormatting = false;
    Matrix2D accumulatedTransform = Matrix2D::Identity();
    std::string accumulatedText;
    std::string accumulatedRichText;
    std::string gridViewColumnsSpec;
    std::vector<AxbCompiledBindingChain> inlineRunBindings;
    std::string inlineRunFormatTemplate;
};

[[nodiscard]] std::string ParseItemsPanelInnerXmlToSpec(std::string_view innerXml, bool& outIsVirtualizing) {
    outIsVirtualizing = false;
    std::size_t pos = 0u;
    while (pos < innerXml.size()) {
        const std::size_t lt = innerXml.find('<', pos);
        if (lt == std::string_view::npos) break;
        if (lt + 3u < innerXml.size() && innerXml.substr(lt, 4u) == "<!--") {
            const std::size_t cend = innerXml.find("-->", lt + 4u);
            if (cend == std::string_view::npos) break;
            pos = cend + 3u;
            continue;
        }
        if (lt + 1u < innerXml.size() && (innerXml[lt + 1u] == '/' || innerXml[lt + 1u] == '?' || innerXml[lt + 1u] == '!')) {
            const std::size_t gt = innerXml.find('>', lt + 1u);
            if (gt == std::string_view::npos) break;
            pos = gt + 1u;
            continue;
        }
        std::size_t tEnd = lt + 1u;
        while (tEnd < innerXml.size() &&
               !std::isspace(static_cast<unsigned char>(innerXml[tEnd])) &&
               innerXml[tEnd] != '>' && innerXml[tEnd] != '/') {
            ++tEnd;
        }
        const std::string_view st = StripXmlPrefix(innerXml.substr(lt + 1u, tEnd - (lt + 1u)));
        if (st.find('.') != std::string_view::npos) {
            const std::size_t gt = innerXml.find('>', tEnd);
            if (gt == std::string_view::npos) break;
            pos = gt + 1u;
            continue;
        }
        std::size_t gt = tEnd;
        char qCh = 0;
        while (gt < innerXml.size()) {
            const char c = innerXml[gt];
            if (qCh != 0) {
                if (c == qCh) qCh = 0;
            } else if (c == '"' || c == '\'') {
                qCh = c;
            } else if (c == '>') {
                break;
            }
            ++gt;
        }
        const std::string_view attrSlice = innerXml.substr(tEnd, (gt < innerXml.size() ? gt : innerXml.size()) - tEnd);
        auto readAttr = [&](std::string_view wanted) -> std::string_view {
            std::size_t ap = 0u;
            while (ap < attrSlice.size()) {
                while (ap < attrSlice.size() && std::isspace(static_cast<unsigned char>(attrSlice[ap]))) ++ap;
                if (ap >= attrSlice.size() || attrSlice[ap] == '/' || attrSlice[ap] == '>') break;
                const std::size_t ns = ap;
                while (ap < attrSlice.size() && attrSlice[ap] != '=' &&
                       !std::isspace(static_cast<unsigned char>(attrSlice[ap])) &&
                       attrSlice[ap] != '/' && attrSlice[ap] != '>') ++ap;
                const std::string_view nm = attrSlice.substr(ns, ap - ns);
                while (ap < attrSlice.size() && std::isspace(static_cast<unsigned char>(attrSlice[ap]))) ++ap;
                std::string_view vl{};
                if (ap < attrSlice.size() && attrSlice[ap] == '=') {
                    ++ap;
                    while (ap < attrSlice.size() && std::isspace(static_cast<unsigned char>(attrSlice[ap]))) ++ap;
                    if (ap < attrSlice.size() && (attrSlice[ap] == '"' || attrSlice[ap] == '\'')) {
                        const char qc = attrSlice[ap++];
                        const std::size_t vs = ap;
                        while (ap < attrSlice.size() && attrSlice[ap] != qc) ++ap;
                        vl = attrSlice.substr(vs, ap - vs);
                        if (ap < attrSlice.size()) ++ap;
                    }
                }
                if (nm == wanted) return Trim(vl);
            }
            return {};
        };

        if (st == "VirtualizingStackPanel") {
            outIsVirtualizing = true;
            const std::string_view ori = readAttr("Orientation");
            return std::string("AERO_PANEL:VirtualizingStackPanel:") + (ori.empty() ? "Vertical" : std::string(ori));
        }
        if (st == "UniformGrid") {
            const std::string_view rows = readAttr("Rows");
            const std::string_view cols = readAttr("Columns");
            const std::string_view fc = readAttr("FirstColumn");
            return "AERO_PANEL:UniformGrid:" +
                   std::string(rows.empty() ? "0" : rows) + ":" +
                   std::string(cols.empty() ? "0" : cols) + ":" +
                   std::string(fc.empty() ? "0" : fc);
        }
        if (st == "WrapPanel" || st == "VirtualizingWrapPanel") {
            const std::string_view ori = readAttr("Orientation");
            const std::string_view iw = readAttr("ItemWidth");
            const std::string_view ih = readAttr("ItemHeight");
            return "AERO_PANEL:WrapPanel:" +
                   std::string(ori.empty() ? "Horizontal" : ori) + ":" +
                   std::string(iw.empty() ? "-1" : iw) + ":" +
                   std::string(ih.empty() ? "-1" : ih);
        }
        if (st == "Canvas") {
            return "AERO_PANEL:Canvas";
        }
        if (st == "StackPanel") {
            const std::string_view ori = readAttr("Orientation");
            const std::string_view sp = readAttr("Spacing");
            return "AERO_PANEL:StackPanel:" +
                   std::string(ori.empty() ? "Vertical" : ori) + ":" +
                   std::string(sp.empty() ? "0" : sp);
        }
        pos = (gt < innerXml.size()) ? (gt + 1u) : innerXml.size();
    }
    return {};
}

[[nodiscard]] std::uint8_t ParseImageBrushStretchCode(std::string_view rawStretch) noexcept {
    const std::string_view s = Trim(rawStretch);
    if (EqualsIgnoreCase(s, "None")) return 0u;
    if (EqualsIgnoreCase(s, "Fill")) return 1u;
    if (EqualsIgnoreCase(s, "Uniform")) return 2u;
    if (EqualsIgnoreCase(s, "UniformToFill")) return 3u;
    return 1u; // WPF/Noesis TileBrush.Stretch default is Fill
}

[[nodiscard]] std::string_view StripImageBrushSpecPrefix(std::string_view sv) noexcept {
    if (sv.size() > 16u && sv.rfind("AERO_IMGBRUSH:", 0) == 0 &&
        sv[15] == ':' && sv[14] >= '0' && sv[14] <= '3') {
        return sv.substr(16u);
    }
    return sv;
}

[[nodiscard]] std::string EncodeImageBrushSpec(std::uint8_t stretchCode, std::string_view bareUri) {
    if (stretchCode == 1u) {
        return std::string(bareUri);
    }
    std::string out;
    out.reserve(16u + bareUri.size());
    out.append("AERO_IMGBRUSH:");
    out.push_back(static_cast<char>('0' + (stretchCode <= 3u ? stretchCode : 1u)));
    out.push_back(':');
    out.append(bareUri);
    return out;
}

enum class SetterOwnerKind : std::uint8_t {
    Style = 0,
    Trigger = 1,
    VisualState = 2,
};

enum class StoryboardOwnerKind : std::uint8_t {
    StandaloneResource = 0,
    TriggerEnter = 1,
    TriggerExit = 2,
    VisualState = 3,
    EventTrigger = 4,
    IgnoredEventTrigger = 5,
};

struct ActiveStyleScope {
    AxbStyleDefinition def{};
    std::size_t scopeDepth = 0;
    std::size_t controlTemplateTriggersDepth = 0;
    std::size_t interactionTriggersDepth = 0;
    bool isInlinePropertyElement = false;
};

struct ActiveTriggerScope {
    AxbStyleTrigger trig{};
    std::size_t scopeDepth = 0;
    std::size_t enterActionsDepth = 0;
    std::size_t exitActionsDepth = 0;
    bool isDataTrigger = false;
    bool hasUnsupportedCondition = false;
};

struct ActiveEventTriggerScope {
    AxbEventTrigger et{};
    std::size_t scopeDepth = 0;
    bool recognized = false;
};

struct ActiveVsgScope {
    AxbVisualStateGroup group{};
    std::size_t scopeDepth = 0;
    std::size_t transitionsDepth = 0;
};

struct ActiveVisualStateScope {
    AxbVisualState state{};
    std::size_t scopeDepth = 0;
};

struct ActiveStoryboardScope {
    AxbStoryboard sb{};
    std::string defaultTargetName;
    std::string defaultTargetProperty;
    StoryboardOwnerKind owner = StoryboardOwnerKind::StandaloneResource;
    std::size_t scopeDepth = 0;
};

struct ActiveTimelineScope {
    AxbAnimationTimeline tl{};
    std::size_t scopeDepth = 0;
};

struct ActiveKeyFrameScope {
    AxbKeyFrame kf{};
    std::size_t scopeDepth = 0;
};

struct ActiveSetterScope {
    AxbPropertySetter setter{"", kUnmappedPropId, {}};
    SetterOwnerKind owner = SetterOwnerKind::Style;
    bool hasValue = false;
    std::size_t scopeDepth = 0;
};

struct ActiveMultiBindingScope {
    AxbCompiledMultiBinding mb{};
    std::size_t scopeDepth = 0;
    bool isNestedInParentMulti = false;
};

struct ActiveBindingElemScope {
    AxbCompiledBindingChain chain{};
    std::size_t scopeDepth = 0;
    bool insideMultiBinding = false;
};

struct ActiveInteractionActionScope {
    AxbInteractionAction action{};
    bool targetEventTrigger = false;
    std::size_t scopeDepth = 0;
};

enum class GradientScopeOwner : std::uint8_t {
    PropertyElement = 0,
    KeyedResource = 1,
    StyleSetter = 2,
};

struct ActiveGradientBrushScope {
    GradientBrushDesc desc{};
    GradientScopeOwner owner = GradientScopeOwner::PropertyElement;
    std::size_t scopeDepth = 0;
    std::uint16_t targetPropId = PropId_Background;
    std::string resourceKey;
};

Point ParseXamlPoint2D(std::string_view raw, Point fallback) noexcept {
    const UnifiedValue pv = ParseBuiltinLiteralPropertyValue(PropId_RenderTransformOrigin, raw);
    Float4Value f4{};
    if (pv.tryGetFloat4(f4)) {
        return Point{f4.x, f4.y};
    }
    return fallback;
}

} // namespace

// ============================================================================
// AeroCompiler Single-Pass XAML + RealScript Unified Compiler
// ============================================================================

namespace { void EnsureDefaultRuntimeXamlHookInstalled() noexcept; }

AeroCompiler::AeroCompiler() {
    EnsureDefaultRuntimeXamlHookInstalled();
    EnsureControlTypesRegistered();
}

std::uint16_t AeroCompiler::ResolvePropertyId(std::string_view propertyName) noexcept {
    return ResolveBuiltinPropertyId(propertyName);
}

UnifiedValue AeroCompiler::ParseLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue) {
    return ParseBuiltinLiteralPropertyValue(propId, rawValue);
}

bool AeroCompiler::BindPackageAgainstProgram(
    AxbPackage& package,
    const ProgramImage& program,
    std::string_view rootDataTypeName,
    const ExecutionDomainContext* domains) {
    return package.BindAgainstProgram(program, rootDataTypeName, domains);
}

namespace {

void PopulateSceneIrFromPackage(
    const AxbPackage& pkg,
    FrontendSourceFormat format,
    std::string_view sourcePath,
    AeroSceneGraphIR& outIr) {
    outIr = AeroSceneGraphIR{};
    outIr.sourceFormat = format;
    outIr.sourcePath = std::string(sourcePath);
    outIr.className = pkg.className;
    outIr.loweredPackage = pkg;

    std::vector<std::uint32_t> parentStack;
    for (const auto& inst : pkg.instructions) {
        if (inst.opcode == AxbOpcode::BeginElement) {
            IrSceneNode node{};
            node.localElementIdx = static_cast<std::uint32_t>(outIr.nodes.size());
            node.parentElementIdx = parentStack.empty() ? 0xFFFFFFFFu : parentStack.back();
            node.typeName = std::string(pkg.GetString(inst.strIdx0));
            node.name = std::string(pkg.GetString(inst.strIdx1));
            if (!parentStack.empty() && parentStack.back() < outIr.nodes.size()) {
                outIr.nodes[parentStack.back()].childIndices.push_back(node.localElementIdx);
            }
            parentStack.push_back(node.localElementIdx);
            outIr.nodes.push_back(std::move(node));
        } else if (inst.opcode == AxbOpcode::EndElement) {
            if (!parentStack.empty()) {
                parentStack.pop_back();
            }
        } else if (inst.opcode == AxbOpcode::SetStaticProperty && !parentStack.empty()) {
            IrSceneNode& cur = outIr.nodes[parentStack.back()];
            cur.staticProperties.emplace_back(inst.propId, inst.literalValue);
            if (inst.propId == PropId_Width) {
                (void)TryGetFloat(inst.literalValue, cur.width);
            } else if (inst.propId == PropId_Height) {
                (void)TryGetFloat(inst.literalValue, cur.height);
            } else if (inst.propId == PropId_AnimTranslateX || inst.propId == PropId_CanvasLeft) {
                (void)TryGetFloat(inst.literalValue, cur.translateX);
            } else if (inst.propId == PropId_AnimTranslateY || inst.propId == PropId_CanvasTop) {
                (void)TryGetFloat(inst.literalValue, cur.translateY);
            } else if (inst.propId == PropId_AnimScaleX) {
                (void)TryGetFloat(inst.literalValue, cur.scaleX);
            } else if (inst.propId == PropId_AnimScaleY) {
                (void)TryGetFloat(inst.literalValue, cur.scaleY);
            } else if (inst.propId == PropId_AnimRotateAngle) {
                (void)TryGetFloat(inst.literalValue, cur.rotateDeg);
            } else if (inst.propId == PropId_AnimSkewX) {
                (void)TryGetFloat(inst.literalValue, cur.skewXDeg);
            } else if (inst.propId == PropId_AnimSkewY) {
                (void)TryGetFloat(inst.literalValue, cur.skewYDeg);
            } else if (inst.propId == PropId_Opacity) {
                (void)TryGetFloat(inst.literalValue, cur.opacity);
            } else if (inst.propId == PropId_PathData) {
                if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                    cur.svgPathData = std::string(sv->view());
                }
            }
        }
    }

    if (!outIr.nodes.empty()) {
        outIr.designWidth = outIr.nodes[0].width > 0.0f ? outIr.nodes[0].width : 256.0f;
        outIr.designHeight = outIr.nodes[0].height > 0.0f ? outIr.nodes[0].height : 256.0f;
    }

    for (const auto& mp : pkg.mtsdfPaths) {
        IrVectorShapeAsset shape{};
        shape.pathHash = mp.pathHash;
        shape.adaptiveCellSize = mp.cellWidth;
        shape.pxRange = mp.pxRange;
        outIr.vectorShapes.push_back(std::move(shape));
    }

    for (std::size_t ciIdx = 0; ciIdx < pkg.contractInputs.size(); ++ciIdx) {
        const auto& cp = pkg.contractInputs[ciIdx];
        IrContractPort port{};
        port.name = std::string(pkg.GetString(cp.nameStrIdx));
        port.direction = cp.isTrigger ? IrContractPortDirection::OutputTrigger : IrContractPortDirection::Input;
        port.valueType = cp.isTrigger ? IrContractPortValueType::Trigger
                                      : static_cast<IrContractPortValueType>(cp.valueKind);
        port.assignedPropId = (cp.bridgePropId != 0xFFFFu)
            ? cp.bridgePropId
            : static_cast<std::uint16_t>(PropId_FirstContractInput + ciIdx);
        port.smInputIndex = static_cast<std::uint16_t>(ciIdx);
        port.defaultValue = cp.defaultValue;
        (void)TryGetFloat(cp.defaultValue, port.defaultFloat);
        outIr.contractPorts.push_back(std::move(port));
    }
}

void EmitDecomposedSvgMatrixInstructions(const Matrix2D& m, AxbPackage& pkg) {
    const DecomposedTransform2D d = PolarDecomposeMatrix2D(m);
    auto pushFloatProp = [&](std::uint16_t propId, float v) {
        AxbInstruction inst{};
        inst.opcode = AxbOpcode::SetStaticProperty;
        inst.propId = propId;
        inst.literalValue = MakeFloat(v);
        pkg.instructions.push_back(std::move(inst));
    };
    if (std::fabs(d.translateX) > 1e-4f) pushFloatProp(PropId_AnimTranslateX, d.translateX);
    if (std::fabs(d.translateY) > 1e-4f) pushFloatProp(PropId_AnimTranslateY, d.translateY);
    if (std::fabs(d.rotateDeg) > 1e-4f) pushFloatProp(PropId_AnimRotateAngle, d.rotateDeg);
    if (std::fabs(d.scaleX - 1.0f) > 1e-4f) pushFloatProp(PropId_AnimScaleX, d.scaleX);
    if (std::fabs(d.scaleY - 1.0f) > 1e-4f) pushFloatProp(PropId_AnimScaleY, d.scaleY);
    if (std::fabs(d.skewXDeg) > 1e-4f) pushFloatProp(PropId_AnimSkewX, d.skewXDeg);
    if (std::fabs(d.skewYDeg) > 1e-4f) pushFloatProp(PropId_AnimSkewY, d.skewYDeg);
}

Matrix2D ParseSvgTransformAttr(std::string_view raw) noexcept {
    raw = Trim(raw);
    if (raw.rfind("matrix(", 0) == 0 && raw.back() == ')') {
        return ParseXamlMatrix2DLiteral(raw.substr(7, raw.size() - 8));
    }
    if (raw.rfind("translate(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(10, raw.size() - 11);
        const std::size_t sep = inner.find_first_of(", ");
        const float tx = ParseFloat32(sep == std::string_view::npos ? inner : inner.substr(0, sep), 0.0f);
        const float ty = sep == std::string_view::npos ? 0.0f : ParseFloat32(Trim(inner.substr(sep + 1)), 0.0f);
        return Matrix2D::Translation(tx, ty);
    }
    if (raw.rfind("scale(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(6, raw.size() - 7);
        const std::size_t sep = inner.find_first_of(", ");
        const float sx = ParseFloat32(sep == std::string_view::npos ? inner : inner.substr(0, sep), 1.0f);
        const float sy = sep == std::string_view::npos ? sx : ParseFloat32(Trim(inner.substr(sep + 1)), sx);
        return Matrix2D::Scaling(sx, sy);
    }
    if (raw.rfind("rotate(", 0) == 0 && raw.back() == ')') {
        const auto inner = raw.substr(7, raw.size() - 8);
        return Matrix2D::Rotation(ParseFloat32(inner, 0.0f));
    }
    return Matrix2D::Identity();
}

// Simple JSON token helpers for Lottie & Rive structured JSON/RIV descriptions
std::string_view FindJsonFieldValue(std::string_view obj, std::string_view key) noexcept {
    const std::string pattern = "\"" + std::string(key) + "\"";
    int depth = 0;
    for (std::size_t i = 0; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '\"') {
            std::size_t endQuote = i + 1;
            while (endQuote < obj.size() && !(obj[endQuote] == '\"' && obj[endQuote - 1] != '\\')) {
                ++endQuote;
            }
            if (endQuote >= obj.size()) break;
            if (depth <= 1 && (endQuote - i + 1) == pattern.size() &&
                obj.substr(i, pattern.size()) == pattern) {
                std::size_t pos = endQuote + 1;
                while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) {
                    ++pos;
                }
                if (pos < obj.size() && obj[pos] == ':') {
                    ++pos;
                    while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) {
                        ++pos;
                    }
                    if (pos >= obj.size()) return {};
                    const char startChar = obj[pos];
                    if (startChar == '\"') {
                        std::size_t valEnd = pos + 1;
                        while (valEnd < obj.size() && !(obj[valEnd] == '\"' && obj[valEnd - 1] != '\\')) {
                            ++valEnd;
                        }
                        return obj.substr(pos + 1, valEnd - (pos + 1));
                    }
                    if (startChar == '{' || startChar == '[') {
                        const char closeChar = (startChar == '{') ? '}' : ']';
                        int subDepth = 0;
                        bool subStr = false;
                        for (std::size_t j = pos; j < obj.size(); ++j) {
                            if (obj[j] == '\"' && (j == pos || obj[j - 1] != '\\')) {
                                subStr = !subStr;
                                continue;
                            }
                            if (subStr) continue;
                            if (obj[j] == startChar) ++subDepth;
                            else if (obj[j] == closeChar) {
                                --subDepth;
                                if (subDepth == 0) return obj.substr(pos, j - pos + 1);
                            }
                        }
                        return {};
                    }
                    std::size_t endVal = pos;
                    while (endVal < obj.size() && obj[endVal] != ',' && obj[endVal] != '}' &&
                           obj[endVal] != ']' && !std::isspace(static_cast<unsigned char>(obj[endVal]))) {
                        ++endVal;
                    }
                    return obj.substr(pos, endVal - pos);
                }
            }
            i = endQuote;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        }
    }
    return {};
}

std::vector<std::string_view> SplitJsonTopLevelArray(std::string_view arr) {
    std::vector<std::string_view> items;
    arr = Trim(arr);
    if (arr.size() < 2 || arr.front() != '[' || arr.back() != ']') return items;
    arr = Trim(arr.substr(1, arr.size() - 2));
    int depth = 0;
    bool inStr = false;
    std::size_t itemStart = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const char c = arr[i];
        if (c == '\"' && (i == 0 || arr[i - 1] != '\\')) inStr = !inStr;
        if (inStr) continue;
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            const auto piece = Trim(arr.substr(itemStart, i - itemStart));
            if (!piece.empty()) items.push_back(piece);
            itemStart = i + 1;
        }
    }
    const auto tail = Trim(arr.substr(itemStart));
    if (!tail.empty()) items.push_back(tail);
    return items;
}

float ExtractFirstFloatFromJsonToken(std::string_view tok, float fallback) noexcept {
    tok = Trim(tok);
    if (tok.empty()) return fallback;
    if (tok.front() == '[') {
        auto elems = SplitJsonTopLevelArray(tok);
        if (!elems.empty()) return ParseFloat32(elems[0], fallback);
        return fallback;
    }
    return ParseFloat32(tok, fallback);
}

bool IsSvgAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4 && EqualsIgnoreCase(path.substr(path.size() - 4), ".svg")) {
        return true;
    }
    const std::string_view trimmed = Trim(content);
    return trimmed.rfind("<svg", 0) == 0;
}

bool IsLottieAssetInput(std::string_view path, std::string_view content) noexcept {
    if ((path.size() >= 5 && EqualsIgnoreCase(path.substr(path.size() - 5), ".json")) ||
        (path.size() >= 7 && EqualsIgnoreCase(path.substr(path.size() - 7), ".lottie"))) {
        const std::string_view trimmed = Trim(content);
        if (!trimmed.empty() && trimmed.front() == '{' &&
            trimmed.find("\"layers\"") != std::string_view::npos) {
            return true;
        }
    }
    const std::string_view trimmed = Trim(content);
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"fr\"") != std::string_view::npos &&
           trimmed.find("\"layers\"") != std::string_view::npos;
}

bool IsRiveAssetInput(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 4 && EqualsIgnoreCase(path.substr(path.size() - 4), ".riv")) {
        return true;
    }
    const std::string_view trimmed = Trim(content);
    if (trimmed.size() >= 4 && trimmed.substr(0, 4) == "RIVE") {
        return true;
    }
    return !trimmed.empty() && trimmed.front() == '{' &&
           trimmed.find("\"artboard\"") != std::string_view::npos &&
           trimmed.find("\"stateMachines\"") != std::string_view::npos;
}

} // namespace

bool SvgTokenFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_SVG_EMPTY: Empty SVG input");
        return false;
    }
    const std::string_view src(reinterpret_cast<const char*>(rawBytes), byteLength);
    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";

    float viewW = 256.0f;
    float viewH = 256.0f;
    std::unordered_set<std::uint64_t> bakedHashes;
    bool rootEmitted = false;
    std::size_t openContainers = 0u;

    std::size_t pos = 0;
    while (pos < src.size()) {
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) break;
        pos = lt + 1;
        if (pos >= src.size()) break;
        if (src.substr(pos, 3) == "!--") {
            const std::size_t endC = src.find("-->", pos + 3);
            pos = (endC == std::string_view::npos) ? src.size() : (endC + 3);
            continue;
        }
        if (src[pos] == '?') {
            const std::size_t endD = src.find("?>", pos + 1);
            pos = (endD == std::string_view::npos) ? src.size() : (endD + 2);
            continue;
        }
        if (src[pos] == '/') {
            const std::size_t gt = src.find('>', pos + 1);
            const std::string_view closeTag = StripXmlPrefix(Trim(src.substr(pos + 1, (gt == std::string_view::npos ? src.size() : gt) - (pos + 1))));
            if ((closeTag == "svg" || closeTag == "g") && openContainers > 0u) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
                --openContainers;
            }
            pos = (gt == std::string_view::npos) ? src.size() : (gt + 1);
            continue;
        }

        std::size_t tagEnd = pos;
        while (tagEnd < src.size() && !std::isspace(static_cast<unsigned char>(src[tagEnd])) &&
               src[tagEnd] != '>' && src[tagEnd] != '/') {
            ++tagEnd;
        }
        const std::string_view shortTag = StripXmlPrefix(src.substr(pos, tagEnd - pos));
        pos = tagEnd;

        std::vector<std::pair<std::string_view, std::string_view>> attrs;
        bool selfClosing = false;
        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
            if (pos >= src.size()) break;
            if (src[pos] == '>') { ++pos; break; }
            if (src.substr(pos, 2) == "/>") { selfClosing = true; pos += 2; break; }
            const std::size_t aStart = pos;
            while (pos < src.size() && src[pos] != '=' &&
                   !std::isspace(static_cast<unsigned char>(src[pos])) &&
                   src[pos] != '>' && src[pos] != '/') {
                ++pos;
            }
            const std::string_view aName = src.substr(aStart, pos - aStart);
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
            std::string_view aVal;
            if (pos < src.size() && src[pos] == '=') {
                ++pos;
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
                if (pos < src.size() && (src[pos] == '\"' || src[pos] == '\'')) {
                    const char q = src[pos++];
                    const std::size_t vStart = pos;
                    while (pos < src.size() && src[pos] != q) ++pos;
                    aVal = src.substr(vStart, pos - vStart);
                    if (pos < src.size() && src[pos] == q) ++pos;
                }
            }
            if (!aName.empty()) attrs.emplace_back(aName, aVal);
        }

        if (shortTag == "svg") {
            std::string_view elemId;
            for (const auto& kv : attrs) {
                if (kv.first == "x:Class") pkg.className = std::string(Trim(kv.second));
                else if (kv.first == "id" || kv.first == "x:Name") elemId = Trim(kv.second);
                else if (kv.first == "width") viewW = ParseFloat32(kv.second, viewW);
                else if (kv.first == "height") viewH = ParseFloat32(kv.second, viewH);
                else if (kv.first == "viewBox") {
                    std::string_view vb = Trim(kv.second);
                    float vals[4]{0.0f, 0.0f, viewW, viewH};
                    for (int vi = 0; vi < 4 && !vb.empty(); ++vi) {
                        const std::size_t sep = vb.find_first_of(", ");
                        vals[vi] = ParseFloat32(sep == std::string_view::npos ? vb : vb.substr(0, sep), vals[vi]);
                        if (sep == std::string_view::npos) break;
                        vb = Trim(vb.substr(sep + 1));
                    }
                    if (vals[2] > 0.0f) viewW = vals[2];
                    if (vals[3] > 0.0f) viewH = vals[3];
                }
            }
            AxbInstruction beginRoot{};
            beginRoot.opcode = AxbOpcode::BeginElement;
            beginRoot.strIdx0 = pkg.InternString("Canvas");
            beginRoot.strIdx1 = pkg.InternString(elemId);
            pkg.instructions.push_back(std::move(beginRoot));

            AxbInstruction wInst{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(viewW)};
            AxbInstruction hInst{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(viewH)};
            pkg.instructions.push_back(std::move(wInst));
            pkg.instructions.push_back(std::move(hInst));
            rootEmitted = true;
            if (selfClosing) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
            } else {
                ++openContainers;
            }
            continue;
        }

        if (!rootEmitted) continue;

        if (shortTag == "g") {
            std::string_view gName;
            Matrix2D gMat = Matrix2D::Identity();
            bool hasMat = false;
            float gOpacity = 1.0f;
            for (const auto& kv : attrs) {
                if (kv.first == "id" || kv.first == "x:Name") gName = Trim(kv.second);
                else if (kv.first == "transform") { gMat = ParseSvgTransformAttr(kv.second); hasMat = true; }
                else if (kv.first == "opacity") gOpacity = ParseFloat32(kv.second, 1.0f);
            }
            AxbInstruction beginG{};
            beginG.opcode = AxbOpcode::BeginElement;
            beginG.strIdx0 = pkg.InternString("Canvas");
            beginG.strIdx1 = pkg.InternString(gName);
            pkg.instructions.push_back(std::move(beginG));
            if (!gName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{std::string(gName), "Canvas", 0, PropertySlotHandle::InvalidIndex});
            }
            if (std::fabs(gOpacity - 1.0f) > 1e-4f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(gOpacity)});
            }
            if (hasMat) {
                EmitDecomposedSvgMatrixInstructions(gMat, pkg);
            }
            if (selfClosing) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
            } else {
                ++openContainers;
            }
            continue;
        }

        if (shortTag == "path") {
            std::string_view pName;
            std::string_view dPath;
            std::string_view fillStr = "#FFFFFFFF";
            std::string_view strokeStr;
            float strokeW = 0.0f;
            float opacity = 1.0f;
            float pathW = viewW;
            float pathH = viewH;
            Matrix2D pMat = Matrix2D::Identity();
            bool hasMat = false;

            for (const auto& kv : attrs) {
                if (kv.first == "id" || kv.first == "x:Name") pName = Trim(kv.second);
                else if (kv.first == "d" || kv.first == "Data") dPath = Trim(kv.second);
                else if (kv.first == "fill" || kv.first == "Fill") fillStr = Trim(kv.second);
                else if (kv.first == "stroke" || kv.first == "Stroke") strokeStr = Trim(kv.second);
                else if (kv.first == "stroke-width" || kv.first == "StrokeThickness") strokeW = ParseFloat32(kv.second, 0.0f);
                else if (kv.first == "opacity" || kv.first == "Opacity") opacity = ParseFloat32(kv.second, 1.0f);
                else if (kv.first == "width" || kv.first == "Width") pathW = ParseFloat32(kv.second, viewW);
                else if (kv.first == "height" || kv.first == "Height") pathH = ParseFloat32(kv.second, viewH);
                else if (kv.first == "transform") { pMat = ParseSvgTransformAttr(kv.second); hasMat = true; }
            }

            if (!dPath.empty()) {
                const std::uint64_t h = ComputeSvgPathHash(dPath);
                if (h != 0 && bakedHashes.insert(h).second) {
                    const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                    const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                    pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
                }
            }

            AxbInstruction beginP{};
            beginP.opcode = AxbOpcode::BeginElement;
            beginP.strIdx0 = pkg.InternString("Path");
            beginP.strIdx1 = pkg.InternString(pName);
            pkg.instructions.push_back(std::move(beginP));
            if (!pName.empty()) {
                pkg.namedControls.push_back(AxbNamedControlSlot{std::string(pName), "Path", 0, PropertySlotHandle::InvalidIndex});
            }
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(pathW)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(pathH)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});
            if (!dPath.empty()) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            }
            if (!fillStr.empty() && !EqualsIgnoreCase(fillStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillStr)});
            }
            if (!strokeStr.empty() && !EqualsIgnoreCase(strokeStr, "none")) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_BorderBrush, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_BorderBrush, strokeStr)});
            }
            if (strokeW > 0.0f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_BorderThickness, 0, 0, 0, 0, value::FromThickness(Thickness(strokeW))});
            }
            if (std::fabs(opacity - 1.0f) > 1e-4f) {
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(opacity)});
            }
            if (hasMat) {
                EmitDecomposedSvgMatrixInstructions(pMat, pkg);
            }
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        }
    }

    while (openContainers > 0u) {
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        --openContainers;
    }

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::SvgVectorToken, sourcePath, outIr);
    return !pkg.instructions.empty();
}

bool LottieFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_LOTTIE_EMPTY: Empty Lottie JSON input");
        return false;
    }
    const std::string_view json(reinterpret_cast<const char*>(rawBytes), byteLength);
    const float fps = ParseFloat32(FindJsonFieldValue(json, "fr"), 60.0f);
    const float inFrame = ParseFloat32(FindJsonFieldValue(json, "ip"), 0.0f);
    const float outFrame = ParseFloat32(FindJsonFieldValue(json, "op"), 60.0f);
    const float compW = ParseFloat32(FindJsonFieldValue(json, "w"), 256.0f);
    const float compH = ParseFloat32(FindJsonFieldValue(json, "h"), 256.0f);
    const std::string_view compName = FindJsonFieldValue(json, "nm");
    const std::string_view classAttr = FindJsonFieldValue(json, "x:Class");
    const float durationSec = std::max(0.001f, (outFrame - inFrame) / std::max(1.0f, fps));

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";
    if (!classAttr.empty()) {
        pkg.className = std::string(classAttr);
    }

    // Root Canvas (localElementIdx = 0)
    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(compName), 0, {}});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(compW)});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(compH)});

    AxbMotionClipDesc mainClip{};
    mainClip.nameStrIdx = pkg.InternString(compName.empty() ? "LottieMain" : compName);
    mainClip.durationSec = durationSec;
    mainClip.firstTrackIdx = 0u;
    mainClip.trackCount = 0u;
    mainClip.isLooping = true;

    struct LayerIndMap {
        int lottieInd = -1;
        int parentInd = -1;
        std::uint32_t localElemIdx = 0u;
    };
    std::vector<LayerIndMap> layerMappings;
    std::unordered_set<std::uint64_t> bakedHashes;

    auto parseAnimOrStaticChannel = [&](std::string_view propObj,
                                        std::uint32_t targetElemIdx,
                                        std::uint16_t propId,
                                        float scaleFactor,
                                        float defaultStaticVal,
                                        int componentIdx) -> float {
        if (propObj.empty()) return defaultStaticVal;
        const std::string_view aFlag = FindJsonFieldValue(propObj, "a");
        const std::string_view kVal = FindJsonFieldValue(propObj, "k");
        if (aFlag != "1") {
            if (kVal.front() == '[') {
                const auto comps = SplitJsonTopLevelArray(kVal);
                if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                    return ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], defaultStaticVal) * scaleFactor;
                }
            }
            return ExtractFirstFloatFromJsonToken(kVal, defaultStaticVal) * scaleFactor;
        }

        const auto kfObjs = SplitJsonTopLevelArray(kVal);
        if (kfObjs.empty()) return defaultStaticVal;

        AxbMotionTrackDesc track{};
        track.localElementIdx = static_cast<std::uint16_t>(targetElemIdx);
        track.targetPropId = propId;
        track.channelCount = 1u;
        track.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());

        float firstVal = defaultStaticVal;
        float prevEndVal = defaultStaticVal;
        bool hasPrevEnd = false;
        for (std::size_t ki = 0; ki < kfObjs.size(); ++ki) {
            const std::string_view kfObj = kfObjs[ki];
            const float tFrame = ParseFloat32(FindJsonFieldValue(kfObj, "t"), inFrame);
            const std::string_view sArr = FindJsonFieldValue(kfObj, "s");
            const std::string_view eArr = FindJsonFieldValue(kfObj, "e");
            float val = hasPrevEnd ? prevEndVal : defaultStaticVal;
            if (!sArr.empty()) {
                if (sArr.front() == '[') {
                    const auto comps = SplitJsonTopLevelArray(sArr);
                    if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                        val = ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], val) * scaleFactor;
                    } else if (!comps.empty()) {
                        val = ParseFloat32(comps[0], val) * scaleFactor;
                    }
                } else {
                    val = ParseFloat32(sArr, val) * scaleFactor;
                }
            }
            if (!eArr.empty()) {
                if (eArr.front() == '[') {
                    const auto comps = SplitJsonTopLevelArray(eArr);
                    if (static_cast<std::size_t>(componentIdx) < comps.size()) {
                        prevEndVal = ParseFloat32(comps[static_cast<std::size_t>(componentIdx)], val) * scaleFactor;
                        hasPrevEnd = true;
                    } else if (!comps.empty()) {
                        prevEndVal = ParseFloat32(comps[0], val) * scaleFactor;
                        hasPrevEnd = true;
                    }
                } else {
                    prevEndVal = ParseFloat32(eArr, val) * scaleFactor;
                    hasPrevEnd = true;
                }
            }
            if (ki == 0) firstVal = val;

            AxbFlatKeyframe kf{};
            kf.timeSec = std::max(0.0f, (tFrame - inFrame) / std::max(1.0f, fps));
            kf.value = Float4Value{val, 0.0f, 0.0f, 0.0f};
            const std::string_view oObj = FindJsonFieldValue(kfObj, "o");
            const std::string_view iObj = FindJsonFieldValue(kfObj, "i");
            if (!oObj.empty() && !iObj.empty()) {
                kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                const float bx1 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "x"), 0.25f), 0.0f, 1.0f);
                const float by1 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(oObj, "y"), 0.25f), -4.0f, 4.0f);
                const float bx2 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "x"), 0.75f), 0.0f, 1.0f);
                const float by2 = std::clamp(ExtractFirstFloatFromJsonToken(FindJsonFieldValue(iObj, "y"), 0.75f), -4.0f, 4.0f);
                kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(std::round(bx1 * 255.0f));
                kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(std::round(bx2 * 255.0f));
                kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::round(by1 * 256.0f));
                kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::round(by2 * 256.0f));
            } else {
                kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
            }
            pkg.motionKeyframes.push_back(std::move(kf));
        }

        track.keyframeCount = static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - track.firstKeyframeIdx;
        if (track.keyframeCount > 0u) {
            pkg.motionTracks.push_back(std::move(track));
            ++mainClip.trackCount;
        }
        return firstVal;
    };

    const std::string_view layersArr = FindJsonFieldValue(json, "layers");
    const auto layerObjs = SplitJsonTopLevelArray(layersArr);
    std::uint32_t nextLocalElemIdx = 1u;
    for (const auto& layerObj : layerObjs) {
        const std::string_view layerName = FindJsonFieldValue(layerObj, "nm");
        const int ind = static_cast<int>(ParseFloat32(FindJsonFieldValue(layerObj, "ind"), -1.0f));
        const int parentInd = static_cast<int>(ParseFloat32(FindJsonFieldValue(layerObj, "parent"), -1.0f));
        const std::string_view dPath = FindJsonFieldValue(layerObj, "d");
        const std::string_view fillHex = FindJsonFieldValue(layerObj, "fill");
        const float lw = ParseFloat32(FindJsonFieldValue(layerObj, "w"), compW);
        const float lh = ParseFloat32(FindJsonFieldValue(layerObj, "h"), compH);

        const std::uint32_t elemIdx = nextLocalElemIdx++;
        layerMappings.push_back(LayerIndMap{ind, parentInd, elemIdx});

        const std::string_view elemType = dPath.empty() ? "Border" : "Path";
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::BeginElement, 0, 0, pkg.InternString(elemType), pkg.InternString(layerName), 0, {}});
        if (!layerName.empty()) {
            pkg.namedControls.push_back(AxbNamedControlSlot{
                std::string(layerName), std::string(elemType), 0, PropertySlotHandle::InvalidIndex});
        }
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(lw)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(lh)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});

        if (!dPath.empty()) {
            const std::uint64_t h = ComputeSvgPathHash(dPath);
            if (h != 0 && bakedHashes.insert(h).second) {
                const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            if (!fillHex.empty()) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
            }
        } else if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
        }

        const std::string_view ksObj = FindJsonFieldValue(layerObj, "ks");
        if (!ksObj.empty()) {
            const float op = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "o"), elemIdx, PropId_Opacity, 0.01f, 1.0f, 0);
            const float rot = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "r"), elemIdx, PropId_AnimRotateAngle, 1.0f, 0.0f, 0);
            const float tx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "p"), elemIdx, PropId_AnimTranslateX, 1.0f, 0.0f, 0);
            const float ty = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "p"), elemIdx, PropId_AnimTranslateY, 1.0f, 0.0f, 1);
            const float sx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "s"), elemIdx, PropId_AnimScaleX, 0.01f, 1.0f, 0);
            const float sy = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "s"), elemIdx, PropId_AnimScaleY, 0.01f, 1.0f, 1);
            const float skx = parseAnimOrStaticChannel(FindJsonFieldValue(ksObj, "sk"), elemIdx, PropId_AnimSkewX, 1.0f, 0.0f, 0);

            if (std::fabs(op - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(op)});
            if (std::fabs(rot) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimRotateAngle, 0, 0, 0, 0, MakeFloat(rot)});
            if (std::fabs(tx) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateX, 0, 0, 0, 0, MakeFloat(tx)});
            if (std::fabs(ty) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateY, 0, 0, 0, 0, MakeFloat(ty)});
            if (std::fabs(sx - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleX, 0, 0, 0, 0, MakeFloat(sx)});
            if (std::fabs(sy - 1.0f) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleY, 0, 0, 0, 0, MakeFloat(sy)});
            if (std::fabs(skx) > 1e-4f)
                pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimSkewX, 0, 0, 0, 0, MakeFloat(skx)});
        }

        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
    }

    // Close Root Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    // Resolve Lottie `parent` layer indices into non-tree AxbTransformConstraintDesc records
    for (const auto& lm : layerMappings) {
        if (lm.parentInd >= 0) {
            for (const auto& targetLm : layerMappings) {
                if (targetLm.lottieInd == lm.parentInd && targetLm.localElemIdx != lm.localElemIdx) {
                    AxbTransformConstraintDesc tc{};
                    tc.childLocalElementIdx = static_cast<std::uint16_t>(lm.localElemIdx);
                    tc.parentLocalElementIdx = static_cast<std::uint16_t>(targetLm.localElemIdx);
                    pkg.transformConstraints.push_back(tc);
                    break;
                }
            }
        }
    }

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    // Add MOT6 clip + default "Progress" contract input + playback state machine
    pkg.motionClips.push_back(mainClip);

    AxbContractInputDesc progressIn{};
    progressIn.nameStrIdx = pkg.InternString("Progress");
    progressIn.valueKind = 1u; // Float
    progressIn.isTrigger = 0u;
    progressIn.bridgePropId = PropId_FirstContractInput;
    progressIn.defaultValue = MakeFloat(0.0f);
    pkg.contractInputs.push_back(progressIn);

    AxbSmState playState{};
    playState.nameStrIdx = pkg.InternString("Playing");
    playState.kind = SmStateKind::SingleClip;
    playState.primaryClipIdx = 0u;
    playState.speedMultiplier = 1.0f;
    pkg.smStates.push_back(playState);

    AxbSmState scrubState{};
    scrubState.nameStrIdx = pkg.InternString("Scrub");
    scrubState.kind = SmStateKind::ScrubbedClip;
    scrubState.primaryClipIdx = 0u;
    scrubState.progressInputSlot = 0u;
    scrubState.speedMultiplier = 1.0f;
    pkg.smStates.push_back(scrubState);

    AxbSmLayerDesc layer{};
    layer.nameStrIdx = pkg.InternString("MainLayer");
    layer.blendMode = SmLayerBlendMode::Override;
    layer.layerWeight = 1.0f;
    layer.firstStateIdx = 0u;
    layer.stateCount = 2u;
    layer.entryStateIdx = 0u;
    pkg.smLayers.push_back(layer);

    AxbStateMachineDesc sm{};
    sm.nameStrIdx = pkg.InternString("LottiePlayback");
    sm.firstLayerIdx = 0u;
    sm.layerCount = 1u;
    pkg.stateMachines.push_back(sm);

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::LottieJson, sourcePath, outIr);
    return true;
}

bool RiveFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    if (!rawBytes || byteLength == 0) {
        outDiagnostics.push_back("AERO_RIVE_EMPTY: Empty Rive .riv input");
        return false;
    }
    if (IsRiveBinaryStream(rawBytes, byteLength)) {
        AxbPackage binPkg{};
        if (!DecodeRiveBinaryToAxbPackage(rawBytes, byteLength, binPkg, &outDiagnostics)) {
            return false;
        }
        PopulateSceneIrFromPackage(binPkg, FrontendSourceFormat::RiveBinary, sourcePath, outIr);
        return true;
    }
    std::string_view content(reinterpret_cast<const char*>(rawBytes), byteLength);
    if (content.size() >= 4 && content.substr(0, 4) == "RIVE") {
        content = Trim(content.substr(4));
    }

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";

    const std::string_view classAttr = FindJsonFieldValue(content, "x:Class");
    if (!classAttr.empty()) {
        pkg.className = std::string(classAttr);
    }

    const std::string_view abObj = FindJsonFieldValue(content, "artboard");
    const std::string_view abSource = abObj.empty() ? content : abObj;
    const std::string_view abName = FindJsonFieldValue(abSource, "name");
    const float abW = ParseFloat32(FindJsonFieldValue(abSource, "width"), 320.0f);
    const float abH = ParseFloat32(FindJsonFieldValue(abSource, "height"), 320.0f);

    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(abName), 0, {}});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(abW)});
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(abH)});

    std::unordered_map<std::string, std::uint32_t> elemIndexByName;
    if (!abName.empty()) {
        elemIndexByName.emplace(std::string(abName), 0u);
    }
    std::unordered_set<std::uint64_t> bakedHashes;

    const auto nodeObjs = SplitJsonTopLevelArray(FindJsonFieldValue(abSource, "nodes"));
    std::uint32_t nextElemIdx = 1u;
    for (const auto& nodeObj : nodeObjs) {
        const std::string_view nName = FindJsonFieldValue(nodeObj, "name");
        const std::string_view nType = FindJsonFieldValue(nodeObj, "type");
        const std::string_view dPath = FindJsonFieldValue(nodeObj, "path");
        const std::string_view fillHex = FindJsonFieldValue(nodeObj, "fill");
        const float nw = ParseFloat32(FindJsonFieldValue(nodeObj, "width"), abW);
        const float nh = ParseFloat32(FindJsonFieldValue(nodeObj, "height"), abH);
        const float nx = ParseFloat32(FindJsonFieldValue(nodeObj, "x"), 0.0f);
        const float ny = ParseFloat32(FindJsonFieldValue(nodeObj, "y"), 0.0f);
        const float nRot = ParseFloat32(FindJsonFieldValue(nodeObj, "rotation"), 0.0f);
        const float nsx = ParseFloat32(FindJsonFieldValue(nodeObj, "scaleX"), 1.0f);
        const float nsy = ParseFloat32(FindJsonFieldValue(nodeObj, "scaleY"), 1.0f);
        const float nOp = ParseFloat32(FindJsonFieldValue(nodeObj, "opacity"), 1.0f);

        const std::uint32_t curIdx = nextElemIdx++;
        if (!nName.empty()) {
            elemIndexByName.emplace(std::string(nName), curIdx);
        }

        const std::string_view runtimeType = (!dPath.empty() || EqualsIgnoreCase(nType, "Path")) ? "Path"
                                           : (EqualsIgnoreCase(nType, "TextBlock") ? "TextBlock" : "Border");
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::BeginElement, 0, 0, pkg.InternString(runtimeType), pkg.InternString(nName), 0, {}});
        if (!nName.empty()) {
            pkg.namedControls.push_back(AxbNamedControlSlot{
                std::string(nName), std::string(runtimeType), 0, PropertySlotHandle::InvalidIndex});
        }
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Width, 0, 0, 0, 0, MakeFloat(nw)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Height, 0, 0, 0, 0, MakeFloat(nh)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_HorizontalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_VerticalAlignment, 0, 0, 0, 0, value::FromInt(1)});
        if (std::fabs(nx) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateX, 0, 0, 0, 0, MakeFloat(nx)});
        if (std::fabs(ny) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimTranslateY, 0, 0, 0, 0, MakeFloat(ny)});
        if (std::fabs(nRot) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimRotateAngle, 0, 0, 0, 0, MakeFloat(nRot)});
        if (std::fabs(nsx - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleX, 0, 0, 0, 0, MakeFloat(nsx)});
        if (std::fabs(nsy - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_AnimScaleY, 0, 0, 0, 0, MakeFloat(nsy)});
        if (std::fabs(nOp - 1.0f) > 1e-4f)
            pkg.instructions.push_back(AxbInstruction{AxbOpcode::SetStaticProperty, PropId_Opacity, 0, 0, 0, 0, MakeFloat(nOp)});

        if (!dPath.empty()) {
            const std::uint64_t h = ComputeSvgPathHash(dPath);
            if (h != 0 && bakedHashes.insert(h).second) {
                const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(dPath);
                const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
                pkg.mtsdfPaths.push_back(BakeSvgPathToMtsdfEntry(dPath, adaptiveDim, 4.0f, slotIdx, 256u));
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_PathData, 0, 0, 0, 0, value::FromString(std::string(dPath))});
            if (!fillHex.empty()) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
            }
        } else if (!fillHex.empty()) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Background, 0, 0, 0, 0, ParseBuiltinLiteralPropertyValue(PropId_Background, fillHex)});
        }

        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
    }

    // Close Artboard Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    if (!pkg.mtsdfPaths.empty()) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    // Parse non-tree bone/transform constraints
    const auto constraintObjs = SplitJsonTopLevelArray(FindJsonFieldValue(abSource, "constraints"));
    for (const auto& cObj : constraintObjs) {
        const std::string constrainedName(FindJsonFieldValue(cObj, "constrained"));
        const std::string targetName(FindJsonFieldValue(cObj, "target"));
        const auto itC = elemIndexByName.find(constrainedName);
        const auto itT = elemIndexByName.find(targetName);
        if (itC != elemIndexByName.end() && itT != elemIndexByName.end()) {
            AxbTransformConstraintDesc tc{};
            tc.childLocalElementIdx = static_cast<std::uint16_t>(itC->second);
            tc.parentLocalElementIdx = static_cast<std::uint16_t>(itT->second);
            pkg.transformConstraints.push_back(tc);
        }
    }

    // Parse clips
    std::unordered_map<std::string, std::uint32_t> clipIndexByName;
    const auto clipObjs = SplitJsonTopLevelArray(FindJsonFieldValue(content, "clips"));
    for (const auto& clObj : clipObjs) {
        const std::string_view clName = FindJsonFieldValue(clObj, "name");
        AxbMotionClipDesc clip{};
        clip.nameStrIdx = pkg.InternString(clName);
        clip.durationSec = std::max(0.001f, ParseFloat32(FindJsonFieldValue(clObj, "duration"), 1.0f));
        clip.isLooping = EqualsIgnoreCase(FindJsonFieldValue(clObj, "loop"), "true");
        clip.firstTrackIdx = static_cast<std::uint32_t>(pkg.motionTracks.size());
        clip.firstEventIdx = static_cast<std::uint32_t>(pkg.motionEvents.size());

        const auto trObjs = SplitJsonTopLevelArray(FindJsonFieldValue(clObj, "tracks"));
        for (const auto& trObj : trObjs) {
            const std::string targetName(FindJsonFieldValue(trObj, "target"));
            const std::string_view propName = FindJsonFieldValue(trObj, "property");
            const auto itElem = elemIndexByName.find(targetName);
            const std::uint32_t targetIdx = (itElem != elemIndexByName.end()) ? itElem->second : 0u;
            const std::uint16_t propId = ResolveBuiltinPropertyId(propName);
            if (propId == kUnmappedPropId) continue;

            AxbMotionTrackDesc tr{};
            tr.localElementIdx = static_cast<std::uint16_t>(targetIdx);
            tr.targetPropId = propId;
            tr.channelCount = 1u;
            tr.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());
            const auto kfObjs = SplitJsonTopLevelArray(FindJsonFieldValue(trObj, "keyframes"));
            for (const auto& kfObj : kfObjs) {
                AxbFlatKeyframe kf{};
                kf.timeSec = ParseFloat32(FindJsonFieldValue(kfObj, "t"), 0.0f);
                const std::string_view vRaw = FindJsonFieldValue(kfObj, "v");
                const UnifiedValue parsedV = ParseBuiltinLiteralPropertyValue(propId, vRaw);
                if (! parsedV.tryGetFloat4(kf.value)) {
                    float sf = 0.0f;
                    (void)TryGetFloat(parsedV, sf);
                    kf.value = Float4Value{sf, 0.0f, 0.0f, 0.0f};
                } else {
                    tr.channelCount = 4u;
                }
                const std::string_view easeStr = FindJsonFieldValue(kfObj, "easing");
                if (EqualsIgnoreCase(easeStr, "CubicBezier")) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                    const float bx1 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "bx1"), 0.25f), 0.0f, 1.0f);
                    const float by1 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "by1"), 0.1f), -4.0f, 4.0f);
                    const float bx2 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "bx2"), 0.25f), 0.0f, 1.0f);
                    const float by2 = std::clamp(ParseFloat32(FindJsonFieldValue(kfObj, "by2"), 1.0f), -4.0f, 4.0f);
                    kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(std::round(bx1 * 255.0f));
                    kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(std::round(bx2 * 255.0f));
                    kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::round(by1 * 256.0f));
                    kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::round(by2 * 256.0f));
                } else {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
                }
                pkg.motionKeyframes.push_back(std::move(kf));
            }
            tr.keyframeCount = static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - tr.firstKeyframeIdx;
            if (tr.keyframeCount > 0u) {
                pkg.motionTracks.push_back(std::move(tr));
            }
        }
        clip.trackCount = static_cast<std::uint32_t>(pkg.motionTracks.size()) - clip.firstTrackIdx;

        const auto evObjs = SplitJsonTopLevelArray(FindJsonFieldValue(clObj, "events"));
        for (const auto& evObj : evObjs) {
            AxbTimelineEventMarker ev{};
            ev.timeSec = ParseFloat32(FindJsonFieldValue(evObj, "t"), 0.0f);
            ev.eventNameStrIdx = pkg.InternString(FindJsonFieldValue(evObj, "name"));
            pkg.motionEvents.push_back(ev);
        }
        clip.eventCount = static_cast<std::uint32_t>(pkg.motionEvents.size() - clip.firstEventIdx);

        const auto clipIdx = static_cast<std::uint32_t>(pkg.motionClips.size());
        if (!clName.empty()) clipIndexByName.emplace(std::string(clName), clipIdx);
        pkg.motionClips.push_back(clip);
    }

    // Parse StateMachines
    const auto smObjs = SplitJsonTopLevelArray(FindJsonFieldValue(content, "stateMachines"));
    for (const auto& smObj : smObjs) {
        AxbStateMachineDesc sm{};
        sm.nameStrIdx = pkg.InternString(FindJsonFieldValue(smObj, "name"));
        sm.firstLayerIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        sm.firstListenerIdx = static_cast<std::uint32_t>(pkg.smListeners.size());

        std::unordered_map<std::string, std::uint32_t> inputIdxByName;
        const auto inObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "inputs"));
        for (const auto& inObj : inObjs) {
            const std::string_view inName = FindJsonFieldValue(inObj, "name");
            const std::string_view inType = FindJsonFieldValue(inObj, "type");
            const auto slotIdx = static_cast<std::uint32_t>(pkg.contractInputs.size());
            const auto portPropId = static_cast<std::uint16_t>(PropId_FirstContractInput + slotIdx);

            AxbContractInputDesc inDesc{};
            inDesc.nameStrIdx = pkg.InternString(inName);
            inDesc.bridgePropId = portPropId;
            if (EqualsIgnoreCase(inType, "Boolean") || EqualsIgnoreCase(inType, "Bool")) {
                inDesc.valueKind = 0u;
                inDesc.defaultValue = UnifiedValue{EqualsIgnoreCase(FindJsonFieldValue(inObj, "default"), "true")};
            } else if (EqualsIgnoreCase(inType, "Trigger")) {
                inDesc.valueKind = 5u;
                inDesc.isTrigger = 1u;
                inDesc.defaultValue = MakeFloat(0.0f);
            } else {
                inDesc.valueKind = 1u;
                inDesc.defaultValue = MakeFloat(ParseFloat32(FindJsonFieldValue(inObj, "default"), 0.0f));
            }
            pkg.contractInputs.push_back(inDesc);
            if (!inName.empty()) inputIdxByName.emplace(std::string(inName), slotIdx);
        }

        const auto layerObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "layers"));
        for (const auto& lyObj : layerObjs) {
            AxbSmLayerDesc ly{};
            ly.nameStrIdx = pkg.InternString(FindJsonFieldValue(lyObj, "name"));
            ly.blendMode = EqualsIgnoreCase(FindJsonFieldValue(lyObj, "blendMode"), "Additive")
                ? SmLayerBlendMode::Additive
                : SmLayerBlendMode::Override;
            ly.layerWeight = ParseFloat32(FindJsonFieldValue(lyObj, "weight"), 1.0f);
            ly.firstStateIdx = static_cast<std::uint32_t>(pkg.smStates.size());
            ly.firstTransitionIdx = static_cast<std::uint32_t>(pkg.smTransitions.size());

            std::unordered_map<std::string, std::uint16_t> stateIdxByName;
            const auto stObjs = SplitJsonTopLevelArray(FindJsonFieldValue(lyObj, "states"));
            for (const auto& stObj : stObjs) {
                const std::string_view stName = FindJsonFieldValue(stObj, "name");
                const std::string_view stKind = FindJsonFieldValue(stObj, "kind");
                const auto globalStIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                if (!stName.empty()) stateIdxByName.emplace(std::string(stName), globalStIdx);

                AxbSmState st{};
                st.nameStrIdx = pkg.InternString(stName);
                st.speedMultiplier = ParseFloat32(FindJsonFieldValue(stObj, "speed"), 1.0f);

                if (EqualsIgnoreCase(stKind, "Blend1D")) {
                    st.kind = SmStateKind::Blend1D;
                    const std::string paramName(FindJsonFieldValue(stObj, "input"));
                    const auto itIn = inputIdxByName.find(paramName);
                    st.progressInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
                    const auto bItems = SplitJsonTopLevelArray(FindJsonFieldValue(stObj, "thresholds"));
                    if (!bItems.empty()) {
                        st.blendRangeMin = ParseFloat32(FindJsonFieldValue(bItems.front(), "threshold"), 0.0f);
                        const std::string cl0(FindJsonFieldValue(bItems.front(), "clip"));
                        const auto it0 = clipIndexByName.find(cl0);
                        st.primaryClipIdx = (it0 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it0->second) : 0u;
                        if (bItems.size() >= 2u) {
                            st.blendRangeMax = ParseFloat32(FindJsonFieldValue(bItems.back(), "threshold"), 100.0f);
                            const std::string cl1(FindJsonFieldValue(bItems.back(), "clip"));
                            const auto it1 = clipIndexByName.find(cl1);
                            st.secondaryClipIdx = (it1 != clipIndexByName.end()) ? static_cast<std::uint16_t>(it1->second) : st.primaryClipIdx;
                        } else {
                            st.blendRangeMax = st.blendRangeMin + 1.0f;
                            st.secondaryClipIdx = st.primaryClipIdx;
                        }
                    }
                } else if (EqualsIgnoreCase(stKind, "ScrubbedClip")) {
                    st.kind = SmStateKind::ScrubbedClip;
                    const std::string clName(FindJsonFieldValue(stObj, "clip"));
                    const auto itCl = clipIndexByName.find(clName);
                    st.primaryClipIdx = (itCl != clipIndexByName.end()) ? static_cast<std::uint16_t>(itCl->second) : 0u;
                    const std::string paramName(FindJsonFieldValue(stObj, "input"));
                    const auto itIn = inputIdxByName.find(paramName);
                    st.progressInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
                } else {
                    st.kind = SmStateKind::SingleClip;
                    const std::string clName(FindJsonFieldValue(stObj, "clip"));
                    const auto itCl = clipIndexByName.find(clName);
                    st.primaryClipIdx = (itCl != clipIndexByName.end()) ? static_cast<std::uint16_t>(itCl->second) : 0u;
                }
                pkg.smStates.push_back(st);
            }
            ly.stateCount = static_cast<std::uint16_t>(pkg.smStates.size() - ly.firstStateIdx);
            ly.entryStateIdx = static_cast<std::uint16_t>(ly.firstStateIdx);

            // Parse transitions in layer
            const auto trObjs = SplitJsonTopLevelArray(FindJsonFieldValue(lyObj, "transitions"));
            for (const auto& trObj : trObjs) {
                const std::string fromName(FindJsonFieldValue(trObj, "from"));
                const std::string toName(FindJsonFieldValue(trObj, "to"));
                const auto itTo = stateIdxByName.find(toName);
                if (itTo == stateIdxByName.end()) continue;

                AxbSmTransition tr{};
                if (fromName == "*" || fromName.empty()) {
                    tr.fromStateIdx = 0xFFFFu;
                } else {
                    const auto itFrom = stateIdxByName.find(fromName);
                    if (itFrom == stateIdxByName.end()) continue;
                    tr.fromStateIdx = itFrom->second;
                }
                tr.toStateIdx = itTo->second;
                tr.crossfadeDurationSec = ParseFloat32(FindJsonFieldValue(trObj, "duration"), 0.15f);
                tr.exitTimeNormalized01 = ParseFloat32(FindJsonFieldValue(trObj, "exitTime"), 0.0f);
                tr.hasExitTime = (tr.exitTimeNormalized01 > 0.0f);
                tr.firstConditionIdx = static_cast<std::uint32_t>(pkg.smConditions.size());

                const auto condObjs = SplitJsonTopLevelArray(FindJsonFieldValue(trObj, "conditions"));
                for (const auto& cObj : condObjs) {
                    const std::string inName(FindJsonFieldValue(cObj, "input"));
                    const auto itIn = inputIdxByName.find(inName);
                    if (itIn == inputIdxByName.end()) continue;
                    AxbSmCondition cond{};
                    cond.inputSlotIdx = static_cast<std::uint16_t>(itIn->second);
                    const std::string_view opStr = FindJsonFieldValue(cObj, "op");
                    if (opStr == "==" || EqualsIgnoreCase(opStr, "Equals")) cond.op = SmCompareOp::Equal;
                    else if (opStr == "!=" || EqualsIgnoreCase(opStr, "NotEquals")) cond.op = SmCompareOp::NotEqual;
                    else if (opStr == ">" || EqualsIgnoreCase(opStr, "Greater")) cond.op = SmCompareOp::Greater;
                    else if (opStr == ">=" || EqualsIgnoreCase(opStr, "GreaterOrEqual")) cond.op = SmCompareOp::GreaterOrEqual;
                    else if (opStr == "<" || EqualsIgnoreCase(opStr, "Less")) cond.op = SmCompareOp::Less;
                    else if (opStr == "<=" || EqualsIgnoreCase(opStr, "LessOrEqual")) cond.op = SmCompareOp::LessOrEqual;
                    else if (EqualsIgnoreCase(opStr, "TriggerFired")) cond.op = SmCompareOp::TriggerFired;
                    else if (EqualsIgnoreCase(opStr, "IsTrue")) cond.op = SmCompareOp::BoolTrue;
                    else if (EqualsIgnoreCase(opStr, "IsFalse")) cond.op = SmCompareOp::BoolFalse;
                    cond.threshold = ParseFloat32(FindJsonFieldValue(cObj, "value"), 0.0f);
                    pkg.smConditions.push_back(cond);
                }
                tr.conditionCount = static_cast<std::uint16_t>(pkg.smConditions.size() - tr.firstConditionIdx);
                pkg.smTransitions.push_back(tr);
            }
            ly.transitionCount = static_cast<std::uint16_t>(pkg.smTransitions.size() - ly.firstTransitionIdx);

            pkg.smLayers.push_back(ly);
        }
        sm.layerCount = static_cast<std::uint16_t>(pkg.smLayers.size() - sm.firstLayerIdx);

        const auto lstObjs = SplitJsonTopLevelArray(FindJsonFieldValue(smObj, "listeners"));
        for (const auto& lObj : lstObjs) {
            AxbSmPointerListener lst{};
            const std::string targetName(FindJsonFieldValue(lObj, "target"));
            const auto itEl = elemIndexByName.find(targetName);
            lst.hitTargetElementIdx = (itEl != elemIndexByName.end()) ? static_cast<std::uint16_t>(itEl->second) : 0u;
            const std::string_view evStr = FindJsonFieldValue(lObj, "event");
            if (EqualsIgnoreCase(evStr, "PointerExit")) lst.eventKind = SmListenerEventKind::PointerExit;
            else if (EqualsIgnoreCase(evStr, "PointerDown")) lst.eventKind = SmListenerEventKind::PointerDown;
            else if (EqualsIgnoreCase(evStr, "PointerUp") || EqualsIgnoreCase(evStr, "Click")) lst.eventKind = SmListenerEventKind::PointerUpInside;
            else if (EqualsIgnoreCase(evStr, "FocusGained")) lst.eventKind = SmListenerEventKind::FocusGained;
            else if (EqualsIgnoreCase(evStr, "FocusLost")) lst.eventKind = SmListenerEventKind::FocusLost;
            else lst.eventKind = SmListenerEventKind::PointerEnter;

            const std::string inName(FindJsonFieldValue(lObj, "input"));
            const auto itIn = inputIdxByName.find(inName);
            lst.targetInputSlot = (itIn != inputIdxByName.end()) ? static_cast<std::uint16_t>(itIn->second) : 0u;
            const std::string_view actStr = FindJsonFieldValue(lObj, "action");
            if (EqualsIgnoreCase(actStr, "FireTrigger")) lst.actionKind = SmListenerActionKind::FireTrigger;
            else if (EqualsIgnoreCase(actStr, "AddFloat") || EqualsIgnoreCase(actStr, "AddDelta")) lst.actionKind = SmListenerActionKind::AddDelta;
            else lst.actionKind = SmListenerActionKind::SetFloatOrBool;
            lst.actionValue = ParseFloat32(FindJsonFieldValue(lObj, "value"), 1.0f);
            pkg.smListeners.push_back(lst);
        }
        sm.listenerCount = static_cast<std::uint16_t>(pkg.smListeners.size() - sm.firstListenerIdx);

        pkg.stateMachines.push_back(sm);
    }

    PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::RiveBinary, sourcePath, outIr);
    return true;
}

bool XamlFrontendImporter::ImportToSceneIR(
    std::string_view sourcePath,
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AeroSceneGraphIR& outIr,
    std::vector<std::string>& outDiagnostics) const {
    AeroCompiler compiler;
    AeroCompileInput in{};
    in.xamlPath = std::string(sourcePath);
    if (rawBytes && byteLength > 0) {
        in.xamlContent.assign(reinterpret_cast<const char*>(rawBytes), byteLength);
    }
    AeroCompileResult res = compiler.Compile(in);
    outDiagnostics.insert(outDiagnostics.end(), res.diagnostics.begin(), res.diagnostics.end());
    outIr = std::move(res.sceneIr);
    return res.succeeded;
}

AeroCompileResult AeroCompiler::Compile(const AeroCompileInput& input) const {
    std::vector<std::string> activeIncludeStack;
    return CompileInternal(input, activeIncludeStack);
}

AeroCompileResult AeroCompiler::CompileInternal(
    const AeroCompileInput& input,
    std::vector<std::string>& activeIncludeStack) const {
    AeroCompileResult result{};
    AxbPackage& pkg = result.axbPackage;
    pkg.InternString("");

    std::string effectiveXamlPath = input.xamlPath;
    std::string loadedContentStorage;
    if (input.xamlContent.empty() && !input.xamlPath.empty()) {
        std::string resolvedMainPath;
        if (ResolveExternalXamlSource(input, "", input.xamlPath, resolvedMainPath, loadedContentStorage)) {
            effectiveXamlPath = std::move(resolvedMainPath);
        } else if (!ReadFileBytesNoThrow(input.xamlPath, loadedContentStorage)) {
            result.diagnostics.push_back(
                "AERO_XAML_FILE_NOT_FOUND: Cannot read XAML file '" + input.xamlPath + "'");
            result.succeeded = false;
            return result;
        }
    } else {
        loadedContentStorage = StripUtf8Bom(input.xamlContent);
    }

    const std::string selfCycleKey = CanonicalizeCycleKeyNoThrow(effectiveXamlPath);
    struct IncludeStackGuard {
        std::vector<std::string>& stack;
        bool pushed = false;
        ~IncludeStackGuard() {
            if (pushed && !stack.empty()) {
                stack.pop_back();
            }
        }
    } stackGuard{activeIncludeStack, false};
    if (!selfCycleKey.empty()) {
        activeIncludeStack.push_back(selfCycleKey);
        stackGuard.pushed = true;
    }

    std::string rootDataType;
    const std::string_view src = loadedContentStorage;

    // Multi-Frontend Importer Dispatch (.svg, Lottie .json/.lottie, Rive .riv)
    const bool isSvgInput = IsSvgAssetInput(effectiveXamlPath, src);
    const bool isLottieInput = !isSvgInput && IsLottieAssetInput(effectiveXamlPath, src);
    const bool isRiveInput = !isSvgInput && !isLottieInput && IsRiveAssetInput(effectiveXamlPath, src);
    if (isSvgInput || isLottieInput || isRiveInput) {
        bool importedOk = false;
        const auto* rawPtr = reinterpret_cast<const std::uint8_t*>(src.data());
        if (isSvgInput) {
            SvgTokenFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        } else if (isLottieInput) {
            LottieFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        } else {
            RiveFrontendImporter importer;
            importedOk = importer.ImportToSceneIR(effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
        }
        if (!importedOk) {
            result.succeeded = false;
            return result;
        }
        pkg = result.sceneIr.loweredPackage;
    }

    std::size_t pos = (isSvgInput || isLottieInput || isRiveInput) ? src.size() : 0;
    bool seenRoot = false;
    std::unordered_set<std::uint64_t> bakedPathHashes;
    std::unordered_set<std::string> mergedCycleKeysInThisPkg;
    std::uint32_t nextInlineVsmStyleId = 0u;

    auto compileAndMergeExternalDictionary = [&](std::string_view rawDictSource) {
        rawDictSource = Trim(rawDictSource);
        if (rawDictSource.empty()) {
            return;
        }
        std::string resolvedDictPath;
        std::string dictXamlContent;
        if (!ResolveExternalXamlSource(
                input,
                effectiveXamlPath,
                rawDictSource,
                resolvedDictPath,
                dictXamlContent)) {
            result.diagnostics.push_back(
                "AERO_XAML_MERGED_DICT_NOT_FOUND: Cannot resolve MergedDictionary Source '" +
                std::string(rawDictSource) + "' from '" + effectiveXamlPath + "'");
            return;
        }

        const std::string targetCycleKey = CanonicalizeCycleKeyNoThrow(resolvedDictPath);
        if (!targetCycleKey.empty()) {
            for (const auto& activeKey : activeIncludeStack) {
                if (activeKey == targetCycleKey) {
                    result.diagnostics.push_back(
                        "AERO_XAML_MERGED_DICT_CYCLE: Circular ResourceDictionary reference detected for '" +
                        resolvedDictPath + "'");
                    return;
                }
            }
            if (!mergedCycleKeysInThisPkg.insert(targetCycleKey).second) {
                return;
            }
        }

        AeroCompileInput dictInput{};
        dictInput.xamlPath = resolvedDictPath;
        dictInput.xamlContent = std::move(dictXamlContent);
        dictInput.externalXamlResolver = input.externalXamlResolver;
        dictInput.resourceSearchRoot = input.resourceSearchRoot;
        dictInput.resourceSearchPaths = input.resourceSearchPaths;
        dictInput.autoMergeGlobalResources = false;

        AeroCompileResult dictRes = this->CompileInternal(dictInput, activeIncludeStack);
        for (const auto& d : dictRes.diagnostics) {
            result.diagnostics.push_back(d);
        }
        if (dictRes.succeeded) {
            MergeDictionaryPackageInto(pkg, dictRes.axbPackage, resolvedDictPath, bakedPathHashes);
        }
    };

    struct DeferredMtsdfJob {
        std::string svg;
        std::uint16_t adaptiveDim = 32u;
        std::uint16_t slotIdx = 0u;
    };
    std::vector<DeferredMtsdfJob> deferredMtsdfJobs;
    auto flushDeferredMtsdfJobs = [&]() {
        if (deferredMtsdfJobs.empty()) {
            return;
        }
        const std::size_t baseIdx = pkg.mtsdfPaths.size();
        const std::size_t jobCount = deferredMtsdfJobs.size();
        pkg.mtsdfPaths.resize(baseIdx + jobCount);
        if (jobCount >= 4u) {
            const unsigned int hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
            const std::size_t workerCount = std::min<std::size_t>(hw, jobCount);
            std::atomic<std::size_t> nextJob{0u};
            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (std::size_t w = 0u; w < workerCount; ++w) {
                workers.emplace_back([&]() {
                    while (true) {
                        const std::size_t idx = nextJob.fetch_add(1u, std::memory_order_relaxed);
                        if (idx >= jobCount) break;
                        const auto& jb = deferredMtsdfJobs[idx];
                        if (baseIdx + idx < pkg.mtsdfPaths.size()) {
                            pkg.mtsdfPaths[baseIdx + idx] =
                                BakeSvgPathToMtsdfEntry(jb.svg, jb.adaptiveDim, 4.0f, jb.slotIdx, 256u);
                        }
                    }
                });
            }
            for (auto& th : workers) {
                th.join();
            }
        } else {
            for (std::size_t idx = 0u; idx < jobCount; ++idx) {
                const auto& jb = deferredMtsdfJobs[idx];
                if (baseIdx + idx < pkg.mtsdfPaths.size()) {
                    pkg.mtsdfPaths[baseIdx + idx] =
                        BakeSvgPathToMtsdfEntry(jb.svg, jb.adaptiveDim, 4.0f, jb.slotIdx, 256u);
                }
            }
        }
        deferredMtsdfJobs.clear();
    };
    auto ensurePathBakedIntoPackage = [&](std::string_view svgPathData) {
        svgPathData = Trim(svgPathData);
        if (svgPathData.empty()) {
            return;
        }
        const std::uint64_t h = ComputeSvgPathHash(svgPathData);
        if (h == 0 || !bakedPathHashes.insert(h).second) {
            return;
        }
        const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(svgPathData);
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size() + deferredMtsdfJobs.size());
        deferredMtsdfJobs.push_back(DeferredMtsdfJob{std::string(svgPathData), adaptiveDim, slotIdx});
    };

    auto emitStaticRenderTransform = [&](const Matrix2D& m) {
        AxbInstruction linInst{};
        linInst.opcode = AxbOpcode::SetStaticProperty;
        linInst.propId = PropId_RenderTransform;
        linInst.literalValue = UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        pkg.instructions.push_back(std::move(linInst));

        AxbInstruction trInst{};
        trInst.opcode = AxbOpcode::SetStaticProperty;
        trInst.propId = PropId_RenderTransformTranslation;
        trInst.literalValue = UnifiedValue{Float4Value{m.tx, m.ty, 0.0f, 0.0f}};
        pkg.instructions.push_back(std::move(trInst));
    };

    auto emitStaticLayoutTransform = [&](const Matrix2D& m) {
        AxbInstruction ltInst{};
        ltInst.opcode = AxbOpcode::SetStaticProperty;
        ltInst.propId = PropId_LayoutTransform;
        ltInst.literalValue = UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        pkg.instructions.push_back(std::move(ltInst));
    };

    std::vector<XmlScopeEntry> scopeStack;
    std::vector<ActiveStyleScope> styleStack;
    std::vector<ActiveTriggerScope> triggerStack;
    std::vector<ActiveEventTriggerScope> eventTriggerStack;
    std::uint32_t nextInlineDataTplId = 0u;
    std::uint32_t nextInlineControlTplId = 0u;
    std::string dtRootTriggersStyleKey;
    std::string ctRootTriggersStyleKey;
    std::vector<ActiveVsgScope> vsgStack;
    std::vector<ActiveVisualStateScope> visualStateStack;
    std::vector<ActiveStoryboardScope> storyboardStack;
    std::vector<ActiveTimelineScope> timelineStack;
    std::vector<ActiveKeyFrameScope> keyFrameStack;
    std::vector<ActiveSetterScope> setterStack;
    std::vector<ActiveGradientBrushScope> gradientStack;
    std::vector<ActiveMultiBindingScope> multiBindingStack;
    std::vector<ActiveBindingElemScope> bindingElemStack;
    std::vector<ActiveInteractionActionScope> actionStack;
    std::size_t nonVisualDepth = 0;

    auto findEnclosingTransformScope = [&]() noexcept -> XmlScopeEntry* {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::TransformPropertyElement) {
                return &(*it);
            }
            if (it->kind != XmlScopeKind::TransformChildNode) {
                break;
            }
        }
        return nullptr;
    };

    auto findLocalOrGlobalResource = [&](std::string_view key) -> const UnifiedValue* {
        for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
            if (it->first == key) {
                return &it->second;
            }
        }
        return AxbTemplateRegistry::Instance().FindResource(key);
    };

    auto findLocalOrGlobalStoryboard = [&](std::string_view key) -> const AxbStoryboard* {
        for (auto it = pkg.storyboards.rbegin(); it != pkg.storyboards.rend(); ++it) {
            if (it->key == key) {
                return &(*it);
            }
        }
        return AxbTemplateRegistry::Instance().FindStoryboard(key);
    };

    auto resolveConverterResource = [&](ValueConverterKind& ioKind, std::string& ioKey) {
        if (ioKey.empty()) {
            return;
        }
        const std::string mathResKey = "__aero_math_expr__:" + ioKey;
        if (const UnifiedValue* rv = findLocalOrGlobalResource(mathResKey)) {
            if (const auto* sv = std::get_if<StringValue>(rv)) {
                ioKind = ValueConverterKind::MathExpression;
                ioKey = sv->str();
                return;
            }
        }
        if (const UnifiedValue* rv = findLocalOrGlobalResource(ioKey)) {
            if (const auto* sv = std::get_if<StringValue>(rv)) {
                if (sv->view().rfind("__aero_alt_conv__:", 0) == 0) {
                    ioKind = ValueConverterKind::CustomScriptOrHost;
                    ioKey = sv->str();
                } else if (sv->view() == "__aero_builtin_conv__:BooleanToVisibility") {
                    ioKind = ValueConverterKind::BooleanToVisibility;
                } else if (sv->view() == "__aero_builtin_conv__:InverseBooleanToVisibility") {
                    ioKind = ValueConverterKind::InverseBooleanToVisibility;
                } else if (sv->view() == "__aero_builtin_conv__:InverseBoolean") {
                    ioKind = ValueConverterKind::InverseBoolean;
                } else if (sv->view().rfind("__aero_script_conv__:", 0) == 0) {
                    ioKind = ValueConverterKind::CustomScriptOrHost;
                    ioKey = std::string(sv->view().substr(21u));
                }
            }
        }
    };

    auto splitPathIntoHops = [](std::string_view pathPart,
                                std::vector<AxbCompiledHop>& outHops,
                                bool keepSingleAttachedPropertyHop = false) {
        pathPart = Trim(pathPart);
        if (pathPart.empty() || pathPart == ".") {
            return;
        }
        auto pushHopSegment = [&](std::string_view rawSeg) {
            std::string_view seg = Trim(rawSeg);
            if (seg.size() >= 2u && seg.front() == '(' && seg.back() == ')') {
                seg = Trim(seg.substr(1u, seg.size() - 2u));
            }
            if (seg.empty()) {
                return;
            }
            AxbCompiledHop hop{};
            std::string baseName;
            std::int32_t parsedIdx = -1;
            if (SplitIndexedMemberName(seg, baseName, parsedIdx)) {
                hop.memberName = std::move(baseName);
                hop.collectionIndex = parsedIdx;
            } else {
                hop.memberName = std::string(seg);
            }
            outHops.push_back(std::move(hop));
        };
        if (keepSingleAttachedPropertyHop) {
            pushHopSegment(pathPart);
            return;
        }
        std::size_t segStart = 0u;
        int parenDepth = 0;
        for (std::size_t i = 0u; i <= pathPart.size(); ++i) {
            if (i < pathPart.size()) {
                if (pathPart[i] == '(') ++parenDepth;
                else if (pathPart[i] == ')' && parenDepth > 0) --parenDepth;
            }
            if (i == pathPart.size() || (pathPart[i] == '.' && parenDepth == 0)) {
                pushHopSegment(pathPart.substr(segStart, i - segStart));
                segStart = i + 1u;
            }
        }
    };

    auto emitPropertyBindingIfValid = [&](std::uint16_t targetPropId, std::string_view rawMarkupExpr) -> bool {
        if (targetPropId == kUnmappedPropId) {
            return false;
        }
        const std::string_view trimmedExpr = Trim(rawMarkupExpr);
        if (trimmedExpr.size() <= 2 || trimmedExpr.front() != '{' || trimmedExpr.back() != '}') {
            return false;
        }
        const ParsedBindingClause parsedBind = ParseBindingMarkupExpression(trimmedExpr);
        if (!parsedBind.isValidBinding) {
            return false;
        }

        if (parsedBind.relativeTemplatedParent) {
            UnifiedValue parsedFallback{};
            if (!parsedBind.fallbackValue.empty()) {
                parsedFallback = ParseLiteralPropertyValue(targetPropId, parsedBind.fallbackValue);
                AxbInstruction fbInst{};
                fbInst.opcode = AxbOpcode::SetStaticProperty;
                fbInst.propId = targetPropId;
                fbInst.literalValue = parsedFallback;
                pkg.instructions.push_back(std::move(fbInst));
            }
            const std::uint16_t srcPropId = ResolveTemplateBindingSourcePropId(parsedBind.pathPart);
            if (srcPropId == kUnmappedPropId) {
                return !parsedBind.fallbackValue.empty();
            }
            AxbInstruction tbInst{};
            tbInst.opcode = AxbOpcode::BindTemplateParent;
            tbInst.propId = targetPropId;
            tbInst.flags = static_cast<std::uint8_t>(parsedBind.converter);
            tbInst.strIdx0 = static_cast<std::uint32_t>(srcPropId);
            tbInst.strIdx1 = pkg.InternString(parsedBind.pathPart);
            tbInst.literalValue = std::move(parsedFallback);
            pkg.instructions.push_back(std::move(tbInst));
            return true;
        }

        UnifiedValue parsedFallback{};
        if (!parsedBind.fallbackValue.empty()) {
            parsedFallback = ParseLiteralPropertyValue(targetPropId, parsedBind.fallbackValue);
            AxbInstruction fbInst{};
            fbInst.opcode = AxbOpcode::SetStaticProperty;
            fbInst.propId = targetPropId;
            fbInst.literalValue = parsedFallback;
            pkg.instructions.push_back(std::move(fbInst));
        }
        const bool isSelfDcBind =
            (parsedBind.pathPart.empty() || parsedBind.pathPart == ".") &&
            parsedBind.elementName.empty() &&
            parsedBind.relativeAncestorType.empty() &&
            !parsedBind.relativeSelf;
        const bool hasElementOrRelSource =
            !parsedBind.elementName.empty() ||
            !parsedBind.relativeAncestorType.empty() ||
            parsedBind.relativeSelf ||
            isSelfDcBind;
        const std::string_view effPathPart = isSelfDcBind ? std::string_view("DataContext") : parsedBind.pathPart;

        UnifiedValue parsedTargetNull{};
        if (parsedBind.hasTargetNullValue) {
            parsedTargetNull = parsedBind.targetNullValue.empty()
                ? value::FromString("")
                : ParseLiteralPropertyValue(targetPropId, parsedBind.targetNullValue);
        }
        AxbCompiledBindingChain chain{};
        chain.rawPath = std::string(effPathPart);
        chain.targetPropertyId = targetPropId;
        chain.mode = parsedBind.mode;
        chain.fallbackValue = std::move(parsedFallback);
        chain.targetNullValue = std::move(parsedTargetNull);
        chain.elementName = std::string(parsedBind.elementName);
        chain.relativeAncestorType = parsedBind.relativeAncestorType;
        chain.relativeSelf = parsedBind.relativeSelf || isSelfDcBind;
        chain.converter = parsedBind.converter;
        chain.converterKey = parsedBind.converterKey;
        chain.converterParameter = parsedBind.converterParameter;
        chain.stringFormat = parsedBind.stringFormat;
        resolveConverterResource(chain.converter, chain.converterKey);
        const bool keepAttachedSingle = parsedBind.isAttachedPropertyPath ||
            (hasElementOrRelSource &&
             effPathPart.find('.') != std::string_view::npos &&
             !IsInstanceSubHopPropertyPath(effPathPart) && ResolveMappedSetterPropertyId(effPathPart) != kUnmappedPropId);
        splitPathIntoHops(effPathPart, chain.hops, keepAttachedSingle);

        const auto chainIdx = static_cast<std::uint32_t>(pkg.bindingChains.size());
        pkg.bindingChains.push_back(std::move(chain));

        AxbInstruction bindInst{};
        bindInst.opcode = AxbOpcode::BindProperty;
        bindInst.propId = targetPropId;
        bindInst.flags = static_cast<std::uint8_t>(parsedBind.mode);
        bindInst.strIdx0 = chainIdx;
        pkg.instructions.push_back(std::move(bindInst));
        return true;
    };

    auto findEnclosingPropertyTargetId = [&]() noexcept -> std::uint16_t {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::BindingPropertyElement ||
                it->kind == XmlScopeKind::TransparentContainer) {
                if (it->targetPropId != 0 && it->targetPropId != kUnmappedPropId) {
                    return it->targetPropId;
                }
                if (it->targetPropId == PropId_Width && EndsWith(StripXmlPrefix(it->tagName), ".Width")) {
                    return PropId_Width;
                }
            }
            if (it->kind == XmlScopeKind::VisualElement) {
                break;
            }
        }
        return kUnmappedPropId;
    };

    auto commitActionToActiveTrigger = [&](AxbInteractionAction&& act, bool preferEventTrigger) {
        if (preferEventTrigger && !eventTriggerStack.empty()) {
            eventTriggerStack.back().et.actions.push_back(std::move(act));
        } else if (!triggerStack.empty()) {
            if (triggerStack.back().exitActionsDepth != 0u) {
                triggerStack.back().trig.exitActions.push_back(std::move(act));
            } else {
                triggerStack.back().trig.enterActions.push_back(std::move(act));
            }
        } else if (!eventTriggerStack.empty()) {
            eventTriggerStack.back().et.actions.push_back(std::move(act));
        }
    };

    auto emitSingleCompiledChain = [&](AxbCompiledBindingChain&& chain) {
        if (chain.targetPropertyId == kUnmappedPropId) {
            return;
        }
        if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
            AxbInstruction fbInst{};
            fbInst.opcode = AxbOpcode::SetStaticProperty;
            fbInst.propId = chain.targetPropertyId;
            fbInst.literalValue = chain.fallbackValue;
            pkg.instructions.push_back(std::move(fbInst));
        }
        const bool hasElemSrc = !chain.elementName.empty() ||
                                !chain.relativeAncestorType.empty() ||
                                chain.relativeSelf;
        if ((chain.rawPath.empty() || chain.rawPath == ".") && !hasElemSrc) {
            chain.relativeSelf = true;
            chain.rawPath = "DataContext";
            chain.hops.clear();
            AxbCompiledHop dcHop{};
            dcHop.memberName = "DataContext";
            chain.hops.push_back(std::move(dcHop));
        }
        const std::uint16_t propId = chain.targetPropertyId;
        const BindingMode mode = chain.mode;
        const auto chainIdx = static_cast<std::uint32_t>(pkg.bindingChains.size());
        pkg.bindingChains.push_back(std::move(chain));

        AxbInstruction bindInst{};
        bindInst.opcode = AxbOpcode::BindProperty;
        bindInst.propId = propId;
        bindInst.flags = static_cast<std::uint8_t>(mode);
        bindInst.strIdx0 = chainIdx;
        pkg.instructions.push_back(std::move(bindInst));
    };

    auto emitCompiledMultiBinding = [&](AxbCompiledMultiBinding&& mb) {
        if (mb.targetPropertyId == kUnmappedPropId || mb.sources.empty()) {
            return;
        }
        if (!std::holds_alternative<std::monostate>(mb.fallbackValue)) {
            AxbInstruction fbInst{};
            fbInst.opcode = AxbOpcode::SetStaticProperty;
            fbInst.propId = mb.targetPropertyId;
            fbInst.literalValue = mb.fallbackValue;
            pkg.instructions.push_back(std::move(fbInst));
        }
        const std::uint16_t propId = mb.targetPropertyId;
        const BindingMode mode = mb.mode;
        const auto mbIdx = static_cast<std::uint32_t>(pkg.multiBindings.size());
        pkg.multiBindings.push_back(std::move(mb));

        AxbInstruction mbInst{};
        mbInst.opcode = AxbOpcode::BindMultiProperty;
        mbInst.propId = propId;
        mbInst.flags = static_cast<std::uint8_t>(mode);
        mbInst.strIdx0 = mbIdx;
        pkg.instructions.push_back(std::move(mbInst));
    };

    auto parseStyleOrAnimationValue = [&](std::uint16_t propId, std::string_view rawVal, bool& outValid) -> UnifiedValue {
        outValid = false;
        if (propId == kUnmappedPropId) {
            return UnifiedValue{};
        }
        const std::string_view trimmed = Trim(rawVal);
        if (trimmed.size() > 2u && trimmed.front() == '{' && trimmed.back() == '}') {
            const std::string_view innerMarkup = Trim(trimmed.substr(1u, trimmed.size() - 2u));
            if (innerMarkup == "x:Null" || innerMarkup.rfind("x:Null ", 0) == 0) {
                outValid = true;
                return NullObject{};
            }
            if (innerMarkup.rfind("x:Static ", 0) == 0 || innerMarkup.rfind("x:Static	", 0) == 0) {
                std::string_view arg = Trim(innerMarkup.substr(8u));
                if (arg.rfind("Member=", 0) == 0) {
                    arg = Trim(arg.substr(7u));
                }
                if (arg.size() >= 2u && ((arg.front() == '\'' && arg.back() == '\'') || (arg.front() == '"' && arg.back() == '"'))) {
                    arg = arg.substr(1u, arg.size() - 2u);
                }
                const auto dotPos = arg.rfind('.');
                const std::string_view memberName = (dotPos != std::string_view::npos) ? arg.substr(dotPos + 1u) : arg;
                if (!memberName.empty()) {
                    outValid = true;
                    return ParseLiteralPropertyValue(propId, memberName);
                }
            }
            const std::string_view resKey = ExtractMarkupResourceKey(trimmed);
            if (!resKey.empty()) {
                if (propId == PropId_Template || propId == PropId_ItemTemplate ||
                    propId == PropId_ContentTemplate || propId == PropId_HeaderTemplate ||
                    propId == PropId_ContextMenuKey || propId == PropId_Style) {
                    outValid = true;
                    return value::FromString(std::string(resKey));
                }
                if (const UnifiedValue* rv = findLocalOrGlobalResource(resKey)) {
                    outValid = true;
                    if (propId == PropId_PathData) {
                        if (const auto* sv = std::get_if<StringValue>(rv)) {
                            ensurePathBakedIntoPackage(sv->view());
                        }
                    }
                    return *rv;
                }
            }
            return UnifiedValue{};
        }
        outValid = true;
        UnifiedValue parsed = ParseLiteralPropertyValue(propId, trimmed);
        if (propId == PropId_PathData) {
            if (const auto* sv = std::get_if<StringValue>(&parsed)) {
                ensurePathBakedIntoPackage(sv->view());
            }
        }
        return parsed;
    };

    auto commitSetterToOwner = [&](AxbPropertySetter&& s, SetterOwnerKind owner) {
        if (s.propId == kUnmappedPropId ||
            (!s.hasBinding && std::holds_alternative<std::monostate>(s.value))) {
            return;
        }
        if (owner == SetterOwnerKind::VisualState && !visualStateStack.empty()) {
            visualStateStack.back().state.setters.push_back(std::move(s));
        } else if (owner == SetterOwnerKind::Trigger && !triggerStack.empty()) {
            triggerStack.back().trig.setters.push_back(std::move(s));
        } else if (owner == SetterOwnerKind::Style && !styleStack.empty()) {
            if (s.propId == PropId_Template) {
                if (const auto* sv = std::get_if<StringValue>(&s.value)) {
                    styleStack.back().def.controlTemplateKey = sv->str();
                    if (const AxbControlTemplate* foundCt = pkg.FindControlTemplate(sv->view())) {
                        styleStack.back().def.inlineControlTemplate =
                            std::make_shared<AxbControlTemplate>(*foundCt);
                    }
                }
            }
            styleStack.back().def.setters.push_back(std::move(s));
        }
    };

    auto buildDataTriggerCondition = [&](std::string_view bindingAttr,
                                         std::string_view valAttr,
                                         bool hasValAttr,
                                         AxbTriggerCondition& outCond) -> bool {
        if (!hasValAttr) {
            return false;
        }
        const ParsedBindingClause parsed = ParseBindingMarkupExpression(bindingAttr);
        if (!parsed.isValidBinding) {
            return false;
        }
        const std::string_view trimmedVal = Trim(valAttr);
        outCond = AxbTriggerCondition{};
        outCond.converter = parsed.converter;
        outCond.converterKey = parsed.converterKey;
        outCond.converterParameter = parsed.converterParameter;
        resolveConverterResource(outCond.converter, outCond.converterKey);
        if (trimmedVal.size() > 2u && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
            const std::string_view innerV = Trim(trimmedVal.substr(1u, trimmedVal.size() - 2u));
            if (innerV.rfind("x:Null", 0) == 0) {
                outCond.expectNull = true;
            } else {
                const std::string_view resK = ExtractMarkupResourceKey(trimmedVal);
                if (!resK.empty()) {
                    if (const UnifiedValue* rv = findLocalOrGlobalResource(resK)) {
                        outCond.expectedValue = *rv;
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }

        auto splitSubObjectHopsAfterFirstDot = [&](std::string_view pathView, std::string_view& outFirstSeg) {
            outFirstSeg = pathView;
            if (pathView.empty() || pathView.front() == '(') {
                return;
            }
            const std::size_t firstDot = pathView.find('.');
            if (firstDot == std::string_view::npos) {
                return;
            }
            const std::string_view candFirst = pathView.substr(0u, firstDot);
            if (candFirst != "DataContext" && candFirst != "Content" &&
                candFirst != "SelectedItem" && candFirst != "Tag") {
                return;
            }
            outFirstSeg = candFirst;
            std::size_t segPos = firstDot + 1u;
            while (segPos < pathView.size()) {
                const std::size_t dot = pathView.find('.', segPos);
                const std::string_view seg = (dot == std::string_view::npos)
                    ? pathView.substr(segPos)
                    : pathView.substr(segPos, dot - segPos);
                if (!seg.empty()) {
                    AxbCompiledHop hop{};
                    std::string baseMem;
                    std::int32_t collIdx = -1;
                    if (SplitIndexedMemberName(seg, baseMem, collIdx)) {
                        hop.memberName = std::move(baseMem);
                        hop.collectionIndex = collIdx;
                    } else {
                        hop.memberName = std::string(seg);
                    }
                    outCond.compiledHops.push_back(std::move(hop));
                }
                if (dot == std::string_view::npos) break;
                segPos = dot + 1u;
            }
        };

        if (parsed.relativeSelf || parsed.relativeTemplatedParent) {
            outCond.sourceKind = TriggerConditionSource::SelfProperty;
            outCond.bindingPath = std::string(parsed.pathPart);
            std::string_view firstSeg;
            splitSubObjectHopsAfterFirstDot(parsed.pathPart, firstSeg);
            outCond.propId = ResolveMappedTriggerConditionId(firstSeg);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                if (!outCond.compiledHops.empty()) {
                    outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                } else {
                    bool ok = false;
                    outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                    if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                }
            }
            return true;
        }

        if (!parsed.elementName.empty()) {
            outCond.sourceKind = TriggerConditionSource::ElementNameProperty;
            outCond.sourceName = std::string(parsed.elementName);
            outCond.bindingPath = std::string(parsed.pathPart);
            std::string_view firstSeg;
            splitSubObjectHopsAfterFirstDot(parsed.pathPart, firstSeg);
            outCond.propId = ResolveMappedTriggerConditionId(firstSeg);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                if (!outCond.compiledHops.empty() ||
                    outCond.propId == PropId_SelectedItem || outCond.propId == PropId_Text ||
                    outCond.propId == PropId_Content || outCond.propId == PropId_Tag) {
                    outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                } else {
                    bool ok = false;
                    outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                    if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                }
            }
            return true;
        }

        if (!parsed.relativeAncestorType.empty()) {
            outCond.sourceKind = TriggerConditionSource::AncestorProperty;
            outCond.sourceName = parsed.relativeAncestorType;
            outCond.bindingPath = std::string(parsed.pathPart);
            std::string_view firstSeg;
            splitSubObjectHopsAfterFirstDot(parsed.pathPart, firstSeg);
            outCond.propId = ResolveMappedTriggerConditionId(firstSeg);
            if (outCond.propId == kUnmappedPropId) return false;
            if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
                if (!outCond.compiledHops.empty()) {
                    outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                } else {
                    bool ok = false;
                    outCond.expectedValue = parseStyleOrAnimationValue(outCond.propId, trimmedVal, ok);
                    if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                }
            }
            return true;
        }

        // DataContext Binding (single-hop, multi-hop, or empty "{Binding}" to DataContext itself)
        outCond.sourceKind = TriggerConditionSource::DataContextBinding;
        outCond.bindingPath = std::string(parsed.pathPart);
        if (!parsed.pathPart.empty() && parsed.pathPart != ".") {
            std::size_t segPos = 0u;
            while (segPos < parsed.pathPart.size()) {
                const std::size_t dot = parsed.pathPart.find('.', segPos);
                const std::string_view seg = (dot == std::string_view::npos)
                    ? parsed.pathPart.substr(segPos)
                    : parsed.pathPart.substr(segPos, dot - segPos);
                if (!seg.empty()) {
                    AxbCompiledHop hop{};
                    std::string baseMem;
                    std::int32_t collIdx = -1;
                    if (SplitIndexedMemberName(seg, baseMem, collIdx)) {
                        hop.memberName = std::move(baseMem);
                        hop.collectionIndex = collIdx;
                    } else {
                        hop.memberName = std::string(seg);
                    }
                    outCond.compiledHops.push_back(std::move(hop));
                }
                if (dot == std::string_view::npos) break;
                segPos = dot + 1u;
            }
        }
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
        }
        return true;
    };

    const bool isCompilingVisualResourceMock =
        (effectiveXamlPath.rfind("__aero_visual_res__:", 0) == 0);
    auto isNonVisualElementTag = [isCompilingVisualResourceMock](std::string_view shortTag) noexcept -> bool {
        if (isCompilingVisualResourceMock &&
            (shortTag == "GradientStop" || shortTag == "TransformGroup" ||
             shortTag == "TranslateTransform" || shortTag == "RotateTransform" ||
             shortTag == "ScaleTransform" || shortTag == "SkewTransform")) {
            return false;
        }
        return shortTag == "Style" ||
               shortTag == "ControlTemplate" ||
               shortTag == "DataTemplate" ||
               shortTag == "HierarchicalDataTemplate" ||
               shortTag == "ItemsPanelTemplate" ||
               shortTag == "Storyboard" ||
               shortTag == "StaticResource" ||
               shortTag == "BeginStoryboard" ||
               shortTag == "VisualStateGroup" ||
               shortTag == "VisualState" ||
               shortTag == "VisualTransition" ||
               shortTag == "EventTrigger" ||
               shortTag == "Trigger" ||
               shortTag == "DataTrigger" ||
               shortTag == "MultiTrigger" ||
               shortTag == "MultiDataTrigger" ||
               shortTag == "Condition" ||
               shortTag == "Setter" ||
               shortTag == "TransformGroup" ||
               shortTag == "ScaleTransform" ||
               shortTag == "RotateTransform" ||
               shortTag == "TranslateTransform" ||
               shortTag == "SkewTransform" ||
               shortTag == "MatrixTransform" ||
               shortTag == "Matrix" ||
               shortTag == "CompositeTransform3D" ||
               shortTag == "MatrixTransform3D" ||
               shortTag == "PlaneProjection" ||
               shortTag == "BlurEffect" ||
               shortTag == "DropShadowEffect" ||
               shortTag == "PixelateEffect" ||
               shortTag == "BackgroundEffectBehavior" ||
               shortTag == "RiveInput" ||
               IsAnimationTimelineTag(shortTag) ||
               IsAnimationKeyFrameTag(shortTag) ||
               IsEasingFunctionTag(shortTag) ||
               shortTag == "Geometry" ||
               shortTag == "PathGeometry" ||
               shortTag == "StreamGeometry" ||
               shortTag == "RectangleGeometry" ||
               shortTag == "EllipseGeometry" ||
               shortTag == "CombinedGeometry" ||
               shortTag == "ImageBrush" ||
               shortTag == "LinearGradientBrush" ||
               shortTag == "RadialGradientBrush" ||
               shortTag == "SolidColorBrush" ||
               shortTag == "GradientStop" ||
               shortTag == "Binding" ||
               shortTag == "MultiBinding" ||
               shortTag == "MathConverter" ||
               shortTag == "ChangePropertyAction" ||
               shortTag == "ControlStoryboardAction" ||
               shortTag == "InvokeCommandAction" ||
               shortTag == "RemoveElementAction" ||
               shortTag == "NavigateToAction" ||
               shortTag == "NavigateBackAction" ||
               shortTag == "NavDestination" ||
               shortTag == "PlaySoundAction" ||
               shortTag == "ComparisonCondition" ||
               shortTag == "ConditionalExpression" ||
               shortTag == "ConditionBehavior" ||
               shortTag == "PropertyChangedTrigger" ||
               shortTag == "StoryboardCompletedTrigger" ||
               shortTag == "SetFocusAction" ||
               shortTag == "SelectAction" ||
               shortTag == "SelectAllAction";
    };

    while (pos < src.size()) {
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) {
            break;
        }

        // Capture inner text between previous '>' and current '<'
        if (lt > pos && !scopeStack.empty()) {
            const std::string_view rawInner = Trim(src.substr(pos, lt - pos));
            if (!rawInner.empty()) {
                XmlScopeEntry& top = scopeStack.back();
                if (top.kind == XmlScopeKind::KeyedResourceItem ||
                    top.kind == XmlScopeKind::RunInline ||
                    top.isMergedDictSourceElement) {
                    const std::string decInner = DecodeXamlXmlEntities(rawInner);
                    if (!top.accumulatedText.empty() && top.accumulatedText.back() != '\n') {
                        top.accumulatedText.push_back(' ');
                    }
                    top.accumulatedText += decInner;
                    if (top.kind == XmlScopeKind::RunInline) {
                        if (!top.accumulatedRichText.empty() && top.accumulatedRichText.back() != '\n') {
                            top.accumulatedRichText.push_back(' ');
                        }
                        top.accumulatedRichText += decInner;
                    }
                } else if (top.kind == XmlScopeKind::NonVisualSkip &&
                           top.tagName.find('.') != std::string::npos &&
                           scopeStack.size() >= 2u &&
                           scopeStack[scopeStack.size() - 2u].kind == XmlScopeKind::KeyedResourceItem) {
                    XmlScopeEntry& ownerItem = scopeStack[scopeStack.size() - 2u];
                    if (!ownerItem.accumulatedText.empty()) ownerItem.accumulatedText.push_back(' ');
                    ownerItem.accumulatedText += DecodeXamlXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::TransparentContainer &&
                           top.targetPropId != 0 && nonVisualDepth == 0) {
                    if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                    top.accumulatedText += DecodeXamlXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::VisualElement && nonVisualDepth == 0) {
                    const std::string_view st = StripXmlPrefix(top.tagName);
                    if (st == "ItemsControl" || st == "ListBox" || st == "ListView" || st == "ComboBox") {
                        const std::string decItemText = DecodeXamlXmlEntities(rawInner);
                        if (!decItemText.empty()) {
                            AxbInstruction bInst{};
                            bInst.opcode = AxbOpcode::BeginElement;
                            bInst.strIdx0 = pkg.InternString("TextBlock");
                            bInst.strIdx1 = pkg.InternString("");
                            pkg.instructions.push_back(std::move(bInst));
                            AxbInstruction tInst{};
                            tInst.opcode = AxbOpcode::SetStaticProperty;
                            tInst.propId = PropId_Text;
                            tInst.literalValue = value::FromString(decItemText);
                            pkg.instructions.push_back(std::move(tInst));
                            AxbInstruction eInst{};
                            eInst.opcode = AxbOpcode::EndElement;
                            pkg.instructions.push_back(std::move(eInst));
                        }
                    } else if (st == "TextBlock" || st == "Label" || st == "Button" ||
                        st == "RepeatButton" || st == "ToggleButton" ||
                        st == "CheckBox" || st == "RadioButton" || st == "Hyperlink" ||
                        st == "ComboBoxItem" || st == "ListBoxItem" ||
                        st == "Double" || st == "Single" || st == "Int32" ||
                        st == "Int64" || st == "Boolean" || st == "String") {
                        const std::string decInner = DecodeXamlXmlEntities(rawInner);
                        if (!top.accumulatedText.empty() && top.accumulatedText.back() != '\n') {
                            top.accumulatedText.push_back(' ');
                        }
                        top.accumulatedText += decInner;
                        if (st == "TextBlock") {
                            if (!top.accumulatedRichText.empty() && top.accumulatedRichText.back() != '\n') {
                                top.accumulatedRichText.push_back(' ');
                            }
                            top.accumulatedRichText += decInner;
                            if (!top.inlineRunBindings.empty()) {
                                for (char ch : decInner) {
                                    if (ch == '{' || ch == '}') top.inlineRunFormatTemplate.push_back(ch);
                                    top.inlineRunFormatTemplate.push_back(ch);
                                }
                            }
                        }
                    }
                }
            }
        }

        pos = lt + 1;
        if (pos >= src.size()) {
            break;
        }

        // Skip XML comments <!-- ... --> or declarations <? ... ?>
        if (src.substr(pos, 3) == "!--") {
            const std::size_t endComment = src.find("-->", pos + 3);
            pos = (endComment == std::string_view::npos) ? src.size() : (endComment + 3);
            continue;
        }
        if (src[pos] == '?') {
            const std::size_t endDecl = src.find("?>", pos + 1);
            pos = (endDecl == std::string_view::npos) ? src.size() : (endDecl + 2);
            continue;
        }

        // Closing tag </Tag>
        if (src[pos] == '/') {
            const std::size_t gt = src.find('>', pos + 1);
            if (!scopeStack.empty()) {
                const std::size_t closingDepth = scopeStack.size();

                if (!keyFrameStack.empty() && keyFrameStack.back().scopeDepth == closingDepth) {
                    ActiveKeyFrameScope finishedKf = std::move(keyFrameStack.back());
                    keyFrameStack.pop_back();
                    if (!timelineStack.empty() &&
                        (!std::holds_alternative<std::monostate>(finishedKf.kf.value) ||
                         !finishedKf.kf.bindingElementName.empty())) {
                        timelineStack.back().tl.keyFrames.push_back(std::move(finishedKf.kf));
                    }
                }

                if (!timelineStack.empty() && timelineStack.back().scopeDepth == closingDepth) {
                    ActiveTimelineScope finishedTl = std::move(timelineStack.back());
                    timelineStack.pop_back();
                    if (!storyboardStack.empty() && finishedTl.tl.targetPropertyId != kUnmappedPropId) {
                        std::sort(
                            finishedTl.tl.keyFrames.begin(),
                            finishedTl.tl.keyFrames.end(),
                            [](const AxbKeyFrame& a, const AxbKeyFrame& b) {
                                return a.timeSeconds < b.timeSeconds;
                            });
                        storyboardStack.back().sb.timelines.push_back(std::move(finishedTl.tl));
                    }
                }

                if (!storyboardStack.empty() && storyboardStack.back().scopeDepth == closingDepth) {
                    ActiveStoryboardScope finishedSb = std::move(storyboardStack.back());
                    storyboardStack.pop_back();
                    switch (finishedSb.owner) {
                        case StoryboardOwnerKind::StandaloneResource:
                            if (!finishedSb.sb.key.empty() || !finishedSb.sb.timelines.empty()) {
                                pkg.storyboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::TriggerEnter:
                            if (!triggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                triggerStack.back().trig.enterStoryboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::TriggerExit:
                            if (!triggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                triggerStack.back().trig.exitStoryboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::VisualState:
                            if (!visualStateStack.empty()) {
                                visualStateStack.back().state.storyboard = std::move(finishedSb.sb);
                            }
                            break;
                        case StoryboardOwnerKind::EventTrigger:
                            if (!eventTriggerStack.empty() && !finishedSb.sb.timelines.empty()) {
                                eventTriggerStack.back().et.storyboards.push_back(std::move(finishedSb.sb));
                            }
                            break;
                        case StoryboardOwnerKind::IgnoredEventTrigger:
                            break;
                    }
                }

                if (!gradientStack.empty() && gradientStack.back().scopeDepth == closingDepth) {
                    ActiveGradientBrushScope finishedGrad = std::move(gradientStack.back());
                    gradientStack.pop_back();
                    if (finishedGrad.desc.stops.empty()) {
                        finishedGrad.desc.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                        finishedGrad.desc.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                    }
                    float separatedBrushOpacity = 1.0f;
                    bool emitSeparatedBrushOpacity = false;
                    if (finishedGrad.owner == GradientScopeOwner::PropertyElement &&
                        (finishedGrad.targetPropId == PropId_Background ||
                         finishedGrad.targetPropId == PropId_BorderBrush) &&
                        std::fabs(finishedGrad.desc.opacity - 1.0f) > 1e-5f) {
                        separatedBrushOpacity = std::clamp(finishedGrad.desc.opacity, 0.0f, 1.0f);
                        finishedGrad.desc.opacity = 1.0f;
                        emitSeparatedBrushOpacity = true;
                    }
                    BakeGradientBrushRamp(finishedGrad.desc);
                    const std::string spec = SerializeGradientBrushSpec(finishedGrad.desc);
                    (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);

                    if (finishedGrad.owner == GradientScopeOwner::PropertyElement) {
                        AxbInstruction setInst{};
                        setInst.opcode = AxbOpcode::SetStaticProperty;
                        setInst.propId = finishedGrad.targetPropId;
                        setInst.literalValue = value::FromString(spec);
                        pkg.instructions.push_back(std::move(setInst));
                        if (emitSeparatedBrushOpacity) {
                            AxbInstruction opInst{};
                            opInst.opcode = AxbOpcode::SetStaticProperty;
                            opInst.propId = (finishedGrad.targetPropId == PropId_BorderBrush)
                                ? static_cast<std::uint16_t>(PropId_BorderOpacity)
                                : static_cast<std::uint16_t>(PropId_BackgroundOpacity);
                            opInst.literalValue = MakeFloat(separatedBrushOpacity);
                            pkg.instructions.push_back(std::move(opInst));
                        }
                    } else if (finishedGrad.owner == GradientScopeOwner::KeyedResource &&
                               !finishedGrad.resourceKey.empty()) {
                        pkg.resources.emplace_back(
                            finishedGrad.resourceKey,
                            value::FromString(spec));
                    } else if (finishedGrad.owner == GradientScopeOwner::StyleSetter &&
                               !setterStack.empty() && !setterStack.back().hasValue) {
                        setterStack.back().setter.value = value::FromString(spec);
                        setterStack.back().hasValue = true;
                    }
                }

                if (!actionStack.empty() && actionStack.back().scopeDepth == closingDepth) {
                    ActiveInteractionActionScope finishedAct = std::move(actionStack.back());
                    actionStack.pop_back();
                    commitActionToActiveTrigger(std::move(finishedAct.action), finishedAct.targetEventTrigger);
                }

                if (!bindingElemStack.empty() && bindingElemStack.back().scopeDepth == closingDepth) {
                    ActiveBindingElemScope finishedBe = std::move(bindingElemStack.back());
                    bindingElemStack.pop_back();
                    resolveConverterResource(finishedBe.chain.converter, finishedBe.chain.converterKey);
                    if (finishedBe.insideMultiBinding && !multiBindingStack.empty()) {
                        multiBindingStack.back().mb.sources.push_back(std::move(finishedBe.chain));
                    } else {
                        emitSingleCompiledChain(std::move(finishedBe.chain));
                    }
                }

                if (!multiBindingStack.empty() && multiBindingStack.back().scopeDepth == closingDepth) {
                    ActiveMultiBindingScope finishedMb = std::move(multiBindingStack.back());
                    multiBindingStack.pop_back();
                    resolveConverterResource(finishedMb.mb.converter, finishedMb.mb.converterKey);
                    if (finishedMb.isNestedInParentMulti && !multiBindingStack.empty()) {
                        AxbCompiledBindingChain nestedSlot{};
                        nestedSlot.nestedMultiBinding =
                            std::make_shared<AxbCompiledMultiBinding>(std::move(finishedMb.mb));
                        multiBindingStack.back().mb.sources.push_back(std::move(nestedSlot));
                    } else {
                        emitCompiledMultiBinding(std::move(finishedMb.mb));
                    }
                }

                if (!setterStack.empty() && setterStack.back().scopeDepth == closingDepth) {
                    ActiveSetterScope finishedSetter = std::move(setterStack.back());
                    setterStack.pop_back();
                    if (finishedSetter.hasValue) {
                        commitSetterToOwner(std::move(finishedSetter.setter), finishedSetter.owner);
                    }
                }

                if (!visualStateStack.empty() && visualStateStack.back().scopeDepth == closingDepth) {
                    ActiveVisualStateScope finishedVs = std::move(visualStateStack.back());
                    visualStateStack.pop_back();
                    if (!vsgStack.empty() && !finishedVs.state.name.empty()) {
                        vsgStack.back().group.states.push_back(std::move(finishedVs.state));
                    }
                }

                if (!vsgStack.empty() && vsgStack.back().transitionsDepth == closingDepth) {
                    vsgStack.back().transitionsDepth = 0u;
                }
                if (!vsgStack.empty() && vsgStack.back().scopeDepth == closingDepth) {
                    ActiveVsgScope finishedVsg = std::move(vsgStack.back());
                    vsgStack.pop_back();
                    if (!styleStack.empty() && !finishedVsg.group.states.empty()) {
                        styleStack.back().def.visualStateGroups.push_back(std::move(finishedVsg.group));
                    }
                }

                if (!triggerStack.empty()) {
                    if (triggerStack.back().enterActionsDepth == closingDepth) {
                        triggerStack.back().enterActionsDepth = 0u;
                    }
                    if (triggerStack.back().exitActionsDepth == closingDepth) {
                        triggerStack.back().exitActionsDepth = 0u;
                    }
                    if (triggerStack.back().scopeDepth == closingDepth) {
                        ActiveTriggerScope finishedTrig = std::move(triggerStack.back());
                        triggerStack.pop_back();
                        if (!styleStack.empty() &&
                            !finishedTrig.hasUnsupportedCondition &&
                            !finishedTrig.trig.conditions.empty()) {
                            styleStack.back().def.triggers.push_back(std::move(finishedTrig.trig));
                        }
                    }
                }

                if (!eventTriggerStack.empty() && eventTriggerStack.back().scopeDepth == closingDepth) {
                    ActiveEventTriggerScope finishedEt = std::move(eventTriggerStack.back());
                    eventTriggerStack.pop_back();
                    if (!styleStack.empty() && finishedEt.recognized &&
                        (!finishedEt.et.storyboards.empty() || !finishedEt.et.actions.empty())) {
                        styleStack.back().def.eventTriggers.push_back(std::move(finishedEt.et));
                    }
                }

                if (!styleStack.empty()) {
                    if (styleStack.back().controlTemplateTriggersDepth == closingDepth) {
                        styleStack.back().controlTemplateTriggersDepth = 0u;
                    }
                    if (styleStack.back().interactionTriggersDepth == closingDepth) {
                        styleStack.back().interactionTriggersDepth = 0u;
                    }
                    if (styleStack.back().scopeDepth == closingDepth) {
                        ActiveStyleScope finishedStyle = std::move(styleStack.back());
                        styleStack.pop_back();
                        if (!finishedStyle.def.key.empty() || !finishedStyle.def.targetTypeName.empty()) {
                            bool mergedExisting = false;
                            if (!finishedStyle.def.key.empty()) {
                                for (auto& existing : pkg.styles) {
                                    if (existing.key == finishedStyle.def.key) {
                                        if (existing.targetTypeName.empty() && !finishedStyle.def.targetTypeName.empty()) {
                                            existing.targetTypeName = finishedStyle.def.targetTypeName;
                                        }
                                        if (existing.basedOnKey.empty() && !finishedStyle.def.basedOnKey.empty()) {
                                            existing.basedOnKey = finishedStyle.def.basedOnKey;
                                        }
                                        if (!finishedStyle.def.controlTemplateKey.empty()) {
                                            existing.controlTemplateKey = finishedStyle.def.controlTemplateKey;
                                        }
                                        if (finishedStyle.def.inlineControlTemplate) {
                                            existing.inlineControlTemplate = finishedStyle.def.inlineControlTemplate;
                                        }
                                        existing.setters.insert(
                                            existing.setters.end(),
                                            finishedStyle.def.setters.begin(),
                                            finishedStyle.def.setters.end());
                                        existing.triggers.insert(
                                            existing.triggers.end(),
                                            finishedStyle.def.triggers.begin(),
                                            finishedStyle.def.triggers.end());
                                        existing.eventTriggers.insert(
                                            existing.eventTriggers.end(),
                                            finishedStyle.def.eventTriggers.begin(),
                                            finishedStyle.def.eventTriggers.end());
                                        existing.visualStateGroups.insert(
                                            existing.visualStateGroups.end(),
                                            finishedStyle.def.visualStateGroups.begin(),
                                            finishedStyle.def.visualStateGroups.end());
                                        mergedExisting = true;
                                        break;
                                    }
                                }
                            }
                            if (!mergedExisting) {
                                pkg.styles.push_back(std::move(finishedStyle.def));
                            }
                        }
                    }
                }

                XmlScopeEntry closed = std::move(scopeStack.back());
                scopeStack.pop_back();
                if (closed.kind == XmlScopeKind::ResourceContainer ||
                    closed.kind == XmlScopeKind::KeyedResourceItem ||
                    closed.kind == XmlScopeKind::BrushPropertyElement ||
                    closed.kind == XmlScopeKind::TransformPropertyElement ||
                    closed.kind == XmlScopeKind::TransformChildNode ||
                    closed.kind == XmlScopeKind::BindingPropertyElement ||
                    closed.kind == XmlScopeKind::Transform3DPropertyElement ||
                    closed.kind == XmlScopeKind::EffectPropertyElement ||
                    closed.kind == XmlScopeKind::NonVisualSkip) {
                    if (nonVisualDepth > 0) {
                        --nonVisualDepth;
                    }
                }
                if (closed.kind == XmlScopeKind::TransformPropertyElement) {
                    if (closed.targetPropId == PropId_LayoutTransform) {
                        emitStaticLayoutTransform(closed.accumulatedTransform);
                    } else {
                        emitStaticRenderTransform(closed.accumulatedTransform);
                    }
                } else if (closed.isMergedDictSourceElement) {
                    if (!closed.accumulatedText.empty()) {
                        compileAndMergeExternalDictionary(closed.accumulatedText);
                    }
                } else if (closed.kind == XmlScopeKind::KeyedResourceItem &&
                           !closed.resourceKey.empty()) {
                    const std::string_view st = StripXmlPrefix(closed.tagName);
                    if (!closed.brushAssigned) {
                        if ((st == "Color" || st == "SolidColorBrush") && !closed.accumulatedText.empty()) {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                value::FromColor(ParseXamlColorLiteral(closed.accumulatedText)));
                        } else if ((st == "Double" || st == "Single") && !closed.accumulatedText.empty()) {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                MakeFloat(ParseFloat32(closed.accumulatedText, 0.0f)));
                        } else if ((st == "Int32" || st == "Int64" || st == "Int16") &&
                                   !closed.accumulatedText.empty()) {
                            const long long iv = std::strtoll(closed.accumulatedText.c_str(), nullptr, 10);
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                value::FromInt(static_cast<std::int64_t>(iv)));
                        } else if (st == "Boolean" && !closed.accumulatedText.empty()) {
                            const std::string_view bt = Trim(closed.accumulatedText);
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                UnifiedValue{EqualsIgnoreCase(bt, "true") || bt == "1"});
                        } else if (st == "Thickness" && !closed.accumulatedText.empty()) {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                ParseBuiltinLiteralPropertyValue(PropId_Margin, closed.accumulatedText));
                        } else if (st == "CornerRadius" && !closed.accumulatedText.empty()) {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                ParseBuiltinLiteralPropertyValue(PropId_CornerRadius, closed.accumulatedText));
                        } else if ((st == "Geometry" || st == "StreamGeometry" || st == "PathGeometry") &&
                                   !closed.accumulatedText.empty()) {
                            ensurePathBakedIntoPackage(closed.accumulatedText);
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                value::FromString(closed.accumulatedText));
                        } else if (st == "String") {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                value::FromString(closed.accumulatedText));
                        } else if (!closed.accumulatedText.empty()) {
                            UpsertPackageResource(
                                pkg,
                                closed.resourceKey,
                                value::FromString(closed.accumulatedText));
                        }
                    }
                } else if (closed.kind == XmlScopeKind::TransparentContainer &&
                           closed.targetPropId != 0 && !closed.accumulatedText.empty()) {
                    if (closed.targetPropId == PropId_PathData) {
                        ensurePathBakedIntoPackage(closed.accumulatedText);
                    }
                    AxbInstruction propTextInst{};
                    propTextInst.opcode = AxbOpcode::SetStaticProperty;
                    propTextInst.propId = closed.targetPropId;
                    propTextInst.literalValue = value::FromString(std::move(closed.accumulatedText));
                    pkg.instructions.push_back(std::move(propTextInst));
                } else if (closed.tagName == "PathFigure:Closed") {
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::VisualElement) break;
                        if ((it->kind == XmlScopeKind::TransparentContainer && it->targetPropId == PropId_PathData) ||
                            (it->kind == XmlScopeKind::KeyedResourceItem &&
                             (StripXmlPrefix(it->tagName) == "PathGeometry" || StripXmlPrefix(it->tagName) == "Geometry"))) {
                            it->accumulatedText += " Z";
                            break;
                        }
                    }
                } else if (closed.isListViewViewContainer && !closed.gridViewColumnsSpec.empty()) {
                    AxbInstruction gvInst{};
                    gvInst.opcode = AxbOpcode::SetStaticProperty;
                    gvInst.propId = PropId_GridViewColumnsSpec;
                    gvInst.literalValue = value::FromString(std::move(closed.gridViewColumnsSpec));
                    pkg.instructions.push_back(std::move(gvInst));
                } else if (closed.kind == XmlScopeKind::RunInline && !closed.accumulatedText.empty()) {
                    const std::string_view closedSt = StripXmlPrefix(closed.tagName);
                    std::string richChunk = closed.accumulatedRichText.empty()
                        ? closed.accumulatedText
                        : closed.accumulatedRichText;
                    bool makesRich = closed.hasInlineRichFormatting;
                    if (closedSt == "Bold" || closedSt == "RunBold" || closedSt == "RunBoldItalic") {
                        richChunk = "[b]" + richChunk + "[/b]";
                        makesRich = true;
                    }
                    if (closedSt == "Italic" || closedSt == "RunItalic" || closedSt == "RunBoldItalic") {
                        richChunk = "[i]" + richChunk + "[/i]";
                        makesRich = true;
                    }
                    if (closedSt == "Underline") {
                        richChunk = "[u]" + richChunk + "[/u]";
                        makesRich = true;
                    }
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::RunInline || it->kind == XmlScopeKind::VisualElement) {
                            bool insertSpace = false;
                            if (!it->accumulatedText.empty() && !closed.accumulatedText.empty()) {
                                const char prevCh = it->accumulatedText.back();
                                const char nextCh = closed.accumulatedText.front();
                                const bool prevGlue = (prevCh == ' ' || prevCh == '\n' || prevCh == '\t' ||
                                                       prevCh == '"' || prevCh == '\'' || prevCh == '(' || prevCh == '[' || prevCh == '=');
                                const bool nextGlue = (nextCh == ' ' || nextCh == '\n' || nextCh == '\t' ||
                                                       nextCh == '"' || nextCh == '\'' || nextCh == ')' || nextCh == ']' ||
                                                       nextCh == '.' || nextCh == ',' || nextCh == ':' || nextCh == ';');
                                if (!prevGlue && !nextGlue) {
                                    insertSpace = true;
                                }
                            }
                            if (insertSpace) {
                                it->accumulatedText.push_back(' ');
                                if (!it->accumulatedRichText.empty()) {
                                    it->accumulatedRichText.push_back(' ');
                                }
                            }
                            if (it->accumulatedRichText.empty() && !it->accumulatedText.empty()) {
                                it->accumulatedRichText = it->accumulatedText;
                            }
                            if (!it->inlineRunBindings.empty()) {
                                if (insertSpace) it->inlineRunFormatTemplate.push_back(' ');
                                for (char ch : closed.accumulatedText) {
                                    if (ch == '{' || ch == '}') it->inlineRunFormatTemplate.push_back(ch);
                                    it->inlineRunFormatTemplate.push_back(ch);
                                }
                            }
                            it->accumulatedText += closed.accumulatedText;
                            it->accumulatedRichText += richChunk;
                            if (makesRich) {
                                it->hasInlineRichFormatting = true;
                            }
                            break;
                        }
                    }
                } else if (closed.kind == XmlScopeKind::VisualElement) {
                    if (!closed.accumulatedText.empty()) {
                        AxbInstruction textInst{};
                        textInst.opcode = AxbOpcode::SetStaticProperty;
                        textInst.propId = PropId_Text;
                        textInst.literalValue = value::FromString(std::move(closed.accumulatedText));
                        pkg.instructions.push_back(std::move(textInst));
                        if (closed.hasInlineRichFormatting &&
                            !closed.accumulatedRichText.empty() &&
                            StripXmlPrefix(closed.tagName) == "TextBlock") {
                            AxbInstruction richInst{};
                            richInst.opcode = AxbOpcode::SetStaticProperty;
                            richInst.propId = PropId_RichText;
                            richInst.literalValue = value::FromString(std::move(closed.accumulatedRichText));
                            pkg.instructions.push_back(std::move(richInst));
                        }
                    }
                    if (!closed.inlineRunBindings.empty()) {
                        AxbCompiledMultiBinding runMb{};
                        runMb.targetPropertyId = PropId_Text;
                        runMb.mode = BindingMode::OneWay;
                        runMb.stringFormat = std::move(closed.inlineRunFormatTemplate);
                        runMb.sources = std::move(closed.inlineRunBindings);
                        emitCompiledMultiBinding(std::move(runMb));
                    }
                    AxbInstruction endInst{};
                    endInst.opcode = AxbOpcode::EndElement;
                    pkg.instructions.push_back(std::move(endInst));
                } else if (closed.kind == XmlScopeKind::ResourceContainer && scopeStack.empty() &&
                           pkg.rootTypeName == "ResourceDictionary") {
                    AxbInstruction endInst{};
                    endInst.opcode = AxbOpcode::EndElement;
                    pkg.instructions.push_back(std::move(endInst));
                }
            }
            pos = (gt == std::string_view::npos) ? src.size() : (gt + 1);
            continue;
        }

        // Opening tag <Tag attr="val" ...> or <Tag ... />
        std::size_t tagEnd = pos;
        while (tagEnd < src.size() &&
               !std::isspace(static_cast<unsigned char>(src[tagEnd])) &&
               src[tagEnd] != '>' && src[tagEnd] != '/') {
            ++tagEnd;
        }
        const std::string_view tagName = src.substr(pos, tagEnd - pos);
        const std::string_view shortTag = StripXmlPrefix(tagName);
        pos = tagEnd;

        struct ParsedAttr {
            std::string_view name;
            std::string_view value;
        };
        std::vector<ParsedAttr> attrs;
        std::string_view xName;
        std::string_view xKey;
        bool selfClosing = false;
        bool closedTagBracket = false;

        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            if (pos >= src.size()) break;
            if (src[pos] == '>') {
                closedTagBracket = true;
                ++pos;
                break;
            }
            if (src.substr(pos, 2) == "/>") {
                closedTagBracket = true;
                selfClosing = true;
                pos += 2;
                break;
            }

            const std::size_t attrStart = pos;
            while (pos < src.size() && src[pos] != '=' &&
                   !std::isspace(static_cast<unsigned char>(src[pos])) &&
                   src[pos] != '>' && src[pos] != '/') {
                ++pos;
            }
            const std::string_view attrName = src.substr(attrStart, pos - attrStart);
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            std::string_view attrVal;
            if (pos < src.size() && src[pos] == '=') {
                ++pos;
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                    ++pos;
                }
                if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
                    const char quote = src[pos++];
                    const std::size_t valStart = pos;
                    while (pos < src.size() && src[pos] != quote) {
                        ++pos;
                    }
                    attrVal = src.substr(valStart, pos - valStart);
                    if (pos < src.size() && src[pos] == quote) {
                        ++pos;
                    }
                }
            }
            if (!attrName.empty()) {
                if (attrName == "x:Class") {
                    if (!seenRoot && nonVisualDepth == 0) {
                        pkg.className = std::string(attrVal);
                    }
                } else if (attrName == "x:DataType") {
                    if (!seenRoot && nonVisualDepth == 0) {
                        rootDataType = std::string(attrVal);
                    }
                } else if (attrName == "x:Name" || attrName == "Name") {
                    xName = attrVal;
                } else if (attrName == "x:Key") {
                    xKey = attrVal;
                } else if (attrName.rfind("xmlns", 0) == 0 || attrName == "mc:Ignorable") {
                    // Ignore XML namespace declarations
                } else {
                    attrs.push_back(ParsedAttr{attrName, attrVal});
                }
            }
        }
        if (!closedTagBracket) {
            result.diagnostics.push_back(
                "AERO_XAML_SYNTAX_ERROR: Unterminated XML tag '<" + std::string(tagName) + ">'");
            result.succeeded = false;
            return result;
        }

        // Helper to push a NonVisualSkip scope and return the new scopeStack depth
        auto pushNonVisualScope = [&](XmlScopeKind sk = XmlScopeKind::NonVisualSkip) -> std::size_t {
            XmlScopeEntry entry{};
            entry.kind = sk;
            entry.tagName = std::string(tagName);
            scopeStack.push_back(std::move(entry));
            ++nonVisualDepth;
            return scopeStack.size();
        };

        // Inside non-visual scope: handle Style/Trigger/Storyboard/VSM, resource extraction, 2D transforms, or brushes
        if (nonVisualDepth > 0) {
            XmlScopeEntry& parentScope = scopeStack.back();

            // 1. Active <Element.RenderTransform> / <Element.LayoutTransform> subtree
            if (XmlScopeEntry* tfScope = findEnclosingTransformScope()) {
                auto resolveTransformResource = [&](std::string_view rawExpr, std::string_view attrName) -> const UnifiedValue* {
                    const std::string_view v = Trim(rawExpr);
                    if (v.size() <= 2 || v.front() != '{' || v.back() != '}') {
                        return nullptr;
                    }
                    const std::string_view inner = Trim(v.substr(1, v.size() - 2));
                    if (inner.rfind("StaticResource ", 0) == 0 || inner.rfind("DynamicResource ", 0) == 0) {
                        std::string_view keyPart = (inner.rfind("StaticResource ", 0) == 0)
                            ? Trim(inner.substr(15))
                            : Trim(inner.substr(16));
                        if (keyPart.rfind("ResourceKey=", 0) == 0) {
                            keyPart = Trim(keyPart.substr(12));
                        }
                        if (const UnifiedValue* rv = findLocalOrGlobalResource(keyPart)) {
                            return rv;
                        }
                        result.diagnostics.push_back(
                            "AERO_XAML_TRANSFORM_RESOURCE: Unresolved resource '" +
                            std::string(keyPart) + "' on <" + std::string(shortTag) + " " +
                            std::string(attrName) + ">");
                        return nullptr;
                    }
                    if (inner == "Binding" || inner == "Bind" ||
                        inner.rfind("Binding ", 0) == 0 ||
                        inner.rfind("Bind ", 0) == 0 ||
                        inner.rfind("EcsBind ", 0) == 0) {
                        result.diagnostics.push_back(
                            "AERO_XAML_TRANSFORM_BINDING: Dynamic binding on child transform element '<" +
                            std::string(shortTag) + " " + std::string(attrName) +
                            ">' is not supported; bind RenderTransform or LayoutTransform directly on the UIElement.");
                    }
                    return nullptr;
                };

                auto evalFloatAttr = [&](const ParsedAttr& a, float fallback, bool& outResolved) -> float {
                    const std::string_view v = Trim(a.value);
                    if (v.empty()) {
                        outResolved = false;
                        return fallback;
                    }
                    if (v.front() != '{') {
                        outResolved = true;
                        return ParseFloat32(v, fallback);
                    }
                    if (const UnifiedValue* rv = resolveTransformResource(v, a.name)) {
                        float fv = fallback;
                        if (TryGetFloat(*rv, fv)) {
                            outResolved = true;
                            return fv;
                        }
                        if (const auto* sv = std::get_if<StringValue>(rv)) {
                            outResolved = true;
                            return ParseFloat32(sv->view(), fallback);
                        }
                    }
                    outResolved = false;
                    return fallback;
                };

                Matrix2D step = Matrix2D::Identity();
                bool hasStep = false;
                if (shortTag == "ScaleTransform") {
                    float sx = 1.0f, sy = 1.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "ScaleX") sx = evalFloatAttr(a, 1.0f, ok);
                        else if (a.name == "ScaleY") sy = evalFloatAttr(a, 1.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Scaling(sx, sy, cx, cy);
                    hasStep = true;
                } else if (shortTag == "RotateTransform") {
                    float angle = 0.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "Angle") angle = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Rotation(angle, cx, cy);
                    hasStep = true;
                } else if (shortTag == "TranslateTransform") {
                    float dx = 0.0f, dy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "X") dx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "Y") dy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Translation(dx, dy);
                    hasStep = true;
                } else if (shortTag == "SkewTransform") {
                    float ax = 0.0f, ay = 0.0f, cx = 0.0f, cy = 0.0f;
                    for (const auto& a : attrs) {
                        bool ok = false;
                        if (a.name == "AngleX") ax = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "AngleY") ay = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
                        else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
                    }
                    step = Matrix2D::Skew(ax, ay, cx, cy);
                    hasStep = true;
                } else if (shortTag == "MatrixTransform" || shortTag == "Matrix") {
                    bool parsedMatrixStr = false;
                    float m11 = 1.0f, m12 = 0.0f, m21 = 0.0f, m22 = 1.0f, ox = 0.0f, oy = 0.0f;
                    bool anyElemAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Matrix") {
                            const std::string_view mv = Trim(a.value);
                            if (!mv.empty() && mv.front() != '{') {
                                step = ParseXamlMatrix2DLiteral(mv);
                                parsedMatrixStr = true;
                                hasStep = true;
                                break;
                            }
                            if (const UnifiedValue* rv = resolveTransformResource(mv, a.name)) {
                                if (const auto* sv = std::get_if<StringValue>(rv)) {
                                    step = ParseXamlMatrix2DLiteral(sv->view());
                                    parsedMatrixStr = true;
                                    hasStep = true;
                                    break;
                                }
                            }
                            continue;
                        }
                        bool ok = false;
                        if (a.name == "M11") { m11 = evalFloatAttr(a, 1.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M12") { m12 = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M21") { m21 = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "M22") { m22 = evalFloatAttr(a, 1.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "OffsetX") { ox = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                        else if (a.name == "OffsetY") { oy = evalFloatAttr(a, 0.0f, ok); anyElemAttr |= ok; }
                    }
                    if (!parsedMatrixStr && anyElemAttr) {
                        step = Matrix2D::FromWpfMatrix(m11, m12, m21, m22, ox, oy);
                        hasStep = true;
                    }
                }

                if (hasStep) {
                    tfScope->accumulatedTransform = Matrix2D::Multiply(step, tfScope->accumulatedTransform);
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope(XmlScopeKind::TransformChildNode);
                }
                continue;
            }

            // 2. Phase 5C: <Style>, <Trigger>, <MultiTrigger>, <VisualStateGroup>, <Storyboard>, <Setter>, <*Ease>
            if (shortTag == "Style") {
                if (!styleStack.empty() && styleStack.back().isInlinePropertyElement) {
                    styleStack.back().isInlinePropertyElement = false;
                    if (!xKey.empty()) {
                        styleStack.back().def.key = std::string(xKey);
                    }
                    for (const auto& a : attrs) {
                        if (a.name == "TargetType") {
                            styleStack.back().def.targetTypeName = ExtractTargetTypeShortName(a.value);
                        } else if (a.name == "BasedOn") {
                            const std::string_view bKey = ExtractMarkupResourceKey(a.value);
                            styleStack.back().def.basedOnKey = !bKey.empty()
                                ? std::string(bKey)
                                : std::string(Trim(a.value));
                        }
                    }
                    if (!selfClosing) {
                        (void)pushNonVisualScope();
                    }
                    continue;
                }

                AxbStyleDefinition styleDef{};
                styleDef.key = std::string(xKey);
                for (const auto& a : attrs) {
                    if (a.name == "TargetType") {
                        styleDef.targetTypeName = ExtractTargetTypeShortName(a.value);
                    } else if (a.name == "BasedOn") {
                        const std::string_view bKey = ExtractMarkupResourceKey(a.value);
                        styleDef.basedOnKey = !bKey.empty()
                            ? std::string(bKey)
                            : std::string(Trim(a.value));
                    }
                }
                if (selfClosing) {
                    if (!styleDef.key.empty() || !styleDef.targetTypeName.empty()) {
                        pkg.styles.push_back(std::move(styleDef));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveStyleScope sc{};
                    sc.def = std::move(styleDef);
                    sc.scopeDepth = depth;
                    styleStack.push_back(std::move(sc));
                }
                continue;
            }

            if (shortTag == "ControlTemplate.Triggers" || shortTag == "Interaction.Triggers") {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    if (!styleStack.empty()) {
                        if (shortTag == "Interaction.Triggers") {
                            styleStack.back().interactionTriggersDepth = depth;
                        } else {
                            styleStack.back().controlTemplateTriggersDepth = depth;
                        }
                    }
                }
                continue;
            }

            // P1-1: Compile <ControlTemplate> inside ResourceDictionary, <Setter Property="Template">, or <Control.Template>
            const bool isInlineSetterCt =
                !setterStack.empty() && setterStack.back().setter.propId == PropId_Template;
            if (shortTag == "ControlTemplate" &&
                (isInlineSetterCt ||
                 ((parentScope.kind == XmlScopeKind::ResourceContainer || parentScope.isControlTemplateContainer) &&
                  styleStack.empty()))) {
                std::string ctTargetType;
                for (const auto& a : attrs) {
                    if (a.name == "TargetType") {
                        ctTargetType = ExtractTargetTypeShortName(a.value);
                    }
                }
                if (ctTargetType.empty() && isInlineSetterCt && !styleStack.empty()) {
                    ctTargetType = styleStack.back().def.targetTypeName;
                }
                if (!selfClosing) {
                    std::size_t innerEnd = 0u;
                    std::size_t afterClose = 0u;
                    if (FindMatchingClosingTag(src, pos, "ControlTemplate", innerEnd, afterClose)) {
                        const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                        const std::string outerScopeName = !pkg.className.empty()
                            ? pkg.className
                            : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                        std::string ctKey;
                        if (!xKey.empty()) {
                            ctKey = std::string(xKey);
                        } else if (isInlineSetterCt || parentScope.isControlTemplateContainer) {
                            ctKey = "__inline_ct_" + outerScopeName + "_" +
                                    std::to_string(nextInlineControlTplId++);
                        } else {
                            ctKey = "__implicit_ct_" + outerScopeName + "_" +
                                    (ctTargetType.empty() ? std::to_string(nextInlineControlTplId++) : ctTargetType);
                        }
                        flushDeferredMtsdfJobs();
                        AxbTemplateRegistry::Instance().RegisterResources(pkg);
                        AeroCompileInput subIn{};
                        subIn.xamlPath = ctKey;
                        subIn.xamlContent = std::string(innerXml);
                        subIn.externalXamlResolver = input.externalXamlResolver;
                        subIn.resourceSearchRoot = input.resourceSearchRoot;
                        subIn.resourceSearchPaths = input.resourceSearchPaths;
                        subIn.autoMergeGlobalResources = false;
                        AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                        if (!subRes.succeeded) {
                            for (const auto& d : subRes.diagnostics) {
                                result.diagnostics.push_back(d);
                            }
                        }
                        if (subRes.succeeded) {
                            AxbControlTemplate ct{};
                            ct.key = ctKey;
                            ct.targetTypeName = std::move(ctTargetType);

                            // Extract <ControlTemplate.Triggers> & root <VisualStateManager.VisualStateGroups>
                            // onto AxbControlTemplate so they evaluate against the templated host control!
                            std::string rootInlineVsmKey;
                            if (!subRes.axbPackage.instructions.empty() &&
                                subRes.axbPackage.instructions[0].opcode == AxbOpcode::BeginElement) {
                                for (std::size_t idx = 1u; idx < subRes.axbPackage.instructions.size(); ++idx) {
                                    const auto& inst = subRes.axbPackage.instructions[idx];
                                    if (inst.opcode == AxbOpcode::BeginElement ||
                                        inst.opcode == AxbOpcode::EndElement) {
                                        break;
                                    }
                                    if (inst.opcode == AxbOpcode::SetStaticProperty &&
                                        inst.propId == PropId_Style) {
                                        if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                                            if (sv->view().rfind("__inline_style_", 0) == 0) {
                                                rootInlineVsmKey = sv->str();
                                                subRes.axbPackage.instructions.erase(
                                                    subRes.axbPackage.instructions.begin() +
                                                    static_cast<std::ptrdiff_t>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                            }

                            for (auto stIt = subRes.axbPackage.styles.begin();
                                 stIt != subRes.axbPackage.styles.end();) {
                                const bool isCtRootTrigs = (stIt->key.rfind("__ct_root_triggers_", 0) == 0);
                                const bool isRootVsm = (!rootInlineVsmKey.empty() && stIt->key == rootInlineVsmKey);
                                if (isCtRootTrigs || isRootVsm) {
                                    for (auto& tr : stIt->triggers) {
                                        if (isCtRootTrigs) {
                                            tr.overrideLocalValues = true;
                                        }
                                        ct.triggers.push_back(std::move(tr));
                                    }
                                    for (auto& et : stIt->eventTriggers) {
                                        ct.eventTriggers.push_back(std::move(et));
                                    }
                                    for (auto& vsg : stIt->visualStateGroups) {
                                        ct.visualStateGroups.push_back(std::move(vsg));
                                    }
                                    if (stIt->setters.empty()) {
                                        stIt = subRes.axbPackage.styles.erase(stIt);
                                        continue;
                                    }
                                }
                                ++stIt;
                            }

                            // Merge baked MTSDF vector geometry entries into outer package
                            for (const auto& mp : subRes.axbPackage.mtsdfPaths) {
                                if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
                                    pkg.mtsdfPaths.push_back(mp);
                                }
                            }

                            ct.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                            AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);

                            if (isInlineSetterCt) {
                                setterStack.back().setter.value = value::FromString(ct.key);
                                setterStack.back().hasValue = true;
                            } else if (parentScope.isControlTemplateContainer) {
                                AxbInstruction setTplInst{};
                                setTplInst.opcode = AxbOpcode::SetStaticProperty;
                                setTplInst.propId = PropId_Template;
                                setTplInst.literalValue = value::FromString(ct.key);
                                pkg.instructions.push_back(std::move(setTplInst));
                            }
                            pkg.controlTemplates.push_back(std::move(ct));
                        }
                        pos = afterClose;
                        continue;
                    }
                } else {
                    continue;
                }
            }

            // Compile <ItemsPanelTemplate> inside ResourceDictionary, <*.ItemsPanel>, or <Setter Property="ItemsPanel">
            {
                const bool isSetterIp =
                    (shortTag == "ItemsPanelTemplate" && !setterStack.empty() &&
                     !setterStack.back().hasValue && setterStack.back().setter.propId == PropId_ItemsPanel);
                if (shortTag == "ItemsPanelTemplate" &&
                    (parentScope.kind == XmlScopeKind::ResourceContainer ||
                     parentScope.isItemsPanelContainer ||
                     isSetterIp)) {
                    if (!selfClosing) {
                        std::size_t innerEnd = 0u;
                        std::size_t afterClose = 0u;
                        if (FindMatchingClosingTag(src, pos, "ItemsPanelTemplate", innerEnd, afterClose)) {
                            bool isVirt = false;
                            const std::string spec = ParseItemsPanelInnerXmlToSpec(
                                src.substr(pos, innerEnd - pos), isVirt);
                            if (!spec.empty()) {
                                if (isSetterIp) {
                                    setterStack.back().setter.value = value::FromString(spec);
                                    setterStack.back().hasValue = true;
                                } else if (parentScope.isItemsPanelContainer) {
                                    pkg.instructions.push_back(AxbInstruction{
                                        AxbOpcode::SetStaticProperty, PropId_ItemsPanel, 0, 0, 0, 0,
                                        value::FromString(spec)});
                                    if (isVirt) {
                                        pkg.instructions.push_back(AxbInstruction{
                                            AxbOpcode::SetStaticProperty, PropId_IsVirtualizing, 0, 0, 0, 0,
                                            UnifiedValue{true}});
                                    }
                                } else if (!xKey.empty()) {
                                    UpsertPackageResource(pkg, xKey, value::FromString(spec));
                                }
                            }
                            pos = afterClose;
                            continue;
                        }
                    } else {
                        continue;
                    }
                }
            }

            // Collect <KeyBinding> inside <*.InputBindings>
            if (shortTag == "KeyBinding") {
                std::string kbKey, kbMods, kbCmd, kbParam;
                for (const auto& a : attrs) {
                    if (a.name == "Key") kbKey = std::string(Trim(a.value));
                    else if (a.name == "Modifiers") kbMods = std::string(Trim(a.value));
                    else if (a.name == "Gesture") {
                        const std::string_view g = Trim(a.value);
                        if (const auto plus = g.rfind('+'); plus != std::string_view::npos) {
                            kbMods = std::string(g.substr(0u, plus));
                            kbKey = std::string(g.substr(plus + 1u));
                        } else {
                            kbKey = std::string(g);
                        }
                    } else if (a.name == "Command") {
                        const auto pb = ParseBindingMarkupExpression(a.value);
                        kbCmd = pb.isValidBinding ? std::string(pb.pathPart) : DecodeXamlXmlEntities(a.value);
                    } else if (a.name == "CommandParameter") {
                        kbParam = DecodeXamlXmlEntities(a.value);
                    }
                }
                const KeyCode kc = ParseXamlKeyCode(kbKey);
                const std::uint8_t km = ParseXamlKeyModifiers(kbMods);
                if (kc != KeyCode::None && !kbCmd.empty()) {
                    const std::string rec = std::to_string(static_cast<unsigned>(kc)) + "|" +
                                            std::to_string(static_cast<unsigned>(km)) + "|" +
                                            kbCmd + "|" + kbParam;
                    AxbInstruction kbInst{};
                    kbInst.opcode = AxbOpcode::SetStaticProperty;
                    kbInst.propId = PropId_KeyBindingsSpec;
                    kbInst.literalValue = value::FromString(rec);
                    pkg.instructions.push_back(std::move(kbInst));
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            // Collect <CommandBinding> inside <*.CommandBindings>
            if (shortTag == "CommandBinding") {
                std::string cbCmd, cbExec, cbCanExec;
                for (const auto& a : attrs) {
                    if (a.name == "Command") {
                        const auto pb = ParseBindingMarkupExpression(a.value);
                        cbCmd = pb.isValidBinding ? std::string(pb.pathPart) : DecodeXamlXmlEntities(a.value);
                    } else if (a.name == "Executed") {
                        cbExec = std::string(Trim(a.value));
                    } else if (a.name == "CanExecute") {
                        cbCanExec = std::string(Trim(a.value));
                    }
                }
                if (!cbCmd.empty() && (!cbExec.empty() || !cbCanExec.empty())) {
                    const std::string rec = cbCmd + "|" + cbExec + "|" + cbCanExec;
                    AxbInstruction cbInst{};
                    cbInst.opcode = AxbOpcode::SetStaticProperty;
                    cbInst.propId = PropId_CommandBindingsSpec;
                    cbInst.literalValue = value::FromString(rec);
                    pkg.instructions.push_back(std::move(cbInst));
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            // Collect <GridViewColumn> inside <ListView.View><GridView>...</GridView></ListView.View>
            if (shortTag == "GridViewColumn") {
                XmlScopeEntry* lvViewScope = nullptr;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) break;
                    if (it->isListViewViewContainer) {
                        lvViewScope = &(*it);
                        break;
                    }
                }
                if (lvViewScope != nullptr) {
                    std::string colHeader, colWidth = "100", colBindPath, colFormat, colCellTpl;
                    for (const auto& a : attrs) {
                        if (a.name == "Header") {
                            colHeader = DecodeXamlXmlEntities(a.value);
                        } else if (a.name == "Width") {
                            colWidth = std::string(Trim(a.value));
                        } else if (a.name == "DisplayMemberBinding") {
                            const auto parsedB = ParseBindingMarkupExpression(a.value);
                            if (parsedB.isValidBinding) {
                                colBindPath = std::string(parsedB.pathPart);
                                colFormat = parsedB.stringFormat;
                            }
                        } else if (a.name == "CellTemplate") {
                            std::string_view rk = ExtractMarkupResourceKey(a.value);
                            colCellTpl = rk.empty() ? DecodeXamlXmlEntities(a.value) : std::string(rk);
                        }
                    }
                    bool consumedFullColumn = false;
                    if (!selfClosing) {
                        std::size_t colInnerEnd = 0u;
                        std::size_t colAfterClose = 0u;
                        if (FindMatchingClosingTag(src, pos, "GridViewColumn", colInnerEnd, colAfterClose)) {
                            const std::string_view colBody = src.substr(pos, colInnerEnd - pos);
                            std::size_t dtPos = std::string_view::npos;
                            {
                                std::size_t searchPos = 0u;
                                while (searchPos < colBody.size()) {
                                    const std::size_t cand = colBody.find("<DataTemplate", searchPos);
                                    if (cand == std::string_view::npos) break;
                                    const std::size_t afterName = cand + 13u;
                                    if (afterName >= colBody.size() ||
                                        colBody[afterName] == '>' ||
                                        colBody[afterName] == '/' ||
                                        std::isspace(static_cast<unsigned char>(colBody[afterName]))) {
                                        dtPos = cand;
                                        break;
                                    }
                                    searchPos = afterName;
                                }
                            }
                            if (dtPos != std::string_view::npos) {
                                std::size_t dtTagGt = std::string_view::npos;
                                {
                                    char quote = '\0';
                                    for (std::size_t k = dtPos + 13u; k < colBody.size(); ++k) {
                                        const char c = colBody[k];
                                        if (quote != '\0') {
                                            if (c == quote) quote = '\0';
                                        } else {
                                            if (c == '\'' || c == '"') {
                                                quote = c;
                                            } else if (c == '>') {
                                                dtTagGt = k;
                                                break;
                                            }
                                        }
                                    }
                                }
                                bool dtSelfClosing = false;
                                if (dtTagGt != std::string_view::npos) {
                                    std::size_t checkIdx = dtTagGt;
                                    while (checkIdx > dtPos + 13u &&
                                           std::isspace(static_cast<unsigned char>(colBody[checkIdx - 1u]))) {
                                        --checkIdx;
                                    }
                                    dtSelfClosing = (checkIdx > dtPos + 13u && colBody[checkIdx - 1u] == '/');
                                }
                                if (dtTagGt != std::string_view::npos && !dtSelfClosing) {
                                    std::size_t dtInnerEnd = 0u;
                                    std::size_t dtAfterClose = 0u;
                                    const std::size_t absDtAfterOpen = pos + dtTagGt + 1u;
                                    if (FindMatchingClosingTag(src, absDtAfterOpen, "DataTemplate", dtInnerEnd, dtAfterClose)) {
                                        const std::string_view dtInnerXml = src.substr(absDtAfterOpen, dtInnerEnd - absDtAfterOpen);
                                        if (!Trim(dtInnerXml).empty()) {
                                            AxbTemplateRegistry::Instance().RegisterResources(pkg);
                                            AeroCompileInput subIn{};
                                            subIn.xamlPath = input.xamlPath;
                                            subIn.xamlContent = std::string(dtInnerXml);
                                            subIn.externalXamlResolver = input.externalXamlResolver;
                                            subIn.resourceSearchRoot = input.resourceSearchRoot;
                                            subIn.resourceSearchPaths = input.resourceSearchPaths;
                                            subIn.autoMergeGlobalResources = false;
                                            AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                                            for (const auto& d : subRes.diagnostics) {
                                                result.diagnostics.push_back(d);
                                            }
                                            if (!subRes.succeeded) {
                                                result.succeeded = false;
                                                return result;
                                            }
                                            for (const auto& mp : subRes.axbPackage.mtsdfPaths) {
                                                if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
                                                    pkg.mtsdfPaths.push_back(mp);
                                                }
                                            }
                                            const std::string genKey = "__aero_gvc_dt__:" +
                                                pkg.className + ":" + std::to_string(pkg.dataTemplates.size());
                                            AxbDataTemplate dt{};
                                            dt.key = genKey;
                                            dt.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                                            AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
                                            pkg.dataTemplates.push_back(std::move(dt));
                                            colCellTpl = genKey;
                                        }
                                    } else {
                                        result.diagnostics.push_back(
                                            "AERO_XAML_UNCLOSED_TAG: Unclosed <DataTemplate> in <GridViewColumn>");
                                        result.succeeded = false;
                                        return result;
                                    }
                                }
                            }
                            pos = colAfterClose;
                            consumedFullColumn = true;
                        }
                    }
                    if (!lvViewScope->gridViewColumnsSpec.empty()) {
                        lvViewScope->gridViewColumnsSpec.push_back('\x1e');
                    }
                    lvViewScope->gridViewColumnsSpec +=
                        colHeader + "\x1f" + colWidth + "\x1f" + colBindPath + "\x1f" + colFormat + "\x1f" + colCellTpl;
                    if (!selfClosing && !consumedFullColumn) {
                        (void)pushNonVisualScope();
                    }
                    continue;
                }
            }

            // Compile <ContextMenu> inside ResourceDictionary, <*.ContextMenu>, or <Setter Property="ContextMenu">
            const bool isInlineSetterCm =
                (shortTag == "ContextMenu" && !setterStack.empty() && !setterStack.back().hasValue &&
                 setterStack.back().setter.propId == PropId_ContextMenuKey);
            if (shortTag == "ContextMenu" &&
                (parentScope.kind == XmlScopeKind::ResourceContainer ||
                 parentScope.isContextMenuContainer ||
                 isInlineSetterCm)) {
                if (!selfClosing) {
                    std::size_t innerEnd = 0u;
                    std::size_t afterClose = 0u;
                    if (FindMatchingClosingTag(src, pos, "ContextMenu", innerEnd, afterClose)) {
                        const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                        const std::string outerScopeName = !pkg.className.empty()
                            ? pkg.className
                            : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                        const std::string cmKey = !xKey.empty()
                            ? std::string(xKey)
                            : ("__ctx_menu_" + outerScopeName + "_" + std::to_string(nextInlineControlTplId++));
                        flushDeferredMtsdfJobs();
                        AxbTemplateRegistry::Instance().RegisterResources(pkg);
                        AeroCompileInput subIn{};
                        subIn.xamlPath = cmKey;
                        subIn.xamlContent = "<ContextMenu>" + std::string(innerXml) + "</ContextMenu>";
                        subIn.externalXamlResolver = input.externalXamlResolver;
                        subIn.resourceSearchRoot = input.resourceSearchRoot;
                        subIn.resourceSearchPaths = input.resourceSearchPaths;
                        subIn.autoMergeGlobalResources = false;
                        AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                        if (subRes.succeeded && !subRes.axbPackage.instructions.empty()) {
                            for (const auto& mp : subRes.axbPackage.mtsdfPaths) {
                                if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
                                    pkg.mtsdfPaths.push_back(mp);
                                }
                            }
                            AxbControlTemplate ct{};
                            ct.key = cmKey;
                            ct.targetTypeName = "ContextMenu";
                            ct.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                            AxbTemplateRegistry::Instance().RegisterControlTemplate(ct);
                            pkg.controlTemplates.push_back(std::move(ct));
                            if (isInlineSetterCm) {
                                setterStack.back().setter.value = value::FromString(cmKey);
                                setterStack.back().hasValue = true;
                            } else if (parentScope.isContextMenuContainer) {
                                AxbInstruction cmInst{};
                                cmInst.opcode = AxbOpcode::SetStaticProperty;
                                cmInst.propId = PropId_ContextMenuKey;
                                cmInst.literalValue = value::FromString(cmKey);
                                pkg.instructions.push_back(std::move(cmInst));
                            } else if (!xKey.empty()) {
                                UpsertPackageResource(pkg, xKey, value::FromString(cmKey));
                            }
                        }
                        pos = afterClose;
                        continue;
                    }
                } else {
                    continue;
                }
            }

            // Phase 5D: Compile <DataTemplate> / <HierarchicalDataTemplate> inside ResourceDictionary, <*.ItemTemplate/ContentTemplate/HeaderTemplate>, or <Setter>
            const bool isDataOrHierTpl = (shortTag == "DataTemplate" || shortTag == "HierarchicalDataTemplate");
            const bool isInlineSetterDt =
                (isDataOrHierTpl && !setterStack.empty() && !setterStack.back().hasValue &&
                 (setterStack.back().setter.propId == PropId_ItemTemplate ||
                  setterStack.back().setter.propId == PropId_ContentTemplate ||
                  setterStack.back().setter.propId == PropId_HeaderTemplate));
            if (isDataOrHierTpl &&
                (parentScope.kind == XmlScopeKind::ResourceContainer ||
                 parentScope.isItemTemplateContainer ||
                 isInlineSetterDt) &&
                (styleStack.empty() || isInlineSetterDt)) {
                std::string dtDataType;
                std::string dtItemsSourcePath;
                std::string dtChildItemTemplateKey;
                for (const auto& a : attrs) {
                    if (a.name == "DataType") {
                        dtDataType = ExtractTargetTypeShortName(a.value);
                    } else if (a.name == "ItemsSource") {
                        const ParsedBindingClause isb = ParseBindingMarkupExpression(a.value);
                        dtItemsSourcePath = isb.isValidBinding
                            ? std::string(isb.pathPart)
                            : std::string(Trim(a.value));
                    } else if (a.name == "ItemTemplate") {
                        const std::string_view rk = ExtractMarkupResourceKey(a.value);
                        dtChildItemTemplateKey = !rk.empty() ? std::string(rk) : std::string(Trim(a.value));
                    }
                }
                if (!selfClosing) {
                    std::size_t innerEnd = 0u;
                    std::size_t afterClose = 0u;
                    if (FindMatchingClosingTag(src, pos, shortTag, innerEnd, afterClose)) {
                        const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                        const std::string outerScopeName = !pkg.className.empty()
                            ? pkg.className
                            : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                        std::string dtKey;
                        if (parentScope.isItemTemplateContainer || isInlineSetterDt) {
                            dtKey = "__item_tpl_" + outerScopeName + "_" +
                                    std::to_string(nextInlineDataTplId++);
                        } else if (!xKey.empty()) {
                            dtKey = std::string(xKey);
                        } else {
                            dtKey = "__implicit_dt_" + outerScopeName + "_" +
                                    (dtDataType.empty() ? std::to_string(nextInlineDataTplId++) : dtDataType);
                        }
                        flushDeferredMtsdfJobs();
                        AxbTemplateRegistry::Instance().RegisterResources(pkg);
                        AeroCompileInput subIn{};
                        subIn.xamlPath = dtKey;
                        subIn.xamlContent = std::string(innerXml);
                        subIn.externalXamlResolver = input.externalXamlResolver;
                        subIn.resourceSearchRoot = input.resourceSearchRoot;
                        subIn.resourceSearchPaths = input.resourceSearchPaths;
                        subIn.autoMergeGlobalResources = false;
                        AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                        if (subRes.succeeded && !subRes.axbPackage.instructions.empty()) {
                            for (const auto& mp : subRes.axbPackage.mtsdfPaths) {
                                if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
                                    pkg.mtsdfPaths.push_back(mp);
                                }
                            }
                            AxbDataTemplate dt{};
                            if (isInlineSetterDt) {
                                dt.key = dtKey.empty()
                                    ? ("__setter_dt_" + outerScopeName + "_" + std::to_string(nextInlineDataTplId++))
                                    : dtKey;
                                setterStack.back().setter.value = value::FromString(dt.key);
                                setterStack.back().hasValue = true;
                            } else if (parentScope.isItemTemplateContainer) {
                                dt.key = dtKey;
                                AxbInstruction setTplInst{};
                                setTplInst.opcode = AxbOpcode::SetStaticProperty;
                                setTplInst.propId = (parentScope.targetPropId != 0 && parentScope.targetPropId != PropId_Background)
                                    ? parentScope.targetPropId
                                    : static_cast<std::uint16_t>(PropId_ItemTemplate);
                                setTplInst.literalValue = value::FromString(dt.key);
                                pkg.instructions.push_back(std::move(setTplInst));
                            } else {
                                dt.key = std::string(xKey);
                            }
                            dt.dataTypeName = std::move(dtDataType);
                            dt.itemsSourcePath = std::move(dtItemsSourcePath);
                            dt.childItemTemplateKey = std::move(dtChildItemTemplateKey);
                            dt.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                            AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
                            pkg.dataTemplates.push_back(std::move(dt));
                        }
                        pos = afterClose;
                        continue;
                    }
                } else {
                    continue;
                }
            }

            if ((shortTag == "EventTrigger" ||
                 shortTag == "StoryboardCompletedTrigger" ||
                 shortTag == "PropertyChangedTrigger") && !styleStack.empty()) {
                AxbEventTrigger et{};
                bool rec = false;
                if (shortTag == "StoryboardCompletedTrigger") {
                    et.eventKind = RoutedTriggerEvent::StoryboardCompleted;
                    for (const auto& a : attrs) {
                        if (a.name == "Storyboard") {
                            const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                            et.sourceName = !sbKey.empty()
                                ? std::string(sbKey)
                                : std::string(Trim(a.value));
                            rec = !et.sourceName.empty();
                        }
                    }
                } else if (shortTag == "PropertyChangedTrigger") {
                    et.eventKind = RoutedTriggerEvent::PropertyChanged;
                    for (const auto& a : attrs) {
                        if (a.name == "Binding") {
                            const ParsedBindingClause pb = ParseBindingMarkupExpression(a.value);
                            if (pb.isValidBinding) {
                                const std::uint16_t pid = ResolveMappedSetterPropertyId(pb.pathPart);
                                if (pid != kUnmappedPropId) {
                                    et.sourceName = std::string(pb.elementName) + "|" + std::to_string(pid);
                                    rec = true;
                                }
                            }
                        }
                    }
                } else {
                    const bool isInteractionEt =
                        (styleStack.back().interactionTriggersDepth != 0u) ||
                        (tagName.find(':') != std::string_view::npos);
                    rec = isInteractionEt; // <b:EventTrigger> defaults to EventName="Loaded" when omitted
                    for (const auto& a : attrs) {
                        if (a.name == "RoutedEvent" || a.name == "EventName") {
                            et.eventKind = ParseRoutedTriggerEvent(a.value, rec);
                        } else if (a.name == "SourceName") {
                            et.sourceName = std::string(Trim(a.value));
                        }
                    }
                }
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveEventTriggerScope ets{};
                    ets.et = std::move(et);
                    ets.scopeDepth = depth;
                    ets.recognized = rec;
                    eventTriggerStack.push_back(std::move(ets));
                }
                continue;
            }

            if ((shortTag == "Trigger" || shortTag == "MultiTrigger" ||
                 shortTag == "DataTrigger" || shortTag == "MultiDataTrigger") &&
                !styleStack.empty()) {
                AxbStyleTrigger trig{};
                trig.overrideLocalValues = (styleStack.back().controlTemplateTriggersDepth != 0u);
                const bool isData = (shortTag == "DataTrigger" || shortTag == "MultiDataTrigger");
                bool unsupportedCond = false;

                if (shortTag == "Trigger") {
                    std::string_view propAttr;
                    std::string_view valAttr;
                    bool hasValAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Property") {
                            propAttr = a.value;
                        } else if (a.name == "Value") {
                            valAttr = a.value;
                            hasValAttr = true;
                        }
                    }
                    const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr);
                    const std::string_view trimTrigVal = Trim(valAttr);
                    const bool isNullTrigVal =
                        trimTrigVal.size() > 2u && trimTrigVal.front() == '{' && trimTrigVal.back() == '}' &&
                        Trim(trimTrigVal.substr(1u, trimTrigVal.size() - 2u)).rfind("x:Null", 0) == 0;
                    if (condPropId == kUnmappedPropId || !hasValAttr) {
                        unsupportedCond = true;
                    } else if (isNullTrigVal) {
                        AxbTriggerCondition c{};
                        c.propId = condPropId;
                        c.expectNull = true;
                        c.expectedValue = UnifiedValue{NullObject{}};
                        trig.conditions.push_back(std::move(c));
                    } else {
                        bool ok = false;
                        UnifiedValue expVal = parseStyleOrAnimationValue(condPropId, valAttr, ok);
                        if (!ok) {
                            unsupportedCond = true;
                        } else {
                            AxbTriggerCondition c{};
                            c.propId = condPropId;
                            c.expectedValue = std::move(expVal);
                            trig.conditions.push_back(std::move(c));
                        }
                    }
                } else if (shortTag == "DataTrigger") {
                    const bool isInterDt =
                        (styleStack.back().interactionTriggersDepth != 0u) ||
                        (tagName.find(':') != std::string_view::npos);
                    trig.isInteractionTrigger = isInterDt;
                    std::string_view bindAttr;
                    std::string_view valAttr;
                    std::string_view cmpAttr;
                    bool hasValAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "Binding") {
                            bindAttr = a.value;
                        } else if (a.name == "Value") {
                            valAttr = a.value;
                            hasValAttr = true;
                        } else if (a.name == "Comparison") {
                            cmpAttr = a.value;
                        }
                    }
                    AxbTriggerCondition dtCond{};
                    if (buildDataTriggerCondition(bindAttr, valAttr, hasValAttr, dtCond)) {
                        if (!cmpAttr.empty()) {
                            dtCond.comparison = ParseTriggerComparisonOp(cmpAttr);
                        }
                        trig.conditions.push_back(std::move(dtCond));
                    } else {
                        unsupportedCond = true;
                    }
                }

                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveTriggerScope ts{};
                    ts.trig = std::move(trig);
                    ts.scopeDepth = depth;
                    ts.isDataTrigger = isData;
                    ts.hasUnsupportedCondition = unsupportedCond;
                    triggerStack.push_back(std::move(ts));
                }
                continue;
            }

            if (shortTag == "Condition" && !triggerStack.empty()) {
                std::string_view propAttr;
                std::string_view bindAttr;
                std::string_view valAttr;
                bool hasBindAttr = false;
                bool hasValAttr = false;
                for (const auto& a : attrs) {
                    if (a.name == "Property") propAttr = a.value;
                    else if (a.name == "Binding") {
                        bindAttr = a.value;
                        hasBindAttr = true;
                    } else if (a.name == "Value") {
                        valAttr = a.value;
                        hasValAttr = true;
                    }
                }
                if (!triggerStack.back().hasUnsupportedCondition) {
                    if (hasBindAttr || triggerStack.back().isDataTrigger) {
                        AxbTriggerCondition dtCond{};
                        if (buildDataTriggerCondition(bindAttr, valAttr, hasValAttr, dtCond)) {
                            triggerStack.back().trig.conditions.push_back(std::move(dtCond));
                        } else {
                            triggerStack.back().hasUnsupportedCondition = true;
                        }
                    } else {
                        const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr);
                        const std::string_view trimCondVal = Trim(valAttr);
                        const bool isNullCondVal =
                            trimCondVal.size() > 2u && trimCondVal.front() == '{' && trimCondVal.back() == '}' &&
                            Trim(trimCondVal.substr(1u, trimCondVal.size() - 2u)).rfind("x:Null", 0) == 0;
                        if (condPropId == kUnmappedPropId || !hasValAttr) {
                            triggerStack.back().hasUnsupportedCondition = true;
                        } else if (isNullCondVal) {
                            AxbTriggerCondition c{};
                            c.propId = condPropId;
                            c.expectNull = true;
                            c.expectedValue = UnifiedValue{NullObject{}};
                            triggerStack.back().trig.conditions.push_back(std::move(c));
                        } else {
                            bool ok = false;
                            UnifiedValue expVal = parseStyleOrAnimationValue(condPropId, valAttr, ok);
                            if (!ok) {
                                triggerStack.back().hasUnsupportedCondition = true;
                            } else {
                                AxbTriggerCondition c{};
                                c.propId = condPropId;
                                c.expectedValue = std::move(expVal);
                                triggerStack.back().trig.conditions.push_back(std::move(c));
                            }
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "ComparisonCondition" && (!triggerStack.empty() || !eventTriggerStack.empty())) {
                const bool preferEtCond = !eventTriggerStack.empty() &&
                    (triggerStack.empty() || eventTriggerStack.back().scopeDepth > triggerStack.back().scopeDepth);
                std::string_view leftOp;
                std::string_view rightOp;
                std::string_view opAttr;
                bool hasRightOp = false;
                for (const auto& a : attrs) {
                    if (a.name == "LeftOperand") leftOp = a.value;
                    else if (a.name == "RightOperand") {
                        rightOp = a.value;
                        hasRightOp = true;
                    } else if (a.name == "Operator") {
                        opAttr = a.value;
                    }
                }
                if (!leftOp.empty() && hasRightOp) {
                    AxbTriggerCondition extraCond{};
                    if (buildDataTriggerCondition(leftOp, rightOp, hasRightOp, extraCond)) {
                        if (!opAttr.empty()) {
                            extraCond.comparison = ParseTriggerComparisonOp(opAttr);
                        }
                        if (preferEtCond) {
                            eventTriggerStack.back().et.conditions.push_back(std::move(extraCond));
                        } else if (!triggerStack.back().hasUnsupportedCondition) {
                            triggerStack.back().trig.conditions.push_back(std::move(extraCond));
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if ((shortTag == "ChangePropertyAction" ||
                 shortTag == "ControlStoryboardAction" ||
                 shortTag == "InvokeCommandAction" ||
                 shortTag == "RemoveElementAction" ||
                 shortTag == "NavigateToAction" ||
                 shortTag == "NavigateBackAction" ||
                 shortTag == "PlaySoundAction") &&
                (!eventTriggerStack.empty() || !triggerStack.empty())) {
                const bool preferEt = !eventTriggerStack.empty() &&
                    (triggerStack.empty() || eventTriggerStack.back().scopeDepth > triggerStack.back().scopeDepth);

                AxbInteractionAction act{};
                act.actionName = std::string(xName);
                for (const auto& a : attrs) {
                    if (a.name == "IsEnabled") {
                        act.isEnabled = !EqualsIgnoreCase(Trim(a.value), "false") && Trim(a.value) != "0";
                    }
                }

                if (shortTag == "ChangePropertyAction") {
                    act.kind = InteractionActionKind::ChangeProperty;
                    std::string_view rawVal;
                    bool hasValAttr = false;
                    for (const auto& a : attrs) {
                        if (a.name == "TargetName") {
                            act.targetName = std::string(Trim(a.value));
                        } else if (a.name == "PropertyName") {
                            act.propertyName = std::string(Trim(a.value));
                            act.propId = ResolveMappedSetterPropertyId(a.value);
                        } else if (a.name == "Value") {
                            rawVal = a.value;
                            hasValAttr = true;
                        }
                    }
                    if (hasValAttr) {
                        const std::string_view trimRaw = Trim(rawVal);
                        const std::string_view innerNull =
                            (trimRaw.size() > 2u && trimRaw.front() == '{' && trimRaw.back() == '}')
                                ? Trim(trimRaw.substr(1u, trimRaw.size() - 2u))
                                : std::string_view{};
                        const ParsedBindingClause valBind = ParseBindingMarkupExpression(trimRaw);
                        if (!innerNull.empty() && innerNull.rfind("x:Null", 0) == 0) {
                            act.isNullValue = true;
                        } else if (valBind.isValidBinding) {
                            act.hasValueBinding = true;
                            auto bc = std::make_shared<AxbCompiledBindingChain>();
                            bc->targetPropertyId = act.propId;
                            bc->rawPath = std::string(valBind.pathPart);
                            bc->elementName = std::string(valBind.elementName);
                            bc->relativeAncestorType = valBind.relativeAncestorType;
                            bc->relativeSelf = valBind.relativeSelf || valBind.relativeTemplatedParent;
                            bc->converter = valBind.converter;
                            bc->converterKey = valBind.converterKey;
                            bc->converterParameter = valBind.converterParameter;
                            resolveConverterResource(bc->converter, bc->converterKey);
                            bc->stringFormat = valBind.stringFormat;
                            if (!valBind.fallbackValue.empty()) {
                                bc->fallbackValue = (act.propId != kUnmappedPropId)
                                    ? ParseLiteralPropertyValue(act.propId, valBind.fallbackValue)
                                    : value::FromString(DecodeXamlXmlEntities(valBind.fallbackValue));
                            }
                            if (valBind.hasTargetNullValue) {
                                bc->targetNullValue = valBind.targetNullValue.empty()
                                    ? value::FromString("")
                                    : ((act.propId != kUnmappedPropId)
                                        ? ParseLiteralPropertyValue(act.propId, valBind.targetNullValue)
                                        : value::FromString(DecodeXamlXmlEntities(valBind.targetNullValue)));
                            }
                            if (!valBind.pathPart.empty() && valBind.pathPart != ".") {
                                const bool hasValElemSrc = !bc->elementName.empty() ||
                                                           !bc->relativeAncestorType.empty() ||
                                                           bc->relativeSelf;
                                const bool keepValAttached = valBind.isAttachedPropertyPath ||
                                    (hasValElemSrc &&
                                     valBind.pathPart.find('.') != std::string_view::npos &&
                                     !IsInstanceSubHopPropertyPath(valBind.pathPart) && ResolveMappedSetterPropertyId(valBind.pathPart) != kUnmappedPropId);
                                splitPathIntoHops(valBind.pathPart, bc->hops, keepValAttached);
                            }
                            act.valueBinding = std::move(bc);
                        } else if (act.propId != kUnmappedPropId) {
                            bool ok = false;
                            act.value = parseStyleOrAnimationValue(act.propId, rawVal, ok);
                            if (!ok) {
                                act.value = value::FromString(DecodeXamlXmlEntities(rawVal));
                            }
                        } else if (EqualsIgnoreCase(act.propertyName, "IsEnabled")) {
                            act.propId = PropId_IsEnabled;
                            act.value = !EqualsIgnoreCase(Trim(rawVal), "false") && Trim(rawVal) != "0";
                        } else {
                            act.value = value::FromString(DecodeXamlXmlEntities(rawVal));
                        }
                    }
                } else if (shortTag == "ControlStoryboardAction") {
                    act.kind = InteractionActionKind::ControlStoryboard;
                    for (const auto& a : attrs) {
                        if (a.name == "Storyboard") {
                            const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                            act.storyboardKey = !sbKey.empty()
                                ? std::string(sbKey)
                                : std::string(Trim(a.value));
                            if (!act.storyboardKey.empty()) {
                                if (const AxbStoryboard* foundSb = findLocalOrGlobalStoryboard(act.storyboardKey)) {
                                    act.inlineStoryboard = *foundSb;
                                }
                            }
                        } else if (a.name == "ControlStoryboardOption") {
                            act.storyboardOption = ParseControlStoryboardOption(a.value);
                        }
                    }
                } else if (shortTag == "InvokeCommandAction") {
                    act.kind = InteractionActionKind::InvokeCommand;
                    for (const auto& a : attrs) {
                        if (a.name == "Command") {
                            const ParsedBindingClause cmdParsed = ParseBindingMarkupExpression(a.value);
                            const std::string_view cmdPath = cmdParsed.isValidBinding
                                ? cmdParsed.pathPart
                                : Trim(a.value);
                            act.commandPath = std::string(cmdPath);
                            splitPathIntoHops(cmdPath, act.commandHops);
                        } else if (a.name == "CommandParameter") {
                            const std::string_view cpTrim = Trim(a.value);
                            const ParsedBindingClause cpBind = ParseBindingMarkupExpression(cpTrim);
                            if (cpBind.isValidBinding) {
                                act.hasCommandParameterBinding = true;
                                act.commandParameterBinding.rawPath = std::string(cpBind.pathPart);
                                act.commandParameterBinding.elementName = std::string(cpBind.elementName);
                                act.commandParameterBinding.relativeAncestorType = cpBind.relativeAncestorType;
                                act.commandParameterBinding.relativeSelf = cpBind.relativeSelf || cpBind.relativeTemplatedParent;
                                act.commandParameterBinding.converter = cpBind.converter;
                                act.commandParameterBinding.converterKey = cpBind.converterKey;
                                act.commandParameterBinding.converterParameter = cpBind.converterParameter;
                                resolveConverterResource(
                                    act.commandParameterBinding.converter,
                                    act.commandParameterBinding.converterKey);
                                const bool hasCpElemSrc = !act.commandParameterBinding.elementName.empty() ||
                                                          !act.commandParameterBinding.relativeAncestorType.empty() ||
                                                          act.commandParameterBinding.relativeSelf;
                                const bool keepCpAttached = cpBind.isAttachedPropertyPath ||
                                    (hasCpElemSrc &&
                                     cpBind.pathPart.find('.') != std::string_view::npos &&
                                     !IsInstanceSubHopPropertyPath(cpBind.pathPart) && ResolveMappedSetterPropertyId(cpBind.pathPart) != kUnmappedPropId);
                                splitPathIntoHops(cpBind.pathPart, act.commandParameterBinding.hops, keepCpAttached);
                            } else {
                                act.hasCommandParameter = true;
                                const std::string_view resK = ExtractMarkupResourceKey(cpTrim);
                                if (!resK.empty()) {
                                    if (const UnifiedValue* rv = findLocalOrGlobalResource(resK)) {
                                        act.commandParameter = *rv;
                                    } else {
                                        act.commandParameter = value::FromString(DecodeXamlXmlEntities(cpTrim));
                                    }
                                } else {
                                    act.commandParameter = value::FromString(DecodeXamlXmlEntities(cpTrim));
                                }
                            }
                        }
                    }
                } else if (shortTag == "RemoveElementAction") {
                    act.kind = InteractionActionKind::RemoveElement;
                    for (const auto& a : attrs) {
                        if (a.name == "TargetName") {
                            act.targetName = std::string(Trim(a.value));
                        } else if (a.name == "TargetObject") {
                            const ParsedBindingClause tb = ParseBindingMarkupExpression(a.value);
                            if (tb.isValidBinding) {
                                auto bc = std::make_shared<AxbCompiledBindingChain>();
                                bc->rawPath = std::string(tb.pathPart);
                                bc->elementName = std::string(tb.elementName);
                                bc->relativeAncestorType = tb.relativeAncestorType;
                                bc->relativeSelf = tb.relativeSelf || tb.relativeTemplatedParent;
                                if (!tb.pathPart.empty() && tb.pathPart != ".") {
                                    splitPathIntoHops(tb.pathPart, bc->hops, tb.isAttachedPropertyPath);
                                }
                                act.targetObjectBinding = std::move(bc);
                            }
                        }
                    }
                } else if (shortTag == "NavigateToAction") {
                    act.kind = InteractionActionKind::InvokeCommand;
                    for (const auto& a : attrs) {
                        if (a.name == "DestinationId") {
                            act.commandPath = "__NavigateTo:" + std::string(Trim(a.value));
                        }
                    }
                } else if (shortTag == "NavigateBackAction") {
                    act.kind = InteractionActionKind::InvokeCommand;
                    act.commandPath = "__NavigateBack";
                } else if (shortTag == "PlaySoundAction") {
                    act.kind = InteractionActionKind::PlaySound;
                    for (const auto& a : attrs) {
                        if (a.name == "Source") {
                            act.soundSource = DecodeXamlXmlEntities(Trim(a.value));
                        } else if (a.name == "Volume") {
                            act.volume = ParseFloat32(a.value, 1.0f);
                        }
                    }
                }

                if (selfClosing) {
                    commitActionToActiveTrigger(std::move(act), preferEt);
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveInteractionActionScope ias{};
                    ias.action = std::move(act);
                    ias.targetEventTrigger = preferEt;
                    ias.scopeDepth = depth;
                    actionStack.push_back(std::move(ias));
                }
                continue;
            }

            if (!actionStack.empty() &&
                actionStack.back().action.kind == InteractionActionKind::ChangeProperty &&
                actionStack.back().action.propId != kUnmappedPropId &&
                (shortTag == "SolidColorBrush" || shortTag == "GradientStop")) {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        actionStack.back().action.value = value::FromColor(ParseXamlColorLiteral(a.value));
                        break;
                    }
                }
            }

            // P1-4: Property-element <MultiBinding> and <Binding> + <noesis:MathConverter>
            if (shortTag == "MultiBinding" && styleStack.empty()) {
                const bool isNested = !multiBindingStack.empty() &&
                    (bindingElemStack.empty() || multiBindingStack.back().scopeDepth > bindingElemStack.back().scopeDepth);
                AxbCompiledMultiBinding mb{};
                mb.targetPropertyId = isNested ? 0u : findEnclosingPropertyTargetId();
                for (const auto& a : attrs) {
                    if (a.name == "Mode") {
                        const std::string_view mStr = Trim(a.value);
                        if (mStr == "TwoWay") mb.mode = BindingMode::TwoWay;
                        else if (mStr == "OneTime") mb.mode = BindingMode::OneTime;
                    } else if (a.name == "StringFormat") {
                        mb.stringFormat = NormalizeXamlFormatOrExpr(a.value);
                    } else if (a.name == "Converter") {
                        const std::string_view convKey = ExtractMarkupResourceKey(a.value);
                        const std::string_view effKey = !convKey.empty() ? convKey : Trim(a.value);
                        mb.converter = ParseValueConverterKind(effKey);
                        mb.converterKey = std::string(effKey);
                    } else if (a.name == "ConverterParameter") {
                        mb.converterParameter = NormalizeXamlFormatOrExpr(a.value);
                    } else if (a.name == "FallbackValue" && mb.targetPropertyId != kUnmappedPropId) {
                        mb.fallbackValue = ParseLiteralPropertyValue(mb.targetPropertyId, Trim(a.value));
                    }
                }
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveMultiBindingScope mbs{};
                    mbs.mb = std::move(mb);
                    mbs.scopeDepth = depth;
                    mbs.isNestedInParentMulti = isNested;
                    multiBindingStack.push_back(std::move(mbs));
                }
                continue;
            }

            if (shortTag == "Binding" && (styleStack.empty() || !actionStack.empty())) {
                const bool insideMulti = !multiBindingStack.empty() &&
                    (bindingElemStack.empty() || multiBindingStack.back().scopeDepth > bindingElemStack.back().scopeDepth);
                AxbCompiledBindingChain ch{};
                ch.targetPropertyId = insideMulti ? 0u : findEnclosingPropertyTargetId();
                std::string_view fbRaw;
                bool hasTnRaw = false;
                std::string_view tnRaw{};
                for (const auto& a : attrs) {
                    if (a.name == "Path") {
                        std::string_view pp = Trim(a.value);
                        bool isAttachedParen = false;
                        if (pp.size() > 2u && pp.front() == '(' && pp.back() == ')') {
                            pp = Trim(pp.substr(1u, pp.size() - 2u));
                            isAttachedParen = true;
                        }
                        ch.rawPath = std::string(pp);
                        splitPathIntoHops(pp, ch.hops, isAttachedParen);
                    } else if (a.name == "Mode") {
                        const std::string_view mStr = Trim(a.value);
                        if (mStr == "TwoWay") ch.mode = BindingMode::TwoWay;
                        else if (mStr == "OneTime") ch.mode = BindingMode::OneTime;
                    } else if (a.name == "ElementName") {
                        ch.elementName = std::string(Trim(a.value));
                    } else if (a.name == "RelativeSource") {
                        std::string_view rsExpr = Trim(a.value);
                        if (rsExpr.size() > 2u && rsExpr.front() == '{' && rsExpr.back() == '}') {
                            rsExpr = Trim(rsExpr.substr(1u, rsExpr.size() - 2u));
                        }
                        if (rsExpr.find("Self") != std::string_view::npos ||
                            rsExpr.find("TemplatedParent") != std::string_view::npos) {
                            ch.relativeSelf = true;
                        } else if (const auto ancPos = rsExpr.find("AncestorType="); ancPos != std::string_view::npos) {
                            std::string_view ancVal = Trim(rsExpr.substr(ancPos + 13u));
                            int bd = 0;
                            std::size_t ae = 0u;
                            while (ae < ancVal.size()) {
                                if (ancVal[ae] == '{') ++bd;
                                else if (ancVal[ae] == '}' && bd > 0) --bd;
                                else if (ancVal[ae] == ',' && bd == 0) break;
                                ++ae;
                            }
                            ch.relativeAncestorType = ExtractTargetTypeShortName(ancVal.substr(0, ae));
                        }
                    } else if (a.name == "Converter") {
                        const std::string_view convKey = ExtractMarkupResourceKey(a.value);
                        const std::string_view effKey = !convKey.empty() ? convKey : Trim(a.value);
                        ch.converter = ParseValueConverterKind(effKey);
                        ch.converterKey = std::string(effKey);
                    } else if (a.name == "ConverterParameter") {
                        ch.converterParameter = NormalizeXamlFormatOrExpr(a.value);
                    } else if (a.name == "StringFormat") {
                        ch.stringFormat = NormalizeXamlFormatOrExpr(a.value);
                    } else if (a.name == "FallbackValue") {
                        fbRaw = Trim(a.value);
                    } else if (a.name == "TargetNullValue") {
                        hasTnRaw = true;
                        tnRaw = Trim(a.value);
                    }
                }
                if (!fbRaw.empty()) {
                    if (ch.targetPropertyId != 0u && ch.targetPropertyId != kUnmappedPropId) {
                        ch.fallbackValue = ParseLiteralPropertyValue(ch.targetPropertyId, fbRaw);
                    } else {
                        ch.fallbackValue = value::FromString(DecodeXamlXmlEntities(fbRaw));
                    }
                }
                if (hasTnRaw) {
                    if (tnRaw.empty()) {
                        ch.targetNullValue = value::FromString("");
                    } else if (ch.targetPropertyId != 0u && ch.targetPropertyId != kUnmappedPropId) {
                        ch.targetNullValue = ParseLiteralPropertyValue(ch.targetPropertyId, tnRaw);
                    } else {
                        ch.targetNullValue = value::FromString(DecodeXamlXmlEntities(tnRaw));
                    }
                }
                if (selfClosing) {
                    resolveConverterResource(ch.converter, ch.converterKey);
                    if (insideMulti) {
                        multiBindingStack.back().mb.sources.push_back(std::move(ch));
                    } else {
                        emitSingleCompiledChain(std::move(ch));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveBindingElemScope bes{};
                    bes.chain = std::move(ch);
                    bes.scopeDepth = depth;
                    bes.insideMultiBinding = insideMulti;
                    bindingElemStack.push_back(std::move(bes));
                }
                continue;
            }

            if (shortTag == "MathConverter") {
                std::string expr;
                for (const auto& a : attrs) {
                    if (a.name == "Expression") {
                        expr = NormalizeXamlFormatOrExpr(a.value);
                        break;
                    }
                }
                if (!bindingElemStack.empty() &&
                    (multiBindingStack.empty() || bindingElemStack.back().scopeDepth > multiBindingStack.back().scopeDepth)) {
                    bindingElemStack.back().chain.converter = ValueConverterKind::MathExpression;
                    bindingElemStack.back().chain.converterKey = std::move(expr);
                } else if (!multiBindingStack.empty()) {
                    multiBindingStack.back().mb.converter = ValueConverterKind::MathExpression;
                    multiBindingStack.back().mb.converterKey = std::move(expr);
                } else if (parentScope.kind == XmlScopeKind::ResourceContainer && !xKey.empty()) {
                    pkg.resources.emplace_back(
                        "__aero_math_expr__:" + std::string(xKey),
                        value::FromString(std::move(expr)));
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if ((EndsWith(shortTag, ".EnterActions") || EndsWith(shortTag, ".ExitActions")) &&
                !triggerStack.empty()) {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    if (EndsWith(shortTag, ".EnterActions")) {
                        triggerStack.back().enterActionsDepth = depth;
                    } else {
                        triggerStack.back().exitActionsDepth = depth;
                    }
                }
                continue;
            }

            if (shortTag == "BeginStoryboard") {
                for (const auto& a : attrs) {
                    if (a.name == "Storyboard") {
                        const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                        if (!sbKey.empty()) {
                            if (const AxbStoryboard* foundSb = findLocalOrGlobalStoryboard(sbKey)) {
                                if (!eventTriggerStack.empty()) {
                                    eventTriggerStack.back().et.storyboards.push_back(*foundSb);
                                } else if (!triggerStack.empty()) {
                                    if (triggerStack.back().exitActionsDepth != 0u) {
                                        triggerStack.back().trig.exitStoryboards.push_back(*foundSb);
                                    } else if (triggerStack.back().enterActionsDepth != 0u) {
                                        triggerStack.back().trig.enterStoryboards.push_back(*foundSb);
                                    }
                                }
                            }
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "VisualStateGroup" && !styleStack.empty()) {
                AxbVisualStateGroup group{};
                group.name = std::string(xName);
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveVsgScope gs{};
                    gs.group = std::move(group);
                    gs.scopeDepth = depth;
                    vsgStack.push_back(std::move(gs));
                }
                continue;
            }

            if (shortTag == "VisualStateGroup.Transitions" && !vsgStack.empty()) {
                if (!selfClosing) {
                    const std::size_t depth = pushNonVisualScope();
                    vsgStack.back().transitionsDepth = depth;
                }
                continue;
            }

            if (shortTag == "VisualTransition" && !vsgStack.empty()) {
                std::string_view fromState;
                std::string_view toState;
                for (const auto& a : attrs) {
                    if (a.name == "From") fromState = Trim(a.value);
                    else if (a.name == "To") toState = Trim(a.value);
                    else if (a.name == "GeneratedDuration") {
                        const float dur = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                        if ((fromState.empty() && toState.empty()) ||
                            vsgStack.back().group.defaultTransitionDuration <= 0.0f) {
                            vsgStack.back().group.defaultTransitionDuration = dur;
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "VisualState" && !vsgStack.empty()) {
                AxbVisualState vs{};
                vs.name = std::string(xName);
                if (selfClosing) {
                    if (!vs.name.empty()) {
                        vsgStack.back().group.states.push_back(std::move(vs));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveVisualStateScope vss{};
                    vss.state = std::move(vs);
                    vss.scopeDepth = depth;
                    visualStateStack.push_back(std::move(vss));
                }
                continue;
            }

            if (shortTag == "Storyboard") {
                StoryboardOwnerKind sbOwner = StoryboardOwnerKind::IgnoredEventTrigger;
                if (!visualStateStack.empty()) {
                    sbOwner = StoryboardOwnerKind::VisualState;
                } else if (!eventTriggerStack.empty()) {
                    sbOwner = StoryboardOwnerKind::EventTrigger;
                } else if (!triggerStack.empty() && triggerStack.back().exitActionsDepth != 0u) {
                    sbOwner = StoryboardOwnerKind::TriggerExit;
                } else if (!triggerStack.empty() && triggerStack.back().enterActionsDepth != 0u) {
                    sbOwner = StoryboardOwnerKind::TriggerEnter;
                } else if (parentScope.kind == XmlScopeKind::ResourceContainer) {
                    sbOwner = StoryboardOwnerKind::StandaloneResource;
                }

                AxbStoryboard sb{};
                sb.key = std::string(xKey);
                std::string defTargetName;
                std::string defTargetProp;
                for (const auto& a : attrs) {
                    if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                        defTargetName = std::string(Trim(a.value));
                    } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                        defTargetProp = std::string(Trim(a.value));
                    }
                }

                if (selfClosing) {
                    if (sbOwner == StoryboardOwnerKind::StandaloneResource && !sb.key.empty()) {
                        pkg.storyboards.push_back(std::move(sb));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveStoryboardScope sbs{};
                    sbs.sb = std::move(sb);
                    sbs.defaultTargetName = std::move(defTargetName);
                    sbs.defaultTargetProperty = std::move(defTargetProp);
                    sbs.owner = sbOwner;
                    sbs.scopeDepth = depth;
                    storyboardStack.push_back(std::move(sbs));
                }
                continue;
            }

            if (shortTag == "StaticResource" && !storyboardStack.empty()) {
                for (const auto& a : attrs) {
                    if (a.name == "ResourceKey") {
                        const std::string_view refKey = Trim(a.value);
                        if (const AxbStoryboard* refSb = findLocalOrGlobalStoryboard(refKey)) {
                            for (const auto& refTl : refSb->timelines) {
                                storyboardStack.back().sb.timelines.push_back(refTl);
                            }
                        }
                    }
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (IsAnimationTimelineTag(shortTag) && !storyboardStack.empty()) {
                AxbAnimationTimeline tl{};
                tl.targetName = storyboardStack.back().defaultTargetName;
                std::string rawTargetProp = storyboardStack.back().defaultTargetProperty;
                std::string_view rawFrom;
                std::string_view rawTo;
                std::string_view rawBy;
                bool hasFromAttr = false;
                bool hasToAttr = false;
                bool hasByAttr = false;

                for (const auto& a : attrs) {
                    if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                        tl.targetName = std::string(Trim(a.value));
                    } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                        rawTargetProp = std::string(Trim(a.value));
                    } else if (a.name == "BeginTime") {
                        tl.beginTimeSeconds = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                    } else if (a.name == "Duration") {
                        tl.durationSeconds = ParseXamlTimeSpanSeconds(a.value, 0.25f);
                    } else if (a.name == "From") {
                        rawFrom = a.value;
                        hasFromAttr = true;
                    } else if (a.name == "To") {
                        rawTo = a.value;
                        hasToAttr = true;
                    } else if (a.name == "By") {
                        rawBy = a.value;
                        hasByAttr = true;
                    } else if (a.name == "AutoReverse") {
                        tl.autoReverse = EqualsIgnoreCase(Trim(a.value), "true") || Trim(a.value) == "1";
                    } else if (a.name == "RepeatBehavior") {
                        tl.repeatCount = ParseRepeatCountAttr(a.value);
                    } else if (a.name == "FillBehavior") {
                        tl.fillBehavior = EqualsIgnoreCase(Trim(a.value), "Stop")
                            ? FillBehavior::Stop
                            : FillBehavior::HoldEnd;
                    } else if (a.name == "SpeedRatio") {
                        tl.speedRatio = std::max(0.01f, ParseFloat32(a.value, 1.0f));
                    } else if (a.name == "AccelerationRatio") {
                        tl.accelerationRatio = std::clamp(ParseFloat32(a.value, 0.0f), 0.0f, 1.0f);
                    } else if (a.name == "DecelerationRatio") {
                        tl.decelerationRatio = std::clamp(ParseFloat32(a.value, 0.0f), 0.0f, 1.0f);
                    }
                }

                tl.targetPropertyId = ResolveMappedAnimationPropertyId(rawTargetProp);
                if (tl.targetPropertyId != kUnmappedPropId) {
                    if (hasFromAttr) {
                        bool ok = false;
                        tl.fromValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawFrom, ok);
                        tl.hasFrom = ok;
                    }
                    if (hasToAttr) {
                        bool ok = false;
                        tl.toValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawTo, ok);
                        tl.hasTo = ok;
                        tl.isBy = false;
                    } else if (hasByAttr) {
                        bool ok = false;
                        tl.toValue = parseStyleOrAnimationValue(tl.targetPropertyId, rawBy, ok);
                        tl.hasTo = ok;
                        tl.isBy = ok;
                    }
                }

                if (selfClosing) {
                    if (tl.targetPropertyId != kUnmappedPropId) {
                        storyboardStack.back().sb.timelines.push_back(std::move(tl));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveTimelineScope tls{};
                    tls.tl = std::move(tl);
                    tls.scopeDepth = depth;
                    timelineStack.push_back(std::move(tls));
                }
                continue;
            }

            if (IsAnimationKeyFrameTag(shortTag) && !timelineStack.empty()) {
                AxbKeyFrame kf{};
                kf.isDiscrete =
                    (shortTag.rfind("Discrete", 0) == 0) ||
                    EndsWith(shortTag, "BooleanKeyFrame") ||
                    EndsWith(shortTag, "ObjectKeyFrame");
                if (shortTag.rfind("Spline", 0) == 0) {
                    kf.easingKind = EasingFunctionKind::Cubic;
                    kf.easingMode = EasingMode::EaseInOut;
                }
                const std::uint16_t targetPropId = timelineStack.back().tl.targetPropertyId;
                for (const auto& a : attrs) {
                    if (a.name == "KeyTime") {
                        kf.timeSeconds = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                    } else if (a.name == "KeySpline") {
                        // Parse "x1,y1 x2,y2" or "x1,y1,x2,y2"
                        std::string normSpline(a.value);
                        for (char& ch : normSpline) {
                            if (ch == ',' || ch == ';') ch = ' ';
                        }
                        std::istringstream iss(normSpline);
                        float sx1 = 0.0f, sy1 = 0.0f, sx2 = 1.0f, sy2 = 1.0f;
                        if (iss >> sx1 >> sy1 >> sx2 >> sy2) {
                            kf.hasKeySpline = true;
                            kf.easingKind = EasingFunctionKind::Spline;
                            kf.splineX1 = std::clamp(sx1, 0.0f, 1.0f);
                            kf.splineY1 = sy1;
                            kf.splineX2 = std::clamp(sx2, 0.0f, 1.0f);
                            kf.splineY2 = sy2;
                        }
                    } else if (a.name == "Value" && targetPropId != kUnmappedPropId) {
                        const std::string_view trimVal = Trim(a.value);
                        const ParsedBindingClause bm = ParseBindingMarkupExpression(trimVal);
                        if (bm.isValidBinding && !bm.elementName.empty() && !bm.pathPart.empty() && bm.pathPart != ".") {
                            kf.bindingElementName = std::string(bm.elementName);
                            const std::uint16_t bp = ResolveMappedSetterPropertyId(bm.pathPart);
                            kf.bindingPropId = (bp != kUnmappedPropId) ? bp : 0u;
                        } else {
                            bool ok = false;
                            UnifiedValue parsed = parseStyleOrAnimationValue(targetPropId, a.value, ok);
                            if (ok) {
                                kf.value = std::move(parsed);
                            }
                        }
                    }
                }
                if (selfClosing) {
                    if (!std::holds_alternative<std::monostate>(kf.value) ||
                        !kf.bindingElementName.empty()) {
                        timelineStack.back().tl.keyFrames.push_back(std::move(kf));
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveKeyFrameScope kfs{};
                    kfs.kf = std::move(kf);
                    kfs.scopeDepth = depth;
                    keyFrameStack.push_back(std::move(kfs));
                }
                continue;
            }

            if (IsEasingFunctionTag(shortTag)) {
                EasingFunctionKind ek = ParseEasingFunctionKind(shortTag);
                if (shortTag == "PowerEase") {
                    ek = EasingFunctionKind::Quadratic;
                    for (const auto& a : attrs) {
                        if (a.name == "Power") {
                            const int p = static_cast<int>(std::lround(ParseFloat32(a.value, 2.0f)));
                            if (p <= 1) ek = EasingFunctionKind::Linear;
                            else if (p == 2) ek = EasingFunctionKind::Quadratic;
                            else if (p == 3) ek = EasingFunctionKind::Cubic;
                            else if (p == 4) ek = EasingFunctionKind::Quartic;
                            else ek = EasingFunctionKind::Quintic;
                        }
                    }
                }
                EasingMode em = EasingMode::EaseOut;
                for (const auto& a : attrs) {
                    if (a.name == "EasingMode") {
                        em = ParseEasingMode(a.value);
                    }
                }
                if (!keyFrameStack.empty()) {
                    keyFrameStack.back().kf.easingKind = ek;
                    keyFrameStack.back().kf.easingMode = em;
                } else if (!timelineStack.empty()) {
                    timelineStack.back().tl.easingKind = ek;
                    timelineStack.back().tl.easingMode = em;
                } else if (!vsgStack.empty()) {
                    vsgStack.back().group.defaultEasingKind = ek;
                    vsgStack.back().group.defaultEasingMode = em;
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            if (shortTag == "Setter" &&
                (!visualStateStack.empty() || !triggerStack.empty() || !styleStack.empty())) {
                SetterOwnerKind owner = SetterOwnerKind::Style;
                if (!visualStateStack.empty()) {
                    owner = SetterOwnerKind::VisualState;
                } else if (!triggerStack.empty()) {
                    owner = SetterOwnerKind::Trigger;
                }

                AxbPropertySetter setter{};
                setter.propId = kUnmappedPropId;
                std::string_view rawVal;
                bool hasValAttr = false;
                for (const auto& a : attrs) {
                    if (a.name == "Property") {
                        const std::string_view pTrim = Trim(a.value);
                        if (pTrim == "Color" && !styleStack.empty() &&
                            StripXmlPrefix(styleStack.back().def.targetTypeName) == "BoxShadow") {
                            setter.propId = PropId_EffectColor;
                        } else {
                            setter.propId = ResolveMappedSetterPropertyId(a.value);
                        }
                    } else if (a.name == "TargetName") {
                        setter.targetName = std::string(Trim(a.value));
                    } else if (a.name == "Value") {
                        rawVal = a.value;
                        hasValAttr = true;
                    }
                }

                bool hasValidVal = false;
                if (setter.propId != kUnmappedPropId && hasValAttr) {
                    const ParsedBindingClause setterBind = ParseBindingMarkupExpression(rawVal);
                    if (setterBind.isValidBinding && owner == SetterOwnerKind::Style) {
                        auto sb = std::make_shared<AxbCompiledBindingChain>();
                        sb->rawPath = std::string(setterBind.pathPart);
                        sb->targetPropertyId = setter.propId;
                        sb->mode = setterBind.mode;
                        sb->elementName = std::string(setterBind.elementName);
                        sb->relativeAncestorType = setterBind.relativeAncestorType;
                        sb->relativeSelf = setterBind.relativeSelf || setterBind.relativeTemplatedParent;
                        sb->converter = setterBind.converter;
                        sb->converterKey = setterBind.converterKey;
                        sb->converterParameter = setterBind.converterParameter;
                        sb->stringFormat = setterBind.stringFormat;
                        if (!setterBind.fallbackValue.empty()) {
                            sb->fallbackValue = ParseLiteralPropertyValue(setter.propId, setterBind.fallbackValue);
                        }
                        if (setterBind.hasTargetNullValue) {
                            sb->targetNullValue = setterBind.targetNullValue.empty()
                                ? value::FromString("")
                                : ParseLiteralPropertyValue(setter.propId, setterBind.targetNullValue);
                        }
                        resolveConverterResource(sb->converter, sb->converterKey);
                        const bool hasSbElemSrc = !sb->elementName.empty() ||
                                                  !sb->relativeAncestorType.empty() ||
                                                  sb->relativeSelf;
                        const bool keepSbAttached = setterBind.isAttachedPropertyPath ||
                            (hasSbElemSrc &&
                             setterBind.pathPart.find('.') != std::string_view::npos &&
                             !IsInstanceSubHopPropertyPath(setterBind.pathPart) && ResolveMappedSetterPropertyId(setterBind.pathPart) != kUnmappedPropId);
                        splitPathIntoHops(setterBind.pathPart, sb->hops, keepSbAttached);
                        setter.hasBinding = true;
                        setter.binding = std::move(sb);
                        hasValidVal = true;
                    } else {
                        const std::string_view trimSetVal = Trim(rawVal);
                        if (setter.propId == PropId_IsChecked &&
                            trimSetVal.size() > 2u && trimSetVal.front() == '{' && trimSetVal.back() == '}' &&
                            Trim(trimSetVal.substr(1u, trimSetVal.size() - 2u)).rfind("x:Null", 0) == 0) {
                            setter.value = UnifiedValue{NullObject{}};
                            hasValidVal = true;
                        } else {
                            setter.value = parseStyleOrAnimationValue(setter.propId, rawVal, hasValidVal);
                        }
                    }
                }

                if (selfClosing) {
                    if (hasValidVal) {
                        commitSetterToOwner(std::move(setter), owner);
                    }
                } else {
                    const std::size_t depth = pushNonVisualScope();
                    ActiveSetterScope ss{};
                    ss.setter = std::move(setter);
                    ss.owner = owner;
                    ss.hasValue = hasValidVal;
                    ss.scopeDepth = depth;
                    setterStack.push_back(std::move(ss));
                }
                continue;
            }

            auto parseGradientBrushHeader = [&](std::string_view stName) -> GradientBrushDesc {
                GradientBrushDesc d{};
                d.kind = (stName == "RadialGradientBrush")
                    ? GradientBrushKind::Radial
                    : GradientBrushKind::Linear;
                d.startPoint = Point{0.0f, 0.0f};
                d.endPoint = Point{1.0f, 1.0f};
                d.center = Point{0.5f, 0.5f};
                d.gradientOrigin = Point{0.5f, 0.5f};
                d.radiusX = 0.5f;
                d.radiusY = 0.5f;
                d.opacity = 1.0f;
                for (const auto& a : attrs) {
                    if (a.value.empty() || a.value.front() == '{') {
                        continue;
                    }
                    if (a.name == "StartPoint") {
                        d.startPoint = ParseXamlPoint2D(a.value, d.startPoint);
                    } else if (a.name == "EndPoint") {
                        d.endPoint = ParseXamlPoint2D(a.value, d.endPoint);
                    } else if (a.name == "Center") {
                        d.center = ParseXamlPoint2D(a.value, d.center);
                    } else if (a.name == "GradientOrigin") {
                        d.gradientOrigin = ParseXamlPoint2D(a.value, d.gradientOrigin);
                    } else if (a.name == "RadiusX") {
                        d.radiusX = std::max(1e-5f, ParseFloat32(a.value, d.radiusX));
                    } else if (a.name == "RadiusY") {
                        d.radiusY = std::max(1e-5f, ParseFloat32(a.value, d.radiusY));
                    } else if (a.name == "Opacity") {
                        d.opacity = std::clamp(ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                    } else if (a.name == "MappingMode") {
                        if (EqualsIgnoreCase(Trim(a.value), "Absolute")) {
                            d.mappingMode = BrushMappingMode::Absolute;
                        } else {
                            d.mappingMode = BrushMappingMode::RelativeToBoundingBox;
                        }
                    } else if (a.name == "SpreadMethod") {
                        const std::string_view sm = Trim(a.value);
                        if (EqualsIgnoreCase(sm, "Reflect")) {
                            d.spreadMethod = GradientSpreadMethod::Reflect;
                        } else if (EqualsIgnoreCase(sm, "Repeat")) {
                            d.spreadMethod = GradientSpreadMethod::Repeat;
                        } else {
                            d.spreadMethod = GradientSpreadMethod::Pad;
                        }
                    }
                }
                return d;
            };

            if (!gradientStack.empty() && shortTag == "GradientStop") {
                auto& activeGrad = gradientStack.back().desc;
                float stopOffset = activeGrad.stops.empty() ? 0.0f : 1.0f;
                Color stopColor = activeGrad.stops.empty()
                    ? Color{1.0f, 0.0f, 0.0f, 1.0f}
                    : Color{0.0f, 1.0f, 0.0f, 1.0f};
                for (const auto& a : attrs) {
                    if (a.name == "Offset") {
                        if (!a.value.empty() && a.value.front() != '{') {
                            stopOffset = ParseFloat32(a.value, stopOffset);
                        }
                    } else if (a.name == "Color") {
                        const std::string_view resKey = ExtractMarkupResourceKey(a.value);
                        if (!resKey.empty()) {
                            if (const UnifiedValue* rv = findLocalOrGlobalResource(resKey)) {
                                if (auto cOpt = value::TryGetColor(*rv)) {
                                    stopColor = *cOpt;
                                }
                            }
                        } else if (!a.value.empty() && a.value.front() != '{') {
                            stopColor = ParseXamlColorLiteral(a.value);
                        }
                    }
                }
                activeGrad.stops.push_back(GradientStop{stopOffset, stopColor});
            }

            if (!setterStack.empty() && !setterStack.back().hasValue &&
                setterStack.back().setter.propId != kUnmappedPropId) {
                if (shortTag == "SolidColorBrush") {
                    float brushAlphaMul = 1.0f;
                    for (const auto& a : attrs) {
                        if (a.name == "Opacity") {
                            brushAlphaMul = std::clamp(ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                        }
                    }
                    for (const auto& a : attrs) {
                        if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                            Color c = ParseXamlColorLiteral(a.value);
                            c.a *= brushAlphaMul;
                            setterStack.back().setter.value = value::FromColor(c);
                            setterStack.back().hasValue = true;
                            break;
                        }
                    }
                } else if (!selfClosing &&
                           (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush")) {
                    XmlScopeEntry gradXml{};
                    gradXml.kind = XmlScopeKind::NonVisualSkip;
                    gradXml.tagName = std::string(tagName);
                    scopeStack.push_back(std::move(gradXml));
                    ++nonVisualDepth;

                    ActiveGradientBrushScope gs{};
                    gs.desc = parseGradientBrushHeader(shortTag);
                    gs.owner = GradientScopeOwner::StyleSetter;
                    gs.scopeDepth = scopeStack.size();
                    gradientStack.push_back(std::move(gs));
                    continue;
                } else if (shortTag == "ImageBrush" || shortTag == "BitmapImage") {
                    bool isSetterImgBrush = (shortTag == "ImageBrush");
                    std::uint8_t setterIbStretch = 1u;
                    if (!isSetterImgBrush) {
                        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                            if (it->kind == XmlScopeKind::VisualElement) break;
                            if (it->isImageBrushScope) {
                                isSetterImgBrush = true;
                                setterIbStretch = it->imageBrushStretch;
                                break;
                            }
                        }
                    } else {
                        for (const auto& a : attrs) {
                            if (a.name == "Stretch") {
                                setterIbStretch = ParseImageBrushStretchCode(a.value);
                            }
                        }
                    }
                    for (const auto& a : attrs) {
                        if ((a.name == "ImageSource" || a.name == "UriSource" || a.name == "Source") &&
                            !Trim(a.value).empty()) {
                            const std::string_view rawU = Trim(a.value);
                            if (rawU.front() == '{') {
                                const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                                if (!refKey.empty()) {
                                    if (const UnifiedValue* rv = findLocalOrGlobalResource(refKey)) {
                                        if (isSetterImgBrush) {
                                            if (const auto* sv = std::get_if<StringValue>(rv)) {
                                                setterStack.back().setter.value = value::FromString(
                                                    EncodeImageBrushSpec(setterIbStretch, StripImageBrushSpecPrefix(sv->view())));
                                            } else {
                                                setterStack.back().setter.value = *rv;
                                            }
                                        } else {
                                            setterStack.back().setter.value = *rv;
                                        }
                                        setterStack.back().hasValue = true;
                                        break;
                                    }
                                }
                            } else {
                                const std::string decoded = DecodeXamlXmlEntities(rawU);
                                setterStack.back().setter.value = value::FromString(
                                    isSetterImgBrush
                                        ? EncodeImageBrushSpec(setterIbStretch, StripImageBrushSpecPrefix(decoded))
                                        : decoded);
                                setterStack.back().hasValue = true;
                                break;
                            }
                        }
                    }
                    if (!selfClosing && shortTag == "ImageBrush" && !setterStack.back().hasValue) {
                        XmlScopeEntry ibXml{};
                        ibXml.kind = XmlScopeKind::NonVisualSkip;
                        ibXml.tagName = std::string(tagName);
                        ibXml.isImageBrushScope = true;
                        ibXml.imageBrushStretch = setterIbStretch;
                        scopeStack.push_back(std::move(ibXml));
                        ++nonVisualDepth;
                        continue;
                    }
                }
            }

            // 2.5 Nested <*.Resources>, <ResourceDictionary.MergedDictionaries>, <ResourceDictionary Source="...">
            if (shortTag == "ResourceDictionary.Source") {
                if (!selfClosing) {
                    XmlScopeEntry srcScope{};
                    srcScope.kind = XmlScopeKind::NonVisualSkip;
                    srcScope.tagName = std::string(tagName);
                    srcScope.isMergedDictSourceElement = true;
                    scopeStack.push_back(std::move(srcScope));
                    ++nonVisualDepth;
                }
                continue;
            }

            if (shortTag == "ResourceDictionary" ||
                shortTag == "ResourceDictionary.MergedDictionaries" ||
                EndsWith(shortTag, ".MergedDictionaries") ||
                EndsWith(shortTag, ".Resources")) {
                if (shortTag == "ResourceDictionary") {
                    for (const auto& a : attrs) {
                        if (a.name == "Source" && !Trim(a.value).empty()) {
                            compileAndMergeExternalDictionary(DecodeXamlXmlEntities(a.value));
                            break;
                        }
                    }
                }
                if (!selfClosing) {
                    XmlScopeEntry resScope{};
                    resScope.kind = XmlScopeKind::ResourceContainer;
                    resScope.tagName = std::string(tagName);
                    scopeStack.push_back(std::move(resScope));
                    ++nonVisualDepth;
                }
                continue;
            }

            // 2b. P1-5: Active <Element.Transform3D> / <UIElement.Projection> subtree
            if (parentScope.kind == XmlScopeKind::Transform3DPropertyElement &&
                (shortTag == "CompositeTransform3D" || shortTag == "PlaneProjection")) {
                CompositeTransform3DParams p3d{};
                for (const auto& a : attrs) {
                    const std::string_view v = Trim(a.value);
                    if (v.empty()) continue;
                    if (v.front() == '{') {
                        if (a.name == "RotationX") (void)emitPropertyBindingIfValid(PropId_AnimRotation3DX, v);
                        else if (a.name == "RotationY") (void)emitPropertyBindingIfValid(PropId_AnimRotation3DY, v);
                        else if (a.name == "RotationZ") (void)emitPropertyBindingIfValid(PropId_AnimRotation3DZ, v);
                        else if (a.name == "TranslateX" || a.name == "LocalOffsetX" || a.name == "GlobalOffsetX")
                            (void)emitPropertyBindingIfValid(PropId_AnimTranslate3DX, v);
                        else if (a.name == "TranslateY" || a.name == "LocalOffsetY" || a.name == "GlobalOffsetY")
                            (void)emitPropertyBindingIfValid(PropId_AnimTranslate3DY, v);
                        else if (a.name == "TranslateZ" || a.name == "LocalOffsetZ" || a.name == "GlobalOffsetZ")
                            (void)emitPropertyBindingIfValid(PropId_AnimTranslate3DZ, v);
                        continue;
                    }
                    if (a.name == "RotationX") p3d.rotationX = ParseFloat32(v, 0.0f);
                    else if (a.name == "RotationY") p3d.rotationY = ParseFloat32(v, 0.0f);
                    else if (a.name == "RotationZ") p3d.rotationZ = ParseFloat32(v, 0.0f);
                    else if (a.name == "TranslateX" || a.name == "LocalOffsetX" || a.name == "GlobalOffsetX")
                        p3d.translateX = ParseFloat32(v, 0.0f);
                    else if (a.name == "TranslateY" || a.name == "LocalOffsetY" || a.name == "GlobalOffsetY")
                        p3d.translateY = ParseFloat32(v, 0.0f);
                    else if (a.name == "TranslateZ" || a.name == "LocalOffsetZ" || a.name == "GlobalOffsetZ")
                        p3d.translateZ = ParseFloat32(v, 0.0f);
                    else if (a.name == "ScaleX") p3d.scaleX = ParseFloat32(v, 1.0f);
                    else if (a.name == "ScaleY") p3d.scaleY = ParseFloat32(v, 1.0f);
                    else if (a.name == "ScaleZ") p3d.scaleZ = ParseFloat32(v, 1.0f);
                    else if (a.name == "CenterX") { p3d.centerX = ParseFloat32(v, 0.0f); p3d.hasExplicitCenter = true; }
                    else if (a.name == "CenterY") { p3d.centerY = ParseFloat32(v, 0.0f); p3d.hasExplicitCenter = true; }
                    else if (a.name == "CenterZ") { p3d.centerZ = ParseFloat32(v, 0.0f); }
                    else if (a.name == "CameraDistance") { p3d.cameraDistance = ParseFloat32(v, 1000.0f); }
                }
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty,
                    PropId_Transform3DRotateXYZ,
                    0, 0, 0, 0,
                    MakeFloat4InlineFast(p3d.rotationX, p3d.rotationY, p3d.rotationZ, p3d.cameraDistance)});
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty,
                    PropId_Transform3DTranslateXYZ,
                    0, 0, 0, 0,
                    MakeFloat4InlineFast(p3d.translateX, p3d.translateY, p3d.translateZ, 0.0f)});
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty,
                    PropId_Transform3DScaleXYZ,
                    0, 0, 0, 0,
                    MakeFloat4InlineFast(p3d.scaleX, p3d.scaleY, p3d.scaleZ, 1.0f)});
                if (p3d.hasExplicitCenter || std::fabs(p3d.centerZ) > 1e-5f) {
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty,
                        PropId_Transform3DCenterXYZ,
                        0, 0, 0, 0,
                        MakeFloat4InlineFast(p3d.centerX, p3d.centerY, p3d.centerZ, p3d.hasExplicitCenter ? 1.0f : 0.0f)});
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            // 2c-pre: <noesis:BackgroundEffectBehavior> containing <BlurEffect Radius="..."/>
            if (shortTag == "BlurEffect" && parentScope.kind != XmlScopeKind::EffectPropertyElement &&
                parentScope.kind != XmlScopeKind::ResourceContainer) {
                bool insideBgEffectBehavior = false;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) break;
                    if (StripXmlPrefix(it->tagName) == "BackgroundEffectBehavior") {
                        insideBgEffectBehavior = true;
                        break;
                    }
                }
                if (insideBgEffectBehavior) {
                    float radius = 10.0f;
                    for (const auto& a : attrs) {
                        if (a.name == "Radius") {
                            const std::string_view v = Trim(a.value);
                            if (!v.empty() && v.front() == '{') {
                                (void)emitPropertyBindingIfValid(PropId_EffectBlurRadius, v);
                            } else {
                                radius = std::max(0.0f, ParseFloat32(v, 10.0f));
                            }
                        }
                    }
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(4)});
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                        MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f)});
                    if (!selfClosing) {
                        (void)pushNonVisualScope();
                    }
                    continue;
                }
            }

            // 2c-path: Structured <PathGeometry> -> <PathFigure> -> <LineSegment>/<BezierSegment>/<ArcSegment>
            if (shortTag == "PathFigure" || shortTag == "LineSegment" || shortTag == "PolyLineSegment" ||
                shortTag == "BezierSegment" || shortTag == "QuadraticBezierSegment" || shortTag == "ArcSegment") {
                XmlScopeEntry* geomOwner = nullptr;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) break;
                    if ((it->kind == XmlScopeKind::TransparentContainer && it->targetPropId == PropId_PathData) ||
                        (it->kind == XmlScopeKind::KeyedResourceItem &&
                         (StripXmlPrefix(it->tagName) == "PathGeometry" || StripXmlPrefix(it->tagName) == "Geometry"))) {
                        geomOwner = &(*it);
                        break;
                    }
                }
                if (geomOwner != nullptr) {
                    if (shortTag == "PathFigure") {
                        std::string startPt = "0,0";
                        bool isClosed = false;
                        for (const auto& a : attrs) {
                            if (a.name == "StartPoint") startPt = std::string(Trim(a.value));
                            else if (a.name == "IsClosed") {
                                const std::string_view tv = Trim(a.value);
                                isClosed = EqualsIgnoreCase(tv, "true") || tv == "1";
                            }
                        }
                        if (!geomOwner->accumulatedText.empty()) geomOwner->accumulatedText.push_back(' ');
                        geomOwner->accumulatedText += "M " + startPt;
                        if (!selfClosing) {
                            XmlScopeEntry figScope{};
                            figScope.kind = XmlScopeKind::NonVisualSkip;
                            figScope.tagName = isClosed ? "PathFigure:Closed" : "PathFigure";
                            scopeStack.push_back(std::move(figScope));
                            ++nonVisualDepth;
                        } else if (isClosed) {
                            geomOwner->accumulatedText += " Z";
                        }
                        continue;
                    } else if (shortTag == "LineSegment") {
                        for (const auto& a : attrs) {
                            if (a.name == "Point" && !Trim(a.value).empty()) {
                                geomOwner->accumulatedText += " L " + std::string(Trim(a.value));
                            }
                        }
                        if (!selfClosing) (void)pushNonVisualScope();
                        continue;
                    } else if (shortTag == "PolyLineSegment") {
                        for (const auto& a : attrs) {
                            if (a.name == "Points" && !Trim(a.value).empty()) {
                                geomOwner->accumulatedText += " L " + std::string(Trim(a.value));
                            }
                        }
                        if (!selfClosing) (void)pushNonVisualScope();
                        continue;
                    } else if (shortTag == "BezierSegment") {
                        std::string p1 = "0,0", p2 = "0,0", p3 = "0,0";
                        for (const auto& a : attrs) {
                            if (a.name == "Point1") p1 = std::string(Trim(a.value));
                            else if (a.name == "Point2") p2 = std::string(Trim(a.value));
                            else if (a.name == "Point3") p3 = std::string(Trim(a.value));
                        }
                        geomOwner->accumulatedText += " C " + p1 + " " + p2 + " " + p3;
                        if (!selfClosing) (void)pushNonVisualScope();
                        continue;
                    } else if (shortTag == "QuadraticBezierSegment") {
                        std::string p1 = "0,0", p2 = "0,0";
                        for (const auto& a : attrs) {
                            if (a.name == "Point1") p1 = std::string(Trim(a.value));
                            else if (a.name == "Point2") p2 = std::string(Trim(a.value));
                        }
                        geomOwner->accumulatedText += " Q " + p1 + " " + p2;
                        if (!selfClosing) (void)pushNonVisualScope();
                        continue;
                    } else if (shortTag == "ArcSegment") {
                        std::string pt = "0,0", sz = "1,1", rot = "0";
                        int largeArc = 0, sweep = 0;
                        for (const auto& a : attrs) {
                            if (a.name == "Point") pt = std::string(Trim(a.value));
                            else if (a.name == "Size") sz = std::string(Trim(a.value));
                            else if (a.name == "RotationAngle") rot = std::string(Trim(a.value));
                            else if (a.name == "IsLargeArc") largeArc = EqualsIgnoreCase(Trim(a.value), "true") ? 1 : 0;
                            else if (a.name == "SweepDirection") sweep = EqualsIgnoreCase(Trim(a.value), "Clockwise") ? 1 : 0;
                        }
                        geomOwner->accumulatedText += " A " + sz + " " + rot + " " +
                            std::to_string(largeArc) + " " + std::to_string(sweep) + " " + pt;
                        if (!selfClosing) (void)pushNonVisualScope();
                        continue;
                    }
                }
            }

            // 2c-shader: <noesis:PlasmaBrush/> / <noesis:ConicGradientBrush/> / <noesis:WavesBrush/> inside <noesis:Brush.Shader>
            if (shortTag == "PlasmaBrush" || shortTag == "ConicGradientBrush" ||
                shortTag == "WavesBrush" || shortTag == "MonochromeBrush") {
                XmlScopeEntry* brushOwner = nullptr;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) break;
                    if (it->kind == XmlScopeKind::BrushPropertyElement) {
                        brushOwner = &(*it);
                        break;
                    }
                }
                if (brushOwner != nullptr && !brushOwner->brushAssigned) {
                    GradientBrushDesc gd{};
                    gd.kind = (shortTag == "PlasmaBrush" || shortTag == "ConicGradientBrush") ? GradientBrushKind::Radial : GradientBrushKind::Linear;
                    gd.startPoint = Point{0.15f, 0.1f};
                    gd.endPoint = Point{0.95f, 0.9f};
                    gd.center = Point{0.55f, 0.5f};
                    gd.gradientOrigin = Point{0.4f, 0.4f};
                    gd.radiusX = 0.75f;
                    gd.radiusY = 0.75f;
                    gd.stops.push_back(GradientStop{0.0f, Color{0.52f, 0.96f, 1.0f, 0.85f}});
                    gd.stops.push_back(GradientStop{0.45f, Color{0.01f, 0.74f, 1.0f, 0.55f}});
                    gd.stops.push_back(GradientStop{1.0f, Color{0.05f, 0.22f, 0.36f, 0.05f}});
                    BakeGradientBrushRamp(gd);
                    const std::string spec = SerializeGradientBrushSpec(gd);
                    (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                    AxbInstruction setInst{};
                    setInst.opcode = AxbOpcode::SetStaticProperty;
                    setInst.propId = brushOwner->targetPropId;
                    setInst.literalValue = value::FromString(spec);
                    pkg.instructions.push_back(std::move(setInst));
                    brushOwner->brushAssigned = true;
                }
                if (!selfClosing) (void)pushNonVisualScope();
                continue;
            }

            // 2c. P1-5: Active <UIElement.Effect> (<BlurEffect/> / <DropShadowEffect/> / <PixelateEffect/>)
            if (parentScope.kind == XmlScopeKind::EffectPropertyElement &&
                (shortTag == "BlurEffect" || shortTag == "DropShadowEffect" || shortTag == "PixelateEffect")) {
                if (shortTag == "PixelateEffect") {
                    float pxSize = 6.0f;
                    for (const auto& a : attrs) {
                        if (a.name == "Size") {
                            const std::string_view v = Trim(a.value);
                            if (!v.empty() && v.front() == '{') {
                                (void)emitPropertyBindingIfValid(PropId_EffectBlurRadius, v);
                            } else {
                                pxSize = std::max(1.0f, ParseFloat32(v, 6.0f));
                            }
                        }
                    }
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(3)});
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                        MakeFloat4InlineFast(pxSize, -1.0f, 0.0f, 1.0f)});
                    if (!selfClosing) (void)pushNonVisualScope();
                    continue;
                }
                if (shortTag == "BlurEffect") {
                    float radius = 5.0f;
                    for (const auto& a : attrs) {
                        if (a.name == "Radius") {
                            const std::string_view v = Trim(a.value);
                            if (!v.empty() && v.front() == '{') {
                                (void)emitPropertyBindingIfValid(PropId_EffectBlurRadius, v);
                            } else {
                                radius = std::max(0.0f, ParseFloat32(v, 5.0f));
                            }
                        }
                    }
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(1)});
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                        MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f)});
                } else {
                    float blurRadius = 5.0f;
                    float shadowDepth = 5.0f;
                    float directionDeg = 315.0f;
                    float opacity = 1.0f;
                    Color shadowCol{0.0f, 0.0f, 0.0f, 1.0f};
                    for (const auto& a : attrs) {
                        const std::string_view v = Trim(a.value);
                        if (v.empty()) continue;
                        if (v.front() == '{') {
                            if (a.name == "BlurRadius") (void)emitPropertyBindingIfValid(PropId_EffectBlurRadius, v);
                            else if (a.name == "ShadowDepth") (void)emitPropertyBindingIfValid(PropId_EffectShadowDepth, v);
                            else if (a.name == "Color") {
                                const std::string_view rk = ExtractMarkupResourceKey(v);
                                if (!rk.empty()) {
                                    if (const UnifiedValue* rv = findLocalOrGlobalResource(rk)) {
                                        if (auto cOpt = value::TryGetColor(*rv)) shadowCol = *cOpt;
                                    }
                                } else {
                                    (void)emitPropertyBindingIfValid(PropId_EffectColor, v);
                                }
                            }
                            continue;
                        }
                        if (a.name == "BlurRadius") blurRadius = std::max(0.0f, ParseFloat32(v, 5.0f));
                        else if (a.name == "ShadowDepth") shadowDepth = std::max(0.0f, ParseFloat32(v, 5.0f));
                        else if (a.name == "Direction") directionDeg = ParseFloat32(v, 315.0f);
                        else if (a.name == "Opacity") opacity = std::clamp(ParseFloat32(v, 1.0f), 0.0f, 1.0f);
                        else if (a.name == "Color") shadowCol = ParseXamlColorLiteral(v);
                    }
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(2)});
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                        MakeFloat4InlineFast(blurRadius, shadowDepth, directionDeg, opacity)});
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectColor, 0, 0, 0, 0,
                        value::FromColor(shadowCol)});
                }
                if (!selfClosing) {
                    (void)pushNonVisualScope();
                }
                continue;
            }

            // 3. Keyed ResourceDictionary items (Brushes, Colors, Doubles, Strings, ImageSource/BitmapImage, SVG Geometries, Effects)
            if (parentScope.kind == XmlScopeKind::ResourceContainer) {
                if (!xKey.empty()) {
                    if (shortTag == "BlurEffect") {
                        float radius = 5.0f;
                        for (const auto& a : attrs) {
                            if (a.name == "Radius" && (a.value.empty() || a.value.front() != '{')) {
                                radius = std::max(0.0f, ParseFloat32(a.value, 5.0f));
                            }
                        }
                        pkg.resources.emplace_back(
                            std::string(xKey),
                            MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f));
                    } else if (shortTag == "DropShadowEffect") {
                        float blurR = 5.0f, depth = 5.0f, dir = 315.0f, op = 1.0f;
                        for (const auto& a : attrs) {
                            if (!a.value.empty() && a.value.front() == '{') continue;
                            if (a.name == "BlurRadius") blurR = std::max(0.0f, ParseFloat32(a.value, 5.0f));
                            else if (a.name == "ShadowDepth") depth = std::max(0.0f, ParseFloat32(a.value, 5.0f));
                            else if (a.name == "Direction") dir = ParseFloat32(a.value, 315.0f);
                            else if (a.name == "Opacity") op = std::clamp(ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                        }
                        pkg.resources.emplace_back(
                            std::string(xKey),
                            MakeFloat4InlineFast(blurR, depth, dir, op));
                    } else if (shortTag == "SolidColorBrush") {
                        bool assignedColor = false;
                        float brushOpacity = 1.0f;
                        bool hasOpacity = false;
                        std::string_view colorAttr;
                        for (const auto& a : attrs) {
                            if (a.name == "Color") {
                                colorAttr = Trim(a.value);
                            } else if (a.name == "Opacity") {
                                brushOpacity = std::clamp(ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                                hasOpacity = true;
                            }
                        }
                        if (!colorAttr.empty()) {
                            Color c{0.0f, 0.0f, 0.0f, 1.0f};
                            bool okColor = false;
                            if (colorAttr.front() == '{') {
                                const std::string_view refKey = ExtractMarkupResourceKey(colorAttr);
                                if (!refKey.empty()) {
                                    if (const UnifiedValue* rv = findLocalOrGlobalResource(refKey)) {
                                        if (const auto cOpt = value::TryGetColor(*rv)) {
                                            c = *cOpt;
                                            okColor = true;
                                        } else if (const auto* sv = std::get_if<StringValue>(rv)) {
                                            c = ParseXamlColorLiteral(sv->view());
                                            okColor = true;
                                        }
                                    }
                                }
                            } else {
                                c = ParseXamlColorLiteral(colorAttr);
                                okColor = true;
                            }
                            if (okColor) {
                                if (hasOpacity) {
                                    c.a *= brushOpacity;
                                }
                                UpsertPackageResource(pkg, xKey, value::FromColor(c));
                                assignedColor = true;
                            }
                        }
                        if (!selfClosing) {
                            XmlScopeEntry itemScope{};
                            itemScope.kind = XmlScopeKind::KeyedResourceItem;
                            itemScope.tagName = std::string(tagName);
                            itemScope.resourceKey = std::string(xKey);
                            itemScope.brushAssigned = assignedColor;
                            scopeStack.push_back(std::move(itemScope));
                            ++nonVisualDepth;
                            continue;
                        }
                    } else if (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush") {
                        GradientBrushDesc gd = parseGradientBrushHeader(shortTag);
                        if (selfClosing) {
                            gd.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                            gd.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                            BakeGradientBrushRamp(gd);
                            const std::string spec = SerializeGradientBrushSpec(gd);
                            (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                            UpsertPackageResource(pkg, xKey, value::FromString(spec));
                            continue;
                        }
                        XmlScopeEntry itemScope{};
                        itemScope.kind = XmlScopeKind::KeyedResourceItem;
                        itemScope.tagName = std::string(tagName);
                        itemScope.resourceKey = std::string(xKey);
                        itemScope.brushAssigned = true;
                        scopeStack.push_back(std::move(itemScope));
                        ++nonVisualDepth;

                        ActiveGradientBrushScope gs{};
                        gs.desc = std::move(gd);
                        gs.owner = GradientScopeOwner::KeyedResource;
                        gs.scopeDepth = scopeStack.size();
                        gs.resourceKey = std::string(xKey);
                        gradientStack.push_back(std::move(gs));
                        continue;
                    } else if (shortTag == "Color") {
                        bool hasChannelAttr = false;
                        std::uint8_t ca = 255u, cr = 0u, cg = 0u, cb = 0u;
                        for (const auto& a : attrs) {
                            if (a.name == "A") { ca = static_cast<std::uint8_t>(std::clamp(ParseFloat32(a.value, 255.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                            else if (a.name == "R") { cr = static_cast<std::uint8_t>(std::clamp(ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                            else if (a.name == "G") { cg = static_cast<std::uint8_t>(std::clamp(ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                            else if (a.name == "B") { cb = static_cast<std::uint8_t>(std::clamp(ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                        }
                        if (hasChannelAttr) {
                            UpsertPackageResource(
                                pkg,
                                xKey,
                                value::FromColor(Color{static_cast<float>(cr) / 255.0f, static_cast<float>(cg) / 255.0f, static_cast<float>(cb) / 255.0f, static_cast<float>(ca) / 255.0f}));
                        }
                        if (!selfClosing) {
                            XmlScopeEntry itemScope{};
                            itemScope.kind = XmlScopeKind::KeyedResourceItem;
                            itemScope.tagName = std::string(tagName);
                            itemScope.resourceKey = std::string(xKey);
                            itemScope.brushAssigned = hasChannelAttr;
                            scopeStack.push_back(std::move(itemScope));
                            ++nonVisualDepth;
                            continue;
                        }
                    } else if (shortTag == "ImageBrush" || shortTag == "BitmapImage" || shortTag == "ImageSource") {
                        const bool isImgBrushItem = (shortTag == "ImageBrush");
                        std::uint8_t ibStretch = 1u;
                        if (isImgBrushItem) {
                            for (const auto& a : attrs) {
                                if (a.name == "Stretch") {
                                    ibStretch = ParseImageBrushStretchCode(a.value);
                                }
                            }
                        }
                        bool assignedUri = false;
                        for (const auto& a : attrs) {
                            if ((a.name == "ImageSource" || a.name == "UriSource" || a.name == "Source") &&
                                !Trim(a.value).empty()) {
                                const std::string_view rawU = Trim(a.value);
                                if (rawU.front() == '{') {
                                    const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                                    if (!refKey.empty()) {
                                        if (const UnifiedValue* rv = findLocalOrGlobalResource(refKey)) {
                                            if (isImgBrushItem) {
                                                if (const auto* sv = std::get_if<StringValue>(rv)) {
                                                    UpsertPackageResource(
                                                        pkg,
                                                        xKey,
                                                        value::FromString(EncodeImageBrushSpec(
                                                            ibStretch,
                                                            StripImageBrushSpecPrefix(sv->view()))));
                                                } else {
                                                    UpsertPackageResource(pkg, xKey, *rv);
                                                }
                                            } else {
                                                UpsertPackageResource(pkg, xKey, *rv);
                                            }
                                            assignedUri = true;
                                            break;
                                        }
                                    }
                                } else {
                                    const std::string decoded = DecodeXamlXmlEntities(rawU);
                                    UpsertPackageResource(
                                        pkg,
                                        xKey,
                                        value::FromString(isImgBrushItem
                                            ? EncodeImageBrushSpec(ibStretch, StripImageBrushSpecPrefix(decoded))
                                            : decoded));
                                    assignedUri = true;
                                    break;
                                }
                            }
                        }
                        if (!selfClosing) {
                            XmlScopeEntry itemScope{};
                            itemScope.kind = XmlScopeKind::KeyedResourceItem;
                            itemScope.tagName = std::string(tagName);
                            itemScope.resourceKey = std::string(xKey);
                            itemScope.brushAssigned = assignedUri;
                            itemScope.isImageBrushScope = isImgBrushItem;
                            itemScope.imageBrushStretch = ibStretch;
                            scopeStack.push_back(std::move(itemScope));
                            ++nonVisualDepth;
                            continue;
                        }
                    } else if (shortTag == "Geometry" || shortTag == "StreamGeometry" || shortTag == "PathGeometry") {
                        bool handledAttr = false;
                        for (const auto& a : attrs) {
                            if (a.name == "Figures" || a.name == "Data") {
                                const std::string decoded = DecodeXamlXmlEntities(a.value);
                                ensurePathBakedIntoPackage(decoded);
                                UpsertPackageResource(pkg, xKey, value::FromString(decoded));
                                handledAttr = true;
                                break;
                            }
                        }
                        if (!selfClosing && !handledAttr) {
                            XmlScopeEntry itemScope{};
                            itemScope.kind = XmlScopeKind::KeyedResourceItem;
                            itemScope.tagName = std::string(tagName);
                            itemScope.resourceKey = std::string(xKey);
                            scopeStack.push_back(std::move(itemScope));
                            ++nonVisualDepth;
                            continue;
                        }
                    } else if (shortTag == "BooleanToVisibilityConverter") {
                        UpsertPackageResource(pkg, xKey, value::FromString("__aero_builtin_conv__:BooleanToVisibility"));
                        if (!selfClosing) {
                            (void)pushNonVisualScope();
                        }
                        continue;
                    } else if (shortTag == "InverseBooleanToVisibilityConverter") {
                        UpsertPackageResource(pkg, xKey, value::FromString("__aero_builtin_conv__:InverseBooleanToVisibility"));
                        if (!selfClosing) {
                            (void)pushNonVisualScope();
                        }
                        continue;
                    } else if (shortTag == "AlternationConverter" && !selfClosing) {
                        std::size_t innerEnd = 0u, afterClose = 0u;
                        if (FindMatchingClosingTag(src, pos, tagName, innerEnd, afterClose)) {
                            const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                            std::string joined = "__aero_alt_conv__:";
                            bool firstC = true;
                            std::size_t scan = 0u;
                            while (scan < innerXml.size()) {
                                const std::size_t cp = innerXml.find("Color=", scan);
                                if (cp == std::string_view::npos) break;
                                std::size_t q = cp + 6u;
                                if (q < innerXml.size() && (innerXml[q] == '"' || innerXml[q] == '\'')) {
                                    const char qc = innerXml[q++];
                                    const std::size_t qe = innerXml.find(qc, q);
                                    if (qe != std::string_view::npos) {
                                        if (!firstC) joined.push_back('|');
                                        joined.append(innerXml.substr(q, qe - q));
                                        firstC = false;
                                        scan = qe + 1u;
                                        continue;
                                    }
                                }
                                scan = cp + 6u;
                            }
                            UpsertPackageResource(pkg, xKey, value::FromString(joined));
                            pos = afterClose;
                            continue;
                        }
                    } else if (shortTag == "GradientStopCollection" && !selfClosing) {
                        std::size_t innerEnd = 0u, afterClose = 0u;
                        if (FindMatchingClosingTag(src, pos, tagName, innerEnd, afterClose)) {
                            const std::string_view innerXml = src.substr(pos, innerEnd - pos);
                            std::string synthXml = "<StackPanel>";
                            synthXml.append(innerXml);
                            synthXml += "</StackPanel>";
                            const std::string resTplKey = "__aero_visual_res__:" + std::string(xKey);
                            AeroCompileInput subIn{};
                            subIn.xamlPath = resTplKey;
                            subIn.xamlContent = std::move(synthXml);
                            subIn.externalXamlResolver = input.externalXamlResolver;
                            subIn.resourceSearchRoot = input.resourceSearchRoot;
                            subIn.resourceSearchPaths = input.resourceSearchPaths;
                            subIn.autoMergeGlobalResources = false;
                            AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                            if (subRes.succeeded) {
                                AxbDataTemplate dt{};
                                dt.key = resTplKey;
                                dt.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                                AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
                                pkg.dataTemplates.push_back(std::move(dt));
                                UpsertPackageResource(pkg, xKey, value::FromString(resTplKey));
                            }
                            pos = afterClose;
                            continue;
                        }
                    } else if (!selfClosing &&
                               (shortTag == "Grid" || shortTag == "Border" || shortTag == "StackPanel" ||
                                shortTag == "TransformGroup" || shortTag == "ContentControl" ||
                                shortTag == "Canvas" || shortTag == "WrapPanel" || shortTag == "DockPanel")) {
                        std::size_t innerEnd = 0u, afterClose = 0u;
                        if (FindMatchingClosingTag(src, pos, tagName, innerEnd, afterClose)) {
                            const std::string_view fullElemXml = src.substr(lt, afterClose - lt);
                            const std::string resTplKey = "__aero_visual_res__:" + std::string(xKey);
                            flushDeferredMtsdfJobs();
                            AxbTemplateRegistry::Instance().RegisterResources(pkg);
                            AeroCompileInput subIn{};
                            subIn.xamlPath = resTplKey;
                            subIn.xamlContent = std::string(fullElemXml);
                            subIn.externalXamlResolver = input.externalXamlResolver;
                            subIn.resourceSearchRoot = input.resourceSearchRoot;
                            subIn.resourceSearchPaths = input.resourceSearchPaths;
                            subIn.autoMergeGlobalResources = false;
                            AeroCompileResult subRes = this->CompileInternal(subIn, activeIncludeStack);
                            if (subRes.succeeded) {
                                for (const auto& mp : subRes.axbPackage.mtsdfPaths) {
                                    if (mp.pathHash != 0 && bakedPathHashes.insert(mp.pathHash).second) {
                                        pkg.mtsdfPaths.push_back(mp);
                                    }
                                }
                                AxbDataTemplate dt{};
                                dt.key = resTplKey;
                                dt.templatePackage = std::make_shared<AxbPackage>(std::move(subRes.axbPackage));
                                AxbTemplateRegistry::Instance().RegisterDataTemplate(dt);
                                pkg.dataTemplates.push_back(std::move(dt));
                                UpsertPackageResource(pkg, xKey, value::FromString(resTplKey));
                            }
                            pos = afterClose;
                            continue;
                        }
                    } else if (shortTag == "String" && selfClosing) {
                        UpsertPackageResource(pkg, xKey, value::FromString(""));
                    } else if (!selfClosing &&
                               (shortTag == "Color" || shortTag == "String" ||
                                shortTag == "Double" || shortTag == "Single" ||
                                shortTag == "Int32" || shortTag == "Int64" || shortTag == "Int16" ||
                                shortTag == "Boolean" || shortTag == "FlowDirection" ||
                                shortTag == "FontFamily" || shortTag == "Thickness" ||
                                shortTag == "CornerRadius")) {
                        XmlScopeEntry itemScope{};
                        itemScope.kind = XmlScopeKind::KeyedResourceItem;
                        itemScope.tagName = std::string(tagName);
                        itemScope.resourceKey = std::string(xKey);
                        scopeStack.push_back(std::move(itemScope));
                        ++nonVisualDepth;
                        continue;
                    } else if (tagName.find(':') != std::string_view::npos) {
                        UpsertPackageResource(
                            pkg,
                            xKey,
                            value::FromString("__aero_script_conv__:" + std::string(shortTag)));
                    }
                }
                if (!selfClosing) {
                    XmlScopeEntry childScope{};
                    childScope.kind = XmlScopeKind::NonVisualSkip;
                    childScope.tagName = std::string(tagName);
                    scopeStack.push_back(std::move(childScope));
                    ++nonVisualDepth;
                }
                continue;
            }
            if (shortTag == "BitmapImage" || shortTag == "ImageBrush") {
                XmlScopeEntry* keyedItemOwner = nullptr;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::ResourceContainer || it->kind == XmlScopeKind::VisualElement) {
                        break;
                    }
                    if (it->kind == XmlScopeKind::KeyedResourceItem) {
                        keyedItemOwner = &(*it);
                        break;
                    }
                }
                if (keyedItemOwner != nullptr && !keyedItemOwner->brushAssigned) {
                    const bool isImgBrushItem = keyedItemOwner->isImageBrushScope || (shortTag == "ImageBrush");
                    std::uint8_t ibStretch = keyedItemOwner->imageBrushStretch;
                    if (shortTag == "ImageBrush") {
                        for (const auto& a : attrs) {
                            if (a.name == "Stretch") {
                                ibStretch = ParseImageBrushStretchCode(a.value);
                            }
                        }
                    }
                    for (const auto& a : attrs) {
                        if ((a.name == "UriSource" || a.name == "ImageSource" || a.name == "Source") &&
                            !Trim(a.value).empty()) {
                            const std::string_view rawU = Trim(a.value);
                            if (rawU.front() == '{') {
                                const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                                if (!refKey.empty()) {
                                    if (const UnifiedValue* rv = findLocalOrGlobalResource(refKey)) {
                                        if (isImgBrushItem) {
                                            if (const auto* sv = std::get_if<StringValue>(rv)) {
                                                UpsertPackageResource(
                                                    pkg,
                                                    keyedItemOwner->resourceKey,
                                                    value::FromString(EncodeImageBrushSpec(
                                                        ibStretch,
                                                        StripImageBrushSpecPrefix(sv->view()))));
                                            } else {
                                                UpsertPackageResource(pkg, keyedItemOwner->resourceKey, *rv);
                                            }
                                        } else {
                                            UpsertPackageResource(pkg, keyedItemOwner->resourceKey, *rv);
                                        }
                                        keyedItemOwner->brushAssigned = true;
                                        break;
                                    }
                                }
                            } else {
                                const std::string decoded = DecodeXamlXmlEntities(rawU);
                                UpsertPackageResource(
                                    pkg,
                                    keyedItemOwner->resourceKey,
                                    value::FromString(isImgBrushItem
                                        ? EncodeImageBrushSpec(ibStretch, StripImageBrushSpecPrefix(decoded))
                                        : decoded));
                                keyedItemOwner->brushAssigned = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (shortTag == "VirtualizingStackPanel") {
                // If <VirtualizingStackPanel/> appears inside <ItemsControl.ItemsPanel> on a live visual element
                // (not inside a ResourceDictionary/Style/ControlTemplate), enable PropId_IsVirtualizing on that control!
                bool insideItemsPanel = false;
                bool blockedByTemplateOrRes = false;
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) {
                        break;
                    }
                    if (it->kind == XmlScopeKind::ResourceContainer ||
                        it->kind == XmlScopeKind::KeyedResourceItem) {
                        blockedByTemplateOrRes = true;
                        break;
                    }
                    const std::string_view st = StripXmlPrefix(it->tagName);
                    if (st == "Style" || st == "ControlTemplate" || st == "DataTemplate") {
                        blockedByTemplateOrRes = true;
                        break;
                    }
                    if (EndsWith(st, ".ItemsPanel")) {
                        insideItemsPanel = true;
                    }
                }
                if (insideItemsPanel && !blockedByTemplateOrRes) {
                    AxbInstruction virtInst{};
                    virtInst.opcode = AxbOpcode::SetStaticProperty;
                    virtInst.propId = PropId_IsVirtualizing;
                    virtInst.literalValue = UnifiedValue{true};
                    pkg.instructions.push_back(std::move(virtInst));
                }
            }
            if (parentScope.kind == XmlScopeKind::BrushPropertyElement && !parentScope.brushAssigned) {
                if (shortTag == "SolidColorBrush") {
                    float brushAlphaMul = 1.0f;
                    for (const auto& a : attrs) {
                        if (a.name == "Opacity") {
                            brushAlphaMul = std::clamp(ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                        }
                    }
                    for (const auto& a : attrs) {
                        if (a.name == "Color") {
                            std::string_view resKey{};
                            std::uint8_t resFlags = 0u;
                            if (TryParseResourceMarkupExtension(a.value, resKey, resFlags) && !resKey.empty()) {
                                AxbInstruction resInst{};
                                resInst.opcode = AxbOpcode::SetResourceProperty;
                                resInst.propId = parentScope.targetPropId;
                                resInst.flags = resFlags;
                                resInst.strIdx0 = pkg.InternString(resKey);
                                if (const UnifiedValue* rv = findLocalOrGlobalResource(resKey)) {
                                    resInst.literalValue = *rv;
                                }
                                pkg.instructions.push_back(std::move(resInst));
                                parentScope.brushAssigned = true;
                            } else if (emitPropertyBindingIfValid(parentScope.targetPropId, a.value)) {
                                parentScope.brushAssigned = true;
                            } else if (a.value.empty() || a.value.front() != '{') {
                                Color c = ParseXamlColorLiteral(a.value);
                                const bool splitOp =
                                    (parentScope.targetPropId == PropId_Background ||
                                     parentScope.targetPropId == PropId_BorderBrush) &&
                                    (std::fabs(brushAlphaMul - 1.0f) > 1e-5f);
                                if (!splitOp) {
                                    c.a *= brushAlphaMul;
                                }
                                AxbInstruction setInst{};
                                setInst.opcode = AxbOpcode::SetStaticProperty;
                                setInst.propId = parentScope.targetPropId;
                                setInst.literalValue = value::FromColor(c);
                                pkg.instructions.push_back(std::move(setInst));
                                if (splitOp) {
                                    AxbInstruction opInst{};
                                    opInst.opcode = AxbOpcode::SetStaticProperty;
                                    opInst.propId = (parentScope.targetPropId == PropId_BorderBrush)
                                        ? static_cast<std::uint16_t>(PropId_BorderOpacity)
                                        : static_cast<std::uint16_t>(PropId_BackgroundOpacity);
                                    opInst.literalValue = MakeFloat(brushAlphaMul);
                                    pkg.instructions.push_back(std::move(opInst));
                                }
                                parentScope.brushAssigned = true;
                            } else {
                                if (emitPropertyBindingIfValid(parentScope.targetPropId, a.value)) {
                                    parentScope.brushAssigned = true;
                                }
                            }
                            break;
                        }
                    }
                } else if (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush") {
                    GradientBrushDesc gd = parseGradientBrushHeader(shortTag);
                    parentScope.brushAssigned = true;
                    if (selfClosing) {
                        gd.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                        gd.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                        BakeGradientBrushRamp(gd);
                        const std::string spec = SerializeGradientBrushSpec(gd);
                        (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                        AxbInstruction setInst{};
                        setInst.opcode = AxbOpcode::SetStaticProperty;
                        setInst.propId = parentScope.targetPropId;
                        setInst.literalValue = value::FromString(spec);
                        pkg.instructions.push_back(std::move(setInst));
                        continue;
                    }
                    const std::uint16_t gradTargetPropId = parentScope.targetPropId;
                    XmlScopeEntry gradScope{};
                    gradScope.kind = XmlScopeKind::BrushPropertyElement;
                    gradScope.tagName = std::string(tagName);
                    gradScope.targetPropId = gradTargetPropId;
                    gradScope.brushAssigned = true;
                    scopeStack.push_back(std::move(gradScope));
                    ++nonVisualDepth;

                    ActiveGradientBrushScope gs{};
                    gs.desc = std::move(gd);
                    gs.owner = GradientScopeOwner::PropertyElement;
                    gs.scopeDepth = scopeStack.size();
                    gs.targetPropId = gradTargetPropId;
                    gradientStack.push_back(std::move(gs));
                    continue;
                } else if (shortTag == "BitmapImage" || shortTag == "ImageBrush") {
                    const bool isImgBrush = (shortTag == "ImageBrush") || parentScope.isImageBrushScope;
                    std::uint8_t ibStretch = parentScope.imageBrushStretch;
                    if (shortTag == "ImageBrush") {
                        ibStretch = 1u;
                        for (const auto& a : attrs) {
                            if (a.name == "Stretch") {
                                ibStretch = ParseImageBrushStretchCode(a.value);
                            }
                        }
                    }
                    bool hadDirectUri = false;
                    for (const auto& a : attrs) {
                        if ((a.name == "UriSource" || a.name == "ImageSource" || a.name == "Source") &&
                            !Trim(a.value).empty()) {
                            hadDirectUri = true;
                            break;
                        }
                    }
                    if (!hadDirectUri && !selfClosing && shortTag == "ImageBrush") {
                        XmlScopeEntry ibScope{};
                        ibScope.kind = XmlScopeKind::BrushPropertyElement;
                        ibScope.tagName = std::string(tagName);
                        ibScope.targetPropId = parentScope.targetPropId;
                        ibScope.isImageBrushScope = true;
                        ibScope.imageBrushStretch = ibStretch;
                        scopeStack.push_back(std::move(ibScope));
                        ++nonVisualDepth;
                        continue;
                    }
                    const std::uint16_t destPropId = isImgBrush
                        ? parentScope.targetPropId
                        : ((shortTag == "BitmapImage")
                            ? static_cast<std::uint16_t>(PropId_ImageSource)
                            : parentScope.targetPropId);
                    for (const auto& a : attrs) {
                        if ((a.name == "UriSource" || a.name == "ImageSource" || a.name == "Source") &&
                            !Trim(a.value).empty()) {
                            const std::string_view rawUri = Trim(a.value);
                            std::string_view resK{};
                            std::uint8_t resF = 0u;
                            if (TryParseResourceMarkupExtension(rawUri, resK, resF)) {
                                const UnifiedValue* rv = findLocalOrGlobalResource(resK);
                                const auto* rvStr = rv ? std::get_if<StringValue>(rv) : nullptr;
                                if (isImgBrush && rvStr != nullptr && resF == AxbResourceFlag_StaticResource) {
                                    AxbInstruction setInst{};
                                    setInst.opcode = AxbOpcode::SetStaticProperty;
                                    setInst.propId = destPropId;
                                    setInst.literalValue = value::FromString(
                                        EncodeImageBrushSpec(ibStretch, StripImageBrushSpecPrefix(rvStr->view())));
                                    pkg.instructions.push_back(std::move(setInst));
                                } else {
                                    AxbInstruction rInst{};
                                    rInst.opcode = AxbOpcode::SetResourceProperty;
                                    rInst.propId = destPropId;
                                    rInst.flags = resF;
                                    rInst.strIdx0 = pkg.InternString(resK);
                                    if (rv != nullptr) {
                                        rInst.literalValue = *rv;
                                    }
                                    pkg.instructions.push_back(std::move(rInst));
                                }
                            } else {
                                const std::string decoded = DecodeXamlXmlEntities(rawUri);
                                AxbInstruction setInst{};
                                setInst.opcode = AxbOpcode::SetStaticProperty;
                                setInst.propId = destPropId;
                                setInst.literalValue = value::FromString(
                                    isImgBrush
                                        ? EncodeImageBrushSpec(ibStretch, StripImageBrushSpecPrefix(decoded))
                                        : decoded);
                                pkg.instructions.push_back(std::move(setInst));
                            }
                            for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                                if (it->kind == XmlScopeKind::VisualElement) break;
                                if (it->kind == XmlScopeKind::BrushPropertyElement) {
                                    it->brushAssigned = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
            if (!selfClosing) {
                XmlScopeEntry skipScope{};
                skipScope.kind = (parentScope.kind == XmlScopeKind::BrushPropertyElement)
                    ? XmlScopeKind::BrushPropertyElement
                    : XmlScopeKind::NonVisualSkip;
                skipScope.tagName = std::string(tagName);
                skipScope.targetPropId = parentScope.targetPropId;
                skipScope.brushAssigned = parentScope.brushAssigned;
                skipScope.isImageBrushScope = parentScope.isImageBrushScope;
                skipScope.imageBrushStretch = parentScope.imageBrushStretch;
                scopeStack.push_back(std::move(skipScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Top-level ResourceDictionary root file
        if (!seenRoot && shortTag == "ResourceDictionary") {
            seenRoot = true;
            pkg.rootTypeName = "ResourceDictionary";
            for (const auto& a : attrs) {
                if (a.name == "Source" && !Trim(a.value).empty()) {
                    compileAndMergeExternalDictionary(DecodeXamlXmlEntities(a.value));
                    break;
                }
            }
            AxbInstruction beginInst{};
            beginInst.opcode = AxbOpcode::BeginElement;
            beginInst.strIdx0 = pkg.InternString("ResourceDictionary");
            beginInst.strIdx1 = pkg.InternString("");
            pkg.instructions.push_back(std::move(beginInst));
            if (selfClosing) {
                AxbInstruction endInst{};
                endInst.opcode = AxbOpcode::EndElement;
                pkg.instructions.push_back(std::move(endInst));
            } else {
                XmlScopeEntry resRoot{};
                resRoot.kind = XmlScopeKind::ResourceContainer;
                resRoot.tagName = std::string(tagName);
                scopeStack.push_back(std::move(resRoot));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <ControlTemplate.Triggers> at top level of a ControlTemplate sub-package!
        if (shortTag == "ControlTemplate.Triggers" && scopeStack.empty()) {
            if (!selfClosing) {
                if (ctRootTriggersStyleKey.empty()) {
                    const std::string scopePrefix = !pkg.className.empty()
                        ? pkg.className
                        : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                    ctRootTriggersStyleKey = "__ct_root_triggers_" +
                        scopePrefix + "_" +
                        std::to_string(nextInlineVsmStyleId++);
                }
                const std::size_t depth = pushNonVisualScope();
                ActiveStyleScope ctTrigStyle{};
                ctTrigStyle.def.key = ctRootTriggersStyleKey;
                ctTrigStyle.def.targetTypeName = !pkg.rootTypeName.empty()
                    ? std::string(StripXmlPrefix(pkg.rootTypeName))
                    : std::string("UIElement");
                ctTrigStyle.controlTemplateTriggersDepth = depth;
                ctTrigStyle.scopeDepth = depth;
                styleStack.push_back(std::move(ctTrigStyle));
            }
            continue;
        }

        // Inline <DataTemplate.Triggers> at top level of a DataTemplate sub-package!
        if ((shortTag == "DataTemplate.Triggers" || shortTag == "HierarchicalDataTemplate.Triggers") &&
            scopeStack.empty()) {
            if (!selfClosing) {
                if (dtRootTriggersStyleKey.empty()) {
                    const std::string scopePrefix = !pkg.className.empty()
                        ? pkg.className
                        : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                    dtRootTriggersStyleKey = "__dt_root_triggers_" +
                        scopePrefix + "_" +
                        std::to_string(nextInlineVsmStyleId++);
                }
                const std::size_t depth = pushNonVisualScope();
                ActiveStyleScope dtTrigStyle{};
                dtTrigStyle.def.key = dtRootTriggersStyleKey;
                dtTrigStyle.def.targetTypeName = !pkg.rootTypeName.empty()
                    ? std::string(StripXmlPrefix(pkg.rootTypeName))
                    : std::string("UIElement");
                dtTrigStyle.controlTemplateTriggersDepth = depth;
                dtTrigStyle.scopeDepth = depth;
                styleStack.push_back(std::move(dtTrigStyle));
            }
            continue;
        }

        // Element-level <VisualStateManager.VisualStateGroups>, <Element.Style>, or <Element.Triggers>!
        if ((shortTag == "VisualStateManager.VisualStateGroups" ||
             EndsWith(shortTag, ".Style") ||
             EndsWith(shortTag, ".Triggers")) &&
            !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry& ownerElemScope = scopeStack.back();
                if (ownerElemScope.inlineStyleKey.empty()) {
                    const std::string scopePrefix = !pkg.className.empty()
                        ? pkg.className
                        : (!input.xamlPath.empty() ? input.xamlPath : pkg.rootTypeName);
                    ownerElemScope.inlineStyleKey = "__inline_style_" +
                        scopePrefix + "_" +
                        std::to_string(nextInlineVsmStyleId++);
                    AxbInstruction styleRefInst{};
                    styleRefInst.opcode = AxbOpcode::SetStaticProperty;
                    styleRefInst.propId = PropId_Style;
                    styleRefInst.literalValue = value::FromString(ownerElemScope.inlineStyleKey);
                    pkg.instructions.push_back(std::move(styleRefInst));
                }
                const std::string synthKey = ownerElemScope.inlineStyleKey;
                const std::string ownerType = std::string(StripXmlPrefix(ownerElemScope.tagName));
                const std::string ownerBasedOnKey = ownerElemScope.resourceKey;
                const std::size_t depth = pushNonVisualScope();
                ActiveStyleScope synthStyle{};
                synthStyle.def.key = synthKey;
                synthStyle.def.targetTypeName = ownerType;
                synthStyle.def.basedOnKey = ownerBasedOnKey;
                synthStyle.scopeDepth = depth;
                synthStyle.isInlinePropertyElement = EndsWith(shortTag, ".Style");
                if (shortTag == "Interaction.Triggers") {
                    synthStyle.interactionTriggersDepth = depth;
                }
                styleStack.push_back(std::move(synthStyle));
            }
            continue;
        }

        // Inline <*.ItemTemplate>, <*.ContentTemplate>, or <*.HeaderTemplate>
        if ((EndsWith(shortTag, ".ItemTemplate") ||
             EndsWith(shortTag, ".ContentTemplate") ||
             EndsWith(shortTag, ".HeaderTemplate")) &&
            !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry itScope{};
                itScope.kind = XmlScopeKind::NonVisualSkip;
                itScope.tagName = std::string(tagName);
                itScope.isItemTemplateContainer = true;
                itScope.targetPropId = EndsWith(shortTag, ".ContentTemplate")
                    ? static_cast<std::uint16_t>(PropId_ContentTemplate)
                    : (EndsWith(shortTag, ".HeaderTemplate")
                           ? static_cast<std::uint16_t>(PropId_HeaderTemplate)
                           : static_cast<std::uint16_t>(PropId_ItemTemplate));
                scopeStack.push_back(std::move(itScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <ItemsControl.ItemsPanel><ItemsPanelTemplate>...</ItemsPanelTemplate></ItemsControl.ItemsPanel>
        if (EndsWith(shortTag, ".ItemsPanel") && !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry ipScope{};
                ipScope.kind = XmlScopeKind::NonVisualSkip;
                ipScope.tagName = std::string(tagName);
                ipScope.isItemsPanelContainer = true;
                scopeStack.push_back(std::move(ipScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <ListView.View><GridView>...</GridView></ListView.View>
        if (EndsWith(shortTag, ".View") && !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry vwScope{};
                vwScope.kind = XmlScopeKind::NonVisualSkip;
                vwScope.tagName = std::string(tagName);
                vwScope.isListViewViewContainer = true;
                scopeStack.push_back(std::move(vwScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <Element.ContextMenu><ContextMenu>...</ContextMenu></Element.ContextMenu>
        if (EndsWith(shortTag, ".ContextMenu") && !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry cmScope{};
                cmScope.kind = XmlScopeKind::NonVisualSkip;
                cmScope.tagName = std::string(tagName);
                cmScope.isContextMenuContainer = true;
                scopeStack.push_back(std::move(cmScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Inline <Control.Template><ControlTemplate>...</ControlTemplate></Control.Template>
        if (EndsWith(shortTag, ".Template") && !scopeStack.empty()) {
            if (!selfClosing) {
                XmlScopeEntry ctScope{};
                ctScope.kind = XmlScopeKind::NonVisualSkip;
                ctScope.tagName = std::string(tagName);
                ctScope.isControlTemplateContainer = true;
                scopeStack.push_back(std::move(ctScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Property-element tags (<Type.Property>)
        if (shortTag == "Grid.RowDefinitions" || shortTag == "Grid.ColumnDefinitions" ||
            EndsWith(shortTag, ".Header") ||
            EndsWith(shortTag, ".Content") ||
            EndsWith(shortTag, ".ToolTip") ||
            EndsWith(shortTag, ".Icon") ||
            EndsWith(shortTag, ".Data")) {
            if (!selfClosing) {
                XmlScopeEntry trans{};
                trans.kind = XmlScopeKind::TransparentContainer;
                trans.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Header")) {
                    trans.targetPropId = PropId_Header;
                } else if (EndsWith(shortTag, ".Content")) {
                    trans.targetPropId = PropId_Text;
                } else if (EndsWith(shortTag, ".ToolTip")) {
                    trans.targetPropId = PropId_ToolTipText;
                } else if (EndsWith(shortTag, ".Data")) {
                    trans.targetPropId = PropId_PathData;
                } else {
                    trans.targetPropId = 0;
                }
                scopeStack.push_back(std::move(trans));
            }
            continue;
        }

        if (shortTag == "RowDefinition" || shortTag == "ColumnDefinition") {
            std::string_view lenStr;
            for (const auto& a : attrs) {
                if ((shortTag == "RowDefinition" && a.name == "Height") ||
                    (shortTag == "ColumnDefinition" && a.name == "Width")) {
                    lenStr = a.value;
                    break;
                }
            }
            const auto [unit, val] = ParseGridLength(lenStr);
            AxbInstruction defInst{};
            defInst.opcode = (shortTag == "RowDefinition")
                ? AxbOpcode::AddRowDefinition
                : AxbOpcode::AddColumnDefinition;
            defInst.flags = static_cast<std::uint8_t>(unit);
            defInst.literalValue = MakeFloat(val);
            pkg.instructions.push_back(std::move(defInst));
            if (!selfClosing) {
                XmlScopeEntry trans{};
                trans.kind = XmlScopeKind::TransparentContainer;
                trans.tagName = std::string(tagName);
                trans.targetPropId = 0;
                scopeStack.push_back(std::move(trans));
            }
            continue;
        }

        if (shortTag == "LineBreak") {
            for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                if (it->kind == XmlScopeKind::RunInline || it->kind == XmlScopeKind::VisualElement) {
                    if (it->accumulatedRichText.empty() && !it->accumulatedText.empty()) {
                        it->accumulatedRichText = it->accumulatedText;
                    }
                    it->accumulatedText.push_back('\n');
                    it->accumulatedRichText.push_back('\n');
                    if (!it->inlineRunBindings.empty()) {
                        it->inlineRunFormatTemplate.push_back('\n');
                    }
                    break;
                }
            }
            if (!selfClosing) {
                XmlScopeEntry lbScope{};
                lbScope.kind = XmlScopeKind::RunInline;
                lbScope.tagName = "LineBreak";
                scopeStack.push_back(std::move(lbScope));
            }
            continue;
        }

        if (shortTag == "Run" || shortTag == "Span" || shortTag == "Bold" ||
            shortTag == "Italic" || shortTag == "Underline") {
            std::string runText;
            std::string_view rawRunTextAttr;
            bool attrBold = false;
            bool attrItalic = false;
            for (const auto& a : attrs) {
                if (a.name == "Text") {
                    rawRunTextAttr = a.value;
                    runText = DecodeXamlXmlEntities(a.value);
                } else if (a.name == "FontWeight") {
                    const std::string_view fw = Trim(a.value);
                    if (EqualsIgnoreCase(fw, "Bold") || EqualsIgnoreCase(fw, "SemiBold") ||
                        EqualsIgnoreCase(fw, "DemiBold") || EqualsIgnoreCase(fw, "ExtraBold") ||
                        EqualsIgnoreCase(fw, "Black") || EqualsIgnoreCase(fw, "Heavy")) {
                        attrBold = true;
                    }
                } else if (a.name == "FontStyle") {
                    const std::string_view fs = Trim(a.value);
                    if (EqualsIgnoreCase(fs, "Italic") || EqualsIgnoreCase(fs, "Oblique")) {
                        attrItalic = true;
                    }
                }
            }
            const bool isBold = (shortTag == "Bold") || attrBold;
            const bool isItalic = (shortTag == "Italic") || attrItalic;
            const bool isUnderline = (shortTag == "Underline");
            const ParsedBindingClause parsedRunBind = ParseBindingMarkupExpression(rawRunTextAttr);
            if (parsedRunBind.isValidBinding) {
                for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                    if (it->kind == XmlScopeKind::VisualElement) {
                        if (it->inlineRunBindings.empty() && !it->accumulatedText.empty()) {
                            for (char ch : it->accumulatedText) {
                                if (ch == '{' || ch == '}') it->inlineRunFormatTemplate.push_back(ch);
                                it->inlineRunFormatTemplate.push_back(ch);
                            }
                        }
                        const std::size_t slotIdx = it->inlineRunBindings.size();
                        it->inlineRunFormatTemplate += "{" + std::to_string(slotIdx) + "}";

                        AxbCompiledBindingChain rChain{};
                        rChain.rawPath = std::string(parsedRunBind.pathPart);
                        rChain.targetPropertyId = PropId_Text;
                        rChain.mode = parsedRunBind.mode;
                        rChain.elementName = std::string(parsedRunBind.elementName);
                        rChain.relativeAncestorType = parsedRunBind.relativeAncestorType;
                        rChain.relativeSelf = parsedRunBind.relativeSelf || parsedRunBind.relativeTemplatedParent;
                        rChain.converter = parsedRunBind.converter;
                        rChain.converterKey = parsedRunBind.converterKey;
                        rChain.converterParameter = parsedRunBind.converterParameter;
                        rChain.stringFormat = parsedRunBind.stringFormat;
                        if (!parsedRunBind.fallbackValue.empty()) {
                            rChain.fallbackValue = ParseLiteralPropertyValue(PropId_Text, parsedRunBind.fallbackValue);
                            it->accumulatedText += DecodeXamlXmlEntities(parsedRunBind.fallbackValue);
                        }
                        resolveConverterResource(rChain.converter, rChain.converterKey);
                        const bool hasRunElemSrc = !rChain.elementName.empty() ||
                                                   !rChain.relativeAncestorType.empty() ||
                                                   rChain.relativeSelf;
                        const bool keepRunAttached = parsedRunBind.isAttachedPropertyPath ||
                            (hasRunElemSrc &&
                             parsedRunBind.pathPart.find('.') != std::string_view::npos &&
                             !IsInstanceSubHopPropertyPath(parsedRunBind.pathPart) && ResolveMappedSetterPropertyId(parsedRunBind.pathPart) != kUnmappedPropId);
                        splitPathIntoHops(parsedRunBind.pathPart, rChain.hops, keepRunAttached);
                        it->inlineRunBindings.push_back(std::move(rChain));
                        break;
                    }
                }
                if (!selfClosing) {
                    XmlScopeEntry runScope{};
                    runScope.kind = XmlScopeKind::RunInline;
                    runScope.tagName = std::string(shortTag);
                    scopeStack.push_back(std::move(runScope));
                }
                continue;
            }
            if (selfClosing) {
                if (!runText.empty()) {
                    std::string richSeg = runText;
                    const bool isRichTag = isBold || isItalic || isUnderline;
                    if (isBold) richSeg = "[b]" + richSeg + "[/b]";
                    if (isItalic) richSeg = "[i]" + richSeg + "[/i]";
                    if (isUnderline) richSeg = "[u]" + richSeg + "[/u]";
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::RunInline || it->kind == XmlScopeKind::VisualElement) {
                            bool insertSpace = false;
                            if (!it->accumulatedText.empty()) {
                                const char prevCh = it->accumulatedText.back();
                                const char nextCh = runText.front();
                                const bool prevGlue = (prevCh == ' ' || prevCh == '\n' || prevCh == '\t' ||
                                                       prevCh == '"' || prevCh == '\'' || prevCh == '(' || prevCh == '[' || prevCh == '=');
                                const bool nextGlue = (nextCh == ' ' || nextCh == '\n' || nextCh == '\t' ||
                                                       nextCh == '"' || nextCh == '\'' || nextCh == ')' || nextCh == ']' ||
                                                       nextCh == '.' || nextCh == ',' || nextCh == ':' || nextCh == ';');
                                if (!prevGlue && !nextGlue) {
                                    insertSpace = true;
                                }
                            }
                            if (insertSpace) {
                                it->accumulatedText.push_back(' ');
                                if (!it->accumulatedRichText.empty()) {
                                    it->accumulatedRichText.push_back(' ');
                                }
                            }
                            if (it->accumulatedRichText.empty() && !it->accumulatedText.empty()) {
                                it->accumulatedRichText = it->accumulatedText;
                            }
                            if (!it->inlineRunBindings.empty()) {
                                if (insertSpace) it->inlineRunFormatTemplate.push_back(' ');
                                for (char ch : runText) {
                                    if (ch == '{' || ch == '}') it->inlineRunFormatTemplate.push_back(ch);
                                    it->inlineRunFormatTemplate.push_back(ch);
                                }
                            }
                            it->accumulatedText += runText;
                            it->accumulatedRichText += richSeg;
                            if (isRichTag) {
                                it->hasInlineRichFormatting = true;
                            }
                            break;
                        }
                    }
                }
            } else {
                XmlScopeEntry runScope{};
                runScope.kind = XmlScopeKind::RunInline;
                if (isBold && isItalic) {
                    runScope.tagName = "RunBoldItalic";
                } else if (isBold && shortTag != "Bold") {
                    runScope.tagName = "RunBold";
                } else if (isItalic && shortTag != "Italic") {
                    runScope.tagName = "RunItalic";
                } else {
                    runScope.tagName = std::string(shortTag);
                }
                runScope.accumulatedRichText = runText;
                runScope.accumulatedText = std::move(runText);
                scopeStack.push_back(std::move(runScope));
            }
            continue;
        }

        if (shortTag == "ResourceDictionary" ||
            shortTag == "ResourceDictionary.MergedDictionaries" ||
            EndsWith(shortTag, ".MergedDictionaries") ||
            EndsWith(shortTag, ".Resources")) {
            if (shortTag == "ResourceDictionary") {
                for (const auto& a : attrs) {
                    if (a.name == "Source" && !Trim(a.value).empty()) {
                        compileAndMergeExternalDictionary(DecodeXamlXmlEntities(a.value));
                        break;
                    }
                }
            }
            if (!selfClosing) {
                XmlScopeEntry resScope{};
                resScope.kind = XmlScopeKind::ResourceContainer;
                resScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(resScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".Transform3D") || EndsWith(shortTag, ".Projection")) {
            if (!selfClosing) {
                XmlScopeEntry tf3Scope{};
                tf3Scope.kind = XmlScopeKind::Transform3DPropertyElement;
                tf3Scope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(tf3Scope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".Effect")) {
            if (!selfClosing) {
                XmlScopeEntry fxScope{};
                fxScope.kind = XmlScopeKind::EffectPropertyElement;
                fxScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(fxScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".RenderTransform") || EndsWith(shortTag, ".LayoutTransform")) {
            if (!selfClosing) {
                XmlScopeEntry tfScope{};
                tfScope.kind = XmlScopeKind::TransformPropertyElement;
                tfScope.tagName = std::string(tagName);
                tfScope.targetPropId = EndsWith(shortTag, ".LayoutTransform")
                    ? PropId_LayoutTransform
                    : PropId_RenderTransform;
                tfScope.accumulatedTransform = Matrix2D::Identity();
                scopeStack.push_back(std::move(tfScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".Background") ||
            EndsWith(shortTag, ".Fill") ||
            EndsWith(shortTag, ".Foreground") ||
            EndsWith(shortTag, ".BorderBrush") ||
            EndsWith(shortTag, ".Stroke") ||
            EndsWith(shortTag, ".Source")) {
            if (!selfClosing) {
                XmlScopeEntry brushScope{};
                brushScope.kind = XmlScopeKind::BrushPropertyElement;
                brushScope.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Foreground")) {
                    brushScope.targetPropId = PropId_Foreground;
                } else if (EndsWith(shortTag, ".BorderBrush") ||
                           EndsWith(shortTag, ".Stroke")) {
                    brushScope.targetPropId = PropId_BorderBrush;
                } else if (EndsWith(shortTag, ".Source")) {
                    brushScope.targetPropId = PropId_ImageSource;
                } else {
                    brushScope.targetPropId = PropId_Background;
                }
                scopeStack.push_back(std::move(brushScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (shortTag.find('.') != std::string_view::npos) {
            if (!selfClosing) {
                const std::uint16_t mappedProp = ResolveMappedSetterPropertyId(shortTag);
                XmlScopeEntry propScope{};
                propScope.kind = (mappedProp != kUnmappedPropId)
                    ? XmlScopeKind::BindingPropertyElement
                    : XmlScopeKind::NonVisualSkip;
                propScope.tagName = std::string(tagName);
                propScope.targetPropId = mappedProp;
                scopeStack.push_back(std::move(propScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (isNonVisualElementTag(shortTag)) {
            if (!selfClosing) {
                (void)pushNonVisualScope();
            }
            continue;
        }

        const bool isRootElement = !seenRoot;
        if (!seenRoot) {
            seenRoot = true;
            pkg.rootTypeName = std::string(tagName);
            if (shortTag == "Application") {
                for (const auto& a : attrs) {
                    if (a.name == "StartupUri") {
                        pkg.startupUri = DecodeXamlXmlEntities(Trim(a.value));
                    }
                }
            }
            if (activeIncludeStack.size() <= 1u) {
                if (!effectiveXamlPath.empty()) {
                    const std::filesystem::path xDir =
                        std::filesystem::path(NormalizeSlashes(effectiveXamlPath)).parent_path();
                    if (!xDir.empty()) {
                        UiResourceContext::Current().AddXamlSearchDirectory(xDir.string());
                    }
                }
                if (!input.resourceSearchRoot.empty()) {
                    UiResourceContext::Current().AddXamlSearchDirectory(input.resourceSearchRoot);
                }
                for (const auto& sp : input.resourceSearchPaths) {
                    if (!sp.empty()) {
                        UiResourceContext::Current().AddXamlSearchDirectory(sp);
                    }
                }
            }
            if (input.autoMergeGlobalResources && activeIncludeStack.size() <= 1u &&
                !effectiveXamlPath.empty()) {
                const std::filesystem::path xamlDir =
                    std::filesystem::path(NormalizeSlashes(effectiveXamlPath)).parent_path();
                const std::string globalRes = DiscoverGlobalResourcesPathForDirectory(xamlDir);
                if (!globalRes.empty()) {
                    compileAndMergeExternalDictionary(globalRes);
                }
            }
        }

        if (!xName.empty()) {
            AxbNamedControlSlot slot{};
            slot.name = std::string(xName);
            slot.typeName = std::string(tagName);
            slot.typeSymbolId = rs_sem::stableTypeId("Aero.UI." + std::string(shortTag));
            pkg.namedControls.push_back(std::move(slot));
        }

        const bool isDirectHeaderChild = !scopeStack.empty() &&
            scopeStack.back().kind == XmlScopeKind::TransparentContainer &&
            EndsWith(StripXmlPrefix(scopeStack.back().tagName), ".Header");

        const bool isInlinePrimitiveVisualItem =
            (shortTag == "Double" || shortTag == "Single" || shortTag == "Int32" ||
             shortTag == "Int64" || shortTag == "Boolean" || shortTag == "String");
        AxbInstruction beginInst{};
        beginInst.opcode = AxbOpcode::BeginElement;
        beginInst.flags = isDirectHeaderChild ? 0x01u : 0x00u;
        beginInst.strIdx0 = pkg.InternString(isInlinePrimitiveVisualItem ? std::string_view("TextBlock") : tagName);
        beginInst.strIdx1 = pkg.InternString(xName);
        pkg.instructions.push_back(std::move(beginInst));

        bool hasExplicitWidth = false;
        bool hasExplicitHeight = false;
        std::string attachedStyleKey;
        for (const auto& a : attrs) {
            if (a.name == "Width") hasExplicitWidth = true;
            if (a.name == "Height") hasExplicitHeight = true;
        }

        (void)hasExplicitWidth;
        (void)hasExplicitHeight;
        for (const auto& attr : attrs) {
            if (attr.name == "d:DesignWidth" || attr.name == "d:DesignHeight") {
                continue;
            }
            if (attr.name == "StartupUri" && shortTag == "Application") {
                continue;
            }
            {
                std::string canonicalEvent;
                if (attr.name == "Click" || attr.name == "OnClick") canonicalEvent = "Click";
                else if (attr.name == "Checked" || attr.name == "Unchecked" ||
                         attr.name == "Indeterminate" || attr.name == "ValueChanged" ||
                         attr.name == "TextChanged" || attr.name == "PasswordChanged" ||
                         attr.name == "SelectionChanged" || attr.name == "SelectedItemChanged" ||
                         attr.name == "Expanded" || attr.name == "Collapsed" ||
                         attr.name == "Loaded" || attr.name == "MouseEnter" ||
                         attr.name == "MouseLeave" || attr.name == "MouseDown" ||
                         attr.name == "MouseLeftButtonDown" || attr.name == "MouseUp" ||
                         attr.name == "MouseLeftButtonUp" || attr.name == "KeyDown" ||
                         attr.name == "KeyUp" || attr.name == "RequestNavigate" ||
                         attr.name == "GotFocus" || attr.name == "LostFocus") {
                    canonicalEvent = std::string(attr.name);
                }
                if (!canonicalEvent.empty()) {
                    const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                    AxbEventBindingRecord ev{};
                    ev.controlName = std::string(xName);
                    ev.eventName = std::move(canonicalEvent);
                    ev.handlerMethodName = std::string(Trim(attr.value));
                    pkg.eventBindings.push_back(std::move(ev));

                    AxbInstruction evInst{};
                    evInst.opcode = AxbOpcode::BindEvent;
                    evInst.strIdx0 = evIdx;
                    pkg.instructions.push_back(std::move(evInst));
                    continue;
                }
            }

            const std::string_view trimmedVal = Trim(attr.value);

            if (attr.name == "Command" && trimmedVal.size() > 2 &&
                trimmedVal.front() == '{' && trimmedVal.back() == '}') {
                std::string_view cmdInner = Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
                if (cmdInner.rfind("Binding ", 0) == 0) cmdInner = Trim(cmdInner.substr(8));
                else if (cmdInner.rfind("Bind ", 0) == 0) cmdInner = Trim(cmdInner.substr(5));
                if (const std::size_t comma = cmdInner.find(','); comma != std::string_view::npos) {
                    cmdInner = Trim(cmdInner.substr(0, comma));
                }
                if (cmdInner.rfind("Path=", 0) == 0) cmdInner = Trim(cmdInner.substr(5));
                if (!cmdInner.empty()) {
                    const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                    AxbEventBindingRecord ev{};
                    ev.controlName = std::string(xName);
                    ev.eventName = "Command";
                    ev.handlerMethodName = std::string(cmdInner);
                    pkg.eventBindings.push_back(std::move(ev));

                    AxbInstruction evInst{};
                    evInst.opcode = AxbOpcode::BindEvent;
                    evInst.strIdx0 = evIdx;
                    pkg.instructions.push_back(std::move(evInst));
                }
                continue;
            }
            if (attr.name == "Command" && !trimmedVal.empty() && trimmedVal.front() != '{') {
                AxbInstruction cmdNameInst{};
                cmdNameInst.opcode = AxbOpcode::SetStaticProperty;
                cmdNameInst.propId = PropId_RoutedCommandName;
                cmdNameInst.literalValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
                pkg.instructions.push_back(std::move(cmdNameInst));
                continue;
            }

            const std::uint16_t propId =
                ((shortTag == "BoxShadow" || shortTag == "GradientStop") && attr.name == "Color")
                    ? static_cast<std::uint16_t>(PropId_EffectColor)
                    : ((shortTag == "ContentControl" && attr.name == "Content")
                        ? static_cast<std::uint16_t>(PropId_Content)
                        : ResolvePropertyId(attr.name));

            if (trimmedVal.size() > 2 && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
                std::string_view inner = Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
                if (inner.rfind("EcsBind ", 0) == 0) {
                    inner = Trim(inner.substr(8));
                    const std::size_t dot = inner.rfind('.');
                    const std::string_view compName =
                        (dot != std::string_view::npos) ? inner.substr(0, dot) : inner;
                    const std::string_view fieldName =
                        (dot != std::string_view::npos) ? inner.substr(dot + 1) : std::string_view{};

                    std::string canonicalComp(compName);
                    if (canonicalComp.find("::") == std::string::npos) {
                        if (const std::size_t lastDot = canonicalComp.rfind('.'); lastDot != std::string::npos) {
                            canonicalComp.replace(lastDot, 1, "::");
                        }
                    }
                    AxbInstruction ecsInst{};
                    ecsInst.opcode = AxbOpcode::BindEcsColumn;
                    ecsInst.propId = propId;
                    ecsInst.strIdx0 = pkg.InternString(compName);
                    ecsInst.strIdx1 = pkg.InternString(fieldName);
                    ecsInst.symbolId = rs_sem::stableTypeId(canonicalComp);
                    pkg.instructions.push_back(std::move(ecsInst));
                    continue;
                }

                // Handle {StaticResource ...}, {DynamicResource ...}, {noesis:Loc ...}, {Loc ...}
                {
                    std::string_view keyPart{};
                    std::uint8_t resFlags = AxbResourceFlag_StaticResource;
                    if (TryParseResourceMarkupExtension(trimmedVal, keyPart, resFlags)) {
                        if (propId == PropId_Style || propId == PropId_ItemTemplate ||
                            propId == PropId_Template || propId == PropId_ContentTemplate ||
                            propId == PropId_HeaderTemplate || propId == PropId_ContextMenuKey) {
                            if (propId == PropId_Style) {
                                attachedStyleKey = std::string(keyPart);
                            }
                            AxbInstruction keyInst{};
                            keyInst.opcode = AxbOpcode::SetStaticProperty;
                            keyInst.propId = propId;
                            keyInst.literalValue = value::FromString(std::string(keyPart));
                            pkg.instructions.push_back(std::move(keyInst));
                            continue;
                        }

                        AxbInstruction resInst{};
                        resInst.opcode = AxbOpcode::SetResourceProperty;
                        resInst.propId = propId;
                        resInst.flags = resFlags;
                        resInst.strIdx0 = pkg.InternString(keyPart);
                        if (const UnifiedValue* resVal = findLocalOrGlobalResource(keyPart)) {
                            resInst.literalValue = *resVal;
                            if (propId == PropId_PathData) {
                                if (const auto* sv = std::get_if<StringValue>(resVal)) {
                                    ensurePathBakedIntoPackage(sv->view());
                                }
                            }
                        } else if (resFlags == AxbResourceFlag_Loc &&
                                   (propId == PropId_Text || propId == PropId_Title ||
                                    propId == PropId_Content || propId == PropId_Placeholder)) {
                            resInst.literalValue = value::FromString(std::string(keyPart));
                        }
                        pkg.instructions.push_back(std::move(resInst));
                        continue;
                    }
                }

                if (inner.rfind("TemplateBinding", 0) == 0) {
                    const ParsedTemplateBindingClause tb = ParseTemplateBindingMarkupExpression(trimmedVal);
                    if (tb.isValid) {
                        const std::uint16_t srcPropId = ResolveTemplateBindingSourcePropId(tb.sourcePath);
                        if (srcPropId != kUnmappedPropId) {
                            AxbInstruction tbInst{};
                            tbInst.opcode = AxbOpcode::BindTemplateParent;
                            tbInst.propId = propId;
                            tbInst.flags = static_cast<std::uint8_t>(tb.converter);
                            tbInst.strIdx0 = static_cast<std::uint32_t>(srcPropId);
                            tbInst.strIdx1 = pkg.InternString(tb.sourcePath);
                            pkg.instructions.push_back(std::move(tbInst));
                        }
                    }
                    continue;
                }

                if (inner.rfind("x:Static", 0) == 0) {
                    std::string_view memberExpr = Trim(inner.substr(8u));
                    if (memberExpr.rfind("Member=", 0) == 0) {
                        memberExpr = Trim(memberExpr.substr(7u));
                    }
                    if (!memberExpr.empty() && (memberExpr.front() == '\'' || memberExpr.front() == '"') &&
                        memberExpr.back() == memberExpr.front() && memberExpr.size() >= 2u) {
                        memberExpr = memberExpr.substr(1u, memberExpr.size() - 2u);
                    }
                    const std::string_view bareMem = StripXmlPrefix(memberExpr);
                    std::string resolvedStaticStr;
                    bool hasStaticResolved = false;
                    bool isExplicitEmptyString = false;
                    if (bareMem == "Visibility.Collapsed") { resolvedStaticStr = "Collapsed"; hasStaticResolved = true; }
                    else if (bareMem == "Visibility.Hidden") { resolvedStaticStr = "Hidden"; hasStaticResolved = true; }
                    else if (bareMem == "Visibility.Visible") { resolvedStaticStr = "Visible"; hasStaticResolved = true; }
                    else if (bareMem == "String.Empty" || bareMem == "System.String.Empty" || bareMem == "string.Empty") {
                        resolvedStaticStr = "";
                        hasStaticResolved = true;
                        isExplicitEmptyString = true;
                    }
                    else if (bareMem == "Double.NaN" || bareMem == "Single.NaN") { resolvedStaticStr = "Auto"; hasStaticResolved = true; }
                    else if (bareMem == "Boolean.TrueString") { resolvedStaticStr = "True"; hasStaticResolved = true; }
                    else if (bareMem == "Boolean.FalseString") { resolvedStaticStr = "False"; hasStaticResolved = true; }
                    if (hasStaticResolved) {
                        AxbInstruction stInst{};
                        stInst.opcode = AxbOpcode::SetStaticProperty;
                        stInst.propId = propId;
                        stInst.literalValue = isExplicitEmptyString
                            ? value::FromString("")
                            : ParseLiteralPropertyValue(propId, resolvedStaticStr);
                        pkg.instructions.push_back(std::move(stInst));
                    }
                    continue;
                }
                if (inner.rfind("x:Null", 0) == 0) {
                    if (propId == PropId_IsChecked) {
                        AxbInstruction nullInst{};
                        nullInst.opcode = AxbOpcode::SetStaticProperty;
                        nullInst.propId = propId;
                        nullInst.literalValue = UnifiedValue{NullObject{}};
                        pkg.instructions.push_back(std::move(nullInst));
                    }
                    continue;
                }
                if (inner.rfind("x:Type", 0) == 0 ||
                    inner.rfind("RelativeSource", 0) == 0) {
                    continue;
                }

                const ParsedBindingClause parsedBind = ParseBindingMarkupExpression(trimmedVal);
                if (!parsedBind.isValidBinding) {
                    continue;
                }

                if (!parsedBind.staticResourceSourceKey.empty() &&
                    (parsedBind.pathPart.empty() || parsedBind.pathPart == "." || parsedBind.pathPart == "Children")) {
                    AxbInstruction resInst{};
                    resInst.opcode = AxbOpcode::SetResourceProperty;
                    resInst.propId = propId;
                    resInst.flags = AxbResourceFlag_StaticResource;
                    resInst.strIdx0 = pkg.InternString(parsedBind.staticResourceSourceKey);
                    if (const UnifiedValue* resVal = findLocalOrGlobalResource(parsedBind.staticResourceSourceKey)) {
                        if (!parsedBind.stringFormat.empty()) {
                            resInst.literalValue = value::FromString(
                                FormatBindingString(parsedBind.stringFormat, resVal, 1u, nullptr));
                        } else {
                            resInst.literalValue = *resVal;
                        }
                    } else if (!parsedBind.fallbackValue.empty()) {
                        resInst.literalValue = ParseLiteralPropertyValue(propId, parsedBind.fallbackValue);
                    }
                    pkg.instructions.push_back(std::move(resInst));
                    continue;
                }

                if (parsedBind.relativeTemplatedParent) {
                    UnifiedValue parsedFallback{};
                    if (!parsedBind.fallbackValue.empty()) {
                        parsedFallback = ParseLiteralPropertyValue(propId, parsedBind.fallbackValue);
                        AxbInstruction fbInst{};
                        fbInst.opcode = AxbOpcode::SetStaticProperty;
                        fbInst.propId = propId;
                        fbInst.literalValue = parsedFallback;
                        pkg.instructions.push_back(std::move(fbInst));
                    }
                    const std::uint16_t srcPropId = ResolveTemplateBindingSourcePropId(parsedBind.pathPart);
                    if (srcPropId != kUnmappedPropId) {
                        AxbInstruction tbInst{};
                        tbInst.opcode = AxbOpcode::BindTemplateParent;
                        tbInst.propId = propId;
                        tbInst.flags = static_cast<std::uint8_t>(parsedBind.converter);
                        tbInst.strIdx0 = static_cast<std::uint32_t>(srcPropId);
                        tbInst.strIdx1 = pkg.InternString(parsedBind.pathPart);
                        tbInst.literalValue = std::move(parsedFallback);
                        pkg.instructions.push_back(std::move(tbInst));
                    }
                    continue;
                }

                BindingMode mode = parsedBind.mode;
                if (!parsedBind.hasExplicitMode &&
                    ((shortTag == "TextBox" && propId == PropId_Text) ||
                     propId == PropId_SelectedItem ||
                     propId == PropId_SelectedValue ||
                     propId == PropId_SelectedIndex)) {
                    mode = BindingMode::TwoWay;
                }
                const std::string_view pathPart = parsedBind.pathPart;
                const std::string_view fallbackValue = parsedBind.fallbackValue;

                UnifiedValue parsedFallback{};
                if (!fallbackValue.empty()) {
                    parsedFallback = ParseLiteralPropertyValue(propId, fallbackValue);
                    AxbInstruction fbInst{};
                    fbInst.opcode = AxbOpcode::SetStaticProperty;
                    fbInst.propId = propId;
                    fbInst.literalValue = parsedFallback;
                    pkg.instructions.push_back(std::move(fbInst));
                }

                const bool isAttrSelfDc =
                    (pathPart.empty() || pathPart == ".") &&
                    parsedBind.elementName.empty() &&
                    parsedBind.relativeAncestorType.empty() &&
                    !parsedBind.relativeSelf;
                const bool hasAttrElemSrc = !parsedBind.elementName.empty() ||
                                            !parsedBind.relativeAncestorType.empty() ||
                                            parsedBind.relativeSelf ||
                                            isAttrSelfDc;
                const std::string_view effAttrPath = isAttrSelfDc ? std::string_view("DataContext") : pathPart;

                UnifiedValue parsedTargetNull{};
                if (parsedBind.hasTargetNullValue) {
                    parsedTargetNull = parsedBind.targetNullValue.empty()
                        ? value::FromString("")
                        : ParseLiteralPropertyValue(propId, parsedBind.targetNullValue);
                }
                AxbCompiledBindingChain chain{};
                chain.rawPath = std::string(effAttrPath);
                chain.targetPropertyId = propId;
                chain.mode = mode;
                chain.fallbackValue = std::move(parsedFallback);
                chain.targetNullValue = std::move(parsedTargetNull);
                chain.elementName = std::string(parsedBind.elementName);
                chain.relativeAncestorType = parsedBind.relativeAncestorType;
                chain.relativeSelf = parsedBind.relativeSelf || isAttrSelfDc;
                chain.converter = parsedBind.converter;
                chain.converterKey = parsedBind.converterKey;
                chain.converterParameter = parsedBind.converterParameter;
                chain.stringFormat = parsedBind.stringFormat;
                resolveConverterResource(chain.converter, chain.converterKey);

                const bool keepAttrAttached = parsedBind.isAttachedPropertyPath ||
                    (hasAttrElemSrc &&
                     effAttrPath.find('.') != std::string_view::npos &&
                     !IsInstanceSubHopPropertyPath(effAttrPath) && ResolveMappedSetterPropertyId(effAttrPath) != kUnmappedPropId);
                splitPathIntoHops(effAttrPath, chain.hops, keepAttrAttached);

                const auto chainIdx = static_cast<std::uint32_t>(pkg.bindingChains.size());
                pkg.bindingChains.push_back(std::move(chain));

                AxbInstruction bindInst{};
                bindInst.opcode = AxbOpcode::BindProperty;
                bindInst.propId = propId;
                bindInst.flags = static_cast<std::uint8_t>(mode);
                bindInst.strIdx0 = chainIdx;
                pkg.instructions.push_back(std::move(bindInst));
                continue;
            }

            // Inline 6-float XAML Matrix syntax: RenderTransform="m11, m12, m21, m22, offsetX, offsetY"
            if (propId == PropId_RenderTransform) {
                emitStaticRenderTransform(ParseXamlMatrix2DLiteral(trimmedVal));
                continue;
            }
            if (propId == PropId_LayoutTransform) {
                emitStaticLayoutTransform(ParseXamlMatrix2DLiteral(trimmedVal));
                continue;
            }

            const UnifiedValue litVal = ParseLiteralPropertyValue(propId, trimmedVal);
            if (propId == PropId_PathData) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    ensurePathBakedIntoPackage(sv->view());
                }
            } else if (propId == PropId_ShapePoints) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    const std::string svg = ConvertPointsToSvgPath(sv->view(), shortTag == "Polygon");
                    if (!svg.empty()) {
                        ensurePathBakedIntoPackage(svg);
                    }
                }
            }
            if (propId == PropId_Style) {
                if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                    attachedStyleKey = std::string(sv->view());
                }
            }

            AxbInstruction setInst{};
            setInst.opcode = AxbOpcode::SetStaticProperty;
            setInst.propId = propId;
            setInst.literalValue = litVal;
            pkg.instructions.push_back(std::move(setInst));
        }

        if (selfClosing) {
            AxbInstruction endInst{};
            endInst.opcode = AxbOpcode::EndElement;
            pkg.instructions.push_back(std::move(endInst));
        } else {
            XmlScopeEntry visScope{};
            visScope.kind = XmlScopeKind::VisualElement;
            visScope.tagName = std::string(tagName);
            visScope.resourceKey = std::move(attachedStyleKey);
            scopeStack.push_back(std::move(visScope));
        }
    }

    if (!isSvgInput && !isLottieInput && !isRiveInput && !scopeStack.empty()) {
        result.diagnostics.push_back(
            "AERO_XAML_UNCLOSED_TAG: Unclosed XML element '<" + scopeStack.back().tagName + ">'");
        result.succeeded = false;
        return result;
    }

    // If <DataTemplate.Triggers> was parsed at top level of a DataTemplate, attach it to the root element
    // and chain BasedOn to any existing root Style!
    if (!dtRootTriggersStyleKey.empty() && !pkg.instructions.empty() &&
        pkg.instructions[0].opcode == AxbOpcode::BeginElement) {
        for (auto& st : pkg.styles) {
            if (st.key == dtRootTriggersStyleKey) {
                if ((st.targetTypeName.empty() || st.targetTypeName == "UIElement") &&
                    !pkg.rootTypeName.empty()) {
                    st.targetTypeName = std::string(StripXmlPrefix(pkg.rootTypeName));
                }
                bool updatedExistingStyleInst = false;
                for (std::size_t idx = 1u; idx < pkg.instructions.size(); ++idx) {
                    auto& inst = pkg.instructions[idx];
                    if (inst.opcode == AxbOpcode::BeginElement ||
                        inst.opcode == AxbOpcode::EndElement) {
                        break;
                    }
                    if (inst.opcode == AxbOpcode::SetStaticProperty &&
                        inst.propId == PropId_Style) {
                        if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                            const std::string existingStyleKey(sv->view());
                            if (!existingStyleKey.empty() && existingStyleKey != dtRootTriggersStyleKey) {
                                if (st.basedOnKey.empty()) {
                                    st.basedOnKey = existingStyleKey;
                                }
                            }
                        }
                        inst.literalValue = value::FromString(dtRootTriggersStyleKey);
                        updatedExistingStyleInst = true;
                    }
                }
                if (!updatedExistingStyleInst) {
                    AxbInstruction dtStyleInst{};
                    dtStyleInst.opcode = AxbOpcode::SetStaticProperty;
                    dtStyleInst.propId = PropId_Style;
                    dtStyleInst.literalValue = value::FromString(dtRootTriggersStyleKey);
                    pkg.instructions.insert(pkg.instructions.begin() + 1, std::move(dtStyleInst));
                }
                break;
            }
        }
    }

    for (auto& st : pkg.styles) {
        if (st.controlTemplateKey.empty()) {
            for (const auto& setter : st.setters) {
                if (setter.propId == PropId_Template) {
                    if (const auto* sv = std::get_if<StringValue>(&setter.value)) {
                        st.controlTemplateKey = sv->str();
                    }
                }
            }
        }
        if (!st.controlTemplateKey.empty() && !st.inlineControlTemplate) {
            if (const AxbControlTemplate* foundCt = pkg.FindControlTemplate(st.controlTemplateKey)) {
                st.inlineControlTemplate = std::make_shared<AxbControlTemplate>(*foundCt);
            }
        }
    }

    flushDeferredMtsdfJobs();
    if (!pkg.mtsdfPaths.empty() && !isSvgInput && !isLottieInput && !isRiveInput) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    if (!isSvgInput && !isLottieInput && !isRiveInput) {
        PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::Xaml, effectiveXamlPath, result.sceneIr);
    }

    // Rewrite generic RelayCommand<T> / DelegateCommand<T> in user scripts to TypedRelayCommand<T> / TypedDelegateCommand<T>
    // so both arity-0 RelayCommand and generic RelayCommand<T> coexist cleanly in module Aero.UI.
    auto rewriteGenericCommandNames = [](std::string_view src) -> std::string {
        std::string out;
        out.reserve(src.size() + 32u);
        bool inStr = false;
        bool inChar = false;
        for (std::size_t i = 0; i < src.size();) {
            const char c = src[i];
            if (inStr) {
                out.push_back(c);
                if (c == '\\' && i + 1u < src.size()) {
                    out.push_back(src[i + 1u]);
                    i += 2u;
                    continue;
                }
                if (c == '"') inStr = false;
                ++i;
                continue;
            }
            if (inChar) {
                out.push_back(c);
                if (c == '\\' && i + 1u < src.size()) {
                    out.push_back(src[i + 1u]);
                    i += 2u;
                    continue;
                }
                if (c == '\'') inChar = false;
                ++i;
                continue;
            }
            {
                std::size_t verbatimPrefix = 0u;
                if (c == '@' && i + 1u < src.size() && src[i + 1u] == '"') {
                    verbatimPrefix = 2u;
                } else if (i + 2u < src.size() && src[i + 2u] == '"' &&
                           ((c == '$' && src[i + 1u] == '@') ||
                            (c == '@' && src[i + 1u] == '$'))) {
                    verbatimPrefix = 3u;
                }
                if (verbatimPrefix != 0u) {
                    out.append(src.substr(i, verbatimPrefix));
                    i += verbatimPrefix;
                    while (i < src.size()) {
                        const char vc = src[i++];
                        out.push_back(vc);
                        if (vc == '"') {
                            if (i < src.size() && src[i] == '"') {
                                out.push_back(src[i++]);
                                continue;
                            }
                            break;
                        }
                    }
                    continue;
                }
            }
            if (c == '"') {
                inStr = true;
                out.push_back(c);
                ++i;
                continue;
            }
            if (c == '\'') {
                inChar = true;
                out.push_back(c);
                ++i;
                continue;
            }
            if (c == '/' && i + 1u < src.size() && src[i + 1u] == '/') {
                while (i < src.size() && src[i] != '\n') {
                    out.push_back(src[i++]);
                }
                continue;
            }
            if (c == '/' && i + 1u < src.size() && src[i + 1u] == '*') {
                out.push_back(src[i++]);
                out.push_back(src[i++]);
                while (i < src.size()) {
                    if (src[i] == '*' && i + 1u < src.size() && src[i + 1u] == '/') {
                        out.push_back(src[i++]);
                        out.push_back(src[i++]);
                        break;
                    }
                    out.push_back(src[i++]);
                }
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const std::size_t start = i;
                while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                    ++i;
                }
                const std::string_view ident = src.substr(start, i - start);
                if (ident == "RelayCommand" || ident == "DelegateCommand") {
                    std::size_t look = i;
                    while (look < src.size() && (src[look] == ' ' || src[look] == '\t')) {
                        ++look;
                    }
                    if (look < src.size() && src[look] == '<') {
                        out.append(ident == "RelayCommand" ? "TypedRelayCommand" : "TypedDelegateCommand");
                        continue;
                    }
                }
                out.append(ident);
                continue;
            }
            out.push_back(c);
            ++i;
        }
        return out;
    };

    // Preprocess C# conditional compilation (#define/#undef/#if/#elif/#else/#endif)
    auto preprocessCSharpDirectives = [](std::string_view raw) -> std::string {
        if (raw.size() >= 3u &&
            static_cast<unsigned char>(raw[0]) == 0xEFu &&
            static_cast<unsigned char>(raw[1]) == 0xBBu &&
            static_cast<unsigned char>(raw[2]) == 0xBFu) {
            raw.remove_prefix(3u);
        }
        if (raw.find('#') == std::string_view::npos) {
            return std::string(raw);
        }
        std::unordered_set<std::string> definedSymbols{
            "AERO", "AEROGUI", "WPF", "DEBUG", "TRACE"
        };
        auto stripLineComment = [](std::string_view expr) noexcept -> std::string {
            std::string cleaned;
            cleaned.reserve(expr.size());
            bool inBlock = false;
            for (std::size_t i = 0u; i < expr.size(); ++i) {
                if (inBlock) {
                    if (expr[i] == '*' && i + 1u < expr.size() && expr[i + 1u] == '/') {
                        inBlock = false;
                        ++i;
                        cleaned.push_back(' ');
                    }
                    continue;
                }
                if (expr[i] == '/' && i + 1u < expr.size() && expr[i + 1u] == '/') {
                    break;
                }
                if (expr[i] == '/' && i + 1u < expr.size() && expr[i + 1u] == '*') {
                    inBlock = true;
                    ++i;
                    continue;
                }
                cleaned.push_back(expr[i]);
            }
            return cleaned;
        };
        auto evalCond = [&definedSymbols, &stripLineComment](std::string_view rawExpr) -> bool {
            const std::string exprStr = stripLineComment(rawExpr);
            std::string_view s(exprStr);
            std::size_t pos = 0u;
            auto skipWs = [&]() noexcept {
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
                    ++pos;
                }
            };
            auto isIdentStart = [](unsigned char c) noexcept {
                return std::isalpha(c) || c == '_';
            };
            auto isIdentCont = [](unsigned char c) noexcept {
                return std::isalnum(c) || c == '_';
            };
            auto parseIdent = [&]() -> std::string_view {
                skipWs();
                const std::size_t start = pos;
                if (pos < s.size() && isIdentCont(static_cast<unsigned char>(s[pos]))) {
                    while (pos < s.size() && isIdentCont(static_cast<unsigned char>(s[pos]))) {
                        ++pos;
                    }
                }
                return s.substr(start, pos - start);
            };
            std::function<bool()> parseOr;
            std::function<bool()> parseAnd;
            std::function<bool()> parseEq;
            std::function<bool()> parseUnary;
            std::function<bool()> parsePrimary;

            parsePrimary = [&]() -> bool {
                skipWs();
                if (pos >= s.size()) {
                    return false;
                }
                if (s[pos] == '(') {
                    ++pos;
                    const bool v = parseOr();
                    skipWs();
                    if (pos < s.size() && s[pos] == ')') {
                        ++pos;
                    }
                    return v;
                }
                if (isIdentCont(static_cast<unsigned char>(s[pos]))) {
                    const std::string_view id = parseIdent();
                    if (id == "true" || id == "1") return true;
                    if (id == "false" || id == "0") return false;
                    if (id == "defined") {
                        skipWs();
                        bool hasParen = false;
                        if (pos < s.size() && s[pos] == '(') {
                            hasParen = true;
                            ++pos;
                        }
                        const std::string_view sym = parseIdent();
                        if (hasParen) {
                            skipWs();
                            if (pos < s.size() && s[pos] == ')') ++pos;
                        }
                        return !sym.empty() && definedSymbols.count(std::string(sym)) > 0u;
                    }
                    if (!id.empty() && isIdentStart(static_cast<unsigned char>(id.front()))) {
                        return definedSymbols.count(std::string(id)) > 0u;
                    }
                    return false;
                }
                return false;
            };
            parseUnary = [&]() -> bool {
                skipWs();
                if (pos < s.size() && s[pos] == '!') {
                    ++pos;
                    return !parseUnary();
                }
                return parsePrimary();
            };
            parseEq = [&]() -> bool {
                bool lhs = parseUnary();
                while (true) {
                    skipWs();
                    if (pos + 1u < s.size() && s[pos] == '=' && s[pos + 1u] == '=') {
                        pos += 2u;
                        const bool rhs = parseUnary();
                        lhs = (lhs == rhs);
                    } else if (pos + 1u < s.size() && s[pos] == '!' && s[pos + 1u] == '=') {
                        pos += 2u;
                        const bool rhs = parseUnary();
                        lhs = (lhs != rhs);
                    } else {
                        break;
                    }
                }
                return lhs;
            };
            parseAnd = [&]() -> bool {
                bool lhs = parseEq();
                while (true) {
                    skipWs();
                    if (pos + 1u < s.size() && s[pos] == '&' && s[pos + 1u] == '&') {
                        pos += 2u;
                        const bool rhs = parseEq();
                        lhs = lhs && rhs;
                    } else {
                        break;
                    }
                }
                return lhs;
            };
            parseOr = [&]() -> bool {
                bool lhs = parseAnd();
                while (true) {
                    skipWs();
                    if (pos + 1u < s.size() && s[pos] == '|' && s[pos + 1u] == '|') {
                        pos += 2u;
                        const bool rhs = parseAnd();
                        lhs = lhs || rhs;
                    } else {
                        break;
                    }
                }
                return lhs;
            };
            return parseOr();
        };
        struct CondFrame {
            bool parentActive = true;
            bool branchTaken = false;
            bool currentActive = true;
        };
        std::vector<CondFrame> condStack;
        std::string out;
        out.reserve(raw.size());
        bool inBlockComment = false;
        bool inVerbatimString = false;
        bool inRawString = false;
        std::size_t rawQuoteLen = 0u;
        auto advanceLexerState = [&](std::string_view line) noexcept {
            bool inString = false;
            bool inChar = false;
            for (std::size_t i = 0u; i < line.size(); ++i) {
                const char c = line[i];
                if (inBlockComment) {
                    if (c == '*' && i + 1u < line.size() && line[i + 1u] == '/') {
                        inBlockComment = false;
                        ++i;
                    }
                    continue;
                }
                if (inRawString) {
                    if (c == '"') {
                        std::size_t q = 0u;
                        while (i + q < line.size() && line[i + q] == '"') ++q;
                        if (q >= rawQuoteLen) {
                            inRawString = false;
                            i += rawQuoteLen - 1u;
                            rawQuoteLen = 0u;
                            continue;
                        }
                        i += q - 1u;
                    }
                    continue;
                }
                if (inVerbatimString) {
                    if (c == '"') {
                        if (i + 1u < line.size() && line[i + 1u] == '"') {
                            ++i;
                        } else {
                            inVerbatimString = false;
                        }
                    }
                    continue;
                }
                if (inString) {
                    if (c == '\\' && i + 1u < line.size()) {
                        ++i;
                    } else if (c == '"') {
                        inString = false;
                    }
                    continue;
                }
                if (inChar) {
                    if (c == '\\' && i + 1u < line.size()) {
                        ++i;
                    } else if (c == '\'') {
                        inChar = false;
                    }
                    continue;
                }
                if (c == '/' && i + 1u < line.size() && line[i + 1u] == '/') {
                    break;
                }
                if (c == '/' && i + 1u < line.size() && line[i + 1u] == '*') {
                    inBlockComment = true;
                    ++i;
                    continue;
                }
                if ((c == '@' || (c == '$' && i + 1u < line.size() && line[i + 1u] == '@') ||
                     (c == '@' && i + 1u < line.size() && line[i + 1u] == '$')) &&
                    ((c == '@' && i + 1u < line.size() && line[i + 1u] == '"') ||
                     (i + 2u < line.size() && line[i + 2u] == '"'))) {
                    const std::size_t qPos = (line[i + 1u] == '"') ? (i + 1u) : (i + 2u);
                    inVerbatimString = true;
                    i = qPos;
                    continue;
                }
                if (c == '"') {
                    std::size_t q = 0u;
                    while (i + q < line.size() && line[i + q] == '"') ++q;
                    if (q >= 3u) {
                        inRawString = true;
                        rawQuoteLen = q;
                        i += q - 1u;
                    } else {
                        inString = true;
                    }
                    continue;
                }
                if (c == '\'') {
                    inChar = true;
                    continue;
                }
            }
        };
        std::size_t lineStart = 0u;
        while (lineStart < raw.size()) {
            std::size_t lineEnd = raw.find('\n', lineStart);
            const bool hasNl = (lineEnd != std::string_view::npos);
            if (!hasNl) lineEnd = raw.size();
            std::string_view line = raw.substr(lineStart, lineEnd - lineStart);
            std::string_view trimmed = line;
            while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
                trimmed.remove_prefix(1u);
            }
            if (!inBlockComment && !inVerbatimString && !inRawString &&
                !trimmed.empty() && trimmed.front() == '#') {
                std::string_view rest = trimmed.substr(1u);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1u);
                std::size_t kwLen = 0u;
                while (kwLen < rest.size() && std::isalpha(static_cast<unsigned char>(rest[kwLen]))) ++kwLen;
                const std::string_view kw = rest.substr(0u, kwLen);
                const std::string_view arg = rest.substr(kwLen);
                const bool active = condStack.empty() ? true : condStack.back().currentActive;
                if (kw == "define") {
                    if (active) {
                        const std::string symClean = stripLineComment(arg);
                        std::string_view sym(symClean);
                        while (!sym.empty() && std::isspace(static_cast<unsigned char>(sym.front()))) sym.remove_prefix(1u);
                        std::size_t slen = 0u;
                        while (slen < sym.size() && (std::isalnum(static_cast<unsigned char>(sym[slen])) || sym[slen] == '_')) {
                            ++slen;
                        }
                        if (slen > 0u) {
                            definedSymbols.insert(std::string(sym.substr(0u, slen)));
                        }
                    }
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
                if (kw == "undef") {
                    if (active) {
                        const std::string symClean = stripLineComment(arg);
                        std::string_view sym(symClean);
                        while (!sym.empty() && std::isspace(static_cast<unsigned char>(sym.front()))) sym.remove_prefix(1u);
                        std::size_t slen = 0u;
                        while (slen < sym.size() && (std::isalnum(static_cast<unsigned char>(sym[slen])) || sym[slen] == '_')) {
                            ++slen;
                        }
                        if (slen > 0u) {
                            definedSymbols.erase(std::string(sym.substr(0u, slen)));
                        }
                    }
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
                if (kw == "if") {
                    const bool pAct = active;
                    const bool cVal = pAct && evalCond(arg);
                    condStack.push_back(CondFrame{pAct, cVal, cVal});
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
                if (kw == "elif" && !condStack.empty()) {
                    auto& top = condStack.back();
                    if (!top.parentActive || top.branchTaken) {
                        top.currentActive = false;
                    } else {
                        const bool cVal = evalCond(arg);
                        top.currentActive = cVal;
                        top.branchTaken = cVal;
                    }
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
                if (kw == "else" && !condStack.empty()) {
                    auto& top = condStack.back();
                    top.currentActive = top.parentActive && !top.branchTaken;
                    top.branchTaken = true;
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
                if (kw == "endif" && !condStack.empty()) {
                    condStack.pop_back();
                    if (hasNl) out.push_back('\n');
                    lineStart = lineEnd + 1u;
                    continue;
                }
            }
            const bool active = condStack.empty() ? true : condStack.back().currentActive;
            if (active) {
                advanceLexerState(line);
                out.append(line);
            }
            if (hasNl) out.push_back('\n');
            lineStart = lineEnd + 1u;
        }
        return out;
    };

    auto isPureNativeExtensionMetadataFile = [](std::string_view content) noexcept -> bool {
        return content.find("namespace NoesisGUIExtensions") != std::string_view::npos &&
               (content.find("DependencyProperty.Register") != std::string_view::npos ||
                content.find("Animatable") != std::string_view::npos);
    };

    // Generate *.g.rs partial class stub when x:Class is specified
    std::vector<SourceFile> allSources;
    allSources.reserve(input.scriptSources.size() + 2u);
    for (const auto& rawSf : input.scriptSources) {
        std::string cleaned = preprocessCSharpDirectives(rawSf.content);
        if (isPureNativeExtensionMetadataFile(cleaned)) {
            continue;
        }
        if (cleaned.find("RelayCommand") != std::string::npos ||
            cleaned.find("DelegateCommand") != std::string::npos) {
            cleaned = rewriteGenericCommandNames(cleaned);
        }
        allSources.push_back(SourceFile{rawSf.path, std::move(cleaned)});
    }
    if (!pkg.className.empty()) {
        std::string moduleName = "Aero.UI";
        std::string shortClassName = pkg.className;
        if (const auto sep = pkg.className.find("::"); sep != std::string::npos) {
            moduleName = pkg.className.substr(0, sep);
            shortClassName = pkg.className.substr(sep + 2);
        } else if (const auto dot = pkg.className.rfind('.'); dot != std::string::npos) {
            moduleName = pkg.className.substr(0, dot);
            shortClassName = pkg.className.substr(dot + 1);
        }

        bool userDefinesRootHandle = false;
        bool userDefinesInitComp = false;
        bool userDefinesSetDc = false;
        bool userDefinesLoadRes = false;
        auto stripRsComments = [](std::string_view src) -> std::string {
            std::string out;
            out.reserve(src.size());
            bool inStr = false;
            for (std::size_t i = 0; i < src.size(); ++i) {
                if (inStr) {
                    out.push_back(src[i]);
                    if (src[i] == '\\' && i + 1 < src.size()) {
                        out.push_back(src[++i]);
                    } else if (src[i] == '"') {
                        inStr = false;
                    }
                    continue;
                }
                if (src[i] == '"') {
                    inStr = true;
                    out.push_back(src[i]);
                } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
                    while (i < src.size() && src[i] != '\n') ++i;
                    out.push_back('\n');
                } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
                    ++i;
                    out.push_back(' ');
                } else {
                    out.push_back(src[i]);
                }
            }
            return out;
        };
        auto extractClassBodies = [](std::string_view cleanSrc, std::string_view targetClass) -> std::string {
            std::string combined;
            const std::string needle = "class " + std::string(targetClass);
            std::size_t searchPos = 0;
            while (searchPos < cleanSrc.size()) {
                const auto pos = cleanSrc.find(needle, searchPos);
                if (pos == std::string_view::npos) {
                    break;
                }
                const std::size_t after = pos + needle.size();
                searchPos = after;
                if (after < cleanSrc.size()) {
                    const unsigned char ch = static_cast<unsigned char>(cleanSrc[after]);
                    if (std::isalnum(ch) || ch == '_') {
                        continue;
                    }
                }
                const auto openBrace = cleanSrc.find('{', after);
                if (openBrace == std::string_view::npos) {
                    break;
                }
                int depth = 1;
                std::size_t i = openBrace + 1u;
                for (; i < cleanSrc.size() && depth > 0; ++i) {
                    if (cleanSrc[i] == '{') ++depth;
                    else if (cleanSrc[i] == '}') --depth;
                }
                combined.append(cleanSrc.substr(openBrace, i - openBrace));
                combined.push_back('\n');
                searchPos = i;
            }
            return combined;
        };
        auto classDeclHasBaseClass = [](std::string_view cleanSrc, std::string_view targetClass) -> bool {
            const std::string needle = "class " + std::string(targetClass);
            std::size_t searchPos = 0;
            while (searchPos < cleanSrc.size()) {
                const auto pos = cleanSrc.find(needle, searchPos);
                if (pos == std::string_view::npos) break;
                const std::size_t after = pos + needle.size();
                searchPos = after;
                if (after < cleanSrc.size()) {
                    const unsigned char ch = static_cast<unsigned char>(cleanSrc[after]);
                    if (std::isalnum(ch) || ch == '_') continue;
                }
                const auto openBrace = cleanSrc.find('{', after);
                if (openBrace == std::string_view::npos) break;
                const std::string_view headerSlice = Trim(cleanSrc.substr(after, openBrace - after));
                if (!headerSlice.empty() && headerSlice.front() == ':') {
                    return true;
                }
                searchPos = openBrace + 1u;
            }
            return false;
        };
        auto mapXamlTagToSdkType = [](std::string_view rawTag) -> std::string {
            const std::string_view st = StripXmlPrefix(rawTag);
            if (st == "Window") return "Window";
            if (st == "UserControl") return "UserControl";
            if (st == "Page") return "Page";
            if (st == "Button") return "Button";
            if (st == "RepeatButton") return "RepeatButton";
            if (st == "ToggleButton") return "ToggleButton";
            if (st == "CheckBox") return "CheckBox";
            if (st == "RadioButton") return "RadioButton";
            if (st == "Hyperlink") return "Hyperlink";
            if (st == "TextBlock" || st == "Label") return "TextBlock";
            if (st == "TextBox") return "TextBox";
            if (st == "PasswordBox") return "PasswordBox";
            if (st == "Slider") return "Slider";
            if (st == "ProgressBar") return "ProgressBar";
            if (st == "ListView") return "ListView";
            if (st == "ListBox") return "ListBox";
            if (st == "Border") return "Border";
            if (st == "Grid") return "Grid";
            if (st == "StackPanel" || st == "VirtualizingStackPanel") return "StackPanel";
            if (st == "DockPanel") return "DockPanel";
            if (st == "WrapPanel") return "WrapPanel";
            if (st == "UniformGrid") return "UniformGrid";
            if (st == "ScrollViewer") return "ScrollViewer";
            if (st == "Canvas") return "Canvas";
            if (st == "Image") return "Image";
            if (st == "GroupBox" || st == "HeaderedContentControl") return "GroupBox";
            if (st == "TabItem") return "TabItem";
            if (st == "Menu") return "Menu";
            if (st == "ToolBar") return "ToolBar";
            if (st == "StatusBar") return "StatusBar";
            if (st == "Popup") return "Popup";
            if (st == "ToolTip") return "ToolTip";
            if (st == "ContextMenu") return "ContextMenu";
            if (st == "GridSplitter") return "GridSplitter";
            if (st == "RiveControl") return "RiveControl";
            if (st == "ComboBox") return "ComboBox";
            if (st == "TabControl") return "TabControl";
            if (st == "Expander") return "Expander";
            if (st == "TreeView") return "TreeView";
            if (st == "TreeViewItem") return "TreeViewItem";
            if (st == "MenuItem") return "MenuItem";
            if (st == "StatusBarItem") return "StatusBarItem";
            if (st == "ContentControl") return "ContentControl";
            return "FrameworkElement";
        };

        const std::string partialStubSuffix = shortClassName + ".g.rs";
        std::unordered_set<std::string> userDeclaredHandleFields;
        bool userInheritsSdkBase = false;
        bool userDefinesDataContextProp = false;
        bool userDefinesCreateMainWindow = false;
        bool userDeclaresConstructors = false;
        bool userDeclaresParameterlessConstructor = false;
        for (const auto& sf : allSources) {
            if (sf.path == partialStubSuffix ||
                (sf.path.size() >= partialStubSuffix.size() &&
                 sf.path.compare(sf.path.size() - partialStubSuffix.size(), partialStubSuffix.size(), partialStubSuffix) == 0)) {
                continue;
            }
            const std::string cleanSrc = stripRsComments(sf.content);
            if (cleanSrc.find("CreateMainWindow(") != std::string::npos ||
                cleanSrc.find("CreateMainWindow (") != std::string::npos) {
                userDefinesCreateMainWindow = true;
            }
            if (classDeclHasBaseClass(cleanSrc, shortClassName)) {
                userInheritsSdkBase = true;
                userDefinesRootHandle = true;
            }
            const std::string classBody = extractClassBodies(cleanSrc, shortClassName);
            if (classBody.empty()) {
                continue;
            }
            {
                int depth = 0;
                bool inBodyStr = false;
                for (std::size_t ci = 0; ci < classBody.size(); ++ci) {
                    const char bc = classBody[ci];
                    if (inBodyStr) {
                        if (bc == '\\' && ci + 1u < classBody.size()) {
                            ++ci;
                            continue;
                        }
                        if (bc == '"') inBodyStr = false;
                        continue;
                    }
                    if (bc == '"') {
                        inBodyStr = true;
                        continue;
                    }
                    if (bc == '{') {
                        ++depth;
                        continue;
                    }
                    if (bc == '}') {
                        --depth;
                        continue;
                    }
                    if (depth == 1 &&
                        ci + shortClassName.size() < classBody.size() &&
                        classBody.compare(ci, shortClassName.size(), shortClassName) == 0) {
                        const bool okBefore = (ci == 0u) ||
                            !(std::isalnum(static_cast<unsigned char>(classBody[ci - 1u])) ||
                              classBody[ci - 1u] == '_' || classBody[ci - 1u] == '.' ||
                              classBody[ci - 1u] == ':' || classBody[ci - 1u] == '~');
                        if (!okBefore) continue;
                        std::size_t prevEnd = ci;
                        while (prevEnd > 0u && std::isspace(static_cast<unsigned char>(classBody[prevEnd - 1u]))) {
                            --prevEnd;
                        }
                        std::size_t prevStart = prevEnd;
                        while (prevStart > 0u && (std::isalnum(static_cast<unsigned char>(classBody[prevStart - 1u])) || classBody[prevStart - 1u] == '_')) {
                            --prevStart;
                        }
                        const std::string_view prevTok(classBody.data() + prevStart, prevEnd - prevStart);
                        if (prevTok == "new") continue;
                        std::size_t look = ci + shortClassName.size();
                        while (look < classBody.size() && std::isspace(static_cast<unsigned char>(classBody[look]))) {
                            ++look;
                        }
                        if (look < classBody.size() && classBody[look] == '(') {
                            userDeclaresConstructors = true;
                            std::size_t p = look + 1u;
                            int pDepth = 1;
                            bool pStr = false;
                            bool hasParamToken = false;
                            while (p < classBody.size() && pDepth > 0) {
                                const char pc = classBody[p++];
                                if (pStr) {
                                    hasParamToken = true;
                                    if (pc == '\\' && p < classBody.size()) ++p;
                                    else if (pc == '"') pStr = false;
                                    continue;
                                }
                                if (pc == '"') {
                                    pStr = true;
                                    hasParamToken = true;
                                    continue;
                                }
                                if (pc == '(') { ++pDepth; hasParamToken = true; }
                                else if (pc == ')') { --pDepth; }
                                else if (!std::isspace(static_cast<unsigned char>(pc))) { hasParamToken = true; }
                            }
                            if (!hasParamToken) {
                                userDeclaresParameterlessConstructor = true;
                            }
                        }
                    }
                }
            }
            if (classBody.find("handle __nativeRootHandle") != std::string::npos) {
                userDefinesRootHandle = true;
            }
            if (classBody.find("void InitializeComponent(") != std::string::npos) {
                userDefinesInitComp = true;
            }
            if (classBody.find("void SetDataContext") != std::string::npos) {
                userDefinesSetDc = true;
            }
            {
                std::size_t dPos = 0u;
                while ((dPos = classBody.find("DataContext", dPos)) != std::string::npos) {
                    const std::size_t afterDc = dPos + 11u;
                    const bool wordStart = (dPos == 0u) || !(std::isalnum(static_cast<unsigned char>(classBody[dPos - 1u])) || classBody[dPos - 1u] == '_' || classBody[dPos - 1u] == '.');
                    const bool wordEnd = (afterDc >= classBody.size()) || !(std::isalnum(static_cast<unsigned char>(classBody[afterDc])) || classBody[afterDc] == '_');
                    if (wordStart && wordEnd) {
                        // Walk backwards before whitespace to see if preceded by a type identifier (e.g. `MyVm DataContext` or `object DataContext`)
                        std::size_t prevEnd = dPos;
                        while (prevEnd > 0u && std::isspace(static_cast<unsigned char>(classBody[prevEnd - 1u]))) {
                            --prevEnd;
                        }
                        if (prevEnd > 0u && (std::isalnum(static_cast<unsigned char>(classBody[prevEnd - 1u])) || classBody[prevEnd - 1u] == '_' || classBody[prevEnd - 1u] == '>' || classBody[prevEnd - 1u] == ']')) {
                            std::size_t prevStart = prevEnd;
                            while (prevStart > 0u && (std::isalnum(static_cast<unsigned char>(classBody[prevStart - 1u])) || classBody[prevStart - 1u] == '_')) {
                                --prevStart;
                            }
                            const std::string_view prevWord(classBody.data() + prevStart, prevEnd - prevStart);
                            if (prevWord != "return" && prevWord != "else" && prevWord != "case") {
                                userDefinesDataContextProp = true;
                                break;
                            }
                        }
                    }
                    dPos = afterDc;
                }
            }
            if (classBody.find("bool LoadResources(") != std::string::npos) {
                userDefinesLoadRes = true;
            }
            for (const auto& nc : pkg.namedControls) {
                if (classBody.find("handle " + nc.name + ";") != std::string::npos ||
                    classBody.find("handle " + nc.name + " ") != std::string::npos) {
                    userDeclaredHandleFields.insert(nc.name);
                }
            }
        }

        std::string xamlLeafName = effectiveXamlPath.empty() ? input.xamlPath : effectiveXamlPath;
        if (const auto slash = xamlLeafName.find_last_of("/\\"); slash != std::string::npos) {
            xamlLeafName = xamlLeafName.substr(slash + 1u);
        }
        if (xamlLeafName.empty()) {
            xamlLeafName = shortClassName + ".xaml";
        }

        std::string escapedLeaf;
        for (char ch : xamlLeafName) {
            if (ch == '\\' || ch == '\"') escapedLeaf.push_back('\\');
            escapedLeaf.push_back(ch);
        }

        result.generatedPartialRsPath = shortClassName + ".g.rs";
        std::string stub = "// Auto-generated by aero-compiler from " + input.xamlPath + "\n";
        stub += "module " + moduleName + ";\n";
        if (userInheritsSdkBase && moduleName != "Aero.UI") {
            stub += "import Aero.UI;\n";
        }
        stub += "\n[Observable]\npublic partial class " + shortClassName + "\n{\n";
        if (!userDefinesRootHandle) {
            stub += "    public handle __nativeRootHandle;\n";
        }
        stub += "    private bool __aero_premounted;\n";
        stub += "    private bool __aero_component_loaded = false;\n";
        stub += "    private void __AeroEnsureLoaded() { if (!this.__aero_component_loaded && !this.__aero_premounted) { this.__aero_component_loaded = true; Aero_LoadComponent__" +
                shortClassName + "(this, \"" + escapedLeaf + "\"); } }\n";
        {
            std::unordered_set<std::string> emittedFieldNames;
            for (const auto& nc : pkg.namedControls) {
                if (!IsValidRealScriptFieldIdentifier(nc.name)) {
                    continue;
                }
                if (userDeclaredHandleFields.count(nc.name) > 0u) {
                    continue;
                }
                if (!emittedFieldNames.insert(nc.name).second) {
                    continue;
                }
                if (userInheritsSdkBase) {
                    const std::string sdkType = mapXamlTagToSdkType(nc.typeName);
                    stub += "    public handle __aero_handle_" + nc.name + ";\n";
                    stub += "    private " + sdkType + " __aero_cache_" + nc.name + " = null;\n";
                    stub += "    public " + sdkType + " " + nc.name + " { get { "
                            "this.__AeroEnsureLoaded(); "
                            "if (this.__aero_cache_" + nc.name + " == null) { " +
                            sdkType + " w = new " + sdkType + "(); "
                            "w.__nativeRootHandle = this.__aero_handle_" + nc.name + "; "
                            "UIElement_RegisterWrapper(this.__aero_handle_" + nc.name + ", w); "
                            "this.__aero_cache_" + nc.name + " = w; } else { "
                            "this.__aero_cache_" + nc.name + ".__nativeRootHandle = this.__aero_handle_" + nc.name + "; "
                            "UIElement_RegisterWrapper(this.__aero_handle_" + nc.name + ", this.__aero_cache_" + nc.name + "); } "
                            "return this.__aero_cache_" + nc.name + "; } }\n";
                } else {
                    stub += "    public handle " + nc.name + ";\n";
                }
            }
        }
        if (!userDefinesInitComp) {
            stub += "    public void InitializeComponent() { }\n";
        }
        if (!userDefinesSetDc) {
            stub += "    public void SetDataContext<T>(T dataContext) where T : class { Aero_SetDataContext__" +
                    shortClassName + "<" + shortClassName + ", T>(this, dataContext); }\n";
        }
        if (!userDefinesLoadRes) {
            stub += "    public bool LoadResources(string uri) { return Aero_LoadResources__" + shortClassName +
                    "(this.__nativeRootHandle, uri); }\n";
        }
        if (userInheritsSdkBase && !userDefinesDataContextProp) {
            stub += "    public object DataContext { get { return GUI_GetDataContextObject(this); } set { GUI_SetDataContextObject(this, value); } }\n";
        }
        stub += "}\n";
        if (userInheritsSdkBase && (!userDeclaresConstructors || userDeclaresParameterlessConstructor)) {
            stub += "public " + shortClassName + " __AeroCreateInstance_" + shortClassName + "() { return new " + shortClassName + "(); }\n";
            if (shortClassName == "MainWindow" && !userDefinesCreateMainWindow) {
                stub += "public MainWindow CreateMainWindow() { return new MainWindow(); }\n";
            }
        }
        stub += "public bool Aero_LoadComponent__" + shortClassName + "(" + shortClassName + " component, string uri) { return false; }\n";
        if (!userDefinesSetDc) {
            stub += "public bool Aero_SetDataContext__" + shortClassName + "<TOwner, TContext>(TOwner owner, TContext dataContext) where TOwner : class where TContext : class { return false; }\n";
        }
        if (!userDefinesLoadRes) {
            stub += "public bool Aero_LoadResources__" + shortClassName + "(handle root, string uri) { return false; }\n";
        }
        result.generatedPartialRsCode = stub;

        if (!allSources.empty()) {
            bool alreadyHasPartialStub = false;
            for (const auto& sf : allSources) {
                if (sf.path == result.generatedPartialRsPath ||
                    (sf.path.size() >= result.generatedPartialRsPath.size() &&
                     sf.path.compare(
                         sf.path.size() - result.generatedPartialRsPath.size(),
                         result.generatedPartialRsPath.size(),
                         result.generatedPartialRsPath) == 0)) {
                    alreadyHasPartialStub = true;
                    break;
                }
            }
            if (!alreadyHasPartialStub) {
                allSources.push_back(SourceFile{result.generatedPartialRsPath, result.generatedPartialRsCode});
            }
        }
    }

    // Auto-provide built-in Aero.UI native SDK declarations when imported by user scripts
    if (!allSources.empty()) {
        bool importsAeroUi = false;
        bool definesAeroUi = false;
        for (const auto& sf : allSources) {
            if (sf.content.find("module Aero.UI;") != std::string::npos ||
                sf.content.find("namespace Aero.UI") != std::string::npos) {
                definesAeroUi = true;
            }
            if (sf.content.find("import Aero.UI;") != std::string::npos ||
                sf.content.find("using Aero.UI;") != std::string::npos ||
                sf.content.find("using System.ComponentModel;") != std::string::npos ||
                sf.content.find("using System.Collections.ObjectModel") != std::string::npos ||
                sf.content.find("using System.Globalization;") != std::string::npos ||
                sf.content.find("using System.Windows") != std::string::npos ||
                sf.content.find("ObservableObject") != std::string::npos ||
                sf.content.find("ViewModelBase") != std::string::npos ||
                sf.content.find("BindableBase") != std::string::npos ||
                sf.content.find("INotifyPropertyChanged") != std::string::npos ||
                sf.content.find("RelayCommand") != std::string::npos ||
                sf.content.find("DelegateCommand") != std::string::npos) {
                importsAeroUi = true;
            }
        }
        if ((importsAeroUi || !pkg.className.empty()) && !definesAeroUi) {
            std::string sdkCode =
                "module Aero.UI;\n"
                "public delegate void Action();\n"
                "public delegate void Action<T>(T arg);\n"
                "public delegate TResult Func<TResult>();\n"
                "public delegate TResult Func<T, TResult>(T arg);\n"
                "public class Type { }\n"
                "public class CultureInfo { }\n"
                "public interface IValueConverter { }\n"
                "public delegate void ExecuteFunc();\n"
                "public delegate bool CanExecuteFunc();\n"
                "public delegate void ExecuteIntFunc(int param);\n"
                "public delegate bool CanExecuteIntFunc(int param);\n"
                "public delegate void ExecuteStringFunc(string param);\n"
                "public delegate bool CanExecuteStringFunc(string param);\n"
                "public delegate void ExecuteHandleFunc(handle param);\n"
                "public delegate bool CanExecuteHandleFunc(handle param);\n"
                "public interface ICommand\n"
                "{\n"
                "    bool CanExecute();\n"
                "    void Execute();\n"
                "}\n"
                "public class DelegateCommand : ICommand\n"
                "{\n"
                "    public ExecuteFunc _exec0 = null;\n"
                "    public CanExecuteFunc _canExec0 = null;\n"
                "    public ExecuteIntFunc _execInt = null;\n"
                "    public CanExecuteIntFunc _canExecInt = null;\n"
                "    public ExecuteStringFunc _execStr = null;\n"
                "    public CanExecuteStringFunc _canExecStr = null;\n"
                "    public ExecuteHandleFunc _execHandle = null;\n"
                "    public CanExecuteHandleFunc _canExecHandle = null;\n"
                "    public DelegateCommand() { }\n"
                "    public DelegateCommand(ExecuteFunc exec) { _exec0 = exec; }\n"
                "    public DelegateCommand(ExecuteFunc exec, CanExecuteFunc canExec) { _exec0 = exec; _canExec0 = canExec; }\n"
                "    public DelegateCommand(ExecuteIntFunc exec) { _execInt = exec; }\n"
                "    public DelegateCommand(ExecuteIntFunc exec, CanExecuteIntFunc canExec) { _execInt = exec; _canExecInt = canExec; }\n"
                "    public DelegateCommand(ExecuteStringFunc exec) { _execStr = exec; }\n"
                "    public DelegateCommand(ExecuteStringFunc exec, CanExecuteStringFunc canExec) { _execStr = exec; _canExecStr = canExec; }\n"
                "    public DelegateCommand(ExecuteHandleFunc exec) { _execHandle = exec; }\n"
                "    public DelegateCommand(ExecuteHandleFunc exec, CanExecuteHandleFunc canExec) { _execHandle = exec; _canExecHandle = canExec; }\n"
                "    public void SetExecuteFunc(ExecuteFunc exec) { _exec0 = exec; }\n"
                "    public void SetCanExecuteFunc(CanExecuteFunc canExec) { _canExec0 = canExec; }\n"
                "    public void SetExecuteFunc(ExecuteIntFunc exec) { _execInt = exec; }\n"
                "    public void SetCanExecuteFunc(CanExecuteIntFunc canExec) { _canExecInt = canExec; }\n"
                "    public void SetExecuteFunc(ExecuteStringFunc exec) { _execStr = exec; }\n"
                "    public void SetCanExecuteFunc(CanExecuteStringFunc canExec) { _canExecStr = canExec; }\n"
                "    public void SetExecuteFunc(ExecuteHandleFunc exec) { _execHandle = exec; }\n"
                "    public void SetCanExecuteFunc(CanExecuteHandleFunc canExec) { _canExecHandle = canExec; }\n"
                "    public void RaiseCanExecuteChanged() { Command_NotifyCanExecuteChanged(this); }\n"
                "    public bool CanExecute() { if (_canExec0 != null) { return _canExec0(); } return true; }\n"
                "    public bool CanExecute(int param) { if (_canExecInt != null) { return _canExecInt(param); } if (_canExec0 != null) { return _canExec0(); } return true; }\n"
                "    public bool CanExecute(string param) { if (_canExecStr != null) { return _canExecStr(param); } if (_canExec0 != null) { return _canExec0(); } return true; }\n"
                "    public bool CanExecute(handle param) { if (_canExecHandle != null) { return _canExecHandle(param); } if (_canExec0 != null) { return _canExec0(); } return true; }\n"
                "    public void Execute() { if (!CanExecute()) { return; } if (_exec0 != null) { _exec0(); } }\n"
                "    public void Execute(int param) { if (!CanExecute(param)) { return; } if (_execInt != null) { _execInt(param); return; } if (_exec0 != null) { _exec0(); } }\n"
                "    public void Execute(string param) { if (!CanExecute(param)) { return; } if (_execStr != null) { _execStr(param); return; } if (_exec0 != null) { _exec0(); } }\n"
                "    public void Execute(handle param) { if (!CanExecute(param)) { return; } if (_execHandle != null) { _execHandle(param); return; } if (_exec0 != null) { _exec0(); } }\n"
                "}\n"
                "public bool GUI_LoadComponent(handle root, string uri) { return false; }\n"
                "public bool GUI_SetDataContext<TOwner, TContext>(TOwner owner, TContext dataContext) where TOwner : class where TContext : class { return false; }\n"
                "public bool GUI_SetDataContextObject(object owner, object dataContext) { return false; }\n"
                "public object GUI_GetDataContextObject(object owner) { return null; }\n"
                "public bool GUI_LoadXaml(handle container, string xamlUri) { return false; }\n"
                "public bool Application_LoadResources(handle root, string uri) { return false; }\n"
                "public bool Application_SetResources(string uri) { return false; }\n"
                "public int UIElement_GetInt(handle elem, string propName) { return 0; }\n"
                "public bool UIElement_SetInt(handle elem, string propName, int val) { return false; }\n"
                "public float UIElement_GetWidth(handle elem) { return 0.0f; }\n"
                "public bool UIElement_SetWidth(handle elem, float width) { return false; }\n"
                "public float UIElement_GetProperty(handle elem, int propId) { return 0.0f; }\n"
                "public bool UIElement_SetProperty(handle elem, int propId, float val) { return false; }\n"
                "public double UIElement_GetDouble(handle elem, string propName) { return 0.0; }\n"
                "public bool UIElement_SetDouble(handle elem, string propName, double val) { return false; }\n"
                "public string PasswordBox_GetPassword(handle elem) { return \"\"; }\n"
                "public bool PasswordBox_ClearPassword(handle elem) { return false; }\n"
                "public bool ContentControl_LoadXaml(handle container, string xamlUri) { return false; }\n"
                "public bool Application_ApplyDictionary(string uri) { return false; }\n"
                "public bool Application_LoadTheme(handle root, string colorName, string accentName) { return false; }\n"
                "public bool UIElement_SetVisibility(handle elem, int visibility) { return false; }\n"
                "public int UIElement_GetVisibility(handle elem) { return 0; }\n"
                "public bool UIElement_Focus(handle elem) { return false; }\n"
                "public handle UIElement_FindName(handle root, string name) { return root; }\n"
                "public bool Storyboard_Begin(handle root, string storyboardKey) { return false; }\n"
                "public bool Storyboard_Stop(handle root, string storyboardKey) { return false; }\n"
                "public string UIElement_GetString(handle elem, string propName) { return \"\"; }\n"
                "public bool UIElement_SetString(handle elem, string propName, string val) { return false; }\n"
                "public bool UIElement_GetBool(handle elem, string propName) { return false; }\n"
                "public bool UIElement_SetBool(handle elem, string propName, bool val) { return false; }\n"
                "public float UIElement_GetFloat(handle elem, string propName) { return 0.0f; }\n"
                "public bool UIElement_SetFloat(handle elem, string propName, float val) { return false; }\n"
                "public string TreeView_GetSelectedHeader(handle elem) { return \"\"; }\n"
                "public bool UIElement_RegisterWrapper(handle elem, object wrapper) { return false; }\n"
                "public bool Command_NotifyCanExecuteChanged(object cmd) { return false; }\n"
                "public bool Observable_NotifyPropertyChanged(object source, string propertyName) { return false; }\n"
                "public bool TextBox_Clear(handle elem) { return false; }\n"
                "public bool TextBox_SelectAll(handle elem) { return false; }\n"
                "public bool TextBox_Select(handle elem, int start, int length) { return false; }\n"
                "public string TextBox_GetSelectedText(handle elem) { return \"\"; }\n"
                "public bool TextBox_SetSelectedText(handle elem, string text) { return false; }\n"
                "public bool TextBox_Copy(handle elem) { return false; }\n"
                "public bool TextBox_Cut(handle elem) { return false; }\n"
                "public bool TextBox_Paste(handle elem) { return false; }\n"
                "public string UIElement_FindResourceString(handle elem, string key) { return \"\"; }\n"
                "public bool UIElement_SetDynamicResourceString(handle elem, string key, string val) { return false; }\n"
                
                "public string ListBox_GetSelectedItemText(handle elem) { return \"\"; }\n"
                "public bool Window_Close(handle elem) { return false; }\n"
                "public class RelayCommand : DelegateCommand\n"
                "{\n"
                "    public RelayCommand() { }\n"
                "    public RelayCommand(ExecuteFunc exec) { _exec0 = exec; }\n"
                "    public RelayCommand(ExecuteFunc exec, CanExecuteFunc canExec) { _exec0 = exec; _canExec0 = canExec; }\n"
                "    public RelayCommand(ExecuteIntFunc exec) { _execInt = exec; }\n"
                "    public RelayCommand(ExecuteIntFunc exec, CanExecuteIntFunc canExec) { _execInt = exec; _canExecInt = canExec; }\n"
                "    public RelayCommand(ExecuteStringFunc exec) { _execStr = exec; }\n"
                "    public RelayCommand(ExecuteStringFunc exec, CanExecuteStringFunc canExec) { _execStr = exec; _canExecStr = canExec; }\n"
                "    public RelayCommand(ExecuteHandleFunc exec) { _execHandle = exec; }\n"
                "    public RelayCommand(ExecuteHandleFunc exec, CanExecuteHandleFunc canExec) { _execHandle = exec; _canExecHandle = canExec; }\n"
                "}\n"
                "public class PropertyChangedEventArgs\n"
                "{\n"
                "    public string PropertyName = \"\";\n"
                "    public PropertyChangedEventArgs() { }\n"
                "    public PropertyChangedEventArgs(string name) { PropertyName = name; }\n"
                "}\n"
                "public interface INotifyPropertyChanged\n"
                "{\n"
                "    void OnPropertyChanged(string propertyName);\n"
                "}\n"
                "public delegate void ExecuteGenericFunc<T>(T param);\n"
                "public delegate bool CanExecuteGenericFunc<T>(T param);\n"
                "public class TypedRelayCommand<T> : ICommand\n"
                "{\n"
                "    public ExecuteGenericFunc<T> _exec = null;\n"
                "    public CanExecuteGenericFunc<T> _canExec = null;\n"
                "    public TypedRelayCommand() { }\n"
                "    public TypedRelayCommand(ExecuteGenericFunc<T> exec) { _exec = exec; }\n"
                "    public TypedRelayCommand(ExecuteGenericFunc<T> exec, CanExecuteGenericFunc<T> canExec) { _exec = exec; _canExec = canExec; }\n"
                "    public void RaiseCanExecuteChanged() { Command_NotifyCanExecuteChanged(this); }\n"
                "    public bool CanExecute() { return true; }\n"
                "    public void Execute() { }\n"
                "    public bool CanExecute(T param) { if (_canExec != null) { return _canExec(param); } return true; }\n"
                "    public void Execute(T param) { if (!CanExecute(param)) { return; } if (_exec != null) { _exec(param); } }\n"
                "}\n"
                "public class TypedDelegateCommand<T> : ICommand\n"
                "{\n"
                "    public ExecuteGenericFunc<T> _exec = null;\n"
                "    public CanExecuteGenericFunc<T> _canExec = null;\n"
                "    public TypedDelegateCommand() { }\n"
                "    public TypedDelegateCommand(ExecuteGenericFunc<T> exec) { _exec = exec; }\n"
                "    public TypedDelegateCommand(ExecuteGenericFunc<T> exec, CanExecuteGenericFunc<T> canExec) { _exec = exec; _canExec = canExec; }\n"
                "    public void RaiseCanExecuteChanged() { Command_NotifyCanExecuteChanged(this); }\n"
                "    public bool CanExecute() { return true; }\n"
                "    public void Execute() { }\n"
                "    public bool CanExecute(T param) { if (_canExec != null) { return _canExec(param); } return true; }\n"
                "    public void Execute(T param) { if (!CanExecute(param)) { return; } if (_exec != null) { _exec(param); } }\n"
                "}\n"
                "public class ObservableObject : INotifyPropertyChanged\n"
                "{\n"
                "    public void OnPropertyChanged(string propertyName = \"\") { Observable_NotifyPropertyChanged(this, propertyName); }\n"
                "    public void RaisePropertyChanged(string propertyName = \"\") { Observable_NotifyPropertyChanged(this, propertyName); }\n"
                "    public bool SetProperty(ref int storage, int value, string propertyName = \"\") { if (storage == value) { return false; } storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "    public bool SetProperty(ref float storage, float value, string propertyName = \"\") { if (storage == value) { return false; } storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "    public bool SetProperty(ref double storage, double value, string propertyName = \"\") { if (storage == value) { return false; } storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "    public bool SetProperty(ref bool storage, bool value, string propertyName = \"\") { if (storage == value) { return false; } storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "    public bool SetProperty(ref string storage, string value, string propertyName = \"\") { if (storage == value) { return false; } storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "    public bool SetProperty<T>(ref T storage, T value) { storage = value; Observable_NotifyPropertyChanged(this, \"\"); return true; }\n"
                "    public bool SetProperty<T>(ref T storage, T value, string propertyName) { storage = value; Observable_NotifyPropertyChanged(this, propertyName); return true; }\n"
                "}\n"
                "public class ViewModelBase : ObservableObject { }\n"
                "public class BindableBase : ObservableObject { }\n"
                "public class RoutedEventArgs\n"
                "{\n"
                "    public handle OriginalSource;\n"
                "    public string EventName = \"\";\n"
                "    public bool Handled = false;\n"
                "}\n"
                "public class TextChangedEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public string Text = \"\";\n"
                "}\n"
                "public class SelectionChangedEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public int SelectedIndex = -1;\n"
                "}\n"
                "public class KeyEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public int KeyCode = 0;\n"
                "    public string Key = \"\";\n"
                "    public bool IsCtrlDown = false;\n"
                "    public bool IsShiftDown = false;\n"
                "    public bool IsAltDown = false;\n"
                "}\n"
                "public class MouseButtonEventArgs : RoutedEventArgs { }\n"
                "public class StartupEventArgs : RoutedEventArgs { }\n"
                "public class RequestNavigateEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public string Uri = \"\";\n"
                "}\n"
                "public class ExecutedRoutedEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public string Command = \"\";\n"
                "    public string Parameter = \"\";\n"
                "}\n"
                "public class CanExecuteRoutedEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public string Command = \"\";\n"
                "    public string Parameter = \"\";\n"
                "    public bool CanExecute = true;\n"
                "}\n"
                "public class RoutedPropertyChangedEventArgs : RoutedEventArgs\n"
                "{\n"
                "    public float NewValue = 0.0f;\n"
                "    public string NewText = \"\";\n"
                "}\n"
                "public enum Visibility { Visible = 0, Hidden = 1, Collapsed = 2 }\n"
                "public enum Orientation { Horizontal = 0, Vertical = 1 }\n"
                "public enum HorizontalAlignment { Left = 0, Center = 1, Right = 2, Stretch = 3 }\n"
                "public enum VerticalAlignment { Top = 0, Center = 1, Bottom = 2, Stretch = 3 }\n"
                "public enum SelectionMode { Single = 0, Multiple = 1, Extended = 2 }\n"
                "public enum ClickMode { Release = 0, Press = 1, Hover = 2 }\n"
                "public enum ExpandDirection { Down = 0, Up = 1, Left = 2, Right = 3 }\n"
                "public enum Dock { Left = 0, Top = 1, Right = 2, Bottom = 3 }\n"
                "public enum TabStripPlacement { Top = 0, Bottom = 1, Left = 2, Right = 3 }\n"
                "public delegate void RoutedEventHandler(object sender, RoutedEventArgs e);\n"
                "public delegate void RequestNavigateEventHandler(object sender, RequestNavigateEventArgs e);\n"
                "public delegate void ExecutedRoutedEventHandler(object sender, ExecutedRoutedEventArgs e);\n"
                "public delegate void CanExecuteRoutedEventHandler(object sender, CanExecuteRoutedEventArgs e);\n"
                "public delegate void TextChangedEventHandler(object sender, TextChangedEventArgs e);\n"
                "public delegate void SelectionChangedEventHandler(object sender, SelectionChangedEventArgs e);\n"
                "public delegate void KeyEventHandler(object sender, KeyEventArgs e);\n"
                "public delegate void MouseButtonEventHandler(object sender, MouseButtonEventArgs e);\n"
                "public delegate void RoutedPropertyChangedFloatHandler(object sender, RoutedPropertyChangedEventArgs e);\n"
                "[Observable]\n"
                "public class DependencyObject\n"
                "{\n"
                "    public handle __nativeRootHandle;\n"
                "    public static Visibility __ToVis(int v) { if (v == 1) { return Visibility.Hidden; } if (v == 2) { return Visibility.Collapsed; } return Visibility.Visible; }\n"
                "    public static int __FromVis(Visibility v) { if (v == Visibility.Hidden) { return 1; } if (v == Visibility.Collapsed) { return 2; } return 0; }\n"
                "    public static HorizontalAlignment __ToHAlign(int v) { if (v == 0) { return HorizontalAlignment.Left; } if (v == 1) { return HorizontalAlignment.Center; } if (v == 2) { return HorizontalAlignment.Right; } return HorizontalAlignment.Stretch; }\n"
                "    public static int __FromHAlign(HorizontalAlignment v) { if (v == HorizontalAlignment.Left) { return 0; } if (v == HorizontalAlignment.Center) { return 1; } if (v == HorizontalAlignment.Right) { return 2; } return 3; }\n"
                "    public static VerticalAlignment __ToVAlign(int v) { if (v == 0) { return VerticalAlignment.Top; } if (v == 1) { return VerticalAlignment.Center; } if (v == 2) { return VerticalAlignment.Bottom; } return VerticalAlignment.Stretch; }\n"
                "    public static int __FromVAlign(VerticalAlignment v) { if (v == VerticalAlignment.Top) { return 0; } if (v == VerticalAlignment.Center) { return 1; } if (v == VerticalAlignment.Bottom) { return 2; } return 3; }\n"
                "    public static SelectionMode __ToSelMode(int v) { if (v == 1) { return SelectionMode.Multiple; } if (v == 2) { return SelectionMode.Extended; } return SelectionMode.Single; }\n"
                "    public static int __FromSelMode(SelectionMode v) { if (v == SelectionMode.Multiple) { return 1; } if (v == SelectionMode.Extended) { return 2; } return 0; }\n"
                "    public static ClickMode __ToClickMode(int v) { if (v == 1) { return ClickMode.Press; } if (v == 2) { return ClickMode.Hover; } return ClickMode.Release; }\n"
                "    public static int __FromClickMode(ClickMode v) { if (v == ClickMode.Press) { return 1; } if (v == ClickMode.Hover) { return 2; } return 0; }\n"
                "    public static TabStripPlacement __ToTabPlacement(int v) { if (v == 1) { return TabStripPlacement.Bottom; } if (v == 2) { return TabStripPlacement.Left; } if (v == 3) { return TabStripPlacement.Right; } return TabStripPlacement.Top; }\n"
                "    public static int __FromTabPlacement(TabStripPlacement v) { if (v == TabStripPlacement.Bottom) { return 1; } if (v == TabStripPlacement.Left) { return 2; } if (v == TabStripPlacement.Right) { return 3; } return 0; }\n"
                "    public static ExpandDirection __ToExpandDir(int v) { if (v == 1) { return ExpandDirection.Up; } if (v == 2) { return ExpandDirection.Left; } if (v == 3) { return ExpandDirection.Right; } return ExpandDirection.Down; }\n"
                "    public static int __FromExpandDir(ExpandDirection v) { if (v == ExpandDirection.Up) { return 1; } if (v == ExpandDirection.Left) { return 2; } if (v == ExpandDirection.Right) { return 3; } return 0; }\n"
                "}\n"
                "public class UIElement : DependencyObject\n"
                "{\n"
                "    public event RoutedEventHandler Loaded;\n"
                "    public event RoutedEventHandler GotFocus;\n"
                "    public event RoutedEventHandler LostFocus;\n"
                "    public event RoutedEventHandler MouseEnter;\n"
                "    public event RoutedEventHandler MouseLeave;\n"
                "    public event MouseButtonEventHandler MouseLeftButtonDown;\n"
                "    public event MouseButtonEventHandler MouseLeftButtonUp;\n"
                "    public event KeyEventHandler KeyDown;\n"
                "    public event KeyEventHandler KeyUp;\n"
                "    public void __RaiseLoaded(RoutedEventArgs e) { Loaded(this, e); }\n"
                "    public void __RaiseGotFocus(RoutedEventArgs e) { GotFocus(this, e); }\n"
                "    public void __RaiseLostFocus(RoutedEventArgs e) { LostFocus(this, e); }\n"
                "    public void __RaiseMouseEnter(RoutedEventArgs e) { MouseEnter(this, e); }\n"
                "    public void __RaiseMouseLeave(RoutedEventArgs e) { MouseLeave(this, e); }\n"
                "    public void __RaiseMouseLeftButtonDown(MouseButtonEventArgs e) { MouseLeftButtonDown(this, e); }\n"
                "    public void __RaiseMouseLeftButtonUp(MouseButtonEventArgs e) { MouseLeftButtonUp(this, e); }\n"
                "    public void __RaiseKeyDown(KeyEventArgs e) { KeyDown(this, e); }\n"
                "    public void __RaiseKeyUp(KeyEventArgs e) { KeyUp(this, e); }\n"
                "    public Visibility Visibility { get { return DependencyObject.__ToVis(UIElement_GetVisibility(this.__nativeRootHandle)); } set { UIElement_SetVisibility(this.__nativeRootHandle, DependencyObject.__FromVis(value)); } }\n"
                "    public bool IsEnabled { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsEnabled\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsEnabled\", value); } }\n"
                "    public bool IsHitTestVisible { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsHitTestVisible\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsHitTestVisible\", value); } }\n"
                "    public float Opacity { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Opacity\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Opacity\", value); } }\n"
                "    public bool Focus() { return UIElement_Focus(this.__nativeRootHandle); }\n"
                "}\n"
                "public class FrameworkElement : UIElement\n"
                "{\n"
                "    public float Width { get { return UIElement_GetWidth(this.__nativeRootHandle); } set { UIElement_SetWidth(this.__nativeRootHandle, value); } }\n"
                "    public float Height { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Height\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Height\", value); } }\n"
                "    public float ActualWidth { get { return UIElement_GetFloat(this.__nativeRootHandle, \"ActualWidth\"); } }\n"
                "    public float ActualHeight { get { return UIElement_GetFloat(this.__nativeRootHandle, \"ActualHeight\"); } }\n"
                "    public HorizontalAlignment HorizontalAlignment { get { return DependencyObject.__ToHAlign(UIElement_GetInt(this.__nativeRootHandle, \"HorizontalAlignment\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"HorizontalAlignment\", DependencyObject.__FromHAlign(value)); } }\n"
                "    public VerticalAlignment VerticalAlignment { get { return DependencyObject.__ToVAlign(UIElement_GetInt(this.__nativeRootHandle, \"VerticalAlignment\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"VerticalAlignment\", DependencyObject.__FromVAlign(value)); } }\n"
                "    public string Tag { get { return UIElement_GetString(this.__nativeRootHandle, \"Tag\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Tag\", value); } }\n"
                "    public string ToolTip { get { return UIElement_GetString(this.__nativeRootHandle, \"ToolTip\"); } set { UIElement_SetString(this.__nativeRootHandle, \"ToolTip\", value); } }\n"
                "    public handle FindName(string name) { return UIElement_FindName(this.__nativeRootHandle, name); }\n"
                "    public string FindResource(string key) { return UIElement_FindResourceString(this.__nativeRootHandle, key); }\n"
                "    public bool SetDynamicResource(string key, string val) { return UIElement_SetDynamicResourceString(this.__nativeRootHandle, key, val); }\n"
                "    public bool BeginStoryboard(string storyboardKey) { return Storyboard_Begin(this.__nativeRootHandle, storyboardKey); }\n"
                "    public bool StopStoryboard(string storyboardKey) { return Storyboard_Stop(this.__nativeRootHandle, storyboardKey); }\n"
                "}\n"
                "public class Control : FrameworkElement\n"
                "{\n"
                "    public float FontSize { get { return UIElement_GetFloat(this.__nativeRootHandle, \"FontSize\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"FontSize\", value); } }\n"
                "}\n"
                "public class ContentControl : Control\n"
                "{\n"
                "    public string Content { get { return UIElement_GetString(this.__nativeRootHandle, \"Content\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Content\", value); } }\n"
                "    public bool LoadXaml(string uri) { return ContentControl_LoadXaml(this.__nativeRootHandle, uri); }\n"
                "}\n"
                "public class Window : ContentControl\n"
                "{\n"
                "    public string Title { get { return UIElement_GetString(this.__nativeRootHandle, \"Title\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Title\", value); } }\n"
                "    public bool Close() { return Window_Close(this.__nativeRootHandle); }\n"
                "}\n"
                "public class UserControl : ContentControl { }\n"
                "public class Page : ContentControl { }\n"
                "public class Button : ContentControl\n"
                "{\n"
                "    public event RoutedEventHandler Click;\n"
                "    public void __RaiseClick(RoutedEventArgs e) { Click(this, e); }\n"
                "    public ClickMode ClickMode { get { return DependencyObject.__ToClickMode(UIElement_GetInt(this.__nativeRootHandle, \"ClickMode\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"ClickMode\", DependencyObject.__FromClickMode(value)); } }\n"
                "}\n"
                "public class RepeatButton : Button { }\n"
                "public class Hyperlink : Button\n"
                "{\n"
                "    public event RequestNavigateEventHandler RequestNavigate;\n"
                "    public void __RaiseRequestNavigate(RequestNavigateEventArgs e) { RequestNavigate(this, e); }\n"
                "    public string NavigateUri { get { return UIElement_GetString(this.__nativeRootHandle, \"NavigateUri\"); } set { UIElement_SetString(this.__nativeRootHandle, \"NavigateUri\", value); } }\n"
                "}\n"
                "public class ToggleButton : Button\n"
                "{\n"
                "    public event RoutedEventHandler Checked;\n"
                "    public event RoutedEventHandler Unchecked;\n"
                "    public event RoutedEventHandler Indeterminate;\n"
                "    public void __RaiseChecked(RoutedEventArgs e) { Checked(this, e); }\n"
                "    public void __RaiseUnchecked(RoutedEventArgs e) { Unchecked(this, e); }\n"
                "    public void __RaiseIndeterminate(RoutedEventArgs e) { Indeterminate(this, e); }\n"
                "    public bool IsChecked { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsChecked\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsChecked\", value); } }\n"
                "    public bool IsThreeState { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsThreeState\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsThreeState\", value); } }\n"
                "}\n"
                "public class CheckBox : ToggleButton { }\n"
                "public class RadioButton : ToggleButton { }\n"
                "public class TextBlock : FrameworkElement\n"
                "{\n"
                "    public string Text { get { return UIElement_GetString(this.__nativeRootHandle, \"Text\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Text\", value); } }\n"
                "}\n"
                "public class TextBox : Control\n"
                "{\n"
                "    public event TextChangedEventHandler TextChanged;\n"
                "    public void __RaiseTextChanged(TextChangedEventArgs e) { TextChanged(this, e); }\n"
                "    public string Text { get { return UIElement_GetString(this.__nativeRootHandle, \"Text\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Text\", value); } }\n"
                "    public string Placeholder { get { return UIElement_GetString(this.__nativeRootHandle, \"Placeholder\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Placeholder\", value); } }\n"
                "    public bool IsReadOnly { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsReadOnly\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsReadOnly\", value); } }\n"
                "    public bool AcceptsReturn { get { return UIElement_GetBool(this.__nativeRootHandle, \"AcceptsReturn\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"AcceptsReturn\", value); } }\n"
                "    public int MaxLength { get { return UIElement_GetInt(this.__nativeRootHandle, \"MaxLength\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"MaxLength\", value); } }\n"
                "    public int MaxLines { get { return UIElement_GetInt(this.__nativeRootHandle, \"MaxLines\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"MaxLines\", value); } }\n"
                "    public int CaretIndex { get { return UIElement_GetInt(this.__nativeRootHandle, \"CaretIndex\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"CaretIndex\", value); } }\n"
                "    public int SelectionStart { get { return UIElement_GetInt(this.__nativeRootHandle, \"SelectionStart\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"SelectionStart\", value); } }\n"
                "    public int SelectionLength { get { return UIElement_GetInt(this.__nativeRootHandle, \"SelectionLength\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"SelectionLength\", value); } }\n"
                "    public string SelectedText { get { return TextBox_GetSelectedText(this.__nativeRootHandle); } set { TextBox_SetSelectedText(this.__nativeRootHandle, value); } }\n"
                "    public void Clear() { TextBox_Clear(this.__nativeRootHandle); }\n"
                "    public void SelectAll() { TextBox_SelectAll(this.__nativeRootHandle); }\n"
                "    public void Select(int start, int length) { TextBox_Select(this.__nativeRootHandle, start, length); }\n"
                "    public bool Copy() { return TextBox_Copy(this.__nativeRootHandle); }\n"
                "    public bool Cut() { return TextBox_Cut(this.__nativeRootHandle); }\n"
                "    public bool Paste() { return TextBox_Paste(this.__nativeRootHandle); }\n"
                "}\n"
                "public class PasswordBox : Control\n"
                "{\n"
                "    public event RoutedEventHandler PasswordChanged;\n"
                "    public void __RaisePasswordChanged(RoutedEventArgs e) { PasswordChanged(this, e); }\n"
                "    public string Password { get { return PasswordBox_GetPassword(this.__nativeRootHandle); } set { UIElement_SetString(this.__nativeRootHandle, \"Text\", value); } }\n"
                "    public string PasswordChar { get { return UIElement_GetString(this.__nativeRootHandle, \"PasswordChar\"); } set { UIElement_SetString(this.__nativeRootHandle, \"PasswordChar\", value); } }\n"
                "    public bool Clear() { return PasswordBox_ClearPassword(this.__nativeRootHandle); }\n"
                "}\n"
                "public class Slider : Control\n"
                "{\n"
                "    public event RoutedPropertyChangedFloatHandler ValueChanged;\n"
                "    public void __RaiseValueChanged(RoutedPropertyChangedEventArgs e) { ValueChanged(this, e); }\n"
                "    public float Value { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Value\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Value\", value); } }\n"
                "    public float Minimum { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Minimum\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Minimum\", value); } }\n"
                "    public float Maximum { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Maximum\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Maximum\", value); } }\n"
                "    public float SmallChange { get { return UIElement_GetFloat(this.__nativeRootHandle, \"SmallChange\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"SmallChange\", value); } }\n"
                "    public float TickFrequency { get { return UIElement_GetFloat(this.__nativeRootHandle, \"TickFrequency\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"TickFrequency\", value); } }\n"
                "    public bool IsSnapToTickEnabled { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsSnapToTickEnabled\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsSnapToTickEnabled\", value); } }\n"
                "    public bool IsDirectionReversed { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsDirectionReversed\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsDirectionReversed\", value); } }\n"
                "}\n"
                "public class ProgressBar : Control\n"
                "{\n"
                "    public float Value { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Value\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Value\", value); } }\n"
                "    public float Maximum { get { return UIElement_GetFloat(this.__nativeRootHandle, \"Maximum\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"Maximum\", value); } }\n"
                "    public bool IsIndeterminate { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsIndeterminate\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsIndeterminate\", value); } }\n"
                "}\n"
                "public class ListBox : Control\n"
                "{\n"
                "    public event SelectionChangedEventHandler SelectionChanged;\n"
                "    public void __RaiseSelectionChanged(SelectionChangedEventArgs e) { SelectionChanged(this, e); }\n"
                "    public int SelectedIndex { get { return UIElement_GetInt(this.__nativeRootHandle, \"SelectedIndex\"); } set { UIElement_SetInt(this.__nativeRootHandle, \"SelectedIndex\", value); } }\n"
                "    public SelectionMode SelectionMode { get { return DependencyObject.__ToSelMode(UIElement_GetInt(this.__nativeRootHandle, \"SelectionMode\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"SelectionMode\", DependencyObject.__FromSelMode(value)); } }\n"
                "    public string SelectedItemText { get { return ListBox_GetSelectedItemText(this.__nativeRootHandle); } }\n"
                "}\n"
                "public class ComboBox : ListBox\n"
                "{\n"
                "    public bool IsDropDownOpen { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsDropDownOpen\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsDropDownOpen\", value); } }\n"
                "    public bool IsEditable { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsEditable\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsEditable\", value); } }\n"
                "    public string Text { get { return UIElement_GetString(this.__nativeRootHandle, \"Text\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Text\", value); } }\n"
                "    public string Placeholder { get { return UIElement_GetString(this.__nativeRootHandle, \"Placeholder\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Placeholder\", value); } }\n"
                "    public float MaxDropDownHeight { get { return UIElement_GetFloat(this.__nativeRootHandle, \"MaxDropDownHeight\"); } set { UIElement_SetFloat(this.__nativeRootHandle, \"MaxDropDownHeight\", value); } }\n"
                "}\n"
                "public class TabControl : ListBox\n"
                "{\n"
                "    public TabStripPlacement TabStripPlacement { get { return DependencyObject.__ToTabPlacement(UIElement_GetInt(this.__nativeRootHandle, \"TabStripPlacement\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"TabStripPlacement\", DependencyObject.__FromTabPlacement(value)); } }\n"
                "}\n"
                "public class Expander : ContentControl\n"
                "{\n"
                "    public event RoutedEventHandler Expanded;\n"
                "    public event RoutedEventHandler Collapsed;\n"
                "    public void __RaiseExpanded(RoutedEventArgs e) { Expanded(this, e); }\n"
                "    public void __RaiseCollapsed(RoutedEventArgs e) { Collapsed(this, e); }\n"
                "    public string Header { get { return UIElement_GetString(this.__nativeRootHandle, \"Header\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Header\", value); } }\n"
                "    public bool IsExpanded { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsExpanded\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsExpanded\", value); } }\n"
                "    public ExpandDirection ExpandDirection { get { return DependencyObject.__ToExpandDir(UIElement_GetInt(this.__nativeRootHandle, \"ExpandDirection\")); } set { UIElement_SetInt(this.__nativeRootHandle, \"ExpandDirection\", DependencyObject.__FromExpandDir(value)); } }\n"
                "}\n"
                "public class TreeView : Control\n"
                "{\n"
                "    public event RoutedPropertyChangedFloatHandler SelectedItemChanged;\n"
                "    public void __RaiseSelectedItemChanged(RoutedPropertyChangedEventArgs e) { SelectedItemChanged(this, e); }\n"
                "    public string SelectedHeader { get { return TreeView_GetSelectedHeader(this.__nativeRootHandle); } }\n"
                "}\n"
                "public class TreeViewItem : Control\n"
                "{\n"
                "    public string Header { get { return UIElement_GetString(this.__nativeRootHandle, \"Header\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Header\", value); } }\n"
                "    public bool IsExpanded { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsExpanded\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsExpanded\", value); } }\n"
                "    public bool IsSelected { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsSelected\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsSelected\", value); } }\n"
                "}\n"
                "public class MenuItem : Button\n"
                "{\n"
                "    public string Header { get { return UIElement_GetString(this.__nativeRootHandle, \"Header\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Header\", value); } }\n"
                "    public bool IsCheckable { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsCheckable\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsCheckable\", value); } }\n"
                "    public bool IsChecked { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsChecked\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsChecked\", value); } }\n"
                "    public bool IsSubmenuOpen { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsSubmenuOpen\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsSubmenuOpen\", value); } }\n"
                "}\n"
                "public class StatusBarItem : ContentControl { }\n"
                "public class ListView : ListBox { }\n"
                "public class Border : FrameworkElement { }\n"
                "public class Panel : FrameworkElement { }\n"
                "public class Grid : Panel { }\n"
                "public class StackPanel : Panel { }\n"
                "public class DockPanel : Panel { }\n"
                "public class WrapPanel : Panel { }\n"
                "public class UniformGrid : Panel { }\n"
                "public class Canvas : Panel { }\n"
                "public class ScrollViewer : ContentControl { }\n"
                "public class Image : FrameworkElement\n"
                "{\n"
                "    public string Source { get { return UIElement_GetString(this.__nativeRootHandle, \"Source\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Source\", value); } }\n"
                "}\n"
                "public class GroupBox : Expander { }\n"
                "public class TabItem : Expander { }\n"
                "public class Menu : Control { }\n"
                "public class ToolBar : Control\n"
                "{\n"
                "    public string Header { get { return UIElement_GetString(this.__nativeRootHandle, \"Header\"); } set { UIElement_SetString(this.__nativeRootHandle, \"Header\", value); } }\n"
                "}\n"
                "public class StatusBar : Control { }\n"
                "public class Popup : FrameworkElement\n"
                "{\n"
                "    public bool IsOpen { get { return UIElement_GetBool(this.__nativeRootHandle, \"IsOpen\"); } set { UIElement_SetBool(this.__nativeRootHandle, \"IsOpen\", value); } }\n"
                "}\n"
                "public class ToolTip : Popup { }\n"
                "public class ContextMenu : Control { }\n"
                "public class GridSplitter : Control { }\n"
                "public class RiveControl : FrameworkElement { }\n"
                "public Application Application_GetCurrent() { return null; }\n"
                "public class Application\n"
                "{\n"
                "    public static Application Current { get { return Application_GetCurrent(); } }\n"
                "    public string StartupUri = \"\";\n"
                "    public virtual void OnStartup(StartupEventArgs e) { }\n"
                "    public bool LoadResources(handle root, string uri) { return Application_LoadResources(root, uri); }\n"
                "    public bool ApplyDictionary(string uri) { return Application_ApplyDictionary(uri); }\n"
                "    public bool LoadTheme(handle root, string colorName, string accentName) { return Application_LoadTheme(root, colorName, accentName); }\n"
                "}\n";
            allSources.insert(allSources.begin(), SourceFile{"__AeroUiSdk.rs", std::move(sdkCode)});
        }
    }

    // If RealScript sources were supplied, run single-pass Compilation + Bytecode Lowering + Link
    if (!allSources.empty()) {
        rs_comp::Compilation compilation(std::move(allSources));
        const auto build = compilation.build();
        for (const auto& d : build.diagnostics.items()) {
            result.diagnostics.push_back(d.code + ": " + d.message);
        }
        if (build.diagnostics.hasErrors()) {
            result.succeeded = false;
            return result;
        }

        rs_bc::Lowerer lowerer;
        for (const auto& mir : build.modules) {
            auto bc = lowerer.lower(mir);
            rs_diag::DiagnosticBag verifyDiag;
            if (!rs_bc::verifyModule(bc, verifyDiag)) {
                for (const auto& d : verifyDiag.items()) {
                    result.diagnostics.push_back(d.code + ": " + d.message);
                }
                result.succeeded = false;
                return result;
            }
            if (bc.name == "Aero.UI") {
                bc.functions.erase(
                    std::remove_if(
                        bc.functions.begin(),
                        bc.functions.end(),
                        [](const auto& fn) {
                            const auto sep = fn.name.find("::");
                            const std::string_view local = (sep != std::string::npos)
                                ? std::string_view(fn.name).substr(sep + 2u)
                                : std::string_view(fn.name);
                            // Keep class methods and generated factory helpers, strip free native SDK stubs
                            if (local.rfind("__AeroCreateInstance_", 0) == 0 ||
                                local == "CreateMainWindow" ||
                                local == "CreateViewModel") {
                                return false;
                            }
                            return local.find('.') == std::string_view::npos;
                        }),
                    bc.functions.end());
            } else {
                bc.functions.erase(
                    std::remove_if(
                        bc.functions.begin(),
                        bc.functions.end(),
                        [](const auto& fn) {
                            return fn.name.find("__Aero_SetDataContext") != std::string::npos || fn.name.find("Aero_SetDataContext__") != std::string::npos || fn.name.find("Aero_LoadComponent__") != std::string::npos || fn.name.find("Aero_LoadResources__") != std::string::npos;
                        }),
                    bc.functions.end());
            }
            result.bytecodeModules.push_back(bc);
        }

        RuntimeError linkErr{};
        auto linked = ProgramImage::link(result.bytecodeModules, linkErr);
        if (!linked) {
            result.diagnostics.push_back("Link error: " + linkErr.message);
            result.succeeded = false;
            return result;
        }
        result.programImage = std::make_shared<const ProgramImage>(std::move(*linked));

        if (!rootDataType.empty() && pkg.rootDataTypeName.empty()) {
            pkg.rootDataTypeName = rootDataType;
        }
        BindPackageAgainstProgram(pkg, *result.programImage, rootDataType);

        if (input.emitAotCpp) {
            rs_aot::CppGenerator aotGen;
            rs_diag::DiagnosticBag aotDiag;
            rs_aot::GenerationOptions genOpts{};
            genOpts.programName = !pkg.className.empty()
                ? rs_aot::sanitizeCppIdentifier(pkg.className)
                : "AeroScriptProgram";
            genOpts.cppNamespace = !input.aotCppNamespace.empty()
                ? input.aotCppNamespace
                : "aero::aot_generated";
            genOpts.emitLineDirectives = false;
            auto aotModules = build.modules;
            for (auto& m : aotModules) {
                if (m.name == "Aero.UI") {
                    m.functions.erase(
                        std::remove_if(
                            m.functions.begin(),
                            m.functions.end(),
                            [](const auto& fn) {
                                const auto sep = fn.name.find("::");
                                const std::string_view local = (sep != std::string::npos)
                                    ? std::string_view(fn.name).substr(sep + 2u)
                                    : std::string_view(fn.name);
                                if (local.rfind("__AeroCreateInstance_", 0) == 0 ||
                                    local == "CreateMainWindow" ||
                                    local == "CreateViewModel") {
                                    return false;
                                }
                                return local.find('.') == std::string_view::npos;
                            }),
                        m.functions.end());
                } else {
                    m.functions.erase(
                        std::remove_if(
                            m.functions.begin(),
                            m.functions.end(),
                            [](const auto& fn) {
                                return fn.name.find("__Aero_SetDataContext") != std::string::npos || fn.name.find("Aero_SetDataContext__") != std::string::npos || fn.name.find("Aero_LoadComponent__") != std::string::npos || fn.name.find("Aero_LoadResources__") != std::string::npos;
                            }),
                        m.functions.end());
                }
            }
            const auto generatedScriptAot = aotGen.generate(aotModules, aotDiag, genOpts);
            for (const auto& d : aotDiag.items()) {
                result.diagnostics.push_back(d.code + ": " + d.message);
            }
            if (aotDiag.hasErrors()) {
                result.succeeded = false;
                return result;
            }
            result.aotCpp = GenerateAotCppFromPackage(
                pkg,
                input.aotCppNamespace,
                input.aotViewBuilderName);
            result.aotCpp.scriptAotHeader = generatedScriptAot.header;
            std::string inlineHeader = generatedScriptAot.header;
            if (const std::size_t pragmaPos = inlineHeader.find("#pragma once"); pragmaPos != std::string::npos) {
                inlineHeader.erase(pragmaPos, 12);
            }
            std::string selfContainedSource = generatedScriptAot.source;
            const std::string includeDirective = "#include \"realscript_aot_generated.h\"";
            if (const std::size_t incPos = selfContainedSource.find(includeDirective); incPos != std::string::npos) {
                selfContainedSource.replace(incPos, includeDirective.size(), inlineHeader);
            }
            result.aotCpp.scriptAotSource = std::move(selfContainedSource);
            result.aotCpp.scriptAotContentHash = generatedScriptAot.contentHash;
        }
    }

    if (input.emitAotCpp && result.aotCpp.source.empty()) {
        result.aotCpp = GenerateAotCppFromPackage(
            pkg,
            input.aotCppNamespace,
            input.aotViewBuilderName);
    }

    result.axbBytes = pkg.Serialize();
    if (pkg.rootTypeName == "ResourceDictionary" && !effectiveXamlPath.empty()) {
        AxbTemplateRegistry::Instance().RegisterDictionary(effectiveXamlPath, pkg);
        if (input.xamlPath != effectiveXamlPath && !input.xamlPath.empty()) {
            AxbTemplateRegistry::Instance().RegisterDictionary(input.xamlPath, pkg);
        }
    }
    if (!pkg.className.empty()) {
        AxbTemplateRegistry::Instance().Register(pkg.className, pkg);
    } else if (!effectiveXamlPath.empty() || pkg.rootTypeName == "ResourceDictionary") {
        AxbTemplateRegistry::Instance().RegisterResources(pkg);
    }
    result.succeeded = true;
    return result;
}

namespace {

std::string FormatFloatLiteral(float v) {
    if (std::isinf(v)) {
        return (v < 0.0f) ? "-std::numeric_limits<float>::infinity()" : "std::numeric_limits<float>::infinity()";
    }
    if (std::isnan(v)) {
        return "-1.0f";
    }
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6ff", v);
    return std::string(buf);
}

std::string EscapeAotCppString(std::string_view sv) {
    std::string out;
    out.reserve(sv.size() + 8u);
    for (std::size_t i = 0u; i < sv.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(sv[i]);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '?':
                if (i + 1u < sv.size() && sv[i + 1u] == '?') {
                    out += "\\?";
                } else {
                    out.push_back('?');
                }
                break;
            default:
                if (c < 0x20u || c >= 0x7Fu) {
                    // Use fixed 3-digit octal escape so following [0-9a-fA-F] chars are never eaten as hex!
                    char oct[5];
                    oct[0] = '\\';
                    oct[1] = static_cast<char>('0' + ((c >> 6u) & 0x07u));
                    oct[2] = static_cast<char>('0' + ((c >> 3u) & 0x07u));
                    oct[3] = static_cast<char>('0' + (c & 0x07u));
                    oct[4] = '\0';
                    out.append(oct, 4u);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string EmitUnifiedValueLiteralCpp(const UnifiedValue& val) {
    if (const auto* b = std::get_if<bool>(&val)) {
        return *b ? "aero::UnifiedValue{true}" : "aero::UnifiedValue{false}";
    }
    if (const auto* i = std::get_if<std::int64_t>(&val)) {
        return "aero::value::FromInt(" + std::to_string(*i) + "LL)";
    }
    if (const auto* f = std::get_if<FloatValue>(&val)) {
        return "aero::MakeFloat(" + FormatFloatLiteral(f->value) + ")";
    }
    Float4Value f4{};
    if (val.tryGetFloat4(f4)) {
        return "aero::MakeFloat4InlineFast(" +
               FormatFloatLiteral(f4.x) + ", " +
               FormatFloatLiteral(f4.y) + ", " +
               FormatFloatLiteral(f4.z) + ", " +
               FormatFloatLiteral(f4.w) + ")";
    }
    if (const auto* s = std::get_if<StringValue>(&val)) {
        return "aero::value::FromString(\"" + EscapeAotCppString(s->str()) + "\")";
    }
    return "aero::UnifiedValue{}";
}

void AppendStoryboardPopulateCpp(
    std::string& src,
    const AxbStoryboard& sb,
    const std::string& sbVar,
    const std::string& indent) {
    if (!sb.key.empty()) {
        src += indent + sbVar + ".key = \"" + EscapeAotCppString(sb.key) + "\";\n";
    }
    for (const auto& tl : sb.timelines) {
        src += indent + "{\n";
        src += indent + "    aero::AxbAnimationTimeline tl{};\n";
        if (!tl.targetName.empty()) {
            src += indent + "    tl.targetName = \"" + EscapeAotCppString(tl.targetName) + "\";\n";
        }
        src += indent + "    tl.targetPropertyId = " + std::to_string(tl.targetPropertyId) + "u;\n";
        src += indent + "    tl.beginTimeSeconds = " + FormatFloatLiteral(tl.beginTimeSeconds) + ";\n";
        src += indent + "    tl.durationSeconds = " + FormatFloatLiteral(tl.durationSeconds) + ";\n";
        src += indent + "    tl.hasFrom = " + std::string(tl.hasFrom ? "true" : "false") + ";\n";
        if (tl.hasFrom) {
            src += indent + "    tl.fromValue = " + EmitUnifiedValueLiteralCpp(tl.fromValue) + ";\n";
        }
        src += indent + "    tl.hasTo = " + std::string(tl.hasTo ? "true" : "false") + ";\n";
        src += indent + "    tl.isBy = " + std::string(tl.isBy ? "true" : "false") + ";\n";
        if (tl.hasTo) {
            src += indent + "    tl.toValue = " + EmitUnifiedValueLiteralCpp(tl.toValue) + ";\n";
        }
        src += indent + "    tl.easingKind = static_cast<aero::EasingFunctionKind>(" +
               std::to_string(static_cast<unsigned>(tl.easingKind)) + "u);\n";
        src += indent + "    tl.easingMode = static_cast<aero::EasingMode>(" +
               std::to_string(static_cast<unsigned>(tl.easingMode)) + "u);\n";
        src += indent + "    tl.fillBehavior = static_cast<aero::FillBehavior>(" +
               std::to_string(static_cast<unsigned>(tl.fillBehavior)) + "u);\n";
        src += indent + "    tl.autoReverse = " + std::string(tl.autoReverse ? "true" : "false") + ";\n";
        src += indent + "    tl.repeatCount = " + std::to_string(tl.repeatCount) + ";\n";
        for (const auto& kf : tl.keyFrames) {
            src += indent + "    {\n";
            src += indent + "        aero::AxbKeyFrame kf{};\n";
            src += indent + "        kf.timeSeconds = " + FormatFloatLiteral(kf.timeSeconds) + ";\n";
            src += indent + "        kf.value = " + EmitUnifiedValueLiteralCpp(kf.value) + ";\n";
            src += indent + "        kf.easingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(kf.easingKind)) + "u);\n";
            src += indent + "        kf.easingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(kf.easingMode)) + "u);\n";
            src += indent + "        kf.isDiscrete = " + std::string(kf.isDiscrete ? "true" : "false") + ";\n";
            src += indent + "        tl.keyFrames.push_back(std::move(kf));\n";
            src += indent + "    }\n";
        }
        src += indent + "    " + sbVar + ".timelines.push_back(std::move(tl));\n";
        src += indent + "}\n";
    }
}

void AppendSetterListPopulateCpp(
    std::string& src,
    const std::vector<AxbPropertySetter>& setters,
    const std::string& targetVecExpr,
    const std::string& indent) {
    for (const auto& s : setters) {
        src += indent + "{\n";
        src += indent + "    aero::AxbPropertySetter ps{};\n";
        if (!s.targetName.empty()) {
            src += indent + "    ps.targetName = \"" + EscapeAotCppString(s.targetName) + "\";\n";
        }
        src += indent + "    ps.propId = " + std::to_string(s.propId) + "u;\n";
        src += indent + "    ps.value = " + EmitUnifiedValueLiteralCpp(s.value) + ";\n";
        src += indent + "    " + targetVecExpr + ".push_back(std::move(ps));\n";
        src += indent + "}\n";
    }
}

} // namespace

AeroViewAotCppOutput AeroCompiler::GenerateAotCppFromPackage(
    const AxbPackage& package,
    std::string_view cppNamespace,
    std::string_view builderFunctionName) {
    AeroViewAotCppOutput out{};
    out.cppNamespace = cppNamespace.empty()
        ? "aero::aot_generated"
        : std::string(cppNamespace);

    if (!builderFunctionName.empty()) {
        out.builderFunctionName = std::string(builderFunctionName);
    } else if (!package.className.empty()) {
        out.builderFunctionName = "BuildView_" + rs_aot::sanitizeCppIdentifier(package.className);
    } else {
        out.builderFunctionName = "BuildAeroView";
    }

    // 1. Emit C++17 Header (.hpp)
    std::string hdr;
    hdr += "#pragma once\n\n";
    hdr += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    hdr += "#include <cstdint>\n";
    hdr += "#include <limits>\n";
    hdr += "#include \"aero/controls/Controls.hpp\"\n";
    hdr += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n\n";
    hdr += "namespace " + out.cppNamespace + " {\n\n";
    hdr += "bool " + out.builderFunctionName + "(\n";
    hdr += "    aero::UIElement* rootControl,\n";
    hdr += "    aero::ExecutionDomainContext* domains = nullptr,\n";
    hdr += "    aero::DualTrackBindingEngine* bindingEngine = nullptr,\n";
    hdr += "    aero::ObjectRef dataContext = {},\n";
    hdr += "    const aero::Interpreter* eventInterpreter = nullptr,\n";
    hdr += "    aero::rs_aot::ExecutionContext* eventAotContext = nullptr);\n\n";
    hdr += "} // namespace " + out.cppNamespace + "\n";
    out.header = std::move(hdr);

    // 2. Emit C++17 Source (.cpp) - unrolled batch creation, static properties, MTSDF, Styles/Storyboards & bindings
    bool hasNestedMultiInBindingChains = false;
    for (const auto& ch : package.bindingChains) {
        if (ch.nestedMultiBinding != nullptr) {
            hasNestedMultiInBindingChains = true;
            break;
        }
    }
    bool hasP1StylePayload = false;
    for (const auto& st : package.styles) {
        if (!st.controlTemplateKey.empty() || st.inlineControlTemplate != nullptr) {
            hasP1StylePayload = true;
            break;
        }
        for (const auto& tr : st.triggers) {
            if (!tr.enterActions.empty() || !tr.exitActions.empty()) {
                hasP1StylePayload = true;
                break;
            }
            for (const auto& c : tr.conditions) {
                if (!c.converterKey.empty() || c.converterSymbolId != 0) {
                    hasP1StylePayload = true;
                    break;
                }
            }
            if (hasP1StylePayload) break;
        }
        if (hasP1StylePayload) break;
        for (const auto& et : st.eventTriggers) {
            if (!et.actions.empty() || !et.conditions.empty()) {
                hasP1StylePayload = true;
                break;
            }
        }
        if (hasP1StylePayload) break;
    }
    const bool needSharedPkg =
        !package.multiBindings.empty() ||
        package.HasMotionSection() ||
        !package.dataTemplates.empty() ||
        !package.controlTemplates.empty() ||
        hasNestedMultiInBindingChains ||
        hasP1StylePayload;

    std::vector<std::uint8_t> sharedPkgBytes;
    if (needSharedPkg) {
        sharedPkgBytes = package.Serialize();
    }
    const bool emitSharedPkg = needSharedPkg && !sharedPkgBytes.empty();

    std::string src;
    src += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    src += "#include <limits>\n";
    src += "#include <mutex>\n";
    src += "#include \"aero/animation/AnimationEngine.hpp\"\n";
    src += "#include \"aero/controls/Controls.hpp\"\n";
    src += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n";
    src += "#include \"realscript/runtime/Runtime.h\"\n\n";
    src += "namespace " + out.cppNamespace + " {\n\n";
    src += "bool " + out.builderFunctionName + "(\n";
    src += "    aero::UIElement* rootControl,\n";
    src += "    aero::ExecutionDomainContext* domains,\n";
    src += "    aero::DualTrackBindingEngine* bindingEngine,\n";
    src += "    aero::ObjectRef dataContext,\n";
    src += "    const aero::Interpreter* eventInterpreter,\n";
    src += "    aero::rs_aot::ExecutionContext* eventAotContext) {\n";
    src += "    if (!rootControl) {\n";
    src += "        return false;\n";
    src += "    }\n";
    src += "    aero::EnsureControlTypesRegistered();\n";

    if (emitSharedPkg) {
        src += "    static std::mutex sAotSharedPkgMutex;\n";
        src += "    static const aero::ProgramImage* sAotBoundProgram = nullptr;\n";
        src += "    static aero::AxbPackage* const sAotSharedPkgPtr = []() {\n";
        src += "        auto* p = new aero::AxbPackage();\n";
        src += "        static constexpr std::uint8_t kAotSharedPkgBytes[] = {";
        for (std::size_t b = 0; b < sharedPkgBytes.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(sharedPkgBytes[b])) + "u";
        }
        src += "};\n";
        src += "        (void)aero::AxbPackage::Deserialize(kAotSharedPkgBytes, sizeof(kAotSharedPkgBytes), *p);\n";
        src += "        p->PinValuesInPools();\n";
        src += "        aero::rs_rt::registerExternalValuePoolRootMarker(\n";
        src += "            p,\n";
        src += "            [p]() noexcept {\n";
        src += "                std::lock_guard<std::mutex> lock(sAotSharedPkgMutex);\n";
        src += "                p->MarkValuesInPools();\n";
        src += "            });\n";
        src += "        return p;\n";
        src += "    }();\n";
        src += "    aero::AxbPackage& sAotSharedPkg = *sAotSharedPkgPtr;\n";
        src += "    bool sAotReboundSharedPkg = false;\n";
        src += "    {\n";
        src += "        std::lock_guard<std::mutex> lock(sAotSharedPkgMutex);\n";
        src += "        const aero::ProgramImage* liveProg = bindingEngine ? bindingEngine->Program() : nullptr;\n";
        src += "        if (liveProg && liveProg != sAotBoundProgram) {\n";
        src += "            (void)sAotSharedPkg.BindAgainstProgram(*liveProg, {}, domains);\n";
        src += "            sAotSharedPkg.PinValuesInPools();\n";
        src += "            sAotBoundProgram = liveProg;\n";
        src += "            sAotReboundSharedPkg = true;\n";
        src += "        }\n";
        src += "    }\n";
        src += "    (void)sAotReboundSharedPkg;\n";
    }

    // Emit pre-baked MTSDF vector path entries so pure C++17 AOT views have 0 runtime SVG parsing
    for (std::size_t mpIdx = 0; mpIdx < package.mtsdfPaths.size(); ++mpIdx) {
        const auto& mp = package.mtsdfPaths[mpIdx];
        if (mp.pathHash == 0 || mp.mtsdfRgbaPixels.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindMtsdfPath(" +
               std::to_string(mp.pathHash) + "ULL)) {\n";
        const std::string arrName = "kMtsdfPixels_" + std::to_string(mpIdx);
        src += "        static constexpr std::uint8_t " + arrName + "[] = {";
        for (std::size_t b = 0; b < mp.mtsdfRgbaPixels.size(); ++b) {
            if (b > 0) src += ",";
            src += std::to_string(static_cast<unsigned>(mp.mtsdfRgbaPixels[b])) + "u";
        }
        src += "};\n";
        src += "        aero::AxbMtsdfPathEntry entry{};\n";
        src += "        entry.pathHash = " + std::to_string(mp.pathHash) + "ULL;\n";
        src += "        entry.boundsX = " + FormatFloatLiteral(mp.boundsX) + ";\n";
        src += "        entry.boundsY = " + FormatFloatLiteral(mp.boundsY) + ";\n";
        src += "        entry.boundsW = " + FormatFloatLiteral(mp.boundsW) + ";\n";
        src += "        entry.boundsH = " + FormatFloatLiteral(mp.boundsH) + ";\n";
        src += "        entry.u0 = " + FormatFloatLiteral(mp.u0) + ";\n";
        src += "        entry.v0 = " + FormatFloatLiteral(mp.v0) + ";\n";
        src += "        entry.u1 = " + FormatFloatLiteral(mp.u1) + ";\n";
        src += "        entry.v1 = " + FormatFloatLiteral(mp.v1) + ";\n";
        src += "        entry.atlasWidth = " + std::to_string(mp.atlasWidth) + "u;\n";
        src += "        entry.atlasHeight = " + std::to_string(mp.atlasHeight) + "u;\n";
        src += "        entry.cellWidth = " + std::to_string(mp.cellWidth) + "u;\n";
        src += "        entry.cellHeight = " + std::to_string(mp.cellHeight) + "u;\n";
        src += "        entry.pxRange = " + FormatFloatLiteral(mp.pxRange) + ";\n";
        src += "        entry.mtsdfRgbaPixels.assign(" + arrName + ", " + arrName + " + sizeof(" + arrName + "));\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterMtsdfPath(std::move(entry));\n";
        src += "    }\n";
    }

    // Emit keyed scalar/color/string resources into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& resPair : package.resources) {
        if (resPair.first.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindResource(\"" +
               EscapeAotCppString(resPair.first) + "\")) {\n";
        src += "        aero::AxbTemplateRegistry::Instance().RegisterResource(\"" +
               EscapeAotCppString(resPair.first) + "\", " +
               EmitUnifiedValueLiteralCpp(resPair.second) + ");\n";
        src += "    }\n";
    }

    // Emit pre-compiled Storyboards & Styles into AxbTemplateRegistry for zero-XML AOT runtime
    for (const auto& sb : package.storyboards) {
        if (sb.key.empty()) {
            continue;
        }
        src += "    if (!aero::AxbTemplateRegistry::Instance().FindStoryboard(\"" +
               EscapeAotCppString(sb.key) + "\")) {\n";
        src += "        aero::AxbStoryboard sbDef{};\n";
        AppendStoryboardPopulateCpp(src, sb, "sbDef", "        ");
        src += "        aero::AxbTemplateRegistry::Instance().RegisterStoryboard(std::move(sbDef));\n";
        src += "    }\n";
    }

    for (std::size_t stIdx = 0; stIdx < package.styles.size(); ++stIdx) {
        const auto& st = package.styles[stIdx];
        if (!st.key.empty()) {
            src += "    if (" + std::string(emitSharedPkg ? "sAotReboundSharedPkg || " : "") +
                   "!aero::AxbTemplateRegistry::Instance().FindStyle(\"" +
                   EscapeAotCppString(st.key) + "\")) {\n";
        } else {
            src += "    {\n";
        }
        src += "        aero::AxbStyleDefinition stDef{};\n";
        if (!st.key.empty()) {
            src += "        stDef.key = \"" + EscapeAotCppString(st.key) + "\";\n";
        }
        if (!st.targetTypeName.empty()) {
            src += "        stDef.targetTypeName = \"" + EscapeAotCppString(st.targetTypeName) + "\";\n";
        }
        if (!st.basedOnKey.empty()) {
            src += "        stDef.basedOnKey = \"" + EscapeAotCppString(st.basedOnKey) + "\";\n";
        }
        if (!st.controlTemplateKey.empty()) {
            src += "        stDef.controlTemplateKey = \"" + EscapeAotCppString(st.controlTemplateKey) + "\";\n";
        }
        AppendSetterListPopulateCpp(src, st.setters, "stDef.setters", "        ");
        for (const auto& trig : st.triggers) {
            src += "        {\n";
            src += "            aero::AxbStyleTrigger trigDef{};\n";
            src += "            trigDef.overrideLocalValues = " +
                   std::string(trig.overrideLocalValues ? "true" : "false") + ";\n";
            for (const auto& cond : trig.conditions) {
                src += "            {\n";
                src += "                aero::AxbTriggerCondition tc{};\n";
                src += "                tc.propId = " + std::to_string(cond.propId) + "u;\n";
                src += "                tc.expectedValue = " + EmitUnifiedValueLiteralCpp(cond.expectedValue) + ";\n";
                src += "                tc.sourceKind = static_cast<aero::TriggerConditionSource>(" +
                       std::to_string(static_cast<unsigned>(cond.sourceKind)) + "u);\n";
                src += "                tc.converter = static_cast<aero::ValueConverterKind>(" +
                       std::to_string(static_cast<unsigned>(cond.converter)) + "u);\n";
                if (!cond.converterKey.empty()) {
                    src += "                tc.converterKey = \"" + EscapeAotCppString(cond.converterKey) + "\";\n";
                }
                if (!cond.converterParameter.empty()) {
                    src += "                tc.converterParameter = \"" + EscapeAotCppString(cond.converterParameter) + "\";\n";
                }
                src += "                tc.converterSymbolId = " + std::to_string(cond.converterSymbolId) + "ULL;\n";
                src += "                tc.expectNull = " + std::string(cond.expectNull ? "true" : "false") + ";\n";
                if (!cond.sourceName.empty()) {
                    src += "                tc.sourceName = \"" + EscapeAotCppString(cond.sourceName) + "\";\n";
                }
                if (!cond.bindingPath.empty()) {
                    src += "                tc.bindingPath = \"" + EscapeAotCppString(cond.bindingPath) + "\";\n";
                }
                for (const auto& chop : cond.compiledHops) {
                    src += "                {\n";
                    src += "                    aero::AxbCompiledHop ch{};\n";
                    src += "                    ch.memberName = \"" + EscapeAotCppString(chop.memberName) + "\";\n";
                    src += "                    ch.declaringTypeId = " + std::to_string(chop.declaringTypeId) + "ULL;\n";
                    src += "                    ch.fieldIndex = " + std::to_string(chop.fieldIndex) + "u;\n";
                    src += "                    ch.observableBitIndex = " + std::to_string(static_cast<unsigned>(chop.observableBitIndex)) + "u;\n";
                    src += "                    ch.getterSymbolId = " + std::to_string(chop.getterSymbolId) + "ULL;\n";
                    src += "                    ch.setterSymbolId = " + std::to_string(chop.setterSymbolId) + "ULL;\n";
                    src += "                    ch.isAutoProperty = " + std::string(chop.isAutoProperty ? "true" : "false") + ";\n";
                    src += "                    ch.isObservable = " + std::string(chop.isObservable ? "true" : "false") + ";\n";
                    src += "                    ch.isReadOnly = " + std::string(chop.isReadOnly ? "true" : "false") + ";\n";
                    src += "                    ch.isSimState = " + std::string(chop.isSimState ? "true" : "false") + ";\n";
                    src += "                    ch.isSimCommand = " + std::string(chop.isSimCommand ? "true" : "false") + ";\n";
                    src += "                    tc.compiledHops.push_back(std::move(ch));\n";
                    src += "                }\n";
                }
                src += "                trigDef.conditions.push_back(std::move(tc));\n";
                src += "            }\n";
            }
            AppendSetterListPopulateCpp(src, trig.setters, "trigDef.setters", "            ");
            for (const auto& esb : trig.enterStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard esbDef{};\n";
                AppendStoryboardPopulateCpp(src, esb, "esbDef", "                ");
                src += "                trigDef.enterStoryboards.push_back(std::move(esbDef));\n";
                src += "            }\n";
            }
            for (const auto& xsb : trig.exitStoryboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard xsbDef{};\n";
                AppendStoryboardPopulateCpp(src, xsb, "xsbDef", "                ");
                src += "                trigDef.exitStoryboards.push_back(std::move(xsbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.triggers.push_back(std::move(trigDef));\n";
            src += "        }\n";
        }
        for (const auto& vsg : st.visualStateGroups) {
            src += "        {\n";
            src += "            aero::AxbVisualStateGroup vsgDef{};\n";
            src += "            vsgDef.name = \"" + EscapeAotCppString(vsg.name) + "\";\n";
            src += "            vsgDef.defaultTransitionDuration = " +
                   FormatFloatLiteral(vsg.defaultTransitionDuration) + ";\n";
            src += "            vsgDef.defaultEasingKind = static_cast<aero::EasingFunctionKind>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingKind)) + "u);\n";
            src += "            vsgDef.defaultEasingMode = static_cast<aero::EasingMode>(" +
                   std::to_string(static_cast<unsigned>(vsg.defaultEasingMode)) + "u);\n";
            for (const auto& vs : vsg.states) {
                src += "            {\n";
                src += "                aero::AxbVisualState vsDef{};\n";
                src += "                vsDef.name = \"" + EscapeAotCppString(vs.name) + "\";\n";
                AppendSetterListPopulateCpp(src, vs.setters, "vsDef.setters", "                ");
                AppendStoryboardPopulateCpp(src, vs.storyboard, "vsDef.storyboard", "                ");
                src += "                vsgDef.states.push_back(std::move(vsDef));\n";
                src += "            }\n";
            }
            src += "            stDef.visualStateGroups.push_back(std::move(vsgDef));\n";
            src += "        }\n";
        }
        for (const auto& et : st.eventTriggers) {
            src += "        {\n";
            src += "            aero::AxbEventTrigger etDef{};\n";
            src += "            etDef.eventKind = static_cast<aero::RoutedTriggerEvent>(" +
                   std::to_string(static_cast<unsigned>(et.eventKind)) + "u);\n";
            if (!et.sourceName.empty()) {
                src += "            etDef.sourceName = \"" + EscapeAotCppString(et.sourceName) + "\";\n";
            }
            for (const auto& etSb : et.storyboards) {
                src += "            {\n";
                src += "                aero::AxbStoryboard etSbDef{};\n";
                AppendStoryboardPopulateCpp(src, etSb, "etSbDef", "                ");
                src += "                etDef.storyboards.push_back(std::move(etSbDef));\n";
                src += "            }\n";
            }
            src += "            stDef.eventTriggers.push_back(std::move(etDef));\n";
            src += "        }\n";
        }
        if (emitSharedPkg && hasP1StylePayload) {
            src += "        if (" + std::to_string(stIdx) + "u < sAotSharedPkg.styles.size()) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(sAotSharedPkg.styles[" +
                   std::to_string(stIdx) + "u]);\n";
            src += "        } else {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
            src += "        }\n";
        } else if (emitSharedPkg) {
            src += "        if (sAotReboundSharedPkg && " + std::to_string(stIdx) + "u < sAotSharedPkg.styles.size()) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(sAotSharedPkg.styles[" +
                   std::to_string(stIdx) + "u]);\n";
            src += "        } else {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
            src += "        }\n";
        } else {
            src += "        aero::AxbTemplateRegistry::Instance().RegisterStyle(std::move(stDef));\n";
        }
        src += "    }\n";
    }

    if (emitSharedPkg) {
        for (std::size_t dtIdx = 0; dtIdx < package.dataTemplates.size(); ++dtIdx) {
            const auto& dt = package.dataTemplates[dtIdx];
            if (!dt.templatePackage) {
                continue;
            }
            if (!dt.key.empty()) {
                src += "    if (sAotReboundSharedPkg || !aero::AxbTemplateRegistry::Instance().FindDataTemplate(\"" +
                       EscapeAotCppString(dt.key) + "\")) {\n";
            } else {
                src += "    {\n";
            }
            src += "        if (" + std::to_string(dtIdx) + "u < sAotSharedPkg.dataTemplates.size() && "
                   "sAotSharedPkg.dataTemplates[" + std::to_string(dtIdx) + "u].templatePackage) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterDataTemplate(sAotSharedPkg.dataTemplates[" +
                   std::to_string(dtIdx) + "u]);\n";
            src += "        }\n";
            src += "    }\n";
        }

        for (std::size_t ctIdx = 0; ctIdx < package.controlTemplates.size(); ++ctIdx) {
            const auto& ct = package.controlTemplates[ctIdx];
            if (!ct.templatePackage) {
                continue;
            }
            if (!ct.key.empty()) {
                src += "    if (sAotReboundSharedPkg || !aero::AxbTemplateRegistry::Instance().FindControlTemplate(\"" +
                       EscapeAotCppString(ct.key) + "\")) {\n";
            } else {
                src += "    {\n";
            }
            src += "        if (" + std::to_string(ctIdx) + "u < sAotSharedPkg.controlTemplates.size() && "
                   "sAotSharedPkg.controlTemplates[" + std::to_string(ctIdx) + "u].templatePackage) {\n";
            src += "            aero::AxbTemplateRegistry::Instance().RegisterControlTemplate(sAotSharedPkg.controlTemplates[" +
                   std::to_string(ctIdx) + "u]);\n";
            src += "        }\n";
            src += "    }\n";
        }
    }

    src += "    aero::NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;\n";
    src += "    aero::ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;\n";
    const std::string_view unusedNote = ""; (void)unusedNote;
    src += "    const aero::ObjectRef effectiveContext = dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();\n";
    src += "    const aero::ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid() ? rootControl->BoundScriptWrapper() : effectiveContext;\n\n";
    src += "    (void)registry; (void)uiHeap; (void)effectiveContext; (void)wrapperObj;\n";
    src += "    (void)bindingEngine; (void)eventInterpreter; (void)eventAotContext;\n\n";

    std::vector<std::string> elemVarStack;
    std::size_t nextElemId = 0;
    bool rootVisited = false;
    src += "    std::vector<aero::UIElement*> localElements;\n";
    src += "    localElements.reserve(" + std::to_string(package.instructions.size() / 2u + 1u) + "u);\n";

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                const std::string varName = "elem_" + std::to_string(nextElemId++);

                if (!rootVisited) {
                    rootVisited = true;
                    src += "    aero::UIElement* " + varName + " = rootControl; (void)" + varName + ";\n";
                    if (package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        const std::string ownerTypeCond = (package.fieldsOwnerTypeId != 0)
                            ? (" && uiHeap->objectTypeIdFast(wrapperObj) == " + std::to_string(package.fieldsOwnerTypeId) + "ULL")
                            : std::string{};
                        src += "    if (domains && registry && uiHeap && wrapperObj.valid() && uiHeap->isAlive(wrapperObj)" + ownerTypeCond + ") {\n";
                        src += "        const auto rootH = rootControl->ToNativeHandle(*registry);\n";
                        src += "        (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                               std::to_string(package.rootHandleFieldIndex) + "u, rootH, nullptr);\n";
                        src += "    }\n";
                    }
                } else {
                    const std::string parentVar = elemVarStack.empty() ? "rootControl" : elemVarStack.back();
                    const std::string escapedType = EscapeAotCppString(std::string(typeName));
                    const std::string_view shortChild = StripXmlPrefix(typeName);
                    if (AxbTemplateRegistry::Instance().Find(typeName) != nullptr &&
                        shortChild != "UserControl" && shortChild != "Page") {
                        src += "    aero::IntrusivePtr<aero::UIElement> " + varName +
                               "_ptr = aero::MakeIntrusive<aero::UserControl>(\"" + escapedType +
                               "\", domains, bindingEngine, effectiveContext, eventInterpreter, eventAotContext);\n";
                    } else {
                        src += "    auto " + varName + "_ptr = aero::CreateElementByTypeName(\"" +
                               escapedType + "\");\n";
                    }
                    src += "    aero::UIElement* " + varName + " = " + varName + "_ptr.Get();\n";
                    src += "    if (" + parentVar + " && " + varName + ") {\n";
                    src += "        " + parentVar + "->AddChild(" + varName + ", registry, uiHeap);\n";
                    if ((inst.flags & 0x01u) != 0u) {
                        src += "        if (auto* hcc = " + parentVar + "->DynamicCast<aero::HeaderedContentControl>()) {\n";
                        src += "            hcc->SetHeaderElement(" + varName + ");\n";
                        src += "        }\n";
                    }
                    src += "    }\n";
                }
                src += "    localElements.push_back(" + varName + ");\n";
                src += "    if (" + varName + " && bindingEngine) {\n";
                src += "        " + varName + "->BindTwoWayEngine(bindingEngine);\n";
                src += "    }\n";

                if (const auto probe = CreateElementByTypeName(typeName)) {
                    if (probe->IsA<ItemsControl>()) {
                        src += "    if (auto* ic = " + varName + " ? " + varName + "->DynamicCast<aero::ItemsControl>() : nullptr) {\n";
                        src += "        ic->SetDomains(domains);\n";
                        src += "        ic->SetBindingEngine(bindingEngine);\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            ic->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    } else if (probe->IsA<TabControl>()) {
                        src += "    if (auto* tc = " + varName + " ? " + varName + "->DynamicCast<aero::TabControl>() : nullptr) {\n";
                        src += "        tc->SetDomains(domains);\n";
                        src += "        tc->SetBindingEngine(bindingEngine);\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            tc->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    }
                    if (probe->IsA<RiveControl>()) {
                        src += "    if (auto* rc = " + varName + " ? " + varName + "->DynamicCast<aero::RiveControl>() : nullptr) {\n";
                        src += "        if (bindingEngine) {\n";
                        src += "            rc->SetAnimationEngine(bindingEngine->GetAnimationEngine());\n";
                        src += "        }\n";
                        src += "    }\n";
                    }
                }

                if (!elemName.empty()) {
                    src += "    if (" + varName + ") {\n";
                    src += "        " + varName + "->SetName(\"" + EscapeAotCppString(std::string(elemName)) + "\");\n";
                    for (const auto& namedSlot : package.namedControls) {
                        if (namedSlot.name == elemName &&
                            namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                            const std::string ownerTypeCond = (package.fieldsOwnerTypeId != 0)
                                ? (" && uiHeap->objectTypeIdFast(wrapperObj) == " + std::to_string(package.fieldsOwnerTypeId) + "ULL")
                                : std::string{};
                            src += "        if (domains && registry && uiHeap && wrapperObj.valid() && uiHeap->isAlive(wrapperObj)" + ownerTypeCond + ") {\n";
                            src += "            const auto h = " + varName + "->ToNativeHandle(*registry);\n";
                            src += "            (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                                   std::to_string(namedSlot.fieldIndexInClass) + "u, h, nullptr);\n";
                            src += "        }\n";
                            break;
                        }
                    }
                    src += "    }\n";
                }

                elemVarStack.push_back(varName);
                break;
            }

            case AxbOpcode::EndElement: {
                if (!elemVarStack.empty()) {
                    elemVarStack.pop_back();
                }
                break;
            }

            case AxbOpcode::SetStaticProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + ") {\n";
                    src += "        " + curVar + "->SetValue(" + std::to_string(inst.propId) + "u, " +
                           EmitUnifiedValueLiteralCpp(inst.literalValue) + ", aero::ValueSourceRank::Local);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindProperty: {
                if (!elemVarStack.empty() && inst.strIdx0 < package.bindingChains.size()) {
                    const std::string& curVar = elemVarStack.back();
                    const auto& chain = package.bindingChains[inst.strIdx0];
                    const bool isElemBind = !chain.elementName.empty() ||
                                            !chain.relativeAncestorType.empty() ||
                                            chain.relativeSelf;
                    src += "    if (" + curVar + " && bindingEngine" +
                           std::string(isElemBind ? "" : " && effectiveContext.valid()") + ") {\n";
                    if (chain.mode == BindingMode::TwoWay) {
                        src += "        " + curVar + "->BindTwoWayEngine(bindingEngine);\n";
                    }
                    if (emitSharedPkg && chain.nestedMultiBinding != nullptr) {
                        src += "        if (" + std::to_string(inst.strIdx0) + "u < sAotSharedPkg.bindingChains.size()) {\n";
                        if (isElemBind) {
                            src += "            (void)bindingEngine->BindElementProperty(sAotSharedPkg.bindingChains[" +
                                   std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        } else {
                            src += "            (void)bindingEngine->BindChain(effectiveContext, sAotSharedPkg.bindingChains[" +
                                   std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        }
                        src += "        }\n";
                    } else {
                        src += "        aero::AxbCompiledBindingChain chain{};\n";
                        src += "        chain.rawPath = \"" + EscapeAotCppString(chain.rawPath) + "\";\n";
                        src += "        chain.targetPropertyId = " + std::to_string(chain.targetPropertyId) + "u;\n";
                        src += "        chain.mode = static_cast<aero::BindingMode>(" +
                               std::to_string(static_cast<unsigned>(chain.mode)) + "u);\n";
                        src += "        chain.converter = static_cast<aero::ValueConverterKind>(" +
                               std::to_string(static_cast<unsigned>(chain.converter)) + "u);\n";
                        if (!chain.converterKey.empty()) {
                            src += "        chain.converterKey = \"" + EscapeAotCppString(chain.converterKey) + "\";\n";
                        }
                        if (!chain.converterParameter.empty()) {
                            src += "        chain.converterParameter = \"" + EscapeAotCppString(chain.converterParameter) + "\";\n";
                        }
                        if (!chain.stringFormat.empty()) {
                            src += "        chain.stringFormat = \"" + EscapeAotCppString(chain.stringFormat) + "\";\n";
                        }
                        src += "        chain.converterSymbolId = " + std::to_string(chain.converterSymbolId) + "ULL;\n";
                        src += "        chain.convertBackSymbolId = " + std::to_string(chain.convertBackSymbolId) + "ULL;\n";
                        if (!chain.elementName.empty()) {
                            src += "        chain.elementName = \"" + EscapeAotCppString(chain.elementName) + "\";\n";
                        }
                        if (!chain.relativeAncestorType.empty()) {
                            src += "        chain.relativeAncestorType = \"" + EscapeAotCppString(chain.relativeAncestorType) + "\";\n";
                        }
                        src += "        chain.relativeSelf = " + std::string(chain.relativeSelf ? "true" : "false") + ";\n";
                        if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                            src += "        chain.fallbackValue = " + EmitUnifiedValueLiteralCpp(chain.fallbackValue) + ";\n";
                        }
                        if (!std::holds_alternative<std::monostate>(chain.targetNullValue)) {
                            src += "        chain.targetNullValue = " + EmitUnifiedValueLiteralCpp(chain.targetNullValue) + ";\n";
                        }
                        for (const auto& hop : chain.hops) {
                            src += "        {\n";
                            src += "            aero::AxbCompiledHop hop{};\n";
                            src += "            hop.memberName = \"" + EscapeAotCppString(hop.memberName) + "\";\n";
                            src += "            hop.declaringTypeId = " + std::to_string(hop.declaringTypeId) + "ULL;\n";
                            src += "            hop.fieldIndex = " + std::to_string(hop.fieldIndex) + "u;\n";
                            src += "            hop.observableBitIndex = " + std::to_string(static_cast<unsigned>(hop.observableBitIndex)) + "u;\n";
                            src += "            hop.getterSymbolId = " + std::to_string(hop.getterSymbolId) + "ULL;\n";
                            src += "            hop.setterSymbolId = " + std::to_string(hop.setterSymbolId) + "ULL;\n";
                            src += "            hop.isAutoProperty = " + std::string(hop.isAutoProperty ? "true" : "false") + ";\n";
                            src += "            hop.isObservable = " + std::string(hop.isObservable ? "true" : "false") + ";\n";
                            src += "            hop.isReadOnly = " + std::string(hop.isReadOnly ? "true" : "false") + ";\n";
                            src += "            hop.isSimState = " + std::string(hop.isSimState ? "true" : "false") + ";\n";
                            src += "            hop.isSimCommand = " + std::string(hop.isSimCommand ? "true" : "false") + ";\n";
                            src += "            chain.hops.push_back(std::move(hop));\n";
                            src += "        }\n";
                        }
                        if (isElemBind) {
                            src += "        (void)bindingEngine->BindElementProperty(chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        } else {
                            src += "        (void)bindingEngine->BindChain(effectiveContext, chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                        }
                    }
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindMultiProperty: {
                if (!elemVarStack.empty() && emitSharedPkg && inst.strIdx0 < package.multiBindings.size()) {
                    const std::string& curVar = elemVarStack.back();
                    const auto& mb = package.multiBindings[inst.strIdx0];
                    src += "    if (" + curVar + " && bindingEngine) {\n";
                    if (mb.mode == BindingMode::TwoWay) {
                        src += "        " + curVar + "->BindTwoWayEngine(bindingEngine);\n";
                    }
                    src += "        if (" + std::to_string(inst.strIdx0) + "u < sAotSharedPkg.multiBindings.size()) {\n";
                    src += "            (void)bindingEngine->BindMultiChain(effectiveContext, sAotSharedPkg.multiBindings[" +
                           std::to_string(inst.strIdx0) + "u], aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEcsColumn: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string_view compName = package.GetString(inst.strIdx0);
                    const std::string_view fieldName = package.GetString(inst.strIdx1);
                    std::string canonicalComp(compName);
                    if (canonicalComp.find("::") == std::string::npos) {
                        if (const std::size_t lastDot = canonicalComp.rfind('.'); lastDot != std::string::npos) {
                            canonicalComp.replace(lastDot, 1, "::");
                        }
                    }
                    const SymbolId compId = (inst.symbolId != 0)
                        ? inst.symbolId
                        : rs_sem::stableTypeId(canonicalComp);
                    src += "    if (" + curVar + " && bindingEngine) {\n";
                    src += "        (void)bindingEngine->BindEcsColumn(" + std::to_string(compId) + "ULL, \"" +
                           EscapeAotCppString(std::string(fieldName)) + "\", 0u, aero::IntrusivePtr<aero::UIElement>(" +
                           curVar + ", true), " + std::to_string(inst.propId) + "u);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    SymbolId handlerSym = inst.symbolId;
                    if (handlerSym == 0 && inst.strIdx0 < package.eventBindings.size()) {
                        handlerSym = package.eventBindings[inst.strIdx0].handlerSymbolId;
                    }
                    src += "    if (" + curVar + " && wrapperObj.valid() && " +
                           std::to_string(handlerSym) + "ULL != 0ULL) {\n";
                    src += "        if (auto* btn = " + curVar + "->DynamicCast<aero::Button>()) {\n";
                    src += "            if (eventInterpreter) {\n";
                    src += "                btn->BindScriptClickHandler(eventInterpreter, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            } else if (eventAotContext) {\n";
                    src += "                btn->BindAotClickHandler(eventAotContext, wrapperObj, " +
                           std::to_string(handlerSym) + "ULL);\n";
                    src += "            }\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::AddRowDefinition: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (auto* grid = " + curVar + "->DynamicCast<aero::Grid>()) {\n";
                    src += "            grid->AddRowDefinition(static_cast<aero::GridUnitType>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u), " +
                           FormatFloatLiteral(val) + ");\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::AddColumnDefinition: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const float val = value::TryGetFloat(inst.literalValue).value_or(1.0f);
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (auto* grid = " + curVar + "->DynamicCast<aero::Grid>()) {\n";
                    src += "            grid->AddColumnDefinition(static_cast<aero::GridUnitType>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u), " +
                           FormatFloatLiteral(val) + ");\n";
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::SetResourceProperty: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    const std::string escapedKey = EscapeAotCppString(std::string(package.GetString(inst.strIdx0)));
                    const bool isDynamicOrLoc =
                        (inst.flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) != 0u;
                    src += "    if (" + curVar + ") {\n";
                    if (isDynamicOrLoc) {
                        src += "        if (const auto* dv = aero::AxbTemplateRegistry::Instance().FindDynamicResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *dv);\n";
                        src += "        } else if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *rv);\n";
                    } else {
                        src += "        if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                               escapedKey + "\")) {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, *rv);\n";
                    }
                    if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        src += "        } else {\n";
                        src += "            aero::AxbTemplateRegistry::ApplyResolvedResourceToElement(" +
                               curVar + ", " + std::to_string(inst.propId) + "u, " +
                               EmitUnifiedValueLiteralCpp(inst.literalValue) + ");\n";
                    }
                    src += "        }\n";
                    if (isDynamicOrLoc) {
                        src += "        aero::AxbTemplateRegistry::Instance().SubscribeDynamicResource(" +
                               curVar + ", rootControl, " + std::to_string(inst.propId) + "u, \"" +
                               escapedKey + "\", " + std::to_string(static_cast<unsigned>(inst.flags)) + "u);\n";
                    }
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindTemplateParent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + " && rootControl->TemplatedParent()) {\n";
                    src += "        rootControl->TemplatedParent()->AddTemplateBinding(" +
                           curVar + ", static_cast<std::uint16_t>(" + std::to_string(inst.strIdx0) +
                           "u), " + std::to_string(inst.propId) +
                           "u, static_cast<aero::ValueConverterKind>(" +
                           std::to_string(static_cast<unsigned>(inst.flags)) + "u));\n";
                    src += "    }\n";
                }
                break;
            }
        }
    }

    if (emitSharedPkg && package.HasMotionSection()) {
        src += "    if (bindingEngine && bindingEngine->GetAnimationEngine()) {\n";
        src += "        (void)bindingEngine->GetAnimationEngine()->InstantiatePackageMotion(\n";
        src += "            rootControl,\n";
        src += "            localElements.data(),\n";
        src += "            localElements.size(),\n";
        src += "            sAotSharedPkg);\n";
        src += "    }\n";
    }

    src += "    return " + std::string(rootVisited ? "true" : "false") + ";\n";
    src += "}\n\n";
    src += "} // namespace " + out.cppNamespace + "\n";
    out.source = std::move(src);
    return out;
}

namespace {

bool CompileRuntimeXamlFromUriHook(
    std::string_view uriOrPath,
    const std::vector<std::string>& searchDirs,
    AxbPackage& outPackage) {
    if (uriOrPath.empty()) {
        return false;
    }
    std::string cleaned = NormalizeSlashes(uriOrPath);
    if (const auto compPos = cleaned.find(";component/"); compPos != std::string::npos) {
        cleaned = cleaned.substr(compPos + 11u);
    }
    while (!cleaned.empty() && (cleaned.front() == '/' || cleaned.front() == '\\')) {
        cleaned.erase(cleaned.begin());
    }
    if (cleaned.rfind("./", 0) == 0) {
        cleaned.erase(0, 2u);
    }

    std::filesystem::path resolvedFile;
    std::error_code ec;
    const std::filesystem::path directCand(uriOrPath);
    if (std::filesystem::exists(directCand, ec) && !ec &&
        std::filesystem::is_regular_file(directCand, ec) && !ec) {
        resolvedFile = directCand;
    } else {
        for (const auto& dirStr : searchDirs) {
            if (dirStr.empty()) continue;
            const std::filesystem::path baseDir(dirStr);
            const std::filesystem::path cand1 = baseDir / cleaned;
            ec.clear();
            if (std::filesystem::exists(cand1, ec) && !ec &&
                std::filesystem::is_regular_file(cand1, ec) && !ec) {
                resolvedFile = cand1;
                break;
            }
            if (const auto lastSlash = cleaned.rfind('/'); lastSlash != std::string::npos) {
                const std::filesystem::path candLeaf = baseDir / cleaned.substr(lastSlash + 1u);
                ec.clear();
                if (std::filesystem::exists(candLeaf, ec) && !ec &&
                    std::filesystem::is_regular_file(candLeaf, ec) && !ec) {
                    resolvedFile = candLeaf;
                    break;
                }
            }
        }
    }
    if (resolvedFile.empty()) {
        static constexpr std::string_view kSampleFallbacks[] = {
            "Data",
            "Samples/Gallery/Data",
            "Samples/Localization/Data",
            "../Samples/Gallery/Data",
            "../Samples/Localization/Data",
        };
        for (std::string_view fb : kSampleFallbacks) {
            const std::filesystem::path cand = std::filesystem::path(fb) / cleaned;
            ec.clear();
            if (std::filesystem::exists(cand, ec) && !ec &&
                std::filesystem::is_regular_file(cand, ec) && !ec) {
                resolvedFile = cand;
                break;
            }
        }
    }
    if (resolvedFile.empty()) {
        return false;
    }

    std::ifstream ifs(resolvedFile, std::ios::binary);
    if (!ifs) {
        return false;
    }
    std::string xamlText((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (xamlText.empty()) {
        return false;
    }

    AeroCompiler compiler;
    AeroCompileInput in{};
    in.xamlPath = resolvedFile.string();
    in.xamlContent = std::move(xamlText);
    in.resourceSearchRoot = resolvedFile.parent_path().string();
    in.resourceSearchPaths = searchDirs;
    AeroCompileResult res = compiler.Compile(in);
    if (!res.succeeded) {
        return false;
    }
    outPackage = std::move(res.axbPackage);
    return true;
}

struct AutoRegisterRuntimeXamlCompileHook {
    AutoRegisterRuntimeXamlCompileHook() noexcept {
        RegisterRuntimeXamlCompileHook(&CompileRuntimeXamlFromUriHook);
    }
};

const AutoRegisterRuntimeXamlCompileHook gAutoRegisterRuntimeXamlCompileHook{};

void EnsureDefaultRuntimeXamlHookInstalled() noexcept {
    if (GetRuntimeXamlCompileHook() == nullptr) {
        RegisterRuntimeXamlCompileHook(&CompileRuntimeXamlFromUriHook);
    }
}

} // namespace

} // namespace aero
