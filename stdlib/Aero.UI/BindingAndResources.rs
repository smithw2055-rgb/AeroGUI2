module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. ResourceDictionary
// ============================================================================

public class ResourceDictionary : DependencyObject {
    private static ResourceDictionary _appResources;

    private Dictionary<string, DependencyObject> _map;
    private string[] _keys;

    public ResourceDictionary() {
        _map = new Dictionary<string, DependencyObject>();
        _keys = new string[0];
    }

    public static ResourceDictionary GlobalApplicationResources() {
        if (_appResources == null) {
            _appResources = new ResourceDictionary();
        }
        return _appResources;
    }

    public void SetBox(string key, DependencyObject box) {
        if (key == null || key.Length == 0) { return; }
        if (!_map.ContainsKey(key)) {
            _keys.Push(key);
        }
        _map[key] = box;
    }

    public void Set(string key, object val) { SetBox(key, DependencyObject.BoxObject(val)); }
    public void Set(string key, string val) { SetBox(key, DependencyObject.BoxString(val)); }
    public void Set(string key, float val) { SetBox(key, DependencyObject.BoxFloat(val)); }
    public void Set(string key, int val) { SetBox(key, DependencyObject.BoxInt(val)); }
    public void Set(string key, bool val) { SetBox(key, DependencyObject.BoxBool(val)); }

    public DependencyObject GetBox(string key) {
        if (key == null || !_map.ContainsKey(key)) { return null; }
        return _map[key];
    }

    public object Get(string key) {
        return DependencyObject.Unbox(GetBox(key));
    }

    public bool Contains(string key) {
        if (key == null) { return false; }
        return _map.ContainsKey(key);
    }

    public bool Remove(string key) {
        if (key == null || !_map.ContainsKey(key)) { return false; }
        _map.Remove(key);
        int idx = _keys.IndexOf(key);
        if (idx >= 0) { _keys.RemoveAt(idx); }
        return true;
    }

    public void Clear() {
        _map.Clear();
        _keys.Clear();
    }

    public string[] Keys() { return _keys; }

    public void Merge(ResourceDictionary other) {
        if (other == null) { return; }
        string[] otherKeys = other.Keys();
        for (int i = 0; i < otherKeys.Length; i += 1) {
            string k = otherKeys[i];
            SetBox(k, other.GetBox(k));
        }
    }
}

// ============================================================================
// 2. Setter, Triggers, Style, ControlTemplate, DataTemplate
// ============================================================================

public class Setter : DependencyObject {
    private DependencyObject _valueBox;
    public string Property { get; set; } = "";
    public string TargetName { get; set; } = "";

    public Setter() {}
    public Setter(string property, object value) {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxObject(value);
    }
    public Setter(string property, string value) {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxString(value);
    }
    public Setter(string property, float value) {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxFloat(value);
    }
    public Setter(string property, int value) {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxInt(value);
    }
    public Setter(string property, bool value) {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxBool(value);
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }
    public DependencyObject ValueBox {
        get { return _valueBox; }
        set { _valueBox = value; }
    }

    public void ApplyTo(UIElement rootTarget) {
        if (rootTarget == null || Property.Length == 0) { return; }
        UIElement dest = TargetName.Length > 0 ? rootTarget.FindName(TargetName) : rootTarget;
        if (dest != null) {
            dest.SetPropertyValue(Property, _valueBox);
        }
    }
}

public class TriggerBase : DependencyObject {
    private Setter[] _setters;
    private Storyboard[] _enterActions;
    private Storyboard[] _exitActions;
    protected bool _wasActive;

    public TriggerBase() {
        _setters = new Setter[0];
        _enterActions = new Storyboard[0];
        _exitActions = new Storyboard[0];
    }

    public Setter[] Setters() { return _setters; }

    public void AddSetter(Setter s) { if (s != null) { _setters.Push(s); } }
    public void AddSetter(string prop, object val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, string val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, float val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, int val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, bool val) { _setters.Push(new Setter(prop, val)); }
    public void AddEnterAction(Storyboard sb) { if (sb != null) { _enterActions.Push(sb); } }
    public void AddExitAction(Storyboard sb) { if (sb != null) { _exitActions.Push(sb); } }

    public virtual void Attach(UIElement target) {}

    public virtual bool IsMatched(UIElement target) { return false; }

    public void Evaluate(UIElement target, Style owningStyle) {
        bool active = IsMatched(target);
        if (active) {
            for (int i = 0; i < _setters.Length; i += 1) {
                _setters[i].ApplyTo(target);
            }
            if (!_wasActive) {
                for (int i = 0; i < _enterActions.Length; i += 1) {
                    _enterActions[i].Begin(target);
                }
            }
        } else if (_wasActive) {
            for (int i = 0; i < _exitActions.Length; i += 1) {
                _exitActions[i].Begin(target);
            }
        }
        _wasActive = active;
    }
}

public class Trigger : TriggerBase {
    private DependencyObject _valueBox;
    public string Property { get; set; } = "";
    public string SourceName { get; set; } = "";

    public Trigger() : base() {}
    public Trigger(string property, object value) : base() {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxObject(value);
    }
    public Trigger(string property, string value) : base() {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxString(value);
    }
    public Trigger(string property, bool value) : base() {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxBool(value);
    }
    public Trigger(string property, int value) : base() {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxInt(value);
    }
    public Trigger(string property, float value) : base() {
        Property = property == null ? "" : property;
        _valueBox = DependencyObject.BoxFloat(value);
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }
    public void SetValue(string v) { _valueBox = DependencyObject.BoxString(v); }
    public void SetValue(bool v) { _valueBox = DependencyObject.BoxBool(v); }
    public void SetValue(int v) { _valueBox = DependencyObject.BoxInt(v); }
    public void SetValue(float v) { _valueBox = DependencyObject.BoxFloat(v); }

    public override bool IsMatched(UIElement target) {
        if (target == null || Property.Length == 0) { return false; }
        UIElement src = SourceName.Length > 0 ? target.FindName(SourceName) : target;
        if (src == null) { return false; }
        DependencyObject actual = src.GetPropertyValue(Property);
        return BoxEquals(actual, _valueBox);
    }

    public static bool BoxEquals(DependencyObject a, DependencyObject b) {
        if (a == b) { return true; }
        if (a == null || b == null) { return false; }
        if (a is BoxedBool && b is BoxedBool) {
            return (a as BoxedBool).Value == (b as BoxedBool).Value;
        }
        if (a is BoxedBool || b is BoxedBool) {
            return DependencyObject.UnboxBool(a, false) == DependencyObject.UnboxBool(b, false);
        }
        if (a is BoxedInt && b is BoxedInt) {
            return (a as BoxedInt).Value == (b as BoxedInt).Value;
        }
        string sa = DependencyObject.UnboxString(a, "");
        string sb = DependencyObject.UnboxString(b, "");
        return sa == sb;
    }
}

public class DataTrigger : TriggerBase {
    private DependencyObject _valueBox;
    public string BindingPath { get; set; } = "";
    public string Binding {
        get { return BindingPath; }
        set { BindingPath = value == null ? "" : value; }
    }

    public DataTrigger() : base() {}
    public DataTrigger(string bindingPath, object value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxObject(value);
    }
    public DataTrigger(string bindingPath, string value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxString(value);
    }
    public DataTrigger(string bindingPath, bool value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxBool(value);
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }

    public override bool IsMatched(UIElement target) {
        if (target == null || BindingPath.Length == 0) { return false; }
        object dc = target.DataContext;
        if (dc == null) { return _valueBox == null; }
        DependencyObject actual = BindingEngine.ReadPathBox(dc, BindingPath);
        return Trigger.BoxEquals(actual, _valueBox);
    }
}

public class EventTrigger : TriggerBase {
    private Storyboard _storyboard;
    public string RoutedEvent { get; set; } = "";
    public string SourceName { get; set; } = "";

    public EventTrigger() : base() {}
    public EventTrigger(string routedEvent, Storyboard sb = null) : base() {
        RoutedEvent = routedEvent == null ? "" : routedEvent;
        _storyboard = sb;
    }

    public Storyboard Storyboard {
        get { return _storyboard; }
        set { _storyboard = value; }
    }

    public override void Attach(UIElement target) {
        if (target == null || RoutedEvent.Length == 0) { return; }
        UIElement src = SourceName.Length > 0 ? target.FindName(SourceName) : target;
        if (src == null) { src = target; }
        string ev = RoutedEvent;
        if (ev == "Loaded" || ev == "FrameworkElement.Loaded") {
            src.Loaded += (object s, RoutedEventArgs e) => { RunActions(target); };
        } else if (ev == "MouseEnter" || ev == "UIElement.MouseEnter") {
            src.MouseEnter += (object s, MouseEventArgs e) => { RunActions(target); };
        } else if (ev == "MouseLeave" || ev == "UIElement.MouseLeave") {
            src.MouseLeave += (object s, MouseEventArgs e) => { RunActions(target); };
        } else if (ev == "GotFocus" || ev == "UIElement.GotFocus") {
            src.GotFocus += (object s, RoutedEventArgs e) => { RunActions(target); };
        } else if (ev == "LostFocus" || ev == "UIElement.LostFocus") {
            src.LostFocus += (object s, RoutedEventArgs e) => { RunActions(target); };
        } else if (ev == "MouseLeftButtonDown" || ev == "UIElement.MouseLeftButtonDown") {
            src.MouseLeftButtonDown += (object s, MouseButtonEventArgs e) => { RunActions(target); };
        } else if (ev == "MouseLeftButtonUp" || ev == "UIElement.MouseLeftButtonUp") {
            src.MouseLeftButtonUp += (object s, MouseButtonEventArgs e) => { RunActions(target); };
        }
    }

    public void RunActions(UIElement target) {
        if (_storyboard != null) { _storyboard.Begin(target); }
    }
}

public class Style : DependencyObject {
    public string TargetType { get; set; } = "";
    public Style BasedOn { get; set; }
    public ResourceDictionary Resources { get; set; }

    private Setter[] _setters;
    private TriggerBase[] _triggers;

    public Style() {
        _setters = new Setter[0];
        _triggers = new TriggerBase[0];
        Resources = new ResourceDictionary();
    }
    public Style(string targetType) : this() {
        TargetType = targetType == null ? "" : targetType;
    }
    public Style(string targetType, Style basedOn) : this(targetType) {
        BasedOn = basedOn;
    }

    public Setter[] Setters() { return _setters; }
    public TriggerBase[] Triggers() { return _triggers; }

    public void AddSetter(Setter setter) { if (setter != null) { _setters.Push(setter); } }
    public void AddSetter(string propName, object value) { _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, string value) { _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, float value) { _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, int value) { _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, bool value) { _setters.Push(new Setter(propName, value)); }

    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { _triggers.Push(trigger); } }
    public void AddDataTrigger(DataTrigger trigger) { AddTrigger(trigger); }
    public void AddEventTrigger(EventTrigger trigger) { AddTrigger(trigger); }

    public void ApplySettersOnly(UIElement target) {
        if (target == null) { return; }
        if (BasedOn != null) { BasedOn.ApplySettersOnly(target); }
        for (int i = 0; i < _setters.Length; i += 1) {
            _setters[i].ApplyTo(target);
        }
    }

    public void ApplyTo(UIElement target) {
        if (target == null) { return; }
        if (BasedOn != null) { BasedOn.ApplyTo(target); }
        for (int i = 0; i < _setters.Length; i += 1) {
            _setters[i].ApplyTo(target);
        }
        for (int i = 0; i < _triggers.Length; i += 1) {
            _triggers[i].Attach(target);
        }
        EvaluateTriggers(target);
    }

    public void EvaluateTriggers(UIElement target) {
        if (target == null) { return; }
        bool anyReverted = false;
        for (int i = 0; i < _triggers.Length; i += 1) {
            if (!_triggers[i].IsMatched(target)) { anyReverted = true; break; }
        }
        if (anyReverted) {
            ApplySettersOnly(target);
        }
        if (BasedOn != null) { BasedOn.EvaluateTriggers(target); }
        for (int i = 0; i < _triggers.Length; i += 1) {
            _triggers[i].Evaluate(target, this);
        }
    }
}

public class ControlTemplate : DependencyObject {
    public string TargetType { get; set; } = "";
    private Func<Control, UIElement> _factory;

    public ControlTemplate() {}
    public ControlTemplate(Func<Control, UIElement> factory) {
        _factory = factory;
    }
    public ControlTemplate(string targetType, Func<Control, UIElement> factory) {
        TargetType = targetType == null ? "" : targetType;
        _factory = factory;
    }

    public Func<Control, UIElement> Factory {
        get { return _factory; }
        set { _factory = value; }
    }

    public UIElement Instantiate(Control owner) {
        if (_factory == null) { return null; }
        UIElement root = _factory(owner);
        if (root != null) { MarkTemplatedParentSubtree(root, owner); }
        return root;
    }

    private static void MarkTemplatedParentSubtree(UIElement node, UIElement owner) {
        node.TemplatedParent = owner;
        for (int i = 0; i < node.ChildCount(); i += 1) {
            MarkTemplatedParentSubtree(node.ChildAt(i), owner);
        }
    }
}

public class DataTemplate : DependencyObject {
    public string DataType { get; set; } = "";
    private Func<object, UIElement> _factory;

    public DataTemplate() {}
    public DataTemplate(Func<object, UIElement> factory) {
        _factory = factory;
    }
    public DataTemplate(string dataType, Func<object, UIElement> factory) {
        DataType = dataType == null ? "" : dataType;
        _factory = factory;
    }

    public Func<object, UIElement> Factory {
        get { return _factory; }
        set { _factory = value; }
    }

    public virtual UIElement Instantiate(object item) {
        if (_factory == null) { return null; }
        UIElement root = _factory(item);
        if (root != null && item != null) {
            root.DataContext = item;
        }
        return root;
    }
}

public class HierarchicalDataTemplate : DataTemplate {
    public string ItemsSourcePath { get; set; } = "";
    public DataTemplate ItemTemplate { get; set; }

    public HierarchicalDataTemplate() : base() {}
    public HierarchicalDataTemplate(Func<object, UIElement> factory, string itemsSourcePath = "") : base(factory) {
        ItemsSourcePath = itemsSourcePath == null ? "" : itemsSourcePath;
    }
}

// ============================================================================
// 3. VisualStateManager & Storyboard / KeyFrames
// ============================================================================

public class VisualState : DependencyObject {
    public string Name { get; set; } = "";
    private Storyboard _storyboard;
    private Setter[] _setters;

    public VisualState() { _setters = new Setter[0]; }
    public VisualState(string name) : this() { Name = name == null ? "" : name; }

    public Storyboard Storyboard {
        get { return _storyboard; }
        set { _storyboard = value; }
    }

    public void AddSetter(Setter s) { if (s != null) { _setters.Push(s); } }
    public void AddSetter(string prop, object val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, string val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, float val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, bool val) { _setters.Push(new Setter(prop, val)); }

    public void Enter(UIElement target) {
        for (int i = 0; i < _setters.Length; i += 1) {
            _setters[i].ApplyTo(target);
        }
        if (_storyboard != null) {
            _storyboard.Begin(target);
        }
    }
}

public class VisualStateGroup : DependencyObject {
    public string Name { get; set; } = "";
    public string CurrentStateName { get; set; } = "";
    private VisualState[] _states;

    public VisualStateGroup() { _states = new VisualState[0]; }
    public VisualStateGroup(string name) : this() { Name = name == null ? "" : name; }

    public void AddState(VisualState st) { if (st != null) { _states.Push(st); } }
    public VisualState FindState(string name) {
        for (int i = 0; i < _states.Length; i += 1) {
            if (_states[i].Name == name) { return _states[i]; }
        }
        return null;
    }
}

public class VisualStateGroupList : DependencyObject {
    public VisualStateGroup[] Items;
    public VisualStateGroupList(VisualStateGroup[] items) { Items = items; }
}

public static class VisualStateManager {
    private static UIElement[] _trackedControls;
    private static VisualStateGroupList[] _trackedGroups;

    private static void EnsureInit() {
        if (_trackedControls == null) {
            _trackedControls = new UIElement[0];
            _trackedGroups = new VisualStateGroupList[0];
        }
    }

    public static void SetVisualStateGroups(UIElement control, VisualStateGroup[] groups) {
        if (control == null || groups == null) { return; }
        EnsureInit();
        for (int i = 0; i < _trackedControls.Length; i += 1) {
            if (_trackedControls[i] == control) {
                _trackedGroups[i] = new VisualStateGroupList(groups);
                return;
            }
        }
        _trackedControls.Push(control);
        _trackedGroups.Push(new VisualStateGroupList(groups));
    }

    public static VisualStateGroup[] GetVisualStateGroups(UIElement control) {
        if (_trackedControls == null || control == null) { return new VisualStateGroup[0]; }
        for (int i = 0; i < _trackedControls.Length; i += 1) {
            if (_trackedControls[i] == control) { return _trackedGroups[i].Items; }
        }
        return new VisualStateGroup[0];
    }

    public static bool GoToState(UIElement control, string stateName, bool useTransitions = true) {
        if (control == null || stateName == null) { return false; }
        VisualStateGroup[] groups = GetVisualStateGroups(control);
        for (int i = 0; i < groups.Length; i += 1) {
            VisualState st = groups[i].FindState(stateName);
            if (st != null) {
                groups[i].CurrentStateName = stateName;
                st.Enter(control);
                return true;
            }
        }
        return false;
    }
}

public class DoubleKeyFrame : DependencyObject {
    public float KeyTimeSec { get; set; }
    public float Value { get; set; }
    public EasingFunctionKind EasingKind { get; set; } = EasingFunctionKind.Linear;
    public EasingMode EasingMode { get; set; } = EasingMode.EaseOut;

    public DoubleKeyFrame() {}
    public DoubleKeyFrame(float value, float keyTimeSec) { Value = value; KeyTimeSec = keyTimeSec; }
}

public class LinearDoubleKeyFrame : DoubleKeyFrame {
    public LinearDoubleKeyFrame(float value, float keyTimeSec) : base(value, keyTimeSec) {
        EasingKind = EasingFunctionKind.Linear;
    }
}

public class EasingDoubleKeyFrame : DoubleKeyFrame {
    public EasingDoubleKeyFrame(float value, float keyTimeSec, EasingFunctionKind kind = EasingFunctionKind.Cubic, EasingMode mode = EasingMode.EaseOut)
        : base(value, keyTimeSec) {
        EasingKind = kind;
        EasingMode = mode;
    }
}

public class TimelineAnimation : DependencyObject {
    public string TargetName { get; set; } = "";
    public string TargetProperty { get; set; } = "Opacity";
    public float FromFloat { get; set; }
    public float ToFloat { get; set; } = 1.0f;
    public bool HasFrom { get; set; }
    public float DurationSec { get; set; } = 0.25f;
    public float DelaySec { get; set; } = 0.0f;
    public EasingFunctionKind EasingKind { get; set; } = EasingFunctionKind.Cubic;
    public EasingMode EasingMode { get; set; } = EasingMode.EaseOut;
    public bool AutoReverse { get; set; } = false;
    public int RepeatCount { get; set; } = 1;
    public bool IsColor { get; set; } = false;
    public string FromColor { get; set; } = "";
    public string ToColor { get; set; } = "";
    public bool UseSpring { get; set; } = false;
    public float Stiffness { get; set; } = 320.0f;
    public float Damping { get; set; } = 28.0f;
    public DoubleKeyFrame[] KeyFrames { get; set; }

    public TimelineAnimation() { KeyFrames = new DoubleKeyFrame[0]; }

    public void StartOn(UIElement rootScope) {
        if (rootScope == null) { return; }
        UIElement dest = TargetName.Length > 0 ? rootScope.FindName(TargetName) : rootScope;
        if (dest == null || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { return; }
        VisualAnimProp animProp = EnumConvert.ParseAnimProp(TargetProperty);
        int propId = EnumConvert.ToInt(animProp);
        int ek = EnumConvert.ToInt(EasingKind);
        int em = EnumConvert.ToInt(EasingMode);

        if (IsColor) {
            uint fromCol = FromColor.Length > 0 ? ParseUtil.ParseColor(FromColor) : 0u;
            uint toCol = ToColor.Length > 0 ? ParseUtil.ParseColor(ToColor) : 0u;
            Anim_StartColor(dest.__nativeHandle, propId, fromCol, toCol, DurationSec, ek, em);
            return;
        }
        if (UseSpring) {
            Anim_SpringFloat(dest.__nativeHandle, propId, ToFloat, Stiffness, Damping);
            return;
        }
        if (KeyFrames != null && KeyFrames.Length > 0) {
            float prevVal = FromFloat;
            float prevTime = 0.0f;
            for (int i = 0; i < KeyFrames.Length; i += 1) {
                DoubleKeyFrame kf = KeyFrames[i];
                float segDur = Math.Maxf(0.01f, kf.KeyTimeSec - prevTime);
                Anim_StartFloat(
                    dest.__nativeHandle, propId, prevVal, kf.Value, segDur,
                    EnumConvert.ToInt(kf.EasingKind), EnumConvert.ToInt(kf.EasingMode),
                    DelaySec + prevTime, false, 1);
                prevVal = kf.Value;
                prevTime = kf.KeyTimeSec;
            }
            return;
        }
        float fromV = FromFloat;
        if (!HasFrom) {
            DependencyObject curBox = dest.GetPropertyValue(TargetProperty);
            if (curBox != null) { fromV = DependencyObject.UnboxFloat(curBox, 0.0f); }
        }
        Anim_StartFloat(dest.__nativeHandle, propId, fromV, ToFloat, DurationSec, ek, em, DelaySec, AutoReverse, RepeatCount);
    }

    public void StopOn(UIElement rootScope) {
        if (rootScope == null) { return; }
        UIElement dest = TargetName.Length > 0 ? rootScope.FindName(TargetName) : rootScope;
        if (dest != null && ParseUtil.IsValidHandle(dest.__nativeHandle)) {
            Anim_StopAll(dest.__nativeHandle);
        }
    }
}

public class DoubleAnimation : TimelineAnimation {
    public float From { get { return FromFloat; } set { FromFloat = value; HasFrom = true; } }
    public float To { get { return ToFloat; } set { ToFloat = value; } }
    public float Duration { get { return DurationSec; } set { DurationSec = value; } }

    public DoubleAnimation() : base() {}
    public DoubleAnimation(float to, float durationSec) : base() {
        ToFloat = to; DurationSec = durationSec; HasFrom = false;
    }
    public DoubleAnimation(float from, float to, float durationSec) : base() {
        FromFloat = from; ToFloat = to; DurationSec = durationSec; HasFrom = true;
    }
}

public class ColorAnimation : TimelineAnimation {
    public string From { get { return FromColor; } set { FromColor = value == null ? "" : value; } }
    public string To { get { return ToColor; } set { ToColor = value == null ? "" : value; } }
    public float Duration { get { return DurationSec; } set { DurationSec = value; } }

    public ColorAnimation() : base() { IsColor = true; }
    public ColorAnimation(string fromColor, string toColor, float durationSec) : base() {
        IsColor = true; From = fromColor; To = toColor; DurationSec = durationSec;
    }
}

public class DoubleAnimationUsingKeyFrames : TimelineAnimation {
    public void AddKeyFrame(DoubleKeyFrame kf) {
        if (kf != null) { KeyFrames.Push(kf); }
    }
}

public class Storyboard : DependencyObject {
    public string TargetName { get; set; } = "";
    public UIElement Target { get; set; }
    private TimelineAnimation[] _children;
    public event EventHandler Completed;

    public Storyboard() {
        _children = new TimelineAnimation[0];
    }

    public TimelineAnimation[] Children() { return _children; }
    public void Add(TimelineAnimation anim) { if (anim != null) { _children.Push(anim); } }

    public void AddDoubleAnimation(
        string targetName, string targetProp, float from, float to, float durationSec,
        EasingFunctionKind ease = EasingFunctionKind.Cubic, EasingMode mode = EasingMode.EaseOut) {
        var da = new DoubleAnimation(from, to, durationSec);
        da.TargetName = targetName == null ? "" : targetName;
        da.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        da.EasingKind = ease;
        da.EasingMode = mode;
        _children.Push(da);
    }

    public void AddDoubleAnimation(
        string targetProp, float from, float to, float durationSec,
        EasingFunctionKind ease = EasingFunctionKind.Cubic, EasingMode mode = EasingMode.EaseOut) {
        AddDoubleAnimation("", targetProp, from, to, durationSec, ease, mode);
    }

    public void AddColorAnimation(
        string targetName, string targetProp, string fromColor, string toColor, float durationSec,
        EasingFunctionKind ease = EasingFunctionKind.Linear, EasingMode mode = EasingMode.EaseOut) {
        var ca = new ColorAnimation(fromColor, toColor, durationSec);
        ca.TargetName = targetName == null ? "" : targetName;
        ca.TargetProperty = targetProp == null ? "Background" : targetProp;
        ca.EasingKind = ease;
        ca.EasingMode = mode;
        _children.Push(ca);
    }

    public void AddSpringAnimation(string targetName, string targetProp, float targetVal, float stiffness = 320.0f, float damping = 28.0f) {
        var sa = new TimelineAnimation();
        sa.UseSpring = true;
        sa.TargetName = targetName == null ? "" : targetName;
        sa.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        sa.ToFloat = targetVal;
        sa.Stiffness = stiffness;
        sa.Damping = damping;
        _children.Push(sa);
    }

    public void Begin(UIElement scope = null) {
        UIElement root = scope != null ? scope : Target;
        if (root == null) { return; }
        for (int i = 0; i < _children.Length; i += 1) {
            TimelineAnimation anim = _children[i];
            if (anim.TargetName.Length == 0 && TargetName.Length > 0) {
                anim.TargetName = TargetName;
            }
            anim.StartOn(root);
        }
        Completed(this, EventArgs.Empty());
    }

    public void Stop(UIElement scope = null) {
        UIElement root = scope != null ? scope : Target;
        if (root == null) { return; }
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].StopOn(root);
        }
    }

    public static void SetTargetName(TimelineAnimation anim, string name) {
        if (anim != null) { anim.TargetName = name == null ? "" : name; }
    }
    public static void SetTargetProperty(TimelineAnimation anim, string prop) {
        if (anim != null) { anim.TargetProperty = prop == null ? "" : prop; }
    }
}

// ============================================================================
// 4. Binding, MultiBinding & BindingEngine (Lambda Fast-Path + Reflect Path)
// ============================================================================

public enum BindingMode {
    OneWay = 0,
    TwoWay = 1,
    OneTime = 2,
    OneWayToSource = 3,
}

public class Binding : DependencyObject {
    public string Path { get; set; } = "";
    public string ElementName { get; set; } = "";
    private DependencyObject _sourceBox;
    public BindingMode Mode { get; set; } = BindingMode.OneWay;
    public IValueConverter Converter { get; set; }
    public string ConverterKey { get; set; } = "";
    public string ConverterParameter { get; set; } = "";
    public string StringFormat { get; set; } = "";
    private DependencyObject _fallbackBox;

    public Binding() {}
    public Binding(string path) { Path = path == null ? "" : path; }

    public object Source {
        get { return DependencyObject.Unbox(_sourceBox); }
        set { _sourceBox = DependencyObject.BoxObject(value); }
    }
    public object FallbackValue {
        get { return DependencyObject.Unbox(_fallbackBox); }
        set { _fallbackBox = DependencyObject.BoxObject(value); }
    }
}

public class MultiBinding : DependencyObject {
    public Binding[] Bindings { get; set; }
    public IMultiValueConverter Converter { get; set; }
    public string ConverterParameter { get; set; } = "";
    public string StringFormat { get; set; } = "";

    public MultiBinding() { Bindings = new Binding[0]; }
    public void AddBinding(Binding b) { if (b != null) { Bindings.Push(b); } }
}

internal class BindingEntry : DependencyObject {
    public UIElement Target;
    public string TargetProp = "";
    public int Mode = 0; // 0=OneWay, 1=TwoWay, 2=OneTime
    public int Kind = 0; // 0=Lambda, 1=Path, 2=Element, 3=TemplateParent, 4=Multi

    public Func<DependencyObject> LambdaGetter;
    public Action<DependencyObject> LambdaSetter;

    public DependencyObject ExplicitSourceBox;
    public UIElement SourceElement;
    public string SourcePath = "";
    public string[] MultiPaths;
    public string ElementName = "";
    public IValueConverter Converter;
    public IMultiValueConverter MultiConverter;
    public string ConverterKey = "";
    public string ConverterParam = "";
    public string StringFormat = "";

    public string LastHash = "";
    public int LastDirtyMask = -1;
    public bool OneTimeDone = false;

    public DependencyObject ResolveSourceBox() {
        if (ExplicitSourceBox != null) { return ExplicitSourceBox; }
        if (Target != null) { return Target.EffectiveDataContextBox(); }
        return null;
    }

    public IValueConverter ResolveConverter() {
        if (Converter != null) { return Converter; }
        if (ConverterKey.Length > 0 && Target != null) {
            DependencyObject box = Target.FindResourceBox(ConverterKey);
            if (box is IValueConverter) { return box as IValueConverter; }
        }
        return null;
    }

    public string FormatValue(DependencyObject box) {
        string raw = DependencyObject.UnboxString(box, "");
        if (StringFormat.Length == 0) { return raw; }
        if (StringFormat.Contains("{0}")) {
            return StringFormat.Replace("{0}", raw);
        }
        return StringFormat + raw;
    }

    public void Evaluate() {
        if (Target == null) { return; }
        if (Mode == 2 && OneTimeDone) { return; }

        if (Kind == 0 && LambdaGetter != null) {
            DependencyObject val = LambdaGetter();
            string h = DependencyObject.UnboxString(val, "");
            if (h != LastHash || !OneTimeDone) {
                LastHash = h;
                OneTimeDone = true;
                Target.SetPropertyValue(TargetProp, val);
            }
            return;
        }

        if (Kind == 2) {
            if (SourceElement == null && ElementName.Length > 0) {
                SourceElement = Target.RootElement().FindName(ElementName);
            }
            if (SourceElement != null) {
                DependencyObject ev = SourceElement.GetPropertyValue(SourcePath);
                if (ev == null && SourceElement.DataContext != null) {
                    ev = BindingEngine.ReadPathBox(SourceElement.DataContext, SourcePath);
                }
                ApplyConverted(ev);
                OneTimeDone = true;
            }
            return;
        }

        if (Kind == 3 && SourceElement != null) {
            DependencyObject tv = SourceElement.GetPropertyValue(SourcePath);
            ApplyConverted(tv);
            OneTimeDone = true;
            return;
        }

        DependencyObject srcBox = ResolveSourceBox();
        object srcObj = DependencyObject.Unbox(srcBox);
        if (srcObj == null) { return; }

        if (Kind == 4 && MultiPaths != null) {
            var vals = new DependencyObject[MultiPaths.Length];
            string sig = "";
            for (int i = 0; i < MultiPaths.Length; i += 1) {
                vals[i] = BindingEngine.ReadPathBox(srcObj, MultiPaths[i]);
                sig = sig + "|" + DependencyObject.UnboxString(vals[i], "");
            }
            if (sig == LastHash && OneTimeDone) { return; }
            LastHash = sig;
            OneTimeDone = true;
            if (MultiConverter != null) {
                object res = MultiConverter.Convert(vals, TargetProp, DependencyObject.BoxString(ConverterParam), "");
                ApplyRawBox(DependencyObject.BoxObject(res));
            } else if (StringFormat.Length > 0) {
                string txt = StringFormat;
                for (int i = 0; i < vals.Length; i += 1) {
                    txt = txt.Replace("{" + i + "}", DependencyObject.UnboxString(vals[i], ""));
                }
                Target.SetPropertyValue(TargetProp, DependencyObject.BoxString(txt));
            }
            return;
        }

        if (Kind == 1) {
            int dm = ParseUtil.IsNativeReady() ? Reflect_GetDirtyMask(srcObj) : -1;
            DependencyObject valBox = BindingEngine.ReadPathBox(srcObj, SourcePath);
            string sig = DependencyObject.UnboxString(valBox, "");
            if (dm == LastDirtyMask && dm >= 0 && sig == LastHash && OneTimeDone) { return; }
            LastDirtyMask = dm;
            if (sig != LastHash || !OneTimeDone) {
                LastHash = sig;
                OneTimeDone = true;
                ApplyConverted(valBox);
            }
        }
    }

    private void ApplyConverted(DependencyObject rawBox) {
        IValueConverter conv = ResolveConverter();
        DependencyObject valBox = rawBox;
        if (conv != null) {
            DependencyObject paramBox = ConverterParam.Length > 0 ? DependencyObject.BoxString(ConverterParam) : null;
            object converted = conv.Convert(
                valBox,
                TargetProp,
                paramBox,
                "");
            valBox = DependencyObject.BoxObject(converted);
        }
        ApplyRawBox(valBox);
    }

    private void ApplyRawBox(DependencyObject valBox) {
        if (StringFormat.Length > 0) {
            Target.SetPropertyValue(TargetProp, DependencyObject.BoxString(FormatValue(valBox)));
        } else {
            Target.SetPropertyValue(TargetProp, valBox);
        }
    }

    public void PushTwoWay(DependencyObject newTargetBox) {
        if (Mode != 1) { return; }
        if (Kind == 0 && LambdaSetter != null) {
            LambdaSetter(newTargetBox);
            return;
        }
        if (Kind == 2 && SourceElement != null) {
            SourceElement.SetPropertyValue(SourcePath, newTargetBox);
            return;
        }
        if (Kind == 1) {
            object srcObj = DependencyObject.Unbox(ResolveSourceBox());
            if (srcObj != null) {
                IValueConverter conv = ResolveConverter();
                DependencyObject writeBox = newTargetBox;
                if (conv != null) {
                    DependencyObject paramBox = ConverterParam.Length > 0 ? DependencyObject.BoxString(ConverterParam) : null;
                    object back = conv.ConvertBack(
                        writeBox,
                        TargetProp,
                        paramBox,
                        "");
                    writeBox = DependencyObject.BoxObject(back);
                }
                BindingEngine.WritePathBox(srcObj, SourcePath, writeBox);
            }
        }
    }
}

public static class BindingEngine {
    private static BindingEntry[] _entries;

    private static void EnsureInit() {
        if (_entries == null) { _entries = new BindingEntry[0]; }
    }

    private static void RegisterEntry(BindingEntry entry) {
        EnsureInit();
        _entries.Push(entry);
        entry.Evaluate();
    }

    public static void BindLambdaOneWay(UIElement target, string targetProp, Func<DependencyObject> getter) {
        if (target == null || targetProp == null || getter == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 0;
        e.Mode = 0;
        e.LambdaGetter = getter;
        RegisterEntry(e);
    }

    public static void BindLambdaString(UIElement target, string targetProp, Func<string> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxString(fn()); });
    }
    public static void BindLambdaFloat(UIElement target, string targetProp, Func<float> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxFloat(fn()); });
    }
    public static void BindLambdaBool(UIElement target, string targetProp, Func<bool> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxBool(fn()); });
    }
    public static void BindLambdaInt(UIElement target, string targetProp, Func<int> getter) {
        if (getter == null) { return; }
        BindLambdaOneWay(target, targetProp, () => { var fn = getter; return DependencyObject.BoxInt(fn()); });
    }

    public static void BindLambdaTwoWay(
        UIElement target, string targetProp,
        Func<DependencyObject> getter, Action<DependencyObject> setter) {
        if (target == null || targetProp == null || getter == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 0;
        e.Mode = 1;
        e.LambdaGetter = getter;
        e.LambdaSetter = setter;
        RegisterEntry(e);
    }

    public static void BindPath(
        UIElement target, string targetProp, object source, string path,
        int mode = 0, string converterKey = "", string converterParam = "", string stringFormat = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 1;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.SourcePath = path == null ? "" : path;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        RegisterEntry(e);
    }

    public static void BindElement(
        UIElement target, string targetProp, string elementName, string sourceProp, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 2;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        RegisterEntry(e);
    }

    public static void BindTemplateParent(
        UIElement target, string targetProp, Control templatedParent, string sourceProp) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = 0;
        e.SourceElement = templatedParent;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        RegisterEntry(e);
    }

    public static void BindMulti(
        UIElement target, string targetProp, object source, string[] paths,
        IMultiValueConverter converter, string converterParam = "", string stringFormat = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 4;
        e.Mode = 0;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.MultiPaths = paths;
        e.MultiConverter = converter;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        RegisterEntry(e);
    }

    public static void NotifyTargetPropertyChanged(UIElement target, string targetProp, DependencyObject newBox) {
        if (_entries == null || target == null) { return; }
        for (int i = 0; i < _entries.Length; i += 1) {
            BindingEntry e = _entries[i];
            if (e.Target == target && e.TargetProp == targetProp && e.Mode == 1) {
                e.PushTwoWay(newBox);
            }
        }
    }

    public static void UnbindTarget(UIElement target) {
        if (_entries == null || target == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            if (_entries[i].Target == target) {
                _entries.RemoveAt(i);
            }
        }
    }

    public static void Flush() {
        if (_entries == null) { return; }
        for (int i = 0; i < _entries.Length; i += 1) {
            _entries[i].Evaluate();
        }
    }

    // ---- Reflection Path Traversals ----
    public static DependencyObject ReadPathBox(object root, string path) {
        if (root == null) { return null; }
        if (path == null || path.Length == 0 || path == ".") {
            if (root is DependencyObject) { return root as DependencyObject; }
            return DependencyObject.BoxObject(root);
        }
        string[] segs = path.Split(".");
        object cur = root;
        for (int i = 0; i < segs.Length - 1; i += 1) {
            string seg = segs[i];
            if (cur is UIElement) {
                DependencyObject sub = (cur as UIElement).GetPropertyValue(seg);
                cur = sub != null ? sub.UnboxObject() : Reflect_GetPropObject(cur, seg);
            } else {
                cur = Reflect_GetPropObject(cur, seg);
            }
            if (cur == null) { return null; }
        }
        string leaf = segs[segs.Length - 1];
        if (cur is UIElement) {
            DependencyObject local = (cur as UIElement).GetPropertyValue(leaf);
            if (local != null) { return local; }
        }
        if (cur is DependencyObject && leaf == "Value") {
            return cur as DependencyObject;
        }
        if (!ParseUtil.IsNativeReady()) { return null; }
        object objVal = Reflect_GetPropObject(cur, leaf);
        if (objVal != null) { return DependencyObject.BoxObject(objVal); }
        string sVal = Reflect_GetPropString(cur, leaf);
        if (sVal != null && sVal.Length > 0) { return DependencyObject.BoxString(sVal); }
        float fVal = Reflect_GetPropFloat(cur, leaf);
        if (fVal != 0.0f) { return DependencyObject.BoxFloat(fVal); }
        int iVal = Reflect_GetPropInt(cur, leaf);
        if (iVal != 0) { return DependencyObject.BoxInt(iVal); }
        bool bVal = Reflect_GetPropBool(cur, leaf);
        if (bVal) { return DependencyObject.BoxBool(true); }
        return DependencyObject.BoxString(sVal == null ? "" : sVal);
    }

    public static void WritePathBox(object root, string path, DependencyObject valBox) {
        if (root == null || path == null || path.Length == 0) { return; }
        string[] segs = path.Split(".");
        object cur = root;
        for (int i = 0; i < segs.Length - 1; i += 1) {
            cur = Reflect_GetPropObject(cur, segs[i]);
            if (cur == null) { return; }
        }
        string leaf = segs[segs.Length - 1];
        if (cur is UIElement) {
            if ((cur as UIElement).SetPropertyValue(leaf, valBox)) { return; }
        }
        if (!ParseUtil.IsNativeReady()) { return; }
        if (valBox is BoxedBool) { Reflect_SetPropBool(cur, leaf, (valBox as BoxedBool).Value); return; }
        if (valBox is BoxedInt) { Reflect_SetPropInt(cur, leaf, (valBox as BoxedInt).Value); return; }
        if (valBox is BoxedFloat) { Reflect_SetPropFloat(cur, leaf, (valBox as BoxedFloat).Value); return; }
        if (valBox is BoxedString) { Reflect_SetPropString(cur, leaf, (valBox as BoxedString).Value); return; }
        Reflect_SetPropObject(cur, leaf, DependencyObject.Unbox(valBox));
    }
}
