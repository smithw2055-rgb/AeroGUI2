#include "aero/compiler/AeroCompiler.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>

#include "aero/controls/Controls.hpp"
#include "aero/core/TypeClass.hpp"
#include "realscript/aot_cpp/AotCpp.h"
#include "realscript/bytecode/Bytecode.h"

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
    } else if (const auto* f4 = std::get_if<Float4Value>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Float4));
        WriteF32(out, f4->x);
        WriteF32(out, f4->y);
        WriteF32(out, f4->z);
        WriteF32(out, f4->w);
    } else if (const auto* s = std::get_if<StringValue>(&val)) {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::String));
        WriteString(out, s->view());
    } else {
        WriteU8(out, static_cast<std::uint8_t>(AxbLiteralTag::Empty));
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
        if (!ReadU32(len) || offset + len > size) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), len);
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
                return true;
            }
            case AxbLiteralTag::String: {
                std::string s;
                if (!ReadString(s)) return false;
                out = value::FromString(std::move(s));
                return true;
            }
        }
        return false;
    }
};

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

std::vector<float> ParseFloatList(std::string_view text) {
    std::vector<float> nums;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (text[pos] == ',' || std::isspace(static_cast<unsigned char>(text[pos])))) {
            ++pos;
        }
        if (pos >= text.size()) break;
        std::size_t end = pos;
        while (end < text.size() && text[end] != ',' &&
               !std::isspace(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        nums.push_back(ParseFloat32(text.substr(pos, end - pos)));
        pos = end;
    }
    return nums;
}

std::uint8_t HexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + (c - 'A'));
    return 0;
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

Color ParseHexColor(std::string_view hex) noexcept {
    if (hex.size() == 6) {
        const float r = static_cast<float>((HexNibble(hex[0]) << 4) | HexNibble(hex[1])) / 255.0f;
        const float g = static_cast<float>((HexNibble(hex[2]) << 4) | HexNibble(hex[3])) / 255.0f;
        const float b = static_cast<float>((HexNibble(hex[4]) << 4) | HexNibble(hex[5])) / 255.0f;
        return Color{r, g, b, 1.0f};
    }
    if (hex.size() == 8) {
        const float a = static_cast<float>((HexNibble(hex[0]) << 4) | HexNibble(hex[1])) / 255.0f;
        const float r = static_cast<float>((HexNibble(hex[2]) << 4) | HexNibble(hex[3])) / 255.0f;
        const float g = static_cast<float>((HexNibble(hex[4]) << 4) | HexNibble(hex[5])) / 255.0f;
        const float b = static_cast<float>((HexNibble(hex[6]) << 4) | HexNibble(hex[7])) / 255.0f;
        return Color{r, g, b, a};
    }
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

Color ParseColorLiteral(std::string_view raw) noexcept {
    raw = Trim(raw);
    if (!raw.empty() && raw.front() == '#') {
        raw.remove_prefix(1);
        if (raw.size() == 3) {
            const float r = static_cast<float>(HexNibble(raw[0]) * 17) / 255.0f;
            const float g = static_cast<float>(HexNibble(raw[1]) * 17) / 255.0f;
            const float b = static_cast<float>(HexNibble(raw[2]) * 17) / 255.0f;
            return Color{r, g, b, 1.0f};
        }
        return ParseHexColor(raw);
    }
    struct NamedColorEntry {
        std::string_view name;
        std::string_view hex8;
    };
    static constexpr NamedColorEntry kNamedColors[] = {
        {"Transparent", "00000000"},
        {"White", "FFFFFFFF"},
        {"Black", "FF000000"},
        {"Red", "FFFF0000"},
        {"Green", "FF008000"},
        {"Blue", "FF0000FF"},
        {"Yellow", "FFFFFF00"},
        {"Cyan", "FF00FFFF"},
        {"Aqua", "FF00FFFF"},
        {"Magenta", "FFFF00FF"},
        {"Fuchsia", "FFFF00FF"},
        {"Gray", "FF808080"},
        {"Grey", "FF808080"},
        {"LightGray", "FFD3D3D3"},
        {"DarkGray", "FFA9A9A9"},
        {"Silver", "FFC0C0C0"},
        {"WhiteSmoke", "FFF5F5F5"},
        {"LightSeaGreen", "FF20B2AA"},
        {"MediumSeaGreen", "FF3CB371"},
        {"SeaGreen", "FF2E8B57"},
        {"Turquoise", "FF40E0D0"},
        {"MediumTurquoise", "FF48D1CC"},
        {"DarkTurquoise", "FF00CED1"},
        {"DodgerBlue", "FF1E90FF"},
        {"DeepSkyBlue", "FF00BFFF"},
        {"SkyBlue", "FF87CEEB"},
        {"LightSkyBlue", "FF87CEFA"},
        {"CornflowerBlue", "FF6495ED"},
        {"RoyalBlue", "FF4169E1"},
        {"SteelBlue", "FF4682B4"},
        {"Teal", "FF008080"},
        {"OrangeRed", "FFFF4500"},
        {"Orange", "FFFFA500"},
        {"Gold", "FFFFD700"},
        {"Crimson", "FFDC143C"},
        {"Tomato", "FFFF6347"},
        {"Coral", "FFFF7F50"},
        {"Salmon", "FFFA8072"},
        {"Lime", "FF00FF00"},
        {"LimeGreen", "FF32CD32"},
        {"LawnGreen", "FF7CFC00"},
        {"GreenYellow", "FFADFF2F"},
        {"Purple", "FF800080"},
        {"Violet", "FFEE82EE"},
        {"Plum", "FFDDA0DD"},
        {"Indigo", "FF4B0082"},
        {"SlateGray", "FF708090"},
        {"DarkSlateGray", "FF2F4F4F"},
        {"Brown", "FFA52A2A"},
        {"SaddleBrown", "FF8B4513"},
        {"Tan", "FFD2B48C"},
        {"Wheat", "FFF5DEB3"},
        {"Beige", "FFF5F5DC"},
        {"Ivory", "FFFFFFF0"},
        {"Navy", "FF000080"},
        {"MidnightBlue", "FF191970"},
        {"Maroon", "FF800000"},
        {"Olive", "FF808000"},
        {"Pink", "FFFFC0CB"},
        {"HotPink", "FFFF69B4"},
        {"DeepPink", "FFFF1493"},
    };
    for (const auto& entry : kNamedColors) {
        if (EqualsIgnoreCase(raw, entry.name)) {
            return ParseHexColor(entry.hex8);
        }
    }
    const auto vals = ParseFloatList(raw);
    if (vals.size() >= 4) return Color{vals[0], vals[1], vals[2], vals[3]};
    if (vals.size() == 3) return Color{vals[0], vals[1], vals[2], 1.0f};
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
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

std::string DecodeXmlEntities(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) {
        return std::string(raw);
    }
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] == '&') {
            if (raw.substr(i, 5) == "&amp;") {
                out.push_back('&');
                i += 5;
                continue;
            }
            if (raw.substr(i, 4) == "&lt;") {
                out.push_back('<');
                i += 4;
                continue;
            }
            if (raw.substr(i, 4) == "&gt;") {
                out.push_back('>');
                i += 4;
                continue;
            }
            if (raw.substr(i, 6) == "&quot;") {
                out.push_back('"');
                i += 6;
                continue;
            }
            if (raw.substr(i, 6) == "&apos;") {
                out.push_back('\'');
                i += 6;
                continue;
            }
        }
        out.push_back(raw[i]);
        ++i;
    }
    return out;
}

} // namespace

// ============================================================================
// AxbPackage String Table & Binary Serialization (.axb4)
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
        WriteU32(out, static_cast<std::uint32_t>(chain.hops.size()));
        for (const auto& hop : chain.hops) {
            WriteString(out, hop.memberName);
            WriteU64(out, hop.declaringTypeId);
            WriteU32(out, hop.fieldIndex);
            WriteU8(out, hop.observableBitIndex);
            WriteU64(out, hop.getterSymbolId);
            WriteU64(out, hop.setterSymbolId);
            std::uint8_t hopFlags = 0;
            if (hop.isAutoProperty) hopFlags |= 0x01u;
            if (hop.isObservable) hopFlags |= 0x02u;
            if (hop.isReadOnly) hopFlags |= 0x04u;
            if (hop.isSimState) hopFlags |= 0x08u;
            if (hop.isSimCommand) hopFlags |= 0x10u;
            WriteU8(out, hopFlags);
        }
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
    return out;
}

bool AxbPackage::Deserialize(
    const std::uint8_t* data,
    std::size_t size,
    AxbPackage& outPackage,
    std::string* errorMessage) {
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
        std::uint32_t hopCount = 0;
        if (!r.ReadString(chain.rawPath) ||
            !r.ReadU16(chain.targetPropertyId) ||
            !r.ReadU8(modeRaw) ||
            !r.ReadU32(hopCount) ||
            hopCount > (r.size - r.offset) / 34u) {
            return false;
        }
        chain.mode = static_cast<BindingMode>(modeRaw);
        chain.hops.resize(hopCount);
        for (std::uint32_t h = 0; h < hopCount; ++h) {
            auto& hop = chain.hops[h];
            std::uint8_t hopFlags = 0;
            if (!r.ReadString(hop.memberName) ||
                !r.ReadU64(hop.declaringTypeId) ||
                !r.ReadU32(hop.fieldIndex) ||
                !r.ReadU8(hop.observableBitIndex) ||
                !r.ReadU64(hop.getterSymbolId) ||
                !r.ReadU64(hop.setterSymbolId) ||
                !r.ReadU8(hopFlags)) {
                return false;
            }
            hop.isAutoProperty = (hopFlags & 0x01u) != 0;
            hop.isObservable = (hopFlags & 0x02u) != 0;
            hop.isReadOnly = (hopFlags & 0x04u) != 0;
            hop.isSimState = (hopFlags & 0x08u) != 0;
            hop.isSimCommand = (hopFlags & 0x10u) != 0;
        }
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
    }

    outPackage = std::move(pkg);
    return true;
}

bool AxbPackage::Deserialize(
    const std::vector<std::uint8_t>& bytes,
    AxbPackage& outPackage,
    std::string* errorMessage) {
    return Deserialize(bytes.data(), bytes.size(), outPackage, errorMessage);
}

// ============================================================================
// AeroCompiler Single-Pass XAML + RealScript Unified Compiler
// ============================================================================

AeroCompiler::AeroCompiler() {
    EnsureControlTypesRegistered();
}

std::uint16_t AeroCompiler::ResolvePropertyId(std::string_view propertyName) noexcept {
    if (propertyName == "Width" || propertyName == "d:DesignWidth") return PropId_Width;
    if (propertyName == "Height" || propertyName == "d:DesignHeight") return PropId_Height;
    if (propertyName == "MinWidth") return PropId_MinWidth;
    if (propertyName == "MinHeight") return PropId_MinHeight;
    if (propertyName == "Margin") return PropId_Margin;
    if (propertyName == "Padding") return PropId_Padding;
    if (propertyName == "Opacity") return PropId_Opacity;
    if (propertyName == "Visibility") return PropId_Visibility;
    if (propertyName == "Background" || propertyName == "Fill") return PropId_Background;
    if (propertyName == "Foreground") return PropId_Foreground;
    if (propertyName == "BorderBrush" || propertyName == "Stroke") return PropId_BorderBrush;
    if (propertyName == "BorderThickness" || propertyName == "StrokeThickness") return PropId_BorderThickness;
    if (propertyName == "CornerRadius" || propertyName == "RadiusX" || propertyName == "RadiusY") return PropId_CornerRadius;
    if (propertyName == "Title" || propertyName == "Header") return PropId_Title;
    if (propertyName == "Tag") return PropId_Tag;
    if (propertyName == "DataContext") return PropId_DataContext;
    if (propertyName == "Text" || propertyName == "Content") return PropId_Text;
    if (propertyName == "Placeholder" || propertyName == "noesis:Text.Placeholder") return PropId_Placeholder;
    if (propertyName == "FontSize") return PropId_FontSize;
    if (propertyName == "Orientation") return PropId_Orientation;
    if (propertyName == "Grid.Row" || propertyName == "Row") return PropId_GridRow;
    if (propertyName == "Grid.Column" || propertyName == "Column") return PropId_GridColumn;
    if (propertyName == "Grid.RowSpan" || propertyName == "RowSpan") return PropId_GridRowSpan;
    if (propertyName == "Grid.ColumnSpan" || propertyName == "ColumnSpan") return PropId_GridColumnSpan;
    if (propertyName == "Canvas.Left" || propertyName == "Left") return PropId_CanvasLeft;
    if (propertyName == "Canvas.Top" || propertyName == "Top") return PropId_CanvasTop;
    if (propertyName == "ItemsSource") return PropId_ItemsSource;
    if (propertyName == "CommandParameter") return PropId_CommandParameter;
    if (propertyName == "Projection3D.Yaw" || propertyName == "Projection3DYaw") return PropId_Projection3DYaw;
    if (propertyName == "Projection3D.Pitch" || propertyName == "Projection3DPitch") return PropId_Projection3DPitch;
    if (propertyName == "Spacing") return PropId_Spacing;
    if (propertyName == "Value") return PropId_Value;
    if (propertyName == "Minimum" || propertyName == "MinValue") return PropId_Minimum;
    if (propertyName == "Maximum" || propertyName == "MaxValue") return PropId_Maximum;
    if (propertyName == "IsChecked" || propertyName == "IsSelected" || propertyName == "IsExpanded") return PropId_IsChecked;
    if (propertyName == "SelectedItem") return PropId_SelectedItem;
    if (propertyName == "SelectedIndex") return PropId_SelectedIndex;
    return PropId_Tag;
}

UnifiedValue AeroCompiler::ParseLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue) {
    rawValue = Trim(rawValue);
    switch (propId) {
        case PropId_Width:
        case PropId_Height:
        case PropId_MinWidth:
        case PropId_MinHeight:
        case PropId_Opacity:
        case PropId_FontSize:
        case PropId_CanvasLeft:
        case PropId_CanvasTop:
        case PropId_Projection3DYaw:
        case PropId_Projection3DPitch:
        case PropId_Spacing:
        case PropId_Value:
        case PropId_Minimum:
        case PropId_Maximum:
            return MakeFloat(ParseFloat32(rawValue));

        case PropId_IsChecked:
            return EqualsIgnoreCase(rawValue, "true") || rawValue == "1";

        case PropId_Visibility:
            if (rawValue == "Collapsed" || rawValue == "2") return value::FromInt(2);
            if (rawValue == "Hidden" || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_Orientation:
            if (rawValue == "Horizontal" || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_GridRow:
        case PropId_GridColumn:
        case PropId_GridRowSpan:
        case PropId_GridColumnSpan:
        case PropId_SelectedIndex: {
            std::int64_t iv = 0;
            (void)std::from_chars(rawValue.data(), rawValue.data() + rawValue.size(), iv);
            return value::FromInt(iv);
        }

        case PropId_Margin:
        case PropId_Padding:
        case PropId_BorderThickness: {
            const auto nums = ParseFloatList(rawValue);
            if (nums.size() == 1) return value::FromThickness(Thickness(nums[0]));
            if (nums.size() == 2) return value::FromThickness(Thickness(nums[0], nums[1]));
            if (nums.size() >= 4) {
                return value::FromThickness(Thickness(nums[0], nums[1], nums[2], nums[3]));
            }
            return value::FromThickness(Thickness{});
        }

        case PropId_CornerRadius: {
            const auto nums = ParseFloatList(rawValue);
            if (nums.size() == 1) return value::FromCornerRadius(CornerRadius(nums[0]));
            if (nums.size() >= 4) {
                return value::FromCornerRadius(CornerRadius(nums[0], nums[1], nums[2], nums[3]));
            }
            return value::FromCornerRadius(CornerRadius{});
        }

        case PropId_Background:
        case PropId_Foreground:
        case PropId_BorderBrush:
            return value::FromColor(ParseColorLiteral(rawValue));

        default:
            return value::FromString(DecodeXmlEntities(rawValue));
    }
}

bool AeroCompiler::BindPackageAgainstProgram(
    AxbPackage& package,
    const ProgramImage& program,
    std::string_view rootDataTypeName,
    const ExecutionDomainContext* domains) {
    ExecutionDomainContext localDomains;
    if (!domains) {
        localDomains.registerProgramObservableMetadata(program);
        domains = &localDomains;
    }

    const std::string effectiveClassName = !rootDataTypeName.empty()
        ? std::string(rootDataTypeName)
        : package.className;

    SymbolId classTypeId = 0;
    if (!effectiveClassName.empty()) {
        std::string canonical = effectiveClassName;
        if (canonical.find("::") == std::string::npos) {
            if (const auto lastDot = canonical.rfind('.'); lastDot != std::string::npos) {
                canonical = canonical.substr(0, lastDot) + "::" + canonical.substr(lastDot + 1);
            }
        }
        classTypeId = rs_sem::stableTypeId(canonical);
        if (!program.resolveType(classTypeId)) {
            for (const auto& mod : program.modules()) {
                for (const auto& t : mod.types) {
                    if (t.name == effectiveClassName ||
                        (mod.name + "." + t.name) == effectiveClassName ||
                        (mod.name + "::" + t.name) == effectiveClassName) {
                        classTypeId = t.id;
                        break;
                    }
                }
            }
        }
    }

    const rs_sem::TypeSymbol* rootTypeSym =
        (classTypeId != 0) ? program.resolveType(classTypeId) : nullptr;

    // 1. Bind __nativeRootHandle and x:Name control field indices on the partial class
    if (rootTypeSym) {
        for (std::size_t fIdx = 0; fIdx < rootTypeSym->fields.size(); ++fIdx) {
            if (rootTypeSym->fields[fIdx].name == "__nativeRootHandle") {
                package.rootHandleFieldIndex = static_cast<std::uint32_t>(fIdx);
                break;
            }
        }
        for (auto& nc : package.namedControls) {
            for (std::size_t fIdx = 0; fIdx < rootTypeSym->fields.size(); ++fIdx) {
                if (rootTypeSym->fields[fIdx].name == nc.name) {
                    nc.fieldIndexInClass = static_cast<std::uint32_t>(fIdx);
                    break;
                }
            }
        }
    }

    // 2. Bind event handler method SymbolIds on the partial class (including Command -> Start/OnStart fallback)
    if (rootTypeSym) {
        for (auto& ev : package.eventBindings) {
            for (const auto& m : rootTypeSym->methods) {
                if (m.name == ev.handlerMethodName) {
                    ev.handlerSymbolId = m.id;
                    break;
                }
            }
            if (ev.handlerSymbolId == 0 && ev.handlerMethodName.size() > 7 &&
                ev.handlerMethodName.compare(ev.handlerMethodName.size() - 7, 7, "Command") == 0) {
                const std::string baseCmd = ev.handlerMethodName.substr(0, ev.handlerMethodName.size() - 7);
                const std::string onBaseCmd = "On" + baseCmd;
                for (const auto& m : rootTypeSym->methods) {
                    if (m.name == baseCmd || m.name == onBaseCmd) {
                        ev.handlerSymbolId = m.id;
                        break;
                    }
                }
            }
        }
        for (auto& inst : package.instructions) {
            if (inst.opcode == AxbOpcode::BindEvent && inst.strIdx0 < package.eventBindings.size()) {
                inst.symbolId = package.eventBindings[inst.strIdx0].handlerSymbolId;
            }
        }
    }

    // 3. Bind multi-hop PropertySlotHandles in bindingChains
    for (auto& chain : package.bindingChains) {
        SymbolId currentHopOwnerId = classTypeId;
        for (auto& hop : chain.hops) {
            if (currentHopOwnerId == 0) {
                break;
            }
            if (auto slotOpt = domains->resolvePropertySlot(
                    program, currentHopOwnerId, hop.memberName)) {
                hop.declaringTypeId = slotOpt->typeId;
                hop.fieldIndex = slotOpt->backingFieldIndex;
                hop.observableBitIndex = slotOpt->observableBitIndex;
                hop.getterSymbolId = slotOpt->getterSymbolId;
                hop.setterSymbolId = slotOpt->setterSymbolId;
                hop.isAutoProperty = slotOpt->isAutoProperty;
                hop.isObservable = slotOpt->isObservable;
                hop.isReadOnly = slotOpt->isReadOnly;
                hop.isSimState = slotOpt->isSimState;
                hop.isSimCommand = slotOpt->isSimCommand;
            }

            // Advance currentHopOwnerId to the hop's member type if it is an Object
            const auto* ownerSym = program.resolveType(currentHopOwnerId);
            SymbolId nextTypeId = 0;
            if (ownerSym) {
                for (const auto& p : ownerSym->properties) {
                    if (p.name == hop.memberName && !p.typeName.empty()) {
                        nextTypeId = rs_sem::stableTypeId(p.typeName);
                        break;
                    }
                }
                if (nextTypeId == 0) {
                    for (const auto& f : ownerSym->fields) {
                        if (f.name == hop.memberName && !f.typeName.empty()) {
                            nextTypeId = rs_sem::stableTypeId(f.typeName);
                            break;
                        }
                    }
                }
            }
            currentHopOwnerId = nextTypeId;
        }
    }

    return true;
}

AeroCompileResult AeroCompiler::Compile(const AeroCompileInput& input) const {
    AeroCompileResult result{};
    AxbPackage& pkg = result.axbPackage;
    pkg.InternString("");

    std::string rootDataType;
    const std::string_view src = input.xamlContent;
    std::size_t pos = 0;
    bool seenRoot = false;

    enum class XmlScopeKind : std::uint8_t {
        VisualElement = 0,
        TransparentContainer = 1,
        RunInline = 2,
        ResourceContainer = 3,
        KeyedResourceItem = 4,
        BrushPropertyElement = 5,
        NonVisualSkip = 6,
    };

    struct XmlScopeEntry {
        XmlScopeKind kind = XmlScopeKind::VisualElement;
        std::string tagName;
        std::string resourceKey;
        std::uint16_t targetPropId = PropId_Background;
        bool brushAssigned = false;
        std::string accumulatedText;
    };

    std::vector<XmlScopeEntry> scopeStack;
    std::size_t nonVisualDepth = 0;

    auto findLocalOrGlobalResource = [&](std::string_view key) -> const UnifiedValue* {
        for (auto it = pkg.resources.rbegin(); it != pkg.resources.rend(); ++it) {
            if (it->first == key) {
                return &it->second;
            }
        }
        return AxbTemplateRegistry::Instance().FindResource(key);
    };

    auto isNonVisualElementTag = [](std::string_view shortTag) noexcept -> bool {
        return shortTag == "Style" ||
               shortTag == "ControlTemplate" ||
               shortTag == "DataTemplate" ||
               shortTag == "ItemsPanelTemplate" ||
               shortTag == "Storyboard" ||
               shortTag == "BeginStoryboard" ||
               shortTag == "VisualStateGroup" ||
               shortTag == "VisualState" ||
               shortTag == "VisualTransition" ||
               shortTag == "EventTrigger" ||
               shortTag == "Trigger" ||
               shortTag == "DataTrigger" ||
               shortTag == "MultiTrigger" ||
               shortTag == "MultiDataTrigger" ||
               shortTag == "Setter" ||
               shortTag == "TransformGroup" ||
               shortTag == "ScaleTransform" ||
               shortTag == "RotateTransform" ||
               shortTag == "TranslateTransform" ||
               shortTag == "SkewTransform" ||
               shortTag == "MatrixTransform" ||
               shortTag == "DoubleAnimation" ||
               shortTag == "DoubleAnimationUsingKeyFrames" ||
               shortTag == "ColorAnimation" ||
               shortTag == "ColorAnimationUsingKeyFrames" ||
               shortTag == "BooleanAnimationUsingKeyFrames" ||
               shortTag == "ObjectAnimationUsingKeyFrames" ||
               shortTag == "PointAnimationUsingKeyFrames" ||
               shortTag == "EasingDoubleKeyFrame" ||
               shortTag == "LinearDoubleKeyFrame" ||
               shortTag == "DiscreteDoubleKeyFrame" ||
               shortTag == "SplineDoubleKeyFrame" ||
               shortTag == "EasingColorKeyFrame" ||
               shortTag == "LinearColorKeyFrame" ||
               shortTag == "DiscreteBooleanKeyFrame" ||
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
               shortTag == "MultiBinding";
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
                    top.kind == XmlScopeKind::RunInline) {
                    if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                    top.accumulatedText += DecodeXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::TransparentContainer &&
                           top.targetPropId != 0 && nonVisualDepth == 0) {
                    if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                    top.accumulatedText += DecodeXmlEntities(rawInner);
                } else if (top.kind == XmlScopeKind::VisualElement && nonVisualDepth == 0) {
                    const std::string_view st = StripXmlPrefix(top.tagName);
                    if (st == "TextBlock" || st == "Label" || st == "Button" ||
                        st == "RepeatButton" || st == "ToggleButton" ||
                        st == "CheckBox" || st == "RadioButton" || st == "Hyperlink") {
                        if (!top.accumulatedText.empty()) top.accumulatedText.push_back(' ');
                        top.accumulatedText += DecodeXmlEntities(rawInner);
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
                XmlScopeEntry closed = std::move(scopeStack.back());
                scopeStack.pop_back();
                if (closed.kind == XmlScopeKind::ResourceContainer ||
                    closed.kind == XmlScopeKind::KeyedResourceItem ||
                    closed.kind == XmlScopeKind::BrushPropertyElement ||
                    closed.kind == XmlScopeKind::NonVisualSkip) {
                    if (nonVisualDepth > 0) {
                        --nonVisualDepth;
                    }
                }
                if (closed.kind == XmlScopeKind::KeyedResourceItem &&
                    !closed.resourceKey.empty()) {
                    const std::string_view st = StripXmlPrefix(closed.tagName);
                    if (st == "Color" && !closed.accumulatedText.empty()) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromColor(ParseColorLiteral(closed.accumulatedText)));
                    } else if (st == "String" || (!closed.brushAssigned && !closed.accumulatedText.empty())) {
                        pkg.resources.emplace_back(
                            closed.resourceKey,
                            value::FromString(closed.accumulatedText));
                    }
                } else if (closed.kind == XmlScopeKind::TransparentContainer &&
                           closed.targetPropId != 0 && !closed.accumulatedText.empty()) {
                    AxbInstruction propTextInst{};
                    propTextInst.opcode = AxbOpcode::SetStaticProperty;
                    propTextInst.propId = closed.targetPropId;
                    propTextInst.literalValue = value::FromString(std::move(closed.accumulatedText));
                    pkg.instructions.push_back(std::move(propTextInst));
                } else if (closed.kind == XmlScopeKind::RunInline && !closed.accumulatedText.empty()) {
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::VisualElement) {
                            if (!it->accumulatedText.empty()) it->accumulatedText.push_back(' ');
                            it->accumulatedText += closed.accumulatedText;
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
                    }
                    AxbInstruction endInst{};
                    endInst.opcode = AxbOpcode::EndElement;
                    pkg.instructions.push_back(std::move(endInst));
                } else if (closed.kind == XmlScopeKind::ResourceContainer && scopeStack.empty()) {
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

        while (pos < src.size()) {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            if (pos >= src.size()) break;
            if (src[pos] == '>') {
                ++pos;
                break;
            }
            if (src.substr(pos, 2) == "/>") {
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

        // If currently inside a non-visual scope, handle resource extraction or brush property assignment
        if (nonVisualDepth > 0) {
            XmlScopeEntry& parentScope = scopeStack.back();
            if (parentScope.kind == XmlScopeKind::ResourceContainer) {
                if (!xKey.empty()) {
                    if (shortTag == "SolidColorBrush") {
                        for (const auto& a : attrs) {
                            if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                                pkg.resources.emplace_back(
                                    std::string(xKey),
                                    value::FromColor(ParseColorLiteral(a.value)));
                                break;
                            }
                        }
                    } else if (!selfClosing &&
                               (shortTag == "LinearGradientBrush" || shortTag == "RadialGradientBrush" ||
                                shortTag == "Color" || shortTag == "String" || shortTag == "FlowDirection")) {
                        XmlScopeEntry itemScope{};
                        itemScope.kind = XmlScopeKind::KeyedResourceItem;
                        itemScope.tagName = std::string(tagName);
                        itemScope.resourceKey = std::string(xKey);
                        scopeStack.push_back(std::move(itemScope));
                        ++nonVisualDepth;
                        continue;
                    }
                }
                if (!selfClosing) {
                    XmlScopeEntry childScope{};
                    childScope.kind = (shortTag == "ResourceDictionary")
                        ? XmlScopeKind::ResourceContainer
                        : XmlScopeKind::NonVisualSkip;
                    childScope.tagName = std::string(tagName);
                    scopeStack.push_back(std::move(childScope));
                    ++nonVisualDepth;
                }
                continue;
            }
            if (parentScope.kind == XmlScopeKind::KeyedResourceItem && !parentScope.brushAssigned &&
                shortTag == "GradientStop") {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        pkg.resources.emplace_back(
                            parentScope.resourceKey,
                            value::FromColor(ParseColorLiteral(a.value)));
                        parentScope.brushAssigned = true;
                        break;
                    }
                }
            }
            if (parentScope.kind == XmlScopeKind::BrushPropertyElement && !parentScope.brushAssigned &&
                (shortTag == "SolidColorBrush" || shortTag == "GradientStop")) {
                for (const auto& a : attrs) {
                    if (a.name == "Color" && (a.value.empty() || a.value.front() != '{')) {
                        AxbInstruction setInst{};
                        setInst.opcode = AxbOpcode::SetStaticProperty;
                        setInst.propId = parentScope.targetPropId;
                        setInst.literalValue = value::FromColor(ParseColorLiteral(a.value));
                        pkg.instructions.push_back(std::move(setInst));
                        parentScope.brushAssigned = true;
                        break;
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
                scopeStack.push_back(std::move(skipScope));
                ++nonVisualDepth;
            }
            continue;
        }

        // Top-level ResourceDictionary root file (e.g. Resources.xaml, Language-en.xaml)
        if (!seenRoot && shortTag == "ResourceDictionary") {
            seenRoot = true;
            pkg.rootTypeName = "ResourceDictionary";
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

        // Property-element tags (<Type.Property>) or non-visual elements
        if (shortTag == "Grid.RowDefinitions" || shortTag == "Grid.ColumnDefinitions" ||
            EndsWith(shortTag, ".Header") ||
            EndsWith(shortTag, ".Content")) {
            if (!selfClosing) {
                XmlScopeEntry trans{};
                trans.kind = XmlScopeKind::TransparentContainer;
                trans.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Header")) {
                    trans.targetPropId = PropId_Title;
                } else if (EndsWith(shortTag, ".Content")) {
                    trans.targetPropId = PropId_Text;
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

        if (shortTag == "Run") {
            std::string runText;
            for (const auto& a : attrs) {
                if (a.name == "Text") {
                    runText = DecodeXmlEntities(a.value);
                    break;
                }
            }
            if (selfClosing) {
                if (!runText.empty()) {
                    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
                        if (it->kind == XmlScopeKind::VisualElement) {
                            if (!it->accumulatedText.empty()) it->accumulatedText.push_back(' ');
                            it->accumulatedText += runText;
                            break;
                        }
                    }
                }
            } else {
                XmlScopeEntry runScope{};
                runScope.kind = XmlScopeKind::RunInline;
                runScope.tagName = "Run";
                runScope.accumulatedText = std::move(runText);
                scopeStack.push_back(std::move(runScope));
            }
            continue;
        }

        if (shortTag == "ResourceDictionary" || EndsWith(shortTag, ".Resources")) {
            if (!selfClosing) {
                XmlScopeEntry resScope{};
                resScope.kind = XmlScopeKind::ResourceContainer;
                resScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(resScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (EndsWith(shortTag, ".Background") ||
            EndsWith(shortTag, ".Fill") ||
            EndsWith(shortTag, ".Foreground") ||
            EndsWith(shortTag, ".BorderBrush") ||
            EndsWith(shortTag, ".Stroke")) {
            if (!selfClosing) {
                XmlScopeEntry brushScope{};
                brushScope.kind = XmlScopeKind::BrushPropertyElement;
                brushScope.tagName = std::string(tagName);
                if (EndsWith(shortTag, ".Foreground")) {
                    brushScope.targetPropId = PropId_Foreground;
                } else if (EndsWith(shortTag, ".BorderBrush") ||
                           EndsWith(shortTag, ".Stroke")) {
                    brushScope.targetPropId = PropId_BorderBrush;
                } else {
                    brushScope.targetPropId = PropId_Background;
                }
                scopeStack.push_back(std::move(brushScope));
                ++nonVisualDepth;
            }
            continue;
        }

        if (shortTag.find('.') != std::string_view::npos || isNonVisualElementTag(shortTag)) {
            if (!selfClosing) {
                XmlScopeEntry skipScope{};
                skipScope.kind = XmlScopeKind::NonVisualSkip;
                skipScope.tagName = std::string(tagName);
                scopeStack.push_back(std::move(skipScope));
                ++nonVisualDepth;
            }
            continue;
        }

        const bool isRootElement = !seenRoot;
        if (!seenRoot) {
            seenRoot = true;
            pkg.rootTypeName = std::string(tagName);
        }

        if (!xName.empty()) {
            AxbNamedControlSlot slot{};
            slot.name = std::string(xName);
            slot.typeName = std::string(tagName);
            slot.typeSymbolId = rs_sem::stableTypeId("Aero.UI." + std::string(shortTag));
            pkg.namedControls.push_back(std::move(slot));
        }

        AxbInstruction beginInst{};
        beginInst.opcode = AxbOpcode::BeginElement;
        beginInst.strIdx0 = pkg.InternString(tagName);
        beginInst.strIdx1 = pkg.InternString(xName);
        pkg.instructions.push_back(std::move(beginInst));

        bool hasExplicitWidth = false;
        bool hasExplicitHeight = false;
        for (const auto& a : attrs) {
            if (a.name == "Width") hasExplicitWidth = true;
            if (a.name == "Height") hasExplicitHeight = true;
        }

        for (const auto& attr : attrs) {
            if (attr.name == "d:DesignWidth" && (!isRootElement || hasExplicitWidth)) {
                continue;
            }
            if (attr.name == "d:DesignHeight" && (!isRootElement || hasExplicitHeight)) {
                continue;
            }
            if (attr.name == "Click" || attr.name == "OnClick") {
                const auto evIdx = static_cast<std::uint32_t>(pkg.eventBindings.size());
                AxbEventBindingRecord ev{};
                ev.controlName = std::string(xName);
                ev.eventName = "Click";
                ev.handlerMethodName = std::string(Trim(attr.value));
                pkg.eventBindings.push_back(std::move(ev));

                AxbInstruction evInst{};
                evInst.opcode = AxbOpcode::BindEvent;
                evInst.strIdx0 = evIdx;
                pkg.instructions.push_back(std::move(evInst));
                continue;
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

            const std::uint16_t propId = ResolvePropertyId(attr.name);

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

                // Handle {StaticResource ...}, {DynamicResource ...}, {noesis:Loc ...}
                if (inner.rfind("StaticResource ", 0) == 0 ||
                    inner.rfind("DynamicResource ", 0) == 0 ||
                    inner.rfind("noesis:Loc ", 0) == 0) {
                    std::string_view keyPart;
                    if (inner.rfind("StaticResource ", 0) == 0) keyPart = Trim(inner.substr(15));
                    else if (inner.rfind("DynamicResource ", 0) == 0) keyPart = Trim(inner.substr(16));
                    else keyPart = Trim(inner.substr(11));

                    if (keyPart.rfind("ResourceKey=", 0) == 0) {
                        keyPart = Trim(keyPart.substr(12));
                    }
                    if (!keyPart.empty()) {
                        AxbInstruction resInst{};
                        resInst.opcode = AxbOpcode::SetResourceProperty;
                        resInst.propId = propId;
                        resInst.strIdx0 = pkg.InternString(keyPart);
                        if (const UnifiedValue* resVal = findLocalOrGlobalResource(keyPart)) {
                            resInst.literalValue = *resVal;
                        } else if (inner.rfind("noesis:Loc ", 0) == 0 &&
                                   (propId == PropId_Text || propId == PropId_Title ||
                                    propId == PropId_Content || propId == PropId_Placeholder)) {
                            resInst.literalValue = value::FromString(std::string(keyPart));
                        }
                        pkg.instructions.push_back(std::move(resInst));
                    }
                    continue;
                }

                // Skip non-DataContext markup extensions ({TemplateBinding}, {x:Type}, {x:Static}, {x:Null}, {RelativeSource})
                if (inner.rfind("TemplateBinding", 0) == 0 ||
                    inner.rfind("x:Type", 0) == 0 ||
                    inner.rfind("x:Static", 0) == 0 ||
                    inner.rfind("x:Null", 0) == 0 ||
                    inner.rfind("RelativeSource", 0) == 0) {
                    continue;
                }

                if (inner == "Binding" || inner == "Bind") {
                    inner = {};
                } else if (inner.rfind("Binding ", 0) == 0) {
                    inner = Trim(inner.substr(8));
                } else if (inner.rfind("Bind ", 0) == 0) {
                    inner = Trim(inner.substr(5));
                } else {
                    continue;
                }

                BindingMode mode = BindingMode::OneWay;
                std::string_view pathPart;
                std::string_view fallbackValue;

                std::size_t clausePos = 0;
                bool firstClause = true;
                while (clausePos < inner.size()) {
                    const std::size_t comma = inner.find(',', clausePos);
                    const std::string_view clause = Trim(
                        (comma == std::string_view::npos)
                            ? inner.substr(clausePos)
                            : inner.substr(clausePos, comma - clausePos));
                    if (clause.rfind("Path=", 0) == 0) {
                        pathPart = Trim(clause.substr(5));
                    } else if (clause.rfind("Mode=", 0) == 0) {
                        const std::string_view modeStr = Trim(clause.substr(5));
                        if (modeStr == "TwoWay") mode = BindingMode::TwoWay;
                        else if (modeStr == "OneTime") mode = BindingMode::OneTime;
                    } else if (clause.rfind("FallbackValue=", 0) == 0) {
                        fallbackValue = Trim(clause.substr(14));
                    } else if (firstClause && clause.find('=') == std::string_view::npos) {
                        pathPart = clause;
                    }
                    firstClause = false;
                    if (comma == std::string_view::npos) break;
                    clausePos = comma + 1;
                }

                UnifiedValue parsedFallback{};
                if (!fallbackValue.empty()) {
                    parsedFallback = ParseLiteralPropertyValue(propId, fallbackValue);
                    AxbInstruction fbInst{};
                    fbInst.opcode = AxbOpcode::SetStaticProperty;
                    fbInst.propId = propId;
                    fbInst.literalValue = parsedFallback;
                    pkg.instructions.push_back(std::move(fbInst));
                }

                if (pathPart.empty() || pathPart == ".") {
                    continue;
                }

                AxbCompiledBindingChain chain{};
                chain.rawPath = std::string(pathPart);
                chain.targetPropertyId = propId;
                chain.mode = mode;
                chain.fallbackValue = std::move(parsedFallback);

                std::size_t segPos = 0;
                while (segPos < pathPart.size()) {
                    const std::size_t dot = pathPart.find('.', segPos);
                    const std::string_view seg = (dot == std::string_view::npos)
                        ? pathPart.substr(segPos)
                        : pathPart.substr(segPos, dot - segPos);
                    if (!seg.empty()) {
                        AxbCompiledHop hop{};
                        hop.memberName = std::string(seg);
                        chain.hops.push_back(std::move(hop));
                    }
                    if (dot == std::string_view::npos) break;
                    segPos = dot + 1;
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

            AxbInstruction setInst{};
            setInst.opcode = AxbOpcode::SetStaticProperty;
            setInst.propId = propId;
            setInst.literalValue = ParseLiteralPropertyValue(propId, trimmedVal);
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
            scopeStack.push_back(std::move(visScope));
        }
    }

    // Generate *.g.rs partial class stub when x:Class is specified
    std::vector<SourceFile> allSources = input.scriptSources;
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

        result.generatedPartialRsPath = shortClassName + ".g.rs";
        std::string stub = "// Auto-generated by aero-compiler from " + input.xamlPath + "\n";
        stub += "module " + moduleName + ";\n\n";
        stub += "public partial class " + shortClassName + "\n{\n";
        stub += "    public handle __nativeRootHandle;\n";
        for (const auto& nc : pkg.namedControls) {
            stub += "    public handle " + nc.name + ";\n";
        }
        stub += "}\n";
        result.generatedPartialRsCode = stub;

        if (!input.scriptSources.empty()) {
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
            const auto generatedScriptAot = aotGen.generate(build.modules, aotDiag, genOpts);
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
    if (!pkg.className.empty()) {
        AxbTemplateRegistry::Instance().Register(pkg.className, pkg);
    } else {
        AxbTemplateRegistry::Instance().RegisterResources(pkg);
    }
    result.succeeded = true;
    return result;
}

namespace {

std::string FormatFloatLiteral(float v) {
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6ff", v);
    return std::string(buf);
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
    if (const auto* f4 = std::get_if<Float4Value>(&val)) {
        return "aero::UnifiedValue{aero::Float4Value{" +
               FormatFloatLiteral(f4->x) + ", " +
               FormatFloatLiteral(f4->y) + ", " +
               FormatFloatLiteral(f4->z) + ", " +
               FormatFloatLiteral(f4->w) + "}}";
    }
    if (const auto* s = std::get_if<StringValue>(&val)) {
        return "aero::value::FromString(\"" + rs_aot::escapeCppString(s->str()) + "\")";
    }
    return "aero::UnifiedValue{}";
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

    // 2. Emit C++17 Source (.cpp) - unrolled batch creation, static properties & pre-resolved bindings
    std::string src;
    src += "// Auto-generated by aero-compiler --aot-cpp (Zero-XML C++17 Visual Tree & Binding Builder)\n";
    src += "#include \"aero/controls/Controls.hpp\"\n";
    src += "#include \"aero/binding/DualTrackBindingEngine.hpp\"\n\n";
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
    src += "    aero::NativeHandleRegistry* registry = domains ? &domains->nativeHandles() : nullptr;\n";
    src += "    aero::ManagedHeap* uiHeap = domains ? &domains->uiHeap() : nullptr;\n";
    src += "    const aero::ObjectRef effectiveContext = dataContext.valid() ? dataContext : rootControl->BoundScriptWrapper();\n";
    src += "    const aero::ObjectRef wrapperObj = rootControl->BoundScriptWrapper().valid() ? rootControl->BoundScriptWrapper() : effectiveContext;\n\n";

    std::vector<std::string> elemVarStack;
    std::size_t nextElemId = 0;
    bool rootVisited = false;

    for (const auto& inst : package.instructions) {
        switch (inst.opcode) {
            case AxbOpcode::BeginElement: {
                const std::string_view typeName = package.GetString(inst.strIdx0);
                const std::string_view elemName = package.GetString(inst.strIdx1);
                const std::string varName = "elem_" + std::to_string(nextElemId++);

                if (!rootVisited) {
                    rootVisited = true;
                    src += "    aero::UIElement* " + varName + " = rootControl;\n";
                    if (package.rootHandleFieldIndex != PropertySlotHandle::InvalidIndex) {
                        src += "    if (domains && registry && wrapperObj.valid()) {\n";
                        src += "        const auto rootH = rootControl->ToNativeHandle(*registry);\n";
                        src += "        (void)domains->writeObjectField(aero::DomainKind::Ui, wrapperObj, " +
                               std::to_string(package.rootHandleFieldIndex) + "u, rootH, nullptr);\n";
                        src += "    }\n";
                    }
                } else {
                    const std::string parentVar = elemVarStack.empty() ? "rootControl" : elemVarStack.back();
                    const std::string escapedType = rs_aot::escapeCppString(std::string(typeName));
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
                    src += "    }\n";
                }

                if (const auto probe = CreateElementByTypeName(typeName);
                    probe && probe->IsA<ItemsControl>()) {
                    src += "    if (auto* ic = " + varName + " ? " + varName + "->DynamicCast<aero::ItemsControl>() : nullptr) {\n";
                    src += "        ic->SetDomains(domains);\n";
                    src += "    }\n";
                }

                if (!elemName.empty()) {
                    src += "    if (" + varName + ") {\n";
                    src += "        " + varName + "->SetName(\"" + rs_aot::escapeCppString(std::string(elemName)) + "\");\n";
                    for (const auto& namedSlot : package.namedControls) {
                        if (namedSlot.name == elemName &&
                            namedSlot.fieldIndexInClass != PropertySlotHandle::InvalidIndex) {
                            src += "        if (domains && registry && wrapperObj.valid()) {\n";
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
                    src += "    if (" + curVar + " && bindingEngine && effectiveContext.valid()) {\n";
                    src += "        aero::AxbCompiledBindingChain chain{};\n";
                    src += "        chain.rawPath = \"" + rs_aot::escapeCppString(chain.rawPath) + "\";\n";
                    src += "        chain.targetPropertyId = " + std::to_string(chain.targetPropertyId) + "u;\n";
                    src += "        chain.mode = static_cast<aero::BindingMode>(" +
                           std::to_string(static_cast<unsigned>(chain.mode)) + "u);\n";
                    if (!std::holds_alternative<std::monostate>(chain.fallbackValue)) {
                        src += "        chain.fallbackValue = " + EmitUnifiedValueLiteralCpp(chain.fallbackValue) + ";\n";
                    }
                    for (const auto& hop : chain.hops) {
                        src += "        {\n";
                        src += "            aero::AxbCompiledHop hop{};\n";
                        src += "            hop.memberName = \"" + rs_aot::escapeCppString(hop.memberName) + "\";\n";
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
                    src += "        (void)bindingEngine->BindChain(effectiveContext, chain, aero::IntrusivePtr<aero::UIElement>(" + curVar + ", true));\n";
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
                           rs_aot::escapeCppString(std::string(fieldName)) + "\", 0u, aero::IntrusivePtr<aero::UIElement>(" +
                           curVar + ", true), " + std::to_string(inst.propId) + "u);\n";
                    src += "    }\n";
                }
                break;
            }

            case AxbOpcode::BindEvent: {
                if (!elemVarStack.empty()) {
                    const std::string& curVar = elemVarStack.back();
                    src += "    if (" + curVar + " && wrapperObj.valid() && " +
                           std::to_string(inst.symbolId) + "ULL != 0ULL) {\n";
                    src += "        if (auto* btn = " + curVar + "->DynamicCast<aero::Button>()) {\n";
                    src += "            if (eventInterpreter) {\n";
                    src += "                btn->BindScriptClickHandler(eventInterpreter, wrapperObj, " +
                           std::to_string(inst.symbolId) + "ULL);\n";
                    src += "            } else if (eventAotContext) {\n";
                    src += "                btn->BindAotClickHandler(eventAotContext, wrapperObj, " +
                           std::to_string(inst.symbolId) + "ULL);\n";
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
                    const std::string escapedKey = rs_aot::escapeCppString(std::string(package.GetString(inst.strIdx0)));
                    src += "    if (" + curVar + ") {\n";
                    src += "        if (const auto* rv = aero::AxbTemplateRegistry::Instance().FindResource(\"" +
                           escapedKey + "\")) {\n";
                    src += "            " + curVar + "->SetValue(" + std::to_string(inst.propId) +
                           "u, *rv, aero::ValueSourceRank::Local);\n";
                    if (!std::holds_alternative<std::monostate>(inst.literalValue)) {
                        src += "        } else {\n";
                        src += "            " + curVar + "->SetValue(" + std::to_string(inst.propId) + "u, " +
                               EmitUnifiedValueLiteralCpp(inst.literalValue) + ", aero::ValueSourceRank::Local);\n";
                    }
                    src += "        }\n";
                    src += "    }\n";
                }
                break;
            }
        }
    }

    src += "    return " + std::string(rootVisited ? "true" : "false") + ";\n";
    src += "}\n\n";
    src += "} // namespace " + out.cppNamespace + "\n";
    out.source = std::move(src);
    return out;
}

} // namespace aero
