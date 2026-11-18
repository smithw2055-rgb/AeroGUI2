#include "aero/asset/MtsdfPathBaker.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace detail {

void SkipSvgDelimiters(std::string_view s, std::size_t& pos) noexcept {
    while (pos < s.size() &&
           (s[pos] == ',' || str::detail::IsAsciiSpace(s[pos]))) {
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
    const std::size_t start = pos;
    if (s[pos] == '+' || s[pos] == '-') {
        ++pos;
    }
    bool seenDot = false;
    bool seenDigit = false;
    bool seenExp = false;
    while (pos < s.size()) {
        const char c = s[pos];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            ++pos;
        } else if (c == '.' && !seenDot && !seenExp) {
            seenDot = true;
            ++pos;
        } else if (!seenExp && (c == 'e' || c == 'E') && seenDigit) {
            std::size_t expPos = pos + 1u;
            if (expPos < s.size() && (s[expPos] == '+' || s[expPos] == '-')) {
                ++expPos;
            }
            if (expPos < s.size() && std::isdigit(static_cast<unsigned char>(s[expPos]))) {
                seenExp = true;
                pos = expPos + 1u;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    if (!seenDigit) {
        pos = start;
        return false;
    }
    outVal = str::ParseFloat32Or(s.substr(start, pos - start), 0.0f);
    return true;
}

float ParseFloat32Token(std::string_view text, float fallback) noexcept {
    text = str::Trim(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        text.remove_suffix(1u);
    }
    return str::ParseFloat32Or(text, fallback);
}

std::vector<float> ParseFloatList(std::string_view text) {
    std::vector<float> nums;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (text[pos] == ',' || str::detail::IsAsciiSpace(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size()) {
            break;
        }
        std::size_t end = pos;
        while (end < text.size() && text[end] != ',' &&
               !str::detail::IsAsciiSpace(text[end])) {
            ++end;
        }
        nums.push_back(ParseFloat32Token(text.substr(pos, end - pos)));
        pos = end;
    }
    return nums;
}

void AppendCompactFloat(std::string& out, float v) {
    char buf[32]{};
    const int n = std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    if (n > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    } else {
        out.push_back('0');
    }
}

} // namespace detail

namespace {

// ============================================================================
// SVG Path Mini-Language Parser & 4-Channel MTSDF Baker
// ============================================================================

struct ColoredSegment {
    Point p0{};
    Point p1{};
    std::uint8_t channelMask = 0x07u; // Bit 0 = R, Bit 1 = G, Bit 2 = B
    bool inClosedContour = true;
    bool smoothAtStart = false;
};

// SVG 1.1 / XAML StreamGeometry arc flag grammar: single '0' or '1' digit (may omit trailing separator, e.g. "a4,4 0 014,-4")
bool TryReadSvgArcFlag(std::string_view s, std::size_t& pos, bool& outFlag) noexcept {
    detail::SkipSvgDelimiters(s, pos);
    if (pos >= s.size() || (s[pos] != '0' && s[pos] != '1')) {
        return false;
    }
    outFlag = (s[pos] == '1');
    ++pos;
    return true;
}

void AppendCubicBezierPoints(
    std::vector<Point>& contour,
    Point p0,
    Point c1,
    Point c2,
    Point p3,
    int steps) {
    const int safeSteps = std::max(1, steps);
    for (int i = 1; i <= safeSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(safeSteps);
        const float u = 1.0f - t;
        const float b0 = u * u * u;
        const float b1 = 3.0f * u * u * t;
        const float b2 = 3.0f * u * t * t;
        const float b3 = t * t * t;
        contour.push_back(Point{
            b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p3.x,
            b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p3.y});
    }
}

void AppendQuadBezierPoints(
    std::vector<Point>& contour,
    Point p0,
    Point c1,
    Point p2,
    int steps) {
    const int safeSteps = std::max(1, steps);
    for (int i = 1; i <= safeSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(safeSteps);
        const float u = 1.0f - t;
        contour.push_back(Point{
            u * u * p0.x + 2.0f * u * t * c1.x + t * t * p2.x,
            u * u * p0.y + 2.0f * u * t * c1.y + t * t * p2.y});
    }
}

} // namespace

// W3C SVG 1.1 Appendix F.6.5 & F.6.6 endpoint-to-center elliptical arc tessellation
void AppendSvgArcPoints(
    Point p0,
    float rxRaw,
    float ryRaw,
    float xAxisRotDeg,
    bool largeArc,
    bool sweep,
    Point p1,
    std::vector<Point>& contour,
    float stepAngleRad,
    int minSteps,
    int maxSteps) {
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

    const float safeStepRad = std::max(1e-3f, stepAngleRad);
    const int safeMin = std::max(1, minSteps);
    const int safeMax = std::max(safeMin, maxSteps);
    const int steps = std::clamp(
        static_cast<int>(std::ceil(std::fabs(deltaTheta) / safeStepRad)),
        safeMin,
        safeMax);
    for (int i = 1; i <= steps; ++i) {
        if (i == steps) {
            contour.push_back(p1);
        } else {
            const float th = theta1 + deltaTheta * (static_cast<float>(i) / static_cast<float>(steps));
            const float ex = rx * std::cos(th);
            const float ey = ry * std::sin(th);
            contour.push_back(Point{
                cosPhi * ex - sinPhi * ey + cx,
                sinPhi * ex + cosPhi * ey + cy});
        }
    }
}

ParsedSvgGeometry ParseSvgPathGeometry(
    std::string_view pathData,
    const SvgPathTessellationOptions& options) {
    ParsedSvgGeometry geom{};
    std::size_t pos = 0;
    detail::SkipSvgDelimiters(pathData, pos);

    std::vector<Point> currentContour;
    std::vector<std::uint8_t> currentSmooth;
    bool currentClosed = false;
    Point cur{0.0f, 0.0f};
    Point startPt{0.0f, 0.0f};
    Point prevCtrl{0.0f, 0.0f};
    char cmd = 0;
    char prevCmd = 0;

    auto flushContour = [&]() {
        if (currentContour.size() >= 2u) {
            if (!currentClosed && currentContour.size() >= 4u) {
                const Point first = currentContour.front();
                const Point last = currentContour.back();
                if (std::fabs(first.x - last.x) <= 1e-4f && std::fabs(first.y - last.y) <= 1e-4f) {
                    currentClosed = true;
                }
            }
            currentSmooth.resize(currentContour.size(), 0u);
            geom.contours.push_back(SvgPathContour{
                std::move(currentContour),
                std::move(currentSmooth),
                currentClosed});
        }
        currentContour.clear();
        currentSmooth.clear();
        currentClosed = false;
    };
    auto markSmoothAppend = [&](std::size_t firstNewIdx) {
        currentSmooth.resize(currentContour.size(), 0u);
        if (currentContour.size() > firstNewIdx + 1u) {
            for (std::size_t idx = firstNewIdx; idx + 1u < currentContour.size(); ++idx) {
                currentSmooth[idx] = 1u;
            }
        }
    };

    while (pos < pathData.size()) {
        detail::SkipSvgDelimiters(pathData, pos);
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
            case 'F':
            case 'f': {
                float ruleVal = 0.0f;
                if (detail::TryReadSvgFloat(pathData, pos, ruleVal)) {
                    geom.nonZeroFill = (static_cast<int>(ruleVal) != 0);
                }
                cmd = 0;
                break;
            }
            case 'M':
            case 'm': {
                float x = 0.0f;
                float y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                flushContour();
                cur = (cmd == 'm') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                startPt = cur;
                prevCtrl = cur;
                currentContour.push_back(cur);
                prevCmd = 'M';
                cmd = (cmd == 'm') ? 'l' : 'L';
                continue;
            }
            case 'L':
            case 'l': {
                float x = 0.0f;
                float y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                cur = (cmd == 'l') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                prevCtrl = cur;
                break;
            }
            case 'H':
            case 'h': {
                float x = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x)) {
                    cmd = 0;
                    break;
                }
                cur.x = (cmd == 'h') ? (cur.x + x) : x;
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                prevCtrl = cur;
                break;
            }
            case 'V':
            case 'v': {
                float y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                cur.y = (cmd == 'v') ? (cur.y + y) : y;
                if (currentContour.empty()) currentContour.push_back(startPt);
                currentContour.push_back(cur);
                prevCtrl = cur;
                break;
            }
            case 'C':
            case 'c': {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x1) || !detail::TryReadSvgFloat(pathData, pos, y1) ||
                    !detail::TryReadSvgFloat(pathData, pos, x2) || !detail::TryReadSvgFloat(pathData, pos, y2) ||
                    !detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const Point p0 = cur;
                const Point c1 = (cmd == 'c') ? Point{cur.x + x1, cur.y + y1} : Point{x1, y1};
                const Point c2 = (cmd == 'c') ? Point{cur.x + x2, cur.y + y2} : Point{x2, y2};
                const Point p3 = (cmd == 'c') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                currentSmooth.resize(currentContour.size(), 0u);
                const std::size_t curveStartIdx = currentContour.size();
                AppendCubicBezierPoints(currentContour, p0, c1, c2, p3, options.cubicSteps);
                markSmoothAppend(curveStartIdx);
                prevCtrl = c2;
                cur = p3;
                break;
            }
            case 'S':
            case 's': {
                float x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x2) || !detail::TryReadSvgFloat(pathData, pos, y2) ||
                    !detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const Point p0 = cur;
                const Point c1 = (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's')
                    ? Point{2.0f * cur.x - prevCtrl.x, 2.0f * cur.y - prevCtrl.y}
                    : cur;
                const Point c2 = (cmd == 's') ? Point{cur.x + x2, cur.y + y2} : Point{x2, y2};
                const Point p3 = (cmd == 's') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                currentSmooth.resize(currentContour.size(), 0u);
                const std::size_t curveStartIdx = currentContour.size();
                AppendCubicBezierPoints(currentContour, p0, c1, c2, p3, options.cubicSteps);
                markSmoothAppend(curveStartIdx);
                prevCtrl = c2;
                cur = p3;
                break;
            }
            case 'Q':
            case 'q': {
                float x1 = 0.0f, y1 = 0.0f, x = 0.0f, y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x1) || !detail::TryReadSvgFloat(pathData, pos, y1) ||
                    !detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const Point p0 = cur;
                const Point c1 = (cmd == 'q') ? Point{cur.x + x1, cur.y + y1} : Point{x1, y1};
                const Point p2 = (cmd == 'q') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                currentSmooth.resize(currentContour.size(), 0u);
                const std::size_t curveStartIdx = currentContour.size();
                AppendQuadBezierPoints(currentContour, p0, c1, p2, options.quadSteps);
                markSmoothAppend(curveStartIdx);
                prevCtrl = c1;
                cur = p2;
                break;
            }
            case 'T':
            case 't': {
                float x = 0.0f, y = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, x) || !detail::TryReadSvgFloat(pathData, pos, y)) {
                    cmd = 0;
                    break;
                }
                const Point p0 = cur;
                const Point c1 = (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't')
                    ? Point{2.0f * cur.x - prevCtrl.x, 2.0f * cur.y - prevCtrl.y}
                    : cur;
                const Point p2 = (cmd == 't') ? Point{cur.x + x, cur.y + y} : Point{x, y};
                if (currentContour.empty()) currentContour.push_back(p0);
                currentSmooth.resize(currentContour.size(), 0u);
                const std::size_t curveStartIdx = currentContour.size();
                AppendQuadBezierPoints(currentContour, p0, c1, p2, options.quadSteps);
                markSmoothAppend(curveStartIdx);
                prevCtrl = c1;
                cur = p2;
                break;
            }
            case 'A':
            case 'a': {
                float rx = 0.0f, ry = 0.0f, rot = 0.0f, ex = 0.0f, ey = 0.0f;
                bool largeArc = false;
                bool sweep = false;
                if (!detail::TryReadSvgFloat(pathData, pos, rx) ||
                    !detail::TryReadSvgFloat(pathData, pos, ry) ||
                    !detail::TryReadSvgFloat(pathData, pos, rot) ||
                    !TryReadSvgArcFlag(pathData, pos, largeArc) ||
                    !TryReadSvgArcFlag(pathData, pos, sweep) ||
                    !detail::TryReadSvgFloat(pathData, pos, ex) ||
                    !detail::TryReadSvgFloat(pathData, pos, ey)) {
                    cmd = 0;
                    break;
                }
                const Point p0 = cur;
                const Point p1 = (cmd == 'a') ? Point{cur.x + ex, cur.y + ey} : Point{ex, ey};
                if (currentContour.empty()) currentContour.push_back(p0);
                currentSmooth.resize(currentContour.size(), 0u);
                const std::size_t curveStartIdx = currentContour.size();
                AppendSvgArcPoints(
                    p0,
                    rx,
                    ry,
                    rot,
                    largeArc,
                    sweep,
                    p1,
                    currentContour,
                    options.arcStepAngleRad,
                    options.arcMinSteps,
                    options.arcMaxSteps);
                markSmoothAppend(curveStartIdx);
                cur = p1;
                prevCtrl = cur;
                break;
            }
            case 'Z':
            case 'z': {
                if (!currentContour.empty()) {
                    const Point last = currentContour.back();
                    if (std::fabs(last.x - startPt.x) > 1e-4f || std::fabs(last.y - startPt.y) > 1e-4f) {
                        currentContour.push_back(startPt);
                    }
                    currentClosed = true;
                    flushContour();
                }
                cur = startPt;
                prevCtrl = cur;
                cmd = 0;
                break;
            }
            default: {
                float dummy = 0.0f;
                if (!detail::TryReadSvgFloat(pathData, pos, dummy)) {
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

std::vector<std::vector<Point>> ParseSvgPathToSubpaths(std::string_view pathData) {
    constexpr float kPi = 3.14159265358979323846f;
    const ParsedSvgGeometry geom = ParseSvgPathGeometry(
        pathData,
        SvgPathTessellationOptions{16, 12, kPi / 16.0f, 4, 64});
    std::vector<std::vector<Point>> subpaths;
    subpaths.reserve(geom.contours.size());
    for (const auto& contour : geom.contours) {
        subpaths.push_back(contour.points);
    }
    return subpaths;
}

namespace {

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
                const bool smoothStart =
                    (i < contour.isSmoothVertex.size()) && (contour.isSmoothVertex[i] != 0u);
                segs.push_back(ColoredSegment{
                    contour.points[i],
                    contour.points[i + 1u],
                    0x07u,
                    contour.closed,
                    smoothStart});
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
                    if (!curr.smoothAtStart && (dotNorm <= 0.985f || crossNorm >= 0.14f)) {
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
            const Point a = contour.points[i];
            const Point b = contour.points[i + 1u];
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

struct CachedSvgSummary {
    Rect bounds{};
    std::size_t segCount = 0u;
    ParsedSvgGeometry geom{};
};

struct CachedBakedMtsdfCell {
    float boundsX = 0.0f;
    float boundsY = 0.0f;
    float boundsW = 0.0f;
    float boundsH = 0.0f;
    std::vector<std::uint8_t> rgbaPixels;
};

struct MtsdfProcessCache {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<const CachedSvgSummary>> geomByHash;
    std::unordered_map<std::uint64_t, CachedBakedMtsdfCell> bakedCells;
};

MtsdfProcessCache& GetMtsdfProcessCache() {
    static MtsdfProcessCache sCache;
    return sCache;
}

std::shared_ptr<const CachedSvgSummary> GetOrParseSvgGeometryCached(std::string_view pathData, std::uint64_t hash) {
    if (hash != 0u) {
        auto& cache = GetMtsdfProcessCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto it = cache.geomByHash.find(hash);
        if (it != cache.geomByHash.end()) {
            return it->second;
        }
    }
    auto summary = std::make_shared<CachedSvgSummary>();
    summary->geom = ParseSvgPathGeometry(pathData);
    summary->bounds = summary->geom.bounds;
    std::size_t segCount = 0u;
    for (const auto& contour : summary->geom.contours) {
        if (contour.points.size() >= 2u) {
            segCount += (contour.points.size() - 1u);
        }
    }
    summary->segCount = segCount;
    if (hash != 0u) {
        auto& cache = GetMtsdfProcessCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.geomByHash.size() > 4096u) {
            cache.geomByHash.clear();
        }
        cache.geomByHash.emplace(hash, summary);
    }
    return summary;
}

[[nodiscard]] std::uint64_t MakeBakedMtsdfCacheKey(
    std::uint64_t pathHash,
    std::uint16_t safeCell,
    float safeRange) noexcept {
    const auto qRange = static_cast<std::uint64_t>(std::lround(safeRange * 256.0f)) & 0xFFFFULL;
    std::uint64_t k = pathHash;
    k ^= (static_cast<std::uint64_t>(safeCell) << 16u) | qRange;
    k *= 1099511628211ULL;
    return k;
}

} // namespace

std::string ConvertPointsToSvgPath(std::string_view rawPoints, bool closePath) {
    const auto coords = detail::ParseFloatList(rawPoints);
    if (coords.size() < 4u) {
        return {};
    }
    std::string svg = "F0 M ";
    detail::AppendCompactFloat(svg, coords[0]);
    svg.push_back(',');
    detail::AppendCompactFloat(svg, coords[1]);
    const std::size_t pairCount = coords.size() / 2u;
    for (std::size_t i = 1u; i < pairCount; ++i) {
        svg.append(" L ");
        detail::AppendCompactFloat(svg, coords[i * 2u]);
        svg.push_back(',');
        detail::AppendCompactFloat(svg, coords[i * 2u + 1u]);
    }
    if (closePath) {
        svg.append(" Z");
    }
    return svg;
}

std::uint64_t ComputeSvgPathHash(std::string_view pathData) noexcept {
    pathData = str::Trim(pathData);
    std::uint64_t hash = 14695981039346656037ULL;
    bool prevWasSpace = false;
    for (const char ch : pathData) {
        if (str::detail::IsAsciiSpace(ch)) {
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
    const std::uint64_t h = ComputeSvgPathHash(pathData);
    return GetOrParseSvgGeometryCached(pathData, h)->bounds;
}

std::uint16_t SelectAdaptiveMtsdfCellSize(
    std::string_view pathData,
    float designMaxSpanPx,
    float* outRecommendedPxRange) noexcept {
    const std::uint64_t h = ComputeSvgPathHash(pathData);
    const auto summary = GetOrParseSvgGeometryCached(pathData, h);
    const std::size_t segCount = summary->segCount;
    const float span = std::max(designMaxSpanPx, std::max(summary->bounds.width, summary->bounds.height));
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
        if (initialAtlas >= 1024u || !tryPackWithSize(1024u)) {
            (void)tryPackWithSize(2048u);
        }
    }
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
    const std::uint64_t pathHash = ComputeSvgPathHash(pathData);

    AxbMtsdfPathEntry entry{};
    entry.pathHash = pathHash;
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

    const std::uint64_t cacheKey = MakeBakedMtsdfCacheKey(pathHash, safeCell, safeRange);
    if (pathHash != 0u) {
        auto& cache = GetMtsdfProcessCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto it = cache.bakedCells.find(cacheKey);
        if (it != cache.bakedCells.end()) {
            entry.boundsX = it->second.boundsX;
            entry.boundsY = it->second.boundsY;
            entry.boundsW = it->second.boundsW;
            entry.boundsH = it->second.boundsH;
            entry.mtsdfRgbaPixels = it->second.rgbaPixels;
            entry.ComputeRowBounds();
            return entry;
        }
    }

    const auto summary = GetOrParseSvgGeometryCached(pathData, pathHash);
    const ParsedSvgGeometry& geom = summary->geom;
    const std::vector<ColoredSegment> coloredSegs = BuildCornerColoredSegments(geom);

    entry.boundsX = geom.bounds.x;
    entry.boundsY = geom.bounds.y;
    entry.boundsW = geom.bounds.width;
    entry.boundsH = geom.bounds.height;
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
    const float invUniformScale = 1.0f / uniformScale;

    const float scaledW = geom.bounds.width * uniformScale;
    const float scaledH = geom.bounds.height * uniformScale;
    const float offsetX = (static_cast<float>(safeCell) - scaledW) * 0.5f - geom.bounds.x * uniformScale;
    const float offsetY = (static_cast<float>(safeCell) - scaledH) * 0.5f - geom.bounds.y * uniformScale;

    // Precompute cell-space segment geometry, AABB & midpoint orientation once (O(S))
    struct PreparedCellSegment {
        float ax = 0.0f;
        float ay = 0.0f;
        float abx = 0.0f;
        float aby = 0.0f;
        float invLenSq = 0.0f;
        float dirX = 0.0f;
        float dirY = 0.0f;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        float leftSign = 1.0f;
        std::uint32_t origIdx = 0u;
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
        const float invLenSq = 1.0f / lenSq;
        const float invLen = std::sqrt(invLenSq);
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

        const auto origIdx = static_cast<std::uint32_t>(preparedSegs.size());
        preparedSegs.push_back(PreparedCellSegment{
            ax, ay, abx, aby, invLenSq, dirX, dirY,
            std::min(ax, bx), std::max(ax, bx),
            std::min(ay, by), std::max(ay, by),
            leftSign, origIdx, seg.channelMask, seg.inClosedContour});
    }

    if (preparedSegs.empty()) {
        return entry;
    }

    // Collect closed-contour SVG edges once for fast per-row straddling-edge filtering
    struct ClosedSvgEdge {
        Point a{};
        Point b{};
    };
    std::vector<ClosedSvgEdge> closedEdges;
    if (anyClosedContour) {
        closedEdges.reserve(summary->segCount);
        for (const auto& contour : geom.contours) {
            if (!contour.closed) continue;
            for (std::size_t i = 0u; i + 1u < contour.points.size(); ++i) {
                const Point a = contour.points[i];
                const Point b = contour.points[i + 1u];
                if (a.y != b.y) {
                    closedEdges.push_back(ClosedSvgEdge{a, b});
                }
            }
        }
    }

    struct RowSegEntry {
        float rowDySq = 0.0f;
        std::uint32_t segIdx = 0u;
    };
    struct PrecomputedRowCross {
        float ax = 0.0f;
        float ay = 0.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        float dySvg = 0.0f;
        float xIntersect = 0.0f;
        bool upward = true;
    };
    std::vector<RowSegEntry> rowOrder(preparedSegs.size());
    std::vector<PrecomputedRowCross> rowCrossEdges;
    rowCrossEdges.reserve(32u);

    for (std::uint16_t py = 0; py < safeCell; ++py) {
        const float cellY = static_cast<float>(py) + 0.5f;
        const float svgY = (cellY - offsetY) * invUniformScale;

        rowCrossEdges.clear();
        if (anyClosedContour) {
            for (const auto& e : closedEdges) {
                if ((e.a.y <= svgY && e.b.y > svgY) || (e.b.y <= svgY && e.a.y > svgY)) {
                    const float dx = e.b.x - e.a.x;
                    const float dy = e.b.y - e.a.y;
                    const float dySvg = svgY - e.a.y;
                    const float vt = dySvg / dy;
                    rowCrossEdges.push_back(PrecomputedRowCross{
                        e.a.x, e.a.y, dx, dy, dySvg, e.a.x + vt * dx, (e.a.y <= svgY)});
                }
            }
        }

        for (std::size_t i = 0u; i < preparedSegs.size(); ++i) {
            const auto& s = preparedSegs[i];
            float dy = 0.0f;
            if (cellY < s.minY) dy = s.minY - cellY;
            else if (cellY > s.maxY) dy = cellY - s.maxY;
            rowOrder[i] = RowSegEntry{dy * dy, static_cast<std::uint32_t>(i)};
        }
        std::sort(rowOrder.begin(), rowOrder.end(), [](const RowSegEntry& lhs, const RowSegEntry& rhs) noexcept {
            return (lhs.rowDySq != rhs.rowDySq) ? (lhs.rowDySq < rhs.rowDySq) : (lhs.segIdx < rhs.segIdx);
        });

        for (std::uint16_t px = 0; px < safeCell; ++px) {
            const float cellX = static_cast<float>(px) + 0.5f;
            const float svgX = (cellX - offsetX) * invUniformScale;

            bool inside = false;
            if (anyClosedContour && !rowCrossEdges.empty()) {
                int winding = 0;
                int crossings = 0;
                for (const auto& e : rowCrossEdges) {
                    if (svgX < e.xIntersect) {
                        ++crossings;
                    }
                    const float side = e.dx * e.dySvg - (svgX - e.ax) * e.dy;
                    if (e.upward) {
                        if (side > 0.0f) ++winding;
                    } else {
                        if (side < 0.0f) --winding;
                    }
                }
                inside = geom.nonZeroFill ? (winding != 0) : ((crossings & 1) != 0);
            }
            const float globalSign = inside ? 1.0f : -1.0f;

            float minTrueDistSq = 1e30f;

            struct ChannelBest {
                float minDistSq = 1e30f;
                float bestOrtho = 1e30f;
                float pseudoDist = 1e30f;
                float signFactor = 1.0f;
                std::uint32_t origIdx = 0xFFFFFFFFu;
                bool inInteriorProj = false;
                bool found = false;
            };
            ChannelBest chBest[3]{};

            float upperBoundSq = 1e30f;
            for (const auto& re : rowOrder) {
                if (re.rowDySq > upperBoundSq) {
                    break;
                }
                const auto& seg = preparedSegs[re.segIdx];
                float dxBox = 0.0f;
                if (cellX < seg.minX) dxBox = seg.minX - cellX;
                else if (cellX > seg.maxX) dxBox = cellX - seg.maxX;
                if (re.rowDySq + dxBox * dxBox > upperBoundSq) {
                    continue;
                }

                const float apx = cellX - seg.ax;
                const float apy = cellY - seg.ay;
                const float proj = (apx * seg.abx + apy * seg.aby) * seg.invLenSq;
                const float clampedT = std::clamp(proj, 0.0f, 1.0f);
                const float dx = cellX - (seg.ax + clampedT * seg.abx);
                const float dy = cellY - (seg.ay + clampedT * seg.aby);
                const float distSq = dx * dx + dy * dy;
                minTrueDistSq = std::min(minTrueDistSq, distSq);

                if (!seg.inClosedContour) {
                    if (!anyClosedContour) {
                        upperBoundSq = minTrueDistSq + 1e-3f;
                    }
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
                             (orthoScore > chBest[c].bestOrtho + 1e-7f ||
                              (std::fabs(orthoScore - chBest[c].bestOrtho) <= 1e-7f &&
                               seg.origIdx < chBest[c].origIdx)));
                        if (!chBest[c].found || closer) {
                            chBest[c].found = true;
                            chBest[c].minDistSq = distSq;
                            chBest[c].bestOrtho = orthoScore;
                            chBest[c].origIdx = seg.origIdx;
                            chBest[c].inInteriorProj = (proj >= 0.0f && proj <= 1.0f);
                            chBest[c].signFactor = sampleOnLeft * seg.leftSign;
                            chBest[c].pseudoDist = pseudoSigned;
                        }
                    }
                }

                if (chBest[0].found && chBest[1].found && chBest[2].found) {
                    const float maxChSq = std::max(chBest[0].minDistSq, std::max(chBest[1].minDistSq, chBest[2].minDistSq));
                    upperBoundSq = std::max(minTrueDistSq, maxChSq) + 1e-3f;
                }
            }

            const float trueSignedDist = globalSign * std::sqrt(minTrueDistSq);
            float rDist = -safeRange;
            float gDist = -safeRange;
            float bDist = -safeRange;

            if (anyClosedContour) {
                for (int c = 0; c < 3; ++c) {
                    if (chBest[c].found && chBest[c].inInteriorProj) {
                        chBest[c].pseudoDist = std::sqrt(chBest[c].minDistSq) * chBest[c].signFactor;
                    }
                }
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

    if (pathHash != 0u) {
        auto& cache = GetMtsdfProcessCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.bakedCells.size() > 4096u) {
            cache.bakedCells.clear();
        }
        cache.bakedCells.emplace(
            cacheKey,
            CachedBakedMtsdfCell{
                entry.boundsX,
                entry.boundsY,
                entry.boundsW,
                entry.boundsH,
                entry.mtsdfRgbaPixels});
    }

    entry.ComputeRowBounds();
    return entry;
}

bool RasterizeSvgPathFillAlphaMask(
    std::string_view pathData,
    const Rect& pathBounds,
    std::uint32_t innerWidth,
    std::uint32_t innerHeight,
    std::uint32_t padPx,
    std::vector<std::uint8_t>& outRgba8) {
    constexpr float kPi = 3.14159265358979323846f;
    const ParsedSvgGeometry geom = ParseSvgPathGeometry(
        pathData,
        SvgPathTessellationOptions{24, 16, kPi / 24.0f, 6, 64});
    if (geom.contours.empty()) {
        return false;
    }

    const std::uint32_t innerW = std::clamp<std::uint32_t>(innerWidth, 1u, 2048u);
    const std::uint32_t innerH = std::clamp<std::uint32_t>(innerHeight, 1u, 2048u);
    const std::uint32_t pad = std::min<std::uint32_t>(padPx, 64u);
    const std::uint32_t texW = innerW + 2u * pad;
    const std::uint32_t texH = innerH + 2u * pad;
    const std::size_t totalPixels = static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH);
    outRgba8.resize(totalPixels * 4u);
    for (std::size_t p = 0u; p < totalPixels; ++p) {
        const std::size_t o = p * 4u;
        outRgba8[o + 0u] = 255u;
        outRgba8[o + 1u] = 255u;
        outRgba8[o + 2u] = 255u;
        outRgba8[o + 3u] = 0u;
    }

    const float originX = (pathBounds.width > 1e-5f) ? pathBounds.x : geom.bounds.x;
    const float originY = (pathBounds.height > 1e-5f) ? pathBounds.y : geom.bounds.y;
    const float bw = (pathBounds.width > 1e-5f) ? pathBounds.width : std::max(1e-5f, geom.bounds.width);
    const float bh = (pathBounds.height > 1e-5f) ? pathBounds.height : std::max(1e-5f, geom.bounds.height);
    const float scaleX = static_cast<float>(innerW) / bw;
    const float scaleY = static_cast<float>(innerH) / bh;
    const float fInnerW = static_cast<float>(innerW);
    const float fInnerH = static_cast<float>(innerH);

    struct RasterEdge {
        float xTop = 0.0f;
        float yTop = 0.0f;
        float yBot = 0.0f;
        float invSlope = 0.0f;
        int dir = 1;
    };

    std::vector<RasterEdge> edges;
    std::vector<std::vector<std::uint32_t>> rowBuckets(innerH);

    auto pushEdge = [&](Point a, Point b) {
        const float dy = b.y - a.y;
        if (std::fabs(dy) <= 1e-6f) {
            return;
        }
        RasterEdge e{};
        if (dy > 0.0f) {
            e.xTop = a.x;
            e.yTop = a.y;
            e.yBot = b.y;
            e.invSlope = (b.x - a.x) / dy;
            e.dir = 1;
        } else {
            e.xTop = b.x;
            e.yTop = b.y;
            e.yBot = a.y;
            e.invSlope = (a.x - b.x) / (-dy);
            e.dir = -1;
        }
        if (e.yBot <= 0.0f || e.yTop >= fInnerH) {
            return;
        }
        const int iyStart = std::clamp(
            static_cast<int>(std::floor(e.yTop)),
            0,
            static_cast<int>(innerH) - 1);
        const auto edgeIdx = static_cast<std::uint32_t>(edges.size());
        edges.push_back(e);
        rowBuckets[static_cast<std::size_t>(iyStart)].push_back(edgeIdx);
    };

    std::vector<Point> pts;
    for (const auto& contour : geom.contours) {
        if (contour.points.size() < 2u) {
            continue;
        }
        pts.clear();
        pts.reserve(contour.points.size());
        for (const Point& rawPt : contour.points) {
            pts.push_back(Point{
                (rawPt.x - originX) * scaleX,
                (rawPt.y - originY) * scaleY});
        }
        for (std::size_t i = 0u; i + 1u < pts.size(); ++i) {
            pushEdge(pts[i], pts[i + 1u]);
        }
        const float closeDx = pts.back().x - pts.front().x;
        const float closeDy = pts.back().y - pts.front().y;
        if (std::fabs(closeDx) > 1e-4f || std::fabs(closeDy) > 1e-4f) {
            pushEdge(pts.back(), pts.front());
        }
    }

    if (edges.empty()) {
        return true;
    }

    struct Crossing {
        float x = 0.0f;
        int dir = 1;
    };

    std::vector<std::uint32_t> active;
    active.reserve(64u);
    std::vector<Crossing> crossings;
    crossings.reserve(64u);
    std::vector<float> rowCov(innerW, 0.0f);

    constexpr int kSubSamples = 8;
    constexpr float kInvSubSamples = 1.0f / static_cast<float>(kSubSamples);

    auto accumulateInterval = [&](float xa, float xb) noexcept {
        xa = std::clamp(xa, 0.0f, fInnerW);
        xb = std::clamp(xb, 0.0f, fInnerW);
        if (xb <= xa + 1e-6f) {
            return;
        }
        const int ix0 = std::clamp(static_cast<int>(std::floor(xa)), 0, static_cast<int>(innerW) - 1);
        const int ix1 = std::min(static_cast<int>(std::floor(xb)), static_cast<int>(innerW) - 1);
        if (ix0 == ix1) {
            rowCov[static_cast<std::size_t>(ix0)] +=
                std::min(xb, static_cast<float>(ix0 + 1)) - std::max(xa, static_cast<float>(ix0));
        } else {
            rowCov[static_cast<std::size_t>(ix0)] += static_cast<float>(ix0 + 1) - xa;
            for (int ix = ix0 + 1; ix < ix1; ++ix) {
                rowCov[static_cast<std::size_t>(ix)] += 1.0f;
            }
            rowCov[static_cast<std::size_t>(ix1)] +=
                std::min(xb, static_cast<float>(ix1 + 1)) - static_cast<float>(ix1);
        }
    };

    for (std::uint32_t iy = 0u; iy < innerH; ++iy) {
        const float rowY0 = static_cast<float>(iy);
        for (const std::uint32_t eIdx : rowBuckets[iy]) {
            active.push_back(eIdx);
        }
        std::size_t writePos = 0u;
        for (std::size_t k = 0u; k < active.size(); ++k) {
            const std::uint32_t eIdx = active[k];
            if (edges[eIdx].yBot > rowY0) {
                active[writePos++] = eIdx;
            }
        }
        active.resize(writePos);
        if (active.empty()) {
            continue;
        }

        std::fill(rowCov.begin(), rowCov.end(), 0.0f);
        for (int s = 0; s < kSubSamples; ++s) {
            const float ys = rowY0 + (static_cast<float>(s) + 0.5f) * kInvSubSamples;
            crossings.clear();
            for (const std::uint32_t eIdx : active) {
                const RasterEdge& e = edges[eIdx];
                if (e.yTop <= ys && ys < e.yBot) {
                    const float xCross = e.xTop + (ys - e.yTop) * e.invSlope;
                    crossings.push_back(Crossing{xCross, e.dir});
                }
            }
            if (crossings.size() < 2u) {
                continue;
            }
            std::sort(
                crossings.begin(),
                crossings.end(),
                [](const Crossing& a, const Crossing& b) noexcept { return a.x < b.x; });

            if (!geom.nonZeroFill) {
                for (std::size_t i = 0u; i + 1u < crossings.size(); i += 2u) {
                    accumulateInterval(crossings[i].x, crossings[i + 1u].x);
                }
            } else {
                int winding = 0;
                float spanStart = 0.0f;
                for (const Crossing& c : crossings) {
                    const int prevW = winding;
                    winding += c.dir;
                    if (prevW == 0 && winding != 0) {
                        spanStart = c.x;
                    } else if (prevW != 0 && winding == 0) {
                        accumulateInterval(spanStart, c.x);
                    }
                }
            }
        }

        const std::size_t dstRow = static_cast<std::size_t>(iy + pad) * static_cast<std::size_t>(texW);
        for (std::uint32_t ix = 0u; ix < innerW; ++ix) {
            const float cov = std::clamp(rowCov[ix] * kInvSubSamples, 0.0f, 1.0f);
            if (cov > 0.0f) {
                const auto a8 = static_cast<std::uint8_t>(std::lround(cov * 255.0f));
                const std::size_t o = (dstRow + static_cast<std::size_t>(ix + pad)) * 4u;
                outRgba8[o + 3u] = a8;
            }
        }
    }

    return true;
}

} // namespace aero
