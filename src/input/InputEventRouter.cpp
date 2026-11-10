#include "aero/input/InputEventRouter.hpp"
#include "aero/animation/AnimationEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "aero/host/HostDrivers.hpp"

namespace aero {

namespace {

constexpr std::size_t kMaxFocusableScan = 256u;

void CollectFocusablePreOrder(
    UIElement* node,
    std::array<UIElement*, kMaxFocusableScan>& outList,
    std::size_t& ioCount) noexcept {
    if (!node || ioCount >= kMaxFocusableScan) {
        return;
    }
    if (node->GetVisibility() != Visibility::Visible ||
        node->GetOpacity() <= 0.0f ||
        !node->IsEnabled()) {
        return;
    }
    if (node->CanReceiveFocus()) {
        outList[ioCount++] = node;
    }
    for (UIElement* c = node->FirstChild(); c != nullptr && ioCount < kMaxFocusableScan; c = c->NextSibling()) {
        CollectFocusablePreOrder(c, outList, ioCount);
    }
}

[[nodiscard]] bool PointInsideElementLocalBounds(const UIElement* elem, Point localPt) noexcept {
    if (!elem) {
        return false;
    }
    const Rect r = elem->FinalRect();
    return r.width > 0.0f && r.height > 0.0f &&
           localPt.x >= r.x && localPt.x <= r.x + r.width &&
           localPt.y >= r.y && localPt.y <= r.y + r.height;
}

[[nodiscard]] bool IsEffectivelyVisibleInTree(
    const UIElement* elem,
    const UIElement* rootVisual) noexcept {
    if (!elem || !rootVisual) {
        return false;
    }
    for (const UIElement* cur = elem; cur != nullptr; cur = cur->Parent()) {
        if (cur->GetVisibility() != Visibility::Visible || cur->GetOpacity() <= 0.0f) {
            return false;
        }
        if (cur == rootVisual) {
            return true;
        }
    }
    return false;
}

} // namespace

InputEventRouter::InputEventRouter(
    UIElement* rootVisual,
    ITextInputMethodHost* imeHost) noexcept
    : rootVisual_(rootVisual),
      imeHost_(imeHost) {}

void InputEventRouter::SetRootVisual(UIElement* root) noexcept {
    if (rootVisual_ != root) {
        CancelPointerInteraction();
        ClearFocus();
        CloseActiveComboBoxIfOpen();
        UpdateHoveredTarget(nullptr);
        rootVisual_ = root;
    }
}

bool InputEventRouter::EnqueueEvent(const QueuedInputEvent& ev) noexcept {
    if (queueCount_ >= kEventQueueCapacity) {
        // Coalesce consecutive PointerMove events at the tail if ring buffer is full
        if (ev.kind == InputEventKind::PointerMove && queueCount_ > 0u) {
            const std::size_t lastIdx = (queueHead_ + queueCount_ - 1u) % kEventQueueCapacity;
            if (eventRing_[lastIdx].kind == InputEventKind::PointerMove) {
                eventRing_[lastIdx] = ev;
                return true;
            }
        }
        return false;
    }
    const std::size_t writeIdx = (queueHead_ + queueCount_) % kEventQueueCapacity;
    eventRing_[writeIdx] = ev;
    ++queueCount_;
    return true;
}

bool InputEventRouter::QueuePointerMove(Point position, std::uint8_t modifiers) noexcept {
    if (queueCount_ > 0u) {
        const std::size_t lastIdx = (queueHead_ + queueCount_ - 1u) % kEventQueueCapacity;
        if (eventRing_[lastIdx].kind == InputEventKind::PointerMove &&
            eventRing_[lastIdx].modifiers == modifiers) {
            eventRing_[lastIdx].x = position.x;
            eventRing_[lastIdx].y = position.y;
            return true;
        }
    }
    QueuedInputEvent ev{};
    ev.kind = InputEventKind::PointerMove;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    return EnqueueEvent(ev);
}

bool InputEventRouter::QueuePointerButton(
    Point position,
    PointerButton button,
    bool isDown,
    std::uint8_t modifiers) noexcept {
    QueuedInputEvent ev{};
    ev.kind = InputEventKind::PointerButton;
    ev.button = button;
    ev.isDown = isDown;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    return EnqueueEvent(ev);
}

bool InputEventRouter::QueueMouseWheel(
    Point position,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) noexcept {
    QueuedInputEvent ev{};
    ev.kind = InputEventKind::MouseWheel;
    ev.modifiers = modifiers;
    ev.x = position.x;
    ev.y = position.y;
    ev.deltaX = scrollDeltaX;
    ev.deltaY = scrollDeltaY;
    return EnqueueEvent(ev);
}

bool InputEventRouter::QueueKey(KeyCode key, bool isDown, std::uint8_t modifiers) noexcept {
    QueuedInputEvent ev{};
    ev.kind = InputEventKind::Key;
    ev.key = key;
    ev.isDown = isDown;
    ev.modifiers = modifiers;
    return EnqueueEvent(ev);
}

bool InputEventRouter::QueueTextInput(std::string_view utf8Text) noexcept {
    if (utf8Text.empty()) {
        return true;
    }
    bool allQueued = true;
    std::size_t offset = 0u;
    while (offset < utf8Text.size()) {
        // Split cleanly at UTF-8 codepoint boundaries within 28-byte inline chunks (0 heap allocs!)
        std::size_t chunkEnd = offset;
        while (chunkEnd < utf8Text.size()) {
            std::size_t nextCursor = chunkEnd;
            std::uint32_t dummyCp = 0;
            if (!DecodeNextUtf8CodePoint(utf8Text, nextCursor, dummyCp)) {
                break;
            }
            if (nextCursor - offset > 28u) {
                break;
            }
            chunkEnd = nextCursor;
        }
        if (chunkEnd == offset) {
            break;
        }
        QueuedInputEvent ev{};
        ev.kind = InputEventKind::TextInput;
        ev.utf8Length = static_cast<std::uint8_t>(chunkEnd - offset);
        std::memcpy(ev.utf8Bytes.data(), utf8Text.data() + offset, ev.utf8Length);
        if (!EnqueueEvent(ev)) {
            allQueued = false;
            break;
        }
        offset = chunkEnd;
    }
    return allQueued;
}

std::size_t InputEventRouter::FlushQueuedEvents() {
    std::size_t processed = 0u;
    while (queueCount_ > 0u) {
        const QueuedInputEvent ev = eventRing_[queueHead_];
        queueHead_ = (queueHead_ + 1u) % kEventQueueCapacity;
        --queueCount_;
        ++processed;

        switch (ev.kind) {
            case InputEventKind::PointerMove:
                (void)DispatchPointerMove(Point{ev.x, ev.y}, ev.modifiers);
                break;
            case InputEventKind::PointerButton:
                if (ev.isDown) {
                    (void)DispatchPointerDown(Point{ev.x, ev.y}, ev.button, ev.modifiers);
                } else {
                    (void)DispatchPointerUp(Point{ev.x, ev.y}, ev.button, ev.modifiers);
                }
                break;
            case InputEventKind::MouseWheel:
                (void)DispatchMouseWheel(Point{ev.x, ev.y}, ev.deltaX, ev.deltaY, ev.modifiers);
                break;
            case InputEventKind::Key:
                if (ev.isDown) {
                    (void)DispatchKeyDown(ev.key, ev.modifiers);
                } else {
                    (void)DispatchKeyUp(ev.key, ev.modifiers);
                }
                break;
            case InputEventKind::TextInput:
                (void)DispatchTextInput(std::string_view(ev.utf8Bytes.data(), ev.utf8Length));
                break;
        }
    }
    return processed;
}

bool InputEventRouter::BelongsToRoot(const UIElement* elem) const noexcept {
    if (!elem || !rootVisual_) {
        return false;
    }
    for (const UIElement* cur = elem; cur != nullptr; cur = cur->Parent()) {
        if (cur == rootVisual_) {
            return true;
        }
    }
    return false;
}

void InputEventRouter::PruneDisconnectedTargets() noexcept {
    if (hoveredElement_ && (!IsEffectivelyVisibleInTree(hoveredElement_.Get(), rootVisual_) ||
                            !hoveredElement_->IsEffectivelyEnabled())) {
        hoveredElement_->SetHovered(false);
        hoveredElement_.Reset();
    }
    if (capturedElement_ && (!IsEffectivelyVisibleInTree(capturedElement_.Get(), rootVisual_) ||
                             !capturedElement_->IsEffectivelyEnabled())) {
        capturedElement_->SetPressed(false);
        capturedElement_.Reset();
    }
    if (focusedElement_ && (!IsEffectivelyVisibleInTree(focusedElement_.Get(), rootVisual_) ||
                            !focusedElement_->CanReceiveFocus())) {
        focusedElement_->SetFocused(false);
        focusedElement_.Reset();
        if (imeHost_) {
            imeHost_->BlurTextInput();
        }
    }
    if (openComboBox_ && (!IsEffectivelyVisibleInTree(openComboBox_.Get(), rootVisual_) ||
                          !openComboBox_->IsDropDownOpen() ||
                          !openComboBox_->IsEffectivelyEnabled())) {
        if (openComboBox_->IsDropDownOpen()) {
            openComboBox_->SetIsDropDownOpen(false);
        }
        openComboBox_->SetHighlightedPopupIndex(-1);
        openComboBox_.Reset();
    }
}

UIElement* InputEventRouter::ResolveInteractiveTarget(UIElement* hitLeaf) const noexcept {
    if (!hitLeaf) {
        return nullptr;
    }
    for (UIElement* cur = hitLeaf; cur != nullptr; cur = cur->Parent()) {
        if (cur->IsInteractiveControl()) {
            return cur;
        }
        if (cur == rootVisual_) {
            break;
        }
    }
    return hitLeaf;
}

void InputEventRouter::UpdateHoveredTarget(UIElement* newHovered) noexcept {
    if (newHovered && !newHovered->IsEffectivelyEnabled()) {
        newHovered = nullptr;
    }
    if (hoveredElement_.Get() == newHovered) {
        return;
    }
    if (hoveredElement_) {
        hoveredElement_->SetHovered(false);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                hoveredElement_.Get(),
                SmListenerEventKind::PointerExit);
        }
    }
    hoveredElement_.Reset(newHovered, true);
    if (hoveredElement_) {
        hoveredElement_->SetHovered(true);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                hoveredElement_.Get(),
                SmListenerEventKind::PointerEnter);
        }
    }
}

void InputEventRouter::CapturePointer(UIElement* element) noexcept {
    if (capturedElement_.Get() == element) {
        return;
    }
    capturedElement_.Reset(element, true);
}

void InputEventRouter::ReleasePointerCapture() noexcept {
    capturedElement_.Reset();
}

void InputEventRouter::CancelPointerInteraction() noexcept {
    if (capturedElement_) {
        capturedElement_->SetPressed(false);
        capturedElement_.Reset();
    }
    if (hoveredElement_) {
        hoveredElement_->SetPressed(false);
    }
}

void InputEventRouter::CloseActiveComboBoxIfOpen() {
    if (openComboBox_) {
        if (openComboBox_->IsDropDownOpen()) {
            openComboBox_->SetIsDropDownOpen(false);
        }
        openComboBox_->SetHighlightedPopupIndex(-1);
        openComboBox_.Reset();
    }
}

void InputEventRouter::SyncImeFocusState() {
    if (!imeHost_) {
        return;
    }
    if (focusedElement_) {
        if (auto* tb = focusedElement_->DynamicCast<TextBox>();
            tb && !tb->IsReadOnly() && !tb->IsPassword()) {
            imeHost_->FocusTextInput(tb, tb->CaretScreenRect());
            return;
        }
    }
    imeHost_->BlurTextInput();
}

bool InputEventRouter::SetFocus(UIElement* element) {
    PruneDisconnectedTargets();
    if (element && (!BelongsToRoot(element) || !element->CanReceiveFocus())) {
        return false;
    }
    if (focusedElement_.Get() == element) {
        SyncImeFocusState();
        return element != nullptr;
    }
    if (focusedElement_) {
        focusedElement_->SetFocused(false);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                focusedElement_.Get(),
                SmListenerEventKind::FocusLost);
        }
    }
    focusedElement_.Reset(element, true);
    if (focusedElement_) {
        focusedElement_->SetFocused(true);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                focusedElement_.Get(),
                SmListenerEventKind::FocusGained);
        }
    }
    SyncImeFocusState();
    return focusedElement_.Get() != nullptr;
}

void InputEventRouter::ClearFocus() {
    if (focusedElement_) {
        focusedElement_->SetFocused(false);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                focusedElement_.Get(),
                SmListenerEventKind::FocusLost);
        }
        focusedElement_.Reset();
    }
    if (imeHost_) {
        imeHost_->BlurTextInput();
    }
}

bool InputEventRouter::DispatchPointerMove(Point position, std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (!rootVisual_) {
        return false;
    }
    PruneDisconnectedTargets();

    // 1. Active pointer capture routes exclusively to capturedElement_ (even outside bounds!)
    if (capturedElement_) {
        const Point localPt = capturedElement_->ScreenToLocal(position);
        const bool inside = PointInsideElementLocalBounds(capturedElement_.Get(), localPt);
        UpdateHoveredTarget(inside ? capturedElement_.Get() : nullptr);
        // Standard Button press-hold behavior: visual Pressed follows inside-bounds while Slider stays active
        if (capturedElement_->IsA<Button>()) {
            capturedElement_->SetPressed(inside);
        }
        return capturedElement_->OnPointerMove(localPt, modifiers);
    }

    // 2. Open ComboBox popup hover tracking takes top overlay priority
    if (openComboBox_ && openComboBox_->IsDropDownOpen()) {
        const std::int32_t popupIdx = openComboBox_->HitTestPopupItemIndex(position);
        openComboBox_->SetHighlightedPopupIndex(popupIdx);
        if (popupIdx >= 0) {
            UpdateHoveredTarget(openComboBox_.Get());
            return true;
        }
    }

    UIElement* rawHit = rootVisual_->HitTest(position);
    UIElement* target = ResolveInteractiveTarget(rawHit);
    UpdateHoveredTarget(target);
    if (target && target->IsEffectivelyEnabled()) {
        const Point localPt = target->ScreenToLocal(position);
        return target->OnPointerMove(localPt, modifiers);
    }
    return false;
}

bool InputEventRouter::DispatchPointerDown(
    Point position,
    PointerButton button,
    std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (!rootVisual_) {
        return false;
    }
    PruneDisconnectedTargets();

    // 1. Check active open ComboBox overlay popup first (Z-topmost over sibling controls)
    if (openComboBox_ && openComboBox_->IsDropDownOpen()) {
        const std::int32_t hitItemIdx = openComboBox_->HitTestPopupItemIndex(position);
        if (hitItemIdx >= 0) {
            if (button == PointerButton::Left) {
                (void)openComboBox_->SelectIndexInteractive(hitItemIdx);
                openComboBox_->SetHighlightedPopupIndex(-1);
                openComboBox_.Reset();
            }
            return true;
        }
        const Point comboLocal = openComboBox_->ScreenToLocal(position);
        if (!PointInsideElementLocalBounds(openComboBox_.Get(), comboLocal)) {
            CloseActiveComboBoxIfOpen();
        }
    }

    UIElement* rawHit = rootVisual_->HitTest(position);
    UIElement* target = ResolveInteractiveTarget(rawHit);
    UpdateHoveredTarget(target);

    if (!target || !target->IsEffectivelyEnabled()) {
        if (button == PointerButton::Left && (!target || target == rootVisual_)) {
            ClearFocus();
        }
        return false;
    }

    if (button == PointerButton::Left) {
        if (target->CanReceiveFocus()) {
            (void)SetFocus(target);
        } else if (!target->IsInteractiveControl()) {
            ClearFocus();
        }
        target->SetPressed(true);
        if (animationEngine_) {
            (void)animationEngine_->DispatchPointerListenerEvent(
                target,
                SmListenerEventKind::PointerDown);
        }
    }

    bool requestCapture = false;
    const Point localPt = target->ScreenToLocal(position);
    const bool handled = target->OnPointerDown(localPt, button, modifiers, requestCapture);

    if (auto* combo = target->DynamicCast<ComboBox>()) {
        if (combo->IsDropDownOpen()) {
            if (openComboBox_.Get() != combo) {
                CloseActiveComboBoxIfOpen();
                openComboBox_.Reset(combo, true);
            }
        } else if (openComboBox_.Get() == combo) {
            openComboBox_.Reset();
        }
    }

    if (requestCapture && button == PointerButton::Left) {
        CapturePointer(target);
    }
    SyncImeFocusState();
    return handled;
}

bool InputEventRouter::DispatchPointerUp(
    Point position,
    PointerButton button,
    std::uint8_t modifiers) {
    lastPointerPos_ = position;
    if (!rootVisual_) {
        return false;
    }
    PruneDisconnectedTargets();

    IntrusivePtr<UIElement> target = capturedElement_;
    if (!target) {
        UIElement* rawHit = rootVisual_->HitTest(position);
        target.Reset(ResolveInteractiveTarget(rawHit), true);
    }

    bool handled = false;
    if (target) {
        const Point localPt = target->ScreenToLocal(position);
        const bool inside = PointInsideElementLocalBounds(target.Get(), localPt);
        if (button == PointerButton::Left) {
            target->SetPressed(false);
            if (inside && animationEngine_) {
                (void)animationEngine_->DispatchPointerListenerEvent(
                    target.Get(),
                    SmListenerEventKind::PointerUpInside);
            }
        }
        if (target->IsEffectivelyEnabled()) {
            handled = target->OnPointerUp(localPt, button, modifiers, inside);
        }
    }

    if (button == PointerButton::Left) {
        ReleasePointerCapture();
    }

    // Refresh hover target after release (in case pointer released outside captured control)
    UIElement* postReleaseHit = rootVisual_->HitTest(position);
    UpdateHoveredTarget(ResolveInteractiveTarget(postReleaseHit));
    SyncImeFocusState();
    return handled;
}

bool InputEventRouter::DispatchMouseWheel(
    Point position,
    float scrollDeltaX,
    float scrollDeltaY,
    std::uint8_t modifiers) {
    (void)modifiers;
    lastPointerPos_ = position;
    if (!rootVisual_ || (scrollDeltaX == 0.0f && scrollDeltaY == 0.0f)) {
        return false;
    }
    PruneDisconnectedTargets();

    UIElement* hit = rootVisual_->HitTest(position);
    for (UIElement* cur = hit; cur != nullptr; cur = cur->Parent()) {
        if (auto* sv = cur->DynamicCast<ScrollViewer>(); sv && sv->IsEffectivelyEnabled()) {
            const bool allowH = (sv->GetHorizontalScrollBarVisibility() != ScrollBarVisibility::Disabled);
            const bool allowV = (sv->GetVerticalScrollBarVisibility() != ScrollBarVisibility::Disabled);
            const float effDx = allowH ? scrollDeltaX : 0.0f;
            const float effDy = allowV ? scrollDeltaY : 0.0f;
            if (effDx == 0.0f && effDy == 0.0f) {
                continue;
            }
            const float beforeX = sv->GetHorizontalOffset();
            const float beforeY = sv->GetVerticalOffset();
            sv->ScrollBy(effDx, effDy);
            const float afterX = sv->GetHorizontalOffset();
            const float afterY = sv->GetVerticalOffset();
            if (std::fabs(afterX - beforeX) > 1e-4f || std::fabs(afterY - beforeY) > 1e-4f) {
                return true;
            }
            // Boundary reached on inner ScrollViewer -> continue bubbling to outer ScrollViewer!
        }
        if (cur == rootVisual_) {
            break;
        }
    }
    return false;
}

bool InputEventRouter::DispatchKeyDown(KeyCode key, std::uint8_t modifiers) {
    if (!rootVisual_ || key == KeyCode::None) {
        return false;
    }
    PruneDisconnectedTargets();

    if (key == KeyCode::Escape && openComboBox_ && openComboBox_->IsDropDownOpen()) {
        CloseActiveComboBoxIfOpen();
        return true;
    }

    if (key == KeyCode::Tab) {
        const bool reverse = (modifiers & KeyMod_Shift) != 0u;
        return MoveFocus(reverse ? FocusDirection::Previous : FocusDirection::Next);
    }

    if (focusedElement_ && focusedElement_->CanReceiveFocus()) {
        if (focusedElement_->OnKeyDown(key, modifiers)) {
            if (auto* combo = focusedElement_->DynamicCast<ComboBox>()) {
                if (combo->IsDropDownOpen()) {
                    openComboBox_.Reset(combo, true);
                } else if (openComboBox_.Get() == combo) {
                    openComboBox_.Reset();
                }
            }
            SyncImeFocusState();
            return true;
        }
    }

    // Fallback spatial directional navigation (Gamepad D-Pad / Arrow keys)
    switch (key) {
        case KeyCode::Left:
            return MoveFocus(FocusDirection::Left);
        case KeyCode::Right:
            return MoveFocus(FocusDirection::Right);
        case KeyCode::Up:
            return MoveFocus(FocusDirection::Up);
        case KeyCode::Down:
            return MoveFocus(FocusDirection::Down);
        default:
            break;
    }
    return false;
}

bool InputEventRouter::DispatchKeyUp(KeyCode key, std::uint8_t modifiers) {
    (void)key;
    (void)modifiers;
    return false;
}

bool InputEventRouter::DispatchTextInput(std::string_view utf8Text) {
    if (!rootVisual_ || utf8Text.empty()) {
        return false;
    }
    PruneDisconnectedTargets();
    if (!focusedElement_ || !focusedElement_->CanReceiveFocus()) {
        return false;
    }
    const bool handled = focusedElement_->OnTextInput(utf8Text);
    if (handled) {
        SyncImeFocusState();
    }
    return handled;
}

bool InputEventRouter::MoveFocus(FocusDirection direction) {
    if (!rootVisual_) {
        return false;
    }
    PruneDisconnectedTargets();

    std::array<UIElement*, kMaxFocusableScan> candidates{};
    std::size_t count = 0u;
    CollectFocusablePreOrder(rootVisual_, candidates, count);
    if (count == 0u) {
        return false;
    }

    if (!focusedElement_) {
        const std::size_t initialIdx = (direction == FocusDirection::Previous) ? (count - 1u) : 0u;
        return SetFocus(candidates[initialIdx]);
    }

    if (direction == FocusDirection::Next || direction == FocusDirection::Previous) {
        std::size_t curIdx = count;
        for (std::size_t i = 0u; i < count; ++i) {
            if (candidates[i] == focusedElement_.Get()) {
                curIdx = i;
                break;
            }
        }
        if (curIdx == count) {
            return SetFocus(candidates[0]);
        }
        const std::size_t nextIdx = (direction == FocusDirection::Next)
            ? ((curIdx + 1u) % count)
            : ((curIdx + count - 1u) % count);
        return SetFocus(candidates[nextIdx]);
    }

    // 2D Spatial / Gamepad Directional Navigation using screen-space bounding centers
    const Rect originBounds = focusedElement_->LocalToScreenBounds(focusedElement_->FinalRect());
    const float ox = originBounds.x + originBounds.width * 0.5f;
    const float oy = originBounds.y + originBounds.height * 0.5f;

    float dirX = 0.0f;
    float dirY = 0.0f;
    switch (direction) {
        case FocusDirection::Left:  dirX = -1.0f; break;
        case FocusDirection::Right: dirX =  1.0f; break;
        case FocusDirection::Up:    dirY = -1.0f; break;
        case FocusDirection::Down:  dirY =  1.0f; break;
        default: break;
    }

    UIElement* bestTarget = nullptr;
    float bestScore = std::numeric_limits<float>::infinity();

    for (std::size_t i = 0u; i < count; ++i) {
        UIElement* cand = candidates[i];
        if (cand == focusedElement_.Get()) {
            continue;
        }
        const Rect cb = cand->LocalToScreenBounds(cand->FinalRect());
        const float cx = cb.x + cb.width * 0.5f;
        const float cy = cb.y + cb.height * 0.5f;
        const float dx = cx - ox;
        const float dy = cy - oy;

        const float primaryProj = dx * dirX + dy * dirY;
        if (primaryProj <= 0.5f) {
            continue;
        }
        const float crossDist = std::fabs(dx * dirY - dy * dirX);
        // Strongly favor aligned axis neighbors while still reaching diagonal controls
        const float score = primaryProj + crossDist * 2.5f;
        if (score < bestScore) {
            bestScore = score;
            bestTarget = cand;
        }
    }

    if (bestTarget) {
        return SetFocus(bestTarget);
    }
    return false;
}

} // namespace aero
