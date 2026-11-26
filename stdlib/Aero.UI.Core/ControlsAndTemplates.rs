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
    private HorizontalAlignment _hContentAlign = HorizontalAlignment.Left;
    private VerticalAlignment _vContentAlign = VerticalAlignment.Center;
    private ControlTemplate _template;
    private UIElement _templateRoot;
    private bool _templateApplied = false;
    protected bool _hasExplicitFontSize = false;
    private bool _hasExplicitFontFamily = false;
    private bool _hasExplicitFontWeight = false;
    private bool _hasExplicitForeground = false;
    private bool _isTabStop = true;
    private bool _templateHasVsm = false;

    public Control() : base(VisualKind.Box) {
        if (_fontFamily == null || _fontFamily.Length == 0) { _fontFamily = "Segoe UI"; }
        if (_fontSize <= 0.0f) { _fontSize = 14.0f; }
        if (_fontWeight == null || _fontWeight.Length == 0) { _fontWeight = "Normal"; }
        if (_fontStyle == null || _fontStyle.Length == 0) { _fontStyle = "Normal"; }
        _hContentAlign = HorizontalAlignment.Left;
        _vContentAlign = VerticalAlignment.Center;
        ThemeManager.EnsureInitialized();
        _foreground = Brushes.DefaultForeground();
    }

    public override string GetTypeName() { return "Control"; }

    public virtual DataTemplate GetNamedContentTemplate(string source) { return null; }
    public virtual DependencyObject GetNamedContentBox(string source) { return null; }
    public virtual bool HasNamedContentSlot(string source) { return false; }

    public Brush Foreground {
        get {
            if (!_hasExplicitForeground) {
                Brush eff = EffectiveTextForeground();
                if (eff != null) { return eff; }
            }
            return _foreground;
        }
        set { _hasExplicitForeground = true; _foreground = value; BumpPropEpoch(); SetExplicitTextForeground(value); OnTypographyChanged(); }
    }
    public string FontFamily {
        get { return _fontFamily; }
        set { _hasExplicitFontFamily = true; _fontFamily = value == null ? "Segoe UI" : value; BumpPropEpoch(); SetExplicitTextFontFamily(_fontFamily); OnTypographyChanged(); }
    }
    public float FontSize {
        get { return _fontSize; }
        set { _hasExplicitFontSize = true; _fontSize = value > 0.0f ? value : 14.0f; BumpPropEpoch(); SetExplicitTextFontSize(_fontSize); OnTypographyChanged(); }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set { _hasExplicitFontWeight = true; _fontWeight = value == null ? "Normal" : value; BumpPropEpoch(); SetExplicitTextFontWeight(_fontWeight); OnTypographyChanged(); }
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
        if (changed) { BumpPropEpoch(); OnTypographyChanged(); }
    }
    public string FontStyle {
        get { return _fontStyle; }
        set { _fontStyle = value == null ? "Normal" : value; BumpPropEpoch(); OnTypographyChanged(); }
    }
    public HorizontalAlignment HorizontalContentAlignment {
        get { return _hContentAlign; }
        set { _hContentAlign = value; BumpPropEpoch(); OnContentAlignmentChanged(); }
    }
    public VerticalAlignment VerticalContentAlignment {
        get { return _vContentAlign; }
        set { _vContentAlign = value; BumpPropEpoch(); OnContentAlignmentChanged(); }
    }

    public ControlTemplate Template {
        get { return _template; }
        set {
            MarkLocalProperty("Template");
            if (_template == value && (_templateApplied || UIElement._closedPopupBuildDepth > 0)) { return; }
            if (_template != null && _templateApplied) {
                _template.ResetTriggers(this);
            }
            _template = value;
            _hasTemplateResources = (_template != null && (_template.HasResources() || _template.RawResourcesOrNull() != null));
            _hasTemplateAmbientResources = (_template != null && _template.AmbientResources != null);
            ResourceDictionary.BumpGlobalMutationEpoch();
            _templateApplied = false;
            _templateHasVsm = false;
            if (UIElement._closedPopupBuildDepth > 0) { return; }
            ApplyTemplate();
        }
    }
    public bool IsTabStop {
        get { return _isTabStop; }
        set {
            if (_isTabStop == value) { return; }
            _isTabStop = value;
            BumpPropEpoch();
            UpdateTriggersAndStates();
        }
    }
    public UIElement TemplateRoot() { return _templateRoot; }
    public bool IsTemplateApplied() { return _templateApplied; }

    protected virtual UIElement CreateDefaultTemplate() { return null; }
    protected virtual void UpdateCommonVisualStates(bool useTransitions) {
        if (!IsEnabled) {
            VisualStateManager.GoToState(this, "Disabled", useTransitions);
        } else if (IsMouseOver) {
            VisualStateManager.GoToState(this, "MouseOver", useTransitions);
        } else {
            VisualStateManager.GoToState(this, "Normal", useTransitions);
        }
    }

    protected virtual void UpdateVisualStates(bool useTransitions) {
        if (!_templateHasVsm && _vsgBox == null && !VisualStateManager._anyNonControlVsg) { return; }
        UpdateCommonVisualStates(useTransitions);
        VisualStateManager.GoToState(this, IsFocused ? "Focused" : "Unfocused", useTransitions);
    }

    protected override bool IsSelfHitTestSolid() {
        if (_template != null) { return false; }
        return base.IsSelfHitTestSolid();
    }

    protected override void SyncBoxStyleToNative() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        if (_template != null) {
            Visual_SetNodeVisualFlags(__nativeHandle, true, true, false);
            return;
        }
        Visual_SetNodeVisualFlags(__nativeHandle, false, false, false);
        base.SyncBoxStyleToNative();
    }

    protected override void SyncPadding() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        if (_template != null) {
            Visual_SetNodeVisualFlags(__nativeHandle, true, true, false);
            return;
        }
        Visual_SetNodeVisualFlags(__nativeHandle, false, false, false);
        base.SyncPadding();
    }

    protected bool HasExplicitForeground() { return _hasExplicitForeground; }

    protected void SetInheritedDefaultForeground(Brush fg) {
        if (_hasExplicitForeground || fg == null) { return; }
        _foreground = fg;
        BumpPropEpoch();
        SetExplicitTextForeground(fg);
        OnTypographyChanged();
    }

    protected void UpdateInheritedForegroundForBackground(Brush bg) {
        if (_hasExplicitForeground || bg == null) { return; }
        uint rgba = bg.ResolveRgba();
        uint a = (rgba / 16777216u) % 256u;
        if (a <= 128u) { return; }
        uint r = rgba % 256u;
        uint g = (rgba / 256u) % 256u;
        uint b = (rgba / 65536u) % 256u;
        uint lum = (r * 299u + g * 587u + b * 114u) / 1000u;
        if (lum > 150u) {
            SetInheritedDefaultForeground(Brushes.Black());
        } else {
            SetInheritedDefaultForeground(Brushes.DefaultForeground());
        }
    }

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
        UpdateVisualStates(true);
    }

    private void SyncTemplatePresenters() {
        if (_templateRoot != null) {
            PropagateTypography(_templateRoot);
        }
    }

    internal static bool IsOwnedByTemplateChain(UIElement node, UIElement owner) {
        if (node == null || owner == null) { return false; }
        UIElement tp = node.TemplatedParent;
        int hops = 0;
        while (tp != null && hops < 8) {
            if (tp == owner) { return true; }
            tp = tp.TemplatedParent;
            hops += 1;
        }
        return false;
    }

    private static void ExpandNestedTemplateControls(UIElement rootNode, UIElement owner) {
        if (rootNode == null) { return; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node != owner && node.IsItemsControlBoundary() && node.TemplatedParent != owner) { continue; }
            if ((node is ContentPresenter || node.IsItemsPresenterBoundary()) && node.TemplatedParent == owner) { continue; }
            if (node != owner && node is Control && node.TemplatedParent == owner) {
                Control subCtrl = node as Control;
                if (subCtrl._appliedStyle == null || (subCtrl.Style != null && subCtrl.Style != subCtrl._appliedStyle)) {
                    subCtrl.ApplyStyleInternal();
                }
                if (!subCtrl.IsTemplateApplied() && subCtrl.TemplateRoot() == null) {
                    subCtrl.ApplyTemplate();
                }
            }
            if (node != rootNode && node.IsClosedPopupBoundary()) { continue; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                UIElement ch = node.ChildAt(i);
                if (ch != null && IsOwnedByTemplateChain(ch, owner)) { q.Push(ch); }
            }
        }
    }

    private void PropagateTypography(UIElement rootNode) {
        PropagateTypographyToSubtree();
        if (rootNode == null) { return; }
        UIElement self = this;
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node is ContentPresenter && node.TemplatedParent == self) {
                (node as ContentPresenter).SyncFromTemplatedParent(this);
                continue;
            }
            if (node.IsItemsPresenterBoundary() && node.TemplatedParent == self) {
                continue;
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                UIElement ch = node.ChildAt(i);
                if (ch != null && IsOwnedByTemplateChain(ch, self)) {
                    q.Push(ch);
                }
            }
        }
    }

    public UIElement GetTemplateChild(string name) {
        if (_templateRoot != null) {
            return _templateRoot.FindTemplateChildName(name, this);
        }
        return FindName(name);
    }

    internal override UIElement GetTemplateRootInternal() { return _templateRoot; }
    internal override UIElement GetTemplateChildInternal(string name) { return GetTemplateChild(name); }
    internal override bool IsOwnedByTemplateChainInternal(UIElement owner) { return Control.IsOwnedByTemplateChain(this, owner); }

    private static void DetachUserContentFromTemplate(UIElement root, UIElement owner) {
        if (root == null) { return; }
        var q = new UIElement[0];
        q.Push(root);
        int qi = 0;
        while (qi < q.Length) {
            UIElement cur = q[qi];
            qi += 1;
            if (cur == null) { continue; }
            if (cur is ContentPresenter) {
                for (int i = cur.ChildCount() - 1; i >= 0; i -= 1) {
                    UIElement ch = cur.ChildAt(i);
                    if (ch != null) {
                        if (IsOwnedByTemplateChain(ch, owner)) {
                            q.Push(ch);
                        } else {
                            cur.RemoveChild(ch);
                        }
                    }
                }
                continue;
            }
            if (cur is Panel && (cur as Panel).IsItemsHost) {
                for (int i = cur.ChildCount() - 1; i >= 0; i -= 1) {
                    UIElement ch = cur.ChildAt(i);
                    if (ch != null) {
                        if (IsOwnedByTemplateChain(ch, owner)) {
                            q.Push(ch);
                        } else {
                            cur.RemoveChild(ch);
                        }
                    }
                }
                continue;
            }
            if (cur.DetachNonTemplateUserContent(owner, q)) {
                continue;
            }
            for (int i = cur.ChildCount() - 1; i >= 0; i -= 1) {
                UIElement ch = cur.ChildAt(i);
                if (ch != null && IsOwnedByTemplateChain(ch, owner)) { q.Push(ch); }
            }
        }
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        if (_templateRoot != null) {
            if (!_templateRoot._isDisposedTree) { q.Push(_templateRoot); }
            _templateRoot = null;
        }
        bool hadRes = _hasTemplateResources || _hasTemplateAmbientResources;
        _template = null;
        _hasTemplateResources = false;
        _hasTemplateAmbientResources = false;
        if (hadRes) {
            ResourceDictionary.BumpGlobalMutationEpoch();
        }
    }

    public override bool ApplyTemplate() {
        if (_isDisposedTree) { return false; }
        if (_template != null && _templateApplied) {
            _template.ResetTriggers(this);
        }
        bool prevHasTplRes = _hasTemplateResources;
        bool prevHasTplAmb = _hasTemplateAmbientResources;
        UIElement self = this;
        if (_templateRoot != null) {
            DetachUserContentFromTemplate(_templateRoot, self);
            if (_templateRoot.Parent == self) {
                RemoveChild(_templateRoot);
            }
            _templateRoot.DisposeSubtree();
            _templateRoot = null;
        }
        UIElement nextRoot = null;
        if (_template != null) {
            UIElement._xamlBuildDepth += 1;
            nextRoot = _template.Instantiate(this);
            UIElement._xamlBuildDepth -= 1;
        } else {
            nextRoot = CreateDefaultTemplate();
        }
        _templateRoot = nextRoot;
        _hasTemplateResources = (_template != null && (_template.HasResources() || _template.RawResourcesOrNull() != null));
        _hasTemplateAmbientResources = (_template != null && _template.AmbientResources != null);
        if (prevHasTplRes || prevHasTplAmb || _hasTemplateResources || _hasTemplateAmbientResources) {
            ResourceDictionary.BumpGlobalMutationEpoch();
        }
        _templateApplied = true;
        SyncBoxStyleToNative();
        SyncPadding();
        SyncInteractiveFlags();
        if (_templateRoot != null) {
            if (_template == null || _templateRoot.TemplatedParent != self) {
                MarkTemplateOwner(_templateRoot, this);
            }
            BindingEngine.BeginBatch();
            bool prevSuppress = _suppressChildAutoLoaded;
            bool prevSuppressStyle = _suppressChildAutoStyle;
            _suppressChildAutoLoaded = true;
            _suppressChildAutoStyle = true;
            AddChild(_templateRoot);
            _suppressChildAutoStyle = prevSuppressStyle;
            _suppressChildAutoLoaded = prevSuppress;
            _templateRoot.ApplyStyleSubtree();
            _templateRoot.RefreshDynamicResourcesRecursive();
            ExpandNestedTemplateControls(_templateRoot, self);
            SyncTemplatePresenters();
            BindingEngine.EndBatch();
            BindingEngine.FlushSubtree(_templateRoot);
        }
        if (_templateRoot != null && _template != null) {
            _template.AttachNonEventTriggers(this);
        }
        OnApplyTemplate();
        if (_templateRoot != null && _template != null) {
            _template.EvaluateTriggers(this);
            _template.AttachEventTriggers(this);
        }
        if (_templateRoot != null && HasLoadedFired() && !_loadingSelf) {
            _templateRoot.EnsureLoadedSubtree();
        }
        _templateHasVsm = (_templateRoot != null) && Control.SubtreeHasVsm(_templateRoot, this);
        UpdateVisualStates(false);
        return _templateRoot != null;
    }

    private static bool SubtreeHasVsm(UIElement node, UIElement owner) {
        if (node == null) { return false; }
        if (node._vsgBox != null) { return true; }
        int cc = node.ChildCount();
        for (int c = 0; c < cc; c += 1) {
            UIElement ch = node.ChildAt(c);
            if (ch != null && ch.TemplatedParent == owner && Control.SubtreeHasVsm(ch, owner)) {
                return true;
            }
        }
        return false;
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
            if (el.TemplatedParent == null) { el.SetTemplatedParentDirect(ownerEl); }
            if (el is ContentPresenter || el.TemplatedParent != ownerEl) { continue; }
            for (int i = 0; i < el.ChildCount(); i += 1) {
                q.Push(el.ChildAt(i));
            }
            if (el is ContentControl && el.TemplatedParent == ownerEl) {
                DependencyObject cb = (el as ContentControl).ContentBox;
                if (cb is UIElement) {
                    UIElement cEl = cb as UIElement;
                    if (cEl.TemplatedParent == null && cEl != ownerEl &&
                        !(ownerEl is ContentControl && (ownerEl as ContentControl).ContentBox == cb)) {
                        q.Push(cEl);
                    }
                }
            }
        }
    }

    protected override void OnLoaded() {
        if (!_templateApplied) {
            ApplyTemplate();
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Foreground" || propName == "Foreground.Color") { Foreground = UIElement.CoerceBrush(value); return true; }
        if (propName == "FontFamily") { FontFamily = DependencyObject.UnboxString(value, "Segoe UI"); return true; }
        if (propName == "FontSize") { FontSize = DependencyObject.UnboxFloat(value, 14.0f); return true; }
        if (propName == "FontWeight") { FontWeight = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "FontStyle") { FontStyle = DependencyObject.UnboxString(value, "Normal"); return true; }
        if (propName == "IsTabStop" || propName == "KeyboardNavigation.IsTabStop") { IsTabStop = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "Template" && value is ControlTemplate) {
            Template = value as ControlTemplate;
            return true;
        }
        if (propName == "HorizontalContentAlignment") {
            if (value is BoxedInt) { HorizontalContentAlignment = EnumConvert.ToHorizontalAlignment((value as BoxedInt).Value); return true; }
            string ha = DependencyObject.UnboxString(value, "Center");
            if (ha == "Left") { HorizontalContentAlignment = HorizontalAlignment.Left; return true; }
            if (ha == "Right") { HorizontalContentAlignment = HorizontalAlignment.Right; return true; }
            if (ha == "Stretch") { HorizontalContentAlignment = HorizontalAlignment.Stretch; return true; }
            HorizontalContentAlignment = HorizontalAlignment.Center;
            return true;
        }
        if (propName == "VerticalContentAlignment") {
            if (value is BoxedInt) { VerticalContentAlignment = EnumConvert.ToVerticalAlignment((value as BoxedInt).Value); return true; }
            string va = DependencyObject.UnboxString(value, "Center");
            if (va == "Top") { VerticalContentAlignment = VerticalAlignment.Top; return true; }
            if (va == "Bottom") { VerticalContentAlignment = VerticalAlignment.Bottom; return true; }
            if (va == "Stretch") { VerticalContentAlignment = VerticalAlignment.Stretch; return true; }
            VerticalContentAlignment = VerticalAlignment.Center;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    protected override DependencyObject TryFindTemplateScopedResourceBox(string key) {
        if (_template != null && _template.HasResources()) {
            return _template.Resources.TryGetLocalBox(key);
        }
        return null;
    }

    protected override DependencyObject TryFindTemplateAmbientResourceBox(string key) {
        if (_template != null && _template.AmbientResources != null) {
            return _template.AmbientResources.TryGetLocalBox(key);
        }
        return null;
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Foreground") { return Foreground; }
        if (propName == "Foreground.Color") {
            Brush fg = Foreground;
            return DependencyObject.BoxString(fg != null ? ParseUtil.FormatArgbHex(fg.ResolveRgba()) : "#FFF1F5F9");
        }
        if (propName == "FontSize") { return DependencyObject.BoxFloat(_fontSize); }
        if (propName == "FontFamily") { return DependencyObject.BoxString(_fontFamily); }
        if (propName == "FontWeight") { return DependencyObject.BoxString(_fontWeight); }
        if (propName == "HorizontalContentAlignment") { return DependencyObject.BoxInt(EnumConvert.ToInt(_hContentAlign)); }
        if (propName == "VerticalContentAlignment") { return DependencyObject.BoxInt(EnumConvert.ToInt(_vContentAlign)); }
        if (propName == "IsTabStop" || propName == "KeyboardNavigation.IsTabStop") { return DependencyObject.BoxBool(_isTabStop); }
        if (propName == "Template") { return _template; }
        return base.GetPropertyValue(propName);
    }
}

public class ContentPresenter : FrameworkElement {
    private DependencyObject _contentBox;
    private DataTemplate _contentTemplate;
    private UIElement _templateInstanceRoot;
    private TextBlock _textView;
    private Brush _foreground;
    private bool _hasExplicitForeground = false;
    private bool _hasExplicitFontFamily = false;
    private bool _hasExplicitFontSize = false;
    private bool _hasExplicitFontWeight = false;
    private string _fontFamily = "Segoe UI";
    private float _fontSize = 14.0f;
    private string _fontWeight = "Normal";
    public string ContentSource { get; set; } = "Content";

    public ContentPresenter() : base(VisualKind.Box) {
        _foreground = Brushes.DefaultForeground();
        SyncInteractiveFlags();
    }

    public override string GetTypeName() { return "ContentPresenter"; }
    public override bool IsNamescopeBoundary() { return _contentTemplate != null || base.IsNamescopeBoundary(); }
    protected virtual bool IsScrollPresenter() { return false; }

    protected override bool IsSelfHitTestSolid() {
        return Background != null;
    }

    public Brush Foreground {
        get { return _foreground; }
        set {
            _hasExplicitForeground = true;
            _foreground = value;
            SetExplicitTextForeground(value);
            if (_textView != null) { _textView.Foreground = value; }
        }
    }
    public string FontFamily {
        get { return _fontFamily; }
        set {
            _hasExplicitFontFamily = true;
            _fontFamily = value == null ? "Segoe UI" : value;
            SetExplicitTextFontFamily(_fontFamily);
            if (_textView != null) { _textView.FontFamily = _fontFamily; }
        }
    }
    public float FontSize {
        get { return _fontSize; }
        set {
            _hasExplicitFontSize = true;
            _fontSize = value > 0.0f ? value : 14.0f;
            SetExplicitTextFontSize(_fontSize);
            if (_textView != null) { _textView.FontSize = _fontSize; }
        }
    }
    public string FontWeight {
        get { return _fontWeight; }
        set {
            _hasExplicitFontWeight = true;
            _fontWeight = value == null ? "Normal" : value;
            SetExplicitTextFontWeight(_fontWeight);
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
        BumpPropEpoch();
        RebuildContentVisual();
    }

    public DataTemplate ContentTemplate {
        get { return _contentTemplate; }
        set {
            if (_contentTemplate == value) { return; }
            _contentTemplate = value;
            BumpPropEpoch();
            RebuildContentVisual();
        }
    }

    public void SetContentAndTemplate(DependencyObject box, DataTemplate tpl) {
        if (_contentBox == box && _contentTemplate == tpl && ChildCount() > 0) { return; }
        _contentBox = box;
        _contentTemplate = tpl;
        BumpPropEpoch();
        RebuildContentVisual();
    }

    private void SyncTextViewAlignment() {
        if (_textView == null) { return; }
        _textView.HorizontalAlignment = HorizontalAlignment.Stretch;
        _textView.VerticalAlignment = VerticalAlignment.Center;
        if (HorizontalAlignment == HorizontalAlignment.Center) {
            _textView.TextAlignment = TextAlignment.Center;
        } else if (HorizontalAlignment == HorizontalAlignment.Right) {
            _textView.TextAlignment = TextAlignment.Right;
        } else {
            _textView.TextAlignment = TextAlignment.Left;
        }
    }

    internal bool _syncOwnerContentAlignment = false;

    public void SyncFromTemplatedParent(Control owner) {
        if (owner == null) { return; }
        UIElement ownerEl = owner;
        if (TemplatedParent != ownerEl) { return; }
        Brush effFg = EffectiveTextForeground();
        string effFf = EffectiveTextFontFamilyOrEmpty();
        float effFs = EffectiveTextFontSizeOrZero();
        string effFw = EffectiveTextFontWeightOrEmpty();
        if (!_hasExplicitForeground) {
            _foreground = (effFg != null && ownerEl.IsAncestorOf(this)) ? effFg : owner.Foreground;
        }
        if (!_hasExplicitFontFamily) {
            _fontFamily = (effFf.Length > 0 && ownerEl.IsAncestorOf(this)) ? effFf : owner.FontFamily;
        }
        if (!_hasExplicitFontSize) {
            _fontSize = (effFs > 0.0f && ownerEl.IsAncestorOf(this)) ? effFs : owner.FontSize;
        }
        if (!_hasExplicitFontWeight) {
            _fontWeight = (effFw.Length > 0 && ownerEl.IsAncestorOf(this)) ? effFw : owner.FontWeight;
        }
        if (!ownerEl.IsAncestorOf(this) || effFg == null) {
            ReceiveInheritedTypography(_fontSize, _fontFamily, _fontWeight, _foreground);
        }
        if (_syncOwnerContentAlignment && !IsScrollPresenter()) {
            if (!_hasExplicitHAlign) { HorizontalAlignment = owner.HorizontalContentAlignment; _hasExplicitHAlign = false; }
            if (!_hasExplicitVAlign) { VerticalAlignment = owner.VerticalContentAlignment; _hasExplicitVAlign = false; }
        }
        if (_textView != null) {
            _textView.Foreground = _foreground;
            _textView.FontFamily = _fontFamily;
            _textView.FontSize = _fontSize;
            _textView.FontWeight = _fontWeight;
            SyncTextViewAlignment();
        }
        if (ContentSource == "SelectionBoxItem") {
            return;
        }
        if (owner.HasNamedContentSlot(ContentSource)) {
            bool needRebuild = false;
            DataTemplate namedTpl = owner.GetNamedContentTemplate(ContentSource);
            DependencyObject namedBox = owner.GetNamedContentBox(ContentSource);
            if (_contentTemplate == null && namedTpl != null) {
                _contentTemplate = namedTpl;
                needRebuild = true;
            }
            if (_contentBox == null && namedBox != null) {
                UIElement self = this;
                UIElement boxEl = (namedBox is UIElement) ? (namedBox as UIElement) : null;
                if (boxEl == null || (boxEl != self && !boxEl.IsAncestorOf(self))) {
                    _contentBox = namedBox;
                    needRebuild = true;
                }
            }
            if (needRebuild) {
                RebuildContentVisual();
            }
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
            if (fg != null && _foreground != fg) { _foreground = fg; changed = true; }
        }
        if (changed) {
            BumpPropEpoch();
            if (_textView != null) {
                _textView.Foreground = _foreground;
                _textView.FontFamily = _fontFamily;
                _textView.FontSize = _fontSize;
                _textView.FontWeight = _fontWeight;
            }
        }
    }

    protected override void OnLoaded() {
        base.OnLoaded();
        if (_contentBox != null && _contentTemplate == null && _templateInstanceRoot == null &&
            !(_contentBox is UIElement) && !(_contentBox is BoxedString) &&
            !(_contentBox is BoxedFloat) && !(_contentBox is BoxedInt) && !(_contentBox is BoxedBool)) {
            if (DataTemplate.ResolveImplicitDataTemplate(this, DependencyObject.Unbox(_contentBox)) != null) {
                RebuildContentVisual();
            }
        }
    }

    private void DisposeTextViewIfAny() {
        if (_textView == null) { return; }
        UIElement oldTv = _textView;
        _textView = null;
        UIElement self = this;
        if (oldTv.Parent == self) { RemoveChild(oldTv); }
        oldTv.DisposeSubtree();
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        if (_textView != null) {
            if (!_textView._isDisposedTree) { q.Push(_textView); }
            _textView = null;
        }
        if (_templateInstanceRoot != null) {
            if (!_templateInstanceRoot._isDisposedTree) { q.Push(_templateInstanceRoot); }
            _templateInstanceRoot = null;
        }
        _contentBox = null;
        _contentTemplate = null;
    }

    public void RebuildContentVisual() {
        if (_isDisposedTree) { return; }
        if (_templateInstanceRoot != null) {
            UIElement oldInst = _templateInstanceRoot;
            _templateInstanceRoot = null;
            UIElement self = this;
            if (oldInst.Parent == self) { RemoveChild(oldInst); }
            oldInst.MarkDisposedSubtree();
        }
        if (_contentBox == null) {
            if (_contentTemplate != null) {
                DisposeTextViewIfAny();
                ClearChildren();
                UIElement._xamlBuildDepth += 1;
                UIElement builtNull = _contentTemplate.Instantiate(null);
                UIElement._xamlBuildDepth -= 1;
                _templateInstanceRoot = builtNull;
                if (builtNull != null) {
                    bool prevSuppressStyleNull = _suppressChildAutoStyle;
                    _suppressChildAutoStyle = true;
                    AddChild(builtNull);
                    _suppressChildAutoStyle = prevSuppressStyleNull;
                    if (UIElement._xamlBuildDepth == 0) {
                        builtNull.ApplyStyleSubtree();
                        builtNull.RefreshDynamicResourcesRecursive();
                        builtNull.ApplyTemplateSubtree();
                        if (HasLoadedFired() && !_suppressChildAutoLoaded) {
                            builtNull.EnsureLoadedSubtree();
                        }
                        BindingEngine.FlushSubtree(builtNull);
                    }
                }
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
            DisposeTextViewIfAny();
            UIElement el = _contentBox as UIElement;
            if (ChildCount() == 1 && ChildAt(0) == el) {
                BindingEngine.FlushSubtree(el);
                return;
            }
            ClearChildren();
            AddChild(el);
            BindingEngine.FlushSubtree(el);
            return;
        }
        DataTemplate effTemplate = _contentTemplate;
        if (effTemplate == null && !(_contentBox is BoxedString) && !(_contentBox is BoxedFloat) &&
            !(_contentBox is BoxedInt) && !(_contentBox is BoxedBool)) {
            effTemplate = DataTemplate.ResolveImplicitDataTemplate(this, DependencyObject.Unbox(_contentBox));
        }
        if (effTemplate != null) {
            DisposeTextViewIfAny();
            ClearChildren();
            UIElement._xamlBuildDepth += 1;
            UIElement built = effTemplate.Instantiate(DependencyObject.Unbox(_contentBox));
            UIElement._xamlBuildDepth -= 1;
            _templateInstanceRoot = built;
            if (built != null) {
                bool prevSuppressStyleBuilt = _suppressChildAutoStyle;
                _suppressChildAutoStyle = true;
                AddChild(built);
                _suppressChildAutoStyle = prevSuppressStyleBuilt;
                if (UIElement._xamlBuildDepth == 0) {
                    built.ApplyStyleSubtree();
                    built.RefreshDynamicResourcesRecursive();
                    built.ApplyTemplateSubtree();
                    if (HasLoadedFired() && !_suppressChildAutoLoaded) {
                        built.EnsureLoadedSubtree();
                    }
                    BindingEngine.FlushSubtree(built);
                }
            }
            return;
        }
        if (!_hasExplicitForeground) {
            Brush effFg = EffectiveTextForeground();
            if (effFg != null) { _foreground = effFg; }
        }
        if (!_hasExplicitFontFamily) {
            string effFf = EffectiveTextFontFamilyOrEmpty();
            if (effFf.Length > 0) { _fontFamily = effFf; }
        }
        if (!_hasExplicitFontSize) {
            float effFs = EffectiveTextFontSizeOrZero();
            if (effFs > 0.0f) { _fontSize = effFs; }
        }
        if (!_hasExplicitFontWeight) {
            string effFw = EffectiveTextFontWeightOrEmpty();
            if (effFw.Length > 0) { _fontWeight = effFw; }
        }
        string text = DependencyObject.UnboxString(_contentBox, "");
        if (_textView == null) {
            ClearChildren();
            _textView = new TextBlock(text);
            _textView.Foreground = _foreground;
            _textView.FontFamily = _fontFamily;
            _textView.FontSize = _fontSize;
            _textView.FontWeight = _fontWeight;
            SyncTextViewAlignment();
            AddChild(_textView);
        } else {
            _textView.Visibility = Visibility.Visible;
            _textView.Text = text;
            _textView.Foreground = _foreground;
            _textView.FontFamily = _fontFamily;
            _textView.FontSize = _fontSize;
            _textView.FontWeight = _fontWeight;
            SyncTextViewAlignment();
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Content") { SetContentBox(value); return true; }
        if (propName == "ContentSource") { ContentSource = DependencyObject.UnboxString(value, "Content"); return true; }
        if (propName == "ContentTemplate" && value is DataTemplate) {
            ContentTemplate = value as DataTemplate;
            return true;
        }
        if (propName == "Foreground" || propName == "TextElement.Foreground" || propName == "Foreground.Color") {
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
        if (propName == "Foreground.Color") {
            return DependencyObject.BoxString(_foreground != null ? ParseUtil.FormatArgbHex(_foreground.ResolveRgba()) : "#FFF1F5F9");
        }
        if (propName == "FontFamily" || propName == "TextElement.FontFamily") { return DependencyObject.BoxString(_fontFamily); }
        if (propName == "FontSize" || propName == "TextElement.FontSize") { return DependencyObject.BoxFloat(_fontSize); }
        if (propName == "FontWeight" || propName == "TextElement.FontWeight") { return DependencyObject.BoxString(_fontWeight); }
        return base.GetPropertyValue(propName);
    }
}

public class ScrollContentPresenter : ContentPresenter {
    private bool _canContentScroll = false;

    public bool CanContentScroll {
        get { return _canContentScroll; }
        set { _canContentScroll = value; }
    }

    public ScrollContentPresenter() : base() {
        ClipToBounds = true;
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetLayoutMode(__nativeHandle, EnumConvert.ToInt(LayoutMode.ScrollViewport), 0.0f, 0.0f, 1, 0);
        }
    }

    public override string GetTypeName() { return "ScrollContentPresenter"; }
    protected override bool IsScrollPresenter() { return true; }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "CanContentScroll") { CanContentScroll = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "CanContentScroll") { return DependencyObject.BoxBool(_canContentScroll); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 3. ContentControl, HeaderedContentControl, GroupBox, Expander, Separator
// ============================================================================

public class ContentControl : Control {
    private DependencyObject _contentBox;
    private DataTemplate _contentTemplate;
    protected ContentPresenter _presenter;

    public ContentControl() : base() {
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }
    public override string GetTypeName() { return "ContentControl"; }

    public override DataTemplate GetNamedContentTemplate(string source) {
        if (source != "Header" && source != "SelectionBoxItem") { return _contentTemplate; }
        return null;
    }
    public override DependencyObject GetNamedContentBox(string source) {
        if (source != "Header" && source != "SelectionBoxItem") { return _contentBox; }
        return null;
    }
    public override bool HasNamedContentSlot(string source) {
        return source != "Header" && source != "SelectionBoxItem";
    }

    public object Content {
        get { return DependencyObject.Unbox(_contentBox); }
        set { SetContentBox(DependencyObject.BoxObject(value)); }
    }
    public DependencyObject ContentBox {
        get { return _contentBox; }
        set { SetContentBox(value); }
    }
    internal override DependencyObject GetContentBoxInternal() { return _contentBox; }
    public bool HasContent { get { return _contentBox != null; } }

    public void SetContent(string text) { SetContentBox(DependencyObject.BoxString(text)); }
    public void SetContent(float v) { SetContentBox(DependencyObject.BoxFloat(v)); }
    public void SetContent(int v) { SetContentBox(DependencyObject.BoxInt(v)); }
    public void SetContent(bool v) { SetContentBox(DependencyObject.BoxBool(v)); }
    public void SetContent(object v) { SetContentBox(DependencyObject.BoxObject(v)); }

    protected virtual bool ShouldRealizeContent() { return true; }

    public void SetContentBox(DependencyObject newBox) {
        DependencyObject oldBox = _contentBox;
        _contentBox = newBox;
        if (_presenter != null) {
            _presenter.SetContentBox(ShouldRealizeContent() ? _contentBox : null);
        }
        NotifyBoundPropertyChanged("Content", _contentBox);
        if (UIElement._attachedPropHook != null) {
            var hook = UIElement._attachedPropHook;
            hook(this, "DragDrop.__ContentChanged", _contentBox);
        }
        OnContentChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_contentBox));
        UpdateTriggersAndStates();
    }

    public DataTemplate ContentTemplate {
        get { return _contentTemplate; }
        set {
            _contentTemplate = value;
            BumpPropEpoch();
            if (_presenter != null) { _presenter.ContentTemplate = value; }
        }
    }

    public void SetContentAndTemplate(DependencyObject newBox, DataTemplate tpl) {
        DependencyObject oldBox = _contentBox;
        _contentTemplate = tpl;
        _contentBox = newBox;
        if (_presenter != null) {
            _presenter.SetContentAndTemplate(ShouldRealizeContent() ? _contentBox : null, _contentTemplate);
        }
        NotifyBoundPropertyChanged("Content", _contentBox);
        if (UIElement._attachedPropHook != null) {
            var hook = UIElement._attachedPropHook;
            hook(this, "DragDrop.__ContentChanged", _contentBox);
        }
        OnContentChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_contentBox));
        UpdateTriggersAndStates();
    }

    protected virtual void OnContentChanged(object oldContent, object newContent) {}

    protected override UIElement CreateDefaultTemplate() {
        _presenter = new ContentPresenter();
        _presenter._syncOwnerContentAlignment = true;
        _presenter.Name = "PART_ContentPresenter";
        _presenter.SetContentAndTemplate(_contentBox, _contentTemplate);
        return _presenter;
    }

    public static ContentPresenter FindFirstTemplatedPresenter(
        UIElement rootNode, UIElement owner, bool headerSource) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node != rootNode && node.IsItemsControlBoundary()) { continue; }
            bool belongsToOwner = (node.TemplatedParent == null || node.TemplatedParent == owner);
            if (node is ContentPresenter) {
                ContentPresenter cp = node as ContentPresenter;
                if (belongsToOwner) {
                    bool isHdr = cp.ContentSource == "Header";
                    if (isHdr == headerSource) { return cp; }
                    if (node.TemplatedParent == owner) { continue; }
                }
            } else if (node != rootNode && node is Control) {
                Control subCtrl = node as Control;
                subCtrl.ApplyStyleInternal();
                if (subCtrl.TemplateRoot() == null) {
                    subCtrl.ApplyTemplate();
                }
            }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        if (_contentBox is UIElement) {
            UIElement cel = _contentBox as UIElement;
            UIElement self = this;
            UIElement pres = _presenter;
            if (!cel._isDisposedTree && (cel.Parent == null || cel.Parent == self || (pres != null && cel.Parent == pres))) {
                q.Push(cel);
            }
        }
        _contentBox = null;
        _contentTemplate = null;
        _presenter = null;
    }

    public override void OnApplyTemplate() {
        if (Template != null) { _presenter = null; }
        UIElement found = GetTemplateChild("PART_ContentPresenter");
        if (!(found is ContentPresenter) || (found as ContentPresenter).ContentSource == "Header") {
            found = GetTemplateChild("ContentHost");
        }
        if (found is ContentPresenter && (found as ContentPresenter).ContentSource != "Header") {
            _presenter = found as ContentPresenter;
        } else {
            _presenter = FindFirstTemplatedPresenter(TemplateRoot(), this, false);
        }
        if (_presenter != null) {
            if (_presenter.TemplatedParent == null) {
                _presenter.TemplatedParent = this;
                _presenter.SyncFromTemplatedParent(this);
            }
            _presenter.SetContentAndTemplate(ShouldRealizeContent() ? _contentBox : null, _contentTemplate);
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

    public override DataTemplate GetNamedContentTemplate(string source) {
        if (source == "Header") { return _headerTemplate; }
        return base.GetNamedContentTemplate(source);
    }
    public override DependencyObject GetNamedContentBox(string source) {
        if (source == "Header") { return _headerBox; }
        return base.GetNamedContentBox(source);
    }
    public override bool HasNamedContentSlot(string source) {
        if (source == "Header") { return true; }
        return base.HasNamedContentSlot(source);
    }

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
        NotifyBoundPropertyChanged("Header", _headerBox);
        OnHeaderChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_headerBox));
    }

    public DataTemplate HeaderTemplate {
        get { return _headerTemplate; }
        set {
            _headerTemplate = value;
            BumpPropEpoch();
            if (_headerPresenter != null) { _headerPresenter.ContentTemplate = value; }
        }
    }

    public void SetHeaderAndTemplate(DependencyObject box, DataTemplate tpl) {
        DependencyObject oldBox = _headerBox;
        _headerTemplate = tpl;
        _headerBox = box;
        if (_headerPresenter != null) { _headerPresenter.SetContentAndTemplate(_headerBox, _headerTemplate); }
        NotifyBoundPropertyChanged("Header", _headerBox);
        OnHeaderChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_headerBox));
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        if (_headerBox is UIElement) {
            UIElement hel = _headerBox as UIElement;
            UIElement self = this;
            UIElement hpres = _headerPresenter;
            if (!hel._isDisposedTree && (hel.Parent == null || hel.Parent == self || (hpres != null && hel.Parent == hpres))) {
                q.Push(hel);
            }
        }
        _headerBox = null;
        _headerTemplate = null;
        _headerPresenter = null;
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
            _headerPresenter.SetContentAndTemplate(_headerBox, _headerTemplate);
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
    internal override int FastPropEpochCategory() { return 7; }
    private bool _isExpanded = false;
    private bool _hasRealizedContent = false;
    private ExpandDirection _expandDirection = ExpandDirection.Down;
    private Border _headerToggle;
    private TextBlock _chevron;
    private Border _bodyHost;

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

    protected override bool ShouldRealizeContent() { return _hasRealizedContent; }

    public override DependencyObject GetNamedContentBox(string source) {
        if (source != "Header" && source != "SelectionBoxItem" && !_hasRealizedContent) {
            return null;
        }
        return base.GetNamedContentBox(source);
    }

    private void RealizeExpanderContentIfNeeded() {
        if (_hasRealizedContent || _isDisposedTree) { return; }
        _hasRealizedContent = true;
        if (_presenter != null) {
            _presenter.SetContentAndTemplate(ContentBox, ContentTemplate);
            if (ContentBox is UIElement) {
                UIElement cel = ContentBox as UIElement;
                if (!cel._isDisposedTree && !cel.HasLoadedFired()) {
                    cel.ApplyStyleSubtree();
                    cel.RefreshDynamicResourcesRecursive();
                    cel.ApplyTemplateSubtree();
                    if (HasLoadedFired()) { cel.EnsureLoadedSubtree(); }
                    BindingEngine.FlushSubtree(cel);
                }
            }
        }
    }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        _headerToggle = null;
        _chevron = null;
        _bodyHost = null;
    }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    public bool IsExpanded {
        get { return _isExpanded; }
        set {
            if (_isExpanded == value) { return; }
            _isExpanded = value;
            if (_isExpanded) {
                RealizeExpanderContentIfNeeded();
            }
            SyncExpanderState();
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("IsExpanded", DependencyObject.BoxBool(_isExpanded));
            if (_isExpanded) {
                var expArgs = new RoutedEventArgs("Expanded", this);
                Expanded(this, expArgs);
                RaiseRoutedEvent("Expanded", expArgs);
            } else {
                var colArgs = new RoutedEventArgs("Collapsed", this);
                Collapsed(this, colArgs);
                RaiseRoutedEvent("Collapsed", colArgs);
            }
        }
    }

    public ExpandDirection ExpandDirection {
        get { return _expandDirection; }
        set {
            if (_expandDirection == value) { return; }
            _expandDirection = value;
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("ExpandDirection", DependencyObject.BoxInt(EnumConvert.ToInt(_expandDirection)));
        }
    }

    private void SyncExpanderState() {
        if (_chevron != null) {
            _chevron.Text = _isExpanded ? "▾" : "▸";
        }
        if (_bodyHost != null) {
            _bodyHost.Visibility = _isExpanded ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
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
        if (propName == "ExpandDirection") {
            if (value is BoxedInt) {
                int ev = (value as BoxedInt).Value;
                if (ev == 1) { ExpandDirection = ExpandDirection.Up; }
                else if (ev == 2) { ExpandDirection = ExpandDirection.Left; }
                else if (ev == 3) { ExpandDirection = ExpandDirection.Right; }
                else { ExpandDirection = ExpandDirection.Down; }
                return true;
            }
            string ed = DependencyObject.UnboxString(value, "Down");
            if (ed == "Up") { ExpandDirection = ExpandDirection.Up; }
            else if (ed == "Left") { ExpandDirection = ExpandDirection.Left; }
            else if (ed == "Right") { ExpandDirection = ExpandDirection.Right; }
            else { ExpandDirection = ExpandDirection.Down; }
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsExpanded") { return DependencyObject.BoxBool(_isExpanded); }
        if (propName == "ExpandDirection") { return DependencyObject.BoxInt(EnumConvert.ToInt(_expandDirection)); }
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

// ============================================================================
// UserControl, Page, Window & Application (Aero.UI.Core)
// ============================================================================

public delegate void WindowOverlayHook(UIElement host);
public delegate void WindowPopupCloseHook();

[Observable]
public class UserControl : ContentControl {
    public UserControl() : base() {
        IsTabStop = false;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }
    public override string GetTypeName() { return "UserControl"; }
    public override bool IsNamescopeBoundary() { return true; }
    internal override int FastPropEpochCategory() { return 0; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }
}

[Observable]
public class Page : ContentControl {
    public string Title { get; set; } = "";
    public string WindowTitle { get; set; } = "";
    public Page() : base() {
        IsTabStop = false;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }
    public override string GetTypeName() { return "Page"; }
    public override bool IsNamescopeBoundary() { return true; }
    internal override int FastPropEpochCategory() { return 0; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }
}

[Observable]
public class Window : ContentControl {
    internal static WindowOverlayHook _overlayHostHook;
    internal static WindowPopupCloseHook _closeAllPopupsHook;

    public string Title { get; set; } = "Aero Window";
    public string Icon { get; set; } = "";
    public string WindowState { get; set; } = "Normal";
    private bool _isActive = false;

    public event CancelEventHandler Closing;
    public event EventHandler Closed;
    public event EventHandler Activated;

    public bool IsActive { get { return _isActive; } }
    internal override int FastPropEpochCategory() { return 0; }

    public Window() : base() {
        ThemeManager.EnsureInitialized();
        Background = new SolidColorBrush("#FF12151C");
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
        if (_overlayHostHook != null) {
            var oh = _overlayHostHook;
            oh(this);
        }
    }

    public override string GetTypeName() { return "Window"; }
    public override bool IsNamescopeBoundary() { return true; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }

    public void Show() {
        _isActive = true;
        if (_overlayHostHook != null) {
            var oh = _overlayHostHook;
            oh(this);
        }
        Visibility = Visibility.Visible;
        if (!IsTemplateApplied()) { ApplyTemplate(); }
        EnsureLoadedSubtree();
        Activated(this, EventArgs.Empty());
    }

    public void Activate() {
        _isActive = true;
        Activated(this, EventArgs.Empty());
    }

    public void Close() {
        var ce = new CancelEventArgs(false);
        Closing(this, ce);
        if (ce.Cancel) { return; }
        _isActive = false;
        if (_closeAllPopupsHook != null) {
            var cp = _closeAllPopupsHook;
            cp();
        }
        Closed(this, EventArgs.Empty());
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Host_CloseWindow();
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        return _presenter;
    }

    public override void OnHostViewportResized(float width, float height) {
        if (width > 1.0f && height > 1.0f) {
            if (!ParseUtil.IsNaN(Width)) { Width = ParseUtil.NaN(); }
            if (!ParseUtil.IsNaN(Height)) { Height = ParseUtil.NaN(); }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Title") { Title = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

public class Application : DependencyObject {
    private static Application _current;
    private ResourceDictionary _resources;

    public Window MainWindow { get; set; }
    public event StartupEventHandler Startup;
    public event EventHandler Exit;

    public Application() {
        _current = this;
        _resources = ResourceDictionary.GlobalApplicationResources();
        ThemeManager.EnsureInitialized();
    }

    public static Application Current() {
        if (_current == null) { _current = new Application(); }
        return _current;
    }

    public ResourceDictionary Resources {
        get { return _resources; }
        set {
            _resources = value;
            ResourceDictionary.BumpGlobalMutationEpoch();
            if (value != null) { ResourceDictionary.GlobalApplicationResources().Merge(value); }
        }
    }

    public object FindResource(string key) {
        if (_resources != null) {
            object localVal = _resources.Get(key);
            if (localVal != null) { return localVal; }
        }
        return ResourceDictionary.GlobalApplicationResources().Get(key);
    }

    public object TryFindResource(string key) {
        return FindResource(key);
    }

    public DependencyObject FindResourceBox(string key) {
        if (_resources != null) {
            DependencyObject localBox = _resources.GetBox(key);
            if (localBox != null) { return localBox; }
        }
        return ResourceDictionary.GlobalApplicationResources().GetBox(key);
    }

    public void Run(Window window = null) {
        Startup(this, new StartupEventArgs(new string[0]));
        if (window != null) { MainWindow = window; }
        if (MainWindow != null) { MainWindow.Show(); }
    }

    public void Shutdown(int exitCode = 0) {
        Exit(this, EventArgs.Empty());
        Host_CloseWindow();
    }
}
