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
    public Border() : base() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetNodeVisualFlags(__nativeHandle, false, false, true);
        }
    }
    public override string GetTypeName() { return "Border"; }

    protected override bool IsSelfHitTestSolid() {
        if (Background != null) { return true; }
        return BorderBrush != null && ParseUtil.IsValidHandle(__nativeHandle) && Visual_GetNodeFloat(__nativeHandle, 18) > 0.0f;
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
        float oy = -_offsetY;
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
    internal override int FastPropEpochCategory() { return 2; }
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
    private static Inline[] _emptyInlines;
    private bool _hadNativeSpans = false;
    private bool _hasExplicitFontSize = false;
    private bool _hasExplicitFontFamily = false;
    private bool _hasExplicitFontWeight = false;
    private bool _hasExplicitForeground = false;
    internal string _rawRichText = "";
    internal bool _richNeedsResolve = false;
    internal static Action<TextBlock, string> _richTextHandler;

    public TextBlock() : base(VisualKind.Text) {
        _foreground = Brushes.DefaultForeground();
    }
    public TextBlock(string text) : base(VisualKind.Text) {
        _foreground = Brushes.DefaultForeground();
        _text = text == null ? "" : text;
        if (_text.Length > 0) { SyncTextToNative(); }
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
        set {
            _hasExplicitFontFamily = true;
            string nv = value == null ? "Segoe UI" : value;
            if (_fontFamily == nv) { return; }
            _fontFamily = nv;
            SyncTextToNative();
        }
    }
    public float FontSize {
        get { return _fontSize; }
        set {
            _hasExplicitFontSize = true;
            float nv = value > 0.0f ? value : 14.0f;
            if (_fontSize == nv) { return; }
            _fontSize = nv;
            SyncTextToNative();
        }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set {
            _hasExplicitFontWeight = true;
            string nv = value == null ? "Normal" : value;
            if (_fontWeight == nv) { return; }
            _fontWeight = nv;
            SyncTextToNative();
        }
    }
    public string FontStyle {
        get { return _fontStyle; }
        set {
            string nv = value == null ? "Normal" : value;
            if (_fontStyle == nv) { return; }
            _fontStyle = nv;
            SyncTextToNative();
        }
    }
    public Brush Foreground {
        get { return _foreground; }
        set {
            _hasExplicitForeground = true;
            if (_foreground == value) { return; }
            _foreground = value;
            SyncTextToNative();
        }
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
        set { if (_textWrapping == value) { return; } _textWrapping = value; SyncTextToNative(); }
    }
    public TextAlignment TextAlignment {
        get { return _textAlignment; }
        set { if (_textAlignment == value) { return; } _textAlignment = value; SyncTextToNative(); }
    }
    public TextTrimming TextTrimming {
        get { return _textTrimming; }
        set { if (_textTrimming == value) { return; } _textTrimming = value; SyncTextToNative(); }
    }
    public float LineHeight {
        get { return _lineHeight; }
        set { if (_lineHeight == value) { return; } _lineHeight = value; SyncTextMetricsToNative(); }
    }
    public float CharacterSpacing {
        get { return _charSpacing; }
        set { if (_charSpacing == value) { return; } _charSpacing = value; SyncTextMetricsToNative(); }
    }
    public int MaxLines {
        get { return _maxLines; }
        set { if (_maxLines == value) { return; } _maxLines = value; SyncTextMetricsToNative(); }
    }
    public int TextDecorations {
        get { return _decorations; }
        set { if (_decorations == value) { return; } _decorations = value; SyncTextMetricsToNative(); }
    }

    public Inline[] Inlines() {
        if (_inlines != null) { return _inlines; }
        if (_emptyInlines == null) { _emptyInlines = new Inline[0]; }
        return _emptyInlines;
    }

    public void AddInline(Inline inline) {
        if (inline == null) { return; }
        if (_inlines == null) { _inlines = new Inline[0]; }
        inline.OwnerTextBlock = this;
        _inlines.Push(inline);
        RebuildFromInlines();
    }
    internal void NotifyInlineChanged() { RebuildFromInlines(); }
    public void AddRun(Run run) { AddInline(run); }
    public void ClearInlines() {
        if (_inlines != null) {
            for (int i = 0; i < _inlines.Length; i += 1) {
                if (_inlines[i] != null) { _inlines[i].OwnerTextBlock = null; }
            }
            _inlines.Clear();
        }
        _text = "";
        SyncTextToNative();
    }
    private static bool IsPrintableOrSpaceAscii(string ch) {
        if (ch == null || ch.Length != 1) { return false; }
        string table = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\t\n\r";
        return table.Contains(ch);
    }

    public void AddRunSpaced(Run r) {
        if (r == null) { return; }
        if (_inlines != null && _inlines.Length > 0 && r.Text != null && r.Text.Length > 0) {
            Inline prev = _inlines[_inlines.Length - 1];
            if (!(prev is LineBreak) && prev.Text != null && prev.Text.Length > 0) {
                string lastCh = prev.Text.Substring(prev.Text.Length - 1, 1);
                string firstCh = r.Text.Substring(0, 1);
                bool prevEndsSpace = (lastCh == " " || lastCh == "\t" || lastCh == "\n" || lastCh == "\r" || lastCh == "(" || lastCh == "[");
                bool nextStartsPunct = (firstCh == " " || firstCh == "\t" || firstCh == "\n" || firstCh == "\r" ||
                                        firstCh == "," || firstCh == "." || firstCh == ";" ||
                                        firstCh == ":" || firstCh == "!" || firstCh == "?" ||
                                        firstCh == ")" || firstCh == "]");
                if (!prevEndsSpace && !nextStartsPunct) {
                    bool prevAscii = IsPrintableOrSpaceAscii(lastCh);
                    bool nextAscii = IsPrintableOrSpaceAscii(firstCh);
                    if (prevAscii || nextAscii) {
                        AddInline(new Run(" "));
                    }
                }
            }
        }
        AddRun(r);
    }

    private void RebuildFromInlines() {
        string combined = "";
        if (_inlines != null) {
            for (int i = 0; i < _inlines.Length; i += 1) {
                combined = combined + _inlines[i].Text;
            }
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
        BumpPropEpoch();
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
        if (_hadNativeSpans) {
            Visual_ClearTextSpans(__nativeHandle);
            _hadNativeSpans = false;
        }
        if (_inlines != null && _inlines.Length > 0) {
            _hadNativeSpans = true;
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
        BumpPropEpoch();
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Visual_SetTextMetrics(__nativeHandle, _lineHeight, _charSpacing, _maxLines, _decorations);
    }

    public Brush Stroke { get; set; }
    public float StrokeThickness { get; set; } = 0.0f;

    internal void ClearRichInlines() {
        if (_inlines == null) { return; }
        for (int k = 0; k < _inlines.Length; k += 1) {
            if (_inlines[k] != null) { _inlines[k].OwnerTextBlock = null; }
        }
        _inlines.Clear();
    }

    internal void PushRichInline(Inline inl) {
        if (inl == null) { return; }
        if (_inlines == null) { _inlines = new Inline[0]; }
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
            if (value is BoxedInt && !(value is BoxedEnum)) {
                int v = (value as BoxedInt).Value;
                TextTrimming = v == 1 ? TextTrimming.CharacterEllipsis : (v == 2 ? TextTrimming.WordEllipsis : TextTrimming.None);
                return true;
            }
            object rawObj = DependencyObject.Unbox(value);
            string tt = DependencyObject.UnboxString(value, "None").Trim();
            if ((tt.Length == 0 || tt == "ComboBoxItem") && rawObj is ContentControl) {
                tt = DependencyObject.UnboxString((rawObj as ContentControl).ContentBox, "None").Trim();
            }
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
        if (propName == "TextWrapping") { return DependencyObject.BoxInt(EnumConvert.ToInt(_textWrapping)); }
        if (propName == "TextAlignment") { return DependencyObject.BoxInt(EnumConvert.ToInt(_textAlignment)); }
        if (propName == "TextTrimming") { return DependencyObject.BoxInt(EnumConvert.ToInt(_textTrimming)); }
        return base.GetPropertyValue(propName);
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        if (_inlines != null) {
            for (int i = 0; i < _inlines.Length; i += 1) {
                if (_inlines[i] != null) {
                    _inlines[i].OwnerTextBlock = null;
                    _inlines[i]._ownBindingsBox = null;
                    _inlines[i].Foreground = null;
                }
            }
            _inlines.Clear();
        }
        _foreground = null;
        Stroke = null;
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

    internal bool ApplyRiveDirect(
        string assetPath, int instanceId, float tLoopSec, float tStateSec,
        float reqW, float reqH, Stretch stretch, RiveInput[] inputs, bool inputsDirty) {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return false; }
        _stretch = stretch;
        uint tintCol = _tint != null ? _tint.ResolveRgba() : ParseUtil.PackArgb(255u, 255u, 255u, 255u);
        Visual_SetRiveParams(
            __nativeHandle,
            assetPath,
            instanceId,
            tLoopSec,
            tStateSec,
            reqW,
            reqH,
            EnumConvert.ToInt(stretch),
            tintCol);
        if (inputsDirty) {
            Visual_ClearRiveInputs(__nativeHandle);
            if (inputs != null) {
                for (int i = 0; i < inputs.Length; i += 1) {
                    RiveInput ri = inputs[i];
                    if (ri != null && ri.InputName != null && ri.InputName.Length > 0 && ri.InputValueBox != null) {
                        float val = (ri.InputValueBox is BoxedBool)
                            ? ((ri.InputValueBox as BoxedBool).Value ? 1.0f : 0.0f)
                            : DependencyObject.UnboxFloat(ri.InputValueBox, 0.0f);
                        Visual_SetRiveInput(__nativeHandle, ri.InputName, val);
                    }
                }
            }
        }
        return true;
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
        if (propName == "StretchDirection") {
            if (value is BoxedInt) { StretchDirection = EnumConvert.ToStretchDirection((value as BoxedInt).Value); return true; }
            string sd = DependencyObject.UnboxString(value, "Both").Trim();
            if (sd == "UpOnly" || sd == "0") { StretchDirection = StretchDirection.UpOnly; return true; }
            if (sd == "DownOnly" || sd == "1") { StretchDirection = StretchDirection.DownOnly; return true; }
            StretchDirection = StretchDirection.Both;
            return true;
        }
        if (propName == "Tint") { Tint = UIElement.CoerceBrush(value); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Source") { return DependencyObject.BoxString(_source); }
        if (propName == "Stretch") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretch)); }
        if (propName == "StretchDirection") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretchDir)); }
        return base.GetPropertyValue(propName);
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
    private bool _hasNativeDash = false;
    private float _strokeThickness = 1.0f;
    protected Stretch _stretch = Stretch.Fill;
    private float _trimStart = 0.0f;
    private float _trimEnd = 1.0f;

    protected Shape() : base(VisualKind.Vector) {
        SyncInteractiveFlags();
    }
    public override string GetTypeName() { return "Shape"; }

    protected override void OnLoaded() {
        base.OnLoaded();
        if (_fill != null && _fill.NeedsLoadedRefresh()) {
            SyncVectorShape();
        }
    }

    protected override bool IsSelfHitTestSolid() {
        return _fill != null || (_stroke != null && _strokeThickness > 0.0f);
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        if (_fill is GradientBrush) { (_fill as GradientBrush).ReleaseNativeMutable(); }
        if (_stroke is GradientBrush) { (_stroke as GradientBrush).ReleaseNativeMutable(); }
        _fill = null;
        _stroke = null;
    }

    public Brush Fill {
        get { return _fill; }
        set {
            if (_fill != value && _fill is GradientBrush) {
                (_fill as GradientBrush).ReleaseNativeMutable();
            }
            _fill = value;
            _ownsClonedFill = false;
            _fillOpacity = value != null ? Math.Clampf(value.Opacity, 0.0f, 1.0f) : 1.0f;
            SyncVectorShape();
            SyncVectorBrushOpacity();
            if (!ParseUtil.IsValidHandle(__nativeHandle)) { SyncInteractiveFlags(); }
        }
    }
    public Brush Stroke {
        get { return _stroke; }
        set {
            if (_stroke != value && _stroke is GradientBrush) {
                (_stroke as GradientBrush).ReleaseNativeMutable();
            }
            _stroke = value;
            _ownsClonedStroke = false;
            _strokeOpacity = value != null ? Math.Clampf(value.Opacity, 0.0f, 1.0f) : 1.0f;
            SyncVectorShape();
            SyncVectorBrushOpacity();
            if (!ParseUtil.IsValidHandle(__nativeHandle)) { SyncInteractiveFlags(); }
        }
    }
    public float StrokeThickness {
        get { return _strokeThickness; }
        set {
            float nv = value >= 0.0f ? value : 0.0f;
            if (_strokeThickness == nv) { return; }
            _strokeThickness = nv;
            SyncVectorShape();
            if (!ParseUtil.IsValidHandle(__nativeHandle)) { SyncInteractiveFlags(); }
        }
    }
    public Stretch Stretch {
        get { return _stretch; }
        set { if (_stretch == value) { return; } _stretch = value; SyncVectorShape(); }
    }
    public float TrimStart {
        get { return _trimStart; }
        set { _trimStart = Math.Clampf(value, 0.0f, 1.0f); SyncVectorShape(); }
    }
    private string _strokeDashArray = "";
    private int _strokeDashCap = 0;
    private int _strokeStartLineCap = 0;
    private int _strokeEndLineCap = 0;

    private static int ParseLineCapCode(string s) {
        if (s == "Round" || s == "2") { return 2; }
        if (s == "Square" || s == "1") { return 1; }
        if (s == "Triangle" || s == "3") { return 3; }
        return 0;
    }

    private static string FormatLineCapCode(int code) {
        if (code == 1) { return "Square"; }
        if (code == 2) { return "Round"; }
        if (code == 3) { return "Triangle"; }
        return "Flat";
    }

    public string StrokeDashArray {
        get { return _strokeDashArray; }
        set { _strokeDashArray = value == null ? "" : value; SyncVectorShape(); }
    }
    public string StrokeDashCap {
        get { return FormatLineCapCode(_strokeDashCap); }
        set { _strokeDashCap = ParseLineCapCode(value); SyncVectorShape(); }
    }
    public string StrokeStartLineCap {
        get { return FormatLineCapCode(_strokeStartLineCap); }
        set { _strokeStartLineCap = ParseLineCapCode(value); SyncVectorShape(); }
    }
    public string StrokeEndLineCap {
        get { return FormatLineCapCode(_strokeEndLineCap); }
        set { _strokeEndLineCap = ParseLineCapCode(value); SyncVectorShape(); }
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
        if (_fill != null && _fill.ApplySurfaceToNative(this, __nativeHandle, false)) {
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
        if (_strokeDashArray.Length > 0 || _strokeDashCap != 0 ||
            _strokeStartLineCap != 0 || _strokeEndLineCap != 0 || _hasNativeDash) {
            Visual_SetVectorStrokeDash(
                __nativeHandle,
                _strokeDashArray,
                _strokeDashCap,
                _strokeStartLineCap,
                _strokeEndLineCap);
            _hasNativeDash = (_strokeDashArray.Length > 0 || _strokeDashCap != 0 ||
                _strokeStartLineCap != 0 || _strokeEndLineCap != 0);
        }
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
        if (propName == "Time" && _fill is ImageBrush && (_fill as ImageBrush).IsPlasmaShader) {
            propName = "Fill.PlasmaTime";
        }
        if ((propName == "Fill.PlasmaSeed" || propName == "Fill.PlasmaColor" ||
             propName == "Fill.PlasmaTime" || propName == "Fill.PlasmaScaleX" ||
             propName == "Fill.PlasmaScaleY") && _fill is ImageBrush) {
            ImageBrush ib = _fill as ImageBrush;
            if (!_ownsClonedFill) {
                ib = ib.CloneImageBrush();
                _fill = ib;
                _ownsClonedFill = true;
            }
            if (propName == "Fill.PlasmaSeed") { ib.PlasmaSeed = DependencyObject.UnboxFloat(value, 100.0f); }
            else if (propName == "Fill.PlasmaColor") { ib.PlasmaColor = DependencyObject.UnboxString(value, "#FF01BCFF"); }
            else if (propName == "Fill.PlasmaTime") { ib.PlasmaTime = DependencyObject.UnboxFloat(value, 0.0f); }
            else if (propName == "Fill.PlasmaScaleX") { ib.PlasmaScaleX = DependencyObject.UnboxFloat(value, 64.0f); }
            else if (propName == "Fill.PlasmaScaleY") { ib.PlasmaScaleY = DependencyObject.UnboxFloat(value, 64.0f); }
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
        if (propName.Length > 5 && propName.Substring(0, 5) == "Fill." && _fill != null) {
            string suffix = propName.Substring(5);
            Brush updatedFill = _fill.ApplySubProperty(suffix, value, !_ownsClonedFill);
            if (updatedFill != null) {
                _fill = updatedFill;
                _ownsClonedFill = true;
                SyncVectorShape();
                return true;
            }
        }
        if (propName.Length > 7 && propName.Substring(0, 7) == "Stroke." && _stroke != null) {
            string suffix = propName.Substring(7);
            Brush updatedStroke = _stroke.ApplySubProperty(suffix, value, !_ownsClonedStroke);
            if (updatedStroke != null) {
                _stroke = updatedStroke;
                _ownsClonedStroke = true;
                SyncVectorShape();
                return true;
            }
        }
        if (propName == "StrokeThickness") { StrokeThickness = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "StrokeDashArray") { StrokeDashArray = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "StrokeDashCap") { StrokeDashCap = DependencyObject.UnboxString(value, "Flat"); return true; }
        if (propName == "StrokeStartLineCap") { StrokeStartLineCap = DependencyObject.UnboxString(value, "Flat"); return true; }
        if (propName == "StrokeEndLineCap") { StrokeEndLineCap = DependencyObject.UnboxString(value, "Flat"); return true; }
        if (propName == "Stretch") {
            if (value is BoxedInt) { Stretch = EnumConvert.ToStretch((value as BoxedInt).Value); return true; }
            string st = DependencyObject.UnboxString(value, "Fill").Trim();
            if (st == "None" || st == "0") { Stretch = Stretch.None; return true; }
            if (st == "Uniform" || st == "2") { Stretch = Stretch.Uniform; return true; }
            if (st == "UniformToFill" || st == "3") { Stretch = Stretch.UniformToFill; return true; }
            Stretch = Stretch.Fill;
            return true;
        }
        if (propName == "TrimStart" || propName == "Path.TrimStart" || propName == "noesis:Path.TrimStart") {
            TrimStart = DependencyObject.UnboxFloat(value, 0.0f);
            return true;
        }
        if (propName == "TrimEnd" || propName == "Path.TrimEnd" || propName == "noesis:Path.TrimEnd") {
            TrimEnd = DependencyObject.UnboxFloat(value, 1.0f);
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Fill") { return _fill; }
        if (propName == "Fill.ImageSource" && _fill is ImageBrush) {
            return DependencyObject.BoxString((_fill as ImageBrush).ImageSource);
        }
        if (propName == "Fill.PlasmaSeed" && _fill is ImageBrush) {
            return DependencyObject.BoxFloat((_fill as ImageBrush).PlasmaSeed);
        }
        if (propName == "Fill.PlasmaColor" && _fill is ImageBrush) {
            return DependencyObject.BoxString((_fill as ImageBrush).PlasmaColor);
        }
        if (_fill is ImageBrush && (propName == "Fill.PlasmaTime" || (propName == "Time" && (_fill as ImageBrush).IsPlasmaShader))) {
            return DependencyObject.BoxFloat((_fill as ImageBrush).PlasmaTime);
        }
        if (propName == "Fill.PlasmaScaleX" && _fill is ImageBrush) {
            return DependencyObject.BoxFloat((_fill as ImageBrush).PlasmaScaleX);
        }
        if (propName == "Fill.PlasmaScaleY" && _fill is ImageBrush) {
            return DependencyObject.BoxFloat((_fill as ImageBrush).PlasmaScaleY);
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
        if (propName.Length > 5 && propName.Substring(0, 5) == "Fill." && _fill != null) {
            DependencyObject stopBox = _fill.ReadSubProperty(propName.Substring(5));
            if (stopBox != null) { return stopBox; }
        }
        if (propName.Length > 7 && propName.Substring(0, 7) == "Stroke." && _stroke != null) {
            DependencyObject stopBox = _stroke.ReadSubProperty(propName.Substring(7));
            if (stopBox != null) { return stopBox; }
        }
        if (propName == "StrokeThickness") { return DependencyObject.BoxFloat(_strokeThickness); }
        if (propName == "Stretch") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretch)); }
        if (propName == "TrimStart" || propName == "Path.TrimStart" || propName == "noesis:Path.TrimStart") {
            return DependencyObject.BoxFloat(_trimStart);
        }
        if (propName == "TrimEnd" || propName == "Path.TrimEnd" || propName == "noesis:Path.TrimEnd") {
            return DependencyObject.BoxFloat(_trimEnd);
        }
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
        _stretch = Stretch.None;
    }
    public Path(string svgPath) : base() {
        _data = svgPath == null ? "" : svgPath;
        _stretch = Stretch.None;
        if (_data.Length > 0) { SyncVectorShape(); }
    }

    public override string GetTypeName() { return "Path"; }
    public string Data {
        get { return _data; }
        set { _data = value == null ? "" : value; SyncVectorShape(); }
    }
    protected override string BuildVectorGeometrySpec() { return _data; }

    private static bool IsSvgSep(string ch) {
        return ch == " " || ch == "\t" || ch == "\n" || ch == "\r" || ch == ",";
    }

    private static bool IsSvgDigit(string ch) {
        return "0123456789".Contains(ch);
    }

    private static int TryParseUnsignedDigits(string s, int startPos, int[] outPair) {
        int i = startPos;
        int acc = 0;
        int count = 0;
        while (i < s.Length) {
            string ch = s.Substring(i, 1);
            if (!IsSvgDigit(ch)) { break; }
            acc = acc * 10 + ParseUtil.ParseInt(ch, 0);
            count += 1;
            i += 1;
        }
        if (count == 0) { return -1; }
        outPair[0] = acc;
        outPair[1] = i;
        return 0;
    }

    private static bool TryParsePathPropertySpec(string propName, int[] outSpec) {
        if (propName == null || propName.Length < 14) { return false; }
        if (propName.Substring(0, 8) != "Data.Fig") { return false; }
        var pair = new int[2];
        if (TryParseUnsignedDigits(propName, 8, pair) < 0) { return false; }
        int figIdx = pair[0];
        int pos = pair[1];
        if (pos + 11 <= propName.Length && propName.Substring(pos, 11) == ".StartPoint") {
            string axis = propName.Substring(pos + 11);
            if (axis == ".X") { outSpec[0] = figIdx; outSpec[1] = -1; outSpec[2] = 0; return true; }
            if (axis == ".Y") { outSpec[0] = figIdx; outSpec[1] = -1; outSpec[2] = 1; return true; }
            return false;
        }
        if (pos + 4 > propName.Length || propName.Substring(pos, 4) != ".Seg") { return false; }
        if (TryParseUnsignedDigits(propName, pos + 4, pair) < 0) { return false; }
        int segIdx = pair[0];
        pos = pair[1];
        string tail = propName.Substring(pos);
        int slot = -1;
        if (tail == ".Point.X") { slot = 2; }
        else if (tail == ".Point.Y") { slot = 3; }
        else if (tail == ".Point1.X") { slot = 4; }
        else if (tail == ".Point1.Y") { slot = 5; }
        else if (tail == ".Point2.X") { slot = 6; }
        else if (tail == ".Point2.Y") { slot = 7; }
        else if (tail == ".Point3.X") { slot = 8; }
        else if (tail == ".Point3.Y") { slot = 9; }
        if (slot < 0) { return false; }
        outSpec[0] = figIdx;
        outSpec[1] = segIdx;
        outSpec[2] = slot;
        return true;
    }

    private static int ScanNextSvgNumber(string svg, int pos, int[] numSpan) {
        int n = svg.Length;
        while (pos < n && IsSvgSep(svg.Substring(pos, 1))) { pos += 1; }
        if (pos >= n) { return -1; }
        int start = pos;
        string c0 = svg.Substring(pos, 1);
        if (c0 == "+" || c0 == "-") { pos += 1; }
        bool hasDigit = false;
        bool seenDot = false;
        while (pos < n) {
            string ch = svg.Substring(pos, 1);
            if (IsSvgDigit(ch)) {
                hasDigit = true;
                pos += 1;
            } else if (ch == "." && !seenDot) {
                seenDot = true;
                pos += 1;
            } else {
                break;
            }
        }
        if (!hasDigit) { return -1; }
        if (pos < n) {
            string ep = svg.Substring(pos, 1);
            if (ep == "e" || ep == "E") {
                int savePos = pos;
                pos += 1;
                if (pos < n) {
                    string sg = svg.Substring(pos, 1);
                    if (sg == "+" || sg == "-") { pos += 1; }
                }
                bool expDigits = false;
                while (pos < n && IsSvgDigit(svg.Substring(pos, 1))) {
                    expDigits = true;
                    pos += 1;
                }
                if (!expDigits) { pos = savePos; }
            }
        }
        numSpan[0] = start;
        numSpan[1] = pos;
        return pos;
    }

    private static bool FindPathTargetNumberSpan(string svg, int wantFig, int wantSeg, int slotKind, int[] outSpan) {
        if (svg == null || svg.Length == 0 || wantFig < 0) { return false; }
        int n = svg.Length;
        int pos = 0;
        int curFig = -1;
        int curSeg = -1;
        string curCmd = "";
        bool expectMoveAsLine = false;
        var numSpan = new int[2];
        var starts = new int[7];
        var ends = new int[7];

        while (pos < n) {
            while (pos < n && IsSvgSep(svg.Substring(pos, 1))) { pos += 1; }
            if (pos >= n) { break; }
            string ch = svg.Substring(pos, 1);
            if (ch == "F" || ch == "f") {
                pos += 1;
                int nextF = ScanNextSvgNumber(svg, pos, numSpan);
                if (nextF > pos) { pos = nextF; }
                continue;
            }
            if ("MmLlHhVvCcSsQqTtAaZz".Contains(ch)) {
                curCmd = ch;
                expectMoveAsLine = false;
                pos += 1;
                if (curCmd == "Z" || curCmd == "z") { continue; }
            }
            if (curCmd.Length == 0) { break; }
            string effCmd = curCmd;
            if (expectMoveAsLine) {
                if (curCmd == "M") { effCmd = "L"; }
                else if (curCmd == "m") { effCmd = "l"; }
            }
            int need = 0;
            if (effCmd == "M" || effCmd == "m" || effCmd == "L" || effCmd == "l" || effCmd == "T" || effCmd == "t") { need = 2; }
            else if (effCmd == "H" || effCmd == "h" || effCmd == "V" || effCmd == "v") { need = 1; }
            else if (effCmd == "S" || effCmd == "s" || effCmd == "Q" || effCmd == "q") { need = 4; }
            else if (effCmd == "C" || effCmd == "c") { need = 6; }
            else if (effCmd == "A" || effCmd == "a") { need = 7; }
            else { break; }

            bool okNums = true;
            for (int k = 0; k < need; k += 1) {
                int nextPos = ScanNextSvgNumber(svg, pos, numSpan);
                if (nextPos < 0) { okNums = false; break; }
                starts[k] = numSpan[0];
                ends[k] = numSpan[1];
                pos = nextPos;
            }
            if (!okNums) { break; }

            if (effCmd == "M" || effCmd == "m") {
                curFig += 1;
                curSeg = -1;
                expectMoveAsLine = true;
                if (curFig == wantFig && wantSeg == -1 && (slotKind == 0 || slotKind == 1)) {
                    outSpan[0] = starts[slotKind];
                    outSpan[1] = ends[slotKind];
                    return true;
                }
            } else {
                curSeg += 1;
                if (curFig == wantFig && curSeg == wantSeg) {
                    int pick = -1;
                    if (slotKind == 2) {
                        if (effCmd == "H" || effCmd == "h") { pick = 0; }
                        else if (effCmd == "V" || effCmd == "v") { pick = -1; }
                        else { pick = need - 2; }
                    } else if (slotKind == 3) {
                        if (effCmd == "V" || effCmd == "v") { pick = 0; }
                        else if (effCmd == "H" || effCmd == "h") { pick = -1; }
                        else { pick = need - 1; }
                    } else if (slotKind == 4 || slotKind == 5) {
                        if (effCmd == "C" || effCmd == "c" || effCmd == "Q" || effCmd == "q") { pick = slotKind - 4; }
                    } else if (slotKind == 6 || slotKind == 7) {
                        if (effCmd == "C" || effCmd == "c") { pick = 2 + (slotKind - 6); }
                        else if (effCmd == "S" || effCmd == "s") { pick = slotKind - 6; }
                        else if (effCmd == "Q" || effCmd == "q") { pick = 2 + (slotKind - 6); }
                    } else if (slotKind == 8 || slotKind == 9) {
                        if (effCmd == "C" || effCmd == "c") { pick = 4 + (slotKind - 8); }
                        else if (effCmd == "S" || effCmd == "s") { pick = 2 + (slotKind - 8); }
                    }
                    if (pick >= 0 && pick < need) {
                        outSpan[0] = starts[pick];
                        outSpan[1] = ends[pick];
                        return true;
                    }
                    return false;
                }
            }
        }
        return false;
    }

    private static float ReadPathTargetNumber(string svg, int[] spec) {
        var span = new int[2];
        if (!FindPathTargetNumberSpan(svg, spec[0], spec[1], spec[2], span)) { return 0.0f; }
        return ParseUtil.ParseFloat(svg.Substring(span[0], span[1] - span[0]), 0.0f);
    }

    private static string WritePathTargetNumber(string svg, int[] spec, float newVal) {
        var span = new int[2];
        if (!FindPathTargetNumberSpan(svg, spec[0], spec[1], spec[2], span)) { return svg; }
        int start = span[0];
        int end = span[1];
        string prefixSep = "";
        if (start > 0 && newVal >= 0.0f) {
            string prevCh = svg.Substring(start - 1, 1);
            if (IsSvgDigit(prevCh) || prevCh == ".") { prefixSep = " "; }
        }
        string suffixSep = "";
        if (end < svg.Length) {
            string nextCh = svg.Substring(end, 1);
            if (IsSvgDigit(nextCh) || nextCh == ".") { suffixSep = " "; }
        }
        return svg.Substring(0, start) + prefixSep + ("" + newVal) + suffixSep + svg.Substring(end);
    }

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
        var spec = new int[3];
        if (TryParsePathPropertySpec(propName, spec)) {
            Data = WritePathTargetNumber(_data, spec, DependencyObject.UnboxFloat(value, 0.0f));
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Data") { return DependencyObject.BoxString(_data); }
        var spec = new int[3];
        if (TryParsePathPropertySpec(propName, spec)) {
            return DependencyObject.BoxFloat(ReadPathTargetNumber(_data, spec));
        }
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
    public void DrawImage(string uri, float x, float y, float w, float h, uint tintRgba = 4294967295u) {
        Draw_Image(_node, uri, x, y, w, h, tintRgba);
    }
    public void BeginTextRuns(
        string text, float x, float y, float w, float h,
        string fontFamily, float fontSize, uint defaultRgba = 4293060848u) {
        Draw_BeginTextRuns(_node, text, x, y, w, h, fontFamily, fontSize, defaultRgba);
    }
    public void AddTextRunSpan(int byteOffset, int byteLength, uint rgba) {
        Draw_AddTextRunSpan(_node, byteOffset, byteLength, rgba);
    }
    public void DrawSyntaxLine(
        string lineText, float x, float y, float w, float h,
        string fontFamily, int langKind, float fontSize = 13.0f, uint defaultRgba = 4293060848u) {
        Draw_SyntaxLine(_node, lineText, x, y, w, h, fontFamily, langKind, fontSize, defaultRgba);
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
