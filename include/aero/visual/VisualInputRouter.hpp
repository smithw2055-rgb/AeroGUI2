#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "aero/drawing/Geometry.hpp"
#include "aero/visual/VisualTree.hpp"

namespace aero {

enum class PointerButton : std::uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
};

enum class KeyCode : std::uint16_t {
    None = 0,
    Tab = 1,
    Enter = 2,
    Space = 3,
    Escape = 4,
    Backspace = 5,
    Delete = 6,
    Left = 7,
    Right = 8,
    Up = 9,
    Down = 10,
    Home = 11,
    End = 12,
    PageUp = 13,
    PageDown = 14,
    A = 15, B = 16, C = 17, D = 18, E = 19, F = 20, G = 21, H = 22, I = 23,
    J = 24, K = 25, L = 26, M = 27, N = 28, O = 29, P = 30, Q = 31, R = 32,
    S = 33, T = 34, U = 35, V = 36, W = 37, X = 38, Y = 39, Z = 40,
    D0 = 41, D1 = 42, D2 = 43, D3 = 44, D4 = 45,
    D5 = 46, D6 = 47, D7 = 48, D8 = 49, D9 = 50,
    F1 = 51, F2 = 52, F3 = 53, F4 = 54, F5 = 55, F6 = 56,
    F7 = 57, F8 = 58, F9 = 59, F10 = 60, F11 = 61, F12 = 62,
};

enum KeyModifierFlags : std::uint8_t {
    KeyMod_None = 0u,
    KeyMod_Shift = 1u << 0u,
    KeyMod_Ctrl = 1u << 1u,
    KeyMod_Alt = 1u << 2u,
};

enum class FocusDirection : std::uint8_t {
    Next = 0,
    Previous = 1,
    Left = 2,
    Right = 3,
    Up = 4,
    Down = 5,
};

enum class NativeInputEventKind : std::uint8_t {
    PointerMove = 0,
    PointerButton = 1,
    MouseWheel = 2,
    Key = 3,
    TextInput = 4,
};

struct NativeInputEvent {
    NativeInputEventKind kind = NativeInputEventKind::PointerMove;
    PointerButton button = PointerButton::Left;
    bool isDown = false;
    std::uint8_t modifiers = KeyMod_None;
    KeyCode key = KeyCode::None;
    std::uint8_t utf8Length = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    std::array<char, 28> utf8Bytes{};
};

static_assert(sizeof(NativeInputEvent) <= 64, "NativeInputEvent must fit within a single 64B cache line.");

struct UIHostPlatformHooks {
    std::function<void(VisualNodeId)> setPointerCapture;
    std::function<void(VisualNodeId)> releasePointerCapture;
    std::function<void(Rect, bool)> syncImeRect;
    std::function<std::string()> getClipboardText;
    std::function<void(std::string_view)> setClipboardText;
    std::function<void()> requestWake;
    std::function<void()> closeWindow;
};

class ITextInputMethodHost {
public:
    virtual ~ITextInputMethodHost() = default;

    virtual void FocusTextInput(VisualNodeId focusedNode, Rect caretScreenRect) = 0;
    virtual void BlurTextInput() = 0;
    virtual void OnCompositionStart() = 0;
    virtual void OnCompositionUpdate(std::string_view preeditText, std::uint32_t caretOffset) = 0;
    virtual void OnCompositionEnd(std::string_view committedText) = 0;
};

class ImeCompositionController final {
public:
    static constexpr std::size_t kMaxCommittedHistoryBytes = 4096u;

    static void AppendBoundedCommittedText(std::string& ioHistory, std::string_view committed);

    void BeginFocus(VisualNodeId focusedNode);
    void EndBlur();
    void BeginComposition();
    void UpdateComposition(std::string_view preeditText, std::uint32_t caretOffset);
    void CommitComposition(std::string_view committedText);

    [[nodiscard]] bool IsImeFocused() const noexcept { return imeFocused_; }
    [[nodiscard]] bool IsComposing() const noexcept { return isComposing_; }
    [[nodiscard]] VisualNodeId FocusedNode() const noexcept { return focusedNode_; }
    [[nodiscard]] std::uint32_t CaretOffset() const noexcept { return caretOffset_; }
    [[nodiscard]] const std::string& PreeditText() const noexcept { return preeditText_; }
    [[nodiscard]] const std::string& CommittedText() const noexcept { return committedText_; }

private:
    VisualNodeId focusedNode_{};
    bool imeFocused_ = false;
    bool isComposing_ = false;
    std::uint32_t caretOffset_ = 0u;
    std::string preeditText_;
    std::string committedText_;
};

class VisualInputRouter final {
public:
    static constexpr std::size_t kEventQueueCapacity = 64u;

    using ScriptEventDispatchFn = std::function<bool(
        VisualNodeId nodeId,
        VisualCallbackFlags eventKind,
        const NativeInputEvent& ev)>;

    explicit VisualInputRouter(
        VisualWorld* world = nullptr,
        ITextInputMethodHost* imeHost = nullptr,
        UIHostPlatformHooks* platformHooks = nullptr) noexcept;

    void SetWorld(VisualWorld* world) noexcept { world_ = world; }
    [[nodiscard]] VisualWorld* World() const noexcept { return world_; }

    void SetImeHost(ITextInputMethodHost* imeHost) noexcept { imeHost_ = imeHost; }
    [[nodiscard]] ITextInputMethodHost* ImeHost() const noexcept { return imeHost_; }

    void SetPlatformHooks(UIHostPlatformHooks* hooks) noexcept { platformHooks_ = hooks; }
    [[nodiscard]] UIHostPlatformHooks* PlatformHooks() const noexcept { return platformHooks_; }

    void SetScriptEventDispatcher(ScriptEventDispatchFn fn) { scriptDispatcher_ = std::move(fn); }

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

    bool DispatchPointerMove(Point position, std::uint8_t modifiers = KeyMod_None);
    bool DispatchPointerDown(
        Point position,
        PointerButton button = PointerButton::Left,
        std::uint8_t modifiers = KeyMod_None);
    bool DispatchPointerUp(
        Point position,
        PointerButton button = PointerButton::Left,
        std::uint8_t modifiers = KeyMod_None);
    bool DispatchMouseWheel(
        Point position,
        float scrollDeltaX,
        float scrollDeltaY,
        std::uint8_t modifiers = KeyMod_None);
    bool DispatchKey(KeyCode key, bool isDown, std::uint8_t modifiers = KeyMod_None);
    bool DispatchTextInput(std::string_view utf8Text);

    [[nodiscard]] VisualNodeId HitTest(Point viewportPoint) const noexcept;

    void SetPointerCapture(VisualNodeId node) noexcept;
    void ReleasePointerCapture(VisualNodeId node) noexcept;
    void CancelPointerInteraction() noexcept;
    [[nodiscard]] VisualNodeId CapturedNode() const noexcept { return capturedNode_; }
    [[nodiscard]] VisualNodeId HoveredNode() const noexcept { return hoveredNode_; }

    bool SetFocus(VisualNodeId node);
    bool MoveFocus(FocusDirection direction);
    [[nodiscard]] VisualNodeId FocusedNode() const noexcept { return focusedNode_; }

    void SyncImeCaretRect(Rect caretScreenRect, bool active) noexcept;
    [[nodiscard]] bool HasFocusedTextInput() const noexcept { return imeActive_; }
    [[nodiscard]] Rect FocusedTextCaretRect() const noexcept { return imeCaretRect_; }

    [[nodiscard]] std::string GetClipboardText() const;
    void SetClipboardText(std::string_view text);

private:
    bool EnqueueEvent(const NativeInputEvent& ev) noexcept;

    VisualWorld* world_ = nullptr;
    ITextInputMethodHost* imeHost_ = nullptr;
    UIHostPlatformHooks* platformHooks_ = nullptr;
    ScriptEventDispatchFn scriptDispatcher_;

    std::array<NativeInputEvent, kEventQueueCapacity> queue_{};
    std::size_t queueHead_ = 0u;
    std::size_t queueTail_ = 0u;
    std::size_t queueCount_ = 0u;

    VisualNodeId hoveredNode_{};
    VisualNodeId capturedNode_{};
    VisualNodeId focusedNode_{};
    Point lastPointerPos_{};
    bool imeActive_ = false;
    Rect imeCaretRect_{};
};

} // namespace aero
