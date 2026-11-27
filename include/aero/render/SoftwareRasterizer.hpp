#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aero/render/PagedRenderTree.hpp"

namespace aero {

struct SoftwareDirtyRectStats {
    std::uint32_t totalTiles = 0u;
    std::uint32_t dirtyTiles = 0u;
    std::uint32_t mergedStrips = 0u;
    bool fellBackToFullFrame = false;
};

class SoftwareRasterizer final {
public:
    SoftwareRasterizer() = delete;

    // Headless software UberShader rasterizer matching GPU UberShader SDF / MTSDF / Scissor math.
    static void RasterizeTreeToRgba8(
        const PagedRenderTree& tree,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        std::vector<std::uint8_t>& outRgba8,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f});

    static void RasterizeTreeDirtyRectsToRgba8(
        const PagedRenderTree& tree,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        const std::vector<Rect>& dirtyRects,
        std::vector<std::uint8_t>& ioRgba8,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f},
        SoftwareDirtyRectStats* outStats = nullptr);

    static void ReleaseTransientPools() noexcept;

    static bool RasterizeBlurSpanPatchPremulRgba8(
        const PagedRenderTree& tree,
        const BlurLayerSpan& span,
        std::size_t& ioBlurCursor,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        int& outPatchX,
        int& outPatchY,
        int& outPatchW,
        int& outPatchH,
        std::vector<std::uint8_t>& outPremulRgba8);

    static bool ComputeBackdropBlurPatchPremulRgba8(
        const BlurLayerSpan& span,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        int readX,
        int readY,
        int readW,
        int readH,
        const std::uint8_t* topDownReadbackRgba8,
        int& outPatchX,
        int& outPatchY,
        int& outPatchW,
        int& outPatchH,
        std::vector<std::uint8_t>& outPremulRgba8,
        const UIResourceStore* optResourceStore = nullptr);
};

} // namespace aero
