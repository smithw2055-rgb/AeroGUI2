module Aero.UI;

import Aero.Native;
import RealScript.Math;

public delegate void PopupDismissHook(UIElement clickedTarget, bool isEscape);
public delegate void AttachedPropChangedHook(UIElement el, string prop, DependencyObject val);

public class BehaviorEventSink : DependencyObject {
    public virtual void OnBehaviorEvent(object sender, RoutedEventArgs e) {}
}

public static class AeroPropId {
    private static Dictionary<string, int> _byName;

    private static void EnsureInit() {
        if (_byName != null) { return; }
        var m = new Dictionary<string, int>();
        string[] prim = "Width|Height|MinWidth|MinHeight|MaxWidth|MaxHeight|ActualWidth|ActualHeight|Margin|Padding|BorderThickness|CornerRadius|HorizontalAlignment|VerticalAlignment|Opacity|BrushOpacity|EffectOpacity|Effect|Effect.Radius|IsEnabled|IsMouseOver|IsPressed|IsFocused|IsKeyboardFocusWithin|IsHitTestVisible|Focusable|ClipToBounds|Visibility|Background|Background.Color|BorderBrush|BorderBrush.Color|BorderBrush.Opacity|RenderTransform|LayoutTransform|RenderTransformOrigin|OpacityMask|Name|DockPanel.Dock|ZIndex|AlternationIndex|TabIndex|DataContext|Tag|Grid.Row|Grid.Column|Grid.RowSpan|Grid.ColumnSpan|Canvas.Left|Canvas.Top|Canvas.Right|Canvas.Bottom|FlowDirection|Rotation3DX|Rotation3DY|Rotation3DZ|Translation3DX|Translation3DY|Translation3DZ".Split("|");
        for (int i = 0; i < prim.Length; i += 1) { m[prim[i]] = i + 1; }
        string[] aliases = "Effect.BlurRadius=19|IsKeyboardFocused=23|Dock=39|Panel.ZIndex=40|Canvas.ZIndex=40|ItemsControl.AlternationIndex=41|(ItemsControl.AlternationIndex)=41|KeyboardNavigation.TabIndex=42|Rot3DX=54|Transform3D.RotationX=54|Projection3D.RotationX=54|Rot3DY=55|Transform3D.RotationY=55|Projection3D.RotationY=55|Rot3DZ=56|Transform3D.RotationZ=56|Projection3D.RotationZ=56|Trans3DX=57|Transform3D.TranslateX=57|Trans3DY=58|Transform3D.TranslateY=58|Trans3DZ=59|Transform3D.TranslateZ=59".Split("|");
        for (int j = 0; j < aliases.Length; j += 1) {
            string[] kv = aliases[j].Split("=");
            if (kv.Length == 2) { m[kv[0]] = ParseUtil.ParseInt(kv[1], 0); }
        }
        _byName = m;
    }

    public static int Resolve(string name) {
        if (name == null || name.Length == 0) { return 0; }
        EnsureInit();
        if (_byName.ContainsKey(name)) { return _byName[name]; }
        return 0;
    }
}

internal class UIElementTransform3DData {
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
}

internal class UIElementEventStore {
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

    internal void RaiseRouted(int kind, UIElement sender) {
        if (kind == 0) { Loaded(sender, new RoutedEventArgs("Loaded", sender)); return; }
        if (kind == 1) { Unloaded(sender, new RoutedEventArgs("Unloaded", sender)); return; }
        if (kind == 2) { GotFocus(sender, new RoutedEventArgs("GotFocus", sender)); return; }
        LostFocus(sender, new RoutedEventArgs("LostFocus", sender));
    }
    internal void RaiseMouse(int kind, UIElement sender, MouseEventArgs e) {
        if (kind == 0) { MouseEnter(sender, e); return; }
        if (kind == 1) { MouseLeave(sender, e); return; }
        MouseMove(sender, e);
    }
    internal void RaiseMouseButton(int kind, UIElement sender, MouseButtonEventArgs e) {
        if (kind == 0) { PreviewMouseLeftButtonDown(sender, e); return; }
        if (kind == 1) { MouseLeftButtonDown(sender, e); return; }
        if (kind == 2) { MouseLeftButtonUp(sender, e); return; }
        if (kind == 3) { MouseRightButtonDown(sender, e); return; }
        MouseRightButtonUp(sender, e);
    }
    internal void RaiseKey(int kind, UIElement sender, KeyEventArgs e) {
        if (kind == 0) { PreviewKeyDown(sender, e); return; }
        if (kind == 1) { KeyDown(sender, e); return; }
        KeyUp(sender, e);
    }
    internal void RaiseMouseWheel(UIElement sender, MouseWheelEventArgs e) { MouseWheel(sender, e); }
    internal void RaiseTextInput(UIElement sender, TextCompositionEventArgs e) { TextInput(sender, e); }
    internal void RaiseDataContextChangedBoxes(UIElement sender, DependencyObject oldBox, DependencyObject newBox) {
        DataContextChanged(sender, DependencyPropertyChangedEventArgs.FromBoxes("DataContext", oldBox, newBox));
    }
    internal void RaiseDataContextChangedValues(UIElement sender, object oldVal, object newVal) {
        DataContextChanged(sender, new DependencyPropertyChangedEventArgs("DataContext", oldVal, newVal));
    }
}

internal class UIElementRareData {
    internal DependencyObject ToolTip;
    internal Func<object, UIElement> ToolTipFactory;
    internal UIElement ContextMenuEl;
    internal KeyBinding[] InputBindings;
    internal string RiveSource = "";
    internal ResourceDictionary LocResources;
    internal string LocSource = "";
    internal string Uid = "";
    internal Transform LayoutTransform;
    internal Brush OpacityMask;
    internal BindingEntry[] _propSubscribers;

    // Merged from UIElementLayoutExtra & cold UIElement slots
    internal int DynResMutEpoch = -1;
    internal int DynResChainEpoch = -1;
    internal float CanvasLeft = ParseUtil.NaN();
    internal float CanvasTop = ParseUtil.NaN();
    internal float CanvasRight = ParseUtil.NaN();
    internal float CanvasBottom = ParseUtil.NaN();
    internal string FlowDirection = "LeftToRight";
    internal string[] DynPropKeys;
    internal DependencyObject[] DynPropVals;

    internal UIElementEventStore _events;
    internal DependencyObject Tag;
    internal ResourceScopeCache ResCache;
    internal UIElement[] DetachedLogicalChildren;
    internal DependencyObject VsgBox;
    internal ResourceDictionary Resources;
    internal ResourceDictionary AmbientResources;
    internal string[] DynResProps;
    internal string[] DynResKeys;
    internal Transform RenderTransform;
    internal Effect Effect;
    internal EffectChangeHook EffectHook;
    internal string ExplicitFontFamily = "";
    internal string ExplicitFontWeight = "";
    internal Brush ExplicitTextForeground;
    internal string InternalRootName = "";
    internal UIElement LogicalParent;
    internal string[] ActiveTriggerProps;
    internal Action[] StateInvalidatedHooks;
    internal Style FocusVisualStyle;
    internal string[] RoutedEventNames;
    internal BehaviorEventSink[] RoutedEventSubs;
    internal DependencyObject DdLastItemBox;
    internal string DdLastEmptyPath = "";
    internal int DdLastItemVer = -1;
    internal int DdLastElemEpoch = -1;
    internal bool DdInitialized = false;
    internal UIElementTransform3DData Transform3DData;
    internal Dictionary<string, UIElement> TemplateNameMap;
    internal UIElement TemplateNameOwner;

    internal UIElementTransform3DData Ensure3D() {
        if (Transform3DData == null) { Transform3DData = new UIElementTransform3DData(); }
        return Transform3DData;
    }
}

internal class ResourceScopeCache {
    public int MutEpoch;
    public int ChainEpoch;
    public int AmbDepth = -1;
    public ResourceDictionary AmbTop;
    public int NextSlot;
    public string K0; public DependencyObject V0;
    public string K1; public DependencyObject V1;
    public string K2; public DependencyObject V2;
    public string K3; public DependencyObject V3;
    public Dictionary<string, int> OverflowIdx;
    public DependencyObject[] OverflowVals;
}

public partial class UIElement : DependencyObject {
    internal int _propEpoch = 1;
    internal int _ancestorChainEpoch = 1;
    internal int _childrenMutEpoch = 0;
    internal static int _disposeEpoch = 1;
    internal static int _treeParentEpoch = 1;
    internal static int _visibilityEpoch = 1;
    internal static int _dataContextEpoch = 1;
    internal static int _globalPropEpoch = 1;
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
        _globalPropEpoch = _globalPropEpoch >= 1000000000 ? 1 : (_globalPropEpoch + 1);
        if (HasUiFlag(16384)) {
            NotifyPropSubscribers();
        }
    }

    internal void AddPropSubscriber(BindingEntry sub) {
        if (sub == null || _isDisposedTree) { return; }
        UIElementRareData r = EnsureRare();
        if (r._propSubscribers == null) { r._propSubscribers = new BindingEntry[0]; }
        for (int i = 0; i < r._propSubscribers.Length; i += 1) {
            if (r._propSubscribers[i] == sub) {
                SetUiFlag(16384, true);
                return;
            }
        }
        r._propSubscribers.Push(sub);
        SetUiFlag(16384, true);
    }

    internal void RemovePropSubscriber(BindingEntry sub) {
        if (sub == null || _rare == null || _rare._propSubscribers == null) { return; }
        BindingEntry[] subs = _rare._propSubscribers;
        for (int i = subs.Length - 1; i >= 0; i -= 1) {
            if (subs[i] == null || subs[i] == sub) {
                subs.RemoveAt(i);
            }
        }
        if (subs.Length == 0) {
            SetUiFlag(16384, false);
        }
    }

    internal void NotifyPropSubscribers() {
        if (_rare == null || _rare._propSubscribers == null) {
            SetUiFlag(16384, false);
            return;
        }
        BindingEntry[] subs = _rare._propSubscribers;
        if (subs.Length == 0) {
            SetUiFlag(16384, false);
            return;
        }
        for (int i = subs.Length - 1; i >= 0; i -= 1) {
            BindingEntry sub = subs[i];
            if (sub == null || (sub.Target != null && sub.Target._isDisposedTree) ||
                (sub.ContextHost != null && sub.ContextHost._isDisposedTree)) {
                subs.RemoveAt(i);
                continue;
            }
            BindingEngine.EnqueueDirtyEntry(sub);
        }
        if (subs.Length == 0) {
            SetUiFlag(16384, false);
        }
    }

    internal void BumpSubtreeAncestorEpoch() {
        UIElement[] dlc = _rare != null ? _rare.DetachedLogicalChildren : null;
        bool noChildren = (_children == null || _children.Length == 0);
        bool noDetached = (dlc == null || dlc.Length == 0);
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
            UIElement[] cd = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
            if (cd != null) {
                for (int i = 0; i < cd.Length; i += 1) {
                    q.Push(cd[i]);
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
    internal UIElementRareData _rare;
    internal UIElementRareData EnsureRare() {
        if (_rare == null) { _rare = new UIElementRareData(); }
        return _rare;
    }
    internal UIElementEventStore EnsureEvents() {
        UIElementRareData r = EnsureRare();
        if (r._events == null) { r._events = new UIElementEventStore(); }
        return r._events;
    }
    private DependencyObject _explicitDataContext;
    private DependencyObject _inheritedDataContext;
    private UIElement _templatedParent;
    private UIElement _parent;
    private UIElement[] _children;
    internal DependencyObject _ownBindingsBox;

    private Style _style;

    private float _width = ParseUtil.NaN();
    private float _height = ParseUtil.NaN();
    private float _minWidth = 0.0f;
    private float _minHeight = 0.0f;
    private float _maxWidth = ParseUtil.PositiveInfinity();
    private float _maxHeight = ParseUtil.PositiveInfinity();
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

    internal static AttachedPropChangedHook _attachedPropHook;
    private BlendingMode _blendMode = BlendingMode.Normal;

    private Brush _background;
    private float _bgOpacity = 1.0f;
    private Brush _borderBrush;

    private float _explicitFontSize = 0.0f;
    private float _inheritedFontSize = 0.0f;
    private string _inheritedFontFamily = "";
    private string _inheritedFontWeight = "";
    private Brush _inheritedTextForeground;
    internal Style _appliedStyle;
    internal bool _hasTemplateResources = false;
    internal bool _hasTemplateAmbientResources = false;
    internal bool _isDisposedTree = false;
    private string[] _localProps;
    internal int _uiFlags = 0;
    internal static AttachedPropChangedHook _toolTipEventHook;
    internal static AttachedPropChangedHook _contextMenuSetterHook;

    internal DependencyObject _vsgBox {
        get { return _rare != null ? _rare.VsgBox : null; }
        set { if (value == null && _rare == null) { return; } EnsureRare().VsgBox = value; }
    }
    public string _internalRootName {
        get { return _rare != null ? _rare.InternalRootName : ""; }
        set { if ((value == null || value.Length == 0) && _rare == null) { return; } EnsureRare().InternalRootName = value == null ? "" : value; }
    }
    internal UIElement _logicalParent {
        get { return _rare != null ? _rare.LogicalParent : null; }
        set { if (value == null && _rare == null) { return; } EnsureRare().LogicalParent = value; }
    }
    internal bool HasActiveTriggerProps() {
        return _rare != null && _rare.ActiveTriggerProps != null && _rare.ActiveTriggerProps.Length > 0;
    }
    internal bool HasActiveTriggerProp(string prop) {
        if (_rare == null || _rare.ActiveTriggerProps == null) { return false; }
        string[] atp = _rare.ActiveTriggerProps;
        for (int ti = atp.Length - 1; ti >= 0; ti -= 1) {
            if (atp[ti] == prop) { return true; }
        }
        return false;
    }
    internal void AddActiveTriggerProp(string prop) {
        UIElementRareData r = EnsureRare();
        if (r.ActiveTriggerProps == null) { r.ActiveTriggerProps = new string[0]; }
        r.ActiveTriggerProps.Push(prop);
    }
    internal void RemoveActiveTriggerProp(string prop) {
        if (_rare == null || _rare.ActiveTriggerProps == null) { return; }
        for (int ti = _rare.ActiveTriggerProps.Length - 1; ti >= 0; ti -= 1) {
            if (_rare.ActiveTriggerProps[ti] == prop) {
                _rare.ActiveTriggerProps.RemoveAt(ti);
                break;
            }
        }
    }

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

    public event RoutedEventHandler Loaded {
        add { UIElementEventStore es = EnsureEvents(); es.Loaded += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.Loaded -= value; } }
    }
    public event RoutedEventHandler Unloaded {
        add { UIElementEventStore es = EnsureEvents(); es.Unloaded += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.Unloaded -= value; } }
    }
    public event RoutedEventHandler GotFocus {
        add { UIElementEventStore es = EnsureEvents(); es.GotFocus += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.GotFocus -= value; } }
    }
    public event RoutedEventHandler LostFocus {
        add { UIElementEventStore es = EnsureEvents(); es.LostFocus += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.LostFocus -= value; } }
    }
    public event MouseEventHandler MouseEnter {
        add { UIElementEventStore es = EnsureEvents(); es.MouseEnter += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseEnter -= value; } }
    }
    public event MouseEventHandler MouseLeave {
        add { UIElementEventStore es = EnsureEvents(); es.MouseLeave += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseLeave -= value; } }
    }
    public event MouseEventHandler MouseMove {
        add { UIElementEventStore es = EnsureEvents(); es.MouseMove += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseMove -= value; } }
    }
    public event MouseButtonEventHandler MouseLeftButtonDown {
        add { UIElementEventStore es = EnsureEvents(); es.MouseLeftButtonDown += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseLeftButtonDown -= value; } }
    }
    public event MouseButtonEventHandler MouseLeftButtonUp {
        add { UIElementEventStore es = EnsureEvents(); es.MouseLeftButtonUp += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseLeftButtonUp -= value; } }
    }
    public event MouseButtonEventHandler MouseRightButtonDown {
        add { UIElementEventStore es = EnsureEvents(); es.MouseRightButtonDown += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseRightButtonDown -= value; } }
    }
    public event MouseButtonEventHandler MouseRightButtonUp {
        add { UIElementEventStore es = EnsureEvents(); es.MouseRightButtonUp += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseRightButtonUp -= value; } }
    }
    public event MouseButtonEventHandler PreviewMouseLeftButtonDown {
        add { UIElementEventStore es = EnsureEvents(); es.PreviewMouseLeftButtonDown += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.PreviewMouseLeftButtonDown -= value; } }
    }
    public event MouseWheelEventHandler MouseWheel {
        add { UIElementEventStore es = EnsureEvents(); es.MouseWheel += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.MouseWheel -= value; } }
    }
    public event KeyEventHandler KeyDown {
        add { UIElementEventStore es = EnsureEvents(); es.KeyDown += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.KeyDown -= value; } }
    }
    public event KeyEventHandler KeyUp {
        add { UIElementEventStore es = EnsureEvents(); es.KeyUp += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.KeyUp -= value; } }
    }
    public event KeyEventHandler PreviewKeyDown {
        add { UIElementEventStore es = EnsureEvents(); es.PreviewKeyDown += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.PreviewKeyDown -= value; } }
    }
    public event TextCompositionEventHandler TextInput {
        add { UIElementEventStore es = EnsureEvents(); es.TextInput += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.TextInput -= value; } }
    }
    public event DependencyPropertyChangedEventHandler DataContextChanged {
        add { UIElementEventStore es = EnsureEvents(); es.DataContextChanged += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.DataContextChanged -= value; } }
    }
    public event SizeChangedEventHandler SizeChanged {
        add { UIElementEventStore es = EnsureEvents(); es.SizeChanged += value; }
        remove { if (_rare != null && _rare._events != null) { _rare._events.SizeChanged -= value; } }
    }

    public UIElement() {
        InitElement(VisualKind.Box);
    }

    protected UIElement(VisualKind kind) {
        InitElement(kind);
    }

    private void InitElement(VisualKind kind) {
        if (_name == null) { _name = ""; }
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
        _blendMode = BlendingMode.Normal;
        if (_inheritedFontFamily == null) { _inheritedFontFamily = ""; }
        if (_inheritedFontWeight == null) { _inheritedFontWeight = ""; }
        _children = null;
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
        get { return _rare != null ? _rare.Uid : ""; }
        set { EnsureRare().Uid = value == null ? "" : value; }
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
        get { return _rare != null ? DependencyObject.Unbox(_rare.Tag) : null; }
        set {
            DependencyObject next = DependencyObject.BoxObject(value);
            DependencyObject cur = _rare != null ? _rare.Tag : null;
            bool same = (cur == next) || (cur != null && next != null && cur.BoxValueEquals(next));
            if (same) { return; }
            if (next == null && _rare == null) { return; }
            EnsureRare().Tag = next;
            NotifyBoundPropertyChanged("Tag", next);
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
            if (node._rare != null) {
                if (node._rare.AmbientResources != null) {
                    ResourceDictionary.PushAmbient(node._rare.AmbientResources);
                    pushed.Push(node._rare.AmbientResources);
                }
                if (node._rare.Resources != null) {
                    ResourceDictionary.PushAmbient(node._rare.Resources);
                    pushed.Push(node._rare.Resources);
                }
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
    public void EnableToolTipHover() {
        if (_toolTipEventHook != null) {
            var tth = _toolTipEventHook;
            tth(this, "ToolTip", DependencyObject.BoxBool(true));
        }
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
        _dataContextEpoch = _dataContextEpoch >= 1000000000 ? 1 : (_dataContextEpoch + 1);
        DependencyObject eff = EffectiveDataContextBox();
        RaiseDataContextChangedBoxes(oldBox, eff);
        PropagateDataContextQueue(this, eff, false);
    }

    private void RaiseDataContextChangedBoxes(DependencyObject oldBox, DependencyObject newBox) {
        OnDataContextChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(newBox));
        if (_rare != null && _rare._events != null) {
            _rare._events.RaiseDataContextChangedBoxes(this, oldBox, newBox);
        }
    }

    private void RaiseDataContextChanged(object oldVal, object newVal) {
        OnDataContextChanged(oldVal, newVal);
        if (_rare != null && _rare._events != null) {
            _rare._events.RaiseDataContextChangedValues(this, oldVal, newVal);
        }
    }

    public ResourceDictionary Resources {
        get {
            UIElementRareData r = EnsureRare();
            if (r.Resources == null) {
                r.Resources = new ResourceDictionary();
            }
            return r.Resources;
        }
        set {
            if (value == null && (_rare == null || _rare.Resources == null)) { return; }
            EnsureRare().Resources = value;
            ResourceDictionary.BumpGlobalMutationEpoch();
            ApplyStyleSubtree();
            RefreshDynamicResourcesRecursive();
        }
    }

    public ResourceDictionary AmbientResources {
        get { return _rare != null ? _rare.AmbientResources : null; }
        set {
            if (_rare == null && value == null) { return; }
            if (_rare != null && _rare.AmbientResources == value) { return; }
            UIElementRareData r = EnsureRare();
            r.AmbientResources = value;
            r.ResCache = null;
            r.DynResMutEpoch = -1;
            if (_children != null && _children.Length > 0) {
                BumpSubtreeAncestorEpoch();
            }
        }
    }

    public Style Style {
        get { return _style; }
        set {
            if (_style == value && (_appliedStyle == value || _closedPopupBuildDepth > 0 || (_xamlBuildDepth > 0 && !HasUiFlag(32)))) { return; }
            Style prev = _style;
            _style = value;
            _appliedStyle = null;
            if ((prev != null && !prev.HasNoStyleResourcesFast()) ||
                (value != null && !value.HasNoStyleResourcesFast())) {
                if (_children != null && _children.Length > 0) {
                    ResourceDictionary.BumpGlobalMutationEpoch();
                } else if (_rare != null) {
                    _rare.ResCache = null;
                    _rare.DynResMutEpoch = -1;
                }
            }
            if (_closedPopupBuildDepth > 0 || (_xamlBuildDepth > 0 && !HasUiFlag(32))) { return; }
            ApplyStyleInternal();
        }
    }
    public Style FocusVisualStyle {
        get { return _rare != null ? _rare.FocusVisualStyle : null; }
        set {
            if (value == null && _rare == null) { return; }
            EnsureRare().FocusVisualStyle = value;
        }
    }

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
        UIElementRareData r = EnsureRare();
        if (r.DynResProps == null) {
            r.DynResProps = new string[0];
            r.DynResKeys = new string[0];
        }
        for (int i = 0; i < r.DynResProps.Length; i += 1) {
            if (r.DynResProps[i] == propName) {
                r.DynResKeys[i] = resourceKey;
                r.DynResMutEpoch = -1;
                if (_xamlBuildDepth == 0 || HasUiFlag(32) || propName == "Style") {
                    ApplyDynamicResourceAt(i);
                }
                return;
            }
        }
        r.DynResProps.Push(propName);
        r.DynResKeys.Push(resourceKey);
        r.DynResMutEpoch = -1;
        if (_xamlBuildDepth == 0 || HasUiFlag(32) || propName == "Style") {
            ApplyDynamicResourceAt(r.DynResProps.Length - 1);
        }
    }

    public string GetResourceReferenceKey(string propName) {
        if (propName == null || _rare == null || _rare.DynResProps == null) { return ""; }
        for (int i = 0; i < _rare.DynResProps.Length; i += 1) {
            if (_rare.DynResProps[i] == propName) { return _rare.DynResKeys[i]; }
        }
        return "";
    }

    public void ClearResourceReference(string propName) {
        if (propName == null || _rare == null || _rare.DynResProps == null) { return; }
        for (int i = _rare.DynResProps.Length - 1; i >= 0; i -= 1) {
            if (_rare.DynResProps[i] == propName) {
                _rare.DynResProps.RemoveAt(i);
                _rare.DynResKeys.RemoveAt(i);
            }
        }
        _rare.DynResMutEpoch = -1;
    }

    private void ApplyDynamicResourceAt(int idx) {
        if (_rare == null || _rare.DynResKeys == null || _rare.DynResProps == null || idx < 0 || idx >= _rare.DynResProps.Length) { return; }
        DependencyObject res = FindResourceBox(_rare.DynResKeys[idx]);
        if (res != null) {
            SetPropertyValue(_rare.DynResProps[idx], res);
        }
    }

    public void RefreshDynamicResourcesRecursive() {
        int mutEp = ResourceDictionary._globalMutationEpoch;
        if (_children == null || _children.Length == 0) {
            if (!_isDisposedTree && _rare != null && _rare.DynResProps != null) {
                if (_rare.DynResMutEpoch == mutEp && _rare.DynResChainEpoch == _ancestorChainEpoch) {
                    return;
                }
                for (int i = 0; i < _rare.DynResProps.Length; i += 1) {
                    ApplyDynamicResourceAt(i);
                }
                _rare.DynResMutEpoch = mutEp;
                _rare.DynResChainEpoch = _ancestorChainEpoch;
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
            if (cur._rare != null && cur._rare.DynResProps != null) {
                if (cur._rare.DynResMutEpoch != mutEp || cur._rare.DynResChainEpoch != cur._ancestorChainEpoch) {
                    for (int i = 0; i < cur._rare.DynResProps.Length; i += 1) {
                        cur.ApplyDynamicResourceAt(i);
                    }
                    cur._rare.DynResMutEpoch = mutEp;
                    cur._rare.DynResChainEpoch = cur._ancestorChainEpoch;
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
                Style prevApplied = _appliedStyle;
                _appliedStyle = active;
                if ((prevApplied != null && !prevApplied.HasNoStyleResourcesFast()) ||
                    !active.HasNoStyleResourcesFast()) {
                    if (_children != null && _children.Length > 0) {
                        ResourceDictionary.BumpGlobalMutationEpoch();
                    } else if (_rare != null) {
                        _rare.ResCache = null;
                        _rare.DynResMutEpoch = -1;
                    }
                }
                active.ApplyTo(this);
                OnStyleApplied(active);
            }
        }
        SetUiFlag(64, false);
    }

    public void ApplyStyleSubtree() {
        if (!(this is Control) && (_children == null || _children.Length == 0)) {
            if (!_isDisposedTree && (_style == null || _style != _appliedStyle)) { ApplyStyleInternal(); }
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
            if (cur._style == null || cur._style != cur._appliedStyle) {
                cur.ApplyStyleInternal();
            }
            if (!cur._isDisposedTree && cur is Control) {
                Control ctrl = cur as Control;
                if (!ctrl.IsTemplateApplied() && !ctrl.ShouldDeferTemplateExpansion() &&
                    (ctrl.Template != null || ctrl.TemplatedParent != null)) {
                    ctrl.ApplyTemplate();
                }
            }
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
                if (!ctrl.IsTemplateApplied() && !ctrl.ShouldDeferTemplateExpansion()) { ctrl.ApplyTemplate(); }
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

    private Thickness ReadNativeThickness(int baseIdx) {
        if (!_hasNative) { return Thickness.SharedZero(); }
        float l = Visual_GetNodeFloat(__nativeHandle, baseIdx);
        float t = Visual_GetNodeFloat(__nativeHandle, baseIdx + 1);
        float r = Visual_GetNodeFloat(__nativeHandle, baseIdx + 2);
        float b = Visual_GetNodeFloat(__nativeHandle, baseIdx + 3);
        if (l == 0.0f && t == 0.0f && r == 0.0f && b == 0.0f) { return Thickness.SharedZero(); }
        return new Thickness(l, t, r, b);
    }
    private void NotifyNativeThickness(string prop, float l, float t, float r, float b) {
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            DependencyObject box = (l == 0.0f && t == 0.0f && r == 0.0f && b == 0.0f)
                ? Thickness.SharedZero()
                : new Thickness(l, t, r, b);
            BindingEngine.NotifyTargetPropertyChanged(this, prop, box);
        }
    }

    public void SetMargin4(float l, float t, float r, float b) {
        if (_hasNative) { Visual_SetMargin(__nativeHandle, l, t, r, b); }
        NotifyNativeThickness("Margin", l, t, r, b);
    }
    public void SetPadding4(float l, float t, float r, float b) {
        if (_hasNative) { Visual_SetPadding(__nativeHandle, l, t, r, b); }
        SyncPadding();
        NotifyNativeThickness("Padding", l, t, r, b);
    }
    public void SetBorderThickness4(float l, float t, float r, float b) {
        if (_hasNative) { Visual_SetBorderThickness(__nativeHandle, l, t, r, b); }
        OnBoxAppearanceChanged();
        NotifyNativeThickness("BorderThickness", l, t, r, b);
    }
    public void SetCornerRadius4(float tl, float tr, float br, float bl) {
        if (_hasNative) { Visual_SetCornerRadius(__nativeHandle, tl, tr, br, bl); }
        OnBoxAppearanceChanged();
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            DependencyObject box = (tl == 0.0f && tr == 0.0f && br == 0.0f && bl == 0.0f)
                ? CornerRadius.SharedZero()
                : new CornerRadius(tl, tr, br, bl);
            BindingEngine.NotifyTargetPropertyChanged(this, "CornerRadius", box);
        }
    }
    public void SetRenderTransformOrigin2(float x, float y) {
        SetUiFlag(512, true);
        if (_hasNative) { Visual_SetTransformOrigin(__nativeHandle, x, y); }
        BumpPropEpoch();
        if (_ownBindingsBox != null) {
            DependencyObject box = (Math.Absf(x - 0.5f) <= 0.00001f && Math.Absf(y - 0.5f) <= 0.00001f)
                ? Point.SharedCenter()
                : new Point(x, y);
            BindingEngine.NotifyTargetPropertyChanged(this, "RenderTransformOrigin", box);
        }
    }

    public Thickness Margin {
        get { return ReadNativeThickness(0); }
        set {
            if (value == null) { SetMargin4(0.0f, 0.0f, 0.0f, 0.0f); return; }
            SetMargin4(value.Left, value.Top, value.Right, value.Bottom);
        }
    }
    public Thickness Padding {
        get { return ReadNativeThickness(4); }
        set {
            if (value == null) { SetPadding4(0.0f, 0.0f, 0.0f, 0.0f); return; }
            SetPadding4(value.Left, value.Top, value.Right, value.Bottom);
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
            Visibility oldVis = _visibility;
            bool wasCollapsed = (oldVis == Visibility.Collapsed);
            _visibility = value;
            BumpVisibilityEpoch();
            if (_hasNative) { Visual_SetVisibility(__nativeHandle, EnumConvert.ToInt(value)); }
            OnVisibilityChanged(oldVis, _visibility);
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
        return _borderBrush != null && _hasNative && Visual_GetNodeFloat(__nativeHandle, 18) > 0.0f;
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
        get { return _rare != null ? _rare.RenderTransform : null; }
        set {
            if (value == null && _rare == null) { return; }
            EnsureRare().RenderTransform = value;
            SyncTransform2D();
            NotifyBoundPropertyChanged("RenderTransform", value);
        }
    }
    public Transform LayoutTransform {
        get { return _rare != null ? _rare.LayoutTransform : null; }
        set { EnsureRare().LayoutTransform = value; SyncLayoutTransform(); NotifyBoundPropertyChanged("LayoutTransform", value); }
    }
    internal void EnsureLayoutTransformEnabledForAnim() {
        if (!_hasNative || HasUiFlag(1)) { return; }
        if (_rare != null && _rare.LayoutTransform != null) {
            SyncLayoutTransform();
        } else {
            SetUiFlag(1, true);
            Visual_SetLayoutTransform(__nativeHandle, 1.0f, 1.0f, 0.0f, true);
        }
    }
    public Point RenderTransformOrigin {
        get {
            if (!_hasNative || !HasUiFlag(512)) { return Point.SharedCenter(); }
            float ox = Visual_GetNodeFloat(__nativeHandle, 16);
            float oy = Visual_GetNodeFloat(__nativeHandle, 17);
            if (Math.Absf(ox - 0.5f) <= 0.00001f && Math.Absf(oy - 0.5f) <= 0.00001f) {
                return Point.SharedCenter();
            }
            return new Point(ox, oy);
        }
        set {
            if (value == null) {
                if (!HasUiFlag(512)) { return; }
                SetUiFlag(512, false);
                if (_rare != null && _rare.RenderTransform != null) {
                    SyncTransform2DOriginOnly();
                } else if (_hasNative) {
                    Visual_SetTransformOrigin(__nativeHandle, 0.5f, 0.5f);
                }
                NotifyBoundPropertyChanged("RenderTransformOrigin", Point.SharedCenter());
                return;
            }
            SetRenderTransformOrigin2(value.X, value.Y);
        }
    }

    public float Rotation3DX { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Rot3DX : 0.0f; } set { if (Rotation3DX == value) { return; } EnsureRare().Ensure3D().Rot3DX = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DX", DependencyObject.BoxFloat(value)); } }
    public float Rotation3DY { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Rot3DY : 0.0f; } set { if (Rotation3DY == value) { return; } EnsureRare().Ensure3D().Rot3DY = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DY", DependencyObject.BoxFloat(value)); } }
    public float Rotation3DZ { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Rot3DZ : 0.0f; } set { if (Rotation3DZ == value) { return; } EnsureRare().Ensure3D().Rot3DZ = value; SyncTransform3D(); NotifyBoundPropertyChanged("Rotation3DZ", DependencyObject.BoxFloat(value)); } }
    public float Translation3DX { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Trans3DX : 0.0f; } set { if (Translation3DX == value) { return; } EnsureRare().Ensure3D().Trans3DX = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DX", DependencyObject.BoxFloat(value)); } }
    public float Translation3DY { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Trans3DY : 0.0f; } set { if (Translation3DY == value) { return; } EnsureRare().Ensure3D().Trans3DY = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DY", DependencyObject.BoxFloat(value)); } }
    public float Translation3DZ { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Trans3DZ : 0.0f; } set { if (Translation3DZ == value) { return; } EnsureRare().Ensure3D().Trans3DZ = value; SyncTransform3D(); NotifyBoundPropertyChanged("Translation3DZ", DependencyObject.BoxFloat(value)); } }
    public float CameraDistance { get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.CamDist : 0.0f; } set { if (CameraDistance == value) { return; } EnsureRare().Ensure3D().CamDist = value; SyncTransform3D(); NotifyBoundPropertyChanged("CameraDistance", DependencyObject.BoxFloat(value)); } }

    public Effect Effect {
        get { return _rare != null ? _rare.Effect : null; }
        set {
            Effect prev = _rare != null ? _rare.Effect : null;
            if (prev == value) { return; }
            if (prev != null && _rare != null && _rare.EffectHook != null) {
                prev.RemoveChangedListener(_rare.EffectHook);
                _rare.EffectHook = null;
            }
            if (value == null && _rare == null) { return; }
            UIElementRareData r = EnsureRare();
            r.Effect = value;
            if (value != null) {
                Effect effRef = value;
                r.EffectHook = value.AddChangedListener(() => {
                    if (_rare != null && _rare.Effect == effRef && !_isDisposedTree) { SyncEffect(); }
                });
            }
            SyncEffect();
            NotifyBoundPropertyChanged("Effect", value);
        }
    }
    public Brush OpacityMask {
        get { return _rare != null ? _rare.OpacityMask : null; }
        set {
            if (OpacityMask == value) { return; }
            EnsureRare().OpacityMask = value;
            SyncOpacityMask();
            NotifyBoundPropertyChanged("OpacityMask", value);
        }
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
        get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Projection3D : null; }
        set {
            if (value == null && (_rare == null || _rare.Transform3DData == null)) { return; }
            UIElementTransform3DData td3 = EnsureRare().Ensure3D();
            td3.Projection3D = value;
            BumpPropEpoch();
            if (value != null) {
                td3.Rot3DX = value.RotationX;
                td3.Rot3DY = value.RotationY;
                td3.Rot3DZ = value.RotationZ;
                td3.Trans3DX = value.LocalOffsetX + value.GlobalOffsetX;
                td3.Trans3DY = value.LocalOffsetY + value.GlobalOffsetY;
                td3.Trans3DZ = value.LocalOffsetZ + value.GlobalOffsetZ;
                td3.Scale3DX = 1.0f;
                td3.Scale3DY = 1.0f;
                td3.Scale3DZ = 1.0f;
                SetUiFlag(2, false);
                SyncTransform3D();
            }
        }
    }
    public CompositeTransform3D Transform3D {
        get { return (_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Transform3D : null; }
        set {
            if (value == null && (_rare == null || _rare.Transform3DData == null)) { return; }
            UIElementTransform3DData td3 = EnsureRare().Ensure3D();
            td3.Transform3D = value;
            BumpPropEpoch();
            if (value != null) {
                td3.Rot3DX = value.RotationX;
                td3.Rot3DY = value.RotationY;
                td3.Rot3DZ = value.RotationZ;
                td3.Trans3DX = value.TranslateX;
                td3.Trans3DY = value.TranslateY;
                td3.Trans3DZ = value.TranslateZ;
                td3.Scale3DX = value.ScaleX;
                td3.Scale3DY = value.ScaleY;
                td3.Scale3DZ = value.ScaleZ;
                td3.Center3DX = value.CenterX;
                td3.Center3DY = value.CenterY;
                td3.Center3DZ = value.CenterZ;
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
        string next = ff == null ? "" : ff;
        if (next.Length == 0 && _rare == null) { return; }
        EnsureRare().ExplicitFontFamily = next;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public string EffectiveTextFontFamilyOrEmpty() {
        if (_rare != null && _rare.ExplicitFontFamily.Length > 0) { return _rare.ExplicitFontFamily; }
        return _inheritedFontFamily == null ? "" : _inheritedFontFamily;
    }
    public void SetExplicitTextFontWeight(string fw) {
        string next = fw == null ? "" : fw;
        if (next.Length == 0 && _rare == null) { return; }
        EnsureRare().ExplicitFontWeight = next;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public string EffectiveTextFontWeightOrEmpty() {
        if (_rare != null && _rare.ExplicitFontWeight.Length > 0) { return _rare.ExplicitFontWeight; }
        return _inheritedFontWeight == null ? "" : _inheritedFontWeight;
    }
    public void SetExplicitTextForeground(Brush fg) {
        if (fg == null && _rare == null) { return; }
        EnsureRare().ExplicitTextForeground = fg;
        BumpPropEpoch();
        PropagateTypographyToSubtree();
    }
    public Brush EffectiveTextForeground() {
        return (_rare != null && _rare.ExplicitTextForeground != null) ? _rare.ExplicitTextForeground : _inheritedTextForeground;
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
                BindingEngine.NotifyTargetPropertyChanged(this, "Background.Color", DependencyObject.BoxColor(_background != null ? _background.ResolveRgba() : 0u));
            }
        }
    }
    public Brush BorderBrush {
        get { return _borderBrush; }
        set { _borderBrush = value; OnBoxAppearanceChanged(); NotifyBoundPropertyChanged("BorderBrush", _borderBrush); }
    }
    public Thickness BorderThickness {
        get { return ReadNativeThickness(8); }
        set {
            if (value == null) { SetBorderThickness4(0.0f, 0.0f, 0.0f, 0.0f); return; }
            SetBorderThickness4(value.Left, value.Top, value.Right, value.Bottom);
        }
    }
    public CornerRadius CornerRadius {
        get {
            if (!_hasNative) { return CornerRadius.SharedZero(); }
            float tl = Visual_GetNodeFloat(__nativeHandle, 12);
            float tr = Visual_GetNodeFloat(__nativeHandle, 13);
            float br = Visual_GetNodeFloat(__nativeHandle, 14);
            float bl = Visual_GetNodeFloat(__nativeHandle, 15);
            if (tl == 0.0f && tr == 0.0f && br == 0.0f && bl == 0.0f) { return CornerRadius.SharedZero(); }
            return new CornerRadius(tl, tr, br, bl);
        }
        set {
            if (value == null) { SetCornerRadius4(0.0f, 0.0f, 0.0f, 0.0f); return; }
            SetCornerRadius4(value.TopLeft, value.TopRight, value.BottomRight, value.BottomLeft);
        }
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
    protected virtual void SyncPadding() {}
    public string EffectiveFlowDirection() {
        UIElement cur = this;
        int guard = 0;
        while (cur != null && guard < 256) {
            guard += 1;
            if (cur.HasUiFlag(16)) { return cur._rare != null ? cur._rare.FlowDirection : "LeftToRight"; }
            cur = cur.Parent;
        }
        return _rare != null ? _rare.FlowDirection : "LeftToRight";
    }

    public string FlowDirection {
        get { return EffectiveFlowDirection(); }
        set {
            string fd = (value == null || value.Trim().Length == 0) ? "LeftToRight" : value.Trim();
            SetUiFlag(16, true);
            UIElementRareData r = EnsureRare();
            if (r.FlowDirection != fd) {
                r.FlowDirection = fd;
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
                if (_rare != null && _rare.FlowDirection == "RightToLeft") { hEnc = hEnc + 512; }
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
            bool borderOnly = effHit && _visualKind == VisualKind.Box && _background == null;
            if (borderOnly != HasUiFlag(8192)) {
                SetUiFlag(8192, borderOnly);
                Visual_SetHitTestBorderOnly(__nativeHandle, borderOnly);
            }
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
        bool hasExpOrig = HasUiFlag(512);
        float ox = hasExpOrig ? Visual_GetNodeFloat(__nativeHandle, 16) : 0.5f;
        float oy = hasExpOrig ? Visual_GetNodeFloat(__nativeHandle, 17) : 0.5f;
        Transform rt = _rare != null ? _rare.RenderTransform : null;
        if (rt == null) {
            Visual_SetTransform2D(__nativeHandle, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, ox, oy);
            return;
        }
        TransformData td = TransformData.SharedScratch();
        rt.Accumulate(td);
        bool useZeroOrig = (td.HasExplicitCenter && !hasExpOrig);
        Visual_SetTransform2D(
            __nativeHandle,
            td.TX, td.TY, td.SX, td.SY, td.RotDeg, td.SkewX, td.SkewY,
            useZeroOrig ? 0.0f : ox,
            useZeroOrig ? 0.0f : oy);
    }
    public void SyncLayoutTransform() {
        if (!_hasNative) { return; }
        Transform lt = _rare != null ? _rare.LayoutTransform : null;
        if (lt == null) {
            SetUiFlag(1, false);
            Visual_SetLayoutTransform(__nativeHandle, 1.0f, 1.0f, 0.0f, false);
            return;
        }
        SetUiFlag(1, true);
        TransformData td = TransformData.SharedScratch();
        lt.Accumulate(td);
        Visual_SetLayoutTransform(__nativeHandle, td.SX, td.SY, td.RotDeg, true);
    }
    public void SyncTransform3D() {
        if (!_hasNative) { return; }
        UIElementTransform3DData td3 = _rare != null ? _rare.Transform3DData : null;
        if (td3 == null) {
            Visual_SetTransform3DEx(__nativeHandle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false);
            return;
        }
        Visual_SetTransform3DEx(
            __nativeHandle,
            td3.Rot3DX, td3.Rot3DY, td3.Rot3DZ,
            td3.Trans3DX, td3.Trans3DY, td3.Trans3DZ,
            td3.Scale3DX, td3.Scale3DY, td3.Scale3DZ,
            td3.Center3DX, td3.Center3DY, td3.Center3DZ,
            td3.CamDist, HasUiFlag(2));
    }
    public void SyncEffect() {
        if (!_hasNative) { return; }
        Effect eff = _rare != null ? _rare.Effect : null;
        if (eff == null) {
            Visual_SetEffect(__nativeHandle, 0, 0.0f, 0.0f, 0.0f, 0u, 1.0f);
            return;
        }
        Visual_SetEffect(
            __nativeHandle,
            EnumConvert.ToInt(eff.Kind),
            eff.Radius,
            eff.Depth,
            eff.Direction,
            eff.ColorRgba,
            eff.Opacity);
    }
    public void SyncOpacityMask() {
        if (!_hasNative) { return; }
        Brush om = _rare != null ? _rare.OpacityMask : null;
        if (om == null) {
            Visual_SetOpacityMask(__nativeHandle, false, -1, 4294967295u, 1.0f);
            return;
        }
        int gradId = om.ResolveGradIdUnscaled();
        uint solidRgba = om.ResolveRgbaUnscaled();
        float op = Math.Clampf(om.Opacity, 0.0f, 1.0f);
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
        Visual_SetBoxColors(__nativeHandle, bgCol, bdCol);
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

    public static void SetCanvasLeft(UIElement el, float v) { if (el != null) { el.EnsureRare().CanvasLeft = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasLeft(UIElement el) { return (el != null && el._rare != null) ? el._rare.CanvasLeft : ParseUtil.NaN(); }
    public static void SetCanvasTop(UIElement el, float v) { if (el != null) { el.EnsureRare().CanvasTop = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasTop(UIElement el) { return (el != null && el._rare != null) ? el._rare.CanvasTop : ParseUtil.NaN(); }
    public static void SetCanvasRight(UIElement el, float v) { if (el != null) { el.EnsureRare().CanvasRight = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasRight(UIElement el) { return (el != null && el._rare != null) ? el._rare.CanvasRight : ParseUtil.NaN(); }
    public static void SetCanvasBottom(UIElement el, float v) { if (el != null) { el.EnsureRare().CanvasBottom = v; el.SyncCanvasOffsets(); } }
    public static float GetCanvasBottom(UIElement el) { return (el != null && el._rare != null) ? el._rare.CanvasBottom : ParseUtil.NaN(); }
    private void SyncCanvasOffsets() {
        if (_hasNative && _rare != null) {
            Visual_SetCanvasOffsets(__nativeHandle, _rare.CanvasLeft, _rare.CanvasTop, _rare.CanvasRight, _rare.CanvasBottom);
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
        _childrenMutEpoch += 1;
        if (_rare != null) { _rare.TemplateNameMap = null; }
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
        _childrenMutEpoch += 1;
        if (_rare != null) { _rare.TemplateNameMap = null; }
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
        if (_parent == null && !HasUiFlag(32) &&
            (_rare == null || (_rare.Resources == null && _rare.LocResources == null &&
                               _rare.ExplicitFontFamily.Length == 0 && _rare.ExplicitFontWeight.Length == 0 &&
                               _rare.ExplicitTextForeground == null)) &&
            _explicitDataContext == null && _inheritedDataContext == null &&
            _explicitFontSize <= 0.0f) {
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
        _childrenMutEpoch += 1;
        if (_rare != null) { _rare.TemplateNameMap = null; }
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
            _childrenMutEpoch += 1;
            if (_rare != null) { _rare.TemplateNameMap = null; }
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
        set { if (_keyboardTabIndex != value) { _keyboardTabIndex = value; BumpPropEpoch(); } }
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
            if (_rare != null && _rare._events != null) {
                _rare._events.RaiseRouted(0, this);
            }
        }
    }

    private void NotifyUnloadedSelf() {
        if (HasUiFlag(32)) {
            SetUiFlag(32, false);
            if (_rare != null && _rare._events != null) {
                _rare._events.RaiseRouted(1, this);
            }
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
        UIElementRareData r = EnsureRare();
        if (r.DetachedLogicalChildren == null) { r.DetachedLogicalChildren = new UIElement[0]; }
        for (int i = 0; i < r.DetachedLogicalChildren.Length; i += 1) {
            if (r.DetachedLogicalChildren[i] == child) { return; }
        }
        r.DetachedLogicalChildren.Push(child);
    }

    internal void UnregisterDetachedLogicalChild(UIElement child) {
        if (child == null || _rare == null || _rare.DetachedLogicalChildren == null) { return; }
        for (int i = _rare.DetachedLogicalChildren.Length - 1; i >= 0; i -= 1) {
            if (_rare.DetachedLogicalChildren[i] == child) {
                _rare.DetachedLogicalChildren.RemoveAt(i);
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
            UIElement[] dlc = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
            if (dlc != null) {
                for (int i = 0; i < dlc.Length; i += 1) {
                    q.Push(dlc[i]);
                }
            }
            if (cur._rare != null) {
                UIElementRareData cr = cur._rare;
                if (cr.Tag is UIElement) {
                    UIElement tagEl = cr.Tag as UIElement;
                    if (!tagEl._isDisposedTree && (tagEl._parent == null || tagEl._parent == cur)) {
                        q.Push(tagEl);
                    }
                }
                cr.Tag = null;
                cr._propSubscribers = null;
                cur.SetUiFlag(16384, false);
                UIElement cmEl = cr.ContextMenuEl;
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
                if (cr.ToolTip is UIElement) {
                    q.Push(cr.ToolTip as UIElement);
                }
                if (cr.Resources != null && cr.Resources.Count() > 0) {
                    string[] rKeys = cr.Resources.Keys();
                    for (int ri = 0; ri < rKeys.Length; ri += 1) {
                        DependencyObject rv = cr.Resources.TryGetLocalBox(rKeys[ri]);
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
                if (cr.Effect != null && cr.EffectHook != null) {
                    cr.Effect.RemoveChangedListener(cr.EffectHook);
                    cr.EffectHook = null;
                }
                cr.StateInvalidatedHooks = null;
                cr.RoutedEventNames = null;
                cr.RoutedEventSubs = null;
                cr.DetachedLogicalChildren = null;
                cr.TemplateNameMap = null;
                cr.TemplateNameOwner = null;
                cr.ResCache = null;
                if (cr.Resources != null) {
                    cr.Resources.CollectOwnedUIElements(q);
                }
                cr.Resources = null;
                cr.AmbientResources = null;
                cr.Effect = null;
                cr.RenderTransform = null;
                cr.DynResProps = null;
                cr.DynResKeys = null;
                cr.LogicalParent = null;
                cr.ActiveTriggerProps = null;
                cr._events = null;
                cur._rare = null;
            } else {
                cur.CollectExtraDisposeTargets(q);
            }
            cur._ownBindingsBox = null;
            if (cur._hasNative && ParseUtil.IsValidHandle(cur.__nativeHandle)) {
                cur._hasNative = false;
                Visual_Destroy(cur.__nativeHandle);
                cur.__nativeHandle = ParseUtil.EmptyHandle();
            }
            cur._parent = null;
            cur._templatedParent = null;
            cur._children = null;
            cur._style = null;
            cur._appliedStyle = null;
            cur._hasTemplateResources = false;
            cur._hasTemplateAmbientResources = false;
            cur._explicitDataContext = null;
            cur._inheritedDataContext = null;
            if (cur._background != null) {
                cur._background.ReleaseNativeMutable();
            }
            cur._background = null;
            if (cur._borderBrush != null) {
                cur._borderBrush.ReleaseNativeMutable();
            }
            cur._borderBrush = null;
            cur._localProps = null;

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
        if (this._name == name) { return this; }
        UIElement rootTp = this._templatedParent;
        if (_children != null) {
            for (int i = 0; i < _children.Length; i += 1) {
                UIElement ch = _children[i];
                if (ch != null && ch._templatedParent == rootTp && ch._name == name) { return ch; }
            }
        }
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
                    if (cur._templatedParent != rootTp && cur is Control && !(cur is ContentControl) && !cur.IsItemsControlBoundary()) { continue; }
                } else {
                    if (cur._name == name) { return cur; }
                }
                UIElement[] cd = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
                if (cd != null) {
                    for (int i = cd.Length - 1; i >= 0; i -= 1) {
                        stack.Push(cd[i]);
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
            UIElement[] cd = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
            if (cd != null) {
                for (int i = cd.Length - 1; i >= 0; i -= 1) {
                    stack.Push(cd[i]);
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
        if (owner != null && _templatedParent == owner) {
            Dictionary<string, UIElement> cachedMap = (_rare != null && _rare.TemplateNameOwner == owner) ? _rare.TemplateNameMap : null;
            if (cachedMap == null) {
                cachedMap = new Dictionary<string, UIElement>();
                var bq = new UIElement[0];
                bq.Push(this);
                int bi = 0;
                while (bi < bq.Length) {
                    UIElement cur = bq[bi];
                    bi += 1;
                    if (cur == null) { continue; }
                    if (cur._name != null && cur._name.Length > 0) {
                        if (cur._templatedParent == owner) {
                            if (!cachedMap.ContainsKey(cur._name) || cachedMap[cur._name]._templatedParent != owner) {
                                cachedMap[cur._name] = cur;
                            }
                        } else if (cur._templatedParent == null) {
                            if (!cachedMap.ContainsKey(cur._name)) {
                                cachedMap[cur._name] = cur;
                            }
                        }
                    }
                    if ((cur is ContentPresenter || cur.IsItemsPresenterBoundary()) && cur._templatedParent == owner) { continue; }
                    if (cur._templatedParent != null && !cur.IsInTemplateOf(owner)) { continue; }
                    UIElement[] cd = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
                    if (cd != null) {
                        for (int d = 0; d < cd.Length; d += 1) {
                            bq.Push(cd[d]);
                        }
                    }
                    if (cur._children != null) {
                        for (int i = 0; i < cur._children.Length; i += 1) {
                            bq.Push(cur._children[i]);
                        }
                    }
                }
                UIElementRareData rr = EnsureRare();
                rr.TemplateNameOwner = owner;
                rr.TemplateNameMap = cachedMap;
            }
            if (cachedMap.ContainsKey(name)) {
                UIElement hit = cachedMap[name];
                if (hit != null && !hit._isDisposedTree) { return hit; }
            }
            return null;
        }
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
            UIElement[] cd = cur._rare != null ? cur._rare.DetachedLogicalChildren : null;
            if (cd != null) {
                for (int d = 0; d < cd.Length; d += 1) {
                    q.Push(cd[d]);
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
        return (_rare != null && _rare.Resources != null) ? _rare.Resources.TryGetLocalBox(key) : null;
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
        UIElement anchor = this;
        int foldGuard = 0;
        while (foldGuard < 64) {
            if ((anchor._rare != null &&
                 (anchor._rare.Resources != null || anchor._rare.LocResources != null || anchor._rare.AmbientResources != null)) ||
                anchor._hasTemplateResources ||
                anchor._hasTemplateAmbientResources) {
                break;
            }
            Style ast = anchor._style != null ? anchor._style : anchor._appliedStyle;
            if (ast != null && !ast.HasNoStyleResourcesFast()) {
                break;
            }
            UIElement alp = anchor._rare != null ? anchor._rare.LogicalParent : null;
            UIElement next = (alp != null && alp != anchor) ? alp : anchor._parent;
            if (next == null || next._templatedParent != anchor._templatedParent) {
                break;
            }
            anchor = next;
            foldGuard += 1;
        }
        int mutEp = ResourceDictionary._globalMutationEpoch;
        int chainEp = anchor._ancestorChainEpoch;
        int ambDepth = ResourceDictionary.CurrentAmbientDepth();
        ResourceDictionary ambTop = ResourceDictionary.CurrentAmbientTop();
        ResourceScopeCache rc = anchor._rare != null ? anchor._rare.ResCache : null;
        if (rc != null && rc.MutEpoch == mutEp && rc.ChainEpoch == chainEp &&
            rc.AmbDepth == ambDepth && rc.AmbTop == ambTop) {
            if (rc.K0 == key) { return rc.V0; }
            if (rc.K1 == key) { return rc.V1; }
            if (rc.K2 == key) { return rc.V2; }
            if (rc.K3 == key) { return rc.V3; }
            if (rc.OverflowIdx != null && rc.OverflowIdx.ContainsKey(key)) {
                int oi = rc.OverflowIdx[key];
                return (rc.OverflowVals != null && oi >= 0 && oi < rc.OverflowVals.Length) ? rc.OverflowVals[oi] : null;
            }
        }
        DependencyObject res = anchor.FindResourceBoxUncached(key);
        if (rc == null) {
            rc = new ResourceScopeCache();
            anchor.EnsureRare().ResCache = rc;
        }
        if (rc.MutEpoch != mutEp || rc.ChainEpoch != chainEp ||
            rc.AmbDepth != ambDepth || rc.AmbTop != ambTop) {
            rc.MutEpoch = mutEp;
            rc.ChainEpoch = chainEp;
            rc.AmbDepth = ambDepth;
            rc.AmbTop = ambTop;
            rc.NextSlot = 0;
            rc.K0 = null; rc.V0 = null;
            rc.K1 = null; rc.V1 = null;
            rc.K2 = null; rc.V2 = null;
            rc.K3 = null; rc.V3 = null;
            rc.OverflowIdx = null;
            rc.OverflowVals = null;
        }
        if (rc.K0 == null) { rc.K0 = key; rc.V0 = res; }
        else if (rc.K1 == null) { rc.K1 = key; rc.V1 = res; }
        else if (rc.K2 == null) { rc.K2 = key; rc.V2 = res; }
        else if (rc.K3 == null) { rc.K3 = key; rc.V3 = res; }
        else {
            if (rc.OverflowIdx == null) {
                rc.OverflowIdx = new Dictionary<string, int>();
                rc.OverflowVals = new DependencyObject[0];
            }
            int oIdx = rc.OverflowVals.Length;
            rc.OverflowVals.Push(res);
            rc.OverflowIdx[key] = oIdx;
        }
        return res;
    }

    private DependencyObject FindResourceBoxUncached(string key) {
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
            if (cur._rare != null && cur._rare.Resources != null) {
                DependencyObject resB = cur._rare.Resources.TryGetLocalBox(key);
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
            if (firstElemAmb == null && cur._rare != null && cur._rare.AmbientResources != null) {
                firstElemAmb = cur._rare.AmbientResources.TryGetLocalBox(key);
            }
            UIElement clp = cur._rare != null ? cur._rare.LogicalParent : null;
            cur = (clp != null && clp != cur) ? clp : cur._parent;
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
            if (tp._rare != null && tp._rare.Resources != null) {
                DependencyObject tpLocal = tp._rare.Resources.TryGetLocalBox(key);
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
            if (firstElemAmb == null && tp._rare != null && tp._rare.AmbientResources != null) {
                firstElemAmb = tp._rare.AmbientResources.TryGetLocalBox(key);
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
        Transform curRt = _rare != null ? _rare.RenderTransform : null;
        if (curRt is TransformGroup) {
            return curRt as TransformGroup;
        }
        var tg = new TransformGroup();
        if (curRt != null) {
            tg.Add(curRt);
        }
        EnsureRare().RenderTransform = tg;
        return tg;
    }

    private TransformGroup EnsureLayoutTransformGroup() {
        UIElementRareData r = EnsureRare();
        if (r.LayoutTransform is TransformGroup) {
            return r.LayoutTransform as TransformGroup;
        }
        var tg = new TransformGroup();
        if (r.LayoutTransform != null) {
            tg.Add(r.LayoutTransform);
        }
        r.LayoutTransform = tg;
        return tg;
    }

    private void SetLayoutTransformScalar(string prop, float v) {
        Transform curLt = _rare != null ? _rare.LayoutTransform : null;
        if (prop == "LayoutTransform.ScaleX" || prop == "LayoutTransform.ScaleY") {
            if (curLt is ScaleTransform) {
                if (prop == "LayoutTransform.ScaleX") { (curLt as ScaleTransform).ScaleX = v; }
                else { (curLt as ScaleTransform).ScaleY = v; }
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
            if (curLt is RotateTransform) {
                (curLt as RotateTransform).Angle = v;
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
        bool hasExpOrig = HasUiFlag(512);
        Transform rt = _rare != null ? _rare.RenderTransform : null;
        TransformData td = null;
        if (rt != null && !hasExpOrig) {
            td = TransformData.SharedScratch();
            rt.Accumulate(td);
        }
        bool useZeroOrig = (td != null && td.HasExplicitCenter && !hasExpOrig);
        float ox = (!useZeroOrig && hasExpOrig) ? Visual_GetNodeFloat(__nativeHandle, 16) : (useZeroOrig ? 0.0f : 0.5f);
        float oy = (!useZeroOrig && hasExpOrig) ? Visual_GetNodeFloat(__nativeHandle, 17) : (useZeroOrig ? 0.0f : 0.5f);
        Visual_SetTransform2D(__nativeHandle, tx, ty, sx, sy, rot, skx, sky, ox, oy);
    }

    private void SyncTransform2DOriginOnly() {
        SyncSingleTransform2DScalar(-1, 0.0f);
    }

    private static bool TryApplyScalarToTransform(Transform tr, int propId, float v) {
        if (tr == null) { return false; }
        if (propId == 3 && tr is TranslateTransform) { (tr as TranslateTransform).X = v; return true; }
        if (propId == 4 && tr is TranslateTransform) { (tr as TranslateTransform).Y = v; return true; }
        if (propId == 5 && tr is ScaleTransform) { (tr as ScaleTransform).ScaleX = v; return true; }
        if (propId == 6 && tr is ScaleTransform) { (tr as ScaleTransform).ScaleY = v; return true; }
        if (propId == 7 && tr is RotateTransform) { (tr as RotateTransform).Angle = v; return true; }
        if (propId == 8 && tr is SkewTransform) { (tr as SkewTransform).AngleX = v; return true; }
        if (propId == 9 && tr is SkewTransform) { (tr as SkewTransform).AngleY = v; return true; }
        return false;
    }

    internal void UpdateManagedTransformScalarOnly(int propId, float v) {
        Transform rt = _rare != null ? _rare.RenderTransform : null;
        if (rt == null || propId < 3 || propId > 9) { return; }
        if (TryApplyScalarToTransform(rt, propId, v)) { return; }
        if (rt is TransformGroup) {
            Transform[] kids = (rt as TransformGroup).Children();
            if (kids == null) { return; }
            for (int i = kids.Length - 1; i >= 0; i -= 1) {
                if (TryApplyScalarToTransform(kids[i], propId, v)) { return; }
            }
        }
    }

    protected void SetTransformScalar(string prop, float v) {
        if (prop == "LayoutTransform.ScaleX" || prop == "LayoutTransform.ScaleY" ||
            prop == "LayoutTransform.Angle" || prop == "LayoutTransform.Rotation") {
            SetLayoutTransformScalar(prop, v);
            NotifyBoundPropertyChanged(prop, DependencyObject.BoxFloat(v));
            return;
        }
        Transform curRt = _rare != null ? _rare.RenderTransform : null;
        if (prop == "TranslateX" || prop == "RenderTransform.X") {
            if (curRt is TranslateTransform) {
                (curRt as TranslateTransform).X = v;
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
            if (curRt is TranslateTransform) {
                (curRt as TranslateTransform).Y = v;
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
            if (curRt is ScaleTransform) {
                (curRt as ScaleTransform).ScaleX = v;
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
            if (curRt is ScaleTransform) {
                (curRt as ScaleTransform).ScaleY = v;
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
            if (curRt is RotateTransform) {
                (curRt as RotateTransform).Angle = v;
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
            if (curRt is SkewTransform) {
                (curRt as SkewTransform).AngleX = v;
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
            if (curRt is SkewTransform) {
                (curRt as SkewTransform).AngleY = v;
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
            Transform lt = _rare != null ? _rare.LayoutTransform : null;
            if (lt != null) { lt.Accumulate(ltd); }
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
        Transform rt = _rare != null ? _rare.RenderTransform : null;
        if (rt != null) { rt.Accumulate(td); }
        if (pid == 3) { return td.TX; }
        if (pid == 4) { return td.TY; }
        if (pid == 5) { return td.SX; }
        if (pid == 6) { return td.SY; }
        if (pid == 7) { return td.RotDeg; }
        if (pid == 8) { return td.SkewX; }
        return td.SkewY;
    }

    // ---- Uniform Property Reflection Bridge (Style, Triggers, BindingEngine, VSM, Behaviors) ----
    public bool TryWriteFloatById(int propId, float v) {
        if (propId <= 0) { return false; }
        bool ok = false;
        switch (propId) {
            case 1: Width = v; ok = true; break;
            case 2: Height = v; ok = true; break;
            case 3: MinWidth = v; ok = true; break;
            case 4: MinHeight = v; ok = true; break;
            case 5: MaxWidth = v; ok = true; break;
            case 6: MaxHeight = v; ok = true; break;
            case 15: Opacity = v; ok = true; break;
            case 16: {
                _bgOpacity = Math.Clampf(v, 0.0f, 1.0f);
                if (_hasNative) { Visual_SetBoxBrushOpacity(__nativeHandle, _bgOpacity); }
                NotifyBoundFloatChanged("BrushOpacity", _bgOpacity);
                ok = true;
                break;
            }
            case 17: {
                float eo = Math.Clampf(v, 0.0f, 1.0f);
                if (_rare != null && _rare.Effect != null) { _rare.Effect.Opacity = eo; SyncEffect(); }
                NotifyBoundFloatChanged("EffectOpacity", eo);
                ok = true;
                break;
            }
            case 19: {
                if (_rare != null && _rare.Effect != null) {
                    _rare.Effect.Radius = v;
                    SyncEffect();
                    NotifyBoundFloatChanged("Effect.Radius", _rare.Effect.Radius);
                    ok = true;
                }
                break;
            }
            case 49: SetCanvasLeft(this, v); NotifyBoundFloatChanged("Canvas.Left", v); ok = true; break;
            case 50: SetCanvasTop(this, v); NotifyBoundFloatChanged("Canvas.Top", v); ok = true; break;
            case 51: SetCanvasRight(this, v); NotifyBoundFloatChanged("Canvas.Right", v); ok = true; break;
            case 52: SetCanvasBottom(this, v); NotifyBoundFloatChanged("Canvas.Bottom", v); ok = true; break;
            case 54: Rotation3DX = v; ok = true; break;
            case 55: Rotation3DY = v; ok = true; break;
            case 56: Rotation3DZ = v; ok = true; break;
            case 57: Translation3DX = v; ok = true; break;
            case 58: Translation3DY = v; ok = true; break;
            case 59: Translation3DZ = v; ok = true; break;
        }
        if (ok) { BumpPropEpoch(); }
        return ok;
    }

    public bool TryWriteColorById(int propId, uint rgba) {
        if (propId == 30) {
            Background = new SolidColorBrush(rgba);
            BumpPropEpoch();
            return true;
        }
        if (propId == 32) {
            BorderBrush = new SolidColorBrush(rgba);
            BumpPropEpoch();
            return true;
        }
        return false;
    }

    public bool SetPropertyById(int propId, string propName, DependencyObject value) {
        if (propId <= 0) {
            return (propName != null && propName.Length > 0) ? SetPropertyValue(propName, value) : false;
        }
        BumpPropEpoch();
        switch (propId) {
            case 1: Width = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true;
            case 2: Height = DependencyObject.UnboxFloat(value, ParseUtil.NaN()); return true;
            case 3: MinWidth = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 4: MinHeight = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 5: MaxWidth = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true;
            case 6: MaxHeight = DependencyObject.UnboxFloat(value, ParseUtil.PositiveInfinity()); return true;
            case 9:
                if (value is Thickness) { Margin = value as Thickness; return true; }
                Margin = Thickness.Parse(DependencyObject.UnboxString(value, "0")); return true;
            case 10:
                if (value is Thickness) { Padding = value as Thickness; return true; }
                Padding = Thickness.Parse(DependencyObject.UnboxString(value, "0")); return true;
            case 11:
                if (value is Thickness) { BorderThickness = value as Thickness; return true; }
                BorderThickness = Thickness.Parse(DependencyObject.UnboxString(value, "0")); return true;
            case 12:
                if (value is CornerRadius) { CornerRadius = value as CornerRadius; return true; }
                CornerRadius = CornerRadius.Parse(DependencyObject.UnboxString(value, "0")); return true;
            case 13:
                if (value is BoxedInt) { HorizontalAlignment = EnumConvert.ToHorizontalAlignment((value as BoxedInt).Value); return true; }
                {
                    string ha = DependencyObject.UnboxString(value, "Stretch").Trim();
                    if (ha == "Left" || ha == "1") { HorizontalAlignment = HorizontalAlignment.Left; return true; }
                    if (ha == "Center" || ha == "2") { HorizontalAlignment = HorizontalAlignment.Center; return true; }
                    if (ha == "Right" || ha == "3") { HorizontalAlignment = HorizontalAlignment.Right; return true; }
                    HorizontalAlignment = HorizontalAlignment.Stretch;
                    return true;
                }
            case 14:
                if (value is BoxedInt) { VerticalAlignment = EnumConvert.ToVerticalAlignment((value as BoxedInt).Value); return true; }
                {
                    string va = DependencyObject.UnboxString(value, "Stretch").Trim();
                    if (va == "Top" || va == "1") { VerticalAlignment = VerticalAlignment.Top; return true; }
                    if (va == "Center" || va == "2") { VerticalAlignment = VerticalAlignment.Center; return true; }
                    if (va == "Bottom" || va == "3") { VerticalAlignment = VerticalAlignment.Bottom; return true; }
                    VerticalAlignment = VerticalAlignment.Stretch;
                    return true;
                }
            case 15: Opacity = DependencyObject.UnboxFloat(value, 1.0f); return true;
            case 16: return TryWriteFloatById(16, DependencyObject.UnboxFloat(value, 1.0f));
            case 17: return TryWriteFloatById(17, DependencyObject.UnboxFloat(value, 1.0f));
            case 18: if (value is Effect) { Effect = value as Effect; return true; } return false;
            case 19: return (_rare != null && _rare.Effect != null) ? TryWriteFloatById(19, DependencyObject.UnboxFloat(value, _rare.Effect.Radius)) : false;
            case 20: IsEnabled = DependencyObject.UnboxBool(value, true); return true;
            case 25: IsHitTestVisible = DependencyObject.UnboxBool(value, true); return true;
            case 26: Focusable = DependencyObject.UnboxBool(value, false); return true;
            case 27: ClipToBounds = DependencyObject.UnboxBool(value, false); return true;
            case 28:
                if (value is BoxedInt) { Visibility = EnumConvert.ToVisibility((value as BoxedInt).Value); return true; }
                {
                    string vs = DependencyObject.UnboxString(value, "Visible").Trim();
                    if (vs.Length > 11 && vs.Substring(0, 11) == "Visibility.") { vs = vs.Substring(11); }
                    if (vs == "Hidden" || vs == "1") { Visibility = Visibility.Hidden; return true; }
                    if (vs == "Collapsed" || vs == "2") { Visibility = Visibility.Collapsed; return true; }
                    Visibility = Visibility.Visible;
                    return true;
                }
            case 29:
            case 30: Background = CoerceBrush(value); return true;
            case 31:
            case 32: BorderBrush = CoerceBrush(value); return true;
            case 33: {
                float bop = Math.Clampf(DependencyObject.UnboxFloat(value, 1.0f), 0.0f, 1.0f);
                if (_borderBrush != null) {
                    var clonedBd = new SolidColorBrush(_borderBrush.ResolveRgbaUnscaled());
                    clonedBd.Opacity = bop;
                    _borderBrush = clonedBd;
                    OnBoxAppearanceChanged();
                }
                return true;
            }
            case 34: if (value is Transform) { RenderTransform = value as Transform; return true; } return false;
            case 35:
                if (value is Transform) { LayoutTransform = value as Transform; return true; }
                if (value == null) { LayoutTransform = null; return true; }
                return false;
            case 36:
                if (value is Point) { RenderTransformOrigin = value as Point; return true; }
                {
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
            case 37: OpacityMask = CoerceBrush(value); return true;
            case 38: Name = DependencyObject.UnboxString(value, ""); return true;
            case 39: {
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
            case 40: ZIndex = DependencyObject.UnboxInt(value, 0); return true;
            case 42: TabIndex = DependencyObject.UnboxInt(value, 0); return true;
            case 43: DataContextBox = value; return true;
            case 44: {
                DependencyObject curTag = _rare != null ? _rare.Tag : null;
                bool same = (curTag == value) || (curTag != null && value != null && curTag.BoxValueEquals(value));
                if (!same) {
                    EnsureRare().Tag = value;
                    NotifyBoundPropertyChanged("Tag", value);
                    UpdateTriggersAndStates();
                }
                return true;
            }
            case 45: { int gv = DependencyObject.UnboxInt(value, 0); SetGridRow(this, gv); NotifyBoundPropertyChanged("Grid.Row", DependencyObject.BoxInt(_gridRow)); return true; }
            case 46: { int gv = DependencyObject.UnboxInt(value, 0); SetGridColumn(this, gv); NotifyBoundPropertyChanged("Grid.Column", DependencyObject.BoxInt(_gridCol)); return true; }
            case 47: { int gv = DependencyObject.UnboxInt(value, 1); SetGridRowSpan(this, gv); NotifyBoundPropertyChanged("Grid.RowSpan", DependencyObject.BoxInt(_gridRowSpan)); return true; }
            case 48: { int gv = DependencyObject.UnboxInt(value, 1); SetGridColumnSpan(this, gv); NotifyBoundPropertyChanged("Grid.ColumnSpan", DependencyObject.BoxInt(_gridColSpan)); return true; }
            case 49: return TryWriteFloatById(49, DependencyObject.UnboxFloat(value, 0.0f));
            case 50: return TryWriteFloatById(50, DependencyObject.UnboxFloat(value, 0.0f));
            case 51: return TryWriteFloatById(51, DependencyObject.UnboxFloat(value, 0.0f));
            case 52: return TryWriteFloatById(52, DependencyObject.UnboxFloat(value, 0.0f));
            case 53: {
                string fd = DependencyObject.UnboxString(value, "LeftToRight").Trim();
                if (fd.Length > 0) { FlowDirection = fd; }
                return true;
            }
            case 54: Rotation3DX = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 55: Rotation3DY = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 56: Rotation3DZ = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 57: Translation3DX = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 58: Translation3DY = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 59: Translation3DZ = DependencyObject.UnboxFloat(value, 0.0f); return true;
            case 7:
            case 8:
            case 21:
            case 22:
            case 23:
            case 24:
            case 41:
                return false;
        }
        return false;
    }

    public virtual bool SetPropertyValue(string propName, DependencyObject value) {
        int pid = AeroPropId.Resolve(propName);
        if (pid > 0) {
            return SetPropertyById(pid, propName, value);
        }
        BumpPropEpoch();
        if (propName == null) { return false; }
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
        if (propName.StartsWith("Fill.") && propName.Length > 5 && _background != null) {
            string suffix = propName.Substring(5);
            bool needClone = !HasUiFlag(8);
            Brush updatedBg = _background.ApplySubProperty(suffix, value, needClone);
            if (updatedBg != null) {
                _background = updatedBg;
                if (needClone) { SetUiFlag(8, true); }
                OnBoxAppearanceChanged();
                return true;
            }
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
        if (propName == "Style" && value is Style) {
            Style = value as Style;
            return true;
        }
        if (propName == "FocusVisualStyle") {
            if (value is Style) { FocusVisualStyle = value as Style; }
            else { FocusVisualStyle = null; }
            return true;
        }
        if (propName == "Transform3D.ScaleX" || propName == "Scale3DX") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Ensure3D().Scale3DX = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.ScaleY" || propName == "Scale3DY") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Ensure3D().Scale3DY = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.ScaleZ" || propName == "Scale3DZ") { float sv = DependencyObject.UnboxFloat(value, 1.0f); EnsureRare().Ensure3D().Scale3DZ = sv; SyncTransform3D(); NotifyBoundPropertyChanged(propName, DependencyObject.BoxFloat(sv)); return true; }
        if (propName == "Transform3D.CenterX") { EnsureRare().Ensure3D().Center3DX = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
        if (propName == "Transform3D.CenterY") { EnsureRare().Ensure3D().Center3DY = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
        if (propName == "Transform3D.CenterZ") { EnsureRare().Ensure3D().Center3DZ = DependencyObject.UnboxFloat(value, 0.0f); SetUiFlag(2, true); SyncTransform3D(); return true; }
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
            if (_attachedPropHook != null && cleanCustom.StartsWith("DragDrop.")) {
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
        if (!propName.StartsWith(" ") && !propName.StartsWith("\t") &&
            !propName.EndsWith(" ") && !propName.EndsWith("\t") &&
            !propName.StartsWith("(") && propName.IndexOf(":") < 0) {
            return propName;
        }
        string s = propName.Trim();
        if (s.Length > 2 && s.StartsWith("(") && s.EndsWith(")")) {
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
        UIElementRareData r = EnsureRare();
        if (r.DynPropKeys == null) {
            r.DynPropKeys = new string[0];
            r.DynPropVals = new DependencyObject[0];
        }
        for (int i = 0; i < r.DynPropKeys.Length; i += 1) {
            if (r.DynPropKeys[i] == cleanKey) {
                r.DynPropVals[i] = value;
                return;
            }
        }
        r.DynPropKeys.Push(cleanKey);
        r.DynPropVals.Push(value);
    }

    private DependencyObject GetDynamicProp(string cleanKey) {
        if (_rare == null || _rare.DynPropKeys == null) { return null; }
        for (int i = 0; i < _rare.DynPropKeys.Length; i += 1) {
            if (_rare.DynPropKeys[i] == cleanKey) { return _rare.DynPropVals[i]; }
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
        ResourceDictionary.BumpGlobalMutationEpoch();
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

    public DependencyObject GetPropertyById(int propId, string propName) {
        if (propId <= 0) {
            return (propName != null && propName.Length > 0) ? GetPropertyValue(propName) : null;
        }
        switch (propId) {
            case 1: return DependencyObject.BoxFloat(_width);
            case 2: return DependencyObject.BoxFloat(_height);
            case 3: return DependencyObject.BoxFloat(_minWidth);
            case 4: return DependencyObject.BoxFloat(_minHeight);
            case 5: return DependencyObject.BoxFloat(_maxWidth);
            case 6: return DependencyObject.BoxFloat(_maxHeight);
            case 7: return DependencyObject.BoxFloat(ActualWidth);
            case 8: return DependencyObject.BoxFloat(ActualHeight);
            case 9: return Margin;
            case 10: return Padding;
            case 11: return BorderThickness;
            case 12: return CornerRadius;
            case 13: return DependencyObject.BoxInt(EnumConvert.ToInt(_hAlign));
            case 14: return DependencyObject.BoxInt(EnumConvert.ToInt(_vAlign));
            case 15: return DependencyObject.BoxFloat(_opacity);
            case 16: return DependencyObject.BoxFloat(_bgOpacity);
            case 17: return DependencyObject.BoxFloat((_rare != null && _rare.Effect != null) ? _rare.Effect.Opacity : 1.0f);
            case 18: return _rare != null ? _rare.Effect : null;
            case 19: return DependencyObject.BoxFloat((_rare != null && _rare.Effect != null) ? _rare.Effect.Radius : 0.0f);
            case 20: return DependencyObject.BoxBool(_isEnabled);
            case 21: return DependencyObject.BoxBool(_isMouseOver);
            case 22: return DependencyObject.BoxBool(_isPressed);
            case 23: return DependencyObject.BoxBool(_isFocused);
            case 24: return DependencyObject.BoxBool(_isKeyboardFocusWithin);
            case 25: return DependencyObject.BoxBool(_isHitTestVisible);
            case 26: return DependencyObject.BoxBool(_focusable);
            case 27: return DependencyObject.BoxBool(_clipToBounds);
            case 28: return DependencyObject.BoxInt(EnumConvert.ToInt(_visibility));
            case 29: return _background;
            case 30: return DependencyObject.BoxColor(_background != null ? _background.ResolveRgba() : 0u);
            case 31: return _borderBrush;
            case 32: return DependencyObject.BoxColor(_borderBrush != null ? _borderBrush.ResolveRgba() : 0u);
            case 33: return DependencyObject.BoxFloat(_borderBrush != null ? _borderBrush.Opacity : 1.0f);
            case 34: return _rare != null ? _rare.RenderTransform : null;
            case 35: return _rare != null ? _rare.LayoutTransform : null;
            case 36: return RenderTransformOrigin;
            case 37: return _rare != null ? _rare.OpacityMask : null;
            case 38: return DependencyObject.BoxString(_name);
            case 39: return DependencyObject.BoxInt(EnumConvert.ToInt(_dock));
            case 40: return DependencyObject.BoxInt(_zIndex);
            case 41: return DependencyObject.BoxInt(_alternationIndex);
            case 42: return DependencyObject.BoxInt(_keyboardTabIndex);
            case 43: return EffectiveDataContextBox();
            case 44: return _rare != null ? _rare.Tag : null;
            case 45: return DependencyObject.BoxInt(_gridRow);
            case 46: return DependencyObject.BoxInt(_gridCol);
            case 47: return DependencyObject.BoxInt(_gridRowSpan);
            case 48: return DependencyObject.BoxInt(_gridColSpan);
            case 49: return DependencyObject.BoxFloat(GetCanvasLeft(this));
            case 50: return DependencyObject.BoxFloat(GetCanvasTop(this));
            case 51: return DependencyObject.BoxFloat(GetCanvasRight(this));
            case 52: return DependencyObject.BoxFloat(GetCanvasBottom(this));
            case 53: return DependencyObject.BoxString(_rare != null ? _rare.FlowDirection : "LeftToRight");
            case 54: return DependencyObject.BoxFloat(Rotation3DX);
            case 55: return DependencyObject.BoxFloat(Rotation3DY);
            case 56: return DependencyObject.BoxFloat(Rotation3DZ);
            case 57: return DependencyObject.BoxFloat(Translation3DX);
            case 58: return DependencyObject.BoxFloat(Translation3DY);
            case 59: return DependencyObject.BoxFloat(Translation3DZ);
        }
        return null;
    }

    public virtual DependencyObject GetPropertyValue(string propName) {
        int pid = AeroPropId.Resolve(propName);
        if (pid > 0) {
            return GetPropertyById(pid, propName);
        }
        if (propName == "RiveControl.Source") { return DependencyObject.BoxString(_rare != null ? _rare.RiveSource : ""); }
        if (propName.StartsWith("Background.") && propName.Length > 11 && _background != null) {
            DependencyObject subVal = _background.ReadSubProperty(propName.Substring(11));
            if (subVal != null) { return subVal; }
        }
        if (propName.StartsWith("Fill.") && propName.Length > 5 && _background != null) {
            DependencyObject stopBox = _background.ReadSubProperty(propName.Substring(5));
            if (stopBox != null) { return stopBox; }
        }
        if (propName == "ToolTip" || propName == "ToolTipService.ToolTip") { return _rare != null ? _rare.ToolTip : null; }
        if (propName == "Element.BlendingMode" || propName == "noesis:Element.BlendingMode" ||
            propName == "local:Element.BlendingMode" || propName == "BlendMode") {
            int bm = _blendMode == BlendingMode.Multiply ? 1 : (_blendMode == BlendingMode.Screen ? 2 : (_blendMode == BlendingMode.Additive ? 3 : 0));
            return DependencyObject.BoxInt(bm);
        }
        if (propName == "Transform3D.ScaleX" || propName == "Scale3DX") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Scale3DX : 1.0f); }
        if (propName == "Transform3D.ScaleY" || propName == "Scale3DY") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Scale3DY : 1.0f); }
        if (propName == "Transform3D.ScaleZ" || propName == "Scale3DZ") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Scale3DZ : 1.0f); }
        if (propName == "Transform3D.CenterX") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Center3DX : 0.0f); }
        if (propName == "Transform3D.CenterY") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Center3DY : 0.0f); }
        if (propName == "Transform3D.CenterZ") { return DependencyObject.BoxFloat((_rare != null && _rare.Transform3DData != null) ? _rare.Transform3DData.Center3DZ : 0.0f); }
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
        if (propName == "ContextMenu") { return _rare != null ? _rare.ContextMenuEl : null; }
        if (propName == "Loc.Source" || propName == "noesis:Loc.Source") { return DependencyObject.BoxString(_rare != null ? _rare.LocSource : ""); }
        DependencyObject customBox = GetDynamicProp(NormalizeCustomPropKey(propName));
        if (customBox != null) { return customBox; }
        return null;
    }

    public static Brush CoerceBrush(DependencyObject value) {
        if (value == null) { return null; }
        if (value is Brush) { return value as Brush; }
        if (value is BoxedColor) { return new SolidColorBrush((value as BoxedColor).Value); }
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
            if (_rare != null && _rare._events != null) { _rare._events.RaiseRouted(2, this); }
        } else {
            OnLostFocus();
            if (_rare != null && _rare._events != null) { _rare._events.RaiseRouted(3, this); }
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
            if (_rare != null && _rare._events != null) { _rare._events.RaiseMouse(0, this, args); }
        } else {
            OnMouseLeave(args);
            if (_rare != null && _rare._events != null) { _rare._events.RaiseMouse(1, this, args); }
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
        if (_xamlBuildDepth > 0 && !HasUiFlag(32) && _appliedStyle == null &&
            (!(this is Control) || !(this as Control).IsTemplateApplied())) {
            return;
        }
        Style activeStyle = _style != null ? _style : _appliedStyle;
        if (activeStyle != null) { activeStyle.EvaluateTriggers(this); }
        OnVisualStatePropertyChanged();
        NotifyStateInvalidatedHooks();
    }

    public void AddStateInvalidatedHook(Action cb) {
        if (cb == null) { return; }
        UIElementRareData r = EnsureRare();
        if (r.StateInvalidatedHooks == null) { r.StateInvalidatedHooks = new Action[0]; }
        r.StateInvalidatedHooks.Push(cb);
    }

    protected void NotifyStateInvalidatedHooks() {
        BumpPropEpoch();
        if (_rare == null || _rare.StateInvalidatedHooks == null) { return; }
        Action[] hooks = _rare.StateInvalidatedHooks;
        for (int i = 0; i < hooks.Length; i += 1) {
            var fn = hooks[i];
            if (fn != null) { fn(); }
        }
    }

    protected virtual void OnVisualStatePropertyChanged() {}
    protected virtual void OnVisibilityChanged(Visibility oldVal, Visibility newVal) {}
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

    internal void RaiseMouseMove(MouseEventArgs e) { OnMouseMove(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouse(2, this, e); } }
    internal void RaisePreviewLeftDown(MouseButtonEventArgs e) { OnPreviewMouseLeftButtonDown(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseButton(0, this, e); } }
    internal void RaiseLeftDown(MouseButtonEventArgs e) { OnMouseLeftButtonDown(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseButton(1, this, e); } }
    internal void RaiseLeftUp(MouseButtonEventArgs e) { OnMouseLeftButtonUp(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseButton(2, this, e); } }
    internal void RaiseRightDown(MouseButtonEventArgs e) { OnMouseRightButtonDown(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseButton(3, this, e); } }
    internal void RaiseRightUp(MouseButtonEventArgs e) { OnMouseRightButtonUp(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseButton(4, this, e); } }
    internal void RaiseWheel(MouseWheelEventArgs e) { OnMouseWheel(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseMouseWheel(this, e); } }
    internal void RaisePreviewKeyDown(KeyEventArgs e) { OnPreviewKeyDown(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseKey(0, this, e); } }
    internal void RaiseKeyDown(KeyEventArgs e) { OnKeyDown(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseKey(1, this, e); } }
    internal void RaiseKeyUp(KeyEventArgs e) { OnKeyUp(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseKey(2, this, e); } }
    internal void RaiseTextInput(TextCompositionEventArgs e) { OnTextInput(e); if (_rare != null && _rare._events != null) { _rare._events.RaiseTextInput(this, e); } }

    public virtual bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == null || ev.Length == 0 || sink == null) { return false; }
        UIElementRareData r = EnsureRare();
        if (r.RoutedEventNames == null) {
            r.RoutedEventNames = new string[0];
            r.RoutedEventSubs = new BehaviorEventSink[0];
        }
        r.RoutedEventNames.Push(ev);
        r.RoutedEventSubs.Push(sink);
        return true;
    }

    public void RaiseRoutedEvent(string ev, RoutedEventArgs args) {
        if (ev == null || ev.Length == 0) { return; }
        RoutedEventArgs effArgs = args != null ? args : new RoutedEventArgs(ev, this);
        UIElement cur = this;
        int guard = 0;
        while (cur != null && !effArgs.Handled && guard < 256) {
            guard += 1;
            if (cur._rare != null && cur._rare.RoutedEventSubs != null && cur._rare.RoutedEventNames != null) {
                int n = cur._rare.RoutedEventSubs.Length;
                for (int i = 0; i < n; i += 1) {
                    if (cur._rare.RoutedEventNames[i] == ev && cur._rare.RoutedEventSubs[i] != null) {
                        cur._rare.RoutedEventSubs[i].OnBehaviorEvent(this, effArgs);
                    }
                }
            }
            cur = cur._parent != null ? cur._parent : cur._templatedParent;
        }
    }

    internal virtual UIElement GetTemplateRootInternal() { return null; }
    internal virtual UIElement GetTemplateChildInternal(string name) { return FindName(name); }
    internal virtual bool IsOwnedByTemplateChainInternal(UIElement owner) { return false; }
    internal virtual DependencyObject GetContentBoxInternal() { return null; }

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
