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

internal class CompiledMathExpr {
    public int[] Ops;
    public int[] OpArgs;
    public float[] Consts;
    private float[] _stack;

    public CompiledMathExpr(int[] ops, int[] opArgs, float[] consts, int maxStack) {
        Ops = ops;
        OpArgs = opArgs;
        Consts = consts;
        int cap = maxStack + 4;
        if (cap < 16) { cap = 16; }
        _stack = new float[cap];
    }

    internal static float FloorF(float x) {
        int k = (int)x;
        float kf = (float)k;
        return (x < kf) ? (kf - 1.0f) : kf;
    }

    internal static float CeilF(float x) {
        int k = (int)x;
        float kf = (float)k;
        return (x > kf) ? (kf + 1.0f) : kf;
    }

    internal static float ExpF(float x) {
        float ln2 = 0.69314718f;
        int m = (int)FloorF(x / ln2 + 0.5f);
        float r = x - (float)m * ln2;
        float r2 = r * r;
        float r3 = r2 * r;
        float r4 = r2 * r2;
        float r5 = r4 * r;
        float r6 = r3 * r3;
        float poly = 1.0f + r + r2 * 0.5f + r3 * 0.16666667f + r4 * 0.04166667f + r5 * 0.00833333f + r6 * 0.00138889f;
        if (m >= 0) {
            int i = 0;
            while (i < m && i < 64) { poly = poly * 2.0f; i += 1; }
        } else {
            int i = 0;
            int negM = -m;
            while (i < negM && i < 64) { poly = poly * 0.5f; i += 1; }
        }
        return poly;
    }

    internal static float LnF(float x) {
        if (x <= 0.0f) { return 0.0f; }
        float ln2 = 0.69314718f;
        int k = 0;
        float v = x;
        while (v > 1.5f && k < 64) { v = v * 0.5f; k += 1; }
        while (v < 0.5f && k > -64) { v = v * 2.0f; k -= 1; }
        float u = (v - 1.0f) / (v + 1.0f);
        float u2 = u * u;
        float u3 = u * u2;
        float u5 = u3 * u2;
        float u7 = u5 * u2;
        float u9 = u7 * u2;
        float s = 2.0f * (u + u3 * 0.33333334f + u5 * 0.2f + u7 * 0.14285715f + u9 * 0.11111111f);
        return s + (float)k * ln2;
    }

    internal static float PowF(float b, float e) {
        if (e == 0.0f) { return 1.0f; }
        if (b == 0.0f) { return e > 0.0f ? 0.0f : 1.0f; }
        int ei = (int)e;
        if ((float)ei == e && ei >= -32 && ei <= 32) {
            float res = 1.0f;
            int cnt = ei < 0 ? -ei : ei;
            for (int i = 0; i < cnt; i += 1) { res = res * b; }
            return ei < 0 ? (1.0f / res) : res;
        }
        if (b < 0.0f) { return 0.0f; }
        return ExpF(e * LnF(b));
    }

    public float EvalSingle(float x) {
        return EvalCore(x, null, false);
    }

    public float EvalMulti(DependencyObject[] values) {
        return EvalCore(0.0f, values, true);
    }

    private float EvalCore(float x, DependencyObject[] values, bool isMulti) {
        if (Ops == null || Ops.Length == 0) { return 0.0f; }
        int sp = 0;
        int n = Ops.Length;
        for (int i = 0; i < n; i += 1) {
            int op = Ops[i];
            if (op == 1) {
                _stack[sp] = Consts[OpArgs[i]];
                sp += 1;
            } else if (op == 2) {
                int idx = OpArgs[i];
                if (!isMulti) {
                    _stack[sp] = idx == 0 ? x : 0.0f;
                } else if (values != null && idx >= 0 && idx < values.Length && values[idx] != null) {
                    _stack[sp] = DependencyObject.UnboxFloat(values[idx], 0.0f);
                } else {
                    _stack[sp] = 0.0f;
                }
                sp += 1;
            } else if (op == 3) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] + _stack[sp];
            } else if (op == 4) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] - _stack[sp];
            } else if (op == 5) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] * _stack[sp];
            } else if (op == 6) {
                sp -= 1;
                float r = _stack[sp];
                _stack[sp - 1] = (r != 0.0f) ? (_stack[sp - 1] / r) : 0.0f;
            } else if (op == 7) {
                sp -= 1;
                float r = _stack[sp];
                if (r != 0.0f) {
                    float l = _stack[sp - 1];
                    _stack[sp - 1] = l - FloorF(l / r) * r;
                } else {
                    _stack[sp - 1] = 0.0f;
                }
            } else if (op == 8) {
                sp -= 1;
                _stack[sp - 1] = PowF(_stack[sp - 1], _stack[sp]);
            } else if (op == 9) {
                _stack[sp - 1] = -_stack[sp - 1];
            } else if (op == 10) {
                _stack[sp - 1] = FloorF(_stack[sp - 1]);
            } else if (op == 11) {
                _stack[sp - 1] = CeilF(_stack[sp - 1]);
            } else if (op == 12) {
                float a = _stack[sp - 1];
                _stack[sp - 1] = a >= 0.0f ? FloorF(a + 0.5f) : -FloorF(-a + 0.5f);
            } else if (op == 13) {
                _stack[sp - 1] = Math.Absf(_stack[sp - 1]);
            } else if (op == 14) {
                float a = _stack[sp - 1];
                _stack[sp - 1] = a >= 0.0f ? Math.Sqrtf(a) : 0.0f;
            } else if (op == 15) {
                _stack[sp - 1] = Math.Sinf(_stack[sp - 1]);
            } else if (op == 16) {
                _stack[sp - 1] = Math.Cosf(_stack[sp - 1]);
            } else if (op == 17) {
                sp -= 1;
                _stack[sp - 1] = Math.Minf(_stack[sp - 1], _stack[sp]);
            } else if (op == 18) {
                sp -= 1;
                _stack[sp - 1] = Math.Maxf(_stack[sp - 1], _stack[sp]);
            } else if (op == 19) {
                sp -= 2;
                _stack[sp - 1] = Math.Clampf(_stack[sp - 1], _stack[sp], _stack[sp + 1]);
            }
        }
        return sp > 0 ? _stack[0] : 0.0f;
    }
}

internal class MathExprParser {
    private static int _debugCompileCount = 0;

    public static int DebugCompileCount() { return _debugCompileCount; }
    public static void DebugResetCompileCount() { _debugCompileCount = 0; }

    private string _s = "";
    private int _p = 0;
    private int[] _ops;
    private int[] _opArgs;
    private float[] _consts;
    private int _depth = 0;
    private int _maxDepth = 1;

    private MathExprParser(string expr) {
        _s = expr == null ? "" : expr;
        _p = 0;
        _ops = new int[0];
        _opArgs = new int[0];
        _consts = new float[0];
    }

    public static CompiledMathExpr Compile(string expr) {
        _debugCompileCount += 1;
        var p = new MathExprParser(expr);
        p.CompileExpr();
        return new CompiledMathExpr(p._ops, p._opArgs, p._consts, p._maxDepth);
    }

    private void Emit(int op, int arg) {
        _ops.Push(op);
        _opArgs.Push(arg);
        if (op == 1 || op == 2) {
            _depth += 1;
            if (_depth > _maxDepth) { _maxDepth = _depth; }
        } else if (op == 19) {
            _depth -= 2;
        } else if (op != 9 && op != 10 && op != 11 && op != 12 && op != 13 && op != 14 && op != 15 && op != 16) {
            _depth -= 1;
        }
    }

    private void EmitConst(float v) {
        int cidx = _consts.Length;
        _consts.Push(v);
        Emit(1, cidx);
    }

    private void SkipWs() {
        while (_p < _s.Length) {
            string ch = _s.Substring(_p, 1);
            if (ch == " " || ch == "\t" || ch == "\r" || ch == "\n") { _p += 1; }
            else { break; }
        }
    }

    private string Peek() {
        SkipWs();
        if (_p >= _s.Length) { return ""; }
        return _s.Substring(_p, 1);
    }

    private void CompileExpr() {
        CompileTerm();
        while (true) {
            string op = Peek();
            if (op == "+") { _p += 1; CompileTerm(); Emit(3, 0); }
            else if (op == "-") { _p += 1; CompileTerm(); Emit(4, 0); }
            else { break; }
        }
    }

    private void CompileTerm() {
        CompilePower();
        while (true) {
            string op = Peek();
            if (op == "*") {
                _p += 1;
                CompilePower();
                Emit(5, 0);
            } else if (op == "/") {
                _p += 1;
                CompilePower();
                Emit(6, 0);
            } else if (op == "%") {
                _p += 1;
                CompilePower();
                Emit(7, 0);
            } else {
                break;
            }
        }
    }

    private void CompilePower() {
        CompileUnary();
        if (Peek() == "^") {
            _p += 1;
            CompilePower();
            Emit(8, 0);
        }
    }

    private void CompileUnary() {
        string op = Peek();
        if (op == "-") { _p += 1; CompileUnary(); Emit(9, 0); return; }
        if (op == "+") { _p += 1; CompileUnary(); return; }
        CompilePrimary();
    }

    private void CompilePrimary() {
        string ch = Peek();
        if (ch == "(") {
            _p += 1;
            CompileExpr();
            if (Peek() == ")") { _p += 1; }
            return;
        }
        if (ch == "{") {
            _p += 1;
            int idx = 0;
            while (_p < _s.Length) {
                string dch = _s.Substring(_p, 1);
                if (dch == "}") { _p += 1; break; }
                int dv = ParseUtil.DigitValue(dch);
                if (dv >= 0) { idx = idx * 10 + dv; }
                _p += 1;
            }
            Emit(2, idx);
            return;
        }
        if (ParseUtil.DigitValue(ch) >= 0 || ch == ".") {
            int start = _p;
            bool seenDot = false;
            while (_p < _s.Length) {
                string nc = _s.Substring(_p, 1);
                if (ParseUtil.DigitValue(nc) >= 0) { _p += 1; }
                else if (nc == "." && !seenDot) { seenDot = true; _p += 1; }
                else { break; }
            }
            EmitConst(ParseUtil.ParseFloat(_s.Substring(start, _p - start), 0.0f));
            return;
        }
        int idStart = _p;
        while (_p < _s.Length) {
            string ic = _s.Substring(_p, 1);
            if (ic == "(" || ic == ")" || ic == "," || ic == "+" || ic == "-" ||
                ic == "*" || ic == "/" || ic == "%" || ic == "^" || ic == " ") { break; }
            _p += 1;
        }
        string ident = _s.Substring(idStart, _p - idStart).ToLower();
        if (ident == "x" || ident == "value") {
            Emit(2, 0);
            return;
        }
        if (ident == "pi") {
            EmitConst(3.14159265f);
            return;
        }
        if (Peek() == "(") {
            _p += 1;
            CompileExpr();
            int argc = 1;
            if (Peek() == ",") {
                _p += 1;
                if (ident == "min" || ident == "max" || ident == "pow" || ident == "clamp") {
                    CompileExpr();
                    argc = 2;
                } else {
                    var discard = new MathExprParser("");
                    discard._s = _s;
                    discard._p = _p;
                    discard.CompileExpr();
                    _p = discard._p;
                }
            }
            if (Peek() == ",") {
                _p += 1;
                if (ident == "clamp") {
                    CompileExpr();
                    argc = 3;
                } else {
                    var discard2 = new MathExprParser("");
                    discard2._s = _s;
                    discard2._p = _p;
                    discard2.CompileExpr();
                    _p = discard2._p;
                }
            }
            if (Peek() == ")") { _p += 1; }
            if (ident == "floor") { Emit(10, 0); return; }
            if (ident == "ceil") { Emit(11, 0); return; }
            if (ident == "round") { Emit(12, 0); return; }
            if (ident == "abs") { Emit(13, 0); return; }
            if (ident == "sqrt") { Emit(14, 0); return; }
            if (ident == "sin") { Emit(15, 0); return; }
            if (ident == "cos") { Emit(16, 0); return; }
            if (ident == "min" && argc >= 2) { Emit(17, 0); return; }
            if (ident == "max" && argc >= 2) { Emit(18, 0); return; }
            if (ident == "pow" && argc >= 2) { Emit(8, 0); return; }
            if (ident == "clamp" && argc >= 3) { Emit(19, 0); return; }
            if (argc == 2) { _ops.Push(0); _opArgs.Push(0); _depth -= 1; }
            else if (argc == 3) { _ops.Push(0); _opArgs.Push(0); _ops.Push(0); _opArgs.Push(0); _depth -= 2; }
            return;
        }
        EmitConst(0.0f);
    }
}

public class MathConverter : DependencyObject, IValueConverter, IMultiValueConverter {
    private string _expression = "";
    private bool _cachedExprValid = false;
    private string _cachedRawExpr = "";
    private CompiledMathExpr _cachedCompiled;
    private static Dictionary<string, CompiledMathExpr> _compiledCache;

    public static int DebugCompileCount() { return MathExprParser.DebugCompileCount(); }
    public static void DebugResetCompileCount() { MathExprParser.DebugResetCompileCount(); }

    public string Expression {
        get { return _expression; }
        set {
            string next = value == null ? "" : value;
            if (_expression != next) {
                _expression = next;
                _cachedExprValid = false;
                _cachedCompiled = null;
            }
        }
    }

    public MathConverter() {}
    public MathConverter(string expression) {
        Expression = expression == null ? "" : expression;
    }

    private static string NormalizeExprString(string raw) {
        string expr = raw != null ? raw.Trim() : "";
        if (expr.Length >= 2 && expr.Substring(0, 2) == "{}") {
            expr = expr.Substring(2).Trim();
        }
        if (expr.Length > 0) {
            string first = expr.Substring(0, 1);
            if (first == "+" || first == "*" || first == "/" || first == "%") {
                expr = "{0}" + expr;
            } else if (first == "-" && expr.Length > 1 && ParseUtil.DigitValue(expr.Substring(1, 1)) >= 0) {
                expr = "{0}" + expr;
            }
        }
        return expr;
    }

    private static CompiledMathExpr GetOrCompile(string normExpr) {
        if (normExpr.Length == 0) { return null; }
        if (_compiledCache == null) { _compiledCache = new Dictionary<string, CompiledMathExpr>(); }
        if (_compiledCache.ContainsKey(normExpr)) {
            return _compiledCache[normExpr];
        }
        CompiledMathExpr compiled = MathExprParser.Compile(normExpr);
        if (_compiledCache.Count >= 256) { _compiledCache.Clear(); }
        _compiledCache[normExpr] = compiled;
        return compiled;
    }

    private CompiledMathExpr ResolveCompiled(object parameter, ref string outNormExpr) {
        if (_expression.Length > 0) {
            if (_cachedExprValid) {
                return _cachedCompiled;
            }
            string norm = NormalizeExprString(_expression);
            _cachedRawExpr = _expression;
            _cachedCompiled = GetOrCompile(norm);
            _cachedExprValid = true;
            outNormExpr = norm;
            return _cachedCompiled;
        }
        string raw = (parameter is DependencyObject)
            ? DependencyObject.UnboxString(parameter as DependencyObject, "")
            : "";
        if (raw.Length == 0) { return null; }
        if (_cachedExprValid && raw == _cachedRawExpr) {
            return _cachedCompiled;
        }
        string normParam = NormalizeExprString(raw);
        _cachedRawExpr = raw;
        _cachedCompiled = GetOrCompile(normParam);
        _cachedExprValid = true;
        outNormExpr = normParam;
        return _cachedCompiled;
    }

    private string ResolveExprString(object parameter) {
        string raw = _expression.Length > 0
            ? _expression
            : ((parameter is DependencyObject) ? DependencyObject.UnboxString(parameter as DependencyObject, "") : "");
        return NormalizeExprString(raw);
    }

    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        float x = (value is DependencyObject) ? DependencyObject.UnboxFloat(value as DependencyObject, 0.0f) : 0.0f;
        string unusedNorm = "";
        CompiledMathExpr compiled = ResolveCompiled(parameter, ref unusedNorm);
        if (compiled == null) { return DependencyObject.BoxFloat(x); }
        return DependencyObject.BoxFloat(compiled.EvalSingle(x));
    }

    public virtual object Convert(DependencyObject[] values, string targetType, object parameter, string culture) {
        int n = values == null ? 0 : values.Length;
        string unusedNorm = "";
        CompiledMathExpr compiled = ResolveCompiled(parameter, ref unusedNorm);
        if (compiled == null) {
            float first = (n > 0 && values[0] != null) ? DependencyObject.UnboxFloat(values[0], 0.0f) : 0.0f;
            return DependencyObject.BoxFloat(first);
        }
        return DependencyObject.BoxFloat(compiled.EvalMulti(values));
    }

    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        float y = (value is DependencyObject) ? DependencyObject.UnboxFloat(value as DependencyObject, 0.0f) : 0.0f;
        string expr = ResolveExprString(parameter);
        if (expr.Length == 0) { return DependencyObject.BoxFloat(y); }
        if (expr.Length >= 3 && expr.Substring(0, 3) == "{0}") {
            string tail = expr.Substring(3).Trim();
            if (tail.Length > 1) {
                string op = tail.Substring(0, 1);
                float k = ParseUtil.ParseFloat(tail.Substring(1), 0.0f);
                if (op == "+") { return DependencyObject.BoxFloat(y - k); }
                if (op == "-") { return DependencyObject.BoxFloat(y + k); }
                if (op == "*") { return DependencyObject.BoxFloat(k != 0.0f ? (y / k) : 0.0f); }
                if (op == "/") { return DependencyObject.BoxFloat(y * k); }
            }
        }
        return DependencyObject.BoxFloat(y);
    }
}
