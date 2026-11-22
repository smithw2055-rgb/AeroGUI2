module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Popup, ToolTip, ToolTipService, PopupService & UIElement.ContextMenu
// ============================================================================

public static class PopupService {
    private static Popup[] _openPopups;
    private static bool _hooked = false;
    private static bool _disposeHookInstalled = false;
    private static UIElement _overlayHost;
    private static UIElement[] _overlayRoots;
    private static Canvas[] _overlayCanvases;

    private static void EnsureDisposeHook() {
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { PopupService.CleanupDisposed(); });
        }
    }

    public static void SetOverlayHost(UIElement host) {
        _overlayHost = host;
    }

    public static void CleanupDisposed() {
        if (_overlayHost != null && _overlayHost._isDisposedTree) {
            _overlayHost = null;
        }
        if (_overlayRoots != null && _overlayCanvases != null) {
            for (int i = _overlayRoots.Length - 1; i >= 0; i -= 1) {
                UIElement r = _overlayRoots[i];
                Canvas c = _overlayCanvases[i];
                if (r == null || r._isDisposedTree || c == null || c._isDisposedTree) {
                    _overlayRoots.RemoveAt(i);
                    _overlayCanvases.RemoveAt(i);
                }
            }
        }
        if (_openPopups != null) {
            for (int i = _openPopups.Length - 1; i >= 0; i -= 1) {
                Popup p = _openPopups[i];
                bool dead = p == null || p._isDisposedTree ||
                    (p.PlacementTarget != null && p.PlacementTarget._isDisposedTree);
                if (dead) {
                    if (p != null) { p.ForceCloseDisposed(); }
                    if (i < _openPopups.Length && _openPopups[i] == p) {
                        _openPopups.RemoveAt(i);
                    }
                }
            }
        }
    }

    public static UIElement ResolveOverlayHost(UIElement anchor) {
        if (anchor != null && !anchor._isDisposedTree) {
            UIElement root = anchor.RootElement();
            if (root != null && !root._isDisposedTree) { return root; }
        }
        if (_overlayHost != null && _overlayHost._isDisposedTree) {
            _overlayHost = null;
        }
        return _overlayHost;
    }

    public static Canvas ResolveOverlayCanvas(UIElement anchor) {
        UIElement root = ResolveOverlayHost(anchor);
        if (root == null || root._isDisposedTree) { return null; }
        if (_overlayRoots == null) {
            _overlayRoots = new UIElement[0];
            _overlayCanvases = new Canvas[0];
        }
        for (int i = _overlayRoots.Length - 1; i >= 0; i -= 1) {
            UIElement r = _overlayRoots[i];
            Canvas c = _overlayCanvases[i];
            if (r == null || r._isDisposedTree || c == null || c._isDisposedTree) {
                _overlayRoots.RemoveAt(i);
                _overlayCanvases.RemoveAt(i);
                continue;
            }
            if (r == root) {
                if (c.Parent != root) {
                    if (c.Parent != null) { c.Parent.RemoveChild(c); }
                    root.AddChild(c);
                }
                return c;
            }
        }
        var canvas = new Canvas();
        canvas.Name = "__AeroPopupOverlay";
        canvas.ZIndex = 10000;
        canvas.HorizontalAlignment = HorizontalAlignment.Stretch;
        canvas.VerticalAlignment = VerticalAlignment.Stretch;
        root.AddChild(canvas);
        _overlayRoots.Push(root);
        _overlayCanvases.Push(canvas);
        EnsureDisposeHook();
        return canvas;
    }

    private static void EnsureHooked() {
        EnsureDisposeHook();
        if (_hooked) { return; }
        _hooked = true;
        InputDispatcher.RegisterPopupDismissHook((UIElement target, bool isEscape) => {
            DismissNonPersistent(target, isEscape);
        });
        BindingEngine.AddPreFlushHook(() => {
            RefreshOpenPositions();
        });
    }

    public static void RefreshOpenPositions() {
        if (_openPopups == null || _openPopups.Length == 0) { return; }
        for (int i = _openPopups.Length - 1; i >= 0; i -= 1) {
            Popup p = _openPopups[i];
            if (p == null || p._isDisposedTree || !p.IsOpen) {
                _openPopups.RemoveAt(i);
                continue;
            }
            p.UpdatePosition();
        }
    }

    public static void RegisterOpen(Popup p) {
        if (p == null) { return; }
        EnsureHooked();
        if (_openPopups == null) { _openPopups = new Popup[0]; }
        if (!_openPopups.Contains(p)) { _openPopups.Push(p); }
    }

    public static void Unregister(Popup p) {
        if (_openPopups == null || p == null) { return; }
        int idx = _openPopups.IndexOf(p);
        if (idx >= 0) { _openPopups.RemoveAt(idx); }
    }

    public static void DismissNonPersistent(UIElement clickedTarget, bool isEscape) {
        if (_openPopups == null || _openPopups.Length == 0) { return; }
        UIElement clickedRoot = (clickedTarget != null && !clickedTarget._isDisposedTree) ? clickedTarget.RootElement() : null;
        Popup[] snapshot = new Popup[0];
        for (int i = 0; i < _openPopups.Length; i += 1) {
            snapshot.Push(_openPopups[i]);
        }
        for (int i = 0; i < snapshot.Length; i += 1) {
            Popup p = snapshot[i];
            if (p == null || p._isDisposedTree) { continue; }
            if (p.StaysOpen && !isEscape) { continue; }
            if (!isEscape && clickedTarget != null) {
                UIElement popupRoot = p.PlacementTarget != null ? p.PlacementTarget.RootElement() : p.RootElement();
                if (clickedRoot != null && popupRoot != null && clickedRoot != popupRoot) { continue; }
                if (p.IsAncestorOf(clickedTarget)) { continue; }
                if (p.PlacementTarget != null && p.PlacementTarget.IsAncestorOf(clickedTarget)) { continue; }
            }
            p.IsOpen = false;
        }
    }

    public static void CloseAll() {
        if (_openPopups == null) { return; }
        while (_openPopups.Length > 0) {
            Popup last = _openPopups[_openPopups.Length - 1];
            last.IsOpen = false;
        }
    }
}

public class Popup : FrameworkElement {
    private UIElement _child;
    private bool _isOpen = false;
    private UIElement _placementTarget;
    private UIElement _logicalParent;
    private bool _staysOpen = false;
    private float _horizOffset = 0.0f;
    private float _vertOffset = 0.0f;
    private string _placement = "Bottom";
    private UIElement _attachedRoot;
    private float _appliedScaleX = 1.0f;
    private float _appliedScaleY = 1.0f;

    public event EventHandler Opened;
    public event EventHandler Closed;

    public Popup() : base(VisualKind.Box) {
        ZIndex = 10000;
        Visibility = Visibility.Collapsed;
        HorizontalAlignment = HorizontalAlignment.Left;
        VerticalAlignment = VerticalAlignment.Top;
        RenderTransformOrigin = new Point(0.0f, 0.0f);
        IsHitTestVisible = true;
        SyncInteractiveFlags();
    }

    public override string GetTypeName() { return "Popup"; }

    protected override bool IsSelfHitTestSolid() {
        return Background != null;
    }

    public UIElement Child {
        get { return _child != null ? _child : (ChildCount() > 0 ? ChildAt(0) : null); }
        set {
            if (_child == value) { return; }
            ClearChildren();
            _child = value;
            if (_child != null) { AddChild(_child); }
        }
    }

    public UIElement PlacementTarget {
        get { return _placementTarget; }
        set { _placementTarget = value; UpdatePosition(); }
    }
    public UIElement LogicalParent {
        get { return _logicalParent; }
    }
    public bool StaysOpen { get { return _staysOpen; } set { _staysOpen = value; } }
    public float HorizontalOffset { get { return _horizOffset; } set { _horizOffset = value; UpdatePosition(); } }
    public float VerticalOffset { get { return _vertOffset; } set { _vertOffset = value; UpdatePosition(); } }
    public string Placement { get { return _placement; } set { _placement = value == null ? "Bottom" : value; UpdatePosition(); } }

    internal void ForceCloseDisposed() {
        _isOpen = false;
        Visibility = Visibility.Collapsed;
        DetachOverlay();
        if (_logicalParent != null) {
            _logicalParent.UnregisterDetachedLogicalChild(this);
        }
    }

    public bool IsOpen {
        get { return _isOpen; }
        set {
            if (_isDisposedTree) {
                if (_isOpen) {
                    PopupService.Unregister(this);
                    ForceCloseDisposed();
                }
                return;
            }
            if (_isOpen == value) { return; }
            _isOpen = value;
            if (_isOpen) {
                AttachToOverlayIfNeeded();
                Visibility = Visibility.Visible;
                UpdatePosition();
                PopupService.RegisterOpen(this);
                Opened(this, EventArgs.Empty());
            } else {
                Visibility = Visibility.Collapsed;
                PopupService.Unregister(this);
                DetachOverlay();
                Closed(this, EventArgs.Empty());
            }
        }
    }

    private void AttachToOverlayIfNeeded() {
        UIElement anchor = _placementTarget != null
            ? _placementTarget
            : (TemplatedParent != null ? TemplatedParent : (_logicalParent != null ? _logicalParent : Parent));
        if (anchor != null) { _logicalParent = anchor; }
        UIElement overlay = PopupService.ResolveOverlayCanvas(anchor);
        UIElement self = this;
        if (overlay == null || overlay == self) { return; }
        if (Parent != overlay) {
            if (AmbientResources == null && Parent != null && Parent.AmbientResources != null) {
                AmbientResources = Parent.AmbientResources;
            }
            if (Parent != null) {
                Parent.RegisterDetachedLogicalChild(this);
                Parent.RemoveChild(this);
            }
            overlay.AddChild(this);
            _attachedRoot = overlay;
        }
        if (_logicalParent != null) {
            ReceiveInheritedDataContext(_logicalParent.EffectiveDataContextBox());
            ReceiveInheritedTypography(
                _logicalParent.EffectiveTextFontSizeOrZero(),
                _logicalParent.EffectiveTextFontFamilyOrEmpty(),
                _logicalParent.EffectiveTextFontWeightOrEmpty(),
                _logicalParent.EffectiveTextForeground());
        }
        BindingEngine.FlushSubtree(this);
    }

    internal override UIElement GetLogicalAncestorOverride() { return _logicalParent; }

    private void DetachOverlay() {
        if (_attachedRoot != null && Parent == _attachedRoot) {
            _attachedRoot.RemoveChild(this);
            _attachedRoot = null;
        }
    }

    public void UpdatePosition() {
        if (!_isOpen) { return; }
        UIElement anchor = _placementTarget != null ? _placementTarget : _logicalParent;
        if (anchor != null) {
            float vx = anchor.ViewportX;
            float vy = anchor.ViewportY;
            float vw = anchor.ScreenBoundsWidth;
            float vh = anchor.ScreenBoundsHeight;
            if (vw <= 0.0f && vh <= 0.0f) {
                vx = anchor.LayoutX;
                vy = anchor.LayoutY;
                vw = anchor.ActualWidth;
                vh = anchor.ActualHeight;
            }
            float scaleX = (anchor.ActualWidth > 0.5f && vw > 0.5f) ? (vw / anchor.ActualWidth) : 1.0f;
            float scaleY = (anchor.ActualHeight > 0.5f && vh > 0.5f) ? (vh / anchor.ActualHeight) : 1.0f;
            if (!ParseUtil.IsFinite(scaleX) || scaleX < 0.05f) { scaleX = 1.0f; }
            if (!ParseUtil.IsFinite(scaleY) || scaleY < 0.05f) { scaleY = 1.0f; }
            if (Math.Absf(scaleX - _appliedScaleX) > 0.005f || Math.Absf(scaleY - _appliedScaleY) > 0.005f) {
                _appliedScaleX = scaleX;
                _appliedScaleY = scaleY;
                RenderTransformOrigin = new Point(0.0f, 0.0f);
                if (Math.Absf(scaleX - 1.0f) > 0.005f || Math.Absf(scaleY - 1.0f) > 0.005f) {
                    RenderTransform = new ScaleTransform(scaleX, scaleY, 0.0f, 0.0f);
                } else {
                    RenderTransform = null;
                }
            }

            float rawW = DesiredWidth > 1.0f ? DesiredWidth : (ActualWidth > 1.0f ? ActualWidth : anchor.ActualWidth);
            float rawH = DesiredHeight > 1.0f ? DesiredHeight : (ActualHeight > 1.0f ? ActualHeight : 96.0f);
            float popW = rawW * scaleX;
            float popH = rawH * scaleY;

            float x = vx + _horizOffset * scaleX;
            float y = vy + _vertOffset * scaleY;
            if (_placement == "Bottom") {
                y = vy + vh + _vertOffset * scaleY;
            } else if (_placement == "Top") {
                y = vy - popH - _vertOffset * scaleY;
            } else if (_placement == "Right") {
                x = vx + vw + _horizOffset * scaleX;
            } else if (_placement == "Left") {
                x = vx - popW - _horizOffset * scaleX;
            } else if (_placement == "Center") {
                x = vx + (vw - popW) * 0.5f + _horizOffset * scaleX;
                y = vy + (vh - popH) * 0.5f + _vertOffset * scaleY;
            }

            UIElement root = PopupService.ResolveOverlayHost(anchor);
            float rootW = root != null ? root.ActualWidth : 0.0f;
            float rootH = root != null ? root.ActualHeight : 0.0f;
            float margin = 4.0f;
            if (_placement == "Bottom" && rootH > 0.0f && popH > 0.0f && (y + popH > rootH - margin)) {
                float aboveY = vy - popH - _vertOffset * scaleY;
                if (aboveY >= margin) {
                    y = aboveY;
                }
            } else if (_placement == "Top" && y < margin && rootH > 0.0f && popH > 0.0f) {
                float belowY = vy + vh + _vertOffset * scaleY;
                if (belowY + popH <= rootH - margin) {
                    y = belowY;
                }
            }
            if (_placement == "Right" && rootW > 0.0f && popW > 0.0f && (x + popW > rootW - margin)) {
                float leftX = vx - popW - _horizOffset * scaleX;
                if (leftX >= margin || vx > rootW * 0.5f || leftX > rootW - popW - x) {
                    x = leftX;
                }
            } else if (_placement == "Left" && x < margin && rootW > 0.0f && popW > 0.0f) {
                float rightX = vx + vw + _horizOffset * scaleX;
                if (rightX + popW <= rootW - margin) {
                    x = rightX;
                }
            }
            if (rootW > 0.0f && popW > 0.0f && x + popW > rootW - margin) {
                x = Math.Maxf(margin, rootW - popW - margin);
            }
            if (x < margin && rootW > popW + margin * 2.0f) {
                x = margin;
            } else if (x < 0.0f) {
                x = 0.0f;
            }
            if (rootH > 0.0f && popH > 0.0f && y + popH > rootH - margin) {
                y = Math.Maxf(margin, rootH - popH - margin);
            }
            if (y < margin && rootH > popH + margin * 2.0f) {
                y = margin;
            } else if (y < 0.0f) {
                y = 0.0f;
            }

            float curLeft = UIElement.GetCanvasLeft(this);
            float curTop = UIElement.GetCanvasTop(this);
            if (ParseUtil.IsNaN(curLeft) || Math.Absf(curLeft - x) > 0.1f) {
                UIElement.SetCanvasLeft(this, x);
            }
            if (ParseUtil.IsNaN(curTop) || Math.Absf(curTop - y) > 0.1f) {
                UIElement.SetCanvasTop(this, y);
            }
        } else {
            float curLeft = UIElement.GetCanvasLeft(this);
            float curTop = UIElement.GetCanvasTop(this);
            if (ParseUtil.IsNaN(curLeft) || Math.Absf(curLeft - _horizOffset) > 0.1f) {
                UIElement.SetCanvasLeft(this, _horizOffset);
            }
            if (ParseUtil.IsNaN(curTop) || Math.Absf(curTop - _vertOffset) > 0.1f) {
                UIElement.SetCanvasTop(this, _vertOffset);
            }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsOpen") { IsOpen = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "StaysOpen") { StaysOpen = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Placement") { Placement = DependencyObject.UnboxString(value, "Bottom"); return true; }
        if (propName == "HorizontalOffset") { HorizontalOffset = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "VerticalOffset") { VerticalOffset = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Child" && value is UIElement) { Child = value as UIElement; return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsOpen") { return DependencyObject.BoxBool(_isOpen); }
        if (propName == "StaysOpen") { return DependencyObject.BoxBool(_staysOpen); }
        if (propName == "Placement") { return DependencyObject.BoxString(_placement); }
        if (propName == "HorizontalOffset") { return DependencyObject.BoxFloat(_horizOffset); }
        if (propName == "VerticalOffset") { return DependencyObject.BoxFloat(_vertOffset); }
        if (propName == "Child") { return Child; }
        return base.GetPropertyValue(propName);
    }
}

public class ToolTip : ContentControl {
    public ToolTip() : base() {
        Background = new SolidColorBrush("#FF0F172A");
        Foreground = new SolidColorBrush("#FFF1F5F9");
        BorderBrush = new SolidColorBrush("#FF3B465C");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(10.0f, 6.0f, 10.0f, 6.0f);
        FontSize = 12.0f;
        Effect = new DropShadowEffect(8.0f, 3.0f, 315.0f, "#FF000000", 0.5f);
    }
    public ToolTip(string text) : this() {
        SetContent(text);
    }
    public override string GetTypeName() { return "ToolTip"; }
}

public static class ToolTipService {
    private static Popup _activeTipPopup;

    public static void ShowFor(UIElement target) {
        if (target == null || !target.HasToolTip) { return; }
        DependencyObject tipBox = target.ToolTipBox;
        if (tipBox == null) { return; }
        Hide();
        _activeTipPopup = new Popup();
        _activeTipPopup.PlacementTarget = target;
        _activeTipPopup.Placement = "Bottom";
        _activeTipPopup.VerticalOffset = 4.0f;
        _activeTipPopup.IsHitTestVisible = false;

        ToolTip tip = null;
        if (tipBox is ToolTip) {
            tip = tipBox as ToolTip;
        } else {
            tip = new ToolTip();
            tip.SetContentBox(tipBox);
        }
        _activeTipPopup.Child = tip;
        _activeTipPopup.IsOpen = true;
    }

    public static void Hide() {
        if (_activeTipPopup != null) {
            _activeTipPopup.IsOpen = false;
            _activeTipPopup = null;
        }
    }
}

public partial class UIElement {
    private ContextMenu _contextMenu;
    private bool _toolTipHooked;

    public ContextMenu ContextMenu {
        get { return _contextMenu; }
        set {
            _contextMenu = value;
            if (_contextMenu != null) {
                MouseRightButtonUp += (object s, MouseButtonEventArgs e) => {
                    if (!e.Handled && _contextMenu != null) {
                        _contextMenu.Open(this, e.X, e.Y);
                        e.Handled = true;
                    }
                };
            }
        }
    }

    public void EnableToolTipHover() {
        if (_toolTipHooked) { return; }
        _toolTipHooked = true;
        MouseEnter += (object s, MouseEventArgs e) => {
            if (HasToolTip) { ToolTipService.ShowFor(this); }
        };
        MouseLeave += (object s, MouseEventArgs e) => {
            ToolTipService.Hide();
        };
    }
}

// ============================================================================
// 2. HeaderedItemsControl, MenuItem, MenuBase, Menu, ContextMenu, StatusBar, ToolBar
// ============================================================================

public class HeaderedItemsControl : ItemsControl {
    private DependencyObject _headerBox;
    private DataTemplate _headerTemplate;
    protected ContentPresenter _headerPresenter;

    public HeaderedItemsControl() : base() {}
    public override string GetTypeName() { return "HeaderedItemsControl"; }

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
    public void SetHeader(int v) { SetHeaderBox(DependencyObject.BoxInt(v)); }
    public void SetHeader(float v) { SetHeaderBox(DependencyObject.BoxFloat(v)); }
    public void SetHeader(object v) { SetHeaderBox(DependencyObject.BoxObject(v)); }

    public void SetHeaderBox(DependencyObject box) {
        DependencyObject oldBox = _headerBox;
        _headerBox = box;
        if (_headerPresenter != null) { _headerPresenter.SetContentBox(box); }
        OnHeaderChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(box));
    }

    public DataTemplate HeaderTemplate {
        get { return _headerTemplate; }
        set {
            _headerTemplate = value;
            if (_headerPresenter != null) { _headerPresenter.ContentTemplate = value; }
        }
    }

    public void SetHeaderAndTemplate(DependencyObject box, DataTemplate tpl) {
        DependencyObject oldBox = _headerBox;
        _headerTemplate = tpl;
        _headerBox = box;
        if (_headerPresenter != null) {
            _headerPresenter.SetContentAndTemplate(_headerBox, _headerTemplate);
        }
        OnHeaderChanged(DependencyObject.Unbox(oldBox), DependencyObject.Unbox(_headerBox));
    }

    protected virtual void OnHeaderChanged(object oldHeader, object newHeader) {}

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        if (Template == null) { return; }
        _headerPresenter = null;
        UIElement hFound = GetTemplateChild("PART_HeaderPresenter");
        if (!(hFound is ContentPresenter)) { hFound = GetTemplateChild("PART_Header"); }
        if (!(hFound is ContentPresenter)) { hFound = GetTemplateChild("HeaderHost"); }
        if (hFound is ContentPresenter) {
            _headerPresenter = hFound as ContentPresenter;
        } else {
            _headerPresenter = ContentControl.FindFirstTemplatedPresenter(TemplateRoot(), this, true);
        }
        if (_headerPresenter != null) {
            if (_headerPresenter.TemplatedParent == null) {
                _headerPresenter.TemplatedParent = this;
                _headerPresenter.SyncFromTemplatedParent(this);
            }
            _headerPresenter.SetContentAndTemplate(_headerBox, _headerTemplate);
        }
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

public class MenuItem : HeaderedItemsControl {
    private ICommand _command;
    private DependencyObject _commandParamBox;
    private DependencyObject _iconBox;
    private string _inputGestureText = "";
    private bool _isCheckable = false;
    private bool _isChecked = false;
    private bool _isSubmenuOpen = false;
    private bool _isTopLevel = false;

    private Popup _submenuPopup;
    private TextBlock _checkGlyph;
    private TextBlock _gestureText;
    private TextBlock _arrowGlyph;

    public event RoutedEventHandler Click;
    public event RoutedEventHandler Checked;
    public event RoutedEventHandler Unchecked;
    public event RoutedEventHandler SubmenuOpened;
    public event RoutedEventHandler SubmenuClosed;

    public MenuItem() : base() {
        Focusable = true;
        Padding = new Thickness(10.0f, 6.0f, 10.0f, 6.0f);
        CornerRadius = new CornerRadius(4.0f);
        HorizontalAlignment = HorizontalAlignment.Stretch;
    }
    public MenuItem(string headerText) : this() {
        SetHeader(headerText);
    }

    public override string GetTypeName() { return "MenuItem"; }

    public ICommand Command {
        get { return _command; }
        set { _command = value; SyncCommandCanExecute(); }
    }
    public object CommandParameter {
        get { return DependencyObject.Unbox(_commandParamBox); }
        set { _commandParamBox = DependencyObject.BoxObject(value); SyncCommandCanExecute(); }
    }
    public DependencyObject CommandParameterBox {
        get { return _commandParamBox; }
        set { _commandParamBox = value; SyncCommandCanExecute(); }
    }
    public string InputGestureText {
        get { return _inputGestureText; }
        set {
            _inputGestureText = value == null ? "" : value;
            if (_gestureText != null) { _gestureText.Text = _inputGestureText; }
        }
    }
    public override DependencyObject GetNamedContentBox(string source) {
        if (source == "Icon") { return _iconBox; }
        return base.GetNamedContentBox(source);
    }
    public override bool HasNamedContentSlot(string source) {
        if (source == "Icon") { return true; }
        return base.HasNamedContentSlot(source);
    }

    public string Role {
        get {
            bool hasKids = ItemCount() > 0;
            if (_isTopLevel) {
                return hasKids ? "TopLevelHeader" : "TopLevelItem";
            }
            return hasKids ? "SubmenuHeader" : "SubmenuItem";
        }
    }

    public bool IsHighlighted {
        get { return IsMouseOver || _isSubmenuOpen; }
    }

    public object Icon {
        get { return DependencyObject.Unbox(_iconBox); }
        set { _iconBox = DependencyObject.BoxObject(value); SyncIconPresenter(); UpdateTriggersAndStates(); }
    }
    public bool IsCheckable {
        get { return _isCheckable; }
        set { _isCheckable = value; SyncMenuVisuals(); UpdateTriggersAndStates(); }
    }
    public bool IsChecked {
        get { return _isChecked; }
        set {
            if (_isChecked == value) { return; }
            _isChecked = value;
            SyncMenuVisuals();
            UpdateTriggersAndStates();
            if (_isChecked) { Checked(this, new RoutedEventArgs("Checked", this)); }
            else { Unchecked(this, new RoutedEventArgs("Unchecked", this)); }
        }
    }
    public bool IsTopLevel {
        get { return _isTopLevel; }
        set {
            if (_isTopLevel == value) { return; }
            _isTopLevel = value;
            SyncMenuVisuals();
            UpdateTriggersAndStates();
        }
    }

    public bool IsSubmenuOpen {
        get { return _isSubmenuOpen; }
        set {
            if (_isSubmenuOpen == value) { return; }
            _isSubmenuOpen = value;
            if (_submenuPopup != null) {
                _submenuPopup.IsOpen = _isSubmenuOpen && ItemCount() > 0;
            }
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("IsSubmenuOpen", DependencyObject.BoxBool(_isSubmenuOpen));
            if (_isSubmenuOpen) { SubmenuOpened(this, new RoutedEventArgs("SubmenuOpened", this)); }
            else { SubmenuClosed(this, new RoutedEventArgs("SubmenuClosed", this)); }
        }
    }

    private static Popup FindFirstTemplatedPopup(UIElement rootNode, UIElement owner) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur is Popup && cur.TemplatedParent == owner) { return cur as Popup; }
            if (cur is ContentPresenter && cur.TemplatedParent == owner) { continue; }
            for (int i = 0; i < cur.ChildCount(); i += 1) {
                UIElement ch = cur.ChildAt(i);
                if (ch != null && Control.IsOwnedByTemplateChain(ch, owner)) { q.Push(ch); }
            }
        }
        return null;
    }

    private void SyncIconPresenter() {
        if (Template == null) { return; }
        UIElement iconHost = GetTemplateChild("IconHost");
        if (!(iconHost is ContentPresenter)) { iconHost = GetTemplateChild("Icon"); }
        if (iconHost is ContentPresenter) {
            (iconHost as ContentPresenter).SetContentBox(_iconBox);
        }
    }

    private void HandleSubmenuPopupClosed() {
        if (_isSubmenuOpen) {
            _isSubmenuOpen = false;
            NotifyBoundPropertyChanged("IsSubmenuOpen", DependencyObject.BoxBool(false));
            UpdateTriggersAndStates();
            SubmenuClosed(this, new RoutedEventArgs("SubmenuClosed", this));
        }
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        if (Template != null) {
            _checkGlyph = null;
            _gestureText = null;
            _arrowGlyph = null;
            _submenuPopup = null;
            UIElement pop = GetTemplateChild("PART_Popup");
            if (!(pop is Popup)) { pop = FindFirstTemplatedPopup(TemplateRoot(), this); }
            if (pop is Popup) {
                _submenuPopup = pop as Popup;
                _submenuPopup.PlacementTarget = this;
                _submenuPopup.IsOpen = _isSubmenuOpen && ItemCount() > 0;
                _submenuPopup.Closed += (object s, EventArgs e) => { HandleSubmenuPopupClosed(); };
            }
            SyncIconPresenter();
        }
    }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is Separator && container.Style == null) {
            DependencyObject stBox = FindResourceBox("MenuItem.SeparatorStyleKey");
            if (stBox is Style) { container.Style = stBox as Style; }
        }
    }

    private void SyncCommandCanExecute() {
        if (_command != null) {
            IsEnabled = CommandTriggerHelper.CanExecuteBox(_command, _commandParamBox);
        }
    }

    private void SyncMenuVisuals() {
        if (_checkGlyph != null) {
            _checkGlyph.Visibility = _isCheckable ? Visibility.Visible : Visibility.Collapsed;
            _checkGlyph.Text = _isChecked ? "✓" : " ";
        }
        if (_arrowGlyph != null) {
            _arrowGlyph.Visibility = (!_isTopLevel && ItemCount() > 0) ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        if (Style == null && Template == null) {
            Background = new SolidColorBrush(IsMouseOver || _isSubmenuOpen ? "#FF2D3548" : "#00000000");
        }
    }

    public override void RefreshItems() {
        base.RefreshItems();
        SyncMenuVisuals();
        UpdateTriggersAndStates();
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (!IsEnabled) { return; }
        e.Handled = true;
        if (ItemCount() > 0) {
            IsSubmenuOpen = !IsSubmenuOpen;
            return;
        }
        if (_isCheckable) { IsChecked = !IsChecked; }
        Click(this, new RoutedEventArgs("Click", this));
        CommandTriggerHelper.ExecuteBox(_command, _commandParamBox);
        PopupService.CloseAll();
    }

    protected override UIElement CreateDefaultTemplate() {
        var grid = new Grid();
        grid.AddColumnDefinition(GridUnitType.Auto, 1.0f);
        grid.AddColumnDefinition(GridUnitType.Star, 1.0f);
        grid.AddColumnDefinition(GridUnitType.Auto, 1.0f);
        grid.AddColumnDefinition(GridUnitType.Auto, 1.0f);

        _checkGlyph = new TextBlock(_isChecked ? "✓" : " ");
        _checkGlyph.Width = 18.0f;
        _checkGlyph.FontSize = 13.0f;
        _checkGlyph.Foreground = new SolidColorBrush("#FF60A5FA");
        _checkGlyph.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_checkGlyph, 0);

        _headerPresenter = new ContentPresenter();
        _headerPresenter.Name = "PART_HeaderPresenter";
        _headerPresenter.ContentTemplate = HeaderTemplate;
        _headerPresenter.SetContentBox(HeaderBox);
        _headerPresenter.HorizontalAlignment = HorizontalAlignment.Left;
        _headerPresenter.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_headerPresenter, 1);

        _gestureText = new TextBlock(_inputGestureText);
        _gestureText.FontSize = 12.0f;
        _gestureText.Foreground = new SolidColorBrush("#FF94A3B8");
        _gestureText.Margin = new Thickness(16.0f, 0.0f, 6.0f, 0.0f);
        _gestureText.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_gestureText, 2);

        _arrowGlyph = new TextBlock("▸");
        _arrowGlyph.FontSize = 12.0f;
        _arrowGlyph.Foreground = new SolidColorBrush("#FF94A3B8");
        _arrowGlyph.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_arrowGlyph, 3);

        _submenuPopup = new Popup();
        _submenuPopup.PlacementTarget = this;
        _submenuPopup.Placement = _isTopLevel ? "Bottom" : "Right";
        _submenuPopup.Closed += (object s, EventArgs e) => { _isSubmenuOpen = false; OnVisualStatePropertyChanged(); };

        var subCard = new Border();
        subCard.Background = new SolidColorBrush("#FF1B202B");
        subCard.BorderBrush = new SolidColorBrush("#FF364054");
        subCard.BorderThickness = new Thickness(1.0f);
        subCard.CornerRadius = new CornerRadius(6.0f);
        subCard.Padding = new Thickness(4.0f);
        subCard.MinWidth = 170.0f;
        subCard.Effect = new DropShadowEffect(10.0f, 4.0f, 315.0f, "#FF000000", 0.5f);

        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.EnsurePanel();
        subCard.Child = _itemsPresenter;
        _submenuPopup.Child = subCard;

        grid.AddChild(_checkGlyph);
        grid.AddChild(_headerPresenter);
        grid.AddChild(_gestureText);
        grid.AddChild(_arrowGlyph);
        grid.AddChild(_submenuPopup);
        RefreshItems();
        return grid;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Command" && value is ICommand) { Command = value as ICommand; return true; }
        if (propName == "CommandParameter") { CommandParameterBox = value; return true; }
        if (propName == "InputGestureText") { InputGestureText = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Icon") { _iconBox = value; SyncIconPresenter(); UpdateTriggersAndStates(); return true; }
        if (propName == "IsCheckable") { IsCheckable = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsChecked") { IsChecked = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsSubmenuOpen") { IsSubmenuOpen = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Role") { return DependencyObject.BoxString(Role); }
        if (propName == "IsHighlighted") { return DependencyObject.BoxBool(IsHighlighted); }
        if (propName == "IsSubmenuOpen") { return DependencyObject.BoxBool(_isSubmenuOpen); }
        if (propName == "IsCheckable") { return DependencyObject.BoxBool(_isCheckable); }
        if (propName == "IsChecked") { return DependencyObject.BoxBool(_isChecked); }
        if (propName == "InputGestureText") { return DependencyObject.BoxString(_inputGestureText); }
        if (propName == "Icon") { return _iconBox; }
        if (propName == "HasItems") { return DependencyObject.BoxBool(ItemCount() > 0); }
        return base.GetPropertyValue(propName);
    }
}

public class MenuBase : ItemsControl {
    public MenuBase() : base() {
        Background = new SolidColorBrush("#FF181C24");
        BorderBrush = new SolidColorBrush("#FF2E3748");
    }
    public override string GetTypeName() { return "MenuBase"; }
}

public class Menu : MenuBase {
    public Menu() : base() {
        BorderThickness = new Thickness(0.0f, 0.0f, 0.0f, 1.0f);
        Padding = new Thickness(6.0f, 2.0f, 6.0f, 2.0f);
        ItemsPanel = () => { return new StackPanel(Orientation.Horizontal, 2.0f); };
    }
    public override string GetTypeName() { return "Menu"; }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is MenuItem) { (container as MenuItem).IsTopLevel = true; }
    }
}

public class ContextMenu : MenuBase {
    private Popup _popup;

    public event RoutedEventHandler Opened;
    public event RoutedEventHandler Closed;

    public ContextMenu() : base() {
        Background = new SolidColorBrush("#FF1B202B");
        BorderBrush = new SolidColorBrush("#FF3B465C");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(4.0f);
        MinWidth = 170.0f;
        Effect = new DropShadowEffect(12.0f, 4.0f, 315.0f, "#FF000000", 0.55f);

        _popup = new Popup();
        _popup.StaysOpen = false;
        _popup.Child = this;
        _popup.Opened += (object s, EventArgs e) => { RaiseOpened(); };
        _popup.Closed += (object s, EventArgs e) => { RaiseClosed(); };
    }

    private void RaiseOpened() { Opened(this, new RoutedEventArgs("Opened", this)); }
    private void RaiseClosed() { Closed(this, new RoutedEventArgs("Closed", this)); }

    public override string GetTypeName() { return "ContextMenu"; }
    public bool IsOpen {
        get { return _popup.IsOpen; }
        set { _popup.IsOpen = value; }
    }

    public void Open(UIElement placementTarget = null, float x = 0.0f, float y = 0.0f) {
        _popup.PlacementTarget = placementTarget;
        _popup.Placement = placementTarget != null ? "Bottom" : "Absolute";
        _popup.HorizontalOffset = x;
        _popup.VerticalOffset = y;
        _popup.IsOpen = true;
    }

    public void Close() { _popup.IsOpen = false; }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is Separator && container.Style == null) {
            DependencyObject stBox = FindResourceBox("MenuItem.SeparatorStyleKey");
            if (stBox is Style) { container.Style = stBox as Style; }
        }
    }
}

public class StatusBarItem : ContentControl {
    public StatusBarItem() : base() {
        HorizontalContentAlignment = HorizontalAlignment.Left;
        VerticalContentAlignment = VerticalAlignment.Center;
        Padding = new Thickness(8.0f, 3.0f, 8.0f, 3.0f);
        Foreground = new SolidColorBrush("#FF94A3B8");
        FontSize = 12.0f;
    }
    public override string GetTypeName() { return "StatusBarItem"; }
}

public class StatusBar : ItemsControl {
    public StatusBar() : base() {
        Background = new SolidColorBrush("#FF141821");
        BorderBrush = new SolidColorBrush("#FF2E3748");
        BorderThickness = new Thickness(0.0f, 1.0f, 0.0f, 0.0f);
        Padding = new Thickness(8.0f, 3.0f, 8.0f, 3.0f);
        ItemsPanel = () => {
            var dp = new DockPanel();
            dp.LastChildFill = false;
            return dp;
        };
    }
    public override string GetTypeName() { return "StatusBar"; }

    protected override UIElement CreateContainerForItem() { return new StatusBarItem(); }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is Separator && container.Style == null) {
            DependencyObject stBox = FindResourceBox("StatusBar.SeparatorStyleKey");
            if (stBox is Style) { container.Style = stBox as Style; }
        }
    }
}

public class ToolBar : HeaderedItemsControl {
    private Orientation _orientation = Orientation.Horizontal;
    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; UpdateTriggersAndStates(); }
    }

    public ToolBar() : base() {
        Background = new SolidColorBrush("#FF1B202B");
        BorderBrush = new SolidColorBrush("#FF2E3748");
        BorderThickness = new Thickness(0.0f, 0.0f, 0.0f, 1.0f);
        Padding = new Thickness(8.0f, 4.0f, 8.0f, 4.0f);
        ItemsPanel = () => { return new StackPanel(_orientation, 2.0f); };
    }
    public override string GetTypeName() { return "ToolBar"; }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container != null && container.Style == null) {
            string styleKey = "";
            if (container is CheckBox) { styleKey = "ToolBar.CheckBoxStyleKey"; }
            else if (container is RadioButton) { styleKey = "ToolBar.RadioButtonStyleKey"; }
            else if (container is ToggleButton) { styleKey = "ToolBar.ToggleButtonStyleKey"; }
            else if (container is Button) { styleKey = "ToolBar.ButtonStyleKey"; }
            else if (container is ComboBox) { styleKey = "ToolBar.ComboBoxStyleKey"; }
            else if (container is TextBox) { styleKey = "ToolBar.TextBoxStyleKey"; }
            else if (container is Separator) { styleKey = "ToolBar.SeparatorStyleKey"; }
            else if (container is Menu) { styleKey = "ToolBar.MenuStyleKey"; }
            if (styleKey.Length > 0) {
                DependencyObject stBox = FindResourceBox(styleKey);
                if (stBox is Style) { container.Style = stBox as Style; }
            }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Orientation") {
            if (value is BoxedInt) {
                Orientation = (value as BoxedInt).Value == 1 ? Orientation.Vertical : Orientation.Horizontal;
                return true;
            }
            string ov = DependencyObject.UnboxString(value, "Horizontal");
            Orientation = ov == "Vertical" ? Orientation.Vertical : Orientation.Horizontal;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        if (propName == "HasOverflowItems") { return DependencyObject.BoxBool(false); }
        return base.GetPropertyValue(propName);
    }

    protected override UIElement CreateDefaultTemplate() {
        var row = new StackPanel(Orientation, 8.0f);
        _headerPresenter = new ContentPresenter();
        _headerPresenter.SetContentBox(HeaderBox);
        _headerPresenter.VerticalAlignment = VerticalAlignment.Center;
        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.PanelFactory = ItemsPanelFactory();
        _itemsPresenter.EnsurePanel();
        row.AddChild(_headerPresenter);
        row.AddChild(_itemsPresenter);
        RefreshItems();
        return row;
    }
}

// ============================================================================
// 3. TreeView & TreeViewItem
// ============================================================================

internal class ArrayCollectionSlot : CollectionSlotBase {
    public ArrayCollectionBox Target;
    public ArrayCollectionSlot(ArrayCollectionBox target) : base() { Target = target; }
    public override bool Matches(object col) {
        var o = col as ArrayCollectionBox;
        return o == Target;
    }
}

internal class ArrayCollectionBox : DependencyObject, ICollectionNotifier {
    private DependencyObject[] _items;
    private CollectionSlotBase _notifierSlot;
    public ArrayCollectionBox() { _items = new DependencyObject[0]; }
    public ArrayCollectionBox(DependencyObject[] items) {
        _items = items == null ? new DependencyObject[0] : items;
    }
    public override object UnboxObject() { return this; }
    public CollectionSlotBase EnsureNotifierSlot() {
        if (_notifierSlot == null) {
            _notifierSlot = CollectionChangeHub.FindOrRegister(new ArrayCollectionSlot(this), this);
        }
        return _notifierSlot;
    }
    public void AddBox(DependencyObject b) { _items.Push(b); }
    public int ItemCount() { return _items.Length; }
    public DependencyObject BoxItemAt(int index) {
        if (index < 0 || index >= _items.Length) { return null; }
        return _items[index];
    }
}

public class TreeViewItem : HeaderedItemsControl {
    private bool _isExpanded = false;
    private bool _isSelected = false;
    private TreeView _ownerTree;

    private Border _rowHost;
    private TextBlock _expanderGlyph;
    private StackPanel _childrenHost;

    public event RoutedEventHandler Expanded;
    public event RoutedEventHandler Collapsed;
    public event RoutedEventHandler Selected;
    public event RoutedEventHandler Unselected;

    public TreeViewItem() : base() {
        Focusable = true;
        HorizontalAlignment = HorizontalAlignment.Stretch;
    }
    public TreeViewItem(string headerText) : this() {
        SetHeader(headerText);
    }

    public override string GetTypeName() { return "TreeViewItem"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "Expanded") {
            Expanded += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    public TreeView OwnerTree {
        get { return _ownerTree; }
        set {
            _ownerTree = value;
            PropagateOwnerToChildren();
        }
    }

    public bool IsExpanded {
        get { return _isExpanded; }
        set {
            if (_isExpanded == value) { return; }
            _isExpanded = value;
            SyncExpandedState();
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("IsExpanded", DependencyObject.BoxBool(_isExpanded));
            if (_isExpanded) { Expanded(this, new RoutedEventArgs("Expanded", this)); }
            else { Collapsed(this, new RoutedEventArgs("Collapsed", this)); }
        }
    }

    public bool IsSelected {
        get { return _isSelected; }
        set {
            if (_isSelected == value) { return; }
            _isSelected = value;
            SyncSelectedVisual();
            UpdateTriggersAndStates();
            NotifyBoundPropertyChanged("IsSelected", DependencyObject.BoxBool(_isSelected));
            if (_isSelected) { Selected(this, new RoutedEventArgs("Selected", this)); }
            else { Unselected(this, new RoutedEventArgs("Unselected", this)); }
        }
    }

    internal void SetSelectedSilent(bool sel) {
        if (_isSelected == sel) { return; }
        _isSelected = sel;
        SyncSelectedVisual();
        UpdateTriggersAndStates();
        NotifyBoundPropertyChanged("IsSelected", DependencyObject.BoxBool(_isSelected));
    }

    private TreeView ResolveOwnerTree() {
        if (_ownerTree != null) { return _ownerTree; }
        UIElement cur = Parent;
        while (cur != null) {
            if (cur is TreeView) {
                _ownerTree = cur as TreeView;
                return _ownerTree;
            }
            cur = cur.Parent;
        }
        return null;
    }

    private static TreeViewItem ClosestTreeViewItemFrom(object src) {
        UIElement cur = src as UIElement;
        while (cur != null) {
            if (cur is TreeViewItem) { return cur as TreeViewItem; }
            cur = cur.Parent;
        }
        return null;
    }

    private void SelectSelf() {
        TreeView tree = ResolveOwnerTree();
        if (tree != null) {
            tree.SelectTreeItem(this);
        } else {
            IsSelected = true;
        }
    }

    public override void OnPreviewMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnPreviewMouseLeftButtonDown(e);
        if (!IsEnabled || e == null) { return; }
        object orig = e.OriginalSource != null ? e.OriginalSource : e.Source;
        TreeViewItem closest = ClosestTreeViewItemFrom(orig);
        if (closest == null || closest == this) {
            SelectSelf();
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled || e == null || e.Handled) { return; }
        object orig = e.OriginalSource != null ? e.OriginalSource : e.Source;
        TreeViewItem closest = ClosestTreeViewItemFrom(orig);
        if (closest != null && closest != this) { return; }
        SelectSelf();
        if (ItemCount() > 0 && (_rowHost == null || e.ClickCount >= 2)) {
            IsExpanded = !IsExpanded;
        }
        e.Handled = true;
    }

    private void SyncExpandedState() {
        bool hasKids = ItemCount() > 0;
        if (_expanderGlyph != null) {
            _expanderGlyph.Text = hasKids ? (_isExpanded ? "▾" : "▸") : " ";
        }
        if (_childrenHost != null) {
            _childrenHost.Visibility = (_isExpanded && hasKids) ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private void SyncSelectedVisual() {
        if (_rowHost == null) { return; }
        if (_isSelected) {
            _rowHost.Background = new SolidColorBrush("#FF2563EB");
        } else if (IsMouseOver) {
            _rowHost.Background = new SolidColorBrush("#FF2D3548");
        } else {
            _rowHost.Background = Brushes.Transparent();
        }
    }

    private void PropagateOwnerToChildren() {
        for (int i = 0; i < _activeContainers.Length; i += 1) {
            if (_activeContainers[i] is TreeViewItem) {
                (_activeContainers[i] as TreeViewItem).OwnerTree = _ownerTree;
            }
        }
    }

    protected override UIElement CreateContainerForItem() {
        var tvi = new TreeViewItem();
        tvi.OwnerTree = ResolveOwnerTree();
        return tvi;
    }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is TreeViewItem) {
            TreeViewItem tvi = container as TreeViewItem;
            TreeView tree = ResolveOwnerTree();
            if (tree != null) { tvi.OwnerTree = tree; }
        }
    }

    protected override void PrepareContainerForItem(UIElement container, DependencyObject itemBox, int index) {
        if (container is TreeViewItem) {
            TreeView.PrepareTreeItemContainer(this, container as TreeViewItem, itemBox, ResolveOwnerTree());
        } else {
            base.PrepareContainerForItem(container, itemBox, index);
        }
    }

    public override void RefreshItems() {
        base.RefreshItems();
        PropagateOwnerToChildren();
        SyncExpandedState();
        UpdateTriggersAndStates();
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        SyncSelectedVisual();
    }

    protected override UIElement CreateDefaultTemplate() {
        var root = new StackPanel(Orientation.Vertical, 2.0f);

        _rowHost = new Border();
        _rowHost.CornerRadius = new CornerRadius(4.0f);
        _rowHost.Padding = new Thickness(6.0f, 4.0f, 6.0f, 4.0f);

        var row = new StackPanel(Orientation.Horizontal, 6.0f);
        _expanderGlyph = new TextBlock(" ");
        _expanderGlyph.Width = 14.0f;
        _expanderGlyph.FontSize = 12.0f;
        _expanderGlyph.Foreground = new SolidColorBrush("#FF94A3B8");
        _expanderGlyph.VerticalAlignment = VerticalAlignment.Center;
        _expanderGlyph.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            if (ItemCount() > 0) { IsExpanded = !IsExpanded; e.Handled = true; }
        };

        _headerPresenter = new ContentPresenter();
        _headerPresenter.Name = "PART_HeaderPresenter";
        _headerPresenter.ContentTemplate = HeaderTemplate;
        _headerPresenter.SetContentBox(HeaderBox);
        _headerPresenter.VerticalAlignment = VerticalAlignment.Center;

        row.AddChild(_expanderGlyph);
        row.AddChild(_headerPresenter);
        _rowHost.Child = row;
        _rowHost.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            SelectSelf();
            if (e.ClickCount >= 2 && ItemCount() > 0) {
                IsExpanded = !IsExpanded;
            }
            e.Handled = true;
        };

        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.PanelFactory = () => { return new StackPanel(Orientation.Vertical, 2.0f); };
        _childrenHost = _itemsPresenter.EnsurePanel() as StackPanel;
        _childrenHost.Margin = new Thickness(18.0f, 2.0f, 0.0f, 0.0f);

        root.AddChild(_rowHost);
        root.AddChild(_itemsPresenter);
        RefreshItems();
        SyncSelectedVisual();
        return root;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsExpanded") { IsExpanded = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsSelected") { IsSelected = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsExpanded") { return DependencyObject.BoxBool(_isExpanded); }
        if (propName == "IsSelected") { return DependencyObject.BoxBool(_isSelected); }
        return base.GetPropertyValue(propName);
    }
}

public class TreeView : ItemsControl {
    private TreeViewItem _selectedItem;

    public event EventHandler SelectedItemChanged;

    public TreeView() : base() {
        Background = new SolidColorBrush("#FF171B24");
        BorderBrush = new SolidColorBrush("#FF364054");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(6.0f);
    }

    public override string GetTypeName() { return "TreeView"; }
    public TreeViewItem SelectedContainer() { return _selectedItem; }
    public object SelectedItem {
        get {
            if (_selectedItem == null) { return null; }
            if (_selectedItem.Header != null) { return _selectedItem.Header; }
            return _selectedItem;
        }
    }

    public void SelectTreeItem(TreeViewItem item) {
        if (_selectedItem == item) { return; }
        if (_selectedItem != null) { _selectedItem.SetSelectedSilent(false); }
        _selectedItem = item;
        if (_selectedItem != null) { _selectedItem.IsSelected = true; }
        SelectedItemChanged(this, EventArgs.Empty());
    }

    private static string InferDataTypeKey(object raw) {
        return ItemsControl.InferImplicitDataTypeKey(raw);
    }

    private static DataTemplate ResolveDataTemplateForItem(
        ItemsControl owner, TreeViewItem tvi, DataTemplate explicitTemplate, object raw) {
        if (explicitTemplate != null) { return explicitTemplate; }
        string dtKey = InferDataTypeKey(raw);
        if (dtKey.Length > 0) {
            DependencyObject res = tvi != null ? tvi.FindResourceBox(dtKey) : null;
            if (res == null && owner != null) { res = owner.FindResourceBox(dtKey); }
            if (res is DataTemplate) { return res as DataTemplate; }
        }
        return null;
    }

    private static DependencyObject ReadHierarchicalItemsSource(object raw, string path) {
        if (raw == null || path == null || path.Length == 0) { return null; }
        DependencyObject subSrc = BindingEngine.ReadPathBox(raw, path);
        if (subSrc != null) {
            object unboxed = DependencyObject.Unbox(subSrc);
            if (subSrc is ICollectionNotifier || unboxed is ICollectionNotifier) {
                return subSrc;
            }
        }
        if (raw is TransformGroup && path == "Children") {
            Transform[] arr = (raw as TransformGroup).Children();
            var box = new ArrayCollectionBox();
            if (arr != null) {
                for (int i = 0; i < arr.Length; i += 1) { box.AddBox(arr[i]); }
            }
            return box;
        }
        if (raw is UIElement && (path == "Children" || path == "LogicalChildren")) {
            UIElement[] arr = (raw as UIElement).Children();
            var box = new ArrayCollectionBox();
            if (arr != null) {
                for (int i = 0; i < arr.Length; i += 1) { box.AddBox(arr[i]); }
            }
            return box;
        }
        if (raw is ItemsControl && path == "Items") {
            ItemsControl ic = raw as ItemsControl;
            var box = new ArrayCollectionBox();
            int n = ic.ItemCount();
            for (int i = 0; i < n; i += 1) { box.AddBox(ic.GetItemBoxAt(i)); }
            return box;
        }
        return subSrc;
    }

    internal static void PrepareTreeItemContainer(
        ItemsControl owner, TreeViewItem tvi, DependencyObject itemBox, TreeView tree) {
        if (tvi == null) { return; }
        tvi.OwnerTree = tree;
        tvi.DataContextBox = itemBox;
        if (owner != null && owner.ItemContainerStyle != null && tvi.ItemContainerStyle == null) {
            tvi.ItemContainerStyle = owner.ItemContainerStyle;
        }
        if (owner != null && owner.ItemContainerStyle != null && tvi.Style == null) {
            tvi.Style = owner.ItemContainerStyle;
        }

        object raw = DependencyObject.Unbox(itemBox);
        DataTemplate itemTpl = ResolveDataTemplateForItem(
            owner, tvi, owner != null ? owner.ItemTemplate : null, raw);
        tvi.SetHeaderAndTemplate(itemBox, itemTpl);

        if (itemTpl is HierarchicalDataTemplate) {
            HierarchicalDataTemplate hdt = itemTpl as HierarchicalDataTemplate;
            DataTemplate childTpl = hdt.ItemTemplate;
            if (childTpl == null && hdt.ItemTemplateKey != null && hdt.ItemTemplateKey.Length > 0) {
                DependencyObject tplBox = tvi.FindResourceBox(hdt.ItemTemplateKey);
                if (tplBox == null && owner != null) { tplBox = owner.FindResourceBox(hdt.ItemTemplateKey); }
                if (tplBox is DataTemplate) { childTpl = tplBox as DataTemplate; }
            }
            if (childTpl == null && (hdt.DataType == null || hdt.DataType.Length == 0)) {
                childTpl = hdt;
            }
            tvi.ItemTemplate = childTpl;

            if (raw != null && hdt.ItemsSourcePath.Length > 0) {
                DependencyObject subSrc = ReadHierarchicalItemsSource(raw, hdt.ItemsSourcePath);
                if (subSrc != null) { tvi.SetItemsSourceBox(subSrc); }
            }
        }
    }

    protected override UIElement CreateContainerForItem() {
        var tvi = new TreeViewItem();
        tvi.OwnerTree = this;
        return tvi;
    }

    protected override void PrepareOwnContainer(UIElement container, int index) {
        base.PrepareOwnContainer(container, index);
        if (container is TreeViewItem) { (container as TreeViewItem).OwnerTree = this; }
    }

    protected override void PrepareContainerForItem(UIElement container, DependencyObject itemBox, int index) {
        if (container is TreeViewItem) {
            PrepareTreeItemContainer(this, container as TreeViewItem, itemBox, this);
        } else {
            base.PrepareContainerForItem(container, itemBox, index);
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        var scroll = new ScrollViewer();
        _itemsPresenter = new ItemsPresenter();
        _itemsPresenter.EnsurePanel();
        scroll.Content = _itemsPresenter;
        RefreshItems();
        return scroll;
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "SelectedItem") { return DependencyObject.BoxObject(SelectedItem); }
        return base.GetPropertyValue(propName);
    }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "SelectedItemChanged") {
            SelectedItemChanged += (object s, EventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, new RoutedEventArgs("SelectedItemChanged", this)); } };
            return true;
        }
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }
}

// ============================================================================
// 4. UserControl, Page, Window & Application
// ============================================================================

public class UserControl : ContentControl {
    public UserControl() : base() {
        IsTabStop = false;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }
    public override string GetTypeName() { return "UserControl"; }
    public override bool IsNamescopeBoundary() { return true; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }
}

public class Page : ContentControl {
    public string Title { get; set; } = "";
    public string WindowTitle { get; set; } = "";
    public Page() : base() {
        IsTabStop = false;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
    }
    public override string GetTypeName() { return "Page"; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }
}

public class Window : ContentControl {
    public string Title { get; set; } = "Aero Window";
    public string Icon { get; set; } = "";
    public string WindowState { get; set; } = "Normal";
    private bool _isActive = false;

    public event CancelEventHandler Closing;
    public event EventHandler Closed;
    public event EventHandler Activated;

    public bool IsActive { get { return _isActive; } }

    public Window() : base() {
        ThemeManager.EnsureInitialized();
        Background = new SolidColorBrush("#FF12151C");
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
        PopupService.SetOverlayHost(this);
    }

    public override string GetTypeName() { return "Window"; }
    protected override void OnBoxAppearanceChanged() {
        base.OnBoxAppearanceChanged();
        UpdateInheritedForegroundForBackground(Background);
    }

    public void Show() {
        _isActive = true;
        PopupService.SetOverlayHost(this);
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
        PopupService.CloseAll();
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
