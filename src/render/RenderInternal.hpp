#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "aero/render/PagedRenderTree.hpp"

namespace aero::render::detail {

[[nodiscard]] inline int SafeFloorToInt(float v, int lo, int hi) noexcept {
    if (!std::isfinite(v)) {
        return lo;
    }
    const float f = std::floor(v);
    if (f <= static_cast<float>(lo)) {
        return lo;
    }
    if (f >= static_cast<float>(hi)) {
        return hi;
    }
    return static_cast<int>(f);
}

[[nodiscard]] inline int SafeCeilToInt(float v, int lo, int hi) noexcept {
    if (!std::isfinite(v)) {
        return lo;
    }
    const float c = std::ceil(v);
    if (c <= static_cast<float>(lo)) {
        return lo;
    }
    if (c >= static_cast<float>(hi)) {
        return hi;
    }
    return static_cast<int>(c);
}

[[nodiscard]] inline bool SpanNeedsEffectPass(const BlurLayerSpan& span) noexcept {
    if (span.hasOpacityMask || span.effectMode == UIEffectKind::OpacityMask) {
        return true;
    }
    switch (span.effectMode) {
        case UIEffectKind::Blur:
        case UIEffectKind::DirectionalBlur:
            return std::isfinite(span.radiusPx) && span.radiusPx > 0.25f;
        case UIEffectKind::Pixelate:
            return std::isfinite(span.radiusPx) && span.radiusPx >= 1.5f;
        case UIEffectKind::Tint:
            return true;
        case UIEffectKind::ChromaticAberration:
            return std::isfinite(span.amountX) && std::fabs(span.amountX) > 0.01f;
        case UIEffectKind::Saturation:
            return std::isfinite(span.saturation) && std::fabs(span.saturation - 1.0f) > 0.001f;
        case UIEffectKind::BackdropBlur:
            return (std::isfinite(span.radiusPx) && span.radiusPx > 0.25f) ||
                   (std::isfinite(span.saturation) && std::fabs(span.saturation - 1.0f) > 0.001f);
        default:
            return false;
    }
}

[[nodiscard]] inline float ComputeSpanBoxRadiusFloat(const BlurLayerSpan& span) noexcept {
    const float ws = (std::isfinite(span.worldScale) && span.worldScale > 0.0f) ? span.worldScale : 1.0f;
    const float effRadius = span.radiusPx * ws;
    if (!std::isfinite(effRadius) || effRadius <= 0.25f) {
        return 0.0f;
    }
    if (span.effectMode == UIEffectKind::BackdropBlur) {
        // Calibrate 3-pass box blur variance to WPF/NoesisGUI Gaussian BlurEffect.Radius (3*sigma = radius)
        return std::clamp(effRadius, 0.25f, 480.0f) * 0.26f;
    }
    return std::clamp(effRadius, 0.5f, 240.0f) * 0.5f;
}

[[nodiscard]] inline int ComputeSpanBoxRadius(const BlurLayerSpan& span) noexcept {
    const float boxRf = ComputeSpanBoxRadiusFloat(span);
    if (boxRf > 0.05f) {
        return std::max(1, static_cast<int>(std::ceil(boxRf)));
    }
    return 0;
}

[[nodiscard]] inline int ComputeSpanPaddingPx(const BlurLayerSpan& span) noexcept {
    switch (span.effectMode) {
        case UIEffectKind::Blur:
        case UIEffectKind::BackdropBlur:
        case UIEffectKind::Pixelate: {
            const int boxR = ComputeSpanBoxRadius(span);
            return (boxR > 0) ? (boxR * 3 + 2) : 0;
        }
        case UIEffectKind::DirectionalBlur: {
            const float r = std::clamp(std::isfinite(span.radiusPx) ? span.radiusPx : 0.0f, 0.0f, 128.0f);
            return static_cast<int>(std::ceil(r)) + 2;
        }
        case UIEffectKind::ChromaticAberration: {
            const float s = std::clamp(std::fabs(std::isfinite(span.amountX) ? span.amountX : 0.0f), 0.0f, 64.0f);
            return static_cast<int>(std::ceil(s)) + 2;
        }
        case UIEffectKind::Tint:
        case UIEffectKind::Saturation:
        default:
            return 0;
    }
}

[[nodiscard]] inline bool ComputeNodeRangeScreenBounds(
    const PagedRenderTree& tree,
    std::uint32_t startNodeId,
    std::uint32_t endNodeId,
    float& outMinX,
    float& outMinY,
    float& outMaxX,
    float& outMaxY) noexcept {
    bool any = false;
    float minX = 1e9f;
    float minY = 1e9f;
    float maxX = -1e9f;
    float maxY = -1e9f;
    for (std::uint32_t nid = startNodeId; nid < endNodeId; ++nid) {
        const RenderNodeDescriptor* nd = tree.GetNode(nid);
        if (!nd || nd->pageIndex >= tree.PageCount() || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
            continue;
        }
        const CommandPage& page = tree.GetPage(nd->pageIndex);
        const bool pageClip = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
        if (pageClip &&
            (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
             !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
            continue;
        }
        const float pClipX0 = page.header.clipX;
        const float pClipY0 = page.header.clipY;
        const float pClipX1 = page.header.clipX + std::max(0.0f, page.header.clipW);
        const float pClipY1 = page.header.clipY + std::max(0.0f, page.header.clipH);
        if (pageClip && (pClipX1 <= pClipX0 || pClipY1 <= pClipY0)) {
            continue;
        }

        const std::size_t quadCount = nd->vertexCount / 4u;
        for (std::size_t q = 0; q < quadCount; ++q) {
            const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
            if (vIdx + 4u > kVerticesPerCommandPage) {
                break;
            }
            const AeroVertex& v0 = page.vertices[vIdx + 0u];
            const AeroVertex& v1 = page.vertices[vIdx + 1u];
            const AeroVertex& v2 = page.vertices[vIdx + 2u];
            const AeroVertex& v3 = page.vertices[vIdx + 3u];
            if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) {
                continue;
            }
            if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
                !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
                !std::isfinite(v2.x) || !std::isfinite(v2.y) ||
                !std::isfinite(v3.x) || !std::isfinite(v3.y)) {
                continue;
            }
            float qMinX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
            float qMaxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
            float qMinY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
            float qMaxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));
            if (pageClip) {
                qMinX = std::max(qMinX, pClipX0);
                qMaxX = std::min(qMaxX, pClipX1);
                qMinY = std::max(qMinY, pClipY0);
                qMaxY = std::min(qMaxY, pClipY1);
                if (qMaxX <= qMinX || qMaxY <= qMinY) {
                    continue;
                }
            }
            minX = std::min(minX, qMinX);
            maxX = std::max(maxX, qMaxX);
            minY = std::min(minY, qMinY);
            maxY = std::max(maxY, qMaxY);
            any = true;
        }
    }
    outMinX = minX;
    outMinY = minY;
    outMaxX = maxX;
    outMaxY = maxY;
    return any;
}

} // namespace aero::render::detail
