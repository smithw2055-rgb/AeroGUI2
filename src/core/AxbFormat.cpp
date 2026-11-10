#include "aero/compiler/AxbFormat.hpp"

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
    if (propertyName == "Width" || propertyName == "d:DesignWidth") return PropId_Width;
    if (propertyName == "Height" || propertyName == "d:DesignHeight") return PropId_Height;
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
    if (propertyName == "IsChecked" || propertyName == "IsSelected" || propertyName == "IsExpanded") return PropId_IsChecked;
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
    return PropId_Tag;
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
            return MakeFloat(ParseFloat32Fast(rawValue));

        case PropId_IsChecked:
        case PropId_ClipToBounds:
            return EqualsIgnoreCase(rawValue, "true") || rawValue == "1";

        case PropId_LastChildFill:
            return !EqualsIgnoreCase(rawValue, "false") && rawValue != "0";

        case PropId_Visibility:
            if (rawValue == "Collapsed" || rawValue == "2") return value::FromInt(2);
            if (rawValue == "Hidden" || rawValue == "1") return value::FromInt(1);
            return value::FromInt(0);

        case PropId_HorizontalAlignment:
            if (EqualsIgnoreCase(rawValue, "Left") || rawValue == "1") return value::FromInt(1);
            if (EqualsIgnoreCase(rawValue, "Center") || rawValue == "2") return value::FromInt(2);
            if (EqualsIgnoreCase(rawValue, "Right") || rawValue == "3") return value::FromInt(3);
            return value::FromInt(0); // Stretch

        case PropId_VerticalAlignment:
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
        case PropId_SelectedIndex: {
            std::int64_t iv = 0;
            (void)std::from_chars(rawValue.data(), rawValue.data() + rawValue.size(), iv);
            return value::FromInt(iv);
        }

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

Size MeasureUtf8TextRun(std::string_view utf8Text, float fontSize) noexcept {
    if (utf8Text.empty()) {
        return Size{0.0f, 0.0f};
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

    // 3. Bind multi-hop PropertySlotHandles in bindingChains
    for (auto& chain : bindingChains) {
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
        std::uint32_t mtsdfMagic = 0;
        std::uint32_t mtsdfCount = 0;
        if (!r.ReadU32(mtsdfMagic) || mtsdfMagic != kAxb4MtsdfSectionMagic ||
            !r.ReadU32(mtsdfCount) || mtsdfCount > (r.size - r.offset) / 56u) {
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

} // namespace aero
