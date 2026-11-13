#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"

namespace aero {

class ITextInputMethodHost;
class AnimationEngine;

enum class InputEventKind : std::uint8_t {
    PointerMove = 0,
    PointerButton = 1,
    MouseWheel = 2,
    Key = 3,
    TextInput = 4,
};

// Fixed-size 48-byte queued input event record (0 heap allocations on queue/dequeue)
struct QueuedInputEvent {
    InputEventKind kind = InputEventKind::PointerMove;
    PointerButton button = PointerButton::Left;
    bool isDown = false;
    std::uint8_t modifiers = KeyMod_None;
    KeyCode key = KeyCode::None;
    std::uint8_t utf8Length = 0;
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    std::array<char, 28> utf8Bytes{};
};

static_assert(sizeof(QueuedInputEvent) <= 64, "QueuedInputEvent must fit within a single 64B cache line.");

// Zero-allocation InputEventRouter:
// - Fixed-capacity inline ring buffer (64 events, 0 heap allocations in steady state)
// - Pointer hit-testing, hover enter/leave tracking, and pointer capture (Slider thumb / Button press-hold)
// - Ancestor-chain MouseWheel bubbling into ScrollViewer (with boundary scroll-chaining)
// - Sequential (Tab / Shift+Tab) and spatial 2D directional (Gamepad D-Pad / Arrow keys) focus navigation
// - Keyboard activation & UTF-8 text editing dispatch to TextBox + automatic ITextInputMethodHost caret sync
class InputEventRouter {
public:
    static constexpr std::size_t kEventQueueCapacity = 64u;

    explicit InputEventRouter(
        UIElement* rootVisual = nullptr,
        ITextInputMethodHost* imeHost = nullptr) noexcept;

    void SetRootVisual(UIElement* root) noexcept;
    [[nodiscard]] UIElement* RootVisual() const noexcept { return rootVisual_; }

    void SetImeHost(ITextInputMethodHost* imeHost) noexcept { imeHost_ = imeHost; }
    [[nodiscard]] ITextInputMethodHost* ImeHost() const noexcept { return imeHost_; }

    void SetAnimationEngine(AnimationEngine* animationEngine) noexcept { animationEngine_ = animationEngine; }
    [[nodiscard]] AnimationEngine* GetAnimationEngine() const noexcept { return animationEngine_; }

    // Non-allocating event ring queue API (for HostDrivers UiPhase_Input)
    bool QueuePointerMove(Point position, std::uint8_t modifiers = KeyMod_None) noexcept;
    bool QueuePointerButton(
        Point position,
        PointerButton button,
        bool isDown,
        std::uint8_t modifiers = KeyMod_None) noexcept;
    bool QueueMouseWheel(
        Point position,
        float scrollDeltaX,
        float scrollDeltaY,
        std::uint8_t modifiers = KeyMod_None) noexcept;
    bool QueueKey(KeyCode key, bool isDown, std::uint8_t modifiers = KeyMod_None) noexcept;
    bool QueueTextInput(std::string_view utf8Text) noexcept;

    [[nodiscard]] bool HasPendingEvents() const noexcept { return queueCount_ > 0u; }
    [[nodiscard]] std::size_t PendingEventCount() const noexcept { return queueCount_; }
    std::size_t FlushQueuedEvents();

    // Immediate synchronous dispatch API
    bool DispatchPointerMove(Point position, std::uint8_t modifiers = KeyMod_None);
    bool DispatchPointerDown(
        Point position,
        PointerButton button = PointerButton::Left,
        std::uint8_t modifiers = KeyMod_None);
    bool DispatchPointerUp(
        Point position,
        PointerButton button = PointerButton::Left,
        std::uint8_t modifiers = KeyMod_None);
    void CancelPointerInteraction() noexcept;

    bool DispatchMouseWheel(
        Point position,
        float scrollDeltaX,
        float scrollDeltaY,
        std::uint8_t modifiers = KeyMod_None);

    bool DispatchKeyDown(KeyCode key, std::uint8_t modifiers = KeyMod_None);
    bool DispatchKeyUp(KeyCode key, std::uint8_t modifiers = KeyMod_None);
    bool DispatchTextInput(std::string_view utf8Text);
    void Tick(float dtSeconds);

    // Pointer capture & active visual target inspection
    void CapturePointer(UIElement* element) noexcept;
    void ReleasePointerCapture() noexcept;
    [[nodiscard]] UIElement* CapturedElement() const noexcept { return capturedElement_.Get(); }
    [[nodiscard]] UIElement* HoveredElement() const noexcept { return hoveredElement_.Get(); }
    [[nodiscard]] UIElement* FocusedElement() const noexcept { return focusedElement_.Get(); }
    [[nodiscard]] ComboBox* ActiveOpenComboBox() const noexcept { return openComboBox_.Get(); }
    [[nodiscard]] Point LastPointerPosition() const noexcept { return lastPointerPos_; }

    // Focus management & keyboard/gamepad navigation
    bool SetFocus(UIElement* element);
    void ClearFocus();
    bool MoveFocus(FocusDirection direction);
    bool MoveFocusNext() { return MoveFocus(FocusDirection::Next); }
    bool MoveFocusPrevious() { return MoveFocus(FocusDirection::Previous); }

private:
    bool EnqueueEvent(const QueuedInputEvent& ev) noexcept;
    void PruneDisconnectedTargets() noexcept;
    [[nodiscard]] bool BelongsToRoot(const UIElement* elem) const noexcept;
    [[nodiscard]] UIElement* ResolveInteractiveTarget(UIElement* hitLeaf) const noexcept;
    void UpdateHoveredTarget(UIElement* newHovered) noexcept;
    void SyncImeFocusState();
    void CloseActiveComboBoxIfOpen();
    void CloseActiveContextMenuIfOpen();
    void CloseActiveToolTipIfOpen() noexcept;

    UIElement* rootVisual_ = nullptr;
    ITextInputMethodHost* imeHost_ = nullptr;
    AnimationEngine* animationEngine_ = nullptr;

    IntrusivePtr<UIElement> hoveredElement_{};
    IntrusivePtr<UIElement> capturedElement_{};
    IntrusivePtr<UIElement> focusedElement_{};
    IntrusivePtr<ComboBox> openComboBox_{};
    IntrusivePtr<ContextMenu> openContextMenu_{};
    IntrusivePtr<ToolTip> activeToolTip_{};
    IntrusivePtr<UIElement> activeToolTipOwner_{};
    float repeatHoldElapsedMs_ = 0.0f;
    float repeatNextFireMs_ = 0.0f;

    Point lastPointerPos_{0.0f, 0.0f};
    std::array<QueuedInputEvent, kEventQueueCapacity> eventRing_{};
    std::size_t queueHead_ = 0u;
    std::size_t queueCount_ = 0u;
};

} // namespace aero
