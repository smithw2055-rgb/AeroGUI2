#pragma once

#include <cstddef>

#include "aero/render/PagedRenderTree.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualTree.hpp"

namespace aero {

class UIResourceStore;

class VisualRenderAdapter final {
public:
    VisualRenderAdapter() = default;
    ~VisualRenderAdapter() = default;

    std::size_t BuildPagedRenderTree(
        const VisualWorld& world,
        const DrawingCommandBuffer* drawBuffer,
        PagedRenderTree& outTree,
        UIResourceStore* resourceStore = nullptr) const;

    std::size_t SyncPagedRenderTree(
        VisualWorld& world,
        const DrawingCommandBuffer* drawBuffer,
        PagedRenderTree& outTree,
        UIResourceStore* resourceStore = nullptr) const;

private:
    static void AttachNodeGradientBrush(
        PagedRenderTree& tree,
        std::uint32_t nid,
        std::int32_t gradId) noexcept;

    static void ApplyNodeTransform(
        PagedRenderTree& tree,
        std::uint32_t beginNid,
        std::uint32_t endNid,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend) noexcept;

    static std::size_t EmitLineSegment(
        PagedRenderTree& tree,
        Point a,
        Point b,
        float thickness,
        const Color& color,
        const void* owner,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitSvgPathPrimitive(
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
        float fillOpacity = 1.0f,
        float strokeOpacity = 1.0f);

    static std::size_t EmitBoxVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitTextVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitImageVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitVectorVisual(
        PagedRenderTree& tree,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitCanvasDrawVisual(
        PagedRenderTree& tree,
        const DrawingCommandBuffer* drawBuffer,
        VisualNodeId id,
        const VisualNode& node,
        const Rect& rect,
        float effOpacity,
        const Matrix2D& world2D,
        const Matrix4x4& world3D,
        bool has3D,
        BlendingMode blend);

    static std::size_t EmitDropShadowLeafSilhouette(
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
        bool wrapWithBlurSpan);

    static std::size_t EmitDropShadowSubtreeRecursive(
        const VisualWorld& world,
        VisualNodeId id,
        float parentOpacity,
        PagedRenderTree& outTree,
        const VisualEffectStyle& dropShadow,
        float dx,
        float dy,
        float blurR);

    static std::size_t EmitNodeRecursive(
        VisualWorld* mutableWorld,
        const VisualWorld& world,
        const DrawingCommandBuffer* drawBuffer,
        VisualNodeId id,
        float parentOpacity,
        PagedRenderTree& outTree,
        const VisualEffectStyle* inheritedDropShadow = nullptr);
};

} // namespace aero
