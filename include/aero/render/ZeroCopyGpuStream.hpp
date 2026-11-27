#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/SoftwareRasterizer.hpp"

namespace aero {

enum class ShaderBackend : std::uint8_t {
    DesktopGL33 = 0, // #version 330 core + Persistent Mapped Ring Buffer
    WebGL2 = 1,      // #version 300 es + glBufferSubData Ring Buffer + Grayscale SDF AA Text
    WebGPU_WGSL = 2, // WebGPU WGSL UberShader + Queue::WriteBuffer Dirty-Page Streaming
};

enum class RingUploadMechanism : std::uint8_t {
    PersistentCoherentMap = 0,
    WebGL2BufferSubData = 1,
    WebGpuWriteBuffer = 2,
};

struct GlBufferSubDataSpan {
    std::uint32_t pageIndex = 0;
    std::size_t ringByteOffset = 0;
    std::size_t byteSize = 0;
    const void* sourcePageVertexPtr = nullptr;
    std::size_t clipWRingByteOffset = 0;
    std::size_t clipWByteSize = 0;
    const float* sourcePageClipWPtr = nullptr;
};

struct GpuClipDrawBatch {
    std::uint32_t firstPageIndex = 0;
    std::uint32_t pageCount = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t textureId = 0; // 0 = default font/UI Skyline atlas, >0 = standalone texture handle
    BlendingMode blendMode = BlendingMode::Normal;
    bool scissorActive = false;
    Rect scissorRect{0.0f, 0.0f, 16384.0f, 16384.0f};
};

struct ZeroCopySubmissionStats {
    std::uint64_t frameIndex = 0;
    std::size_t dirtyPagesUploaded = 0;
    std::size_t verticesStreamed = 0;
    std::size_t bytesStreamed = 0;
    std::size_t drawCallCount = 0;
    RingUploadMechanism mechanism = RingUploadMechanism::PersistentCoherentMap;
    bool grayscaleSdfTextActive = false;
};

class ZeroCopyGpuStream {
public:
    explicit ZeroCopyGpuStream(
        ShaderBackend backend = ShaderBackend::DesktopGL33,
        std::size_t maxPagesInRing = 32);

    [[nodiscard]] ShaderBackend Backend() const noexcept { return backend_; }
    void SetBackend(ShaderBackend backend) noexcept;

    [[nodiscard]] RingUploadMechanism UploadMechanism() const noexcept { return uploadMechanism_; }
    [[nodiscard]] bool IsGrayscaleSdfTextRouted() const noexcept {
        return backend_ == ShaderBackend::WebGL2;
    }

    // Bind an externally mapped GPU persistent buffer (or use internal aligned ring storage)
    void BindExternalMappedRing(void* mappedPtr, std::size_t capacityBytes) noexcept;
    void BindExternalMappedClipWRing(void* mappedPtr, std::size_t capacityBytes) noexcept;

    // Stream ONLY dirty 4KB CommandPages from PagedRenderTree into the GPU ring buffer
    // with 0 intermediate copies and 0 heap allocations on the frame hot path.
    ZeroCopySubmissionStats SubmitDirtyPages(PagedRenderTree& tree) noexcept;

    [[nodiscard]] const ZeroCopySubmissionStats& LastStats() const noexcept {
        return lastStats_;
    }

    [[nodiscard]] const std::vector<GlBufferSubDataSpan>& LastBufferSubDataSpans() const noexcept {
        return subDataSpans_;
    }

    [[nodiscard]] const std::vector<GpuClipDrawBatch>& LastClipDrawBatches() const noexcept {
        return clipDrawBatches_;
    }

    [[nodiscard]] const AeroVertex* MappedPageVertices(std::size_t pageIndex) const noexcept;
    [[nodiscard]] const float* MappedPageClipW(std::size_t pageIndex) const noexcept;
    [[nodiscard]] std::size_t RingCapacityBytes() const noexcept { return ringCapacityBytes_; }
    [[nodiscard]] std::size_t ClipWRingCapacityBytes() const noexcept { return clipWRingCapacityBytes_; }

    static void RasterizeTreeToRgba8(
        const PagedRenderTree& tree,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        std::vector<std::uint8_t>& outRgba8,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f}) {
        SoftwareRasterizer::RasterizeTreeToRgba8(tree, viewportWidth, viewportHeight, outRgba8, clearColor);
    }

    static void RasterizeTreeDirtyRectsToRgba8(
        const PagedRenderTree& tree,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        const std::vector<Rect>& dirtyRects,
        std::vector<std::uint8_t>& ioRgba8,
        Color clearColor = Color{0.06f, 0.08f, 0.11f, 1.0f},
        SoftwareDirtyRectStats* outStats = nullptr) {
        SoftwareRasterizer::RasterizeTreeDirtyRectsToRgba8(
            tree, viewportWidth, viewportHeight, dirtyRects, ioRgba8, clearColor, outStats);
    }

    static void ReleaseTransientPools() noexcept {
        SoftwareRasterizer::ReleaseTransientPools();
    }

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
        std::vector<std::uint8_t>& outPremulRgba8) {
        return SoftwareRasterizer::RasterizeBlurSpanPatchPremulRgba8(
            tree, span, ioBlurCursor, viewportWidth, viewportHeight,
            outPatchX, outPatchY, outPatchW, outPatchH, outPremulRgba8);
    }

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
        const UIResourceStore* optResourceStore = nullptr) {
        return SoftwareRasterizer::ComputeBackdropBlurPatchPremulRgba8(
            span, viewportWidth, viewportHeight, readX, readY, readW, readH,
            topDownReadbackRgba8, outPatchX, outPatchY, outPatchW, outPatchH,
            outPremulRgba8, optResourceStore);
    }

    // Built-in unified UberShader sources (Desktop GL 3.3, WebGL2 #version 300 es, WebGPU WGSL)
    [[nodiscard]] static std::string_view UberVertexShaderSource(ShaderBackend backend) noexcept;
    [[nodiscard]] static std::string_view UberPixelShaderSource(ShaderBackend backend) noexcept;

private:
    ShaderBackend backend_ = ShaderBackend::DesktopGL33;
    RingUploadMechanism uploadMechanism_ = RingUploadMechanism::PersistentCoherentMap;
    std::uint64_t frameCounter_ = 0;
    std::size_t maxPagesInRing_ = 32;
    std::size_t ringCapacityBytes_ = 0;
    std::vector<std::uint8_t> ownedRingStorage_;
    std::size_t clipWRingCapacityBytes_ = 0;
    std::vector<float> ownedClipWRingStorage_;
    std::vector<std::size_t> prevUploadedBytes_;
    std::uint8_t* mappedRingBase_ = nullptr;
    float* mappedClipWRingBase_ = nullptr;
    std::vector<GlBufferSubDataSpan> subDataSpans_;
    std::vector<GpuClipDrawBatch> clipDrawBatches_;
    std::uint32_t cachedBatchTreeId_ = 0u;
    std::size_t cachedBatchPageCount_ = 0u;
    ZeroCopySubmissionStats lastStats_{};
};

} // namespace aero
