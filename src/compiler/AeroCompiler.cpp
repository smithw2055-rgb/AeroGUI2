#include "aero/compiler/AeroCompiler.hpp"
#include "AeroCompilerInternal.hpp"
#include "aero/compiler/CSharpCompatLowerer.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if defined(_MSC_VER)
#define AERO_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define AERO_NOINLINE __attribute__((noinline))
#else
#define AERO_NOINLINE
#endif

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/controls/AxbTemplateMounter.hpp"
#include "aero/controls/Panels.hpp"
#include "aero/core/StringUtils.hpp"
#include "aero/core/TypeClass.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/jit/StencilJit.h"
#include "realscript/optimization/Optimizer.h"

namespace aero {

namespace {

[[nodiscard]] std::pair<GridUnitType, float> ParseGridLength(std::string_view raw) noexcept {
    raw = str::Trim(raw);
    if (raw.empty() || raw == "*") {
        return {GridUnitType::Star, 1.0f};
    }
    if (str::EqualsIgnoreCase(raw, "Auto")) {
        return {GridUnitType::Auto, 1.0f};
    }
    if (raw.back() == '*') {
        const std::string_view numPart = str::Trim(raw.substr(0u, raw.size() - 1u));
        const float starWeight = numPart.empty() ? 1.0f : compiler_detail::ParseFloat32(numPart, 1.0f);
        return {GridUnitType::Star, starWeight};
    }
    return {GridUnitType::Pixel, compiler_detail::ParseFloat32(raw, 0.0f)};
}

bool TryParseResourceMarkupExtension(
    std::string_view rawExpr,
    std::string_view& outKey,
    std::uint8_t& outFlags) noexcept {
    outKey = {};
    outFlags = AxbResourceFlag_StaticResource;
    std::string_view v = str::Trim(rawExpr);
    if (v.size() <= 2u || v.front() != '{' || v.back() != '}') {
        return false;
    }
    std::string_view inner = str::Trim(v.substr(1u, v.size() - 2u));
    if (inner.rfind("StaticResource ", 0) == 0) {
        outFlags = AxbResourceFlag_StaticResource;
        inner = str::Trim(inner.substr(15u));
    } else if (inner.rfind("DynamicResource ", 0) == 0) {
        outFlags = AxbResourceFlag_DynamicResource;
        inner = str::Trim(inner.substr(16u));
    } else if (inner.rfind("noesis:Loc ", 0) == 0) {
        outFlags = AxbResourceFlag_Loc;
        inner = str::Trim(inner.substr(11u));
    } else if (inner.rfind("Loc ", 0) == 0) {
        outFlags = AxbResourceFlag_Loc;
        inner = str::Trim(inner.substr(4u));
    } else {
        return false;
    }

    if (const std::size_t comma = inner.find(','); comma != std::string_view::npos) {
        inner = str::Trim(inner.substr(0u, comma));
    }
    if (inner.rfind("ResourceKey=", 0) == 0) {
        inner = str::Trim(inner.substr(12u));
    } else if (inner.rfind("Key=", 0) == 0) {
        inner = str::Trim(inner.substr(4u));
    }
    if (inner.size() >= 2u &&
        ((inner.front() == '\'' && inner.back() == '\'') ||
         (inner.front() == '"' && inner.back() == '"'))) {
        inner = str::Trim(inner.substr(1u, inner.size() - 2u));
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
    const std::string norm = NormalizeSlashes(str::Trim(rawPath));
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
    std::string s = NormalizeSlashes(str::Trim(rawUri));
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
    requestedSource = str::Trim(requestedSource);
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
                        const std::string_view rel = str::Trim(
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
    std::string_view v = str::Trim(rawTypeExpr);
    if (v.size() > 2u && v.front() == '{' && v.back() == '}') {
        v = str::Trim(v.substr(1u, v.size() - 2u));
        if (str::StartsWith(v, "x:Type ")) {
            v = str::Trim(v.substr(7u));
        } else if (str::StartsWith(v, "Type ")) {
            v = str::Trim(v.substr(5u));
        }
        if (str::StartsWith(v, "TypeName=")) {
            v = str::Trim(v.substr(9u));
        }
    }
    return std::string(str::ShortTypeName(v));
}

[[nodiscard]] bool IsInstanceSubHopPropertyPath(std::string_view path) noexcept {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view prefix = path.substr(0u, dot);
    return prefix == "SelectedItem" || prefix == "DataContext" || prefix == "Content" ||
           prefix == "PlacementTarget" || prefix == "Header" || prefix == "Tag";
}

std::uint16_t ResolveOwnerAwarePropertyId(
    std::string_view rawProp,
    std::string_view ownerType) noexcept {
    rawProp = str::Trim(rawProp);
    if (rawProp.size() > 2u && rawProp.front() == '(' && rawProp.back() == ')') {
        rawProp = str::Trim(rawProp.substr(1u, rawProp.size() - 2u));
    }
    if (rawProp.empty()) {
        return PropId_Tag;
    }
    if (rawProp == "Tag") {
        return PropId_Tag;
    }
    if (const std::size_t dot = rawProp.rfind('.'); dot != std::string_view::npos) {
        const std::string_view qualOwner = str::StripNamespacePrefix(str::Trim(rawProp.substr(0u, dot)));
        const std::string_view qualLeaf = str::Trim(rawProp.substr(dot + 1u));
        if (!qualOwner.empty() && !qualLeaf.empty()) {
            const std::uint16_t customId = FindCustomProperty(qualOwner, qualLeaf);
            if (customId != 0xFFFFu) {
                return customId;
            }
        }
    } else if (!ownerType.empty()) {
        const std::string_view ownerShort = str::StripNamespacePrefix(str::Trim(ownerType));
        if (!ownerShort.empty()) {
            const std::uint16_t customId = FindCustomProperty(ownerShort, rawProp);
            if (customId != 0xFFFFu) {
                return customId;
            }
        }
    }
    return ResolveBuiltinPropertyId(rawProp);
}

std::uint16_t ResolveMappedSetterPropertyId(
    std::string_view rawProp,
    std::string_view ownerType = {}) noexcept {
    rawProp = str::Trim(rawProp);
    if (rawProp.empty()) {
        return compiler_detail::kUnmappedPropId;
    }
    if (rawProp == "Tag") {
        return PropId_Tag;
    }
    const std::uint16_t ownerAware = ResolveOwnerAwarePropertyId(rawProp, ownerType);
    if (ownerAware != PropId_Tag) {
        return ownerAware;
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
    return compiler_detail::kUnmappedPropId;
}

std::uint16_t ResolveMappedTriggerConditionId(
    std::string_view rawProp,
    std::string_view ownerType = {}) noexcept {
    rawProp = str::Trim(rawProp);
    if (rawProp.empty()) {
        return compiler_detail::kUnmappedPropId;
    }
    if (rawProp == "Tag" || str::EndsWith(rawProp, ".Tag")) {
        return PropId_Tag;
    }
    if (!ownerType.empty()) {
        const std::uint16_t customOrOwner = ResolveOwnerAwarePropertyId(rawProp, ownerType);
        if (IsCustomPropertyId(customOrOwner)) {
            return customOrOwner;
        }
    }
    const std::uint16_t id = ResolveTriggerConditionPropertyId(rawProp);
    return (id == PropId_Tag) ? compiler_detail::kUnmappedPropId : id;
}

std::uint16_t ResolveMappedAnimationPropertyId(
    std::string_view rawPath,
    std::string_view targetOwnerType = {}) noexcept {
    rawPath = str::Trim(rawPath);
    if (rawPath.empty()) {
        return compiler_detail::kUnmappedPropId;
    }
    if (rawPath == "Tag" || str::EndsWith(rawPath, ".Tag") || str::EndsWith(rawPath, ".Tag)")) {
        return PropId_Tag;
    }
    if (!targetOwnerType.empty()) {
        const std::string_view ownerShort = str::StripNamespacePrefix(targetOwnerType);
        if (ownerShort == "DirectionalBlurEffect") {
            if (rawPath == "Angle" || str::EndsWith(rawPath, ".Angle") || str::EndsWith(rawPath, ".Angle)")) {
                return PropId_EffectAngle;
            }
            if (rawPath == "Radius" || str::EndsWith(rawPath, ".Radius") || str::EndsWith(rawPath, ".Radius)")) {
                return PropId_EffectBlurRadius;
            }
        } else if (ownerShort == "TintEffect") {
            if (rawPath == "Color" || str::EndsWith(rawPath, ".Color") || str::EndsWith(rawPath, ".Color)")) {
                return PropId_EffectColor;
            }
        } else if (ownerShort == "ChromaticAberrationEffect") {
            if (rawPath == "AmountX" || str::EndsWith(rawPath, ".AmountX") || str::EndsWith(rawPath, ".AmountX)")) {
                return PropId_EffectAmountX;
            }
        } else if (ownerShort == "SaturationEffect" || ownerShort == "SaturationBrush") {
            if (rawPath == "Saturation" || str::EndsWith(rawPath, ".Saturation") || str::EndsWith(rawPath, ".Saturation)")) {
                return PropId_Saturation;
            }
        } else if (ownerShort == "PlasmaBrush") {
            if (rawPath == "Time" || str::EndsWith(rawPath, ".Time") || str::EndsWith(rawPath, ".Time)")) {
                return PropId_ShaderTime;
            }
        }
        const std::uint16_t ownerAware = ResolveOwnerAwarePropertyId(rawPath, ownerShort);
        if (IsCustomPropertyId(ownerAware)) {
            return ownerAware;
        }
    }
    const std::uint16_t id = ResolveAnimationTargetPropertyId(rawPath);
    return (id == PropId_Tag) ? compiler_detail::kUnmappedPropId : id;
}

bool IsAnimationTimelineTag(std::string_view shortTag) noexcept {
    return shortTag == "DoubleAnimation" ||
           shortTag == "SingleAnimation" ||
           shortTag == "Int32Animation" ||
           shortTag == "ColorAnimation" ||
           shortTag == "ThicknessAnimation" ||
           shortTag == "PointAnimation" ||
           shortTag == "DoubleAnimationUsingKeyFrames" ||
           shortTag == "SingleAnimationUsingKeyFrames" ||
           shortTag == "Int32AnimationUsingKeyFrames" ||
           shortTag == "ColorAnimationUsingKeyFrames" ||
           shortTag == "ThicknessAnimationUsingKeyFrames" ||
           shortTag == "BooleanAnimationUsingKeyFrames" ||
           shortTag == "ObjectAnimationUsingKeyFrames" ||
           shortTag == "PointAnimationUsingKeyFrames";
}

bool IsAnimationKeyFrameTag(std::string_view shortTag) noexcept {
    return str::EndsWith(shortTag, "DoubleKeyFrame") ||
           str::EndsWith(shortTag, "SingleKeyFrame") ||
           str::EndsWith(shortTag, "Int32KeyFrame") ||
           str::EndsWith(shortTag, "ColorKeyFrame") ||
           str::EndsWith(shortTag, "ThicknessKeyFrame") ||
           str::EndsWith(shortTag, "BooleanKeyFrame") ||
           str::EndsWith(shortTag, "ObjectKeyFrame") ||
           str::EndsWith(shortTag, "PointKeyFrame");
}

bool IsEasingFunctionTag(std::string_view shortTag) noexcept {
    return shortTag.size() > 4u && str::EndsWith(shortTag, "Ease");
}

std::int32_t ParseRepeatCountAttr(std::string_view rawVal) noexcept {
    rawVal = str::Trim(rawVal);
    if (rawVal.empty()) {
        return 1;
    }
    if (str::EqualsIgnoreCase(rawVal, "Forever")) {
        return 0;
    }
    if (rawVal.back() == 'x' || rawVal.back() == 'X') {
        rawVal.remove_suffix(1);
    }
    const float f = compiler_detail::ParseFloat32(rawVal, 1.0f);
    return (f <= 0.0f) ? 0 : std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(f)));
}

std::string NormalizeXamlFormatOrExpr(std::string_view raw) {
    std::string_view s = str::Trim(raw);
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

std::uint16_t ResolveTemplateBindingSourcePropId(
    std::string_view rawProp,
    std::string_view ownerType = {}) noexcept {
    rawProp = str::Trim(rawProp);
    if (rawProp.size() > 2u && rawProp.front() == '(' && rawProp.back() == ')') {
        rawProp = str::Trim(rawProp.substr(1u, rawProp.size() - 2u));
    }
    if (rawProp.empty()) {
        return compiler_detail::kUnmappedPropId;
    }
    if (rawProp == "Content" || str::EndsWith(rawProp, ".Content")) {
        return PropId_Content;
    }
    if (rawProp == "Tag" || str::EndsWith(rawProp, ".Tag")) {
        return PropId_Tag;
    }
    const std::uint16_t ownerAware = ResolveOwnerAwarePropertyId(rawProp, ownerType);
    if (ownerAware != PropId_Tag) {
        return ownerAware;
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
    std::string_view v = str::Trim(rawExpr);
    if (v.size() <= 2u || v.front() != '{' || v.back() != '}') {
        return out;
    }
    std::string_view inner = str::Trim(v.substr(1u, v.size() - 2u));
    if (inner == "TemplateBinding") {
        return out;
    }
    if (inner.rfind("TemplateBinding ", 0) == 0 || inner.rfind("TemplateBinding	", 0) == 0) {
        inner = str::Trim(inner.substr(15u));
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
        const std::string_view clause = str::Trim(inner.substr(pos, end - pos));
        if (clause.rfind("Property=", 0) == 0) {
            out.sourcePath = str::Trim(clause.substr(9u));
        } else if (clause.rfind("Path=", 0) == 0) {
            out.sourcePath = str::Trim(clause.substr(5u));
        } else if (clause.rfind("Converter=", 0) == 0) {
            const std::string_view convExpr = str::Trim(clause.substr(10u));
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
        out.sourcePath = str::Trim(out.sourcePath.substr(1u, out.sourcePath.size() - 2u));
    }
    out.isValid = !out.sourcePath.empty();
    return out;
}

ParsedBindingClause ParseBindingMarkupExpression(std::string_view rawExpr) {
    ParsedBindingClause out{};
    std::string_view v = str::Trim(rawExpr);
    if (v.size() < 2u || v.front() != '{' || v.back() != '}') {
        return out;
    }
    std::string_view inner = str::Trim(v.substr(1u, v.size() - 2u));
    if (inner == "Binding" || inner == "Bind") {
        out.isValidBinding = true;
        return out;
    }
    if (inner.rfind("Binding ", 0) == 0) {
        inner = str::Trim(inner.substr(8u));
    } else if (inner.rfind("Bind ", 0) == 0) {
        inner = str::Trim(inner.substr(5u));
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
        const std::string_view clause = str::Trim(inner.substr(pos, end - pos));
        if (clause.rfind("Path=", 0) == 0) {
            out.pathPart = str::Trim(clause.substr(5u));
        } else if (clause.rfind("Mode=", 0) == 0) {
            out.hasExplicitMode = true;
            const std::string_view mStr = str::Trim(clause.substr(5u));
            if (mStr == "TwoWay") out.mode = BindingMode::TwoWay;
            else if (mStr == "OneTime") out.mode = BindingMode::OneTime;
        } else if (clause.rfind("FallbackValue=", 0) == 0) {
            std::string_view fv = str::Trim(clause.substr(14u));
            if (fv.size() >= 2u && fv.front() == '\'' && fv.back() == '\'') {
                fv = fv.substr(1u, fv.size() - 2u);
            }
            out.fallbackValue = fv;
        } else if (clause.rfind("TargetNullValue=", 0) == 0) {
            std::string_view tnv = str::Trim(clause.substr(16u));
            if (tnv.size() >= 2u && tnv.front() == '\'' && tnv.back() == '\'') {
                tnv = tnv.substr(1u, tnv.size() - 2u);
            }
            out.hasTargetNullValue = true;
            out.targetNullValue = tnv;
        } else if (clause.rfind("ElementName=", 0) == 0) {
            out.elementName = str::Trim(clause.substr(12u));
        } else if (clause.rfind("Source=", 0) == 0) {
            const std::string_view srcExpr = str::Trim(clause.substr(7u));
            const std::string_view srcKey = ExtractMarkupResourceKey(srcExpr);
            if (!srcKey.empty()) {
                out.staticResourceSourceKey = std::string(srcKey);
            }
        } else if (clause.rfind("Converter=", 0) == 0) {
            const std::string_view convExpr = str::Trim(clause.substr(10u));
            const std::string_view convKey = ExtractMarkupResourceKey(convExpr);
            const std::string_view effKey = !convKey.empty() ? convKey : convExpr;
            out.converter = ParseValueConverterKind(effKey);
            out.converterKey = std::string(effKey);
        } else if (clause.rfind("ConverterParameter=", 0) == 0) {
            out.converterParameter = NormalizeXamlFormatOrExpr(clause.substr(19u));
        } else if (clause.rfind("StringFormat=", 0) == 0) {
            out.stringFormat = NormalizeXamlFormatOrExpr(clause.substr(13u));
        } else if (clause.rfind("RelativeSource=", 0) == 0) {
            std::string_view rsExpr = str::Trim(clause.substr(15u));
            if (rsExpr.size() > 2u && rsExpr.front() == '{' && rsExpr.back() == '}') {
                rsExpr = str::Trim(rsExpr.substr(1u, rsExpr.size() - 2u));
            }
            if (rsExpr.find("TemplatedParent") != std::string_view::npos) {
                out.relativeTemplatedParent = true;
            } else if (rsExpr.find("Self") != std::string_view::npos) {                out.relativeSelf = true;
            } else if (const auto ancPos = rsExpr.find("AncestorType="); ancPos != std::string_view::npos) {
                std::string_view ancVal = str::Trim(rsExpr.substr(ancPos + 13u));
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
        out.pathPart = str::Trim(out.pathPart.substr(1u, out.pathPart.size() - 2u));
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
        const std::string_view st = str::StripNamespacePrefix(xml.substr(nameStart, nameEnd - nameStart));
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
    bool hasAnyRtStep = false;
    bool canDecomposeStaticRt = true;
    int maxRtDecompStage = 0; // 1=Scale, 2=Skew, 3=Rotate, 4=Translate
    bool hasRtScale = false;
    bool hasRtSkew = false;
    bool hasRtRotate = false;
    bool hasRtTranslate = false;
    float rtScaleX = 1.0f;
    float rtScaleY = 1.0f;
    float rtSkewX = 0.0f;
    float rtSkewY = 0.0f;
    float rtRotateAngle = 0.0f;
    float rtTranslateX = 0.0f;
    float rtTranslateY = 0.0f;
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
        const std::string_view st = str::StripNamespacePrefix(innerXml.substr(lt + 1u, tEnd - (lt + 1u)));
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
                if (nm == wanted) return str::Trim(vl);
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
    const std::string_view s = str::Trim(rawStretch);
    if (str::EqualsIgnoreCase(s, "None")) return 0u;
    if (str::EqualsIgnoreCase(s, "Fill")) return 1u;
    if (str::EqualsIgnoreCase(s, "Uniform")) return 2u;
    if (str::EqualsIgnoreCase(s, "UniformToFill")) return 3u;
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
    AxbPropertySetter setter{"", compiler_detail::kUnmappedPropId, {}};
    SetterOwnerKind owner = SetterOwnerKind::Style;
    bool hasValue = false;
    std::size_t scopeDepth = 0;
};

struct ActiveMultiBindingScope {
    AxbCompiledMultiBinding mb{};
    std::size_t scopeDepth = 0;
    bool isNestedInParentMulti = false;
    bool attachToActionTargetObject = false;
    bool attachToActionValue = false;
    bool attachToActionCmdParam = false;
};

struct ActiveBindingElemScope {
    AxbCompiledBindingChain chain{};
    std::size_t scopeDepth = 0;
    bool insideMultiBinding = false;
    bool attachToActionTargetObject = false;
    bool attachToActionValue = false;
    bool attachToActionCmdParam = false;
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
    if (pv.tryGetQuatf(f4)) {
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
    return ResolveOwnerAwarePropertyId(propertyName, {});
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

thread_local std::string_view t_activeTemplateTargetTypeHint{};

struct ScopedTargetTypeHintGuard {
    std::string_view prev;
    explicit ScopedTargetTypeHintGuard(std::string_view next) noexcept
        : prev(t_activeTemplateTargetTypeHint) {
        if (!next.empty()) {
            t_activeTemplateTargetTypeHint = next;
        }
    }
    ~ScopedTargetTypeHintGuard() {
        t_activeTemplateTargetTypeHint = prev;
    }
};

struct ParsedXmlAttr {
    std::string_view name;
    std::string_view value;
};

struct OpenTagInfo {
    std::string_view tagName;
    std::string_view shortTag;
    std::string_view xName;
    std::string_view xKey;
    bool selfClosing = false;
    std::vector<ParsedXmlAttr> attrs;
};

struct DeferredMtsdfJob {
    std::string svg;
    std::uint16_t adaptiveDim = 32u;
    std::uint16_t slotIdx = 0u;
};

struct XamlParseState {
    std::unordered_set<std::uint64_t> bakedPathHashes;
    std::unordered_set<std::string> mergedCycleKeysInThisPkg;
    std::uint32_t nextInlineVsmStyleId = 0u;
    std::vector<DeferredMtsdfJob> deferredMtsdfJobs;
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
    std::unordered_map<std::string, std::string> namedElementTypes;
    OpenTagInfo openTag;
    std::size_t nonVisualDepth = 0;
};

struct XamlCtx {
    const AeroCompiler& compiler;
    const AeroCompileInput& input;
    std::vector<std::string>& activeIncludeStack;
    AeroCompileResult& result;
    AxbPackage& pkg;
    const std::string& effectiveXamlPath;
    std::string& rootDataType;
    std::string_view src;
    bool isCompilingVisualResourceMock = false;
    bool seenRoot = false;
    std::size_t pos = 0u;
    XamlParseState& st;
};

AERO_NOINLINE void CompileInternalBody(
    const AeroCompiler& compiler,
    const AeroCompileInput& input,
    std::vector<std::string>& activeIncludeStack,
    AeroCompileResult& result);

AERO_NOINLINE std::unique_ptr<AeroCompileResult> CompileSubPackageHeap(
    const AeroCompiler& compiler,
    std::unique_ptr<AeroCompileInput> subIn,
    std::vector<std::string>& activeIncludeStack) {
    auto subRes = std::make_unique<AeroCompileResult>();
    if (subIn) {
        CompileInternalBody(compiler, *subIn, activeIncludeStack, *subRes);
    }
    return subRes;
}

AERO_NOINLINE bool RunFrontendImportPhaseNoInline(
    std::string_view effectiveXamlPath,
    std::string_view src,
    bool isSvgInput,
    bool isLottieInput,
    bool isRiveInput,
    AeroCompileResult& result) {
    bool importedOk = false;
    const auto* rawPtr = reinterpret_cast<const std::uint8_t*>(src.data());
    if (isSvgInput) {
        auto importer = std::make_unique<SvgTokenFrontendImporter>();
        importedOk = importer->ImportToSceneIR(
            effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
    } else if (isLottieInput) {
        auto importer = std::make_unique<LottieFrontendImporter>();
        importedOk = importer->ImportToSceneIR(
            effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
    } else if (isRiveInput) {
        auto importer = std::make_unique<RiveFrontendImporter>();
        importedOk = importer->ImportToSceneIR(
            effectiveXamlPath, rawPtr, src.size(), result.sceneIr, result.diagnostics);
    }
    if (!importedOk) {
        result.succeeded = false;
        return false;
    }
    result.axbPackage = result.sceneIr.loweredPackage;
    return true;
}

AERO_NOINLINE void CompileAndMergeExternalDictionaryNoInline(
    XamlCtx& ctx,
    std::string_view rawDictSource) {
    rawDictSource = str::Trim(rawDictSource);
    if (rawDictSource.empty()) {
        return;
    }
    std::string resolvedDictPath;
    std::string dictXamlContent;
    if (!ResolveExternalXamlSource(
            ctx.input,
            ctx.effectiveXamlPath,
            rawDictSource,
            resolvedDictPath,
            dictXamlContent)) {
        ctx.result.diagnostics.push_back(
            "AERO_XAML_MERGED_DICT_NOT_FOUND: Cannot resolve MergedDictionary Source '" +
            std::string(rawDictSource) + "' from '" + ctx.effectiveXamlPath + "'");
        return;
    }

    const std::string targetCycleKey = CanonicalizeCycleKeyNoThrow(resolvedDictPath);
    if (!targetCycleKey.empty()) {
        for (const auto& activeKey : ctx.activeIncludeStack) {
            if (activeKey == targetCycleKey) {
                ctx.result.diagnostics.push_back(
                    "AERO_XAML_MERGED_DICT_CYCLE: Circular ResourceDictionary reference detected for '" +
                    resolvedDictPath + "'");
                return;
            }
        }
        if (!ctx.st.mergedCycleKeysInThisPkg.insert(targetCycleKey).second) {
            return;
        }
    }

    auto dictInput = std::make_unique<AeroCompileInput>();
    dictInput->xamlPath = resolvedDictPath;
    dictInput->xamlContent = std::move(dictXamlContent);
    dictInput->externalXamlResolver = ctx.input.externalXamlResolver;
    dictInput->resourceSearchRoot = ctx.input.resourceSearchRoot;
    dictInput->resourceSearchPaths = ctx.input.resourceSearchPaths;
    dictInput->autoMergeGlobalResources = false;

    std::unique_ptr<AeroCompileResult> dictRes = CompileSubPackageHeap(
        ctx.compiler, std::move(dictInput), ctx.activeIncludeStack);
    for (const auto& d : dictRes->diagnostics) {
        ctx.result.diagnostics.push_back(d);
    }
    if (dictRes->succeeded) {
        MergeDictionaryPackageInto(ctx.pkg, dictRes->axbPackage, resolvedDictPath, ctx.st.bakedPathHashes);
    }
}

AERO_NOINLINE void FlushDeferredMtsdfJobsNoInline(XamlCtx& ctx) {
    auto& deferredMtsdfJobs = ctx.st.deferredMtsdfJobs;
    auto& pkg = ctx.pkg;
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
}

void EnsurePathBakedIntoPackageHelper(XamlCtx& ctx, std::string_view svgPathData) {
    svgPathData = str::Trim(svgPathData);
    if (svgPathData.empty()) {
        return;
    }
    const std::uint64_t h = ComputeSvgPathHash(svgPathData);
    if (h == 0 || !ctx.st.bakedPathHashes.insert(h).second) {
        return;
    }
    const std::uint16_t adaptiveDim = SelectAdaptiveMtsdfCellSize(svgPathData);
    const auto slotIdx = static_cast<std::uint16_t>(
        ctx.pkg.mtsdfPaths.size() + ctx.st.deferredMtsdfJobs.size());
    ctx.st.deferredMtsdfJobs.push_back(
        DeferredMtsdfJob{std::string(svgPathData), adaptiveDim, slotIdx});
}

void EmitStaticRenderTransformMatrix(AxbPackage& pkg, const Matrix2D& m) {
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
}

void EmitStaticLayoutTransformMatrix(AxbPackage& pkg, const Matrix2D& m) {
    AxbInstruction ltInst{};
    ltInst.opcode = AxbOpcode::SetStaticProperty;
    ltInst.propId = PropId_LayoutTransform;
    ltInst.literalValue = UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
    pkg.instructions.push_back(std::move(ltInst));
}

void EnsureEnclosingElementHasNameIfEmpty(AxbPackage& pkg, std::string_view xName) {
    xName = str::Trim(xName);
    if (xName.empty()) {
        return;
    }
    std::size_t depth = 0u;
    for (auto it = pkg.instructions.rbegin(); it != pkg.instructions.rend(); ++it) {
        if (it->opcode == AxbOpcode::EndElement) {
            ++depth;
        } else if (it->opcode == AxbOpcode::BeginElement) {
            if (depth == 0u) {
                if (pkg.GetString(it->strIdx1).empty()) {
                    it->strIdx1 = pkg.InternString(xName);
                }
                return;
            }
            --depth;
        }
    }
}

XmlScopeEntry* FindEnclosingTransformScopeHelper(std::vector<XmlScopeEntry>& scopeStack) noexcept {
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        if (it->kind == XmlScopeKind::TransformPropertyElement) {
            return &(*it);
        }
        if (it->kind != XmlScopeKind::TransformChildNode) {
            break;
        }
    }
    return nullptr;
}

const UnifiedValue* FindLocalOrGlobalResourceHelper(const AxbPackage& pkg, std::string_view key) noexcept {
    for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
        if (it->first == key) {
            return &it->second;
        }
    }
    return AxbTemplateRegistry::Instance().FindResource(key);
}

const AxbStoryboard* FindLocalOrGlobalStoryboardHelper(const AxbPackage& pkg, std::string_view key) noexcept {
    for (auto it = pkg.storyboards.rbegin(); it != pkg.storyboards.rend(); ++it) {
        if (it->key == key) {
            return &(*it);
        }
    }
    return AxbTemplateRegistry::Instance().FindStoryboard(key);
}

void ResolveConverterResourceHelper(const AxbPackage& pkg, ValueConverterKind& ioKind, std::string& ioKey) {
    if (ioKey.empty()) {
        return;
    }
    const std::string mathResKey = "__aero_math_expr__:" + ioKey;
    if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, mathResKey)) {
        if (const auto* sv = std::get_if<StringValue>(rv)) {
            ioKind = ValueConverterKind::MathExpression;
            ioKey = sv->str();
            return;
        }
    }
    if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, ioKey)) {
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
}

void SplitPathIntoHopsHelper(
    std::string_view pathPart,
    std::vector<AxbCompiledHop>& outHops,
    bool keepSingleAttachedPropertyHop = false) {
    pathPart = str::Trim(pathPart);
    if (pathPart.empty() || pathPart == ".") {
        return;
    }
    auto pushHopSegment = [&](std::string_view rawSeg) {
        std::string_view seg = str::Trim(rawSeg);
        if (seg.size() >= 2u && seg.front() == '(' && seg.back() == ')') {
            seg = str::Trim(seg.substr(1u, seg.size() - 2u));
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
}

bool TemplatedParentNeedsCompiledChain(const ParsedBindingClause& parsedBind) noexcept {
    const std::string_view p = parsedBind.pathPart;
    const std::size_t dot = p.find('.');
    // ParseBindingMarkupExpression strips outer parentheses on attached properties and sets isAttachedPropertyPath.
    const bool isSingleAttachedProp =
        parsedBind.isAttachedPropertyPath &&
        dot != std::string_view::npos &&
        p.find('.', dot + 1u) == std::string_view::npos &&
        p.find('(') == std::string_view::npos &&
        p.find(')') == std::string_view::npos &&
        p.find('[') == std::string_view::npos;
    return !parsedBind.stringFormat.empty() ||
           !parsedBind.converterKey.empty() ||
           !parsedBind.converterParameter.empty() ||
           parsedBind.converter != ValueConverterKind::None ||
           parsedBind.hasTargetNullValue ||
           parsedBind.mode == BindingMode::TwoWay ||
           (!isSingleAttachedProp && p.find('.') != std::string_view::npos);
}

AERO_NOINLINE AxbCompiledBindingChain BuildCompiledBindingChainHelper(
    const AxbPackage& pkg,
    const ParsedBindingClause& parsed,
    std::uint16_t targetPropId,
    bool inferSelfDataContextIfEmpty,
    std::string_view ownerType = {}) {
    AxbCompiledBindingChain chain{};
    const bool isSelfDcBind =
        inferSelfDataContextIfEmpty &&
        (parsed.pathPart.empty() || parsed.pathPart == ".") &&
        parsed.elementName.empty() &&
        parsed.relativeAncestorType.empty() &&
        !parsed.relativeSelf &&
        !parsed.relativeTemplatedParent;
    const std::string_view effPathPart = isSelfDcBind ? std::string_view("DataContext") : parsed.pathPart;

    chain.targetPropertyId = targetPropId;
    chain.rawPath = std::string(effPathPart);
    chain.mode = parsed.mode;
    chain.elementName = std::string(parsed.elementName);
    if (parsed.relativeTemplatedParent) {
        chain.relativeAncestorType = "$TemplatedParent";
        chain.relativeSelf = false;
    } else {
        chain.relativeAncestorType = parsed.relativeAncestorType;
        chain.relativeSelf = parsed.relativeSelf || isSelfDcBind;
    }
    chain.converter = parsed.converter;
    chain.converterKey = parsed.converterKey;
    chain.converterParameter = parsed.converterParameter;
    chain.stringFormat = parsed.stringFormat;
    ResolveConverterResourceHelper(pkg, chain.converter, chain.converterKey);

    if (!parsed.fallbackValue.empty()) {
        chain.fallbackValue = (targetPropId != 0u && targetPropId != compiler_detail::kUnmappedPropId)
            ? AeroCompiler::ParseLiteralPropertyValue(targetPropId, parsed.fallbackValue)
            : value::FromString(DecodeXamlXmlEntities(parsed.fallbackValue));
    }
    if (parsed.hasTargetNullValue) {
        chain.targetNullValue = parsed.targetNullValue.empty()
            ? value::FromString("")
            : ((targetPropId != 0u && targetPropId != compiler_detail::kUnmappedPropId)
                ? AeroCompiler::ParseLiteralPropertyValue(targetPropId, parsed.targetNullValue)
                : value::FromString(DecodeXamlXmlEntities(parsed.targetNullValue)));
    }

    if (!effPathPart.empty() && effPathPart != ".") {
        const bool hasElemSrc = !chain.elementName.empty() ||
                                !chain.relativeAncestorType.empty() ||
                                chain.relativeSelf;
        const bool keepAttachedSingle = parsed.isAttachedPropertyPath ||
            (hasElemSrc &&
             effPathPart.find('.') != std::string_view::npos &&
             !IsInstanceSubHopPropertyPath(effPathPart) &&
             ResolveMappedSetterPropertyId(effPathPart, ownerType) != compiler_detail::kUnmappedPropId);
        SplitPathIntoHopsHelper(effPathPart, chain.hops, keepAttachedSingle);
    }
    return chain;
}

bool EmitPropertyBindingIfValidHelper(
    XamlCtx& ctx,
    std::uint16_t targetPropId,
    std::string_view rawMarkupExpr) {
    if (targetPropId == compiler_detail::kUnmappedPropId) {
        return false;
    }
    const std::string_view trimmedExpr = str::Trim(rawMarkupExpr);
    if (trimmedExpr.size() <= 2 || trimmedExpr.front() != '{' || trimmedExpr.back() != '}') {
        return false;
    }
    const ParsedBindingClause parsedBind = ParseBindingMarkupExpression(trimmedExpr);
    if (!parsedBind.isValidBinding) {
        return false;
    }

    auto& pkg = ctx.pkg;
    if (parsedBind.relativeTemplatedParent && !TemplatedParentNeedsCompiledChain(parsedBind)) {
        UnifiedValue parsedFallback{};
        if (!parsedBind.fallbackValue.empty()) {
            parsedFallback = AeroCompiler::ParseLiteralPropertyValue(targetPropId, parsedBind.fallbackValue);
            AxbInstruction fbInst{};
            fbInst.opcode = AxbOpcode::SetStaticProperty;
            fbInst.propId = targetPropId;
            fbInst.literalValue = parsedFallback;
            pkg.instructions.push_back(std::move(fbInst));
        }
        const std::uint16_t srcPropId =
            ResolveTemplateBindingSourcePropId(parsedBind.pathPart, t_activeTemplateTargetTypeHint);
        if (srcPropId == compiler_detail::kUnmappedPropId) {
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

    AxbCompiledBindingChain chain = BuildCompiledBindingChainHelper(
        pkg, parsedBind, targetPropId, true, t_activeTemplateTargetTypeHint);
    if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
        AxbInstruction fbInst{};
        fbInst.opcode = AxbOpcode::SetStaticProperty;
        fbInst.propId = targetPropId;
        fbInst.literalValue = chain.fallbackValue;
        pkg.instructions.push_back(std::move(fbInst));
    }

    const auto chainIdx = static_cast<std::uint32_t>(pkg.bindingChains.size());
    pkg.bindingChains.push_back(std::move(chain));

    AxbInstruction bindInst{};
    bindInst.opcode = AxbOpcode::BindProperty;
    bindInst.propId = targetPropId;
    bindInst.flags = static_cast<std::uint8_t>(parsedBind.mode);
    bindInst.strIdx0 = chainIdx;
    pkg.instructions.push_back(std::move(bindInst));
    return true;
}

std::uint16_t FindEnclosingPropertyTargetIdHelper(const std::vector<XmlScopeEntry>& scopeStack) noexcept {
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        if (it->kind == XmlScopeKind::BindingPropertyElement ||
            it->kind == XmlScopeKind::TransparentContainer) {
            if (it->targetPropId != 0 && it->targetPropId != compiler_detail::kUnmappedPropId) {
                return it->targetPropId;
            }
            if (it->targetPropId == PropId_Width && str::EndsWith(str::StripNamespacePrefix(it->tagName), ".Width")) {
                return PropId_Width;
            }
        }
        if (it->kind == XmlScopeKind::VisualElement) {
            break;
        }
    }
    return compiler_detail::kUnmappedPropId;
}

void CommitActionToActiveTriggerHelper(
    XamlParseState& st,
    AxbInteractionAction&& act,
    bool preferEventTrigger) {
    if (preferEventTrigger && !st.eventTriggerStack.empty()) {
        st.eventTriggerStack.back().et.actions.push_back(std::move(act));
    } else if (!st.triggerStack.empty()) {
        if (st.triggerStack.back().exitActionsDepth != 0u) {
            st.triggerStack.back().trig.exitActions.push_back(std::move(act));
        } else {
            st.triggerStack.back().trig.enterActions.push_back(std::move(act));
        }
    } else if (!st.eventTriggerStack.empty()) {
        st.eventTriggerStack.back().et.actions.push_back(std::move(act));
    }
}

void EmitSingleCompiledChainHelper(AxbPackage& pkg, AxbCompiledBindingChain&& chain) {
    if (chain.targetPropertyId == compiler_detail::kUnmappedPropId) {
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
}

void EmitCompiledMultiBindingHelper(AxbPackage& pkg, AxbCompiledMultiBinding&& mb) {
    if (mb.targetPropertyId == compiler_detail::kUnmappedPropId || mb.sources.empty()) {
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
}

UnifiedValue ParseStyleOrAnimationValueHelper(
    XamlCtx& ctx,
    std::uint16_t propId,
    std::string_view rawVal,
    bool& outValid) {
    outValid = false;
    if (propId == compiler_detail::kUnmappedPropId) {
        return UnifiedValue{};
    }
    const std::string_view trimmed = str::Trim(rawVal);
    if (trimmed.size() > 2u && trimmed.front() == '{' && trimmed.back() == '}') {
        const std::string_view innerMarkup = str::Trim(trimmed.substr(1u, trimmed.size() - 2u));
        if (innerMarkup == "x:Null" || innerMarkup.rfind("x:Null ", 0) == 0) {
            outValid = true;
            return NullObject{};
        }
        if (innerMarkup.rfind("x:Static ", 0) == 0 || innerMarkup.rfind("x:Static\t", 0) == 0) {
            std::string_view arg = str::Trim(innerMarkup.substr(8u));
            if (arg.rfind("Member=", 0) == 0) {
                arg = str::Trim(arg.substr(7u));
            }
            if (arg.size() >= 2u && ((arg.front() == '\'' && arg.back() == '\'') || (arg.front() == '"' && arg.back() == '"'))) {
                arg = arg.substr(1u, arg.size() - 2u);
            }
            const auto dotPos = arg.rfind('.');
            const std::string_view memberName = (dotPos != std::string_view::npos) ? arg.substr(dotPos + 1u) : arg;
            if (!memberName.empty()) {
                outValid = true;
                return AeroCompiler::ParseLiteralPropertyValue(propId, memberName);
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
            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, resKey)) {
                outValid = true;
                if (propId == PropId_PathData) {
                    if (const auto* sv = std::get_if<StringValue>(rv)) {
                        EnsurePathBakedIntoPackageHelper(ctx, sv->view());
                    }
                }
                return *rv;
            }
        }
        return UnifiedValue{};
    }
    outValid = true;
    UnifiedValue parsed = AeroCompiler::ParseLiteralPropertyValue(propId, trimmed);
    if (propId == PropId_PathData) {
        if (const auto* sv = std::get_if<StringValue>(&parsed)) {
            EnsurePathBakedIntoPackageHelper(ctx, sv->view());
        }
    }
    return parsed;
}

void CommitSetterToOwnerHelper(XamlCtx& ctx, AxbPropertySetter&& s, SetterOwnerKind owner) {
    auto& st = ctx.st;
    if (s.propId == compiler_detail::kUnmappedPropId ||
        (!s.hasBinding && std::holds_alternative<std::monostate>(s.value))) {
        return;
    }
    if (owner == SetterOwnerKind::VisualState && !st.visualStateStack.empty()) {
        st.visualStateStack.back().state.setters.push_back(std::move(s));
    } else if (owner == SetterOwnerKind::Trigger && !st.triggerStack.empty()) {
        st.triggerStack.back().trig.setters.push_back(std::move(s));
    } else if (owner == SetterOwnerKind::Style && !st.styleStack.empty()) {
        if (s.propId == PropId_Template) {
            if (const auto* sv = std::get_if<StringValue>(&s.value)) {
                st.styleStack.back().def.controlTemplateKey = sv->str();
                if (const AxbControlTemplate* foundCt = ctx.pkg.FindControlTemplate(sv->view())) {
                    st.styleStack.back().def.inlineControlTemplate =
                        std::make_shared<AxbControlTemplate>(*foundCt);
                }
            }
        }
        st.styleStack.back().def.setters.push_back(std::move(s));
    }
}

AERO_NOINLINE bool BuildDataTriggerConditionHelper(
    XamlCtx& ctx,
    std::string_view bindingAttr,
    std::string_view valAttr,
    bool hasValAttr,
    AxbTriggerCondition& outCond,
    std::string_view ownerType = {}) {
    if (!hasValAttr) {
        return false;
    }
    const ParsedBindingClause parsed = ParseBindingMarkupExpression(bindingAttr);
    if (!parsed.isValidBinding) {
        return false;
    }
    const std::string_view trimmedVal = str::Trim(valAttr);
    outCond = AxbTriggerCondition{};
    outCond.converter = parsed.converter;
    outCond.converterKey = parsed.converterKey;
    outCond.converterParameter = parsed.converterParameter;
    ResolveConverterResourceHelper(ctx.pkg, outCond.converter, outCond.converterKey);
    if (trimmedVal.size() > 2u && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
        const std::string_view innerV = str::Trim(trimmedVal.substr(1u, trimmedVal.size() - 2u));
        if (innerV.rfind("x:Null", 0) == 0) {
            outCond.expectNull = true;
        } else {
            const std::string_view resK = ExtractMarkupResourceKey(trimmedVal);
            if (!resK.empty()) {
                if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, resK)) {
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

    const std::string_view effOwner = !ownerType.empty() ? ownerType : t_activeTemplateTargetTypeHint;
    if (parsed.relativeTemplatedParent) {
        outCond.sourceKind = TriggerConditionSource::AncestorProperty;
        outCond.sourceName = "$TemplatedParent";
        outCond.bindingPath = std::string(parsed.pathPart);
        std::string_view firstSeg;
        splitSubObjectHopsAfterFirstDot(parsed.pathPart, firstSeg);
        outCond.propId = ResolveMappedTriggerConditionId(firstSeg, effOwner);
        if (outCond.propId == compiler_detail::kUnmappedPropId) return false;
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            if (!outCond.compiledHops.empty()) {
                outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            } else {
                bool ok = false;
                outCond.expectedValue = ParseStyleOrAnimationValueHelper(ctx, outCond.propId, trimmedVal, ok);
                if (!ok) outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            }
        }
        return true;
    }

    if (parsed.relativeSelf) {
        outCond.sourceKind = TriggerConditionSource::SelfProperty;
        outCond.bindingPath = std::string(parsed.pathPart);
        std::string_view firstSeg;
        splitSubObjectHopsAfterFirstDot(parsed.pathPart, firstSeg);
        outCond.propId = ResolveMappedTriggerConditionId(firstSeg, effOwner);
        if (outCond.propId == compiler_detail::kUnmappedPropId) return false;
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            if (!outCond.compiledHops.empty()) {
                outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            } else {
                bool ok = false;
                outCond.expectedValue = ParseStyleOrAnimationValueHelper(ctx, outCond.propId, trimmedVal, ok);
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
        std::string_view namedOwner;
        if (const auto it = ctx.st.namedElementTypes.find(outCond.sourceName);
            it != ctx.st.namedElementTypes.end()) {
            namedOwner = it->second;
        }
        outCond.propId = ResolveMappedTriggerConditionId(firstSeg, namedOwner);
        if (outCond.propId == compiler_detail::kUnmappedPropId) return false;
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            if (!outCond.compiledHops.empty() ||
                outCond.propId == PropId_SelectedItem || outCond.propId == PropId_Text ||
                outCond.propId == PropId_Content || outCond.propId == PropId_Tag) {
                outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            } else {
                bool ok = false;
                outCond.expectedValue = ParseStyleOrAnimationValueHelper(ctx, outCond.propId, trimmedVal, ok);
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
        outCond.propId = ResolveMappedTriggerConditionId(firstSeg, parsed.relativeAncestorType);
        if (outCond.propId == compiler_detail::kUnmappedPropId) return false;
        if (!outCond.expectNull && std::holds_alternative<std::monostate>(outCond.expectedValue)) {
            if (!outCond.compiledHops.empty()) {
                outCond.expectedValue = value::FromString(DecodeXamlXmlEntities(trimmedVal));
            } else {
                bool ok = false;
                outCond.expectedValue = ParseStyleOrAnimationValueHelper(ctx, outCond.propId, trimmedVal, ok);
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
}

bool IsNonVisualElementTagHelper(bool isCompilingVisualResourceMock, std::string_view shortTag) noexcept {
    if (isCompilingVisualResourceMock &&
        (shortTag == "GradientStop" || shortTag == "TransformGroup" ||
         shortTag == "TranslateTransform" || shortTag == "RotateTransform" ||
         shortTag == "ScaleTransform" || shortTag == "SkewTransform")) {
        return false;
    }
    bool isTemplatedControl = false;
    if (IsCustomControlRegistered(shortTag, &isTemplatedControl)) {
        (void)isTemplatedControl;
        return false;
    }
    if (FindCustomAnimationType(shortTag) != nullptr) {
        return true;
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
           shortTag == "KeyTrigger" ||
           shortTag == "TimerTrigger" ||
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
           shortTag == "TintEffect" ||
           shortTag == "ChromaticAberrationEffect" ||
           shortTag == "DirectionalBlurEffect" ||
           shortTag == "SaturationEffect" ||
           shortTag == "BackgroundEffectBehavior" ||
           shortTag == "MouseDragElementBehavior" ||
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
           shortTag == "ConicGradientBrush" ||
           shortTag == "SolidColorBrush" ||
           shortTag == "PlasmaBrush" ||
           shortTag == "SaturationBrush" ||
           shortTag == "WavesBrush" ||
           shortTag == "MonochromeBrush" ||
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
           shortTag == "GoToStateAction" ||
           shortTag == "LaunchUriOrFileAction" ||
           shortTag == "LaunchUriAction" ||
           shortTag == "PlayMediaAction" ||
           shortTag == "StopMediaAction" ||
           shortTag == "ComparisonCondition" ||
           shortTag == "ConditionalExpression" ||
           shortTag == "ConditionBehavior" ||
           shortTag == "PropertyChangedTrigger" ||
           shortTag == "StoryboardCompletedTrigger" ||
           shortTag == "SetFocusAction" ||
           shortTag == "SelectAction" ||
           shortTag == "SelectAllAction";
}

std::size_t PushNonVisualScopeHelper(
    XamlCtx& ctx,
    std::string_view tagName,
    XmlScopeKind sk = XmlScopeKind::NonVisualSkip) {
    XmlScopeEntry entry{};
    entry.kind = sk;
    entry.tagName = std::string(tagName);
    ctx.st.scopeStack.push_back(std::move(entry));
    ++ctx.st.nonVisualDepth;
    return ctx.st.scopeStack.size();
}

AERO_NOINLINE void EmitMouseDragElementBehaviorHelper(
    XamlCtx& ctx,
    const OpenTagInfo& tag) {
    bool constrainStatic = false;
    bool hasConstrainBinding = false;
    std::string_view constrainBindingExpr;
    std::string_view constrainResKey;
    std::uint8_t constrainResFlags = AxbResourceFlag_StaticResource;

    for (const auto& a : tag.attrs) {
        if (a.name == "ConstrainToParentBounds") {
            const std::string_view tv = str::Trim(a.value);
            if (tv.size() > 2u && tv.front() == '{' && tv.back() == '}') {
                std::string_view rk;
                if (TryParseResourceMarkupExtension(tv, rk, constrainResFlags)) {
                    constrainResKey = rk;
                    if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, rk)) {
                        if (const auto* bv = std::get_if<bool>(rv)) {
                            constrainStatic = *bv;
                        } else if (const auto* sv = std::get_if<StringValue>(rv)) {
                            constrainStatic = str::EqualsIgnoreCase(str::Trim(sv->view()), "true");
                        }
                    }
                } else {
                    hasConstrainBinding = true;
                    constrainBindingExpr = tv;
                }
            } else {
                const std::string dec = DecodeXamlXmlEntities(tv);
                constrainStatic = str::EqualsIgnoreCase(str::Trim(dec), "true") || str::Trim(dec) == "1";
            }
        }
    }

    AxbInstruction baseInst{};
    baseInst.opcode = AxbOpcode::SetStaticProperty;
    baseInst.propId = PropId_MouseDragConstrain;
    baseInst.literalValue = UnifiedValue{constrainStatic};
    ctx.pkg.instructions.push_back(std::move(baseInst));

    if (!constrainResKey.empty()) {
        AxbInstruction resInst{};
        resInst.opcode = AxbOpcode::SetResourceProperty;
        resInst.propId = PropId_MouseDragConstrain;
        resInst.flags = constrainResFlags;
        resInst.strIdx0 = ctx.pkg.InternString(constrainResKey);
        if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, constrainResKey)) {
            resInst.literalValue = *rv;
        }
        ctx.pkg.instructions.push_back(std::move(resInst));
    } else if (hasConstrainBinding) {
        (void)EmitPropertyBindingIfValidHelper(ctx, PropId_MouseDragConstrain, constrainBindingExpr);
    }

    if (!tag.selfClosing) {
        (void)PushNonVisualScopeHelper(ctx, tag.tagName);
    }
}

GradientBrushDesc ParseGradientBrushHeaderHelper(const OpenTagInfo& tag) {
    GradientBrushDesc d{};
    if (tag.shortTag == "RadialGradientBrush") {
        d.kind = GradientBrushKind::Radial;
    } else if (tag.shortTag == "ConicGradientBrush") {
        d.kind = GradientBrushKind::Conic;
    } else {
        d.kind = GradientBrushKind::Linear;
    }
    d.startPoint = Point{0.0f, 0.0f};
    d.endPoint = Point{1.0f, 1.0f};
    d.center = Point{0.5f, 0.5f};
    d.gradientOrigin = Point{0.5f, 0.5f};
    d.radiusX = 0.5f;
    d.radiusY = 0.5f;
    d.opacity = 1.0f;
    for (const auto& a : tag.attrs) {
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
            d.radiusX = std::max(1e-5f, compiler_detail::ParseFloat32(a.value, d.radiusX));
        } else if (a.name == "RadiusY") {
            d.radiusY = std::max(1e-5f, compiler_detail::ParseFloat32(a.value, d.radiusY));
        } else if (a.name == "Opacity") {
            d.opacity = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
        } else if (a.name == "MappingMode") {
            d.mappingMode = str::EqualsIgnoreCase(str::Trim(a.value), "Absolute")
                ? BrushMappingMode::Absolute
                : BrushMappingMode::RelativeToBoundingBox;
        } else if (a.name == "SpreadMethod") {
            const std::string_view sm = str::Trim(a.value);
            if (str::EqualsIgnoreCase(sm, "Reflect")) {
                d.spreadMethod = GradientSpreadMethod::Reflect;
            } else if (str::EqualsIgnoreCase(sm, "Repeat")) {
                d.spreadMethod = GradientSpreadMethod::Repeat;
            } else {
                d.spreadMethod = GradientSpreadMethod::Pad;
            }
        }
    }
    return d;
}

bool HasEffectKindOnCurrentVisualElement(const AxbPackage& pkg) noexcept {
    for (auto it = pkg.instructions.rbegin(); it != pkg.instructions.rend(); ++it) {
        if (it->opcode == AxbOpcode::BeginElement || it->opcode == AxbOpcode::EndElement) {
            break;
        }
        if ((it->opcode == AxbOpcode::SetStaticProperty ||
             it->opcode == AxbOpcode::BindProperty ||
             it->opcode == AxbOpcode::BindTemplateParent ||
             it->opcode == AxbOpcode::SetResourceProperty) &&
            it->propId == PropId_EffectKind) {
            return true;
        }
    }
    return false;
}

bool EmitDynamicResourceOrBindingForPropHelper(
    XamlCtx& ctx,
    std::uint16_t propId,
    std::string_view rawExpr) {
    const std::string_view tv = str::Trim(rawExpr);
    if (tv.size() <= 2u || tv.front() != '{') {
        return false;
    }
    std::string_view resKey;
    std::uint8_t resFlags = 0u;
    if (TryParseResourceMarkupExtension(tv, resKey, resFlags) && !resKey.empty()) {
        AxbInstruction rInst{};
        rInst.opcode = AxbOpcode::SetResourceProperty;
        rInst.propId = propId;
        rInst.flags = resFlags;
        rInst.strIdx0 = ctx.pkg.InternString(resKey);
        if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, resKey)) {
            rInst.literalValue = *rv;
        }
        ctx.pkg.instructions.push_back(std::move(rInst));
        return true;
    }
    return EmitPropertyBindingIfValidHelper(ctx, propId, tv);
}

AERO_NOINLINE int TryHandleSubPackageTemplateTagNoInline(
    XamlCtx& ctx,
    XmlScopeEntry& parentScope,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    auto& styleStack = state.styleStack;
    auto& setterStack = state.setterStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xKey = tag.xKey;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;
    const std::string_view src = ctx.src;

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
                    colWidth = std::string(str::Trim(a.value));
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
                if (FindMatchingClosingTag(src, ctx.pos, "GridViewColumn", colInnerEnd, colAfterClose)) {
                    const std::string_view colBody = src.substr(ctx.pos, colInnerEnd - ctx.pos);
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
                            const std::size_t absDtAfterOpen = ctx.pos + dtTagGt + 1u;
                            if (FindMatchingClosingTag(src, absDtAfterOpen, "DataTemplate", dtInnerEnd, dtAfterClose)) {
                                const std::string_view dtInnerXml = src.substr(absDtAfterOpen, dtInnerEnd - absDtAfterOpen);
                                if (!str::Trim(dtInnerXml).empty()) {
                                    AxbTemplateRegistry::Instance().RegisterResources(pkg);
                                    auto subIn = std::make_unique<AeroCompileInput>();
                                    subIn->xamlPath = ctx.input.xamlPath;
                                    subIn->xamlContent = std::string(dtInnerXml);
                                    subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                                    subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                                    subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                                    subIn->autoMergeGlobalResources = false;
                                    auto subRes = CompileSubPackageHeap(
                                        ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                                    for (const auto& d : subRes->diagnostics) {
                                        ctx.result.diagnostics.push_back(d);
                                    }
                                    if (!subRes->succeeded) {
                                        ctx.result.succeeded = false;
                                        return -1;
                                    }
                                    for (const auto& mp : subRes->axbPackage.mtsdfPaths) {
                                        if (mp.pathHash != 0 && state.bakedPathHashes.insert(mp.pathHash).second) {
                                            pkg.mtsdfPaths.push_back(mp);
                                        }
                                    }
                                    const std::string genKey = "__aero_gvc_dt__:" +
                                        pkg.className + ":" + std::to_string(pkg.dataTemplates.size());
                                    auto dt = std::make_unique<AxbDataTemplate>();
                                    dt->key = genKey;
                                    dt->templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                                    AxbTemplateRegistry::Instance().RegisterDataTemplate(*dt);
                                    pkg.dataTemplates.push_back(std::move(*dt));
                                    colCellTpl = genKey;
                                }
                            } else {
                                ctx.result.diagnostics.push_back(
                                    "AERO_XAML_UNCLOSED_TAG: Unclosed <DataTemplate> in <GridViewColumn>");
                                ctx.result.succeeded = false;
                                return -1;
                            }
                        }
                    }
                    ctx.pos = colAfterClose;
                    consumedFullColumn = true;
                }
            }
            if (!lvViewScope->gridViewColumnsSpec.empty()) {
                lvViewScope->gridViewColumnsSpec.push_back('\x1e');
            }
            lvViewScope->gridViewColumnsSpec +=
                colHeader + "\x1f" + colWidth + "\x1f" + colBindPath + "\x1f" + colFormat + "\x1f" + colCellTpl;
            if (!selfClosing && !consumedFullColumn) {
                (void)PushNonVisualScopeHelper(ctx, tagName);
            }
            return 1;
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
            if (FindMatchingClosingTag(src, ctx.pos, "ContextMenu", innerEnd, afterClose)) {
                const std::string_view innerXml = src.substr(ctx.pos, innerEnd - ctx.pos);
                const std::string outerScopeName = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                const std::string cmKey = !xKey.empty()
                    ? std::string(xKey)
                    : ("__ctx_menu_" + outerScopeName + "_" + std::to_string(state.nextInlineControlTplId++));
                FlushDeferredMtsdfJobsNoInline(ctx);
                AxbTemplateRegistry::Instance().RegisterResources(pkg);
                auto subIn = std::make_unique<AeroCompileInput>();
                subIn->xamlPath = cmKey;
                subIn->xamlContent = "<ContextMenu>" + std::string(innerXml) + "</ContextMenu>";
                subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                subIn->autoMergeGlobalResources = false;
                auto subRes = CompileSubPackageHeap(
                    ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                if (subRes->succeeded && !subRes->axbPackage.instructions.empty()) {
                    for (const auto& mp : subRes->axbPackage.mtsdfPaths) {
                        if (mp.pathHash != 0 && state.bakedPathHashes.insert(mp.pathHash).second) {
                            pkg.mtsdfPaths.push_back(mp);
                        }
                    }
                    auto ct = std::make_unique<AxbControlTemplate>();
                    ct->key = cmKey;
                    ct->targetTypeName = "ContextMenu";
                    ct->templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                    AxbTemplateRegistry::Instance().RegisterControlTemplate(*ct);
                    pkg.controlTemplates.push_back(std::move(*ct));
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
                ctx.pos = afterClose;
                return 1;
            }
        } else {
            return 1;
        }
    }

    // Compile <DataTemplate> / <HierarchicalDataTemplate>
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
                    : std::string(str::Trim(a.value));
            } else if (a.name == "ItemTemplate") {
                const std::string_view rk = ExtractMarkupResourceKey(a.value);
                dtChildItemTemplateKey = !rk.empty() ? std::string(rk) : std::string(str::Trim(a.value));
            }
        }
        if (!selfClosing) {
            std::size_t innerEnd = 0u;
            std::size_t afterClose = 0u;
            if (FindMatchingClosingTag(src, ctx.pos, shortTag, innerEnd, afterClose)) {
                const std::string_view innerXml = src.substr(ctx.pos, innerEnd - ctx.pos);
                const std::string outerScopeName = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                std::string dtKey;
                if (parentScope.isItemTemplateContainer || isInlineSetterDt) {
                    dtKey = "__item_tpl_" + outerScopeName + "_" +
                            std::to_string(state.nextInlineDataTplId++);
                } else if (!xKey.empty()) {
                    dtKey = std::string(xKey);
                } else {
                    dtKey = "__implicit_dt_" + outerScopeName + "_" +
                            (dtDataType.empty() ? std::to_string(state.nextInlineDataTplId++) : dtDataType);
                }
                FlushDeferredMtsdfJobsNoInline(ctx);
                AxbTemplateRegistry::Instance().RegisterResources(pkg);
                auto subIn = std::make_unique<AeroCompileInput>();
                subIn->xamlPath = dtKey;
                subIn->xamlContent = std::string(innerXml);
                subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                subIn->autoMergeGlobalResources = false;
                auto subRes = CompileSubPackageHeap(
                    ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                if (subRes->succeeded && !subRes->axbPackage.instructions.empty()) {
                    for (const auto& mp : subRes->axbPackage.mtsdfPaths) {
                        if (mp.pathHash != 0 && state.bakedPathHashes.insert(mp.pathHash).second) {
                            pkg.mtsdfPaths.push_back(mp);
                        }
                    }
                    auto dt = std::make_unique<AxbDataTemplate>();
                    if (isInlineSetterDt) {
                        dt->key = dtKey.empty()
                            ? ("__setter_dt_" + outerScopeName + "_" + std::to_string(state.nextInlineDataTplId++))
                            : dtKey;
                        setterStack.back().setter.value = value::FromString(dt->key);
                        setterStack.back().hasValue = true;
                    } else if (parentScope.isItemTemplateContainer) {
                        dt->key = dtKey;
                        AxbInstruction setTplInst{};
                        setTplInst.opcode = AxbOpcode::SetStaticProperty;
                        setTplInst.propId = (parentScope.targetPropId != 0 && parentScope.targetPropId != PropId_Background)
                            ? parentScope.targetPropId
                            : static_cast<std::uint16_t>(PropId_ItemTemplate);
                        setTplInst.literalValue = value::FromString(dt->key);
                        pkg.instructions.push_back(std::move(setTplInst));
                    } else {
                        dt->key = std::string(xKey);
                    }
                    dt->dataTypeName = std::move(dtDataType);
                    dt->itemsSourcePath = std::move(dtItemsSourcePath);
                    dt->childItemTemplateKey = std::move(dtChildItemTemplateKey);
                    dt->templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                    AxbTemplateRegistry::Instance().RegisterDataTemplate(*dt);
                    pkg.dataTemplates.push_back(std::move(*dt));
                }
                ctx.pos = afterClose;
                return 1;
            }
        } else {
            return 1;
        }
    }

    return 0;
}

AERO_NOINLINE void HandleGraphicsEffectsShadersAndResourcesNoInline(
    XamlCtx& ctx,
    XmlScopeEntry& parentScope,
    const OpenTagInfo& tag,
    std::size_t tagStartLt) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    auto& setterStack = state.setterStack;
    auto& gradientStack = state.gradientStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xName = tag.xName;
    const auto xKey = tag.xKey;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;
    const std::string_view src = ctx.src;

    if (!gradientStack.empty() && shortTag == "GradientStop") {
        auto& activeGrad = gradientStack.back().desc;
        float stopOffset = activeGrad.stops.empty() ? 0.0f : 1.0f;
        Color stopColor = activeGrad.stops.empty()
            ? Color{1.0f, 0.0f, 0.0f, 1.0f}
            : Color{0.0f, 1.0f, 0.0f, 1.0f};
        for (const auto& a : attrs) {
            if (a.name == "Offset") {
                if (!a.value.empty() && a.value.front() != '{') {
                    stopOffset = compiler_detail::ParseFloat32(a.value, stopOffset);
                }
            } else if (a.name == "Color") {
                const std::string_view resKey = ExtractMarkupResourceKey(a.value);
                if (!resKey.empty()) {
                    if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, resKey)) {
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
        setterStack.back().setter.propId != compiler_detail::kUnmappedPropId) {
        if (shortTag == "SolidColorBrush") {
            float brushAlphaMul = 1.0f;
            for (const auto& a : attrs) {
                if (a.name == "Opacity") {
                    brushAlphaMul = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
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
        } else if (shortTag == "LinearGradientBrush" ||
                   shortTag == "RadialGradientBrush" ||
                   shortTag == "ConicGradientBrush") {
            GradientBrushDesc gd = ParseGradientBrushHeaderHelper(tag);
            if (selfClosing) {
                gd.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                gd.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                BakeGradientBrushRamp(gd);
                const std::string spec = SerializeGradientBrushSpec(gd);
                (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                setterStack.back().setter.value = value::FromString(spec);
                setterStack.back().hasValue = true;
                return;
            }
            (void)PushNonVisualScopeHelper(ctx, tagName);

            ActiveGradientBrushScope gs{};
            gs.desc = std::move(gd);
            gs.owner = GradientScopeOwner::StyleSetter;
            gs.scopeDepth = scopeStack.size();
            gradientStack.push_back(std::move(gs));
            return;
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
                    !str::Trim(a.value).empty()) {
                    const std::string_view rawU = str::Trim(a.value);
                    if (rawU.front() == '{') {
                        const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                        if (!refKey.empty()) {
                            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, refKey)) {
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
                ++state.nonVisualDepth;
                return;
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
            ++state.nonVisualDepth;
        }
        return;
    }

    if (shortTag == "ResourceDictionary" ||
        shortTag == "ResourceDictionary.MergedDictionaries" ||
        str::EndsWith(shortTag, ".MergedDictionaries") ||
        str::EndsWith(shortTag, ".Resources")) {
        if (shortTag == "ResourceDictionary") {
            for (const auto& a : attrs) {
                if (a.name == "Source" && !str::Trim(a.value).empty()) {
                    CompileAndMergeExternalDictionaryNoInline(ctx, DecodeXamlXmlEntities(a.value));
                    break;
                }
            }
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName, XmlScopeKind::ResourceContainer);
        }
        return;
    }

    if (shortTag == "MouseDragElementBehavior") {
        EmitMouseDragElementBehaviorHelper(ctx, tag);
        return;
    }

    // 2b. P1-5: Active <Element.Transform3D> / <UIElement.Projection> subtree
    if (parentScope.kind == XmlScopeKind::Transform3DPropertyElement &&
        (shortTag == "CompositeTransform3D" || shortTag == "PlaneProjection")) {
        CompositeTransform3DParams p3d{};
        for (const auto& a : attrs) {
            const std::string_view v = str::Trim(a.value);
            if (v.empty()) continue;
            if (v.front() == '{') {
                if (a.name == "RotationX") (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimRotation3DX, v);
                else if (a.name == "RotationY") (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimRotation3DY, v);
                else if (a.name == "RotationZ") (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimRotation3DZ, v);
                else if (a.name == "TranslateX" || a.name == "LocalOffsetX" || a.name == "GlobalOffsetX")
                    (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimTranslate3DX, v);
                else if (a.name == "TranslateY" || a.name == "LocalOffsetY" || a.name == "GlobalOffsetY")
                    (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimTranslate3DY, v);
                else if (a.name == "TranslateZ" || a.name == "LocalOffsetZ" || a.name == "GlobalOffsetZ")
                    (void)EmitPropertyBindingIfValidHelper(ctx, PropId_AnimTranslate3DZ, v);
                continue;
            }
            if (a.name == "RotationX") p3d.rotationX = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "RotationY") p3d.rotationY = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "RotationZ") p3d.rotationZ = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "TranslateX" || a.name == "LocalOffsetX" || a.name == "GlobalOffsetX")
                p3d.translateX = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "TranslateY" || a.name == "LocalOffsetY" || a.name == "GlobalOffsetY")
                p3d.translateY = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "TranslateZ" || a.name == "LocalOffsetZ" || a.name == "GlobalOffsetZ")
                p3d.translateZ = compiler_detail::ParseFloat32(v, 0.0f);
            else if (a.name == "ScaleX") p3d.scaleX = compiler_detail::ParseFloat32(v, 1.0f);
            else if (a.name == "ScaleY") p3d.scaleY = compiler_detail::ParseFloat32(v, 1.0f);
            else if (a.name == "ScaleZ") p3d.scaleZ = compiler_detail::ParseFloat32(v, 1.0f);
            else if (a.name == "CenterX") { p3d.centerX = compiler_detail::ParseFloat32(v, 0.0f); p3d.hasExplicitCenter = true; }
            else if (a.name == "CenterY") { p3d.centerY = compiler_detail::ParseFloat32(v, 0.0f); p3d.hasExplicitCenter = true; }
            else if (a.name == "CenterZ") { p3d.centerZ = compiler_detail::ParseFloat32(v, 0.0f); }
            else if (a.name == "CameraDistance") { p3d.cameraDistance = compiler_detail::ParseFloat32(v, 1000.0f); }
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return;
    }

    // 2c-pre: <noesis:BackgroundEffectBehavior> containing <BlurEffect Radius="..."/>
    if (shortTag == "BlurEffect" && parentScope.kind != XmlScopeKind::EffectPropertyElement &&
        parentScope.kind != XmlScopeKind::ResourceContainer) {
        bool insideBgEffectBehavior = false;
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::VisualElement) break;
            if (str::StripNamespacePrefix(it->tagName) == "BackgroundEffectBehavior") {
                insideBgEffectBehavior = true;
                break;
            }
        }
        if (insideBgEffectBehavior) {
            float radius = 10.0f;
            for (const auto& a : attrs) {
                if (a.name == "Radius") {
                    const std::string_view v = str::Trim(a.value);
                    if (!v.empty() && v.front() == '{') {
                        (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectBlurRadius, v);
                    } else {
                        radius = std::max(0.0f, compiler_detail::ParseFloat32(v, 10.0f));
                    }
                }
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(4)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f)});
            if (!selfClosing) {
                (void)PushNonVisualScopeHelper(ctx, tagName);
            }
            return;
        }
    }

    // 2c-path: Structured <PathGeometry> -> <PathFigure> -> segments
    if (shortTag == "PathFigure" || shortTag == "LineSegment" || shortTag == "PolyLineSegment" ||
        shortTag == "BezierSegment" || shortTag == "QuadraticBezierSegment" || shortTag == "ArcSegment") {
        XmlScopeEntry* geomOwner = nullptr;
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::VisualElement) break;
            if ((it->kind == XmlScopeKind::TransparentContainer && it->targetPropId == PropId_PathData) ||
                (it->kind == XmlScopeKind::KeyedResourceItem &&
                 (str::StripNamespacePrefix(it->tagName) == "PathGeometry" || str::StripNamespacePrefix(it->tagName) == "Geometry"))) {
                geomOwner = &(*it);
                break;
            }
        }
        if (geomOwner != nullptr) {
            if (shortTag == "PathFigure") {
                std::string startPt = "0,0";
                bool isClosed = false;
                for (const auto& a : attrs) {
                    if (a.name == "StartPoint") startPt = std::string(str::Trim(a.value));
                    else if (a.name == "IsClosed") {
                        const std::string_view tv = str::Trim(a.value);
                        isClosed = str::EqualsIgnoreCase(tv, "true") || tv == "1";
                    }
                }
                if (!geomOwner->accumulatedText.empty()) geomOwner->accumulatedText.push_back(' ');
                geomOwner->accumulatedText += "M " + startPt;
                if (!selfClosing) {
                    XmlScopeEntry figScope{};
                    figScope.kind = XmlScopeKind::NonVisualSkip;
                    figScope.tagName = isClosed ? "PathFigure:Closed" : "PathFigure";
                    scopeStack.push_back(std::move(figScope));
                    ++state.nonVisualDepth;
                } else if (isClosed) {
                    geomOwner->accumulatedText += " Z";
                }
                return;
            } else if (shortTag == "LineSegment") {
                for (const auto& a : attrs) {
                    if (a.name == "Point" && !str::Trim(a.value).empty()) {
                        geomOwner->accumulatedText += " L " + std::string(str::Trim(a.value));
                    }
                }
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "PolyLineSegment") {
                for (const auto& a : attrs) {
                    if (a.name == "Points" && !str::Trim(a.value).empty()) {
                        geomOwner->accumulatedText += " L " + std::string(str::Trim(a.value));
                    }
                }
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "BezierSegment") {
                std::string p1 = "0,0", p2 = "0,0", p3 = "0,0";
                for (const auto& a : attrs) {
                    if (a.name == "Point1") p1 = std::string(str::Trim(a.value));
                    else if (a.name == "Point2") p2 = std::string(str::Trim(a.value));
                    else if (a.name == "Point3") p3 = std::string(str::Trim(a.value));
                }
                geomOwner->accumulatedText += " C " + p1 + " " + p2 + " " + p3;
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "QuadraticBezierSegment") {
                std::string p1 = "0,0", p2 = "0,0";
                for (const auto& a : attrs) {
                    if (a.name == "Point1") p1 = std::string(str::Trim(a.value));
                    else if (a.name == "Point2") p2 = std::string(str::Trim(a.value));
                }
                geomOwner->accumulatedText += " Q " + p1 + " " + p2;
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "ArcSegment") {
                std::string pt = "0,0", sz = "1,1", rot = "0";
                int largeArc = 0, sweep = 0;
                for (const auto& a : attrs) {
                    if (a.name == "Point") pt = std::string(str::Trim(a.value));
                    else if (a.name == "Size") sz = std::string(str::Trim(a.value));
                    else if (a.name == "RotationAngle") rot = std::string(str::Trim(a.value));
                    else if (a.name == "IsLargeArc") largeArc = str::EqualsIgnoreCase(str::Trim(a.value), "true") ? 1 : 0;
                    else if (a.name == "SweepDirection") sweep = str::EqualsIgnoreCase(str::Trim(a.value), "Clockwise") ? 1 : 0;
                }
                geomOwner->accumulatedText += " A " + sz + " " + rot + " " +
                    std::to_string(largeArc) + " " + std::to_string(sweep) + " " + pt;
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            }
        }
    }

    // 2c-shader: <SaturationBrush/> / <ConicGradientBrush/> / <PlasmaBrush/> / <WavesBrush/> / <MonochromeBrush/>
    if (shortTag == "SaturationBrush" &&
        state.styleStack.empty() && state.setterStack.empty()) {
        if (!xName.empty()) {
            state.namedElementTypes[std::string(xName)] = std::string(tagName);
            EnsureEnclosingElementHasNameIfEmpty(pkg, xName);
        }
        float satVal = 1.0f;
        std::string_view satDynamicExpr;
        for (const auto& a : attrs) {
            if (a.name == "Saturation") {
                const std::string_view v = str::Trim(a.value);
                if (!v.empty() && v.front() == '{') {
                    satDynamicExpr = v;
                } else {
                    satVal = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 4.0f);
                }
            }
        }
        if (!HasEffectKindOnCurrentVisualElement(pkg)) {
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(8)});
        }
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::SetStaticProperty, PropId_Saturation, 0, 0, 0, 0, MakeFloat(satVal)});
        if (!satDynamicExpr.empty()) {
            (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_Saturation, satDynamicExpr);
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return;
    }

    if ((shortTag == "PlasmaBrush" || shortTag == "ConicGradientBrush" ||
         shortTag == "WavesBrush" || shortTag == "MonochromeBrush") &&
        parentScope.kind != XmlScopeKind::ResourceContainer) {
        XmlScopeEntry* brushOwner = nullptr;
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::VisualElement) break;
            if (it->kind == XmlScopeKind::BrushPropertyElement) {
                brushOwner = &(*it);
                break;
            }
        }
        if (shortTag == "ConicGradientBrush" &&
            state.styleStack.empty() && state.setterStack.empty() &&
            brushOwner != nullptr) {
            GradientBrushDesc gd = ParseGradientBrushHeaderHelper(tag);
            brushOwner->brushAssigned = true;
            if (selfClosing) {
                gd.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                gd.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                BakeGradientBrushRamp(gd);
                const std::string spec = SerializeGradientBrushSpec(gd);
                (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                AxbInstruction setInst{};
                setInst.opcode = AxbOpcode::SetStaticProperty;
                setInst.propId = brushOwner->targetPropId;
                setInst.literalValue = value::FromString(spec);
                pkg.instructions.push_back(std::move(setInst));
                return;
            }
            const std::uint16_t gradTargetPropId = brushOwner->targetPropId;
            XmlScopeEntry gradScope{};
            gradScope.kind = XmlScopeKind::BrushPropertyElement;
            gradScope.tagName = std::string(tagName);
            gradScope.targetPropId = gradTargetPropId;
            gradScope.brushAssigned = true;
            scopeStack.push_back(std::move(gradScope));
            ++state.nonVisualDepth;

            ActiveGradientBrushScope gs{};
            gs.desc = std::move(gd);
            gs.owner = GradientScopeOwner::PropertyElement;
            gs.scopeDepth = scopeStack.size();
            gs.targetPropId = gradTargetPropId;
            gradientStack.push_back(std::move(gs));
            return;
        }
        if (state.styleStack.empty() && state.setterStack.empty()) {
            if (brushOwner != nullptr && !brushOwner->brushAssigned) {
                GradientBrushDesc gd{};
                gd.kind = (shortTag == "PlasmaBrush") ? GradientBrushKind::Radial : GradientBrushKind::Linear;
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
            if (shortTag == "PlasmaBrush") {
                if (!xName.empty()) {
                    state.namedElementTypes[std::string(xName)] = std::string(tagName);
                    EnsureEnclosingElementHasNameIfEmpty(pkg, xName);
                }
                float shaderTime = 0.0f;
                std::string_view timeDynamicExpr;
                std::string_view colorDynamicExpr;
                bool hasStaticColor = false;
                Color plasmaCol{1.0f, 1.0f, 1.0f, 1.0f};
                for (const auto& a : attrs) {
                    const std::string_view v = str::Trim(a.value);
                    if (a.name == "Time") {
                        if (!v.empty() && v.front() == '{') {
                            timeDynamicExpr = v;
                        } else {
                            shaderTime = compiler_detail::ParseFloat32(v, 0.0f);
                        }
                    } else if (a.name == "Color") {
                        if (!v.empty() && v.front() == '{') {
                            colorDynamicExpr = v;
                        } else {
                            plasmaCol = ParseXamlColorLiteral(v);
                            hasStaticColor = true;
                        }
                    }
                }
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_ShaderTime, 0, 0, 0, 0, MakeFloat(shaderTime)});
                if (!timeDynamicExpr.empty()) {
                    (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_ShaderTime, timeDynamicExpr);
                }
                if (hasStaticColor) {
                    pkg.instructions.push_back(AxbInstruction{
                        AxbOpcode::SetStaticProperty, PropId_EffectColor, 0, 0, 0, 0, value::FromColor(plasmaCol)});
                } else if (!colorDynamicExpr.empty()) {
                    (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectColor, colorDynamicExpr);
                }
            }
        }
        if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
        return;
    }

    // 2c. P1-5 & Extended Effects: Active <UIElement.Effect>
    if (parentScope.kind == XmlScopeKind::EffectPropertyElement &&
        (shortTag == "BlurEffect" || shortTag == "DropShadowEffect" || shortTag == "PixelateEffect" ||
         shortTag == "TintEffect" || shortTag == "ChromaticAberrationEffect" ||
         shortTag == "DirectionalBlurEffect" || shortTag == "SaturationEffect")) {
        if (!xName.empty()) {
            state.namedElementTypes[std::string(xName)] = std::string(tagName);
            EnsureEnclosingElementHasNameIfEmpty(pkg, xName);
        }
        if (shortTag == "PixelateEffect") {
            float pxSize = 6.0f;
            std::string_view sizeDyn;
            for (const auto& a : attrs) {
                if (a.name == "Size") {
                    const std::string_view v = str::Trim(a.value);
                    if (!v.empty() && v.front() == '{') sizeDyn = v;
                    else pxSize = std::max(1.0f, compiler_detail::ParseFloat32(v, 6.0f));
                }
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(3)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(pxSize, -1.0f, 0.0f, 1.0f)});
            if (!sizeDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectBlurRadius, sizeDyn);
        } else if (shortTag == "BlurEffect") {
            float radius = 5.0f;
            std::string_view radDyn;
            for (const auto& a : attrs) {
                if (a.name == "Radius") {
                    const std::string_view v = str::Trim(a.value);
                    if (!v.empty() && v.front() == '{') radDyn = v;
                    else radius = std::max(0.0f, compiler_detail::ParseFloat32(v, 5.0f));
                }
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(1)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f)});
            if (!radDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectBlurRadius, radDyn);
        } else if (shortTag == "DropShadowEffect") {
            float blurRadius = 5.0f;
            float shadowDepth = 5.0f;
            float directionDeg = 315.0f;
            float opacity = 1.0f;
            Color shadowCol{0.0f, 0.0f, 0.0f, 1.0f};
            std::string_view brDyn, sdDyn, opDyn, colDyn;
            for (const auto& a : attrs) {
                const std::string_view v = str::Trim(a.value);
                if (v.empty()) continue;
                if (v.front() == '{') {
                    if (a.name == "BlurRadius") brDyn = v;
                    else if (a.name == "ShadowDepth") sdDyn = v;
                    else if (a.name == "Opacity") opDyn = v;
                    else if (a.name == "Color") colDyn = v;
                    continue;
                }
                if (a.name == "BlurRadius") blurRadius = std::max(0.0f, compiler_detail::ParseFloat32(v, 5.0f));
                else if (a.name == "ShadowDepth") shadowDepth = std::max(0.0f, compiler_detail::ParseFloat32(v, 5.0f));
                else if (a.name == "Direction") directionDeg = compiler_detail::ParseFloat32(v, 315.0f);
                else if (a.name == "Opacity") opacity = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 1.0f);
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
            if (!brDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectBlurRadius, brDyn);
            if (!sdDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectShadowDepth, sdDyn);
            if (!opDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectOpacity, opDyn);
            if (!colDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectColor, colDyn);
        } else if (shortTag == "TintEffect") {
            Color tintCol{1.0f, 1.0f, 1.0f, 1.0f};
            float opacity = 1.0f;
            std::string_view colDyn, opDyn;
            for (const auto& a : attrs) {
                const std::string_view v = str::Trim(a.value);
                if (v.empty()) continue;
                if (v.front() == '{') {
                    if (a.name == "Color") colDyn = v;
                    else if (a.name == "Opacity") opDyn = v;
                    continue;
                }
                if (a.name == "Color") tintCol = ParseXamlColorLiteral(v);
                else if (a.name == "Opacity") opacity = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 1.0f);
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(5)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(0.0f, -1.0f, 0.0f, opacity)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectColor, 0, 0, 0, 0,
                value::FromColor(tintCol)});
            if (!colDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectColor, colDyn);
            if (!opDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectOpacity, opDyn);
        } else if (shortTag == "ChromaticAberrationEffect") {
            float amountX = 0.0f;
            float opacity = 1.0f;
            std::string_view axDyn, opDyn;
            for (const auto& a : attrs) {
                const std::string_view v = str::Trim(a.value);
                if (v.empty()) continue;
                if (v.front() == '{') {
                    if (a.name == "AmountX" || a.name == "Amount") axDyn = v;
                    else if (a.name == "Opacity") opDyn = v;
                    continue;
                }
                if (a.name == "AmountX" || a.name == "Amount") amountX = compiler_detail::ParseFloat32(v, 0.0f);
                else if (a.name == "Opacity") opacity = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 1.0f);
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(6)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(0.0f, -1.0f, 0.0f, opacity)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectAmountX, 0, 0, 0, 0, MakeFloat(amountX)});
            if (!axDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectAmountX, axDyn);
            if (!opDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectOpacity, opDyn);
        } else if (shortTag == "DirectionalBlurEffect") {
            float radius = 0.0f;
            float angle = 0.0f;
            float opacity = 1.0f;
            std::string_view radDyn, angDyn, opDyn;
            for (const auto& a : attrs) {
                const std::string_view v = str::Trim(a.value);
                if (v.empty()) continue;
                if (v.front() == '{') {
                    if (a.name == "Radius") radDyn = v;
                    else if (a.name == "Angle") angDyn = v;
                    else if (a.name == "Opacity") opDyn = v;
                    continue;
                }
                if (a.name == "Radius") radius = std::max(0.0f, compiler_detail::ParseFloat32(v, 0.0f));
                else if (a.name == "Angle") angle = compiler_detail::ParseFloat32(v, 0.0f);
                else if (a.name == "Opacity") opacity = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 1.0f);
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(7)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(radius, -1.0f, angle, opacity)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectBlurRadius, 0, 0, 0, 0, MakeFloat(radius)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectAngle, 0, 0, 0, 0, MakeFloat(angle)});
            if (!radDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectBlurRadius, radDyn);
            if (!angDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectAngle, angDyn);
            if (!opDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectOpacity, opDyn);
        } else if (shortTag == "SaturationEffect") {
            float saturation = 1.0f;
            float opacity = 1.0f;
            std::string_view satDyn, opDyn;
            for (const auto& a : attrs) {
                const std::string_view v = str::Trim(a.value);
                if (v.empty()) continue;
                if (v.front() == '{') {
                    if (a.name == "Saturation") satDyn = v;
                    else if (a.name == "Opacity") opDyn = v;
                    continue;
                }
                if (a.name == "Saturation") saturation = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 4.0f);
                else if (a.name == "Opacity") opacity = std::clamp(compiler_detail::ParseFloat32(v, 1.0f), 0.0f, 1.0f);
            }
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectKind, 0, 0, 0, 0, value::FromInt(8)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_EffectParams, 0, 0, 0, 0,
                MakeFloat4InlineFast(0.0f, -1.0f, 0.0f, opacity)});
            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::SetStaticProperty, PropId_Saturation, 0, 0, 0, 0, MakeFloat(saturation)});
            if (!satDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_Saturation, satDyn);
            if (!opDyn.empty()) (void)EmitDynamicResourceOrBindingForPropHelper(ctx, PropId_EffectOpacity, opDyn);
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return;
    }

    // 3. Keyed ResourceDictionary items
    if (parentScope.kind == XmlScopeKind::ResourceContainer) {
        if (!xKey.empty()) {
            if (shortTag == "BlurEffect") {
                float radius = 5.0f;
                for (const auto& a : attrs) {
                    if (a.name == "Radius" && (a.value.empty() || a.value.front() != '{')) {
                        radius = std::max(0.0f, compiler_detail::ParseFloat32(a.value, 5.0f));
                    }
                }
                UpsertPackageResource(pkg, xKey, MakeFloat4InlineFast(radius, -1.0f, 0.0f, 1.0f));
            } else if (shortTag == "DropShadowEffect") {
                float blurR = 5.0f, depth = 5.0f, dir = 315.0f, op = 1.0f;
                for (const auto& a : attrs) {
                    if (!a.value.empty() && a.value.front() == '{') continue;
                    if (a.name == "BlurRadius") blurR = std::max(0.0f, compiler_detail::ParseFloat32(a.value, 5.0f));
                    else if (a.name == "ShadowDepth") depth = std::max(0.0f, compiler_detail::ParseFloat32(a.value, 5.0f));
                    else if (a.name == "Direction") dir = compiler_detail::ParseFloat32(a.value, 315.0f);
                    else if (a.name == "Opacity") op = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                }
                UpsertPackageResource(pkg, xKey, MakeFloat4InlineFast(blurR, depth, dir, op));
            } else if (shortTag == "TintEffect") {
                Color c{1.0f, 1.0f, 1.0f, 1.0f};
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        c = ParseXamlColorLiteral(a.value);
                    }
                }
                UpsertPackageResource(pkg, xKey, value::FromColor(c));
            } else if (shortTag == "ChromaticAberrationEffect") {
                float ax = 0.0f;
                for (const auto& a : attrs) {
                    if ((a.name == "AmountX" || a.name == "Amount") && (a.value.empty() || a.value.front() != '{')) {
                        ax = compiler_detail::ParseFloat32(a.value, 0.0f);
                    }
                }
                UpsertPackageResource(pkg, xKey, MakeFloat(ax));
            } else if (shortTag == "DirectionalBlurEffect") {
                float rad = 0.0f, ang = 0.0f, op = 1.0f;
                for (const auto& a : attrs) {
                    if (!a.value.empty() && a.value.front() == '{') continue;
                    if (a.name == "Radius") rad = std::max(0.0f, compiler_detail::ParseFloat32(a.value, 0.0f));
                    else if (a.name == "Angle") ang = compiler_detail::ParseFloat32(a.value, 0.0f);
                    else if (a.name == "Opacity") op = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                }
                UpsertPackageResource(pkg, xKey, MakeFloat4InlineFast(rad, -1.0f, ang, op));
            } else if (shortTag == "SaturationEffect") {
                float sat = 1.0f;
                for (const auto& a : attrs) {
                    if (a.name == "Saturation" && (a.value.empty() || a.value.front() != '{')) {
                        sat = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 4.0f);
                    }
                }
                UpsertPackageResource(pkg, xKey, MakeFloat(sat));
            } else if (shortTag == "SolidColorBrush") {
                bool assignedColor = false;
                float brushOpacity = 1.0f;
                bool hasOpacity = false;
                std::string_view colorAttr;
                for (const auto& a : attrs) {
                    if (a.name == "Color") {
                        colorAttr = str::Trim(a.value);
                    } else if (a.name == "Opacity") {
                        brushOpacity = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
                        hasOpacity = true;
                    }
                }
                if (!colorAttr.empty()) {
                    Color c{0.0f, 0.0f, 0.0f, 1.0f};
                    bool okColor = false;
                    if (colorAttr.front() == '{') {
                        const std::string_view refKey = ExtractMarkupResourceKey(colorAttr);
                        if (!refKey.empty()) {
                            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, refKey)) {
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
                    ++state.nonVisualDepth;
                    return;
                }
            } else if (shortTag == "LinearGradientBrush" ||
                       shortTag == "RadialGradientBrush" ||
                       shortTag == "ConicGradientBrush") {
                GradientBrushDesc gd = ParseGradientBrushHeaderHelper(tag);
                if (selfClosing) {
                    gd.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
                    gd.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
                    BakeGradientBrushRamp(gd);
                    const std::string spec = SerializeGradientBrushSpec(gd);
                    (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);
                    UpsertPackageResource(pkg, xKey, value::FromString(spec));
                    return;
                }
                XmlScopeEntry itemScope{};
                itemScope.kind = XmlScopeKind::KeyedResourceItem;
                itemScope.tagName = std::string(tagName);
                itemScope.resourceKey = std::string(xKey);
                itemScope.brushAssigned = true;
                scopeStack.push_back(std::move(itemScope));
                ++state.nonVisualDepth;

                ActiveGradientBrushScope gs{};
                gs.desc = std::move(gd);
                gs.owner = GradientScopeOwner::KeyedResource;
                gs.scopeDepth = scopeStack.size();
                gs.resourceKey = std::string(xKey);
                gradientStack.push_back(std::move(gs));
                return;
            } else if (shortTag == "Color") {
                bool hasChannelAttr = false;
                std::uint8_t ca = 255u, cr = 0u, cg = 0u, cb = 0u;
                for (const auto& a : attrs) {
                    if (a.name == "A") { ca = static_cast<std::uint8_t>(std::clamp(compiler_detail::ParseFloat32(a.value, 255.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                    else if (a.name == "R") { cr = static_cast<std::uint8_t>(std::clamp(compiler_detail::ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                    else if (a.name == "G") { cg = static_cast<std::uint8_t>(std::clamp(compiler_detail::ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
                    else if (a.name == "B") { cb = static_cast<std::uint8_t>(std::clamp(compiler_detail::ParseFloat32(a.value, 0.0f), 0.0f, 255.0f)); hasChannelAttr = true; }
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
                    ++state.nonVisualDepth;
                    return;
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
                        !str::Trim(a.value).empty()) {
                        const std::string_view rawU = str::Trim(a.value);
                        if (rawU.front() == '{') {
                            const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                            if (!refKey.empty()) {
                                if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, refKey)) {
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
                    ++state.nonVisualDepth;
                    return;
                }
            } else if (shortTag == "Geometry" || shortTag == "StreamGeometry" || shortTag == "PathGeometry") {
                bool handledAttr = false;
                for (const auto& a : attrs) {
                    if (a.name == "Figures" || a.name == "Data") {
                        const std::string decoded = DecodeXamlXmlEntities(a.value);
                        EnsurePathBakedIntoPackageHelper(ctx, decoded);
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
                    ++state.nonVisualDepth;
                    return;
                }
            } else if (shortTag == "BooleanToVisibilityConverter") {
                UpsertPackageResource(pkg, xKey, value::FromString("__aero_builtin_conv__:BooleanToVisibility"));
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "InverseBooleanToVisibilityConverter") {
                UpsertPackageResource(pkg, xKey, value::FromString("__aero_builtin_conv__:InverseBooleanToVisibility"));
                if (!selfClosing) (void)PushNonVisualScopeHelper(ctx, tagName);
                return;
            } else if (shortTag == "AlternationConverter" && !selfClosing) {
                std::size_t innerEnd = 0u, afterClose = 0u;
                if (FindMatchingClosingTag(src, ctx.pos, tagName, innerEnd, afterClose)) {
                    const std::string_view innerXml = src.substr(ctx.pos, innerEnd - ctx.pos);
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
                    ctx.pos = afterClose;
                    return;
                }
            } else if (shortTag == "GradientStopCollection" && !selfClosing) {
                std::size_t innerEnd = 0u, afterClose = 0u;
                if (FindMatchingClosingTag(src, ctx.pos, tagName, innerEnd, afterClose)) {
                    const std::string_view innerXml = src.substr(ctx.pos, innerEnd - ctx.pos);
                    std::string synthXml = "<StackPanel>";
                    synthXml.append(innerXml);
                    synthXml += "</StackPanel>";
                    const std::string resTplKey = "__aero_visual_res__:" + std::string(xKey);
                    auto subIn = std::make_unique<AeroCompileInput>();
                    subIn->xamlPath = resTplKey;
                    subIn->xamlContent = std::move(synthXml);
                    subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                    subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                    subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                    subIn->autoMergeGlobalResources = false;
                    auto subRes = CompileSubPackageHeap(
                        ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                    if (subRes->succeeded) {
                        auto dt = std::make_unique<AxbDataTemplate>();
                        dt->key = resTplKey;
                        dt->templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                        AxbTemplateRegistry::Instance().RegisterDataTemplate(*dt);
                        pkg.dataTemplates.push_back(std::move(*dt));
                        UpsertPackageResource(pkg, xKey, value::FromString(resTplKey));
                    }
                    ctx.pos = afterClose;
                    return;
                }
            } else if (!selfClosing &&
                       (shortTag == "Grid" || shortTag == "Border" || shortTag == "StackPanel" ||
                        shortTag == "TransformGroup" || shortTag == "ContentControl" ||
                        shortTag == "Canvas" || shortTag == "WrapPanel" || shortTag == "DockPanel")) {
                std::size_t innerEnd = 0u, afterClose = 0u;
                if (FindMatchingClosingTag(src, ctx.pos, tagName, innerEnd, afterClose)) {
                    const std::string_view fullElemXml = src.substr(tagStartLt, afterClose - tagStartLt);
                    const std::string resTplKey = "__aero_visual_res__:" + std::string(xKey);
                    FlushDeferredMtsdfJobsNoInline(ctx);
                    AxbTemplateRegistry::Instance().RegisterResources(pkg);
                    auto subIn = std::make_unique<AeroCompileInput>();
                    subIn->xamlPath = resTplKey;
                    subIn->xamlContent = std::string(fullElemXml);
                    subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                    subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                    subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                    subIn->autoMergeGlobalResources = false;
                    auto subRes = CompileSubPackageHeap(
                        ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                    if (subRes->succeeded) {
                        for (const auto& mp : subRes->axbPackage.mtsdfPaths) {
                            if (mp.pathHash != 0 && state.bakedPathHashes.insert(mp.pathHash).second) {
                                pkg.mtsdfPaths.push_back(mp);
                            }
                        }
                        auto dt = std::make_unique<AxbDataTemplate>();
                        dt->key = resTplKey;
                        dt->templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                        AxbTemplateRegistry::Instance().RegisterDataTemplate(*dt);
                        pkg.dataTemplates.push_back(std::move(*dt));
                        UpsertPackageResource(pkg, xKey, value::FromString(resTplKey));
                    }
                    ctx.pos = afterClose;
                    return;
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
                ++state.nonVisualDepth;
                return;
            } else if (tagName.find(':') != std::string_view::npos) {
                UpsertPackageResource(
                    pkg,
                    xKey,
                    value::FromString("__aero_script_conv__:" + std::string(shortTag)));
            }
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return;
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
                    !str::Trim(a.value).empty()) {
                    const std::string_view rawU = str::Trim(a.value);
                    if (rawU.front() == '{') {
                        const std::string_view refKey = ExtractMarkupResourceKey(rawU);
                        if (!refKey.empty()) {
                            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, refKey)) {
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
        bool insideItemsPanel = false;
        bool blockedByTemplateOrRes = false;
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::VisualElement) break;
            if (it->kind == XmlScopeKind::ResourceContainer ||
                it->kind == XmlScopeKind::KeyedResourceItem) {
                blockedByTemplateOrRes = true;
                break;
            }
            const std::string_view st = str::StripNamespacePrefix(it->tagName);
            if (st == "Style" || st == "ControlTemplate" || st == "DataTemplate") {
                blockedByTemplateOrRes = true;
                break;
            }
            if (str::EndsWith(st, ".ItemsPanel")) {
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
                    brushAlphaMul = std::clamp(compiler_detail::ParseFloat32(a.value, 1.0f), 0.0f, 1.0f);
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
                        if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, resKey)) {
                            resInst.literalValue = *rv;
                        }
                        pkg.instructions.push_back(std::move(resInst));
                        parentScope.brushAssigned = true;
                    } else if (EmitPropertyBindingIfValidHelper(ctx, parentScope.targetPropId, a.value)) {
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
                    }
                    break;
                }
            }
        } else if (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush") {
            GradientBrushDesc gd = ParseGradientBrushHeaderHelper(tag);
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
                return;
            }
            const std::uint16_t gradTargetPropId = parentScope.targetPropId;
            XmlScopeEntry gradScope{};
            gradScope.kind = XmlScopeKind::BrushPropertyElement;
            gradScope.tagName = std::string(tagName);
            gradScope.targetPropId = gradTargetPropId;
            gradScope.brushAssigned = true;
            scopeStack.push_back(std::move(gradScope));
            ++state.nonVisualDepth;

            ActiveGradientBrushScope gs{};
            gs.desc = std::move(gd);
            gs.owner = GradientScopeOwner::PropertyElement;
            gs.scopeDepth = scopeStack.size();
            gs.targetPropId = gradTargetPropId;
            gradientStack.push_back(std::move(gs));
            return;
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
                    !str::Trim(a.value).empty()) {
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
                ++state.nonVisualDepth;
                return;
            }
            const std::uint16_t destPropId = isImgBrush
                ? parentScope.targetPropId
                : ((shortTag == "BitmapImage")
                    ? static_cast<std::uint16_t>(PropId_ImageSource)
                    : parentScope.targetPropId);
            for (const auto& a : attrs) {
                if ((a.name == "UriSource" || a.name == "ImageSource" || a.name == "Source") &&
                    !str::Trim(a.value).empty()) {
                    const std::string_view rawUri = str::Trim(a.value);
                    std::string_view resK{};
                    std::uint8_t resF = 0u;
                    if (TryParseResourceMarkupExtension(rawUri, resK, resF)) {
                        const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, resK);
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
        ++state.nonVisualDepth;
    }
}

AERO_NOINLINE void HandleTransformChildNoInline(
    XamlCtx& ctx,
    XmlScopeEntry* tfScope,
    const OpenTagInfo& tag) {
    if (!tfScope) {
        return;
    }
    const auto shortTag = tag.shortTag;
    const auto& attrs = tag.attrs;

    auto resolveTransformResource = [&](std::string_view rawExpr, std::string_view attrName) -> const UnifiedValue* {
        const std::string_view v = str::Trim(rawExpr);
        if (v.size() <= 2u || v.front() != '{' || v.back() != '}') {
            return nullptr;
        }
        const std::string_view inner = str::Trim(v.substr(1u, v.size() - 2u));
        if (inner.rfind("StaticResource ", 0) == 0 || inner.rfind("DynamicResource ", 0) == 0) {
            std::string_view keyPart = (inner.rfind("StaticResource ", 0) == 0)
                ? str::Trim(inner.substr(15u))
                : str::Trim(inner.substr(16u));
            if (keyPart.rfind("ResourceKey=", 0) == 0) {
                keyPart = str::Trim(keyPart.substr(12u));
            }
            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, keyPart)) {
                return rv;
            }
            ctx.result.diagnostics.push_back(
                "AERO_XAML_TRANSFORM_RESOURCE: Unresolved resource '" +
                std::string(keyPart) + "' on <" + std::string(shortTag) + " " +
                std::string(attrName) + ">");
            return nullptr;
        }
        if (inner == "Binding" || inner == "Bind" ||
            inner.rfind("Binding ", 0) == 0 ||
            inner.rfind("Bind ", 0) == 0 ||
            inner.rfind("EcsBind ", 0) == 0) {
            ctx.result.diagnostics.push_back(
                "AERO_XAML_TRANSFORM_BINDING: Dynamic binding on child transform element '<" +
                std::string(shortTag) + " " + std::string(attrName) +
                ">' is not supported; bind RenderTransform or LayoutTransform directly on the UIElement.");
        }
        return nullptr;
    };

    auto evalFloatAttr = [&](const ParsedXmlAttr& a, float fallback, bool& outResolved) -> float {
        const std::string_view v = str::Trim(a.value);
        if (v.empty()) {
            outResolved = false;
            return fallback;
        }
        if (v.front() != '{') {
            outResolved = true;
            return compiler_detail::ParseFloat32(v, fallback);
        }
        if (const UnifiedValue* rv = resolveTransformResource(v, a.name)) {
            float fv = fallback;
            if (TryGetFloat(*rv, fv)) {
                outResolved = true;
                return fv;
            }
            if (const auto* sv = std::get_if<StringValue>(rv)) {
                outResolved = true;
                return compiler_detail::ParseFloat32(sv->view(), fallback);
            }
        }
        outResolved = false;
        return fallback;
    };

    Matrix2D step = Matrix2D::Identity();
    bool hasStep = false;
    bool allAttrsResolved = true;
    float cx = 0.0f;
    float cy = 0.0f;

    if (shortTag == "ScaleTransform") {
        float sx = 1.0f, sy = 1.0f;
        for (const auto& a : attrs) {
            bool ok = true;
            if (a.name == "ScaleX") sx = evalFloatAttr(a, 1.0f, ok);
            else if (a.name == "ScaleY") sy = evalFloatAttr(a, 1.0f, ok);
            else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
            allAttrsResolved &= ok;
        }
        step = Matrix2D::Scaling(sx, sy, cx, cy);
        hasStep = true;
        tfScope->hasAnyRtStep = true;
        if (tfScope->targetPropId == PropId_RenderTransform &&
            tfScope->canDecomposeStaticRt &&
            allAttrsResolved &&
            std::fabs(cx) <= 1e-5f && std::fabs(cy) <= 1e-5f &&
            tfScope->maxRtDecompStage <= 1) {
            tfScope->rtScaleX *= sx;
            tfScope->rtScaleY *= sy;
            tfScope->hasRtScale = true;
            tfScope->maxRtDecompStage = 1;
        } else {
            tfScope->canDecomposeStaticRt = false;
        }
    } else if (shortTag == "RotateTransform") {
        float angle = 0.0f;
        for (const auto& a : attrs) {
            bool ok = true;
            if (a.name == "Angle") angle = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
            allAttrsResolved &= ok;
        }
        step = Matrix2D::Rotation(angle, cx, cy);
        hasStep = true;
        tfScope->hasAnyRtStep = true;
        if (tfScope->targetPropId == PropId_RenderTransform &&
            tfScope->canDecomposeStaticRt &&
            allAttrsResolved &&
            std::fabs(cx) <= 1e-5f && std::fabs(cy) <= 1e-5f &&
            tfScope->maxRtDecompStage <= 3) {
            tfScope->rtRotateAngle += angle;
            tfScope->hasRtRotate = true;
            tfScope->maxRtDecompStage = 3;
        } else {
            tfScope->canDecomposeStaticRt = false;
        }
    } else if (shortTag == "TranslateTransform") {
        float dx = 0.0f, dy = 0.0f;
        for (const auto& a : attrs) {
            bool ok = true;
            if (a.name == "X") dx = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "Y") dy = evalFloatAttr(a, 0.0f, ok);
            allAttrsResolved &= ok;
        }
        step = Matrix2D::Translation(dx, dy);
        hasStep = true;
        tfScope->hasAnyRtStep = true;
        if (tfScope->targetPropId == PropId_RenderTransform &&
            tfScope->canDecomposeStaticRt &&
            allAttrsResolved &&
            tfScope->maxRtDecompStage <= 4) {
            tfScope->rtTranslateX += dx;
            tfScope->rtTranslateY += dy;
            tfScope->hasRtTranslate = true;
            tfScope->maxRtDecompStage = 4;
        } else {
            tfScope->canDecomposeStaticRt = false;
        }
    } else if (shortTag == "SkewTransform") {
        float ax = 0.0f, ay = 0.0f;
        for (const auto& a : attrs) {
            bool ok = true;
            if (a.name == "AngleX") ax = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "AngleY") ay = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "CenterX") cx = evalFloatAttr(a, 0.0f, ok);
            else if (a.name == "CenterY") cy = evalFloatAttr(a, 0.0f, ok);
            allAttrsResolved &= ok;
        }
        step = Matrix2D::Skew(ax, ay, cx, cy);
        hasStep = true;
        tfScope->hasAnyRtStep = true;
        if (tfScope->targetPropId == PropId_RenderTransform &&
            tfScope->canDecomposeStaticRt &&
            allAttrsResolved &&
            std::fabs(cx) <= 1e-5f && std::fabs(cy) <= 1e-5f &&
            !tfScope->hasRtSkew && tfScope->maxRtDecompStage < 2) {
            tfScope->rtSkewX = ax;
            tfScope->rtSkewY = ay;
            tfScope->hasRtSkew = true;
            tfScope->maxRtDecompStage = 2;
        } else {
            tfScope->canDecomposeStaticRt = false;
        }
    } else if (shortTag == "MatrixTransform" || shortTag == "Matrix") {
        bool parsedMatrixStr = false;
        float m11 = 1.0f, m12 = 0.0f, m21 = 0.0f, m22 = 1.0f, ox = 0.0f, oy = 0.0f;
        bool anyElemAttr = false;
        for (const auto& a : attrs) {
            if (a.name == "Matrix") {
                const std::string_view mv = str::Trim(a.value);
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
        tfScope->hasAnyRtStep = true;
        tfScope->canDecomposeStaticRt = false;
    }

    if (hasStep) {
        tfScope->accumulatedTransform = Matrix2D::Multiply(step, tfScope->accumulatedTransform);
    }
    if (!tag.selfClosing) {
        (void)PushNonVisualScopeHelper(ctx, tag.tagName, XmlScopeKind::TransformChildNode);
    }
}

AERO_NOINLINE int TryHandleStyleControlTemplateKeyBindingsNoInline(
    XamlCtx& ctx,
    XmlScopeEntry& parentScope,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& styleStack = state.styleStack;
    auto& setterStack = state.setterStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xKey = tag.xKey;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

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
                        : std::string(str::Trim(a.value));
                }
            }
            if (!selfClosing) {
                (void)PushNonVisualScopeHelper(ctx, tagName);
            }
            return 1;
        }

        auto styleDefPtr = std::make_unique<AxbStyleDefinition>();
        styleDefPtr->key = std::string(xKey);
        for (const auto& a : attrs) {
            if (a.name == "TargetType") {
                styleDefPtr->targetTypeName = ExtractTargetTypeShortName(a.value);
            } else if (a.name == "BasedOn") {
                const std::string_view bKey = ExtractMarkupResourceKey(a.value);
                styleDefPtr->basedOnKey = !bKey.empty()
                    ? std::string(bKey)
                    : std::string(str::Trim(a.value));
            }
        }
        if (selfClosing) {
            if (!styleDefPtr->key.empty() || !styleDefPtr->targetTypeName.empty()) {
                pkg.styles.push_back(std::move(*styleDefPtr));
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            auto scPtr = std::make_unique<ActiveStyleScope>();
            scPtr->def = std::move(*styleDefPtr);
            scPtr->scopeDepth = depth;
            styleStack.push_back(std::move(*scPtr));
        }
        return 1;
    }

    if (shortTag == "ControlTemplate.Triggers" || shortTag == "Interaction.Triggers") {
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            if (!styleStack.empty()) {
                if (shortTag == "Interaction.Triggers") {
                    styleStack.back().interactionTriggersDepth = depth;
                } else {
                    styleStack.back().controlTemplateTriggersDepth = depth;
                }
            }
        }
        return 1;
    }

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
            if (FindMatchingClosingTag(ctx.src, ctx.pos, "ControlTemplate", innerEnd, afterClose)) {
                const std::string_view innerXml = ctx.src.substr(ctx.pos, innerEnd - ctx.pos);
                const std::string outerScopeName = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                std::string ctKey;
                if (!xKey.empty()) {
                    ctKey = std::string(xKey);
                } else if (isInlineSetterCt || parentScope.isControlTemplateContainer) {
                    ctKey = "__inline_ct_" + outerScopeName + "_" +
                            std::to_string(state.nextInlineControlTplId++);
                } else {
                    ctKey = "__implicit_ct_" + outerScopeName + "_" +
                            (ctTargetType.empty() ? std::to_string(state.nextInlineControlTplId++) : ctTargetType);
                }
                FlushDeferredMtsdfJobsNoInline(ctx);
                AxbTemplateRegistry::Instance().RegisterResources(pkg);
                ScopedTargetTypeHintGuard templateTypeGuard(ctTargetType);
                auto subIn = std::make_unique<AeroCompileInput>();
                subIn->xamlPath = ctKey;
                subIn->xamlContent = std::string(innerXml);
                subIn->externalXamlResolver = ctx.input.externalXamlResolver;
                subIn->resourceSearchRoot = ctx.input.resourceSearchRoot;
                subIn->resourceSearchPaths = ctx.input.resourceSearchPaths;
                subIn->autoMergeGlobalResources = false;
                std::unique_ptr<AeroCompileResult> subRes =
                    CompileSubPackageHeap(ctx.compiler, std::move(subIn), ctx.activeIncludeStack);
                if (!subRes->succeeded) {
                    for (const auto& d : subRes->diagnostics) {
                        ctx.result.diagnostics.push_back(d);
                    }
                }
                if (subRes->succeeded) {
                    auto ctPtr = std::make_unique<AxbControlTemplate>();
                    auto& ct = *ctPtr;
                    ct.key = ctKey;
                    ct.targetTypeName = std::move(ctTargetType);

                    std::string rootInlineVsmKey;
                    std::size_t rootInlineStyleInstIdx = ~std::size_t{0};
                    if (!subRes->axbPackage.instructions.empty() &&
                        subRes->axbPackage.instructions[0].opcode == AxbOpcode::BeginElement) {
                        for (std::size_t idx = 1u; idx < subRes->axbPackage.instructions.size(); ++idx) {
                            const auto& inst = subRes->axbPackage.instructions[idx];
                            if (inst.opcode == AxbOpcode::BeginElement ||
                                inst.opcode == AxbOpcode::EndElement) {
                                break;
                            }
                            if (inst.opcode == AxbOpcode::SetStaticProperty &&
                                inst.propId == PropId_Style) {
                                if (const auto* sv = std::get_if<StringValue>(&inst.literalValue)) {
                                    if (sv->view().rfind("__inline_style_", 0) == 0) {
                                        rootInlineVsmKey = sv->str();
                                        rootInlineStyleInstIdx = idx;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    bool erasedRootInlineStyle = false;
                    for (auto stIt = subRes->axbPackage.styles.begin();
                         stIt != subRes->axbPackage.styles.end();) {
                        const bool isCtRootTrigs = (stIt->key.rfind("__ct_root_triggers_", 0) == 0);
                        const bool isRootVsm = (!rootInlineVsmKey.empty() && stIt->key == rootInlineVsmKey);
                        if (isCtRootTrigs || isRootVsm) {
                            if (isCtRootTrigs) {
                                for (auto& tr : stIt->triggers) {
                                    tr.overrideLocalValues = true;
                                    ct.triggers.push_back(std::move(tr));
                                }
                                for (auto& et : stIt->eventTriggers) {
                                    ct.eventTriggers.push_back(std::move(et));
                                }
                            }
                            for (auto& vsg : stIt->visualStateGroups) {
                                ct.visualStateGroups.push_back(std::move(vsg));
                            }
                            if (stIt->setters.empty() &&
                                stIt->triggers.empty() &&
                                stIt->eventTriggers.empty()) {
                                if (isRootVsm) {
                                    erasedRootInlineStyle = true;
                                }
                                stIt = subRes->axbPackage.styles.erase(stIt);
                                continue;
                            }
                        }
                        ++stIt;
                    }
                    if (erasedRootInlineStyle &&
                        rootInlineStyleInstIdx < subRes->axbPackage.instructions.size()) {
                        subRes->axbPackage.instructions.erase(
                            subRes->axbPackage.instructions.begin() +
                            static_cast<std::ptrdiff_t>(rootInlineStyleInstIdx));
                    }

                    for (const auto& mp : subRes->axbPackage.mtsdfPaths) {
                        if (mp.pathHash != 0 && state.bakedPathHashes.insert(mp.pathHash).second) {
                            pkg.mtsdfPaths.push_back(mp);
                        }
                    }

                    ct.templatePackage = std::make_shared<AxbPackage>(std::move(subRes->axbPackage));
                    if (!ct.targetTypeName.empty()) {
                        for (auto& inst : ct.templatePackage->instructions) {
                            if (inst.opcode == AxbOpcode::BindTemplateParent &&
                                (inst.strIdx0 == compiler_detail::kUnmappedPropId || inst.strIdx0 == PropId_Tag)) {
                                const std::uint16_t customId = FindCustomProperty(
                                    ct.targetTypeName,
                                    ct.templatePackage->GetString(inst.strIdx1));
                                if (customId != compiler_detail::kUnmappedPropId) {
                                    inst.strIdx0 = static_cast<std::uint32_t>(customId);
                                }
                            }
                        }
                    }
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
                ctx.pos = afterClose;
                return 1;
            }
        } else {
            return 1;
        }
    }

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
                if (FindMatchingClosingTag(ctx.src, ctx.pos, "ItemsPanelTemplate", innerEnd, afterClose)) {
                    bool isVirt = false;
                    const std::string spec = ParseItemsPanelInnerXmlToSpec(
                        ctx.src.substr(ctx.pos, innerEnd - ctx.pos), isVirt);
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
                    ctx.pos = afterClose;
                    return 1;
                }
            } else {
                return 1;
            }
        }
    }

    if (shortTag == "KeyBinding") {
        std::string kbKey, kbMods, kbCmd, kbParam;
        for (const auto& a : attrs) {
            if (a.name == "Key") kbKey = std::string(str::Trim(a.value));
            else if (a.name == "Modifiers") kbMods = std::string(str::Trim(a.value));
            else if (a.name == "Gesture") {
                const std::string_view g = str::Trim(a.value);
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    if (shortTag == "CommandBinding") {
        std::string cbCmd, cbExec, cbCanExec;
        for (const auto& a : attrs) {
            if (a.name == "Command") {
                const auto pb = ParseBindingMarkupExpression(a.value);
                cbCmd = pb.isValidBinding ? std::string(pb.pathPart) : DecodeXamlXmlEntities(a.value);
            } else if (a.name == "Executed") {
                cbExec = std::string(str::Trim(a.value));
            } else if (a.name == "CanExecute") {
                cbCanExec = std::string(str::Trim(a.value));
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    return 0;
}

AERO_NOINLINE int TryHandleTriggerAndConditionTagNoInline(
    XamlCtx& ctx,
    const OpenTagInfo& tag) {
    auto& state = ctx.st;
    auto& styleStack = state.styleStack;
    auto& triggerStack = state.triggerStack;
    auto& eventTriggerStack = state.eventTriggerStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    const std::string_view trigOwnerType = !styleStack.empty()
        ? std::string_view(styleStack.back().def.targetTypeName)
        : t_activeTemplateTargetTypeHint;

    if ((shortTag == "EventTrigger" ||
         shortTag == "StoryboardCompletedTrigger" ||
         shortTag == "PropertyChangedTrigger" ||
         shortTag == "KeyTrigger" ||
         shortTag == "TimerTrigger") && !styleStack.empty()) {
        auto etsPtr = std::make_unique<ActiveEventTriggerScope>();
        auto& et = etsPtr->et;
        bool rec = false;

        if (shortTag == "StoryboardCompletedTrigger") {
            et.eventKind = RoutedTriggerEvent::StoryboardCompleted;
            for (const auto& a : attrs) {
                if (a.name == "Storyboard") {
                    const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                    et.sourceName = !sbKey.empty()
                        ? std::string(sbKey)
                        : std::string(str::Trim(a.value));
                    rec = !et.sourceName.empty();
                }
            }
        } else if (shortTag == "PropertyChangedTrigger") {
            et.eventKind = RoutedTriggerEvent::PropertyChanged;
            for (const auto& a : attrs) {
                if (a.name == "Binding") {
                    const ParsedBindingClause pb = ParseBindingMarkupExpression(a.value);
                    if (pb.isValidBinding) {
                        std::string_view pctOwner = trigOwnerType;
                        if (!pb.elementName.empty()) {
                            if (const auto it = state.namedElementTypes.find(std::string(pb.elementName));
                                it != state.namedElementTypes.end()) {
                                pctOwner = it->second;
                            }
                        }
                        const std::uint16_t pid = ResolveMappedSetterPropertyId(pb.pathPart, pctOwner);
                        if (pid != compiler_detail::kUnmappedPropId) {
                            et.sourceName = std::string(pb.elementName) + "|" + std::to_string(pid);
                            rec = true;
                        }
                    }
                }
            }
        } else if (shortTag == "KeyTrigger") {
            et.eventKind = RoutedTriggerEvent::KeyDownTrigger;
            std::string_view rawKey;
            std::string_view rawMods;
            for (const auto& a : attrs) {
                if (a.name == "Key") {
                    rawKey = a.value;
                } else if (a.name == "Modifiers") {
                    rawMods = a.value;
                } else if (a.name == "ActiveOnFocus") {
                    const std::string_view tv = str::Trim(a.value);
                    et.activeOnFocus = str::EqualsIgnoreCase(tv, "true") || tv == "1";
                } else if (a.name == "FiredOn") {
                    et.firedOnKeyUp = str::EqualsIgnoreCase(str::Trim(a.value), "KeyUp");
                } else if (a.name == "SourceName") {
                    et.sourceName = std::string(str::Trim(a.value));
                }
            }
            et.keyCode = ParseXamlKeyCode(rawKey);
            et.modifiers = ParseXamlKeyModifiers(rawMods);
            rec = (et.keyCode != KeyCode::None);
        } else if (shortTag == "TimerTrigger") {
            et.eventKind = RoutedTriggerEvent::TimerTrigger;
            et.triggerSourceEvent = RoutedTriggerEvent::Loaded;
            et.totalTicks = -1;
            et.timerIntervalMs = 1000u;
            rec = true;
            for (const auto& a : attrs) {
                if (a.name == "RoutedEvent" || a.name == "EventName") {
                    bool evRec = false;
                    const RoutedTriggerEvent srcEv = ParseRoutedTriggerEvent(a.value, evRec);
                    if (evRec) {
                        et.triggerSourceEvent = srcEv;
                    }
                } else if (a.name == "TotalTicks") {
                    const std::string ttStr(str::Trim(a.value));
                    et.totalTicks = static_cast<std::int32_t>(std::strtoll(ttStr.c_str(), nullptr, 10));
                } else if (a.name == "SourceName") {
                    et.sourceName = std::string(str::Trim(a.value));
                } else if (a.name == "MillisecondsPerTick") {
                    const std::string_view trimMs = str::Trim(a.value);
                    AxbTriggerCondition timerCond{};
                    if (BuildDataTriggerConditionHelper(ctx, trimMs, "0", true, timerCond, trigOwnerType)) {
                        timerCond.expectedValue = value::FromString("__TimerInterval");
                        et.conditions.push_back(std::move(timerCond));
                    } else if (!trimMs.empty() && trimMs.front() == '{') {
                        const std::string_view rk = ExtractMarkupResourceKey(trimMs);
                        if (!rk.empty()) {
                            if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(ctx.pkg, rk)) {
                                float fv = 1000.0f;
                                if (TryGetFloat(*rv, fv)) {
                                    et.timerIntervalMs = static_cast<std::uint32_t>(
                                        std::max<long>(1L, std::lround(fv)));
                                }
                            }
                        }
                    } else {
                        const float msVal = std::max(1.0f, compiler_detail::ParseFloat32(trimMs, 1000.0f));
                        et.timerIntervalMs = static_cast<std::uint32_t>(std::lround(msVal));
                    }
                }
            }
        } else {
            const bool isInteractionEt =
                (styleStack.back().interactionTriggersDepth != 0u) ||
                (tagName.find(':') != std::string_view::npos);
            rec = isInteractionEt;
            for (const auto& a : attrs) {
                if (a.name == "RoutedEvent" || a.name == "EventName") {
                    et.eventKind = ParseRoutedTriggerEvent(a.value, rec);
                } else if (a.name == "SourceName") {
                    et.sourceName = std::string(str::Trim(a.value));
                }
            }
        }
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            etsPtr->scopeDepth = depth;
            etsPtr->recognized = rec;
            eventTriggerStack.push_back(std::move(*etsPtr));
        }
        return 1;
    }

    if ((shortTag == "Trigger" || shortTag == "MultiTrigger" ||
         shortTag == "DataTrigger" || shortTag == "MultiDataTrigger") &&
        !styleStack.empty()) {
        auto tsPtr = std::make_unique<ActiveTriggerScope>();
        auto& trig = tsPtr->trig;
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
            const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr, trigOwnerType);
            const std::string_view trimTrigVal = str::Trim(valAttr);
            const bool isNullTrigVal =
                trimTrigVal.size() > 2u && trimTrigVal.front() == '{' && trimTrigVal.back() == '}' &&
                str::Trim(trimTrigVal.substr(1u, trimTrigVal.size() - 2u)).rfind("x:Null", 0) == 0;
            if (condPropId == compiler_detail::kUnmappedPropId || !hasValAttr) {
                unsupportedCond = true;
            } else if (isNullTrigVal) {
                AxbTriggerCondition c{};
                c.propId = condPropId;
                c.expectNull = true;
                c.expectedValue = UnifiedValue{NullObject{}};
                trig.conditions.push_back(std::move(c));
            } else {
                bool ok = false;
                UnifiedValue expVal = ParseStyleOrAnimationValueHelper(ctx, condPropId, valAttr, ok);
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
            if (BuildDataTriggerConditionHelper(ctx, bindAttr, valAttr, hasValAttr, dtCond, trigOwnerType)) {
                if (!cmpAttr.empty()) {
                    dtCond.comparison = ParseTriggerComparisonOp(cmpAttr);
                }
                trig.conditions.push_back(std::move(dtCond));
            } else {
                unsupportedCond = true;
            }
        }

        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            tsPtr->scopeDepth = depth;
            tsPtr->isDataTrigger = isData;
            tsPtr->hasUnsupportedCondition = unsupportedCond;
            triggerStack.push_back(std::move(*tsPtr));
        }
        return 1;
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
                if (BuildDataTriggerConditionHelper(ctx, bindAttr, valAttr, hasValAttr, dtCond, trigOwnerType)) {
                    triggerStack.back().trig.conditions.push_back(std::move(dtCond));
                } else {
                    triggerStack.back().hasUnsupportedCondition = true;
                }
            } else {
                const std::uint16_t condPropId = ResolveMappedTriggerConditionId(propAttr, trigOwnerType);
                const std::string_view trimCondVal = str::Trim(valAttr);
                const bool isNullCondVal =
                    trimCondVal.size() > 2u && trimCondVal.front() == '{' && trimCondVal.back() == '}' &&
                    str::Trim(trimCondVal.substr(1u, trimCondVal.size() - 2u)).rfind("x:Null", 0) == 0;
                if (condPropId == compiler_detail::kUnmappedPropId || !hasValAttr) {
                    triggerStack.back().hasUnsupportedCondition = true;
                } else if (isNullCondVal) {
                    AxbTriggerCondition c{};
                    c.propId = condPropId;
                    c.expectNull = true;
                    c.expectedValue = UnifiedValue{NullObject{}};
                    triggerStack.back().trig.conditions.push_back(std::move(c));
                } else {
                    bool ok = false;
                    UnifiedValue expVal = ParseStyleOrAnimationValueHelper(ctx, condPropId, valAttr, ok);
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
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
            if (BuildDataTriggerConditionHelper(ctx, leftOp, rightOp, hasRightOp, extraCond, trigOwnerType)) {
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    return 0;
}

AERO_NOINLINE int TryHandleInteractionActionTagNoInline(
    XamlCtx& ctx,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& eventTriggerStack = state.eventTriggerStack;
    auto& triggerStack = state.triggerStack;
    auto& actionStack = state.actionStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xName = tag.xName;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    if (!actionStack.empty() && shortTag.find('.') != std::string_view::npos &&
        (str::EndsWith(shortTag, ".TargetObject") ||
         str::EndsWith(shortTag, ".Value") ||
         str::EndsWith(shortTag, ".Uri") ||
         str::EndsWith(shortTag, ".Path") ||
         str::EndsWith(shortTag, ".CommandParameter"))) {
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    const bool isActionTag =
        shortTag == "ChangePropertyAction" ||
        shortTag == "ControlStoryboardAction" ||
        shortTag == "InvokeCommandAction" ||
        shortTag == "RemoveElementAction" ||
        shortTag == "NavigateToAction" ||
        shortTag == "NavigateBackAction" ||
        shortTag == "PlaySoundAction" ||
        shortTag == "GoToStateAction" ||
        shortTag == "SetFocusAction" ||
        shortTag == "SelectAction" ||
        shortTag == "SelectAllAction" ||
        shortTag == "LaunchUriOrFileAction" ||
        shortTag == "LaunchUriAction" ||
        shortTag == "PlayMediaAction" ||
        shortTag == "StopMediaAction";

    if (!isActionTag || (eventTriggerStack.empty() && triggerStack.empty())) {
        return 0;
    }

    const bool preferEt = !eventTriggerStack.empty() &&
        (triggerStack.empty() || eventTriggerStack.back().scopeDepth > triggerStack.back().scopeDepth);
    const std::string_view styleOwnerHint = !state.styleStack.empty()
        ? std::string_view(state.styleStack.back().def.targetTypeName)
        : t_activeTemplateTargetTypeHint;

    auto iasPtr = std::make_unique<ActiveInteractionActionScope>();
    auto& act = iasPtr->action;
    act.actionName = std::string(xName);
    for (const auto& a : attrs) {
        if (a.name == "IsEnabled") {
            act.isEnabled = !str::EqualsIgnoreCase(str::Trim(a.value), "false") && str::Trim(a.value) != "0";
        }
    }

    auto applyTargetObjectAttr = [&](std::string_view rawTargetObj, std::string_view ownerHint) {
        const ParsedBindingClause tb = ParseBindingMarkupExpression(rawTargetObj);
        if (tb.isValidBinding) {
            act.targetObjectBinding = std::make_shared<AxbCompiledBindingChain>(
                BuildCompiledBindingChainHelper(pkg, tb, 0u, false, ownerHint));
        }
    };

    if (shortTag == "ChangePropertyAction") {
        act.kind = InteractionActionKind::ChangeProperty;
        std::string_view rawPropName;
        std::string_view rawVal;
        std::string_view rawTargetObj;
        bool hasValAttr = false;
        for (const auto& a : attrs) {
            if (a.name == "TargetName") {
                act.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "PropertyName") {
                act.propertyName = std::string(str::Trim(a.value));
                rawPropName = a.value;
            } else if (a.name == "Value") {
                rawVal = a.value;
                hasValAttr = true;
            } else if (a.name == "TargetObject") {
                rawTargetObj = a.value;
            }
        }
        std::string_view ownerHint = styleOwnerHint;
        if (!act.targetName.empty()) {
            if (const auto it = state.namedElementTypes.find(act.targetName);
                it != state.namedElementTypes.end()) {
                ownerHint = it->second;
            }
        }
        if (!rawPropName.empty()) {
            act.propId = ResolveMappedSetterPropertyId(rawPropName, ownerHint);
        }
        if (!rawTargetObj.empty()) {
            applyTargetObjectAttr(rawTargetObj, ownerHint);
        }
        if (hasValAttr) {
            const std::string_view trimRaw = str::Trim(rawVal);
            const std::string_view innerNull =
                (trimRaw.size() > 2u && trimRaw.front() == '{' && trimRaw.back() == '}')
                    ? str::Trim(trimRaw.substr(1u, trimRaw.size() - 2u))
                    : std::string_view{};
            const ParsedBindingClause valBind = ParseBindingMarkupExpression(trimRaw);
            if (!innerNull.empty() && innerNull.rfind("x:Null", 0) == 0) {
                act.isNullValue = true;
            } else if (valBind.isValidBinding) {
                act.hasValueBinding = true;
                act.valueBinding = std::make_shared<AxbCompiledBindingChain>(
                    BuildCompiledBindingChainHelper(pkg, valBind, act.propId, false, ownerHint));
            } else if (act.propId != compiler_detail::kUnmappedPropId) {
                bool ok = false;
                act.value = ParseStyleOrAnimationValueHelper(ctx, act.propId, rawVal, ok);
                if (!ok) {
                    act.value = value::FromString(DecodeXamlXmlEntities(rawVal));
                }
            } else if (str::EqualsIgnoreCase(act.propertyName, "IsEnabled")) {
                act.propId = PropId_IsEnabled;
                act.value = !str::EqualsIgnoreCase(str::Trim(rawVal), "false") && str::Trim(rawVal) != "0";
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
                    : std::string(str::Trim(a.value));
                if (!act.storyboardKey.empty()) {
                    if (const AxbStoryboard* foundSb = FindLocalOrGlobalStoryboardHelper(pkg, act.storyboardKey)) {
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
                    : str::Trim(a.value);
                act.commandPath = std::string(cmdPath);
                SplitPathIntoHopsHelper(cmdPath, act.commandHops);
            } else if (a.name == "CommandParameter") {
                const std::string_view cpTrim = str::Trim(a.value);
                const ParsedBindingClause cpBind = ParseBindingMarkupExpression(cpTrim);
                if (cpBind.isValidBinding) {
                    act.hasCommandParameterBinding = true;
                    act.commandParameterBinding = BuildCompiledBindingChainHelper(
                        pkg, cpBind, 0u, false, styleOwnerHint);
                } else {
                    act.hasCommandParameter = true;
                    const std::string_view resK = ExtractMarkupResourceKey(cpTrim);
                    if (!resK.empty()) {
                        if (const UnifiedValue* rv = FindLocalOrGlobalResourceHelper(pkg, resK)) {
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
                act.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "TargetObject") {
                applyTargetObjectAttr(a.value, styleOwnerHint);
            }
        }
    } else if (shortTag == "NavigateToAction") {
        act.kind = InteractionActionKind::InvokeCommand;
        for (const auto& a : attrs) {
            if (a.name == "DestinationId") {
                act.commandPath = "__NavigateTo:" + std::string(str::Trim(a.value));
            }
        }
    } else if (shortTag == "NavigateBackAction") {
        act.kind = InteractionActionKind::InvokeCommand;
        act.commandPath = "__NavigateBack";
    } else if (shortTag == "PlaySoundAction") {
        act.kind = InteractionActionKind::PlaySound;
        for (const auto& a : attrs) {
            if (a.name == "Source") {
                act.soundSource = DecodeXamlXmlEntities(str::Trim(a.value));
            } else if (a.name == "Volume") {
                act.volume = compiler_detail::ParseFloat32(a.value, 1.0f);
            }
        }
    } else if (shortTag == "GoToStateAction") {
        act.kind = InteractionActionKind::GoToState;
        act.volume = 1.0f;
        for (const auto& a : attrs) {
            if (a.name == "StateName") {
                act.storyboardKey = DecodeXamlXmlEntities(str::Trim(a.value));
            } else if (a.name == "UseTransitions") {
                const std::string_view ut = str::Trim(a.value);
                act.volume = (!str::EqualsIgnoreCase(ut, "false") && ut != "0") ? 1.0f : 0.0f;
            } else if (a.name == "TargetName") {
                act.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "TargetObject") {
                applyTargetObjectAttr(a.value, styleOwnerHint);
            }
        }
    } else if (shortTag == "LaunchUriOrFileAction" || shortTag == "LaunchUriAction") {
        act.kind = InteractionActionKind::LaunchUri;
        for (const auto& a : attrs) {
            if (a.name == "Path" || a.name == "Uri" || a.name == "Source") {
                const std::string_view trimU = str::Trim(a.value);
                const ParsedBindingClause ub = ParseBindingMarkupExpression(trimU);
                if (ub.isValidBinding) {
                    act.hasValueBinding = true;
                    act.valueBinding = std::make_shared<AxbCompiledBindingChain>(
                        BuildCompiledBindingChainHelper(pkg, ub, 0u, true, styleOwnerHint));
                } else {
                    const std::string decoded = DecodeXamlXmlEntities(trimU);
                    act.soundSource = decoded;
                    act.value = value::FromString(decoded);
                }
            }
        }
    } else {
        if (shortTag == "SetFocusAction") act.kind = InteractionActionKind::SetFocus;
        else if (shortTag == "SelectAction") act.kind = InteractionActionKind::Select;
        else if (shortTag == "SelectAllAction") act.kind = InteractionActionKind::SelectAll;
        else if (shortTag == "PlayMediaAction") act.kind = InteractionActionKind::PlayMedia;
        else if (shortTag == "StopMediaAction") act.kind = InteractionActionKind::StopMedia;

        for (const auto& a : attrs) {
            if (a.name == "TargetName") {
                act.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "TargetObject") {
                applyTargetObjectAttr(a.value, styleOwnerHint);
            }
        }
    }

    if (selfClosing) {
        CommitActionToActiveTriggerHelper(state, std::move(act), preferEt);
    } else {
        const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
        iasPtr->targetEventTrigger = preferEt;
        iasPtr->scopeDepth = depth;
        actionStack.push_back(std::move(*iasPtr));
    }
    return 1;
}

AERO_NOINLINE int TryHandleBindingAndMultiBindingTagNoInline(
    XamlCtx& ctx,
    XmlScopeEntry& parentScope,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& styleStack = state.styleStack;
    auto& actionStack = state.actionStack;
    auto& multiBindingStack = state.multiBindingStack;
    auto& bindingElemStack = state.bindingElemStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xKey = tag.xKey;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    if (!actionStack.empty() &&
        actionStack.back().action.kind == InteractionActionKind::ChangeProperty &&
        actionStack.back().action.propId != compiler_detail::kUnmappedPropId &&
        (shortTag == "SolidColorBrush" || shortTag == "GradientStop")) {
        for (const auto& a : attrs) {
            if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                actionStack.back().action.value = value::FromColor(ParseXamlColorLiteral(a.value));
                break;
            }
        }
    }

    if (shortTag == "MultiBinding" && (styleStack.empty() || !actionStack.empty())) {
        const bool isNested = !multiBindingStack.empty() &&
            (bindingElemStack.empty() || multiBindingStack.back().scopeDepth > bindingElemStack.back().scopeDepth);
        const bool insideAction = !isNested && !actionStack.empty() &&
            (styleStack.empty() || actionStack.back().scopeDepth > styleStack.back().scopeDepth);
        auto mbsPtr = std::make_unique<ActiveMultiBindingScope>();
        auto& mb = mbsPtr->mb;
        if (isNested) {
            mb.targetPropertyId = 0u;
        } else if (insideAction) {
            mb.targetPropertyId = (actionStack.back().action.propId != compiler_detail::kUnmappedPropId)
                ? actionStack.back().action.propId
                : 0u;
            mbsPtr->attachToActionValue = true;
        } else {
            mb.targetPropertyId = FindEnclosingPropertyTargetIdHelper(state.scopeStack);
        }
        for (const auto& a : attrs) {
            if (a.name == "Mode") {
                const std::string_view mStr = str::Trim(a.value);
                if (mStr == "TwoWay") mb.mode = BindingMode::TwoWay;
                else if (mStr == "OneTime") mb.mode = BindingMode::OneTime;
            } else if (a.name == "StringFormat") {
                mb.stringFormat = NormalizeXamlFormatOrExpr(a.value);
            } else if (a.name == "Converter") {
                const std::string_view convKey = ExtractMarkupResourceKey(a.value);
                const std::string_view effKey = !convKey.empty() ? convKey : str::Trim(a.value);
                mb.converter = ParseValueConverterKind(effKey);
                mb.converterKey = std::string(effKey);
            } else if (a.name == "ConverterParameter") {
                mb.converterParameter = NormalizeXamlFormatOrExpr(a.value);
            } else if (a.name == "FallbackValue" && mb.targetPropertyId != 0u && mb.targetPropertyId != compiler_detail::kUnmappedPropId) {
                mb.fallbackValue = AeroCompiler::ParseLiteralPropertyValue(mb.targetPropertyId, str::Trim(a.value));
            }
        }
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            mbsPtr->scopeDepth = depth;
            mbsPtr->isNestedInParentMulti = isNested;
            multiBindingStack.push_back(std::move(*mbsPtr));
        }
        return 1;
    }

    if (shortTag == "Binding" && (styleStack.empty() || !actionStack.empty())) {
        const bool insideMulti = !multiBindingStack.empty() &&
            (bindingElemStack.empty() || multiBindingStack.back().scopeDepth > bindingElemStack.back().scopeDepth);
        const bool insideAction = !insideMulti && !actionStack.empty() &&
            (styleStack.empty() || actionStack.back().scopeDepth > styleStack.back().scopeDepth);
        const std::string_view parentShort = str::StripNamespacePrefix(parentScope.tagName);
        const bool actTargetObj = insideAction && str::EndsWith(parentShort, ".TargetObject");
        const bool actCmdParam = insideAction && str::EndsWith(parentShort, ".CommandParameter");
        const bool actVal = insideAction && !actTargetObj && !actCmdParam;

        auto besPtr = std::make_unique<ActiveBindingElemScope>();
        auto& ch = besPtr->chain;
        if (insideMulti) {
            ch.targetPropertyId = 0u;
        } else if (insideAction) {
            ch.targetPropertyId = actVal ? actionStack.back().action.propId : 0u;
        } else {
            ch.targetPropertyId = FindEnclosingPropertyTargetIdHelper(state.scopeStack);
        }

        const std::string_view ownerHint = !styleStack.empty()
            ? std::string_view(styleStack.back().def.targetTypeName)
            : t_activeTemplateTargetTypeHint;
        std::string_view fbRaw;
        bool hasTnRaw = false;
        std::string_view tnRaw{};
        for (const auto& a : attrs) {
            if (a.name == "Path") {
                std::string_view pp = str::Trim(a.value);
                bool isAttachedParen = false;
                if (pp.size() > 2u && pp.front() == '(' && pp.back() == ')') {
                    pp = str::Trim(pp.substr(1u, pp.size() - 2u));
                    isAttachedParen = true;
                }
                ch.rawPath = std::string(pp);
                SplitPathIntoHopsHelper(pp, ch.hops, isAttachedParen);
            } else if (a.name == "Mode") {
                const std::string_view mStr = str::Trim(a.value);
                if (mStr == "TwoWay") ch.mode = BindingMode::TwoWay;
                else if (mStr == "OneTime") ch.mode = BindingMode::OneTime;
            } else if (a.name == "ElementName") {
                ch.elementName = std::string(str::Trim(a.value));
            } else if (a.name == "RelativeSource") {
                std::string_view rsExpr = str::Trim(a.value);
                if (rsExpr.size() > 2u && rsExpr.front() == '{' && rsExpr.back() == '}') {
                    rsExpr = str::Trim(rsExpr.substr(1u, rsExpr.size() - 2u));
                }
                if (rsExpr.find("TemplatedParent") != std::string_view::npos) {
                    ch.relativeAncestorType = "$TemplatedParent";
                    ch.relativeSelf = false;
                } else if (rsExpr.find("Self") != std::string_view::npos) {
                    ch.relativeSelf = true;
                } else if (const auto ancPos = rsExpr.find("AncestorType="); ancPos != std::string_view::npos) {
                    std::string_view ancVal = str::Trim(rsExpr.substr(ancPos + 13u));
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
                const std::string_view effKey = !convKey.empty() ? convKey : str::Trim(a.value);
                ch.converter = ParseValueConverterKind(effKey);
                ch.converterKey = std::string(effKey);
            } else if (a.name == "ConverterParameter") {
                ch.converterParameter = NormalizeXamlFormatOrExpr(a.value);
            } else if (a.name == "StringFormat") {
                ch.stringFormat = NormalizeXamlFormatOrExpr(a.value);
            } else if (a.name == "FallbackValue") {
                fbRaw = str::Trim(a.value);
            } else if (a.name == "TargetNullValue") {
                hasTnRaw = true;
                tnRaw = str::Trim(a.value);
            }
        }
        (void)ownerHint;
        if (!fbRaw.empty()) {
            if (ch.targetPropertyId != 0u && ch.targetPropertyId != compiler_detail::kUnmappedPropId) {
                ch.fallbackValue = AeroCompiler::ParseLiteralPropertyValue(ch.targetPropertyId, fbRaw);
            } else {
                ch.fallbackValue = value::FromString(DecodeXamlXmlEntities(fbRaw));
            }
        }
        if (hasTnRaw) {
            if (tnRaw.empty()) {
                ch.targetNullValue = value::FromString("");
            } else if (ch.targetPropertyId != 0u && ch.targetPropertyId != compiler_detail::kUnmappedPropId) {
                ch.targetNullValue = AeroCompiler::ParseLiteralPropertyValue(ch.targetPropertyId, tnRaw);
            } else {
                ch.targetNullValue = value::FromString(DecodeXamlXmlEntities(tnRaw));
            }
        }
        if (selfClosing) {
            ResolveConverterResourceHelper(pkg, ch.converter, ch.converterKey);
            if (insideMulti) {
                multiBindingStack.back().mb.sources.push_back(std::move(ch));
            } else if (insideAction) {
                auto& act = actionStack.back().action;
                if (actTargetObj) {
                    act.targetObjectBinding = std::make_shared<AxbCompiledBindingChain>(std::move(ch));
                } else if (actCmdParam) {
                    act.hasCommandParameterBinding = true;
                    act.commandParameterBinding = std::move(ch);
                } else {
                    act.hasValueBinding = true;
                    act.valueBinding = std::make_shared<AxbCompiledBindingChain>(std::move(ch));
                }
            } else {
                EmitSingleCompiledChainHelper(pkg, std::move(ch));
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            besPtr->scopeDepth = depth;
            besPtr->insideMultiBinding = insideMulti;
            besPtr->attachToActionTargetObject = actTargetObj;
            besPtr->attachToActionCmdParam = actCmdParam;
            besPtr->attachToActionValue = actVal;
            bindingElemStack.push_back(std::move(*besPtr));
        }
        return 1;
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    return 0;
}

AERO_NOINLINE int TryHandleVsmStoryboardAndSetterTagNoInline(
    XamlCtx& ctx,
    XmlScopeEntry& parentScope,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& styleStack = state.styleStack;
    auto& triggerStack = state.triggerStack;
    auto& eventTriggerStack = state.eventTriggerStack;
    auto& vsgStack = state.vsgStack;
    auto& visualStateStack = state.visualStateStack;
    auto& storyboardStack = state.storyboardStack;
    auto& timelineStack = state.timelineStack;
    auto& keyFrameStack = state.keyFrameStack;
    auto& setterStack = state.setterStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xName = tag.xName;
    const auto xKey = tag.xKey;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    if ((str::EndsWith(shortTag, ".EnterActions") || str::EndsWith(shortTag, ".ExitActions")) &&
        !triggerStack.empty()) {
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            if (str::EndsWith(shortTag, ".EnterActions")) {
                triggerStack.back().enterActionsDepth = depth;
            } else {
                triggerStack.back().exitActionsDepth = depth;
            }
        }
        return 1;
    }

    if (shortTag == "BeginStoryboard") {
        for (const auto& a : attrs) {
            if (a.name == "Storyboard") {
                const std::string_view sbKey = ExtractMarkupResourceKey(a.value);
                if (!sbKey.empty()) {
                    if (const AxbStoryboard* foundSb = FindLocalOrGlobalStoryboardHelper(pkg, sbKey)) {
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
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    if (shortTag == "VisualStateGroup" && !styleStack.empty()) {
        auto gsPtr = std::make_unique<ActiveVsgScope>();
        gsPtr->group.name = std::string(xName);
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            gsPtr->scopeDepth = depth;
            vsgStack.push_back(std::move(*gsPtr));
        }
        return 1;
    }

    if (shortTag == "VisualStateGroup.Transitions" && !vsgStack.empty()) {
        if (!selfClosing) {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            vsgStack.back().transitionsDepth = depth;
        }
        return 1;
    }

    if (shortTag == "VisualTransition" && !vsgStack.empty()) {
        std::string_view fromState;
        std::string_view toState;
        for (const auto& a : attrs) {
            if (a.name == "From") fromState = str::Trim(a.value);
            else if (a.name == "To") toState = str::Trim(a.value);
            else if (a.name == "GeneratedDuration") {
                const float dur = ParseXamlTimeSpanSeconds(a.value, 0.0f);
                if ((fromState.empty() && toState.empty()) ||
                    vsgStack.back().group.defaultTransitionDuration <= 0.0f) {
                    vsgStack.back().group.defaultTransitionDuration = dur;
                }
            }
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    if (shortTag == "VisualState" && !vsgStack.empty()) {
        auto vssPtr = std::make_unique<ActiveVisualStateScope>();
        vssPtr->state.name = std::string(xName);
        if (selfClosing) {
            if (!vssPtr->state.name.empty()) {
                vsgStack.back().group.states.push_back(std::move(vssPtr->state));
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            vssPtr->scopeDepth = depth;
            visualStateStack.push_back(std::move(*vssPtr));
        }
        return 1;
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

        auto sbsPtr = std::make_unique<ActiveStoryboardScope>();
        sbsPtr->sb.key = std::string(xKey);
        for (const auto& a : attrs) {
            if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                sbsPtr->defaultTargetName = std::string(str::Trim(a.value));
            } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                sbsPtr->defaultTargetProperty = std::string(str::Trim(a.value));
            }
        }

        if (selfClosing) {
            if (sbOwner == StoryboardOwnerKind::StandaloneResource && !sbsPtr->sb.key.empty()) {
                pkg.storyboards.push_back(std::move(sbsPtr->sb));
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            sbsPtr->owner = sbOwner;
            sbsPtr->scopeDepth = depth;
            storyboardStack.push_back(std::move(*sbsPtr));
        }
        return 1;
    }

    if (shortTag == "StaticResource" && !storyboardStack.empty()) {
        for (const auto& a : attrs) {
            if (a.name == "ResourceKey") {
                const std::string_view refKey = str::Trim(a.value);
                if (const AxbStoryboard* refSb = FindLocalOrGlobalStoryboardHelper(pkg, refKey)) {
                    for (const auto& refTl : refSb->timelines) {
                        storyboardStack.back().sb.timelines.push_back(refTl);
                    }
                }
            }
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    const CustomAnimationSpec* customSpec = IsAnimationTimelineTag(shortTag)
        ? nullptr
        : FindCustomAnimationType(shortTag);
    if ((IsAnimationTimelineTag(shortTag) || customSpec != nullptr) && !storyboardStack.empty()) {
        auto tlsPtr = std::make_unique<ActiveTimelineScope>();
        auto& tl = tlsPtr->tl;
        tl.targetName = storyboardStack.back().defaultTargetName;
        std::string rawTargetProp = storyboardStack.back().defaultTargetProperty;
        if (customSpec != nullptr) {
            tl.customAnim = customSpec->defaultParams;
        }
        std::string_view rawFrom;
        std::string_view rawTo;
        std::string_view rawBy;
        bool hasFromAttr = false;
        bool hasToAttr = false;
        bool hasByAttr = false;

        for (const auto& a : attrs) {
            if (a.name == "Storyboard.TargetName" || a.name == "TargetName") {
                tl.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "Storyboard.TargetProperty" || a.name == "TargetProperty") {
                rawTargetProp = std::string(str::Trim(a.value));
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
                tl.autoReverse = str::EqualsIgnoreCase(str::Trim(a.value), "true") || str::Trim(a.value) == "1";
            } else if (a.name == "RepeatBehavior") {
                tl.repeatCount = ParseRepeatCountAttr(a.value);
            } else if (a.name == "FillBehavior") {
                tl.fillBehavior = str::EqualsIgnoreCase(str::Trim(a.value), "Stop")
                    ? FillBehavior::Stop
                    : FillBehavior::HoldEnd;
            } else if (a.name == "SpeedRatio") {
                tl.speedRatio = std::max(0.01f, compiler_detail::ParseFloat32(a.value, 1.0f));
            } else if (a.name == "AccelerationRatio") {
                tl.accelerationRatio = std::clamp(compiler_detail::ParseFloat32(a.value, 0.0f), 0.0f, 1.0f);
            } else if (a.name == "DecelerationRatio") {
                tl.decelerationRatio = std::clamp(compiler_detail::ParseFloat32(a.value, 0.0f), 0.0f, 1.0f);
            } else if (customSpec != nullptr && customSpec->parseAttr != nullptr) {
                customSpec->parseAttr(a.name, a.value, tl.customAnim);
            }
        }

        std::string_view targetOwnerType;
        if (!tl.targetName.empty()) {
            if (const auto it = state.namedElementTypes.find(tl.targetName);
                it != state.namedElementTypes.end()) {
                targetOwnerType = it->second;
            }
        }
        if (targetOwnerType.empty()) {
            targetOwnerType = !styleStack.empty()
                ? std::string_view(styleStack.back().def.targetTypeName)
                : t_activeTemplateTargetTypeHint;
        }
        tl.targetPropertyId = ResolveMappedAnimationPropertyId(rawTargetProp, targetOwnerType);
        if (tl.targetPropertyId != compiler_detail::kUnmappedPropId) {
            if (hasFromAttr) {
                bool ok = false;
                tl.fromValue = ParseStyleOrAnimationValueHelper(ctx, tl.targetPropertyId, rawFrom, ok);
                tl.hasFrom = ok;
            }
            if (hasToAttr) {
                bool ok = false;
                tl.toValue = ParseStyleOrAnimationValueHelper(ctx, tl.targetPropertyId, rawTo, ok);
                tl.hasTo = ok;
                tl.isBy = false;
            } else if (hasByAttr) {
                bool ok = false;
                tl.toValue = ParseStyleOrAnimationValueHelper(ctx, tl.targetPropertyId, rawBy, ok);
                tl.hasTo = ok;
                tl.isBy = ok;
            }
        }

        if (selfClosing) {
            if (tl.targetPropertyId != compiler_detail::kUnmappedPropId) {
                storyboardStack.back().sb.timelines.push_back(std::move(tl));
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            tlsPtr->scopeDepth = depth;
            timelineStack.push_back(std::move(*tlsPtr));
        }
        return 1;
    }

    if (IsAnimationKeyFrameTag(shortTag) && !timelineStack.empty()) {
        auto kfsPtr = std::make_unique<ActiveKeyFrameScope>();
        auto& kf = kfsPtr->kf;
        kf.isDiscrete =
            (shortTag.rfind("Discrete", 0) == 0) ||
            str::EndsWith(shortTag, "BooleanKeyFrame") ||
            str::EndsWith(shortTag, "ObjectKeyFrame");
        if (shortTag.rfind("Spline", 0) == 0) {
            kf.easingKind = EasingFunctionKind::Cubic;
            kf.easingMode = EasingMode::EaseInOut;
        }
        const std::uint16_t targetPropId = timelineStack.back().tl.targetPropertyId;
        for (const auto& a : attrs) {
            if (a.name == "KeyTime") {
                kf.timeSeconds = ParseXamlTimeSpanSeconds(a.value, 0.0f);
            } else if (a.name == "KeySpline") {
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
            } else if (a.name == "Value" && targetPropId != compiler_detail::kUnmappedPropId) {
                const std::string_view trimVal = str::Trim(a.value);
                const ParsedBindingClause bm = ParseBindingMarkupExpression(trimVal);
                if (bm.isValidBinding && !bm.elementName.empty() && !bm.pathPart.empty() && bm.pathPart != ".") {
                    kf.bindingElementName = std::string(bm.elementName);
                    std::string_view bindOwner;
                    if (const auto it = state.namedElementTypes.find(kf.bindingElementName);
                        it != state.namedElementTypes.end()) {
                        bindOwner = it->second;
                    }
                    const std::uint16_t bp = ResolveMappedSetterPropertyId(bm.pathPart, bindOwner);
                    kf.bindingPropId = (bp != compiler_detail::kUnmappedPropId) ? bp : 0u;
                } else {
                    bool ok = false;
                    UnifiedValue parsed = ParseStyleOrAnimationValueHelper(ctx, targetPropId, a.value, ok);
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
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            kfsPtr->scopeDepth = depth;
            keyFrameStack.push_back(std::move(*kfsPtr));
        }
        return 1;
    }

    if (IsEasingFunctionTag(shortTag)) {
        EasingFunctionKind ek = ParseEasingFunctionKind(shortTag);
        float easeExponent = 2.0f;
        bool hasEaseExponent = false;
        if (shortTag == "PowerEase") {
            ek = EasingFunctionKind::Quadratic;
            for (const auto& a : attrs) {
                if (a.name == "Power") {
                    const int p = static_cast<int>(std::lround(compiler_detail::ParseFloat32(a.value, 2.0f)));
                    if (p <= 1) ek = EasingFunctionKind::Linear;
                    else if (p == 2) ek = EasingFunctionKind::Quadratic;
                    else if (p == 3) ek = EasingFunctionKind::Cubic;
                    else if (p == 4) ek = EasingFunctionKind::Quartic;
                    else ek = EasingFunctionKind::Quintic;
                }
            }
        } else if (shortTag == "ExponentialEase") {
            for (const auto& a : attrs) {
                if (a.name == "Exponent") {
                    easeExponent = compiler_detail::ParseFloat32(a.value, 2.0f);
                    hasEaseExponent = true;
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
            if (ek == EasingFunctionKind::Exponential && hasEaseExponent) {
                timelineStack.back().tl.customAnim.params[0] = easeExponent;
                timelineStack.back().tl.customAnim.flags |= 1u;
            }
        } else if (!vsgStack.empty()) {
            vsgStack.back().group.defaultEasingKind = ek;
            vsgStack.back().group.defaultEasingMode = em;
        }
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    if (shortTag == "Setter" &&
        (!visualStateStack.empty() || !triggerStack.empty() || !styleStack.empty())) {
        SetterOwnerKind owner = SetterOwnerKind::Style;
        if (!visualStateStack.empty()) {
            owner = SetterOwnerKind::VisualState;
        } else if (!triggerStack.empty()) {
            owner = SetterOwnerKind::Trigger;
        }

        auto ssPtr = std::make_unique<ActiveSetterScope>();
        auto& setter = ssPtr->setter;
        setter.propId = compiler_detail::kUnmappedPropId;
        std::string_view rawPropAttr;
        std::string_view rawVal;
        bool hasValAttr = false;
        for (const auto& a : attrs) {
            if (a.name == "Property") {
                rawPropAttr = a.value;
            } else if (a.name == "TargetName") {
                setter.targetName = std::string(str::Trim(a.value));
            } else if (a.name == "Value") {
                rawVal = a.value;
                hasValAttr = true;
            }
        }

        std::string_view setterOwnerHint = !styleStack.empty()
            ? std::string_view(styleStack.back().def.targetTypeName)
            : t_activeTemplateTargetTypeHint;
        if (!setter.targetName.empty()) {
            if (const auto it = state.namedElementTypes.find(setter.targetName);
                it != state.namedElementTypes.end()) {
                setterOwnerHint = it->second;
            }
        }
        if (!rawPropAttr.empty()) {
            const std::string_view pTrim = str::Trim(rawPropAttr);
            if (pTrim == "Color" && !styleStack.empty() &&
                str::StripNamespacePrefix(styleStack.back().def.targetTypeName) == "BoxShadow") {
                setter.propId = PropId_EffectColor;
            } else {
                setter.propId = ResolveMappedSetterPropertyId(rawPropAttr, setterOwnerHint);
            }
        }

        bool hasValidVal = false;
        if (setter.propId != compiler_detail::kUnmappedPropId && hasValAttr) {
            const ParsedBindingClause setterBind = ParseBindingMarkupExpression(rawVal);
            if (setterBind.isValidBinding && owner == SetterOwnerKind::Style) {
                setter.hasBinding = true;
                setter.binding = std::make_shared<AxbCompiledBindingChain>(
                    BuildCompiledBindingChainHelper(pkg, setterBind, setter.propId, false, setterOwnerHint));
                hasValidVal = true;
            } else {
                const std::string_view trimSetVal = str::Trim(rawVal);
                if (setter.propId == PropId_IsChecked &&
                    trimSetVal.size() > 2u && trimSetVal.front() == '{' && trimSetVal.back() == '}' &&
                    str::Trim(trimSetVal.substr(1u, trimSetVal.size() - 2u)).rfind("x:Null", 0) == 0) {
                    setter.value = UnifiedValue{NullObject{}};
                    hasValidVal = true;
                } else {
                    setter.value = ParseStyleOrAnimationValueHelper(ctx, setter.propId, rawVal, hasValidVal);
                }
            }
        }

        if (selfClosing) {
            if (hasValidVal) {
                CommitSetterToOwnerHelper(ctx, std::move(setter), owner);
            }
        } else {
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            ssPtr->owner = owner;
            ssPtr->hasValue = hasValidVal;
            ssPtr->scopeDepth = depth;
            setterStack.push_back(std::move(*ssPtr));
        }
        return 1;
    }

    return 0;
}

AERO_NOINLINE int HandleTopLevelAndInlinePropertyElementsNoInline(
    XamlCtx& ctx,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    auto& styleStack = state.styleStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    // Top-level ResourceDictionary root file
    if (!ctx.seenRoot && shortTag == "ResourceDictionary") {
        ctx.seenRoot = true;
        pkg.rootTypeName = "ResourceDictionary";
        for (const auto& a : attrs) {
            if (a.name == "Source" && !str::Trim(a.value).empty()) {
                CompileAndMergeExternalDictionaryNoInline(ctx, DecodeXamlXmlEntities(a.value));
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
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Inline <ControlTemplate.Triggers> at top level of a ControlTemplate sub-package!
    if (shortTag == "ControlTemplate.Triggers" && scopeStack.empty()) {
        if (!selfClosing) {
            if (state.ctRootTriggersStyleKey.empty()) {
                const std::string scopePrefix = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                state.ctRootTriggersStyleKey = "__ct_root_triggers_" +
                    scopePrefix + "_" +
                    std::to_string(state.nextInlineVsmStyleId++);
            }
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            ActiveStyleScope ctTrigStyle{};
            ctTrigStyle.def.key = state.ctRootTriggersStyleKey;
            ctTrigStyle.def.targetTypeName = !t_activeTemplateTargetTypeHint.empty()
                ? std::string(str::StripNamespacePrefix(t_activeTemplateTargetTypeHint))
                : (!pkg.rootTypeName.empty()
                    ? std::string(str::StripNamespacePrefix(pkg.rootTypeName))
                    : std::string("UIElement"));
            ctTrigStyle.controlTemplateTriggersDepth = depth;
            ctTrigStyle.scopeDepth = depth;
            styleStack.push_back(std::move(ctTrigStyle));
        }
        return 1;
    }

    // Inline <DataTemplate.Triggers> at top level of a DataTemplate sub-package!
    if ((shortTag == "DataTemplate.Triggers" || shortTag == "HierarchicalDataTemplate.Triggers") &&
        scopeStack.empty()) {
        if (!selfClosing) {
            if (state.dtRootTriggersStyleKey.empty()) {
                const std::string scopePrefix = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                state.dtRootTriggersStyleKey = "__dt_root_triggers_" +
                    scopePrefix + "_" +
                    std::to_string(state.nextInlineVsmStyleId++);
            }
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            ActiveStyleScope dtTrigStyle{};
            dtTrigStyle.def.key = state.dtRootTriggersStyleKey;
            dtTrigStyle.def.targetTypeName = !pkg.rootTypeName.empty()
                ? std::string(str::StripNamespacePrefix(pkg.rootTypeName))
                : std::string("UIElement");
            dtTrigStyle.controlTemplateTriggersDepth = depth;
            dtTrigStyle.scopeDepth = depth;
            styleStack.push_back(std::move(dtTrigStyle));
        }
        return 1;
    }

    // Element-level <VisualStateManager.VisualStateGroups>, <Element.Style>, or <Element.Triggers>!
    if ((shortTag == "VisualStateManager.VisualStateGroups" ||
         str::EndsWith(shortTag, ".Style") ||
         str::EndsWith(shortTag, ".Triggers")) &&
        !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry& ownerElemScope = scopeStack.back();
            if (ownerElemScope.inlineStyleKey.empty()) {
                const std::string scopePrefix = !pkg.className.empty()
                    ? pkg.className
                    : (!ctx.input.xamlPath.empty() ? ctx.input.xamlPath : pkg.rootTypeName);
                ownerElemScope.inlineStyleKey = "__inline_style_" +
                    scopePrefix + "_" +
                    std::to_string(state.nextInlineVsmStyleId++);
                AxbInstruction styleRefInst{};
                styleRefInst.opcode = AxbOpcode::SetStaticProperty;
                styleRefInst.propId = PropId_Style;
                styleRefInst.literalValue = value::FromString(ownerElemScope.inlineStyleKey);
                pkg.instructions.push_back(std::move(styleRefInst));
            }
            const std::string synthKey = ownerElemScope.inlineStyleKey;
            const std::string ownerType = std::string(str::StripNamespacePrefix(ownerElemScope.tagName));
            const std::string ownerBasedOnKey = ownerElemScope.resourceKey;
            const std::size_t depth = PushNonVisualScopeHelper(ctx, tagName);
            ActiveStyleScope synthStyle{};
            synthStyle.def.key = synthKey;
            synthStyle.def.targetTypeName = ownerType;
            synthStyle.def.basedOnKey = ownerBasedOnKey;
            synthStyle.scopeDepth = depth;
            synthStyle.isInlinePropertyElement = str::EndsWith(shortTag, ".Style");
            if (shortTag == "Interaction.Triggers") {
                synthStyle.interactionTriggersDepth = depth;
            }
            styleStack.push_back(std::move(synthStyle));
        }
        return 1;
    }

    // Inline <*.ItemTemplate>, <*.ContentTemplate>, or <*.HeaderTemplate>
    if ((str::EndsWith(shortTag, ".ItemTemplate") ||
         str::EndsWith(shortTag, ".ContentTemplate") ||
         str::EndsWith(shortTag, ".HeaderTemplate")) &&
        !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry itScope{};
            itScope.kind = XmlScopeKind::NonVisualSkip;
            itScope.tagName = std::string(tagName);
            itScope.isItemTemplateContainer = true;
            itScope.targetPropId = str::EndsWith(shortTag, ".ContentTemplate")
                ? static_cast<std::uint16_t>(PropId_ContentTemplate)
                : (str::EndsWith(shortTag, ".HeaderTemplate")
                       ? static_cast<std::uint16_t>(PropId_HeaderTemplate)
                       : static_cast<std::uint16_t>(PropId_ItemTemplate));
            scopeStack.push_back(std::move(itScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Inline <ItemsControl.ItemsPanel><ItemsPanelTemplate>...</ItemsPanelTemplate></ItemsControl.ItemsPanel>
    if (str::EndsWith(shortTag, ".ItemsPanel") && !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry ipScope{};
            ipScope.kind = XmlScopeKind::NonVisualSkip;
            ipScope.tagName = std::string(tagName);
            ipScope.isItemsPanelContainer = true;
            scopeStack.push_back(std::move(ipScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Inline <ListView.View><GridView>...</GridView></ListView.View>
    if (str::EndsWith(shortTag, ".View") && !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry vwScope{};
            vwScope.kind = XmlScopeKind::NonVisualSkip;
            vwScope.tagName = std::string(tagName);
            vwScope.isListViewViewContainer = true;
            scopeStack.push_back(std::move(vwScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Inline <Element.ContextMenu><ContextMenu>...</ContextMenu></Element.ContextMenu>
    if (str::EndsWith(shortTag, ".ContextMenu") && !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry cmScope{};
            cmScope.kind = XmlScopeKind::NonVisualSkip;
            cmScope.tagName = std::string(tagName);
            cmScope.isContextMenuContainer = true;
            scopeStack.push_back(std::move(cmScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Inline <Control.Template><ControlTemplate>...</ControlTemplate></Control.Template>
    if (str::EndsWith(shortTag, ".Template") && !scopeStack.empty()) {
        if (!selfClosing) {
            XmlScopeEntry ctScope{};
            ctScope.kind = XmlScopeKind::NonVisualSkip;
            ctScope.tagName = std::string(tagName);
            ctScope.isControlTemplateContainer = true;
            scopeStack.push_back(std::move(ctScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    // Property-element tags (<Type.Property>)
    if (shortTag == "Grid.RowDefinitions" || shortTag == "Grid.ColumnDefinitions" ||
        str::EndsWith(shortTag, ".Header") ||
        str::EndsWith(shortTag, ".Content") ||
        str::EndsWith(shortTag, ".ToolTip") ||
        str::EndsWith(shortTag, ".Icon") ||
        str::EndsWith(shortTag, ".Data")) {
        if (!selfClosing) {
            XmlScopeEntry trans{};
            trans.kind = XmlScopeKind::TransparentContainer;
            trans.tagName = std::string(tagName);
            if (str::EndsWith(shortTag, ".Header")) {
                trans.targetPropId = PropId_Header;
            } else if (str::EndsWith(shortTag, ".Content")) {
                trans.targetPropId = PropId_Text;
            } else if (str::EndsWith(shortTag, ".ToolTip")) {
                trans.targetPropId = PropId_ToolTipText;
            } else if (str::EndsWith(shortTag, ".Data")) {
                trans.targetPropId = PropId_PathData;
            } else {
                trans.targetPropId = 0;
            }
            scopeStack.push_back(std::move(trans));
        }
        return 1;
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
        return 1;
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
        return 1;
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
                const std::string_view fw = str::Trim(a.value);
                if (str::EqualsIgnoreCase(fw, "Bold") || str::EqualsIgnoreCase(fw, "SemiBold") ||
                    str::EqualsIgnoreCase(fw, "DemiBold") || str::EqualsIgnoreCase(fw, "ExtraBold") ||
                    str::EqualsIgnoreCase(fw, "Black") || str::EqualsIgnoreCase(fw, "Heavy")) {
                    attrBold = true;
                }
            } else if (a.name == "FontStyle") {
                const std::string_view fs = str::Trim(a.value);
                if (str::EqualsIgnoreCase(fs, "Italic") || str::EqualsIgnoreCase(fs, "Oblique")) {
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

                    AxbCompiledBindingChain rChain = BuildCompiledBindingChainHelper(
                        pkg, parsedRunBind, PropId_Text, false, it->tagName);
                    if (!parsedRunBind.fallbackValue.empty()) {
                        it->accumulatedText += DecodeXamlXmlEntities(parsedRunBind.fallbackValue);
                    }
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
            return 1;
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
        return 1;
    }

    if (shortTag == "ResourceDictionary" ||
        shortTag == "ResourceDictionary.MergedDictionaries" ||
        str::EndsWith(shortTag, ".MergedDictionaries") ||
        str::EndsWith(shortTag, ".Resources")) {
        if (shortTag == "ResourceDictionary") {
            for (const auto& a : attrs) {
                if (a.name == "Source" && !str::Trim(a.value).empty()) {
                    CompileAndMergeExternalDictionaryNoInline(ctx, DecodeXamlXmlEntities(a.value));
                    break;
                }
            }
        }
        if (!selfClosing) {
            XmlScopeEntry resScope{};
            resScope.kind = XmlScopeKind::ResourceContainer;
            resScope.tagName = std::string(tagName);
            scopeStack.push_back(std::move(resScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (str::EndsWith(shortTag, ".Transform3D") || str::EndsWith(shortTag, ".Projection")) {
        if (!selfClosing) {
            XmlScopeEntry tf3Scope{};
            tf3Scope.kind = XmlScopeKind::Transform3DPropertyElement;
            tf3Scope.tagName = std::string(tagName);
            scopeStack.push_back(std::move(tf3Scope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (str::EndsWith(shortTag, ".Effect")) {
        if (!selfClosing) {
            XmlScopeEntry fxScope{};
            fxScope.kind = XmlScopeKind::EffectPropertyElement;
            fxScope.tagName = std::string(tagName);
            scopeStack.push_back(std::move(fxScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (str::EndsWith(shortTag, ".RenderTransform") || str::EndsWith(shortTag, ".LayoutTransform")) {
        if (!selfClosing) {
            XmlScopeEntry tfScope{};
            tfScope.kind = XmlScopeKind::TransformPropertyElement;
            tfScope.tagName = std::string(tagName);
            tfScope.targetPropId = str::EndsWith(shortTag, ".LayoutTransform")
                ? PropId_LayoutTransform
                : PropId_RenderTransform;
            tfScope.accumulatedTransform = Matrix2D::Identity();
            scopeStack.push_back(std::move(tfScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (str::EndsWith(shortTag, ".Background") ||
        str::EndsWith(shortTag, ".Fill") ||
        str::EndsWith(shortTag, ".Foreground") ||
        str::EndsWith(shortTag, ".BorderBrush") ||
        str::EndsWith(shortTag, ".Stroke") ||
        str::EndsWith(shortTag, ".Source")) {
        if (!selfClosing) {
            XmlScopeEntry brushScope{};
            brushScope.kind = XmlScopeKind::BrushPropertyElement;
            brushScope.tagName = std::string(tagName);
            if (str::EndsWith(shortTag, ".Foreground")) {
                brushScope.targetPropId = PropId_Foreground;
            } else if (str::EndsWith(shortTag, ".BorderBrush") ||
                       str::EndsWith(shortTag, ".Stroke")) {
                brushScope.targetPropId = PropId_BorderBrush;
            } else if (str::EndsWith(shortTag, ".Source")) {
                brushScope.targetPropId = PropId_ImageSource;
            } else {
                brushScope.targetPropId = PropId_Background;
            }
            scopeStack.push_back(std::move(brushScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (shortTag.find('.') != std::string_view::npos) {
        if (!selfClosing) {
            std::string_view ownerHint;
            if (const std::size_t dot = shortTag.rfind('.'); dot != std::string_view::npos) {
                ownerHint = shortTag.substr(0u, dot);
            }
            const std::uint16_t mappedProp = ResolveMappedSetterPropertyId(shortTag, ownerHint);
            XmlScopeEntry propScope{};
            propScope.kind = (mappedProp != compiler_detail::kUnmappedPropId)
                ? XmlScopeKind::BindingPropertyElement
                : XmlScopeKind::NonVisualSkip;
            propScope.tagName = std::string(tagName);
            propScope.targetPropId = mappedProp;
            scopeStack.push_back(std::move(propScope));
            ++state.nonVisualDepth;
        }
        return 1;
    }

    if (shortTag == "MouseDragElementBehavior") {
        EmitMouseDragElementBehaviorHelper(ctx, tag);
        return 1;
    }

    if (IsNonVisualElementTagHelper(ctx.isCompilingVisualResourceMock, shortTag)) {
        if (!selfClosing) {
            (void)PushNonVisualScopeHelper(ctx, tagName);
        }
        return 1;
    }

    return 0;
}

AERO_NOINLINE void HandleVisualElementTagNoInline(
    XamlCtx& ctx,
    const OpenTagInfo& tag) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    const auto tagName = tag.tagName;
    const auto shortTag = tag.shortTag;
    const auto xName = tag.xName;
    const bool selfClosing = tag.selfClosing;
    const auto& attrs = tag.attrs;

    if (!ctx.seenRoot) {
        ctx.seenRoot = true;
        pkg.rootTypeName = std::string(tagName);
        if (shortTag == "Application") {
            for (const auto& a : attrs) {
                if (a.name == "StartupUri") {
                    pkg.startupUri = DecodeXamlXmlEntities(str::Trim(a.value));
                }
            }
        }
        if (ctx.activeIncludeStack.size() <= 1u) {
            if (!ctx.effectiveXamlPath.empty()) {
                const std::filesystem::path xDir =
                    std::filesystem::path(NormalizeSlashes(ctx.effectiveXamlPath)).parent_path();
                if (!xDir.empty()) {
                    UIResourceContext::Current().AddXamlSearchDirectory(xDir.string());
                }
            }
            if (!ctx.input.resourceSearchRoot.empty()) {
                UIResourceContext::Current().AddXamlSearchDirectory(ctx.input.resourceSearchRoot);
            }
            for (const auto& sp : ctx.input.resourceSearchPaths) {
                if (!sp.empty()) {
                    UIResourceContext::Current().AddXamlSearchDirectory(sp);
                }
            }
        }
        if (ctx.input.autoMergeGlobalResources && ctx.activeIncludeStack.size() <= 1u &&
            !ctx.effectiveXamlPath.empty()) {
            const std::filesystem::path xamlDir =
                std::filesystem::path(NormalizeSlashes(ctx.effectiveXamlPath)).parent_path();
            const std::string globalRes = DiscoverGlobalResourcesPathForDirectory(xamlDir);
            if (!globalRes.empty()) {
                CompileAndMergeExternalDictionaryNoInline(ctx, globalRes);
            }
        }
    }

    if (!xName.empty()) {
        state.namedElementTypes[std::string(xName)] = std::string(tagName);
        AxbNamedControlSlot slot{};
        slot.name = std::string(xName);
        slot.typeName = std::string(tagName);
        slot.typeSymbolId = rs_sem::stableTypeId("Aero.UI." + std::string(shortTag));
        pkg.namedControls.push_back(std::move(slot));
    }

    bool isCustomTemplated = false;
    (void)IsCustomControlRegistered(tagName, &isCustomTemplated);

    const bool isDirectHeaderChild = !scopeStack.empty() &&
        scopeStack.back().kind == XmlScopeKind::TransparentContainer &&
        str::EndsWith(str::StripNamespacePrefix(scopeStack.back().tagName), ".Header");

    const bool isInlinePrimitiveVisualItem =
        (shortTag == "Double" || shortTag == "Single" || shortTag == "Int32" ||
         shortTag == "Int64" || shortTag == "Boolean" || shortTag == "String");
    AxbInstruction beginInst{};
    beginInst.opcode = AxbOpcode::BeginElement;
    beginInst.flags = isDirectHeaderChild ? 0x01u : 0x00u;
    beginInst.strIdx0 = pkg.InternString(isInlinePrimitiveVisualItem ? std::string_view("TextBlock") : tagName);
    beginInst.strIdx1 = pkg.InternString(xName);
    pkg.instructions.push_back(std::move(beginInst));

    std::string attachedStyleKey;
    const std::string_view ownerHintForTemplate =
        !t_activeTemplateTargetTypeHint.empty() ? t_activeTemplateTargetTypeHint : tagName;

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
                     attr.name == "GotFocus" || attr.name == "LostFocus" ||
                     attr.name == "MediaEnded") {
                canonicalEvent = std::string(attr.name);
            }
            if (!canonicalEvent.empty()) {
                const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                AxbEventBindingRecord ev{};
                ev.controlName = std::string(xName);
                ev.eventName = std::move(canonicalEvent);
                ev.handlerMethodName = std::string(str::Trim(attr.value));
                pkg.eventBindings.push_back(std::move(ev));

                AxbInstruction evInst{};
                evInst.opcode = AxbOpcode::BindEvent;
                evInst.strIdx0 = evIdx;
                pkg.instructions.push_back(std::move(evInst));
                continue;
            }
        }

        const std::string_view trimmedVal = str::Trim(attr.value);

        if (attr.name == "Command" && trimmedVal.size() > 2 &&
            trimmedVal.front() == '{' && trimmedVal.back() == '}') {
            std::string_view cmdInner = str::Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
            if (cmdInner.rfind("Binding ", 0) == 0) cmdInner = str::Trim(cmdInner.substr(8));
            else if (cmdInner.rfind("Bind ", 0) == 0) cmdInner = str::Trim(cmdInner.substr(5));
            if (const std::size_t comma = cmdInner.find(','); comma != std::string_view::npos) {
                cmdInner = str::Trim(cmdInner.substr(0, comma));
            }
            if (cmdInner.rfind("Path=", 0) == 0) cmdInner = str::Trim(cmdInner.substr(5));
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
                    : ResolveOwnerAwarePropertyId(attr.name, tagName));

        if (trimmedVal.size() > 2 && trimmedVal.front() == '{' && trimmedVal.back() == '}') {
            std::string_view inner = str::Trim(trimmedVal.substr(1, trimmedVal.size() - 2));
            if (inner.rfind("EcsBind ", 0) == 0) {
                inner = str::Trim(inner.substr(8));
                const std::size_t dot = inner.rfind('.');
                const std::string_view compName =
                    (dot != std::string_view::npos) ? inner.substr(0, dot) : inner;
                const std::string_view fieldName =
                    (dot != std::string_view::npos) ? inner.substr(dot + 1) : std::string_view{};

                const std::string canonicalComp = compiler_detail::CanonicalizeEcsComponentName(compName);
                AxbInstruction ecsInst{};
                ecsInst.opcode = AxbOpcode::BindEcsColumn;
                ecsInst.propId = propId;
                ecsInst.strIdx0 = pkg.InternString(compName);
                ecsInst.strIdx1 = pkg.InternString(fieldName);
                ecsInst.symbolId = rs_sem::stableTypeId(canonicalComp);
                pkg.instructions.push_back(std::move(ecsInst));
                continue;
            }

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
                    if (const UnifiedValue* resVal = FindLocalOrGlobalResourceHelper(pkg, keyPart)) {
                        resInst.literalValue = *resVal;
                        if (propId == PropId_PathData) {
                            if (const auto* sv = std::get_if<StringValue>(resVal)) {
                                EnsurePathBakedIntoPackageHelper(ctx, sv->view());
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
                    const std::uint16_t srcPropId =
                        ResolveTemplateBindingSourcePropId(tb.sourcePath, t_activeTemplateTargetTypeHint);
                    if (srcPropId != compiler_detail::kUnmappedPropId) {
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
                std::string_view memberExpr = str::Trim(inner.substr(8u));
                if (memberExpr.rfind("Member=", 0) == 0) {
                    memberExpr = str::Trim(memberExpr.substr(7u));
                }
                if (!memberExpr.empty() && (memberExpr.front() == '\'' || memberExpr.front() == '"') &&
                    memberExpr.back() == memberExpr.front() && memberExpr.size() >= 2u) {
                    memberExpr = memberExpr.substr(1u, memberExpr.size() - 2u);
                }
                const std::string_view bareMem = str::StripNamespacePrefix(memberExpr);
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
                        : AeroCompiler::ParseLiteralPropertyValue(propId, resolvedStaticStr);
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
                if (const UnifiedValue* resVal = FindLocalOrGlobalResourceHelper(pkg, parsedBind.staticResourceSourceKey)) {
                    if (!parsedBind.stringFormat.empty()) {
                        resInst.literalValue = value::FromString(
                            FormatBindingString(parsedBind.stringFormat, resVal, 1u, nullptr));
                    } else {
                        resInst.literalValue = *resVal;
                    }
                } else if (!parsedBind.fallbackValue.empty()) {
                    resInst.literalValue = AeroCompiler::ParseLiteralPropertyValue(propId, parsedBind.fallbackValue);
                }
                pkg.instructions.push_back(std::move(resInst));
                continue;
            }

            if (parsedBind.relativeTemplatedParent && !TemplatedParentNeedsCompiledChain(parsedBind)) {
                UnifiedValue parsedFallback{};
                if (!parsedBind.fallbackValue.empty()) {
                    parsedFallback = AeroCompiler::ParseLiteralPropertyValue(propId, parsedBind.fallbackValue);
                    AxbInstruction fbInst{};
                    fbInst.opcode = AxbOpcode::SetStaticProperty;
                    fbInst.propId = propId;
                    fbInst.literalValue = parsedFallback;
                    pkg.instructions.push_back(std::move(fbInst));
                }
                const std::uint16_t srcPropId =
                    ResolveTemplateBindingSourcePropId(parsedBind.pathPart, t_activeTemplateTargetTypeHint);
                if (srcPropId != compiler_detail::kUnmappedPropId) {
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

            AxbCompiledBindingChain chain = BuildCompiledBindingChainHelper(
                pkg, parsedBind, propId, true, ownerHintForTemplate);
            chain.mode = mode;

            if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                AxbInstruction fbInst{};
                fbInst.opcode = AxbOpcode::SetStaticProperty;
                fbInst.propId = propId;
                fbInst.literalValue = chain.fallbackValue;
                pkg.instructions.push_back(std::move(fbInst));
            }

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

        if (propId == PropId_RenderTransform) {
            EmitStaticRenderTransformMatrix(pkg, ParseXamlMatrix2DLiteral(trimmedVal));
            continue;
        }
        if (propId == PropId_LayoutTransform) {
            EmitStaticLayoutTransformMatrix(pkg, ParseXamlMatrix2DLiteral(trimmedVal));
            continue;
        }

        const UnifiedValue litVal = AeroCompiler::ParseLiteralPropertyValue(propId, trimmedVal);
        if (propId == PropId_PathData) {
            if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                EnsurePathBakedIntoPackageHelper(ctx, sv->view());
            }
        } else if (propId == PropId_ShapePoints) {
            if (const auto* sv = std::get_if<StringValue>(&litVal)) {
                const std::string svg = ConvertPointsToSvgPath(sv->view(), shortTag == "Polygon");
                if (!svg.empty()) {
                    EnsurePathBakedIntoPackageHelper(ctx, svg);
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

AERO_NOINLINE bool FinalizeXamlPhaseNoInline(
    XamlCtx& ctx,
    bool isSvgInput,
    bool isLottieInput,
    bool isRiveInput) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;

    if (!isSvgInput && !isLottieInput && !isRiveInput && !state.scopeStack.empty()) {
        ctx.result.diagnostics.push_back(
            "AERO_XAML_UNCLOSED_TAG: Unclosed XML element '<" + state.scopeStack.back().tagName + ">'");
        ctx.result.succeeded = false;
        return false;
    }

    if (!state.dtRootTriggersStyleKey.empty() && !pkg.instructions.empty() &&
        pkg.instructions[0].opcode == AxbOpcode::BeginElement) {
        for (auto& st : pkg.styles) {
            if (st.key == state.dtRootTriggersStyleKey) {
                if ((st.targetTypeName.empty() || st.targetTypeName == "UIElement") &&
                    !pkg.rootTypeName.empty()) {
                    st.targetTypeName = std::string(str::StripNamespacePrefix(pkg.rootTypeName));
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
                            if (!existingStyleKey.empty() && existingStyleKey != state.dtRootTriggersStyleKey) {
                                if (st.basedOnKey.empty()) {
                                    st.basedOnKey = existingStyleKey;
                                }
                            }
                        }
                        inst.literalValue = value::FromString(state.dtRootTriggersStyleKey);
                        updatedExistingStyleInst = true;
                    }
                }
                if (!updatedExistingStyleInst) {
                    AxbInstruction dtStyleInst{};
                    dtStyleInst.opcode = AxbOpcode::SetStaticProperty;
                    dtStyleInst.propId = PropId_Style;
                    dtStyleInst.literalValue = value::FromString(state.dtRootTriggersStyleKey);
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

    FlushDeferredMtsdfJobsNoInline(ctx);
    if (!pkg.mtsdfPaths.empty() && !isSvgInput && !isLottieInput && !isRiveInput) {
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, 256u);
    }

    if (!isSvgInput && !isLottieInput && !isRiveInput) {
        compiler_detail::PopulateSceneIrFromPackage(pkg, FrontendSourceFormat::Xaml, ctx.effectiveXamlPath, ctx.result.sceneIr);
    }
    return true;
}

AERO_NOINLINE void HandleInnerTextNoInline(XamlCtx& ctx, std::size_t lt) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    const std::string_view rawInner = str::Trim(ctx.src.substr(ctx.pos, lt - ctx.pos));
    if (rawInner.empty()) {
        return;
    }
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
               top.targetPropId != 0 && state.nonVisualDepth == 0) {
        if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
        top.accumulatedText += DecodeXamlXmlEntities(rawInner);
    } else if (top.kind == XmlScopeKind::VisualElement && state.nonVisualDepth == 0) {
        const std::string_view st = str::StripNamespacePrefix(top.tagName);
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

AERO_NOINLINE void HandleClosingTagNoInline(XamlCtx& ctx) {
    auto& pkg = ctx.pkg;
    auto& state = ctx.st;
    auto& scopeStack = state.scopeStack;
    if (scopeStack.empty()) {
        return;
    }
    const std::size_t closingDepth = scopeStack.size();

    if (!state.keyFrameStack.empty() && state.keyFrameStack.back().scopeDepth == closingDepth) {
        auto finishedKf = std::make_unique<ActiveKeyFrameScope>(std::move(state.keyFrameStack.back()));
        state.keyFrameStack.pop_back();
        if (!state.timelineStack.empty() &&
            (!std::holds_alternative<std::monostate>(finishedKf->kf.value) ||
             !finishedKf->kf.bindingElementName.empty())) {
            state.timelineStack.back().tl.keyFrames.push_back(std::move(finishedKf->kf));
        }
    }

    if (!state.timelineStack.empty() && state.timelineStack.back().scopeDepth == closingDepth) {
        auto finishedTl = std::make_unique<ActiveTimelineScope>(std::move(state.timelineStack.back()));
        state.timelineStack.pop_back();
        if (!state.storyboardStack.empty() && finishedTl->tl.targetPropertyId != compiler_detail::kUnmappedPropId) {
            std::sort(
                finishedTl->tl.keyFrames.begin(),
                finishedTl->tl.keyFrames.end(),
                [](const AxbKeyFrame& a, const AxbKeyFrame& b) {
                    return a.timeSeconds < b.timeSeconds;
                });
            state.storyboardStack.back().sb.timelines.push_back(std::move(finishedTl->tl));
        }
    }

    if (!state.storyboardStack.empty() && state.storyboardStack.back().scopeDepth == closingDepth) {
        auto finishedSb = std::make_unique<ActiveStoryboardScope>(std::move(state.storyboardStack.back()));
        state.storyboardStack.pop_back();
        switch (finishedSb->owner) {
            case StoryboardOwnerKind::StandaloneResource:
                if (!finishedSb->sb.key.empty() || !finishedSb->sb.timelines.empty()) {
                    pkg.storyboards.push_back(std::move(finishedSb->sb));
                }
                break;
            case StoryboardOwnerKind::TriggerEnter:
                if (!state.triggerStack.empty() && !finishedSb->sb.timelines.empty()) {
                    state.triggerStack.back().trig.enterStoryboards.push_back(std::move(finishedSb->sb));
                }
                break;
            case StoryboardOwnerKind::TriggerExit:
                if (!state.triggerStack.empty() && !finishedSb->sb.timelines.empty()) {
                    state.triggerStack.back().trig.exitStoryboards.push_back(std::move(finishedSb->sb));
                }
                break;
            case StoryboardOwnerKind::VisualState:
                if (!state.visualStateStack.empty()) {
                    state.visualStateStack.back().state.storyboard = std::move(finishedSb->sb);
                }
                break;
            case StoryboardOwnerKind::EventTrigger:
                if (!state.eventTriggerStack.empty() && !finishedSb->sb.timelines.empty()) {
                    state.eventTriggerStack.back().et.storyboards.push_back(std::move(finishedSb->sb));
                }
                break;
            case StoryboardOwnerKind::IgnoredEventTrigger:
                break;
        }
    }

    if (!state.gradientStack.empty() && state.gradientStack.back().scopeDepth == closingDepth) {
        auto finishedGrad = std::make_unique<ActiveGradientBrushScope>(std::move(state.gradientStack.back()));
        state.gradientStack.pop_back();
        if (finishedGrad->desc.stops.empty()) {
            finishedGrad->desc.stops.push_back(GradientStop{0.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}});
            finishedGrad->desc.stops.push_back(GradientStop{1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}});
        }
        float separatedBrushOpacity = 1.0f;
        bool emitSeparatedBrushOpacity = false;
        if (finishedGrad->owner == GradientScopeOwner::PropertyElement &&
            (finishedGrad->targetPropId == PropId_Background ||
             finishedGrad->targetPropId == PropId_BorderBrush) &&
            std::fabs(finishedGrad->desc.opacity - 1.0f) > 1e-5f) {
            separatedBrushOpacity = std::clamp(finishedGrad->desc.opacity, 0.0f, 1.0f);
            finishedGrad->desc.opacity = 1.0f;
            emitSeparatedBrushOpacity = true;
        }
        BakeGradientBrushRamp(finishedGrad->desc);
        const std::string spec = SerializeGradientBrushSpec(finishedGrad->desc);
        (void)GradientBrushRegistry::Instance().RegisterOrResolveSpec(spec);

        if (finishedGrad->owner == GradientScopeOwner::PropertyElement) {
            AxbInstruction setInst{};
            setInst.opcode = AxbOpcode::SetStaticProperty;
            setInst.propId = finishedGrad->targetPropId;
            setInst.literalValue = value::FromString(spec);
            pkg.instructions.push_back(std::move(setInst));
            if (emitSeparatedBrushOpacity) {
                AxbInstruction opInst{};
                opInst.opcode = AxbOpcode::SetStaticProperty;
                opInst.propId = (finishedGrad->targetPropId == PropId_BorderBrush)
                    ? static_cast<std::uint16_t>(PropId_BorderOpacity)
                    : static_cast<std::uint16_t>(PropId_BackgroundOpacity);
                opInst.literalValue = MakeFloat(separatedBrushOpacity);
                pkg.instructions.push_back(std::move(opInst));
            }
        } else if (finishedGrad->owner == GradientScopeOwner::KeyedResource &&
                   !finishedGrad->resourceKey.empty()) {
            pkg.resources.emplace_back(
                finishedGrad->resourceKey,
                value::FromString(spec));
        } else if (finishedGrad->owner == GradientScopeOwner::StyleSetter &&
                   !state.setterStack.empty() && !state.setterStack.back().hasValue) {
            state.setterStack.back().setter.value = value::FromString(spec);
            state.setterStack.back().hasValue = true;
        }
    }

    if (!state.actionStack.empty() && state.actionStack.back().scopeDepth == closingDepth) {
        auto finishedAct = std::make_unique<ActiveInteractionActionScope>(std::move(state.actionStack.back()));
        state.actionStack.pop_back();
        CommitActionToActiveTriggerHelper(state, std::move(finishedAct->action), finishedAct->targetEventTrigger);
    }

    if (!state.bindingElemStack.empty() && state.bindingElemStack.back().scopeDepth == closingDepth) {
        auto finishedBe = std::make_unique<ActiveBindingElemScope>(std::move(state.bindingElemStack.back()));
        state.bindingElemStack.pop_back();
        ResolveConverterResourceHelper(pkg, finishedBe->chain.converter, finishedBe->chain.converterKey);
        if (finishedBe->insideMultiBinding && !state.multiBindingStack.empty()) {
            state.multiBindingStack.back().mb.sources.push_back(std::move(finishedBe->chain));
        } else if ((finishedBe->attachToActionTargetObject ||
                    finishedBe->attachToActionCmdParam ||
                    finishedBe->attachToActionValue) &&
                   !state.actionStack.empty()) {
            auto& act = state.actionStack.back().action;
            if (finishedBe->attachToActionTargetObject) {
                act.targetObjectBinding = std::make_shared<AxbCompiledBindingChain>(std::move(finishedBe->chain));
            } else if (finishedBe->attachToActionCmdParam) {
                act.hasCommandParameterBinding = true;
                act.commandParameterBinding = std::move(finishedBe->chain);
            } else {
                act.hasValueBinding = true;
                act.valueBinding = std::make_shared<AxbCompiledBindingChain>(std::move(finishedBe->chain));
            }
        } else {
            EmitSingleCompiledChainHelper(pkg, std::move(finishedBe->chain));
        }
    }

    if (!state.multiBindingStack.empty() && state.multiBindingStack.back().scopeDepth == closingDepth) {
        auto finishedMb = std::make_unique<ActiveMultiBindingScope>(std::move(state.multiBindingStack.back()));
        state.multiBindingStack.pop_back();
        ResolveConverterResourceHelper(pkg, finishedMb->mb.converter, finishedMb->mb.converterKey);
        if (finishedMb->isNestedInParentMulti && !state.multiBindingStack.empty()) {
            AxbCompiledBindingChain nestedSlot{};
            nestedSlot.nestedMultiBinding =
                std::make_shared<AxbCompiledMultiBinding>(std::move(finishedMb->mb));
            state.multiBindingStack.back().mb.sources.push_back(std::move(nestedSlot));
        } else if (finishedMb->attachToActionValue && !state.actionStack.empty()) {
            state.actionStack.back().action.hasValueBinding = true;
            state.actionStack.back().action.valueMultiBinding =
                std::make_shared<AxbCompiledMultiBinding>(std::move(finishedMb->mb));
        } else {
            EmitCompiledMultiBindingHelper(pkg, std::move(finishedMb->mb));
        }
    }

    if (!state.setterStack.empty() && state.setterStack.back().scopeDepth == closingDepth) {
        auto finishedSetter = std::make_unique<ActiveSetterScope>(std::move(state.setterStack.back()));
        state.setterStack.pop_back();
        if (finishedSetter->hasValue) {
            CommitSetterToOwnerHelper(ctx, std::move(finishedSetter->setter), finishedSetter->owner);
        }
    }

    if (!state.visualStateStack.empty() && state.visualStateStack.back().scopeDepth == closingDepth) {
        auto finishedVs = std::make_unique<ActiveVisualStateScope>(std::move(state.visualStateStack.back()));
        state.visualStateStack.pop_back();
        if (!state.vsgStack.empty() && !finishedVs->state.name.empty()) {
            state.vsgStack.back().group.states.push_back(std::move(finishedVs->state));
        }
    }

    if (!state.vsgStack.empty() && state.vsgStack.back().transitionsDepth == closingDepth) {
        state.vsgStack.back().transitionsDepth = 0u;
    }
    if (!state.vsgStack.empty() && state.vsgStack.back().scopeDepth == closingDepth) {
        auto finishedVsg = std::make_unique<ActiveVsgScope>(std::move(state.vsgStack.back()));
        state.vsgStack.pop_back();
        if (!state.styleStack.empty() && !finishedVsg->group.states.empty()) {
            state.styleStack.back().def.visualStateGroups.push_back(std::move(finishedVsg->group));
        }
    }

    if (!state.triggerStack.empty()) {
        if (state.triggerStack.back().enterActionsDepth == closingDepth) {
            state.triggerStack.back().enterActionsDepth = 0u;
        }
        if (state.triggerStack.back().exitActionsDepth == closingDepth) {
            state.triggerStack.back().exitActionsDepth = 0u;
        }
        if (state.triggerStack.back().scopeDepth == closingDepth) {
            auto finishedTrig = std::make_unique<ActiveTriggerScope>(std::move(state.triggerStack.back()));
            state.triggerStack.pop_back();
            if (!state.styleStack.empty() &&
                !finishedTrig->hasUnsupportedCondition &&
                !finishedTrig->trig.conditions.empty()) {
                state.styleStack.back().def.triggers.push_back(std::move(finishedTrig->trig));
            }
        }
    }

    if (!state.eventTriggerStack.empty() && state.eventTriggerStack.back().scopeDepth == closingDepth) {
        auto finishedEt = std::make_unique<ActiveEventTriggerScope>(std::move(state.eventTriggerStack.back()));
        state.eventTriggerStack.pop_back();
        if (!state.styleStack.empty() && finishedEt->recognized &&
            (!finishedEt->et.storyboards.empty() || !finishedEt->et.actions.empty())) {
            state.styleStack.back().def.eventTriggers.push_back(std::move(finishedEt->et));
        }
    }

    if (!state.styleStack.empty()) {
        if (state.styleStack.back().controlTemplateTriggersDepth == closingDepth) {
            state.styleStack.back().controlTemplateTriggersDepth = 0u;
        }
        if (state.styleStack.back().interactionTriggersDepth == closingDepth) {
            state.styleStack.back().interactionTriggersDepth = 0u;
        }
        if (state.styleStack.back().scopeDepth == closingDepth) {
            auto finishedStyle = std::make_unique<ActiveStyleScope>(std::move(state.styleStack.back()));
            state.styleStack.pop_back();
            if (!finishedStyle->def.key.empty() || !finishedStyle->def.targetTypeName.empty()) {
                bool mergedExisting = false;
                if (!finishedStyle->def.key.empty()) {
                    for (auto& existing : pkg.styles) {
                        if (existing.key == finishedStyle->def.key) {
                            if (existing.targetTypeName.empty() && !finishedStyle->def.targetTypeName.empty()) {
                                existing.targetTypeName = finishedStyle->def.targetTypeName;
                            }
                            if (existing.basedOnKey.empty() && !finishedStyle->def.basedOnKey.empty()) {
                                existing.basedOnKey = finishedStyle->def.basedOnKey;
                            }
                            if (!finishedStyle->def.controlTemplateKey.empty()) {
                                existing.controlTemplateKey = finishedStyle->def.controlTemplateKey;
                            }
                            if (finishedStyle->def.inlineControlTemplate) {
                                existing.inlineControlTemplate = finishedStyle->def.inlineControlTemplate;
                            }
                            existing.setters.insert(
                                existing.setters.end(),
                                finishedStyle->def.setters.begin(),
                                finishedStyle->def.setters.end());
                            existing.triggers.insert(
                                existing.triggers.end(),
                                finishedStyle->def.triggers.begin(),
                                finishedStyle->def.triggers.end());
                            existing.eventTriggers.insert(
                                existing.eventTriggers.end(),
                                finishedStyle->def.eventTriggers.begin(),
                                finishedStyle->def.eventTriggers.end());
                            existing.visualStateGroups.insert(
                                existing.visualStateGroups.end(),
                                finishedStyle->def.visualStateGroups.begin(),
                                finishedStyle->def.visualStateGroups.end());
                            mergedExisting = true;
                            break;
                        }
                    }
                }
                if (!mergedExisting) {
                    pkg.styles.push_back(std::move(finishedStyle->def));
                }
            }
        }
    }

    auto closed = std::make_unique<XmlScopeEntry>(std::move(scopeStack.back()));
    scopeStack.pop_back();
    if (closed->kind == XmlScopeKind::ResourceContainer ||
        closed->kind == XmlScopeKind::KeyedResourceItem ||
        closed->kind == XmlScopeKind::BrushPropertyElement ||
        closed->kind == XmlScopeKind::TransformPropertyElement ||
        closed->kind == XmlScopeKind::TransformChildNode ||
        closed->kind == XmlScopeKind::BindingPropertyElement ||
        closed->kind == XmlScopeKind::Transform3DPropertyElement ||
        closed->kind == XmlScopeKind::EffectPropertyElement ||
        closed->kind == XmlScopeKind::NonVisualSkip) {
        if (state.nonVisualDepth > 0) {
            --state.nonVisualDepth;
        }
    }
    if (closed->kind == XmlScopeKind::TransformPropertyElement) {
        if (closed->targetPropId == PropId_LayoutTransform) {
            EmitStaticLayoutTransformMatrix(pkg, closed->accumulatedTransform);
        } else if (closed->hasAnyRtStep && closed->canDecomposeStaticRt) {
            // Reset any prior attribute RenderTransform="m11,..." before emitting decomposed channels.
            EmitStaticRenderTransformMatrix(pkg, Matrix2D::Identity());
            if (closed->hasRtScale) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimScaleX, 0, 0, 0, 0, MakeFloat(closed->rtScaleX)});
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimScaleY, 0, 0, 0, 0, MakeFloat(closed->rtScaleY)});
            }
            if (closed->hasRtSkew) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimSkewX, 0, 0, 0, 0, MakeFloat(closed->rtSkewX)});
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimSkewY, 0, 0, 0, 0, MakeFloat(closed->rtSkewY)});
            }
            if (closed->hasRtRotate) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimRotateAngle, 0, 0, 0, 0, MakeFloat(closed->rtRotateAngle)});
            }
            if (closed->hasRtTranslate) {
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimTranslateX, 0, 0, 0, 0, MakeFloat(closed->rtTranslateX)});
                pkg.instructions.push_back(AxbInstruction{
                    AxbOpcode::SetStaticProperty, PropId_AnimTranslateY, 0, 0, 0, 0, MakeFloat(closed->rtTranslateY)});
            }
        } else {
            EmitStaticRenderTransformMatrix(pkg, closed->accumulatedTransform);
        }
    } else if (closed->isMergedDictSourceElement) {
        if (!closed->accumulatedText.empty()) {
            CompileAndMergeExternalDictionaryNoInline(ctx, closed->accumulatedText);
        }
    } else if (closed->kind == XmlScopeKind::KeyedResourceItem &&
               !closed->resourceKey.empty()) {
        const std::string_view st = str::StripNamespacePrefix(closed->tagName);
        if (!closed->brushAssigned) {
            if ((st == "Color" || st == "SolidColorBrush") && !closed->accumulatedText.empty()) {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    value::FromColor(ParseXamlColorLiteral(closed->accumulatedText)));
            } else if ((st == "Double" || st == "Single") && !closed->accumulatedText.empty()) {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    MakeFloat(compiler_detail::ParseFloat32(closed->accumulatedText, 0.0f)));
            } else if ((st == "Int32" || st == "Int64" || st == "Int16") &&
                       !closed->accumulatedText.empty()) {
                const long long iv = std::strtoll(closed->accumulatedText.c_str(), nullptr, 10);
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    value::FromInt(static_cast<std::int64_t>(iv)));
            } else if (st == "Boolean" && !closed->accumulatedText.empty()) {
                const std::string_view bt = str::Trim(closed->accumulatedText);
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    UnifiedValue{str::EqualsIgnoreCase(bt, "true") || bt == "1"});
            } else if (st == "Thickness" && !closed->accumulatedText.empty()) {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    ParseBuiltinLiteralPropertyValue(PropId_Margin, closed->accumulatedText));
            } else if (st == "CornerRadius" && !closed->accumulatedText.empty()) {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    ParseBuiltinLiteralPropertyValue(PropId_CornerRadius, closed->accumulatedText));
            } else if ((st == "Geometry" || st == "StreamGeometry" || st == "PathGeometry") &&
                       !closed->accumulatedText.empty()) {
                EnsurePathBakedIntoPackageHelper(ctx, closed->accumulatedText);
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    value::FromString(closed->accumulatedText));
            } else if (st == "String") {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    value::FromString(closed->accumulatedText));
            } else if (!closed->accumulatedText.empty()) {
                UpsertPackageResource(
                    pkg,
                    closed->resourceKey,
                    value::FromString(closed->accumulatedText));
            }
        }
    } else if (closed->kind == XmlScopeKind::TransparentContainer &&
               closed->targetPropId != 0 && !closed->accumulatedText.empty()) {
        if (closed->targetPropId == PropId_PathData) {
            EnsurePathBakedIntoPackageHelper(ctx, closed->accumulatedText);
        }
        AxbInstruction propTextInst{};
        propTextInst.opcode = AxbOpcode::SetStaticProperty;
        propTextInst.propId = closed->targetPropId;
        propTextInst.literalValue = value::FromString(std::move(closed->accumulatedText));
        pkg.instructions.push_back(std::move(propTextInst));
    } else if (closed->tagName == "PathFigure:Closed") {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            if (it->kind == XmlScopeKind::VisualElement) break;
            if ((it->kind == XmlScopeKind::TransparentContainer && it->targetPropId == PropId_PathData) ||
                (it->kind == XmlScopeKind::KeyedResourceItem &&
                 (str::StripNamespacePrefix(it->tagName) == "PathGeometry" || str::StripNamespacePrefix(it->tagName) == "Geometry"))) {
                it->accumulatedText += " Z";
                break;
            }
        }
    } else if (closed->isListViewViewContainer && !closed->gridViewColumnsSpec.empty()) {
        AxbInstruction gvInst{};
        gvInst.opcode = AxbOpcode::SetStaticProperty;
        gvInst.propId = PropId_GridViewColumnsSpec;
        gvInst.literalValue = value::FromString(std::move(closed->gridViewColumnsSpec));
        pkg.instructions.push_back(std::move(gvInst));
    } else if (closed->kind == XmlScopeKind::RunInline && !closed->accumulatedText.empty()) {
        const std::string_view closedSt = str::StripNamespacePrefix(closed->tagName);
        std::string richChunk = closed->accumulatedRichText.empty()
            ? closed->accumulatedText
            : closed->accumulatedRichText;
        bool makesRich = closed->hasInlineRichFormatting;
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
                if (!it->accumulatedText.empty() && !closed->accumulatedText.empty()) {
                    const char prevCh = it->accumulatedText.back();
                    const char nextCh = closed->accumulatedText.front();
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
                    for (char ch : closed->accumulatedText) {
                        if (ch == '{' || ch == '}') it->inlineRunFormatTemplate.push_back(ch);
                        it->inlineRunFormatTemplate.push_back(ch);
                    }
                }
                it->accumulatedText += closed->accumulatedText;
                it->accumulatedRichText += richChunk;
                if (makesRich) {
                    it->hasInlineRichFormatting = true;
                }
                break;
            }
        }
    } else if (closed->kind == XmlScopeKind::VisualElement) {
        if (!closed->accumulatedText.empty()) {
            AxbInstruction textInst{};
            textInst.opcode = AxbOpcode::SetStaticProperty;
            textInst.propId = PropId_Text;
            textInst.literalValue = value::FromString(std::move(closed->accumulatedText));
            pkg.instructions.push_back(std::move(textInst));
            if (closed->hasInlineRichFormatting &&
                !closed->accumulatedRichText.empty() &&
                str::StripNamespacePrefix(closed->tagName) == "TextBlock") {
                AxbInstruction richInst{};
                richInst.opcode = AxbOpcode::SetStaticProperty;
                richInst.propId = PropId_RichText;
                richInst.literalValue = value::FromString(std::move(closed->accumulatedRichText));
                pkg.instructions.push_back(std::move(richInst));
            }
        }
        if (!closed->inlineRunBindings.empty()) {
            AxbCompiledMultiBinding runMb{};
            runMb.targetPropertyId = PropId_Text;
            runMb.mode = BindingMode::OneWay;
            runMb.stringFormat = std::move(closed->inlineRunFormatTemplate);
            runMb.sources = std::move(closed->inlineRunBindings);
            EmitCompiledMultiBindingHelper(pkg, std::move(runMb));
        }
        AxbInstruction endInst{};
        endInst.opcode = AxbOpcode::EndElement;
        pkg.instructions.push_back(std::move(endInst));
    } else if (closed->kind == XmlScopeKind::ResourceContainer && scopeStack.empty() &&
               pkg.rootTypeName == "ResourceDictionary") {
        AxbInstruction endInst{};
        endInst.opcode = AxbOpcode::EndElement;
        pkg.instructions.push_back(std::move(endInst));
    }
}

AERO_NOINLINE bool RunXamlPhaseNoInline(
    const AeroCompiler& compiler,
    const AeroCompileInput& input,
    std::vector<std::string>& activeIncludeStack,
    std::string& effectiveXamlPath,
    std::string& loadedContentStorage,
    std::string& rootDataType,
    AeroCompileResult& result) {
    AxbPackage& pkg = result.axbPackage;
    const std::string_view src = loadedContentStorage;

    const bool isSvgInput = compiler_detail::IsSvgAssetInput(effectiveXamlPath, src);
    const bool isLottieInput = !isSvgInput && compiler_detail::IsLottieAssetInput(effectiveXamlPath, src);
    const bool isRiveInput = !isSvgInput && !isLottieInput && compiler_detail::IsRiveAssetInput(effectiveXamlPath, src);
    if (isSvgInput || isLottieInput || isRiveInput) {
        if (!RunFrontendImportPhaseNoInline(
                effectiveXamlPath, src, isSvgInput, isLottieInput, isRiveInput, result)) {
            return false;
        }
    }

    auto state = std::make_unique<XamlParseState>();
    XamlCtx ctx{
        compiler,
        input,
        activeIncludeStack,
        result,
        pkg,
        effectiveXamlPath,
        rootDataType,
        src,
        (effectiveXamlPath.rfind("__aero_visual_res__:", 0) == 0),
        false,
        (isSvgInput || isLottieInput || isRiveInput) ? src.size() : 0u,
        *state,
    };

    auto& pos = ctx.pos;
    auto& seenRoot = ctx.seenRoot;
    auto& scopeStack = state->scopeStack;
    auto& nonVisualDepth = state->nonVisualDepth;

    while (pos < src.size()) {
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) {
            break;
        }

        // Capture inner text between previous '>' and current '<'
        if (lt > pos && !scopeStack.empty()) {
            HandleInnerTextNoInline(ctx, lt);
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
            HandleClosingTagNoInline(ctx);
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
        auto& openTag = state->openTag;
        openTag.tagName = src.substr(pos, tagEnd - pos);
        openTag.shortTag = str::StripNamespacePrefix(openTag.tagName);
        openTag.xName = {};
        openTag.xKey = {};
        openTag.selfClosing = false;
        openTag.attrs.clear();
        pos = tagEnd;

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
                openTag.selfClosing = true;
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
                    openTag.xName = attrVal;
                } else if (attrName == "x:Key") {
                    openTag.xKey = attrVal;
                } else if (attrName.rfind("xmlns", 0) == 0 || attrName == "mc:Ignorable") {
                    // Ignore XML namespace declarations
                } else {
                    openTag.attrs.push_back(ParsedXmlAttr{attrName, attrVal});
                }
            }
        }
        if (!closedTagBracket) {
            result.diagnostics.push_back(
                "AERO_XAML_SYNTAX_ERROR: Unterminated XML tag '<" + std::string(openTag.tagName) + ">'");
            result.succeeded = false;
            return false;
        }

        // Inside non-visual scope: handle Style/Trigger/Storyboard/VSM, resource extraction, 2D transforms, or brushes
        if (nonVisualDepth > 0) {
            XmlScopeEntry& parentScope = scopeStack.back();

            // 1. Active <Element.RenderTransform> / <Element.LayoutTransform> subtree
            if (XmlScopeEntry* tfScope = FindEnclosingTransformScopeHelper(scopeStack)) {
                HandleTransformChildNoInline(ctx, tfScope, state->openTag);
                continue;
            }

            // 2. Style, ControlTemplate, ItemsPanelTemplate, KeyBinding, CommandBinding
            {
                const int stHandled = TryHandleStyleControlTemplateKeyBindingsNoInline(ctx, parentScope, state->openTag);
                if (stHandled > 0) {
                    continue;
                }
                if (stHandled < 0) {
                    return false;
                }
            }

            {
                const int tplHandled = TryHandleSubPackageTemplateTagNoInline(ctx, parentScope, state->openTag);
                if (tplHandled > 0) {
                    continue;
                }
                if (tplHandled < 0) {
                    return false;
                }
            }

            if (TryHandleTriggerAndConditionTagNoInline(ctx, state->openTag) > 0) {
                continue;
            }

            if (TryHandleInteractionActionTagNoInline(ctx, state->openTag) > 0) {
                continue;
            }

            if (TryHandleBindingAndMultiBindingTagNoInline(ctx, parentScope, state->openTag) > 0) {
                continue;
            }

            if (TryHandleVsmStoryboardAndSetterTagNoInline(ctx, parentScope, state->openTag) > 0) {
                continue;
            }

            HandleGraphicsEffectsShadersAndResourcesNoInline(ctx, parentScope, state->openTag, lt);
            continue;
        }

        {
            const int handledTop = HandleTopLevelAndInlinePropertyElementsNoInline(ctx, state->openTag);
            if (handledTop > 0) {
                continue;
            }
            if (handledTop < 0) {
                return false;
            }
        }

        HandleVisualElementTagNoInline(ctx, state->openTag);
    }

    return FinalizeXamlPhaseNoInline(ctx, isSvgInput, isLottieInput, isRiveInput);
}

AERO_NOINLINE bool RunScriptSynthesisAndCompilationPhase(
    const AeroCompileInput& input,
    std::string_view effectiveXamlPath,
    const std::string& loadedContentStorage,
    std::string& rootDataType,
    AeroCompileResult& result) {
    AxbPackage& pkg = result.axbPackage;

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

    auto hasWholeWord = [](std::string_view text, std::string_view word) noexcept -> bool {
        if (word.empty()) return false;
        std::size_t pos = 0;
        while ((pos = text.find(word, pos)) != std::string_view::npos) {
            const bool leftOk =
                (pos == 0) ||
                (!std::isalnum(static_cast<unsigned char>(text[pos - 1])) &&
                 text[pos - 1] != '_');
            const std::size_t endPos = pos + word.size();
            const bool rightOk =
                (endPos == text.size()) ||
                (!std::isalnum(static_cast<unsigned char>(text[endPos])) &&
                 text[endPos] != '_');
            if (leftOk && rightOk) {
                return true;
            }
            pos = endPos;
        }
        return false;
    };

    bool preLoweredImportsAeroUi = false;
    bool preLoweredDefinesAeroUi = false;
    auto checkAeroUiMarkers = [&](std::string_view text) {
        if (text.find("module Aero.UI;") != std::string_view::npos ||
            text.find("namespace Aero.UI") != std::string_view::npos) {
            preLoweredDefinesAeroUi = true;
        }
        if (text.find("import Aero.UI;") != std::string_view::npos ||
            text.find("using Aero.UI;") != std::string_view::npos ||
            text.find("using System.ComponentModel;") != std::string_view::npos ||
            text.find("using System.Collections.ObjectModel") != std::string_view::npos ||
            text.find("using System.Globalization;") != std::string_view::npos ||
            text.find("using System.Windows") != std::string_view::npos ||
            hasWholeWord(text, "ObservableObject") ||
            hasWholeWord(text, "ViewModelBase") ||
            hasWholeWord(text, "BindableBase") ||
            hasWholeWord(text, "INotifyPropertyChanged") ||
            hasWholeWord(text, "RelayCommand") ||
            hasWholeWord(text, "DelegateCommand") ||
            text.find("ObservableList<") != std::string_view::npos ||
            text.find("ObservableCollection<") != std::string_view::npos ||
            hasWholeWord(text, "Action") ||
            hasWholeWord(text, "Func") ||
            hasWholeWord(text, "ICommand")) {
            preLoweredImportsAeroUi = true;
        }
    };

    // Generate *.g.rs partial class stub when x:Class is specified
    std::vector<SourceFile> allSources;
    allSources.reserve(input.scriptSources.size() + 2u);
    for (const auto& rawSf : input.scriptSources) {
        std::string cleaned = preprocessCSharpDirectives(rawSf.content);
        if (isPureNativeExtensionMetadataFile(cleaned)) {
            continue;
        }
        checkAeroUiMarkers(rawSf.content);
        checkAeroUiMarkers(cleaned);
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
                const std::string_view headerSlice = str::Trim(cleanSrc.substr(after, openBrace - after));
                if (!headerSlice.empty() && headerSlice.front() == ':') {
                    return true;
                }
                searchPos = openBrace + 1u;
            }
            return false;
        };
        auto mapXamlTagToSdkType = [](std::string_view rawTag) -> std::string {
            const std::string_view st = str::StripNamespacePrefix(rawTag);
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

        std::string xamlLeafName = effectiveXamlPath.empty() ? input.xamlPath : std::string(effectiveXamlPath);
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

    // Auto-provide built-in Aero.UI native SDK declarations when imported by user scripts,
    // and run L2 C# compatibility syntax lowering across all script units.
    if (!allSources.empty()) {
        bool importsAeroUi = preLoweredImportsAeroUi;
        bool definesAeroUi = preLoweredDefinesAeroUi;
        for (const auto& sf : allSources) {
            checkAeroUiMarkers(sf.content);
            importsAeroUi = importsAeroUi || preLoweredImportsAeroUi;
            definesAeroUi = definesAeroUi || preLoweredDefinesAeroUi;
        }
        const bool injectSdk = (importsAeroUi || !pkg.className.empty()) && !definesAeroUi;

        auto extractDeclaredModule = [](std::string_view src) -> std::string {
            auto skipWsAndComments = [&](std::size_t p) -> std::size_t {
                while (p < src.size()) {
                    const char ch = src[p];
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                        ++p;
                        continue;
                    }
                    if (ch == '/' && p + 1 < src.size() && src[p + 1] == '/') {
                        p += 2;
                        while (p < src.size() && src[p] != '\n') ++p;
                        continue;
                    }
                    if (ch == '/' && p + 1 < src.size() && src[p + 1] == '*') {
                        p += 2;
                        while (p + 1 < src.size() && !(src[p] == '*' && src[p + 1] == '/')) ++p;
                        p = (p + 1 < src.size()) ? (p + 2) : src.size();
                        continue;
                    }
                    break;
                }
                return p;
            };
            int depth = 0;
            std::size_t i = 0;
            while (i < src.size()) {
                i = skipWsAndComments(i);
                if (i >= src.size()) break;
                const char c = src[i];
                if (c == '"') {
                    ++i;
                    while (i < src.size()) {
                        if (src[i] == '\\' && i + 1 < src.size()) {
                            i += 2;
                            continue;
                        }
                        if (src[i++] == '"') break;
                    }
                    continue;
                }
                if (c == '{') { ++depth; ++i; continue; }
                if (c == '}') { if (depth > 0) --depth; ++i; continue; }
                if (depth == 0 && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
                    const std::size_t start = i;
                    while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) ++i;
                    const std::string_view kw = src.substr(start, i - start);
                    if (kw == "module" || kw == "namespace") {
                        std::size_t p = skipWsAndComments(i);
                        std::string mod;
                        while (p < src.size()) {
                            if (std::isalpha(static_cast<unsigned char>(src[p])) || src[p] == '_') {
                                const std::size_t segStart = p;
                                while (p < src.size() && (std::isalnum(static_cast<unsigned char>(src[p])) || src[p] == '_')) ++p;
                                mod.append(src.substr(segStart, p - segStart));
                                p = skipWsAndComments(p);
                                if (p < src.size() && src[p] == '.') {
                                    mod.push_back('.');
                                    p = skipWsAndComments(p + 1);
                                    continue;
                                }
                                if (p < src.size() && ((kw == "module" && src[p] == ';') ||
                                                       (kw == "namespace" && (src[p] == ';' || src[p] == '{')))) {
                                    return mod;
                                }
                                break;
                            }
                            break;
                        }
                    }
                    continue;
                }
                ++i;
            }
            return {};
        };

        std::unordered_set<std::string> availableModules;
        if (injectSdk) {
            availableModules.insert("Aero.UI");
        }
        for (const auto& sf : allSources) {
            const std::string mod = extractDeclaredModule(sf.content);
            if (!mod.empty()) {
                availableModules.insert(mod);
            }
        }

        auto ensureImportsAeroUiAfterModule = [](std::string& content) {
            auto skipWsAndComments = [&](std::size_t p) -> std::size_t {
                while (p < content.size()) {
                    const char ch = content[p];
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                        ++p;
                        continue;
                    }
                    if (ch == '/' && p + 1 < content.size() && content[p + 1] == '/') {
                        p += 2;
                        while (p < content.size() && content[p] != '\n') ++p;
                        continue;
                    }
                    if (ch == '/' && p + 1 < content.size() && content[p + 1] == '*') {
                        p += 2;
                        while (p + 1 < content.size() && !(content[p] == '*' && content[p + 1] == '/')) ++p;
                        p = (p + 1 < content.size()) ? (p + 2) : content.size();
                        continue;
                    }
                    break;
                }
                return p;
            };
            bool hasImportAeroUi = false;
            std::size_t moduleSemiAfter = std::string::npos;
            std::string moduleName;
            int depth = 0;
            std::size_t i = 0;
            while (i < content.size()) {
                i = skipWsAndComments(i);
                if (i >= content.size()) break;
                const char c = content[i];
                if (c == '"') {
                    ++i;
                    while (i < content.size()) {
                        if (content[i] == '\\' && i + 1 < content.size()) {
                            i += 2;
                            continue;
                        }
                        if (content[i++] == '"') break;
                    }
                    continue;
                }
                if (c == '{') { ++depth; ++i; continue; }
                if (c == '}') { if (depth > 0) --depth; ++i; continue; }
                if (depth == 0 && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
                    const std::size_t start = i;
                    while (i < content.size() && (std::isalnum(static_cast<unsigned char>(content[i])) || content[i] == '_')) ++i;
                    const std::string_view kw = std::string_view(content).substr(start, i - start);
                    if (kw == "module" || kw == "import") {
                        std::size_t p = skipWsAndComments(i);
                        std::string qname;
                        while (p < content.size() && (std::isalpha(static_cast<unsigned char>(content[p])) || content[p] == '_')) {
                            const std::size_t s0 = p;
                            while (p < content.size() && (std::isalnum(static_cast<unsigned char>(content[p])) || content[p] == '_')) ++p;
                            qname.append(std::string_view(content).substr(s0, p - s0));
                            p = skipWsAndComments(p);
                            if (p < content.size() && content[p] == '.') {
                                qname.push_back('.');
                                p = skipWsAndComments(p + 1);
                                continue;
                            }
                            if (p < content.size() && content[p] == ';') {
                                if (kw == "module" && moduleSemiAfter == std::string::npos) {
                                    moduleSemiAfter = p + 1;
                                    moduleName = qname;
                                } else if (kw == "import" && qname == "Aero.UI") {
                                    hasImportAeroUi = true;
                                }
                            }
                            break;
                        }
                    }
                    continue;
                }
                ++i;
            }
            if (hasImportAeroUi) {
                return;
            }
            if (moduleSemiAfter != std::string::npos) {
                if (moduleName != "Aero.UI") {
                    content.insert(moduleSemiAfter, " import Aero.UI;");
                }
            } else {
                content.insert(0, "import Aero.UI; ");
            }
        };

        for (auto& sf : allSources) {
            sf.content = LowerCSharpCompatSyntax(sf.content, &availableModules);
            if (injectSdk) {
                ensureImportsAeroUiAfterModule(sf.content);
            }
        }

        if (injectSdk) {
            std::string sdkCode =
                "module Aero.UI;\n"
                "public delegate void Action();\n"
                "public delegate void Action<T>(T arg);\n"
                "public delegate void Action<T1, T2>(T1 arg1, T2 arg2);\n"
                "public delegate void Action<T1, T2, T3>(T1 arg1, T2 arg2, T3 arg3);\n"
                "public delegate TResult Func<TResult>();\n"
                "public delegate TResult Func<T, TResult>(T arg);\n"
                "public delegate TResult Func<T1, T2, TResult>(T1 arg1, T2 arg2);\n"
                "public delegate TResult Func<T1, T2, T3, TResult>(T1 arg1, T2 arg2, T3 arg3);\n"
                "public class ObservableListEnumerator<T>\n"
                "{\n"
                "    public T[] _items;\n"
                "    public int _index;\n"
                "    public ObservableListEnumerator(T[] items) { _items = items; _index = -1; }\n"
                "    public bool MoveNext() { _index = _index + 1; return _index < _items.length; }\n"
                "    public T Current() { return _items[_index]; }\n"
                "}\n"
                "public class ObservableList<T>\n"
                "{\n"
                "    public T[] items;\n"
                "    public ObservableList() { items = new T[0]; }\n"
                "    public ObservableList(int capacity) { items = new T[0]; items.reserve(capacity); }\n"
                "    public int Count() { return items.length; }\n"
                "    public int Length() { return items.length; }\n"
                "    public bool IsEmpty() { return items.length == 0; }\n"
                "    public T Get(int index) { return items[index]; }\n"
                "    public void Set(int index, T value) { items[index] = value; }\n"
                "    public T First() { return items[0]; }\n"
                "    public T Last() { return items[items.length - 1]; }\n"
                "    public void Reserve(int capacity) { items.reserve(capacity); }\n"
                "    public void reserve(int capacity) { items.reserve(capacity); }\n"
                "    public void Add(T value) { items.push(value); }\n"
                "    public void add(T value) { items.push(value); }\n"
                "    public void Push(T value) { items.push(value); }\n"
                "    public void push(T value) { items.push(value); }\n"
                "    public void Insert(int index, T value) { items.insert(index, value); }\n"
                "    public void insert(int index, T value) { items.insert(index, value); }\n"
                "    public void RemoveAt(int index) { items.removeAt(index); }\n"
                "    public void removeAt(int index) { items.removeAt(index); }\n"
                "    public bool Remove(T value) { int idx = items.IndexOf(value); if (idx < 0) { return false; } items.removeAt(idx); return true; }\n"
                "    public bool remove(T value) { return Remove(value); }\n"
                "    public void Move(int oldIndex, int newIndex) { if (oldIndex == newIndex || oldIndex < 0 || oldIndex >= items.length || newIndex < 0 || newIndex >= items.length) { return; } T val = items[oldIndex]; items.removeAt(oldIndex); items.insert(newIndex, val); }\n"
                "    public int IndexOf(T value) { return items.IndexOf(value); }\n"
                "    public int indexOf(T value) { return items.IndexOf(value); }\n"
                "    public int LastIndexOf(T value) { return items.LastIndexOf(value); }\n"
                "    public int lastIndexOf(T value) { return items.LastIndexOf(value); }\n"
                "    public bool Contains(T value) { return items.Contains(value); }\n"
                "    public bool contains(T value) { return items.Contains(value); }\n"
                "    public void Reverse() { items.Reverse(); }\n"
                "    public void reverse() { items.Reverse(); }\n"
                "    public void Sort() { items.Sort(); }\n"
                "    public void sort() { items.Sort(); }\n"
                "    public T[] AsArray() { return items; }\n"
                "    public T[] ToArray() { T[] copy = new T[items.length]; int i = 0; while (i < items.length) { copy[i] = items[i]; i = i + 1; } return copy; }\n"
                "    public void Clear() { items.clear(); }\n"
                "    public void clear() { items.clear(); }\n"
                "    public ObservableListEnumerator<T> GetEnumerator() { return new ObservableListEnumerator<T>(items); }\n"
                "}\n"
                "public class ObservableCollection<T>\n"
                "{\n"
                "    public T[] items;\n"
                "    public ObservableCollection() { items = new T[0]; }\n"
                "    public ObservableCollection(int capacity) { items = new T[0]; items.reserve(capacity); }\n"
                "    public int Count() { return items.length; }\n"
                "    public int Length() { return items.length; }\n"
                "    public bool IsEmpty() { return items.length == 0; }\n"
                "    public T Get(int index) { return items[index]; }\n"
                "    public void Set(int index, T value) { items[index] = value; }\n"
                "    public T First() { return items[0]; }\n"
                "    public T Last() { return items[items.length - 1]; }\n"
                "    public void Reserve(int capacity) { items.reserve(capacity); }\n"
                "    public void reserve(int capacity) { items.reserve(capacity); }\n"
                "    public void Add(T value) { items.push(value); }\n"
                "    public void add(T value) { items.push(value); }\n"
                "    public void Push(T value) { items.push(value); }\n"
                "    public void push(T value) { items.push(value); }\n"
                "    public void Insert(int index, T value) { items.insert(index, value); }\n"
                "    public void insert(int index, T value) { items.insert(index, value); }\n"
                "    public void RemoveAt(int index) { items.removeAt(index); }\n"
                "    public void removeAt(int index) { items.removeAt(index); }\n"
                "    public bool Remove(T value) { int idx = items.IndexOf(value); if (idx < 0) { return false; } items.removeAt(idx); return true; }\n"
                "    public bool remove(T value) { return Remove(value); }\n"
                "    public void Move(int oldIndex, int newIndex) { if (oldIndex == newIndex || oldIndex < 0 || oldIndex >= items.length || newIndex < 0 || newIndex >= items.length) { return; } T val = items[oldIndex]; items.removeAt(oldIndex); items.insert(newIndex, val); }\n"
                "    public int IndexOf(T value) { return items.IndexOf(value); }\n"
                "    public int indexOf(T value) { return items.IndexOf(value); }\n"
                "    public int LastIndexOf(T value) { return items.LastIndexOf(value); }\n"
                "    public int lastIndexOf(T value) { return items.LastIndexOf(value); }\n"
                "    public bool Contains(T value) { return items.Contains(value); }\n"
                "    public bool contains(T value) { return items.Contains(value); }\n"
                "    public void Reverse() { items.Reverse(); }\n"
                "    public void reverse() { items.Reverse(); }\n"
                "    public void Sort() { items.Sort(); }\n"
                "    public void sort() { items.Sort(); }\n"
                "    public T[] AsArray() { return items; }\n"
                "    public T[] ToArray() { T[] copy = new T[items.length]; int i = 0; while (i < items.length) { copy[i] = items[i]; i = i + 1; } return copy; }\n"
                "    public void Clear() { items.clear(); }\n"
                "    public void clear() { items.clear(); }\n"
                "    public ObservableListEnumerator<T> GetEnumerator() { return new ObservableListEnumerator<T>(items); }\n"
                "}\n"
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

    // If RealScript sources were supplied, run single-pass Compilation (+ optional -O2 MIR Optimization) + Bytecode Lowering + Link
    if (!allSources.empty()) {
        rs_comp::CompilationOptions compOpts{};
        if (input.compilerSession != nullptr) {
            compOpts.session = std::shared_ptr<rs_comp::CompilerSession>(
                input.compilerSession,
                [](rs_comp::CompilerSession*) noexcept {});
        }
        if (input.enableReactiveMacros) {
            compOpts.macroPlugins.push_back(rs_comp::createStandardReactiveMacroPlugin());
        }
        rs_comp::Compilation compilation(std::move(allSources), std::move(compOpts));
        auto build = compilation.build();
        for (const auto& d : build.diagnostics.items()) {
            result.diagnostics.push_back(d.code + ": " + d.message);
        }
        if (build.diagnostics.hasErrors()) {
            result.succeeded = false;
            return false;
        }

        if (input.optimizeBytecode) {
            rs_opt::Optimizer optimizer;
            rs_opt::Options optOpts{};
            optOpts.level = rs_opt::Level::Aggressive;
            rs_diag::DiagnosticBag optDiag;
            auto optRes = optimizer.optimize(std::move(build.modules), optDiag, optOpts);
            for (const auto& d : optDiag.items()) {
                result.diagnostics.push_back(d.code + ": " + d.message);
            }
            if (optDiag.hasErrors()) {
                result.succeeded = false;
                return false;
            }
            build.modules = std::move(optRes.modules);
        }

        auto stripSynthesizedSdkOrStubFunctions = [](const std::string& modName, auto& functions) {
            if (modName == "Aero.UI") {
                functions.erase(
                    std::remove_if(
                        functions.begin(),
                        functions.end(),
                        [](const auto& fn) {
                            const auto sep = fn.name.find("::");
                            const std::string_view local = (sep != std::string::npos)
                                ? std::string_view(fn.name).substr(sep + 2u)
                                : std::string_view(fn.name);
                            // Keep class methods and generated factory helpers, strip free native SDK stubs
                            if (str::StartsWith(local, "__AeroCreateInstance_") ||
                                local == "CreateMainWindow" ||
                                local == "CreateViewModel") {
                                return false;
                            }
                            return local.find('.') == std::string_view::npos;
                        }),
                    functions.end());
            } else {
                functions.erase(
                    std::remove_if(
                        functions.begin(),
                        functions.end(),
                        [](const auto& fn) {
                            return fn.name.find("__Aero_SetDataContext") != std::string::npos ||
                                   fn.name.find("Aero_SetDataContext__") != std::string::npos ||
                                   fn.name.find("GUI_SetDataContext__") != std::string::npos ||
                                   fn.name.find("Aero_LoadComponent__") != std::string::npos ||
                                   fn.name.find("GUI_LoadComponent__") != std::string::npos ||
                                   fn.name.find("Aero_LoadResources__") != std::string::npos;
                        }),
                    functions.end());
            }
        };

        rs_bc::Lowerer lowerer;
        for (const auto& mir : build.modules) {
            auto bc = lowerer.lower(mir);
            rs_diag::DiagnosticBag verifyDiag;
            if (!rs_bc::verifyModule(bc, verifyDiag)) {
                for (const auto& d : verifyDiag.items()) {
                    result.diagnostics.push_back(d.code + ": " + d.message);
                }
                result.succeeded = false;
                return false;
            }
            stripSynthesizedSdkOrStubFunctions(bc.name, bc.functions);
            for (auto& fref : bc.functionReferences) {
                const auto sep = fref.name.find("::");
                const std::string_view local = (sep != std::string::npos)
                    ? std::string_view(fref.name).substr(sep + 2u)
                    : std::string_view(fref.name);
                if (local == "Aero_LoadComponent" ||
                    local.rfind("Aero_LoadComponent__", 0) == 0 ||
                    local.rfind("GUI_LoadComponent__", 0) == 0) {
                    fref.name = "Aero.UI::GUI_LoadComponent";
                } else if (
                    local == "Aero_SetDataContext" ||
                    local == "__Aero_SetDataContext" ||
                    local.rfind("Aero_SetDataContext__", 0) == 0 ||
                    local.rfind("__Aero_SetDataContext__", 0) == 0 ||
                    local.rfind("GUI_SetDataContext__", 0) == 0) {
                    fref.name = "Aero.UI::GUI_SetDataContext";
                } else if (
                    local == "Aero_LoadResources" ||
                    local.rfind("Aero_LoadResources__", 0) == 0) {
                    fref.name = "Aero.UI::Application_LoadResources";
                }
            }
            result.bytecodeModules.push_back(bc);
        }

        RuntimeError linkErr{};
        auto linked = ProgramImage::link(result.bytecodeModules, linkErr);
        if (!linked) {
            result.diagnostics.push_back("Link error: " + linkErr.message);
            result.succeeded = false;
            return false;
        }
        result.programImage = std::make_shared<const ProgramImage>(std::move(*linked));

        if (!rootDataType.empty() && pkg.rootDataTypeName.empty()) {
            pkg.rootDataTypeName = rootDataType;
        }
        AeroCompiler::BindPackageAgainstProgram(pkg, *result.programImage, rootDataType);

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
                stripSynthesizedSdkOrStubFunctions(m.name, m.functions);
            }
            const auto generatedScriptAot = aotGen.generate(aotModules, aotDiag, genOpts);
            for (const auto& d : aotDiag.items()) {
                result.diagnostics.push_back(d.code + ": " + d.message);
            }
            if (aotDiag.hasErrors()) {
                result.succeeded = false;
                return false;
            }
            result.aotCpp = AeroCompiler::GenerateAotCppFromPackage(
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
    return true;
}

} // namespace

AeroCompileResult AeroCompiler::Compile(const AeroCompileInput& input) const {
    std::vector<std::string> activeIncludeStack;
    return CompileInternal(input, activeIncludeStack);
}

AeroCompileResult AeroCompiler::CompileInternal(
    const AeroCompileInput& input,
    std::vector<std::string>& activeIncludeStack) const {
    auto resultPtr = std::make_unique<AeroCompileResult>();
    CompileInternalBody(*this, input, activeIncludeStack, *resultPtr);
    return std::move(*resultPtr);
}

namespace {

AERO_NOINLINE void CompileInternalBody(
    const AeroCompiler& compiler,
    const AeroCompileInput& input,
    std::vector<std::string>& activeIncludeStack,
    AeroCompileResult& result) {
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
            return;
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
    if (!RunXamlPhaseNoInline(
            compiler,
            input,
            activeIncludeStack,
            effectiveXamlPath,
            loadedContentStorage,
            rootDataType,
            result)) {
        return;
    }

    if (!RunScriptSynthesisAndCompilationPhase(
            input,
            effectiveXamlPath,
            loadedContentStorage,
            rootDataType,
            result)) {
        return;
    }

    if (input.emitAotCpp && result.aotCpp.source.empty()) {
        result.aotCpp = AeroCompiler::GenerateAotCppFromPackage(
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
}

} // namespace

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

bool AttachDesktopStencilJit(
    ExecutionDomainContext& domains,
    realscript::jit::StencilModule* uiModule,
    realscript::jit::StencilModule* simModule) {
    const bool uiValid = (uiModule != nullptr && uiModule->valid());
    const bool simValid = (simModule != nullptr && simModule->valid());
    if (!uiValid && !simValid) {
        domains.clearDesktopJitInvoker();
        return false;
    }

    if (uiValid && !uiModule->heap()) {
        uiModule->setHeap(domains.uiHeapPtr());
    }
    if (simValid && !simModule->heap()) {
        simModule->setHeap(domains.simHeapPtr());
    }

    ExecutionDomainContext* domainsPtr = &domains;
    domains.setDesktopJitInvoker(
        [domainsPtr, uiModule, simModule](
            DomainKind domain,
            rs_sem::SymbolId functionId,
            const rs_rt::Value* arguments,
            std::size_t argumentCount,
            bool& outSucceeded,
            rs_rt::Value& outResult,
            rs_rt::RuntimeError& outError) -> bool {
            rs::jit::StencilModule* mod =
                (domain == DomainKind::SimDeterministic) ? simModule : uiModule;
            if (!mod || !mod->valid()) {
                return false;
            }
            if (mod->functionTier(functionId) == rs::jit::StencilTier::InterpreterFallback) {
                return false;
            }

            std::vector<rs_rt::Value> argVec;
            if (arguments != nullptr && argumentCount > 0u) {
                argVec.assign(arguments, arguments + argumentCount);
            }

            rs_rt::ExecutionOptions execOpts{};
            execOpts.limits.instructionBudget = 5'000'000;
            execOpts.executionPolicy = domainsPtr->executionPolicy(domain);
            const auto res = mod->invoke(functionId, argVec, std::move(execOpts));
            outSucceeded = res.succeeded;
            outResult = res.value;
            outError = res.error;
            return true;
        });
    return true;
}

} // namespace aero
