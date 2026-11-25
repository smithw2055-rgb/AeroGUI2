#include "aero/visual/VisualRenderAdapter.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "aero/asset/GradientBrush.hpp"
#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/RiveRenderer.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

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

[[nodiscard]] std::size_t ParseCsvFloatsFixed(
    std::string_view raw,
    float* out,
    std::size_t maxCount) noexcept {
    std::size_t count = 0u;
    const char* p = raw.data();
    const char* end = raw.data() + raw.size();
    while (p < end && count < maxCount) {
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
        out[count++] = val;
        p = next;
    }
    return count;
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

[[nodiscard]] const TextLayoutResult* ResolveCachedTextLayout(
    const VisualNode& node,
    const Rect& textRect,
    PagedRenderTree& tree,
    TextLayoutResult& outUncached) {
    const VisualTextData& txt = node.Text();
    UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
    const float maxW = (txt.wrap == TextWrapping::NoWrap && txt.trim == TextTrimming::None)
        ? 1000000.0f
        : std::max(1.0f, textRect.width);
    const float maxH = (txt.trim == TextTrimming::None)
        ? 1000000.0f
        : std::max(1.0f, textRect.height);
    const std::uint64_t curFontRev = store->Fonts().Revision();
    if (node.HasTextData() &&
        txt.hasCachedLayout &&
        txt.cachedRtl == node.layout.effectiveRtl &&
        txt.cachedFontRev == curFontRev &&
        std::fabs(txt.cachedMaxW - maxW) <= 1e-3f &&
        (std::fabs(txt.cachedMaxH - maxH) <= 1e-3f ||
         (txt.trim == TextTrimming::None &&
          maxH >= txt.cachedLayout.totalHeight - 1e-3f &&
          txt.cachedMaxH >= txt.cachedLayout.totalHeight - 1e-3f))) {
        return &txt.cachedLayout;
    }

    TextLayoutResult computed{};
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
        computed = store->Fonts().MeasureRichLayout(
            runs,
            txt.fontFamily,
            txt.fontSize,
            txt.fontWeight,
            txt.fontItalic,
            maxW,
            txt.wrap,
            txt.charSpacing,
            txt.lineHeight,
            node.layout.effectiveRtl,
            txt.trim,
            maxH);
    } else {
        computed = store->Fonts().MeasureLayout(
            txt.text,
            txt.fontFamily,
            txt.fontSize,
            maxW,
            txt.wrap,
            txt.charSpacing,
            txt.fontWeight,
            txt.fontItalic,
            txt.lineHeight,
            node.layout.effectiveRtl,
            txt.trim,
            maxH);
    }
    if (node.HasTextData()) {
        txt.cachedLayout = std::move(computed);
        txt.cachedMaxW = maxW;
        txt.cachedMaxH = maxH;
        txt.cachedRtl = node.layout.effectiveRtl;
        txt.cachedFontRev = curFontRev;
        txt.hasCachedLayout = true;
        return &txt.cachedLayout;
    }
    outUncached = std::move(computed);
    return &outUncached;
}

[[nodiscard]] bool CanSkipLeafTextClip(
    const VisualNode& node,
    const Rect& rect,
    const Matrix2D& world2D,
    bool has3D,
    PagedRenderTree& tree) {
    if (node.kind != VisualKind::Text || !node.children.empty() ||
        node.layout.layoutMode == LayoutMode::ScrollViewport) {
        return false;
    }
    if (has3D ||
        std::fabs(world2D.m01) > 1e-4f ||
        std::fabs(world2D.m10) > 1e-4f ||
        world2D.m00 < 0.0f ||
        world2D.m11 < 0.0f) {
        return false;
    }
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return false;
    }
    if (node.box.isBoxShadow ||
        node.Effect().kind != UIEffectKind::None ||
        node.OpacityMask().enabled) {
        return false;
    }
    const VisualTextData& txt = node.Text();
    if (txt.showCaret || txt.selLenCp > 0 || txt.decorations != 0) {
        return false;
    }
    if (txt.text.empty() && txt.richSpans.empty()) {
        return true;
    }
    if (txt.align == TextAlignment::Justify) {
        return false;
    }

    const float padL = std::max(0.0f, node.layout.padding.left);
    const float padT = std::max(0.0f, node.layout.padding.top);
    const float padR = std::max(0.0f, node.layout.padding.right);
    const float padB = std::max(0.0f, node.layout.padding.bottom);
    const Rect textRect{
        rect.x + padL,
        rect.y + padT,
        std::max(0.0f, rect.width - padL - padR),
        std::max(0.0f, rect.height - padT - padB)};
    if (textRect.width <= 0.0f || textRect.height <= 0.0f) {
        return false;
    }

    TextLayoutResult uncached{};
    const TextLayoutResult* measured = ResolveCachedTextLayout(node, textRect, tree, uncached);
    if (!measured) {
        return false;
    }
    if (measured->totalWidth > textRect.width + 0.01f ||
        measured->totalHeight > textRect.height + 0.01f) {
        return false;
    }

    TextAlignment effAlign = txt.align;
    if (measured->baseRtl) {
        if (effAlign == TextAlignment::Left) effAlign = TextAlignment::Right;
        else if (effAlign == TextAlignment::Right) effAlign = TextAlignment::Left;
    }

    const float rectR = rect.x + rect.width;
    const float rectB = rect.y + rect.height;
    for (const PositionedGlyph& pg : measured->glyphs) {
        if (pg.underline || pg.strikethrough) {
            return false;
        }
        if (pg.isWhitespace) {
            continue;
        }
        float alignDx = 0.0f;
        if (pg.lineIndex < measured->lines.size()) {
            const float lw = measured->lines[pg.lineIndex].width;
            if (effAlign == TextAlignment::Center) {
                alignDx = std::max(0.0f, (textRect.width - lw) * 0.5f);
            } else if (effAlign == TextAlignment::Right) {
                alignDx = std::max(0.0f, textRect.width - lw);
            }
        }
        const float italicTopShear = pg.syntheticItalic ? (0.18f * std::max(0.0f, pg.y1 - pg.y0)) : 0.0f;
        const float boldHalf = pg.syntheticBold ? 0.175f : 0.0f;
        const float gx0 = textRect.x + alignDx + pg.x0 - boldHalf;
        const float gx1 = textRect.x + alignDx + pg.x1 + boldHalf + italicTopShear;
        const float gy0 = textRect.y + pg.y0;
        const float gy1 = textRect.y + pg.y1;
        if (gx0 < rect.x || gx1 > rectR || gy0 < rect.y || gy1 > rectB) {
            return false;
        }
    }
    return true;
}

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
            if (has3D) {
                tree.SetNodeWorldMatrix3D(desc, world3D);
            } else {
                desc.worldMatrix3DIndex = 0xFFFFFFFFu;
            }
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

std::size_t VisualRenderAdapter::EmitStyledDashCapsule(
    PagedRenderTree& tree,
    Point a,
    Point b,
    float thickness,
    std::uint8_t startCap,
    std::uint8_t endCap,
    const Color& color,
    std::int32_t strokeGradId,
    const void* owner,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D,
    BlendingMode blend) {
    const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
    if (thickness <= 0.0f || color.a <= 0.001f) {
        return 0u;
    }
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    const float r = thickness * 0.5f;
    const float extS = (startCap == 1u || startCap == 2u) ? r : 0.0f;
    const float extE = (endCap == 1u || endCap == 2u) ? r : 0.0f;
    if (extS <= 0.0f && extE <= 0.0f && len <= 1e-4f) {
        return 0u;
    }
    const float totalLen = std::max(0.5f, len + extS + extE);
    const float rS = (startCap == 2u) ? r : 0.0f;
    const float rE = (endCap == 2u) ? r : 0.0f;
    const float angleDeg = (len > 1e-4f) ? (std::atan2(dy, dx) * kRadToDeg) : 0.0f;
    const Matrix2D segRot = Matrix2D::Rotation(angleDeg).AroundPivot(a.x, a.y);
    const Matrix2D comb2D = Matrix2D::Multiply(world2D, segRot);
    const Matrix4x4 comb3D = has3D
        ? Matrix4x4::Multiply(world3D, Matrix4x4::FromMatrix2D(segRot))
        : Matrix4x4{};

    const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
    const std::uint32_t nid = tree.AllocateRoundedRectNode(
        a.x - extS,
        a.y - r,
        totalLen,
        thickness,
        color,
        CornerRadius{rS, rE, rE, rS},
        owner);
    AttachNodeGradientBrush(tree, nid, strokeGradId);
    const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
    ApplyNodeTransform(tree, beginNid, endNid, comb2D, comb3D, has3D, blend);
    return endNid - beginNid;
}

std::size_t VisualRenderAdapter::EmitDashedPolylinePoints(
    PagedRenderTree& tree,
    const std::vector<Point>& pts,
    const std::vector<float>& rawDashes,
    std::uint8_t dashCap,
    float strokeWidth,
    const Color& strokeColor,
    std::int32_t strokeGradId,
    const void* owner,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D,
    BlendingMode blend) {
    if (pts.size() < 2u || rawDashes.empty() || strokeWidth <= 0.0f) {
        return 0u;
    }
    const float unit = std::max(0.5f, strokeWidth);
    std::vector<float> pattern;
    pattern.reserve(rawDashes.size() * 2u);
    for (float d : rawDashes) {
        pattern.push_back(std::max(0.0f, d) * unit);
    }
    if ((pattern.size() % 2u) == 1u) {
        const std::size_t n = pattern.size();
        for (std::size_t i = 0u; i < n; ++i) {
            pattern.push_back(pattern[i]);
        }
    }
    float period = 0.0f;
    for (float p : pattern) period += p;
    if (period <= 1e-4f) {
        return 0u;
    }

    std::vector<float> cumLen(pts.size(), 0.0f);
    for (std::size_t i = 1u; i < pts.size(); ++i) {
        cumLen[i] = cumLen[i - 1u] + std::hypot(pts[i].x - pts[i - 1u].x, pts[i].y - pts[i - 1u].y);
    }
    const float totalLen = cumLen.back();
    if (totalLen <= 1e-4f) {
        return 0u;
    }

    auto sampleAtArc = [&](float s) -> Point {
        const float cs = std::clamp(s, 0.0f, totalLen);
        auto it = std::lower_bound(cumLen.begin(), cumLen.end(), cs);
        if (it == cumLen.begin()) return pts.front();
        if (it == cumLen.end()) return pts.back();
        const std::size_t idx = static_cast<std::size_t>(it - cumLen.begin());
        const float s0 = cumLen[idx - 1u];
        const float seg = cumLen[idx] - s0;
        const float f = (seg > 1e-6f) ? ((cs - s0) / seg) : 0.0f;
        return Point{
            pts[idx - 1u].x + (pts[idx].x - pts[idx - 1u].x) * f,
            pts[idx - 1u].y + (pts[idx].y - pts[idx - 1u].y) * f};
    };

    std::size_t emitted = 0u;
    float cursor = 0.0f;
    std::size_t patIdx = 0u;
    int guard = 0;
    while (cursor < totalLen - 1e-4f && guard++ < 4096) {
        const bool isDash = (patIdx % 2u) == 0u;
        const float step = pattern[patIdx % pattern.size()];
        const float nextCursor = std::min(totalLen, cursor + step);
        if (isDash) {
            if (nextCursor > cursor + 1e-4f) {
                std::vector<Point> segPts;
                segPts.push_back(sampleAtArc(cursor));
                auto it = std::upper_bound(cumLen.begin(), cumLen.end(), cursor + 1e-4f);
                while (it != cumLen.end() && *it < nextCursor - 1e-4f) {
                    const std::size_t k = static_cast<std::size_t>(it - cumLen.begin());
                    if (k > 0u && k + 1u < pts.size()) {
                        segPts.push_back(pts[k]);
                    }
                    ++it;
                }
                segPts.push_back(sampleAtArc(nextCursor));
                for (std::size_t j = 1u; j < segPts.size(); ++j) {
                    const std::uint8_t sCap = (j == 1u) ? dashCap : 2u;
                    const std::uint8_t eCap = (j + 1u == segPts.size()) ? dashCap : 2u;
                    emitted += EmitStyledDashCapsule(
                        tree, segPts[j - 1u], segPts[j], strokeWidth, sCap, eCap,
                        strokeColor, strokeGradId, owner, world2D, world3D, has3D, blend);
                }
            } else if (dashCap == 1u || dashCap == 2u) {
                const Point p0 = sampleAtArc(cursor);
                emitted += EmitStyledDashCapsule(
                    tree, p0, p0, strokeWidth, dashCap, dashCap, strokeColor, strokeGradId,
                    owner, world2D, world3D, has3D, blend);
            }
        }
        cursor += step;
        ++patIdx;
    }
    return emitted;
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
        float strokeOpacity,
        const VisualVectorData* cachedVec) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        if (pathData.empty() ||
            (stretch != Stretch::None && (destBox.width <= 0.0f || destBox.height <= 0.0f))) {
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
        const float effTrimLo = std::clamp(std::min(trimStart, trimEnd), 0.0f, 1.0f);
        const float effTrimHi = std::clamp(std::max(trimStart, trimEnd), 0.0f, 1.0f);
        const bool trimCollapsed = (effTrimHi - effTrimLo <= 0.0005f);
        if (trimCollapsed && !hasFill) {
            return 0u;
        }
        const bool trimmed = trimCollapsed || (effTrimLo > 0.001f) || (effTrimHi < 0.999f);

        const std::vector<std::vector<Point>> parsedSubpaths =
            (hasStroke && trimmed && !trimCollapsed) ? ParseSvgPathToSubpaths(pathData) : std::vector<std::vector<Point>>{};
        Rect rawBounds{};
        std::uint64_t hash = 0u;
        if (cachedVec != nullptr && cachedVec->shapeKind == VectorShapeKind::SvgPath &&
            cachedVec->pathOrParams == pathData) {
            if (!cachedVec->svgBoundsCached) {
                cachedVec->cachedPathHash = ComputeSvgPathHash(pathData);
                cachedVec->cachedSvgBounds = MeasureSvgPathBounds(pathData);
                cachedVec->svgBoundsCached = true;
            }
            rawBounds = cachedVec->cachedSvgBounds;
            hash = cachedVec->cachedPathHash;
        } else {
            rawBounds = MeasureSvgPathBounds(pathData);
            hash = ComputeSvgPathHash(pathData);
        }
        Rect strokeAdjustedDest = destBox;
        if (stretch != Stretch::None && hasStroke && strokeWidth > 0.0f) {
            const float halfStroke = 0.5f * strokeWidth;
            if (strokeAdjustedDest.width > strokeWidth) {
                strokeAdjustedDest.x += halfStroke;
                strokeAdjustedDest.width -= strokeWidth;
            }
            if (strokeAdjustedDest.height > strokeWidth) {
                strokeAdjustedDest.y += halfStroke;
                strokeAdjustedDest.height -= strokeWidth;
            }
        }
        Rect fitted = strokeAdjustedDest;
        if (rawBounds.width > 0.0f && rawBounds.height > 0.0f) {
            if (stretch == Stretch::None) {
                fitted = Rect{
                    destBox.x + rawBounds.x,
                    destBox.y + rawBounds.y,
                    rawBounds.width,
                    rawBounds.height};
            } else {
                fitted = ComputeStretchedBox(strokeAdjustedDest, rawBounds.width, rawBounds.height, stretch);
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

        if (hasFill) {
            const Color fillColor = (fillGradId > 0)
                ? Color{1.0f, 1.0f, 1.0f, effFillOp}
                : UnpackRgba32(fillRgba, effFillOp);

            RasterizedPathMaskEntry maskEntry{};
            if (store->GetOrRasterizePathMask(hash, pathData, rawBounds, innerW, innerH, 1u, maskEntry) &&
                maskEntry.texWidth > 0u) {
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

        const VisualNode* ownerNode = static_cast<const VisualNode*>(owner);
        const bool hasDash = (ownerNode != nullptr) && (ownerNode->kind == VisualKind::Vector) &&
                             !ownerNode->Vector().dashArray.empty();
        if (hasStroke && !trimCollapsed) {
            const Color strokeColor = (strokeGradId > 0)
                ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                : UnpackRgba32(strokeRgba, effStrokeOp);
            if (!trimmed && !hasDash) {
                const bool isOwnerVec = (ownerNode != nullptr) && (ownerNode->kind == VisualKind::Vector);
                const std::uint8_t sLineCap = isOwnerVec ? ownerNode->Vector().startLineCap : 2u;
                const std::uint8_t eLineCap = isOwnerVec ? ownerNode->Vector().endLineCap : 2u;
                const float rawStrokeScale = 0.5f * (sx + sy);
                float strokeSs = (std::max(fitted.width * sx, fitted.height * sy) <= 96.0f) ? 4.0f : 1.0f;
                const float rawStrokePx = strokeWidth * rawStrokeScale;
                if (strokeSs > 1.0f && rawStrokePx * strokeSs > 240.0f) {
                    strokeSs = std::max(1.0f, 240.0f / std::max(1e-3f, rawStrokePx));
                }
                const float effSx = sx * strokeSs;
                const float effSy = sy * strokeSs;
                const std::uint32_t strokeInnerW = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(fitted.width * effSx)), 1u, 2048u);
                const std::uint32_t strokeInnerH = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::lround(fitted.height * effSy)), 1u, 2048u);
                const float strokeWidthPx = strokeWidth * 0.5f * (effSx + effSy);
                const std::uint32_t padPx = std::clamp<std::uint32_t>(
                    static_cast<std::uint32_t>(std::ceil(strokeWidthPx + 2.0f * strokeSs)), 2u, 256u);
                RasterizedPathMaskEntry strokeMask{};
                if (store->GetOrRasterizePathStrokeMask(
                        hash, pathData, rawBounds, strokeInnerW, strokeInnerH, strokeWidthPx, padPx, strokeMask, sLineCap, eLineCap) &&
                    strokeMask.texWidth > 0u) {
                    const float worldPadX = static_cast<float>(padPx) / effSx;
                    const float worldPadY = static_cast<float>(padPx) / effSy;
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

            const std::vector<std::vector<Point>> fallbackSubpaths =
                (!trimmed) ? ParseSvgPathToSubpaths(pathData) : std::vector<std::vector<Point>>{};
            const auto& subpaths = trimmed ? parsedSubpaths : fallbackSubpaths;
            if (!subpaths.empty() && rawBounds.width > 1e-5f && rawBounds.height > 1e-5f) {
                const float scaleX = fitted.width / rawBounds.width;
                const float scaleY = fitted.height / rawBounds.height;
                const bool isVec = (ownerNode != nullptr) && (ownerNode->kind == VisualKind::Vector);
                const std::uint8_t pathStartCap = isVec ? ownerNode->Vector().startLineCap : 2u;
                const std::uint8_t pathEndCap = isVec ? ownerNode->Vector().endLineCap : 2u;
                for (const auto& sub : subpaths) {
                    if (sub.size() < 2u) {
                        continue;
                    }
                    std::vector<Point> mapped;
                    mapped.reserve(sub.size());
                    for (const Point& pt : sub) {
                        mapped.push_back(Point{
                            fitted.x + (pt.x - rawBounds.x) * scaleX,
                            fitted.y + (pt.y - rawBounds.y) * scaleY});
                    }
                    std::vector<Point> finalPts = trimmed
                        ? TrimPolylineRange(mapped, effTrimLo, effTrimHi)
                        : mapped;
                    if (hasDash) {
                        count += EmitDashedPolylinePoints(
                            tree, finalPts, ownerNode->Vector().dashArray, ownerNode->Vector().dashCap,
                            strokeWidth, strokeColor, strokeGradId, owner, world2D, world3D, has3D, blend);
                    } else if (finalPts.size() >= 2u) {
                        const float minEndSeg = 0.2f * strokeWidth;
                        auto nearlyCollinear = [](Point p0, Point p1, Point p2) noexcept -> bool {
                            const float ax = p1.x - p0.x, ay = p1.y - p0.y;
                            const float bx = p2.x - p1.x, by = p2.y - p1.y;
                            const float la = std::hypot(ax, ay), lb = std::hypot(bx, by);
                            if (la <= 1e-5f || lb <= 1e-5f) return true;
                            return ((ax * bx + ay * by) / (la * lb)) > 0.98f;
                        };
                        while (pathStartCap == 0u && finalPts.size() >= 3u &&
                               std::hypot(finalPts[1u].x - finalPts[0u].x, finalPts[1u].y - finalPts[0u].y) < minEndSeg &&
                               nearlyCollinear(finalPts[0u], finalPts[1u], finalPts[2u])) {
                            finalPts.erase(finalPts.begin() + 1);
                        }
                        while (pathEndCap == 0u && finalPts.size() >= 3u &&
                               std::hypot(finalPts.back().x - finalPts[finalPts.size() - 2u].x,
                                          finalPts.back().y - finalPts[finalPts.size() - 2u].y) < minEndSeg &&
                               nearlyCollinear(finalPts[finalPts.size() - 3u], finalPts[finalPts.size() - 2u], finalPts.back())) {
                            finalPts.erase(finalPts.end() - 2);
                        }
                        std::vector<float> segCum(finalPts.size(), 0.0f);
                        for (std::size_t i = 1u; i < finalPts.size(); ++i) {
                            segCum[i] = segCum[i - 1u] +
                                std::hypot(finalPts[i].x - finalPts[i - 1u].x, finalPts[i].y - finalPts[i - 1u].y);
                        }
                        const float polyLen = segCum.back();
                        const float halfStrokeW = 0.5f * strokeWidth;
                        auto unitDir = [](Point a, Point b) noexcept -> Point {
                            const float dx = b.x - a.x;
                            const float dy = b.y - a.y;
                            const float l = std::hypot(dx, dy);
                            return (l > 1e-5f) ? Point{dx / l, dy / l} : Point{0.0f, 0.0f};
                        };
                        const Point startTan = unitDir(finalPts[0u], finalPts[1u]);
                        const Point endTan = unitDir(finalPts[finalPts.size() - 2u], finalPts.back());
                        for (std::size_t i = 1u; i < finalPts.size(); ++i) {
                            const Point segTan = unitDir(finalPts[i - 1u], finalPts[i]);
                            std::uint8_t sCap = 2u;
                            if (i == 1u) {
                                sCap = pathStartCap;
                            } else if (pathStartCap == 0u && segCum[i - 1u] < halfStrokeW &&
                                       (startTan.x * segTan.x + startTan.y * segTan.y) > 0.9f) {
                                sCap = 0u;
                            }
                            std::uint8_t eCap = 2u;
                            if (i + 1u == finalPts.size()) {
                                eCap = pathEndCap;
                            } else if (pathEndCap == 0u && (polyLen - segCum[i]) < halfStrokeW &&
                                       (endTan.x * segTan.x + endTan.y * segTan.y) > 0.9f) {
                                eCap = 0u;
                            }
                            count += EmitStyledDashCapsule(
                                tree, finalPts[i - 1u], finalPts[i], strokeWidth,
                                sCap, eCap, strokeColor, strokeGradId,
                                owner, world2D, world3D, has3D, blend);
                        }
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
        BlendingMode blend,
        const VisualWorld* optWorld,
        const DrawingCommandBuffer* optDrawBuffer) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        std::size_t count = 0u;
        const bool hasBorderThickness = HasPositiveThickness(node.box.borderThickness);
        Color borderColor = UnpackRgba32(node.box.borderRgba, effOpacity);
        const GradientBrushDesc* borderGb = nullptr;
        if (hasBorderThickness && node.box.borderGradId > 0) {
            UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
            borderGb = store->GradientBrushes().Find(
                static_cast<std::uint32_t>(node.box.borderGradId));
            if (!borderGb) {
                borderGb = GradientBrushRegistry::Instance().Find(
                    static_cast<std::uint32_t>(node.box.borderGradId));
            }
            if (borderGb && !borderGb->stops.empty()) {
                Color sampled = SampleGradientRampAtT(*borderGb, 0.5f);
                if (sampled.a <= 0.001f) {
                    for (const GradientStop& st : borderGb->stops) {
                        if (st.color.a > sampled.a) {
                            sampled = st.color;
                        }
                    }
                }
                sampled.a *= borderGb->opacity * effOpacity;
                if (sampled.a > 0.0f) {
                    borderColor = sampled;
                }
            }
        }
        const bool hasGradientBorder = hasBorderThickness && borderGb != nullptr && !borderGb->stops.empty() &&
                                       (borderGb->opacity * effOpacity > 0.0f);
        const bool hasVisibleBorder = hasBorderThickness && (hasGradientBorder || borderColor.a > 0.0f);

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
        if (node.kind == VisualKind::Box &&
            (!boxImg.uri.empty() || boxImg.visualBrushSource.IsValid()) &&
            HasAlpha(boxImg.tintRgba) &&
            bgRect.width > 0.0f && bgRect.height > 0.0f) {
            count += EmitImageVisual(tree, node, bgRect, effOpacity, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
        }

        if (hasVisibleBorder && rect.width > 0.0f && rect.height > 0.0f) {
            const float btL = std::max(0.0f, node.box.borderThickness.left);
            const float btT = std::max(0.0f, node.box.borderThickness.top);
            const float btR = std::max(0.0f, node.box.borderThickness.right);
            const float btB = std::max(0.0f, node.box.borderThickness.bottom);
            const bool hasInnerHole = (rect.width > btL + btR + 1e-3f) && (rect.height > btT + btB + 1e-3f);
            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
            if (hasGradientBorder && !hasInnerHole) {
                Matrix2D borderWorld2D = world2D;
                CornerRadius borderRadii = node.box.cornerRadius;
                if (node.layout.effectiveRtl) {
                    const float cx2 = 2.0f * rect.x + rect.width;
                    borderWorld2D.tx += world2D.m00 * cx2;
                    borderWorld2D.ty += world2D.m10 * cx2;
                    borderWorld2D.m00 = -world2D.m00;
                    borderWorld2D.m10 = -world2D.m10;
                    std::swap(borderRadii.topLeft, borderRadii.topRight);
                    std::swap(borderRadii.bottomLeft, borderRadii.bottomRight);
                }
                const Color tint{1.0f, 1.0f, 1.0f, std::clamp(effOpacity, 0.0f, 1.0f)};
                (void)tree.AllocateGradientRoundedRectNode(
                    rect.x,
                    rect.y,
                    rect.width,
                    rect.height,
                    static_cast<std::uint32_t>(node.box.borderGradId),
                    borderRadii,
                    false,
                    tint,
                    1.0f,
                    &node);
                const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                ApplyNodeTransform(tree, beginNid, endNid, borderWorld2D, world3D, has3D, blend);
                count += (endNid - beginNid);
            } else {
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
        TextLayoutResult uncachedLayout{};
        const TextLayoutResult* measuredPtr = ResolveCachedTextLayout(node, textRect, tree, uncachedLayout);
        const TextLayoutResult& measured = *measuredPtr;

        std::size_t count = 0u;
        struct ScopedTreeHintBounds {
            PagedRenderTree& tree;
            bool prevHas;
            Rect prevBounds;
            ScopedTreeHintBounds(PagedRenderTree& t) noexcept
                : tree(t), prevHas(t.hasHintNextBounds_), prevBounds(t.hintNextBounds_) {}
            ~ScopedTreeHintBounds() noexcept {
                tree.hasHintNextBounds_ = prevHas;
                tree.hintNextBounds_ = prevBounds;
            }
        } hintGuard(tree);
        if (!has3D && tree.activeBlurSpanDepth_ == 0u && !forceSolidShadowColor) {
            const float fontPad = std::max(4.0f, txt.fontSize * 0.5f);
            const float covW = std::max(textRect.width, measured.totalWidth);
            const float covH = std::max(textRect.height, measured.totalHeight);
            const float lx0 = std::min(rect.x - 2.0f, textRect.x - fontPad);
            const float ly0 = std::min(rect.y - 2.0f, textRect.y - fontPad);
            const float lx1 = std::max(rect.x + rect.width + 2.0f, textRect.x + covW + fontPad);
            const float ly1 = std::max(rect.y + rect.height + 2.0f, textRect.y + covH + fontPad);
            tree.hintNextBounds_ = TransformRectViewportBounds(
                Rect{lx0, ly0, std::max(0.1f, lx1 - lx0), std::max(0.1f, ly1 - ly0)},
                world2D,
                world3D,
                false);
            tree.hasHintNextBounds_ = true;
        } else {
            tree.hasHintNextBounds_ = false;
        }
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
                TextLayoutResult justifiedDeco{};
                const TextLayoutResult* decoLayoutPtr = &measured;
                const bool useJustify = (txt.align == TextAlignment::Justify && textRect.width > 0.0f);
                if (useJustify) {
                    justifiedDeco = measured;
                    ApplyTextAlignmentToLayout(justifiedDeco, txt.align, textRect.width);
                    decoLayoutPtr = &justifiedDeco;
                }
                const TextLayoutResult& decoLayout = *decoLayoutPtr;
                const float strokeH = std::max(1.0f, txt.fontSize * 0.07f);
                const bool nodeU = (txt.decorations & 1) != 0;
                const bool nodeS = (txt.decorations & 2) != 0;
                for (const TextLineMetrics& lm : decoLayout.lines) {
                    const float decoDx = useJustify ? 0.0f : lineAlignDx(lm);
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
                                    textRect.x + decoDx + xStart,
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
                for (std::size_t li = 0u; li < measured.lines.size(); ++li) {
                    const TextLineMetrics& line = measured.lines[li];
                    const float alignDx = lineAlignDx(line);
                    const std::size_t gEnd = std::min(line.firstGlyphIndex + line.glyphCount, measured.glyphs.size());
                    if (line.glyphCount == 0u) {
                        const bool isLastLine = (li + 1u == measured.lines.size());
                        const std::uint32_t nextFirstCp = !isLastLine
                            ? measured.lines[li + 1u].firstCodePointIndex
                            : 0xFFFFFFFFu;
                        if (targetCp >= line.firstCodePointIndex && targetCp < nextFirstCp) {
                            caretX = textRect.x + alignDx;
                            caretY = textRect.y + line.topY;
                            caretH = std::max(1.0f, line.lineHeight);
                            placed = true;
                            break;
                        }
                        continue;
                    }
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
                    if (line.endedByNewline && gEnd > line.firstGlyphIndex) {
                        const PositionedGlyph& endG = measured.glyphs[gEnd - 1u];
                        if (targetCp == endG.sourceCodePointIndex + 1u) {
                            caretX = textRect.x + alignDx + endG.penX + endG.advanceX;
                            caretY = textRect.y + line.topY;
                            caretH = std::max(1.0f, line.lineHeight);
                            placed = true;
                            break;
                        }
                    }
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

bool VisualRenderAdapter::RasterizeVisualBrushToEntry(
    UIResourceStore* store,
    const VisualWorld* optWorld,
    const DrawingCommandBuffer* optDrawBuffer,
    VisualNodeId sourceNodeId,
    ImageAtlasEntry& outEntry) {
    if (!store || !optWorld || !sourceNodeId.IsValid()) {
        return false;
    }
    struct VbDepthGuard {
        int& depth;
        explicit VbDepthGuard(int& d) noexcept : depth(d) { ++depth; }
        ~VbDepthGuard() noexcept { --depth; }
    };
    thread_local int sVisualBrushDepth = 0;
    if (sVisualBrushDepth >= 2) {
        return false;
    }
    VbDepthGuard depthGuard(sVisualBrushDepth);

    const VisualNode* srcNode = optWorld->GetNode(sourceNodeId);
    if (!srcNode || srcNode->layout.layoutRect.width <= 0.5f || srcNode->layout.layoutRect.height <= 0.5f) {
        return false;
    }

    const Rect& srcRect = srcNode->layout.layoutRect;
    const float maxDim = std::max(srcRect.width, srcRect.height);
    const float ss = std::clamp(512.0f / std::max(1.0f, maxDim), 1.0f, 2.5f);
    const std::uint32_t texW = std::clamp<std::uint32_t>(
        static_cast<std::uint32_t>(std::lround(srcRect.width * ss)), 2u, 512u);
    const std::uint32_t texH = std::clamp<std::uint32_t>(
        static_cast<std::uint32_t>(std::lround(srcRect.height * ss)), 2u, 512u);

    PagedRenderTree vbTree;
    vbTree.SetResourceStore(store);
    vbTree.SetRootDpiScale(1.0f);
    EmitNodeRecursive(nullptr, *optWorld, optDrawBuffer, sourceNodeId, 1.0f, vbTree);
    if (vbTree.NodeCount() == 0u) {
        return false;
    }

    Matrix2D invSrc{};
    if (!srcNode->layout.worldTransform.TryInvert(invSrc)) {
        invSrc = Matrix2D::Identity();
    }
    const float sxTex = static_cast<float>(texW) / srcRect.width;
    const float syTex = static_cast<float>(texH) / srcRect.height;
    const Matrix2D toTex = Matrix2D::Multiply(
        Matrix2D{sxTex, 0.0f, 0.0f, syTex, 0.0f, 0.0f},
        Matrix2D::Multiply(Matrix2D::Translation(-srcRect.x, -srcRect.y), invSrc));
    Matrix2D invToTex{};
    if (!toTex.TryInvert(invToTex)) {
        invToTex = Matrix2D::Identity();
    }
    const Matrix4x4 toTex3D = Matrix4x4::FromMatrix2D(toTex);
    const float toTexScale = std::max(
        0.25f,
        0.5f * (std::hypot(toTex.m00, toTex.m01) + std::hypot(toTex.m10, toTex.m11)));

    for (auto& nd : vbTree.nodes_) {
        const Matrix2D origM{nd.m00, nd.m01, nd.m10, nd.m11, nd.baseTx, nd.baseTy};
        const Matrix2D remapped = Matrix2D::Multiply(toTex, origM);
        nd.m00 = remapped.m00;
        nd.m01 = remapped.m01;
        nd.m10 = remapped.m10;
        nd.m11 = remapped.m11;
        nd.baseTx = remapped.tx;
        nd.baseTy = remapped.ty;
        nd.tx = remapped.tx + nd.x * (remapped.m00 - 1.0f) + nd.y * remapped.m01;
        nd.ty = remapped.ty + nd.x * remapped.m10 + nd.y * (remapped.m11 - 1.0f);
        if (nd.hasPerspective3D) {
            vbTree.SetNodeWorldMatrix3D(
                nd,
                Matrix4x4::Multiply(toTex3D, vbTree.GetNodeWorldMatrix3D(nd)));
        }
        const bool isSingleQuadPrimitive =
            (nd.vertexCount == 4u) &&
            (nd.primType != AeroPrimType::SdfTextGlyph) &&
            (nd.primType != AeroPrimType::DynamicMesh);
        if (isSingleQuadPrimitive) {
            vbTree.WriteQuadVertices(nd);
        } else if (nd.pageIndex < vbTree.PageCount()) {
            // Preserve multi-quad / glyph-run / dynamic-mesh vertices instead of overwriting the first quad.
            CommandPage& pg = const_cast<CommandPage&>(vbTree.GetPage(nd.pageIndex));
            const std::size_t vStart = nd.vertexOffsetInPage;
            const std::size_t vEnd = std::min<std::size_t>(
                vStart + nd.vertexCount,
                kVerticesPerCommandPage);
            for (std::size_t vi = vStart; vi < vEnd; ++vi) {
                const Point tp = toTex.TransformPoint(Point{pg.vertices[vi].x, pg.vertices[vi].y});
                pg.vertices[vi].x = tp.x;
                pg.vertices[vi].y = tp.y;
            }
        }
    }

    for (std::size_t pi = 0u; pi < vbTree.PageCount(); ++pi) {
        CommandPage& pg = const_cast<CommandPage&>(vbTree.GetPage(pi));
        if ((pg.header.flags & CommandPageFlag_ScissorClipActive) != 0u) {
            const Rect cr = toTex.TransformRectBounds(
                Rect{pg.header.clipX, pg.header.clipY, pg.header.clipW, pg.header.clipH});
            pg.header.clipX = cr.x;
            pg.header.clipY = cr.y;
            pg.header.clipW = cr.width;
            pg.header.clipH = cr.height;
        }
    }

    for (BlurLayerSpan& span : vbTree.blurLayerSpans_) {
        if (span.clipActive) {
            span.clipRect = toTex.TransformRectBounds(span.clipRect);
        }
        if (span.maskDestRect.width > 0.0f && span.maskDestRect.height > 0.0f) {
            span.maskDestRect = toTex.TransformRectBounds(span.maskDestRect);
        }
        span.worldScale *= toTexScale;
        span.opacityMaskInvWorld2D = Matrix2D::Multiply(span.opacityMaskInvWorld2D, invToTex);
    }

    std::vector<std::uint8_t> vbPixels;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(
        vbTree, texW, texH, vbPixels, Color{0.0f, 0.0f, 0.0f, 0.0f});
    const std::size_t nPx = static_cast<std::size_t>(texW) * texH;
    if (vbPixels.size() < nPx * 4u) {
        return false;
    }
    for (std::size_t p = 0u; p < nPx; ++p) {
        std::uint8_t* px = vbPixels.data() + p * 4u;
        const std::uint8_t a = px[3];
        if (a > 0u && a < 255u) {
            const float invA = 255.0f / static_cast<float>(a);
            px[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(px[0]) * invA + 0.5f, 0.0f, 255.0f));
            px[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(px[1]) * invA + 0.5f, 0.0f, 255.0f));
            px[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(px[2]) * invA + 0.5f, 0.0f, 255.0f));
        }
    }

    std::uint64_t vbCacheKey = 14695981039346656037ULL;
    auto mixU64 = [&](std::uint64_t v) noexcept {
        vbCacheKey ^= v;
        vbCacheKey *= 1099511628211ULL;
    };
    mixU64(optWorld->WorldInstanceId());
    mixU64((static_cast<std::uint64_t>(sourceNodeId.generation) << 32u) |
           static_cast<std::uint64_t>(sourceNodeId.index));
    mixU64((static_cast<std::uint64_t>(texW) << 32u) | static_cast<std::uint64_t>(texH));
    return store->Images().RegisterOrUpdateVisualBrushSlot(
        vbCacheKey, texW, texH, std::move(vbPixels), outEntry);
}

std::size_t VisualRenderAdapter::EmitImageVisual(
    PagedRenderTree& tree,
    const VisualNode& node,
    const Rect& rect,
    float effOpacity,
    const Matrix2D& world2D,
    const Matrix4x4& world3D,
    bool has3D,
    BlendingMode blend,
    const VisualWorld* optWorld,
    const DrawingCommandBuffer* optDrawBuffer) {
    const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
    const VisualImageData& img = node.Image();
    if ((img.uri.empty() && !img.visualBrushSource.IsValid()) || !HasAlpha(img.tintRgba) ||
        rect.width <= 0.0f || rect.height <= 0.0f) {
        return 0u;
    }
    UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
    ImageAtlasEntry vbTempEntry{};
    const ImageAtlasEntry* entry = nullptr;
    if (img.visualBrushSource.IsValid() && optWorld != nullptr) {
        if (RasterizeVisualBrushToEntry(store, optWorld, optDrawBuffer, img.visualBrushSource, vbTempEntry)) {
            entry = &vbTempEntry;
        }
    }
    if (!entry && !img.uri.empty()) {
        entry = store->Images().LoadOrGet(img.uri);
    }
    if (!entry || entry->width == 0u || entry->height == 0u) {
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

    Rect cellRect = rect;
    if (img.hasExplicitViewport) {
        if (img.viewportAbsolute) {
            cellRect = Rect{
                rect.x + img.viewportX,
                rect.y + img.viewportY,
                std::max(1e-4f, img.viewportW),
                std::max(1e-4f, img.viewportH)};
        } else {
            cellRect = Rect{
                rect.x + img.viewportX * rect.width,
                rect.y + img.viewportY * rect.height,
                std::max(1e-4f, img.viewportW * rect.width),
                std::max(1e-4f, img.viewportH * rect.height)};
        }
    }

    float sx = 1.0f;
    float sy = 1.0f;
    switch (img.stretch) {
        case Stretch::None:
            sx = 1.0f;
            sy = 1.0f;
            break;
        case Stretch::Fill:
            sx = cellRect.width / vbW;
            sy = cellRect.height / vbH;
            break;
        case Stretch::Uniform: {
            const float s = std::min(cellRect.width / vbW, cellRect.height / vbH);
            sx = s;
            sy = s;
            break;
        }
        case Stretch::UniformToFill: {
            const float s = std::max(cellRect.width / vbW, cellRect.height / vbH);
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

    // Map the Viewbox rectangle into cellRect, then clip/tile into rect
    const float placedVbW = vbW * sx;
    const float placedVbH = vbH * sy;
    const float alignFx = (img.alignX == 0u) ? 0.0f : ((img.alignX == 2u) ? 1.0f : 0.5f);
    const float alignFy = (img.alignY == 0u) ? 0.0f : ((img.alignY == 2u) ? 1.0f : 0.5f);
    const float placedVbX = cellRect.x + (cellRect.width - placedVbW) * alignFx;
    const float placedVbY = cellRect.y + (cellRect.height - placedVbH) * alignFy;
    const float imgX0 = placedVbX - (vx * imgW) * sx;
    const float imgY0 = placedVbY - (vy * imgH) * sy;
    const float fullImgW = std::max(1e-4f, imgW * sx);
    const float fullImgH = std::max(1e-4f, imgH * sy);

    const bool hasCustomViewbox = (vx > 1e-4f || vy > 1e-4f || vw < 0.9999f || vh < 0.9999f) &&
                                  img.visualBrushSource.IsValid();
    const float clipBaseX0 = hasCustomViewbox ? std::max(rect.x, placedVbX) : rect.x;
    const float clipBaseY0 = hasCustomViewbox ? std::max(rect.y, placedVbY) : rect.y;
    const float clipBaseX1 = hasCustomViewbox ? std::min(rect.x + rect.width, placedVbX + placedVbW) : (rect.x + rect.width);
    const float clipBaseY1 = hasCustomViewbox ? std::min(rect.y + rect.height, placedVbY + placedVbH) : (rect.y + rect.height);

    const float ix0 = std::max(clipBaseX0, imgX0);
    const float iy0 = std::max(clipBaseY0, imgY0);
    const float ix1 = std::min(clipBaseX1, imgX0 + fullImgW);
    const float iy1 = std::min(clipBaseY1, imgY0 + fullImgH);
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
    const float effTileW = img.hasExplicitViewport ? cellRect.width : placedVbW;
    const float effTileH = img.hasExplicitViewport ? cellRect.height : placedVbH;
    const float origX = img.hasExplicitViewport ? cellRect.x : placedVbX;
    const float origY = img.hasExplicitViewport ? cellRect.y : placedVbY;
    const bool needsTileRepeat = img.tileWrap && effTileW >= 1.0f && effTileH >= 1.0f &&
        (effTileW < rect.width - 0.5f || effTileH < rect.height - 0.5f ||
         origX > rect.x + 0.5f || origY > rect.y + 0.5f ||
         origX + effTileW < rect.x + rect.width - 0.5f ||
         origY + effTileH < rect.y + rect.height - 0.5f);
    if (needsTileRepeat) {
        const float tileW = effTileW;
        const float tileH = effTileH;
        const int cStart = static_cast<int>(std::floor((rect.x - origX) / tileW));
        const int cEnd = static_cast<int>(std::ceil((rect.x + rect.width - origX) / tileW));
        const int rStart = static_cast<int>(std::floor((rect.y - origY) / tileH));
        const int rEnd = static_cast<int>(std::ceil((rect.y + rect.height - origY) / tileH));
        const int cols = std::max(0, cEnd - cStart);
        const int rows = std::max(0, rEnd - rStart);
        if (cols > 0 && rows > 0 && cols * rows <= 256) {
            const float subOffX = placedVbX - origX;
            const float subOffY = placedVbY - origY;
            const float safeVbW = std::max(1e-4f, placedVbW);
            const float safeVbH = std::max(1e-4f, placedVbH);
            for (int r = rStart; r < rEnd; ++r) {
                const float cellY0 = origY + static_cast<float>(r) * tileH;
                const float subY0 = cellY0 + subOffY;
                const float drawY0 = std::max(rect.y, std::max(cellY0, subY0));
                const float drawY1 = std::min(
                    rect.y + rect.height,
                    std::min(cellY0 + tileH, subY0 + placedVbH));
                if (drawY1 <= drawY0) continue;
                const float tv0 = vy + ((drawY0 - subY0) / safeVbH) * vh;
                const float tv1 = vy + ((drawY1 - subY0) / safeVbH) * vh;
                for (int c = cStart; c < cEnd; ++c) {
                    const float cellX0 = origX + static_cast<float>(c) * tileW;
                    const float subX0 = cellX0 + subOffX;
                    const float drawX0 = std::max(rect.x, std::max(cellX0, subX0));
                    const float drawX1 = std::min(
                        rect.x + rect.width,
                        std::min(cellX0 + tileW, subX0 + placedVbW));
                    if (drawX1 <= drawX0) continue;
                    const float tu0 = vx + ((drawX0 - subX0) / safeVbW) * vw;
                    const float tu1 = vx + ((drawX1 - subX0) / safeVbW) * vw;
                    const float fu0 = entry->u0 + (flipRtlImg ? tu1 : tu0) * spanU;
                    const float fu1 = entry->u0 + (flipRtlImg ? tu0 : tu1) * spanU;
                    (void)tree.AllocateTexturedNode(
                        drawX0,
                        drawY0,
                        drawX1 - drawX0,
                        drawY1 - drawY0,
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
        BlendingMode blend,
        const VisualWorld* optWorld,
        const DrawingCommandBuffer* optDrawBuffer) {
        const ScopedBlendMode blendGuard(tree.activeBlendMode_, blend);
        std::size_t count = 0u;
        const VisualVectorData& vec = node.Vector();
        const VisualImageData& fillImg = node.Image();
        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
        auto resolveFillEntry = [&](ImageAtlasEntry& outEntry) -> bool {
            if (fillImg.visualBrushSource.IsValid() && optWorld != nullptr) {
                return RasterizeVisualBrushToEntry(
                    store, optWorld, optDrawBuffer, fillImg.visualBrushSource, outEntry);
            }
            if (!fillImg.uri.empty()) {
                if (const ImageAtlasEntry* loaded = store->Images().LoadOrGet(fillImg.uri)) {
                    outEntry = *loaded;
                    return (outEntry.width > 0u && outEntry.height > 0u);
                }
            }
            return false;
        };
        auto computeFillCellRect = [&]() -> Rect {
            if (!fillImg.hasExplicitViewport) {
                return rect;
            }
            return fillImg.viewportAbsolute
                ? Rect{rect.x + fillImg.viewportX, rect.y + fillImg.viewportY,
                       std::max(1e-4f, fillImg.viewportW), std::max(1e-4f, fillImg.viewportH)}
                : Rect{rect.x + fillImg.viewportX * rect.width, rect.y + fillImg.viewportY * rect.height,
                       std::max(1e-4f, fillImg.viewportW * rect.width), std::max(1e-4f, fillImg.viewportH * rect.height)};
        };

        switch (vec.shapeKind) {
            case VectorShapeKind::SvgPath: {
                const float effFillOp = std::clamp(effOpacity * vec.fillOpacity, 0.0f, 1.0f);
                const bool hasImageFill = (!fillImg.uri.empty() || fillImg.visualBrushSource.IsValid()) &&
                                          effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                if (hasImageFill) {
                    ImageAtlasEntry fillEntry{};
                    if (!vec.pathOrParams.empty() && resolveFillEntry(fillEntry)) {
                        if (!vec.svgBoundsCached) {
                            vec.cachedPathHash = ComputeSvgPathHash(vec.pathOrParams);
                            vec.cachedSvgBounds = MeasureSvgPathBounds(vec.pathOrParams);
                            vec.svgBoundsCached = true;
                        }
                        const Rect rawBounds = vec.cachedSvgBounds;
                        Rect fitted = rect;
                        if (rawBounds.width > 0.0f && rawBounds.height > 0.0f) {
                            fitted = (vec.stretch == Stretch::None)
                                ? Rect{rect.x + rawBounds.x, rect.y + rawBounds.y, rawBounds.width, rawBounds.height}
                                : ComputeStretchedBox(rect, rawBounds.width, rawBounds.height, vec.stretch);
                        }
                        const Rect imgCellRect = computeFillCellRect();
                        const float dpi = std::max(0.25f, tree.RootDpiScale());
                        const float sx = std::max(0.25f, std::hypot(world2D.m00, world2D.m01) * dpi);
                        const float sy = std::max(0.25f, std::hypot(world2D.m10, world2D.m11) * dpi);
                        const std::uint32_t innerW = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(fitted.width * sx)), 1u, 2048u);
                        const std::uint32_t innerH = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(fitted.height * sy)), 1u, 2048u);
                        RasterizedPathMaskEntry maskedEntry{};
                        if (CompositeMaskedPathTexture(
                                *store, store->Images(),
                                vec.cachedPathHash, vec.pathOrParams, rawBounds,
                                imgCellRect, fitted, innerW, innerH, 1u,
                                fillEntry, fillImg.stretch, fillImg.tintRgba, fillImg.tileWrap, maskedEntry) &&
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
                            count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
                        }
                    } else {
                        count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
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
                    vec.strokeOpacity,
                    &vec);
                break;
            }

            case VectorShapeKind::Ellipse: {
                const float effFillOp = std::clamp(effOpacity * vec.fillOpacity, 0.0f, 1.0f);
                const float effStrokeOp = std::clamp(effOpacity * vec.strokeOpacity, 0.0f, 1.0f);
                const bool hasImageFill = (!fillImg.uri.empty() || fillImg.visualBrushSource.IsValid()) &&
                                          effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                const bool hasFill = (HasAlpha(vec.fillRgba) || vec.fillGradId > 0) && effFillOp > 0.001f;
                if (hasImageFill) {
                    static constexpr std::string_view kUnitCirclePath =
                        "M 0.5,0 A 0.5,0.5 0 1 1 0.5,1 A 0.5,0.5 0 1 1 0.5,0 Z";
                    static const std::uint64_t kUnitCircleHash = ComputeSvgPathHash(kUnitCirclePath);
                    static constexpr Rect kUnitCircleBounds{0.0f, 0.0f, 1.0f, 1.0f};
                    ImageAtlasEntry fillEntry{};
                    if (resolveFillEntry(fillEntry)) {
                        const Rect imgCellRect = computeFillCellRect();
                        const float dpi = std::max(0.25f, tree.RootDpiScale());
                        const float sx = std::max(0.25f, std::hypot(world2D.m00, world2D.m01) * dpi);
                        const float sy = std::max(0.25f, std::hypot(world2D.m10, world2D.m11) * dpi);
                        const std::uint32_t innerW = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(rect.width * sx)), 1u, 2048u);
                        const std::uint32_t innerH = std::clamp<std::uint32_t>(
                            static_cast<std::uint32_t>(std::lround(rect.height * sy)), 1u, 2048u);
                        RasterizedPathMaskEntry maskedEntry{};
                        if (CompositeMaskedPathTexture(
                                *store, store->Images(),
                                kUnitCircleHash, kUnitCirclePath, kUnitCircleBounds,
                                imgCellRect, rect, innerW, innerH, 1u,
                                fillEntry, fillImg.stretch, fillImg.tintRgba, fillImg.tileWrap, maskedEntry) &&
                            maskedEntry.textureId > 0u) {
                            const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
                            (void)tree.AllocateTexturedNode(
                                rect.x, rect.y, rect.width, rect.height,
                                maskedEntry.u0, maskedEntry.v0, maskedEntry.u1, maskedEntry.v1,
                                Color{1.0f, 1.0f, 1.0f, effFillOp}, 1.0f, &node, maskedEntry.textureId);
                            const auto endNid = static_cast<std::uint32_t>(tree.nodes_.size());
                            ApplyNodeTransform(tree, beginNid, endNid, world2D, world3D, has3D, blend);
                            count += (endNid - beginNid);
                        } else {
                            count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
                        }
                    } else {
                        count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
                    }
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
                    const Color strokeCol = (vec.strokeGradId > 0)
                        ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                        : UnpackRgba32(vec.strokeRgba, effStrokeOp);
                    const bool trimmed = (vec.trimStart > 0.001f) || (vec.trimEnd < 0.999f);
                    if (trimmed && (vec.trimEnd <= vec.trimStart + 0.0005f || rect.width <= 1.0f || rect.height <= 1.0f)) {
                        // Fully trimmed or degenerate trimmed arc: emit nothing.
                    } else if ((trimmed || !vec.dashArray.empty()) && rect.width > 1.0f && rect.height > 1.0f) {
                        constexpr int kEllipseSegs = 96;
                        const float cx = rect.x + rect.width * 0.5f;
                        const float cy = rect.y + rect.height * 0.5f;
                        const float rx = std::max(0.5f, (rect.width - vec.strokeWidth) * 0.5f);
                        const float ry = std::max(0.5f, (rect.height - vec.strokeWidth) * 0.5f);
                        std::vector<Point> ringPts;
                        ringPts.reserve(static_cast<std::size_t>(kEllipseSegs + 1));
                        for (int s = 0; s <= kEllipseSegs; ++s) {
                            const float ang = (static_cast<float>(s) / static_cast<float>(kEllipseSegs)) * 6.28318530718f;
                            ringPts.push_back(Point{cx + std::cos(ang) * rx, cy + std::sin(ang) * ry});
                        }
                        const std::vector<Point> finalPts = trimmed
                            ? TrimPolylineRange(ringPts, vec.trimStart, vec.trimEnd)
                            : ringPts;
                        if (!vec.dashArray.empty()) {
                            count += EmitDashedPolylinePoints(
                                tree, finalPts, vec.dashArray, vec.dashCap,
                                vec.strokeWidth, strokeCol, vec.strokeGradId, &node, world2D, world3D, has3D, blend);
                        } else {
                            for (std::size_t i = 1u; i < finalPts.size(); ++i) {
                                const std::uint8_t sCap = (i == 1u) ? vec.startLineCap : 2u;
                                const std::uint8_t eCap = (i + 1u == finalPts.size()) ? vec.endLineCap : 2u;
                                count += EmitStyledDashCapsule(
                                    tree, finalPts[i - 1u], finalPts[i], vec.strokeWidth,
                                    sCap, eCap, strokeCol, vec.strokeGradId,
                                    &node, world2D, world3D, has3D, blend);
                            }
                        }
                    } else {
                        const auto beginNid = static_cast<std::uint32_t>(tree.nodes_.size());
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
                const bool hasImageFill = (!fillImg.uri.empty() || fillImg.visualBrushSource.IsValid()) &&
                                          effFillOp > 0.001f &&
                                          rect.width > 0.0f && rect.height > 0.0f;
                const bool hasFill = (HasAlpha(vec.fillRgba) || vec.fillGradId > 0) && effFillOp > 0.001f;
                if (hasImageFill) {
                    count += EmitImageVisual(tree, node, rect, effFillOp, world2D, world3D, has3D, blend, optWorld, optDrawBuffer);
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
                    if (!vec.dashArray.empty() && rect.width > 1.0f && rect.height > 1.0f) {
                        const float halfS = std::min(vec.strokeWidth * 0.5f, 0.5f * std::min(rect.width, rect.height));
                        const float x0 = rect.x + halfS;
                        const float y0 = rect.y + halfS;
                        const float x1 = rect.x + rect.width - halfS;
                        const float y1 = rect.y + rect.height - halfS;
                        const float iw = std::max(0.0f, x1 - x0);
                        const float ih = std::max(0.0f, y1 - y0);
                        const float irx = std::min(std::max(0.0f, rx - halfS), iw * 0.5f);
                        const float iry = std::min(std::max(0.0f, ry - halfS), ih * 0.5f);
                        std::vector<Point> rectPts;
                        if (irx > 0.5f && iry > 0.5f) {
                            constexpr int kCornerSteps = 8;
                            constexpr float kHalfPi = kPi * 0.5f;
                            auto appendArc = [&](float cx, float cy, float a0) {
                                for (int s = 0; s <= kCornerSteps; ++s) {
                                    const float ang = a0 + (static_cast<float>(s) / static_cast<float>(kCornerSteps)) * kHalfPi;
                                    rectPts.push_back(Point{cx + std::cos(ang) * irx, cy + std::sin(ang) * iry});
                                }
                            };
                            rectPts.reserve(static_cast<std::size_t>((kCornerSteps + 1) * 4 + 1));
                            appendArc(x1 - irx, y0 + iry, -kHalfPi);
                            appendArc(x1 - irx, y1 - iry, 0.0f);
                            appendArc(x0 + irx, y1 - iry, kHalfPi);
                            appendArc(x0 + irx, y0 + iry, kPi);
                            rectPts.push_back(rectPts.front());
                        } else {
                            rectPts = {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1}, Point{x0, y0}};
                        }
                        const Color dashCol = (vec.strokeGradId > 0)
                            ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                            : strokeCol;
                        count += EmitDashedPolylinePoints(
                            tree, rectPts, vec.dashArray, vec.dashCap,
                            vec.strokeWidth, dashCol, vec.strokeGradId, &node,
                            world2D, world3D, has3D, blend);
                    } else {
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
                }
                break;
            }

            case VectorShapeKind::Line: {
                const float effStrokeOp = std::clamp(effOpacity * vec.strokeOpacity, 0.0f, 1.0f);
                if ((!HasAlpha(vec.strokeRgba) && vec.strokeGradId <= 0) ||
                    vec.strokeWidth <= 0.0f || effStrokeOp <= 0.001f) {
                    break;
                }
                float nums[4]{};
                const std::size_t numCount = ParseCsvFloatsFixed(vec.pathOrParams, nums, 4u);
                Point p0{rect.x, rect.y};
                Point p1{rect.x + rect.width, rect.y + rect.height};
                if (numCount >= 4u) {
                    p0 = Point{rect.x + nums[0], rect.y + nums[1]};
                    p1 = Point{rect.x + nums[2], rect.y + nums[3]};
                }
                const float s0 = std::clamp(vec.trimStart, 0.0f, 1.0f);
                const float s1 = std::clamp(vec.trimEnd, 0.0f, 1.0f);
                if (s1 > s0) {
                    const Point tp0{p0.x + (p1.x - p0.x) * s0, p0.y + (p1.y - p0.y) * s0};
                    const Point tp1{p0.x + (p1.x - p0.x) * s1, p0.y + (p1.y - p0.y) * s1};
                    const Color strokeCol = (vec.strokeGradId > 0)
                        ? Color{1.0f, 1.0f, 1.0f, effStrokeOp}
                        : UnpackRgba32(vec.strokeRgba, effStrokeOp);
                    if (!vec.dashArray.empty()) {
                        const std::vector<Point> linePts{tp0, tp1};
                        count += EmitDashedPolylinePoints(
                            tree, linePts, vec.dashArray, vec.dashCap,
                            vec.strokeWidth, strokeCol, vec.strokeGradId, &node,
                            world2D, world3D, has3D, blend);
                    } else {
                        count += EmitStyledDashCapsule(
                            tree,
                            tp0,
                            tp1,
                            vec.strokeWidth,
                            vec.startLineCap,
                            vec.endLineCap,
                            strokeCol,
                            vec.strokeGradId,
                            &node,
                            world2D,
                            world3D,
                            has3D,
                            blend);
                    }
                }
                break;
            }

            case VectorShapeKind::RadialArc: {
                const std::uint32_t rgba = HasAlpha(vec.strokeRgba) ? vec.strokeRgba : vec.fillRgba;
                if (!HasAlpha(rgba)) {
                    break;
                }
                float nums[3]{};
                const std::size_t numCount = ParseCsvFloatsFixed(vec.pathOrParams, nums, 3u);
                const float startDeg = (numCount >= 1u) ? nums[0] : 0.0f;
                const float sweepDeg = (numCount >= 2u) ? nums[1] : 360.0f;
                const float innerRatio = (numCount >= 3u)
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
                case DrawCommandKind::DrawTextRuns: {
                    if (!cmd.text.empty() && curOpacity > 0.0f) {
                        UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
                        const std::size_t sep = cmd.text.find('\0');
                        const std::string_view fontFam = (sep != std::string::npos)
                            ? std::string_view(cmd.text).substr(0u, sep)
                            : std::string_view{};
                        const std::string_view txt = (sep != std::string::npos)
                            ? std::string_view(cmd.text).substr(sep + 1u)
                            : std::string_view(cmd.text);
                        if (!txt.empty()) {
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
                            const float fontSize = (cmd.p0 > 0.0f) ? cmd.p0 : 14.0f;

                            if (cmd.textSpans.empty()) {
                                count += tree.EmitTextRunNodes(
                                    rect.x + cmd.rect.x,
                                    rect.y + cmd.rect.y,
                                    fontSize,
                                    txt,
                                    UnpackRgba32(cmd.colorRgba, curOpacity),
                                    curOpacity,
                                    state,
                                    &node,
                                    fontFam,
                                    boxW,
                                    TextWrapping::NoWrap,
                                    TextAlignment::Left,
                                    cmd.rect.width,
                                    nullptr);
                            } else {
                                std::vector<RichTextRunInput> runs;
                                runs.reserve(cmd.textSpans.size() + 2u);
                                std::size_t cursor = 0u;
                                const std::size_t n = txt.size();
                                for (const StyledTextSpan& sp : cmd.textSpans) {
                                    const std::size_t s = std::min<std::size_t>(sp.byteOffset, n);
                                    const std::size_t e = std::min<std::size_t>(s + sp.byteLength, n);
                                    if (s > cursor) {
                                        RichTextRunInput gap{};
                                        gap.text = txt.substr(cursor, s - cursor);
                                        gap.fontFamily = fontFam;
                                        gap.fontSize = fontSize;
                                        gap.fgRgba = cmd.colorRgba;
                                        gap.hasFg = true;
                                        runs.push_back(gap);
                                    }
                                    if (e > s) {
                                        RichTextRunInput run{};
                                        run.text = txt.substr(s, e - s);
                                        run.fontFamily = fontFam;
                                        run.fontSize = fontSize;
                                        run.fgRgba = sp.colorRgba;
                                        run.hasFg = true;
                                        runs.push_back(run);
                                        cursor = e;
                                    }
                                }
                                if (cursor < n) {
                                    RichTextRunInput tail{};
                                    tail.text = txt.substr(cursor, n - cursor);
                                    tail.fontFamily = fontFam;
                                    tail.fontSize = fontSize;
                                    tail.fgRgba = cmd.colorRgba;
                                    tail.hasFg = true;
                                    runs.push_back(tail);
                                }
                                if (!runs.empty()) {
                                    TextLayoutResult layout = store->Fonts().MeasureRichLayout(
                                        runs,
                                        fontFam,
                                        fontSize,
                                        400,
                                        false,
                                        boxW,
                                        TextWrapping::NoWrap);
                                    count += tree.EmitPreparedTextLayoutNodes(
                                        rect.x + cmd.rect.x,
                                        rect.y + cmd.rect.y,
                                        fontSize,
                                        std::move(layout),
                                        UnpackRgba32(cmd.colorRgba, curOpacity),
                                        curOpacity,
                                        state,
                                        &node,
                                        TextAlignment::Left,
                                        cmd.rect.width,
                                        nullptr,
                                        nullptr,
                                        0.0f,
                                        /*ignoreCustomGlyphColors=*/false,
                                        /*reserveDynamicSlack=*/false);
                                }
                            }
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
        if (!node || node->layout.visibility != Visibility::Visible) {
            return 0u;
        }

        const auto nodeRenderStart = static_cast<std::uint32_t>(outTree.nodes_.size());
        std::size_t count = 0u;
        const float effOpacity = std::clamp(parentOpacity * node->layout.opacity, 0.0f, 1.0f);
        if (effOpacity <= 0.001f) {
            if (mutableWorld != nullptr) {
                if (VisualNode* mutNode = mutableWorld->GetNode(id)) {
                    mutNode->layout.renderNodeId = 0u;
                    mutNode->layout.renderNodeCount = 0u;
                }
            }
            return 0u;
        }
        const Rect& rect = node->layout.layoutRect;
        const Matrix2D& world2D = node->layout.worldTransform;
        const Matrix4x4& world3D = node->WorldMatrix3D();
        const bool has3D = node->layout.hasPerspective3D;
        const VisualImageData& imgData = node->Image();
        const VisualEffectStyle& effStyle = node->Effect();
        const VisualOpacityMask& maskStyle = node->OpacityMask();
        const BlendingMode ownBlend = node->transform2D.blendMode;
        const BlendingMode inheritedBlend = outTree.activeBlendMode_;
        const BlendingMode blend = (ownBlend != BlendingMode::Normal) ? ownBlend : inheritedBlend;

        const bool isVisible = (node->layout.visibility == Visibility::Visible);
        const bool hasOwnBoxVisual =
            (node->box.bgOpacity > 0.001f && (HasAlpha(node->box.bgRgba) || node->box.bgGradId > 0)) ||
            ((!imgData.uri.empty() || imgData.visualBrushSource.IsValid()) && HasAlpha(imgData.tintRgba)) ||
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
        const UIResourceStore* effStore = outTree.ResourceStore() ? outTree.ResourceStore() : world.ResourceStore();
        const bool hasOpacityMask = isVisible && NodeNeedsOpacityMaskSpan(*node, effStore);
        const bool useTranslucentDropShadowSpan =
            isVisible &&
            ownDropShadow != nullptr &&
            !hasOpacityMask &&
            effOpacity < 0.999f &&
            HasAlpha(ownDropShadow->colorRgba) &&
            ownDropShadow->opacity > 0.0f &&
            (drawsSelf || !node->children.empty());
        if (useTranslucentDropShadowSpan) {
            childInheritedShadow = nullptr;
        }

        // Emit DropShadow behind this element (outside element clip) before node content.
        if (isVisible &&
            !useTranslucentDropShadowSpan &&
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
            ((effStyle.kind != UIEffectKind::None && effStyle.kind != UIEffectKind::DropShadow) ||
             useTranslucentDropShadowSpan);
        const BlendingMode selfBlend = (hasPostProcessEffect || hasOpacityMask) ? BlendingMode::Normal : blend;
        const ScopedBlendMode blendGuard(outTree.activeBlendMode_, selfBlend);
        std::size_t blurSpanIndex = std::numeric_limits<std::size_t>::max();
        const std::uint32_t spanStartNid = static_cast<std::uint32_t>(outTree.nodes_.size());
        if (hasPostProcessEffect || hasOpacityMask) {
            BlurLayerSpan span{};
            span.firstNodeId = spanStartNid;
            span.endNodeId = spanStartNid;
            span.effectMode = hasPostProcessEffect ? effStyle.kind : UIEffectKind::OpacityMask;
            span.worldScale = worldScale;
            if (useTranslucentDropShadowSpan) {
                span.radiusPx = std::max(0.0f, effStyle.radius);
                span.angleDeg = effStyle.dirDeg;
                span.amountX = has3D ? (effStyle.depth / std::max(0.25f, worldScale)) : effStyle.depth;
                span.effectColor = UnpackRgba32(effStyle.colorRgba, std::clamp(effStyle.opacity, 0.0f, 1.0f));
                span.effectOpacity = effOpacity;
            } else if (hasPostProcessEffect) {
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
                        if (!vec.svgBoundsCached) {
                            vec.cachedPathHash = ComputeSvgPathHash(vec.pathOrParams);
                            vec.cachedSvgBounds = MeasureSvgPathBounds(vec.pathOrParams);
                            vec.svgBoundsCached = true;
                        }
                        const Rect rawB = vec.cachedSvgBounds;
                        if (rawB.width > 0.0f && rawB.height > 0.0f) {
                            const Rect fitted = (vec.stretch == Stretch::None)
                                ? Rect{rect.x + rawB.x, rect.y + rawB.y, rawB.width, rawB.height}
                                : ComputeStretchedBox(rect, rawB.width, rawB.height, vec.stretch);
                            span.maskDestRect = TransformRectViewportBounds(fitted, world2D, world3D, has3D);
                            span.clipRect = outTree.activeClipEnabled_
                                ? IntersectRects(outTree.activeClipRect_, span.maskDestRect)
                                : span.maskDestRect;
                        }
                        const std::uint64_t pathHash = vec.cachedPathHash;
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
            outTree.activeBlurSpanDepth_ += 1u;
            outTree.coalesceBarrierPage_ = outTree.pages_.size();
        }

        // Push node scissor clip (ClipToBounds or ScrollViewport)
        const bool prevClipEnabled = outTree.activeClipEnabled_;
        const Rect prevClipRect = outTree.activeClipRect_;
        const bool clipsSelf = node->layout.clipToBounds || node->layout.needsLayoutClip ||
                               (node->layout.layoutMode == LayoutMode::ScrollViewport);
        bool skipClipped = false;
        if (clipsSelf) {
            const Rect nodeWorldBounds = TransformRectViewportBounds(rect, world2D, world3D, has3D);
            const Rect newClip = prevClipEnabled
                ? IntersectRects(prevClipRect, nodeWorldBounds)
                : nodeWorldBounds;
            if (newClip.width <= 0.0f || newClip.height <= 0.0f) {
                skipClipped = true;
            } else if (!CanSkipLeafTextClip(*node, rect, world2D, has3D, outTree)) {
                outTree.activeClipEnabled_ = true;
                outTree.activeClipRect_ = newClip;
            }
        }

        if (!skipClipped) {
            const float innerEffOpacity = useTranslucentDropShadowSpan ? 1.0f : effOpacity;
            const bool hasNonZeroRect = (rect.width > 0.0f && rect.height > 0.0f);
            const bool allowZeroRectVector =
                (node->kind == VisualKind::Vector) &&
                (node->Vector().stretch == Stretch::None ||
                 node->Vector().shapeKind == VectorShapeKind::Line);
            if (isVisible && (hasNonZeroRect || allowZeroRectVector)) {
                const bool prevHasHint = outTree.hasHintNextBounds_;
                const Rect prevHint = outTree.hintNextBounds_;
                const bool canHintBounds =
                    !has3D &&
                    outTree.activeBlurSpanDepth_ == 0u &&
                    childInheritedShadow == nullptr &&
                    node->Effect().kind == UIEffectKind::None &&
                    !node->OpacityMask().enabled &&
                    hasNonZeroRect &&
                    node->kind != VisualKind::Text &&
                    node->kind != VisualKind::CanvasDraw &&
                    !(node->kind == VisualKind::Box && node->box.isBoxShadow) &&
                    !(node->kind == VisualKind::Vector && node->Vector().stretch == Stretch::None);
                if (canHintBounds) {
                    const float maxBorder = std::max(
                        std::max(0.0f, std::max(node->box.borderThickness.left, node->box.borderThickness.right)),
                        std::max(0.0f, std::max(node->box.borderThickness.top, node->box.borderThickness.bottom)));
                    const float strokeExtra = (node->kind == VisualKind::Vector)
                        ? std::max(2.0f, node->Vector().strokeWidth + 4.0f)
                        : (maxBorder + 2.0f);
                    const Rect paddedLocal{
                        rect.x - strokeExtra,
                        rect.y - strokeExtra,
                        rect.width + 2.0f * strokeExtra,
                        rect.height + 2.0f * strokeExtra};
                    outTree.hintNextBounds_ = TransformRectViewportBounds(paddedLocal, world2D, world3D, false);
                    outTree.hasHintNextBounds_ = true;
                } else {
                    outTree.hasHintNextBounds_ = false;
                }

                if (hasNonZeroRect) {
                    count += EmitBoxVisual(outTree, *node, rect, innerEffOpacity, world2D, world3D, has3D, selfBlend, &world, drawBuffer);
                }

                switch (node->kind) {
                    case VisualKind::Text:
                        if (hasNonZeroRect) {
                            count += EmitTextVisual(outTree, *node, rect, innerEffOpacity, world2D, world3D, has3D, selfBlend);
                        }
                        break;
                    case VisualKind::Image:
                        if (hasNonZeroRect) {
                            count += EmitImageVisual(outTree, *node, rect, innerEffOpacity, world2D, world3D, has3D, selfBlend, &world, drawBuffer);
                        }
                        break;
                    case VisualKind::Vector:
                        count += EmitVectorVisual(outTree, *node, rect, innerEffOpacity, world2D, world3D, has3D, selfBlend, &world, drawBuffer);
                        break;
                    case VisualKind::CanvasDraw:
                        if (hasNonZeroRect) {
                            count += EmitCanvasDrawVisual(
                                outTree, drawBuffer, id, *node, rect, innerEffOpacity, world2D, world3D, has3D, selfBlend);
                        }
                        break;
                    case VisualKind::Box:
                    default:
                        break;
                }

                outTree.hasHintNextBounds_ = prevHasHint;
                outTree.hintNextBounds_ = prevHint;
            }

            if (!node->children.empty()) {
                bool needZSort = false;
                std::int32_t firstZ = 0;
                bool sawFirst = false;
                for (VisualNodeId cid : node->children) {
                    if (const VisualNode* ch = world.GetNode(cid)) {
                        if (!sawFirst) {
                            firstZ = ch->layout.zIndex;
                            sawFirst = true;
                        } else if (ch->layout.zIndex != firstZ) {
                            needZSort = true;
                            break;
                        }
                    }
                }
                const bool hasShadowPass =
                    isVisible &&
                    childInheritedShadow != nullptr &&
                    HasAlpha(childInheritedShadow->colorRgba) &&
                    childInheritedShadow->opacity > 0.0f;

                if (!needZSort && !hasShadowPass) {
                    // Zero-allocation fast path for >98% of UI containers with uniform zIndex
                    for (VisualNodeId cid : node->children) {
                        if (world.GetNode(cid) != nullptr) {
                            count += EmitNodeRecursive(
                                mutableWorld,
                                world,
                                drawBuffer,
                                cid,
                                innerEffOpacity,
                                outTree,
                                childInheritedShadow);
                        }
                    }
                } else {
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
                if (needZSort) {
                    std::sort(sorted.begin(), sorted.end(), [](const ZChild& a, const ZChild& b) {
                        if (a.z != b.z) {
                            return a.z < b.z;
                        }
                        return a.order < b.order;
                    });
                }

                const VisualEffectStyle* passInheritedShadow = childInheritedShadow;
                if (hasShadowPass) {
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
                        outTree.activeBlurSpanDepth_ += 1u;
                        outTree.coalesceBarrierPage_ = outTree.pages_.size();
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
                        if (outTree.activeBlurSpanDepth_ > 0u) {
                            outTree.activeBlurSpanDepth_ -= 1u;
                        }
                        outTree.coalesceBarrierPage_ = outTree.pages_.size();
                    }
                    passInheritedShadow = nullptr;
                }

                for (const ZChild& item : sorted) {
                    count += EmitNodeRecursive(
                        mutableWorld,
                        world,
                        drawBuffer,
                        item.id,
                        innerEffOpacity,
                        outTree,
                        passInheritedShadow);
                }
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
            if (outTree.activeBlurSpanDepth_ > 0u) {
                outTree.activeBlurSpanDepth_ -= 1u;
            }
            outTree.coalesceBarrierPage_ = outTree.pages_.size();
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
            shadowSpan.clipRect = outTree.activeClipRect_;
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
            auto& sv = shadowVecNode.EnsureVectorData();
            sv = srcVec;
            const bool hasSrcImg =
                !imgData.uri.empty() && HasAlpha(imgData.tintRgba) && srcVec.fillOpacity > 0.001f;
            if (hasSrcImg) {
                auto& si = shadowVecNode.EnsureImageData();
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
                sv.strokeRgba = 0u;
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
            auto& st = shadowTextNode.EnsureTextData();
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
    const bool clipsSelf = node->layout.clipToBounds || node->layout.needsLayoutClip ||
                           (node->layout.layoutMode == LayoutMode::ScrollViewport);
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
        if (newClip.width <= 0.0f || newClip.height <= 0.0f) {
            skipClipped = true;
        } else if (!CanSkipLeafTextClip(*node, rect, world2D, has3D, outTree)) {
            outTree.activeClipEnabled_ = true;
            outTree.activeClipRect_ = newClip;
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
            bool uniformZ = (node->children.size() <= 1u);
            if (!uniformZ) {
                std::int32_t firstZ = 0;
                bool hasFirstZ = false;
                uniformZ = true;
                for (VisualNodeId cid : node->children) {
                    if (const VisualNode* ch = world.GetNode(cid)) {
                        if (!hasFirstZ) {
                            firstZ = ch->layout.zIndex;
                            hasFirstZ = true;
                        } else if (ch->layout.zIndex != firstZ) {
                            uniformZ = false;
                            break;
                        }
                    }
                }
            }
            if (uniformZ) {
                for (VisualNodeId cid : node->children) {
                    count += EmitDropShadowSubtreeRecursive(
                        world,
                        cid,
                        effOpacity,
                        outTree,
                        dropShadow,
                        dx,
                        dy,
                        blurR);
                }
            } else {
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
    UIResourceStore* const resolvedStore = resourceStore ? resourceStore : const_cast<UIResourceStore*>(world.ResourceStore());
    outTree.SetResourceStore(resolvedStore);
    if (resolvedStore != nullptr) {
        resolvedStore->BeginPathMaskFrame();
    }
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

    const std::uint64_t drawVer =
        (drawBuffer && !drawBuffer->Empty()) ? drawBuffer->GlobalVersion() : 0u;
    const bool drawBufferUnchanged = (drawVer == outTree.SyncedDrawBufferVersion());

    // Fast Path 1: nothing dirty and render tree already matches this world & structure version
    if (!world.HasAnyDirty() &&
        !world.IsStructureDirty() &&
        drawBufferUnchanged &&
        outTree.MatchesSyncedWorldState(world.WorldInstanceId(), world.StructureVersion())) {
        return outTree.NodeCount();
    }

    // Fast Path 2: visual-only leaf solid-box updates in place
    const auto& dirtyList = world.DirtyNodes();
    if (!world.IsStructureDirty() &&
        drawBufferUnchanged &&
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
                node->Image().visualBrushSource.IsValid() ||
                node->Effect().kind != UIEffectKind::None ||
                node->OpacityMask().enabled ||
                node->layout.layoutRect.width <= 0.0f ||
                node->layout.layoutRect.height <= 0.0f ||
                (drawBuffer != nullptr && drawBuffer->HasCommands(id))) {
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
                    p->transform2D.blendMode != BlendingMode::Normal ||
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

    // Full rebuild fallback: prefetch multiple dirty .riv visuals in parallel before tree emit
    if (world.Root().IsValid() && store != nullptr) {
        std::vector<std::pair<VisualNodeId, float>> visitStack;
        visitStack.reserve(64u);
        visitStack.emplace_back(world.Root(), 1.0f);
        std::vector<std::string> dirtyRiveUris;
        std::unordered_set<std::string> seenRiveUris;
        ImageAtlas& atlas = store->Images();
        while (!visitStack.empty()) {
            const auto [curId, curParentOp] = visitStack.back();
            visitStack.pop_back();
            const VisualNode* vn = world.GetNode(curId);
            if (!vn || !vn->alive || vn->layout.visibility != Visibility::Visible) {
                continue;
            }
            const float effOp = std::clamp(curParentOp * vn->layout.opacity, 0.0f, 1.0f);
            if (effOp <= 0.001f) {
                continue;
            }
            const std::string& uri = vn->Image().uri;
            if (!uri.empty() && IsRiveAssetUri(uri)) {
                const ImageAtlasEntry* cached = atlas.FindLoaded(uri);
                if ((!cached || cached->uriKey != uri) && seenRiveUris.insert(uri).second) {
                    dirtyRiveUris.push_back(uri);
                }
            }
            for (VisualNodeId cid : vn->children) {
                visitStack.emplace_back(cid, effOp);
            }
        }
        if (dirtyRiveUris.size() >= 2u) {
            struct RivePrefetchPool {
                std::mutex mtx;
                std::condition_variable cvWork;
                std::condition_variable cvDone;
                std::vector<std::thread> workers;
                ImageAtlas* curAtlas = nullptr;
                const std::vector<std::string>* curUris = nullptr;
                std::atomic<std::size_t> nextIdx{0u};
                std::size_t remainingWorkers = 0u;
                std::uint64_t batchGen = 0u;
                bool stop = false;

                RivePrefetchPool() {
                    const std::size_t hw = std::thread::hardware_concurrency();
                    const std::size_t nThreads = std::clamp<std::size_t>(hw / 2u, 2u, 4u);
                    workers.reserve(nThreads);
                    for (std::size_t t = 0u; t < nThreads; ++t) {
                        workers.emplace_back([this]() {
                            std::uint64_t seenGen = 0u;
                            for (;;) {
                                ImageAtlas* localAtlas = nullptr;
                                const std::vector<std::string>* localUris = nullptr;
                                {
                                    std::unique_lock<std::mutex> lk(mtx);
                                    cvWork.wait(lk, [&]() { return stop || batchGen != seenGen; });
                                    if (stop) break;
                                    seenGen = batchGen;
                                    localAtlas = curAtlas;
                                    localUris = curUris;
                                }
                                if (localAtlas && localUris) {
                                    const std::size_t n = localUris->size();
                                    for (;;) {
                                        const std::size_t idx = nextIdx.fetch_add(1u, std::memory_order_relaxed);
                                        if (idx >= n) break;
                                        (void)localAtlas->LoadOrGet((*localUris)[idx]);
                                    }
                                }
                                std::lock_guard<std::mutex> lk(mtx);
                                if (--remainingWorkers == 0u) {
                                    cvDone.notify_one();
                                }
                            }
                        });
                    }
                }
                ~RivePrefetchPool() {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        stop = true;
                    }
                    cvWork.notify_all();
                    for (std::thread& w : workers) {
                        if (w.joinable()) w.join();
                    }
                }
                void Run(ImageAtlas& atlasRef, const std::vector<std::string>& uris) {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        curAtlas = &atlasRef;
                        curUris = &uris;
                        nextIdx.store(0u, std::memory_order_relaxed);
                        remainingWorkers = workers.size();
                        ++batchGen;
                    }
                    cvWork.notify_all();
                    std::unique_lock<std::mutex> lk(mtx);
                    cvDone.wait(lk, [&]() { return remainingWorkers == 0u; });
                    curAtlas = nullptr;
                    curUris = nullptr;
                }
            };
            auto parseDim = [](const std::string& uriStr, std::string_view key, std::uint64_t defVal) -> std::uint64_t {
                const std::size_t pos = uriStr.find(key);
                if (pos == std::string::npos) return defVal;
                std::size_t i = pos + key.size();
                std::uint64_t val = 0u;
                bool any = false;
                while (i < uriStr.size() && uriStr[i] >= '0' && uriStr[i] <= '9') {
                    val = val * 10u + static_cast<std::uint64_t>(uriStr[i] - '0');
                    any = true;
                    ++i;
                }
                return any ? val : defVal;
            };
            auto estArea = [&](const std::string& uriStr) -> std::uint64_t {
                const std::uint64_t w = parseDim(uriStr, "__w=", 960u);
                const std::uint64_t h = parseDim(uriStr, "__h=", 960u);
                return w * h;
            };
            std::sort(dirtyRiveUris.begin(), dirtyRiveUris.end(),
                      [&](const std::string& a, const std::string& b) {
                          return estArea(a) > estArea(b);
                      });
            static RivePrefetchPool sRivePool;
            sRivePool.Run(atlas, dirtyRiveUris);
        }
    }

    if (store != nullptr) {
        store->BeginPathMaskFrame();
    }
    outTree.Clear();
    outTree.SetResourceStore(store);
    if (!world.Root().IsValid()) {
        outTree.SetSyncedWorldState(world.WorldInstanceId(), world.StructureVersion(), drawVer);
        world.ClearDirtyFlags();
        return 0u;
    }
    outTree.ReserveCapacity(world.LiveNodeCount() * 2u);
    const std::size_t count =
        EmitNodeRecursive(&world, world, drawBuffer, world.Root(), 1.0f, outTree);
    outTree.SetSyncedWorldState(world.WorldInstanceId(), world.StructureVersion(), drawVer);
    world.ClearDirtyFlags();
    return count;
}

} // namespace aero
