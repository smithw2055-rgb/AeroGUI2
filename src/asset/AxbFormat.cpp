#include "aero/asset/AxbFormat.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <variant>

#include "aero/core/StringUtils.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

namespace {

enum class AxbLiteralTag : std::uint8_t {
    Empty = 0,
    Bool = 1,
    Int64 = 2,
    Float32 = 3,
    Float4 = 4,
    String = 5,
};

inline constexpr std::uint32_t kAxb4MtsdfSectionMagic = 0x4644534Du;          // "MSDF"
inline constexpr std::uint32_t kAxb4StyleAnimSectionMagic = 0x35435453u;      // "STC5"
inline constexpr std::uint32_t kAxb4Phase5DSectionMagic = 0x35505444u;        // "DTP5"
inline constexpr std::uint32_t kAxb4P15SectionMagic = 0x35315041u;            // "AP15"
inline constexpr std::uint32_t kAxb4SetterActionBindSectionMagic = 0x34414253u; // "SBA4"
inline constexpr std::uint32_t kAxb4TargetNullExtSectionMagic = 0x34454E54u;   // "TNE4"
inline constexpr std::uint32_t kAxb4EventTrigExtSectionMagic = 0x34455445u;   // "ETE4"

void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t v) {
    out.push_back(v);
}

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
    }
}

void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
    }
}

void WriteF32(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(float));
    WriteU32(out, bits);
}

void WriteString(std::vector<std::uint8_t>& out, std::string_view s) {
    WriteU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void WriteValue(std::vector<std::uint8_t>& out, const UnifiedValue& val) {
    if (const auto* b = std::get_if<bool>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Bool));
        WriteU8(out, *b ? 1u : 0u);
    } else if (const auto* i = std::get_if<std::int64_t>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Int64));
        WriteU64(out, static_cast<std::uint64_t>(*i));
    } else if (const auto* f = std::get_if<FloatValue>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Float32));
        WriteF32(out, f->value);
    } else if (Float4Value f4{}; val.tryGetQuatf(f4)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Float4));
        WriteF32(out, f4.x);
        WriteF32(out, f4.y);
        WriteF32(out, f4.z);
        WriteF32(out, f4.w);
    } else if (const auto* s = std::get_if<StringValue>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::String));
        WriteString(out, s->view());
    } else {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Empty));
    }
}

void WriteHopVector(std::vector<std::uint8_t>& out, const std::vector<AxbCompiledHop>& hops) {
    WriteU32(out, static_cast<std::uint32_t>(hops.size()));
    for (const auto& hop : hops) {
        const std::string wireMemberName =
            (hop.collectionIndex >= 0 && hop.memberName.find('[') == std::string::npos)
                ? (hop.memberName + "[" + std::to_string(hop.collectionIndex) + "]")
                : hop.memberName;
        WriteString(out, wireMemberName);
        WriteU64(out, hop.declaringTypeId);
        WriteU32(out, hop.fieldIndex);
        WriteU8(out, hop.observableBitIndex);
        WriteU64(out, hop.getterSymbolId);
        WriteU64(out, hop.setterSymbolId);
        std::uint8_t hopFlags = 0u;
        if (hop.isAutoProperty) hopFlags |= 0x01u;
        if (hop.isObservable) hopFlags |= 0x02u;
        if (hop.isReadOnly) hopFlags |= 0x04u;
        if (hop.isSimState) hopFlags |= 0x08u;
        if (hop.isSimCommand) hopFlags |= 0x10u;
        WriteU8(out, hopFlags);
    }
}

void WriteSetterList(std::vector<std::uint8_t>& out, const std::vector<AxbPropertySetter>& setters) {
    WriteU32(out, static_cast<std::uint32_t>(setters.size()));
    for (const auto& st : setters) {
        WriteString(out, st.targetName);
        WriteU16(out, st.propId);
        WriteValue(out, st.value);
    }
}

void WriteStoryboard(std::vector<std::uint8_t>& out, const AxbStoryboard& sb) {
    WriteString(out, sb.key);
    WriteU32(out, static_cast<std::uint32_t>(sb.timelines.size()));
    for (const auto& tl : sb.timelines) {
        WriteString(out, tl.targetName);
        WriteU16(out, tl.targetPropertyId);
        WriteF32(out, tl.beginTimeSeconds);
        WriteF32(out, tl.durationSeconds);
        const bool hasRatios =
            (std::fabs(tl.speedRatio - 1.0f) > 1e-5f) ||
            (tl.accelerationRatio > 1e-5f) ||
            (tl.decelerationRatio > 1e-5f);
        const bool hasCustomAnim =
            (tl.customAnim.animTypeId != 0u) ||
            (tl.customAnim.reserved != 0u) ||
            (tl.customAnim.flags != 0u) ||
            (std::fabs(tl.customAnim.params[0]) > 1e-6f) ||
            (std::fabs(tl.customAnim.params[1]) > 1e-6f) ||
            (std::fabs(tl.customAnim.params[2]) > 1e-6f) ||
            (std::fabs(tl.customAnim.params[3]) > 1e-6f);
        std::uint8_t tlFlags = 0u;
        if (tl.hasFrom) tlFlags |= 0x01u;
        if (tl.hasTo) tlFlags |= 0x02u;
        if (tl.autoReverse) tlFlags |= 0x04u;
        if (tl.fillBehavior == FillBehavior::Stop) tlFlags |= 0x08u;
        if (tl.isBy) tlFlags |= 0x10u;
        if (hasRatios) tlFlags |= 0x20u;
        if (hasCustomAnim) tlFlags |= 0x40u;
        WriteU8(out, tlFlags);
        if (hasRatios) {
            WriteF32(out, tl.speedRatio);
            WriteF32(out, tl.accelerationRatio);
            WriteF32(out, tl.decelerationRatio);
        }
        if (hasCustomAnim) {
            WriteU16(out, tl.customAnim.animTypeId);
            WriteU16(out, tl.customAnim.reserved);
            WriteU32(out, tl.customAnim.flags);
            for (std::size_t pi = 0u; pi < kMaxCustomAnimationParams; ++pi) {
                WriteF32(out, tl.customAnim.params[pi]);
            }
        }
        WriteValue(out, tl.fromValue);
        WriteValue(out, tl.toValue);
        WriteU8(out, static_cast<std::uint8_t>(tl.easingKind));
        WriteU8(out, static_cast<std::uint8_t>(tl.easingMode));
        WriteU32(out, static_cast<std::uint32_t>(tl.repeatCount));
        WriteU32(out, static_cast<std::uint32_t>(tl.keyFrames.size()));
        for (const auto& kf : tl.keyFrames) {
            WriteF32(out, kf.timeSeconds);
            WriteValue(out, kf.value);
            WriteU8(out, static_cast<std::uint8_t>(kf.easingKind));
            WriteU8(out, static_cast<std::uint8_t>(kf.easingMode));
            std::uint8_t kfFlags = kf.isDiscrete ? 0x01u : 0x00u;
            if (kf.hasKeySpline) kfFlags |= 0x02u;
            WriteU8(out, kfFlags);
            if (kf.hasKeySpline) {
                WriteF32(out, kf.splineX1);
                WriteF32(out, kf.splineY1);
                WriteF32(out, kf.splineX2);
                WriteF32(out, kf.splineY2);
            }
        }
    }
}

struct ByteReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;

    bool ReadU8(std::uint8_t& out) noexcept {
        if (offset + 1 > size) return false;
        out = data[offset++];
        return true;
    }

    bool ReadU16(std::uint16_t& out) noexcept {
        if (offset + 2 > size) return false;
        out = static_cast<std::uint16_t>(data[offset]) |
              static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8u);
        offset += 2;
        return true;
    }

    bool ReadU32(std::uint32_t& out) noexcept {
        if (offset + 4 > size) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            out |= (static_cast<std::uint32_t>(data[offset + i]) << (i * 8));
        }
        offset += 4;
        return true;
    }

    bool ReadU64(std::uint64_t& out) noexcept {
        if (offset + 8 > size) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= (static_cast<std::uint64_t>(data[offset + i]) << (i * 8));
        }
        offset += 8;
        return true;
    }

    bool ReadF32(float& out) noexcept {
        std::uint32_t bits = 0;
        if (!ReadU32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }

    bool ReadString(std::string& out) {
        std::uint32_t len = 0;
        if (!ReadU32(len) || len > size - offset) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
    }

    bool ReadBytes(std::vector<std::uint8_t>& out) {
        std::uint32_t len = 0;
        if (!ReadU32(len) || len > size - offset) return false;
        out.assign(data + offset, data + offset + len);
        offset += len;
        return true;
    }

    bool ReadValue(UnifiedValue& out) {
        std::uint8_t tagRaw = 0;
        if (!ReadU8(tagRaw)) return false;
        switch (static_cast<AxbLiteralTag>(tagRaw)) {
            case AxbLiteralTag::Empty:
                out = std::monostate{};
                return true;
            case AxbLiteralTag::Bool: {
                std::uint8_t b = 0;
                if (!ReadU8(b)) return false;
                out = (b != 0);
                return true;
            }
            case AxbLiteralTag::Int64: {
                std::uint64_t v = 0;
                if (!ReadU64(v)) return false;
                out = static_cast<std::int64_t>(v);
                return true;
            }
            case AxbLiteralTag::Float32: {
                float f = 0.0f;
                if (!ReadF32(f)) return false;
                out = MakeFloat(f);
                return true;
            }
            case AxbLiteralTag::Float4: {
                Float4Value f4{};
                if (!ReadF32(f4.x) || !ReadF32(f4.y) || !ReadF32(f4.z) || !ReadF32(f4.w)) {
                    return false;
                }
                out = UnifiedValue{f4};
                if (out.isTraceable()) {
                    rs_rt::pinValueInPools(out);
                }
                return true;
            }
            case AxbLiteralTag::String: {
                std::string s;
                if (!ReadString(s)) return false;
                out = value::FromString(std::move(s));
                if (out.isTraceable()) {
                    rs_rt::pinValueInPools(out);
                }
                return true;
            }
        }
        return false;
    }
};

bool ReadHopVector(ByteReader& r, std::vector<AxbCompiledHop>& hops) {
    std::uint32_t hopCount = 0;
    if (!r.ReadU32(hopCount) || hopCount > (r.size - r.offset) / 34u) {
        return false;
    }
    hops.resize(hopCount);
    for (std::uint32_t h = 0; h < hopCount; ++h) {
        auto& hop = hops[h];
        std::uint8_t hopFlags = 0u;
        if (!r.ReadString(hop.memberName) ||
            !r.ReadU64(hop.declaringTypeId) ||
            !r.ReadU32(hop.fieldIndex) ||
            !r.ReadU8(hop.observableBitIndex) ||
            !r.ReadU64(hop.getterSymbolId) ||
            !r.ReadU64(hop.setterSymbolId) ||
            !r.ReadU8(hopFlags)) {
            return false;
        }
        hop.isAutoProperty = (hopFlags & 0x01u) != 0u;
        hop.isObservable = (hopFlags & 0x02u) != 0u;
        hop.isReadOnly = (hopFlags & 0x04u) != 0u;
        hop.isSimState = (hopFlags & 0x08u) != 0u;
        hop.isSimCommand = (hopFlags & 0x10u) != 0u;
        std::string baseName;
        std::int32_t parsedIdx = -1;
        if (SplitIndexedMemberName(hop.memberName, baseName, parsedIdx)) {
            hop.memberName = std::move(baseName);
            hop.collectionIndex = parsedIdx;
        }
    }
    return true;
}

struct CustomAnimationStorageEntry {
    std::string typeNameStorage;
    std::string shortNameStorage;
    CustomAnimationSpec spec{};
};

struct CustomAnimationRegistryState {
    std::mutex mutex;
    std::deque<CustomAnimationStorageEntry> entries;
    std::unordered_map<std::string, std::uint16_t> byExactName;
    std::unordered_map<std::string, std::uint16_t> byShortName;
};

CustomAnimationRegistryState& GetCustomAnimationRegistryState() noexcept {
    static CustomAnimationRegistryState sState;
    return sState;
}

} // namespace

std::uint16_t RegisterCustomAnimationType(const CustomAnimationSpec& spec) {
    const std::string_view cleanType = str::Trim(spec.typeName);
    const std::string_view shortType = str::StripNamespacePrefix(cleanType);
    if (cleanType.empty() || shortType.empty()) {
        return 0u;
    }

    auto& state = GetCustomAnimationRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (const auto it = state.byExactName.find(std::string(cleanType));
        it != state.byExactName.end()) {
        return it->second;
    }
    if (const auto it = state.byShortName.find(std::string(shortType));
        it != state.byShortName.end()) {
        return it->second;
    }
    if (state.entries.size() >= 0xFFFFu) {
        return 0u;
    }

    const std::uint16_t assignedId = static_cast<std::uint16_t>(state.entries.size() + 1u);
    CustomAnimationStorageEntry entry{};
    entry.typeNameStorage.assign(cleanType.data(), cleanType.size());
    entry.shortNameStorage.assign(shortType.data(), shortType.size());
    entry.spec = spec;
    entry.spec.typeName = entry.typeNameStorage;
    entry.spec.defaultParams.animTypeId = assignedId;
    entry.spec.defaultParams.reserved = 0u;

    state.entries.push_back(std::move(entry));
    state.byExactName.emplace(std::string(cleanType), assignedId);
    state.byShortName.try_emplace(std::string(shortType), assignedId);
    return assignedId;
}

const CustomAnimationSpec* FindCustomAnimationType(std::string_view typeName) noexcept {
    const std::string_view cleanType = str::Trim(typeName);
    if (cleanType.empty()) {
        return nullptr;
    }
    auto& state = GetCustomAnimationRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (const auto& entry : state.entries) {
        if (entry.typeNameStorage == cleanType) {
            return &entry.spec;
        }
    }
    const std::string_view shortType = str::StripNamespacePrefix(cleanType);
    for (const auto& entry : state.entries) {
        if (entry.shortNameStorage == shortType) {
            return &entry.spec;
        }
    }
    return nullptr;
}

const CustomAnimationSpec* GetCustomAnimationTypeById(std::uint16_t animTypeId) noexcept {
    if (animTypeId == 0u) {
        return nullptr;
    }
    auto& state = GetCustomAnimationRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    const std::size_t idx = static_cast<std::size_t>(animTypeId - 1u);
    if (idx >= state.entries.size()) {
        return nullptr;
    }
    return &state.entries[idx].spec;
}

rs_rt::NativeHandleRegistry& FallbackVisualHandleRegistry() noexcept {
    static rs_rt::NativeHandleRegistry sReg;
    return sReg;
}

bool DecodeNextUtf8CodePoint(
    std::string_view utf8Text,
    std::size_t& ioCursor,
    std::uint32_t& outCodePoint) noexcept {
    if (ioCursor >= utf8Text.size()) {
        return false;
    }
    const auto b0 = static_cast<std::uint8_t>(utf8Text[ioCursor]);
    if ((b0 & 0x80u) == 0u) {
        outCodePoint = b0;
        ioCursor += 1u;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u && b0 >= 0xC2u && ioCursor + 1u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        if ((b1 & 0xC0u) == 0x80u) {
            outCodePoint = (static_cast<std::uint32_t>(b0 & 0x1Fu) << 6u) | (b1 & 0x3Fu);
            ioCursor += 2u;
            return true;
        }
    } else if ((b0 & 0xF0u) == 0xE0u && ioCursor + 2u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
            const std::uint32_t cp =
                (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12u) |
                (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6u) |
                (b2 & 0x3Fu);
            if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) {
                outCodePoint = cp;
                ioCursor += 3u;
                return true;
            }
        }
    } else if ((b0 & 0xF8u) == 0xF0u && b0 <= 0xF4u && ioCursor + 3u < utf8Text.size()) {
        const auto b1 = static_cast<std::uint8_t>(utf8Text[ioCursor + 1u]);
        const auto b2 = static_cast<std::uint8_t>(utf8Text[ioCursor + 2u]);
        const auto b3 = static_cast<std::uint8_t>(utf8Text[ioCursor + 3u]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
            const std::uint32_t cp =
                (static_cast<std::uint32_t>(b0 & 0x07u) << 18u) |
                (static_cast<std::uint32_t>(b1 & 0x3Fu) << 12u) |
                (static_cast<std::uint32_t>(b2 & 0x3Fu) << 6u) |
                (b3 & 0x3Fu);
            if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                outCodePoint = cp;
                ioCursor += 4u;
                return true;
            }
        }
    }
    outCodePoint = 0xFFFDu;
    ioCursor += 1u;
    return true;
}

bool IsWideCjkCodePoint(std::uint32_t cp) noexcept {
    return (cp >= 0x2E80u && cp <= 0x9FFFu) ||
           (cp >= 0xAC00u && cp <= 0xD7AFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu) ||
           (cp >= 0xFF01u && cp <= 0xFF60u) ||
           (cp >= 0x20000u && cp <= 0x2FA1Fu);
}

Size MeasureUtf8TextRun(
    std::string_view utf8Text,
    float fontSize,
    std::string_view fontFamily,
    float maxWidth,
    TextWrapping wrapMode) noexcept {
    if (utf8Text.empty()) {
        return Size{0.0f, 0.0f};
    }
    if (!fontFamily.empty() || wrapMode != TextWrapping::NoWrap ||
        !FontAtlas::Instance().DefaultFontFamily().empty()) {
        const TextLayoutResult res = MeasureTextLayout(utf8Text, fontFamily, fontSize, maxWidth, wrapMode);
        return Size{res.totalWidth, res.totalHeight};
    }
    return MeasureUtf8TextRun(utf8Text, fontSize);
}

Size MeasureUtf8TextRun(std::string_view utf8Text, float fontSize) noexcept {
    if (utf8Text.empty()) {
        return Size{0.0f, 0.0f};
    }
    if (!FontAtlas::Instance().DefaultFontFamily().empty()) {
        const TextLayoutResult res = MeasureTextLayout(utf8Text, {}, fontSize, 1000000.0f, TextWrapping::NoWrap);
        return Size{res.totalWidth, res.totalHeight};
    }
    const float safeFont = std::max(1.0f, fontSize);
    float maxLineW = 0.0f;
    float curLineW = 0.0f;
    std::size_t lineCount = 1u;
    std::size_t cursor = 0;
    std::uint32_t cp = 0;
    bool anyCp = false;
    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        anyCp = true;
        if (cp == '\r') {
            continue;
        }
        if (cp == '\n') {
            maxLineW = std::max(maxLineW, curLineW);
            curLineW = 0.0f;
            ++lineCount;
            continue;
        }
        curLineW += IsWideCjkCodePoint(cp) ? (safeFont * 0.95f) : (safeFont * 0.6f);
    }
    if (!anyCp) {
        return Size{0.0f, 0.0f};
    }
    maxLineW = std::max(maxLineW, curLineW);
    const float totalH = (lineCount == 1u)
        ? (safeFont * 1.25f)
        : (static_cast<float>(lineCount) * safeFont * 1.2f);
    return Size{maxLineW, totalH};
}

std::size_t CountVisibleUtf8Glyphs(std::string_view utf8Text) noexcept {
    std::size_t count = 0;
    std::size_t cursor = 0;
    std::uint32_t cp = 0;
    while (DecodeNextUtf8CodePoint(utf8Text, cursor, cp)) {
        if (cp != ' ' && cp != '\r' && cp != '\n') {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// AxbPackage String Table, Hot-Reload Rebinding & Binary Serialization (.axb4)
// ============================================================================

std::uint32_t AxbPackage::InternString(std::string_view text) {
    if (stringTable.empty()) {
        stringTable.emplace_back("");
    }
    if (text.empty()) {
        return 0;
    }
    for (std::size_t i = 1; i < stringTable.size(); ++i) {
        if (stringTable[i] == text) {
            return static_cast<std::uint32_t>(i);
        }
    }
    const auto idx = static_cast<std::uint32_t>(stringTable.size());
    stringTable.emplace_back(text);
    return idx;
}

std::string_view AxbPackage::GetString(std::uint32_t index) const noexcept {
    if (index < stringTable.size()) {
        return stringTable[index];
    }
    return {};
}

namespace {

template <typename Fn>
void VisitPackageValues(const AxbPackage& pkg, Fn&& fn) noexcept {
    for (const auto& inst : pkg.instructions) {
        if (inst.literalValue.isTraceable()) {
            fn(inst.literalValue);
        }
    }
    for (const auto& res : pkg.resources) {
        if (res.second.isTraceable()) {
            fn(res.second);
        }
    }
    auto visitBindingChain = [&](auto& selfMulti, const AxbCompiledBindingChain& ch) noexcept -> void {
        if (ch.fallbackValue.isTraceable()) {
            fn(ch.fallbackValue);
        }
        if (ch.targetNullValue.isTraceable()) {
            fn(ch.targetNullValue);
        }
        if (ch.nestedMultiBinding) {
            selfMulti(selfMulti, *ch.nestedMultiBinding);
        }
    };
    auto visitMultiBindingRec = [&](auto& self, const AxbCompiledMultiBinding& mb) noexcept -> void {
        if (mb.fallbackValue.isTraceable()) {
            fn(mb.fallbackValue);
        }
        for (const auto& srcCh : mb.sources) {
            visitBindingChain(self, srcCh);
        }
    };
    for (const auto& ch : pkg.bindingChains) {
        visitBindingChain(visitMultiBindingRec, ch);
    }
    for (const auto& mb : pkg.multiBindings) {
        visitMultiBindingRec(visitMultiBindingRec, mb);
    }
    auto visitStoryboards = [&](const std::vector<AxbStoryboard>& sbs) noexcept {
        for (const auto& sb : sbs) {
            for (const auto& tl : sb.timelines) {
                if (tl.fromValue.isTraceable()) fn(tl.fromValue);
                if (tl.toValue.isTraceable()) fn(tl.toValue);
                for (const auto& kf : tl.keyFrames) {
                    if (kf.value.isTraceable()) fn(kf.value);
                }
            }
        }
    };
    visitStoryboards(pkg.storyboards);
    auto visitInteractionActions = [&](const std::vector<AxbInteractionAction>& acts) noexcept {
        for (const auto& act : acts) {
            if (act.value.isTraceable()) fn(act.value);
            if (act.valueBinding && act.valueBinding->fallbackValue.isTraceable()) {
                fn(act.valueBinding->fallbackValue);
            }
            if (act.valueBinding && act.valueBinding->targetNullValue.isTraceable()) {
                fn(act.valueBinding->targetNullValue);
            }
            if (act.valueMultiBinding) {
                visitMultiBindingRec(visitMultiBindingRec, *act.valueMultiBinding);
            }
            if (act.commandParameter.isTraceable()) fn(act.commandParameter);
            if (act.commandParameterBinding.fallbackValue.isTraceable()) {
                fn(act.commandParameterBinding.fallbackValue);
            }
            if (act.commandParameterBinding.targetNullValue.isTraceable()) {
                fn(act.commandParameterBinding.targetNullValue);
            }
            if (act.commandParameterBinding.nestedMultiBinding) {
                visitMultiBindingRec(visitMultiBindingRec, *act.commandParameterBinding.nestedMultiBinding);
            }
            for (const auto& tl : act.inlineStoryboard.timelines) {
                if (tl.fromValue.isTraceable()) fn(tl.fromValue);
                if (tl.toValue.isTraceable()) fn(tl.toValue);
                for (const auto& kf : tl.keyFrames) {
                    if (kf.value.isTraceable()) fn(kf.value);
                }
            }
        }
    };
    auto visitControlTemplate = [&](const AxbControlTemplate& ct) noexcept {
        if (ct.templatePackage) {
            VisitPackageValues(*ct.templatePackage, fn);
        }
        for (const auto& trig : ct.triggers) {
            for (const auto& cond : trig.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            for (const auto& setter : trig.setters) {
                if (setter.value.isTraceable()) fn(setter.value);
                if (setter.binding) visitBindingChain(visitMultiBindingRec, *setter.binding);
            }
            visitStoryboards(trig.enterStoryboards);
            visitStoryboards(trig.exitStoryboards);
            visitInteractionActions(trig.enterActions);
            visitInteractionActions(trig.exitActions);
        }
        for (const auto& et : ct.eventTriggers) {
            for (const auto& cond : et.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            visitStoryboards(et.storyboards);
            visitInteractionActions(et.actions);
        }
        for (const auto& vsg : ct.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& setter : vs.setters) {
                    if (setter.value.isTraceable()) fn(setter.value);
                    if (setter.binding) visitBindingChain(visitMultiBindingRec, *setter.binding);
                }
                for (const auto& tl : vs.storyboard.timelines) {
                    if (tl.fromValue.isTraceable()) fn(tl.fromValue);
                    if (tl.toValue.isTraceable()) fn(tl.toValue);
                    for (const auto& kf : tl.keyFrames) {
                        if (kf.value.isTraceable()) fn(kf.value);
                    }
                }
            }
        }
    };
    for (const auto& st : pkg.styles) {
        for (const auto& setter : st.setters) {
            if (setter.value.isTraceable()) fn(setter.value);
            if (setter.binding) visitBindingChain(visitMultiBindingRec, *setter.binding);
        }
        for (const auto& trig : st.triggers) {
            for (const auto& cond : trig.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            for (const auto& setter : trig.setters) {
                if (setter.value.isTraceable()) fn(setter.value);
                if (setter.binding) visitBindingChain(visitMultiBindingRec, *setter.binding);
            }
            visitStoryboards(trig.enterStoryboards);
            visitStoryboards(trig.exitStoryboards);
            visitInteractionActions(trig.enterActions);
            visitInteractionActions(trig.exitActions);
        }
        for (const auto& et : st.eventTriggers) {
            for (const auto& cond : et.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            visitStoryboards(et.storyboards);
            visitInteractionActions(et.actions);
        }
        for (const auto& vsg : st.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& setter : vs.setters) {
                    if (setter.value.isTraceable()) fn(setter.value);
                    if (setter.binding) visitBindingChain(visitMultiBindingRec, *setter.binding);
                }
                for (const auto& tl : vs.storyboard.timelines) {
                    if (tl.fromValue.isTraceable()) fn(tl.fromValue);
                    if (tl.toValue.isTraceable()) fn(tl.toValue);
                    for (const auto& kf : tl.keyFrames) {
                        if (kf.value.isTraceable()) fn(kf.value);
                    }
                }
            }
        }
        if (st.inlineControlTemplate) {
            visitControlTemplate(*st.inlineControlTemplate);
        }
    }
    for (const auto& ci : pkg.contractInputs) {
        if (ci.defaultValue.isTraceable()) {
            fn(ci.defaultValue);
        }
    }
    for (const auto& dt : pkg.dataTemplates) {
        if (dt.templatePackage) {
            VisitPackageValues(*dt.templatePackage, fn);
        }
    }
    for (const auto& ct : pkg.controlTemplates) {
        visitControlTemplate(ct);
    }
}

template <typename Package, typename Fn>
void VisitAllPackageSetters(Package& pkg, Fn&& fn) {
    for (auto& st : pkg.styles) {
        for (auto& setter : st.setters) fn(setter);
        for (auto& trig : st.triggers) {
            for (auto& setter : trig.setters) fn(setter);
        }
        for (auto& vsg : st.visualStateGroups) {
            for (auto& vs : vsg.states) {
                for (auto& setter : vs.setters) fn(setter);
            }
        }
    }
    for (auto& ct : pkg.controlTemplates) {
        for (auto& trig : ct.triggers) {
            for (auto& setter : trig.setters) fn(setter);
        }
        for (auto& vsg : ct.visualStateGroups) {
            for (auto& vs : vsg.states) {
                for (auto& setter : vs.setters) fn(setter);
            }
        }
    }
}

template <typename Package, typename Fn>
void VisitAllPackageActions(Package& pkg, Fn&& fn) {
    for (auto& st : pkg.styles) {
        for (auto& trig : st.triggers) {
            for (auto& act : trig.enterActions) fn(act);
            for (auto& act : trig.exitActions) fn(act);
        }
        for (auto& et : st.eventTriggers) {
            for (auto& act : et.actions) fn(act);
        }
    }
    for (auto& ct : pkg.controlTemplates) {
        for (auto& trig : ct.triggers) {
            for (auto& act : trig.enterActions) fn(act);
            for (auto& act : trig.exitActions) fn(act);
        }
        for (auto& et : ct.eventTriggers) {
            for (auto& act : et.actions) fn(act);
        }
    }
}

template <typename Package, typename Fn>
void VisitAllPackageEventTriggers(Package& pkg, Fn&& fn) {
    for (auto& st : pkg.styles) {
        for (auto& et : st.eventTriggers) fn(et);
    }
    for (auto& ct : pkg.controlTemplates) {
        for (auto& et : ct.eventTriggers) fn(et);
    }
}

template <typename Package, typename Fn>
void VisitAllNestedBindingChains(Package& pkg, Fn&& fn) {
    auto visitMulti = [&](auto& selfMulti, auto& selfChain, auto& mb) -> void {
        for (auto& srcCh : mb.sources) {
            selfChain(selfMulti, selfChain, srcCh);
        }
    };
    auto visitChain = [&](auto& selfMulti, auto& selfChain, auto& ch) -> void {
        fn(ch);
        if (ch.nestedMultiBinding) {
            selfMulti(selfMulti, selfChain, *ch.nestedMultiBinding);
        }
    };
    auto visitActions = [&](auto& acts) {
        for (auto& act : acts) {
            if (act.valueBinding) {
                visitChain(visitMulti, visitChain, *act.valueBinding);
            }
            if (act.valueMultiBinding) {
                visitMulti(visitMulti, visitChain, *act.valueMultiBinding);
            }
            if (act.hasCommandParameterBinding) {
                visitChain(visitMulti, visitChain, act.commandParameterBinding);
            } else {
                fn(act.commandParameterBinding);
            }
            if (act.targetObjectBinding) {
                visitChain(visitMulti, visitChain, *act.targetObjectBinding);
            }
        }
    };
    auto visitSetters = [&](auto& setters) {
        for (auto& setter : setters) {
            if (setter.binding) {
                visitChain(visitMulti, visitChain, *setter.binding);
            }
        }
    };

    for (auto& ch : pkg.bindingChains) {
        if (ch.nestedMultiBinding) {
            visitMulti(visitMulti, visitChain, *ch.nestedMultiBinding);
        }
    }
    for (auto& mb : pkg.multiBindings) {
        visitMulti(visitMulti, visitChain, mb);
    }
    for (auto& st : pkg.styles) {
        visitSetters(st.setters);
        for (auto& trig : st.triggers) {
            visitSetters(trig.setters);
            visitActions(trig.enterActions);
            visitActions(trig.exitActions);
        }
        for (auto& et : st.eventTriggers) {
            visitActions(et.actions);
        }
        for (auto& vsg : st.visualStateGroups) {
            for (auto& vs : vsg.states) {
                visitSetters(vs.setters);
            }
        }
    }
    for (auto& ct : pkg.controlTemplates) {
        for (auto& trig : ct.triggers) {
            visitSetters(trig.setters);
            visitActions(trig.enterActions);
            visitActions(trig.exitActions);
        }
        for (auto& et : ct.eventTriggers) {
            visitActions(et.actions);
        }
        for (auto& vsg : ct.visualStateGroups) {
            for (auto& vs : vsg.states) {
                visitSetters(vs.setters);
            }
        }
    }
}

} // namespace

void AxbPackage::PinValuesInPools() const noexcept {
    VisitPackageValues(*this, [](const UnifiedValue& v) noexcept {
        rs_rt::pinValueInPools(v);
    });
}

void AxbPackage::MarkValuesInPools() const noexcept {
    VisitPackageValues(*this, [](const UnifiedValue& v) noexcept {
        rs_rt::markValueInPools(v);
    });
}

bool AxbPackage::BindAgainstProgram(
    const ProgramImage& program,
    std::string_view rootDataTypeOverride,
    const ExecutionDomainContext* domains) {
    ExecutionDomainContext localDomains;
    if (!domains) {
        localDomains.registerProgramObservableMetadata(program);
        domains = &localDomains;
    }

    auto resolveTypeIdByName = [&](std::string_view rawName) -> SymbolId {
        std::string trimmed(str::Trim(rawName));
        if (trimmed.empty()) {
            return 0;
        }
        std::string canonical = trimmed;
        if (canonical.find("::") == std::string::npos) {
            if (const auto lastDot = canonical.rfind('.'); lastDot != std::string::npos) {
                canonical = canonical.substr(0, lastDot) + "::" + canonical.substr(lastDot + 1);
            }
        }
        SymbolId foundId = rs_sem::stableTypeId(canonical);
        if (program.resolveType(foundId)) {
            return foundId;
        }
        for (const auto& mod : program.modules()) {
            for (const auto& t : mod.types) {
                if (t.name == trimmed ||
                    (mod.name + "." + t.name) == trimmed ||
                    (mod.name + "::" + t.name) == trimmed ||
                    t.name == canonical ||
                    (mod.name + "::" + t.name) == canonical) {
                    return t.id;
                }
            }
        }
        return 0;
    };

    fieldsOwnerTypeId = 0;
    rootHandleFieldIndex = PropertySlotHandle::InvalidIndex;
    for (auto& nc : namedControls) {
        nc.fieldIndexInClass = PropertySlotHandle::InvalidIndex;
    }

    const std::string effectiveWrapperClassName = !className.empty()
        ? className
        : hostWrapperClassName;
    const SymbolId ownClassTypeId = !className.empty()
        ? resolveTypeIdByName(className)
        : 0;
    const rs_sem::TypeSymbol* preOwnClassSym =
        (ownClassTypeId != 0) ? program.resolveType(ownClassTypeId) : nullptr;

    auto inferDataContextTypeFromXClass = [&](const rs_sem::TypeSymbol* clsSym) -> std::string {
        if (!clsSym) {
            return {};
        }
        // 1. Inspect SetDataContext<T> specialization generated on this x:Class!
        for (const auto& m : clsSym->methods) {
            if (str::StartsWith(m.name, "SetDataContext__") && m.parameters.size() >= 2u) {
                const std::string& dcParamType = m.parameters[1].typeName;
                if (!dcParamType.empty() && resolveTypeIdByName(dcParamType) != 0) {
                    return dcParamType;
                }
            }
        }
        // 2. Inspect Aero.UI::GUI_SetDataContext__<Owner>__<TContext> in compiled modules
        for (const auto& mod : program.modules()) {
            for (const auto& fn : mod.functions) {
                if ((fn.name.find("GUI_SetDataContext__") != std::string::npos || fn.name.find("__Aero_SetDataContext__") != std::string::npos) &&
                    fn.parameterTypeIds.size() >= 2u &&
                    fn.parameterTypeIds[0] == clsSym->id &&
                    fn.parameterTypeIds[1] != 0) {
                    if (const auto* dcTs = program.resolveType(fn.parameterTypeIds[1])) {
                        return dcTs->moduleName.empty()
                            ? dcTs->name
                            : (dcTs->moduleName + "::" + dcTs->name);
                    }
                }
            }
            for (const auto& fref : mod.functionReferences) {
                if ((fref.name == "Aero.UI::GUI_SetDataContext" ||
                     fref.name.find("GUI_SetDataContext__") != std::string::npos ||
                     fref.name.find("__Aero_SetDataContext") != std::string::npos ||
                     fref.name.find("Aero_SetDataContext__") != std::string::npos) &&
                    fref.parameterTypeIds.size() >= 2u &&
                    fref.parameterTypeIds[0] == clsSym->id &&
                    fref.parameterTypeIds[1] != 0) {
                    if (const auto* dcTs = program.resolveType(fref.parameterTypeIds[1])) {
                        return dcTs->moduleName.empty()
                            ? dcTs->name
                            : (dcTs->moduleName + "::" + dcTs->name);
                    }
                }
            }
        }
        // 3. Inspect DataContext / ViewModel property or field on x:Class
        for (const auto& pr : clsSym->properties) {
            if ((pr.name == "DataContext" || pr.name == "ViewModel") &&
                !pr.typeName.empty() && resolveTypeIdByName(pr.typeName) != 0) {
                return pr.typeName;
            }
        }
        for (const auto& fl : clsSym->fields) {
            if ((fl.name == "DataContext" || fl.name == "ViewModel" ||
                 fl.name == "_dataContext" || fl.name == "_viewModel" ||
                 fl.name == "_ViewModel" || fl.name == "<ViewModel>k__BackingField" ||
                 fl.name == "<DataContext>k__BackingField") &&
                !fl.typeName.empty() && resolveTypeIdByName(fl.typeName) != 0) {
                return fl.typeName;
            }
        }
        // 4. Same-module ViewModel fallback if x:Class doesn't directly declare bound properties
        if (!clsSym->moduleName.empty()) {
            const std::string candVm = clsSym->moduleName + "::ViewModel";
            const SymbolId vmTid = resolveTypeIdByName(candVm);
            if (vmTid != 0 && vmTid != clsSym->id) {
                bool clsHasFirstHop = false;
                for (const auto& ch : bindingChains) {
                    if (!ch.hops.empty() && ch.elementName.empty() && !ch.relativeSelf &&
                        ch.relativeAncestorType.empty() &&
                        domains->resolvePropertySlot(program, clsSym->id, ch.hops[0].memberName).has_value()) {
                        clsHasFirstHop = true;
                        break;
                    }
                }
                if (!clsHasFirstHop) {
                    return candVm;
                }
            }
        }
        return {};
    };

    if (rootDataTypeOverride.empty() && rootDataTypeName.empty() && preOwnClassSym) {
        const std::string inferredDc = inferDataContextTypeFromXClass(preOwnClassSym);
        if (!inferredDc.empty()) {
            rootDataTypeName = inferredDc;
        }
    }

    const std::string effectiveDataTypeName = !rootDataTypeOverride.empty()
        ? std::string(rootDataTypeOverride)
        : (!rootDataTypeName.empty() ? rootDataTypeName : className);

    const SymbolId wrapperTypeId = !effectiveWrapperClassName.empty()
        ? resolveTypeIdByName(effectiveWrapperClassName)
        : 0;
    const SymbolId dataTypeId = !effectiveDataTypeName.empty()
        ? resolveTypeIdByName(effectiveDataTypeName)
        : ownClassTypeId;

    const rs_sem::TypeSymbol* ownClassTypeSym =
        (ownClassTypeId != 0) ? program.resolveType(ownClassTypeId) : nullptr;
    const rs_sem::TypeSymbol* wrapperTypeSym =
        (wrapperTypeId != 0) ? program.resolveType(wrapperTypeId) : nullptr;
    const rs_sem::TypeSymbol* dataTypeSym =
        (dataTypeId != 0) ? program.resolveType(dataTypeId) : nullptr;
    // Nested DataTemplate / ControlTemplate sub-packages do NOT own MainWindow's x:Class fields!
    const rs_sem::TypeSymbol* fieldsOwnerSym = ownClassTypeSym
        ? ownClassTypeSym
        : (hostWrapperClassName.empty() ? dataTypeSym : nullptr);

    auto findMethodOnTypeHierarchy = [&](const rs_sem::TypeSymbol* startSym,
                                         std::string_view methodName) -> SymbolId {
        if (!startSym || methodName.empty()) {
            return 0;
        }
        const rs_sem::TypeSymbol* walkSym = startSym;
        for (std::uint32_t depth = 0u; walkSym && depth < 16u; ++depth) {
            for (const auto& m : walkSym->methods) {
                if (m.name == methodName) {
                    return m.id;
                }
            }
            if (methodName.size() > 7u && str::EndsWith(methodName, "Command")) {
                const std::string baseCmd(methodName.substr(0u, methodName.size() - 7u));
                const std::string onBaseCmd = "On" + baseCmd;
                for (const auto& m : walkSym->methods) {
                    if (m.name == baseCmd || m.name == onBaseCmd) {
                        return m.id;
                    }
                }
            }
            walkSym = (walkSym->baseTypeId != 0 && walkSym->baseTypeId != walkSym->id)
                ? program.resolveType(walkSym->baseTypeId)
                : nullptr;
        }
        return 0;
    };

    // 1. Bind __nativeRootHandle and x:Name control field indices ONLY when the field is a 'handle' on fieldsOwnerSym
    if (fieldsOwnerSym) {
        fieldsOwnerTypeId = fieldsOwnerSym->id;
        for (std::size_t fIdx = 0; fIdx < fieldsOwnerSym->fields.size(); ++fIdx) {
            if (fieldsOwnerSym->fields[fIdx].name == "__nativeRootHandle" &&
                fieldsOwnerSym->fields[fIdx].type == rs_sem::PrimitiveType::Handle) {
                rootHandleFieldIndex = static_cast<std::uint32_t>(fIdx);
                break;
            }
        }
        for (auto& nc : namedControls) {
            if (!IsValidRealScriptFieldIdentifier(nc.name)) {
                continue;
            }
            const std::string typedBackingField = "__aero_handle_" + nc.name;
            for (std::size_t fIdx = 0; fIdx < fieldsOwnerSym->fields.size(); ++fIdx) {
                if ((fieldsOwnerSym->fields[fIdx].name == nc.name ||
                     fieldsOwnerSym->fields[fIdx].name == typedBackingField) &&
                    fieldsOwnerSym->fields[fIdx].type == rs_sem::PrimitiveType::Handle) {
                    nc.fieldIndexInClass = static_cast<std::uint32_t>(fIdx);
                    break;
                }
            }
        }
    }

    auto resolveMemberTargetTypeId = [&](SymbolId ownerTypeId, const AxbCompiledHop& hop) -> SymbolId {
        std::string rawNextTypeName;
        const rs_sem::TypeSymbol* walkOwner = (ownerTypeId != 0) ? program.resolveType(ownerTypeId) : nullptr;
        for (std::uint32_t baseDepth = 0u; walkOwner && baseDepth < 16u && rawNextTypeName.empty(); ++baseDepth) {
            for (const auto& pr : walkOwner->properties) {
                if (pr.name == hop.memberName && !pr.typeName.empty()) {
                    rawNextTypeName = pr.typeName;
                    break;
                }
            }
            if (rawNextTypeName.empty()) {
                for (const auto& fl : walkOwner->fields) {
                    if (fl.name == hop.memberName && !fl.typeName.empty()) {
                        rawNextTypeName = fl.typeName;
                        break;
                    }
                }
            }
            if (rawNextTypeName.empty()) {
                const std::string getterName = "get_" + hop.memberName;
                for (const auto& m : walkOwner->methods) {
                    if ((hop.getterSymbolId != 0 && m.id == hop.getterSymbolId) ||
                        (m.propertyGetter && m.name == getterName)) {
                        const std::string& rtName = rs_sem::storageReturnTypeNameOf(m);
                        if (!rtName.empty()) {
                            rawNextTypeName = rtName;
                            break;
                        }
                    }
                }
            }
            walkOwner = (walkOwner->baseTypeId != 0 && walkOwner->baseTypeId != walkOwner->id)
                ? program.resolveType(walkOwner->baseTypeId)
                : nullptr;
        }
        if (rawNextTypeName.empty()) {
            return 0;
        }

        auto unwrapCollectionElementType = [&](std::string_view containerTypeName) -> SymbolId {
            const std::string_view elemName = str::Trim(containerTypeName);
            if (elemName.size() > 2u && str::EndsWith(elemName, "[]")) {
                return resolveTypeIdByName(elemName.substr(0u, elemName.size() - 2u));
            }
            const auto lt = elemName.find('<');
            const auto gt = elemName.rfind('>');
            if (lt != std::string_view::npos && gt != std::string_view::npos && gt > lt + 1u) {
                return resolveTypeIdByName(elemName.substr(lt + 1u, gt - lt - 1u));
            }
            const SymbolId containerId = resolveTypeIdByName(elemName);
            if (const rs_sem::TypeSymbol* cSym = (containerId != 0) ? program.resolveType(containerId) : nullptr) {
                for (const auto& f : cSym->fields) {
                    if (f.name == "_items" && f.typeName.size() > 2u && str::EndsWith(f.typeName, "[]")) {
                        return resolveTypeIdByName(
                            std::string_view(f.typeName).substr(0u, f.typeName.size() - 2u));
                    }
                }
            }
            return 0;
        };

        if (hop.collectionIndex >= 0) {
            return unwrapCollectionElementType(rawNextTypeName);
        }
        return resolveTypeIdByName(rawNextTypeName);
    };

    auto applyResolvedSlotToHop = [](AxbCompiledHop& hop, const PropertySlotHandle& slot) noexcept {
        hop.declaringTypeId = slot.typeId;
        hop.fieldIndex = slot.backingFieldIndex;
        hop.observableBitIndex = slot.observableBitIndex;
        hop.getterSymbolId = slot.getterSymbolId;
        hop.setterSymbolId = slot.setterSymbolId;
        hop.isAutoProperty = slot.isAutoProperty;
        hop.isObservable = slot.isObservable;
        hop.isReadOnly = slot.isReadOnly;
        hop.isSimState = slot.isSimState;
        hop.isSimCommand = slot.isSimCommand;
    };

    auto normalizeHopIndexedName = [](AxbCompiledHop& hop) {
        if (hop.collectionIndex < 0 && hop.memberName.find('[') != std::string::npos) {
            std::string baseName;
            std::int32_t parsedIdx = -1;
            if (SplitIndexedMemberName(hop.memberName, baseName, parsedIdx)) {
                hop.memberName = std::move(baseName);
                hop.collectionIndex = parsedIdx;
            }
        }
    };

    auto bindHopVector = [&](std::vector<AxbCompiledHop>& hops, SymbolId rootOwnerId) {
        SymbolId currentHopOwnerId = rootOwnerId;
        for (auto& hop : hops) {
            normalizeHopIndexedName(hop);
            if (currentHopOwnerId == 0) {
                continue;
            }
            if (auto slotOpt = domains->resolvePropertySlot(
                    program, currentHopOwnerId, hop.memberName)) {
                applyResolvedSlotToHop(hop, *slotOpt);
            }
            currentHopOwnerId = resolveMemberTargetTypeId(currentHopOwnerId, hop);
            hop.targetTypeId = currentHopOwnerId;
            if (const rs_sem::TypeSymbol* tSym =
                    (currentHopOwnerId != 0) ? program.resolveType(currentHopOwnerId) : nullptr) {
                hop.targetIsEnum = (tSym->kind == rs_sem::TypeKind::Enum);
            }
        }
    };

    auto typeHasCommandPropertyOrField = [&](const rs_sem::TypeSymbol* startSym, std::string_view propName) -> bool {
        if (!startSym || propName.empty()) return false;
        const rs_sem::TypeSymbol* walk = startSym;
        for (std::uint32_t d = 0u; walk && d < 16u; ++d) {
            auto isCmdType = [&](const std::string& tn) -> bool {
                if (tn.empty()) return false;
                if (tn.find("DelegateCommand") != std::string::npos || tn.find("ICommand") != std::string::npos) {
                    return true;
                }
                const SymbolId tid = resolveTypeIdByName(tn);
                if (const auto* ts = (tid != 0) ? program.resolveType(tid) : nullptr) {
                    for (const auto& m : ts->methods) {
                        if (m.name == "Execute") return true;
                    }
                }
                return false;
            };
            for (const auto& pr : walk->properties) {
                if (pr.name == propName && isCmdType(pr.typeName)) return true;
            }
            for (const auto& fl : walk->fields) {
                if (fl.name == propName && isCmdType(fl.typeName)) return true;
            }
            walk = (walk->baseTypeId != 0 && walk->baseTypeId != walk->id)
                ? program.resolveType(walk->baseTypeId)
                : nullptr;
        }
        return false;
    };

    // 2. Bind event handler method SymbolIds: check MainWindow (wrapperTypeSym) FIRST (unless Command binds to ICommand property on DataContext!), then ViewModel / sub-object path
    if (wrapperTypeSym || dataTypeSym) {
        for (auto& ev : eventBindings) {
            ev.handlerSymbolId = 0;
            ev.targetIsWrapper = false;
            ev.prefixHops.clear();
            if (ev.eventName == "Command" && dataTypeSym && ev.handlerMethodName.find('.') == std::string::npos &&
                typeHasCommandPropertyOrField(dataTypeSym, ev.handlerMethodName)) {
                // Leave handlerSymbolId == 0 so runtime Button dispatches DelegateCommand / ICommand on DataContext!
                continue;
            }
            if (wrapperTypeSym && ev.handlerMethodName.find('.') == std::string::npos) {
                const SymbolId mId = findMethodOnTypeHierarchy(wrapperTypeSym, ev.handlerMethodName);
                if (mId != 0) {
                    ev.handlerSymbolId = mId;
                    ev.targetIsWrapper = true;
                    continue;
                }
            }
            if (dataTypeSym) {
                if (ev.handlerMethodName.find('.') == std::string::npos) {
                    const SymbolId mId = findMethodOnTypeHierarchy(dataTypeSym, ev.handlerMethodName);
                    if (mId != 0) {
                        ev.handlerSymbolId = mId;
                    }
                } else {
                    std::size_t segPos = 0u;
                    std::vector<std::string_view> segs;
                    while (segPos < ev.handlerMethodName.size()) {
                        const std::size_t dot = ev.handlerMethodName.find('.', segPos);
                        const std::string_view seg = (dot == std::string::npos)
                            ? std::string_view(ev.handlerMethodName).substr(segPos)
                            : std::string_view(ev.handlerMethodName).substr(segPos, dot - segPos);
                        if (!seg.empty()) {
                            segs.push_back(seg);
                        }
                        if (dot == std::string::npos) break;
                        segPos = dot + 1u;
                    }
                    if (segs.size() >= 2u) {
                        for (std::size_t si = 0u; si + 1u < segs.size(); ++si) {
                            AxbCompiledHop hop{};
                            hop.memberName = std::string(segs[si]);
                            normalizeHopIndexedName(hop);
                            ev.prefixHops.push_back(std::move(hop));
                        }
                        bindHopVector(ev.prefixHops, dataTypeId);
                        const SymbolId subOwnerId = !ev.prefixHops.empty()
                            ? ev.prefixHops.back().targetTypeId
                            : dataTypeId;
                        const rs_sem::TypeSymbol* subOwnerSym =
                            (subOwnerId != 0) ? program.resolveType(subOwnerId) : nullptr;
                        if (subOwnerSym) {
                            ev.handlerSymbolId = findMethodOnTypeHierarchy(subOwnerSym, segs.back());
                        }
                    }
                }
            }
        }
        for (auto& inst : instructions) {
            if (inst.opcode == AxbOpcode::BindEvent && inst.strIdx0 < eventBindings.size()) {
                inst.symbolId = eventBindings[inst.strIdx0].handlerSymbolId;
            }
        }
    }

    // 2b. Bind timeline motionEvents handler method SymbolIds (MainWindow first, then ViewModel)
    if (wrapperTypeSym || dataTypeSym) {
        for (auto& mev : motionEvents) {
            const std::string_view evName = GetString(mev.eventNameStrIdx);
            if (evName.empty()) {
                continue;
            }
            const std::string onEvName = "On" + std::string(evName);
            SymbolId mId = findMethodOnTypeHierarchy(wrapperTypeSym, evName);
            if (mId == 0) mId = findMethodOnTypeHierarchy(wrapperTypeSym, onEvName);
            if (mId == 0) mId = findMethodOnTypeHierarchy(dataTypeSym, evName);
            if (mId == 0) mId = findMethodOnTypeHierarchy(dataTypeSym, onEvName);
            if (mId != 0) {
                mev.handlerSymbolId = mId;
            }
        }
    }

    auto resolveConverterSymbols = [&](ValueConverterKind kind,
                                       const std::string& convKey,
                                       SymbolId& ioConvSym,
                                       SymbolId& ioBackSym) {
        if (kind != ValueConverterKind::CustomScriptOrHost || convKey.empty()) {
            return;
        }
        const std::string shortKey = [&]() {
            if (const auto c = convKey.rfind(':'); c != std::string::npos) {
                return convKey.substr(c + 1u);
            }
            if (const auto d = convKey.rfind('.'); d != std::string::npos) {
                return convKey.substr(d + 1u);
            }
            return convKey;
        }();
        const std::string fwd1 = shortKey + "_Convert";
        const std::string fwd2 = shortKey + "::Convert";
        const std::string fwd3 = shortKey + ".Convert";
        const std::string back1 = shortKey + "_ConvertBack";
        const std::string back2 = shortKey + "::ConvertBack";
        const std::string back3 = shortKey + ".ConvertBack";

        for (const auto& mod : program.modules()) {
            for (const auto& fn : mod.functions) {
                std::string_view leafName = fn.name;
                if (const auto sep = leafName.rfind("::"); sep != std::string_view::npos) {
                    leafName = leafName.substr(sep + 2u);
                }
                if (ioConvSym == 0 &&
                    (fn.name == convKey || leafName == shortKey ||
                     leafName == fwd1 || leafName == fwd3 ||
                     fn.name.find(fwd2) != std::string::npos ||
                     fn.name.find(fwd3) != std::string::npos)) {
                    ioConvSym = fn.symbolId;
                }
                if (ioBackSym == 0 &&
                    (leafName == back1 || leafName == back3 ||
                     fn.name.find(back2) != std::string::npos ||
                     fn.name.find(back3) != std::string::npos)) {
                    ioBackSym = fn.symbolId;
                }
            }
        }
    };

    auto bindMultiRec = [&](auto& self, AxbCompiledMultiBinding& mb) -> void {
        resolveConverterSymbols(
            mb.converter,
            mb.converterKey,
            mb.converterSymbolId,
            mb.convertBackSymbolId);
        for (auto& srcCh : mb.sources) {
            if (srcCh.nestedMultiBinding) {
                self(self, *srcCh.nestedMultiBinding);
                continue;
            }
            resolveConverterSymbols(
                srcCh.converter,
                srcCh.converterKey,
                srcCh.converterSymbolId,
                srcCh.convertBackSymbolId);
            for (auto& h : srcCh.hops) {
                normalizeHopIndexedName(h);
            }
            if (srcCh.elementName.empty() && srcCh.relativeAncestorType.empty() && !srcCh.relativeSelf) {
                bindHopVector(srcCh.hops, dataTypeId);
            }
        }
    };

    auto bindActionCommand = [&](AxbInteractionAction& act) {
        if (act.kind == InteractionActionKind::ChangeProperty) {
            if (act.valueBinding) {
                resolveConverterSymbols(
                    act.valueBinding->converter,
                    act.valueBinding->converterKey,
                    act.valueBinding->converterSymbolId,
                    act.valueBinding->convertBackSymbolId);
                for (auto& h : act.valueBinding->hops) {
                    normalizeHopIndexedName(h);
                }
                if (act.valueBinding->nestedMultiBinding) {
                    bindMultiRec(bindMultiRec, *act.valueBinding->nestedMultiBinding);
                } else if (act.valueBinding->elementName.empty() &&
                           act.valueBinding->relativeAncestorType.empty() &&
                           !act.valueBinding->relativeSelf) {
                    bindHopVector(act.valueBinding->hops, dataTypeId);
                }
            }
            if (act.valueMultiBinding) {
                bindMultiRec(bindMultiRec, *act.valueMultiBinding);
            }
            return;
        }
        if (act.kind != InteractionActionKind::InvokeCommand) {
            return;
        }
        if (act.hasCommandParameterBinding) {
            resolveConverterSymbols(
                act.commandParameterBinding.converter,
                act.commandParameterBinding.converterKey,
                act.commandParameterBinding.converterSymbolId,
                act.commandParameterBinding.convertBackSymbolId);
            for (auto& h : act.commandParameterBinding.hops) {
                normalizeHopIndexedName(h);
            }
            if (act.commandParameterBinding.nestedMultiBinding) {
                bindMultiRec(bindMultiRec, *act.commandParameterBinding.nestedMultiBinding);
            }
            if (act.commandParameterBinding.elementName.empty() &&
                act.commandParameterBinding.relativeAncestorType.empty() &&
                !act.commandParameterBinding.relativeSelf) {
                bindHopVector(act.commandParameterBinding.hops, dataTypeId);
            }
        }
        if (act.commandPath.empty() && act.commandHops.empty()) {
            return;
        }
        if (act.commandHops.empty() && !act.commandPath.empty()) {
            std::size_t segPos = 0u;
            while (segPos < act.commandPath.size()) {
                const std::size_t dot = act.commandPath.find('.', segPos);
                const std::string_view seg = (dot == std::string::npos)
                    ? std::string_view(act.commandPath).substr(segPos)
                    : std::string_view(act.commandPath).substr(segPos, dot - segPos);
                if (!seg.empty()) {
                    AxbCompiledHop hop{};
                    hop.memberName = std::string(seg);
                    normalizeHopIndexedName(hop);
                    act.commandHops.push_back(std::move(hop));
                }
                if (dot == std::string::npos) break;
                segPos = dot + 1u;
            }
        } else {
            for (auto& h : act.commandHops) {
                normalizeHopIndexedName(h);
            }
        }

        std::string leafCmd = !act.commandHops.empty()
            ? act.commandHops.back().memberName
            : act.commandPath;
        if (const auto dot = leafCmd.rfind('.'); dot != std::string::npos) {
            leafCmd = leafCmd.substr(dot + 1u);
        }

        // Check DataContext ICommand/DelegateCommand property FIRST, else MainWindow (wrapperTypeSym) for single-hop InvokeCommandAction!
        act.commandMethodSymbolId = 0;
        act.commandTargetsWrapper = false;
        const bool dcHasCmdProp = (dataTypeSym && act.commandHops.size() <= 1u && !leafCmd.empty() &&
                                   typeHasCommandPropertyOrField(dataTypeSym, leafCmd));
        if (!dcHasCmdProp && wrapperTypeSym && act.commandHops.size() <= 1u && !leafCmd.empty()) {
            const SymbolId wrapperMethodId = findMethodOnTypeHierarchy(wrapperTypeSym, leafCmd);
            if (wrapperMethodId != 0) {
                act.commandMethodSymbolId = wrapperMethodId;
                act.commandTargetsWrapper = true;
                if (domains->isSimCommandFunction(act.commandMethodSymbolId)) {
                    act.isSimCommand = true;
                }
                return;
            }
        }

        SymbolId ownerTypeId = dataTypeId;
        if (act.commandHops.size() > 1u) {
            for (std::size_t hi = 0u; hi + 1u < act.commandHops.size(); ++hi) {
                auto& hop = act.commandHops[hi];
                if (ownerTypeId == 0) {
                    break;
                }
                if (auto slotOpt = domains->resolvePropertySlot(program, ownerTypeId, hop.memberName)) {
                    applyResolvedSlotToHop(hop, *slotOpt);
                }
                ownerTypeId = resolveMemberTargetTypeId(ownerTypeId, hop);
                hop.targetTypeId = ownerTypeId;
            }
        }
        if (!act.commandHops.empty() && ownerTypeId != 0) {
            auto& lastHop = act.commandHops.back();
            if (auto slotOpt = domains->resolvePropertySlot(program, ownerTypeId, lastHop.memberName)) {
                applyResolvedSlotToHop(lastHop, *slotOpt);
                if (lastHop.isSimCommand) {
                    act.isSimCommand = true;
                }
            }
        }
        const rs_sem::TypeSymbol* methodOwnerSym =
            (ownerTypeId != 0) ? program.resolveType(ownerTypeId) : nullptr;
        if (methodOwnerSym && !leafCmd.empty()) {
            act.commandMethodSymbolId = findMethodOnTypeHierarchy(methodOwnerSym, leafCmd);
        }
        if (act.commandMethodSymbolId != 0 &&
            domains->isSimCommandFunction(act.commandMethodSymbolId)) {
            act.isSimCommand = true;
        }
    };

    auto bindConditionEnumLiteral = [&](AxbTriggerCondition& cond) {
        if (cond.sourceKind != TriggerConditionSource::DataContextBinding ||
            cond.compiledHops.empty()) {
            return;
        }
        const auto& leafHop = cond.compiledHops.back();
        if (!leafHop.targetIsEnum || leafHop.targetTypeId == 0) {
            return;
        }
        if (const auto* sv = std::get_if<StringValue>(&cond.expectedValue)) {
            std::int64_t enumVal = 0;
            if (TryResolveEnumMemberByName(&program, leafHop.targetTypeId, sv->view(), enumVal)) {
                cond.expectedValue = value::FromInt(enumVal);
            }
        }
    };

    auto bindConditionList = [&](std::vector<AxbTriggerCondition>& conditions) {
        for (auto& cond : conditions) {
            SymbolId dummyBack = 0;
            resolveConverterSymbols(
                cond.converter,
                cond.converterKey,
                cond.converterSymbolId,
                dummyBack);
            for (auto& h : cond.compiledHops) {
                normalizeHopIndexedName(h);
            }
            if (dataTypeId != 0 &&
                cond.sourceKind == TriggerConditionSource::DataContextBinding &&
                !cond.compiledHops.empty()) {
                bindHopVector(cond.compiledHops, dataTypeId);
                bindConditionEnumLiteral(cond);
            }
        }
    };

    // 3. Bind multi-hop PropertySlotHandles & converter symbols in bindingChains and multiBindings (against ViewModel!)
    for (auto& chain : bindingChains) {
        resolveConverterSymbols(
            chain.converter,
            chain.converterKey,
            chain.converterSymbolId,
            chain.convertBackSymbolId);
        for (auto& h : chain.hops) {
            normalizeHopIndexedName(h);
        }
        if (chain.nestedMultiBinding) {
            bindMultiRec(bindMultiRec, *chain.nestedMultiBinding);
        }
        if (!chain.elementName.empty() || !chain.relativeAncestorType.empty() || chain.relativeSelf) {
            continue;
        }
        bindHopVector(chain.hops, dataTypeId);
    }
    for (auto& mb : multiBindings) {
        bindMultiRec(bindMultiRec, mb);
    }

    auto bindSetterBindings = [&](std::vector<AxbPropertySetter>& setters) {
        for (auto& setter : setters) {
            if (setter.hasBinding && setter.binding) {
                resolveConverterSymbols(
                    setter.binding->converter,
                    setter.binding->converterKey,
                    setter.binding->converterSymbolId,
                    setter.binding->convertBackSymbolId);
                for (auto& h : setter.binding->hops) {
                    normalizeHopIndexedName(h);
                }
                if (setter.binding->nestedMultiBinding) {
                    bindMultiRec(bindMultiRec, *setter.binding->nestedMultiBinding);
                } else if (setter.binding->elementName.empty() &&
                           setter.binding->relativeAncestorType.empty() &&
                           !setter.binding->relativeSelf) {
                    bindHopVector(setter.binding->hops, dataTypeId);
                }
            }
        }
    };

    auto bindControlTemplate = [&](AxbControlTemplate& ct) {
        for (auto& trig : ct.triggers) {
            bindSetterBindings(trig.setters);
            bindConditionList(trig.conditions);
            for (auto& act : trig.enterActions) {
                bindActionCommand(act);
            }
            for (auto& act : trig.exitActions) {
                bindActionCommand(act);
            }
        }
        for (auto& et : ct.eventTriggers) {
            bindConditionList(et.conditions);
            for (auto& act : et.actions) {
                bindActionCommand(act);
            }
        }
        if (ct.templatePackage) {
            if (ct.templatePackage->className.empty() && !effectiveWrapperClassName.empty()) {
                ct.templatePackage->hostWrapperClassName = effectiveWrapperClassName;
            }
            (void)ct.templatePackage->BindAgainstProgram(program, effectiveDataTypeName, domains);
        }
    };

    // 4. Bind DataTrigger / MultiDataTrigger compiledHops & Interaction actions against root DataType (ViewModel)
    for (auto& style : styles) {
        bindSetterBindings(style.setters);
        for (auto& trig : style.triggers) {
            bindSetterBindings(trig.setters);
            bindConditionList(trig.conditions);
            for (auto& act : trig.enterActions) {
                bindActionCommand(act);
            }
            for (auto& act : trig.exitActions) {
                bindActionCommand(act);
            }
        }
        for (auto& et : style.eventTriggers) {
            bindConditionList(et.conditions);
            for (auto& act : et.actions) {
                bindActionCommand(act);
            }
        }
        if (style.inlineControlTemplate) {
            bindControlTemplate(*style.inlineControlTemplate);
        }
    }

    // 5. Bind nested DataTemplate & ControlTemplate sub-packages (propagating hostWrapperClassName for wrapper commands without clobbering className!)
    for (auto& dt : dataTemplates) {
        if (dt.templatePackage) {
            if (dt.templatePackage->className.empty() && !effectiveWrapperClassName.empty()) {
                dt.templatePackage->hostWrapperClassName = effectiveWrapperClassName;
            }
            const std::string subDataType = !dt.dataTypeName.empty()
                ? dt.dataTypeName
                : effectiveDataTypeName;
            (void)dt.templatePackage->BindAgainstProgram(program, subDataType, domains);
        }
    }
    for (auto& ct : controlTemplates) {
        bindControlTemplate(ct);
    }

    return true;
}

bool AxbPackage::HasP15Section() const noexcept {
    if (!startupUri.empty()) {
        return true;
    }
    for (const auto& dt : dataTemplates) {
        if (!dt.itemsSourcePath.empty() || !dt.childItemTemplateKey.empty()) {
            return true;
        }
    }
    auto checkActs = [](const std::vector<AxbInteractionAction>& acts) -> bool {
        for (const auto& a : acts) {
            if (a.targetObjectBinding != nullptr) {
                return true;
            }
        }
        return false;
    };
    for (const auto& st : styles) {
        for (const auto& tr : st.triggers) {
            if (checkActs(tr.enterActions) || checkActs(tr.exitActions)) return true;
        }
        for (const auto& et : st.eventTriggers) {
            if (checkActs(et.actions)) return true;
        }
    }
    for (const auto& ct : controlTemplates) {
        for (const auto& tr : ct.triggers) {
            if (checkActs(tr.enterActions) || checkActs(tr.exitActions)) return true;
        }
        for (const auto& et : ct.eventTriggers) {
            if (checkActs(et.actions)) return true;
        }
    }
    return false;
}

bool AxbPackage::HasP14Section() const noexcept {
    if (!multiBindings.empty()) {
        return true;
    }
    for (const auto& ch : bindingChains) {
        if (!ch.converterKey.empty() || !ch.converterParameter.empty() ||
            !ch.stringFormat.empty() || ch.converterSymbolId != 0 ||
            ch.convertBackSymbolId != 0 || ch.nestedMultiBinding != nullptr) {
            return true;
        }
    }
    for (const auto& st : styles) {
        for (const auto& trig : st.triggers) {
            if (trig.isInteractionTrigger || !trig.enterActions.empty() || !trig.exitActions.empty()) {
                return true;
            }
            for (const auto& cond : trig.conditions) {
                if (cond.comparison != TriggerComparisonOp::Equal ||
                    !cond.converterKey.empty() || !cond.converterParameter.empty() ||
                    cond.converterSymbolId != 0) {
                    return true;
                }
            }
        }
        for (const auto& et : st.eventTriggers) {
            if (!et.actions.empty() || !et.conditions.empty()) {
                return true;
            }
        }
    }
    for (const auto& ct : controlTemplates) {
        for (const auto& trig : ct.triggers) {
            if (trig.isInteractionTrigger || !trig.enterActions.empty() || !trig.exitActions.empty()) {
                return true;
            }
            for (const auto& cond : trig.conditions) {
                if (cond.comparison != TriggerComparisonOp::Equal ||
                    !cond.converterKey.empty() || !cond.converterParameter.empty() ||
                    cond.converterSymbolId != 0) {
                    return true;
                }
            }
        }
        for (const auto& et : ct.eventTriggers) {
            if (!et.actions.empty() || !et.conditions.empty()) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::uint8_t> AxbPackage::Serialize() const {
    std::vector<std::uint8_t> out;
    out.reserve(512);

    WriteU32(out, kAxb4Magic);
    WriteU32(out, kAxb4Version);
    WriteString(out, className);
    WriteString(out, rootTypeName);

    WriteU32(out, static_cast<std::uint32_t>(stringTable.size()));
    for (const auto& s : stringTable) {
        WriteString(out, s);
    }

    WriteU32(out, static_cast<std::uint32_t>(namedControls.size()));
    for (const auto& nc : namedControls) {
        WriteString(out, nc.name);
        WriteString(out, nc.typeName);
        WriteU64(out, nc.typeSymbolId);
        WriteU32(out, nc.fieldIndexInClass);
    }

    WriteU32(out, static_cast<std::uint32_t>(bindingChains.size()));
    for (const auto& chain : bindingChains) {
        WriteString(out, chain.rawPath);
        WriteU16(out, chain.targetPropertyId);
        WriteU8(out, static_cast<std::uint8_t>(chain.mode));
        WriteHopVector(out, chain.hops);
    }

    WriteU32(out, static_cast<std::uint32_t>(eventBindings.size()));
    for (const auto& ev : eventBindings) {
        WriteString(out, ev.controlName);
        WriteString(out, ev.eventName);
        WriteString(out, ev.handlerMethodName);
        WriteU64(out, ev.handlerSymbolId);
    }

    WriteU32(out, static_cast<std::uint32_t>(instructions.size()));
    for (const auto& inst : instructions) {
        WriteU8(out, static_cast<std::uint8_t>(inst.opcode));
        WriteU16(out, inst.propId);
        WriteU8(out, inst.flags);
        WriteU32(out, inst.strIdx0);
        WriteU32(out, inst.strIdx1);
        WriteU64(out, inst.symbolId);
        WriteValue(out, inst.literalValue);
    }

    WriteU32(out, rootHandleFieldIndex);
    WriteU32(out, static_cast<std::uint32_t>(resources.size()));
    for (const auto& kv : resources) {
        WriteString(out, kv.first);
        WriteValue(out, kv.second);
    }
    WriteU32(out, static_cast<std::uint32_t>(bindingChains.size()));
    for (const auto& chain : bindingChains) {
        WriteValue(out, chain.fallbackValue);
    }
    {
        bool hasAnyTargetNull = false;
        for (const auto& chain : bindingChains) {
            if (!std::holds_alternative<std::monostate>(chain.targetNullValue)) {
                hasAnyTargetNull = true;
                break;
            }
        }
        if (hasAnyTargetNull) {
            WriteU32(out, 0x344E5441u);
            WriteU32(out, static_cast<std::uint32_t>(bindingChains.size()));
            for (const auto& chain : bindingChains) {
                WriteValue(out, chain.targetNullValue);
            }
        }
    }

    if (!mtsdfPaths.empty()) {
        WriteU32(out, kAxb4MtsdfSectionMagic);
        WriteU32(out, static_cast<std::uint32_t>(mtsdfPaths.size()));
        for (const auto& mp : mtsdfPaths) {
            WriteU64(out, mp.pathHash);
            WriteF32(out, mp.boundsX);
            WriteF32(out, mp.boundsY);
            WriteF32(out, mp.boundsW);
            WriteF32(out, mp.boundsH);
            WriteF32(out, mp.u0);
            WriteF32(out, mp.v0);
            WriteF32(out, mp.u1);
            WriteF32(out, mp.v1);
            WriteU16(out, mp.atlasWidth);
            WriteU16(out, mp.atlasHeight);
            WriteU16(out, mp.cellWidth);
            WriteU16(out, mp.cellHeight);
            WriteF32(out, mp.pxRange);
            WriteU32(out, static_cast<std::uint32_t>(mp.mtsdfRgbaPixels.size()));
            out.insert(out.end(), mp.mtsdfRgbaPixels.begin(), mp.mtsdfRgbaPixels.end());
        }
    }

    if (!styles.empty() || !storyboards.empty()) {
        WriteU32(out, kAxb4StyleAnimSectionMagic);
        WriteU32(out, static_cast<std::uint32_t>(styles.size()));
        for (const auto& style : styles) {
            WriteString(out, style.key);
            WriteString(out, style.targetTypeName);
            WriteString(out, style.basedOnKey);
            WriteSetterList(out, style.setters);
            WriteU32(out, static_cast<std::uint32_t>(style.triggers.size()));
            for (const auto& trig : style.triggers) {
                WriteU8(out, trig.overrideLocalValues ? 1u : 0u);
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU16(out, cond.propId);
                    WriteValue(out, cond.expectedValue);
                }
                WriteSetterList(out, trig.setters);
                WriteU32(out, static_cast<std::uint32_t>(trig.enterStoryboards.size()));
                for (const auto& sb : trig.enterStoryboards) WriteStoryboard(out, sb);
                WriteU32(out, static_cast<std::uint32_t>(trig.exitStoryboards.size()));
                for (const auto& sb : trig.exitStoryboards) WriteStoryboard(out, sb);
            }
            WriteU32(out, static_cast<std::uint32_t>(style.visualStateGroups.size()));
            for (const auto& vsg : style.visualStateGroups) {
                WriteString(out, vsg.name);
                WriteF32(out, vsg.defaultTransitionDuration);
                WriteU8(out, static_cast<std::uint8_t>(vsg.defaultEasingKind));
                WriteU8(out, static_cast<std::uint8_t>(vsg.defaultEasingMode));
                WriteU32(out, static_cast<std::uint32_t>(vsg.states.size()));
                for (const auto& vs : vsg.states) {
                    WriteString(out, vs.name);
                    WriteSetterList(out, vs.setters);
                    WriteStoryboard(out, vs.storyboard);
                }
            }
        }
        WriteU32(out, static_cast<std::uint32_t>(storyboards.size()));
        for (const auto& sb : storyboards) {
            WriteStoryboard(out, sb);
        }
    }

    bool hasPhase5D = !dataTemplates.empty();
    if (!hasPhase5D) {
        for (const auto& ch : bindingChains) {
            if (!ch.elementName.empty() || !ch.relativeAncestorType.empty() ||
                ch.relativeSelf || ch.converter != ValueConverterKind::None) {
                hasPhase5D = true;
                break;
            }
        }
    }
    if (!hasPhase5D) {
        for (const auto& st : styles) {
            if (!st.eventTriggers.empty()) {
                hasPhase5D = true;
                break;
            }
            for (const auto& trig : st.triggers) {
                for (const auto& cond : trig.conditions) {
                    if (cond.sourceKind != TriggerConditionSource::SelfProperty ||
                        cond.converter != ValueConverterKind::None ||
                        cond.expectNull) {
                        hasPhase5D = true;
                        break;
                    }
                }
                if (hasPhase5D) break;
            }
            if (hasPhase5D) break;
        }
    }

    if (hasPhase5D) {
        WriteU32(out, kAxb4Phase5DSectionMagic);

        // 1. Binding chain extensions
        WriteU32(out, static_cast<std::uint32_t>(bindingChains.size()));
        for (const auto& ch : bindingChains) {
            WriteString(out, ch.elementName);
            WriteString(out, ch.relativeAncestorType);
            WriteU8(out, ch.relativeSelf ? 1u : 0u);
            WriteU8(out, static_cast<std::uint8_t>(ch.converter));
        }

        // 2. Style DataTrigger condition extensions & EventTriggers
        WriteU32(out, static_cast<std::uint32_t>(styles.size()));
        for (const auto& st : styles) {
            WriteU32(out, static_cast<std::uint32_t>(st.triggers.size()));
            for (const auto& trig : st.triggers) {
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU8(out, static_cast<std::uint8_t>(cond.sourceKind));
                    WriteU8(out, static_cast<std::uint8_t>(cond.converter));
                    WriteU8(out, cond.expectNull ? 1u : 0u);
                    WriteString(out, cond.sourceName);
                    WriteString(out, cond.bindingPath);
                    WriteHopVector(out, cond.compiledHops);
                }
            }
            WriteU32(out, static_cast<std::uint32_t>(st.eventTriggers.size()));
            for (const auto& et : st.eventTriggers) {
                WriteU8(out, static_cast<std::uint8_t>(et.eventKind));
                WriteString(out, et.sourceName);
                WriteU32(out, static_cast<std::uint32_t>(et.storyboards.size()));
                for (const auto& sb : et.storyboards) {
                    WriteStoryboard(out, sb);
                }
            }
        }

        // 3. Nested compiled DataTemplate sub-packages
        WriteU32(out, static_cast<std::uint32_t>(dataTemplates.size()));
        for (const auto& dt : dataTemplates) {
            WriteString(out, dt.key);
            WriteString(out, dt.dataTypeName);
            const std::vector<std::uint8_t> subBytes = dt.templatePackage
                ? dt.templatePackage->Serialize()
                : std::vector<std::uint8_t>{};
            WriteU32(out, static_cast<std::uint32_t>(subBytes.size()));
            out.insert(out.end(), subBytes.begin(), subBytes.end());
        }
    }

    if (HasMotionSection()) {
        WriteU32(out, kAxb4MotionSectionMagic);

        // 1. Flat keyframes (28 bytes each)
        WriteU32(out, static_cast<std::uint32_t>(motionKeyframes.size()));
        for (const auto& kf : motionKeyframes) {
            WriteF32(out, kf.timeSec);
            WriteF32(out, kf.value.x);
            WriteF32(out, kf.value.y);
            WriteF32(out, kf.value.z);
            WriteF32(out, kf.value.w);
            WriteU8(out, kf.interp);
            WriteU8(out, kf.easeKindOrCtrlX1);
            WriteU8(out, kf.easeModeOrCtrlX2);
            WriteU8(out, kf.flags);
            WriteU16(out, static_cast<std::uint16_t>(kf.ctrlY1Fixed8));
            WriteU16(out, static_cast<std::uint16_t>(kf.ctrlY2Fixed8));
        }

        // 2. Motion tracks
        WriteU32(out, static_cast<std::uint32_t>(motionTracks.size()));
        for (const auto& tr : motionTracks) {
            WriteU16(out, tr.localElementIdx);
            WriteU16(out, tr.targetPropId);
            WriteU8(out, tr.channelCount);
            WriteU8(out, tr.quantizeInlineW);
            WriteU16(out, tr.reserved);
            WriteU32(out, tr.firstKeyframeIdx);
            WriteU32(out, tr.keyframeCount);
        }

        // 3. Timeline event markers
        WriteU32(out, static_cast<std::uint32_t>(motionEvents.size()));
        for (const auto& mev : motionEvents) {
            WriteF32(out, mev.timeSec);
            WriteU32(out, mev.eventNameStrIdx);
            WriteU64(out, mev.handlerSymbolId);
            WriteF32(out, mev.floatPayload);
            WriteU32(out, static_cast<std::uint32_t>(mev.intPayload));
        }

        // 4. Motion clips
        WriteU32(out, static_cast<std::uint32_t>(motionClips.size()));
        for (const auto& cl : motionClips) {
            WriteU32(out, cl.nameStrIdx);
            WriteF32(out, cl.durationSec);
            WriteU8(out, cl.isLooping ? 1u : 0u);
            WriteU8(out, cl.autoReverse ? 1u : 0u);
            WriteU32(out, cl.firstTrackIdx);
            WriteU32(out, cl.trackCount);
            WriteU32(out, cl.firstEventIdx);
            WriteU32(out, cl.eventCount);
        }

        // 5. Transform constraints
        WriteU32(out, static_cast<std::uint32_t>(transformConstraints.size()));
        for (const auto& tc : transformConstraints) {
            WriteU16(out, tc.childLocalElementIdx);
            WriteU16(out, tc.parentLocalElementIdx);
        }

        // 6. State machine conditions
        WriteU32(out, static_cast<std::uint32_t>(smConditions.size()));
        for (const auto& sc : smConditions) {
            WriteU16(out, sc.inputSlotIdx);
            WriteU8(out, static_cast<std::uint8_t>(sc.op));
            WriteU8(out, sc.reserved);
            WriteF32(out, sc.threshold);
        }

        // 7. State machine transitions
        WriteU32(out, static_cast<std::uint32_t>(smTransitions.size()));
        for (const auto& tr : smTransitions) {
            WriteU16(out, tr.fromStateIdx);
            WriteU16(out, tr.toStateIdx);
            WriteF32(out, tr.crossfadeDurationSec);
            WriteU8(out, static_cast<std::uint8_t>(tr.easeKind));
            WriteU8(out, static_cast<std::uint8_t>(tr.easeMode));
            WriteU8(out, tr.hasExitTime ? 1u : 0u);
            WriteU8(out, tr.allowSelfInterrupt ? 1u : 0u);
            WriteF32(out, tr.exitTimeNormalized01);
            WriteU32(out, tr.firstConditionIdx);
            WriteU16(out, tr.conditionCount);
            WriteU16(out, tr.priority);
        }

        // 8. State machine states
        WriteU32(out, static_cast<std::uint32_t>(smStates.size()));
        for (const auto& st : smStates) {
            WriteU32(out, st.nameStrIdx);
            WriteU8(out, static_cast<std::uint8_t>(st.kind));
            WriteU8(out, st.reserved);
            WriteU16(out, st.primaryClipIdx);
            WriteU16(out, st.secondaryClipIdx);
            WriteU16(out, st.progressInputSlot);
            WriteF32(out, st.speedMultiplier);
            WriteF32(out, st.blendRangeMin);
            WriteF32(out, st.blendRangeMax);
        }

        // 9. State machine layers
        WriteU32(out, static_cast<std::uint32_t>(smLayers.size()));
        for (const auto& ly : smLayers) {
            WriteU32(out, ly.nameStrIdx);
            WriteU8(out, static_cast<std::uint8_t>(ly.blendMode));
            WriteU8(out, ly.reserved);
            WriteU16(out, ly.entryStateIdx);
            WriteF32(out, ly.layerWeight);
            WriteU32(out, ly.firstStateIdx);
            WriteU16(out, ly.stateCount);
            WriteU32(out, ly.firstTransitionIdx);
            WriteU16(out, ly.transitionCount);
        }

        // 10. Pointer listeners
        WriteU32(out, static_cast<std::uint32_t>(smListeners.size()));
        for (const auto& ls : smListeners) {
            WriteU16(out, ls.hitTargetElementIdx);
            WriteU8(out, static_cast<std::uint8_t>(ls.eventKind));
            WriteU8(out, static_cast<std::uint8_t>(ls.actionKind));
            WriteU16(out, ls.targetInputSlot);
            WriteF32(out, ls.actionValue);
        }

        // 11. Contract input descriptors
        WriteU32(out, static_cast<std::uint32_t>(contractInputs.size()));
        for (const auto& ci : contractInputs) {
            WriteU32(out, ci.nameStrIdx);
            WriteU8(out, ci.valueKind);
            WriteU8(out, ci.isTrigger);
            WriteU16(out, ci.bridgePropId);
            WriteValue(out, ci.defaultValue);
        }

        // 12. State machine descriptors
        WriteU32(out, static_cast<std::uint32_t>(stateMachines.size()));
        for (const auto& sm : stateMachines) {
            WriteU32(out, sm.nameStrIdx);
            WriteU32(out, sm.firstLayerIdx);
            WriteU16(out, sm.layerCount);
            WriteU32(out, sm.firstListenerIdx);
            WriteU16(out, sm.listenerCount);
        }
    }

    if (!controlTemplates.empty()) {
        WriteU32(out, kAxb4ControlTemplateSectionMagic);
        WriteU32(out, static_cast<std::uint32_t>(controlTemplates.size()));
        for (const auto& ct : controlTemplates) {
            WriteString(out, ct.key);
            WriteString(out, ct.targetTypeName);
            const std::vector<std::uint8_t> subBytes = ct.templatePackage
                ? ct.templatePackage->Serialize()
                : std::vector<std::uint8_t>{};
            WriteU32(out, static_cast<std::uint32_t>(subBytes.size()));
            out.insert(out.end(), subBytes.begin(), subBytes.end());

            // Triggers (including Phase 5D condition fields)
            WriteU32(out, static_cast<std::uint32_t>(ct.triggers.size()));
            for (const auto& trig : ct.triggers) {
                WriteU8(out, trig.overrideLocalValues ? 1u : 0u);
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU16(out, cond.propId);
                    WriteValue(out, cond.expectedValue);
                    WriteU8(out, static_cast<std::uint8_t>(cond.sourceKind));
                    WriteU8(out, static_cast<std::uint8_t>(cond.converter));
                    WriteU8(out, cond.expectNull ? 1u : 0u);
                    WriteString(out, cond.sourceName);
                    WriteString(out, cond.bindingPath);
                    WriteHopVector(out, cond.compiledHops);
                }
                WriteSetterList(out, trig.setters);
                WriteU32(out, static_cast<std::uint32_t>(trig.enterStoryboards.size()));
                for (const auto& sb : trig.enterStoryboards) WriteStoryboard(out, sb);
                WriteU32(out, static_cast<std::uint32_t>(trig.exitStoryboards.size()));
                for (const auto& sb : trig.exitStoryboards) WriteStoryboard(out, sb);
            }

            // EventTriggers
            WriteU32(out, static_cast<std::uint32_t>(ct.eventTriggers.size()));
            for (const auto& et : ct.eventTriggers) {
                WriteU8(out, static_cast<std::uint8_t>(et.eventKind));
                WriteString(out, et.sourceName);
                WriteU32(out, static_cast<std::uint32_t>(et.storyboards.size()));
                for (const auto& sb : et.storyboards) WriteStoryboard(out, sb);
            }

            // VisualStateGroups
            WriteU32(out, static_cast<std::uint32_t>(ct.visualStateGroups.size()));
            for (const auto& vsg : ct.visualStateGroups) {
                WriteString(out, vsg.name);
                WriteF32(out, vsg.defaultTransitionDuration);
                WriteU8(out, static_cast<std::uint8_t>(vsg.defaultEasingKind));
                WriteU8(out, static_cast<std::uint8_t>(vsg.defaultEasingMode));
                WriteU32(out, static_cast<std::uint32_t>(vsg.states.size()));
                for (const auto& vs : vsg.states) {
                    WriteString(out, vs.name);
                    WriteSetterList(out, vs.setters);
                    WriteStoryboard(out, vs.storyboard);
                }
            }
        }
    }

    if (!mergedDictionarySources.empty()) {
        WriteU32(out, kAxb4MergedDictSectionMagic);
        WriteU32(out, static_cast<std::uint32_t>(mergedDictionarySources.size()));
        for (const auto& srcUri : mergedDictionarySources) {
            WriteString(out, srcUri);
        }
    }

    auto writeFullChainRec = [&](auto& selfChain, auto& selfMulti, const AxbCompiledBindingChain& ch) -> void {
        WriteString(out, ch.rawPath);
        WriteU16(out, ch.targetPropertyId);
        WriteU8(out, static_cast<std::uint8_t>(ch.mode));
        WriteValue(out, ch.fallbackValue);
        WriteHopVector(out, ch.hops);
        WriteString(out, ch.elementName);
        WriteString(out, ch.relativeAncestorType);
        WriteU8(out, ch.relativeSelf ? 1u : 0u);
        WriteU8(out, static_cast<std::uint8_t>(ch.converter));
        WriteString(out, ch.converterKey);
        WriteString(out, ch.converterParameter);
        WriteString(out, ch.stringFormat);
        WriteU64(out, ch.converterSymbolId);
        WriteU64(out, ch.convertBackSymbolId);
        WriteU8(out, ch.nestedMultiBinding ? 1u : 0u);
        if (ch.nestedMultiBinding) {
            selfMulti(selfChain, selfMulti, *ch.nestedMultiBinding);
        }
    };

    auto writeMultiBindingRec = [&](auto& selfChain, auto& selfMulti, const AxbCompiledMultiBinding& mb) -> void {
        WriteU16(out, mb.targetPropertyId);
        WriteU8(out, static_cast<std::uint8_t>(mb.mode));
        WriteU8(out, static_cast<std::uint8_t>(mb.converter));
        WriteString(out, mb.converterKey);
        WriteString(out, mb.converterParameter);
        WriteString(out, mb.stringFormat);
        WriteValue(out, mb.fallbackValue);
        WriteU64(out, mb.converterSymbolId);
        WriteU64(out, mb.convertBackSymbolId);
        WriteU32(out, static_cast<std::uint32_t>(mb.sources.size()));
        for (const auto& srcCh : mb.sources) {
            selfChain(selfChain, selfMulti, srcCh);
        }
    };

    if (HasP14Section()) {
        WriteU32(out, kAxb4P14SectionMagic);

        auto writeActionList = [&](const std::vector<AxbInteractionAction>& acts) {
            WriteU32(out, static_cast<std::uint32_t>(acts.size()));
            for (const auto& act : acts) {
                WriteU8(out, static_cast<std::uint8_t>(act.kind));
                WriteString(out, act.actionName);
                std::uint8_t actFlags = 0u;
                if (act.isEnabled) actFlags |= 0x01u;
                if (act.isSimCommand) actFlags |= 0x02u;
                if (act.hasCommandParameter) actFlags |= 0x04u;
                if (act.hasCommandParameterBinding) actFlags |= 0x08u;
                WriteU8(out, actFlags);
                WriteString(out, act.targetName);
                WriteU16(out, act.propId);
                WriteString(out, act.propertyName);
                WriteValue(out, act.value);
                WriteString(out, act.storyboardKey);
                WriteStoryboard(out, act.inlineStoryboard);
                WriteU8(out, static_cast<std::uint8_t>(act.storyboardOption));
                WriteString(out, act.commandPath);
                WriteHopVector(out, act.commandHops);
                WriteU64(out, act.commandMethodSymbolId);
                WriteValue(out, act.commandParameter);
                if (act.hasCommandParameterBinding) {
                    writeFullChainRec(writeFullChainRec, writeMultiBindingRec, act.commandParameterBinding);
                }
                WriteString(out, act.soundSource);
                WriteF32(out, act.volume);
            }
        };

        auto writeFullConditionList = [&](const std::vector<AxbTriggerCondition>& conds) {
            WriteU32(out, static_cast<std::uint32_t>(conds.size()));
            for (const auto& cond : conds) {
                WriteU16(out, cond.propId);
                WriteValue(out, cond.expectedValue);
                WriteU8(out, static_cast<std::uint8_t>(cond.sourceKind));
                WriteU8(out, static_cast<std::uint8_t>(cond.converter));
                WriteU8(out, cond.expectNull ? 1u : 0u);
                WriteString(out, cond.sourceName);
                WriteString(out, cond.bindingPath);
                WriteHopVector(out, cond.compiledHops);
                WriteU8(out, static_cast<std::uint8_t>(cond.comparison));
                WriteString(out, cond.converterKey);
                WriteString(out, cond.converterParameter);
                WriteU64(out, cond.converterSymbolId);
            }
        };

        // 1. P1-4 extensions on top-level bindingChains
        WriteU32(out, static_cast<std::uint32_t>(bindingChains.size()));
        for (const auto& ch : bindingChains) {
            WriteString(out, ch.converterKey);
            WriteString(out, ch.converterParameter);
            WriteString(out, ch.stringFormat);
            WriteU64(out, ch.converterSymbolId);
            WriteU64(out, ch.convertBackSymbolId);
            WriteU8(out, ch.nestedMultiBinding ? 1u : 0u);
            if (ch.nestedMultiBinding) {
                writeMultiBindingRec(writeFullChainRec, writeMultiBindingRec, *ch.nestedMultiBinding);
            }
        }

        // 2. Compiled MultiBindings
        WriteU32(out, static_cast<std::uint32_t>(multiBindings.size()));
        for (const auto& mb : multiBindings) {
            writeMultiBindingRec(writeFullChainRec, writeMultiBindingRec, mb);
        }

        // 3. P1-4 style extensions (TriggerComparisonOp, converter keys, Interaction actions)
        WriteU32(out, static_cast<std::uint32_t>(styles.size()));
        for (const auto& st : styles) {
            WriteU32(out, static_cast<std::uint32_t>(st.triggers.size()));
            for (const auto& trig : st.triggers) {
                WriteU8(out, trig.isInteractionTrigger ? 1u : 0u);
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU8(out, static_cast<std::uint8_t>(cond.comparison));
                    WriteString(out, cond.converterKey);
                    WriteString(out, cond.converterParameter);
                    WriteU64(out, cond.converterSymbolId);
                }
                writeActionList(trig.enterActions);
                writeActionList(trig.exitActions);
            }
            WriteU32(out, static_cast<std::uint32_t>(st.eventTriggers.size()));
            for (const auto& et : st.eventTriggers) {
                writeActionList(et.actions);
                writeFullConditionList(et.conditions);
            }
        }

        // 4. P1-4 controlTemplate extensions (TriggerComparisonOp, converter keys, Interaction actions)
        WriteU32(out, static_cast<std::uint32_t>(controlTemplates.size()));
        for (const auto& ct : controlTemplates) {
            WriteU32(out, static_cast<std::uint32_t>(ct.triggers.size()));
            for (const auto& trig : ct.triggers) {
                WriteU8(out, trig.isInteractionTrigger ? 1u : 0u);
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU8(out, static_cast<std::uint8_t>(cond.comparison));
                    WriteString(out, cond.converterKey);
                    WriteString(out, cond.converterParameter);
                    WriteU64(out, cond.converterSymbolId);
                }
                writeActionList(trig.enterActions);
                writeActionList(trig.exitActions);
            }
            WriteU32(out, static_cast<std::uint32_t>(ct.eventTriggers.size()));
            for (const auto& et : ct.eventTriggers) {
                writeActionList(et.actions);
                writeFullConditionList(et.conditions);
            }
        }
    }

    if (HasP15Section()) {
        WriteU32(out, kAxb4P15SectionMagic);
        WriteString(out, startupUri);
        WriteU32(out, static_cast<std::uint32_t>(dataTemplates.size()));
        for (const auto& dt : dataTemplates) {
            WriteString(out, dt.itemsSourcePath);
            WriteString(out, dt.childItemTemplateKey);
        }
        auto writeActTargetObj = [&](const std::vector<AxbInteractionAction>& acts) {
            WriteU32(out, static_cast<std::uint32_t>(acts.size()));
            for (const auto& act : acts) {
                if (act.targetObjectBinding) {
                    WriteU8(out, 1u);
                    WriteString(out, act.targetObjectBinding->rawPath);
                    WriteString(out, act.targetObjectBinding->elementName);
                    WriteString(out, act.targetObjectBinding->relativeAncestorType);
                    WriteU8(out, act.targetObjectBinding->relativeSelf ? 1u : 0u);
                } else {
                    WriteU8(out, 0u);
                }
            }
        };
        WriteU32(out, static_cast<std::uint32_t>(styles.size()));
        for (const auto& st : styles) {
            WriteU32(out, static_cast<std::uint32_t>(st.triggers.size()));
            for (const auto& tr : st.triggers) {
                writeActTargetObj(tr.enterActions);
                writeActTargetObj(tr.exitActions);
            }
            WriteU32(out, static_cast<std::uint32_t>(st.eventTriggers.size()));
            for (const auto& et : st.eventTriggers) {
                writeActTargetObj(et.actions);
            }
        }
        WriteU32(out, static_cast<std::uint32_t>(controlTemplates.size()));
        for (const auto& ct : controlTemplates) {
            WriteU32(out, static_cast<std::uint32_t>(ct.triggers.size()));
            for (const auto& tr : ct.triggers) {
                writeActTargetObj(tr.enterActions);
                writeActTargetObj(tr.exitActions);
            }
            WriteU32(out, static_cast<std::uint32_t>(ct.eventTriggers.size()));
            for (const auto& et : ct.eventTriggers) {
                writeActTargetObj(et.actions);
            }
        }
    }

    {
        struct SetterBindEntry {
            std::uint32_t ordinal = 0u;
            const AxbCompiledBindingChain* chain = nullptr;
        };
        struct ActionBindEntry {
            std::uint32_t ordinal = 0u;
            const AxbCompiledBindingChain* valueBinding = nullptr;
            const AxbCompiledMultiBinding* valueMultiBinding = nullptr;
        };
        std::vector<SetterBindEntry> setterBindEntries;
        std::uint32_t setterOrdinal = 0u;
        VisitAllPackageSetters(*this, [&](const AxbPropertySetter& setter) {
            if (setter.binding) {
                setterBindEntries.push_back(SetterBindEntry{setterOrdinal, setter.binding.get()});
            }
            ++setterOrdinal;
        });
        std::vector<ActionBindEntry> actionBindEntries;
        std::uint32_t actionOrdinal = 0u;
        VisitAllPackageActions(*this, [&](const AxbInteractionAction& act) {
            if (act.valueBinding || act.valueMultiBinding) {
                actionBindEntries.push_back(ActionBindEntry{
                    actionOrdinal,
                    act.valueBinding.get(),
                    act.valueMultiBinding.get()});
            }
            ++actionOrdinal;
        });
        if (!setterBindEntries.empty() || !actionBindEntries.empty()) {
            WriteU32(out, kAxb4SetterActionBindSectionMagic);
            WriteU32(out, static_cast<std::uint32_t>(setterBindEntries.size()));
            for (const auto& entry : setterBindEntries) {
                WriteU32(out, entry.ordinal);
                writeFullChainRec(writeFullChainRec, writeMultiBindingRec, *entry.chain);
            }
            WriteU32(out, static_cast<std::uint32_t>(actionBindEntries.size()));
            for (const auto& entry : actionBindEntries) {
                WriteU32(out, entry.ordinal);
                std::uint8_t flags = 0u;
                if (entry.valueBinding) flags |= 0x01u;
                if (entry.valueMultiBinding) flags |= 0x02u;
                WriteU8(out, flags);
                if (entry.valueBinding) {
                    writeFullChainRec(writeFullChainRec, writeMultiBindingRec, *entry.valueBinding);
                }
                if (entry.valueMultiBinding) {
                    writeMultiBindingRec(writeFullChainRec, writeMultiBindingRec, *entry.valueMultiBinding);
                }
            }
        }
    }

    {
        struct TargetNullExtEntry {
            std::uint32_t ordinal = 0u;
            UnifiedValue value{};
        };
        std::vector<TargetNullExtEntry> extEntries;
        std::uint32_t ordinal = 0u;
        VisitAllNestedBindingChains(*this, [&](const AxbCompiledBindingChain& ch) {
            if (!std::holds_alternative<std::monostate>(ch.targetNullValue)) {
                extEntries.push_back(TargetNullExtEntry{ordinal, ch.targetNullValue});
            }
            ++ordinal;
        });
        if (!extEntries.empty()) {
            WriteU32(out, kAxb4TargetNullExtSectionMagic);
            WriteU32(out, static_cast<std::uint32_t>(extEntries.size()));
            for (const auto& entry : extEntries) {
                WriteU32(out, entry.ordinal);
                WriteValue(out, entry.value);
            }
        }
    }

    {
        struct EventTrigExtEntry {
            std::uint32_t ordinal = 0u;
            const AxbEventTrigger* et = nullptr;
        };
        struct ActionExtEntry {
            std::uint32_t ordinal = 0u;
            const AxbInteractionAction* act = nullptr;
        };
        std::vector<EventTrigExtEntry> etEntries;
        std::uint32_t etOrdinal = 0u;
        VisitAllPackageEventTriggers(*this, [&](const AxbEventTrigger& et) {
            if (et.keyCode != KeyCode::None ||
                et.modifiers != KeyMod_None ||
                et.activeOnFocus ||
                et.firedOnKeyUp ||
                et.timerIntervalMs != 0u ||
                et.totalTicks != 0 ||
                et.triggerSourceEvent != RoutedTriggerEvent::Loaded) {
                etEntries.push_back(EventTrigExtEntry{etOrdinal, &et});
            }
            ++etOrdinal;
        });
        std::vector<ActionExtEntry> actEntries;
        std::uint32_t actOrdinal = 0u;
        VisitAllPackageActions(*this, [&](const AxbInteractionAction& act) {
            if (act.isNullValue || act.commandTargetsWrapper) {
                actEntries.push_back(ActionExtEntry{actOrdinal, &act});
            }
            ++actOrdinal;
        });
        if (!etEntries.empty() || !actEntries.empty()) {
            WriteU32(out, kAxb4EventTrigExtSectionMagic);
            WriteU32(out, static_cast<std::uint32_t>(etEntries.size()));
            for (const auto& entry : etEntries) {
                WriteU32(out, entry.ordinal);
                WriteU16(out, static_cast<std::uint16_t>(entry.et->keyCode));
                WriteU8(out, entry.et->modifiers);
                std::uint8_t flags = 0u;
                if (entry.et->activeOnFocus) flags |= 0x01u;
                if (entry.et->firedOnKeyUp) flags |= 0x02u;
                WriteU8(out, flags);
                WriteU32(out, entry.et->timerIntervalMs);
                WriteU32(out, static_cast<std::uint32_t>(entry.et->totalTicks));
                WriteU8(out, static_cast<std::uint8_t>(entry.et->triggerSourceEvent));
            }
            WriteU32(out, static_cast<std::uint32_t>(actEntries.size()));
            for (const auto& entry : actEntries) {
                WriteU32(out, entry.ordinal);
                std::uint8_t flags = 0u;
                if (entry.act->isNullValue) flags |= 0x01u;
                if (entry.act->commandTargetsWrapper) flags |= 0x02u;
                WriteU8(out, flags);
            }
        }
    }

    return out;
}

namespace {

bool DeserializeAxbPackageWithDepth(
    const std::uint8_t* data,
    std::size_t size,
    AxbPackage& outPackage,
    std::string* errorMessage,
    std::uint32_t depth) {
    constexpr std::uint32_t kMaxNestedAxbDeserializeDepth = 8u;
    if (depth > kMaxNestedAxbDeserializeDepth) {
        if (errorMessage) *errorMessage = "AXB4 nested template recursion depth exceeded (max 8)";
        return false;
    }

    ByteReader r{data, size, 0};
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!r.ReadU32(magic) || magic != kAxb4Magic) {
        if (errorMessage) *errorMessage = "invalid AXB4 magic header";
        return false;
    }
    if (!r.ReadU32(version) || version != kAxb4Version) {
        if (errorMessage) *errorMessage = "unsupported AXB4 version";
        return false;
    }

    AxbPackage pkg{};
    if (!r.ReadString(pkg.className) || !r.ReadString(pkg.rootTypeName)) {
        if (errorMessage) *errorMessage = "corrupted AXB4 header strings";
        return false;
    }

    std::uint32_t strCount = 0;
    if (!r.ReadU32(strCount) || strCount > (r.size - r.offset) / 4u) return false;
    pkg.stringTable.resize(strCount);
    for (std::uint32_t i = 0; i < strCount; ++i) {
        if (!r.ReadString(pkg.stringTable[i])) return false;
    }

    std::uint32_t ncCount = 0;
    if (!r.ReadU32(ncCount) || ncCount > (r.size - r.offset) / 20u) return false;
    pkg.namedControls.resize(ncCount);
    for (std::uint32_t i = 0; i < ncCount; ++i) {
        auto& nc = pkg.namedControls[i];
        if (!r.ReadString(nc.name) ||
            !r.ReadString(nc.typeName) ||
            !r.ReadU64(nc.typeSymbolId) ||
            !r.ReadU32(nc.fieldIndexInClass)) {
            return false;
        }
    }

    std::uint32_t chainCount = 0;
    if (!r.ReadU32(chainCount) || chainCount > (r.size - r.offset) / 11u) return false;
    pkg.bindingChains.resize(chainCount);
    for (std::uint32_t i = 0; i < chainCount; ++i) {
        auto& chain = pkg.bindingChains[i];
        std::uint8_t modeRaw = 0;
        if (!r.ReadString(chain.rawPath) ||
            !r.ReadU16(chain.targetPropertyId) ||
            !r.ReadU8(modeRaw) ||
            !ReadHopVector(r, chain.hops)) {
            return false;
        }
        chain.mode = static_cast<BindingMode>(modeRaw);
    }

    std::uint32_t evCount = 0;
    if (!r.ReadU32(evCount) || evCount > (r.size - r.offset) / 20u) return false;
    pkg.eventBindings.resize(evCount);
    for (std::uint32_t i = 0; i < evCount; ++i) {
        auto& ev = pkg.eventBindings[i];
        if (!r.ReadString(ev.controlName) ||
            !r.ReadString(ev.eventName) ||
            !r.ReadString(ev.handlerMethodName) ||
            !r.ReadU64(ev.handlerSymbolId)) {
            return false;
        }
    }

    std::uint32_t instCount = 0;
    if (!r.ReadU32(instCount) || instCount > (r.size - r.offset) / 21u) return false;
    pkg.instructions.resize(instCount);
    for (std::uint32_t i = 0; i < instCount; ++i) {
        auto& inst = pkg.instructions[i];
        std::uint8_t opRaw = 0;
        if (!r.ReadU8(opRaw) ||
            !r.ReadU16(inst.propId) ||
            !r.ReadU8(inst.flags) ||
            !r.ReadU32(inst.strIdx0) ||
            !r.ReadU32(inst.strIdx1) ||
            !r.ReadU64(inst.symbolId) ||
            !r.ReadValue(inst.literalValue)) {
            return false;
        }
        inst.opcode = static_cast<AxbOpcode>(opRaw);
    }

    if (r.offset < r.size) {
        if (!r.ReadU32(pkg.rootHandleFieldIndex)) {
            if (errorMessage) *errorMessage = "truncated AXB4 rootHandleFieldIndex";
            return false;
        }
    }
    if (r.offset < r.size) {
        std::uint32_t resCount = 0;
        if (!r.ReadU32(resCount) || resCount > (r.size - r.offset) / 5u) {
            if (errorMessage) *errorMessage = "corrupted AXB4 resources section";
            return false;
        }
        pkg.resources.resize(resCount);
        for (std::uint32_t i = 0; i < resCount; ++i) {
            if (!r.ReadString(pkg.resources[i].first) ||
                !r.ReadValue(pkg.resources[i].second)) {
                if (errorMessage) *errorMessage = "corrupted AXB4 resource entry";
                return false;
            }
        }
    }
    if (r.offset < r.size) {
        std::uint32_t fbCount = 0;
        if (!r.ReadU32(fbCount) || fbCount > pkg.bindingChains.size() ||
            fbCount > (r.size - r.offset)) {
            if (errorMessage) *errorMessage = "corrupted AXB4 binding fallback section";
            return false;
        }
        for (std::uint32_t i = 0; i < fbCount; ++i) {
            if (!r.ReadValue(pkg.bindingChains[i].fallbackValue)) {
                if (errorMessage) *errorMessage = "corrupted AXB4 binding fallback value";
                return false;
            }
        }
        if (r.offset + 4u <= r.size) {
            const std::size_t savedOff = r.offset;
            std::uint32_t tnMagic = 0u;
            if (r.ReadU32(tnMagic) && tnMagic == 0x344E5441u) {
                std::uint32_t tnCount = 0u;
                if (!r.ReadU32(tnCount) || tnCount > pkg.bindingChains.size() ||
                    tnCount > (r.size - r.offset)) {
                    if (errorMessage) *errorMessage = "corrupted AXB4 TargetNullValue section";
                    return false;
                }
                for (std::uint32_t i = 0u; i < tnCount; ++i) {
                    if (!r.ReadValue(pkg.bindingChains[i].targetNullValue)) {
                        if (errorMessage) *errorMessage = "corrupted AXB4 TargetNullValue entry";
                        return false;
                    }
                }
            } else {
                r.offset = savedOff;
            }
        }
    }
    auto failStcSection = [&](const char* msg) -> bool {
        if (errorMessage) *errorMessage = msg;
        return false;
    };

    constexpr std::uint8_t kMaxEasingKindRaw = static_cast<std::uint8_t>(EasingFunctionKind::Spline);
    constexpr std::uint8_t kMaxEasingModeRaw = static_cast<std::uint8_t>(EasingMode::EaseInOut);

    auto readStoryboard = [&](AxbStoryboard& sb) -> bool {
        std::uint32_t tlCount = 0;
        if (!r.ReadString(sb.key) || !r.ReadU32(tlCount) || tlCount > (r.size - r.offset) / 16u) {
            return failStcSection("corrupted AXB4 Storyboard header");
        }
        sb.timelines.resize(tlCount);
        for (std::uint32_t t = 0; t < tlCount; ++t) {
            auto& tl = sb.timelines[t];
            std::uint8_t tlFlags = 0u, ek = 0u, em = 0u;
            std::uint32_t repRaw = 1u, kfCount = 0u;
            if (!r.ReadString(tl.targetName) ||
                !r.ReadU16(tl.targetPropertyId) ||
                !r.ReadF32(tl.beginTimeSeconds) ||
                !r.ReadF32(tl.durationSeconds) ||
                !r.ReadU8(tlFlags) ||
                (((tlFlags & 0x20u) != 0u) &&
                 (!r.ReadF32(tl.speedRatio) ||
                  !r.ReadF32(tl.accelerationRatio) ||
                  !r.ReadF32(tl.decelerationRatio))) ||
                (((tlFlags & 0x40u) != 0u) &&
                 (!r.ReadU16(tl.customAnim.animTypeId) ||
                  !r.ReadU16(tl.customAnim.reserved) ||
                  !r.ReadU32(tl.customAnim.flags) ||
                  !r.ReadF32(tl.customAnim.params[0]) ||
                  !r.ReadF32(tl.customAnim.params[1]) ||
                  !r.ReadF32(tl.customAnim.params[2]) ||
                  !r.ReadF32(tl.customAnim.params[3]))) ||
                !r.ReadValue(tl.fromValue) ||
                !r.ReadValue(tl.toValue) ||
                !r.ReadU8(ek) ||
                !r.ReadU8(em) ||
                ek > kMaxEasingKindRaw ||
                em > kMaxEasingModeRaw ||
                !r.ReadU32(repRaw) ||
                !r.ReadU32(kfCount) ||
                kfCount > (r.size - r.offset) / 8u) {
                return failStcSection("corrupted AXB4 AnimationTimeline entry");
            }
            tl.hasFrom = (tlFlags & 0x01u) != 0u;
            tl.hasTo = (tlFlags & 0x02u) != 0u;
            tl.autoReverse = (tlFlags & 0x04u) != 0u;
            tl.fillBehavior = ((tlFlags & 0x08u) != 0u) ? FillBehavior::Stop : FillBehavior::HoldEnd;
            tl.isBy = (tlFlags & 0x10u) != 0u;
            tl.easingKind = static_cast<EasingFunctionKind>(ek);
            tl.easingMode = static_cast<EasingMode>(em);
            tl.repeatCount = static_cast<std::int32_t>(repRaw);
            tl.keyFrames.resize(kfCount);
            for (std::uint32_t k = 0; k < kfCount; ++k) {
                auto& kf = tl.keyFrames[k];
                std::uint8_t kek = 0u, kem = 0u, kdisc = 0u;
                if (!r.ReadF32(kf.timeSeconds) ||
                    !r.ReadValue(kf.value) ||
                    !r.ReadU8(kek) ||
                    !r.ReadU8(kem) ||
                    !r.ReadU8(kdisc) ||
                    kek > kMaxEasingKindRaw ||
                    kem > kMaxEasingModeRaw) {
                    return failStcSection("corrupted AXB4 KeyFrame entry");
                }
                kf.easingKind = static_cast<EasingFunctionKind>(kek);
                kf.easingMode = static_cast<EasingMode>(kem);
                kf.isDiscrete = (kdisc & 0x01u) != 0u;
                kf.hasKeySpline = (kdisc & 0x02u) != 0u;
                if (kf.hasKeySpline) {
                    if (!r.ReadF32(kf.splineX1) ||
                        !r.ReadF32(kf.splineY1) ||
                        !r.ReadF32(kf.splineX2) ||
                        !r.ReadF32(kf.splineY2)) {
                        return failStcSection("corrupted AXB4 KeySpline entry");
                    }
                }
            }
        }
        return true;
    };

    auto readSetterList = [&](std::vector<AxbPropertySetter>& setters, const char* hdrErr, const char* itemErr) -> bool {
        std::uint32_t sc = 0;
        if (!r.ReadU32(sc) || sc > (r.size - r.offset) / 7u) {
            return failStcSection(hdrErr);
        }
        setters.resize(sc);
        for (std::uint32_t i = 0; i < sc; ++i) {
            if (!r.ReadString(setters[i].targetName) ||
                !r.ReadU16(setters[i].propId) ||
                !r.ReadValue(setters[i].value)) {
                return failStcSection(itemErr);
            }
        }
        return true;
    };

    constexpr std::uint32_t kSecBit_MSDF = 1u << 0;
    constexpr std::uint32_t kSecBit_STC5 = 1u << 1;
    constexpr std::uint32_t kSecBit_DTP5 = 1u << 2;
    constexpr std::uint32_t kSecBit_MOT6 = 1u << 3;
    constexpr std::uint32_t kSecBit_CTP1 = 1u << 4;
    constexpr std::uint32_t kSecBit_MR12 = 1u << 5;
    constexpr std::uint32_t kSecBit_AP14 = 1u << 6;
    constexpr std::uint32_t kSecBit_AP15 = 1u << 7;
    constexpr std::uint32_t kSecBit_TNE4 = 1u << 8;
    constexpr std::uint32_t kSecBit_SBA4 = 1u << 9;
    constexpr std::uint32_t kSecBit_ETE4 = 1u << 10;
    std::uint32_t seenSectionMask = 0u;

    auto readFullChainRec = [&](auto& selfChain, auto& selfMulti, AxbCompiledBindingChain& ch, int depth) -> bool {
        if (depth > 8) return false;
        std::uint8_t modeRaw = 0u, relSelf = 0u, convRaw = 0u, hasNested = 0u;
        if (!r.ReadString(ch.rawPath) ||
            !r.ReadU16(ch.targetPropertyId) ||
            !r.ReadU8(modeRaw) ||
            modeRaw > static_cast<std::uint8_t>(BindingMode::OneTime) ||
            !r.ReadValue(ch.fallbackValue) ||
            !ReadHopVector(r, ch.hops) ||
            !r.ReadString(ch.elementName) ||
            !r.ReadString(ch.relativeAncestorType) ||
            !r.ReadU8(relSelf) ||
            !r.ReadU8(convRaw) ||
            convRaw > static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost) ||
            !r.ReadString(ch.converterKey) ||
            !r.ReadString(ch.converterParameter) ||
            !r.ReadString(ch.stringFormat) ||
            !r.ReadU64(ch.converterSymbolId) ||
            !r.ReadU64(ch.convertBackSymbolId) ||
            !r.ReadU8(hasNested)) {
            return false;
        }
        ch.mode = static_cast<BindingMode>(modeRaw);
        ch.relativeSelf = (relSelf != 0u);
        ch.converter = static_cast<ValueConverterKind>(convRaw);
        if (hasNested != 0u) {
            auto nested = std::make_shared<AxbCompiledMultiBinding>();
            if (!selfMulti(selfChain, selfMulti, *nested, depth + 1)) {
                return false;
            }
            ch.nestedMultiBinding = std::move(nested);
        } else {
            ch.nestedMultiBinding.reset();
        }
        return true;
    };

    auto readMultiBindingRec = [&](auto& selfChain, auto& selfMulti, AxbCompiledMultiBinding& mb, int depth) -> bool {
        if (depth > 8) return false;
        std::uint8_t modeRaw = 0u, convRaw = 0u;
        std::uint32_t srcCount = 0u;
        if (!r.ReadU16(mb.targetPropertyId) ||
            !r.ReadU8(modeRaw) ||
            modeRaw > static_cast<std::uint8_t>(BindingMode::OneTime) ||
            !r.ReadU8(convRaw) ||
            convRaw > static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost) ||
            !r.ReadString(mb.converterKey) ||
            !r.ReadString(mb.converterParameter) ||
            !r.ReadString(mb.stringFormat) ||
            !r.ReadValue(mb.fallbackValue) ||
            !r.ReadU64(mb.converterSymbolId) ||
            !r.ReadU64(mb.convertBackSymbolId) ||
            !r.ReadU32(srcCount) ||
            srcCount > (r.size - r.offset) / 16u) {
            return false;
        }
        mb.mode = static_cast<BindingMode>(modeRaw);
        mb.converter = static_cast<ValueConverterKind>(convRaw);
        mb.sources.resize(srcCount);
        for (std::uint32_t i = 0u; i < srcCount; ++i) {
            if (!selfChain(selfChain, selfMulti, mb.sources[i], depth + 1)) {
                return false;
            }
        }
        return true;
    };

    while (r.offset < r.size) {
        std::uint32_t trailMagic = 0;
        if (!r.ReadU32(trailMagic)) {
            return failStcSection("corrupted AXB4 trailing section magic");
        }
        if (trailMagic == kAxb4MtsdfSectionMagic) {
            if ((seenSectionMask & kSecBit_MSDF) != 0u) {
                return failStcSection("corrupted AXB4 duplicate MSDF section");
            }
            seenSectionMask |= kSecBit_MSDF;
            std::uint32_t mtsdfCount = 0;
            if (!r.ReadU32(mtsdfCount) || mtsdfCount > (r.size - r.offset) / 56u) {
                return failStcSection("corrupted AXB4 MTSDF atlas section");
            }
            pkg.mtsdfPaths.resize(mtsdfCount);
            for (std::uint32_t i = 0; i < mtsdfCount; ++i) {
                auto& mp = pkg.mtsdfPaths[i];
                if (!r.ReadU64(mp.pathHash) ||
                    !r.ReadF32(mp.boundsX) ||
                    !r.ReadF32(mp.boundsY) ||
                    !r.ReadF32(mp.boundsW) ||
                    !r.ReadF32(mp.boundsH) ||
                    !r.ReadF32(mp.u0) ||
                    !r.ReadF32(mp.v0) ||
                    !r.ReadF32(mp.u1) ||
                    !r.ReadF32(mp.v1) ||
                    !r.ReadU16(mp.atlasWidth) ||
                    !r.ReadU16(mp.atlasHeight) ||
                    !r.ReadU16(mp.cellWidth) ||
                    !r.ReadU16(mp.cellHeight) ||
                    !r.ReadF32(mp.pxRange) ||
                    !r.ReadBytes(mp.mtsdfRgbaPixels)) {
                    return failStcSection("corrupted AXB4 MTSDF entry");
                }
                mp.ComputeRowBounds();
            }
        } else if (trailMagic == kAxb4StyleAnimSectionMagic) {
            if ((seenSectionMask & kSecBit_STC5) != 0u) {
                return failStcSection("corrupted AXB4 duplicate STC5 section");
            }
            if ((seenSectionMask & (kSecBit_DTP5 | kSecBit_AP14)) != 0u) {
                return failStcSection("corrupted AXB4 out-of-order STC5 section");
            }
            seenSectionMask |= kSecBit_STC5;

            std::uint32_t styleCount = 0;
            if (!r.ReadU32(styleCount) || styleCount > (r.size - r.offset) / 16u) {
                return failStcSection("corrupted AXB4 Style section header");
            }
            pkg.styles.resize(styleCount);
            for (std::uint32_t si = 0; si < styleCount; ++si) {
                auto& style = pkg.styles[si];
                std::uint32_t trigCount = 0;
                if (!r.ReadString(style.key) ||
                    !r.ReadString(style.targetTypeName) ||
                    !r.ReadString(style.basedOnKey) ||
                    !readSetterList(style.setters, "corrupted AXB4 Setter list header", "corrupted AXB4 Setter entry") ||
                    !r.ReadU32(trigCount) ||
                    trigCount > (r.size - r.offset) / 12u) {
                    return failStcSection("corrupted AXB4 Style definition");
                }
                for (const auto& s : style.setters) {
                    if (s.propId == PropId_Template) {
                        if (const auto* sv = std::get_if<StringValue>(&s.value)) {
                            style.controlTemplateKey = sv->str();
                        }
                    }
                }
                style.triggers.resize(trigCount);
                for (std::uint32_t ti = 0; ti < trigCount; ++ti) {
                    auto& trig = style.triggers[ti];
                    std::uint8_t ovLocal = 0u;
                    std::uint32_t condCount = 0, enterCount = 0, exitCount = 0;
                    if (!r.ReadU8(ovLocal) ||
                        !r.ReadU32(condCount) ||
                        condCount > (r.size - r.offset) / 3u) {
                        return failStcSection("corrupted AXB4 StyleTrigger header");
                    }
                    trig.overrideLocalValues = (ovLocal != 0u);
                    trig.conditions.resize(condCount);
                    for (std::uint32_t ci = 0; ci < condCount; ++ci) {
                        if (!r.ReadU16(trig.conditions[ci].propId) ||
                            !r.ReadValue(trig.conditions[ci].expectedValue)) {
                            return failStcSection("corrupted AXB4 TriggerCondition entry");
                        }
                    }
                    if (!readSetterList(trig.setters, "corrupted AXB4 Setter list header", "corrupted AXB4 Setter entry") ||
                        !r.ReadU32(enterCount) ||
                        enterCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 Trigger EnterActions header");
                    }
                    trig.enterStoryboards.resize(enterCount);
                    for (std::uint32_t ei = 0; ei < enterCount; ++ei) {
                        if (!readStoryboard(trig.enterStoryboards[ei])) return false;
                    }
                    if (!r.ReadU32(exitCount) || exitCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 Trigger ExitActions header");
                    }
                    trig.exitStoryboards.resize(exitCount);
                    for (std::uint32_t xi = 0; xi < exitCount; ++xi) {
                        if (!readStoryboard(trig.exitStoryboards[xi])) return false;
                    }
                }
                std::uint32_t vsgCount = 0;
                if (!r.ReadU32(vsgCount) || vsgCount > (r.size - r.offset) / 10u) {
                    return failStcSection("corrupted AXB4 VisualStateGroup list header");
                }
                style.visualStateGroups.resize(vsgCount);
                for (std::uint32_t gi = 0; gi < vsgCount; ++gi) {
                    auto& vsg = style.visualStateGroups[gi];
                    std::uint8_t dek = 0u, dem = 0u;
                    std::uint32_t vsCount = 0;
                    if (!r.ReadString(vsg.name) ||
                        !r.ReadF32(vsg.defaultTransitionDuration) ||
                        !r.ReadU8(dek) ||
                        !r.ReadU8(dem) ||
                        dek > kMaxEasingKindRaw ||
                        dem > kMaxEasingModeRaw ||
                        !r.ReadU32(vsCount) ||
                        vsCount > (r.size - r.offset) / 8u) {
                        return failStcSection("corrupted AXB4 VisualStateGroup entry");
                    }
                    vsg.defaultEasingKind = static_cast<EasingFunctionKind>(dek);
                    vsg.defaultEasingMode = static_cast<EasingMode>(dem);
                    vsg.states.resize(vsCount);
                    for (std::uint32_t vi = 0; vi < vsCount; ++vi) {
                        if (!r.ReadString(vsg.states[vi].name) ||
                            !readSetterList(vsg.states[vi].setters, "corrupted AXB4 Setter list header", "corrupted AXB4 Setter entry") ||
                            !readStoryboard(vsg.states[vi].storyboard)) {
                            return failStcSection("corrupted AXB4 VisualState entry");
                        }
                    }
                }
            }
            std::uint32_t sbCount = 0;
            if (!r.ReadU32(sbCount) || sbCount > (r.size - r.offset) / 4u) {
                return failStcSection("corrupted AXB4 Storyboards list header");
            }
            pkg.storyboards.resize(sbCount);
            for (std::uint32_t sbi = 0; sbi < sbCount; ++sbi) {
                if (!readStoryboard(pkg.storyboards[sbi])) return false;
            }
        } else if (trailMagic == kAxb4Phase5DSectionMagic) {
            if ((seenSectionMask & kSecBit_DTP5) != 0u) {
                return failStcSection("corrupted AXB4 duplicate DTP5 section");
            }
            if ((seenSectionMask & kSecBit_AP14) != 0u) {
                return failStcSection("corrupted AXB4 out-of-order DTP5 section after AP14");
            }
            seenSectionMask |= kSecBit_DTP5;
            constexpr std::uint8_t kMaxConverterRaw =
                static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost);
            constexpr std::uint8_t kMaxConditionSourceRaw =
                static_cast<std::uint8_t>(TriggerConditionSource::AncestorProperty);
            constexpr std::uint8_t kMaxRoutedEventRaw =
                static_cast<std::uint8_t>(RoutedTriggerEvent::MediaEnded);

            std::uint32_t bcExtCount = 0;
            if (!r.ReadU32(bcExtCount) || bcExtCount != pkg.bindingChains.size()) {
                return failStcSection("corrupted AXB4 Phase 5D bindingChains header");
            }
            for (std::uint32_t i = 0; i < bcExtCount; ++i) {
                std::uint8_t relSelf = 0u, conv = 0u;
                if (!r.ReadString(pkg.bindingChains[i].elementName) ||
                    !r.ReadString(pkg.bindingChains[i].relativeAncestorType) ||
                    !r.ReadU8(relSelf) ||
                    !r.ReadU8(conv) ||
                    conv > kMaxConverterRaw) {
                    return failStcSection("corrupted AXB4 Phase 5D bindingChain extension");
                }
                pkg.bindingChains[i].relativeSelf = (relSelf != 0u);
                pkg.bindingChains[i].converter = static_cast<ValueConverterKind>(conv);
            }

            std::uint32_t stExtCount = 0;
            if (!r.ReadU32(stExtCount) || stExtCount != pkg.styles.size()) {
                return failStcSection("corrupted AXB4 Phase 5D styles extension header");
            }
            for (std::uint32_t si = 0; si < stExtCount; ++si) {
                auto& st = pkg.styles[si];
                std::uint32_t trigExtCount = 0;
                if (!r.ReadU32(trigExtCount) || trigExtCount != st.triggers.size()) {
                    return failStcSection("corrupted AXB4 Phase 5D triggers extension header");
                }
                for (std::uint32_t ti = 0; ti < trigExtCount; ++ti) {
                    auto& trig = st.triggers[ti];
                    std::uint32_t condExtCount = 0;
                    if (!r.ReadU32(condExtCount) || condExtCount != trig.conditions.size()) {
                        return failStcSection("corrupted AXB4 Phase 5D conditions extension header");
                    }
                    for (std::uint32_t ci = 0; ci < condExtCount; ++ci) {
                        auto& cond = trig.conditions[ci];
                        std::uint8_t sk = 0u, cv = 0u, expNull = 0u;
                        if (!r.ReadU8(sk) ||
                            !r.ReadU8(cv) ||
                            !r.ReadU8(expNull) ||
                            sk > kMaxConditionSourceRaw ||
                            cv > kMaxConverterRaw ||
                            !r.ReadString(cond.sourceName) ||
                            !r.ReadString(cond.bindingPath) ||
                            !ReadHopVector(r, cond.compiledHops)) {
                            return failStcSection("corrupted AXB4 Phase 5D condition extension");
                        }
                        cond.sourceKind = static_cast<TriggerConditionSource>(sk);
                        cond.converter = static_cast<ValueConverterKind>(cv);
                        cond.expectNull = (expNull != 0u);
                    }
                }

                std::uint32_t etCount = 0;
                if (!r.ReadU32(etCount) || etCount > (r.size - r.offset) / 6u) {
                    return failStcSection("corrupted AXB4 Phase 5D EventTriggers header");
                }
                st.eventTriggers.resize(etCount);
                for (std::uint32_t ei = 0; ei < etCount; ++ei) {
                    auto& et = st.eventTriggers[ei];
                    std::uint8_t ek = 0u;
                    std::uint32_t sbCount = 0;
                    if (!r.ReadU8(ek) ||
                        ek > kMaxRoutedEventRaw ||
                        !r.ReadString(et.sourceName) ||
                        !r.ReadU32(sbCount) ||
                        sbCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 Phase 5D EventTrigger entry");
                    }
                    et.eventKind = static_cast<RoutedTriggerEvent>(ek);
                    et.storyboards.resize(sbCount);
                    for (std::uint32_t sbi = 0; sbi < sbCount; ++sbi) {
                        if (!readStoryboard(et.storyboards[sbi])) return false;
                    }
                }
            }

            std::uint32_t dtCount = 0;
            if (!r.ReadU32(dtCount) || dtCount > (r.size - r.offset) / 12u) {
                return failStcSection("corrupted AXB4 Phase 5D DataTemplates header");
            }
            pkg.dataTemplates.resize(dtCount);
            for (std::uint32_t di = 0; di < dtCount; ++di) {
                auto& dt = pkg.dataTemplates[di];
                std::vector<std::uint8_t> subBytes;
                if (!r.ReadString(dt.key) ||
                    !r.ReadString(dt.dataTypeName) ||
                    !r.ReadBytes(subBytes)) {
                    return failStcSection("corrupted AXB4 Phase 5D DataTemplate entry");
                }
                if (!subBytes.empty()) {
                    auto subPkg = std::make_shared<AxbPackage>();
                    if (!DeserializeAxbPackageWithDepth(
                            subBytes.data(), subBytes.size(), *subPkg, errorMessage, depth + 1u)) {
                        return false;
                    }
                    dt.templatePackage = std::move(subPkg);
                }
            }
        } else if (trailMagic == kAxb4MotionSectionMagic) {
            if ((seenSectionMask & kSecBit_MOT6) != 0u) {
                return failStcSection("corrupted AXB4 duplicate MOT6 section");
            }
            seenSectionMask |= kSecBit_MOT6;
            // 1. Flat keyframes
            std::uint32_t kfCount = 0;
            if (!r.ReadU32(kfCount) || kfCount > (r.size - r.offset) / 28u) {
                return failStcSection("corrupted AXB4 MOT6 motionKeyframes header");
            }
            pkg.motionKeyframes.resize(kfCount);
            for (std::uint32_t i = 0; i < kfCount; ++i) {
                auto& kf = pkg.motionKeyframes[i];
                std::uint16_t y1Raw = 0u, y2Raw = 0u;
                if (!r.ReadF32(kf.timeSec) ||
                    !r.ReadF32(kf.value.x) ||
                    !r.ReadF32(kf.value.y) ||
                    !r.ReadF32(kf.value.z) ||
                    !r.ReadF32(kf.value.w) ||
                    !r.ReadU8(kf.interp) ||
                    !r.ReadU8(kf.easeKindOrCtrlX1) ||
                    !r.ReadU8(kf.easeModeOrCtrlX2) ||
                    !r.ReadU8(kf.flags) ||
                    !r.ReadU16(y1Raw) ||
                    !r.ReadU16(y2Raw)) {
                    return failStcSection("corrupted AXB4 MOT6 keyframe entry");
                }
                kf.ctrlY1Fixed8 = static_cast<std::int16_t>(y1Raw);
                kf.ctrlY2Fixed8 = static_cast<std::int16_t>(y2Raw);
            }

            // 2. Motion tracks
            std::uint32_t trCount = 0;
            if (!r.ReadU32(trCount) || trCount > (r.size - r.offset) / 16u) {
                return failStcSection("corrupted AXB4 MOT6 motionTracks header");
            }
            pkg.motionTracks.resize(trCount);
            for (std::uint32_t i = 0; i < trCount; ++i) {
                auto& tr = pkg.motionTracks[i];
                if (!r.ReadU16(tr.localElementIdx) ||
                    !r.ReadU16(tr.targetPropId) ||
                    !r.ReadU8(tr.channelCount) ||
                    !r.ReadU8(tr.quantizeInlineW) ||
                    !r.ReadU16(tr.reserved) ||
                    !r.ReadU32(tr.firstKeyframeIdx) ||
                    !r.ReadU32(tr.keyframeCount)) {
                    return failStcSection("corrupted AXB4 MOT6 track entry");
                }
            }

            // 3. Timeline event markers
            std::uint32_t mevCount = 0;
            if (!r.ReadU32(mevCount) || mevCount > (r.size - r.offset) / 24u) {
                return failStcSection("corrupted AXB4 MOT6 motionEvents header");
            }
            pkg.motionEvents.resize(mevCount);
            for (std::uint32_t i = 0; i < mevCount; ++i) {
                auto& mev = pkg.motionEvents[i];
                std::uint32_t ipRaw = 0u;
                if (!r.ReadF32(mev.timeSec) ||
                    !r.ReadU32(mev.eventNameStrIdx) ||
                    !r.ReadU64(mev.handlerSymbolId) ||
                    !r.ReadF32(mev.floatPayload) ||
                    !r.ReadU32(ipRaw)) {
                    return failStcSection("corrupted AXB4 MOT6 event marker entry");
                }
                mev.intPayload = static_cast<std::int32_t>(ipRaw);
            }

            // 4. Motion clips
            std::uint32_t clCount = 0;
            if (!r.ReadU32(clCount) || clCount > (r.size - r.offset) / 26u) {
                return failStcSection("corrupted AXB4 MOT6 motionClips header");
            }
            pkg.motionClips.resize(clCount);
            for (std::uint32_t i = 0; i < clCount; ++i) {
                auto& cl = pkg.motionClips[i];
                std::uint8_t loopRaw = 0u, revRaw = 0u;
                if (!r.ReadU32(cl.nameStrIdx) ||
                    !r.ReadF32(cl.durationSec) ||
                    !r.ReadU8(loopRaw) ||
                    !r.ReadU8(revRaw) ||
                    !r.ReadU32(cl.firstTrackIdx) ||
                    !r.ReadU32(cl.trackCount) ||
                    !r.ReadU32(cl.firstEventIdx) ||
                    !r.ReadU32(cl.eventCount)) {
                    return failStcSection("corrupted AXB4 MOT6 clip entry");
                }
                cl.isLooping = (loopRaw != 0u);
                cl.autoReverse = (revRaw != 0u);
            }

            // 5. Transform constraints
            std::uint32_t tcCount = 0;
            if (!r.ReadU32(tcCount) || tcCount > (r.size - r.offset) / 4u) {
                return failStcSection("corrupted AXB4 MOT6 transformConstraints header");
            }
            pkg.transformConstraints.resize(tcCount);
            for (std::uint32_t i = 0; i < tcCount; ++i) {
                auto& tc = pkg.transformConstraints[i];
                if (!r.ReadU16(tc.childLocalElementIdx) ||
                    !r.ReadU16(tc.parentLocalElementIdx)) {
                    return failStcSection("corrupted AXB4 MOT6 transformConstraint entry");
                }
            }

            // 6. State machine conditions
            std::uint32_t scCount = 0;
            if (!r.ReadU32(scCount) || scCount > (r.size - r.offset) / 8u) {
                return failStcSection("corrupted AXB4 MOT6 smConditions header");
            }
            pkg.smConditions.resize(scCount);
            for (std::uint32_t i = 0; i < scCount; ++i) {
                auto& sc = pkg.smConditions[i];
                std::uint8_t opRaw = 0u;
                if (!r.ReadU16(sc.inputSlotIdx) ||
                    !r.ReadU8(opRaw) ||
                    !r.ReadU8(sc.reserved) ||
                    !r.ReadF32(sc.threshold) ||
                    opRaw > static_cast<std::uint8_t>(SmCompareOp::TriggerFired)) {
                    return failStcSection("corrupted AXB4 MOT6 smCondition entry");
                }
                sc.op = static_cast<SmCompareOp>(opRaw);
            }

            // 7. State machine transitions
            std::uint32_t smTrCount = 0;
            if (!r.ReadU32(smTrCount) || smTrCount > (r.size - r.offset) / 24u) {
                return failStcSection("corrupted AXB4 MOT6 smTransitions header");
            }
            pkg.smTransitions.resize(smTrCount);
            for (std::uint32_t i = 0; i < smTrCount; ++i) {
                auto& tr = pkg.smTransitions[i];
                std::uint8_t ek = 0u, em = 0u, hasExit = 0u, selfInt = 0u;
                if (!r.ReadU16(tr.fromStateIdx) ||
                    !r.ReadU16(tr.toStateIdx) ||
                    !r.ReadF32(tr.crossfadeDurationSec) ||
                    !r.ReadU8(ek) ||
                    !r.ReadU8(em) ||
                    !r.ReadU8(hasExit) ||
                    !r.ReadU8(selfInt) ||
                    !r.ReadF32(tr.exitTimeNormalized01) ||
                    !r.ReadU32(tr.firstConditionIdx) ||
                    !r.ReadU16(tr.conditionCount) ||
                    !r.ReadU16(tr.priority) ||
                    ek > kMaxEasingKindRaw ||
                    em > kMaxEasingModeRaw) {
                    return failStcSection("corrupted AXB4 MOT6 smTransition entry");
                }
                tr.easeKind = static_cast<EasingFunctionKind>(ek);
                tr.easeMode = static_cast<EasingMode>(em);
                tr.hasExitTime = (hasExit != 0u);
                tr.allowSelfInterrupt = (selfInt != 0u);
            }

            // 8. State machine states
            std::uint32_t smStCount = 0;
            if (!r.ReadU32(smStCount) || smStCount > (r.size - r.offset) / 24u) {
                return failStcSection("corrupted AXB4 MOT6 smStates header");
            }
            pkg.smStates.resize(smStCount);
            for (std::uint32_t i = 0; i < smStCount; ++i) {
                auto& st = pkg.smStates[i];
                std::uint8_t kRaw = 0u;
                if (!r.ReadU32(st.nameStrIdx) ||
                    !r.ReadU8(kRaw) ||
                    !r.ReadU8(st.reserved) ||
                    !r.ReadU16(st.primaryClipIdx) ||
                    !r.ReadU16(st.secondaryClipIdx) ||
                    !r.ReadU16(st.progressInputSlot) ||
                    !r.ReadF32(st.speedMultiplier) ||
                    !r.ReadF32(st.blendRangeMin) ||
                    !r.ReadF32(st.blendRangeMax) ||
                    kRaw > static_cast<std::uint8_t>(SmStateKind::ExitState)) {
                    return failStcSection("corrupted AXB4 MOT6 smState entry");
                }
                st.kind = static_cast<SmStateKind>(kRaw);
            }

            // 9. State machine layers
            std::uint32_t smLyCount = 0;
            if (!r.ReadU32(smLyCount) || smLyCount > (r.size - r.offset) / 24u) {
                return failStcSection("corrupted AXB4 MOT6 smLayers header");
            }
            pkg.smLayers.resize(smLyCount);
            for (std::uint32_t i = 0; i < smLyCount; ++i) {
                auto& ly = pkg.smLayers[i];
                std::uint8_t bmRaw = 0u;
                if (!r.ReadU32(ly.nameStrIdx) ||
                    !r.ReadU8(bmRaw) ||
                    !r.ReadU8(ly.reserved) ||
                    !r.ReadU16(ly.entryStateIdx) ||
                    !r.ReadF32(ly.layerWeight) ||
                    !r.ReadU32(ly.firstStateIdx) ||
                    !r.ReadU16(ly.stateCount) ||
                    !r.ReadU32(ly.firstTransitionIdx) ||
                    !r.ReadU16(ly.transitionCount) ||
                    bmRaw > static_cast<std::uint8_t>(SmLayerBlendMode::Additive)) {
                    return failStcSection("corrupted AXB4 MOT6 smLayer entry");
                }
                ly.blendMode = static_cast<SmLayerBlendMode>(bmRaw);
            }

            // 10. Pointer listeners
            std::uint32_t lsCount = 0;
            if (!r.ReadU32(lsCount) || lsCount > (r.size - r.offset) / 10u) {
                return failStcSection("corrupted AXB4 MOT6 smListeners header");
            }
            pkg.smListeners.resize(lsCount);
            for (std::uint32_t i = 0; i < lsCount; ++i) {
                auto& ls = pkg.smListeners[i];
                std::uint8_t evRaw = 0u, actRaw = 0u;
                if (!r.ReadU16(ls.hitTargetElementIdx) ||
                    !r.ReadU8(evRaw) ||
                    !r.ReadU8(actRaw) ||
                    !r.ReadU16(ls.targetInputSlot) ||
                    !r.ReadF32(ls.actionValue) ||
                    evRaw > static_cast<std::uint8_t>(SmListenerEventKind::FocusLost) ||
                    actRaw > static_cast<std::uint8_t>(SmListenerActionKind::AddDelta)) {
                    return failStcSection("corrupted AXB4 MOT6 smListener entry");
                }
                ls.eventKind = static_cast<SmListenerEventKind>(evRaw);
                ls.actionKind = static_cast<SmListenerActionKind>(actRaw);
            }

            // 11. Contract input descriptors
            std::uint32_t ciCount = 0;
            if (!r.ReadU32(ciCount) || ciCount > (r.size - r.offset) / 9u) {
                return failStcSection("corrupted AXB4 MOT6 contractInputs header");
            }
            pkg.contractInputs.resize(ciCount);
            for (std::uint32_t i = 0; i < ciCount; ++i) {
                auto& ci = pkg.contractInputs[i];
                if (!r.ReadU32(ci.nameStrIdx) ||
                    !r.ReadU8(ci.valueKind) ||
                    !r.ReadU8(ci.isTrigger) ||
                    !r.ReadU16(ci.bridgePropId) ||
                    !r.ReadValue(ci.defaultValue)) {
                    return failStcSection("corrupted AXB4 MOT6 contractInput entry");
                }
            }

            // 12. State machine descriptors
            std::uint32_t smCount = 0;
            if (!r.ReadU32(smCount) || smCount > (r.size - r.offset) / 16u) {
                return failStcSection("corrupted AXB4 MOT6 stateMachines header");
            }
            pkg.stateMachines.resize(smCount);
            for (std::uint32_t i = 0; i < smCount; ++i) {
                auto& sm = pkg.stateMachines[i];
                if (!r.ReadU32(sm.nameStrIdx) ||
                    !r.ReadU32(sm.firstLayerIdx) ||
                    !r.ReadU16(sm.layerCount) ||
                    !r.ReadU32(sm.firstListenerIdx) ||
                    !r.ReadU16(sm.listenerCount)) {
                    return failStcSection("corrupted AXB4 MOT6 stateMachine entry");
                }
            }
        } else if (trailMagic == kAxb4ControlTemplateSectionMagic) {
            if ((seenSectionMask & kSecBit_CTP1) != 0u) {
                return failStcSection("corrupted AXB4 duplicate CTP1 section");
            }
            if ((seenSectionMask & kSecBit_AP14) != 0u) {
                return failStcSection("corrupted AXB4 out-of-order CTP1 section after AP14");
            }
            seenSectionMask |= kSecBit_CTP1;
            constexpr std::uint8_t kMaxCtpConverterRaw =
                static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost);
            constexpr std::uint8_t kMaxCtpCondSrcRaw =
                static_cast<std::uint8_t>(TriggerConditionSource::AncestorProperty);
            constexpr std::uint8_t kMaxCtpRoutedEventRaw =
                static_cast<std::uint8_t>(RoutedTriggerEvent::MediaEnded);

            std::uint32_t ctCount = 0;
            if (!r.ReadU32(ctCount) || ctCount > (r.size - r.offset) / 12u) {
                return failStcSection("corrupted AXB4 CTP1 ControlTemplates header");
            }
            pkg.controlTemplates.resize(ctCount);
            for (std::uint32_t ci = 0; ci < ctCount; ++ci) {
                auto& ct = pkg.controlTemplates[ci];
                std::vector<std::uint8_t> subBytes;
                if (!r.ReadString(ct.key) ||
                    !r.ReadString(ct.targetTypeName) ||
                    !r.ReadBytes(subBytes)) {
                    return failStcSection("corrupted AXB4 CTP1 ControlTemplate header");
                }
                if (!subBytes.empty()) {
                    auto subPkg = std::make_shared<AxbPackage>();
                    if (!DeserializeAxbPackageWithDepth(
                            subBytes.data(), subBytes.size(), *subPkg, errorMessage, depth + 1u)) {
                        return false;
                    }
                    ct.templatePackage = std::move(subPkg);
                }

                std::uint32_t trigCount = 0;
                if (!r.ReadU32(trigCount) || trigCount > (r.size - r.offset) / 12u) {
                    return failStcSection("corrupted AXB4 CTP1 triggers header");
                }
                ct.triggers.resize(trigCount);
                for (std::uint32_t ti = 0; ti < trigCount; ++ti) {
                    auto& trig = ct.triggers[ti];
                    std::uint8_t ovLocal = 0u;
                    std::uint32_t condCount = 0, enterCount = 0, exitCount = 0;
                    if (!r.ReadU8(ovLocal) ||
                        !r.ReadU32(condCount) ||
                        condCount > (r.size - r.offset) / 6u) {
                        return failStcSection("corrupted AXB4 CTP1 trigger header");
                    }
                    trig.overrideLocalValues = (ovLocal != 0u);
                    trig.conditions.resize(condCount);
                    for (std::uint32_t k = 0; k < condCount; ++k) {
                        auto& cond = trig.conditions[k];
                        std::uint8_t sk = 0u, cv = 0u, expNull = 0u;
                        if (!r.ReadU16(cond.propId) ||
                            !r.ReadValue(cond.expectedValue) ||
                            !r.ReadU8(sk) ||
                            !r.ReadU8(cv) ||
                            !r.ReadU8(expNull) ||
                            sk > kMaxCtpCondSrcRaw ||
                            cv > kMaxCtpConverterRaw ||
                            !r.ReadString(cond.sourceName) ||
                            !r.ReadString(cond.bindingPath) ||
                            !ReadHopVector(r, cond.compiledHops)) {
                            return failStcSection("corrupted AXB4 CTP1 TriggerCondition");
                        }
                        cond.sourceKind = static_cast<TriggerConditionSource>(sk);
                        cond.converter = static_cast<ValueConverterKind>(cv);
                        cond.expectNull = (expNull != 0u);
                    }
                    if (!readSetterList(trig.setters, "corrupted AXB4 CTP1 Setter list header", "corrupted AXB4 CTP1 Setter entry") ||
                        !r.ReadU32(enterCount) ||
                        enterCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 CTP1 EnterActions");
                    }
                    trig.enterStoryboards.resize(enterCount);
                    for (std::uint32_t ei = 0; ei < enterCount; ++ei) {
                        if (!readStoryboard(trig.enterStoryboards[ei])) return false;
                    }
                    if (!r.ReadU32(exitCount) || exitCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 CTP1 ExitActions");
                    }
                    trig.exitStoryboards.resize(exitCount);
                    for (std::uint32_t xi = 0; xi < exitCount; ++xi) {
                        if (!readStoryboard(trig.exitStoryboards[xi])) return false;
                    }
                }

                std::uint32_t etCount = 0;
                if (!r.ReadU32(etCount) || etCount > (r.size - r.offset) / 6u) {
                    return failStcSection("corrupted AXB4 CTP1 EventTriggers header");
                }
                ct.eventTriggers.resize(etCount);
                for (std::uint32_t ei = 0; ei < etCount; ++ei) {
                    auto& et = ct.eventTriggers[ei];
                    std::uint8_t ek = 0u;
                    std::uint32_t sbCount = 0;
                    if (!r.ReadU8(ek) ||
                        ek > kMaxCtpRoutedEventRaw ||
                        !r.ReadString(et.sourceName) ||
                        !r.ReadU32(sbCount) ||
                        sbCount > (r.size - r.offset) / 4u) {
                        return failStcSection("corrupted AXB4 CTP1 EventTrigger entry");
                    }
                    et.eventKind = static_cast<RoutedTriggerEvent>(ek);
                    et.storyboards.resize(sbCount);
                    for (std::uint32_t sbi = 0; sbi < sbCount; ++sbi) {
                        if (!readStoryboard(et.storyboards[sbi])) return false;
                    }
                }

                std::uint32_t vsgCount = 0;
                if (!r.ReadU32(vsgCount) || vsgCount > (r.size - r.offset) / 10u) {
                    return failStcSection("corrupted AXB4 CTP1 VisualStateGroups header");
                }
                ct.visualStateGroups.resize(vsgCount);
                for (std::uint32_t gi = 0; gi < vsgCount; ++gi) {
                    auto& vsg = ct.visualStateGroups[gi];
                    std::uint8_t dek = 0u, dem = 0u;
                    std::uint32_t vsCount = 0;
                    if (!r.ReadString(vsg.name) ||
                        !r.ReadF32(vsg.defaultTransitionDuration) ||
                        !r.ReadU8(dek) ||
                        !r.ReadU8(dem) ||
                        dek > kMaxEasingKindRaw ||
                        dem > kMaxEasingModeRaw ||
                        !r.ReadU32(vsCount) ||
                        vsCount > (r.size - r.offset) / 8u) {
                        return failStcSection("corrupted AXB4 CTP1 VisualStateGroup entry");
                    }
                    vsg.defaultEasingKind = static_cast<EasingFunctionKind>(dek);
                    vsg.defaultEasingMode = static_cast<EasingMode>(dem);
                    vsg.states.resize(vsCount);
                    for (std::uint32_t vi = 0; vi < vsCount; ++vi) {
                        if (!r.ReadString(vsg.states[vi].name) ||
                            !readSetterList(vsg.states[vi].setters, "corrupted AXB4 CTP1 Setter list header", "corrupted AXB4 CTP1 Setter entry") ||
                            !readStoryboard(vsg.states[vi].storyboard)) {
                            return failStcSection("corrupted AXB4 CTP1 VisualState entry");
                        }
                    }
                }
            }

            for (auto& st : pkg.styles) {
                if (!st.controlTemplateKey.empty()) {
                    if (const AxbControlTemplate* foundCt = pkg.FindControlTemplate(st.controlTemplateKey)) {
                        st.inlineControlTemplate = std::make_shared<AxbControlTemplate>(*foundCt);
                    }
                }
            }
        } else if (trailMagic == kAxb4MergedDictSectionMagic) {
            if ((seenSectionMask & kSecBit_MR12) != 0u) {
                return failStcSection("corrupted AXB4 duplicate MR12 section");
            }
            seenSectionMask |= kSecBit_MR12;
            std::uint32_t mrCount = 0;
            if (!r.ReadU32(mrCount) || mrCount > (r.size - r.offset) / 4u) {
                return failStcSection("corrupted AXB4 mergedDictionarySources header");
            }
            pkg.mergedDictionarySources.resize(mrCount);
            for (std::uint32_t i = 0; i < mrCount; ++i) {
                if (!r.ReadString(pkg.mergedDictionarySources[i])) {
                    return failStcSection("corrupted AXB4 mergedDictionarySources entry");
                }
            }
        } else if (trailMagic == kAxb4P14SectionMagic) {
            if ((seenSectionMask & kSecBit_AP14) != 0u) {
                return failStcSection("corrupted AXB4 duplicate AP14 section");
            }
            seenSectionMask |= kSecBit_AP14;

            auto readActionList = [&](std::vector<AxbInteractionAction>& acts) -> bool {
                std::uint32_t actCount = 0u;
                if (!r.ReadU32(actCount) || actCount > (r.size - r.offset) / 16u) {
                    return false;
                }
                acts.resize(actCount);
                for (std::uint32_t i = 0u; i < actCount; ++i) {
                    auto& act = acts[i];
                    std::uint8_t kindRaw = 0u, actFlags = 0u, sbOptRaw = 0u;
                    if (!r.ReadU8(kindRaw) ||
                        kindRaw > static_cast<std::uint8_t>(InteractionActionKind::StopMedia) ||
                        !r.ReadString(act.actionName) ||
                        !r.ReadU8(actFlags) ||
                        !r.ReadString(act.targetName) ||
                        !r.ReadU16(act.propId) ||
                        !r.ReadString(act.propertyName) ||
                        !r.ReadValue(act.value) ||
                        !r.ReadString(act.storyboardKey) ||
                        !readStoryboard(act.inlineStoryboard) ||
                        !r.ReadU8(sbOptRaw) ||
                        sbOptRaw > static_cast<std::uint8_t>(ControlStoryboardOption::TogglePlayPause) ||
                        !r.ReadString(act.commandPath) ||
                        !ReadHopVector(r, act.commandHops) ||
                        !r.ReadU64(act.commandMethodSymbolId) ||
                        !r.ReadValue(act.commandParameter)) {
                        return false;
                    }
                    act.kind = static_cast<InteractionActionKind>(kindRaw);
                    act.isEnabled = (actFlags & 0x01u) != 0u;
                    act.isSimCommand = (actFlags & 0x02u) != 0u;
                    act.hasCommandParameter = (actFlags & 0x04u) != 0u;
                    act.hasCommandParameterBinding = (actFlags & 0x08u) != 0u;
                    act.storyboardOption = static_cast<ControlStoryboardOption>(sbOptRaw);
                    if (act.hasCommandParameterBinding) {
                        if (!readFullChainRec(readFullChainRec, readMultiBindingRec, act.commandParameterBinding, 0)) {
                            return false;
                        }
                    }
                    if (!r.ReadString(act.soundSource) || !r.ReadF32(act.volume)) {
                        return false;
                    }
                }
                return true;
            };

            auto readFullConditionList = [&](std::vector<AxbTriggerCondition>& conds) -> bool {
                std::uint32_t condCount = 0u;
                if (!r.ReadU32(condCount) || condCount > (r.size - r.offset) / 16u) {
                    return false;
                }
                conds.resize(condCount);
                for (std::uint32_t ci = 0u; ci < condCount; ++ci) {
                    auto& cond = conds[ci];
                    std::uint8_t sk = 0u, cv = 0u, expNull = 0u, cmpRaw = 0u;
                    if (!r.ReadU16(cond.propId) ||
                        !r.ReadValue(cond.expectedValue) ||
                        !r.ReadU8(sk) ||
                        !r.ReadU8(cv) ||
                        !r.ReadU8(expNull) ||
                        sk > static_cast<std::uint8_t>(TriggerConditionSource::AncestorProperty) ||
                        cv > static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost) ||
                        !r.ReadString(cond.sourceName) ||
                        !r.ReadString(cond.bindingPath) ||
                        !ReadHopVector(r, cond.compiledHops) ||
                        !r.ReadU8(cmpRaw) ||
                        cmpRaw > static_cast<std::uint8_t>(TriggerComparisonOp::GreaterThanOrEqual) ||
                        !r.ReadString(cond.converterKey) ||
                        !r.ReadString(cond.converterParameter) ||
                        !r.ReadU64(cond.converterSymbolId)) {
                        return false;
                    }
                    cond.sourceKind = static_cast<TriggerConditionSource>(sk);
                    cond.converter = static_cast<ValueConverterKind>(cv);
                    cond.expectNull = (expNull != 0u);
                    cond.comparison = static_cast<TriggerComparisonOp>(cmpRaw);
                }
                return true;
            };

            std::uint32_t bcP14Count = 0u;
            if (!r.ReadU32(bcP14Count) || bcP14Count != pkg.bindingChains.size()) {
                return failStcSection("corrupted AXB4 AP14 bindingChains header");
            }
            for (std::uint32_t i = 0u; i < bcP14Count; ++i) {
                auto& ch = pkg.bindingChains[i];
                std::uint8_t hasNested = 0u;
                if (!r.ReadString(ch.converterKey) ||
                    !r.ReadString(ch.converterParameter) ||
                    !r.ReadString(ch.stringFormat) ||
                    !r.ReadU64(ch.converterSymbolId) ||
                    !r.ReadU64(ch.convertBackSymbolId) ||
                    !r.ReadU8(hasNested)) {
                    return failStcSection("corrupted AXB4 AP14 bindingChain extension");
                }
                if (hasNested != 0u) {
                    auto nested = std::make_shared<AxbCompiledMultiBinding>();
                    if (!readMultiBindingRec(readFullChainRec, readMultiBindingRec, *nested, 0)) {
                        return failStcSection("corrupted AXB4 AP14 nested MultiBinding");
                    }
                    ch.nestedMultiBinding = std::move(nested);
                }
            }

            std::uint32_t mbCount = 0u;
            if (!r.ReadU32(mbCount) || mbCount > (r.size - r.offset) / 16u) {
                return failStcSection("corrupted AXB4 AP14 multiBindings header");
            }
            pkg.multiBindings.resize(mbCount);
            for (std::uint32_t i = 0u; i < mbCount; ++i) {
                if (!readMultiBindingRec(readFullChainRec, readMultiBindingRec, pkg.multiBindings[i], 0)) {
                    return failStcSection("corrupted AXB4 AP14 multiBinding entry");
                }
            }

            std::uint32_t stP14Count = 0u;
            if (!r.ReadU32(stP14Count) || stP14Count != pkg.styles.size()) {
                return failStcSection("corrupted AXB4 AP14 styles header");
            }
            for (std::uint32_t si = 0u; si < stP14Count; ++si) {
                auto& st = pkg.styles[si];
                std::uint32_t trigP14Count = 0u;
                if (!r.ReadU32(trigP14Count) || trigP14Count != st.triggers.size()) {
                    return failStcSection("corrupted AXB4 AP14 triggers header");
                }
                for (std::uint32_t ti = 0u; ti < trigP14Count; ++ti) {
                    auto& trig = st.triggers[ti];
                    std::uint8_t isInterRaw = 0u;
                    std::uint32_t condP14Count = 0u;
                    if (!r.ReadU8(isInterRaw) ||
                        !r.ReadU32(condP14Count) ||
                        condP14Count != trig.conditions.size()) {
                        return failStcSection("corrupted AXB4 AP14 trigger extension");
                    }
                    trig.isInteractionTrigger = (isInterRaw != 0u);
                    for (std::uint32_t ci = 0u; ci < condP14Count; ++ci) {
                        auto& cond = trig.conditions[ci];
                        std::uint8_t cmpRaw = 0u;
                        if (!r.ReadU8(cmpRaw) ||
                            cmpRaw > static_cast<std::uint8_t>(TriggerComparisonOp::GreaterThanOrEqual) ||
                            !r.ReadString(cond.converterKey) ||
                            !r.ReadString(cond.converterParameter) ||
                            !r.ReadU64(cond.converterSymbolId)) {
                            return failStcSection("corrupted AXB4 AP14 condition extension");
                        }
                        cond.comparison = static_cast<TriggerComparisonOp>(cmpRaw);
                    }
                    if (!readActionList(trig.enterActions) || !readActionList(trig.exitActions)) {
                        return failStcSection("corrupted AXB4 AP14 trigger action list");
                    }
                }
                std::uint32_t etP14Count = 0u;
                if (!r.ReadU32(etP14Count) || etP14Count != st.eventTriggers.size()) {
                    return failStcSection("corrupted AXB4 AP14 eventTriggers header");
                }
                for (std::uint32_t ei = 0u; ei < etP14Count; ++ei) {
                    if (!readActionList(st.eventTriggers[ei].actions) ||
                        !readFullConditionList(st.eventTriggers[ei].conditions)) {
                        return failStcSection("corrupted AXB4 AP14 eventTrigger action list");
                    }
                }
            }

            std::uint32_t ctP14Count = 0u;
            if (!r.ReadU32(ctP14Count) || ctP14Count != pkg.controlTemplates.size()) {
                return failStcSection("corrupted AXB4 AP14 controlTemplates header");
            }
            for (std::uint32_t ci = 0u; ci < ctP14Count; ++ci) {
                auto& ct = pkg.controlTemplates[ci];
                std::uint32_t trigP14Count = 0u;
                if (!r.ReadU32(trigP14Count) || trigP14Count != ct.triggers.size()) {
                    return failStcSection("corrupted AXB4 AP14 controlTemplate triggers header");
                }
                for (std::uint32_t ti = 0u; ti < trigP14Count; ++ti) {
                    auto& trig = ct.triggers[ti];
                    std::uint8_t isInterRaw = 0u;
                    std::uint32_t condP14Count = 0u;
                    if (!r.ReadU8(isInterRaw) ||
                        !r.ReadU32(condP14Count) ||
                        condP14Count != trig.conditions.size()) {
                        return failStcSection("corrupted AXB4 AP14 controlTemplate trigger extension");
                    }
                    trig.isInteractionTrigger = (isInterRaw != 0u);
                    for (std::uint32_t k = 0u; k < condP14Count; ++k) {
                        auto& cond = trig.conditions[k];
                        std::uint8_t cmpRaw = 0u;
                        if (!r.ReadU8(cmpRaw) ||
                            cmpRaw > static_cast<std::uint8_t>(TriggerComparisonOp::GreaterThanOrEqual) ||
                            !r.ReadString(cond.converterKey) ||
                            !r.ReadString(cond.converterParameter) ||
                            !r.ReadU64(cond.converterSymbolId)) {
                            return failStcSection("corrupted AXB4 AP14 controlTemplate condition extension");
                        }
                        cond.comparison = static_cast<TriggerComparisonOp>(cmpRaw);
                    }
                    if (!readActionList(trig.enterActions) || !readActionList(trig.exitActions)) {
                        return failStcSection("corrupted AXB4 AP14 controlTemplate trigger action list");
                    }
                }
                std::uint32_t etP14Count = 0u;
                if (!r.ReadU32(etP14Count) || etP14Count != ct.eventTriggers.size()) {
                    return failStcSection("corrupted AXB4 AP14 controlTemplate eventTriggers header");
                }
                for (std::uint32_t ei = 0u; ei < etP14Count; ++ei) {
                    if (!readActionList(ct.eventTriggers[ei].actions) ||
                        !readFullConditionList(ct.eventTriggers[ei].conditions)) {
                        return failStcSection("corrupted AXB4 AP14 controlTemplate eventTrigger action list");
                    }
                }
            }
            for (auto& st : pkg.styles) {
                if (!st.controlTemplateKey.empty()) {
                    if (const AxbControlTemplate* foundCt = pkg.FindControlTemplate(st.controlTemplateKey)) {
                        st.inlineControlTemplate = std::make_shared<AxbControlTemplate>(*foundCt);
                    }
                }
            }
        } else if (trailMagic == kAxb4P15SectionMagic) {
            if ((seenSectionMask & kSecBit_AP15) != 0u) {
                return failStcSection("corrupted AXB4 duplicate AP15 section");
            }
            seenSectionMask |= kSecBit_AP15;
            if (!r.ReadString(pkg.startupUri)) {
                return failStcSection("corrupted AXB4 AP15 startupUri");
            }
            std::uint32_t dtP15Count = 0u;
            if (!r.ReadU32(dtP15Count) || dtP15Count != pkg.dataTemplates.size()) {
                return failStcSection("corrupted AXB4 AP15 dataTemplates count");
            }
            for (std::uint32_t i = 0u; i < dtP15Count; ++i) {
                if (!r.ReadString(pkg.dataTemplates[i].itemsSourcePath) ||
                    !r.ReadString(pkg.dataTemplates[i].childItemTemplateKey)) {
                    return failStcSection("corrupted AXB4 AP15 dataTemplate fields");
                }
            }
            auto readActTargetObj = [&](std::vector<AxbInteractionAction>& acts) -> bool {
                std::uint32_t ac = 0u;
                if (!r.ReadU32(ac) || ac != acts.size()) return false;
                for (std::uint32_t i = 0u; i < ac; ++i) {
                    std::uint8_t hasObj = 0u;
                    if (!r.ReadU8(hasObj)) return false;
                    if (hasObj != 0u) {
                        auto bc = std::make_shared<AxbCompiledBindingChain>();
                        std::uint8_t relS = 0u;
                        if (!r.ReadString(bc->rawPath) ||
                            !r.ReadString(bc->elementName) ||
                            !r.ReadString(bc->relativeAncestorType) ||
                            !r.ReadU8(relS)) {
                            return false;
                        }
                        bc->relativeSelf = (relS != 0u);
                        if (!bc->rawPath.empty() && bc->rawPath != ".") {
                            AxbCompiledHop hop{};
                            hop.memberName = bc->rawPath;
                            bc->hops.push_back(std::move(hop));
                        }
                        acts[i].targetObjectBinding = std::move(bc);
                    }
                }
                return true;
            };
            std::uint32_t stC = 0u;
            if (!r.ReadU32(stC) || stC != pkg.styles.size()) return false;
            for (std::uint32_t si = 0u; si < stC; ++si) {
                std::uint32_t trC = 0u;
                if (!r.ReadU32(trC) || trC != pkg.styles[si].triggers.size()) return false;
                for (std::uint32_t ti = 0u; ti < trC; ++ti) {
                    if (!readActTargetObj(pkg.styles[si].triggers[ti].enterActions) ||
                        !readActTargetObj(pkg.styles[si].triggers[ti].exitActions)) return false;
                }
                std::uint32_t etC = 0u;
                if (!r.ReadU32(etC) || etC != pkg.styles[si].eventTriggers.size()) return false;
                for (std::uint32_t ei = 0u; ei < etC; ++ei) {
                    if (!readActTargetObj(pkg.styles[si].eventTriggers[ei].actions)) return false;
                }
            }
            std::uint32_t ctC = 0u;
            if (!r.ReadU32(ctC) || ctC != pkg.controlTemplates.size()) return false;
            for (std::uint32_t ci = 0u; ci < ctC; ++ci) {
                std::uint32_t trC = 0u;
                if (!r.ReadU32(trC) || trC != pkg.controlTemplates[ci].triggers.size()) return false;
                for (std::uint32_t ti = 0u; ti < trC; ++ti) {
                    if (!readActTargetObj(pkg.controlTemplates[ci].triggers[ti].enterActions) ||
                        !readActTargetObj(pkg.controlTemplates[ci].triggers[ti].exitActions)) return false;
                }
                std::uint32_t etC = 0u;
                if (!r.ReadU32(etC) || etC != pkg.controlTemplates[ci].eventTriggers.size()) return false;
                for (std::uint32_t ei = 0u; ei < etC; ++ei) {
                    if (!readActTargetObj(pkg.controlTemplates[ci].eventTriggers[ei].actions)) return false;
                }
            }
        } else if (trailMagic == kAxb4SetterActionBindSectionMagic) {
            if ((seenSectionMask & kSecBit_SBA4) != 0u) {
                return failStcSection("corrupted AXB4 duplicate SBA4 section");
            }
            if ((seenSectionMask & kSecBit_TNE4) != 0u) {
                return failStcSection("corrupted AXB4 out-of-order SBA4 section after TNE4");
            }
            seenSectionMask |= kSecBit_SBA4;
            std::vector<AxbPropertySetter*> allSetters;
            VisitAllPackageSetters(pkg, [&](AxbPropertySetter& setter) {
                allSetters.push_back(&setter);
            });
            std::uint32_t sbCount = 0u;
            if (!r.ReadU32(sbCount) || sbCount > allSetters.size() || sbCount > (r.size - r.offset) / 8u) {
                return failStcSection("corrupted AXB4 SBA4 setter header");
            }
            for (std::uint32_t i = 0u; i < sbCount; ++i) {
                std::uint32_t ordinal = 0u;
                if (!r.ReadU32(ordinal) || ordinal >= allSetters.size()) {
                    return failStcSection("corrupted AXB4 SBA4 setter ordinal");
                }
                auto bc = std::make_shared<AxbCompiledBindingChain>();
                if (!readFullChainRec(readFullChainRec, readMultiBindingRec, *bc, 0)) {
                    return failStcSection("corrupted AXB4 SBA4 setter binding");
                }
                allSetters[ordinal]->hasBinding = true;
                allSetters[ordinal]->binding = std::move(bc);
            }
            std::vector<AxbInteractionAction*> allActions;
            VisitAllPackageActions(pkg, [&](AxbInteractionAction& act) {
                allActions.push_back(&act);
            });
            std::uint32_t abCount = 0u;
            if (!r.ReadU32(abCount) || abCount > allActions.size() || abCount > (r.size - r.offset) / 5u) {
                return failStcSection("corrupted AXB4 SBA4 action header");
            }
            for (std::uint32_t i = 0u; i < abCount; ++i) {
                std::uint32_t ordinal = 0u;
                std::uint8_t flags = 0u;
                if (!r.ReadU32(ordinal) || ordinal >= allActions.size() ||
                    !r.ReadU8(flags) || flags == 0u || (flags & ~0x03u) != 0u) {
                    return failStcSection("corrupted AXB4 SBA4 action entry");
                }
                if ((flags & 0x01u) != 0u) {
                    auto bc = std::make_shared<AxbCompiledBindingChain>();
                    if (!readFullChainRec(readFullChainRec, readMultiBindingRec, *bc, 0)) {
                        return failStcSection("corrupted AXB4 SBA4 action valueBinding");
                    }
                    allActions[ordinal]->hasValueBinding = true;
                    allActions[ordinal]->valueBinding = std::move(bc);
                }
                if ((flags & 0x02u) != 0u) {
                    auto mb = std::make_shared<AxbCompiledMultiBinding>();
                    if (!readMultiBindingRec(readFullChainRec, readMultiBindingRec, *mb, 0)) {
                        return failStcSection("corrupted AXB4 SBA4 action valueMultiBinding");
                    }
                    allActions[ordinal]->valueMultiBinding = std::move(mb);
                }
            }
        } else if (trailMagic == kAxb4TargetNullExtSectionMagic) {
            if ((seenSectionMask & kSecBit_TNE4) != 0u) {
                return failStcSection("corrupted AXB4 duplicate TNE4 section");
            }
            seenSectionMask |= kSecBit_TNE4;
            std::vector<AxbCompiledBindingChain*> nestedChains;
            VisitAllNestedBindingChains(pkg, [&](AxbCompiledBindingChain& ch) {
                nestedChains.push_back(&ch);
            });
            std::uint32_t tneCount = 0u;
            if (!r.ReadU32(tneCount) ||
                tneCount > nestedChains.size() ||
                tneCount > (r.size - r.offset) / 5u) {
                return failStcSection("corrupted AXB4 TNE4 header");
            }
            for (std::uint32_t i = 0u; i < tneCount; ++i) {
                std::uint32_t ordinal = 0u;
                UnifiedValue val{};
                if (!r.ReadU32(ordinal) ||
                    ordinal >= nestedChains.size() ||
                    !r.ReadValue(val)) {
                    return failStcSection("corrupted AXB4 TNE4 entry");
                }
                nestedChains[ordinal]->targetNullValue = std::move(val);
            }
        } else if (trailMagic == kAxb4EventTrigExtSectionMagic) {
            if ((seenSectionMask & kSecBit_ETE4) != 0u) {
                return failStcSection("corrupted AXB4 duplicate ETE4 section");
            }
            seenSectionMask |= kSecBit_ETE4;
            std::vector<AxbEventTrigger*> allEventTriggers;
            VisitAllPackageEventTriggers(pkg, [&](AxbEventTrigger& et) {
                allEventTriggers.push_back(&et);
            });
            std::uint32_t etExtCount = 0u;
            if (!r.ReadU32(etExtCount) ||
                etExtCount > allEventTriggers.size() ||
                etExtCount > (r.size - r.offset) / 17u) {
                return failStcSection("corrupted AXB4 ETE4 trigger header");
            }
            for (std::uint32_t i = 0u; i < etExtCount; ++i) {
                std::uint32_t ordinal = 0u, intervalMs = 0u, totalTicksRaw = 0u;
                std::uint16_t keyRaw = 0u;
                std::uint8_t mods = 0u, flags = 0u, trigSrcRaw = 0u;
                if (!r.ReadU32(ordinal) ||
                    ordinal >= allEventTriggers.size() ||
                    !r.ReadU16(keyRaw) ||
                    !r.ReadU8(mods) ||
                    !r.ReadU8(flags) ||
                    !r.ReadU32(intervalMs) ||
                    !r.ReadU32(totalTicksRaw) ||
                    !r.ReadU8(trigSrcRaw) ||
                    trigSrcRaw > static_cast<std::uint8_t>(RoutedTriggerEvent::MediaEnded)) {
                    return failStcSection("corrupted AXB4 ETE4 trigger entry");
                }
                AxbEventTrigger* et = allEventTriggers[ordinal];
                et->keyCode = static_cast<KeyCode>(keyRaw);
                et->modifiers = mods;
                et->activeOnFocus = (flags & 0x01u) != 0u;
                et->firedOnKeyUp = (flags & 0x02u) != 0u;
                et->timerIntervalMs = intervalMs;
                et->totalTicks = static_cast<std::int32_t>(totalTicksRaw);
                et->triggerSourceEvent = static_cast<RoutedTriggerEvent>(trigSrcRaw);
            }

            std::vector<AxbInteractionAction*> allActions;
            VisitAllPackageActions(pkg, [&](AxbInteractionAction& act) {
                allActions.push_back(&act);
            });
            std::uint32_t actExtCount = 0u;
            if (!r.ReadU32(actExtCount) ||
                actExtCount > allActions.size() ||
                actExtCount > (r.size - r.offset) / 5u) {
                return failStcSection("corrupted AXB4 ETE4 action header");
            }
            for (std::uint32_t i = 0u; i < actExtCount; ++i) {
                std::uint32_t ordinal = 0u;
                std::uint8_t flags = 0u;
                if (!r.ReadU32(ordinal) ||
                    ordinal >= allActions.size() ||
                    !r.ReadU8(flags)) {
                    return failStcSection("corrupted AXB4 ETE4 action entry");
                }
                AxbInteractionAction* act = allActions[ordinal];
                act->isNullValue = (flags & 0x01u) != 0u;
                act->commandTargetsWrapper = (flags & 0x02u) != 0u;
            }
        } else {
            return failStcSection("corrupted AXB4 unknown trailing section magic");
        }
    }

    for (auto& st : pkg.styles) {
        if (!st.controlTemplateKey.empty()) {
            if (const AxbControlTemplate* foundCt = pkg.FindControlTemplate(st.controlTemplateKey)) {
                st.inlineControlTemplate = std::make_shared<AxbControlTemplate>(*foundCt);
            }
        }
    }

    pkg.PinValuesInPools();
    outPackage = std::move(pkg);
    return true;
}

} // namespace

bool AxbPackage::Deserialize(
    const std::uint8_t* data,
    std::size_t size,
    AxbPackage& outPackage,
    std::string* errorMessage) {
    return DeserializeAxbPackageWithDepth(data, size, outPackage, errorMessage, 0u);
}

bool AxbPackage::Deserialize(
    const std::vector<std::uint8_t>& bytes,
    AxbPackage& outPackage,
    std::string* errorMessage) {
    return Deserialize(bytes.data(), bytes.size(), outPackage, errorMessage);
}

bool IsValidRealScriptFieldIdentifier(std::string_view name) noexcept {
    if (name.empty() || name == "__nativeRootHandle") {
        return false;
    }
    const unsigned char c0 = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(c0) || c0 == '_')) {
        return false;
    }
    for (std::size_t i = 1u; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!(std::isalnum(c) || c == '_')) {
            return false;
        }
    }
    static constexpr std::string_view kKeywords[] = {
        "class", "partial", "module", "import", "public", "private", "protected", "internal",
        "static", "const", "readonly", "override", "virtual", "abstract", "sealed",
        "return", "int", "float", "double", "bool", "string", "void", "handle", "object",
        "var", "let", "null", "true", "false", "this", "base", "super", "new",
        "if", "else", "for", "foreach", "in", "while", "do", "switch", "case", "default",
        "break", "continue", "enum", "struct", "interface", "delegate", "event",
        "get", "set", "value", "async", "await", "try", "catch", "finally", "throw",
        "sequence", "yield"
    };
    for (const auto kw : kKeywords) {
        if (name == kw) {
            return false;
        }
    }
    return true;
}

bool SplitIndexedMemberName(
    std::string_view raw,
    std::string& outBaseMember,
    std::int32_t& outIndex) {
    raw = str::Trim(raw);
    const std::size_t lb = raw.find('[');
    if (lb == std::string_view::npos || lb == 0u || raw.back() != ']') {
        return false;
    }
    const std::string_view base = str::Trim(raw.substr(0u, lb));
    const std::string_view idxSlice = str::Trim(raw.substr(lb + 1u, raw.size() - lb - 2u));
    if (base.empty() || idxSlice.empty()) {
        return false;
    }
    std::int32_t parsed = -1;
    const auto [ptr, ec] = std::from_chars(idxSlice.data(), idxSlice.data() + idxSlice.size(), parsed);
    if (ec != std::errc{} || ptr != idxSlice.data() + idxSlice.size() || parsed < 0) {
        return false;
    }
    outBaseMember.assign(base.data(), base.size());
    outIndex = parsed;
    return true;
}

bool TryResolveEnumMemberByName(
    const ProgramImage* program,
    SymbolId enumTypeId,
    std::string_view rawName,
    std::int64_t& outValue) noexcept {
    if (!program || enumTypeId == 0) {
        return false;
    }
    const rs_sem::TypeSymbol* tSym = program->resolveType(enumTypeId);
    if (!tSym || tSym->kind != rs_sem::TypeKind::Enum) {
        return false;
    }
    std::string_view memberName = str::Trim(rawName);
    if (memberName.empty()) {
        return false;
    }
    if (const auto sep = memberName.rfind("::"); sep != std::string_view::npos) {
        memberName = memberName.substr(sep + 2u);
    } else if (const auto dot = memberName.rfind('.'); dot != std::string_view::npos) {
        memberName = memberName.substr(dot + 1u);
    }
    for (const auto& em : tSym->enumMembers) {
        if (em.name == memberName) {
            outValue = em.value;
            return true;
        }
    }
    std::int64_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(memberName.data(), memberName.data() + memberName.size(), parsed);
    if (ec == std::errc{} && ptr == memberName.data() + memberName.size()) {
        outValue = parsed;
        return true;
    }
    return false;
}

bool TryFormatEnumValueName(
    const ProgramImage* program,
    SymbolId enumTypeId,
    std::int64_t intVal,
    std::string& outName) {
    if (!program || enumTypeId == 0) {
        return false;
    }
    const rs_sem::TypeSymbol* tSym = program->resolveType(enumTypeId);
    if (!tSym || tSym->kind != rs_sem::TypeKind::Enum) {
        return false;
    }
    for (const auto& em : tSym->enumMembers) {
        if (em.value == intVal) {
            outName = em.name;
            return true;
        }
    }
    return false;
}

namespace {
std::atomic<RuntimeXamlCompileHook> gRuntimeXamlCompileHook{nullptr};
} // namespace

void RegisterRuntimeXamlCompileHook(RuntimeXamlCompileHook hook) noexcept {
    gRuntimeXamlCompileHook.store(hook, std::memory_order_release);
}

RuntimeXamlCompileHook GetRuntimeXamlCompileHook() noexcept {
    return gRuntimeXamlCompileHook.load(std::memory_order_acquire);
}

} // namespace aero
