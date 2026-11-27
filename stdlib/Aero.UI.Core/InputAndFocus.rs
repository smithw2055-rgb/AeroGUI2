module Aero.UI;

import Aero.Native;
import RealScript.Math;

public static class FocusManager {
    public static void CollectFocusable(UIElement root, UIElement[] output) {
        if (root == null) { return; }
        if (root.Visibility != Visibility.Visible || !root.IsEnabled) { return; }
        bool canTabStop = !(root is Control) || (root as Control).IsTabStop;
        if (root.Focusable && canTabStop) { output.Push(root); }
        for (int i = 0; i < root.ChildCount(); i += 1) {
            CollectFocusable(root.ChildAt(i), output);
        }
    }

    public static bool MoveFocus(UIElement current, FocusDirection dir) {
        if (current == null) { return false; }
        UIElement root = current.RootElement();
        var list = new UIElement[0];
        CollectFocusable(root, list);
        if (list.Length == 0) { return false; }
        int curIdx = -1;
        for (int i = 0; i < list.Length; i += 1) {
            if (list[i] == current) { curIdx = i; break; }
        }
        bool backward = (dir == FocusDirection.Previous || dir == FocusDirection.Left || dir == FocusDirection.Up);
        int nextIdx = 0;
        if (curIdx >= 0) {
            nextIdx = backward
                ? (curIdx - 1 + list.Length) % list.Length
                : (curIdx + 1) % list.Length;
        } else if (backward) {
            nextIdx = list.Length - 1;
        }
        return InputDispatcher.SetFocus(list[nextIdx]);
    }
}

public static class InputDispatcher {
    private static UIElement _hovered;
    private static UIElement _captured;
    private static UIElement _focused;
    private static UIElement _lastClickTarget;
    private static float _lastClickX;
    private static float _lastClickY;
    private static int _clickSeq;
    private static float _uiClockSec = 0.0f;
    private static float _lastClickTimeSec = -10.0f;
    private static PopupDismissHook[] _popupHooks;

    internal static void AdvanceUiClock(float dt) {
        if (dt > 0.0f) { _uiClockSec = _uiClockSec + dt; }
    }

    public static UIElement HoveredElement() { return _hovered; }
    public static UIElement CapturedElement() { return _captured; }
    public static UIElement FocusedElement() { return _focused; }

    public static void RegisterPopupDismissHook(PopupDismissHook hook) {
        if (hook == null) { return; }
        if (_popupHooks == null) { _popupHooks = new PopupDismissHook[0]; }
        _popupHooks.Push(hook);
    }

    public static void NotifyPopupHooks(UIElement target, bool isEscape) {
        if (_popupHooks == null) { return; }
        for (int i = 0; i < _popupHooks.Length; i += 1) {
            var fn = _popupHooks[i];
            if (fn != null) { fn(target, isEscape); }
        }
    }

    public static void CapturePointer(UIElement el) {
        _captured = el;
        if (el != null && ParseUtil.IsValidHandle(el.__nativeHandle)) {
            Host_SetPointerCapture(el.__nativeHandle);
        }
    }

    public static void ReleasePointerCapture(UIElement el) {
        if (el == null || _captured == el) {
            UIElement old = _captured;
            _captured = null;
            if (old != null && ParseUtil.IsValidHandle(old.__nativeHandle)) {
                Host_ReleasePointerCapture(old.__nativeHandle);
            }
        }
    }

    private static bool ContainsEl(UIElement[] chain, UIElement target) {
        if (chain == null || target == null) { return false; }
        for (int i = 0; i < chain.Length; i += 1) {
            if (chain[i] == target) { return true; }
        }
        return false;
    }

    private static bool _syncingNativeFocus = false;

    public static bool SetFocus(UIElement el) {
        if (el == _focused) {
            if (!_syncingNativeFocus && el != null && ParseUtil.IsNativeReady() && ParseUtil.IsValidHandle(el.__nativeHandle)) {
                _syncingNativeFocus = true;
                Visual_SetFocus(el.__nativeHandle);
                _syncingNativeFocus = false;
            }
            return true;
        }
        if (el != null && (!el.Focusable || !el.IsEnabled || el.Visibility != Visibility.Visible)) {
            return false;
        }
        UIElement prev = _focused;
        _focused = el;
        if (!_syncingNativeFocus && ParseUtil.IsNativeReady()) {
            _syncingNativeFocus = true;
            if (el != null && ParseUtil.IsValidHandle(el.__nativeHandle)) {
                Visual_SetFocus(el.__nativeHandle);
            } else if (el == null) {
                Visual_SetFocus(ParseUtil.EmptyHandle());
            }
            _syncingNativeFocus = false;
        }
        UIElement[] oldChain = AncestorChain(prev);
        UIElement[] newChain = AncestorChain(_focused);
        for (int i = 0; i < oldChain.Length; i += 1) {
            UIElement o = oldChain[i];
            if (o != null && !ContainsEl(newChain, o)) {
                o.SetKeyboardFocusWithinState(false);
            }
        }
        if (prev != null) { prev.SetFocusedState(false); }
        for (int i = 0; i < newChain.Length; i += 1) {
            UIElement n = newChain[i];
            if (n != null && !ContainsEl(oldChain, n)) {
                n.SetKeyboardFocusWithinState(true);
            }
        }
        if (_focused != null) { _focused.SetFocusedState(true); }
        return true;
    }

    private static UIElement[] AncestorChain(UIElement leaf) {
        var chain = new UIElement[0];
        UIElement cur = leaf;
        int guard = 0;
        while (cur != null && guard < 256) {
            guard += 1;
            chain.Push(cur);
            cur = cur.Parent != null ? cur.Parent : cur._logicalParent;
        }
        return chain;
    }

    private static void ApplyHoverChainRecursive(
        UIElement cur, UIElement prev, float x, float y, int mods) {
        if (cur == null) { return; }
        ApplyHoverChainRecursive(cur.Parent, prev, x, y, mods);
        if (prev == null || !cur.IsAncestorOf(prev)) {
            cur.SetMouseOverState(true, x, y, mods);
        }
    }

    private static void UpdateHoverChain(UIElement newTarget, float x, float y, int mods) {
        if (_hovered == newTarget) { return; }
        UIElement prev = _hovered;
        _hovered = newTarget;
        if (prev != null) {
            UIElement cur = prev;
            while (cur != null) {
                if (newTarget == null || !cur.IsAncestorOf(newTarget)) {
                    cur.SetMouseOverState(false, x, y, mods);
                }
                cur = cur.Parent;
            }
        }
        if (newTarget != null) {
            ApplyHoverChainRecursive(newTarget, prev, x, y, mods);
        }
    }

    private static UIElement[] _pressedChain;

    internal static void CleanupDisposed() {
        if (_captured != null && _captured._isDisposedTree) { _captured = null; }
        if (_hovered != null && _hovered._isDisposedTree) { _hovered = null; }
        if (_focused != null && _focused._isDisposedTree) { _focused = null; }
        if (_lastClickTarget != null && _lastClickTarget._isDisposedTree) {
            _lastClickTarget = null;
            _clickSeq = 0;
        }
        if (_pressedChain != null && _pressedChain.Length > 0) {
            int pw = 0;
            for (int i = 0; i < _pressedChain.Length; i += 1) {
                UIElement el = _pressedChain[i];
                if (el != null && !el._isDisposedTree) {
                    if (pw != i) { _pressedChain[pw] = el; }
                    pw += 1;
                }
            }
            while (_pressedChain.Length > pw) {
                _pressedChain.RemoveAt(_pressedChain.Length - 1);
            }
        }
    }

    private static void ClearPressedChain() {
        if (_pressedChain == null || _pressedChain.Length == 0) { return; }
        for (int i = 0; i < _pressedChain.Length; i += 1) {
            UIElement el = _pressedChain[i];
            if (el != null && !el._isDisposedTree) {
                el.SetPressedState(false);
            }
        }
        _pressedChain.Clear();
    }

    public static bool __AeroDispatchPointer(
        handle node, int kind, float x, float y, int button, int modifiers, float dx, float dy) {
        if (_captured != null && _captured._isDisposedTree) { _captured = null; }
        if (_hovered != null && _hovered._isDisposedTree) { _hovered = null; }
        UIElement rawTarget = Visual_GetOwner(node) as UIElement;
        if (kind == 8) {
            UpdateHoverChain(rawTarget, x, y, modifiers);
            return true;
        }
        if (kind == 16) {
            if (dx == 0.0f) {
                UpdateHoverChain(null, x, y, modifiers);
            }
            return true;
        }

        UIElement target = _captured != null ? _captured : rawTarget;
        if (target == null) {
            if (kind == 2) {
                NotifyPopupHooks(null, false);
                if (EnumConvert.ToPointerButton(button) == PointerButton.Left) {
                    ClearPressedChain();
                }
            }
            if (kind == 4) {
                if (EnumConvert.ToPointerButton(button) == PointerButton.Left) {
                    ClearPressedChain();
                }
                if (_captured != null) { ReleasePointerCapture(_captured); }
            }
            if (kind == 1 || kind == 0) { UpdateHoverChain(null, x, y, modifiers); }
            return false;
        }

        if (kind == 1 || kind == 0) {
            if (_captured == null && rawTarget != null &&
                (_hovered == null || (rawTarget != _hovered && !rawTarget.IsAncestorOf(_hovered)))) {
                UpdateHoverChain(rawTarget, x, y, modifiers);
            }
            var moveArgs = new MouseEventArgs("MouseMove", target, x, y, modifiers);
            UIElement cur = target;
            while (cur != null && !moveArgs.Handled) {
                cur.RaiseMouseMove(moveArgs);
                cur = cur.Parent;
            }
            return moveArgs.Handled;
        }

        if (kind == 2) {
            NotifyPopupHooks(target, false);
            PointerButton btn = EnumConvert.ToPointerButton(button);
            int clickCount = 1;
            bool sameClickTarget = (_lastClickTarget == target) ||
                (target != null && _lastClickTarget != null &&
                 target.TemplatedParent != null &&
                 target.TemplatedParent == _lastClickTarget.TemplatedParent);
            float sinceLast = _uiClockSec - _lastClickTimeSec;
            if (btn == PointerButton.Left && sameClickTarget && _clickSeq == 1 &&
                sinceLast >= 0.0f && sinceLast <= 0.5f &&
                Math.Absf(x - _lastClickX) < 6.0f && Math.Absf(y - _lastClickY) < 6.0f) {
                clickCount = 2;
                _clickSeq = 0;
                _lastClickTimeSec = -10.0f;
            } else {
                _lastClickTarget = target;
                _lastClickX = x;
                _lastClickY = y;
                _clickSeq = 1;
                _lastClickTimeSec = _uiClockSec;
            }

            var downArgs = new MouseButtonEventArgs(
                btn == PointerButton.Right ? "MouseRightButtonDown" : "MouseLeftButtonDown",
                target, x, y, modifiers, btn, true, clickCount);

            if (btn == PointerButton.Left) {
                ClearPressedChain();
                if (_pressedChain == null) { _pressedChain = new UIElement[0]; }
                UIElement[] chain = AncestorChain(target);
                for (int i = chain.Length - 1; i >= 0 && !downArgs.Handled; i -= 1) {
                    chain[i].RaisePreviewLeftDown(downArgs);
                }
                if (!downArgs.Handled) {
                    UIElement f = target;
                    while (f != null) {
                        if (f.Focusable && f.IsEnabled) { SetFocus(f); break; }
                        f = f.Parent;
                    }
                }
            }

            UIElement cur = target;
            while (cur != null && !downArgs.Handled) {
                if (cur.IsEnabled) {
                    if (btn == PointerButton.Left) {
                        cur.SetPressedState(true);
                        _pressedChain.Push(cur);
                        cur.RaiseLeftDown(downArgs);
                    } else if (btn == PointerButton.Right) {
                        cur.RaiseRightDown(downArgs);
                    }
                }
                cur = cur.Parent;
            }
            return downArgs.Handled;
        }

        if (kind == 4) {
            PointerButton btn = EnumConvert.ToPointerButton(button);
            var upArgs = new MouseButtonEventArgs(
                btn == PointerButton.Right ? "MouseRightButtonUp" : "MouseLeftButtonUp",
                target, x, y, modifiers, btn, false, 1);

            UIElement cur = target;
            while (cur != null && !upArgs.Handled) {
                if (btn == PointerButton.Left) {
                    cur.SetPressedState(false);
                    if (cur.IsEnabled) { cur.RaiseLeftUp(upArgs); }
                } else if (btn == PointerButton.Right && cur.IsEnabled) {
                    cur.RaiseRightUp(upArgs);
                }
                cur = cur.Parent;
            }
            if (btn == PointerButton.Left) {
                ClearPressedChain();
            }
            if (_captured != null) { ReleasePointerCapture(_captured); }
            return upArgs.Handled;
        }

        if (kind == 32) {
            var wheelArgs = new MouseWheelEventArgs("MouseWheel", target, x, y, modifiers, dx, dy);
            UIElement cur = target;
            while (cur != null && !wheelArgs.Handled) {
                if (cur.IsEnabled) { cur.RaiseWheel(wheelArgs); }
                cur = cur.Parent;
            }
            return wheelArgs.Handled;
        }
        return false;
    }

    public static bool __AeroDispatchKey(handle node, int kind, int keyCode, int modifiers) {
        UIElement target = Visual_GetOwner(node) as UIElement;
        if (target == null) { target = _focused; }
        if (target == null) { return false; }

        KeyCode kc = EnumConvert.ToKeyCode(keyCode);
        bool isDown = (kind == 64 || kind == 1);

        if (isDown && kc == KeyCode.Escape) {
            NotifyPopupHooks(target, true);
        }

        var args = new KeyEventArgs(isDown ? "KeyDown" : "KeyUp", target, kc, modifiers, isDown);
        if (isDown) {
            UIElement[] chain = AncestorChain(target);
            for (int i = chain.Length - 1; i >= 0 && !args.Handled; i -= 1) {
                chain[i].RaisePreviewKeyDown(args);
            }
        }

        UIElement cur = target;
        while (cur != null && !args.Handled) {
            if (cur.IsEnabled) {
                if (isDown) { cur.RaiseKeyDown(args); }
                else { cur.RaiseKeyUp(args); }
            }
            cur = cur.Parent;
        }

        if (isDown && !args.Handled) {
            UIElement bcur = target;
            while (bcur != null && !args.Handled) {
                if (bcur.IsEnabled && bcur.TryHandleInputBindings(args)) {
                    break;
                }
                bcur = bcur.Parent;
            }
        }

        if (isDown && !args.Handled && kc == KeyCode.Tab) {
            bool shift = KeyModifiers.Has(modifiers, KeyModifiers.Shift());
            FocusManager.MoveFocus(target, shift ? FocusDirection.Previous : FocusDirection.Next);
            return true;
        }
        if (isDown && !args.Handled && (kc == KeyCode.Up || kc == KeyCode.Down)) {
            FocusManager.MoveFocus(target, kc == KeyCode.Up ? FocusDirection.Previous : FocusDirection.Next);
            return true;
        }
        return args.Handled;
    }

    public static bool __AeroDispatchText(handle node, string text) {
        UIElement target = Visual_GetOwner(node) as UIElement;
        if (target == null) { target = _focused; }
        if (target == null || text == null || text.Length == 0) { return false; }
        var args = new TextCompositionEventArgs("TextInput", target, text);
        UIElement cur = target;
        while (cur != null && !args.Handled) {
            if (cur.IsEnabled) { cur.RaiseTextInput(args); }
            cur = cur.Parent;
        }
        return args.Handled;
    }

    public static void __AeroDispatchFocus(handle node, bool focused) {
        UIElement target = Visual_GetOwner(node) as UIElement;
        if (target == null) { return; }
        bool prevSync = _syncingNativeFocus;
        _syncingNativeFocus = true;
        if (focused) { SetFocus(target); }
        else if (_focused == target) { SetFocus(null); }
        _syncingNativeFocus = prevSync;
    }

    public static void __AeroDispatchCustomMeasure(handle node, float availW, float availH) {
        UIElement target = Visual_GetOwner(node) as UIElement;
        if (target != null) { target.OnCustomMeasure(availW, availH); }
    }

    public static void __AeroDispatchDraw(handle node) {
        UIElement target = Visual_GetOwner(node) as UIElement;
        if (target != null) { target.OnRenderVisual(); }
    }
}
