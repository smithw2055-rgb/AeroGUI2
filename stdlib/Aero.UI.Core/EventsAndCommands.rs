module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. Standard Delegates & Event Handler Delegates
// ============================================================================

public delegate void Action();
public delegate void Action<T>(T arg);
public delegate void Action<T1, T2>(T1 arg1, T2 arg2);
public delegate TResult Func<TResult>();
public delegate TResult Func<T, TResult>(T arg);
public delegate TResult Func<T1, T2, TResult>(T1 arg1, T2 arg2);
public delegate bool Predicate<T>(T value);

public delegate void EventHandler(object sender, EventArgs e);
public delegate void EventHandler<TEventArgs>(object sender, TEventArgs e);
public delegate void PropertyChangedEventHandler(object sender, PropertyChangedEventArgs e);

public delegate void RoutedEventHandler(object sender, RoutedEventArgs e);
public delegate void TextChangedEventHandler(object sender, TextChangedEventArgs e);
public delegate void SelectionChangedEventHandler(object sender, SelectionChangedEventArgs e);
public delegate void KeyEventHandler(object sender, KeyEventArgs e);
public delegate void MouseEventHandler(object sender, MouseEventArgs e);
public delegate void MouseButtonEventHandler(object sender, MouseButtonEventArgs e);
public delegate void MouseWheelEventHandler(object sender, MouseWheelEventArgs e);
public delegate void TextCompositionEventHandler(object sender, TextCompositionEventArgs e);
public delegate void RequestNavigateEventHandler(object sender, RequestNavigateEventArgs e);
public delegate void RoutedPropertyChangedEventHandler<T>(object sender, RoutedPropertyChangedEventArgs<T> e);
public delegate void SizeChangedEventHandler(object sender, SizeChangedEventArgs e);
public delegate void DependencyPropertyChangedEventHandler(object sender, DependencyPropertyChangedEventArgs e);
public delegate void StartupEventHandler(object sender, StartupEventArgs e);
public delegate void CancelEventHandler(object sender, CancelEventArgs e);
public delegate void DragStartedEventHandler(object sender, DragStartedEventArgs e);
public delegate void DragDeltaEventHandler(object sender, DragDeltaEventArgs e);
public delegate void DragCompletedEventHandler(object sender, DragCompletedEventArgs e);
public delegate void ScrollEventHandler(object sender, ScrollEventArgs e);

// ============================================================================
// 2. EventArgs & RoutedEventArgs Hierarchy
// ============================================================================

public class EventArgs : DependencyObject {
    public static EventArgs Empty() { return new EventArgs(); }
}

public class PropertyChangedEventArgs : EventArgs {
    public string PropertyName { get; set; } = "";
    public PropertyChangedEventArgs() {}
    public PropertyChangedEventArgs(string propertyName) {
        PropertyName = propertyName == null ? "" : propertyName;
    }
}

public class CancelEventArgs : EventArgs {
    public bool Cancel { get; set; }
    public CancelEventArgs() {}
    public CancelEventArgs(bool cancel) { Cancel = cancel; }
}

public class StartupEventArgs : EventArgs {
    public string[] Args { get; set; }
    public StartupEventArgs() { Args = new string[0]; }
    public StartupEventArgs(string[] args) { Args = args == null ? new string[0] : args; }
}

public class RoutedEventArgs : EventArgs {
    private DependencyObject _source;
    private DependencyObject _originalSource;

    public bool Handled { get; set; }
    public string RoutedEvent { get; set; } = "";

    public RoutedEventArgs() {}
    public RoutedEventArgs(string routedEvent) {
        RoutedEvent = routedEvent == null ? "" : routedEvent;
    }
    public RoutedEventArgs(string routedEvent, object source) {
        RoutedEvent = routedEvent == null ? "" : routedEvent;
        _source = DependencyObject.BoxObject(source);
        _originalSource = _source;
    }

    public object Source {
        get { return DependencyObject.Unbox(_source); }
        set { _source = DependencyObject.BoxObject(value); }
    }
    public object OriginalSource {
        get { return DependencyObject.Unbox(_originalSource); }
        set { _originalSource = DependencyObject.BoxObject(value); }
    }
}

public class TextChangedEventArgs : RoutedEventArgs {
    public string Text { get; set; } = "";
    public TextChangedEventArgs() : base("TextChanged") {}
    public TextChangedEventArgs(string text, object source = null) : base("TextChanged", source) {
        Text = text == null ? "" : text;
    }
}

public class SelectionChangedEventArgs : RoutedEventArgs {
    private DependencyObject[] _addedItems;
    private DependencyObject[] _removedItems;

    public int SelectedIndex { get; set; } = -1;

    public SelectionChangedEventArgs() : base("SelectionChanged") {
        _addedItems = new DependencyObject[0];
        _removedItems = new DependencyObject[0];
    }
    public SelectionChangedEventArgs(
        object source, int selectedIndex, DependencyObject removed, DependencyObject added)
        : base("SelectionChanged", source) {
        SelectedIndex = selectedIndex;
        _addedItems = new DependencyObject[0];
        _removedItems = new DependencyObject[0];
        if (removed != null) { _removedItems.Push(removed); }
        if (added != null) { _addedItems.Push(added); }
    }

    public DependencyObject[] AddedItemBoxes() { return _addedItems; }
    public DependencyObject[] RemovedItemBoxes() { return _removedItems; }
    public object AddedItem() {
        if (_addedItems.Length > 0) { return DependencyObject.Unbox(_addedItems[0]); }
        return null;
    }
    public object RemovedItem() {
        if (_removedItems.Length > 0) { return DependencyObject.Unbox(_removedItems[0]); }
        return null;
    }
}

public class KeyEventArgs : RoutedEventArgs {
    public KeyCode Key { get; set; } = KeyCode.None;
    public int Modifiers { get; set; }
    public bool IsDown { get; set; }
    public bool IsRepeat { get; set; }

    public KeyEventArgs() : base("KeyDown") {}
    public KeyEventArgs(string routedEvent, object source, KeyCode key, int modifiers, bool isDown)
        : base(routedEvent, source) {
        Key = key;
        Modifiers = modifiers;
        IsDown = isDown;
    }

    public bool IsUp { get { return !IsDown; } }
    public bool IsShiftDown { get { return KeyModifiers.Has(Modifiers, KeyModifiers.Shift()); } }
    public bool IsControlDown { get { return KeyModifiers.Has(Modifiers, KeyModifiers.Ctrl()); } }
    public bool IsAltDown { get { return KeyModifiers.Has(Modifiers, KeyModifiers.Alt()); } }
}

public class MouseEventArgs : RoutedEventArgs {
    public float X { get; set; }
    public float Y { get; set; }
    public int Modifiers { get; set; }

    public MouseEventArgs() : base("MouseMove") {}
    public MouseEventArgs(string routedEvent, object source, float x, float y, int modifiers = 0)
        : base(routedEvent, source) {
        X = x;
        Y = y;
        Modifiers = modifiers;
    }

    public Point GetPosition(UIElement relativeTo) {
        if (relativeTo != null && ParseUtil.IsValidHandle(relativeTo.__nativeHandle)) {
            float lx = Visual_ScreenToLocal(relativeTo.__nativeHandle, X, Y, 0);
            float ly = Visual_ScreenToLocal(relativeTo.__nativeHandle, X, Y, 1);
            return new Point(lx, ly);
        }
        return new Point(X, Y);
    }
}

public class MouseButtonEventArgs : MouseEventArgs {
    public PointerButton ChangedButton { get; set; } = PointerButton.Left;
    public bool IsDown { get; set; }
    public int ClickCount { get; set; } = 1;

    public MouseButtonEventArgs() : base("MouseLeftButtonDown", null, 0.0f, 0.0f, 0) {}
    public MouseButtonEventArgs(
        string routedEvent, object source, float x, float y, int modifiers,
        PointerButton button, bool isDown, int clickCount = 1)
        : base(routedEvent, source, x, y, modifiers) {
        ChangedButton = button;
        IsDown = isDown;
        ClickCount = clickCount;
    }
}

public class MouseWheelEventArgs : MouseEventArgs {
    public float DeltaX { get; set; }
    public float DeltaY { get; set; }
    public int Delta { get { return (int)(DeltaY * 120.0f); } }

    public MouseWheelEventArgs() : base("MouseWheel", null, 0.0f, 0.0f, 0) {}
    public MouseWheelEventArgs(
        string routedEvent, object source, float x, float y, int modifiers, float dx, float dy)
        : base(routedEvent, source, x, y, modifiers) {
        DeltaX = dx;
        DeltaY = dy;
    }
}

public class TextCompositionEventArgs : RoutedEventArgs {
    public string Text { get; set; } = "";
    public TextCompositionEventArgs() : base("TextInput") {}
    public TextCompositionEventArgs(string routedEvent, object source, string text)
        : base(routedEvent, source) {
        Text = text == null ? "" : text;
    }
}

public class RequestNavigateEventArgs : RoutedEventArgs {
    public string Uri { get; set; } = "";
    public string TargetName { get; set; } = "";
    public RequestNavigateEventArgs() : base("RequestNavigate") {}
    public RequestNavigateEventArgs(string uri, string targetName = "", object source = null)
        : base("RequestNavigate", source) {
        Uri = uri == null ? "" : uri;
        TargetName = targetName == null ? "" : targetName;
    }
}

public class RoutedPropertyChangedEventArgs<T> : RoutedEventArgs {
    public T OldValue { get; set; }
    public T NewValue { get; set; }
    public RoutedPropertyChangedEventArgs() : base("ValueChanged") {}
    public RoutedPropertyChangedEventArgs(T oldVal, T newVal, object source = null)
        : base("ValueChanged", source) {
        OldValue = oldVal;
        NewValue = newVal;
    }
}

public class SizeChangedEventArgs : RoutedEventArgs {
    public Size PreviousSize { get; set; }
    public Size NewSize { get; set; }
    public bool WidthChanged { get; set; }
    public bool HeightChanged { get; set; }

    public SizeChangedEventArgs(Size prevSize, Size newSize, object source = null)
        : base("SizeChanged", source) {
        PreviousSize = prevSize == null ? new Size(0.0f, 0.0f) : prevSize;
        NewSize = newSize == null ? new Size(0.0f, 0.0f) : newSize;
        WidthChanged = PreviousSize.Width != NewSize.Width;
        HeightChanged = PreviousSize.Height != NewSize.Height;
    }
}

public class DependencyPropertyChangedEventArgs : EventArgs {
    private DependencyObject _oldBox;
    private DependencyObject _newBox;
    public string Property { get; set; } = "";

    public DependencyPropertyChangedEventArgs() {}
    public DependencyPropertyChangedEventArgs(string prop, object oldVal, object newVal) {
        Property = prop == null ? "" : prop;
        _oldBox = DependencyObject.BoxObject(oldVal);
        _newBox = DependencyObject.BoxObject(newVal);
    }

    internal static DependencyPropertyChangedEventArgs FromBoxes(
        string prop, DependencyObject oldBox, DependencyObject newBox) {
        var args = new DependencyPropertyChangedEventArgs();
        args.Property = prop == null ? "" : prop;
        args._oldBox = oldBox;
        args._newBox = newBox;
        return args;
    }

    public object OldValue { get { return DependencyObject.Unbox(_oldBox); } }
    public object NewValue { get { return DependencyObject.Unbox(_newBox); } }
}

public class DragStartedEventArgs : RoutedEventArgs {
    public float HorizontalOffset { get; set; }
    public float VerticalOffset { get; set; }
    public DragStartedEventArgs(float hx, float vy, object source = null) : base("DragStarted", source) {
        HorizontalOffset = hx;
        VerticalOffset = vy;
    }
}

public class DragDeltaEventArgs : RoutedEventArgs {
    public float HorizontalChange { get; set; }
    public float VerticalChange { get; set; }
    public DragDeltaEventArgs(float dx, float dy, object source = null) : base("DragDelta", source) {
        HorizontalChange = dx;
        VerticalChange = dy;
    }
}

public class DragCompletedEventArgs : RoutedEventArgs {
    public float HorizontalChange { get; set; }
    public float VerticalChange { get; set; }
    public bool Canceled { get; set; }
    public DragCompletedEventArgs(float dx, float dy, bool canceled, object source = null)
        : base("DragCompleted", source) {
        HorizontalChange = dx;
        VerticalChange = dy;
        Canceled = canceled;
    }
}

public class ScrollEventArgs : RoutedEventArgs {
    public float NewValue { get; set; }
    public ScrollEventArgs(float newVal, object source = null) : base("Scroll", source) {
        NewValue = newVal;
    }
}

// ============================================================================
// 3. ICommand, RelayCommand / DelegateCommand (Non-Generic & Generic<T>)
// ============================================================================

public interface ICommand {
    bool CanExecute(object parameter);
    void Execute(object parameter);
}

public class RelayCommand : DependencyObject, ICommand {
    private Action _exec0;
    private Func<bool> _can0;
    private Action<object> _exec1;
    private Func<object, bool> _can1;
    private Action[] _simpleListeners;

    public event EventHandler CanExecuteChanged;

    public RelayCommand() {
        _simpleListeners = new Action[0];
    }
    public RelayCommand(Action execute) : this() {
        _exec0 = execute;
    }
    public RelayCommand(Action execute, Func<bool> canExecute) : this() {
        _exec0 = execute;
        _can0 = canExecute;
    }
    public RelayCommand(Action<object> execute) : this() {
        _exec1 = execute;
    }
    public RelayCommand(Action<object> execute, Func<object, bool> canExecute) : this() {
        _exec1 = execute;
        _can1 = canExecute;
    }

    public void SetExecute(Action execute) { _exec0 = execute; _exec1 = null; }
    public void SetExecuteWithParam(Action<object> execute) { _exec1 = execute; _exec0 = null; }
    public void SetCanExecute(Func<bool> canExecute) { _can0 = canExecute; _can1 = null; RaiseCanExecuteChanged(); }
    public void SetCanExecuteWithParam(Func<object, bool> canExecute) { _can1 = canExecute; _can0 = null; RaiseCanExecuteChanged(); }
    public void SetExecuteFunc(Action execute) { SetExecute(execute); }
    public void SetExecuteFuncWithParam(Action<object> execute) { SetExecuteWithParam(execute); }
    public void SetCanExecuteFunc(Func<bool> canExecute) { SetCanExecute(canExecute); }
    public void SetCanExecuteFuncWithParam(Func<object, bool> canExecute) { SetCanExecuteWithParam(canExecute); }

    public virtual bool CanExecute(object parameter) {
        if (_can1 != null) { return _can1(parameter); }
        if (_can0 != null) { return _can0(); }
        return true;
    }

    public virtual void Execute(object parameter) {
        if (!CanExecute(parameter)) { return; }
        if (_exec1 != null) { _exec1(parameter); return; }
        if (_exec0 != null) { _exec0(); }
    }

    public void SubscribeCanExecuteChanged(Action listener) {
        if (listener == null) { return; }
        for (int i = 0; i < _simpleListeners.Length; i += 1) {
            if (_simpleListeners[i] == listener) { return; }
        }
        _simpleListeners.Push(listener);
    }

    public void UnsubscribeCanExecuteChanged(Action listener) {
        for (int i = 0; i < _simpleListeners.Length; i += 1) {
            if (_simpleListeners[i] == listener) {
                _simpleListeners.RemoveAt(i);
                return;
            }
        }
    }

    public void RaiseCanExecuteChanged() {
        CanExecuteChanged(this, EventArgs.Empty());
        for (int i = 0; i < _simpleListeners.Length; i += 1) {
            var fn = _simpleListeners[i];
            if (fn != null) { fn(); }
        }
    }
}

public class DelegateCommand : RelayCommand {
    public DelegateCommand() : base() {}
    public DelegateCommand(Action execute) : base(execute) {}
    public DelegateCommand(Action execute, Func<bool> canExecute) : base(execute, canExecute) {}
    public DelegateCommand(Action<object> execute) : base(execute) {}
    public DelegateCommand(Action<object> execute, Func<object, bool> canExecute) : base(execute, canExecute) {}
}

public class RelayCommand<T> : DependencyObject, ICommand {
    private Action<T> _exec;
    private Func<T, bool> _can;
    public event EventHandler CanExecuteChanged;

    public RelayCommand(Action<T> execute) { _exec = execute; }
    public RelayCommand(Action<T> execute, Func<T, bool> canExecute) {
        _exec = execute;
        _can = canExecute;
    }

    public virtual bool CanExecute(T parameter) {
        if (_can == null) { return true; }
        return _can(parameter);
    }

    public virtual void Execute(T parameter) {
        if (!CanExecute(parameter)) { return; }
        if (_exec != null) { _exec(parameter); }
    }

    public virtual bool CanExecute(object parameter) {
        var box = parameter as TypedBox<T>;
        if (box != null) { return CanExecute(box.Value); }
        return _can == null;
    }

    public virtual void Execute(object parameter) {
        var box = parameter as TypedBox<T>;
        if (box != null) { Execute(box.Value); }
    }

    public void RaiseCanExecuteChanged() {
        CanExecuteChanged(this, EventArgs.Empty());
    }
}

public class DelegateCommand<T> : RelayCommand<T> {
    public DelegateCommand(Action<T> execute) : base(execute) {}
    public DelegateCommand(Action<T> execute, Func<T, bool> canExecute) : base(execute, canExecute) {}
}

public static class ApplicationCommands {
    private static RelayCommand _close;
    private static RelayCommand _copy;
    private static RelayCommand _cut;
    private static RelayCommand _paste;
    private static RelayCommand _selectAll;
    private static RelayCommand _save;
    private static RelayCommand _open;
    private static RelayCommand _newCmd;
    private static RelayCommand _delete;

    public static RelayCommand Close() {
        if (_close == null) { _close = new RelayCommand(() => { Host_CloseWindow(); }); }
        return _close;
    }
    public static RelayCommand Copy() {
        if (_copy == null) { _copy = new RelayCommand(); }
        return _copy;
    }
    public static RelayCommand Cut() {
        if (_cut == null) { _cut = new RelayCommand(); }
        return _cut;
    }
    public static RelayCommand Paste() {
        if (_paste == null) { _paste = new RelayCommand(); }
        return _paste;
    }
    public static RelayCommand SelectAll() {
        if (_selectAll == null) { _selectAll = new RelayCommand(); }
        return _selectAll;
    }
    public static RelayCommand Save() {
        if (_save == null) { _save = new RelayCommand(); }
        return _save;
    }
    public static RelayCommand Open() {
        if (_open == null) { _open = new RelayCommand(); }
        return _open;
    }
    public static RelayCommand New() {
        if (_newCmd == null) { _newCmd = new RelayCommand(); }
        return _newCmd;
    }
    public static RelayCommand Delete() {
        if (_delete == null) { _delete = new RelayCommand(); }
        return _delete;
    }
}

// ============================================================================
// 4. INotifyPropertyChanged & MVVM Base Classes
// ============================================================================

public interface INotifyPropertyChanged {
    void NotifyPropertyChanged(string propertyName);
}

public class ObservableObject : DependencyObject, INotifyPropertyChanged {
    private int _notifyEpoch = 0;
    public event PropertyChangedEventHandler PropertyChanged;

    public virtual void NotifyPropertyChanged(string propertyName = "") {
        _notifyEpoch = _notifyEpoch >= 1000000000 ? 1 : (_notifyEpoch + 1);
        BindingEngine.BumpManagedVmEpoch();
        PropertyChanged(this, new PropertyChangedEventArgs(propertyName));
    }

    public void OnPropertyChanged(string propertyName = "") {
        NotifyPropertyChanged(propertyName);
    }

    public void RaisePropertyChanged(string propertyName = "") {
        NotifyPropertyChanged(propertyName);
    }

    public bool SetProperty(ref int field, int value, string propName = "") {
        if (field == value) { return false; }
        field = value;
        NotifyPropertyChanged(propName);
        return true;
    }
    public bool SetProperty(ref float field, float value, string propName = "") {
        if (field == value) { return false; }
        field = value;
        NotifyPropertyChanged(propName);
        return true;
    }
    public bool SetProperty(ref bool field, bool value, string propName = "") {
        if (field == value) { return false; }
        field = value;
        NotifyPropertyChanged(propName);
        return true;
    }
    public bool SetProperty(ref string field, string value, string propName = "") {
        if (field == value) { return false; }
        field = value;
        NotifyPropertyChanged(propName);
        return true;
    }
    public bool SetProperty(ref DependencyObject field, DependencyObject value, string propName = "") {
        if (field == value) { return false; }
        field = value;
        NotifyPropertyChanged(propName);
        return true;
    }

    public static bool SetField<T>(ObservableObject target, ref T field, T value, string propName = "") {
        field = value;
        if (target != null) { target.NotifyPropertyChanged(propName); }
        return true;
    }
}

public class ViewModelBase : ObservableObject {}
public class BindableBase : ObservableObject {}

// ============================================================================
// 5. ObservableList<T> & ObservableCollection<T> (STRICT 1-field C++ FFI ABI)
// ============================================================================

public class CollectionSlotBase {
    public Action[] Listeners;
    public CollectionSlotBase() { Listeners = new Action[0]; }
    public virtual bool Matches(object col) { return false; }
    public void AddListener(Action fn) {
        if (fn == null) { return; }
        for (int i = 0; i < Listeners.Length; i += 1) {
            if (Listeners[i] == fn) { return; }
        }
        Listeners.Push(fn);
    }
    public void RemoveListener(Action fn) {
        for (int i = 0; i < Listeners.Length; i += 1) {
            if (Listeners[i] == fn) {
                Listeners.RemoveAt(i);
                return;
            }
        }
    }
    public void Notify() {
        BindingEngine.BumpManagedVmEpoch();
        for (int i = 0; i < Listeners.Length; i += 1) {
            var fn = Listeners[i];
            if (fn != null) { fn(); }
        }
    }
}

public class TypedCollectionSlot<T> : CollectionSlotBase {
    public ObservableList<T> Target;
    public TypedCollectionSlot(ObservableList<T> target) : base() { Target = target; }
    public override bool Matches(object col) {
        var o = col as ObservableList<T>;
        return o == Target;
    }
}

public interface ICollectionNotifier {
    CollectionSlotBase EnsureNotifierSlot();
    int ItemCount();
    DependencyObject BoxItemAt(int index);
}

public static class CollectionChangeHub {
    private static CollectionSlotBase[] _slots;

    private static void EnsureInit() {
        if (_slots == null) { _slots = new CollectionSlotBase[0]; }
    }

    public static CollectionSlotBase FindOrRegister(CollectionSlotBase candidate, object col) {
        EnsureInit();
        for (int i = 0; i < _slots.Length; i += 1) {
            if (_slots[i].Matches(col)) { return _slots[i]; }
        }
        _slots.Push(candidate);
        return candidate;
    }

    public static void Subscribe(object col, Action listener) {
        var host = col as ICollectionNotifier;
        if (host == null || listener == null) { return; }
        var slot = host.EnsureNotifierSlot();
        if (slot != null) {
            EnsureInit();
            bool hasSlot = false;
            for (int i = 0; i < _slots.Length; i += 1) {
                if (_slots[i] == slot) { hasSlot = true; break; }
            }
            if (!hasSlot) { _slots.Push(slot); }
            slot.AddListener(listener);
        }
    }

    public static void Unsubscribe(object col, Action listener) {
        if (_slots == null || col == null || listener == null) { return; }
        for (int i = 0; i < _slots.Length; i += 1) {
            if (_slots[i].Matches(col)) {
                _slots[i].RemoveListener(listener);
                if (_slots[i].Listeners.Length == 0) {
                    _slots.RemoveAt(i);
                }
                return;
            }
        }
    }

    public static void NotifyChanged(object col) {
        if (_slots == null || _slots.Length == 0 || col == null) { return; }
        for (int i = 0; i < _slots.Length; i += 1) {
            if (_slots[i].Matches(col)) {
                _slots[i].Notify();
                return;
            }
        }
    }
}

// CRITICAL: Keep _items as the ONLY field (field 0) on ObservableList<T> for AeroObservableList ABI!
public class ObservableList<T> : ICollectionNotifier {
    private T[] _items;

    public ObservableList() {
        _items = new T[0];
    }

    public ObservableList(int initialCapacity) {
        _items = new T[0];
        if (initialCapacity > 0) { _items.Reserve(initialCapacity); }
    }

    public int Count() { return _items.Length; }
    public T Get(int index) { return _items[index]; }
    public void Set(int index, T value) {
        _items[index] = value;
        CollectionChangeHub.NotifyChanged(this);
    }

    public void Add(T item) {
        _items.Push(item);
        CollectionChangeHub.NotifyChanged(this);
    }

    public void AddRange(T[] items) {
        if (items == null || items.Length == 0) { return; }
        for (int i = 0; i < items.Length; i += 1) {
            _items.Push(items[i]);
        }
        CollectionChangeHub.NotifyChanged(this);
    }

    public void Insert(int index, T item) {
        _items.Insert(index, item);
        CollectionChangeHub.NotifyChanged(this);
    }

    public void RemoveAt(int index) {
        _items.RemoveAt(index);
        CollectionChangeHub.NotifyChanged(this);
    }

    public bool Remove(T item) {
        int idx = _items.IndexOf(item);
        if (idx < 0) { return false; }
        _items.RemoveAt(idx);
        CollectionChangeHub.NotifyChanged(this);
        return true;
    }

    public void Clear() {
        if (_items.Length == 0) { return; }
        _items.Clear();
        CollectionChangeHub.NotifyChanged(this);
    }

    public int IndexOf(T item) { return _items.IndexOf(item); }
    public bool Contains(T item) { return _items.Contains(item); }
    public T[] ToArray() { return _items; }

    public int ItemCount() { return _items.Length; }
    public DependencyObject BoxItemAt(int index) { return new TypedBox<T>(_items[index]); }
    public CollectionSlotBase EnsureNotifierSlot() {
        return CollectionChangeHub.FindOrRegister(new TypedCollectionSlot<T>(this), this);
    }
}

public class ObservableCollection<T> : ObservableList<T> {
    public ObservableCollection() : base() {}
    public ObservableCollection(int initialCapacity) : base(initialCapacity) {}
}

// ============================================================================
// 6. Value Converters (IValueConverter & Built-in Converters)
// ============================================================================

public interface IValueConverter {
    object Convert(object value, string targetType, object parameter, string culture);
    object ConvertBack(object value, string targetType, object parameter, string culture);
}

public interface IMultiValueConverter {
    object Convert(DependencyObject[] values, string targetType, object parameter, string culture);
}

public class BoolToVisibilityConverter : DependencyObject, IValueConverter {
    public Visibility TrueVisibility { get; set; } = Visibility.Visible;
    public Visibility FalseVisibility { get; set; } = Visibility.Collapsed;
    public bool Invert { get; set; }

    public BoolToVisibilityConverter() {}
    public BoolToVisibilityConverter(bool invert) { Invert = invert; }

    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        bool flag = false;
        if (value is DependencyObject) {
            flag = DependencyObject.UnboxBool(value as DependencyObject, false);
        } else if (value != null) {
            flag = true;
        }
        bool inv = Invert;
        if (parameter is BoxedString) {
            string p = (parameter as BoxedString).Value.ToLower();
            if (p == "invert" || p == "inverse" || p == "!") { inv = !inv; }
        }
        if (inv) { flag = !flag; }
        return DependencyObject.BoxInt(EnumConvert.ToInt(flag ? TrueVisibility : FalseVisibility));
    }

    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        int raw = (value is DependencyObject) ? DependencyObject.UnboxInt(value as DependencyObject, 0) : 0;
        bool visible = raw == 0;
        return DependencyObject.BoxBool(Invert ? !visible : visible);
    }
}

public class InverseBoolConverter : DependencyObject, IValueConverter {
    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        bool b = (value is DependencyObject) ? DependencyObject.UnboxBool(value as DependencyObject, false) : false;
        return DependencyObject.BoxBool(!b);
    }
    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        return Convert(value, targetType, parameter, culture);
    }
}

public class NullToVisibilityConverter : DependencyObject, IValueConverter {
    public bool Invert { get; set; }
    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        bool isPresent = value != null;
        if (value is BoxedString && (value as BoxedString).Value.Length == 0) {
            isPresent = false;
        }
        if (Invert) { isPresent = !isPresent; }
        return DependencyObject.BoxInt(EnumConvert.ToInt(isPresent ? Visibility.Visible : Visibility.Collapsed));
    }
    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        return null;
    }
}

public class AlternationConverter : DependencyObject, IValueConverter {
    private DependencyObject[] _values;
    public AlternationConverter() { _values = new DependencyObject[0]; }
    public void AddValue(DependencyObject v) { _values.Push(v); }
    public void AddValue(string v) { _values.Push(DependencyObject.BoxString(v)); }

    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        if (_values.Length == 0) { return value; }
        int idx = (value is DependencyObject) ? DependencyObject.UnboxInt(value as DependencyObject, 0) : 0;
        int mod = idx % _values.Length;
        if (mod < 0) { mod = mod + _values.Length; }
        return DependencyObject.Unbox(_values[mod]);
    }
    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        return null;
    }
}
