#include "aero/visual/VisualRenderAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

#include "aero/asset/GradientBrush.hpp"
#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kDegToRad = kPi / 180.0f;

[[nodiscard]] Color UnpackRgba32(std::uint32_t rgba, float opacityMultiplier = 1.0f) noexcept {
    const float r = static_cast<float>(rgba & 0xFFu) / 255.0f;
    const float g = static_cast<float>((rgba >> 8u) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((rgba >> 16u) & 0xFFu) / 255.0f;
    const float a = (static_cast<float>((rgba >> 24u) & 0xFFu) / 255.0f) * opacityMultiplier;
    return Color{r, g, b, std::clamp(a, 0.0f, 1.0f)};
}

[[nodiscard]] bool HasAlpha(std::uint32_t rgba) noexcept {
    return (rgba >> 24u) > 0u;
}

[[nodiscard]] bool HasPositiveThickness(const Thickness& t) noexcept {
    return (t.left > 0.0f) || (t.top > 0.0f) || (t.right > 0.0f) || (t.bottom > 0.0f);
}

[[nodiscard]] Rect IntersectRects(const Rect& a, const Rect& b) noexcept {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

[[nodiscard]] Rect ComputeStretchedBox(
    const Rect& targetBox,
    float contentW,
    float contentH,
    Stretch stretch) noexcept {
    if (targetBox.width <= 0.0f || targetBox.height <= 0.0f ||
        contentW <= 1e-5f || contentH <= 1e-5f) {
        return targetBox;
    }
    float sx = 1.0f;
    float sy = 1.0f;
    switch (stretch) {
        case Stretch::None:
            sx = 1.0f;
            sy = 1.0f;
            break;
        case Stretch::Fill:
            sx = targetBox.width / contentW;
            sy = targetBox.height / contentH;
            break;
        case Stretch::Uniform: {
            const float u = std::min(targetBox.width / contentW, targetBox.height / contentH);
            sx = u;
            sy = u;
            break;
        }
        case Stretch::UniformToFill: {
            const float u = std::max(targetBox.width / contentW, targetBox.height / contentH);
            sx = u;
            sy = u;
            break;
        }
    }
    const float w = contentW * sx;
    const float h = contentH * sy;
    return Rect{
        targetBox.x + (targetBox.width - w) * 0.5f,
        targetBox.y + (targetBox.height - h) * 0.5f,
        w,
        h};
}

[[nodiscard]] Rect TransformRectViewportBounds(
    const Rect& localRect,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D) noexcept {
    if (has3D) {
        return world3D.TransformRectBounds(localRect);
    }
    if (!world2D.IsIdentity()) {
        return world2D.TransformRectBounds(localRect);
    }
    return localRect;
}

[[nodiscard]] std::vector<float> ParseCsvFloats(std::string_view raw) {
    std::vector<float> out;
    const char* p = raw.data();
    const char* end = raw.data() + raw.size();
    while (p < end) {
        while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',' || *p == ';')) {
            ++p;
        }
        if (p >= end) {
            break;
        }
        char* next = nullptr;
        const float val = std::strtof(p, &next);
        if (next == p) {
            ++p;
            continue;
        }
        out.push_back(val);
        p = next;
    }
    return out;
}

// Lightweight SVG subpath polyline parser supporting M/L/H/V/Q/C/Z for trimmed stroke & open path rendering.
struct ParsedSvgSubpaths {
    std::vector<std::vector<Point>> subpaths;
    bool hasCloseCommand = false;
};

[[nodiscard]] ParsedSvgSubpaths ParseSvgPolylines(std::string_view svg) {
    ParsedSvgSubpaths res;
    std::vector<Point> curSubpath;
    Point curPt{0.0f, 0.0f};
    Point startPt{0.0f, 0.0f};
    Point lastCtrl{0.0f, 0.0f};
    char lastCmd = 0;

    const char* p = svg.data();
    const char* end = svg.data() + svg.size();

    auto skipSep = [&]() {
        while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) {
            ++p;
        }
    };
    auto tryReadFloat = [&](float& outVal) -> bool {
        skipSep();
        if (p >= end) return false;
        char* next = nullptr;
        const float v = std::strtof(p, &next);
        if (next == p) return false;
        outVal = v;
        p = next;
        return true;
    };
    auto flushSubpath = [&]() {
        if (curSubpath.size() >= 2u) {
            res.subpaths.push_back(std::move(curSubpath));
        }
        curSubpath.clear();
    };

    char cmd = 0;
    while (p < end) {
        skipSep();
        if (p >= end) break;
        if (std::isalpha(static_cast<unsigned char>(*p))) {
            cmd = *p++;
        } else if (cmd == 0) {
            ++p;
            continue;
        } else if (cmd == 'M') {
            cmd = 'L';
        } else if (cmd == 'm') {
            cmd = 'l';
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

        if (upper == 'Z') {
            res.hasCloseCommand = true;
            if (!curSubpath.empty() &&
                (std::fabs(curPt.x - startPt.x) > 1e-4f || std::fabs(curPt.y - startPt.y) > 1e-4f)) {
                curSubpath.push_back(startPt);
            }
            curPt = startPt;
            flushSubpath();
            lastCmd = upper;
            cmd = 0;
            continue;
        }

        if (upper == 'M') {
            float x = 0.0f, y = 0.0f;
            if (!tryReadFloat(x) || !tryReadFloat(y)) break;
            if (rel) { x += curPt.x; y += curPt.y; }
            flushSubpath();
            curPt = Point{x, y};
            startPt = curPt;
            curSubpath.push_back(curPt);
            lastCmd = 'M';
            continue;
        }

        if (upper == 'L') {
            float x = 0.0f, y = 0.0f;
            if (!tryReadFloat(x) || !tryReadFloat(y)) break;
            if (rel) { x += curPt.x; y += curPt.y; }
            if (curSubpath.empty()) curSubpath.push_back(curPt);
            curPt = Point{x, y};
            curSubpath.push_back(curPt);
            lastCmd = 'L';
            continue;
        }

        if (upper == 'H') {
            float x = 0.0f;
            if (!tryReadFloat(x)) break;
            if (rel) x += curPt.x;
            if (curSubpath.empty()) curSubpath.push_back(curPt);
            curPt.x = x;
            curSubpath.push_back(curPt);
            lastCmd = 'H';
            continue;
        }

        if (upper == 'V') {
            float y = 0.0f;
            if (!tryReadFloat(y)) break;
            if (rel) y += curPt.y;
            if (curSubpath.empty()) curSubpath.push_back(curPt);
            curPt.y = y;
            curSubpath.push_back(curPt);
            lastCmd = 'V';
            continue;
        }

        if (upper == 'Q') {
            float cx = 0.0f, cy = 0.0f, x = 0.0f, y = 0.0f;
            if (!tryReadFloat(cx) || !tryReadFloat(cy) || !tryReadFloat(x) || !tryReadFloat(y)) break;
            if (rel) { cx += curPt.x; cy += curPt.y; x += curPt.x; y += curPt.y; }
            if (curSubpath.empty()) curSubpath.push_back(curPt);
            const Point p0 = curPt;
            constexpr int kSteps = 12;
            for (int s = 1; s <= kSteps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(kSteps);
                const float u = 1.0f - t;
                curSubpath.push_back(Point{
                    u * u * p0.x + 2.0f * u * t * cx + t * t * x,
                    u * u * p0.y + 2.0f * u * t * cy + t * t * y});
            }
            lastCtrl = Point{cx, cy};
            curPt = Point{x, y};
            lastCmd = 'Q';
            continue;
        }

        if (upper == 'C') {
            float c1x = 0.0f, c1y = 0.0f, c2x = 0.0f, c2y = 0.0f, x = 0.0f, y = 0.0f;
            if (!tryReadFloat(c1x) || !tryReadFloat(c1y) ||
                !tryReadFloat(c2x) || !tryReadFloat(c2y) ||
                !tryReadFloat(x) || !tryReadFloat(y)) break;
            if (rel) {
                c1x += curPt.x; c1y += curPt.y;
                c2x += curPt.x; c2y += curPt.y;
                x += curPt.x; y += curPt.y;
            }
            if (curSubpath.empty()) curSubpath.push_back(curPt);
            const Point p0 = curPt;
            constexpr int kSteps = 16;
            for (int s = 1; s <= kSteps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(kSteps);
                const float u = 1.0f - t;
                const float u2 = u * u;
                const float t2 = t * t;
                curSubpath.push_back(Point{
                    u2 * u * p0.x + 3.0f * u2 * t * c1x + 3.0f * u * t2 * c2x + t2 * t * x,
                    u2 * u * p0.y + 3.0f * u2 * t * c1y + 3.0f * u * t2 * c2y + t2 * t * y});
            }
            lastCtrl = Point{c2x, c2y};
            curPt = Point{x, y};
            lastCmd = 'C';
            continue;
        }

        // Unsupported SVG command: consume one number if present to advance safely.
        float dummy = 0.0f;
        if (!tryReadFloat(dummy)) {
            cmd = 0;
        }
        (void)lastCmd;
        (void)lastCtrl;
    }
    flushSubpath();
    return res;
}

[[nodiscard]] std::vector<Point> TrimPolylineRange(
    const std::vector<Point>& pts,
    float trimStart01,
    float trimEnd01) {
    std::vector<Point> out;
    if (pts.size() < 2u) {
        return out;
    }
    const float s0 = std::clamp(trimStart01, 0.0f, 1.0f);
    const float s1 = std::clamp(trimEnd01, 0.0f, 1.0f);
    if (s1 <= s0) {
        return out;
    }
    if (s0 <= 1e-5f && s1 >= 1.0f - 1e-5f) {
        return pts;
    }

    std::vector<float> cumLen(pts.size(), 0.0f);
    float totalLen = 0.0f;
    for (std::size_t i = 1u; i < pts.size(); ++i) {
        totalLen += std::hypot(pts[i].x - pts[i - 1u].x, pts[i].y - pts[i - 1u].y);
        cumLen[i] = totalLen;
    }
    if (totalLen <= 1e-5f) {
        return out;
    }
    const float targetStart = s0 * totalLen;
    const float targetEnd = s1 * totalLen;

    bool started = false;
    for (std::size_t i = 1u; i < pts.size(); ++i) {
        const float d0 = cumLen[i - 1u];
        const float d1 = cumLen[i];
        const float segLen = d1 - d0;
        if (segLen <= 1e-6f || d1 < targetStart || d0 > targetEnd) {
            continue;
        }
        const float t0 = std::clamp((targetStart - d0) / segLen, 0.0f, 1.0f);
        const float t1 = std::clamp((targetEnd - d0) / segLen, 0.0f, 1.0f);
        if (t1 <= t0) {
            continue;
        }
        const Point a{
            pts[i - 1u].x + (pts[i].x - pts[i - 1u].x) * t0,
            pts[i - 1u].y + (pts[i].y - pts[i - 1u].y) * t0};
        const Point b{
            pts[i - 1u].x + (pts[i].x - pts[i - 1u].x) * t1,
            pts[i - 1u].y + (pts[i].y - pts[i - 1u].y) * t1};
        if (!started) {
            out.push_back(a);
            started = true;
        }
        out.push_back(b);
    }
    return out;
}

class ScopedBlendMode final {
public:
    ScopedBlendMode(BlendingMode& slot, BlendingMode next) noexcept
        : slot_(slot), prev_(slot) {
        slot_ = next;
    }

    ~ScopedBlendMode() noexcept {
        slot_ = prev_;
    }

    ScopedBlendMode(const ScopedBlendMode&) = delete;
    ScopedBlendMode& operator=(const ScopedBlendMode&) = delete;

private:
    BlendingMode& slot_;
    BlendingMode prev_;
};

} // namespace

void VisualRenderAdapter::ApplyNodeTransform(
        PagedRenderTree& tree,
        std::uint32_t beginNid,
        std::uint32_t endNid,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) noexcept {
        if (!has3D && world2D.IsIdentity() && blend == BlendingMode::Normal) {
            return;
        }
        for (std::uint32_t nid = beginNid; nid < endNid; ++nid) {
            if (nid >= tree.nodes_.size()) {
                break;
            }
            RenderNodeDescriptor& desc = tree.nodes_[nid];
            const bool isRotatedOr3D =
                has3D ||
                std::fabs(world2D.m01) > 1e-4f ||
                std::fabs(world2D.m10) > 1e-4f;
            if (isRotatedOr3D &&
                desc.primType == AeroPrimType::SolidQuad &&
                desc.sdfPadPx <= 0.0f &&
                desc.width > 0.0f &&
                desc.height > 0.0f) {
                constexpr float aaPad = 1.0f;
                desc.x -= aaPad;
                desc.y -= aaPad;
                desc.width += 2.0f * aaPad;
                desc.height += 2.0f * aaPad;
                desc.primType = AeroPrimType::SdfRoundedRect;
                desc.softFeatherPx = 0.0f;
                desc.sdfPadPx = aaPad;
                desc.encodedSdfParams = 0u;
                desc.encodedPrimExtra = static_cast<std::uint16_t>(2u << 5u);
            }
            desc.m00 = world2D.m00;
            desc.m01 = world2D.m01;
            desc.m10 = world2D.m10;
            desc.m11 = world2D.m11;
            desc.baseTx = world2D.tx;
            desc.baseTy = world2D.ty;
            desc.tx = world2D.tx + desc.x * (world2D.m00 - 1.0f) + desc.y * world2D.m01;
            desc.ty = world2D.ty + desc.x * world2D.m10 + desc.y * (world2D.m11 - 1.0f);
            desc.hasPerspective3D = has3D;
            desc.worldMatrix3D = world3D;
            desc.blendMode = blend;
            tree.WriteQuadVertices(desc);
        }
    }

std::size_t VisualRenderAdapter::EmitLineSegment(
        PagedRenderTree& tree,
        Point a,
        Point b,
        float thickness,
        const Color& color,
        const void* owner,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        if (color.a <= 0.0f || thickness <= 0.0f) {
            return 0u;
        }
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::hypot(dx, dy);
        if (len <= 1e-4f) {
            return 0u;
        }
        const float angleDeg = std::atan2(dy, dx) * kRadToDeg;
        const Matrix2D segRot = Matrix2D::Rotation(angleDeg).AroundPivot(a.x, a.y);
        const Matrix2D comb2D = Matrix2D::Multiply(world2D, segRot);
        const Matrix4x4 comb3D = has3D
            ? Matrix4x4::Multiply(world3D, Matrix4x4::FromMatrix2D(segRot))
            : Matrix4x4{};

        const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
        (void)tree.AllocateRoundedRectNode(
            a.x,
            a.y - thickness * 0.5f,
            len,
            thickness,
            color,
            CornerRadius{thickness * 0.5f},
            owner);
        const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
        ApplyNodeTransform(tree, beginNid, endNid, comb2D, comb3D, has3D, blend);
        return endNid - beginNid;
    }

void VisualRenderAdapter::AttachNodeGradientBrush(
    PagedRenderTree& tree,
    std::uint32_t nid,
    std::int32_t gradId) noexcept {
    if (gradId <= 0 || nid >= tree.nodes_.size()) {
        return;
    }
    RenderNodeDescriptor& desc = tree.nodes_[nid];
    desc.gradientBrushId = static_cast<std::uint32_t>(gradId);
    desc.encodedPrimExtra = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(gradId) & 0x0FFFu) << 4u);
    tree.WriteQuadVertices(desc);
}

std::size_t VisualRenderAdapter::EmitSvgPathPrimitive(
        PagedRenderTree& tree,
        const Rect& destBox,
        std::string_view pathData,
        std::uint32_t fillRgba,
        std::int32_t fillGradId,
        std::uint32_t strokeRgba,
        std::int32_t strokeGradId,
        float strokeWidth,
        Stretch stretch,
        float trimStart,
        float trimEnd,
        float effOpacity,
        const void* owner,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend,
        float fillOpacity,
        float strokeOpacity) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        if (pathData.empty() || destBox.width <= 0.0f || destBox.height <= 0.0f) {
            return 0u;
        }
        std::size_t count = 0u;
        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
        const float effFillOp = std::clamp(effOpacity * fillOpacity, 0.0f, 1.0f);
        const float effStrokeOp = std::clamp(effOpacity * strokeOpacity, 0.0f, 1.0f);
        const bool hasFill = (HasAlpha(fillRgba) || fillGradId > 0) && effFillOp > 0.001f;
        const bool hasStroke = (HasAlpha(strokeRgba) || strokeGradId > 0) && strokeWidth > 0.0f && effStrokeOp > 0.001f;
        if (!hasFill && !hasStroke) {
            return 0u;
        }
        const bool trimmed = (trimStart > 0.001f) || (trimEnd < 0.999f);

        const ParsedSvgSubpaths parsed = (hasStroke && trimmed) ? ParseSvgPolylines(pathData) : ParsedSvgSubpaths{};
        const Rect rawBounds = MeasureSvgPathBounds(pathData);
        Rect fitted = destBox;
        if (rawBounds.width > 0.0f && rawBounds.height > 0.0f) {
            if (stretch == Stretch::None) {
                fitted = Rect{
                    destBox.x + rawBounds.x,
                    destBox.y + rawBounds.y,
                    rawBounds.width,
                    rawBounds.height};
            } else {
                fitted = ComputeStretchedBox(destBox, rawBounds.width, rawBounds.height, stretch);
            }
        } else if (stretch == Stretch::None) {
            fitted = Rect{
                destBox.x,
                destBox.y,
                (rawBounds.width > 0.0f) ? rawBounds.width : destBox.width,
                (rawBounds.height > 0.0f) ? rawBounds.height : destBox.height};
        }

        const float dpi = std::max(0.25f, tree.RootDpiScale());
        const float sx = std::max(0.25f, std::hypot(world2D.m00, world2D.m01) * dpi);
        const float sy = std::max(0.25f, std::hypot(world2D.m10, world2D.m11) * dpi);
        const std::uint32_t innerW = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::lround(fitted.width * sx)), 1u, 2048u);
        const std::uint32_t innerH = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::lround(fitted.height * sy)), 1u, 2048u);
        const std::uint64_t hash = ComputeSvgPathHash(pathData);

        if (hasFill) {
            const Color fillColor = (fillGradId > 0)
                ? Color{1.0f, 1.0f, 1.0f, effFillOp}
                : UnpackRgba32(fillRgba, effFillOp);

            RasterizedPathMaskEntry maskEntry{};
            if (store->GetOrRasterizePathMask(hash, pathData, rawBounds, innerW, innerH, 1u, maskEntry) &&
                maskEntry.textureId > 0u) {
                const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                const std::uint32_t nid = tree.AllocateTexturedNode(
                    fitted.x,
                    fitted.y,
                    fitted.width,
                    fitted.height,
                    maskEntry.u0,
                    maskEntry.v0,
                    maskEntry.u1,
                    maskEntry.v1,
                    fillColor,
                    1.0f,
                    owner,
                    maskEntry.textureId);
                AttachNodeGradientBrush(tree, nid, fillGradId);
                const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                count += (endNid - beginNid);
            } else {
                const AxbMtsdfPathEntry* entry = store->FindMtsdfPath(hash);
                if (!entry) {
                    float pxRange = 4.0f;
                    const std::uint16_t cellSize = SelectAdaptiveMtsdfCellSize(
                        pathData, std::max(fitted.width, fitted.height), &pxRange);
                    const AxbMtsdfPathEntry baked = BakeSvgPathToMtsdfEntry(pathData, cellSize, pxRange);
                    if (!baked.mtsdfRgbaPixels.empty() && baked.cellWidth > 0u && baked.cellHeight > 0u) {
                        store->RegisterMtsdfPath(baked);
                        entry = store->FindMtsdfPath(baked.pathHash);
                    }
                }
                if (entry && !entry->mtsdfRgbaPixels.empty() && entry->cellWidth > 0u && entry->cellHeight > 0u) {
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    const std::uint32_t nid = tree.AllocateMtsdfPathNode(
                        fitted.x,
                        fitted.y,
                        fitted.width,
                        fitted.height,
                        *entry,
                        fillColor,
                        0.0f,
                        1.0f,
                        owner);
                    AttachNodeGradientBrush(tree, nid, fillGradId);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
            }
        }

        if (hasStroke) {
            const Color strokeColor = (strokeGradId > 0)
                ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                : UnpackRgba32(strokeRgba, effStrokeOp);
            if (!trimmed) {
                const float strokeWidthPx = strokeWidth * 0.5f * (sx + sy);
                const std::uint32_t padPx = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::ceil(0.5f * strokeWidthPx + 2.0f)), 2u, 64u);
                RasterizedPathMaskEntry strokeMask{};
                if (store->GetOrRasterizePathStrokeMask(
                        hash, pathData, rawBounds, innerW, innerH, strokeWidthPx, padPx, strokeMask) &&
                    strokeMask.textureId > 0u) {
                    const float worldPadX = static_cast<float>(padPx) / sx;
                    const float worldPadY = static_cast<float>(padPx) / sy;
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    const std::uint32_t nid = tree.AllocateTexturedNode(
                        fitted.x - worldPadX,
                        fitted.y - worldPadY,
                        fitted.width + 2.0f * worldPadX,
                        fitted.height + 2.0f * worldPadY,
                        strokeMask.u0,
                        strokeMask.v0,
                        strokeMask.u1,
                        strokeMask.v1,
                        strokeColor,
                        1.0f,
                        owner,
                        strokeMask.textureId);
                    AttachNodeGradientBrush(tree, nid, strokeGradId);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                    return count;
                }
            }

            const ParsedSvgSubpaths& polyData = (trimmed) ? parsed : ParseSvgPolylines(pathData);
            if (!polyData.subpaths.empty() && rawBounds.width > 1e-5f && rawBounds.height > 1e-5f) {
                const float scaleX = fitted.width / rawBounds.width;
                const float scaleY = fitted.height / rawBounds.height;
                for (const auto& sub : polyData.subpaths) {
                    std::vector<Point> mapped;
                    mapped.reserve(sub.size());
                    for (const Point& pt : sub) {
                        mapped.push_back(Point{
                            fitted.x + (pt.x - rawBounds.x) * scaleX,
                            fitted.y + (pt.y - rawBounds.y) * scaleY});
                    }
                    const std::vector<Point> finalPts = trimmed
                        ? TrimPolylineRange(mapped, trimStart, trimEnd)
                        : mapped;
                    for (std::size_t i = 1u; i < finalPts.size(); ++i) {
                        count += EmitLineSegment(
                            tree, finalPts[i - 1u], finalPts[i], strokeWidth, strokeColor,
                            owner, world2D, world3D, has3D, blend);
                    }
                }
            }
        }
        return count;
    }

std::size_t VisualRenderAdapter::EmitBoxVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        std::size_t count = 0u;
        const bool hasBorderThickness = HasPositiveThickness(node.box.borderThickness);
        Color borderColor = UnpackRgba32(node.box.borderRgba, effOpacity);
        if (hasBorderThickness && node.box.borderGradId > 0) {
            UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
            const GradientBrushDesc* gb = store->GradientBrushes().Find(
                static_cast<std::uint32_t>(node.box.borderGradId));
            if (!gb) {
                gb = GradientBrushRegistry::Instance().Find(
                    static_cast<std::uint32_t>(node.box.borderGradId));
            }
            if (gb && !gb->stops.empty()) {
                Color sampled = SampleGradientRampAtT(*gb, 0.5f);
                if (sampled.a <= 0.001f) {
                    for (const GradientStop& st : gb->stops) {
                        if (st.color.a > sampled.a) {
                            sampled = st.color;
                        }
                    }
                }
                sampled.a *= gb->opacity * effOpacity;
                if (sampled.a > 0.0f) {
                    borderColor = sampled;
                }
            }
        }
        const bool hasVisibleBorder = hasBorderThickness && (borderColor.a > 0.0f);

        Rect bgRect = rect;
        CornerRadius bgRadii = node.box.cornerRadius;
        if (hasBorderThickness) {
            const float insetScale = hasVisibleBorder ? 0.5f : 1.0f;
            const float inL = std::max(0.0f, node.box.borderThickness.left) * insetScale;
            const float inT = std::max(0.0f, node.box.borderThickness.top) * insetScale;
            const float inR = std::max(0.0f, node.box.borderThickness.right) * insetScale;
            const float inB = std::max(0.0f, node.box.borderThickness.bottom) * insetScale;
            bgRect.x += inL;
            bgRect.y += inT;
            bgRect.width = std::max(0.0f, rect.width - (inL + inR));
            bgRect.height = std::max(0.0f, rect.height - (inT + inB));
            bgRadii.topLeft = std::max(0.0f, bgRadii.topLeft - std::max(inL, inT));
            bgRadii.topRight = std::max(0.0f, bgRadii.topRight - std::max(inR, inT));
            bgRadii.bottomRight = std::max(0.0f, bgRadii.bottomRight - std::max(inR, inB));
            bgRadii.bottomLeft = std::max(0.0f, bgRadii.bottomLeft - std::max(inL, inB));
        }

        const bool hasBgGrad = node.box.bgGradId > 0;
        const float bgEffOpacity = std::clamp(effOpacity * node.box.bgOpacity, 0.0f, 1.0f);
        if (bgEffOpacity > 0.0f && (hasBgGrad || HasAlpha(node.box.bgRgba)) &&
            bgRect.width > 0.0f && bgRect.height > 0.0f) {
            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
            Matrix2D bgWorld2D = world2D;
            if (hasBgGrad) {
                if (node.layout.effectiveRtl) {
                    const float cx2 = 2.0f * bgRect.x + bgRect.width;
                    bgWorld2D.tx += world2D.m00 * cx2;
                    bgWorld2D.ty += world2D.m10 * cx2;
                    bgWorld2D.m00 = -world2D.m00;
                    bgWorld2D.m10 = -world2D.m10;
                    std::swap(bgRadii.topLeft, bgRadii.topRight);
                    std::swap(bgRadii.bottomLeft, bgRadii.bottomRight);
                }
                const Color tint{1.0f, 1.0f, 1.0f, bgEffOpacity};
                (void)tree.AllocateGradientRoundedRectNode(
                    bgRect.x,
                    bgRect.y,
                    bgRect.width,
                    bgRect.height,
                    static_cast<std::uint32_t>(node.box.bgGradId),
                    bgRadii,
                    false,
                    tint,
                    1.0f,
                    &node);
            } else {
                (void)tree.AllocateRoundedRectNode(
                    bgRect.x,
                    bgRect.y,
                    bgRect.width,
                    bgRect.height,
                    UnpackRgba32(node.box.bgRgba, bgEffOpacity),
                    bgRadii,
                    &node);
            }
            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
            ApplyNodeTransform(tree, beginNid, endNid, hasBgGrad ? bgWorld2D : world2D, world3D, has3D, blend);
            count += (endNid - beginNid);
        }

        const VisualImageData& boxImg = node.Image();
        if (node.kind == VisualKind::Box && !boxImg.uri.empty() && HasAlpha(boxImg.tintRgba) &&
            bgRect.width > 0.0f && bgRect.height > 0.0f) {
            count += EmitImageVisual(tree, node, bgRect, effOpacity, world2D, world3D, has3D, blend);
        }

        if (hasVisibleBorder) {
            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
            (void)tree.AllocateBorderRingNode(
                rect.x,
                rect.y,
                rect.width,
                rect.height,
                borderColor,
                node.box.cornerRadius,
                node.box.borderThickness,
                &node);
            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
            ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
            count += (endNid - beginNid);
        }
        return count;
    }

std::size_t VisualRenderAdapter::EmitTextVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend,
        bool forceSolidShadowColor) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        const VisualTextData& txt = node.Text();
        const float padL = std::max(0.0f, node.layout.padding.left);
        const float padT = std::max(0.0f, node.layout.padding.top);
        const float padR = std::max(0.0f, node.layout.padding.right);
        const float padB = std::max(0.0f, node.layout.padding.bottom);
        const Rect textRect{
            rect.x + padL,
            rect.y + padT,
            std::max(0.0f, rect.width - padL - padR),
            std::max(0.0f, rect.height - padT - padB)};

        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
        const float maxW = (txt.wrap == TextWrapping::NoWrap)
            ? 1000000.0f
            : std::max(1.0f, textRect.width);
        TextLayoutResult measured{};
        if (!txt.richSpans.empty()) {
            std::vector<RichTextRunInput> runs;
            runs.reserve(txt.richSpans.size());
            for (const VisualTextSpan& sp : txt.richSpans) {
                RichTextRunInput r{};
                r.text = sp.text;
                r.fontFamily = sp.fontFamily;
                r.fontSize = sp.fontSize;
                r.fontWeight = sp.fontWeight;
                r.fontItalic = sp.fontItalic;
                r.fgRgba = sp.fgRgba;
                r.hasFg = sp.hasFg;
                r.decorations = sp.decorations;
                runs.push_back(r);
            }
            measured = store->Fonts().MeasureRichLayout(
                runs,
                txt.fontFamily,
                txt.fontSize,
                txt.fontWeight,
                txt.fontItalic,
                maxW,
                txt.wrap,
                txt.charSpacing,
                txt.lineHeight,
                node.layout.effectiveRtl);
        } else {
            measured = store->Fonts().MeasureLayout(
                txt.text,
                txt.fontFamily,
                txt.fontSize,
                maxW,
                txt.wrap,
                txt.charSpacing,
                txt.fontWeight,
                txt.fontItalic,
                txt.lineHeight,
                node.layout.effectiveRtl);
        }

        std::size_t count = 0u;
        auto lineAlignDx = [&](const TextLineMetrics& line) noexcept -> float {
            TextAlignment effA = txt.align;
            if (node.layout.effectiveRtl) {
                if (effA == TextAlignment::Left) effA = TextAlignment::Right;
                else if (effA == TextAlignment::Right) effA = TextAlignment::Left;
            }
            if (effA == TextAlignment::Center) {
                return std::max(0.0f, (textRect.width - line.width) * 0.5f);
            }
            if (effA == TextAlignment::Right || (effA == TextAlignment::Justify && node.layout.effectiveRtl && line.endedByNewline)) {
                return std::max(0.0f, textRect.width - line.width);
            }
            return 0.0f;
        };

        // Optional text selection highlight behind glyphs
        if (txt.selLenCp > 0 && HasAlpha(txt.selRgba) && !measured.lines.empty()) {
            const std::uint32_t selBegin = static_cast<std::uint32_t>(std::max<std::int32_t>(0, txt.selStartCp));
            const std::uint32_t selEnd = selBegin + static_cast<std::uint32_t>(txt.selLenCp);
            const Color selCol = UnpackRgba32(txt.selRgba, effOpacity);

            for (const TextLineMetrics& line : measured.lines) {
                const float alignDx = lineAlignDx(line);
                const std::size_t gEnd = std::min(line.firstGlyphIndex + line.glyphCount, measured.glyphs.size());
                float xMin = std::numeric_limits<float>::infinity();
                float xMax = -std::numeric_limits<float>::infinity();
                for (std::size_t gi = line.firstGlyphIndex; gi < gEnd; ++gi) {
                    const PositionedGlyph& g = measured.glyphs[gi];
                    if (g.sourceCodePointIndex >= selBegin && g.sourceCodePointIndex < selEnd) {
                        xMin = std::min(xMin, g.penX);
                        xMax = std::max(xMax, g.penX + g.advanceX);
                    }
                }
                if (std::isfinite(xMin) && xMax > xMin) {
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    (void)tree.AllocateRoundedRectNode(
                        textRect.x + alignDx + xMin,
                        textRect.y + line.topY,
                        xMax - xMin,
                        std::max(1.0f, line.lineHeight),
                        selCol,
                        CornerRadius{},
                        &node);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
            }
        }

        // Main SDF text run
        if (!txt.text.empty() || !txt.richSpans.empty()) {
            PagedRenderTraversalState state{};
            state.clipRect = tree.activeClipRect_;
            state.clipActive = tree.activeClipEnabled_;
            state.inheritedOpacity = effOpacity;
            state.m00 = world2D.m00;
            state.m01 = world2D.m01;
            state.m10 = world2D.m10;
            state.m11 = world2D.m11;
            state.tx = world2D.tx;
            state.ty = world2D.ty;
            state.hasPerspective3D = has3D;
            state.blendMode = blend;
            state.world3D = world3D;

            const GradientBrushDesc* fgGrad = nullptr;
            if (!forceSolidShadowColor && txt.fgGradId > 0) {
                fgGrad = store->GradientBrushes().Find(static_cast<std::uint32_t>(txt.fgGradId));
                if (!fgGrad) {
                    fgGrad = GradientBrushRegistry::Instance().Find(static_cast<std::uint32_t>(txt.fgGradId));
                }
            }

            const float textOpacity = effOpacity * std::clamp(txt.fgOpacity, 0.0f, 1.0f);
            const Color fg = UnpackRgba32(txt.fgRgba, textOpacity);
            count += tree.EmitPreparedTextLayoutNodes(
                textRect.x,
                textRect.y,
                txt.fontSize,
                measured,
                fg,
                textOpacity,
                state,
                &node,
                txt.align,
                textRect.width,
                nullptr,
                fgGrad,
                textRect.height,
                forceSolidShadowColor);

            bool hasDecorations = (txt.decorations != 0);
            if (!hasDecorations) {
                for (const PositionedGlyph& pg : measured.glyphs) {
                    if (pg.underline || pg.strikethrough) {
                        hasDecorations = true;
                        break;
                    }
                }
            }
            if (hasDecorations && !measured.lines.empty()) {
                TextLayoutResult decoLayout = measured;
                ApplyTextAlignmentToLayout(decoLayout, txt.align, textRect.width);
                const float strokeH = std::max(1.0f, txt.fontSize * 0.07f);
                const bool nodeU = (txt.decorations & 1) != 0;
                const bool nodeS = (txt.decorations & 2) != 0;
                for (const TextLineMetrics& lm : decoLayout.lines) {
                    const std::size_t gEnd = std::min(lm.firstGlyphIndex + lm.glyphCount, decoLayout.glyphs.size());
                    auto emitDecoRun = [&](bool isStrike) {
                        std::size_t k = lm.firstGlyphIndex;
                        while (k < gEnd) {
                            const PositionedGlyph& g0 = decoLayout.glyphs[k];
                            const bool active = isStrike ? (nodeS || g0.strikethrough) : (nodeU || g0.underline);
                            if (!active) {
                                ++k;
                                continue;
                            }
                            const std::uint32_t runRgba = (!forceSolidShadowColor && g0.hasCustomRgba) ? g0.customRgba : txt.fgRgba;
                            const bool runHasCustom = !forceSolidShadowColor && g0.hasCustomRgba;
                            float xStart = g0.penX;
                            float xEnd = g0.penX + g0.advanceX;
                            std::size_t m = k + 1u;
                            while (m < gEnd) {
                                const PositionedGlyph& gm = decoLayout.glyphs[m];
                                const bool mAct = isStrike ? (nodeS || gm.strikethrough) : (nodeU || gm.underline);
                                const bool mCustom = !forceSolidShadowColor && gm.hasCustomRgba;
                                const std::uint32_t mRgba = mCustom ? gm.customRgba : txt.fgRgba;
                                if (!mAct || mCustom != runHasCustom || mRgba != runRgba) {
                                    break;
                                }
                                xEnd = gm.penX + gm.advanceX;
                                ++m;
                            }
                            if (xEnd > xStart + 0.5f) {
                                const float barY = textRect.y + (isStrike
                                    ? (lm.baselineY - std::max(2.0f, lm.ascent * 0.32f))
                                    : (lm.baselineY + std::max(1.0f, lm.descent * 0.35f)));
                                const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                                (void)tree.AllocateRoundedRectNode(
                                    textRect.x + xStart,
                                    barY,
                                    xEnd - xStart,
                                    strokeH,
                                    UnpackRgba32(runRgba, textOpacity),
                                    CornerRadius{},
                                    &node);
                                const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                                ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                                count += (endNid - beginNid);
                            }
                            k = m;
                        }
                    };
                    emitDecoRun(false);
                    emitDecoRun(true);
                }
            }
        }

        // Optional caret cursor overlay
        if (txt.showCaret && txt.caretCp >= 0 && HasAlpha(txt.caretRgba)) {
            const auto targetCp = static_cast<std::uint32_t>(txt.caretCp);
            float caretX = textRect.x;
            float caretY = textRect.y;
            float caretH = std::max(1.0f, (measured.lineHeight > 0.0f) ? measured.lineHeight : txt.fontSize * 1.25f);
            if (!measured.lines.empty()) {
                caretX = textRect.x + lineAlignDx(measured.lines.front());
                caretY = textRect.y + measured.lines.front().topY;
                caretH = std::max(1.0f, measured.lines.front().lineHeight);
                bool placed = false;
                for (const TextLineMetrics& line : measured.lines) {
                    const float alignDx = lineAlignDx(line);
                    const std::size_t gEnd = std::min(line.firstGlyphIndex + line.glyphCount, measured.glyphs.size());
                    for (std::size_t gi = line.firstGlyphIndex; gi < gEnd; ++gi) {
                        const PositionedGlyph& g = measured.glyphs[gi];
                        if (g.sourceCodePointIndex >= targetCp) {
                            caretX = textRect.x + alignDx + g.penX;
                            caretY = textRect.y + line.topY;
                            caretH = std::max(1.0f, line.lineHeight);
                            placed = true;
                            break;
                        }
                    }
                    if (placed) break;
                }
                if (!placed && !measured.glyphs.empty()) {
                    const PositionedGlyph& lastG = measured.glyphs.back();
                    const std::size_t li = std::min<std::size_t>(lastG.lineIndex, measured.lines.size() - 1u);
                    caretX = textRect.x + lineAlignDx(measured.lines[li]) + lastG.penX + lastG.advanceX;
                    caretY = textRect.y + measured.lines[li].topY;
                    caretH = std::max(1.0f, measured.lines[li].lineHeight);
                }
            }
            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
            (void)tree.AllocateRoundedRectNode(
                caretX,
                caretY,
                1.5f,
                caretH,
                UnpackRgba32(txt.caretRgba, effOpacity),
                CornerRadius{},
                &node);
            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
            ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
            count += (endNid - beginNid);
        }
        return count;
    }

std::size_t VisualRenderAdapter::EmitImageVisual(
    PagedRenderTree& tree,
    const VisualNode& node,
    const Rect& rect,
    float effOpacity,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D,
    BlendingMode blend) {
    const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
    const VisualImageData& img = node.Image();
    if (img.uri.empty() || !HasAlpha(img.tintRgba)) {
        return 0u;
    }
    UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
    const ImageAtlasEntry* entry = store->Images().LoadOrGet(img.uri);
    if (!entry || entry->width == 0u || entry->height == 0u ||
        rect.width <= 0.0f || rect.height <= 0.0f) {
        return 0u;
    }
    const float imgW = static_cast<float>(entry->width);
    const float imgH = static_cast<float>(entry->height);
    const float vx = std::clamp(img.viewboxX, 0.0f, 0.9999f);
    const float vy = std::clamp(img.viewboxY, 0.0f, 0.9999f);
    const float vw = std::clamp(img.viewboxW, 1e-4f, 1.0f - vx);
    const float vh = std::clamp(img.viewboxH, 1e-4f, 1.0f - vy);
    const float vbW = imgW * vw;
    const float vbH = imgH * vh;
    if (vbW <= 0.0f || vbH <= 0.0f) {
        return 0u;
    }

    float sx = 1.0f;
    float sy = 1.0f;
    switch (img.stretch) {
        case Stretch::None:
            sx = 1.0f;
            sy = 1.0f;
            break;
        case Stretch::Fill:
            sx = rect.width / vbW;
            sy = rect.height / vbH;
            break;
        case Stretch::Uniform: {
            const float s = std::min(rect.width / vbW, rect.height / vbH);
            sx = s;
            sy = s;
            break;
        }
        case Stretch::UniformToFill: {
            const float s = std::max(rect.width / vbW, rect.height / vbH);
            sx = s;
            sy = s;
            break;
        }
    }
    if (img.stretch != Stretch::None) {
        if (img.stretchDir == StretchDirection::UpOnly) {
            sx = std::max(1.0f, sx);
            sy = std::max(1.0f, sy);
        } else if (img.stretchDir == StretchDirection::DownOnly) {
            sx = std::min(1.0f, sx);
            sy = std::min(1.0f, sy);
        }
    }

    // Map the Viewbox rectangle into rect, then intersect the full unclipped source image with rect
    const float placedVbW = vbW * sx;
    const float placedVbH = vbH * sy;
    const float placedVbX = rect.x + (rect.width - placedVbW) * 0.5f;
    const float placedVbY = rect.y + (rect.height - placedVbH) * 0.5f;
    const float imgX0 = placedVbX - (vx * imgW) * sx;
    const float imgY0 = placedVbY - (vy * imgH) * sy;
    const float fullImgW = std::max(1e-4f, imgW * sx);
    const float fullImgH = std::max(1e-4f, imgH * sy);

    const float ix0 = std::max(rect.x, imgX0);
    const float iy0 = std::max(rect.y, imgY0);
    const float ix1 = std::min(rect.x + rect.width, imgX0 + fullImgW);
    const float iy1 = std::min(rect.y + rect.height, imgY0 + fullImgH);
    if (ix1 <= ix0 || iy1 <= iy0) {
        return 0u;
    }

    const float srcU0 = std::clamp((ix0 - imgX0) / fullImgW, 0.0f, 1.0f);
    const float srcU1 = std::clamp((ix1 - imgX0) / fullImgW, 0.0f, 1.0f);
    const float srcV0 = std::clamp((iy0 - imgY0) / fullImgH, 0.0f, 1.0f);
    const float srcV1 = std::clamp((iy1 - imgY0) / fullImgH, 0.0f, 1.0f);
    const float spanU = entry->u1 - entry->u0;
    const float spanV = entry->v1 - entry->v0;
    const bool flipRtlImg = node.layout.effectiveRtl && (node.kind == VisualKind::Box);
    const Color tintedColor = UnpackRgba32(img.tintRgba, effOpacity);

    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
    if (img.tileWrap && placedVbW >= 1.0f && placedVbH >= 1.0f &&
        (placedVbW < rect.width - 0.5f || placedVbH < rect.height - 0.5f)) {
        const float tileW = placedVbW;
        const float tileH = placedVbH;
        const int cols = std::max(1, static_cast<int>(std::ceil(rect.width / tileW)));
        const int rows = std::max(1, static_cast<int>(std::ceil(rect.height / tileH)));
        if (cols * rows <= 256) {
            for (int r = 0; r < rows; ++r) {
                const float ty0 = rect.y + static_cast<float>(r) * tileH;
                const float ty1 = std::min(rect.y + rect.height, ty0 + tileH);
                if (ty1 <= ty0) continue;
                const float tv0 = vy + ((ty0 - ty0) / tileH) * vh;
                const float tv1 = vy + ((ty1 - ty0) / tileH) * vh;
                for (int c = 0; c < cols; ++c) {
                    const float tx0 = rect.x + static_cast<float>(c) * tileW;
                    const float tx1 = std::min(rect.x + rect.width, tx0 + tileW);
                    if (tx1 <= tx0) continue;
                    const float tu0 = vx;
                    const float tu1 = vx + ((tx1 - tx0) / tileW) * vw;
                    const float fu0 = entry->u0 + (flipRtlImg ? tu1 : tu0) * spanU;
                    const float fu1 = entry->u0 + (flipRtlImg ? tu0 : tu1) * spanU;
                    (void)tree.AllocateTexturedNode(
                        tx0,
                        ty0,
                        tx1 - tx0,
                        ty1 - ty0,
                        fu0,
                        entry->v0 + tv0 * spanV,
                        fu1,
                        entry->v0 + tv1 * spanV,
                        tintedColor,
                        1.0f,
                        &node,
                        entry->textureId);
                }
            }
            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
            ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
            return endNid - beginNid;
        }
    }

    const float finalU0 = entry->u0 + (flipRtlImg ? srcU1 : srcU0) * spanU;
    const float finalU1 = entry->u0 + (flipRtlImg ? srcU0 : srcU1) * spanU;
    (void)tree.AllocateTexturedNode(
        ix0,
        iy0,
        ix1 - ix0,
        iy1 - iy0,
        finalU0,
        entry->v0 + srcV0 * spanV,
        finalU1,
        entry->v0 + srcV1 * spanV,
        tintedColor,
        1.0f,
        &node,
        entry->textureId);
    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
    return endNid - beginNid;
}

std::size_t VisualRenderAdapter::EmitVectorVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        std::size_t count = 0u;
        const VisualVectorData& vec = node.Vector();
        const VisualImageData& fillImg = node.Image();

        switch (vec.shapeKind) {
            case VectorShapeKind::SvgPath: {
                const float effFillOp = std::clamp(effOpacity * vec.fillOpacity, 0.0f, 1.0f);
                const bool hasImageFill = !fillImg.uri.empty() && effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                if (hasImageFill) {
                    if (!vec.pathOrParams.empty()) {
                        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
                        const Rect rawBounds = MeasureSvgPathBounds(vec.pathOrParams);
                        Rect fitted = rect;
                        if (rawBounds.width > 0.0f && rawBounds.height > 0.0f) {
                            fitted = (vec.stretch == Stretch::None)
                                ? Rect{rect.x + rawBounds.x, rect.y + rawBounds.y, rawBounds.width, rawBounds.height}
                                : ComputeStretchedBox(rect, rawBounds.width, rawBounds.height, vec.stretch);
                        }
                        const float dpi = std::max(0.25f, tree.RootDpiScale());
                        const float sx = std::max(0.25f, std::hypot(world2D.m00, world2D.m01) * dpi);
                        const float sy = std::max(0.25f, std::hypot(world2D.m10, world2D.m11) * dpi);
                        const std::uint32_t innerW = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(fitted.width * sx)), 1u, 2048u);
                        const std::uint32_t innerH = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(fitted.height * sy)), 1u, 2048u);
                        RasterizedPathMaskEntry maskedEntry{};
                        if (CompositeMaskedPathImage(
                                *store, store->Images(),
                                ComputeSvgPathHash(vec.pathOrParams), vec.pathOrParams, rawBounds,
                                rect, fitted, innerW, innerH, 1u,
                                fillImg.uri, fillImg.stretch, fillImg.tintRgba, fillImg.tileWrap, maskedEntry) &&
                            maskedEntry.textureId > 0u) {
                            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                            (void)tree.AllocateTexturedNode(
                                fitted.x, fitted.y, fitted.width, fitted.height,
                                maskedEntry.u0, maskedEntry.v0, maskedEntry.u1, maskedEntry.v1,
                                Color{1.0f, 1.0f, 1.0f, effFillOp}, 1.0f, &node, maskedEntry.textureId);
                            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                            ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                            count += (endNid - beginNid);
                        } else {
                            count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend);
                        }
                    } else {
                        count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend);
                    }
                }
                count += EmitSvgPathPrimitive(
                    tree,
                    rect,
                    vec.pathOrParams,
                    hasImageFill ? 0u : vec.fillRgba,
                    hasImageFill ? -1 : vec.fillGradId,
                    vec.strokeRgba,
                    vec.strokeGradId,
                    vec.strokeWidth,
                    vec.stretch,
                    vec.trimStart,
                    vec.trimEnd,
                    effOpacity,
                    &node,
                    world2D,
                    world3D,
                    has3D,
                    blend,
                    vec.fillOpacity,
                    vec.strokeOpacity);
                break;
            }

            case VectorShapeKind::Ellipse: {
                const float effFillOp = std::clamp(effOpacity * vec.fillOpacity, 0.0f, 1.0f);
                const float effStrokeOp = std::clamp(effOpacity * vec.strokeOpacity, 0.0f, 1.0f);
                const bool hasImageFill = !fillImg.uri.empty() && effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                const bool hasFill = (HasAlpha(vec.fillRgba) || vec.fillGradId > 0) && effFillOp > 0.001f;
                if (hasImageFill) {
                    count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend);
                } else if (hasFill) {
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    if (vec.fillGradId > 0) {
                        const Color tint{1.0f, 1.0f, 1.0f, effFillOp};
                        (void)tree.AllocateGradientRoundedRectNode(
                            rect.x,
                            rect.y,
                            rect.width,
                            rect.height,
                            static_cast<std::uint32_t>(vec.fillGradId),
                            CornerRadius{},
                            true,
                            tint,
                            1.0f,
                            &node);
                    } else {
                        (void)tree.AllocateEllipseNode(
                            rect.x,
                            rect.y,
                            rect.width,
                            rect.height,
                            UnpackRgba32(vec.fillRgba, effFillOp),
                            0.0f,
                            false,
                            1.0f,
                            &node);
                    }
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
                if ((HasAlpha(vec.strokeRgba) || vec.strokeGradId > 0) &&
                    vec.strokeWidth > 0.0f &&
                    effStrokeOp > 0.001f) {
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    const Color strokeCol = (vec.strokeGradId > 0)
                        ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                        : UnpackRgba32(vec.strokeRgba, effStrokeOp);
                    const std::uint32_t nid = tree.AllocateEllipseNode(
                        rect.x,
                        rect.y,
                        rect.width,
                        rect.height,
                        strokeCol,
                        vec.strokeWidth,
                        true,
                        1.0f,
                        &node);
                    AttachNodeGradientBrush(tree, nid, vec.strokeGradId);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
                break;
            }

            case VectorShapeKind::Rectangle: {
                const float effFillOp = std::clamp(effOpacity * vec.fillOpacity, 0.0f, 1.0f);
                const float effStrokeOp = std::clamp(effOpacity * vec.strokeOpacity, 0.0f, 1.0f);
                float rx = 0.0f;
                float ry = 0.0f;
                if (!vec.pathOrParams.empty()) {
                    const char* p = vec.pathOrParams.c_str();
                    char* endP = nullptr;
                    rx = std::max(0.0f, std::strtof(p, &endP));
                    if (endP && *endP == ',') {
                        ry = std::max(0.0f, std::strtof(endP + 1, nullptr));
                    }
                }
                const CornerRadius cr{std::max(rx, ry)};
                const bool hasImageFill = !fillImg.uri.empty() && effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                const bool hasFill = (HasAlpha(vec.fillRgba) || vec.fillGradId > 0) && effFillOp > 0.001f;
                if (hasImageFill) {
                    count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend);
                } else if (hasFill && rect.width > 0.0f && rect.height > 0.0f) {
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    if (vec.fillGradId > 0) {
                        const Color tint{1.0f, 1.0f, 1.0f, effFillOp};
                        (void)tree.AllocateGradientRoundedRectNode(
                            rect.x,
                            rect.y,
                            rect.width,
                            rect.height,
                            static_cast<std::uint32_t>(vec.fillGradId),
                            cr,
                            false,
                            tint,
                            1.0f,
                            &node);
                    } else {
                        (void)tree.AllocateRoundedRectNode(
                            rect.x,
                            rect.y,
                            rect.width,
                            rect.height,
                            UnpackRgba32(vec.fillRgba, effFillOp),
                            cr,
                            &node);
                    }
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
                const bool hasStroke = (HasAlpha(vec.strokeRgba) || vec.strokeGradId > 0) &&
                    vec.strokeWidth > 0.0f && effStrokeOp > 0.001f;
                if (hasStroke && rect.width > 0.0f && rect.height > 0.0f) {
                    Color strokeCol = UnpackRgba32(vec.strokeRgba, effStrokeOp);
                    if (vec.strokeGradId > 0) {
                        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
                        const GradientBrushDesc* gb = store->GradientBrushes().Find(
                            static_cast<std::uint32_t>(vec.strokeGradId));
                        if (!gb) {
                            gb = GradientBrushRegistry::Instance().Find(
                                static_cast<std::uint32_t>(vec.strokeGradId));
                        }
                        if (gb && !gb->stops.empty()) {
                            Color sampled = SampleGradientRampAtT(*gb, 0.5f);
                            sampled.a *= gb->opacity * effStrokeOp;
                            if (sampled.a > 0.0f) {
                                strokeCol = sampled;
                            }
                        }
                    }
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    (void)tree.AllocateBorderRingNode(
                        rect.x,
                        rect.y,
                        rect.width,
                        rect.height,
                        strokeCol,
                        cr,
                        Thickness{vec.strokeWidth},
                        &node);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
                break;
            }

            case VectorShapeKind::Line: {
                const float effStrokeOp = std::clamp(effOpacity * vec.strokeOpacity, 0.0f, 1.0f);
                if (!HasAlpha(vec.strokeRgba) || vec.strokeWidth <= 0.0f || effStrokeOp <= 0.001f) {
                    break;
                }
                const std::vector<float> nums = ParseCsvFloats(vec.pathOrParams);
                Point p0{rect.x, rect.y};
                Point p1{rect.x + rect.width, rect.y + rect.height};
                if (nums.size() >= 4u) {
                    p0 = Point{rect.x + nums[0], rect.y + nums[1]};
                    p1 = Point{rect.x + nums[2], rect.y + nums[3]};
                }
                const float s0 = std::clamp(vec.trimStart, 0.0f, 1.0f);
                const float s1 = std::clamp(vec.trimEnd, 0.0f, 1.0f);
                if (s1 > s0) {
                    const Point tp0{p0.x + (p1.x - p0.x) * s0, p0.y + (p1.y - p0.y) * s0};
                    const Point tp1{p0.x + (p1.x - p0.x) * s1, p0.y + (p1.y - p0.y) * s1};
                    count += EmitLineSegment(
                        tree,
                        tp0,
                        tp1,
                        vec.strokeWidth,
                        UnpackRgba32(vec.strokeRgba, effStrokeOp),
                        &node,
                        world2D,
                        world3D,
                        has3D,
                        blend);
                }
                break;
            }

            case VectorShapeKind::RadialArc: {
                const std::uint32_t rgba = HasAlpha(vec.strokeRgba) ? vec.strokeRgba : vec.fillRgba;
                if (!HasAlpha(rgba)) {
                    break;
                }
                const std::vector<float> nums = ParseCsvFloats(vec.pathOrParams);
                const float startDeg = (nums.size() >= 1u) ? nums[0] : 0.0f;
                const float sweepDeg = (nums.size() >= 2u) ? nums[1] : 360.0f;
                const float innerRatio = (nums.size() >= 3u)
                    ? std::clamp(nums[2], 0.0f, 0.99f)
                    : 0.7f;
                const float s0 = std::clamp(vec.trimStart, 0.0f, 1.0f);
                const float s1 = std::clamp(vec.trimEnd, 0.0f, 1.0f);
                if (s1 > s0) {
                    const float effStart = startDeg + sweepDeg * s0;
                    const float effSweep = sweepDeg * (s1 - s0);
                    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    (void)tree.AllocateRadialArcNode(
                        rect.x,
                        rect.y,
                        rect.width,
                        rect.height,
                        effStart,
                        effSweep,
                        innerRatio,
                        UnpackRgba32(rgba, effOpacity),
                        1.0f,
                        &node);
                    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                    ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                    count += (endNid - beginNid);
                }
                break;
            }
        }
        return count;
    }

std::size_t VisualRenderAdapter::EmitCanvasDrawVisual(
        PagedRenderTree& tree,
        const DrawingCommandBuffer* drawBuffer,
        VisualNodeId id,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        if (!drawBuffer) {
            return 0u;
        }
        const DrawingCommandList* list = drawBuffer->FindCommands(id);
        if (!list || list->commands.empty()) {
            return 0u;
        }

        struct SavedClip {
            bool active = false;
            Rect rect{};
        };
        std::vector<SavedClip> clipStack;
        std::vector<Matrix2D> matrixStack;
        std::vector<float> opacityStack;

        Matrix2D localCanvas2D = Matrix2D::Identity();
        float curOpacity = effOpacity;
        const Matrix2D toRectSpace = Matrix2D::Translation(rect.x, rect.y);
        const Matrix2D fromRectSpace = Matrix2D::Translation(-rect.x, -rect.y);

        auto currentWorld2D = [&]() noexcept -> Matrix2D {
            if (localCanvas2D.IsIdentity()) {
                return world2D;
            }
            const Matrix2D rectM = Matrix2D::Multiply(
                Matrix2D::Multiply(toRectSpace, localCanvas2D),
                fromRectSpace);
            return Matrix2D::Multiply(world2D, rectM);
        };
        auto currentWorld3D = [&]() noexcept -> Matrix4x4 {
            if (!has3D || localCanvas2D.IsIdentity()) {
                return world3D;
            }
            const Matrix2D rectM = Matrix2D::Multiply(
                Matrix2D::Multiply(toRectSpace, localCanvas2D),
                fromRectSpace);
            return Matrix4x4::Multiply(world3D, Matrix4x4::FromMatrix2D(rectM));
        };

        std::size_t count = 0u;
        for (const DrawCommand& cmd : list->commands) {
            const Matrix2D cmdW2D = currentWorld2D();
            const Matrix4x4 cmdW3D = currentWorld3D();

            switch (cmd.kind) {
                case DrawCommandKind::Clear: {
                    if (HasAlpha(cmd.colorRgba)) {
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        (void)tree.AllocateRoundedRectNode(
                            rect.x, rect.y, rect.width, rect.height,
                            UnpackRgba32(cmd.colorRgba, curOpacity), CornerRadius{}, &node);
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::FillRect: {
                    if (cmd.gradientId > 0 || HasAlpha(cmd.colorRgba)) {
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        if (cmd.gradientId > 0) {
                            const Color tint{1.0f, 1.0f, 1.0f, curOpacity};
                            (void)tree.AllocateGradientRoundedRectNode(
                                rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                                static_cast<std::uint32_t>(cmd.gradientId), CornerRadius{}, false, tint, 1.0f, &node);
                        } else {
                            (void)tree.AllocateRoundedRectNode(
                                rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                                UnpackRgba32(cmd.colorRgba, curOpacity), CornerRadius{}, &node);
                        }
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::DrawRect: {
                    if (HasAlpha(cmd.colorRgba) && cmd.strokeWidth > 0.0f) {
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        (void)tree.AllocateBorderRingNode(
                            rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                            UnpackRgba32(cmd.colorRgba, curOpacity), CornerRadius{}, Thickness{cmd.strokeWidth}, &node);
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::FillRoundedRect: {
                    const float radius = std::max(0.0f, (cmd.p1 > 0.0f) ? (0.5f * (cmd.p0 + cmd.p1)) : cmd.p0);
                    if (cmd.gradientId > 0 || HasAlpha(cmd.colorRgba)) {
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        if (cmd.gradientId > 0) {
                            const Color tint{1.0f, 1.0f, 1.0f, curOpacity};
                            (void)tree.AllocateGradientRoundedRectNode(
                                rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                                static_cast<std::uint32_t>(cmd.gradientId), CornerRadius{radius}, false, tint, 1.0f, &node);
                        } else {
                            (void)tree.AllocateRoundedRectNode(
                                rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                                UnpackRgba32(cmd.colorRgba, curOpacity), CornerRadius{radius}, &node);
                        }
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::DrawRoundedRect: {
                    const float radius = std::max(0.0f, (cmd.p1 > 0.0f) ? (0.5f * (cmd.p0 + cmd.p1)) : cmd.p0);
                    if (HasAlpha(cmd.colorRgba) && cmd.strokeWidth > 0.0f) {
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        (void)tree.AllocateBorderRingNode(
                            rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height,
                            UnpackRgba32(cmd.colorRgba, curOpacity), CornerRadius{radius}, Thickness{cmd.strokeWidth}, &node);
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::FillEllipse: {
                    if (cmd.gradientId > 0 || HasAlpha(cmd.colorRgba)) {
                        const float bx = rect.x + cmd.rect.x - cmd.rect.width;
                        const float by = rect.y + cmd.rect.y - cmd.rect.height;
                        const float bw = cmd.rect.width * 2.0f;
                        const float bh = cmd.rect.height * 2.0f;
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        if (cmd.gradientId > 0) {
                            const Color tint{1.0f, 1.0f, 1.0f, curOpacity};
                            (void)tree.AllocateGradientRoundedRectNode(
                                bx, by, bw, bh, static_cast<std::uint32_t>(cmd.gradientId),
                                CornerRadius{}, true, tint, 1.0f, &node);
                        } else {
                            (void)tree.AllocateEllipseNode(
                                bx, by, bw, bh, UnpackRgba32(cmd.colorRgba, curOpacity),
                                0.0f, false, 1.0f, &node);
                        }
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::DrawEllipse: {
                    if (HasAlpha(cmd.colorRgba) && cmd.strokeWidth > 0.0f) {
                        const float bx = rect.x + cmd.rect.x - cmd.rect.width;
                        const float by = rect.y + cmd.rect.y - cmd.rect.height;
                        const float bw = cmd.rect.width * 2.0f;
                        const float bh = cmd.rect.height * 2.0f;
                        const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                        (void)tree.AllocateEllipseNode(
                            bx, by, bw, bh, UnpackRgba32(cmd.colorRgba, curOpacity),
                            cmd.strokeWidth, true, 1.0f, &node);
                        const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                        ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                        count += (eN - bN);
                    }
                    break;
                }
                case DrawCommandKind::DrawLine: {
                    if (HasAlpha(cmd.colorRgba) && cmd.strokeWidth > 0.0f) {
                        count += EmitLineSegment(
                            tree,
                            Point{rect.x + cmd.p0, rect.y + cmd.p1},
                            Point{rect.x + cmd.p2, rect.y + cmd.p3},
                            cmd.strokeWidth,
                            UnpackRgba32(cmd.colorRgba, curOpacity),
                            &node,
                            cmdW2D,
                            cmdW3D,
                            has3D,
                            blend);
                    }
                    break;
                }
                case DrawCommandKind::DrawPolyline: {
                    if (HasAlpha(cmd.colorRgba) && cmd.strokeWidth > 0.0f && cmd.points.size() >= 2u) {
                        const Color col = UnpackRgba32(cmd.colorRgba, curOpacity);
                        for (std::size_t pi = 1u; pi < cmd.points.size(); ++pi) {
                            count += EmitLineSegment(
                                tree,
                                Point{rect.x + cmd.points[pi - 1u].x, rect.y + cmd.points[pi - 1u].y},
                                Point{rect.x + cmd.points[pi].x, rect.y + cmd.points[pi].y},
                                cmd.strokeWidth,
                                col,
                                &node,
                                cmdW2D,
                                cmdW3D,
                                has3D,
                                blend);
                        }
                    }
                    break;
                }
                case DrawCommandKind::DrawPath: {
                    const Rect dest{
                        rect.x + cmd.rect.x,
                        rect.y + cmd.rect.y,
                        (cmd.rect.width > 0.0f) ? cmd.rect.width : rect.width,
                        (cmd.rect.height > 0.0f) ? cmd.rect.height : rect.height};
                    count += EmitSvgPathPrimitive(
                        tree,
                        dest,
                        cmd.text,
                        cmd.colorRgba,
                        cmd.gradientId,
                        cmd.secondaryRgba,
                        -1,
                        cmd.strokeWidth,
                        Stretch::Uniform,
                        0.0f,
                        1.0f,
                        curOpacity,
                        &node,
                        cmdW2D,
                        cmdW3D,
                        has3D,
                        blend);
                    break;
                }
                case DrawCommandKind::DrawImage: {
                    if (!cmd.text.empty() && HasAlpha(cmd.colorRgba)) {
                        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
                        const ImageAtlasEntry* entry = store->Images().LoadOrGet(cmd.text);
                        if (entry && entry->width > 0u && entry->height > 0u) {
                            const Rect dest{rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height};
                            const ImageStretchPlacement place = ComputeImageStretchPlacement(
                                dest,
                                static_cast<float>(entry->width),
                                static_cast<float>(entry->height),
                                Stretch::Fill,
                                StretchDirection::Both,
                                entry->u0,
                                entry->v0,
                                entry->u1,
                                entry->v1);
                            if (place.valid && place.destRect.width > 0.0f && place.destRect.height > 0.0f) {
                                const auto bN = static_cast<std::uint32_t>(tree.nodes_.size());
                                (void)tree.AllocateTexturedNode(
                                    place.destRect.x, place.destRect.y,
                                    place.destRect.width, place.destRect.height,
                                    place.u0, place.v0, place.u1, place.v1,
                                    UnpackRgba32(cmd.colorRgba, curOpacity),
                                    1.0f,
                                    &node,
                                    entry->textureId);
                                const auto eN = static_cast<std::uint32_t>(tree.nodes_.size());
                                ApplyNodeTransform(tree, bN, eN, cmdW2D, cmdW3D, has3D, blend);
                                count += (eN - bN);
                            }
                        }
                    }
                    break;
                }
                case DrawCommandKind::DrawText: {
                    if (HasAlpha(cmd.colorRgba) && !cmd.text.empty()) {
                        const std::size_t sep = cmd.text.find('\0');
                        const std::string_view fontFam = (sep != std::string::npos)
                            ? std::string_view(cmd.text).substr(0u, sep)
                            : std::string_view{};
                        const std::string_view txt = (sep != std::string::npos)
                            ? std::string_view(cmd.text).substr(sep + 1u)
                            : std::string_view(cmd.text);
                        if (!txt.empty()) {
                            TextAlignment tAlign = TextAlignment::Left;
                            const auto hAlign = static_cast<HorizontalAlignment>(cmd.intArg0);
                            if (hAlign == HorizontalAlignment::Center) tAlign = TextAlignment::Center;
                            else if (hAlign == HorizontalAlignment::Right) tAlign = TextAlignment::Right;

                            PagedRenderTraversalState state{};
                            state.clipRect = tree.activeClipRect_;
                            state.clipActive = tree.activeClipEnabled_;
                            state.inheritedOpacity = curOpacity;
                            state.m00 = cmdW2D.m00;
                            state.m01 = cmdW2D.m01;
                            state.m10 = cmdW2D.m10;
                            state.m11 = cmdW2D.m11;
                            state.tx = cmdW2D.tx;
                            state.ty = cmdW2D.ty;
                            state.hasPerspective3D = has3D;
                            state.blendMode = blend;
                            state.world3D = cmdW3D;
                            const float boxW = (cmd.rect.width > 0.0f) ? cmd.rect.width : 1000000.0f;
                            count += tree.EmitTextRunNodes(
                                rect.x + cmd.rect.x,
                                rect.y + cmd.rect.y,
                                (cmd.p0 > 0.0f) ? cmd.p0 : 14.0f,
                                txt,
                                UnpackRgba32(cmd.colorRgba, curOpacity),
                                curOpacity,
                                state,
                                &node,
                                fontFam,
                                boxW,
                                TextWrapping::NoWrap,
                                tAlign,
                                cmd.rect.width,
                                nullptr);
                        }
                    }
                    break;
                }
                case DrawCommandKind::PushClip: {
                    clipStack.push_back(SavedClip{tree.activeClipEnabled_, tree.activeClipRect_});
                    const Rect localClip{rect.x + cmd.rect.x, rect.y + cmd.rect.y, cmd.rect.width, cmd.rect.height};
                    const Rect worldClip = TransformRectViewportBounds(localClip, cmdW2D, cmdW3D, has3D);
                    tree.activeClipRect_ = tree.activeClipEnabled_
                        ? IntersectRects(tree.activeClipRect_, worldClip)
                        : worldClip;
                    tree.activeClipEnabled_ = true;
                    break;
                }
                case DrawCommandKind::PopClip: {
                    if (!clipStack.empty()) {
                        const SavedClip prev = clipStack.back();
                        clipStack.pop_back();
                        tree.activeClipEnabled_ = prev.active;
                        tree.activeClipRect_ = prev.rect;
                    }
                    break;
                }
                case DrawCommandKind::PushTransform: {
                    matrixStack.push_back(localCanvas2D);
                    Matrix2D stepM = Matrix2D::Identity();
                    if (std::fabs(cmd.p2 - 1.0f) > 1e-6f || std::fabs(cmd.p3 - 1.0f) > 1e-6f) {
                        stepM = Matrix2D::Multiply(Matrix2D::Scaling(cmd.p2, cmd.p3), stepM);
                    }
                    if (std::fabs(cmd.strokeWidth) > 1e-6f) {
                        stepM = Matrix2D::Multiply(Matrix2D::Rotation(cmd.strokeWidth), stepM);
                    }
                    stepM.tx += cmd.p0;
                    stepM.ty += cmd.p1;
                    localCanvas2D = Matrix2D::Multiply(localCanvas2D, stepM);
                    break;
                }
                case DrawCommandKind::PopTransform: {
                    if (!matrixStack.empty()) {
                        localCanvas2D = matrixStack.back();
                        matrixStack.pop_back();
                    }
                    break;
                }
                case DrawCommandKind::PushOpacity: {
                    opacityStack.push_back(curOpacity);
                    curOpacity = std::clamp(curOpacity * cmd.p0, 0.0f, 1.0f);
                    break;
                }
                case DrawCommandKind::PopOpacity: {
                    if (!opacityStack.empty()) {
                        curOpacity = opacityStack.back();
                        opacityStack.pop_back();
                    }
                    break;
                }
            }
        }

        if (!clipStack.empty()) {
            tree.activeClipEnabled_ = clipStack.front().active;
            tree.activeClipRect_ = clipStack.front().rect;
        }
        return count;
    }

[[nodiscard]] bool NodeNeedsOpacityMaskSpan(
    const VisualNode& node,
    const UIResourceStore* store) noexcept {
    const VisualOpacityMask& om = node.OpacityMask();
    if (!om.enabled || node.layout.layoutRect.width <= 0.0f || node.layout.layoutRect.height <= 0.0f) {
        return false;
    }
    const float maskOp = std::clamp(om.opacity, 0.0f, 1.0f);
    if (om.gradId <= 0) {
        const float solidA = static_cast<float>((om.solidRgba >> 24u) & 0xFFu) / 255.0f;
        return (solidA * maskOp) < 0.999f;
    }
    GradientBrushDesc desc{};
    const auto brushId = static_cast<std::uint32_t>(om.gradId);
    const bool found = (store != nullptr && store->GradientBrushes().TryGetBrush(brushId, desc)) ||
                       GradientBrushRegistry::Instance().TryGetBrush(brushId, desc);
    if (!found || desc.stops.empty()) {
        return maskOp < 0.999f;
    }
    for (std::size_t i = 0u; i < desc.stops.size() && i < om.stopOffsets.size(); ++i) {
        if (std::isfinite(om.stopOffsets[i])) {
            desc.stops[i].offset = std::clamp(om.stopOffsets[i], 0.0f, 1.0f);
        }
    }
    std::stable_sort(
        desc.stops.begin(),
        desc.stops.end(),
        [](const GradientStop& a, const GradientStop& b) noexcept {
            return a.offset < b.offset;
        });
    const float brushOp = std::clamp(desc.opacity, 0.0f, 1.0f) * maskOp;
    if (brushOp < 0.999f) {
        return true;
    }
    bool allOpaque = true;
    for (const auto& s : desc.stops) {
        if (s.color.a < 0.999f) {
            allOpaque = false;
            break;
        }
    }
    if (allOpaque) {
        return false;
    }
    // At rest, a Pad mask whose first stop is opaque white at offset 1.0 covers t in [0, 1] at 100% alpha.
    if (desc.spreadMethod == GradientSpreadMethod::Pad &&
        desc.stops.front().offset >= 0.999f &&
        desc.stops.front().color.a >= 0.999f) {
        return false;
    }
    return true;
}

std::size_t VisualRenderAdapter::EmitNodeRecursive(
        VisualWorld* mutableWorld,
        const VisualWorld& world,
        const DrawingCommandBuffer* drawBuffer,
        VisualNodeId id,
        float parentOpacity,
        PagedRenderTree& outTree,
        const VisualEffectStyle* inheritedDropShadow) {
        const VisualNode* node = world.GetNode(id);
        if (!node || node->layout.visibility == Visibility::Collapsed) {
            return 0u;
        }

        const auto nodeRenderStart = static_cast<std::uint32_t>(outTree.nodes_.size());
        std::size_t count = 0u;
        const float effOpacity = std::clamp(parentOpacity * node->layout.opacity, 0.0f, 1.0f);
        const Rect& rect = node->layout.layoutRect;
        const Matrix2D& world2D = node->layout.worldTransform;
        const Matrix4x4& world3D = node->WorldMatrix3D();
        const bool has3D = node->layout.hasPerspective3D;
        const BlendingMode blend = node->transform2D.blendMode;
        const ScopedBlendMode blendGuard(outTree.activeBlendMode_, blend);
        const VisualImageData& imgData = node->Image();
        const VisualEffectStyle& effStyle = node->Effect();
        const VisualOpacityMask& maskStyle = node->OpacityMask();

        const bool isVisible = (node->layout.visibility == Visibility::Visible);
        const bool hasOwnBoxVisual =
            (node->box.bgOpacity > 0.001f && (HasAlpha(node->box.bgRgba) || node->box.bgGradId > 0)) ||
            (!imgData.uri.empty() && HasAlpha(imgData.tintRgba)) ||
            (HasPositiveThickness(node->box.borderThickness) &&
             (HasAlpha(node->box.borderRgba) || node->box.borderGradId > 0));
        const bool drawsSelf =
            (node->kind == VisualKind::Vector ||
             node->kind == VisualKind::Text ||
             node->kind == VisualKind::Image ||
             node->kind == VisualKind::CanvasDraw ||
             hasOwnBoxVisual ||
             node->box.isBoxShadow);

        const VisualEffectStyle* ownDropShadow =
            (effStyle.kind == UIEffectKind::DropShadow) ? &effStyle : nullptr;
        const VisualEffectStyle* activeDropShadow =
            drawsSelf
                ? (ownDropShadow ? ownDropShadow : (effStyle.kind == UIEffectKind::None ? inheritedDropShadow : nullptr))
                : nullptr;
        const VisualEffectStyle* childInheritedShadow =
            !drawsSelf
                ? (ownDropShadow ? ownDropShadow : (effStyle.kind == UIEffectKind::None ? inheritedDropShadow : nullptr))
                : nullptr;

        const float worldScale = std::max(
            0.25f,
            0.5f * (std::hypot(world2D.m00, world2D.m01) + std::hypot(world2D.m10, world2D.m11)));

        // Emit DropShadow behind this element (outside element clip) before node content.
        if (isVisible &&
            activeDropShadow != nullptr &&
            rect.width > 0.0f &&
            rect.height > 0.0f &&
            HasAlpha(activeDropShadow->colorRgba) &&
            activeDropShadow->opacity > 0.0f) {
            const float rad = activeDropShadow->dirDeg * kDegToRad;
            const float scaleOff = has3D ? 1.0f : worldScale;
            const float dx = std::cos(rad) * activeDropShadow->depth * scaleOff;
            const float dy = -std::sin(rad) * activeDropShadow->depth * scaleOff;
            const float blurR = std::max(0.0f, activeDropShadow->radius);
            count += EmitDropShadowLeafSilhouette(
                outTree,
                *node,
                rect,
                effOpacity,
                world2D,
                world3D,
                has3D,
                blend,
                *activeDropShadow,
                dx,
                dy,
                blurR,
                true);
        }

        // Open post-process BlurLayerSpan BEFORE child emission to guarantee pre-order span ordering.
        const bool hasPostProcessEffect =
            isVisible &&
            effStyle.kind != UIEffectKind::None &&
            effStyle.kind != UIEffectKind::DropShadow;
        const UIResourceStore* effStore = outTree.ResourceStore() ? outTree.ResourceStore() : world.ResourceStore();
        const bool hasOpacityMask = isVisible && NodeNeedsOpacityMaskSpan(*node, effStore);
        std::size_t blurSpanIndex = std::numeric_limits<std::size_t>::max();
        const std::uint32_t spanStartNid = static_cast<std::uint32_t>(outTree.nodes_.size());
        if (hasPostProcessEffect || hasOpacityMask) {
            BlurLayerSpan span{};
            span.firstNodeId = spanStartNid;
            span.endNodeId = spanStartNid;
            span.effectMode = hasPostProcessEffect ? effStyle.kind : UIEffectKind::OpacityMask;
            span.worldScale = worldScale;
            if (hasPostProcessEffect) {
                span.radiusPx = std::max(0.0f, effStyle.radius);
                span.angleDeg = effStyle.dirDeg;
                span.amountX = (effStyle.depth != 0.0f) ? effStyle.depth : effStyle.radius;
                span.saturation = (effStyle.kind == UIEffectKind::Saturation)
                    ? ((effStyle.depth != 0.0f || effStyle.radius == 0.0f)
                           ? effStyle.depth
                           : effStyle.radius)
                    : ((effStyle.depth != 0.0f) ? effStyle.depth : 1.0f);
                span.effectColor = UnpackRgba32(effStyle.colorRgba, 1.0f);
                span.effectOpacity = std::clamp(effStyle.opacity, 0.0f, 1.0f);
            }
            if (hasOpacityMask) {
                span.hasOpacityMask = true;
                span.opacityMaskGradId = (maskStyle.gradId > 0)
                    ? static_cast<std::uint32_t>(maskStyle.gradId)
                    : 0u;
                span.opacityMaskSolidRgba = maskStyle.solidRgba;
                span.opacityMaskOpacity = std::clamp(maskStyle.opacity, 0.0f, 1.0f);
                span.opacityMaskLocalRect = rect;
                span.opacityMaskStopOffsets = maskStyle.stopOffsets;
                Matrix2D invW{};
                if (!world2D.TryInvert(invW)) {
                    invW = Matrix2D::Identity();
                }
                span.opacityMaskInvWorld2D = invW;
            }
            const Rect worldBounds = TransformRectViewportBounds(rect, world2D, world3D, has3D);
            span.clipRect = outTree.activeClipEnabled_
                ? IntersectRects(outTree.activeClipRect_, worldBounds)
                : worldBounds;
            span.clipActive = (hasPostProcessEffect && effStyle.kind == UIEffectKind::BackdropBlur) ||
                              outTree.activeClipEnabled_;
            span.blendMode = (span.effectMode == UIEffectKind::BackdropBlur)
                ? BlendingMode::Normal
                : blend;
            if (span.effectMode == UIEffectKind::BackdropBlur) {
                span.maskDestRect = worldBounds;
                span.maskOpacity = std::clamp(effOpacity * effStyle.opacity, 0.0f, 1.0f);
                if (node->kind == VisualKind::Vector) {
                    const VisualVectorData& vec = node->Vector();
                    if (vec.shapeKind == VectorShapeKind::SvgPath && !vec.pathOrParams.empty()) {
                        const Rect rawB = MeasureSvgPathBounds(vec.pathOrParams);
                        if (rawB.width > 0.0f && rawB.height > 0.0f) {
                            const Rect fitted = (vec.stretch == Stretch::None)
                                ? Rect{rect.x + rawB.x, rect.y + rawB.y, rawB.width, rawB.height}
                                : ComputeStretchedBox(rect, rawB.width, rawB.height, vec.stretch);
                            span.maskDestRect = TransformRectViewportBounds(fitted, world2D, world3D, has3D);
                            span.clipRect = outTree.activeClipEnabled_
                                ? IntersectRects(outTree.activeClipRect_, span.maskDestRect)
                                : span.maskDestRect;
                        }
                        const std::uint64_t pathHash = ComputeSvgPathHash(vec.pathOrParams);
                        UIResourceStore* mutStore = outTree.ResourceStore() ? outTree.ResourceStore() : &UIResourceStore::Default();
                        const AxbMtsdfPathEntry* storeEntry = mutStore->FindMtsdfPath(pathHash);
                        if (!storeEntry) {
                            const AxbMtsdfPathEntry* existing = outTree.FindMtsdfEntry(pathHash);
                            if (!existing && effStore) {
                                existing = effStore->FindMtsdfPath(pathHash);
                            }
                            if (existing && !existing->mtsdfRgbaPixels.empty() &&
                                existing->cellWidth > 0u && existing->cellHeight > 0u) {
                                mutStore->RegisterMtsdfPath(*existing);
                            } else {
                                float pxRange = 4.0f;
                                const float maxSpan = std::max(span.maskDestRect.width, span.maskDestRect.height);
                                const std::uint16_t cellSize = SelectAdaptiveMtsdfCellSize(
                                    vec.pathOrParams,
                                    maxSpan > 1.0f ? maxSpan : 32.0f,
                                    &pxRange);
                                const AxbMtsdfPathEntry baked = BakeSvgPathToMtsdfEntry(vec.pathOrParams, cellSize, pxRange);
                                if (!baked.mtsdfRgbaPixels.empty() && baked.cellWidth > 0u && baked.cellHeight > 0u) {
                                    mutStore->RegisterMtsdfPath(baked);
                                }
                            }
                        }
                        span.maskMtsdfHash = pathHash;
                    }
                }
            }
            blurSpanIndex = outTree.blurLayerSpans_.size();
            outTree.blurLayerSpans_.push_back(span);
        }

        // Push node scissor clip (ClipToBounds or ScrollViewport)
        const bool prevClipEnabled = outTree.activeClipEnabled_;
        const Rect prevClipRect = outTree.activeClipRect_;
        const bool clipsSelf = node->layout.clipToBounds || (node->layout.layoutMode == LayoutMode::ScrollViewport);
        bool skipClipped = false;
        if (clipsSelf) {
            const Rect nodeWorldBounds = TransformRectViewportBounds(rect, world2D, world3D, has3D);
            const Rect newClip = prevClipEnabled
                ? IntersectRects(prevClipRect, nodeWorldBounds)
                : nodeWorldBounds;
            outTree.activeClipEnabled_ = true;
            outTree.activeClipRect_ = newClip;
            if (newClip.width <= 0.0f || newClip.height <= 0.0f) {
                skipClipped = true;
            }
        }

        if (!skipClipped) {
            if (isVisible && rect.width > 0.0f && rect.height > 0.0f) {
                count += EmitBoxVisual(outTree, *node, rect, effOpacity, world2D, world3D, has3D, blend);

                switch (node->kind) {
                    case VisualKind::Text:
                        count += EmitTextVisual(outTree, *node, rect, effOpacity, world2D, world3D, has3D, blend);
                        break;
                    case VisualKind::Image:
                        count += EmitImageVisual(outTree, *node, rect, effOpacity, world2D, world3D, has3D, blend);
                        break;
                    case VisualKind::Vector:
                        count += EmitVectorVisual(outTree, *node, rect, effOpacity, world2D, world3D, has3D, blend);
                        break;
                    case VisualKind::CanvasDraw:
                        count += EmitCanvasDrawVisual(
                            outTree, drawBuffer, id, *node, rect, effOpacity, world2D, world3D, has3D, blend);
                        break;
                    case VisualKind::Box:
                    default:
                        break;
                }
            }

            if (!node->children.empty()) {
                struct ZChild {
                    VisualNodeId id{};
                    std::int32_t z = 0;
                    std::size_t order = 0u;
                };
                std::vector<ZChild> sorted;
                sorted.reserve(node->children.size());
                for (std::size_t ci = 0u; ci < node->children.size(); ++ci) {
                    if (const VisualNode* ch = world.GetNode(node->children[ci])) {
                        sorted.push_back(ZChild{node->children[ci], ch->layout.zIndex, ci});
                    }
                }
                std::sort(sorted.begin(), sorted.end(), [](const ZChild& a, const ZChild& b) {
                    if (a.z != b.z) {
                        return a.z < b.z;
                    }
                    return a.order < b.order;
                });

                const VisualEffectStyle* passInheritedShadow = childInheritedShadow;
                if (isVisible &&
                    childInheritedShadow != nullptr &&
                    HasAlpha(childInheritedShadow->colorRgba) &&
                    childInheritedShadow->opacity > 0.0f) {
                    const float rad = childInheritedShadow->dirDeg * kDegToRad;
                    const float scaleOff = has3D ? 1.0f : worldScale;
                    const float dx = std::cos(rad) * childInheritedShadow->depth * scaleOff;
                    const float dy = -std::sin(rad) * childInheritedShadow->depth * scaleOff;
                    const float blurR = std::max(0.0f, childInheritedShadow->radius);
                    std::size_t shadowSpanIndex = std::numeric_limits<std::size_t>::max();
                    const auto bN = static_cast<std::uint32_t>(outTree.nodes_.size());
                    if (blurR > 0.5f) {
                        BlurLayerSpan shadowSpan{};
                        shadowSpan.firstNodeId = bN;
                        shadowSpan.endNodeId = bN;
                        shadowSpan.effectMode = UIEffectKind::Blur;
                        shadowSpan.radiusPx = blurR;
                        shadowSpan.worldScale = worldScale;
                        shadowSpan.effectOpacity = 1.0f;
                        shadowSpan.blendMode = blend;
                        shadowSpan.clipRect = outTree.activeClipRect_;
                        shadowSpan.clipActive = outTree.activeClipEnabled_;
                        shadowSpanIndex = outTree.blurLayerSpans_.size();
                        outTree.blurLayerSpans_.push_back(shadowSpan);
                    }
                    for (const ZChild& item : sorted) {
                        count += EmitDropShadowSubtreeRecursive(
                            world,
                            item.id,
                            effOpacity,
                            outTree,
                            *childInheritedShadow,
                            dx,
                            dy,
                            blurR);
                    }
                    const auto eN = static_cast<std::uint32_t>(outTree.nodes_.size());
                    if (shadowSpanIndex < outTree.blurLayerSpans_.size()) {
                        outTree.blurLayerSpans_[shadowSpanIndex].endNodeId = eN;
                        if (eN <= bN) {
                            outTree.blurLayerSpans_.erase(
                                outTree.blurLayerSpans_.begin() + static_cast<std::ptrdiff_t>(shadowSpanIndex));
                        }
                    }
                    passInheritedShadow = nullptr;
                }

                for (const ZChild& item : sorted) {
                    count += EmitNodeRecursive(
                        mutableWorld,
                        world,
                        drawBuffer,
                        item.id,
                        effOpacity,
                        outTree,
                        passInheritedShadow);
                }
            }
        }

        outTree.activeClipEnabled_ = prevClipEnabled;
        outTree.activeClipRect_ = prevClipRect;

        if (blurSpanIndex < outTree.blurLayerSpans_.size()) {
            const auto spanEndNid = static_cast<std::uint32_t>(outTree.nodes_.size());
            BlurLayerSpan& span = outTree.blurLayerSpans_[blurSpanIndex];
            span.endNodeId = spanEndNid;
            if (span.endNodeId <= span.firstNodeId && span.effectMode != UIEffectKind::BackdropBlur) {
                outTree.blurLayerSpans_.erase(
                    outTree.blurLayerSpans_.begin() + static_cast<std::ptrdiff_t>(blurSpanIndex));
            }
        }

        if (mutableWorld) {
            if (VisualNode* mutNode = mutableWorld->GetNode(id)) {
                mutNode->layout.renderNodeId = nodeRenderStart;
                mutNode->layout.renderNodeCount = static_cast<std::uint32_t>(
                    outTree.nodes_.size() - nodeRenderStart);
            }
        }
        return count;
}

std::size_t VisualRenderAdapter::EmitDropShadowLeafSilhouette(
    PagedRenderTree& outTree,
    const VisualNode& node,
    const Rect& rect,
    float effOpacity,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D,
    BlendingMode blend,
    const VisualEffectStyle& dropShadow,
    float dx,
    float dy,
    float blurR,
    bool wrapWithBlurSpan) {
    const ScopedBlendMode blendGuard(outTree.activeBlendMode_, blend);
    if (rect.width <= 0.0f || rect.height <= 0.0f ||
        !HasAlpha(dropShadow.colorRgba) || dropShadow.opacity <= 0.0f) {
        return 0u;
    }
    const float effShadowOpacity = effOpacity * dropShadow.opacity;
    const Color shadowCol = UnpackRgba32(dropShadow.colorRgba, effShadowOpacity);
    if (shadowCol.a <= 0.0f) {
        return 0u;
    }
    const VisualImageData& imgData = node.Image();
    const bool hasOwnBoxVisual =
        (node.box.bgOpacity > 0.001f && (HasAlpha(node.box.bgRgba) || node.box.bgGradId > 0)) ||
        (!imgData.uri.empty() && HasAlpha(imgData.tintRgba)) ||
        (HasPositiveThickness(node.box.borderThickness) &&
         (HasAlpha(node.box.borderRgba) || node.box.borderGradId > 0));

    Matrix2D shadowWorld2D = world2D;
    shadowWorld2D.tx += dx;
    shadowWorld2D.ty += dy;
    const Rect shiftedRect = has3D
        ? Rect{rect.x + dx, rect.y + dy, rect.width, rect.height}
        : rect;
    const Matrix2D& shifted2D = has3D ? world2D : shadowWorld2D;

    if (node.kind == VisualKind::Vector || (node.kind == VisualKind::Text && !hasOwnBoxVisual)) {
        std::size_t shadowSpanIndex = std::numeric_limits<std::size_t>::max();
        const auto bN = static_cast<std::uint32_t>(outTree.nodes_.size());
        const float leafScale = std::max(0.25f, 0.5f * (std::hypot(world2D.m00, world2D.m01) + std::hypot(world2D.m10, world2D.m11)));
        if (wrapWithBlurSpan && blurR > 0.5f) {
            BlurLayerSpan shadowSpan{};
            shadowSpan.firstNodeId = bN;
            shadowSpan.endNodeId = bN;
            shadowSpan.effectMode = UIEffectKind::Blur;
            shadowSpan.radiusPx = blurR;
            shadowSpan.worldScale = leafScale;
            shadowSpan.effectOpacity = 1.0f;
            shadowSpan.blendMode = blend;
            const Rect shadowWorld = TransformRectViewportBounds(shiftedRect, shifted2D, world3D, has3D);
            shadowSpan.clipRect = outTree.activeClipEnabled_
                ? IntersectRects(outTree.activeClipRect_, shadowWorld)
                : shadowWorld;
            shadowSpan.clipActive = outTree.activeClipEnabled_;
            shadowSpanIndex = outTree.blurLayerSpans_.size();
            outTree.blurLayerSpans_.push_back(shadowSpan);
        }
        std::size_t emitted = 0u;
        if (node.kind == VisualKind::Vector) {
            const VisualVectorData& srcVec = node.Vector();
            VisualNode shadowVecNode{};
            shadowVecNode.id = node.id;
            shadowVecNode.kind = node.kind;
            auto& sv = shadowVecNode.EnsureExtras().vector;
            sv = srcVec;
            const bool hasSrcImg =
                !imgData.uri.empty() && HasAlpha(imgData.tintRgba) && srcVec.fillOpacity > 0.001f;
            if (hasSrcImg) {
                auto& si = shadowVecNode.EnsureExtras().image;
                si = imgData;
                si.tintRgba = dropShadow.colorRgba;
            }
            const bool hasSrcFill =
                (HasAlpha(srcVec.fillRgba) || srcVec.fillGradId > 0) &&
                srcVec.fillOpacity > 0.001f;
            const bool hasSrcStroke =
                (HasAlpha(srcVec.strokeRgba) || srcVec.strokeGradId > 0) &&
                srcVec.strokeWidth > 0.0f &&
                srcVec.strokeOpacity > 0.001f;
            if (hasSrcFill) {
                sv.fillRgba = dropShadow.colorRgba;
                sv.fillGradId = -1;
                sv.strokeRgba = hasSrcStroke ? dropShadow.colorRgba : 0u;
                sv.strokeGradId = -1;
            } else if (hasSrcStroke) {
                sv.strokeRgba = dropShadow.colorRgba;
                sv.strokeGradId = -1;
            }
            emitted = EmitVectorVisual(
                outTree, shadowVecNode, shiftedRect, effShadowOpacity, shifted2D, world3D, has3D, blend);
        } else {
            VisualNode shadowTextNode{};
            shadowTextNode.id = node.id;
            shadowTextNode.kind = node.kind;
            shadowTextNode.layout.padding = node.layout.padding;
            auto& st = shadowTextNode.EnsureExtras().text;
            st = node.Text();
            st.fgRgba = dropShadow.colorRgba;
            st.fgGradId = -1;
            st.selLenCp = 0;
            st.showCaret = false;
            emitted = EmitTextVisual(
                outTree, shadowTextNode, shiftedRect, effShadowOpacity, shifted2D, world3D, has3D, blend, true);
        }
        const auto eN = static_cast<std::uint32_t>(outTree.nodes_.size());
        for (std::uint32_t k = bN; k < eN; ++k) {
            outTree.nodes_[k].sourceElement = &node;
        }
        if (shadowSpanIndex < outTree.blurLayerSpans_.size()) {
            outTree.blurLayerSpans_[shadowSpanIndex].endNodeId = eN;
            if (eN <= bN) {
                outTree.blurLayerSpans_.erase(
                    outTree.blurLayerSpans_.begin() + static_cast<std::ptrdiff_t>(shadowSpanIndex));
            }
        }
        return emitted;
    }

    if (!hasOwnBoxVisual && !node.box.isBoxShadow && node.kind != VisualKind::Image && node.kind != VisualKind::CanvasDraw) {
        return 0u;
    }
    const bool hollowBorderOnly =
        !node.box.isBoxShadow &&
        imgData.uri.empty() &&
        !HasAlpha(node.box.bgRgba) &&
        node.box.bgGradId <= 0 &&
        HasPositiveThickness(node.box.borderThickness);
    std::size_t boxShadowSpanIdx = std::numeric_limits<std::size_t>::max();
    const auto bN = static_cast<std::uint32_t>(outTree.nodes_.size());
    if (hollowBorderOnly) {
        (void)outTree.AllocateBorderRingNode(
            rect.x + (has3D ? dx : 0.0f),
            rect.y + (has3D ? dy : 0.0f),
            rect.width,
            rect.height,
            shadowCol,
            node.box.cornerRadius,
            node.box.borderThickness,
            &node);
    } else if (node.box.isBoxShadow && wrapWithBlurSpan && blurR > 0.5f) {
        BlurLayerSpan shadowSpan{};
        shadowSpan.firstNodeId = bN;
        shadowSpan.endNodeId = bN;
        shadowSpan.effectMode = UIEffectKind::Blur;
        shadowSpan.radiusPx = blurR;
        shadowSpan.effectOpacity = 1.0f;
        shadowSpan.blendMode = blend;
        shadowSpan.clipRect = outTree.activeClipRect_;
        shadowSpan.clipActive = outTree.activeClipEnabled_;
        boxShadowSpanIdx = outTree.blurLayerSpans_.size();
        outTree.blurLayerSpans_.push_back(shadowSpan);

        (void)outTree.AllocateRoundedRectNode(
            rect.x + (has3D ? dx : 0.0f),
            rect.y + (has3D ? dy : 0.0f),
            rect.width,
            rect.height,
            shadowCol,
            node.box.cornerRadius,
            &node);
    } else {
        const float boxBlur = wrapWithBlurSpan ? blurR : 0.0f;
        const Rect shadowRect{
            rect.x - boxBlur,
            rect.y - boxBlur,
            rect.width + 2.0f * boxBlur,
            rect.height + 2.0f * boxBlur};
        const std::uint32_t shadowNid = outTree.AllocateRoundedRectNode(
            shadowRect.x + (has3D ? dx : 0.0f),
            shadowRect.y + (has3D ? dy : 0.0f),
            shadowRect.width,
            shadowRect.height,
            shadowCol,
            node.box.cornerRadius,
            &node);
        if (wrapWithBlurSpan && shadowNid < outTree.nodes_.size()) {
            outTree.nodes_[shadowNid].softFeatherPx = std::max(0.5f, blurR);
            outTree.nodes_[shadowNid].sdfPadPx = blurR;
        }
    }
    const auto eN = static_cast<std::uint32_t>(outTree.nodes_.size());
    ApplyNodeTransform(outTree, bN, eN, shifted2D, world3D, has3D, blend);
    if (boxShadowSpanIdx < outTree.blurLayerSpans_.size()) {
        outTree.blurLayerSpans_[boxShadowSpanIdx].endNodeId = eN;
        if (eN <= bN) {
            outTree.blurLayerSpans_.erase(
                outTree.blurLayerSpans_.begin() + static_cast<std::ptrdiff_t>(boxShadowSpanIdx));
        }
    }
    return (eN - bN);
}

std::size_t VisualRenderAdapter::EmitDropShadowSubtreeRecursive(
    const VisualWorld& world,
    VisualNodeId id,
    float parentOpacity,
    PagedRenderTree& outTree,
    const VisualEffectStyle& dropShadow,
    float dx,
    float dy,
    float blurR) {
    const VisualNode* node = world.GetNode(id);
    if (!node || node->layout.visibility != Visibility::Visible) {
        return 0u;
    }
    const float effOpacity = std::clamp(parentOpacity * node->layout.opacity, 0.0f, 1.0f);
    if (effOpacity <= 0.001f) {
        return 0u;
    }
    const Rect& rect = node->layout.layoutRect;
    const Matrix2D& world2D = node->layout.worldTransform;
    const Matrix4x4& world3D = node->WorldMatrix3D();
    const bool has3D = node->layout.hasPerspective3D;
    const BlendingMode blend = node->transform2D.blendMode;
    const ScopedBlendMode blendGuard(outTree.activeBlendMode_, blend);

    const bool prevClipEnabled = outTree.activeClipEnabled_;
    const Rect prevClipRect = outTree.activeClipRect_;
    const bool clipsSelf = node->layout.clipToBounds || (node->layout.layoutMode == LayoutMode::ScrollViewport);
    bool skipClipped = false;
    if (clipsSelf) {
        const Rect nodeWorldBounds = TransformRectViewportBounds(rect, world2D, world3D, has3D);
        const Rect shiftedClipBounds{
            nodeWorldBounds.x + dx,
            nodeWorldBounds.y + dy,
            nodeWorldBounds.width,
            nodeWorldBounds.height};
        const Rect newClip = prevClipEnabled
            ? IntersectRects(prevClipRect, shiftedClipBounds)
            : shiftedClipBounds;
        outTree.activeClipEnabled_ = true;
        outTree.activeClipRect_ = newClip;
        if (newClip.width <= 0.0f || newClip.height <= 0.0f) {
            skipClipped = true;
        }
    }

    std::size_t count = 0u;
    if (!skipClipped) {
        count += EmitDropShadowLeafSilhouette(
            outTree,
            *node,
            rect,
            effOpacity,
            world2D,
            world3D,
            has3D,
            blend,
            dropShadow,
            dx,
            dy,
            blurR,
            false);

        if (!node->children.empty()) {
            struct ZChild {
                VisualNodeId id{};
                std::int32_t z = 0;
                std::size_t order = 0u;
            };
            std::vector<ZChild> sorted;
            sorted.reserve(node->children.size());
            for (std::size_t ci = 0u; ci < node->children.size(); ++ci) {
                if (const VisualNode* ch = world.GetNode(node->children[ci])) {
                    sorted.push_back(ZChild{node->children[ci], ch->layout.zIndex, ci});
                }
            }
            std::sort(sorted.begin(), sorted.end(), [](const ZChild& a, const ZChild& b) {
                if (a.z != b.z) {
                    return a.z < b.z;
                }
                return a.order < b.order;
            });
            for (const ZChild& item : sorted) {
                count += EmitDropShadowSubtreeRecursive(
                    world,
                    item.id,
                    effOpacity,
                    outTree,
                    dropShadow,
                    dx,
                    dy,
                    blurR);
            }
        }
    }
    outTree.activeClipEnabled_ = prevClipEnabled;
    outTree.activeClipRect_ = prevClipRect;
    return count;
}

std::size_t VisualRenderAdapter::BuildPagedRenderTree(
    const VisualWorld& world,
    const DrawingCommandBuffer* drawBuffer,
    PagedRenderTree& outTree,
    UIResourceStore* resourceStore) const {
    outTree.Clear();
    outTree.SetResourceStore(resourceStore ? resourceStore : const_cast<UIResourceStore*>(world.ResourceStore()));
    if (!world.Root().IsValid()) {
        return 0u;
    }
    outTree.ReserveCapacity(world.LiveNodeCount() * 2u);
    return EmitNodeRecursive(nullptr, world, drawBuffer, world.Root(), 1.0f, outTree);
}

std::size_t VisualRenderAdapter::SyncPagedRenderTree(
    VisualWorld& world,
    const DrawingCommandBuffer* drawBuffer,
    PagedRenderTree& outTree,
    UIResourceStore* resourceStore) const {
    UIResourceStore* store = resourceStore ? resourceStore : world.ResourceStore();
    outTree.SetResourceStore(store);

    // Fast Path 1: nothing dirty and render tree already matches this world & structure version
    if (!world.HasAnyDirty() &&
        !world.IsStructureDirty() &&
        drawBuffer == nullptr &&
        outTree.MatchesSyncedWorldState(world.WorldInstanceId(), world.StructureVersion())) {
        return outTree.NodeCount();
    }

    // Fast Path 2: visual-only leaf solid-box updates in place
    const auto& dirtyList = world.DirtyNodes();
    if (!world.IsStructureDirty() &&
        drawBuffer == nullptr &&
        !dirtyList.empty() &&
        outTree.MatchesSyncedWorldState(world.WorldInstanceId(), world.StructureVersion())) {
        bool allPatched = true;
        for (VisualNodeId id : dirtyList) {
            const VisualNode* node = world.GetNode(id);
            if (!node || !node->alive || !node->children.empty() ||
                node->kind != VisualKind::Box ||
                node->layout.visibility != Visibility::Visible ||
                node->layout.renderNodeCount != 1u ||
                node->layout.hasPerspective3D ||
                node->box.isBoxShadow ||
                node->box.bgGradId > 0 ||
                HasPositiveThickness(node->box.borderThickness) ||
                !node->Image().uri.empty() ||
                node->Effect().kind != UIEffectKind::None ||
                node->OpacityMask().enabled ||
                node->layout.layoutRect.width <= 0.0f ||
                node->layout.layoutRect.height <= 0.0f) {
                allPatched = false;
                break;
            }
            float effOpacity = std::clamp(node->layout.opacity, 0.0f, 1.0f);
            VisualNodeId curParent = node->parent;
            int depth = 0;
            while (curParent.IsValid() && depth < 128) {
                const VisualNode* p = world.GetNode(curParent);
                if (!p ||
                    p->layout.visibility != Visibility::Visible ||
                    p->layout.hasPerspective3D ||
                    p->Effect().kind != UIEffectKind::None ||
                    p->OpacityMask().enabled) {
                    allPatched = false;
                    break;
                }
                effOpacity *= std::clamp(p->layout.opacity, 0.0f, 1.0f);
                curParent = p->parent;
                ++depth;
            }
            if (!allPatched || curParent.IsValid()) {
                allPatched = false;
                break;
            }
            const float effFillOp = std::clamp(effOpacity * node->box.bgOpacity, 0.0f, 1.0f);
            if (!HasAlpha(node->box.bgRgba) || effFillOp <= 0.001f) {
                allPatched = false;
                break;
            }
            if (!outTree.UpdateSolidBoxNodeInPlace(
                    node->layout.renderNodeId,
                    node->layout.layoutRect,
                    UnpackRgba32(node->box.bgRgba, effFillOp),
                    node->box.cornerRadius,
                    node->layout.worldTransform,
                    node->transform2D.blendMode)) {
                allPatched = false;
                break;
            }
        }
        if (allPatched) {
            world.ClearDirtyFlags();
            return outTree.NodeCount();
        }
    }

    // Full rebuild fallback
    outTree.Clear();
    outTree.SetResourceStore(store);
    if (!world.Root().IsValid()) {
        outTree.SetSyncedWorldState(world.WorldInstanceId(), world.StructureVersion());
        world.ClearDirtyFlags();
        return 0u;
    }
    outTree.ReserveCapacity(world.LiveNodeCount() * 2u);
    const std::size_t count =
        EmitNodeRecursive(&world, world, drawBuffer, world.Root(), 1.0f, outTree);
    outTree.SetSyncedWorldState(world.WorldInstanceId(), world.StructureVersion());
    world.ClearDirtyFlags();
    return count;
}

} // namespace aero
