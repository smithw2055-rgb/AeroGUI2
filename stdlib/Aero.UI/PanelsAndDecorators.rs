module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Viewbox & LayoutScaler
// ============================================================================

public class Viewbox : Decorator {
    private Stretch _stretch = Stretch.Uniform;
    private StretchDirection _stretchDir = StretchDirection.Both;

    public Viewbox() : base() {
        SyncViewboxMode();
    }

    public override string GetTypeName() { return "Viewbox"; }

    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; SyncViewboxMode(); }
    }
    public StretchDirection StretchDirection {
        get { return _stretchDir; }
        set { _stretchDir = value; SyncViewboxMode(); }
    }

    private void SyncViewboxMode() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetLayoutMode(
                __nativeHandle,
                EnumConvert.ToInt(LayoutMode.Viewbox),
                0.0f, 0.0f,
                EnumConvert.ToInt(_stretch),
                EnumConvert.ToInt(_stretchDir));
        }
    }
}

public class LayoutScaler : Viewbox {
    public float DesignWidth { get; set; } = 1920.0f;
    public float DesignHeight { get; set; } = 1080.0f;

    public LayoutScaler() : base() {
        Stretch = Stretch.Uniform;
        StretchDirection = StretchDirection.Both;
    }

    public override string GetTypeName() { return "LayoutScaler"; }
}

// ============================================================================
// 2. Panel Base & Standard Panels (Stack, Dock, Wrap, UniformGrid, Canvas)
// ============================================================================

public class Panel : FrameworkElement {
    public bool IsItemsHost { get; set; } = false;

    public Panel() : base(VisualKind.Box) {
        SyncInteractiveFlags();
    }
    public override string GetTypeName() { return "Panel"; }

    protected override bool IsSelfHitTestSolid() {
        return Background != null;
    }

    public void Add(UIElement child) { AddChild(child); }

    protected void SetNativeLayoutMode(LayoutMode mode, float p0, float p1, int i0, int i1) {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetLayoutMode(__nativeHandle, EnumConvert.ToInt(mode), p0, p1, i0, i1);
        }
    }
}

public class StackPanel : Panel {
    private Orientation _orientation = Orientation.Vertical;
    private float _spacing = 0.0f;

    public StackPanel() : base() {
        SyncStackLayout();
    }
    public StackPanel(Orientation orientation, float spacing = 0.0f) : base() {
        _orientation = orientation;
        _spacing = spacing;
        SyncStackLayout();
    }

    public override string GetTypeName() { return "StackPanel"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; SyncStackLayout(); }
    }
    public float Spacing {
        get { return _spacing; }
        set { _spacing = value >= 0.0f ? value : 0.0f; SyncStackLayout(); }
    }

    private void SyncStackLayout() {
        SetNativeLayoutMode(LayoutMode.Stack, _spacing, 0.0f, EnumConvert.ToInt(_orientation), 0);
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Orientation") {
            if (value is BoxedInt) { Orientation = EnumConvert.ToOrientation((value as BoxedInt).Value); return true; }
            string s = DependencyObject.UnboxString(value, "Vertical");
            Orientation = s == "Horizontal" ? Orientation.Horizontal : Orientation.Vertical;
            return true;
        }
        if (propName == "Spacing") { Spacing = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

public class VirtualizingStackPanel : StackPanel {
    public VirtualizingStackPanel() : base() {}
    public override string GetTypeName() { return "VirtualizingStackPanel"; }
}

public class TabPanel : StackPanel {
    public TabPanel() : base(Orientation.Horizontal, 0.0f) {}
    public override string GetTypeName() { return "TabPanel"; }
}

public class ToolBarPanel : StackPanel {
    public ToolBarPanel() : base(Orientation.Horizontal, 2.0f) {}
    public override string GetTypeName() { return "ToolBarPanel"; }
}

public class DockPanel : Panel {
    private bool _lastChildFill = true;

    public DockPanel() : base() {
        SyncDockLayout();
    }

    public override string GetTypeName() { return "DockPanel"; }

    public bool LastChildFill {
        get { return _lastChildFill; }
        set { _lastChildFill = value; SyncDockLayout(); }
    }

    private void SyncDockLayout() {
        SetNativeLayoutMode(LayoutMode.Dock, 0.0f, 0.0f, _lastChildFill ? 1 : 0, 0);
    }
}

public class WrapPanel : Panel {
    private Orientation _orientation = Orientation.Horizontal;
    private float _itemWidth = ParseUtil.NaN();
    private float _itemHeight = ParseUtil.NaN();
    private float _spacing = 0.0f;

    public WrapPanel() : base() {
        SyncWrapLayout();
    }

    public override string GetTypeName() { return "WrapPanel"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; SyncWrapLayout(); }
    }
    public float ItemWidth {
        get { return _itemWidth; }
        set { _itemWidth = value; SyncWrapLayout(); }
    }
    public float ItemHeight {
        get { return _itemHeight; }
        set { _itemHeight = value; SyncWrapLayout(); }
    }
    public float Spacing {
        get { return _spacing; }
        set { _spacing = value >= 0.0f ? value : 0.0f; SyncWrapLayout(); }
    }

    private void SyncWrapLayout() {
        SetNativeLayoutMode(
            LayoutMode.Wrap,
            ParseUtil.IsNaN(_itemWidth) ? _spacing : _itemWidth,
            ParseUtil.IsNaN(_itemHeight) ? _spacing : _itemHeight,
            EnumConvert.ToInt(_orientation),
            0);
    }
}

public class ToolBarOverflowPanel : WrapPanel {
    public ToolBarOverflowPanel() : base() {}
    public override string GetTypeName() { return "ToolBarOverflowPanel"; }
}

public class UniformGrid : Panel {
    private int _rows = 0;
    private int _columns = 0;
    private int _firstColumn = 0;
    private float _spacing = 0.0f;

    public UniformGrid() : base() {
        SyncUniformLayout();
    }

    public override string GetTypeName() { return "UniformGrid"; }

    public int Rows {
        get { return _rows; }
        set { _rows = value >= 0 ? value : 0; SyncUniformLayout(); }
    }
    public int Columns {
        get { return _columns; }
        set { _columns = value >= 0 ? value : 0; SyncUniformLayout(); }
    }
    public int FirstColumn {
        get { return _firstColumn; }
        set { _firstColumn = value >= 0 ? value : 0; SyncUniformLayout(); }
    }
    public float Spacing {
        get { return _spacing; }
        set { _spacing = value >= 0.0f ? value : 0.0f; SyncUniformLayout(); }
    }

    private void SyncUniformLayout() {
        SetNativeLayoutMode(LayoutMode.UniformGrid, _spacing, _spacing, _rows, _columns);
    }
}

public class Canvas : Panel {
    public Canvas() : base() {
        SetNativeLayoutMode(LayoutMode.Canvas, 0.0f, 0.0f, 0, 0);
    }
    public override string GetTypeName() { return "Canvas"; }
}

// ============================================================================
// 3. Grid, GridLength, RowDefinition & ColumnDefinition
// ============================================================================

public class GridLength : DependencyObject {
    public float Value { get; set; } = 1.0f;
    public GridUnitType GridUnitType { get; set; } = GridUnitType.Star;

    public GridLength() {}
    public GridLength(float value, GridUnitType unitType = GridUnitType.Pixel) {
        Value = value;
        GridUnitType = unitType;
    }

    public static GridLength Auto() { return new GridLength(1.0f, GridUnitType.Auto); }
    public static GridLength FromPixel(float px) { return new GridLength(px, GridUnitType.Pixel); }
    public static GridLength FromStar(float weight = 1.0f) { return new GridLength(weight, GridUnitType.Star); }

    public static GridLength Parse(string spec) {
        if (spec == null) { return Auto(); }
        string s = spec.Trim();
        if (s.Length == 0 || s == "Auto" || s == "auto") { return Auto(); }
        if (s.EndsWith("*")) {
            string num = s.Substring(0, s.Length - 1).Trim();
            float weight = num.Length == 0 ? 1.0f : ParseUtil.ParseFloat(num, 1.0f);
            return FromStar(weight);
        }
        return FromPixel(ParseUtil.ParseFloat(s, 0.0f));
    }
}

public class RowDefinition : DependencyObject {
    public GridLength Height { get; set; }
    public float MinHeight { get; set; } = 0.0f;
    public float MaxHeight { get; set; } = ParseUtil.PositiveInfinity();

    public RowDefinition() { Height = GridLength.FromStar(1.0f); }
    public RowDefinition(GridLength height) { Height = height == null ? GridLength.FromStar(1.0f) : height; }
    public RowDefinition(float value, GridUnitType unit = GridUnitType.Star) {
        Height = new GridLength(value, unit);
    }

    public static RowDefinition Auto() { return new RowDefinition(GridLength.Auto()); }
    public static RowDefinition Pixel(float px) { return new RowDefinition(GridLength.FromPixel(px)); }
    public static RowDefinition Star(float weight = 1.0f) { return new RowDefinition(GridLength.FromStar(weight)); }
}

public class ColumnDefinition : DependencyObject {
    public GridLength Width { get; set; }
    public float MinWidth { get; set; } = 0.0f;
    public float MaxWidth { get; set; } = ParseUtil.PositiveInfinity();

    public ColumnDefinition() { Width = GridLength.FromStar(1.0f); }
    public ColumnDefinition(GridLength width) { Width = width == null ? GridLength.FromStar(1.0f) : width; }
    public ColumnDefinition(float value, GridUnitType unit = GridUnitType.Star) {
        Width = new GridLength(value, unit);
    }

    public static ColumnDefinition Auto() { return new ColumnDefinition(GridLength.Auto()); }
    public static ColumnDefinition Pixel(float px) { return new ColumnDefinition(GridLength.FromPixel(px)); }
    public static ColumnDefinition Star(float weight = 1.0f) { return new ColumnDefinition(GridLength.FromStar(weight)); }
}

public class RowDefinitionCollection : DependencyObject {
    private Grid _owner;
    private RowDefinition[] _items;

    public RowDefinitionCollection(Grid owner) {
        _owner = owner;
        _items = new RowDefinition[0];
    }

    public int Count() { return _items.Length; }
    public RowDefinition Get(int index) { return _items[index]; }
    public RowDefinition[] Items() { return _items; }

    public void Add(RowDefinition def) {
        if (def == null) { return; }
        _items.Push(def);
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
    public void Clear() {
        _items.Clear();
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
}

public class ColumnDefinitionCollection : DependencyObject {
    private Grid _owner;
    private ColumnDefinition[] _items;

    public ColumnDefinitionCollection(Grid owner) {
        _owner = owner;
        _items = new ColumnDefinition[0];
    }

    public int Count() { return _items.Length; }
    public ColumnDefinition Get(int index) { return _items[index]; }
    public ColumnDefinition[] Items() { return _items; }

    public void Add(ColumnDefinition def) {
        if (def == null) { return; }
        _items.Push(def);
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
    public void Clear() {
        _items.Clear();
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
}

public class Grid : Panel {
    private RowDefinitionCollection _rowDefs;
    private ColumnDefinitionCollection _colDefs;
    private float _rowSpacing = 0.0f;
    private float _colSpacing = 0.0f;
    public bool ShowGridLines { get; set; } = false;

    public Grid() : base() {
        _rowDefs = new RowDefinitionCollection(this);
        _colDefs = new ColumnDefinitionCollection(this);
        SyncGridTracks();
    }

    public override string GetTypeName() { return "Grid"; }

    public RowDefinitionCollection RowDefinitions { get { return _rowDefs; } }
    public ColumnDefinitionCollection ColumnDefinitions { get { return _colDefs; } }

    public float RowSpacing {
        get { return _rowSpacing; }
        set { _rowSpacing = value >= 0.0f ? value : 0.0f; SyncGridTracks(); }
    }
    public float ColumnSpacing {
        get { return _colSpacing; }
        set { _colSpacing = value >= 0.0f ? value : 0.0f; SyncGridTracks(); }
    }

    public void AddRowDefinition(GridUnitType unit, float value = 1.0f) {
        _rowDefs.Add(new RowDefinition(value, unit));
    }
    public void AddColumnDefinition(GridUnitType unit, float value = 1.0f) {
        _colDefs.Add(new ColumnDefinition(value, unit));
    }
    public void ClearRowDefinitions() { _rowDefs.Clear(); }
    public void ClearColumnDefinitions() { _colDefs.Clear(); }

    public void SetRowDefinitions(string spec) {
        _rowDefs.Clear();
        if (spec == null || spec.Length == 0) { return; }
        string[] parts = spec.Split(",");
        for (int i = 0; i < parts.Length; i += 1) {
            _rowDefs.Add(new RowDefinition(GridLength.Parse(parts[i])));
        }
    }

    public void SetColumnDefinitions(string spec) {
        _colDefs.Clear();
        if (spec == null || spec.Length == 0) { return; }
        string[] parts = spec.Split(",");
        for (int i = 0; i < parts.Length; i += 1) {
            _colDefs.Add(new ColumnDefinition(GridLength.Parse(parts[i])));
        }
    }

    public void SyncGridTracks() {
        SetNativeLayoutMode(LayoutMode.Grid, _colSpacing, _rowSpacing, 0, 0);
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Visual_GridClearDefs(__nativeHandle);
        RowDefinition[] rows = _rowDefs.Items();
        for (int i = 0; i < rows.Length; i += 1) {
            GridLength gl = rows[i].Height;
            Visual_GridAddRowDef(__nativeHandle, EnumConvert.ToInt(gl.GridUnitType), gl.Value);
        }
        ColumnDefinition[] cols = _colDefs.Items();
        for (int i = 0; i < cols.Length; i += 1) {
            GridLength gl = cols[i].Width;
            Visual_GridAddColDef(__nativeHandle, EnumConvert.ToInt(gl.GridUnitType), gl.Value);
        }
    }
}
