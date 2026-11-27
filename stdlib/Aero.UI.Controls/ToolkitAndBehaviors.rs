module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. DateTimeControl, RadialTicks & RadialNeedle (100% RealScript Toolkit)
// ============================================================================

public class DateTimeControl : Control {
    private int _year = 2026;
    private int _month = 11;
    private int _day = 16;
    private int _hour = 12;
    private int _minute = 0;
    private int _second = 0;
    private string _format = "yyyy-MM-dd HH:mm:ss";
    private bool _showDate = true;
    private bool _showTime = true;
    private bool _useSystemTime = false;
    private int _lastSysSec = -1;
    private int _lastSysMsBucket = -1;
    private static DateTimeControl[] _liveClocks;
    private static bool _sysHookInstalled = false;
    private TextBlock _display;

    public event EventHandler DateTimeChanged;

    public DateTimeControl() : base() {
    }

    public override string GetTypeName() { return "DateTimeControl"; }

    protected virtual void OnTimeChanged() {}
    protected virtual void OnLiveSystemTimeSample(int yr, int mo, int dy, int hr, int mn, int sc) {
        SetDateTime(yr, mo, dy, hr, mn, sc);
    }
    protected virtual void OnLiveSystemTimeSampleMs(int yr, int mo, int dy, int hr, int mn, int sc, int ms) {
        OnLiveSystemTimeSample(yr, mo, dy, hr, mn, sc);
    }

    private static void RegisterLiveClock(DateTimeControl clock) {
        if (clock == null) { return; }
        if (_liveClocks == null) { _liveClocks = new DateTimeControl[0]; }
        for (int i = 0; i < _liveClocks.Length; i += 1) {
            if (_liveClocks[i] == clock) { return; }
        }
        _liveClocks.Push(clock);
        if (!_sysHookInstalled) {
            _sysHookInstalled = true;
            BindingEngine.AddPreFlushHook(() => { DateTimeControl.TickLiveClocks(); });
            UIElement.RegisterDisposeHook(() => { DateTimeControl.CleanupDisposed(); });
        }
    }

    public static void CleanupDisposed() {
        if (_liveClocks == null) { return; }
        for (int i = _liveClocks.Length - 1; i >= 0; i -= 1) {
            if (_liveClocks[i] == null || _liveClocks[i]._isDisposedTree) {
                _liveClocks.RemoveAt(i);
            }
        }
    }

    public static void TickLiveClocks() {
        if (_liveClocks == null || _liveClocks.Length == 0) { return; }
        int w = 0;
        for (int i = 0; i < _liveClocks.Length; i += 1) {
            DateTimeControl dt = _liveClocks[i];
            if (dt == null || dt._isDisposedTree) { continue; }
            if (w != i) { _liveClocks[w] = dt; }
            w += 1;
            if (dt._useSystemTime) {
                dt.SyncFromSystemTime();
            }
        }
        while (_liveClocks.Length > w) {
            _liveClocks.RemoveAt(_liveClocks.Length - 1);
        }
    }

    public void SyncFromSystemTime() {
        if (!_useSystemTime || _isDisposedTree) { return; }
        Host_RequestWakeAfterMs(100);
        int yr = Host_GetLocalTimePart(0);
        int mo = Host_GetLocalTimePart(1);
        int dy = Host_GetLocalTimePart(2);
        int hr = Host_GetLocalTimePart(3);
        int mn = Host_GetLocalTimePart(4);
        int sc = Host_GetLocalTimePart(5);
        int ms = Host_GetLocalTimePart(6);
        int msBucket = ms / 16;
        if (yr <= 0) { return; }
        if (msBucket == _lastSysMsBucket && sc == _lastSysSec &&
            dy == _day && mo == _month && yr == _year &&
            hr == _hour && mn == _minute) {
            return;
        }
        _lastSysSec = sc;
        _lastSysMsBucket = msBucket;
        OnLiveSystemTimeSampleMs(yr, mo, dy, hr, mn, sc, ms);
    }

    protected override void OnLoaded() {
        base.OnLoaded();
        if (_useSystemTime) {
            SyncFromSystemTime();
        }
    }

    public int Year { get { return _year; } set { _year = value; SyncDisplay(); OnTimeChanged(); } }
    public int Month { get { return _month; } set { _month = ClampRange(value, 1, 12); SyncDisplay(); OnTimeChanged(); } }
    public int Day { get { return _day; } set { _day = ClampRange(value, 1, DaysInMonth(_year, _month)); SyncDisplay(); OnTimeChanged(); } }
    public int Hour { get { return _hour; } set { _hour = ClampRange(value, 0, 23); SyncDisplay(); OnTimeChanged(); } }
    public int Minute { get { return _minute; } set { _minute = ClampRange(value, 0, 59); SyncDisplay(); OnTimeChanged(); } }
    public int Second { get { return _second; } set { _second = ClampRange(value, 0, 59); SyncDisplay(); OnTimeChanged(); } }
    public bool ShowDate { get { return _showDate; } set { _showDate = value; SyncDisplay(); } }
    public bool ShowTime { get { return _showTime; } set { _showTime = value; SyncDisplay(); } }
    public bool UseSystemTime {
        get { return _useSystemTime; }
        set {
            _useSystemTime = value;
            if (value) {
                RegisterLiveClock(this);
                SyncFromSystemTime();
            }
        }
    }
    public string Format {
        get { return _format; }
        set { _format = value == null ? "yyyy-MM-dd HH:mm:ss" : value; SyncDisplay(); }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Year") { Year = DependencyObject.UnboxInt(value, _year); return true; }
        if (propName == "Month") { Month = DependencyObject.UnboxInt(value, _month); return true; }
        if (propName == "Day") { Day = DependencyObject.UnboxInt(value, _day); return true; }
        if (propName == "Hour") { Hour = DependencyObject.UnboxInt(value, _hour); return true; }
        if (propName == "Minute") { Minute = DependencyObject.UnboxInt(value, _minute); return true; }
        if (propName == "Second") { Second = DependencyObject.UnboxInt(value, _second); return true; }
        if (propName == "Format") { Format = DependencyObject.UnboxString(value, _format); return true; }
        if (propName == "ShowDate") { ShowDate = DependencyObject.UnboxBool(value, _showDate); return true; }
        if (propName == "ShowTime") { ShowTime = DependencyObject.UnboxBool(value, _showTime); return true; }
        if (propName == "UseSystemTime") { UseSystemTime = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Year") { return DependencyObject.BoxInt(_year); }
        if (propName == "Month") { return DependencyObject.BoxInt(_month); }
        if (propName == "Day") { return DependencyObject.BoxInt(_day); }
        if (propName == "Hour") { return DependencyObject.BoxInt(_hour); }
        if (propName == "Minute") { return DependencyObject.BoxInt(_minute); }
        if (propName == "Second") { return DependencyObject.BoxInt(_second); }
        if (propName == "Format") { return DependencyObject.BoxString(_format); }
        if (propName == "ShowDate") { return DependencyObject.BoxBool(_showDate); }
        if (propName == "ShowTime") { return DependencyObject.BoxBool(_showTime); }
        if (propName == "UseSystemTime") { return DependencyObject.BoxBool(UseSystemTime); }
        return base.GetPropertyValue(propName);
    }

    public void SetDateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
        _year = year;
        _month = ClampRange(month, 1, 12);
        _day = ClampRange(day, 1, DaysInMonth(_year, _month));
        _hour = ClampRange(hour, 0, 23);
        _minute = ClampRange(minute, 0, 59);
        _second = ClampRange(second, 0, 59);
        SyncDisplay();
        OnTimeChanged();
        DateTimeChanged(this, EventArgs.Empty());
    }

    public void AdvanceSeconds(int deltaSeconds) {
        int total = _hour * 3600 + _minute * 60 + _second + deltaSeconds;
        while (total >= 86400) {
            total -= 86400;
            AdvanceOneDay();
        }
        while (total < 0) {
            total += 86400;
            StepBackOneDay();
        }
        _hour = total / 3600;
        _minute = (total % 3600) / 60;
        _second = total % 60;
        SyncDisplay();
        OnTimeChanged();
        DateTimeChanged(this, EventArgs.Empty());
    }

    private void AdvanceOneDay() {
        _day += 1;
        if (_day > DaysInMonth(_year, _month)) {
            _day = 1;
            _month += 1;
            if (_month > 12) { _month = 1; _year += 1; }
        }
    }

    private void StepBackOneDay() {
        _day -= 1;
        if (_day < 1) {
            _month -= 1;
            if (_month < 1) { _month = 12; _year -= 1; }
            _day = DaysInMonth(_year, _month);
        }
    }

    public static bool IsLeapYear(int y) {
        return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
    }

    public static int DaysInMonth(int y, int m) {
        if (m == 2) { return IsLeapYear(y) ? 29 : 28; }
        if (m == 4 || m == 6 || m == 9 || m == 11) { return 30; }
        return 31;
    }

    // Returns 0=Sunday .. 6=Saturday
    public int DayOfWeek() {
        int y = _year;
        int m = _month;
        if (m < 3) { m += 12; y -= 1; }
        int k = y % 100;
        int j = y / 100;
        int h = (_day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
        return (h + 6) % 7;
    }

    private static int ClampRange(int v, int lo, int hi) {
        if (v < lo) { return lo; }
        if (v > hi) { return hi; }
        return v;
    }

    private static string Pad2(int v) {
        return v < 10 ? ("0" + v) : ("" + v);
    }

    private static string Pad4(int v) {
        if (v < 10) { return "000" + v; }
        if (v < 100) { return "00" + v; }
        if (v < 1000) { return "0" + v; }
        return "" + v;
    }

    public string FormattedText() {
        if (_format.Length > 0 && _format != "yyyy-MM-dd HH:mm:ss") {
            return _format
                .Replace("yyyy", Pad4(_year))
                .Replace("MM", Pad2(_month))
                .Replace("dd", Pad2(_day))
                .Replace("HH", Pad2(_hour))
                .Replace("mm", Pad2(_minute))
                .Replace("ss", Pad2(_second));
        }
        string d = _showDate ? (Pad4(_year) + "-" + Pad2(_month) + "-" + Pad2(_day)) : "";
        string t = _showTime ? (Pad2(_hour) + ":" + Pad2(_minute) + ":" + Pad2(_second)) : "";
        if (d.Length > 0 && t.Length > 0) { return d + " " + t; }
        return d + t;
    }

    private void SyncDisplay() {
        BumpPropEpoch();
        BindingEngine.MarkFlushDirty();
        if (_display != null) {
            _display.Text = FormattedText();
            _display.Foreground = Foreground;
            _display.FontSize = FontSize;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _display = new TextBlock(FormattedText());
        _display.Foreground = Foreground;
        _display.FontSize = FontSize;
        _display.FontFamily = FontFamily;
        _display.VerticalAlignment = VerticalAlignment.Center;
        return _display;
    }
}

public class RadialTicks : CustomDrawElement {
    private int _tickCount = 60;
    private int _majorStep = 5;
    private float _startAngleDeg = -225.0f;
    private float _sweepAngleDeg = 270.0f;
    private float _minorLength = 6.0f;
    private float _majorLength = 13.0f;
    private float _strokeThickness = 1.5f;
    private float _majorThickness = 2.5f;
    private float _inset = 4.0f;
    private Brush _strokeBrush;
    private Brush _majorStrokeBrush;
    private float _minimum = 0.0f;
    private float _maximum = 60.0f;
    private float _spacing = 1.0f;
    private float _minAngle = 0.0f;
    private float _maxAngle = 360.0f;
    private bool _useRangeMode = false;

    public RadialTicks() : base() {
        _strokeBrush = new SolidColorBrush("#FF64748B");
        _majorStrokeBrush = new SolidColorBrush("#FFF1F5F9");
    }

    public override string GetTypeName() { return "RadialTicks"; }

    public int TickCount { get { return _tickCount; } set { _tickCount = value > 1 ? value : 2; InvalidateDraw(); } }
    public int MajorStep { get { return _majorStep; } set { _majorStep = value > 0 ? value : 1; InvalidateDraw(); } }
    public float StartAngleDeg { get { return _startAngleDeg; } set { _startAngleDeg = value; InvalidateDraw(); } }
    public float SweepAngleDeg { get { return _sweepAngleDeg; } set { _sweepAngleDeg = value; InvalidateDraw(); } }
    public float MinorLength { get { return _minorLength; } set { _minorLength = value; InvalidateDraw(); } }
    public float MajorLength { get { return _majorLength; } set { _majorLength = value; InvalidateDraw(); } }
    public Brush Stroke { get { return _strokeBrush; } set { _strokeBrush = value; InvalidateDraw(); } }
    public Brush MajorStroke { get { return _majorStrokeBrush; } set { _majorStrokeBrush = value; InvalidateDraw(); } }

    public float Minimum { get { return _minimum; } set { _minimum = value; RecomputeFromRange(); } }
    public float Maximum { get { return _maximum; } set { _maximum = value; RecomputeFromRange(); } }
    public float Spacing { get { return _spacing; } set { _spacing = value > 0.0f ? value : 1.0f; RecomputeFromRange(); } }
    public float MinAngle { get { return _minAngle; } set { _minAngle = value; RecomputeFromAngles(); } }
    public float MaxAngle { get { return _maxAngle; } set { _maxAngle = value; RecomputeFromAngles(); } }
    public float TickLength {
        get { return _minorLength; }
        set { _minorLength = value; _majorLength = value; InvalidateDraw(); }
    }
    public float TickWidth {
        get { return _strokeThickness; }
        set { _strokeThickness = value; _majorThickness = value; InvalidateDraw(); }
    }
    public Brush Brush {
        get { return _strokeBrush; }
        set { _strokeBrush = value; _majorStrokeBrush = value; InvalidateDraw(); }
    }

    private void RecomputeFromRange() {
        _useRangeMode = true;
        float span = Math.Maxf(0.0f, _maximum - _minimum);
        int cnt = (int)(span / Math.Maxf(0.001f, _spacing) + 0.0001f);
        if (_sweepAngleDeg < 359.5f) { cnt = cnt + 1; }
        _tickCount = cnt > 1 ? cnt : 2;
        _majorStep = 1;
        InvalidateDraw();
    }

    private void RecomputeFromAngles() {
        _useRangeMode = true;
        _startAngleDeg = _minAngle - 90.0f;
        _sweepAngleDeg = _maxAngle - _minAngle;
        RecomputeFromRange();
    }

    public override void OnRender(DrawingContext dc) {
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 180.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 180.0f);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float inset = Math.Maxf(_inset, Padding.Left);
        float outerR = Math.Maxf(8.0f, Math.Minf(cx, cy) - inset);
        float degToRad = 0.0174532925f;

        uint minorCol = _strokeBrush != null ? _strokeBrush.ResolveRgba() : ParseUtil.PackArgb(255u, 100u, 116u, 139u);
        uint majorCol = _majorStrokeBrush != null ? _majorStrokeBrush.ResolveRgba() : ParseUtil.PackArgb(255u, 241u, 245u, 249u);

        int denom = (_useRangeMode && Math.Absf(_sweepAngleDeg) >= 359.5f)
            ? (_tickCount > 0 ? _tickCount : 1)
            : (_tickCount > 1 ? (_tickCount - 1) : 1);
        for (int i = 0; i < _tickCount; i += 1) {
            bool isMajor = (i % _majorStep) == 0;
            float t = (float)i / (float)denom;
            float rad = (_startAngleDeg + t * _sweepAngleDeg) * degToRad;
            float dx = Math.Cosf(rad);
            float dy = Math.Sinf(rad);
            float innerR = Math.Maxf(2.0f, outerR - (isMajor ? _majorLength : _minorLength));
            dc.DrawLine(
                cx + dx * innerR, cy + dy * innerR,
                cx + dx * outerR, cy + dy * outerR,
                isMajor ? majorCol : minorCol,
                isMajor ? _majorThickness : _strokeThickness);
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 60.0f); return true; }
        if (propName == "Spacing") { Spacing = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "MinAngle") { MinAngle = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MaxAngle") { MaxAngle = DependencyObject.UnboxFloat(value, 360.0f); return true; }
        if (propName == "TickLength") { TickLength = DependencyObject.UnboxFloat(value, 10.0f); return true; }
        if (propName == "TickWidth") { TickWidth = DependencyObject.UnboxFloat(value, 2.0f); return true; }
        if (propName == "Brush" || propName == "Stroke") { Brush = UIElement.CoerceBrush(value); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

public class RadialNeedle : CustomDrawElement {
    private float _minimum = 0.0f;
    private float _maximum = 100.0f;
    private float _value = 0.0f;
    private float _startAngleDeg = -225.0f;
    private float _sweepAngleDeg = 270.0f;
    private float _minAngle = 0.0f;
    private float _maxAngle = 360.0f;
    private bool _useClockOrigin = false;
    private float _needleLengthRatio = 0.78f;
    private float _needleWidth = 3.0f;
    private float _tailLength = 14.0f;
    private float _pivotRadius = 6.0f;
    private Brush _fill;
    private bool _childHostConfigured = false;

    public RadialNeedle() : base() {
        _fill = new SolidColorBrush("#FFEF4444");
    }

    public override string GetTypeName() { return "RadialNeedle"; }

    public float Minimum { get { return _minimum; } set { _minimum = value; SyncRotationOrDraw(); } }
    public float Maximum { get { return _maximum; } set { _maximum = value; SyncRotationOrDraw(); } }
    public float Value { get { return _value; } set { _value = value; SyncRotationOrDraw(); } }
    public float StartAngleDeg { get { return _startAngleDeg; } set { _startAngleDeg = value; SyncRotationOrDraw(); } }
    public float SweepAngleDeg { get { return _sweepAngleDeg; } set { _sweepAngleDeg = value; SyncRotationOrDraw(); } }
    public float MinAngle {
        get { return _minAngle; }
        set { _minAngle = value; _useClockOrigin = true; _startAngleDeg = _minAngle; _sweepAngleDeg = _maxAngle - _minAngle; SyncRotationOrDraw(); }
    }
    public float MaxAngle {
        get { return _maxAngle; }
        set { _maxAngle = value; _useClockOrigin = true; _startAngleDeg = _minAngle; _sweepAngleDeg = _maxAngle - _minAngle; SyncRotationOrDraw(); }
    }
    public float NeedleLengthRatio { get { return _needleLengthRatio; } set { _needleLengthRatio = value; InvalidateDraw(); } }
    public float NeedleWidth { get { return _needleWidth; } set { _needleWidth = value; InvalidateDraw(); } }
    public float PivotRadius { get { return _pivotRadius; } set { _pivotRadius = value; InvalidateDraw(); } }
    public Brush Fill { get { return _fill; } set { _fill = value; InvalidateDraw(); } }

    public float CurrentAngleDeg() {
        float span = _maximum - _minimum;
        float frac = span > 0.0001f ? Math.Clampf((_value - _minimum) / span, 0.0f, 1.0f) : 0.0f;
        return _startAngleDeg + frac * _sweepAngleDeg;
    }

    private void SyncRotationOrDraw() {
        if (ChildCount() > 0) {
            RenderTransformOrigin = new Point(0.5f, 0.5f);
            RenderTransform = new RotateTransform(CurrentAngleDeg());
        }
        InvalidateDraw();
    }

    private void ConfigureNeedleChild(UIElement child) {
        if (child == null) { return; }
        if (!_childHostConfigured && ParseUtil.IsValidHandle(__nativeHandle)) {
            _childHostConfigured = true;
            UpdateCallbackMask(VisualCallbacks.StandardInput());
            Visual_SetLayoutMode(__nativeHandle, EnumConvert.ToInt(LayoutMode.Grid), 0.0f, 0.0f, 0, 0);
            Visual_GridClearDefs(__nativeHandle);
            Visual_GridAddRowDef(__nativeHandle, EnumConvert.ToInt(GridUnitType.Star), 1.0f);
            Visual_GridAddRowDef(__nativeHandle, EnumConvert.ToInt(GridUnitType.Star), 1.0f);
        }
        UIElement.SetGridRow(child, 0);
        child.VerticalAlignment = VerticalAlignment.Bottom;
    }

    public override void AddChild(UIElement child) {
        base.AddChild(child);
        ConfigureNeedleChild(child);
        SyncRotationOrDraw();
    }

    public override void InsertChild(int index, UIElement child) {
        base.InsertChild(index, child);
        ConfigureNeedleChild(child);
        SyncRotationOrDraw();
    }

    public override void OnRender(DrawingContext dc) {
        if (ChildCount() > 0) { return; }
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 180.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 180.0f);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float radLen = Math.Maxf(8.0f, Math.Minf(cx, cy) * _needleLengthRatio);
        float ang = _useClockOrigin ? (CurrentAngleDeg() - 90.0f) : CurrentAngleDeg();
        float rad = ang * 0.0174532925f;
        float dx = Math.Cosf(rad);
        float dy = Math.Sinf(rad);
        uint col = _fill != null ? _fill.ResolveRgba() : ParseUtil.PackArgb(255u, 239u, 68u, 68u);

        dc.DrawLine(cx - dx * _tailLength, cy - dy * _tailLength, cx + dx * radLen, cy + dy * radLen, col, _needleWidth);
        dc.FillEllipse(cx, cy, _pivotRadius, _pivotRadius, col, -1);
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Value") { Value = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 100.0f); return true; }
        if (propName == "MinAngle") { MinAngle = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "MaxAngle") { MaxAngle = DependencyObject.UnboxFloat(value, 360.0f); return true; }
        if (propName == "Fill") { Fill = UIElement.CoerceBrush(value); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

// ============================================================================
// 2. Interactivity, Triggers & Actions (Interaction, ChangeProperty, InvokeCommand, Storyboard)
// ============================================================================

public class TriggerAction : DependencyObject {
    public string ActionName { get; set; } = "";
    public string TargetName { get; set; } = "";
    public UIElement TargetObject { get; set; }
    public string TargetObjectPath { get; set; } = "";
    public string TargetObjectRelativeMode { get; set; } = "";
    public string TargetObjectAncestorType { get; set; } = "";
    public string TargetObjectElementName { get; set; } = "";
    public bool IsEnabled { get; set; } = true;

    public void SetTargetObjectBinding(string path, string relMode, string ancType, string elName) {
        TargetObjectPath = path == null ? "" : path;
        TargetObjectRelativeMode = relMode == null ? "" : relMode;
        TargetObjectAncestorType = ancType == null ? "" : ancType;
        TargetObjectElementName = elName == null ? "" : elName;
    }

    public UIElement ResolveTarget(UIElement associatedObject) {
        if (TargetObject != null) { return TargetObject; }
        if (TargetObjectPath.Length > 0 && associatedObject != null) {
            UIElement src = associatedObject;
            if (TargetObjectElementName.Length > 0) {
                src = BindingEntry.FindElementScoped(associatedObject, TargetObjectElementName);
                if (src == null && associatedObject.RootElement() != null) {
                    src = associatedObject.RootElement().FindName(TargetObjectElementName);
                }
            } else if (TargetObjectRelativeMode == "TemplatedParent") {
                src = associatedObject.TemplatedParent;
            } else if (TargetObjectRelativeMode == "FindAncestor" || TargetObjectRelativeMode == "AncestorType" ||
                       TargetObjectAncestorType.Length > 0) {
                UIElement anc = associatedObject;
                while (anc != null && TargetObjectAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, TargetObjectAncestorType)) {
                    anc = BindingEntry.StepAncestor(anc);
                }
                src = anc;
            }
            if (src != null) {
                if (TargetObjectPath == ".") { return src; }
                DependencyObject box = src.GetPropertyValue(TargetObjectPath);
                if (box == null) { box = BindingEngine.ReadPathBox(src, TargetObjectPath); }
                object unboxed = DependencyObject.Unbox(box);
                if (unboxed is UIElement) { return unboxed as UIElement; }
            }
        }
        if (associatedObject != null && TargetName.Length > 0) {
            UIElement found = BindingEntry.FindElementScoped(associatedObject, TargetName);
            if (found == null) { found = associatedObject.FindName(TargetName); }
            if (found == null && associatedObject.RootElement() != null) {
                found = associatedObject.RootElement().FindName(TargetName);
            }
            if (found != null) { return found; }
        }
        return associatedObject;
    }

    public virtual bool SetActionProperty(string propName, DependencyObject value) {
        if (propName == "IsEnabled") { IsEnabled = DependencyObject.UnboxBool(value, true); return true; }
        if (propName == "TargetName") { TargetName = DependencyObject.UnboxString(value, ""); return true; }
        return false;
    }

    public virtual void Invoke(UIElement associatedObject, DependencyObject parameter) {}
}

public class InvokeCommandAction : TriggerAction {
    public ICommand Command { get; set; }
    public string CommandPath { get; set; } = "";
    public string CommandParameterPath { get; set; } = "";
    public string CommandParameterRelativeMode { get; set; } = "";
    public string CommandParameterAncestorType { get; set; } = "";
    public string CommandParameterElementName { get; set; } = "";
    private DependencyObject _paramBox;
    public bool PassEventArgsToCommand { get; set; } = false;

    public InvokeCommandAction() {}
    public InvokeCommandAction(ICommand cmd, object param = null) {
        Command = cmd;
        _paramBox = DependencyObject.BoxObject(param);
    }

    public object CommandParameter {
        get { return DependencyObject.Unbox(_paramBox); }
        set { _paramBox = DependencyObject.BoxObject(value); }
    }
    public void SetCommandParameter(string v) { _paramBox = DependencyObject.BoxString(v); }
    public void SetCommandParameterBox(DependencyObject box) { _paramBox = box; }

    private DependencyObject ResolveDynamicParameter(UIElement associatedObject) {
        if (associatedObject == null) { return null; }
        bool selfPath = CommandParameterPath.Length == 0 || CommandParameterPath == ".";
        if (CommandParameterRelativeMode == "Self") {
            return selfPath ? associatedObject : BindingEngine.ReadPathBox(associatedObject, CommandParameterPath);
        }
        if (CommandParameterRelativeMode == "TemplatedParent") {
            UIElement tp = associatedObject.TemplatedParent;
            if (tp == null) { return null; }
            return selfPath ? tp : BindingEngine.ReadPathBox(tp, CommandParameterPath);
        }
        if (CommandParameterAncestorType.Length > 0 ||
            CommandParameterRelativeMode == "FindAncestor" ||
            CommandParameterRelativeMode == "AncestorType") {
            UIElement cur = associatedObject;
            while (cur != null && CommandParameterAncestorType.Length > 0 &&
                   !BindingEntry.MatchesAncestorType(cur, CommandParameterAncestorType)) {
                cur = BindingEntry.StepAncestor(cur);
            }
            if (cur == null) { return null; }
            return selfPath ? cur : BindingEngine.ReadPathBox(cur, CommandParameterPath);
        }
        if (CommandParameterElementName.Length > 0) {
            UIElement named = BindingEntry.FindElementScoped(associatedObject, CommandParameterElementName);
            if (named == null && associatedObject.RootElement() != null) {
                named = associatedObject.RootElement().FindName(CommandParameterElementName);
            }
            if (named == null) { return null; }
            return selfPath ? named : BindingEngine.ReadPathBox(named, CommandParameterPath);
        }
        if (CommandParameterPath.Length > 0 && associatedObject.DataContext != null) {
            return selfPath
                ? DependencyObject.BoxObject(associatedObject.DataContext)
                : BindingEngine.ReadPathBox(associatedObject.DataContext, CommandParameterPath);
        }
        return null;
    }

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        ICommand cmd = Command;
        if (cmd == null && CommandPath.Length > 0 && associatedObject != null && associatedObject.DataContext != null) {
            DependencyObject cb = BindingEngine.ReadPathBox(associatedObject.DataContext, CommandPath);
            cmd = DependencyObject.Unbox(cb) as ICommand;
        }
        if (cmd == null) { return; }
        DependencyObject resolved = _paramBox;
        if (resolved == null &&
            (CommandParameterPath.Length > 0 || CommandParameterElementName.Length > 0 ||
             CommandParameterRelativeMode.Length > 0 || CommandParameterAncestorType.Length > 0)) {
            resolved = ResolveDynamicParameter(associatedObject);
        }
        DependencyObject arg = (resolved == null && PassEventArgsToCommand) ? parameter : resolved;
        CommandTriggerHelper.ExecuteBox(cmd, arg);
    }
}

public class ChangePropertyAction : TriggerAction {
    public string PropertyName { get; set; } = "";
    public Binding ValueBinding { get; set; }
    public MultiBinding ValueMultiBinding { get; set; }
    private DependencyObject _valueBox;

    public ChangePropertyAction() {}
    public ChangePropertyAction(string propertyName, object value, string targetName = "") {
        PropertyName = propertyName == null ? "" : propertyName;
        _valueBox = DependencyObject.BoxObject(value);
        TargetName = targetName == null ? "" : targetName;
    }
    public ChangePropertyAction(string propertyName, string value, string targetName = "") {
        PropertyName = propertyName == null ? "" : propertyName;
        _valueBox = DependencyObject.BoxString(value);
        TargetName = targetName == null ? "" : targetName;
    }
    public ChangePropertyAction(string propertyName, float value, string targetName = "") {
        PropertyName = propertyName == null ? "" : propertyName;
        _valueBox = DependencyObject.BoxFloat(value);
        TargetName = targetName == null ? "" : targetName;
    }
    public ChangePropertyAction(string propertyName, bool value, string targetName = "") {
        PropertyName = propertyName == null ? "" : propertyName;
        _valueBox = DependencyObject.BoxBool(value);
        TargetName = targetName == null ? "" : targetName;
    }

    public void SetValueBox(DependencyObject box) { _valueBox = box; }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }

    private DependencyObject ResolveActionValueBox(UIElement associatedObject) {
        if (ValueMultiBinding != null) {
            Binding[] subs = ValueMultiBinding.Bindings;
            int n = subs != null ? subs.Length : 0;
            var vals = new DependencyObject[n];
            for (int i = 0; i < n; i += 1) {
                vals[i] = BehaviorDataTrigger.EvaluateDynamicBinding(associatedObject, subs[i]);
            }
            if (ValueMultiBinding.Converter != null) {
                DependencyObject pBox = ValueMultiBinding.ConverterParameter.Length > 0
                    ? DependencyObject.BoxString(ValueMultiBinding.ConverterParameter) : null;
                return DependencyObject.BoxObject(ValueMultiBinding.Converter.Convert(vals, PropertyName, pBox, ""));
            }
            return n > 0 ? vals[0] : null;
        }
        if (ValueBinding != null) {
            return BehaviorDataTrigger.EvaluateDynamicBinding(associatedObject, ValueBinding);
        }
        return _valueBox;
    }

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled || PropertyName.Length == 0) { return; }
        DependencyObject valBox = ResolveActionValueBox(associatedObject);
        if (TargetObject == null && TargetName.Length > 0) {
            UIElement namedEl = associatedObject != null ? BindingEntry.FindElementScoped(associatedObject, TargetName) : null;
            if (namedEl == null && associatedObject != null) {
                namedEl = associatedObject.FindName(TargetName);
            }
            if (namedEl == null && associatedObject != null && associatedObject.RootElement() != null) {
                namedEl = associatedObject.RootElement().FindName(TargetName);
            }
            if (namedEl != null) {
                namedEl.SetPropertyValue(PropertyName, valBox);
                return;
            }
            TriggerAction namedAct = Interaction.FindNamedAction(associatedObject, TargetName);
            if (namedAct != null) {
                namedAct.SetActionProperty(PropertyName, valBox);
                return;
            }
            return;
        }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest != null) {
            dest.SetPropertyValue(PropertyName, valBox);
        }
    }
}

public class ControlStoryboardAction : TriggerAction {
    public Storyboard Storyboard { get; set; }
    public string StoryboardKey { get; set; } = "";
    public string ControlStoryboardOption { get; set; } = "Play";

    public ControlStoryboardAction() {}
    public ControlStoryboardAction(Storyboard sb, string option = "Play") {
        Storyboard = sb;
        ControlStoryboardOption = option == null ? "Play" : option;
    }

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        Storyboard sb = Storyboard;
        if (sb == null && StoryboardKey.Length > 0 && dest != null) {
            DependencyObject res = dest.FindResourceBox(StoryboardKey);
            if (res is Storyboard) { sb = res as Storyboard; }
        }
        if (sb == null) { return; }
        if (ControlStoryboardOption == "Stop") { sb.Stop(dest); }
        else { sb.Begin(dest); }
    }
}

public class SetFocusAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest != null) { dest.Focus(); }
    }
}

public class RemoveElementAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest == null) { return; }
        if (associatedObject != null) {
            UIElement menuAnc = associatedObject;
            while (menuAnc != null) {
                if (menuAnc.GetTypeName() == "ContextMenu") {
                    menuAnc.SetPropertyValue("IsOpen", DependencyObject.BoxBool(false));
                    break;
                }
                menuAnc = BindingEntry.StepAncestor(menuAnc);
            }
        }
        UIElement p = dest.Parent;
        while (p != null) {
            if (p is ItemsControl) {
                ItemsControl ic = p as ItemsControl;
                int idx = ic.IndexOfItemContainer(dest);
                if (idx >= 0 && ic.TryRemoveItemAt(idx)) {
                    return;
                }
                break;
            }
            p = p.Parent;
        }
        if (dest.Parent != null) {
            dest.Parent.RemoveChild(dest);
        }
    }
}

public class SelectAllAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is TextBoxBase) { (dest as TextBoxBase).SelectAll(); }
    }
}

public class SelectAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is ListBoxItem) { (dest as ListBoxItem).IsSelected = true; }
        else if (dest is TextBoxBase) { (dest as TextBoxBase).SelectAll(); }
        else if (dest != null) { dest.Focus(); }
    }
}

public class PlaySoundAction : TriggerAction {
    public string Source { get; set; } = "";
    public float Volume { get; set; } = 1.0f;

    public override bool SetActionProperty(string propName, DependencyObject value) {
        if (propName == "Source") { Source = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Volume") { Volume = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        return base.SetActionProperty(propName, value);
    }

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled || Source == null || Source.Length == 0) { return; }
        Host_PlaySound(Source, Math.Clampf(Volume, 0.0f, 1.0f));
    }
}

public class NavDestination : DependencyObject {
    public string Id { get; set; } = "";
    public DependencyObject ContentBox { get; set; }
    public string BindingPath { get; set; } = "";
    public DataTemplate ContentTemplate { get; set; }
}

public static class TransitionContentPresenter {
    private static UIElement[] _enterEls;
    private static string[] _enterKeys;
    private static UIElement[] _exitEls;
    private static string[] _exitKeys;
    private static bool _disposeHookInstalled = false;

    private static void EnsureDisposeHook() {
        if (_disposeHookInstalled) { return; }
        _disposeHookInstalled = true;
        UIElement.RegisterDisposeHook(() => { TransitionContentPresenter.CleanupDisposed(); });
    }

    public static void SetEnterKey(UIElement el, string key) {
        if (el == null || key == null || key.Length == 0) { return; }
        if (_enterEls == null) {
            _enterEls = new UIElement[0];
            _enterKeys = new string[0];
            EnsureDisposeHook();
        }
        for (int i = _enterEls.Length - 1; i >= 0; i -= 1) {
            if (_enterEls[i] == null || _enterEls[i]._isDisposedTree) {
                _enterEls.RemoveAt(i);
                _enterKeys.RemoveAt(i);
            } else if (_enterEls[i] == el) {
                _enterKeys[i] = key;
                return;
            }
        }
        _enterEls.Push(el);
        _enterKeys.Push(key);
    }

    public static void SetExitKey(UIElement el, string key) {
        if (el == null || key == null || key.Length == 0) { return; }
        if (_exitEls == null) {
            _exitEls = new UIElement[0];
            _exitKeys = new string[0];
            EnsureDisposeHook();
        }
        for (int i = _exitEls.Length - 1; i >= 0; i -= 1) {
            if (_exitEls[i] == null || _exitEls[i]._isDisposedTree) {
                _exitEls.RemoveAt(i);
                _exitKeys.RemoveAt(i);
            } else if (_exitEls[i] == el) {
                _exitKeys[i] = key;
                return;
            }
        }
        _exitEls.Push(el);
        _exitKeys.Push(key);
    }

    private static string FindRegisteredEnterKey(UIElement el) {
        if (el == null || _enterEls == null) { return ""; }
        for (int i = _enterEls.Length - 1; i >= 0; i -= 1) {
            if (_enterEls[i] == null || _enterEls[i]._isDisposedTree) {
                _enterEls.RemoveAt(i);
                _enterKeys.RemoveAt(i);
            } else if (_enterEls[i] == el) {
                return _enterKeys[i];
            }
        }
        return "";
    }

    private static string FindRegisteredExitKey(UIElement el) {
        if (el == null || _exitEls == null) { return ""; }
        for (int i = _exitEls.Length - 1; i >= 0; i -= 1) {
            if (_exitEls[i] == null || _exitEls[i]._isDisposedTree) {
                _exitEls.RemoveAt(i);
                _exitKeys.RemoveAt(i);
            } else if (_exitEls[i] == el) {
                return _exitKeys[i];
            }
        }
        return "";
    }

    internal static void CleanupDisposed() {
        if (_enterEls != null) {
            for (int i = _enterEls.Length - 1; i >= 0; i -= 1) {
                if (_enterEls[i] == null || _enterEls[i]._isDisposedTree) {
                    _enterEls.RemoveAt(i);
                    _enterKeys.RemoveAt(i);
                }
            }
        }
        if (_exitEls != null) {
            for (int i = _exitEls.Length - 1; i >= 0; i -= 1) {
                if (_exitEls[i] == null || _exitEls[i]._isDisposedTree) {
                    _exitEls.RemoveAt(i);
                    _exitKeys.RemoveAt(i);
                }
            }
        }
    }

    private static UIElement FindExitTarget(UIElement root) {
        if (root == null || _exitEls == null || _exitEls.Length == 0) { return null; }
        var q = new UIElement[0];
        q.Push(root);
        int idx = 0;
        while (idx < q.Length && idx < 32) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null || cur._isDisposedTree) { continue; }
            if (FindRegisteredExitKey(cur).Length > 0) { return cur; }
            for (int i = 0; i < cur.ChildCount(); i += 1) {
                q.Push(cur.ChildAt(i));
            }
        }
        return null;
    }

    public static bool HasExitTransition(UIElement root) {
        return FindExitTarget(root) != null;
    }

    public static bool PlayExit(UIElement root, Action onFinished = null) {
        UIElement target = FindExitTarget(root);
        if (target == null) {
            if (onFinished != null) { onFinished(); }
            return false;
        }
        string key = FindRegisteredExitKey(target);
        if (key.Length == 0) {
            if (onFinished != null) { onFinished(); }
            return false;
        }
        DependencyObject res = target.FindResourceBox(key);
        if (res is Storyboard) {
            (res as Storyboard).BeginWithCompletion(target, onFinished);
            return true;
        }
        if (onFinished != null) { onFinished(); }
        return false;
    }

    public static void PlayEnter(UIElement root) {
        if (root == null || _enterEls == null) { return; }
        var q = new UIElement[0];
        q.Push(root);
        int idx = 0;
        while (idx < q.Length && idx < 32) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            string key = FindRegisteredEnterKey(cur);
            if (key.Length > 0) {
                cur.BeginStoryboard(key);
                return;
            }
            for (int i = 0; i < cur.ChildCount(); i += 1) {
                q.Push(cur.ChildAt(i));
            }
        }
    }
}

public static class Navigation {
    private static ContentControl[] _hosts;
    private static string[] _curDestIds;
    private static int[] _hostNavGen;
    private static NavDestination[] _pendingDests;
    private static int[] _pendingGens;
    private static int[] _destHostIdx;
    private static NavDestination[] _dests;
    private static int[] _histHostIdx;
    private static string[] _histDestIds;

    private static void EnsureTables() {
        if (_hosts == null) {
            _hosts = new ContentControl[0];
            _curDestIds = new string[0];
            _hostNavGen = new int[0];
            _pendingDests = new NavDestination[0];
            _pendingGens = new int[0];
            _destHostIdx = new int[0];
            _dests = new NavDestination[0];
            _histHostIdx = new int[0];
            _histDestIds = new string[0];
            UIElement.RegisterDisposeHook(() => { Navigation.CleanupDisposed(); });
        }
    }

    private static int EnsureHostIndex(ContentControl host) {
        EnsureTables();
        for (int i = 0; i < _hosts.Length; i += 1) {
            if (_hosts[i] == host) { return i; }
        }
        _hosts.Push(host);
        _curDestIds.Push("");
        _hostNavGen.Push(0);
        _pendingDests.Push(null);
        _pendingGens.Push(0);
        return _hosts.Length - 1;
    }

    internal static void CleanupDisposed() {
        if (_hosts == null) { return; }
        for (int i = 0; i < _hosts.Length; i += 1) {
            if (_hosts[i] != null && _hosts[i]._isDisposedTree) {
                _hosts[i] = null;
                _hostNavGen[i] = _hostNavGen[i] + 1;
                _pendingDests[i] = null;
            }
        }
    }

    public static void RegisterDestination(UIElement hostEl, NavDestination dest) {
        ContentControl host = hostEl as ContentControl;
        if (host == null || dest == null) { return; }
        int hi = EnsureHostIndex(host);
        for (int i = 0; i < _dests.Length; i += 1) {
            if (_destHostIdx[i] == hi && _dests[i].Id == dest.Id) {
                _dests[i] = dest;
                return;
            }
        }
        _destHostIdx.Push(hi);
        _dests.Push(dest);
        if (_curDestIds[hi].Length == 0 && dest.Id.Length > 0) {
            _curDestIds[hi] = dest.Id;
        }
    }

    private static int FindHostIndexForCaller(UIElement caller) {
        if (_hosts == null || _hosts.Length == 0) { return -1; }
        UIElement cur = caller;
        while (cur != null) {
            if (cur is ContentControl) {
                ContentControl cc = cur as ContentControl;
                for (int i = 0; i < _hosts.Length; i += 1) {
                    if (_hosts[i] == cc) { return i; }
                }
            }
            cur = cur.Parent;
        }
        for (int i = _hosts.Length - 1; i >= 0; i -= 1) {
            if (_hosts[i] != null && !_hosts[i]._isDisposedTree) { return i; }
        }
        return -1;
    }

    private static NavDestination FindDestination(int hi, string destId) {
        if (hi < 0 || destId == null || destId.Length == 0) { return null; }
        for (int i = 0; i < _dests.Length; i += 1) {
            if (_destHostIdx[i] == hi && _dests[i].Id == destId) { return _dests[i]; }
        }
        return null;
    }

    private static void CommitDestination(int hi, int expectedGen, NavDestination dest) {
        if (hi < 0 || hi >= _hosts.Length || dest == null) { return; }
        if (_hostNavGen[hi] != expectedGen) { return; }
        ContentControl host = _hosts[hi];
        if (host == null || host._isDisposedTree) { return; }
        DependencyObject contentBox = dest.ContentBox;
        if (contentBox == null && dest.BindingPath.Length > 0 && host.DataContext != null) {
            contentBox = BindingEngine.ReadPathBox(host.DataContext, dest.BindingPath);
        }
        BindingEngine.ClearBindings(host, "Content");
        host.SetContentAndTemplate(contentBox, dest.ContentTemplate);
        host.ApplyTemplateSubtree();
        BindingEngine.FlushSubtree(host);
        host.EnsureLoadedSubtree();
        TransitionContentPresenter.PlayEnter(host);
    }

    internal static void CommitPending() {
        if (_hosts == null || _pendingDests == null) { return; }
        for (int i = 0; i < _hosts.Length; i += 1) {
            NavDestination d = _pendingDests[i];
            if (d != null) {
                int expGen = _pendingGens[i];
                _pendingDests[i] = null;
                CommitDestination(i, expGen, d);
            }
        }
    }

    private static void ApplyDestination(int hi, NavDestination dest) {
        if (hi < 0 || hi >= _hosts.Length || dest == null) { return; }
        ContentControl host = _hosts[hi];
        if (host == null || host._isDisposedTree) { return; }
        int gen = _hostNavGen[hi] + 1;
        _hostNavGen[hi] = gen;
        _pendingDests[hi] = null;
        bool hasOldContent = host.ContentBox != null || host.ChildCount() > 0;
        if (hasOldContent && TransitionContentPresenter.HasExitTransition(host)) {
            _pendingDests[hi] = dest;
            _pendingGens[hi] = gen;
            TransitionContentPresenter.PlayExit(host, () => {
                Navigation.CommitPending();
            });
            return;
        }
        CommitDestination(hi, gen, dest);
    }

    public static void NavigateTo(UIElement caller, string destinationId) {
        if (destinationId == null || destinationId.Length == 0) { return; }
        int hi = FindHostIndexForCaller(caller);
        if (hi < 0) { return; }
        NavDestination dest = FindDestination(hi, destinationId);
        if (dest == null) { return; }
        string prev = _curDestIds[hi];
        if (prev.Length > 0 && prev != destinationId) {
            _histHostIdx.Push(hi);
            _histDestIds.Push(prev);
        }
        _curDestIds[hi] = destinationId;
        ApplyDestination(hi, dest);
    }

    public static void NavigateBack(UIElement caller) {
        int hi = FindHostIndexForCaller(caller);
        if (hi < 0) { return; }
        for (int i = _histHostIdx.Length - 1; i >= 0; i -= 1) {
            if (_histHostIdx[i] == hi) {
                string prevId = _histDestIds[i];
                _histHostIdx.RemoveAt(i);
                _histDestIds.RemoveAt(i);
                NavDestination dest = FindDestination(hi, prevId);
                if (dest != null) {
                    _curDestIds[hi] = prevId;
                    ApplyDestination(hi, dest);
                }
                return;
            }
        }
    }
}

public class NavigateToAction : TriggerAction {
    public string Source { get; set; } = "";
    public string TargetUri { get; set; } = "";
    public string DestinationId { get; set; } = "";
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        string id = DestinationId.Length > 0 ? DestinationId : (Source.Length > 0 ? Source : TargetUri);
        if (id.Length > 0) { Navigation.NavigateTo(associatedObject, id); }
    }
}

public class NavigateBackAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        Navigation.NavigateBack(associatedObject);
    }
}

public class RiveInput : FrameworkElement {
    private string _inputName = "";
    private DependencyObject _inputValueBox;
    internal RiveControl _ownerRive;

    public RiveInput() : base(VisualKind.Box) {
        Visibility = Visibility.Collapsed;
        IsHitTestVisible = false;
    }
    public RiveInput(string name, object val) : this() {
        _inputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxObject(val);
    }
    public RiveInput(string name, bool val) : this() {
        _inputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxBool(val);
    }
    public RiveInput(string name, float val) : this() {
        _inputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxFloat(val);
    }
    public RiveInput(string name, int val) : this() {
        _inputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxInt(val);
    }
    public RiveInput(string name, string val) : this() {
        _inputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxString(val);
    }

    public override string GetTypeName() { return "RiveInput"; }

    public string InputName {
        get { return _inputName; }
        set { _inputName = value == null ? "" : value; NotifyOwnerRive(); }
    }

    public object InputValue {
        get { return DependencyObject.Unbox(_inputValueBox); }
        set { _inputValueBox = DependencyObject.BoxObject(value); NotifyOwnerRive(); }
    }
    public DependencyObject InputValueBox {
        get { return _inputValueBox; }
        set { _inputValueBox = value; NotifyOwnerRive(); }
    }
    public void SetInputValueString(string v) { _inputValueBox = DependencyObject.BoxString(v); NotifyOwnerRive(); }
    public void SetInputValueFloat(float v) { _inputValueBox = DependencyObject.BoxFloat(v); NotifyOwnerRive(); }
    public void SetInputValueBool(bool v) { _inputValueBox = DependencyObject.BoxBool(v); NotifyOwnerRive(); }

    private void NotifyOwnerRive() {
        if (_ownerRive != null) { _ownerRive.OnInputChanged(); }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "InputName") { InputName = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "InputValue") { InputValueBox = value; return true; }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "InputName") { return DependencyObject.BoxString(_inputName); }
        if (propName == "InputValue") { return _inputValueBox; }
        return base.GetPropertyValue(propName);
    }
}

public class RiveControl : Control {
    private string _source = "";
    private Stretch _stretch = Stretch.Uniform;
    private RiveInput[] _inputs;
    private Image _imageView;
    private int _instanceId = 0;
    private float _animTime = 0.0f;
    private float _stateTime = 0.0f;
    private string _lastInputsSig = "";
    private string _cachedCleanSrc = "";
    private bool _cachedHasInlineQuery = false;
    private bool _inputsDirty = true;
    private string[] _cachedInputNames;
    private float[] _cachedInputVals;
    private static int _nextInstanceId = 1;
    private static RiveControl[] _activeRives;
    private static bool _riveHookInstalled = false;

    public RiveControl() : base() {
        _inputs = new RiveInput[0];
        _instanceId = _nextInstanceId;
        _nextInstanceId += 1;
        Focusable = false;
        IsTabStop = false;
        RegisterLiveRive(this);
    }

    private static void RegisterLiveRive(RiveControl rc) {
        if (rc == null) { return; }
        if (_activeRives == null) { _activeRives = new RiveControl[0]; }
        if (!_riveHookInstalled) {
            _riveHookInstalled = true;
            BindingEngine.AddFrameTickHook((float dt) => { RiveControl.TickAll(dt); });
            UIElement.RegisterDisposeHook(() => { RiveControl.CleanupDisposed(); });
        }
        _activeRives.Push(rc);
    }

    internal static void CleanupDisposed() {
        if (_activeRives == null) { return; }
        for (int i = _activeRives.Length - 1; i >= 0; i -= 1) {
            if (_activeRives[i] == null || _activeRives[i]._isDisposedTree) {
                _activeRives.RemoveAt(i);
            }
        }
    }

    public static void TickAll() {
        TickAll(0.0166667f);
    }

    public static void TickAll(float dt) {
        if (dt <= 0.0f || _activeRives == null || _activeRives.Length == 0) { return; }
        float step = Math.Clampf(dt, 0.0f, 0.1f);
        bool anyActive = false;
        for (int i = 0; i < _activeRives.Length; i += 1) {
            RiveControl rc = _activeRives[i];
            if (rc == null || rc._isDisposedTree || rc._source.Length == 0 || rc.Visibility != Visibility.Visible) {
                continue;
            }
            rc._animTime += step;
            rc._stateTime += step;
            rc.SyncRiveVisual();
            anyActive = true;
        }
        if (anyActive && ParseUtil.IsNativeReady()) {
            Host_RequestRenderWake();
        }
    }

    public override string GetTypeName() { return "RiveControl"; }

    public string Source {
        get { return _source; }
        set {
            string next = value == null ? "" : value;
            if (_source != next) {
                _source = next;
                _animTime = 0.0f;
                _stateTime = 0.0f;
                string clean = _source.Trim();
                if (clean.Length >= 7 && clean.Substring(0, 7) == "rive://") {
                    clean = clean.Substring(7);
                }
                _cachedCleanSrc = clean;
                _cachedHasInlineQuery = clean.Contains("?");
                _inputsDirty = true;
            }
            SyncRiveVisual();
        }
    }
    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; SyncRiveVisual(); }
    }

    public RiveInput[] Inputs() { return _inputs; }
    public void AddInput(RiveInput input) {
        if (input != null) {
            input._ownerRive = this;
            _inputs.Push(input);
            AddChild(input);
            _inputsDirty = true;
            OnInputChanged();
        }
    }

    internal void OnInputChanged() {
        bool changed = false;
        int n = _inputs != null ? _inputs.Length : 0;
        if (_cachedInputNames == null || _cachedInputNames.Length != n) {
            changed = true;
            _cachedInputNames = new string[n];
            _cachedInputVals = new float[n];
        }
        for (int i = 0; i < n; i += 1) {
            RiveInput ri = _inputs[i];
            string nm = (ri != null && ri.InputName != null) ? ri.InputName : "";
            float fv = (ri != null && ri.InputValueBox != null) ? DependencyObject.UnboxFloat(ri.InputValueBox, 0.0f) : 0.0f;
            if (!changed && (_cachedInputNames[i] != nm || _cachedInputVals[i] != fv)) {
                changed = true;
            }
            _cachedInputNames[i] = nm;
            _cachedInputVals[i] = fv;
        }
        if (changed) {
            _inputsDirty = true;
            _stateTime = 0.0f;
        }
        SyncRiveVisual();
    }

    private string BuildInputsQuery() {
        string q = "";
        for (int i = 0; i < _inputs.Length; i += 1) {
            RiveInput ri = _inputs[i];
            if (ri != null && ri.InputName.Length > 0 && ri.InputValueBox != null) {
                string vStr = DependencyObject.UnboxString(ri.InputValueBox, "0").Trim();
                if (vStr.Length > 0) {
                    q = q + "&" + ri.InputName + "=" + vStr;
                }
            }
        }
        return q;
    }

    public static void SetSource(UIElement el, string src) {
        if (el is RiveControl) { (el as RiveControl).Source = src; }
        else if (el != null) { el.SetPropertyValue("RiveControl.Source", DependencyObject.BoxString(src)); }
    }
    public static string GetSource(UIElement el) {
        if (el is RiveControl) { return (el as RiveControl).Source; }
        if (el != null) { return DependencyObject.UnboxString(el.GetPropertyValue("RiveControl.Source"), ""); }
        return "";
    }

    private static string QuantizeRiveClock(float t) {
        int ticks = (int)(t * 60.0f + 0.5f);
        float qt = (float)ticks / 60.0f;
        return "" + qt;
    }

    internal void SyncRiveVisual() {
        if (_imageView == null) { return; }
        _imageView.Stretch = _stretch;
        if (_cachedCleanSrc.Length == 0 && _source.Length > 0) {
            string c0 = _source.Trim();
            if (c0.Length >= 7 && c0.Substring(0, 7) == "rive://") {
                c0 = c0.Substring(7);
            }
            _cachedCleanSrc = c0;
            _cachedHasInlineQuery = c0.Contains("?");
        }
        string cleanSrc = _cachedCleanSrc;
        if (cleanSrc.Length == 0) {
            if (_imageView.Source != "") { _imageView.Source = ""; }
            return;
        }
        float reqW = 0.0f;
        float reqH = 0.0f;
        if (ActualWidth >= 16.0f && ActualHeight >= 16.0f) {
            float effScale = (Effect != null) ? 0.5f : 1.0f;
            int tw = (((int)(ActualWidth * effScale) + 15) / 16) * 16;
            int th = (((int)(ActualHeight * effScale) + 15) / 16) * 16;
            if (tw < 64) { tw = 64; }
            if (th < 64) { th = 64; }
            reqW = (float)tw;
            reqH = (float)th;
        }
        if (!_cachedHasInlineQuery) {
            string[] rNames = null;
            DependencyObject[] rVals = null;
            if (_inputsDirty && _inputs != null && _inputs.Length > 0) {
                rNames = new string[_inputs.Length];
                rVals = new DependencyObject[_inputs.Length];
                for (int riIdx = 0; riIdx < _inputs.Length; riIdx += 1) {
                    RiveInput ri = _inputs[riIdx];
                    if (ri != null) {
                        rNames[riIdx] = ri.InputName;
                        rVals[riIdx] = ri.InputValueBox;
                    }
                }
            }
            if (_imageView.ApplyRiveDirect(
                    cleanSrc,
                    _instanceId,
                    _animTime,
                    _stateTime,
                    reqW,
                    reqH,
                    _stretch,
                    rNames,
                    rVals,
                    _inputsDirty)) {
                _inputsDirty = false;
                if (Visibility == Visibility.Visible) {
                    Host_RequestRenderWake();
                }
                return;
            }
        }
        string inp = BuildInputsQuery();
        if (inp != _lastInputsSig) {
            _lastInputsSig = inp;
            _stateTime = 0.0f;
        }
        string sizeQ = "";
        if (reqW >= 16.0f && reqH >= 16.0f) {
            sizeQ = "&__w=" + (int)reqW + "&__h=" + (int)reqH;
        }
        string sep = _cachedHasInlineQuery ? "&" : "?";
        string q = sep + "__inst=" + _instanceId +
                   "&__t=" + QuantizeRiveClock(_animTime) +
                   "&__ts=" + QuantizeRiveClock(_stateTime) +
                   sizeQ +
                   inp;
        string nextUri = "rive://" + cleanSrc + q;
        if (_imageView.Source != nextUri) {
            _imageView.Source = nextUri;
        }
        if (ParseUtil.IsNativeReady() && Visibility == Visibility.Visible) {
            Host_RequestRenderWake();
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _imageView = new Image();
        _imageView.HorizontalAlignment = HorizontalAlignment.Stretch;
        _imageView.VerticalAlignment = VerticalAlignment.Stretch;
        SyncRiveVisual();
        return _imageView;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Source" || propName == "RiveControl.Source") { Source = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Stretch") {
            if (value is BoxedInt) { Stretch = EnumConvert.ToStretch((value as BoxedInt).Value); return true; }
            string ss = DependencyObject.UnboxString(value, "Uniform");
            if (ss == "Fill") { Stretch = Stretch.Fill; return true; }
            if (ss == "None") { Stretch = Stretch.None; return true; }
            if (ss == "UniformToFill") { Stretch = Stretch.UniformToFill; return true; }
            Stretch = Stretch.Uniform;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Source" || propName == "RiveControl.Source") { return DependencyObject.BoxString(_source); }
        return base.GetPropertyValue(propName);
    }
}

public class MediaElement : FrameworkElement {
    private string _source = "";
    private Stretch _stretch = Stretch.Uniform;
    private float _volume = 1.0f;
    private bool _isMuted = false;
    private string _loadedBehavior = "Play";
    private string _unloadedBehavior = "Stop";
    private bool _isPlaying = false;
    private float _positionSec = 0.0f;
    private float _durationSec = 10.0f;

    public event RoutedEventHandler MediaOpened;
    public event RoutedEventHandler MediaEnded;

    public MediaElement() : base(VisualKind.CanvasDraw) {}

    public override string GetTypeName() { return "MediaElement"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "MediaEnded") {
            MediaEnded += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    protected override int GetCallbackMask() {
        return VisualCallbacks.StandardInput() + VisualCallbacks.Draw();
    }

    public string Source {
        get { return _source; }
        set { _source = value == null ? "" : value; InvalidateDraw(); }
    }
    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; InvalidateDraw(); }
    }
    public float Volume {
        get { return _volume; }
        set { _volume = Math.Clampf(value, 0.0f, 1.0f); }
    }
    public bool IsMuted {
        get { return _isMuted; }
        set { _isMuted = value; InvalidateDraw(); }
    }
    public string LoadedBehavior {
        get { return _loadedBehavior; }
        set { _loadedBehavior = value == null ? "Play" : value; }
    }
    public string UnloadedBehavior {
        get { return _unloadedBehavior; }
        set { _unloadedBehavior = value == null ? "Stop" : value; }
    }
    public float PositionSec {
        get { return _positionSec; }
        set { _positionSec = Math.Maxf(0.0f, value); InvalidateDraw(); }
    }
    public bool IsPlaying { get { return _isPlaying; } }

    public void InvalidateDraw() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_InvalidateDraw(__nativeHandle);
        }
    }

    public void Play() { _isPlaying = true; InvalidateDraw(); }
    public void Pause() { _isPlaying = false; InvalidateDraw(); }
    public void Stop() { _isPlaying = false; _positionSec = 0.0f; InvalidateDraw(); }
    public void Rewind() { _positionSec = 0.0f; InvalidateDraw(); }

    protected override void OnLoaded() {
        if (_loadedBehavior == "Play") { _isPlaying = true; }
        MediaOpened(this, new RoutedEventArgs("MediaOpened", this));
        InvalidateDraw();
    }

    public override void OnRenderVisual() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        Draw_Begin(__nativeHandle);
        var dc = new DrawingContext(__nativeHandle);
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 480.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 270.0f);

        dc.FillRoundedRect(0.0f, 0.0f, w, h, 8.0f, 8.0f, ParseUtil.PackArgb(255u, 15u, 23u, 42u), -1);
        dc.FillRect(12.0f, 12.0f, Math.Maxf(16.0f, w - 24.0f), Math.Maxf(16.0f, h - 24.0f), ParseUtil.PackArgb(255u, 22u, 33u, 62u), -1);
        dc.FillEllipse(w * 0.5f, h * 0.45f, Math.Minf(w, h) * 0.22f, Math.Minf(w, h) * 0.22f,
            ParseUtil.PackArgb(96u, 56u, 189u, 248u), -1);
        dc.StrokeRoundedRect(1.0f, 1.0f, Math.Maxf(10.0f, w - 2.0f), Math.Maxf(10.0f, h - 2.0f),
            8.0f, 8.0f, ParseUtil.PackArgb(180u, 71u, 85u, 105u), 1.5f);

        float barY = Math.Maxf(16.0f, h - 22.0f);
        float barW = Math.Maxf(24.0f, w - 40.0f);
        dc.FillRoundedRect(20.0f, barY, barW, 4.0f, 2.0f, 2.0f, ParseUtil.PackArgb(160u, 71u, 85u, 105u), -1);
        float prog = _isPlaying ? 0.35f : 0.05f;
        dc.FillRoundedRect(20.0f, barY, barW * prog, 4.0f, 2.0f, 2.0f, ParseUtil.PackArgb(255u, 56u, 189u, 248u), -1);

        string badge = (_isPlaying ? "▶ " : "❚❚ ") + (_source.Length > 0 ? _source : "MediaElement");
        dc.DrawText(badge, 18.0f, Math.Maxf(10.0f, h * 0.45f - 12.0f), Math.Maxf(24.0f, w - 36.0f), 24.0f,
            "Segoe UI", 13.0f, ParseUtil.PackArgb(230u, 226u, 232u, 240u), HorizontalAlignment.Center, VerticalAlignment.Center);
        Draw_End(__nativeHandle);
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Source") { Source = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Volume") { Volume = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "IsMuted") { IsMuted = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "LoadedBehavior") { LoadedBehavior = DependencyObject.UnboxString(value, "Play"); return true; }
        if (propName == "Position") { PositionSec = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Source") { return DependencyObject.BoxString(_source); }
        if (propName == "Volume") { return DependencyObject.BoxFloat(_volume); }
        if (propName == "IsMuted") { return DependencyObject.BoxBool(_isMuted); }
        return base.GetPropertyValue(propName);
    }
}

public class PlayMediaAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is MediaElement) { (dest as MediaElement).Play(); }
    }
}

public class PauseMediaAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is MediaElement) { (dest as MediaElement).Pause(); }
    }
}

public class StopMediaAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is MediaElement) { (dest as MediaElement).Stop(); }
    }
}

public class RewindMediaAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest is MediaElement) { (dest as MediaElement).Rewind(); }
    }
}

public class ComparisonCondition : DependencyObject {
    public string PropertyPath { get; set; } = "";
    public string ElementName { get; set; } = "";
    public string Operator { get; set; } = "Equal";
    private DependencyObject _rightBox;

    public ComparisonCondition() {}
    public ComparisonCondition(string propPath, string op, DependencyObject rightVal) {
        PropertyPath = propPath == null ? "" : propPath;
        Operator = op == null ? "Equal" : op;
        _rightBox = rightVal;
    }
    public ComparisonCondition(string propPath, string op, DependencyObject rightVal, string elementName)
        : this(propPath, op, rightVal) {
        ElementName = elementName == null ? "" : elementName;
    }

    public object RightOperand {
        get { return DependencyObject.Unbox(_rightBox); }
        set { _rightBox = DependencyObject.BoxObject(value); }
    }

    private UIElement ResolveSourceElement(UIElement scope) {
        if (scope == null) { return null; }
        if (ElementName == null || ElementName.Length == 0) { return scope; }
        if (scope.Name == ElementName) { return scope; }
        UIElement se = BindingEntry.FindElementScoped(scope, ElementName);
        if (se == null) { se = scope.FindName(ElementName); }
        if (se == null && scope.RootElement() != null) {
            se = scope.RootElement().FindName(ElementName);
        }
        return se;
    }

    public bool Evaluate(UIElement scope) {
        if (scope == null || PropertyPath.Length == 0) { return true; }
        UIElement src = ResolveSourceElement(scope);
        if (src == null) { return false; }
        DependencyObject left = src.GetPropertyValue(PropertyPath);
        if (left == null) { left = BindingEngine.ReadPathBox(src, PropertyPath); }
        if (left == null && PropertyPath.Length > 12 && PropertyPath.Substring(0, 12) == "DataContext." && src.DataContext != null) {
            left = BindingEngine.ReadPathBox(src.DataContext, PropertyPath.Substring(12));
        }
        if (left == null && src.DataContext != null && ElementName.Length == 0) {
            left = BindingEngine.ReadPathBox(src.DataContext, PropertyPath);
        }
        if (Operator == "Equal" || Operator == "==") { return Trigger.BoxEqualsForProperty(PropertyPath, left, _rightBox); }
        if (Operator == "NotEqual" || Operator == "!=") { return !Trigger.BoxEqualsForProperty(PropertyPath, left, _rightBox); }
        float lf = DependencyObject.UnboxFloat(left, ParseUtil.NaN());
        float rf = DependencyObject.UnboxFloat(_rightBox, ParseUtil.NaN());
        if (ParseUtil.IsNaN(lf) || ParseUtil.IsNaN(rf)) { return false; }
        if (Operator == "LessThan" || Operator == "<") { return lf < rf; }
        if (Operator == "GreaterThan" || Operator == ">") { return lf > rf; }
        if (Operator == "LessThanOrEqual" || Operator == "<=") { return lf <= rf; }
        if (Operator == "GreaterThanOrEqual" || Operator == ">=") { return lf >= rf; }
        return false;
    }
}

public class BehaviorTriggerBase : DependencyObject, IBehaviorTrigger {
    private TriggerAction[] _actions;
    private ComparisonCondition[] _conditions;

    public BehaviorTriggerBase() {
        _actions = new TriggerAction[0];
        _conditions = new ComparisonCondition[0];
    }

    public TriggerAction[] Actions() { return _actions; }
    public void AddAction(TriggerAction action) { if (action != null) { _actions.Push(action); } }
    public void AddCondition(ComparisonCondition cond) { if (cond != null) { _conditions.Push(cond); } }

    public virtual void Attach(UIElement associatedObject) {}

    protected void Fire(UIElement associatedObject, DependencyObject parameter) {
        for (int i = 0; i < _conditions.Length; i += 1) {
            if (!_conditions[i].Evaluate(associatedObject)) { return; }
        }
        for (int i = 0; i < _actions.Length; i += 1) {
            _actions[i].Invoke(associatedObject, parameter);
        }
    }
}

internal class BehaviorBoundEventSink : BehaviorEventSink {
    private BehaviorEventTrigger _trigger;
    private UIElement _owner;
    public BehaviorBoundEventSink(BehaviorEventTrigger trigger, UIElement owner) {
        _trigger = trigger;
        _owner = owner;
    }
    public override void OnBehaviorEvent(object sender, RoutedEventArgs e) {
        if (_trigger != null) { _trigger.DispatchEvent(_owner, e); }
    }
}

public class BehaviorEventTrigger : BehaviorTriggerBase {
    public string EventName { get; set; } = "Loaded";
    public string SourceName { get; set; } = "";
    private UIElement[] _wiredOwners;
    private static BehaviorEventTrigger[] _pendingTriggers;
    private static UIElement[] _pendingOwners;
    private static bool _pendingHookInstalled = false;
    private static BehaviorEventTrigger[] _wiredTriggers;
    private static bool _wiredCleanupInstalled = false;

    public BehaviorEventTrigger() : base() {
        _wiredOwners = new UIElement[0];
    }
    public BehaviorEventTrigger(string eventName) : base() {
        EventName = eventName == null ? "Loaded" : eventName;
        _wiredOwners = new UIElement[0];
    }

    internal void DispatchEvent(UIElement owner, RoutedEventArgs e) {
        Fire(owner, e);
    }

    private UIElement ResolveSourceElement(UIElement associatedObject) {
        if (associatedObject == null) { return null; }
        if (SourceName.Length == 0) { return associatedObject; }
        UIElement se = BindingEntry.FindElementScoped(associatedObject, SourceName);
        if (se == null) { se = associatedObject.FindName(SourceName); }
        if (se == null) {
            UIElement root = associatedObject.RootElement();
            if (root != null) { se = root.FindName(SourceName); }
        }
        return se;
    }

    private bool IsWiredOwner(UIElement owner) {
        if (owner == null || _wiredOwners == null) { return false; }
        for (int i = 0; i < _wiredOwners.Length; i += 1) {
            if (_wiredOwners[i] == owner) { return true; }
        }
        return false;
    }

    internal static void CleanupDisposedWired() {
        if (_wiredTriggers == null) { return; }
        for (int i = _wiredTriggers.Length - 1; i >= 0; i -= 1) {
            BehaviorEventTrigger tr = _wiredTriggers[i];
            if (tr == null) {
                _wiredTriggers.RemoveAt(i);
                continue;
            }
            if (tr._wiredOwners != null) {
                for (int j = tr._wiredOwners.Length - 1; j >= 0; j -= 1) {
                    if (tr._wiredOwners[j] == null || tr._wiredOwners[j]._isDisposedTree) {
                        tr._wiredOwners.RemoveAt(j);
                    }
                }
            }
            if (tr._wiredOwners == null || tr._wiredOwners.Length == 0) {
                _wiredTriggers.RemoveAt(i);
            }
        }
    }

    private void WireSourceEvents(UIElement src, UIElement associatedObject) {
        if (src == null || associatedObject == null || IsWiredOwner(associatedObject)) { return; }
        if (!_wiredCleanupInstalled) {
            _wiredCleanupInstalled = true;
            UIElement.RegisterDisposeHook(() => { BehaviorEventTrigger.CleanupDisposedWired(); });
        }
        if (_wiredTriggers == null) { _wiredTriggers = new BehaviorEventTrigger[0]; }
        bool tracked = false;
        for (int i = 0; i < _wiredTriggers.Length; i += 1) {
            if (_wiredTriggers[i] == this) { tracked = true; break; }
        }
        if (!tracked) { _wiredTriggers.Push(this); }
        _wiredOwners.Push(associatedObject);
        string ev = EventName;
        if (ev == "Loaded") {
            src.Loaded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
            if (src.HasLoadedFired()) {
                Fire(associatedObject, new RoutedEventArgs("Loaded", src));
            }
        } else if (ev == "MouseEnter") {
            src.MouseEnter += (object s, MouseEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeave") {
            src.MouseLeave += (object s, MouseEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeftButtonDown" || ev == "MouseDown") {
            src.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "PreviewMouseDown" || ev == "PreviewMouseLeftButtonDown") {
            src.PreviewMouseLeftButtonDown += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeftButtonUp" || ev == "MouseUp") {
            src.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseRightButtonDown") {
            src.MouseRightButtonDown += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseRightButtonUp") {
            src.MouseRightButtonUp += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "KeyDown") {
            src.KeyDown += (object s, KeyEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "KeyUp") {
            src.KeyUp += (object s, KeyEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "PreviewKeyDown") {
            src.PreviewKeyDown += (object s, KeyEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "GotFocus" || ev == "GotKeyboardFocus") {
            src.GotFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "LostFocus" || ev == "LostKeyboardFocus") {
            src.LostFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (src.SubscribeBehaviorRoutedEvent(ev, new BehaviorBoundEventSink(this, associatedObject))) {
            return;
        } else if (ev == "Click" || ev == "Checked" || ev == "Unchecked" || ev == "Expanded") {
            src.Loaded += (object s, RoutedEventArgs e) => { WireDescendantRoutedEvent(src, ev, associatedObject); };
            if (src.HasLoadedFired()) {
                WireDescendantRoutedEvent(src, ev, associatedObject);
            }
        }
    }

    private static void EnqueuePendingAttach(BehaviorEventTrigger trig, UIElement owner) {
        if (trig == null || owner == null) { return; }
        if (!_pendingHookInstalled) {
            _pendingHookInstalled = true;
            _pendingTriggers = new BehaviorEventTrigger[0];
            _pendingOwners = new UIElement[0];
            BindingEngine.AddPreFlushHook(() => { BehaviorEventTrigger.RetryPendingAttaches(); });
            UIElement.RegisterDisposeHook(() => { BehaviorEventTrigger.CleanupDisposedPending(); });
        }
        if (_pendingTriggers == null) { _pendingTriggers = new BehaviorEventTrigger[0]; }
        if (_pendingOwners == null) { _pendingOwners = new UIElement[0]; }
        for (int i = 0; i < _pendingTriggers.Length; i += 1) {
            if (_pendingTriggers[i] == trig && _pendingOwners[i] == owner) { return; }
        }
        _lastRetryTreeEpoch = -1;
        _pendingTriggers.Push(trig);
        _pendingOwners.Push(owner);
    }

    private static int _lastRetryTreeEpoch = -1;

    internal static void CleanupDisposedPending() {
        if (_pendingTriggers == null || _pendingOwners == null) { return; }
        for (int i = _pendingOwners.Length - 1; i >= 0; i -= 1) {
            if (_pendingOwners[i] == null || _pendingOwners[i]._isDisposedTree) {
                _pendingTriggers.RemoveAt(i);
                _pendingOwners.RemoveAt(i);
            }
        }
    }

    public static void RetryPendingAttaches() {
        if (_pendingTriggers == null || _pendingTriggers.Length == 0) { return; }
        if (UIElement._treeParentEpoch == _lastRetryTreeEpoch) { return; }
        _lastRetryTreeEpoch = UIElement._treeParentEpoch;
        int w = 0;
        for (int i = 0; i < _pendingTriggers.Length; i += 1) {
            BehaviorEventTrigger trig = _pendingTriggers[i];
            UIElement owner = _pendingOwners[i];
            if (trig == null || owner == null || owner._isDisposedTree || trig.IsWiredOwner(owner)) { continue; }
            UIElement src = trig.ResolveSourceElement(owner);
            if (src != null) {
                trig.WireSourceEvents(src, owner);
            } else {
                if (w != i) {
                    _pendingTriggers[w] = trig;
                    _pendingOwners[w] = owner;
                }
                w += 1;
            }
        }
        while (_pendingTriggers.Length > w) {
            int last = _pendingTriggers.Length - 1;
            _pendingTriggers.RemoveAt(last);
            _pendingOwners.RemoveAt(last);
        }
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        UIElement src = ResolveSourceElement(associatedObject);
        if (src != null) {
            WireSourceEvents(src, associatedObject);
            return;
        }
        associatedObject.Loaded += (object s, RoutedEventArgs e) => {
            if (!IsWiredOwner(associatedObject)) {
                UIElement lateSrc = ResolveSourceElement(associatedObject);
                if (lateSrc != null) {
                    WireSourceEvents(lateSrc, associatedObject);
                } else if (SourceName.Length == 0) {
                    WireSourceEvents(associatedObject, associatedObject);
                }
            }
        };
        if (SourceName.Length > 0) {
            EnqueuePendingAttach(this, associatedObject);
        }
    }

    private void WireDescendantRoutedEvent(UIElement node, string ev, UIElement associatedObject) {
        if (node == null) { return; }
        node.SubscribeBehaviorRoutedEvent(ev, new BehaviorBoundEventSink(this, associatedObject));
        int n = node.ChildCount();
        for (int i = 0; i < n; i += 1) {
            WireDescendantRoutedEvent(node.ChildAt(i), ev, associatedObject);
        }
    }
}

public class BehaviorDataTrigger : BehaviorTriggerBase {
    public string BindingPath { get; set; } = "";
    public string ElementName { get; set; } = "";
    public string RelativeSourceMode { get; set; } = "";
    public string RelativeSourceAncestorType { get; set; } = "";
    public string Comparison { get; set; } = "Equal";
    public Binding ValueBinding { get; set; }
    private DependencyObject _valueBox;
    private UIElement[] _initTargets;
    private UIElement[] _matchedTargets;
    private UIElement[] _hookedTargets;
    private UIElement[] _hookedSources;
    private static BehaviorDataTrigger[] _reactiveBTriggers;
    private static UIElement[] _reactiveBTargets;
    private static BehaviorDataTrigger[] _triggersWithTargets;
    private static bool _reactiveBHookInstalled = false;

    public BehaviorDataTrigger() : base() {
        _valueBox = DependencyObject.BoxBool(true);
        _initTargets = new UIElement[0];
        _matchedTargets = new UIElement[0];
        _hookedTargets = new UIElement[0];
        _hookedSources = new UIElement[0];
    }
    public BehaviorDataTrigger(string bindingPath, object value) : this() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxObject(value);
    }

    public void SetValueBox(DependencyObject box) { _valueBox = box; }
    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }

    private UIElement ResolveElementSource(UIElement associatedObject) {
        if (associatedObject == null || ElementName.Length == 0) { return null; }
        if (associatedObject.Name == ElementName) { return associatedObject; }
        UIElement se = BindingEntry.FindElementScoped(associatedObject, ElementName);
        if (se == null) { se = associatedObject.FindName(ElementName); }
        if (se == null) {
            UIElement root = associatedObject.RootElement();
            if (root != null) { se = root.FindName(ElementName); }
        }
        return se;
    }

    private bool HasInitTarget(UIElement target) {
        for (int i = 0; i < _initTargets.Length; i += 1) {
            if (_initTargets[i] == target) { return true; }
        }
        return false;
    }

    private bool IsMatchedFor(UIElement target) {
        for (int i = 0; i < _matchedTargets.Length; i += 1) {
            if (_matchedTargets[i] == target) { return true; }
        }
        return false;
    }

    private void SetMatchedFor(UIElement target, bool active) {
        for (int i = _matchedTargets.Length - 1; i >= 0; i -= 1) {
            if (_matchedTargets[i] == target) {
                if (!active) { _matchedTargets.RemoveAt(i); }
                return;
            }
        }
        if (active) { _matchedTargets.Push(target); }
    }

    private bool HasHookedSource(UIElement target, UIElement src) {
        for (int i = 0; i < _hookedTargets.Length; i += 1) {
            if (_hookedTargets[i] == target && _hookedSources[i] == src) { return true; }
        }
        return false;
    }

    private void TryHookSource(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        UIElement src = null;
        if (ElementName.Length > 0) {
            src = ResolveElementSource(associatedObject);
        } else if (RelativeSourceMode == "Self") {
            src = associatedObject;
        } else if (RelativeSourceMode == "TemplatedParent") {
            src = associatedObject.TemplatedParent;
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = associatedObject;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            src = anc;
        }
        if (src == null || HasHookedSource(associatedObject, src)) { return; }
        TrackTriggerWithTargets();
        _hookedTargets.Push(associatedObject);
        _hookedSources.Push(src);
        src.AddStateInvalidatedHook(() => { CheckAndFire(associatedObject, false); });
    }

    private void TrackTriggerWithTargets() {
        if (!_reactiveBHookInstalled) {
            _reactiveBHookInstalled = true;
            BindingEngine.AddPreFlushHook(() => { BehaviorDataTrigger.FlushReactive(); });
            UIElement.RegisterDisposeHook(() => { BehaviorDataTrigger.CleanupDisposedReactive(); });
        }
        if (_triggersWithTargets == null) { _triggersWithTargets = new BehaviorDataTrigger[0]; }
        for (int i = 0; i < _triggersWithTargets.Length; i += 1) {
            if (_triggersWithTargets[i] == this) { return; }
        }
        _triggersWithTargets.Push(this);
    }

    internal void CleanupDisposedTargets() {
        if (_initTargets != null) {
            for (int i = _initTargets.Length - 1; i >= 0; i -= 1) {
                if (_initTargets[i] == null || _initTargets[i]._isDisposedTree) {
                    _initTargets.RemoveAt(i);
                }
            }
        }
        if (_matchedTargets != null) {
            for (int i = _matchedTargets.Length - 1; i >= 0; i -= 1) {
                if (_matchedTargets[i] == null || _matchedTargets[i]._isDisposedTree) {
                    _matchedTargets.RemoveAt(i);
                }
            }
        }
        if (_hookedTargets != null && _hookedSources != null) {
            for (int i = _hookedTargets.Length - 1; i >= 0; i -= 1) {
                if (_hookedTargets[i] == null || _hookedTargets[i]._isDisposedTree ||
                    _hookedSources[i] == null || _hookedSources[i]._isDisposedTree) {
                    _hookedTargets.RemoveAt(i);
                    _hookedSources.RemoveAt(i);
                }
            }
        }
    }

    private static DependencyObject[] _reactiveBLastDc;
    private static int[] _reactiveBLastVer;
    private static UIElement[] _reactiveBSrc;
    private static int[] _reactiveBSrcTreeEpoch;
    private static int[] _reactiveBSrcChainEpoch;
    private static int _reactiveBCompactDisposeEpoch = -1;
    private static bool _reactiveBAllFastEpochReady = false;
    private static int _reactiveBFastNativeVmEp = -1;
    private static int _reactiveBFastManagedVmEp = -1;
    private static int _reactiveBFastUiPropEp = -1;
    private static int _reactiveBFastLayoutEp = -1;
    private static int _reactiveBFastDcEp = -1;
    private static int _reactiveBFastTreeEp = -1;
    private static int _reactiveBFastVisEp = -1;
    private static int _reactiveBFastDispEp = -1;

    private static void RegisterReactive(BehaviorDataTrigger dt, UIElement target) {
        if (dt == null || target == null) { return; }
        if (_reactiveBTriggers == null) {
            _reactiveBTriggers = new BehaviorDataTrigger[0];
            _reactiveBTargets = new UIElement[0];
            _reactiveBLastDc = new DependencyObject[0];
            _reactiveBLastVer = new int[0];
            _reactiveBSrc = new UIElement[0];
            _reactiveBSrcTreeEpoch = new int[0];
            _reactiveBSrcChainEpoch = new int[0];
        }
        if (!_reactiveBHookInstalled) {
            _reactiveBHookInstalled = true;
            BindingEngine.AddPreFlushHook(() => { BehaviorDataTrigger.FlushReactive(); });
            UIElement.RegisterDisposeHook(() => { BehaviorDataTrigger.CleanupDisposedReactive(); });
        }
        for (int i = 0; i < _reactiveBTriggers.Length; i += 1) {
            if (_reactiveBTriggers[i] == dt && _reactiveBTargets[i] == target) { return; }
        }
        _reactiveBTriggers.Push(dt);
        _reactiveBTargets.Push(target);
        _reactiveBLastDc.Push(null);
        _reactiveBLastVer.Push(-1);
        _reactiveBSrc.Push(null);
        _reactiveBSrcTreeEpoch.Push(0);
        _reactiveBSrcChainEpoch.Push(0);
        _reactiveBAllFastEpochReady = false;
    }

    internal static void CleanupDisposedReactive() {
        _reactiveBAllFastEpochReady = false;
        if (_reactiveBTriggers != null && _reactiveBTargets != null) {
            for (int i = _reactiveBTargets.Length - 1; i >= 0; i -= 1) {
                if (_reactiveBTargets[i] == null || _reactiveBTargets[i]._isDisposedTree) {
                    _reactiveBTriggers.RemoveAt(i);
                    _reactiveBTargets.RemoveAt(i);
                    if (_reactiveBLastDc != null && i < _reactiveBLastDc.Length) {
                        _reactiveBLastDc.RemoveAt(i);
                    }
                    if (_reactiveBLastVer != null && i < _reactiveBLastVer.Length) {
                        _reactiveBLastVer.RemoveAt(i);
                    }
                    if (_reactiveBSrc != null && i < _reactiveBSrc.Length) {
                        _reactiveBSrc.RemoveAt(i);
                    }
                    if (_reactiveBSrcTreeEpoch != null && i < _reactiveBSrcTreeEpoch.Length) {
                        _reactiveBSrcTreeEpoch.RemoveAt(i);
                    }
                    if (_reactiveBSrcChainEpoch != null && i < _reactiveBSrcChainEpoch.Length) {
                        _reactiveBSrcChainEpoch.RemoveAt(i);
                    }
                }
            }
        }
        if (_triggersWithTargets != null) {
            for (int i = _triggersWithTargets.Length - 1; i >= 0; i -= 1) {
                BehaviorDataTrigger dt = _triggersWithTargets[i];
                if (dt == null) {
                    _triggersWithTargets.RemoveAt(i);
                    continue;
                }
                dt.CleanupDisposedTargets();
                bool anyLeft = (dt._initTargets != null && dt._initTargets.Length > 0) ||
                               (dt._matchedTargets != null && dt._matchedTargets.Length > 0) ||
                               (dt._hookedTargets != null && dt._hookedTargets.Length > 0);
                if (!anyLeft) {
                    _triggersWithTargets.RemoveAt(i);
                }
            }
        }
    }

    public static void FlushReactive() {
        if (_reactiveBTriggers == null || _reactiveBTargets == null || _reactiveBTriggers.Length == 0) { return; }
        if (BindingEngine._inFlushPass && _reactiveBAllFastEpochReady &&
            _reactiveBFastNativeVmEp == BindingEngine._curNativeVmEpoch &&
            _reactiveBFastManagedVmEp == BindingEngine._curManagedVmEpoch &&
            _reactiveBFastUiPropEp == UIElement._globalPropEpoch &&
            _reactiveBFastLayoutEp == BindingEngine._curLayoutSizeEpoch &&
            _reactiveBFastDcEp == UIElement._dataContextEpoch &&
            _reactiveBFastTreeEp == UIElement._treeParentEpoch &&
            _reactiveBFastVisEp == UIElement._visibilityEpoch &&
            _reactiveBFastDispEp == UIElement._disposeEpoch) {
            return;
        }
        int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
        int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
        int snapUiPropEp = UIElement._globalPropEpoch;
        int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
        int snapDcEp = UIElement._dataContextEpoch;
        int snapTreeEp = UIElement._treeParentEpoch;
        int snapVisEp = UIElement._visibilityEpoch;
        int snapDispEp = UIElement._disposeEpoch;
        if (_reactiveBLastDc == null) {
            _reactiveBLastDc = new DependencyObject[0];
            _reactiveBLastVer = new int[0];
        }
        if (_reactiveBSrc == null) {
            _reactiveBSrc = new UIElement[0];
            _reactiveBSrcTreeEpoch = new int[0];
            _reactiveBSrcChainEpoch = new int[0];
        }
        while (_reactiveBLastDc.Length < _reactiveBTriggers.Length) {
            _reactiveBLastDc.Push(null);
        }
        while (_reactiveBLastVer.Length < _reactiveBTriggers.Length) {
            _reactiveBLastVer.Push(-1);
        }
        while (_reactiveBSrc.Length < _reactiveBTriggers.Length) {
            _reactiveBSrc.Push(null);
        }
        while (_reactiveBSrcTreeEpoch.Length < _reactiveBTriggers.Length) {
            _reactiveBSrcTreeEpoch.Push(0);
        }
        while (_reactiveBSrcChainEpoch.Length < _reactiveBTriggers.Length) {
            _reactiveBSrcChainEpoch.Push(0);
        }
        if (_reactiveBCompactDisposeEpoch != UIElement._disposeEpoch) {
            _reactiveBCompactDisposeEpoch = UIElement._disposeEpoch;
            int write = 0;
            for (int i = 0; i < _reactiveBTriggers.Length; i += 1) {
                UIElement t = _reactiveBTargets[i];
                if (t == null || t._isDisposedTree) { continue; }
                if (write != i) {
                    _reactiveBTriggers[write] = _reactiveBTriggers[i];
                    _reactiveBTargets[write] = t;
                    _reactiveBLastDc[write] = _reactiveBLastDc[i];
                    _reactiveBLastVer[write] = _reactiveBLastVer[i];
                    _reactiveBSrc[write] = _reactiveBSrc[i];
                    _reactiveBSrcTreeEpoch[write] = _reactiveBSrcTreeEpoch[i];
                    _reactiveBSrcChainEpoch[write] = _reactiveBSrcChainEpoch[i];
                }
                write += 1;
            }
            while (_reactiveBTriggers.Length > write) {
                _reactiveBTriggers.RemoveAt(_reactiveBTriggers.Length - 1);
                _reactiveBTargets.RemoveAt(_reactiveBTargets.Length - 1);
            }
            while (_reactiveBLastDc.Length > write) {
                _reactiveBLastDc.RemoveAt(_reactiveBLastDc.Length - 1);
            }
            while (_reactiveBLastVer.Length > write) {
                _reactiveBLastVer.RemoveAt(_reactiveBLastVer.Length - 1);
            }
            while (_reactiveBSrc.Length > write) {
                _reactiveBSrc.RemoveAt(_reactiveBSrc.Length - 1);
            }
            while (_reactiveBSrcTreeEpoch.Length > write) {
                _reactiveBSrcTreeEpoch.RemoveAt(_reactiveBSrcTreeEpoch.Length - 1);
            }
            while (_reactiveBSrcChainEpoch.Length > write) {
                _reactiveBSrcChainEpoch.RemoveAt(_reactiveBSrcChainEpoch.Length - 1);
            }
        }
        if (_reactiveBTriggers.Length == 0) {
            _reactiveBAllFastEpochReady = false;
            return;
        }
        bool allFastVer = true;
        bool anyFired = false;
        for (int i = 0; i < _reactiveBTriggers.Length; i += 1) {
            BehaviorDataTrigger bdt = _reactiveBTriggers[i];
            UIElement t = _reactiveBTargets[i];
            if (t.IsInsideClosedPopup()) {
                if (bdt.ValueBinding != null) { allFastVer = false; }
                continue;
            }
            if (bdt.ValueBinding == null &&
                bdt.ElementName.Length == 0 &&
                bdt.RelativeSourceMode.Length == 0 &&
                bdt.RelativeSourceAncestorType.Length == 0 &&
                bdt.BindingPath.Length > 0 &&
                !bdt.BindingPath.Contains(".") &&
                bdt.HasInitTarget(t)) {
                DependencyObject dcBox = t.EffectiveDataContextBox();
                object dc = DependencyObject.Unbox(dcBox);
                int dm = (dc != null && !(dc is UIElement) && ParseUtil.IsNativeReady())
                    ? Reflect_GetDirtyMask(dc)
                    : (dc == null ? 0 : -1);
                if (dm >= 0 && _reactiveBLastVer[i] == dm && _reactiveBLastDc[i] == dcBox) {
                    continue;
                }
                if (dm < 0) { allFastVer = false; }
                _reactiveBLastDc[i] = dcBox;
                _reactiveBLastVer[i] = dm;
            } else if (bdt.ValueBinding == null &&
                       (bdt.ElementName.Length > 0 || bdt.RelativeSourceMode.Length > 0 || bdt.RelativeSourceAncestorType.Length > 0) &&
                       bdt.BindingPath.Length > 0 &&
                       bdt.HasInitTarget(t)) {
                UIElement srcEl = _reactiveBSrc[i];
                if (srcEl == null || srcEl._isDisposedTree ||
                    _reactiveBSrcTreeEpoch[i] != UIElement._treeParentEpoch ||
                    _reactiveBSrcChainEpoch[i] != t._ancestorChainEpoch) {
                    srcEl = null;
                    if (bdt.ElementName.Length > 0) {
                        srcEl = bdt.ResolveElementSource(t);
                    } else if (bdt.RelativeSourceMode == "Self") {
                        srcEl = t;
                    } else if (bdt.RelativeSourceMode == "TemplatedParent") {
                        srcEl = t.TemplatedParent;
                    } else if (bdt.RelativeSourceMode == "FindAncestor" || bdt.RelativeSourceMode == "AncestorType" ||
                               bdt.RelativeSourceAncestorType.Length > 0) {
                        UIElement anc = t;
                        while (anc != null && bdt.RelativeSourceAncestorType.Length > 0 &&
                               !BindingEntry.MatchesAncestorType(anc, bdt.RelativeSourceAncestorType)) {
                            anc = BindingEntry.StepAncestor(anc);
                        }
                        srcEl = anc;
                    }
                    if (srcEl != null && !srcEl._isDisposedTree) {
                        _reactiveBSrc[i] = srcEl;
                        _reactiveBSrcTreeEpoch[i] = UIElement._treeParentEpoch;
                        _reactiveBSrcChainEpoch[i] = t._ancestorChainEpoch;
                    } else {
                        srcEl = null;
                        _reactiveBSrc[i] = null;
                    }
                }
                if (srcEl != null) {
                    DependencyObject srcBox = (DependencyObject)srcEl;
                    if ((!bdt.BindingPath.Contains(".") || UIElement.IsAttachedElementPropPath(bdt.BindingPath)) &&
                        BindingEntry.CanUseFastElementPropEpoch(srcEl, bdt.BindingPath)) {
                        int ep = BindingEntry.GetFastElementEpoch(srcEl, bdt.BindingPath);
                        if (ep >= 0 && _reactiveBLastVer[i] == ep && _reactiveBLastDc[i] == srcBox) {
                            continue;
                        }
                        if (ep < 0) { allFastVer = false; }
                        _reactiveBLastDc[i] = srcBox;
                        _reactiveBLastVer[i] = ep;
                    } else if (bdt.BindingPath.Length > 12 && bdt.BindingPath.Substring(0, 12) == "DataContext." &&
                               !bdt.BindingPath.Substring(12).Contains(".") &&
                               srcEl.DataContext != null && !(srcEl.DataContext is UIElement) && ParseUtil.IsNativeReady()) {
                        int dm = Reflect_GetDirtyMask(srcEl.DataContext);
                        int ep2 = dm >= 0 ? ((srcEl._propEpoch % 16384) * 16384 + (dm % 16384) + 1) : -1;
                        if (ep2 >= 0 && _reactiveBLastVer[i] == ep2 && _reactiveBLastDc[i] == srcBox) {
                            continue;
                        }
                        if (ep2 < 0) { allFastVer = false; }
                        _reactiveBLastDc[i] = srcBox;
                        _reactiveBLastVer[i] = ep2;
                    } else {
                        allFastVer = false;
                    }
                } else {
                    allFastVer = false;
                }
            } else {
                allFastVer = false;
            }
            anyFired = true;
            bdt.CheckAndFire(t, false);
        }
        if (allFastVer && !anyFired &&
            BindingEngine._inFlushPass &&
            snapNativeVmEp == BindingEngine._curNativeVmEpoch &&
            snapManagedVmEp == BindingEngine._curManagedVmEpoch &&
            snapUiPropEp == UIElement._globalPropEpoch &&
            snapLayoutEp == BindingEngine._curLayoutSizeEpoch &&
            snapDcEp == UIElement._dataContextEpoch &&
            snapTreeEp == UIElement._treeParentEpoch &&
            snapVisEp == UIElement._visibilityEpoch &&
            snapDispEp == UIElement._disposeEpoch) {
            _reactiveBAllFastEpochReady = true;
            _reactiveBFastNativeVmEp = snapNativeVmEp;
            _reactiveBFastManagedVmEp = snapManagedVmEp;
            _reactiveBFastUiPropEp = snapUiPropEp;
            _reactiveBFastLayoutEp = snapLayoutEp;
            _reactiveBFastDcEp = snapDcEp;
            _reactiveBFastTreeEp = snapTreeEp;
            _reactiveBFastVisEp = snapVisEp;
            _reactiveBFastDispEp = snapDispEp;
        } else {
            _reactiveBAllFastEpochReady = false;
        }
    }

    public static DependencyObject EvaluateDynamicBinding(UIElement associatedObject, Binding b) {
        return BindingEntry.EvaluateStandaloneBinding(associatedObject, b);
    }

    private void CheckAndFire(UIElement associatedObject, bool isInitialAttach) {
        if (associatedObject == null || BindingPath.Length == 0) { return; }
        DependencyObject actual = null;
        object reflectObj = null;
        DependencyObject expectedBox = ValueBinding != null ? EvaluateDynamicBinding(associatedObject, ValueBinding) : _valueBox;
        if (ElementName.Length > 0) {
            UIElement se = ResolveElementSource(associatedObject);
            if (se == null) { return; }
            reflectObj = se;
            actual = se.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(se, BindingPath); }
            if (actual == null && BindingPath.Length > 12 && BindingPath.Substring(0, 12) == "DataContext." && se.DataContext != null) {
                if (!ParseUtil.IsNativeReady() && !(se.DataContext is UIElement)) { return; }
                reflectObj = se.DataContext;
                actual = BindingEngine.ReadPathBox(se.DataContext, BindingPath.Substring(12));
            }
            if (actual == null && se.DataContext != null) {
                if (!ParseUtil.IsNativeReady() && !(se.DataContext is UIElement)) { return; }
                reflectObj = se.DataContext;
                actual = BindingEngine.ReadPathBox(se.DataContext, BindingPath);
            }
        } else if (RelativeSourceMode == "Self") {
            reflectObj = associatedObject;
            actual = associatedObject.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(associatedObject, BindingPath); }
        } else if (RelativeSourceMode == "TemplatedParent") {
            UIElement tp = associatedObject.TemplatedParent;
            if (tp == null) { return; }
            reflectObj = tp;
            actual = tp.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(tp, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = associatedObject;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            if (anc == null) { return; }
            reflectObj = anc;
            actual = anc.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
            if (actual == null && BindingPath.Length > 12 && BindingPath.Substring(0, 12) == "DataContext." && anc.DataContext != null) {
                if (!ParseUtil.IsNativeReady() && !(anc.DataContext is UIElement)) { return; }
                reflectObj = anc.DataContext;
                actual = BindingEngine.ReadPathBox(anc.DataContext, BindingPath.Substring(12));
            }
        } else if (associatedObject.DataContext != null) {
            if (!ParseUtil.IsNativeReady() && !(associatedObject.DataContext is UIElement)) { return; }
            reflectObj = associatedObject.DataContext;
            actual = BindingEngine.ReadPathBox(associatedObject.DataContext, BindingPath);
        } else if (expectedBox != null) {
            if (HasInitTarget(associatedObject)) {
                SetMatchedFor(associatedObject, false);
            }
            return;
        }
        bool matched = false;
        string effPath = (BindingPath.Length > 12 && BindingPath.Substring(0, 12) == "DataContext.")
            ? BindingPath.Substring(12)
            : BindingPath;
        if (Comparison == "Equal" || Comparison == "==" || Comparison == "NotEqual" || Comparison == "!=") {
            bool eqMatched = Trigger.BoxEqualsForProperty(effPath, actual, expectedBox);
            if (!eqMatched && reflectObj != null && expectedBox is BoxedString && ParseUtil.IsNativeReady()) {
                int ordEq = Reflect_ResolveEnumOrdinal(reflectObj, effPath, (expectedBox as BoxedString).Value);
                if (ordEq != -2147483647) {
                    eqMatched = (Reflect_GetPropInt(reflectObj, effPath) == ordEq);
                }
            }
            matched = (Comparison == "Equal" || Comparison == "==") ? eqMatched : !eqMatched;
        } else {
            string expectedStr = DependencyObject.UnboxString(expectedBox, "");
            int ord = (reflectObj != null && expectedStr.Length > 0 && ParseUtil.IsNativeReady())
                ? Reflect_ResolveEnumOrdinal(reflectObj, effPath, expectedStr)
                : -2147483647;
            if (ord != -2147483647) {
                int actOrd = Reflect_GetPropInt(reflectObj, effPath);
                if (Comparison == "GreaterThanOrEqual" || Comparison == ">=") { matched = actOrd >= ord; }
                else if (Comparison == "GreaterThan" || Comparison == ">") { matched = actOrd > ord; }
                else if (Comparison == "LessThanOrEqual" || Comparison == "<=") { matched = actOrd <= ord; }
                else if (Comparison == "LessThan" || Comparison == "<") { matched = actOrd < ord; }
                else { matched = Trigger.BoxEqualsForProperty(effPath, actual, expectedBox); }
            } else {
                float lf = DependencyObject.UnboxFloat(actual, ParseUtil.NaN());
                float rf = DependencyObject.UnboxFloat(expectedBox, ParseUtil.NaN());
                if (!ParseUtil.IsNaN(lf) && !ParseUtil.IsNaN(rf)) {
                    if (Comparison == "GreaterThanOrEqual" || Comparison == ">=") { matched = lf >= rf; }
                    else if (Comparison == "GreaterThan" || Comparison == ">") { matched = lf > rf; }
                    else if (Comparison == "LessThanOrEqual" || Comparison == "<=") { matched = lf <= rf; }
                    else if (Comparison == "LessThan" || Comparison == "<") { matched = lf < rf; }
                } else if (Comparison != "GreaterThanOrEqual" && Comparison != ">=" &&
                           Comparison != "GreaterThan" && Comparison != ">" &&
                           Comparison != "LessThanOrEqual" && Comparison != "<=" &&
                           Comparison != "LessThan" && Comparison != "<") {
                    matched = Trigger.BoxEqualsForProperty(effPath, actual, expectedBox);
                }
            }
        }
        if (!HasInitTarget(associatedObject)) {
            TrackTriggerWithTargets();
            _initTargets.Push(associatedObject);
            if (isInitialAttach && BindingPath == "IsMouseOver" && matched && !DependencyObject.UnboxBool(expectedBox, true)) {
                SetMatchedFor(associatedObject, true);
                return;
            }
            if (matched) {
                SetMatchedFor(associatedObject, true);
                Fire(associatedObject, null);
            }
            return;
        }
        bool was = IsMatchedFor(associatedObject);
        if (matched && !was) {
            SetMatchedFor(associatedObject, true);
            Fire(associatedObject, null);
        } else if (!matched && was) {
            SetMatchedFor(associatedObject, false);
        }
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        TryHookSource(associatedObject);
        RegisterReactive(this, associatedObject);
        CheckAndFire(associatedObject, true);
        associatedObject.Loaded += (object s, RoutedEventArgs e) => {
            TryHookSource(associatedObject);
            CheckAndFire(associatedObject, true);
        };
        associatedObject.GotFocus += (object s, RoutedEventArgs e) => { CheckAndFire(associatedObject, false); };
        associatedObject.LostFocus += (object s, RoutedEventArgs e) => { CheckAndFire(associatedObject, false); };
        associatedObject.DataContextChanged += (object s, DependencyPropertyChangedEventArgs e) => { CheckAndFire(associatedObject, false); };
    }
}

public class BehaviorBase : DependencyObject {
    public virtual void Attach(UIElement associatedObject) {}
    internal virtual void Detach() {}
}

public class MouseDragElementBehavior : BehaviorBase {
    public bool ConstrainToParentBounds { get; set; } = false;
    private bool _dragging = false;
    private float _startX = 0.0f;
    private float _startY = 0.0f;
    private float _baseTX = 0.0f;
    private float _baseTY = 0.0f;

    private float ClampX(UIElement target, float tx) {
        if (!ConstrainToParentBounds || target == null || target.Parent == null) { return tx; }
        UIElement parent = target.Parent;
        float pw = parent.ActualWidth;
        if (pw <= 0.0f && ParseUtil.IsFinite(parent.Width) && parent.Width > 0.0f) { pw = parent.Width; }
        if (pw <= 0.0f) { return tx; }
        float cw = target.ActualWidth;
        if (cw <= 0.0f && ParseUtil.IsFinite(target.Width) && target.Width > 0.0f) { cw = target.Width; }
        float px = parent.LayoutX;
        float cx = target.LayoutX;
        float minTX = px - cx;
        float maxTX = (px + pw) - (cx + cw);
        if (maxTX < minTX) { maxTX = minTX; }
        return Math.Clampf(tx, minTX, maxTX);
    }

    private float ClampY(UIElement target, float ty) {
        if (!ConstrainToParentBounds || target == null || target.Parent == null) { return ty; }
        UIElement parent = target.Parent;
        float ph = parent.ActualHeight;
        if (ph <= 0.0f && ParseUtil.IsFinite(parent.Height) && parent.Height > 0.0f) { ph = parent.Height; }
        if (ph <= 0.0f) { return ty; }
        float ch = target.ActualHeight;
        if (ch <= 0.0f && ParseUtil.IsFinite(target.Height) && target.Height > 0.0f) { ch = target.Height; }
        float py = parent.LayoutY;
        float cy = target.LayoutY;
        float minTY = py - cy;
        float maxTY = (py + ph) - (cy + ch);
        if (maxTY < minTY) { maxTY = minTY; }
        return Math.Clampf(ty, minTY, maxTY);
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        associatedObject.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            if (associatedObject.RenderTransform is TranslateTransform) {
                TranslateTransform tt = associatedObject.RenderTransform as TranslateTransform;
                _baseTX = tt.X;
                _baseTY = tt.Y;
            }
            _dragging = true;
            _startX = e.X;
            _startY = e.Y;
            associatedObject.CaptureMouse();
        };
        associatedObject.MouseMove += (object s, MouseEventArgs e) => {
            if (!_dragging) { return; }
            float tx = ClampX(associatedObject, _baseTX + (e.X - _startX));
            float ty = ClampY(associatedObject, _baseTY + (e.Y - _startY));
            associatedObject.RenderTransform = new TranslateTransform(tx, ty);
        };
        associatedObject.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
            if (_dragging) {
                float tx = ClampX(associatedObject, _baseTX + (e.X - _startX));
                float ty = ClampY(associatedObject, _baseTY + (e.Y - _startY));
                _baseTX = tx;
                _baseTY = ty;
                associatedObject.RenderTransform = new TranslateTransform(tx, ty);
                _dragging = false;
                associatedObject.ReleaseMouseCapture();
            }
        };
    }
}

public class TranslateZoomRotateBehavior : BehaviorBase {
    public string SupportedGestures { get; set; } = "All";
    public float MinimumScale { get; set; } = 0.25f;
    public float MaximumScale { get; set; } = 4.0f;
    public override void Attach(UIElement associatedObject) {}
}

public class BackgroundEffectBehavior : BehaviorBase {
    private UIElement _source;
    private Effect _effect;
    private UIElement _host;
    private BackdropBlurEffect _backdrop;
    private EffectChangeHook _hook;

    public UIElement Source {
        get { return _source; }
        set { _source = value; }
    }

    public Effect Effect {
        get { return _effect; }
        set {
            if (_effect == value) { return; }
            UnhookEffect();
            _effect = value;
            HookAndSync();
        }
    }

    private void UnhookEffect() {
        if (_effect != null && _hook != null) {
            _effect.RemoveChangedListener(_hook);
        }
        _hook = null;
    }

    private void SyncToHost() {
        if (_host == null || _host._isDisposedTree) {
            UnhookEffect();
            _host = null;
            return;
        }
        if (_effect == null) { return; }
        if (_effect is BlurEffect) {
            float r = _effect.Radius;
            if (_backdrop == null) {
                _backdrop = new BackdropBlurEffect(r);
            } else {
                _backdrop.Radius = r;
            }
            Effect targetEffect = _backdrop;
            if (_host.Effect != targetEffect) {
                _host.Effect = targetEffect;
            } else {
                _host.SyncEffect();
            }
        } else {
            _host.Effect = _effect;
        }
    }

    private void HookAndSync() {
        if (_host == null || _host._isDisposedTree) { return; }
        if (_effect != null && _hook == null) {
            _hook = _effect.AddChangedListener(() => { SyncToHost(); });
        }
        SyncToHost();
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        _host = associatedObject;
        HookAndSync();
    }

    internal override void Detach() {
        UnhookEffect();
        _host = null;
        _backdrop = null;
    }
}

public class ConditionalExpression : DependencyObject {
    public string ForwardChaining { get; set; } = "And";
    private ComparisonCondition[] _conditions;
    public ConditionalExpression() { _conditions = new ComparisonCondition[0]; }
    public void AddCondition(ComparisonCondition c) { if (c != null) { _conditions.Push(c); } }
    public bool Evaluate(UIElement scope) {
        if (_conditions.Length == 0) { return true; }
        bool isOr = ForwardChaining == "Or";
        for (int i = 0; i < _conditions.Length; i += 1) {
            bool ok = _conditions[i].Evaluate(scope);
            if (isOr && ok) { return true; }
            if (!isOr && !ok) { return false; }
        }
        return !isOr;
    }
}

public class KeyTrigger : BehaviorTriggerBase {
    public KeyCode Key { get; set; } = KeyCode.None;
    public int Modifiers { get; set; } = 0;

    public KeyTrigger() : base() {}
    public KeyTrigger(KeyCode key, int modifiers = 0) : base() {
        Key = key;
        Modifiers = modifiers;
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        associatedObject.KeyDown += (object s, KeyEventArgs e) => {
            if (e.Key == Key && (Modifiers == 0 || (e.Modifiers % 8) == (Modifiers % 8))) {
                Fire(associatedObject, e);
                e.Handled = true;
            }
        };
    }
}

public class StoryboardCompletedTrigger : BehaviorTriggerBase {
    public Storyboard Storyboard { get; set; }

    public StoryboardCompletedTrigger() : base() {}
    public StoryboardCompletedTrigger(Storyboard sb) : base() { Storyboard = sb; }

    public override void Attach(UIElement associatedObject) {
        if (Storyboard != null) {
            Storyboard.Completed += (object s, EventArgs e) => {
                if (associatedObject != null && !associatedObject._isDisposedTree) {
                    Fire(associatedObject, e);
                }
            };
        }
    }
}

public class GoToStateAction : TriggerAction {
    public string StateName { get; set; } = "";
    public bool UseTransitions { get; set; } = true;

    public GoToStateAction() : base() {}
    public GoToStateAction(string stateName, bool useTransitions = true) : base() {
        StateName = stateName == null ? "" : stateName;
        UseTransitions = useTransitions;
    }

    public override bool SetActionProperty(string propName, DependencyObject value) {
        if (propName == "StateName") { StateName = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "UseTransitions") { UseTransitions = DependencyObject.UnboxBool(value, true); return true; }
        return base.SetActionProperty(propName, value);
    }

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled || StateName.Length == 0) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        UIElement cur = dest != null ? dest : associatedObject;
        while (cur != null) {
            if (VisualStateManager.GoToElementState(cur, StateName, UseTransitions)) { return; }
            if (cur is Control && VisualStateManager.GoToState(cur as Control, StateName, UseTransitions)) { return; }
            cur = cur.TemplatedParent != null ? cur.TemplatedParent : cur.Parent;
        }
    }
}

public class PropertyChangedTrigger : BehaviorTriggerBase {
    public Binding Binding { get; set; }
    private UIElement[] _targets;
    private DependencyObject[] _lastVals;
    private UIElement[] _cachedSrcEls;
    private int[] _cachedSrcEpochs;
    private static PropertyChangedTrigger[] _allPct;
    private static UIElement[] _allPctOwners;
    private static bool _pctHookInstalled = false;

    public PropertyChangedTrigger() : base() {
        _targets = new UIElement[0];
        _lastVals = new DependencyObject[0];
        _cachedSrcEls = new UIElement[0];
        _cachedSrcEpochs = new int[0];
    }

    private void PruneDisposedTargets() {
        for (int i = _targets.Length - 1; i >= 0; i -= 1) {
            if (_targets[i] == null || _targets[i]._isDisposedTree) {
                _targets.RemoveAt(i);
                _lastVals.RemoveAt(i);
                if (_cachedSrcEls != null && i < _cachedSrcEls.Length) {
                    _cachedSrcEls.RemoveAt(i);
                }
                if (_cachedSrcEpochs != null && i < _cachedSrcEpochs.Length) {
                    _cachedSrcEpochs.RemoveAt(i);
                }
            }
        }
    }

    private static int _lastPctCleanupDisposeEpoch = -1;

    private void CheckTarget(UIElement owner, bool prune = true) {
        if (prune) { PruneDisposedTargets(); }
        Binding b = this.Binding;
        if (owner == null || owner._isDisposedTree || b == null || owner.IsInsideClosedPopup()) { return; }
        int existingIdx = -1;
        for (int i = 0; i < _targets.Length; i += 1) {
            if (_targets[i] == owner) {
                existingIdx = i;
                break;
            }
        }
        if (existingIdx >= 0 && b.Converter == null && b.ElementName.Length > 0) {
            string bp = b.Path == null ? "" : b.Path;
            if (!bp.Contains(".") || UIElement.IsAttachedElementPropPath(bp)) {
                UIElement se = _cachedSrcEls[existingIdx];
                if (se == null || se._isDisposedTree) {
                    se = BindingEntry.FindElementScoped(owner, b.ElementName);
                    if (se == null) { se = owner.FindName(b.ElementName); }
                    if (se == null && owner.RootElement() != null) {
                        se = owner.RootElement().FindName(b.ElementName);
                    }
                    _cachedSrcEls[existingIdx] = se;
                }
                if (se != null && BindingEntry.CanUseFastElementPropEpoch(se, bp)) {
                    int ep = BindingEntry.GetFastElementEpoch(se, bp);
                    if (ep >= 0 && ep == _cachedSrcEpochs[existingIdx]) {
                        return;
                    }
                    _cachedSrcEpochs[existingIdx] = ep;
                }
            }
        }
        DependencyObject cur = BehaviorDataTrigger.EvaluateDynamicBinding(owner, b);
        if (existingIdx >= 0) {
            DependencyObject prev = _lastVals[existingIdx];
            bool same = (prev == cur) || (prev != null && cur != null && prev.BoxValueEquals(cur));
            if (!same) {
                _lastVals[existingIdx] = cur;
                Fire(owner, cur);
            }
            return;
        }
        _targets.Push(owner);
        _lastVals.Push(cur);
        _cachedSrcEls.Push(null);
        _cachedSrcEpochs.Push(-1);
    }

    internal static void CleanupDisposed() {
        if (_allPct == null || _allPctOwners == null) { return; }
        for (int i = _allPctOwners.Length - 1; i >= 0; i -= 1) {
            UIElement o = _allPctOwners[i];
            if (o == null || o._isDisposedTree || _allPct[i] == null) {
                _allPct.RemoveAt(i);
                _allPctOwners.RemoveAt(i);
            } else {
                _allPct[i].PruneDisposedTargets();
            }
        }
    }

    public static void FlushAll() {
        if (_allPct == null || _allPct.Length == 0) { return; }
        if (UIElement._disposeEpoch != _lastPctCleanupDisposeEpoch) {
            _lastPctCleanupDisposeEpoch = UIElement._disposeEpoch;
            CleanupDisposed();
        }
        for (int i = 0; i < _allPct.Length; i += 1) {
            _allPct[i].CheckTarget(_allPctOwners[i], false);
        }
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        if (_allPct == null) {
            _allPct = new PropertyChangedTrigger[0];
            _allPctOwners = new UIElement[0];
        }
        if (!_pctHookInstalled) {
            _pctHookInstalled = true;
            BindingEngine.AddPostFlushHook(() => { PropertyChangedTrigger.FlushAll(); });
            UIElement.RegisterDisposeHook(() => { PropertyChangedTrigger.CleanupDisposed(); });
        }
        _allPct.Push(this);
        _allPctOwners.Push(associatedObject);
        CheckTarget(associatedObject, false);
        associatedObject.Loaded += (object s, RoutedEventArgs e) => { CheckTarget(associatedObject, true); };
        associatedObject.DataContextChanged += (object s, DependencyPropertyChangedEventArgs e) => { CheckTarget(associatedObject, true); };
    }
}

public static class DragDrop {
    private static UIElement[] _participants;
    private static UIElement _selectedSource;
    private static UIElement _pressCandidate;
    private static float _pressStartX = 0.0f;
    private static float _pressStartY = 0.0f;
    private static bool _isDragging = false;
    private static Panel _overlayHost;
    private static Border _dragAdorner;
    private static Image _dragAdornerImage;
    private static TranslateTransform _dragAdornerTransform;
    private static float _dragAdornerHalfW = 56.0f;
    private static float _dragAdornerHalfH = 56.0f;
    private static bool _hookInstalled = false;

    internal static bool IsElementInActiveDrag(UIElement el) {
        if (el == null) { return false; }
        return el == _selectedSource || (_isDragging && el == _pressCandidate);
    }

    public static void EnsureInstalled() {
        if (_hookInstalled) { return; }
        _hookInstalled = true;
        _participants = new UIElement[0];
        UIElement._attachedPropHook = (UIElement el, string prop, DependencyObject val) => {
            DragDrop.OnAttachedPropertyChanged(el, prop, val);
        };
        InputDispatcher.RegisterPopupDismissHook((UIElement clicked, bool isEscape) => {
            if (isEscape && (DragDrop.HasSelectedSource() || DragDrop.HasPressCandidate())) {
                DragDrop.CancelSelection();
            }
        });
        BindingEngine.AddPreFlushHook(() => { DragDrop.RefreshAllContentEmpty(); });
    }

    private static string ReadRole(UIElement el) {
        if (el == null) { return ""; }
        return DependencyObject.UnboxString(el.GetPropertyValue("DragDrop.Role"), "");
    }

    private static DependencyObject GetElementItemBox(UIElement el) {
        if (el == null) { return null; }
        if (el is ContentControl) {
            DependencyObject cb = (el as ContentControl).ContentBox;
            if (cb != null) { return cb; }
        }
        return el.EffectiveDataContextBox();
    }

    private static bool ComputeIsEmpty(UIElement el) {
        DependencyObject itemBox = GetElementItemBox(el);
        object item = DependencyObject.Unbox(itemBox);
        if (item == null) { return true; }
        string emptyPath = DependencyObject.UnboxString(el.GetPropertyValue("DragDrop.EmptyContentPath"), "Name");
        if (emptyPath.Length > 0) {
            DependencyObject pb = BindingEngine.ReadPathBox(item, emptyPath);
            if (pb == null) { return true; }
            string s = DependencyObject.UnboxString(pb, "").Trim();
            return s.Length == 0;
        }
        return false;
    }

    private static string ReadItemKindStr(UIElement el) {
        if (el == null) { return ""; }
        DependencyObject explicitKind = el.GetPropertyValue("DragDrop.Kind");
        if (explicitKind != null) {
            string ks = DependencyObject.UnboxString(explicitKind, "").Trim();
            if (ks.Length > 0) { return ks; }
        }
        DependencyObject itemBox = GetElementItemBox(el);
        object item = DependencyObject.Unbox(itemBox);
        if (item != null) {
            DependencyObject cb = BindingEngine.ReadPathBox(item, "Category");
            if (cb != null) {
                string cs = DependencyObject.UnboxString(cb, "").Trim();
                if (cs.Length > 0) { return cs; }
            }
            DependencyObject kb = BindingEngine.ReadPathBox(item, "Kind");
            if (kb != null) {
                string ks2 = DependencyObject.UnboxString(kb, "").Trim();
                if (ks2.Length > 0) { return ks2; }
            }
        }
        return "";
    }

    private static bool IsItemCompatibleWithTarget(string itemKind, UIElement targetEl) {
        if (targetEl == null) { return false; }
        bool matchKind = DependencyObject.UnboxBool(targetEl.GetPropertyValue("DragDrop.MatchKind"), true);
        if (!matchKind) { return true; }
        if (itemKind == null || itemKind.Length == 0 || itemKind == "None" || itemKind == "All") { return true; }
        string dstKind = ReadItemKindStr(targetEl);
        if (dstKind.Length == 0 || dstKind == "None" || dstKind == "All") { return true; }
        return dstKind == itemKind;
    }

    private static UIElement FindOwningItemContainer(UIElement el, ItemsControl ic) {
        if (el == null || ic == null) { return null; }
        UIElement icEl = ic;
        UIElement cur = el;
        while (cur != null && cur != icEl) {
            if (ic.IndexOfItemContainer(cur) >= 0) { return cur; }
            cur = cur.Parent;
        }
        return null;
    }

    private static ItemsControl FindOwningItemsControl(UIElement el) {
        if (el == null) { return null; }
        UIElement p = el.Parent;
        while (p != null) {
            if (p is ItemsControl) {
                ItemsControl ic = p as ItemsControl;
                if (FindOwningItemContainer(el, ic) != null) { return ic; }
            }
            p = p.Parent;
        }
        return null;
    }

    public static void RefreshElementEmpty(UIElement el) {
        if (el == null || el._isDisposedTree) { return; }
        if (el._rare != null && el._rare.DdInitialized && el._rare.DdLastElemEpoch == el._propEpoch && !el._rare.DdLastEmptyPath.Contains(".")) {
            object cachedItem = DependencyObject.Unbox(el._rare.DdLastItemBox);
            int fastDm = (cachedItem != null && !(cachedItem is UIElement) && ParseUtil.IsNativeReady())
                ? Reflect_GetDirtyMask(cachedItem)
                : (cachedItem == null ? 0 : -1);
            if (fastDm >= 0 && fastDm == el._rare.DdLastItemVer) {
                return;
            }
        }
        string emptyPath = DependencyObject.UnboxString(el.GetPropertyValue("DragDrop.EmptyContentPath"), "Name");
        DependencyObject itemBox = GetElementItemBox(el);
        object item = DependencyObject.Unbox(itemBox);
        int dm = (item != null && !(item is UIElement) && ParseUtil.IsNativeReady())
            ? Reflect_GetDirtyMask(item)
            : (item == null ? 0 : -1);
        UIElementRareData er = el.EnsureRare();
        if (dm >= 0 && !emptyPath.Contains(".") && emptyPath == er.DdLastEmptyPath &&
            er.DdLastItemVer == dm && er.DdLastItemBox == itemBox &&
            el.GetPropertyValue("DragDrop.IsContentEmpty") != null &&
            el.GetPropertyValue("DragDrop.IsDropCompatible") != null) {
            er.DdInitialized = true;
            er.DdLastElemEpoch = el._propEpoch;
            return;
        }
        er.DdLastEmptyPath = emptyPath;
        er.DdLastItemBox = itemBox;
        er.DdLastItemVer = dm;
        bool empty = ComputeIsEmpty(el);
        DependencyObject cur = el.GetPropertyValue("DragDrop.IsContentEmpty");
        if (cur == null || DependencyObject.UnboxBool(cur, !empty) != empty) {
            el.SetPropertyValue("DragDrop.IsContentEmpty", DependencyObject.BoxBool(empty));
        }
        if (el.GetPropertyValue("DragDrop.IsDropCompatible") == null) {
            el.SetPropertyValue("DragDrop.IsDropCompatible", DependencyObject.BoxBool(true));
        }
        er.DdInitialized = true;
        er.DdLastElemEpoch = el._propEpoch;
    }

    public static void RefreshAllContentEmpty() {
        if (_overlayHost != null && _overlayHost._isDisposedTree) {
            HideDragAdorner();
            _overlayHost = null;
        }
        if ((_selectedSource != null && _selectedSource._isDisposedTree) ||
            (_pressCandidate != null && _pressCandidate._isDisposedTree)) {
            _pressCandidate = null;
            _selectedSource = null;
            _isDragging = false;
            HideDragAdorner();
        }
        if (_participants == null) { return; }
        for (int i = _participants.Length - 1; i >= 0; i -= 1) {
            UIElement p = _participants[i];
            if (p == null || p._isDisposedTree) {
                _participants.RemoveAt(i);
                continue;
            }
            RefreshElementEmpty(p);
        }
    }

    private static void UpdateCompatibilityStates() {
        if (_participants == null) { return; }
        string srcKind = _selectedSource != null ? ReadItemKindStr(_selectedSource) : "";
        for (int i = 0; i < _participants.Length; i += 1) {
            UIElement p = _participants[i];
            if (p == null || p._isDisposedTree) { continue; }
            bool compat = true;
            if (_selectedSource != null && p != _selectedSource) {
                compat = IsItemCompatibleWithTarget(srcKind, p);
            }
            DependencyObject curCompat = p.GetPropertyValue("DragDrop.IsDropCompatible");
            if (curCompat == null || DependencyObject.UnboxBool(curCompat, !compat) != compat) {
                p.SetPropertyValue("DragDrop.IsDropCompatible", DependencyObject.BoxBool(compat));
            }
            string stateName = compat ? "Compatible" : "NonCompatible";
            VisualStateManager.GoToElementState(p, stateName, true);
            if (p is Control) {
                VisualStateManager.GoToState(p as Control, stateName, true);
            }
        }
        BindingEngine.Flush();
    }

    public static bool HasSelectedSource() { return _selectedSource != null; }
    public static bool HasPressCandidate() { return _pressCandidate != null; }

    private static Panel FindGridOrCanvasRecursive(UIElement el) {
        if (el == null || el._isDisposedTree) { return null; }
        if (el is Grid || el is Canvas) { return el as Panel; }
        if (el is ContentControl && (el as ContentControl).Content is UIElement) {
            Panel foundC = FindGridOrCanvasRecursive((el as ContentControl).Content as UIElement);
            if (foundC != null) { return foundC; }
        }
        int cc = el.ChildCount();
        for (int i = 0; i < cc; i += 1) {
            Panel found = FindGridOrCanvasRecursive(el.ChildAt(i));
            if (found != null) { return found; }
        }
        return null;
    }

    private static Panel ResolveOverlayPanel(UIElement refEl) {
        if (_overlayHost != null && !_overlayHost._isDisposedTree) {
            return _overlayHost;
        }
        Panel bestAncestor = null;
        UIElement cur = refEl;
        while (cur != null) {
            if (cur is Grid || cur is Canvas) {
                bestAncestor = cur as Panel;
            }
            cur = cur.Parent;
        }
        if (bestAncestor != null) { return bestAncestor; }
        UIElement root = refEl != null ? refEl.RootElement() : null;
        if (root is Grid || root is Canvas) { return root as Panel; }
        if (root is ContentControl && (root as ContentControl).Content is UIElement) {
            UIElement contentEl = (root as ContentControl).Content as UIElement;
            if (contentEl is Grid || contentEl is Canvas) { return contentEl as Panel; }
            Panel inner = FindGridOrCanvasRecursive(contentEl);
            if (inner != null) { return inner; }
        }
        Panel fromRoot = FindGridOrCanvasRecursive(root);
        if (fromRoot != null) { return fromRoot; }
        if (root is Panel) { return root as Panel; }
        if (root is ContentControl && (root as ContentControl).Content is Panel) {
            return (root as ContentControl).Content as Panel;
        }
        return null;
    }

    private static void ShowDragAdorner(UIElement src, float screenX, float screenY) {
        HideDragAdorner();
        Panel host = ResolveOverlayPanel(src);
        if (host == null) { return; }
        object item = DependencyObject.Unbox(GetElementItemBox(src));
        string iconUri = "";
        if (item != null) {
            iconUri = DependencyObject.UnboxString(BindingEngine.ReadPathBox(item, "Icon"), "");
        }

        float sw = src != null ? src.ActualWidth : 0.0f;
        float sh = src != null ? src.ActualHeight : 0.0f;
        float adW = 112.0f;
        float adH = 40.0f;
        var adorner = new Border();
        if (iconUri.Length > 0) {
            float tileSide = (sw > 8.0f && sh > 8.0f) ? Math.Minf(sw, sh) : 112.0f;
            tileSide = Math.Clampf(tileSide, 64.0f, 160.0f);
            adW = tileSide;
            adH = tileSide;
            adorner.CornerRadius = new CornerRadius(4.0f);
            adorner.Background = new SolidColorBrush("#78DA9858");
            adorner.BorderBrush = new SolidColorBrush("#FFEBA147");
            adorner.BorderThickness = new Thickness(2.0f);
        } else {
            adW = Math.Clampf(sw > 8.0f ? sw : 112.0f, 64.0f, 320.0f);
            adH = Math.Clampf(sh > 8.0f ? sh : 40.0f, 28.0f, 160.0f);
            adorner.CornerRadius = new CornerRadius(6.0f);
            adorner.Background = (src != null && src.Background != null)
                ? src.Background
                : new SolidColorBrush("#CC1E293B");
            adorner.BorderBrush = (src != null && src.BorderBrush != null)
                ? src.BorderBrush
                : new SolidColorBrush("#FF94A3B8");
            adorner.BorderThickness = new Thickness(1.5f);
        }
        _dragAdornerHalfW = adW * 0.5f;
        _dragAdornerHalfH = adH * 0.5f;

        adorner.Width = adW;
        adorner.Height = adH;
        adorner.HorizontalAlignment = HorizontalAlignment.Left;
        adorner.VerticalAlignment = VerticalAlignment.Top;
        adorner.IsHitTestVisible = false;
        adorner.ZIndex = 9999;
        adorner.Opacity = 0.94f;
        if (host is Grid) {
            UIElement.SetGridRow(adorner, 0);
            UIElement.SetGridColumn(adorner, 0);
            UIElement.SetGridRowSpan(adorner, 64);
            UIElement.SetGridColumnSpan(adorner, 64);
        }
        _dragAdornerTransform = new TranslateTransform(0.0f, 0.0f);
        adorner.RenderTransform = _dragAdornerTransform;
        if (iconUri.Length > 0) {
            float pad = Math.Maxf(6.0f, Math.Minf(adW, adH) * 0.16f);
            var img = new Image();
            img.Source = iconUri;
            img.Stretch = Stretch.Uniform;
            img.Margin = new Thickness(pad);
            adorner.Child = img;
            _dragAdornerImage = img;
        }
        _dragAdorner = adorner;
        host.AddChild(adorner);
        UpdateDragAdornerPosition(screenX, screenY);
    }

    private static void UpdateDragAdornerPosition(float screenX, float screenY) {
        if (_dragAdorner == null || _dragAdorner.Parent == null) { return; }
        UIElement host = _dragAdorner.Parent;
        float lx = screenX;
        float ly = screenY;
        if (ParseUtil.IsValidHandle(host.__nativeHandle)) {
            lx = Visual_ScreenToLocal(host.__nativeHandle, screenX, screenY, 0);
            ly = Visual_ScreenToLocal(host.__nativeHandle, screenX, screenY, 1);
        }
        if (_dragAdornerTransform == null) {
            _dragAdornerTransform = new TranslateTransform(lx - _dragAdornerHalfW, ly - _dragAdornerHalfH);
        } else {
            _dragAdornerTransform.X = lx - _dragAdornerHalfW;
            _dragAdornerTransform.Y = ly - _dragAdornerHalfH;
        }
        _dragAdorner.RenderTransform = _dragAdornerTransform;
    }

    private static void HideDragAdorner() {
        Border oldAdorner = _dragAdorner;
        _dragAdorner = null;
        _dragAdornerImage = null;
        _dragAdornerTransform = null;
        if (oldAdorner != null) {
            if (oldAdorner.Parent != null) {
                oldAdorner.Parent.RemoveChild(oldAdorner);
            }
            oldAdorner.DisposeSubtree();
        }
    }

    private static bool IsPointInsideElement(UIElement el, float screenX, float screenY) {
        if (el == null) { return false; }
        UIElement cur = el;
        while (cur != null) {
            if (cur._isDisposedTree || cur.Visibility != Visibility.Visible) { return false; }
            cur = cur.Parent;
        }
        if (!ParseUtil.IsValidHandle(el.__nativeHandle)) { return false; }
        float w = el.ActualWidth;
        float h = el.ActualHeight;
        if (w <= 0.5f || h <= 0.5f) { return false; }
        float lx = Visual_ScreenToLocal(el.__nativeHandle, screenX, screenY, 0);
        float ly = Visual_ScreenToLocal(el.__nativeHandle, screenX, screenY, 1);
        return lx >= 0.0f && ly >= 0.0f && lx <= w && ly <= h;
    }

    private static UIElement FindDropTargetAtPoint(UIElement src, float screenX, float screenY) {
        if (_participants == null) { return null; }
        UIElement bestSpecific = null;
        UIElement bestItemsControl = null;
        ItemsControl fallbackBag = null;
        for (int i = _participants.Length - 1; i >= 0; i -= 1) {
            UIElement p = _participants[i];
            if (p == null || p._isDisposedTree || p == src) { continue; }
            string role = ReadRole(p);
            if (role != "DragAndDrop" && role != "Drop") { continue; }
            if (fallbackBag == null) {
                ItemsControl ownerIc = FindOwningItemsControl(p);
                if (ownerIc != null && IsPointInsideElement(ownerIc, screenX, screenY)) {
                    fallbackBag = ownerIc;
                }
            }
            if (!IsPointInsideElement(p, screenX, screenY)) { continue; }
            if (p is ItemsControl) {
                if (bestItemsControl == null) { bestItemsControl = p; }
            } else {
                bestSpecific = p;
                break;
            }
        }
        if (bestSpecific != null) { return bestSpecific; }
        if (bestItemsControl != null) { return bestItemsControl; }
        if (fallbackBag != null && !(src.Parent != null && FindOwningItemsControl(src) == fallbackBag)) {
            return fallbackBag;
        }
        return null;
    }

    public static void CancelSelection() {
        if (_pressCandidate != null) {
            _pressCandidate.ReleaseMouseCapture();
        }
        _pressCandidate = null;
        _isDragging = false;
        HideDragAdorner();
        _selectedSource = null;
        UpdateCompatibilityStates();
    }

    private static void TryTransfer(UIElement src, UIElement dst) {
        if (src == null || dst == null || src == dst) { return; }
        UIElement root = src.RootElement();
        if (root == null || root == src) {
            root = dst.RootElement();
        }

        ItemsControl srcIc = FindOwningItemsControl(src);
        UIElement srcCont = srcIc != null ? FindOwningItemContainer(src, srcIc) : null;
        int srcIdx = (srcIc != null && srcCont != null) ? srcIc.IndexOfItemContainer(srcCont) : -1;

        ItemsControl dstIc = (dst is ItemsControl) ? (dst as ItemsControl) : FindOwningItemsControl(dst);
        UIElement dstCont = (dst is ItemsControl || dstIc == null) ? null : FindOwningItemContainer(dst, dstIc);
        int dstIdx = (dstIc != null && dstCont != null) ? dstIc.IndexOfItemContainer(dstCont) : -1;

        DependencyObject srcItemBox = GetElementItemBox(src);
        DependencyObject dstItemBox = (dst is ItemsControl) ? null : GetElementItemBox(dst);
        DependencyObject srcDefBox = src.GetPropertyValue("DragDrop.DefaultContent");
        bool dstEmpty = (dst is ItemsControl) ? true : ComputeIsEmpty(dst);

        if (srcIc != null && srcIdx >= 0 && dstIc != null) {
            if (dstIdx >= 0) {
                if (!dstEmpty) {
                    srcIc.TryReplaceItemBoxAt(srcIdx, dstItemBox);
                    dstIc.TryReplaceItemBoxAt(dstIdx, srcItemBox);
                } else {
                    DependencyObject emptyCellBox = dstItemBox != null ? dstItemBox : srcDefBox;
                    dstIc.TryReplaceItemBoxAt(dstIdx, srcItemBox);
                    if (emptyCellBox != null) {
                        srcIc.TryReplaceItemBoxAt(srcIdx, emptyCellBox);
                    } else {
                        srcIc.TryRemoveItemAt(srcIdx);
                    }
                }
            } else {
                if (dstIc.TryAddItemBox(srcItemBox)) {
                    if (srcDefBox != null) {
                        srcIc.TryReplaceItemBoxAt(srcIdx, srcDefBox);
                    } else {
                        srcIc.TryRemoveItemAt(srcIdx);
                    }
                }
            }
        } else if (srcIc != null && srcIdx >= 0 && dst is ContentControl) {
            (dst as ContentControl).SetContentBox(srcItemBox);
            if (!dstEmpty && dstItemBox != null) {
                srcIc.TryReplaceItemBoxAt(srcIdx, dstItemBox);
            } else if (srcDefBox != null) {
                srcIc.TryReplaceItemBoxAt(srcIdx, srcDefBox);
            } else {
                srcIc.TryRemoveItemAt(srcIdx);
            }
        } else if (src is ContentControl && dstIc != null) {
            if (dstIdx >= 0 && !dstEmpty && IsItemCompatibleWithTarget(ReadItemKindStr(dst), src)) {
                dstIc.TryReplaceItemBoxAt(dstIdx, srcItemBox);
                (src as ContentControl).SetContentBox(dstItemBox);
            } else if (dstIdx >= 0 && dstEmpty) {
                dstIc.TryReplaceItemBoxAt(dstIdx, srcItemBox);
                (src as ContentControl).SetContentBox(srcDefBox);
            } else {
                int emptyIdx = -1;
                int cc = dstIc.ContainerCount();
                for (int i = 0; i < cc; i += 1) {
                    UIElement cont = dstIc.ContainerAt(i);
                    if (cont != null && ComputeIsEmpty(cont)) {
                        emptyIdx = dstIc.IndexOfItemContainer(cont);
                        if (emptyIdx >= 0) { break; }
                    }
                }
                if (emptyIdx >= 0) {
                    dstIc.TryReplaceItemBoxAt(emptyIdx, srcItemBox);
                } else if (dstIdx >= 0) {
                    dstIc.TryInsertItemBoxAt(dstIdx, srcItemBox);
                } else {
                    dstIc.TryAddItemBox(srcItemBox);
                }
                (src as ContentControl).SetContentBox(srcDefBox);
            }
        } else if (src is ContentControl && dst is ContentControl) {
            if (!dstEmpty && IsItemCompatibleWithTarget(ReadItemKindStr(dst), src)) {
                (src as ContentControl).SetContentBox(dstItemBox);
                (dst as ContentControl).SetContentBox(srcItemBox);
            } else {
                (dst as ContentControl).SetContentBox(srcItemBox);
                (src as ContentControl).SetContentBox(srcDefBox);
            }
        }

        if (ParseUtil.IsNativeReady()) {
            if (root == null) { root = dst.RootElement(); }
            object vm = root != null ? root.DataContext : null;
            if (vm == null && root != null) { vm = root; }
            if (vm != null) {
                Reflect_InvokeCommand(vm, "RecalculateStats", "");
            }
        }
        RefreshAllContentEmpty();
        BindingEngine.Flush();
    }

    public static void HandleActivate(UIElement el) {
        if (el == null) { return; }
        string role = ReadRole(el);
        if (_selectedSource == null) {
            if ((role == "DragAndDrop" || role == "Drag") && !ComputeIsEmpty(el)) {
                _selectedSource = el;
                UpdateCompatibilityStates();
            }
            return;
        }
        if (_selectedSource == el) {
            CancelSelection();
            return;
        }
        bool compat = DependencyObject.UnboxBool(el.GetPropertyValue("DragDrop.IsDropCompatible"), true);
        if (compat && (role == "DragAndDrop" || role == "Drop")) {
            UIElement src = _selectedSource;
            _selectedSource = null;
            TryTransfer(src, el);
            UpdateCompatibilityStates();
        }
    }

    public static void HandlePointerDown(UIElement el, MouseButtonEventArgs e) {
        if (el == null || e == null) { return; }
        string role = ReadRole(el);
        if (_selectedSource != null && !_isDragging) {
            HandleActivate(el);
            e.Handled = true;
            return;
        }
        if ((role == "DragAndDrop" || role == "Drag") && !ComputeIsEmpty(el)) {
            _pressCandidate = el;
            _pressStartX = e.X;
            _pressStartY = e.Y;
            _isDragging = false;
            el.CaptureMouse();
            e.Handled = true;
        }
    }

    public static void HandlePointerMove(UIElement el, MouseEventArgs e) {
        if (el == null || e == null || _pressCandidate != el) { return; }
        float dx = e.X - _pressStartX;
        float dy = e.Y - _pressStartY;
        if (!_isDragging && (dx * dx + dy * dy) >= 16.0f) {
            _isDragging = true;
            _selectedSource = el;
            UpdateCompatibilityStates();
            ShowDragAdorner(el, e.X, e.Y);
        }
        if (_isDragging) {
            UpdateDragAdornerPosition(e.X, e.Y);
        }
    }

    public static void HandlePointerUp(UIElement el, MouseButtonEventArgs e) {
        if (el == null || e == null) { return; }
        if (_pressCandidate != el) { return; }
        el.ReleaseMouseCapture();
        bool wasDragging = _isDragging;
        _pressCandidate = null;
        _isDragging = false;
        HideDragAdorner();
        if (wasDragging) {
            UIElement dst = FindDropTargetAtPoint(el, e.X, e.Y);
            string srcKind = ReadItemKindStr(el);
            _selectedSource = null;
            if (dst != null && IsItemCompatibleWithTarget(srcKind, dst)) {
                TryTransfer(el, dst);
            }
            UpdateCompatibilityStates();
            e.Handled = true;
        } else {
            HandleActivate(el);
            e.Handled = true;
        }
    }

    public static void OnAttachedPropertyChanged(UIElement el, string prop, DependencyObject val) {
        if (el == null) { return; }
        if (prop == "DragDrop.IsOverlayHost") {
            if (DependencyObject.UnboxBool(val, false) && el is Panel) {
                _overlayHost = el as Panel;
            }
            return;
        }
        if (prop == "DragDrop.IsContentEmpty" || prop == "DragDrop.IsDropCompatible") {
            return;
        }
        if (prop == "DragDrop.__ContentChanged") {
            RefreshElementEmpty(el);
            return;
        }
        string role = ReadRole(el);
        if (role.Length == 0) { return; }
        if (el is ContentControl && !el.Focusable) {
            el.Focusable = true;
        }
        bool known = false;
        for (int i = 0; i < _participants.Length; i += 1) {
            if (_participants[i] == el) { known = true; break; }
        }
        if (!known) {
            _participants.Push(el);
            el.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                DragDrop.HandlePointerDown(el, e);
            };
            el.MouseMove += (object s, MouseEventArgs e) => {
                DragDrop.HandlePointerMove(el, e);
            };
            el.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
                DragDrop.HandlePointerUp(el, e);
            };
            el.KeyDown += (object s, KeyEventArgs e) => {
                if (e.Key == KeyCode.Space || e.Key == KeyCode.Enter) {
                    DragDrop.HandleActivate(el);
                    e.Handled = true;
                } else if (e.Key == KeyCode.Escape && (DragDrop.HasSelectedSource() || DragDrop.HasPressCandidate())) {
                    DragDrop.CancelSelection();
                    e.Handled = true;
                }
            };
        }
        RefreshElementEmpty(el);
    }
}

public static class Interaction {
    private static UIElement[] _owners;
    private static BehaviorTriggerBase[] _triggers;
    private static UIElement[] _behOwners;
    private static BehaviorBase[] _behaviors;
    private static bool _disposeHookInstalled = false;

    private static void EnsureDisposeHook() {
        if (_disposeHookInstalled) { return; }
        _disposeHookInstalled = true;
        UIElement.RegisterDisposeHook(() => { Interaction.CleanupDisposed(); });
    }

    public static void AddTrigger(UIElement target, BehaviorTriggerBase trigger) {
        DragDrop.EnsureInstalled();
        if (target == null || trigger == null) { return; }
        if (_owners == null) {
            _owners = new UIElement[0];
            _triggers = new BehaviorTriggerBase[0];
            EnsureDisposeHook();
        }
        _owners.Push(target);
        _triggers.Push(trigger);
        trigger.Attach(target);
    }

    public static void AddBehavior(UIElement target, BehaviorBase behavior) {
        if (target == null || behavior == null) { return; }
        if (_behOwners == null) {
            _behOwners = new UIElement[0];
            _behaviors = new BehaviorBase[0];
            EnsureDisposeHook();
        }
        _behOwners.Push(target);
        _behaviors.Push(behavior);
        behavior.Attach(target);
    }

    public static BehaviorTriggerBase[] GetTriggers(UIElement target) {
        var res = new BehaviorTriggerBase[0];
        if (_owners == null || target == null) { return res; }
        for (int i = 0; i < _owners.Length; i += 1) {
            if (_owners[i] == target) { res.Push(_triggers[i]); }
        }
        return res;
    }

    public static void CleanupDisposed() {
        if (_owners != null && _triggers != null) {
            for (int i = _owners.Length - 1; i >= 0; i -= 1) {
                if (_owners[i] == null || _owners[i]._isDisposedTree) {
                    _owners.RemoveAt(i);
                    _triggers.RemoveAt(i);
                }
            }
        }
        if (_behOwners != null && _behaviors != null) {
            for (int i = _behOwners.Length - 1; i >= 0; i -= 1) {
                if (_behOwners[i] == null || _behOwners[i]._isDisposedTree) {
                    if (_behaviors[i] != null) {
                        _behaviors[i].Detach();
                    }
                    _behOwners.RemoveAt(i);
                    _behaviors.RemoveAt(i);
                }
            }
        }
    }

    public static TriggerAction FindNamedAction(UIElement scope, string name) {
        if (_triggers == null || name == null || name.Length == 0) { return null; }
        UIElement scopeRoot = scope != null ? scope.RootElement() : null;
        TriggerAction fallback = null;
        for (int i = 0; i < _triggers.Length; i += 1) {
            BehaviorTriggerBase trig = _triggers[i];
            if (trig == null) { continue; }
            bool sameRoot = (scopeRoot == null) || (_owners[i] != null && _owners[i].RootElement() == scopeRoot);
            TriggerAction[] acts = trig.Actions();
            for (int j = 0; j < acts.Length; j += 1) {
                TriggerAction act = acts[j];
                if (act != null && act.ActionName == name) {
                    if (sameRoot) { return act; }
                    if (fallback == null) { fallback = act; }
                }
            }
        }
        return fallback;
    }
}

public static class RichText {
    private static bool _registered = false;

    public static void Register() {
        if (_registered) { return; }
        _registered = true;
        TextBlock._richTextHandler = (TextBlock tb, string raw) => { Apply(tb, raw); };
    }

    private static string ExtractBindFormat(string header) {
        if (header == null || header.Length < 8) { return ""; }
        int fIdx = -1;
        for (int i = 0; i <= header.Length - 6; i += 1) {
            if (header.Substring(i, 6) == "format" || header.Substring(i, 6) == "Format") {
                fIdx = i + 6;
                break;
            }
        }
        if (fIdx < 0) { return ""; }
        while (fIdx < header.Length && header.Substring(fIdx, 1) != "=") { fIdx += 1; }
        if (fIdx >= header.Length) { return ""; }
        fIdx += 1;
        while (fIdx < header.Length && header.Substring(fIdx, 1) == " ") { fIdx += 1; }
        if (fIdx >= header.Length) { return ""; }
        string q = header.Substring(fIdx, 1);
        if (q != "'" && q != "\"") { return ""; }
        int endQ = fIdx + 1;
        while (endQ < header.Length && header.Substring(endQ, 1) != q) { endQ += 1; }
        if (endQ <= fIdx + 1) { return ""; }
        return header.Substring(fIdx + 1, endQ - (fIdx + 1));
    }

    public static void Apply(TextBlock tb, string raw) {
        if (tb == null) { return; }
        string src = raw == null ? "" : raw;
        tb._rawRichText = src;
        tb._richNeedsResolve = false;
        tb.ClearRichInlines();

        if (src.Length == 0) {
            tb.FinishRichText(true);
            return;
        }

        var cur = new Inline();
        string[] tagStack = new string[0];
        Inline[] stateStack = new Inline[0];

        int i = 0;
        int literalStart = 0;
        int len = src.Length;

        while (i < len) {
            if (src.Substring(i, 1) != "[") {
                i += 1;
                continue;
            }
            int closeIdx = -1;
            int j = i + 1;
            while (j < len) {
                string cj = src.Substring(j, 1);
                if (cj == "]") { closeIdx = j; break; }
                if (cj == "[") { break; }
                j += 1;
            }
            if (closeIdx <= i + 1) {
                i += 1;
                continue;
            }

            if (i > literalStart) {
                var litRun = new Run(src.Substring(literalStart, i - literalStart));
                litRun.CopyStyleFrom(cur);
                tb.PushRichInline(litRun);
            }

            string tagBody = src.Substring(i + 1, closeIdx - (i + 1)).Trim();
            if (tagBody.Length > 1 && tagBody.Substring(0, 1) == "/") {
                string closeTag = tagBody.Substring(1, tagBody.Length - 1).Trim();
                int matchIdx = -1;
                for (int s = tagStack.Length - 1; s >= 0; s -= 1) {
                    if (tagStack[s] == closeTag) { matchIdx = s; break; }
                }
                if (matchIdx >= 0) {
                    cur.CopyStyleFrom(stateStack[matchIdx]);
                    while (tagStack.Length > matchIdx) {
                        int top = tagStack.Length - 1;
                        tagStack.RemoveAt(top);
                        stateStack.RemoveAt(top);
                    }
                }
                i = closeIdx + 1;
                literalStart = i;
                continue;
            }

            if (tagBody == "bind" || (tagBody.Length >= 5 && tagBody.Substring(0, 5) == "bind ")) {
                int endBind = -1;
                int bSearch = closeIdx + 1;
                while (bSearch <= len - 7) {
                    if (src.Substring(bSearch, 7) == "[/bind]") {
                        endBind = bSearch;
                        break;
                    }
                    bSearch += 1;
                }
                if (endBind >= closeIdx + 1) {
                    string bindPath = src.Substring(closeIdx + 1, endBind - (closeIdx + 1)).Trim();
                    string fmtStr = ExtractBindFormat(tagBody);
                    var bRun = new Run("");
                    bRun.CopyStyleFrom(cur);
                    tb.PushRichInline(bRun);
                    if (bindPath.Length > 0) {
                        BindingEngine.BindInlineEx(bRun, "Text", bindPath, "", 0, null, "", "", fmtStr);
                        tb._richNeedsResolve = true;
                    }
                    i = endBind + 7;
                    literalStart = i;
                    continue;
                }
            }

            string tagName = tagBody;
            string tagArg = "";
            int eqPos = -1;
            for (int e = 0; e < tagBody.Length; e += 1) {
                if (tagBody.Substring(e, 1) == "=") { eqPos = e; break; }
            }
            if (eqPos > 0) {
                tagName = tagBody.Substring(0, eqPos).Trim();
                tagArg = tagBody.Substring(eqPos + 1, tagBody.Length - (eqPos + 1)).Trim();
            }

            tagStack.Push(tagName);
            stateStack.Push(cur.CloneStyleState());

            if (tagName == "b") { cur.FontWeight = "Bold"; }
            else if (tagName == "i") { cur.FontStyle = "Italic"; }
            else if (tagName == "u") { if (cur.Decorations == 0) { cur.Decorations = 1; } else if (cur.Decorations == 2) { cur.Decorations = 3; } }
            else if (tagName == "s") { if (cur.Decorations == 0) { cur.Decorations = 2; } else if (cur.Decorations == 1) { cur.Decorations = 3; } }
            else if (tagName == "size") {
                float sz = ParseUtil.ParseFloat(tagArg, 0.0f);
                if (sz > 0.0f) { cur.FontSize = sz; }
            } else if (tagName == "color") {
                if (tagArg.Length > 0) { cur.Foreground = new SolidColorBrush(tagArg); }
            } else if (tagName == "font") {
                if (tagArg.Length > 0) { cur.FontFamily = tagArg; }
            } else if (tagName == "style") {
                DependencyObject res = tb.FindResourceBox(tagArg);
                if (res is Style) {
                    var sChain = new Style[0];
                    Style stCur = res as Style;
                    while (stCur != null && sChain.Length < 16) {
                        sChain.Push(stCur);
                        stCur = stCur.BasedOn;
                    }
                    for (int sc = sChain.Length - 1; sc >= 0; sc -= 1) {
                        Setter[] setters = sChain[sc].Setters();
                        for (int si = 0; si < setters.Length; si += 1) {
                            Setter se = setters[si];
                            if (se == null) { continue; }
                            if (se.Property == "Foreground") {
                                Brush b = UIElement.CoerceBrush(se.ValueBox);
                                if (b != null) { cur.Foreground = b; }
                            } else if (se.Property == "FontWeight") {
                                string fw = DependencyObject.UnboxString(se.ValueBox, "");
                                if (fw.Length > 0) { cur.FontWeight = fw; }
                            } else if (se.Property == "FontStyle") {
                                string fs = DependencyObject.UnboxString(se.ValueBox, "");
                                if (fs.Length > 0) { cur.FontStyle = fs; }
                            } else if (se.Property == "FontSize") {
                                float fsVal = DependencyObject.UnboxFloat(se.ValueBox, 0.0f);
                                if (fsVal > 0.0f) { cur.FontSize = fsVal; }
                            } else if (se.Property == "FontFamily") {
                                string ff = DependencyObject.UnboxString(se.ValueBox, "");
                                if (ff.Length > 0) { cur.FontFamily = ff; }
                            }
                        }
                    }
                } else {
                    tb._richNeedsResolve = true;
                }
            }

            i = closeIdx + 1;
            literalStart = i;
        }

        if (literalStart < len) {
            var tailRun = new Run(src.Substring(literalStart, len - literalStart));
            tailRun.CopyStyleFrom(cur);
            tb.PushRichInline(tailRun);
        }

        tb.FinishRichText(false);
    }
}
