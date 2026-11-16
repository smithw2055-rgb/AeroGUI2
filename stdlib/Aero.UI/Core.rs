module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Enumerations (1:1 Ordinals with C++ Aero Drawing & Visual Microkernel)
// ============================================================================

public enum Visibility {
    Visible = 0,
    Hidden = 1,
    Collapsed = 2,
}

public enum Orientation {
    Horizontal = 0,
    Vertical = 1,
}

public enum HorizontalAlignment {
    Stretch = 0,
    Left = 1,
    Center = 2,
    Right = 3,
}

public enum VerticalAlignment {
    Stretch = 0,
    Top = 1,
    Center = 2,
    Bottom = 3,
}

public enum Stretch {
    None = 0,
    Fill = 1,
    Uniform = 2,
    UniformToFill = 3,
}

public enum StretchDirection {
    UpOnly = 0,
    DownOnly = 1,
    Both = 2,
}

public enum ExpandDirection {
    Down = 0,
    Up = 1,
    Left = 2,
    Right = 3,
}

public enum Dock {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
}

public enum ClickMode {
    Release = 0,
    Press = 1,
    Hover = 2,
}

public enum SelectionMode {
    Single = 0,
    Multiple = 1,
    Extended = 2,
}

public enum TabStripPlacement {
    Top = 0,
    Bottom = 1,
    Left = 2,
    Right = 3,
}

public enum ScrollBarVisibility {
    Disabled = 0,
    Auto = 1,
    Hidden = 2,
    Visible = 3,
}

public enum TextWrapping {
    NoWrap = 0,
    Wrap = 1,
    WrapWithOverflow = 2,
}

public enum TextAlignment {
    Left = 0,
    Center = 1,
    Right = 2,
    Justify = 3,
}

public enum TextTrimming {
    None = 0,
    CharacterEllipsis = 1,
    WordEllipsis = 2,
}

public enum GridUnitType {
    Auto = 0,
    Pixel = 1,
    Star = 2,
}

public enum UIEffectKind {
    None = 0,
    Blur = 1,
    DropShadow = 2,
    Pixelate = 3,
    BackdropBlur = 4,
    Tint = 5,
    ChromaticAberration = 6,
    DirectionalBlur = 7,
    Saturation = 8,
}

public enum BlendingMode {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Additive = 3,
}

public enum VisualKind {
    Box = 0,
    Text = 1,
    Image = 2,
    Vector = 3,
    CanvasDraw = 4,
}

public enum LayoutMode {
    Manual = 0,
    Stack = 1,
    Dock = 2,
    Wrap = 3,
    Grid = 4,
    UniformGrid = 5,
    Canvas = 6,
    ScrollViewport = 7,
    Viewbox = 8,
    CustomCallback = 9,
}

public enum VectorShapeKind {
    SvgPath = 0,
    Ellipse = 1,
    Line = 2,
    RadialArc = 3,
}

public enum VisualLayoutRectQuery {
    X = 0,
    Y = 1,
    Width = 2,
    Height = 3,
    DesiredWidth = 4,
    DesiredHeight = 5,
    ScrollExtentWidth = 6,
    ScrollExtentHeight = 7,
}

public enum VisualAnimProp {
    Opacity = 0,
    Width = 1,
    Height = 2,
    TranslateX = 3,
    TranslateY = 4,
    ScaleX = 5,
    ScaleY = 6,
    RotateDeg = 7,
    SkewX = 8,
    SkewY = 9,
    Rot3DX = 10,
    Rot3DY = 11,
    Rot3DZ = 12,
    Trans3DX = 13,
    Trans3DY = 14,
    Trans3DZ = 15,
    BgColor = 16,
    FgColor = 17,
    BorderColor = 18,
    CornerRadius = 19,
    PathTrimStart = 20,
    PathTrimEnd = 21,
    EffectRadius = 22,
    CustomScalar0 = 23,
    CustomScalar1 = 24,
    CustomScalar2 = 25,
    CustomScalar3 = 26,
}

public enum EasingFunctionKind {
    Linear = 0,
    Quadratic = 1,
    Cubic = 2,
    Quartic = 3,
    Quintic = 4,
    Sine = 5,
    Circle = 6,
    Exponential = 7,
    Back = 8,
    Bounce = 9,
    Elastic = 10,
    Spline = 11,
}

public enum EasingMode {
    EaseOut = 0,
    EaseIn = 1,
    EaseInOut = 2,
}

public enum PointerButton {
    Left = 0,
    Right = 1,
    Middle = 2,
}

public enum KeyCode {
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
}

public enum FocusDirection {
    Next = 0,
    Previous = 1,
    Left = 2,
    Right = 3,
    Up = 4,
    Down = 5,
}

public enum NativeInputEventKind {
    PointerMove = 0,
    PointerButton = 1,
    MouseWheel = 2,
    Key = 3,
    TextInput = 4,
}

public static class VisualCallbacks {
    public static int None() { return 0; }
    public static int PointerMove() { return 1; }
    public static int PointerDown() { return 2; }
    public static int PointerUp() { return 4; }
    public static int PointerEnter() { return 8; }
    public static int PointerLeave() { return 16; }
    public static int MouseWheel() { return 32; }
    public static int KeyDown() { return 64; }
    public static int KeyUp() { return 128; }
    public static int TextInput() { return 256; }
    public static int FocusChanged() { return 512; }
    public static int CustomMeasure() { return 1024; }
    public static int CustomArrange() { return 2048; }
    public static int Draw() { return 4096; }
    public static int StandardInput() { return 1023; }
}

public static class KeyModifiers {
    public static int None() { return 0; }
    public static int Shift() { return 1; }
    public static int Ctrl() { return 2; }
    public static int Alt() { return 4; }
    public static bool Has(int mods, int flag) {
        if (flag <= 0) { return false; }
        return ((mods / flag) % 2) != 0;
    }
}

public static class EnumConvert {
    public static int ToInt(Visibility v) {
        switch (v) {
            case Visibility.Visible: return 0;
            case Visibility.Hidden: return 1;
            case Visibility.Collapsed: return 2;
        }
        return 0;
    }
    public static Visibility ToVisibility(int v) {
        if (v == 1) { return Visibility.Hidden; }
        if (v == 2) { return Visibility.Collapsed; }
        return Visibility.Visible;
    }
    public static int ToInt(Orientation v) {
        if (v == Orientation.Vertical) { return 1; }
        return 0;
    }
    public static Orientation ToOrientation(int v) {
        if (v == 1) { return Orientation.Vertical; }
        return Orientation.Horizontal;
    }
    public static int ToInt(HorizontalAlignment v) {
        switch (v) {
            case HorizontalAlignment.Stretch: return 0;
            case HorizontalAlignment.Left: return 1;
            case HorizontalAlignment.Center: return 2;
            case HorizontalAlignment.Right: return 3;
        }
        return 0;
    }
    public static HorizontalAlignment ToHorizontalAlignment(int v) {
        if (v == 1) { return HorizontalAlignment.Left; }
        if (v == 2) { return HorizontalAlignment.Center; }
        if (v == 3) { return HorizontalAlignment.Right; }
        return HorizontalAlignment.Stretch;
    }
    public static int ToInt(VerticalAlignment v) {
        switch (v) {
            case VerticalAlignment.Stretch: return 0;
            case VerticalAlignment.Top: return 1;
            case VerticalAlignment.Center: return 2;
            case VerticalAlignment.Bottom: return 3;
        }
        return 0;
    }
    public static VerticalAlignment ToVerticalAlignment(int v) {
        if (v == 1) { return VerticalAlignment.Top; }
        if (v == 2) { return VerticalAlignment.Center; }
        if (v == 3) { return VerticalAlignment.Bottom; }
        return VerticalAlignment.Stretch;
    }
    public static int ToInt(Stretch v) {
        switch (v) {
            case Stretch.None: return 0;
            case Stretch.Fill: return 1;
            case Stretch.Uniform: return 2;
            case Stretch.UniformToFill: return 3;
        }
        return 2;
    }
    public static Stretch ToStretch(int v) {
        if (v == 0) { return Stretch.None; }
        if (v == 1) { return Stretch.Fill; }
        if (v == 3) { return Stretch.UniformToFill; }
        return Stretch.Uniform;
    }
    public static int ToInt(StretchDirection v) {
        switch (v) {
            case StretchDirection.UpOnly: return 0;
            case StretchDirection.DownOnly: return 1;
            case StretchDirection.Both: return 2;
        }
        return 2;
    }
    public static StretchDirection ToStretchDirection(int v) {
        if (v == 0) { return StretchDirection.UpOnly; }
        if (v == 1) { return StretchDirection.DownOnly; }
        return StretchDirection.Both;
    }
    public static int ToInt(Dock v) {
        switch (v) {
            case Dock.Left: return 0;
            case Dock.Top: return 1;
            case Dock.Right: return 2;
            case Dock.Bottom: return 3;
        }
        return 0;
    }
    public static Dock ToDock(int v) {
        if (v == 1) { return Dock.Top; }
        if (v == 2) { return Dock.Right; }
        if (v == 3) { return Dock.Bottom; }
        return Dock.Left;
    }
    public static int ToInt(TextWrapping v) {
        switch (v) {
            case TextWrapping.NoWrap: return 0;
            case TextWrapping.Wrap: return 1;
            case TextWrapping.WrapWithOverflow: return 2;
        }
        return 0;
    }
    public static int ToInt(TextAlignment v) {
        switch (v) {
            case TextAlignment.Left: return 0;
            case TextAlignment.Center: return 1;
            case TextAlignment.Right: return 2;
            case TextAlignment.Justify: return 3;
        }
        return 0;
    }
    public static int ToInt(TextTrimming v) {
        switch (v) {
            case TextTrimming.None: return 0;
            case TextTrimming.CharacterEllipsis: return 1;
            case TextTrimming.WordEllipsis: return 2;
        }
        return 0;
    }
    public static int ToInt(GridUnitType v) {
        switch (v) {
            case GridUnitType.Auto: return 0;
            case GridUnitType.Pixel: return 1;
            case GridUnitType.Star: return 2;
        }
        return 2;
    }
    public static int ToInt(UIEffectKind v) {
        switch (v) {
            case UIEffectKind.None: return 0;
            case UIEffectKind.Blur: return 1;
            case UIEffectKind.DropShadow: return 2;
            case UIEffectKind.Pixelate: return 3;
            case UIEffectKind.BackdropBlur: return 4;
            case UIEffectKind.Tint: return 5;
            case UIEffectKind.ChromaticAberration: return 6;
            case UIEffectKind.DirectionalBlur: return 7;
            case UIEffectKind.Saturation: return 8;
        }
        return 0;
    }
    public static int ToInt(VisualKind v) {
        switch (v) {
            case VisualKind.Box: return 0;
            case VisualKind.Text: return 1;
            case VisualKind.Image: return 2;
            case VisualKind.Vector: return 3;
            case VisualKind.CanvasDraw: return 4;
        }
        return 0;
    }
    public static int ToInt(LayoutMode v) {
        switch (v) {
            case LayoutMode.Manual: return 0;
            case LayoutMode.Stack: return 1;
            case LayoutMode.Dock: return 2;
            case LayoutMode.Wrap: return 3;
            case LayoutMode.Grid: return 4;
            case LayoutMode.UniformGrid: return 5;
            case LayoutMode.Canvas: return 6;
            case LayoutMode.ScrollViewport: return 7;
            case LayoutMode.Viewbox: return 8;
            case LayoutMode.CustomCallback: return 9;
        }
        return 0;
    }
    public static int ToInt(VectorShapeKind v) {
        switch (v) {
            case VectorShapeKind.SvgPath: return 0;
            case VectorShapeKind.Ellipse: return 1;
            case VectorShapeKind.Line: return 2;
            case VectorShapeKind.RadialArc: return 3;
        }
        return 0;
    }
    public static int ToInt(VisualLayoutRectQuery v) {
        switch (v) {
            case VisualLayoutRectQuery.X: return 0;
            case VisualLayoutRectQuery.Y: return 1;
            case VisualLayoutRectQuery.Width: return 2;
            case VisualLayoutRectQuery.Height: return 3;
            case VisualLayoutRectQuery.DesiredWidth: return 4;
            case VisualLayoutRectQuery.DesiredHeight: return 5;
            case VisualLayoutRectQuery.ScrollExtentWidth: return 6;
            case VisualLayoutRectQuery.ScrollExtentHeight: return 7;
        }
        return 0;
    }
    public static int ToInt(VisualAnimProp v) {
        switch (v) {
            case VisualAnimProp.Opacity: return 0;
            case VisualAnimProp.Width: return 1;
            case VisualAnimProp.Height: return 2;
            case VisualAnimProp.TranslateX: return 3;
            case VisualAnimProp.TranslateY: return 4;
            case VisualAnimProp.ScaleX: return 5;
            case VisualAnimProp.ScaleY: return 6;
            case VisualAnimProp.RotateDeg: return 7;
            case VisualAnimProp.SkewX: return 8;
            case VisualAnimProp.SkewY: return 9;
            case VisualAnimProp.Rot3DX: return 10;
            case VisualAnimProp.Rot3DY: return 11;
            case VisualAnimProp.Rot3DZ: return 12;
            case VisualAnimProp.Trans3DX: return 13;
            case VisualAnimProp.Trans3DY: return 14;
            case VisualAnimProp.Trans3DZ: return 15;
            case VisualAnimProp.BgColor: return 16;
            case VisualAnimProp.FgColor: return 17;
            case VisualAnimProp.BorderColor: return 18;
            case VisualAnimProp.CornerRadius: return 19;
            case VisualAnimProp.PathTrimStart: return 20;
            case VisualAnimProp.PathTrimEnd: return 21;
            case VisualAnimProp.EffectRadius: return 22;
            case VisualAnimProp.CustomScalar0: return 23;
            case VisualAnimProp.CustomScalar1: return 24;
            case VisualAnimProp.CustomScalar2: return 25;
            case VisualAnimProp.CustomScalar3: return 26;
        }
        return 0;
    }
    public static VisualAnimProp ParseAnimProp(string name) {
        string n = name.Trim();
        if (n == "Opacity") { return VisualAnimProp.Opacity; }
        if (n == "Width") { return VisualAnimProp.Width; }
        if (n == "Height") { return VisualAnimProp.Height; }
        if (n == "TranslateX" || n == "RenderTransform.X") { return VisualAnimProp.TranslateX; }
        if (n == "TranslateY" || n == "RenderTransform.Y") { return VisualAnimProp.TranslateY; }
        if (n == "ScaleX" || n == "RenderTransform.ScaleX") { return VisualAnimProp.ScaleX; }
        if (n == "ScaleY" || n == "RenderTransform.ScaleY") { return VisualAnimProp.ScaleY; }
        if (n == "RotateDeg" || n == "Angle" || n == "RenderTransform.Angle") { return VisualAnimProp.RotateDeg; }
        if (n == "SkewX" || n == "RenderTransform.AngleX") { return VisualAnimProp.SkewX; }
        if (n == "SkewY" || n == "RenderTransform.AngleY") { return VisualAnimProp.SkewY; }
        if (n == "Rot3DX" || n == "Rotation3DX") { return VisualAnimProp.Rot3DX; }
        if (n == "Rot3DY" || n == "Rotation3DY") { return VisualAnimProp.Rot3DY; }
        if (n == "Rot3DZ" || n == "Rotation3DZ") { return VisualAnimProp.Rot3DZ; }
        if (n == "Trans3DX" || n == "Translation3DX") { return VisualAnimProp.Trans3DX; }
        if (n == "Trans3DY" || n == "Translation3DY") { return VisualAnimProp.Trans3DY; }
        if (n == "Trans3DZ" || n == "Translation3DZ") { return VisualAnimProp.Trans3DZ; }
        if (n == "Background" || n == "Background.Color" || n == "Fill") { return VisualAnimProp.BgColor; }
        if (n == "Foreground" || n == "Foreground.Color") { return VisualAnimProp.FgColor; }
        if (n == "BorderBrush" || n == "BorderBrush.Color" || n == "Stroke") { return VisualAnimProp.BorderColor; }
        if (n == "CornerRadius") { return VisualAnimProp.CornerRadius; }
        if (n == "TrimStart") { return VisualAnimProp.PathTrimStart; }
        if (n == "TrimEnd") { return VisualAnimProp.PathTrimEnd; }
        if (n == "EffectRadius" || n == "Effect.Radius" || n == "BlurRadius") { return VisualAnimProp.EffectRadius; }
        return VisualAnimProp.Opacity;
    }
    public static int ToInt(EasingFunctionKind v) {
        switch (v) {
            case EasingFunctionKind.Linear: return 0;
            case EasingFunctionKind.Quadratic: return 1;
            case EasingFunctionKind.Cubic: return 2;
            case EasingFunctionKind.Quartic: return 3;
            case EasingFunctionKind.Quintic: return 4;
            case EasingFunctionKind.Sine: return 5;
            case EasingFunctionKind.Circle: return 6;
            case EasingFunctionKind.Exponential: return 7;
            case EasingFunctionKind.Back: return 8;
            case EasingFunctionKind.Bounce: return 9;
            case EasingFunctionKind.Elastic: return 10;
            case EasingFunctionKind.Spline: return 11;
        }
        return 0;
    }
    public static int ToInt(EasingMode v) {
        switch (v) {
            case EasingMode.EaseOut: return 0;
            case EasingMode.EaseIn: return 1;
            case EasingMode.EaseInOut: return 2;
        }
        return 0;
    }
    public static int ToInt(PointerButton v) {
        if (v == PointerButton.Right) { return 1; }
        if (v == PointerButton.Middle) { return 2; }
        return 0;
    }
    public static PointerButton ToPointerButton(int v) {
        if (v == 1) { return PointerButton.Right; }
        if (v == 2) { return PointerButton.Middle; }
        return PointerButton.Left;
    }
    public static KeyCode ToKeyCode(int v) {
        switch (v) {
            case 1: return KeyCode.Tab;
            case 2: return KeyCode.Enter;
            case 3: return KeyCode.Space;
            case 4: return KeyCode.Escape;
            case 5: return KeyCode.Backspace;
            case 6: return KeyCode.Delete;
            case 7: return KeyCode.Left;
            case 8: return KeyCode.Right;
            case 9: return KeyCode.Up;
            case 10: return KeyCode.Down;
            case 11: return KeyCode.Home;
            case 12: return KeyCode.End;
            case 13: return KeyCode.PageUp;
            case 14: return KeyCode.PageDown;
            case 15: return KeyCode.A; case 16: return KeyCode.B; case 17: return KeyCode.C;
            case 18: return KeyCode.D; case 19: return KeyCode.E; case 20: return KeyCode.F;
            case 21: return KeyCode.G; case 22: return KeyCode.H; case 23: return KeyCode.I;
            case 24: return KeyCode.J; case 25: return KeyCode.K; case 26: return KeyCode.L;
            case 27: return KeyCode.M; case 28: return KeyCode.N; case 29: return KeyCode.O;
            case 30: return KeyCode.P; case 31: return KeyCode.Q; case 32: return KeyCode.R;
            case 33: return KeyCode.S; case 34: return KeyCode.T; case 35: return KeyCode.U;
            case 36: return KeyCode.V; case 37: return KeyCode.W; case 38: return KeyCode.X;
            case 39: return KeyCode.Y; case 40: return KeyCode.Z;
            case 41: return KeyCode.D0; case 42: return KeyCode.D1; case 43: return KeyCode.D2;
            case 44: return KeyCode.D3; case 45: return KeyCode.D4; case 46: return KeyCode.D5;
            case 47: return KeyCode.D6; case 48: return KeyCode.D7; case 49: return KeyCode.D8;
            case 50: return KeyCode.D9;
            case 51: return KeyCode.F1; case 52: return KeyCode.F2; case 53: return KeyCode.F3;
            case 54: return KeyCode.F4; case 55: return KeyCode.F5; case 56: return KeyCode.F6;
            case 57: return KeyCode.F7; case 58: return KeyCode.F8; case 59: return KeyCode.F9;
            case 60: return KeyCode.F10; case 61: return KeyCode.F11; case 62: return KeyCode.F12;
        }
        return KeyCode.None;
    }
}

// ============================================================================
// 2. Parsing, Numeric, & Color Helpers (Pure RealScript + Aero.Native)
// ============================================================================

public class HandleNullBox {
    public handle Empty;
}

public static class ParseUtil {
    private static HandleNullBox _nullBox;
    private static bool _nativeReady;

    public static handle EmptyHandle() {
        if (_nullBox == null) { _nullBox = new HandleNullBox(); }
        return _nullBox.Empty;
    }
    public static bool IsValidHandle(handle h) {
        return h != EmptyHandle();
    }
    public static bool IsNativeReady() { return _nativeReady; }
    public static void MarkNativeReady() { _nativeReady = true; }

    public static float NaN() { return 0.0f / 0.0f; }
    public static float PositiveInfinity() { return 1.0f / 0.0f; }
    public static bool IsNaN(float v) { return v != v; }
    public static bool IsFinite(float v) {
        return v == v && v < 1000000000000000000000.0f && v > -1000000000000000000000.0f;
    }

    public static int DigitValue(string ch) {
        if (ch == "0") { return 0; }
        if (ch == "1") { return 1; }
        if (ch == "2") { return 2; }
        if (ch == "3") { return 3; }
        if (ch == "4") { return 4; }
        if (ch == "5") { return 5; }
        if (ch == "6") { return 6; }
        if (ch == "7") { return 7; }
        if (ch == "8") { return 8; }
        if (ch == "9") { return 9; }
        return -1;
    }

    public static int HexDigitValue(string ch) {
        int d = DigitValue(ch);
        if (d >= 0) { return d; }
        if (ch == "a" || ch == "A") { return 10; }
        if (ch == "b" || ch == "B") { return 11; }
        if (ch == "c" || ch == "C") { return 12; }
        if (ch == "d" || ch == "D") { return 13; }
        if (ch == "e" || ch == "E") { return 14; }
        if (ch == "f" || ch == "F") { return 15; }
        return -1;
    }

    public static string HexDigit(uint v) {
        uint x = v % 16u;
        if (x == 0u) { return "0"; }
        if (x == 1u) { return "1"; }
        if (x == 2u) { return "2"; }
        if (x == 3u) { return "3"; }
        if (x == 4u) { return "4"; }
        if (x == 5u) { return "5"; }
        if (x == 6u) { return "6"; }
        if (x == 7u) { return "7"; }
        if (x == 8u) { return "8"; }
        if (x == 9u) { return "9"; }
        if (x == 10u) { return "A"; }
        if (x == 11u) { return "B"; }
        if (x == 12u) { return "C"; }
        if (x == 13u) { return "D"; }
        if (x == 14u) { return "E"; }
        return "F";
    }

    public static int ParseInt(string text, int fallback = 0) {
        if (text == null) { return fallback; }
        string s = text.Trim();
        if (s.Length == 0) { return fallback; }
        int i = 0;
        int sign = 1;
        string first = s.Substring(0, 1);
        if (first == "-") { sign = -1; i = 1; }
        else if (first == "+") { i = 1; }
        if (i >= s.Length) { return fallback; }
        int acc = 0;
        bool any = false;
        while (i < s.Length) {
            int d = DigitValue(s.Substring(i, 1));
            if (d < 0) { break; }
            acc = acc * 10 + d;
            any = true;
            i += 1;
        }
        return any ? (acc * sign) : fallback;
    }

    public static float ParseFloat(string text, float fallback = 0.0f) {
        if (text == null) { return fallback; }
        string s = text.Trim();
        if (s.Length == 0) { return fallback; }
        if (s == "Auto" || s == "auto" || s == "NaN") { return NaN(); }
        int i = 0;
        float sign = 1.0f;
        string first = s.Substring(0, 1);
        if (first == "-") { sign = -1.0f; i = 1; }
        else if (first == "+") { i = 1; }
        float whole = 0.0f;
        float frac = 0.0f;
        float div = 1.0f;
        bool inFrac = false;
        bool any = false;
        while (i < s.Length) {
            string ch = s.Substring(i, 1);
            if (ch == "." && !inFrac) {
                inFrac = true;
                i += 1;
                continue;
            }
            int d = DigitValue(ch);
            if (d < 0) { break; }
            any = true;
            if (!inFrac) {
                whole = whole * 10.0f + (float)d;
            } else {
                div = div * 10.0f;
                frac = frac + ((float)d / div);
            }
            i += 1;
        }
        return any ? ((whole + frac) * sign) : fallback;
    }

    public static bool ParseBool(string text, bool fallback = false) {
        if (text == null) { return fallback; }
        string s = text.Trim().ToLower();
        if (s == "true" || s == "1" || s == "yes") { return true; }
        if (s == "false" || s == "0" || s == "no") { return false; }
        return fallback;
    }

    public static uint PackArgb(uint a, uint r, uint g, uint b) {
        return (a % 256u) * 16777216u + (r % 256u) * 65536u + (g % 256u) * 256u + (b % 256u);
    }

    public static string FormatArgbHex(uint argb) {
        uint a = (argb / 16777216u) % 256u;
        uint r = (argb / 65536u) % 256u;
        uint g = (argb / 256u) % 256u;
        uint b = argb % 256u;
        return "#" + HexDigit(a / 16u) + HexDigit(a % 16u)
                   + HexDigit(r / 16u) + HexDigit(r % 16u)
                   + HexDigit(g / 16u) + HexDigit(g % 16u)
                   + HexDigit(b / 16u) + HexDigit(b % 16u);
    }

    public static uint ParseHexColor(string hex, uint fallback = 0u) {
        if (hex == null) { return fallback; }
        string s = hex.Trim();
        if (s.Length > 0 && s.Substring(0, 1) == "#") {
            s = s.Substring(1);
            if (s.Length == 3 || s.Length == 4) {
                int idx = 0;
                uint a = 255u;
                if (s.Length == 4) {
                    int ha = HexDigitValue(s.Substring(0, 1));
                    if (ha < 0) { return fallback; }
                    a = (uint)(ha * 17);
                    idx = 1;
                }
                int hr = HexDigitValue(s.Substring(idx, 1));
                int hg = HexDigitValue(s.Substring(idx + 1, 1));
                int hb = HexDigitValue(s.Substring(idx + 2, 1));
                if (hr < 0 || hg < 0 || hb < 0) { return fallback; }
                return PackArgb(a, (uint)(hr * 17), (uint)(hg * 17), (uint)(hb * 17));
            }
            if (s.Length == 6 || s.Length == 8) {
                int idx = 0;
                uint a = 255u;
                if (s.Length == 8) {
                    int ha1 = HexDigitValue(s.Substring(0, 1));
                    int ha2 = HexDigitValue(s.Substring(1, 1));
                    if (ha1 < 0 || ha2 < 0) { return fallback; }
                    a = (uint)(ha1 * 16 + ha2);
                    idx = 2;
                }
                int r1 = HexDigitValue(s.Substring(idx, 1));
                int r2 = HexDigitValue(s.Substring(idx + 1, 1));
                int g1 = HexDigitValue(s.Substring(idx + 2, 1));
                int g2 = HexDigitValue(s.Substring(idx + 3, 1));
                int b1 = HexDigitValue(s.Substring(idx + 4, 1));
                int b2 = HexDigitValue(s.Substring(idx + 5, 1));
                if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) { return fallback; }
                return PackArgb(a, (uint)(r1 * 16 + r2), (uint)(g1 * 16 + g2), (uint)(b1 * 16 + b2));
            }
        }
        return fallback;
    }

    public static uint ParseColor(string color) {
        if (color == null || color.Length == 0) { return 0u; }
        string c = color.Trim();
        if (c.Length > 0 && c.Substring(0, 1) == "#") {
            return ParseHexColor(c, 0u);
        }
        string low = c.ToLower();
        if (low == "transparent") { return 0u; }
        if (low == "black") { return PackArgb(255u, 0u, 0u, 0u); }
        if (low == "white") { return PackArgb(255u, 255u, 255u, 255u); }
        if (low == "red") { return PackArgb(255u, 255u, 0u, 0u); }
        if (low == "green") { return PackArgb(255u, 0u, 128u, 0u); }
        if (low == "lime") { return PackArgb(255u, 0u, 255u, 0u); }
        if (low == "blue") { return PackArgb(255u, 0u, 0u, 255u); }
        if (low == "yellow") { return PackArgb(255u, 255u, 255u, 0u); }
        if (low == "cyan" || low == "aqua") { return PackArgb(255u, 0u, 255u, 255u); }
        if (low == "magenta" || low == "fuchsia") { return PackArgb(255u, 255u, 0u, 255u); }
        if (low == "gray" || low == "grey") { return PackArgb(255u, 128u, 128u, 128u); }
        if (low == "orange") { return PackArgb(255u, 255u, 165u, 0u); }
        if (low == "gold") { return PackArgb(255u, 255u, 215u, 0u); }
        if (low == "silver") { return PackArgb(255u, 192u, 192u, 192u); }
        return Color_ParseRgba(c);
    }

    public static uint ApplyOpacityToArgb(uint argb, float opacity) {
        if (opacity >= 0.9999f) { return argb; }
        if (opacity <= 0.0f) { return argb % 16777216u; }
        uint a = (argb / 16777216u) % 256u;
        uint scaledA = (uint)Math.Clampf((float)a * opacity, 0.0f, 255.0f);
        return (argb % 16777216u) + scaledA * 16777216u;
    }

    public static int Utf8CodePointCount(string text) {
        if (text == null) { return 0; }
        return text.Length;
    }

    public static string SliceCodePoints(string text, int startCp, int countCp) {
        if (text == null || countCp <= 0 || startCp >= text.Length) { return ""; }
        int s = startCp < 0 ? 0 : startCp;
        int avail = text.Length - s;
        int len = countCp > avail ? avail : countCp;
        return text.Substring(s, len);
    }

    public static int FindNextWordBoundary(string text, int fromCp) {
        if (text == null || fromCp >= text.Length) { return text == null ? 0 : text.Length; }
        int i = fromCp < 0 ? 0 : fromCp;
        while (i < text.Length && text.Substring(i, 1) == " ") { i += 1; }
        while (i < text.Length && text.Substring(i, 1) != " ") { i += 1; }
        return i;
    }

    public static int FindPrevWordBoundary(string text, int fromCp) {
        if (text == null || fromCp <= 0) { return 0; }
        int i = fromCp > text.Length ? text.Length : fromCp;
        while (i > 0 && text.Substring(i - 1, 1) == " ") { i -= 1; }
        while (i > 0 && text.Substring(i - 1, 1) != " ") { i -= 1; }
        return i;
    }
}

// ============================================================================
// 3. DependencyObject & Type-Safe Boxing (Zero RS5151 bare-object fields)
// ============================================================================

public class DependencyObject {
    public virtual object UnboxObject() { return this; }
    public virtual string ToDisplayString() { return ""; }

    public static DependencyObject BoxObject(object value) {
        if (value == null) { return null; }
        if (value is DependencyObject) { return value as DependencyObject; }
        if (value is ICollectionNotifier) { return new CollectionBox(value as ICollectionNotifier); }
        return new NativeObjectBox(value);
    }

    public static object Unbox(DependencyObject box) {
        if (box == null) { return null; }
        return box.UnboxObject();
    }

    public static DependencyObject BoxString(string v) { return new BoxedString(v); }
    public static DependencyObject BoxFloat(float v) { return new BoxedFloat(v); }
    public static DependencyObject BoxInt(int v) { return new BoxedInt(v); }
    public static DependencyObject BoxBool(bool v) { return new BoxedBool(v); }

    public static string UnboxString(DependencyObject box, string fallback = "") {
        if (box == null) { return fallback; }
        if (box is BoxedString) { return (box as BoxedString).Value; }
        if (box is BoxedFloat) { return "" + (box as BoxedFloat).Value; }
        if (box is BoxedInt) { return "" + (box as BoxedInt).Value; }
        if (box is BoxedBool) { return (box as BoxedBool).Value ? "True" : "False"; }
        return box.ToDisplayString();
    }

    public static float UnboxFloat(DependencyObject box, float fallback = 0.0f) {
        if (box == null) { return fallback; }
        if (box is BoxedFloat) { return (box as BoxedFloat).Value; }
        if (box is BoxedInt) { return (float)(box as BoxedInt).Value; }
        if (box is BoxedString) { return ParseUtil.ParseFloat((box as BoxedString).Value, fallback); }
        return fallback;
    }

    public static int UnboxInt(DependencyObject box, int fallback = 0) {
        if (box == null) { return fallback; }
        if (box is BoxedInt) { return (box as BoxedInt).Value; }
        if (box is BoxedFloat) { return (int)(box as BoxedFloat).Value; }
        if (box is BoxedString) { return ParseUtil.ParseInt((box as BoxedString).Value, fallback); }
        return fallback;
    }

    public static bool UnboxBool(DependencyObject box, bool fallback = false) {
        if (box == null) { return fallback; }
        if (box is BoxedBool) { return (box as BoxedBool).Value; }
        if (box is BoxedInt) { return (box as BoxedInt).Value != 0; }
        if (box is BoxedString) { return ParseUtil.ParseBool((box as BoxedString).Value, fallback); }
        return fallback;
    }
}

public class TypedBox<T> : DependencyObject {
    public T Value;
    public TypedBox(T v) { Value = v; }
    public override string ToDisplayString() { return "" + Value; }
    public override object UnboxObject() {
        if (ParseUtil.IsNativeReady()) {
            var r = Reflect_GetPropObject(this, "Value");
            if (r != null) { return r; }
        }
        return this;
    }
}

public class BoxedString : TypedBox<string> {
    public BoxedString(string v) : base(v) {}
}

public class BoxedFloat : TypedBox<float> {
    public BoxedFloat(float v) : base(v) {}
}

public class BoxedInt : TypedBox<int> {
    public BoxedInt(int v) : base(v) {}
}

public class BoxedBool : TypedBox<bool> {
    public BoxedBool(bool v) : base(v) {}
}

public class CollectionBox : DependencyObject {
    public ICollectionNotifier Source;
    public CollectionBox(ICollectionNotifier src) { Source = src; }
    public override object UnboxObject() { return Source; }
    public override string ToDisplayString() {
        return Source != null ? ("Collection[" + Source.ItemCount() + "]") : "";
    }
}

public class NativeObjectBox : DependencyObject {
    private handle _slot;
    private bool _hasSlot;

    public NativeObjectBox(object value) {
        _slot = Visual_Create(0);
        _hasSlot = true;
        ParseUtil.MarkNativeReady();
        Visual_BindOwner(_slot, value, 0);
    }

    public override object UnboxObject() {
        if (!_hasSlot) { return null; }
        return Visual_GetOwner(_slot);
    }

    public void Release() {
        if (_hasSlot) {
            Visual_Destroy(_slot);
            _hasSlot = false;
        }
    }
}

// ============================================================================
// 4. Geometry & Layout Primitives
// ============================================================================

public class Thickness : DependencyObject {
    public float Left;
    public float Top;
    public float Right;
    public float Bottom;

    public Thickness() {}
    public Thickness(float uniform) {
        Left = uniform; Top = uniform; Right = uniform; Bottom = uniform;
    }
    public Thickness(float horizontal, float vertical) {
        Left = horizontal; Top = vertical; Right = horizontal; Bottom = vertical;
    }
    public Thickness(float left, float top, float right, float bottom) {
        Left = left; Top = top; Right = right; Bottom = bottom;
    }

    public Thickness Clone() { return new Thickness(Left, Top, Right, Bottom); }
    public override string ToDisplayString() {
        return "" + Left + "," + Top + "," + Right + "," + Bottom;
    }

    public static Thickness Parse(string text) {
        if (text == null || text.Length == 0) { return new Thickness(0.0f); }
        string[] parts = text.Trim().Split(",");
        if (parts.Length == 1) {
            return new Thickness(ParseUtil.ParseFloat(parts[0], 0.0f));
        }
        if (parts.Length == 2) {
            return new Thickness(
                ParseUtil.ParseFloat(parts[0], 0.0f),
                ParseUtil.ParseFloat(parts[1], 0.0f));
        }
        if (parts.Length >= 4) {
            return new Thickness(
                ParseUtil.ParseFloat(parts[0], 0.0f),
                ParseUtil.ParseFloat(parts[1], 0.0f),
                ParseUtil.ParseFloat(parts[2], 0.0f),
                ParseUtil.ParseFloat(parts[3], 0.0f));
        }
        return new Thickness(0.0f);
    }
}

public class CornerRadius : DependencyObject {
    public float TopLeft;
    public float TopRight;
    public float BottomRight;
    public float BottomLeft;

    public CornerRadius() {}
    public CornerRadius(float uniform) {
        TopLeft = uniform; TopRight = uniform; BottomRight = uniform; BottomLeft = uniform;
    }
    public CornerRadius(float topLeft, float topRight, float bottomRight, float bottomLeft) {
        TopLeft = topLeft; TopRight = topRight; BottomRight = bottomRight; BottomLeft = bottomLeft;
    }

    public CornerRadius Clone() {
        return new CornerRadius(TopLeft, TopRight, BottomRight, BottomLeft);
    }
    public override string ToDisplayString() {
        return "" + TopLeft + "," + TopRight + "," + BottomRight + "," + BottomLeft;
    }

    public static CornerRadius Parse(string text) {
        if (text == null || text.Length == 0) { return new CornerRadius(0.0f); }
        string[] parts = text.Trim().Split(",");
        if (parts.Length == 1) {
            return new CornerRadius(ParseUtil.ParseFloat(parts[0], 0.0f));
        }
        if (parts.Length >= 4) {
            return new CornerRadius(
                ParseUtil.ParseFloat(parts[0], 0.0f),
                ParseUtil.ParseFloat(parts[1], 0.0f),
                ParseUtil.ParseFloat(parts[2], 0.0f),
                ParseUtil.ParseFloat(parts[3], 0.0f));
        }
        return new CornerRadius(0.0f);
    }
}

public class Point : DependencyObject {
    public float X;
    public float Y;
    public Point() {}
    public Point(float x, float y) { X = x; Y = y; }
    public override string ToDisplayString() { return "" + X + "," + Y; }
    public static Point Parse(string text) {
        if (text == null) { return new Point(0.0f, 0.0f); }
        string[] parts = text.Trim().Split(",");
        if (parts.Length >= 2) {
            return new Point(ParseUtil.ParseFloat(parts[0], 0.0f), ParseUtil.ParseFloat(parts[1], 0.0f));
        }
        return new Point(0.0f, 0.0f);
    }
}

public class Size : DependencyObject {
    public float Width;
    public float Height;
    public Size() {}
    public Size(float w, float h) { Width = w; Height = h; }
    public override string ToDisplayString() { return "" + Width + "," + Height; }
}

public class Rect : DependencyObject {
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public Rect() {}
    public Rect(float x, float y, float w, float h) { X = x; Y = y; Width = w; Height = h; }
    public bool Contains(float px, float py) {
        return px >= X && py >= Y && px <= (X + Width) && py <= (Y + Height);
    }
    public override string ToDisplayString() {
        return "" + X + "," + Y + "," + Width + "," + Height;
    }
}

// ============================================================================
// 5. Brushes, Transforms, and Effects
// ============================================================================

public class Brush : DependencyObject {
    public float Opacity { get; set; } = 1.0f;
    public virtual uint ResolveRgba() { return 0u; }
    public virtual int ResolveGradId() { return -1; }
}

public class SolidColorBrush : Brush {
    private uint _rgba;
    private string _colorStr = "";

    public SolidColorBrush() {}
    public SolidColorBrush(uint argb) {
        _rgba = argb;
    }
    public SolidColorBrush(string color) {
        _colorStr = color == null ? "" : color;
        _rgba = ParseUtil.ParseColor(_colorStr);
    }
    public SolidColorBrush(uint argb, float opacity) {
        _rgba = argb;
        Opacity = opacity;
    }

    public uint ColorRgba {
        get { return _rgba; }
        set { _rgba = value; _colorStr = ""; }
    }
    public string Color {
        get {
            if (_colorStr.Length == 0) { _colorStr = ParseUtil.FormatArgbHex(_rgba); }
            return _colorStr;
        }
        set { _colorStr = value == null ? "" : value; _rgba = ParseUtil.ParseColor(_colorStr); }
    }

    public override uint ResolveRgba() {
        return ParseUtil.ApplyOpacityToArgb(_rgba, Opacity);
    }
    public override string ToDisplayString() { return Color; }
}

public static class Brushes {
    private static SolidColorBrush _transparent;
    private static SolidColorBrush _black;
    private static SolidColorBrush _white;
    private static SolidColorBrush _defaultFg;

    public static SolidColorBrush Transparent() {
        if (_transparent == null) { _transparent = new SolidColorBrush(0u); }
        return _transparent;
    }
    public static SolidColorBrush Black() {
        if (_black == null) { _black = new SolidColorBrush(4278190080u); }
        return _black;
    }
    public static SolidColorBrush White() {
        if (_white == null) { _white = new SolidColorBrush(4294967295u); }
        return _white;
    }
    public static SolidColorBrush DefaultForeground() {
        if (_defaultFg == null) { _defaultFg = new SolidColorBrush(ParseUtil.PackArgb(255u, 241u, 245u, 249u)); }
        return _defaultFg;
    }
    public static SolidColorBrush Red() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 255u, 0u, 0u)); }
    public static SolidColorBrush Green() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 128u, 0u)); }
    public static SolidColorBrush Blue() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 0u, 255u)); }
    public static SolidColorBrush Yellow() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 255u, 255u, 0u)); }
    public static SolidColorBrush Cyan() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 255u, 255u)); }
    public static SolidColorBrush Gray() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 128u, 128u, 128u)); }
    public static SolidColorBrush FromHex(string hex) { return new SolidColorBrush(hex); }
}

public class GradientStop : DependencyObject {
    public uint ColorRgba { get; set; }
    public string Color {
        get { return ParseUtil.FormatArgbHex(ColorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }
    public float Offset { get; set; }

    public GradientStop() {}
    public GradientStop(uint rgba, float offset) { ColorRgba = rgba; Offset = offset; }
    public GradientStop(string color, float offset) { ColorRgba = ParseUtil.ParseColor(color); Offset = offset; }
}

public class GradientBrush : Brush {
    private GradientStop[] _stops;
    protected int _cachedGradId = -1;
    protected bool _dirty = true;
    public int SpreadMethod { get; set; } = 0;
    public int MappingMode { get; set; } = 1;

    public GradientBrush() { _stops = new GradientStop[0]; }

    public GradientStop[] GradientStops() { return _stops; }

    public void AddStop(GradientStop stop) {
        if (stop == null) { return; }
        _stops.Push(stop);
        _dirty = true;
    }
    public void AddStop(uint rgba, float offset) {
        AddStop(new GradientStop(rgba, offset));
    }
    public void AddStop(string color, float offset) {
        AddStop(new GradientStop(color, offset));
    }
    public void ClearStops() {
        _stops.Clear();
        _dirty = true;
    }

    protected string BuildStopsSpec() {
        string spec = "";
        for (int i = 0; i < _stops.Length; i += 1) {
            if (i > 0) { spec = spec + ";"; }
            uint col = ParseUtil.ApplyOpacityToArgb(_stops[i].ColorRgba, Opacity);
            spec = spec + _stops[i].Offset + ":" + ParseUtil.FormatArgbHex(col);
        }
        return spec;
    }

    public override uint ResolveRgba() {
        if (_stops.Length > 0) { return _stops[0].ColorRgba; }
        return 0u;
    }
}

public class LinearGradientBrush : GradientBrush {
    private float _x0 = 0.0f;
    private float _y0 = 0.0f;
    private float _x1 = 0.0f;
    private float _y1 = 1.0f;

    public LinearGradientBrush() : base() {}
    public LinearGradientBrush(float x0, float y0, float x1, float y1) : base() {
        _x0 = x0; _y0 = y0; _x1 = x1; _y1 = y1;
    }
    public LinearGradientBrush(Point start, Point end) : base() {
        if (start != null) { _x0 = start.X; _y0 = start.Y; }
        if (end != null) { _x1 = end.X; _y1 = end.Y; }
    }

    public float X0 { get { return _x0; } set { _x0 = value; _dirty = true; } }
    public float Y0 { get { return _y0; } set { _y0 = value; _dirty = true; } }
    public float X1 { get { return _x1; } set { _x1 = value; _dirty = true; } }
    public float Y1 { get { return _y1; } set { _y1 = value; _dirty = true; } }
    public Point StartPoint {
        get { return new Point(_x0, _y0); }
        set { if (value != null) { _x0 = value.X; _y0 = value.Y; _dirty = true; } }
    }
    public Point EndPoint {
        get { return new Point(_x1, _y1); }
        set { if (value != null) { _x1 = value.X; _y1 = value.Y; _dirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterLinearGradient(
                _x0, _y0, _x1, _y1, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }
}

public class RadialGradientBrush : GradientBrush {
    private float _cx = 0.5f;
    private float _cy = 0.5f;
    private float _ox = 0.5f;
    private float _oy = 0.5f;
    private float _rx = 0.5f;
    private float _ry = 0.5f;

    public RadialGradientBrush() : base() {}
    public RadialGradientBrush(float cx, float cy, float rx, float ry) : base() {
        _cx = cx; _cy = cy; _ox = cx; _oy = cy; _rx = rx; _ry = ry;
    }

    public float CenterX { get { return _cx; } set { _cx = value; _dirty = true; } }
    public float CenterY { get { return _cy; } set { _cy = value; _dirty = true; } }
    public float OriginX { get { return _ox; } set { _ox = value; _dirty = true; } }
    public float OriginY { get { return _oy; } set { _oy = value; _dirty = true; } }
    public float RadiusX { get { return _rx; } set { _rx = value; _dirty = true; } }
    public float RadiusY { get { return _ry; } set { _ry = value; _dirty = true; } }
    public Point Center {
        get { return new Point(_cx, _cy); }
        set { if (value != null) { _cx = value.X; _cy = value.Y; _dirty = true; } }
    }
    public Point GradientOrigin {
        get { return new Point(_ox, _oy); }
        set { if (value != null) { _ox = value.X; _oy = value.Y; _dirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterRadialGradient(
                _cx, _cy, _ox, _oy, _rx, _ry, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }
}

public class TransformData : DependencyObject {
    public float TX = 0.0f;
    public float TY = 0.0f;
    public float SX = 1.0f;
    public float SY = 1.0f;
    public float RotDeg = 0.0f;
    public float SkewX = 0.0f;
    public float SkewY = 0.0f;
}

public class Transform : DependencyObject {
    public virtual void Accumulate(TransformData acc) {}
}

public class TranslateTransform : Transform {
    public float X { get; set; }
    public float Y { get; set; }
    public TranslateTransform() {}
    public TranslateTransform(float x, float y) { X = x; Y = y; }
    public override void Accumulate(TransformData acc) {
        acc.TX = acc.TX + X;
        acc.TY = acc.TY + Y;
    }
}

public class ScaleTransform : Transform {
    public float ScaleX { get; set; } = 1.0f;
    public float ScaleY { get; set; } = 1.0f;
    public float CenterX { get; set; }
    public float CenterY { get; set; }
    public ScaleTransform() {}
    public ScaleTransform(float sx, float sy) { ScaleX = sx; ScaleY = sy; }
    public ScaleTransform(float sx, float sy, float cx, float cy) {
        ScaleX = sx; ScaleY = sy; CenterX = cx; CenterY = cy;
    }
    public override void Accumulate(TransformData acc) {
        acc.SX = acc.SX * ScaleX;
        acc.SY = acc.SY * ScaleY;
    }
}

public class RotateTransform : Transform {
    public float Angle { get; set; }
    public float CenterX { get; set; }
    public float CenterY { get; set; }
    public RotateTransform() {}
    public RotateTransform(float angle) { Angle = angle; }
    public RotateTransform(float angle, float cx, float cy) {
        Angle = angle; CenterX = cx; CenterY = cy;
    }
    public override void Accumulate(TransformData acc) {
        acc.RotDeg = acc.RotDeg + Angle;
    }
}

public class SkewTransform : Transform {
    public float AngleX { get; set; }
    public float AngleY { get; set; }
    public float CenterX { get; set; }
    public float CenterY { get; set; }
    public SkewTransform() {}
    public SkewTransform(float ax, float ay) { AngleX = ax; AngleY = ay; }
    public override void Accumulate(TransformData acc) {
        acc.SkewX = acc.SkewX + AngleX;
        acc.SkewY = acc.SkewY + AngleY;
    }
}

public class TransformGroup : Transform {
    private Transform[] _children;
    public TransformGroup() { _children = new Transform[0]; }
    public Transform[] Children() { return _children; }
    public void Add(Transform t) { if (t != null) { _children.Push(t); } }
    public void Clear() { _children.Clear(); }
    public override void Accumulate(TransformData acc) {
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].Accumulate(acc);
        }
    }
}

public class Effect : DependencyObject {
    public UIEffectKind Kind { get; set; } = UIEffectKind.None;
    public float Radius { get; set; }
    public float Depth { get; set; }
    public float Direction { get; set; } = 315.0f;
    public uint ColorRgba { get; set; } = 4278190080u;
    public float Opacity { get; set; } = 1.0f;

    public string Color {
        get { return ParseUtil.FormatArgbHex(ColorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }
}

public class UIEffect : Effect {
    public UIEffect() {}
    public UIEffect(UIEffectKind kind, float radius = 0.0f) { Kind = kind; Radius = radius; }
}

public class BlurEffect : Effect {
    public BlurEffect() { Kind = UIEffectKind.Blur; Radius = 8.0f; }
    public BlurEffect(float radius) { Kind = UIEffectKind.Blur; Radius = radius; }
}

public class DropShadowEffect : Effect {
    public DropShadowEffect() {
        Kind = UIEffectKind.DropShadow;
        Radius = 10.0f;
        Depth = 4.0f;
        Direction = 315.0f;
        Opacity = 0.45f;
        ColorRgba = ParseUtil.PackArgb(255u, 0u, 0u, 0u);
    }
    public DropShadowEffect(float blurRadius, float shadowDepth = 4.0f, float direction = 315.0f, string color = "#FF000000", float opacity = 0.45f) {
        Kind = UIEffectKind.DropShadow;
        Radius = blurRadius;
        Depth = shadowDepth;
        Direction = direction;
        ColorRgba = ParseUtil.ParseColor(color);
        Opacity = opacity;
    }
    public float BlurRadius { get { return Radius; } set { Radius = value; } }
    public float ShadowDepth { get { return Depth; } set { Depth = value; } }
}

public class BackdropBlurEffect : Effect {
    public BackdropBlurEffect(float radius = 16.0f) { Kind = UIEffectKind.BackdropBlur; Radius = radius; }
}

public class TintEffect : Effect {
    public TintEffect(string color = "#FF2266FF", float opacity = 0.5f) {
        Kind = UIEffectKind.Tint;
        ColorRgba = ParseUtil.ParseColor(color);
        Opacity = opacity;
    }
}

public class PixelateEffect : Effect {
    public PixelateEffect(float pixelSize = 6.0f) { Kind = UIEffectKind.Pixelate; Radius = pixelSize; }
}

public class ChromaticAberrationEffect : Effect {
    public ChromaticAberrationEffect(float intensity = 3.0f) { Kind = UIEffectKind.ChromaticAberration; Radius = intensity; }
}

public class DirectionalBlurEffect : Effect {
    public DirectionalBlurEffect(float radius = 8.0f, float direction = 0.0f) {
        Kind = UIEffectKind.DirectionalBlur; Radius = radius; Direction = direction;
    }
}

public class SaturationEffect : Effect {
    public SaturationEffect(float factor = 1.0f) { Kind = UIEffectKind.Saturation; Radius = factor; }
}

// ============================================================================
// 6. UIElement & FrameworkElement
// ============================================================================

public delegate void PopupDismissHook(UIElement clickedTarget, bool isEscape);

[Observable]
public partial class UIElement : DependencyObject {
    public handle __nativeHandle;
    private bool _hasNative;

    private string _name = "";
    private string _uid = "";
    private DependencyObject _tag;
    private DependencyObject _toolTip;
    private DependencyObject _explicitDataContext;
    private DependencyObject _inheritedDataContext;
    private UIElement _templatedParent;
    private UIElement _parent;
    private UIElement[] _children;

    private ResourceDictionary _resources;
    private Style _style;
    private string[] _dynResProps;
    private string[] _dynResKeys;

    private float _width = ParseUtil.NaN();
    private float _height = ParseUtil.NaN();
    private float _minWidth = 0.0f;
    private float _minHeight = 0.0f;
    private float _maxWidth = ParseUtil.PositiveInfinity();
    private float _maxHeight = ParseUtil.PositiveInfinity();
    private Thickness _margin;
    private Thickness _padding;
    private HorizontalAlignment _hAlign = HorizontalAlignment.Stretch;
    private VerticalAlignment _vAlign = VerticalAlignment.Stretch;
    private Visibility _visibility = Visibility.Visible;
    private float _opacity = 1.0f;
    private bool _isEnabled = true;
    private bool _isHitTestVisible = true;
    private bool _focusable = false;
    private bool _isFocused = false;
    private bool _isMouseOver = false;
    private bool _isPressed = false;
    private bool _clipToBounds = false;
    private int _zIndex = 0;

    private int _gridRow = 0;
    private int _gridCol = 0;
    private int _gridRowSpan = 1;
    private int _gridColSpan = 1;
    private Dock _dock = Dock.Left;
    private float _canvasLeft = ParseUtil.NaN();
    private float _canvasTop = ParseUtil.NaN();
    private float _canvasRight = ParseUtil.NaN();
    private float _canvasBottom = ParseUtil.NaN();

    private Transform _renderTransform;
    private Point _renderTransformOrigin;
    private float _rot3DX = 0.0f;
    private float _rot3DY = 0.0f;
    private float _rot3DZ = 0.0f;
    private float _trans3DX = 0.0f;
    private float _trans3DY = 0.0f;
    private float _trans3DZ = 0.0f;
    private float _camDist = 0.0f;
    private Effect _effect;
    private BlendingMode _blendMode = BlendingMode.Normal;

    private Brush _background;
    private Brush _borderBrush;
    private Thickness _borderThickness;
    private CornerRadius _cornerRadius;

    private bool _loadedFired = false;
    private bool _isStyleApplying = false;

    public event RoutedEventHandler Loaded;
    public event RoutedEventHandler Unloaded;
    public event RoutedEventHandler GotFocus;
    public event RoutedEventHandler LostFocus;
    public event MouseEventHandler MouseEnter;
    public event MouseEventHandler MouseLeave;
    public event MouseEventHandler MouseMove;
    public event MouseButtonEventHandler MouseLeftButtonDown;
    public event MouseButtonEventHandler MouseLeftButtonUp;
    public event MouseButtonEventHandler MouseRightButtonDown;
    public event MouseButtonEventHandler MouseRightButtonUp;
    public event MouseButtonEventHandler PreviewMouseLeftButtonDown;
    public event MouseWheelEventHandler MouseWheel;
    public event KeyEventHandler KeyDown;
    public event KeyEventHandler KeyUp;
    public event KeyEventHandler PreviewKeyDown;
    public event TextCompositionEventHandler TextInput;
    public event DependencyPropertyChangedEventHandler DataContextChanged;
    public event SizeChangedEventHandler SizeChanged;

    public UIElement() {
        InitElement(VisualKind.Box);
    }

    protected UIElement(VisualKind kind) {
        InitElement(kind);
    }

    private void InitElement(VisualKind kind) {
        _children = new UIElement[0];
        _margin = new Thickness(0.0f);
        _padding = new Thickness(0.0f);
        _borderThickness = new Thickness(0.0f);
        _cornerRadius = new CornerRadius(0.0f);
        _renderTransformOrigin = new Point(0.5f, 0.5f);
        _dynResProps = new string[0];
        _dynResKeys = new string[0];
        CreateNativeVisual(kind);
    }

    protected virtual int GetCallbackMask() {
        return VisualCallbacks.StandardInput();
    }

    protected void CreateNativeVisual(VisualKind kind) {
        __nativeHandle = Visual_Create(EnumConvert.ToInt(kind));
        _hasNative = true;
        ParseUtil.MarkNativeReady();
        Visual_BindOwner(__nativeHandle, this, GetCallbackMask());
    }

    protected void UpdateCallbackMask(int mask) {
        if (_hasNative) {
            Visual_BindOwner(__nativeHandle, this, mask);
        }
    }

    public virtual string GetTypeName() { return "UIElement"; }

    public string Name {
        get { return _name; }
        set { _name = value == null ? "" : value; }
    }
    public string Uid {
        get { return _uid; }
        set { _uid = value == null ? "" : value; }
    }
    public object Tag {
        get { return DependencyObject.Unbox(_tag); }
        set { _tag = DependencyObject.BoxObject(value); }
    }
    public object ToolTip {
        get { return DependencyObject.Unbox(_toolTip); }
        set { _toolTip = DependencyObject.BoxObject(value); }
    }
    public DependencyObject ToolTipBox {
        get { return _toolTip; }
        set { _toolTip = value; }
    }
    public void SetToolTip(string text) { _toolTip = DependencyObject.BoxString(text); }

    public UIElement Parent { get { return _parent; } }
    public UIElement TemplatedParent {
        get { return _templatedParent; }
        set { _templatedParent = value; }
    }

    public object DataContext {
        get {
            if (_explicitDataContext != null) { return DependencyObject.Unbox(_explicitDataContext); }
            return DependencyObject.Unbox(_inheritedDataContext);
        }
        set {
            DependencyObject oldBox = _explicitDataContext != null ? _explicitDataContext : _inheritedDataContext;
            _explicitDataContext = DependencyObject.BoxObject(value);
            PropagateDataContextToSubtree(oldBox);
        }
    }

    public DependencyObject DataContextBox {
        get { return _explicitDataContext != null ? _explicitDataContext : _inheritedDataContext; }
        set {
            DependencyObject oldBox = _explicitDataContext != null ? _explicitDataContext : _inheritedDataContext;
            _explicitDataContext = value;
            PropagateDataContextToSubtree(oldBox);
        }
    }

    public void SetDataContext(object dc) { DataContext = dc; }
    public void ClearLocalDataContext() {
        DependencyObject oldBox = _explicitDataContext;
        _explicitDataContext = null;
        PropagateDataContextToSubtree(oldBox);
    }

    internal void ReceiveInheritedDataContext(DependencyObject parentDc) {
        if (_explicitDataContext != null) { return; }
        DependencyObject oldBox = _inheritedDataContext;
        _inheritedDataContext = parentDc;
        if (oldBox != parentDc) {
            RaiseDataContextChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(parentDc));
            for (int i = 0; i < _children.Length; i += 1) {
                _children[i].ReceiveInheritedDataContext(EffectiveDataContextBox());
            }
        }
    }

    internal DependencyObject EffectiveDataContextBox() {
        return _explicitDataContext != null ? _explicitDataContext : _inheritedDataContext;
    }

    private void PropagateDataContextToSubtree(DependencyObject oldBox) {
        DependencyObject eff = EffectiveDataContextBox();
        RaiseDataContextChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(eff));
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].ReceiveInheritedDataContext(eff);
        }
    }

    private void RaiseDataContextChanged(object oldVal, object newVal) {
        OnDataContextChanged(oldVal, newVal);
        DataContextChanged(this, new DependencyPropertyChangedEventArgs("DataContext", oldVal, newVal));
    }

    public ResourceDictionary Resources {
        get {
            if (_resources == null) { _resources = new ResourceDictionary(); }
            return _resources;
        }
        set { _resources = value; RefreshDynamicResourcesRecursive(); }
    }

    public Style Style {
        get { return _style; }
        set {
            if (_style == value) { return; }
            _style = value;
            ApplyStyleInternal();
        }
    }

    public void SetResourceReference(string propName, string resourceKey) {
        if (propName == null || resourceKey == null) { return; }
        for (int i = 0; i < _dynResProps.Length; i += 1) {
            if (_dynResProps[i] == propName) {
                _dynResKeys[i] = resourceKey;
                ApplyDynamicResourceAt(i);
                return;
            }
        }
        _dynResProps.Push(propName);
        _dynResKeys.Push(resourceKey);
        ApplyDynamicResourceAt(_dynResProps.Length - 1);
    }

    private void ApplyDynamicResourceAt(int idx) {
        DependencyObject res = FindResourceBox(_dynResKeys[idx]);
        if (res != null) {
            SetPropertyValue(_dynResProps[idx], res);
        }
    }

    public void RefreshDynamicResourcesRecursive() {
        for (int i = 0; i < _dynResProps.Length; i += 1) {
            ApplyDynamicResourceAt(i);
        }
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].RefreshDynamicResourcesRecursive();
        }
    }

    public void ApplyStyleInternal() {
        if (_isStyleApplying) { return; }
        _isStyleApplying = true;
        Style active = _style;
        if (active == null) {
            DependencyObject themeRes = FindResourceBox(GetTypeName());
            if (themeRes is Style) { active = themeRes as Style; }
        }
        if (active != null) {
            active.ApplyTo(this);
            OnStyleApplied(active);
        }
        _isStyleApplying = false;
    }

    // ---- Layout & Geometry Properties ----
    public float Width { get { return _width; } set { _width = value; SyncSize(); } }
    public float Height { get { return _height; } set { _height = value; SyncSize(); } }
    public float MinWidth { get { return _minWidth; } set { _minWidth = value; SyncMinMaxSize(); } }
    public float MinHeight { get { return _minHeight; } set { _minHeight = value; SyncMinMaxSize(); } }
    public float MaxWidth { get { return _maxWidth; } set { _maxWidth = value; SyncMinMaxSize(); } }
    public float MaxHeight { get { return _maxHeight; } set { _maxHeight = value; SyncMinMaxSize(); } }

    public Thickness Margin {
        get { return _margin; }
        set { _margin = value == null ? new Thickness(0.0f) : value; SyncMargin(); }
    }
    public Thickness Padding {
        get { return _padding; }
        set { _padding = value == null ? new Thickness(0.0f) : value; SyncPadding(); }
    }
    public HorizontalAlignment HorizontalAlignment {
        get { return _hAlign; }
        set { _hAlign = value; SyncAlignment(); }
    }
    public VerticalAlignment VerticalAlignment {
        get { return _vAlign; }
        set { _vAlign = value; SyncAlignment(); }
    }
    public Visibility Visibility {
        get { return _visibility; }
        set {
            if (_visibility == value) { return; }
            _visibility = value;
            if (_hasNative) { Visual_SetVisibility(__nativeHandle, EnumConvert.ToInt(value)); }
            OnVisualStatePropertyChanged();
        }
    }
    public float Opacity {
        get { return _opacity; }
        set {
            _opacity = Math.Clampf(value, 0.0f, 1.0f);
            if (_hasNative) { Visual_SetOpacity(__nativeHandle, _opacity); }
        }
    }
    public bool IsEnabled {
        get { return _isEnabled; }
        set {
            if (_isEnabled == value) { return; }
            _isEnabled = value;
            SyncInteractiveFlags();
            OnEnabledChanged();
            UpdateTriggersAndStates();
        }
    }
    public bool IsHitTestVisible {
        get { return _isHitTestVisible; }
        set { _isHitTestVisible = value; SyncInteractiveFlags(); }
    }
    public bool Focusable {
        get { return _focusable; }
        set { _focusable = value; SyncInteractiveFlags(); }
    }
    public bool IsFocused { get { return _isFocused; } }
    public bool IsMouseOver { get { return _isMouseOver; } }
    public bool IsPressed { get { return _isPressed; } }
    public bool ClipToBounds {
        get { return _clipToBounds; }
        set {
            _clipToBounds = value;
            if (_hasNative) { Visual_SetClipToBounds(__nativeHandle, value); }
        }
    }
    public int ZIndex {
        get { return _zIndex; }
        set {
            _zIndex = value;
            if (_hasNative) { Visual_SetZIndex(__nativeHandle, value); }
        }
    }

    public Transform RenderTransform {
        get { return _renderTransform; }
        set { _renderTransform = value; SyncTransform2D(); }
    }
    public Point RenderTransformOrigin {
        get { return _renderTransformOrigin; }
        set {
            _renderTransformOrigin = value == null ? new Point(0.5f, 0.5f) : value;
            SyncTransform2D();
        }
    }

    public float Rotation3DX { get { return _rot3DX; } set { _rot3DX = value; SyncTransform3D(); } }
    public float Rotation3DY { get { return _rot3DY; } set { _rot3DY = value; SyncTransform3D(); } }
    public float Rotation3DZ { get { return _rot3DZ; } set { _rot3DZ = value; SyncTransform3D(); } }
    public float Translation3DX { get { return _trans3DX; } set { _trans3DX = value; SyncTransform3D(); } }
    public float Translation3DY { get { return _trans3DY; } set { _trans3DY = value; SyncTransform3D(); } }
    public float Translation3DZ { get { return _trans3DZ; } set { _trans3DZ = value; SyncTransform3D(); } }
    public float CameraDistance { get { return _camDist; } set { _camDist = value; SyncTransform3D(); } }

    public Effect Effect {
        get { return _effect; }
        set { _effect = value; SyncEffect(); }
    }
    public BlendingMode BlendMode {
        get { return _blendMode; }
        set { _blendMode = value; }
    }

    public Brush Background {
        get { return _background; }
        set { _background = value; OnBoxAppearanceChanged(); }
    }
    public Brush BorderBrush {
        get { return _borderBrush; }
        set { _borderBrush = value; OnBoxAppearanceChanged(); }
    }
    public Thickness BorderThickness {
        get { return _borderThickness; }
        set { _borderThickness = value == null ? new Thickness(0.0f) : value; OnBoxAppearanceChanged(); }
    }
    public CornerRadius CornerRadius {
        get { return _cornerRadius; }
        set { _cornerRadius = value == null ? new CornerRadius(0.0f) : value; OnBoxAppearanceChanged(); }
    }

    // ---- Layout Query Helpers ----
    public float ActualWidth { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 2) : 0.0f; } }
    public float ActualHeight { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 3) : 0.0f; } }
    public float DesiredWidth { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 4) : 0.0f; } }
    public float DesiredHeight { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 5) : 0.0f; } }
    public float LayoutX { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 0) : 0.0f; } }
    public float LayoutY { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 1) : 0.0f; } }
    public float ScrollExtentWidth { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 6) : 0.0f; } }
    public float ScrollExtentHeight { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 7) : 0.0f; } }

    protected void SyncSize() {
        if (_hasNative) { Visual_SetSize(__nativeHandle, _width, _height); }
    }
    protected void SyncMinMaxSize() {
        if (_hasNative) { Visual_SetMinMaxSize(__nativeHandle, _minWidth, _minHeight, _maxWidth, _maxHeight); }
    }
    protected void SyncMargin() {
        if (_hasNative) { Visual_SetMargin(__nativeHandle, _margin.Left, _margin.Top, _margin.Right, _margin.Bottom); }
    }
    protected void SyncPadding() {
        if (_hasNative) { Visual_SetPadding(__nativeHandle, _padding.Left, _padding.Top, _padding.Right, _padding.Bottom); }
    }
    protected void SyncAlignment() {
        if (_hasNative) {
            Visual_SetAlignment(__nativeHandle, EnumConvert.ToInt(_hAlign), EnumConvert.ToInt(_vAlign));
        }
    }
    protected void SyncInteractiveFlags() {
        if (_hasNative) {
            Visual_SetInteractiveFlags(__nativeHandle, _isEnabled, _isHitTestVisible, _focusable);
        }
    }
    public void SyncTransform2D() {
        if (!_hasNative) { return; }
        var td = new TransformData();
        if (_renderTransform != null) { _renderTransform.Accumulate(td); }
        Visual_SetTransform2D(
            __nativeHandle,
            td.TX, td.TY, td.SX, td.SY, td.RotDeg, td.SkewX, td.SkewY,
            _renderTransformOrigin.X, _renderTransformOrigin.Y);
    }
    public void SyncTransform3D() {
        if (!_hasNative) { return; }
        Visual_SetTransform3D(__nativeHandle, _rot3DX, _rot3DY, _rot3DZ, _trans3DX, _trans3DY, _trans3DZ, _camDist);
    }
    public void SyncEffect() {
        if (!_hasNative) { return; }
        if (_effect == null) {
            Visual_SetEffect(__nativeHandle, 0, 0.0f, 0.0f, 0.0f, 0u, 1.0f);
            return;
        }
        Visual_SetEffect(
            __nativeHandle,
            EnumConvert.ToInt(_effect.Kind),
            _effect.Radius,
            _effect.Depth,
            _effect.Direction,
            _effect.ColorRgba,
            _effect.Opacity);
    }

    protected virtual void OnBoxAppearanceChanged() {
        SyncBoxStyleToNative();
    }

    protected void SyncBoxStyleToNative() {
        if (!_hasNative) { return; }
        uint bgCol = _background != null ? _background.ResolveRgba() : 0u;
        uint bdCol = _borderBrush != null ? _borderBrush.ResolveRgba() : 0u;
        int bgGrad = _background != null ? _background.ResolveGradId() : -1;
        int bdGrad = _borderBrush != null ? _borderBrush.ResolveGradId() : -1;
        Visual_SetBoxStyle(
            __nativeHandle,
            bgCol, bdCol,
            _borderThickness.Left, _borderThickness.Top, _borderThickness.Right, _borderThickness.Bottom,
            _cornerRadius.TopLeft, _cornerRadius.TopRight, _cornerRadius.BottomRight, _cornerRadius.BottomLeft);
        Visual_SetBoxGradients(__nativeHandle, bgGrad, bdGrad);
    }

    // ---- Attached Layout Properties ----
    public static void SetGridRow(UIElement el, int row) {
        if (el == null) { return; }
        el._gridRow = row < 0 ? 0 : row;
        el.SyncGridCell();
    }
    public static int GetGridRow(UIElement el) { return el == null ? 0 : el._gridRow; }
    public static void SetGridColumn(UIElement el, int col) {
        if (el == null) { return; }
        el._gridCol = col < 0 ? 0 : col;
        el.SyncGridCell();
    }
    public static int GetGridColumn(UIElement el) { return el == null ? 0 : el._gridCol; }
    public static void SetGridRowSpan(UIElement el, int span) {
        if (el == null) { return; }
        el._gridRowSpan = span < 1 ? 1 : span;
        el.SyncGridCell();
    }
    public static int GetGridRowSpan(UIElement el) { return el == null ? 1 : el._gridRowSpan; }
    public static void SetGridColumnSpan(UIElement el, int span) {
        if (el == null) { return; }
        el._gridColSpan = span < 1 ? 1 : span;
        el.SyncGridCell();
    }
    public static int GetGridColumnSpan(UIElement el) { return el == null ? 1 : el._gridColSpan; }
    private void SyncGridCell() {
        if (_hasNative) {
            Visual_SetGridCell(__nativeHandle, _gridRow, _gridCol, _gridRowSpan, _gridColSpan);
        }
    }

    public static void SetDock(UIElement el, Dock dock) {
        if (el == null) { return; }
        el._dock = dock;
        if (el._hasNative) { Visual_SetDock(el.__nativeHandle, EnumConvert.ToInt(dock)); }
    }
    public static Dock GetDock(UIElement el) { return el == null ? Dock.Left : el._dock; }

    public static void SetCanvasLeft(UIElement el, float v) { if (el != null) { el._canvasLeft = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasLeft(UIElement el) { return el == null ? 0.0f : el._canvasLeft; }
    public static void SetCanvasTop(UIElement el, float v) { if (el != null) { el._canvasTop = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasTop(UIElement el) { return el == null ? 0.0f : el._canvasTop; }
    public static void SetCanvasRight(UIElement el, float v) { if (el != null) { el._canvasRight = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasRight(UIElement el) { return el == null ? 0.0f : el._canvasRight; }
    public static void SetCanvasBottom(UIElement el, float v) { if (el != null) { el._canvasBottom = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasBottom(UIElement el) { return el == null ? 0.0f : el._canvasBottom; }
    private void SyncCanvasOffsets() {
        if (_hasNative) {
            Visual_SetCanvasOffsets(__nativeHandle, _canvasLeft, _canvasTop, _canvasRight, _canvasBottom);
        }
    }
    public static void SetZIndex(UIElement el, int z) { if (el != null) { el.ZIndex = z; } }
    public static int GetZIndex(UIElement el) { return el == null ? 0 : el.ZIndex; }

    // ---- Visual Tree Management ----
    public int ChildCount() { return _children.Length; }
    public UIElement ChildAt(int index) { return _children[index]; }
    public UIElement[] Children() { return _children; }

    public virtual void AddChild(UIElement child) {
        if (child == null) { return; }
        if (child._parent != null) { child._parent.RemoveChild(child); }
        child._parent = this;
        _children.Push(child);
        if (_hasNative && child._hasNative) {
            Visual_AddChild(__nativeHandle, child.__nativeHandle);
        }
        OnChildAttached(child);
    }

    public virtual void InsertChild(int index, UIElement child) {
        if (child == null) { return; }
        if (child._parent != null) { child._parent.RemoveChild(child); }
        int idx = index < 0 ? 0 : (index > _children.Length ? _children.Length : index);
        child._parent = this;
        _children.Insert(idx, child);
        if (_hasNative && child._hasNative) {
            Visual_InsertChild(__nativeHandle, idx, child.__nativeHandle);
        }
        OnChildAttached(child);
    }

    protected void OnChildAttached(UIElement child) {
        child.ReceiveInheritedDataContext(EffectiveDataContextBox());
        child.ApplyStyleInternal();
        child.RefreshDynamicResourcesRecursive();
        child.EnsureLoadedSubtree();
    }

    public virtual void RemoveChild(UIElement child) {
        if (child == null) { return; }
        int idx = -1;
        for (int i = 0; i < _children.Length; i += 1) {
            if (_children[i] == child) { idx = i; break; }
        }
        if (idx < 0) { return; }
        _children.RemoveAt(idx);
        child._parent = null;
        if (_hasNative && child._hasNative) {
            Visual_RemoveChild(__nativeHandle, child.__nativeHandle);
        }
        child.NotifyUnloadedSubtree();
    }

    public virtual void ClearChildren() {
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i]._parent = null;
            _children[i].NotifyUnloadedSubtree();
        }
        _children.Clear();
        if (_hasNative) {
            Visual_ClearChildren(__nativeHandle);
        }
    }

    public void EnsureLoadedSubtree() {
        if (!_loadedFired) {
            _loadedFired = true;
            ApplyStyleInternal();
            OnLoaded();
            Loaded(this, new RoutedEventArgs("Loaded", this));
        }
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].EnsureLoadedSubtree();
        }
    }

    protected void NotifyUnloadedSubtree() {
        if (_loadedFired) {
            _loadedFired = false;
            Unloaded(this, new RoutedEventArgs("Unloaded", this));
        }
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].NotifyUnloadedSubtree();
        }
    }

    public UIElement FindName(string name) {
        if (name == null || name.Length == 0) { return null; }
        if (_name == name) { return this; }
        for (int i = 0; i < _children.Length; i += 1) {
            UIElement found = _children[i].FindName(name);
            if (found != null) { return found; }
        }
        return null;
    }

    public UIElement RootElement() {
        UIElement cur = this;
        while (cur._parent != null) { cur = cur._parent; }
        return cur;
    }

    public bool IsAncestorOf(UIElement candidate) {
        UIElement cur = candidate;
        while (cur != null) {
            if (cur == this) { return true; }
            cur = cur._parent;
        }
        return false;
    }

    public DependencyObject FindResourceBox(string key) {
        if (key == null || key.Length == 0) { return null; }
        UIElement cur = this;
        while (cur != null) {
            if (cur._resources != null && cur._resources.Contains(key)) {
                return cur._resources.GetBox(key);
            }
            cur = cur._parent;
        }
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        if (appRes != null && appRes.Contains(key)) {
            return appRes.GetBox(key);
        }
        return null;
    }

    public object FindResource(string key) {
        return DependencyObject.Unbox(FindResourceBox(key));
    }

    public object TryFindResource(string key) {
        return FindResource(key);
    }

    // ---- Focus, Capture, Storyboards ----
    public bool Focus() {
        return InputDispatcher.SetFocus(this);
    }
    public void CaptureMouse() {
        InputDispatcher.CapturePointer(this);
    }
    public void ReleaseMouseCapture() {
        InputDispatcher.ReleasePointerCapture(this);
    }
    public void UpdateLayout() {
        if (_hasNative) { Host_RequestWake(); }
    }

    public void BeginStoryboard(string key) {
        DependencyObject box = FindResourceBox(key);
        if (box is Storyboard) { (box as Storyboard).Begin(this); }
    }
    public void StopStoryboard(string key) {
        DependencyObject box = FindResourceBox(key);
        if (box is Storyboard) { (box as Storyboard).Stop(this); }
    }
    public void BeginStoryboard(Storyboard sb) {
        if (sb != null) { sb.Begin(this); }
    }
    public void StopStoryboard(Storyboard sb) {
        if (sb != null) { sb.Stop(this); }
    }

    // ---- Uniform Property Reflection Bridge (Style, Triggers, BindingEngine, VSM, Behaviors) ----
    public virtual bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == null) { return false; }
        if (propName == "Width") { Width = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true; }
        if (propName == "Height") { Height = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true; }
        if (propName == "MinWidth") { MinWidth = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MinHeight") { MinHeight = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MaxWidth") { MaxWidth = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true; }
        if (propName == "MaxHeight") { MaxHeight = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true; }
        if (propName == "Opacity") { Opacity = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "ZIndex") { ZIndex = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "IsEnabled") { IsEnabled = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "IsHitTestVisible") { IsHitTestVisible = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "Focusable") { Focusable = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "ClipToBounds") { ClipToBounds = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Visibility") {
            if (value is BoxedInt) { Visibility = EnumConvert.ToVisibility((value as BoxedInt).Value); return true; }
            string vs = DependencyObject.UnboxString(value, "Visible").Trim();
            if (vs == "Hidden") { Visibility = Visibility.Hidden; return true; }
            if (vs == "Collapsed") { Visibility = Visibility.Collapsed; return true; }
            Visibility = Visibility.Visible;
            return true;
        }
        if (propName == "HorizontalAlignment") {
            if (value is BoxedInt) { HorizontalAlignment = EnumConvert.ToHorizontalAlignment((value as BoxedInt).Value); return true; }
            string ha = DependencyObject.UnboxString(value, "Stretch").Trim();
            if (ha == "Left") { HorizontalAlignment = HorizontalAlignment.Left; return true; }
            if (ha == "Center") { HorizontalAlignment = HorizontalAlignment.Center; return true; }
            if (ha == "Right") { HorizontalAlignment = HorizontalAlignment.Right; return true; }
            HorizontalAlignment = HorizontalAlignment.Stretch;
            return true;
        }
        if (propName == "VerticalAlignment") {
            if (value is BoxedInt) { VerticalAlignment = EnumConvert.ToVerticalAlignment((value as BoxedInt).Value); return true; }
            string va = DependencyObject.UnboxString(value, "Stretch").Trim();
            if (va == "Top") { VerticalAlignment = VerticalAlignment.Top; return true; }
            if (va == "Center") { VerticalAlignment = VerticalAlignment.Center; return true; }
            if (va == "Bottom") { VerticalAlignment = VerticalAlignment.Bottom; return true; }
            VerticalAlignment = VerticalAlignment.Stretch;
            return true;
        }
        if (propName == "Margin") {
            if (value is Thickness) { Margin = value as Thickness; return true; }
            Margin = Thickness.Parse(DependencyObject.UnboxString(value, "0"));
            return true;
        }
        if (propName == "Padding") {
            if (value is Thickness) { Padding = value as Thickness; return true; }
            Padding = Thickness.Parse(DependencyObject.UnboxString(value, "0"));
            return true;
        }
        if (propName == "BorderThickness") {
            if (value is Thickness) { BorderThickness = value as Thickness; return true; }
            BorderThickness = Thickness.Parse(DependencyObject.UnboxString(value, "0"));
            return true;
        }
        if (propName == "CornerRadius") {
            if (value is CornerRadius) { CornerRadius = value as CornerRadius; return true; }
            CornerRadius = CornerRadius.Parse(DependencyObject.UnboxString(value, "0"));
            return true;
        }
        if (propName == "Background") {
            Background = CoerceBrush(value);
            return true;
        }
        if (propName == "BorderBrush") {
            BorderBrush = CoerceBrush(value);
            return true;
        }
        if (propName == "RenderTransform") {
            if (value is Transform) { RenderTransform = value as Transform; return true; }
            return false;
        }
        if (propName == "Effect") {
            if (value is Effect) { Effect = value as Effect; return true; }
            return false;
        }
        if (propName == "ToolTip") {
            ToolTipBox = value;
            return true;
        }
        if (propName == "Name") {
            Name = DependencyObject.UnboxString(value, "");
            return true;
        }
        if (propName == "DataContext") {
            DataContextBox = value;
            return true;
        }
        if (propName == "Tag") {
            _tag = value;
            return true;
        }
        if (propName == "Style" && value is Style) {
            Style = value as Style;
            return true;
        }
        if (propName == "Grid.Row") { SetGridRow(this, DependencyObject.UnboxInt(value, 0)); return true; }
        if (propName == "Grid.Column") { SetGridColumn(this, DependencyObject.UnboxInt(value, 0)); return true; }
        if (propName == "Grid.RowSpan") { SetGridRowSpan(this, DependencyObject.UnboxInt(value, 1)); return true; }
        if (propName == "Grid.ColumnSpan") { SetGridColumnSpan(this, DependencyObject.UnboxInt(value, 1)); return true; }
        if (propName == "Canvas.Left") { SetCanvasLeft(this, DependencyObject.UnboxFloat(value, 0.0f)); return true; }
        if (propName == "Canvas.Top") { SetCanvasTop(this, DependencyObject.UnboxFloat(value, 0.0f)); return true; }
        if (propName == "Canvas.Right") { SetCanvasRight(this, DependencyObject.UnboxFloat(value, 0.0f)); return true; }
        if (propName == "Canvas.Bottom") { SetCanvasBottom(this, DependencyObject.UnboxFloat(value, 0.0f)); return true; }
        return false;
    }

    public virtual DependencyObject GetPropertyValue(string propName) {
        if (propName == "Width") { return DependencyObject.BoxFloat(_width); }
        if (propName == "Height") { return DependencyObject.BoxFloat(_height); }
        if (propName == "ActualWidth") { return DependencyObject.BoxFloat(ActualWidth); }
        if (propName == "ActualHeight") { return DependencyObject.BoxFloat(ActualHeight); }
        if (propName == "Opacity") { return DependencyObject.BoxFloat(_opacity); }
        if (propName == "IsEnabled") { return DependencyObject.BoxBool(_isEnabled); }
        if (propName == "IsMouseOver") { return DependencyObject.BoxBool(_isMouseOver); }
        if (propName == "IsPressed") { return DependencyObject.BoxBool(_isPressed); }
        if (propName == "IsFocused") { return DependencyObject.BoxBool(_isFocused); }
        if (propName == "Visibility") { return DependencyObject.BoxInt(EnumConvert.ToInt(_visibility)); }
        if (propName == "Background") { return _background; }
        if (propName == "BorderBrush") { return _borderBrush; }
        if (propName == "Name") { return DependencyObject.BoxString(_name); }
        if (propName == "DataContext") { return EffectiveDataContextBox(); }
        if (propName == "Tag") { return _tag; }
        return null;
    }

    public static Brush CoerceBrush(DependencyObject value) {
        if (value == null) { return null; }
        if (value is Brush) { return value as Brush; }
        string s = DependencyObject.UnboxString(value, "");
        if (s.Length > 0) { return new SolidColorBrush(s); }
        return null;
    }

    public void SetProperty(string propName, string value) {
        SetPropertyValue(propName, DependencyObject.BoxString(value));
    }
    public void SetProperty(string propName, float value) {
        SetPropertyValue(propName, DependencyObject.BoxFloat(value));
    }
    public void SetProperty(string propName, int value) {
        SetPropertyValue(propName, DependencyObject.BoxInt(value));
    }
    public void SetProperty(string propName, bool value) {
        SetPropertyValue(propName, DependencyObject.BoxBool(value));
    }
    public void SetProperty(string propName, object value) {
        SetPropertyValue(propName, DependencyObject.BoxObject(value));
    }

    // ---- Internal State Updates & Virtual Hooks ----
    internal void SetFocusedState(bool focused) {
        if (_isFocused == focused) { return; }
        _isFocused = focused;
        if (focused) {
            OnGotFocus();
            GotFocus(this, new RoutedEventArgs("GotFocus", this));
        } else {
            OnLostFocus();
            LostFocus(this, new RoutedEventArgs("LostFocus", this));
        }
        UpdateTriggersAndStates();
    }

    internal void SetMouseOverState(bool over, float x, float y, int mods) {
        if (_isMouseOver == over) { return; }
        _isMouseOver = over;
        var args = new MouseEventArgs(over ? "MouseEnter" : "MouseLeave", this, x, y, mods);
        if (over) {
            OnMouseEnter(args);
            MouseEnter(this, args);
        } else {
            OnMouseLeave(args);
            MouseLeave(this, args);
        }
        UpdateTriggersAndStates();
    }

    internal void SetPressedState(bool pressed) {
        if (_isPressed == pressed) { return; }
        _isPressed = pressed;
        UpdateTriggersAndStates();
    }

    protected virtual void UpdateTriggersAndStates() {
        if (_style != null) { _style.EvaluateTriggers(this); }
        OnVisualStatePropertyChanged();
    }

    protected virtual void OnVisualStatePropertyChanged() {}
    protected virtual void OnLoaded() {}
    protected virtual void OnEnabledChanged() {}
    protected virtual void OnDataContextChanged(object oldVal, object newVal) {}
    protected virtual void OnStyleApplied(Style style) {}

    public virtual void OnMouseEnter(MouseEventArgs e) {}
    public virtual void OnMouseLeave(MouseEventArgs e) {}
    public virtual void OnMouseMove(MouseEventArgs e) {}
    public virtual void OnMouseLeftButtonDown(MouseButtonEventArgs e) {}
    public virtual void OnMouseLeftButtonUp(MouseButtonEventArgs e) {}
    public virtual void OnMouseRightButtonDown(MouseButtonEventArgs e) {}
    public virtual void OnMouseRightButtonUp(MouseButtonEventArgs e) {}
    public virtual void OnPreviewMouseLeftButtonDown(MouseButtonEventArgs e) {}
    public virtual void OnMouseWheel(MouseWheelEventArgs e) {}
    public virtual void OnKeyDown(KeyEventArgs e) {}
    public virtual void OnKeyUp(KeyEventArgs e) {}
    public virtual void OnPreviewKeyDown(KeyEventArgs e) {}
    public virtual void OnTextInput(TextCompositionEventArgs e) {}
    public virtual void OnGotFocus() {}
    public virtual void OnLostFocus() {}
    public virtual void OnCustomMeasure(float availW, float availH) {}
    public virtual void OnCustomArrange(float availW, float availH) {}
    public virtual void OnRenderVisual() {}

    internal void RaiseMouseMove(MouseEventArgs e) { OnMouseMove(e); MouseMove(this, e); }
    internal void RaisePreviewLeftDown(MouseButtonEventArgs e) { OnPreviewMouseLeftButtonDown(e); PreviewMouseLeftButtonDown(this, e); }
    internal void RaiseLeftDown(MouseButtonEventArgs e) { OnMouseLeftButtonDown(e); MouseLeftButtonDown(this, e); }
    internal void RaiseLeftUp(MouseButtonEventArgs e) { OnMouseLeftButtonUp(e); MouseLeftButtonUp(this, e); }
    internal void RaiseRightDown(MouseButtonEventArgs e) { OnMouseRightButtonDown(e); MouseRightButtonDown(this, e); }
    internal void RaiseRightUp(MouseButtonEventArgs e) { OnMouseRightButtonUp(e); MouseRightButtonUp(this, e); }
    internal void RaiseWheel(MouseWheelEventArgs e) { OnMouseWheel(e); MouseWheel(this, e); }
    internal void RaisePreviewKeyDown(KeyEventArgs e) { OnPreviewKeyDown(e); PreviewKeyDown(this, e); }
    internal void RaiseKeyDown(KeyEventArgs e) { OnKeyDown(e); KeyDown(this, e); }
    internal void RaiseKeyUp(KeyEventArgs e) { OnKeyUp(e); KeyUp(this, e); }
    internal void RaiseTextInput(TextCompositionEventArgs e) { OnTextInput(e); TextInput(this, e); }
}

public class FrameworkElement : UIElement {
    public FrameworkElement() : base(VisualKind.Box) {}
    protected FrameworkElement(VisualKind kind) : base(kind) {}
    public override string GetTypeName() { return "FrameworkElement"; }
    public virtual bool ApplyTemplate() { OnApplyTemplate(); return true; }
    public virtual void OnApplyTemplate() {}
}

// ============================================================================
// 7. InputDispatcher & FocusManager (Native -> Script Event Pipeline)
// ============================================================================

public static class FocusManager {
    public static void CollectFocusable(UIElement root, UIElement[] output) {
        if (root == null) { return; }
        if (root.Visibility != Visibility.Visible || !root.IsEnabled) { return; }
        if (root.Focusable) { output.Push(root); }
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
    private static PopupDismissHook[] _popupHooks;

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

    public static bool SetFocus(UIElement el) {
        if (el == _focused) { return true; }
        if (el != null && (!el.Focusable || !el.IsEnabled || el.Visibility != Visibility.Visible)) {
            return false;
        }
        UIElement prev = _focused;
        _focused = el;
        if (prev != null) { prev.SetFocusedState(false); }
        if (_focused != null) { _focused.SetFocusedState(true); }
        return true;
    }

    private static UIElement[] AncestorChain(UIElement leaf) {
        var chain = new UIElement[0];
        UIElement cur = leaf;
        while (cur != null) {
            chain.Push(cur);
            cur = cur.Parent;
        }
        return chain;
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
            UIElement[] chain = AncestorChain(newTarget);
            for (int i = chain.Length - 1; i >= 0; i -= 1) {
                if (prev == null || !chain[i].IsAncestorOf(prev)) {
                    chain[i].SetMouseOverState(true, x, y, mods);
                }
            }
        }
    }

    public static bool __AeroDispatchPointer(
        handle node, int kind, float x, float y, int button, int modifiers, float dx, float dy) {
        UIElement rawTarget = Visual_GetOwner(node) as UIElement;
        UIElement target = _captured != null ? _captured : rawTarget;
        if (target == null) {
            if (kind == 2) { NotifyPopupHooks(null, false); }
            if (kind == 16 || kind == 1 || kind == 0) { UpdateHoverChain(null, x, y, modifiers); }
            return false;
        }

        if (kind == 8) {
            UpdateHoverChain(target, x, y, modifiers);
            return true;
        }
        if (kind == 16) {
            if (_hovered != null && (rawTarget == _hovered || _hovered.IsAncestorOf(rawTarget))) {
                UpdateHoverChain(null, x, y, modifiers);
            }
            return true;
        }

        if (kind == 1 || kind == 0) {
            UpdateHoverChain(rawTarget != null ? rawTarget : target, x, y, modifiers);
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
            if (btn == PointerButton.Left && _lastClickTarget == target &&
                Math.Absf(x - _lastClickX) < 5.0f && Math.Absf(y - _lastClickY) < 5.0f && _clickSeq == 1) {
                clickCount = 2;
                _clickSeq = 0;
            } else {
                _lastClickTarget = target;
                _lastClickX = x;
                _lastClickY = y;
                _clickSeq = 1;
            }

            if (btn == PointerButton.Left) {
                UIElement f = target;
                while (f != null) {
                    if (f.Focusable && f.IsEnabled) { SetFocus(f); break; }
                    f = f.Parent;
                }
            }

            var downArgs = new MouseButtonEventArgs(
                btn == PointerButton.Right ? "MouseRightButtonDown" : "MouseLeftButtonDown",
                target, x, y, modifiers, btn, true, clickCount);

            if (btn == PointerButton.Left) {
                UIElement[] chain = AncestorChain(target);
                for (int i = chain.Length - 1; i >= 0 && !downArgs.Handled; i -= 1) {
                    chain[i].RaisePreviewLeftDown(downArgs);
                }
            }

            UIElement cur = target;
            while (cur != null && !downArgs.Handled) {
                if (cur.IsEnabled) {
                    if (btn == PointerButton.Left) {
                        cur.SetPressedState(true);
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

        if (isDown && !args.Handled && kc == KeyCode.Tab) {
            bool shift = KeyModifiers.Has(modifiers, KeyModifiers.Shift());
            FocusManager.MoveFocus(target, shift ? FocusDirection.Previous : FocusDirection.Next);
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
        if (focused) { SetFocus(target); }
        else if (_focused == target) { SetFocus(null); }
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
