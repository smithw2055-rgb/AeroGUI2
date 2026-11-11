#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"
#include "aero/render/FontAtlas.hpp"

namespace aero {

enum class AeroPrimType : std::uint16_t {
    SolidQuad = 0,
    SdfRoundedRect = 1,
    SdfTextGlyph = 2,
    TexturedQuad = 3,
    MsdfVectorPath = 4,      // 4-channel MTSDF Vector Path (4-vertex Quad, 0 CPU triangulation)
    SdfRadialArc = 5,        // Analytical SDF Ring/Arc/Progressbar (startAngle, sweepAngle, innerRadius)
    DynamicMesh = 6,         // Deformable 2D triangle strip/indexed quad mesh inside 4KB CommandPage
    SdfEllipse = 7,          // Analytical SDF Ellipse / Circle fill & hollow stroke (P1-3)
    GradientRoundedRect = 8, // Multi-stop Linear/Radial Gradient RoundedRect & Ellipse (P1-3)
};

// High-bit SDF sub-mode stored in bits [3..4] of AeroVertex::primType when (primType & 0x7u) == SdfRoundedRect:
//   Mode 0: Compact uniform fast path (uniform CornerRadius + legacy single-quad borderThickness)
//   Mode 1: Hollow SDF border ring with uniform CornerRadius (uniform or 4-side BorderThickness)
//   Mode 2: Asymmetric 4-corner CornerRadius(topLeft, topRight, bottomRight, bottomLeft) pure fill
//   Mode 3: Asymmetric 4-corner CornerRadius(topLeft, topRight, bottomRight, bottomLeft) hollow SDF border ring
enum AeroSdfPrimMode : std::uint16_t {
    AeroSdfPrimMode_UniformFast = 0u,
    AeroSdfPrimMode_UniformHollowBorder = 1u,
    AeroSdfPrimMode_AsymmetricFill = 2u,
    AeroSdfPrimMode_AsymmetricHollowBorder = 3u,
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

enum CommandPageFlags : std::uint32_t {
    CommandPageFlag_None = 0u,
    CommandPageFlag_ScissorClipActive = 1u << 0u,
};

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
    // reserved[0] stores bound textureId (0 = default font/UI Skyline atlas, >0 = standalone texture handle)
    std::uint32_t reserved[3]{0, 0, 0};

    [[nodiscard]] constexpr std::uint32_t TextureId() const noexcept { return reserved[0]; }
    constexpr void SetTextureId(std::uint32_t texId) noexcept { reserved[0] = texId; }
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
    // UV rectangle for TexturedQuad / MsdfVectorPath (unused by SolidQuad/SdfRoundedRect)
    float uv0X = 0.0f;
    float uv0Y = 0.0f;
    float uv1X = 1.0f;
    float uv1Y = 1.0f;
    // 2D affine matrix (m00, m01, m10, m11, tx, ty) & cached ancestor state
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float baseTx = 0.0f;
    float baseTy = 0.0f;
    float inheritedOpacity = 1.0f;
    std::uint32_t baseRgba = 0xFFFFFFFFu;
    std::uint16_t encodedPrimExtra = 0; // High bits [3..15] OR'd into AeroVertex::primType for 4-corner/border SDF or gradientId
    bool isHollowBorder = false;        // True when node renders an independent hollow BorderBrush/Stroke ring
    bool isEllipseShape = false;        // True when GradientRoundedRect clips to analytical SDF Ellipse
    std::uint32_t textureId = 0u;
    std::uint32_t gradientBrushId = 0u; // Non-zero GradientBrushRegistry ID for AeroPrimType::GradientRoundedRect
    std::uint64_t mtsdfPathHash = 0;
    const UIElement* sourceElement = nullptr;
    CornerRadius cornerRadii{};
    Thickness borderThicknesses{};
    // P1-5: 3D perspective planar homography & soft SDF shadow/blur parameters
    bool hasPerspective3D = false;
    float softFeatherPx = 0.0f;
    float sdfPadPx = 0.0f;
    Matrix4x4 worldMatrix3D{};
};

struct BlurLayerSpan {
    std::uint32_t firstNodeId = 0u;
    std::uint32_t endNodeId = 0u;
    float radiusPx = 0.0f;
    Rect clipRect{0.0f, 0.0f, 16384.0f, 16384.0f};
    bool clipActive = false;
};

// Pack RGBA [0..1] floats into ABGR/RGBA 32-bit uint
[[nodiscard]] std::uint32_t PackColorRgba8(const Color& color, float opacityMultiplier = 1.0f) noexcept;

// Pack cornerRadius and borderThickness (in 0.5px steps, 0..127.5px) into 16-bit flags (uniform fast path)
[[nodiscard]] std::uint16_t PackSdfRoundedRectParams(float cornerRadius, float borderThickness) noexcept;
void UnpackSdfRoundedRectParams(
    std::uint16_t packed,
    float& outCornerRadius,
    float& outBorderThickness) noexcept;

[[nodiscard]] constexpr bool IsUniformCornerRadius(const CornerRadius& cr) noexcept {
    return cr.topLeft == cr.topRight &&
           cr.topRight == cr.bottomRight &&
           cr.bottomRight == cr.bottomLeft;
}

[[nodiscard]] constexpr bool IsUniformThickness(const Thickness& bt) noexcept {
    return bt.left == bt.top &&
           bt.top == bt.right &&
           bt.right == bt.bottom;
}

// Pack 4-component CornerRadius + 4-side BorderThickness + hollow border mode into AeroVertex::primType[3..15] & flags[0..15]
void PackSdfRoundedRectVertexParams(
    const CornerRadius& cornerRadii,
    const Thickness& borderThickness,
    bool hollowBorder,
    std::uint16_t& outPrimExtra,
    std::uint16_t& outFlags) noexcept;

void UnpackSdfRoundedRectVertexParams(
    std::uint16_t vertexPrimType,
    std::uint16_t vertexFlags,
    CornerRadius& outCornerRadii,
    Thickness& outBorderThickness,
    bool& outHollowBorder) noexcept;

// P1-3: Analytical SDF Ellipse / Circle vertex packing & CPU coverage reference
[[nodiscard]] std::uint16_t PackSdfEllipseParams(
    float strokeThickness,
    bool hollowStroke) noexcept;
void UnpackSdfEllipseParams(
    std::uint16_t packedFlags,
    float& outStrokeThickness,
    bool& outHollowStroke) noexcept;
[[nodiscard]] float EvaluateSdfEllipseCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float strokeThickness = 0.0f,
    bool hollowStroke = false,
    float aaFeatherWidth = 1.0f) noexcept;

// P1-3: GradientRoundedRect vertex packing (12-bit brushId in primType[4..15] + cornerRadius & ellipse bit in flags)
void PackGradientRoundedRectVertexParams(
    std::uint32_t gradientBrushId,
    float cornerRadius,
    bool isEllipseShape,
    std::uint16_t& outPrimExtra,
    std::uint16_t& outFlags) noexcept;
void UnpackGradientRoundedRectVertexParams(
    std::uint16_t vertexPrimWord,
    std::uint16_t vertexFlags,
    std::uint32_t& outGradientBrushId,
    float& outCornerRadius,
    bool& outIsEllipseShape) noexcept;

// Decode effective AeroPrimType from 16-bit AeroVertex::primType preserving P0-1 3-bit SdfRoundedRect extra modes
[[nodiscard]] constexpr AeroPrimType DecodeVertexPrimType(std::uint16_t rawVertexPrim) noexcept {
    const std::uint16_t low3 = static_cast<std::uint16_t>(rawVertexPrim & 0x0007u);
    if (low3 == 0u && (rawVertexPrim & 0x0008u) != 0u) {
        return AeroPrimType::GradientRoundedRect;
    }
    return static_cast<AeroPrimType>(low3);
}

// Pack MTSDF normalized UV range (in 1/4096 UV units), optional stroke width (bits 8..14), and strokeOnly bit (bit 15)
[[nodiscard]] std::uint16_t PackMtsdfParams(
    float pxRange,
    std::uint16_t atlasSize,
    float strokeThickness = 0.0f,
    bool strokeOnly = false) noexcept;
void UnpackMtsdfParams(
    std::uint16_t packed,
    float& outUvRange,
    float& outStrokeThickness) noexcept;
[[nodiscard]] constexpr bool IsMtsdfStrokeOnly(std::uint16_t packed) noexcept {
    return (packed & 0x8000u) != 0u;
}

// CPU reference evaluation matching the GPU UberShader SDF analytical rounded-rect anti-aliasing (uniform fast path)
[[nodiscard]] float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float cornerRadius,
    float aaFeatherWidth = 1.0f) noexcept;

// CPU reference evaluation matching GPU sdRoundedBox4(p, b, vec4(tr, br, tl, bl)) for asymmetric 4-corner CornerRadius
[[nodiscard]] float EvaluateSdfRoundedRectCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    const CornerRadius& cornerRadii,
    float aaFeatherWidth = 1.0f) noexcept;

// CPU reference evaluation for hollow SDF border rings with asymmetric 4-corner CornerRadius and 4-side BorderThickness
[[nodiscard]] float EvaluateSdfRoundedBorderCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    const CornerRadius& cornerRadii,
    const Thickness& borderThickness,
    float aaFeatherWidth = 1.0f) noexcept;

// CPU reference evaluation matching the GPU UberShader 4-channel MTSDF median(R,G,B) + fwidth AA
[[nodiscard]] float EvaluateMtsdfCoverage(
    const AxbMtsdfPathEntry& entry,
    float normX01,
    float normY01,
    float renderedWidthPx,
    float renderedHeightPx,
    bool singleChannelTrueSdfOnly = false) noexcept;

// Pack startAngleDeg [0..360) (6 bits), sweepAngleDeg [0..360] (6 bits), innerRadiusRatio01 [0..0.9375] (4 bits)
[[nodiscard]] std::uint16_t PackSdfRadialArcParams(
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01) noexcept;
void UnpackSdfRadialArcParams(
    std::uint16_t packed,
    float& outStartAngleDeg,
    float& outSweepAngleDeg,
    float& outInnerRadiusRatio01) noexcept;

// CPU reference evaluation matching the GPU UberShader SDF analytical radial ring/arc anti-aliasing
[[nodiscard]] float EvaluateSdfRadialArcCoverage(
    float sampleLocalX,
    float sampleLocalY,
    float halfWidth,
    float halfHeight,
    float startAngleDeg,
    float sweepAngleDeg,
    float innerRadiusRatio01,
    float aaFeatherWidth = 1.0f) noexcept;

class PagedRenderTree {
public:
    PagedRenderTree();

    void Clear() noexcept;

    [[nodiscard]] std::uint32_t TreeId() const noexcept { return treeId_; }

    // Full build of paged 4KB render tree from a laid-out UIElement hierarchy
    std::size_t BuildFromVisualTree(const UIElement* rootElement);

    // O(dirty_nodes) zero-allocation incremental synchronization from UIElement hierarchy:
    // Walks ONLY branches carrying (RenderDirty | SubtreeRenderDirty) and updates bound render
    // node spans in O(1) via UIElement::RenderNodeId() without rebuilding clean pages.
    std::size_t SyncVisualTree(const UIElement* rootElement);

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

    // Allocate a filled rounded-rect node supporting asymmetric 4-component CornerRadius(tl, tr, br, bl)
    std::uint32_t AllocateRoundedRectNode(
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        const CornerRadius& cornerRadii,
        const UIElement* owner = nullptr);

    // Allocate an analytical hollow SDF border ring node supporting independent BorderBrush, 4-corner radii & 4-side thickness
    std::uint32_t AllocateBorderRingNode(
        float x,
        float y,
        float width,
        float height,
        const Color& borderColor,
        const CornerRadius& cornerRadii,
        const Thickness& borderThickness,
        const UIElement* owner = nullptr);

    // P1-3: Allocate an analytical SDF Ellipse / Circle node (fill or hollow stroke ring)
    std::uint32_t AllocateEllipseNode(
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        float strokeThickness = 0.0f,
        bool hollowStroke = false,
        float opacity = 1.0f,
        const UIElement* owner = nullptr);

    // P1-3: Allocate a multi-stop Linear/Radial Gradient RoundedRect (or Ellipse) node
    std::uint32_t AllocateGradientRoundedRectNode(
        float x,
        float y,
        float width,
        float height,
        std::uint32_t gradientBrushId,
        const CornerRadius& cornerRadii = CornerRadius{},
        bool isEllipseShape = false,
        const Color& tintModulator = Color{1.0f, 1.0f, 1.0f, 1.0f},
        float opacity = 1.0f,
        const UIElement* owner = nullptr);

    // Allocate a multi-digit HUD numeric node backed by pre-cached glyph UVs (0 font shaping)
    std::uint32_t AllocateHudDigitsNode(
        float x,
        float y,
        float glyphWidth,
        float glyphHeight,
        std::uint32_t maxDigits,
        const Color& color);

    // Allocate a general UTF-8 SdfTextGlyph run node (supports TTF/SDF FontAtlas, wrapping & CJK shards, up to 42 glyphs/page)
    std::uint32_t AllocateTextRunNode(
        float x,
        float y,
        float fontSize,
        std::string_view utf8Text,
        const Color& color,
        float opacity = 1.0f,
        std::uint32_t reservedGlyphCapacity = 0,
        const UIElement* owner = nullptr,
        std::string_view fontFamily = {},
        float maxWidth = 1000000.0f,
        TextWrapping wrapMode = TextWrapping::NoWrap);

    // Allocate a 4-vertex 4-channel MTSDF vector path quad (0 CPU triangulation)
    std::uint32_t AllocateMtsdfPathNode(
        float x,
        float y,
        float width,
        float height,
        const AxbMtsdfPathEntry& mtsdfEntry,
        const Color& fillColor,
        float strokeThickness = 0.0f,
        float opacity = 1.0f,
        const UIElement* owner = nullptr);

    // Allocate a 4-vertex textured image quad (default textureId = 0 shared UI atlas)
    std::uint32_t AllocateTexturedNode(
        float x,
        float y,
        float width,
        float height,
        float u0,
        float v0,
        float u1,
        float v1,
        const Color& tintColor,
        float opacity = 1.0f,
        const UIElement* owner = nullptr);

    // Allocate a 4-vertex textured image quad bound to an explicit textureId slot
    std::uint32_t AllocateTexturedNode(
        float x,
        float y,
        float width,
        float height,
        float u0,
        float v0,
        float u1,
        float v1,
        const Color& tintColor,
        float opacity,
        const UIElement* owner,
        std::uint32_t textureId);

    // Allocate a 4-vertex analytical SDF radial arc / ring / gauge quad (0 CPU tessellation)
    std::uint32_t AllocateRadialArcNode(
        float x,
        float y,
        float width,
        float height,
        float startAngleDeg,
        float sweepAngleDeg,
        float innerRadiusRatio01,
        const Color& color,
        float opacity = 1.0f,
        const UIElement* owner = nullptr);

    bool UpdateRadialArcNode(
        std::uint32_t nodeId,
        float startAngleDeg,
        float sweepAngleDeg,
        float innerRadiusRatio01) noexcept;

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
    bool UpdateTextRunNode(
        std::uint32_t nodeId,
        float x,
        float y,
        float fontSize,
        std::string_view utf8Text,
        const Color& color,
        float opacity,
        std::string_view fontFamily = {},
        float maxWidth = 1000000.0f,
        TextWrapping wrapMode = TextWrapping::NoWrap) noexcept;
    bool InvalidateNodePage(std::uint32_t nodeId) noexcept;
    bool RefreshTextNode(std::uint32_t nodeId) noexcept;
    std::size_t RefreshGlyphRange(std::uint32_t rangeStart, std::uint32_t rangeEnd) noexcept;
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
    [[nodiscard]] const std::vector<BlurLayerSpan>& BlurLayerSpans() const noexcept {
        return blurLayerSpans_;
    }

    // Unified 1024x1024 UI atlas MTSDF lower-half region [y: 512..959] (1024 x 448 RGBA8)
    [[nodiscard]] std::uint32_t MtsdfAtlasOwnerId() const noexcept { return mtsdfOwnerId_; }
    [[nodiscard]] std::uint64_t MtsdfAtlasRevision() const noexcept { return mtsdfAtlasRevision_; }
    [[nodiscard]] std::uint32_t MtsdfAtlasPopulatedRows() const noexcept { return mtsdfPopulatedRows_; }
    [[nodiscard]] const std::vector<std::uint8_t>& MtsdfAtlasRgba8() const noexcept {
        return mtsdfAtlasRgba8_;
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
    [[nodiscard]] GlyphUvRect LookupCodePointGlyph(std::uint32_t codePoint) const noexcept;
    [[nodiscard]] GlyphUvRect LookupFontGlyph(
        std::string_view fontFamily,
        std::uint32_t codePoint,
        float fontSize = 14.0f) const noexcept;

private:
    struct RenderTraversalState {
        Rect clipRect{0.0f, 0.0f, 16384.0f, 16384.0f};
        bool clipActive = false;
        float inheritedOpacity = 1.0f;
        float m00 = 1.0f;
        float m01 = 0.0f;
        float m10 = 0.0f;
        float m11 = 1.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        bool hasPerspective3D = false;
        Matrix4x4 world3D{};
    };

    void InitPreCachedDigitGlyphs() noexcept;
    void RegisterMtsdfInTreeAtlas(const AxbMtsdfPathEntry& entry);
    std::uint32_t EnsurePageForAllocation(
        std::uint32_t neededVertices,
        const Rect& clipRect,
        bool clipActive,
        std::uint32_t textureId = 0u);
    void WriteQuadVertices(RenderNodeDescriptor& node) noexcept;
    void ZeroOutNodeVertices(RenderNodeDescriptor& node) noexcept;
    void MarkPageDirty(std::uint32_t pageIndex, std::uint16_t vertexOffsetInPage) noexcept;
    bool WriteLayoutGlyphSlice(
        std::uint32_t nodeId,
        float x,
        float y,
        float fontSize,
        const TextLayoutResult& layout,
        std::size_t& ioGlyphCursor,
        const Color& color,
        float opacity) noexcept;
    std::uint16_t EmitTextRunNodes(
        float x,
        float y,
        float fontSize,
        std::string_view utf8Text,
        const Color& color,
        float opacity,
        const RenderTraversalState& curState,
        const UIElement* owner,
        std::string_view fontFamily = {},
        float maxWidth = 1000000.0f,
        TextWrapping wrapMode = TextWrapping::NoWrap);
    bool UpdateElementInPlace(
        const UIElement* element,
        const RenderTraversalState& curState);
    void CollapseSubtreeRenderNodesRecursive(
        const UIElement* elem,
        std::size_t& ioUpdatedNodes) noexcept;
    struct DeferredComboPopup {
        const class ComboBox* combo = nullptr;
        RenderTraversalState state{};
    };

    [[nodiscard]] bool SubtreeContainsActiveComboPopup(const UIElement* elem) const noexcept;
    void EmitDeferredComboPopups();
    void TraverseAndEmitElement(const UIElement* elem, const RenderTraversalState& parentState);
    bool SyncDirtySubtreeRecursive(
        const UIElement* elem,
        const RenderTraversalState& parentState,
        bool forceSubtreeSync,
        std::size_t& ioUpdatedNodes,
        bool& outNeedsFullRebuild);

    std::uint32_t treeId_ = 1u;
    const UIElement* boundRootElement_ = nullptr;
    Rect activeClipRect_{0.0f, 0.0f, 16384.0f, 16384.0f};
    bool activeClipEnabled_ = false;
    std::vector<CommandPage> pages_;
    std::vector<RenderNodeDescriptor> nodes_;
    std::vector<std::uint8_t> pageDirtyFlags_;
    std::vector<std::uint32_t> dirtyPageList_;
    std::vector<DeferredComboPopup> pendingComboPopups_;
    struct StandaloneTextRunRecord {
        float x = 0.0f;
        float y = 0.0f;
        float fontSize = 14.0f;
        std::string utf8Text;
        std::string fontFamily;
        Color color{1.0f, 1.0f, 1.0f, 1.0f};
        float opacity = 1.0f;
        float maxWidth = 1000000.0f;
        TextWrapping wrapMode = TextWrapping::NoWrap;
    };

    std::vector<BlurLayerSpan> blurLayerSpans_;
    std::vector<const UIElement*> activeComboPopupOwners_;
    std::unordered_map<std::uint32_t, StandaloneTextRunRecord> standaloneTextRuns_;
    std::array<GlyphUvRect, 128> preCachedGlyphs_{};
    std::uint32_t mtsdfOwnerId_ = 0u;
    std::uint64_t mtsdfAtlasRevision_ = 0u;
    std::uint32_t mtsdfPopulatedRows_ = 0u;
    std::vector<std::uint8_t> mtsdfAtlasRgba8_;
    std::unordered_set<std::uint64_t> mtsdfRegisteredHashes_;
};

} // namespace aero
