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
        HorizontalContentAlignment = HorizontalAlignment.Left;
        VerticalContentAlignment = VerticalAlignment.Center;
    }

    public override string GetTypeName() { return "ButtonBase"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

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

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        if (_command is RelayCommand && _canExecHook != null) {
            (_command as RelayCommand).UnsubscribeCanExecuteChanged(_canExecHook);
        }
        _command = null;
        _canExecHook = null;
        _commandParamBox = null;
    }

    protected void SyncCommandCanExecute() {
        if (_command != null) {
            IsEnabled = CommandTriggerHelper.CanExecuteBox(_command, _commandParamBox);
        }
    }

    protected virtual void OnClick() {
        var clickArgs = new RoutedEventArgs("Click", this);
        Click(this, clickArgs);
        RaiseRoutedEvent("Click", clickArgs);
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

    protected override void UpdateCommonVisualStates(bool useTransitions) {
        if (!IsEnabled) { VisualStateManager.GoToState(this, "Disabled", useTransitions); }
        else if (IsPressed) { VisualStateManager.GoToState(this, "Pressed", useTransitions); }
        else if (IsMouseOver) { VisualStateManager.GoToState(this, "MouseOver", useTransitions); }
        else { VisualStateManager.GoToState(this, "Normal", useTransitions); }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Command" && value is ICommand) { Command = value as ICommand; return true; }
        if (propName == "CommandParameter") { CommandParameterBox = value; return true; }
        if (propName == "ClickMode") {
            if (value is BoxedInt) {
                int cm = (value as BoxedInt).Value;
                ClickMode = cm == 1 ? ClickMode.Press : (cm == 2 ? ClickMode.Hover : ClickMode.Release);
                return true;
            }
            string s = DependencyObject.UnboxString(value, "Release");
            if (s == "Press") { ClickMode = ClickMode.Press; return true; }
            if (s == "Hover") { ClickMode = ClickMode.Hover; return true; }
            ClickMode = ClickMode.Release;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }
}

public class Button : ButtonBase {
    public bool IsDefault { get; set; }
    public bool IsCancel { get; set; }
    private bool _usingDefaultTemplate = false;

    public Button() : base() {
        HorizontalContentAlignment = HorizontalAlignment.Center;
        Background = new SolidColorBrush("#FF242B38");
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
        base.OnVisualStatePropertyChanged();
        if (!_usingDefaultTemplate || Style != null || _appliedStyle != null || Template != null) { return; }
        if (!HasLocalProperty("Opacity")) {
            Opacity = IsEnabled ? 1.0f : 0.45f;
        }
        if (IsEnabled && !HasLocalProperty("Background")) {
            if (IsPressed) {
                Background = new SolidColorBrush("#FF1C222D");
            } else if (IsMouseOver) {
                Background = new SolidColorBrush("#FF303A4D");
                if (!HasLocalProperty("BorderBrush")) { BorderBrush = new SolidColorBrush("#FF3B82F6"); }
            } else {
                Background = new SolidColorBrush("#FF242B38");
                if (!HasLocalProperty("BorderBrush")) { BorderBrush = new SolidColorBrush("#FF3B465C"); }
            }
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _usingDefaultTemplate = true;
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        return _presenter;
    }
}

public class RepeatButton : Button {
    public int Delay { get; set; } = 250;
    public int Interval { get; set; } = 33;
    private string _routedCommandName = "";
    private float _repeatElapsedMs = 0.0f;
    private bool _repeatStarted = false;
    private static RepeatButton[] _activeRepeats;
    private static bool _repeatHookInstalled = false;

    public RepeatButton() : base() {
        ClickMode = ClickMode.Press;
    }
    public override string GetTypeName() { return "RepeatButton"; }

    private bool IsRepeatEngaged() {
        if (!IsEnabled || _isDisposedTree || Visibility != Visibility.Visible) { return false; }
        if (ClickMode == ClickMode.Hover) { return IsMouseOver; }
        return IsPressed;
    }

    private static void EnsureRepeatHook() {
        if (_repeatHookInstalled) { return; }
        _repeatHookInstalled = true;
        _activeRepeats = new RepeatButton[0];
        BindingEngine.AddFrameTickHook((float dt) => { RepeatButton.StepActiveRepeats(dt); });
    }

    private void StartRepeatTracking() {
        EnsureRepeatHook();
        _repeatElapsedMs = 0.0f;
        _repeatStarted = false;
        bool exists = false;
        for (int i = 0; i < _activeRepeats.Length; i += 1) {
            if (_activeRepeats[i] == this) { exists = true; break; }
        }
        if (!exists) { _activeRepeats.Push(this); }
        if (ParseUtil.IsNativeReady()) {
            int firstWait = Delay > 16 ? Delay : 16;
            Host_RequestWakeAfterMs(firstWait);
        }
    }

    public static void StepActiveRepeats(float dtSeconds) {
        if (_activeRepeats == null || _activeRepeats.Length == 0) { return; }
        float dtMs = dtSeconds * 1000.0f;
        if (dtMs <= 0.0f) { dtMs = 16.0f; }
        int minNextWakeMs = 0;
        for (int i = _activeRepeats.Length - 1; i >= 0; i -= 1) {
            RepeatButton rb = _activeRepeats[i];
            if (rb == null || !rb.IsRepeatEngaged()) {
                if (rb != null) {
                    rb._repeatElapsedMs = 0.0f;
                    rb._repeatStarted = false;
                }
                _activeRepeats.RemoveAt(i);
                continue;
            }
            rb._repeatElapsedMs += dtMs;
            int effDelay = rb.Delay > 16 ? rb.Delay : 16;
            int effInterval = rb.Interval > 10 ? rb.Interval : 10;
            if (!rb._repeatStarted) {
                if (rb._repeatElapsedMs >= (float)effDelay) {
                    rb._repeatStarted = true;
                    rb._repeatElapsedMs -= (float)effDelay;
                    rb.OnClick();
                }
                int rem = effDelay - (int)rb._repeatElapsedMs;
                if (rem < 16) { rem = 16; }
                if (minNextWakeMs == 0 || rem < minNextWakeMs) { minNextWakeMs = rem; }
            } else {
                int guard = 0;
                while (rb._repeatElapsedMs >= (float)effInterval && guard < 4) {
                    rb._repeatElapsedMs -= (float)effInterval;
                    rb.OnClick();
                    guard += 1;
                }
                if (rb._repeatElapsedMs >= (float)effInterval) { rb._repeatElapsedMs = 0.0f; }
                int rem = effInterval - (int)rb._repeatElapsedMs;
                if (rem < 16) { rem = 16; }
                if (minNextWakeMs == 0 || rem < minNextWakeMs) { minNextWakeMs = rem; }
            }
        }
        if (minNextWakeMs > 0 && ParseUtil.IsNativeReady()) {
            Host_RequestWakeAfterMs(minNextWakeMs);
        }
    }

    public override void OnMouseEnter(MouseEventArgs e) {
        base.OnMouseEnter(e);
        if (ClickMode == ClickMode.Hover && IsEnabled) {
            StartRepeatTracking();
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (ClickMode != ClickMode.Hover && IsEnabled && IsPressed) {
            StartRepeatTracking();
        }
    }

    public void TickRepeat() {
        if (IsRepeatEngaged()) { OnClick(); }
    }

    private ScrollBar FindAncestorScrollBar() {
        UIElement cur = TemplatedParent != null ? TemplatedParent : Parent;
        while (cur != null) {
            if (cur is ScrollBar) { return cur as ScrollBar; }
            cur = cur.Parent != null ? cur.Parent : cur.TemplatedParent;
        }
        return null;
    }

    private ScrollViewer FindAncestorScrollViewer() {
        UIElement cur = TemplatedParent != null ? TemplatedParent : Parent;
        while (cur != null) {
            if (cur is ScrollViewer) { return cur as ScrollViewer; }
            cur = cur.Parent != null ? cur.Parent : cur.TemplatedParent;
        }
        return null;
    }

    private Slider FindAncestorSlider() {
        UIElement cur = TemplatedParent != null ? TemplatedParent : Parent;
        while (cur != null) {
            if (cur is Slider) { return cur as Slider; }
            cur = cur.Parent != null ? cur.Parent : cur.TemplatedParent;
        }
        return null;
    }

    protected override void OnClick() {
        base.OnClick();
        if (_routedCommandName.Length == 0) { return; }
        if (_routedCommandName == "Slider.DecreaseLarge" || _routedCommandName == "Slider.IncreaseLarge") {
            Slider sl = FindAncestorSlider();
            if (sl != null && !sl.IsMoveToPointEnabled) {
                float step = sl.LargeChange > 0.0f ? sl.LargeChange : 1.0f;
                float nextVal = _routedCommandName == "Slider.DecreaseLarge" ? (sl.Value - step) : (sl.Value + step);
                sl.Value = sl.SnapValueToTicks(nextVal);
            }
            return;
        }
        ScrollBar sb = FindAncestorScrollBar();
        if (sb != null) {
            float small = sb.SmallChange > 0.0f ? sb.SmallChange : 16.0f;
            float large = sb.ViewportSize > 0.0f ? sb.ViewportSize : (sb.LargeChange > 0.0f ? sb.LargeChange : 80.0f);
            if (_routedCommandName == "ScrollBar.LineUpCommand" || _routedCommandName == "ScrollBar.LineLeftCommand") {
                sb.Value = sb.Value - small;
            } else if (_routedCommandName == "ScrollBar.LineDownCommand" || _routedCommandName == "ScrollBar.LineRightCommand") {
                sb.Value = sb.Value + small;
            } else if (_routedCommandName == "ScrollBar.PageUpCommand" || _routedCommandName == "ScrollBar.PageLeftCommand") {
                sb.Value = sb.Value - large;
            } else if (_routedCommandName == "ScrollBar.PageDownCommand" || _routedCommandName == "ScrollBar.PageRightCommand") {
                sb.Value = sb.Value + large;
            }
            return;
        }
        ScrollViewer sv = FindAncestorScrollViewer();
        if (sv != null) {
            float hPage = sv.ViewportWidth > 0.0f ? sv.ViewportWidth : 80.0f;
            float vPage = sv.ViewportHeight > 0.0f ? sv.ViewportHeight : 80.0f;
            if (_routedCommandName == "ScrollBar.LineLeftCommand") {
                sv.ScrollToHorizontalOffset(sv.HorizontalOffset - 16.0f);
            } else if (_routedCommandName == "ScrollBar.LineRightCommand") {
                sv.ScrollToHorizontalOffset(sv.HorizontalOffset + 16.0f);
            } else if (_routedCommandName == "ScrollBar.LineUpCommand") {
                sv.ScrollToVerticalOffset(sv.VerticalOffset - 16.0f);
            } else if (_routedCommandName == "ScrollBar.LineDownCommand") {
                sv.ScrollToVerticalOffset(sv.VerticalOffset + 16.0f);
            } else if (_routedCommandName == "ScrollBar.PageLeftCommand") {
                sv.ScrollToHorizontalOffset(sv.HorizontalOffset - hPage);
            } else if (_routedCommandName == "ScrollBar.PageRightCommand") {
                sv.ScrollToHorizontalOffset(sv.HorizontalOffset + hPage);
            } else if (_routedCommandName == "ScrollBar.PageUpCommand") {
                sv.ScrollToVerticalOffset(sv.VerticalOffset - vPage);
            } else if (_routedCommandName == "ScrollBar.PageDownCommand") {
                sv.ScrollToVerticalOffset(sv.VerticalOffset + vPage);
            }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Delay") { Delay = DependencyObject.UnboxInt(value, 250); return true; }
        if (propName == "Interval") { Interval = DependencyObject.UnboxInt(value, 33); return true; }
        if (propName == "Command" && !(value is ICommand)) {
            _routedCommandName = DependencyObject.UnboxString(value, "");
            return true;
        }
        return base.SetPropertyValue(propName, value);
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
        base.OnVisualStatePropertyChanged();
        if (Style == null && Template == null) {
            Foreground = new SolidColorBrush(IsMouseOver ? "#FF93C5FD" : "#FF60A5FA");
        }
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
    internal override int FastPropEpochCategory() { return 3; }
    private bool _isChecked = false;
    private bool _isIndeterminate = false;
    public bool IsThreeState { get; set; } = false;

    public event RoutedEventHandler Checked;
    public event RoutedEventHandler Unchecked;
    public event RoutedEventHandler Indeterminate;

    public ToggleButton() : base() {
        Padding = new Thickness(12.0f, 6.0f, 12.0f, 6.0f);
    }

    protected override UIElement CreateDefaultTemplate() {
        if (Style == null && _appliedStyle == null && Template == null) {
            if (!HasLocalProperty("Background") && Background == null) {
                Background = new SolidColorBrush(_isIndeterminate ? "#FF334155" : (_isChecked ? "#FF3B82F6" : "#FF242B38"));
            }
            if (!HasLocalProperty("BorderBrush") && BorderBrush == null) {
                BorderBrush = new SolidColorBrush(_isIndeterminate ? "#FF94A3B8" : (_isChecked ? "#FF60A5FA" : "#FF3B465C"));
            }
            if (!HasLocalProperty("BorderThickness") &&
                BorderThickness.Left == 0.0f && BorderThickness.Top == 0.0f &&
                BorderThickness.Right == 0.0f && BorderThickness.Bottom == 0.0f) {
                BorderThickness = new Thickness(1.0f);
            }
            CornerRadius curCr = this.CornerRadius;
            if (!HasLocalProperty("CornerRadius") &&
                (curCr == null ||
                 (curCr.TopLeft == 0.0f && curCr.TopRight == 0.0f &&
                  curCr.BottomRight == 0.0f && curCr.BottomLeft == 0.0f))) {
                CornerRadius = new CornerRadius(6.0f);
            }
        }
        return base.CreateDefaultTemplate();
    }

    public override string GetTypeName() { return "ToggleButton"; }

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        return base.SubscribeBehaviorRoutedEvent(ev, sink);
    }

    public bool IsChecked {
        get { return _isChecked; }
        set {
            if (_isChecked == value && !_isIndeterminate) { return; }
            _isChecked = value;
            _isIndeterminate = false;
            OnCheckedChanged();
            BindingEngine.NotifyTargetPropertyChanged(this, "IsChecked", DependencyObject.BoxBool(_isChecked));
            BindingEngine.MarkFlushDirty();
        }
    }

    public bool IsIndeterminate {
        get { return _isIndeterminate; }
        set {
            if (_isIndeterminate == value) { return; }
            _isIndeterminate = value;
            if (value) {
                _isChecked = false;
            }
            UpdateVisualCheckedState();
            UpdateTriggersAndStates();
            if (value) {
                var indArgs = new RoutedEventArgs("Indeterminate", this);
                Indeterminate(this, indArgs);
                RaiseRoutedEvent("Indeterminate", indArgs);
            }
            BindingEngine.NotifyTargetPropertyChanged(this, "IsChecked", _isIndeterminate ? null : DependencyObject.BoxBool(_isChecked));
            BindingEngine.MarkFlushDirty();
        }
    }

    protected virtual void OnCheckedChanged() {
        UpdateVisualCheckedState();
        UpdateTriggersAndStates();
        if (_isChecked) {
            var chkArgs = new RoutedEventArgs("Checked", this);
            Checked(this, chkArgs);
            RaiseRoutedEvent("Checked", chkArgs);
        } else {
            var unchkArgs = new RoutedEventArgs("Unchecked", this);
            Unchecked(this, unchkArgs);
            RaiseRoutedEvent("Unchecked", unchkArgs);
        }
    }

    protected override void UpdateVisualStates(bool useTransitions) {
        base.UpdateVisualStates(useTransitions);
        if (_isIndeterminate) {
            VisualStateManager.GoToState(this, "Indeterminate", useTransitions);
        } else {
            VisualStateManager.GoToState(this, _isChecked ? "Checked" : "Unchecked", useTransitions);
        }
    }

    protected virtual void UpdateVisualCheckedState() {
        if (Style == null && _appliedStyle == null && Template == null && IsTemplateApplied()) {
            if (_isIndeterminate) {
                if (!HasLocalProperty("Background")) { Background = new SolidColorBrush("#FF334155"); }
                if (!HasLocalProperty("BorderBrush")) { BorderBrush = new SolidColorBrush("#FF94A3B8"); }
            } else {
                if (!HasLocalProperty("Background")) {
                    Background = new SolidColorBrush(_isChecked ? "#FF3B82F6" : "#FF242B38");
                }
                if (!HasLocalProperty("BorderBrush")) {
                    BorderBrush = new SolidColorBrush(_isChecked ? "#FF60A5FA" : "#FF3B465C");
                }
            }
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
        if (propName == "IsChecked") {
            if (value == null) {
                IsIndeterminate = true;
                return true;
            }
            string sVal = (value is BoxedString) ? (value as BoxedString).Value.Trim() : "";
            if (sVal == "{x:Null}" || sVal == "null") {
                IsIndeterminate = true;
                return true;
            }
            IsChecked = DependencyObject.UnboxBool(value, false);
            return true;
        }
        if (propName == "IsThreeState") { IsThreeState = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override bool IsExplicitNullProperty(string propName) {
        return propName == "IsChecked" && _isIndeterminate;
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsChecked") {
            if (_isIndeterminate) { return null; }
            return DependencyObject.BoxBool(_isChecked);
        }
        if (propName == "IsThreeState") { return DependencyObject.BoxBool(IsThreeState); }
        return base.GetPropertyValue(propName);
    }
}

public class CheckBox : ToggleButton {
    private Border _box;
    private Path _checkMark;

    public CheckBox() : base() {
        Background = Brushes.Transparent();
        BorderThickness = new Thickness(0.0f);
        Padding = new Thickness(0.0f);
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
            string bg = active
                ? (IsMouseOver ? "#FF1C92E0" : "#FF1382CE")
                : (IsMouseOver ? "#FF5D6369" : "#FF4E5358");
            _box.Background = new SolidColorBrush(bg);
            _box.BorderBrush = Brushes.Transparent();
        }
        if (_checkMark != null) {
            if (IsIndeterminate) {
                _checkMark.Data = "M 3.5,8.5 H 13.5";
                _checkMark.Visibility = Visibility.Visible;
            } else if (IsChecked) {
                _checkMark.Data = "M 3.2,8.8 L 6.8,12.5 L 13.8,4.8";
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
        _box.Width = 17.0f;
        _box.Height = 17.0f;
        _box.CornerRadius = new CornerRadius(2.0f);
        _box.BorderThickness = new Thickness(0.0f);
        _box.VerticalAlignment = VerticalAlignment.Center;

        _checkMark = new Path("M 3.2,8.8 L 6.8,12.5 L 13.8,4.8");
        _checkMark.Width = 17.0f;
        _checkMark.Height = 17.0f;
        _checkMark.Stroke = new SolidColorBrush("#FF181C20");
        _checkMark.StrokeThickness = 2.1f;
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
    private static bool _disposeHookInstalled = false;

    private string _groupName = "";
    private Border _ring;
    private Ellipse _dot;

    public RadioButton() : base() {
        Background = Brushes.Transparent();
        BorderThickness = new Thickness(0.0f);
        Padding = new Thickness(0.0f);
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
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { RadioButton.CleanupDisposed(); });
        }
        for (int i = 0; i < _instances.Length; i += 1) {
            if (_instances[i] == rb) { return; }
        }
        _instances.Push(rb);
    }

    internal static void CleanupDisposed() {
        if (_instances == null) { return; }
        for (int i = _instances.Length - 1; i >= 0; i -= 1) {
            if (_instances[i] == null || _instances[i]._isDisposedTree) {
                _instances.RemoveAt(i);
            }
        }
    }

    private void UncheckGroupPeers() {
        if (_instances == null) { return; }
        UIElement myRoot = RootElement();
        for (int i = _instances.Length - 1; i >= 0; i -= 1) {
            RadioButton other = _instances[i];
            if (other == null || other._isDisposedTree) {
                _instances.RemoveAt(i);
                continue;
            }
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
            _ring.BorderBrush = Brushes.Transparent();
            string bg = IsChecked
                ? (IsMouseOver ? "#FF1C92E0" : "#FF1382CE")
                : (IsMouseOver ? "#FF5D6369" : "#FF4E5358");
            _ring.Background = new SolidColorBrush(bg);
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
        _ring.Width = 17.0f;
        _ring.Height = 17.0f;
        _ring.CornerRadius = new CornerRadius(8.5f);
        _ring.BorderThickness = new Thickness(0.0f);
        _ring.VerticalAlignment = VerticalAlignment.Center;

        _dot = new Ellipse();
        _dot.Width = 7.0f;
        _dot.Height = 7.0f;
        _dot.Fill = new SolidColorBrush("#FF181C20");
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
    }

    public override string GetTypeName() { return "Thumb"; }
    public bool IsDragging { get { return _isDragging; } }

    protected override UIElement CreateDefaultTemplate() {
        var b = new Border();
        CornerRadius cr = CornerRadius;
        b.Background = Background != null ? Background : new SolidColorBrush("#FF475569");
        b.CornerRadius = cr.TopLeft > 0.0f ? cr : new CornerRadius(4.0f);
        return b;
    }

    private Point ToTrackPoint(float screenX, float screenY) {
        if (Parent != null && ParseUtil.IsValidHandle(Parent.__nativeHandle)) {
            return new Point(
                Visual_ScreenToLocal(Parent.__nativeHandle, screenX, screenY, 0),
                Visual_ScreenToLocal(Parent.__nativeHandle, screenX, screenY, 1));
        }
        return new Point(screenX, screenY);
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        Point pt = ToTrackPoint(e.X, e.Y);
        _isDragging = true;
        _startX = pt.X;
        _startY = pt.Y;
        _prevX = pt.X;
        _prevY = pt.Y;
        CaptureMouse();
        UpdateTriggersAndStates();
        e.Handled = true;
        DragStarted(this, new DragStartedEventArgs(0.0f, 0.0f, this));
    }

    public override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!_isDragging) { return; }
        Point pt = ToTrackPoint(e.X, e.Y);
        float dx = pt.X - _prevX;
        float dy = pt.Y - _prevY;
        _prevX = pt.X;
        _prevY = pt.Y;
        if (dx != 0.0f || dy != 0.0f) {
            DragDelta(this, new DragDeltaEventArgs(dx, dy, this));
        }
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonUp(e);
        if (!_isDragging) { return; }
        Point pt = ToTrackPoint(e.X, e.Y);
        _isDragging = false;
        ReleaseMouseCapture();
        UpdateTriggersAndStates();
        e.Handled = true;
        DragCompleted(this, new DragCompletedEventArgs(pt.X - _startX, pt.Y - _startY, false, this));
    }

    protected override void UpdateVisualStates(bool useTransitions) {
        if (!IsEnabled) { VisualStateManager.GoToState(this, "Disabled", useTransitions); }
        else if (IsPressed || _isDragging) { VisualStateManager.GoToState(this, "Pressed", useTransitions); }
        else if (IsMouseOver) { VisualStateManager.GoToState(this, "MouseOver", useTransitions); }
        else { VisualStateManager.GoToState(this, "Normal", useTransitions); }
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "IsDragging") { return DependencyObject.BoxBool(_isDragging); }
        return base.GetPropertyValue(propName);
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

public class Track : FrameworkElement {
    internal override int FastPropEpochCategory() { return 4; }
    private Orientation _orientation = Orientation.Horizontal;
    private float _min = 0.0f;
    private float _max = 100.0f;
    private float _value = 0.0f;
    private float _viewportSize = 0.0f;
    private bool _isDirectionReversed = false;
    private UIElement _decreaseRepeatButton;
    private UIElement _increaseRepeatButton;
    private Thumb _thumb;

    public Track() : base() {
        if (ParseUtil.IsValidHandle(__nativeHandle)) {
            Visual_SetLayoutMode(__nativeHandle, EnumConvert.ToInt(LayoutMode.Grid), 0.0f, 0.0f, 0, 0);
        }
        UpdateTrackVisuals();
    }
    public override string GetTypeName() { return "Track"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public float Minimum {
        get { return _min; }
        set { _min = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public float Maximum {
        get { return _max; }
        set { _max = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public float Value {
        get { return _value; }
        set { _value = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public float ViewportSize {
        get { return _viewportSize; }
        set { _viewportSize = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public bool IsDirectionReversed {
        get { return _isDirectionReversed; }
        set { _isDirectionReversed = value; BumpPropEpoch(); UpdateTrackVisuals(); }
    }
    public UIElement DecreaseRepeatButton {
        get { return _decreaseRepeatButton; }
        set {
            if (_decreaseRepeatButton != null) { RemoveChild(_decreaseRepeatButton); }
            _decreaseRepeatButton = value;
            if (value != null) {
                value.HorizontalAlignment = HorizontalAlignment.Stretch;
                value.VerticalAlignment = VerticalAlignment.Stretch;
                AddChild(value);
            }
            UpdateTrackVisuals();
        }
    }
    public UIElement IncreaseRepeatButton {
        get { return _increaseRepeatButton; }
        set {
            if (_increaseRepeatButton != null) { RemoveChild(_increaseRepeatButton); }
            _increaseRepeatButton = value;
            if (value != null) {
                value.HorizontalAlignment = HorizontalAlignment.Stretch;
                value.VerticalAlignment = VerticalAlignment.Stretch;
                AddChild(value);
            }
            UpdateTrackVisuals();
        }
    }
    public Thumb Thumb {
        get { return _thumb; }
        set {
            if (_thumb != null) { RemoveChild(_thumb); }
            _thumb = value;
            if (value != null) {
                if (ParseUtil.IsNaN(value.Width)) { value.HorizontalAlignment = HorizontalAlignment.Stretch; }
                if (ParseUtil.IsNaN(value.Height)) { value.VerticalAlignment = VerticalAlignment.Stretch; }
                AddChild(value);
            }
            UpdateTrackVisuals();
        }
    }

    protected override void OnLoaded() {
        base.OnLoaded();
        UpdateTrackVisuals();
    }

    private void UpdateTrackVisuals() {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        float span = Math.Maxf(0.0f, _max - _min);
        float frac = span > 0.00001f ? Math.Clampf((_value - _min) / span, 0.0f, 1.0f) : 0.0f;
        float beforeFrac = (_orientation == Orientation.Vertical)
            ? (_isDirectionReversed ? frac : (1.0f - frac))
            : (_isDirectionReversed ? (1.0f - frac) : frac);

        Visual_GridClearDefs(__nativeHandle);
        int starUnit = EnumConvert.ToInt(GridUnitType.Star);
        int autoUnit = EnumConvert.ToInt(GridUnitType.Auto);
        int pxUnit = EnumConvert.ToInt(GridUnitType.Pixel);

        bool swapButtons = (_orientation == Orientation.Vertical) ? !_isDirectionReversed : _isDirectionReversed;
        UIElement firstBtn = swapButtons ? _increaseRepeatButton : _decreaseRepeatButton;
        UIElement secondBtn = swapButtons ? _decreaseRepeatButton : _increaseRepeatButton;

        if (_viewportSize <= 0.0f || ParseUtil.IsNaN(_viewportSize)) {
            float w0 = beforeFrac;
            float w2 = 1.0f - beforeFrac;
            if (firstBtn != null) {
                firstBtn.Visibility = w0 > 0.0001f ? Visibility.Visible : Visibility.Collapsed;
            }
            if (secondBtn != null) {
                secondBtn.Visibility = w2 > 0.0001f ? Visibility.Visible : Visibility.Collapsed;
            }
            if (_orientation == Orientation.Vertical) {
                Visual_GridAddRowDef(__nativeHandle, w0 > 0.0001f ? starUnit : pxUnit, w0 > 0.0001f ? w0 : 0.0f);
                Visual_GridAddRowDef(__nativeHandle, autoUnit, 1.0f);
                Visual_GridAddRowDef(__nativeHandle, w2 > 0.0001f ? starUnit : pxUnit, w2 > 0.0001f ? w2 : 0.0f);
                if (firstBtn != null) {
                    UIElement.SetGridRow(firstBtn, 0);
                    UIElement.SetGridColumn(firstBtn, 0);
                }
                if (_thumb != null) {
                    UIElement.SetGridRow(_thumb, 1);
                    UIElement.SetGridColumn(_thumb, 0);
                }
                if (secondBtn != null) {
                    UIElement.SetGridRow(secondBtn, 2);
                    UIElement.SetGridColumn(secondBtn, 0);
                }
            } else {
                Visual_GridAddColDef(__nativeHandle, w0 > 0.0001f ? starUnit : pxUnit, w0 > 0.0001f ? w0 : 0.0f);
                Visual_GridAddColDef(__nativeHandle, autoUnit, 1.0f);
                Visual_GridAddColDef(__nativeHandle, w2 > 0.0001f ? starUnit : pxUnit, w2 > 0.0001f ? w2 : 0.0f);
                if (firstBtn != null) {
                    UIElement.SetGridRow(firstBtn, 0);
                    UIElement.SetGridColumn(firstBtn, 0);
                }
                if (_thumb != null) {
                    UIElement.SetGridRow(_thumb, 0);
                    UIElement.SetGridColumn(_thumb, 1);
                }
                if (secondBtn != null) {
                    UIElement.SetGridRow(secondBtn, 0);
                    UIElement.SetGridColumn(secondBtn, 2);
                }
            }
            return;
        }

        if (firstBtn != null) { firstBtn.Visibility = Visibility.Visible; }
        if (secondBtn != null) { secondBtn.Visibility = Visibility.Visible; }
        float thumbRatio = (span > 0.00001f) ? Math.Clampf(_viewportSize / (span + _viewportSize), 0.08f, 1.0f) : 1.0f;
        float freeRatio = Math.Maxf(0.0f, 1.0f - thumbRatio);
        float sv0 = freeRatio * beforeFrac;
        float sv1 = thumbRatio;
        float sv2 = freeRatio * (1.0f - beforeFrac);
        if (_orientation == Orientation.Vertical) {
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv0);
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv1);
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv2);
            if (firstBtn != null) {
                UIElement.SetGridRow(firstBtn, 0);
                UIElement.SetGridColumn(firstBtn, 0);
            }
            if (_thumb != null) {
                UIElement.SetGridRow(_thumb, 1);
                UIElement.SetGridColumn(_thumb, 0);
            }
            if (secondBtn != null) {
                UIElement.SetGridRow(secondBtn, 2);
                UIElement.SetGridColumn(secondBtn, 0);
            }
        } else {
            Visual_GridAddColDef(__nativeHandle, starUnit, sv0);
            Visual_GridAddColDef(__nativeHandle, starUnit, sv1);
            Visual_GridAddColDef(__nativeHandle, starUnit, sv2);
            if (firstBtn != null) {
                UIElement.SetGridRow(firstBtn, 0);
                UIElement.SetGridColumn(firstBtn, 0);
            }
            if (_thumb != null) {
                UIElement.SetGridRow(_thumb, 0);
                UIElement.SetGridColumn(_thumb, 1);
            }
            if (secondBtn != null) {
                UIElement.SetGridRow(secondBtn, 0);
                UIElement.SetGridColumn(secondBtn, 2);
            }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 100.0f); return true; }
        if (propName == "Value") { Value = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "ViewportSize") { ViewportSize = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "IsDirectionReversed") { IsDirectionReversed = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Orientation") {
            string ov = DependencyObject.UnboxString(value, "");
            Orientation = (ov == "Vertical" || ov == "1") ? Orientation.Vertical : Orientation.Horizontal;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Minimum") { return DependencyObject.BoxFloat(_min); }
        if (propName == "Maximum") { return DependencyObject.BoxFloat(_max); }
        if (propName == "Value") { return DependencyObject.BoxFloat(_value); }
        if (propName == "ViewportSize") { return DependencyObject.BoxFloat(_viewportSize); }
        if (propName == "IsDirectionReversed") { return DependencyObject.BoxBool(_isDirectionReversed); }
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        return base.GetPropertyValue(propName);
    }
}

public class TickBar : CustomDrawElement {
    private Brush _fill;
    private string _placement = "Top";
    private float _min = 0.0f;
    private float _max = 10.0f;
    private float _tickFreq = 1.0f;
    private float _reservedSpace = 0.0f;
    private bool _isDirRev = false;
    private float[] _ticks;
    private string _ticksSpec = "";

    internal static float[] ParseTicksString(string raw) {
        var list = new float[0];
        if (raw == null || raw.Length == 0) { return list; }
        string norm = raw.Replace(",", " ");
        string[] parts = norm.Split(" ");
        for (int i = 0; i < parts.Length; i += 1) {
            string tok = parts[i].Trim();
            if (tok.Length > 0) {
                float val = ParseUtil.ParseFloat(tok, ParseUtil.NaN());
                if (ParseUtil.IsFinite(val)) {
                    list.Push(val);
                }
            }
        }
        // Insertion sort ascending
        for (int i = 1; i < list.Length; i += 1) {
            float key = list[i];
            int j = i - 1;
            while (j >= 0 && list[j] > key) {
                list[j + 1] = list[j];
                j -= 1;
            }
            list[j + 1] = key;
        }
        return list;
    }

    public Brush Fill { get { return _fill; } set { _fill = value; InvalidateDraw(); } }
    public string Placement { get { return _placement; } set { _placement = value == null ? "Top" : value; InvalidateDraw(); } }
    public float Minimum { get { return _min; } set { _min = value; InvalidateDraw(); } }
    public float Maximum { get { return _max; } set { _max = value; InvalidateDraw(); } }
    public float TickFrequency { get { return _tickFreq; } set { _tickFreq = value; InvalidateDraw(); } }
    public float ReservedSpace { get { return _reservedSpace; } set { _reservedSpace = value; InvalidateDraw(); } }
    public bool IsDirectionReversed { get { return _isDirRev; } set { _isDirRev = value; InvalidateDraw(); } }
    public string Ticks {
        get { return _ticksSpec; }
        set {
            _ticksSpec = value == null ? "" : value.Trim();
            _ticks = ParseTicksString(_ticksSpec);
            InvalidateDraw();
        }
    }

    public TickBar() : base() {
        IsHitTestVisible = false;
    }
    public override string GetTypeName() { return "TickBar"; }

    public override void OnRender(DrawingContext dc) {
        float w = ActualWidth;
        float h = ActualHeight;
        if (w <= 0.5f || h <= 0.5f) { return; }
        float minV = _min;
        float maxV = _max;
        float freq = _tickFreq;
        bool rev = _isDirRev;
        float resSpace = _reservedSpace;
        Brush effFill = _fill;
        float[] customTicks = _ticks;
        bool vertical = (_placement == "Left" || _placement == "Right");
        if (TemplatedParent is Slider) {
            Slider sl = TemplatedParent as Slider;
            minV = sl.Minimum;
            maxV = sl.Maximum;
            if (sl.TickFrequency > 0.0f) { freq = sl.TickFrequency; }
            rev = sl.IsDirectionReversed;
            vertical = (sl.Orientation == Orientation.Vertical);
            if (effFill == null) { effFill = sl.Foreground; }
            float[] slTicks = sl.TicksArray();
            if ((customTicks == null || customTicks.Length == 0) && slTicks != null && slTicks.Length > 0) {
                customTicks = slTicks;
            }
            if (resSpace <= 0.0f) {
                resSpace = sl.EffectiveThumbSpan();
            }
        }
        uint col = effFill != null ? effFill.ResolveRgba() : ParseUtil.PackArgb(200u, 120u, 130u, 140u);
        if ((col / 16777216u) % 256u == 0u) { return; }
        float span = maxV - minV;
        if (span <= 0.0001f) { span = 1.0f; }
        if (span != span) { return; }
        float halfRes = resSpace * 0.5f;

        if (customTicks != null && customTicks.Length > 0) {
            if (!vertical) {
                float usable = Math.Maxf(1.0f, w - resSpace);
                for (int i = 0; i < customTicks.Length; i += 1) {
                    float tv = customTicks[i];
                    if (tv < minV - 0.0001f || tv > maxV + 0.0001f) { continue; }
                    float t = Math.Clampf((tv - minV) / span, 0.0f, 1.0f);
                    if (rev) { t = 1.0f - t; }
                    float x = halfRes + t * usable;
                    dc.DrawLine(x, 0.0f, x, h, col, 1.0f);
                }
            } else {
                float usable = Math.Maxf(1.0f, h - resSpace);
                for (int i = 0; i < customTicks.Length; i += 1) {
                    float tv = customTicks[i];
                    if (tv < minV - 0.0001f || tv > maxV + 0.0001f) { continue; }
                    float t = Math.Clampf((tv - minV) / span, 0.0f, 1.0f);
                    if (!rev) { t = 1.0f - t; }
                    float y = halfRes + t * usable;
                    dc.DrawLine(0.0f, y, w, y, col, 1.0f);
                }
            }
            return;
        }

        if (freq <= 0.0001f) { freq = span / 10.0f; }
        if (freq != freq) { return; }
        int maxTicks = 200;
        if (span / freq > (float)maxTicks) { freq = span / (float)maxTicks; }
        if (!vertical) {
            float usable = Math.Maxf(1.0f, w - resSpace);
            float y0 = 0.0f;
            float y1 = h;
            float v = minV;
            int guard = 0;
            while (v <= maxV + freq * 0.01f && guard <= maxTicks + 2) {
                float t = Math.Clampf((v - minV) / span, 0.0f, 1.0f);
                if (rev) { t = 1.0f - t; }
                float x = halfRes + t * usable;
                dc.DrawLine(x, y0, x, y1, col, 1.0f);
                v = v + freq;
                guard += 1;
            }
        } else {
            float usable = Math.Maxf(1.0f, h - resSpace);
            float x0 = 0.0f;
            float x1 = w;
            float v = minV;
            int guard = 0;
            while (v <= maxV + freq * 0.01f && guard <= maxTicks + 2) {
                float t = Math.Clampf((v - minV) / span, 0.0f, 1.0f);
                if (!rev) { t = 1.0f - t; }
                float y = halfRes + t * usable;
                dc.DrawLine(x0, y, x1, y, col, 1.0f);
                v = v + freq;
                guard += 1;
            }
        }
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Fill") { Fill = UIElement.CoerceBrush(value); return true; }
        if (propName == "Placement") { Placement = DependencyObject.UnboxString(value, "Top"); return true; }
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 10.0f); return true; }
        if (propName == "TickFrequency") { TickFrequency = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "ReservedSpace") { ReservedSpace = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "IsDirectionReversed") { IsDirectionReversed = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Ticks") { Ticks = DependencyObject.UnboxString(value, ""); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Fill") { return _fill; }
        if (propName == "Placement") { return DependencyObject.BoxString(_placement); }
        if (propName == "Minimum") { return DependencyObject.BoxFloat(_min); }
        if (propName == "Maximum") { return DependencyObject.BoxFloat(_max); }
        if (propName == "TickFrequency") { return DependencyObject.BoxFloat(_tickFreq); }
        if (propName == "ReservedSpace") { return DependencyObject.BoxFloat(_reservedSpace); }
        if (propName == "IsDirectionReversed") { return DependencyObject.BoxBool(_isDirRev); }
        if (propName == "Ticks") { return DependencyObject.BoxString(_ticksSpec); }
        return base.GetPropertyValue(propName);
    }
}

public class RangeBase : Control {
    internal override int FastPropEpochCategory() { return 4; }
    private float _min = 0.0f;
    private float _max = 100.0f;
    private float _value = 0.0f;
    private float _desiredValue = 0.0f;
    private float _smallChange = 1.0f;
    private float _largeChange = 10.0f;

    public float SmallChange {
        get { return _smallChange; }
        set { _smallChange = value; BumpPropEpoch(); }
    }
    public float LargeChange {
        get { return _largeChange; }
        set { _largeChange = value; BumpPropEpoch(); }
    }

    public event RoutedPropertyChangedEventHandler<float> ValueChanged;

    public RangeBase() : base() {}
    public override string GetTypeName() { return "RangeBase"; }

    private void ApplyClampedValue() {
        float clamped = Math.Clampf(_desiredValue, _min, _max);
        if (_value == clamped) { return; }
        float old = _value;
        _value = clamped;
        var evArgs = new RoutedPropertyChangedEventArgs<float>(old, clamped, this);
        OnValueChanged(old, clamped);
        ValueChanged(this, evArgs);
        RaiseRoutedEvent("ValueChanged", evArgs);
        BindingEngine.MarkFlushDirty();
        BindingEngine.NotifyTargetPropertyChanged(this, "Value", DependencyObject.BoxFloat(clamped));
        NotifyStateInvalidatedHooks();
    }

    public float Minimum {
        get { return _min; }
        set {
            _min = value;
            if (_max < _min) { _max = _min; }
            BumpPropEpoch();
            ApplyClampedValue();
            OnRangeUpdated();
            NotifyStateInvalidatedHooks();
            BindingEngine.MarkFlushDirty();
        }
    }
    public float Maximum {
        get { return _max; }
        set {
            _max = value < _min ? _min : value;
            BumpPropEpoch();
            ApplyClampedValue();
            OnRangeUpdated();
            NotifyStateInvalidatedHooks();
            BindingEngine.MarkFlushDirty();
        }
    }
    public float Value {
        get { return _value; }
        set {
            _desiredValue = value;
            ApplyClampedValue();
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
    private static Slider[] _defaultSliders;

    private Orientation _orientation = Orientation.Horizontal;
    private float _tickFrequency = 1.0f;
    private string _tickPlacement = "None";
    private bool _isDirectionReversed = false;
    private float _lastTickMin = 0.0f;
    private float _lastTickMax = 10.0f;
    private float[] _ticks;
    private string _ticksSpec = "";
    private float _dragRawValue = 0.0f;
    public bool IsSnapToTickEnabled { get; set; } = false;
    public bool IsMoveToPointEnabled { get; set; } = false;

    public string Ticks {
        get { return _ticksSpec; }
        set {
            string next = value == null ? "" : value.Trim();
            if (_ticksSpec == next) { return; }
            _ticksSpec = next;
            _ticks = TickBar.ParseTicksString(_ticksSpec);
            InvalidateSubtreeTickBars(TemplateRoot());
        }
    }

    internal float[] TicksArray() { return _ticks; }

    internal float EffectiveThumbSpan() {
        if (_partTrack != null && _partTrack.Thumb != null) {
            float ts = _orientation == Orientation.Horizontal
                ? _partTrack.Thumb.ActualWidth
                : _partTrack.Thumb.ActualHeight;
            if (ts > 0.0f) { return ts; }
        }
        return 4.0f;
    }

    public float SnapValueToTicks(float raw) {
        float clamped = Math.Clampf(raw, Minimum, Maximum);
        if (!IsSnapToTickEnabled) { return clamped; }
        if (_ticks != null && _ticks.Length > 0) {
            float best = Minimum;
            float bestDist = Math.Absf(clamped - Minimum);
            float maxDist = Math.Absf(clamped - Maximum);
            if (maxDist < bestDist) { best = Maximum; bestDist = maxDist; }
            for (int i = 0; i < _ticks.Length; i += 1) {
                float tv = Math.Clampf(_ticks[i], Minimum, Maximum);
                float d = Math.Absf(clamped - tv);
                if (d < bestDist) {
                    bestDist = d;
                    best = tv;
                }
            }
            return best;
        }
        if (_tickFrequency > 0.0001f) {
            float steps = (clamped - Minimum) / _tickFrequency;
            int snapped = (int)(steps + 0.5f);
            return Math.Clampf(Minimum + (float)snapped * _tickFrequency, Minimum, Maximum);
        }
        return clamped;
    }
    public bool IsDirectionReversed {
        get { return _isDirectionReversed; }
        set {
            if (_isDirectionReversed == value) { return; }
            _isDirectionReversed = value;
            OnRangeUpdated();
            InvalidateSubtreeTickBars(TemplateRoot());
        }
    }
    public string TickPlacement {
        get { return _tickPlacement; }
        set {
            string next = value == null ? "None" : value.Trim();
            if (_tickPlacement == next) { return; }
            _tickPlacement = next;
            UpdateTriggersAndStates();
        }
    }

    private Grid _trackGrid;
    private Border _trackBg;
    private Border _fillBar;
    private Border _thumb;
    private Track _partTrack;
    private bool _scrubbing = false;
    private float _lastSyncW = -1.0f;
    private float _lastSyncH = -1.0f;

    public Slider() : base() {
        Focusable = true;
        Maximum = 10.0f;
        LargeChange = 1.0f;
        SmallChange = 0.1f;
        RegisterDefaultSlider(this);
    }
    public Slider(float min, float max, float val) : this() {
        Minimum = min;
        Maximum = max;
        Value = val;
    }

    private static int _lastSyncLayoutEpoch = -1;

    private static void RegisterDefaultSlider(Slider s) {
        if (_defaultSliders == null) {
            _defaultSliders = new Slider[0];
            BindingEngine.AddPreFlushHook(() => { Slider.SyncAllDefaultLayouts(); });
        }
        _lastSyncLayoutEpoch = -1;
        if (!_defaultSliders.Contains(s)) { _defaultSliders.Push(s); }
    }

    public static void SyncAllDefaultLayouts() {
        if (_defaultSliders == null || _defaultSliders.Length == 0) { return; }
        int curEp = BindingEngine._curLayoutSizeEpoch;
        if (curEp == _lastSyncLayoutEpoch) { return; }
        _lastSyncLayoutEpoch = curEp;
        for (int i = _defaultSliders.Length - 1; i >= 0; i -= 1) {
            Slider s = _defaultSliders[i];
            if (s == null || s._isDisposedTree) {
                _defaultSliders.RemoveAt(i);
                continue;
            }
            if (s._thumb == null || s.IsInsideClosedPopup() || s.Visibility == Visibility.Collapsed) { continue; }
            float aw = s.ActualWidth;
            float ah = s.ActualHeight;
            if (Math.Absf(aw - s._lastSyncW) > 0.25f || Math.Absf(ah - s._lastSyncH) > 0.25f) {
                s._lastSyncW = aw;
                s._lastSyncH = ah;
                s.OnRangeUpdated();
            }
        }
    }

    public override string GetTypeName() { return "Slider"; }

    public Orientation Orientation {
        get { return _orientation; }
        set {
            if (_orientation == value) { return; }
            _orientation = value;
            OnRangeUpdated();
            UpdateTriggersAndStates();
            InvalidateSubtreeTickBars(TemplateRoot());
        }
    }
    public float TickFrequency {
        get { return _tickFrequency; }
        set {
            _tickFrequency = value >= 0.0f ? value : 0.0f;
            InvalidateSubtreeTickBars(TemplateRoot());
        }
    }

    private static void InvalidateSubtreeTickBars(UIElement rootNode) {
        if (rootNode == null) { return; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement n = q[idx];
            idx += 1;
            if (n == null) { continue; }
            if (n is TickBar) { (n as TickBar).InvalidateDraw(); }
            for (int i = 0; i < n.ChildCount(); i += 1) { q.Push(n.ChildAt(i)); }
        }
    }

    private static Track FindFirstTrack(UIElement rootNode) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node is Track) { return node as Track; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    private void SyncPartTrack() {
        if (_partTrack == null) { return; }
        _partTrack.Orientation = _orientation;
        _partTrack.IsDirectionReversed = IsDirectionReversed;
        _partTrack.Minimum = Minimum;
        _partTrack.Maximum = Maximum;
        _partTrack.Value = Value;
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        _partTrack = null;
        if (Template != null) {
            _trackBg = null;
            _fillBar = null;
            _thumb = null;
            UIElement pt = GetTemplateChild("PART_Track");
            if (pt is Track) { _partTrack = pt as Track; }
            else { _partTrack = FindFirstTrack(TemplateRoot()); }
            if (_partTrack != null) {
                SyncPartTrack();
                if (_partTrack.Thumb != null) {
                    _partTrack.Thumb.DragStarted += (object s, DragStartedEventArgs e) => {
                        _dragRawValue = Value;
                    };
                    _partTrack.Thumb.DragDelta += (object s, DragDeltaEventArgs e) => {
                        float tw = (_partTrack != null && _partTrack.ActualWidth > 0.0f) ? _partTrack.ActualWidth : ActualWidth;
                        float th = (_partTrack != null && _partTrack.ActualHeight > 0.0f) ? _partTrack.ActualHeight : ActualHeight;
                        float thumbSpan = EffectiveThumbSpan();
                        float usableW = Math.Maxf(1.0f, tw - thumbSpan);
                        float usableH = Math.Maxf(1.0f, th - thumbSpan);
                        float span = Maximum - Minimum;
                        if (span <= 0.00001f) { return; }
                        float deltaFrac = _orientation == Orientation.Horizontal
                            ? (e.HorizontalChange / usableW)
                            : (-e.VerticalChange / usableH);
                        if (IsDirectionReversed) { deltaFrac = -deltaFrac; }
                        _dragRawValue = Math.Clampf(_dragRawValue + deltaFrac * span, Minimum, Maximum);
                        Value = SnapValueToTicks(_dragRawValue);
                    };
                }
                if (_partTrack.DecreaseRepeatButton != null) {
                    _partTrack.DecreaseRepeatButton.PreviewMouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                        if (IsMoveToPointEnabled) {
                            UpdateFromPointer(e.X, e.Y);
                            e.Handled = true;
                        }
                    };
                }
                if (_partTrack.IncreaseRepeatButton != null) {
                    _partTrack.IncreaseRepeatButton.PreviewMouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                        if (IsMoveToPointEnabled) {
                            UpdateFromPointer(e.X, e.Y);
                            e.Handled = true;
                        }
                    };
                }
            }
        }
    }

    protected override void OnRangeUpdated() {
        SyncPartTrack();
        if (Minimum != _lastTickMin || Maximum != _lastTickMax) {
            _lastTickMin = Minimum;
            _lastTickMax = Maximum;
            InvalidateSubtreeTickBars(TemplateRoot());
        }
        if (_trackBg == null || _fillBar == null || _thumb == null) { return; }
        float frac = NormalizedFraction();
        if (IsDirectionReversed) { frac = 1.0f - frac; }
        float w = ActualWidth > 15.0f ? ActualWidth : (ParseUtil.IsFinite(Width) ? Width : 140.0f);
        float h = ActualHeight > 15.0f ? ActualHeight : (ParseUtil.IsFinite(Height) ? Height : 22.0f);
        if (_orientation == Orientation.Horizontal) {
            if (_trackGrid != null) {
                _trackGrid.Height = 22.0f;
                _trackGrid.Width = ParseUtil.NaN();
            }
            _trackBg.Height = 2.0f;
            _trackBg.Width = ParseUtil.NaN();
            _trackBg.HorizontalAlignment = HorizontalAlignment.Stretch;
            _trackBg.VerticalAlignment = VerticalAlignment.Center;
            float usable = Math.Maxf(1.0f, w - 15.0f);
            float thumbX = usable * frac;
            _fillBar.Height = 2.0f;
            _fillBar.Width = thumbX + 7.5f;
            _fillBar.HorizontalAlignment = HorizontalAlignment.Left;
            _fillBar.VerticalAlignment = VerticalAlignment.Center;
            _fillBar.Margin = new Thickness(0.0f);
            _thumb.HorizontalAlignment = HorizontalAlignment.Left;
            _thumb.VerticalAlignment = VerticalAlignment.Center;
            _thumb.Margin = new Thickness(thumbX, 0.0f, 0.0f, 0.0f);
        } else {
            if (_trackGrid != null) {
                _trackGrid.Width = 22.0f;
                _trackGrid.Height = ParseUtil.NaN();
            }
            _trackBg.Width = 2.0f;
            _trackBg.Height = ParseUtil.NaN();
            _trackBg.HorizontalAlignment = HorizontalAlignment.Center;
            _trackBg.VerticalAlignment = VerticalAlignment.Stretch;
            float usable = Math.Maxf(1.0f, h - 15.0f);
            float thumbY = usable * (1.0f - frac);
            _fillBar.Width = 2.0f;
            _fillBar.Height = Math.Maxf(2.0f, h - thumbY - 7.5f);
            _fillBar.HorizontalAlignment = HorizontalAlignment.Center;
            _fillBar.VerticalAlignment = VerticalAlignment.Bottom;
            _fillBar.Margin = new Thickness(0.0f);
            _thumb.HorizontalAlignment = HorizontalAlignment.Center;
            _thumb.VerticalAlignment = VerticalAlignment.Top;
            _thumb.Margin = new Thickness(0.0f, thumbY, 0.0f, 0.0f);
        }
    }

    private void UpdateFromPointer(float screenX, float screenY) {
        if (!ParseUtil.IsValidHandle(__nativeHandle)) { return; }
        float lx = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 0);
        float ly = Visual_ScreenToLocal(__nativeHandle, screenX, screenY, 1);
        float frac = 0.0f;
        if (_partTrack != null) {
            handle thandle = ParseUtil.IsValidHandle(_partTrack.__nativeHandle) ? _partTrack.__nativeHandle : __nativeHandle;
            float tx = Visual_ScreenToLocal(thandle, screenX, screenY, 0);
            float ty = Visual_ScreenToLocal(thandle, screenX, screenY, 1);
            float tw = Math.Maxf(1.0f, _partTrack.ActualWidth > 0.0f ? _partTrack.ActualWidth : ActualWidth);
            float th = Math.Maxf(1.0f, _partTrack.ActualHeight > 0.0f ? _partTrack.ActualHeight : ActualHeight);
            float thumbW = (_partTrack.Thumb != null && _partTrack.Thumb.ActualWidth > 0.0f) ? _partTrack.Thumb.ActualWidth : 0.0f;
            float thumbH = (_partTrack.Thumb != null && _partTrack.Thumb.ActualHeight > 0.0f) ? _partTrack.Thumb.ActualHeight : 0.0f;
            float usableW = Math.Maxf(1.0f, tw - thumbW);
            float usableH = Math.Maxf(1.0f, th - thumbH);
            frac = _orientation == Orientation.Horizontal
                ? Math.Clampf((tx - thumbW * 0.5f) / usableW, 0.0f, 1.0f)
                : 1.0f - Math.Clampf((ty - thumbH * 0.5f) / usableH, 0.0f, 1.0f);
        } else {
            float w = Math.Maxf(24.0f, ActualWidth);
            float h = Math.Maxf(24.0f, ActualHeight);
            frac = _orientation == Orientation.Horizontal
                ? Math.Clampf((lx - 7.5f) / Math.Maxf(1.0f, w - 15.0f), 0.0f, 1.0f)
                : 1.0f - Math.Clampf((ly - 7.5f) / Math.Maxf(1.0f, h - 15.0f), 0.0f, 1.0f);
        }
        if (IsDirectionReversed) { frac = 1.0f - frac; }
        float raw = Minimum + frac * (Maximum - Minimum);
        Value = SnapValueToTicks(raw);
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        if (Template == null && _thumb != null) {
            _thumb.Background = new SolidColorBrush((IsMouseOver || _scrubbing) ? "#FF2597E5" : "#FF1382CE");
        }
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled) { return; }
        _scrubbing = true;
        if (Template == null && _thumb != null) { _thumb.Background = new SolidColorBrush("#FF2597E5"); }
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
            if (Template == null && _thumb != null) {
                _thumb.Background = new SolidColorBrush(IsMouseOver ? "#FF2597E5" : "#FF1382CE");
            }
            ReleaseMouseCapture();
            e.Handled = true;
        }
    }

    private float StepValueWithTickSnap(float current, float delta) {
        if (!IsSnapToTickEnabled) {
            return Math.Clampf(current + delta, Minimum, Maximum);
        }
        float snapped = SnapValueToTicks(current + delta);
        if ((delta > 0.0f && snapped > current + 0.0001f) ||
            (delta < 0.0f && snapped < current - 0.0001f)) {
            return snapped;
        }
        if (_ticks != null && _ticks.Length > 0) {
            if (delta > 0.0f) {
                for (int i = 0; i < _ticks.Length; i += 1) {
                    float tv = Math.Clampf(_ticks[i], Minimum, Maximum);
                    if (tv > current + 0.0001f) { return tv; }
                }
                return Maximum;
            }
            for (int i = _ticks.Length - 1; i >= 0; i -= 1) {
                float tv = Math.Clampf(_ticks[i], Minimum, Maximum);
                if (tv < current - 0.0001f) { return tv; }
            }
            return Minimum;
        }
        if (_tickFrequency > 0.0001f) {
            float steps = Math.Maxf(0.0f, (current - Minimum) / _tickFrequency);
            if (delta > 0.0f) {
                int nextStep = (int)(steps + 0.0001f) + 1;
                return Math.Clampf(Minimum + (float)nextStep * _tickFrequency, Minimum, Maximum);
            }
            int k = (int)steps;
            float prev = Minimum + (float)k * _tickFrequency;
            if (prev >= current - 0.0001f && k > 0) {
                prev = Minimum + (float)(k - 1) * _tickFrequency;
            }
            return Math.Clampf(prev, Minimum, Maximum);
        }
        return Math.Clampf(current + delta, Minimum, Maximum);
    }

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        if (e.Key == KeyCode.Left || e.Key == KeyCode.Down) { Value = StepValueWithTickSnap(Value, -SmallChange); e.Handled = true; }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.Up) { Value = StepValueWithTickSnap(Value, SmallChange); e.Handled = true; }
        else if (e.Key == KeyCode.Home) { Value = Minimum; e.Handled = true; }
        else if (e.Key == KeyCode.End) { Value = Maximum; e.Handled = true; }
    }

    protected override UIElement CreateDefaultTemplate() {
        _trackGrid = new Grid();
        _trackGrid.Height = 22.0f;
        _trackGrid.MinWidth = 80.0f;
        _trackGrid.Background = Brushes.Transparent();
        _trackGrid.VerticalAlignment = VerticalAlignment.Center;

        _trackBg = new Border();
        _trackBg.Height = 2.0f;
        _trackBg.Background = new SolidColorBrush("#FF5D656B");
        _trackBg.CornerRadius = new CornerRadius(1.0f);
        _trackBg.HorizontalAlignment = HorizontalAlignment.Stretch;
        _trackBg.VerticalAlignment = VerticalAlignment.Center;

        _fillBar = new Border();
        _fillBar.Height = 2.0f;
        _fillBar.Background = new SolidColorBrush("#FF1382CE");
        _fillBar.CornerRadius = new CornerRadius(1.0f);
        _fillBar.HorizontalAlignment = HorizontalAlignment.Left;
        _fillBar.VerticalAlignment = VerticalAlignment.Center;

        _thumb = new Border();
        _thumb.Width = 15.0f;
        _thumb.Height = 15.0f;
        _thumb.CornerRadius = new CornerRadius(7.5f);
        _thumb.BorderThickness = new Thickness(0.0f);
        _thumb.Background = new SolidColorBrush("#FF1382CE");
        _thumb.HorizontalAlignment = HorizontalAlignment.Left;
        _thumb.VerticalAlignment = VerticalAlignment.Center;

        _trackGrid.AddChild(_trackBg);
        _trackGrid.AddChild(_fillBar);
        _trackGrid.AddChild(_thumb);
        OnRangeUpdated();
        return _trackGrid;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsDirectionReversed") { IsDirectionReversed = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsSnapToTickEnabled") { IsSnapToTickEnabled = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "IsMoveToPointEnabled") { IsMoveToPointEnabled = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Ticks") { Ticks = DependencyObject.UnboxString(value, ""); return true; }
        if (propName == "TickFrequency") { TickFrequency = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "TickPlacement") {
            if (value is BoxedInt) {
                int tp = (value as BoxedInt).Value;
                TickPlacement = tp == 1 ? "TopLeft" : (tp == 2 ? "BottomRight" : (tp == 3 ? "Both" : "None"));
                return true;
            }
            TickPlacement = DependencyObject.UnboxString(value, "None");
            return true;
        }
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
        if (propName == "IsDirectionReversed") { return DependencyObject.BoxBool(IsDirectionReversed); }
        if (propName == "IsSnapToTickEnabled") { return DependencyObject.BoxBool(IsSnapToTickEnabled); }
        if (propName == "IsMoveToPointEnabled") { return DependencyObject.BoxBool(IsMoveToPointEnabled); }
        if (propName == "Ticks") { return DependencyObject.BoxString(_ticksSpec); }
        if (propName == "TickFrequency") { return DependencyObject.BoxFloat(_tickFrequency); }
        if (propName == "TickPlacement") { return DependencyObject.BoxString(_tickPlacement); }
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        return base.GetPropertyValue(propName);
    }
}

public class ProgressBar : RangeBase {
    private bool _isIndeterminate = false;
    private Orientation _orientation = Orientation.Horizontal;
    private UIElement _trackPart;
    private UIElement _indicatorPart;
    private float _lastSyncedTrackSpan = -1.0f;
    private static ProgressBar[] _liveBars;
    private static bool _barSyncInstalled = false;
    private static int _lastSyncLayoutEpoch = -1;

    private static void RegisterProgressBar(ProgressBar bar) {
        if (bar == null) { return; }
        if (!_barSyncInstalled) {
            _barSyncInstalled = true;
            _liveBars = new ProgressBar[0];
            BindingEngine.AddPostFlushHook(() => { ProgressBar.SyncAllProgressBars(); });
            UIElement.RegisterDisposeHook(() => { ProgressBar.CleanupDisposedBars(); });
        }
        _lastSyncLayoutEpoch = -1;
        for (int i = 0; i < _liveBars.Length; i += 1) {
            if (_liveBars[i] == bar) { return; }
        }
        _liveBars.Push(bar);
    }

    internal static void CleanupDisposedBars() {
        if (_liveBars == null) { return; }
        for (int i = _liveBars.Length - 1; i >= 0; i -= 1) {
            if (_liveBars[i] == null || _liveBars[i]._isDisposedTree) {
                _liveBars.RemoveAt(i);
            }
        }
    }

    public static void SyncAllProgressBars() {
        if (_liveBars == null || _liveBars.Length == 0) { return; }
        int curEp = BindingEngine._curLayoutSizeEpoch;
        if (curEp == _lastSyncLayoutEpoch) { return; }
        _lastSyncLayoutEpoch = curEp;
        for (int i = 0; i < _liveBars.Length; i += 1) {
            ProgressBar pb = _liveBars[i];
            if (pb == null || pb._isDisposedTree || pb.IsInsideClosedPopup() || pb.Visibility == Visibility.Collapsed) { continue; }
            pb.SyncTrackResizeIfNeeded();
        }
    }

    public ProgressBar() : base() {
        Foreground = new SolidColorBrush("#FF3B82F6");
        Background = new SolidColorBrush("#FF1B202B");
        RegisterProgressBar(this);
    }

    public override string GetTypeName() { return "ProgressBar"; }

    public bool IsIndeterminate {
        get { return _isIndeterminate; }
        set {
            if (_isIndeterminate == value) { return; }
            _isIndeterminate = value;
            OnRangeUpdated();
            UpdateTriggersAndStates();
        }
    }
    public Orientation Orientation {
        get { return _orientation; }
        set {
            if (_orientation == value) { return; }
            _orientation = value;
            OnRangeUpdated();
            UpdateTriggersAndStates();
        }
    }

    private float ResolveTrackWidth() {
        if (_trackPart != null && _trackPart.ActualWidth > 1.0f) { return _trackPart.ActualWidth; }
        if (ActualWidth > 1.0f) { return ActualWidth; }
        return ParseUtil.IsFinite(Width) ? Width : 200.0f;
    }

    private float ResolveTrackHeight() {
        if (_trackPart != null && _trackPart.ActualHeight > 1.0f) { return _trackPart.ActualHeight; }
        if (ActualHeight > 1.0f) { return ActualHeight; }
        return ParseUtil.IsFinite(Height) ? Height : 10.0f;
    }

    private void SyncTrackResizeIfNeeded() {
        if (_indicatorPart == null) { return; }
        bool useHorizontalTrack = (Template != null || _orientation == Orientation.Horizontal);
        float span = useHorizontalTrack ? ResolveTrackWidth() : ResolveTrackHeight();
        if (Math.Absf(span - _lastSyncedTrackSpan) > 0.25f) {
            OnRangeUpdated();
        }
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        _lastSyncLayoutEpoch = -1;
        if (Template == null) { return; }
        _trackPart = GetTemplateChild("PART_Track");
        _indicatorPart = GetTemplateChild("PART_Indicator");
        _lastSyncedTrackSpan = -1.0f;
        OnRangeUpdated();
    }

    protected override void OnRangeUpdated() {
        if (_indicatorPart == null) { return; }
        if (Template == null) {
            _indicatorPart.Background = Foreground;
        }
        float w = ResolveTrackWidth();
        float h = ResolveTrackHeight();
        bool useHorizontalTrack = (Template != null || _orientation == Orientation.Horizontal);
        _lastSyncedTrackSpan = useHorizontalTrack ? w : h;
        float frac = _isIndeterminate ? 0.35f : NormalizedFraction();
        if (useHorizontalTrack) {
            _indicatorPart.HorizontalAlignment = HorizontalAlignment.Left;
            _indicatorPart.Width = w * frac;
            if (Template == null) { _indicatorPart.Height = h; }
        } else {
            _indicatorPart.VerticalAlignment = VerticalAlignment.Bottom;
            _indicatorPart.Width = w;
            _indicatorPart.Height = h * frac;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        CornerRadius curCr = CornerRadius;
        if (!HasLocalProperty("CornerRadius") && (curCr == null || curCr.TopLeft == 0.0f)) {
            CornerRadius = new CornerRadius(5.0f);
        }
        var track = new Border();
        if (!ParseUtil.IsFinite(Height)) { track.MinHeight = 10.0f; }
        if (!ParseUtil.IsFinite(Width) && MinWidth <= 0.0f) { track.MinWidth = 100.0f; }
        track.Background = Background;
        track.CornerRadius = CornerRadius;
        track.ClipToBounds = true;
        track.HorizontalAlignment = HorizontalAlignment.Stretch;
        track.VerticalAlignment = VerticalAlignment.Stretch;

        var indicator = new Border();
        indicator.Name = "PART_Indicator";
        indicator.Background = Foreground;
        indicator.CornerRadius = CornerRadius;
        indicator.HorizontalAlignment = HorizontalAlignment.Left;
        track.Child = indicator;

        _trackPart = track;
        _indicatorPart = indicator;
        _lastSyncedTrackSpan = -1.0f;
        OnRangeUpdated();
        return track;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "IsIndeterminate") { IsIndeterminate = DependencyObject.UnboxBool(value, false); return true; }
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
        if (propName == "IsIndeterminate") { return DependencyObject.BoxBool(_isIndeterminate); }
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        return base.GetPropertyValue(propName);
    }
}

public class ScrollBar : RangeBase {
    private Orientation _orientation = Orientation.Vertical;
    private float _viewportSize = 100.0f;
    private bool _isDirectionReversed = false;
    private bool _hasExplicitDirRev = false;
    private Border _trackBg;
    private Border _decBtn;
    private Border _incBtn;
    private Thumb _thumb;
    private Track _partTrack;
    private bool _hoverExpanded = false;
    private float _lastSyncW = -1.0f;
    private float _lastSyncH = -1.0f;
    private static ScrollBar[] _defaultBars;

    public event ScrollEventHandler Scroll;

    public ScrollBar() : base() {
        SmallChange = 24.0f;
        LargeChange = 120.0f;
    }

    private static int _lastSyncLayoutEpoch = -1;

    private static void RegisterDefaultBar(ScrollBar sb) {
        if (_defaultBars == null) {
            _defaultBars = new ScrollBar[0];
            BindingEngine.AddPreFlushHook(() => { ScrollBar.SyncAllDefaultLayouts(); });
        }
        _lastSyncLayoutEpoch = -1;
        if (!_defaultBars.Contains(sb)) { _defaultBars.Push(sb); }
    }

    public static void SyncAllDefaultLayouts() {
        if (_defaultBars == null || _defaultBars.Length == 0) { return; }
        int curEp = BindingEngine._curLayoutSizeEpoch;
        if (curEp == _lastSyncLayoutEpoch) { return; }
        _lastSyncLayoutEpoch = curEp;
        for (int i = _defaultBars.Length - 1; i >= 0; i -= 1) {
            ScrollBar sb = _defaultBars[i];
            if (sb == null || sb._isDisposedTree) {
                _defaultBars.RemoveAt(i);
                continue;
            }
            if (sb._thumb == null || sb.Template != null || sb.IsInsideClosedPopup() || sb.Visibility == Visibility.Collapsed) { continue; }
            float aw = sb.ActualWidth;
            float ah = sb.ActualHeight;
            if (Math.Absf(aw - sb._lastSyncW) > 0.25f || Math.Absf(ah - sb._lastSyncH) > 0.25f) {
                sb._lastSyncW = aw;
                sb._lastSyncH = ah;
                sb.OnRangeUpdated();
            }
        }
    }

    public override string GetTypeName() { return "ScrollBar"; }

    public Orientation Orientation {
        get { return _orientation; }
        set { _orientation = value; BumpPropEpoch(); OnRangeUpdated(); }
    }
    public float ViewportSize {
        get { return _viewportSize; }
        set { _viewportSize = value >= 0.0f ? value : 0.0f; OnRangeUpdated(); }
    }
    public bool IsDirectionReversed {
        get { return _partTrack != null ? _partTrack.IsDirectionReversed : _isDirectionReversed; }
        set {
            _hasExplicitDirRev = true;
            _isDirectionReversed = value;
            if (_partTrack != null) { _partTrack.IsDirectionReversed = value; }
            OnRangeUpdated();
        }
    }

    protected override void OnRangeUpdated() {
        if (_partTrack != null) {
            _partTrack.Orientation = _orientation;
            _partTrack.Minimum = Minimum;
            _partTrack.Maximum = Maximum;
            _partTrack.ViewportSize = _viewportSize;
            if (_hasExplicitDirRev) { _partTrack.IsDirectionReversed = _isDirectionReversed; }
            _partTrack.Value = Value;
            return;
        }
        if (_thumb == null) { return; }
        float span = Maximum - Minimum;
        float total = span + _viewportSize;
        float fracPos = NormalizedFraction();
        float inset = _hoverExpanded ? 16.0f : 4.0f;
        if (_orientation == Orientation.Vertical) {
            float trackH = Math.Maxf(24.0f, ActualHeight > 0.0f ? ActualHeight : 200.0f);
            if (_trackBg != null) {
                _trackBg.Width = 15.0f;
                _trackBg.Height = trackH;
                UIElement.SetCanvasLeft(_trackBg, 0.0f);
                UIElement.SetCanvasTop(_trackBg, 0.0f);
            }
            if (_decBtn != null) {
                UIElement.SetCanvasLeft(_decBtn, 0.0f);
                UIElement.SetCanvasTop(_decBtn, 0.0f);
            }
            if (_incBtn != null) {
                UIElement.SetCanvasLeft(_incBtn, 0.0f);
                UIElement.SetCanvasTop(_incBtn, Math.Maxf(0.0f, trackH - 15.0f));
            }
            float usable = Math.Maxf(24.0f, trackH - inset * 2.0f);
            float ratio = total > 0.0f ? Math.Clampf(_viewportSize / total, 0.12f, 1.0f) : 1.0f;
            float thumbH = Math.Minf(usable, Math.Maxf(24.0f, usable * ratio));
            float travel = Math.Maxf(0.0f, usable - thumbH);
            _thumb.Width = _hoverExpanded ? 6.5f : 3.5f;
            _thumb.Height = thumbH;
            UIElement.SetCanvasLeft(_thumb, _hoverExpanded ? 4.25f : 9.5f);
            UIElement.SetCanvasTop(_thumb, inset + travel * fracPos);
        } else {
            float trackW = Math.Maxf(24.0f, ActualWidth > 0.0f ? ActualWidth : 200.0f);
            if (_trackBg != null) {
                _trackBg.Width = trackW;
                _trackBg.Height = 15.0f;
                UIElement.SetCanvasLeft(_trackBg, 0.0f);
                UIElement.SetCanvasTop(_trackBg, 0.0f);
            }
            if (_decBtn != null) {
                UIElement.SetCanvasLeft(_decBtn, 0.0f);
                UIElement.SetCanvasTop(_decBtn, 0.0f);
            }
            if (_incBtn != null) {
                UIElement.SetCanvasLeft(_incBtn, Math.Maxf(0.0f, trackW - 15.0f));
                UIElement.SetCanvasTop(_incBtn, 0.0f);
            }
            float usable = Math.Maxf(24.0f, trackW - inset * 2.0f);
            float ratio = total > 0.0f ? Math.Clampf(_viewportSize / total, 0.12f, 1.0f) : 1.0f;
            float thumbW = Math.Minf(usable, Math.Maxf(24.0f, usable * ratio));
            float travel = Math.Maxf(0.0f, usable - thumbW);
            _thumb.Width = thumbW;
            _thumb.Height = _hoverExpanded ? 6.5f : 3.5f;
            UIElement.SetCanvasLeft(_thumb, inset + travel * fracPos);
            UIElement.SetCanvasTop(_thumb, _hoverExpanded ? 4.25f : 9.5f);
        }
    }

    protected override void OnValueChanged(float oldVal, float newVal) {
        base.OnValueChanged(oldVal, newVal);
        Scroll(this, new ScrollEventArgs(newVal, this));
    }

    public override void OnApplyTemplate() {
        base.OnApplyTemplate();
        if (Template != null) {
            _thumb = null;
            _trackBg = null;
            _decBtn = null;
            _incBtn = null;
            _partTrack = GetTemplateChild("PART_Track") as Track;
            if (_partTrack != null && _hasExplicitDirRev) {
                _partTrack.IsDirectionReversed = _isDirectionReversed;
            }
            if (_partTrack != null && _partTrack.Thumb != null) {
                _partTrack.Thumb.DragDelta += (object s, DragDeltaEventArgs e) => {
                    float span = Maximum - Minimum;
                    if (span <= 0.001f) { return; }
                    if (_orientation == Orientation.Vertical) {
                        float trackH = Math.Maxf(24.0f, _partTrack.ActualHeight);
                        float thumbH = Math.Maxf(8.0f, _partTrack.Thumb.ActualHeight);
                        float travel = Math.Maxf(1.0f, trackH - thumbH);
                        float deltaY = _partTrack.IsDirectionReversed ? e.VerticalChange : -e.VerticalChange;
                        Value = Value + (deltaY / travel) * span;
                    } else {
                        float trackW = Math.Maxf(24.0f, _partTrack.ActualWidth);
                        float thumbW = Math.Maxf(8.0f, _partTrack.Thumb.ActualWidth);
                        float travel = Math.Maxf(1.0f, trackW - thumbW);
                        float deltaX = _partTrack.IsDirectionReversed ? -e.HorizontalChange : e.HorizontalChange;
                        Value = Value + (deltaX / travel) * span;
                    }
                };
            }
            OnRangeUpdated();
        }
    }

    private void ApplyThumbBrush(string hex, float radius) {
        if (_thumb == null) { return; }
        var br = new SolidColorBrush(hex);
        var cr = new CornerRadius(radius);
        _thumb.Background = br;
        _thumb.CornerRadius = cr;
        if (_thumb.TemplateRoot() is Border tb) {
            tb.Background = br;
            tb.CornerRadius = cr;
        }
    }

    private static void AnimateBarPartOpacity(UIElement el, float toAlpha) {
        if (el == null) { return; }
        if (ParseUtil.IsValidHandle(el.__nativeHandle)) {
            Anim_StartFloatTo(el.__nativeHandle, 0, toAlpha, 0.14f, 0, 0, 0.0f, false, 1);
        } else {
            el.Opacity = toAlpha;
        }
    }

    private void UpdateDefaultHoverState() {
        if (Template != null || _thumb == null) { return; }
        bool expanded = IsMouseOver || _thumb.IsDragging;
        if (expanded != _hoverExpanded) {
            _hoverExpanded = expanded;
            float targetOp = expanded ? 1.0f : 0.0f;
            AnimateBarPartOpacity(_trackBg, targetOp);
            AnimateBarPartOpacity(_decBtn, targetOp);
            AnimateBarPartOpacity(_incBtn, targetOp);
            OnRangeUpdated();
        }
        ApplyThumbBrush(expanded ? "#FF9BA1A8" : "#FF7A8086", expanded ? 3.25f : 1.75f);
    }

    protected override void OnVisualStatePropertyChanged() {
        base.OnVisualStatePropertyChanged();
        UpdateDefaultHoverState();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
        base.OnMouseLeftButtonDown(e);
        if (!e.Handled && IsEnabled) {
            e.Handled = true;
        }
    }

    protected override UIElement CreateDefaultTemplate() {
        _partTrack = null;
        _hoverExpanded = false;
        var host = new Canvas();
        host.Background = Brushes.Transparent();
        host.Width = _orientation == Orientation.Vertical ? 15.0f : ParseUtil.NaN();
        host.Height = _orientation == Orientation.Horizontal ? 15.0f : ParseUtil.NaN();

        _trackBg = new Border();
        _trackBg.Background = new SolidColorBrush("#EB282D32");
        _trackBg.CornerRadius = new CornerRadius(0.0f);
        _trackBg.Opacity = 0.0f;

        _decBtn = new Border();
        _decBtn.Width = 15.0f;
        _decBtn.Height = 15.0f;
        _decBtn.Background = Brushes.Transparent();
        _decBtn.Opacity = 0.0f;
        var decGlyph = new Path(_orientation == Orientation.Vertical ? "M 4,9 L 7.5,5.5 L 11,9" : "M 9,4 L 5.5,7.5 L 9,11");
        decGlyph.Width = 15.0f;
        decGlyph.Height = 15.0f;
        decGlyph.Stroke = new SolidColorBrush("#FFB8BEC4");
        decGlyph.StrokeThickness = 1.4f;
        decGlyph.Stretch = Stretch.None;
        _decBtn.Child = decGlyph;
        _decBtn.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            if (IsEnabled) { Value = Value - SmallChange; e.Handled = true; }
        };
        _decBtn.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => { e.Handled = true; };

        _incBtn = new Border();
        _incBtn.Width = 15.0f;
        _incBtn.Height = 15.0f;
        _incBtn.Background = Brushes.Transparent();
        _incBtn.Opacity = 0.0f;
        var incGlyph = new Path(_orientation == Orientation.Vertical ? "M 4,6 L 7.5,9.5 L 11,6" : "M 6,4 L 9.5,7.5 L 6,11");
        incGlyph.Width = 15.0f;
        incGlyph.Height = 15.0f;
        incGlyph.Stroke = new SolidColorBrush("#FFB8BEC4");
        incGlyph.StrokeThickness = 1.4f;
        incGlyph.Stretch = Stretch.None;
        _incBtn.Child = incGlyph;
        _incBtn.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
            if (IsEnabled) { Value = Value + SmallChange; e.Handled = true; }
        };
        _incBtn.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => { e.Handled = true; };

        _thumb = new Thumb();
        _thumb.Background = new SolidColorBrush("#FF7A8086");
        _thumb.CornerRadius = new CornerRadius(1.75f);
        _thumb.DragStarted += (object s, DragStartedEventArgs e) => { UpdateDefaultHoverState(); };
        _thumb.DragCompleted += (object s, DragCompletedEventArgs e) => { UpdateDefaultHoverState(); };
        _thumb.DragDelta += (object s, DragDeltaEventArgs e) => {
            float span = Maximum - Minimum;
            if (span <= 0.001f) { return; }
            float inset = _hoverExpanded ? 16.0f : 4.0f;
            if (_orientation == Orientation.Vertical) {
                float usable = Math.Maxf(24.0f, ActualHeight - inset * 2.0f);
                float travel = Math.Maxf(1.0f, usable - _thumb.Height);
                Value = Value + (e.VerticalChange / travel) * span;
            } else {
                float usable = Math.Maxf(24.0f, ActualWidth - inset * 2.0f);
                float travel = Math.Maxf(1.0f, usable - _thumb.Width);
                Value = Value + (e.HorizontalChange / travel) * span;
            }
        };

        host.AddChild(_trackBg);
        host.AddChild(_decBtn);
        host.AddChild(_incBtn);
        host.AddChild(_thumb);
        RegisterDefaultBar(this);
        UpdateDefaultHoverState();
        OnRangeUpdated();
        return host;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "ViewportSize") { ViewportSize = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "IsDirectionReversed") { IsDirectionReversed = DependencyObject.UnboxBool(value, false); return true; }
        if (propName == "Orientation") {
            if (value is BoxedInt) {
                Orientation = (value as BoxedInt).Value == 1 ? Orientation.Vertical : Orientation.Horizontal;
                return true;
            }
            string ov = DependencyObject.UnboxString(value, "Vertical");
            Orientation = ov == "Horizontal" ? Orientation.Horizontal : Orientation.Vertical;
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "ViewportSize") { return DependencyObject.BoxFloat(_viewportSize); }
        if (propName == "IsDirectionReversed") { return DependencyObject.BoxBool(IsDirectionReversed); }
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        return base.GetPropertyValue(propName);
    }
}

public class ScrollViewer : ContentControl {
    private static ScrollViewer[] _instances;

    private float _offsetX = 0.0f;
    private float _offsetY = 0.0f;
    private ScrollBarVisibility _hVis = ScrollBarVisibility.Disabled;
    private ScrollBarVisibility _vVis = ScrollBarVisibility.Auto;
    private bool _canContentScroll = false;

    private UIElement _viewport;
    private ScrollBar _vBar;
    private ScrollBar _hBar;
    private bool _syncingBars = false;
    private float _lastSyncExtW = -1.0f;
    private float _lastSyncExtH = -1.0f;
    private float _lastSyncVpW = -1.0f;
    private float _lastSyncVpH = -1.0f;

    public event ScrollEventHandler ScrollChanged;

    public ScrollViewer() : base() {
        ClipToBounds = true;
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Top;
        RegisterInstance(this);
    }

    private static int _lastSyncLayoutEpoch = -1;

    private static void RegisterInstance(ScrollViewer sv) {
        if (_instances == null) {
            _instances = new ScrollViewer[0];
            BindingEngine.AddPreFlushHook(() => { ScrollViewer.SyncAllMetrics(); });
        }
        _lastSyncLayoutEpoch = -1;
        for (int i = 0; i < _instances.Length; i += 1) {
            if (_instances[i] == sv) { return; }
        }
        _instances.Push(sv);
    }

    public static void SyncAllMetrics() {
        if (_instances == null || _instances.Length == 0) { return; }
        int curEp = BindingEngine._curLayoutSizeEpoch;
        if (curEp == _lastSyncLayoutEpoch) { return; }
        _lastSyncLayoutEpoch = curEp;
        for (int i = _instances.Length - 1; i >= 0; i -= 1) {
            ScrollViewer sv = _instances[i];
            if (sv == null || sv._isDisposedTree) {
                _instances.RemoveAt(i);
                continue;
            }
            if (sv.IsInsideClosedPopup() || sv.Visibility == Visibility.Collapsed) { continue; }
            float ew = sv.ExtentWidth;
            float eh = sv.ExtentHeight;
            float vw = sv.ViewportWidth;
            float vh = sv.ViewportHeight;
            if (ew != sv._lastSyncExtW || eh != sv._lastSyncExtH ||
                vw != sv._lastSyncVpW || vh != sv._lastSyncVpH) {
                sv._lastSyncExtW = ew;
                sv._lastSyncExtH = eh;
                sv._lastSyncVpW = vw;
                sv._lastSyncVpH = vh;
                sv.SyncScrollBarRanges();
            }
        }
    }

    public override string GetTypeName() { return "ScrollViewer"; }

    protected override void CollectExtraDisposeTargets(UIElement[] q) {
        base.CollectExtraDisposeTargets(q);
        _viewport = null;
        _vBar = null;
        _hBar = null;
    }

    public bool CanContentScroll {
        get { return _canContentScroll; }
        set { _canContentScroll = value; }
    }
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
    protected override float GetLogicalViewportWidth() { return _viewport != null ? _viewport.ActualWidth : ActualWidth; }
    protected override float GetLogicalViewportHeight() { return _viewport != null ? _viewport.ActualHeight : ActualHeight; }
    public float ScrollableWidth { get { return Math.Maxf(0.0f, ExtentWidth - ViewportWidth); } }
    public float ScrollableHeight { get { return Math.Maxf(0.0f, ExtentHeight - ViewportHeight); } }

    public Visibility ComputedVerticalScrollBarVisibility {
        get {
            bool showV = _vVis == ScrollBarVisibility.Visible || (_vVis == ScrollBarVisibility.Auto && ExtentHeight > ViewportHeight + 1.0f);
            return showV ? Visibility.Visible : Visibility.Collapsed;
        }
    }
    public Visibility ComputedHorizontalScrollBarVisibility {
        get {
            bool showH = _hVis == ScrollBarVisibility.Visible || (_hVis == ScrollBarVisibility.Auto && ExtentWidth > ViewportWidth + 1.0f);
            return showH ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private float MaxEffectiveVerticalOffset() {
        float vh = ViewportHeight;
        if (vh <= 0.0f) { return -1.0f; }
        if (_vVis == ScrollBarVisibility.Disabled) { return 0.0f; }
        if (_vVis == ScrollBarVisibility.Auto && ComputedVerticalScrollBarVisibility != Visibility.Visible) { return 0.0f; }
        return Math.Maxf(0.0f, ExtentHeight - vh);
    }

    private float MaxEffectiveHorizontalOffset() {
        float vw = ViewportWidth;
        if (vw <= 0.0f) { return -1.0f; }
        if (_hVis == ScrollBarVisibility.Disabled) { return 0.0f; }
        if (_hVis == ScrollBarVisibility.Auto && ComputedHorizontalScrollBarVisibility != Visibility.Visible) { return 0.0f; }
        return Math.Maxf(0.0f, ExtentWidth - vw);
    }

    public void ScrollToVerticalOffset(float offset) {
        float maxY = MaxEffectiveVerticalOffset();
        float next = maxY >= 0.0f ? Math.Clampf(offset, 0.0f, maxY) : Math.Maxf(0.0f, offset);
        _offsetY = next;
        ApplyScrollOffsets();
    }

    public void ScrollToHorizontalOffset(float offset) {
        float maxX = MaxEffectiveHorizontalOffset();
        float next = maxX >= 0.0f ? Math.Clampf(offset, 0.0f, maxX) : Math.Maxf(0.0f, offset);
        _offsetX = next;
        ApplyScrollOffsets();
    }

    public void ScrollToHome() { _offsetX = 0.0f; _offsetY = 0.0f; ApplyScrollOffsets(); }
    public void ScrollToEnd() { ScrollToVerticalOffset(ExtentHeight); }

    private void ApplyScrollOffsets() {
        if (_viewport != null && ParseUtil.IsValidHandle(_viewport.__nativeHandle)) {
            Visual_SetScrollOffsets(_viewport.__nativeHandle, _offsetX, _offsetY);
        }
        PopupService.InvalidatePositions();
        SyncScrollBarRanges();
        BindingEngine.NotifyTargetPropertyChanged(this, "VerticalOffset", DependencyObject.BoxFloat(_offsetY));
        BindingEngine.NotifyTargetPropertyChanged(this, "HorizontalOffset", DependencyObject.BoxFloat(_offsetX));
        ScrollChanged(this, new ScrollEventArgs(_offsetY, this));
    }

    private void SyncScrollBarRanges() {
        if (_syncingBars) { return; }
        _syncingBars = true;
        bool offsetClamped = false;
        float maxY = MaxEffectiveVerticalOffset();
        if (maxY >= 0.0f && (_offsetY > maxY || _offsetY < 0.0f)) {
            _offsetY = Math.Clampf(_offsetY, 0.0f, maxY);
            offsetClamped = true;
        }
        float maxX = MaxEffectiveHorizontalOffset();
        if (maxX >= 0.0f && (_offsetX > maxX || _offsetX < 0.0f)) {
            _offsetX = Math.Clampf(_offsetX, 0.0f, maxX);
            offsetClamped = true;
        }
        if (offsetClamped) {
            if (_viewport != null && ParseUtil.IsValidHandle(_viewport.__nativeHandle)) {
                Visual_SetScrollOffsets(_viewport.__nativeHandle, _offsetX, _offsetY);
            }
            BindingEngine.NotifyTargetPropertyChanged(this, "VerticalOffset", DependencyObject.BoxFloat(_offsetY));
            BindingEngine.NotifyTargetPropertyChanged(this, "HorizontalOffset", DependencyObject.BoxFloat(_offsetX));
        }
        if (_vBar != null) {
            _vBar.Maximum = maxY >= 0.0f ? maxY : Math.Maxf(0.0f, ExtentHeight - ViewportHeight);
            float vh = ViewportHeight;
            _vBar.ViewportSize = vh > 0.0f ? vh : 16.0f;
            _vBar.Value = _offsetY;
        }
        if (_hBar != null) {
            _hBar.Maximum = maxX >= 0.0f ? maxX : Math.Maxf(0.0f, ExtentWidth - ViewportWidth);
            _hBar.ViewportSize = Math.Maxf(16.0f, ViewportWidth);
            _hBar.Value = _offsetX;
        }
        SyncScrollBarVisibility();
        _syncingBars = false;
        if (offsetClamped) {
            ScrollChanged(this, new ScrollEventArgs(_offsetY, this));
        }
    }

    private int _lastCompVVis = -1;
    private int _lastCompHVis = -1;

    private void SyncScrollBarVisibility() {
        Visibility cv = ComputedVerticalScrollBarVisibility;
        Visibility ch = ComputedHorizontalScrollBarVisibility;
        if (_vBar != null) {
            _vBar.Visibility = cv;
        }
        if (_hBar != null) {
            _hBar.Visibility = ch;
        }
        int iv = EnumConvert.ToInt(cv);
        int ih = EnumConvert.ToInt(ch);
        if (iv != _lastCompVVis) {
            _lastCompVVis = iv;
            BindingEngine.NotifyTargetPropertyChanged(this, "ComputedVerticalScrollBarVisibility", DependencyObject.BoxInt(iv));
        }
        if (ih != _lastCompHVis) {
            _lastCompHVis = ih;
            BindingEngine.NotifyTargetPropertyChanged(this, "ComputedHorizontalScrollBarVisibility", DependencyObject.BoxInt(ih));
        }
    }

    public override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        if (e.Handled) { return; }
        float dy = Math.Absf(e.DeltaY) >= 20.0f ? (e.DeltaY / 120.0f) : e.DeltaY;
        float dx = Math.Absf(e.DeltaX) >= 20.0f ? (e.DeltaX / 120.0f) : e.DeltaX;
        float maxY = MaxEffectiveVerticalOffset();
        float maxX = MaxEffectiveHorizontalOffset();
        if (_vVis != ScrollBarVisibility.Disabled && dy != 0.0f && maxY > 0.0f) {
            ScrollToVerticalOffset(_offsetY - dy * 48.0f);
            e.Handled = true;
        } else if (_hVis != ScrollBarVisibility.Disabled && dx != 0.0f && maxX > 0.0f) {
            ScrollToHorizontalOffset(_offsetX - dx * 48.0f);
            e.Handled = true;
        }
    }

    private static ScrollContentPresenter FindDescendantScrollContentPresenter(UIElement rootNode) {
        if (rootNode == null) { return null; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node != rootNode && node is ScrollViewer) { continue; }
            if (node is ScrollContentPresenter) { return node as ScrollContentPresenter; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
        return null;
    }

    public override void OnApplyTemplate() {
        _lastSyncLayoutEpoch = -1;
        if (Template != null) {
            _presenter = null;
            _viewport = null;
            _vBar = null;
            _hBar = null;
            UIElement scpEl = GetTemplateChild("PART_ScrollContentPresenter");
            ScrollContentPresenter scp = (scpEl is ScrollContentPresenter)
                ? (scpEl as ScrollContentPresenter)
                : FindDescendantScrollContentPresenter(TemplateRoot());
            if (scp != null) {
                _presenter = scp;
                _viewport = scp;
                scp.ContentTemplate = ContentTemplate;
                scp.SetContentBox(ContentBox);
            }
            UIElement vb = GetTemplateChild("PART_VerticalScrollBar");
            if (vb is ScrollBar) {
                _vBar = vb as ScrollBar;
                _vBar.Scroll += (object s, ScrollEventArgs e) => {
                    if (!_syncingBars) { ScrollToVerticalOffset(e.NewValue); }
                };
            }
            UIElement hb = GetTemplateChild("PART_HorizontalScrollBar");
            if (hb is ScrollBar) {
                _hBar = hb as ScrollBar;
                _hBar.Scroll += (object s, ScrollEventArgs e) => {
                    if (!_syncingBars) { ScrollToHorizontalOffset(e.NewValue); }
                };
            }
            ApplyScrollOffsets();
            return;
        }
        base.OnApplyTemplate();
    }

    protected override UIElement CreateDefaultTemplate() {
        var grid = new Grid();
        grid.AddColumnDefinition(GridUnitType.Star, 1.0f);
        grid.AddRowDefinition(GridUnitType.Star, 1.0f);

        var defaultBorder = new Border();
        defaultBorder.ClipToBounds = true;
        if (ParseUtil.IsValidHandle(defaultBorder.__nativeHandle)) {
            Visual_SetLayoutMode(defaultBorder.__nativeHandle, EnumConvert.ToInt(LayoutMode.ScrollViewport), 0.0f, 0.0f, 1, 0);
        }
        _presenter = new ContentPresenter();
        _presenter.Name = "PART_ContentPresenter";
        _presenter.ContentTemplate = ContentTemplate;
        _presenter.SetContentBox(ContentBox);
        defaultBorder.Child = _presenter;
        _viewport = defaultBorder;

        _vBar = new ScrollBar();
        _vBar.Orientation = Orientation.Vertical;
        _vBar.Width = 15.0f;
        _vBar.HorizontalAlignment = HorizontalAlignment.Right;
        _vBar.VerticalAlignment = VerticalAlignment.Stretch;
        _vBar.ZIndex = 10;
        UIElement.SetGridColumn(_vBar, 0);
        _vBar.Scroll += (object s, ScrollEventArgs e) => {
            if (!_syncingBars) { ScrollToVerticalOffset(e.NewValue); }
        };

        _hBar = new ScrollBar();
        _hBar.Orientation = Orientation.Horizontal;
        _hBar.Height = 15.0f;
        _hBar.VerticalAlignment = VerticalAlignment.Bottom;
        _hBar.HorizontalAlignment = HorizontalAlignment.Stretch;
        _hBar.ZIndex = 10;
        UIElement.SetGridRow(_hBar, 0);
        _hBar.Scroll += (object s, ScrollEventArgs e) => {
            if (!_syncingBars) { ScrollToHorizontalOffset(e.NewValue); }
        };

        grid.AddChild(_viewport);
        grid.AddChild(_vBar);
        grid.AddChild(_hBar);
        SyncScrollBarVisibility();
        return grid;
    }

    private static ScrollBarVisibility ParseScrollVis(DependencyObject value, ScrollBarVisibility fallback) {
        if (value is BoxedInt) {
            int v = (value as BoxedInt).Value;
            if (v == 0) { return ScrollBarVisibility.Disabled; }
            if (v == 1) { return ScrollBarVisibility.Auto; }
            if (v == 2) { return ScrollBarVisibility.Hidden; }
            if (v == 3) { return ScrollBarVisibility.Visible; }
            return fallback;
        }
        string s = DependencyObject.UnboxString(value, "");
        if (s == "Disabled") { return ScrollBarVisibility.Disabled; }
        if (s == "Auto") { return ScrollBarVisibility.Auto; }
        if (s == "Hidden") { return ScrollBarVisibility.Hidden; }
        if (s == "Visible") { return ScrollBarVisibility.Visible; }
        return fallback;
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "VerticalOffset") { VerticalOffset = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "HorizontalOffset") { HorizontalOffset = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "VerticalScrollBarVisibility" || propName == "ScrollViewer.VerticalScrollBarVisibility") {
            VerticalScrollBarVisibility = ParseScrollVis(value, _vVis);
            return true;
        }
        if (propName == "HorizontalScrollBarVisibility" || propName == "ScrollViewer.HorizontalScrollBarVisibility") {
            HorizontalScrollBarVisibility = ParseScrollVis(value, _hVis);
            return true;
        }
        if (propName == "CanContentScroll" || propName == "ScrollViewer.CanContentScroll") {
            CanContentScroll = DependencyObject.UnboxBool(value, false);
            return true;
        }
        return base.SetPropertyValue(propName, value);
    }

    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "VerticalOffset") { return DependencyObject.BoxFloat(_offsetY); }
        if (propName == "HorizontalOffset") { return DependencyObject.BoxFloat(_offsetX); }
        if (propName == "ViewportHeight") { return DependencyObject.BoxFloat(ViewportHeight); }
        if (propName == "ViewportWidth") { return DependencyObject.BoxFloat(ViewportWidth); }
        if (propName == "ExtentHeight") { return DependencyObject.BoxFloat(ExtentHeight); }
        if (propName == "ExtentWidth") { return DependencyObject.BoxFloat(ExtentWidth); }
        if (propName == "ScrollableHeight") { return DependencyObject.BoxFloat(ScrollableHeight); }
        if (propName == "ScrollableWidth") { return DependencyObject.BoxFloat(ScrollableWidth); }
        if (propName == "ComputedVerticalScrollBarVisibility") {
            return DependencyObject.BoxInt(EnumConvert.ToInt(ComputedVerticalScrollBarVisibility));
        }
        if (propName == "ComputedHorizontalScrollBarVisibility") {
            return DependencyObject.BoxInt(EnumConvert.ToInt(ComputedHorizontalScrollBarVisibility));
        }
        if (propName == "VerticalScrollBarVisibility" || propName == "ScrollViewer.VerticalScrollBarVisibility") {
            return DependencyObject.BoxInt(EnumConvert.ToInt(_vVis));
        }
        if (propName == "HorizontalScrollBarVisibility" || propName == "ScrollViewer.HorizontalScrollBarVisibility") {
            return DependencyObject.BoxInt(EnumConvert.ToInt(_hVis));
        }
        if (propName == "CanContentScroll" || propName == "ScrollViewer.CanContentScroll") {
            return DependencyObject.BoxBool(_canContentScroll);
        }
        return base.GetPropertyValue(propName);
    }
}
