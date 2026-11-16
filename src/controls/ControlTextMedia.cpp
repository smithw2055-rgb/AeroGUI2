#include "ControlsInternal.hpp"

#include "aero/animation/AnimationEngine.hpp"
#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "realscript/aot_cpp/AotRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace aero {

namespace {

[[nodiscard]] DualTrackBindingEngine* FindBindingEngineForRichText(const UIElement* start) noexcept {
    DualTrackBindingEngine* foundEng = nullptr;
    for (const UIElement* cur = start; cur != nullptr && !foundEng; cur = cur->Parent()) {
        foundEng = cur->BoundTwoWayEngine();
        if (!foundEng && cur->TemplatedParent() != nullptr) {
            foundEng = cur->TemplatedParent()->BoundTwoWayEngine();
        }
    }
    return foundEng;
}

[[nodiscard]] bool HasRichTextCommon(const UIElement& elem) noexcept {
    const UnifiedValue rv = elem.GetValue(PropId_RichText);
    if (const auto* s = std::get_if<StringValue>(&rv)) {
        return !s->view().empty();
    }
    if (const UIElement* tp = elem.TemplatedParent()) {
        if (elem.FindCompactEntry(PropId_RichText) != nullptr) {
            const UnifiedValue tpContent = tp->GetValue(PropId_Content);
            if (const auto* cs = std::get_if<StringValue>(&tpContent)) {
                return !cs->view().empty();
            }
        }
    }
    return false;
}

[[nodiscard]] std::string GetRawRichTextCommon(const UIElement& elem) {
    const UnifiedValue rv = elem.GetValue(PropId_RichText);
    if (const auto* s = std::get_if<StringValue>(&rv); s && !s->view().empty()) {
        return s->str();
    }
    if (const UIElement* tp = elem.TemplatedParent()) {
        const UnifiedValue tpContent = tp->GetValue(PropId_Content);
        if (const auto* cs = std::get_if<StringValue>(&tpContent); cs && !cs->view().empty()) {
            return cs->str();
        }
    }
    return {};
}

[[nodiscard]] std::string GetTextCommon(const UIElement& elem) {
    const UnifiedValue v = elem.GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v); s && !s->view().empty()) {
        return s->str();
    }
    if (!std::holds_alternative<std::monostate>(v) &&
        !std::holds_alternative<NullObject>(v) &&
        !std::holds_alternative<StringValue>(v)) {
        return rs_rt::valueToString(v);
    }
    if (HasRichTextCommon(elem)) {
        return StripAndResolveRichTextBbCode(GetRawRichTextCommon(elem), &elem);
    }
    const UnifiedValue cv = elem.GetValue(PropId_Content);
    if (const auto* cs = std::get_if<StringValue>(&cv); cs && !cs->view().empty()) {
        return cs->str();
    }
    if (!std::holds_alternative<std::monostate>(cv) &&
        !std::holds_alternative<NullObject>(cv) &&
        !std::holds_alternative<StringValue>(cv)) {
        return rs_rt::valueToString(cv);
    }
    return {};
}

[[nodiscard]] TextWrapping GetTextWrappingCommon(const UIElement& elem) noexcept {
    const UnifiedValue v = elem.GetValue(PropId_TextWrapping);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i == 1) return TextWrapping::Wrap;
        if (*i == 2) return TextWrapping::WrapWithOverflow;
        return TextWrapping::NoWrap;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_TextWrapping, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi == 1) return TextWrapping::Wrap;
            if (*pi == 2) return TextWrapping::WrapWithOverflow;
        }
    }
    return TextWrapping::NoWrap;
}

[[nodiscard]] TextAlignment GetTextAlignmentCommon(const UIElement& elem) noexcept {
    const UnifiedValue v = elem.GetValue(PropId_TextAlignment);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i == 1) return TextAlignment::Center;
        if (*i == 2) return TextAlignment::Right;
        if (*i == 3) return TextAlignment::Justify;
        return TextAlignment::Left;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const UnifiedValue parsed = ParseBuiltinLiteralPropertyValue(PropId_TextAlignment, sv->view());
        if (const auto* pi = std::get_if<std::int64_t>(&parsed)) {
            if (*pi == 1) return TextAlignment::Center;
            if (*pi == 2) return TextAlignment::Right;
            if (*pi == 3) return TextAlignment::Justify;
        }
    }
    return TextAlignment::Left;
}

void RegisterRichTextIfBound(UIElement* elem, std::uint16_t propId, std::string_view rawRt) {
    if (!elem || (propId != PropId_RichText && propId != PropId_DataContext) || elem->RefCount() == 0u) {
        return;
    }
    if (rawRt.find("[bind") == std::string_view::npos) {
        return;
    }
    if (DualTrackBindingEngine* foundEng = FindBindingEngineForRichText(elem)) {
        foundEng->RegisterRichTextDependent(IntrusivePtr<UIElement>(elem));
    }
}

[[nodiscard]] Size MeasureTextBlockLike(
    const UIElement& elem,
    std::string text,
    float fontSize,
    std::string_view family,
    TextWrapping wrapMode,
    Size availableSize) noexcept {
    const Thickness p = elem.GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    if (text.empty()) {
        const UnifiedValue ph = elem.GetValue(PropId_Placeholder);
        if (const auto* s = std::get_if<StringValue>(&ph)) {
            text = s->str();
        }
    }
    const float maxTextW = (wrapMode != TextWrapping::NoWrap)
        ? std::max(1.0f, availableSize.width - padW)
        : std::max(0.0f, availableSize.width - padW);
    const Size textMetrics = MeasureUtf8TextRun(text, fontSize, family, maxTextW, wrapMode);
    return Size{textMetrics.width + padW, textMetrics.height + padH};
}

[[nodiscard]] std::string FirstUtf8CodePointOrDefault(
    std::string_view raw,
    std::string_view fallback) {
    if (!raw.empty()) {
        std::size_t cur = 0u;
        std::uint32_t dummy = 0u;
        if (DecodeNextUtf8CodePoint(raw, cur, dummy) && cur > 0u) {
            return std::string(raw.substr(0u, cur));
        }
    }
    return std::string(fallback);
}

[[nodiscard]] std::size_t FilterEditableUtf8Text(
    std::string_view utf8Text,
    std::size_t maxCodePoints,
    bool allowNewline,
    std::string& outFiltered) {
    outFiltered.clear();
    outFiltered.reserve(utf8Text.size());
    std::size_t cursor = 0u;
    std::size_t cpCount = 0u;
    std::uint32_t cp = 0u;
    while (cpCount < maxCodePoints) {
        const std::size_t cpStart = cursor;
        if (!DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
            break;
        }
        if (cp == '\n' && allowNewline) {
            outFiltered.push_back('\n');
            ++cpCount;
            continue;
        }
        if (cp < 0x20u || cp == 0x7Fu) {
            continue;
        }
        outFiltered.append(utf8Text.substr(cpStart, cursor - cpStart));
        ++cpCount;
    }
    return cpCount;
}

[[nodiscard]] std::string SpliceUtf8Range(
    std::string_view cur,
    std::size_t startCp,
    std::size_t lenCp,
    std::string_view replacement) {
    const std::size_t b0 = detail::Utf8ByteOffsetAtCodePoint(cur, startCp);
    const std::size_t b1 = detail::Utf8ByteOffsetAtCodePoint(cur, startCp + lenCp);
    std::string updated;
    updated.reserve(b0 + replacement.size() + (cur.size() - b1));
    updated.append(cur.substr(0u, b0));
    updated.append(replacement);
    updated.append(cur.substr(b1));
    return updated;
}

[[nodiscard]] std::size_t HitTestLineCodePoint(
    std::string_view text,
    const TextLayoutResult& layout,
    float relX,
    std::size_t startCp) noexcept {
    std::size_t cursor = 0u;
    std::size_t glyphIdx = 0u;
    std::size_t cpIdx = startCp;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(text, cursor, cp)) {
        if (cp == '\r' || cp == '\n') {
            ++cpIdx;
            continue;
        }
        if (glyphIdx < layout.glyphs.size()) {
            const PositionedGlyph& g = layout.glyphs[glyphIdx++];
            if (relX < g.penX + g.advanceX * 0.5f) {
                break;
            }
        }
        ++cpIdx;
    }
    return cpIdx;
}

[[nodiscard]] Stretch ReadStretchProperty(const UIElement& elem) noexcept {
    const UnifiedValue v = elem.GetValue(PropId_Stretch);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 3) {
            return static_cast<Stretch>(*i);
        }
    }
    return Stretch::Uniform;
}

[[nodiscard]] StretchDirection ReadStretchDirectionProperty(const UIElement& elem) noexcept {
    const UnifiedValue v = elem.GetValue(PropId_StretchDirection);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= 0 && *i <= 2) {
            return static_cast<StretchDirection>(*i);
        }
    }
    return StretchDirection::Both;
}

} // namespace

namespace detail {

std::string ResolveRichTextBindToken(
    std::string_view propPath,
    std::string_view formatSpec,
    const UIElement* owner) {
    UnifiedValue foundVal{};
    bool hasVal = false;
    auto tryReadFromObj = [&](const ObjectRef& objRef, DualTrackBindingEngine* eng) -> bool {
        if (!objRef.valid() || !eng || !eng->Program()) return false;
        const auto* heap = eng->Domains().heapForObject(objRef);
        if (!heap) return false;
        const SymbolId typeId = heap->objectTypeId(objRef).value_or(0);
        if (typeId == 0) return false;
        if (auto resolved = eng->Domains().resolvePropertySlot(*eng->Program(), typeId, std::string(propPath))) {
            if (auto fv = heap->fieldGet(objRef, resolved->backingFieldIndex)) {
                foundVal = *fv;
                return true;
            }
        }
        return false;
    };

    DualTrackBindingEngine* foundEng = FindBindingEngineForRichText(owner);
    if (foundEng != nullptr && owner != nullptr && owner->RefCount() > 0u) {
        foundEng->RegisterRichTextDependent(IntrusivePtr<UIElement>(const_cast<UIElement*>(owner)));
    }
    for (const UIElement* cur = owner; cur != nullptr && !hasVal; cur = cur->Parent()) {
        const UnifiedValue dcVal = cur->GetValue(PropId_DataContext);
        if (const auto* objRef = std::get_if<ObjectRef>(&dcVal)) {
            if (tryReadFromObj(*objRef, foundEng)) {
                hasVal = true;
                break;
            }
        }
        if (!hasVal && cur->TemplatedParent() != nullptr) {
            const UIElement* tp = cur->TemplatedParent();
            const UnifiedValue tpDc = tp->GetValue(PropId_DataContext);
            if (const auto* objRef = std::get_if<ObjectRef>(&tpDc)) {
                if (tryReadFromObj(*objRef, foundEng)) {
                    hasVal = true;
                    break;
                }
            }
        }
    }

    // Fallback: look for Slider named propPath or default sample value so unmounted XAML still formats cleanly
    if (!hasVal && owner != nullptr) {
        const UIElement* root = owner;
        while (root->Parent() != nullptr) root = root->Parent();
        std::vector<const UIElement*> stack{root};
        while (!stack.empty() && !hasVal) {
            const UIElement* n = stack.back();
            stack.pop_back();
            if (n->Name() == propPath) {
                foundVal = n->GetValue(PropId_Value);
                if (!std::holds_alternative<std::monostate>(foundVal)) {
                    hasVal = true;
                    break;
                }
            }
            for (const UIElement* c = n->FirstChild(); c != nullptr; c = c->NextSibling()) {
                stack.push_back(c);
            }
        }
    }
    if (!hasVal) {
        if (!formatSpec.empty()) {
            foundVal = MakeFloat(0.0f);
            hasVal = true;
        } else {
            return {};
        }
    }

    float numVal = 0.0f;
    const bool isNum = TryGetFloat(foundVal, numVal);
    if (isNum && !formatSpec.empty()) {
        // Handle '{0:0}%' or '{0:F1}' style Noesis StringFormat
        const std::size_t braceOpen = formatSpec.find("{0");
        const std::size_t braceClose = (braceOpen != std::string_view::npos)
            ? formatSpec.find('}', braceOpen)
            : std::string_view::npos;
        if (braceOpen != std::string_view::npos && braceClose != std::string_view::npos) {
            std::string out(formatSpec.substr(0, braceOpen));
            char buf[64]{};
            const std::string_view fmtInner = formatSpec.substr(braceOpen + 2, braceClose - (braceOpen + 2));
            if (fmtInner == ":0" || fmtInner.empty()) {
                std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(std::round(numVal)));
            } else if (fmtInner == ":0.0" || fmtInner == ":F1") {
                std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(numVal));
            } else {
                std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(numVal));
            }
            out += buf;
            out.append(formatSpec.substr(braceClose + 1));
            return out;
        }
    }
    return rs_rt::valueToString(foundVal);
}

std::size_t CountUtf8CodePointsSafe(std::string_view s) noexcept {
    std::size_t count = 0u;
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(s, cursor, cp)) {
        ++count;
    }
    return count;
}

std::size_t Utf8ByteOffsetAtCodePoint(std::string_view s, std::size_t cpTarget) noexcept {
    std::size_t count = 0u;
    std::size_t cursor = 0u;
    std::uint32_t cp = 0u;
    while (count < cpTarget && DecodeNextUtf8CodePoint(s, cursor, cp)) {
        ++count;
    }
    return cursor;
}

bool IsWordCodePoint(std::uint32_t cp) noexcept {
    if (cp >= 128u) return true;
    const auto uc = static_cast<unsigned char>(cp);
    return std::isalnum(uc) != 0 || uc == '_';
}

std::size_t PrevWordBoundaryCp(std::string_view text, std::size_t fromCp) noexcept {
    std::vector<std::uint32_t> cps;
    std::size_t cur = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(text, cur, cp)) cps.push_back(cp);
    std::size_t i = std::min(fromCp, cps.size());
    while (i > 0u && !IsWordCodePoint(cps[i - 1u])) --i;
    while (i > 0u && IsWordCodePoint(cps[i - 1u])) --i;
    return i;
}

std::size_t NextWordBoundaryCp(std::string_view text, std::size_t fromCp) noexcept {
    std::vector<std::uint32_t> cps;
    std::size_t cur = 0u;
    std::uint32_t cp = 0u;
    while (DecodeNextUtf8CodePoint(text, cur, cp)) cps.push_back(cp);
    std::size_t i = std::min(fromCp, cps.size());
    while (i < cps.size() && IsWordCodePoint(cps[i])) ++i;
    while (i < cps.size() && !IsWordCodePoint(cps[i])) ++i;
    return i;
}

std::mutex& SharedClipboardMutex() noexcept {
    static std::mutex m;
    return m;
}

std::string& SharedClipboardBuffer() noexcept {
    static std::string s;
    return s;
}

bool ReadBinaryFileBytes(const std::string& path, std::vector<std::uint8_t>& outBytes) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return false;
    }
    const std::streamsize len = ifs.tellg();
    if (len <= 0) {
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(len));
    ifs.seekg(0, std::ios::beg);
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(outBytes.data()), len));
}

bool ResolveAndReadRiveAsset(
    std::string_view rawUri,
    std::vector<std::uint8_t>& outBytes) {
    std::string uri(rawUri);
    while (!uri.empty() && (uri.front() == ' ' || uri.front() == '\t')) uri.erase(uri.begin());
    while (!uri.empty() && (uri.back() == ' ' || uri.back() == '\t')) uri.pop_back();
    if (uri.empty()) {
        return false;
    }
    for (char& ch : uri) {
        if (ch == '\\') ch = '/';
    }
    if (uri.rfind("pack://", 0) == 0) {
        const std::size_t semi = uri.find(";component/");
        if (semi != std::string::npos) {
            uri = uri.substr(semi + 11u);
        }
    }
    if (!uri.empty() && uri.front() == '/') {
        if (ReadBinaryFileBytes(uri, outBytes)) {
            return true;
        }
        uri.erase(uri.begin());
        if (!uri.empty() && ReadBinaryFileBytes(uri, outBytes)) {
            return true;
        }
    } else if (ReadBinaryFileBytes(uri, outBytes)) {
        return true;
    }

    {
        const auto dirs = UIResourceContext::Current().RiveSearchDirectoriesSnapshot();
        for (const auto& dir : dirs) {
            if (dir.empty()) continue;
            std::string full = dir;
            if (full.back() != '/') full.push_back('/');
            full += uri;
            if (ReadBinaryFileBytes(full, outBytes)) {
                return true;
            }
        }
    }

    static constexpr std::string_view kFallbackPrefixes[] = {
        "Data/",
        "Samples/Rive/Data/",
        "../Samples/Rive/Data/",
    };
    for (std::string_view pfx : kFallbackPrefixes) {
        std::string candidate(pfx);
        candidate += uri;
        if (ReadBinaryFileBytes(candidate, outBytes)) {
            return true;
        }
    }
    return false;
}

} // namespace detail

std::string StripAndResolveRichTextBbCode(
    std::string_view rawBbCode,
    const UIElement* owner) {
    std::string out;
    out.reserve(rawBbCode.size());
    std::size_t i = 0u;
    while (i < rawBbCode.size()) {
        if (rawBbCode[i] == '[') {
            const std::size_t closeBracket = rawBbCode.find(']', i + 1u);
            if (closeBracket != std::string_view::npos) {
                const std::string_view tagContent = rawBbCode.substr(i + 1u, closeBracket - i - 1u);
                if (tagContent == "br" || tagContent == "br/") {
                    out.push_back('\n');
                    i = closeBracket + 1u;
                    continue;
                }
                if (tagContent.rfind("bind", 0) == 0 &&
                    (tagContent.size() == 4u || tagContent[4] == ' ' || tagContent[4] == '=')) {
                    std::string_view fmtSpec;
                    const std::size_t fmtPos = tagContent.find("format=");
                    if (fmtPos != std::string_view::npos) {
                        std::size_t qStart = fmtPos + 7u;
                        if (qStart < tagContent.size() && (tagContent[qStart] == '\'' || tagContent[qStart] == '"')) {
                            const char qCh = tagContent[qStart++];
                            const std::size_t qEnd = tagContent.find(qCh, qStart);
                            if (qEnd != std::string_view::npos) {
                                fmtSpec = tagContent.substr(qStart, qEnd - qStart);
                            }
                        }
                    }
                    const std::size_t endBind = rawBbCode.find("[/bind]", closeBracket + 1u);
                    if (endBind != std::string_view::npos) {
                        const std::string_view bindPath = rawBbCode.substr(closeBracket + 1u, endBind - (closeBracket + 1u));
                        out += detail::ResolveRichTextBindToken(bindPath, fmtSpec, owner);
                        i = endBind + 7u;
                        continue;
                    }
                }
                if (tagContent.rfind("img", 0) == 0) {
                    const std::size_t endImg = rawBbCode.find("[/img]", closeBracket + 1u);
                    if (endImg != std::string_view::npos) {
                        i = endImg + 6u;
                        continue;
                    }
                }
                // Known BBCode opening/closing formatting tags
                const bool isClosing = !tagContent.empty() && tagContent.front() == '/';
                const std::string_view bare = isClosing ? tagContent.substr(1u) : tagContent;
                const std::size_t eqPos = bare.find('=');
                const std::string_view tagName = (eqPos != std::string_view::npos) ? bare.substr(0u, eqPos) : bare;
                if (tagName == "b" || tagName == "i" || tagName == "u" || tagName == "s" ||
                    tagName == "style" || tagName == "color" || tagName == "size" || tagName == "url") {
                    i = closeBracket + 1u;
                    continue;
                }
            }
        }
        out.push_back(rawBbCode[i++]);
    }
    return out;
}

// ============================================================================
// TextBlock
// ============================================================================

AERO_IMPLEMENT_CLASS(TextBlock, UIElement, "Aero.UI.TextBlock")

TextBlock::TextBlock() {
    EnsureControlTypesRegistered();
}

void TextBlock::SetText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

bool TextBlock::HasRichText() const noexcept {
    return HasRichTextCommon(*this);
}

std::string TextBlock::GetRawRichText() const {
    return GetRawRichTextCommon(*this);
}

std::string TextBlock::GetText() const {
    return GetTextCommon(*this);
}

void TextBlock::SetFontSize(float fontSize) {
    SetValue(PropId_FontSize, MakeFloat(fontSize), ValueSourceRank::Local);
}

float TextBlock::GetFontSize() const noexcept {
    float out = 14.0f;
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        if (TryGetFloat(cur->GetValue(PropId_FontSize), out)) {
            return out;
        }
        if (cur == this || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                if (TryGetFloat(tp->GetValue(PropId_FontSize), out)) {
                    return out;
                }
            }
        }
    }
    return 14.0f;
}

void TextBlock::SetFontFamily(std::string_view fontFamily) {
    if (fontFamily.empty()) {
        (void)ClearValue(PropId_FontFamily, ValueSourceRank::Local);
    } else {
        SetValue(PropId_FontFamily, value::FromString(std::string(fontFamily)), ValueSourceRank::Local);
    }
}

std::string TextBlock::GetFontFamily() const {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        const UnifiedValue v = cur->GetValue(PropId_FontFamily);
        if (const auto* s = std::get_if<StringValue>(&v)) {
            if (!s->view().empty()) {
                return s->str();
            }
        }
        if (cur == this || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                const UnifiedValue tpVal = tp->GetValue(PropId_FontFamily);
                if (const auto* s = std::get_if<StringValue>(&tpVal)) {
                    if (!s->view().empty()) {
                        return s->str();
                    }
                }
            }
        }
    }
    return {};
}

void TextBlock::SetTextWrapping(TextWrapping wrapping) {
    SetValue(
        PropId_TextWrapping,
        value::FromInt(static_cast<std::int64_t>(wrapping)),
        ValueSourceRank::Local);
}

TextWrapping TextBlock::GetTextWrapping() const noexcept {
    return GetTextWrappingCommon(*this);
}

void TextBlock::SetForeground(const Color& color) {
    SetValue(PropId_Foreground, value::FromColor(color), ValueSourceRank::Local);
}

void TextBlock::SetTextAlignment(TextAlignment alignment) {
    SetValue(
        PropId_TextAlignment,
        value::FromInt(static_cast<std::int64_t>(alignment)),
        ValueSourceRank::Local);
}

TextAlignment TextBlock::GetTextAlignment() const noexcept {
    return GetTextAlignmentCommon(*this);
}

Color TextBlock::GetForeground() const noexcept {
    for (const UIElement* cur = this; cur != nullptr; cur = cur->Parent()) {
        if (auto c = value::TryGetColor(cur->GetValue(PropId_Foreground))) {
            return *c;
        }
        if (cur == this || cur->IsA<ContentPresenter>()) {
            if (const UIElement* tp = cur->TemplatedParent()) {
                if (auto c = value::TryGetColor(tp->GetValue(PropId_Foreground))) {
                    return *c;
                }
            }
        }
    }
    return Color{0.94f, 0.96f, 0.98f, 1.0f};
}

void TextBlock::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    UIElement::OnPropertyChanged(propId, newValue);
    RegisterRichTextIfBound(this, propId, GetRawRichText());
}

Size TextBlock::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate() || FirstChild() != nullptr) {
        return UIElement::MeasureCore(availableSize);
    }
    return MeasureTextBlockLike(*this, GetText(), GetFontSize(), GetFontFamily(), GetTextWrapping(), availableSize);
}

// ============================================================================
// Label
// ============================================================================

AERO_IMPLEMENT_CLASS(Label, ContentControl, "Aero.UI.Label")

Label::Label() {
    EnsureControlTypesRegistered();
}

void Label::SetText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

bool Label::HasRichText() const noexcept {
    return HasRichTextCommon(*this);
}

std::string Label::GetRawRichText() const {
    return GetRawRichTextCommon(*this);
}

std::string Label::GetText() const {
    return GetTextCommon(*this);
}

void Label::SetTextWrapping(TextWrapping wrapping) {
    SetValue(
        PropId_TextWrapping,
        value::FromInt(static_cast<std::int64_t>(wrapping)),
        ValueSourceRank::Local);
}

TextWrapping Label::GetTextWrapping() const noexcept {
    return GetTextWrappingCommon(*this);
}

void Label::SetTextAlignment(TextAlignment alignment) {
    SetValue(
        PropId_TextAlignment,
        value::FromInt(static_cast<std::int64_t>(alignment)),
        ValueSourceRank::Local);
}

TextAlignment Label::GetTextAlignment() const noexcept {
    return GetTextAlignmentCommon(*this);
}

void Label::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    ContentControl::OnPropertyChanged(propId, newValue);
    RegisterRichTextIfBound(this, propId, GetRawRichText());
}

Size Label::MeasureCore(Size availableSize) noexcept {
    if (HasControlTemplate() || FirstChild() != nullptr) {
        return ContentControl::MeasureCore(availableSize);
    }
    return MeasureTextBlockLike(*this, GetText(), GetFontSize(), GetFontFamily(), GetTextWrapping(), availableSize);
}

// ============================================================================
// TextBoxBase, TextBox, PasswordBox
// ============================================================================

AERO_IMPLEMENT_CLASS(TextBoxBase, Control, "Aero.UI.TextBoxBase")

TextBoxBase::TextBoxBase() {
    EnsureControlTypesRegistered();
}

void TextBoxBase::SetText(std::string_view text) {
    SetValue(PropId_Text, value::FromString(std::string(text)), ValueSourceRank::Local);
}

std::string TextBoxBase::GetText() const {
    const UnifiedValue v = GetValue(PropId_Text);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        return s->str();
    }
    if (!std::holds_alternative<std::monostate>(v) &&
        !std::holds_alternative<NullObject>(v)) {
        return rs_rt::valueToString(v);
    }
    return {};
}

void TextBoxBase::SetTextWrapping(TextWrapping wrapping) {
    SetValue(
        PropId_TextWrapping,
        value::FromInt(static_cast<std::int64_t>(wrapping)),
        ValueSourceRank::Local);
}

TextWrapping TextBoxBase::GetTextWrapping() const noexcept {
    return GetTextWrappingCommon(*this);
}

void TextBoxBase::SetTextAlignment(TextAlignment alignment) {
    SetValue(
        PropId_TextAlignment,
        value::FromInt(static_cast<std::int64_t>(alignment)),
        ValueSourceRank::Local);
}

TextAlignment TextBoxBase::GetTextAlignment() const noexcept {
    return GetTextAlignmentCommon(*this);
}

void TextBoxBase::SetPlaceholder(std::string_view placeholder) {
    SetValue(PropId_Placeholder, value::FromString(std::string(placeholder)), ValueSourceRank::Local);
}

std::string TextBoxBase::GetPlaceholder() const {
    const UnifiedValue v = GetValue(PropId_Placeholder);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void TextBoxBase::SetIsReadOnly(bool readOnly) {
    SetValue(PropId_IsReadOnly, UnifiedValue{readOnly}, ValueSourceRank::Local);
}

bool TextBoxBase::IsReadOnly() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsReadOnly);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void TextBoxBase::SetMaxLength(std::int32_t maxLen) {
    SetValue(PropId_MaxLength, value::FromInt(std::max<std::int32_t>(0, maxLen)), ValueSourceRank::Local);
}

std::int32_t TextBoxBase::GetMaxLength() const noexcept {
    const UnifiedValue v = GetValue(PropId_MaxLength);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i > 0) ? static_cast<std::int32_t>(*i) : 0;
    }
    return 0;
}

void TextBoxBase::SetClipboardText(std::string_view utf8Text) {
    std::lock_guard<std::mutex> lock(detail::SharedClipboardMutex());
    detail::SharedClipboardBuffer().assign(utf8Text);
}

std::string TextBoxBase::GetClipboardText() {
    std::lock_guard<std::mutex> lock(detail::SharedClipboardMutex());
    return detail::SharedClipboardBuffer();
}

void TextBoxBase::SetSelectionStart(std::size_t codePointIndex) {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    const std::size_t clamped = std::min(codePointIndex, maxCp);
    SetValue(PropId_SelectionStart, value::FromInt(static_cast<std::int64_t>(clamped)), ValueSourceRank::Local);
    const std::size_t curLen = GetSelectionLength();
    if (clamped + curLen > maxCp) {
        SetValue(PropId_SelectionLength, value::FromInt(static_cast<std::int64_t>(maxCp - clamped)), ValueSourceRank::Local);
    }
    hasAllSelected_ = (maxCp > 0u && clamped == 0u && GetSelectionLength() == maxCp);
    InvalidateRender();
}

std::size_t TextBoxBase::GetSelectionStart() const noexcept {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    if (hasAllSelected_) return 0u;
    const UnifiedValue v = GetValue(PropId_SelectionStart);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i < 0) return 0u;
        return std::min(static_cast<std::size_t>(*i), maxCp);
    }
    return std::min(GetCaretIndex(), maxCp);
}

void TextBoxBase::SetSelectionLength(std::size_t codePointLength) {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    const std::size_t start = GetSelectionStart();
    const std::size_t clamped = std::min(codePointLength, (maxCp >= start) ? (maxCp - start) : 0u);
    SetValue(PropId_SelectionLength, value::FromInt(static_cast<std::int64_t>(clamped)), ValueSourceRank::Local);
    hasAllSelected_ = (maxCp > 0u && start == 0u && clamped == maxCp);
    InvalidateRender();
}

std::size_t TextBoxBase::GetSelectionLength() const noexcept {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    if (hasAllSelected_) return maxCp;
    const UnifiedValue v = GetValue(PropId_SelectionLength);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i <= 0) return 0u;
        const std::size_t start = GetSelectionStart();
        return std::min(static_cast<std::size_t>(*i), (maxCp >= start) ? (maxCp - start) : 0u);
    }
    return 0u;
}

void TextBoxBase::Select(std::size_t startCodePoint, std::size_t lengthCodePoints) {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    const std::size_t s = std::min(startCodePoint, maxCp);
    const std::size_t l = std::min(lengthCodePoints, maxCp - s);
    selectionAnchorCp_ = s;
    SetValue(PropId_SelectionStart, value::FromInt(static_cast<std::int64_t>(s)), ValueSourceRank::Local);
    SetValue(PropId_SelectionLength, value::FromInt(static_cast<std::int64_t>(l)), ValueSourceRank::Local);
    SetValue(PropId_CaretIndex, value::FromInt(static_cast<std::int64_t>(s + l)), ValueSourceRank::Local);
    hasAllSelected_ = (maxCp > 0u && s == 0u && l == maxCp);
    InvalidateRender();
}

std::string TextBoxBase::GetSelectedText() const {
    const std::size_t len = GetSelectionLength();
    if (len == 0u) return {};
    const std::string cur = GetText();
    const std::size_t s = GetSelectionStart();
    const std::size_t b0 = detail::Utf8ByteOffsetAtCodePoint(cur, s);
    const std::size_t b1 = detail::Utf8ByteOffsetAtCodePoint(cur, s + len);
    return cur.substr(b0, b1 - b0);
}

void TextBoxBase::SetSelectedText(std::string_view replacement) {
    if (IsReadOnly() || !IsEffectivelyEnabled()) return;
    const std::string cur = GetText();
    const std::size_t l = GetSelectionLength();
    const std::size_t s = (l > 0u) ? GetSelectionStart() : GetCaretIndex();
    const std::size_t replCp = detail::CountUtf8CodePointsSafe(replacement);
    (void)CommitEditedText(SpliceUtf8Range(cur, s, l, replacement), s + replCp);
}

bool TextBoxBase::Copy() const {
    if (IsPassword()) return false;
    const std::string sel = GetSelectedText();
    if (sel.empty()) return false;
    SetClipboardText(sel);
    return true;
}

bool TextBoxBase::Cut() {
    if (IsReadOnly() || !IsEffectivelyEnabled() || IsPassword()) return false;
    const std::string sel = GetSelectedText();
    if (sel.empty()) return false;
    SetClipboardText(sel);
    SetSelectedText("");
    return true;
}

bool TextBoxBase::Paste() {
    if (IsReadOnly() || !IsEffectivelyEnabled()) return false;
    const std::string clip = GetClipboardText();
    if (clip.empty()) return false;
    return InsertText(clip);
}

void TextBoxBase::SetCaretIndex(std::size_t codePointIndex) {
    hasAllSelected_ = false;
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    const std::size_t clamped = std::min(codePointIndex, maxCp);
    selectionAnchorCp_ = clamped;
    SetValue(PropId_SelectionStart, value::FromInt(static_cast<std::int64_t>(clamped)), ValueSourceRank::Local);
    SetValue(PropId_SelectionLength, value::FromInt(0), ValueSourceRank::Local);
    SetValue(PropId_CaretIndex, value::FromInt(static_cast<std::int64_t>(clamped)), ValueSourceRank::Local);
    InvalidateRender();
}

std::size_t TextBoxBase::GetCaretIndex() const noexcept {
    const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
    const UnifiedValue v = GetValue(PropId_CaretIndex);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i < 0) return 0u;
        return std::min(static_cast<std::size_t>(*i), maxCp);
    }
    return maxCp;
}

void TextBoxBase::SetIsPassword(bool isPassword) {
    SetValue(PropId_IsPassword, UnifiedValue{isPassword}, ValueSourceRank::Local);
}

bool TextBoxBase::IsPassword() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsPassword);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

void TextBoxBase::SetAcceptsReturn(bool accepts) {
    SetValue(PropId_AcceptsReturn, UnifiedValue{accepts}, ValueSourceRank::Local);
}

bool TextBoxBase::AcceptsReturn() const noexcept {
    const UnifiedValue v = GetValue(PropId_AcceptsReturn);
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return false;
}

std::int32_t TextBoxBase::GetMaxLines() const noexcept {
    const UnifiedValue v = GetValue(PropId_MaxLines);
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return (*i > 0) ? static_cast<std::int32_t>(*i) : 0;
    }
    return 0;
}

void TextBoxBase::Clear() {
    hasAllSelected_ = false;
    (void)CommitEditedText("", 0u);
}

void TextBoxBase::SelectAll() {
    const std::size_t len = detail::CountUtf8CodePointsSafe(GetText());
    Select(0u, len);
}

std::string TextBoxBase::GetDisplayString() const {
    const std::string raw = GetText();
    if (raw.empty() || !IsPassword()) {
        return raw;
    }
    std::string maskUnit = "*";
    const UnifiedValue pcVal = GetValue(PropId_PasswordChar);
    if (const auto* sv = std::get_if<StringValue>(&pcVal)) {
        maskUnit = FirstUtf8CodePointOrDefault(sv->view(), "*");
    }
    const std::size_t cpCount = detail::CountUtf8CodePointsSafe(raw);
    if (maskUnit.size() == 1u) {
        return std::string(cpCount, maskUnit[0]);
    }
    std::string out;
    out.reserve(cpCount * maskUnit.size());
    for (std::size_t i = 0u; i < cpCount; ++i) {
        out.append(maskUnit);
    }
    return out;
}

float TextBoxBase::HorizontalTextScrollOffset() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const float padRight = (p.right > 0.0f) ? p.right : 6.0f;
    const float innerW = std::max(0.0f, r.width - (padLeft + padRight));
    if (innerW <= 4.0f) {
        return 0.0f;
    }
    const float fontSize = GetFontSize();
    const std::string displayStr = GetDisplayString();
    const std::size_t caretCp = std::min(GetCaretIndex(), detail::CountUtf8CodePointsSafe(displayStr));
    const std::size_t byteOff = detail::Utf8ByteOffsetAtCodePoint(displayStr, caretCp);
    std::size_t lineStartByte = 0u;
    if (displayStr.find('\n') != std::string::npos) {
        for (std::size_t i = byteOff; i > 0u; --i) {
            if (displayStr[i - 1u] == '\n') {
                lineStartByte = i;
                break;
            }
        }
    }
    const Size prefixMetrics = MeasureUtf8TextRun(
        std::string_view(displayStr).substr(lineStartByte, byteOff - lineStartByte),
        fontSize,
        GetFontFamily());
    const float maxCaretRelX = std::max(0.0f, innerW - 2.0f);
    return (prefixMetrics.width > maxCaretRelX) ? (prefixMetrics.width - maxCaretRelX) : 0.0f;
}

Rect TextBoxBase::CaretLocalRect() const noexcept {
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float fontSize = GetFontSize();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const std::string displayStr = GetDisplayString();
    const std::size_t caretCp = std::min(GetCaretIndex(), detail::CountUtf8CodePointsSafe(displayStr));
    const std::size_t byteOff = detail::Utf8ByteOffsetAtCodePoint(displayStr, caretCp);
    const float scrollX = HorizontalTextScrollOffset();
    if (displayStr.find('\n') != std::string::npos) {
        const float padTop = (p.top > 0.0f) ? p.top : 4.0f;
        std::size_t lineIdx = 0u;
        std::size_t lineStartByte = 0u;
        for (std::size_t i = 0u; i < byteOff; ++i) {
            if (displayStr[i] == '\n') {
                ++lineIdx;
                lineStartByte = i + 1u;
            }
        }
        const TextLayoutResult layout = FontAtlas::Instance().MeasureLayout(
            displayStr,
            GetFontFamily(),
            std::max(1.0f, fontSize),
            detail::kUnconstrainedSpace,
            TextWrapping::NoWrap);
        const float lineH = std::max(12.0f, layout.lineHeight);
        const Size linePrefix = MeasureUtf8TextRun(
            std::string_view(displayStr).substr(lineStartByte, byteOff - lineStartByte),
            fontSize,
            GetFontFamily());
        return Rect{
            r.x + padLeft + linePrefix.width - scrollX,
            r.y + padTop + static_cast<float>(lineIdx) * lineH,
            2.0f,
            lineH};
    }
    const float caretH = std::max(12.0f, fontSize * 1.2f);
    const Size prefixMetrics = MeasureUtf8TextRun(
        std::string_view(displayStr).substr(0, byteOff),
        fontSize,
        GetFontFamily());
    const float boxH = (r.height > 0.0f) ? r.height : std::max(28.0f, caretH + 8.0f);
    const float cy = r.y + std::max(2.0f, (boxH - caretH) * 0.5f);
    return Rect{r.x + padLeft + prefixMetrics.width - scrollX, cy, 2.0f, caretH};
}

Rect TextBoxBase::CaretScreenRect() const noexcept {
    return LocalToScreenBounds(CaretLocalRect());
}

void TextBoxBase::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Text) {
        const std::size_t cpLen = detail::CountUtf8CodePointsSafe(GetText());
        if (const auto* entry = FindCompactEntry(PropId_CaretIndex)) {
            if (const auto* ci = std::get_if<std::int64_t>(&entry->effectiveValue)) {
                if (*ci > static_cast<std::int64_t>(cpLen)) {
                    SetCaretIndex(cpLen);
                }
            }
        }
    }
}

void TextBoxBase::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    if (!focused) {
        hasAllSelected_ = false;
        SetValue(PropId_SelectionLength, value::FromInt(0), ValueSourceRank::Local);
    }
}

void TextBoxBase::OnEditedTextCommitted(const std::string& newText) {
    (void)newText;
}

bool TextBoxBase::CommitEditedText(std::string newText, std::size_t newCaretCodePoint) {
    hasAllSelected_ = false;
    const std::string prevText = GetText();
    SetText(newText);
    SetCaretIndex(newCaretCodePoint);
    if (newText != prevText) {
        OnEditedTextCommitted(newText);
        (void)NotifyTwoWayPropertyMutated(PropId_Text);
        return true;
    }
    return false;
}

bool TextBoxBase::InsertText(std::string_view utf8Text) {
    if (IsReadOnly() || !IsEffectivelyEnabled() || utf8Text.empty()) {
        return false;
    }
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        SetValue(PropId_SelectionLength, value::FromInt(0), ValueSourceRank::Local);
        return ReplaceAllTextInteractive(utf8Text);
    }
    const bool allowNewline = AcceptsReturn() && !IsPassword();
    const std::int32_t maxLen = GetMaxLength();
    if (GetSelectionLength() > 0u) {
        const std::string cur = GetText();
        const std::size_t selStart = GetSelectionStart();
        const std::size_t selLen = GetSelectionLength();
        const std::size_t baseCpLen = detail::CountUtf8CodePointsSafe(cur) - selLen;
        const std::size_t allow = (maxLen > 0)
            ? ((static_cast<std::size_t>(maxLen) > baseCpLen) ? (static_cast<std::size_t>(maxLen) - baseCpLen) : 0u)
            : 1000000u;
        std::string filtered;
        const std::size_t insCount = FilterEditableUtf8Text(utf8Text, allow, allowNewline, filtered);
        return CommitEditedText(SpliceUtf8Range(cur, selStart, selLen, filtered), selStart + insCount);
    }

    const std::string cur = GetText();
    const std::size_t curCpLen = detail::CountUtf8CodePointsSafe(cur);
    const std::size_t remainingAllowed = (maxLen > 0)
        ? ((static_cast<std::size_t>(maxLen) > curCpLen) ? (static_cast<std::size_t>(maxLen) - curCpLen) : 0u)
        : 1000000u;
    if (remainingAllowed == 0u) {
        return false;
    }

    std::string filtered;
    const std::size_t insertedCpCount =
        FilterEditableUtf8Text(utf8Text, remainingAllowed, allowNewline, filtered);
    if (insertedCpCount == 0u) {
        return false;
    }

    const std::size_t caretCp = GetCaretIndex();
    return CommitEditedText(SpliceUtf8Range(cur, caretCp, 0u, filtered), caretCp + insertedCpCount);
}

bool TextBoxBase::ReplaceAllTextInteractive(std::string_view utf8Text) {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    const std::int32_t maxLen = GetMaxLength();
    const std::size_t limit = (maxLen > 0) ? static_cast<std::size_t>(maxLen) : 1000000u;
    std::string filtered;
    const std::size_t cpCount =
        FilterEditableUtf8Text(utf8Text, limit, AcceptsReturn() && !IsPassword(), filtered);
    return CommitEditedText(std::move(filtered), cpCount);
}

bool TextBoxBase::Backspace() {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        return CommitEditedText("", 0u);
    }
    if (GetSelectionLength() > 0u) {
        const std::size_t s = GetSelectionStart();
        return CommitEditedText(SpliceUtf8Range(GetText(), s, GetSelectionLength(), {}), s);
    }
    const std::size_t caretCp = GetCaretIndex();
    if (caretCp == 0u) {
        return false;
    }
    return CommitEditedText(SpliceUtf8Range(GetText(), caretCp - 1u, 1u, {}), caretCp - 1u);
}

bool TextBoxBase::DeleteForward() {
    if (IsReadOnly() || !IsEffectivelyEnabled()) {
        return false;
    }
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        return CommitEditedText("", 0u);
    }
    if (GetSelectionLength() > 0u) {
        const std::size_t s = GetSelectionStart();
        return CommitEditedText(SpliceUtf8Range(GetText(), s, GetSelectionLength(), {}), s);
    }
    const std::string cur = GetText();
    const std::size_t totalCp = detail::CountUtf8CodePointsSafe(cur);
    const std::size_t caretCp = GetCaretIndex();
    if (caretCp >= totalCp) {
        return false;
    }
    return CommitEditedText(SpliceUtf8Range(cur, caretCp, 1u, {}), caretCp);
}

bool TextBoxBase::MoveCaretLeft() {
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        SetCaretIndex(0u);
        return true;
    }
    const std::size_t cur = GetCaretIndex();
    if (cur == 0u) {
        return false;
    }
    SetCaretIndex(cur - 1u);
    return true;
}

bool TextBoxBase::MoveCaretRight() {
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        SetCaretIndex(detail::CountUtf8CodePointsSafe(GetText()));
        return true;
    }
    const std::size_t cur = GetCaretIndex();
    const std::size_t total = detail::CountUtf8CodePointsSafe(GetText());
    if (cur >= total) {
        return false;
    }
    SetCaretIndex(cur + 1u);
    return true;
}

bool TextBoxBase::MoveCaretHome() {
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        SetCaretIndex(0u);
        return true;
    }
    if (GetCaretIndex() == 0u) {
        return false;
    }
    SetCaretIndex(0u);
    return true;
}

bool TextBoxBase::MoveCaretEnd() {
    if (hasAllSelected_) {
        hasAllSelected_ = false;
        SetCaretIndex(detail::CountUtf8CodePointsSafe(GetText()));
        return true;
    }
    const std::size_t total = detail::CountUtf8CodePointsSafe(GetText());
    if (GetCaretIndex() == total) {
        return false;
    }
    SetCaretIndex(total);
    return true;
}

bool TextBoxBase::OnPointerDown(
    Point localPoint,
    PointerButton button,
    std::uint8_t modifiers,
    bool& outRequestCapture) {
    outRequestCapture = false;
    if (button != PointerButton::Left || !IsEffectivelyEnabled()) {
        return false;
    }
    const Rect r = FinalRect();
    const Thickness p = GetPadding();
    const float padLeft = (p.left > 0.0f) ? p.left : 6.0f;
    const float relX = std::max(0.0f, localPoint.x - (r.x + padLeft) + HorizontalTextScrollOffset());
    const float fontSize = std::max(1.0f, GetFontSize());
    const std::string display = GetDisplayString();
    const TextLayoutResult layout = FontAtlas::Instance().MeasureLayout(
        display,
        GetFontFamily(),
        fontSize,
        detail::kUnconstrainedSpace,
        TextWrapping::NoWrap);

    std::size_t cpIdx = 0u;
    if (display.find('\n') != std::string::npos) {
        struct LineSlice {
            std::size_t startCp = 0u;
            std::size_t byteStart = 0u;
            std::size_t byteEnd = 0u;
        };
        std::vector<LineSlice> lines;
        std::size_t curLineStartCp = 0u;
        std::size_t curLineByteStart = 0u;
        std::size_t scanCursor = 0u;
        std::size_t totalCp = 0u;
        std::uint32_t cp = 0u;
        while (scanCursor < display.size()) {
            const std::size_t prevCursor = scanCursor;
            if (!DecodeNextUtf8CodePoint(display, scanCursor, cp)) break;
            if (cp == '\n') {
                std::size_t endByte = prevCursor;
                if (endByte > curLineByteStart && display[endByte - 1u] == '\r') {
                    --endByte;
                }
                lines.push_back(LineSlice{curLineStartCp, curLineByteStart, endByte});
                ++totalCp;
                curLineStartCp = totalCp;
                curLineByteStart = scanCursor;
            } else {
                ++totalCp;
            }
        }
        {
            std::size_t endByte = display.size();
            if (endByte > curLineByteStart && display[endByte - 1u] == '\r') {
                --endByte;
            }
            lines.push_back(LineSlice{curLineStartCp, curLineByteStart, endByte});
        }
        const float padTop = (p.top > 0.0f) ? p.top : 4.0f;
        const float relY = std::max(0.0f, localPoint.y - (r.y + padTop));
        const float lineH = std::max(1.0f, layout.lineHeight);
        const std::size_t targetLine = std::min<std::size_t>(
            static_cast<std::size_t>(std::floor(relY / lineH)),
            lines.size() - 1u);
        const LineSlice& ls = lines[targetLine];
        const std::string_view lineView(
            display.data() + ls.byteStart,
            ls.byteEnd - ls.byteStart);
        const TextLayoutResult lineLayout = FontAtlas::Instance().MeasureLayout(
            lineView,
            GetFontFamily(),
            fontSize,
            detail::kUnconstrainedSpace,
            TextWrapping::NoWrap);
        cpIdx = HitTestLineCodePoint(lineView, lineLayout, relX, ls.startCp);
    } else {
        cpIdx = HitTestLineCodePoint(display, layout, relX, 0u);
    }

    const bool shift = (modifiers & KeyMod_Shift) != 0u;
    if (shift) {
        const std::size_t maxCp = detail::CountUtf8CodePointsSafe(GetText());
        const std::size_t anchor = std::min(selectionAnchorCp_, maxCp);
        const std::size_t clampedTarget = std::min(cpIdx, maxCp);
        if (clampedTarget >= anchor) {
            Select(anchor, clampedTarget - anchor);
        } else {
            hasAllSelected_ = false;
            selectionAnchorCp_ = anchor;
            SetValue(PropId_SelectionStart, value::FromInt(static_cast<std::int64_t>(clampedTarget)), ValueSourceRank::Local);
            SetValue(PropId_SelectionLength, value::FromInt(static_cast<std::int64_t>(anchor - clampedTarget)), ValueSourceRank::Local);
            SetValue(PropId_CaretIndex, value::FromInt(static_cast<std::int64_t>(clampedTarget)), ValueSourceRank::Local);
            InvalidateRender();
        }
    } else {
        SetCaretIndex(cpIdx);
    }
    return true;
}

bool TextBoxBase::OnKeyDown(KeyCode key, std::uint8_t modifiers) {
    if (!IsEffectivelyEnabled()) {
        return false;
    }
    const bool ctrl = (modifiers & KeyMod_Ctrl) != 0u;
    const bool shift = (modifiers & KeyMod_Shift) != 0u;
    if (ctrl && !shift) {
        if (key == KeyCode::A) {
            SelectAll();
            return true;
        }
        if (key == KeyCode::C) {
            return Copy();
        }
        if (key == KeyCode::X) {
            return Cut();
        }
        if (key == KeyCode::V) {
            return Paste();
        }
    }
    auto moveOrExtendTo = [&](std::size_t targetCp) {
        if (shift) {
            const std::size_t anchor = (GetSelectionLength() > 0u) ? selectionAnchorCp_ : GetCaretIndex();
            selectionAnchorCp_ = anchor;
            const std::size_t s = std::min(anchor, targetCp);
            const std::size_t l = std::max(anchor, targetCp) - s;
            SetValue(PropId_SelectionStart, value::FromInt(static_cast<std::int64_t>(s)), ValueSourceRank::Local);
            SetValue(PropId_SelectionLength, value::FromInt(static_cast<std::int64_t>(l)), ValueSourceRank::Local);
            SetValue(PropId_CaretIndex, value::FromInt(static_cast<std::int64_t>(targetCp)), ValueSourceRank::Local);
            const std::size_t total = detail::CountUtf8CodePointsSafe(GetText());
            hasAllSelected_ = (total > 0u && s == 0u && l == total);
            InvalidateRender();
        } else {
            SetCaretIndex(targetCp);
        }
    };
    const std::string curText = GetText();
    const std::size_t totalCp = detail::CountUtf8CodePointsSafe(curText);
    const std::size_t caretCp = GetCaretIndex();
    switch (key) {
        case KeyCode::Backspace:
            return Backspace();
        case KeyCode::Delete:
            return DeleteForward();
        case KeyCode::Left: {
            if (!shift && !ctrl && GetSelectionLength() > 0u) {
                SetCaretIndex(GetSelectionStart());
                return true;
            }
            const std::size_t nextCp = ctrl
                ? detail::PrevWordBoundaryCp(curText, caretCp)
                : ((caretCp > 0u) ? (caretCp - 1u) : 0u);
            moveOrExtendTo(nextCp);
            return true;
        }
        case KeyCode::Right: {
            if (!shift && !ctrl && GetSelectionLength() > 0u) {
                SetCaretIndex(GetSelectionStart() + GetSelectionLength());
                return true;
            }
            const std::size_t nextCp = ctrl
                ? detail::NextWordBoundaryCp(curText, caretCp)
                : std::min(totalCp, caretCp + 1u);
            moveOrExtendTo(nextCp);
            return true;
        }
        case KeyCode::Home:
            moveOrExtendTo(0u);
            return true;
        case KeyCode::End:
            moveOrExtendTo(totalCp);
            return true;
        case KeyCode::Enter:
            if (AcceptsReturn() && !IsPassword()) {
                return InsertText("\n");
            }
            return false;
        default:
            return false;
    }
}

bool TextBoxBase::OnTextInput(std::string_view utf8Text) {
    return InsertText(utf8Text);
}

Size TextBoxBase::MeasureCore(Size availableSize) noexcept {
    (void)availableSize;
    const Thickness p = GetPadding();
    const float padW = (p.left + p.right > 0.0f) ? (p.left + p.right) : 12.0f;
    const float padH = (p.top + p.bottom > 0.0f) ? (p.top + p.bottom) : 8.0f;
    const float fontSize = GetFontSize();
    std::string text = GetDisplayString();
    if (text.empty()) {
        text = GetPlaceholder();
    }
    if (text.find('\n') != std::string::npos) {
        const TextLayoutResult layout = FontAtlas::Instance().MeasureLayout(
            text,
            GetFontFamily(),
            std::max(1.0f, fontSize),
            detail::kUnconstrainedSpace,
            TextWrapping::NoWrap);
        std::size_t nlCount = 0u;
        for (char c : text) {
            if (c == '\n') ++nlCount;
        }
        const float multiH = std::max(
            layout.totalHeight,
            static_cast<float>(nlCount + 1u) * std::max(12.0f, layout.lineHeight));
        return Size{
            std::max(80.0f, layout.totalWidth + padW),
            std::max(28.0f, multiH + padH)};
    }
    const Size metrics = MeasureUtf8TextRun(text, fontSize, GetFontFamily());
    return Size{
        std::max(80.0f, metrics.width + padW),
        std::max(28.0f, std::max(fontSize * 1.25f, metrics.height) + padH)};
}

AERO_IMPLEMENT_CLASS(TextBox, TextBoxBase, "Aero.UI.TextBox")

TextBox::TextBox() {
    EnsureControlTypesRegistered();
}

void TextBox::OnEditedTextCommitted(const std::string& newText) {
    if (textChangedCallback_) {
        textChangedCallback_(*this, newText);
    }
    (void)DispatchWrapperRoutedEvent("TextChanged", value::FromString(newText), true);
}

AERO_IMPLEMENT_CLASS(PasswordBox, TextBoxBase, "Aero.UI.PasswordBox")

PasswordBox::PasswordBox() {
    EnsureControlTypesRegistered();
    (void)SetValue(PropId_IsPassword, UnifiedValue{true}, ValueSourceRank::Default);
}

void PasswordBox::SetPassword(std::string_view password) {
    SetText(password);
    SetCaretIndex(detail::CountUtf8CodePointsSafe(password));
}

void PasswordBox::SetPasswordChar(std::string_view ch) {
    SetValue(PropId_PasswordChar, value::FromString(std::string(ch)), ValueSourceRank::Local);
}

std::string PasswordBox::GetPasswordChar() const {
    const UnifiedValue v = GetValue(PropId_PasswordChar);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return FirstUtf8CodePointOrDefault(sv->view(), "*");
    }
    return "*";
}

void PasswordBox::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    TextBoxBase::OnPropertyChanged(propId, newValue);
    if (propId == PropId_Text) {
        OnEditedTextCommitted(GetText());
    }
}

void PasswordBox::OnEditedTextCommitted(const std::string& newText) {
    if (newText != lastNotifiedPassword_) {
        lastNotifiedPassword_ = newText;
        if (passwordChangedCallback_) {
            passwordChangedCallback_(*this, newText);
        }
        (void)DispatchWrapperRoutedEvent("PasswordChanged", value::FromString(newText), true);
    }
}

// ============================================================================
// Image
// ============================================================================

AERO_IMPLEMENT_CLASS(Image, UIElement, "Aero.UI.Image")

Image::Image() {
    EnsureControlTypesRegistered();
}

void Image::SetSource(std::string_view sourceUri) {
    SetValue(PropId_ImageSource, value::FromString(std::string(sourceUri)), ValueSourceRank::Local);
}

std::string Image::GetSource() const {
    const auto v = GetValue(PropId_ImageSource);
    if (const auto* s = std::get_if<StringValue>(&v)) {
        const std::string_view sv = s->view();
        if (sv.size() > 16u && sv.rfind("AERO_IMGBRUSH:", 0) == 0 &&
            sv[15] == ':' && sv[14] >= '0' && sv[14] <= '3') {
            return std::string(sv.substr(16u));
        }
        return s->str();
    }
    return {};
}

void Image::SetStretch(Stretch stretch) {
    SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch Image::GetStretch() const noexcept {
    return ReadStretchProperty(*this);
}

void Image::SetStretchDirection(StretchDirection dir) {
    SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection Image::GetStretchDirection() const noexcept {
    return ReadStretchDirectionProperty(*this);
}

Size Image::GetNaturalImageSize() const {
    const std::string src = GetSource();
    if (src.empty()) {
        return Size{0.0f, 0.0f};
    }
    if (const ImageAtlasEntry* entry = ImageAtlas::Instance().LoadOrGet(src)) {
        return Size{static_cast<float>(entry->width), static_cast<float>(entry->height)};
    }
    return Size{0.0f, 0.0f};
}

Size Image::MeasureCore(Size availableSize) noexcept {
    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const std::string src = GetSource();
    if (src.empty()) {
        return Size{padW, padH};
    }

    const ImageAtlasEntry* entry = ImageAtlas::Instance().LoadOrGet(src);
    if (!entry || entry->width == 0u || entry->height == 0u) {
        return Size{padW, padH};
    }

    const float natW = static_cast<float>(entry->width);
    const float natH = static_cast<float>(entry->height);
    const Stretch stretch = GetStretch();
    const StretchDirection dir = GetStretchDirection();

    const bool hasW = std::isfinite(availableSize.width) && (availableSize.width < detail::kUnconstrainedSpace * 0.5f);
    const bool hasH = std::isfinite(availableSize.height) && (availableSize.height < detail::kUnconstrainedSpace * 0.5f);
    if (stretch == Stretch::None || (!hasW && !hasH)) {
        return Size{natW + padW, natH + padH};
    }

    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);
    float sx = 1.0f;
    float sy = 1.0f;
    detail::ComputeViewboxScaleFactors(
        natW,
        natH,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        stretch,
        dir,
        sx,
        sy);

    float desiredW = natW * sx;
    float desiredH = natH * sy;
    if (hasW) {
        desiredW = std::min(desiredW, innerAvailW);
    }
    if (hasH) {
        desiredH = std::min(desiredH, innerAvailH);
    }
    return Size{desiredW + padW, desiredH + padH};
}

// ============================================================================
// RiveControl
// ============================================================================

AERO_IMPLEMENT_CLASS(RiveControl, Control, "Aero.UI.RiveControl")

RiveControl::RiveControl() {
    EnsureControlTypesRegistered();
}

RiveControl::RiveControl(
    std::string_view sourceUri,
    AnimationEngine* animationEngine)
    : externalAnimationEngine_(animationEngine) {
    EnsureControlTypesRegistered();
    if (!sourceUri.empty()) {
        SetSource(sourceUri);
    }
}

RiveControl::~RiveControl() {
    if (ownedAnimationEngine_ && artboardRoot_) {
        (void)ownedAnimationEngine_->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }
}

void RiveControl::AddAssetSearchDirectory(std::string_view directory) {
    UIResourceContext::Current().AddRiveSearchDirectory(directory);
}

void RiveControl::ClearAssetSearchDirectories() {
    UIResourceContext::Current().ClearRiveSearchDirectories();
}

void RiveControl::SetSource(std::string_view sourceUri) {
    (void)SetValue(PropId_RiveSource, value::FromString(std::string(sourceUri)), ValueSourceRank::Local);
}

std::string RiveControl::GetSource() const {
    const UnifiedValue rv = GetValue(PropId_RiveSource);
    if (const auto* sv = std::get_if<StringValue>(&rv)) {
        if (!sv->view().empty()) {
            return sv->str();
        }
    }
    const UnifiedValue iv = GetValue(PropId_ImageSource);
    if (const auto* sv = std::get_if<StringValue>(&iv)) {
        return sv->str();
    }
    return {};
}

void RiveControl::SetStretch(Stretch stretch) {
    (void)SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch RiveControl::GetStretch() const noexcept {
    return ReadStretchProperty(*this);
}

void RiveControl::SetStretchDirection(StretchDirection dir) {
    (void)SetValue(
        PropId_StretchDirection,
        value::FromInt(static_cast<std::int64_t>(dir)),
        ValueSourceRank::Local);
}

StretchDirection RiveControl::GetStretchDirection() const noexcept {
    return ReadStretchDirectionProperty(*this);
}

void RiveControl::SetAnimationEngine(AnimationEngine* engine) noexcept {
    if (externalAnimationEngine_ == engine) {
        return;
    }
    if (externalAnimationEngine_ && artboardRoot_) {
        (void)externalAnimationEngine_->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }
    externalAnimationEngine_ = engine;
    if (mountedPackage_ && artboardRoot_) {
        std::shared_ptr<AxbPackage> pkgCopy = mountedPackage_;
        (void)MountDecodedRivePackage(std::move(pkgCopy));
    }
}

AnimationEngine* RiveControl::ActiveAnimationEngine() const noexcept {
    return externalAnimationEngine_ ? externalAnimationEngine_ : ownedAnimationEngine_.get();
}

bool RiveControl::LoadFromMemory(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    std::string_view artboardNameHint) {
    if (!rawBytes || byteLength == 0u) {
        return false;
    }
    auto pkg = std::make_shared<AxbPackage>();
    if (!DecodeRiveBinaryToAxbPackage(rawBytes, byteLength, *pkg, nullptr, artboardNameHint)) {
        return false;
    }
    loadedSourceUri_.clear();
    return MountDecodedRivePackage(std::move(pkg));
}

bool RiveControl::ReloadFromSourceUri(std::string_view sourceUri) {
    if (reloadingSource_) {
        return false;
    }
    if (!sourceUri.empty() && sourceUri == loadedSourceUri_ && IsRiveLoaded()) {
        return true;
    }
    reloadingSource_ = true;

    // Check pre-registered AxbTemplateRegistry first, then disk .riv file
    if (const AxbPackage* cachedPkg = AxbTemplateRegistry::Instance().Find(sourceUri)) {
        auto pkgCopy = std::make_shared<AxbPackage>(*cachedPkg);
        const bool ok = MountDecodedRivePackage(std::move(pkgCopy));
        if (ok) loadedSourceUri_ = std::string(sourceUri);
        reloadingSource_ = false;
        return ok;
    }

    std::vector<std::uint8_t> rawBytes;
    if (!detail::ResolveAndReadRiveAsset(sourceUri, rawBytes)) {
        reloadingSource_ = false;
        return false;
    }

    auto decodedPkg = std::make_shared<AxbPackage>();
    if (!DecodeRiveBinaryToAxbPackage(rawBytes.data(), rawBytes.size(), *decodedPkg, nullptr)) {
        reloadingSource_ = false;
        return false;
    }

    const bool ok = MountDecodedRivePackage(std::move(decodedPkg));
    if (ok) {
        loadedSourceUri_ = std::string(sourceUri);
    }
    reloadingSource_ = false;
    return ok;
}

bool RiveControl::MountDecodedRivePackage(std::shared_ptr<AxbPackage> pkg) {
    if (!pkg || pkg->instructions.empty()) {
        return false;
    }

    AnimationEngine* activeAnim = externalAnimationEngine_;
    if (!activeAnim) {
        if (!ownedAnimationEngine_) {
            ownedAnimationEngine_ = std::make_unique<AnimationEngine>(64u);
        } else {
            ownedAnimationEngine_->Clear();
        }
        activeAnim = ownedAnimationEngine_.get();
    } else if (artboardRoot_) {
        (void)activeAnim->StopSubtreeAnimations(artboardRoot_.Get(), false);
    }

    while (FirstChild() != nullptr) {
        (void)RemoveChild(FirstChild());
    }
    artboardRoot_.Reset();

    mountedPackage_ = std::move(pkg);
    artboardRoot_ = MakeIntrusive<Canvas>();
    (void)AddChild(artboardRoot_.Get());

    const bool mounted = MountAxbTemplate(
        artboardRoot_.Get(),
        *mountedPackage_,
        nullptr,
        nullptr,
        {},
        nullptr,
        nullptr,
        activeAnim);
    if (!mounted) {
        artboardRoot_.Reset();
        mountedPackage_.reset();
        return false;
    }

    const float abW = (artboardRoot_->GetWidth() > 0.0f) ? artboardRoot_->GetWidth() : 256.0f;
    const float abH = (artboardRoot_->GetHeight() > 0.0f) ? artboardRoot_->GetHeight() : 256.0f;
    artboardSize_ = Size{abW, abH};

    // Prime initial keyframe pose at t=0 so visual properties match entry state immediately
    if (activeAnim && mountedPackage_->HasMotionSection()) {
        (void)activeAnim->EvaluateStateMachines(0.0f);
        (void)activeAnim->Tick(0.0f);
    }

    InvalidateLayout();
    InvalidateRender();
    return true;
}

bool RiveControl::SetInputFloat(std::string_view inputName, float value) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return false;
    }
    const bool ok = anim->SetStateMachineInputFloatByName(artboardRoot_.Get(), inputName, value);
    if (ok) {
        (void)anim->EvaluateStateMachines(0.0f);
        InvalidateRender();
    }
    return ok;
}

bool RiveControl::SetInputBool(std::string_view inputName, bool value) noexcept {
    return SetInputFloat(inputName, value ? 1.0f : 0.0f);
}

bool RiveControl::FireTrigger(std::string_view triggerName) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return false;
    }
    const bool ok = anim->FireStateMachineTriggerByName(artboardRoot_.Get(), triggerName);
    if (ok) {
        (void)anim->EvaluateStateMachines(0.0f);
        InvalidateRender();
    }
    return ok;
}

float RiveControl::GetInputFloat(std::string_view inputName) const noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return 0.0f;
    }
    return anim->GetStateMachineInputFloatByName(artboardRoot_.Get(), inputName);
}

std::size_t RiveControl::Advance(float dtSeconds) noexcept {
    AnimationEngine* anim = ActiveAnimationEngine();
    if (!anim || !artboardRoot_) {
        return 0u;
    }
    (void)anim->EvaluateStateMachines(dtSeconds);
    return anim->Tick(dtSeconds);
}

void RiveControl::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    Control::OnPropertyChanged(propId, newValue);
    if (propId == PropId_RiveSource || propId == PropId_ImageSource) {
        if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            if (!sv->view().empty()) {
                (void)ReloadFromSourceUri(sv->view());
            }
        }
        return;
    }
    if (propId >= PropId_FirstContractInput &&
        propId < static_cast<std::uint16_t>(PropId_FirstContractInput + 0x1000u) &&
        artboardRoot_) {
        AnimationEngine* anim = ActiveAnimationEngine();
        if (!anim) {
            return;
        }
        const auto slotIdx = static_cast<std::uint16_t>(propId - PropId_FirstContractInput);
        float fv = 0.0f;
        if (TryGetFloat(newValue, fv)) {
            (void)anim->SetStateMachineInputFloat(artboardRoot_.Get(), slotIdx, fv);
            (void)anim->EvaluateStateMachines(0.0f);
        } else if (const auto* bv = std::get_if<bool>(&newValue)) {
            (void)anim->SetStateMachineInputBool(artboardRoot_.Get(), slotIdx, *bv);
            (void)anim->EvaluateStateMachines(0.0f);
        } else if (const auto* sv = std::get_if<StringValue>(&newValue)) {
            const float parsed = (sv->view() == "true" || sv->view() == "True" || sv->view() == "1")
                ? 1.0f
                : 0.0f;
            (void)anim->SetStateMachineInputFloat(artboardRoot_.Get(), slotIdx, parsed);
            (void)anim->EvaluateStateMachines(0.0f);
        }
    }
}

Size RiveControl::MeasureCore(Size availableSize) noexcept {
    if (!artboardRoot_) {
        return Control::MeasureCore(availableSize);
    }

    const Thickness p = GetPadding();
    const float padW = p.left + p.right;
    const float padH = p.top + p.bottom;
    const float innerAvailW = std::max(0.0f, availableSize.width - padW);
    const float innerAvailH = std::max(0.0f, availableSize.height - padH);

    artboardRoot_->Measure(Size{detail::kUnconstrainedSpace, detail::kUnconstrainedSpace});
    const float natW = (artboardSize_.width > 0.0f) ? artboardSize_.width : artboardRoot_->DesiredSize().width;
    const float natH = (artboardSize_.height > 0.0f) ? artboardSize_.height : artboardRoot_->DesiredSize().height;
    const bool hasW = std::isfinite(availableSize.width) && (availableSize.width < detail::kUnconstrainedSpace * 0.5f);
    const bool hasH = std::isfinite(availableSize.height) && (availableSize.height < detail::kUnconstrainedSpace * 0.5f);

    float sx = 1.0f;
    float sy = 1.0f;
    detail::ComputeViewboxScaleFactors(
        natW,
        natH,
        innerAvailW,
        innerAvailH,
        hasW,
        hasH,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    return Size{natW * sx + padW, natH * sy + padH};
}

void RiveControl::ArrangeCore(Rect finalRect) noexcept {
    if (!artboardRoot_) {
        Control::ArrangeCore(finalRect);
        return;
    }

    const Thickness p = GetPadding();
    const float startX = finalRect.x + p.left;
    const float startY = finalRect.y + p.top;
    const float innerW = std::max(0.0f, finalRect.width - (p.left + p.right));
    const float innerH = std::max(0.0f, finalRect.height - (p.top + p.bottom));

    const float natW = (artboardSize_.width > 0.0f) ? artboardSize_.width : artboardRoot_->DesiredSize().width;
    const float natH = (artboardSize_.height > 0.0f) ? artboardSize_.height : artboardRoot_->DesiredSize().height;

    float sx = 1.0f;
    float sy = 1.0f;
    detail::ComputeViewboxScaleFactors(
        natW,
        natH,
        innerW,
        innerH,
        true,
        true,
        GetStretch(),
        GetStretchDirection(),
        sx,
        sy);

    const float scaledW = natW * sx;
    const float scaledH = natH * sy;
    const float newOriginX = startX + (innerW - scaledW) * 0.5f;
    const float newOriginY = startY + (innerH - scaledH) * 0.5f;

    const bool scaleChanged =
        (std::fabs(childScaleX_ - sx) > 1e-5f) ||
        (std::fabs(childScaleY_ - sy) > 1e-5f) ||
        (std::fabs(childOriginX_ - newOriginX) > 1e-3f) ||
        (std::fabs(childOriginY_ - newOriginY) > 1e-3f);

    childScaleX_ = sx;
    childScaleY_ = sy;
    childOriginX_ = newOriginX;
    childOriginY_ = newOriginY;

    artboardRoot_->Arrange(Rect{newOriginX, newOriginY, natW, natH});

    if (scaleChanged) {
        InvalidateRender();
        detail::MarkSubtreeRenderDirtyRecursive(artboardRoot_.Get());
    }
}

// ============================================================================
// MediaElement
// ============================================================================

AERO_IMPLEMENT_CLASS(MediaElement, UIElement, "Aero.UI.MediaElement")

MediaElement::MediaElement() {
    EnsureControlTypesRegistered();
    (void)SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(Stretch::Uniform)), ValueSourceRank::Default);
    (void)SetValue(PropId_LoadedBehavior, value::FromString("Play"), ValueSourceRank::Default);
    (void)SetValue(PropId_UnloadedBehavior, value::FromString("Close"), ValueSourceRank::Default);
    (void)SetValue(PropId_Volume, MakeFloat(1.0f), ValueSourceRank::Default);
    (void)SetValue(PropId_IsMuted, UnifiedValue{false}, ValueSourceRank::Default);
    (void)SetValue(PropId_MediaPosition, MakeFloat(0.0f), ValueSourceRank::Default);
}

void MediaElement::SetSource(std::string_view sourceUri) {
    (void)SetValue(PropId_ImageSource, value::FromString(std::string(sourceUri)), ValueSourceRank::Local);
}

std::string MediaElement::GetSource() const {
    const UnifiedValue v = GetValue(PropId_ImageSource);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return {};
}

void MediaElement::SetStretch(Stretch stretch) {
    (void)SetValue(PropId_Stretch, value::FromInt(static_cast<std::int64_t>(stretch)), ValueSourceRank::Local);
}

Stretch MediaElement::GetStretch() const noexcept {
    return ReadStretchProperty(*this);
}

void MediaElement::SetLoadedBehavior(std::string_view behavior) {
    (void)SetValue(PropId_LoadedBehavior, value::FromString(std::string(behavior)), ValueSourceRank::Local);
}

std::string MediaElement::GetLoadedBehavior() const {
    const UnifiedValue v = GetValue(PropId_LoadedBehavior);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return "Play";
}

void MediaElement::SetUnloadedBehavior(std::string_view behavior) {
    (void)SetValue(PropId_UnloadedBehavior, value::FromString(std::string(behavior)), ValueSourceRank::Local);
}

std::string MediaElement::GetUnloadedBehavior() const {
    const UnifiedValue v = GetValue(PropId_UnloadedBehavior);
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        return sv->str();
    }
    return "Close";
}

void MediaElement::SetVolume(float volume) {
    (void)SetValue(PropId_Volume, MakeFloat(std::clamp(volume, 0.0f, 1.0f)), ValueSourceRank::Local);
}

float MediaElement::GetVolume() const noexcept {
    float v = 1.0f;
    if (TryGetFloat(GetValue(PropId_Volume), v)) {
        return std::clamp(v, 0.0f, 1.0f);
    }
    return 1.0f;
}

void MediaElement::SetIsMuted(bool muted) {
    (void)SetValue(PropId_IsMuted, UnifiedValue{muted}, ValueSourceRank::Local);
}

bool MediaElement::GetIsMuted() const noexcept {
    const UnifiedValue v = GetValue(PropId_IsMuted);
    if (const auto* bv = std::get_if<bool>(&v)) {
        return *bv;
    }
    if (const auto* iv = std::get_if<std::int64_t>(&v)) {
        return *iv != 0;
    }
    return false;
}

void MediaElement::SetPosition(float seconds) {
    const float clamped = std::max(0.0f, seconds);
    positionSeconds_ = clamped;
    (void)SetValue(PropId_MediaPosition, MakeFloat(clamped), ValueSourceRank::Local);
    InvalidateVisual();
}

float MediaElement::GetPosition() const noexcept {
    return positionSeconds_;
}

void MediaElement::Play() {
    isPlaying_ = true;
    InvalidateVisual();
}

void MediaElement::Pause() {
    isPlaying_ = false;
    InvalidateVisual();
}

void MediaElement::Stop() {
    isPlaying_ = false;
    positionSeconds_ = 0.0f;
    InvalidateVisual();
}

void MediaElement::SetDurationSeconds(float duration) {
    durationSeconds_ = std::max(0.0f, duration);
}

void MediaElement::OnPropertyChanged(std::uint16_t propId, const UnifiedValue& newValue) {
    UIElement::OnPropertyChanged(propId, newValue);
    if (propId == PropId_LoadedBehavior || propId == PropId_ImageSource) {
        const std::string behavior = GetLoadedBehavior();
        if (behavior == "Manual" || behavior == "Pause") {
            isPlaying_ = false;
        } else if (behavior == "Stop" || behavior == "Close") {
            isPlaying_ = false;
            positionSeconds_ = 0.0f;
        } else if (!GetSource().empty()) {
            isPlaying_ = true;
        }
        InvalidateVisual();
    } else if (propId == PropId_MediaPosition) {
        float pos = 0.0f;
        if (TryGetFloat(newValue, pos)) {
            positionSeconds_ = std::max(0.0f, pos);
            InvalidateVisual();
        }
    } else if (propId == PropId_Volume || propId == PropId_IsMuted) {
        InvalidateVisual();
    }
}

void MediaElement::TickControl(float dtSeconds) {
    if (!isPlaying_ || dtSeconds <= 0.0f) {
        return;
    }
    positionSeconds_ += dtSeconds;
    if (durationSeconds_ > 0.0f && positionSeconds_ >= durationSeconds_) {
        positionSeconds_ = durationSeconds_;
        isPlaying_ = false;
        ++mediaEndedGeneration_;
        if (mediaEndedCallback_) {
            mediaEndedCallback_(*this);
        }
    }
    InvalidateVisual();
}

Size MediaElement::MeasureCore(Size availableSize) noexcept {
    const float w = (GetWidth() > 0.0f) ? GetWidth() : 0.0f;
    const float h = (GetHeight() > 0.0f) ? GetHeight() : 0.0f;
    return Size{
        std::min(availableSize.width, w),
        std::min(availableSize.height, h)};
}

void TextBlock::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "text";
    ioInfo.value = GetText();
}

bool TextBlock::TryGetRichTextDisplayString(std::string& outText) const {
    outText = GetText();
    return true;
}

void Label::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "text";
    ioInfo.value = GetText();
    if (ioInfo.value.empty()) {
        ioInfo.value = GetContentText();
    }
}

bool Label::TryGetRichTextDisplayString(std::string& outText) const {
    outText = GetText();
    return true;
}

void TextBoxBase::PopulateAriaInfo(AriaElementInfo& ioInfo) const {
    UIElement::PopulateAriaInfo(ioInfo);
    ioInfo.role = "textbox";
    ioInfo.value = GetDisplayString();
}

} // namespace aero
