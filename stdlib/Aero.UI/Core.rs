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
}

// ============================================================================
// 3. DependencyObject & Type-Safe Boxing (Zero RS5151 bare-object fields)
// ============================================================================

public class DependencyObject {
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

    public static string UnboxString(DependencyObject box, string fallback = "") {
        if (box == null) { return fallback; }
        if (box is BoxedString) { return (box as BoxedString).Value; }
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

    public override bool BoxValueEquals(DependencyObject other) {
        DependencyObject self = this;
        if (self == other) { return true; }
        if (other == null || !(other is NativeObjectBox)) { return false; }
        return UnboxObject() == other.UnboxObject();
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
    public static Size Parse(string text) {
        if (text == null) { return new Size(0.0f, 0.0f); }
        string[] parts = text.Trim().Split(",");
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
    public virtual uint ResolveRgbaUnscaled() { return ResolveRgba(); }
    public virtual int ResolveGradIdUnscaled() { return ResolveGradId(); }
    public virtual bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) { return false; }
    public virtual bool NeedsLoadedRefresh() { return false; }
    public virtual Brush ApplySubProperty(string suffix, DependencyObject val, bool allowClone) { return null; }
    public virtual DependencyObject ReadSubProperty(string suffix) { return null; }
}

public class SolidColorBrush : Brush {
    public override string ImplicitDataTypeKey() { return "SolidColorBrush"; }
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
    public override uint ResolveRgbaUnscaled() {
        return _rgba;
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
    public override string ImplicitDataTypeKey() { return "GradientStop"; }
    public uint ColorRgba { get; set; }
    public string Color {
        get { return ParseUtil.FormatArgbHex(ColorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }
    public float Offset { get; set; }

    public GradientStop() {}
    public GradientStop(uint rgba, float offset) { ColorRgba = rgba; Offset = offset; }
    public GradientStop(string color, float offset) { ColorRgba = ParseUtil.ParseColor(color); Offset = offset; }
    public override string ToDisplayString() { return Color + "@" + Offset; }
}

public class GradientBrush : Brush {
    private GradientStop[] _stops;
    protected int _cachedGradId = -1;
    protected bool _dirty = true;
    protected int _cachedUnscaledGradId = -1;
    protected bool _unscaledDirty = true;
    private int _spreadMethod = 0;
    private int _mappingMode = 0;

    public int SpreadMethod {
        get { return _spreadMethod; }
        set { if (_spreadMethod != value) { _spreadMethod = value; _dirty = true; _unscaledDirty = true; } }
    }
    public int MappingMode {
        get { return _mappingMode; }
        set { if (_mappingMode != value) { _mappingMode = value; _dirty = true; _unscaledDirty = true; } }
    }

    public GradientBrush() { _stops = new GradientStop[0]; }

    public GradientStop[] GradientStops() { return _stops; }

    public void AddStop(GradientStop stop) {
        if (stop == null) { return; }
        _stops.Push(stop);
        _dirty = true;
        _unscaledDirty = true;
    }
    public void AddStop(uint rgba, float offset) {
        AddStop(new GradientStop(rgba, offset));
    }
    public void AddStop(string color, float offset) {
        AddStop(new GradientStop(color, offset));
    }
    public void AddStopObj(object colorObj, float offset) {
        if (colorObj is BoxedString) {
            AddStop((colorObj as BoxedString).Value, offset);
        } else if (colorObj is SolidColorBrush) {
            AddStop((colorObj as SolidColorBrush).ResolveRgba(), offset);
        }
    }
    public void ClearStops() {
        _stops.Clear();
        _dirty = true;
        _unscaledDirty = true;
    }

    public int StopCount() { return _stops.Length; }

    public string GetStopColor(int idx) {
        if (idx < 0 || idx >= _stops.Length) { return "#00000000"; }
        return _stops[idx].Color;
    }

    public float GetStopOffset(int idx) {
        if (idx < 0 || idx >= _stops.Length) { return 0.0f; }
        return _stops[idx].Offset;
    }

    public bool SetStopColor(int idx, string hex) {
        if (idx < 0 || idx >= _stops.Length) { return false; }
        _stops[idx].Color = hex == null ? "#00000000" : hex;
        _dirty = true;
        _unscaledDirty = true;
        return true;
    }

    public bool SetStopOffset(int idx, float off) {
        if (idx < 0 || idx >= _stops.Length) { return false; }
        _stops[idx].Offset = Math.Clampf(off, 0.0f, 1.0f);
        _dirty = true;
        _unscaledDirty = true;
        return true;
    }

    protected void CopyGradientStopsAndPropsTo(GradientBrush dst) {
        if (dst == null) { return; }
        dst.Opacity = Opacity;
        dst.SpreadMethod = SpreadMethod;
        dst.MappingMode = MappingMode;
        for (int i = 0; i < _stops.Length; i += 1) {
            dst.AddStop(_stops[i].ColorRgba, _stops[i].Offset);
        }
    }

    public virtual GradientBrush CloneBrush() {
        var copy = new GradientBrush();
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }

    internal static int ParseStopSlotIndex(string suffix) {
        if (suffix == null || suffix.Length < 10) { return -1; }
        if (suffix.Substring(0, 4) != "Stop") { return -1; }
        if (suffix.EndsWith("Color")) {
            string numC = suffix.Substring(4, suffix.Length - 9);
            return ParseUtil.ParseInt(numC, -1);
        }
        if (suffix.EndsWith("Offset")) {
            string numO = suffix.Substring(4, suffix.Length - 10);
            return ParseUtil.ParseInt(numO, -1);
        }
        return -1;
    }

    internal bool TryApplyStopProperty(string suffix, DependencyObject value) {
        int idx = ParseStopSlotIndex(suffix);
        if (idx < 0) { return false; }
        if (suffix.Contains("Color")) {
            return SetStopColor(idx, DependencyObject.UnboxString(value, "#00000000"));
        }
        return SetStopOffset(idx, DependencyObject.UnboxFloat(value, 0.0f));
    }

    internal DependencyObject TryReadStopProperty(string suffix) {
        int idx = ParseStopSlotIndex(suffix);
        if (idx < 0) { return null; }
        if (suffix.Contains("Color")) {
            return DependencyObject.BoxString(GetStopColor(idx));
        }
        return DependencyObject.BoxFloat(GetStopOffset(idx));
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

    protected string BuildStopsSpecUnscaled() {
        string spec = "";
        for (int i = 0; i < _stops.Length; i += 1) {
            if (i > 0) { spec = spec + ";"; }
            spec = spec + _stops[i].Offset + ":" + ParseUtil.FormatArgbHex(_stops[i].ColorRgba);
        }
        return spec;
    }

    public override uint ResolveRgba() {
        if (_stops.Length > 0) { return _stops[0].ColorRgba; }
        return 0u;
    }

    public override string ToDisplayString() {
        return "grad:" + _stops.Length + ":" + BuildStopsSpec();
    }
}

public class LinearGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "LinearGradientBrush"; }
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

    public float X0 { get { return _x0; } set { _x0 = value; _dirty = true; _unscaledDirty = true; } }
    public float Y0 { get { return _y0; } set { _y0 = value; _dirty = true; _unscaledDirty = true; } }
    public float X1 { get { return _x1; } set { _x1 = value; _dirty = true; _unscaledDirty = true; } }
    public float Y1 { get { return _y1; } set { _y1 = value; _dirty = true; _unscaledDirty = true; } }
    public Point StartPoint {
        get { return new Point(_x0, _y0); }
        set { if (value != null) { _x0 = value.X; _y0 = value.Y; _dirty = true; _unscaledDirty = true; } }
    }
    public Point EndPoint {
        get { return new Point(_x1, _y1); }
        set { if (value != null) { _x1 = value.X; _y1 = value.Y; _dirty = true; _unscaledDirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterLinearGradient(
                _x0, _y0, _x1, _y1, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            _cachedUnscaledGradId = Visual_RegisterLinearGradient(
                _x0, _y0, _x1, _y1, SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new LinearGradientBrush(_x0, _y0, _x1, _y1);
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }
}

public class RadialGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "RadialGradientBrush"; }
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
    public RadialGradientBrush(float cx, float cy, float ox, float oy, float rx, float ry) : base() {
        _cx = cx; _cy = cy; _ox = ox; _oy = oy; _rx = rx; _ry = ry;
    }

    public float CenterX { get { return _cx; } set { _cx = value; _dirty = true; _unscaledDirty = true; } }
    public float CenterY { get { return _cy; } set { _cy = value; _dirty = true; _unscaledDirty = true; } }
    public float OriginX { get { return _ox; } set { _ox = value; _dirty = true; _unscaledDirty = true; } }
    public float OriginY { get { return _oy; } set { _oy = value; _dirty = true; _unscaledDirty = true; } }
    public float RadiusX { get { return _rx; } set { _rx = value; _dirty = true; _unscaledDirty = true; } }
    public float RadiusY { get { return _ry; } set { _ry = value; _dirty = true; _unscaledDirty = true; } }
    public Point Center {
        get { return new Point(_cx, _cy); }
        set { if (value != null) { _cx = value.X; _cy = value.Y; _dirty = true; _unscaledDirty = true; } }
    }
    public Point GradientOrigin {
        get { return new Point(_ox, _oy); }
        set { if (value != null) { _ox = value.X; _oy = value.Y; _dirty = true; _unscaledDirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterRadialGradient(
                _cx, _cy, _ox, _oy, _rx, _ry, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            _cachedUnscaledGradId = Visual_RegisterRadialGradient(
                _cx, _cy, _ox, _oy, _rx, _ry, SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new RadialGradientBrush(_cx, _cy, _ox, _oy, _rx, _ry);
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }
}

public class ConicGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "ConicGradientBrush"; }
    private float _cx = 0.5f;
    private float _cy = 0.5f;

    public ConicGradientBrush() : base() {}
    public float CenterX { get { return _cx; } set { _cx = value; _dirty = true; _unscaledDirty = true; } }
    public float CenterY { get { return _cy; } set { _cy = value; _dirty = true; _unscaledDirty = true; } }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterConicGradient(
                _cx, _cy, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            _cachedUnscaledGradId = Visual_RegisterConicGradient(
                _cx, _cy, SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new ConicGradientBrush();
        copy.CenterX = _cx;
        copy.CenterY = _cy;
        CopyGradientStopsAndPropsTo(copy);
        return copy;
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
    public bool HasExplicitCenter = false;
    private static TransformData _scratch;

    internal static TransformData SharedScratch() {
        if (_scratch == null) { _scratch = new TransformData(); }
        _scratch.TX = 0.0f;
        _scratch.TY = 0.0f;
        _scratch.SX = 1.0f;
        _scratch.SY = 1.0f;
        _scratch.RotDeg = 0.0f;
        _scratch.SkewX = 0.0f;
        _scratch.SkewY = 0.0f;
        _scratch.HasExplicitCenter = false;
        return _scratch;
    }
}

public class Transform : DependencyObject {
    private string[] _attachedKeys;
    private DependencyObject[] _attachedVals;

    public virtual void Accumulate(TransformData acc) {}

    public void SetAttachedValue(string key, DependencyObject val) {
        if (key == null || key.Length == 0) { return; }
        if (_attachedKeys == null) {
            _attachedKeys = new string[0];
            _attachedVals = new DependencyObject[0];
        }
        for (int i = 0; i < _attachedKeys.Length; i += 1) {
            if (_attachedKeys[i] == key) {
                _attachedVals[i] = val;
                return;
            }
        }
        _attachedKeys.Push(key);
        _attachedVals.Push(val);
    }

    public DependencyObject GetAttachedValue(string key) {
        if (key == null || _attachedKeys == null) { return null; }
        for (int i = 0; i < _attachedKeys.Length; i += 1) {
            if (_attachedKeys[i] == key) { return _attachedVals[i]; }
        }
        return null;
    }
}

public class TranslateTransform : Transform {
    public override string ImplicitDataTypeKey() { return "TranslateTransform"; }
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
    public override string ImplicitDataTypeKey() { return "ScaleTransform"; }
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private bool _hasExplicitCenter = false;
    public float ScaleX { get; set; } = 1.0f;
    public float ScaleY { get; set; } = 1.0f;
    public float CenterX {
        get { return _centerX; }
        set { _centerX = value; _hasExplicitCenter = true; }
    }
    public float CenterY {
        get { return _centerY; }
        set { _centerY = value; _hasExplicitCenter = true; }
    }
    public ScaleTransform() {}
    public ScaleTransform(float sx, float sy) { ScaleX = sx; ScaleY = sy; }
    public ScaleTransform(float sx, float sy, float cx, float cy) {
        ScaleX = sx; ScaleY = sy; _centerX = cx; _centerY = cy; _hasExplicitCenter = true;
    }
    public override void Accumulate(TransformData acc) {
        if (_hasExplicitCenter || _centerX != 0.0f || _centerY != 0.0f) {
            acc.HasExplicitCenter = true;
        }
        if (_centerX != 0.0f || _centerY != 0.0f) {
            acc.TX = (acc.TX - _centerX) * ScaleX + _centerX;
            acc.TY = (acc.TY - _centerY) * ScaleY + _centerY;
        }
        acc.SX = acc.SX * ScaleX;
        acc.SY = acc.SY * ScaleY;
    }
}

public class RotateTransform : Transform {
    public override string ImplicitDataTypeKey() { return "RotateTransform"; }
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private bool _hasExplicitCenter = false;
    public float Angle { get; set; }
    public float CenterX {
        get { return _centerX; }
        set { _centerX = value; _hasExplicitCenter = true; }
    }
    public float CenterY {
        get { return _centerY; }
        set { _centerY = value; _hasExplicitCenter = true; }
    }
    public RotateTransform() {}
    public RotateTransform(float angle) { Angle = angle; }
    public RotateTransform(float angle, float cx, float cy) {
        Angle = angle; _centerX = cx; _centerY = cy; _hasExplicitCenter = true;
    }
    public override void Accumulate(TransformData acc) {
        if (_hasExplicitCenter || _centerX != 0.0f || _centerY != 0.0f) {
            acc.HasExplicitCenter = true;
        }
        if (Angle != 0.0f && (_centerX != 0.0f || _centerY != 0.0f)) {
            float rad = Angle * 0.0174532925f;
            float s = 0.0f;
            float c = 1.0f;
            if (ParseUtil.IsFinite(rad)) {
                float rawS = Math.Sinf(rad);
                float rawC = Math.Cosf(rad);
                if (ParseUtil.IsFinite(rawS) && ParseUtil.IsFinite(rawC)) {
                    s = rawS;
                    c = rawC;
                }
            }
            float dx = acc.TX - _centerX;
            float dy = acc.TY - _centerY;
            acc.TX = dx * c - dy * s + _centerX;
            acc.TY = dx * s + dy * c + _centerY;
        }
        acc.RotDeg = acc.RotDeg + Angle;
    }
}

public class SkewTransform : Transform {
    public override string ImplicitDataTypeKey() { return "SkewTransform"; }
    public float AngleX { get; set; }
    public float AngleY { get; set; }
    public float CenterX { get; set; }
    public float CenterY { get; set; }
    public SkewTransform() {}
    public SkewTransform(float ax, float ay) { AngleX = ax; AngleY = ay; }
    public SkewTransform(float ax, float ay, float cx, float cy) {
        AngleX = ax; AngleY = ay; CenterX = cx; CenterY = cy;
    }
    public override void Accumulate(TransformData acc) {
        acc.SkewX = acc.SkewX + AngleX;
        acc.SkewY = acc.SkewY + AngleY;
    }
}

public class TransformGroup : Transform {
    public override string ImplicitDataTypeKey() { return "TransformGroup"; }
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

internal class EffectChangeHook {
    public Action Callback;
    public bool Active = true;
    public EffectChangeHook(Action cb) { Callback = cb; }
}

public class Effect : DependencyObject {
    private UIEffectKind _kind = UIEffectKind.None;
    private float _radius = 0.0f;
    private float _depth = 0.0f;
    private float _direction = 315.0f;
    private uint _colorRgba = 4278190080u;
    private float _opacity = 1.0f;
    private EffectChangeHook[] _listeners;

    internal EffectChangeHook AddChangedListener(Action cb) {
        if (cb == null) { return null; }
        if (_listeners == null) { _listeners = new EffectChangeHook[0]; }
        var hook = new EffectChangeHook(cb);
        _listeners.Push(hook);
        return hook;
    }

    internal void RemoveChangedListener(EffectChangeHook hook) {
        if (hook == null) { return; }
        hook.Active = false;
        if (_listeners == null) { return; }
        for (int i = _listeners.Length - 1; i >= 0; i -= 1) {
            if (_listeners[i] == hook || !_listeners[i].Active) {
                _listeners.RemoveAt(i);
            }
        }
    }

    internal void ClearChangedListeners() {
        if (_listeners == null) { return; }
        for (int i = 0; i < _listeners.Length; i += 1) {
            _listeners[i].Active = false;
        }
        _listeners.Clear();
    }

    private void NotifyChanged() {
        if (_listeners == null || _listeners.Length == 0) { return; }
        for (int i = _listeners.Length - 1; i >= 0; i -= 1) {
            EffectChangeHook h = _listeners[i];
            if (h == null || !h.Active || h.Callback == null) {
                _listeners.RemoveAt(i);
                continue;
            }
            var cb = h.Callback;
            cb();
        }
    }

    public UIEffectKind Kind {
        get { return _kind; }
        set { if (_kind == value) { return; } _kind = value; NotifyChanged(); }
    }
    public float Radius {
        get { return _radius; }
        set { if (_radius == value) { return; } _radius = value; NotifyChanged(); }
    }
    public float Depth {
        get { return _depth; }
        set { if (_depth == value) { return; } _depth = value; NotifyChanged(); }
    }
    public float Direction {
        get { return _direction; }
        set { if (_direction == value) { return; } _direction = value; NotifyChanged(); }
    }
    public uint ColorRgba {
        get { return _colorRgba; }
        set { if (_colorRgba == value) { return; } _colorRgba = value; NotifyChanged(); }
    }
    public float Opacity {
        get { return _opacity; }
        set { if (_opacity == value) { return; } _opacity = value; NotifyChanged(); }
    }

    public string Color {
        get { return ParseUtil.FormatArgbHex(_colorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }

    public bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == null) { return false; }
        if (propName == "Radius" || propName == "BlurRadius" || propName == "Size" || propName == "PixelSize" || propName == "Intensity" || propName == "Factor") {
            Radius = DependencyObject.UnboxFloat(value, _radius);
            return true;
        }
        if (propName == "Depth" || propName == "ShadowDepth") {
            Depth = DependencyObject.UnboxFloat(value, _depth);
            return true;
        }
        if (propName == "Direction" || propName == "Angle") {
            Direction = DependencyObject.UnboxFloat(value, _direction);
            return true;
        }
        if (propName == "Opacity") {
            Opacity = Math.Clampf(DependencyObject.UnboxFloat(value, _opacity), 0.0f, 1.0f);
            return true;
        }
        if (propName == "Color") {
            if (value is BoxedInt) { ColorRgba = (uint)(value as BoxedInt).Value; }
            else { Color = DependencyObject.UnboxString(value, Color); }
            return true;
        }
        return false;
    }

    public DependencyObject GetPropertyValue(string propName) {
        if (propName == "Radius" || propName == "BlurRadius" || propName == "Size" || propName == "PixelSize" || propName == "Intensity" || propName == "Factor") {
            return DependencyObject.BoxFloat(_radius);
        }
        if (propName == "Depth" || propName == "ShadowDepth") { return DependencyObject.BoxFloat(_depth); }
        if (propName == "Direction" || propName == "Angle") { return DependencyObject.BoxFloat(_direction); }
        if (propName == "Opacity") { return DependencyObject.BoxFloat(_opacity); }
        if (propName == "Color") { return DependencyObject.BoxString(Color); }
        return null;
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

public class ImageBrush : Brush {
    public override string ImplicitDataTypeKey() { return "ImageBrush"; }
    public string ImageSource { get; set; } = "";
    public Stretch Stretch { get; set; } = Stretch.Fill;
    public string TileMode { get; set; } = "None";
    public string Viewbox { get; set; } = "0,0,1,1";
    public string Viewport { get; set; } = "";
    public string ViewportUnits { get; set; } = "RelativeToBoundingBox";
    public uint TintRgba { get; set; } = 0u;
    public bool IsPlasmaShader { get; set; } = false;
    private float _plasmaScaleX = 64.0f;
    private float _plasmaScaleY = 64.0f;
    private float _plasmaTime = 0.0f;
    private float _plasmaSeed = 100.0f;
    private string _plasmaColor = "#FF01BCFF";
    private float _plasmaVw = 300.0f;
    private float _plasmaVh = 90.0f;

    public ImageBrush() { TintRgba = ParseUtil.PackArgb(48u, 32u, 56u, 96u); }
    public ImageBrush(string source) {
        ImageSource = source == null ? "" : source;
        TintRgba = ParseUtil.PackArgb(48u, 32u, 56u, 96u);
    }

    public void EnablePlasmaShader(float sx, float sy, float timeSec, float seed, string colorHex, float vw, float vh) {
        IsPlasmaShader = true;
        _plasmaScaleX = sx;
        _plasmaScaleY = sy;
        _plasmaTime = timeSec;
        _plasmaSeed = seed;
        _plasmaColor = (colorHex == null || colorHex.Length == 0) ? "#FF01BCFF" : colorHex;
        _plasmaVw = vw > 1.0f ? vw : 300.0f;
        _plasmaVh = vh > 1.0f ? vh : 90.0f;
        RebuildPlasmaUri();
    }

    public float PlasmaScaleX { get { return _plasmaScaleX; } set { _plasmaScaleX = value; RebuildPlasmaUri(); } }
    public float PlasmaScaleY { get { return _plasmaScaleY; } set { _plasmaScaleY = value; RebuildPlasmaUri(); } }
    public float PlasmaTime {
        get { return _plasmaTime; }
        set {
            if (Math.Absf(value - _plasmaTime) >= 0.004f) {
                _plasmaTime = value;
                RebuildPlasmaUri();
            }
        }
    }
    public float PlasmaSeed { get { return _plasmaSeed; } set { _plasmaSeed = value; RebuildPlasmaUri(); } }
    public string PlasmaColor {
        get { return _plasmaColor; }
        set {
            string next = (value == null || value.Length == 0) ? "#FF01BCFF" : value;
            if (_plasmaColor != next) { _plasmaColor = next; RebuildPlasmaUri(); }
        }
    }

    private void RebuildPlasmaUri() {
        if (!IsPlasmaShader) { return; }
        ImageSource = "shader://plasma?sx=" + _plasmaScaleX +
                      "&sy=" + _plasmaScaleY +
                      "&seed=" + _plasmaSeed +
                      "&time=" + _plasmaTime +
                      "&color=" + _plasmaColor +
                      "&vw=" + _plasmaVw +
                      "&vh=" + _plasmaVh;
    }

    public ImageBrush CloneImageBrush() {
        var c = new ImageBrush(ImageSource);
        c.Stretch = Stretch;
        c.TileMode = TileMode;
        c.Viewbox = Viewbox;
        c.Viewport = Viewport;
        c.ViewportUnits = ViewportUnits;
        c.TintRgba = TintRgba;
        c.Opacity = Opacity;
        if (IsPlasmaShader) {
            c.EnablePlasmaShader(_plasmaScaleX, _plasmaScaleY, _plasmaTime, _plasmaSeed, _plasmaColor, _plasmaVw, _plasmaVh);
        }
        return c;
    }

    public override uint ResolveRgba() { return TintRgba; }

    public override bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) {
        uint alpha = tintByOpacity ? (uint)Math.Clampf(Opacity * 255.0f + 0.5f, 0.0f, 255.0f) : 255u;
        uint tint = ParseUtil.PackArgb(alpha, 255u, 255u, 255u);
        Visual_SetVisualBrushSource(h, ParseUtil.EmptyHandle(), 1, 1);
        Visual_SetImage(
            h,
            ImageSource,
            EnumConvert.ToInt(Stretch),
            EnumConvert.ToInt(StretchDirection.Both),
            tint);
        UIElement.ApplyImageBrushViewbox(h, Viewbox);
        UIElement.ApplyImageBrushViewport(h, Viewport, ViewportUnits);
        Visual_SetImageTile(h, TileMode == "Tile");
        return true;
    }

    public override Brush ApplySubProperty(string suffix, DependencyObject val, bool allowClone) {
        if (suffix != "ImageSource" && suffix != "PlasmaSeed" && suffix != "PlasmaColor" &&
            suffix != "PlasmaTime" && suffix != "PlasmaScaleX" && suffix != "PlasmaScaleY") {
            return null;
        }
        ImageBrush target = allowClone ? CloneImageBrush() : this;
        if (suffix == "ImageSource") { target.ImageSource = DependencyObject.UnboxString(val, ""); }
        else if (suffix == "PlasmaSeed") { target.PlasmaSeed = DependencyObject.UnboxFloat(val, 100.0f); }
        else if (suffix == "PlasmaColor") { target.PlasmaColor = DependencyObject.UnboxString(val, "#FF01BCFF"); }
        else if (suffix == "PlasmaTime") { target.PlasmaTime = DependencyObject.UnboxFloat(val, 0.0f); }
        else if (suffix == "PlasmaScaleX") { target.PlasmaScaleX = DependencyObject.UnboxFloat(val, 64.0f); }
        else if (suffix == "PlasmaScaleY") { target.PlasmaScaleY = DependencyObject.UnboxFloat(val, 64.0f); }
        return target;
    }

    public override DependencyObject ReadSubProperty(string suffix) {
        if (suffix == "ImageSource") { return DependencyObject.BoxString(ImageSource); }
        if (suffix == "PlasmaSeed") { return DependencyObject.BoxFloat(PlasmaSeed); }
        if (suffix == "PlasmaColor") { return DependencyObject.BoxString(PlasmaColor); }
        if (suffix == "PlasmaTime") { return DependencyObject.BoxFloat(PlasmaTime); }
        if (suffix == "PlasmaScaleX") { return DependencyObject.BoxFloat(PlasmaScaleX); }
        if (suffix == "PlasmaScaleY") { return DependencyObject.BoxFloat(PlasmaScaleY); }
        return null;
    }
}

public class VisualBrush : Brush {
    public override string ImplicitDataTypeKey() { return "VisualBrush"; }
    public UIElement Visual { get; set; }
    public string VisualElementName { get; set; } = "";
    public Stretch Stretch { get; set; } = Stretch.Fill;
    public string TileMode { get; set; } = "None";
    public string Viewbox { get; set; } = "0,0,1,1";
    public string Viewport { get; set; } = "";
    public string ViewportUnits { get; set; } = "RelativeToBoundingBox";
    public string AlignmentX { get; set; } = "Center";
    public string AlignmentY { get; set; } = "Center";

    public VisualBrush() {}
    public VisualBrush(UIElement visual) { Visual = visual; }

    public UIElement ResolveVisual(UIElement host) {
        if (Visual != null) { return Visual; }
        if (VisualElementName != null && VisualElementName.Length > 0 && host != null) {
            UIElement found = host.FindName(VisualElementName);
            UIElement prev = host;
            UIElement cur = host.Parent != null ? host.Parent : host._logicalParent;
            int guard = 0;
            while (found == null && cur != null && guard < 128) {
                guard += 1;
                found = cur.FindNameExcluding(VisualElementName, prev);
                prev = cur;
                cur = cur.Parent != null ? cur.Parent : cur._logicalParent;
            }
            if (found == null) {
                UIElement root = host.RootElement();
                if (root != null) { found = root.FindName(VisualElementName); }
            }
            if (found != null) { Visual = found; }
            return found;
        }
        return null;
    }

    public static int ParseAlignX(string s) {
        if (s == null) { return 1; }
        string t = s.Trim();
        if (t == "Left" || t == "0") { return 0; }
        if (t == "Right" || t == "2") { return 2; }
        return 1;
    }

    public static int ParseAlignY(string s) {
        if (s == null) { return 1; }
        string t = s.Trim();
        if (t == "Top" || t == "0") { return 0; }
        if (t == "Bottom" || t == "2") { return 2; }
        return 1;
    }

    public override bool NeedsLoadedRefresh() { return true; }

    public override bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) {
        UIElement srcEl = ResolveVisual(owner);
        uint alpha = tintByOpacity ? (uint)Math.Clampf(Opacity * 255.0f + 0.5f, 0.0f, 255.0f) : 255u;
        uint tint = ParseUtil.PackArgb(alpha, 255u, 255u, 255u);
        Visual_SetImage(
            h,
            "",
            EnumConvert.ToInt(Stretch),
            EnumConvert.ToInt(StretchDirection.Both),
            tint);
        UIElement.ApplyImageBrushViewbox(h, Viewbox);
        UIElement.ApplyImageBrushViewport(h, Viewport, ViewportUnits);
        Visual_SetImageTile(h, TileMode == "Tile");
        handle srcH = (srcEl != null && ParseUtil.IsValidHandle(srcEl.__nativeHandle))
            ? srcEl.__nativeHandle
            : ParseUtil.EmptyHandle();
        Visual_SetVisualBrushSource(
            h,
            srcH,
            ParseAlignX(AlignmentX),
            ParseAlignY(AlignmentY));
        return true;
    }
}

public class PlaneProjection : DependencyObject {
    public float RotationX { get; set; } = 0.0f;
    public float RotationY { get; set; } = 0.0f;
    public float RotationZ { get; set; } = 0.0f;
    public float LocalOffsetX { get; set; } = 0.0f;
    public float LocalOffsetY { get; set; } = 0.0f;
    public float LocalOffsetZ { get; set; } = 0.0f;
    public float GlobalOffsetX { get; set; } = 0.0f;
    public float GlobalOffsetY { get; set; } = 0.0f;
    public float GlobalOffsetZ { get; set; } = 0.0f;
    public float CenterOfRotationX { get; set; } = 0.5f;
    public float CenterOfRotationY { get; set; } = 0.5f;
    public float CenterOfRotationZ { get; set; } = 0.0f;

    public PlaneProjection() {}
    public PlaneProjection(float rx, float ry, float rz) {
        RotationX = rx; RotationY = ry; RotationZ = rz;
    }
}

public class CompositeTransform3D : DependencyObject {
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private float _centerZ = 0.0f;
    public bool HasExplicitCenter { get; set; } = false;
    public float RotationX { get; set; } = 0.0f;
    public float RotationY { get; set; } = 0.0f;
    public float RotationZ { get; set; } = 0.0f;
    public float TranslateX { get; set; } = 0.0f;
    public float TranslateY { get; set; } = 0.0f;
    public float TranslateZ { get; set; } = 0.0f;
    public float ScaleX { get; set; } = 1.0f;
    public float ScaleY { get; set; } = 1.0f;
    public float ScaleZ { get; set; } = 1.0f;
    public float CenterX { get { return _centerX; } set { _centerX = value; HasExplicitCenter = true; } }
    public float CenterY { get { return _centerY; } set { _centerY = value; HasExplicitCenter = true; } }
    public float CenterZ { get { return _centerZ; } set { _centerZ = value; HasExplicitCenter = true; } }

    public CompositeTransform3D() {}
}

public class KeyBinding : DependencyObject {
    public KeyCode Key { get; set; } = KeyCode.None;
    public int Modifiers { get; set; } = 0;
    public ICommand Command { get; set; }
    public string CommandPath { get; set; } = "";
    public string CommandParameterPath { get; set; } = "";
    private DependencyObject _paramBox;

    public KeyBinding() {}
    public KeyBinding(ICommand command, KeyCode key, int modifiers = 0) {
        Command = command;
        Key = key;
        Modifiers = modifiers;
    }

    public object CommandParameter {
        get { return DependencyObject.Unbox(_paramBox); }
        set { _paramBox = DependencyObject.BoxObject(value); }
    }
    public void SetCommandParameter(string val) { _paramBox = DependencyObject.BoxString(val); }
    public void SetCommandParameterBox(DependencyObject box) { _paramBox = box; }
    public DependencyObject GetCommandParameterBox() { return _paramBox; }
}

// ============================================================================
// 6. UIElement & FrameworkElement
// ============================================================================

public delegate void PopupDismissHook(UIElement clickedTarget, bool isEscape);
public delegate void AttachedPropChangedHook(UIElement el, string prop, DependencyObject val);

public class BehaviorEventSink : DependencyObject {
    public virtual void OnBehaviorEvent(object sender, RoutedEventArgs e) {}
}

internal class UIElementRareData {
    internal DependencyObject ToolTip;
    internal Func<object, UIElement> ToolTipFactory;
    internal UIElement ContextMenuEl;
    internal float Rot3DX = 0.0f;
    internal float Rot3DY = 0.0f;
    internal float Rot3DZ = 0.0f;
    internal float Trans3DX = 0.0f;
    internal float Trans3DY = 0.0f;
    internal float Trans3DZ = 0.0f;
    internal float Scale3DX = 1.0f;
    internal float Scale3DY = 1.0f;
    internal float Scale3DZ = 1.0f;
    internal float Center3DX = 0.0f;
    internal float Center3DY = 0.0f;
    internal float Center3DZ = 0.0f;
    internal float CamDist = 0.0f;
    internal PlaneProjection Projection3D;
    internal CompositeTransform3D Transform3D;
    internal KeyBinding[] InputBindings;
    internal string RiveSource = "";
    internal ResourceDictionary LocResources;
    internal string LocSource = "";
}

public partial class UIElement : DependencyObject {
    internal DependencyObject _ddLastItemBox;
    internal int _ddLastItemVer = -1;
    internal int _ddLastElemEpoch = -1;
    internal bool _ddInitialized = false;
    internal string _ddLastEmptyPath = "";
    internal int _propEpoch = 1;
    internal int _ancestorChainEpoch = 1;
    internal static int _disposeEpoch = 1;
    internal static int _treeParentEpoch = 1;
    internal static int _visibilityEpoch = 1;
    public static int _closedPopupBuildDepth = 0;

    private bool _cachedClosedPopupVal = false;
    private int _cachedClosedPopupTreeEpoch = 0;
    private int _cachedClosedPopupChainEpoch = 0;
    private bool _cachedCollapsedVal = false;
    private int _cachedCollapsedVisEpoch = 0;
    private int _cachedCollapsedTreeEpoch = 0;
    private int _cachedCollapsedChainEpoch = 0;

    internal void BumpPropEpoch() {
        _propEpoch = _propEpoch >= 1000000000 ? 1 : (_propEpoch + 1);
    }

    internal void BumpSubtreeAncestorEpoch() {
        bool noChildren = (_children == null || _children.Length == 0);
        bool noDetached = (_detachedLogicalChildren == null || _detachedLogicalChildren.Length == 0);
        if (noChildren && noDetached) {
            _ancestorChainEpoch = _ancestorChainEpoch >= 1000000000 ? 1 : (_ancestorChainEpoch + 1);
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int qi = 0;
        while (qi < q.Length) {
            UIElement cur = q[qi];
            qi += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            cur._ancestorChainEpoch = cur._ancestorChainEpoch >= 1000000000 ? 1 : (cur._ancestorChainEpoch + 1);
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
            if (cur._detachedLogicalChildren != null) {
                for (int i = 0; i < cur._detachedLogicalChildren.Length; i += 1) {
                    q.Push(cur._detachedLogicalChildren[i]);
                }
            }
        }
    }

    internal bool IsInsideClosedPopup() {
        if (_parent == null) { return false; }
        if (_cachedClosedPopupTreeEpoch == _treeParentEpoch &&
            _cachedClosedPopupChainEpoch == _ancestorChainEpoch) {
            return _cachedClosedPopupVal;
        }
        bool res = false;
        UIElement cur = _parent;
        int guard = 0;
        while (cur != null && guard < 128) {
            guard += 1;
            if (cur.IsClosedPopupBoundary()) {
                res = true;
                break;
            }
            cur = cur._parent;
        }
        _cachedClosedPopupVal = res;
        _cachedClosedPopupTreeEpoch = _treeParentEpoch;
        _cachedClosedPopupChainEpoch = _ancestorChainEpoch;
        return res;
    }

    internal static void BumpVisibilityEpoch() {
        _visibilityEpoch = _visibilityEpoch >= 1000000000 ? 1 : (_visibilityEpoch + 1);
    }

    internal bool IsInsideCollapsedSubtree() {
        if (_parent == null) { return false; }
        if (_cachedCollapsedVisEpoch == _visibilityEpoch &&
            _cachedCollapsedTreeEpoch == _treeParentEpoch &&
            _cachedCollapsedChainEpoch == _ancestorChainEpoch) {
            return _cachedCollapsedVal;
        }
        bool collapsed = false;
        UIElement cur = _parent;
        int guard = 0;
        while (cur != null && guard < 128) {
            guard += 1;
            if (cur._visibility == Visibility.Collapsed) {
                collapsed = true;
                break;
            }
            cur = cur._parent;
        }
        _cachedCollapsedVal = collapsed;
        _cachedCollapsedVisEpoch = _visibilityEpoch;
        _cachedCollapsedTreeEpoch = _treeParentEpoch;
        _cachedCollapsedChainEpoch = _ancestorChainEpoch;
        return collapsed;
    }

    internal virtual int FastPropEpochCategory() { return 1; }

    internal static void BumpTreeParentEpoch() {
        _treeParentEpoch = _treeParentEpoch >= 1000000000 ? 1 : (_treeParentEpoch + 1);
    }
    public handle __nativeHandle;
    private bool _hasNative;
    private VisualKind _visualKind = VisualKind.Box;

    private string _name = "";
    private string _uid = "";
    private DependencyObject _tag;
    internal UIElementRareData _rare;
    internal UIElementRareData EnsureRare() {
        if (_rare == null) { _rare = new UIElementRareData(); }
        return _rare;
    }
    private DependencyObject _explicitDataContext;
    private DependencyObject _inheritedDataContext;
    private UIElement _templatedParent;
    private UIElement _parent;
    private UIElement[] _children;
    private UIElement[] _detachedLogicalChildren;
    internal DependencyObject _vsgBox;
    internal DependencyObject _ownBindingsBox;

    private ResourceDictionary _resources;
    private ResourceDictionary _ambientResources;
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
    private bool _isKeyboardFocusWithin = false;
    private bool _isMouseOver = false;
    private bool _isPressed = false;
    private bool _clipToBounds = false;
    private int _zIndex = 0;
    private int _alternationIndex = 0;

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
    private Transform _layoutTransform;
    private Point _renderTransformOrigin;
    internal static AttachedPropChangedHook _attachedPropHook;
    private Effect _effect;
    private Brush _opacityMask;
    private BlendingMode _blendMode = BlendingMode.Normal;

    private Brush _background;
    private float _bgOpacity = 1.0f;
    private Brush _borderBrush;
    private Thickness _borderThickness;
    private CornerRadius _cornerRadius;

    private float _explicitFontSize = 0.0f;
    private float _inheritedFontSize = 0.0f;
    private string _explicitFontFamily = "";
    private string _inheritedFontFamily = "";
    private string _explicitFontWeight = "";
    private string _inheritedFontWeight = "";
    private Brush _explicitTextForeground;
    private Brush _inheritedTextForeground;
    private string _flowDirection = "LeftToRight";
    internal Style _appliedStyle;
    internal bool _hasTemplateResources = false;
    internal bool _hasTemplateAmbientResources = false;
    public string _internalRootName = "";
    internal bool _isDisposedTree = false;
    private string[] _dynPropKeys;
    private DependencyObject[] _dynPropVals;
    internal UIElement _logicalParent;
    private string[] _localProps;
    internal string[] _activeTriggerProps;
    internal int _uiFlags = 0;
    internal static AttachedPropChangedHook _toolTipEventHook;
    internal static AttachedPropChangedHook _contextMenuSetterHook;

    internal bool HasUiFlag(int bit) {
        return ((_uiFlags / bit) % 2) != 0;
    }

    internal void SetUiFlag(int bit, bool enabled) {
        bool cur = HasUiFlag(bit);
        if (enabled && !cur) {
            _uiFlags = _uiFlags + bit;
        } else if (!enabled && cur) {
            _uiFlags = _uiFlags - bit;
        }
    }

    protected bool _hasExplicitHAlign = false;
    protected bool _hasExplicitVAlign = false;
    private Action[] _stateInvalidatedHooks;

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
        if (_name == null) { _name = ""; }
        if (_uid == null) { _uid = ""; }
        _width = ParseUtil.NaN();
        _height = ParseUtil.NaN();
        _maxWidth = ParseUtil.PositiveInfinity();
        _maxHeight = ParseUtil.PositiveInfinity();
        _hAlign = HorizontalAlignment.Stretch;
        _vAlign = VerticalAlignment.Stretch;
        _visibility = Visibility.Visible;
        _opacity = 1.0f;
        _isEnabled = true;
        _isHitTestVisible = true;
        _gridRowSpan = 1;
        _gridColSpan = 1;
        _dock = Dock.Left;
        _canvasLeft = ParseUtil.NaN();
        _canvasTop = ParseUtil.NaN();
        _canvasRight = ParseUtil.NaN();
        _canvasBottom = ParseUtil.NaN();
        _blendMode = BlendingMode.Normal;
        if (_explicitFontFamily == null) { _explicitFontFamily = ""; }
        if (_inheritedFontFamily == null) { _inheritedFontFamily = ""; }
        if (_explicitFontWeight == null) { _explicitFontWeight = ""; }
        if (_inheritedFontWeight == null) { _inheritedFontWeight = ""; }
        if (_flowDirection == null || _flowDirection.Length == 0) { _flowDirection = "LeftToRight"; }
        _children = null;
        _detachedLogicalChildren = null;
        _margin = Thickness.SharedZero();
        _padding = Thickness.SharedZero();
        _borderThickness = Thickness.SharedZero();
        _cornerRadius = CornerRadius.SharedZero();
        _renderTransformOrigin = Point.SharedCenter();
        _dynResProps = null;
        _dynResKeys = null;
        CreateNativeVisual(kind);
        SyncInteractiveFlags();
    }

    protected virtual int GetCallbackMask() {
        return VisualCallbacks.StandardInput();
    }

    protected void CreateNativeVisual(VisualKind kind) {
        _visualKind = kind;
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
    protected void NotifyBoundPropertyChanged(string propName, DependencyObject boxedValue) {
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            BindingEngine.NotifyTargetPropertyChanged(this, propName, boxedValue);
        }
    }
    protected void NotifyBoundFloatChanged(string propName, float val) {
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            BindingEngine.NotifyTargetPropertyChanged(this, propName, DependencyObject.BoxFloat(val));
        }
    }
    protected void NotifyBoundIntChanged(string propName, int val) {
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            BindingEngine.NotifyTargetPropertyChanged(this, propName, DependencyObject.BoxInt(val));
        }
    }

    public object Tag {
        get { return DependencyObject.Unbox(_tag); }
        set {
            DependencyObject next = DependencyObject.BoxObject(value);
            bool same = (_tag == next) || (_tag != null && next != null && _tag.BoxValueEquals(next));
            if (same) { return; }
            _tag = next;
            NotifyBoundPropertyChanged("Tag", _tag);
            UpdateTriggersAndStates();
        }
    }
    public bool HasToolTip {
        get { return _rare != null && (_rare.ToolTip != null || _rare.ToolTipFactory != null); }
    }
    public void SetLazyToolTip(Func<object, UIElement> factory) {
        if (factory == null && _rare == null) { return; }
        UIElementRareData r = EnsureRare();
        r.ToolTipFactory = factory;
        r.ToolTip = null;
        if (factory != null) { EnableToolTipHover(); }
    }
    private void EnsureToolTipMaterialized() {
        if (_rare == null || _rare.ToolTip != null || _rare.ToolTipFactory == null) { return; }
        var fn = _rare.ToolTipFactory;
        _rare.ToolTipFactory = null;
        var pushed = new ResourceDictionary[0];
        var chain = new UIElement[0];
        UIElement cur = this;
        while (cur != null) {
            chain.Push(cur);
            cur = cur._parent;
        }
        for (int i = chain.Length - 1; i >= 0; i -= 1) {
            UIElement node = chain[i];
            if (node._ambientResources != null) {
                ResourceDictionary.PushAmbient(node._ambientResources);
                pushed.Push(node._ambientResources);
            }
            if (node._resources != null) {
                ResourceDictionary.PushAmbient(node._resources);
                pushed.Push(node._resources);
            }
        }
        UIElement built = fn(DataContext);
        for (int i = pushed.Length - 1; i >= 0; i -= 1) {
            ResourceDictionary.PopAmbient(pushed[i]);
        }
        EnsureRare().ToolTip = built;
    }
    public object ToolTip {
        get { EnsureToolTipMaterialized(); return _rare != null ? DependencyObject.Unbox(_rare.ToolTip) : null; }
        set {
            if (value == null && _rare == null) { return; }
            UIElementRareData r = EnsureRare();
            r.ToolTipFactory = null;
            r.ToolTip = DependencyObject.BoxObject(value);
            if (_toolTipEventHook != null && r.ToolTip != null) { var tth = _toolTipEventHook; tth(this, "ToolTip", r.ToolTip); }
        }
    }
    public DependencyObject ToolTipBox {
        get { EnsureToolTipMaterialized(); return _rare != null ? _rare.ToolTip : null; }
        set {
            if (value == null && _rare == null) { return; }
            UIElementRareData r = EnsureRare();
            r.ToolTipFactory = null;
            r.ToolTip = value;
            if (_toolTipEventHook != null && r.ToolTip != null) { var tth = _toolTipEventHook; tth(this, "ToolTip", r.ToolTip); }
        }
    }
    public void SetToolTip(string text) {
        UIElementRareData r = EnsureRare();
        r.ToolTipFactory = null;
        r.ToolTip = DependencyObject.BoxString(text);
        if (r.ToolTip != null) { EnableToolTipHover(); }
    }

    public UIElement Parent { get { return _parent; } }
    public UIElement TemplatedParent {
        get { return _templatedParent; }
        set { _templatedParent = value; BumpSubtreeAncestorEpoch(); BumpPropEpoch(); }
    }
    internal void SetTemplatedParentDirect(UIElement tp) {
        _templatedParent = tp;
        _ancestorChainEpoch = _ancestorChainEpoch >= 1000000000 ? 1 : (_ancestorChainEpoch + 1);
        BumpPropEpoch();
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

    public virtual void SetDataContext(object dc) { DataContext = dc; }
    public void ClearLocalDataContext() {
        DependencyObject oldBox = _explicitDataContext;
        _explicitDataContext = null;
        BumpPropEpoch();
        PropagateDataContextToSubtree(oldBox);
    }

    private static void PropagateDataContextQueue(UIElement startNode, DependencyObject dcBox, bool includeStart) {
        if (startNode == null) { return; }
        if (!includeStart && (startNode._children == null || startNode._children.Length == 0)) { return; }
        var els = new UIElement[0];
        var dcs = new DependencyObject[0];
        if (includeStart) {
            els.Push(startNode);
            dcs.Push(dcBox);
        } else if (startNode._children != null) {
            for (int i = 0; i < startNode._children.Length; i += 1) {
                els.Push(startNode._children[i]);
                dcs.Push(dcBox);
            }
        }
        int idx = 0;
        while (idx < els.Length) {
            UIElement cur = els[idx];
            DependencyObject parentDc = dcs[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur._explicitDataContext != null) {
                cur._inheritedDataContext = parentDc;
                continue;
            }
            DependencyObject oldBox = cur._inheritedDataContext;
            if (oldBox == parentDc) { continue; }
            cur._inheritedDataContext = parentDc;
            cur.BumpPropEpoch();
            cur.RaiseDataContextChangedBoxes(oldBox, parentDc);
            if (cur._children == null) { continue; }
            DependencyObject eff = cur.EffectiveDataContextBox();
            for (int i = 0; i < cur._children.Length; i += 1) {
                els.Push(cur._children[i]);
                dcs.Push(eff);
            }
        }
    }

    internal void ReceiveInheritedDataContext(DependencyObject parentDc) {
        PropagateDataContextQueue(this, parentDc, true);
    }

    internal DependencyObject EffectiveDataContextBox() {
        return _explicitDataContext != null ? _explicitDataContext : _inheritedDataContext;
    }

    internal DependencyObject InheritedDataContextBox() {
        if (_inheritedDataContext != null) { return _inheritedDataContext; }
        UIElement p = _parent != null ? _parent : _logicalParent;
        return p != null ? p.EffectiveDataContextBox() : null;
    }

    private void PropagateDataContextToSubtree(DependencyObject oldBox) {
        DependencyObject eff = EffectiveDataContextBox();
        RaiseDataContextChangedBoxes(oldBox, eff);
        PropagateDataContextQueue(this, eff, false);
    }

    private void RaiseDataContextChangedBoxes(DependencyObject oldBox, DependencyObject newBox) {
        OnDataContextChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(newBox));
        DataContextChanged(this, DependencyPropertyChangedEventArgs.FromBoxes("DataContext", oldBox, newBox));
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
        set {
            _resources = value;
            ApplyStyleSubtree();
            RefreshDynamicResourcesRecursive();
        }
    }

    public ResourceDictionary AmbientResources {
        get { return _ambientResources; }
        set { _ambientResources = value; }
    }

    public Style Style {
        get { return _style; }
        set {
            if (_style == value && (_appliedStyle == value || _closedPopupBuildDepth > 0)) { return; }
            _style = value;
            _appliedStyle = null;
            if (_closedPopupBuildDepth > 0) { return; }
            ApplyStyleInternal();
        }
    }
    public Style FocusVisualStyle { get; set; }

    public void MarkLocalProperty(string key) {
        if (HasUiFlag(64) || HasUiFlag(256) || key == null || key.Length == 0) { return; }
        if (_localProps == null) { _localProps = new string[0]; }
        for (int i = 0; i < _localProps.Length; i += 1) {
            if (_localProps[i] == key) { return; }
        }
        _localProps.Push(key);
    }

    public bool HasLocalProperty(string normalizedProp) {
        if (_localProps == null || normalizedProp == null) { return false; }
        for (int i = 0; i < _localProps.Length; i += 1) {
            if (_localProps[i] == normalizedProp) { return true; }
        }
        return false;
    }

    public void SetResourceReference(string propName, string resourceKey) {
        if (propName == null || resourceKey == null) { return; }
        if (!HasUiFlag(64) && !HasUiFlag(256)) {
            MarkLocalProperty(propName);
        }
        if (_dynResProps == null) {
            _dynResProps = new string[0];
            _dynResKeys = new string[0];
        }
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

    public string GetResourceReferenceKey(string propName) {
        if (propName == null || _dynResProps == null) { return ""; }
        for (int i = 0; i < _dynResProps.Length; i += 1) {
            if (_dynResProps[i] == propName) { return _dynResKeys[i]; }
        }
        return "";
    }

    public void ClearResourceReference(string propName) {
        if (propName == null || _dynResProps == null) { return; }
        for (int i = _dynResProps.Length - 1; i >= 0; i -= 1) {
            if (_dynResProps[i] == propName) {
                _dynResProps.RemoveAt(i);
                _dynResKeys.RemoveAt(i);
            }
        }
    }

    private void ApplyDynamicResourceAt(int idx) {
        if (_dynResKeys == null || _dynResProps == null || idx < 0 || idx >= _dynResProps.Length) { return; }
        DependencyObject res = FindResourceBox(_dynResKeys[idx]);
        if (res != null) {
            SetPropertyValue(_dynResProps[idx], res);
        }
    }

    public void RefreshDynamicResourcesRecursive() {
        if (_children == null || _children.Length == 0) {
            if (!_isDisposedTree && _dynResProps != null) {
                for (int i = 0; i < _dynResProps.Length; i += 1) {
                    ApplyDynamicResourceAt(i);
                }
            }
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            if (cur._dynResProps != null) {
                for (int i = 0; i < cur._dynResProps.Length; i += 1) {
                    cur.ApplyDynamicResourceAt(i);
                }
            }
            if (cur != this && cur.IsClosedPopupBoundary()) { continue; }
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
    }

    public void ApplyStyleInternal() {
        if (HasUiFlag(64) || _isDisposedTree) { return; }
        SetUiFlag(64, true);
        Style active = _style;
        if (active == null) {
            string tn = GetTypeName();
            if (ResourceDictionary.MayHaveStyleKey(tn)) {
                DependencyObject themeRes = FindResourceBox(tn);
                if (themeRes is Style) { active = themeRes as Style; }
            }
        }
        if (active != null) {
            if (active == _appliedStyle) {
                active.EvaluateTriggers(this);
            } else {
                _appliedStyle = active;
                active.ApplyTo(this);
                OnStyleApplied(active);
            }
        }
        SetUiFlag(64, false);
    }

    public void ApplyStyleSubtree() {
        if (!(this is Control) && (_children == null || _children.Length == 0)) {
            if (!_isDisposedTree) { ApplyStyleInternal(); }
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            UIElement prevTpl = (cur is Control) ? (cur as Control).TemplateRoot() : null;
            cur.ApplyStyleInternal();
            if (cur._isDisposedTree) { continue; }
            UIElement nextTpl = (cur is Control) ? (cur as Control).TemplateRoot() : null;
            if (cur != this && cur.IsClosedPopupBoundary()) { continue; }
            if (cur._children != null) {
                int preCount = cur._children.Length;
                for (int i = 0; i < preCount; i += 1) {
                    UIElement ch = cur._children[i];
                    if (nextTpl != null && nextTpl != prevTpl && ch == nextTpl) { continue; }
                    q.Push(ch);
                }
            }
        }
    }

    public void ApplyTemplateSubtree() {
        if (!(this is Control) && (_children == null || _children.Length == 0)) {
            if (!_isDisposedTree && (_appliedStyle == null || (_style != null && _style != _appliedStyle))) {
                ApplyStyleInternal();
            }
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            if (cur._appliedStyle == null || (cur._style != null && cur._style != cur._appliedStyle)) {
                cur.ApplyStyleInternal();
            }
            if (cur._isDisposedTree) { continue; }
            if (cur is Control) {
                Control ctrl = cur as Control;
                if (!ctrl.IsTemplateApplied()) { ctrl.ApplyTemplate(); }
            }
            if (cur._isDisposedTree) { continue; }
            if (cur != this && cur.IsClosedPopupBoundary()) { continue; }
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
    }

    // ---- Layout & Geometry Properties ----
    public float Width {
        get { return _width; }
        set {
            if (_width == value || (!ParseUtil.IsFinite(_width) && !ParseUtil.IsFinite(value))) { return; }
            _width = value;
            SyncSize();
            NotifyBoundFloatChanged("Width", _width);
        }
    }
    public float Height {
        get { return _height; }
        set {
            if (_height == value || (!ParseUtil.IsFinite(_height) && !ParseUtil.IsFinite(value))) { return; }
            _height = value;
            SyncSize();
            NotifyBoundFloatChanged("Height", _height);
        }
    }
    public float MinWidth {
        get { return _minWidth; }
        set {
            if (_minWidth == value) { return; }
            _minWidth = value;
            SyncMinMaxSize();
            NotifyBoundFloatChanged("MinWidth", _minWidth);
        }
    }
    public float MinHeight {
        get { return _minHeight; }
        set {
            if (_minHeight == value) { return; }
            _minHeight = value;
            SyncMinMaxSize();
            NotifyBoundFloatChanged("MinHeight", _minHeight);
        }
    }
    public float MaxWidth {
        get { return _maxWidth; }
        set {
            if (_maxWidth == value || (!ParseUtil.IsFinite(_maxWidth) && !ParseUtil.IsFinite(value))) { return; }
            _maxWidth = value;
            SyncMinMaxSize();
            NotifyBoundFloatChanged("MaxWidth", _maxWidth);
        }
    }
    public float MaxHeight {
        get { return _maxHeight; }
        set {
            if (_maxHeight == value || (!ParseUtil.IsFinite(_maxHeight) && !ParseUtil.IsFinite(value))) { return; }
            _maxHeight = value;
            SyncMinMaxSize();
            NotifyBoundFloatChanged("MaxHeight", _maxHeight);
        }
    }

    public Thickness Margin {
        get { return _margin; }
        set {
            _margin = value == null ? new Thickness(0.0f) : value;
            SyncMargin();
            NotifyBoundPropertyChanged("Margin", _margin);
        }
    }
    public Thickness Padding {
        get { return _padding; }
        set {
            _padding = value == null ? new Thickness(0.0f) : value;
            SyncPadding();
            NotifyBoundPropertyChanged("Padding", _padding);
        }
    }
    public HorizontalAlignment HorizontalAlignment {
        get { return _hAlign; }
        set {
            _hasExplicitHAlign = true;
            if (_hAlign == value) { return; }
            _hAlign = value;
            SyncAlignment();
            NotifyBoundIntChanged("HorizontalAlignment", EnumConvert.ToInt(_hAlign));
        }
    }
    public VerticalAlignment VerticalAlignment {
        get { return _vAlign; }
        set {
            _hasExplicitVAlign = true;
            if (_vAlign == value) { return; }
            _vAlign = value;
            SyncAlignment();
            NotifyBoundIntChanged("VerticalAlignment", EnumConvert.ToInt(_vAlign));
        }
    }
    public Visibility Visibility {
        get { return _visibility; }
        set {
            if (_visibility == value) { return; }
            bool wasCollapsed = (_visibility == Visibility.Collapsed);
            _visibility = value;
            BumpVisibilityEpoch();
            if (_hasNative) { Visual_SetVisibility(__nativeHandle, EnumConvert.ToInt(value)); }
            OnVisualStatePropertyChanged();
            UpdateTriggersAndStates();
            if (wasCollapsed && _visibility != Visibility.Collapsed && _xamlBuildDepth == 0 &&
                !IsInsideCollapsedSubtree() && !IsInsideClosedPopup()) {
                BindingEngine.FlushSubtree(this);
            }
            NotifyBoundPropertyChanged("Visibility", DependencyObject.BoxInt(EnumConvert.ToInt(_visibility)));
        }
    }
    public float Opacity {
        get { return _opacity; }
        set {
            float clamped = Math.Clampf(value, 0.0f, 1.0f);
            if (_opacity == clamped) { return; }
            _opacity = clamped;
            if (_hasNative) { Visual_SetOpacity(__nativeHandle, _opacity); }
            NotifyBoundPropertyChanged("Opacity", DependencyObject.BoxFloat(_opacity));
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
            NotifyBoundPropertyChanged("IsEnabled", DependencyObject.BoxBool(_isEnabled));
        }
    }
    private bool _effectiveHitTestVisible = true;
    internal static int _xamlBuildDepth = 0;

    public bool IsHitTestVisible {
        get { return _isHitTestVisible; }
        set {
            if (_isHitTestVisible == value) { return; }
            _isHitTestVisible = value;
            SyncInteractiveFlagsSubtree();
            NotifyBoundPropertyChanged("IsHitTestVisible", DependencyObject.BoxBool(_isHitTestVisible));
        }
    }
    public bool EffectiveIsHitTestVisible() {
        return _effectiveHitTestVisible;
    }
    protected virtual bool IsSelfHitTestSolid() {
        if (_visualKind == VisualKind.Text || _visualKind == VisualKind.Image || _visualKind == VisualKind.CanvasDraw) {
            return true;
        }
        if (_background != null) { return true; }
        Thickness bt = _borderThickness;
        return _borderBrush != null && (bt.Left > 0.0f || bt.Top > 0.0f || bt.Right > 0.0f || bt.Bottom > 0.0f);
    }
    public bool Focusable {
        get { return _focusable; }
        set {
            if (_focusable == value) { return; }
            _focusable = value;
            SyncInteractiveFlags();
            NotifyBoundPropertyChanged("Focusable", DependencyObject.BoxBool(_focusable));
        }
    }
    public bool IsFocused { get { return _isFocused; } }
    public bool IsKeyboardFocusWithin { get { return _isKeyboardFocusWithin; } }
    public bool IsMouseOver { get { return _isMouseOver; } }
    public bool IsPressed { get { return _isPressed; } }
    public bool ClipToBounds {
        get { return _clipToBounds; }
        set {
            if (_clipToBounds == value) { return; }
            _clipToBounds = value;
            if (_hasNative) { Visual_SetClipToBounds(__nativeHandle, value); }
            NotifyBoundPropertyChanged("ClipToBounds", DependencyObject.BoxBool(_clipToBounds));
        }
    }
    public int AlternationIndex {
        get { return _alternationIndex; }
    }
    internal void SetAlternationIndex(int idx) {
        if (_alternationIndex == idx) { return; }
        _alternationIndex = idx;
        DependencyObject boxed = DependencyObject.BoxInt(_alternationIndex);
        NotifyBoundPropertyChanged("ItemsControl.AlternationIndex", boxed);
        NotifyBoundPropertyChanged("AlternationIndex", boxed);
    }

    public int ZIndex {
        get { return _zIndex; }
        set {
            if (_zIndex == value) { return; }
            _zIndex = value;
            if (_hasNative) { Visual_SetZIndex(__nativeHandle, value); }
            NotifyBoundPropertyChanged("ZIndex", DependencyObject.BoxInt(_zIndex));
        }
    }

    public Transform RenderTransform {
        get { return _renderTransform; }
        set { _renderTransform = value; SyncTransform2D(); NotifyBoundPropertyChanged("RenderTransform", _renderTransform); }
    }
    public Transform LayoutTransform {
        get { return _layoutTransform; }
        set { _layoutTransform = value; SyncLayoutTransform(); NotifyBoundPropertyChanged("LayoutTransform", _layoutTransform); }
    }
    internal void EnsureLayoutTransformEnabledForAnim() {
        if (!_hasNative || HasUiFlag(1)) { return; }
        if (_layoutTransform != null) {
            SyncLayoutTransform();
        } else {
            SetUiFlag(1, true);
            Visual_SetLayoutTransform(__nativeHandle, 1.0f, 1.0f, 0.0f, true);
        }
    }
    public Point RenderTransformOrigin {
        get { return _renderTransformOrigin; }
        set {
            SetUiFlag(512, value != null);
            _renderTransformOrigin = value == null ? new Point(0.5f, 0.5f) : value;
            SyncTransform2D();
            NotifyBoundPropertyChanged("RenderTransformOrigin", _renderTransformOrigin);
        }
    }

    public float Rotation3DX { get { return _rare != null ? _rare.Rot3DX : 0.0f; } set { if (Rotation3DX == value) { return; } EnsureRare().Rot3DX = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DX", DependencyObject.BoxFloat(value)); } }
    public float Rotation3DY { get { return _rare != null ? _rare.Rot3DY : 0.0f; } set { if (Rotation3DY == value) { return; } EnsureRare().Rot3DY = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DY", DependencyObject.BoxFloat(value)); } }
    public float Rotation3DZ { get { return _rare != null ? _rare.Rot3DZ : 0.0f; } set { if (Rotation3DZ == value) { return; } EnsureRare().Rot3DZ = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DZ", DependencyObject.BoxFloat(value)); } }
    public float Translation3DX { get { return _rare != null ? _rare.Trans3DX : 0.0f; } set { if (Translation3DX == value) { return; } EnsureRare().Trans3DX = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DX", DependencyObject.BoxFloat(value)); } }
    public float Translation3DY { get { return _rare != null ? _rare.Trans3DY : 0.0f; } set { if (Translation3DY == value) { return; } EnsureRare().Trans3DY = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DY", DependencyObject.BoxFloat(value)); } }
    public float Translation3DZ { get { return _rare != null ? _rare.Trans3DZ : 0.0f; } set { if (Translation3DZ == value) { return; } EnsureRare().Trans3DZ = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DZ", DependencyObject.BoxFloat(value)); } }
    public float CameraDistance { get { return _rare != null ? _rare.CamDist : 0.0f; } set { if (CameraDistance == value) { return; } EnsureRare().CamDist = value; SyncTransform3D(); NotifyBoundPropertyChanged("CameraDistance", DependencyObject.BoxFloat(value)); } }

    private EffectChangeHook _effectHook;

    public Effect Effect {
        get { return _effect; }
        set {
            if (_effect == value) { return; }
            if (_effect != null && _effectHook != null) {
                _effect.RemoveChangedListener(_effectHook);
                _effectHook = null;
            }
            _effect = value;
            if (_effect != null) {
                Effect effRef = _effect;
                _effectHook = _effect.AddChangedListener(() => {
                    if (_effect == effRef && !_isDisposedTree) { SyncEffect(); }
                });
            }
            SyncEffect();
            NotifyBoundPropertyChanged("Effect", _effect);
        }
    }
    public Brush OpacityMask {
        get { return _opacityMask; }
        set { if (_opacityMask == value) { return; } _opacityMask = value; SyncOpacityMask(); NotifyBoundPropertyChanged("OpacityMask", _opacityMask); }
    }
    public BlendingMode BlendMode {
        get { return _blendMode; }
        set {
            if (_blendMode == value) { return; }
            _blendMode = value;
            int bm = value == BlendingMode.Multiply ? 1 : (value == BlendingMode.Screen ? 2 : (value == BlendingMode.Additive ? 3 : 0));
            if (_hasNative) { Visual_SetBlendMode(__nativeHandle, bm); }
            DependencyObject boxedBm = DependencyObject.BoxInt(bm);
            NotifyBoundPropertyChanged("BlendMode", boxedBm);
            NotifyBoundPropertyChanged("Element.BlendingMode", boxedBm);
        }
    }
    public PlaneProjection Projection3D {
        get { return _rare != null ? _rare.Projection3D : null; }
        set {
            if (value == null && _rare == null) { return; }
            UIElementRareData r = EnsureRare();
            r.Projection3D = value;
            BumpPropEpoch();
            if (value != null) {
                r.Rot3DX = value.RotationX;
                r.Rot3DY = value.RotationY;
                r.Rot3DZ = value.RotationZ;
                r.Trans3DX = value.LocalOffsetX + value.GlobalOffsetX;
                r.Trans3DY = value.LocalOffsetY + value.GlobalOffsetY;
                r.Trans3DZ = value.LocalOffsetZ + value.GlobalOffsetZ;
                r.Scale3DX = 1.0f;
                r.Scale3DY = 1.0f;
                r.Scale3DZ = 1.0f;
                SetUiFlag(2, false);
                SyncTransform3D();
            }
        }
    }
    public CompositeTransform3D Transform3D {
        get { return _rare != null ? _rare.Transform3D : null; }
        set {
            if (value == null && _rare == null) { return; }
            UIElementRareData r = EnsureRare();
            r.Transform3D = value;
            BumpPropEpoch();
            if (value != null) {
                r.Rot3DX = value.RotationX;
                r.Rot3DY = value.RotationY;
                r.Rot3DZ = value.RotationZ;
                r.Trans3DX = value.TranslateX;
                r.Trans3DY = value.TranslateY;
                r.Trans3DZ = value.TranslateZ;
                r.Scale3DX = value.ScaleX;
                r.Scale3DY = value.ScaleY;
                r.Scale3DZ = value.ScaleZ;
                r.Center3DX = value.CenterX;
                r.Center3DY = value.CenterY;
                r.Center3DZ = value.CenterZ;
                SetUiFlag(2, true);
                SyncTransform3D();
            }
        }
    }

    public void SetExplicitTextFontSize(float sz) {
        _explicitFontSize = sz > 0.0f ? sz : 0.0f;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public float EffectiveTextFontSizeOrZero() {
        return _explicitFontSize > 0.0f ? _explicitFontSize : _inheritedFontSize;
    }
    public void SetExplicitTextFontFamily(string ff) {
        _explicitFontFamily = ff == null ? "" : ff;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public string EffectiveTextFontFamilyOrEmpty() {
        if (_explicitFontFamily != null && _explicitFontFamily.Length > 0) { return _explicitFontFamily; }
        return _inheritedFontFamily == null ? "" : _inheritedFontFamily;
    }
    public void SetExplicitTextFontWeight(string fw) {
        _explicitFontWeight = fw == null ? "" : fw;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public string EffectiveTextFontWeightOrEmpty() {
        if (_explicitFontWeight != null && _explicitFontWeight.Length > 0) { return _explicitFontWeight; }
        return _inheritedFontWeight == null ? "" : _inheritedFontWeight;
    }
    public void SetExplicitTextForeground(Brush fg) {
        _explicitTextForeground = fg;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public Brush EffectiveTextForeground() {
        return _explicitTextForeground != null ? _explicitTextForeground : _inheritedTextForeground;
    }

    internal virtual void OnInheritedTypographyChanged() {}

    private static void PropagateTypographyQueue(
        UIElement startNode, float startFs, string startFf, string startFw, Brush startFg, bool includeStart) {
        if (startNode == null) { return; }
        if (!includeStart && (startNode._children == null || startNode._children.Length == 0)) { return; }
        var els = new UIElement[0];
        var fss = new float[0];
        var ffs = new string[0];
        var fws = new string[0];
        var fgs = new Brush[0];
        if (includeStart) {
            els.Push(startNode);
            fss.Push(startFs);
            ffs.Push(startFf == null ? "" : startFf);
            fws.Push(startFw == null ? "" : startFw);
            fgs.Push(startFg);
        } else if (startNode._children != null) {
            for (int i = 0; i < startNode._children.Length; i += 1) {
                els.Push(startNode._children[i]);
                fss.Push(startFs);
                ffs.Push(startFf == null ? "" : startFf);
                fws.Push(startFw == null ? "" : startFw);
                fgs.Push(startFg);
            }
        }
        int idx = 0;
        while (idx < els.Length) {
            UIElement cur = els[idx];
            float cFs = fss[idx];
            string cFf = ffs[idx];
            string cFw = fws[idx];
            Brush cFg = fgs[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur._inheritedFontSize == cFs &&
                cur._inheritedFontFamily == cFf &&
                cur._inheritedFontWeight == cFw &&
                cur._inheritedTextForeground == cFg) {
                continue;
            }
            cur._inheritedFontSize = cFs;
            cur._inheritedFontFamily = cFf;
            cur._inheritedFontWeight = cFw;
            cur._inheritedTextForeground = cFg;
            cur.BumpPropEpoch();
            cur.OnInheritedTypographyChanged();
            if (cur._children == null || cur._children.Length == 0) { continue; }
            float outFs = cur.EffectiveTextFontSizeOrZero();
            string outFf = cur.EffectiveTextFontFamilyOrEmpty();
            string outFw = cur.EffectiveTextFontWeightOrEmpty();
            Brush outFg = cur.EffectiveTextForeground();
            for (int i = 0; i < cur._children.Length; i += 1) {
                els.Push(cur._children[i]);
                fss.Push(outFs);
                ffs.Push(outFf);
                fws.Push(outFw);
                fgs.Push(outFg);
            }
        }
    }

    internal void ReceiveInheritedTypography(float fs, string ff, string fw, Brush fg) {
        PropagateTypographyQueue(this, fs, ff, fw, fg, true);
    }

    protected void PropagateTypographyToSubtree() {
        OnInheritedTypographyChanged();
        if (_children == null || _children.Length == 0) { return; }
        PropagateTypographyQueue(
            this,
            EffectiveTextFontSizeOrZero(),
            EffectiveTextFontFamilyOrEmpty(),
            EffectiveTextFontWeightOrEmpty(),
            EffectiveTextForeground(),
            false);
    }

    public void AddInputBinding(KeyBinding kb) {
        if (kb != null) {
            UIElementRareData r = EnsureRare();
            if (r.InputBindings == null) { r.InputBindings = new KeyBinding[0]; }
            r.InputBindings.Push(kb);
        }
    }
    public void AddKeyBinding(KeyBinding kb) { AddInputBinding(kb); }
    public KeyBinding[] InputBindings() {
        return (_rare != null && _rare.InputBindings != null) ? _rare.InputBindings : new KeyBinding[0];
    }

    internal bool TryHandleInputBindings(KeyEventArgs e) {
        if (e == null || e.Handled || _rare == null || _rare.InputBindings == null || _rare.InputBindings.Length == 0) {
            return false;
        }
        int evMods = e.Modifiers % 8;
        KeyBinding[] ibs = _rare.InputBindings;
        for (int i = 0; i < ibs.Length; i += 1) {
            KeyBinding kb = ibs[i];
            if (kb.Key == e.Key && (kb.Modifiers % 8) == evMods) {
                ICommand cmd = kb.Command;
                if (cmd == null && kb.CommandPath.Length > 0) {
                    object dc = DataContext;
                    if (dc != null) {
                        DependencyObject box = BindingEngine.ReadPathBox(dc, kb.CommandPath);
                        cmd = DependencyObject.Unbox(box) as ICommand;
                    }
                }
                if (cmd != null) {
                    object paramObj = kb.CommandParameter;
                    if (paramObj == null && kb.CommandParameterPath.Length > 0 && DataContext != null) {
                        paramObj = DependencyObject.Unbox(BindingEngine.ReadPathBox(DataContext, kb.CommandParameterPath));
                    }
                    if (cmd.CanExecute(paramObj)) {
                        cmd.Execute(paramObj);
                        e.Handled = true;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    public Brush Background {
        get { return _background; }
        set {
            _background = value;
            SetUiFlag(4, false);
            SetUiFlag(8, false);
            _bgOpacity = value != null ? Math.Clampf(value.Opacity, 0.0f, 1.0f) : 1.0f;
            OnBoxAppearanceChanged();
            NotifyBoundPropertyChanged("Background", _background);
            if (_ownBindingsBox != null) {
                BindingEngine.NotifyTargetPropertyChanged(this, "Background.Color", DependencyObject.BoxString(_background != null ? ParseUtil.FormatArgbHex(_background.ResolveRgba()) : "#00000000"));
            }
        }
    }
    public Brush BorderBrush {
        get { return _borderBrush; }
        set { _borderBrush = value; OnBoxAppearanceChanged(); NotifyBoundPropertyChanged("BorderBrush", _borderBrush); }
    }
    public Thickness BorderThickness {
        get { return _borderThickness; }
        set { _borderThickness = value == null ? new Thickness(0.0f) : value; OnBoxAppearanceChanged(); NotifyBoundPropertyChanged("BorderThickness", _borderThickness); }
    }
    public CornerRadius CornerRadius {
        get { return _cornerRadius; }
        set { _cornerRadius = value == null ? new CornerRadius(0.0f) : value; OnBoxAppearanceChanged(); NotifyBoundPropertyChanged("CornerRadius", _cornerRadius); }
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
    public float ViewportX { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 8) : 0.0f; } }
    public float ViewportY { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 9) : 0.0f; } }
    public float ScreenBoundsWidth { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 10) : 0.0f; } }
    public float ScreenBoundsHeight { get { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 11) : 0.0f; } }
    protected virtual float GetLogicalViewportWidth() { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 10) : 0.0f; }
    protected virtual float GetLogicalViewportHeight() { return _hasNative ? Visual_GetLayoutRect(__nativeHandle, 11) : 0.0f; }
    public float ViewportWidth { get { return GetLogicalViewportWidth(); } }
    public float ViewportHeight { get { return GetLogicalViewportHeight(); } }

    protected void SyncSize() {
        if (_hasNative) { Visual_SetSize(__nativeHandle, _width, _height); }
    }
    protected void SyncMinMaxSize() {
        if (_hasNative) { Visual_SetMinMaxSize(__nativeHandle, _minWidth, _minHeight, _maxWidth, _maxHeight); }
    }
    protected void SyncMargin() {
        if (_hasNative) { Visual_SetMargin(__nativeHandle, _margin.Left, _margin.Top, _margin.Right, _margin.Bottom); }
    }
    protected virtual void SyncPadding() {
        if (_hasNative) { Visual_SetPadding(__nativeHandle, _padding.Left, _padding.Top, _padding.Right, _padding.Bottom); }
    }
    public string EffectiveFlowDirection() {
        UIElement cur = this;
        int guard = 0;
        while (cur != null && guard < 256) {
            guard += 1;
            if (cur.HasUiFlag(16)) { return cur._flowDirection; }
            cur = cur.Parent;
        }
        return _flowDirection;
    }

    public string FlowDirection {
        get { return EffectiveFlowDirection(); }
        set {
            string fd = (value == null || value.Trim().Length == 0) ? "LeftToRight" : value.Trim();
            SetUiFlag(16, true);
            if (_flowDirection != fd) {
                _flowDirection = fd;
            }
            SyncAlignment();
            UpdateLayout();
        }
    }

    protected void SyncAlignment() {
        if (_hasNative) {
            int hEnc = EnumConvert.ToInt(_hAlign);
            if (HasUiFlag(16)) {
                hEnc = hEnc + 256;
                if (_flowDirection == "RightToLeft") { hEnc = hEnc + 512; }
            }
            Visual_SetAlignment(__nativeHandle, hEnc, EnumConvert.ToInt(_vAlign));
        }
    }
    protected void SyncInteractiveFlags() {
        bool parentEff = _parent == null ? true : _parent._effectiveHitTestVisible;
        _effectiveHitTestVisible = _isHitTestVisible && parentEff;
        if (_hasNative) {
            bool effHit = _effectiveHitTestVisible && IsSelfHitTestSolid();
            Visual_SetInteractiveFlags(__nativeHandle, _isEnabled, effHit, _focusable);
        }
    }
    public void SyncInteractiveFlagsSubtree() {
        if (_children == null || _children.Length == 0) {
            SyncInteractiveFlags();
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            cur.SyncInteractiveFlags();
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
    }
    public void SyncTransform2D() {
        if (!_hasNative) { return; }
        if (_renderTransform == null) {
            Visual_SetTransform2D(
                __nativeHandle,
                0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                _renderTransformOrigin.X,
                _renderTransformOrigin.Y);
            return;
        }
        TransformData td = TransformData.SharedScratch();
        _renderTransform.Accumulate(td);
        bool useZeroOrig = (td.HasExplicitCenter && !HasUiFlag(512));
        Visual_SetTransform2D(
            __nativeHandle,
            td.TX, td.TY, td.SX, td.SY, td.RotDeg, td.SkewX, td.SkewY,
            useZeroOrig ? 0.0f : _renderTransformOrigin.X,
            useZeroOrig ? 0.0f : _renderTransformOrigin.Y);
    }
    public void SyncLayoutTransform() {
        if (!_hasNative) { return; }
        if (_layoutTransform == null) {
            SetUiFlag(1, false);
            Visual_SetLayoutTransform(__nativeHandle, 1.0f, 1.0f, 0.0f, false);
            return;
        }
        SetUiFlag(1, true);
        TransformData td = TransformData.SharedScratch();
        _layoutTransform.Accumulate(td);
        Visual_SetLayoutTransform(__nativeHandle, td.SX, td.SY, td.RotDeg, true);
    }
    public void SyncTransform3D() {
        if (!_hasNative) { return; }
        if (_rare == null) {
            Visual_SetTransform3DEx(__nativeHandle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false);
            return;
        }
        Visual_SetTransform3DEx(
            __nativeHandle,
            _rare.Rot3DX, _rare.Rot3DY, _rare.Rot3DZ,
            _rare.Trans3DX, _rare.Trans3DY, _rare.Trans3DZ,
            _rare.Scale3DX, _rare.Scale3DY, _rare.Scale3DZ,
            _rare.Center3DX, _rare.Center3DY, _rare.Center3DZ,
            _rare.CamDist, HasUiFlag(2));
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
    public void SyncOpacityMask() {
        if (!_hasNative) { return; }
        if (_opacityMask == null) {
            Visual_SetOpacityMask(__nativeHandle, false, -1, 4294967295u, 1.0f);
            return;
        }
        int gradId = _opacityMask.ResolveGradIdUnscaled();
        uint solidRgba = _opacityMask.ResolveRgbaUnscaled();
        float op = Math.Clampf(_opacityMask.Opacity, 0.0f, 1.0f);
        Visual_SetOpacityMask(__nativeHandle, true, gradId, solidRgba, op);
    }

    protected virtual void OnBoxAppearanceChanged() {
        SyncBoxStyleToNative();
        SyncInteractiveFlags();
    }

    internal static void ApplyImageBrushViewbox(handle h, string viewbox) {
        float vx = 0.0f;
        float vy = 0.0f;
        float vw = 1.0f;
        float vh = 1.0f;
        if (viewbox != null && viewbox.Length > 0) {
            string norm = viewbox.Trim().Replace(",", " ");
            string[] rawParts = norm.Split(" ");
            var vals = new float[0];
            for (int i = 0; i < rawParts.Length; i += 1) {
                string tok = rawParts[i].Trim();
                if (tok.Length > 0) {
                    vals.Push(ParseUtil.ParseFloat(tok, vals.Length >= 2 ? 1.0f : 0.0f));
                }
            }
            if (vals.Length >= 4) {
                vx = vals[0];
                vy = vals[1];
                vw = vals[2];
                vh = vals[3];
            }
        }
        Visual_SetImageViewbox(h, vx, vy, vw, vh);
    }

    internal static void ApplyImageBrushViewport(handle h, string viewport, string viewportUnits) {
        if (viewport == null || viewport.Trim().Length == 0) {
            Visual_SetImageViewport(h, 0.0f, 0.0f, 1.0f, 1.0f, false, false);
            return;
        }
        float vpx = 0.0f;
        float vpy = 0.0f;
        float vpw = 1.0f;
        float vph = 1.0f;
        string norm = viewport.Trim().Replace(",", " ");
        string[] rawParts = norm.Split(" ");
        var vals = new float[0];
        for (int i = 0; i < rawParts.Length; i += 1) {
            string tok = rawParts[i].Trim();
            if (tok.Length > 0) {
                vals.Push(ParseUtil.ParseFloat(tok, vals.Length >= 2 ? 1.0f : 0.0f));
            }
        }
        if (vals.Length >= 4) {
            vpx = vals[0];
            vpy = vals[1];
            vpw = vals[2];
            vph = vals[3];
        }
        bool isAbs = (viewportUnits != null && viewportUnits.Trim().ToLower() == "absolute");
        Visual_SetImageViewport(h, vpx, vpy, vpw, vph, true, isAbs);
    }

    internal static void ClearImageBrushFromNative(handle h) {
        Visual_SetImage(h, "", 0, 2, 4294967295u);
        Visual_SetImageViewbox(h, 0.0f, 0.0f, 1.0f, 1.0f);
        Visual_SetImageViewport(h, 0.0f, 0.0f, 1.0f, 1.0f, false, false);
        Visual_SetVisualBrushSource(h, ParseUtil.EmptyHandle(), 1, 1);
        Visual_SetImageTile(h, false);
    }

    protected virtual void SyncBoxStyleToNative() {
        if (!_hasNative) { return; }
        uint bgCol = 0u;
        int bgGrad = -1;
        float bgOp = 1.0f;
        if (_background != null && _background.ApplySurfaceToNative(this, __nativeHandle, true)) {
            SetUiFlag(128, true);
        } else {
            if (HasUiFlag(128)) {
                ClearImageBrushFromNative(__nativeHandle);
                SetUiFlag(128, false);
            }
            if (_background != null) {
                bgCol = _background.ResolveRgbaUnscaled();
                bgGrad = _background.ResolveGradIdUnscaled();
                bgOp = _bgOpacity;
            }
        }
        uint bdCol = _borderBrush != null ? _borderBrush.ResolveRgba() : 0u;
        int bdGrad = _borderBrush != null ? _borderBrush.ResolveGradId() : -1;
        Visual_SetBoxStyle(
            __nativeHandle,
            bgCol, bdCol,
            _borderThickness.Left, _borderThickness.Top, _borderThickness.Right, _borderThickness.Bottom,
            _cornerRadius.TopLeft, _cornerRadius.TopRight, _cornerRadius.BottomRight, _cornerRadius.BottomLeft);
        Visual_SetBoxGradients(__nativeHandle, bgGrad, bdGrad);
        Visual_SetBoxBrushOpacity(__nativeHandle, bgOp);
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
    public int ChildCount() { return _children == null ? 0 : _children.Length; }
    public UIElement ChildAt(int index) {
        if (_children == null || index < 0 || index >= _children.Length) { return null; }
        return _children[index];
    }
    public UIElement[] Children() { return _children != null ? _children : new UIElement[0]; }

    public virtual void AddChild(UIElement child) {
        if (child == null || child == this || child.IsAncestorOf(this)) { return; }
        if (child._parent != null) { child._parent.RemoveChild(child); }
        child._parent = this; child.BumpSubtreeAncestorEpoch();
        if (_children == null) { _children = new UIElement[0]; }
        _children.Push(child);
        if (_hasNative && child._hasNative) {
            Visual_AddChild(__nativeHandle, child.__nativeHandle);
        }
        OnChildAttached(child);
    }

    public virtual void InsertChild(int index, UIElement child) {
        if (child == null || child == this || child.IsAncestorOf(this)) { return; }
        if (child._parent != null) { child._parent.RemoveChild(child); }
        if (_children == null) { _children = new UIElement[0]; }
        int idx = index < 0 ? 0 : (index > _children.Length ? _children.Length : index);
        child._parent = this; child.BumpSubtreeAncestorEpoch();
        _children.Insert(idx, child);
        if (_hasNative && child._hasNative) {
            Visual_InsertChild(__nativeHandle, idx, child.__nativeHandle);
        }
        OnChildAttached(child);
    }

    protected void OnChildAttached(UIElement child) {
        bool wantEffHit = child._isHitTestVisible && _effectiveHitTestVisible;
        if (child._effectiveHitTestVisible != wantEffHit) {
            child.SyncInteractiveFlagsSubtree();
        }
        if ((_xamlBuildDepth > 0 && !HasUiFlag(32)) || _suppressChildAutoStyle || _closedPopupBuildDepth > 0 || IsClosedPopupBoundary()) {
            DependencyObject effDc = EffectiveDataContextBox();
            if (effDc != null) { child.ReceiveInheritedDataContext(effDc); }
            float eFs = EffectiveTextFontSizeOrZero();
            string eFf = EffectiveTextFontFamilyOrEmpty();
            string eFw = EffectiveTextFontWeightOrEmpty();
            Brush eFg = EffectiveTextForeground();
            if (eFs > 0.0f || eFf.Length > 0 || eFw.Length > 0 || eFg != null) {
                child.ReceiveInheritedTypography(eFs, eFf, eFw, eFg);
            }
            return;
        }
        if (_parent == null && !HasUiFlag(32) && _resources == null && (_rare == null || _rare.LocResources == null) &&
            _explicitDataContext == null && _inheritedDataContext == null &&
            _explicitFontSize <= 0.0f && _explicitFontFamily.Length == 0 &&
            _explicitFontWeight.Length == 0 && _explicitTextForeground == null) {
            return;
        }
        child.ReceiveInheritedDataContext(EffectiveDataContextBox());
        child.ReceiveInheritedTypography(
            EffectiveTextFontSizeOrZero(),
            EffectiveTextFontFamilyOrEmpty(),
            EffectiveTextFontWeightOrEmpty(),
            EffectiveTextForeground());
        child.ApplyStyleSubtree();
        child.RefreshDynamicResourcesRecursive();
        if (HasUiFlag(32) && !_suppressChildAutoLoaded) {
            child.EnsureLoadedSubtree();
        }
    }

    public virtual void RemoveChild(UIElement child) {
        if (child == null || _children == null) { return; }
        int idx = -1;
        for (int i = 0; i < _children.Length; i += 1) {
            if (_children[i] == child) { idx = i; break; }
        }
        if (idx < 0) { return; }
        _children.RemoveAt(idx);
        child._parent = null; child.BumpSubtreeAncestorEpoch();
        if (child._effectiveHitTestVisible != child._isHitTestVisible) {
            child.SyncInteractiveFlagsSubtree();
        }
        if (_hasNative && child._hasNative) {
            Visual_RemoveChild(__nativeHandle, child.__nativeHandle);
        }
        child.NotifyUnloadedSubtree();
    }

    public virtual void ClearChildren() {
        if (_children != null) {
            for (int i = 0; i < _children.Length; i += 1) {
                UIElement ch = _children[i];
                ch._parent = null; ch.BumpSubtreeAncestorEpoch();
                if (ch._effectiveHitTestVisible != ch._isHitTestVisible) {
                    ch.SyncInteractiveFlagsSubtree();
                }
                ch.NotifyUnloadedSubtree();
            }
            _children.Clear();
        }
        if (_hasNative) {
            Visual_ClearChildren(__nativeHandle);
        }
    }

    internal bool _suppressChildAutoLoaded = false;
    internal bool _suppressChildAutoStyle = false;
    internal bool _loadingSelf = false;
    private int _keyboardTabIndex = 0;
    public int TabIndex {
        get { return _keyboardTabIndex; }
        set { _keyboardTabIndex = value; }
    }

    internal bool HasLoadedFired() { return HasUiFlag(32); }

    private void EnsureLoadedSelf() {
        if (_isDisposedTree) { return; }
        if (!HasUiFlag(32)) {
            SetUiFlag(32, true);
            _loadingSelf = true;
            if (_appliedStyle == null || (_style != null && _style != _appliedStyle)) {
                ApplyStyleInternal();
            } else {
                _appliedStyle.EvaluateTriggers(this);
            }
            OnLoaded();
            _loadingSelf = false;
            Loaded(this, new RoutedEventArgs("Loaded", this));
        }
    }

    private void NotifyUnloadedSelf() {
        if (HasUiFlag(32)) {
            SetUiFlag(32, false);
            Unloaded(this, new RoutedEventArgs("Unloaded", this));
        }
    }

    public void EnsureLoadedSubtree() {
        if (_isDisposedTree) { return; }
        EnsureLoadedSelf();
        if (_children == null || _children.Length == 0) { return; }
        var q = new UIElement[0];
        int initialCount = _children.Length;
        for (int i = 0; i < initialCount; i += 1) {
            q.Push(_children[i]);
        }
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            cur.EnsureLoadedSelf();
            if (cur.IsClosedPopupBoundary()) { continue; }
            if (cur._children != null) {
                int postCount = cur._children.Length;
                for (int i = 0; i < postCount; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
    }

    protected void NotifyUnloadedSubtree() {
        if (_children == null || _children.Length == 0) {
            NotifyUnloadedSelf();
            return;
        }
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            cur.NotifyUnloadedSelf();
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
    }

    internal void RegisterDetachedLogicalChild(UIElement child) {
        if (child == null) { return; }
        if (_detachedLogicalChildren == null) { _detachedLogicalChildren = new UIElement[0]; }
        for (int i = 0; i < _detachedLogicalChildren.Length; i += 1) {
            if (_detachedLogicalChildren[i] == child) { return; }
        }
        _detachedLogicalChildren.Push(child);
    }

    internal void UnregisterDetachedLogicalChild(UIElement child) {
        if (child == null || _detachedLogicalChildren == null) { return; }
        for (int i = _detachedLogicalChildren.Length - 1; i >= 0; i -= 1) {
            if (_detachedLogicalChildren[i] == child) {
                _detachedLogicalChildren.RemoveAt(i);
            }
        }
    }

    private static Action[] _disposeHooks;

    internal static void RegisterDisposeHook(Action hook) {
        if (hook == null) { return; }
        if (_disposeHooks == null) { _disposeHooks = new Action[0]; }
        _disposeHooks.Push(hook);
    }

    protected virtual void CollectExtraDisposeTargets(UIElement[] q) {}

    public void DisposeSubtree() {
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            cur._isDisposedTree = true;
            _disposeEpoch = _disposeEpoch >= 1000000000 ? 1 : (_disposeEpoch + 1);
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
            if (cur._detachedLogicalChildren != null) {
                for (int i = 0; i < cur._detachedLogicalChildren.Length; i += 1) {
                    q.Push(cur._detachedLogicalChildren[i]);
                }
            }
            if (cur._rare != null) {
                UIElement cmEl = cur._rare.ContextMenuEl;
                if (cmEl != null) {
                    if (!cmEl.HasUiFlag(4096)) {
                        q.Push(cmEl);
                    } else {
                        UIElement pt = cmEl.GetPropertyValue("PlacementTarget") as UIElement;
                        if (pt == cur) {
                            cmEl.SetPropertyValue("IsOpen", DependencyObject.BoxBool(false));
                        }
                    }
                }
                if (cur._rare.ToolTip is UIElement) {
                    q.Push(cur._rare.ToolTip as UIElement);
                }
            }
            if (cur._resources != null && cur._resources.Count() > 0) {
                string[] rKeys = cur._resources.Keys();
                for (int ri = 0; ri < rKeys.Length; ri += 1) {
                    DependencyObject rv = cur._resources.TryGetLocalBox(rKeys[ri]);
                    if (rv is UIElement) {
                        UIElement rel = rv as UIElement;
                        if (!rel._isDisposedTree && (rel._parent == null || rel._parent == cur || rel.DisposeWhenResourceOwned())) {
                            q.Push(rel);
                        }
                    } else if (rv is Style) {
                        Setter[] stSetters = (rv as Style).Setters();
                        if (stSetters != null) {
                            for (int si = 0; si < stSetters.Length; si += 1) {
                                Setter stSetter = stSetters[si];
                                if (stSetter != null && stSetter.ValueBox is UIElement) {
                                    UIElement sel = stSetter.ValueBox as UIElement;
                                    if (!sel._isDisposedTree && (sel._parent == null || sel._parent == cur || sel.DisposeWhenResourceOwned())) {
                                        q.Push(sel);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            cur.CollectExtraDisposeTargets(q);
            if (cur._effect != null && cur._effectHook != null) {
                cur._effect.RemoveChangedListener(cur._effectHook);
                cur._effectHook = null;
            }
            cur._ownBindingsBox = null;
            cur._stateInvalidatedHooks = null;
            cur._routedEventNames = null;
            cur._routedEventSubs = null;
            if (cur._hasNative && ParseUtil.IsValidHandle(cur.__nativeHandle)) {
                cur._hasNative = false;
                Visual_Destroy(cur.__nativeHandle);
                cur.__nativeHandle = ParseUtil.EmptyHandle();
            }
            cur._parent = null;
            cur._templatedParent = null;
            cur._children = null;
            cur._detachedLogicalChildren = null;
            cur._rare = null;
            cur._resources = null;
            cur._ambientResources = null;
            cur._style = null;
            cur._appliedStyle = null;
            cur._hasTemplateResources = false;
            cur._hasTemplateAmbientResources = false;
            cur._explicitDataContext = null;
            cur._inheritedDataContext = null;
            cur._effect = null;
            cur._background = null;
            cur._borderBrush = null;
            cur._renderTransform = null;
            cur._layoutTransform = null;
            cur._opacityMask = null;
            cur._dynResProps = null;
            cur._dynResKeys = null;
            cur._logicalParent = null;
            cur._localProps = null;
            cur._activeTriggerProps = null;

        }
        InputDispatcher.CleanupDisposed();
        if (_disposeHooks != null) {
            for (int i = 0; i < _disposeHooks.Length; i += 1) {
                var fn = _disposeHooks[i];
                if (fn != null) { fn(); }
            }
        }
    }

    internal void MarkDisposedSubtree() {
        DisposeSubtree();
    }

    public UIElement FindName(string name) {
        if (name == null || name.Length == 0) { return null; }
        UIElement rootTp = this._templatedParent;
        for (int pass = 0; pass < 2; pass += 1) {
            bool scoped = (pass == 0);
            var stack = new UIElement[0];
            stack.Push(this);
            while (stack.Length > 0) {
                int last = stack.Length - 1;
                UIElement cur = stack[last];
                stack.RemoveAt(last);
                if (cur == null) { continue; }
                if (scoped && cur != this) {
                    if (cur._templatedParent == rootTp && cur._name == name) { return cur; }
                    if (cur.IsNamescopeBoundary()) { continue; }
                    if (cur._templatedParent != rootTp && cur is Control && !(cur is ContentControl) && !(cur is ItemsControl)) { continue; }
                } else {
                    if (cur._name == name) { return cur; }
                }
                if (cur._detachedLogicalChildren != null) {
                    for (int i = cur._detachedLogicalChildren.Length - 1; i >= 0; i -= 1) {
                        stack.Push(cur._detachedLogicalChildren[i]);
                    }
                }
                if (cur._children != null) {
                    for (int i = cur._children.Length - 1; i >= 0; i -= 1) {
                        stack.Push(cur._children[i]);
                    }
                }
            }
        }
        return null;
    }

    internal UIElement FindNameExcluding(string name, UIElement skipNode) {
        if (name == null || name.Length == 0) { return null; }
        var stack = new UIElement[0];
        stack.Push(this);
        while (stack.Length > 0) {
            int last = stack.Length - 1;
            UIElement cur = stack[last];
            stack.RemoveAt(last);
            if (cur == null || cur == skipNode) { continue; }
            if (cur._name == name) { return cur; }
            if (cur._detachedLogicalChildren != null) {
                for (int i = cur._detachedLogicalChildren.Length - 1; i >= 0; i -= 1) {
                    stack.Push(cur._detachedLogicalChildren[i]);
                }
            }
            if (cur._children != null) {
                for (int i = cur._children.Length - 1; i >= 0; i -= 1) {
                    stack.Push(cur._children[i]);
                }
            }
        }
        return null;
    }

    internal UIElement FindTemplateChildName(string name, UIElement owner) {
        if (name == null || name.Length == 0) { return null; }
        UIElement fallback = null;
        var q = new UIElement[0];
        q.Push(this);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur._name == name) {
                if (cur.TemplatedParent == owner) { return cur; }
                if (cur.TemplatedParent == null && fallback == null) { fallback = cur; }
            }
            if ((cur is ContentPresenter || cur.IsItemsPresenterBoundary()) && cur.TemplatedParent == owner) { continue; }
            if (cur.TemplatedParent != null && !cur.IsInTemplateOf(owner)) { continue; }
            if (cur._detachedLogicalChildren != null) {
                for (int d = 0; d < cur._detachedLogicalChildren.Length; d += 1) {
                    q.Push(cur._detachedLogicalChildren[d]);
                }
            }
            if (cur._children != null) {
                for (int i = 0; i < cur._children.Length; i += 1) {
                    q.Push(cur._children[i]);
                }
            }
        }
        return fallback;
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

    protected DependencyObject TryFindLocalResourceBox(string key) {
        return _resources != null ? _resources.TryGetLocalBox(key) : null;
    }

    protected virtual DependencyObject TryFindTemplateScopedResourceBox(string key) { return null; }
    protected virtual DependencyObject TryFindTemplateAmbientResourceBox(string key) { return null; }

    protected DependencyObject TryFindStyleScopedResourceBox(string key) {
        Style activeStyle = _style != null ? _style : _appliedStyle;
        return activeStyle != null ? activeStyle.TryFindStyleResourceBox(key) : null;
    }

    internal bool IsInTemplateOf(UIElement owner) {
        if (owner == null) { return false; }
        UIElement tp = _templatedParent;
        int hops = 0;
        while (tp != null && hops < 8) {
            if (tp == owner) { return true; }
            tp = tp._templatedParent;
            hops += 1;
        }
        return false;
    }

    public virtual DependencyObject FindResourceBox(string key) {
        if (key == null || key.Length == 0) { return null; }
        DependencyObject firstElemAmb = null;
        DependencyObject firstTplAmb = null;
        UIElement tpTarget = _templatedParent;
        bool sawTpTarget = (tpTarget == null);
        UIElement lastVisited = null;
        UIElement cur = this;
        int walkGuard = 0;
        while (cur != null && walkGuard < 256) {
            walkGuard += 1;
            lastVisited = cur;
            if (cur == tpTarget) { sawTpTarget = true; }
            if (cur._rare != null && cur._rare.LocResources != null) {
                DependencyObject locB = cur._rare.LocResources.TryGetLocalBox(key);
                if (locB != null) { return locB; }
            }
            if (cur._resources != null) {
                DependencyObject resB = cur._resources.TryGetLocalBox(key);
                if (resB != null) { return resB; }
            }
            if (cur._hasTemplateResources) {
                DependencyObject tplScoped = cur.TryFindTemplateScopedResourceBox(key);
                if (tplScoped != null) { return tplScoped; }
            }
            Style curSt = cur._style != null ? cur._style : cur._appliedStyle;
            if (curSt != null) {
                DependencyObject styScoped = curSt.TryFindStyleResourceBox(key);
                if (styScoped != null) { return styScoped; }
            }
            if (firstTplAmb == null && cur._hasTemplateAmbientResources && (cur == this || IsInTemplateOf(cur))) {
                firstTplAmb = cur.TryFindTemplateAmbientResourceBox(key);
            }
            if (firstElemAmb == null && cur._ambientResources != null) {
                firstElemAmb = cur._ambientResources.TryGetLocalBox(key);
            }
            cur = (cur._logicalParent != null && cur._logicalParent != cur) ? cur._logicalParent : cur._parent;
        }
        UIElement tp = null;
        if (!sawTpTarget) {
            tp = tpTarget;
        } else if (lastVisited != null && lastVisited._templatedParent != null) {
            tp = lastVisited._templatedParent;
        }
        while (tp != null) {
            if (tp._rare != null && tp._rare.LocResources != null) {
                DependencyObject tpLoc = tp._rare.LocResources.TryGetLocalBox(key);
                if (tpLoc != null) { return tpLoc; }
            }
            if (tp._resources != null) {
                DependencyObject tpLocal = tp._resources.TryGetLocalBox(key);
                if (tpLocal != null) { return tpLocal; }
            }
            if (tp._hasTemplateResources) {
                DependencyObject tpScoped = tp.TryFindTemplateScopedResourceBox(key);
                if (tpScoped != null) { return tpScoped; }
            }
            Style tpSt = tp._style != null ? tp._style : tp._appliedStyle;
            if (tpSt != null) {
                DependencyObject tpStyScoped = tpSt.TryFindStyleResourceBox(key);
                if (tpStyScoped != null) { return tpStyScoped; }
            }
            if (firstTplAmb == null && tp._hasTemplateAmbientResources) {
                firstTplAmb = tp.TryFindTemplateAmbientResourceBox(key);
            }
            if (firstElemAmb == null && tp._ambientResources != null) {
                firstElemAmb = tp._ambientResources.TryGetLocalBox(key);
            }
            tp = tp._parent != null ? tp._parent : tp._templatedParent;
        }
        if (firstElemAmb != null) { return firstElemAmb; }
        if (firstTplAmb != null) { return firstTplAmb; }
        DependencyObject ambient = ResourceDictionary.FindAmbientBox(key);
        if (ambient != null) { return ambient; }
        ResourceDictionary appRes = ResourceDictionary.GlobalApplicationResources();
        return appRes != null ? appRes.TryGetLocalBox(key) : null;
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

    private TransformGroup EnsureTransformGroup() {
        if (_renderTransform is TransformGroup) {
            return _renderTransform as TransformGroup;
        }
        var tg = new TransformGroup();
        if (_renderTransform != null) {
            tg.Add(_renderTransform);
        }
        _renderTransform = tg;
        return tg;
    }

    private TransformGroup EnsureLayoutTransformGroup() {
        if (_layoutTransform is TransformGroup) {
            return _layoutTransform as TransformGroup;
        }
        var tg = new TransformGroup();
        if (_layoutTransform != null) {
            tg.Add(_layoutTransform);
        }
        _layoutTransform = tg;
        return tg;
    }

    private void SetLayoutTransformScalar(string prop, float v) {
        if (prop == "LayoutTransform.ScaleX" || prop == "LayoutTransform.ScaleY") {
            if (_layoutTransform is ScaleTransform) {
                if (prop == "LayoutTransform.ScaleX") { (_layoutTransform as ScaleTransform).ScaleX = v; }
                else { (_layoutTransform as ScaleTransform).ScaleY = v; }
                SyncLayoutTransform();
                return;
            }
            TransformGroup tg = EnsureLayoutTransformGroup();
            Transform[] kids = tg.Children();
            ScaleTransform sc = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is ScaleTransform) { sc = kids[i] as ScaleTransform; }
            }
            if (sc == null) { sc = new ScaleTransform(1.0f, 1.0f); tg.Add(sc); }
            if (prop == "LayoutTransform.ScaleX") { sc.ScaleX = v; }
            else { sc.ScaleY = v; }
            SyncLayoutTransform();
            return;
        }
        if (prop == "LayoutTransform.Angle" || prop == "LayoutTransform.Rotation") {
            if (_layoutTransform is RotateTransform) {
                (_layoutTransform as RotateTransform).Angle = v;
                SyncLayoutTransform();
                return;
            }
            TransformGroup tg = EnsureLayoutTransformGroup();
            Transform[] kids = tg.Children();
            RotateTransform rt = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is RotateTransform) { rt = kids[i] as RotateTransform; }
            }
            if (rt == null) { rt = new RotateTransform(); tg.Add(rt); }
            rt.Angle = v;
            SyncLayoutTransform();
        }
    }

    private void SyncSingleTransform2DScalar(int propId, float v) {
        if (!_hasNative) { return; }
        if (!ParseUtil.IsNativeReady()) {
            SyncTransform2D();
            return;
        }
        float tx = propId == 3 ? v : Anim_ReadCurrentFloat(__nativeHandle, 3);
        float ty = propId == 4 ? v : Anim_ReadCurrentFloat(__nativeHandle, 4);
        float sx = propId == 5 ? v : Anim_ReadCurrentFloat(__nativeHandle, 5);
        float sy = propId == 6 ? v : Anim_ReadCurrentFloat(__nativeHandle, 6);
        float rot = propId == 7 ? v : Anim_ReadCurrentFloat(__nativeHandle, 7);
        float skx = propId == 8 ? v : Anim_ReadCurrentFloat(__nativeHandle, 8);
        float sky = propId == 9 ? v : Anim_ReadCurrentFloat(__nativeHandle, 9);
        TransformData td = null;
        if (_renderTransform != null && !HasUiFlag(512)) {
            td = TransformData.SharedScratch();
            _renderTransform.Accumulate(td);
        }
        bool useZeroOrig = (td != null && td.HasExplicitCenter && !HasUiFlag(512));
        Visual_SetTransform2D(
            __nativeHandle,
            tx, ty, sx, sy, rot, skx, sky,
            useZeroOrig ? 0.0f : _renderTransformOrigin.X,
            useZeroOrig ? 0.0f : _renderTransformOrigin.Y);
    }

    protected void SetTransformScalar(string prop, float v) {
        if (prop == "LayoutTransform.ScaleX" || prop == "LayoutTransform.ScaleY" ||
            prop == "LayoutTransform.Angle" || prop == "LayoutTransform.Rotation") {
            SetLayoutTransformScalar(prop, v);
            NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "TranslateX" || prop == "RenderTransform.X") {
            if (_renderTransform is TranslateTransform) {
                (_renderTransform as TranslateTransform).X = v;
                SyncSingleTransform2DScalar(3, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            TranslateTransform tr = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is TranslateTransform) { tr = kids[i] as TranslateTransform; }
            }
            if (tr == null) { tr = new TranslateTransform(); tg.Add(tr); }
            tr.X = v;
            SyncSingleTransform2DScalar(3, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "TranslateY" || prop == "RenderTransform.Y") {
            if (_renderTransform is TranslateTransform) {
                (_renderTransform as TranslateTransform).Y = v;
                SyncSingleTransform2DScalar(4, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            TranslateTransform tr = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is TranslateTransform) { tr = kids[i] as TranslateTransform; }
            }
            if (tr == null) { tr = new TranslateTransform(); tg.Add(tr); }
            tr.Y = v;
            SyncSingleTransform2DScalar(4, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "ScaleX" || prop == "RenderTransform.ScaleX") {
            if (_renderTransform is ScaleTransform) {
                (_renderTransform as ScaleTransform).ScaleX = v;
                SyncSingleTransform2DScalar(5, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            ScaleTransform sc = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is ScaleTransform) { sc = kids[i] as ScaleTransform; }
            }
            if (sc == null) { sc = new ScaleTransform(1.0f, 1.0f); tg.Add(sc); }
            sc.ScaleX = v;
            SyncSingleTransform2DScalar(5, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "ScaleY" || prop == "RenderTransform.ScaleY") {
            if (_renderTransform is ScaleTransform) {
                (_renderTransform as ScaleTransform).ScaleY = v;
                SyncSingleTransform2DScalar(6, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            ScaleTransform sc = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is ScaleTransform) { sc = kids[i] as ScaleTransform; }
            }
            if (sc == null) { sc = new ScaleTransform(1.0f, 1.0f); tg.Add(sc); }
            sc.ScaleY = v;
            SyncSingleTransform2DScalar(6, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "Rotation" || prop == "Angle" || prop == "RenderTransform.Angle") {
            if (_renderTransform is RotateTransform) {
                (_renderTransform as RotateTransform).Angle = v;
                SyncSingleTransform2DScalar(7, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            RotateTransform rt = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is RotateTransform) { rt = kids[i] as RotateTransform; }
            }
            if (rt == null) { rt = new RotateTransform(); tg.Add(rt); }
            rt.Angle = v;
            SyncSingleTransform2DScalar(7, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "SkewX" || prop == "RenderTransform.SkewX" || prop == "RenderTransform.AngleX") {
            if (_renderTransform is SkewTransform) {
                (_renderTransform as SkewTransform).AngleX = v;
                SyncSingleTransform2DScalar(8, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            SkewTransform sk = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is SkewTransform) { sk = kids[i] as SkewTransform; }
            }
            if (sk == null) { sk = new SkewTransform(); tg.Add(sk); }
            sk.AngleX = v;
            SyncSingleTransform2DScalar(8, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        if (prop == "SkewY" || prop == "RenderTransform.SkewY" || prop == "RenderTransform.AngleY") {
            if (_renderTransform is SkewTransform) {
                (_renderTransform as SkewTransform).AngleY = v;
                SyncSingleTransform2DScalar(9, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
                return;
            }
            TransformGroup tg = EnsureTransformGroup();
            Transform[] kids = tg.Children();
            SkewTransform sk = null;
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] is SkewTransform) { sk = kids[i] as SkewTransform; }
            }
            if (sk == null) { sk = new SkewTransform(); tg.Add(sk); }
            sk.AngleY = v;
            SyncSingleTransform2DScalar(9, v);
                NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
    }

    protected float GetTransformScalar(string prop) {
        if (prop == "LayoutTransform.ScaleX" || prop == "LayoutTransform.ScaleY" ||
            prop == "LayoutTransform.Angle" || prop == "LayoutTransform.Rotation") {
            TransformData ltd = TransformData.SharedScratch();
            if (_layoutTransform != null) { _layoutTransform.Accumulate(ltd); }
            if (prop == "LayoutTransform.ScaleX") { return ltd.SX; }
            if (prop == "LayoutTransform.ScaleY") { return ltd.SY; }
            return ltd.RotDeg;
        }
        int pid = 0;
        if (prop == "TranslateX" || prop == "RenderTransform.X") { pid = 3; }
        else if (prop == "TranslateY" || prop == "RenderTransform.Y") { pid = 4; }
        else if (prop == "ScaleX" || prop == "RenderTransform.ScaleX") { pid = 5; }
        else if (prop == "ScaleY" || prop == "RenderTransform.ScaleY") { pid = 6; }
        else if (prop == "Rotation" || prop == "Angle" || prop == "RenderTransform.Angle") { pid = 7; }
        else if (prop == "SkewX" || prop == "RenderTransform.SkewX" || prop == "RenderTransform.AngleX") { pid = 8; }
        else if (prop == "SkewY" || prop == "RenderTransform.SkewY" || prop == "RenderTransform.AngleY") { pid = 9; }
        if (pid == 0) { return 0.0f; }
        if (_hasNative && ParseUtil.IsNativeReady()) { return Anim_ReadCurrentFloat(__nativeHandle, pid); }
        TransformData td = TransformData.SharedScratch();
        if (_renderTransform != null) { _renderTransform.Accumulate(td); }
        if (pid == 3) { return td.TX; }
        if (pid == 4) { return td.TY; }
        if (pid == 5) { return td.SX; }
        if (pid == 6) { return td.SY; }
        if (pid == 7) { return td.RotDeg; }
        if (pid == 8) { return td.SkewX; }
        return td.SkewY;
    }

    // ---- Uniform Property Reflection Bridge (Style, Triggers, BindingEngine, VSM, Behaviors) ----
    public virtual bool SetPropertyValue(string propName, DependencyObject value) {
        BumpPropEpoch();
        if (propName == null) { return false; }
        if (propName == "Width") { Width = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true; }
        if (propName == "Height") { Height = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true; }
        if (propName == "MinWidth") { MinWidth = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MinHeight") { MinHeight = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MaxWidth") { MaxWidth = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true; }
        if (propName == "MaxHeight") { MaxHeight = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true; }
        if (propName == "Opacity") { Opacity = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "BrushOpacity") {
            _bgOpacity = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
            if (_hasNative) { Visual_SetBoxBrushOpacity(__nativeHandle, _bgOpacity); }
            NotifyBoundPropertyChanged("BrushOpacity", DependencyObject.BoxFloat(_bgOpacity));
            return true;
        }
        if (propName == "EffectOpacity") {
            float eo = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
            if (_effect != null) { _effect.Opacity = eo; SyncEffect(); }
            NotifyBoundPropertyChanged("EffectOpacity", DependencyObject.BoxFloat(eo));
            return true;
        }
        if (propName == "Effect.Radius" || propName == "Effect.BlurRadius") {
            if (_effect != null) {
                _effect.Radius = DependencyObject.UnboxFloat(value, _effect.Radius);
                SyncEffect();
                NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(_effect.Radius));
                return true;
            }
            return false;
        }
        if (propName == "ZIndex" || propName == "Panel.ZIndex" || propName == "Canvas.ZIndex") {
            ZIndex = DependencyObject.UnboxInt(value, 0);
            return true;
        }
        if (propName == "IsEnabled") { IsEnabled = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "IsHitTestVisible") { IsHitTestVisible = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "Focusable") { Focusable = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "ClipToBounds") { ClipToBounds = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Visibility") {
            if (value is BoxedInt) { Visibility = EnumConvert.ToVisibility((value as BoxedInt).Value); return true; }
            string vs = DependencyObject.UnboxString(value, "Visible").Trim();
            if (vs.Length > 11 && vs.Substring(0, 11) == "Visibility.") { vs = vs.Substring(11); }
            if (vs == "Hidden" || vs == "1") { Visibility = Visibility.Hidden; return true; }
            if (vs == "Collapsed" || vs == "2") { Visibility = Visibility.Collapsed; return true; }
            Visibility = Visibility.Visible;
            return true;
        }
        if (propName == "HorizontalAlignment") {
            if (value is BoxedInt) { HorizontalAlignment = EnumConvert.ToHorizontalAlignment((value as BoxedInt).Value); return true; }
            string ha = DependencyObject.UnboxString(value, "Stretch").Trim();
            if (ha == "Left" || ha == "1") { HorizontalAlignment = HorizontalAlignment.Left; return true; }
            if (ha == "Center" || ha == "2") { HorizontalAlignment = HorizontalAlignment.Center; return true; }
            if (ha == "Right" || ha == "3") { HorizontalAlignment = HorizontalAlignment.Right; return true; }
            HorizontalAlignment = HorizontalAlignment.Stretch;
            return true;
        }
        if (propName == "VerticalAlignment") {
            if (value is BoxedInt) { VerticalAlignment = EnumConvert.ToVerticalAlignment((value as BoxedInt).Value); return true; }
            string va = DependencyObject.UnboxString(value, "Stretch").Trim();
            if (va == "Top" || va == "1") { VerticalAlignment = VerticalAlignment.Top; return true; }
            if (va == "Center" || va == "2") { VerticalAlignment = VerticalAlignment.Center; return true; }
            if (va == "Bottom" || va == "3") { VerticalAlignment = VerticalAlignment.Bottom; return true; }
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
        if (propName == "Background" || propName == "Background.Color") {
            Background = CoerceBrush(value);
            return true;
        }
        if (propName.StartsWith("Background.") && propName.Length > 11 && _background != null) {
            string bgSub = propName.Substring(11);
            bool needClone = !HasUiFlag(8);
            Brush updatedBg = _background.ApplySubProperty(bgSub, value, needClone);
            if (updatedBg != null) {
                _background = updatedBg;
                if (needClone) { SetUiFlag(8, true); }
                OnBoxAppearanceChanged();
                return true;
            }
            if (bgSub == "ImageSource") { return false; }
        }
        bool isFillPrefix = propName.StartsWith("Fill.") && propName.Length > 5;
        if (isFillPrefix || (propName.StartsWith("Background.") && propName.Length > 11)) {
            string suffix = isFillPrefix ? propName.Substring(5) : propName.Substring(11);
            if (_background is GradientBrush && GradientBrush.ParseStopSlotIndex(suffix) >= 0) {
                GradientBrush gb = _background as GradientBrush;
                if (!HasUiFlag(4)) {
                    gb = gb.CloneBrush();
                    _background = gb;
                    SetUiFlag(4, true);
                }
                gb.TryApplyStopProperty(suffix, value);
                OnBoxAppearanceChanged();
                return true;
            }
        }
        if (propName == "BorderBrush" || propName == "BorderBrush.Color") {
            BorderBrush = CoerceBrush(value);
            return true;
        }
        if (propName == "BorderBrush.Opacity") {
            float bop = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
            if (_borderBrush != null) {
                var clonedBd = new SolidColorBrush(_borderBrush.ResolveRgbaUnscaled());
                clonedBd.Opacity = bop;
                _borderBrush = clonedBd;
                OnBoxAppearanceChanged();
            }
            return true;
        }
        if (propName == "TranslateX" || propName == "RenderTransform.X" ||
            propName == "TranslateY" || propName == "RenderTransform.Y" ||
            propName == "ScaleX" || propName == "RenderTransform.ScaleX" ||
            propName == "ScaleY" || propName == "RenderTransform.ScaleY" ||
            propName == "Rotation" || propName == "Angle" || propName == "RenderTransform.Angle" ||
            propName == "SkewX" || propName == "RenderTransform.SkewX" || propName == "RenderTransform.AngleX" ||
            propName == "SkewY" || propName == "RenderTransform.SkewY" || propName == "RenderTransform.AngleY" ||
            propName == "LayoutTransform.ScaleX" || propName == "LayoutTransform.ScaleY" ||
            propName == "LayoutTransform.Angle" || propName == "LayoutTransform.Rotation") {
            float defVal = (propName == "ScaleX" || propName == "RenderTransform.ScaleX" ||
                            propName == "ScaleY" || propName == "RenderTransform.ScaleY" ||
                            propName == "LayoutTransform.ScaleX" || propName == "LayoutTransform.ScaleY") ? 1.0f : 0.0f;
            SetTransformScalar(propName, DependencyObject.UnboxFloat(value, defVal));
            return true;
        }
        if (propName == "RenderTransform") {
            if (value is Transform) { RenderTransform = value as Transform; return true; }
            return false;
        }
        if (propName == "LayoutTransform") {
            if (value is Transform) { LayoutTransform = value as Transform; return true; }
            if (value == null) { LayoutTransform = null; return true; }
            return false;
        }
        if (propName == "RenderTransformOrigin") {
            if (value is Point) { RenderTransformOrigin = value as Point; return true; }
            string s = DependencyObject.UnboxString(value, "").Trim();
            if (s.Length > 0) {
                string[] parts = s.Split(",");
                if (parts.Length < 2) { parts = s.Split(" "); }
                if (parts.Length >= 2) {
                    RenderTransformOrigin = new Point(
                        ParseUtil.ParseFloat(parts[0], 0.5f),
                        ParseUtil.ParseFloat(parts[1], 0.5f));
                    return true;
                }
            }
            return false;
        }
        if (propName == "Effect") {
            if (value is Effect) { Effect = value as Effect; return true; }
            return false;
        }
        if (propName == "OpacityMask") {
            OpacityMask = CoerceBrush(value);
            return true;
        }
        if (propName == "Name") {
            Name = DependencyObject.UnboxString(value, "");
            return true;
        }
        if (propName == "TabIndex" || propName == "KeyboardNavigation.TabIndex") {
            TabIndex = DependencyObject.UnboxInt(value, 0);
            return true;
        }
        if (propName == "DataContext") {
            DataContextBox = value;
            return true;
        }
        if (propName == "Tag") {
            bool same = (_tag == value) || (_tag != null && value != null && _tag.BoxValueEquals(value));
            if (!same) {
                _tag = value;
                NotifyBoundPropertyChanged("Tag", _tag);
                UpdateTriggersAndStates();
            }
            return true;
        }
        if (propName == "Style" && value is Style) {
            Style = value as Style;
            return true;
        }
        if (propName == "FocusVisualStyle") {
            if (value is Style) { FocusVisualStyle = value as Style; }
            else { FocusVisualStyle = null; }
            return true;
        }
        if (propName == "Grid.Row") { int gv = DependencyObject.UnboxInt(value, 0); SetGridRow(this, gv); NotifyBoundPropertyChanged(propName, DependencyObject.BoxInt(_gridRow)); return true; }
        if (propName == "Grid.Column") { int gv = DependencyObject.UnboxInt(value, 0); SetGridColumn(this, gv); NotifyBoundPropertyChanged(propName, DependencyObject.BoxInt(_gridCol)); return true; }
        if (propName == "Grid.RowSpan") { int gv = DependencyObject.UnboxInt(value, 1); SetGridRowSpan(this, gv); NotifyBoundPropertyChanged(propName, DependencyObject.BoxInt(_gridRowSpan)); return true; }
        if (propName == "Grid.ColumnSpan") { int gv = DependencyObject.UnboxInt(value, 1); SetGridColumnSpan(this, gv); NotifyBoundPropertyChanged(propName, DependencyObject.BoxInt(_gridColSpan)); return true; }
        if (propName == "Canvas.Left") { SetCanvasLeft(this, DependencyObject.UnboxFloat(value, 0.0f)); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(_canvasLeft)); return true; }
        if (propName == "Canvas.Top") { SetCanvasTop(this, DependencyObject.UnboxFloat(value, 0.0f)); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(_canvasTop)); return true; }
        if (propName == "Canvas.Right") { SetCanvasRight(this, DependencyObject.UnboxFloat(value, 0.0f)); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(_canvasRight)); return true; }
        if (propName == "Canvas.Bottom") { SetCanvasBottom(this, DependencyObject.UnboxFloat(value, 0.0f)); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(_canvasBottom)); return true; }
        if (propName == "DockPanel.Dock" || propName == "Dock") {
            Dock d = Dock.Left;
            if (value is BoxedInt) {
                int dv = (value as BoxedInt).Value;
                d = dv == 1 ? Dock.Top : (dv == 2 ? Dock.Right : (dv == 3 ? Dock.Bottom : Dock.Left));
            } else {
                string ds = DependencyObject.UnboxString(value, "Left").Trim();
                if (ds == "Top" || ds == "1") { d = Dock.Top; }
                else if (ds == "Right" || ds == "2") { d = Dock.Right; }
                else if (ds == "Bottom" || ds == "3") { d = Dock.Bottom; }
            }
            SetDock(this, d);
            DependencyObject boxedDock = DependencyObject.BoxInt(EnumConvert.ToInt(_dock));
            NotifyBoundPropertyChanged("DockPanel.Dock", boxedDock);
            NotifyBoundPropertyChanged("Dock", boxedDock);
            return true;
        }
        if (propName == "Rotation3DX" || propName == "Rot3DX" || propName == "Transform3D.RotationX" || propName == "Projection3D.RotationX") { Rotation3DX = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Rotation3DY" || propName == "Rot3DY" || propName == "Transform3D.RotationY" || propName == "Projection3D.RotationY") { Rotation3DY = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Rotation3DZ" || propName == "Rot3DZ" || propName == "Transform3D.RotationZ" || propName == "Projection3D.RotationZ") { Rotation3DZ = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Translation3DX" || propName == "Trans3DX" || propName == "Transform3D.TranslateX") { Translation3DX = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Translation3DY" || propName == "Trans3DY" || propName == "Transform3D.TranslateY") { Translation3DY = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Translation3DZ" || propName == "Trans3DZ" || propName == "Transform3D.TranslateZ") { Translation3DZ = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Transform3D.ScaleX" || propName == "Scale3DX") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Scale3DX = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.ScaleY" || propName == "Scale3DY") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Scale3DY = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.ScaleZ" || propName == "Scale3DZ") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Scale3DZ = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.CenterX") { EnsureRare().Center3DX = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
        if (propName == "Transform3D.CenterY") { EnsureRare().Center3DY = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
        if (propName == "Transform3D.CenterZ") { EnsureRare().Center3DZ = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
        if (propName == "CameraDistance") { CameraDistance = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "TextElement.FontSize" || propName == "TextBlock.FontSize") {
            SetExplicitTextFontSize(DependencyObject.UnboxFloat(value, 14.0f));
            return true;
        }
        if (propName == "TextElement.FontFamily" || propName == "TextBlock.FontFamily") {
            SetExplicitTextFontFamily(DependencyObject.UnboxString(value, "Segoe UI"));
            return true;
        }
        if (propName == "TextElement.FontWeight" || propName == "TextBlock.FontWeight") {
            SetExplicitTextFontWeight(DependencyObject.UnboxString(value, "Normal"));
            return true;
        }
        if (propName == "TextElement.Foreground" || propName == "TextBlock.Foreground") {
            SetExplicitTextForeground(CoerceBrush(value));
            return true;
        }
        if (propName == "ToolTip" || propName == "ToolTipService.ToolTip" || propName == "noesis:ToolTipService.ToolTip") {
            if (value != null || _rare != null) {
                UIElementRareData r = EnsureRare();
                r.ToolTipFactory = null;
                r.ToolTip = value;
            }
            if (_toolTipEventHook != null && value != null) {
                var tth = _toolTipEventHook;
                tth(this, "ToolTip", value);
            }
            return true;
        }
        if (propName == "Element.BlendingMode" || propName == "noesis:Element.BlendingMode" ||
            propName == "local:Element.BlendingMode" || propName == "BlendMode") {
            if (value is BoxedInt) {
                int bm = (value as BoxedInt).Value;
                BlendMode = bm == 1 ? BlendingMode.Multiply : (bm == 2 ? BlendingMode.Screen : (bm == 3 ? BlendingMode.Additive : BlendingMode.Normal));
            } else {
                BlendMode = EnumConvert.ParseBlendingMode(DependencyObject.UnboxString(value, "Normal"));
            }
            return true;
        }
        if ((propName == "Element.Projection3D" || propName == "Projection3D") && value is PlaneProjection) {
            Projection3D = value as PlaneProjection;
            return true;
        }
        if ((propName == "Element.Transform3D" || propName == "Transform3D") && value is CompositeTransform3D) {
            Transform3D = value as CompositeTransform3D;
            return true;
        }
        if (propName == "RiveControl.Source") {
            EnsureRare().RiveSource = DependencyObject.UnboxString(value, "");
            return true;
        }
        if (propName == "Loc.Source" || propName == "noesis:Loc.Source") {
            ApplyLocSource(DependencyObject.UnboxString(value, ""));
            return true;
        }
        if (propName == "FlowDirection") {
            string fd = DependencyObject.UnboxString(value, "LeftToRight").Trim();
            if (fd.Length > 0) {
                FlowDirection = fd;
            }
            return true;
        }
        if (propName == "ContextMenu") {
            if (_contextMenuSetterHook != null) {
                var cmHook = _contextMenuSetterHook;
                cmHook(this, propName, value);
            } else {
                UIElement cmVal = DependencyObject.Unbox(value) as UIElement;
                if (cmVal != null || _rare != null) {
                    EnsureRare().ContextMenuEl = cmVal;
                }
            }
            return true;
        }
        string cleanCustom = NormalizeCustomPropKey(propName);
        if (IsCustomAttachedPropKey(cleanCustom)) {
            if (cleanCustom.Length > 6 && cleanCustom.EndsWith("_Int32") && value != null && !(value is BoxedInt)) {
                value = DependencyObject.BoxInt(DependencyObject.UnboxInt(value, 0));
            }
            SetDynamicProp(cleanCustom, value);
            NotifyBoundPropertyChanged(cleanCustom, value);
            if (cleanCustom != propName) {
                NotifyBoundPropertyChanged(propName, value);
            }
            if (_attachedPropHook != null && cleanCustom.Length >= 9 && cleanCustom.Substring(0, 9) == "DragDrop.") {
                var hook = _attachedPropHook;
                hook(this, cleanCustom, value);
            }
            UpdateTriggersAndStates();
            return true;
        }
        return false;
    }

    internal static string NormalizeCustomPropKey(string propName) {
        if (propName == null || propName.Length == 0) { return ""; }
        int len = propName.Length;
        string first = propName.Substring(0, 1);
        string last = propName.Substring(len - 1, 1);
        if (first != " " && first != "\t" && last != " " && last != "\t" && first != "(" && propName.IndexOf(":") < 0) {
            return propName;
        }
        string s = propName.Trim();
        if (s.Length > 2 && s.Substring(0, 1) == "(" && s.Substring(s.Length - 1, 1) == ")") {
            s = s.Substring(1, s.Length - 2).Trim();
        }
        int colon = s.IndexOf(":");
        int dot = s.IndexOf(".");
        if (colon > 0 && colon + 1 < s.Length && (dot < 0 || colon < dot)) {
            s = s.Substring(colon + 1);
        }
        return s;
    }

    internal static bool IsAttachedElementPropPath(string propName) {
        if (propName == null || propName.Length == 0) { return false; }
        string clean = NormalizeCustomPropKey(propName);
        if (IsCustomAttachedPropKey(clean)) { return true; }
        return clean == "ItemsControl.AlternationIndex" || clean == "AlternationIndex";
    }

    private static bool IsCustomAttachedPropKey(string cleanKey) {
        return cleanKey.StartsWith("Custom.") ||
               cleanKey.StartsWith("DragDrop.") ||
               cleanKey.StartsWith("ToolTipService.") ||
               cleanKey.StartsWith("ScrollViewer.") ||
               cleanKey.StartsWith("Text.");
    }

    private void SetDynamicProp(string cleanKey, DependencyObject value) {
        BumpPropEpoch();
        BindingEngine.MarkFlushDirty();
        if (_dynPropKeys == null) {
            _dynPropKeys = new string[0];
            _dynPropVals = new DependencyObject[0];
        }
        for (int i = 0; i < _dynPropKeys.Length; i += 1) {
            if (_dynPropKeys[i] == cleanKey) {
                _dynPropVals[i] = value;
                return;
            }
        }
        _dynPropKeys.Push(cleanKey);
        _dynPropVals.Push(value);
    }

    private DependencyObject GetDynamicProp(string cleanKey) {
        if (_dynPropKeys == null) { return null; }
        for (int i = 0; i < _dynPropKeys.Length; i += 1) {
            if (_dynPropKeys[i] == cleanKey) { return _dynPropVals[i]; }
        }
        return null;
    }

    public void ApplyLocSource(string sourceName) {
        string nextSrc = sourceName == null ? "" : sourceName.Trim();
        ResourceDictionary dict = ResourceDictionary.GetLocSource(nextSrc);
        if (dict == null) { return; }
        UIElementRareData r = EnsureRare();
        r.LocSource = nextSrc;
        r.LocResources = dict;
        RefreshDynamicResourcesRecursive();
        BindingEngine.Flush();
        UpdateLayout();
    }

    public virtual bool IsExplicitNullProperty(string propName) { return false; }

    internal virtual bool IsItemsControlBoundary() { return false; }
    internal virtual bool IsItemsPresenterBoundary() { return false; }
    internal virtual bool IsClosedPopupBoundary() { return false; }
    internal virtual bool DisposeWhenResourceOwned() { return false; }
    internal virtual bool DetachNonTemplateUserContent(UIElement owner, UIElement[] q) { return false; }

    public virtual DependencyObject GetPropertyValue(string propName) {
        if (propName == "RiveControl.Source") { return DependencyObject.BoxString(_rare != null ? _rare.RiveSource : ""); }
        if (propName == "Width") { return DependencyObject.BoxFloat(_width); }
        if (propName == "Height") { return DependencyObject.BoxFloat(_height); }
        if (propName == "MinWidth") { return DependencyObject.BoxFloat(_minWidth); }
        if (propName == "MinHeight") { return DependencyObject.BoxFloat(_minHeight); }
        if (propName == "MaxWidth") { return DependencyObject.BoxFloat(_maxWidth); }
        if (propName == "MaxHeight") { return DependencyObject.BoxFloat(_maxHeight); }
        if (propName == "ActualWidth") { return DependencyObject.BoxFloat(ActualWidth); }
        if (propName == "ActualHeight") { return DependencyObject.BoxFloat(ActualHeight); }
        if (propName == "Margin") { return _margin; }
        if (propName == "Padding") { return _padding; }
        if (propName == "BorderThickness") { return _borderThickness; }
        if (propName == "CornerRadius") { return _cornerRadius; }
        if (propName == "HorizontalAlignment") { return DependencyObject.BoxInt(EnumConvert.ToInt(_hAlign)); }
        if (propName == "VerticalAlignment") { return DependencyObject.BoxInt(EnumConvert.ToInt(_vAlign)); }
        if (propName == "Opacity") { return DependencyObject.BoxFloat(_opacity); }
        if (propName == "BrushOpacity") { return DependencyObject.BoxFloat(_bgOpacity); }
        if (propName == "EffectOpacity") { return DependencyObject.BoxFloat(_effect != null ? _effect.Opacity : 1.0f); }
        if (propName == "Effect") { return _effect; }
        if (propName == "Effect.Radius" || propName == "Effect.BlurRadius") { return DependencyObject.BoxFloat(_effect != null ? _effect.Radius : 0.0f); }
        if (propName == "IsEnabled") { return DependencyObject.BoxBool(_isEnabled); }
        if (propName == "IsMouseOver") { return DependencyObject.BoxBool(_isMouseOver); }
        if (propName == "IsPressed") { return DependencyObject.BoxBool(_isPressed); }
        if (propName == "IsFocused" || propName == "IsKeyboardFocused") { return DependencyObject.BoxBool(_isFocused); }
        if (propName == "IsKeyboardFocusWithin") { return DependencyObject.BoxBool(_isKeyboardFocusWithin); }
        if (propName == "Visibility") { return DependencyObject.BoxInt(EnumConvert.ToInt(_visibility)); }
        if (propName == "Background") { return _background; }
        if (propName == "Background.Color") {
            return DependencyObject.BoxString(_background != null ? ParseUtil.FormatArgbHex(_background.ResolveRgba()) : "#00000000");
        }
        if (propName.StartsWith("Background.") && propName.Length > 11 && _background != null) {
            DependencyObject subVal = _background.ReadSubProperty(propName.Substring(11));
            if (subVal != null) { return subVal; }
        }
        bool isGetFill = propName.StartsWith("Fill.") && propName.Length > 5;
        if (isGetFill || (propName.StartsWith("Background.") && propName.Length > 11)) {
            string suffix = isGetFill ? propName.Substring(5) : propName.Substring(11);
            if (_background is GradientBrush) {
                DependencyObject stopBox = (_background as GradientBrush).TryReadStopProperty(suffix);
                if (stopBox != null) { return stopBox; }
            }
        }
        if (propName == "BorderBrush") { return _borderBrush; }
        if (propName == "RenderTransform") { return _renderTransform; }
        if (propName == "LayoutTransform") { return _layoutTransform; }
        if (propName == "RenderTransformOrigin") { return _renderTransformOrigin; }
        if (propName == "OpacityMask") { return _opacityMask; }
        if (propName == "ToolTip" || propName == "ToolTipService.ToolTip") { return _rare != null ? _rare.ToolTip : null; }
        if (propName == "Element.BlendingMode" || propName == "noesis:Element.BlendingMode" ||
            propName == "local:Element.BlendingMode" || propName == "BlendMode") {
            int bm = _blendMode == BlendingMode.Multiply ? 1 : (_blendMode == BlendingMode.Screen ? 2 : (_blendMode == BlendingMode.Additive ? 3 : 0));
            return DependencyObject.BoxInt(bm);
        }
        if (propName == "BorderBrush.Color") {
            return DependencyObject.BoxString(_borderBrush != null ? ParseUtil.FormatArgbHex(_borderBrush.ResolveRgba()) : "#00000000");
        }
        if (propName == "BorderBrush.Opacity") {
            return DependencyObject.BoxFloat(_borderBrush != null ? _borderBrush.Opacity : 1.0f);
        }
        if (propName == "Name") { return DependencyObject.BoxString(_name); }
        if (propName == "DockPanel.Dock" || propName == "Dock") { return DependencyObject.BoxInt(EnumConvert.ToInt(_dock)); }
        if (propName == "ZIndex" || propName == "Panel.ZIndex" || propName == "Canvas.ZIndex") { return DependencyObject.BoxInt(_zIndex); }
        if (propName == "ItemsControl.AlternationIndex" || propName == "(ItemsControl.AlternationIndex)" || propName == "AlternationIndex") {
            return DependencyObject.BoxInt(_alternationIndex);
        }
        if (propName == "TabIndex" || propName == "KeyboardNavigation.TabIndex") { return DependencyObject.BoxInt(_keyboardTabIndex); }
        if (propName == "DataContext") { return EffectiveDataContextBox(); }
        if (propName == "Tag") { return _tag; }
        if (propName == "Rotation3DX" || propName == "Rot3DX" || propName == "Transform3D.RotationX" || propName == "Projection3D.RotationX") { return DependencyObject.BoxFloat(Rotation3DX); }
        if (propName == "Rotation3DY" || propName == "Rot3DY" || propName == "Transform3D.RotationY" || propName == "Projection3D.RotationY") { return DependencyObject.BoxFloat(Rotation3DY); }
        if (propName == "Rotation3DZ" || propName == "Rot3DZ" || propName == "Transform3D.RotationZ" || propName == "Projection3D.RotationZ") { return DependencyObject.BoxFloat(Rotation3DZ); }
        if (propName == "Translation3DX" || propName == "Trans3DX" || propName == "Transform3D.TranslateX") { return DependencyObject.BoxFloat(Translation3DX); }
        if (propName == "Translation3DY" || propName == "Trans3DY" || propName == "Transform3D.TranslateY") { return DependencyObject.BoxFloat(Translation3DY); }
        if (propName == "Translation3DZ" || propName == "Trans3DZ" || propName == "Transform3D.TranslateZ") { return DependencyObject.BoxFloat(Translation3DZ); }
        if (propName == "Transform3D.ScaleX" || propName == "Scale3DX") { return DependencyObject.BoxFloat(_rare != null ? _rare.Scale3DX : 1.0f); }
        if (propName == "Transform3D.ScaleY" || propName == "Scale3DY") { return DependencyObject.BoxFloat(_rare != null ? _rare.Scale3DY : 1.0f); }
        if (propName == "Transform3D.ScaleZ" || propName == "Scale3DZ") { return DependencyObject.BoxFloat(_rare != null ? _rare.Scale3DZ : 1.0f); }
        if (propName == "Transform3D.CenterX") { return DependencyObject.BoxFloat(_rare != null ? _rare.Center3DX : 0.0f); }
        if (propName == "Transform3D.CenterY") { return DependencyObject.BoxFloat(_rare != null ? _rare.Center3DY : 0.0f); }
        if (propName == "Transform3D.CenterZ") { return DependencyObject.BoxFloat(_rare != null ? _rare.Center3DZ : 0.0f); }
        if (propName == "RenderTransform.ScaleX" || propName == "ScaleX" ||
            propName == "RenderTransform.ScaleY" || propName == "ScaleY" ||
            propName == "RenderTransform.Angle" || propName == "Rotation" || propName == "Angle" ||
            propName == "RenderTransform.X" || propName == "TranslateX" ||
            propName == "RenderTransform.Y" || propName == "TranslateY" ||
            propName == "RenderTransform.SkewX" || propName == "SkewX" || propName == "RenderTransform.AngleX" ||
            propName == "RenderTransform.SkewY" || propName == "SkewY" || propName == "RenderTransform.AngleY" ||
            propName == "LayoutTransform.ScaleX" || propName == "LayoutTransform.ScaleY" ||
            propName == "LayoutTransform.Angle" || propName == "LayoutTransform.Rotation") {
            return DependencyObject.BoxFloat(GetTransformScalar(propName));
        }
        if (propName == "FlowDirection") { return DependencyObject.BoxString(_flowDirection); }
        if (propName == "ContextMenu") { return _rare != null ? _rare.ContextMenuEl : null; }
        if (propName == "Loc.Source" || propName == "noesis:Loc.Source") { return DependencyObject.BoxString(_rare != null ? _rare.LocSource : ""); }
        DependencyObject customBox = GetDynamicProp(NormalizeCustomPropKey(propName));
        if (customBox != null) { return customBox; }
        return null;
    }

    public static Brush CoerceBrush(DependencyObject value) {
        if (value == null) { return null; }
        if (value is Brush) { return value as Brush; }
        string s = DependencyObject.UnboxString(value, "");
        if (s.Length > 0) { return new SolidColorBrush(s); }
        return null;
    }

    public void SetPropertyBox(string propName, DependencyObject value) {
        SetPropertyValue(propName, value);
    }
    public void SetPropertyObject(string propName, object value) {
        SetPropertyValue(propName, DependencyObject.BoxObject(value));
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
        BumpPropEpoch();
        if (focused) {
            OnGotFocus();
            GotFocus(this, new RoutedEventArgs("GotFocus", this));
        } else {
            OnLostFocus();
            LostFocus(this, new RoutedEventArgs("LostFocus", this));
        }
        BindingEngine.NotifyTargetPropertyChanged(this, "IsFocused", DependencyObject.BoxBool(_isFocused));
        BindingEngine.NotifyTargetPropertyChanged(this, "IsKeyboardFocused", DependencyObject.BoxBool(_isFocused));
        UpdateTriggersAndStates();
    }

    internal void SetKeyboardFocusWithinState(bool within) {
        if (_isKeyboardFocusWithin == within) { return; }
        _isKeyboardFocusWithin = within;
        BumpPropEpoch();
        BindingEngine.NotifyTargetPropertyChanged(this, "IsKeyboardFocusWithin", DependencyObject.BoxBool(_isKeyboardFocusWithin));
        UpdateTriggersAndStates();
    }

    internal void SetMouseOverState(bool over, float x, float y, int mods) {
        if (_isMouseOver == over) { return; }
        _isMouseOver = over;
        BumpPropEpoch();
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
        BumpPropEpoch();
        UpdateTriggersAndStates();
    }

    internal void RefreshTriggersAndStates() {
        UpdateTriggersAndStates();
    }

    protected virtual void UpdateTriggersAndStates() {
        BumpPropEpoch();
        Style activeStyle = _style != null ? _style : _appliedStyle;
        if (activeStyle != null) { activeStyle.EvaluateTriggers(this); }
        OnVisualStatePropertyChanged();
        NotifyStateInvalidatedHooks();
    }

    public void AddStateInvalidatedHook(Action cb) {
        if (cb == null) { return; }
        if (_stateInvalidatedHooks == null) { _stateInvalidatedHooks = new Action[0]; }
        _stateInvalidatedHooks.Push(cb);
    }

    protected void NotifyStateInvalidatedHooks() {
        BumpPropEpoch();
        if (_stateInvalidatedHooks == null) { return; }
        for (int i = 0; i < _stateInvalidatedHooks.Length; i += 1) {
            var fn = _stateInvalidatedHooks[i];
            if (fn != null) { fn(); }
        }
    }

    protected virtual void OnVisualStatePropertyChanged() {}
    protected virtual void OnLoaded() {
        if (_background != null && _background.NeedsLoadedRefresh()) { SyncBoxStyleToNative(); }
    }
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
    public virtual void OnHostViewportResized(float width, float height) {}

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

    private string[] _routedEventNames;
    private BehaviorEventSink[] _routedEventSubs;

    public virtual bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == null || ev.Length == 0 || sink == null) { return false; }
        if (_routedEventNames == null) {
            _routedEventNames = new string[0];
            _routedEventSubs = new BehaviorEventSink[0];
        }
        _routedEventNames.Push(ev);
        _routedEventSubs.Push(sink);
        return true;
    }

    public void RaiseRoutedEvent(string ev, RoutedEventArgs args) {
        if (ev == null || ev.Length == 0) { return; }
        RoutedEventArgs effArgs = args != null ? args : new RoutedEventArgs(ev, this);
        UIElement cur = this;
        int guard = 0;
        while (cur != null && !effArgs.Handled && guard < 256) {
            guard += 1;
            if (cur._routedEventSubs != null && cur._routedEventNames != null) {
                int n = cur._routedEventSubs.Length;
                for (int i = 0; i < n; i += 1) {
                    if (cur._routedEventNames[i] == ev && cur._routedEventSubs[i] != null) {
                        cur._routedEventSubs[i].OnBehaviorEvent(this, effArgs);
                    }
                }
            }
            cur = cur._parent != null ? cur._parent : cur._templatedParent;
        }
    }

    public virtual bool IsNamescopeBoundary() { return HasUiFlag(1024); }
}

public class FrameworkElement : UIElement {
    public FrameworkElement() : base(VisualKind.Box) {}
    protected FrameworkElement(VisualKind kind) : base(kind) {}
    public override string GetTypeName() { return "FrameworkElement"; }
    public void SetDataContext<T>(T vm) { DataContext = vm; }
    public virtual bool ApplyTemplate() { OnApplyTemplate(); return true; }
    public virtual void OnApplyTemplate() {}
}

public static class TextElement {
    public static void SetFontSize(UIElement el, float size) {
        if (el == null) { return; }
        el.SetExplicitTextFontSize(size);
        el.SetPropertyValue("FontSize", DependencyObject.BoxFloat(size));
    }
    public static float GetFontSize(UIElement el) {
        if (el == null) { return 14.0f; }
        float f = el.EffectiveTextFontSizeOrZero();
        return f > 0.0f ? f : 14.0f;
    }
    public static void SetFontFamily(UIElement el, string family) {
        if (el == null) { return; }
        el.SetExplicitTextFontFamily(family);
        el.SetPropertyValue("FontFamily", DependencyObject.BoxString(family));
    }
    public static string GetFontFamily(UIElement el) {
        if (el == null) { return "Segoe UI"; }
        string ff = el.EffectiveTextFontFamilyOrEmpty();
        return ff.Length > 0 ? ff : "Segoe UI";
    }
    public static void SetFontWeight(UIElement el, string weight) {
        if (el == null) { return; }
        el.SetExplicitTextFontWeight(weight);
        el.SetPropertyValue("FontWeight", DependencyObject.BoxString(weight));
    }
    public static string GetFontWeight(UIElement el) {
        if (el == null) { return "Normal"; }
        string fw = el.EffectiveTextFontWeightOrEmpty();
        return fw.Length > 0 ? fw : "Normal";
    }
    public static void SetForeground(UIElement el, Brush brush) {
        if (el == null) { return; }
        el.SetExplicitTextForeground(brush);
        el.SetPropertyValue("Foreground", brush);
    }
    public static Brush GetForeground(UIElement el) {
        return el == null ? null : el.EffectiveTextForeground();
    }
}

public static class Element {
    public static void SetProjection3D(UIElement el, PlaneProjection proj) {
        if (el != null) { el.Projection3D = proj; }
    }
    public static PlaneProjection GetProjection3D(UIElement el) {
        return el == null ? null : el.Projection3D;
    }
    public static void SetTransform3D(UIElement el, CompositeTransform3D tr) {
        if (el != null) { el.Transform3D = tr; }
    }
    public static CompositeTransform3D GetTransform3D(UIElement el) {
        return el == null ? null : el.Transform3D;
    }
    public static void SetBlendingMode(UIElement el, BlendingMode mode) {
        if (el != null) { el.BlendMode = mode; }
    }
    public static BlendingMode GetBlendingMode(UIElement el) {
        return el == null ? BlendingMode.Normal : el.BlendMode;
    }
}

// ============================================================================
// 7. InputDispatcher & FocusManager (Native -> Script Event Pipeline)
// ============================================================================

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
