module Aero.UI;
import Aero.Native;

public enum Visibility {
    Visible = 0,
    Hidden = 1,
    Collapsed = 2
}

public enum Orientation {
    Horizontal = 0,
    Vertical = 1
}

public enum HorizontalAlignment {
    Stretch = 0,
    Left = 1,
    Center = 2,
    Right = 3
}

public enum VerticalAlignment {
    Stretch = 0,
    Top = 1,
    Center = 2,
    Bottom = 3
}

public enum Stretch {
    None = 0,
    Fill = 1,
    Uniform = 2,
    UniformToFill = 3
}

public enum StretchDirection {
    UpOnly = 0,
    DownOnly = 1,
    Both = 2
}

public enum ExpandDirection {
    Down = 0,
    Up = 1,
    Left = 2,
    Right = 3
}

public enum Dock {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3
}

public enum ClickMode {
    Release = 0,
    Press = 1,
    Hover = 2
}

public enum SelectionMode {
    Single = 0,
    Multiple = 1,
    Extended = 2
}

public enum TabStripPlacement {
    Top = 0,
    Bottom = 1,
    Left = 2,
    Right = 3
}

public enum ScrollBarVisibility {
    Disabled = 0,
    Auto = 1,
    Hidden = 2,
    Visible = 3
}

public enum TextWrapping {
    NoWrap = 0,
    Wrap = 1,
    WrapWithOverflow = 2
}

public enum TextAlignment {
    Left = 0,
    Center = 1,
    Right = 2,
    Justify = 3
}

public enum TextTrimming {
    None = 0,
    CharacterEllipsis = 1,
    WordEllipsis = 2
}

public enum GridUnitType {
    Auto = 0,
    Pixel = 1,
    Star = 2
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
    Saturation = 8
}

public enum BlendingMode {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Additive = 3
}

public enum VisualKind {
    Box = 0,
    Text = 1,
    Image = 2,
    Vector = 3,
    CanvasDraw = 4
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
    CustomCallback = 9
}

public enum VectorShapeKind {
    SvgPath = 0,
    Ellipse = 1,
    Line = 2,
    RadialArc = 3
}
