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
        if (v <= (-2147483647 - 1)) { return "-2147483648"; }
        bool neg = v < 0;
        int n = neg ? -v : v;
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
    public float FloatW { get; set; } = 0.0f;
    public float ValueY { get; set; } = 0.0f;
    public float ValueZ { get; set; } = 0.0f;
    public float ValueW { get; set; } = 0.0f;
    public float ColorR { get; set; } = 0.0f;
    public float ColorG { get; set; } = 0.0f;
    public float ColorB { get; set; } = 0.0f;
    public float ColorA { get; set; } = 1.0f;
    public int IntValue { get; set; } = 0;

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

public class PropertyCardActionEventArgs : RoutedEventArgs {
    public string GroupKey { get; set; } = "";
    public string Category { get; set; } = "";
    public string Action { get; set; } = "";

    public PropertyCardActionEventArgs() : base("PropertyCardAction") {}
    public PropertyCardActionEventArgs(string groupKey, string category, string action, object source = null)
        : base("PropertyCardAction", source) {
        GroupKey = groupKey == null ? "" : groupKey;
        Category = category == null ? "" : category;
        Action = action == null ? "" : action;
    }
}

public delegate void PropertyCardActionEventHandler(object sender, PropertyCardActionEventArgs e);

public class PropertyAssetActionEventArgs : RoutedEventArgs {
    public string PropertyPath { get; set; } = "";
    public string Action { get; set; } = "";
    public string AssetFilter { get; set; } = "";
    public string AssetPath { get; set; } = "";

    public PropertyAssetActionEventArgs() : base("PropertyAssetAction") {}
    public PropertyAssetActionEventArgs(string propPath, string action, string filter, string path, object source = null)
        : base("PropertyAssetAction", source) {
        PropertyPath = propPath == null ? "" : propPath;
        Action = action == null ? "" : action;
        AssetFilter = filter == null ? "" : filter;
        AssetPath = path == null ? "" : path;
    }
}

public delegate void PropertyAssetActionEventHandler(object sender, PropertyAssetActionEventArgs e);

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
    private bool _isMixed = false;
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
        _inlineBox.LostFocus += (object s, RoutedEventArgs re) => {
            self.CommitInlineText();
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
            _valueText.Text = _isMixed ? "—" : FormatFloat(_value);
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
    public bool IsMixed {
        get { return _isMixed; }
        set {
            if (_isMixed == value) { return; }
            _isMixed = value;
            RefreshVisuals();
        }
    }
    public bool IsInlineEditing() { return _inlineEditing; }
    public TextBox InlineTextBox() { return _inlineBox; }
    public string DisplayedValueText() { return _valueText != null ? _valueText.Text : ""; }
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
        if (emitEvents) {
            _isMixed = false;
        }
        BumpPropEpoch();
        NotifyBoundPropertyChanged("Value", DependencyObject.BoxFloat(_value));
        RefreshVisuals();
        if (emitEvents) {
            var ev = new ValueEditLifecycleEventArgs(_propertyPath, prev, _value, phase, this);
            ev.IntValue = EditorMath.RoundToInt(_value);
            ValueChanged(this, ev);
            if (phase == EditLifecyclePhase.Commit) {
                var cev = new ValueEditLifecycleEventArgs(_propertyPath, _editStartValue, _value, EditLifecyclePhase.Commit, this);
                cev.IntValue = EditorMath.RoundToInt(_value);
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
        _inlineBox.PlaceholderText = _isMixed ? "—" : "";
        _inlineBox.Text = _isMixed ? "" : FormatFloat(_value);
        RefreshVisuals();
        _inlineBox.Focus();
        _inlineBox.SelectAll();
    }

    public void CommitInlineText() {
        if (!_inlineEditing) { return; }
        _inlineEditing = false;
        string raw = _inlineBox.Text == null ? "" : _inlineBox.Text.Trim();
        if (_isMixed && (raw.Length == 0 || raw == "—")) {
            RefreshVisuals();
            return;
        }
        float parsed = ParseUtil.ParseFloat(raw, _value);
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

    public NumericScrubber ScrubberX() { return _xBox; }
    public NumericScrubber ScrubberY() { return _yBox; }

    public float X { get { return _xBox.Value; } set { _xBox.Value = value; } }
    public float Y { get { return _yBox.Value; } set { _yBox.Value = value; } }
    public float Step {
        get { return _xBox.Step; }
        set { _xBox.Step = value; _yBox.Step = value; }
    }
    public bool IsMixed {
        get { return _xBox.IsMixed || _yBox.IsMixed; }
        set { _xBox.IsMixed = value; _yBox.IsMixed = value; }
    }
    public bool IsReadOnly {
        get { return _xBox.IsReadOnly; }
        set { _xBox.IsReadOnly = value; _yBox.IsReadOnly = value; }
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
    public bool IsMixed {
        get { return _xBox.IsMixed || _yBox.IsMixed || _zBox.IsMixed; }
        set { _xBox.IsMixed = value; _yBox.IsMixed = value; _zBox.IsMixed = value; }
    }
    public bool IsReadOnly {
        get { return _xBox.IsReadOnly; }
        set { _xBox.IsReadOnly = value; _yBox.IsReadOnly = value; _zBox.IsReadOnly = value; }
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

public class ColorPicker : Control {
    private float _r = 1.0f;
    private float _g = 1.0f;
    private float _b = 1.0f;
    private float _a = 1.0f;
    private bool _isHdr = false;
    private bool _isMixed = false;
    internal bool _isReadOnly = false;
    private string _propertyPath = "";
    private bool _syncing = false;

    private Grid _rootGrid;
    private Border _swatchBtn;
    private TextBlock _hdrBadge;
    private TextBox _hexBox;
    private Button _expandBtn;
    private Popup _popup;
    private Slider[] _sliders;
    private NumericScrubber[] _scrubbers;

    public event ValueEditEventHandler ColorChanged;
    public event ValueEditEventHandler EditCommitted;

    public ColorPicker() : base() {
        Height = 26.0f;
        _sliders = new Slider[4];
        _scrubbers = new NumericScrubber[4];
        BuildVisuals();
        RefreshVisuals();
    }

    public override string GetTypeName() { return "ColorPicker"; }

    private void BuildVisuals() {
        _rootGrid = new Grid();
        _rootGrid.ColumnSpacing = 4.0f;
        _rootGrid.SetColumnDefinitions("44,*,24");
        AddChild(_rootGrid);

        _swatchBtn = new Border();
        _swatchBtn.Height = 24.0f;
        _swatchBtn.CornerRadius = new CornerRadius(3.0f);
        _swatchBtn.BorderBrush = new SolidColorBrush("#FF475569");
        _swatchBtn.BorderThickness = new Thickness(1.0f);
        _hdrBadge = new TextBlock();
        _hdrBadge.Text = "HDR";
        _hdrBadge.FontSize = 9.0f;
        _hdrBadge.FontWeight = "Bold";
        _hdrBadge.Foreground = new SolidColorBrush("#FFFFFFFF");
        _hdrBadge.HorizontalAlignment = HorizontalAlignment.Center;
        _hdrBadge.VerticalAlignment = VerticalAlignment.Center;
        _hdrBadge.Visibility = Visibility.Collapsed;
        _swatchBtn.Child = _hdrBadge;
        UIElement.SetGridColumn(_swatchBtn, 0);
        _rootGrid.AddChild(_swatchBtn);

        _hexBox = new TextBox();
        _hexBox.Height = 24.0f;
        _hexBox.FontSize = 11.0f;
        _hexBox.Padding = new Thickness(4.0f, 2.0f, 4.0f, 2.0f);
        UIElement.SetGridColumn(_hexBox, 1);
        _rootGrid.AddChild(_hexBox);

        _expandBtn = new Button();
        var expTxt = new TextBlock();
        expTxt.Text = "▾";
        expTxt.FontSize = 10.0f;
        _expandBtn.Content = expTxt;
        _expandBtn.Width = 24.0f;
        _expandBtn.Height = 24.0f;
        _expandBtn.Padding = new Thickness(0.0f);
        UIElement.SetGridColumn(_expandBtn, 2);
        _rootGrid.AddChild(_expandBtn);

        _popup = new Popup();
        _popup.PlacementTarget = _swatchBtn;
        _popup.Placement = "Bottom";
        _popup.StaysOpen = false;

        var popPanel = new Border();
        popPanel.Width = 236.0f;
        popPanel.Background = new SolidColorBrush("#FF0B1120");
        popPanel.BorderBrush = new SolidColorBrush("#FF334155");
        popPanel.BorderThickness = new Thickness(1.0f);
        popPanel.CornerRadius = new CornerRadius(4.0f);
        popPanel.Padding = new Thickness(8.0f);

        var chStack = new StackPanel();
        chStack.Orientation = Orientation.Vertical;
        chStack.Spacing = 4.0f;

        string[] labels = new string[4];
        labels[0] = "R";
        labels[1] = "G";
        labels[2] = "B";
        labels[3] = "A";
        string[] colors = new string[4];
        colors[0] = "#FFEF4444";
        colors[1] = "#FF22C55E";
        colors[2] = "#FF3B82F6";
        colors[3] = "#FF94A3B8";

        ColorPicker self = this;
        for (int i = 0; i < 4; i += 1) {
            int chIdx = i;
            var row = new Grid();
            row.SetColumnDefinitions("*,68");
            row.ColumnSpacing = 6.0f;

            var sl = new Slider();
            sl.Minimum = 0.0f;
            sl.Maximum = (i < 3 && _isHdr) ? 16.0f : 1.0f;
            sl.SmallChange = 0.02f;
            sl.LargeChange = 0.1f;
            sl.VerticalAlignment = VerticalAlignment.Center;
            UIElement.SetGridColumn(sl, 0);
            row.AddChild(sl);

            var sc = new NumericScrubber();
            sc.Label = labels[i];
            sc.LabelBrush = new SolidColorBrush(colors[i]);
            sc.Minimum = 0.0f;
            sc.Maximum = (i < 3 && _isHdr) ? 16.0f : 1.0f;
            sc.Step = 0.02f;
            sc.Decimals = 2;
            UIElement.SetGridColumn(sc, 1);
            row.AddChild(sc);

            sl.ValueChanged += (object s, RoutedPropertyChangedEventArgs<float> e) => {
                self.OnChannelChangedFromSlider(chIdx, e.NewValue);
            };
            sl.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
                self.RaiseCommit();
            };
            sc.ValueChanged += (object s, ValueEditLifecycleEventArgs e) => {
                self.OnChannelChangedFromScrubber(chIdx, e.NewValue, false);
            };
            sc.EditCommitted += (object s, ValueEditLifecycleEventArgs e) => {
                self.OnChannelChangedFromScrubber(chIdx, e.NewValue, true);
            };

            _sliders[i] = sl;
            _scrubbers[i] = sc;
            chStack.AddChild(row);
        }

        popPanel.Child = chStack;
        _popup.Child = popPanel;

        _swatchBtn.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            if (!self._isReadOnly) {
                self.TogglePopup();
                e.Handled = true;
            }
        };
        _expandBtn.Click += (object s, RoutedEventArgs e) => {
            if (!self._isReadOnly) {
                self.TogglePopup();
            }
        };
        _hexBox.KeyDown += (object s, KeyEventArgs ke) => {
            if (ke.Key == KeyCode.Enter) {
                self.CommitHexInput();
                ke.Handled = true;
            }
        };
        _hexBox.LostFocus += (object s, RoutedEventArgs re) => {
            self.CommitHexInput();
        };
    }

    public void TogglePopup() {
        if (_popup == null) { return; }
        _popup.IsOpen = !_popup.IsOpen;
    }

    public bool IsPickerPopupOpen() {
        return _popup != null && _popup.IsOpen;
    }

    public void ClosePopup() {
        if (_popup != null) { _popup.IsOpen = false; }
    }

    public TextBox HexTextBox() { return _hexBox; }
    public Border SwatchBorder() { return _swatchBtn; }
    public Button ExpandButton() { return _expandBtn; }
    public NumericScrubber ChannelScrubber(int idx) {
        if (idx < 0 || idx >= 4) { return null; }
        return _scrubbers[idx];
    }

    public float R { get { return _r; } set { _r = Math.Maxf(0.0f, value); RefreshVisuals(); } }
    public float G { get { return _g; } set { _g = Math.Maxf(0.0f, value); RefreshVisuals(); } }
    public float B { get { return _b; } set { _b = Math.Maxf(0.0f, value); RefreshVisuals(); } }
    public float A { get { return _a; } set { _a = Math.Clampf(value, 0.0f, 1.0f); RefreshVisuals(); } }
    public bool IsHdr {
        get { return _isHdr; }
        set {
            if (_isHdr == value) { return; }
            _isHdr = value;
            UpdateChannelRanges();
            RefreshVisuals();
        }
    }
    public bool IsMixed {
        get { return _isMixed; }
        set {
            if (_isMixed == value) { return; }
            _isMixed = value;
            RefreshVisuals();
        }
    }
    public bool IsReadOnly {
        get { return _isReadOnly; }
        set {
            _isReadOnly = value;
            if (_hexBox != null) { _hexBox.IsReadOnly = value; }
            for (int i = 0; i < 4; i += 1) {
                if (_scrubbers[i] != null) { _scrubbers[i].IsReadOnly = value; }
            }
        }
    }
    public string PropertyPath {
        get { return _propertyPath; }
        set { _propertyPath = value == null ? "" : value; }
    }

    public void SetColor(float r, float g, float b, float a, bool emit = false) {
        float maxC = _isHdr ? 64.0f : 1.0f;
        _r = Math.Clampf(r, 0.0f, maxC);
        _g = Math.Clampf(g, 0.0f, maxC);
        _b = Math.Clampf(b, 0.0f, maxC);
        _a = Math.Clampf(a, 0.0f, 1.0f);
        if (emit) { _isMixed = false; }
        RefreshVisuals();
        if (emit) {
            RaiseEdit();
            RaiseCommit();
        }
    }

    private void UpdateChannelRanges() {
        float maxRgb = _isHdr ? 16.0f : 1.0f;
        for (int i = 0; i < 3; i += 1) {
            if (_sliders[i] != null) { _sliders[i].Maximum = maxRgb; }
            if (_scrubbers[i] != null) { _scrubbers[i].Maximum = maxRgb; }
        }
    }

    private static string FormatF2(float v) {
        bool neg = v < 0.0f;
        float av = neg ? -v : v;
        int scaled = EditorMath.RoundToInt(av * 100.0f);
        int whole = scaled / 100;
        int frac = scaled % 100;
        string fs = EditorMath.IntToStr(frac);
        if (fs.Length < 2) { fs = "0" + fs; }
        return (neg ? "-" : "") + EditorMath.IntToStr(whole) + "." + fs;
    }

    public string FormatColorPayload() {
        if (_isHdr || _r > 1.001f || _g > 1.001f || _b > 1.001f) {
            return FormatF2(_r) + "," + FormatF2(_g) + "," + FormatF2(_b) + "," + FormatF2(_a);
        }
        uint rB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(_r * 255.0f), 0, 255);
        uint gB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(_g * 255.0f), 0, 255);
        uint bB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(_b * 255.0f), 0, 255);
        uint aB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(_a * 255.0f), 0, 255);
        return ParseUtil.FormatArgbHex(ParseUtil.PackArgb(aB, rB, gB, bB));
    }

    private void RefreshVisuals() {
        _syncing = true;
        uint rB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(Math.Clampf(_r, 0.0f, 1.0f) * 255.0f), 0, 255);
        uint gB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(Math.Clampf(_g, 0.0f, 1.0f) * 255.0f), 0, 255);
        uint bB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(Math.Clampf(_b, 0.0f, 1.0f) * 255.0f), 0, 255);
        uint aB = (uint)EditorMath.Clampi(EditorMath.RoundToInt(Math.Clampf(_a, 0.0f, 1.0f) * 255.0f), 0, 255);
        if (_swatchBtn != null) {
            if (_isMixed) {
                _swatchBtn.Background = new SolidColorBrush("#FF334155");
            } else {
                _swatchBtn.Background = new SolidColorBrush(ParseUtil.PackArgb(aB, rB, gB, bB));
            }
        }
        if (_hdrBadge != null) {
            _hdrBadge.Visibility = _isHdr ? Visibility.Visible : Visibility.Collapsed;
        }
        if (_hexBox != null && !_hexBox.IsFocused) {
            _hexBox.PlaceholderText = _isMixed ? "—" : "#AARRGGBB";
            string targetHex = _isMixed ? "" : FormatColorPayload();
            if (_hexBox.Text != targetHex) {
                _hexBox.Text = targetHex;
            }
        }
        float[] vals = new float[4];
        vals[0] = _r; vals[1] = _g; vals[2] = _b; vals[3] = _a;
        for (int i = 0; i < 4; i += 1) {
            if (_sliders[i] != null) { _sliders[i].Value = Math.Clampf(vals[i], _sliders[i].Minimum, _sliders[i].Maximum); }
            if (_scrubbers[i] != null) {
                _scrubbers[i].IsMixed = _isMixed;
                _scrubbers[i].Value = vals[i];
            }
        }
        _syncing = false;
    }

    private ValueEditLifecycleEventArgs MakeArgs(EditLifecyclePhase phase) {
        var ev = new ValueEditLifecycleEventArgs(_propertyPath, 0.0f, _r, phase, this);
        ev.NewText = FormatColorPayload();
        ev.ColorR = _r;
        ev.ColorG = _g;
        ev.ColorB = _b;
        ev.ColorA = _a;
        ev.ValueY = _g;
        ev.ValueZ = _b;
        ev.ValueW = _a;
        ev.FloatW = _a;
        return ev;
    }

    internal void RaiseEdit() { ColorChanged(this, MakeArgs(EditLifecyclePhase.Change)); }
    internal void RaiseCommit() { EditCommitted(this, MakeArgs(EditLifecyclePhase.Commit)); }

    internal void OnChannelChangedFromSlider(int chIdx, float val) {
        if (_syncing || _isReadOnly) { return; }
        _isMixed = false;
        if (chIdx == 0) { _r = val; }
        else if (chIdx == 1) { _g = val; }
        else if (chIdx == 2) { _b = val; }
        else { _a = Math.Clampf(val, 0.0f, 1.0f); }
        RefreshVisuals();
        RaiseEdit();
    }

    internal void OnChannelChangedFromScrubber(int chIdx, float val, bool commit) {
        if (_syncing || _isReadOnly) { return; }
        _isMixed = false;
        if (chIdx == 0) { _r = Math.Maxf(0.0f, val); }
        else if (chIdx == 1) { _g = Math.Maxf(0.0f, val); }
        else if (chIdx == 2) { _b = Math.Maxf(0.0f, val); }
        else { _a = Math.Clampf(val, 0.0f, 1.0f); }
        RefreshVisuals();
        if (commit) {
            RaiseEdit();
            RaiseCommit();
        } else {
            RaiseEdit();
        }
    }

    internal void CommitHexInput() {
        if (_syncing || _isReadOnly || _hexBox == null) { return; }
        string txt = _hexBox.Text == null ? "" : _hexBox.Text.Trim();
        if (txt.Length == 0 || txt == "—") {
            RefreshVisuals();
            return;
        }
        if (txt.StartsWith("#")) {
            uint parsed = ParseUtil.ParseHexColor(txt, 0u);
            if (parsed == 0u && txt != "#00000000" && txt != "#0000") {
                RefreshVisuals();
                return;
            }
            uint aB = (parsed / 16777216u) % 256u;
            uint bB = (parsed / 65536u) % 256u;
            uint gB = (parsed / 256u) % 256u;
            uint rB = parsed % 256u;
            _r = (float)rB / 255.0f;
            _g = (float)gB / 255.0f;
            _b = (float)bB / 255.0f;
            _a = (float)aB / 255.0f;
            _isMixed = false;
            RefreshVisuals();
            RaiseEdit();
            RaiseCommit();
            return;
        }
        string[] parts = txt.Split(",");
        if (parts.Length >= 3) {
            float maxC = _isHdr ? 64.0f : 1.0f;
            _r = Math.Clampf(ParseUtil.ParseFloat(parts[0], _r), 0.0f, maxC);
            _g = Math.Clampf(ParseUtil.ParseFloat(parts[1], _g), 0.0f, maxC);
            _b = Math.Clampf(ParseUtil.ParseFloat(parts[2], _b), 0.0f, maxC);
            if (parts.Length >= 4) {
                _a = Math.Clampf(ParseUtil.ParseFloat(parts[3], _a), 0.0f, 1.0f);
            }
            _isMixed = false;
            RefreshVisuals();
            RaiseEdit();
            RaiseCommit();
            return;
        }
        RefreshVisuals();
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
    private static int _nextGroupSeqId = 1;
    internal int _groupSeqId = 0;
    private DockPane[] _panes;
    private int _selectedIndex = -1;
    private Grid _rootGrid;
    private StackPanel _tabStrip;
    private Border _contentHost;
    internal DockingWorkspace _ownerWorkspace;
    internal string _zoneTag = "D";
    private bool _maximizedDetached = false;

    public event EventHandler ActivePaneChanged;

    public DockTabGroup() : base() {
        _groupSeqId = _nextGroupSeqId;
        _nextGroupSeqId += 1;
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
    internal UIElement TabStripPanel() { return _tabStrip; }
    public UIElement[] TabButtons() { return _tabStrip != null ? _tabStrip.Children() : new UIElement[0]; }

    public int IndexOfPane(string paneId) {
        if (paneId == null || paneId.Length == 0) { return -1; }
        for (int i = 0; i < _panes.Length; i += 1) {
            if (_panes[i].PaneId == paneId) { return i; }
        }
        return -1;
    }

    public bool MovePaneToIndex(int fromIdx, int toIdx) {
        if (fromIdx < 0 || fromIdx >= _panes.Length || toIdx < 0 || toIdx >= _panes.Length || fromIdx == toIdx) {
            return false;
        }
        DockPane active = ActivePane();
        DockPane moved = _panes[fromIdx];
        _panes.RemoveAt(fromIdx);
        _panes.Insert(toIdx, moved);
        if (active != null) {
            for (int i = 0; i < _panes.Length; i += 1) {
                if (_panes[i].PaneId == active.PaneId) {
                    _selectedIndex = i;
                    break;
                }
            }
        }
        RebuildTabsAndContent();
        return true;
    }

    internal void ClearAllPanesSilent() {
        _panes.Clear();
        _selectedIndex = -1;
        RebuildTabsAndContent();
    }

    internal void SetMaximizedDetached(bool detached) {
        _maximizedDetached = detached;
        RebuildTabsAndContent();
    }

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
        int existing = IndexOfPane(pane.PaneId);
        if (existing >= 0) {
            if (selectIt) { SelectedIndex = existing; }
            return;
        }
        _panes.Push(pane);
        bool selChanged = false;
        if (_selectedIndex < 0 || selectIt) {
            _selectedIndex = _panes.Length - 1;
            selChanged = true;
        }
        RebuildTabsAndContent();
        if (selChanged) {
            ActivePaneChanged(this, EventArgs.Empty());
        }
    }

    public bool RemovePaneById(string paneId) {
        if (paneId == null || paneId.Length == 0) { return false; }
        for (int i = 0; i < _panes.Length; i += 1) {
            if (_panes[i].PaneId == paneId) {
                _panes.RemoveAt(i);
                if (i < _selectedIndex) {
                    _selectedIndex -= 1;
                } else if (_selectedIndex >= _panes.Length) {
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
                if (_selectedIndex == i) {
                    ActivePaneChanged(this, EventArgs.Empty());
                } else {
                    SelectedIndex = i;
                }
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
                if (self._ownerWorkspace != null) {
                    self._ownerWorkspace.PrepareTabDragSlots(self);
                }
                self.SelectedIndex = capturedIdx;
                if (self._ownerWorkspace != null) {
                    self._ownerWorkspace.BeginTabDragCandidate(self, p, e.X, e.Y);
                }
                e.Handled = true;
            };
            _tabStrip.AddChild(tabBtn);
        }

        _contentHost.Child = null;
        if (_maximizedDetached) { return; }
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
    private static int _nextSplitSeqId = 1;
    internal int _splitSeqId = 0;
    private Orientation _orientation = Orientation.Horizontal;
    private float _splitRatio = 0.5f;
    private UIElement _first;
    private UIElement _second;
    private Grid _grid;
    private Thumb _splitter;

    public DockSplitNode() : base() {
        _splitSeqId = _nextSplitSeqId;
        _nextSplitSeqId += 1;
        InitSplitVisuals();
        RebuildSplitLayout();
    }

    public DockSplitNode(Orientation orientation, float ratio, UIElement first, UIElement second) : base() {
        _splitSeqId = _nextSplitSeqId;
        _nextSplitSeqId += 1;
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

    private static bool SameSplitChild(UIElement a, UIElement b) {
        if (a == null && b == null) { return true; }
        if (a == null || b == null) { return false; }
        if (a is DockTabGroup && b is DockTabGroup) {
            return (a as DockTabGroup)._groupSeqId == (b as DockTabGroup)._groupSeqId;
        }
        if (a is DockSplitNode && b is DockSplitNode) {
            return (a as DockSplitNode)._splitSeqId == (b as DockSplitNode)._splitSeqId;
        }
        return a.__nativeHandle == b.__nativeHandle;
    }

    internal bool ReplaceChild(UIElement oldChild, UIElement newChild) {
        if (SameSplitChild(_first, oldChild)) {
            _first = newChild;
            RebuildSplitLayout();
            return true;
        }
        if (SameSplitChild(_second, oldChild)) {
            _second = newChild;
            RebuildSplitLayout();
            return true;
        }
        return false;
    }

    internal void DetachChildrenForCollapse() {
        _first = null;
        _second = null;
        if (_grid != null) {
            _grid.ClearChildren();
        }
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

internal class DockLayoutNodeDesc {
    public bool IsSplit = false;
    public Orientation Orient = Orientation.Horizontal;
    public float Ratio = 0.5f;
    public DockLayoutNodeDesc First;
    public DockLayoutNodeDesc Second;
    public string ZoneTag = "D";
    public string ActivePaneId = "";
    public string[] PaneIds;

    public DockLayoutNodeDesc() {
        PaneIds = new string[0];
    }
}

internal class DockParseCursor {
    public string Src = "";
    public int Pos = 0;
    public bool Failed = false;
}

public class DockingWorkspace : Control {
    private static int _nextWorkspaceSeqId = 1;
    internal int _workspaceSeqId = 0;
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
    private Dictionary<string, int> _paneDefaultZones;

    private float _savedLeftRatio = 0.20f;
    private float _savedRightRatio = 0.74f;
    private float _savedBottomRatio = 0.72f;

    // Maximize / Restore host
    private string _maximizedPaneId = "";
    internal string _lastActivatedPaneId = "";
    private Border _maximizedHost;
    private TextBlock _maximizedTitle;
    private Border _maximizedContentHost;

    // Tab DnD & Dock Hints Overlay
    private Canvas _dockOverlayCanvas;
    private Border _dropPreviewBox;
    private Border _hintCenter;
    private Border _hintLeft;
    private Border _hintRight;
    private Border _hintTop;
    private Border _hintBottom;
    private Border _dragGhost;
    private TextBlock _dragGhostText;

    private bool _dragCandidateActive = false;
    private bool _isDraggingPane = false;
    private DockTabGroup _dragSourceGroup;
    private DockPane _dragPane;
    private string _dragPaneId = "";
    private float _dragStartX = 0.0f;
    private float _dragStartY = 0.0f;
    private DockTabGroup _dropTargetGroup;
    private DockZone _hoveredDropZone = DockZone.Center;
    private float _dragTabStripOriginX = 0.0f;
    private float _dragGrabOffsetX = 0.0f;
    private float[] _dragTabSlotWidths;
    private bool _dragTabSlotValid = false;
    private int _dragTabSlotGroupSeqId = -1;

    public event EventHandler MaximizedStateChanged;

    public DockingWorkspace() : base() {
        _workspaceSeqId = _nextWorkspaceSeqId;
        _nextWorkspaceSeqId += 1;
        Background = new SolidColorBrush("#FF090D16");
        _panesById = new Dictionary<string, DockPane>();
        _paneDefaultZones = new Dictionary<string, int>();
        _dragTabSlotWidths = new float[0];

        _hostGrid = new Grid();
        AddChild(_hostGrid);

        BuildMaximizeAndOverlayLayers();
        ResetDefaultIdeLayout();
    }

    public override string GetTypeName() { return "DockingWorkspace"; }

    private Border CreateHintBadge(string glyph) {
        var b = new Border();
        b.Width = 26.0f;
        b.Height = 26.0f;
        b.CornerRadius = new CornerRadius(4.0f);
        b.Background = new SolidColorBrush("#CC0F172A");
        b.BorderBrush = new SolidColorBrush("#FF38BDF8");
        b.BorderThickness = new Thickness(1.5f);
        b.IsHitTestVisible = false;
        var t = new TextBlock();
        t.Text = glyph;
        t.FontSize = 11.0f;
        t.FontWeight = "Bold";
        t.Foreground = new SolidColorBrush("#FFF8FAFC");
        t.HorizontalAlignment = HorizontalAlignment.Center;
        t.VerticalAlignment = VerticalAlignment.Center;
        b.Child = t;
        return b;
    }

    private void BuildMaximizeAndOverlayLayers() {
        _maximizedHost = new Border();
        _maximizedHost.Background = new SolidColorBrush("#FF0F172A");
        _maximizedHost.BorderBrush = new SolidColorBrush("#FF38BDF8");
        _maximizedHost.BorderThickness = new Thickness(1.0f);
        _maximizedHost.Visibility = Visibility.Collapsed;
        _maximizedHost.ZIndex = 100;
        UIElement.SetGridRow(_maximizedHost, 0);
        UIElement.SetGridColumn(_maximizedHost, 0);

        var maxGrid = new Grid();
        maxGrid.SetRowDefinitions("28,*");
        _maximizedHost.Child = maxGrid;

        var maxBar = new Border();
        maxBar.Background = new SolidColorBrush("#FF0B1120");
        maxBar.BorderBrush = new SolidColorBrush("#FF1E293B");
        maxBar.BorderThickness = new Thickness(0.0f, 0.0f, 0.0f, 1.0f);
        maxBar.Padding = new Thickness(8.0f, 2.0f, 6.0f, 2.0f);
        UIElement.SetGridRow(maxBar, 0);

        var maxBarGrid = new Grid();
        maxBarGrid.SetColumnDefinitions("*,Auto");
        _maximizedTitle = new TextBlock();
        _maximizedTitle.Text = "";
        _maximizedTitle.FontSize = 12.0f;
        _maximizedTitle.FontWeight = "Bold";
        _maximizedTitle.Foreground = new SolidColorBrush("#FFF8FAFC");
        _maximizedTitle.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(_maximizedTitle, 0);
        maxBarGrid.AddChild(_maximizedTitle);

        var restoreBtn = new Button();
        var restoreTxt = new TextBlock();
        restoreTxt.Text = "Restore";
        restoreTxt.FontSize = 11.0f;
        restoreBtn.Content = restoreTxt;
        restoreBtn.Height = 22.0f;
        restoreBtn.Padding = new Thickness(8.0f, 0.0f, 8.0f, 0.0f);
        DockingWorkspace self = this;
        restoreBtn.Click += (object s, RoutedEventArgs e) => {
            self.RestoreMaximizedPane();
        };
        UIElement.SetGridColumn(restoreBtn, 1);
        maxBarGrid.AddChild(restoreBtn);
        maxBar.Child = maxBarGrid;
        maxGrid.AddChild(maxBar);

        _maximizedContentHost = new Border();
        _maximizedContentHost.Background = new SolidColorBrush("#FF0F172A");
        UIElement.SetGridRow(_maximizedContentHost, 1);
        maxGrid.AddChild(_maximizedContentHost);

        _dockOverlayCanvas = new Canvas();
        _dockOverlayCanvas.IsHitTestVisible = false;
        _dockOverlayCanvas.Visibility = Visibility.Collapsed;
        _dockOverlayCanvas.ZIndex = 9000;
        UIElement.SetGridRow(_dockOverlayCanvas, 0);
        UIElement.SetGridColumn(_dockOverlayCanvas, 0);

        _dropPreviewBox = new Border();
        _dropPreviewBox.Background = new SolidColorBrush("#4038BDF8");
        _dropPreviewBox.BorderBrush = new SolidColorBrush("#FF38BDF8");
        _dropPreviewBox.BorderThickness = new Thickness(2.0f);
        _dropPreviewBox.CornerRadius = new CornerRadius(3.0f);
        _dropPreviewBox.IsHitTestVisible = false;
        _dropPreviewBox.Visibility = Visibility.Collapsed;
        _dockOverlayCanvas.AddChild(_dropPreviewBox);

        _hintCenter = CreateHintBadge("C");
        _hintLeft = CreateHintBadge("L");
        _hintRight = CreateHintBadge("R");
        _hintTop = CreateHintBadge("T");
        _hintBottom = CreateHintBadge("B");
        _dockOverlayCanvas.AddChild(_hintCenter);
        _dockOverlayCanvas.AddChild(_hintLeft);
        _dockOverlayCanvas.AddChild(_hintRight);
        _dockOverlayCanvas.AddChild(_hintTop);
        _dockOverlayCanvas.AddChild(_hintBottom);

        _dragGhost = new Border();
        _dragGhost.Background = new SolidColorBrush("#E61E293B");
        _dragGhost.BorderBrush = new SolidColorBrush("#FF38BDF8");
        _dragGhost.BorderThickness = new Thickness(1.0f);
        _dragGhost.CornerRadius = new CornerRadius(3.0f);
        _dragGhost.Padding = new Thickness(8.0f, 3.0f, 8.0f, 3.0f);
        _dragGhost.IsHitTestVisible = false;
        _dragGhostText = new TextBlock();
        _dragGhostText.FontSize = 11.0f;
        _dragGhostText.Foreground = new SolidColorBrush("#FFF8FAFC");
        _dragGhost.Child = _dragGhostText;
        _dockOverlayCanvas.AddChild(_dragGhost);

        _hostGrid.AddChild(_maximizedHost);
        _hostGrid.AddChild(_dockOverlayCanvas);
    }

    private void WireGroup(DockTabGroup grp, string zoneTag) {
        if (grp == null) { return; }
        grp._ownerWorkspace = this;
        grp._zoneTag = zoneTag;
        DockingWorkspace self = this;
        DockTabGroup capturedGrp = grp;
        grp.ActivePaneChanged += (object s, EventArgs e) => {
            DockPane ap = capturedGrp.ActivePane();
            if (ap != null && ap.PaneId != null && ap.PaneId.Length > 0) {
                self._lastActivatedPaneId = ap.PaneId;
            }
        };
    }

    private static bool SameGroup(DockTabGroup a, DockTabGroup b) {
        if (a == null && b == null) { return true; }
        if (a == null || b == null) { return false; }
        return a._groupSeqId == b._groupSeqId;
    }

    private static bool SameSplit(DockSplitNode a, DockSplitNode b) {
        if (a == null && b == null) { return true; }
        if (a == null || b == null) { return false; }
        return a._splitSeqId == b._splitSeqId;
    }

    private static bool NodeIsGroup(UIElement node, DockTabGroup g) {
        if (node == null || g == null) { return false; }
        if (!(node is DockTabGroup)) { return false; }
        return (node as DockTabGroup)._groupSeqId == g._groupSeqId;
    }

    private static bool NodeIsSplit(UIElement node, DockSplitNode s) {
        if (node == null || s == null) { return false; }
        if (!(node is DockSplitNode)) { return false; }
        return (node as DockSplitNode)._splitSeqId == s._splitSeqId;
    }

    private static bool SameUi(UIElement a, UIElement b) {
        if (a == null && b == null) { return true; }
        if (a == null || b == null) { return false; }
        if (a is DockTabGroup && b is DockTabGroup) {
            return (a as DockTabGroup)._groupSeqId == (b as DockTabGroup)._groupSeqId;
        }
        if (a is DockSplitNode && b is DockSplitNode) {
            return (a as DockSplitNode)._splitSeqId == (b as DockSplitNode)._splitSeqId;
        }
        return a.__nativeHandle == b.__nativeHandle;
    }

    private static int ZoneToInt(DockZone zone) {
        if (zone == DockZone.Left) { return 1; }
        if (zone == DockZone.Right) { return 2; }
        if (zone == DockZone.Top) { return 3; }
        if (zone == DockZone.Bottom) { return 4; }
        return 0;
    }

    private void WireTreeGroups(UIElement node) {
        if (node == null) { return; }
        if (node is DockTabGroup) {
            var grp = node as DockTabGroup;
            if (grp._ownerWorkspace == null || grp._ownerWorkspace._workspaceSeqId != _workspaceSeqId) {
                WireGroup(grp, grp._zoneTag);
            }
            return;
        }
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            WireTreeGroups(sp.First);
            WireTreeGroups(sp.Second);
        }
    }

    public void ResetDefaultIdeLayout() {
        if (_maximizedPaneId.Length > 0) {
            RestoreMaximizedPane();
        }
        string[] keys = CollectPaneIdKeys();

        _leftGroup = new DockTabGroup();
        _centerGroup = new DockTabGroup();
        _rightGroup = new DockTabGroup();
        _bottomGroup = new DockTabGroup();

        WireGroup(_leftGroup, "L");
        WireGroup(_centerGroup, "C");
        WireGroup(_rightGroup, "R");
        WireGroup(_bottomGroup, "B");

        _centerBottomSplit = new DockSplitNode(Orientation.Vertical, _savedBottomRatio, _centerGroup, _bottomGroup);
        _centerRightSplit = new DockSplitNode(Orientation.Horizontal, _savedRightRatio, _centerBottomSplit, _rightGroup);
        _mainHorizSplit = new DockSplitNode(Orientation.Horizontal, _savedLeftRatio, _leftGroup, _centerRightSplit);
        SetRootDockNode(_mainHorizSplit);

        for (int i = 0; i < keys.Length; i += 1) {
            string pid = keys[i];
            if (!_panesById.ContainsKey(pid)) { continue; }
            DockPane p = _panesById[pid];
            DockZone z = DockZone.Center;
            if (_paneDefaultZones.ContainsKey(pid)) {
                int zi = _paneDefaultZones[pid];
                if (zi == 1) { z = DockZone.Left; }
                else if (zi == 2) { z = DockZone.Right; }
                else if (zi == 3) { z = DockZone.Top; }
                else if (zi == 4) { z = DockZone.Bottom; }
            }
            ZoneGroup(z).AddPane(p, false);
        }
    }

    private string[] CollectPaneIdKeys() {
        var keys = new string[0];
        DockTabGroup[] groups = CollectAllTabGroups();
        for (int gi = 0; gi < groups.Length; gi += 1) {
            DockPane[] ps = groups[gi].Panes();
            for (int pi = 0; pi < ps.Length; pi += 1) {
                if (ps[pi].PaneId != null && ps[pi].PaneId.Length > 0) {
                    keys.Push(ps[pi].PaneId);
                }
            }
        }
        return keys;
    }

    public UIElement RootDockNode() { return _rootNode; }

    public void SetRootDockNode(UIElement node) {
        if (_rootNode != null && _hostGrid != null) {
            _hostGrid.RemoveChild(_rootNode);
        }
        _rootNode = node;
        if (_rootNode != null && _hostGrid != null) {
            UIElement.SetGridRow(_rootNode, 0);
            UIElement.SetGridColumn(_rootNode, 0);
            _hostGrid.InsertChild(0, _rootNode);
            if (_maximizedPaneId.Length > 0) {
                _rootNode.Visibility = Visibility.Collapsed;
            } else {
                _rootNode.Visibility = Visibility.Visible;
            }
            WireTreeGroups(_rootNode);
        }
    }

    public DockTabGroup ZoneGroup(DockZone zone) {
        if (zone == DockZone.Left) { return _leftGroup; }
        if (zone == DockZone.Right) { return _rightGroup; }
        if (zone == DockZone.Bottom) { return _bottomGroup; }
        return _centerGroup;
    }

    private void CollectTabGroupsRecursive(UIElement node, DockTabGroup[] outGroups) {
        if (node == null) { return; }
        if (node is DockTabGroup) {
            outGroups.Push(node as DockTabGroup);
            return;
        }
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            CollectTabGroupsRecursive(sp.First, outGroups);
            CollectTabGroupsRecursive(sp.Second, outGroups);
        }
    }

    private DockTabGroup[] CollectAllTabGroups() {
        var res = new DockTabGroup[0];
        CollectTabGroupsRecursive(_rootNode, res);
        return res;
    }

    private bool IsNodeInTree(UIElement cur, UIElement target) {
        if (cur == null || target == null) { return false; }
        if (SameUi(cur, target)) { return true; }
        if (cur is DockSplitNode) {
            var sp = cur as DockSplitNode;
            return IsNodeInTree(sp.First, target) || IsNodeInTree(sp.Second, target);
        }
        return false;
    }

    private DockSplitNode FindParentSplit(UIElement cur, UIElement target) {
        if (cur == null || target == null) { return null; }
        if (cur is DockSplitNode) {
            var sp = cur as DockSplitNode;
            if (SameUi(sp.First, target) || SameUi(sp.Second, target)) { return sp; }
            DockSplitNode leftFound = FindParentSplit(sp.First, target);
            if (leftFound != null) { return leftFound; }
            return FindParentSplit(sp.Second, target);
        }
        return null;
    }

    private DockTabGroup FindGroupContainingPane(string paneId) {
        if (paneId == null || paneId.Length == 0) { return null; }
        DockTabGroup[] groups = CollectAllTabGroups();
        for (int i = 0; i < groups.Length; i += 1) {
            if (groups[i].IndexOfPane(paneId) >= 0) {
                return groups[i];
            }
        }
        return null;
    }

    private void CollapseEmptyGroup(DockTabGroup emptyGroup) {
        if (emptyGroup == null || NodeIsGroup(_rootNode, emptyGroup)) { return; }
        DockSplitNode parent = FindParentSplit(_rootNode, emptyGroup);
        if (parent == null) { return; }
        UIElement sibling = NodeIsGroup(parent.First, emptyGroup) ? parent.Second : parent.First;
        if (NodeIsSplit(_rootNode, parent)) {
            parent.DetachChildrenForCollapse();
            SetRootDockNode(sibling);
        } else {
            DockSplitNode grand = FindParentSplit(_rootNode, parent);
            parent.DetachChildrenForCollapse();
            if (grand != null) {
                grand.ReplaceChild(parent, sibling);
            }
        }
        if (SameSplit(_mainHorizSplit, parent)) { _mainHorizSplit = null; }
        if (SameSplit(_centerRightSplit, parent)) { _centerRightSplit = null; }
        if (SameSplit(_centerBottomSplit, parent)) { _centerBottomSplit = null; }
    }

    private DockTabGroup EnsureZoneGroupAttached(DockZone zone) {
        DockTabGroup target = ZoneGroup(zone);
        if (target == null) {
            target = new DockTabGroup();
            if (zone == DockZone.Left) { _leftGroup = target; WireGroup(target, "L"); }
            else if (zone == DockZone.Right) { _rightGroup = target; WireGroup(target, "R"); }
            else if (zone == DockZone.Bottom) { _bottomGroup = target; WireGroup(target, "B"); }
            else { _centerGroup = target; WireGroup(target, "C"); }
        }
        if (_rootNode == null) {
            SetRootDockNode(target);
            return target;
        }
        if (IsNodeInTree(_rootNode, target)) {
            return target;
        }
        if (_rootNode is DockTabGroup && (_rootNode as DockTabGroup).PaneCount() == 0) {
            SetRootDockNode(target);
            return target;
        }
        if (zone == DockZone.Left) {
            var sp = new DockSplitNode(Orientation.Horizontal, _savedLeftRatio, target, _rootNode);
            if (_mainHorizSplit == null || !IsNodeInTree(_rootNode, _mainHorizSplit)) {
                _mainHorizSplit = sp;
            }
            SetRootDockNode(sp);
        } else if (zone == DockZone.Right) {
            var sp = new DockSplitNode(Orientation.Horizontal, _savedRightRatio, _rootNode, target);
            if (_centerRightSplit == null || !IsNodeInTree(_rootNode, _centerRightSplit)) {
                _centerRightSplit = sp;
            }
            SetRootDockNode(sp);
        } else if (zone == DockZone.Bottom) {
            var sp = new DockSplitNode(Orientation.Vertical, _savedBottomRatio, _rootNode, target);
            if (_centerBottomSplit == null || !IsNodeInTree(_rootNode, _centerBottomSplit)) {
                _centerBottomSplit = sp;
            }
            SetRootDockNode(sp);
        } else if (zone == DockZone.Top) {
            var sp = new DockSplitNode(Orientation.Vertical, 0.28f, target, _rootNode);
            SetRootDockNode(sp);
        } else {
            // Center was collapsed out: attach to root via split if root is another zone
            if (NodeIsGroup(_rootNode, _leftGroup)) {
                var sp = new DockSplitNode(Orientation.Horizontal, _savedLeftRatio, _leftGroup, target);
                if (_mainHorizSplit == null || !IsNodeInTree(_rootNode, _mainHorizSplit)) {
                    _mainHorizSplit = sp;
                }
                SetRootDockNode(sp);
            } else if (NodeIsGroup(_rootNode, _rightGroup)) {
                var sp = new DockSplitNode(Orientation.Horizontal, _savedRightRatio, target, _rightGroup);
                if (_centerRightSplit == null || !IsNodeInTree(_rootNode, _centerRightSplit)) {
                    _centerRightSplit = sp;
                }
                SetRootDockNode(sp);
            } else if (NodeIsGroup(_rootNode, _bottomGroup)) {
                var sp = new DockSplitNode(Orientation.Vertical, _savedBottomRatio, target, _bottomGroup);
                if (_centerBottomSplit == null || !IsNodeInTree(_rootNode, _centerBottomSplit)) {
                    _centerBottomSplit = sp;
                }
                SetRootDockNode(sp);
            } else if (_mainHorizSplit == null || !IsNodeInTree(_rootNode, _mainHorizSplit)) {
                var sp = new DockSplitNode(Orientation.Horizontal, _savedLeftRatio, _rootNode, target);
                _mainHorizSplit = sp;
                SetRootDockNode(sp);
            } else {
                var sp = new DockSplitNode(Orientation.Horizontal, 0.5f, _rootNode, target);
                SetRootDockNode(sp);
            }
        }
        return target;
    }

    public DockPane RegisterPane(string paneId, string title, UIElement content, DockZone zone = DockZone.Center) {
        if (paneId != null && paneId.Length > 0 && _panesById.ContainsKey(paneId)) {
            UnregisterPane(paneId);
        }
        var pane = new DockPane(paneId, title, content);
        if (paneId != null && paneId.Length > 0) {
            _panesById[paneId] = pane;
            _paneDefaultZones[paneId] = ZoneToInt(zone);
        }
        DockTabGroup grp = EnsureZoneGroupAttached(zone);
        if (grp != null) {
            grp.AddPane(pane, true);
        }
        if (paneId != null && paneId.Length > 0) {
            _lastActivatedPaneId = paneId;
        }
        return pane;
    }

    public bool UnregisterPane(string paneId) {
        if (paneId == null || paneId.Length == 0 || !_panesById.ContainsKey(paneId)) {
            return false;
        }
        if (_maximizedPaneId == paneId) {
            RestoreMaximizedPane();
        }
        DockTabGroup grp = FindGroupContainingPane(paneId);
        _panesById.Remove(paneId);
        if (_paneDefaultZones.ContainsKey(paneId)) {
            _paneDefaultZones.Remove(paneId);
        }
        if (grp != null) {
            grp.RemovePaneById(paneId);
            if (grp.PaneCount() == 0) {
                CollapseEmptyGroup(grp);
            }
        }
        if (_lastActivatedPaneId == paneId) {
            _lastActivatedPaneId = "";
        }
        return true;
    }

    public bool ActivatePane(string paneId) {
        if (paneId == null || paneId.Length == 0) { return false; }
        DockTabGroup[] groups = CollectAllTabGroups();
        for (int i = 0; i < groups.Length; i += 1) {
            if (groups[i].SelectPaneById(paneId)) {
                _lastActivatedPaneId = paneId;
                return true;
            }
        }
        return false;
    }

    public bool MovePaneToZone(string paneId, DockZone targetZone) {
        if (paneId == null || paneId.Length == 0 || !_panesById.ContainsKey(paneId)) { return false; }
        if (_maximizedPaneId == paneId) {
            RestoreMaximizedPane();
        }
        DockPane pane = _panesById[paneId];
        DockTabGroup src = FindGroupContainingPane(paneId);
        DockTabGroup dst = EnsureZoneGroupAttached(targetZone);
        if (dst == null) { return false; }
        if (SameGroup(src, dst)) {
            dst.SelectPaneById(paneId);
            return true;
        }
        if (src != null) {
            src.RemovePaneById(paneId);
        }
        dst.AddPane(pane, true);
        if (src != null && src.PaneCount() == 0) {
            CollapseEmptyGroup(src);
        }
        _lastActivatedPaneId = paneId;
        return true;
    }

    private void SplitGroupWithPane(DockTabGroup targetGroup, DockPane pane, DockTabGroup sourceGroup, DockZone side) {
        if (targetGroup == null || pane == null) { return; }
        if (sourceGroup != null) {
            sourceGroup.RemovePaneById(pane.PaneId);
        }
        var newGrp = new DockTabGroup();
        WireGroup(newGrp, "D");
        newGrp.AddPane(pane, true);

        Orientation orient = (side == DockZone.Left || side == DockZone.Right) ? Orientation.Horizontal : Orientation.Vertical;
        UIElement firstChild = (side == DockZone.Left || side == DockZone.Top) ? (UIElement)newGrp : (UIElement)targetGroup;
        UIElement secondChild = (side == DockZone.Left || side == DockZone.Top) ? (UIElement)targetGroup : (UIElement)newGrp;

        DockSplitNode parent = FindParentSplit(_rootNode, targetGroup);
        var newSplit = new DockSplitNode(orient, 0.5f, firstChild, secondChild);
        if (parent == null) {
            SetRootDockNode(newSplit);
        } else {
            parent.ReplaceChild(targetGroup, newSplit);
        }

        if (sourceGroup != null && !SameGroup(sourceGroup, targetGroup) && sourceGroup.PaneCount() == 0) {
            CollapseEmptyGroup(sourceGroup);
        }
        _lastActivatedPaneId = pane.PaneId;
    }

    public bool DockPaneRelativeTo(string paneId, string targetPaneId, DockZone zone) {
        if (paneId == null || paneId.Length == 0 || !_panesById.ContainsKey(paneId)) { return false; }
        DockPane pane = _panesById[paneId];
        DockTabGroup src = FindGroupContainingPane(paneId);
        DockTabGroup dst = FindGroupContainingPane(targetPaneId);
        if (dst == null) { return false; }
        if (_maximizedPaneId == paneId) { RestoreMaximizedPane(); }
        if (zone == DockZone.Center) {
            if (SameGroup(src, dst)) {
                dst.SelectPaneById(paneId);
                return true;
            }
            if (src != null) { src.RemovePaneById(paneId); }
            dst.AddPane(pane, true);
            if (src != null && src.PaneCount() == 0) { CollapseEmptyGroup(src); }
            _lastActivatedPaneId = paneId;
            return true;
        }
        if (SameGroup(src, dst) && src.PaneCount() <= 1) { return false; }
        SplitGroupWithPane(dst, pane, src, zone);
        return true;
    }

    // ---- Maximize / Restore ----

    public string MaximizedPaneId { get { return _maximizedPaneId; } }
    public bool IsPaneMaximized { get { return _maximizedPaneId.Length > 0; } }

    public bool MaximizePane(string paneId) {
        if (paneId == null || paneId.Length == 0 || !_panesById.ContainsKey(paneId)) { return false; }
        if (_maximizedPaneId == paneId) { return true; }
        if (_maximizedPaneId.Length > 0) {
            RestoreMaximizedPane();
        }
        DockPane pane = _panesById[paneId];
        DockTabGroup owner = FindGroupContainingPane(paneId);
        if (owner == null) { return false; }
        owner.SelectPaneById(paneId);
        owner.SetMaximizedDetached(true);

        _maximizedPaneId = paneId;
        _lastActivatedPaneId = paneId;
        if (_maximizedTitle != null) { _maximizedTitle.Text = pane.Title; }
        if (_maximizedContentHost != null) {
            object raw = pane.Content;
            if (raw is UIElement) {
                _maximizedContentHost.Child = raw as UIElement;
            } else {
                _maximizedContentHost.Child = pane;
            }
        }
        if (_rootNode != null) { _rootNode.Visibility = Visibility.Collapsed; }
        if (_maximizedHost != null) { _maximizedHost.Visibility = Visibility.Visible; }
        MaximizedStateChanged(this, EventArgs.Empty());
        return true;
    }

    public bool RestoreMaximizedPane() {
        if (_maximizedPaneId.Length == 0) { return false; }
        string prevId = _maximizedPaneId;
        _maximizedPaneId = "";
        if (_maximizedContentHost != null) { _maximizedContentHost.Child = null; }
        if (_maximizedHost != null) { _maximizedHost.Visibility = Visibility.Collapsed; }
        if (_rootNode != null) { _rootNode.Visibility = Visibility.Visible; }
        DockTabGroup owner = FindGroupContainingPane(prevId);
        if (owner != null) {
            owner.SetMaximizedDetached(false);
        }
        MaximizedStateChanged(this, EventArgs.Empty());
        return true;
    }

    public bool ToggleMaximizeActivePane() {
        if (_maximizedPaneId.Length > 0) {
            return RestoreMaximizedPane();
        }
        string targetId = "";
        if (_lastActivatedPaneId.Length > 0 && _panesById.ContainsKey(_lastActivatedPaneId)) {
            targetId = _lastActivatedPaneId;
        } else if (_centerGroup != null && _centerGroup.ActivePane() != null) {
            targetId = _centerGroup.ActivePane().PaneId;
        } else {
            DockTabGroup[] groups = CollectAllTabGroups();
            for (int i = 0; i < groups.Length; i += 1) {
                DockPane ap = groups[i].ActivePane();
                if (ap != null && ap.PaneId.Length > 0) {
                    targetId = ap.PaneId;
                    break;
                }
            }
        }
        if (targetId.Length == 0) { return false; }
        return MaximizePane(targetId);
    }

    // ---- Tab Drag & Drop + Dock Hints Overlay ----

    public bool IsDraggingPane { get { return _isDraggingPane; } }
    public string DragPaneId { get { return _isDraggingPane ? _dragPaneId : ""; } }
    public DockZone HoveredDropZone { get { return _hoveredDropZone; } }
    public bool IsDockOverlayVisible { get { return _dockOverlayCanvas != null && _dockOverlayCanvas.Visibility == Visibility.Visible; } }

    private bool TryCaptureDragTabSlots(DockTabGroup grp) {
        if (grp == null) { return false; }
        UIElement[] btns = grp.TabButtons();
        if (btns.Length == 0) { return false; }
        float firstW = btns[0].ActualWidth > 1.0f ? btns[0].ActualWidth : btns[0].ScreenBoundsWidth;
        if (firstW <= 1.0f) { return false; }
        var widths = new float[btns.Length];
        for (int i = 0; i < btns.Length; i += 1) {
            float w = btns[i].ActualWidth > 1.0f ? btns[i].ActualWidth : btns[i].ScreenBoundsWidth;
            if (w <= 1.0f) { return false; }
            widths[i] = w;
        }
        _dragTabStripOriginX = btns[0].ViewportX;
        _dragTabSlotWidths = widths;
        _dragTabSlotValid = true;
        _dragTabSlotGroupSeqId = grp._groupSeqId;
        return true;
    }

    internal void PrepareTabDragSlots(DockTabGroup grp) {
        _dragTabSlotValid = false;
        _dragTabSlotWidths = new float[0];
        _dragTabSlotGroupSeqId = -1;
        _dragGrabOffsetX = 0.0f;
        TryCaptureDragTabSlots(grp);
    }

    internal void BeginTabDragCandidate(DockTabGroup srcGroup, DockPane pane, float screenX, float screenY) {
        if (srcGroup == null || pane == null || _maximizedPaneId.Length > 0) { return; }
        _dragCandidateActive = true;
        _isDraggingPane = false;
        _dragSourceGroup = srcGroup;
        _dragPane = pane;
        _dragPaneId = pane.PaneId;
        _dragStartX = screenX;
        _dragStartY = screenY;
        _dropTargetGroup = null;
        _hoveredDropZone = DockZone.Center;
        if (!TryCaptureDragTabSlots(srcGroup)) {
            if (_dragTabSlotGroupSeqId != srcGroup._groupSeqId || _dragTabSlotWidths.Length != srcGroup.PaneCount()) {
                _dragTabSlotValid = false;
                _dragTabSlotWidths = new float[0];
            }
        }
        _dragGrabOffsetX = 0.0f;
        int dragIdx = srcGroup.IndexOfPane(pane.PaneId);
        if (_dragTabSlotValid && dragIdx >= 0 && dragIdx < _dragTabSlotWidths.Length) {
            float slotLeft = _dragTabStripOriginX;
            for (int si = 0; si < dragIdx; si += 1) {
                slotLeft = slotLeft + _dragTabSlotWidths[si] + 2.0f;
            }
            _dragGrabOffsetX = Math.Clampf(screenX - slotLeft, 0.0f, _dragTabSlotWidths[dragIdx]);
        }
        CaptureMouse();
    }

    private void SetOverlayHintsVisible(bool vis) {
        Visibility v = vis ? Visibility.Visible : Visibility.Collapsed;
        if (_dropPreviewBox != null) { _dropPreviewBox.Visibility = v; }
        if (_hintCenter != null) { _hintCenter.Visibility = v; }
        if (_hintLeft != null) { _hintLeft.Visibility = v; }
        if (_hintRight != null) { _hintRight.Visibility = v; }
        if (_hintTop != null) { _hintTop.Visibility = v; }
        if (_hintBottom != null) { _hintBottom.Visibility = v; }
    }

    private void UpdateOverlayForTarget(DockTabGroup target, DockZone zone) {
        if (_dockOverlayCanvas == null || target == null) {
            SetOverlayHintsVisible(false);
            return;
        }
        SetOverlayHintsVisible(true);

        float gx = target.LayoutX;
        float gy = target.LayoutY;
        if (ParseUtil.IsValidHandle(_dockOverlayCanvas.__nativeHandle)) {
            gx = Visual_ScreenToLocal(_dockOverlayCanvas.__nativeHandle, target.ViewportX, target.ViewportY, 0);
            gy = Visual_ScreenToLocal(_dockOverlayCanvas.__nativeHandle, target.ViewportX, target.ViewportY, 1);
        }
        float gw = Math.Maxf(20.0f, target.ActualWidth > 1.0f ? target.ActualWidth : target.ScreenBoundsWidth);
        float gh = Math.Maxf(20.0f, target.ActualHeight > 1.0f ? target.ActualHeight : target.ScreenBoundsHeight);

        float px = gx;
        float py = gy;
        float pw = gw;
        float ph = gh;
        if (zone == DockZone.Left) { pw = gw * 0.5f; }
        else if (zone == DockZone.Right) { px = gx + gw * 0.5f; pw = gw * 0.5f; }
        else if (zone == DockZone.Top) { ph = gh * 0.5f; }
        else if (zone == DockZone.Bottom) { py = gy + gh * 0.5f; ph = gh * 0.5f; }

        UIElement.SetCanvasLeft(_dropPreviewBox, px);
        UIElement.SetCanvasTop(_dropPreviewBox, py);
        _dropPreviewBox.Width = pw;
        _dropPreviewBox.Height = ph;

        float cx = gx + gw * 0.5f - 13.0f;
        float cy = gy + gh * 0.5f - 13.0f;
        UIElement.SetCanvasLeft(_hintCenter, cx);
        UIElement.SetCanvasTop(_hintCenter, cy);
        UIElement.SetCanvasLeft(_hintLeft, cx - 32.0f);
        UIElement.SetCanvasTop(_hintLeft, cy);
        UIElement.SetCanvasLeft(_hintRight, cx + 32.0f);
        UIElement.SetCanvasTop(_hintRight, cy);
        UIElement.SetCanvasLeft(_hintTop, cx);
        UIElement.SetCanvasTop(_hintTop, cy - 32.0f);
        UIElement.SetCanvasLeft(_hintBottom, cx);
        UIElement.SetCanvasTop(_hintBottom, cy + 32.0f);

        string activeBg = "#FF0284C7";
        string idleBg = "#CC0F172A";
        _hintCenter.Background = new SolidColorBrush(zone == DockZone.Center ? activeBg : idleBg);
        _hintLeft.Background = new SolidColorBrush(zone == DockZone.Left ? activeBg : idleBg);
        _hintRight.Background = new SolidColorBrush(zone == DockZone.Right ? activeBg : idleBg);
        _hintTop.Background = new SolidColorBrush(zone == DockZone.Top ? activeBg : idleBg);
        _hintBottom.Background = new SolidColorBrush(zone == DockZone.Bottom ? activeBg : idleBg);
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_dragCandidateActive) { return; }
        float dx = e.X - _dragStartX;
        float dy = e.Y - _dragStartY;
        if (!_isDraggingPane) {
            if ((dx * dx + dy * dy) < 25.0f) { return; }
            _isDraggingPane = true;
            if (_dockOverlayCanvas != null) {
                _dockOverlayCanvas.Visibility = Visibility.Visible;
            }
            if (_dragGhostText != null && _dragPane != null) {
                _dragGhostText.Text = _dragPane.Title;
            }
        }
        if (_dragGhost != null && _dockOverlayCanvas != null) {
            float ox = ParseUtil.IsValidHandle(_dockOverlayCanvas.__nativeHandle) ? Visual_ScreenToLocal(_dockOverlayCanvas.__nativeHandle, e.X, e.Y, 0) : e.X;
            float oy = ParseUtil.IsValidHandle(_dockOverlayCanvas.__nativeHandle) ? Visual_ScreenToLocal(_dockOverlayCanvas.__nativeHandle, e.X, e.Y, 1) : e.Y;
            UIElement.SetCanvasLeft(_dragGhost, ox + 12.0f);
            UIElement.SetCanvasTop(_dragGhost, oy + 12.0f);
        }

        // Case A: Intra-group horizontal tab reordering
        if (_dragSourceGroup != null && IsNodeInTree(_rootNode, _dragSourceGroup) && _dragSourceGroup.PaneCount() > 1 && ParseUtil.IsValidHandle(_dragSourceGroup.__nativeHandle)) {
            float sx = Visual_ScreenToLocal(_dragSourceGroup.__nativeHandle, e.X, e.Y, 0);
            float sy = Visual_ScreenToLocal(_dragSourceGroup.__nativeHandle, e.X, e.Y, 1);
            float sw = _dragSourceGroup.ActualWidth > 1.0f ? _dragSourceGroup.ActualWidth : _dragSourceGroup.ScreenBoundsWidth;
            if (sy >= -8.0f && sy <= 34.0f && sx >= -8.0f && sx <= sw + 8.0f) {
                UIElement[] btns = _dragSourceGroup.TabButtons();
                if (btns.Length > 1) {
                    TryCaptureDragTabSlots(_dragSourceGroup);
                    int curIdx = _dragSourceGroup.IndexOfPane(_dragPaneId);
                    if (curIdx >= 0) {
                        int targetIdx = curIdx;
                        if (_dragTabSlotValid && _dragTabSlotWidths.Length == btns.Length) {
                            int nSlots = _dragTabSlotWidths.Length;
                            var slotLefts = new float[nSlots];
                            float accX = _dragTabStripOriginX;
                            for (int si = 0; si < nSlots; si += 1) {
                                slotLefts[si] = accX;
                                accX = accX + _dragTabSlotWidths[si] + 2.0f;
                            }
                            float ghostLeft = e.X - _dragGrabOffsetX;
                            for (int bi = 0; bi < curIdx; bi += 1) {
                                float bw = _dragTabSlotWidths[bi];
                                float thresh = slotLefts[bi] + bw * 0.5f;
                                if (ghostLeft <= thresh) {
                                    targetIdx = bi;
                                    break;
                                }
                            }
                            if (targetIdx == curIdx) {
                                for (int bi = curIdx + 1; bi < nSlots; bi += 1) {
                                    float bw = _dragTabSlotWidths[bi];
                                    float thresh = slotLefts[bi] + bw * 0.5f;
                                    if (e.X >= thresh) {
                                        targetIdx = bi;
                                    }
                                }
                            }
                        } else {
                            targetIdx = btns.Length - 1;
                            for (int bi = 0; bi < btns.Length; bi += 1) {
                                float bw = btns[bi].ActualWidth > 1.0f ? btns[bi].ActualWidth : btns[bi].ScreenBoundsWidth;
                                float mid = btns[bi].ViewportX + bw * 0.5f;
                                if (e.X < mid) {
                                    targetIdx = bi;
                                    break;
                                }
                            }
                        }
                        if (targetIdx != curIdx && _dragSourceGroup.MovePaneToIndex(curIdx, targetIdx)) {
                            if (_dragTabSlotValid && _dragTabSlotWidths.Length == btns.Length) {
                                float movedW = _dragTabSlotWidths[curIdx];
                                _dragTabSlotWidths.RemoveAt(curIdx);
                                _dragTabSlotWidths.Insert(targetIdx, movedW);
                            }
                        }
                    }
                }
                _dropTargetGroup = null;
                _hoveredDropZone = DockZone.Center;
                SetOverlayHintsVisible(false);
                return;
            }
        }

        // Case B: Cross-zone / split target
        DockTabGroup[] groups = CollectAllTabGroups();
        DockTabGroup hitGroup = null;
        float hitLx = 0.0f;
        float hitLy = 0.0f;
        for (int i = 0; i < groups.Length; i += 1) {
            DockTabGroup g = groups[i];
            if (!ParseUtil.IsValidHandle(g.__nativeHandle)) { continue; }
            float lx = Visual_ScreenToLocal(g.__nativeHandle, e.X, e.Y, 0);
            float ly = Visual_ScreenToLocal(g.__nativeHandle, e.X, e.Y, 1);
            float gw = g.ActualWidth > 1.0f ? g.ActualWidth : g.ScreenBoundsWidth;
            float gh = g.ActualHeight > 1.0f ? g.ActualHeight : g.ScreenBoundsHeight;
            if (lx >= 0.0f && lx <= gw && ly >= 0.0f && ly <= gh) {
                hitGroup = g;
                hitLx = lx;
                hitLy = ly;
                break;
            }
        }

        if (hitGroup == null) {
            _dropTargetGroup = null;
            SetOverlayHintsVisible(false);
            return;
        }

        float w = hitGroup.ActualWidth > 1.0f ? hitGroup.ActualWidth : hitGroup.ScreenBoundsWidth;
        float h = hitGroup.ActualHeight > 1.0f ? hitGroup.ActualHeight : hitGroup.ScreenBoundsHeight;
        DockZone zone = DockZone.Center;
        if (hitLy <= 32.0f) {
            zone = DockZone.Center;
        } else if (hitLx < w * 0.25f) {
            zone = DockZone.Left;
        } else if (hitLx > w * 0.75f) {
            zone = DockZone.Right;
        } else if (hitLy < h * 0.25f) {
            zone = DockZone.Top;
        } else if (hitLy > h * 0.75f) {
            zone = DockZone.Bottom;
        }
        if (SameGroup(hitGroup, _dragSourceGroup) && _dragSourceGroup.PaneCount() <= 1) {
            zone = DockZone.Center;
        }

        _dropTargetGroup = hitGroup;
        _hoveredDropZone = zone;
        UpdateOverlayForTarget(hitGroup, zone);
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (!_dragCandidateActive) { return; }
        bool wasDragging = _isDraggingPane;
        DockTabGroup srcGrp = _dragSourceGroup;
        DockTabGroup dstGrp = _dropTargetGroup;
        DockPane pane = _dragPane;
        string paneId = _dragPaneId;
        DockZone dropZone = _hoveredDropZone;

        _dragCandidateActive = false;
        _isDraggingPane = false;
        _dragSourceGroup = null;
        _dropTargetGroup = null;
        _dragPane = null;
        _dragPaneId = "";
        _hoveredDropZone = DockZone.Center;
        _dragTabSlotValid = false;
        _dragTabSlotWidths = new float[0];
        _dragTabSlotGroupSeqId = -1;
        _dragGrabOffsetX = 0.0f;
        SetOverlayHintsVisible(false);
        if (_dockOverlayCanvas != null) {
            _dockOverlayCanvas.Visibility = Visibility.Collapsed;
        }
        ReleaseMouseCapture();

        if (!wasDragging || dstGrp == null || pane == null) { return; }
        if (dropZone == DockZone.Center) {
            if (!SameGroup(dstGrp, srcGrp)) {
                if (srcGrp != null) { srcGrp.RemovePaneById(paneId); }
                dstGrp.AddPane(pane, true);
                if (srcGrp != null && srcGrp.PaneCount() == 0) {
                    CollapseEmptyGroup(srcGrp);
                }
                _lastActivatedPaneId = paneId;
            }
        } else {
            if (SameGroup(srcGrp, dstGrp) && srcGrp.PaneCount() <= 1) { return; }
            SplitGroupWithPane(dstGrp, pane, srcGrp, dropZone);
        }
        e.Handled = true;
    }

    public float LeftSplitRatio {
        get { return _mainHorizSplit != null ? _mainHorizSplit.SplitRatio : _savedLeftRatio; }
        set {
            _savedLeftRatio = Math.Clampf(value, 0.1f, 0.9f);
            if (_mainHorizSplit != null) { _mainHorizSplit.SplitRatio = _savedLeftRatio; }
        }
    }
    public float RightSplitRatio {
        get { return _centerRightSplit != null ? _centerRightSplit.SplitRatio : _savedRightRatio; }
        set {
            _savedRightRatio = Math.Clampf(value, 0.1f, 0.9f);
            if (_centerRightSplit != null) { _centerRightSplit.SplitRatio = _savedRightRatio; }
        }
    }
    public float BottomSplitRatio {
        get { return _centerBottomSplit != null ? _centerBottomSplit.SplitRatio : _savedBottomRatio; }
        set {
            _savedBottomRatio = Math.Clampf(value, 0.1f, 0.9f);
            if (_centerBottomSplit != null) { _centerBottomSplit.SplitRatio = _savedBottomRatio; }
        }
    }

    // ---- Serialization (aero-dock-v2: & aero-dock-v1: compatibility; ZERO "|" chars!) ----

    private static string EscapeDockToken(string s) {
        if (s == null || s.Length == 0) { return ""; }
        string outStr = "";
        for (int i = 0; i < s.Length; i += 1) {
            string ch = s.Substring(i, 1);
            if (ch == "%") { outStr = outStr + "%25"; }
            else if (ch == ",") { outStr = outStr + "%2C"; }
            else if (ch == "(") { outStr = outStr + "%28"; }
            else if (ch == ")") { outStr = outStr + "%29"; }
            else if (ch == ";") { outStr = outStr + "%3B"; }
            else if (ch == "|") { outStr = outStr + "%7C"; }
            else if (ch == "=") { outStr = outStr + "%3D"; }
            else { outStr = outStr + ch; }
        }
        return outStr;
    }

    private static string UnescapeDockToken(string s) {
        if (s == null || s.Length == 0) { return ""; }
        string outStr = "";
        int i = 0;
        while (i < s.Length) {
            if (i + 2 < s.Length && s.Substring(i, 1) == "%") {
                string code = s.Substring(i + 1, 2).ToUpper();
                if (code == "25") { outStr = outStr + "%"; i += 3; continue; }
                if (code == "2C") { outStr = outStr + ","; i += 3; continue; }
                if (code == "28") { outStr = outStr + "("; i += 3; continue; }
                if (code == "29") { outStr = outStr + ")"; i += 3; continue; }
                if (code == "3B") { outStr = outStr + ";"; i += 3; continue; }
                if (code == "7C") { outStr = outStr + "|"; i += 3; continue; }
                if (code == "3D") { outStr = outStr + "="; i += 3; continue; }
            }
            outStr = outStr + s.Substring(i, 1);
            i += 1;
        }
        return outStr;
    }

    private string SerializeNodeV2(UIElement node) {
        if (node is DockSplitNode) {
            var sp = node as DockSplitNode;
            string ori = sp.Orientation == Orientation.Horizontal ? "H" : "V";
            int milli = EditorMath.RoundToInt(sp.SplitRatio * 1000.0f);
            return "S(" + ori + "," + EditorMath.IntToStr(milli) + "," + SerializeNodeV2(sp.First) + "," + SerializeNodeV2(sp.Second) + ")";
        }
        if (node is DockTabGroup) {
            var g = node as DockTabGroup;
            string tag = (g._zoneTag != null && g._zoneTag.Length > 0) ? g._zoneTag : "D";
            DockPane ap = g.ActivePane();
            string activeId = ap != null ? EscapeDockToken(ap.PaneId) : "";
            string res = "G(" + tag + "," + activeId;
            DockPane[] ps = g.Panes();
            for (int i = 0; i < ps.Length; i += 1) {
                res = res + "," + EscapeDockToken(ps[i].PaneId);
            }
            res = res + ")";
            return res;
        }
        return "G(D,)";
    }

    public string SerializeLayout() {
        string treeSpec = _rootNode != null ? SerializeNodeV2(_rootNode) : "G(C,)";
        string spec = "aero-dock-v2:" + treeSpec;
        if (_maximizedPaneId.Length > 0) {
            spec = spec + ";max=" + EscapeDockToken(_maximizedPaneId);
        }
        return spec;
    }

    private static string ReadTokenUntil(DockParseCursor c, string stopChars) {
        int start = c.Pos;
        while (c.Pos < c.Src.Length) {
            string ch = c.Src.Substring(c.Pos, 1);
            if (stopChars.Contains(ch)) { break; }
            c.Pos += 1;
        }
        return c.Src.Substring(start, c.Pos - start);
    }

    private static bool ExpectChar(DockParseCursor c, string expected) {
        if (c.Pos >= c.Src.Length || c.Src.Substring(c.Pos, 1) != expected) {
            c.Failed = true;
            return false;
        }
        c.Pos += 1;
        return true;
    }

    private static int ParseStrictNonNegInt(string raw) {
        if (raw == null || raw.Length == 0) { return -1; }
        int acc = 0;
        for (int i = 0; i < raw.Length; i += 1) {
            int d = ParseUtil.DigitValue(raw.Substring(i, 1));
            if (d < 0) { return -1; }
            if (acc > 100000000) { return -1; }
            acc = acc * 10 + d;
        }
        return acc;
    }

    private static float ParseStrictV1RatioMilli(string raw) {
        if (raw == null) { return -1.0f; }
        string s = raw.Trim();
        if (s.Length == 0) { return -1.0f; }
        bool seenDot = false;
        bool anyDigit = false;
        for (int i = 0; i < s.Length; i += 1) {
            string ch = s.Substring(i, 1);
            if (ch == ".") {
                if (seenDot || i == 0 || i == s.Length - 1) { return -1.0f; }
                seenDot = true;
            } else {
                if (ParseUtil.DigitValue(ch) < 0) { return -1.0f; }
                anyDigit = true;
            }
        }
        if (!anyDigit) { return -1.0f; }
        float val = ParseUtil.ParseFloat(s, -1.0f);
        if (val <= 0.0f || val >= 1000.0f) { return -1.0f; }
        return val;
    }

    private static DockLayoutNodeDesc ParseNodeV2(DockParseCursor c) {
        if (c.Failed || c.Pos >= c.Src.Length) { c.Failed = true; return null; }
        string kind = c.Src.Substring(c.Pos, 1);
        c.Pos += 1;
        if (!ExpectChar(c, "(")) { return null; }

        if (kind == "S") {
            var desc = new DockLayoutNodeDesc();
            desc.IsSplit = true;
            string ori = ReadTokenUntil(c, ",");
            if (ori == "H") { desc.Orient = Orientation.Horizontal; }
            else if (ori == "V") { desc.Orient = Orientation.Vertical; }
            else { c.Failed = true; return null; }
            if (!ExpectChar(c, ",")) { return null; }

            string milliStr = ReadTokenUntil(c, ",");
            int milli = ParseStrictNonNegInt(milliStr);
            if (milli <= 0 || milli >= 1000) { c.Failed = true; return null; }
            desc.Ratio = Math.Clampf((float)milli / 1000.0f, 0.1f, 0.9f);
            if (!ExpectChar(c, ",")) { return null; }

            desc.First = ParseNodeV2(c);
            if (c.Failed || !ExpectChar(c, ",")) { c.Failed = true; return null; }
            desc.Second = ParseNodeV2(c);
            if (c.Failed || !ExpectChar(c, ")")) { c.Failed = true; return null; }
            return desc;
        }

        if (kind == "G") {
            var desc = new DockLayoutNodeDesc();
            desc.IsSplit = false;
            desc.ZoneTag = ReadTokenUntil(c, ",");
            if (desc.ZoneTag.Length == 0) { desc.ZoneTag = "D"; }
            if (!ExpectChar(c, ",")) { return null; }

            desc.ActivePaneId = UnescapeDockToken(ReadTokenUntil(c, ",)"));
            while (!c.Failed && c.Pos < c.Src.Length && c.Src.Substring(c.Pos, 1) == ",") {
                c.Pos += 1;
                string pid = UnescapeDockToken(ReadTokenUntil(c, ",)"));
                if (pid.Length > 0) {
                    desc.PaneIds.Push(pid);
                }
            }
            if (!ExpectChar(c, ")")) { return null; }
            return desc;
        }

        c.Failed = true;
        return null;
    }

    private bool IsDefaultFourZoneTopology() {
        return NodeIsSplit(_rootNode, _mainHorizSplit) &&
               _mainHorizSplit != null &&
               NodeIsGroup(_mainHorizSplit.First, _leftGroup) &&
               NodeIsSplit(_mainHorizSplit.Second, _centerRightSplit) &&
               _centerRightSplit != null &&
               NodeIsSplit(_centerRightSplit.First, _centerBottomSplit) &&
               NodeIsGroup(_centerRightSplit.Second, _rightGroup) &&
               _centerBottomSplit != null &&
               NodeIsGroup(_centerBottomSplit.First, _centerGroup) &&
               NodeIsGroup(_centerBottomSplit.Second, _bottomGroup);
    }

    private UIElement MaterializeNodeV2(
        DockLayoutNodeDesc desc,
        Dictionary<string, bool> usedTags,
        Dictionary<string, bool> placedPanes,
        DockTabGroup[] createdGroups) {
        if (desc == null) { return null; }
        if (desc.IsSplit) {
            UIElement first = MaterializeNodeV2(desc.First, usedTags, placedPanes, createdGroups);
            UIElement second = MaterializeNodeV2(desc.Second, usedTags, placedPanes, createdGroups);
            return new DockSplitNode(desc.Orient, desc.Ratio, first, second);
        }

        DockTabGroup grp = null;
        string tag = desc.ZoneTag;
        if ((tag == "L" || tag == "C" || tag == "R" || tag == "B") && !usedTags.ContainsKey(tag)) {
            usedTags[tag] = true;
            if (tag == "L") { grp = _leftGroup; }
            else if (tag == "C") { grp = _centerGroup; }
            else if (tag == "R") { grp = _rightGroup; }
            else if (tag == "B") { grp = _bottomGroup; }
        }
        if (grp == null) {
            grp = new DockTabGroup();
            WireGroup(grp, tag);
        } else {
            grp.ClearAllPanesSilent();
            grp._zoneTag = tag;
        }
        createdGroups.Push(grp);

        for (int i = 0; i < desc.PaneIds.Length; i += 1) {
            string pid = desc.PaneIds[i];
            if (_panesById.ContainsKey(pid) && !placedPanes.ContainsKey(pid)) {
                placedPanes[pid] = true;
                grp.AddPane(_panesById[pid], false);
            }
        }
        if (desc.ActivePaneId.Length > 0) {
            grp.SelectPaneById(desc.ActivePaneId);
        } else if (grp.PaneCount() > 0 && grp.SelectedIndex < 0) {
            grp.SelectedIndex = 0;
        }
        return grp;
    }

    public bool DeserializeLayout(string spec) {
        if (spec == null || spec.Length == 0) { return false; }

        if (spec.StartsWith("aero-dock-v1:")) {
            string body = spec.Substring(13);
            string[] parts = body.Split(",");
            if (parts.Length != 3) { return false; }
            float v0 = ParseStrictV1RatioMilli(parts[0]);
            float v1 = ParseStrictV1RatioMilli(parts[1]);
            float v2 = ParseStrictV1RatioMilli(parts[2]);
            if (v0 <= 0.0f || v1 <= 0.0f || v2 <= 0.0f) { return false; }

            RestoreMaximizedPane();
            if (!IsDefaultFourZoneTopology()) {
                ResetDefaultIdeLayout();
            }
            LeftSplitRatio = v0 / 1000.0f;
            RightSplitRatio = v1 / 1000.0f;
            BottomSplitRatio = v2 / 1000.0f;
            return true;
        }

        if (!spec.StartsWith("aero-dock-v2:")) { return false; }
        string fullBody = spec.Substring(13);
        string treePart = fullBody;
        string maxPaneId = "";
        int semiIdx = fullBody.IndexOf(";");
        if (semiIdx >= 0) {
            treePart = fullBody.Substring(0, semiIdx);
            string metaPart = fullBody.Substring(semiIdx + 1);
            if (metaPart.Length == 0) { return false; }
            string[] clauses = metaPart.Split(";");
            bool seenMax = false;
            for (int ci = 0; ci < clauses.Length; ci += 1) {
                string cl = clauses[ci];
                if (cl.Length == 0) { return false; }
                if (cl.StartsWith("max=")) {
                    if (seenMax) { return false; }
                    string rawMax = cl.Substring(4);
                    if (rawMax.Length == 0) { return false; }
                    maxPaneId = UnescapeDockToken(rawMax);
                    seenMax = true;
                } else {
                    return false;
                }
            }
        }
        if (treePart.Length == 0) { return false; }

        var cursor = new DockParseCursor();
        cursor.Src = treePart;
        cursor.Pos = 0;
        DockLayoutNodeDesc ast = ParseNodeV2(cursor);
        if (cursor.Failed || ast == null || cursor.Pos != treePart.Length) {
            return false;
        }

        // Validated AST: apply atomically
        RestoreMaximizedPane();

        string[] allKnownIds = CollectPaneIdKeys();
        var usedTags = new Dictionary<string, bool>();
        var placedPanes = new Dictionary<string, bool>();
        var createdGroups = new DockTabGroup[0];

        if (_leftGroup == null) { _leftGroup = new DockTabGroup(); WireGroup(_leftGroup, "L"); }
        if (_centerGroup == null) { _centerGroup = new DockTabGroup(); WireGroup(_centerGroup, "C"); }
        if (_rightGroup == null) { _rightGroup = new DockTabGroup(); WireGroup(_rightGroup, "R"); }
        if (_bottomGroup == null) { _bottomGroup = new DockTabGroup(); WireGroup(_bottomGroup, "B"); }

        UIElement newRoot = MaterializeNodeV2(ast, usedTags, placedPanes, createdGroups);
        if (newRoot == null || createdGroups.Length == 0) { return false; }

        // Ensure no registered pane is lost if it wasn't present in the saved layout
        DockTabGroup fallbackGroup = IsNodeInTree(newRoot, _centerGroup) ? _centerGroup : createdGroups[0];
        for (int i = 0; i < allKnownIds.Length; i += 1) {
            string pid = allKnownIds[i];
            if (!placedPanes.ContainsKey(pid) && _panesById.ContainsKey(pid)) {
                placedPanes[pid] = true;
                fallbackGroup.AddPane(_panesById[pid], false);
            }
        }

        SetRootDockNode(newRoot);

        if (allKnownIds.Length > 0) {
            for (int gi = 0; gi < createdGroups.Length; gi += 1) {
                if (createdGroups[gi].PaneCount() == 0 && !NodeIsGroup(_rootNode, createdGroups[gi])) {
                    CollapseEmptyGroup(createdGroups[gi]);
                }
            }
        }

        // Rebind default 4-zone split pointers if root matches standard topology
        _mainHorizSplit = null;
        _centerRightSplit = null;
        _centerBottomSplit = null;
        if (_rootNode is DockSplitNode) {
            var mh = _rootNode as DockSplitNode;
            if (NodeIsGroup(mh.First, _leftGroup) && mh.Second is DockSplitNode) {
                var cr = mh.Second as DockSplitNode;
                if (NodeIsGroup(cr.Second, _rightGroup) && cr.First is DockSplitNode) {
                    var cb = cr.First as DockSplitNode;
                    if (NodeIsGroup(cb.First, _centerGroup) && NodeIsGroup(cb.Second, _bottomGroup)) {
                        _mainHorizSplit = mh;
                        _centerRightSplit = cr;
                        _centerBottomSplit = cb;
                        _savedLeftRatio = mh.SplitRatio;
                        _savedRightRatio = cr.SplitRatio;
                        _savedBottomRatio = cb.SplitRatio;
                    }
                }
            }
        }

        if (maxPaneId.Length > 0 && _panesById.ContainsKey(maxPaneId)) {
            MaximizePane(maxPaneId);
        }
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
    public float FloatW { get; set; } = 1.0f;
    public bool BoolValue { get; set; } = false;
    public string StringValue { get; set; } = "";
    public float MinValue { get; set; } = -1000000.0f;
    public float MaxValue { get; set; } = 1000000.0f;
    public float Step { get; set; } = 0.1f;
    public bool IsOverridden { get; set; } = false;
    public bool IsMixedValue { get; set; } = false;
    public bool IsReadOnly { get; set; } = false;

    public float ColorR { get; set; } = 1.0f;
    public float ColorG { get; set; } = 1.0f;
    public float ColorB { get; set; } = 1.0f;
    public float ColorA { get; set; } = 1.0f;
    public bool ColorHdr { get; set; } = false;
    public int IntValue { get; set; } = 0;
    public string[] EnumOptions { get; set; }
    public string AssetFilter { get; set; } = "";
    public string CustomHostId { get; set; } = "";
    public string GroupKey { get; set; } = "";
    public string HeaderText { get; set; } = "";
    public bool CanRemoveCard { get; set; } = false;

    public InspectorPropertyItem() {
        EnumOptions = new string[0];
    }
    public InspectorPropertyItem(string category, string path, string displayName, string typeKind) {
        EnumOptions = new string[0];
        Category = category == null ? "General" : category;
        PropertyPath = path == null ? "" : path;
        DisplayName = displayName == null ? PropertyPath : displayName;
        TypeKind = typeKind == null ? "float" : typeKind;
    }
}

internal class PropertyGridRowEntry {
    public string Key = "";
    public string CardKey = "";
    public string TypeKind = "float";
    public InspectorPropertyItem Item;

    public StackPanel WrapperPanel;
    public Border HeaderBorder;
    public TextBlock HeaderLabel;
    public Border RowRoot;
    public Grid RowGrid;
    public TextBlock NameLabel;
    public Button RevertButton;

    public UIElement EditorRoot;
    public NumericScrubber Scrubber;
    public Slider RangeSlider;
    public NumericScrubber RangeScrubber;
    public Vector2Field Vec2;
    public Vector3Field Vec3;
    public ColorPicker ColorEd;
    public ComboBox Combo;
    public CheckBox Check;
    public TextBox TextEd;
    public TextBox AssetText;
    public WrapPanel LayerWrap;
    public Button[] LayerButtons;
    public TextBlock LayerSummary;
    public Border CustomHostBox;

    public string[] EnumNames;
    public int[] EnumValues;
    public string CachedEnumSig = "";
    public string CachedLayerSig = "";
    public bool Syncing = false;

    public PropertyGridRowEntry() {
        LayerButtons = new Button[0];
        EnumNames = new string[0];
        EnumValues = new int[0];
    }

    public bool HasActiveFocus() {
        if (TextEd != null && TextEd.IsKeyboardFocusWithin) { return true; }
        if (AssetText != null && AssetText.IsKeyboardFocusWithin) { return true; }
        if (Scrubber != null && (Scrubber.IsInlineEditing() || Scrubber.IsKeyboardFocusWithin)) { return true; }
        if (RangeScrubber != null && (RangeScrubber.IsInlineEditing() || RangeScrubber.IsKeyboardFocusWithin)) { return true; }
        if (Vec2 != null && Vec2.IsKeyboardFocusWithin) { return true; }
        if (Vec3 != null && Vec3.IsKeyboardFocusWithin) { return true; }
        if (ColorEd != null && (ColorEd.IsPickerPopupOpen() || ColorEd.IsKeyboardFocusWithin)) { return true; }
        if (Combo != null && Combo.IsDropDownOpen) { return true; }
        return false;
    }
}

internal class PropertyGridElementList {
    public UIElement[] Items;
    public PropertyGridElementList() {
        Items = new UIElement[0];
    }
}

internal class PropertyGridCardEntry {
    public string CardKey = "";
    public string Category = "";
    public bool IsExpanded = true;
    public bool CanRemove = false;

    public Border CardRoot;
    public StackPanel CardStack;
    public Border HeaderBorder;
    public Button ToggleBtn;
    public TextBlock ChevronText;
    public TextBlock TitleLabel;
    public Button ResetBtn;
    public Button CopyBtn;
    public Button PasteBtn;
    public Button RemoveBtn;
    public StackPanel BodyPanel;
}

public class PropertyGrid : Control {
    private InspectorPropertyItem[] _items;
    private string _filterText = "";
    private Grid _rootGrid;
    private TextBox _searchBox;
    private ScrollViewer _scrollViewer;
    private StackPanel _rowsPanel;

    private Dictionary<string, PropertyGridRowEntry> _rowsByKey;
    private Dictionary<string, PropertyGridCardEntry> _cardsByKey;
    private Dictionary<string, bool> _cardCollapsedState;
    private Dictionary<string, Border> _customHostsById;
    private Dictionary<string, UIElement> _customHostContentById;
    private string[] _orderedRowKeys;
    private int _visibleRowCount = 0;

    public event ValueEditEventHandler PropertyEdited;
    public event ValueEditEventHandler PropertyCommitted;
    public event ValueEditEventHandler PropertyRevertRequested;
    public event PropertyCardActionEventHandler PropertyCardActionRequested;
    public event PropertyAssetActionEventHandler PropertyAssetPickRequested;

    public PropertyGrid() : base() {
        _items = new InspectorPropertyItem[0];
        _rowsByKey = new Dictionary<string, PropertyGridRowEntry>();
        _cardsByKey = new Dictionary<string, PropertyGridCardEntry>();
        _cardCollapsedState = new Dictionary<string, bool>();
        _customHostsById = new Dictionary<string, Border>();
        _customHostContentById = new Dictionary<string, UIElement>();
        _orderedRowKeys = new string[0];

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
        _rowsPanel.Spacing = 4.0f;
        _rowsPanel.Margin = new Thickness(4.0f);
        _scrollViewer.Content = _rowsPanel;
        _rootGrid.AddChild(_scrollViewer);
    }

    public override string GetTypeName() { return "PropertyGrid"; }

    internal void ApplySearchFilterText(string txt) {
        _filterText = txt == null ? "" : txt.Trim().ToLower();
        ReconcileRows();
    }

    internal void RaisePropertyEdited(ValueEditLifecycleEventArgs ev) { PropertyEdited(this, ev); }
    internal void RaisePropertyCommitted(ValueEditLifecycleEventArgs ev) { PropertyCommitted(this, ev); }
    internal void RaisePropertyRevert(ValueEditLifecycleEventArgs ev) { PropertyRevertRequested(this, ev); }
    internal void RaiseAssetAction(PropertyAssetActionEventArgs ev) { PropertyAssetPickRequested(this, ev); }

    public string FilterText {
        get { return _filterText; }
        set {
            string raw = value == null ? "" : value;
            _filterText = raw.Trim().ToLower();
            if (_searchBox != null && _searchBox.Text != raw) {
                _searchBox.Text = raw;
            } else {
                ReconcileRows();
            }
        }
    }

    public int VisibleRowCount() {
        return _visibleRowCount;
    }

    public void SetProperties(InspectorPropertyItem[] items) {
        _items = items == null ? new InspectorPropertyItem[0] : items;
        ReconcileRows();
    }

    public UIElement GetRowEditor(string propertyPath) {
        if (propertyPath == null || propertyPath.Length == 0) { return null; }
        for (int i = 0; i < _orderedRowKeys.Length; i += 1) {
            string k = _orderedRowKeys[i];
            if (_rowsByKey.ContainsKey(k)) {
                PropertyGridRowEntry e = _rowsByKey[k];
                if (e.Item != null && e.Item.PropertyPath == propertyPath) {
                    return e.EditorRoot;
                }
            }
        }
        return null;
    }

    public UIElement GetCustomHostContainer(string customHostId) {
        if (customHostId == null || customHostId.Length == 0) { return null; }
        if (_customHostsById.ContainsKey(customHostId)) {
            return _customHostsById[customHostId];
        }
        return null;
    }

    public void SetCustomHostContent(string customHostId, UIElement content) {
        if (customHostId == null || customHostId.Length == 0) { return; }
        if (content == null) {
            if (_customHostContentById.ContainsKey(customHostId)) {
                _customHostContentById.Remove(customHostId);
            }
        } else {
            _customHostContentById[customHostId] = content;
        }
        if (_customHostsById.ContainsKey(customHostId)) {
            Border b = _customHostsById[customHostId];
            if (b != null) {
                b.Child = content;
            }
        }
    }

    public bool IsCardExpanded(string groupOrCategoryKey) {
        if (groupOrCategoryKey == null || groupOrCategoryKey.Length == 0) { return true; }
        if (_cardCollapsedState.ContainsKey(groupOrCategoryKey)) {
            return !_cardCollapsedState[groupOrCategoryKey];
        }
        return true;
    }

    public void SetCardExpanded(string groupOrCategoryKey, bool expanded) {
        if (groupOrCategoryKey == null || groupOrCategoryKey.Length == 0) { return; }
        _cardCollapsedState[groupOrCategoryKey] = !expanded;
        if (_cardsByKey.ContainsKey(groupOrCategoryKey)) {
            ApplyCardExpandedVisual(_cardsByKey[groupOrCategoryKey], expanded);
        }
        RecountVisibleRows();
    }

    public void TriggerCardAction(string groupOrCategoryKey, string action) {
        if (groupOrCategoryKey == null || action == null) { return; }
        string cat = groupOrCategoryKey;
        if (_cardsByKey.ContainsKey(groupOrCategoryKey)) {
            cat = _cardsByKey[groupOrCategoryKey].Category;
        }
        PropertyCardActionRequested(this, new PropertyCardActionEventArgs(groupOrCategoryKey, cat, action, this));
    }

    public void AssignAssetPath(string propertyPath, string assetPath) {
        if (propertyPath == null || propertyPath.Length == 0) { return; }
        string nextPath = assetPath == null ? "" : assetPath;
        for (int i = 0; i < _orderedRowKeys.Length; i += 1) {
            string k = _orderedRowKeys[i];
            if (!_rowsByKey.ContainsKey(k)) { continue; }
            PropertyGridRowEntry entry = _rowsByKey[k];
            if (entry.Item == null || entry.Item.PropertyPath != propertyPath) { continue; }
            if (entry.Item.IsReadOnly) { return; }
            string oldTxt = entry.Item.StringValue;
            entry.Item.StringValue = nextPath;
            entry.Item.IsMixedValue = false;
            if (entry.AssetText != null) {
                entry.Syncing = true;
                entry.AssetText.Text = nextPath;
                entry.AssetText.PlaceholderText = entry.Item.AssetFilter.Length > 0 ? ("Asset (" + entry.Item.AssetFilter + ")") : "None (Asset)";
                entry.Syncing = false;
            }
            PropertyAssetPickRequested(this, new PropertyAssetActionEventArgs(propertyPath, "Drop", entry.Item.AssetFilter, nextPath, this));
            var outEv = new ValueEditLifecycleEventArgs(propertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, this);
            outEv.OldText = oldTxt;
            outEv.NewText = nextPath;
            RaisePropertyEdited(outEv);
            RaisePropertyCommitted(outEv);
            break;
        }
    }

    private bool MatchesFilter(InspectorPropertyItem item) {
        if (_filterText.Length == 0) { return true; }
        if (item.DisplayName != null && item.DisplayName.ToLower().Contains(_filterText)) { return true; }
        if (item.PropertyPath != null && item.PropertyPath.ToLower().Contains(_filterText)) { return true; }
        if (item.Category != null && item.Category.ToLower().Contains(_filterText)) { return true; }
        if (item.GroupKey != null && item.GroupKey.ToLower().Contains(_filterText)) { return true; }
        if (item.HeaderText != null && item.HeaderText.ToLower().Contains(_filterText)) { return true; }
        return false;
    }

    private static string ResolveCardKey(InspectorPropertyItem item) {
        if (item.GroupKey != null && item.GroupKey.Length > 0) { return item.GroupKey; }
        if (item.Category != null && item.Category.Length > 0) { return item.Category; }
        return "General";
    }

    private static string MakeBaseRowKey(InspectorPropertyItem item) {
        string tk = item.TypeKind == null ? "float" : item.TypeKind;
        if (item.PropertyPath != null && item.PropertyPath.Length > 0) {
            return item.PropertyPath + "\n" + tk;
        }
        string hdr = item.HeaderText == null ? "" : item.HeaderText;
        string cat = item.Category == null ? "General" : item.Category;
        return "__hdr__\n" + cat + "\n" + hdr + "\n" + tk;
    }

    private static int Pow2Int(int bit) {
        int p = 1;
        for (int i = 0; i < bit; i += 1) { p = p * 2; }
        return p;
    }

    private static bool HasMaskBit(int mask, int bit) {
        if (bit < 0 || bit > 31) { return false; }
        if (bit == 31) { return mask < 0; }
        int low31 = mask < 0 ? ((mask + 2147483647) + 1) : mask;
        int p = Pow2Int(bit);
        return ((low31 / p) % 2) != 0;
    }

    internal static int ToggleMaskBit(int mask, int bit) {
        if (bit < 0 || bit > 31) { return mask; }
        if (bit == 31) {
            return mask < 0 ? ((mask + 2147483647) + 1) : ((mask - 2147483647) - 1);
        }
        int low31 = mask < 0 ? ((mask + 2147483647) + 1) : mask;
        int p = Pow2Int(bit);
        if (((low31 / p) % 2) != 0) {
            return mask - p;
        }
        return mask + p;
    }

    internal static string FormatCompactFloat(float v) {
        bool neg = v < 0.0f;
        float av = neg ? -v : v;
        int scaled = EditorMath.RoundToInt(av * 100.0f);
        int whole = scaled / 100;
        int frac = scaled % 100;
        string fs = EditorMath.IntToStr(frac);
        if (fs.Length < 2) { fs = "0" + fs; }
        return (neg ? "-" : "") + EditorMath.IntToStr(whole) + "." + fs;
    }

    private static void ApplyCardExpandedVisual(PropertyGridCardEntry card, bool expanded) {
        if (card == null) { return; }
        card.IsExpanded = expanded;
        if (card.ChevronText != null) {
            card.ChevronText.Text = expanded ? "▾" : "▸";
        }
        if (card.BodyPanel != null) {
            card.BodyPanel.Visibility = expanded ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private Button MakeCardActionButton(string label, float width) {
        var btn = new Button();
        var txt = new TextBlock();
        txt.Text = label;
        txt.FontSize = 10.0f;
        txt.Foreground = new SolidColorBrush("#FFCBD5E1");
        btn.Content = txt;
        btn.Width = width;
        btn.Height = 20.0f;
        btn.Padding = new Thickness(2.0f, 0.0f, 2.0f, 0.0f);
        return btn;
    }

    private PropertyGridCardEntry GetOrCreateCard(string cardKey, string category) {
        if (_cardsByKey.ContainsKey(cardKey)) {
            PropertyGridCardEntry existing = _cardsByKey[cardKey];
            existing.Category = category;
            if (existing.TitleLabel != null) {
                existing.TitleLabel.Text = category;
            }
            return existing;
        }

        var card = new PropertyGridCardEntry();
        card.CardKey = cardKey;
        card.Category = category;
        card.IsExpanded = !_cardCollapsedState.ContainsKey(cardKey) || !_cardCollapsedState[cardKey];

        card.CardRoot = new Border();
        card.CardRoot.Background = new SolidColorBrush("#FF0F172A");
        card.CardRoot.BorderBrush = new SolidColorBrush("#FF1E293B");
        card.CardRoot.BorderThickness = new Thickness(1.0f);
        card.CardRoot.CornerRadius = new CornerRadius(4.0f);

        card.CardStack = new StackPanel();
        card.CardStack.Orientation = Orientation.Vertical;
        card.CardRoot.Child = card.CardStack;

        card.HeaderBorder = new Border();
        card.HeaderBorder.Background = new SolidColorBrush("#FF1E293B");
        card.HeaderBorder.Padding = new Thickness(6.0f, 3.0f, 4.0f, 3.0f);
        card.CardStack.AddChild(card.HeaderBorder);

        var headerGrid = new Grid();
        headerGrid.SetColumnDefinitions("22,*,Auto");
        headerGrid.ColumnSpacing = 4.0f;
        card.HeaderBorder.Child = headerGrid;

        card.ToggleBtn = new Button();
        card.ChevronText = new TextBlock();
        card.ChevronText.Text = card.IsExpanded ? "▾" : "▸";
        card.ChevronText.FontSize = 11.0f;
        card.ToggleBtn.Content = card.ChevronText;
        card.ToggleBtn.Width = 20.0f;
        card.ToggleBtn.Height = 20.0f;
        card.ToggleBtn.Padding = new Thickness(0.0f);
        UIElement.SetGridColumn(card.ToggleBtn, 0);
        headerGrid.AddChild(card.ToggleBtn);

        card.TitleLabel = new TextBlock();
        card.TitleLabel.Text = category;
        card.TitleLabel.FontSize = 11.0f;
        card.TitleLabel.FontWeight = "Bold";
        card.TitleLabel.Foreground = new SolidColorBrush("#FFE2E8F0");
        card.TitleLabel.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(card.TitleLabel, 1);
        headerGrid.AddChild(card.TitleLabel);

        var actions = new StackPanel();
        actions.Orientation = Orientation.Horizontal;
        actions.Spacing = 2.0f;
        card.ResetBtn = MakeCardActionButton("Reset", 38.0f);
        card.CopyBtn = MakeCardActionButton("Copy", 34.0f);
        card.PasteBtn = MakeCardActionButton("Paste", 38.0f);
        card.RemoveBtn = MakeCardActionButton("×", 20.0f);
        card.RemoveBtn.Visibility = Visibility.Collapsed;
        actions.AddChild(card.ResetBtn);
        actions.AddChild(card.CopyBtn);
        actions.AddChild(card.PasteBtn);
        actions.AddChild(card.RemoveBtn);
        UIElement.SetGridColumn(actions, 2);
        headerGrid.AddChild(actions);

        card.BodyPanel = new StackPanel();
        card.BodyPanel.Orientation = Orientation.Vertical;
        card.BodyPanel.Spacing = 2.0f;
        card.BodyPanel.Margin = new Thickness(2.0f);
        card.CardStack.AddChild(card.BodyPanel);

        ApplyCardExpandedVisual(card, card.IsExpanded);

        PropertyGrid self = this;
        PropertyGridCardEntry capturedCard = card;
        card.ToggleBtn.Click += (object s, RoutedEventArgs e) => {
            self.SetCardExpanded(capturedCard.CardKey, !capturedCard.IsExpanded);
        };
        card.ResetBtn.Click += (object s, RoutedEventArgs e) => {
            self.TriggerCardAction(capturedCard.CardKey, "Reset");
        };
        card.CopyBtn.Click += (object s, RoutedEventArgs e) => {
            self.TriggerCardAction(capturedCard.CardKey, "Copy");
        };
        card.PasteBtn.Click += (object s, RoutedEventArgs e) => {
            self.TriggerCardAction(capturedCard.CardKey, "Paste");
        };
        card.RemoveBtn.Click += (object s, RoutedEventArgs e) => {
            self.TriggerCardAction(capturedCard.CardKey, "Remove");
        };

        _cardsByKey[cardKey] = card;
        return card;
    }

    private static void ParseEnumOptionsIntoEntry(PropertyGridRowEntry entry, string[] opts) {
        int n = opts == null ? 0 : opts.Length;
        var names = new string[n];
        var vals = new int[n];
        string sig = "";
        for (int i = 0; i < n; i += 1) {
            string raw = opts[i] == null ? "" : opts[i];
            int eq = raw.IndexOf("=");
            if (eq > 0) {
                names[i] = raw.Substring(0, eq);
                vals[i] = ParseUtil.ParseInt(raw.Substring(eq + 1), i);
            } else {
                names[i] = raw;
                vals[i] = i;
            }
            sig = sig + raw + "\n";
        }
        entry.EnumNames = names;
        entry.EnumValues = vals;
        entry.CachedEnumSig = sig;
    }

    private void SyncColorPickerFromItem(ColorPicker cp, InspectorPropertyItem item) {
        if (cp == null || item == null) { return; }
        float r = item.ColorR;
        float g = item.ColorG;
        float b = item.ColorB;
        float a = item.ColorA;
        if (r == 1.0f && g == 1.0f && b == 1.0f && a == 1.0f && item.StringValue != null && item.StringValue.StartsWith("#")) {
            uint parsed = ParseUtil.ParseHexColor(item.StringValue, 0u);
            if (parsed != 0u || item.StringValue == "#00000000" || item.StringValue == "#0000") {
                a = (float)((parsed / 16777216u) % 256u) / 255.0f;
                b = (float)((parsed / 65536u) % 256u) / 255.0f;
                g = (float)((parsed / 256u) % 256u) / 255.0f;
                r = (float)(parsed % 256u) / 255.0f;
            }
        }
        cp.IsHdr = item.ColorHdr;
        cp.IsReadOnly = item.IsReadOnly;
        cp.PropertyPath = item.PropertyPath;
        cp.IsMixed = item.IsMixedValue;
        cp.SetColor(r, g, b, a, false);
    }

    internal void RefreshLayerMaskButtons(PropertyGridRowEntry entry, int maskVal, bool isMixed) {
        if (entry == null || entry.LayerButtons == null) { return; }
        for (int i = 0; i < entry.LayerButtons.Length; i += 1) {
            Button btn = entry.LayerButtons[i];
            if (btn == null) { continue; }
            bool active = !isMixed && HasMaskBit(maskVal, i);
            btn.Background = new SolidColorBrush(active ? "#FF0284C7" : "#FF1E293B");
        }
        if (entry.LayerSummary != null) {
            entry.LayerSummary.Text = isMixed ? "—" : ("0x" + EditorMath.IntToStr(maskVal));
        }
    }

    private PropertyGridRowEntry CreateRowEntry(string rowKey, string cardKey, InspectorPropertyItem item) {
        var entry = new PropertyGridRowEntry();
        entry.Key = rowKey;
        entry.CardKey = cardKey;
        entry.TypeKind = item.TypeKind == null ? "float" : item.TypeKind;
        entry.Item = item;

        entry.WrapperPanel = new StackPanel();
        entry.WrapperPanel.Orientation = Orientation.Vertical;
        entry.WrapperPanel.Spacing = 2.0f;

        entry.HeaderBorder = new Border();
        entry.HeaderBorder.Padding = new Thickness(6.0f, 4.0f, 6.0f, 2.0f);
        entry.HeaderBorder.Background = new SolidColorBrush("#FF162032");
        entry.HeaderBorder.CornerRadius = new CornerRadius(2.0f);
        entry.HeaderLabel = new TextBlock();
        entry.HeaderLabel.FontSize = 11.0f;
        entry.HeaderLabel.FontWeight = "Bold";
        entry.HeaderLabel.Foreground = new SolidColorBrush("#FF93C5FD");
        entry.HeaderBorder.Child = entry.HeaderLabel;
        entry.HeaderBorder.Visibility = Visibility.Collapsed;
        entry.WrapperPanel.AddChild(entry.HeaderBorder);

        entry.RowRoot = new Border();
        entry.RowRoot.Padding = new Thickness(6.0f, 3.0f, 4.0f, 3.0f);
        entry.RowRoot.Background = new SolidColorBrush("#FF111827");
        entry.RowRoot.BorderThickness = new Thickness(3.0f, 0.0f, 0.0f, 0.0f);
        entry.WrapperPanel.AddChild(entry.RowRoot);

        entry.RowGrid = new Grid();
        entry.RowGrid.SetColumnDefinitions("120,*,Auto");
        entry.RowGrid.ColumnSpacing = 6.0f;
        entry.RowRoot.Child = entry.RowGrid;

        entry.NameLabel = new TextBlock();
        entry.NameLabel.FontSize = 12.0f;
        entry.NameLabel.VerticalAlignment = VerticalAlignment.Center;
        UIElement.SetGridColumn(entry.NameLabel, 0);
        entry.RowGrid.AddChild(entry.NameLabel);

        PropertyGrid self = this;
        PropertyGridRowEntry capEntry = entry;
        string tk = entry.TypeKind;

        if (tk == "float" || tk == "int") {
            var scrub = new NumericScrubber();
            scrub.Label = tk == "int" ? "#" : "V";
            scrub.Decimals = tk == "int" ? 0 : 2;
            scrub.ValueChanged += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = ev.NewValue;
                capEntry.Item.IntValue = EditorMath.RoundToInt(ev.NewValue);
                capEntry.Item.IsMixedValue = false;
                ev.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyEdited(ev);
            };
            scrub.EditCommitted += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = ev.NewValue;
                capEntry.Item.IntValue = EditorMath.RoundToInt(ev.NewValue);
                capEntry.Item.IsMixedValue = false;
                ev.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyCommitted(ev);
            };
            entry.Scrubber = scrub;
            entry.EditorRoot = scrub;
        } else if (tk == "float_range" || tk == "int_range") {
            var rangeGrid = new Grid();
            rangeGrid.SetColumnDefinitions("*,72");
            rangeGrid.ColumnSpacing = 6.0f;

            var slider = new Slider();
            slider.VerticalAlignment = VerticalAlignment.Center;
            slider.IsSnapToTickEnabled = (tk == "int_range");
            UIElement.SetGridColumn(slider, 0);
            rangeGrid.AddChild(slider);

            var scrub = new NumericScrubber();
            scrub.Label = tk == "int_range" ? "#" : "R";
            scrub.Decimals = tk == "int_range" ? 0 : 2;
            UIElement.SetGridColumn(scrub, 1);
            rangeGrid.AddChild(scrub);

            slider.ValueChanged += (object s, RoutedPropertyChangedEventArgs<float> ev) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                float nv = tk == "int_range" ? (float)EditorMath.RoundToInt(ev.NewValue) : ev.NewValue;
                capEntry.Syncing = true;
                scrub.IsMixed = false;
                scrub.Value = nv;
                capEntry.Syncing = false;
                float oldV = capEntry.Item.FloatX;
                capEntry.Item.FloatX = nv;
                capEntry.Item.IntValue = EditorMath.RoundToInt(nv);
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, oldV, nv, EditLifecyclePhase.Change, self);
                outEv.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyEdited(outEv);
            };
            slider.MouseLeftButtonUp += (object s, MouseButtonEventArgs ev) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                float nv = capEntry.Item.FloatX;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, nv, nv, EditLifecyclePhase.Commit, self);
                outEv.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyCommitted(outEv);
            };
            scrub.ValueChanged += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Syncing = true;
                slider.Value = Math.Clampf(ev.NewValue, slider.Minimum, slider.Maximum);
                capEntry.Syncing = false;
                capEntry.Item.FloatX = ev.NewValue;
                capEntry.Item.IntValue = EditorMath.RoundToInt(ev.NewValue);
                capEntry.Item.IsMixedValue = false;
                ev.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyEdited(ev);
            };
            scrub.EditCommitted += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Syncing = true;
                slider.Value = Math.Clampf(ev.NewValue, slider.Minimum, slider.Maximum);
                capEntry.Syncing = false;
                capEntry.Item.FloatX = ev.NewValue;
                capEntry.Item.IntValue = EditorMath.RoundToInt(ev.NewValue);
                capEntry.Item.IsMixedValue = false;
                ev.IntValue = capEntry.Item.IntValue;
                self.RaisePropertyCommitted(ev);
            };

            entry.RangeSlider = slider;
            entry.RangeScrubber = scrub;
            entry.EditorRoot = rangeGrid;
        } else if (tk == "vec2") {
            var v2 = new Vector2Field();
            v2.VectorChanged += (object s, EventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = v2.X;
                capEntry.Item.FloatY = v2.Y;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, v2.X, EditLifecyclePhase.Change, self);
                outEv.ValueY = v2.Y;
                outEv.NewText = PropertyGrid.FormatCompactFloat(v2.X) + "," + PropertyGrid.FormatCompactFloat(v2.Y);
                self.RaisePropertyEdited(outEv);
            };
            v2.EditCommitted += (object s, EventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = v2.X;
                capEntry.Item.FloatY = v2.Y;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, v2.X, EditLifecyclePhase.Commit, self);
                outEv.ValueY = v2.Y;
                outEv.NewText = PropertyGrid.FormatCompactFloat(v2.X) + "," + PropertyGrid.FormatCompactFloat(v2.Y);
                self.RaisePropertyCommitted(outEv);
            };
            entry.Vec2 = v2;
            entry.EditorRoot = v2;
        } else if (tk == "vec3") {
            var v3 = new Vector3Field();
            v3.VectorChanged += (object s, EventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = v3.X;
                capEntry.Item.FloatY = v3.Y;
                capEntry.Item.FloatZ = v3.Z;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, v3.X, EditLifecyclePhase.Change, self);
                outEv.ValueY = v3.Y;
                outEv.ValueZ = v3.Z;
                outEv.NewText = PropertyGrid.FormatCompactFloat(v3.X) + "," + PropertyGrid.FormatCompactFloat(v3.Y) + "," + PropertyGrid.FormatCompactFloat(v3.Z);
                self.RaisePropertyEdited(outEv);
            };
            v3.EditCommitted += (object s, EventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.FloatX = v3.X;
                capEntry.Item.FloatY = v3.Y;
                capEntry.Item.FloatZ = v3.Z;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, v3.X, EditLifecyclePhase.Commit, self);
                outEv.ValueY = v3.Y;
                outEv.ValueZ = v3.Z;
                outEv.NewText = PropertyGrid.FormatCompactFloat(v3.X) + "," + PropertyGrid.FormatCompactFloat(v3.Y) + "," + PropertyGrid.FormatCompactFloat(v3.Z);
                self.RaisePropertyCommitted(outEv);
            };
            entry.Vec3 = v3;
            entry.EditorRoot = v3;
        } else if (tk == "color") {
            var cp = new ColorPicker();
            cp.ColorChanged += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.ColorR = ev.ColorR;
                capEntry.Item.ColorG = ev.ColorG;
                capEntry.Item.ColorB = ev.ColorB;
                capEntry.Item.ColorA = ev.ColorA;
                capEntry.Item.FloatX = ev.ColorR;
                capEntry.Item.FloatY = ev.ColorG;
                capEntry.Item.FloatZ = ev.ColorB;
                capEntry.Item.FloatW = ev.ColorA;
                capEntry.Item.StringValue = ev.NewText;
                capEntry.Item.IsMixedValue = false;
                self.RaisePropertyEdited(ev);
            };
            cp.EditCommitted += (object s, ValueEditLifecycleEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                capEntry.Item.ColorR = ev.ColorR;
                capEntry.Item.ColorG = ev.ColorG;
                capEntry.Item.ColorB = ev.ColorB;
                capEntry.Item.ColorA = ev.ColorA;
                capEntry.Item.FloatX = ev.ColorR;
                capEntry.Item.FloatY = ev.ColorG;
                capEntry.Item.FloatZ = ev.ColorB;
                capEntry.Item.FloatW = ev.ColorA;
                capEntry.Item.StringValue = ev.NewText;
                capEntry.Item.IsMixedValue = false;
                self.RaisePropertyCommitted(ev);
            };
            entry.ColorEd = cp;
            entry.EditorRoot = cp;
        } else if (tk == "enum") {
            var combo = new ComboBox();
            combo.Height = 26.0f;
            combo.SelectionChanged += (object s, SelectionChangedEventArgs ev) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                int idx = combo.SelectedIndex;
                if (idx < 0 || idx >= capEntry.EnumNames.Length) { return; }
                string optName = capEntry.EnumNames[idx];
                int optVal = capEntry.EnumValues[idx];
                float oldV = capEntry.Item.FloatX;
                string oldTxt = capEntry.Item.StringValue;
                capEntry.Item.IntValue = optVal;
                capEntry.Item.FloatX = (float)optVal;
                capEntry.Item.StringValue = optName;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, oldV, (float)optVal, EditLifecyclePhase.Commit, self);
                outEv.IntValue = optVal;
                outEv.OldText = oldTxt;
                outEv.NewText = optName;
                self.RaisePropertyEdited(outEv);
                self.RaisePropertyCommitted(outEv);
            };
            entry.Combo = combo;
            entry.EditorRoot = combo;
        } else if (tk == "asset") {
            var assetGrid = new Grid();
            assetGrid.SetColumnDefinitions("*,Auto,Auto");
            assetGrid.ColumnSpacing = 4.0f;

            var tb = new TextBox();
            tb.Height = 26.0f;
            tb.FontSize = 11.0f;
            UIElement.SetGridColumn(tb, 0);
            assetGrid.AddChild(tb);

            var pickBtn = new Button();
            var pickTxt = new TextBlock();
            pickTxt.Text = "Pick";
            pickTxt.FontSize = 11.0f;
            pickBtn.Content = pickTxt;
            pickBtn.Width = 40.0f;
            pickBtn.Height = 24.0f;
            pickBtn.Padding = new Thickness(2.0f, 0.0f, 2.0f, 0.0f);
            UIElement.SetGridColumn(pickBtn, 1);
            assetGrid.AddChild(pickBtn);

            var clrBtn = new Button();
            var clrTxt = new TextBlock();
            clrTxt.Text = "×";
            clrTxt.FontSize = 11.0f;
            clrBtn.Content = clrTxt;
            clrBtn.Width = 24.0f;
            clrBtn.Height = 24.0f;
            clrBtn.Padding = new Thickness(0.0f);
            UIElement.SetGridColumn(clrBtn, 2);
            assetGrid.AddChild(clrBtn);

            tb.KeyDown += (object s, KeyEventArgs ke) => {
                if (ke.Key == KeyCode.Enter && !capEntry.Syncing && !capEntry.Item.IsReadOnly) {
                    string oldTxt = capEntry.Item.StringValue;
                    capEntry.Item.StringValue = tb.Text == null ? "" : tb.Text;
                    capEntry.Item.IsMixedValue = false;
                    var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, self);
                    outEv.OldText = oldTxt;
                    outEv.NewText = capEntry.Item.StringValue;
                    self.RaisePropertyEdited(outEv);
                    self.RaisePropertyCommitted(outEv);
                    ke.Handled = true;
                }
            };
            tb.LostFocus += (object s, RoutedEventArgs re) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                string nxt = tb.Text == null ? "" : tb.Text;
                if (nxt != capEntry.Item.StringValue && !(capEntry.Item.IsMixedValue && nxt.Length == 0)) {
                    string oldTxt = capEntry.Item.StringValue;
                    capEntry.Item.StringValue = nxt;
                    capEntry.Item.IsMixedValue = false;
                    var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, self);
                    outEv.OldText = oldTxt;
                    outEv.NewText = nxt;
                    self.RaisePropertyEdited(outEv);
                    self.RaisePropertyCommitted(outEv);
                }
            };
            pickBtn.Click += (object s, RoutedEventArgs ev) => {
                if (capEntry.Item.IsReadOnly) { return; }
                self.RaiseAssetAction(new PropertyAssetActionEventArgs(
                    capEntry.Item.PropertyPath, "Pick", capEntry.Item.AssetFilter, capEntry.Item.StringValue, self));
            };
            clrBtn.Click += (object s, RoutedEventArgs ev) => {
                if (capEntry.Item.IsReadOnly) { return; }
                string oldTxt = capEntry.Item.StringValue;
                capEntry.Syncing = true;
                tb.Text = "";
                capEntry.Syncing = false;
                capEntry.Item.StringValue = "";
                capEntry.Item.IsMixedValue = false;
                self.RaiseAssetAction(new PropertyAssetActionEventArgs(
                    capEntry.Item.PropertyPath, "Clear", capEntry.Item.AssetFilter, "", self));
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, self);
                outEv.OldText = oldTxt;
                outEv.NewText = "";
                self.RaisePropertyEdited(outEv);
                self.RaisePropertyCommitted(outEv);
            };

            entry.AssetText = tb;
            entry.EditorRoot = assetGrid;
        } else if (tk == "layermask") {
            var wrap = new WrapPanel();
            wrap.Orientation = Orientation.Horizontal;
            entry.LayerWrap = wrap;
            entry.EditorRoot = wrap;
        } else if (tk == "custom_host") {
            var hostBorder = new Border();
            hostBorder.MinHeight = 26.0f;
            hostBorder.Background = new SolidColorBrush("#FF0B1120");
            hostBorder.BorderBrush = new SolidColorBrush("#FF1E293B");
            hostBorder.BorderThickness = new Thickness(1.0f);
            hostBorder.CornerRadius = new CornerRadius(3.0f);
            hostBorder.Padding = new Thickness(4.0f, 2.0f, 4.0f, 2.0f);
            entry.CustomHostBox = hostBorder;
            entry.EditorRoot = hostBorder;
        } else if (tk == "bool") {
            var cb = new CheckBox();
            cb.Click += (object s, RoutedEventArgs ev) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                capEntry.Item.BoolValue = cb.IsChecked;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(
                    capEntry.Item.PropertyPath,
                    capEntry.Item.BoolValue ? 0.0f : 1.0f,
                    capEntry.Item.BoolValue ? 1.0f : 0.0f,
                    EditLifecyclePhase.Commit,
                    self);
                self.RaisePropertyEdited(outEv);
                self.RaisePropertyCommitted(outEv);
            };
            entry.Check = cb;
            entry.EditorRoot = cb;
        } else {
            var tb = new TextBox();
            tb.Height = 26.0f;
            tb.FontSize = 12.0f;
            tb.TextChanged += (object s, TextChangedEventArgs ev) => {
                if (capEntry.Syncing) { return; }
                string oldTxt = capEntry.Item.StringValue;
                capEntry.Item.StringValue = tb.Text;
                capEntry.Item.IsMixedValue = false;
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Commit, self);
                outEv.OldText = oldTxt;
                outEv.NewText = capEntry.Item.StringValue;
                self.RaisePropertyEdited(outEv);
                self.RaisePropertyCommitted(outEv);
            };
            entry.TextEd = tb;
            entry.EditorRoot = tb;
        }

        if (entry.EditorRoot != null) {
            UIElement.SetGridColumn(entry.EditorRoot, 1);
            entry.RowGrid.AddChild(entry.EditorRoot);
        }

        entry.RevertButton = new Button();
        var revTxt = new TextBlock();
        revTxt.Text = "R";
        revTxt.FontSize = 10.0f;
        entry.RevertButton.Content = revTxt;
        entry.RevertButton.Width = 22.0f;
        entry.RevertButton.Height = 22.0f;
        entry.RevertButton.Padding = new Thickness(0.0f);
        entry.RevertButton.Click += (object s, RoutedEventArgs ev) => {
            self.RaisePropertyRevert(new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, 0.0f, 0.0f, EditLifecyclePhase.Cancel, self));
        };
        UIElement.SetGridColumn(entry.RevertButton, 2);
        entry.RowGrid.AddChild(entry.RevertButton);

        return entry;
    }

    private void RebuildLayerMaskChildren(PropertyGridRowEntry entry, InspectorPropertyItem item) {
        if (entry.LayerWrap == null) { return; }
        int count = 8;
        if (item.EnumOptions != null && item.EnumOptions.Length > 0) {
            count = EditorMath.Clampi(item.EnumOptions.Length, 1, 16);
        }
        string sig = EditorMath.IntToStr(count) + ":";
        for (int i = 0; i < count; i += 1) {
            string lbl = (item.EnumOptions != null && i < item.EnumOptions.Length && item.EnumOptions[i] != null && item.EnumOptions[i].Length > 0)
                ? item.EnumOptions[i]
                : EditorMath.IntToStr(i);
            sig = sig + lbl + "\n";
        }
        if (entry.CachedLayerSig == sig && entry.LayerButtons.Length == count) { return; }
        entry.CachedLayerSig = sig;
        entry.LayerWrap.ClearChildren();
        entry.LayerButtons = new Button[count];

        PropertyGrid self = this;
        PropertyGridRowEntry capEntry = entry;
        for (int i = 0; i < count; i += 1) {
            int bitIdx = i;
            string lbl = (item.EnumOptions != null && i < item.EnumOptions.Length && item.EnumOptions[i] != null && item.EnumOptions[i].Length > 0)
                ? item.EnumOptions[i]
                : EditorMath.IntToStr(i);
            var b = new Button();
            var bt = new TextBlock();
            bt.Text = lbl;
            bt.FontSize = 10.0f;
            bt.Foreground = new SolidColorBrush("#FFF8FAFC");
            b.Content = bt;
            b.MinWidth = 22.0f;
            b.Height = 20.0f;
            b.Padding = new Thickness(4.0f, 0.0f, 4.0f, 0.0f);
            b.Margin = new Thickness(0.0f, 0.0f, 2.0f, 2.0f);
            b.Click += (object s, RoutedEventArgs ev) => {
                if (capEntry.Syncing || capEntry.Item.IsReadOnly) { return; }
                int curMask = capEntry.Item.IntValue != 0 ? capEntry.Item.IntValue : EditorMath.RoundToInt(capEntry.Item.FloatX);
                int nextMask = PropertyGrid.ToggleMaskBit(curMask, bitIdx);
                capEntry.Item.IntValue = nextMask;
                capEntry.Item.FloatX = (float)nextMask;
                capEntry.Item.IsMixedValue = false;
                self.RefreshLayerMaskButtons(capEntry, nextMask, false);
                var outEv = new ValueEditLifecycleEventArgs(capEntry.Item.PropertyPath, (float)curMask, (float)nextMask, EditLifecyclePhase.Commit, self);
                outEv.IntValue = nextMask;
                outEv.NewText = EditorMath.IntToStr(nextMask);
                self.RaisePropertyEdited(outEv);
                self.RaisePropertyCommitted(outEv);
            };
            entry.LayerButtons[i] = b;
            entry.LayerWrap.AddChild(b);
        }
        entry.LayerSummary = new TextBlock();
        entry.LayerSummary.FontSize = 11.0f;
        entry.LayerSummary.Foreground = new SolidColorBrush("#FF94A3B8");
        entry.LayerSummary.VerticalAlignment = VerticalAlignment.Center;
        entry.LayerSummary.Margin = new Thickness(4.0f, 0.0f, 0.0f, 0.0f);
        entry.LayerWrap.AddChild(entry.LayerSummary);
    }

    private void UpdateRowFromItem(PropertyGridRowEntry entry, InspectorPropertyItem next) {
        if (entry == null || next == null) { return; }
        entry.Item = next;

        if (entry.HeaderBorder != null && entry.HeaderLabel != null) {
            if (next.HeaderText != null && next.HeaderText.Length > 0) {
                entry.HeaderLabel.Text = next.HeaderText;
                entry.HeaderBorder.Visibility = Visibility.Visible;
            } else {
                entry.HeaderBorder.Visibility = Visibility.Collapsed;
            }
        }

        if (entry.RowRoot != null) {
            entry.RowRoot.BorderBrush = new SolidColorBrush(next.IsOverridden ? "#FF38BDF8" : "#00000000");
        }
        if (entry.NameLabel != null) {
            entry.NameLabel.Text = next.IsMixedValue ? (next.DisplayName + " (—)") : next.DisplayName;
            entry.NameLabel.FontWeight = next.IsOverridden ? "Bold" : "Normal";
            entry.NameLabel.Foreground = new SolidColorBrush(next.IsOverridden ? "#FFF8FAFC" : "#FFCBD5E1");
        }
        if (entry.RevertButton != null) {
            entry.RevertButton.Visibility = next.IsOverridden ? Visibility.Visible : Visibility.Collapsed;
        }

        // Focus protection: never overwrite text/value/selection of an actively focused editor!
        if (entry.HasActiveFocus()) {
            return;
        }

        entry.Syncing = true;
        string tk = entry.TypeKind;
        if (tk == "float" || tk == "int") {
            NumericScrubber sc = entry.Scrubber;
            if (sc != null) {
                sc.Minimum = next.MinValue;
                sc.Maximum = next.MaxValue;
                sc.Step = tk == "int" ? 1.0f : next.Step;
                sc.IsReadOnly = next.IsReadOnly;
                sc.PropertyPath = next.PropertyPath;
                float val = (tk == "int" && next.IntValue != 0 && next.FloatX == 0.0f) ? (float)next.IntValue : next.FloatX;
                sc.Value = val;
                sc.IsMixed = next.IsMixedValue;
            }
        } else if (tk == "float_range" || tk == "int_range") {
            float mn = next.MinValue;
            float mx = next.MaxValue;
            if (mn <= -999999.0f && mx >= 999999.0f) {
                mn = 0.0f;
                mx = tk == "int_range" ? 100.0f : 1.0f;
            }
            float val = (tk == "int_range" && next.IntValue != 0 && next.FloatX == 0.0f) ? (float)next.IntValue : next.FloatX;
            if (entry.RangeSlider != null) {
                entry.RangeSlider.Minimum = mn;
                entry.RangeSlider.Maximum = mx;
                entry.RangeSlider.SmallChange = tk == "int_range" ? 1.0f : next.Step;
                entry.RangeSlider.TickFrequency = tk == "int_range" ? 1.0f : next.Step;
                entry.RangeSlider.IsEnabled = !next.IsReadOnly;
                entry.RangeSlider.Value = Math.Clampf(val, mn, mx);
            }
            if (entry.RangeScrubber != null) {
                entry.RangeScrubber.Minimum = mn;
                entry.RangeScrubber.Maximum = mx;
                entry.RangeScrubber.Step = tk == "int_range" ? 1.0f : next.Step;
                entry.RangeScrubber.IsReadOnly = next.IsReadOnly;
                entry.RangeScrubber.PropertyPath = next.PropertyPath;
                entry.RangeScrubber.Value = val;
                entry.RangeScrubber.IsMixed = next.IsMixedValue;
            }
        } else if (tk == "vec2") {
            if (entry.Vec2 != null) {
                entry.Vec2.Step = next.Step;
                entry.Vec2.IsReadOnly = next.IsReadOnly;
                entry.Vec2.X = next.FloatX;
                entry.Vec2.Y = next.FloatY;
                entry.Vec2.IsMixed = next.IsMixedValue;
            }
        } else if (tk == "vec3") {
            if (entry.Vec3 != null) {
                entry.Vec3.Step = next.Step;
                entry.Vec3.IsReadOnly = next.IsReadOnly;
                entry.Vec3.X = next.FloatX;
                entry.Vec3.Y = next.FloatY;
                entry.Vec3.Z = next.FloatZ;
                entry.Vec3.IsMixed = next.IsMixedValue;
            }
        } else if (tk == "color") {
            SyncColorPickerFromItem(entry.ColorEd, next);
        } else if (tk == "enum") {
            ComboBox cb = entry.Combo;
            if (cb != null) {
                string nextSig = "";
                if (next.EnumOptions != null) {
                    for (int i = 0; i < next.EnumOptions.Length; i += 1) {
                        nextSig = nextSig + (next.EnumOptions[i] == null ? "" : next.EnumOptions[i]) + "\n";
                    }
                }
                if (entry.CachedEnumSig != nextSig) {
                    ParseEnumOptionsIntoEntry(entry, next.EnumOptions);
                    cb.ClearItems();
                    for (int i = 0; i < entry.EnumNames.Length; i += 1) {
                        cb.AddItem(entry.EnumNames[i]);
                    }
                }
                cb.IsEnabled = !next.IsReadOnly;
                if (next.IsMixedValue) {
                    cb.Placeholder = "—";
                    cb.SelectedIndex = -1;
                } else {
                    cb.Placeholder = "";
                    int targetVal = next.IntValue != 0 ? next.IntValue : EditorMath.RoundToInt(next.FloatX);
                    int matchIdx = -1;
                    for (int i = 0; i < entry.EnumValues.Length; i += 1) {
                        if (next.StringValue != null && next.StringValue.Length > 0 && entry.EnumNames[i] == next.StringValue) {
                            matchIdx = i;
                            break;
                        }
                        if (entry.EnumValues[i] == targetVal) {
                            matchIdx = i;
                            break;
                        }
                    }
                    cb.SelectedIndex = matchIdx;
                }
            }
        } else if (tk == "asset") {
            if (entry.AssetText != null) {
                entry.AssetText.IsReadOnly = next.IsReadOnly;
                entry.AssetText.PlaceholderText = next.IsMixedValue
                    ? "—"
                    : (next.AssetFilter != null && next.AssetFilter.Length > 0 ? ("Asset (" + next.AssetFilter + ")") : "None (Asset)");
                string txt = next.IsMixedValue ? "" : (next.StringValue == null ? "" : next.StringValue);
                if (entry.AssetText.Text != txt) {
                    entry.AssetText.Text = txt;
                }
            }
        } else if (tk == "layermask") {
            RebuildLayerMaskChildren(entry, next);
            int maskVal = next.IntValue != 0 ? next.IntValue : EditorMath.RoundToInt(next.FloatX);
            RefreshLayerMaskButtons(entry, maskVal, next.IsMixedValue);
        } else if (tk == "custom_host") {
            if (entry.CustomHostBox != null) {
                string cid = next.CustomHostId == null ? "" : next.CustomHostId;
                entry.CustomHostBox.Name = "PART_CustomHost_" + cid;
                if (cid.Length > 0) {
                    _customHostsById[cid] = entry.CustomHostBox;
                    if (_customHostContentById.ContainsKey(cid)) {
                        UIElement customEl = _customHostContentById[cid];
                        if (entry.CustomHostBox.Child != customEl) {
                            entry.CustomHostBox.Child = customEl;
                        }
                    } else if (entry.CustomHostBox.Child == null) {
                        var ph = new TextBlock();
                        ph.Text = cid;
                        ph.FontSize = 11.0f;
                        ph.Foreground = new SolidColorBrush("#FF64748B");
                        entry.CustomHostBox.Child = ph;
                    }
                }
            }
        } else if (tk == "bool") {
            if (entry.Check != null) {
                entry.Check.IsEnabled = !next.IsReadOnly;
                entry.Check.IsIndeterminate = next.IsMixedValue;
                if (!next.IsMixedValue) {
                    entry.Check.IsChecked = next.BoolValue;
                }
            }
        } else {
            if (entry.TextEd != null) {
                entry.TextEd.IsReadOnly = next.IsReadOnly;
                entry.TextEd.PlaceholderText = next.IsMixedValue ? "—" : "";
                string txt = next.IsMixedValue ? "" : (next.StringValue == null ? "" : next.StringValue);
                if (entry.TextEd.Text != txt) {
                    entry.TextEd.Text = txt;
                }
            }
        }
        entry.Syncing = false;
    }

    private static bool SameElement(UIElement a, UIElement b) {
        if (a == null && b == null) { return true; }
        if (a == null || b == null) { return false; }
        return a.__nativeHandle == b.__nativeHandle;
    }

    private static bool ArrayContainsElement(UIElement[] arr, UIElement target) {
        for (int i = 0; i < arr.Length; i += 1) {
            if (SameElement(arr[i], target)) { return true; }
        }
        return false;
    }

    private static int FindChildIndexInPanel(StackPanel panel, UIElement target) {
        UIElement[] ch = panel.Children();
        for (int i = 0; i < ch.Length; i += 1) {
            if (SameElement(ch[i], target)) { return i; }
        }
        return -1;
    }

    private static void ReconcilePanelChildren(StackPanel panel, UIElement[] desired) {
        if (panel == null) { return; }
        // 1. Remove obsolete children not in desired
        UIElement[] existing = panel.Children();
        for (int i = 0; i < existing.Length; i += 1) {
            if (!ArrayContainsElement(desired, existing[i])) {
                panel.RemoveChild(existing[i]);
            }
        }
        // 2. Reconcile order without detaching focused elements
        for (int idx = 0; idx < desired.Length; idx += 1) {
            UIElement want = desired[idx];
            if (idx < panel.ChildCount() && SameElement(panel.ChildAt(idx), want)) {
                continue;
            }
            int curPos = FindChildIndexInPanel(panel, want);
            if (curPos < 0) {
                panel.InsertChild(idx, want);
            } else if (!want.IsKeyboardFocusWithin) {
                panel.RemoveChild(want);
                panel.InsertChild(idx, want);
            } else {
                // Keep focused subtree mounted in place; shift preceding non-focused sibling past it
                while (idx < panel.ChildCount() && !SameElement(panel.ChildAt(idx), want)) {
                    UIElement displace = panel.ChildAt(idx);
                    if (displace.IsKeyboardFocusWithin) { break; }
                    panel.RemoveChild(displace);
                    int wantPos = FindChildIndexInPanel(panel, want);
                    panel.InsertChild(wantPos + 1, displace);
                }
            }
        }
    }

    private void RecountVisibleRows() {
        int cnt = 0;
        for (int i = 0; i < _orderedRowKeys.Length; i += 1) {
            string rk = _orderedRowKeys[i];
            if (_rowsByKey.ContainsKey(rk)) {
                PropertyGridRowEntry re = _rowsByKey[rk];
                if (_cardsByKey.ContainsKey(re.CardKey) && _cardsByKey[re.CardKey].IsExpanded) {
                    cnt += 1;
                }
            }
        }
        _visibleRowCount = cnt;
    }

    private void ReconcileRows() {
        if (_rowsPanel == null) { return; }

        var nextCardOrder = new string[0];
        var cardCategoryByKey = new Dictionary<string, string>();
        var cardCanRemoveByKey = new Dictionary<string, bool>();
        var cardRowLists = new Dictionary<string, PropertyGridElementList>();
        var nextRowsByKey = new Dictionary<string, PropertyGridRowEntry>();
        var nextRowOrder = new string[0];
        var dupCount = new Dictionary<string, int>();

        for (int i = 0; i < _items.Length; i += 1) {
            InspectorPropertyItem item = _items[i];
            if (!MatchesFilter(item)) { continue; }

            string cardKey = ResolveCardKey(item);
            string cardCat = (item.Category != null && item.Category.Length > 0) ? item.Category : cardKey;
            if (!cardCategoryByKey.ContainsKey(cardKey)) {
                cardCategoryByKey[cardKey] = cardCat;
                cardCanRemoveByKey[cardKey] = item.CanRemoveCard;
                cardRowLists[cardKey] = new PropertyGridElementList();
                nextCardOrder.Push(cardKey);
            } else if (item.CanRemoveCard) {
                cardCanRemoveByKey[cardKey] = true;
            }

            string baseKey = MakeBaseRowKey(item);
            int seen = dupCount.ContainsKey(baseKey) ? dupCount[baseKey] : 0;
            dupCount[baseKey] = seen + 1;
            string rowKey = seen == 0 ? baseKey : (baseKey + "#dup" + EditorMath.IntToStr(seen));

            PropertyGridRowEntry entry = null;
            if (_rowsByKey.ContainsKey(rowKey)) {
                entry = _rowsByKey[rowKey];
                entry.CardKey = cardKey;
            } else {
                entry = CreateRowEntry(rowKey, cardKey, item);
            }
            UpdateRowFromItem(entry, item);

            nextRowsByKey[rowKey] = entry;
            nextRowOrder.Push(rowKey);
            cardRowLists[cardKey].Items.Push(entry.WrapperPanel);
        }

        // Remove cards no longer visible
        var nextCardsByKey = new Dictionary<string, PropertyGridCardEntry>();
        var desiredCardRoots = new UIElement[0];
        for (int ci = 0; ci < nextCardOrder.Length; ci += 1) {
            string ck = nextCardOrder[ci];
            PropertyGridCardEntry card = GetOrCreateCard(ck, cardCategoryByKey[ck]);
            card.CanRemove = cardCanRemoveByKey[ck];
            if (card.RemoveBtn != null) {
                card.RemoveBtn.Visibility = card.CanRemove ? Visibility.Visible : Visibility.Collapsed;
            }
            ReconcilePanelChildren(card.BodyPanel, cardRowLists[ck].Items);
            nextCardsByKey[ck] = card;
            desiredCardRoots.Push(card.CardRoot);
        }

        ReconcilePanelChildren(_rowsPanel, desiredCardRoots);

        _rowsByKey = nextRowsByKey;
        _cardsByKey = nextCardsByKey;
        _orderedRowKeys = nextRowOrder;
        RecountVisibleRows();
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
