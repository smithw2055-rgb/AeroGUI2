#include "aero/visual/VisualInputRouter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "aero/core/UIResourceStore.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

void ImeCompositionController::AppendBoundedCommittedText(std::string& ioHistory, std::string_view committed) {
    if (committed.empty()) {
        return;
    }
    if (committed.size() >= kMaxCommittedHistoryBytes) {
        std::size_t start = committed.size() - kMaxCommittedHistoryBytes;
        while (start < committed.size() &&
               (static_cast<std::uint8_t>(committed[start]) & 0xC0u) == 0x80u) {
            ++start;
        }
        ioHistory.assign(committed.substr(start));
        return;
    }
    if (ioHistory.size() + committed.size() > kMaxCommittedHistoryBytes) {
        std::size_t drop = (ioHistory.size() + committed.size()) - kMaxCommittedHistoryBytes;
        while (drop < ioHistory.size() &&
               (static_cast<std::uint8_t>(ioHistory[drop]) & 0xC0u) == 0x80u) {
            ++drop;
        }
        ioHistory.erase(0u, drop);
    }
    ioHistory.append(committed);
}

void ImeCompositionController::BeginFocus(VisualNodeId focusedNode) {
    if (focusedNode_ != focusedNode) {
        isComposing_ = false;
        preeditText_.clear();
        committedText_.clear();
        caretOffset_ = 0u;
    }
    focusedNode_ = focusedNode;
    imeFocused_ = focusedNode.IsValid();
}

void ImeCompositionController::EndBlur() {
    focusedNode_ = VisualNodeId{};
    imeFocused_ = false;
    isComposing_ = false;
    caretOffset_ = 0u;
    preeditText_.clear();
    committedText_.clear();
}

void ImeCompositionController::BeginComposition() {
    isComposing_ = true;
}

void ImeCompositionController::UpdateComposition(std::string_view preeditText, std::uint32_t caretOffset) {
    isComposing_ = true;
    preeditText_.assign(preeditText);
    caretOffset_ = caretOffset;
}

void ImeCompositionController::CommitComposition(std::string_view committedText) {
    isComposing_ = false;
    preeditText_.clear();
    AppendBoundedCommittedText(committedText_, committedText);
}

VisualInputRouter::VisualInputRouter(
    VisualWorld* world,
    ITextInputMethodHost* imeHost,
    UIHostPlatformHooks* platformHooks) noexcept
    : world_(world)
    , imeHost_(imeHost)
    , platformHooks_(platformHooks) {}

bool VisualInputRouter::EnqueueEvent(const NativeInputEvent& ev) noexcept {
    if (queueCount_ >= kEventQueueCapacity) {
        return false;
    }
    queue_[queueTail_] = ev;
    queueTail_ = (queueTail_ + 1u) % kEventQueueCapacity;
    ++queueCount_;
    if (platformHooks_ && platformHooks_->requestWake) {
        platformHooks_->requestWake();
    }
    return true;
}

bool VisualInputRouter::QueuePointerMove(Point position, std::uint8_t modifiers) noexcept {
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerMove;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    return EnqueueEvent(ev);
}

bool VisualInputRouter::QueuePointerButton(
    Point position,
    PointerButton button,
    bool isDown,
    std::uint8_t modifiers) noexcept {
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = isDown;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    return EnqueueEvent(ev);
}

bool VisualInputRouter::QueueMouseWheel(
    Point position,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) noexcept {
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::MouseWheel;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    ev.deltaX = scrollDeltaX;
    ev.deltaY = scrollDeltaY;
    return EnqueueEvent(ev);
}

bool VisualInputRouter::QueueKey(KeyCode key, bool isDown, std::uint8_t modifiers) noexcept {
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::Key;
    ev.key = key;
    ev.isDown = isDown;
    ev.modifiers = modifiers;
    return EnqueueEvent(ev);
}

bool VisualInputRouter::QueueTextInput(std::string_view utf8Text) noexcept {
    if (utf8Text.empty()) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::TextInput;
    const std::size_t len = std::min<std::size_t>(utf8Text.size(), ev.utf8Bytes.size());
    std::memcpy(ev.utf8Bytes.data(), utf8Text.data(), len);
    ev.utf8Length = static_cast<std::uint8_t>(len);
    return EnqueueEvent(ev);
}

std::size_t VisualInputRouter::FlushQueuedEvents() {
    std::size_t processed = 0u;
    while (queueCount_ > 0u) {
        const NativeInputEvent ev = queue_[queueHead_];
        queueHead_ = (queueHead_ + 1u) % kEventQueueCapacity;
        --queueCount_;
        ++processed;
        switch (ev.kind) {
        case NativeInputEventKind::PointerMove:
            (void)DispatchPointerMove(Point{ev.x, ev.y}, ev.modifiers);
            break;
        case NativeInputEventKind::PointerButton:
            if (ev.isDown) {
                (void)DispatchPointerDown(Point{ev.x, ev.y}, ev.button, ev.modifiers);
            } else {
                (void)DispatchPointerUp(Point{ev.x, ev.y}, ev.button, ev.modifiers);
            }
            break;
        case NativeInputEventKind::MouseWheel:
            (void)DispatchMouseWheel(Point{ev.x, ev.y}, ev.deltaX, ev.deltaY, ev.modifiers);
            break;
        case NativeInputEventKind::Key:
            (void)DispatchKey(ev.key, ev.isDown, ev.modifiers);
            break;
        case NativeInputEventKind::TextInput:
            (void)DispatchTextInput(std::string_view(ev.utf8Bytes.data(), ev.utf8Length));
            break;
        }
    }
    return processed;
}

namespace {

[[nodiscard]] std::vector<VisualNodeId> SortedChildrenByZOrder(
    const VisualWorld& world,
    const std::vector<VisualNodeId>& children) {
    struct ZEntry {
        VisualNodeId id{};
        std::int32_t zIndex = 0;
        std::size_t order = 0u;
    };
    std::vector<ZEntry> sorted;
    sorted.reserve(children.size());
    for (std::size_t i = 0u; i < children.size(); ++i) {
        const VisualNode* child = world.GetNode(children[i]);
        if (child) {
            sorted.push_back(ZEntry{children[i], child->layout.zIndex, i});
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const ZEntry& a, const ZEntry& b) {
        if (a.zIndex != b.zIndex) {
            return a.zIndex < b.zIndex;
        }
        return a.order < b.order;
    });
    std::vector<VisualNodeId> out;
    out.reserve(sorted.size());
    for (const ZEntry& e : sorted) {
        out.push_back(e.id);
    }
    return out;
}

[[nodiscard]] VisualNodeId HitTestNodeRecursive(
    const VisualWorld& world,
    VisualNodeId id,
    Point viewportPoint) noexcept {
    const VisualNode* node = world.GetNode(id);
    if (!node || node->layout.visibility != Visibility::Visible || !node->layout.isEnabled) {
        return VisualNodeId{};
    }

    Point localPt{};
    if (!world.TryScreenToLocal(id, viewportPoint, localPt)) {
        return VisualNodeId{};
    }
    const bool inside = localPt.x >= 0.0f &&
                        localPt.y >= 0.0f &&
                        localPt.x <= node->layout.layoutRect.width &&
                        localPt.y <= node->layout.layoutRect.height;
    const bool clipsChildren =
        node->layout.clipToBounds || node->layout.needsLayoutClip ||
        (node->layout.layoutMode == LayoutMode::ScrollViewport);
    if (clipsChildren && !inside) {
        return VisualNodeId{};
    }

    if (!node->children.empty()) {
        const std::vector<VisualNodeId> paintOrder = SortedChildrenByZOrder(world, node->children);
        for (std::size_t i = paintOrder.size(); i > 0u; --i) {
            const VisualNodeId childHit = HitTestNodeRecursive(world, paintOrder[i - 1u], viewportPoint);
            if (childHit.IsValid()) {
                return childHit;
            }
        }
    }

    if (inside && node->layout.isHitTestVisible) {
        if (node->kind == VisualKind::Image) {
            const VisualImageData& img = node->Image();
            if (img.uri.size() >= 7u && img.uri.substr(0u, 7u) == "rive://") {
                if (const UIResourceStore* store = world.ResourceStore()) {
                    const ImageAtlas& atlas = store->Images();
                    if (const ImageAtlasEntry* entry = atlas.FindLoaded(img.uri)) {
                        const Rect localBox{0.0f, 0.0f, node->layout.layoutRect.width, node->layout.layoutRect.height};
                        const ImageStretchPlacement place = ComputeImageStretchPlacement(
                            localBox,
                            static_cast<float>(entry->width),
                            static_cast<float>(entry->height),
                            img.stretch,
                            img.stretchDir,
                            entry->u0, entry->v0, entry->u1, entry->v1);
                        if (!place.valid ||
                            localPt.x < place.destRect.x ||
                            localPt.y < place.destRect.y ||
                            localPt.x > place.destRect.x + place.destRect.width ||
                            localPt.y > place.destRect.y + place.destRect.height ||
                            place.destRect.width <= 1e-4f ||
                            place.destRect.height <= 1e-4f) {
                            return VisualNodeId{};
                        }
                        const float rx = (localPt.x - place.destRect.x) / place.destRect.width;
                        const float ry = (localPt.y - place.destRect.y) / place.destRect.height;
                        const float u = place.u0 + rx * (place.u1 - place.u0);
                        const float v = place.v0 + ry * (place.v1 - place.v0);
                        const Color px = atlas.SampleBilinearRgba(entry->textureId, u, v);
                        if (px.a < 0.03f) {
                            return VisualNodeId{};
                        }
                    }
                }
            }
        }
        return id;
    }
    return VisualNodeId{};
}

[[nodiscard]] Point NodeViewportCenter(const VisualNode& node) noexcept {
    const Rect& r = node.layout.layoutRect;
    const Point untransformed{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    if (node.layout.hasPerspective3D) {
        return node.WorldMatrix3D().ProjectPoint2D(untransformed);
    }
    return node.layout.worldTransform.TransformPoint(untransformed);
}

void CollectFocusablesRecursive(
    const VisualWorld& world,
    VisualNodeId id,
    std::vector<VisualNodeId>& outFocusables) {
    const VisualNode* node = world.GetNode(id);
    if (!node || node->layout.visibility != Visibility::Visible || !node->layout.isEnabled) {
        return;
    }
    if (node->layout.isFocusable) {
        outFocusables.push_back(id);
    }
    if (!node->children.empty()) {
        const std::vector<VisualNodeId> paintOrder = SortedChildrenByZOrder(world, node->children);
        for (VisualNodeId childId : paintOrder) {
            CollectFocusablesRecursive(world, childId, outFocusables);
        }
    }
}

[[nodiscard]] bool DispatchWithCallbackMask(
    VisualWorld* world,
    const VisualInputRouter::ScriptEventDispatchFn& dispatcher,
    VisualNodeId startTarget,
    VisualCallbackFlags flag,
    const NativeInputEvent& ev,
    bool allowBubble) {
    if (!dispatcher || !world || !startTarget.IsValid()) {
        return false;
    }

    bool anyExplicitMaskInChain = false;
    VisualNodeId cur = startTarget;
    std::size_t hops = 0u;
    const std::size_t maxHops = world->LiveNodeCount() + 1u;
    while (cur.IsValid() && hops < maxHops) {
        const VisualNode* node = world->GetNode(cur);
        if (!node) {
            break;
        }
        if (node->hasScriptOwner || node->callbackMask != VisualCallback_None) {
            anyExplicitMaskInChain = true;
            if ((node->callbackMask & flag) != 0u) {
                if (dispatcher(cur, flag, ev)) {
                    return true;
                }
            }
        }
        if (!allowBubble) {
            break;
        }
        cur = node->parent;
        ++hops;
    }

    if (!anyExplicitMaskInChain) {
        return dispatcher(startTarget, flag, ev);
    }
    return false;
}

[[nodiscard]] bool IsNodeDescendantOrSelf(
    const VisualWorld* world,
    VisualNodeId ancestor,
    VisualNodeId candidate) noexcept {
    if (!world || !ancestor.IsValid() || !candidate.IsValid()) {
        return false;
    }
    VisualNodeId cur = candidate;
    std::size_t hops = 0u;
    const std::size_t maxHops = world->LiveNodeCount() + 1u;
    while (cur.IsValid() && hops < maxHops) {
        if (cur == ancestor) {
            return true;
        }
        const VisualNode* n = world->GetNode(cur);
        if (!n) {
            break;
        }
        cur = n->parent;
        ++hops;
    }
    return false;
}

void SyncHoverTransition(
    VisualWorld* world,
    const VisualInputRouter::ScriptEventDispatchFn& dispatcher,
    VisualNodeId& hoveredNode,
    VisualNodeId capturedNode,
    VisualNodeId rawHit,
    Point position,
    std::uint8_t modifiers) {
    const VisualNodeId effectiveHover = capturedNode.IsValid()
        ? (IsNodeDescendantOrSelf(world, capturedNode, rawHit) ? rawHit : VisualNodeId{})
        : rawHit;
    if (effectiveHover == hoveredNode) {
        return;
    }
    NativeInputEvent transitionEv{};
    transitionEv.kind = NativeInputEventKind::PointerMove;
    transitionEv.x = position.x;
    transitionEv.y = position.y;
    transitionEv.modifiers = modifiers;
    // deltaX == 1.0 signals that PointerEnter immediately follows so LCA ancestor hover does not flicker.
    transitionEv.deltaX = effectiveHover.IsValid() ? 1.0f : 0.0f;
    bool dispatchedLeave = false;
    if (hoveredNode.IsValid()) {
        dispatchedLeave = DispatchWithCallbackMask(
            world, dispatcher, hoveredNode, VisualCallback_PointerLeave, transitionEv, false);
    }
    hoveredNode = effectiveHover;
    if (hoveredNode.IsValid()) {
        transitionEv.deltaX = 0.0f;
        (void)DispatchWithCallbackMask(
            world, dispatcher, hoveredNode, VisualCallback_PointerEnter, transitionEv, false);
    } else if (!dispatchedLeave && dispatcher) {
        transitionEv.deltaX = 0.0f;
        (void)dispatcher(VisualNodeId{}, VisualCallback_PointerLeave, transitionEv);
    }
}

} // namespace

bool VisualInputRouter::DispatchPointerMove(Point position, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (world_ && hoveredNode_.IsValid() && !world_->IsAlive(hoveredNode_)) {
        hoveredNode_ = VisualNodeId{};
    }
    if (world_ && capturedNode_.IsValid() && !world_->IsAlive(capturedNode_)) {
        capturedNode_ = VisualNodeId{};
    }
    const VisualNodeId rawHit = HitTest(position);
    SyncHoverTransition(world_, scriptDispatcher_, hoveredNode_, capturedNode_, rawHit, position, modifiers);
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : rawHit;
    if (target.IsValid()) {
        NativeInputEvent ev{};
        ev.kind = NativeInputEventKind::PointerMove;
        ev.x = position.x;
        ev.y = position.y;
        ev.modifiers = modifiers;
        return DispatchWithCallbackMask(
            world_, scriptDispatcher_, target, VisualCallback_PointerMove, ev, true);
    }
    return false;
}

bool VisualInputRouter::DispatchPointerDown(Point position, PointerButton button, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (world_ && hoveredNode_.IsValid() && !world_->IsAlive(hoveredNode_)) {
        hoveredNode_ = VisualNodeId{};
    }
    if (world_ && capturedNode_.IsValid() && !world_->IsAlive(capturedNode_)) {
        capturedNode_ = VisualNodeId{};
    }
    const VisualNodeId rawHit = HitTest(position);
    SyncHoverTransition(world_, scriptDispatcher_, hoveredNode_, capturedNode_, rawHit, position, modifiers);
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : rawHit;
    if (!target.IsValid()) {
        if (scriptDispatcher_) {
            NativeInputEvent ev{};
            ev.kind = NativeInputEventKind::PointerButton;
            ev.button = button;
            ev.isDown = true;
            ev.x = position.x;
            ev.y = position.y;
            ev.modifiers = modifiers;
            return scriptDispatcher_(VisualNodeId{}, VisualCallback_PointerDown, ev);
        }
        return false;
    }

    if (button == PointerButton::Left && world_) {
        VisualNodeId cur = target;
        std::size_t hops = 0u;
        const std::size_t maxHops = world_->LiveNodeCount() + 1u;
        while (cur.IsValid() && hops < maxHops) {
            const VisualNode* n = world_->GetNode(cur);
            if (!n) break;
            if (n->layout.isFocusable && n->layout.isEnabled && n->layout.visibility == Visibility::Visible) {
                (void)SetFocus(cur);
                break;
            }
            cur = n->parent;
            ++hops;
        }
    }

    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = true;
    ev.x = position.x;
    ev.y = position.y;
    ev.modifiers = modifiers;
    return DispatchWithCallbackMask(
        world_, scriptDispatcher_, target, VisualCallback_PointerDown, ev, true);
}

bool VisualInputRouter::DispatchPointerUp(Point position, PointerButton button, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (world_ && hoveredNode_.IsValid() && !world_->IsAlive(hoveredNode_)) {
        hoveredNode_ = VisualNodeId{};
    }
    if (world_ && capturedNode_.IsValid() && !world_->IsAlive(capturedNode_)) {
        capturedNode_ = VisualNodeId{};
    }
    const VisualNodeId rawHit = HitTest(position);
    SyncHoverTransition(world_, scriptDispatcher_, hoveredNode_, capturedNode_, rawHit, position, modifiers);
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = false;
    ev.x = position.x;
    ev.y = position.y;
    ev.modifiers = modifiers;
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : rawHit;
    bool handled = false;
    if (!target.IsValid()) {
        if (scriptDispatcher_) {
            handled = scriptDispatcher_(VisualNodeId{}, VisualCallback_PointerUp, ev);
        }
    } else {
        handled = DispatchWithCallbackMask(
            world_, scriptDispatcher_, target, VisualCallback_PointerUp, ev, true);
    }
    if (world_ && capturedNode_.IsValid() && !world_->IsAlive(capturedNode_)) {
        capturedNode_ = VisualNodeId{};
    }
    if (!capturedNode_.IsValid()) {
        if (world_ && hoveredNode_.IsValid() && !world_->IsAlive(hoveredNode_)) {
            hoveredNode_ = VisualNodeId{};
        }
        SyncHoverTransition(
            world_, scriptDispatcher_, hoveredNode_, VisualNodeId{}, HitTest(position), position, modifiers);
    }
    return handled;
}

bool VisualInputRouter::DispatchMouseWheel(
    Point position,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) {
    lastPointerPos_ = position;
    const VisualNodeId target = HitTest(position);
    if (!target.IsValid()) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::MouseWheel;
    ev.x = position.x;
    ev.y = position.y;
    ev.deltaX = scrollDeltaX;
    ev.deltaY = scrollDeltaY;
    ev.modifiers = modifiers;
    return DispatchWithCallbackMask(
        world_, scriptDispatcher_, target, VisualCallback_MouseWheel, ev, true);
}

bool VisualInputRouter::DispatchKey(KeyCode key, bool isDown, std::uint8_t modifiers) {
    if (world_ && focusedNode_.IsValid() && !world_->IsAlive(focusedNode_)) {
        focusedNode_ = VisualNodeId{};
    }
    bool handled = false;
    const VisualNodeId keyTarget = focusedNode_.IsValid()
        ? focusedNode_
        : ((world_ && world_->Root().IsValid()) ? world_->Root() : VisualNodeId{});
    if (keyTarget.IsValid()) {
        NativeInputEvent ev{};
        ev.kind = NativeInputEventKind::Key;
        ev.key = key;
        ev.isDown = isDown;
        ev.modifiers = modifiers;
        handled = DispatchWithCallbackMask(
            world_,
            scriptDispatcher_,
            keyTarget,
            isDown ? VisualCallback_KeyDown : VisualCallback_KeyUp,
            ev,
            true);
    }
    if (!handled && isDown && key == KeyCode::Tab) {
        const FocusDirection dir = (modifiers & KeyMod_Shift) != 0u
            ? FocusDirection::Previous
            : FocusDirection::Next;
        return MoveFocus(dir);
    }
    return handled;
}

bool VisualInputRouter::DispatchTextInput(std::string_view utf8Text) {
    if (world_ && focusedNode_.IsValid() && !world_->IsAlive(focusedNode_)) {
        focusedNode_ = VisualNodeId{};
    }
    if (!focusedNode_.IsValid() || utf8Text.empty()) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::TextInput;
    const std::size_t len = std::min<std::size_t>(utf8Text.size(), ev.utf8Bytes.size());
    std::memcpy(ev.utf8Bytes.data(), utf8Text.data(), len);
    ev.utf8Length = static_cast<std::uint8_t>(len);
    return DispatchWithCallbackMask(
        world_, scriptDispatcher_, focusedNode_, VisualCallback_TextInput, ev, true);
}

VisualNodeId VisualInputRouter::HitTest(Point viewportPoint) const noexcept {
    if (!world_ || !world_->Root().IsValid()) {
        return VisualNodeId{};
    }
    return HitTestNodeRecursive(*world_, world_->Root(), viewportPoint);
}

void VisualInputRouter::SetPointerCapture(VisualNodeId node) noexcept {
    capturedNode_ = (world_ && world_->IsAlive(node)) ? node : VisualNodeId{};
    if (platformHooks_ && platformHooks_->setPointerCapture && capturedNode_.IsValid()) {
        platformHooks_->setPointerCapture(capturedNode_);
    }
}

void VisualInputRouter::ReleasePointerCapture(VisualNodeId node) noexcept {
    if (!node.IsValid() || capturedNode_ == node) {
        const VisualNodeId prev = capturedNode_;
        capturedNode_ = VisualNodeId{};
        if (platformHooks_ && platformHooks_->releasePointerCapture) {
            platformHooks_->releasePointerCapture(prev);
        }
    }
}

void VisualInputRouter::CancelPointerInteraction() noexcept {
    if (capturedNode_.IsValid()) {
        ReleasePointerCapture(capturedNode_);
    }
    if (hoveredNode_.IsValid() && world_ && world_->IsAlive(hoveredNode_)) {
        NativeInputEvent leaveEv{};
        leaveEv.kind = NativeInputEventKind::PointerMove;
        (void)DispatchWithCallbackMask(
            world_, scriptDispatcher_, hoveredNode_, VisualCallback_PointerLeave, leaveEv, false);
    }
    hoveredNode_ = VisualNodeId{};
}

bool VisualInputRouter::SetFocus(VisualNodeId node) {
    const VisualNodeId sanitized = (world_ && world_->IsAlive(node)) ? node : VisualNodeId{};
    if (focusedNode_ == sanitized) {
        return true;
    }
    const VisualNodeId oldFocus = focusedNode_;
    focusedNode_ = sanitized;
    if (oldFocus.IsValid() && world_ && world_->IsAlive(oldFocus)) {
        NativeInputEvent ev{};
        (void)DispatchWithCallbackMask(
            world_, scriptDispatcher_, oldFocus, VisualCallback_FocusChanged, ev, false);
    }
    if (focusedNode_.IsValid()) {
        NativeInputEvent ev{};
        ev.isDown = true;
        (void)DispatchWithCallbackMask(
            world_, scriptDispatcher_, focusedNode_, VisualCallback_FocusChanged, ev, false);
    } else if (imeActive_) {
        SyncImeCaretRect(Rect{}, false);
    }
    return true;
}

bool VisualInputRouter::MoveFocus(FocusDirection direction) {
    if (!world_ || !world_->Root().IsValid()) {
        return false;
    }
    std::vector<VisualNodeId> focusables;
    CollectFocusablesRecursive(*world_, world_->Root(), focusables);
    if (focusables.empty()) {
        return false;
    }

    auto it = std::find(focusables.begin(), focusables.end(), focusedNode_);
    if (direction == FocusDirection::Next) {
        if (it == focusables.end()) {
            return SetFocus(focusables.front());
        }
        const std::size_t idx = static_cast<std::size_t>(it - focusables.begin());
        return SetFocus(focusables[(idx + 1u) % focusables.size()]);
    }
    if (direction == FocusDirection::Previous) {
        if (it == focusables.end()) {
            return SetFocus(focusables.back());
        }
        const std::size_t idx = static_cast<std::size_t>(it - focusables.begin());
        const std::size_t prevIdx = (idx == 0u) ? (focusables.size() - 1u) : (idx - 1u);
        return SetFocus(focusables[prevIdx]);
    }

    if (it == focusables.end()) {
        return SetFocus(focusables.front());
    }
    const VisualNode* curNode = world_->GetNode(focusedNode_);
    if (!curNode) {
        return SetFocus(focusables.front());
    }
    const Point curCenter = NodeViewportCenter(*curNode);

    VisualNodeId bestId{};
    float bestScore = std::numeric_limits<float>::infinity();
    for (VisualNodeId candId : focusables) {
        if (candId == focusedNode_) {
            continue;
        }
        const VisualNode* candNode = world_->GetNode(candId);
        if (!candNode) {
            continue;
        }
        const Point candCenter = NodeViewportCenter(*candNode);
        const float dx = candCenter.x - curCenter.x;
        const float dy = candCenter.y - curCenter.y;
        float primary = 0.0f;
        float cross = 0.0f;
        bool validDir = false;
        switch (direction) {
            case FocusDirection::Left:
                validDir = (dx < -1e-3f);
                primary = -dx;
                cross = std::fabs(dy);
                break;
            case FocusDirection::Right:
                validDir = (dx > 1e-3f);
                primary = dx;
                cross = std::fabs(dy);
                break;
            case FocusDirection::Up:
                validDir = (dy < -1e-3f);
                primary = -dy;
                cross = std::fabs(dx);
                break;
            case FocusDirection::Down:
                validDir = (dy > 1e-3f);
                primary = dy;
                cross = std::fabs(dx);
                break;
            default:
                break;
        }
        if (!validDir) {
            continue;
        }
        const float score = primary * primary + 4.0f * cross * cross;
        if (score < bestScore) {
            bestScore = score;
            bestId = candId;
        }
    }
    return bestId.IsValid() ? SetFocus(bestId) : false;
}

void VisualInputRouter::SyncImeCaretRect(Rect caretScreenRect, bool active) noexcept {
    imeActive_ = active;
    imeCaretRect_ = caretScreenRect;
    if (imeHost_) {
        if (active && focusedNode_.IsValid()) {
            imeHost_->FocusTextInput(focusedNode_, caretScreenRect);
        } else if (!active) {
            imeHost_->BlurTextInput();
        }
    }
    if (platformHooks_ && platformHooks_->syncImeRect) {
        platformHooks_->syncImeRect(caretScreenRect, active);
    }
}

std::string VisualInputRouter::GetClipboardText() const {
    if (platformHooks_ && platformHooks_->getClipboardText) {
        return platformHooks_->getClipboardText();
    }
    return {};
}

void VisualInputRouter::SetClipboardText(std::string_view text) {
    if (platformHooks_ && platformHooks_->setClipboardText) {
        platformHooks_->setClipboardText(text);
    }
}

} // namespace aero
