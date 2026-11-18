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
    private TextBlock _display;

    public event EventHandler DateTimeChanged;

    public DateTimeControl() : base() {
        Foreground = new SolidColorBrush("#FFF1F5F9");
        FontSize = 14.0f;
    }

    public override string GetTypeName() { return "DateTimeControl"; }

    public int Year { get { return _year; } set { _year = value; SyncDisplay(); } }
    public int Month { get { return _month; } set { _month = ClampRange(value, 1, 12); SyncDisplay(); } }
    public int Day { get { return _day; } set { _day = ClampRange(value, 1, DaysInMonth(_year, _month)); SyncDisplay(); } }
    public int Hour { get { return _hour; } set { _hour = ClampRange(value, 0, 23); SyncDisplay(); } }
    public int Minute { get { return _minute; } set { _minute = ClampRange(value, 0, 59); SyncDisplay(); } }
    public int Second { get { return _second; } set { _second = ClampRange(value, 0, 59); SyncDisplay(); } }
    public bool ShowDate { get { return _showDate; } set { _showDate = value; SyncDisplay(); } }
    public bool ShowTime { get { return _showTime; } set { _showTime = value; SyncDisplay(); } }
    public bool UseSystemTime { get; set; } = false;
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
        Width = 180.0f;
        Height = 180.0f;
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

    public RadialNeedle() : base() {
        _fill = new SolidColorBrush("#FFEF4444");
        Width = 180.0f;
        Height = 180.0f;
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

    public override void AddChild(UIElement child) {
        base.AddChild(child);
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
    public string TargetName { get; set; } = "";
    public UIElement TargetObject { get; set; }
    public bool IsEnabled { get; set; } = true;

    public UIElement ResolveTarget(UIElement associatedObject) {
        if (TargetObject != null) { return TargetObject; }
        if (associatedObject != null && TargetName.Length > 0) {
            UIElement found = associatedObject.RootElement().FindName(TargetName);
            if (found != null) { return found; }
        }
        return associatedObject;
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
        if (CommandParameterPath.Length == 0 || associatedObject == null) { return null; }
        if (CommandParameterRelativeMode == "Self") {
            return BindingEngine.ReadPathBox(associatedObject, CommandParameterPath);
        }
        if (CommandParameterAncestorType.Length > 0) {
            UIElement cur = associatedObject;
            while (cur != null && cur.GetTypeName() != CommandParameterAncestorType) {
                cur = cur.Parent;
            }
            if (cur != null) {
                return BindingEngine.ReadPathBox(cur, CommandParameterPath);
            }
        }
        if (CommandParameterElementName.Length > 0) {
            UIElement named = associatedObject.RootElement().FindName(CommandParameterElementName);
            if (named != null) {
                return BindingEngine.ReadPathBox(named, CommandParameterPath);
            }
        }
        if (associatedObject.DataContext != null) {
            return BindingEngine.ReadPathBox(associatedObject.DataContext, CommandParameterPath);
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
        if (resolved == null && CommandParameterPath.Length > 0) {
            resolved = ResolveDynamicParameter(associatedObject);
        }
        DependencyObject arg = (resolved == null && PassEventArgsToCommand) ? parameter : resolved;
        CommandTriggerHelper.ExecuteBox(cmd, arg);
    }
}

public class ChangePropertyAction : TriggerAction {
    public string PropertyName { get; set; } = "";
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

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled || PropertyName.Length == 0) { return; }
        UIElement dest = ResolveTarget(associatedObject);
        if (dest != null) {
            dest.SetPropertyValue(PropertyName, _valueBox);
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
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {}
}

public class NavigateToAction : TriggerAction {
    public string Source { get; set; } = "";
    public string TargetUri { get; set; } = "";
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {}
}

public class NavigateBackAction : TriggerAction {
    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {}
}

public class RiveInput : DependencyObject {
    public string InputName { get; set; } = "";
    private DependencyObject _inputValueBox;

    public RiveInput() {}
    public RiveInput(string name, object val) {
        InputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxObject(val);
    }
    public RiveInput(string name, bool val) {
        InputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxBool(val);
    }
    public RiveInput(string name, float val) {
        InputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxFloat(val);
    }
    public RiveInput(string name, int val) {
        InputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxInt(val);
    }
    public RiveInput(string name, string val) {
        InputName = name == null ? "" : name;
        _inputValueBox = DependencyObject.BoxString(val);
    }

    public object InputValue {
        get { return DependencyObject.Unbox(_inputValueBox); }
        set { _inputValueBox = DependencyObject.BoxObject(value); }
    }
    public DependencyObject InputValueBox {
        get { return _inputValueBox; }
        set { _inputValueBox = value; }
    }
    public void SetInputValueString(string v) { _inputValueBox = DependencyObject.BoxString(v); }
    public void SetInputValueFloat(float v) { _inputValueBox = DependencyObject.BoxFloat(v); }
    public void SetInputValueBool(bool v) { _inputValueBox = DependencyObject.BoxBool(v); }
}

internal class RiveArtboardCanvas : CustomDrawElement {
    public RiveControl Owner;

    public RiveArtboardCanvas(RiveControl owner) : base() {
        Owner = owner;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        VerticalAlignment = VerticalAlignment.Stretch;
    }

    public override void OnRender(DrawingContext dc) {
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 240.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 160.0f);
        bool hovered = Owner != null && Owner.IsMouseOver;
        bool pressed = Owner != null && Owner.IsPressed;
        string src = Owner != null ? Owner.Source : "";
        string tagStr = Owner != null ? DependencyObject.UnboxString(Owner.GetPropertyValue("Tag"), "") : "";

        uint hash = 2166136261u;
        string key = src + "|" + tagStr;
        for (int i = 0; i < key.Length; i += 1) {
            hash = (hash * 16777619u + (uint)(i + 1) * 31u + (uint)(key.Substring(i, 1) == "a" ? 7u : 13u)) % 65521u;
        }
        uint rBase = 48u + (hash % 110u);
        uint gBase = 72u + ((hash / 11u) % 110u);
        uint bBase = 136u + ((hash / 97u) % 105u);
        if (hovered) {
            rBase = rBase + 28u < 245u ? rBase + 28u : 245u;
            gBase = gBase + 28u < 245u ? gBase + 28u : 245u;
        }
        if (pressed) {
            bBase = bBase + 36u < 255u ? bBase + 36u : 255u;
        }
        uint bgCol = ParseUtil.PackArgb(255u, rBase / 3u, gBase / 3u, bBase / 2u);
        uint cardGlow = ParseUtil.PackArgb(hovered ? 210u : 140u, rBase, gBase, bBase);
        uint accent = ParseUtil.PackArgb(255u, 96u + (rBase % 140u), 180u, 255u);

        dc.FillRoundedRect(2.0f, 2.0f, Math.Maxf(12.0f, w - 4.0f), Math.Maxf(12.0f, h - 4.0f), 14.0f, 14.0f, bgCol, -1);
        dc.StrokeRoundedRect(2.0f, 2.0f, Math.Maxf(12.0f, w - 4.0f), Math.Maxf(12.0f, h - 4.0f), 14.0f, 14.0f, cardGlow, hovered ? 2.5f : 1.5f);

        float cx = w * 0.5f;
        float cy = h * 0.46f;
        float radius = Math.Minf(w, h) * (pressed ? 0.24f : (hovered ? 0.28f : 0.25f));
        dc.FillEllipse(cx, cy, radius * 1.15f, radius * 1.15f, ParseUtil.PackArgb(48u, 255u, 255u, 255u), -1);
        dc.FillEllipse(cx, cy, radius * 0.72f, radius * 0.72f, cardGlow, -1);
        dc.StrokeEllipse(cx, cy, radius, radius, accent, 2.0f);
        dc.DrawLine(cx - radius * 0.45f, cy, cx + radius * 0.45f, cy, accent, 2.0f);
        dc.DrawLine(cx, cy - radius * 0.45f, cx, cy + radius * 0.45f, ParseUtil.PackArgb(220u, 255u, 255u, 255u), 2.0f);

        string label = tagStr.Length > 0 ? tagStr : (src.Length > 0 ? src : "Rive");
        dc.DrawText(label, 12.0f, Math.Maxf(8.0f, h - 32.0f), Math.Maxf(24.0f, w - 24.0f), 22.0f, "Segoe UI", 12.0f,
            ParseUtil.PackArgb(230u, 241u, 245u, 249u), HorizontalAlignment.Center, VerticalAlignment.Center);
    }
}

public class RiveControl : Control {
    private string _source = "";
    private Stretch _stretch = Stretch.Uniform;
    private RiveInput[] _inputs;
    private RiveArtboardCanvas _canvas;

    public RiveControl() : base() {
        _inputs = new RiveInput[0];
        Focusable = true;
    }

    public override string GetTypeName() { return "RiveControl"; }

    public string Source {
        get { return _source; }
        set { _source = value == null ? "" : value; RedrawCanvas(); }
    }
    public Stretch Stretch {
        get { return _stretch; }
        set { _stretch = value; RedrawCanvas(); }
    }

    public RiveInput[] Inputs() { return _inputs; }
    public void AddInput(RiveInput input) {
        if (input != null) {
            _inputs.Push(input);
            RedrawCanvas();
        }
    }

    public static void SetSource(UIElement el, string src) {
        if (el is RiveControl) { (el as RiveControl).Source = src; }
    }
    public static string GetSource(UIElement el) {
        if (el is RiveControl) { return (el as RiveControl).Source; }
        return "";
    }

    private void RedrawCanvas() {
        if (_canvas != null) { _canvas.InvalidateDraw(); }
    }

    protected override UIElement CreateDefaultTemplate() {
        _canvas = new RiveArtboardCanvas(this);
        return _canvas;
    }

    public override void OnMouseEnter(MouseEventArgs e) { RedrawCanvas(); }
    public override void OnMouseLeave(MouseEventArgs e) { RedrawCanvas(); }
    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) { RedrawCanvas(); }
    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) { RedrawCanvas(); }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Source" || propName == "RiveControl.Source") { Source = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "Stretch") {
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
    public string Operator { get; set; } = "Equal";
    private DependencyObject _rightBox;

    public ComparisonCondition() {}
    public ComparisonCondition(string propPath, string op, DependencyObject rightVal) {
        PropertyPath = propPath == null ? "" : propPath;
        Operator = op == null ? "Equal" : op;
        _rightBox = rightVal;
    }

    public object RightOperand {
        get { return DependencyObject.Unbox(_rightBox); }
        set { _rightBox = DependencyObject.BoxObject(value); }
    }

    public bool Evaluate(UIElement scope) {
        if (scope == null || PropertyPath.Length == 0) { return true; }
        DependencyObject left = scope.GetPropertyValue(PropertyPath);
        if (left == null && scope.DataContext != null) {
            left = BindingEngine.ReadPathBox(scope.DataContext, PropertyPath);
        }
        if (Operator == "Equal" || Operator == "==") { return Trigger.BoxEquals(left, _rightBox); }
        if (Operator == "NotEqual" || Operator == "!=") { return !Trigger.BoxEquals(left, _rightBox); }
        float lf = DependencyObject.UnboxFloat(left, 0.0f);
        float rf = DependencyObject.UnboxFloat(_rightBox, 0.0f);
        if (Operator == "LessThan" || Operator == "<") { return lf < rf; }
        if (Operator == "GreaterThan" || Operator == ">") { return lf > rf; }
        if (Operator == "LessThanOrEqual" || Operator == "<=") { return lf <= rf; }
        if (Operator == "GreaterThanOrEqual" || Operator == ">=") { return lf >= rf; }
        return false;
    }
}

public class BehaviorTriggerBase : DependencyObject {
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

public class BehaviorEventTrigger : BehaviorTriggerBase {
    public string EventName { get; set; } = "Loaded";
    public string SourceName { get; set; } = "";
    private UIElement[] _wiredOwners;

    public BehaviorEventTrigger() : base() {
        _wiredOwners = new UIElement[0];
    }
    public BehaviorEventTrigger(string eventName) : base() {
        EventName = eventName == null ? "Loaded" : eventName;
        _wiredOwners = new UIElement[0];
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

    private void WireSourceEvents(UIElement src, UIElement associatedObject) {
        if (src == null || associatedObject == null || IsWiredOwner(associatedObject)) { return; }
        _wiredOwners.Push(associatedObject);
        string ev = EventName;
        if (ev == "Loaded") {
            src.Loaded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
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
        } else if (ev == "GotFocus") {
            src.GotFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "LostFocus") {
            src.LostFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Click" && src is ButtonBase) {
            var bb = src as ButtonBase;
            bb.Click += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Click") {
            src.Loaded += (object s, RoutedEventArgs e) => { WireDescendantRoutedEvent(src, ev, associatedObject); };
        } else if (ev == "Checked" && src is ToggleButton) {
            var tb = src as ToggleButton;
            tb.Checked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Checked") {
            src.Loaded += (object s, RoutedEventArgs e) => { WireDescendantRoutedEvent(src, ev, associatedObject); };
        } else if (ev == "Unchecked" && src is ToggleButton) {
            var tb = src as ToggleButton;
            tb.Unchecked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Unchecked") {
            src.Loaded += (object s, RoutedEventArgs e) => { WireDescendantRoutedEvent(src, ev, associatedObject); };
        } else if (ev == "TextChanged" && src is TextBoxBase) {
            var txb = src as TextBoxBase;
            txb.TextChanged += (object s, TextChangedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "SelectionChanged" && src is Selector) {
            var sel = src as Selector;
            sel.SelectionChanged += (object s, SelectionChangedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Expanded" && src is Expander) {
            var ex = src as Expander;
            ex.Expanded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Expanded" && src is TreeViewItem) {
            var tvi = src as TreeViewItem;
            tvi.Expanded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Expanded") {
            src.Loaded += (object s, RoutedEventArgs e) => { WireDescendantRoutedEvent(src, ev, associatedObject); };
        } else if (ev == "Collapsed" && src is Expander) {
            var ex = src as Expander;
            ex.Collapsed += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MediaEnded" && src is MediaElement) {
            var me = src as MediaElement;
            me.MediaEnded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
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
                WireSourceEvents(lateSrc != null ? lateSrc : associatedObject, associatedObject);
            }
        };
    }

    private void WireDescendantRoutedEvent(UIElement node, string ev, UIElement associatedObject) {
        if (node == null) { return; }
        if (ev == "Click" && node is ButtonBase) {
            ButtonBase bb = node as ButtonBase;
            bb.Click += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Checked" && node is ToggleButton) {
            ToggleButton tb = node as ToggleButton;
            tb.Checked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Unchecked" && node is ToggleButton) {
            ToggleButton tb = node as ToggleButton;
            tb.Unchecked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Expanded" && node is Expander) {
            Expander ex = node as Expander;
            ex.Expanded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Expanded" && node is TreeViewItem) {
            TreeViewItem tvi = node as TreeViewItem;
            tvi.Expanded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        }
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
    private DependencyObject _valueBox;
    private UIElement[] _initTargets;
    private UIElement[] _matchedTargets;
    private UIElement[] _hookedTargets;
    private UIElement[] _hookedSources;

    public BehaviorDataTrigger() : base() {
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
        }
        if (src == null || HasHookedSource(associatedObject, src)) { return; }
        _hookedTargets.Push(associatedObject);
        _hookedSources.Push(src);
        src.AddStateInvalidatedHook(() => { CheckAndFire(associatedObject, false); });
    }

    private void CheckAndFire(UIElement associatedObject, bool isInitialAttach) {
        if (associatedObject == null || BindingPath.Length == 0) { return; }
        DependencyObject actual = null;
        if (ElementName.Length > 0) {
            UIElement se = ResolveElementSource(associatedObject);
            if (se == null) { return; }
            actual = se.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(se, BindingPath); }
            if (actual == null && se.DataContext != null) { actual = BindingEngine.ReadPathBox(se.DataContext, BindingPath); }
        } else if (RelativeSourceMode == "Self") {
            actual = associatedObject.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(associatedObject, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") {
            UIElement anc = associatedObject;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && anc.GetTypeName() != RelativeSourceAncestorType) {
                anc = anc.Parent;
            }
            if (anc != null) {
                actual = anc.GetPropertyValue(BindingPath);
                if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
            }
        } else if (associatedObject.DataContext != null) {
            actual = BindingEngine.ReadPathBox(associatedObject.DataContext, BindingPath);
        }
        bool eq = Trigger.BoxEquals(actual, _valueBox);
        bool matched = (Comparison == "NotEqual") ? !eq : eq;
        if (!HasInitTarget(associatedObject)) {
            _initTargets.Push(associatedObject);
            if (isInitialAttach && BindingPath == "IsMouseOver" && matched && !DependencyObject.UnboxBool(_valueBox, true)) {
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
}

public class MouseDragElementBehavior : BehaviorBase {
    public bool ConstrainToParentBounds { get; set; } = false;
    private bool _dragging = false;
    private float _startX = 0.0f;
    private float _startY = 0.0f;
    private float _baseTX = 0.0f;
    private float _baseTY = 0.0f;

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        associatedObject.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            _dragging = true;
            _startX = e.X;
            _startY = e.Y;
            associatedObject.CaptureMouse();
        };
        associatedObject.MouseMove += (object s, MouseEventArgs e) => {
            if (!_dragging) { return; }
            float dx = e.X - _startX;
            float dy = e.Y - _startY;
            associatedObject.RenderTransform = new TranslateTransform(_baseTX + dx, _baseTY + dy);
        };
        associatedObject.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => {
            if (_dragging) {
                _baseTX = _baseTX + (e.X - _startX);
                _baseTY = _baseTY + (e.Y - _startY);
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
    public UIElement Source { get; set; }
    public Effect Effect { get; set; }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        Effect eff = this.Effect;
        if (eff != null) {
            if (eff is BlurEffect) {
                associatedObject.Effect = new BackdropBlurEffect(eff.Radius);
            } else {
                associatedObject.Effect = eff;
            }
        }
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
            Storyboard.Completed += (object s, EventArgs e) => { Fire(associatedObject, e); };
        }
    }
}

public static class Interaction {
    private static UIElement[] _owners;
    private static BehaviorTriggerBase[] _triggers;
    private static UIElement[] _behOwners;
    private static BehaviorBase[] _behaviors;

    public static void AddTrigger(UIElement target, BehaviorTriggerBase trigger) {
        if (target == null || trigger == null) { return; }
        if (_owners == null) {
            _owners = new UIElement[0];
            _triggers = new BehaviorTriggerBase[0];
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
}
