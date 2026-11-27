#include "XamlEmitterInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace aero::xaml_detail {

[[nodiscard]] std::uint64_t FastHashBytes64(const void* data,  std::size_t size) noexcept {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(size);
    const auto* ptr = static_cast<const unsigned char*>(data);
    std::size_t i = 0u;
    while (i + 8u <= size) {
        std::uint64_t word = 0u;
        std::memcpy(&word, ptr + i, sizeof(word));
        h ^= word;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= (h >> 30u);
        i += 8u;
    }
    if (i < size) {
        std::uint64_t tail = 0u;
        std::memcpy(&tail, ptr + i, size - i);
        h ^= tail;
        h *= 0x94D049BB133111EBULL;
        h ^= (h >> 31u);
    }
    return h;
}

[[nodiscard]] std::string ReadFileTextUtf8(
    const std::filesystem::path& path, 
    std::uint64_t* outRawHash) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        if (outRawHash) {
            *outRawHash = 0u;
        }
        return {};
    }
    const std::streampos sz = in.tellg();
    std::string text;
    if (sz > 0) {
        text.resize(static_cast<std::size_t>(sz));
        in.seekg(0, std::ios::beg);
        if (!in.read(text.data(), sz)) {
            if (outRawHash) {
                *outRawHash = 0u;
            }
            return {};
        }
    }
    if (outRawHash) {
        *outRawHash = FastHashBytes64(text.data(), text.size());
    }
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEFu &&
        static_cast<unsigned char>(text[1]) == 0xBBu &&
        static_cast<unsigned char>(text[2]) == 0xBFu) {
        text.erase(0, 3);
    }
    return text;
}

void ExtractKeysFromExtension(
    const MarkupExtensionNode& ext, 
    std::unordered_set<std::string>& usedTypes, 
    std::unordered_set<std::string>& liveKeys) {
    if (ext.kind == MarkupExtensionKind::StaticResource ||
        ext.kind == MarkupExtensionKind::DynamicResource) {
        const std::string rk = ResolveResourceKeyFromExtension(ext);
        if (!rk.empty()) {
            liveKeys.insert(rk);
            const auto* p0 = ext.FindPositional(0);
            const auto* rka = ext.FindNamed("ResourceKey");
            const MarkupArgValue* av = p0 ? p0 : rka;
            if (av && av->isExtension && av->extension &&
                av->extension->kind == MarkupExtensionKind::XType) {
                usedTypes.insert(rk);
            }
        }
    } else if (ext.kind == MarkupExtensionKind::XType) {
        if (const auto* p0 = ext.FindPositional(0)) {
            const std::string t = StripTargetTypeName(p0->literal);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
        } else if (const auto* tn = ext.FindNamed("TypeName")) {
            const std::string t = StripTargetTypeName(tn->literal);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
        }
    }
    for (const auto& arg : ext.arguments) {
        if (arg.value.isExtension && arg.value.extension) {
            ExtractKeysFromExtension(*arg.value.extension, usedTypes, liveKeys);
        } else if (!arg.value.literal.empty() && LooksLikeMarkupExtension(arg.value.literal)) {
            const auto sub = ParseMarkupExtension(arg.value.literal);
            if (sub.IsOk()) {
                ExtractKeysFromExtension(sub, usedTypes, liveKeys);
            }
        }
    }
}

void ScanXamlSubtreeForSeeds(
    const XamlNode& node, 
    std::unordered_set<std::string>& usedTypes, 
    std::unordered_set<std::string>& liveKeys, 
    bool skipDefinitionOwnKey, 
    bool skipExternalSourceDicts) {
    if (skipExternalSourceDicts && node.localName == "ResourceDictionary" &&
        node.FindAttribute("Source") != nullptr) {
        return;
    }

    if (!node.localName.empty() && node.localName.find('.') == std::string::npos) {
        usedTypes.insert(node.localName);
        if (node.localName == "TransitionContentPresenter") {
            usedTypes.insert("ContentPresenter");
        } else if (node.localName == "RichText") {
            usedTypes.insert("TextBlock");
        }
    }

    for (const auto& attr : node.attributes) {
        if (attr.isXmlns || IsDesignOrMarkupCompatPrefix(attr.prefix, attr.namespaceUri)) {
            continue;
        }
        if (skipDefinitionOwnKey && attr.prefix == "x" && attr.localName == "Key") {
            continue;
        }
        if (attr.localName == "TargetType" || attr.localName == "DataType" ||
            attr.localName == "AncestorType") {
            const std::string t = StripTargetTypeName(attr.value);
            if (!t.empty()) {
                usedTypes.insert(t);
                liveKeys.insert(t);
            }
            continue;
        }
        if (LooksLikeMarkupExtension(attr.value)) {
            const auto ext = ParseMarkupExtension(attr.value);
            if (ext.IsOk()) {
                ExtractKeysFromExtension(ext, usedTypes, liveKeys);
            }
            continue;
        }
        if (node.localName == "StaticResource" &&
            (attr.localName == "ResourceKey" || attr.localName == "Key")) {
            const std::string val = StripTargetTypeName(attr.value);
            if (!val.empty()) {
                liveKeys.insert(val);
            }
            continue;
        }
        if (attr.localName == "BasedOn" || attr.localName == "Style" ||
            attr.localName == "Template" || attr.localName == "ItemContainerStyle" ||
            attr.localName == "ItemTemplate" || attr.localName == "ContentTemplate" ||
            attr.localName == "ItemsPanel" || attr.localName == "Storyboard") {
            const std::string val = StripTargetTypeName(attr.value);
            if (!val.empty()) {
                liveKeys.insert(val);
            }
        }
    }

    for (const auto& ch : node.children) {
        if (ch && !ShouldIgnoreXmlElement(*ch)) {
            ScanXamlSubtreeForSeeds(*ch, usedTypes, liveKeys, false, skipExternalSourceDicts);
        }
    }
    for (const auto& [k, prop] : node.propertyElements) {
        (void)k;
        if (prop) {
            ScanXamlSubtreeForSeeds(*prop, usedTypes, liveKeys, false, skipExternalSourceDicts);
        }
    }
}

void ScanHintTextForSeeds(
    std::string_view text, 
    std::unordered_set<std::string>& usedTypes, 
    std::unordered_set<std::string>& liveKeys) {
    const std::size_t n = text.size();
    std::size_t i = 0u;
    while (i < n) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isalpha(ch) || ch == '_') {
            const std::size_t start = i++;
            while (i < n) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (std::isalnum(c) || c == '_') {
                    ++i;
                } else {
                    break;
                }
            }
            const std::string tok(text.substr(start, i - start));
            usedTypes.insert(tok);
            liveKeys.insert(tok);
            continue;
        }
        if (ch == '"') {
            std::string lit;
            ++i;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1u < n) {
                    ++i;
                }
                lit.push_back(text[i++]);
            }
            if (i < n && text[i] == '"') {
                ++i;
            }
            if (!lit.empty()) {
                liveKeys.insert(lit);
                if (LooksLikeMarkupExtension(lit)) {
                    const auto ext = ParseMarkupExtension(lit);
                    if (ext.IsOk()) {
                        ExtractKeysFromExtension(ext, usedTypes, liveKeys);
                    }
                }
            }
            continue;
        }
        if (ch == '{') {
            int depth = 1;
            std::size_t j = i + 1u;
            const std::size_t limit = std::min(n, i + 256u);
            while (j < limit && depth > 0 && text[j] != '\n' && text[j] != '\r') {
                if (text[j] == '{') ++depth;
                else if (text[j] == '}') --depth;
                ++j;
            }
            if (depth == 0) {
                const std::string_view snippet = text.substr(i, j - i);
                if (LooksLikeMarkupExtension(snippet)) {
                    const auto ext = ParseMarkupExtension(snippet);
                    if (ext.IsOk()) {
                        ExtractKeysFromExtension(ext, usedTypes, liveKeys);
                    }
                }
            }
        }
        ++i;
    }
}

void XamlEmitter::RecordExternalDependency(std::string_view normPath,  std::uint64_t contentHash) {
    if (normPath.empty()) {
        return;
    }
    const std::string key(normPath);
    if (recordedExternalDeps_.insert(key).second) {
        externalDependencies_.push_back(ExternalXamlDep{key, contentHash});
    }
}

[[nodiscard]] const XamlEmitter::ParsedExternalDoc* XamlEmitter::ResolveAndLoadExternalDoc(
    std::string_view requestedSource, 
    std::string_view currentXamlPath, 
    bool reportErrors) {
    const std::string cleanRel = NormalizePackSourcePath(requestedSource);
    if (cleanRel.empty()) {
        return nullptr;
    }

    const std::string pairKey = std::string(currentXamlPath) + "|" + cleanRel;
    if (const auto lookupIt = externalPathLookup_.find(pairKey); lookupIt != externalPathLookup_.end()) {
        if (const auto docIt = externalDocCache_.find(lookupIt->second); docIt != externalDocCache_.end()) {
            return &docIt->second;
        }
    }

    std::string resolvedPath;
    std::string xamlContent;
    std::uint64_t rawHash = 0u;
    bool loaded = false;

    if (opts_.externalXamlResolver) {
        loaded = opts_.externalXamlResolver(cleanRel, currentXamlPath, resolvedPath, xamlContent);
        if (loaded) {
            rawHash = FastHashBytes64(xamlContent.data(), xamlContent.size());
        }
    }
    if (!loaded) {
        std::vector<std::filesystem::path> candidates;
        const std::filesystem::path curDir = std::filesystem::path(currentXamlPath).parent_path();
        if (!curDir.empty()) {
            candidates.push_back(curDir / cleanRel);
        }
        if (!opts_.resourceSearchRoot.empty()) {
            candidates.push_back(std::filesystem::path(opts_.resourceSearchRoot) / cleanRel);
        }
        for (const auto& sp : opts_.resourceSearchPaths) {
            candidates.push_back(std::filesystem::path(sp) / cleanRel);
        }
        candidates.push_back(std::filesystem::path(cleanRel));
        // Fallback for packaged NoesisTheme references (`Theme/NoesisTheme.*.xaml`)
        if (aero::str::StartsWith(cleanRel, "Theme/")) {
            std::filesystem::path probe = curDir;
            for (int up = 0; up < 4 && !probe.empty(); ++up) {
                candidates.push_back(probe / "Theme" / "Data" / cleanRel);
                candidates.push_back(probe / "Samples" / "Theme" / "Data" / cleanRel);
                probe = probe.parent_path();
            }
            candidates.push_back(std::filesystem::path("Samples/Theme/Data") / cleanRel);
        }

        std::error_code ec;
        for (const auto& cand : candidates) {
            if (std::filesystem::is_regular_file(cand, ec)) {
                resolvedPath = cand.lexically_normal().generic_string();
                if (const auto existingIt = externalDocCache_.find(resolvedPath);
                    existingIt != externalDocCache_.end()) {
                    externalPathLookup_[pairKey] = resolvedPath;
                    return &existingIt->second;
                }
                xamlContent = ReadFileTextUtf8(cand, &rawHash);
                loaded = !xamlContent.empty();
                if (loaded) {
                    break;
                }
            }
        }
    }

    if (!loaded) {
        if (reportErrors) {
            AddError(
                "Could not resolve MergedDictionary Source '" + std::string(requestedSource) +
                "' referenced from '" + std::string(currentXamlPath) + "'");
        }
        return nullptr;
    }
    if (resolvedPath.empty()) {
        resolvedPath = cleanRel;
    }
    externalPathLookup_[pairKey] = resolvedPath;
    if (const auto existingIt = externalDocCache_.find(resolvedPath);
        existingIt != externalDocCache_.end()) {
        return &existingIt->second;
    }

    std::vector<std::string> tempDiag;
    std::unique_ptr<XamlNode> mergedRoot;
    if (!BuildXamlDom(xamlContent, mergedRoot, reportErrors ? diagnostics_ : tempDiag) || !mergedRoot) {
        if (reportErrors) {
            hasErrors_ = true;
        }
        return nullptr;
    }
    RecordExternalDependency(resolvedPath, rawHash);
    ParsedExternalDoc doc;
    doc.resolvedPath = resolvedPath;
    doc.hash = rawHash;
    doc.root = std::move(mergedRoot);
    const auto [insertedIt, unused] = externalDocCache_.emplace(resolvedPath, std::move(doc));
    (void)unused;
    return &insertedIt->second;
}

[[nodiscard]] bool XamlEmitter::IsThemeResourceDictPath(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    return path.find("NoesisTheme") != std::string_view::npos ||
           path.find("/Theme/") != std::string_view::npos ||
           path.find("\\Theme\\") != std::string_view::npos ||
           path.find("/Themes/") != std::string_view::npos ||
           path.find("\\Themes\\") != std::string_view::npos;
}

[[nodiscard]] bool XamlEmitter::HasDynamicXamlLoadingHint(
    const std::vector<std::string>& hints) noexcept {
    for (const std::string& hint : hints) {
        if (hint.find("XamlReader.Load") != std::string::npos ||
            hint.find("XamlReader.Parse") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void XamlEmitter::CollectExternalDictNodes(
    const XamlNode& dictNode, 
    std::string_view currentXamlPath) {
    if (const auto* rootSrc = dictNode.FindAttribute("Source")) {
        CollectExternalDictInclude(rootSrc->value, currentXamlPath);
    }
    if (const auto* mergedEl = dictNode.FindPropertyElement("ResourceDictionary", "MergedDictionaries")) {
        for (const auto& child : mergedEl->children) {
            if (!child || ShouldIgnoreXmlElement(*child)) continue;
            if (child->localName == "ResourceDictionary") {
                if (const auto* srcAttr = child->FindAttribute("Source")) {
                    CollectExternalDictInclude(srcAttr->value, currentXamlPath);
                } else {
                    CollectExternalDictNodes(*child, currentXamlPath);
                }
            }
        }
    }
    for (const auto& child : dictNode.children) {
        if (!child || ShouldIgnoreXmlElement(*child)) continue;
        if (child->localName == "ResourceDictionary") {
            if (const auto* srcAttr = child->FindAttribute("Source")) {
                CollectExternalDictInclude(srcAttr->value, currentXamlPath);
            } else {
                CollectExternalDictNodes(*child, currentXamlPath);
            }
            continue;
        }
        if (externalDictDepth_ > 0) {
            if (IsThemeResourceDictPath(currentXamlPath) &&
                IsPrunableExternalResourceTag(child->localName)) {
                if (externalPrunableCandidates_.insert(child.get()).second) {
                    externalPrunableList_.push_back(child.get());
                }
            } else {
                externalNonPrunableNodes_.push_back(child.get());
            }
        }
    }
}

void XamlEmitter::CollectExternalDictInclude(
    std::string_view requestedSource, 
    std::string_view currentXamlPath) {
    const ParsedExternalDoc* doc = ResolveAndLoadExternalDoc(requestedSource, currentXamlPath, false);
    if (!doc || !doc->root) {
        return;
    }
    if (!visitedExternalXamls_.insert(doc->resolvedPath).second) {
        return;
    }
    ++externalDictDepth_;
    CollectExternalDictNodes(*doc->root, doc->resolvedPath);
    --externalDictDepth_;
    visitedExternalXamls_.erase(doc->resolvedPath);
}

void XamlEmitter::WalkTreeForExternalDictRefs(const XamlNode& node,  std::string_view currentXamlPath) {
    if (node.localName == "ResourceDictionary") {
        if (const auto* src = node.FindAttribute("Source")) {
            CollectExternalDictInclude(src->value, currentXamlPath);
        }
    }
    for (const auto& ch : node.children) {
        if (ch && !ShouldIgnoreXmlElement(*ch)) {
            WalkTreeForExternalDictRefs(*ch, currentXamlPath);
        }
    }
    for (const auto& [k, prop] : node.propertyElements) {
        (void)k;
        if (prop) {
            WalkTreeForExternalDictRefs(*prop, currentXamlPath);
        }
    }
}

void XamlEmitter::PrecomputeLiveExternalStyleNodes(
    const XamlNode& primaryRoot, 
    std::string_view currentXamlPath) {
    externalPrunableList_.clear();
    externalPrunableCandidates_.clear();
    liveExternalNodes_.clear();
    externalNonPrunableNodes_.clear();
    hasLiveExternalFilter_ = false;

    if (HasDynamicXamlLoadingHint(opts_.usageHintTexts)) {
        return;
    }

    WalkTreeForExternalDictRefs(primaryRoot, currentXamlPath);
    if (externalPrunableList_.empty()) {
        return;
    }

    hasLiveExternalFilter_ = true;
    std::unordered_set<std::string> usedTypes;
    std::unordered_set<std::string> liveKeys;

    ScanXamlSubtreeForSeeds(primaryRoot, usedTypes, liveKeys, false, true);
    for (const XamlNode* nonPrune : externalNonPrunableNodes_) {
        if (nonPrune) {
            ScanXamlSubtreeForSeeds(*nonPrune, usedTypes, liveKeys, false, false);
        }
    }
    for (const std::string& hint : opts_.usageHintTexts) {
        ScanHintTextForSeeds(hint, usedTypes, liveKeys);
    }
    ExpandControlHierarchy(usedTypes);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const XamlNode* cand : externalPrunableList_) {
            if (!cand || liveExternalNodes_.count(cand) > 0u) {
                continue;
            }
            const std::string rawKey = cand->GetKey();
            bool isLive = false;
            if (!rawKey.empty()) {
                std::string effKey = rawKey;
                if (LooksLikeMarkupExtension(effKey)) {
                    effKey = NormalizeResourceKey(effKey);
                }
                if (liveKeys.count(rawKey) > 0u || liveKeys.count(effKey) > 0u) {
                    isLive = true;
                } else if (cand->localName == "Style") {
                    if (const auto* tt = cand->FindAttribute("TargetType")) {
                        const std::string ttName = StripTargetTypeName(tt->value);
                        if (!ttName.empty() && effKey == ttName && usedTypes.count(ttName) > 0u) {
                            isLive = true;
                        }
                    }
                }
            } else {
                if (cand->localName == "Style") {
                    if (const auto* tt = cand->FindAttribute("TargetType")) {
                        const std::string ttName = StripTargetTypeName(tt->value);
                        if (ttName.empty() || usedTypes.count(ttName) > 0u) {
                            isLive = true;
                        }
                    } else {
                        isLive = true;
                    }
                } else {
                    isLive = true;
                }
            }
            if (isLive) {
                liveExternalNodes_.insert(cand);
                ScanXamlSubtreeForSeeds(*cand, usedTypes, liveKeys, true, false);
                ExpandControlHierarchy(usedTypes);
                changed = true;
            }
        }
    }
}

[[nodiscard]] std::string XamlEmitter::EmitExternalDictionaryInclude(
    std::string_view requestedSource, 
    std::string_view currentXamlPath, 
    std::string_view targetDictVar, 
    std::ostringstream& out, 
    int indent) {
    const ParsedExternalDoc* doc = ResolveAndLoadExternalDoc(
        requestedSource, currentXamlPath, true);
    if (!doc || !doc->root) {
        return {};
    }
    if (!visitedExternalXamls_.insert(doc->resolvedPath).second) {
        AddError("Circular ResourceDictionary Source detected: " + doc->resolvedPath);
        return {};
    }
    ++externalDictDepth_;
    EmitResourceDictionaryEntries(*doc->root, doc->resolvedPath, targetDictVar, out, indent);
    --externalDictDepth_;
    visitedExternalXamls_.erase(doc->resolvedPath);
    return doc->resolvedPath;
}

void XamlEmitter::EmitResourceDictionaryEntries(
    const XamlNode& dictNode, 
    std::string_view currentXamlPath, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent) {
    if (const auto* rootSrc = dictNode.FindAttribute("Source")) {
        (void)EmitExternalDictionaryInclude(
            rootSrc->value, currentXamlPath, dictVar, out, indent);
    }
    if (const auto* mergedEl = dictNode.FindPropertyElement("ResourceDictionary", "MergedDictionaries")) {
        for (const auto& child : mergedEl->children) {
            if (ShouldIgnoreXmlElement(*child)) {
                continue;
            }
            if (child->localName == "ResourceDictionary") {
                if (const auto* srcAttr = child->FindAttribute("Source")) {
                    (void)EmitExternalDictionaryInclude(
                        srcAttr->value, currentXamlPath, dictVar, out, indent);
                } else {
                    EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
                }
            }
        }
    }

    for (const auto& child : dictNode.children) {
        if (ShouldIgnoreXmlElement(*child)) {
            continue;
        }
        if (child->localName == "ResourceDictionary") {
            if (const auto* srcAttr = child->FindAttribute("Source")) {
                (void)EmitExternalDictionaryInclude(
                    srcAttr->value, currentXamlPath, dictVar, out, indent);
            } else {
                EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
            }
            continue;
        }
        EmitSingleDirectResource(*child, dictVar, out, indent);
    }
}

void XamlEmitter::EmitStandaloneResourceDictionary(
    const XamlNode& root, 
    std::string_view currentXamlPath, 
    std::ostringstream& out) {
    inPartialClass_ = false;
    out << "public class " << shortClassName_ << "\n{\n";
    Indent(out, 1);
    out << "public static void Register(ResourceDictionary dict)\n";
    Indent(out, 1);
    out << "{\n";
    Indent(out, 2);
    out << "if (dict == null) { return; }\n";
    EmitResourceDictionaryEntries(root, currentXamlPath, "dict", out, 2);
    Indent(out, 1);
    out << "}\n\n";
    Indent(out, 1);
    out << "public static ResourceDictionary Build()\n";
    Indent(out, 1);
    out << "{\n";
    Indent(out, 2);
    out << "var dict = new ResourceDictionary();\n";
    Indent(out, 2);
    out << "Register(dict);\n";
    Indent(out, 2);
    out << "return dict;\n";
    Indent(out, 1);
    out << "}\n\n";
    Indent(out, 1);
    out << "public " << shortClassName_ << "()\n";
    Indent(out, 1);
    out << "{\n";
    Indent(out, 2);
    out << "Register(ResourceDictionary.GlobalApplicationResources());\n";
    Indent(out, 1);
    out << "}\n}\n";
}

bool XamlEmitter::EmitResourcesPropertyElement(
    const XamlNode& ownerNode, 
    std::string_view ownerType, 
    std::string_view ownerVar, 
    std::string_view currentXamlPath, 
    std::ostringstream& out, 
    int indent) {
    const XamlNode* resProp = ownerNode.FindPropertyElement(ownerType, "Resources");
    if (resProp == nullptr) {
        resProp = ownerNode.FindPropertyElement("FrameworkElement", "Resources");
    }
    if (resProp == nullptr) {
        return false;
    }

    const std::string dictVar = std::string(ownerVar) + ".Resources";
    Indent(out, indent);
    out << "ResourceDictionary.PushAmbient(" << dictVar << ");\n";
    for (const auto& child : resProp->children) {
        if (ShouldIgnoreXmlElement(*child)) {
            continue;
        }
        if (child->localName == "ResourceDictionary") {
            EmitResourceDictionaryEntries(*child, currentXamlPath, dictVar, out, indent);
        } else {
            EmitSingleDirectResource(*child, dictVar, out, indent);
        }
    }
    return true;
}

void XamlEmitter::EmitSingleDirectResource(
    const XamlNode& child, 
    std::string_view dictVar, 
    std::ostringstream& out, 
    int indent) {
    if (hasLiveExternalFilter_ &&
        externalDictDepth_ > 0 &&
        externalPrunableCandidates_.count(&child) > 0u &&
        liveExternalNodes_.count(&child) == 0u) {
        return;
    }
    std::string key = NormalizeResourceKey(child.GetKey());
    if (child.localName == "Style" && key.empty()) {
        if (const auto* tt = child.FindAttribute("TargetType")) {
            key = StripTargetTypeName(tt->value);
        }
    } else if ((child.localName == "DataTemplate" || child.localName == "HierarchicalDataTemplate") && key.empty()) {
        if (const auto* dt = child.FindAttribute("DataType")) {
            key = StripTargetTypeName(dt->value);
        } else if (const auto* xdt = child.FindXAttribute("DataType")) {
            key = StripTargetTypeName(xdt->value);
        }
    }
    if (key.empty()) {
        return;
    }
    if (child.localName == "Color") {
        const std::string hex = std::string(aero::str::Trim(child.textContent));
        knownColors_[key] = hex;
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
            << EscapeRsString(hex) << "));\n";
    } else if (IsXamlBrushElementTag(child.localName)) {
        const std::string expr = EmitBrushObject(child, {}, dictVar, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << expr << ");\n";
    } else if (child.localName == "Style") {
        const std::string v = EmitStyle(child, dictVar, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
    } else if (child.localName == "ControlTemplate") {
        const std::string v = EmitControlTemplate(child, dictVar, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
    } else if (child.localName == "DataTemplate" || child.localName == "HierarchicalDataTemplate") {
        const std::string v = EmitDataTemplate(child, dictVar, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
    } else if (child.localName == "ItemsPanelTemplate") {
        const std::string v = EmitItemsPanelTemplate(child, dictVar, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
    } else if (child.localName == "Storyboard") {
        knownStoryboards_[key] = &child;
        const std::string v = EmitStoryboard(child, out, indent, dictVar);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << v << ");\n";
    } else if (child.localName == "Thickness") {
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", "
            << FormatThicknessExpr(child.textContent) << ");\n";
    } else if (child.localName == "CornerRadius") {
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", "
            << FormatCornerRadiusExpr(child.textContent) << ");\n";
    } else if (child.localName == "Double" || child.localName == "Single") {
        float val = 0.0f;
        (void)aero::str::TryParseFloat32(child.textContent, val);
        knownDoubles_[key] = val;
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxFloat("
            << FormatFloatLiteral(val) << "));\n";
    } else if (child.localName == "Int32" || child.localName == "Int64") {
        const long iv = std::strtol(std::string(child.textContent).c_str(), nullptr, 10);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxInt(" << iv << "));\n";
    } else if (child.localName == "Boolean") {
        const bool bv = aero::str::EqualsIgnoreCase(aero::str::Trim(child.textContent), "True");
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxBool("
            << (bv ? "true" : "false") << "));\n";
    } else if (child.localName == "String" || child.localName == "FontFamily" ||
               child.localName == "FlowDirection") {
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
            << EscapeRsString(aero::str::Trim(child.textContent)) << "));\n";
    } else if (child.localName == "BitmapImage") {
        std::string uri;
        if (const auto* u = child.FindAttribute("UriSource")) uri = u->value;
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
            << EscapeRsString(uri) << "));\n";
    } else if (child.localName == "ImageSource") {
        const std::string uri = std::string(aero::str::Trim(child.textContent));
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
            << EscapeRsString(uri) << "));\n";
    } else if (child.localName == "StreamGeometry" ||
               child.localName == "PathGeometry" ||
               child.localName == "Geometry") {
        const std::string geom = (child.localName == "PathGeometry")
            ? ConvertPathGeometryToSvgData(child)
            : std::string(aero::str::Trim(child.textContent));
        knownGeometries_[key] = geom;
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", DependencyObject.BoxString("
            << EscapeRsString(geom) << "));\n";
    } else if (child.localName == "BlurEffect" || child.localName == "DropShadowEffect" ||
               child.localName == "BackdropBlurEffect" || child.localName == "TintEffect" ||
               child.localName == "PixelateEffect" || child.localName == "ChromaticAberrationEffect" ||
               child.localName == "DirectionalBlurEffect" || child.localName == "SaturationEffect") {
        const std::string efVar = EmitEffectObject(child, {}, out, indent);
        if (!efVar.empty()) {
            Indent(out, indent);
            out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << efVar << ");\n";
        }
    } else if (child.localName == "BoolToVisibilityConverter" ||
               child.localName == "BooleanToVisibilityConverter" ||
               child.localName == "InverseBoolConverter" ||
               child.localName == "NullToVisibilityConverter" ||
               child.localName == "AlternationConverter" ||
               child.localName == "MathConverter") {
        const std::string typeName = (child.localName == "BooleanToVisibilityConverter")
            ? std::string("BoolToVisibilityConverter")
            : child.localName;
        const std::string cVar = FreshVar("conv");
        Indent(out, indent);
        out << "var " << cVar << " = new " << typeName << "();\n";
        if (typeName == "MathConverter") {
            if (const auto* ex = child.FindAttribute("Expression")) {
                Indent(out, indent);
                out << cVar << ".Expression = " << EscapeRsString(ex->value) << ";\n";
            }
        }
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << cVar << ");\n";
    } else if (child.localName == "GradientStopCollection") {
        const std::string gscVar = FreshVar("gsc");
        Indent(out, indent);
        out << "var " << gscVar << " = new ArrayCollectionBox();\n";
        for (const auto& stop : child.children) {
            if (!stop || stop->localName != "GradientStop") {
                continue;
            }
            float offset = 0.0f;
            if (const auto* oAttr = stop->FindAttribute("Offset")) {
                (void)aero::str::TryParseFloat32(oAttr->value, offset);
            }
            std::string color = "#00000000";
            if (const auto* cAttr = stop->FindAttribute("Color")) {
                if (!LooksLikeMarkupExtension(cAttr->value)) {
                    color = ResolveColorHexOrString(cAttr->value);
                }
            }
            Indent(out, indent);
            out << gscVar << ".AddBox(new GradientStop(" << EscapeRsString(color)
                << ", " << FormatFloatLiteral(offset) << "));\n";
        }
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << gscVar << ");\n";
    } else if (IsKnownUIElementType(child.localName)) {
        const std::string elVar = EmitConstructedElement(
            child, {}, dictVar, opts_.xamlPath, out, indent);
        Indent(out, indent);
        out << dictVar << ".SetBox(" << EscapeRsString(key) << ", " << elVar << ");\n";
    }
}

} // namespace aero::xaml_detail
