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
        if (ev == "Click") {
            Click += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
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
        if (Style != null || Template != null) { return; }
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
    private string _routedCommandName = "";

    public RepeatButton() : base() {
        ClickMode = ClickMode.Press;
    }
    public override string GetTypeName() { return "RepeatButton"; }

    public void TickRepeat() {
        if (IsEnabled && IsPressed) { OnClick(); }
    }

    private ScrollBar FindAncestorScrollBar() {
        UIElement cur = TemplatedParent != null ? TemplatedParent : Parent;
        while (cur != null) {
            if (cur is ScrollBar) { return cur as ScrollBar; }
            cur = cur.TemplatedParent != null ? cur.TemplatedParent : cur.Parent;
        }
        return null;
    }

    protected override void OnClick() {
        base.OnClick();
        if (_routedCommandName.Length == 0) { return; }
        ScrollBar sb = FindAncestorScrollBar();
        if (sb == null) { return; }
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
    }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Delay") { Delay = DependencyObject.UnboxInt(value, 500); return true; }
        if (propName == "Interval") { Interval = DependencyObject.UnboxInt(value, 100); return true; }
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

    public override bool SubscribeBehaviorRoutedEvent(string ev, BehaviorEventSink sink) {
        if (ev == "Checked") {
            Checked += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        if (ev == "Unchecked") {
            Unchecked += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
        if (ev == "Indeterminate") {
            Indeterminate += (object s, RoutedEventArgs e) => { if (sink != null) { sink.OnBehaviorEvent(s, e); } };
            return true;
        }
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
            UpdateTriggersAndStates();
        }
    }

    protected virtual void OnCheckedChanged() {
        UpdateVisualCheckedState();
        UpdateTriggersAndStates();
        if (_isChecked) {
            Checked(this, new RoutedEventArgs("Checked", this));
        } else {
            Unchecked(this, new RoutedEventArgs("Unchecked", this));
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
        if (Style == null && Template == null) {
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
        set { _orientation = value; UpdateTrackVisuals(); }
    }
    public float Minimum {
        get { return _min; }
        set { _min = value; UpdateTrackVisuals(); }
    }
    public float Maximum {
        get { return _max; }
        set { _max = value; UpdateTrackVisuals(); }
    }
    public float Value {
        get { return _value; }
        set { _value = value; UpdateTrackVisuals(); }
    }
    public float ViewportSize {
        get { return _viewportSize; }
        set { _viewportSize = value; UpdateTrackVisuals(); }
    }
    public bool IsDirectionReversed {
        get { return _isDirectionReversed; }
        set { _isDirectionReversed = value; UpdateTrackVisuals(); }
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

        if (_viewportSize <= 0.0f || ParseUtil.IsNaN(_viewportSize)) {
            float w0 = beforeFrac;
            float w2 = 1.0f - beforeFrac;
            if (_decreaseRepeatButton != null) {
                _decreaseRepeatButton.Visibility = w0 > 0.0001f ? Visibility.Visible : Visibility.Collapsed;
            }
            if (_increaseRepeatButton != null) {
                _increaseRepeatButton.Visibility = w2 > 0.0001f ? Visibility.Visible : Visibility.Collapsed;
            }
            if (_orientation == Orientation.Vertical) {
                Visual_GridAddRowDef(__nativeHandle, w0 > 0.0001f ? starUnit : pxUnit, w0 > 0.0001f ? w0 : 0.0f);
                Visual_GridAddRowDef(__nativeHandle, autoUnit, 1.0f);
                Visual_GridAddRowDef(__nativeHandle, w2 > 0.0001f ? starUnit : pxUnit, w2 > 0.0001f ? w2 : 0.0f);
                if (_decreaseRepeatButton != null) {
                    UIElement.SetGridRow(_decreaseRepeatButton, 0);
                    UIElement.SetGridColumn(_decreaseRepeatButton, 0);
                }
                if (_thumb != null) {
                    UIElement.SetGridRow(_thumb, 1);
                    UIElement.SetGridColumn(_thumb, 0);
                }
                if (_increaseRepeatButton != null) {
                    UIElement.SetGridRow(_increaseRepeatButton, 2);
                    UIElement.SetGridColumn(_increaseRepeatButton, 0);
                }
            } else {
                Visual_GridAddColDef(__nativeHandle, w0 > 0.0001f ? starUnit : pxUnit, w0 > 0.0001f ? w0 : 0.0f);
                Visual_GridAddColDef(__nativeHandle, autoUnit, 1.0f);
                Visual_GridAddColDef(__nativeHandle, w2 > 0.0001f ? starUnit : pxUnit, w2 > 0.0001f ? w2 : 0.0f);
                if (_decreaseRepeatButton != null) {
                    UIElement.SetGridRow(_decreaseRepeatButton, 0);
                    UIElement.SetGridColumn(_decreaseRepeatButton, 0);
                }
                if (_thumb != null) {
                    UIElement.SetGridRow(_thumb, 0);
                    UIElement.SetGridColumn(_thumb, 1);
                }
                if (_increaseRepeatButton != null) {
                    UIElement.SetGridRow(_increaseRepeatButton, 0);
                    UIElement.SetGridColumn(_increaseRepeatButton, 2);
                }
            }
            return;
        }

        if (_decreaseRepeatButton != null) { _decreaseRepeatButton.Visibility = Visibility.Visible; }
        if (_increaseRepeatButton != null) { _increaseRepeatButton.Visibility = Visibility.Visible; }
        float thumbRatio = (span > 0.00001f) ? Math.Clampf(_viewportSize / (span + _viewportSize), 0.08f, 1.0f) : 1.0f;
        float freeRatio = Math.Maxf(0.0f, 1.0f - thumbRatio);
        float sv0 = freeRatio * beforeFrac;
        float sv1 = thumbRatio;
        float sv2 = freeRatio * (1.0f - beforeFrac);
        if (_orientation == Orientation.Vertical) {
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv0);
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv1);
            Visual_GridAddRowDef(__nativeHandle, starUnit, sv2);
            if (_decreaseRepeatButton != null) {
                UIElement.SetGridRow(_decreaseRepeatButton, 0);
                UIElement.SetGridColumn(_decreaseRepeatButton, 0);
            }
            if (_thumb != null) {
                UIElement.SetGridRow(_thumb, 1);
                UIElement.SetGridColumn(_thumb, 0);
            }
            if (_increaseRepeatButton != null) {
                UIElement.SetGridRow(_increaseRepeatButton, 2);
                UIElement.SetGridColumn(_increaseRepeatButton, 0);
            }
        } else {
            Visual_GridAddColDef(__nativeHandle, starUnit, sv0);
            Visual_GridAddColDef(__nativeHandle, starUnit, sv1);
            Visual_GridAddColDef(__nativeHandle, starUnit, sv2);
            if (_decreaseRepeatButton != null) {
                UIElement.SetGridRow(_decreaseRepeatButton, 0);
                UIElement.SetGridColumn(_decreaseRepeatButton, 0);
            }
            if (_thumb != null) {
                UIElement.SetGridRow(_thumb, 0);
                UIElement.SetGridColumn(_thumb, 1);
            }
            if (_increaseRepeatButton != null) {
                UIElement.SetGridRow(_increaseRepeatButton, 0);
                UIElement.SetGridColumn(_increaseRepeatButton, 2);
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

public class TickBar : FrameworkElement {
    public Brush Fill { get; set; }
    public string Placement { get; set; } = "Top";
    public float Minimum { get; set; } = 0.0f;
    public float Maximum { get; set; } = 10.0f;
    public float TickFrequency { get; set; } = 1.0f;
    public float ReservedSpace { get; set; } = 0.0f;
    public bool IsDirectionReversed { get; set; } = false;

    public TickBar() : base() {}
    public override string GetTypeName() { return "TickBar"; }

    public override bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == "Fill") { Fill = UIElement.CoerceBrush(value); return true; }
        if (propName == "Placement") { Placement = DependencyObject.UnboxString(value, "Top"); return true; }
        if (propName == "Minimum") { Minimum = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "Maximum") { Maximum = DependencyObject.UnboxFloat(value, 10.0f); return true; }
        if (propName == "TickFrequency") { TickFrequency = DependencyObject.UnboxFloat(value, 1.0f); return true; }
        if (propName == "ReservedSpace") { ReservedSpace = DependencyObject.UnboxFloat(value, 0.0f); return true; }
        if (propName == "IsDirectionReversed") { IsDirectionReversed = DependencyObject.UnboxBool(value, false); return true; }
        return base.SetPropertyValue(propName, value);
    }
    public override DependencyObject GetPropertyValue(string propName) {
        if (propName == "Fill") { return Fill; }
        if (propName == "Placement") { return DependencyObject.BoxString(Placement); }
        if (propName == "Minimum") { return DependencyObject.BoxFloat(Minimum); }
        if (propName == "Maximum") { return DependencyObject.BoxFloat(Maximum); }
        if (propName == "TickFrequency") { return DependencyObject.BoxFloat(TickFrequency); }
        if (propName == "ReservedSpace") { return DependencyObject.BoxFloat(ReservedSpace); }
        if (propName == "IsDirectionReversed") { return DependencyObject.BoxBool(IsDirectionReversed); }
        return base.GetPropertyValue(propName);
    }
}

public class RangeBase : Control {
    private float _min = 0.0f;
    private float _max = 100.0f;
    private float _value = 0.0f;
    private float _desiredValue = 0.0f;
    public float SmallChange { get; set; } = 1.0f;
    public float LargeChange { get; set; } = 10.0f;

    public event RoutedPropertyChangedEventHandler<float> ValueChanged;

    public RangeBase() : base() {}
    public override string GetTypeName() { return "RangeBase"; }

    private void ApplyClampedValue() {
        float clamped = Math.Clampf(_desiredValue, _min, _max);
        if (_value == clamped) { return; }
        float old = _value;
        _value = clamped;
        OnValueChanged(old, clamped);
        ValueChanged(this, new RoutedPropertyChangedEventArgs<float>(old, clamped, this));
        BindingEngine.NotifyTargetPropertyChanged(this, "Value", DependencyObject.BoxFloat(clamped));
        NotifyStateInvalidatedHooks();
    }

    public float Minimum {
        get { return _min; }
        set {
            _min = value;
            if (_max < _min) { _max = _min; }
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
    private float _tickFrequency = 0.0f;
    public bool IsSnapToTickEnabled { get; set; } = false;
    public bool IsDirectionReversed { get; set; } = false;

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
        RegisterDefaultSlider(this);
    }
    public Slider(float min, float max, float val) : this() {
        Minimum = min;
        Maximum = max;
        Value = val;
    }

    private static void RegisterDefaultSlider(Slider s) {
        if (_defaultSliders == null) {
            _defaultSliders = new Slider[0];
            BindingEngine.AddPreFlushHook(() => { Slider.SyncAllDefaultLayouts(); });
        }
        if (!_defaultSliders.Contains(s)) { _defaultSliders.Push(s); }
    }

    public static void SyncAllDefaultLayouts() {
        if (_defaultSliders == null) { return; }
        for (int i = _defaultSliders.Length - 1; i >= 0; i -= 1) {
            Slider s = _defaultSliders[i];
            if (s == null || s._isDisposedTree) {
                _defaultSliders.RemoveAt(i);
                continue;
            }
            if (s._thumb == null) { continue; }
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
        set { _orientation = value; OnRangeUpdated(); }
    }
    public float TickFrequency {
        get { return _tickFrequency; }
        set { _tickFrequency = value >= 0.0f ? value : 0.0f; }
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
                    _partTrack.Thumb.DragDelta += (object s, DragDeltaEventArgs e) => {
                        float w = Math.Maxf(1.0f, ActualWidth);
                        float h = Math.Maxf(1.0f, ActualHeight);
                        float span = Maximum - Minimum;
                        if (span <= 0.00001f) { return; }
                        float deltaFrac = _orientation == Orientation.Horizontal
                            ? (e.HorizontalChange / w)
                            : (-e.VerticalChange / h);
                        if (IsDirectionReversed) { deltaFrac = -deltaFrac; }
                        Value = Value + deltaFrac * span;
                    };
                }
                if (_partTrack.DecreaseRepeatButton != null) {
                    _partTrack.DecreaseRepeatButton.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                        UpdateFromPointer(e.X, e.Y);
                    };
                }
                if (_partTrack.IncreaseRepeatButton != null) {
                    _partTrack.IncreaseRepeatButton.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => {
                        UpdateFromPointer(e.X, e.Y);
                    };
                }
            }
        }
    }

    protected override void OnRangeUpdated() {
        SyncPartTrack();
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
        if (IsSnapToTickEnabled && _tickFrequency > 0.0001f) {
            float steps = (raw - Minimum) / _tickFrequency;
            int snapped = (int)(steps + 0.5f);
            raw = Minimum + (float)snapped * _tickFrequency;
        }
        Value = raw;
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

    public override void OnKeyDown(KeyEventArgs e) {
        base.OnKeyDown(e);
        if (!IsEnabled) { return; }
        if (e.Key == KeyCode.Left || e.Key == KeyCode.Down) { Value = Value - SmallChange; e.Handled = true; }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.Up) { Value = Value + SmallChange; e.Handled = true; }
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
        if (propName == "TickFrequency") { TickFrequency = DependencyObject.UnboxFloat(value, 0.0f); return true; }
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
        if (propName == "TickFrequency") { return DependencyObject.BoxFloat(_tickFrequency); }
        if (propName == "Orientation") { return DependencyObject.BoxInt(EnumConvert.ToInt(_orientation)); }
        return base.GetPropertyValue(propName);
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

    private static void RegisterDefaultBar(ScrollBar sb) {
        if (_defaultBars == null) {
            _defaultBars = new ScrollBar[0];
            BindingEngine.AddPreFlushHook(() => { ScrollBar.SyncAllDefaultLayouts(); });
        }
        if (!_defaultBars.Contains(sb)) { _defaultBars.Push(sb); }
    }

    public static void SyncAllDefaultLayouts() {
        if (_defaultBars == null) { return; }
        for (int i = _defaultBars.Length - 1; i >= 0; i -= 1) {
            ScrollBar sb = _defaultBars[i];
            if (sb == null || sb._isDisposedTree) {
                _defaultBars.RemoveAt(i);
                continue;
            }
            if (sb._thumb == null || sb.Template != null) { continue; }
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
        set { _orientation = value; OnRangeUpdated(); }
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

    private static void RegisterInstance(ScrollViewer sv) {
        if (_instances == null) {
            _instances = new ScrollViewer[0];
            BindingEngine.AddPreFlushHook(() => { ScrollViewer.SyncAllMetrics(); });
        }
        for (int i = 0; i < _instances.Length; i += 1) {
            if (_instances[i] == sv) { return; }
        }
        _instances.Push(sv);
    }

    public static void SyncAllMetrics() {
        if (_instances == null) { return; }
        for (int i = _instances.Length - 1; i >= 0; i -= 1) {
            ScrollViewer sv = _instances[i];
            if (sv == null || sv._isDisposedTree) {
                _instances.RemoveAt(i);
                continue;
            }
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

    private void SyncScrollBarVisibility() {
        if (_vBar != null) {
            _vBar.Visibility = ComputedVerticalScrollBarVisibility;
        }
        if (_hBar != null) {
            _hBar.Visibility = ComputedHorizontalScrollBarVisibility;
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
