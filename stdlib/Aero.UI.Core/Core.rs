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
    OpacityMask = 9,
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
    Rectangle = 4,
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
    ViewportX = 8,
    ViewportY = 9,
    ViewportWidth = 10,
    ViewportHeight = 11,
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
    EffectOpacity = 27,
    BrushOpacity = 28,
    FillColor = 29,
    StrokeColor = 30,
    OpacityMaskStop0Offset = 31,
    OpacityMaskStop1Offset = 32,
    OpacityMaskStop2Offset = 33,
    OpacityMaskStop3Offset = 34,
    FillBrushOpacity = 35,
    StrokeBrushOpacity = 36,
    FillStop0Color = 37,
    FillStop1Color = 38,
    FillStop2Color = 39,
    FillStop3Color = 40,
    FillStop0Offset = 41,
    FillStop1Offset = 42,
    FillStop2Offset = 43,
    FillStop3Offset = 44,
    StrokeStop0Color = 45,
    StrokeStop1Color = 46,
    StrokeStop2Color = 47,
    StrokeStop3Color = 48,
    StrokeStop0Offset = 49,
    StrokeStop1Offset = 50,
    StrokeStop2Offset = 51,
    StrokeStop3Offset = 52,
    FgBrushOpacity = 53,
    ManagedCustom = 54,
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
    public static int Parse(string text) {
        if (text == null || text.Length == 0) { return 0; }
        string s = text.ToLower().Trim();
        int m = 0;
        if (s.Contains("ctrl") || s.Contains("control")) { m = m + 2; }
        if (s.Contains("shift")) { m = m + 1; }
        if (s.Contains("alt")) { m = m + 4; }
        return m;
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
            case UIEffectKind.OpacityMask: return 9;
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
            case VectorShapeKind.Rectangle: return 4;
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
            case VisualLayoutRectQuery.ViewportX: return 8;
            case VisualLayoutRectQuery.ViewportY: return 9;
            case VisualLayoutRectQuery.ViewportWidth: return 10;
            case VisualLayoutRectQuery.ViewportHeight: return 11;
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
            case VisualAnimProp.EffectOpacity: return 27;
            case VisualAnimProp.BrushOpacity: return 28;
            case VisualAnimProp.FillColor: return 29;
            case VisualAnimProp.StrokeColor: return 30;
            case VisualAnimProp.OpacityMaskStop0Offset: return 31;
            case VisualAnimProp.OpacityMaskStop1Offset: return 32;
            case VisualAnimProp.OpacityMaskStop2Offset: return 33;
            case VisualAnimProp.OpacityMaskStop3Offset: return 34;
            case VisualAnimProp.FillBrushOpacity: return 35;
            case VisualAnimProp.StrokeBrushOpacity: return 36;
            case VisualAnimProp.FillStop0Color: return 37;
            case VisualAnimProp.FillStop1Color: return 38;
            case VisualAnimProp.FillStop2Color: return 39;
            case VisualAnimProp.FillStop3Color: return 40;
            case VisualAnimProp.FillStop0Offset: return 41;
            case VisualAnimProp.FillStop1Offset: return 42;
            case VisualAnimProp.FillStop2Offset: return 43;
            case VisualAnimProp.FillStop3Offset: return 44;
            case VisualAnimProp.StrokeStop0Color: return 45;
            case VisualAnimProp.StrokeStop1Color: return 46;
            case VisualAnimProp.StrokeStop2Color: return 47;
            case VisualAnimProp.StrokeStop3Color: return 48;
            case VisualAnimProp.StrokeStop0Offset: return 49;
            case VisualAnimProp.StrokeStop1Offset: return 50;
            case VisualAnimProp.StrokeStop2Offset: return 51;
            case VisualAnimProp.StrokeStop3Offset: return 52;
            case VisualAnimProp.FgBrushOpacity: return 53;
            case VisualAnimProp.ManagedCustom: return 54;
        }
        return 0;
    }
    private static int ParseGradientStopPropId(string n) {
        if (n == "OpacityMask.Stop0.Offset" || (n.Contains("OpacityMask") && (n.Contains("[0]") || n.Contains("Stop0")))) { return 31; }
        if (n == "OpacityMask.Stop1.Offset" || (n.Contains("OpacityMask") && (n.Contains("[1]") || n.Contains("Stop1")))) { return 32; }
        if (n == "OpacityMask.Stop2.Offset" || (n.Contains("OpacityMask") && (n.Contains("[2]") || n.Contains("Stop2")))) { return 33; }
        if (n == "OpacityMask.Stop3.Offset" || (n.Contains("OpacityMask") && (n.Contains("[3]") || n.Contains("Stop3")))) { return 34; }
        if (n == "Fill.Stop0Color" || n == "Background.Stop0Color") { return 37; }
        if (n == "Fill.Stop1Color" || n == "Background.Stop1Color") { return 38; }
        if (n == "Fill.Stop2Color" || n == "Background.Stop2Color") { return 39; }
        if (n == "Fill.Stop3Color" || n == "Background.Stop3Color") { return 40; }
        if (n == "Fill.Stop0Offset" || n == "Background.Stop0Offset") { return 41; }
        if (n == "Fill.Stop1Offset" || n == "Background.Stop1Offset") { return 42; }
        if (n == "Fill.Stop2Offset" || n == "Background.Stop2Offset") { return 43; }
        if (n == "Fill.Stop3Offset" || n == "Background.Stop3Offset") { return 44; }
        if (n == "Stroke.Stop0Color") { return 45; }
        if (n == "Stroke.Stop1Color") { return 46; }
        if (n == "Stroke.Stop2Color") { return 47; }
        if (n == "Stroke.Stop3Color") { return 48; }
        if (n == "Stroke.Stop0Offset") { return 49; }
        if (n == "Stroke.Stop1Offset") { return 50; }
        if (n == "Stroke.Stop2Offset") { return 51; }
        if (n == "Stroke.Stop3Offset") { return 52; }
        return -1;
    }
    public static int ParseAnimPropOrInvalid(string name) {
        if (name == null) { return -1; }
        string n = name.Trim();
        if (n.Length == 0) { return -1; }
        int gradProp = ParseGradientStopPropId(n);
        if (gradProp >= 0) { return gradProp; }
        if (n.Contains("OpacityMask") || n.Contains("GradientStop")) { return -1; }
        if (n == "Opacity") { return 0; }
        if (n == "Width" || n == "MinWidth") { return 1; }
        if (n == "Height" || n == "MinHeight") { return 2; }
        if (n == "TranslateX" || n == "RenderTransform.X" || n == "X" || n == "Canvas.Left") { return 3; }
        if (n == "TranslateY" || n == "RenderTransform.Y" || n == "Y" || n == "Canvas.Top") { return 4; }
        if (n == "ScaleX" || n == "RenderTransform.ScaleX") { return 5; }
        if (n == "ScaleY" || n == "RenderTransform.ScaleY") { return 6; }
        if (n == "RotateDeg" || n == "Rotation" || n == "Angle" || n == "RenderTransform.Angle") { return 7; }
        if (n == "SkewX" || n == "RenderTransform.AngleX" || n == "AngleX") { return 8; }
        if (n == "SkewY" || n == "RenderTransform.AngleY" || n == "AngleY") { return 9; }
        if (n == "LayoutTransform.ScaleX") { return 24; }
        if (n == "LayoutTransform.ScaleY") { return 25; }
        if (n == "LayoutTransform.Angle" || n == "LayoutTransform.Rotation") { return 26; }
        if (n == "Rot3DX" || n == "Rotation3DX" || n == "RotationX") { return 10; }
        if (n == "Rot3DY" || n == "Rotation3DY" || n == "RotationY") { return 11; }
        if (n == "Rot3DZ" || n == "Rotation3DZ" || n == "RotationZ") { return 12; }
        if (n == "Trans3DX" || n == "Translation3DX" || n == "TranslateZ") { return 13; }
        if (n == "Trans3DY" || n == "Translation3DY") { return 14; }
        if (n == "Trans3DZ" || n == "Translation3DZ" || n == "GlobalOffsetZ") { return 15; }
        if (n == "Background" || n == "Background.Color") { return 16; }
        if (n == "Foreground" || n == "Foreground.Color") { return 17; }
        if (n == "BorderBrush" || n == "BorderBrush.Color") { return 18; }
        if (n == "CornerRadius") { return 19; }
        if (n == "PathTrimStart" || n == "TrimStart" || n == "Path.TrimStart" || n == "noesis:Path.TrimStart") { return 20; }
        if (n == "PathTrimEnd" || n == "TrimEnd" || n == "Path.TrimEnd" || n == "noesis:Path.TrimEnd") { return 21; }
        if (n == "EffectRadius" || n == "Effect.Radius" || n == "Effect.BlurRadius" || n == "BlurRadius" || n == "DropShadowEffect.BlurRadius") { return 22; }
        if (n == "EffectOpacity" || n == "Effect.Opacity" || n == "DropShadowEffect.Opacity") { return 27; }
        if (n == "BrushOpacity" || n == "Brush.Opacity") { return 28; }
        if (n == "Fill" || n == "Fill.Color") { return 29; }
        if (n == "Stroke" || n == "Stroke.Color") { return 30; }
        if (n == "Fill.Opacity") { return 35; }
        if (n == "Stroke.Opacity") { return 36; }
        if (n == "Foreground.Opacity" || n == "FgBrushOpacity") { return 53; }
        return -1;
    }
    public static VisualAnimProp AnimPropFromInt(int id) {
        switch (id) {
            case 0: return VisualAnimProp.Opacity;
            case 1: return VisualAnimProp.Width;
            case 2: return VisualAnimProp.Height;
            case 3: return VisualAnimProp.TranslateX;
            case 4: return VisualAnimProp.TranslateY;
            case 5: return VisualAnimProp.ScaleX;
            case 6: return VisualAnimProp.ScaleY;
            case 7: return VisualAnimProp.RotateDeg;
            case 8: return VisualAnimProp.SkewX;
            case 9: return VisualAnimProp.SkewY;
            case 10: return VisualAnimProp.Rot3DX;
            case 11: return VisualAnimProp.Rot3DY;
            case 12: return VisualAnimProp.Rot3DZ;
            case 13: return VisualAnimProp.Trans3DX;
            case 14: return VisualAnimProp.Trans3DY;
            case 15: return VisualAnimProp.Trans3DZ;
            case 16: return VisualAnimProp.BgColor;
            case 17: return VisualAnimProp.FgColor;
            case 18: return VisualAnimProp.BorderColor;
            case 19: return VisualAnimProp.CornerRadius;
            case 20: return VisualAnimProp.PathTrimStart;
            case 21: return VisualAnimProp.PathTrimEnd;
            case 22: return VisualAnimProp.EffectRadius;
            case 23: return VisualAnimProp.CustomScalar0;
            case 24: return VisualAnimProp.CustomScalar1;
            case 25: return VisualAnimProp.CustomScalar2;
            case 26: return VisualAnimProp.CustomScalar3;
            case 27: return VisualAnimProp.EffectOpacity;
            case 28: return VisualAnimProp.BrushOpacity;
            case 29: return VisualAnimProp.FillColor;
            case 30: return VisualAnimProp.StrokeColor;
            case 31: return VisualAnimProp.OpacityMaskStop0Offset;
            case 32: return VisualAnimProp.OpacityMaskStop1Offset;
            case 33: return VisualAnimProp.OpacityMaskStop2Offset;
            case 34: return VisualAnimProp.OpacityMaskStop3Offset;
            case 35: return VisualAnimProp.FillBrushOpacity;
            case 36: return VisualAnimProp.StrokeBrushOpacity;
            case 37: return VisualAnimProp.FillStop0Color;
            case 38: return VisualAnimProp.FillStop1Color;
            case 39: return VisualAnimProp.FillStop2Color;
            case 40: return VisualAnimProp.FillStop3Color;
            case 41: return VisualAnimProp.FillStop0Offset;
            case 42: return VisualAnimProp.FillStop1Offset;
            case 43: return VisualAnimProp.FillStop2Offset;
            case 44: return VisualAnimProp.FillStop3Offset;
            case 45: return VisualAnimProp.StrokeStop0Color;
            case 46: return VisualAnimProp.StrokeStop1Color;
            case 47: return VisualAnimProp.StrokeStop2Color;
            case 48: return VisualAnimProp.StrokeStop3Color;
            case 49: return VisualAnimProp.StrokeStop0Offset;
            case 50: return VisualAnimProp.StrokeStop1Offset;
            case 51: return VisualAnimProp.StrokeStop2Offset;
            case 52: return VisualAnimProp.StrokeStop3Offset;
            case 53: return VisualAnimProp.FgBrushOpacity;
            case 54: return VisualAnimProp.ManagedCustom;
        }
        return VisualAnimProp.Opacity;
    }
    public static VisualAnimProp ParseAnimProp(string name) {
        int id = ParseAnimPropOrInvalid(name);
        return id >= 0 ? AnimPropFromInt(id) : VisualAnimProp.Opacity;
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
    public static int ToInt(ExpandDirection v) {
        switch (v) {
            case ExpandDirection.Up: return 1;
            case ExpandDirection.Left: return 2;
            case ExpandDirection.Right: return 3;
        }
        return 0;
    }
    public static int ToInt(ClickMode v) {
        switch (v) {
            case ClickMode.Press: return 1;
            case ClickMode.Hover: return 2;
        }
        return 0;
    }
    public static int ToInt(SelectionMode v) {
        switch (v) {
            case SelectionMode.Multiple: return 1;
            case SelectionMode.Extended: return 2;
        }
        return 0;
    }
    public static int ToInt(TabStripPlacement v) {
        switch (v) {
            case TabStripPlacement.Bottom: return 1;
            case TabStripPlacement.Left: return 2;
            case TabStripPlacement.Right: return 3;
        }
        return 0;
    }
    public static int ToInt(ScrollBarVisibility v) {
        switch (v) {
            case ScrollBarVisibility.Auto: return 1;
            case ScrollBarVisibility.Hidden: return 2;
            case ScrollBarVisibility.Visible: return 3;
        }
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
    public static KeyCode ParseKeyCode(string text) {
        if (text == null || text.Length == 0) { return KeyCode.None; }
        string k = text.Trim().ToLower();
        if (k == "tab") { return KeyCode.Tab; }
        if (k == "enter" || k == "return") { return KeyCode.Enter; }
        if (k == "space") { return KeyCode.Space; }
        if (k == "escape" || k == "esc") { return KeyCode.Escape; }
        if (k == "back" || k == "backspace") { return KeyCode.Backspace; }
        if (k == "delete" || k == "del") { return KeyCode.Delete; }
        if (k == "left") { return KeyCode.Left; }
        if (k == "right") { return KeyCode.Right; }
        if (k == "up") { return KeyCode.Up; }
        if (k == "down") { return KeyCode.Down; }
        if (k == "home") { return KeyCode.Home; }
        if (k == "end") { return KeyCode.End; }
        if (k == "pageup" || k == "prior") { return KeyCode.PageUp; }
        if (k == "pagedown" || k == "next") { return KeyCode.PageDown; }
        if (k == "a") { return KeyCode.A; } if (k == "b") { return KeyCode.B; }
        if (k == "c") { return KeyCode.C; } if (k == "d") { return KeyCode.D; }
        if (k == "e") { return KeyCode.E; } if (k == "f") { return KeyCode.F; }
        if (k == "g") { return KeyCode.G; } if (k == "h") { return KeyCode.H; }
        if (k == "i") { return KeyCode.I; } if (k == "j") { return KeyCode.J; }
        if (k == "k") { return KeyCode.K; } if (k == "l") { return KeyCode.L; }
        if (k == "m") { return KeyCode.M; } if (k == "n") { return KeyCode.N; }
        if (k == "o") { return KeyCode.O; } if (k == "p") { return KeyCode.P; }
        if (k == "q") { return KeyCode.Q; } if (k == "r") { return KeyCode.R; }
        if (k == "s") { return KeyCode.S; } if (k == "t") { return KeyCode.T; }
        if (k == "u") { return KeyCode.U; } if (k == "v") { return KeyCode.V; }
        if (k == "w") { return KeyCode.W; } if (k == "x") { return KeyCode.X; }
        if (k == "y") { return KeyCode.Y; } if (k == "z") { return KeyCode.Z; }
        if (k == "0" || k == "d0") { return KeyCode.D0; }
        if (k == "1" || k == "d1") { return KeyCode.D1; }
        if (k == "2" || k == "d2") { return KeyCode.D2; }
        if (k == "3" || k == "d3") { return KeyCode.D3; }
        if (k == "4" || k == "d4") { return KeyCode.D4; }
        if (k == "5" || k == "d5") { return KeyCode.D5; }
        if (k == "6" || k == "d6") { return KeyCode.D6; }
        if (k == "7" || k == "d7") { return KeyCode.D7; }
        if (k == "8" || k == "d8") { return KeyCode.D8; }
        if (k == "9" || k == "d9") { return KeyCode.D9; }
        if (k == "f1") { return KeyCode.F1; } if (k == "f2") { return KeyCode.F2; }
        if (k == "f3") { return KeyCode.F3; } if (k == "f4") { return KeyCode.F4; }
        if (k == "f5") { return KeyCode.F5; } if (k == "f6") { return KeyCode.F6; }
        if (k == "f7") { return KeyCode.F7; } if (k == "f8") { return KeyCode.F8; }
        if (k == "f9") { return KeyCode.F9; } if (k == "f10") { return KeyCode.F10; }
        if (k == "f11") { return KeyCode.F11; } if (k == "f12") { return KeyCode.F12; }
        return KeyCode.None;
    }
    public static BlendingMode ParseBlendingMode(string text) {
        if (text == null) { return BlendingMode.Normal; }
        string m = text.Trim().ToLower();
        if (m == "multiply") { return BlendingMode.Multiply; }
        if (m == "screen") { return BlendingMode.Screen; }
        if (m == "additive" || m == "add") { return BlendingMode.Additive; }
        return BlendingMode.Normal;
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
        return (a % 256u) * 16777216u + (b % 256u) * 65536u + (g % 256u) * 256u + (r % 256u);
    }

    public static string FormatArgbHex(uint argb) {
        uint a = (argb / 16777216u) % 256u;
        uint b = (argb / 65536u) % 256u;
        uint g = (argb / 256u) % 256u;
        uint r = argb % 256u;
        return "#" + HexDigit(a / 16u) + HexDigit(a % 16u)
                   + HexDigit(r / 16u) + HexDigit(r % 16u)
                   + HexDigit(g / 16u) + HexDigit(g % 16u)
                   + HexDigit(b / 16u) + HexDigit(b % 16u);
    }

    public static uint ParseHexColor(string hex, uint fallback = 0u) {
        if (hex == null || hex.Length == 0) { return fallback; }
        string s = hex.Trim();
        if (s.StartsWith("#")) {
            if (IsNativeReady() && (s.Length == 4 || s.Length == 5 || s.Length == 7 || s.Length == 9)) {
                uint nativeCol = Color_ParseRgba(s);
                if (nativeCol != 4278190080u || s == "#000000" || s == "#FF000000" || s == "#ff000000" || s == "#000" || s == "#F000" || s == "#f000") {
                    return nativeCol;
                }
            }
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
        if (c.Length == 0) { return 0u; }
        if (c.StartsWith("#")) {
            return ParseHexColor(c, 0u);
        }
        if (IsNativeReady()) {
            return Color_ParseRgba(c);
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
        if (text == null || text.Length == 0) { return 0; }
        if (_nativeReady) { return Utf8_CodePointCount(text); }
        return text.Length;
    }

    public static string SliceCodePoints(string text, int startCp, int countCp) {
        if (text == null || countCp <= 0 || text.Length == 0) { return ""; }
        if (_nativeReady) { return Utf8_SliceCodePoints(text, startCp, countCp); }
        if (startCp >= text.Length) { return ""; }
        int s = startCp < 0 ? 0 : startCp;
        int avail = text.Length - s;
        int len = countCp > avail ? avail : countCp;
        return text.Substring(s, len);
    }

    public static string FromCodePoint(int cp) {
        if (cp <= 0 || cp > 1114111 || (cp >= 55296 && cp <= 57343)) { return ""; }
        if (_nativeReady) { return Utf8_FromCodePoint(cp); }
        if (cp == 10) { return "\n"; }
        if (cp == 13) { return "\r"; }
        if (cp == 9) { return "\t"; }
        if (cp == 32) { return " "; }
        if (cp == 8226) { return "•"; }
        return "";
    }

    public static int FindNextWordBoundary(string text, int fromCp) {
        if (text == null || text.Length == 0) { return 0; }
        int totalCp = Utf8CodePointCount(text);
        if (fromCp >= totalCp) { return totalCp; }
        int i = fromCp < 0 ? 0 : fromCp;
        if (totalCp == text.Length) {
            while (i < totalCp && text.Substring(i, 1) == " ") { i += 1; }
            while (i < totalCp && text.Substring(i, 1) != " ") { i += 1; }
            return i;
        }
        while (i < totalCp && SliceCodePoints(text, i, 1) == " ") { i += 1; }
        while (i < totalCp && SliceCodePoints(text, i, 1) != " ") { i += 1; }
        return i;
    }

    public static int FindPrevWordBoundary(string text, int fromCp) {
        if (text == null || text.Length == 0 || fromCp <= 0) { return 0; }
        int totalCp = Utf8CodePointCount(text);
        int i = fromCp > totalCp ? totalCp : fromCp;
        if (totalCp == text.Length) {
            while (i > 0 && text.Substring(i - 1, 1) == " ") { i -= 1; }
            while (i > 0 && text.Substring(i - 1, 1) != " ") { i -= 1; }
            return i;
        }
        while (i > 0 && SliceCodePoints(text, i - 1, 1) == " ") { i -= 1; }
        while (i > 0 && SliceCodePoints(text, i - 1, 1) != " ") { i -= 1; }
        return i;
    }

    public static string[] SplitFloatTokens(string text) {
        if (text == null) { return new string[0]; }
        string t = text.Trim();
        if (t.Length == 0) { return new string[0]; }
        if (!t.Contains(" ") && !t.Contains("\t") && !t.Contains("\r") && !t.Contains("\n")) {
            return t.Split(",");
        }
        var res = new string[0];
        int i = 0;
        int n = t.Length;
        while (i < n) {
            while (i < n) {
                string ch = t.Substring(i, 1);
                if (ch == "," || ch == " " || ch == "\t" || ch == "\r" || ch == "\n") {
                    i += 1;
                } else {
                    break;
                }
            }
            if (i >= n) { break; }
            int start = i;
            while (i < n) {
                string ch = t.Substring(i, 1);
                if (ch == "," || ch == " " || ch == "\t" || ch == "\r" || ch == "\n") {
                    break;
                }
                i += 1;
            }
            res.Push(t.Substring(start, i - start));
        }
        return res;
    }
}

// ============================================================================
// 3. DependencyObject & Type-Safe Boxing (Zero RS5151 bare-object fields)
// ============================================================================

public class DependencyObject {
    internal virtual int DepPropEpoch() { return 1; }
    public virtual object UnboxObject() { return this; }
    public virtual string ToDisplayString() { return ""; }
    public virtual string ImplicitDataTypeKey() { return ""; }
    public virtual bool BoxValueEquals(DependencyObject other) { return this == other; }

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

    private static BoxedString _emptyStringBox = null;
    private static BoxedBool _trueBox = null;
    private static BoxedBool _falseBox = null;
    private static BoxedFloat _zeroFloatBox = null;
    private static BoxedFloat _oneFloatBox = null;
    private static BoxedInt[] _smallIntBoxes = null;
    private static BoxedColor _transparentColorBox = null;
    private static BoxedColor _blackColorBox = null;
    private static BoxedColor _whiteColorBox = null;

    public static DependencyObject BoxString(string v) {
        if (v != null && v.Length == 0) {
            if (_emptyStringBox == null) { _emptyStringBox = new BoxedString(""); }
            return _emptyStringBox;
        }
        return new BoxedString(v);
    }
    public static DependencyObject BoxEnum(string name, int ordinal) { return new BoxedEnum(name, ordinal); }
    public static DependencyObject BoxFloat(float v) {
        if (v == 0.0f) {
            if (_zeroFloatBox == null) { _zeroFloatBox = new BoxedFloat(0.0f); }
            return _zeroFloatBox;
        }
        if (v == 1.0f) {
            if (_oneFloatBox == null) { _oneFloatBox = new BoxedFloat(1.0f); }
            return _oneFloatBox;
        }
        return new BoxedFloat(v);
    }
    public static DependencyObject BoxInt(int v) {
        if (v >= -64 && v <= 1024) {
            if (_smallIntBoxes == null) { _smallIntBoxes = new BoxedInt[1089]; }
            int idx = v + 64;
            BoxedInt hit = _smallIntBoxes[idx];
            if (hit == null) {
                hit = new BoxedInt(v);
                _smallIntBoxes[idx] = hit;
            }
            return hit;
        }
        return new BoxedInt(v);
    }
    public static DependencyObject BoxBool(bool v) {
        if (v) {
            if (_trueBox == null) { _trueBox = new BoxedBool(true); }
            return _trueBox;
        }
        if (_falseBox == null) { _falseBox = new BoxedBool(false); }
        return _falseBox;
    }
    public static DependencyObject BoxColor(uint rgba) {
        if (rgba == 0u) {
            if (_transparentColorBox == null) { _transparentColorBox = new BoxedColor(0u); }
            return _transparentColorBox;
        }
        if (rgba == 4278190080u) {
            if (_blackColorBox == null) { _blackColorBox = new BoxedColor(4278190080u); }
            return _blackColorBox;
        }
        if (rgba == 4294967295u) {
            if (_whiteColorBox == null) { _whiteColorBox = new BoxedColor(4294967295u); }
            return _whiteColorBox;
        }
        return new BoxedColor(rgba);
    }
    public static uint UnboxColor(DependencyObject box, uint fallback = 0u) {
        if (box == null) { return fallback; }
        if (box is BoxedColor) { return (box as BoxedColor).Value; }
        if (box is BoxedInt) { return (uint)(box as BoxedInt).Value; }
        string s = UnboxString(box, "");
        if (s.Length > 0) { return ParseUtil.ParseColor(s); }
        return fallback;
    }

    public static string UnboxString(DependencyObject box, string fallback = "") {
        if (box == null) { return fallback; }
        if (box is BoxedString) { return (box as BoxedString).Value; }
        if (box is BoxedColor) { return ParseUtil.FormatArgbHex((box as BoxedColor).Value); }
        if (box is BoxedFloat) {
            float fv = (box as BoxedFloat).Value;
            if (ParseUtil.IsFinite(fv) && Math.Absf(fv) < 100000000.0f) {
                int iv = (int)fv;
                if ((float)iv == fv) { return "" + iv; }
            }
            return "" + fv;
        }
        if (box is BoxedInt) { return "" + (box as BoxedInt).Value; }
        if (box is BoxedBool) { return (box as BoxedBool).Value ? "True" : "False"; }
        return box.ToDisplayString();
    }

    public static float UnboxFloat(DependencyObject box, float fallback = 0.0f) {
        if (box == null) { return fallback; }
        if (box is BoxedEnum) { return (float)(box as BoxedEnum).Ordinal; }
        if (box is BoxedFloat) { return (box as BoxedFloat).Value; }
        if (box is BoxedInt) { return (float)(box as BoxedInt).Value; }
        if (box is BoxedString) { return ParseUtil.ParseFloat((box as BoxedString).Value, fallback); }
        return fallback;
    }

    public static int UnboxInt(DependencyObject box, int fallback = 0) {
        if (box == null) { return fallback; }
        if (box is BoxedEnum) { return (box as BoxedEnum).Ordinal; }
        if (box is BoxedInt) { return (box as BoxedInt).Value; }
        if (box is BoxedFloat) {
            float fv = (box as BoxedFloat).Value;
            return (int)(fv >= 0.0f ? (fv + 0.5f) : (fv - 0.5f));
        }
        if (box is BoxedString) { return ParseUtil.ParseInt((box as BoxedString).Value, fallback); }
        return fallback;
    }

    public static bool UnboxBool(DependencyObject box, bool fallback = false) {
        if (box == null) { return fallback; }
        if (box is BoxedBool) { return (box as BoxedBool).Value; }
        if (box is BoxedEnum) { return (box as BoxedEnum).Ordinal != 0; }
        if (box is BoxedInt) { return (box as BoxedInt).Value != 0; }
        if (box is BoxedString) { return ParseUtil.ParseBool((box as BoxedString).Value, fallback); }
        return fallback;
    }

    internal virtual DependencyObject GetBindingPropertyValue(string propName) { return null; }

    internal static bool SameValueBox(DependencyObject a, DependencyObject b) {
        if (a == b) { return true; }
        if (a == null || b == null) { return false; }
        if (a is BoxedString && b is BoxedString) {
            return (a as BoxedString).Value == (b as BoxedString).Value;
        }
        if (a is BoxedInt && b is BoxedInt) {
            return (a as BoxedInt).Value == (b as BoxedInt).Value;
        }
        if (a is BoxedFloat && b is BoxedFloat) {
            return (a as BoxedFloat).Value == (b as BoxedFloat).Value;
        }
        if (a is BoxedBool && b is BoxedBool) {
            return (a as BoxedBool).Value == (b as BoxedBool).Value;
        }
        if (a is BoxedColor && b is BoxedColor) {
            return (a as BoxedColor).Value == (b as BoxedColor).Value;
        }
        if (a.BoxValueEquals(b)) { return true; }
        object ua = DependencyObject.Unbox(a);
        object ub = DependencyObject.Unbox(b);
        if (ua != null && ub != null && ua == ub) {
            return true;
        }
        return false;
    }
}

public class TypedBox<T> : DependencyObject {
    private T _value;
    public T Value { get { return _value; } }
    public TypedBox(T v) { _value = v; }
    public override string ToDisplayString() { return "" + _value; }
    public override bool BoxValueEquals(DependencyObject other) {
        DependencyObject self = this;
        if (self == other) { return true; }
        var o = other as TypedBox<T>;
        if (o == null) { return false; }
        var probe = new T[1];
        probe[0] = _value;
        return probe.Contains(o._value);
    }
    public override object UnboxObject() {
        if (ParseUtil.IsNativeReady()) {
            var r = Reflect_GetPropObject(this, "_value");
            if (r != null) { return r; }
        }
        return this;
    }
}

public class BoxedString : TypedBox<string> {
    public BoxedString(string v) : base(v) {}
    public override object UnboxObject() { return this; }
}

public class BoxedEnum : BoxedString {
    private int _ordinal = 0;
    public int Ordinal { get { return _ordinal; } }
    public BoxedEnum(string v, int ordinal) : base(v) { _ordinal = ordinal; }
}

public class BoxedFloat : TypedBox<float> {
    public BoxedFloat(float v) : base(v) {}
    public override object UnboxObject() { return this; }
}

public class BoxedInt : TypedBox<int> {
    public BoxedInt(int v) : base(v) {}
    public override object UnboxObject() { return this; }
}

public class BoxedBool : TypedBox<bool> {
    public BoxedBool(bool v) : base(v) {}
    public override object UnboxObject() { return this; }
}

public class BoxedColor : DependencyObject {
    private uint _value;
    public uint Value { get { return _value; } }
    public BoxedColor(uint v) { _value = v; }
    public override string ToDisplayString() { return ParseUtil.FormatArgbHex(_value); }
    public override bool BoxValueEquals(DependencyObject other) {
        DependencyObject self = this;
        if (self == other) { return true; }
        if (other is BoxedColor) { return (other as BoxedColor)._value == _value; }
        return false;
    }
    public override object UnboxObject() { return this; }
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
    private object _value;

    public NativeObjectBox(object value) {
        _value = value;
    }

    public override object UnboxObject() {
        return _value;
    }

    public override bool BoxValueEquals(DependencyObject other) {
        DependencyObject self = this;
        if (self == other) { return true; }
        if (other == null || !(other is NativeObjectBox)) { return false; }
        return _value == (other as NativeObjectBox)._value;
    }

    public void Release() {
        _value = null;
    }
}

// ============================================================================
// 4. Geometry & Layout Primitives
// ============================================================================

public class Thickness : DependencyObject {
    private static Thickness _zeroThickness = null;
    public static Thickness SharedZero() {
        if (_zeroThickness == null) { _zeroThickness = new Thickness(0.0f); }
        return _zeroThickness;
    }
    private float _left;
    private float _top;
    private float _right;
    private float _bottom;
    public float Left { get { return _left; } }
    public float Top { get { return _top; } }
    public float Right { get { return _right; } }
    public float Bottom { get { return _bottom; } }

    public Thickness() {}
    public Thickness(float uniform) {
        _left = uniform; _top = uniform; _right = uniform; _bottom = uniform;
    }
    public Thickness(float horizontal, float vertical) {
        _left = horizontal; _top = vertical; _right = horizontal; _bottom = vertical;
    }
    public Thickness(float left, float top, float right, float bottom) {
        _left = left; _top = top; _right = right; _bottom = bottom;
    }

    public Thickness Clone() { return new Thickness(_left, _top, _right, _bottom); }
    public override string ToDisplayString() {
        return "" + _left + "," + _top + "," + _right + "," + _bottom;
    }

    public static Thickness Parse(string text) {
        if (text == null || text.Length == 0) { return new Thickness(0.0f); }
        string[] parts = ParseUtil.SplitFloatTokens(text);
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
    private static CornerRadius _zeroCornerRadius = null;
    public static CornerRadius SharedZero() {
        if (_zeroCornerRadius == null) { _zeroCornerRadius = new CornerRadius(0.0f); }
        return _zeroCornerRadius;
    }
    private float _topLeft;
    private float _topRight;
    private float _bottomRight;
    private float _bottomLeft;
    public float TopLeft { get { return _topLeft; } }
    public float TopRight { get { return _topRight; } }
    public float BottomRight { get { return _bottomRight; } }
    public float BottomLeft { get { return _bottomLeft; } }

    public CornerRadius() {}
    public CornerRadius(float uniform) {
        _topLeft = uniform; _topRight = uniform; _bottomRight = uniform; _bottomLeft = uniform;
    }
    public CornerRadius(float topLeft, float topRight, float bottomRight, float bottomLeft) {
        _topLeft = topLeft; _topRight = topRight; _bottomRight = bottomRight; _bottomLeft = bottomLeft;
    }

    public CornerRadius Clone() {
        return new CornerRadius(_topLeft, _topRight, _bottomRight, _bottomLeft);
    }
    public override string ToDisplayString() {
        return "" + _topLeft + "," + _topRight + "," + _bottomRight + "," + _bottomLeft;
    }

    public static CornerRadius Parse(string text) {
        if (text == null || text.Length == 0) { return new CornerRadius(0.0f); }
        string[] parts = ParseUtil.SplitFloatTokens(text);
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
    private static Point _centerPoint = null;
    public static Point SharedCenter() {
        if (_centerPoint == null) { _centerPoint = new Point(0.5f, 0.5f); }
        return _centerPoint;
    }
    private float _x;
    private float _y;
    public float X { get { return _x; } }
    public float Y { get { return _y; } }
    public Point() {}
    public Point(float x, float y) { _x = x; _y = y; }
    public override string ToDisplayString() { return "" + _x + "," + _y; }
    public static Point Parse(string text) {
        if (text == null) { return new Point(0.0f, 0.0f); }
        string[] parts = ParseUtil.SplitFloatTokens(text);
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
    public static Size Parse(string text) {
        if (text == null) { return new Size(0.0f, 0.0f); }
        string[] parts = ParseUtil.SplitFloatTokens(text);
        if (parts.Length >= 2) {
            return new Size(ParseUtil.ParseFloat(parts[0], 0.0f), ParseUtil.ParseFloat(parts[1], 0.0f));
        }
        return new Size(0.0f, 0.0f);
    }
}

public class Rect : DependencyObject {
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public Rect() {}
    public Rect(float x, float y, float w, float h) { X = x; Y = y; Width = w; Height = h; }
    public bool Contains(float px, float py) {
        return px >= X && px >= Y && px <= (X + Width) && py <= (Y + Height);
    }
    public override string ToDisplayString() {
        return "" + X + "," + Y + "," + Width + "," + Height;
    }
    public static Rect Parse(string text) {
        if (text == null) { return new Rect(0.0f, 0.0f, 0.0f, 0.0f); }
        string[] parts = ParseUtil.SplitFloatTokens(text);
        if (parts.Length >= 4) {
            return new Rect(
                ParseUtil.ParseFloat(parts[0], 0.0f),
                ParseUtil.ParseFloat(parts[1], 0.0f),
                ParseUtil.ParseFloat(parts[2], 0.0f),
                ParseUtil.ParseFloat(parts[3], 0.0f));
        }
        return new Rect(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

// ============================================================================
// 5. Brushes, Transforms, and Effects
// ============================================================================
