#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero {

enum class AeroPrimType : std::uint16_t {
    SolidQuad = 0,
    SdfRoundedRect = 1,
    SdfTextGlyph = 2,
    TexturedQuad = 3,
};

enum AeroVertexFlags : std::uint16_t {
    AeroVertexFlag_None = 0u,
    AeroVertexFlag_GrayscaleSdfText = 1u << 0u,
    AeroVertexFlag_HasBorder = 1u << 1u,
    AeroVertexFlag_PremultipliedAlpha = 1u << 2u,
};

// 24-byte compact GPU vertex aligned with 32-bit float pipeline (0 fp64 conversion)
struct AeroVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t rgba = 0xFFFFFFFFu;
    std::uint16_t primType = static_cast<std::uint16_t>(AeroPrimType::SolidQuad);
    std::uint16_t flags = 0u;
};

static_assert(sizeof(AeroVertex) == 24, "AeroVertex must be exactly 24 bytes.");
static_assert(alignof(AeroVertex) == 4, "AeroVertex must be 4-byte aligned.");

inline constexpr std::size_t kCommandPageSizeBytes = 4096;
inline constexpr std::size_t kCommandPageHeaderBytes = 64;
inline constexpr std::size_t kVerticesPerCommandPage =
    (kCommandPageSizeBytes - kCommandPageHeaderBytes) / sizeof(AeroVertex); // 168 vertices (42 quads)

struct CommandPageHeader {
    std::uint32_t pageIndex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t nodeCount = 0;
    std::uint32_t dirtyRevision = 1;
    std::uint32_t firstNodeId = 0;
    std::uint32_t maxQuads = static_cast<std::uint32_t>(kVerticesPerCommandPage / 4);
    std::uint64_t dirtyQuadMask = 0;
    float clipX = 0.0f;
    float clipY = 0.0f;
    float clipW = 16384.0f;
    float clipH = 16384.0f;
    std::uint32_t flags = 0;
    std::uint32_t reserved[3]{0, 0, 0};
};

static_assert(sizeof(CommandPageHeader) == 64, "CommandPageHeader must be exactly 64 bytes.");

// Exactly 4KB (4096 bytes) paged GPU command/vertex block
struct alignas(64) CommandPage {
    CommandPageHeader header{};
    std::array<AeroVertex, kVerticesPerCommandPage> vertices{};
};

static_assert(sizeof(CommandPage) == kCommandPageSizeBytes, "CommandPage must be exactly 4096 bytes (4KB).");

struct GlyphUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float advanceX = 8.0f;
    bool valid = false;
};

struct RenderNodeDescriptor {
    std::uint32_t nodeId = 0;
    std::uint32_t pageIndex = 0;
    std::uint16_t vertexOffsetInPage = 0;
    std::uint16_t vertexCount = 4;
    AeroPrimType primType = AeroPrimType::SolidQuad;
    std::uint16_t encodedSdfParams = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float opacity = 1.0f;
    float cornerRadius = 0.0f;
    float borderThickness = 0.0f;
    // 2D affine matrix (m00, m01, m10, m11, tx, ty)
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    std::uint32_t baseRgba = 0xFFFFFFFFu;
    const UIElement* sourceElement = nullptr;
};

// Pack RGBA [0..1] floats into ABGR/RGBA 32-bit uint
[[nodiscard]] std::uint32_t PackColorRgba8(const Color& color, float opacityMultiplier = 1.0f) noexcept;

// Pack cornerRadius and borderThickness (in 0.5px steps, 0..127.5px) into 16-bit flags
[[nodiscard]] std::uint16_t PackSdfRoundedRectParams(float cornerRadius, float borderThickness) noexcept;
void UnpackSdfRoundedRectParams(
    std::uint16_t packed,
    float& outCornerRadius,
    float& outBorderThickness) noexcept;

// CPU reference evaluation matching the GPU UberShader SDF analytical rounded-rect anti-aliasing
[[nodiscard]] float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float cornerRadius,
    float aaFeatherWidth = 1.0f) noexcept;

class PagedRenderTree {
public:
    PagedRenderTree();

    void Clear() noexcept;

    // Build paged 4KB render tree from a laid-out UIElement hierarchy
    std::size_t BuildFromVisualTree(const UIElement* rootElement);

    // Allocate a standalone fast-path render node (e.g. ECS HUD health bar or damage counter)
    std::uint32_t AllocateRenderNode(
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        float cornerRadius = 0.0f,
        float borderThickness = 0.0f,
        const UIElement* owner = nullptr);

    // Allocate a multi-digit HUD numeric node backed by pre-cached glyph UVs (0 font shaping)
    std::uint32_t AllocateHudDigitsNode(
        float x,
        float y,
        float glyphWidth,
        float glyphHeight,
        std::uint32_t maxDigits,
        const Color& color);

    // Reserve a contiguous batch of N HUD bar nodes for O(1) SIMD/pointer-walk Track B updates
    std::uint32_t AllocateHudBarBatch(
        std::size_t count,
        float startX,
        float startY,
        float barWidth,
        float barHeight,
        float verticalSpacing,
        const Color& color,
        float cornerRadius = 4.0f);

    // Partial node updates: re-records ONLY the owning 4KB CommandPage and marks that page dirty
    bool UpdateNodeWidth(std::uint32_t nodeId, float newWidth) noexcept;
    bool UpdateNodeOpacity(std::uint32_t nodeId, float newOpacity) noexcept;
    bool UpdateNodeTransform2D(
        std::uint32_t nodeId,
        float m00,
        float m01,
        float m10,
        float m11,
        float tx,
        float ty) noexcept;
    bool UpdateHudDigitsText(std::uint32_t nodeId, std::string_view asciiDigits) noexcept;
    bool InvalidateNodePage(std::uint32_t nodeId) noexcept;
    bool UpdateDirtyElement(const UIElement* element) noexcept;

    // Track B (EcsColumnSimdTrack) direct zero-allocation batch update:
    // Reads contiguous float values (with byte stride & offset) and directly updates
    // PagedRenderTree node widths and 4KB page vertices, bypassing DP & LayoutEngine.
    std::size_t BatchUpdateWidthsFromRawColumn(
        std::uint32_t firstNodeId,
        std::size_t nodeCount,
        const void* rawColumnBase,
        std::size_t elementStrideBytes,
        std::size_t fieldByteOffset,
        float scaleFactor = 1.0f) noexcept;

    std::size_t BatchUpdateFromEcsTrackB(
        const DualTrackBindingEngine& bindingEngine,
        std::uint32_t firstNodeId = 0) noexcept;

    [[nodiscard]] std::size_t PageCount() const noexcept { return pages_.size(); }
    [[nodiscard]] const std::vector<CommandPage>& Pages() const noexcept { return pages_; }
    [[nodiscard]] const CommandPage& GetPage(std::size_t pageIndex) const noexcept {
        return pages_[pageIndex];
    }

    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] const RenderNodeDescriptor* GetNode(std::uint32_t nodeId) const noexcept {
        return (nodeId < nodes_.size()) ? &nodes_[nodeId] : nullptr;
    }

    // Dirty 4KB page tracking
    [[nodiscard]] bool IsPageDirty(std::size_t pageIndex) const noexcept;
    [[nodiscard]] std::size_t DirtyPageCount() const noexcept;
    [[nodiscard]] const std::vector<std::uint32_t>& DirtyPageIndices() const noexcept {
        return dirtyPageList_;
    }
    void ClearDirtyPages() noexcept;

    [[nodiscard]] const GlyphUvRect& LookupPreCachedGlyph(char asciiChar) const noexcept {
        const auto idx = static_cast<std::uint8_t>(asciiChar) & 0x7Fu;
        return preCachedGlyphs_[idx];
    }

private:
    void InitPreCachedDigitGlyphs() noexcept;
    void WriteQuadVertices(RenderNodeDescriptor& node) noexcept;
    void MarkPageDirty(std::uint32_t pageIndex, std::uint16_t vertexOffsetInPage) noexcept;
    void TraverseAndEmitElement(const UIElement* elem);

    std::vector<CommandPage> pages_;
    std::vector<RenderNodeDescriptor> nodes_;
    std::vector<std::uint8_t> pageDirtyFlags_;
    std::vector<std::uint32_t> dirtyPageList_;
    std::array<GlyphUvRect, 128> preCachedGlyphs_{};
};

} // namespace aero
