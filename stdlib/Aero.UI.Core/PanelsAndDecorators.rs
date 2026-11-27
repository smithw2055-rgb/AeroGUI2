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

    protected virtual void SyncViewboxMode() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetLayoutMode(
                __nativeHandle,
                EnumConvert.ToInt(LayoutMode.Viewbox),
                0.0f, 0.0f,
                EnumConvert.ToInt(_stretch),
                EnumConvert.ToInt(_stretchDir));
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Stretch") {
            if (value is BoxedInt) { Stretch = EnumConvert.ToStretch((value as BoxedInt).Value); return true; }
            string st = DependencyObject.UnboxString(value, "Uniform").Trim();
            if (st == "None" || st == "0") { Stretch = Stretch.None; return true; }
            if (st == "Fill" || st == "1") { Stretch = Stretch.Fill; return true; }
            if (st == "UniformToFill" || st == "3") { Stretch = Stretch.UniformToFill; return true; }
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
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Stretch") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretch)); }
        if (propName == "StretchDirection") { return DependencyObject.BoxInt(EnumConvert.ToInt(_stretchDir)); }
        return base.GetPropertyValue(propName);
    }
}

public class LayoutScaler : Viewbox {
    public float DesignWidth { get; set; } = 1920.0f;
    public float DesignHeight { get; set; } = 1080.0f;
    private Size _referenceResolution;

    public LayoutScaler() : base() {
        _referenceResolution = new Size(1920.0f, 1080.0f);
        Stretch = Stretch.Uniform;
        StretchDirection = StretchDirection.Both;
        SyncViewboxMode();
    }

    public override string GetTypeName() { return "LayoutScaler"; }

    public Size ReferenceResolution {
        get { return _referenceResolution; }
        set {
            _referenceResolution = value == null ? new Size(0.0f, 0.0f) : value;
            SyncViewboxMode();
        }
    }

    protected override void SyncViewboxMode() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            float rw = _referenceResolution != null ? _referenceResolution.Width : 0.0f;
            float rh = _referenceResolution != null ? _referenceResolution.Height : 0.0f;
            Visual_SetLayoutMode(
                __nativeHandle,
                EnumConvert.ToInt(LayoutMode.Viewbox),
                rw, rh,
                EnumConvert.ToInt(Stretch),
                EnumConvert.ToInt(StretchDirection) + 256);
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "ReferenceResolution") {
            if (value is Size) {
                ReferenceResolution = value as Size;
            } else {
                ReferenceResolution = Size.Parse(DependencyObject.UnboxString(value, "0,0"));
            }
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "ReferenceResolution") { return _referenceResolution; }
        return base.GetPropertyValue(propName);
    }
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

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsItemsHost") {
            IsItemsHost = DependencyObject.UnboxBool(value, false);
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsItemsHost") { return DependencyObject.BoxBool(IsItemsHost); }
        if (propName == "Children") {
            var col = new ArrayCollectionBox();
            UIElement[] kids = Children();
            for (int i = 0; i < kids.Length; i += 1) {
                col.AddBox(kids[i]);
            }
            return col;
        }
        return base.GetPropertyValue(propName);
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

public interface IVirtualizingItemsPanel {
    bool IsVirtualizingEnabled();
    int ComputeItemsPerRow(float viewportWidth, float estimatedItemWidth);
    float ComputeRowStride(float estimatedItemHeight);
    void ApplyVirtualWindowPadding(int firstRow, int lastRow, int totalRows, float rowStride);
    void ResetVirtualWindowPadding();
}

public class VirtualizingStackPanel : StackPanel, IVirtualizingItemsPanel {
    private bool _hasBasePad = false;
    private float _basePadL = 0.0f;
    private float _basePadT = 0.0f;
    private float _basePadR = 0.0f;
    private float _basePadB = 0.0f;
    private float _lastVirtPadTop = -1.0f;
    private float _lastVirtPadBottom = -1.0f;

    public VirtualizingStackPanel() : base() {}
    public override string GetTypeName() { return "VirtualizingStackPanel"; }

    public virtual bool IsVirtualizingEnabled() { return true; }
    public virtual int ComputeItemsPerRow(float viewportWidth, float estimatedItemWidth) { return 1; }
    public virtual float ComputeRowStride(float estimatedItemHeight) {
        float h = estimatedItemHeight > 8.0f ? estimatedItemHeight : 32.0f;
        return h + Spacing;
    }
    public virtual void ApplyVirtualWindowPadding(int firstRow, int lastRow, int totalRows, float rowStride) {
        if (!_hasBasePad) {
            Thickness p = Padding;
            if (p != null) {
                _basePadL = p.Left;
                _basePadT = p.Top;
                _basePadR = p.Right;
                _basePadB = p.Bottom;
            }
            _hasBasePad = true;
        }
        int remRows = totalRows > (lastRow + 1) ? (totalRows - 1 - lastRow) : 0;
        float topPad = _basePadT + (float)firstRow * rowStride;
        float botPad = _basePadB + (float)remRows * rowStride;
        if (topPad != _lastVirtPadTop || botPad != _lastVirtPadBottom) {
            _lastVirtPadTop = topPad;
            _lastVirtPadBottom = botPad;
            SetPadding4(_basePadL, topPad, _basePadR, botPad);
        }
    }
    public virtual void ResetVirtualWindowPadding() {
        if (_hasBasePad && (_lastVirtPadTop != _basePadT || _lastVirtPadBottom != _basePadB)) {
            _lastVirtPadTop = _basePadT;
            _lastVirtPadBottom = _basePadB;
            SetPadding4(_basePadL, _basePadT, _basePadR, _basePadB);
        }
    }
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

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "LastChildFill") {
            LastChildFill = DependencyObject.UnboxBool(value, true);
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "LastChildFill") { return DependencyObject.BoxBool(_lastChildFill); }
        return base.GetPropertyValue(propName);
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
            ParseUtil.IsNaN(_itemWidth) ? 0.0f : _itemWidth,
            ParseUtil.IsNaN(_itemHeight) ? 0.0f : _itemHeight,
            EnumConvert.ToInt(_orientation),
            (int)(_spacing * 1000.0f));
    }

    private static float ParseItemLengthBox(DependencyObject value) {
        if (value == null) { return ParseUtil.NaN(); }
        if (value is BoxedFloat) { return (value as BoxedFloat).Value; }
        if (value is BoxedInt && !(value is BoxedEnum)) { return (float)(value as BoxedInt).Value; }
        object rawObj = DependencyObject.Unbox(value);
        string s = DependencyObject.UnboxString(value, "").Trim();
        if ((s.Length == 0 || s == "ComboBoxItem" || s == "ListBoxItem") && rawObj is UIElement) {
            s = DependencyObject.UnboxString((rawObj as UIElement).GetContentBoxInternal(), "").Trim();
        }
        string low = s.ToLower();
        if (low.Length == 0 || low == "auto" || low == "nan") { return ParseUtil.NaN(); }
        return ParseUtil.ParseFloat(s, ParseUtil.NaN());
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Orientation") {
            if (value is BoxedInt && !(value is BoxedEnum)) {
                Orientation = (value as BoxedInt).Value == 1 ? Orientation.Vertical : Orientation.Horizontal;
                return true;
            }
            object rawObj = DependencyObject.Unbox(value);
            string os = DependencyObject.UnboxString(value, "Horizontal").Trim();
            if ((os.Length == 0 || os == "ComboBoxItem" || os == "ListBoxItem") && rawObj is UIElement) {
                os = DependencyObject.UnboxString((rawObj as UIElement).GetContentBoxInternal(), "Horizontal").Trim();
            }
            Orientation = os == "Vertical" ? Orientation.Vertical : Orientation.Horizontal;
            return true;
        }
        if (propName == "ItemWidth") { ItemWidth = ParseItemLengthBox(value); return true; }
        if (propName == "ItemHeight") { ItemHeight = ParseItemLengthBox(value); return true; }
        if (propName == "Spacing") { Spacing = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        if (propName == "ItemWidth") { return DependencyObject.BoxFloat(_itemWidth); }
        if (propName == "ItemHeight") { return DependencyObject.BoxFloat(_itemHeight); }
        if (propName == "Spacing") { return DependencyObject.BoxFloat(_spacing); }
        return base.GetPropertyValue(propName);
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
        SetNativeLayoutMode(LayoutMode.UniformGrid, (float)_firstColumn, _spacing, _rows, _columns);
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Rows") { Rows = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "Columns") { Columns = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "FirstColumn") { FirstColumn = DependencyObject.UnboxInt(value, 0); return true; }
        if (propName == "Spacing") { Spacing = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Rows") { return DependencyObject.BoxInt(_rows); }
        if (propName == "Columns") { return DependencyObject.BoxInt(_columns); }
        if (propName == "FirstColumn") { return DependencyObject.BoxInt(_firstColumn); }
        if (propName == "Spacing") { return DependencyObject.BoxFloat(_spacing); }
        return base.GetPropertyValue(propName);
    }
}

public class VirtualizingUniformGrid : UniformGrid, IVirtualizingItemsPanel {
    private bool _hasBasePad = false;
    private float _basePadL = 0.0f;
    private float _basePadT = 0.0f;
    private float _basePadR = 0.0f;
    private float _basePadB = 0.0f;
    private float _lastVirtPadTop = -1.0f;
    private float _lastVirtPadBottom = -1.0f;

    public VirtualizingUniformGrid() : base() {}
    public override string GetTypeName() { return "VirtualizingUniformGrid"; }

    public virtual bool IsVirtualizingEnabled() { return true; }
    public virtual int ComputeItemsPerRow(float viewportWidth, float estimatedItemWidth) {
        if (Columns > 0) { return Columns; }
        float cellW = estimatedItemWidth > 16.0f ? estimatedItemWidth : 120.0f;
        if (viewportWidth <= 0.0f) { return 1; }
        float denom = cellW + Spacing;
        int cols = denom > 0.0f ? (int)((viewportWidth + Spacing) / denom) : 1;
        return cols > 1 ? cols : 1;
    }
    public virtual float ComputeRowStride(float estimatedItemHeight) {
        float h = estimatedItemHeight > 8.0f ? estimatedItemHeight : 32.0f;
        return h + Spacing;
    }
    public virtual void ApplyVirtualWindowPadding(int firstRow, int lastRow, int totalRows, float rowStride) {
        if (!_hasBasePad) {
            Thickness p = Padding;
            if (p != null) {
                _basePadL = p.Left;
                _basePadT = p.Top;
                _basePadR = p.Right;
                _basePadB = p.Bottom;
            }
            _hasBasePad = true;
        }
        int remRows = totalRows > (lastRow + 1) ? (totalRows - 1 - lastRow) : 0;
        float topPad = _basePadT + (float)firstRow * rowStride;
        float botPad = _basePadB + (float)remRows * rowStride;
        if (topPad != _lastVirtPadTop || botPad != _lastVirtPadBottom) {
            _lastVirtPadTop = topPad;
            _lastVirtPadBottom = botPad;
            SetPadding4(_basePadL, topPad, _basePadR, botPad);
        }
    }
    public float VirtualTopPadding() { return _lastVirtPadTop; }
    public float VirtualBottomPadding() { return _lastVirtPadBottom; }
    public virtual void ResetVirtualWindowPadding() {
        if (_hasBasePad && (_lastVirtPadTop != _basePadT || _lastVirtPadBottom != _basePadB)) {
            _lastVirtPadTop = _basePadT;
            _lastVirtPadBottom = _basePadB;
            SetPadding4(_basePadL, _basePadT, _basePadR, _basePadB);
        }
    }
}

public class VirtualizingWrapGrid : WrapPanel, IVirtualizingItemsPanel {
    private bool _hasBasePad = false;
    private float _basePadL = 0.0f;
    private float _basePadT = 0.0f;
    private float _basePadR = 0.0f;
    private float _basePadB = 0.0f;
    private float _lastVirtPadTop = -1.0f;
    private float _lastVirtPadBottom = -1.0f;

    public VirtualizingWrapGrid() : base() {}
    public override string GetTypeName() { return "VirtualizingWrapGrid"; }

    public virtual bool IsVirtualizingEnabled() { return true; }
    public virtual int ComputeItemsPerRow(float viewportWidth, float estimatedItemWidth) {
        float iw = ItemWidth;
        float effW = (!ParseUtil.IsNaN(iw) && iw > 0.0f) ? iw : (estimatedItemWidth > 16.0f ? estimatedItemWidth : 120.0f);
        if (viewportWidth <= 0.0f) { return 1; }
        float denom = effW + Spacing;
        int cols = denom > 0.0f ? (int)((viewportWidth + Spacing) / denom) : 1;
        return cols > 1 ? cols : 1;
    }
    public virtual float ComputeRowStride(float estimatedItemHeight) {
        float ih = ItemHeight;
        float effH = (!ParseUtil.IsNaN(ih) && ih > 0.0f) ? ih : (estimatedItemHeight > 8.0f ? estimatedItemHeight : 32.0f);
        return effH + Spacing;
    }
    public virtual void ApplyVirtualWindowPadding(int firstRow, int lastRow, int totalRows, float rowStride) {
        if (!_hasBasePad) {
            Thickness p = Padding;
            if (p != null) {
                _basePadL = p.Left;
                _basePadT = p.Top;
                _basePadR = p.Right;
                _basePadB = p.Bottom;
            }
            _hasBasePad = true;
        }
        int remRows = totalRows > (lastRow + 1) ? (totalRows - 1 - lastRow) : 0;
        float topPad = _basePadT + (float)firstRow * rowStride;
        float botPad = _basePadB + (float)remRows * rowStride;
        if (topPad != _lastVirtPadTop || botPad != _lastVirtPadBottom) {
            _lastVirtPadTop = topPad;
            _lastVirtPadBottom = botPad;
            SetPadding4(_basePadL, topPad, _basePadR, botPad);
        }
    }
    public float VirtualTopPadding() { return _lastVirtPadTop; }
    public float VirtualBottomPadding() { return _lastVirtPadBottom; }
    public virtual void ResetVirtualWindowPadding() {
        if (_hasBasePad && (_lastVirtPadTop != _basePadT || _lastVirtPadBottom != _basePadB)) {
            _lastVirtPadTop = _basePadT;
            _lastVirtPadBottom = _basePadB;
            SetPadding4(_basePadL, _basePadT, _basePadR, _basePadB);
        }
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
    public string Name { get; set; } = "";
    internal Grid _ownerGrid;
    private GridLength _height;
    public GridLength Height {
        get { return _height; }
        set {
            _height = value == null ? GridLength.FromStar(1.0f) : value;
            if (_ownerGrid != null) { _ownerGrid.SyncGridTracks(); }
        }
    }
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
    public string Name { get; set; } = "";
    internal Grid _ownerGrid;
    private GridLength _width;
    public GridLength Width {
        get { return _width; }
        set {
            _width = value == null ? GridLength.FromStar(1.0f) : value;
            if (_ownerGrid != null) { _ownerGrid.SyncGridTracks(); }
        }
    }
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
        def._ownerGrid = _owner;
        _items.Push(def);
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
    internal void AddNoSync(RowDefinition def) {
        if (def == null) { return; }
        def._ownerGrid = _owner;
        _items.Push(def);
    }
    internal void ClearNoSync() {
        _items.Clear();
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
        def._ownerGrid = _owner;
        _items.Push(def);
        if (_owner != null) { _owner.SyncGridTracks(); }
    }
    internal void AddNoSync(ColumnDefinition def) {
        if (def == null) { return; }
        def._ownerGrid = _owner;
        _items.Push(def);
    }
    internal void ClearNoSync() {
        _items.Clear();
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
        SyncGridTracks();
    }

    public override string GetTypeName() { return "Grid"; }

    public RowDefinitionCollection RowDefinitions {
        get {
            if (_rowDefs == null) { _rowDefs = new RowDefinitionCollection(this); }
            return _rowDefs;
        }
    }
    public ColumnDefinitionCollection ColumnDefinitions {
        get {
            if (_colDefs == null) { _colDefs = new ColumnDefinitionCollection(this); }
            return _colDefs;
        }
    }

    public float RowSpacing {
        get { return _rowSpacing; }
        set { float nv = value >= 0.0f ? value : 0.0f; if (_rowSpacing == nv) { return; } _rowSpacing = nv; SyncGridTracks(); }
    }
    public float ColumnSpacing {
        get { return _colSpacing; }
        set { float nv = value >= 0.0f ? value : 0.0f; if (_colSpacing == nv) { return; } _colSpacing = nv; SyncGridTracks(); }
    }

    public void AddRowDefinition(GridUnitType unit, float value = 1.0f, string name = "") {
        var def = new RowDefinition(value, unit);
        if (name != null && name.Length > 0) { def.Name = name; }
        RowDefinitions.Add(def);
    }
    public void AddColumnDefinition(GridUnitType unit, float value = 1.0f, string name = "") {
        var def = new ColumnDefinition(value, unit);
        if (name != null && name.Length > 0) { def.Name = name; }
        ColumnDefinitions.Add(def);
    }
    public void ClearRowDefinitions() { if (_rowDefs != null) { _rowDefs.Clear(); } }
    public void ClearColumnDefinitions() { if (_colDefs != null) { _colDefs.Clear(); } }

    public void SetRowDefinitions(string spec) {
        if (spec == null || spec.Length == 0) {
            if (_rowDefs != null && _rowDefs.Count() > 0) { _rowDefs.Clear(); }
            return;
        }
        RowDefinitionCollection defs = RowDefinitions;
        defs.ClearNoSync();
        string[] parts = spec.Split(",");
        for (int i = 0; i < parts.Length; i += 1) {
            defs.AddNoSync(new RowDefinition(GridLength.Parse(parts[i])));
        }
        SyncGridTracks();
    }

    public void SetColumnDefinitions(string spec) {
        if (spec == null || spec.Length == 0) {
            if (_colDefs != null && _colDefs.Count() > 0) { _colDefs.Clear(); }
            return;
        }
        ColumnDefinitionCollection defs = ColumnDefinitions;
        defs.ClearNoSync();
        string[] parts = spec.Split(",");
        for (int i = 0; i < parts.Length; i += 1) {
            defs.AddNoSync(new ColumnDefinition(GridLength.Parse(parts[i])));
        }
        SyncGridTracks();
    }

    private static GridLength CoerceDefinitionGridLength(DependencyObject val) {
        if (val == null) { return GridLength.Auto(); }
        if (val is GridLength) { return val as GridLength; }
        if (val is BoxedFloat) {
            float fv = (val as BoxedFloat).Value;
            return ParseUtil.IsNaN(fv) ? GridLength.Auto() : GridLength.FromPixel(fv);
        }
        if (val is BoxedInt) {
            return GridLength.FromPixel((float)(val as BoxedInt).Value);
        }
        string s = DependencyObject.UnboxString(val, "Auto").Trim();
        if (s == "NaN" || s == "nan") { return GridLength.Auto(); }
        return GridLength.Parse(s);
    }

    public bool SetNamedDefinitionProperty(string defName, string propName, DependencyObject val) {
        if (defName == null || defName.Length == 0) { return false; }
        if (_rowDefs != null) {
            RowDefinition[] rows = _rowDefs.Items();
            for (int i = 0; i < rows.Length; i += 1) {
                if (rows[i].Name == defName && propName == "Height") {
                    rows[i].Height = CoerceDefinitionGridLength(val);
                    return true;
                }
            }
        }
        if (_colDefs != null) {
            ColumnDefinition[] cols = _colDefs.Items();
            for (int i = 0; i < cols.Length; i += 1) {
                if (cols[i].Name == defName && propName == "Width") {
                    cols[i].Width = CoerceDefinitionGridLength(val);
                    return true;
                }
            }
        }
        return false;
    }

    public DependencyObject GetNamedDefinitionProperty(string defName, string propName) {
        if (defName == null || defName.Length == 0) { return null; }
        if (_rowDefs != null) {
            RowDefinition[] rows = _rowDefs.Items();
            for (int i = 0; i < rows.Length; i += 1) {
                if (rows[i].Name == defName && propName == "Height") { return rows[i].Height; }
            }
        }
        if (_colDefs != null) {
            ColumnDefinition[] cols = _colDefs.Items();
            for (int i = 0; i < cols.Length; i += 1) {
                if (cols[i].Name == defName && propName == "Width") { return cols[i].Width; }
            }
        }
        return null;
    }

    public void SyncGridTracks() {
        SetNativeLayoutMode(LayoutMode.Grid, _colSpacing, _rowSpacing, 0, 0);
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Visual_GridClearDefs(__nativeHandle);
        if (_rowDefs != null) {
            RowDefinition[] rows = _rowDefs.Items();
            for (int i = 0; i < rows.Length; i += 1) {
                GridLength gl = rows[i].Height;
                Visual_GridAddRowDef(__nativeHandle, EnumConvert.ToInt(gl.GridUnitType), gl.Value);
            }
        }
        if (_colDefs != null) {
            ColumnDefinition[] cols = _colDefs.Items();
            for (int i = 0; i < cols.Length; i += 1) {
                GridLength gl = cols[i].Width;
                Visual_GridAddColDef(__nativeHandle, EnumConvert.ToInt(gl.GridUnitType), gl.Value);
            }
        }
    }
}
