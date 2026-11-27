#include "aero/visual/NativeXmlDomBridge.hpp"

#include "aero/core/StringUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

namespace {

struct FastXmlAttr {
    std::string name;
    std::string value;
};

struct FastXmlNode {
    std::string rawName;
    std::string localName;
    std::string text;
    std::vector<FastXmlAttr> attrs;
    std::int32_t parentId = -1;
    std::int32_t childCount = 0;
    bool lastChildIsLineBreak = false;
    bool hasPrefix = false;
};

struct FastXmlScratch {
    std::vector<FastXmlNode> nodes;
};

[[nodiscard]] FastXmlScratch& ThreadXmlScratch() noexcept {
    static thread_local FastXmlScratch tlsXmlScratch;
    return tlsXmlScratch;
}

[[nodiscard]] constexpr bool IsXmlSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] constexpr std::string_view TrimXmlView(std::string_view sv) noexcept {
    std::size_t b = 0u;
    std::size_t e = sv.size();
    while (b < e && IsXmlSpace(sv[b])) {
        ++b;
    }
    while (e > b && IsXmlSpace(sv[e - 1u])) {
        --e;
    }
    return sv.substr(b, e - b);
}

[[nodiscard]] constexpr std::string_view StripPrefixView(
    std::string_view sv,
    bool& outHasPrefix) noexcept {
    const std::size_t colon = sv.rfind(':');
    if (colon != std::string_view::npos && colon + 1u < sv.size()) {
        outHasPrefix = true;
        return sv.substr(colon + 1u);
    }
    outHasPrefix = false;
    return sv;
}

[[nodiscard]] std::int64_t ArgXmlInt(ValueSpan args, std::size_t idx) noexcept {
    if (idx >= args.size()) {
        return 0;
    }
    std::int64_t iv = 0;
    if (args[idx].tryGetInt64(iv)) {
        return iv;
    }
    EnumValue ev{};
    if (args[idx].tryGetEnum(ev)) {
        return ev.value;
    }
    return 0;
}

[[nodiscard]] std::string DecodeXmlEntities(std::string_view raw) {
    const std::size_t firstAmp = raw.find('&');
    if (firstAmp == std::string_view::npos) {
        return std::string(raw);
    }
    std::string out;
    out.reserve(raw.size());
    std::size_t pos = 0u;
    while (pos < raw.size()) {
        const std::size_t amp = raw.find('&', pos);
        if (amp == std::string_view::npos) {
            out.append(raw.data() + pos, raw.size() - pos);
            break;
        }
        out.append(raw.data() + pos, amp - pos);
        const std::size_t semi = raw.find(';', amp + 1u);
        if (semi != std::string_view::npos && semi > amp + 1u && semi - amp <= 12u) {
            const std::string_view ent = raw.substr(amp + 1u, semi - (amp + 1u));
            if (ent == "lt") { out.push_back('<'); pos = semi + 1u; continue; }
            if (ent == "gt") { out.push_back('>'); pos = semi + 1u; continue; }
            if (ent == "quot") { out.push_back('"'); pos = semi + 1u; continue; }
            if (ent == "apos") { out.push_back('\''); pos = semi + 1u; continue; }
            if (ent == "amp") { out.push_back('&'); pos = semi + 1u; continue; }
            if (ent.size() > 1u && ent[0] == '#') {
                std::uint32_t cp = 0u;
                bool valid = true;
                if (ent.size() > 2u && (ent[1] == 'x' || ent[1] == 'X')) {
                    for (std::size_t k = 2u; k < ent.size(); ++k) {
                        const char hc = ent[k];
                        std::uint32_t d = 16u;
                        if (hc >= '0' && hc <= '9') d = static_cast<std::uint32_t>(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') d = static_cast<std::uint32_t>(10 + (hc - 'a'));
                        else if (hc >= 'A' && hc <= 'F') d = static_cast<std::uint32_t>(10 + (hc - 'A'));
                        if (d >= 16u) { valid = false; break; }
                        cp = cp * 16u + d;
                        if (cp > 0x10FFFFu) { valid = false; break; }
                    }
                } else {
                    for (std::size_t k = 1u; k < ent.size(); ++k) {
                        const char dc = ent[k];
                        if (dc < '0' || dc > '9') { valid = false; break; }
                        cp = cp * 10u + static_cast<std::uint32_t>(dc - '0');
                        if (cp > 0x10FFFFu) { valid = false; break; }
                    }
                }
                if (valid && cp > 0u && cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                    str::AppendUtf8CodePoint(out, cp);
                    pos = semi + 1u;
                    continue;
                }
            }
        }
        out.push_back('&');
        pos = amp + 1u;
    }
    return out;
}

std::int64_t ParseXmlIntoScratch(std::string_view src) {
    auto& scratch = ThreadXmlScratch();
    scratch.nodes.clear();

    if (src.size() >= 3u &&
        static_cast<unsigned char>(src[0]) == 0xEFu &&
        static_cast<unsigned char>(src[1]) == 0xBBu &&
        static_cast<unsigned char>(src[2]) == 0xBFu) {
        src.remove_prefix(3u);
    }
    const std::size_t firstLt = src.find('<');
    if (firstLt == std::string_view::npos) {
        return 0;
    }
    src.remove_prefix(firstLt);
    scratch.nodes.reserve(std::max<std::size_t>(32u, src.size() / 64u));

    auto appendChildNode = [&](std::int32_t parentId, std::string raw, std::string local, bool hasPfx, std::string txt) -> std::int32_t {
        const auto id = static_cast<std::int32_t>(scratch.nodes.size());
        FastXmlNode n;
        n.rawName = std::move(raw);
        n.localName = std::move(local);
        n.text = std::move(txt);
        n.parentId = parentId;
        n.hasPrefix = hasPfx;
        if (parentId >= 0 && static_cast<std::size_t>(parentId) < scratch.nodes.size()) {
            auto& p = scratch.nodes[static_cast<std::size_t>(parentId)];
            p.childCount += 1;
            p.lastChildIsLineBreak = (n.localName == "LineBreak");
        }
        scratch.nodes.push_back(std::move(n));
        return id;
    };

    auto appendTextSegment = [&](std::int32_t parentId, std::string_view rawSeg) {
        if (parentId < 0 || rawSeg.empty()) {
            return;
        }
        std::string decoded = DecodeXmlEntities(rawSeg);
        if (decoded.empty()) {
            return;
        }
        const std::string_view tr = TrimXmlView(decoded);
        const std::string_view pLocal = scratch.nodes[static_cast<std::size_t>(parentId)].localName;
        const bool isInline = (pLocal == "TextBlock" || pLocal == "Span" || pLocal == "Bold" ||
                               pLocal == "Italic" || pLocal == "Underline" || pLocal == "Hyperlink");
        if (isInline) {
            if (tr.empty()) {
                (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
            } else {
                const bool hasLead = IsXmlSpace(decoded.front());
                const bool hasTrail = IsXmlSpace(decoded.back());
                const auto& pRef = scratch.nodes[static_cast<std::size_t>(parentId)];
                if (hasLead && pRef.childCount > 0 && !pRef.lastChildIsLineBreak) {
                    (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
                }
                (void)appendChildNode(parentId, "#text", "#text", false, std::string(tr));
                scratch.nodes[static_cast<std::size_t>(parentId)].text += decoded;
                if (hasTrail) {
                    (void)appendChildNode(parentId, "#ws", "#ws", false, std::string{});
                }
            }
            return;
        }
        if (!tr.empty()) {
            if (pLocal == "ItemsControl" || pLocal == "ListBox" || pLocal == "ListView" || pLocal == "ComboBox") {
                (void)appendChildNode(parentId, "#text", "#text", false, std::string(tr));
            }
            scratch.nodes[static_cast<std::size_t>(parentId)].text += decoded;
        }
    };

    std::vector<std::int32_t> stack;
    stack.reserve(32u);
    std::size_t maxAttrs = 0u;
    std::size_t pos = 0u;
    const std::size_t len = src.size();
    while (pos < len) {
        if (scratch.nodes.size() >= 262143u || maxAttrs >= 1024u) {
            std::vector<FastXmlNode>().swap(scratch.nodes);
            return -1;
        }
        const std::size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) {
            if (!stack.empty() && pos < len) {
                appendTextSegment(stack.back(), src.substr(pos));
            }
            break;
        }
        if (lt > pos && !stack.empty()) {
            appendTextSegment(stack.back(), src.substr(pos, lt - pos));
        }
        if (lt + 1u >= len) {
            break;
        }

        if (lt + 4u <= len && src.substr(lt, 4u) == "<!--") {
            const std::size_t endCom = src.find("-->", lt + 4u);
            if (endCom == std::string_view::npos) break;
            pos = endCom + 3u;
            continue;
        }
        if (lt + 9u <= len && src.substr(lt, 9u) == "<![CDATA[") {
            const std::size_t endCd = src.find("]]>", lt + 9u);
            if (endCd == std::string_view::npos) break;
            if (!stack.empty()) {
                scratch.nodes[static_cast<std::size_t>(stack.back())].text.append(
                    src.data() + lt + 9u, endCd - (lt + 9u));
            }
            pos = endCd + 3u;
            continue;
        }
        const char c1 = src[lt + 1u];
        if (c1 == '?' || c1 == '!') {
            const std::size_t gt = src.find('>', lt + 2u);
            if (gt == std::string_view::npos) break;
            pos = gt + 1u;
            continue;
        }

        std::size_t gt = std::string_view::npos;
        char quote = '\0';
        for (std::size_t k = lt + 1u; k < len; ++k) {
            const char ch = src[k];
            if (quote != '\0') {
                if (ch == quote) quote = '\0';
            } else {
                if (ch == '"' || ch == '\'') quote = ch;
                else if (ch == '>') { gt = k; break; }
            }
        }
        if (gt == std::string_view::npos) {
            break;
        }

        std::string_view header = TrimXmlView(src.substr(lt + 1u, gt - (lt + 1u)));
        pos = gt + 1u;
        if (header.empty()) {
            continue;
        }

        if (header.front() == '/') {
            const std::string_view closeRaw = TrimXmlView(header.substr(1u));
            bool dummyPfx = false;
            const std::string_view closeLocal = StripPrefixView(closeRaw, dummyPfx);
            while (!stack.empty()) {
                const std::int32_t poppedId = stack.back();
                stack.pop_back();
                const auto& popped = scratch.nodes[static_cast<std::size_t>(poppedId)];
                if (popped.rawName == closeRaw || popped.localName == closeLocal) {
                    break;
                }
            }
            continue;
        }

        bool selfClosing = false;
        if (!header.empty() && header.back() == '/') {
            selfClosing = true;
            header = TrimXmlView(header.substr(0u, header.size() - 1u));
        }
        std::size_t nameEnd = 0u;
        while (nameEnd < header.size() && !IsXmlSpace(header[nameEnd])) {
            ++nameEnd;
        }
        const std::string_view rawNameSv = header.substr(0u, nameEnd);
        bool hasPfx = false;
        const std::string_view localNameSv = StripPrefixView(rawNameSv, hasPfx);
        const std::int32_t parentId = stack.empty() ? -1 : stack.back();
        const std::int32_t nodeId = appendChildNode(
            parentId, std::string(rawNameSv), std::string(localNameSv), hasPfx, std::string{});

        if (nameEnd < header.size()) {
            const std::string_view attrText = header.substr(nameEnd);
            std::size_t ai = 0u;
            const std::size_t alen = attrText.size();
            auto& attrVec = scratch.nodes[static_cast<std::size_t>(nodeId)].attrs;
            while (ai < alen) {
                while (ai < alen && IsXmlSpace(attrText[ai])) ++ai;
                if (ai >= alen) break;
                const std::size_t aStart = ai;
                while (ai < alen && attrText[ai] != '=' && !IsXmlSpace(attrText[ai])) ++ai;
                const std::string_view aName = TrimXmlView(attrText.substr(aStart, ai - aStart));
                const bool skipAttr =
                    aName.empty() ||
                    aName == "xmlns" ||
                    str::StartsWith(aName, "xmlns:") ||
                    str::StartsWith(aName, "mc:") ||
                    str::StartsWith(aName, "d:") ||
                    aName == "SnapsToDevicePixels" ||
                    aName == "UseLayoutRounding";
                while (ai < alen && IsXmlSpace(attrText[ai])) ++ai;
                if (ai >= alen || attrText[ai] != '=') {
                    if (!skipAttr) {
                        attrVec.push_back(FastXmlAttr{std::string(aName), std::string{}});
                    }
                    continue;
                }
                ++ai;
                while (ai < alen && IsXmlSpace(attrText[ai])) ++ai;
                if (ai >= alen) break;
                const char aq = attrText[ai];
                if (aq == '"' || aq == '\'') {
                    ++ai;
                    const std::size_t vStart = ai;
                    while (ai < alen && attrText[ai] != aq) ++ai;
                    if (!skipAttr) {
                        attrVec.push_back(FastXmlAttr{
                            std::string(aName),
                            DecodeXmlEntities(attrText.substr(vStart, ai - vStart))});
                    }
                    if (ai < alen) ++ai;
                } else {
                    const std::size_t vStart = ai;
                    while (ai < alen && !IsXmlSpace(attrText[ai])) ++ai;
                    if (!skipAttr) {
                        attrVec.push_back(FastXmlAttr{
                            std::string(aName),
                            DecodeXmlEntities(attrText.substr(vStart, ai - vStart))});
                    }
                }
            }
            maxAttrs = std::max(maxAttrs, attrVec.size());
        }

        if (!selfClosing) {
            stack.push_back(nodeId);
        }
    }

    if (scratch.nodes.size() >= 262143u || maxAttrs >= 1024u) {
        std::vector<FastXmlNode>().swap(scratch.nodes);
        return -1;
    }
    return static_cast<std::int64_t>(scratch.nodes.size());
}

} // namespace

bool RegisterNativeXmlDomBindings(
    BindingRegistry& bindings,
    const ScriptBackendView& backend) {
    auto bindVal = [&](const char* name, auto fn) -> bool {
        const std::string bare(name);
        const std::string canon = "Aero.Native::" + bare;
        return bindings.bind(canon, fn, rs_rt::BindingDeterminism::NonDeterministic) &&
               bindings.bind(bare, std::move(fn), rs_rt::BindingDeterminism::NonDeterministic);
    };
    auto bindVoid = [&](const char* name, auto fn) -> bool {
        const std::string bare(name);
        const std::string canon = "Aero.Native::" + bare;
        const std::string natBare = "__rs_native_" + bare;
        const std::string natCanon = "Aero.Native::__rs_native_" + bare;
        return bindings.bind(canon, fn, rs_rt::BindingDeterminism::NonDeterministic) &&
               bindings.bind(natCanon, fn, rs_rt::BindingDeterminism::NonDeterministic) &&
               bindings.bind(natBare, fn, rs_rt::BindingDeterminism::NonDeterministic) &&
               bindings.bind(bare, std::move(fn), rs_rt::BindingDeterminism::NonDeterministic);
    };

    bool ok = true;
    ok &= bindVal("Xml_ParseDocument", [backend](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        std::string scratch;
        const std::string_view xmlView = ExtractStringArgView(backend, args, 0u, scratch);
        return UnifiedValue{ParseXmlIntoScratch(xmlView)};
    });
    ok &= bindVoid("Xml_ClearDocument", [](const rs_bc::FunctionReference&, ValueSpan, RuntimeError&) -> std::optional<UnifiedValue> {
        std::vector<FastXmlNode>().swap(ThreadXmlScratch().nodes);
        return UnifiedValue{std::int64_t{0}};
    });
    ok &= bindVal("Xml_NodeMeta", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{std::int64_t{0}};
        }
        const auto& n = nodes[static_cast<std::size_t>(idx)];
        const std::int64_t parentPlus1 = static_cast<std::int64_t>(n.parentId + 1) & 0x3FFFFLL;
        const std::int64_t attrCount = (static_cast<std::int64_t>(n.attrs.size()) & 0x3FFLL) << 18u;
        const std::int64_t hasText = (!n.text.empty() ? 1LL : 0LL) << 28u;
        const std::int64_t hasPfx = (n.hasPrefix ? 1LL : 0LL) << 29u;
        return UnifiedValue{parentPlus1 | attrCount | hasText | hasPfx};
    });
    ok &= bindVal("Xml_NodeRawName", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        return UnifiedValue{StringValue{nodes[static_cast<std::size_t>(idx)].rawName}};
    });
    ok &= bindVal("Xml_NodeLocalName", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        return UnifiedValue{StringValue{nodes[static_cast<std::size_t>(idx)].localName}};
    });
    ok &= bindVal("Xml_NodeText", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        return UnifiedValue{StringValue{nodes[static_cast<std::size_t>(idx)].text}};
    });
    ok &= bindVal("Xml_AttrName", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        const auto aIdx = ArgXmlInt(args, 1u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        const auto& attrs = nodes[static_cast<std::size_t>(idx)].attrs;
        if (aIdx < 0 || static_cast<std::size_t>(aIdx) >= attrs.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        return UnifiedValue{StringValue{attrs[static_cast<std::size_t>(aIdx)].name}};
    });
    ok &= bindVal("Xml_AttrValue", [](const rs_bc::FunctionReference&, ValueSpan args, RuntimeError&) -> std::optional<UnifiedValue> {
        const auto& nodes = ThreadXmlScratch().nodes;
        const auto idx = ArgXmlInt(args, 0u);
        const auto aIdx = ArgXmlInt(args, 1u);
        if (idx < 0 || static_cast<std::size_t>(idx) >= nodes.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        const auto& attrs = nodes[static_cast<std::size_t>(idx)].attrs;
        if (aIdx < 0 || static_cast<std::size_t>(aIdx) >= attrs.size()) {
            return UnifiedValue{StringValue{std::string{}}};
        }
        return UnifiedValue{StringValue{attrs[static_cast<std::size_t>(aIdx)].value}};
    });
    return ok;
}

} // namespace aero
