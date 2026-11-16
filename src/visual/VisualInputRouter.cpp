#include "aero/visual/VisualInputRouter.hpp"

#include <algorithm>
#include <cstring>

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

bool VisualInputRouter::DispatchPointerMove(Point position, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : HitTest(position);
    if (target != hoveredNode_) {
        NativeInputEvent leaveEv{};
        leaveEv.kind = NativeInputEventKind::PointerMove;
        leaveEv.x = position.x;
        leaveEv.y = position.y;
        leaveEv.modifiers = modifiers;
        if (hoveredNode_.IsValid() && scriptDispatcher_) {
            (void)scriptDispatcher_(hoveredNode_, VisualCallback_PointerLeave, leaveEv);
        }
        hoveredNode_ = target;
        if (hoveredNode_.IsValid() && scriptDispatcher_) {
            (void)scriptDispatcher_(hoveredNode_, VisualCallback_PointerEnter, leaveEv);
        }
    }
    if (target.IsValid() && scriptDispatcher_) {
        NativeInputEvent ev{};
        ev.kind = NativeInputEventKind::PointerMove;
        ev.x = position.x;
        ev.y = position.y;
        ev.modifiers = modifiers;
        return scriptDispatcher_(target, VisualCallback_PointerMove, ev);
    }
    return false;
}

bool VisualInputRouter::DispatchPointerDown(Point position, PointerButton button, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : HitTest(position);
    if (!target.IsValid() || !scriptDispatcher_) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = true;
    ev.x = position.x;
    ev.y = position.y;
    ev.modifiers = modifiers;
    return scriptDispatcher_(target, VisualCallback_PointerDown, ev);
}

bool VisualInputRouter::DispatchPointerUp(Point position, PointerButton button, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    const VisualNodeId target = capturedNode_.IsValid() ? capturedNode_ : HitTest(position);
    if (!target.IsValid() || !scriptDispatcher_) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = false;
    ev.x = position.x;
    ev.y = position.y;
    ev.modifiers = modifiers;
    return scriptDispatcher_(target, VisualCallback_PointerUp, ev);
}

bool VisualInputRouter::DispatchMouseWheel(
    Point position,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) {
    lastPointerPos_ = position;
    const VisualNodeId target = HitTest(position);
    if (!target.IsValid() || !scriptDispatcher_) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::MouseWheel;
    ev.x = position.x;
    ev.y = position.y;
    ev.deltaX = scrollDeltaX;
    ev.deltaY = scrollDeltaY;
    ev.modifiers = modifiers;
    return scriptDispatcher_(target, VisualCallback_MouseWheel, ev);
}

bool VisualInputRouter::DispatchKey(KeyCode key, bool isDown, std::uint8_t modifiers) {
    if (!focusedNode_.IsValid() || !scriptDispatcher_) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::Key;
    ev.key = key;
    ev.isDown = isDown;
    ev.modifiers = modifiers;
    return scriptDispatcher_(
        focusedNode_,
        isDown ? VisualCallback_KeyDown : VisualCallback_KeyUp,
        ev);
}

bool VisualInputRouter::DispatchTextInput(std::string_view utf8Text) {
    if (!focusedNode_.IsValid() || !scriptDispatcher_ || utf8Text.empty()) {
        return false;
    }
    NativeInputEvent ev{};
    ev.kind = NativeInputEventKind::TextInput;
    const std::size_t len = std::min<std::size_t>(utf8Text.size(), ev.utf8Bytes.size());
    std::memcpy(ev.utf8Bytes.data(), utf8Text.data(), len);
    ev.utf8Length = static_cast<std::uint8_t>(len);
    return scriptDispatcher_(focusedNode_, VisualCallback_TextInput, ev);
}

VisualNodeId VisualInputRouter::HitTest(Point viewportPoint) const noexcept {
    if (!world_ || !world_->Root().IsValid()) {
        return VisualNodeId{};
    }
    VisualNodeId hit{};
    auto visit = [&](auto& self, VisualNodeId id) -> void {
        const VisualNode* node = world_->GetNode(id);
        if (!node || node->layout.visibility != Visibility::Visible || !node->layout.isEnabled) {
            return;
        }
        const Rect& r = node->layout.layoutRect;
        const bool inside = viewportPoint.x >= r.x &&
                            viewportPoint.y >= r.y &&
                            viewportPoint.x <= (r.x + r.width) &&
                            viewportPoint.y <= (r.y + r.height);
        if (node->layout.clipToBounds && !inside) {
            return;
        }
        if (inside && node->layout.isHitTestVisible) {
            hit = id;
        }
        for (VisualNodeId child : node->children) {
            self(self, child);
        }
    };
    visit(visit, world_->Root());
    return hit;
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
    hoveredNode_ = VisualNodeId{};
}

bool VisualInputRouter::SetFocus(VisualNodeId node) {
    if (focusedNode_ == node) {
        return true;
    }
    const VisualNodeId oldFocus = focusedNode_;
    focusedNode_ = (world_ && world_->IsAlive(node)) ? node : VisualNodeId{};
    if (oldFocus.IsValid() && scriptDispatcher_) {
        NativeInputEvent ev{};
        (void)scriptDispatcher_(oldFocus, VisualCallback_FocusChanged, ev);
    }
    if (focusedNode_.IsValid() && scriptDispatcher_) {
        NativeInputEvent ev{};
        ev.isDown = true;
        (void)scriptDispatcher_(focusedNode_, VisualCallback_FocusChanged, ev);
    }
    return true;
}

bool VisualInputRouter::MoveFocus(FocusDirection direction) {
    (void)direction;
    return false;
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
