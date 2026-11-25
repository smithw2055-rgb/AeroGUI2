module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// Aero.UI Workbench & EditorKit Controls (Sorted after VisualPrimitives.rs)
//   1. EditorMath & ValueEditLifecycleEventArgs
//   2. NumericScrubber, Vector2Field, Vector3Field
//   3. ViewportSurface (Zero-Copy External GPU FBO Texture Host + Gizmo Layer)
//   4. DockingWorkspace, DockSplitNode, DockTabGroup, DockPane
//   5. CodeEditorView (Virtualized Multi-Color Syntax Code Workbench + Gutter/IME)
//   6. PropertyGrid & InspectorPropertyItem (Reflection Inspector + Override Bar)
//   7. NodeGraphCanvas (Infinite Pan/Zoom Graph + Cubic Bezier SDF Wires)
// ============================================================================

public static class EditorMath {
    public static int Maxi(int a, int b) { return a > b ? a : b; }
    public static int Mini(int a, int b) { return a < b ? a : b; }
    public static int Clampi(int v, int lo, int hi) {
        if (v < lo) { return lo; }
        if (v > hi) { return hi; }
        return v;
    }
    public static int RoundToInt(float v) {
        if (v >= 0.0f) { return (int)(v + 0.5f); }
        return (int)(v - 0.5f);
    }
    public static float Floorf(float v) {
        int iv = (int)v;
        if ((float)iv > v) { return (float)(iv - 1); }
        return (float)iv;
    }
    public static string DigitChar(int d) {
        int r = d % 10;
        if (r == 0) { return "0"; }
        if (r == 1) { return "1"; }
        if (r == 2) { return "2"; }
        if (r == 3) { return "3"; }
        if (r == 4) { return "4"; }
        if (r == 5) { return "5"; }
        if (r == 6) { return "6"; }
        if (r == 7) { return "7"; }
        if (r == 8) { return "8"; }
        return "9";
    }
    public static string IntToStr(int v) {
        if (v == 0) { return "0"; }
        bool neg = v < 0;
        int n = neg ? -v : v;
        if (n < 0) { return "-2147483648"; }
        string res = "";
        while (n > 0) {
            res = DigitChar(n % 10) + res;
            n = n / 10;
        }
        return neg ? ("-" + res) : res;
    }
    public static string JoinLines(string[] lines) {
        if (lines == null || lines.Length == 0) { return ""; }
        string acc = lines[0] == null ? "" : lines[0];
        for (int i = 1; i < lines.Length; i += 1) {
            acc = acc + "\n" + (lines[i] == null ? "" : lines[i]);
        }
        return acc;
    }
}

public enum EditLifecyclePhase {
    Begin = 0,
    Change = 1,
    Commit = 2,
    Cancel = 3
}

public class ValueEditLifecycleEventArgs : RoutedEventArgs {
    public string PropertyPath { get; set; } = "";
    public float OldValue { get; set; } = 0.0f;
    public float NewValue { get; set; } = 0.0f;
    public string OldText { get; set; } = "";
    public string NewText { get; set; } = "";
    public EditLifecyclePhase Phase { get; set; } = EditLifecyclePhase.Change;

    public ValueEditLifecycleEventArgs() : base("ValueEdit") {}
    public ValueEditLifecycleEventArgs(
        string propPath,
        float oldVal,
        float newVal,
        EditLifecyclePhase phase,
        object source = null) : base("ValueEdit", source) {
        PropertyPath = propPath == null ? "" : propPath;
        OldValue = oldVal;
        NewValue = newVal;
        Phase = phase;
    }
}

public delegate void ValueEditEventHandler(object sender, ValueEditLifecycleEventArgs e);

public class ViewportInputEventArgs : RoutedEventArgs {
    public float LocalX { get; set; } = 0.0f;
    public float LocalY { get; set; } = 0.0f;
    public float NormalizedX { get; set; } = 0.0f;
    public float NormalizedY { get; set; } = 0.0f;
    public float WheelDelta { get; set; } = 0.0f;
    public int Button { get; set; } = 0;
    public int Modifiers { get; set; } = 0;

    public ViewportInputEventArgs() : base("ViewportInput") {}
}

public delegate void ViewportInputEventHandler(object sender, ViewportInputEventArgs e);

// ============================================================================
// 1. NumericScrubber, Vector2Field, Vector3Field
// ============================================================================

public class NumericScrubber : Control {
    private string _label = "X";
    private Brush _labelBrush;
    private float _value = 0.0f;
    private float _min = -1000000.0f;
    private float _max = 1000000.0f;
    private float _step = 0.1f;
    private int _decimals = 2;
    private bool _isReadOnly = false;
    private string _propertyPath = "";

    private bool _scrubbing = false;
    private bool _dragMoved = false;
    private float _dragStartX = 0.0f;
    private float _dragLastX = 0.0f;
    private float _editStartValue = 0.0f;
    private bool _inlineEditing = false;

    private Grid _rootGrid;
    private Border _labelBadge;
    private TextBlock _labelText;
    private TextBlock _valueText;
    private TextBox _inlineBox;

    public event ValueEditEventHandler ValueChanged;
    public event ValueEditEventHandler EditCommitted;

    public NumericScrubber() : base() {
        Focusable = true;
        MinWidth = 68.0f;
        Height = 26.0f;
        Background = new SolidColorBrush("#FF0F172A");
        BorderBrush = new SolidColorBrush("#FF334155");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(4.0f);
        _labelBrush = new SolidColorBrush("#FF38BDF8");
        BuildVisuals();
        RefreshVisuals();
    }

    public override string GetTypeName() { return "NumericScrubber"; }

    private void BuildVisuals() {
        _rootGrid = new Grid();
        _rootGrid.SetColumnDefinitions("22,*");
        AddChild(_rootGrid);

        _labelBadge = new Border();
        _labelBadge.Background = new SolidColorBrush("#331E293B");
        _labelBadge.CornerRadius = new CornerRadius(3.0f, 0.0f, 0.0f, 3.0f);
        UIElement.SetGridColumn(_labelBadge, 0);

        _labelText = new TextBlock();
        _labelText.FontSize = 11.0f;
        _labelText.FontWeight = "Bold";
        _labelText.HorizontalAlignment = HorizontalAlignment.Center;
        _labelText.VerticalAlignment = VerticalAlignment.Center;
        _labelBadge.Child = _labelText;
        _rootGrid.AddChild(_labelBadge);

        _valueText = new TextBlock();
        _valueText.FontSize = 12.0f;
        _valueText.Foreground = new SolidColorBrush("#FFE2E8F0");
        _valueText.Margin = new Thickness(6.0f, 0.0f, 6.0f, 0.0f);
        _valueText.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_valueText, 1);
        _rootGrid.AddChild(_valueText);

        _inlineBox = new TextBox();
        _inlineBox.FontSize = 12.0f;
        _inlineBox.Padding = new Thickness(4.0f, 2.0f, 4.0f, 2.0f);
        _inlineBox.BorderThickness = new Thickness(0.0f);
        _inlineBox.Visibility = Visibility.Collapsed;
        UIElement.SetGridColumn(_inlineBox, 1);
        NumericScrubber self = this;
        _inlineBox.KeyDown += (object s, KeyEventArgs ke) => {
            if (ke.Key == KeyCode.Enter) {
                self.CommitInlineText();
                ke.Handled = true;
            } else if (ke.Key == KeyCode.Escape) {
                self.CancelInlineText();
                ke.Handled = true;
            }
        };
        _rootGrid.AddChild(_inlineBox);
    }

    private string FormatFloat(float v) {
        int dec = EditorMath.Clampi(_decimals, 0, 4);
        if (dec == 0) {
            return EditorMath.IntToStr(EditorMath.RoundToInt(v));
        }
        float scale = 1.0f;
        for (int i = 0; i < dec; i += 1) { scale *= 10.0f; }
        bool neg = v < 0.0f;
        float absV = neg ? -v : v;
        int scaled = EditorMath.RoundToInt(absV * scale);
        int intScale = EditorMath.RoundToInt(scale);
        int whole = scaled / intScale;
        int frac = scaled % intScale;
        string fracStr = EditorMath.IntToStr(frac);
        while (fracStr.Length < dec) { fracStr = "0" + fracStr; }
        return (neg ? "-" : "") + EditorMath.IntToStr(whole) + "." + fracStr;
    }

    private void RefreshVisuals() {
        if (_labelText != null) {
            _labelText.Text = _label;
            _labelText.Foreground = _labelBrush != null ? _labelBrush : new SolidColorBrush("#FF38BDF8");
        }
        if (_valueText != null) {
            _valueText.Text = FormatFloat(_value);
            _valueText.Visibility = _inlineEditing ? Visibility.Collapsed : Visibility.Visible;
        }
        if (_inlineBox != null) {
            _inlineBox.Visibility = _inlineEditing ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    public string Label {
        get { return _label; }
        set { _label = value == null ? "" : value; RefreshVisuals(); }
    }
    public Brush LabelBrush {
        get { return _labelBrush; }
        set { _labelBrush = value; RefreshVisuals(); }
    }
    public float Minimum {
        get { return _min; }
        set { _min = value; if (_value < _min) { Value = _min; } }
    }
    public float Maximum {
        get { return _max; }
        set { _max = value; if (_value > _max) { Value = _max; } }
    }
    public float Step {
        get { return _step; }
        set { _step = value > 0.00001f ? value : 0.00001f; }
    }
    public int Decimals {
        get { return _decimals; }
        set { _decimals = value; RefreshVisuals(); }
    }
    public bool IsReadOnly {
        get { return _isReadOnly; }
        set { _isReadOnly = value; }
    }
    public string PropertyPath {
        get { return _propertyPath; }
        set { _propertyPath = value == null ? "" : value; }
    }

    public float Value {
        get { return _value; }
        set { SetValueInternal(value, false, EditLifecyclePhase.Change); }
    }

    private void SetValueInternal(float nextVal, bool emitEvents, EditLifecyclePhase phase) {
        float clamped = Math.Clampf(nextVal, _min, _max);
        float prev = _value;
        _value = clamped;
        BumpPropEpoch();
        NotifyBoundPropertyChanged("Value", DependencyObject.BoxFloat(_value));
        RefreshVisuals();
        if (emitEvents) {
            var ev = new ValueEditLifecycleEventArgs(_propertyPath, prev, _value, phase, this);
            ValueChanged(this, ev);
            if (phase == EditLifecyclePhase.Commit) {
                var cev = new ValueEditLifecycleEventArgs(_propertyPath, _editStartValue, _value, EditLifecyclePhase.Commit, this);
                EditCommitted(this, cev);
            }
        }
    }

    public void ScrubByPixels(float deltaPixels, int modifiers = 0) {
        if (_isReadOnly) { return; }
        float mult = 1.0f;
        if (KeyModifiers.Has(modifiers, KeyModifiers.Shift())) { mult = 0.1f; }
        else if (KeyModifiers.Has(modifiers, KeyModifiers.Ctrl())) { mult = 10.0f; }
        float oldV = _value;
        _editStartValue = oldV;
        SetValueInternal(oldV + deltaPixels * _step * mult, true, EditLifecyclePhase.Commit);
    }

    private void EnterInlineEdit() {
        if (_isReadOnly || _inlineEditing) { return; }
        _inlineEditing = true;
        _editStartValue = _value;
        _inlineBox.Text = FormatFloat(_value);
        RefreshVisuals();
        _inlineBox.Focus();
    }

    public void CommitInlineText() {
        if (!_inlineEditing) { return; }
        _inlineEditing = false;
        float parsed = ParseUtil.ParseFloat(_inlineBox.Text, _value);
        SetValueInternal(parsed, true, EditLifecyclePhase.Commit);
    }

    public void CancelInlineText() {
        if (!_inlineEditing) { return; }
        _inlineEditing = false;
        _value = _editStartValue;
        RefreshVisuals();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled || _isReadOnly || _inlineEditing) { return; }
        Focus();
        _scrubbing = true;
        _dragMoved = false;
        _dragStartX = e.X;
        _dragLastX = e.X;
        _editStartValue = _value;
        CaptureMouse();
        ValueChanged(this, new ValueEditLifecycleEventArgs(_propertyPath, _value, _value, EditLifecyclePhase.Begin, this));
        e.Handled = true;
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_scrubbing) { return; }
        float dx = e.X - _dragLastX;
        if (Math.Absf(e.X - _dragStartX) > 2.0f) {
            _dragMoved = true;
        }
        if (_dragMoved && Math.Absf(dx) > 0.001f) {
            _dragLastX = e.X;
            float mult = 1.0f;
            if (KeyModifiers.Has(e.Modifiers, KeyModifiers.Shift())) { mult = 0.1f; }
            else if (KeyModifiers.Has(e.Modifiers, KeyModifiers.Ctrl())) { mult = 10.0f; }
            SetValueInternal(_value + dx * _step * mult, true, EditLifecyclePhase.Change);
        }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (!_scrubbing) { return; }
        _scrubbing = false;
        ReleaseMouseCapture();
        if (!_dragMoved) {
            EnterInlineEdit();
        } else {
            EditCommitted(this, new ValueEditLifecycleEventArgs(_propertyPath, _editStartValue, _value, EditLifecyclePhase.Commit, this));
        }
        e.Handled = true;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Label") { Label = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Value") { Value = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, -1000000.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 1000000.0f); return true; }
        if (propName == "Step") { Step = DependencyObject.UnboxFloat(value, 0.1f); return true; }
        if (propName == "Decimals") { Decimals = DependencyObject.UnboxInt(value, 2); return true; }
        if (propName == "IsReadOnly") { IsReadOnly = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "PropertyPath") { PropertyPath = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Label") { return DependencyObject.BoxString(_label); }
        if (propName == "Value") { return DependencyObject.BoxFloat(_value); }
        if (propName == "Minimum") { return DependencyObject.BoxFloat(_min); }
        if (propName == "Maximum") { return DependencyObject.BoxFloat(_max); }
        if (propName == "Step") { return DependencyObject.BoxFloat(_step); }
        if (propName == "Decimals") { return DependencyObject.BoxInt(_decimals); }
        if (propName == "IsReadOnly") { return DependencyObject.BoxBool(_isReadOnly); }
        if (propName == "PropertyPath") { return DependencyObject.BoxString(_propertyPath); }
        return base.GetPropertyValue(propName);
    }
}

public class Vector2Field : Control {
    private NumericScrubber _xBox;
    private NumericScrubber _yBox;

    public event EventHandler VectorChanged;
    public event EventHandler EditCommitted;

    public Vector2Field() : base() {
        Height = 26.0f;
        var g = new Grid();
        g.ColumnSpacing = 4.0f;
        g.SetColumnDefinitions("*,*");
        AddChild(g);

        _xBox = new NumericScrubber();
        _xBox.Label = "X";
        _xBox.LabelBrush = new SolidColorBrush("#FFEF4444");
        UIElement.SetGridColumn(_xBox, 0);
        g.AddChild(_xBox);

        _yBox = new NumericScrubber();
        _yBox.Label = "Y";
        _yBox.LabelBrush = new SolidColorBrush("#FF22C55E");
        UIElement.SetGridColumn(_yBox, 1);
        g.AddChild(_yBox);

        Vector2Field self = this;
        _xBox.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseVectorChanged(); };
        _yBox.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseVectorChanged(); };
        _xBox.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseEditCommitted(); };
        _yBox.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseEditCommitted(); };
    }

    public override string GetTypeName() { return "Vector2Field"; }

    internal void RaiseVectorChanged() { VectorChanged(this, EventArgs.Empty()); }
    internal void RaiseEditCommitted() { EditCommitted(this, EventArgs.Empty()); }

    public float X { get { return _xBox.Value; } set { _xBox.Value = value; } }
    public float Y { get { return _yBox.Value; } set { _yBox.Value = value; } }
    public float Step {
        get { return _xBox.Step; }
        set { _xBox.Step = value; _yBox.Step = value; }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "X") { X = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Y") { Y = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Step") { Step = DependencyObject.UnboxFloat(value, 0.1f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "X") { return DependencyObject.BoxFloat(X); }
        if (propName == "Y") { return DependencyObject.BoxFloat(Y); }
        if (propName == "Step") { return DependencyObject.BoxFloat(Step); }
        return base.GetPropertyValue(propName);
    }
}

public class Vector3Field : Control {
    private NumericScrubber _xBox;
    private NumericScrubber _yBox;
    private NumericScrubber _zBox;

    public event EventHandler VectorChanged;
    public event EventHandler EditCommitted;

    public Vector3Field() : base() {
        Height = 26.0f;
        var g = new Grid();
        g.ColumnSpacing = 4.0f;
        g.SetColumnDefinitions("*,*,*");
        AddChild(g);

        _xBox = new NumericScrubber();
        _xBox.Label = "X";
        _xBox.LabelBrush = new SolidColorBrush("#FFEF4444");
        UIElement.SetGridColumn(_xBox, 0);
        g.AddChild(_xBox);

        _yBox = new NumericScrubber();
        _yBox.Label = "Y";
        _yBox.LabelBrush = new SolidColorBrush("#FF22C55E");
        UIElement.SetGridColumn(_yBox, 1);
        g.AddChild(_yBox);

        _zBox = new NumericScrubber();
        _zBox.Label = "Z";
        _zBox.LabelBrush = new SolidColorBrush("#FF3B82F6");
        UIElement.SetGridColumn(_zBox, 2);
        g.AddChild(_zBox);

        Vector3Field self = this;
        _xBox.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseVectorChanged(); };
        _yBox.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseVectorChanged(); };
        _zBox.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseVectorChanged(); };
        _xBox.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseEditCommitted(); };
        _yBox.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseEditCommitted(); };
        _zBox.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => { self.RaiseEditCommitted(); };
    }

    public override string GetTypeName() { return "Vector3Field"; }

    internal void RaiseVectorChanged() { VectorChanged(this, EventArgs.Empty()); }
    internal void RaiseEditCommitted() { EditCommitted(this, EventArgs.Empty()); }

    public NumericScrubber ScrubberX() { return _xBox; }
    public NumericScrubber ScrubberY() { return _yBox; }
    public NumericScrubber ScrubberZ() { return _zBox; }

    public float X { get { return _xBox.Value; } set { _xBox.Value = value; } }
    public float Y { get { return _yBox.Value; } set { _yBox.Value = value; } }
    public float Z { get { return _zBox.Value; } set { _zBox.Value = value; } }
    public float Step {
        get { return _xBox.Step; }
        set { _xBox.Step = value; _yBox.Step = value; _zBox.Step = value; }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "X") { X = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Y") { Y = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Z") { Z = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Step") { Step = DependencyObject.UnboxFloat(value, 0.1f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "X") { return DependencyObject.BoxFloat(X); }
        if (propName == "Y") { return DependencyObject.BoxFloat(Y); }
        if (propName == "Z") { return DependencyObject.BoxFloat(Z); }
        if (propName == "Step") { return DependencyObject.BoxFloat(Step); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 2. ViewportSurface (Zero-Copy External FBO Texture Host + Gizmo Canvas)
// ============================================================================

public class ViewportSurface : ContentControl {
    private string _textureUri = "viewport://scene";
    private Stretch _stretch = Stretch.Fill;
    private string _overlayBadgeText = "";
    private Grid _hostGrid;
    private Image _fboImage;
    private CustomDrawElement _gizmoLayer;
    private Border _badgeBorder;
    private TextBlock _badgeText;
    private ContentPresenter _overlayPresenter;

    public event ViewportInputEventHandler ViewportPointerDown;
    public event ViewportInputEventHandler ViewportPointerMove;
    public event ViewportInputEventHandler ViewportPointerUp;
    public event ViewportInputEventHandler ViewportWheel;

    public ViewportSurface() : base() {
        Focusable = true;
        ClipToBounds = true;
        Background = new SolidColorBrush("#FF090D16");

        _hostGrid = new Grid();
        AddChild(_hostGrid);

        _fboImage = new Image();
        _fboImage.Stretch = _stretch;
        _fboImage.Source = _textureUri;
        _fboImage.IsHitTestVisible = false;
        _hostGrid.AddChild(_fboImage);

        _gizmoLayer = new CustomDrawElement();
        _gizmoLayer.IsHitTestVisible = false;
        _hostGrid.AddChild(_gizmoLayer);

        _overlayPresenter = new ContentPresenter();
        _hostGrid.AddChild(_overlayPresenter);

        _badgeBorder = new Border();
        _badgeBorder.Background = new SolidColorBrush("#AA0F172A");
        _badgeBorder.BorderBrush = new SolidColorBrush("#FF334155");
        _badgeBorder.BorderThickness = new Thickness(1.0f);
        _badgeBorder.CornerRadius = new CornerRadius(4.0f);
        _badgeBorder.Padding = new Thickness(8.0f, 3.0f, 8.0f, 3.0f);
        _badgeBorder.Margin = new Thickness(8.0f);
        _badgeBorder.HorizontalAlignment = HorizontalAlignment.Right;
        _badgeBorder.VerticalAlignment = VerticalAlignment.Top;
        _badgeBorder.IsHitTestVisible = false;
        _badgeBorder.Visibility = Visibility.Collapsed;

        _badgeText = new TextBlock();
        _badgeText.FontSize = 11.0f;
        _badgeText.Foreground = new SolidColorBrush("#FF94A3B8");
        _badgeBorder.Child = _badgeText;
        _hostGrid.AddChild(_badgeBorder);
    }

    public override string GetTypeName() { return "ViewportSurface"; }

    protected override void OnContentChanged(object oldVal, object newVal) {
        if (_overlayPresenter != null) {
            _overlayPresenter.Content = newVal;
        }
    }

    public string TextureUri {
        get { return _textureUri; }
        set {
            _textureUri = value == null ? "" : value;
            if (_fboImage != null) { _fboImage.Source = _textureUri; }
            BumpPropEpoch();
        }
    }

    public Stretch Stretch {
        get { return _stretch; }
        set {
            _stretch = value;
            if (_fboImage != null) { _fboImage.Stretch = _stretch; }
        }
    }

    public string OverlayBadgeText {
        get { return _overlayBadgeText; }
        set {
            _overlayBadgeText = value == null ? "" : value;
            if (_badgeText != null) { _badgeText.Text = _overlayBadgeText; }
            if (_badgeBorder != null) {
                _badgeBorder.Visibility = _overlayBadgeText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
            }
        }
    }

    public CustomDrawElement GizmoLayer() { return _gizmoLayer; }

    public Action<DrawingContext> GizmoRenderCallback {
        get { return _gizmoLayer != null ? _gizmoLayer.RenderCallback : null; }
        set { if (_gizmoLayer != null) { _gizmoLayer.RenderCallback = value; } }
    }

    public void InvalidateGizmos() {
        if (_gizmoLayer != null) { _gizmoLayer.InvalidateDraw(); }
    }

    private ViewportInputEventArgs MakeViewportArgs(float screenX, float screenY, int button, int mods, float wheel) {
        float lx = screenX;
        float ly = screenY;
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            lx = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 0);
            ly = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 1);
        }
        float vw = Math.Maxf(1.0f, ActualWidth);
        float vh = Math.Maxf(1.0f, ActualHeight);
        var ev = new ViewportInputEventArgs();
        ev.Source = this;
        ev.LocalX = lx;
        ev.LocalY = ly;
        ev.NormalizedX = Math.Clampf(lx / vw, 0.0f, 1.0f);
        ev.NormalizedY = Math.Clampf(ly / vh, 0.0f, 1.0f);
        ev.Button = button;
        ev.Modifiers = mods;
        ev.WheelDelta = wheel;
        return ev;
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        Focus();
        CaptureMouse();
        ViewportPointerDown(this, MakeViewportArgs(e.X, e.Y, 0, e.Modifiers, 0.0f));
    }

    public override void OnMouseRightButtonDown(MouseButtonEventArgs e) {
        base.OnMouseRightButtonDown(e);
        Focus();
        CaptureMouse();
        ViewportPointerDown(this, MakeViewportArgs(e.X, e.Y, 1, e.Modifiers, 0.0f));
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        ViewportPointerMove(this, MakeViewportArgs(e.X, e.Y, 0, e.Modifiers, 0.0f));
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        ReleaseMouseCapture();
        ViewportPointerUp(this, MakeViewportArgs(e.X, e.Y, 0, e.Modifiers, 0.0f));
    }

    public override void OnMouseRightButtonUp(MouseButtonEventArgs e) {
        base.OnMouseRightButtonUp(e);
        ReleaseMouseCapture();
        ViewportPointerUp(this, MakeViewportArgs(e.X, e.Y, 1, e.Modifiers, 0.0f));
    }

    public override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        ViewportWheel(this, MakeViewportArgs(e.X, e.Y, 0, e.Modifiers, e.Delta));
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "TextureUri") { TextureUri = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "OverlayBadgeText") { OverlayBadgeText = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "TextureUri") { return DependencyObject.BoxString(_textureUri); }
        if (propName == "OverlayBadgeText") { return DependencyObject.BoxString(_overlayBadgeText); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 3. DockingWorkspace / DockSplitNode / DockTabGroup / DockPane
// ============================================================================

public enum DockZone {
    Center = 0,
    Left = 1,
    Right = 2,
    Top = 3,
    Bottom = 4
}

public class DockPane : ContentControl {
    public string PaneId { get; set; } = "";
    public string Title { get; set; } = "Pane";
    public bool CanClose { get; set; } = true;

    public DockPane() : base() {}
    public DockPane(string id, string title, UIElement content = null) : base() {
        PaneId = id == null ? "" : id;
        Title = title == null ? "Pane" : title;
        if (content != null) { Content = content; }
    }

    public override string GetTypeName() { return "DockPane"; }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "PaneId") { PaneId = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Title") { Title = DependencyObject.UnboxString(value, "Pane"); return true; }
        if (propName == "CanClose") { CanClose = DependencyObject.UnboxBool(value, true); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "PaneId") { return DependencyObject.BoxString(PaneId); }
        if (propName == "Title") { return DependencyObject.BoxString(Title); }
        if (propName == "CanClose") { return DependencyObject.BoxBool(CanClose); }
        return base.GetPropertyValue(propName);
    }
}

public class DockTabGroup : Control {
    private DockPane[] _panes;
    private int _selectedIndex = -1;
    private Grid _rootGrid;
    private StackPanel _tabStrip;
    private Border _contentHost;

    public event EventHandler ActivePaneChanged;

    public DockTabGroup() : base() {
        _panes = new DockPane[0];
        Background = new SolidColorBrush("#FF111827");
        BorderBrush = new SolidColorBrush("#FF1E293B");
        BorderThickness = new Thickness(1.0f);

        _rootGrid = new Grid();
        _rootGrid.SetRowDefinitions("28,*");
        AddChild(_rootGrid);

        var headerBar = new Border();
        headerBar.Background = new SolidColorBrush("#FF0B1120");
        headerBar.BorderBrush = new SolidColorBrush("#FF1E293B");
        headerBar.BorderThickness = new Thickness(0.0f, 0.0f, 0.0f, 1.0f);
        UIElement.SetGridRow(headerBar, 0);

        _tabStrip = new StackPanel();
        _tabStrip.Orientation = Orientation.Horizontal;
        _tabStrip.Spacing = 2.0f;
        headerBar.Child = _tabStrip;
        _rootGrid.AddChild(headerBar);

        _contentHost = new Border();
        _contentHost.Background = new SolidColorBrush("#FF0F172A");
        UIElement.SetGridRow(_contentHost, 1);
        _rootGrid.AddChild(_contentHost);
    }

    public override string GetTypeName() { return "DockTabGroup"; }

    public int PaneCount() { return _panes.Length; }
    public DockPane[] Panes() { return _panes; }

    public int SelectedIndex {
        get { return _selectedIndex; }
        set {
            int next = _panes.Length == 0 ? -1 : EditorMath.Clampi(value, 0, _panes.Length - 1);
            if (_selectedIndex == next) { return; }
            _selectedIndex = next;
            RebuildTabsAndContent();
            ActivePaneChanged(this, EventArgs.Empty());
        }
    }

    public DockPane ActivePane() {
        if (_selectedIndex >= 0 && _selectedIndex < _panes.Length) {
            return _panes[_selectedIndex];
        }
        return null;
    }

    public void AddPane(DockPane pane, bool selectIt = true) {
        if (pane == null) { return; }
        _panes.Push(pane);
        if (_selectedIndex < 0 || selectIt) {
            _selectedIndex = _panes.Length - 1;
        }
        RebuildTabsAndContent();
    }

    public bool RemovePaneById(string paneId) {
        if (paneId == null || paneId.Length == 0) { return false; }
        for (int i = 0; i < _panes.Length; i += 1) {
            if (_panes[i].PaneId == paneId) {
                _panes.RemoveAt(i);
                if (_selectedIndex >= _panes.Length) {
                    _selectedIndex = _panes.Length - 1;
                }
                RebuildTabsAndContent();
                return true;
            }
        }
        return false;
    }

    public bool SelectPaneById(string paneId) {
        for (int i = 0; i < _panes.Length; i += 1) {
            if (_panes[i].PaneId == paneId) {
                SelectedIndex = i;
                return true;
            }
        }
        return false;
    }

    private void RebuildTabsAndContent() {
        if (_tabStrip == null || _contentHost == null) { return; }
        _tabStrip.ClearChildren();
        DockTabGroup self = this;
        for (int i = 0; i < _panes.Length; i += 1) {
            int capturedIdx = i;
            DockPane p = _panes[i];
            bool active = (i == _selectedIndex);

            var tabBtn = new Border();
            tabBtn.Padding = new Thickness(10.0f, 4.0f, 10.0f, 4.0f);
            tabBtn.Background = new SolidColorBrush(active ? "#FF1E293B" : "#00000000");
            tabBtn.BorderBrush = new SolidColorBrush(active ? "#FF38BDF8" : "#00000000");
            tabBtn.BorderThickness = new Thickness(0.0f, 2.0f, 0.0f, 0.0f);

            var lbl = new TextBlock();
            lbl.Text = p.Title;
            lbl.FontSize = 12.0f;
            lbl.Foreground = new SolidColorBrush(active ? "#FFF8FAFC" : "#FF94A3B8");
            lbl.VerticalAlignment = VerticalAlignment.Center;
            tabBtn.Child = lbl;

            tabBtn.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                self.SelectedIndex = capturedIdx;
                e.Handled = true;
            };
            _tabStrip.AddChild(tabBtn);
        }

        _contentHost.Child = null;
        DockPane cur = ActivePane();
        if (cur != null) {
            object rawContent = cur.Content;
            if (rawContent is UIElement) {
                _contentHost.Child = rawContent as UIElement;
            } else {
                _contentHost.Child = cur;
            }
        }
    }
}

public class DockSplitNode : Control {
    private Orientation _orientation = Orientation.Horizontal;
    private float _splitRatio = 0.5f;
    private UIElement _first;
    private UIElement _second;
    private Grid _grid;
    private Thumb _splitter;

    public DockSplitNode() : base() {
        InitSplitVisuals();
        RebuildSplitLayout();
    }

    public DockSplitNode(Orientation orientation, float ratio, UIElement first, UIElement second) : base() {
        _orientation = orientation;
        _splitRatio = Math.Clampf(ratio, 0.1f, 0.9f);
        _first = first;
        _second = second;
        InitSplitVisuals();
        RebuildSplitLayout();
    }

    private void InitSplitVisuals() {
        _grid = new Grid();
        AddChild(_grid);

        _splitter = new Thumb();
        _splitter.Background = new SolidColorBrush("#FF1E293B");
        DockSplitNode self = this;
        _splitter.DragDelta += (object s, DragDeltaEventArgs e) => {
            self.HandleSplitterDrag(e.HorizontalChange, e.VerticalChange);
        };
    }

    internal void HandleSplitterDrag(float dx, float dy) {
        if (_orientation == Orientation.Horizontal) {
            float totalW = Math.Maxf(100.0f, ActualWidth);
            SplitRatio = Math.Clampf(_splitRatio + (dx / totalW), 0.1f, 0.9f);
        } else {
            float totalH = Math.Maxf(100.0f, ActualHeight);
            SplitRatio = Math.Clampf(_splitRatio + (dy / totalH), 0.1f, 0.9f);
        }
    }

    public override string GetTypeName() { return "DockSplitNode"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; RebuildSplitLayout(); }
    }
    public float SplitRatio {
        get { return _splitRatio; }
        set { _splitRatio = Math.Clampf(value, 0.1f, 0.9f); UpdateStarWeights(); }
    }
    public UIElement First {
        get { return _first; }
        set { _first = value; RebuildSplitLayout(); }
    }
    public UIElement Second {
        get { return _second; }
        set { _second = value; RebuildSplitLayout(); }
    }

    private void UpdateStarWeights() {
        if (_grid == null) { return; }
        float r0 = Math.Maxf(0.05f, _splitRatio);
        float r1 = Math.Maxf(0.05f, 1.0f - _splitRatio);
        if (_orientation == Orientation.Horizontal) {
            if (_grid.ColumnDefinitions.Count() == 3) {
                _grid.ColumnDefinitions.Get(0).Width = GridLength.FromStar(r0);
                _grid.ColumnDefinitions.Get(2).Width = GridLength.FromStar(r1);
            }
        } else {
            if (_grid.RowDefinitions.Count() == 3) {
                _grid.RowDefinitions.Get(0).Height = GridLength.FromStar(r0);
                _grid.RowDefinitions.Get(2).Height = GridLength.FromStar(r1);
            }
        }
    }

    private void RebuildSplitLayout() {
        if (_grid == null) { return; }
        _grid.ClearChildren();
        _grid.ClearRowDefinitions();
        _grid.ClearColumnDefinitions();

        float r0 = Math.Maxf(0.05f, _splitRatio);
        float r1 = Math.Maxf(0.05f, 1.0f - _splitRatio);

        if (_orientation == Orientation.Horizontal) {
            _grid.ColumnDefinitions.AddNoSync(ColumnDefinition.Star(r0));
            _grid.ColumnDefinitions.AddNoSync(ColumnDefinition.Pixel(4.0f));
            _grid.ColumnDefinitions.AddNoSync(ColumnDefinition.Star(r1));
            _grid.SyncGridTracks();
            _splitter.Width = 4.0f;
            _splitter.Height = ParseUtil.NaN();
            if (_first != null) { UIElement.SetGridRow(_first, 0); UIElement.SetGridColumn(_first, 0); _grid.AddChild(_first); }
            UIElement.SetGridRow(_splitter, 0); UIElement.SetGridColumn(_splitter, 1);
            _grid.AddChild(_splitter);
            if (_second != null) { UIElement.SetGridRow(_second, 0); UIElement.SetGridColumn(_second, 2); _grid.AddChild(_second); }
        } else {
            _grid.RowDefinitions.AddNoSync(RowDefinition.Star(r0));
            _grid.RowDefinitions.AddNoSync(RowDefinition.Pixel(4.0f));
            _grid.RowDefinitions.AddNoSync(RowDefinition.Star(r1));
            _grid.SyncGridTracks();
            _splitter.Height = 4.0f;
            _splitter.Width = ParseUtil.NaN();
            if (_first != null) { UIElement.SetGridRow(_first, 0); UIElement.SetGridColumn(_first, 0); _grid.AddChild(_first); }
            UIElement.SetGridRow(_splitter, 1); UIElement.SetGridColumn(_splitter, 0);
            _grid.AddChild(_splitter);
            if (_second != null) { UIElement.SetGridRow(_second, 2); UIElement.SetGridColumn(_second, 0); _grid.AddChild(_second); }
        }
    }
}

public class DockingWorkspace : Control {
    private Grid _hostGrid;
    private UIElement _rootNode;
    private DockTabGroup _leftGroup;
    private DockTabGroup _centerGroup;
    private DockTabGroup _rightGroup;
    private DockTabGroup _bottomGroup;
    private DockSplitNode _mainHorizSplit;
    private DockSplitNode _centerBottomSplit;
    private DockSplitNode _centerRightSplit;
    private Dictionary<string, DockPane> _panesById;

    public DockingWorkspace() : base() {
        Background = new SolidColorBrush("#FF090D16");
        _panesById = new Dictionary<string, DockPane>();
        _hostGrid = new Grid();
        AddChild(_hostGrid);
        ResetDefaultIdeLayout();
    }

    public override string GetTypeName() { return "DockingWorkspace"; }

    public void ResetDefaultIdeLayout() {
        _leftGroup = new DockTabGroup();
        _centerGroup = new DockTabGroup();
        _rightGroup = new DockTabGroup();
        _bottomGroup = new DockTabGroup();

        _centerBottomSplit = new DockSplitNode(Orientation.Vertical, 0.72f, _centerGroup, _bottomGroup);
        _centerRightSplit = new DockSplitNode(Orientation.Horizontal, 0.74f, _centerBottomSplit, _rightGroup);
        _mainHorizSplit = new DockSplitNode(Orientation.Horizontal, 0.20f, _leftGroup, _centerRightSplit);
        SetRootDockNode(_mainHorizSplit);
    }

    public UIElement RootDockNode() { return _rootNode; }

    public void SetRootDockNode(UIElement node) {
        _rootNode = node;
        _hostGrid.ClearChildren();
        if (_rootNode != null) {
            _hostGrid.AddChild(_rootNode);
        }
    }

    public DockTabGroup ZoneGroup(DockZone zone) {
        if (zone == DockZone.Left) { return _leftGroup; }
        if (zone == DockZone.Right) { return _rightGroup; }
        if (zone == DockZone.Bottom) { return _bottomGroup; }
        return _centerGroup;
    }

    public DockPane RegisterPane(string paneId, string title, UIElement content, DockZone zone = DockZone.Center) {
        var pane = new DockPane(paneId, title, content);
        if (paneId != null && paneId.Length > 0) {
            _panesById[paneId] = pane;
        }
        DockTabGroup grp = ZoneGroup(zone);
        if (grp != null) {
            grp.AddPane(pane, true);
        }
        return pane;
    }

    public bool ActivatePane(string paneId) {
        if (_leftGroup != null && _leftGroup.SelectPaneById(paneId)) { return true; }
        if (_centerGroup != null && _centerGroup.SelectPaneById(paneId)) { return true; }
        if (_rightGroup != null && _rightGroup.SelectPaneById(paneId)) { return true; }
        if (_bottomGroup != null && _bottomGroup.SelectPaneById(paneId)) { return true; }
        return false;
    }

    public bool MovePaneToZone(string paneId, DockZone targetZone) {
        if (!_panesById.ContainsKey(paneId)) { return false; }
        DockPane pane = _panesById[paneId];
        if (_leftGroup != null) { _leftGroup.RemovePaneById(paneId); }
        if (_centerGroup != null) { _centerGroup.RemovePaneById(paneId); }
        if (_rightGroup != null) { _rightGroup.RemovePaneById(paneId); }
        if (_bottomGroup != null) { _bottomGroup.RemovePaneById(paneId); }
        DockTabGroup dst = ZoneGroup(targetZone);
        if (dst == null) { return false; }
        dst.AddPane(pane, true);
        return true;
    }

    public float LeftSplitRatio {
        get { return _mainHorizSplit != null ? _mainHorizSplit.SplitRatio : 0.20f; }
        set { if (_mainHorizSplit != null) { _mainHorizSplit.SplitRatio = value; } }
    }
    public float RightSplitRatio {
        get { return _centerRightSplit != null ? _centerRightSplit.SplitRatio : 0.74f; }
        set { if (_centerRightSplit != null) { _centerRightSplit.SplitRatio = value; } }
    }
    public float BottomSplitRatio {
        get { return _centerBottomSplit != null ? _centerBottomSplit.SplitRatio : 0.72f; }
        set { if (_centerBottomSplit != null) { _centerBottomSplit.SplitRatio = value; } }
    }

    public string SerializeLayout() {
        int lPct = EditorMath.RoundToInt(LeftSplitRatio * 1000.0f);
        int rPct = EditorMath.RoundToInt(RightSplitRatio * 1000.0f);
        int bPct = EditorMath.RoundToInt(BottomSplitRatio * 1000.0f);
        return "aero-dock-v1:" + EditorMath.IntToStr(lPct) + "," + EditorMath.IntToStr(rPct) + "," + EditorMath.IntToStr(bPct);
    }

    public bool DeserializeLayout(string spec) {
        if (spec == null || !spec.StartsWith("aero-dock-v1:")) { return false; }
        string body = spec.Substring(13);
        string[] parts = body.Split(",");
        if (parts.Length < 3) { return false; }
        LeftSplitRatio = ParseUtil.ParseFloat(parts[0], 200.0f) / 1000.0f;
        RightSplitRatio = ParseUtil.ParseFloat(parts[1], 740.0f) / 1000.0f;
        BottomSplitRatio = ParseUtil.ParseFloat(parts[2], 720.0f) / 1000.0f;
        return true;
    }
}

// ============================================================================
// 4. CodeEditorView (Virtualized Multi-Color Code Workbench + LSP/DAP Hooks)
// ============================================================================

public class CodeDiagnosticItem : DependencyObject {
    public int Line { get; set; } = 1;
    public int Column { get; set; } = 1;
    public int Length { get; set; } = 4;
    public int Severity { get; set; } = 2;
    public string Message { get; set; } = "";

    public CodeDiagnosticItem() {}
    public CodeDiagnosticItem(int line, int col, int len, int severity, string msg) {
        Line = line;
        Column = col;
        Length = len;
        Severity = severity;
        Message = msg == null ? "" : msg;
    }
}

public class CodeEditorView : Control {
    private string[] _lines;
    private string _language = "rs";
    private bool _isReadOnly = false;
    private float _lineHeight = 20.0f;
    private float _gutterWidth = 48.0f;
    private int _scrollFirstLine = 0;
    private int _caretLine = 0;
    private int _caretCol = 0;
    private int _anchorLine = 0;
    private int _anchorCol = 0;
    private bool _isDragSelecting = false;
    private int _activeExecLine = -1;
    private Dictionary<int, bool> _breakpoints;
    private CodeDiagnosticItem[] _diagnostics;
    private string[] _completions;
    private int _completionIndex = 0;
    private bool _completionVisible = false;

    private CustomDrawElement _canvas;

    public event TextChangedEventHandler TextChanged;
    public event EventHandler SaveRequested;
    public event EventHandler CaretMoved;
    public event EventHandler BreakpointsChanged;

    public CodeEditorView() : base() {
        Focusable = true;
        ClipToBounds = true;
        FontFamily = "";
        FontSize = 13.0f;
        Background = new SolidColorBrush("#FF0B1120");
        BorderBrush = new SolidColorBrush("#FF1E293B");
        BorderThickness = new Thickness(1.0f);

        _lines = new string[1];
        _lines[0] = "";
        _breakpoints = new Dictionary<int, bool>();
        _diagnostics = new CodeDiagnosticItem[0];
        _completions = new string[0];

        CodeEditorView self = this;
        _canvas = new CustomDrawElement((DrawingContext dc) => { self.RenderCodeCanvas(dc); });
        _canvas.IsHitTestVisible = false;
        AddChild(_canvas);
    }

    public override string GetTypeName() { return "CodeEditorView"; }

    public string Language {
        get { return _language; }
        set { _language = value == null ? "rs" : value.ToLower(); InvalidateEditor(); }
    }

    public bool IsReadOnly {
        get { return _isReadOnly; }
        set { _isReadOnly = value; InvalidateEditor(); }
    }

    public int LineCount() { return _lines.Length; }
    public int CaretLine { get { return _caretLine; } set { SetCaretPosition(value, _caretCol); } }
    public int CaretColumn { get { return _caretCol; } set { SetCaretPosition(_caretLine, value); } }
    public bool HasSelection { get { return _anchorLine != _caretLine || _anchorCol != _caretCol; } }

    private bool IsAnchorBeforeCaret() {
        return _anchorLine < _caretLine || (_anchorLine == _caretLine && _anchorCol <= _caretCol);
    }

    public int SelectionStartLine { get { return IsAnchorBeforeCaret() ? _anchorLine : _caretLine; } }
    public int SelectionStartColumn { get { return IsAnchorBeforeCaret() ? _anchorCol : _caretCol; } }
    public int SelectionEndLine { get { return IsAnchorBeforeCaret() ? _caretLine : _anchorLine; } }
    public int SelectionEndColumn { get { return IsAnchorBeforeCaret() ? _caretCol : _anchorCol; } }

    public int ScrollFirstLine {
        get { return _scrollFirstLine; }
        set {
            int maxFirst = EditorMath.Maxi(0, _lines.Length - 1);
            _scrollFirstLine = EditorMath.Clampi(value, 0, maxFirst);
            InvalidateEditor();
        }
    }
    public int ActiveExecutionLine {
        get { return _activeExecLine; }
        set { _activeExecLine = value; InvalidateEditor(); }
    }

    private int LineLen(string s) {
        return ParseUtil.Utf8CodePointCount(s);
    }

    private string LineSlice(string s, int start, int count) {
        return ParseUtil.SliceCodePoints(s, start, count);
    }

    private string LineTail(string s, int start) {
        int len = LineLen(s);
        if (start < 0) { start = 0; }
        if (start >= len) { return ""; }
        return LineSlice(s, start, len - start);
    }

    public string Text {
        get { return EditorMath.JoinLines(_lines); }
        set {
            string norm = (value == null ? "" : value).Replace("\r\n", "\n").Replace("\r", "\n");
            string[] split = norm.Split("\n");
            if (split == null || split.Length == 0) {
                _lines = new string[1];
                _lines[0] = "";
            } else {
                _lines = split;
            }
            _caretLine = EditorMath.Clampi(_caretLine, 0, _lines.Length - 1);
            _caretCol = EditorMath.Clampi(_caretCol, 0, LineLen(_lines[_caretLine]));
            _anchorLine = _caretLine;
            _anchorCol = _caretCol;
            _isDragSelecting = false;
            _scrollFirstLine = EditorMath.Clampi(_scrollFirstLine, 0, EditorMath.Maxi(0, _lines.Length - 1));
            BumpPropEpoch();
            InvalidateEditor();
        }
    }

    public string GetLineText(int zeroBasedLine) {
        if (zeroBasedLine < 0 || zeroBasedLine >= _lines.Length) { return ""; }
        return _lines[zeroBasedLine];
    }

    private void MoveCaretInternal(int line, int col, bool extendSelection) {
        _caretLine = EditorMath.Clampi(line, 0, _lines.Length - 1);
        _caretCol = EditorMath.Clampi(col, 0, LineLen(_lines[_caretLine]));
        if (!extendSelection) {
            _anchorLine = _caretLine;
            _anchorCol = _caretCol;
        }
        EnsureCaretScrolledIntoView();
        SyncCodeImeCaret();
        InvalidateEditor();
        CaretMoved(this, EventArgs.Empty());
    }

    public void SetCaretPosition(int line, int col) {
        MoveCaretInternal(line, col, false);
    }

    public void SetSelection(int startLine, int startCol, int endLine, int endCol) {
        int sLine = EditorMath.Clampi(startLine, 0, _lines.Length - 1);
        int sCol = EditorMath.Clampi(startCol, 0, LineLen(_lines[sLine]));
        int eLine = EditorMath.Clampi(endLine, 0, _lines.Length - 1);
        int eCol = EditorMath.Clampi(endCol, 0, LineLen(_lines[eLine]));
        _anchorLine = sLine;
        _anchorCol = sCol;
        _caretLine = eLine;
        _caretCol = eCol;
        EnsureCaretScrolledIntoView();
        SyncCodeImeCaret();
        InvalidateEditor();
        CaretMoved(this, EventArgs.Empty());
    }

    public void ClearSelection() {
        _anchorLine = _caretLine;
        _anchorCol = _caretCol;
        InvalidateEditor();
    }

    public void SelectAll() {
        int lastLine = _lines.Length - 1;
        SetSelection(0, 0, lastLine, LineLen(_lines[lastLine]));
    }

    public string GetSelectedText() {
        if (!HasSelection) { return ""; }
        int sLine = SelectionStartLine;
        int sCol = SelectionStartColumn;
        int eLine = SelectionEndLine;
        int eCol = SelectionEndColumn;
        if (sLine == eLine) {
            return LineSlice(_lines[sLine], sCol, eCol - sCol);
        }
        string acc = LineTail(_lines[sLine], sCol);
        for (int i = sLine + 1; i < eLine; i += 1) {
            acc = acc + "\n" + _lines[i];
        }
        acc = acc + "\n" + LineSlice(_lines[eLine], 0, eCol);
        return acc;
    }

    public void CopySelection() {
        string sel = GetSelectedText();
        if (sel.Length > 0) { Host_SetClipboardText(sel); }
    }

    public void CutSelection() {
        if (_isReadOnly || !HasSelection) { return; }
        CopySelection();
        DeleteSelectionInternal(true);
    }

    public void ToggleBreakpoint(int oneBasedLine) {
        if (oneBasedLine < 1) { return; }
        if (_breakpoints.ContainsKey(oneBasedLine) && _breakpoints[oneBasedLine]) {
            _breakpoints[oneBasedLine] = false;
        } else {
            _breakpoints[oneBasedLine] = true;
        }
        InvalidateEditor();
        BreakpointsChanged(this, EventArgs.Empty());
    }

    public bool HasBreakpoint(int oneBasedLine) {
        return _breakpoints.ContainsKey(oneBasedLine) && _breakpoints[oneBasedLine];
    }

    public void SetDiagnostics(CodeDiagnosticItem[] items) {
        _diagnostics = items == null ? new CodeDiagnosticItem[0] : items;
        InvalidateEditor();
    }

    public void ShowCompletions(string[] items) {
        _completions = items == null ? new string[0] : items;
        _completionIndex = 0;
        _completionVisible = _completions.Length > 0;
        InvalidateEditor();
    }

    public void HideCompletions() {
        _completionVisible = false;
        InvalidateEditor();
    }

    private int ResolveLangKind() {
        if (_language == "rs" || _language == "realscript" || _language == "cs") { return 1; }
        if (_language == "xaml" || _language == "xml") { return 2; }
        if (_language == "ascene" || _language == "adata" || _language == "agraph") { return 3; }
        return 0;
    }

    private float MeasurePrefixX(string lineStr, int col) {
        int lineLen = LineLen(lineStr);
        int safeCol = EditorMath.Clampi(col, 0, lineLen);
        if (safeCol <= 0) { return 0.0f; }
        string sub = LineSlice(lineStr, 0, safeCol);
        if (ParseUtil.IsNativeReady()) {
            return Visual_MeasureText(sub, FontFamily, FontSize, 1000000.0f, 0, 0);
        }
        return (float)safeCol * (FontSize * 0.6f);
    }

    private int HitTestLineIndex(float localY) {
        return EditorMath.Clampi(_scrollFirstLine + (int)EditorMath.Floorf((localY - 4.0f) / _lineHeight), 0, _lines.Length - 1);
    }

    private int HitTestColumnInLine(string lineStr, float textRelX) {
        int lineLen = LineLen(lineStr);
        if (textRelX <= 0.0f || lineLen <= 0) { return 0; }
        if (!ParseUtil.IsNativeReady()) {
            float approxCharW = Math.Maxf(6.0f, FontSize * 0.58f);
            return EditorMath.Clampi(EditorMath.RoundToInt(textRelX / approxCharW), 0, lineLen);
        }
        float totalW = MeasurePrefixX(lineStr, lineLen);
        if (textRelX >= totalW) { return lineLen; }
        int lo = 0;
        int hi = lineLen;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            float wMid = MeasurePrefixX(lineStr, mid);
            if (wMid < textRelX) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            float wPrev = MeasurePrefixX(lineStr, lo - 1);
            float wCur = MeasurePrefixX(lineStr, lo);
            if ((textRelX - wPrev) < (wCur - textRelX)) {
                return lo - 1;
            }
        }
        return lo;
    }

    private bool IsWordChar(string ch) {
        if (ch == null || ch.Length == 0) { return false; }
        if (ch.Length > 1) { return true; }
        return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_".Contains(ch);
    }

    private void EnsureCaretScrolledIntoView() {
        float vh = ActualHeight > 20.0f ? ActualHeight : 400.0f;
        int visibleRows = EditorMath.Maxi(1, (int)(vh / _lineHeight) - 1);
        if (_caretLine < _scrollFirstLine) {
            _scrollFirstLine = _caretLine;
        } else if (_caretLine >= _scrollFirstLine + visibleRows) {
            _scrollFirstLine = _caretLine - visibleRows + 1;
        }
    }

    private void SyncCodeImeCaret() {
        if (!ParseUtil.IsNativeReady()) { return; }
        if (!IsFocused || _isReadOnly) {
            Host_SyncImeRect(0.0f, 0.0f, 0.0f, 0.0f, false);
            return;
        }
        float localX = _gutterWidth + 8.0f + MeasurePrefixX(_lines[_caretLine], _caretCol);
        float localY = (float)(_caretLine - _scrollFirstLine) * _lineHeight + 4.0f;
        float wy = Visual_GetLayoutRect(__nativeHandle, 1) + localY;
        float wx = Visual_GetLayoutRect(__nativeHandle, 0) + localX;
        Host_SyncImeRect(wx, wy, 2.0f, _lineHeight, true);
    }

    private void InvalidateEditor() {
        if (_canvas != null) { _canvas.InvalidateDraw(); }
    }

    internal void RenderCodeCanvas(DrawingContext dc) {
        float w = Math.Maxf(120.0f, ActualWidth > 0.0f ? ActualWidth : 640.0f);
        float h = Math.Maxf(60.0f, ActualHeight > 0.0f ? ActualHeight : 360.0f);

        uint colBg = ParseUtil.PackArgb(255u, 11u, 17u, 32u);
        uint colGutter = ParseUtil.PackArgb(255u, 15u, 23u, 42u);
        uint colBorder = ParseUtil.PackArgb(255u, 30u, 41u, 59u);
        uint colCaretRow = ParseUtil.PackArgb(34u, 56u, 189u, 248u);
        uint colSelection = ParseUtil.PackArgb(115u, 56u, 189u, 248u);
        uint colExecRow = ParseUtil.PackArgb(68u, 250u, 204u, 21u);
        uint colRed = ParseUtil.PackArgb(255u, 239u, 68u, 68u);
        uint colAmber = ParseUtil.PackArgb(255u, 245u, 158u, 11u);
        uint colText = ParseUtil.PackArgb(255u, 226u, 232u, 240u);
        uint colMuted = ParseUtil.PackArgb(255u, 100u, 116u, 139u);
        uint colAccent = ParseUtil.PackArgb(255u, 56u, 189u, 248u);

        dc.FillRect(0.0f, 0.0f, w, h, colBg);
        dc.FillRect(0.0f, 0.0f, _gutterWidth, h, colGutter);
        dc.DrawLine(_gutterWidth, 0.0f, _gutterWidth, h, colBorder, 1.0f);

        int visibleCount = EditorMath.Maxi(1, (int)(h / _lineHeight) + 2);
        int endLine = EditorMath.Mini(_lines.Length, _scrollFirstLine + visibleCount);
        int langKind = ResolveLangKind();
        bool hasSel = HasSelection;
        int selSLine = hasSel ? SelectionStartLine : -1;
        int selSCol = hasSel ? SelectionStartColumn : 0;
        int selELine = hasSel ? SelectionEndLine : -1;
        int selECol = hasSel ? SelectionEndColumn : 0;

        for (int ln = _scrollFirstLine; ln < endLine; ln += 1) {
            float rowY = (float)(ln - _scrollFirstLine) * _lineHeight + 4.0f;
            int oneBased = ln + 1;

            if (!hasSel && ln == _caretLine) {
                dc.FillRect(_gutterWidth + 1.0f, rowY - 2.0f, w - _gutterWidth - 1.0f, _lineHeight, colCaretRow);
            }
            if (oneBased == _activeExecLine) {
                dc.FillRect(_gutterWidth + 1.0f, rowY - 2.0f, w - _gutterWidth - 1.0f, _lineHeight, colExecRow);
            }

            if (HasBreakpoint(oneBased)) {
                dc.FillEllipse(10.0f, rowY + (_lineHeight * 0.42f), 4.5f, 4.5f, colRed);
            }

            uint lnCol = (ln == _caretLine) ? colText : colMuted;
            dc.DrawText(
                EditorMath.IntToStr(oneBased),
                16.0f,
                rowY,
                _gutterWidth - 22.0f,
                _lineHeight,
                FontFamily,
                FontSize - 1.0f,
                lnCol,
                HorizontalAlignment.Right,
                VerticalAlignment.Top);

            string codeLine = _lines[ln];
            if (hasSel && ln >= selSLine && ln <= selELine) {
                int c0 = (ln == selSLine) ? selSCol : 0;
                int c1 = (ln == selELine) ? selECol : LineLen(codeLine);
                float x0 = _gutterWidth + 8.0f + MeasurePrefixX(codeLine, c0);
                float x1 = _gutterWidth + 8.0f + MeasurePrefixX(codeLine, c1);
                if (ln < selELine) {
                    x1 = Math.Minf(w - 2.0f, Math.Maxf(x1 + 8.0f, x0 + 8.0f));
                }
                if (x0 < _gutterWidth + 1.0f) { x0 = _gutterWidth + 1.0f; }
                if (x1 > w) { x1 = w; }
                if (x1 > x0) {
                    dc.FillRect(x0, rowY - 2.0f, x1 - x0, _lineHeight, colSelection);
                }
            }

            if (codeLine.Length > 0) {
                dc.DrawSyntaxLine(
                    codeLine,
                    _gutterWidth + 8.0f,
                    rowY,
                    w - _gutterWidth - 12.0f,
                    _lineHeight,
                    FontFamily,
                    langKind,
                    FontSize,
                    colText);
            }

            for (int di = 0; di < _diagnostics.Length; di += 1) {
                CodeDiagnosticItem diag = _diagnostics[di];
                if (diag.Line == oneBased) {
                    float sx = _gutterWidth + 8.0f + MeasurePrefixX(codeLine, diag.Column - 1);
                    float sw = Math.Maxf(18.0f, (float)diag.Length * (FontSize * 0.58f));
                    uint dCol = diag.Severity >= 2 ? colRed : colAmber;
                    dc.DrawLine(sx, rowY + _lineHeight - 3.0f, sx + sw, rowY + _lineHeight - 3.0f, dCol, 1.8f);
                }
            }
        }

        if (IsFocused && !_isReadOnly && _caretLine >= _scrollFirstLine && _caretLine < endLine) {
            float cx = _gutterWidth + 8.0f + MeasurePrefixX(_lines[_caretLine], _caretCol);
            float cy = (float)(_caretLine - _scrollFirstLine) * _lineHeight + 3.0f;
            dc.FillRect(cx, cy, 2.0f, _lineHeight - 2.0f, colAccent);
        }

        if (_completionVisible && _completions.Length > 0) {
            float px = Math.Minf(w - 210.0f, _gutterWidth + 8.0f + MeasurePrefixX(_lines[_caretLine], _caretCol));
            float py = (float)(_caretLine - _scrollFirstLine + 1) * _lineHeight + 4.0f;
            int showCnt = EditorMath.Mini(6, _completions.Length);
            float popH = (float)showCnt * 22.0f + 6.0f;
            uint popBg = ParseUtil.PackArgb(255u, 30u, 41u, 59u);
            uint popBorder = ParseUtil.PackArgb(255u, 71u, 85u, 105u);
            uint popSel = ParseUtil.PackArgb(255u, 2u, 132u, 199u);
            uint popFg = ParseUtil.PackArgb(255u, 248u, 250u, 252u);
            dc.FillRoundedRect(px, py, 200.0f, popH, 4.0f, 4.0f, popBg);
            dc.StrokeRoundedRect(px, py, 200.0f, popH, 4.0f, 4.0f, popBorder, 1.0f);
            for (int ci = 0; ci < showCnt; ci += 1) {
                float iy = py + 3.0f + (float)ci * 22.0f;
                if (ci == _completionIndex) {
                    dc.FillRoundedRect(px + 3.0f, iy, 194.0f, 20.0f, 3.0f, 3.0f, popSel);
                }
                dc.DrawText(_completions[ci], px + 8.0f, iy + 2.0f, 184.0f, 20.0f, FontFamily, 12.0f, popFg);
            }
        }
    }

    private bool DeleteSelectionInternal(bool notify) {
        if (_isReadOnly || !HasSelection) { return false; }
        int sLine = SelectionStartLine;
        int sCol = SelectionStartColumn;
        int eLine = SelectionEndLine;
        int eCol = SelectionEndColumn;

        string head = LineSlice(_lines[sLine], 0, sCol);
        string tail = LineTail(_lines[eLine], eCol);
        _lines[sLine] = head + tail;
        for (int r = eLine; r > sLine; r -= 1) {
            _lines.RemoveAt(r);
        }
        _caretLine = EditorMath.Clampi(sLine, 0, _lines.Length - 1);
        _caretCol = EditorMath.Clampi(sCol, 0, LineLen(_lines[_caretLine]));
        _anchorLine = _caretLine;
        _anchorCol = _caretCol;
        if (notify) {
            EnsureCaretScrolledIntoView();
            SyncCodeImeCaret();
            BumpPropEpoch();
            InvalidateEditor();
            CaretMoved(this, EventArgs.Empty());
            TextChanged(this, new TextChangedEventArgs(Text, this));
        }
        return true;
    }

    private void InsertTextAtCaret(string ins) {
        if (_isReadOnly || ins == null || ins.Length == 0) { return; }
        if (HasSelection) {
            DeleteSelectionInternal(false);
        }
        string cur = _lines[_caretLine];
        int curLen = LineLen(cur);
        int col = EditorMath.Clampi(_caretCol, 0, curLen);
        string left = LineSlice(cur, 0, col);
        string right = LineTail(cur, col);

        string clean = ins.Replace("\r\n", "\n").Replace("\r", "\n");
        string[] parts = clean.Split("\n");
        if (parts.Length <= 1) {
            _lines[_caretLine] = left + clean + right;
            _caretCol = col + LineLen(clean);
        } else {
            _lines[_caretLine] = left + parts[0];
            for (int p = 1; p < parts.Length - 1; p += 1) {
                _lines.Insert(_caretLine + p, parts[p]);
            }
            string lastPart = parts[parts.Length - 1];
            _lines.Insert(_caretLine + parts.Length - 1, lastPart + right);
            _caretLine += parts.Length - 1;
            _caretCol = LineLen(lastPart);
        }
        _anchorLine = _caretLine;
        _anchorCol = _caretCol;
        EnsureCaretScrolledIntoView();
        SyncCodeImeCaret();
        BumpPropEpoch();
        InvalidateEditor();
        CaretMoved(this, EventArgs.Empty());
        TextChanged(this, new TextChangedEventArgs(Text, this));
    }

    private void InsertSmartNewline() {
        if (_isReadOnly) { return; }
        if (HasSelection) {
            DeleteSelectionInternal(false);
        }
        string cur = _lines[_caretLine];
        int curLen = LineLen(cur);
        int spaces = 0;
        while (spaces < curLen && LineSlice(cur, spaces, 1) == " ") { spaces += 1; }
        string trimmed = LineSlice(cur, 0, EditorMath.Clampi(_caretCol, 0, curLen)).Trim();
        if (trimmed.EndsWith("{") || (trimmed.EndsWith(">") && !trimmed.EndsWith("/>") && !trimmed.StartsWith("</"))) {
            spaces += 4;
        }
        string indent = "";
        for (int i = 0; i < spaces; i += 1) { indent = indent + " "; }
        InsertTextAtCaret("\n" + indent);
    }

    private void BackspaceAtCaret() {
        if (_isReadOnly) { return; }
        if (HasSelection) {
            DeleteSelectionInternal(true);
            return;
        }
        string cur = _lines[_caretLine];
        if (_caretCol > 0) {
            _lines[_caretLine] = LineSlice(cur, 0, _caretCol - 1) + LineTail(cur, _caretCol);
            _caretCol -= 1;
        } else if (_caretLine > 0) {
            string prev = _lines[_caretLine - 1];
            int joinCol = LineLen(prev);
            _lines[_caretLine - 1] = prev + cur;
            _lines.RemoveAt(_caretLine);
            _caretLine -= 1;
            _caretCol = joinCol;
        } else {
            return;
        }
        _anchorLine = _caretLine;
        _anchorCol = _caretCol;
        EnsureCaretScrolledIntoView();
        SyncCodeImeCaret();
        BumpPropEpoch();
        InvalidateEditor();
        CaretMoved(this, EventArgs.Empty());
        TextChanged(this, new TextChangedEventArgs(Text, this));
    }

    private void DeleteAtCaret() {
        if (_isReadOnly) { return; }
        if (HasSelection) {
            DeleteSelectionInternal(true);
            return;
        }
        string cur = _lines[_caretLine];
        int curLen = LineLen(cur);
        if (_caretCol < curLen) {
            _lines[_caretLine] = LineSlice(cur, 0, _caretCol) + LineTail(cur, _caretCol + 1);
        } else if (_caretLine + 1 < _lines.Length) {
            _lines[_caretLine] = cur + _lines[_caretLine + 1];
            _lines.RemoveAt(_caretLine + 1);
        } else {
            return;
        }
        _anchorLine = _caretLine;
        _anchorCol = _caretCol;
        EnsureCaretScrolledIntoView();
        SyncCodeImeCaret();
        BumpPropEpoch();
        InvalidateEditor();
        TextChanged(this, new TextChangedEventArgs(Text, this));
    }

    public override void OnGotFocus() {
        base.OnGotFocus();
        SyncCodeImeCaret();
        InvalidateEditor();
    }

    public override void OnLostFocus() {
        base.OnLostFocus();
        if (_isDragSelecting) {
            _isDragSelecting = false;
            ReleaseMouseCapture();
        }
        SyncCodeImeCaret();
        InvalidateEditor();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        Focus();
        if (_completionVisible) { _completionVisible = false; }
        float lx = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 0) : e.X;
        float ly = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 1) : e.Y;
        int clickedLine = HitTestLineIndex(ly);
        if (lx < _gutterWidth) {
            ToggleBreakpoint(clickedLine + 1);
            e.Handled = true;
            return;
        }
        string lnText = _lines[clickedLine];
        int lineLen = LineLen(lnText);
        int col = HitTestColumnInLine(lnText, lx - (_gutterWidth + 8.0f));
        if (e.ClickCount >= 3) {
            if (clickedLine + 1 < _lines.Length) {
                SetSelection(clickedLine, 0, clickedLine + 1, 0);
            } else {
                SetSelection(clickedLine, 0, clickedLine, lineLen);
            }
        } else if (e.ClickCount == 2) {
            int probe = col >= lineLen && lineLen > 0 ? lineLen - 1 : col;
            if (lineLen > 0 && IsWordChar(LineSlice(lnText, probe, 1))) {
                int wStart = probe;
                int wEnd = probe + 1;
                while (wStart > 0 && IsWordChar(LineSlice(lnText, wStart - 1, 1))) { wStart -= 1; }
                while (wEnd < lineLen && IsWordChar(LineSlice(lnText, wEnd, 1))) { wEnd += 1; }
                SetSelection(clickedLine, wStart, clickedLine, wEnd);
            } else {
                MoveCaretInternal(clickedLine, col, false);
            }
        } else {
            bool shift = KeyModifiers.Has(e.Modifiers, KeyModifiers.Shift());
            MoveCaretInternal(clickedLine, col, shift);
            _isDragSelecting = true;
            CaptureMouse();
        }
        e.Handled = true;
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_isDragSelecting) { return; }
        float lx = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 0) : e.X;
        float ly = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 1) : e.Y;
        if (ly < 4.0f && _scrollFirstLine > 0) {
            _scrollFirstLine -= 1;
        } else if (ActualHeight > 20.0f && ly > ActualHeight - 4.0f && _scrollFirstLine < _lines.Length - 1) {
            _scrollFirstLine += 1;
        }
        int line = HitTestLineIndex(ly);
        int col = HitTestColumnInLine(_lines[line], lx - (_gutterWidth + 8.0f));
        MoveCaretInternal(line, col, true);
        e.Handled = true;
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (_isDragSelecting) {
            _isDragSelecting = false;
            ReleaseMouseCapture();
            e.Handled = true;
        }
    }

    public override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        int deltaLines = e.Delta > 0.0f ? -3 : 3;
        ScrollFirstLine = _scrollFirstLine + deltaLines;
        e.Handled = true;
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        bool shift = e.IsShiftDown;

        if (e.IsControlDown && e.Key == KeyCode.S) {
            SaveRequested(this, EventArgs.Empty());
            e.Handled = true;
            return;
        }
        if (e.IsControlDown && e.Key == KeyCode.A) {
            SelectAll();
            e.Handled = true;
            return;
        }
        if (e.IsControlDown && e.Key == KeyCode.C) {
            CopySelection();
            e.Handled = true;
            return;
        }
        if (e.IsControlDown && e.Key == KeyCode.X) {
            CutSelection();
            e.Handled = true;
            return;
        }
        if (e.IsControlDown && e.Key == KeyCode.V) {
            string clip = Host_GetClipboardText();
            if (clip != null && clip.Length > 0) { InsertTextAtCaret(clip); }
            e.Handled = true;
            return;
        }

        if (_completionVisible && _completions.Length > 0) {
            if (e.Key == KeyCode.Down) {
                _completionIndex = (_completionIndex + 1) % _completions.Length;
                InvalidateEditor();
                e.Handled = true;
                return;
            }
            if (e.Key == KeyCode.Up) {
                _completionIndex = (_completionIndex - 1 + _completions.Length) % _completions.Length;
                InvalidateEditor();
                e.Handled = true;
                return;
            }
            if (e.Key == KeyCode.Enter || e.Key == KeyCode.Tab) {
                string pick = _completions[_completionIndex];
                _completionVisible = false;
                InsertTextAtCaret(pick);
                e.Handled = true;
                return;
            }
            if (e.Key == KeyCode.Escape) {
                HideCompletions();
                e.Handled = true;
                return;
            }
        }

        if (e.Key == KeyCode.Escape && HasSelection) {
            ClearSelection();
            e.Handled = true;
            return;
        }
        if (e.Key == KeyCode.Left) {
            if (!shift && HasSelection) {
                MoveCaretInternal(SelectionStartLine, SelectionStartColumn, false);
            } else if (_caretCol > 0) {
                MoveCaretInternal(_caretLine, _caretCol - 1, shift);
            } else if (_caretLine > 0) {
                MoveCaretInternal(_caretLine - 1, LineLen(_lines[_caretLine - 1]), shift);
            }
            e.Handled = true;
            return;
        }
        if (e.Key == KeyCode.Right) {
            if (!shift && HasSelection) {
                MoveCaretInternal(SelectionEndLine, SelectionEndColumn, false);
            } else if (_caretCol < LineLen(_lines[_caretLine])) {
                MoveCaretInternal(_caretLine, _caretCol + 1, shift);
            } else if (_caretLine + 1 < _lines.Length) {
                MoveCaretInternal(_caretLine + 1, 0, shift);
            }
            e.Handled = true;
            return;
        }
        if (e.Key == KeyCode.Up) { MoveCaretInternal(_caretLine - 1, _caretCol, shift); e.Handled = true; return; }
        if (e.Key == KeyCode.Down) { MoveCaretInternal(_caretLine + 1, _caretCol, shift); e.Handled = true; return; }
        if (e.Key == KeyCode.Home) { MoveCaretInternal(_caretLine, 0, shift); e.Handled = true; return; }
        if (e.Key == KeyCode.End) { MoveCaretInternal(_caretLine, LineLen(_lines[_caretLine]), shift); e.Handled = true; return; }
        if (e.Key == KeyCode.Backspace) { BackspaceAtCaret(); e.Handled = true; return; }
        if (e.Key == KeyCode.Delete) { DeleteAtCaret(); e.Handled = true; return; }
        if (e.Key == KeyCode.Enter) { InsertSmartNewline(); e.Handled = true; return; }
        if (e.Key == KeyCode.Tab) { InsertTextAtCaret("    "); e.Handled = true; return; }
    }

    public override void OnTextInput(TextCompositionEventArgs e) {
        base.OnTextInput(e);
        if (!IsEnabled || _isReadOnly || e.Text == null || e.Text.Length == 0) { return; }
        string clean = e.Text.Replace("\r", "").Replace("\n", "");
        if (clean.Length > 0) {
            InsertTextAtCaret(clean);
            e.Handled = true;
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Text") { Text = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Language") { Language = DependencyObject.UnboxString(value, "rs"); return true; }
        if (propName == "IsReadOnly") { IsReadOnly = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Text") { return DependencyObject.BoxString(Text); }
        if (propName == "Language") { return DependencyObject.BoxString(_language); }
        if (propName == "IsReadOnly") { return DependencyObject.BoxBool(_isReadOnly); }
        return base.GetPropertyValue(propName);
    }
}

// ============================================================================
// 5. PropertyGrid & InspectorPropertyItem (Reflection Inspector + Override Bar)
// ============================================================================

public class InspectorPropertyItem : DependencyObject {
    public string Category { get; set; } = "General";
    public string PropertyPath { get; set; } = "";
    public string DisplayName { get; set; } = "";
    public string TypeKind { get; set; } = "float";
    public float FloatX { get; set; } = 0.0f;
    public float FloatY { get; set; } = 0.0f;
    public float FloatZ { get; set; } = 0.0f;
    public bool BoolValue { get; set; } = false;
    public string StringValue { get; set; } = "";
    public float MinValue { get; set; } = -1000000.0f;
    public float MaxValue { get; set; } = 1000000.0f;
    public float Step { get; set; } = 0.1f;
    public bool IsOverridden { get; set; } = false;
    public bool IsMixedValue { get; set; } = false;
    public bool IsReadOnly { get; set; } = false;

    public InspectorPropertyItem() {}
    public InspectorPropertyItem(string category, string path, string displayName, string typeKind) {
        Category = category == null ? "General" : category;
        PropertyPath = path == null ? "" : path;
        DisplayName = displayName == null ? PropertyPath : displayName;
        TypeKind = typeKind == null ? "float" : typeKind;
    }
}

public class PropertyGrid : Control {
    private InspectorPropertyItem[] _items;
    private string _filterText = "";
    private Grid _rootGrid;
    private TextBox _searchBox;
    private ScrollViewer _scrollViewer;
    private StackPanel _rowsPanel;

    public event ValueEditEventHandler PropertyEdited;
    public event ValueEditEventHandler PropertyCommitted;
    public event ValueEditEventHandler PropertyRevertRequested;

    public PropertyGrid() : base() {
        _items = new InspectorPropertyItem[0];
        Background = new SolidColorBrush("#FF0F172A");

        _rootGrid = new Grid();
        _rootGrid.SetRowDefinitions("36,*");
        AddChild(_rootGrid);

        var searchWrap = new Border();
        searchWrap.Padding = new Thickness(6.0f, 4.0f, 6.0f, 4.0f);
        searchWrap.Background = new SolidColorBrush("#FF0B1120");
        searchWrap.BorderBrush = new SolidColorBrush("#FF1E293B");
        searchWrap.BorderThickness = new Thickness(0.0f, 0.0f, 0.0f, 1.0f);
        UIElement.SetGridRow(searchWrap, 0);

        _searchBox = new TextBox();
        _searchBox.PlaceholderText = "Filter properties...";
        _searchBox.FontSize = 12.0f;
        _searchBox.Height = 26.0f;
        PropertyGrid self = this;
        TextBox sb = _searchBox;
        _searchBox.TextChanged += (object s, TextChangedEventArgs e) => {
            self.ApplySearchFilterText(sb.Text);
        };
        searchWrap.Child = _searchBox;
        _rootGrid.AddChild(searchWrap);

        _scrollViewer = new ScrollViewer();
        UIElement.SetGridRow(_scrollViewer, 1);
        _rowsPanel = new StackPanel();
        _rowsPanel.Orientation = Orientation.Vertical;
        _rowsPanel.Spacing = 2.0f;
        _rowsPanel.Margin = new Thickness(4.0f);
        _scrollViewer.Content = _rowsPanel;
        _rootGrid.AddChild(_scrollViewer);
    }

    public override string GetTypeName() { return "PropertyGrid"; }

    internal void ApplySearchFilterText(string txt) {
        _filterText = txt == null ? "" : txt.Trim().ToLower();
        RebuildRows();
    }

    internal void RaisePropertyEdited(ValueEditLifecycleEventArgs ev) { PropertyEdited(this, ev); }
    internal void RaisePropertyCommitted(ValueEditLifecycleEventArgs ev) { PropertyCommitted(this, ev); }
    internal void RaisePropertyRevert(ValueEditLifecycleEventArgs ev) { PropertyRevertRequested(this, ev); }

    public string FilterText {
        get { return _filterText; }
        set {
            _filterText = value == null ? "" : value.Trim().ToLower();
            if (_searchBox != null && _searchBox.Text != value) { _searchBox.Text = value; }
            RebuildRows();
        }
    }

    public int VisibleRowCount() {
        return _rowsPanel != null ? _rowsPanel.Children().Length : 0;
    }

    public void SetProperties(InspectorPropertyItem[] items) {
        _items = items == null ? new InspectorPropertyItem[0] : items;
        RebuildRows();
    }

    private bool MatchesFilter(InspectorPropertyItem item) {
        if (_filterText.Length == 0) { return true; }
        if (item.DisplayName.ToLower().Contains(_filterText)) { return true; }
        if (item.PropertyPath.ToLower().Contains(_filterText)) { return true; }
        if (item.Category.ToLower().Contains(_filterText)) { return true; }
        return false;
    }

    private void RebuildRows() {
        if (_rowsPanel == null) { return; }
        _rowsPanel.ClearChildren();
        string lastCategory = "";
        PropertyGrid self = this;

        for (int i = 0; i < _items.Length; i += 1) {
            InspectorPropertyItem item = _items[i];
            if (!MatchesFilter(item)) { continue; }

            if (item.Category != lastCategory) {
                lastCategory = item.Category;
                var catHeader = new Border();
                catHeader.Background = new SolidColorBrush("#FF1E293B");
                catHeader.CornerRadius = new CornerRadius(3.0f);
                catHeader.Padding = new Thickness(8.0f, 4.0f, 8.0f, 4.0f);
                catHeader.Margin = new Thickness(0.0f, 4.0f, 0.0f, 2.0f);
                var catLbl = new TextBlock();
                catLbl.Text = lastCategory;
                catLbl.FontSize = 11.0f;
                catLbl.FontWeight = "Bold";
                catLbl.Foreground = new SolidColorBrush("#FF94A3B8");
                catHeader.Child = catLbl;
                _rowsPanel.AddChild(catHeader);
            }

            var rowBorder = new Border();
            rowBorder.Padding = new Thickness(6.0f, 3.0f, 4.0f, 3.0f);
            rowBorder.Background = new SolidColorBrush("#FF111827");
            rowBorder.BorderBrush = new SolidColorBrush(item.IsOverridden ? "#FF38BDF8" : "#00000000");
            rowBorder.BorderThickness = new Thickness(3.0f, 0.0f, 0.0f, 0.0f);

            var rowGrid = new Grid();
            rowGrid.SetColumnDefinitions("120,*,Auto");
            rowGrid.ColumnSpacing = 6.0f;
            rowBorder.Child = rowGrid;

            var nameLbl = new TextBlock();
            nameLbl.Text = item.IsMixedValue ? (item.DisplayName + " (—)") : item.DisplayName;
            nameLbl.FontSize = 12.0f;
            nameLbl.FontWeight = item.IsOverridden ? "Bold" : "Normal";
            nameLbl.Foreground = new SolidColorBrush(item.IsOverridden ? "#FFF8FAFC" : "#FFCBD5E1");
            nameLbl.VerticalAlignment = VerticalAlignment.Center;
            UIElement.SetGridColumn(nameLbl, 0);
            rowGrid.AddChild(nameLbl);

            InspectorPropertyItem captured = item;
            if (item.TypeKind == "float" || item.TypeKind == "int") {
                var scrub = new NumericScrubber();
                scrub.Label = item.TypeKind == "int" ? "#" : "V";
                scrub.Decimals = item.TypeKind == "int" ? 0 : 2;
                scrub.Minimum = item.MinValue;
                scrub.Maximum = item.MaxValue;
                scrub.Step = item.TypeKind == "int" ? 1.0f : item.Step;
                scrub.Value = item.FloatX;
                scrub.IsReadOnly = item.IsReadOnly;
                scrub.PropertyPath = item.PropertyPath;
                scrub.ValueChanged += (object s, ValueEditLifecycleEventArgs ev) => {
                    captured.FloatX = ev.NewValue;
                    self.RaisePropertyEdited(ev);
                };
                scrub.EditCommitted += (object s, ValueEditLifecycleEventArgs ev) => {
                    captured.FloatX = ev.NewValue;
                    self.RaisePropertyCommitted(ev);
                };
                UIElement.SetGridColumn(scrub, 1);
                rowGrid.AddChild(scrub);
            } else if (item.TypeKind == "vec3") {
                var v3 = new Vector3Field();
                v3.Step = item.Step;
                v3.X = item.FloatX;
                v3.Y = item.FloatY;
                v3.Z = item.FloatZ;
                v3.VectorChanged += (object s, EventArgs ev) => {
                    captured.FloatX = v3.X;
                    captured.FloatY = v3.Y;
                    captured.FloatZ = v3.Z;
                    self.RaisePropertyEdited(new ValueEditLifecycleEventArgs(captured.PropertyPath, 0.0f, v3.X, EditLifecyclePhase.Change, self));
                };
                v3.EditCommitted += (object s, EventArgs ev) => {
                    captured.FloatX = v3.X;
                    captured.FloatY = v3.Y;
                    captured.FloatZ = v3.Z;
                    self.RaisePropertyCommitted(new ValueEditLifecycleEventArgs(captured.PropertyPath, 0.0f, v3.X, EditLifecyclePhase.Commit, self));
                };
                UIElement.SetGridColumn(v3, 1);
                rowGrid.AddChild(v3);
            } else if (item.TypeKind == "bool") {
                var cb = new CheckBox();
                cb.IsChecked = item.BoolValue;
                cb.Click += (object s, RoutedEventArgs ev) => {
                    captured.BoolValue = cb.IsChecked;
                    var outEv = new ValueEditLifecycleEventArgs(captured.PropertyPath, captured.BoolValue ? 0.0f : 1.0f, captured.BoolValue ? 1.0f : 0.0f, EditLifecyclePhase.Commit, self);
                    self.RaisePropertyEdited(outEv);
                    self.RaisePropertyCommitted(outEv);
                };
                UIElement.SetGridColumn(cb, 1);
                rowGrid.AddChild(cb);
            } else {
                var tb = new TextBox();
                tb.Text = item.StringValue;
                tb.IsReadOnly = item.IsReadOnly;
                tb.Height = 26.0f;
                tb.FontSize = 12.0f;
                tb.TextChanged += (object s, TextChangedEventArgs ev) => {
                    string oldTxt = captured.StringValue;
                    captured.StringValue = tb.Text;
                    var outEv = new ValueEditLifecycleEventArgs(captured.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, self);
                    outEv.OldText = oldTxt;
                    outEv.NewText = captured.StringValue;
                    self.RaisePropertyEdited(outEv);
                    self.RaisePropertyCommitted(outEv);
                };
                UIElement.SetGridColumn(tb, 1);
                rowGrid.AddChild(tb);
            }

            if (item.IsOverridden) {
                var revBtn = new Button();
                var revTxt = new TextBlock();
                revTxt.Text = "R";
                revTxt.FontSize = 10.0f;
                revBtn.Content = revTxt;
                revBtn.Width = 22.0f;
                revBtn.Height = 22.0f;
                revBtn.Padding = new Thickness(0.0f);
                revBtn.Click += (object s, RoutedEventArgs ev) => {
                    self.RaisePropertyRevert(new ValueEditLifecycleEventArgs(captured.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Cancel, self));
                };
                UIElement.SetGridColumn(revBtn, 2);
                rowGrid.AddChild(revBtn);
            }

            _rowsPanel.AddChild(rowBorder);
        }
    }
}

// ============================================================================
// 6. NodeGraphCanvas (Infinite Pan/Zoom Graph + Cubic Bezier SDF Wires)
// ============================================================================

public class NodeGraphNodeData : DependencyObject {
    public string NodeId { get; set; } = "";
    public string Title { get; set; } = "State";
    public float X { get; set; } = 80.0f;
    public float Y { get; set; } = 80.0f;
    public float Width { get; set; } = 160.0f;
    public float Height { get; set; } = 76.0f;
    public uint AccentRgba { get; set; } = 4278355143u;
    public string[] Inputs { get; set; }
    public string[] Outputs { get; set; }

    public NodeGraphNodeData() {
        AccentRgba = ParseUtil.PackArgb(255u, 2u, 132u, 199u);
        Inputs = new string[0];
        Outputs = new string[0];
    }
    public NodeGraphNodeData(string id, string title, float x, float y) {
        AccentRgba = ParseUtil.PackArgb(255u, 2u, 132u, 199u);
        Inputs = new string[0];
        Outputs = new string[0];
        NodeId = id == null ? "" : id;
        Title = title == null ? "Node" : title;
        X = x;
        Y = y;
    }
}

public class NodeGraphLinkData : DependencyObject {
    public string FromNodeId { get; set; } = "";
    public int FromPinIndex { get; set; } = 0;
    public string ToNodeId { get; set; } = "";
    public int ToPinIndex { get; set; } = 0;
    public uint WireRgba { get; set; } = 4281908728u;

    public NodeGraphLinkData() {
        WireRgba = ParseUtil.PackArgb(255u, 56u, 189u, 248u);
    }
    public NodeGraphLinkData(string fromNode, int fromPin, string toNode, int toPin) {
        WireRgba = ParseUtil.PackArgb(255u, 56u, 189u, 248u);
        FromNodeId = fromNode == null ? "" : fromNode;
        FromPinIndex = fromPin;
        ToNodeId = toNode == null ? "" : toNode;
        ToPinIndex = toPin;
    }
}

public class NodeGraphCanvas : Control {
    private NodeGraphNodeData[] _nodes;
    private NodeGraphLinkData[] _links;
    private float _panX = 0.0f;
    private float _panY = 0.0f;
    private float _zoom = 1.0f;
    private int _selectedNodeIdx = -1;
    private bool _draggingNode = false;
    private bool _panning = false;
    private float _lastMouseX = 0.0f;
    private float _lastMouseY = 0.0f;
    private CustomDrawElement _canvas;

    public event EventHandler GraphModified;

    public NodeGraphCanvas() : base() {
        Focusable = true;
        ClipToBounds = true;
        Background = new SolidColorBrush("#FF090D16");
        _nodes = new NodeGraphNodeData[0];
        _links = new NodeGraphLinkData[0];

        NodeGraphCanvas self = this;
        _canvas = new CustomDrawElement((DrawingContext dc) => { self.RenderGraph(dc); });
        _canvas.IsHitTestVisible = false;
        AddChild(_canvas);
    }

    public override string GetTypeName() { return "NodeGraphCanvas"; }

    public float PanX { get { return _panX; } set { _panX = value; InvalidateGraph(); } }
    public float PanY { get { return _panY; } set { _panY = value; InvalidateGraph(); } }
    public float Zoom { get { return _zoom; } set { _zoom = Math.Clampf(value, 0.25f, 2.5f); InvalidateGraph(); } }
    public int SelectedNodeIndex { get { return _selectedNodeIdx; } }

    public void SetGraph(NodeGraphNodeData[] nodes, NodeGraphLinkData[] links) {
        _nodes = nodes == null ? new NodeGraphNodeData[0] : nodes;
        _links = links == null ? new NodeGraphLinkData[0] : links;
        InvalidateGraph();
    }

    public void AddNode(NodeGraphNodeData node) {
        if (node == null) { return; }
        _nodes.Push(node);
        InvalidateGraph();
    }

    public void AddLink(NodeGraphLinkData link) {
        if (link == null) { return; }
        _links.Push(link);
        InvalidateGraph();
    }

    public void InvalidateGraph() {
        if (_canvas != null) { _canvas.InvalidateDraw(); }
    }

    private NodeGraphNodeData FindNode(string id) {
        for (int i = 0; i < _nodes.Length; i += 1) {
            if (_nodes[i].NodeId == id) { return _nodes[i]; }
        }
        return null;
    }

    private void DrawBezierWire(DrawingContext dc, float x0, float y0, float x3, float y3, uint rgba) {
        float dist = Math.Maxf(40.0f, Math.Absf(x3 - x0) * 0.5f);
        float x1 = x0 + dist;
        float y1 = y0;
        float x2 = x3 - dist;
        float y2 = y3;
        int segs = 16;
        float prevX = x0;
        float prevY = y0;
        for (int s = 1; s <= segs; s += 1) {
            float t = (float)s / (float)segs;
            float u = 1.0f - t;
            float bx = u * u * u * x0 + 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t * x3;
            float by = u * u * u * y0 + 3.0f * u * u * t * y1 + 3.0f * u * t * t * y2 + t * t * t * y3;
            dc.DrawLine(prevX, prevY, bx, by, rgba, 2.2f);
            prevX = bx;
            prevY = by;
        }
    }

    internal void RenderGraph(DrawingContext dc) {
        float w = Math.Maxf(100.0f, ActualWidth > 0.0f ? ActualWidth : 640.0f);
        float h = Math.Maxf(100.0f, ActualHeight > 0.0f ? ActualHeight : 360.0f);

        uint colBg = ParseUtil.PackArgb(255u, 9u, 13u, 22u);
        uint colGrid = ParseUtil.PackArgb(255u, 19u, 28u, 46u);
        uint colNodeBg = ParseUtil.PackArgb(238u, 17u, 24u, 39u);
        uint colSel = ParseUtil.PackArgb(255u, 56u, 189u, 248u);
        uint colBorder = ParseUtil.PackArgb(255u, 51u, 65u, 85u);
        uint colTitle = ParseUtil.PackArgb(255u, 248u, 250u, 252u);
        uint colPinOut = ParseUtil.PackArgb(255u, 52u, 211u, 153u);
        uint colPinText = ParseUtil.PackArgb(255u, 203u, 213u, 225u);

        dc.FillRect(0.0f, 0.0f, w, h, colBg);

        float step = 32.0f * _zoom;
        if (step >= 8.0f) {
            float ox = _panX - EditorMath.Floorf(_panX / step) * step;
            float oy = _panY - EditorMath.Floorf(_panY / step) * step;
            for (float gx = ox; gx < w; gx += step) {
                dc.DrawLine(gx, 0.0f, gx, h, colGrid, 1.0f);
            }
            for (float gy = oy; gy < h; gy += step) {
                dc.DrawLine(0.0f, gy, w, gy, colGrid, 1.0f);
            }
        }

        dc.PushTransform(_panX, _panY, _zoom, _zoom, 0.0f);

        for (int li = 0; li < _links.Length; li += 1) {
            NodeGraphLinkData lnk = _links[li];
            NodeGraphNodeData src = FindNode(lnk.FromNodeId);
            NodeGraphNodeData dst = FindNode(lnk.ToNodeId);
            if (src != null && dst != null) {
                float sx = src.X + src.Width;
                float sy = src.Y + 38.0f + (float)lnk.FromPinIndex * 18.0f;
                float dx = dst.X;
                float dy = dst.Y + 38.0f + (float)lnk.ToPinIndex * 18.0f;
                DrawBezierWire(dc, sx, sy, dx, dy, lnk.WireRgba);
            }
        }

        float viewMinX = (-_panX) / _zoom - 40.0f;
        float viewMinY = (-_panY) / _zoom - 40.0f;
        float viewMaxX = (w - _panX) / _zoom + 40.0f;
        float viewMaxY = (h - _panY) / _zoom + 40.0f;

        for (int ni = 0; ni < _nodes.Length; ni += 1) {
            NodeGraphNodeData nd = _nodes[ni];
            if (nd.X + nd.Width < viewMinX || nd.X > viewMaxX ||
                nd.Y + nd.Height < viewMinY || nd.Y > viewMaxY) {
                continue;
            }
            bool sel = (ni == _selectedNodeIdx);
            dc.FillRoundedRect(nd.X, nd.Y, nd.Width, nd.Height, 6.0f, 6.0f, colNodeBg);
            dc.FillRoundedRect(nd.X, nd.Y, nd.Width, 24.0f, 6.0f, 6.0f, nd.AccentRgba);
            dc.StrokeRoundedRect(nd.X, nd.Y, nd.Width, nd.Height, 6.0f, 6.0f, sel ? colSel : colBorder, sel ? 2.0f : 1.0f);
            dc.DrawText(nd.Title, nd.X + 8.0f, nd.Y + 4.0f, nd.Width - 16.0f, 20.0f, FontFamily, 12.0f, colTitle);

            for (int pi = 0; pi < nd.Inputs.Length; pi += 1) {
                float py = nd.Y + 38.0f + (float)pi * 18.0f;
                dc.FillEllipse(nd.X, py, 4.5f, 4.5f, colSel);
                dc.DrawText(nd.Inputs[pi], nd.X + 8.0f, py - 7.0f, nd.Width * 0.5f, 16.0f, FontFamily, 11.0f, colPinText);
            }
            for (int po = 0; po < nd.Outputs.Length; po += 1) {
                float py = nd.Y + 38.0f + (float)po * 18.0f;
                dc.FillEllipse(nd.X + nd.Width, py, 4.5f, 4.5f, colPinOut);
                dc.DrawText(nd.Outputs[po], nd.X + nd.Width * 0.45f, py - 7.0f, nd.Width * 0.5f - 8.0f, 16.0f, FontFamily, 11.0f, colPinText, HorizontalAlignment.Right);
            }
        }

        dc.PopTransform();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        Focus();
        float lx = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 0) : e.X;
        float ly = ParseUtil.IsValidHandle(__nativeHandle) ? Visual_ScreenToLocal(__nativeHandle, e.X, e.Y, 1) : e.Y;
        float gx = (lx - _panX) / _zoom;
        float gy = (ly - _panY) / _zoom;

        _selectedNodeIdx = -1;
        for (int i = _nodes.Length - 1; i >= 0; i -= 1) {
            NodeGraphNodeData nd = _nodes[i];
            if (gx >= nd.X && gx <= nd.X + nd.Width && gy >= nd.Y && gy <= nd.Y + nd.Height) {
                _selectedNodeIdx = i;
                break;
            }
        }
        _lastMouseX = e.X;
        _lastMouseY = e.Y;
        if (_selectedNodeIdx >= 0) {
            _draggingNode = true;
        } else {
            _panning = true;
        }
        CaptureMouse();
        InvalidateGraph();
        e.Handled = true;
    }

    public override void OnMouseRightButtonDown(MouseButtonEventArgs e) {
        base.OnMouseRightButtonDown(e);
        _panning = true;
        _lastMouseX = e.X;
        _lastMouseY = e.Y;
        CaptureMouse();
        e.Handled = true;
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        float dx = e.X - _lastMouseX;
        float dy = e.Y - _lastMouseY;
        _lastMouseX = e.X;
        _lastMouseY = e.Y;
        if (_draggingNode && _selectedNodeIdx >= 0 && _selectedNodeIdx < _nodes.Length) {
            _nodes[_selectedNodeIdx].X += dx / _zoom;
            _nodes[_selectedNodeIdx].Y += dy / _zoom;
            InvalidateGraph();
            GraphModified(this, EventArgs.Empty());
        } else if (_panning) {
            _panX += dx;
            _panY += dy;
            InvalidateGraph();
        }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        _draggingNode = false;
        _panning = false;
        ReleaseMouseCapture();
    }

    public override void OnMouseRightButtonUp(MouseButtonEventArgs e) {
        base.OnMouseRightButtonUp(e);
        _panning = false;
        ReleaseMouseCapture();
    }

    public override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        float factor = e.Delta > 0.0f ? 1.1f : 0.9f;
        Zoom = _zoom * factor;
        e.Handled = true;
    }
}
