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
    public string Format {
        get { return _format; }
        set { _format = value == null ? "yyyy-MM-dd HH:mm:ss" : value; SyncDisplay(); }
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

    public override void OnRender(DrawingContext dc) {
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 180.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 180.0f);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float outerR = Math.Maxf(8.0f, Math.Minf(cx, cy) - _inset);
        float degToRad = 0.0174532925f;

        uint minorCol = _strokeBrush != null ? _strokeBrush.ResolveRgba() : ParseUtil.PackArgb(255u, 100u, 116u, 139u);
        uint majorCol = _majorStrokeBrush != null ? _majorStrokeBrush.ResolveRgba() : ParseUtil.PackArgb(255u, 241u, 245u, 249u);

        int denom = _tickCount > 1 ? (_tickCount - 1) : 1;
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
}

public class RadialNeedle : CustomDrawElement {
    private float _minimum = 0.0f;
    private float _maximum = 100.0f;
    private float _value = 0.0f;
    private float _startAngleDeg = -225.0f;
    private float _sweepAngleDeg = 270.0f;
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

    public float Minimum { get { return _minimum; } set { _minimum = value; InvalidateDraw(); } }
    public float Maximum { get { return _maximum; } set { _maximum = value; InvalidateDraw(); } }
    public float Value { get { return _value; } set { _value = value; InvalidateDraw(); } }
    public float StartAngleDeg { get { return _startAngleDeg; } set { _startAngleDeg = value; InvalidateDraw(); } }
    public float SweepAngleDeg { get { return _sweepAngleDeg; } set { _sweepAngleDeg = value; InvalidateDraw(); } }
    public float NeedleLengthRatio { get { return _needleLengthRatio; } set { _needleLengthRatio = value; InvalidateDraw(); } }
    public float NeedleWidth { get { return _needleWidth; } set { _needleWidth = value; InvalidateDraw(); } }
    public float PivotRadius { get { return _pivotRadius; } set { _pivotRadius = value; InvalidateDraw(); } }
    public Brush Fill { get { return _fill; } set { _fill = value; InvalidateDraw(); } }

    public float CurrentAngleDeg() {
        float span = _maximum - _minimum;
        float frac = span > 0.0001f ? Math.Clampf((_value - _minimum) / span, 0.0f, 1.0f) : 0.0f;
        return _startAngleDeg + frac * _sweepAngleDeg;
    }

    public override void OnRender(DrawingContext dc) {
        float w = ActualWidth > 0.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 180.0f);
        float h = ActualHeight > 0.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 180.0f);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float radLen = Math.Maxf(8.0f, Math.Minf(cx, cy) * _needleLengthRatio);
        float rad = CurrentAngleDeg() * 0.0174532925f;
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

    public override void Invoke(UIElement associatedObject, DependencyObject parameter) {
        if (!IsEnabled || Command == null) { return; }
        DependencyObject arg = (_paramBox == null && PassEventArgsToCommand) ? parameter : _paramBox;
        CommandTriggerHelper.ExecuteBox(Command, arg);
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

    public BehaviorEventTrigger() : base() {}
    public BehaviorEventTrigger(string eventName) : base() {
        EventName = eventName == null ? "Loaded" : eventName;
    }

    public override void Attach(UIElement associatedObject) {
        if (associatedObject == null) { return; }
        UIElement src = SourceName.Length > 0 ? associatedObject.RootElement().FindName(SourceName) : associatedObject;
        if (src == null) { src = associatedObject; }
        string ev = EventName;
        if (ev == "Loaded") {
            src.Loaded += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseEnter") {
            src.MouseEnter += (object s, MouseEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeave") {
            src.MouseLeave += (object s, MouseEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeftButtonDown") {
            src.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "MouseLeftButtonUp") {
            src.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "GotFocus") {
            src.GotFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "LostFocus") {
            src.LostFocus += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Click" && src is ButtonBase) {
            var bb = src as ButtonBase;
            bb.Click += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Checked" && src is ToggleButton) {
            var tb = src as ToggleButton;
            tb.Checked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "Unchecked" && src is ToggleButton) {
            var tb = src as ToggleButton;
            tb.Unchecked += (object s, RoutedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "TextChanged" && src is TextBoxBase) {
            var txb = src as TextBoxBase;
            txb.TextChanged += (object s, TextChangedEventArgs e) => { Fire(associatedObject, e); };
        } else if (ev == "SelectionChanged" && src is Selector) {
            var sel = src as Selector;
            sel.SelectionChanged += (object s, SelectionChangedEventArgs e) => { Fire(associatedObject, e); };
        }
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

    public static BehaviorTriggerBase[] GetTriggers(UIElement target) {
        var res = new BehaviorTriggerBase[0];
        if (_owners == null || target == null) { return res; }
        for (int i = 0; i < _owners.Length; i += 1) {
            if (_owners[i] == target) { res.Push(_triggers[i]); }
        }
        return res;
    }
}
