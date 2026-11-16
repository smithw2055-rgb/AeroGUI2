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
};

} // namespace aero
