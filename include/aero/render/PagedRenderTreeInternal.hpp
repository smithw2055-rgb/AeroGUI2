#pragma once

#include "aero/render/PagedRenderTree.hpp"

namespace aero {

struct MtsdfMappedQuad {
    Rect quadRect{};
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct SurfaceFillBorderPlan {
    Rect fillRect{};
    CornerRadius fillRadii{};
    Color fillColor{0.0f, 0.0f, 0.0f, 0.0f};
    bool hasVisibleFill = false;

    Rect borderRect{};
    CornerRadius borderRadii{};
    Thickness borderThickness{};
    Color borderColor{0.0f, 0.0f, 0.0f, 0.0f};
    bool reserveBorderNode = false;
    bool hasVisibleBorder = false;
};

[[nodiscard]] std::string ResolveElementFontFamily(const UIElement* elem);
[[nodiscard]] std::size_t CountLayoutVisibleGlyphs(const TextLayoutResult& layout) noexcept;
[[nodiscard]] Matrix2D MatrixFromState(
    float m00, float m01, float m10, float m11, float tx, float ty) noexcept;
[[nodiscard]] MtsdfMappedQuad ComputeMtsdfMappedPlacement(
    const Rect& fitted,
    const AxbMtsdfPathEntry& entry,
    float strokeThickness) noexcept;
void ApplyTextAlignmentToLayout(
    TextLayoutResult& layout,
    TextAlignment align,
    float alignBoxWidth) noexcept;
[[nodiscard]] Rect ComputeStretchedDestRect(
    const Rect& box,
    float naturalW,
    float naturalH,
    Stretch stretch) noexcept;
[[nodiscard]] Color ModulateInteractiveSurfaceColor(Color base, const UIElement* elem) noexcept;
[[nodiscard]] CornerRadius SanitizeCornerRadius(const CornerRadius& r) noexcept;
[[nodiscard]] Thickness SanitizeThickness(const Thickness& t) noexcept;
[[nodiscard]] bool HasPositiveThickness(const Thickness& t) noexcept;
[[nodiscard]] std::uint32_t ExtractGradBrushId(const UnifiedValue& v) noexcept;
[[nodiscard]] std::string ExtractContainerImageBrushUri(
    const UIElement* elem,
    Stretch* outStretch = nullptr);
[[nodiscard]] SurfaceFillBorderPlan BuildSurfaceFillBorderPlan(
    const Rect& outerRect,
    const CornerRadius& rawRadii,
    bool hasVisibleFill,
    const Color& fillColor,
    bool reserveBorderNode,
    const Color& borderColor,
    const Thickness& rawBorderThickness) noexcept;
[[nodiscard]] SurfaceFillBorderPlan ComputeContainerFillBorderPlan(
    const UIElement* elem,
    const Rect& outerRect) noexcept;
void ConfigureRoundedRectDescriptor(
    RenderNodeDescriptor& desc,
    const CornerRadius& rawRadii,
    const Thickness& rawBorder,
    bool hollowBorder) noexcept;
void ParseRichTextToPlainAndStyles(
    std::string_view rawBbCode,
    const Color& baseColor,
    const UIElement* owner,
    std::string& outPlainUtf8,
    std::vector<RichGlyphStyle>& outCodePointStyles);

struct ControlRenderEmitter {
    [[nodiscard]] static std::vector<RenderNodeDescriptor>& Nodes(PagedRenderTree& tree) noexcept {
        return tree.nodes_;
    }
    [[nodiscard]] static const std::vector<RenderNodeDescriptor>& Nodes(const PagedRenderTree& tree) noexcept {
        return tree.nodes_;
    }
    [[nodiscard]] static std::vector<CommandPage>& Pages(PagedRenderTree& tree) noexcept {
        return tree.pages_;
    }
    [[nodiscard]] static std::vector<const UIElement*>& ActiveComboPopupOwners(PagedRenderTree& tree) noexcept {
        return tree.activeComboPopupOwners_;
    }
    static void QueueDeferredComboPopup(
        PagedRenderTree& tree,
        const UIElement* combo,
        const PagedRenderTraversalState& state) {
        tree.pendingComboPopups_.push_back(PagedRenderTree::DeferredComboPopup{combo, state});
    }
    static void SyncQuadTransform(
        RenderNodeDescriptor& nd,
        const PagedRenderTraversalState& curState) noexcept {
        nd.m00 = curState.m00;
        nd.m01 = curState.m01;
        nd.m10 = curState.m10;
        nd.m11 = curState.m11;
        nd.baseTx = curState.tx;
        nd.baseTy = curState.ty;
        nd.tx = curState.tx + nd.x * (curState.m00 - 1.0f) + nd.y * curState.m01;
        nd.ty = curState.ty + nd.x * curState.m10 + nd.y * (curState.m11 - 1.0f);
        nd.inheritedOpacity = curState.inheritedOpacity;
        nd.hasPerspective3D = curState.hasPerspective3D;
        nd.worldMatrix3D = curState.world3D;
    }
    static void ApplyNodeTransform(
        PagedRenderTree& tree,
        std::uint32_t nodeId,
        const PagedRenderTraversalState& curState) noexcept {
        RenderNodeDescriptor& nd = tree.nodes_[nodeId];
        SyncQuadTransform(nd, curState);
        nd.blendMode = curState.blendMode;
        if (nd.primType != AeroPrimType::SdfTextGlyph) {
            tree.WriteQuadVertices(nd);
        }
    }
    static void RegisterMtsdfInTreeAtlas(
        PagedRenderTree& tree,
        const AxbMtsdfPathEntry& entry) {
        tree.RegisterMtsdfInTreeAtlas(entry);
    }
    static void MarkPageDirty(
        PagedRenderTree& tree,
        std::uint32_t pageIndex,
        std::uint16_t vertexOffsetInPage) noexcept {
        tree.MarkPageDirty(pageIndex, vertexOffsetInPage);
    }
    static void WriteQuadVertices(
        PagedRenderTree& tree,
        RenderNodeDescriptor& node) noexcept {
        tree.WriteQuadVertices(node);
    }
    static bool WriteLayoutGlyphSlice(
        PagedRenderTree& tree,
        std::uint32_t nodeId,
        float x,
        float y,
        float fontSize,
        const TextLayoutResult& layout,
        std::size_t& ioGlyphCursor,
        const Color& color,
        float opacity,
        const std::vector<RichGlyphStyle>* optRichStyles = nullptr) noexcept {
        return tree.WriteLayoutGlyphSlice(
            nodeId, x, y, fontSize, layout, ioGlyphCursor, color, opacity, optRichStyles);
    }
    static std::uint16_t EmitTextRunNodes(
        PagedRenderTree& tree,
        float x,
        float y,
        float fontSize,
        std::string_view utf8Text,
        const Color& color,
        float opacity,
        const PagedRenderTraversalState& curState,
        const UIElement* owner,
        std::string_view fontFamily = {},
        float maxWidth = 1000000.0f,
        TextWrapping wrapMode = TextWrapping::NoWrap,
        TextAlignment textAlign = TextAlignment::Left,
        float alignBoxWidth = 0.0f,
        const std::vector<RichGlyphStyle>* optRichStyles = nullptr) {
        return tree.EmitTextRunNodes(
            x, y, fontSize, utf8Text, color, opacity, curState, owner,
            fontFamily, maxWidth, wrapMode, textAlign, alignBoxWidth, optRichStyles);
    }
    static std::uint32_t AllocateRenderNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        float cornerRadius = 0.0f,
        float borderThickness = 0.0f,
        const UIElement* owner = nullptr) {
        return tree.AllocateRenderNode(
            x, y, width, height, color, cornerRadius, borderThickness, owner);
    }
    static std::uint32_t AllocateRoundedRectNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        const CornerRadius& cornerRadii,
        const UIElement* owner = nullptr) {
        return tree.AllocateRoundedRectNode(
            x, y, width, height, color, cornerRadii, owner);
    }
    static std::uint32_t AllocateBorderRingNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        const Color& borderColor,
        const CornerRadius& cornerRadii,
        const Thickness& borderThickness,
        const UIElement* owner = nullptr) {
        return tree.AllocateBorderRingNode(
            x, y, width, height, borderColor, cornerRadii, borderThickness, owner);
    }
    static std::uint32_t AllocateEllipseNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        const Color& color,
        float strokeThickness = 0.0f,
        bool hollowStroke = false,
        float opacity = 1.0f,
        const UIElement* owner = nullptr) {
        return tree.AllocateEllipseNode(
            x, y, width, height, color, strokeThickness, hollowStroke, opacity, owner);
    }
    static std::uint32_t AllocateGradientRoundedRectNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        std::uint32_t gradientBrushId,
        const CornerRadius& cornerRadii = CornerRadius{},
        bool isEllipseShape = false,
        const Color& tintModulator = Color{1.0f, 1.0f, 1.0f, 1.0f},
        float opacity = 1.0f,
        const UIElement* owner = nullptr) {
        return tree.AllocateGradientRoundedRectNode(
            x, y, width, height, gradientBrushId, cornerRadii, isEllipseShape,
            tintModulator, opacity, owner);
    }
    static std::uint32_t AllocateRadialArcNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        float startAngleDeg,
        float sweepAngleDeg,
        float innerRadiusRatio01,
        const Color& color,
        float opacity = 1.0f,
        const UIElement* owner = nullptr) {
        return tree.AllocateRadialArcNode(
            x, y, width, height, startAngleDeg, sweepAngleDeg, innerRadiusRatio01,
            color, opacity, owner);
    }
    static std::uint32_t AllocateMtsdfPathNode(
        PagedRenderTree& tree,
        float x,
        float y,
        float width,
        float height,
        const AxbMtsdfPathEntry& mtsdfEntry,
        const Color& fillColor,
        float strokeThickness = 0.0f,
        float opacity = 1.0f,
        const UIElement* owner = nullptr) {
        return tree.AllocateMtsdfPathNode(
            x, y, width, height, mtsdfEntry, fillColor, strokeThickness, opacity, owner);
    }
    static std::uint32_t AllocateTexturedNode(
        PagedRenderTree& tree,
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
        const UIElement* owner = nullptr) {
        return tree.AllocateTexturedNode(
            x, y, width, height, u0, v0, u1, v1, tintColor, opacity, owner);
    }
    static std::uint32_t AllocateTexturedNode(
        PagedRenderTree& tree,
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
        std::uint32_t textureId) {
        return tree.AllocateTexturedNode(
            x, y, width, height, u0, v0, u1, v1, tintColor, opacity, owner, textureId);
    }
    static std::uint16_t EmitCapsuleLine(
        PagedRenderTree& tree,
        float x1,
        float y1,
        float x2,
        float y2,
        float strokeW,
        const Color& strokeCol,
        float effOpacity,
        bool emitEvenIfInvisible,
        bool roundCapExtend,
        const PagedRenderTraversalState& curState,
        const UIElement* owner);
    static std::uint16_t EmitTrimmedPathSubpaths(
        PagedRenderTree& tree,
        std::string_view pathStr,
        const Rect& box,
        Stretch stretch,
        float strokeW,
        const Color& strokeCol,
        std::uint32_t strokeGradId,
        float effOpacity,
        const PagedRenderTraversalState& curState,
        const UIElement* owner,
        float trimStart,
        float trimEnd);
    static void EmitGenericContainerPrimitives(
        PagedRenderTree& tree,
        const UIElement* elem,
        const PagedRenderTraversalState& curState,
        Rect r,
        float w,
        float h,
        float effOpacity,
        std::uint16_t& ioEmittedCount);
};

} // namespace aero
