#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/UiResourceContext.hpp"
#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

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

inline constexpr std::uint32_t kAxb4MtsdfSectionMagic = 0x4644534Du; // "MSDF"
inline constexpr std::uint32_t kAxb4StyleAnimSectionMagic = 0x35435453u; // "STC5"
inline constexpr std::uint32_t kAxb4Phase5DSectionMagic = 0x35505444u;   // "DTP5"

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
    } else if (Float4Value f4{}; val.tryGetFloat4(f4)) {
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

std::string_view TrimWs(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

float ParseFloat32Fast(std::string_view text, float fallback = 0.0f) noexcept {
    text = TrimWs(text);
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

std::vector<float> ParseFloatListFast(std::string_view text) {
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
        nums.push_back(ParseFloat32Fast(text.substr(pos, end - pos)));
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

// ============================================================================
// SVG Path Mini-Language Parser & 4-Channel MTSDF Baker
// ============================================================================

struct SvgVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct ColoredSegment {
    SvgVec2 p0{};
    SvgVec2 p1{};
    std::uint8_t channelMask = 0x07u; // Bit 0 = R, Bit 1 = G, Bit 2 = B
    bool inClosedContour = true;
};

struct SvgContour {
    std::vector<SvgVec2> points;
    bool closed = false;
};

struct ParsedSvgGeometry {
    bool nonZeroFill = false; // Default XAML F0 = EvenOdd, F1 = NonZero
    std::vector<SvgContour> contours;
    Rect bounds{0.0f, 0.0f, 0.0f, 0.0f};
};

void SkipSvgDelimiters(std::string_view s, std::size_t& pos) noexcept {
    while (pos < s.size() &&
           (s[pos] == ',' || std::isspace(static_cast<unsigned char>(s[pos])))) {
        ++pos;
    }
}

bool TryReadSvgFloat(std::string_view s, std::size_t& pos, float& outVal) noexcept {
    SkipSvgDelimiters(s, pos);
    if (pos >= s.size()) {
        return false;
    }
    const char ch = s[pos];
    if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' || ch == '.')) {
        return false;
    }
    std::size_t start = pos;
    if (s[pos] == '+' || s[pos] == '-') {
        ++pos;
    }
    bool seenDot = false;
    bool seenDigit = false;
    while (pos < s.size()) {
        const char c = s[pos];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            ++pos;
        } else if (c == '.' && !seenDot) {
            seenDot = true;
            ++pos;
        } else if ((c == 'e' || c == 'E') && seenDigit) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                ++pos;
            }
        } else {
            break;
        }
    }
    if (!seenDigit) {
        pos = start;
        return false;
    }
    outVal = ParseFloat32Fast(s.substr(start, pos - start), 0.0f);
    return true;
}

// SVG 1.1 / XAML StreamGeometry arc flag grammar: single '0' or '1' digit (may omit trailing separator, e.g. "a4,4 0 014,-4")
bool TryReadSvgArcFlag(std::string_view s, std::size_t& pos, bool& outFlag) noexcept {
    SkipSvgDelimiters(s, pos);
    if (pos >= s.size() || (s[pos] != '0' && s[pos] != '1')) {
        return false;
    }
    outFlag = (s[pos] == '1');
    ++pos;
    return true;
}

// W3C SVG 1.1 Appendix F.6.5 & F.6.6 endpoint-to-center elliptical arc tessellation
void AppendSvgEllipticalArc(
    std::vector<SvgVec2>& contour,
    SvgVec2 p0,
    float rxRaw,
    float ryRaw,
    float xAxisRotDeg,
    bool largeArc,
    bool sweep,
    SvgVec2 p1) {
    const float dx2 = (p0.x - p1.x) * 0.5f;
    const float dy2 = (p0.y - p1.y) * 0.5f;
    if (dx2 * dx2 + dy2 * dy2 <= 1e-10f) {
        return;
    }
    float rx = std::fabs(rxRaw);
    float ry = std::fabs(ryRaw);
    if (rx <= 1e-5f || ry <= 1e-5f) {
        contour.push_back(p1);
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float phi = xAxisRotDeg * (kPi / 180.0f);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    const float x1p = cosPhi * dx2 + sinPhi * dy2;
    const float y1p = -sinPhi * dx2 + cosPhi * dy2;

    float rxSq = rx * rx;
    float rySq = ry * ry;
    const float x1pSq = x1p * x1p;
    const float y1pSq = y1p * y1p;

    const float lambda = x1pSq / rxSq + y1pSq / rySq;
    if (lambda > 1.0f) {
        const float s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
        rxSq = rx * rx;
        rySq = ry * ry;
    }

    const float denom = rxSq * y1pSq + rySq * x1pSq;
    const float numer = std::max(0.0f, rxSq * rySq - denom);
    const float rad = (denom > 1e-12f) ? std::sqrt(numer / denom) : 0.0f;
    const float sign = (largeArc == sweep) ? -1.0f : 1.0f;
    const float cxp = sign * rad * (rx * y1p / ry);
    const float cyp = sign * rad * (-ry * x1p / rx);

    const float cx = cosPhi * cxp - sinPhi * cyp + (p0.x + p1.x) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (p0.y + p1.y) * 0.5f;

    auto vecAngle = [](float ux, float uy, float vx, float vy) noexcept -> float {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        if (len <= 1e-12f) return 0.0f;
        const float c = std::clamp(dot / len, -1.0f, 1.0f);
        const float a = std::acos(c);
        return (ux * vy - uy * vx < 0.0f) ? -a : a;
    };

    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;

    const float theta1 = vecAngle(1.0f, 0.0f, ux, uy);
    float deltaTheta = vecAngle(ux, uy, vx, vy);
    if (!sweep && deltaTheta > 0.0f) {
        deltaTheta -= 2.0f * kPi;
    } else if (sweep && deltaTheta < 0.0f) {
        deltaTheta += 2.0f * kPi;
    }

    const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(deltaTheta) / (kPi / 10.0f))), 2, 48);
    for (int i = 1; i <= steps; ++i) {
        if (i == steps) {
            contour.push_back(p1);
        } else {
            const float th = theta1 + deltaTheta * (static_cast<float>(i) / static_cast<float>(steps));
            const float ex = rx * std::cos(th);
            const float ey = ry * std::sin(th);
            contour.push_back(SvgVec2{
                cosPhi * ex - sinPhi * ey + cx,
                sinPhi * ex + cosPhi * ey + cy});
        }
    }
}

ParsedSvgGeometry ParseSvgPathGeometry(std::string_view pathData) {
    ParsedSvgGeometry geom{};
    std::size_t pos = 0;
    SkipSvgDelimiters(pathData, pos);

    // Optional XAML FillRule prefix: "F0" (EvenOdd) or "F1" (NonZero)
    if (pos < pathData.size() && (pathData[pos] == 'F' || pathData[pos] == 'f')) {
        ++pos;
        float ruleVal = 0.0f;
        if (TryReadSvgFloat(pathData, pos, ruleVal)) {
            geom.nonZeroFill = (static_cast<int>(ruleVal) != 0);
        }
    }

    std::vector<SvgVec2> currentContour;
    bool currentClosed = false;
    SvgVec2 cur{0.0f, 0.0f};
    SvgVec2 startPt{0.0f, 0.0f};
    SvgVec2 prevCtrl{0.0f, 0.0f};
    char cmd = 0;
    char prevCmd = 0;

    auto flushContour = [&]() {
        if (currentContour.size() >= 2u) {
            if (!currentClosed && currentContour.size() >= 4u) {
                const SvgVec2 first = currentContour.front();
                const SvgVec2 last = currentContour.back();
                if (std::fabs(first.x - last.x) <= 1e-4f && std::fabs(first.y - last.y) <= 1e-4f) {
                    currentClosed = true;
                }
            }
            geom.contours.push_back(SvgContour{std::move(currentContour), currentClosed});
        }
        currentContour.clear();
        currentClosed = false;
    };

    while (pos < pathData.size()) {
        SkipSvgDelimiters(pathData, pos);
        if (pos >= pathData.size()) {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(pathData[pos]))) {
            cmd = pathData[pos++];
        } else if (cmd == 0) {
            ++pos;
            continue;
        }

        switch (cmd) {
            case 'M':
            case 'm': {
                float x = 0.0f;
                float y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                flushContour();
                cur = (cmd == 'm') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                startPt = cur;
                currentContour.push_back(cur);
                cmd = (cmd == 'm') ? 'l' : 'L';
                break;
            }
            case 'L':
            case 'l': {
                float x = 0.0f;
                float y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                cur = (cmd == 'l') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                break;
            }
            case 'H':
            case 'h': {
                float x = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x)) {
                    cmd = 0;
                    break;
                }
                cur.x = (cmd == 'h') ? (cur.x + x) : x;
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                break;
            }
            case 'V':
            case 'v': {
                float y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                cur.y = (cmd == 'v') ? (cur.y + y) : y;
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                break;
            }
            case 'C':
            case 'c': {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x1) || !TryReadSvgFloat(pathData, pos, y1) ||
                    !TryReadSvgFloat(pathData, pos, x2) || !TryReadSvgFloat(pathData, pos, y2) ||
                    !TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const SvgVec2 p0 = cur;
                const SvgVec2 c1 = (cmd == 'c') ? SvgVec2{cur.x + x1, cur.y + y1} : SvgVec2{x1, y1};
                const SvgVec2 c2 = (cmd == 'c') ? SvgVec2{cur.x + x2, cur.y + y2} : SvgVec2{x2, y2};
                const SvgVec2 p3 = (cmd == 'c') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                constexpr int kSteps = 8;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(kSteps);
                    const float u = 1.0f - t;
                    const SvgVec2 pt{
                        u * u * u * p0.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * p3.x,
                        u * u * u * p0.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * p3.y};
                    currentContour.push_back(pt);
                }
                prevCtrl = c2;
                cur = p3;
                break;
            }
            case 'S':
            case 's': {
                float x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x2) || !TryReadSvgFloat(pathData, pos, y2) ||
                    !TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const SvgVec2 p0 = cur;
                const SvgVec2 c1 = (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's')
                    ? SvgVec2{2.0f * cur.x - prevCtrl.x, 2.0f * cur.y - prevCtrl.y}
                    : cur;
                const SvgVec2 c2 = (cmd == 's') ? SvgVec2{cur.x + x2, cur.y + y2} : SvgVec2{x2, y2};
                const SvgVec2 p3 = (cmd == 's') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                constexpr int kSteps = 8;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(kSteps);
                    const float u = 1.0f - t;
                    currentContour.push_back(SvgVec2{
                        u * u * u * p0.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * p3.x,
                        u * u * u * p0.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * p3.y});
                }
                prevCtrl = c2;
                cur = p3;
                break;
            }
            case 'Q':
            case 'q': {
                float x1 = 0.0f, y1 = 0.0f, x = 0.0f, y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x1) || !TryReadSvgFloat(pathData, pos, y1) ||
                    !TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const SvgVec2 p0 = cur;
                const SvgVec2 c1 = (cmd == 'q') ? SvgVec2{cur.x + x1, cur.y + y1} : SvgVec2{x1, y1};
                const SvgVec2 p2 = (cmd == 'q') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                constexpr int kSteps = 6;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(kSteps);
                    const float u = 1.0f - t;
                    currentContour.push_back(SvgVec2{
                        u * u * p0.x + 2.0f * u * t * c1.x + t * t * p2.x,
                        u * u * p0.y + 2.0f * u * t * c1.y + t * t * p2.y});
                }
                prevCtrl = c1;
                cur = p2;
                break;
            }
            case 'T':
            case 't': {
                float x = 0.0f, y = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, x) || !TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const SvgVec2 p0 = cur;
                const SvgVec2 c1 = (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't')
                    ? SvgVec2{2.0f * cur.x - prevCtrl.x, 2.0f * cur.y - prevCtrl.y}
                    : cur;
                const SvgVec2 p2 = (cmd == 't') ? SvgVec2{cur.x + x, cur.y + y} : SvgVec2{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                constexpr int kSteps = 6;
                for (int i = 1; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(kSteps);
                    const float u = 1.0f - t;
                    currentContour.push_back(SvgVec2{
                        u * u * p0.x + 2.0f * u * t * c1.x + t * t * p2.x,
                        u * u * p0.y + 2.0f * u * t * c1.y + t * t * p2.y});
                }
                prevCtrl = c1;
                cur = p2;
                break;
            }
            case 'A':
            case 'a': {
                float rx = 0.0f, ry = 0.0f, rot = 0.0f, ex = 0.0f, ey = 0.0f;
                bool largeArc = false;
                bool sweep = false;
                if (!TryReadSvgFloat(pathData, pos, rx) ||
                    !TryReadSvgFloat(pathData, pos, ry) ||
                    !TryReadSvgFloat(pathData, pos, rot) ||
                    !TryReadSvgArcFlag(pathData, pos, largeArc) ||
                    !TryReadSvgArcFlag(pathData, pos, sweep) ||
                    !TryReadSvgFloat(pathData, pos, ex) ||
                    !TryReadSvgFloat(pathData, pos, ey)) {
                    cmd = 0;
                    break;
                }
                const SvgVec2 p0 = cur;
                const SvgVec2 p1 = (cmd == 'a') ? SvgVec2{cur.x + ex, cur.y + ey} : SvgVec2{ex, ey};
                if (currentContour.empty()) currentContour.push_back(p0);
                AppendSvgEllipticalArc(currentContour, p0, rx, ry, rot, largeArc, sweep, p1);
                cur = p1;
                break;
            }
            case 'Z':
            case 'z': {
                if (!currentContour.empty()) {
                    const SvgVec2 last = currentContour.back();
                    if (std::fabs(last.x - startPt.x) > 1e-4f || std::fabs(last.y - startPt.y) > 1e-4f) {
                        currentContour.push_back(startPt);
                    }
                    currentClosed = true;
                    flushContour();
                }
                cur = startPt;
                cmd = 0;
                break;
            }
            default: {
                float dummy = 0.0f;
                if (!TryReadSvgFloat(pathData, pos, dummy)) {
                    cmd = 0;
                }
                break;
            }
        }
        prevCmd = cmd;
    }
    flushContour();

    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    bool anyPt = false;
    for (const auto& contour : geom.contours) {
        for (const auto& pt : contour.points) {
            minX = std::min(minX, pt.x);
            minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x);
            maxY = std::max(maxY, pt.y);
            anyPt = true;
        }
    }

    if (anyPt) {
        geom.bounds = Rect{
            minX,
            minY,
            std::max(1.0f, maxX - minX),
            std::max(1.0f, maxY - minY)};
    } else {
        geom.bounds = Rect{0.0f, 0.0f, 32.0f, 32.0f};
    }
    return geom;
}

std::vector<ColoredSegment> BuildCornerColoredSegments(const ParsedSvgGeometry& geom) {
    std::vector<ColoredSegment> allSegments;
    static constexpr std::uint8_t kCornerPalette[3] = {
        0x05u, // Magenta (R | B)
        0x03u, // Yellow  (R | G)
        0x06u, // Cyan    (G | B)
    };

    for (const auto& contour : geom.contours) {
        std::vector<ColoredSegment> segs;
        for (std::size_t i = 0; i + 1u < contour.points.size(); ++i) {
            const float dx = contour.points[i + 1u].x - contour.points[i].x;
            const float dy = contour.points[i + 1u].y - contour.points[i].y;
            if (dx * dx + dy * dy > 1e-8f) {
                segs.push_back(ColoredSegment{
                    contour.points[i],
                    contour.points[i + 1u],
                    0x07u,
                    contour.closed});
            }
        }
        if (segs.empty()) {
            continue;
        }

        if (contour.closed) {
            // Identify sharp corners between consecutive segments around closed loop
            std::vector<std::size_t> cornerIndices;
            const std::size_t n = segs.size();
            for (std::size_t i = 0; i < n; ++i) {
                const auto& prev = segs[(i + n - 1u) % n];
                const auto& curr = segs[i];
                const float ux = prev.p1.x - prev.p0.x;
                const float uy = prev.p1.y - prev.p0.y;
                const float vx = curr.p1.x - curr.p0.x;
                const float vy = curr.p1.y - curr.p0.y;
                const float lenU = std::sqrt(ux * ux + uy * uy);
                const float lenV = std::sqrt(vx * vx + vy * vy);
                if (lenU > 1e-6f && lenV > 1e-6f) {
                    const float dotNorm = (ux * vx + uy * vy) / (lenU * lenV);
                    const float crossNorm = std::fabs(ux * vy - uy * vx) / (lenU * lenV);
                    if (dotNorm <= 0.985f || crossNorm >= 0.14f) {
                        cornerIndices.push_back(i);
                    }
                }
            }

            if (cornerIndices.size() >= 2u) {
                const std::size_t cCount = cornerIndices.size();
                for (std::size_t c = 0; c < cCount; ++c) {
                    std::uint8_t mask = kCornerPalette[c % 3u];
                    // Ensure last corner group doesn't collide with group 0's palette entry
                    if (c + 1u == cCount && (c % 3u) == 0u) {
                        mask = kCornerPalette[1u];
                    }
                    const std::size_t startSeg = cornerIndices[c];
                    const std::size_t endSeg = cornerIndices[(c + 1u) % cCount];
                    std::size_t idx = startSeg;
                    while (true) {
                        segs[idx].channelMask = mask;
                        idx = (idx + 1u) % n;
                        if (idx == endSeg) {
                            break;
                        }
                    }
                }
            } else if (cornerIndices.size() == 1u && n >= 2u) {
                // Single-corner teardrop contour: split smooth loop into 3 (or 2) channel-sharing arcs
                // so the two segments meeting at the lone sharp apex have distinct colors sharing 1 channel.
                const std::size_t c0 = cornerIndices[0];
                if (n >= 3u) {
                    const std::size_t len0 = n / 3u;
                    const std::size_t len1 = (n - len0) / 2u;
                    for (std::size_t k = 0; k < n; ++k) {
                        const std::uint8_t mask = (k < len0)
                            ? 0x05u // Magenta (R | B)
                            : ((k < len0 + len1) ? 0x06u /* Cyan (G | B) */ : 0x03u /* Yellow (R | G) */);
                        segs[(c0 + k) % n].channelMask = mask;
                    }
                } else {
                    segs[c0].channelMask = 0x05u;
                    segs[(c0 + 1u) % n].channelMask = 0x03u;
                }
            }
        }

        allSegments.insert(allSegments.end(), segs.begin(), segs.end());
    }
    return allSegments;
}

bool IsPointInsideSvgGeometry(const ParsedSvgGeometry& geom, float px, float py) noexcept {
    int winding = 0;
    int crossings = 0;
    for (const auto& contour : geom.contours) {
        if (!contour.closed) {
            continue;
        }
        for (std::size_t i = 0; i + 1u < contour.points.size(); ++i) {
            const SvgVec2 a = contour.points[i];
            const SvgVec2 b = contour.points[i + 1u];
            if ((a.y <= py && b.y > py) || (b.y <= py && a.y > py)) {
                const float vt = (py - a.y) / (b.y - a.y);
                const float xIntersect = a.x + vt * (b.x - a.x);
                if (px < xIntersect) {
                    ++crossings;
                }
            }
            if (a.y <= py) {
                if (b.y > py && ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) > 0.0f) {
                    ++winding;
                }
            } else {
                if (b.y <= py && ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) < 0.0f) {
                    --winding;
                }
            }
        }
    }
    return geom.nonZeroFill ? (winding != 0) : ((crossings & 1) != 0);
}

std::uint8_t QuantizeNormalizedSdf(float signedDistInTexels, float pxRange) noexcept {
    const float safeRange = std::max(pxRange, 1.0f);
    const float normalized = std::clamp(0.5f + (signedDistInTexels / (2.0f * safeRange)), 0.0f, 1.0f);
    return static_cast<std::uint8_t>(normalized * 255.0f + 0.5f);
}

} // namespace

// ============================================================================
// Public Property & MTSDF Utilities (Zero Compiler-Frontend Dependency)
// ============================================================================

Color ParseXamlColorLiteral(std::string_view raw) noexcept {
    raw = TrimWs(raw);
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
        {"Moccasin", "FFFFE4B5"},
        {"Aquamarine", "FF7FFFD4"},
        {"Thistle", "FFD8BFD8"},
        {"CadetBlue", "FF5F9EA0"},
        {"PaleTurquoise", "FFAFEEEE"},
        {"YellowGreen", "FF9ACD32"},
        {"AliceBlue", "FFF0F8FF"},
        {"Khaki", "FFF0E68C"},
        {"Orchid", "FFDA70D6"},
    };
    for (const auto& entry : kNamedColors) {
        if (EqualsIgnoreCase(raw, entry.name)) {
            return ParseHexColor(entry.hex8);
        }
    }
    const auto vals = ParseFloatListFast(raw);
    if (vals.size() >= 4) return Color{vals[0], vals[1], vals[2], vals[3]};
    if (vals.size() == 3) return Color{vals[0], vals[1], vals[2], 1.0f};
    return Color{0.0f, 0.0f, 0.0f, 1.0f};
}

std::string DecodeXamlXmlEntities(std::string_view raw) {
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

Matrix2D ParseXamlMatrix2DLiteral(std::string_view raw) noexcept {
    raw = TrimWs(raw);
    if (raw.empty() || EqualsIgnoreCase(raw, "Identity")) {
        return Matrix2D::Identity();
    }
    const auto nums = ParseFloatListFast(raw);
    if (nums.size() >= 6u) {
        return Matrix2D::FromWpfMatrix(nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]);
    }
    if (nums.size() >= 4u) {
        return Matrix2D::FromWpfMatrix(nums[0], nums[1], nums[2], nums[3], 0.0f, 0.0f);
    }
    return Matrix2D::Identity();
}

std::uint16_t ResolveBuiltinPropertyId(std::string_view propertyName) noexcept {
    if (propertyName == "Width" || propertyName == "ActualWidth" || propertyName == "d:DesignWidth") return PropId_Width;
    if (propertyName == "Height" || propertyName == "ActualHeight" || propertyName == "d:DesignHeight") return PropId_Height;
    if (propertyName == "Custom.Number_Single" || propertyName == "Number_Single") return PropId_Value;
    if (propertyName == "MinWidth") return PropId_MinWidth;
    if (propertyName == "MinHeight") return PropId_MinHeight;
    if (propertyName == "MaxWidth") return PropId_MaxWidth;
    if (propertyName == "MaxHeight") return PropId_MaxHeight;
    if (propertyName == "HorizontalAlignment") return PropId_HorizontalAlignment;
    if (propertyName == "VerticalAlignment") return PropId_VerticalAlignment;
    if (propertyName == "Margin") return PropId_Margin;
    if (propertyName == "Padding") return PropId_Padding;
    if (propertyName == "Opacity") return PropId_Opacity;
    if (propertyName == "Visibility") return PropId_Visibility;
    if (propertyName == "Background" || propertyName == "Fill") return PropId_Background;
    if (propertyName == "Foreground") return PropId_Foreground;
    if (propertyName == "BorderBrush" || propertyName == "Stroke") return PropId_BorderBrush;
    if (propertyName == "BorderThickness") return PropId_BorderThickness;
    if (propertyName == "StrokeThickness") return PropId_StrokeThickness;
    if (propertyName == "CornerRadius") return PropId_CornerRadius;
    if (propertyName == "RadiusX") return PropId_RadiusX;
    if (propertyName == "RadiusY") return PropId_RadiusY;
    if (propertyName == "Points") return PropId_ShapePoints;
    if (propertyName == "Rows") return PropId_UniformRows;
    if (propertyName == "Columns") return PropId_UniformColumns;
    if (propertyName == "FirstColumn") return PropId_FirstColumn;
    if (propertyName == "Title") return PropId_Title;
    if (propertyName == "Header") return PropId_Header;
    if (propertyName == "Tag") return PropId_Tag;
    if (propertyName == "DataContext") return PropId_DataContext;
    if (propertyName == "Text" || propertyName == "Content") return PropId_Text;
    if (propertyName == "Placeholder" || propertyName == "noesis:Text.Placeholder") return PropId_Placeholder;
    if (propertyName == "FontSize" || propertyName == "TextElement.FontSize") return PropId_FontSize;
    if (propertyName == "FontFamily" || propertyName == "TextElement.FontFamily") return PropId_FontFamily;
    if (propertyName == "TextWrapping") return PropId_TextWrapping;
    if (propertyName == "Orientation") return PropId_Orientation;
    if (propertyName == "Grid.Row" || propertyName == "Row") return PropId_GridRow;
    if (propertyName == "Grid.Column" || propertyName == "Column") return PropId_GridColumn;
    if (propertyName == "Grid.RowSpan" || propertyName == "RowSpan") return PropId_GridRowSpan;
    if (propertyName == "Grid.ColumnSpan" || propertyName == "ColumnSpan") return PropId_GridColumnSpan;
    if (propertyName == "Canvas.Left" || propertyName == "Left") return PropId_CanvasLeft;
    if (propertyName == "Canvas.Top" || propertyName == "Top") return PropId_CanvasTop;
    if (propertyName == "DockPanel.Dock" || propertyName == "Dock") return PropId_Dock;
    if (propertyName == "LastChildFill") return PropId_LastChildFill;
    if (propertyName == "ItemWidth") return PropId_ItemWidth;
    if (propertyName == "ItemHeight") return PropId_ItemHeight;
    if (propertyName == "StretchDirection") return PropId_StretchDirection;
    if (propertyName == "HorizontalScrollBarVisibility" ||
        propertyName == "ScrollViewer.HorizontalScrollBarVisibility") {
        return PropId_HorizontalScrollBarVisibility;
    }
    if (propertyName == "VerticalScrollBarVisibility" ||
        propertyName == "ScrollViewer.VerticalScrollBarVisibility") {
        return PropId_VerticalScrollBarVisibility;
    }
    if (propertyName == "HorizontalOffset" || propertyName == "ScrollViewer.HorizontalOffset") {
        return PropId_HorizontalOffset;
    }
    if (propertyName == "VerticalOffset" || propertyName == "ScrollViewer.VerticalOffset") {
        return PropId_VerticalOffset;
    }
    if (propertyName == "ItemsSource") return PropId_ItemsSource;
    if (propertyName == "CommandParameter") return PropId_CommandParameter;
    if (propertyName == "Projection3D.Yaw" || propertyName == "Projection3DYaw") return PropId_Projection3DYaw;
    if (propertyName == "Projection3D.Pitch" || propertyName == "Projection3DPitch") return PropId_Projection3DPitch;
    if (propertyName == "Spacing") return PropId_Spacing;
    if (propertyName == "Value") return PropId_Value;
    if (propertyName == "Minimum" || propertyName == "MinValue") return PropId_Minimum;
    if (propertyName == "Maximum" || propertyName == "MaxValue") return PropId_Maximum;
    if (propertyName == "IsChecked" || propertyName == "IsSelected") return PropId_IsChecked;
    if (propertyName == "IsExpanded") return PropId_IsExpanded;
    if (propertyName == "BackgroundGradientId" || propertyName == "FillGradientId") return PropId_BackgroundGradientId;
    if (propertyName == "ForegroundGradientId") return PropId_ForegroundGradientId;
    if (propertyName == "BorderGradientId" || propertyName == "StrokeGradientId") return PropId_BorderGradientId;
    if (propertyName == "X1") return PropId_LineX1;
    if (propertyName == "Y1") return PropId_LineY1;
    if (propertyName == "X2") return PropId_LineX2;
    if (propertyName == "Y2") return PropId_LineY2;
    if (propertyName == "ExpandDirection") return PropId_ExpandDirection;
    if (propertyName == "IsIndeterminate") return PropId_IsIndeterminate;
    if (propertyName == "TabStripPlacement") return PropId_TabStripPlacement;
    if (propertyName == "ContentTemplate") return PropId_ContentTemplate;
    if (propertyName == "SelectedItem") return PropId_SelectedItem;
    if (propertyName == "SelectedIndex") return PropId_SelectedIndex;
    if (propertyName == "Data" || propertyName == "PathData") return PropId_PathData;
    if (propertyName == "Source" || propertyName == "ImageSource") return PropId_ImageSource;
    if (propertyName == "Stretch") return PropId_Stretch;
    if (propertyName == "ClipToBounds") return PropId_ClipToBounds;
    if (propertyName == "RenderTransformMatrix") return PropId_RenderTransformMatrix;
    if (propertyName == "RenderTransform") return PropId_RenderTransform;
    if (propertyName == "LayoutTransform") return PropId_LayoutTransform;
    if (propertyName == "RenderTransformTranslation") return PropId_RenderTransformTranslation;
    if (propertyName == "RenderTransformOrigin") return PropId_RenderTransformOrigin;
    if (propertyName == "IsEnabled") return PropId_IsEnabled;
    if (propertyName == "Focusable") return PropId_Focusable;
    if (propertyName == "GroupName") return PropId_GroupName;
    if (propertyName == "IsReadOnly") return PropId_IsReadOnly;
    if (propertyName == "MaxLength") return PropId_MaxLength;
    if (propertyName == "CaretIndex") return PropId_CaretIndex;
    if (propertyName == "IsDropDownOpen") return PropId_IsDropDownOpen;
    if (propertyName == "SmallChange") return PropId_SmallChange;
    if (propertyName == "LargeChange") return PropId_LargeChange;
    if (propertyName == "IsVirtualizing" ||
        propertyName == "VirtualizingStackPanel.IsVirtualizing") {
        return PropId_IsVirtualizing;
    }
    if (propertyName == "IsPassword") return PropId_IsPassword;
    if (propertyName == "Style") return PropId_Style;
    if (propertyName == "ItemTemplate") return PropId_ItemTemplate;
    if (propertyName == "Template") return PropId_Template;
    if (propertyName == "TemplatedParent") return PropId_TemplatedParent;
    if (propertyName == "HorizontalContentAlignment") return PropId_HorizontalContentAlignment;
    if (propertyName == "VerticalContentAlignment") return PropId_VerticalContentAlignment;
    if (propertyName == "ContentStringFormat") return PropId_ContentStringFormat;
    if (propertyName == "ScaleX" || propertyName == "AnimScaleX") return PropId_AnimScaleX;
    if (propertyName == "ScaleY" || propertyName == "AnimScaleY") return PropId_AnimScaleY;
    if (propertyName == "RotateAngle" || propertyName == "Angle" || propertyName == "AnimRotateAngle") {
        return PropId_AnimRotateAngle;
    }
    if (propertyName == "TranslateX" || propertyName == "AnimTranslateX") return PropId_AnimTranslateX;
    if (propertyName == "TranslateY" || propertyName == "AnimTranslateY") return PropId_AnimTranslateY;
    if (propertyName == "SkewX" || propertyName == "AnimSkewX" || propertyName == "AngleX") return PropId_AnimSkewX;
    if (propertyName == "SkewY" || propertyName == "AnimSkewY" || propertyName == "AngleY") return PropId_AnimSkewY;
    if (propertyName == "MergedDictionarySource") return PropId_MergedDictionarySource;
    if (propertyName == "noesis:Loc.Source" || propertyName == "Loc.Source") return PropId_LocSource;
    if (propertyName == "FlowDirection") return PropId_FlowDirection;
    if (propertyName == "noesis:RiveControl.Source" || propertyName == "RiveControl.Source" ||
        propertyName == "RiveSource") {
        return PropId_RiveSource;
    }
    if (propertyName == "Transform3D" || propertyName == "noesis:Element.Transform3D" ||
        propertyName == "Element.Transform3D") {
        return PropId_Transform3DRotateXYZ;
    }
    if (propertyName == "RotationX" || propertyName == "AnimRotation3DX" || propertyName == "Rotation3DX") {
        return PropId_AnimRotation3DX;
    }
    if (propertyName == "RotationY" || propertyName == "AnimRotation3DY" || propertyName == "Rotation3DY") {
        return PropId_AnimRotation3DY;
    }
    if (propertyName == "RotationZ" || propertyName == "AnimRotation3DZ" || propertyName == "Rotation3DZ") {
        return PropId_AnimRotation3DZ;
    }
    if (propertyName == "TranslateZ" || propertyName == "AnimTranslate3DZ" ||
        propertyName == "LocalOffsetZ" || propertyName == "GlobalOffsetZ") {
        return PropId_AnimTranslate3DZ;
    }
    if (propertyName == "Effect" || propertyName == "UIElement.Effect") return PropId_EffectParams;
    if (propertyName == "EffectKind") return PropId_EffectKind;
    if (propertyName == "EffectColor") return PropId_EffectColor;
    if (propertyName == "BlurRadius" || propertyName == "EffectBlurRadius") return PropId_EffectBlurRadius;
    if (propertyName == "ShadowDepth" || propertyName == "EffectShadowDepth") return PropId_EffectShadowDepth;
    return PropId_Tag;
}

std::uint16_t ResolveTriggerConditionPropertyId(std::string_view propertyName) noexcept {
    propertyName = TrimWs(propertyName);
    if (const auto dot = propertyName.rfind('.'); dot != std::string_view::npos) {
        propertyName = propertyName.substr(dot + 1);
    }
    if (propertyName == "IsMouseOver" || propertyName == "IsHovered") {
        return PropId_TriggerIsMouseOver;
    }
    if (propertyName == "IsPressed" || propertyName == "IsDragging") {
        return PropId_TriggerIsPressed;
    }
    if (propertyName == "IsFocused" || propertyName == "IsKeyboardFocused") {
        return PropId_TriggerIsFocused;
    }
    if (propertyName == "IsKeyboardFocusWithin") {
        return PropId_TriggerIsKeyboardFocusWithin;
    }
    return ResolveBuiltinPropertyId(propertyName);
}

std::uint16_t ResolveAnimationTargetPropertyId(std::string_view targetPropertyPath) noexcept {
    std::string_view s = TrimWs(targetPropertyPath);
    if (s.empty()) {
        return PropId_Tag;
    }
    const bool mentionsTransform3D =
        (s.find("Transform3D") != std::string_view::npos) ||
        (s.find("Projection") != std::string_view::npos);
    if (mentionsTransform3D || s.find("RotationX") != std::string_view::npos ||
        s.find("RotationY") != std::string_view::npos || s.find("RotationZ") != std::string_view::npos ||
        s.find("TranslateZ") != std::string_view::npos) {
        if (s.find("RotationX") != std::string_view::npos) return PropId_AnimRotation3DX;
        if (s.find("RotationY") != std::string_view::npos) return PropId_AnimRotation3DY;
        if (s.find("RotationZ") != std::string_view::npos) return PropId_AnimRotation3DZ;
        if (s.find("TranslateX") != std::string_view::npos || s.find("OffsetX") != std::string_view::npos) {
            return PropId_AnimTranslate3DX;
        }
        if (s.find("TranslateY") != std::string_view::npos || s.find("OffsetY") != std::string_view::npos) {
            return PropId_AnimTranslate3DY;
        }
        if (s.find("TranslateZ") != std::string_view::npos || s.find("OffsetZ") != std::string_view::npos) {
            return PropId_AnimTranslate3DZ;
        }
    }
    if (s.find("BlurRadius") != std::string_view::npos ||
        (s.find("BlurEffect") != std::string_view::npos && s.find("Radius") != std::string_view::npos)) {
        return PropId_EffectBlurRadius;
    }
    if (s.find("ShadowDepth") != std::string_view::npos) {
        return PropId_EffectShadowDepth;
    }

    const bool mentionsRenderTransform =
        (s.find("RenderTransform") != std::string_view::npos) ||
        (s.find("ScaleTransform") != std::string_view::npos) ||
        (s.find("RotateTransform") != std::string_view::npos) ||
        (s.find("TranslateTransform") != std::string_view::npos);

    if (s.find("ScaleX") != std::string_view::npos) return PropId_AnimScaleX;
    if (s.find("ScaleY") != std::string_view::npos) return PropId_AnimScaleY;
    if (s.find("SkewX") != std::string_view::npos || s.find("AngleX") != std::string_view::npos) {
        return PropId_AnimSkewX;
    }
    if (s.find("SkewY") != std::string_view::npos || s.find("AngleY") != std::string_view::npos) {
        return PropId_AnimSkewY;
    }
    if (s.find("Angle") != std::string_view::npos) return PropId_AnimRotateAngle;
    if (s.find("TranslateX") != std::string_view::npos) return PropId_AnimTranslateX;
    if (s.find("TranslateY") != std::string_view::npos) return PropId_AnimTranslateY;
    if (mentionsRenderTransform) {
        if (s.find(".X)") != std::string_view::npos || s.rfind(".X") == s.size() - 2u) {
            return PropId_AnimTranslateX;
        }
        if (s.find(".Y)") != std::string_view::npos || s.rfind(".Y") == s.size() - 2u) {
            return PropId_AnimTranslateY;
        }
    }
    if (s.find("Background") != std::string_view::npos || s.find("Fill") != std::string_view::npos) {
        return PropId_Background;
    }
    if (s.find("Foreground") != std::string_view::npos) {
        return PropId_Foreground;
    }
    if (s.find("BorderBrush") != std::string_view::npos || s.find("Stroke") != std::string_view::npos) {
        return PropId_BorderBrush;
    }
    // Strip parens and take last leaf token e.g. "(UIElement.Opacity)" -> "Opacity"
    while (!s.empty() && (s.back() == ')' || std::isspace(static_cast<unsigned char>(s.back())))) {
        s.remove_suffix(1);
    }
    if (const auto lastDot = s.rfind('.'); lastDot != std::string_view::npos) {
        s = s.substr(lastDot + 1);
    }
    while (!s.empty() && s.front() == '(') {
        s.remove_prefix(1);
    }
    return ResolveBuiltinPropertyId(s);
}

float ParseXamlTimeSpanSeconds(std::string_view rawTimeSpan, float fallbackSeconds) noexcept {
    std::string_view s = TrimWs(rawTimeSpan);
    if (s.empty() || EqualsIgnoreCase(s, "Automatic") || EqualsIgnoreCase(s, "Forever")) {
        return fallbackSeconds;
    }
    if (s.size() > 2u && EqualsIgnoreCase(s.substr(s.size() - 2u), "ms")) {
        return std::max(0.0f, ParseFloat32Fast(s.substr(0, s.size() - 2u), fallbackSeconds * 1000.0f) * 0.001f);
    }
    if (s.size() > 1u && (s.back() == 's' || s.back() == 'S')) {
        return std::max(0.0f, ParseFloat32Fast(s.substr(0, s.size() - 1u), fallbackSeconds));
    }
    // Format H:M:S.fff (or D.H:M:S.fff)
    const std::size_t firstColon = s.find(':');
    if (firstColon == std::string_view::npos) {
        return std::max(0.0f, ParseFloat32Fast(s, fallbackSeconds));
    }
    const std::size_t secondColon = s.find(':', firstColon + 1u);
    if (secondColon == std::string_view::npos) {
        const float mins = ParseFloat32Fast(s.substr(0, firstColon), 0.0f);
        const float secs = ParseFloat32Fast(s.substr(firstColon + 1u), 0.0f);
        return std::max(0.0f, mins * 60.0f + secs);
    }
    std::string_view hPart = s.substr(0, firstColon);
    float days = 0.0f;
    if (const std::size_t dayDot = hPart.find('.'); dayDot != std::string_view::npos) {
        days = ParseFloat32Fast(hPart.substr(0, dayDot), 0.0f);
        hPart = hPart.substr(dayDot + 1u);
    }
    const float hours = ParseFloat32Fast(hPart, 0.0f);
    const float mins = ParseFloat32Fast(s.substr(firstColon + 1u, secondColon - firstColon - 1u), 0.0f);
    const float secs = ParseFloat32Fast(s.substr(secondColon + 1u), 0.0f);
    return std::max(0.0f, ((days * 24.0f + hours) * 60.0f + mins) * 60.0f + secs);
}

EasingFunctionKind ParseEasingFunctionKind(std::string_view tagName) noexcept {
    tagName = TrimWs(tagName);
    if (const auto colon = tagName.rfind(':'); colon != std::string_view::npos) {
        tagName = tagName.substr(colon + 1u);
    }
    if (tagName.find("Quadratic") != std::string_view::npos) return EasingFunctionKind::Quadratic;
    if (tagName.find("Cubic") != std::string_view::npos) return EasingFunctionKind::Cubic;
    if (tagName.find("Quartic") != std::string_view::npos) return EasingFunctionKind::Quartic;
    if (tagName.find("Quintic") != std::string_view::npos) return EasingFunctionKind::Quintic;
    if (tagName.find("Sine") != std::string_view::npos) return EasingFunctionKind::Sine;
    if (tagName.find("Circle") != std::string_view::npos) return EasingFunctionKind::Circle;
    if (tagName.find("Exponential") != std::string_view::npos) return EasingFunctionKind::Exponential;
    if (tagName.find("Back") != std::string_view::npos) return EasingFunctionKind::Back;
    if (tagName.find("Bounce") != std::string_view::npos) return EasingFunctionKind::Bounce;
    if (tagName.find("Elastic") != std::string_view::npos) return EasingFunctionKind::Elastic;
    return EasingFunctionKind::Linear;
}

EasingMode ParseEasingMode(std::string_view rawMode) noexcept {
    rawMode = TrimWs(rawMode);
    if (EqualsIgnoreCase(rawMode, "EaseIn") || rawMode == "1") return EasingMode::EaseIn;
    if (EqualsIgnoreCase(rawMode, "EaseInOut") || rawMode == "2") return EasingMode::EaseInOut;
    return EasingMode::EaseOut;
}

ValueConverterKind ParseValueConverterKind(std::string_view rawConverterKey) noexcept {
    std::string_view k = TrimWs(rawConverterKey);
    if (k.empty()) {
        return ValueConverterKind::None;
    }
    if (const auto colon = k.rfind(':'); colon != std::string_view::npos) {
        k = k.substr(colon + 1u);
    }
    std::string lower;
    lower.reserve(k.size());
    for (char ch : k) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    const bool isInv = (lower.find("inv") != std::string::npos) || (lower.find("not") != std::string::npos);
    if (lower.find("null") != std::string::npos) {
        if (lower.find("vis") != std::string::npos) {
            return ValueConverterKind::NullToCollapsed;
        }
        return ValueConverterKind::NullToBoolean;
    }
    if (lower.find("vis") != std::string::npos) {
        return isInv
            ? ValueConverterKind::InverseBooleanToVisibility
            : ValueConverterKind::BooleanToVisibility;
    }
    if (isInv && lower.find("bool") != std::string::npos) {
        return ValueConverterKind::InverseBoolean;
    }
    if (lower.find("mathconverter") != std::string::npos || lower == "math") {
        return ValueConverterKind::MathExpression;
    }
    if (lower == "none") {
        return ValueConverterKind::None;
    }
    return ValueConverterKind::CustomScriptOrHost;
}

TriggerComparisonOp ParseTriggerComparisonOp(std::string_view rawOp) noexcept {
    const std::string_view s = TrimWs(rawOp);
    if (s.empty() || EqualsIgnoreCase(s, "Equal") || s == "==" || EqualsIgnoreCase(s, "Eq")) {
        return TriggerComparisonOp::Equal;
    }
    if (EqualsIgnoreCase(s, "NotEqual") || s == "!=" || s == "<>" || EqualsIgnoreCase(s, "Ne")) {
        return TriggerComparisonOp::NotEqual;
    }
    if (EqualsIgnoreCase(s, "LessThan") || s == "<" || EqualsIgnoreCase(s, "Lt")) {
        return TriggerComparisonOp::LessThan;
    }
    if (EqualsIgnoreCase(s, "LessThanOrEqual") || s == "<=" || EqualsIgnoreCase(s, "Le")) {
        return TriggerComparisonOp::LessThanOrEqual;
    }
    if (EqualsIgnoreCase(s, "GreaterThan") || s == ">" || EqualsIgnoreCase(s, "Gt")) {
        return TriggerComparisonOp::GreaterThan;
    }
    if (EqualsIgnoreCase(s, "GreaterThanOrEqual") || s == ">=" || EqualsIgnoreCase(s, "Ge")) {
        return TriggerComparisonOp::GreaterThanOrEqual;
    }
    return TriggerComparisonOp::Equal;
}

ControlStoryboardOption ParseControlStoryboardOption(std::string_view rawOpt) noexcept {
    const std::string_view s = TrimWs(rawOpt);
    if (EqualsIgnoreCase(s, "Stop")) {
        return ControlStoryboardOption::Stop;
    }
    if (EqualsIgnoreCase(s, "TogglePlayPause") ||
        EqualsIgnoreCase(s, "Pause") ||
        EqualsIgnoreCase(s, "Resume")) {
        return ControlStoryboardOption::TogglePlayPause;
    }
    return ControlStoryboardOption::Play;
}

UnifiedValue ApplyBuiltinValueConverter(
    ValueConverterKind kind,
    const UnifiedValue& rawVal,
    bool inverseForTwoWay) noexcept {
    if (kind == ValueConverterKind::None) {
        return rawVal;
    }
    auto isNullLike = [](const UnifiedValue& v) noexcept -> bool {
        if (std::holds_alternative<std::monostate>(v) || std::holds_alternative<NullObject>(v)) {
            return true;
        }
        if (const auto* ref = std::get_if<ObjectRef>(&v)) {
            return !ref->valid();
        }
        return false;
    };
    auto isTruthy = [](const UnifiedValue& v) noexcept -> bool {
        if (const auto* b = std::get_if<bool>(&v)) return *b;
        if (const auto* i = std::get_if<std::int64_t>(&v)) return *i != 0;
        if (const auto* f = std::get_if<FloatValue>(&v)) return f->value != 0.0f;
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            return EqualsIgnoreCase(sv->view(), "true") || sv->view() == "1";
        }
        if (const auto* ref = std::get_if<ObjectRef>(&v)) {
            return ref->valid();
        }
        return false;
    };

    if (inverseForTwoWay) {
        switch (kind) {
            case ValueConverterKind::BooleanToVisibility:
                if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
                    return UnifiedValue{*iv == 0};
                }
                return UnifiedValue{isTruthy(rawVal)};
            case ValueConverterKind::InverseBooleanToVisibility:
                if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
                    return UnifiedValue{*iv != 0};
                }
                return UnifiedValue{!isTruthy(rawVal)};
            case ValueConverterKind::InverseBoolean:
                return UnifiedValue{!isTruthy(rawVal)};
            default:
                return rawVal;
        }
    }

    switch (kind) {
        case ValueConverterKind::BooleanToVisibility:
            return isTruthy(rawVal) ? value::FromInt(0) : value::FromInt(2);
        case ValueConverterKind::InverseBooleanToVisibility:
            return isTruthy(rawVal) ? value::FromInt(2) : value::FromInt(0);
        case ValueConverterKind::InverseBoolean:
            return UnifiedValue{!isTruthy(rawVal)};
        case ValueConverterKind::NullToCollapsed:
            return isNullLike(rawVal) ? value::FromInt(2) : value::FromInt(0);
        case ValueConverterKind::NullToBoolean:
            return UnifiedValue{!isNullLike(rawVal)};
        case ValueConverterKind::None:
        default:
            return rawVal;
    }
}

namespace {

[[nodiscard]] float CoerceUnifiedValueToFloat32(const UnifiedValue& v) noexcept {
    if (const auto* f = std::get_if<FloatValue>(&v)) {
        return std::isfinite(f->value) ? f->value : 0.0f;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<float>(*i);
    }
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b ? 1.0f : 0.0f;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        const std::string_view s = TrimWs(sv->view());
        if (EqualsIgnoreCase(s, "true")) return 1.0f;
        if (EqualsIgnoreCase(s, "false")) return 0.0f;
        return ParseFloat32Fast(s, 0.0f);
    }
    return 0.0f;
}

struct MathExprParser {
    std::string_view expr{};
    std::size_t pos = 0u;
    const UnifiedValue* args = nullptr;
    std::size_t argCount = 0u;
    float paramVal = 0.0f;
    bool ok = true;

    void SkipWs() noexcept {
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
    }

    float ReadArg(std::size_t idx) const noexcept {
        if (args && idx < argCount) {
            return CoerceUnifiedValueToFloat32(args[idx]);
        }
        return 0.0f;
    }

    float ParseTernary(int depth) noexcept {
        if (!ok || depth > 32) {
            ok = false;
            return 0.0f;
        }
        const float cond = ParseLogicalOr(depth + 1);
        SkipWs();
        if (pos < expr.size() && expr[pos] == '?') {
            ++pos;
            const float trueBranch = ParseTernary(depth + 1);
            SkipWs();
            if (pos >= expr.size() || expr[pos] != ':') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            const float falseBranch = ParseTernary(depth + 1);
            return (std::fabs(cond) > 1e-6f) ? trueBranch : falseBranch;
        }
        return cond;
    }

    float ParseLogicalOr(int depth) noexcept {
        float lhs = ParseLogicalAnd(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '|' && expr[pos + 1u] == '|') {
                pos += 2u;
                const float rhs = ParseLogicalAnd(depth + 1);
                lhs = (std::fabs(lhs) > 1e-6f || std::fabs(rhs) > 1e-6f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseLogicalAnd(int depth) noexcept {
        float lhs = ParseEquality(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '&' && expr[pos + 1u] == '&') {
                pos += 2u;
                const float rhs = ParseEquality(depth + 1);
                lhs = (std::fabs(lhs) > 1e-6f && std::fabs(rhs) > 1e-6f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseEquality(int depth) noexcept {
        float lhs = ParseRelational(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '=' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseRelational(depth + 1);
                lhs = (std::fabs(lhs - rhs) <= 1e-5f) ? 1.0f : 0.0f;
            } else if (pos + 1u < expr.size() && expr[pos] == '!' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseRelational(depth + 1);
                lhs = (std::fabs(lhs - rhs) > 1e-5f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseRelational(int depth) noexcept {
        float lhs = ParseAdditive(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '<' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs <= rhs + 1e-6f) ? 1.0f : 0.0f;
            } else if (pos + 1u < expr.size() && expr[pos] == '>' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs + 1e-6f >= rhs) ? 1.0f : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '<') {
                ++pos;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs < rhs) ? 1.0f : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '>') {
                ++pos;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs > rhs) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseAdditive(int depth) noexcept {
        float lhs = ParseMultiplicative(depth + 1);
        while (ok) {
            SkipWs();
            if (pos < expr.size() && expr[pos] == '+') {
                ++pos;
                lhs += ParseMultiplicative(depth + 1);
            } else if (pos < expr.size() && expr[pos] == '-') {
                ++pos;
                lhs -= ParseMultiplicative(depth + 1);
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseMultiplicative(int depth) noexcept {
        float lhs = ParseUnary(depth + 1);
        while (ok) {
            SkipWs();
            if (pos < expr.size() && expr[pos] == '*') {
                ++pos;
                lhs *= ParseUnary(depth + 1);
            } else if (pos < expr.size() && expr[pos] == '/') {
                ++pos;
                const float rhs = ParseUnary(depth + 1);
                lhs = (std::fabs(rhs) > 1e-12f) ? (lhs / rhs) : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '%') {
                ++pos;
                const float rhs = ParseUnary(depth + 1);
                lhs = (std::fabs(rhs) > 1e-12f) ? std::fmod(lhs, rhs) : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseUnary(int depth) noexcept {
        if (!ok || depth > 36) {
            ok = false;
            return 0.0f;
        }
        SkipWs();
        if (pos < expr.size()) {
            if (expr[pos] == '+') {
                ++pos;
                return ParseUnary(depth + 1);
            }
            if (expr[pos] == '-') {
                ++pos;
                return -ParseUnary(depth + 1);
            }
            if (expr[pos] == '!') {
                ++pos;
                const float v = ParseUnary(depth + 1);
                return (std::fabs(v) <= 1e-6f) ? 1.0f : 0.0f;
            }
        }
        return ParsePrimary(depth + 1);
    }

    float ParsePrimary(int depth) noexcept {
        SkipWs();
        if (pos >= expr.size()) {
            ok = false;
            return 0.0f;
        }

        // 1. Parenthesized sub-expression
        if (expr[pos] == '(') {
            ++pos;
            const float val = ParseTernary(depth + 1);
            SkipWs();
            if (pos >= expr.size() || expr[pos] != ')') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            return val;
        }

        // 2. Indexed placeholder {0}, {1}, ...
        if (expr[pos] == '{') {
            ++pos;
            SkipWs();
            std::size_t idx = 0u;
            bool anyDigit = false;
            while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
                anyDigit = true;
                idx = idx * 10u + static_cast<std::size_t>(expr[pos] - '0');
                ++pos;
            }
            SkipWs();
            // Skip optional format suffix inside {0:...} if any
            if (pos < expr.size() && expr[pos] == ':') {
                while (pos < expr.size() && expr[pos] != '}') {
                    ++pos;
                }
            }
            if (!anyDigit || pos >= expr.size() || expr[pos] != '}') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            return ReadArg(idx);
        }

        // 3. @VALUE / @PARAMETER variables
        if (expr[pos] == '@') {
            ++pos;
            const std::size_t idStart = pos;
            while (pos < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            const std::string_view name = expr.substr(idStart, pos - idStart);
            if (EqualsIgnoreCase(name, "PARAMETER") || EqualsIgnoreCase(name, "PARAM")) {
                return paramVal;
            }
            return ReadArg(0u);
        }

        // 4. Numeric literal (32-bit float only)
        if (std::isdigit(static_cast<unsigned char>(expr[pos])) || expr[pos] == '.') {
            const std::size_t start = pos;
            bool seenDot = false;
            bool seenDigit = false;
            while (pos < expr.size()) {
                const char c = expr[pos];
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    seenDigit = true;
                    ++pos;
                } else if (c == '.' && !seenDot) {
                    seenDot = true;
                    ++pos;
                } else if ((c == 'e' || c == 'E') && seenDigit) {
                    ++pos;
                    if (pos < expr.size() && (expr[pos] == '+' || expr[pos] == '-')) {
                        ++pos;
                    }
                } else {
                    break;
                }
            }
            if (pos < expr.size() && (expr[pos] == 'f' || expr[pos] == 'F')) {
                ++pos;
            }
            if (!seenDigit) {
                ok = false;
                return 0.0f;
            }
            std::string_view numSlice = expr.substr(start, pos - start);
            if (!numSlice.empty() && (numSlice.back() == 'f' || numSlice.back() == 'F')) {
                numSlice.remove_suffix(1u);
            }
            return ParseFloat32Fast(numSlice, 0.0f);
        }

        // 5. Identifier (function call or named constant/variable)
        if (std::isalpha(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_') {
            const std::size_t idStart = pos;
            while (pos < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            const std::string_view ident = expr.substr(idStart, pos - idStart);
            SkipWs();

            if (pos < expr.size() && expr[pos] == '(') {
                ++pos;
                float fnArgs[16]{};
                std::size_t fnArgCount = 0u;
                SkipWs();
                if (pos < expr.size() && expr[pos] != ')') {
                    while (ok) {
                        const float v = ParseTernary(depth + 1);
                        if (fnArgCount < 16u) {
                            fnArgs[fnArgCount++] = v;
                        }
                        SkipWs();
                        if (pos < expr.size() && expr[pos] == ',') {
                            ++pos;
                            continue;
                        }
                        break;
                    }
                }
                if (pos >= expr.size() || expr[pos] != ')') {
                    ok = false;
                    return 0.0f;
                }
                ++pos;

                if (EqualsIgnoreCase(ident, "min")) {
                    if (fnArgCount == 0u) return 0.0f;
                    float m = fnArgs[0];
                    for (std::size_t i = 1u; i < fnArgCount; ++i) m = std::min(m, fnArgs[i]);
                    return m;
                }
                if (EqualsIgnoreCase(ident, "max")) {
                    if (fnArgCount == 0u) return 0.0f;
                    float m = fnArgs[0];
                    for (std::size_t i = 1u; i < fnArgCount; ++i) m = std::max(m, fnArgs[i]);
                    return m;
                }
                if (EqualsIgnoreCase(ident, "abs")) {
                    return (fnArgCount >= 1u) ? std::fabs(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "clamp")) {
                    if (fnArgCount >= 3u) {
                        const float lo = std::min(fnArgs[1], fnArgs[2]);
                        const float hi = std::max(fnArgs[1], fnArgs[2]);
                        return std::clamp(fnArgs[0], lo, hi);
                    }
                    return (fnArgCount >= 1u) ? fnArgs[0] : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "floor")) {
                    return (fnArgCount >= 1u) ? std::floor(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "ceil")) {
                    return (fnArgCount >= 1u) ? std::ceil(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "round")) {
                    return (fnArgCount >= 1u) ? std::round(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "trunc")) {
                    return (fnArgCount >= 1u) ? std::trunc(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "pow")) {
                    if (fnArgCount >= 2u) {
                        if (fnArgs[0] < 0.0f && std::floor(fnArgs[1]) != fnArgs[1]) {
                            return 0.0f;
                        }
                        const float r = std::pow(fnArgs[0], fnArgs[1]);
                        return std::isfinite(r) ? r : 0.0f;
                    }
                    return 0.0f;
                }
                if (EqualsIgnoreCase(ident, "sqrt")) {
                    return (fnArgCount >= 1u && fnArgs[0] > 0.0f) ? std::sqrt(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "sin")) {
                    return (fnArgCount >= 1u) ? std::sin(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "cos")) {
                    return (fnArgCount >= 1u) ? std::cos(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "tan")) {
                    return (fnArgCount >= 1u) ? std::tan(fnArgs[0]) : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "lerp")) {
                    return (fnArgCount >= 3u)
                        ? (fnArgs[0] + (fnArgs[1] - fnArgs[0]) * fnArgs[2])
                        : 0.0f;
                }
                if (EqualsIgnoreCase(ident, "sign")) {
                    if (fnArgCount == 0u) return 0.0f;
                    return (fnArgs[0] > 0.0f) ? 1.0f : ((fnArgs[0] < 0.0f) ? -1.0f : 0.0f);
                }
                ok = false;
                return 0.0f;
            }

            if (EqualsIgnoreCase(ident, "VALUE") || ident == "x" || ident == "X") {
                return ReadArg(0u);
            }
            if (EqualsIgnoreCase(ident, "PARAMETER") || EqualsIgnoreCase(ident, "PARAM")) {
                return paramVal;
            }
            if (EqualsIgnoreCase(ident, "PI")) {
                return 3.14159265f;
            }
            if (ident == "E") {
                return 2.71828183f;
            }
            if (EqualsIgnoreCase(ident, "true")) {
                return 1.0f;
            }
            if (EqualsIgnoreCase(ident, "false")) {
                return 0.0f;
            }
        }

        ok = false;
        return 0.0f;
    }
};

void AppendUint64Padded(std::string& out, std::uint64_t val, std::size_t minDigits) {
    char buf[32]{};
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec != std::errc{}) {
        out.append(std::max<std::size_t>(1u, minDigits), '0');
        return;
    }
    const std::size_t len = static_cast<std::size_t>(ptr - buf);
    if (len < minDigits) {
        out.append(minDigits - len, '0');
    }
    out.append(buf, len);
}

void AppendFixedFloat32(
    std::string& out,
    float numVal,
    std::size_t minIntDigits,
    std::size_t minFracDigits,
    std::size_t maxFracDigits) {
    if (!std::isfinite(numVal)) {
        out += "0";
        return;
    }
    const std::size_t clampedMaxFrac = std::min<std::size_t>(maxFracDigits, 6u);
    const std::size_t clampedMinFrac = std::min<std::size_t>(minFracDigits, clampedMaxFrac);

    std::uint64_t scale = 1u;
    for (std::size_t i = 0u; i < clampedMaxFrac; ++i) {
        scale *= 10u;
    }

    bool neg = (numVal < 0.0f);
    const float absVal = std::fabs(numVal);
    const auto scaled = static_cast<std::uint64_t>(
        std::llround(absVal * static_cast<float>(scale)));
    if (scaled == 0u) {
        neg = false;
    }

    const std::uint64_t whole = scaled / scale;
    std::uint64_t frac = scaled % scale;

    if (neg) {
        out.push_back('-');
    }
    if (whole == 0u && minIntDigits == 0u && clampedMaxFrac == 0u) {
        out.push_back('0');
        return;
    }
    AppendUint64Padded(out, whole, std::max<std::size_t>(1u, minIntDigits));

    if (clampedMaxFrac > 0u) {
        char fracBuf[16]{};
        for (std::size_t idx = clampedMaxFrac; idx > 0u; --idx) {
            fracBuf[idx - 1u] = static_cast<char>('0' + (frac % 10u));
            frac /= 10u;
        }
        std::size_t actualFracLen = clampedMaxFrac;
        while (actualFracLen > clampedMinFrac && fracBuf[actualFracLen - 1u] == '0') {
            --actualFracLen;
        }
        if (actualFracLen > 0u) {
            out.push_back('.');
            out.append(fracBuf, actualFracLen);
        }
    }
}

std::string FormatSingleBindingArgument(
    const UnifiedValue& rawVal,
    std::string_view spec,
    const ExecutionDomainContext* domains) {
    spec = TrimWs(spec);

    // Unwrap RealScript heap String ObjectRef when available
    std::string_view strView{};
    bool hasString = false;
    if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
        strView = sv->view();
        hasString = true;
    } else if (const auto* objRef = std::get_if<ObjectRef>(&rawVal)) {
        if (objRef->kind == ObjectKind::String && domains) {
            if (const auto resolvedSv = domains->stringViewAcrossDomains(*objRef)) {
                strView = *resolvedSv;
                hasString = true;
            }
        }
    }

    if (std::holds_alternative<std::monostate>(rawVal) ||
        std::holds_alternative<NullObject>(rawVal)) {
        return {};
    }

    if (spec.empty()) {
        if (hasString) {
            return std::string(strView);
        }
        if (const auto* b = std::get_if<bool>(&rawVal)) {
            return *b ? "true" : "false";
        }
        if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
            std::string s;
            if (*iv < 0) {
                s.push_back('-');
                const auto mag = static_cast<std::uint64_t>(-(*iv + 1)) + 1u;
                AppendUint64Padded(s, mag, 1u);
            } else {
                AppendUint64Padded(s, static_cast<std::uint64_t>(*iv), 1u);
            }
            return s;
        }
        if (const auto* fv = std::get_if<FloatValue>(&rawVal)) {
            std::string s;
            AppendFixedFloat32(s, fv->value, 1u, 0u, 4u);
            return s;
        }
        return rs_rt::valueToString(rawVal);
    }

    // Try extracting numeric value (including numeric strings if a numeric specifier was supplied)
    bool hasNum = false;
    float numVal = 0.0f;
    if (const auto* fv = std::get_if<FloatValue>(&rawVal)) {
        numVal = fv->value;
        hasNum = true;
    } else if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
        numVal = static_cast<float>(*iv);
        hasNum = true;
    } else if (const auto* b = std::get_if<bool>(&rawVal)) {
        numVal = *b ? 1.0f : 0.0f;
        hasNum = true;
    } else if (hasString && !strView.empty()) {
        const std::string_view trimmed = TrimWs(strView);
        char stackBuf[64]{};
        if (trimmed.size() < sizeof(stackBuf)) {
            for (std::size_t i = 0u; i < trimmed.size(); ++i) stackBuf[i] = trimmed[i];
            char* endPtr = nullptr;
            const float parsed = std::strtof(stackBuf, &endPtr);
            if (endPtr == stackBuf + trimmed.size()) {
                numVal = parsed;
                hasNum = true;
            }
        }
    }

    if (!hasNum) {
        return hasString ? std::string(strView) : rs_rt::valueToString(rawVal);
    }

    // 1. Standard single-letter format specifiers: F/f, N/n, P/p, D/d, C/c, G/g
    const char lead = spec.front();
    const char leadUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(lead)));
    if (leadUpper == 'F' || leadUpper == 'N' || leadUpper == 'P' ||
        leadUpper == 'D' || leadUpper == 'C' || leadUpper == 'G') {
        bool digitsOk = true;
        std::size_t prec = 0u;
        for (std::size_t i = 1u; i < spec.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(spec[i]))) {
                digitsOk = false;
                break;
            }
            prec = prec * 10u + static_cast<std::size_t>(spec[i] - '0');
        }
        if (digitsOk) {
            const bool hasExplicitPrec = (spec.size() > 1u);
            std::string out;
            if (leadUpper == 'G') {
                AppendFixedFloat32(out, numVal, 1u, 0u, hasExplicitPrec ? prec : 4u);
                return out;
            }
            if (leadUpper == 'D') {
                const std::size_t pad = hasExplicitPrec ? std::min<std::size_t>(prec, 16u) : 1u;
                const auto iv = std::holds_alternative<std::int64_t>(rawVal)
                    ? std::get<std::int64_t>(rawVal)
                    : static_cast<std::int64_t>(std::llround(numVal));
                if (iv < 0) {
                    out.push_back('-');
                    const auto mag = static_cast<std::uint64_t>(-(iv + 1)) + 1u;
                    AppendUint64Padded(out, mag, pad);
                } else {
                    AppendUint64Padded(out, static_cast<std::uint64_t>(iv), pad);
                }
                return out;
            }
            if (leadUpper == 'P') {
                const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
                AppendFixedFloat32(out, numVal * 100.0f, 1u, p, p);
                out.push_back('%');
                return out;
            }
            if (leadUpper == 'C') {
                const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
                if (numVal < 0.0f) {
                    out += "-$";
                    AppendFixedFloat32(out, std::fabs(numVal), 1u, p, p);
                } else {
                    out.push_back('$');
                    AppendFixedFloat32(out, numVal, 1u, p, p);
                }
                return out;
            }
            // 'F' or 'N'
            const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
            AppendFixedFloat32(out, numVal, 1u, p, p);
            return out;
        }
    }

    // 2. Custom numeric pattern: '0', '#', ',', '.', '%' (e.g. "0", "00", "000", "0.0", "#,.##")
    bool isCustomPattern = false;
    for (char c : spec) {
        if (c == '0' || c == '#') {
            isCustomPattern = true;
            break;
        }
    }
    if (isCustomPattern) {
        bool hasPercent = false;
        for (char c : spec) {
            if (c == '%') {
                hasPercent = true;
                break;
            }
        }
        float scaledVal = hasPercent ? (numVal * 100.0f) : numVal;
        const std::size_t dotPos = spec.find('.');
        const std::string_view intPart = (dotPos == std::string_view::npos)
            ? spec
            : spec.substr(0u, dotPos);
        const std::string_view fracPart = (dotPos == std::string_view::npos)
            ? std::string_view{}
            : spec.substr(dotPos + 1u);

        // Count trailing scaling commas immediately preceding '.' (or end of intPart, e.g. "#,.##" -> /1000)
        std::size_t endInt = intPart.size();
        while (endInt > 0u && intPart[endInt - 1u] == '%') {
            --endInt;
        }
        while (endInt > 0u && intPart[endInt - 1u] == ',') {
            scaledVal *= 0.001f;
            --endInt;
        }

        std::size_t minIntZeros = 0u;
        for (std::size_t i = 0u; i < endInt; ++i) {
            if (intPart[i] == '0') {
                ++minIntZeros;
            }
        }
        std::size_t minFracZeros = 0u;
        std::size_t maxFracDigits = 0u;
        for (char c : fracPart) {
            if (c == '0') {
                ++minFracZeros;
                ++maxFracDigits;
            } else if (c == '#') {
                ++maxFracDigits;
            }
        }

        std::string out;
        AppendFixedFloat32(
            out,
            scaledVal,
            std::max<std::size_t>(1u, minIntZeros),
            minFracZeros,
            maxFracDigits);
        if (hasPercent) {
            out.push_back('%');
        }
        return out;
    }

    return hasString ? std::string(strView) : rs_rt::valueToString(rawVal);
}

} // namespace

bool EvaluateMathConverterExpression(
    std::string_view expression,
    const UnifiedValue* args,
    std::size_t argCount,
    float parameterValue,
    float& outResult) noexcept {
    std::string_view s = TrimWs(expression);
    if (s.size() >= 2u && s[0] == '{' && s[1] == '}') {
        s = TrimWs(s.substr(2u));
    }
    if (s.empty()) {
        outResult = (args && argCount > 0u) ? CoerceUnifiedValueToFloat32(args[0]) : 0.0f;
        return false;
    }
    MathExprParser parser{s, 0u, args, argCount, parameterValue, true};
    const float value = parser.ParseTernary(0);
    parser.SkipWs();
    if (!parser.ok || parser.pos != s.size() || !std::isfinite(value)) {
        outResult = 0.0f;
        return false;
    }
    outResult = value;
    return true;
}

std::string FormatBindingString(
    std::string_view formatSpec,
    const UnifiedValue* args,
    std::size_t argCount,
    const ExecutionDomainContext* domains) {
    std::string_view fmt = formatSpec;
    const std::string_view trimmed = TrimWs(fmt);
    if (trimmed.size() >= 2u && trimmed.front() == '\'' && trimmed.back() == '\'') {
        fmt = trimmed.substr(1u, trimmed.size() - 2u);
    }
    if (fmt.size() >= 2u && fmt[0] == '{' && fmt[1] == '}') {
        fmt.remove_prefix(2u);
    }
    if (fmt.empty()) {
        if (args && argCount > 0u) {
            return FormatSingleBindingArgument(args[0], {}, domains);
        }
        return {};
    }

    // Standalone format specifier without '{' (e.g. StringFormat=F1, StringFormat=000, StringFormat=P0)
    if (fmt.find('{') == std::string_view::npos && fmt.find('}') == std::string_view::npos) {
        if (args && argCount > 0u) {
            return FormatSingleBindingArgument(args[0], fmt, domains);
        }
        return std::string(fmt);
    }

    std::string out;
    out.reserve(fmt.size() + 16u);
    std::size_t i = 0u;
    while (i < fmt.size()) {
        if (fmt[i] == '{') {
            if (i + 1u < fmt.size() && fmt[i + 1u] == '{') {
                out.push_back('{');
                i += 2u;
                continue;
            }
            const std::size_t closePos = fmt.find('}', i + 1u);
            if (closePos == std::string_view::npos) {
                out.push_back(fmt[i++]);
                continue;
            }
            const std::string_view token = fmt.substr(i + 1u, closePos - (i + 1u));
            std::size_t p = 0u;
            while (p < token.size() && std::isspace(static_cast<unsigned char>(token[p]))) {
                ++p;
            }
            std::size_t argIdx = 0u;
            bool hasDigits = false;
            while (p < token.size() && std::isdigit(static_cast<unsigned char>(token[p]))) {
                hasDigits = true;
                argIdx = argIdx * 10u + static_cast<std::size_t>(token[p] - '0');
                ++p;
            }
            if (!hasDigits) {
                out.append(fmt.substr(i, closePos - i + 1u));
                i = closePos + 1u;
                continue;
            }
            int alignWidth = 0;
            bool hasAlign = false;
            if (p < token.size() && token[p] == ',') {
                ++p;
                bool negAlign = false;
                while (p < token.size() && std::isspace(static_cast<unsigned char>(token[p]))) ++p;
                if (p < token.size() && token[p] == '-') {
                    negAlign = true;
                    ++p;
                }
                int w = 0;
                while (p < token.size() && std::isdigit(static_cast<unsigned char>(token[p]))) {
                    w = w * 10 + (token[p] - '0');
                    hasAlign = true;
                    ++p;
                }
                alignWidth = negAlign ? -w : w;
            }
            std::string_view itemSpec{};
            if (p < token.size() && token[p] == ':') {
                itemSpec = token.substr(p + 1u);
            }
            std::string formattedItem;
            if (args && argIdx < argCount) {
                formattedItem = FormatSingleBindingArgument(args[argIdx], itemSpec, domains);
            }
            if (hasAlign) {
                const std::size_t absW = static_cast<std::size_t>(std::abs(alignWidth));
                if (formattedItem.size() < absW) {
                    const std::size_t pad = absW - formattedItem.size();
                    if (alignWidth < 0) {
                        out += formattedItem;
                        out.append(pad, ' ');
                    } else {
                        out.append(pad, ' ');
                        out += formattedItem;
                    }
                } else {
                    out += formattedItem;
                }
            } else {
                out += formattedItem;
            }
            i = closePos + 1u;
            continue;
        }
        if (fmt[i] == '}' && i + 1u < fmt.size() && fmt[i + 1u] == '}') {
            out.push_back('}');
            i += 2u;
            continue;
        }
        out.push_back(fmt[i++]);
    }
    return out;
}

RoutedTriggerEvent ParseRoutedTriggerEvent(
    std::string_view rawRoutedEvent,
    bool& outRecognized) noexcept {
    outRecognized = false;
    std::string_view s = TrimWs(rawRoutedEvent);
    if (s.empty()) {
        outRecognized = true;
        return RoutedTriggerEvent::Loaded;
    }
    if (const auto dot = s.rfind('.'); dot != std::string_view::npos) {
        s = s.substr(dot + 1u);
    }
    if (EqualsIgnoreCase(s, "Loaded")) {
        outRecognized = true;
        return RoutedTriggerEvent::Loaded;
    }
    if (EqualsIgnoreCase(s, "Click")) {
        outRecognized = true;
        return RoutedTriggerEvent::Click;
    }
    if (EqualsIgnoreCase(s, "MouseEnter")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseEnter;
    }
    if (EqualsIgnoreCase(s, "MouseLeave")) {
        outRecognized = true;
        return RoutedTriggerEvent::MouseLeave;
    }
    if (EqualsIgnoreCase(s, "Checked")) {
        outRecognized = true;
        return RoutedTriggerEvent::Checked;
    }
    if (EqualsIgnoreCase(s, "Unchecked")) {
        outRecognized = true;
        return RoutedTriggerEvent::Unchecked;
    }
    if (EqualsIgnoreCase(s, "SelectionChanged")) {
        outRecognized = true;
        return RoutedTriggerEvent::SelectionChanged;
    }
    if (EqualsIgnoreCase(s, "ValueChanged")) {
        outRecognized = true;
        return RoutedTriggerEvent::ValueChanged;
    }
    return RoutedTriggerEvent::Loaded;
}

UnifiedValue ParseBuiltinLiteralPropertyValue(
    std::uint16_t propId,
    std::string_view rawValue) {
    rawValue = TrimWs(rawValue);
    switch (propId) {
        case PropId_Width:
        case PropId_Height:
        case PropId_ItemWidth:
        case PropId_ItemHeight:
            if (EqualsIgnoreCase(rawValue, "Auto") || EqualsIgnoreCase(rawValue, "NaN")) {
                return MakeFloat(-1.0f);
            }
            return MakeFloat(ParseFloat32Fast(rawValue, -1.0f));

        case PropId_MaxWidth:
        case PropId_MaxHeight:
            if (EqualsIgnoreCase(rawValue, "Infinity") ||
                EqualsIgnoreCase(rawValue, "+Infinity") ||
                EqualsIgnoreCase(rawValue, "Auto") ||
                EqualsIgnoreCase(rawValue, "NaN")) {
                return MakeFloat(std::numeric_limits<float>::infinity());
            }
            return MakeFloat(ParseFloat32Fast(rawValue, std::numeric_limits<float>::infinity()));

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
        case PropId_HorizontalOffset:
        case PropId_VerticalOffset:
        case PropId_SmallChange:
        case PropId_LargeChange:
        case PropId_AnimScaleX:
        case PropId_AnimScaleY:
        case PropId_AnimRotateAngle:
        case PropId_AnimTranslateX:
        case PropId_AnimTranslateY:
        case PropId_AnimSkewX:
        case PropId_AnimSkewY:
        case PropId_RadiusX:
        case PropId_RadiusY:
        case PropId_StrokeThickness:
        case PropId_LineX1:
        case PropId_LineY1:
        case PropId_LineX2:
        case PropId_LineY2:
        case PropId_AnimRotation3DX:
        case PropId_AnimRotation3DY:
        case PropId_AnimRotation3DZ:
        case PropId_AnimTranslate3DX:
        case PropId_AnimTranslate3DY:
        case PropId_AnimTranslate3DZ:
        case PropId_EffectBlurRadius:
        case PropId_EffectShadowDepth:
            return MakeFloat(ParseFloat32Fast(rawValue));

        case PropId_EffectKind: {
            if (EqualsIgnoreCase(rawValue, "Blur") || EqualsIgnoreCase(rawValue, "BlurEffect") || rawValue == "1") {
                return value::FromInt(1);
            }
            if (EqualsIgnoreCase(rawValue, "DropShadow") || EqualsIgnoreCase(rawValue, "DropShadowEffect") || rawValue == "2") {
                return value::FromInt(2);
            }
            return value::FromInt(0);
        }

        case PropId_Transform3DRotateXYZ:
        case PropId_Transform3DTranslateXYZ:
        case PropId_Transform3DScaleXYZ:
        case PropId_Transform3DCenterXYZ:
        case PropId_EffectParams: {
            const auto nums = ParseFloatListFast(rawValue);
            const float x = nums.size() > 0 ? nums[0] : 0.0f;
            const float y = nums.size() > 1 ? nums[1] : 0.0f;
            const float z = nums.size() > 2 ? nums[2] : 0.0f;
            const float w = nums.size() > 3 ? nums[3] : 1.0f;
            return MakeFloat4InlineFast(x, y, z, w);
        }

        case PropId_IsChecked:
        case PropId_ClipToBounds:
        case PropId_IsReadOnly:
        case PropId_IsDropDownOpen:
        case PropId_IsVirtualizing:
        case PropId_IsPassword:
        case PropId_IsExpanded:
        case PropId_IsIndeterminate:
        case PropId_TriggerIsMouseOver:
        case PropId_TriggerIsPressed:
        case PropId_TriggerIsFocused:
        case PropId_TriggerIsKeyboardFocusWithin:
            return EqualsIgnoreCase(rawValue, "true") || rawValue == "1";

        case PropId_LastChildFill:
        case PropId_IsEnabled:
        case PropId_Focusable:
            return !EqualsIgnoreCase(rawValue, "false") && rawValue != "0";

        case PropId_Visibility:
            if (rawValue == "Collapsed" || rawValue == "2") return value::FromInt(2);
            if (rawValue == "Hidden" || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_HorizontalAlignment:
        case PropId_HorizontalContentAlignment:
            if (EqualsIgnoreCase(rawValue, "Left") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Center") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Stretch

        case PropId_VerticalAlignment:
        case PropId_VerticalContentAlignment:
            if (EqualsIgnoreCase(rawValue, "Top") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Center") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Stretch

        case PropId_Dock:
            if (EqualsIgnoreCase(rawValue, "Top") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Right") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Left

        case PropId_Orientation:
            if (EqualsIgnoreCase(rawValue, "Horizontal") || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_TextWrapping:
            if (EqualsIgnoreCase(rawValue, "Wrap") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "WrapWithOverflow") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0); // NoWrap

        case PropId_Stretch:
            if (EqualsIgnoreCase(rawValue, "None") || rawValue == "0") return value::FromInt(0);
            if (EqualsIgnoreCase(rawValue, "Fill") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "UniformToFill") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(2); // Default Stretch::Uniform = 2

        case PropId_StretchDirection:
            if (EqualsIgnoreCase(rawValue, "UpOnly") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "DownOnly") || rawValue == "2") return value::FromInt(2);
            return value::FromInt(0); // Both

        case PropId_HorizontalScrollBarVisibility:
        case PropId_VerticalScrollBarVisibility:
            if (EqualsIgnoreCase(rawValue, "Disabled") || rawValue == "0") return value::FromInt(0);
            if (EqualsIgnoreCase(rawValue, "Auto") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Hidden") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Visible") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(propId == PropId_HorizontalScrollBarVisibility ? 0 : 1);

        case PropId_GridRow:
        case PropId_GridColumn:
        case PropId_GridRowSpan:
        case PropId_GridColumnSpan:
        case PropId_SelectedIndex:
        case PropId_MaxLength:
        case PropId_CaretIndex:
        case PropId_UniformRows:
        case PropId_UniformColumns:
        case PropId_FirstColumn:
        case PropId_BackgroundGradientId:
        case PropId_ForegroundGradientId:
        case PropId_BorderGradientId: {
            std::int64_t iv = 0;
            const auto [ptr, ec] = std::from_chars(rawValue.data(), rawValue.data() + rawValue.size(), iv);
            if (ec != std::errc{} || ptr == rawValue.data()) {
                iv = static_cast<std::int64_t>(std::lround(ParseFloat32Fast(rawValue, 0.0f)));
            }
            return value::FromInt(iv);
        }

        case PropId_ExpandDirection:
            if (EqualsIgnoreCase(rawValue, "Up") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Left") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Down

        case PropId_TabStripPlacement:
            if (EqualsIgnoreCase(rawValue, "Bottom") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Left") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Top

        case PropId_Margin:
        case PropId_Padding:
        case PropId_BorderThickness: {
            const auto nums = ParseFloatListFast(rawValue);
            if (nums.size() == 1) return value::FromThickness(Thickness(nums[0]));
            if (nums.size() == 2) return value::FromThickness(Thickness(nums[0], nums[1]));
            if (nums.size() >= 4) {
                return value::FromThickness(Thickness(nums[0], nums[1], nums[2], nums[3]));
            }
            return value::FromThickness(Thickness{});
        }

        case PropId_CornerRadius: {
            const auto nums = ParseFloatListFast(rawValue);
            if (nums.size() == 1) return value::FromCornerRadius(CornerRadius(nums[0]));
            if (nums.size() >= 4) {
                return value::FromCornerRadius(CornerRadius(nums[0], nums[1], nums[2], nums[3]));
            }
            return value::FromCornerRadius(CornerRadius{});
        }

        case PropId_RenderTransformOrigin: {
            const auto nums = ParseFloatListFast(rawValue);
            if (nums.size() == 1) {
                return UnifiedValue{Float4Value{nums[0], nums[0], 0.0f, 0.0f}};
            }
            if (nums.size() >= 2) {
                return UnifiedValue{Float4Value{nums[0], nums[1], 0.0f, 0.0f}};
            }
            return UnifiedValue{Float4Value{0.0f, 0.0f, 0.0f, 0.0f}};
        }

        case PropId_RenderTransform: {
            const Matrix2D m = ParseXamlMatrix2DLiteral(rawValue);
            if (std::fabs(m.tx) > 1e-6f || std::fabs(m.ty) > 1e-6f) {
                return value::FromString(std::string(rawValue));
            }
            return UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        }

        case PropId_LayoutTransform: {
            const Matrix2D m = ParseXamlMatrix2DLiteral(rawValue);
            return UnifiedValue{Float4Value{m.m00, m.m01, m.m10, m.m11}};
        }

        case PropId_RenderTransformTranslation: {
            const auto nums = ParseFloatListFast(rawValue);
            if (nums.size() >= 2) {
                return UnifiedValue{Float4Value{nums[0], nums[1], 0.0f, 0.0f}};
            }
            return UnifiedValue{Float4Value{0.0f, 0.0f, 0.0f, 0.0f}};
        }

        case PropId_RenderTransformMatrix: {
            const auto nums = ParseFloatListFast(rawValue);
            if (nums.size() >= 4) {
                return UnifiedValue{Float4Value{nums[0], nums[1], nums[2], nums[3]}};
            }
            return UnifiedValue{Float4Value{1.0f, 1.0f, 0.0f, 0.0f}};
        }

        case PropId_Background:
        case PropId_Foreground:
        case PropId_BorderBrush:
            if (IsGradientBrushSpec(rawValue)) {
                return value::FromString(std::string(rawValue));
            }
        case PropId_EffectColor:
            return value::FromColor(ParseXamlColorLiteral(rawValue));

        default:
            return value::FromString(DecodeXamlXmlEntities(rawValue));
    }
}

std::uint64_t ComputeSvgPathHash(std::string_view pathData) noexcept {
    pathData = TrimWs(pathData);
    std::uint64_t hash = 14695981039346656037ULL;
    bool prevWasSpace = false;
    for (const char ch : pathData) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!prevWasSpace) {
                hash ^= static_cast<std::uint8_t>(' ');
                hash *= 1099511628211ULL;
                prevWasSpace = true;
            }
        } else {
            hash ^= static_cast<std::uint8_t>(ch);
            hash *= 1099511628211ULL;
            prevWasSpace = false;
        }
    }
    return hash;
}

Rect MeasureSvgPathBounds(std::string_view pathData) {
    return ParseSvgPathGeometry(pathData).bounds;
}

std::uint16_t SelectAdaptiveMtsdfCellSize(
    std::string_view pathData,
    float designMaxSpanPx,
    float* outRecommendedPxRange) noexcept {
    const ParsedSvgGeometry geom = ParseSvgPathGeometry(pathData);
    std::size_t segCount = 0;
    for (const auto& contour : geom.contours) {
        if (contour.points.size() >= 2u) {
            segCount += (contour.points.size() - 1u);
        }
    }
    const float span = std::max(designMaxSpanPx, std::max(geom.bounds.width, geom.bounds.height));
    std::uint16_t cell = 32u;
    float range = 4.0f;
    if (span <= 40.0f && segCount <= 16u) {
        cell = 32u;
        range = 4.0f;
    } else if (span <= 128.0f && segCount <= 64u) {
        cell = 64u;
        range = 6.0f;
    } else {
        cell = 128u;
        range = 8.0f;
    }
    if (outRecommendedPxRange) {
        *outRecommendedPxRange = range;
    }
    return cell;
}

void PackMtsdfAtlasSkyline(
    std::vector<AxbMtsdfPathEntry>& ioEntries,
    std::uint16_t atlasSize) noexcept {
    if (ioEntries.empty()) {
        return;
    }

    auto tryPackWithSize = [&](std::uint16_t targetAtlasSize) -> bool {
        const std::uint16_t safeAtlas = std::max<std::uint16_t>(targetAtlasSize, 256u);
        const std::uint16_t yStart = static_cast<std::uint16_t>(safeAtlas / 2u);
        std::vector<std::uint16_t> skyline(safeAtlas, yStart);

        // Pack in descending cell height order for minimal Skyline fragmentation while preserving ioEntries order
        std::vector<std::size_t> order(ioEntries.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return ioEntries[a].cellHeight > ioEntries[b].cellHeight;
        });

        const float invAtlas = 1.0f / static_cast<float>(safeAtlas);
        for (const std::size_t idx : order) {
            auto& entry = ioEntries[idx];
            const std::uint16_t cw = std::clamp<std::uint16_t>(entry.cellWidth, 8u, safeAtlas);
            const std::uint16_t ch = std::clamp<std::uint16_t>(entry.cellHeight, 8u, safeAtlas);
            std::uint16_t bestX = 0xFFFFu;
            std::uint16_t bestY = 0xFFFFu;

            for (std::uint16_t x = 0; x + cw <= safeAtlas; x = static_cast<std::uint16_t>(x + 8u)) {
                std::uint16_t maxY = yStart;
                for (std::uint16_t k = 0; k < cw; ++k) {
                    maxY = std::max(maxY, skyline[x + k]);
                }
                if (maxY + ch <= safeAtlas && maxY < bestY) {
                    bestY = maxY;
                    bestX = x;
                }
            }

            if (bestX == 0xFFFFu) {
                return false;
            }

            for (std::uint16_t k = 0; k < cw; ++k) {
                skyline[bestX + k] = static_cast<std::uint16_t>(bestY + ch);
            }

            entry.atlasWidth = safeAtlas;
            entry.atlasHeight = safeAtlas;
            entry.u0 = (static_cast<float>(bestX) + 0.5f) * invAtlas;
            entry.v0 = (static_cast<float>(bestY) + 0.5f) * invAtlas;
            entry.u1 = (static_cast<float>(bestX + cw) - 0.5f) * invAtlas;
            entry.v1 = (static_cast<float>(bestY + ch) - 0.5f) * invAtlas;
        }
        return true;
    };

    const std::uint16_t initialAtlas = std::max<std::uint16_t>(atlasSize, 512u);
    if (!tryPackWithSize(initialAtlas)) {
        (void)tryPackWithSize(1024u);
    }
}

DecomposedTransform2D PolarDecomposeMatrix2D(const Matrix2D& m) noexcept {
    DecomposedTransform2D out{};
    out.translateX = m.tx;
    out.translateY = m.ty;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    const float sx = std::sqrt(m.m00 * m.m00 + m.m10 * m.m10);
    if (sx <= 1e-7f) {
        out.scaleX = 0.0f;
        out.scaleY = std::sqrt(m.m01 * m.m01 + m.m11 * m.m11);
        return out;
    }

    out.scaleX = sx;
    const float rad = std::atan2(m.m10, m.m00);
    out.rotateDeg = rad * kRadToDeg;

    const float c = m.m00 / sx;
    const float s = m.m10 / sx;
    // Un-rotate second column (m01, m11)^T by R(-rad):
    // u = k * sy, v = sy
    const float u = c * m.m01 + s * m.m11;
    const float sy = -s * m.m01 + c * m.m11;
    out.scaleY = sy;
    if (std::fabs(sy) > 1e-7f) {
        out.skewXDeg = std::atan(u / sy) * kRadToDeg;
    }
    return out;
}

AxbMtsdfPathEntry BakeSvgPathToMtsdfEntry(
    std::string_view pathData,
    std::uint16_t cellSize,
    float pxRange,
    std::uint16_t atlasSlotIndex,
    std::uint16_t atlasSize) {
    const std::uint16_t safeCell = std::clamp<std::uint16_t>(cellSize, 8u, 128u);
    const std::uint16_t safeAtlas = std::max<std::uint16_t>(atlasSize, safeCell);
    const float safeRange = std::max(pxRange, 1.0f);

    const ParsedSvgGeometry geom = ParseSvgPathGeometry(pathData);
    const std::vector<ColoredSegment> coloredSegs = BuildCornerColoredSegments(geom);

    AxbMtsdfPathEntry entry{};
    entry.pathHash = ComputeSvgPathHash(pathData);
    entry.boundsX = geom.bounds.x;
    entry.boundsY = geom.bounds.y;
    entry.boundsW = geom.bounds.width;
    entry.boundsH = geom.bounds.height;
    entry.atlasWidth = safeAtlas;
    entry.atlasHeight = safeAtlas;
    entry.cellWidth = safeCell;
    entry.cellHeight = safeCell;
    entry.pxRange = safeRange;

    // Partition atlas vertically: top half v in [0.0..0.5) is reserved for font glyphs;
    // bottom half v in [0.5..1.0) holds MTSDF vector icon cells.
    const std::uint16_t cols = std::max<std::uint16_t>(1u, safeAtlas / safeCell);
    const std::uint16_t totalRows = cols;
    const std::uint16_t rowBase = (totalRows >= 2u) ? static_cast<std::uint16_t>(totalRows / 2u) : 0u;
    const std::uint16_t iconRows = std::max<std::uint16_t>(1u, static_cast<std::uint16_t>(totalRows - rowBase));
    const std::uint16_t slotCol = atlasSlotIndex % cols;
    const std::uint16_t slotRow = static_cast<std::uint16_t>(rowBase + ((atlasSlotIndex / cols) % iconRows));
    const float invAtlas = 1.0f / static_cast<float>(safeAtlas);
    // Inset UV by 0.5 texel to prevent bilinear bleeding into neighboring atlas cells
    entry.u0 = (static_cast<float>(slotCol * safeCell) + 0.5f) * invAtlas;
    entry.v0 = (static_cast<float>(slotRow * safeCell) + 0.5f) * invAtlas;
    entry.u1 = (static_cast<float>((slotCol + 1u) * safeCell) - 0.5f) * invAtlas;
    entry.v1 = (static_cast<float>((slotRow + 1u) * safeCell) - 0.5f) * invAtlas;

    entry.mtsdfRgbaPixels.resize(static_cast<std::size_t>(safeCell) * safeCell * 4u, 0u);

    if (coloredSegs.empty()) {
        return entry;
    }

    // Reserve padding = ceil(pxRange) + 1 texels on all 4 sides so outer distance falloff never clips
    const float padTexels = std::ceil(safeRange) + 1.0f;
    const float usableTexels = std::max(2.0f, static_cast<float>(safeCell) - 2.0f * padTexels);
    const float scaleX = usableTexels / std::max(1e-3f, geom.bounds.width);
    const float scaleY = usableTexels / std::max(1e-3f, geom.bounds.height);
    const float uniformScale = std::min(scaleX, scaleY);

    const float scaledW = geom.bounds.width * uniformScale;
    const float scaledH = geom.bounds.height * uniformScale;
    const float offsetX = (static_cast<float>(safeCell) - scaledW) * 0.5f - geom.bounds.x * uniformScale;
    const float offsetY = (static_cast<float>(safeCell) - scaledH) * 0.5f - geom.bounds.y * uniformScale;

    // Precompute cell-space segment geometry & midpoint inside/outside orientation once (O(S))
    struct PreparedCellSegment {
        float ax = 0.0f;
        float ay = 0.0f;
        float abx = 0.0f;
        float aby = 0.0f;
        float lenSq = 0.0f;
        float dirX = 0.0f;
        float dirY = 0.0f;
        float leftSign = 1.0f;
        std::uint8_t channelMask = 0x07u;
        bool inClosedContour = true;
    };

    std::vector<PreparedCellSegment> preparedSegs;
    preparedSegs.reserve(coloredSegs.size());
    bool anyClosedContour = false;
    const float maxBoundDim = std::max(geom.bounds.width, geom.bounds.height);

    for (const auto& seg : coloredSegs) {
        const float ax = seg.p0.x * uniformScale + offsetX;
        const float ay = seg.p0.y * uniformScale + offsetY;
        const float bx = seg.p1.x * uniformScale + offsetX;
        const float by = seg.p1.y * uniformScale + offsetY;
        const float abx = bx - ax;
        const float aby = by - ay;
        const float lenSq = abx * abx + aby * aby;
        if (lenSq <= 1e-10f) {
            continue;
        }
        const float invLen = 1.0f / std::sqrt(lenSq);
        const float dirX = abx * invLen;
        const float dirY = aby * invLen;

        float leftSign = -1.0f;
        if (seg.inClosedContour) {
            anyClosedContour = true;
            const float svgDx = seg.p1.x - seg.p0.x;
            const float svgDy = seg.p1.y - seg.p0.y;
            const float svgLen = std::sqrt(svgDx * svgDx + svgDy * svgDy);
            const float midSvgX = (seg.p0.x + seg.p1.x) * 0.5f;
            const float midSvgY = (seg.p0.y + seg.p1.y) * 0.5f;
            const float normSvgX = -svgDy / std::max(svgLen, 1e-6f);
            const float normSvgY = svgDx / std::max(svgLen, 1e-6f);
            const float eps = std::max(1e-5f, std::min(svgLen * 0.1f, maxBoundDim * 5e-4f));
            const bool leftSideInside = IsPointInsideSvgGeometry(
                geom,
                midSvgX + normSvgX * eps,
                midSvgY + normSvgY * eps);
            leftSign = leftSideInside ? 1.0f : -1.0f;
        }

        preparedSegs.push_back(PreparedCellSegment{
            ax, ay, abx, aby, lenSq, dirX, dirY, leftSign, seg.channelMask, seg.inClosedContour});
    }

    if (preparedSegs.empty()) {
        return entry;
    }

    for (std::uint16_t py = 0; py < safeCell; ++py) {
        for (std::uint16_t px = 0; px < safeCell; ++px) {
            const float cellX = static_cast<float>(px) + 0.5f;
            const float cellY = static_cast<float>(py) + 0.5f;
            const float svgX = (cellX - offsetX) / uniformScale;
            const float svgY = (cellY - offsetY) / uniformScale;

            const bool inside = anyClosedContour && IsPointInsideSvgGeometry(geom, svgX, svgY);
            const float globalSign = inside ? 1.0f : -1.0f;

            float minTrueDistSq = 1e30f;

            struct ChannelBest {
                float minDistSq = 1e30f;
                float bestOrtho = 1e30f;
                float pseudoDist = 1e30f;
                bool found = false;
            };
            ChannelBest chBest[3]{};

            for (const auto& seg : preparedSegs) {
                const float apx = cellX - seg.ax;
                const float apy = cellY - seg.ay;
                const float proj = (apx * seg.abx + apy * seg.aby) / seg.lenSq;
                const float clampedT = std::clamp(proj, 0.0f, 1.0f);
                const float dx = cellX - (seg.ax + clampedT * seg.abx);
                const float dy = cellY - (seg.ay + clampedT * seg.aby);
                const float distSq = dx * dx + dy * dy;
                minTrueDistSq = std::min(minTrueDistSq, distSq);

                if (!seg.inClosedContour) {
                    continue;
                }

                // Perpendicular distance to infinite tangent line (preserves sharp corner intersection!)
                const float crossVal = seg.dirX * apy - seg.dirY * apx;
                const float lineAbsDist = std::fabs(crossVal);
                const float sampleOnLeft = (crossVal >= 0.0f) ? 1.0f : -1.0f;
                const float pseudoSigned = lineAbsDist * sampleOnLeft * seg.leftSign;
                const float orthoScore = lineAbsDist;

                for (int c = 0; c < 3; ++c) {
                    if ((seg.channelMask & (1u << c)) != 0u) {
                        const bool closer = (distSq + 1e-4f < chBest[c].minDistSq) ||
                            (std::fabs(distSq - chBest[c].minDistSq) <= 1e-4f &&
                             orthoScore > chBest[c].bestOrtho);
                        if (!chBest[c].found || closer) {
                            chBest[c].found = true;
                            chBest[c].minDistSq = distSq;
                            chBest[c].bestOrtho = orthoScore;
                            chBest[c].pseudoDist = (proj >= 0.0f && proj <= 1.0f)
                                ? (std::sqrt(distSq) * sampleOnLeft * seg.leftSign)
                                : pseudoSigned;
                        }
                    }
                }
            }

            const float trueSignedDist = globalSign * std::sqrt(minTrueDistSq);
            float rDist = -safeRange;
            float gDist = -safeRange;
            float bDist = -safeRange;

            if (anyClosedContour) {
                rDist = chBest[0].found ? chBest[0].pseudoDist : trueSignedDist;
                gDist = chBest[1].found ? chBest[1].pseudoDist : trueSignedDist;
                bDist = chBest[2].found ? chBest[2].pseudoDist : trueSignedDist;

                // Enforce median sign consistency with true winding sign against tangent-extension clashes
                const float med = std::max(std::min(rDist, gDist), std::min(std::max(rDist, gDist), bDist));
                if ((med > 0.0f) != inside &&
                    (std::fabs(trueSignedDist) > 0.15f || std::fabs(med - trueSignedDist) > 1.25f)) {
                    rDist = trueSignedDist;
                    gDist = trueSignedDist;
                    bDist = trueSignedDist;
                }
            }

            const std::size_t pixIdx = (static_cast<std::size_t>(py) * safeCell + px) * 4u;
            entry.mtsdfRgbaPixels[pixIdx + 0u] = QuantizeNormalizedSdf(rDist, safeRange);
            entry.mtsdfRgbaPixels[pixIdx + 1u] = QuantizeNormalizedSdf(gDist, safeRange);
            entry.mtsdfRgbaPixels[pixIdx + 2u] = QuantizeNormalizedSdf(bDist, safeRange);
            entry.mtsdfRgbaPixels[pixIdx + 3u] = QuantizeNormalizedSdf(trueSignedDist, safeRange);
        }
    }

    return entry;
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
    auto visitMultiBindingRec = [&](auto& self, const AxbCompiledMultiBinding& mb) noexcept -> void {
        if (mb.fallbackValue.isTraceable()) {
            fn(mb.fallbackValue);
        }
        for (const auto& srcCh : mb.sources) {
            if (srcCh.fallbackValue.isTraceable()) {
                fn(srcCh.fallbackValue);
            }
            if (srcCh.nestedMultiBinding) {
                self(self, *srcCh.nestedMultiBinding);
            }
        }
    };
    for (const auto& ch : pkg.bindingChains) {
        if (ch.fallbackValue.isTraceable()) {
            fn(ch.fallbackValue);
        }
        if (ch.nestedMultiBinding) {
            visitMultiBindingRec(visitMultiBindingRec, *ch.nestedMultiBinding);
        }
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
    for (const auto& st : pkg.styles) {
        for (const auto& setter : st.setters) {
            if (setter.value.isTraceable()) fn(setter.value);
        }
        for (const auto& trig : st.triggers) {
            for (const auto& cond : trig.conditions) {
                if (cond.expectedValue.isTraceable()) fn(cond.expectedValue);
            }
            for (const auto& setter : trig.setters) {
                if (setter.value.isTraceable()) fn(setter.value);
            }
            visitStoryboards(trig.enterStoryboards);
            visitStoryboards(trig.exitStoryboards);
        }
        auto visitInteractionActions = [&](const std::vector<AxbInteractionAction>& acts) noexcept {
            for (const auto& act : acts) {
                if (act.value.isTraceable()) fn(act.value);
                if (act.commandParameter.isTraceable()) fn(act.commandParameter);
                if (act.commandParameterBinding.fallbackValue.isTraceable()) {
                    fn(act.commandParameterBinding.fallbackValue);
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
        for (const auto& trig : st.triggers) {
            visitInteractionActions(trig.enterActions);
            visitInteractionActions(trig.exitActions);
        }
        for (const auto& et : st.eventTriggers) {
            visitStoryboards(et.storyboards);
            visitInteractionActions(et.actions);
        }
        for (const auto& vsg : st.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& setter : vs.setters) {
                    if (setter.value.isTraceable()) fn(setter.value);
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
            }
            visitStoryboards(trig.enterStoryboards);
            visitStoryboards(trig.exitStoryboards);
        }
        for (const auto& et : ct.eventTriggers) {
            visitStoryboards(et.storyboards);
        }
        for (const auto& vsg : ct.visualStateGroups) {
            for (const auto& vs : vsg.states) {
                for (const auto& setter : vs.setters) {
                    if (setter.value.isTraceable()) fn(setter.value);
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
    for (const auto& ct : pkg.controlTemplates) {
        visitControlTemplate(ct);
    }
    for (const auto& st : pkg.styles) {
        if (st.inlineControlTemplate) {
            visitControlTemplate(*st.inlineControlTemplate);
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
    std::string_view rootDataTypeName,
    const ExecutionDomainContext* domains) {
    ExecutionDomainContext localDomains;
    if (!domains) {
        localDomains.registerProgramObservableMetadata(program);
        domains = &localDomains;
    }

    const std::string effectiveClassName = !rootDataTypeName.empty()
        ? std::string(rootDataTypeName)
        : className;

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
                rootHandleFieldIndex = static_cast<std::uint32_t>(fIdx);
                break;
            }
        }
        for (auto& nc : namedControls) {
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
        for (auto& ev : eventBindings) {
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
        for (auto& inst : instructions) {
            if (inst.opcode == AxbOpcode::BindEvent && inst.strIdx0 < eventBindings.size()) {
                inst.symbolId = eventBindings[inst.strIdx0].handlerSymbolId;
            }
        }
    }

    auto bindHopVector = [&](std::vector<AxbCompiledHop>& hops, SymbolId rootOwnerId) {
        SymbolId currentHopOwnerId = rootOwnerId;
        for (auto& hop : hops) {
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
            const auto* ownerSym = program.resolveType(currentHopOwnerId);
            SymbolId nextTypeId = 0;
            std::string rawNextTypeName;
            if (ownerSym) {
                for (const auto& pr : ownerSym->properties) {
                    if (pr.name == hop.memberName && !pr.typeName.empty()) {
                        rawNextTypeName = pr.typeName;
                        break;
                    }
                }
                if (rawNextTypeName.empty()) {
                    for (const auto& fl : ownerSym->fields) {
                        if (fl.name == hop.memberName && !fl.typeName.empty()) {
                            rawNextTypeName = fl.typeName;
                            break;
                        }
                    }
                }
                if (!rawNextTypeName.empty()) {
                    std::string canonicalNext = rawNextTypeName;
                    if (canonicalNext.find("::") == std::string::npos) {
                        if (const auto lastDot = canonicalNext.rfind('.'); lastDot != std::string::npos) {
                            canonicalNext = canonicalNext.substr(0, lastDot) + "::" + canonicalNext.substr(lastDot + 1);
                        }
                    }
                    nextTypeId = rs_sem::stableTypeId(canonicalNext);
                    if (!program.resolveType(nextTypeId)) {
                        for (const auto& mod : program.modules()) {
                            for (const auto& t : mod.types) {
                                if (t.name == rawNextTypeName ||
                                    (mod.name + "." + t.name) == rawNextTypeName ||
                                    (mod.name + "::" + t.name) == rawNextTypeName) {
                                    nextTypeId = t.id;
                                    break;
                                }
                            }
                            if (program.resolveType(nextTypeId)) {
                                break;
                            }
                        }
                    }
                }
            }
            currentHopOwnerId = nextTypeId;
        }
    };

    // 2b. Bind timeline motionEvents handler method SymbolIds on the partial class
    if (rootTypeSym) {
        for (auto& mev : motionEvents) {
            const std::string_view evName = GetString(mev.eventNameStrIdx);
            if (evName.empty()) {
                continue;
            }
            const std::string onEvName = "On" + std::string(evName);
            for (const auto& m : rootTypeSym->methods) {
                if (m.name == evName || m.name == onEvName) {
                    mev.handlerSymbolId = m.id;
                    break;
                }
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
        const std::string back1 = shortKey + "_ConvertBack";
        const std::string back2 = shortKey + "::ConvertBack";

        for (const auto& mod : program.modules()) {
            for (const auto& fn : mod.functions) {
                std::string_view leafName = fn.name;
                if (const auto sep = leafName.rfind("::"); sep != std::string_view::npos) {
                    leafName = leafName.substr(sep + 2u);
                }
                if (ioConvSym == 0 &&
                    (fn.name == convKey || leafName == shortKey ||
                     leafName == fwd1 || fn.name.find(fwd2) != std::string::npos)) {
                    ioConvSym = fn.symbolId;
                }
                if (ioBackSym == 0 &&
                    (leafName == back1 || fn.name.find(back2) != std::string::npos)) {
                    ioBackSym = fn.symbolId;
                }
            }
        }
    };

    auto bindActionCommand = [&](AxbInteractionAction& act) {
        if (act.kind != InteractionActionKind::InvokeCommand) {
            return;
        }
        if (act.hasCommandParameterBinding &&
            act.commandParameterBinding.elementName.empty() &&
            act.commandParameterBinding.relativeAncestorType.empty() &&
            !act.commandParameterBinding.relativeSelf) {
            bindHopVector(act.commandParameterBinding.hops, classTypeId);
            resolveConverterSymbols(
                act.commandParameterBinding.converter,
                act.commandParameterBinding.converterKey,
                act.commandParameterBinding.converterSymbolId,
                act.commandParameterBinding.convertBackSymbolId);
        }
        if (act.commandPath.empty()) {
            return;
        }
        if (!act.commandHops.empty()) {
            bindHopVector(act.commandHops, classTypeId);
            if (act.commandHops.back().isSimCommand) {
                act.isSimCommand = true;
            }
        }
        // Resolve method SymbolId on owner class (last hop's declaring/owner type or rootTypeSym)
        const rs_sem::TypeSymbol* methodOwnerSym = rootTypeSym;
        if (act.commandHops.size() > 1u) {
            const SymbolId penultDecl = act.commandHops.back().declaringTypeId;
            if (penultDecl != 0) {
                if (const auto* ts = program.resolveType(penultDecl)) {
                    methodOwnerSym = ts;
                }
            }
        }
        std::string leafCmd = act.commandPath;
        if (const auto dot = leafCmd.rfind('.'); dot != std::string::npos) {
            leafCmd = leafCmd.substr(dot + 1u);
        }
        if (methodOwnerSym && !leafCmd.empty()) {
            for (const auto& m : methodOwnerSym->methods) {
                if (m.name == leafCmd) {
                    act.commandMethodSymbolId = m.id;
                    break;
                }
            }
            if (act.commandMethodSymbolId == 0 && leafCmd.size() > 7u &&
                leafCmd.compare(leafCmd.size() - 7u, 7u, "Command") == 0) {
                const std::string baseCmd = leafCmd.substr(0u, leafCmd.size() - 7u);
                const std::string onBaseCmd = "On" + baseCmd;
                for (const auto& m : methodOwnerSym->methods) {
                    if (m.name == baseCmd || m.name == onBaseCmd) {
                        act.commandMethodSymbolId = m.id;
                        break;
                    }
                }
            }
        }
        if (act.commandMethodSymbolId != 0) {
            if (const auto* loc = program.resolveFunction(act.commandMethodSymbolId)) {
                if (loc->function && loc->function->simCommand) {
                    act.isSimCommand = true;
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
            if (srcCh.elementName.empty() && srcCh.relativeAncestorType.empty() && !srcCh.relativeSelf) {
                bindHopVector(srcCh.hops, classTypeId);
            }
        }
    };

    // 3. Bind multi-hop PropertySlotHandles & converter symbols in bindingChains and multiBindings
    for (auto& chain : bindingChains) {
        resolveConverterSymbols(
            chain.converter,
            chain.converterKey,
            chain.converterSymbolId,
            chain.convertBackSymbolId);
        if (chain.nestedMultiBinding) {
            bindMultiRec(bindMultiRec, *chain.nestedMultiBinding);
        }
        if (!chain.elementName.empty() || !chain.relativeAncestorType.empty() || chain.relativeSelf) {
            continue;
        }
        bindHopVector(chain.hops, classTypeId);
    }
    for (auto& mb : multiBindings) {
        bindMultiRec(bindMultiRec, mb);
    }

    // 4. Bind DataTrigger / MultiDataTrigger compiledHops & Interaction actions against root DataType
    for (auto& style : styles) {
        for (auto& trig : style.triggers) {
            for (auto& cond : trig.conditions) {
                SymbolId dummyBack = 0;
                resolveConverterSymbols(
                    cond.converter,
                    cond.converterKey,
                    cond.converterSymbolId,
                    dummyBack);
                if (classTypeId != 0 &&
                    cond.sourceKind == TriggerConditionSource::DataContextBinding &&
                    !cond.compiledHops.empty()) {
                    bindHopVector(cond.compiledHops, classTypeId);
                }
            }
            for (auto& act : trig.enterActions) {
                bindActionCommand(act);
            }
            for (auto& act : trig.exitActions) {
                bindActionCommand(act);
            }
        }
        for (auto& et : style.eventTriggers) {
            for (auto& act : et.actions) {
                bindActionCommand(act);
            }
        }
    }

    // 5. Bind nested DataTemplate & ControlTemplate sub-packages
    for (auto& dt : dataTemplates) {
        if (dt.templatePackage) {
            (void)dt.templatePackage->BindAgainstProgram(program, dt.dataTypeName, domains);
        }
    }
    for (auto& ct : controlTemplates) {
        if (classTypeId != 0) {
            for (auto& trig : ct.triggers) {
                for (auto& cond : trig.conditions) {
                    if (cond.sourceKind == TriggerConditionSource::DataContextBinding &&
                        !cond.compiledHops.empty()) {
                        bindHopVector(cond.compiledHops, classTypeId);
                    }
                }
            }
        }
        if (ct.templatePackage) {
            (void)ct.templatePackage->BindAgainstProgram(program, effectiveClassName, domains);
        }
    }

    return true;
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
            if (!et.actions.empty()) {
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
            if (!et.actions.empty()) {
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

    auto writeStoryboard = [&](const AxbStoryboard& sb) {
        WriteString(out, sb.key);
        WriteU32(out, static_cast<std::uint32_t>(sb.timelines.size()));
        for (const auto& tl : sb.timelines) {
            WriteString(out, tl.targetName);
            WriteU16(out, tl.targetPropertyId);
            WriteF32(out, tl.beginTimeSeconds);
            WriteF32(out, tl.durationSeconds);
            std::uint8_t tlFlags = 0u;
            if (tl.hasFrom) tlFlags |= 0x01u;
            if (tl.hasTo) tlFlags |= 0x02u;
            if (tl.autoReverse) tlFlags |= 0x04u;
            if (tl.fillBehavior == FillBehavior::Stop) tlFlags |= 0x08u;
            if (tl.isBy) tlFlags |= 0x10u;
            WriteU8(out, tlFlags);
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
                WriteU8(out, kf.isDiscrete ? 1u : 0u);
            }
        }
    };

    if (!styles.empty() || !storyboards.empty()) {
        auto writeSetterList = [&](const std::vector<AxbPropertySetter>& setters) {
            WriteU32(out, static_cast<std::uint32_t>(setters.size()));
            for (const auto& st : setters) {
                WriteString(out, st.targetName);
                WriteU16(out, st.propId);
                WriteValue(out, st.value);
            }
        };

        WriteU32(out, kAxb4StyleAnimSectionMagic);
        WriteU32(out, static_cast<std::uint32_t>(styles.size()));
        for (const auto& style : styles) {
            WriteString(out, style.key);
            WriteString(out, style.targetTypeName);
            WriteString(out, style.basedOnKey);
            writeSetterList(style.setters);
            WriteU32(out, static_cast<std::uint32_t>(style.triggers.size()));
            for (const auto& trig : style.triggers) {
                WriteU8(out, trig.overrideLocalValues ? 1u : 0u);
                WriteU32(out, static_cast<std::uint32_t>(trig.conditions.size()));
                for (const auto& cond : trig.conditions) {
                    WriteU16(out, cond.propId);
                    WriteValue(out, cond.expectedValue);
                }
                writeSetterList(trig.setters);
                WriteU32(out, static_cast<std::uint32_t>(trig.enterStoryboards.size()));
                for (const auto& sb : trig.enterStoryboards) writeStoryboard(sb);
                WriteU32(out, static_cast<std::uint32_t>(trig.exitStoryboards.size()));
                for (const auto& sb : trig.exitStoryboards) writeStoryboard(sb);
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
                    writeSetterList(vs.setters);
                    writeStoryboard(vs.storyboard);
                }
            }
        }
        WriteU32(out, static_cast<std::uint32_t>(storyboards.size()));
        for (const auto& sb : storyboards) {
            writeStoryboard(sb);
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
                    WriteU32(out, static_cast<std::uint32_t>(cond.compiledHops.size()));
                    for (const auto& hop : cond.compiledHops) {
                        WriteString(out, hop.memberName);
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
            }
            WriteU32(out, static_cast<std::uint32_t>(st.eventTriggers.size()));
            for (const auto& et : st.eventTriggers) {
                WriteU8(out, static_cast<std::uint8_t>(et.eventKind));
                WriteString(out, et.sourceName);
                WriteU32(out, static_cast<std::uint32_t>(et.storyboards.size()));
                for (const auto& sb : et.storyboards) {
                    writeStoryboard(sb);
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
        auto writeCtpSetterList = [&](const std::vector<AxbPropertySetter>& setters) {
            WriteU32(out, static_cast<std::uint32_t>(setters.size()));
            for (const auto& st : setters) {
                WriteString(out, st.targetName);
                WriteU16(out, st.propId);
                WriteValue(out, st.value);
            }
        };

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
                    WriteU32(out, static_cast<std::uint32_t>(cond.compiledHops.size()));
                    for (const auto& hop : cond.compiledHops) {
                        WriteString(out, hop.memberName);
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
                writeCtpSetterList(trig.setters);
                WriteU32(out, static_cast<std::uint32_t>(trig.enterStoryboards.size()));
                for (const auto& sb : trig.enterStoryboards) writeStoryboard(sb);
                WriteU32(out, static_cast<std::uint32_t>(trig.exitStoryboards.size()));
                for (const auto& sb : trig.exitStoryboards) writeStoryboard(sb);
            }

            // EventTriggers
            WriteU32(out, static_cast<std::uint32_t>(ct.eventTriggers.size()));
            for (const auto& et : ct.eventTriggers) {
                WriteU8(out, static_cast<std::uint8_t>(et.eventKind));
                WriteString(out, et.sourceName);
                WriteU32(out, static_cast<std::uint32_t>(et.storyboards.size()));
                for (const auto& sb : et.storyboards) writeStoryboard(sb);
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
                    writeCtpSetterList(vs.setters);
                    writeStoryboard(vs.storyboard);
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

    if (HasP14Section()) {
        WriteU32(out, kAxb4P14SectionMagic);

        auto writeHopVec = [&](const std::vector<AxbCompiledHop>& hops) {
            WriteU32(out, static_cast<std::uint32_t>(hops.size()));
            for (const auto& hop : hops) {
                WriteString(out, hop.memberName);
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
        };

        auto writeFullChainRec = [&](auto& selfChain, auto& selfMulti, const AxbCompiledBindingChain& ch) -> void {
            WriteString(out, ch.rawPath);
            WriteU16(out, ch.targetPropertyId);
            WriteU8(out, static_cast<std::uint8_t>(ch.mode));
            WriteValue(out, ch.fallbackValue);
            writeHopVec(ch.hops);
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
                writeStoryboard(act.inlineStoryboard);
                WriteU8(out, static_cast<std::uint8_t>(act.storyboardOption));
                WriteString(out, act.commandPath);
                writeHopVec(act.commandHops);
                WriteU64(out, act.commandMethodSymbolId);
                WriteValue(out, act.commandParameter);
                if (act.hasCommandParameterBinding) {
                    writeFullChainRec(writeFullChainRec, writeMultiBindingRec, act.commandParameterBinding);
                }
                WriteString(out, act.soundSource);
                WriteF32(out, act.volume);
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
            }
        }
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
    if (r.offset < r.size) {
        const std::size_t peekPos = r.offset;
        std::uint32_t secMagic = 0;
        if (!r.ReadU32(secMagic)) {
            if (errorMessage) *errorMessage = "corrupted AXB4 trailing section header";
            return false;
        }
        if (secMagic == kAxb4MtsdfSectionMagic) {
            std::uint32_t mtsdfCount = 0;
            if (!r.ReadU32(mtsdfCount) || mtsdfCount > (r.size - r.offset) / 56u) {
                if (errorMessage) *errorMessage = "corrupted AXB4 MTSDF atlas section";
                return false;
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
                    if (errorMessage) *errorMessage = "corrupted AXB4 MTSDF entry";
                    return false;
                }
            }
        } else {
            r.offset = peekPos;
        }
    }

    auto failStcSection = [&](const char* msg) -> bool {
        if (errorMessage) *errorMessage = msg;
        return false;
    };

    constexpr std::uint8_t kMaxEasingKindRaw = static_cast<std::uint8_t>(EasingFunctionKind::Elastic);
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
                kf.isDiscrete = (kdisc != 0u);
            }
        }
        return true;
    };

    if (r.offset < r.size) {
        const std::size_t peekStc = r.offset;
        std::uint32_t stcMagic = 0;
        if (!r.ReadU32(stcMagic)) {
            if (errorMessage) *errorMessage = "corrupted AXB4 trailing section magic";
            return false;
        }
        if (stcMagic == kAxb4StyleAnimSectionMagic) {
            auto readSetterList = [&](std::vector<AxbPropertySetter>& setters) -> bool {
                std::uint32_t sc = 0;
                if (!r.ReadU32(sc) || sc > (r.size - r.offset) / 7u) {
                    return failStcSection("corrupted AXB4 Setter list header");
                }
                setters.resize(sc);
                for (std::uint32_t i = 0; i < sc; ++i) {
                    if (!r.ReadString(setters[i].targetName) ||
                        !r.ReadU16(setters[i].propId) ||
                        !r.ReadValue(setters[i].value)) {
                        return failStcSection("corrupted AXB4 Setter entry");
                    }
                }
                return true;
            };

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
                    !readSetterList(style.setters) ||
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
                    if (!readSetterList(trig.setters) ||
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
                            !readSetterList(vsg.states[vi].setters) ||
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
        } else {
            r.offset = peekStc;
        }
    }

    if (r.offset < r.size) {
        const std::size_t peekDtp = r.offset;
        std::uint32_t dtpMagic = 0;
        if (!r.ReadU32(dtpMagic)) {
            return failStcSection("corrupted AXB4 Phase 5D section magic");
        }
        if (dtpMagic == kAxb4Phase5DSectionMagic) {
            constexpr std::uint8_t kMaxConverterRaw =
                static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost);
            constexpr std::uint8_t kMaxConditionSourceRaw =
                static_cast<std::uint8_t>(TriggerConditionSource::AncestorProperty);
            constexpr std::uint8_t kMaxRoutedEventRaw =
                static_cast<std::uint8_t>(RoutedTriggerEvent::ValueChanged);

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
                        std::uint32_t hopCount = 0;
                        if (!r.ReadU8(sk) ||
                            !r.ReadU8(cv) ||
                            !r.ReadU8(expNull) ||
                            sk > kMaxConditionSourceRaw ||
                            cv > kMaxConverterRaw ||
                            !r.ReadString(cond.sourceName) ||
                            !r.ReadString(cond.bindingPath) ||
                            !r.ReadU32(hopCount) ||
                            hopCount > (r.size - r.offset) / 34u) {
                            return failStcSection("corrupted AXB4 Phase 5D condition extension");
                        }
                        cond.sourceKind = static_cast<TriggerConditionSource>(sk);
                        cond.converter = static_cast<ValueConverterKind>(cv);
                        cond.expectNull = (expNull != 0u);
                        cond.compiledHops.resize(hopCount);
                        for (std::uint32_t h = 0; h < hopCount; ++h) {
                            auto& hop = cond.compiledHops[h];
                            std::uint8_t hopFlags = 0u;
                            if (!r.ReadString(hop.memberName) ||
                                !r.ReadU64(hop.declaringTypeId) ||
                                !r.ReadU32(hop.fieldIndex) ||
                                !r.ReadU8(hop.observableBitIndex) ||
                                !r.ReadU64(hop.getterSymbolId) ||
                                !r.ReadU64(hop.setterSymbolId) ||
                                !r.ReadU8(hopFlags)) {
                                return failStcSection("corrupted AXB4 Phase 5D compiledHop entry");
                            }
                            hop.isAutoProperty = (hopFlags & 0x01u) != 0u;
                            hop.isObservable = (hopFlags & 0x02u) != 0u;
                            hop.isReadOnly = (hopFlags & 0x04u) != 0u;
                            hop.isSimState = (hopFlags & 0x08u) != 0u;
                            hop.isSimCommand = (hopFlags & 0x10u) != 0u;
                        }
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
                    if (!AxbPackage::Deserialize(subBytes, *subPkg, errorMessage)) {
                        return false;
                    }
                    dt.templatePackage = std::move(subPkg);
                }
            }
        } else {
            r.offset = peekDtp;
        }
    }

    while (r.offset < r.size) {
        std::uint32_t trailMagic = 0;
        if (!r.ReadU32(trailMagic)) {
            return failStcSection("corrupted AXB4 trailing section magic");
        }
        if (trailMagic == kAxb4MotionSectionMagic) {
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
        constexpr std::uint8_t kMaxCtpConverterRaw =
            static_cast<std::uint8_t>(ValueConverterKind::CustomScriptOrHost);
        constexpr std::uint8_t kMaxCtpCondSrcRaw =
            static_cast<std::uint8_t>(TriggerConditionSource::AncestorProperty);
        constexpr std::uint8_t kMaxCtpRoutedEventRaw =
            static_cast<std::uint8_t>(RoutedTriggerEvent::ValueChanged);

        auto readCtpSetterList = [&](std::vector<AxbPropertySetter>& setters) -> bool {
            std::uint32_t sc = 0;
            if (!r.ReadU32(sc) || sc > (r.size - r.offset) / 7u) {
                return failStcSection("corrupted AXB4 CTP1 Setter list header");
            }
            setters.resize(sc);
            for (std::uint32_t i = 0; i < sc; ++i) {
                if (!r.ReadString(setters[i].targetName) ||
                    !r.ReadU16(setters[i].propId) ||
                    !r.ReadValue(setters[i].value)) {
                    return failStcSection("corrupted AXB4 CTP1 Setter entry");
                }
            }
            return true;
        };

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
                if (!AxbPackage::Deserialize(subBytes, *subPkg, errorMessage)) {
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
                    std::uint32_t hopCount = 0;
                    if (!r.ReadU16(cond.propId) ||
                        !r.ReadValue(cond.expectedValue) ||
                        !r.ReadU8(sk) ||
                        !r.ReadU8(cv) ||
                        !r.ReadU8(expNull) ||
                        sk > kMaxCtpCondSrcRaw ||
                        cv > kMaxCtpConverterRaw ||
                        !r.ReadString(cond.sourceName) ||
                        !r.ReadString(cond.bindingPath) ||
                        !r.ReadU32(hopCount) ||
                        hopCount > (r.size - r.offset) / 34u) {
                        return failStcSection("corrupted AXB4 CTP1 TriggerCondition");
                    }
                    cond.sourceKind = static_cast<TriggerConditionSource>(sk);
                    cond.converter = static_cast<ValueConverterKind>(cv);
                    cond.expectNull = (expNull != 0u);
                    cond.compiledHops.resize(hopCount);
                    for (std::uint32_t h = 0; h < hopCount; ++h) {
                        auto& hop = cond.compiledHops[h];
                        std::uint8_t hopFlags = 0u;
                        if (!r.ReadString(hop.memberName) ||
                            !r.ReadU64(hop.declaringTypeId) ||
                            !r.ReadU32(hop.fieldIndex) ||
                            !r.ReadU8(hop.observableBitIndex) ||
                            !r.ReadU64(hop.getterSymbolId) ||
                            !r.ReadU64(hop.setterSymbolId) ||
                            !r.ReadU8(hopFlags)) {
                            return failStcSection("corrupted AXB4 CTP1 compiledHop");
                        }
                        hop.isAutoProperty = (hopFlags & 0x01u) != 0u;
                        hop.isObservable = (hopFlags & 0x02u) != 0u;
                        hop.isReadOnly = (hopFlags & 0x04u) != 0u;
                        hop.isSimState = (hopFlags & 0x08u) != 0u;
                        hop.isSimCommand = (hopFlags & 0x10u) != 0u;
                    }
                }
                if (!readCtpSetterList(trig.setters) ||
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
                        !readCtpSetterList(vsg.states[vi].setters) ||
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

        auto readHopVec = [&](std::vector<AxbCompiledHop>& hops) -> bool {
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
            }
            return true;
        };

        auto readFullChainRec = [&](auto& selfChain, auto& selfMulti, AxbCompiledBindingChain& ch, int depth) -> bool {
            if (depth > 8) return false;
            std::uint8_t modeRaw = 0u, relSelf = 0u, convRaw = 0u, hasNested = 0u;
            if (!r.ReadString(ch.rawPath) ||
                !r.ReadU16(ch.targetPropertyId) ||
                !r.ReadU8(modeRaw) ||
                modeRaw > static_cast<std::uint8_t>(BindingMode::OneTime) ||
                !r.ReadValue(ch.fallbackValue) ||
                !readHopVec(ch.hops) ||
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
                    kindRaw > static_cast<std::uint8_t>(InteractionActionKind::PlaySound) ||
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
                    !readHopVec(act.commandHops) ||
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
                if (!readActionList(st.eventTriggers[ei].actions)) {
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
                if (!readActionList(ct.eventTriggers[ei].actions)) {
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
        } else {
            return failStcSection("corrupted AXB4 unknown trailing section magic");
        }
    }

    pkg.PinValuesInPools();
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
// P1-3: GradientBrushRegistry, 1D LUT Baking, Linear/Radial Evaluation & Points->SVG
// ============================================================================

namespace {

constexpr std::string_view kGradientSpecPrefix = "@aero.grad:";

[[nodiscard]] float ApplySpreadMethod01(float rawT, GradientSpreadMethod spread) noexcept {
    switch (spread) {
        case GradientSpreadMethod::Repeat:
            return std::clamp(rawT - std::floor(rawT), 0.0f, 1.0f);
        case GradientSpreadMethod::Reflect: {
            const float m = std::fmod(std::fabs(rawT), 2.0f);
            return std::clamp((m > 1.0f) ? (2.0f - m) : m, 0.0f, 1.0f);
        }
        case GradientSpreadMethod::Pad:
        default:
            return std::clamp(rawT, 0.0f, 1.0f);
    }
}

[[nodiscard]] Color EvaluateStopsDirect01(
    const std::vector<GradientStop>& sortedStops,
    float clampedT01,
    float brushOpacity) noexcept {
    const float op = std::clamp(brushOpacity, 0.0f, 1.0f);
    if (sortedStops.empty()) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (sortedStops.size() == 1u || clampedT01 <= sortedStops.front().offset) {
        Color c = sortedStops.front().color;
        c.a = std::clamp(c.a * op, 0.0f, 1.0f);
        return c;
    }
    if (clampedT01 >= sortedStops.back().offset) {
        Color c = sortedStops.back().color;
        c.a = std::clamp(c.a * op, 0.0f, 1.0f);
        return c;
    }
    for (std::size_t i = 0u; i + 1u < sortedStops.size(); ++i) {
        const GradientStop& s0 = sortedStops[i];
        const GradientStop& s1 = sortedStops[i + 1u];
        if (clampedT01 >= s0.offset && clampedT01 <= s1.offset) {
            const float span = s1.offset - s0.offset;
            const float f = (span > 1e-6f) ? std::clamp((clampedT01 - s0.offset) / span, 0.0f, 1.0f) : 0.0f;
            return Color{
                s0.color.r + (s1.color.r - s0.color.r) * f,
                s0.color.g + (s1.color.g - s0.color.g) * f,
                s0.color.b + (s1.color.b - s0.color.b) * f,
                std::clamp((s0.color.a + (s1.color.a - s0.color.a) * f) * op, 0.0f, 1.0f)};
        }
    }
    Color c = sortedStops.back().color;
    c.a = std::clamp(c.a * op, 0.0f, 1.0f);
    return c;
}

void AppendCompactFloat(std::string& out, float v) {
    char buf[32]{};
    const int n = std::snprintf(buf, sizeof(buf), "%.6g", static_cast<float>(v));
    if (n > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    } else {
        out.push_back('0');
    }
}

void AppendHexByte(std::string& out, float norm01) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const auto b = static_cast<std::uint8_t>(std::clamp(norm01 * 255.0f + 0.5f, 0.0f, 255.0f));
    out.push_back(kHex[(b >> 4u) & 0x0Fu]);
    out.push_back(kHex[b & 0x0Fu]);
}

} // namespace

void BakeGradientBrushRamp(GradientBrushDesc& ioBrush) noexcept {
    std::vector<GradientStop> sorted = ioBrush.stops;
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [](const GradientStop& a, const GradientStop& b) noexcept {
            return a.offset < b.offset;
        });
    for (std::uint32_t i = 0u; i < GradientBrushDesc::kLutWidth; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(GradientBrushDesc::kLutWidth - 1u);
        const Color c = EvaluateStopsDirect01(sorted, t, ioBrush.opacity);
        const std::size_t base = static_cast<std::size_t>(i) * 4u;
        ioBrush.bakedRampRgba8[base + 0u] =
            static_cast<std::uint8_t>(std::clamp(c.r * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 1u] =
            static_cast<std::uint8_t>(std::clamp(c.g * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 2u] =
            static_cast<std::uint8_t>(std::clamp(c.b * 255.0f + 0.5f, 0.0f, 255.0f));
        ioBrush.bakedRampRgba8[base + 3u] =
            static_cast<std::uint8_t>(std::clamp(c.a * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

Color SampleGradientRampAtT(const GradientBrushDesc& brush, float rawT) noexcept {
    const float t01 = ApplySpreadMethod01(rawT, brush.spreadMethod);
    const float coord = t01 * static_cast<float>(GradientBrushDesc::kLutWidth - 1u);
    const auto idx0 = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::floor(coord)), 0, static_cast<int>(GradientBrushDesc::kLutWidth - 1u)));
    const std::uint32_t idx1 = std::min<std::uint32_t>(idx0 + 1u, GradientBrushDesc::kLutWidth - 1u);
    const float frac = std::clamp(coord - static_cast<float>(idx0), 0.0f, 1.0f);

    const std::size_t b0 = static_cast<std::size_t>(idx0) * 4u;
    const std::size_t b1 = static_cast<std::size_t>(idx1) * 4u;
    constexpr float kInv255 = 1.0f / 255.0f;
    const float r0 = static_cast<float>(brush.bakedRampRgba8[b0 + 0u]) * kInv255;
    const float g0 = static_cast<float>(brush.bakedRampRgba8[b0 + 1u]) * kInv255;
    const float bl0 = static_cast<float>(brush.bakedRampRgba8[b0 + 2u]) * kInv255;
    const float a0 = static_cast<float>(brush.bakedRampRgba8[b0 + 3u]) * kInv255;

    const float r1 = static_cast<float>(brush.bakedRampRgba8[b1 + 0u]) * kInv255;
    const float g1 = static_cast<float>(brush.bakedRampRgba8[b1 + 1u]) * kInv255;
    const float bl1 = static_cast<float>(brush.bakedRampRgba8[b1 + 2u]) * kInv255;
    const float a1 = static_cast<float>(brush.bakedRampRgba8[b1 + 3u]) * kInv255;

    return Color{
        r0 + (r1 - r0) * frac,
        g0 + (g1 - g0) * frac,
        bl0 + (bl1 - bl0) * frac,
        a0 + (a1 - a0) * frac};
}

Color EvaluateGradientBrushAtNormalizedUv(
    const GradientBrushDesc& brush,
    float normU,
    float normV,
    float quadWidthPx,
    float quadHeightPx) noexcept {
    const float sampleX = (brush.mappingMode == BrushMappingMode::Absolute)
        ? (normU * std::max(1.0f, quadWidthPx))
        : normU;
    const float sampleY = (brush.mappingMode == BrushMappingMode::Absolute)
        ? (normV * std::max(1.0f, quadHeightPx))
        : normV;

    float t = 0.0f;
    if (brush.kind == GradientBrushKind::Linear) {
        const float dx = brush.endPoint.x - brush.startPoint.x;
        const float dy = brush.endPoint.y - brush.startPoint.y;
        const float lenSq = dx * dx + dy * dy;
        if (lenSq > 1e-8f) {
            t = ((sampleX - brush.startPoint.x) * dx + (sampleY - brush.startPoint.y) * dy) / lenSq;
        }
    } else {
        const float rx = std::max(1e-4f, std::fabs(brush.radiusX));
        const float ry = std::max(1e-4f, std::fabs(brush.radiusY));
        const float qx = (sampleX - brush.center.x) / rx;
        const float qy = (sampleY - brush.center.y) / ry;
        float fx = (brush.gradientOrigin.x - brush.center.x) / rx;
        float fy = (brush.gradientOrigin.y - brush.center.y) / ry;
        const float fLenSq = fx * fx + fy * fy;
        if (fLenSq < 1e-6f) {
            t = std::sqrt(qx * qx + qy * qy);
        } else {
            if (fLenSq > 0.9801f) {
                const float invFLen = 0.99f / std::sqrt(fLenSq);
                fx *= invFLen;
                fy *= invFLen;
            }
            const float dx = qx - fx;
            const float dy = qy - fy;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= 1e-10f) {
                t = 0.0f;
            } else {
                const float a = 1.0f - (fx * fx + fy * fy);
                const float b = fx * dx + fy * dy;
                const float denom = b + std::sqrt(std::max(0.0f, b * b + a * d2));
                t = (denom > 1e-8f) ? (d2 / denom) : 1.0f;
            }
        }
    }

    return SampleGradientRampAtT(brush, t);
}

bool IsGradientBrushSpec(std::string_view raw) noexcept {
    raw = TrimWs(raw);
    return raw.size() > kGradientSpecPrefix.size() &&
           raw.substr(0u, kGradientSpecPrefix.size()) == kGradientSpecPrefix;
}

std::string SerializeGradientBrushSpec(const GradientBrushDesc& brush) {
    std::string out(kGradientSpecPrefix);
    out.push_back(brush.kind == GradientBrushKind::Radial ? 'R' : 'L');
    out.push_back(';');
    out.push_back(brush.mappingMode == BrushMappingMode::Absolute ? '1' : '0');
    out.push_back(',');
    out.push_back(static_cast<char>('0' + (static_cast<int>(brush.spreadMethod) % 3)));
    out.push_back(',');
    AppendCompactFloat(out, brush.opacity);
    out.push_back(';');
    if (brush.kind == GradientBrushKind::Linear) {
        AppendCompactFloat(out, brush.startPoint.x);
        out.push_back(',');
        AppendCompactFloat(out, brush.startPoint.y);
        out.push_back(',');
        AppendCompactFloat(out, brush.endPoint.x);
        out.push_back(',');
        AppendCompactFloat(out, brush.endPoint.y);
    } else {
        AppendCompactFloat(out, brush.center.x);
        out.push_back(',');
        AppendCompactFloat(out, brush.center.y);
        out.push_back(',');
        AppendCompactFloat(out, brush.gradientOrigin.x);
        out.push_back(',');
        AppendCompactFloat(out, brush.gradientOrigin.y);
        out.push_back(',');
        AppendCompactFloat(out, brush.radiusX);
        out.push_back(',');
        AppendCompactFloat(out, brush.radiusY);
    }
    for (const auto& s : brush.stops) {
        out.push_back(';');
        AppendCompactFloat(out, s.offset);
        out.push_back(':');
        out.push_back('#');
        AppendHexByte(out, s.color.a);
        AppendHexByte(out, s.color.r);
        AppendHexByte(out, s.color.g);
        AppendHexByte(out, s.color.b);
    }
    return out;
}

bool TryDeserializeGradientBrushSpec(std::string_view raw, GradientBrushDesc& outBrush) {
    raw = TrimWs(raw);
    if (!IsGradientBrushSpec(raw)) {
        return false;
    }
    std::string_view body = raw.substr(kGradientSpecPrefix.size());
    std::vector<std::string_view> tokens;
    std::size_t pos = 0u;
    while (pos <= body.size()) {
        const std::size_t semi = body.find(';', pos);
        if (semi == std::string_view::npos) {
            tokens.push_back(body.substr(pos));
            break;
        }
        tokens.push_back(body.substr(pos, semi - pos));
        pos = semi + 1u;
    }
    if (tokens.size() < 3u) {
        return false;
    }

    GradientBrushDesc parsed{};
    parsed.kind = (!tokens[0].empty() && (tokens[0][0] == 'R' || tokens[0][0] == 'r'))
        ? GradientBrushKind::Radial
        : GradientBrushKind::Linear;

    const auto hdrNums = ParseFloatListFast(tokens[1]);
    if (hdrNums.size() >= 1u) {
        parsed.mappingMode = (static_cast<int>(std::lround(hdrNums[0])) != 0)
            ? BrushMappingMode::Absolute
            : BrushMappingMode::RelativeToBoundingBox;
    }
    if (hdrNums.size() >= 2u) {
        const int sp = std::clamp(static_cast<int>(std::lround(hdrNums[1])), 0, 2);
        parsed.spreadMethod = static_cast<GradientSpreadMethod>(sp);
    }
    if (hdrNums.size() >= 3u) {
        parsed.opacity = std::clamp(hdrNums[2], 0.0f, 1.0f);
    }

    const auto geomNums = ParseFloatListFast(tokens[2]);
    if (parsed.kind == GradientBrushKind::Linear) {
        if (geomNums.size() >= 4u) {
            parsed.startPoint = Point{geomNums[0], geomNums[1]};
            parsed.endPoint = Point{geomNums[2], geomNums[3]};
        }
    } else {
        if (geomNums.size() >= 6u) {
            parsed.center = Point{geomNums[0], geomNums[1]};
            parsed.gradientOrigin = Point{geomNums[2], geomNums[3]};
            parsed.radiusX = geomNums[4];
            parsed.radiusY = geomNums[5];
        }
    }

    for (std::size_t i = 3u; i < tokens.size(); ++i) {
        const std::string_view st = TrimWs(tokens[i]);
        const std::size_t colon = st.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        GradientStop stop{};
        stop.offset = ParseFloat32Fast(st.substr(0u, colon), 0.0f);
        stop.color = ParseXamlColorLiteral(st.substr(colon + 1u));
        parsed.stops.push_back(stop);
    }

    BakeGradientBrushRamp(parsed);
    outBrush = std::move(parsed);
    return true;
}

GradientBrushRegistry& GradientBrushRegistry::Instance() noexcept {
    return UiResourceContext::Current().GradientBrushes();
}

std::uint32_t GradientBrushRegistry::RegisterBrush(GradientBrushDesc desc) {
    BakeGradientBrushRamp(desc);
    const std::string canonicalSpec = SerializeGradientBrushSpec(desc);
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = specToId_.find(canonicalSpec); it != specToId_.end()) {
        return it->second;
    }
    if (brushes_.size() >= kMaxBrushId) {
        return brushes_.back().brushId;
    }
    const auto newId = static_cast<std::uint32_t>(brushes_.size() + 1u);
    desc.brushId = newId;
    brushes_.push_back(std::move(desc));
    specToId_.emplace(canonicalSpec, newId);
    ++revision_;
    return newId;
}

std::uint32_t GradientBrushRegistry::RegisterOrResolveSpec(std::string_view serializedSpec) {
    const std::string key(TrimWs(serializedSpec));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = specToId_.find(key); it != specToId_.end()) {
            return it->second;
        }
    }
    GradientBrushDesc desc{};
    if (!TryDeserializeGradientBrushSpec(key, desc)) {
        return 0u;
    }
    return RegisterBrush(std::move(desc));
}

const GradientBrushDesc* GradientBrushRegistry::Find(std::uint32_t brushId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (brushId == 0u || brushId > brushes_.size()) {
        return nullptr;
    }
    return &brushes_[brushId - 1u];
}

std::size_t GradientBrushRegistry::BrushCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return brushes_.size();
}

std::uint32_t GradientBrushRegistry::Revision() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

void GradientBrushRegistry::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    brushes_.clear();
    specToId_.clear();
    ++revision_;
}

std::string ConvertPointsToSvgPath(std::string_view rawPoints, bool closePath) {
    const auto coords = ParseFloatListFast(rawPoints);
    if (coords.size() < 4u) {
        return {};
    }
    std::string svg = "F0 M ";
    AppendCompactFloat(svg, coords[0]);
    svg.push_back(',');
    AppendCompactFloat(svg, coords[1]);
    const std::size_t pairCount = coords.size() / 2u;
    for (std::size_t i = 1u; i < pairCount; ++i) {
        svg.append(" L ");
        AppendCompactFloat(svg, coords[i * 2u]);
        svg.push_back(',');
        AppendCompactFloat(svg, coords[i * 2u + 1u]);
    }
    if (closePath) {
        svg.append(" Z");
    }
    return svg;
}


// ============================================================================
// P1-5: Real Rive v7 LEB128 Binary .riv Stream Decoder -> AxbPackage + MOT6
// ============================================================================

namespace {

enum class RivePropBacking : std::uint8_t {
    VarUint = 0,
    StringOrBytes = 1,
    Float32 = 2,
    Color32 = 3,
    Unknown = 0xFFu,
};

[[nodiscard]] RivePropBacking LookupRiveCoreBacking(std::uint32_t propKey) noexcept {
    switch (propKey) {
        // String / ByteSpan properties
        case 4u:
        case 55u:
        case 138u:
        case 203u:
        case 212u:
        case 246u:
        case 280u:
            return RivePropBacking::StringOrBytes;

        // 32-bit ARGB Color properties
        case 37u:
        case 38u:
        case 88u:
            return RivePropBacking::Color32;

        // 32-bit IEEE-754 Float properties
        case 7u: case 8u: case 9u: case 10u: case 11u: case 12u:
        case 13u: case 14u: case 15u: case 16u: case 17u: case 18u:
        case 20u: case 21u:
        case 24u: case 25u: case 26u:
        case 31u:
        case 33u: case 34u: case 35u: case 36u:
        case 39u:
        case 42u: case 43u: case 44u: case 45u: case 46u:
        case 47u:
        case 58u:
        case 63u: case 64u: case 65u: case 66u:
        case 70u:
        case 79u: case 80u: case 81u:
        case 82u: case 83u:
        case 84u: case 85u: case 86u: case 87u:
        case 89u: case 90u: case 91u:
        case 96u: case 97u: case 98u: case 99u: case 100u: case 101u:
        case 104u: case 105u: case 106u: case 107u: case 108u: case 109u:
        case 114u: case 115u: case 116u:
        case 123u: case 124u:
        case 126u: case 127u:
        case 140u:
        case 157u:
        case 161u: case 162u: case 163u:
        case 165u: case 166u:
        case 172u:
        case 177u:
        case 183u: case 184u: case 185u: case 193u: case 194u:
        case 202u:
        case 207u: case 208u:
        case 228u:
            return RivePropBacking::Float32;

        // LEB128 VarUint / Bool / Id / Enum properties
        case 3u: case 5u: case 6u:
        case 23u: case 32u: case 40u: case 41u:
        case 48u: case 49u: case 50u: case 51u: case 53u:
        case 56u: case 57u: case 59u: case 60u: case 61u: case 62u:
        case 67u: case 68u: case 69u:
        case 92u: case 93u: case 94u: case 95u:
        case 102u: case 103u:
        case 110u: case 111u: case 112u: case 113u:
        case 117u: case 118u: case 119u: case 120u: case 121u: case 122u:
        case 125u: case 128u: case 129u:
        case 131u: case 136u: case 141u: case 149u: case 150u:
        case 151u: case 152u: case 153u: case 154u: case 155u: case 156u:
        case 158u: case 160u: case 164u: case 167u: case 168u:
        case 173u: case 174u: case 175u:
        case 178u: case 179u: case 180u: case 181u: case 182u:
        case 186u: case 187u: case 188u: case 189u: case 190u: case 191u: case 192u:
        case 195u: case 196u: case 197u: case 198u: case 199u: case 200u: case 201u:
        case 204u: case 205u: case 206u:
        case 211u: case 224u: case 225u: case 226u: case 227u: case 229u:
        case 236u: case 237u: case 240u:
            return RivePropBacking::VarUint;

        default:
            return RivePropBacking::Unknown;
    }
}

struct RiveStreamReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;

    [[nodiscard]] bool ReadVarUint(std::uint64_t& out) noexcept {
        out = 0;
        unsigned shift = 0u;
        while (pos < size && shift < 64u) {
            const std::uint8_t b = data[pos++];
            out |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0u) {
                return true;
            }
            shift += 7u;
        }
        return false;
    }

    [[nodiscard]] bool ReadU32LE(std::uint32_t& out) noexcept {
        if (pos + 4u > size) {
            return false;
        }
        out = static_cast<std::uint32_t>(data[pos + 0u]) |
              (static_cast<std::uint32_t>(data[pos + 1u]) << 8u) |
              (static_cast<std::uint32_t>(data[pos + 2u]) << 16u) |
              (static_cast<std::uint32_t>(data[pos + 3u]) << 24u);
        pos += 4u;
        return true;
    }

    [[nodiscard]] bool ReadF32LE(float& out) noexcept {
        std::uint32_t bits = 0u;
        if (!ReadU32LE(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }

    [[nodiscard]] bool ReadSpan(const std::uint8_t*& outPtr, std::size_t& outLen) noexcept {
        std::uint64_t len64 = 0u;
        if (!ReadVarUint(len64) || len64 > (size - pos)) {
            return false;
        }
        outPtr = data + pos;
        outLen = static_cast<std::size_t>(len64);
        pos += outLen;
        return true;
    }
};

struct RiveDecodedProp {
    std::uint32_t key = 0u;
    RivePropBacking kind = RivePropBacking::VarUint;
    std::uint64_t uVal = 0u;
    float fVal = 0.0f;
    std::string sVal;
};

struct RiveDecodedObject {
    std::uint32_t typeKey = 0u;
    std::vector<RiveDecodedProp> props;

    [[nodiscard]] bool HasProp(std::uint32_t key) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return true;
        }
        return false;
    }

    [[nodiscard]] std::uint64_t GetU64(std::uint32_t key, std::uint64_t fallback = 0u) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return p.uVal;
        }
        return fallback;
    }

    [[nodiscard]] float GetF32(std::uint32_t key, float fallback = 0.0f) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) {
                if (p.kind == RivePropBacking::Float32) return p.fVal;
                return static_cast<float>(p.uVal);
            }
        }
        return fallback;
    }

    [[nodiscard]] std::string_view GetStr(std::uint32_t key) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) return p.sVal;
        }
        return {};
    }

    [[nodiscard]] Color GetArgbColor(std::uint32_t key, Color fallback = Color{1.0f, 1.0f, 1.0f, 1.0f}) const noexcept {
        for (const auto& p : props) {
            if (p.key == key) {
                const auto argb = static_cast<std::uint32_t>(p.uVal);
                const float a = static_cast<float>((argb >> 24u) & 0xFFu) * (1.0f / 255.0f);
                const float r = static_cast<float>((argb >> 16u) & 0xFFu) * (1.0f / 255.0f);
                const float g = static_cast<float>((argb >> 8u) & 0xFFu) * (1.0f / 255.0f);
                const float b = static_cast<float>(argb & 0xFFu) * (1.0f / 255.0f);
                return Color{r, g, b, a};
            }
        }
        return fallback;
    }
};

struct RiveDecodedArtboard {
    std::vector<RiveDecodedObject> objects;
};

void AppendSvgNumber(std::string& out, float v) {
    if (std::isnan(v) || std::isinf(v)) {
        out.push_back('0');
        return;
    }
    if (v < 0.0f) {
        if (-v < 0.0005f) {
            out.push_back('0');
            return;
        }
        out.push_back('-');
        v = -v;
    }
    const auto scaled = static_cast<std::int64_t>(v * 1000.0f + 0.5f);
    const std::int64_t intPart = scaled / 1000;
    const std::int64_t fracPart = scaled % 1000;
    char buf[24]{};
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), intPart);
    (void)ec;
    out.append(buf, static_cast<std::size_t>(ptr - buf));
    if (fracPart > 0) {
        out.push_back('.');
        char fbuf[3] = {
            static_cast<char>('0' + (fracPart / 100) % 10),
            static_cast<char>('0' + (fracPart / 10) % 10),
            static_cast<char>('0' + fracPart % 10)};
        std::size_t flen = 3u;
        while (flen > 0u && fbuf[flen - 1u] == '0') {
            --flen;
        }
        out.append(fbuf, flen);
    }
}

void AppendSvgPt(std::string& out, float x, float y) {
    AppendSvgNumber(out, x);
    out.push_back(' ');
    AppendSvgNumber(out, y);
}

struct RiveCubicVertex {
    float x = 0.0f;
    float y = 0.0f;
    float inX = 0.0f;
    float inY = 0.0f;
    float outX = 0.0f;
    float outY = 0.0f;
    bool hasIn = false;
    bool hasOut = false;
};

[[nodiscard]] std::string BuildRivePathSvg(
    const RiveDecodedArtboard& ab,
    std::size_t pathObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap) {
    const RiveDecodedObject& pathObj = ab.objects[pathObjIdx];
    const std::uint32_t tk = pathObj.typeKey;
    std::string svg;

    // PointsPath (typeKey = 16)
    if (tk == 16u) {
        const bool isClosed = (pathObj.GetU64(32u, 0u) != 0u);
        std::vector<RiveCubicVertex> verts;
        for (std::size_t childIdx : childrenMap[pathObjIdx]) {
            const RiveDecodedObject& vo = ab.objects[childIdx];
            const std::uint32_t vtk = vo.typeKey;
            if (vtk != 5u && vtk != 6u && vtk != 34u && vtk != 35u) {
                continue;
            }
            RiveCubicVertex cv{};
            cv.x = vo.GetF32(24u, 0.0f);
            cv.y = vo.GetF32(25u, 0.0f);
            cv.inX = cv.x;
            cv.inY = cv.y;
            cv.outX = cv.x;
            cv.outY = cv.y;

            if (vtk == 6u) {
                // CubicDetachedVertex
                const float inRot = vo.GetF32(84u, 0.0f);
                const float inDist = vo.GetF32(85u, 0.0f);
                const float outRot = vo.GetF32(86u, 0.0f);
                const float outDist = vo.GetF32(87u, 0.0f);
                if (std::fabs(inDist) > 1e-4f) {
                    cv.inX = cv.x + std::cos(inRot) * inDist;
                    cv.inY = cv.y + std::sin(inRot) * inDist;
                    cv.hasIn = true;
                }
                if (std::fabs(outDist) > 1e-4f) {
                    cv.outX = cv.x + std::cos(outRot) * outDist;
                    cv.outY = cv.y + std::sin(outRot) * outDist;
                    cv.hasOut = true;
                }
            } else if (vtk == 35u) {
                // CubicMirroredVertex
                const float rot = vo.GetF32(82u, 0.0f);
                const float dist = vo.GetF32(83u, 0.0f);
                if (std::fabs(dist) > 1e-4f) {
                    const float dx = std::cos(rot) * dist;
                    const float dy = std::sin(rot) * dist;
                    cv.outX = cv.x + dx;
                    cv.outY = cv.y + dy;
                    cv.inX = cv.x - dx;
                    cv.inY = cv.y - dy;
                    cv.hasIn = true;
                    cv.hasOut = true;
                }
            } else if (vtk == 34u) {
                // CubicAsymmetricVertex
                const float rot = vo.GetF32(79u, 0.0f);
                const float inDist = vo.GetF32(80u, 0.0f);
                const float outDist = vo.GetF32(81u, 0.0f);
                const float c = std::cos(rot);
                const float s = std::sin(rot);
                if (std::fabs(inDist) > 1e-4f) {
                    cv.inX = cv.x - c * inDist;
                    cv.inY = cv.y - s * inDist;
                    cv.hasIn = true;
                }
                if (std::fabs(outDist) > 1e-4f) {
                    cv.outX = cv.x + c * outDist;
                    cv.outY = cv.y + s * outDist;
                    cv.hasOut = true;
                }
            }
            verts.push_back(cv);
        }

        if (verts.size() < 2u) {
            return {};
        }
        svg.reserve(verts.size() * 28u);
        svg.push_back('M');
        AppendSvgPt(svg, verts[0].x, verts[0].y);

        const std::size_t segCount = isClosed ? verts.size() : (verts.size() - 1u);
        for (std::size_t i = 0u; i < segCount; ++i) {
            const RiveCubicVertex& a = verts[i];
            const RiveCubicVertex& b = verts[(i + 1u) % verts.size()];
            if (a.hasOut || b.hasIn) {
                svg.push_back('C');
                AppendSvgPt(svg, a.outX, a.outY);
                svg.push_back(' ');
                AppendSvgPt(svg, b.inX, b.inY);
                svg.push_back(' ');
                AppendSvgPt(svg, b.x, b.y);
            } else {
                svg.push_back('L');
                AppendSvgPt(svg, b.x, b.y);
            }
        }
        if (isClosed) {
            svg.push_back('Z');
        }
        return svg;
    }

    // Ellipse (typeKey = 4)
    if (tk == 4u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float cx = (0.5f - ox) * w;
        const float cy = (0.5f - oy) * h;
        const float rx = w * 0.5f;
        const float ry = h * 0.5f;
        constexpr float kKappa = 0.55228475f;
        const float kx = rx * kKappa;
        const float ky = ry * kKappa;

        svg.reserve(160u);
        svg.push_back('M');
        AppendSvgPt(svg, cx, cy - ry);
        svg.push_back('C');
        AppendSvgPt(svg, cx + kx, cy - ry); svg.push_back(' ');
        AppendSvgPt(svg, cx + rx, cy - ky); svg.push_back(' ');
        AppendSvgPt(svg, cx + rx, cy);
        svg.push_back('C');
        AppendSvgPt(svg, cx + rx, cy + ky); svg.push_back(' ');
        AppendSvgPt(svg, cx + kx, cy + ry); svg.push_back(' ');
        AppendSvgPt(svg, cx, cy + ry);
        svg.push_back('C');
        AppendSvgPt(svg, cx - kx, cy + ry); svg.push_back(' ');
        AppendSvgPt(svg, cx - rx, cy + ky); svg.push_back(' ');
        AppendSvgPt(svg, cx - rx, cy);
        svg.push_back('C');
        AppendSvgPt(svg, cx - rx, cy - ky); svg.push_back(' ');
        AppendSvgPt(svg, cx - kx, cy - ry); svg.push_back(' ');
        AppendSvgPt(svg, cx, cy - ry);
        svg.push_back('Z');
        return svg;
    }

    // Rectangle (typeKey = 7)
    if (tk == 7u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float x0 = -ox * w;
        const float y0 = -oy * h;
        const float x1 = x0 + w;
        const float y1 = y0 + h;

        const float maxR = 0.5f * std::min(w, h);
        const float tl = std::clamp(pathObj.GetF32(31u, 0.0f), 0.0f, maxR);
        const bool linked = (pathObj.GetU64(164u, 1u) != 0u);
        const float tr = std::clamp(linked ? tl : pathObj.GetF32(161u, tl), 0.0f, maxR);
        const float bl = std::clamp(linked ? tl : pathObj.GetF32(162u, tl), 0.0f, maxR);
        const float br = std::clamp(linked ? tl : pathObj.GetF32(163u, tl), 0.0f, maxR);

        svg.reserve(192u);
        if (tl <= 1e-3f && tr <= 1e-3f && br <= 1e-3f && bl <= 1e-3f) {
            svg.push_back('M'); AppendSvgPt(svg, x0, y0);
            svg.push_back('L'); AppendSvgPt(svg, x1, y0);
            svg.push_back('L'); AppendSvgPt(svg, x1, y1);
            svg.push_back('L'); AppendSvgPt(svg, x0, y1);
            svg.push_back('Z');
            return svg;
        }
        constexpr float kK = 0.55228475f;
        svg.push_back('M'); AppendSvgPt(svg, x0 + tl, y0);
        svg.push_back('L'); AppendSvgPt(svg, x1 - tr, y0);
        if (tr > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x1 - tr * (1.0f - kK), y0); svg.push_back(' ');
            AppendSvgPt(svg, x1, y0 + tr * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x1, y0 + tr);
        }
        svg.push_back('L'); AppendSvgPt(svg, x1, y1 - br);
        if (br > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x1, y1 - br * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x1 - br * (1.0f - kK), y1); svg.push_back(' ');
            AppendSvgPt(svg, x1 - br, y1);
        }
        svg.push_back('L'); AppendSvgPt(svg, x0 + bl, y1);
        if (bl > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x0 + bl * (1.0f - kK), y1); svg.push_back(' ');
            AppendSvgPt(svg, x0, y1 - bl * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x0, y1 - bl);
        }
        svg.push_back('L'); AppendSvgPt(svg, x0, y0 + tl);
        if (tl > 1e-3f) {
            svg.push_back('C');
            AppendSvgPt(svg, x0, y0 + tl * (1.0f - kK)); svg.push_back(' ');
            AppendSvgPt(svg, x0 + tl * (1.0f - kK), y0); svg.push_back(' ');
            AppendSvgPt(svg, x0 + tl, y0);
        }
        svg.push_back('Z');
        return svg;
    }

    // Triangle (8), Polygon (51), Star (52)
    if (tk == 8u || tk == 51u || tk == 52u) {
        const float w = pathObj.GetF32(20u, 0.0f);
        const float h = pathObj.GetF32(21u, 0.0f);
        if (w <= 1e-3f || h <= 1e-3f) {
            return {};
        }
        const float ox = pathObj.GetF32(123u, 0.5f);
        const float oy = pathObj.GetF32(124u, 0.5f);
        const float cx = (0.5f - ox) * w;
        const float cy = (0.5f - oy) * h;
        const float rx = w * 0.5f;
        const float ry = h * 0.5f;
        const std::uint32_t pts = (tk == 8u) ? 3u : std::max<std::uint32_t>(3u, static_cast<std::uint32_t>(pathObj.GetU64(125u, 5u)));
        const bool isStar = (tk == 52u);
        const float innerRatio = isStar ? std::clamp(pathObj.GetF32(127u, 0.5f), 0.05f, 1.0f) : 1.0f;
        const std::uint32_t totalVerts = isStar ? (pts * 2u) : pts;
        constexpr float kHalfPi = 1.57079632679f;
        constexpr float kTwoPi = 6.28318530718f;
        for (std::uint32_t i = 0u; i < totalVerts; ++i) {
            const float angle = -kHalfPi + (kTwoPi * static_cast<float>(i)) / static_cast<float>(totalVerts);
            const float rScale = (isStar && (i & 1u) != 0u) ? innerRatio : 1.0f;
            const float px = cx + std::cos(angle) * rx * rScale;
            const float py = cy + std::sin(angle) * ry * rScale;
            svg.push_back(i == 0u ? 'M' : 'L');
            AppendSvgPt(svg, px, py);
        }
        svg.push_back('Z');
        return svg;
    }

    return {};
}

struct RiveResolvedPaint {
    bool hasFill = false;
    Color fillColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool hasStroke = false;
    Color strokeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float strokeThickness = 0.0f;
};

[[nodiscard]] bool ResolveRivePaintNodeColor(
    const RiveDecodedArtboard& ab,
    std::size_t paintObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap,
    Color& outColor) noexcept {
    if (ab.objects[paintObjIdx].GetU64(41u, 1u) == 0u) {
        return false;
    }
    for (std::size_t cIdx : childrenMap[paintObjIdx]) {
        const RiveDecodedObject& co = ab.objects[cIdx];
        if (co.typeKey == 18u) {
            // SolidColor
            Color col = co.GetArgbColor(37u, Color{1.0f, 1.0f, 1.0f, 1.0f});
            const float op = std::clamp(co.GetF32(18u, 1.0f), 0.0f, 1.0f);
            col.a *= op;
            if (col.a > 1e-4f) {
                outColor = col;
                return true;
            }
        } else if (co.typeKey == 22u || co.typeKey == 17u) {
            // LinearGradient (22) or RadialGradient (17): average stops weighted by alpha
            float sumR = 0.0f;
            float sumG = 0.0f;
            float sumB = 0.0f;
            float sumA = 0.0f;
            std::size_t stopCount = 0u;
            for (std::size_t sIdx : childrenMap[cIdx]) {
                const RiveDecodedObject& stopObj = ab.objects[sIdx];
                if (stopObj.typeKey == 19u) {
                    const Color sc = stopObj.GetArgbColor(38u, Color{1.0f, 1.0f, 1.0f, 1.0f});
                    sumR += sc.r;
                    sumG += sc.g;
                    sumB += sc.b;
                    sumA += sc.a;
                    ++stopCount;
                }
            }
            if (stopCount > 0u) {
                const float invN = 1.0f / static_cast<float>(stopCount);
                const float gradOp = std::clamp(
                    co.HasProp(46u) ? co.GetF32(46u, 1.0f) : co.GetF32(36u, 1.0f),
                    0.0f,
                    1.0f);
                Color avg{sumR * invN, sumG * invN, sumB * invN, (sumA * invN) * gradOp};
                if (avg.a > 1e-4f) {
                    outColor = avg;
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] RiveResolvedPaint ResolveRiveShapePaints(
    const RiveDecodedArtboard& ab,
    std::size_t shapeObjIdx,
    const std::vector<std::vector<std::size_t>>& childrenMap) noexcept {
    RiveResolvedPaint res{};
    for (std::size_t cIdx : childrenMap[shapeObjIdx]) {
        const RiveDecodedObject& co = ab.objects[cIdx];
        if (co.typeKey == 20u) {
            Color fc{};
            if (ResolveRivePaintNodeColor(ab, cIdx, childrenMap, fc)) {
                res.hasFill = true;
                res.fillColor = fc;
            }
        } else if (co.typeKey == 24u) {
            Color sc{};
            const float thick = co.GetF32(47u, 1.0f);
            if (thick > 1e-3f && ResolveRivePaintNodeColor(ab, cIdx, childrenMap, sc)) {
                res.hasStroke = true;
                res.strokeColor = sc;
                res.strokeThickness = thick;
            }
        }
    }
    return res;
}

[[nodiscard]] constexpr bool IsRiveHierarchyNodeType(std::uint32_t tk) noexcept {
    return tk == 2u || tk == 3u || tk == 40u || tk == 41u || tk == 92u || tk == 100u;
}

[[nodiscard]] constexpr bool IsRiveGeometryPathType(std::uint32_t tk) noexcept {
    return tk == 16u || tk == 4u || tk == 7u || tk == 8u || tk == 51u || tk == 52u;
}

} // namespace

bool IsRiveBinaryStream(
    const std::uint8_t* rawBytes,
    std::size_t byteLength) noexcept {
    if (!rawBytes || byteLength < 6u) {
        return false;
    }
    if (std::memcmp(rawBytes, "RIVE", 4u) != 0) {
        return false;
    }
    std::size_t idx = 4u;
    while (idx < byteLength &&
           (rawBytes[idx] == ' ' || rawBytes[idx] == '\t' ||
            rawBytes[idx] == '\r' || rawBytes[idx] == '\n')) {
        ++idx;
    }
    if (idx < byteLength && (rawBytes[idx] == '{' || rawBytes[idx] == '[')) {
        return false;
    }
    RiveStreamReader reader{rawBytes, byteLength, 4u};
    std::uint64_t majorVer = 0u;
    if (!reader.ReadVarUint(majorVer)) {
        return false;
    }
    return majorVer >= 6u && majorVer <= 32u;
}

bool DecodeRiveBinaryToAxbPackage(
    const std::uint8_t* rawBytes,
    std::size_t byteLength,
    AxbPackage& outPackage,
    std::vector<std::string>* outDiagnostics,
    std::string_view artboardNameHint) {
    if (!IsRiveBinaryStream(rawBytes, byteLength)) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_BAD_HEADER: Invalid Rive binary stream header");
        }
        return false;
    }

    RiveStreamReader r{rawBytes, byteLength, 4u};
    std::uint64_t majorVer = 0u;
    std::uint64_t minorVer = 0u;
    std::uint64_t fileId = 0u;
    if (!r.ReadVarUint(majorVer) || !r.ReadVarUint(minorVer) || !r.ReadVarUint(fileId)) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_TRUNCATED_HEADER: Truncated Rive version/fileId header");
        }
        return false;
    }

    // 1. Decode Table of Contents (ToC): LEB128 property keys terminated by 0,
    //    followed by 2-bit backing types packed 4 properties per uint32_t LE (Rive v7 currentBit == 8 rule).
    std::vector<std::uint32_t> tocKeys;
    while (true) {
        std::uint64_t key64 = 0u;
        if (!r.ReadVarUint(key64)) {
            if (outDiagnostics) {
                outDiagnostics->push_back("AERO_RIVE_TRUNCATED_TOC: Truncated Rive ToC property key table");
            }
            return false;
        }
        if (key64 == 0u) {
            break;
        }
        tocKeys.push_back(static_cast<std::uint32_t>(key64));
    }

    std::vector<std::pair<std::uint32_t, RivePropBacking>> tocBacking;
    tocBacking.reserve(tocKeys.size());
    std::uint32_t curBit = 8u;
    std::uint32_t curWord = 0u;
    for (std::uint32_t k : tocKeys) {
        if (curBit == 8u) {
            if (!r.ReadU32LE(curWord)) {
                if (outDiagnostics) {
                    outDiagnostics->push_back("AERO_RIVE_TRUNCATED_TOC_BITS: Truncated Rive ToC bit table");
                }
                return false;
            }
            curBit = 0u;
        }
        const auto btRaw = static_cast<std::uint8_t>((curWord >> curBit) & 0x3u);
        curBit += 2u;
        tocBacking.emplace_back(k, static_cast<RivePropBacking>(btRaw));
    }

    auto resolvePropBacking = [&](std::uint32_t propKey) noexcept -> RivePropBacking {
        for (const auto& kv : tocBacking) {
            if (kv.first == propKey) {
                return kv.second;
            }
        }
        return LookupRiveCoreBacking(propKey);
    };

    // 2. Decode Core object stream grouped into Artboards (typeKey = 1)
    std::vector<RiveDecodedArtboard> artboards;
    while (r.pos < r.size) {
        std::uint64_t typeKey64 = 0u;
        if (!r.ReadVarUint(typeKey64)) {
            if (outDiagnostics) {
                outDiagnostics->push_back("AERO_RIVE_TRUNCATED_OBJECT: Truncated object typeKey");
            }
            return false;
        }
        if (typeKey64 == 0u) {
            break;
        }

        RiveDecodedObject obj{};
        obj.typeKey = static_cast<std::uint32_t>(typeKey64);
        while (true) {
            std::uint64_t propKey64 = 0u;
            if (!r.ReadVarUint(propKey64)) {
                if (outDiagnostics) {
                    outDiagnostics->push_back("AERO_RIVE_TRUNCATED_PROP: Truncated property key");
                }
                return false;
            }
            if (propKey64 == 0u) {
                break;
            }
            const auto pk = static_cast<std::uint32_t>(propKey64);
            const RivePropBacking bt = resolvePropBacking(pk);
            if (bt == RivePropBacking::Unknown) {
                if (outDiagnostics) {
                    outDiagnostics->push_back(
                        "AERO_RIVE_UNKNOWN_PROP: Unknown Rive property key " + std::to_string(pk));
                }
                return false;
            }

            RiveDecodedProp dp{};
            dp.key = pk;
            dp.kind = bt;
            if (bt == RivePropBacking::VarUint) {
                if (!r.ReadVarUint(dp.uVal)) return false;
            } else if (bt == RivePropBacking::StringOrBytes) {
                const std::uint8_t* ptr = nullptr;
                std::size_t len = 0u;
                if (!r.ReadSpan(ptr, len)) return false;
                // Skip copying embedded binary image blobs (prop 212 = FileAssetContents.bytes)
                if (pk != 212u && len > 0u) {
                    dp.sVal.assign(reinterpret_cast<const char*>(ptr), len);
                }
            } else if (bt == RivePropBacking::Float32) {
                if (!r.ReadF32LE(dp.fVal)) return false;
            } else if (bt == RivePropBacking::Color32) {
                std::uint32_t c32 = 0u;
                if (!r.ReadU32LE(c32)) return false;
                dp.uVal = c32;
            }
            obj.props.push_back(std::move(dp));
        }

        if (obj.typeKey == 1u) {
            RiveDecodedArtboard ab{};
            ab.objects.push_back(std::move(obj));
            artboards.push_back(std::move(ab));
        } else if (!artboards.empty()) {
            artboards.back().objects.push_back(std::move(obj));
        }
    }

    if (artboards.empty()) {
        if (outDiagnostics) {
            outDiagnostics->push_back("AERO_RIVE_NO_ARTBOARD: Rive stream contains no Artboard");
        }
        return false;
    }

    std::size_t mainAbIdx = 0u;
    if (!artboardNameHint.empty()) {
        for (std::size_t i = 0u; i < artboards.size(); ++i) {
            if (artboards[i].objects[0].GetStr(4u) == artboardNameHint) {
                mainAbIdx = i;
                break;
            }
        }
    }

    const RiveDecodedArtboard& mainAb = artboards[mainAbIdx];
    const RiveDecodedObject& abRootObj = mainAb.objects[0];
    const std::string_view abName = abRootObj.GetStr(4u);
    const float abW = std::max(1.0f, abRootObj.GetF32(7u, 500.0f));
    const float abH = std::max(1.0f, abRootObj.GetF32(8u, 500.0f));
    const float abOriginX = abRootObj.GetF32(11u, 0.0f);
    const float abOriginY = abRootObj.GetF32(12u, 0.0f);

    AxbPackage pkg{};
    pkg.InternString("");
    pkg.rootTypeName = "Canvas";
    if (!abName.empty()) {
        pkg.className = std::string(abName);
    }

    std::uint32_t nextLocalElemIdx = 0u;
    auto pushStaticProp = [&](std::uint16_t propId, UnifiedValue val) {
        pkg.instructions.push_back(AxbInstruction{
            AxbOpcode::SetStaticProperty, propId, 0, 0, 0, 0, std::move(val)});
    };

    // Emit root Artboard Canvas (localElementIdx = 0)
    pkg.instructions.push_back(AxbInstruction{
        AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(abName), 0, {}});
    ++nextLocalElemIdx;
    pushStaticProp(PropId_Width, MakeFloat(abW));
    pushStaticProp(PropId_Height, MakeFloat(abH));
    pushStaticProp(PropId_ClipToBounds, UnifiedValue{abRootObj.GetU64(196u, 1u) != 0u});

    if (abRootObj.GetU64(41u, 0u) != 0u) {
        // Optional Artboard solid fill child if present
        for (std::size_t oi = 1u; oi < mainAb.objects.size(); ++oi) {
            if (mainAb.objects[oi].typeKey == 20u && mainAb.objects[oi].GetU64(5u, 0xFFFFFFFFu) == 0u) {
                for (std::size_t ci = 1u; ci < mainAb.objects.size(); ++ci) {
                    if (mainAb.objects[ci].typeKey == 18u && mainAb.objects[ci].GetU64(5u, 0xFFFFFFFFu) == oi) {
                        const Color bg = mainAb.objects[ci].GetArgbColor(37u);
                        if (bg.a > 1e-4f) {
                            pushStaticProp(PropId_Background, value::FromColor(bg));
                        }
                    }
                }
            }
        }
    }

    // Count total candidate paths across artboards to pick optimal MTSDF cell size (fast baking on huge scenes)
    std::size_t estimatedPathCount = 0u;
    for (const auto& o : mainAb.objects) {
        if (IsRiveGeometryPathType(o.typeKey)) {
            ++estimatedPathCount;
        }
    }
    const bool compactBakeMode = (estimatedPathCount > 36u);

    std::vector<std::pair<std::uint64_t, Rect>> bakedPathBoundsCache;
    auto ensurePathBaked = [&](std::string_view svgData, Rect& outBounds) -> bool {
        if (svgData.empty()) {
            return false;
        }
        const std::uint64_t h = ComputeSvgPathHash(svgData);
        if (h == 0u) {
            return false;
        }
        for (const auto& kv : bakedPathBoundsCache) {
            if (kv.first == h) {
                outBounds = kv.second;
                return (outBounds.width > 1e-3f && outBounds.height > 1e-3f);
            }
        }
        float recRange = 4.0f;
        std::uint16_t cellDim = compactBakeMode
            ? static_cast<std::uint16_t>(24u)
            : SelectAdaptiveMtsdfCellSize(svgData, 32.0f, &recRange);
        if (compactBakeMode) {
            recRange = 3.0f;
        }
        const auto slotIdx = static_cast<std::uint16_t>(pkg.mtsdfPaths.size());
        AxbMtsdfPathEntry entry = BakeSvgPathToMtsdfEntry(svgData, cellDim, recRange, slotIdx, 512u);
        outBounds = Rect{entry.boundsX, entry.boundsY, entry.boundsW, entry.boundsH};
        pkg.mtsdfPaths.push_back(std::move(entry));
        bakedPathBoundsCache.emplace_back(h, outBounds);
        return (outBounds.width > 1e-3f && outBounds.height > 1e-3f);
    };

    // Map main artboard object index -> emitted AxbPackage localElementIdx (0xFFFFFFFFu if unmapped)
    std::vector<std::uint32_t> mainObjToElemIdx(mainAb.objects.size(), 0xFFFFFFFFu);
    mainObjToElemIdx[0] = 0u;

    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

    auto emitArtboardSubtree = [&](auto& selfRef,
                                   std::size_t abIndex,
                                   float rootOffsetX,
                                   float rootOffsetY,
                                   int nestingDepth,
                                   bool recordMainIndices) -> void {
        const RiveDecodedArtboard& ab = artboards[abIndex];
        std::vector<std::vector<std::size_t>> childrenMap(ab.objects.size());
        for (std::size_t i = 1u; i < ab.objects.size(); ++i) {
            const std::uint64_t pid = ab.objects[i].GetU64(5u, 0xFFFFFFFFULL);
            if (pid < ab.objects.size()) {
                childrenMap[static_cast<std::size_t>(pid)].push_back(i);
            }
        }

        auto emitNodeRecursive = [&](auto& nodeRec, std::size_t objIdx, bool parentIsArtboardRoot) -> void {
            const RiveDecodedObject& obj = ab.objects[objIdx];
            const std::uint32_t tk = obj.typeKey;
            if (!IsRiveHierarchyNodeType(tk)) {
                return;
            }

            const std::string_view nodeName = recordMainIndices ? obj.GetStr(4u) : std::string_view{};
            const std::uint32_t myElemIdx = nextLocalElemIdx++;
            if (recordMainIndices && objIdx < mainObjToElemIdx.size()) {
                mainObjToElemIdx[objIdx] = myElemIdx;
            }

            pkg.instructions.push_back(AxbInstruction{
                AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(nodeName), 0, {}});
            if (!nodeName.empty() && recordMainIndices) {
                pkg.namedControls.push_back(AxbNamedControlSlot{
                    std::string(nodeName), "Canvas", 0, PropertySlotHandle::InvalidIndex});
            }
            pushStaticProp(PropId_Width, MakeFloat(1.0f));
            pushStaticProp(PropId_Height, MakeFloat(1.0f));
            pushStaticProp(PropId_HorizontalAlignment, value::FromInt(1)); // Left
            pushStaticProp(PropId_VerticalAlignment, value::FromInt(1));   // Top

            float tx = (tk == 41u) ? obj.GetF32(90u, 0.0f) : obj.GetF32(13u, 0.0f);
            float ty = (tk == 41u) ? obj.GetF32(91u, 0.0f) : obj.GetF32(14u, 0.0f);
            if (tk == 40u && !obj.HasProp(13u)) {
                // Child Bone defaults x to parent Bone's length along +X
                const std::uint64_t pid = obj.GetU64(5u, 0xFFFFFFFFULL);
                if (pid < ab.objects.size() &&
                    (ab.objects[static_cast<std::size_t>(pid)].typeKey == 40u ||
                     ab.objects[static_cast<std::size_t>(pid)].typeKey == 41u)) {
                    tx = ab.objects[static_cast<std::size_t>(pid)].GetF32(89u, 0.0f);
                }
            }
            if (parentIsArtboardRoot) {
                tx += rootOffsetX;
                ty += rootOffsetY;
            }
            const float rotDeg = obj.GetF32(15u, 0.0f) * kRadToDeg;
            const float sx = obj.GetF32(16u, 1.0f);
            const float sy = obj.GetF32(17u, 1.0f);
            const float op = std::clamp(obj.GetF32(18u, 1.0f), 0.0f, 1.0f);

            if (std::fabs(tx) > 1e-4f) pushStaticProp(PropId_AnimTranslateX, MakeFloat(tx));
            if (std::fabs(ty) > 1e-4f) pushStaticProp(PropId_AnimTranslateY, MakeFloat(ty));
            if (std::fabs(rotDeg) > 1e-4f) pushStaticProp(PropId_AnimRotateAngle, MakeFloat(rotDeg));
            if (std::fabs(sx - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleX, MakeFloat(sx));
            if (std::fabs(sy - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleY, MakeFloat(sy));
            if (std::fabs(op - 1.0f) > 1e-4f) pushStaticProp(PropId_Opacity, MakeFloat(op));

            // If Shape (typeKey = 3), emit MTSDF <Path> elements for all geometry path children
            if (tk == 3u) {
                const RiveResolvedPaint paint = ResolveRiveShapePaints(ab, objIdx, childrenMap);
                if (paint.hasFill || paint.hasStroke) {
                    for (std::size_t cIdx : childrenMap[objIdx]) {
                        const RiveDecodedObject& pathObj = ab.objects[cIdx];
                        if (!IsRiveGeometryPathType(pathObj.typeKey)) {
                            continue;
                        }
                        const std::string svgPath = BuildRivePathSvg(ab, cIdx, childrenMap);
                        Rect pathBounds{};
                        if (!ensurePathBaked(svgPath, pathBounds)) {
                            continue;
                        }

                        const float px = pathObj.GetF32(13u, 0.0f);
                        const float py = pathObj.GetF32(14u, 0.0f);
                        const float pRotDeg = pathObj.GetF32(15u, 0.0f) * kRadToDeg;
                        const float psx = pathObj.GetF32(16u, 1.0f);
                        const float psy = pathObj.GetF32(17u, 1.0f);
                        const bool hasPathLocalTf =
                            (std::fabs(px) > 1e-4f) || (std::fabs(py) > 1e-4f) ||
                            (std::fabs(pRotDeg) > 1e-4f) || (std::fabs(psx - 1.0f) > 1e-4f) ||
                            (std::fabs(psy - 1.0f) > 1e-4f);

                        if (hasPathLocalTf) {
                            const std::uint32_t wrapIdx = nextLocalElemIdx++;
                            if (recordMainIndices && cIdx < mainObjToElemIdx.size()) {
                                mainObjToElemIdx[cIdx] = wrapIdx;
                            }
                            pkg.instructions.push_back(AxbInstruction{
                                AxbOpcode::BeginElement, 0, 0, pkg.InternString("Canvas"), pkg.InternString(""), 0, {}});
                            pushStaticProp(PropId_Width, MakeFloat(1.0f));
                            pushStaticProp(PropId_Height, MakeFloat(1.0f));
                            if (std::fabs(px) > 1e-4f) pushStaticProp(PropId_AnimTranslateX, MakeFloat(px));
                            if (std::fabs(py) > 1e-4f) pushStaticProp(PropId_AnimTranslateY, MakeFloat(py));
                            if (std::fabs(pRotDeg) > 1e-4f) pushStaticProp(PropId_AnimRotateAngle, MakeFloat(pRotDeg));
                            if (std::fabs(psx - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleX, MakeFloat(psx));
                            if (std::fabs(psy - 1.0f) > 1e-4f) pushStaticProp(PropId_AnimScaleY, MakeFloat(psy));
                        }

                        const std::uint32_t pathElemIdx = nextLocalElemIdx++;
                        if (recordMainIndices && cIdx < mainObjToElemIdx.size() && !hasPathLocalTf) {
                            mainObjToElemIdx[cIdx] = pathElemIdx;
                        }
                        const std::string_view pName = recordMainIndices ? pathObj.GetStr(4u) : std::string_view{};
                        pkg.instructions.push_back(AxbInstruction{
                            AxbOpcode::BeginElement, 0, 0, pkg.InternString("Path"), pkg.InternString(pName), 0, {}});
                        pushStaticProp(PropId_CanvasLeft, MakeFloat(pathBounds.x));
                        pushStaticProp(PropId_CanvasTop, MakeFloat(pathBounds.y));
                        pushStaticProp(PropId_Width, MakeFloat(std::max(1.0f, pathBounds.width)));
                        pushStaticProp(PropId_Height, MakeFloat(std::max(1.0f, pathBounds.height)));
                        pushStaticProp(PropId_Stretch, value::FromInt(1)); // Stretch::Fill (exact 1:1 with boundsW x boundsH)
                        pushStaticProp(PropId_PathData, value::FromString(svgPath));
                        if (paint.hasFill) {
                            pushStaticProp(PropId_Background, value::FromColor(paint.fillColor));
                        }
                        if (paint.hasStroke) {
                            pushStaticProp(PropId_BorderBrush, value::FromColor(paint.strokeColor));
                            pushStaticProp(PropId_BorderThickness, value::FromThickness(Thickness{paint.strokeThickness}));
                        }
                        pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

                        if (hasPathLocalTf) {
                            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
                        }
                    }
                }
            }

            // If NestedArtboard (typeKey = 92), recursively inline target artboard visuals
            if (tk == 92u && nestingDepth < 2) {
                const auto nestedAbId = static_cast<std::size_t>(obj.GetU64(197u, 0xFFFFFFFFULL));
                if (nestedAbId < artboards.size() && nestedAbId != abIndex) {
                    const RiveDecodedObject& nestedRoot = artboards[nestedAbId].objects[0];
                    const float nw = std::max(1.0f, nestedRoot.GetF32(7u, 100.0f));
                    const float nh = std::max(1.0f, nestedRoot.GetF32(8u, 100.0f));
                    const float nox = nestedRoot.GetF32(11u, 0.0f);
                    const float noy = nestedRoot.GetF32(12u, 0.0f);
                    selfRef(selfRef, nestedAbId, -nox * nw, -noy * nh, nestingDepth + 1, false);
                }
            }

            // Recurse into child hierarchy nodes in exact draw order
            for (std::size_t cIdx : childrenMap[objIdx]) {
                if (IsRiveHierarchyNodeType(ab.objects[cIdx].typeKey)) {
                    nodeRec(nodeRec, cIdx, false);
                }
            }

            pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});
        };

        for (std::size_t cIdx : childrenMap[0]) {
            if (IsRiveHierarchyNodeType(ab.objects[cIdx].typeKey)) {
                emitNodeRecursive(emitNodeRecursive, cIdx, true);
            }
        }
    };

    emitArtboardSubtree(
        emitArtboardSubtree,
        mainAbIdx,
        abOriginX * abW,
        abOriginY * abH,
        0,
        true);

    // Close root Artboard Canvas
    pkg.instructions.push_back(AxbInstruction{AxbOpcode::EndElement});

    if (!pkg.mtsdfPaths.empty()) {
        const std::uint16_t targetAtlas = (pkg.mtsdfPaths.size() > 64u)
            ? static_cast<std::uint16_t>(1024u)
            : static_cast<std::uint16_t>(512u);
        PackMtsdfAtlasSkyline(pkg.mtsdfPaths, targetAtlas);
    }

    // 3. Extract TransformConstraints (typeKey = 87, 88, 89)
    for (std::size_t i = 1u; i < mainAb.objects.size(); ++i) {
        const RiveDecodedObject& co = mainAb.objects[i];
        if (co.typeKey == 87u || co.typeKey == 88u || co.typeKey == 89u) {
            const auto constrainedObjId = static_cast<std::size_t>(co.GetU64(5u, 0xFFFFFFFFULL));
            const auto targetObjId = static_cast<std::size_t>(co.GetU64(173u, 0xFFFFFFFFULL));
            if (constrainedObjId < mainObjToElemIdx.size() &&
                targetObjId < mainObjToElemIdx.size()) {
                const std::uint32_t cElem = mainObjToElemIdx[constrainedObjId];
                const std::uint32_t tElem = mainObjToElemIdx[targetObjId];
                if (cElem != 0xFFFFFFFFu && tElem != 0xFFFFFFFFu && cElem != tElem) {
                    AxbTransformConstraintDesc tc{};
                    tc.childLocalElementIdx = static_cast<std::uint16_t>(cElem);
                    tc.parentLocalElementIdx = static_cast<std::uint16_t>(tElem);
                    pkg.transformConstraints.push_back(tc);
                }
            }
        }
    }

    // 4. Decode LinearAnimations (typeKey = 31) -> MOT6 Clips, Tracks & Keyframes
    auto mapRiveKeyedProperty = [](std::uint32_t rivePropKey, bool& outConvertRadToDeg) noexcept -> std::uint16_t {
        outConvertRadToDeg = false;
        switch (rivePropKey) {
            case 13u:
            case 90u:
                return PropId_AnimTranslateX;
            case 14u:
            case 91u:
                return PropId_AnimTranslateY;
            case 15u:
                outConvertRadToDeg = true;
                return PropId_AnimRotateAngle;
            case 16u:
                return PropId_AnimScaleX;
            case 17u:
                return PropId_AnimScaleY;
            case 18u:
                return PropId_Opacity;
            case 20u:
                return PropId_Width;
            case 21u:
                return PropId_Height;
            case 37u:
            case 38u:
                return PropId_Background;
            case 47u:
                return PropId_BorderThickness;
            default:
                return 0xFFFFu;
        }
    };

    for (std::size_t idx = 1u; idx < mainAb.objects.size();) {
        if (mainAb.objects[idx].typeKey != 31u) {
            ++idx;
            continue;
        }
        const RiveDecodedObject& animObj = mainAb.objects[idx];
        const std::string_view animName = animObj.GetStr(55u);
        const float fps = std::max(1.0f, static_cast<float>(animObj.GetU64(56u, 60u)));
        const std::uint64_t numFrames = animObj.GetU64(57u, 60u);
        const std::uint64_t loopVal = animObj.GetU64(59u, 0u);
        float maxKfTime = (numFrames > 0u) ? (static_cast<float>(numFrames) / fps) : 1.0f;

        AxbMotionClipDesc clip{};
        clip.nameStrIdx = pkg.InternString(animName);
        clip.isLooping = (loopVal == 1u || loopVal == 2u);
        clip.autoReverse = (loopVal == 2u);
        clip.firstTrackIdx = static_cast<std::uint32_t>(pkg.motionTracks.size());
        clip.firstEventIdx = static_cast<std::uint32_t>(pkg.motionEvents.size());

        std::uint32_t currentTargetElemIdx = 0xFFFFFFFFu;
        AxbMotionTrackDesc activeTrack{};
        bool hasActiveTrack = false;
        bool activeConvertRad = false;

        auto flushActiveTrack = [&]() {
            if (!hasActiveTrack) return;
            activeTrack.keyframeCount =
                static_cast<std::uint32_t>(pkg.motionKeyframes.size()) - activeTrack.firstKeyframeIdx;
            if (activeTrack.keyframeCount > 0u) {
                pkg.motionTracks.push_back(activeTrack);
            }
            hasActiveTrack = false;
        };

        ++idx;
        while (idx < mainAb.objects.size()) {
            const RiveDecodedObject& sub = mainAb.objects[idx];
            if (sub.typeKey == 31u || sub.typeKey == 53u || sub.typeKey == 1u) {
                break;
            }
            if (sub.typeKey == 25u) {
                // KeyedObject
                flushActiveTrack();
                const auto objId = static_cast<std::size_t>(sub.GetU64(51u, 0xFFFFFFFFULL));
                if (objId < mainObjToElemIdx.size()) {
                    currentTargetElemIdx = mainObjToElemIdx[objId];
                    // If paint child (SolidColor) is keyed, resolve to parent Shape/Path element
                    if (currentTargetElemIdx == 0xFFFFFFFFu && objId < mainAb.objects.size()) {
                        std::size_t walk = objId;
                        for (int hop = 0; hop < 4 && walk < mainAb.objects.size(); ++hop) {
                            if (mainObjToElemIdx[walk] != 0xFFFFFFFFu) {
                                currentTargetElemIdx = mainObjToElemIdx[walk];
                                break;
                            }
                            const auto p = static_cast<std::size_t>(mainAb.objects[walk].GetU64(5u, 0xFFFFFFFFULL));
                            if (p >= mainAb.objects.size()) break;
                            walk = p;
                        }
                    }
                } else {
                    currentTargetElemIdx = 0xFFFFFFFFu;
                }
            } else if (sub.typeKey == 26u) {
                // KeyedProperty
                flushActiveTrack();
                const auto rivePropKey = static_cast<std::uint32_t>(sub.GetU64(53u, 0u));
                bool convRad = false;
                const std::uint16_t mappedProp = mapRiveKeyedProperty(rivePropKey, convRad);
                if (currentTargetElemIdx != 0xFFFFFFFFu && mappedProp != 0xFFFFu) {
                    activeTrack = AxbMotionTrackDesc{};
                    activeTrack.localElementIdx = static_cast<std::uint16_t>(currentTargetElemIdx);
                    activeTrack.targetPropId = mappedProp;
                    activeTrack.channelCount = (mappedProp == PropId_Background || mappedProp == PropId_BorderThickness) ? 4u : 1u;
                    activeTrack.firstKeyframeIdx = static_cast<std::uint32_t>(pkg.motionKeyframes.size());
                    activeConvertRad = convRad;
                    hasActiveTrack = true;
                }
            } else if ((sub.typeKey == 30u || sub.typeKey == 37u) && hasActiveTrack) {
                // KeyFrameDouble (30) or KeyFrameColor (37)
                AxbFlatKeyframe kf{};
                const auto frameIdx = sub.GetU64(67u, 0u);
                kf.timeSec = static_cast<float>(frameIdx) / fps;
                maxKfTime = std::max(maxKfTime, kf.timeSec);

                if (sub.typeKey == 37u) {
                    const Color kc = sub.GetArgbColor(88u, Color{1.0f, 1.0f, 1.0f, 1.0f});
                    kf.value = kc.ToFloat4();
                } else {
                    float val = sub.GetF32(70u, 0.0f);
                    if (activeConvertRad) {
                        val *= kRadToDeg;
                    }
                    if (activeTrack.targetPropId == PropId_BorderThickness) {
                        kf.value = Float4Value{val, val, val, val};
                    } else {
                        kf.value = Float4Value{val, 0.0f, 0.0f, 0.0f};
                    }
                }

                const std::uint64_t interpType = sub.GetU64(68u, 1u);
                if (interpType == 0u) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::HoldStep);
                } else if (interpType == 2u) {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::CubicBezier);
                    float x1 = 0.42f, y1 = 0.0f, x2 = 0.58f, y2 = 1.0f;
                    const auto interpId = static_cast<std::size_t>(sub.GetU64(69u, 0xFFFFFFFFULL));
                    if (interpId < mainAb.objects.size() && mainAb.objects[interpId].typeKey == 28u) {
                        const RiveDecodedObject& eo = mainAb.objects[interpId];
                        x1 = std::clamp(eo.GetF32(63u, 0.42f), 0.0f, 1.0f);
                        y1 = std::clamp(eo.GetF32(64u, 0.0f), -4.0f, 4.0f);
                        x2 = std::clamp(eo.GetF32(65u, 0.58f), 0.0f, 1.0f);
                        y2 = std::clamp(eo.GetF32(66u, 1.0f), -4.0f, 4.0f);
                    }
                    kf.easeKindOrCtrlX1 = static_cast<std::uint8_t>(x1 * 255.0f + 0.5f);
                    kf.easeModeOrCtrlX2 = static_cast<std::uint8_t>(x2 * 255.0f + 0.5f);
                    kf.ctrlY1Fixed8 = static_cast<std::int16_t>(std::lround(y1 * 256.0f));
                    kf.ctrlY2Fixed8 = static_cast<std::int16_t>(std::lround(y2 * 256.0f));
                } else {
                    kf.interp = static_cast<std::uint8_t>(CurveInterpMode::Linear);
                }
                pkg.motionKeyframes.push_back(kf);
            }
            ++idx;
        }
        flushActiveTrack();

        clip.durationSec = std::max(0.016f, maxKfTime);
        clip.trackCount = static_cast<std::uint32_t>(pkg.motionTracks.size()) - clip.firstTrackIdx;
        clip.eventCount = static_cast<std::uint32_t>(pkg.motionEvents.size()) - clip.firstEventIdx;
        pkg.motionClips.push_back(clip);
    }

    // 5. Decode StateMachines (typeKey = 53) -> MOT6 StateMachines, Inputs, Layers, States & Transitions
    for (std::size_t idx = 1u; idx < mainAb.objects.size();) {
        if (mainAb.objects[idx].typeKey != 53u) {
            ++idx;
            continue;
        }
        const RiveDecodedObject& smObj = mainAb.objects[idx];
        AxbStateMachineDesc sm{};
        sm.nameStrIdx = pkg.InternString(smObj.GetStr(55u));
        sm.firstLayerIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        sm.firstListenerIdx = static_cast<std::uint32_t>(pkg.smListeners.size());

        const std::uint32_t smFirstInputSlot = static_cast<std::uint32_t>(pkg.contractInputs.size());
        std::vector<std::uint8_t> smInputKinds; // 0=Bool, 1=Float, 5=Trigger

        ++idx;
        // Parse StateMachine inputs (56=Number, 58=Trigger, 59=Bool) and Layers (57)
        while (idx < mainAb.objects.size() && mainAb.objects[idx].typeKey != 53u && mainAb.objects[idx].typeKey != 1u) {
            const RiveDecodedObject& item = mainAb.objects[idx];
            if (item.typeKey == 56u || item.typeKey == 58u || item.typeKey == 59u) {
                const auto globalSlot = static_cast<std::uint16_t>(pkg.contractInputs.size());
                AxbContractInputDesc inDesc{};
                inDesc.nameStrIdx = pkg.InternString(item.GetStr(138u));
                inDesc.bridgePropId = static_cast<std::uint16_t>(PropId_FirstContractInput + globalSlot);
                if (item.typeKey == 59u) {
                    inDesc.valueKind = 0u;
                    inDesc.defaultValue = UnifiedValue{item.GetU64(141u, 0u) != 0u};
                    smInputKinds.push_back(0u);
                } else if (item.typeKey == 58u) {
                    inDesc.valueKind = 5u;
                    inDesc.isTrigger = 1u;
                    inDesc.defaultValue = MakeFloat(0.0f);
                    smInputKinds.push_back(5u);
                } else {
                    inDesc.valueKind = 1u;
                    inDesc.defaultValue = MakeFloat(item.GetF32(140u, 0.0f));
                    smInputKinds.push_back(1u);
                }
                pkg.contractInputs.push_back(std::move(inDesc));
                ++idx;
                continue;
            }

            if (item.typeKey == 57u) {
                // StateMachineLayer
                AxbSmLayerDesc layer{};
                layer.nameStrIdx = pkg.InternString(item.GetStr(138u));
                layer.blendMode = SmLayerBlendMode::Override;
                layer.layerWeight = 1.0f;
                layer.firstStateIdx = static_cast<std::uint32_t>(pkg.smStates.size());
                layer.firstTransitionIdx = static_cast<std::uint32_t>(pkg.smTransitions.size());

                struct RawRiveState {
                    std::uint32_t tk = 0u;
                    std::uint16_t emittedPlayableIdx = 0xFFFFu;
                    struct RawTransition {
                        std::size_t toRawStateIdx = 0u;
                        float durationSec = 0.1f;
                        bool hasExitTime = false;
                        float exitTime01 = 1.0f;
                        std::uint32_t firstCondIdx = 0u;
                        std::uint16_t condCount = 0u;
                    };
                    std::vector<RawTransition> transitions;
                };
                std::vector<RawRiveState> rawStates;

                ++idx;
                while (idx < mainAb.objects.size()) {
                    const RiveDecodedObject& lo = mainAb.objects[idx];
                    if (lo.typeKey == 57u || lo.typeKey == 53u || lo.typeKey == 1u ||
                        lo.typeKey == 114u || lo.typeKey == 115u) {
                        break;
                    }
                    // LayerState subclasses: 61=AnimationState, 62=AnyState, 63=EntryState, 64=ExitState, 73/76=BlendState
                    if (lo.typeKey == 61u || lo.typeKey == 62u || lo.typeKey == 63u ||
                        lo.typeKey == 64u || lo.typeKey == 73u || lo.typeKey == 76u) {
                        RawRiveState rs{};
                        rs.tk = lo.typeKey;
                        if (lo.typeKey == 61u || lo.typeKey == 73u || lo.typeKey == 76u) {
                            const auto clipId = static_cast<std::uint16_t>(lo.GetU64(149u, 0u));
                            AxbSmState st{};
                            st.nameStrIdx = pkg.InternString(
                                clipId < pkg.motionClips.size()
                                    ? pkg.GetString(pkg.motionClips[clipId].nameStrIdx)
                                    : "State");
                            st.kind = SmStateKind::SingleClip;
                            st.primaryClipIdx = (clipId < pkg.motionClips.size()) ? clipId : 0xFFFFu;
                            st.speedMultiplier = 1.0f;
                            rs.emittedPlayableIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                            pkg.smStates.push_back(st);
                        } else if (lo.typeKey == 64u) {
                            AxbSmState st{};
                            st.nameStrIdx = pkg.InternString("Exit");
                            st.kind = SmStateKind::ExitState;
                            rs.emittedPlayableIdx = static_cast<std::uint16_t>(pkg.smStates.size());
                            pkg.smStates.push_back(st);
                        }
                        rawStates.push_back(std::move(rs));
                        ++idx;
                        continue;
                    }

                    if (lo.typeKey == 65u && !rawStates.empty()) {
                        // StateTransition
                        RawRiveState::RawTransition rt{};
                        rt.toRawStateIdx = static_cast<std::size_t>(lo.GetU64(151u, 0u));
                        rt.durationSec = static_cast<float>(lo.GetU64(158u, 0u)) * 0.001f;
                        const std::uint64_t trFlags = lo.GetU64(152u, 0u);
                        if ((trFlags & 0x2u) != 0u) {
                            rt.hasExitTime = true;
                            rt.exitTime01 = std::clamp(static_cast<float>(lo.GetU64(160u, 100u)) * 0.01f, 0.0f, 1.0f);
                        }
                        rt.firstCondIdx = static_cast<std::uint32_t>(pkg.smConditions.size());

                        ++idx;
                        while (idx < mainAb.objects.size()) {
                            const RiveDecodedObject& co = mainAb.objects[idx];
                            if (co.typeKey != 68u && co.typeKey != 70u && co.typeKey != 71u) {
                                break;
                            }
                            const auto localInId = static_cast<std::size_t>(co.GetU64(155u, 0u));
                            if (localInId < smInputKinds.size()) {
                                AxbSmCondition cond{};
                                cond.inputSlotIdx = static_cast<std::uint16_t>(smFirstInputSlot + localInId);
                                const std::uint8_t ik = smInputKinds[localInId];
                                const std::uint64_t opVal = co.GetU64(156u, 0u);
                                if (ik == 5u || co.typeKey == 68u) {
                                    cond.op = SmCompareOp::TriggerFired;
                                } else if (ik == 0u || co.typeKey == 71u) {
                                    cond.op = (opVal == 1u) ? SmCompareOp::BoolFalse : SmCompareOp::BoolTrue;
                                } else {
                                    // Rive TransitionValueCondition opValue: 0==, 1!=, 2<, 3<=, 4>, 5>=
                                    switch (opVal) {
                                        case 0u: cond.op = SmCompareOp::Equal; break;
                                        case 1u: cond.op = SmCompareOp::NotEqual; break;
                                        case 2u: cond.op = SmCompareOp::Less; break;
                                        case 3u: cond.op = SmCompareOp::LessOrEqual; break;
                                        case 4u: cond.op = SmCompareOp::Greater; break;
                                        case 5u: cond.op = SmCompareOp::GreaterOrEqual; break;
                                        default: cond.op = SmCompareOp::Equal; break;
                                    }
                                    cond.threshold = co.GetF32(157u, 0.0f);
                                }
                                pkg.smConditions.push_back(cond);
                                ++rt.condCount;
                            }
                            ++idx;
                        }
                        rawStates.back().transitions.push_back(rt);
                        continue;
                    }
                    ++idx;
                }

                // Resolve EntryState default transition & playable state transitions
                std::uint16_t resolvedEntryGlobal = static_cast<std::uint16_t>(layer.firstStateIdx);
                for (const auto& rs : rawStates) {
                    if (rs.tk == 63u && !rs.transitions.empty()) {
                        const std::size_t targetRaw = rs.transitions[0].toRawStateIdx;
                        if (targetRaw < rawStates.size() && rawStates[targetRaw].emittedPlayableIdx != 0xFFFFu) {
                            resolvedEntryGlobal = rawStates[targetRaw].emittedPlayableIdx;
                        }
                    }
                }
                for (const auto& rs : rawStates) {
                    if (rs.tk == 63u) continue;
                    const std::uint16_t fromIdx = (rs.tk == 62u)
                        ? static_cast<std::uint16_t>(0xFFFFu)
                        : static_cast<std::uint16_t>(rs.emittedPlayableIdx - layer.firstStateIdx);
                    if (rs.tk != 62u && rs.emittedPlayableIdx == 0xFFFFu) continue;

                    for (const auto& rt : rs.transitions) {
                        if (rt.toRawStateIdx >= rawStates.size()) continue;
                        const std::uint16_t toGlobal = rawStates[rt.toRawStateIdx].emittedPlayableIdx;
                        if (toGlobal == 0xFFFFu) continue;
                        AxbSmTransition tr{};
                        tr.fromStateIdx = fromIdx;
                        tr.toStateIdx = static_cast<std::uint16_t>(toGlobal - layer.firstStateIdx);
                        tr.crossfadeDurationSec = rt.durationSec;
                        tr.hasExitTime = rt.hasExitTime;
                        tr.exitTimeNormalized01 = rt.exitTime01;
                        tr.firstConditionIdx = rt.firstCondIdx;
                        tr.conditionCount = rt.condCount;
                        pkg.smTransitions.push_back(tr);
                    }
                }

                layer.stateCount = static_cast<std::uint16_t>(pkg.smStates.size() - layer.firstStateIdx);
                layer.transitionCount = static_cast<std::uint16_t>(pkg.smTransitions.size() - layer.firstTransitionIdx);
                if (layer.stateCount > 0u) {
                    layer.entryStateIdx = (resolvedEntryGlobal >= layer.firstStateIdx &&
                                           resolvedEntryGlobal < layer.firstStateIdx + layer.stateCount)
                        ? static_cast<std::uint16_t>(resolvedEntryGlobal - layer.firstStateIdx)
                        : static_cast<std::uint16_t>(0u);
                    pkg.smLayers.push_back(layer);
                }
                continue;
            }

            ++idx;
        }

        sm.layerCount = static_cast<std::uint16_t>(pkg.smLayers.size() - sm.firstLayerIdx);
        sm.listenerCount = static_cast<std::uint16_t>(pkg.smListeners.size() - sm.firstListenerIdx);
        if (sm.layerCount > 0u) {
            pkg.stateMachines.push_back(sm);
        }
    }

    // If the .riv file contains timeline animations but no explicit StateMachine (e.g. leaves-*.riv),
    // synthesize a default single-layer StateMachine playing clip 0 automatically.
    if (pkg.stateMachines.empty() && !pkg.motionClips.empty()) {
        AxbSmState defState{};
        defState.nameStrIdx = pkg.motionClips[0].nameStrIdx;
        defState.kind = SmStateKind::SingleClip;
        defState.primaryClipIdx = 0u;
        defState.speedMultiplier = 1.0f;
        const auto firstStIdx = static_cast<std::uint32_t>(pkg.smStates.size());
        pkg.smStates.push_back(defState);

        AxbSmLayerDesc defLayer{};
        defLayer.nameStrIdx = pkg.InternString("DefaultTimeline");
        defLayer.blendMode = SmLayerBlendMode::Override;
        defLayer.entryStateIdx = 0u;
        defLayer.layerWeight = 1.0f;
        defLayer.firstStateIdx = firstStIdx;
        defLayer.stateCount = 1u;
        const auto firstLyIdx = static_cast<std::uint32_t>(pkg.smLayers.size());
        pkg.smLayers.push_back(defLayer);

        AxbStateMachineDesc defSm{};
        defSm.nameStrIdx = pkg.InternString("AutoPlayback");
        defSm.firstLayerIdx = firstLyIdx;
        defSm.layerCount = 1u;
        pkg.stateMachines.push_back(defSm);
    }

    pkg.PinValuesInPools();
    outPackage = std::move(pkg);
    return true;
}

} // namespace aero
