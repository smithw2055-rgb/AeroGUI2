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

[[nodiscard]] bool SameFloatOrNaN(float a, float b) noexcept {
    return (std::isnan(a) && std::isnan(b)) || (a == b);
}

[[nodiscard]] bool SameThicknessExact(const Thickness& a, const Thickness& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

[[nodiscard]] bool SameCornerRadiusExact(const CornerRadius& a, const CornerRadius& b) noexcept {
    return a.topLeft == b.topLeft && a.topRight == b.topRight &&
           a.bottomRight == b.bottomRight && a.bottomLeft == b.bottomLeft;
}

void ReleaseMutableGradIfOwned(
    VisualWorld& world,
    std::int32_t& ioGradId,
    std::int32_t replacementBaseId,
    bool& ioOwnsMutable) noexcept {
    if (!ioOwnsMutable) {
        return;
    }
    if (ioGradId > 0) {
        const auto bid = static_cast<std::uint32_t>(ioGradId);
        UIResourceStore* store = world.ResourceStore();
        if (!(store != nullptr && store->GradientBrushes().ReleaseMutableBrush(bid))) {
            (void)GradientBrushRegistry::Instance().ReleaseMutableBrush(bid);
        }
    }
    ioOwnsMutable = false;
    ioGradId = replacementBaseId;
}

} // namespace

VisualWorld::VisualWorld(
    NativeHandleRegistry* handleRegistry,
    UIResourceStore* resourceStore,
    ManagedHeap* managedHeap)
    : visualNodeTypeId_(rs_sem::stableTypeId(kVisualNodeTypeName))
    , objectBoxTypeId_(rs_sem::stableTypeId("Aero.ObjectBox"))
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
        for (auto& box : objectBoxes_) {
            if (!box.alive) {
                continue;
            }
            if (box.nativeHandle.valid()) {
                (void)handleRegistry_->release(box.nativeHandle);
            }
            box.nativeHandle = handleRegistry->createBorrowed(objectBoxTypeId_, &box, false);
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
    SyncCanvasDrawRegistration(node);
    ++liveCount_;
    structureDirty_ = true;
    layoutOrTransformDirty_ = true;
    ++structureVersion_;
    anyDirty_ = true;
    return node.id;
}

void VisualWorld::SyncCanvasDrawRegistration(VisualNode& node) noexcept {
    const bool wantsCanvas = node.alive &&
        (node.kind == VisualKind::CanvasDraw || (node.callbackMask & VisualCallback_Draw) != 0u);
    if (wantsCanvas == node.inCanvasDrawList) {
        return;
    }
    if (wantsCanvas) {
        canvasDrawNodes_.push_back(node.id);
        node.inCanvasDrawList = true;
    } else {
        for (std::size_t i = 0u; i < canvasDrawNodes_.size(); ++i) {
            if (canvasDrawNodes_[i].index == node.id.index) {
                canvasDrawNodes_[i] = canvasDrawNodes_.back();
                canvasDrawNodes_.pop_back();
                break;
            }
        }
        node.inCanvasDrawList = false;
    }
}

void VisualWorld::ReleaseNodeMutableGradients(VisualNode& node) noexcept {
    if (node.box.ownsMutableBgGrad) {
        ReleaseMutableGradIfOwned(*this, node.box.bgGradId, node.box.baseBgGradId, node.box.ownsMutableBgGrad);
    }
    if (node.box.ownsMutableBorderGrad) {
        ReleaseMutableGradIfOwned(*this, node.box.borderGradId, node.box.baseBorderGradId, node.box.ownsMutableBorderGrad);
    }
    if (node.HasVectorData()) {
        auto& vec = *node.extras->vector;
        if (vec.ownsMutableFillGrad) {
            ReleaseMutableGradIfOwned(*this, vec.fillGradId, vec.baseFillGradId, vec.ownsMutableFillGrad);
        }
        if (vec.ownsMutableStrokeGrad) {
            ReleaseMutableGradIfOwned(*this, vec.strokeGradId, vec.baseStrokeGradId, vec.ownsMutableStrokeGrad);
        }
    }
}

void VisualWorld::DestroySubtreeDetached(VisualNodeId id) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) {
        return;
    }
    if (!node->children.empty()) {
        std::vector<VisualNodeId> childrenCopy;
        childrenCopy.swap(node->children);
        for (VisualNodeId childId : childrenCopy) {
            DestroySubtreeDetached(childId);
        }
    }
    if (rootId_ == id) {
        rootId_ = VisualNodeId{};
    }
    ReleaseNodeMutableGradients(*node);
    node->pinnedOwnerRoot.reset();
    node->scriptOwner = ObjectRef{};
    node->hasScriptOwner = false;
    node->callbackMask = VisualCallback_None;
    if (node->nativeHandle.valid()) {
        (void)handleRegistry_->release(node->nativeHandle);
        node->nativeHandle = NativeHandle{};
    }
    node->parent = VisualNodeId{};
    node->extras.reset();
    node->alive = false;
    SyncCanvasDrawRegistration(*node);
    node->drawDirty = false;
    node->dirtyFlags = VisualDirty_Clean;
    freeIndices_.push_back(id.index);
    if (liveCount_ > 0u) {
        --liveCount_;
    }
    ++destroyedCount_;
}

bool VisualWorld::DestroyNode(VisualNodeId id) {
    VisualNode* node = GetNode(id);
    if (!node) {
        return false;
    }
    DetachFromParent(*node);
    DestroySubtreeDetached(id);
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
    SyncCanvasDrawRegistration(*node);
    RefreshOwnerPin(*node);
    return true;
}

ObjectRef VisualWorld::GetOwner(VisualNodeId id) const noexcept {
    const VisualNode* node = GetNode(id);
    return node ? node->scriptOwner : ObjectRef{};
}

NativeHandle VisualWorld::CreateObjectBox() {
    std::uint32_t idx = 0u;
    if (!freeObjectBoxIndices_.empty()) {
        idx = freeObjectBoxIndices_.back();
        freeObjectBoxIndices_.pop_back();
        ObjectBoxSlot& slot = objectBoxes_[idx];
        const std::uint32_t nextGen = slot.generation + 1u;
        slot = ObjectBoxSlot{};
        slot.index = idx;
        slot.generation = (nextGen == 0u) ? 1u : nextGen;
    } else {
        idx = static_cast<std::uint32_t>(objectBoxes_.size());
        objectBoxes_.emplace_back();
        objectBoxes_.back().index = idx;
        objectBoxes_.back().generation = 1u;
    }
    ObjectBoxSlot& slot = objectBoxes_[idx];
    slot.alive = true;
    slot.nativeHandle = handleRegistry_->createBorrowed(objectBoxTypeId_, &slot, false);
    ++liveObjectBoxCount_;
    return slot.nativeHandle;
}

VisualWorld::ObjectBoxSlot* VisualWorld::ResolveObjectBox(NativeHandle handle) noexcept {
    if (!handle.valid()) {
        return nullptr;
    }
    void* raw = handleRegistry_->resolveRawFast(handle, objectBoxTypeId_);
    if (!raw) {
        return nullptr;
    }
    auto* slot = static_cast<ObjectBoxSlot*>(raw);
    if (!slot->alive || slot->nativeHandle != handle) {
        return nullptr;
    }
    return slot;
}

const VisualWorld::ObjectBoxSlot* VisualWorld::ResolveObjectBox(NativeHandle handle) const noexcept {
    if (!handle.valid()) {
        return nullptr;
    }
    void* raw = handleRegistry_->resolveRawFast(handle, objectBoxTypeId_);
    if (!raw) {
        return nullptr;
    }
    const auto* slot = static_cast<const ObjectBoxSlot*>(raw);
    if (!slot->alive || slot->nativeHandle != handle) {
        return nullptr;
    }
    return slot;
}

bool VisualWorld::IsObjectBoxHandle(NativeHandle handle) const noexcept {
    return ResolveObjectBox(handle) != nullptr;
}

void VisualWorld::RefreshObjectBoxPin(ObjectBoxSlot& slot) noexcept {
    slot.pinnedValueRoot.reset();
    if (!managedHeap_) {
        return;
    }
    if (const auto* ov = std::get_if<ObjectRef>(&slot.value); ov && ov->valid() && managedHeap_->isAlive(*ov)) {
        slot.pinnedValueRoot = managedHeap_->retain(*ov);
    }
}

bool VisualWorld::BindObjectBox(NativeHandle handle, const rs_rt::Value& val, std::uint32_t flags) {
    ObjectBoxSlot* slot = ResolveObjectBox(handle);
    if (!slot) {
        return false;
    }
    if (flags == 1u) {
        ObjectRef ref{};
        if (const auto* ov = std::get_if<ObjectRef>(&val); ov && ov->valid()) {
            ref = *ov;
        }
        slot->ownerBox = ref;
    } else {
        slot->value = val;
        RefreshObjectBoxPin(*slot);
    }
    return true;
}

rs_rt::Value VisualWorld::GetObjectBoxValue(NativeHandle handle) const noexcept {
    const ObjectBoxSlot* slot = ResolveObjectBox(handle);
    if (!slot) {
        return rs_rt::Value{rs_rt::NullObject{}};
    }
    if (const auto* ov = std::get_if<ObjectRef>(&slot->value)) {
        if (managedHeap_ && (!ov->valid() || !managedHeap_->isAlive(*ov))) {
            return rs_rt::Value{rs_rt::NullObject{}};
        }
    }
    return slot->value;
}

bool VisualWorld::DestroyObjectBox(NativeHandle handle) noexcept {
    ObjectBoxSlot* slot = ResolveObjectBox(handle);
    if (!slot) {
        return false;
    }
    slot->pinnedValueRoot.reset();
    if (slot->nativeHandle.valid()) {
        (void)handleRegistry_->release(slot->nativeHandle);
        slot->nativeHandle = NativeHandle{};
    }
    slot->alive = false;
    slot->value = rs_rt::Value{};
    slot->ownerBox = ObjectRef{};
    freeObjectBoxIndices_.push_back(slot->index);
    if (liveObjectBoxCount_ > 0u) {
        --liveObjectBoxCount_;
    }
    return true;
}

std::size_t VisualWorld::SweepDeadObjectBoxes() noexcept {
    if (!managedHeap_ || liveObjectBoxCount_ == 0u) {
        return 0u;
    }
    std::size_t swept = 0u;
    for (std::size_t i = 0u; i < objectBoxes_.size(); ++i) {
        ObjectBoxSlot& slot = objectBoxes_[i];
        if (!slot.alive || !slot.ownerBox.valid() || managedHeap_->isAlive(slot.ownerBox)) {
            continue;
        }
        slot.pinnedValueRoot.reset();
        if (slot.nativeHandle.valid()) {
            (void)handleRegistry_->release(slot.nativeHandle);
            slot.nativeHandle = NativeHandle{};
        }
        slot.alive = false;
        slot.value = rs_rt::Value{};
        slot.ownerBox = ObjectRef{};
        freeObjectBoxIndices_.push_back(slot.index);
        if (liveObjectBoxCount_ > 0u) {
            --liveObjectBoxCount_;
        }
        ++swept;
    }
    return swept;
}

bool VisualWorld::SetSize(VisualNodeId id, float width, float height) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (SameFloatOrNaN(node->layout.width, width) &&
        SameFloatOrNaN(node->layout.height, height)) {
        return true;
    }
    node->layout.width = width;
    node->layout.height = height;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetMinMaxSize(VisualNodeId id, float minW, float minH, float maxW, float maxH) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const float nextMinW = IsFinitePositive(minW) ? minW : 0.0f;
    const float nextMinH = IsFinitePositive(minH) ? minH : 0.0f;
    const float nextMaxW = (std::isnan(maxW) || maxW < 0.0f) ? std::numeric_limits<float>::infinity() : maxW;
    const float nextMaxH = (std::isnan(maxH) || maxH < 0.0f) ? std::numeric_limits<float>::infinity() : maxH;
    if (node->layout.minWidth == nextMinW &&
        node->layout.minHeight == nextMinH &&
        node->layout.maxWidth == nextMaxW &&
        node->layout.maxHeight == nextMaxH) {
        return true;
    }
    node->layout.minWidth = nextMinW;
    node->layout.minHeight = nextMinH;
    node->layout.maxWidth = nextMaxW;
    node->layout.maxHeight = nextMaxH;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetMargin(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const Thickness next{l, t, r, b};
    if (SameThicknessExact(node->layout.margin, next)) {
        return true;
    }
    node->layout.margin = next;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetPadding(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const Thickness next{l, t, r, b};
    if (SameThicknessExact(node->layout.padding, next)) {
        return true;
    }
    node->layout.padding = next;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetAlignment(VisualNodeId id, HorizontalAlignment hAlign, VerticalAlignment vAlign) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->layout.hAlign == hAlign && node->layout.vAlign == vAlign) {
        return true;
    }
    node->layout.hAlign = hAlign;
    node->layout.vAlign = vAlign;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetVisibility(VisualNodeId id, Visibility vis) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->layout.visibility == vis) {
        return true;
    }
    node->layout.visibility = vis;
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

bool VisualWorld::SetOpacity(VisualNodeId id, float opacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const float nextOp = std::clamp(opacity, 0.0f, 1.0f);
    if (node->layout.opacity == nextOp) {
        return true;
    }
    node->layout.opacity = nextOp;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetZIndex(VisualNodeId id, std::int32_t zIndex) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->layout.zIndex == zIndex) {
        return true;
    }
    node->layout.zIndex = zIndex;
    MarkAncestorsDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetClipToBounds(VisualNodeId id, bool clip) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->layout.clipToBounds == clip) {
        return true;
    }
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

bool VisualWorld::SetHitTestBorderOnly(
    VisualNodeId id,
    bool borderOnly) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->layout.hitTestBorderOnly = borderOnly;
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
    if (node->layout.layoutMode == mode &&
        SameFloatOrNaN(node->layout.layoutParam0, p0) &&
        SameFloatOrNaN(node->layout.layoutParam1, p1) &&
        node->layout.layoutInt0 == i0 &&
        node->layout.layoutInt1 == i1) {
        return true;
    }
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
    if (node->HasGridDefs()) {
        node->EnsureGridDefs().rowDefs.clear();
        node->EnsureGridDefs().colDefs.clear();
    }
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::GridAddRowDef(VisualNodeId id, GridUnitType unit, float value) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->EnsureGridDefs().rowDefs.push_back(VisualGridLength{unit, value});
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::GridAddColDef(VisualNodeId id, GridUnitType unit, float value) {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    node->EnsureGridDefs().colDefs.push_back(VisualGridLength{unit, value});
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
    const std::int32_t r0 = std::max<std::int32_t>(0, row);
    const std::int32_t c0 = std::max<std::int32_t>(0, col);
    const std::int32_t rs = std::max<std::int32_t>(1, rowSpan);
    const std::int32_t cs = std::max<std::int32_t>(1, colSpan);
    if (node->layout.gridRow == r0 &&
        node->layout.gridCol == c0 &&
        node->layout.gridRowSpan == rs &&
        node->layout.gridColSpan == cs) {
        return true;
    }
    node->layout.gridRow = r0;
    node->layout.gridCol = c0;
    node->layout.gridRowSpan = rs;
    node->layout.gridColSpan = cs;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetDock(VisualNodeId id, Dock dock) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->layout.dock == dock) {
        return true;
    }
    node->layout.dock = dock;
    MarkAncestorsDirty(id, VisualDirty_Layout);
    return true;
}

bool VisualWorld::SetCanvasOffsets(VisualNodeId id, float l, float t, float r, float b) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (SameFloatOrNaN(node->layout.canvasLeft, l) &&
        SameFloatOrNaN(node->layout.canvasTop, t) &&
        SameFloatOrNaN(node->layout.canvasRight, r) &&
        SameFloatOrNaN(node->layout.canvasBottom, b)) {
        return true;
    }
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
    if (node->layout.scrollOffsetX == ox && node->layout.scrollOffsetY == oy) {
        return true;
    }
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
    const Thickness nextBt{btL, btT, btR, btB};
    const CornerRadius nextCr{crTL, crTR, crBR, crBL};
    if (node->box.bgRgba == bgRgba &&
        node->box.borderRgba == borderRgba &&
        SameThicknessExact(node->box.borderThickness, nextBt) &&
        SameCornerRadiusExact(node->box.cornerRadius, nextCr)) {
        return true;
    }
    node->box.bgRgba = bgRgba;
    node->box.borderRgba = borderRgba;
    node->box.borderThickness = nextBt;
    node->box.cornerRadius = nextCr;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxGradients(VisualNodeId id, std::int32_t bgGradId, std::int32_t borderGradId) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    bool changed = false;
    if (!node->box.ownsMutableBgGrad || node->box.baseBgGradId != bgGradId) {
        ReleaseMutableGradIfOwned(*this, node->box.bgGradId, bgGradId, node->box.ownsMutableBgGrad);
        if (node->box.bgGradId != bgGradId || node->box.baseBgGradId != bgGradId) {
            node->box.bgGradId = bgGradId;
            node->box.baseBgGradId = bgGradId;
            changed = true;
        }
    }
    if (!node->box.ownsMutableBorderGrad || node->box.baseBorderGradId != borderGradId) {
        ReleaseMutableGradIfOwned(*this, node->box.borderGradId, borderGradId, node->box.ownsMutableBorderGrad);
        if (node->box.borderGradId != borderGradId || node->box.baseBorderGradId != borderGradId) {
            node->box.borderGradId = borderGradId;
            node->box.baseBorderGradId = borderGradId;
            changed = true;
        }
    }
    if (!changed) {
        return true;
    }
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxBrushOpacity(VisualNodeId id, float bgOpacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const float nextOp = std::clamp(bgOpacity, 0.0f, 1.0f);
    if (node->box.bgOpacity == nextOp) {
        return true;
    }
    node->box.bgOpacity = nextOp;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetBoxShadow(VisualNodeId id, bool isBoxShadow) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->box.isBoxShadow == isBoxShadow) {
        return true;
    }
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
    if (node->transform2D.tx == tx &&
        node->transform2D.ty == ty &&
        node->transform2D.sx == sx &&
        node->transform2D.sy == sy &&
        node->transform2D.rotDeg == rotDeg &&
        node->transform2D.skewX == skewX &&
        node->transform2D.skewY == skewY &&
        node->transform2D.originX == originX &&
        node->transform2D.originY == originY) {
        return true;
    }
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
    const float safeSx = std::isfinite(sx) ? sx : 1.0f;
    const float safeSy = std::isfinite(sy) ? sy : 1.0f;
    const float safeSz = std::isfinite(sz) ? sz : 1.0f;
    const float safeCx = std::isfinite(cx) ? cx : 0.0f;
    const float safeCy = std::isfinite(cy) ? cy : 0.0f;
    const float safeCz = std::isfinite(cz) ? cz : 0.0f;
    const bool effEnabled =
        std::fabs(rx) > 1e-6f || std::fabs(ry) > 1e-6f || std::fabs(rz) > 1e-6f ||
        std::fabs(tx) > 1e-6f || std::fabs(ty) > 1e-6f || std::fabs(tz) > 1e-6f ||
        std::fabs(safeSx - 1.0f) > 1e-6f || std::fabs(safeSy - 1.0f) > 1e-6f || std::fabs(safeSz - 1.0f) > 1e-6f ||
        camDist > 0.0f;
    if (!node->HasSpatial3D() && !effEnabled) {
        return true;
    }
    auto& t3d = node->EnsureSpatial3D().transform3D;
    if (t3d.enabled == effEnabled &&
        t3d.rx == rx && t3d.ry == ry && t3d.rz == rz &&
        t3d.tx == tx && t3d.ty == ty && t3d.tz == tz &&
        t3d.sx == safeSx && t3d.sy == safeSy && t3d.sz == safeSz &&
        t3d.cx == safeCx && t3d.cy == safeCy && t3d.cz == safeCz &&
        t3d.camDist == camDist && t3d.useExplicitCenter == useExplicitCenter) {
        return true;
    }
    t3d.rx = rx;
    t3d.ry = ry;
    t3d.rz = rz;
    t3d.tx = tx;
    t3d.ty = ty;
    t3d.tz = tz;
    t3d.sx = safeSx;
    t3d.sy = safeSy;
    t3d.sz = safeSz;
    t3d.cx = safeCx;
    t3d.cy = safeCy;
    t3d.cz = safeCz;
    t3d.camDist = camDist;
    t3d.useExplicitCenter = useExplicitCenter;
    t3d.enabled = effEnabled;
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
    if (!node->HasEffectMask() && kind == UIEffectKind::None) {
        return true;
    }
    auto& eff = node->EnsureEffectMask().effect;
    if (eff.kind == kind &&
        eff.radius == radius &&
        eff.depth == depth &&
        eff.dirDeg == dirDeg &&
        eff.colorRgba == colorRgba &&
        eff.opacity == opacity) {
        return true;
    }
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
    if (!node->HasEffectMask() && !enabled) {
        return true;
    }
    auto& om = node->EnsureEffectMask().opacityMask;
    const float nextOp = std::clamp(opacity, 0.0f, 1.0f);
    if (om.enabled == enabled &&
        (!enabled || (om.gradId == gradId && om.solidRgba == solidRgba && om.opacity == nextOp))) {
        return true;
    }
    const bool gradChanged = (om.gradId != gradId) || !enabled;
    om.enabled = enabled;
    om.gradId = gradId;
    om.solidRgba = solidRgba;
    om.opacity = nextOp;
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
    const bool hadText = node->HasTextData();
    auto& txt = node->EnsureTextData();
    const float nextSize = (fontSize > 0.0f) ? fontSize : 14.0f;
    if (hadText &&
        txt.text == text &&
        txt.fontFamily == fontFamily &&
        txt.fontSize == nextSize &&
        txt.fgRgba == fgRgba &&
        txt.wrap == wrap &&
        txt.align == align &&
        txt.trim == trim) {
        return true;
    }
    const bool layoutChanged =
        !hadText ||
        txt.text != text ||
        txt.fontFamily != fontFamily ||
        std::fabs(txt.fontSize - nextSize) > 1e-4f ||
        txt.wrap != wrap ||
        txt.trim != trim;
    if (layoutChanged) {
        txt.InvalidateLayoutCache();
    }
    txt.text.assign(text);
    txt.fontFamily.assign(fontFamily);
    txt.fontSize = nextSize;
    txt.fgRgba = fgRgba;
    txt.wrap = wrap;
    txt.align = align;
    txt.trim = trim;
    if (layoutChanged) {
        MarkAncestorsDirty(id, VisualDirty_All);
    } else {
        MarkDirty(id, VisualDirty_Visual);
    }
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
    const bool hadText = node->HasTextData();
    auto& txt = node->EnsureTextData();
    if (hadText &&
        txt.lineHeight == lineHeight &&
        txt.charSpacing == charSpacing &&
        txt.maxLines == maxLines &&
        txt.decorations == decorations) {
        return true;
    }
    const bool layoutChanged =
        !hadText ||
        std::fabs(txt.lineHeight - lineHeight) > 1e-4f ||
        std::fabs(txt.charSpacing - charSpacing) > 1e-4f ||
        txt.maxLines != maxLines;
    if (layoutChanged) {
        txt.InvalidateLayoutCache();
    }
    txt.lineHeight = lineHeight;
    txt.charSpacing = charSpacing;
    txt.maxLines = maxLines;
    txt.decorations = decorations;
    if (layoutChanged) {
        MarkAncestorsDirty(id, VisualDirty_All);
    } else {
        MarkDirty(id, VisualDirty_Visual);
    }
    return true;
}

bool VisualWorld::SetTextStyle(
    VisualNodeId id,
    int fontWeight,
    bool fontItalic,
    std::int32_t fgGradId) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const bool hadText = node->HasTextData();
    auto& txt = node->EnsureTextData();
    const int nextWeight = (fontWeight > 0) ? fontWeight : 400;
    if (hadText &&
        txt.fontWeight == nextWeight &&
        txt.fontItalic == fontItalic &&
        txt.fgGradId == fgGradId) {
        return true;
    }
    const bool layoutChanged =
        !hadText ||
        txt.fontWeight != nextWeight ||
        txt.fontItalic != fontItalic;
    if (layoutChanged) {
        txt.InvalidateLayoutCache();
    }
    txt.fontWeight = nextWeight;
    txt.fontItalic = fontItalic;
    txt.fgGradId = fgGradId;
    if (layoutChanged) {
        MarkAncestorsDirty(id, VisualDirty_All);
    } else {
        MarkDirty(id, VisualDirty_Visual);
    }
    return true;
}

bool VisualWorld::SetTextBrushOpacity(VisualNodeId id, float fgOpacity) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    const float nextOp = std::clamp(fgOpacity, 0.0f, 1.0f);
    if (!node->HasTextData() && nextOp == 1.0f) {
        return true;
    }
    auto& txt = node->EnsureTextData();
    if (txt.fgOpacity == nextOp) {
        return true;
    }
    txt.fgOpacity = nextOp;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::ClearTextSpans(VisualNodeId id) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    if (node->HasTextData() && !node->Text().richSpans.empty()) {
        auto& txt = node->EnsureTextData();
        txt.richSpans.clear();
        txt.InvalidateLayoutCache();
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
    auto& txtRef = node->EnsureTextData();
    txtRef.richSpans.push_back(std::move(span));
    txtRef.InvalidateLayoutCache();
    MarkAncestorsDirty(id, VisualDirty_All);
    return true;
}

const TextLayoutResult* VisualWorld::GetOrComputeTextLayout(
    const VisualNode& node,
    float maxWidth,
    float maxHeight,
    TextLayoutResult* optScratchFallback) const {
    const VisualTextData& txt = node.Text();
    UIResourceStore* store = const_cast<VisualWorld*>(this)->ResourceStore();
    if (!store) {
        if (!optScratchFallback) {
            return nullptr;
        }
        *optScratchFallback = TextLayoutResult{};
        const float safeFont = (txt.fontSize > 1.0f) ? txt.fontSize : 14.0f;
        optScratchFallback->totalWidth = static_cast<float>(txt.text.size()) * safeFont * 0.6f;
        optScratchFallback->totalHeight = safeFont * 1.25f;
        return optScratchFallback;
    }
    const std::uint64_t fontRev = store->Fonts().Revision();
    if (node.HasTextData() &&
        txt.hasCachedLayout &&
        txt.cachedRtl == node.layout.effectiveRtl &&
        txt.cachedFontRev == fontRev &&
        std::fabs(txt.cachedMaxW - maxWidth) <= 1e-3f &&
        (std::fabs(txt.cachedMaxH - maxHeight) <= 1e-3f ||
         (txt.trim == TextTrimming::None &&
          maxHeight >= txt.cachedLayout.totalHeight - 1e-3f &&
          txt.cachedMaxH >= txt.cachedLayout.totalHeight - 1e-3f))) {
        return &txt.cachedLayout;
    }
    TextLayoutResult computed{};
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
        computed = store->Fonts().MeasureRichLayout(
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
            txt.trim,
            maxHeight);
    } else {
        computed = store->Fonts().MeasureLayout(
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
            txt.trim,
            maxHeight);
    }
    if (node.HasTextData()) {
        txt.cachedLayout = std::move(computed);
        txt.cachedMaxW = maxWidth;
        txt.cachedMaxH = maxHeight;
        txt.cachedRtl = node.layout.effectiveRtl;
        txt.cachedFontRev = fontRev;
        txt.hasCachedLayout = true;
        return &txt.cachedLayout;
    }
    if (optScratchFallback) {
        *optScratchFallback = std::move(computed);
        return optScratchFallback;
    }
    return nullptr;
}

TextLayoutResult VisualWorld::LayoutTextNode(const VisualNode& node, float maxWidth) const {
    TextLayoutResult fallback{};
    const TextLayoutResult* res = GetOrComputeTextLayout(node, maxWidth, 1000000.0f, &fallback);
    if (!res) {
        return TextLayoutResult{};
    }
    if (res == &fallback) {
        return fallback;
    }
    return *res;
}

Size VisualWorld::MeasureTextNode(const VisualNode& node, float maxWidth) const {
    const VisualTextData& txt = node.Text();
    if (txt.text.empty() && txt.richSpans.empty()) {
        const float safeFont = std::max(1.0f, txt.fontSize);
        return Size{0.0f, (txt.lineHeight > 0.0f) ? std::max(safeFont * 1.25f, txt.lineHeight) : (safeFont * 1.25f)};
    }
    TextLayoutResult fallback{};
    const TextLayoutResult* res = GetOrComputeTextLayout(node, maxWidth, 1000000.0f, &fallback);
    return res ? Size{res->totalWidth, res->totalHeight} : Size{};
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
    if (!node->HasTextData() && selLenCp == 0 && !showCaret && caretCp < 0) {
        return true;
    }
    auto& txt = node->EnsureTextData();
    if (txt.selStartCp == selStartCp &&
        txt.selLenCp == selLenCp &&
        txt.caretCp == caretCp &&
        txt.showCaret == showCaret &&
        txt.selRgba == selRgba &&
        txt.caretRgba == caretRgba) {
        return true;
    }
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
    TextLayoutResult fallback{};
    const TextLayoutResult* layoutPtr = GetOrComputeTextLayout(*node, maxW, 1000000.0f, &fallback);
    if (!layoutPtr || layoutPtr->lines.empty()) {
        return 0;
    }
    const TextLayoutResult& layout = *layoutPtr;

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
    const bool hadImg = node->HasImageData();
    auto& img = node->EnsureImageData();
    if (hadImg &&
        img.uri == uri &&
        img.stretch == stretch &&
        img.stretchDir == stretchDir &&
        img.tintRgba == tintRgba) {
        return true;
    }
    const bool layoutChanged = !hadImg || (img.uri != uri) || (img.stretch != stretch) || (img.stretchDir != stretchDir);
    img.uri.assign(uri);
    img.stretch = stretch;
    img.stretchDir = stretchDir;
    img.tintRgba = tintRgba;
    if (layoutChanged) {
        MarkAncestorsDirty(id, VisualDirty_All);
    } else {
        MarkDirty(id, VisualDirty_Visual);
    }
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
    const float nx = std::isfinite(vx) ? vx : 0.0f;
    const float ny = std::isfinite(vy) ? vy : 0.0f;
    const float nw = std::isfinite(vw) ? vw : 1.0f;
    const float nh = std::isfinite(vh) ? vh : 1.0f;
    if (!node->HasImageData() && nx == 0.0f && ny == 0.0f && nw == 1.0f && nh == 1.0f) {
        return true;
    }
    auto& img = node->EnsureImageData();
    if (img.viewboxX == nx && img.viewboxY == ny && img.viewboxW == nw && img.viewboxH == nh) {
        return true;
    }
    img.viewboxX = nx;
    img.viewboxY = ny;
    img.viewboxW = nw;
    img.viewboxH = nh;
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
    const float nx = std::isfinite(vpx) ? vpx : 0.0f;
    const float ny = std::isfinite(vpy) ? vpy : 0.0f;
    const float nw = (std::isfinite(vpw) && vpw > 0.0f) ? vpw : 1.0f;
    const float nh = (std::isfinite(vph) && vph > 0.0f) ? vph : 1.0f;
    if (!node->HasImageData() && !hasExplicit && !isAbsolute && nx == 0.0f && ny == 0.0f && nw == 1.0f && nh == 1.0f) {
        return true;
    }
    auto& img = node->EnsureImageData();
    if (img.viewportX == nx && img.viewportY == ny && img.viewportW == nw && img.viewportH == nh &&
        img.hasExplicitViewport == hasExplicit && img.viewportAbsolute == isAbsolute) {
        return true;
    }
    img.viewportX = nx;
    img.viewportY = ny;
    img.viewportW = nw;
    img.viewportH = nh;
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
    const auto ax = static_cast<std::uint8_t>(std::clamp(alignX, 0, 2));
    const auto ay = static_cast<std::uint8_t>(std::clamp(alignY, 0, 2));
    if (!node->HasImageData() && !sourceId.IsValid() && ax == 1u && ay == 1u) {
        return true;
    }
    auto& img = node->EnsureImageData();
    if (img.visualBrushSource == sourceId && img.alignX == ax && img.alignY == ay) {
        return true;
    }
    img.visualBrushSource = sourceId;
    img.alignX = ax;
    img.alignY = ay;
    MarkDirty(id, VisualDirty_Visual);
    return true;
}

bool VisualWorld::SetImageTileMode(VisualNodeId id, bool tileWrap) noexcept {
    VisualNode* node = GetNode(id);
    if (!node) return false;
    auto& img = node->EnsureImageData();
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
    const bool firstInit = !node->HasVectorData();
    auto& vec = node->EnsureVectorData();
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
        ReleaseMutableGradIfOwned(*this, vec.fillGradId, fillGradId, vec.ownsMutableFillGrad);
        vec.fillGradId = fillGradId;
        vec.baseFillGradId = fillGradId;
    }
    vec.strokeRgba = strokeRgba;
    if (!vec.ownsMutableStrokeGrad || vec.baseStrokeGradId != strokeGradId) {
        ReleaseMutableGradIfOwned(*this, vec.strokeGradId, strokeGradId, vec.ownsMutableStrokeGrad);
        vec.strokeGradId = strokeGradId;
        vec.baseStrokeGradId = strokeGradId;
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
    const float nf = std::clamp(fillOpacity, 0.0f, 1.0f);
    const float ns = std::clamp(strokeOpacity, 0.0f, 1.0f);
    if (!node->HasVectorData() && nf == 1.0f && ns == 1.0f) {
        return true;
    }
    auto& vec = node->EnsureVectorData();
    if (vec.fillOpacity == nf && vec.strokeOpacity == ns) {
        return true;
    }
    vec.fillOpacity = nf;
    vec.strokeOpacity = ns;
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
    auto& vec = node->EnsureVectorData();
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
    SyncCanvasDrawRegistration(*node);
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
                node->EnsureVectorData().trimStart = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::PathTrimEnd:
                node->EnsureVectorData().trimEnd = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::EffectRadius:
                node->EnsureEffectMask().effect.radius = std::max(0.0f, v);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::EffectOpacity:
                node->EnsureEffectMask().effect.opacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::BrushOpacity:
                node->box.bgOpacity = std::clamp(v, 0.0f, 1.0f);
                MarkDirty(binding.targetNode, VisualDirty_Visual);
                break;
            case VisualAnimProp::FgBrushOpacity:
                node->EnsureTextData().fgOpacity = std::clamp(v, 0.0f, 1.0f);
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
    for (auto& slot : objectBoxes_) {
        if (!slot.alive) {
            continue;
        }
        slot.pinnedValueRoot.reset();
        slot.value = rs_rt::Value{};
        slot.ownerBox = ObjectRef{};
        if (slot.nativeHandle.valid()) {
            (void)handleRegistry_->release(slot.nativeHandle);
            slot.nativeHandle = NativeHandle{};
        }
    }
    nodes_.clear();
    freeIndices_.clear();
    objectBoxes_.clear();
    freeObjectBoxIndices_.clear();
    liveObjectBoxCount_ = 0u;
    canvasDrawNodes_.clear();
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
    for (const auto& slot : objectBoxes_) {
        if (slot.alive) {
            rs_rt::markValueInPools(slot.value);
        }
    }
}

void VisualWorld::RefreshScriptOwnerPins() noexcept {
    for (auto& node : nodes_) {
        if (node.alive) {
            RefreshOwnerPin(node);
        }
    }
    for (auto& slot : objectBoxes_) {
        if (slot.alive) {
            RefreshObjectBoxPin(slot);
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
        depthRefreshStack_.clear();
        if (depthRefreshStack_.capacity() < 64u) {
            depthRefreshStack_.reserve(64u);
        }
        depthRefreshStack_.emplace_back(id, depth);
        while (!depthRefreshStack_.empty()) {
            const auto [curId, curDepth] = depthRefreshStack_.back();
            depthRefreshStack_.pop_back();
            VisualNode* node = GetNode(curId);
            if (!node) {
                continue;
            }
            node->layout.treeDepth = curDepth;
            const std::uint16_t nextDepth = (curDepth < std::numeric_limits<std::uint16_t>::max())
                ? static_cast<std::uint16_t>(curDepth + 1u)
                : std::numeric_limits<std::uint16_t>::max();
            for (VisualNodeId childId : node->children) {
                depthRefreshStack_.emplace_back(childId, nextDepth);
            }
        }
    } catch (...) {
    }
}

std::size_t VisualWorld::EstimatedMemoryBytes() const noexcept {
    std::size_t total = nodes_.capacity_bytes() + objectBoxes_.capacity_bytes();
    total += freeIndices_.capacity() * sizeof(std::uint32_t);
    total += freeObjectBoxIndices_.capacity() * sizeof(std::uint32_t);
    total += canvasDrawNodes_.capacity() * sizeof(VisualNodeId);
    total += dirtyNodes_.capacity() * sizeof(VisualNodeId);
    total += ecsBindings_.capacity() * sizeof(VisualEcsColumnBinding);
    for (const auto& b : measureDepthBuckets_) {
        total += b.capacity() * sizeof(VisualNodeId);
    }
    for (const auto& b : arrangeDepthBuckets_) {
        total += b.capacity() * sizeof(VisualNodeId);
    }
    for (const auto& b : transformDepthBuckets_) {
        total += b.capacity() * sizeof(VisualNodeId);
    }
    for (std::size_t i = 0u; i < nodes_.size(); ++i) {
        const VisualNode& n = nodes_[i];
        total += n.children.capacity() * sizeof(VisualNodeId);
        if (n.extras) {
            total += sizeof(VisualNodeExtras);
            if (n.extras->spatial3D) total += sizeof(VisualSpatial3DData);
            if (n.extras->effectMask) total += sizeof(VisualEffectAndMaskData);
            if (n.extras->text) {
                total += sizeof(VisualTextData) + n.extras->text->text.capacity() +
                         n.extras->text->fontFamily.capacity() +
                         n.extras->text->richSpans.capacity() * sizeof(VisualTextSpan) +
                         n.extras->text->cachedLayout.glyphs.capacity() * sizeof(PositionedGlyph);
            }
            if (n.extras->image) total += sizeof(VisualImageData) + n.extras->image->uri.capacity();
            if (n.extras->vector) {
                total += sizeof(VisualVectorData) + n.extras->vector->pathOrParams.capacity() +
                         n.extras->vector->dashArray.capacity() * sizeof(float);
            }
            if (n.extras->gridDefs) {
                total += sizeof(VisualGridDefsData) +
                         (n.extras->gridDefs->rowDefs.capacity() + n.extras->gridDefs->colDefs.capacity()) *
                             sizeof(VisualGridLength);
            }
        }
    }
    return total;
}

} // namespace aero
