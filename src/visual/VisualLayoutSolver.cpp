#include "aero/visual/VisualTree.hpp"

#include <algorithm>
#include <cmath>

namespace aero {

namespace {

[[nodiscard]] float ResolveSizeOrFallback(float explicitSize, float available, float fallback) noexcept {
    if (std::isfinite(explicitSize) && explicitSize >= 0.0f) {
        return explicitSize;
    }
    if (std::isfinite(available) && available >= 0.0f) {
        return available;
    }
    return fallback;
}

void LayoutNodeRecursive(
    VisualWorld& world,
    VisualNodeId id,
    Rect availableRect) noexcept {
    VisualNode* node = world.GetNode(id);
    if (!node || node->layout.visibility == Visibility::Collapsed) {
        return;
    }

    const float marginH = node->layout.margin.left + node->layout.margin.right;
    const float marginV = node->layout.margin.top + node->layout.margin.bottom;

    const float targetW = std::clamp(
        ResolveSizeOrFallback(node->layout.width, availableRect.width - marginH, 0.0f),
        node->layout.minWidth,
        node->layout.maxWidth);
    const float targetH = std::clamp(
        ResolveSizeOrFallback(node->layout.height, availableRect.height - marginV, 0.0f),
        node->layout.minHeight,
        node->layout.maxHeight);

    node->layout.desiredSize = Size{targetW + marginH, targetH + marginV};
    node->layout.layoutRect = Rect{
        availableRect.x + node->layout.margin.left,
        availableRect.y + node->layout.margin.top,
        targetW,
        targetH};

    const Rect innerRect{
        node->layout.layoutRect.x + node->layout.padding.left,
        node->layout.layoutRect.y + node->layout.padding.top,
        std::max(0.0f, targetW - (node->layout.padding.left + node->layout.padding.right)),
        std::max(0.0f, targetH - (node->layout.padding.top + node->layout.padding.bottom))};

    const auto childrenCopy = node->children;
    for (VisualNodeId childId : childrenCopy) {
        LayoutNodeRecursive(world, childId, innerRect);
    }
}

} // namespace

void VisualWorld::UpdateLayout(float viewportWidth, float viewportHeight) {
    if (!rootId_.IsValid()) {
        return;
    }
    const float safeW = std::max(0.0f, viewportWidth);
    const float safeH = std::max(0.0f, viewportHeight);
    LayoutNodeRecursive(*this, rootId_, Rect{0.0f, 0.0f, safeW, safeH});
}

} // namespace aero
