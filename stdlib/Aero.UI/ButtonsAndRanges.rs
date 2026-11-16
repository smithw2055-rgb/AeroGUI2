module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Shared Command & Click Activation Helper (used by ButtonBase & MenuItem)
// ============================================================================

public static class CommandTriggerHelper {
    public static object ResolveCommandArg(ICommand cmd, DependencyObject paramBox) {
        if (paramBox == null) { return null; }
        if (paramBox is NativeObjectBox) { return paramBox.UnboxObject(); }
        return paramBox;
    }

    public static bool CanExecuteBox(ICommand cmd, DependencyObject paramBox) {
        if (cmd == null) { return true; }
        return cmd.CanExecute(ResolveCommandArg(cmd, paramBox));
    }

    public static void ExecuteBox(ICommand cmd, DependencyObject paramBox) {
        if (cmd == null) { return; }
        object arg = ResolveCommandArg(cmd, paramBox);
        if (cmd.CanExecute(arg)) {
            cmd.Execute(arg);
        }
    }
}

// ============================================================================
// 2. ButtonBase, Button, RepeatButton, Hyperlink, ToggleButton, CheckBox, RadioButton
// ============================================================================

public class ButtonBase : ContentControl {
    private ICommand _command;
    private DependencyObject _commandParamBox;
    private ClickMode _clickMode = ClickMode.Release;
    private Action _canExecHook;

    public event RoutedEventHandler Click;

    public ButtonBase() : base() {
        Focusable = true;
    }

    public override string GetTypeName() { return "ButtonBase"; }

    public ClickMode ClickMode {
        get { return _clickMode; }
        set { _clickMode = value; }
    }

    public ICommand Command {
        get { return _command; }
        set {
            if (_command is RelayCommand && _canExecHook != null) {
                (_command as RelayCommand).UnsubscribeCanExecuteChanged(_canExecHook);
            }
            _command = value;
            if (_command is RelayCommand) {
                if (_canExecHook == null) {
                    _canExecHook = () => { SyncCommandCanExecute(); };
                }
                (_command as RelayCommand).SubscribeCanExecuteChanged(_canExecHook);
            }
            SyncCommandCanExecute();
        }
    }

    public object CommandParameter {
        get { return DependencyObject.Unbox(_commandParamBox); }
        set { _commandParamBox = DependencyObject.BoxObject(value); SyncCommandCanExecute(); }
    }
    public DependencyObject CommandParameterBox {
        get { return _commandParamBox; }
        set { _commandParamBox = value; SyncCommandCanExecute(); }
    }
    public void SetCommandParameter(string v) { CommandParameterBox = DependencyObject.BoxString(v); }
    public void SetCommandParameter(int v) { CommandParameterBox = DependencyObject.BoxInt(v); }
    public void SetCommandParameter(float v) { CommandParameterBox = DependencyObject.BoxFloat(v); }
    public void SetCommandParameter(object v) { CommandParameter = v; }

    protected void SyncCommandCanExecute() {
        if (_command != null) {
            IsEnabled = CommandTriggerHelper.CanExecuteBox(_command, _commandParamBox);
        }
    }

    protected virtual void OnClick() {
        Click(this, new RoutedEventArgs("Click", this));
        CommandTriggerHelper.ExecuteBox(_command, _commandParamBox);
    }

    public void PerformClick() {
        if (IsEnabled) { OnClick(); }
    }

    public override void OnMouseEnter(MouseEventArgs e) {
        base.OnMouseEnter(e);
        if (_clickMode == ClickMode.Hover && IsEnabled) {
            OnClick();
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        e.Handled = true;
        if (_clickMode == ClickMode.Press) {
            OnClick();
        } else {
            CaptureMouse();
        }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        ReleaseMouseCapture();
        if (!IsEnabled) { return; }
        if (_clickMode == ClickMode.Release && IsMouseOver) {
            e.Handled = true;
            OnClick();
        }
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        if (e.Key == KeyCode.Space || e.Key == KeyCode.Enter) {
            e.Handled = true;
            OnClick();
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Command" && value is ICommand) { Command = value as ICommand; return true; }
        if (propName == "CommandParameter") { CommandParameterBox = value; return true; }
        return base.SetPropertyValue(propName, value);
    }
}

public class Button : ButtonBase {
    public bool IsDefault { get; set; }
    public bool IsCancel { get; set; }

    public Button() : base() {
        Background = new SolidColorBrush("#FF242B38");
        Foreground = new SolidColorBrush("#FFF1F5F9");
        BorderBrush = new SolidColorBrush("#FF3B465C");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(14.0f, 7.0f, 14.0f, 7.0f);
    }
    public Button(string contentText) : this() {
        SetContent(contentText);
    }

    public override string GetTypeName() { return "Button"; }

    protected override void OnVisualStatePropertyChanged() {
        if (Style != null) { return; }
        if (!IsEnabled) {
            Opacity = 0.45f;
        } else {
            Opacity = 1.0f;
            if (IsPressed) {
                Background = new SolidColorBrush("#FF1C222D");
            } else if (IsMouseOver) {
                Background = new SolidColorBrush("#FF303A4D");
                BorderBrush = new SolidColorBrush("#FF3B82F6");
            } else {
                Background = new SolidColorBrush("#FF242B38");
                BorderBrush = new SolidColorBrush("#FF3B465C");
            }
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        return _presenter;
    }
}

public class RepeatButton : Button {
    public int Delay { get; set; } = 500;
    public int Interval { get; set; } = 100;

    public RepeatButton() : base() {
        ClickMode = ClickMode.Press;
    }
    public override string GetTypeName() { return "RepeatButton"; }

    public void TickRepeat() {
        if (IsEnabled && IsPressed) { OnClick(); }
    }
}

public class Hyperlink : ButtonBase {
    public string NavigateUri { get; set; } = "";
    public string TargetName { get; set; } = "";
    public event RequestNavigateEventHandler RequestNavigate;

    public Hyperlink() : base() {
        Foreground = new SolidColorBrush("#FF60A5FA");
        Padding = new Thickness(2.0f, 1.0f, 2.0f, 1.0f);
    }
    public Hyperlink(string text, string uri = "") : this() {
        SetContent(text);
        NavigateUri = uri == null ? "" : uri;
    }

    public override string GetTypeName() { return "Hyperlink"; }

    protected override void OnVisualStatePropertyChanged() {
        Foreground = new SolidColorBrush(IsMouseOver ? "#FF93C5FD" : "#FF60A5FA");
    }

    protected override UIElement CreateDefaultTemplate() {
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.SetContentBox(ContentBox);
        return _presenter;
    }

    protected override void OnClick() {
        base.OnClick();
        RequestNavigate(this, new RequestNavigateEventArgs(NavigateUri, TargetName, this));
    }
}

public class ToggleButton : ButtonBase {
    private bool _isChecked = false;
    private bool _isIndeterminate = false;
    public bool IsThreeState { get; set; } = false;

    public event RoutedEventHandler Checked;
    public event RoutedEventHandler Unchecked;
    public event RoutedEventHandler Indeterminate;

    public ToggleButton() : base() {
        Background = new SolidColorBrush("#FF242B38");
        BorderBrush = new SolidColorBrush("#FF3B465C");
        BorderThickness = new Thickness(1.0f);
        CornerRadius = new CornerRadius(6.0f);
        Padding = new Thickness(12.0f, 6.0f, 12.0f, 6.0f);
    }

    public override string GetTypeName() { return "ToggleButton"; }

    public bool IsChecked {
        get { return _isChecked; }
        set {
            if (_isChecked == value && !_isIndeterminate) { return; }
            _isChecked = value;
            _isIndeterminate = false;
            OnCheckedChanged();
            BindingEngine.NotifyTargetPropertyChanged(this, "IsChecked", DependencyObject.BoxBool(_isChecked));
        }
    }

    public bool IsIndeterminate {
        get { return _isIndeterminate; }
        set {
            if (_isIndeterminate == value) { return; }
            _isIndeterminate = value;
            if (value) {
                _isChecked = false;
                Indeterminate(this, new RoutedEventArgs("Indeterminate", this));
            }
            UpdateVisualCheckedState();
        }
    }

    protected virtual void OnCheckedChanged() {
        UpdateVisualCheckedState();
        if (_isChecked) {
            Checked(this, new RoutedEventArgs("Checked", this));
        } else {
            Unchecked(this, new RoutedEventArgs("Unchecked", this));
        }
    }

    protected virtual void UpdateVisualCheckedState() {
        if (Style == null) {
            Background = new SolidColorBrush(_isChecked ? "#FF3B82F6" : "#FF242B38");
            BorderBrush = new SolidColorBrush(_isChecked ? "#FF60A5FA" : "#FF3B465C");
        }
    }

    protected virtual void OnToggle() {
        if (IsThreeState) {
            if (!_isChecked && !_isIndeterminate) { IsChecked = true; }
            else if (_isChecked) { IsIndeterminate = true; }
            else { IsChecked = false; }
        } else {
            IsChecked = !_isChecked;
        }
    }

    protected override void OnClick() {
        OnToggle();
        base.OnClick();
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsChecked") { IsChecked = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsThreeState") { IsThreeState = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsChecked") { return DependencyObject.BoxBool(_isChecked); }
        return base.GetPropertyValue(propName);
    }
}

public class CheckBox : ToggleButton {
    private Border _box;
    private Path _checkMark;

    public CheckBox() : base() {
        Background = Brushes.Transparent();
        BorderThickness = new Thickness(0.0f);
        Padding = new Thickness(2.0f);
        HorizontalContentAlignment = HorizontalAlignment.Left;
    }
    public CheckBox(string label, bool initialChecked = false) : this() {
        SetContent(label);
        IsChecked = initialChecked;
    }

    public override string GetTypeName() { return "CheckBox"; }

    protected override void UpdateVisualCheckedState() {
        if (_box != null) {
            bool active = IsChecked || IsIndeterminate;
            _box.Background = new SolidColorBrush(active ? "#FF3B82F6" : "#FF1C222D");
            _box.BorderBrush = new SolidColorBrush(active ? "#FF60A5FA" : (IsMouseOver ? "#FF64748B" : "#FF475569"));
        }
        if (_checkMark != null) {
            if (IsIndeterminate) {
                _checkMark.Data = "M 4,9 H 14";
                _checkMark.Visibility = Visibility.Visible;
            } else if (IsChecked) {
                _checkMark.Data = "M 3.5,9.5 L 7,13 L 14.5,5";
                _checkMark.Visibility = Visibility.Visible;
            } else {
                _checkMark.Visibility = Visibility.Collapsed;
            }
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        var row = new StackPanel(Orientation.Horizontal, 8.0f);
        row.VerticalAlignment = VerticalAlignment.Center;

        _box = new Border();
        _box.Name = "PART_CheckBox";
        _box.Width = 18.0f;
        _box.Height = 18.0f;
        _box.CornerRadius = new CornerRadius(4.0f);
        _box.BorderThickness = new Thickness(1.5f);
        _box.VerticalAlignment = VerticalAlignment.Center;

        _checkMark = new Path("M 3.5,9.5 L 7,13 L 14.5,5");
        _checkMark.Width = 18.0f;
        _checkMark.Height = 18.0f;
        _checkMark.Stroke = Brushes.White();
        _checkMark.StrokeThickness = 2.0f;
        _checkMark.Stretch = Stretch.None;
        _box.Child = _checkMark;

        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        _presenter.VerticalAlignment = VerticalAlignment.Center;

        row.AddChild(_box);
        row.AddChild(_presenter);
        UpdateVisualCheckedState();
        return row;
    }
}

public class RadioButton : ToggleButton {
    private static RadioButton[] _instances;

    private string _groupName = "";
    private Border _ring;
    private Ellipse _dot;

    public RadioButton() : base() {
        Background = Brushes.Transparent();
        BorderThickness = new Thickness(0.0f);
        Padding = new Thickness(2.0f);
        HorizontalContentAlignment = HorizontalAlignment.Left;
        RegisterInstance(this);
    }
    public RadioButton(string label, string groupName = "") : this() {
        SetContent(label);
        _groupName = groupName == null ? "" : groupName;
    }

    public override string GetTypeName() { return "RadioButton"; }

    public string GroupName {
        get { return _groupName; }
        set { _groupName = value == null ? "" : value; }
    }

    private static void RegisterInstance(RadioButton rb) {
        if (_instances == null) { _instances = new RadioButton[0]; }
        for (int i = 0; i < _instances.Length; i += 1) {
            if (_instances[i] == rb) { return; }
        }
        _instances.Push(rb);
    }

    private void UncheckGroupPeers() {
        if (_instances == null) { return; }
        UIElement myRoot = RootElement();
        for (int i = 0; i < _instances.Length; i += 1) {
            RadioButton other = _instances[i];
            if (other == this || !other.IsChecked) { continue; }
            bool sameGroup = false;
            if (_groupName.Length > 0) {
                sameGroup = (other._groupName == _groupName) && (other.RootElement() == myRoot);
            } else {
                sameGroup = (other._groupName.Length == 0) && (other.Parent == Parent);
            }
            if (sameGroup) {
                other.IsChecked = false;
            }
        }
    }

    protected override void OnCheckedChanged() {
        if (IsChecked) { UncheckGroupPeers(); }
        base.OnCheckedChanged();
    }

    protected override void OnToggle() {
        if (!IsChecked) { IsChecked = true; }
    }

    protected override void UpdateVisualCheckedState() {
        if (_ring != null) {
            _ring.BorderBrush = new SolidColorBrush(IsChecked ? "#FF3B82F6" : (IsMouseOver ? "#FF64748B" : "#FF475569"));
            _ring.Background = new SolidColorBrush("#FF1C222D");
        }
        if (_dot != null) {
            _dot.Visibility = IsChecked ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        var row = new StackPanel(Orientation.Horizontal, 8.0f);
        row.VerticalAlignment = VerticalAlignment.Center;

        _ring = new Border();
        _ring.Name = "PART_RadioRing";
        _ring.Width = 18.0f;
        _ring.Height = 18.0f;
        _ring.CornerRadius = new CornerRadius(9.0f);
        _ring.BorderThickness = new Thickness(1.5f);
        _ring.VerticalAlignment = VerticalAlignment.Center;

        _dot = new Ellipse();
        _dot.Width = 8.0f;
        _dot.Height = 8.0f;
        _dot.Fill = new SolidColorBrush("#FF3B82F6");
        _dot.HorizontalAlignment = HorizontalAlignment.Center;
        _dot.VerticalAlignment = VerticalAlignment.Center;
        _ring.Child = _dot;

        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        _presenter.VerticalAlignment = VerticalAlignment.Center;

        row.AddChild(_ring);
        row.AddChild(_presenter);
        UpdateVisualCheckedState();
        return row;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "GroupName") { GroupName = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }
}

// ============================================================================
// 3. Thumb, GridSplitter, RangeBase, Slider, ProgressBar, ScrollBar, ScrollViewer
// ============================================================================

public class Thumb : Control {
    private bool _isDragging = false;
    private float _startX = 0.0f;
    private float _startY = 0.0f;
    private float _prevX = 0.0f;
    private float _prevY = 0.0f;

    public event DragStartedEventHandler DragStarted;
    public event DragDeltaEventHandler DragDelta;
    public event DragCompletedEventHandler DragCompleted;

    public Thumb() : base() {
        Background = new SolidColorBrush("#FF475569");
        CornerRadius = new CornerRadius(4.0f);
    }

    public override string GetTypeName() { return "Thumb"; }
    public bool IsDragging { get { return _isDragging; } }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        _isDragging = true;
        _startX = e.X;
        _startY = e.Y;
        _prevX = e.X;
        _prevY = e.Y;
        CaptureMouse();
        e.Handled = true;
        DragStarted(this, new DragStartedEventArgs(0.0f, 0.0f, this));
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_isDragging) { return; }
        float dx = e.X - _prevX;
        float dy = e.Y - _prevY;
        _prevX = e.X;
        _prevY = e.Y;
        if (dx != 0.0f || dy != 0.0f) {
            DragDelta(this, new DragDeltaEventArgs(dx, dy, this));
        }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (!_isDragging) { return; }
        _isDragging = false;
        ReleaseMouseCapture();
        e.Handled = true;
        DragCompleted(this, new DragCompletedEventArgs(e.X - _startX, e.Y - _startY, false, this));
    }
}

public class GridSplitter : Thumb {
    public GridSplitter() : base() {
        Background = new SolidColorBrush("#FF334155");
        CornerRadius = new CornerRadius(2.0f);
        DragDelta += (object s, DragDeltaEventArgs e) => { ApplyGridResize(e.HorizontalChange, e.VerticalChange); };
    }

    public override string GetTypeName() { return "GridSplitter"; }

    private void ApplyGridResize(float dx, float dy) {
        if (!(Parent is Grid)) { return; }
        Grid g = Parent as Grid;
        float aw = ActualWidth;
        float ah = ActualHeight;
        if (aw <= ah + 2.0f) {
            int col = UIElement.GetGridColumn(this);
            ColumnDefinitionCollection cols = g.ColumnDefinitions;
            if (col > 0 && col < cols.Count()) {
                ColumnDefinition left = cols.Get(col - 1);
                float nextW = Math.Maxf(24.0f, left.Width.Value + dx);
                left.Width = GridLength.FromPixel(nextW);
                g.SyncGridTracks();
            }
        } else {
            int row = UIElement.GetGridRow(this);
            RowDefinitionCollection rows = g.RowDefinitions;
            if (row > 0 && row < rows.Count()) {
                RowDefinition top = rows.Get(row - 1);
                float nextH = Math.Maxf(24.0f, top.Height.Value + dy);
                top.Height = GridLength.FromPixel(nextH);
                g.SyncGridTracks();
            }
        }
    }
}

public class RangeBase : Control {
    private float _min = 0.0f;
    private float _max = 100.0f;
    private float _value = 0.0f;
    public float SmallChange { get; set; } = 1.0f;
    public float LargeChange { get; set; } = 10.0f;

    public event RoutedPropertyChangedEventHandler<float> ValueChanged;

    public RangeBase() : base() {}
    public override string GetTypeName() { return "RangeBase"; }

    public float Minimum {
        get { return _min; }
        set {
            _min = value;
            if (_max < _min) { _max = _min; }
            Value = _value;
            OnRangeUpdated();
        }
    }
    public float Maximum {
        get { return _max; }
        set {
            _max = value < _min ? _min : value;
            Value = _value;
            OnRangeUpdated();
        }
    }
    public float Value {
        get { return _value; }
        set {
            float clamped = Math.Clampf(value, _min, _max);
            if (_value == clamped) { return; }
            float old = _value;
            _value = clamped;
            OnValueChanged(old, clamped);
            ValueChanged(this, new RoutedPropertyChangedEventArgs<float>(old, clamped, this));
            BindingEngine.NotifyTargetPropertyChanged(this, "Value", DependencyObject.BoxFloat(clamped));
        }
    }

    public float NormalizedFraction() {
        float span = _max - _min;
        if (span <= 0.00001f) { return 0.0f; }
        return Math.Clampf((_value - _min) / span, 0.0f, 1.0f);
    }

    protected virtual void OnRangeUpdated() {}
    protected virtual void OnValueChanged(float oldVal, float newVal) { OnRangeUpdated(); }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 100.0f); return true; }
        if (propName == "Value") { Value = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "SmallChange") { SmallChange = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "LargeChange") { LargeChange = DependencyObject.UnboxFloat(value, 10.0f); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Value") { return DependencyObject.BoxFloat(_value); }
        if (propName == "Minimum") { return DependencyObject.BoxFloat(_min); }
        if (propName == "Maximum") { return DependencyObject.BoxFloat(_max); }
        return base.GetPropertyValue(propName);
    }
}

public class Slider : RangeBase {
    private Orientation _orientation = Orientation.Horizontal;
    private float _tickFrequency = 0.0f;
    public bool IsSnapToTickEnabled { get; set; } = false;
    public bool IsDirectionReversed { get; set; } = false;

    private Border _trackBg;
    private Border _fillBar;
    private Border _thumb;
    private bool _scrubbing = false;

    public Slider() : base() {
        Focusable = true;
        Height = 26.0f;
        MinWidth = 100.0f;
    }
    public Slider(float min, float max, float val) : this() {
        Minimum = min;
        Maximum = max;
        Value = val;
    }

    public override string GetTypeName() { return "Slider"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; OnRangeUpdated(); }
    }
    public float TickFrequency {
        get { return _tickFrequency; }
        set { _tickFrequency = value >= 0.0f ? value : 0.0f; }
    }

    protected override void OnRangeUpdated() {
        if (_trackBg == null || _fillBar == null || _thumb == null) { return; }
        float frac = NormalizedFraction();
        if (IsDirectionReversed) { frac = 1.0f - frac; }
        float w = ActualWidth > 18.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 180.0f);
        float h = ActualHeight > 18.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 26.0f);
        if (_orientation == Orientation.Horizontal) {
            float usable = Math.Maxf(1.0f, w - 18.0f);
            float thumbX = usable * frac;
            _fillBar.Width = thumbX + 9.0f;
            UIElement.SetCanvasLeft(_thumb, thumbX);
            UIElement.SetCanvasTop(_thumb, Math.Maxf(0.0f, (h - 18.0f) * 0.5f));
        } else {
            float usable = Math.Maxf(1.0f, h - 18.0f);
            float thumbY = usable * (1.0f - frac);
            _fillBar.Height = Math.Maxf(4.0f, h - thumbY - 9.0f);
            UIElement.SetCanvasTop(_fillBar, thumbY + 9.0f);
            UIElement.SetCanvasLeft(_thumb, Math.Maxf(0.0f, (w - 18.0f) * 0.5f));
            UIElement.SetCanvasTop(_thumb, thumbY);
        }
    }

    private void UpdateFromPointer(float screenX, float screenY) {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        float lx = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 0);
        float ly = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 1);
        float w = Math.Maxf(24.0f, ActualWidth);
        float h = Math.Maxf(24.0f, ActualHeight);
        float frac = _orientation == Orientation.Horizontal
            ? Math.Clampf((lx - 9.0f) / Math.Maxf(1.0f, w - 18.0f), 0.0f, 1.0f)
            : 1.0f - Math.Clampf((ly - 9.0f) / Math.Maxf(1.0f, h - 18.0f), 0.0f, 1.0f);
        if (IsDirectionReversed) { frac = 1.0f - frac; }
        float raw = Minimum + frac * (Maximum - Minimum);
        if (IsSnapToTickEnabled && _tickFrequency > 0.0001f) {
            float steps = (raw - Minimum) / _tickFrequency;
            int snapped = (int)(steps + 0.5f);
            raw = Minimum + (float)snapped * _tickFrequency;
        }
        Value = raw;
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        _scrubbing = true;
        CaptureMouse();
        UpdateFromPointer(e.X, e.Y);
        e.Handled = true;
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (_scrubbing) { UpdateFromPointer(e.X, e.Y); }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (_scrubbing) {
            _scrubbing = false;
            ReleaseMouseCapture();
            e.Handled = true;
        }
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        if (e.Key == KeyCode.Left || e.Key == KeyCode.Down) { Value = Value - SmallChange; e.Handled = true; }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.Up) { Value = Value + SmallChange; e.Handled = true; }
        else if (e.Key == KeyCode.Home) { Value = Minimum; e.Handled = true; }
        else if (e.Key == KeyCode.End) { Value = Maximum; e.Handled = true; }
    }

    protected override UIElement CreateDefaultTemplate() {
        var root = new Canvas();
        _trackBg = new Border();
        _trackBg.Height = 6.0f;
        _trackBg.Background = new SolidColorBrush("#FF1E2633");
        _trackBg.CornerRadius = new CornerRadius(3.0f);
        _trackBg.HorizontalAlignment = HorizontalAlignment.Stretch;
        UIElement.SetCanvasLeft(_trackBg, 0.0f);
        UIElement.SetCanvasRight(_trackBg, 0.0f);
        UIElement.SetCanvasTop(_trackBg, 10.0f);

        _fillBar = new Border();
        _fillBar.Height = 6.0f;
        _fillBar.Background = new SolidColorBrush("#FF3B82F6");
        _fillBar.CornerRadius = new CornerRadius(3.0f);
        UIElement.SetCanvasLeft(_fillBar, 0.0f);
        UIElement.SetCanvasTop(_fillBar, 10.0f);

        _thumb = new Border();
        _thumb.Width = 18.0f;
        _thumb.Height = 18.0f;
        _thumb.CornerRadius = new CornerRadius(9.0f);
        _thumb.Background = Brushes.White();
        _thumb.BorderBrush = new SolidColorBrush("#FF3B82F6");
        _thumb.BorderThickness = new Thickness(2.0f);

        root.AddChild(_trackBg);
        root.AddChild(_fillBar);
        root.AddChild(_thumb);
        OnRangeUpdated();
        return root;
    }
}

public class ProgressBar : RangeBase {
    private bool _isIndeterminate = false;
    private Orientation _orientation = Orientation.Horizontal;
    private Border _track;
    private Border _indicator;

    public ProgressBar() : base() {
        Height = 10.0f;
        MinWidth = 100.0f;
        Foreground = new SolidColorBrush("#FF3B82F6");
        Background = new SolidColorBrush("#FF1B202B");
        CornerRadius = new CornerRadius(5.0f);
    }

    public override string GetTypeName() { return "ProgressBar"; }

    public bool IsIndeterminate {
        get { return _isIndeterminate; }
        set { _isIndeterminate = value; OnRangeUpdated(); }
    }
    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; OnRangeUpdated(); }
    }

    protected override void OnRangeUpdated() {
        if (_indicator == null) { return; }
        _indicator.Background = Foreground;
        float w = ActualWidth > 1.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 200.0f);
        float h = ActualHeight > 1.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 10.0f);
        float frac = _isIndeterminate ? 0.35f : NormalizedFraction();
        if (_orientation == Orientation.Horizontal) {
            _indicator.Width = w * frac;
            _indicator.Height = h;
        } else {
            _indicator.Width = w;
            _indicator.Height = h * frac;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _track = new Border();
        _track.Background = Background;
        _track.CornerRadius = CornerRadius;
        _track.ClipToBounds = true;
        _track.HorizontalAlignment = HorizontalAlignment.Stretch;
        _track.VerticalAlignment = VerticalAlignment.Stretch;

        _indicator = new Border();
        _indicator.Name = "PART_Indicator";
        _indicator.Background = Foreground;
        _indicator.CornerRadius = CornerRadius;
        _indicator.HorizontalAlignment = HorizontalAlignment.Left;
        _track.Child = _indicator;

        OnRangeUpdated();
        return _track;
    }
}

public class ScrollBar : RangeBase {
    private Orientation _orientation = Orientation.Vertical;
    private float _viewportSize = 100.0f;
    private Border _trackBg;
    private Thumb _thumb;

    public event ScrollEventHandler Scroll;

    public ScrollBar() : base() {
        SmallChange = 24.0f;
        LargeChange = 120.0f;
    }

    public override string GetTypeName() { return "ScrollBar"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; OnRangeUpdated(); }
    }
    public float ViewportSize {
        get { return _viewportSize; }
        set { _viewportSize = value > 0.0f ? value : 1.0f; OnRangeUpdated(); }
    }

    protected override void OnRangeUpdated() {
        if (_thumb == null) { return; }
        float span = Maximum - Minimum;
        float total = span + _viewportSize;
        float fracPos = NormalizedFraction();
        if (_orientation == Orientation.Vertical) {
            float trackH = Math.Maxf(24.0f, ActualHeight > 0.0f ? ActualHeight : 200.0f);
            float ratio = total > 0.0f ? Math.Clampf(_viewportSize / total, 0.12f, 1.0f) : 1.0f;
            float thumbH = Math.Maxf(24.0f, trackH * ratio);
            float travel = Math.Maxf(0.0f, trackH - thumbH);
            _thumb.Width = 8.0f;
            _thumb.Height = thumbH;
            UIElement.SetCanvasLeft(_thumb, 1.0f);
            UIElement.SetCanvasTop(_thumb, travel * fracPos);
        } else {
            float trackW = Math.Maxf(24.0f, ActualWidth > 0.0f ? ActualWidth : 200.0f);
            float ratio = total > 0.0f ? Math.Clampf(_viewportSize / total, 0.12f, 1.0f) : 1.0f;
            float thumbW = Math.Maxf(24.0f, trackW * ratio);
            float travel = Math.Maxf(0.0f, trackW - thumbW);
            _thumb.Width = thumbW;
            _thumb.Height = 8.0f;
            UIElement.SetCanvasLeft(_thumb, travel * fracPos);
            UIElement.SetCanvasTop(_thumb, 1.0f);
        }
    }

    protected override void OnValueChanged(float oldVal, float newVal) {
        base.OnValueChanged(oldVal, newVal);
        Scroll(this, new ScrollEventArgs(newVal, this));
    }

    protected override UIElement CreateDefaultTemplate() {
        var host = new Canvas();
        host.Width = _orientation == Orientation.Vertical ? 10.0f : ParseUtil.NaN();
        host.Height = _orientation == Orientation.Horizontal ? 10.0f : ParseUtil.NaN();

        _trackBg = new Border();
        _trackBg.Background = new SolidColorBrush("#331E2633");
        _trackBg.CornerRadius = new CornerRadius(4.0f);
        _trackBg.HorizontalAlignment = HorizontalAlignment.Stretch;
        _trackBg.VerticalAlignment = VerticalAlignment.Stretch;

        _thumb = new Thumb();
        _thumb.Background = new SolidColorBrush("#FF475569");
        _thumb.CornerRadius = new CornerRadius(4.0f);
        _thumb.DragDelta += (object s, DragDeltaEventArgs e) => {
            float span = Maximum - Minimum;
            if (span <= 0.001f) { return; }
            if (_orientation == Orientation.Vertical) {
                float trackH = Math.Maxf(24.0f, ActualHeight);
                float travel = Math.Maxf(1.0f, trackH - _thumb.Height);
                Value = Value + (e.VerticalChange / travel) * span;
            } else {
                float trackW = Math.Maxf(24.0f, ActualWidth);
                float travel = Math.Maxf(1.0f, trackW - _thumb.Width);
                Value = Value + (e.HorizontalChange / travel) * span;
            }
        };

        host.AddChild(_trackBg);
        host.AddChild(_thumb);
        OnRangeUpdated();
        return host;
    }
}

public class ScrollViewer : ContentControl {
    private float _offsetX = 0.0f;
    private float _offsetY = 0.0f;
    private ScrollBarVisibility _hVis = ScrollBarVisibility.Disabled;
    private ScrollBarVisibility _vVis = ScrollBarVisibility.Auto;

    private Border _viewport;
    private ScrollBar _vBar;
    private ScrollBar _hBar;
    private bool _syncingBars = false;

    public event ScrollEventHandler ScrollChanged;

    public ScrollViewer() : base() {
        ClipToBounds = true;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Top;
    }

    public override string GetTypeName() { return "ScrollViewer"; }

    public float HorizontalOffset {
        get { return _offsetX; }
        set { ScrollToHorizontalOffset(value); }
    }
    public float VerticalOffset {
        get { return _offsetY; }
        set { ScrollToVerticalOffset(value); }
    }
    public ScrollBarVisibility HorizontalScrollBarVisibility {
        get { return _hVis; }
        set { _hVis = value; SyncScrollBarVisibility(); }
    }
    public ScrollBarVisibility VerticalScrollBarVisibility {
        get { return _vVis; }
        set { _vVis = value; SyncScrollBarVisibility(); }
    }

    public float ExtentWidth { get { return _viewport != null ? _viewport.ScrollExtentWidth : ScrollExtentWidth; } }
    public float ExtentHeight { get { return _viewport != null ? _viewport.ScrollExtentHeight : ScrollExtentHeight; } }
    public float ViewportWidth { get { return _viewport != null ? _viewport.ActualWidth : ActualWidth; } }
    public float ViewportHeight { get { return _viewport != null ? _viewport.ActualHeight : ActualHeight; } }

    public void ScrollToVerticalOffset(float offset) {
        float maxY = Math.Maxf(0.0f, ExtentHeight - ViewportHeight);
        float next = Math.Clampf(offset, 0.0f, maxY > 0.0f ? maxY : Math.Maxf(0.0f, offset));
        _offsetY = next;
        ApplyScrollOffsets();
    }

    public void ScrollToHorizontalOffset(float offset) {
        float maxX = Math.Maxf(0.0f, ExtentWidth - ViewportWidth);
        float next = Math.Clampf(offset, 0.0f, maxX > 0.0f ? maxX : Math.Maxf(0.0f, offset));
        _offsetX = next;
        ApplyScrollOffsets();
    }

    public void ScrollToHome() { _offsetX = 0.0f; _offsetY = 0.0f; ApplyScrollOffsets(); }
    public void ScrollToEnd() { ScrollToVerticalOffset(ExtentHeight); }

    private void ApplyScrollOffsets() {
        if (_viewport != null && ParseUtil.IsValidHandle(_viewport.__nativeHandle)) {
            Visual_SetScrollOffsets(_viewport.__nativeHandle, _offsetX, _offsetY);
        }
        SyncScrollBarRanges();
        ScrollChanged(this, new ScrollEventArgs(_offsetY, this));
    }

    private void SyncScrollBarRanges() {
        if (_syncingBars) { return; }
        _syncingBars = true;
        if (_vBar != null) {
            float maxY = Math.Maxf(0.0f, ExtentHeight - ViewportHeight);
            _vBar.Maximum = maxY;
            _vBar.ViewportSize = Math.Maxf(16.0f, ViewportHeight);
            _vBar.Value = _offsetY;
        }
        if (_hBar != null) {
            float maxX = Math.Maxf(0.0f, ExtentWidth - ViewportWidth);
            _hBar.Maximum = maxX;
            _hBar.ViewportSize = Math.Maxf(16.0f, ViewportWidth);
            _hBar.Value = _offsetX;
        }
        SyncScrollBarVisibility();
        _syncingBars = false;
    }

    private void SyncScrollBarVisibility() {
        if (_vBar != null) {
            bool showV = _vVis == ScrollBarVisibility.Visible || (_vVis == ScrollBarVisibility.Auto && ExtentHeight > ViewportHeight + 1.0f);
            _vBar.Visibility = showV ? Visibility.Visible : Visibility.Collapsed;
        }
        if (_hBar != null) {
            bool showH = _hVis == ScrollBarVisibility.Visible || (_hVis == ScrollBarVisibility.Auto && ExtentWidth > ViewportWidth + 1.0f);
            _hBar.Visibility = showH ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    public override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        if (e.Handled) { return; }
        if (_vVis != ScrollBarVisibility.Disabled && e.DeltaY != 0.0f) {
            ScrollToVerticalOffset(_offsetY - e.DeltaY * 42.0f);
            e.Handled = true;
        } else if (_hVis != ScrollBarVisibility.Disabled && e.DeltaX != 0.0f) {
            ScrollToHorizontalOffset(_offsetX - e.DeltaX * 42.0f);
            e.Handled = true;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        var grid = new Grid();
        grid.AddColumnDefinition(GridUnitType.Star, 1.0f);
        grid.AddColumnDefinition(GridUnitType.Auto, 1.0f);
        grid.AddRowDefinition(GridUnitType.Star, 1.0f);
        grid.AddRowDefinition(GridUnitType.Auto, 1.0f);

        _viewport = new Border();
        _viewport.ClipToBounds = true;
        if (ParseUtil.IsValidHandle(_viewport.__nativeHandle)) {
            Visual_SetLayoutMode(_viewport.__nativeHandle, EnumConvert.ToInt(LayoutMode.ScrollViewport), 0.0f, 0.0f, 0, 0);
        }
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        _viewport.Child = _presenter;

        _vBar = new ScrollBar();
        _vBar.Orientation = Orientation.Vertical;
        _vBar.Width = 10.0f;
        UIElement.SetGridColumn(_vBar, 1);
        _vBar.Scroll += (object s, ScrollEventArgs e) => {
            if (!_syncingBars) { ScrollToVerticalOffset(e.NewValue); }
        };

        _hBar = new ScrollBar();
        _hBar.Orientation = Orientation.Horizontal;
        _hBar.Height = 10.0f;
        UIElement.SetGridRow(_hBar, 1);
        _hBar.Scroll += (object s, ScrollEventArgs e) => {
            if (!_syncingBars) { ScrollToHorizontalOffset(e.NewValue); }
        };

        grid.AddChild(_viewport);
        grid.AddChild(_vBar);
        grid.AddChild(_hBar);
        SyncScrollBarVisibility();
        return grid;
    }
}
