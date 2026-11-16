module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. ThemeManager (Built-in Aero Dark/Light Theme Tokens & Default Styles)
// ============================================================================

public static class ThemeManager {
    private static bool _initialized = false;
    private static bool _isDark = true;

    public static void EnsureInitialized() {
        if (_initialized) { return; }
        _initialized = true;
        ApplyAeroDarkTheme(ResourceDictionary.GlobalApplicationResources());
    }

    public static bool IsDarkTheme() { return _isDark; }

    public static void ApplyAeroDarkTheme(ResourceDictionary rd = null) {
        ResourceDictionary dict = rd != null ? rd : ResourceDictionary.GlobalApplicationResources();
        _isDark = true;
        _initialized = true;

        dict.Set("AeroBackgroundBrush", new SolidColorBrush("#FF12151C"));
        dict.Set("AeroSurfaceBrush", new SolidColorBrush("#FF1B202B"));
        dict.Set("AeroCardBrush", new SolidColorBrush("#FF222836"));
        dict.Set("AeroHoverBrush", new SolidColorBrush("#FF2D3548"));
        dict.Set("AeroPressedBrush", new SolidColorBrush("#FF161A23"));
        dict.Set("AeroBorderBrush", new SolidColorBrush("#FF364054"));
        dict.Set("AeroAccentBrush", new SolidColorBrush("#FF3B82F6"));
        dict.Set("AeroAccentHoverBrush", new SolidColorBrush("#FF60A5FA"));
        dict.Set("AeroAccentPressedBrush", new SolidColorBrush("#FF2563EB"));
        dict.Set("AeroTextPrimaryBrush", new SolidColorBrush("#FFF1F5F9"));
        dict.Set("AeroTextSecondaryBrush", new SolidColorBrush("#FF94A3B8"));
        dict.Set("AeroDangerBrush", new SolidColorBrush("#FFEF4444"));
        dict.Set("AeroSuccessBrush", new SolidColorBrush("#FF22C55E"));

        RegisterDefaultStyles(dict, "#FF242B38", "#FF303A4D", "#FF1C222D", "#FF3B465C", "#FFF1F5F9", "#FF3B82F6");
    }

    public static void ApplyAeroLightTheme(ResourceDictionary rd = null) {
        ResourceDictionary dict = rd != null ? rd : ResourceDictionary.GlobalApplicationResources();
        _isDark = false;
        _initialized = true;

        dict.Set("AeroBackgroundBrush", new SolidColorBrush("#FFF5F7FA"));
        dict.Set("AeroSurfaceBrush", new SolidColorBrush("#FFFFFFFF"));
        dict.Set("AeroCardBrush", new SolidColorBrush("#FFFFFFFF"));
        dict.Set("AeroHoverBrush", new SolidColorBrush("#FFE8EEF7"));
        dict.Set("AeroPressedBrush", new SolidColorBrush("#FFD7E2F2"));
        dict.Set("AeroBorderBrush", new SolidColorBrush("#FFCBD5E1"));
        dict.Set("AeroAccentBrush", new SolidColorBrush("#FF2563EB"));
        dict.Set("AeroAccentHoverBrush", new SolidColorBrush("#FF3B82F6"));
        dict.Set("AeroAccentPressedBrush", new SolidColorBrush("#FF1D4ED8"));
        dict.Set("AeroTextPrimaryBrush", new SolidColorBrush("#FF0F172A"));
        dict.Set("AeroTextSecondaryBrush", new SolidColorBrush("#FF64748B"));
        dict.Set("AeroDangerBrush", new SolidColorBrush("#FFDC2626"));
        dict.Set("AeroSuccessBrush", new SolidColorBrush("#FF16A34A"));

        RegisterDefaultStyles(dict, "#FFFFFFFF", "#FFE8EEF7", "#FFD7E2F2", "#FFCBD5E1", "#FF0F172A", "#FF2563EB");
    }

    private static void RegisterDefaultStyles(
        ResourceDictionary dict,
        string ctrlBg, string hoverBg, string pressBg, string borderHex, string fgHex, string accentHex) {

        var btnStyle = new Style("Button");
        btnStyle.AddSetter("Background", ctrlBg);
        btnStyle.AddSetter("Foreground", fgHex);
        btnStyle.AddSetter("BorderBrush", borderHex);
        btnStyle.AddSetter("BorderThickness", "1");
        btnStyle.AddSetter("CornerRadius", "6");
        btnStyle.AddSetter("Padding", "14,7,14,7");
        var btnHover = new Trigger("IsMouseOver", true);
        btnHover.AddSetter("Background", hoverBg);
        btnHover.AddSetter("BorderBrush", accentHex);
        btnStyle.AddTrigger(btnHover);
        var btnPress = new Trigger("IsPressed", true);
        btnPress.AddSetter("Background", pressBg);
        btnStyle.AddTrigger(btnPress);
        var btnDis = new Trigger("IsEnabled", false);
        btnDis.AddSetter("Opacity", 0.45f);
        btnStyle.AddTrigger(btnDis);
        dict.Set("Button", btnStyle);

        var tbStyle = new Style("TextBox");
        tbStyle.AddSetter("Background", pressBg);
        tbStyle.AddSetter("Foreground", fgHex);
        tbStyle.AddSetter("BorderBrush", borderHex);
        tbStyle.AddSetter("BorderThickness", "1");
        tbStyle.AddSetter("CornerRadius", "5");
        tbStyle.AddSetter("Padding", "8,6,8,6");
        var tbFocus = new Trigger("IsFocused", true);
        tbFocus.AddSetter("BorderBrush", accentHex);
        tbStyle.AddTrigger(tbFocus);
        dict.Set("TextBox", tbStyle);

        var pwStyle = new Style("PasswordBox", tbStyle);
        pwStyle.TargetType = "PasswordBox";
        dict.Set("PasswordBox", pwStyle);

        var sepStyle = new Style("Separator");
        sepStyle.AddSetter("Height", 1.0f);
        sepStyle.AddSetter("Background", borderHex);
        sepStyle.AddSetter("Margin", "0,4,0,4");
        dict.Set("Separator", sepStyle);
    }
}

// ============================================================================
// 2. Control Base Class & ContentPresenter (: FrameworkElement, NOT Control!)
// ============================================================================

public class Control : FrameworkElement {
    private Brush _foreground;
    private string _fontFamily = "Segoe UI";
    private float _fontSize = 14.0f;
    private string _fontWeight = "Normal";
    private string _fontStyle = "Normal";
    private HorizontalAlignment _hContentAlign = HorizontalAlignment.Center;
    private VerticalAlignment _vContentAlign = VerticalAlignment.Center;
    private ControlTemplate _template;
    private UIElement _templateRoot;
    private bool _templateApplied = false;
    private bool _hasExplicitFontSize = false;
    private bool _hasExplicitFontFamily = false;
    private bool _hasExplicitFontWeight = false;
    private bool _hasExplicitForeground = false;

    public Control() : base(VisualKind.Box) {
        if (_fontFamily == null || _fontFamily.Length == 0) { _fontFamily = "Segoe UI"; }
        if (_fontSize <= 0.0f) { _fontSize = 14.0f; }
        if (_fontWeight == null || _fontWeight.Length == 0) { _fontWeight = "Normal"; }
        if (_fontStyle == null || _fontStyle.Length == 0) { _fontStyle = "Normal"; }
        _hContentAlign = HorizontalAlignment.Center;
        _vContentAlign = VerticalAlignment.Center;
        ThemeManager.EnsureInitialized();
        _foreground = Brushes.DefaultForeground();
    }

    public override string GetTypeName() { return "Control"; }

    public Brush Foreground {
        get { return _foreground; }
        set { _hasExplicitForeground = true; _foreground = value; SetExplicitTextForeground(value); OnTypographyChanged(); }
    }
    public string FontFamily {
        get { return _fontFamily; }
        set { _hasExplicitFontFamily = true; _fontFamily = value == null ? "Segoe UI" : value; SetExplicitTextFontFamily(_fontFamily); OnTypographyChanged(); }
    }
    public float FontSize {
        get { return _fontSize; }
        set { _hasExplicitFontSize = true; _fontSize = value > 0.0f ? value : 14.0f; SetExplicitTextFontSize(_fontSize); OnTypographyChanged(); }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set { _hasExplicitFontWeight = true; _fontWeight = value == null ? "Normal" : value; SetExplicitTextFontWeight(_fontWeight); OnTypographyChanged(); }
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
            if (fg != null && _foreground != fg) { _foreground = fg; changed = true; }
        }
        if (changed) { OnTypographyChanged(); }
    }
    public string FontStyle {
        get { return _fontStyle; }
        set { _fontStyle = value == null ? "Normal" : value; OnTypographyChanged(); }
    }
    public HorizontalAlignment HorizontalContentAlignment {
        get { return _hContentAlign; }
        set { _hContentAlign = value; OnContentAlignmentChanged(); }
    }
    public VerticalAlignment VerticalContentAlignment {
        get { return _vContentAlign; }
        set { _vContentAlign = value; OnContentAlignmentChanged(); }
    }

    public ControlTemplate Template {
        get { return _template; }
        set {
            if (_template == value && _templateApplied) { return; }
            _template = value;
            _templateApplied = false;
            ApplyTemplate();
        }
    }
    public UIElement TemplateRoot() { return _templateRoot; }

    protected virtual UIElement CreateDefaultTemplate() { return null; }

    protected virtual void OnTypographyChanged() {
        SyncTemplatePresenters();
    }
    protected virtual void OnContentAlignmentChanged() {
        SyncTemplatePresenters();
    }
    protected override void OnVisualStatePropertyChanged() {
        if (_template != null && _templateApplied) {
            _template.EvaluateTriggers(this);
        }
    }

    private void SyncTemplatePresenters() {
        if (_templateRoot != null) {
            PropagateTypography(_templateRoot);
        }
    }

    private void PropagateTypography(UIElement rootNode) {
        if (rootNode == null) { return; }
        UIElement self = this;
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null || node.TemplatedParent != self) { continue; }
            if (node is ContentPresenter) {
                (node as ContentPresenter).SyncFromTemplatedParent(this);
                continue;
            }
            if (node is TextBlock) {
                TextBlock tb = node as TextBlock;
                if (_hasExplicitForeground) { tb.Foreground = _foreground; }
                if (_hasExplicitFontSize) { tb.FontSize = _fontSize; }
                if (_hasExplicitFontFamily) { tb.FontFamily = _fontFamily; }
                if (_hasExplicitFontWeight) { tb.FontWeight = _fontWeight; }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                UIElement ch = node.ChildAt(i);
                if (ch != null && ch.TemplatedParent == self) {
                    q.Push(ch);
                }
            }
        }
    }

    public UIElement GetTemplateChild(string name) {
        if (_templateRoot != null) {
            UIElement found = _templateRoot.FindName(name);
            if (found != null) { return found; }
        }
        return FindName(name);
    }

    public override bool ApplyTemplate() {
        UIElement self = this;
        UIElement nextRoot = null;
        if (_template != null) {
            nextRoot = _template.Instantiate(this);
        } else {
            nextRoot = CreateDefaultTemplate();
        }
        if (_templateRoot != null && _templateRoot.Parent == self) {
            RemoveChild(_templateRoot);
        }
        _templateRoot = nextRoot;
        _templateApplied = true;
        if (_templateRoot != null) {
            MarkTemplateOwner(_templateRoot, this);
            AddChild(_templateRoot);
            SyncTemplatePresenters();
            if (_template != null) {
                _template.EvaluateTriggers(this);
            }
        }
        OnApplyTemplate();
        return _templateRoot != null;
    }

    private static void MarkTemplateOwner(UIElement rootEl, Control owner) {
        if (rootEl == null || owner == null) { return; }
        UIElement ownerEl = owner;
        var q = new UIElement[0];
        q.Push(rootEl);
        int idx = 0;
        while (idx < q.Length) {
            UIElement el = q[idx];
            idx += 1;
            if (el == null) { continue; }
            if (el.TemplatedParent == null) { el.TemplatedParent = ownerEl; }
            if (el is ContentPresenter || el.TemplatedParent != ownerEl) { continue; }
            for (int i = 0; i < el.ChildCount(); i += 1) {
                q.Push(el.ChildAt(i));
            }
        }
    }

    protected override void OnLoaded() {
        if (!_templateApplied) {
            ApplyTemplate();
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Foreground") { Foreground = UIElement.CoerceBrush(value); return true; }
        if (propName == "FontFamily") { FontFamily = DependencyObject.UnboxString(value, "Segoe UI"); return true; }
        if (propName == "FontSize") { FontSize = DependencyObject.UnboxFloat(value, 14.0f); return true; }
        if (propName == "FontWeight") { FontWeight = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "FontStyle") { FontStyle = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "Template" && value is ControlTemplate) {
            Template = value as ControlTemplate;
            return true;
        }
        if (propName == "HorizontalContentAlignment") {
            string ha = DependencyObject.UnboxString(value, "Center");
            if (ha == "Left") { HorizontalContentAlignment = HorizontalAlignment.Left; return true; }
            if (ha == "Right") { HorizontalContentAlignment = HorizontalAlignment.Right; return true; }
            if (ha == "Stretch") { HorizontalContentAlignment = HorizontalAlignment.Stretch; return true; }
            HorizontalContentAlignment = HorizontalAlignment.Center;
            return true;
        }
        if (propName == "VerticalContentAlignment") {
            string va = DependencyObject.UnboxString(value, "Center");
            if (va == "Top") { VerticalContentAlignment = VerticalAlignment.Top; return true; }
            if (va == "Bottom") { VerticalContentAlignment = VerticalAlignment.Bottom; return true; }
            if (va == "Stretch") { VerticalContentAlignment = VerticalAlignment.Stretch; return true; }
            VerticalContentAlignment = VerticalAlignment.Center;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject FindResourceBox(string key) {
        if (key == null || key.Length == 0) { return null; }
        DependencyObject local = TryFindLocalResourceBox(key);
        if (local != null) { return local; }
        if (_template != null && _template.Resources != null && _template.Resources.Contains(key)) {
            return _template.Resources.GetBox(key);
        }
        return base.FindResourceBox(key);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Foreground") { return _foreground; }
        if (propName == "FontSize") { return DependencyObject.BoxFloat(_fontSize); }
        if (propName == "FontFamily") { return DependencyObject.BoxString(_fontFamily); }
        return base.GetPropertyValue(propName);
    }
}

public class ContentPresenter : FrameworkElement {
    private DependencyObject _contentBox;
    private DataTemplate _contentTemplate;
    private UIElement _templateInstanceRoot;
    private TextBlock _textView;
    private Brush _foreground;
    private string _fontFamily = "Segoe UI";
    private float _fontSize = 14.0f;
    private string _fontWeight = "Normal";
    public string ContentSource { get; set; } = "Content";

    public ContentPresenter() : base(VisualKind.Box) {
        _foreground = Brushes.DefaultForeground();
    }

    public override string GetTypeName() { return "ContentPresenter"; }

    public Brush Foreground {
        get { return _foreground; }
        set { _foreground = value; if (_textView != null) { _textView.Foreground = value; } }
    }
    public string FontFamily {
        get { return _fontFamily; }
        set {
            _fontFamily = value == null ? "Segoe UI" : value;
            if (_textView != null) { _textView.FontFamily = _fontFamily; }
        }
    }
    public float FontSize {
        get { return _fontSize; }
        set {
            _fontSize = value > 0.0f ? value : 14.0f;
            if (_textView != null) { _textView.FontSize = _fontSize; }
        }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set {
            _fontWeight = value == null ? "Normal" : value;
            if (_textView != null) { _textView.FontWeight = _fontWeight; }
        }
    }

    public object Content {
        get { return DependencyObject.Unbox(_contentBox); }
        set { SetContentBox(DependencyObject.BoxObject(value)); }
    }
    public DependencyObject ContentBox {
        get { return _contentBox; }
        set { SetContentBox(value); }
    }
    public void SetContent(string text) { SetContentBox(DependencyObject.BoxString(text)); }
    public void SetContent(float v) { SetContentBox(DependencyObject.BoxFloat(v)); }
    public void SetContent(int v) { SetContentBox(DependencyObject.BoxInt(v)); }
    public void SetContent(bool v) { SetContentBox(DependencyObject.BoxBool(v)); }
    public void SetContent(object v) { SetContentBox(DependencyObject.BoxObject(v)); }

    public void SetContentBox(DependencyObject box) {
        if (box == null && _contentBox == null && _contentTemplate == null) { return; }
        _contentBox = box;
        RebuildContentVisual();
    }

    public DataTemplate ContentTemplate {
        get { return _contentTemplate; }
        set {
            if (_contentTemplate == value) { return; }
            _contentTemplate = value;
            RebuildContentVisual();
        }
    }

    public void SetContentAndTemplate(DependencyObject box, DataTemplate tpl) {
        if (_contentBox == box && _contentTemplate == tpl && ChildCount() > 0) { return; }
        _contentBox = box;
        _contentTemplate = tpl;
        RebuildContentVisual();
    }

    public void SyncFromTemplatedParent(Control owner) {
        if (owner == null) { return; }
        UIElement ownerEl = owner;
        if (TemplatedParent != ownerEl) { return; }
        _foreground = owner.Foreground;
        _fontFamily = owner.FontFamily;
        _fontSize = owner.FontSize;
        _fontWeight = owner.FontWeight;
        HorizontalAlignment = owner.HorizontalContentAlignment;
        VerticalAlignment = owner.VerticalContentAlignment;
        if (_textView != null) {
            _textView.Foreground = _foreground;
            _textView.FontFamily = _fontFamily;
            _textView.FontSize = _fontSize;
            _textView.FontWeight = _fontWeight;
        }
        if (ContentSource == "Header" && owner is HeaderedContentControl) {
            HeaderedContentControl hcc = owner as HeaderedContentControl;
            bool needHdrRebuild = false;
            if (_contentTemplate == null && hcc.HeaderTemplate != null) {
                _contentTemplate = hcc.HeaderTemplate;
                needHdrRebuild = true;
            }
            if (_contentBox == null && hcc.HeaderBox != null) {
                UIElement self = this;
                UIElement hbEl = (hcc.HeaderBox is UIElement) ? (hcc.HeaderBox as UIElement) : null;
                if (hbEl == null || (hbEl != self && !hbEl.IsAncestorOf(self))) {
                    _contentBox = hcc.HeaderBox;
                    needHdrRebuild = true;
                }
            }
            if (needHdrRebuild) {
                RebuildContentVisual();
            }
            return;
        }
        if (owner is ContentControl) {
            ContentControl cc = owner as ContentControl;
            bool needRebuild = false;
            if (_contentTemplate == null && cc.ContentTemplate != null) {
                _contentTemplate = cc.ContentTemplate;
                needRebuild = true;
            }
            if (_contentBox == null && cc.ContentBox != null) {
                UIElement self = this;
                UIElement cbEl = (cc.ContentBox is UIElement) ? (cc.ContentBox as UIElement) : null;
                if (cbEl == null || (cbEl != self && !cbEl.IsAncestorOf(self))) {
                    _contentBox = cc.ContentBox;
                    needRebuild = true;
                }
            }
            if (needRebuild) {
                RebuildContentVisual();
            }
        }
    }

    public void RebuildContentVisual() {
        if (_templateInstanceRoot != null) {
            _templateInstanceRoot.MarkDisposedSubtree();
            _templateInstanceRoot = null;
        }
        if (_contentBox == null) {
            if (_contentTemplate != null) {
                _textView = null;
                ClearChildren();
                UIElement builtNull = _contentTemplate.Instantiate(null);
                _templateInstanceRoot = builtNull;
                if (builtNull != null) { AddChild(builtNull); }
                return;
            }
            if (_textView != null) {
                _textView.Text = "";
                _textView.Visibility = Visibility.Collapsed;
                return;
            }
            ClearChildren();
            return;
        }
        if (_contentBox is UIElement) {
            _textView = null;
            UIElement el = _contentBox as UIElement;
            if (ChildCount() == 1 && ChildAt(0) == el) { return; }
            ClearChildren();
            AddChild(el);
            return;
        }
        if (_contentTemplate != null) {
            _textView = null;
            ClearChildren();
            UIElement built = _contentTemplate.Instantiate(DependencyObject.Unbox(_contentBox));
            _templateInstanceRoot = built;
            if (built != null) { AddChild(built); }
            return;
        }
        string text = DependencyObject.UnboxString(_contentBox, "");
        if (_textView == null) {
            ClearChildren();
            _textView = new TextBlock(text);
            _textView.Foreground = _foreground;
            _textView.FontFamily = _fontFamily;
            _textView.FontSize = _fontSize;
            _textView.FontWeight = _fontWeight;
            _textView.HorizontalAlignment = HorizontalAlignment.Center;
            _textView.VerticalAlignment = VerticalAlignment.Center;
            AddChild(_textView);
        } else {
            _textView.Visibility = Visibility.Visible;
            _textView.Text = text;
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Content") { SetContentBox(value); return true; }
        if (propName == "ContentSource") { ContentSource = DependencyObject.UnboxString(value, "Content"); return true; }
        if (propName == "ContentTemplate" && value is DataTemplate) {
            ContentTemplate = value as DataTemplate;
            return true;
        }
        if (propName == "Foreground" || propName == "TextElement.Foreground") {
            Foreground = UIElement.CoerceBrush(value);
            return true;
        }
        if (propName == "FontFamily" || propName == "TextElement.FontFamily") {
            FontFamily = DependencyObject.UnboxString(value, "Segoe UI");
            return true;
        }
        if (propName == "FontSize" || propName == "TextElement.FontSize") {
            FontSize = DependencyObject.UnboxFloat(value, 14.0f);
            return true;
        }
        if (propName == "FontWeight" || propName == "TextElement.FontWeight") {
            FontWeight = DependencyObject.UnboxString(value, "Normal");
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Content") { return _contentBox; }
        if (propName == "Foreground" || propName == "TextElement.Foreground") { return _foreground; }
        if (propName == "FontFamily" || propName == "TextElement.FontFamily") { return DependencyObject.BoxString(_fontFamily); }
        if (propName == "FontSize" || propName == "TextElement.FontSize") { return DependencyObject.BoxFloat(_fontSize); }
        if (propName == "FontWeight" || propName == "TextElement.FontWeight") { return DependencyObject.BoxString(_fontWeight); }
        return base.GetPropertyValue(propName);
    }
}

public class ScrollContentPresenter : ContentPresenter {
    public bool CanContentScroll { get; set; } = false;
    public ScrollContentPresenter() : base() {}
    public override string GetTypeName() { return "ScrollContentPresenter"; }
}

// ============================================================================
// 3. ContentControl, HeaderedContentControl, GroupBox, Expander, Separator
// ============================================================================

public class ContentControl : Control {
    private DependencyObject _contentBox;
    private DataTemplate _contentTemplate;
    protected ContentPresenter _presenter;

    public ContentControl() : base() {}
    public override string GetTypeName() { return "ContentControl"; }

    public object Content {
        get { return DependencyObject.Unbox(_contentBox); }
        set { SetContentBox(DependencyObject.BoxObject(value)); }
    }
    public DependencyObject ContentBox {
        get { return _contentBox; }
        set { SetContentBox(value); }
    }
    public bool HasContent { get { return _contentBox != null; } }

    public void SetContent(string text) { SetContentBox(DependencyObject.BoxString(text)); }
    public void SetContent(float v) { SetContentBox(DependencyObject.BoxFloat(v)); }
    public void SetContent(int v) { SetContentBox(DependencyObject.BoxInt(v)); }
    public void SetContent(bool v) { SetContentBox(DependencyObject.BoxBool(v)); }
    public void SetContent(object v) { SetContentBox(DependencyObject.BoxObject(v)); }

    public void SetContentBox(DependencyObject newBox) {
        DependencyObject oldBox = _contentBox;
        _contentBox = newBox;
        if (_presenter != null) {
            _presenter.SetContentBox(_contentBox);
        }
        OnContentChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_contentBox));
    }

    public DataTemplate ContentTemplate {
        get { return _contentTemplate; }
        set {
            _contentTemplate = value;
            if (_presenter != null) { _presenter.ContentTemplate = value; }
        }
    }

    public void SetContentAndTemplate(DependencyObject newBox, DataTemplate tpl) {
        DependencyObject oldBox = _contentBox;
        _contentTemplate = tpl;
        _contentBox = newBox;
        if (_presenter != null) {
            _presenter.SetContentAndTemplate(_contentBox, _contentTemplate);
        }
        OnContentChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_contentBox));
    }

    protected virtual void OnContentChanged(object oldContent, object newContent) {}

    protected override UIElement CreateDefaultTemplate() {
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = _contentTemplate;
        _presenter.SetContentBox(_contentBox);
        return _presenter;
    }

    protected static ContentPresenter FindFirstTemplatedPresenter(
        UIElement rootNode, UIElement owner, bool headerSource) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node.TemplatedParent != null && node.TemplatedParent != owner) { continue; }
            if (node is ContentPresenter) {
                ContentPresenter cp = node as ContentPresenter;
                bool isHdr = cp.ContentSource == "Header";
                if (isHdr == headerSource) { return cp; }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    public override void OnApplyTemplate() {
        UIElement found = GetTemplateChild("PART_ContentPresenter");
        if (!(found is ContentPresenter)) { found = GetTemplateChild("ContentHost"); }
        if (found is ContentPresenter) {
            _presenter = found as ContentPresenter;
        } else {
            _presenter = FindFirstTemplatedPresenter(TemplateRoot(), this, false);
        }
        if (_presenter != null) {
            _presenter.ContentTemplate = _contentTemplate;
            _presenter.SetContentBox(_contentBox);
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Content") { SetContentBox(value); return true; }
        if (propName == "ContentTemplate" && value is DataTemplate) {
            ContentTemplate = value as DataTemplate;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Content") { return _contentBox; }
        return base.GetPropertyValue(propName);
    }
}

public class Label : ContentControl {
    public Label() : base() {
        Padding = new Thickness(4.0f, 2.0f, 4.0f, 2.0f);
        HorizontalContentAlignment = HorizontalAlignment.Left;
        VerticalContentAlignment = VerticalAlignment.Center;
    }
    public override string GetTypeName() { return "Label"; }
}

public class HeaderedContentControl : ContentControl {
    private DependencyObject _headerBox;
    private DataTemplate _headerTemplate;
    protected ContentPresenter _headerPresenter;

    public HeaderedContentControl() : base() {}
    public override string GetTypeName() { return "HeaderedContentControl"; }

    public object Header {
        get { return DependencyObject.Unbox(_headerBox); }
        set { SetHeaderBox(DependencyObject.BoxObject(value)); }
    }
    public DependencyObject HeaderBox {
        get { return _headerBox; }
        set { SetHeaderBox(value); }
    }

    public void SetHeader(string text) { SetHeaderBox(DependencyObject.BoxString(text)); }
    public void SetHeader(float v) { SetHeaderBox(DependencyObject.BoxFloat(v)); }
    public void SetHeader(int v) { SetHeaderBox(DependencyObject.BoxInt(v)); }
    public void SetHeader(object v) { SetHeaderBox(DependencyObject.BoxObject(v)); }

    public void SetHeaderBox(DependencyObject box) {
        DependencyObject oldBox = _headerBox;
        _headerBox = box;
        if (_headerPresenter != null) { _headerPresenter.SetContentBox(_headerBox); }
        OnHeaderChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_headerBox));
    }

    public DataTemplate HeaderTemplate {
        get { return _headerTemplate; }
        set {
            _headerTemplate = value;
            if (_headerPresenter != null) { _headerPresenter.ContentTemplate = value; }
        }
    }

    protected virtual void OnHeaderChanged(object oldHeader, object newHeader) {}

    public override void OnApplyTemplate() {
        UIElement hFound = GetTemplateChild("PART_HeaderPresenter");
        if (!(hFound is ContentPresenter)) { hFound = GetTemplateChild("HeaderHost"); }
        if (hFound is ContentPresenter) {
            _headerPresenter = hFound as ContentPresenter;
        } else {
            _headerPresenter = FindFirstTemplatedPresenter(TemplateRoot(), this, true);
        }
        if (_headerPresenter != null) {
            _headerPresenter.ContentTemplate = _headerTemplate;
            _headerPresenter.SetContentBox(_headerBox);
        }
        base.OnApplyTemplate();
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Header") { SetHeaderBox(value); return true; }
        if (propName == "HeaderTemplate" && value is DataTemplate) {
            HeaderTemplate = value as DataTemplate;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Header") { return _headerBox; }
        return base.GetPropertyValue(propName);
    }
}

public class GroupBox : HeaderedContentControl {
    public GroupBox() : base() {
        Background = new SolidColorBrush("#FF1B202B");
        BorderBrush = new SolidColorBrush("#FF364054");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(8.0f);
        Padding = new Thickness(10.0f);
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }

    public override string GetTypeName() { return "GroupBox"; }

    protected override UIElement CreateDefaultTemplate() {
        var root = new StackPanel(Orientation.Vertical, 6.0f);
        var headerBar = new Border();
        headerBar.Background = new SolidColorBrush("#FF232A38");
        headerBar.CornerRadius = new CornerRadius(4.0f);
        headerBar.Padding = new Thickness(8.0f, 4.0f, 8.0f, 4.0f);
        _headerPresenter = new ContentPresenter();
        _headerPresenter.Name = "PART_HeaderPresenter";
        _headerPresenter.ContentTemplate = HeaderTemplate;
        _headerPresenter.SetContentBox(HeaderBox);
        headerBar.Child = _headerPresenter;

        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);

        root.AddChild(headerBar);
        root.AddChild(_presenter);
        return root;
    }
}

public class Expander : HeaderedContentControl {
    private bool _isExpanded = false;
    private ExpandDirection _expandDirection = ExpandDirection.Down;
    private Border _headerToggle;
    private TextBlock _chevron;
    private Border _bodyHost;
    private UIElement _customContentBorder;
    private UIElement _customContentBg;
    private UIElement _customArrow;

    public event RoutedEventHandler Expanded;
    public event RoutedEventHandler Collapsed;

    public Expander() : base() {
        Background = new SolidColorBrush("#FF1B202B");
        BorderBrush = new SolidColorBrush("#FF364054");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Focusable = true;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
    }

    public override string GetTypeName() { return "Expander"; }

    public bool IsExpanded {
        get { return _isExpanded; }
        set {
            if (_isExpanded == value) { return; }
            _isExpanded = value;
            SyncExpanderState();
            if (_isExpanded) {
                Expanded(this, new RoutedEventArgs("Expanded", this));
            } else {
                Collapsed(this, new RoutedEventArgs("Collapsed", this));
            }
        }
    }

    public ExpandDirection ExpandDirection {
        get { return _expandDirection; }
        set { _expandDirection = value; }
    }

    private void SyncExpanderState() {
        if (_chevron != null) {
            _chevron.Text = _isExpanded ? "▾" : "▸";
        }
        if (_bodyHost != null) {
            _bodyHost.Visibility = _isExpanded ? Visibility.Visible : Visibility.Collapsed;
        }
        if (_customContentBorder != null) {
            _customContentBorder.Visibility = _isExpanded ? Visibility.Visible : Visibility.Collapsed;
        }
        if (_customContentBg != null) {
            float sc = _isExpanded ? 1.0f : 0.0f;
            _customContentBg.RenderTransform = new ScaleTransform(sc, sc);
        }
        if (_customArrow != null) {
            float ang = _isExpanded ? 90.0f : 0.0f;
            _customArrow.RenderTransform = new RotateTransform(ang);
        }
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        _customContentBorder = GetTemplateChild("ContentBorder");
        _customContentBg = GetTemplateChild("ContentBg");
        _customArrow = GetTemplateChild("Arrow");
        SyncExpanderState();
    }

    protected override UIElement CreateDefaultTemplate() {
        var root = new StackPanel(Orientation.Vertical, 0.0f);

        _headerToggle = new Border();
        _headerToggle.Name = "PART_HeaderToggle";
        _headerToggle.Background = new SolidColorBrush("#FF232A38");
        _headerToggle.Padding = new Thickness(10.0f, 8.0f, 10.0f, 8.0f);
        _headerToggle.CornerRadius = new CornerRadius(5.0f);

        var row = new StackPanel(Orientation.Horizontal, 8.0f);
        _chevron = new TextBlock(_isExpanded ? "▾" : "▸");
        _chevron.FontSize = 13.0f;
        _chevron.VerticalAlignment = VerticalAlignment.Center;

        _headerPresenter = new ContentPresenter();
        _headerPresenter.Name = "PART_HeaderPresenter";
        _headerPresenter.ContentTemplate = HeaderTemplate;
        _headerPresenter.SetContentBox(HeaderBox);

        row.AddChild(_chevron);
        row.AddChild(_headerPresenter);
        _headerToggle.Child = row;
        _headerToggle.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
            if (IsEnabled) { IsExpanded = !IsExpanded; e.Handled = true; }
        };

        _bodyHost = new Border();
        _bodyHost.Padding = new Thickness(10.0f, 8.0f, 10.0f, 8.0f);
        _bodyHost.Visibility = _isExpanded ? Visibility.Visible : Visibility.Collapsed;

        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        _bodyHost.Child = _presenter;

        root.AddChild(_headerToggle);
        root.AddChild(_bodyHost);
        return root;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsExpanded") { IsExpanded = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsExpanded") { return DependencyObject.BoxBool(_isExpanded); }
        return base.GetPropertyValue(propName);
    }
}

public class Separator : Control {
    public Separator() : base() {
        Height = 1.0f;
        Background = new SolidColorBrush("#FF364054");
        Margin = new Thickness(0.0f, 4.0f, 0.0f, 4.0f);
        HorizontalAlignment = HorizontalAlignment.Stretch;
    }
    public override string GetTypeName() { return "Separator"; }
}
