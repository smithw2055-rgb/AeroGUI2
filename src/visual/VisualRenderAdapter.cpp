#include "aero/visual/VisualRenderAdapter.hpp"

#include <algorithm>

#include "aero/core/UIResourceStore.hpp"

namespace aero {

namespace {

[[nodiscard]] Color UnpackRgba32(std::uint32_t rgba, float opacityMultiplier = 1.0f) noexcept {
    const float r = static_cast<float>(rgba & 0xFFu) / 255.0f;
    const float g = static_cast<float>((rgba >> 8u) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((rgba >> 16u) & 0xFFu) / 255.0f;
    const float a = (static_cast<float>((rgba >> 24u) & 0xFFu) / 255.0f) * opacityMultiplier;
    return Color{r, g, b, std::clamp(a, 0.0f, 1.0f)};
}

std::size_t EmitVisualNodeRecursive(
    const VisualWorld& world,
    const DrawingCommandBuffer* drawBuffer,
    VisualNodeId id,
    float parentOpacity,
    PagedRenderTree& outTree) {
    const VisualNode* node = world.GetNode(id);
    if (!node || node->layout.visibility == Visibility::Collapsed) {
        return 0u;
    }

    std::size_t count = 0u;
    const float effOpacity = std::clamp(parentOpacity * node->layout.opacity, 0.0f, 1.0f);
    const Rect& rect = node->layout.layoutRect;

    if (node->layout.visibility == Visibility::Visible && rect.width > 0.0f && rect.height > 0.0f) {
        if ((node->box.bgRgba >> 24u) > 0u) {
            const Color bg = UnpackRgba32(node->box.bgRgba, effOpacity);
            (void)outTree.AllocateRoundedRectNode(
                rect.x,
                rect.y,
                rect.width,
                rect.height,
                bg,
                node->box.cornerRadius,
                node);
            ++count;
        }
        if (node->kind == VisualKind::Text && !node->text.text.empty()) {
            const Color fg = UnpackRgba32(node->text.fgRgba, effOpacity);
            (void)outTree.AllocateTextRunNode(
                rect.x,
                rect.y,
                node->text.fontSize,
                node->text.text,
                fg,
                effOpacity,
                0u,
                node,
                node->text.fontFamily,
                rect.width,
                node->text.wrap);
            ++count;
        }
        if (node->kind == VisualKind::CanvasDraw && drawBuffer != nullptr) {
            if (const DrawingCommandList* list = drawBuffer->FindCommands(id)) {
                for (const DrawCommand& cmd : list->commands) {
                    if (cmd.kind == DrawCommandKind::FillRect && (cmd.colorRgba >> 24u) > 0u) {
                        (void)outTree.AllocateRoundedRectNode(
                            rect.x + cmd.rect.x,
                            rect.y + cmd.rect.y,
                            cmd.rect.width,
                            cmd.rect.height,
                            UnpackRgba32(cmd.colorRgba, effOpacity),
                            CornerRadius{},
                            node);
                        ++count;
                    }
                }
            }
        }
    }

    for (VisualNodeId childId : node->children) {
        count += EmitVisualNodeRecursive(world, drawBuffer, childId, effOpacity, outTree);
    }
    return count;
}

} // namespace

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
    return EmitVisualNodeRecursive(world, drawBuffer, world.Root(), 1.0f, outTree);
}

std::size_t VisualRenderAdapter::SyncPagedRenderTree(
    VisualWorld& world,
    const DrawingCommandBuffer* drawBuffer,
    PagedRenderTree& outTree,
    UIResourceStore* resourceStore) const {
    const std::size_t count = BuildPagedRenderTree(world, drawBuffer, outTree, resourceStore);
    world.ClearDirtyFlags();
    return count;
}

} // namespace aero
