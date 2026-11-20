module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Decorator, Border & BoxShadow
// ============================================================================

public class Decorator : FrameworkElement {
    private UIElement _child;

    public Decorator() : base(VisualKind.Box) {
        SyncInteractiveFlags();
    }
    public override string GetTypeName() { return "Decorator"; }

    protected override bool IsSelfHitTestSolid() {
        return Background != null;
    }

    public UIElement Child {
        get { return _child; }
        set {
            if (_child == value) { return; }
            ClearChildren();
            _child = value;
            if (_child != null) { AddChild(_child); }
        }
    }
}

public class Border : Decorator {
    public Border() : base() {}
    public override string GetTypeName() { return "Border"; }

    protected override bool IsSelfHitTestSolid() {
        if (Background != null) { return true; }
        Thickness bt = BorderThickness;
        return BorderBrush != null && (bt.Left > 0.0f || bt.Top > 0.0f || bt.Right > 0.0f || bt.Bottom > 0.0f);
    }

    protected override void SyncPadding() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Thickness p = Padding;
        Thickness b = BorderThickness;
        Visual_SetPadding(
            __nativeHandle,
            p.Left + b.Left,
            p.Top + b.Top,
            p.Right + b.Right,
            p.Bottom + b.Bottom);
    }

    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        SyncPadding();
    }
}

public class BoxShadow : Decorator {
    private DropShadowEffect _shadow;
    private float _offsetX = 0.0f;
    private float _offsetY = 0.0f;

    public BoxShadow() : base() {
        _shadow = new DropShadowEffect();
        _shadow.ShadowDepth = 0.0f;
        _shadow.Opacity = 1.0f;
        _shadow.ColorRgba = ParseUtil.PackArgb(128u, 0u, 0u, 0u);
        Effect = _shadow;
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetBoxShadow(__nativeHandle, true);
        }
        SyncEffect();
    }

    public override string GetTypeName() { return "BoxShadow"; }

    private void UpdateShadowOffset() {
        float ox = _offsetX;
        float oy = _offsetY;
        float depth = Math.Sqrtf(ox * ox + oy * oy);
        _shadow.ShadowDepth = depth;
        if (depth > 0.0001f) {
            float ax = Math.Absf(ox);
            float ay = Math.Absf(oy);
            float z = ax > ay ? (ay / ax) : (ax / ay);
            float baseDeg = z * (45.0f - (z - 1.0f) * (14.0f + 3.83f * z));
            if (ax < ay) { baseDeg = 90.0f - baseDeg; }
            float deg = baseDeg;
            if (ox < 0.0f && oy >= 0.0f) { deg = 180.0f - baseDeg; }
            else if (ox < 0.0f && oy < 0.0f) { deg = 180.0f + baseDeg; }
            else if (ox >= 0.0f && oy < 0.0f) { deg = 360.0f - baseDeg; }
            _shadow.Direction = deg;
        }
        SyncEffect();
    }

    public string Color {
        get { return _shadow.Color; }
        set { _shadow.Color = value; SyncEffect(); }
    }
    public string ShadowColor {
        get { return _shadow.Color; }
        set { _shadow.Color = value; SyncEffect(); }
    }
    public float BlurRadius {
        get { return _shadow.BlurRadius; }
        set { _shadow.BlurRadius = value; SyncEffect(); }
    }
    public float ShadowDepth {
        get { return _shadow.ShadowDepth; }
        set { _shadow.ShadowDepth = value; SyncEffect(); }
    }
    public float Direction {
        get { return _shadow.Direction; }
        set { _shadow.Direction = value; SyncEffect(); }
    }
    public float ShadowOpacity {
        get { return _shadow.Opacity; }
        set { _shadow.Opacity = value; SyncEffect(); }
    }
    public float OffsetX {
        get { return _offsetX; }
        set { _offsetX = value; UpdateShadowOffset(); }
    }
    public float OffsetY {
        get { return _offsetY; }
        set { _offsetY = value; UpdateShadowOffset(); }
    }
    public float Spread { get; set; } = 0.0f;
    public bool Inside { get; set; } = false;

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Color" || propName == "ShadowColor") { Color = DependencyObject.UnboxString(value, "#7F000000"); return true; }
        if (propName == "BlurRadius") { BlurRadius = DependencyObject.UnboxFloat(value, 10.0f); return true; }
        if (propName == "OffsetX") { OffsetX = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "OffsetY") { OffsetY = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Spread") { Spread = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Inside") { Inside = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "ShadowDepth") { ShadowDepth = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Direction") { Direction = DependencyObject.UnboxFloat(value, 315.0f); return true; }
        if (propName == "ShadowOpacity") { ShadowOpacity = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

// ============================================================================
// 2. Run, Inline & TextBlock
// ============================================================================

public class Inline : DependencyObject {
    public string Text { get; set; } = "";
    public Brush Foreground { get; set; }
    public string FontFamily { get; set; } = "";
    public string FontWeight { get; set; } = "";
    public string FontStyle { get; set; } = "";
    public float FontSize { get; set; } = 0.0f;
    public int Decorations { get; set; } = 0;
    internal TextBlock OwnerTextBlock;
    internal DependencyObject _ownBindingsBox;

    public virtual bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Text") {
            Text = DependencyObject.UnboxString(value, "");
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "Foreground") {
            Foreground = UIElement.CoerceBrush(value);
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "FontFamily") {
            FontFamily = DependencyObject.UnboxString(value, "");
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "FontWeight") {
            FontWeight = DependencyObject.UnboxString(value, "Normal");
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "FontStyle") {
            FontStyle = DependencyObject.UnboxString(value, "Normal");
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "FontSize") {
            FontSize = DependencyObject.UnboxFloat(value, 14.0f);
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        if (propName == "TextDecorations") {
            Decorations = DependencyObject.UnboxInt(value, 0);
            if (OwnerTextBlock != null) { OwnerTextBlock.NotifyInlineChanged(); }
            return true;
        }
        return false;
    }

    public virtual DependencyObject GetPropertyValue(string propName) {
        if (propName == "Text") { return DependencyObject.BoxString(Text); }
        if (propName == "Foreground") { return Foreground; }
        if (propName == "FontFamily") { return DependencyObject.BoxString(FontFamily); }
        if (propName == "FontWeight") { return DependencyObject.BoxString(FontWeight); }
        if (propName == "FontStyle") { return DependencyObject.BoxString(FontStyle); }
        if (propName == "FontSize") { return DependencyObject.BoxFloat(FontSize); }
        if (propName == "TextDecorations") { return DependencyObject.BoxInt(Decorations); }
        return null;
    }

    internal Inline CloneStyleState() {
        var c = new Inline();
        c.CopyStyleFrom(this);
        return c;
    }

    internal void CopyStyleFrom(Inline other) {
        if (other == null) { return; }
        FontWeight = other.FontWeight;
        FontStyle = other.FontStyle;
        FontSize = other.FontSize;
        FontFamily = other.FontFamily;
        Foreground = other.Foreground;
        Decorations = other.Decorations;
    }
}

public class Run : Inline {
    public Run() {}
    public Run(string text) { Text = text == null ? "" : text; }
    public override string ToDisplayString() { return Text; }
}

public class LineBreak : Inline {
    public LineBreak() { Text = "\n"; }
}

public class TextBlock : FrameworkElement {
    private string _text = "";
    private string _fontFamily = "Segoe UI";
    private float _fontSize = 14.0f;
    private string _fontWeight = "Normal";
    private string _fontStyle = "Normal";
    private Brush _foreground;
    private TextWrapping _textWrapping = TextWrapping.NoWrap;
    private TextAlignment _textAlignment = TextAlignment.Left;
    private TextTrimming _textTrimming = TextTrimming.None;
    private float _lineHeight = 0.0f;
    private float _charSpacing = 0.0f;
    private int _maxLines = 0;
    private int _decorations = 0;
    private Inline[] _inlines;
    private bool _hasExplicitFontSize = false;
    private bool _hasExplicitFontFamily = false;
    private bool _hasExplicitFontWeight = false;
    private bool _hasExplicitForeground = false;
    internal string _rawRichText = "";
    internal bool _richNeedsResolve = false;
    internal static Action<TextBlock, string> _richTextHandler;

    public TextBlock() : base(VisualKind.Text) {
        _inlines = new Inline[0];
        _foreground = Brushes.DefaultForeground();
        SyncTextToNative();
    }
    public TextBlock(string text) : base(VisualKind.Text) {
        _inlines = new Inline[0];
        _foreground = Brushes.DefaultForeground();
        _text = text == null ? "" : text;
        SyncTextToNative();
    }

    public override string GetTypeName() { return "TextBlock"; }

    public string Text {
        get { return _text; }
        set {
            string v = value == null ? "" : value;
            _rawRichText = "";
            _richNeedsResolve = false;
            if (_inlines != null && _inlines.Length > 0) {
                for (int k = 0; k < _inlines.Length; k += 1) {
                    if (_inlines[k] != null) { _inlines[k].OwnerTextBlock = null; }
                }
                _inlines.Clear();
                _text = v;
                SyncTextToNative();
                return;
            }
            if (_text == v) { return; }
            _text = v;
            SyncTextToNative();
        }
    }
    public string FontFamily {
        get { return _fontFamily; }
        set { _hasExplicitFontFamily = true; _fontFamily = value == null ? "Segoe UI" : value; SyncTextToNative(); }
    }
    public float FontSize {
        get { return _fontSize; }
        set { _hasExplicitFontSize = true; _fontSize = value > 0.0f ? value : 14.0f; SyncTextToNative(); }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set { _hasExplicitFontWeight = true; _fontWeight = value == null ? "Normal" : value; SyncTextToNative(); }
    }
    public string FontStyle {
        get { return _fontStyle; }
        set { _fontStyle = value == null ? "Normal" : value; SyncTextToNative(); }
    }
    public Brush Foreground {
        get { return _foreground; }
        set { _hasExplicitForeground = true; _foreground = value; SyncTextToNative(); }
    }

    internal override void OnInheritedTypographyChanged() {
        bool changed = false;
        if (!_hasExplicitFontSize) {
            float fs = EffectiveTextFontSizeOrZero();
            float nextFs = fs > 0.0f ? fs : 14.0f;
            if (_fontSize != nextFs) { _fontSize = nextFs; changed = true; }
        }
        if (!_hasExplicitFontFamily) {
            string ff = EffectiveTextFontFamilyOrEmpty();
            string nextFf = ff.Length > 0 ? ff : "Segoe UI";
            if (_fontFamily != nextFf) { _fontFamily = nextFf; changed = true; }
        }
        if (!_hasExplicitFontWeight) {
            string fw = EffectiveTextFontWeightOrEmpty();
            string nextFw = fw.Length > 0 ? fw : "Normal";
            if (_fontWeight != nextFw) { _fontWeight = nextFw; changed = true; }
        }
        if (!_hasExplicitForeground) {
            Brush fg = EffectiveTextForeground();
            Brush nextFg = fg != null ? fg : Brushes.DefaultForeground();
            if (_foreground != nextFg) { _foreground = nextFg; changed = true; }
        }
        if (changed) { SyncTextToNative(); }
    }
    public TextWrapping TextWrapping {
        get { return _textWrapping; }
        set { _textWrapping = value; SyncTextToNative(); }
    }
    public TextAlignment TextAlignment {
        get { return _textAlignment; }
        set { _textAlignment = value; SyncTextToNative(); }
    }
    public TextTrimming TextTrimming {
        get { return _textTrimming; }
        set { _textTrimming = value; SyncTextToNative(); }
    }
    public float LineHeight {
        get { return _lineHeight; }
        set { _lineHeight = value; SyncTextMetricsToNative(); }
    }
    public float CharacterSpacing {
        get { return _charSpacing; }
        set { _charSpacing = value; SyncTextMetricsToNative(); }
    }
    public int MaxLines {
        get { return _maxLines; }
        set { _maxLines = value; SyncTextMetricsToNative(); }
    }
    public int TextDecorations {
        get { return _decorations; }
        set { _decorations = value; SyncTextMetricsToNative(); }
    }

    public Inline[] Inlines() { return _inlines; }

    public void AddInline(Inline inline) {
        if (inline == null) { return; }
        inline.OwnerTextBlock = this;
        _inlines.Push(inline);
        RebuildFromInlines();
    }
    internal void NotifyInlineChanged() { RebuildFromInlines(); }
    public void AddRun(Run run) { AddInline(run); }
    public void ClearInlines() {
        for (int i = 0; i < _inlines.Length; i += 1) {
            if (_inlines[i] != null) { _inlines[i].OwnerTextBlock = null; }
        }
        _inlines.Clear();
        _text = "";
        SyncTextToNative();
    }

    private void RebuildFromInlines() {
        string combined = "";
        for (int i = 0; i < _inlines.Length; i += 1) {
            combined = combined + _inlines[i].Text;
        }
        _text = combined;
        SyncTextToNative();
    }

    public Size Measure(float maxWidth = 100000.0f) {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return new Size(0.0f, 0.0f); }
        float w = Visual_MeasureText(_text, _fontFamily, _fontSize, maxWidth, EnumConvert.ToInt(_textWrapping), 0);
        float h = Visual_MeasureText(_text, _fontFamily, _fontSize, maxWidth, EnumConvert.ToInt(_textWrapping), 1);
        return new Size(w, h);
    }

    private static int ParseFontWeightOrZero(string w) {
        if (w == null || w.Length == 0) { return 0; }
        string s = w.Trim();
        if (s.Length == 0) { return 0; }
        if (s == "Bold" || s == "bold") { return 700; }
        if (s == "SemiBold" || s == "semibold" || s == "DemiBold" || s == "demibold") { return 600; }
        if (s == "Normal" || s == "normal" || s == "Regular" || s == "regular") { return 400; }
        if (s == "Medium" || s == "medium") { return 500; }
        if (s == "ExtraBold" || s == "extrabold" || s == "UltraBold" || s == "ultrabold") { return 800; }
        if (s == "Black" || s == "black" || s == "Heavy" || s == "heavy") { return 900; }
        if (s == "Light" || s == "light") { return 300; }
        if (s == "ExtraLight" || s == "extralight" || s == "UltraLight" || s == "ultralight") { return 200; }
        if (s == "Thin" || s == "thin") { return 100; }
        int numeric = ParseUtil.ParseInt(s, 0);
        return numeric > 0 ? numeric : 400;
    }

    private static int ParseFontWeight(string w) {
        int v = ParseFontWeightOrZero(w);
        return v > 0 ? v : 400;
    }

    private static bool IsItalicStyle(string s) {
        if (s == null || s.Length == 0) { return false; }
        string t = s.Trim();
        return t == "Italic" || t == "italic" || t == "Oblique" || t == "oblique";
    }

    protected void SyncTextToNative() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        uint fg = _foreground != null ? _foreground.ResolveRgbaUnscaled() : ParseUtil.PackArgb(255u, 234u, 236u, 239u);
        int fgGradId = _foreground != null ? _foreground.ResolveGradIdUnscaled() : -1;
        float fgOp = _foreground != null ? Math.Clampf(_foreground.Opacity, 0.0f, 1.0f) : 1.0f;
        Visual_SetText(
            __nativeHandle,
            _text,
            _fontFamily,
            _fontSize,
            fg,
            EnumConvert.ToInt(_textWrapping),
            EnumConvert.ToInt(_textAlignment),
            EnumConvert.ToInt(_textTrimming));
        Visual_SetTextStyle(
            __nativeHandle,
            ParseFontWeight(_fontWeight),
            IsItalicStyle(_fontStyle),
            fgGradId);
        Visual_SetTextBrushOpacity(__nativeHandle, fgOp);
        Visual_ClearTextSpans(__nativeHandle);
        if (_inlines != null && _inlines.Length > 0) {
            for (int i = 0; i < _inlines.Length; i += 1) {
                Inline inl = _inlines[i];
                if (inl == null) { continue; }
                bool hasFg = inl.Foreground != null;
                uint spanFg = hasFg ? inl.Foreground.ResolveRgba() : 0u;
                int spanItalic = inl.FontStyle.Length == 0 ? -1 : (IsItalicStyle(inl.FontStyle) ? 1 : 0);
                Visual_AddTextSpan(
                    __nativeHandle,
                    inl.Text,
                    inl.FontFamily,
                    inl.FontSize,
                    ParseFontWeightOrZero(inl.FontWeight),
                    spanItalic,
                    spanFg,
                    hasFg,
                    inl.Decorations);
            }
        }
    }

    protected void SyncTextMetricsToNative() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Visual_SetTextMetrics(__nativeHandle, _lineHeight, _charSpacing, _maxLines, _decorations);
    }

    public Brush Stroke { get; set; }
    public float StrokeThickness { get; set; } = 0.0f;

    internal void ClearRichInlines() {
        for (int k = 0; k < _inlines.Length; k += 1) {
            if (_inlines[k] != null) { _inlines[k].OwnerTextBlock = null; }
        }
        _inlines.Clear();
    }

    internal void PushRichInline(Inline inl) {
        if (inl == null) { return; }
        inl.OwnerTextBlock = this;
        _inlines.Push(inl);
    }

    internal void FinishRichText(bool emptyClear) {
        if (emptyClear) {
            _text = "";
            SyncTextToNative();
        } else {
            RebuildFromInlines();
        }
    }

    protected override void OnLoaded() {
        base.OnLoaded();
        if (_rawRichText.Length > 0 && _richNeedsResolve && _richTextHandler != null) {
            _richTextHandler(this, _rawRichText);
        }
        if (_inlines != null && _inlines.Length > 0) {
            BindingEngine.FlushInlinesFor(this);
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Text") { Text = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "RichText.Text" || propName == "noesis:RichText.Text") {
            string raw = DependencyObject.UnboxString(value, "");
            if (_richTextHandler != null) {
                _richTextHandler(this, raw);
            } else {
                Text = raw;
            }
            return true;
        }
        if (propName == "FontFamily") { FontFamily = DependencyObject.UnboxString(value, "Segoe UI"); return true; }
        if (propName == "FontSize") { FontSize = DependencyObject.UnboxFloat(value, 14.0f); return true; }
        if (propName == "FontWeight") { FontWeight = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "FontStyle") { FontStyle = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "Foreground" || propName == "Foreground.Color") { Foreground = UIElement.CoerceBrush(value); return true; }
        if (propName == "Stroke") { Stroke = UIElement.CoerceBrush(value); return true; }
        if (propName == "StrokeThickness") { StrokeThickness = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "LineHeight") { LineHeight = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "TextWrapping") {
            if (value is BoxedInt) {
                int v = (value as BoxedInt).Value;
                TextWrapping = v == 1 ? TextWrapping.Wrap : (v == 2 ? TextWrapping.WrapWithOverflow : TextWrapping.NoWrap);
                return true;
            }
            string tw = DependencyObject.UnboxString(value, "NoWrap");
            TextWrapping = tw == "Wrap" ? TextWrapping.Wrap : (tw == "WrapWithOverflow" ? TextWrapping.WrapWithOverflow : TextWrapping.NoWrap);
            return true;
        }
        if (propName == "TextAlignment") {
            string ta = DependencyObject.UnboxString(value, "Left");
            if (ta == "Center") { TextAlignment = TextAlignment.Center; return true; }
            if (ta == "Right") { TextAlignment = TextAlignment.Right; return true; }
            if (ta == "Justify") { TextAlignment = TextAlignment.Justify; return true; }
            TextAlignment = TextAlignment.Left;
            return true;
        }
        if (propName == "TextTrimming") {
            string tt = DependencyObject.UnboxString(value, "None");
            if (tt == "CharacterEllipsis") { TextTrimming = TextTrimming.CharacterEllipsis; return true; }
            if (tt == "WordEllipsis") { TextTrimming = TextTrimming.WordEllipsis; return true; }
            TextTrimming = TextTrimming.None;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Text") { return DependencyObject.BoxString(_text); }
        if (propName == "FontSize") { return DependencyObject.BoxFloat(_fontSize); }
        if (propName == "FontFamily") { return DependencyObject.BoxString(_fontFamily); }
        if (propName == "Foreground") { return _foreground; }
        if (propName == "Foreground.Color") {
            return DependencyObject.BoxString(_foreground != null ? ParseUtil.FormatArgbHex(_foreground.ResolveRgba()) : "#FFEAECEF");
        }
        if (propName == "Stroke") { return Stroke; }
        if (propName == "StrokeThickness") { return DependencyObject.BoxFloat(StrokeThickness); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 3. Image
// ============================================================================

public class Image : FrameworkElement {
    private string _source = "";
    private Stretch _stretch = Stretch.Uniform;
    private StretchDirection _stretchDir = StretchDirection.Both;
    private Brush _tint;

    public Image() : base(VisualKind.Image) {
        SyncImageToNative();
    }
    public Image(string sourceUri) : base(VisualKind.Image) {
        _source = sourceUri == null ? "" : sourceUri;
        SyncImageToNative();
    }

    public override string GetTypeName() { return "Image"; }

    public string Source {
        get { return _source; }
        set { _source = value == null ? "" : value; SyncImageToNative(); }
    }
    public void SetSource(string uri) { Source = uri; }

    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; SyncImageToNative(); }
    }
    public StretchDirection StretchDirection {
        get { return _stretchDir; }
        set { _stretchDir = value; SyncImageToNative(); }
    }
    public Brush Tint {
        get { return _tint; }
        set { _tint = value; SyncImageToNative(); }
    }

    protected void SyncImageToNative() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        uint tintCol = _tint != null ? _tint.ResolveRgba() : ParseUtil.PackArgb(255u, 255u, 255u, 255u);
        Visual_SetImage(
            __nativeHandle,
            _source,
            EnumConvert.ToInt(_stretch),
            EnumConvert.ToInt(_stretchDir),
            tintCol);
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Source") { Source = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Stretch") {
            if (value is BoxedInt) { Stretch = EnumConvert.ToStretch((value as BoxedInt).Value); return true; }
            string st = DependencyObject.UnboxString(value, "Uniform");
            if (st == "None") { Stretch = Stretch.None; return true; }
            if (st == "Fill") { Stretch = Stretch.Fill; return true; }
            if (st == "UniformToFill") { Stretch = Stretch.UniformToFill; return true; }
            Stretch = Stretch.Uniform;
            return true;
        }
        if (propName == "Tint") { Tint = UIElement.CoerceBrush(value); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

// ============================================================================
// 4. Vector Shapes (Shape, Rectangle, Ellipse, Line, Path, Polygon, Polyline)
// ============================================================================

public class Shape : FrameworkElement {
    private Brush _fill;
    private Brush _stroke;
    private float _fillOpacity = 1.0f;
    private float _strokeOpacity = 1.0f;
    private bool _ownsClonedFill = false;
    private bool _ownsClonedStroke = false;
    private bool _hadFillImage = false;
    private float _strokeThickness = 1.0f;
    private Stretch _stretch = Stretch.Fill;
    private float _trimStart = 0.0f;
    private float _trimEnd = 1.0f;

    protected Shape() : base(VisualKind.Vector) {
        SyncInteractiveFlags();
    }
    public override string GetTypeName() { return "Shape"; }

    protected override bool IsSelfHitTestSolid() {
        return _fill != null || (_stroke != null && _strokeThickness > 0.0f);
    }

    public Brush Fill {
        get { return _fill; }
        set {
            _fill = value;
            _ownsClonedFill = false;
            _fillOpacity = value != null ? Math.Clampf(value.Opacity, 0.0f, 1.0f) : 1.0f;
            SyncVectorShape();
            SyncVectorBrushOpacity();
        }
    }
    public Brush Stroke {
        get { return _stroke; }
        set {
            _stroke = value;
            _ownsClonedStroke = false;
            _strokeOpacity = value != null ? Math.Clampf(value.Opacity, 0.0f, 1.0f) : 1.0f;
            SyncVectorShape();
            SyncVectorBrushOpacity();
        }
    }
    public float StrokeThickness {
        get { return _strokeThickness; }
        set { _strokeThickness = value >= 0.0f ? value : 0.0f; SyncVectorShape(); }
    }
    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; SyncVectorShape(); }
    }
    public float TrimStart {
        get { return _trimStart; }
        set { _trimStart = Math.Clampf(value, 0.0f, 1.0f); SyncVectorShape(); }
    }
    public float TrimEnd {
        get { return _trimEnd; }
        set { _trimEnd = Math.Clampf(value, 0.0f, 1.0f); SyncVectorShape(); }
    }

    protected virtual VectorShapeKind GetVectorKind() { return VectorShapeKind.SvgPath; }
    protected virtual string BuildVectorGeometrySpec() { return ""; }

    public void SyncVectorBrushOpacity() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Visual_SetVectorBrushOpacity(__nativeHandle, _fillOpacity, _strokeOpacity);
    }

    public void SyncVectorShape() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        uint fillCol = 0u;
        int fillGrad = -1;
        if (_fill is ImageBrush) {
            ImageBrush ib = _fill as ImageBrush;
            Visual_SetImage(
                __nativeHandle,
                ib.ImageSource,
                EnumConvert.ToInt(ib.Stretch),
                EnumConvert.ToInt(StretchDirection.Both),
                ParseUtil.PackArgb(255u, 255u, 255u, 255u));
            UIElement.ApplyImageBrushViewbox(__nativeHandle, ib.Viewbox);
            _hadFillImage = true;
        } else {
            if (_hadFillImage) {
                UIElement.ClearImageBrushFromNative(__nativeHandle);
                _hadFillImage = false;
            }
            if (_fill != null) {
                fillCol = _fill.ResolveRgbaUnscaled();
                fillGrad = _fill.ResolveGradIdUnscaled();
            }
        }
        uint strokeCol = _stroke != null ? _stroke.ResolveRgbaUnscaled() : 0u;
        int strokeGrad = _stroke != null ? _stroke.ResolveGradIdUnscaled() : -1;
        Visual_SetVectorShape(
            __nativeHandle,
            EnumConvert.ToInt(GetVectorKind()),
            BuildVectorGeometrySpec(),
            fillCol, fillGrad,
            strokeCol, strokeGrad,
            _strokeThickness,
            EnumConvert.ToInt(_stretch),
            _trimStart, _trimEnd);
        SyncInteractiveFlags();
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Fill") { Fill = UIElement.CoerceBrush(value); return true; }
        if (propName == "Stroke") { Stroke = UIElement.CoerceBrush(value); return true; }
        if (propName == "Fill.ImageSource" && _fill is ImageBrush) {
            (_fill as ImageBrush).ImageSource = DependencyObject.UnboxString(value, "");
            SyncVectorShape();
            return true;
        }
        if (propName == "Fill.Opacity") {
            _fillOpacity = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
            SyncVectorBrushOpacity();
            return true;
        }
        if (propName == "Stroke.Opacity") {
            _strokeOpacity = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
            SyncVectorBrushOpacity();
            return true;
        }
        if (propName == "Fill.Color") {
            Brush b = UIElement.CoerceBrush(value);
            if (b != null) { b.Opacity = _fillOpacity; }
            _fill = b;
            _ownsClonedFill = false;
            SyncVectorShape();
            return true;
        }
        if (propName == "Stroke.Color") {
            Brush b = UIElement.CoerceBrush(value);
            if (b != null) { b.Opacity = _strokeOpacity; }
            _stroke = b;
            _ownsClonedStroke = false;
            SyncVectorShape();
            return true;
        }
        if (propName.Length > 5 && propName.Substring(0, 5) == "Fill.") {
            string suffix = propName.Substring(5);
            if (_fill is GradientBrush && GradientBrush.ParseStopSlotIndex(suffix) >= 0) {
                GradientBrush gb = _fill as GradientBrush;
                if (!_ownsClonedFill) {
                    gb = gb.CloneBrush();
                    _fill = gb;
                    _ownsClonedFill = true;
                }
                gb.TryApplyStopProperty(suffix, value);
                SyncVectorShape();
                return true;
            }
        }
        if (propName.Length > 7 && propName.Substring(0, 7) == "Stroke.") {
            string suffix = propName.Substring(7);
            if (_stroke is GradientBrush && GradientBrush.ParseStopSlotIndex(suffix) >= 0) {
                GradientBrush gb = _stroke as GradientBrush;
                if (!_ownsClonedStroke) {
                    gb = gb.CloneBrush();
                    _stroke = gb;
                    _ownsClonedStroke = true;
                }
                gb.TryApplyStopProperty(suffix, value);
                SyncVectorShape();
                return true;
            }
        }
        if (propName == "StrokeThickness") { StrokeThickness = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "Stretch") {
            if (value is BoxedInt) { Stretch = EnumConvert.ToStretch((value as BoxedInt).Value); return true; }
            string st = DependencyObject.UnboxString(value, "Fill").Trim();
            if (st == "None" || st == "0") { Stretch = Stretch.None; return true; }
            if (st == "Uniform" || st == "2") { Stretch = Stretch.Uniform; return true; }
            if (st == "UniformToFill" || st == "3") { Stretch = Stretch.UniformToFill; return true; }
            Stretch = Stretch.Fill;
            return true;
        }
        if (propName == "TrimStart") { TrimStart = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "TrimEnd") { TrimEnd = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Fill") { return _fill; }
        if (propName == "Fill.ImageSource" && _fill is ImageBrush) {
            return DependencyObject.BoxString((_fill as ImageBrush).ImageSource);
        }
        if (propName == "Fill.Opacity") { return DependencyObject.BoxFloat(_fillOpacity); }
        if (propName == "Fill.Color") {
            return DependencyObject.BoxString(_fill != null ? ParseUtil.FormatArgbHex(_fill.ResolveRgbaUnscaled()) : "#00000000");
        }
        if (propName == "Stroke") { return _stroke; }
        if (propName == "Stroke.Opacity") { return DependencyObject.BoxFloat(_strokeOpacity); }
        if (propName == "Stroke.Color") {
            return DependencyObject.BoxString(_stroke != null ? ParseUtil.FormatArgbHex(_stroke.ResolveRgbaUnscaled()) : "#00000000");
        }
        if (propName.Length > 5 && propName.Substring(0, 5) == "Fill." && _fill is GradientBrush) {
            DependencyObject stopBox = (_fill as GradientBrush).TryReadStopProperty(propName.Substring(5));
            if (stopBox != null) { return stopBox; }
        }
        if (propName.Length > 7 && propName.Substring(0, 7) == "Stroke." && _stroke is GradientBrush) {
            DependencyObject stopBox = (_stroke as GradientBrush).TryReadStopProperty(propName.Substring(7));
            if (stopBox != null) { return stopBox; }
        }
        if (propName == "StrokeThickness") { return DependencyObject.BoxFloat(_strokeThickness); }
        if (propName == "Stretch") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretch)); }
        return base.GetPropertyValue(propName);
    }
}

public class Rectangle : Shape {
    private float _radiusX;
    private float _radiusY;

    public Rectangle() : base() { SyncVectorShape(); }
    public override string GetTypeName() { return "Rectangle"; }

    public float RadiusX {
        get { return _radiusX; }
        set { _radiusX = value >= 0.0f ? value : 0.0f; SyncVectorShape(); }
    }
    public float RadiusY {
        get { return _radiusY; }
        set { _radiusY = value >= 0.0f ? value : 0.0f; SyncVectorShape(); }
    }

    protected override VectorShapeKind GetVectorKind() { return VectorShapeKind.Rectangle; }

    protected override string BuildVectorGeometrySpec() {
        return "" + _radiusX + "," + _radiusY;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "RadiusX") { RadiusX = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "RadiusY") { RadiusY = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "RadiusX") { return DependencyObject.BoxFloat(_radiusX); }
        if (propName == "RadiusY") { return DependencyObject.BoxFloat(_radiusY); }
        return base.GetPropertyValue(propName);
    }
}

public class Ellipse : Shape {
    public Ellipse() : base() { SyncVectorShape(); }
    public override string GetTypeName() { return "Ellipse"; }
    protected override VectorShapeKind GetVectorKind() { return VectorShapeKind.Ellipse; }
}

public class Line : Shape {
    private float _x1;
    private float _y1;
    private float _x2;
    private float _y2;

    public Line() : base() { Stretch = Stretch.None; SyncVectorShape(); }
    public Line(float x1, float y1, float x2, float y2) : base() {
        _x1 = x1; _y1 = y1; _x2 = x2; _y2 = y2;
        Stretch = Stretch.None;
        SyncVectorShape();
    }

    public override string GetTypeName() { return "Line"; }
    public float X1 { get { return _x1; } set { _x1 = value; SyncVectorShape(); } }
    public float Y1 { get { return _y1; } set { _y1 = value; SyncVectorShape(); } }
    public float X2 { get { return _x2; } set { _x2 = value; SyncVectorShape(); } }
    public float Y2 { get { return _y2; } set { _y2 = value; SyncVectorShape(); } }

    protected override VectorShapeKind GetVectorKind() { return VectorShapeKind.Line; }
    protected override string BuildVectorGeometrySpec() {
        return "" + _x1 + "," + _y1 + "," + _x2 + "," + _y2;
    }
}

public class Path : Shape {
    private string _data = "";

    public Path() : base() {
        Stretch = Stretch.None;
        SyncVectorShape();
    }
    public Path(string svgPath) : base() {
        _data = svgPath == null ? "" : svgPath;
        Stretch = Stretch.None;
        SyncVectorShape();
    }

    public override string GetTypeName() { return "Path"; }
    public string Data {
        get { return _data; }
        set { _data = value == null ? "" : value; SyncVectorShape(); }
    }
    protected override string BuildVectorGeometrySpec() { return _data; }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Data") {
            if (value is Geometry) {
                Data = (value as Geometry).Data;
                return true;
            }
            if (value != null) {
                object raw = value.UnboxObject();
                if (raw is Geometry) {
                    Data = (raw as Geometry).Data;
                    return true;
                }
            }
            Data = DependencyObject.UnboxString(value, "");
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Data") { return DependencyObject.BoxString(_data); }
        return base.GetPropertyValue(propName);
    }
    public override string ToDisplayString() { return "path:" + _data; }
}

public class Geometry : Path {
    public Geometry() : base() { Stretch = Stretch.Uniform; }
    public Geometry(string svgPath) : base(svgPath) { Stretch = Stretch.Uniform; }
    public override string GetTypeName() { return "Geometry"; }
}

public class PolyShapeBase : Shape {
    private Point[] _points;

    protected PolyShapeBase() : base() {
        _points = new Point[0];
        Stretch = Stretch.None;
        SyncVectorShape();
    }

    protected virtual bool IsClosed() { return false; }

    public Point[] PointList() { return _points; }

    public void AddPoint(float x, float y) {
        _points.Push(new Point(x, y));
        SyncVectorShape();
    }
    public void ClearPoints() {
        _points.Clear();
        SyncVectorShape();
    }

    public string Points {
        get {
            string s = "";
            for (int i = 0; i < _points.Length; i += 1) {
                if (i > 0) { s = s + " "; }
                s = s + _points[i].X + "," + _points[i].Y;
            }
            return s;
        }
        set {
            _points.Clear();
            if (value != null && value.Length > 0) {
                string[] pairs = value.Trim().Split(" ");
                for (int i = 0; i < pairs.Length; i += 1) {
                    string p = pairs[i].Trim();
                    if (p.Length == 0) { continue; }
                    string[] xy = p.Split(",");
                    if (xy.Length >= 2) {
                        _points.Push(new Point(
                            ParseUtil.ParseFloat(xy[0], 0.0f),
                            ParseUtil.ParseFloat(xy[1], 0.0f)));
                    }
                }
            }
            SyncVectorShape();
        }
    }

    protected override string BuildVectorGeometrySpec() {
        if (_points.Length == 0) { return ""; }
        string svg = "M " + _points[0].X + "," + _points[0].Y;
        for (int i = 1; i < _points.Length; i += 1) {
            svg = svg + " L " + _points[i].X + "," + _points[i].Y;
        }
        if (IsClosed() && _points.Length >= 2) { svg = svg + " Z"; }
        return svg;
    }
}

public class Polygon : PolyShapeBase {
    public Polygon() : base() {}
    public override string GetTypeName() { return "Polygon"; }
    protected override bool IsClosed() { return true; }
}

public class Polyline : PolyShapeBase {
    public Polyline() : base() {}
    public override string GetTypeName() { return "Polyline"; }
    protected override bool IsClosed() { return false; }
}

// ============================================================================
// 5. DrawingContext & CustomDrawElement (Imperative 2D Canvas API)
// ============================================================================

public class DrawingContext : DependencyObject {
    private handle _node;

    public DrawingContext(handle visualNode) {
        _node = visualNode;
    }

    public void FillRect(float x, float y, float w, float h, uint rgba, int gradId = -1) {
        Draw_FillRect(_node, x, y, w, h, rgba, gradId);
    }
    public void FillRect(float x, float y, float w, float h, Brush brush) {
        if (brush == null) { return; }
        Draw_FillRect(_node, x, y, w, h, brush.ResolveRgba(), brush.ResolveGradId());
    }
    public void StrokeRect(float x, float y, float w, float h, uint rgba, float strokeWidth = 1.0f) {
        Draw_StrokeRect(_node, x, y, w, h, rgba, strokeWidth);
    }
    public void StrokeRect(float x, float y, float w, float h, Brush brush, float strokeWidth = 1.0f) {
        if (brush == null) { return; }
        Draw_StrokeRect(_node, x, y, w, h, brush.ResolveRgba(), strokeWidth);
    }
    public void FillRoundedRect(float x, float y, float w, float h, float rx, float ry, uint rgba, int gradId = -1) {
        Draw_FillRoundedRect(_node, x, y, w, h, rx, ry, rgba, gradId);
    }
    public void FillRoundedRect(float x, float y, float w, float h, float rx, float ry, Brush brush) {
        if (brush == null) { return; }
        Draw_FillRoundedRect(_node, x, y, w, h, rx, ry, brush.ResolveRgba(), brush.ResolveGradId());
    }
    public void StrokeRoundedRect(float x, float y, float w, float h, float rx, float ry, uint rgba, float strokeWidth = 1.0f) {
        Draw_StrokeRoundedRect(_node, x, y, w, h, rx, ry, rgba, strokeWidth);
    }
    public void FillEllipse(float cx, float cy, float rx, float ry, uint rgba, int gradId = -1) {
        Draw_FillEllipse(_node, cx, cy, rx, ry, rgba, gradId);
    }
    public void FillEllipse(float cx, float cy, float rx, float ry, Brush brush) {
        if (brush == null) { return; }
        Draw_FillEllipse(_node, cx, cy, rx, ry, brush.ResolveRgba(), brush.ResolveGradId());
    }
    public void StrokeEllipse(float cx, float cy, float rx, float ry, uint rgba, float strokeWidth = 1.0f) {
        Draw_StrokeEllipse(_node, cx, cy, rx, ry, rgba, strokeWidth);
    }
    public void DrawLine(float x1, float y1, float x2, float y2, uint rgba, float strokeWidth = 1.0f) {
        Draw_Line(_node, x1, y1, x2, y2, rgba, strokeWidth);
    }
    public void DrawLine(float x1, float y1, float x2, float y2, Brush brush, float strokeWidth = 1.0f) {
        if (brush == null) { return; }
        Draw_Line(_node, x1, y1, x2, y2, brush.ResolveRgba(), strokeWidth);
    }
    public void DrawPath(string svgPath, float x, float y, float w, float h, uint fillRgba, uint strokeRgba, float strokeWidth = 1.0f) {
        Draw_Path(_node, svgPath, x, y, w, h, fillRgba, strokeRgba, strokeWidth);
    }
    public void DrawText(
        string text, float x, float y, float w, float h,
        string fontFamily, float fontSize, uint rgba,
        HorizontalAlignment hAlign = HorizontalAlignment.Left,
        VerticalAlignment vAlign = VerticalAlignment.Top) {
        Draw_Text(_node, text, x, y, w, h, fontFamily, fontSize, rgba, EnumConvert.ToInt(hAlign), EnumConvert.ToInt(vAlign));
    }
    public void PushClip(float x, float y, float w, float h) { Draw_PushClip(_node, x, y, w, h); }
    public void PopClip() { Draw_PopClip(_node); }
    public void PushTransform(float tx, float ty, float sx = 1.0f, float sy = 1.0f, float rotDeg = 0.0f) {
        Draw_PushTransform(_node, tx, ty, sx, sy, rotDeg);
    }
    public void PopTransform() { Draw_PopTransform(_node); }
}

public class CustomDrawElement : FrameworkElement {
    private Action<DrawingContext> _renderCallback;

    public CustomDrawElement() : base(VisualKind.CanvasDraw) {}
    public CustomDrawElement(Action<DrawingContext> onRender) : base(VisualKind.CanvasDraw) {
        _renderCallback = onRender;
    }

    public override string GetTypeName() { return "CustomDrawElement"; }

    protected override int GetCallbackMask() {
        return VisualCallbacks.StandardInput() + VisualCallbacks.Draw() + VisualCallbacks.CustomMeasure();
    }

    public Action<DrawingContext> RenderCallback {
        get { return _renderCallback; }
        set { _renderCallback = value; InvalidateDraw(); }
    }

    public void InvalidateDraw() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_InvalidateDraw(__nativeHandle);
        }
    }

    public virtual void OnRender(DrawingContext dc) {}

    public override void OnRenderVisual() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Draw_Begin(__nativeHandle);
        var dc = new DrawingContext(__nativeHandle);
        OnRender(dc);
        if (_renderCallback != null) { _renderCallback(dc); }
        Draw_End(__nativeHandle);
    }
}
