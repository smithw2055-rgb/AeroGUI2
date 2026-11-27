module Aero.UI;

import Aero.Native;
import RealScript.Math;

public class Binding : DependencyObject {
    public string Path { get; set; } = "";
    public string ElementName { get; set; } = "";
    private DependencyObject _sourceBox;
    internal int _cachedSrcPropId = -2;
    internal string[] _cachedSegs;
    internal bool _cachedSingleHop = true;
    internal bool _prepReady = false;
    internal string _prepPath = "";
    public BindingMode Mode { get; set; } = BindingMode.OneWay;
    public IValueConverter Converter { get; set; }
    public string ConverterKey { get; set; } = "";
    public string ConverterParameter { get; set; } = "";
    public string StringFormat { get; set; } = "";
    public string RelativeSourceMode { get; set; } = "";
    public string RelativeSourceAncestorType { get; set; } = "";
    public bool HasFallbackValue { get; set; } = false;
    public bool HasTargetNullValue { get; set; } = false;
    private DependencyObject _fallbackBox;
    private DependencyObject _targetNullBox;

    public Binding() {}
    public Binding(string path) { Path = path == null ? "" : path; }

    public object Source {
        get { return DependencyObject.Unbox(_sourceBox); }
        set { _sourceBox = DependencyObject.BoxObject(value); }
    }
    public DependencyObject SourceBox {
        get { return _sourceBox; }
        set { _sourceBox = value; }
    }
    public object FallbackValue {
        get { return DependencyObject.Unbox(_fallbackBox); }
        set { _fallbackBox = DependencyObject.BoxObject(value); HasFallbackValue = true; }
    }
    public DependencyObject FallbackBox {
        get { return _fallbackBox; }
        set { _fallbackBox = value; HasFallbackValue = true; }
    }
    public object TargetNullValue {
        get { return DependencyObject.Unbox(_targetNullBox); }
        set { _targetNullBox = DependencyObject.BoxObject(value); HasTargetNullValue = true; }
    }
    public DependencyObject TargetNullBox {
        get { return _targetNullBox; }
        set { _targetNullBox = value; HasTargetNullValue = true; }
    }
}

public class MultiBinding : DependencyObject {
    public Binding[] Bindings { get; set; }
    public IMultiValueConverter Converter { get; set; }
    public string ConverterParameter { get; set; } = "";
    public string StringFormat { get; set; } = "";

    public static DependencyObject EvaluateBox(UIElement target, DependencyObject mbBox) {
        MultiBinding mb = mbBox as MultiBinding;
        if (target == null || mb == null) { return null; }
        Binding[] subs = mb.Bindings;
        int n = subs != null ? subs.Length : 0;
        var vals = new DependencyObject[n];
        for (int i = 0; i < n; i += 1) {
            vals[i] = BindingEntry.EvaluateStandaloneBinding(target, subs[i]);
        }
        if (mb.Converter != null) {
            DependencyObject pBox = null;
            if (mb.ConverterParameter.Length > 0) {
                pBox = DependencyObject.BoxString(mb.ConverterParameter);
            }
            return DependencyObject.BoxObject(mb.Converter.Convert(vals, "", pBox, ""));
        }
        return n > 0 ? vals[0] : null;
    }

    public MultiBinding() { Bindings = new Binding[0]; }
    public void AddBinding(Binding b) { if (b != null) { Bindings.Push(b); } }
}

internal class BindingEntry : DependencyObject {
    public UIElement Target;
    public Inline TargetInline;
    public Effect TargetEffect;
    public UIElement ContextHost;
    internal bool _isApplying = false;
    internal bool _isTriggerBinding = false;
    public string TargetProp = "";
    public int Mode = 0; // 0=OneWay, 1=TwoWay, 2=OneTime
    public int Kind = 0; // 0=Lambda, 1=Path, 2=Element, 3=TemplateParent, 4=Multi, 5=Compiled

    public Func<DependencyObject> LambdaGetter;
    public Action<DependencyObject> LambdaSetter;
    public Action<UIElement, object> CompiledUpdater;

    public DependencyObject ExplicitSourceBox;
    public UIElement SourceElement;
    public string SourcePath = "";
    public string[] PathSegments;
    public bool IsSingleHopPath = false;
    public string[] MultiPaths;
    public Binding[] MultiBindings;
    public string ElementName = "";
    public string RelativeSourceMode = "";
    public string RelativeSourceAncestorType = "";
    public IValueConverter Converter;
    public IMultiValueConverter MultiConverter;
    public string ConverterKey = "";
    public string ConverterParam = "";
    public string StringFormat = "";
    public bool HasFallback = false;
    public DependencyObject FallbackBox;
    public bool HasTargetNull = false;
    public DependencyObject TargetNullBox;

    public string LastHash = "";
    public int LastDirtyMask = -1;
    public bool OneTimeDone = false;
    private UIElement _lastFindNameRoot;
    private UIElement _cachedAncestorHost;
    private UIElement _cachedAncestor;
    private int _cachedAncestorEpoch = 0;
    private int _cachedHostChainEpoch = 0;
    private bool _elemPropResolvedOnSelf = false;
    private int _lastMultiEpochHash = -1;
    private DependencyObject _lastTemplateValBox;
    private DependencyObject _lastPathValBox;
    private DependencyObject _lastSrcBox;
    private UIElement[] _multiElementSources;
    private UIElement[] _multiFindRoots;
    private int _fastPropCatCache = -1;
    private int _fastPropKindCache = 0;
    private DependencyObject _lastMidBox;
    private int _lastMidDirtyMask = -1;
    private bool _fastKind1VmClean = false;
    private bool _fastElemClean = false;
    private int _fastNativeVmEp = -1;
    private int _fastManagedVmEp = -1;
    private int _fastDcEp = -1;
    private int _fastTreeEp = -1;
    private int _fastVisEp = -1;
    private int _fastUiPropEp = -1;
    private int _fastLayoutEp = -1;
    private int _fastDispEp = -1;
    internal int TargetPropId = 0;
    internal int SourcePropId = 0;
    internal bool _inPollOnly = false;
    internal int _dirtyEnqueuedGen = 0;
    internal int _lastEvalWaveGen = 0;
    private UIElement _subscribedUiSrc;
    private int _subscribedVmToken = 0;
    private int _subscribedMidVmToken = 0;

    internal void BindUiSourceSub(UIElement el) {
        if (_subscribedUiSrc == el) { return; }
        if (_subscribedUiSrc != null) { _subscribedUiSrc.RemovePropSubscriber(this); }
        _subscribedUiSrc = el;
        if (el != null && !el._isDisposedTree) { el.AddPropSubscriber(this); }
    }

    internal void BindVmSourceSub(int tok0, int tok1) {
        if (_subscribedVmToken != tok0) {
            if (_subscribedVmToken > 0) { BindingEngine.UnsubscribeVmToken(_subscribedVmToken, this); }
            _subscribedVmToken = tok0;
            if (tok0 > 0) { BindingEngine.SubscribeVmToken(tok0, this); }
        }
        if (_subscribedMidVmToken != tok1) {
            if (_subscribedMidVmToken > 0) { BindingEngine.UnsubscribeVmToken(_subscribedMidVmToken, this); }
            _subscribedMidVmToken = tok1;
            if (tok1 > 0) { BindingEngine.SubscribeVmToken(tok1, this); }
        }
    }

    internal void ClearSourceSubscriptions() {
        BindUiSourceSub(null);
        BindVmSourceSub(0, 0);
    }

    internal bool IsDead() {
        if (Target != null && Target._isDisposedTree) { return true; }
        if (ContextHost != null && ContextHost._isDisposedTree) { return true; }
        if (Target == null && TargetEffect == null && TargetInline != null &&
            (TargetInline.OwnerTextBlock == null || TargetInline.OwnerTextBlock._isDisposedTree)) {
            return true;
        }
        return false;
    }

    internal bool IsEpochFastClean() {
        if (Target == null && TargetInline == null && TargetEffect == null) { return true; }
        if (Mode == 2 && OneTimeDone) { return true; }
        if (!OneTimeDone) { return false; }
        if (Target != null && Target.HasActiveTriggerProp(TargetProp)) {
            return false;
        }
        if (_fastKind1VmClean && (Kind == 1 || Kind == 5)) {
            return _subscribedVmToken > 0 &&
                   (IsSingleHopPath || _subscribedMidVmToken > 0) &&
                   BindingEngine._curNativeVmEpoch == _fastNativeVmEp &&
                   BindingEngine._curManagedVmEpoch == _fastManagedVmEp &&
                   UIElement._dataContextEpoch == _fastDcEp &&
                   UIElement._treeParentEpoch == _fastTreeEp &&
                   UIElement._visibilityEpoch == _fastVisEp &&
                   UIElement._disposeEpoch == _fastDispEp;
        }
        if (_fastElemClean) {
            return _subscribedUiSrc != null &&
                   !_subscribedUiSrc._isDisposedTree &&
                   UIElement._globalPropEpoch == _fastUiPropEp &&
                   BindingEngine._curLayoutSizeEpoch == _fastLayoutEp &&
                   UIElement._treeParentEpoch == _fastTreeEp &&
                   UIElement._dataContextEpoch == _fastDcEp &&
                   UIElement._visibilityEpoch == _fastVisEp &&
                   UIElement._disposeEpoch == _fastDispEp;
        }
        return false;
    }

    internal static bool IsLayoutDependentElementProp(string prop) {
        return prop == "ActualWidth" || prop == "ActualHeight" ||
               prop == "ViewportWidth" || prop == "ViewportHeight" ||
               prop == "ExtentWidth" || prop == "ExtentHeight" ||
               prop == "ScrollableWidth" || prop == "ScrollableHeight" ||
               prop == "ComputedVerticalScrollBarVisibility" ||
               prop == "ComputedHorizontalScrollBarVisibility";
    }

    internal static bool CanUseFastElementPropEpoch(UIElement el, string prop) {
        if (el == null || prop == null || prop.Length == 0) {
            return false;
        }
        int cat = el.FastPropEpochCategory();
        if (cat == 0) {
            return false;
        }
        if (prop == "Background" || prop == "BorderBrush" || prop == "BorderThickness" ||
            prop == "CornerRadius" || prop == "Padding" || prop == "Margin" ||
            prop == "Foreground" || prop == "FontFamily" || prop == "FontSize" ||
            prop == "FontWeight" || prop == "FontStyle" ||
            prop == "Width" || prop == "Height" ||
            prop == "MinWidth" || prop == "MinHeight" ||
            prop == "MaxWidth" || prop == "MaxHeight" ||
            prop == "ActualWidth" || prop == "ActualHeight" ||
            prop == "HorizontalAlignment" || prop == "VerticalAlignment" ||
            prop == "HorizontalContentAlignment" || prop == "VerticalContentAlignment" ||
            prop == "Visibility" || prop == "Opacity" ||
            prop == "IsEnabled" || prop == "IsHitTestVisible" ||
            prop == "Focusable" || prop == "IsTabStop" || prop == "TabIndex" ||
            prop == "IsFocused" || prop == "IsKeyboardFocused" || prop == "IsKeyboardFocusWithin" ||
            prop == "IsMouseOver" || prop == "IsPressed" ||
            prop == "Content" || prop == "Header" || prop == "Tag" ||
            prop == "AlternationIndex" || prop == "ItemsControl.AlternationIndex" ||
            prop == "ZIndex" || prop == "ClipToBounds" ||
            prop == "RenderTransform" || prop == "LayoutTransform" || prop == "RenderTransformOrigin" ||
            prop == "OpacityMask" || prop == "Effect" ||
            prop == "CanContentScroll" || prop == "VerticalOffset" || prop == "HorizontalOffset" ||
            prop == "HorizontalScrollBarVisibility" || prop == "VerticalScrollBarVisibility" ||
            prop == "ViewportWidth" || prop == "ViewportHeight" ||
            prop == "ExtentWidth" || prop == "ExtentHeight" ||
            prop == "ScrollableWidth" || prop == "ScrollableHeight" ||
            prop == "ComputedVerticalScrollBarVisibility" || prop == "ComputedHorizontalScrollBarVisibility") {
            return true;
        }
        if ((prop == "Text" || prop == "Placeholder" || prop == "PlaceholderText") &&
            (cat == 2 || cat == 6)) {
            return true;
        }
        if (prop == "IsReadOnly" && cat == 2) {
            return true;
        }
        if (prop == "IsChecked" && (cat == 3 || cat == 10)) {
            return true;
        }
        if (prop == "IsIndeterminate" && cat == 3) {
            return true;
        }
        if ((prop == "Value" || prop == "Minimum" || prop == "Maximum" || prop == "Orientation") &&
            cat == 4) {
            return true;
        }
        if ((prop == "SelectedIndex" || prop == "SelectedItem") && (cat == 5 || cat == 6 || cat == 11)) {
            return true;
        }
        if ((prop == "IsDropDownOpen" || prop == "IsEditable") && cat == 6) { return true; }
        if (prop == "IsExpanded" && (cat == 7 || cat == 9)) { return true; }
        if (prop == "IsSelected" && (cat == 8 || cat == 9)) { return true; }
        if (prop == "IsSubmenuOpen" && cat == 10) { return true; }
        if (prop == "Children" && (el is Panel)) { return true; }
        if (UIElement.IsAttachedElementPropPath(prop)) { return true; }
        return false;
    }

    internal static bool IsGuaranteedUIElementProperty(UIElement el, string prop) {
        if (el == null || prop == null || prop.Length == 0) { return false; }
        if (prop == "Content") { return (el is ContentControl) || (el is ContentPresenter); }
        if (prop == "Header") { return (el is HeaderedContentControl) || el.IsItemsControlBoundary(); }
        return CanUseFastElementPropEpoch(el, prop);
    }

    internal static int GetFastElementEpoch(UIElement el, string prop) {
        if (el == null) { return -1; }
        if (prop == "Children" && (el is Panel)) {
            int cep = (el._propEpoch % 100000) * 4099 + (el._childrenMutEpoch % 4099) + 1;
            return cep <= 0 ? 1 : cep;
        }
        if (IsLayoutDependentElementProp(prop)) {
            int ep = (el._propEpoch % 100000) * 4099 + (BindingEngine._curLayoutSizeEpoch % 4099) + 1;
            return ep <= 0 ? 1 : ep;
        }
        return el._propEpoch;
    }

    private int TryGetCachedFastElementEpoch(UIElement el) {
        if (el == null) { return -1; }
        int cat = el.FastPropEpochCategory();
        if (cat == 0) { return -1; }
        if (_fastPropCatCache != cat) {
            _fastPropCatCache = cat;
            if (SourcePath == "Children" && (el is Panel)) {
                _fastPropKindCache = 3;
            } else if (CanUseFastElementPropEpoch(el, SourcePath)) {
                _fastPropKindCache = IsLayoutDependentElementProp(SourcePath) ? 2 : 1;
            } else {
                _fastPropKindCache = -1;
            }
        }
        if (_fastPropKindCache <= 0) { return -1; }
        if (_fastPropKindCache == 3) {
            int cep = (el._propEpoch % 100000) * 4099 + (el._childrenMutEpoch % 4099) + 1;
            return cep <= 0 ? 1 : cep;
        }
        if (_fastPropKindCache == 2) {
            int ep = (el._propEpoch % 100000) * 4099 + (BindingEngine._curLayoutSizeEpoch % 4099) + 1;
            return ep <= 0 ? 1 : ep;
        }
        return el._propEpoch;
    }

    internal void InitPathSegments() {
        if (SourcePath != null && SourcePath.Length > 0 && SourcePath != ".") {
            if (!SourcePath.Contains(".")) {
                PathSegments = null;
                IsSingleHopPath = true;
                return;
            }
            if (UIElement.IsAttachedElementPropPath(SourcePath)) {
                PathSegments = new string[1];
                PathSegments[0] = SourcePath;
                IsSingleHopPath = true;
                return;
            }
            PathSegments = BindingEngine.SplitBindingPath(SourcePath);
            IsSingleHopPath = (PathSegments.Length == 1);
        } else {
            PathSegments = null;
            IsSingleHopPath = true;
        }
    }

    internal static UIElement FindElementScoped(UIElement host, string elementName) {
        if (host == null || elementName == null || elementName.Length == 0) { return null; }
        if (host._internalRootName == elementName || host.Name == elementName) { return host; }
        if (host.TemplatedParent != null) {
            UIElement t = host;
            while (t.Parent != null && t.Parent.TemplatedParent == host.TemplatedParent) {
                t = t.Parent;
            }
            if (t._internalRootName == elementName) { return t; }
            UIElement localHit = t.FindTemplateChildName(elementName, host.TemplatedParent);
            if (localHit != null) { return localHit; }
            if (host.TemplatedParent._internalRootName == elementName || host.TemplatedParent.Name == elementName) { return host.TemplatedParent; }
        }
        UIElement cur = host;
        while (cur.Parent != null) {
            cur = cur.Parent;
            if (cur._internalRootName == elementName || cur.Name == elementName) { return cur; }
            if (cur.IsNamescopeBoundary()) {
                UIElement scopeHit = cur.FindName(elementName);
                if (scopeHit != null) { return scopeHit; }
                break;
            }
        }
        return null;
    }

    private static bool IsToggleButtonType(string tn) {
        return tn == "ToggleButton" || tn == "CheckBox" || tn == "RadioButton";
    }
    private static bool IsButtonBaseType(string tn) {
        return tn == "ButtonBase" || tn == "Button" || tn == "RepeatButton" || IsToggleButtonType(tn);
    }
    private static bool IsListBoxType(string tn) {
        return tn == "ListBox" || tn == "ListView";
    }
    private static bool IsSelectorType(string tn) {
        return tn == "Selector" || tn == "ComboBox" || tn == "TabControl" || IsListBoxType(tn);
    }
    private static bool IsItemsControlType(string tn) {
        return tn == "ItemsControl" || tn == "TreeView" || tn == "TreeViewItem" ||
               tn == "Menu" || tn == "MenuItem" || tn == "ContextMenu" || tn == "MenuBase" ||
               tn == "StatusBar" || tn == "ToolBar" || IsSelectorType(tn);
    }
    private static bool IsHeaderedContentControlType(string tn) {
        return tn == "HeaderedContentControl" || tn == "GroupBox" || tn == "Expander";
    }
    private static bool IsContentControlType(string tn) {
        return tn == "ContentControl" || tn == "Label" || tn == "UserControl" ||
               tn == "Page" || tn == "Window" || tn == "ListBoxItem" || tn == "ComboBoxItem" ||
               tn == "ListViewItem" || tn == "TabItem" || tn == "SelectableItem" ||
               tn == "ScrollViewer" || tn == "StatusBarItem" ||
               IsHeaderedContentControlType(tn) || IsButtonBaseType(tn);
    }
    private static bool IsRangeBaseType(string tn) {
        return tn == "RangeBase" || tn == "Slider" || tn == "ProgressBar" || tn == "ScrollBar";
    }
    private static bool IsTextBoxBaseType(string tn) {
        return tn == "TextBoxBase" || tn == "TextBox";
    }

    internal static UIElement StepAncestor(UIElement cur) {
        if (cur == null) { return null; }
        if (cur._logicalParent != null) { return cur._logicalParent; }
        return cur.Parent != null ? cur.Parent : cur.TemplatedParent;
    }

    internal static bool MatchesAncestorType(UIElement anc, string typeName) {
        if (anc == null || typeName == null || typeName.Length == 0) { return false; }
        string tn = anc.GetTypeName();
        if (tn == typeName) { return true; }
        if (typeName == "UIElement" || typeName == "FrameworkElement") { return true; }
        if (typeName == "Control") {
            return tn == "Control" || tn == "PasswordBox" || IsContentControlType(tn) ||
                   IsItemsControlType(tn) || IsRangeBaseType(tn) || IsTextBoxBaseType(tn);
        }
        if (typeName == "ContentControl") { return IsContentControlType(tn); }
        if (typeName == "HeaderedContentControl") { return IsHeaderedContentControlType(tn); }
        if (typeName == "ItemsControl") { return IsItemsControlType(tn); }
        if (typeName == "Selector") { return IsSelectorType(tn); }
        if (typeName == "ListBox") { return IsListBoxType(tn); }
        if (typeName == "ButtonBase") { return IsButtonBaseType(tn); }
        if (typeName == "ToggleButton") { return IsToggleButtonType(tn); }
        if (typeName == "RangeBase") { return IsRangeBaseType(tn); }
        if (typeName == "Panel") {
            return tn == "Panel" || tn == "StackPanel" || tn == "VirtualizingStackPanel" ||
                   tn == "TabPanel" || tn == "ToolBarPanel" || tn == "DockPanel" ||
                   tn == "WrapPanel" || tn == "ToolBarOverflowPanel" || tn == "UniformGrid" ||
                   tn == "VirtualizingUniformGrid" || tn == "VirtualizingWrapGrid" ||
                   tn == "Canvas" || tn == "Grid";
        }
        if (typeName == "Shape") {
            return tn == "Shape" || tn == "Rectangle" || tn == "Ellipse" ||
                   tn == "Line" || tn == "Path" || tn == "Polygon" || tn == "Polyline";
        }
        if (typeName == "TextBoxBase") { return IsTextBoxBaseType(tn); }
        if (typeName == "Decorator") { return tn == "Decorator" || tn == "Border" || tn == "Viewbox"; }
        return false;
    }

    private static DependencyObject ReadElementOrDcPathBox(UIElement srcEl, string path) {
        if (srcEl == null) { return null; }
        if (path.Length == 0 || path == ".") { return srcEl; }
        DependencyObject sv = srcEl.GetPropertyValue(path);
        if (sv != null) { return sv; }
        if (srcEl.IsExplicitNullProperty(path)) { return null; }
        sv = BindingEngine.ReadPathBox(srcEl, path);
        if (sv == null && path.Length > 12 && path.Substring(0, 12) == "DataContext." && srcEl.DataContext != null) {
            sv = BindingEngine.ReadPathBox(srcEl.DataContext, path.Substring(12));
        }
        if (sv == null && srcEl.DataContext != null) { sv = BindingEngine.ReadPathBox(srcEl.DataContext, path); }
        return sv;
    }

    public static DependencyObject EvaluateStandaloneBinding(UIElement associatedObject, Binding b) {
        if (b == null || associatedObject == null) { return null; }
        DependencyObject sv = null;
        string path = b.Path == null ? "" : b.Path;
        if (b.ElementName.Length > 0) {
            UIElement se = FindElementScoped(associatedObject, b.ElementName);
            if (se == null) { se = associatedObject.FindName(b.ElementName); }
            if (se == null && associatedObject.RootElement() != null) {
                se = associatedObject.RootElement().FindName(b.ElementName);
            }
            sv = ReadElementOrDcPathBox(se, path);
        } else if (b.RelativeSourceMode == "Self") {
            sv = ReadElementOrDcPathBox(associatedObject, path);
        } else if (b.RelativeSourceMode == "TemplatedParent") {
            sv = ReadElementOrDcPathBox(associatedObject.TemplatedParent, path);
        } else if (b.RelativeSourceMode == "FindAncestor" || b.RelativeSourceMode == "AncestorType") {
            UIElement anc = associatedObject;
            while (anc != null && b.RelativeSourceAncestorType.Length > 0 && !MatchesAncestorType(anc, b.RelativeSourceAncestorType)) {
                anc = StepAncestor(anc);
            }
            sv = ReadElementOrDcPathBox(anc, path);
        } else if (b.SourceBox != null && (path.Length == 0 || path == ".")) {
            sv = b.SourceBox;
        } else {
            object dc = b.Source != null ? b.Source : associatedObject.DataContext;
            if (dc != null && path.Length > 0) {
                sv = BindingEngine.ReadPathBox(dc, path);
            }
        }
        if (sv == null && b.HasFallbackValue) { sv = b.FallbackBox; }
        if (b.Converter != null) {
            DependencyObject paramBox = b.ConverterParameter.Length > 0 ? DependencyObject.BoxString(b.ConverterParameter) : null;
            sv = DependencyObject.BoxObject(b.Converter.Convert(sv, "", paramBox, ""));
        }
        return sv;
    }

    public DependencyObject ResolveSourceBox() {
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (RelativeSourceMode == "Self") {
            return Target != null ? (DependencyObject)Target : (TargetEffect != null ? (DependencyObject)TargetEffect : (DependencyObject)TargetInline);
        }
        if (RelativeSourceMode == "TemplatedParent" && host != null) {
            return host.TemplatedParent;
        }
        if ((RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") && host != null) {
            if (_cachedAncestorHost == host &&
                _cachedAncestorEpoch == UIElement._treeParentEpoch &&
                _cachedHostChainEpoch == host._ancestorChainEpoch &&
                (_cachedAncestor == null || !_cachedAncestor._isDisposedTree)) {
                return _cachedAncestor;
            }
            UIElement anc = host;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = StepAncestor(anc);
            }
            _cachedAncestorHost = host;
            _cachedAncestor = anc;
            _cachedAncestorEpoch = UIElement._treeParentEpoch;
            _cachedHostChainEpoch = host._ancestorChainEpoch;
            return anc;
        }
        if (ExplicitSourceBox != null) { return ExplicitSourceBox; }
        if (Target != null && TargetProp == "DataContext" && RelativeSourceMode.Length == 0) {
            return Target.InheritedDataContextBox();
        }
        if (host != null) { return host.EffectiveDataContextBox(); }
        return null;
    }

    public IValueConverter ResolveConverter() {
        if (Converter != null) { return Converter; }
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (ConverterKey.Length > 0 && host != null) {
            DependencyObject box = host.FindResourceBox(ConverterKey);
            if (box is IValueConverter) {
                Converter = box as IValueConverter;
                return Converter;
            }
        }
        return null;
    }

    private static string FormatFixedDecimal(float v, int digits) {
        if (digits <= 0) {
            int iv = (int)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
            return "" + iv;
        }
        float scale = digits == 1 ? 10.0f : 100.0f;
        int mod = digits == 1 ? 10 : 100;
        bool neg = v < 0.0f;
        int scaled = (int)(Math.Absf(v) * scale + 0.5f);
        int whole = scaled / mod;
        int frac = scaled % mod;
        string fracStr = "" + frac;
        if (digits == 2 && frac < 10) { fracStr = "0" + fracStr; }
        return (neg && (whole > 0 || frac > 0) ? "-" : "") + whole + "." + fracStr;
    }

    private static string GroupThousands(string digits) {
        if (digits == null || digits.Length <= 3) { return digits == null ? "" : digits; }
        string res = "";
        int count = 0;
        for (int i = digits.Length - 1; i >= 0; i -= 1) {
            if (count > 0 && (count % 3) == 0) {
                res = "," + res;
            }
            res = digits.Substring(i, 1) + res;
            count += 1;
        }
        return res;
    }

    private static string FormatCustomNumericSpec(DependencyObject box, string spec) {
        if (spec == null || spec.Length == 0) { return null; }
        bool hasDigitToken = false;
        int dotIdx = -1;
        for (int i = 0; i < spec.Length; i += 1) {
            string ch = spec.Substring(i, 1);
            if (ch == "0" || ch == "#") {
                hasDigitToken = true;
            } else if (ch == ".") {
                if (dotIdx >= 0) { return null; }
                dotIdx = i;
            } else if (ch != ",") {
                return null;
            }
        }
        if (!hasDigitToken) { return null; }

        string intPart = dotIdx >= 0 ? spec.Substring(0, dotIdx) : spec;
        string fracPart = dotIdx >= 0 ? spec.Substring(dotIdx + 1) : "";

        int scaleCommas = 0;
        int p = intPart.Length - 1;
        while (p >= 0 && intPart.Substring(p, 1) == ",") {
            scaleCommas += 1;
            p -= 1;
        }
        bool hasGroupSep = false;
        int minInt = 0;
        for (int i = 0; i <= p; i += 1) {
            string ch = intPart.Substring(i, 1);
            if (ch == "0") { minInt += 1; }
            else if (ch == ",") { hasGroupSep = true; }
        }

        int minFrac = 0;
        int maxFrac = 0;
        for (int i = 0; i < fracPart.Length; i += 1) {
            string ch = fracPart.Substring(i, 1);
            if (ch == "0") { minFrac += 1; maxFrac += 1; }
            else if (ch == "#") { maxFrac += 1; }
        }
        if (maxFrac > 4) { maxFrac = 4; }
        if (minFrac > maxFrac) { minFrac = maxFrac; }

        bool neg = false;
        int wholeVal = 0;
        int fracVal = 0;
        int fracPow = 1;
        for (int i = 0; i < maxFrac; i += 1) { fracPow = fracPow * 10; }

        if (box is BoxedInt) {
            int iv = (box as BoxedInt).Value;
            neg = iv < 0;
            int absVal = neg ? -iv : iv;
            int remNum = 0;
            int remDen = 1;
            if (scaleCommas == 0) {
                wholeVal = absVal;
            } else {
                int reduced = absVal;
                for (int c = 0; c < scaleCommas - 1; c += 1) {
                    reduced = reduced / 1000;
                }
                wholeVal = reduced / 1000;
                remNum = reduced % 1000;
                remDen = 1000;
            }
            if (maxFrac > 0) {
                fracVal = (remNum * fracPow + remDen / 2) / remDen;
                if (fracVal >= fracPow) {
                    wholeVal = wholeVal + 1;
                    fracVal = fracVal - fracPow;
                }
            } else if (remNum * 2 >= remDen) {
                wholeVal = wholeVal + 1;
            }
        } else {
            float fv = DependencyObject.UnboxFloat(box, 0.0f);
            neg = fv < 0.0f;
            float mag = Math.Absf(fv);
            for (int c = 0; c < scaleCommas; c += 1) {
                mag = mag / 1000.0f;
            }
            int scaled = (int)(mag * (float)fracPow + 0.5f);
            wholeVal = scaled / fracPow;
            fracVal = scaled % fracPow;
        }

        string wholeStr = "";
        if (wholeVal == 0) {
            if (minInt > 0) {
                for (int i = 0; i < minInt; i += 1) { wholeStr = wholeStr + "0"; }
            }
        } else {
            wholeStr = "" + wholeVal;
            while (wholeStr.Length < minInt) { wholeStr = "0" + wholeStr; }
            if (hasGroupSep) { wholeStr = GroupThousands(wholeStr); }
        }

        string fracStr = "";
        if (maxFrac > 0) {
            string padded = "" + fracVal;
            while (padded.Length < maxFrac) { padded = "0" + padded; }
            int keep = padded.Length;
            while (keep > minFrac && padded.Substring(keep - 1, 1) == "0") {
                keep -= 1;
            }
            fracStr = keep > 0 ? padded.Substring(0, keep) : "";
        }

        if (wholeStr.Length == 0 && fracStr.Length == 0) {
            return "0";
        }
        string signPrefix = (neg && (wholeVal > 0 || fracVal > 0)) ? "-" : "";
        return signPrefix + wholeStr + (fracStr.Length > 0 ? ("." + fracStr) : "");
    }

    private static string FormatSpecOnly(DependencyObject box, string spec) {
        string raw = DependencyObject.UnboxString(box, "");
        if (spec == null || spec.Length == 0) { return raw; }
        if (box is BoxedFloat || box is BoxedInt) {
            float fv = DependencyObject.UnboxFloat(box, 0.0f);
            if (spec == "G" || spec == "g") {
                if (box is BoxedInt) { return "" + (box as BoxedInt).Value; }
                int iv = (int)(fv >= 0.0f ? (fv + 0.0001f) : (fv - 0.0001f));
                if (Math.Absf(fv - (float)iv) < 0.0001f) { return "" + iv; }
                return raw;
            }
            if (spec == "00" || spec == "D2") {
                int iv = (box is BoxedInt) ? (box as BoxedInt).Value : (int)(fv >= 0.0f ? (fv + 0.0001f) : (fv - 0.0001f));
                if (iv >= 0 && iv < 10) { return "0" + iv; }
                return "" + iv;
            }
            if (spec == "F0" || spec == "f0" || spec == "N0" || spec == "n0" || spec == "0") {
                return FormatFixedDecimal(fv, 0);
            }
            if (spec == "F1" || spec == "f1" || spec == "0.0") {
                return FormatFixedDecimal(fv, 1);
            }
            if (spec == "F2" || spec == "f2" || spec == "0.00") {
                return FormatFixedDecimal(fv, 2);
            }
            if (spec == "C" || spec == "c" || spec == "C2" || spec == "c2") {
                return "$" + FormatFixedDecimal(fv, 2);
            }
            if (spec == "C0" || spec == "c0") {
                return "$" + FormatFixedDecimal(fv, 0);
            }
            if (spec == "C1" || spec == "c1") {
                return "$" + FormatFixedDecimal(fv, 1);
            }
            if (spec == "P0" || spec == "p0") {
                return FormatFixedDecimal(fv * 100.0f, 0) + "%";
            }
            if (spec == "P1" || spec == "p1") {
                return FormatFixedDecimal(fv * 100.0f, 1) + "%";
            }
            if (spec == "P" || spec == "p" || spec == "P2" || spec == "p2") {
                return FormatFixedDecimal(fv * 100.0f, 2) + "%";
            }
            if (spec == "P3" || spec == "p3") {
                return FormatFixedDecimal(fv * 100.0f, 3) + "%";
            }
            if (spec.Length >= 2 && spec.Substring(spec.Length - 1, 1) == "%") {
                string inner = spec.Substring(0, spec.Length - 1);
                string pctNum = FormatCustomNumericSpec(DependencyObject.BoxFloat(fv * 100.0f), inner);
                if (pctNum != null) { return pctNum + "%"; }
            }
            string customNum = FormatCustomNumericSpec(box, spec);
            if (customNum != null) { return customNum; }
        }
        return raw;
    }

    private static int FindSubstr(string s, string needle, int start) {
        if (s == null || needle == null || needle.Length == 0) { return -1; }
        int limit = s.Length - needle.Length;
        for (int i = start < 0 ? 0 : start; i <= limit; i += 1) {
            if (s.Substring(i, needle.Length) == needle) { return i; }
        }
        return -1;
    }

    public string FormatValue(DependencyObject box) {
        string fmt = StringFormat == null ? "" : StringFormat;
        if (fmt.Length >= 2 && fmt.Substring(0, 2) == "{}") {
            fmt = fmt.Substring(2);
        }
        if (fmt.Length == 0) { return DependencyObject.UnboxString(box, ""); }
        if (fmt == "G" || fmt == "g" || fmt == "00" || fmt == "F0" || fmt == "f0" ||
            fmt == "F1" || fmt == "f1" || fmt == "F2" || fmt == "f2" ||
            fmt == "C" || fmt == "c" || fmt == "C0" || fmt == "c0" ||
            fmt == "C1" || fmt == "c1" || fmt == "C2" || fmt == "c2" ||
            fmt == "D2" || fmt == "N0" || fmt == "n0" || fmt == "0" || fmt == "0.0" || fmt == "0.00" ||
            fmt == "P" || fmt == "p" || fmt == "P0" || fmt == "p0" ||
            fmt == "P1" || fmt == "p1" || fmt == "P2" || fmt == "p2" || fmt == "P3" || fmt == "p3") {
            return FormatSpecOnly(box, fmt);
        }
        if (FindSubstr(fmt, "{", 0) < 0) {
            string direct = FormatSpecOnly(box, fmt);
            string rawCheck = DependencyObject.UnboxString(box, "");
            if (direct != rawCheck) { return direct; }
        }
        int colIdx = FindSubstr(fmt, "{0:", 0);
        if (colIdx >= 0) {
            int endBrace = FindSubstr(fmt, "}", colIdx);
            if (endBrace > colIdx + 3) {
                string spec = fmt.Substring(colIdx + 3, endBrace - (colIdx + 3));
                string formatted = FormatSpecOnly(box, spec);
                return fmt.Substring(0, colIdx) + formatted + fmt.Substring(endBrace + 1);
            }
        }
        string raw = DependencyObject.UnboxString(box, "");
        if (fmt.Contains("{0}")) {
            return fmt.Replace("{0}", raw);
        }
        return fmt + raw;
    }

    private void ApplyTargetBox(DependencyObject valBox) {
        BindingEngine.MarkFlushDirty();
        bool prev = _isApplying;
        _isApplying = true;
        if (Target != null) {
            if (TargetPropId > 0) {
                if (valBox is BoxedFloat && Target.TryWriteFloatById(TargetPropId, (valBox as BoxedFloat).Value)) {
                    // written via zero-dispatch float slot
                } else if (valBox is BoxedColor && Target.TryWriteColorById(TargetPropId, (valBox as BoxedColor).Value)) {
                    // written via zero-dispatch color slot
                } else {
                    Target.SetPropertyById(TargetPropId, TargetProp, valBox);
                }
            } else {
                Target.SetPropertyValue(TargetProp, valBox);
            }
        } else if (TargetInline != null) {
            TargetInline.SetPropertyValue(TargetProp, valBox);
        } else if (TargetEffect != null) {
            TargetEffect.SetPropertyValue(TargetProp, valBox);
        }
        _isApplying = prev;
    }

    private int TryComputeMultiEpochHash(UIElement host, int mbLen) {
        if (!OneTimeDone || _multiElementSources == null || _multiElementSources.Length < mbLen) {
            return -1;
        }
        int h = 17;
        for (int i = 0; i < mbLen; i += 1) {
            Binding sub = MultiBindings[i];
            string sp = sub.Path == null ? "" : sub.Path;
            int subEp = -1;
            if (sub.ElementName.Length > 0) {
                UIElement se = _multiElementSources[i];
                if (se == null || se._isDisposedTree) { return -1; }
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(se, sp)) {
                    subEp = GetFastElementEpoch(se, sp);
                } else if (sp.Length > 8 && sp.Substring(0, 8) == "Content." &&
                           !sp.Substring(8).Contains(".") && se is ContentControl) {
                    object cObj = (se as ContentControl).Content;
                    int cdm = (cObj != null && !(cObj is UIElement) && ParseUtil.IsNativeReady())
                        ? Reflect_GetDirtyMask(cObj)
                        : (cObj == null ? 0 : -1);
                    if (cdm >= 0) {
                        subEp = (se._propEpoch % 16384) * 16384 + (cdm % 16384) + 1;
                    }
                }
            } else if (sub.RelativeSourceMode == "Self" && Target != null) {
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(Target, sp)) {
                    subEp = GetFastElementEpoch(Target, sp);
                }
            } else if (sub.RelativeSourceMode == "TemplatedParent" && host != null && host.TemplatedParent != null) {
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(host.TemplatedParent, sp)) {
                    subEp = GetFastElementEpoch(host.TemplatedParent, sp);
                }
            } else if (sub.ElementName.Length == 0 && sub.RelativeSourceMode.Length == 0 && sub.SourceBox == null) {
                object dc = sub.Source != null ? sub.Source : DependencyObject.Unbox(ResolveSourceBox());
                if (dc != null && !(dc is UIElement) && ParseUtil.IsNativeReady()) {
                    if (!sp.Contains(".")) {
                        subEp = Reflect_GetDirtyMask(dc);
                    } else {
                        int dot = sp.IndexOf(".");
                        if (dot > 0 && sp.Substring(dot + 1).IndexOf(".") < 0) {
                            int dm0 = Reflect_GetDirtyMask(dc);
                            if (dm0 >= 0) {
                                object mid = DependencyObject.Unbox(BindingEngine.ReadPathBox(dc, sp.Substring(0, dot)));
                                int dm1 = (mid != null && !(mid is UIElement))
                                    ? Reflect_GetDirtyMask(mid)
                                    : (mid == null ? 0 : -1);
                                if (dm1 >= 0) {
                                    subEp = (dm0 % 16384) * 16384 + (dm1 % 16384) + 1;
                                }
                            }
                        }
                    }
                }
            }
            if (subEp < 0) { return -1; }
            h = ((h * 31) + (subEp % 1000000)) % 10000007;
            if (h <= 0) { h = 1; }
        }
        return h;
    }

    public void Evaluate() {
        if (Target == null && TargetInline == null && TargetEffect == null) { return; }
        if (Mode == 2 && OneTimeDone) { return; }
        if (BindingEngine._inFlushPass && !BindingEngine._inSubtreeFlush && !BindingEngine._inPushWave && OneTimeDone &&
            (Target == null || !Target.HasActiveTriggerProp(TargetProp))) {
            if (_fastKind1VmClean && (Kind == 1 || Kind == 5) &&
                BindingEngine._curNativeVmEpoch == _fastNativeVmEp &&
                BindingEngine._curManagedVmEpoch == _fastManagedVmEp &&
                UIElement._dataContextEpoch == _fastDcEp &&
                UIElement._treeParentEpoch == _fastTreeEp &&
                UIElement._visibilityEpoch == _fastVisEp &&
                UIElement._disposeEpoch == _fastDispEp) {
                return;
            }
            if (_fastElemClean &&
                UIElement._globalPropEpoch == _fastUiPropEp &&
                BindingEngine._curLayoutSizeEpoch == _fastLayoutEp &&
                UIElement._treeParentEpoch == _fastTreeEp &&
                UIElement._dataContextEpoch == _fastDcEp &&
                UIElement._visibilityEpoch == _fastVisEp &&
                UIElement._disposeEpoch == _fastDispEp) {
                return;
            }
        }
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (!BindingEngine._inSubtreeFlush && host != null && host.IsInsideClosedPopup() && TargetProp != "IsOpen") {
            return;
        }
        if (!BindingEngine._inSubtreeFlush && OneTimeDone && !_isTriggerBinding && host != null &&
            TargetProp != "Visibility" && TargetProp != "IsVisible" &&
            TargetProp != "IsSelected" && TargetProp != "IsChecked" && TargetProp != "IsOpen" &&
            host.IsInsideCollapsedSubtree()) {
            return;
        }
        if (!_isTriggerBinding && Target != null && Target.HasActiveTriggerProp(TargetProp)) {
            return;
        }

        if (Kind == 0 && LambdaGetter != null) {
            DependencyObject val = LambdaGetter();
            string h = DependencyObject.UnboxString(val, "");
            if (h != LastHash || !OneTimeDone) {
                LastHash = h;
                OneTimeDone = true;
                ApplyTargetBox(val);
            }
            return;
        }

        if (Kind == 2) {
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            if (SourceElement != null && SourceElement._isDisposedTree) {
                ClearSourceSubscriptions();
                SourceElement = null;
                _lastFindNameRoot = null;
                LastDirtyMask = -1;
                _elemPropResolvedOnSelf = false;
                _fastElemClean = false;
            }
            if (SourceElement == null && ElementName.Length > 0 && host != null) {
                SourceElement = BindingEntry.FindElementScoped(host, ElementName);
                if (SourceElement == null) {
                    UIElement curRoot = host.RootElement();
                    if (curRoot != null && curRoot != _lastFindNameRoot) {
                        _lastFindNameRoot = curRoot;
                        SourceElement = curRoot.FindName(ElementName);
                    }
                }
            }
            if (SourceElement != null) {
                int elemDm = IsSingleHopPath ? TryGetCachedFastElementEpoch(SourceElement) : -1;
                bool canFastElem = elemDm >= 0;
                if (OneTimeDone && _elemPropResolvedOnSelf && elemDm >= 0 && elemDm == LastDirtyMask) {
                    if (IsSingleHopPath) {
                        MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                        if (_subscribedUiSrc != SourceElement || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                            BindUiSourceSub(SourceElement);
                            BindVmSourceSub(0, 0);
                        }
                    } else {
                        _fastElemClean = false;
                        ClearSourceSubscriptions();
                    }
                    return;
                }
                DependencyObject selfVal = canFastElem
                    ? (SourcePropId > 0 ? SourceElement.GetPropertyById(SourcePropId, SourcePath) : SourceElement.GetPropertyValue(SourcePath))
                    : null;
                bool explicitNull = (selfVal == null) &&
                    (IsGuaranteedUIElementProperty(SourceElement, SourcePath) || SourceElement.IsExplicitNullProperty(SourcePath));
                _elemPropResolvedOnSelf = canFastElem && (selfVal != null || explicitNull || SourceElement.DataContext == null);
                DependencyObject ev = (selfVal != null || explicitNull)
                    ? selfVal
                    : ReadElementOrDcPathBox(SourceElement, SourcePath);
                if (ev == null && !explicitNull && HasFallback) { ev = FallbackBox; }
                bool isDirectUi = ev != null && (ev.UnboxObject() is UIElement);
                bool colorSame = OneTimeDone && (ev is BoxedColor) && (_lastTemplateValBox is BoxedColor) &&
                                 ((ev as BoxedColor).Value == (_lastTemplateValBox as BoxedColor).Value);
                bool boxDiff = (ev is Brush || ev is UIElement || _lastTemplateValBox is Brush || _lastTemplateValBox is UIElement)
                    && (ev != _lastTemplateValBox);
                LastDirtyMask = _elemPropResolvedOnSelf ? elemDm : -1;
                if (!colorSame) {
                    string sig = isDirectUi ? "<ui>" : (ev != null ? DependencyObject.UnboxString(ev, "") : "<null>");
                    if (!OneTimeDone || boxDiff || (!isDirectUi && sig != LastHash)) {
                        _lastTemplateValBox = ev;
                        LastHash = sig;
                        OneTimeDone = true;
                        ApplyConverted(ev);
                    }
                } else {
                    _lastTemplateValBox = ev;
                }
                if (OneTimeDone && _elemPropResolvedOnSelf && IsSingleHopPath) {
                    MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                    BindUiSourceSub(SourceElement);
                    BindVmSourceSub(0, 0);
                } else {
                    _fastElemClean = false;
                    ClearSourceSubscriptions();
                }
            } else {
                _fastElemClean = false;
                ClearSourceSubscriptions();
                if (HasFallback && host != null && !OneTimeDone) {
                    OneTimeDone = true;
                    ApplyRawBox(FallbackBox);
                }
            }
            return;
        }

        if (Kind == 3 && SourceElement != null) {
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            int tpEpoch = IsSingleHopPath ? TryGetCachedFastElementEpoch(SourceElement) : -1;
            bool canFastTp = tpEpoch >= 0;
            if (OneTimeDone && _elemPropResolvedOnSelf && canFastTp && tpEpoch == LastDirtyMask) {
                MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                if (IsSingleHopPath) {
                    if (_subscribedUiSrc != SourceElement || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                        BindUiSourceSub(SourceElement);
                        BindVmSourceSub(0, 0);
                    }
                } else {
                    ClearSourceSubscriptions();
                }
                return;
            }
            DependencyObject tv = SourcePropId > 0
                ? SourceElement.GetPropertyById(SourcePropId, SourcePath)
                : SourceElement.GetPropertyValue(SourcePath);
            bool explicitNull = (tv == null) && (canFastTp || SourceElement.IsExplicitNullProperty(SourcePath));
            _elemPropResolvedOnSelf = canFastTp;
            if (tv == null && !explicitNull) {
                tv = BindingEngine.ReadPathBox(SourceElement, SourcePath);
            }
            if (tv == null && !explicitNull && HasFallback) { tv = FallbackBox; }
            bool colorSame = OneTimeDone && (tv is BoxedColor) && (_lastTemplateValBox is BoxedColor) &&
                             ((tv as BoxedColor).Value == (_lastTemplateValBox as BoxedColor).Value);
            bool boxDiff = (tv is Brush || tv is UIElement || _lastTemplateValBox is Brush || _lastTemplateValBox is UIElement)
                && (tv != _lastTemplateValBox);
            LastDirtyMask = _elemPropResolvedOnSelf ? tpEpoch : -1;
            if (!colorSame) {
                string sig = tv != null ? DependencyObject.UnboxString(tv, "") : "<null>";
                if (boxDiff || sig != LastHash || !OneTimeDone) {
                    _lastTemplateValBox = tv;
                    LastHash = sig;
                    OneTimeDone = true;
                    ApplyConverted(tv);
                }
            } else {
                _lastTemplateValBox = tv;
            }
            if (OneTimeDone && _elemPropResolvedOnSelf) {
                MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                if (IsSingleHopPath) {
                    BindUiSourceSub(SourceElement);
                    BindVmSourceSub(0, 0);
                } else {
                    ClearSourceSubscriptions();
                }
            } else {
                _fastElemClean = false;
                ClearSourceSubscriptions();
            }
            return;
        }

        if (Kind == 4 && MultiBindings != null) {
            int mbLen = MultiBindings.Length;
            if (_multiElementSources == null || _multiElementSources.Length < mbLen) {
                _multiElementSources = new UIElement[mbLen];
                _multiFindRoots = new UIElement[mbLen];
            }
            int preMultiHash = TryComputeMultiEpochHash(host, mbLen);
            if (preMultiHash >= 0 && preMultiHash == _lastMultiEpochHash) {
                return;
            }
            var vals = new DependencyObject[mbLen];
            string sig = "";
            for (int i = 0; i < mbLen; i += 1) {
                Binding sub = MultiBindings[i];
                DependencyObject sv = null;
                bool subExplicitNull = false;
                if (sub.ElementName.Length > 0 && host != null) {
                    UIElement se = _multiElementSources[i];
                    if (se != null && se._isDisposedTree) {
                        se = null;
                        _multiElementSources[i] = null;
                        _multiFindRoots[i] = null;
                    }
                    if (se == null) {
                        se = BindingEntry.FindElementScoped(host, sub.ElementName);
                        if (se == null) {
                            UIElement curRoot = host.RootElement();
                            if (curRoot != null && curRoot != _multiFindRoots[i]) {
                                _multiFindRoots[i] = curRoot;
                                se = curRoot.FindName(sub.ElementName);
                            }
                        }
                        if (se != null) {
                            _multiElementSources[i] = se;
                        }
                    }
                    if (se != null) {
                        sv = ReadElementOrDcPathBox(se, sub.Path);
                        subExplicitNull = (sv == null) && se.IsExplicitNullProperty(sub.Path);
                    }
                } else if (sub.RelativeSourceMode == "Self" && host != null) {
                    if (Target != null) {
                        sv = Target.GetPropertyValue(sub.Path);
                        subExplicitNull = (sv == null) && Target.IsExplicitNullProperty(sub.Path);
                        if (sv == null && !subExplicitNull) { sv = BindingEngine.ReadPathBox(Target, sub.Path); }
                    } else {
                        sv = TargetInline.GetPropertyValue(sub.Path);
                        if (sv == null) { sv = BindingEngine.ReadPathBox(TargetInline, sub.Path); }
                    }
                } else if (sub.RelativeSourceMode == "TemplatedParent" && host != null && host.TemplatedParent != null) {
                    sv = host.TemplatedParent.GetPropertyValue(sub.Path);
                    subExplicitNull = (sv == null) && host.TemplatedParent.IsExplicitNullProperty(sub.Path);
                    if (sv == null && !subExplicitNull) { sv = BindingEngine.ReadPathBox(host.TemplatedParent, sub.Path); }
                } else if (sub.SourceBox != null && (sub.Path == null || sub.Path.Length == 0 || sub.Path == ".")) {
                    sv = sub.SourceBox;
                } else {
                    object dc = sub.Source != null ? sub.Source : DependencyObject.Unbox(ResolveSourceBox());
                    if (dc != null) {
                        sv = BindingEngine.ReadPathBox(dc, sub.Path);
                        if (sv == null && dc is UIElement) {
                            subExplicitNull = (dc as UIElement).IsExplicitNullProperty(sub.Path);
                        }
                    }
                }
                if (sv == null && !subExplicitNull && sub.HasFallbackValue) { sv = sub.FallbackBox; }
                vals[i] = sv;
                sig = sig + "|" + (sv != null ? DependencyObject.UnboxString(sv, "") : "<null>");
            }
            if (sig == LastHash && OneTimeDone) {
                _lastMultiEpochHash = TryComputeMultiEpochHash(host, mbLen);
                return;
            }
            LastHash = sig;
            OneTimeDone = true;
            if (MultiConverter != null) {
                object res = MultiConverter.Convert(vals, TargetProp, DependencyObject.BoxString(ConverterParam), "");
                ApplyRawBox(DependencyObject.BoxObject(res));
            } else if (StringFormat.Length > 0) {
                string txt = StringFormat;
                if (txt.Length >= 2 && txt.Substring(0, 2) == "{}") { txt = txt.Substring(2); }
                for (int i = 0; i < vals.Length; i += 1) {
                    txt = txt.Replace("{" + i + "}", DependencyObject.UnboxString(vals[i], ""));
                }
                ApplyTargetBox(DependencyObject.BoxString(txt));
            }
            _lastMultiEpochHash = TryComputeMultiEpochHash(host, mbLen);
            return;
        }

        if (Kind == 5 && CompiledUpdater != null) {
            if (Target == null) { return; }
            int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
            int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) {
                _fastKind1VmClean = false;
                ClearSourceSubscriptions();
                return;
            }
            int dm = ParseUtil.IsNativeReady() ? Reflect_GetDirtyMask(srcObj) : -1;
            bool sameSrc = DependencyObject.SameValueBox(srcBox, _lastSrcBox);
            bool canKind5VmFast = !(srcObj is UIElement) && dm >= 0;
            if (OneTimeDone && sameSrc && canKind5VmFast && dm == LastDirtyMask) {
                MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                if (_subscribedVmToken == 0 || _subscribedMidVmToken != 0 || _subscribedUiSrc != null) {
                    int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                    BindUiSourceSub(null);
                    BindVmSourceSub(tok0, 0);
                }
                return;
            }
            _lastSrcBox = srcBox;
            LastDirtyMask = dm;
            OneTimeDone = true;
            CompiledUpdater(Target, srcObj);
            BindingEngine.MarkFlushDirty();
            if (canKind5VmFast) {
                MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                BindUiSourceSub(null);
                BindVmSourceSub(tok0, 0);
            } else {
                _fastKind1VmClean = false;
                ClearSourceSubscriptions();
            }
            return;
        }

        if (Kind == 1) {
            if (host == null && RelativeSourceMode != "Self") { return; }
            int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
            int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) {
                _lastMidBox = null;
                _lastMidDirtyMask = -1;
                _fastKind1VmClean = false;
                _fastElemClean = false;
                ClearSourceSubscriptions();
                if (HasFallback) { ApplyRawBox(FallbackBox); }
                return;
            }
            bool srcIsUi = srcObj is UIElement;
            bool srcIsNonUiDep = !srcIsUi && (srcObj is DependencyObject);
            int dm = -1;
            if (srcIsUi) {
                dm = TryGetCachedFastElementEpoch(srcObj as UIElement);
            } else if (srcIsNonUiDep) {
                dm = (srcObj as DependencyObject).DepPropEpoch();
            } else if (ParseUtil.IsNativeReady()) {
                dm = Reflect_GetDirtyMask(srcObj);
            }
            bool sameSrc = DependencyObject.SameValueBox(srcBox, _lastSrcBox);
            if (OneTimeDone && sameSrc && dm >= 0 && dm == LastDirtyMask) {
                if (IsSingleHopPath) {
                    if (!srcIsUi && !srcIsNonUiDep) {
                        MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                        if (_subscribedVmToken == 0 || _subscribedMidVmToken != 0 || _subscribedUiSrc != null) {
                            UpdateKind1Subscriptions(true, false, srcObj);
                        }
                    } else if (srcIsUi && _elemPropResolvedOnSelf) {
                        MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                        if (_subscribedUiSrc != (srcObj as UIElement) || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                            UpdateKind1Subscriptions(false, true, srcObj);
                        }
                    }
                    return;
                }
                if (_lastMidDirtyMask >= 0 && _lastMidBox != null && PathSegments != null && PathSegments.Length == 2) {
                    object cachedMid = DependencyObject.Unbox(_lastMidBox);
                    int midDm = -1;
                    if (cachedMid is UIElement) {
                        UIElement midEl = cachedMid as UIElement;
                        if (CanUseFastElementPropEpoch(midEl, PathSegments[1])) {
                            midDm = GetFastElementEpoch(midEl, PathSegments[1]);
                        }
                    } else if (cachedMid != null && !(cachedMid is DependencyObject) && ParseUtil.IsNativeReady()) {
                        midDm = Reflect_GetDirtyMask(cachedMid);
                    }
                    if (midDm >= 0 && midDm == _lastMidDirtyMask) {
                        if (!srcIsUi && !srcIsNonUiDep && cachedMid != null && !(cachedMid is UIElement)) {
                            MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                            if (_subscribedVmToken == 0 || _subscribedMidVmToken == 0 || _subscribedUiSrc != null) {
                                int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                                int tok1 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(cachedMid) : 0;
                                BindUiSourceSub(null);
                                BindVmSourceSub(tok0, tok1);
                            }
                        }
                        return;
                    }
                }
            }
            DependencyObject valBox = null;
            bool explicitNull = false;
            if (srcIsUi && IsSingleHopPath) {
                UIElement srcEl = srcObj as UIElement;
                valBox = SourcePropId > 0 ? srcEl.GetPropertyById(SourcePropId, SourcePath) : srcEl.GetPropertyValue(SourcePath);
                explicitNull = (valBox == null) &&
                    (IsGuaranteedUIElementProperty(srcEl, SourcePath) || srcEl.IsExplicitNullProperty(SourcePath));
                _elemPropResolvedOnSelf = (dm >= 0) && (valBox != null || explicitNull || srcEl.DataContext == null);
            } else {
                _elemPropResolvedOnSelf = false;
            }
            if (valBox == null && !explicitNull) {
                valBox = BindingEngine.ReadSegmentsBox(srcObj, SourcePath, PathSegments);
            }
            if (valBox == null && !explicitNull && HasFallback) { valBox = FallbackBox; }
            bool boxDiff = (valBox != _lastPathValBox) && !DependencyObject.SameValueBox(valBox, _lastPathValBox);
            _lastMidBox = null;
            _lastMidDirtyMask = -1;
            bool midIsNonUiObservable = false;
            if (!IsSingleHopPath && PathSegments != null && PathSegments.Length == 2 && dm >= 0 &&
                !(srcObj is Transform) && !PathSegments[0].Contains("[") && !PathSegments[1].Contains("[")) {
                DependencyObject midBox = null;
                object midObj = null;
                if (srcIsUi) {
                    UIElement srcEl0 = srcObj as UIElement;
                    midBox = srcEl0.GetPropertyValue(PathSegments[0]);
                    if (midBox != null) {
                        midObj = DependencyObject.Unbox(midBox);
                    } else if (!srcEl0.IsExplicitNullProperty(PathSegments[0])) {
                        midObj = Reflect_GetPropObject(srcObj, PathSegments[0]);
                        if (midObj != null) { midBox = DependencyObject.BoxObject(midObj); }
                    }
                } else {
                    midObj = Reflect_GetPropObject(srcObj, PathSegments[0]);
                    if (midObj != null) { midBox = DependencyObject.BoxObject(midObj); }
                }
                if (midObj is UIElement) {
                    UIElement midEl = midObj as UIElement;
                    if (CanUseFastElementPropEpoch(midEl, PathSegments[1])) {
                        _lastMidBox = midBox;
                        _lastMidDirtyMask = GetFastElementEpoch(midEl, PathSegments[1]);
                    }
                } else if (midObj != null && !(midObj is DependencyObject) && ParseUtil.IsNativeReady()) {
                    _lastMidBox = midBox;
                    _lastMidDirtyMask = Reflect_GetDirtyMask(midObj);
                    midIsNonUiObservable = _lastMidDirtyMask >= 0;
                }
            }
            bool canKind1VmFast = !srcIsUi && dm >= 0 && (IsSingleHopPath || midIsNonUiObservable);
            bool canKind1ElemFast = srcIsUi && IsSingleHopPath && _elemPropResolvedOnSelf;
            if (!boxDiff && OneTimeDone && sameSrc &&
                (valBox == _lastPathValBox || valBox is BoxedInt || valBox is BoxedFloat || valBox is BoxedBool || valBox is BoxedString || valBox is BoxedColor)) {
                _lastSrcBox = srcBox;
                LastDirtyMask = dm;
                if (canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
                else { _fastKind1VmClean = false; }
                if (canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
                else { _fastElemClean = false; }
                UpdateKind1Subscriptions(canKind1VmFast, canKind1ElemFast, srcObj);
                return;
            }
            string sig = valBox != null ? DependencyObject.UnboxString(valBox, "") : "<null>";
            if (!boxDiff && sig == LastHash && OneTimeDone && sameSrc) {
                _lastSrcBox = srcBox;
                LastDirtyMask = dm;
                if (canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
                else { _fastKind1VmClean = false; }
                if (canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
                else { _fastElemClean = false; }
                UpdateKind1Subscriptions(canKind1VmFast, canKind1ElemFast, srcObj);
                return;
            }
            _lastSrcBox = srcBox;
            LastDirtyMask = dm;
            if (boxDiff || sig != LastHash || !OneTimeDone || !sameSrc) {
                _lastPathValBox = valBox;
                LastHash = sig;
                OneTimeDone = true;
                ApplyConverted(valBox);
            }
            if (OneTimeDone && canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
            else { _fastKind1VmClean = false; }
            if (OneTimeDone && canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
            else { _fastElemClean = false; }
            UpdateKind1Subscriptions(_fastKind1VmClean, _fastElemClean, srcObj);
            return;
        }

        DependencyObject mSrcBox = ResolveSourceBox();
        object mSrcObj = DependencyObject.Unbox(mSrcBox);
        if (mSrcObj == null) {
            if (HasFallback) { ApplyRawBox(FallbackBox); }
            return;
        }

        if (Kind == 4 && MultiPaths != null) {
            var vals = new DependencyObject[MultiPaths.Length];
            string sig = "";
            for (int i = 0; i < MultiPaths.Length; i += 1) {
                vals[i] = BindingEngine.ReadPathBox(mSrcObj, MultiPaths[i]);
                sig = sig + "|" + DependencyObject.UnboxString(vals[i], "");
            }
            if (sig == LastHash && OneTimeDone) { return; }
            LastHash = sig;
            OneTimeDone = true;
            if (MultiConverter != null) {
                object res = MultiConverter.Convert(vals, TargetProp, DependencyObject.BoxString(ConverterParam), "");
                ApplyRawBox(DependencyObject.BoxObject(res));
            } else if (StringFormat.Length > 0) {
                string txt = StringFormat;
                if (txt.Length >= 2 && txt.Substring(0, 2) == "{}") { txt = txt.Substring(2); }
                for (int i = 0; i < vals.Length; i += 1) {
                    txt = txt.Replace("{" + i + "}", DependencyObject.UnboxString(vals[i], ""));
                }
                ApplyTargetBox(DependencyObject.BoxString(txt));
            }
            return;
        }
    }

    private void MarkVmFastClean(int navEp, int manEp, int dcEp, int trEp, int visEp, int dispEp) {
        _fastKind1VmClean = true;
        _fastNativeVmEp = navEp;
        _fastManagedVmEp = manEp;
        _fastDcEp = dcEp;
        _fastTreeEp = trEp;
        _fastVisEp = visEp;
        _fastDispEp = dispEp;
    }

    private void MarkElemFastClean(int uiEp, int layEp, int trEp, int dcEp, int visEp, int dispEp) {
        _fastElemClean = true;
        _fastUiPropEp = uiEp;
        _fastLayoutEp = layEp;
        _fastTreeEp = trEp;
        _fastDcEp = dcEp;
        _fastVisEp = visEp;
        _fastDispEp = dispEp;
    }

    private void UpdateKind1Subscriptions(bool canVm, bool canElem, object srcObj) {
        if (canVm) {
            int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
            int tok1 = 0;
            if (!IsSingleHopPath && _lastMidBox != null) {
                object midObj2 = DependencyObject.Unbox(_lastMidBox);
                if (midObj2 != null && ParseUtil.IsNativeReady()) {
                    tok1 = Reflect_GetObjectToken(midObj2);
                }
            }
            BindUiSourceSub(null);
            BindVmSourceSub(tok0, tok1);
        } else if (canElem) {
            BindUiSourceSub(srcObj as UIElement);
            BindVmSourceSub(0, 0);
        } else {
            ClearSourceSubscriptions();
        }
    }

    private void ApplyConverted(DependencyObject rawBox) {
        IValueConverter conv = ResolveConverter();
        DependencyObject valBox = rawBox;
        if (conv != null) {
            DependencyObject paramBox = ConverterParam.Length > 0 ? DependencyObject.BoxString(ConverterParam) : null;
            object converted = conv.Convert(
                valBox,
                TargetProp,
                paramBox,
                "");
            valBox = DependencyObject.BoxObject(converted);
        }
        ApplyRawBox(valBox);
    }

    private void ApplyRawBox(DependencyObject valBox) {
        if (valBox == null && HasTargetNull) { valBox = TargetNullBox; }
        else if (HasTargetNull && valBox is BoxedString && (valBox as BoxedString).Value.Length == 0) {
            valBox = TargetNullBox;
        }
        if (StringFormat.Length > 0) {
            ApplyTargetBox(DependencyObject.BoxString(FormatValue(valBox)));
        } else {
            ApplyTargetBox(valBox);
        }
    }

    public void PushTwoWay(DependencyObject newTargetBox) {
        if (Mode != 1) { return; }
        if (Kind == 0 && LambdaSetter != null) {
            LambdaSetter(newTargetBox);
            return;
        }
        if (Kind == 2 && SourceElement != null) {
            SourceElement.SetPropertyValue(SourcePath, newTargetBox);
            return;
        }
        if (Kind == 3 && SourceElement != null) {
            SourceElement.SetPropertyValue(SourcePath, newTargetBox);
            return;
        }
        if (Kind == 1) {
            object srcObj = DependencyObject.Unbox(ResolveSourceBox());
            if (srcObj != null) {
                IValueConverter conv = ResolveConverter();
                DependencyObject writeBox = newTargetBox;
                if (conv != null) {
                    DependencyObject paramBox = ConverterParam.Length > 0 ? DependencyObject.BoxString(ConverterParam) : null;
                    object back = conv.ConvertBack(
                        writeBox,
                        TargetProp,
                        paramBox,
                        "");
                    writeBox = DependencyObject.BoxObject(back);
                }
                BindingEngine.WritePathBox(srcObj, SourcePath, writeBox);
            }
        }
    }
}

internal class VmSubBucket {
    public BindingEntry[] Items;
    public VmSubBucket() { Items = new BindingEntry[0]; }
}

internal class ElementBindingList : DependencyObject {
    public BindingEntry[] Items;
    public ElementBindingList() { Items = new BindingEntry[0]; }
}

public delegate void FrameTickHook(float dt);

public static class BindingEngine {
    private static BindingEntry[] _entries;
    private static Action[] _preFlushHooks;
    private static Action[] _postFlushHooks;
    private static Action[] _postLayoutHooks;
    private static FrameTickHook[] _frameTickHooks;
    internal static int _batchDepth = 0;
    private static BindingEntry[] _batchPending;
    private static bool _flushPassDirty = false;
    private static bool _inPostFlush = false;
    internal static int _curLayoutSizeEpoch = 1;
    internal static int _managedVmNotifyEpoch = 1;
    internal static int _curNativeVmEpoch = 1;
    internal static int _curManagedVmEpoch = 1;
    internal static bool _inFlushPass = false;
    internal static bool _inSubtreeFlush = false;
    internal static bool _inPushWave = false;
    private static int _lastCompactDisposeEpoch = -1;
    private static int _structEpoch = 1;
    private static bool _hasFlushSnapshot = false;
    private static int _snapStructEpoch = -1;
    private static int _snapNativeVmEp = -1;
    private static int _snapManagedVmEp = -1;
    private static int _snapUiPropEp = -1;
    private static int _snapLayoutEp = -1;
    private static int _snapDcEp = -1;
    private static int _snapTreeEp = -1;
    private static int _snapVisEp = -1;
    private static int _snapDispEp = -1;
    private static BindingEntry[] _pollOnlyEntries;
    private static VmSubBucket[] _vmSubs;
    private static BindingEntry[] _dirtyQueue;
    private static int _dirtyWaveGen = 1;
    private static bool _pendingPushWave = false;
    private static int _debugShortCircuitFlushCount = 0;
    private static int _debugPushFlushCount = 0;
    private static int _debugEvaluatedEntryCount = 0;

    internal static void BumpStructEpoch() {
        _structEpoch = _structEpoch >= 1000000000 ? 1 : (_structEpoch + 1);
        _hasFlushSnapshot = false;
    }

    public static int DebugShortCircuitFlushCount() {
        return _debugShortCircuitFlushCount;
    }

    public static int DebugPushFlushCount() {
        return _debugPushFlushCount;
    }

    public static int DebugEvaluatedEntryCount() {
        return _debugEvaluatedEntryCount;
    }

    public static void DebugResetFlushStats() {
        _debugShortCircuitFlushCount = 0;
        _debugPushFlushCount = 0;
        _debugEvaluatedEntryCount = 0;
    }

    internal static void SubscribeVmToken(int tok, BindingEntry entry) {
        if (tok <= 0 || entry == null) { return; }
        if (_vmSubs == null) { _vmSubs = new VmSubBucket[0]; }
        while (_vmSubs.Length <= tok) { _vmSubs.Push(null); }
        VmSubBucket bucket = _vmSubs[tok];
        if (bucket == null) {
            bucket = new VmSubBucket();
            _vmSubs[tok] = bucket;
        }
        BindingEntry[] arr = bucket.Items;
        for (int i = 0; i < arr.Length; i += 1) {
            if (arr[i] == entry) { return; }
        }
        arr.Push(entry);
    }

    internal static void UnsubscribeVmToken(int tok, BindingEntry entry) {
        if (tok <= 0 || entry == null || _vmSubs == null || tok >= _vmSubs.Length) { return; }
        VmSubBucket bucket = _vmSubs[tok];
        if (bucket == null || bucket.Items == null) { return; }
        BindingEntry[] arr = bucket.Items;
        for (int i = arr.Length - 1; i >= 0; i -= 1) {
            if (arr[i] == null || arr[i] == entry) {
                arr.RemoveAt(i);
            }
        }
        if (arr.Length == 0) {
            _vmSubs[tok] = null;
        }
    }

    internal static void EnqueueDirtyEntry(BindingEntry entry) {
        if (entry == null || entry.IsDead()) { return; }
        if (entry._dirtyEnqueuedGen == _dirtyWaveGen) { return; }
        entry._dirtyEnqueuedGen = _dirtyWaveGen;
        if (_dirtyQueue == null) { _dirtyQueue = new BindingEntry[0]; }
        _dirtyQueue.Push(entry);
        _pendingPushWave = true;
    }

    private static bool StructEpochsMatch() {
        return _structEpoch == _snapStructEpoch &&
               _curManagedVmEpoch == _snapManagedVmEp &&
               UIElement._dataContextEpoch == _snapDcEp &&
               UIElement._treeParentEpoch == _snapTreeEp &&
               UIElement._visibilityEpoch == _snapVisEp &&
               UIElement._disposeEpoch == _snapDispEp;
    }

    private static void ClearDirtyQueue() {
        _dirtyQueue = new BindingEntry[0];
        _pendingPushWave = false;
        _dirtyWaveGen = _dirtyWaveGen >= 1000000000 ? 1 : (_dirtyWaveGen + 1);
    }

    private static int DrainCppDirtyQueues() {
        if (!ParseUtil.IsNativeReady()) { return 0; }
        int vmCnt = Reflect_BeginConsumeDirtyTokens();
        int uiCnt = Visual_BeginConsumeDirtyPropNodes();
        if (vmCnt >= 0 && _vmSubs != null) {
            for (int i = 0; i < vmCnt; i += 1) {
                int tok = Reflect_GetConsumedDirtyToken(i);
                if (tok > 0 && tok < _vmSubs.Length) {
                    VmSubBucket bucket = _vmSubs[tok];
                    if (bucket != null && bucket.Items != null) {
                        BindingEntry[] items = bucket.Items;
                        for (int j = items.Length - 1; j >= 0; j -= 1) {
                            BindingEntry e = items[j];
                            if (e == null || e.IsDead()) {
                                items.RemoveAt(j);
                                continue;
                            }
                            EnqueueDirtyEntry(e);
                        }
                        if (items.Length == 0) { _vmSubs[tok] = null; }
                    }
                }
            }
        }
        if (uiCnt > 0) {
            for (int i = 0; i < uiCnt; i += 1) {
                object owner = Visual_GetConsumedDirtyPropOwner(i);
                if (owner is UIElement) {
                    UIElement el = owner as UIElement;
                    if (el.HasUiFlag(16384)) {
                        el.BumpPropEpoch();
                    }
                }
            }
        }
        if (vmCnt < 0 || uiCnt < 0) { return -1; }
        return vmCnt + uiCnt;
    }

    internal static void BumpManagedVmEpoch() {
        _managedVmNotifyEpoch = _managedVmNotifyEpoch >= 1000000000 ? 1 : (_managedVmNotifyEpoch + 1);
    }

    private static void RefreshLayoutEpoch() {
        if (ParseUtil.IsNativeReady()) {
            _curLayoutSizeEpoch = Visual_GetWorldLayoutEpoch();
        }
    }

    internal static void RefreshVmEpochs() {
        _curManagedVmEpoch = _managedVmNotifyEpoch;
        if (ParseUtil.IsNativeReady()) {
            _curNativeVmEpoch = Reflect_GetGlobalObservableEpoch();
        }
    }

    internal static void MarkFlushDirty() {
        _flushPassDirty = true;
    }

    public static void BeginBatch() {
        _batchDepth += 1;
        if (_batchPending == null) { _batchPending = new BindingEntry[0]; }
    }

    public static void EndBatch() {
        if (_batchDepth <= 0) { return; }
        _batchDepth -= 1;
        if (_batchDepth > 0 || _batchPending == null || _batchPending.Length == 0) { return; }
        BumpStructEpoch();
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        BindingEntry[] pending = _batchPending;
        _batchPending = new BindingEntry[0];
        for (int i = 0; i < pending.Length; i += 1) {
            BindingEntry e = pending[i];
            if (e != null &&
                (e.Target == null || !e.Target._isDisposedTree) &&
                (e.ContextHost == null || !e.ContextHost._isDisposedTree)) {
                e.Evaluate();
            }
        }
        _inFlushPass = prevInFlush;
    }

    public static void SetPreFlushHook(Action hook) {
        _preFlushHooks = new Action[0];
        if (hook != null) { _preFlushHooks.Push(hook); }
    }

    public static void AddPreFlushHook(Action hook) {
        if (hook == null) { return; }
        if (_preFlushHooks == null) { _preFlushHooks = new Action[0]; }
        _preFlushHooks.Push(hook);
    }

    public static void AddPostFlushHook(Action hook) {
        if (hook == null) { return; }
        if (_postFlushHooks == null) { _postFlushHooks = new Action[0]; }
        _postFlushHooks.Push(hook);
    }

    public static void AddPostLayoutHook(Action hook) {
        if (hook == null) { return; }
        if (_postLayoutHooks == null) { _postLayoutHooks = new Action[0]; }
        _postLayoutHooks.Push(hook);
    }

    public static void AddFrameTickHook(FrameTickHook hook) {
        if (hook == null) { return; }
        if (_frameTickHooks == null) { _frameTickHooks = new FrameTickHook[0]; }
        _frameTickHooks.Push(hook);
    }

    public static void StepFrameTicks(float dt) {
        if (dt <= 0.0f) { return; }
        InputDispatcher.AdvanceUiClock(dt);
        if (_frameTickHooks == null) { return; }
        for (int h = 0; h < _frameTickHooks.Length; h += 1) {
            var fn = _frameTickHooks[h];
            if (fn != null) { fn(dt); }
        }
    }

    private static bool _disposeHookInstalled = false;
    private static int _pendingDisposedCount = 0;

    internal static void OnSubtreeDisposed() {
        _pendingDisposedCount += 1;
        if (_pendingDisposedCount >= 64) {
            _pendingDisposedCount = 0;
            CleanupDisposed();
        }
    }

    private static void EnsureInit() {
        if (_entries == null) { _entries = new BindingEntry[0]; }
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { BindingEngine.OnSubtreeDisposed(); });
        }
    }

    internal static string[] PreparePathSegments(string path) {
        if (path != null && path.Length > 0 && path != ".") {
            if (!path.Contains(".")) { return null; }
            if (UIElement.IsAttachedElementPropPath(path)) {
                var one = new string[1];
                one[0] = path;
                return one;
            }
            return SplitBindingPath(path);
        }
        return null;
    }

    private static void RegisterEntryWithIds(BindingEntry entry, int targetPropId, int srcPropId) {
        EnsureInit();
        BumpStructEpoch();
        if (entry != null) {
            entry.TargetPropId = targetPropId != -2
                ? targetPropId
                : ((entry.Target != null && entry.TargetProp != null) ? AeroPropId.Resolve(entry.TargetProp) : 0);
            entry.SourcePropId = srcPropId != -2
                ? srcPropId
                : ((entry.SourcePath != null) ? AeroPropId.Resolve(entry.SourcePath) : 0);
        }
        _entries.Push(entry);
        if (entry != null && entry.Target != null) {
            ElementBindingList list = null;
            if (entry.Target._ownBindingsBox is ElementBindingList) {
                list = entry.Target._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.Target._ownBindingsBox = list;
            }
            list.Items.Push(entry);
        } else if (entry != null && entry.TargetInline != null) {
            ElementBindingList list = null;
            if (entry.TargetInline._ownBindingsBox is ElementBindingList) {
                list = entry.TargetInline._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.TargetInline._ownBindingsBox = list;
            }
            list.Items.Push(entry);
        } else if (entry != null && entry.TargetEffect != null && entry.ContextHost != null) {
            ElementBindingList list = null;
            if (entry.ContextHost._ownBindingsBox is ElementBindingList) {
                list = entry.ContextHost._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.ContextHost._ownBindingsBox = list;
            }
            list.Items.Push(entry);
        }
        if (_batchDepth > 0) {
            if (_batchPending == null) { _batchPending = new BindingEntry[0]; }
            _batchPending.Push(entry);
            return;
        }
        entry.Evaluate();
    }

    private static void RegisterEntry(BindingEntry entry) {
        RegisterEntryWithIds(entry, -2, -2);
    }

    internal static void BindPathPrepared(
        UIElement target, int targetPropId, string targetProp, DependencyObject explicitSourceBox,
        int srcPropId, string path, string[] segs, bool singleHop,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback, DependencyObject fallbackBox,
        bool hasTargetNull, DependencyObject targetNullBox,
        string relativeMode, string ancestorType) {
        if (target == null || targetProp == null) { return; }
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) { UnbindTriggerProperty(target, targetProp); }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 1;
        e.Mode = mode;
        e.ExplicitSourceBox = explicitSourceBox;
        e.SourcePath = path == null ? "" : path;
        e.PathSegments = segs;
        e.IsSingleHopPath = singleHop;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        e.RelativeSourceMode = relativeMode == null ? "" : relativeMode;
        e.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        RegisterEntryWithIds(e, targetPropId, srcPropId);
    }

    internal static void BindElementPrepared(
        UIElement target, int targetPropId, string targetProp, string elementName,
        int srcPropId, string sourceProp, string[] segs, bool singleHop,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback, DependencyObject fallbackBox,
        bool hasTargetNull, DependencyObject targetNullBox) {
        if (target == null || targetProp == null) { return; }
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) { UnbindTriggerProperty(target, targetProp); }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 2;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.PathSegments = segs;
        e.IsSingleHopPath = singleHop;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntryWithIds(e, targetPropId, srcPropId);
    }

    internal static void BindTemplateParentPrepared(
        UIElement target, int targetPropId, string targetProp, Control templatedParent,
        int srcPropId, string sourceProp, string[] segs, bool singleHop,
        IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback, DependencyObject fallbackBox,
        bool hasTargetNull, DependencyObject targetNullBox, int mode) {
        if (target == null || targetProp == null) { return; }
        target.ClearResourceReference(targetProp);
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = mode;
        e.SourceElement = templatedParent;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.PathSegments = segs;
        e.IsSingleHopPath = singleHop;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntryWithIds(e, targetPropId, srcPropId);
    }

    private static int _textPropIdCache = -2;

    internal static void BindTextFromBindingFast(UIElement target, Binding b) {
        if (target == null || b == null) { return; }
        if (_textPropIdCache == -2) { _textPropIdCache = AeroPropId.Resolve("Text"); }
        string bp = b.Path == null ? "" : b.Path;
        if (!b._prepReady || b._prepPath != bp) {
            b._prepReady = true;
            b._prepPath = bp;
            b._cachedSrcPropId = bp.Length > 0 ? AeroPropId.Resolve(bp) : 0;
            b._cachedSegs = PreparePathSegments(bp);
            b._cachedSingleHop = (b._cachedSegs == null || b._cachedSegs.Length == 1);
        }
        int modeInt = b.Mode == BindingMode.TwoWay ? 1 : (b.Mode == BindingMode.OneTime ? 2 : (b.Mode == BindingMode.OneWayToSource ? 3 : 0));
        if (b.ElementName.Length > 0) {
            BindElementPrepared(
                target, _textPropIdCache, "Text", b.ElementName, b._cachedSrcPropId, bp,
                b._cachedSegs, b._cachedSingleHop, modeInt,
                b.Converter, b.ConverterKey, b.ConverterParameter, b.StringFormat,
                b.HasFallbackValue, b.FallbackBox, b.HasTargetNullValue, b.TargetNullBox);
        } else {
            BindPathPrepared(
                target, _textPropIdCache, "Text", b.SourceBox, b._cachedSrcPropId, bp,
                b._cachedSegs, b._cachedSingleHop, modeInt,
                b.Converter, b.ConverterKey, b.ConverterParameter, b.StringFormat,
                b.HasFallbackValue, b.FallbackBox, b.HasTargetNullValue, b.TargetNullBox,
                b.RelativeSourceMode, b.RelativeSourceAncestorType);
        }
    }

    public static void ClearBindings(UIElement target, string propertyName) {
        if (target == null || propertyName == null || propertyName.Length == 0) { return; }
        if (target._ownBindingsBox is ElementBindingList) {
            ElementBindingList list = target._ownBindingsBox as ElementBindingList;
            for (int i = list.Items.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = list.Items[i];
                if (e == null || (e.Target == target && e.TargetProp == propertyName)) {
                    list.Items.RemoveAt(i);
                }
            }
        }
        if (_entries != null) {
            for (int i = _entries.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = _entries[i];
                if (e == null || (e.Target == target && e.TargetProp == propertyName)) {
                    if (e != null) { e.ClearSourceSubscriptions(); }
                    _entries.RemoveAt(i);
                }
            }
        }
        BumpStructEpoch();
    }

    public static void BindCompiledAction(
        UIElement target, string targetProp, object source, int mode, Action<UIElement, object> updater) {
        if (target == null || targetProp == null || updater == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 5;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.CompiledUpdater = updater;
        RegisterEntry(e);
    }

    public static void BindLambdaOneWay(UIElement target, string targetProp, Func<DependencyObject> getter) {
        if (target == null || targetProp == null || getter == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 0;
        e.Mode = 0;
        e.LambdaGetter = getter;
        RegisterEntry(e);
    }

    public static void BindLambdaString(UIElement target, string targetProp, Func<string> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxString(fn()); });
    }
    public static void BindLambdaFloat(UIElement target, string targetProp, Func<float> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxFloat(fn()); });
    }
    public static void BindLambdaBool(UIElement target, string targetProp, Func<bool> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxBool(fn()); });
    }
    public static void BindLambdaInt(UIElement target, string targetProp, Func<int> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxInt(fn()); });
    }

    public static void BindLambdaTwoWay(
        UIElement target, string targetProp,
        Func<DependencyObject> getter, Action<DependencyObject> setter) {
        if (target == null || targetProp == null || getter == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 0;
        e.Mode = 1;
        e.LambdaGetter = getter;
        e.LambdaSetter = setter;
        RegisterEntry(e);
    }

    public static void BindPath(
        UIElement target, string targetProp, object source, string path,
        int mode = 0, string converterKey = "", string converterParam = "", string stringFormat = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 1;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.SourcePath = path == null ? "" : path;
        e.InitPathSegments();
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        RegisterEntry(e);
    }

    public static void BindElement(
        UIElement target, string targetProp, string elementName, string sourceProp, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 2;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.InitPathSegments();
        RegisterEntry(e);
    }

    public static void BindTemplateParent(
        UIElement target, string targetProp, Control templatedParent, string sourceProp, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        target.ClearResourceReference(targetProp);
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = mode;
        e.SourceElement = templatedParent;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.InitPathSegments();
        RegisterEntry(e);
    }

    public static void BindMulti(
        UIElement target, string targetProp, object source, string[] paths,
        IMultiValueConverter converter, string converterParam = "", string stringFormat = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 4;
        e.Mode = 0;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.MultiPaths = paths;
        e.MultiConverter = converter;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        RegisterEntry(e);
    }

    public static void BindPathEx(
        UIElement target, string targetProp, object source, string path,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null,
        string relativeMode = "", string ancestorType = "") {
        if (target == null || targetProp == null) { return; }
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) {
            UnbindTriggerProperty(target, targetProp);
        }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 1;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.SourcePath = path == null ? "" : path;
        e.InitPathSegments();
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        e.RelativeSourceMode = relativeMode == null ? "" : relativeMode;
        e.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        RegisterEntry(e);
    }

    public static void BindElementEx(
        UIElement target, string targetProp, string elementName, string sourceProp,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null) {
        if (target == null || targetProp == null) { return; }
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) {
            UnbindTriggerProperty(target, targetProp);
        }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 2;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.InitPathSegments();
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntry(e);
    }

    public static void BindInlineEx(
        Inline target, string targetProp, string path, string elementName,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null,
        string relativeMode = "", string ancestorType = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.TargetInline = target;
        e.TargetProp = targetProp;
        e.Kind = (elementName != null && elementName.Length > 0) ? 2 : 1;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = path == null ? "" : path;
        e.InitPathSegments();
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        e.RelativeSourceMode = relativeMode == null ? "" : relativeMode;
        e.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        RegisterEntry(e);
    }

    public static void BindTemplateParentEx(
        UIElement target, string targetProp, Control templatedParent, string sourceProp,
        IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        target.ClearResourceReference(targetProp);
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = mode;
        e.SourceElement = templatedParent;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.InitPathSegments();
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntry(e);
    }

    public static void BindMultiBinding(UIElement target, string targetProp, MultiBinding mb) {
        if (target == null || targetProp == null || mb == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 4;
        e.Mode = 0;
        e.MultiBindings = mb.Bindings;
        e.MultiConverter = mb.Converter;
        e.ConverterParam = mb.ConverterParameter == null ? "" : mb.ConverterParameter;
        e.StringFormat = mb.StringFormat == null ? "" : mb.StringFormat;
        RegisterEntry(e);
    }

    public static void BindEffect(Effect targetEffect, UIElement contextHost, string targetProp, Binding b) {
        if (targetEffect == null || targetProp == null || b == null) { return; }
        var e = new BindingEntry();
        e.TargetEffect = targetEffect;
        e.ContextHost = contextHost;
        e.TargetProp = targetProp;
        e.Mode = b.Mode == BindingMode.TwoWay ? 1 : (b.Mode == BindingMode.OneTime ? 2 : (b.Mode == BindingMode.OneWayToSource ? 3 : 0));
        if (b.ElementName != null && b.ElementName.Length > 0) {
            e.Kind = 2;
            e.ElementName = b.ElementName;
        } else {
            e.Kind = 1;
            e.ExplicitSourceBox = b.SourceBox;
            e.RelativeSourceMode = b.RelativeSourceMode == null ? "" : b.RelativeSourceMode;
            e.RelativeSourceAncestorType = b.RelativeSourceAncestorType == null ? "" : b.RelativeSourceAncestorType;
        }
        e.SourcePath = b.Path == null ? "" : b.Path;
        e.InitPathSegments();
        e.Converter = b.Converter;
        e.ConverterKey = b.ConverterKey == null ? "" : b.ConverterKey;
        e.ConverterParam = b.ConverterParameter == null ? "" : b.ConverterParameter;
        e.StringFormat = b.StringFormat == null ? "" : b.StringFormat;
        e.HasFallback = b.HasFallbackValue;
        e.FallbackBox = b.FallbackBox;
        e.HasTargetNull = b.HasTargetNullValue;
        e.TargetNullBox = b.TargetNullBox;
        RegisterEntry(e);
    }

    public static void NotifyTargetPropertyChanged(UIElement target, string targetProp, DependencyObject newBox) {
        if (target == null) { return; }
        target.BumpPropEpoch();
        MarkFlushDirty();
        if (!(target._ownBindingsBox is ElementBindingList)) { return; }
        BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
        for (int i = 0; i < own.Length; i += 1) {
            BindingEntry e = own[i];
            if (e.Target == target && e.TargetProp == targetProp && e.Mode == 1 && !e._isApplying) {
                e.PushTwoWay(newBox);
            }
        }
    }

    public static void CleanupDisposed() {
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            BindingEntry e = _entries[i];
            if (e == null || e.IsDead()) {
                if (e != null) { e.ClearSourceSubscriptions(); }
                _entries.RemoveAt(i);
                continue;
            }
            if (e.SourceElement != null && e.SourceElement._isDisposedTree) {
                e.ClearSourceSubscriptions();
                e.SourceElement = null;
            }
        }
        BumpStructEpoch();
    }

    public static bool HasTriggerBinding(UIElement target, string prop) {
        if (target == null || prop == null || prop.Length == 0) { return false; }
        if (target._ownBindingsBox is ElementBindingList) {
            BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
            for (int i = 0; i < own.Length; i += 1) {
                if (own[i] != null && own[i]._isTriggerBinding && own[i].TargetProp == prop) {
                    return true;
                }
            }
        }
        return false;
    }

    public static void UnbindTriggerProperty(UIElement target, string prop) {
        if (target == null || prop == null || prop.Length == 0) { return; }
        if (target._ownBindingsBox is ElementBindingList) {
            BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
            for (int i = own.Length - 1; i >= 0; i -= 1) {
                if (own[i] != null && own[i]._isTriggerBinding && own[i].TargetProp == prop) {
                    own.RemoveAt(i);
                }
            }
        }
        if (_entries != null) {
            for (int i = _entries.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = _entries[i];
                if (e != null && e._isTriggerBinding && e.Target == target && e.TargetProp == prop) {
                    e.ClearSourceSubscriptions();
                    _entries.RemoveAt(i);
                }
            }
        }
        BumpStructEpoch();
    }

    public static void UnbindTarget(UIElement target) {
        if (target == null) { return; }
        target._ownBindingsBox = null;
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            if (_entries[i].Target == target) {
                _entries[i].ClearSourceSubscriptions();
                _entries.RemoveAt(i);
            }
        }
        BumpStructEpoch();
    }

    public static void FlushInlinesFor(TextBlock tb) {
        if (tb == null || tb._isDisposedTree) { return; }
        Inline[] inlines = tb.Inlines();
        if (inlines == null) { return; }
        for (int i = 0; i < inlines.Length; i += 1) {
            Inline inl = inlines[i];
            if (inl != null && inl._ownBindingsBox is ElementBindingList) {
                BindingEntry[] own = (inl._ownBindingsBox as ElementBindingList).Items;
                for (int k = 0; k < own.Length; k += 1) {
                    own[k].Evaluate();
                }
            }
        }
    }

    public static void FlushSubtree(UIElement subtreeRoot) {
        if (subtreeRoot == null || subtreeRoot._isDisposedTree) { return; }
        RefreshLayoutEpoch();
        bool prevSub = _inSubtreeFlush;
        _inSubtreeFlush = true;
        var q = new UIElement[0];
        q.Push(subtreeRoot);
        int qi = 0;
        while (qi < q.Length) {
            UIElement node = q[qi];
            qi += 1;
            if (node == null || node._isDisposedTree) { continue; }
            if (node._ownBindingsBox is ElementBindingList) {
                BindingEntry[] own = (node._ownBindingsBox as ElementBindingList).Items;
                for (int k = 0; k < own.Length; k += 1) {
                    own[k].Evaluate();
                }
            }
            if (node is TextBlock) {
                FlushInlinesFor(node as TextBlock);
            }
            int cc = node.ChildCount();
            for (int c = 0; c < cc; c += 1) {
                UIElement ch = node.ChildAt(c);
                if (ch != null && !ch._isDisposedTree) {
                    q.Push(ch);
                }
            }
        }
        _inSubtreeFlush = prevSub;
    }

    private static void FlushCleanupOnly() {
        TriggerBase.FlushPendingCleanup();
        Storyboard.FlushPendingCleanup();
        if (_pendingDisposedCount > 0) {
            _pendingDisposedCount = 0;
            CleanupDisposed();
        }
    }

    private static void FlushBodyAndPostHooks() {
        if (_entries != null) {
            if (_lastCompactDisposeEpoch != UIElement._disposeEpoch) {
                _lastCompactDisposeEpoch = UIElement._disposeEpoch;
                BumpStructEpoch();
                int write = 0;
                for (int i = 0; i < _entries.Length; i += 1) {
                    BindingEntry e = _entries[i];
                    if (e == null || e.IsDead()) {
                        if (e != null) { e.ClearSourceSubscriptions(); }
                        continue;
                    }
                    if (write != i) { _entries[write] = e; }
                    write += 1;
                }
                while (_entries.Length > write) {
                    _entries.RemoveAt(_entries.Length - 1);
                }
            }
            RefreshLayoutEpoch();
            RefreshVmEpochs();
            int drainState = DrainCppDirtyQueues();
            bool structClean = _hasFlushSnapshot && drainState >= 0 && StructEpochsMatch();
            bool shortCircuited = false;
            if (structClean && !_pendingPushWave && drainState == 0 &&
                _curNativeVmEpoch == _snapNativeVmEp &&
                UIElement._globalPropEpoch == _snapUiPropEp &&
                _curLayoutSizeEpoch == _snapLayoutEp) {
                _flushPassDirty = false;
                if (_pollOnlyEntries != null && _pollOnlyEntries.Length > 0) {
                    for (int pi = 0; pi < _pollOnlyEntries.Length; pi += 1) {
                        _pollOnlyEntries[pi].Evaluate();
                        _debugEvaluatedEntryCount += 1;
                    }
                }
                if (!_flushPassDirty && !_pendingPushWave && _structEpoch == _snapStructEpoch) {
                    shortCircuited = true;
                    _debugShortCircuitFlushCount += 1;
                } else {
                    _hasFlushSnapshot = false;
                }
            } else if (structClean) {
                bool pushConverged = false;
                bool prevInPush = _inPushWave;
                _inPushWave = true;
                for (int wave = 0; wave < 4; wave += 1) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _flushPassDirty = false;
                    _dirtyWaveGen = _dirtyWaveGen >= 1000000000 ? 1 : (_dirtyWaveGen + 1);
                    int evalStamp = _dirtyWaveGen;
                    BindingEntry[] batch = _dirtyQueue;
                    _dirtyQueue = new BindingEntry[0];
                    _pendingPushWave = false;
                    if (batch != null) {
                        for (int bi = 0; bi < batch.Length; bi += 1) {
                            BindingEntry de = batch[bi];
                            if (de == null || de.IsDead()) { continue; }
                            de.Evaluate();
                            de._lastEvalWaveGen = evalStamp;
                            _debugEvaluatedEntryCount += 1;
                            if (!de._inPollOnly && !de.IsEpochFastClean()) {
                                if (_pollOnlyEntries == null) { _pollOnlyEntries = new BindingEntry[0]; }
                                _pollOnlyEntries.Push(de);
                                de._inPollOnly = true;
                            }
                        }
                    }
                    if ((wave == 0 || _flushPassDirty) && _pollOnlyEntries != null && _pollOnlyEntries.Length > 0) {
                        for (int pi = 0; pi < _pollOnlyEntries.Length; pi += 1) {
                            BindingEntry pe = _pollOnlyEntries[pi];
                            if (pe != null && pe._lastEvalWaveGen != evalStamp) {
                                pe.Evaluate();
                                pe._lastEvalWaveGen = evalStamp;
                                _debugEvaluatedEntryCount += 1;
                            }
                        }
                    }
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    if (!StructEpochsMatch()) {
                        pushConverged = false;
                        break;
                    }
                    int moreDrained = DrainCppDirtyQueues();
                    if (moreDrained < 0) {
                        pushConverged = false;
                        break;
                    }
                    if (!_pendingPushWave && (_dirtyQueue == null || _dirtyQueue.Length == 0)) {
                        pushConverged = true;
                        break;
                    }
                }
                _inPushWave = prevInPush;
                if (pushConverged) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _snapNativeVmEp = _curNativeVmEpoch;
                    _snapUiPropEp = UIElement._globalPropEpoch;
                    _snapLayoutEp = _curLayoutSizeEpoch;
                    _hasFlushSnapshot = true;
                    shortCircuited = true;
                    _debugPushFlushCount += 1;
                } else {
                    _hasFlushSnapshot = false;
                }
            }
            if (!shortCircuited) {
                ClearDirtyQueue();
                bool converged = false;
                for (int pass = 0; pass < 3; pass += 1) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _flushPassDirty = false;
                    for (int i = 0; i < _entries.Length; i += 1) {
                        _entries[i].Evaluate();
                        _debugEvaluatedEntryCount += 1;
                    }
                    if (!_flushPassDirty) {
                        converged = true;
                        break;
                    }
                }
                if (converged) {
                    if (_pollOnlyEntries == null) {
                        _pollOnlyEntries = new BindingEntry[0];
                    } else {
                        while (_pollOnlyEntries.Length > 0) {
                            _pollOnlyEntries.RemoveAt(_pollOnlyEntries.Length - 1);
                        }
                    }
                    for (int i = 0; i < _entries.Length; i += 1) {
                        BindingEntry pe = _entries[i];
                        if (pe != null) {
                            pe._inPollOnly = false;
                            if (!pe.IsEpochFastClean()) {
                                _pollOnlyEntries.Push(pe);
                                pe._inPollOnly = true;
                            }
                        }
                    }
                    int postDrain = DrainCppDirtyQueues();
                    ClearDirtyQueue();
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _snapStructEpoch = _structEpoch;
                    _snapNativeVmEp = _curNativeVmEpoch;
                    _snapManagedVmEp = _curManagedVmEpoch;
                    _snapUiPropEp = UIElement._globalPropEpoch;
                    _snapLayoutEp = _curLayoutSizeEpoch;
                    _snapDcEp = UIElement._dataContextEpoch;
                    _snapTreeEp = UIElement._treeParentEpoch;
                    _snapVisEp = UIElement._visibilityEpoch;
                    _snapDispEp = UIElement._disposeEpoch;
                    _hasFlushSnapshot = (postDrain >= 0);
                } else {
                    ClearDirtyQueue();
                    _hasFlushSnapshot = false;
                }
            }
        }
        if (!_inPostFlush && _postFlushHooks != null && _postFlushHooks.Length > 0) {
            _inPostFlush = true;
            _flushPassDirty = false;
            for (int h = 0; h < _postFlushHooks.Length; h += 1) {
                var fn = _postFlushHooks[h];
                if (fn != null) { fn(); }
            }
            if (_flushPassDirty && _entries != null) {
                _hasFlushSnapshot = false;
                RefreshVmEpochs();
                _flushPassDirty = false;
                for (int i = 0; i < _entries.Length; i += 1) {
                    _entries[i].Evaluate();
                    _debugEvaluatedEntryCount += 1;
                }
            }
            _inPostFlush = false;
        }
    }

    public static void Flush() {
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        if (_preFlushHooks != null) {
            for (int h = 0; h < _preFlushHooks.Length; h += 1) {
                var fn = _preFlushHooks[h];
                if (fn != null) { fn(); }
            }
        }
        FlushCleanupOnly();
        FlushBodyAndPostHooks();
        _inFlushPass = prevInFlush;
    }

    public static void FlushPostLayout() {
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        FlushCleanupOnly();
        bool hasEntriesOrPost = (_entries != null && _entries.Length > 0) ||
                                (_postFlushHooks != null && _postFlushHooks.Length > 0);
        if (hasEntriesOrPost) {
            FlushBodyAndPostHooks();
        }
        if (_postLayoutHooks != null && _postLayoutHooks.Length > 0) {
            for (int h = 0; h < _postLayoutHooks.Length; h += 1) {
                var fn = _postLayoutHooks[h];
                if (fn != null) { fn(); }
            }
        }
        _inFlushPass = prevInFlush;
    }

    // ---- Reflection Path Traversals ----
    internal static string[] SplitBindingPath(string path) {
        if (path == null || path.Length == 0) { return new string[0]; }
        if (!path.Contains(".")) {
            var single = new string[1];
            single[0] = path;
            return single;
        }
        if (!path.Contains("(")) {
            return path.Split(".");
        }
        var parts = new string[0];
        int parenDepth = 0;
        int start = 0;
        for (int i = 0; i < path.Length; i += 1) {
            string ch = path.Substring(i, 1);
            if (ch == "(") {
                parenDepth += 1;
            } else if (ch == ")" && parenDepth > 0) {
                parenDepth -= 1;
            } else if (ch == "." && parenDepth == 0) {
                parts.Push(path.Substring(start, i - start));
                start = i + 1;
            }
        }
        parts.Push(path.Substring(start));
        return parts;
    }

    internal static DependencyObject ReadSegmentsBox(object root, string path, string[] segs) {
        if (root == null) { return null; }
        if (path == null || path.Length == 0 || path == ".") {
            if (root is DependencyObject) { return root as DependencyObject; }
            return DependencyObject.BoxObject(root);
        }
        if (root is UIElement) {
            UIElement rootEl = root as UIElement;
            DependencyObject direct = rootEl.GetPropertyValue(path);
            if (direct != null) { return direct; }
            if (rootEl.IsExplicitNullProperty(path)) { return null; }
            if (path.Length > 2 && path.Substring(0, 1) == "(" && path.Substring(path.Length - 1, 1) == ")") {
                return null;
            }
        }
        if (root is Transform) {
            Transform rootTr = root as Transform;
            DependencyObject rawAtt = rootTr.GetAttachedValue(path);
            if (rawAtt != null) { return rawAtt; }
            if (path.Length > 2 && path.Substring(0, 1) == "(" && path.Substring(path.Length - 1, 1) == ")") {
                string innerAtt = path.Substring(1, path.Length - 2).Trim();
                DependencyObject directAtt = rootTr.GetAttachedValue(innerAtt);
                if (directAtt != null) { return directAtt; }
            }
        }
        object cur = root;
        string leaf = path;
        if (segs != null || path.Contains(".")) {
            string[] parts = segs != null ? segs : SplitBindingPath(path);
            for (int i = 0; i < parts.Length - 1; i += 1) {
                string seg = parts[i];
                if (cur is UIElement) {
                    UIElement curEl = cur as UIElement;
                    DependencyObject sub = curEl.GetPropertyValue(seg);
                    cur = sub != null ? sub.UnboxObject() : (curEl.IsExplicitNullProperty(seg) ? null : Reflect_GetPropObject(cur, seg));
                } else {
                    cur = Reflect_GetPropObject(cur, seg);
                }
                if (cur == null) { return null; }
            }
            leaf = parts[parts.Length - 1];
        }
        if (cur is UIElement) {
            UIElement leafEl = cur as UIElement;
            DependencyObject local = leafEl.GetPropertyValue(leaf);
            if (local != null) { return local; }
            if (leafEl.IsExplicitNullProperty(leaf)) { return null; }
        }
        if (cur is SolidColorBrush) {
            SolidColorBrush scb = cur as SolidColorBrush;
            if (leaf == "Color") { return DependencyObject.BoxString(scb.Color); }
            if (leaf == "Opacity") { return DependencyObject.BoxFloat(scb.Opacity); }
        }
        if (cur is GradientStop) {
            GradientStop gs = cur as GradientStop;
            if (leaf == "Color") { return DependencyObject.BoxString(gs.Color); }
            if (leaf == "Offset") { return DependencyObject.BoxFloat(gs.Offset); }
        }
        if (cur is Transform) {
            Transform trObj = cur as Transform;
            string cleanLeaf = leaf;
            if (cleanLeaf.Length > 2 && cleanLeaf.Substring(0, 1) == "(" && cleanLeaf.Substring(cleanLeaf.Length - 1, 1) == ")") {
                cleanLeaf = cleanLeaf.Substring(1, cleanLeaf.Length - 2).Trim();
            }
            DependencyObject att = trObj.GetAttachedValue(cleanLeaf);
            if (att != null) { return att; }
            if (cur is TransformGroup && cleanLeaf == "Children") {
                return (cur as TransformGroup).ChildrenBox();
            }
            if (cur is RotateTransform && cleanLeaf == "Angle") { return DependencyObject.BoxFloat((cur as RotateTransform).Angle); }
            if (cur is ScaleTransform && cleanLeaf == "ScaleX") { return DependencyObject.BoxFloat((cur as ScaleTransform).ScaleX); }
            if (cur is ScaleTransform && cleanLeaf == "ScaleY") { return DependencyObject.BoxFloat((cur as ScaleTransform).ScaleY); }
            if (cur is TranslateTransform && cleanLeaf == "X") { return DependencyObject.BoxFloat((cur as TranslateTransform).X); }
            if (cur is TranslateTransform && cleanLeaf == "Y") { return DependencyObject.BoxFloat((cur as TranslateTransform).Y); }
            if (cur is SkewTransform && cleanLeaf == "AngleX") { return DependencyObject.BoxFloat((cur as SkewTransform).AngleX); }
            if (cur is SkewTransform && cleanLeaf == "AngleY") { return DependencyObject.BoxFloat((cur as SkewTransform).AngleY); }
        }
        if (cur is DependencyObject && !(cur is UIElement)) {
            DependencyObject depProp = (cur as DependencyObject).GetBindingPropertyValue(leaf);
            if (depProp != null) { return depProp; }
        }
        if (cur is DependencyObject && leaf == "Value") {
            return cur as DependencyObject;
        }
        if (!ParseUtil.IsNativeReady()) { return null; }
        int kind = Reflect_GetPropTypeKind(cur, leaf);
        if (kind == 1) { return DependencyObject.BoxBool(Reflect_GetPropBool(cur, leaf)); }
        if (kind == 2) { return DependencyObject.BoxInt(Reflect_GetPropInt(cur, leaf)); }
        if (kind == 3) { return DependencyObject.BoxFloat(Reflect_GetPropFloat(cur, leaf)); }
        if (kind == 4) { return DependencyObject.BoxString(Reflect_GetPropString(cur, leaf)); }
        if (kind == 6) {
            return DependencyObject.BoxEnum(
                Reflect_GetPropString(cur, leaf),
                Reflect_GetPropInt(cur, leaf));
        }
        if (kind == 5) {
            object objVal = Reflect_GetPropObject(cur, leaf);
            return objVal != null ? DependencyObject.BoxObject(objVal) : null;
        }
        return null;
    }

    public static DependencyObject ReadPathBox(object root, string path) {
        if (root is UIElement && path != null && path.Length > 0) {
            UIElement rootEl = root as UIElement;
            DependencyObject direct = rootEl.GetPropertyValue(path);
            if (direct != null) { return direct; }
            if (rootEl.IsExplicitNullProperty(path)) { return null; }
        }
        return ReadSegmentsBox(root, path, null);
    }

    private static int TryParseBracketIndexer(string seg, string[] outProp) {
        if (seg == null || seg.Length < 3) { return -1; }
        int lb = -1;
        int rb = -1;
        for (int i = 0; i < seg.Length; i += 1) {
            string ch = seg.Substring(i, 1);
            if (ch == "[" && lb < 0) { lb = i; }
            else if (ch == "]" && lb >= 0) { rb = i; break; }
        }
        if (lb < 0 || rb <= lb + 1) { return -1; }
        outProp[0] = seg.Substring(0, lb);
        return ParseUtil.ParseInt(seg.Substring(lb + 1, rb - lb - 1), -1);
    }

    public static void WritePathBox(object root, string path, DependencyObject valBox) {
        if (root == null || path == null || path.Length == 0) { return; }
        if (root is UIElement && (root as UIElement).SetPropertyValue(path, valBox)) { return; }
        string[] segs = path.Split(".");
        object cur = root;
        for (int i = 0; i < segs.Length - 1; i += 1) {
            cur = Reflect_GetPropObject(cur, segs[i]);
            if (cur == null) { return; }
        }
        string leaf = segs[segs.Length - 1];
        if (cur is UIElement) {
            if ((cur as UIElement).SetPropertyValue(leaf, valBox)) { return; }
        }
        if (!ParseUtil.IsNativeReady()) { return; }
        var propOut = new string[1];
        int listIdx = TryParseBracketIndexer(leaf, propOut);
        if (listIdx >= 0) {
            object listObj = propOut[0].Length > 0 ? Reflect_GetPropObject(cur, propOut[0]) : cur;
            if (listObj != null) {
                Reflect_ListSet(listObj, listIdx, DependencyObject.Unbox(valBox));
                MarkFlushDirty();
            }
            return;
        }
        if (valBox is BoxedBool) { Reflect_SetPropBool(cur, leaf, (valBox as BoxedBool).Value); return; }
        if (valBox is BoxedInt) { Reflect_SetPropInt(cur, leaf, (valBox as BoxedInt).Value); return; }
        if (valBox is BoxedFloat) { Reflect_SetPropFloat(cur, leaf, (valBox as BoxedFloat).Value); return; }
        if (valBox is BoxedString) { Reflect_SetPropString(cur, leaf, (valBox as BoxedString).Value); return; }
        Reflect_SetPropObject(cur, leaf, DependencyObject.Unbox(valBox));
    }
}
