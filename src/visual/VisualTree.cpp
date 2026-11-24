#include "aero/visual/VisualTree.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>
#include <realscript/semantic/Semantic.h>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"

namespace aero {

namespace {

std::atomic<std::uint64_t> gNextVisualWorldId{1u};

[[nodiscard]] bool IsFinitePositive(float v) noexcept {
    return std::isfinite(v) && v >= 0.0f;
}

} // namespace

VisualWorld::VisualWorld(
    NativeHandleRegistry* handleRegistry,
    UIResourceStore* resourceStore,
    ManagedHeap* managedHeap)
    : visualNodeTypeId_(rs_sem::stableTypeId(kVisualNodeTypeName))
    , handleRegistry_(handleRegistry ? handleRegistry : &ownedFallbackRegistry_)
    , resourceStore_(resourceStore)
    , managedHeap_(managedHeap)
    , worldInstanceId_(gNextVisualWorldId.fetch_add(1u, std::memory_order_relaxed)) {
    measureDepthBuckets_.resize(64u);
    arrangeDepthBuckets_.resize(64u);
    transformDepthBuckets_.resize(64u);
    rs_rt::registerExternalValuePoolRootMarker(this, [this]() {
        MarkScriptRootsInPools();
    });
}

VisualWorld::~VisualWorld() {
    rs_rt::unregisterExternalValuePoolRootMarker(this);
    Clear();
}

void VisualWorld::BindRuntime(
    NativeHandleRegistry* handleRegistry,
    ManagedHeap* managedHeap) noexcept {
    if (handleRegistry && handleRegistry != handleRegistry_) {
        for (auto& node : nodes_) {
            if (!node.alive) {
                continue;
            }
            if (node.nativeHandle.valid()) {
                (void)handleRegistry_->release(node.nativeHandle);
            }
            node.nativeHandle = handleRegistry->createBorrowed(visualNodeTypeId_, &node, false);
        }
        handleRegistry_ = handleRegistry;
    }
    managedHeap_ = managedHeap;
    RefreshScriptOwnerPins();
}

UIResourceStore* VisualWorld::ResourceStore() noexcept {
    return resourceStore_ ? resourceStore_ : &UIResourceStore::Default();
}

const UIResourceStore* VisualWorld::ResourceStore() const noexcept {
    return resourceStore_ ? resourceStore_ : &UIResourceStore::Default();
}

NativeHandleRegistry& VisualWorld::HandleRegistry() noexcept {
    return *handleRegistry_;
}

const NativeHandleRegistry& VisualWorld::HandleRegistry() const noexcept {
    return *handleRegistry_;
}

VisualNodeId VisualWorld::CreateNode(VisualKind kind) {
    std::uint32_t idx = 0u;
    if (!freeIndices_.empty()) {
        idx = freeIndices_.back();
        freeIndices_.pop_back();
        VisualNode& slot = nodes_[idx];
        const std::uint32_t nextGen = slot.id.generation + 1u;
        slot = VisualNode{};
        slot.id.index = idx;
        slot.id.generation = (nextGen == 0u) ? 1u : nextGen;
    } else {
        idx = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
        nodes_.back().id.index = idx;
        nodes_.back().id.generation = 1u;
    }

    VisualNode& node = nodes_[idx];
    node.kind = kind;
    node.alive = true;
    node.dirtyFlags = VisualDirty_All;
    node.nativeHandle = handleRegistry_->createBorrowed(visualNodeTypeId_, &node, false);
    ++liveCount_;
    structureDirty_ = true;
    layoutOrTransformDirty_ = true;
    ++structureVersion_;
    anyDirty_ = true;
    return node.id;
}

bool VisualWorld::DestroyNode(VisualNodeId id) {
    VisualNode* node = GetNode(id);
    if (!node) {
        return false;
    }

    const auto childrenCopy = node->children;
    for (VisualNodeId childId : childrenCopy) {
        (void)DestroyNode(childId);
    }

    DetachFromParent(*node);
    if (rootId_ == id) {
        rootId_ = VisualNodeId{};
    }

    node->pinnedOwnerRoot.reset();
    node->scriptOwner = ObjectRef{};
    node->hasScriptOwner = false;
    if (node->nativeHandle.valid()) {
        (void)handleRegistry_->release(node->nativeHandle);
        node->nativeHandle = NativeHandle{};
    }
    node->children.clear();
    node->extras.reset();
    node->alive = false;
    node->dirtyFlags = VisualDirty_Clean;
    freeIndices_.push_back(id.index);
    if (liveCount_ > 0u) {
        --liveCount_;
    }
    ++destroyedCount_;
    structureDirty_ = true;
    layoutOrTransformDirty_ = true;
    ++structureVersion_;
    anyDirty_ = true;
    return true;
}

NativeHandle VisualWorld::HandleFor(VisualNodeId id) const noexcept {
    const VisualNode* node = GetNode(id);
    return node ? node->nativeHandle : NativeHandle{};
}

VisualNodeId VisualWorld::ResolveHandle(NativeHandle handle) const noexcept {
    if (!handle.valid()) {
        return VisualNodeId{};
    }
    void* raw = handleRegistry_->resolveRawFast(handle, visualNodeTypeId_);
    if (!raw) {
        return VisualNodeId{};
    }
    const auto* node = static_cast<const VisualNode*>(raw);
    if (!node->alive || node->nativeHandle != handle) {
        return VisualNodeId{};
    }
    return node->id;
}

bool VisualWorld::IsAlive(VisualNodeId id) const noexcept {
    return GetNode(id) != nullptr;
}

VisualNode* VisualWorld::GetNode(VisualNodeId id) noexcept {
    if (!id.IsValid() || id.index >= nodes_.size()) {
        return nullptr;
    }
    VisualNode& node = nodes_[id.index];
    if (!node.alive || node.id.generation != id.generation) {
        return nullptr;
    }
    return &node;
}

const VisualNode* VisualWorld::GetNode(VisualNodeId id) const noexcept {
    if (!id.IsValid() || id.index >= nodes_.size()) {
        return nullptr;
    }
    const VisualNode& node = nodes_[id.index];
    if (!node.alive || node.id.generation != id.generation) {
        return nullptr;
    }
    return &node;
}

void VisualWorld::SetRoot(VisualNodeId id) noexcept {
    rootId_ = IsAlive(id) ? id : VisualNodeId{};
    if (rootId_.IsValid()) {
        RefreshSubtreeDepths(rootId_, 0u);
    }
    structureDirty_ = true;
    ++structureVersion_;
    anyDirty_ = true;
}

bool VisualWorld::AddChild(VisualNodeId parentId, VisualNodeId childId) {
    VisualNode* parent = GetNode(parentId);
    VisualNode* child = GetNode(childId);
    if (!parent || !child || parentId == childId || WouldCreateCycle(parentId, childId)) {
        return false;
    }
    DetachFromParent(*child);
    child->parent = parentId;
    parent->children.push_back(childId);
    const std::uint16_t childDepth = (parent->layout.treeDepth < std::numeric_limits<std::uint16_t>::max())
        ? static_cast<std::uint16_t>(parent->layout.treeDepth + 1u)
        : std::numeric_limits<std::uint16_t>::max();
    RefreshSubtreeDepths(childId, childDepth);
    child->layout.hasValidMeasure = false;
    child->layout.hasValidArrange = false;
    MarkAncestorsDirty(parentId, VisualDirty_All);
    return true;
}

bool VisualWorld::InsertChild(VisualNodeId parentId, std::int32_t index, VisualNodeId childId) {
    VisualNode* parent = GetNode(parentId);
    VisualNode* child = GetNode(childId);
    if (!parent || !child || parentId == childId || WouldCreateCycle(parentId, childId)) {
        return false;
    }
    DetachFromParent(*child);
    child->parent = parentId;
    const std::size_t clampedIndex = (index <= 0)
        ? 0u
        : std::min(static_cast<std::size_t>(index), parent->children.size());
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(clampedIndex), childId);
    const std::uint16_t childDepth = (parent->layout.treeDepth < std::numeric_limits<std::uint16_t>::max())
        ? static_cast<std::uint16_t>(parent->layout.treeDepth + 1u)
        : std::numeric_limits<std::uint16_t>::max();
    RefreshSubtreeDepths(childId, childDepth);
    child->layout.hasValidMeasure = false;
    child->layout.hasValidArrange = false;
    MarkAncestorsDirty(parentId, VisualDirty_All);
    return true;
}

bool VisualWorld::RemoveChild(VisualNodeId parentId, VisualNodeId childId) {
    VisualNode* parent = GetNode(parentId);
    VisualNode* child = GetNode(childId);
    if (!parent || !child || child->parent != parentId) {
        return false;
    }
    auto it = std::find(parent->children.begin(), parent->children.end(), childId);
    if (it != parent->children.end()) {
        parent->children.erase(it);
    }
    child->parent = VisualNodeId{};
    MarkAncestorsDirty(parentId, VisualDirty_All);
    return true;
}

bool VisualWorld::ClearChildren(VisualNodeId parentId) {
    VisualNode* parent = GetNode(parentId);
    if (!parent) {
        return false;
    }
    for (VisualNodeId childId : parent->children) {
        if (VisualNode* child = GetNode(childId)) {
            child->parent = VisualNodeId{};
        }
    }
    parent->children.clear();
    MarkAncestorsDirty(parentId, VisualDirty_All);
    return true;
}

bool VisualWorld::BindOwner(VisualNodeId id, ObjectRef owner, std::uint32_t callbackMask) {
    VisualNode* node = GetNode(id);
    if (!node) {
        return false;
    }
    node->scriptOwner = owner;
    node->hasScriptOwner = owner.valid();
    node->callbackMask = callbackMask;
    RefreshOwnerPin(*node);
    return true;
}

ObjectRef VisualWorld::GetOwner(VisualNodeId id) const noexcept {
    const VisualNode* node = GetNode(id);
    return node ? node->scriptOwner : ObjectRef{};
}

bool VisualWorld::SetSize(VisualNodeId id, float width, float height) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.width = width;
    node->layout.height = height;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetMinMaxSize(VisualNodeId id, float minW, float minH, float maxW, float maxH) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.minWidth = IsFinitePositive(minW) ? minW : 0.0f;
    node->layout.minHeight = IsFinitePositive(minH) ? minH : 0.0f;
    node->layout.maxWidth = (std::isnan(maxW) || maxW < 0.0f) ? std::numeric_limits<float>::infinity() : maxW;
    node->layout.maxHeight = (std::isnan(maxH) || maxH < 0.0f) ? std::numeric_limits<float>::infinity() : maxH;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetMargin(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.margin = Thickness{l, t, r, b};
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetPadding(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.padding = Thickness{l, t, r, b};
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetAlignment(VisualNodeId id, HorizontalAlignment hAlign, VerticalAlignment vAlign) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.hAlign = hAlign;
    node->layout.vAlign = vAlign;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetVisibility(VisualNodeId id, Visibility vis) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.visibility = vis;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetOpacity(VisualNodeId id, float opacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.opacity = std::clamp(opacity, 0.0f, 1.0f);
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetZIndex(VisualNodeId id, std::int32_t zIndex) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.zIndex = zIndex;
    MarkAncestorsDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetClipToBounds(VisualNodeId id, bool clip) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.clipToBounds = clip;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetInteractiveFlags(
    VisualNodeId id,
    bool enabled,
    bool hitTestVisible,
    bool focusable) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.isEnabled = enabled;
    node->layout.isHitTestVisible = hitTestVisible;
    node->layout.isFocusable = focusable;
    return true;
}

bool VisualWorld::SetLayoutMode(
    VisualNodeId id,
    LayoutMode mode,
    float p0,
    float p1,
    std::int32_t i0,
    std::int32_t i1) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.layoutMode = mode;
    node->layout.layoutParam0 = p0;
    node->layout.layoutParam1 = p1;
    node->layout.layoutInt0 = i0;
    node->layout.layoutInt1 = i1;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::GridClearDefs(VisualNodeId id) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->HasExtras()) {
        node->EnsureExtras().rowDefs.clear();
        node->EnsureExtras().colDefs.clear();
    }
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::GridAddRowDef(VisualNodeId id, GridUnitType unit, float value) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->EnsureExtras().rowDefs.push_back(VisualGridLength{unit, value});
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::GridAddColDef(VisualNodeId id, GridUnitType unit, float value) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->EnsureExtras().colDefs.push_back(VisualGridLength{unit, value});
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetGridCell(
    VisualNodeId id,
    std::int32_t row,
    std::int32_t col,
    std::int32_t rowSpan,
    std::int32_t colSpan) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.gridRow = std::max<std::int32_t>(0, row);
    node->layout.gridCol = std::max<std::int32_t>(0, col);
    node->layout.gridRowSpan = std::max<std::int32_t>(1, rowSpan);
    node->layout.gridColSpan = std::max<std::int32_t>(1, colSpan);
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetDock(VisualNodeId id, Dock dock) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.dock = dock;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetCanvasOffsets(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.canvasLeft = l;
    node->layout.canvasTop = t;
    node->layout.canvasRight = r;
    node->layout.canvasBottom = b;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetScrollOffsets(VisualNodeId id, float ox, float oy) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.scrollOffsetX = ox;
    node->layout.scrollOffsetY = oy;
    MarkDirty(id, VisualDirty_All);
    return true;
}

float VisualWorld::GetLayoutRectScalar(VisualNodeId id, VisualLayoutRectQuery which) const noexcept {
    const VisualNode* node = GetNode(id);
    if (!node) return 0.0f;
    switch (which) {
    case VisualLayoutRectQuery::X: return node->layout.layoutRect.x;
    case VisualLayoutRectQuery::Y: return node->layout.layoutRect.y;
    case VisualLayoutRectQuery::Width: return node->layout.layoutRect.width;
    case VisualLayoutRectQuery::Height: return node->layout.layoutRect.height;
    case VisualLayoutRectQuery::DesiredWidth: return node->layout.desiredSize.width;
    case VisualLayoutRectQuery::DesiredHeight: return node->layout.desiredSize.height;
    case VisualLayoutRectQuery::ScrollExtentWidth: return node->layout.scrollExtent.width;
    case VisualLayoutRectQuery::ScrollExtentHeight: return node->layout.scrollExtent.height;
    case VisualLayoutRectQuery::ViewportX:
    case VisualLayoutRectQuery::ViewportY:
    case VisualLayoutRectQuery::ViewportWidth:
    case VisualLayoutRectQuery::ViewportHeight: {
        Rect bounds = node->layout.layoutRect;
        if (node->layout.hasPerspective3D) {
            bounds = node->WorldMatrix3D().TransformRectBounds(bounds);
        } else if (!node->layout.worldTransform.IsIdentity()) {
            bounds = node->layout.worldTransform.TransformRectBounds(bounds);
        }
        switch (which) {
        case VisualLayoutRectQuery::ViewportX: return bounds.x;
        case VisualLayoutRectQuery::ViewportY: return bounds.y;
        case VisualLayoutRectQuery::ViewportWidth: return bounds.width;
        case VisualLayoutRectQuery::ViewportHeight: return bounds.height;
        default: break;
        }
        return 0.0f;
    }
    }
    return 0.0f;
}

bool VisualWorld::TryScreenToLocal(VisualNodeId id, Point viewportPoint, Point& outLocal) const noexcept {
    const VisualNode* node = GetNode(id);
    if (!node) {
        outLocal = Point{};
        return false;
    }
    Point worldPt = viewportPoint;
    if (node->layout.hasPerspective3D) {
        if (!node->WorldMatrix3D().InverseProjectScreenToLocalPlane(viewportPoint, worldPt)) {
            outLocal = Point{};
            return false;
        }
    } else if (!node->layout.worldTransform.IsIdentity()) {
        Matrix2D invM{};
        if (!node->layout.worldTransform.TryInvert(invM)) {
            outLocal = Point{};
            return false;
        }
        worldPt = invM.TransformPoint(viewportPoint);
    }
    outLocal = Point{
        worldPt.x - node->layout.layoutRect.x,
        worldPt.y - node->layout.layoutRect.y};
    return std::isfinite(outLocal.x) && std::isfinite(outLocal.y);
}

Point VisualWorld::ScreenToLocal(VisualNodeId id, Point viewportPoint) const noexcept {
    Point local{};
    if (TryScreenToLocal(id, viewportPoint, local)) {
        return local;
    }
    const VisualNode* node = GetNode(id);
    if (!node) {
        return Point{};
    }
    return Point{
        viewportPoint.x - node->layout.layoutRect.x,
        viewportPoint.y - node->layout.layoutRect.y};
}

bool VisualWorld::SetBoxStyle(
    VisualNodeId id,
    std::uint32_t bgRgba,
    std::uint32_t borderRgba,
    float btL,
    float btT,
    float btR,
    float btB,
    float crTL,
    float crTR,
    float crBR,
    float crBL) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->box.bgRgba = bgRgba;
    node->box.borderRgba = borderRgba;
    node->box.borderThickness = Thickness{btL, btT, btR, btB};
    node->box.cornerRadius = CornerRadius{crTL, crTR, crBR, crBL};
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxGradients(VisualNodeId id, std::int32_t bgGradId, std::int32_t borderGradId) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (!node->box.ownsMutableBgGrad || node->box.baseBgGradId != bgGradId) {
        node->box.bgGradId = bgGradId;
        node->box.baseBgGradId = bgGradId;
        node->box.ownsMutableBgGrad = false;
    }
    if (!node->box.ownsMutableBorderGrad || node->box.baseBorderGradId != borderGradId) {
        node->box.borderGradId = borderGradId;
        node->box.baseBorderGradId = borderGradId;
        node->box.ownsMutableBorderGrad = false;
    }
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxBrushOpacity(VisualNodeId id, float bgOpacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->box.bgOpacity = std::clamp(bgOpacity, 0.0f, 1.0f);
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxShadow(VisualNodeId id, bool isBoxShadow) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->box.isBoxShadow = isBoxShadow;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetTransform2D(
    VisualNodeId id,
    float tx,
    float ty,
    float sx,
    float sy,
    float rotDeg,
    float skewX,
    float skewY,
    float originX,
    float originY) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->transform2D.tx = tx;
    node->transform2D.ty = ty;
    node->transform2D.sx = sx;
    node->transform2D.sy = sy;
    node->transform2D.rotDeg = rotDeg;
    node->transform2D.skewX = skewX;
    node->transform2D.skewY = skewY;
    node->transform2D.originX = originX;
    node->transform2D.originY = originY;
    MarkDirty(id, static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
    return true;
}

bool VisualWorld::SetBlendMode(VisualNodeId id, BlendingMode mode) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->transform2D.blendMode == mode) return true;
    node->transform2D.blendMode = mode;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetLayoutTransform2D(
    VisualNodeId id,
    float sx,
    float sy,
    float rotDeg,
    float skewX,
    float skewY) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    VisualLayoutTransform2D& lt = node->layout.layoutTransform2D;
    const float safeSx = std::isfinite(sx) ? sx : 1.0f;
    const float safeSy = std::isfinite(sy) ? sy : 1.0f;
    const float safeRot = std::isfinite(rotDeg) ? rotDeg : 0.0f;
    const float safeSkX = std::isfinite(skewX) ? skewX : 0.0f;
    const float safeSkY = std::isfinite(skewY) ? skewY : 0.0f;
    const bool nextHas =
        std::fabs(safeSx - 1.0f) > 1e-6f ||
        std::fabs(safeSy - 1.0f) > 1e-6f ||
        std::fabs(safeRot) > 1e-6f ||
        std::fabs(safeSkX) > 1e-6f ||
        std::fabs(safeSkY) > 1e-6f;
    if (node->layout.hasLayoutTransform == nextHas &&
        std::fabs(lt.sx - safeSx) <= 1e-6f &&
        std::fabs(lt.sy - safeSy) <= 1e-6f &&
        std::fabs(lt.rotDeg - safeRot) <= 1e-6f &&
        std::fabs(lt.skewX - safeSkX) <= 1e-6f &&
        std::fabs(lt.skewY - safeSkY) <= 1e-6f) {
        return true;
    }
    lt.sx = safeSx;
    lt.sy = safeSy;
    lt.rotDeg = safeRot;
    lt.skewX = safeSkX;
    lt.skewY = safeSkY;
    node->layout.hasLayoutTransform = nextHas;
    node->customScalars[1] = safeSx;
    node->customScalars[2] = safeSy;
    node->customScalars[3] = safeRot;
    node->layout.hasValidMeasure = false;
    node->layout.hasValidArrange = false;
    MarkDirty(id, static_cast<VisualDirtyFlags>(VisualDirty_Layout | VisualDirty_Transform | VisualDirty_Visual));
    return true;
}

bool VisualWorld::SetLayoutTransform(
    VisualNodeId id,
    float sx,
    float sy,
    float rotDeg,
    bool enabled) noexcept {
    if (!enabled) {
        return SetLayoutTransform2D(id, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    VisualNode* node = GetNode(id);
    if (!node) return false;
    VisualLayoutTransform2D& lt = node->layout.layoutTransform2D;
    const float safeSx = std::isfinite(sx) ? sx : 1.0f;
    const float safeSy = std::isfinite(sy) ? sy : 1.0f;
    const float safeRot = std::isfinite(rotDeg) ? rotDeg : 0.0f;
    if (node->layout.hasLayoutTransform &&
        std::fabs(lt.sx - safeSx) <= 1e-6f &&
        std::fabs(lt.sy - safeSy) <= 1e-6f &&
        std::fabs(lt.rotDeg - safeRot) <= 1e-6f &&
        std::fabs(lt.skewX) <= 1e-6f &&
        std::fabs(lt.skewY) <= 1e-6f) {
        return true;
    }
    lt.sx = safeSx;
    lt.sy = safeSy;
    lt.rotDeg = safeRot;
    lt.skewX = 0.0f;
    lt.skewY = 0.0f;
    node->layout.hasLayoutTransform = true;
    node->customScalars[1] = safeSx;
    node->customScalars[2] = safeSy;
    node->customScalars[3] = safeRot;
    node->layout.hasValidMeasure = false;
    node->layout.hasValidArrange = false;
    MarkDirty(id, static_cast<VisualDirtyFlags>(VisualDirty_Layout | VisualDirty_Transform | VisualDirty_Visual));
    return true;
}

bool VisualWorld::SetTransform3D(
    VisualNodeId id,
    float rx,
    float ry,
    float rz,
    float tx,
    float ty,
    float tz,
    float camDist) noexcept {
    return SetTransform3DEx(
        id, rx, ry, rz, tx, ty, tz,
        1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        camDist, false);
}

bool VisualWorld::SetTransform3DEx(
    VisualNodeId id,
    float rx,
    float ry,
    float rz,
    float tx,
    float ty,
    float tz,
    float sx,
    float sy,
    float sz,
    float cx,
    float cy,
    float cz,
    float camDist,
    bool useExplicitCenter) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& t3d = node->EnsureExtras().transform3D;
    t3d.rx = rx;
    t3d.ry = ry;
    t3d.rz = rz;
    t3d.tx = tx;
    t3d.ty = ty;
    t3d.tz = tz;
    t3d.sx = std::isfinite(sx) ? sx : 1.0f;
    t3d.sy = std::isfinite(sy) ? sy : 1.0f;
    t3d.sz = std::isfinite(sz) ? sz : 1.0f;
    t3d.cx = std::isfinite(cx) ? cx : 0.0f;
    t3d.cy = std::isfinite(cy) ? cy : 0.0f;
    t3d.cz = std::isfinite(cz) ? cz : 0.0f;
    t3d.camDist = camDist;
    t3d.useExplicitCenter = useExplicitCenter;
    t3d.enabled =
        std::fabs(rx) > 1e-6f || std::fabs(ry) > 1e-6f || std::fabs(rz) > 1e-6f ||
        std::fabs(tx) > 1e-6f || std::fabs(ty) > 1e-6f || std::fabs(tz) > 1e-6f ||
        std::fabs(t3d.sx - 1.0f) > 1e-6f || std::fabs(t3d.sy - 1.0f) > 1e-6f || std::fabs(t3d.sz - 1.0f) > 1e-6f ||
        camDist > 0.0f;
    MarkDirty(id, static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
    return true;
}

bool VisualWorld::SetEffect(
    VisualNodeId id,
    UIEffectKind kind,
    float radius,
    float depth,
    float dirDeg,
    std::uint32_t colorRgba,
    float opacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& eff = node->EnsureExtras().effect;
    eff.kind = kind;
    eff.radius = radius;
    eff.depth = depth;
    eff.dirDeg = dirDeg;
    eff.colorRgba = colorRgba;
    eff.opacity = opacity;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetOpacityMask(
    VisualNodeId id,
    bool enabled,
    std::int32_t gradId,
    std::uint32_t solidRgba,
    float opacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& om = node->EnsureExtras().opacityMask;
    const bool gradChanged = (om.gradId != gradId) || !enabled;
    om.enabled = enabled;
    om.gradId = gradId;
    om.solidRgba = solidRgba;
    om.opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (gradChanged) {
        om.stopOffsets.fill(std::numeric_limits<float>::quiet_NaN());
    }
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetText(
    VisualNodeId id,
    std::string_view text,
    std::string_view fontFamily,
    float fontSize,
    std::uint32_t fgRgba,
    TextWrapping wrap,
    TextAlignment align,
    TextTrimming trim) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& txt = node->EnsureExtras().text;
    txt.text.assign(text);
    txt.fontFamily.assign(fontFamily);
    txt.fontSize = (fontSize > 0.0f) ? fontSize : 14.0f;
    txt.fgRgba = fgRgba;
    txt.wrap = wrap;
    txt.align = align;
    txt.trim = trim;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetTextMetrics(
    VisualNodeId id,
    float lineHeight,
    float charSpacing,
    std::int32_t maxLines,
    std::int32_t decorations) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& txt = node->EnsureExtras().text;
    txt.lineHeight = lineHeight;
    txt.charSpacing = charSpacing;
    txt.maxLines = maxLines;
    txt.decorations = decorations;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetTextStyle(
    VisualNodeId id,
    int fontWeight,
    bool fontItalic,
    std::int32_t fgGradId) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& txt = node->EnsureExtras().text;
    txt.fontWeight = (fontWeight > 0) ? fontWeight : 400;
    txt.fontItalic = fontItalic;
    txt.fgGradId = fgGradId;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetTextBrushOpacity(VisualNodeId id, float fgOpacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->EnsureExtras().text.fgOpacity = std::clamp(fgOpacity, 0.0f, 1.0f);
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::ClearTextSpans(VisualNodeId id) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->HasExtras() && !node->Text().richSpans.empty()) {
        node->EnsureExtras().text.richSpans.clear();
        MarkAncestorsDirty(id, VisualDirty_All);
    }
    return true;
}

bool VisualWorld::AddTextSpan(
    VisualNodeId id,
    std::string_view text,
    std::string_view fontFamily,
    float fontSize,
    int fontWeight,
    std::int8_t fontItalic,
    std::uint32_t fgRgba,
    bool hasFg,
    std::int32_t decorations) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    VisualTextSpan span{};
    span.text.assign(text);
    span.fontFamily.assign(fontFamily);
    span.fontSize = fontSize;
    span.fontWeight = fontWeight;
    span.fontItalic = fontItalic;
    span.fgRgba = fgRgba;
    span.hasFg = hasFg;
    span.decorations = decorations;
    node->EnsureExtras().text.richSpans.push_back(std::move(span));
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

TextLayoutResult VisualWorld::LayoutTextNode(const VisualNode& node, float maxWidth) const {
    const VisualTextData& txt = node.Text();
    UIResourceStore* store = const_cast<VisualWorld*>(this)->ResourceStore();
    if (!store) {
        TextLayoutResult fallback{};
        const float safeFont = (txt.fontSize > 1.0f) ? txt.fontSize : 14.0f;
        fallback.totalWidth = static_cast<float>(txt.text.size()) * safeFont * 0.6f;
        fallback.totalHeight = safeFont * 1.25f;
        return fallback;
    }
    if (!txt.richSpans.empty()) {
        std::vector<RichTextRunInput> runs;
        runs.reserve(txt.richSpans.size());
        for (const VisualTextSpan& sp : txt.richSpans) {
            RichTextRunInput r{};
            r.text = sp.text;
            r.fontFamily = sp.fontFamily;
            r.fontSize = sp.fontSize;
            r.fontWeight = sp.fontWeight;
            r.fontItalic = sp.fontItalic;
            r.fgRgba = sp.fgRgba;
            r.hasFg = sp.hasFg;
            r.decorations = sp.decorations;
            runs.push_back(r);
        }
        return store->Fonts().MeasureRichLayout(
            runs,
            txt.fontFamily,
            txt.fontSize,
            txt.fontWeight,
            txt.fontItalic,
            maxWidth,
            txt.wrap,
            txt.charSpacing,
            txt.lineHeight,
            node.layout.effectiveRtl,
            txt.trim);
    }
    return store->Fonts().MeasureLayout(
        txt.text,
        txt.fontFamily,
        txt.fontSize,
        maxWidth,
        txt.wrap,
        txt.charSpacing,
        txt.fontWeight,
        txt.fontItalic,
        txt.lineHeight,
        node.layout.effectiveRtl,
        txt.trim);
}

Size VisualWorld::MeasureTextNode(const VisualNode& node, float maxWidth) const {
    const VisualTextData& txt = node.Text();
    if (txt.text.empty() && txt.richSpans.empty()) {
        const float safeFont = std::max(1.0f, txt.fontSize);
        return Size{0.0f, (txt.lineHeight > 0.0f) ? std::max(safeFont * 1.25f, txt.lineHeight) : (safeFont * 1.25f)};
    }
    const TextLayoutResult res = LayoutTextNode(node, maxWidth);
    return Size{res.totalWidth, res.totalHeight};
}

bool VisualWorld::SetTextSelection(
    VisualNodeId id,
    std::int32_t selStartCp,
    std::int32_t selLenCp,
    std::int32_t caretCp,
    bool showCaret,
    std::uint32_t selRgba,
    std::uint32_t caretRgba) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& txt = node->EnsureExtras().text;
    txt.selStartCp = selStartCp;
    txt.selLenCp = selLenCp;
    txt.caretCp = caretCp;
    txt.showCaret = showCaret;
    txt.selRgba = selRgba;
    txt.caretRgba = caretRgba;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

Size VisualWorld::MeasureText(
    std::string_view text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrap,
    float charSpacing) const {
    if (text.empty()) {
        return Size{0.0f, 0.0f};
    }
    UIResourceStore* store = const_cast<VisualWorld*>(this)->ResourceStore();
    if (store) {
        const TextLayoutResult res = store->Fonts().MeasureLayout(
            text, fontFamily, fontSize, maxWidth, wrap, charSpacing);
        return Size{res.totalWidth, res.totalHeight};
    }
    const float safeFont = (fontSize > 1.0f) ? fontSize : 14.0f;
    return Size{static_cast<float>(text.size()) * safeFont * 0.6f, safeFont * 1.25f};
}

std::int32_t VisualWorld::HitTestTextCodePoint(VisualNodeId id, float localX, float localY) const noexcept {
    const VisualNode* node = GetNode(id);
    if (!node) {
        return 0;
    }
    const VisualTextData& txt = node->Text();
    if (txt.text.empty() && txt.richSpans.empty()) {
        return 0;
    }
    const float padL = std::max(0.0f, node->layout.padding.left);
    const float padT = std::max(0.0f, node->layout.padding.top);
    const float padR = std::max(0.0f, node->layout.padding.right);
    const float boxW = std::max(0.0f, node->layout.layoutRect.width - padL - padR);
    const float tx = localX - padL;
    const float ty = localY - padT;
    const float maxW = (txt.wrap == TextWrapping::NoWrap && txt.trim == TextTrimming::None)
        ? 1000000.0f
        : std::max(1.0f, boxW);
    const TextLayoutResult layout = LayoutTextNode(*node, maxW);
    if (layout.lines.empty()) {
        return 0;
    }

    const TextLineMetrics* chosenLine = &layout.lines.back();
    if (ty <= layout.lines.front().topY + layout.lines.front().lineHeight) {
        chosenLine = &layout.lines.front();
    } else {
        for (const TextLineMetrics& line : layout.lines) {
            if (ty <= line.topY + line.lineHeight) {
                chosenLine = &line;
                break;
            }
        }
    }

    float alignDx = 0.0f;
    if (txt.align == TextAlignment::Center) {
        alignDx = std::max(0.0f, (boxW - chosenLine->width) * 0.5f);
    } else if (txt.align == TextAlignment::Right) {
        alignDx = std::max(0.0f, boxW - chosenLine->width);
    }

    const std::size_t lineBegin = std::min(chosenLine->firstGlyphIndex, layout.glyphs.size());
    const std::size_t lineEnd = std::min(lineBegin + chosenLine->glyphCount, layout.glyphs.size());
    for (std::size_t i = lineBegin; i < lineEnd; ++i) {
        const PositionedGlyph& g = layout.glyphs[i];
        const float midX = g.penX + alignDx + g.advanceX * 0.5f;
        if (tx < midX) {
            return static_cast<std::int32_t>(g.sourceCodePointIndex);
        }
    }
    if (lineEnd > lineBegin) {
        return static_cast<std::int32_t>(layout.glyphs[lineEnd - 1u].sourceCodePointIndex + 1u);
    }
    return static_cast<std::int32_t>(chosenLine->firstCodePointIndex);
}

bool VisualWorld::SetImage(
    VisualNodeId id,
    std::string_view uri,
    Stretch stretch,
    StretchDirection stretchDir,
    std::uint32_t tintRgba) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureExtras().image;
    img.uri.assign(uri);
    img.stretch = stretch;
    img.stretchDir = stretchDir;
    img.tintRgba = tintRgba;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetImageViewbox(
    VisualNodeId id,
    float vx,
    float vy,
    float vw,
    float vh) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureExtras().image;
    img.viewboxX = std::isfinite(vx) ? vx : 0.0f;
    img.viewboxY = std::isfinite(vy) ? vy : 0.0f;
    img.viewboxW = std::isfinite(vw) ? vw : 1.0f;
    img.viewboxH = std::isfinite(vh) ? vh : 1.0f;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetImageViewport(
    VisualNodeId id,
    float vpx,
    float vpy,
    float vpw,
    float vph,
    bool hasExplicit,
    bool isAbsolute) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureExtras().image;
    img.viewportX = std::isfinite(vpx) ? vpx : 0.0f;
    img.viewportY = std::isfinite(vpy) ? vpy : 0.0f;
    img.viewportW = (std::isfinite(vpw) && vpw > 0.0f) ? vpw : 1.0f;
    img.viewportH = (std::isfinite(vph) && vph > 0.0f) ? vph : 1.0f;
    img.hasExplicitViewport = hasExplicit;
    img.viewportAbsolute = isAbsolute;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetVisualBrushSource(
    VisualNodeId id,
    VisualNodeId sourceId,
    int alignX,
    int alignY) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureExtras().image;
    img.visualBrushSource = sourceId;
    img.alignX = static_cast<std::uint8_t>(std::clamp(alignX, 0, 2));
    img.alignY = static_cast<std::uint8_t>(std::clamp(alignY, 0, 2));
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetImageTileMode(VisualNodeId id, bool tileWrap) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureExtras().image;
    if (img.tileWrap == tileWrap) {
        return true;
    }
    img.tileWrap = tileWrap;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetVectorShape(
    VisualNodeId id,
    VectorShapeKind shapeKind,
    std::string_view pathOrParams,
    std::uint32_t fillRgba,
    std::int32_t fillGradId,
    std::uint32_t strokeRgba,
    std::int32_t strokeGradId,
    float strokeWidth,
    Stretch stretch,
    float trimStart,
    float trimEnd) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const bool firstInit = (node->extras == nullptr);
    auto& vec = node->EnsureExtras().vector;
    const bool geomChanged = firstInit || (vec.shapeKind != shapeKind) || (vec.pathOrParams != pathOrParams);
    const bool layoutRelevantChanged =
        geomChanged ||
        (std::fabs(vec.strokeWidth - strokeWidth) > 1e-6f) ||
        (vec.stretch != stretch) ||
        (std::fabs(vec.trimStart - trimStart) > 1e-6f) ||
        (std::fabs(vec.trimEnd - trimEnd) > 1e-6f);
    const std::uint32_t prevFillRgba = vec.fillRgba;
    const std::int32_t prevFillGrad = vec.fillGradId;
    const std::uint32_t prevStrokeRgba = vec.strokeRgba;
    const std::int32_t prevStrokeGrad = vec.strokeGradId;

    vec.shapeKind = shapeKind;
    if (geomChanged) {
        vec.pathOrParams.assign(pathOrParams);
        vec.cachedPathHash = 0u;
        vec.cachedSvgBounds = Rect{};
        vec.svgBoundsCached = false;
    }
    vec.fillRgba = fillRgba;
    if (!vec.ownsMutableFillGrad || vec.baseFillGradId != fillGradId) {
        vec.fillGradId = fillGradId;
        vec.baseFillGradId = fillGradId;
        vec.ownsMutableFillGrad = false;
    }
    vec.strokeRgba = strokeRgba;
    if (!vec.ownsMutableStrokeGrad || vec.baseStrokeGradId != strokeGradId) {
        vec.strokeGradId = strokeGradId;
        vec.baseStrokeGradId = strokeGradId;
        vec.ownsMutableStrokeGrad = false;
    }
    vec.strokeWidth = strokeWidth;
    vec.stretch = stretch;
    vec.trimStart = trimStart;
    vec.trimEnd = trimEnd;

    const bool visualChanged =
        layoutRelevantChanged ||
        (prevFillRgba != vec.fillRgba) ||
        (prevFillGrad != vec.fillGradId) ||
        (prevStrokeRgba != vec.strokeRgba) ||
        (prevStrokeGrad != vec.strokeGradId);
    if (!visualChanged) {
        return true;
    }
    if (layoutRelevantChanged) {
        MarkAncestorsDirty(id, VisualDirty_All);
    } else {
        MarkDirty(id, VisualDirty_Visual);
    }
    return true;
}

bool VisualWorld::SetVectorBrushOpacity(VisualNodeId id, float fillOpacity, float strokeOpacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& vec = node->EnsureExtras().vector;
    vec.fillOpacity = std::clamp(fillOpacity, 0.0f, 1.0f);
    vec.strokeOpacity = std::clamp(strokeOpacity, 0.0f, 1.0f);
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetVectorStrokeDash(
    VisualNodeId id,
    std::string_view dashSpec,
    std::int32_t dashCap,
    std::int32_t startCap,
    std::int32_t endCap) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& vec = node->EnsureExtras().vector;
    vec.dashArray.clear();
    std::size_t pos = 0u;
    while (pos < dashSpec.size()) {
        while (pos < dashSpec.size() &&
               (dashSpec[pos] == ' ' || dashSpec[pos] == '\t' ||
                dashSpec[pos] == ',' || dashSpec[pos] == '\r' || dashSpec[pos] == '\n')) {
            ++pos;
        }
        if (pos >= dashSpec.size()) break;
        std::size_t endPos = pos;
        while (endPos < dashSpec.size() &&
               dashSpec[endPos] != ' ' && dashSpec[endPos] != '\t' &&
               dashSpec[endPos] != ',' && dashSpec[endPos] != '\r' && dashSpec[endPos] != '\n') {
            ++endPos;
        }
        const std::string tok(dashSpec.substr(pos, endPos - pos));
        char* parseEnd = nullptr;
        const float v = std::strtof(tok.c_str(), &parseEnd);
        if (parseEnd != tok.c_str() && std::isfinite(v) && v >= 0.0f) {
            vec.dashArray.push_back(v);
        }
        pos = endPos;
    }
    vec.dashCap = static_cast<std::uint8_t>(std::clamp(dashCap, 0, 3));
    vec.startLineCap = static_cast<std::uint8_t>(std::clamp(startCap, 0, 3));
    vec.endLineCap = static_cast<std::uint8_t>(std::clamp(endCap, 0, 3));
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::InvalidateDraw(VisualNodeId id) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->drawDirty = true;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

void VisualWorld::SetCustomLayoutCallbacks(CustomMeasureCallback onMeasure, CustomArrangeCallback onArrange) {
    customMeasureCallback_ = std::move(onMeasure);
    customArrangeCallback_ = std::move(onArrange);
    if (rootId_.IsValid()) {
        MarkDirty(rootId_, VisualDirty_Layout);
    }
}

bool VisualWorld::BindEcsColumn(
    VisualNodeId nodeId,
    std::uint32_t targetProp,
    const float* columnData,
    std::size_t rowIndex,
    std::size_t columnCount) {
    if (!IsAlive(nodeId) || !columnData || rowIndex >= columnCount) {
        return false;
    }
    for (auto& existing : ecsBindings_) {
        if (existing.targetNode == nodeId && existing.targetProp == targetProp) {
            existing.columnData = columnData;
            existing.rowIndex = rowIndex;
            existing.columnCount = columnCount;
            existing.hasApplied = false;
            return true;
        }
    }
    VisualEcsColumnBinding binding{};
    binding.targetNode = nodeId;
    binding.targetProp = targetProp;
    binding.columnData = columnData;
    binding.rowIndex = rowIndex;
    binding.columnCount = columnCount;
    ecsBindings_.push_back(binding);
    return true;
}

void VisualWorld::ClearEcsBindings() noexcept {
    ecsBindings_.clear();
}

std::size_t VisualWorld::FlushTrackB() {
    std::size_t updated = 0u;
    for (auto& binding : ecsBindings_) {
        if (!binding.columnData || binding.rowIndex >= binding.columnCount) {
            continue;
        }
        VisualNode* node = GetNode(binding.targetNode);
        if (!node) {
            continue;
        }
        const float v = binding.columnData[binding.rowIndex];
        if (!std::isfinite(v) || (binding.hasApplied && v == binding.lastAppliedValue)) {
            continue;
        }
        binding.lastAppliedValue = v;
        binding.hasApplied = true;
        switch (static_cast<VisualAnimProp>(binding.targetProp)) {
            case VisualAnimProp::Opacity:
                node->layout.opacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::Width:
                node->layout.width = std::max(0.0f, v);
                MarkAncestorsDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Layout | VisualDirty_Visual));
                break;
            case VisualAnimProp::Height:
                node->layout.height = std::max(0.0f, v);
                MarkAncestorsDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Layout | VisualDirty_Visual));
                break;
            case VisualAnimProp::TranslateX:
                node->transform2D.tx = v;
                MarkDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
                break;
            case VisualAnimProp::TranslateY:
                node->transform2D.ty = v;
                MarkDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
                break;
            case VisualAnimProp::ScaleX:
                node->transform2D.sx = v;
                MarkDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
                break;
            case VisualAnimProp::ScaleY:
                node->transform2D.sy = v;
                MarkDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
                break;
            case VisualAnimProp::RotateDeg:
                node->transform2D.rotDeg = v;
                MarkDirty(
                    binding.targetNode,
                    static_cast<VisualDirtyFlags>(VisualDirty_Transform | VisualDirty_Visual));
                break;
            case VisualAnimProp::CornerRadius:
                node->box.cornerRadius = CornerRadius{std::max(0.0f, v)};
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::PathTrimStart:
                node->EnsureExtras().vector.trimStart = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::PathTrimEnd:
                node->EnsureExtras().vector.trimEnd = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::EffectRadius:
                node->EnsureExtras().effect.radius = std::max(0.0f, v);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::EffectOpacity:
                node->EnsureExtras().effect.opacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::BrushOpacity:
                node->box.bgOpacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::FgBrushOpacity:
                node->EnsureExtras().text.fgOpacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            default:
                node->customScalars[binding.targetProp % 4u] = v;
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
        }
        ++updated;
    }
    return updated;
}

void VisualWorld::MarkDirty(VisualNodeId id, VisualDirtyFlags flags) noexcept {
    if (VisualNode* node = GetNode(id)) {
        const std::uint8_t prev = node->dirtyFlags;
        node->dirtyFlags = static_cast<std::uint8_t>(prev | flags);
        if ((flags & (VisualDirty_Structure | VisualDirty_Layout | VisualDirty_Transform)) != 0u) {
            layoutOrTransformDirty_ = true;
        }
        if ((flags & (VisualDirty_Structure | VisualDirty_Layout)) != 0u) {
            structureDirty_ = true;
            ++structureVersion_;
        } else if (!structureDirty_ && prev == VisualDirty_Clean) {
            dirtyNodes_.push_back(id);
        }
        anyDirty_ = true;
    }
}

void VisualWorld::ClearDirtyFlags() noexcept {
    if (!structureDirty_ && !dirtyNodes_.empty() && dirtyNodes_.size() * 2u < nodes_.size()) {
        for (VisualNodeId id : dirtyNodes_) {
            if (VisualNode* node = GetNode(id)) {
                node->dirtyFlags = VisualDirty_Clean;
                node->drawDirty = false;
            }
        }
    } else {
        for (auto& node : nodes_) {
            if (node.alive) {
                node.dirtyFlags = VisualDirty_Clean;
                node.drawDirty = false;
            }
        }
    }
    dirtyNodes_.clear();
    structureDirty_ = false;
    layoutOrTransformDirty_ = false;
    anyDirty_ = false;
}

void VisualWorld::Clear() noexcept {
    for (auto& node : nodes_) {
        if (!node.alive) {
            continue;
        }
        node.pinnedOwnerRoot.reset();
        node.scriptOwner = ObjectRef{};
        node.hasScriptOwner = false;
        if (node.nativeHandle.valid()) {
            (void)handleRegistry_->release(node.nativeHandle);
            node.nativeHandle = NativeHandle{};
        }
    }
    nodes_.clear();
    freeIndices_.clear();
    ecsBindings_.clear();
    for (auto& bucket : measureDepthBuckets_) {
        bucket.clear();
    }
    for (auto& bucket : arrangeDepthBuckets_) {
        bucket.clear();
    }
    for (auto& bucket : transformDepthBuckets_) {
        bucket.clear();
    }
    lastViewportSize_ = Size{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()};
    lastLaidOutRoot_ = VisualNodeId{};
    rootId_ = VisualNodeId{};
    liveCount_ = 0u;
    dirtyNodes_.clear();
    structureDirty_ = true;
    layoutOrTransformDirty_ = true;
    ++structureVersion_;
    anyDirty_ = false;
}

void VisualWorld::MarkScriptRootsInPools() const noexcept {
    for (const auto& node : nodes_) {
        if (node.alive && node.hasScriptOwner && node.scriptOwner.valid()) {
            rs_rt::markValueInPools(UnifiedValue{node.scriptOwner});
        }
    }
}

void VisualWorld::RefreshScriptOwnerPins() noexcept {
    for (auto& node : nodes_) {
        if (node.alive) {
            RefreshOwnerPin(node);
        }
    }
}

VisualNode* VisualWorld::NodeAtSlot(std::size_t slot) noexcept {
    return (slot < nodes_.size() && nodes_[slot].alive) ? &nodes_[slot] : nullptr;
}

const VisualNode* VisualWorld::NodeAtSlot(std::size_t slot) const noexcept {
    return (slot < nodes_.size() && nodes_[slot].alive) ? &nodes_[slot] : nullptr;
}

void VisualWorld::MarkAncestorsDirty(VisualNodeId id, VisualDirtyFlags flags) noexcept {
    VisualNodeId cur = id;
    while (cur.IsValid()) {
        VisualNode* node = GetNode(cur);
        if (!node) {
            break;
        }
        node->dirtyFlags = static_cast<std::uint8_t>(node->dirtyFlags | flags);
        cur = node->parent;
    }
    structureDirty_ = true;
    layoutOrTransformDirty_ = true;
    ++structureVersion_;
    anyDirty_ = true;
}

void VisualWorld::DetachFromParent(VisualNode& child) noexcept {
    if (!child.parent.IsValid()) {
        return;
    }
    if (VisualNode* oldParent = GetNode(child.parent)) {
        auto it = std::find(oldParent->children.begin(), oldParent->children.end(), child.id);
        if (it != oldParent->children.end()) {
            oldParent->children.erase(it);
        }
        MarkAncestorsDirty(oldParent->id, VisualDirty_All);
    }
    child.parent = VisualNodeId{};
}

void VisualWorld::RefreshOwnerPin(VisualNode& node) noexcept {
    node.pinnedOwnerRoot.reset();
    if (!managedHeap_ || !node.hasScriptOwner || !node.scriptOwner.valid()) {
        return;
    }
    if (managedHeap_->isAlive(node.scriptOwner)) {
        node.pinnedOwnerRoot = managedHeap_->retain(node.scriptOwner);
    }
}

bool VisualWorld::WouldCreateCycle(VisualNodeId parentId, VisualNodeId childId) const noexcept {
    VisualNodeId cur = parentId;
    std::size_t hops = 0u;
    const std::size_t maxHops = liveCount_ + 1u;
    while (cur.IsValid() && hops < maxHops) {
        if (cur == childId) {
            return true;
        }
        const VisualNode* n = GetNode(cur);
        if (!n) {
            break;
        }
        cur = n->parent;
        ++hops;
    }
    return false;
}

void VisualWorld::RefreshSubtreeDepths(VisualNodeId id, std::uint16_t depth) noexcept {
    try {
        std::vector<std::pair<VisualNodeId, std::uint16_t>> stack;
        stack.reserve(64u);
        stack.emplace_back(id, depth);
        while (!stack.empty()) {
            const auto [curId, curDepth] = stack.back();
            stack.pop_back();
            VisualNode* node = GetNode(curId);
            if (!node) {
                continue;
            }
            node->layout.treeDepth = curDepth;
            const std::uint16_t nextDepth = (curDepth < std::numeric_limits<std::uint16_t>::max())
                ? static_cast<std::uint16_t>(curDepth + 1u)
                : std::numeric_limits<std::uint16_t>::max();
            for (VisualNodeId childId : node->children) {
                stack.emplace_back(childId, nextDepth);
            }
        }
    } catch (...) {
    }
}

} // namespace aero
