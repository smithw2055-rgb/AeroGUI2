module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. ResourceDictionary
// ============================================================================

public class ResourceDictionary : DependencyObject {
    private static ResourceDictionary _appResources;
    private static Dictionary<string, ResourceDictionary> _locSources;
    private static Dictionary<string, int> _registeredStyleKeys;
    private static ResourceDictionary[] _ambientStack;

    public static void PushAmbient(ResourceDictionary rd) {
        if (rd == null) { return; }
        if (_ambientStack == null) { _ambientStack = new ResourceDictionary[0]; }
        _ambientStack.Push(rd);
    }

    public static void PopAmbient(ResourceDictionary rd) {
        if (rd == null || _ambientStack == null) { return; }
        for (int i = _ambientStack.Length - 1; i >= 0; i -= 1) {
            if (_ambientStack[i] == rd) {
                _ambientStack.RemoveAt(i);
                return;
            }
        }
    }

    public static DependencyObject FindAmbientBox(string key) {
        if (key == null || key.Length == 0 || _ambientStack == null) { return null; }
        for (int i = _ambientStack.Length - 1; i >= 0; i -= 1) {
            ResourceDictionary rd = _ambientStack[i];
            if (rd != null && rd.Contains(key)) {
                return rd.GetBox(key);
            }
        }
        return null;
    }

    public static object FindAmbientOrGlobal(string key) {
        DependencyObject box = FindAmbientBox(key);
        if (box != null) { return DependencyObject.Unbox(box); }
        ResourceDictionary appRes = GlobalApplicationResources();
        if (appRes != null && appRes.Contains(key)) {
            return DependencyObject.Unbox(appRes.GetBox(key));
        }
        return null;
    }

    private Dictionary<string, DependencyObject> _map;
    private string[] _keys;

    private static string NormalizeLocKey(string src) {
        if (src == null) { return ""; }
        string s = src.Trim();
        while (s.Length > 0) {
            if (s.Length >= 2 && s.Substring(0, 2) == "./") { s = s.Substring(2); }
            else if (s.Substring(0, 1) == "/") { s = s.Substring(1); }
            else { break; }
        }
        return s;
    }

    private static string LocBaseName(string k) {
        int slash = -1;
        for (int i = k.Length - 1; i >= 0; i -= 1) {
            if (k.Substring(i, 1) == "/") { slash = i; break; }
        }
        if (slash >= 0 && slash + 1 < k.Length) {
            return k.Substring(slash + 1);
        }
        return "";
    }

    public static void RegisterLocSource(string sourceName, ResourceDictionary dict) {
        if (sourceName == null || dict == null) { return; }
        if (_locSources == null) { _locSources = new Dictionary<string, ResourceDictionary>(); }
        string k = NormalizeLocKey(sourceName);
        if (k.Length == 0) { return; }
        _locSources[k] = dict;
        string baseName = LocBaseName(k);
        if (baseName.Length > 0) {
            _locSources[baseName] = dict;
        }
    }

    public static ResourceDictionary GetLocSource(string sourceName) {
        if (_locSources == null || sourceName == null) { return null; }
        string k = NormalizeLocKey(sourceName);
        if (k.Length == 0) { return null; }
        if (_locSources.ContainsKey(k)) { return _locSources[k]; }
        string baseName = LocBaseName(k);
        if (baseName.Length > 0 && _locSources.ContainsKey(baseName)) {
            return _locSources[baseName];
        }
        return null;
    }

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

    public static bool MayHaveStyleKey(string key) {
        if (key == null || key.Length == 0 || _registeredStyleKeys == null) { return false; }
        return _registeredStyleKeys.ContainsKey(key);
    }

    public void SetBox(string key, DependencyObject box) {
        if (key == null || key.Length == 0) { return; }
        if (!_map.ContainsKey(key)) {
            _keys.Push(key);
        }
        _map[key] = box;
        if (box is Style || DependencyObject.Unbox(box) is Style) {
            if (_registeredStyleKeys == null) {
                _registeredStyleKeys = new Dictionary<string, int>();
            }
            _registeredStyleKeys[key] = 1;
        }
    }

    public void Set(string key, object val) { SetBox(key, DependencyObject.BoxObject(val)); }
    public void Set(string key, string val) { SetBox(key, DependencyObject.BoxString(val)); }
    public void Set(string key, float val) { SetBox(key, DependencyObject.BoxFloat(val)); }
    public void Set(string key, int val) { SetBox(key, DependencyObject.BoxInt(val)); }
    public void Set(string key, bool val) { SetBox(key, DependencyObject.BoxBool(val)); }

    public DependencyObject GetBox(string key) {
        if (key == null) { return null; }
        if (_map.ContainsKey(key)) { return _map[key]; }
        ResourceDictionary appRes = GlobalApplicationResources();
        if (this != appRes && appRes != null && appRes.Contains(key)) {
            return appRes.GetBox(key);
        }
        return null;
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
    public void AddMergedDictionary(ResourceDictionary other) {
        Merge(other);
    }
}

// ============================================================================
// 2. Setter, Triggers, Style, ControlTemplate, DataTemplate
// ============================================================================

public class Setter : DependencyObject {
    private DependencyObject _valueBox;
    public string Property { get; set; } = "";
    public string TargetName { get; set; } = "";
    public string BindingPath { get; set; } = "";
    public string BindingElementName { get; set; } = "";
    public string BindingRelativeMode { get; set; } = "";
    public string BindingAncestorType { get; set; } = "";
    public IValueConverter BindingConverter { get; set; }
    public string BindingConverterKey { get; set; } = "";
    public string BindingConverterParam { get; set; } = "";
    public string BindingStringFormat { get; set; } = "";
    public int BindingMode { get; set; } = 0;
    public bool BindingHasFallback { get; set; } = false;
    private DependencyObject _bindingFallbackBox;

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

    public static Setter FromBox(string property, DependencyObject valueBox, string targetName = "") {
        var s = new Setter();
        s.Property = property == null ? "" : property;
        s._valueBox = valueBox;
        s.TargetName = targetName == null ? "" : targetName;
        return s;
    }

    public static Setter FromBinding(
        string property, string path,
        IValueConverter conv = null, string converterKey = "", string converterParam = "",
        string stringFormat = "", int mode = 0, string elementName = "",
        string relativeMode = "", string ancestorType = "", string targetName = "") {
        var s = new Setter();
        s.Property = property == null ? "" : property;
        s.BindingPath = path == null ? "" : path;
        s.BindingConverter = conv;
        s.BindingConverterKey = converterKey == null ? "" : converterKey;
        s.BindingConverterParam = converterParam == null ? "" : converterParam;
        s.BindingStringFormat = stringFormat == null ? "" : stringFormat;
        s.BindingMode = mode;
        s.BindingElementName = elementName == null ? "" : elementName;
        s.BindingRelativeMode = relativeMode == null ? "" : relativeMode;
        s.BindingAncestorType = ancestorType == null ? "" : ancestorType;
        s.TargetName = targetName == null ? "" : targetName;
        return s;
    }

    public void SetBindingFallback(DependencyObject fallbackBox) {
        BindingHasFallback = true;
        _bindingFallbackBox = fallbackBox;
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }
    public DependencyObject ValueBox {
        get { return _valueBox; }
        set { _valueBox = value; }
    }

    public string PropertyName() { return Property; }

    public UIElement ResolveTarget(UIElement rootTarget) {
        if (rootTarget == null || Property.Length == 0) { return null; }
        return TargetName.Length > 0 ? rootTarget.FindName(TargetName) : rootTarget;
    }

    public void ApplyTo(UIElement rootTarget) {
        UIElement dest = ResolveTarget(rootTarget);
        if (dest != null) {
            if (BindingPath.Length > 0) {
                BindingEngine.BindPathEx(
                    dest, Property, null, BindingPath, BindingMode,
                    BindingConverter, BindingConverterKey, BindingConverterParam, BindingStringFormat,
                    BindingHasFallback, _bindingFallbackBox, false, null,
                    BindingRelativeMode, BindingAncestorType);
            } else {
                dest.SetPropertyValue(Property, _valueBox);
            }
        }
    }
}

public class TriggerBase : DependencyObject {
    private Setter[] _setters;
    private Storyboard[] _enterActions;
    private Storyboard[] _exitActions;
    private UIElement[] _activeTargets;
    private UIElement[] _savedOwners;
    private UIElement[] _savedDests;
    private string[] _savedProps;

    private static UIElement[] _baseDests;
    private static string[] _baseProps;
    private static DependencyObject[] _baseBoxes;
    private static int[] _baseRefs;

    public TriggerBase() {
        _setters = new Setter[0];
        _enterActions = new Storyboard[0];
        _exitActions = new Storyboard[0];
        _activeTargets = new UIElement[0];
        _savedOwners = new UIElement[0];
        _savedDests = new UIElement[0];
        _savedProps = new string[0];
    }

    private static void EnsureBaseTables() {
        if (_baseDests == null) {
            _baseDests = new UIElement[0];
            _baseProps = new string[0];
            _baseBoxes = new DependencyObject[0];
            _baseRefs = new int[0];
        }
    }

    private static void AcquireBaseValue(UIElement dest, string prop) {
        if (dest == null || prop == null || prop.Length == 0) { return; }
        EnsureBaseTables();
        for (int i = _baseDests.Length - 1; i >= 0; i -= 1) {
            if (_baseDests[i] == dest && _baseProps[i] == prop) {
                _baseRefs[i] = _baseRefs[i] + 1;
                return;
            }
        }
        _baseDests.Push(dest);
        _baseProps.Push(prop);
        _baseBoxes.Push(dest.GetPropertyValue(prop));
        _baseRefs.Push(1);
    }

    private static void ReleaseBaseValue(UIElement dest, string prop) {
        if (dest == null || prop == null || prop.Length == 0 || _baseDests == null) { return; }
        for (int i = _baseDests.Length - 1; i >= 0; i -= 1) {
            if (_baseDests[i] == dest && _baseProps[i] == prop) {
                DependencyObject baseBox = _baseBoxes[i];
                int nextRef = _baseRefs[i] - 1;
                if (nextRef <= 0) {
                    _baseDests.RemoveAt(i);
                    _baseProps.RemoveAt(i);
                    _baseBoxes.RemoveAt(i);
                    _baseRefs.RemoveAt(i);
                } else {
                    _baseRefs[i] = nextRef;
                }
                dest.SetPropertyValue(prop, baseBox);
                return;
            }
        }
    }

    public Setter[] Setters() { return _setters; }

    public void AddSetter(Setter s) { if (s != null) { _setters.Push(s); } }
    public void AddSetter(string prop, object val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, string val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, float val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, int val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, bool val) { _setters.Push(new Setter(prop, val)); }
    public void AddSetterBox(string prop, DependencyObject box, string targetName = "") {
        _setters.Push(Setter.FromBox(prop, box, targetName));
    }
    public void AddEnterAction(Storyboard sb) { if (sb != null) { _enterActions.Push(sb); } }
    public void AddExitAction(Storyboard sb) { if (sb != null) { _exitActions.Push(sb); } }

    public virtual void Attach(UIElement target) {}

    public virtual bool IsMatched(UIElement target) { return false; }

    internal bool WasActiveFor(UIElement target) {
        if (target == null) { return false; }
        for (int i = 0; i < _activeTargets.Length; i += 1) {
            if (_activeTargets[i] == target) { return true; }
        }
        return false;
    }

    private void SetActiveFor(UIElement target, bool active) {
        if (target == null) { return; }
        for (int i = _activeTargets.Length - 1; i >= 0; i -= 1) {
            if (_activeTargets[i] == target) {
                if (!active) { _activeTargets.RemoveAt(i); }
                return;
            }
        }
        if (active) { _activeTargets.Push(target); }
    }

    internal void DeactivateTarget(UIElement target, Style owningStyle, bool runExitActions) {
        if (target == null || !WasActiveFor(target)) { return; }
        SetActiveFor(target, false);
        if (owningStyle == null) {
            for (int i = _savedOwners.Length - 1; i >= 0; i -= 1) {
                if (_savedOwners[i] == target) {
                    UIElement dest = _savedDests[i];
                    string prop = _savedProps[i];
                    _savedOwners.RemoveAt(i);
                    _savedDests.RemoveAt(i);
                    _savedProps.RemoveAt(i);
                    if (dest != null && prop.Length > 0) {
                        ReleaseBaseValue(dest, prop);
                    }
                }
            }
        }
        if (runExitActions) {
            for (int i = 0; i < _exitActions.Length; i += 1) {
                _exitActions[i].Begin(target);
            }
        }
    }

    internal void DeactivateIfUnmatched(UIElement target, Style owningStyle) {
        if (target != null && WasActiveFor(target) && !IsMatched(target)) {
            DeactivateTarget(target, owningStyle, true);
        }
    }

    internal void ApplyIfMatched(UIElement target, Style owningStyle) {
        if (target == null || !IsMatched(target)) { return; }
        bool wasActive = WasActiveFor(target);
        bool savePrev = (owningStyle == null) && !wasActive;
        if (!wasActive) {
            SetActiveFor(target, true);
        }
        for (int i = 0; i < _setters.Length; i += 1) {
            Setter s = _setters[i];
            if (savePrev && s.BindingPath.Length == 0) {
                UIElement dest = s.ResolveTarget(target);
                string prop = s.PropertyName();
                if (dest != null && prop.Length > 0) {
                    AcquireBaseValue(dest, prop);
                    _savedOwners.Push(target);
                    _savedDests.Push(dest);
                    _savedProps.Push(prop);
                }
            }
            s.ApplyTo(target);
        }
        if (!wasActive) {
            for (int i = 0; i < _enterActions.Length; i += 1) {
                _enterActions[i].Begin(target);
            }
        }
    }

    public void Evaluate(UIElement target, Style owningStyle) {
        if (target == null) { return; }
        DeactivateIfUnmatched(target, owningStyle);
        ApplyIfMatched(target, owningStyle);
    }

    public static void EvaluateGroup(TriggerBase[] triggers, UIElement target, Style owningStyle) {
        if (target == null || triggers == null || triggers.Length == 0) { return; }
        for (int i = 0; i < triggers.Length; i += 1) {
            if (triggers[i] != null) { triggers[i].DeactivateIfUnmatched(target, owningStyle); }
        }
        for (int i = 0; i < triggers.Length; i += 1) {
            if (triggers[i] != null) { triggers[i].ApplyIfMatched(target, owningStyle); }
        }
    }

    public static void ResetGroup(TriggerBase[] triggers, UIElement target, Style owningStyle) {
        if (target == null || triggers == null || triggers.Length == 0) { return; }
        for (int i = 0; i < triggers.Length; i += 1) {
            if (triggers[i] != null) { triggers[i].DeactivateTarget(target, owningStyle, false); }
        }
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
    public static Trigger CreateBox(string property, DependencyObject valueBox, string sourceName = "") {
        var t = new Trigger();
        t.Property = property == null ? "" : property;
        t._valueBox = valueBox;
        t.SourceName = sourceName == null ? "" : sourceName;
        return t;
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
    public string RelativeSourceMode { get; set; } = "";
    public string RelativeSourceAncestorType { get; set; } = "";
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
    public DataTrigger(string bindingPath, int value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxInt(value);
    }
    public DataTrigger(string bindingPath, float value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxFloat(value);
    }
    public static DataTrigger CreateBox(string bindingPath, DependencyObject valueBox, string relMode = "", string ancestorType = "") {
        var dt = new DataTrigger();
        dt.BindingPath = bindingPath == null ? "" : bindingPath;
        dt._valueBox = valueBox;
        dt.RelativeSourceMode = relMode == null ? "" : relMode;
        dt.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        return dt;
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }

    public override bool IsMatched(UIElement target) {
        if (target == null || BindingPath.Length == 0) { return false; }
        DependencyObject actual = null;
        if (RelativeSourceMode == "Self") {
            actual = target.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(target, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && anc.GetTypeName() != RelativeSourceAncestorType) {
                anc = anc.Parent;
            }
            if (anc != null) {
                actual = anc.GetPropertyValue(BindingPath);
                if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
            }
        } else {
            object dc = target.DataContext;
            if (dc == null) { return _valueBox == null; }
            actual = BindingEngine.ReadPathBox(dc, BindingPath);
        }
        return Trigger.BoxEquals(actual, _valueBox);
    }
}

public class TriggerCondition : DependencyObject {
    public string BindingPath { get; set; } = "";
    public string ElementName { get; set; } = "";
    public string RelativeSourceMode { get; set; } = "";
    public string RelativeSourceAncestorType { get; set; } = "";
    public DependencyObject ValueBox { get; set; }

    public TriggerCondition() {}
    public TriggerCondition(
        string bindingPath, DependencyObject valueBox,
        string elementName = "", string relMode = "", string ancestorType = "") {
        BindingPath = bindingPath == null ? "" : bindingPath;
        ValueBox = valueBox;
        ElementName = elementName == null ? "" : elementName;
        RelativeSourceMode = relMode == null ? "" : relMode;
        RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
    }

    public UIElement ResolveElementSource(UIElement target) {
        if (target == null || ElementName.Length == 0) { return null; }
        UIElement se = BindingEntry.FindElementScoped(target, ElementName);
        if (se == null) {
            UIElement root = target.RootElement();
            if (root != null) { se = root.FindName(ElementName); }
        }
        return se;
    }

    public bool Evaluate(UIElement target) {
        if (target == null || BindingPath.Length == 0) { return false; }
        DependencyObject actual = null;
        if (ElementName.Length > 0) {
            UIElement se = ResolveElementSource(target);
            if (se == null) { return false; }
            actual = se.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(se, BindingPath); }
            if (actual == null && se.DataContext != null) { actual = BindingEngine.ReadPathBox(se.DataContext, BindingPath); }
        } else if (RelativeSourceMode == "Self") {
            actual = target.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(target, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && anc.GetTypeName() != RelativeSourceAncestorType) {
                anc = anc.Parent;
            }
            if (anc != null) {
                actual = anc.GetPropertyValue(BindingPath);
                if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
            }
        } else {
            object dc = target.DataContext;
            if (dc == null) { return ValueBox == null; }
            actual = BindingEngine.ReadPathBox(dc, BindingPath);
        }
        return Trigger.BoxEquals(actual, ValueBox);
    }
}

public class MultiDataTrigger : TriggerBase {
    private TriggerCondition[] _conditions;
    private UIElement[] _hookedTargets;
    private UIElement[] _hookedSources;

    public MultiDataTrigger() : base() {
        _conditions = new TriggerCondition[0];
        _hookedTargets = new UIElement[0];
        _hookedSources = new UIElement[0];
    }

    public void AddCondition(
        string bindingPath, DependencyObject valueBox,
        string elementName = "", string relMode = "", string ancestorType = "") {
        _conditions.Push(new TriggerCondition(bindingPath, valueBox, elementName, relMode, ancestorType));
    }

    private bool HasHooked(UIElement target, UIElement src) {
        for (int i = 0; i < _hookedTargets.Length; i += 1) {
            if (_hookedTargets[i] == target && _hookedSources[i] == src) { return true; }
        }
        return false;
    }

    private void HookElementSources(UIElement target) {
        if (target == null) { return; }
        for (int i = 0; i < _conditions.Length; i += 1) {
            TriggerCondition cond = _conditions[i];
            if (cond.ElementName.Length == 0) { continue; }
            UIElement se = cond.ResolveElementSource(target);
            if (se == null || HasHooked(target, se)) { continue; }
            _hookedTargets.Push(target);
            _hookedSources.Push(se);
            se.AddStateInvalidatedHook(() => { Evaluate(target, null); });
        }
    }

    public override void Attach(UIElement target) {
        if (target == null) { return; }
        HookElementSources(target);
        target.Loaded += (object s, RoutedEventArgs e) => {
            HookElementSources(target);
            Evaluate(target, null);
        };
    }

    public override bool IsMatched(UIElement target) {
        if (target == null || _conditions.Length == 0) { return false; }
        for (int i = 0; i < _conditions.Length; i += 1) {
            if (!_conditions[i].Evaluate(target)) { return false; }
        }
        return true;
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
    private BehaviorTriggerBase[] _behaviorTriggers;
    private UIElement[] _evaluatingTargets;

    public Style() {
        _setters = new Setter[0];
        _triggers = new TriggerBase[0];
        _behaviorTriggers = new BehaviorTriggerBase[0];
        _evaluatingTargets = new UIElement[0];
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
    public void AddSetterBox(string propName, DependencyObject valueBox, string targetName = "") {
        _setters.Push(Setter.FromBox(propName, valueBox, targetName));
    }

    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { _triggers.Push(trigger); } }
    public void AddDataTrigger(DataTrigger trigger) { AddTrigger(trigger); }
    public void AddEventTrigger(EventTrigger trigger) { AddTrigger(trigger); }
    public void AddBehaviorTrigger(BehaviorTriggerBase trigger) {
        if (trigger != null) { _behaviorTriggers.Push(trigger); }
    }

    private static Style[] CollectChain(Style leaf) {
        var chain = new Style[0];
        Style cur = leaf;
        while (cur != null && chain.Length < 32) {
            chain.Push(cur);
            cur = cur.BasedOn;
        }
        return chain;
    }

    public void ApplySettersOnly(UIElement target) {
        if (target == null) { return; }
        Style[] chain = Style.CollectChain(this);
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Setter[] ss = chain[c]._setters;
            for (int i = 0; i < ss.Length; i += 1) {
                ss[i].ApplyTo(target);
            }
        }
    }

    public void ApplyTo(UIElement target) {
        if (target == null) { return; }
        Style[] chain = Style.CollectChain(this);
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Style st = chain[c];
            for (int i = 0; i < st._setters.Length; i += 1) {
                st._setters[i].ApplyTo(target);
            }
            for (int i = 0; i < st._triggers.Length; i += 1) {
                st._triggers[i].Attach(target);
            }
            for (int i = 0; i < st._behaviorTriggers.Length; i += 1) {
                st._behaviorTriggers[i].Attach(target);
            }
        }
        EvaluateTriggers(target);
    }

    public void EvaluateTriggers(UIElement target) {
        if (target == null) { return; }
        if (_evaluatingTargets == null) { _evaluatingTargets = new UIElement[0]; }
        for (int e = 0; e < _evaluatingTargets.Length; e += 1) {
            if (_evaluatingTargets[e] == target) { return; }
        }
        _evaluatingTargets.Push(target);
        Style[] chain = Style.CollectChain(this);
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Style st = chain[c];
            bool anyReverted = false;
            for (int i = 0; i < st._triggers.Length; i += 1) {
                if (st._triggers[i].WasActiveFor(target) && !st._triggers[i].IsMatched(target)) { anyReverted = true; break; }
            }
            if (anyReverted) {
                for (int i = 0; i < st._setters.Length; i += 1) {
                    st._setters[i].ApplyTo(target);
                }
            }
            TriggerBase.EvaluateGroup(st._triggers, target, st);
        }
        for (int e = _evaluatingTargets.Length - 1; e >= 0; e -= 1) {
            if (_evaluatingTargets[e] == target) {
                _evaluatingTargets.RemoveAt(e);
                break;
            }
        }
    }
}

public class ControlTemplate : DependencyObject {
    public string TargetType { get; set; } = "";
    private Func<Control, UIElement> _factory;
    private TriggerBase[] _triggers;
    private UIElement[] _evaluatingOwners;
    public ResourceDictionary Resources { get; set; }
    public ResourceDictionary AmbientResources { get; set; }

    public ControlTemplate() {
        _triggers = new TriggerBase[0];
        _evaluatingOwners = new UIElement[0];
        Resources = new ResourceDictionary();
    }
    public ControlTemplate(Func<Control, UIElement> factory) : this() {
        _factory = factory;
    }
    public ControlTemplate(string targetType, Func<Control, UIElement> factory) : this() {
        TargetType = targetType == null ? "" : targetType;
        _factory = factory;
    }

    public Func<Control, UIElement> Factory {
        get { return _factory; }
        set { _factory = value; }
    }
    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { _triggers.Push(trigger); } }
    public TriggerBase[] Triggers() { return _triggers; }

    public UIElement Instantiate(Control owner) {
        if (_factory == null) { return null; }
        ResourceDictionary.PushAmbient(AmbientResources);
        ResourceDictionary.PushAmbient(Resources);
        UIElement root = _factory(owner);
        if (root != null) {
            if (AmbientResources != null && root.AmbientResources == null) {
                root.AmbientResources = AmbientResources;
            }
            if (Resources != null && Resources.Keys().Length > 0) {
                root.Resources.Merge(Resources);
            }
            MarkTemplatedParentSubtree(root, owner);
            BindingEngine.FlushSubtree(root);
        }
        for (int i = 0; i < _triggers.Length; i += 1) {
            _triggers[i].Attach(owner);
        }
        ResourceDictionary.PopAmbient(Resources);
        ResourceDictionary.PopAmbient(AmbientResources);
        return root;
    }

    public void EvaluateTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        UIElement ownerEl = owner;
        if (_evaluatingOwners == null) { _evaluatingOwners = new UIElement[0]; }
        for (int i = 0; i < _evaluatingOwners.Length; i += 1) {
            if (_evaluatingOwners[i] == ownerEl) { return; }
        }
        _evaluatingOwners.Push(ownerEl);
        TriggerBase.EvaluateGroup(_triggers, owner, null);
        for (int i = _evaluatingOwners.Length - 1; i >= 0; i -= 1) {
            if (_evaluatingOwners[i] == ownerEl) {
                _evaluatingOwners.RemoveAt(i);
                break;
            }
        }
    }

    public void ResetTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        TriggerBase.ResetGroup(_triggers, owner, null);
    }

    private static void MarkTemplatedParentSubtree(UIElement rootNode, UIElement owner) {
        if (rootNode == null || owner == null) { return; }
        var q = new UIElement[0];
        q.Push(rootNode);
        int idx = 0;
        while (idx < q.Length) {
            UIElement node = q[idx];
            idx += 1;
            if (node == null) { continue; }
            if (node.TemplatedParent == null) { node.TemplatedParent = owner; }
            if (node is ContentPresenter || node.TemplatedParent != owner) { continue; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
        }
    }
}

public class DataTemplate : DependencyObject {
    public string DataType { get; set; } = "";
    private Func<object, UIElement> _factory;
    private TriggerBase[] _triggers;
    public ResourceDictionary Resources { get; set; }
    public ResourceDictionary AmbientResources { get; set; }

    public DataTemplate() {
        _triggers = new TriggerBase[0];
        Resources = new ResourceDictionary();
    }
    public DataTemplate(Func<object, UIElement> factory) : this() {
        _factory = factory;
    }
    public DataTemplate(string dataType, Func<object, UIElement> factory) : this() {
        DataType = dataType == null ? "" : dataType;
        _factory = factory;
    }

    public Func<object, UIElement> Factory {
        get { return _factory; }
        set { _factory = value; }
    }
    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { _triggers.Push(trigger); } }

    public virtual UIElement Instantiate(object item) {
        if (_factory == null) { return null; }
        ResourceDictionary.PushAmbient(AmbientResources);
        ResourceDictionary.PushAmbient(Resources);
        UIElement root = _factory(item);
        if (root != null) {
            if (AmbientResources != null && root.AmbientResources == null) {
                root.AmbientResources = AmbientResources;
            }
            if (Resources != null && Resources.Keys().Length > 0) {
                root.Resources.Merge(Resources);
            }
            if (item != null) {
                root.DataContext = item;
            }
            if (_triggers != null && _triggers.Length > 0) {
                AttachTriggers(root);
            }
        }
        ResourceDictionary.PopAmbient(Resources);
        ResourceDictionary.PopAmbient(AmbientResources);
        return root;
    }

    private void AttachTriggers(UIElement root) {
        TriggerBase[] list = _triggers;
        for (int i = 0; i < list.Length; i += 1) {
            list[i].Attach(root);
        }
        TriggerBase.EvaluateGroup(list, root, null);
        root.Loaded += (object s, RoutedEventArgs e) => {
            TriggerBase.EvaluateGroup(list, root, null);
        };
        root.DataContextChanged += (object s, DependencyPropertyChangedEventArgs e) => {
            TriggerBase.EvaluateGroup(list, root, null);
        };
        root.GotFocus += (object s, RoutedEventArgs e) => {
            TriggerBase.EvaluateGroup(list, root, null);
        };
        root.LostFocus += (object s, RoutedEventArgs e) => {
            TriggerBase.EvaluateGroup(list, root, null);
        };
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
    public string StoryboardKey { get; set; } = "";
    private Storyboard _storyboard;
    private Setter[] _setters;
    private UIElement[] _savedTargets;
    private string[] _savedProps;
    private DependencyObject[] _savedBoxes;

    public VisualState() {
        _setters = new Setter[0];
        _savedTargets = new UIElement[0];
        _savedProps = new string[0];
        _savedBoxes = new DependencyObject[0];
    }
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
    public void AddSetterBox(string propName, DependencyObject valueBox, string targetName = "") {
        _setters.Push(Setter.FromBox(propName, valueBox, targetName));
    }

    public Setter[] Setters() { return _setters; }

    internal void StartStoryboardOnly(UIElement target) {
        if (target == null) { return; }
        if (_storyboard == null && StoryboardKey.Length > 0) {
            DependencyObject box = target.FindResourceBox(StoryboardKey);
            if (box is Storyboard) { _storyboard = box as Storyboard; }
        }
        if (_storyboard != null) {
            _storyboard.Begin(target);
        }
    }

    internal void StopStoryboardOnly(UIElement target) {
        if (_storyboard != null && target != null) {
            _storyboard.Stop(target);
        }
    }

    public void Enter(UIElement target) {
        if (target == null) { return; }
        Exit(target);
        for (int i = 0; i < _setters.Length; i += 1) {
            Setter s = _setters[i];
            if (s.BindingPath.Length == 0) {
                UIElement dest = s.ResolveTarget(target);
                string prop = s.PropertyName();
                if (dest != null && prop.Length > 0) {
                    _savedTargets.Push(dest);
                    _savedProps.Push(prop);
                    _savedBoxes.Push(dest.GetPropertyValue(prop));
                }
            }
            s.ApplyTo(target);
        }
        StartStoryboardOnly(target);
    }

    public void Exit(UIElement target) {
        for (int i = _savedTargets.Length - 1; i >= 0; i -= 1) {
            if (_savedTargets[i] != null) {
                _savedTargets[i].SetPropertyValue(_savedProps[i], _savedBoxes[i]);
            }
        }
        _savedTargets.Clear();
        _savedProps.Clear();
        _savedBoxes.Clear();
        StopStoryboardOnly(target);
    }
}

public class VisualTransition : DependencyObject {
    public string From { get; set; } = "";
    public string To { get; set; } = "";
    public float GeneratedDurationSec { get; set; } = 0.0f;
    public EasingFunctionKind EasingKind { get; set; } = EasingFunctionKind.Linear;
    public EasingMode EasingMode { get; set; } = EasingMode.EaseOut;
    public Storyboard Storyboard { get; set; }

    public void SetEasing(int kind, int mode) {
        EasingKind = Storyboard.IntToEasingKind(kind);
        EasingMode = Storyboard.IntToEasingMode(mode);
    }
}

public class VisualStateGroup : DependencyObject {
    public string Name { get; set; } = "";
    public string CurrentStateName { get; set; } = "";
    private VisualState _currentState;
    private VisualState[] _states;
    private VisualTransition[] _transitions;
    private UIElement[] _baseTargets;
    private string[] _baseProps;
    private DependencyObject[] _baseBoxes;
    private bool _hasActiveTransition;

    public VisualStateGroup() {
        _states = new VisualState[0];
        _transitions = new VisualTransition[0];
        _baseTargets = new UIElement[0];
        _baseProps = new string[0];
        _baseBoxes = new DependencyObject[0];
    }
    public VisualStateGroup(string name) : this() { Name = name == null ? "" : name; }

    public void AddState(VisualState st) { if (st != null) { _states.Push(st); } }
    public void AddTransition(VisualTransition tr) { if (tr != null) { _transitions.Push(tr); } }

    public VisualState FindState(string name) {
        for (int i = 0; i < _states.Length; i += 1) {
            if (_states[i].Name == name) { return _states[i]; }
        }
        return null;
    }

    private VisualTransition FindBestTransition(string fromName, string toName) {
        VisualTransition best = null;
        int bestScore = -1;
        for (int i = 0; i < _transitions.Length; i += 1) {
            VisualTransition tr = _transitions[i];
            if (tr == null) { continue; }
            bool fromMatch = tr.From.Length == 0 || tr.From == fromName;
            bool toMatch = tr.To.Length == 0 || tr.To == toName;
            if (!fromMatch || !toMatch) { continue; }
            int score = (tr.From.Length > 0 ? 2 : 0) + (tr.To.Length > 0 ? 4 : 0);
            if (score > bestScore) {
                bestScore = score;
                best = tr;
            }
        }
        return best;
    }

    private void RecordBaseSnapshot(UIElement dest, string prop) {
        if (dest == null || prop == null || prop.Length == 0) { return; }
        for (int i = 0; i < _baseTargets.Length; i += 1) {
            if (_baseTargets[i] == dest && _baseProps[i] == prop) {
                return;
            }
        }
        _baseTargets.Push(dest);
        _baseProps.Push(prop);
        _baseBoxes.Push(dest.GetPropertyValue(prop));
    }

    private static bool IsColorAnimPropId(int propId) {
        return propId == 16 || propId == 17 || propId == 18 || propId == 29 || propId == 30;
    }

    public bool GoTo(VisualState next, UIElement scope, bool useTransitions = true) {
        if (next == null) { return false; }
        if (_currentState == next && CurrentStateName == next.Name) { return true; }

        Setter[] nextSetters = next.Setters();
        if (scope != null && nextSetters != null) {
            for (int i = 0; i < nextSetters.Length; i += 1) {
                Setter s = nextSetters[i];
                if (s.BindingPath.Length == 0) {
                    UIElement dest = s.ResolveTarget(scope);
                    string prop = s.PropertyName();
                    RecordBaseSnapshot(dest, prop);
                }
            }
        }

        VisualTransition tr = useTransitions ? FindBestTransition(CurrentStateName, next.Name) : null;
        float dur = tr != null ? tr.GeneratedDurationSec : 0.0f;

        var animTargets = new UIElement[0];
        var animPropIds = new int[0];
        var animIsColor = new bool[0];
        var startFloats = new float[0];
        var startColors = new uint[0];

        if (scope != null && dur > 0.001f) {
            for (int i = 0; i < _baseTargets.Length; i += 1) {
                UIElement dest = _baseTargets[i];
                string prop = _baseProps[i];
                if (dest == null || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { continue; }
                int propId = EnumConvert.ParseAnimPropOrInvalid(prop);
                if (propId < 0) { continue; }
                bool isCol = IsColorAnimPropId(propId);
                animTargets.Push(dest);
                animPropIds.Push(propId);
                animIsColor.Push(isCol);
                if (isCol) {
                    startFloats.Push(0.0f);
                    startColors.Push(Anim_ReadCurrentColor(dest.__nativeHandle, propId));
                } else {
                    startFloats.Push(Anim_ReadCurrentFloat(dest.__nativeHandle, propId));
                    startColors.Push(0u);
                }
            }
        }

        if (_currentState != null) {
            _currentState.StopStoryboardOnly(scope);
        }

        if (dur <= 0.001f && _hasActiveTransition && scope != null) {
            for (int i = 0; i < _baseTargets.Length; i += 1) {
                UIElement dest = _baseTargets[i];
                if (dest != null && ParseUtil.IsValidHandle(dest.__nativeHandle)) {
                    Anim_StopAll(dest.__nativeHandle);
                }
            }
            _hasActiveTransition = false;
        }

        for (int i = _baseTargets.Length - 1; i >= 0; i -= 1) {
            if (_baseTargets[i] != null) {
                _baseTargets[i].SetPropertyValue(_baseProps[i], _baseBoxes[i]);
            }
        }

        if (scope != null && nextSetters != null) {
            for (int i = 0; i < nextSetters.Length; i += 1) {
                nextSetters[i].ApplyTo(scope);
            }
        }

        _currentState = next;
        CurrentStateName = next.Name;

        if (scope != null && dur > 0.001f && animTargets.Length > 0) {
            int ek = tr != null ? EnumConvert.ToInt(tr.EasingKind) : 0;
            int em = tr != null ? EnumConvert.ToInt(tr.EasingMode) : 0;
            for (int i = 0; i < animTargets.Length; i += 1) {
                UIElement dest = animTargets[i];
                int propId = animPropIds[i];
                if (animIsColor[i]) {
                    uint startCol = startColors[i];
                    uint endCol = Anim_ReadCurrentColor(dest.__nativeHandle, propId);
                    if (endCol != startCol) {
                        Anim_StartColorDelayed(dest.__nativeHandle, propId, startCol, endCol, dur, ek, em, 0.0f);
                        _hasActiveTransition = true;
                    }
                } else {
                    float startVal = startFloats[i];
                    float endVal = Anim_ReadCurrentFloat(dest.__nativeHandle, propId);
                    if (Math.Absf(endVal - startVal) > 0.0005f) {
                        Anim_StartFloat(dest.__nativeHandle, propId, startVal, endVal, dur, ek, em, 0.0f, false, 1);
                        _hasActiveTransition = true;
                    }
                }
            }
            if (tr != null && tr.Storyboard != null) {
                tr.Storyboard.Begin(scope);
            }
        }

        if (scope != null) {
            next.StartStoryboardOnly(scope);
        }
        return true;
    }
}

public class VisualStateGroupList : DependencyObject {
    public VisualStateGroup[] Items;
    public VisualStateGroupList(VisualStateGroup[] items) { Items = items; }
}

public static class VisualStateManager {
    private static VisualStateGroup[] _emptyGroups;

    public static void SetVisualStateGroups(UIElement control, VisualStateGroup[] groups) {
        if (control == null || groups == null) { return; }
        control._vsgBox = new VisualStateGroupList(groups);
    }

    public static VisualStateGroup[] GetVisualStateGroups(UIElement control) {
        if (control != null && control._vsgBox is VisualStateGroupList) {
            return (control._vsgBox as VisualStateGroupList).Items;
        }
        if (_emptyGroups == null) { _emptyGroups = new VisualStateGroup[0]; }
        return _emptyGroups;
    }

    private static bool TryGoToInSubtree(UIElement node, UIElement templatedOwner, string stateName, bool useTransitions) {
        if (node == null) { return false; }
        if (node._vsgBox is VisualStateGroupList) {
            VisualStateGroup[] groups = (node._vsgBox as VisualStateGroupList).Items;
            for (int i = 0; i < groups.Length; i += 1) {
                VisualState st = groups[i].FindState(stateName);
                if (st != null) {
                    return groups[i].GoTo(st, node, useTransitions);
                }
            }
        }
        int cc = node.ChildCount();
        for (int c = 0; c < cc; c += 1) {
            UIElement ch = node.ChildAt(c);
            if (ch != null && ch.TemplatedParent == templatedOwner) {
                if (TryGoToInSubtree(ch, templatedOwner, stateName, useTransitions)) {
                    return true;
                }
            }
        }
        return false;
    }

    private static bool HasStateInSubtree(UIElement node, UIElement templatedOwner, string stateName) {
        if (node == null) { return false; }
        if (node._vsgBox is VisualStateGroupList) {
            VisualStateGroup[] groups = (node._vsgBox as VisualStateGroupList).Items;
            for (int i = 0; i < groups.Length; i += 1) {
                if (groups[i].FindState(stateName) != null) {
                    return true;
                }
            }
        }
        int cc = node.ChildCount();
        for (int c = 0; c < cc; c += 1) {
            UIElement ch = node.ChildAt(c);
            if (ch != null && ch.TemplatedParent == templatedOwner) {
                if (HasStateInSubtree(ch, templatedOwner, stateName)) {
                    return true;
                }
            }
        }
        return false;
    }

    public static bool HasState(UIElement control, string stateName) {
        if (control == null || stateName == null) { return false; }
        if (control is Control) {
            UIElement tplRoot = (control as Control).TemplateRoot();
            if (tplRoot != null && HasStateInSubtree(tplRoot, control, stateName)) {
                return true;
            }
        }
        if (control._vsgBox is VisualStateGroupList) {
            VisualStateGroup[] groups = (control._vsgBox as VisualStateGroupList).Items;
            for (int i = 0; i < groups.Length; i += 1) {
                if (groups[i].FindState(stateName) != null) {
                    return true;
                }
            }
        }
        return false;
    }

    public static bool GoToState(UIElement control, string stateName, bool useTransitions = true) {
        if (control == null || stateName == null) { return false; }
        if (control is Control) {
            UIElement tplRoot = (control as Control).TemplateRoot();
            if (tplRoot != null && TryGoToInSubtree(tplRoot, control, stateName, useTransitions)) {
                return true;
            }
        }
        if (control._vsgBox is VisualStateGroupList) {
            VisualStateGroup[] groups = (control._vsgBox as VisualStateGroupList).Items;
            for (int i = 0; i < groups.Length; i += 1) {
                VisualState st = groups[i].FindState(stateName);
                if (st != null) {
                    return groups[i].GoTo(st, control, useTransitions);
                }
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
    public uint FromRgba { get; set; } = 0u;
    public uint ToRgba { get; set; } = 0u;
    public bool HasColorUint { get; set; } = false;
    public bool UseSpring { get; set; } = false;
    public float Stiffness { get; set; } = 320.0f;
    public float Damping { get; set; } = 28.0f;
    public DoubleKeyFrame[] KeyFrames { get; set; }

    public TimelineAnimation() { KeyFrames = new DoubleKeyFrame[0]; }

    private static UIElement ResolveAnimTarget(UIElement rootScope, string targetName) {
        if (rootScope == null) { return null; }
        if (targetName == null || targetName.Length == 0) { return rootScope; }
        UIElement found = rootScope.FindName(targetName);
        if (found != null) { return found; }
        if (rootScope.TemplatedParent != null) {
            found = rootScope.TemplatedParent.FindName(targetName);
            if (found != null) { return found; }
        }
        UIElement cur = rootScope.Parent;
        while (cur != null) {
            found = cur.FindName(targetName);
            if (found != null) { return found; }
            cur = cur.Parent;
        }
        return null;
    }

    public void StartOn(UIElement rootScope) {
        if (rootScope == null) { return; }
        UIElement dest = ResolveAnimTarget(rootScope, TargetName);
        if (dest == null || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { return; }
        int propId = EnumConvert.ParseAnimPropOrInvalid(TargetProperty);
        if (propId < 0) { return; }
        int ek = EnumConvert.ToInt(EasingKind);
        int em = EnumConvert.ToInt(EasingMode);

        if (IsColor) {
            uint toCol = HasColorUint ? ToRgba : (ToColor.Length > 0 ? ParseUtil.ParseColor(ToColor) : 0u);
            if (HasColorUint || HasFrom || FromColor.Length > 0) {
                uint fromCol = HasColorUint ? FromRgba : ParseUtil.ParseColor(FromColor);
                Anim_StartColorDelayed(dest.__nativeHandle, propId, fromCol, toCol, DurationSec, ek, em, DelaySec);
            } else {
                Anim_StartColorTo(dest.__nativeHandle, propId, toCol, DurationSec, ek, em, DelaySec);
            }
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
        if (!HasFrom) {
            Anim_StartFloatTo(dest.__nativeHandle, propId, ToFloat, DurationSec, ek, em, DelaySec, AutoReverse, RepeatCount);
            return;
        }
        Anim_StartFloat(dest.__nativeHandle, propId, FromFloat, ToFloat, DurationSec, ek, em, DelaySec, AutoReverse, RepeatCount);
    }

    public void StopOn(UIElement rootScope) {
        if (rootScope == null) { return; }
        UIElement dest = ResolveAnimTarget(rootScope, TargetName);
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

    internal static EasingFunctionKind IntToEasingKind(int kind) {
        switch (kind) {
            case 1: return EasingFunctionKind.Quadratic;
            case 2: return EasingFunctionKind.Cubic;
            case 3: return EasingFunctionKind.Quartic;
            case 4: return EasingFunctionKind.Quintic;
            case 5: return EasingFunctionKind.Sine;
            case 6: return EasingFunctionKind.Circle;
            case 7: return EasingFunctionKind.Exponential;
            case 8: return EasingFunctionKind.Back;
            case 9: return EasingFunctionKind.Bounce;
            case 10: return EasingFunctionKind.Elastic;
            case 11: return EasingFunctionKind.Spline;
        }
        return EasingFunctionKind.Linear;
    }

    internal static EasingMode IntToEasingMode(int mode) {
        if (mode == 1) { return EasingMode.EaseIn; }
        if (mode == 2) { return EasingMode.EaseInOut; }
        return EasingMode.EaseOut;
    }

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
        string targetName, string targetProp, float from, float to, float durationSec,
        int easeKind, int easeMode, float delaySec) {
        var da = new DoubleAnimation(from, to, durationSec);
        da.TargetName = targetName == null ? "" : targetName;
        da.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        da.EasingKind = IntToEasingKind(easeKind);
        da.EasingMode = IntToEasingMode(easeMode);
        da.DelaySec = delaySec;
        _children.Push(da);
    }

    public void AddDoubleToAnimation(
        string targetName, string targetProp, float to, float durationSec,
        int easeKind, int easeMode, float delaySec) {
        var da = new DoubleAnimation(to, durationSec);
        da.TargetName = targetName == null ? "" : targetName;
        da.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        da.EasingKind = IntToEasingKind(easeKind);
        da.EasingMode = IntToEasingMode(easeMode);
        da.DelaySec = delaySec;
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

    public void AddColorAnimation(
        string targetName, string targetProp, string fromColor, string toColor, float durationSec,
        int easeKind, int easeMode, float delaySec) {
        var ca = new ColorAnimation(fromColor, toColor, durationSec);
        ca.TargetName = targetName == null ? "" : targetName;
        ca.TargetProperty = targetProp == null ? "Background" : targetProp;
        ca.EasingKind = IntToEasingKind(easeKind);
        ca.EasingMode = IntToEasingMode(easeMode);
        ca.DelaySec = delaySec;
        _children.Push(ca);
    }

    public void AddColorAnimation(
        string targetName, string targetProp, uint fromRgba, uint toRgba, float durationSec,
        int easeKind, int easeMode, float delaySec) {
        var ca = new ColorAnimation();
        ca.TargetName = targetName == null ? "" : targetName;
        ca.TargetProperty = targetProp == null ? "Background" : targetProp;
        ca.HasColorUint = true;
        ca.FromRgba = fromRgba;
        ca.ToRgba = toRgba;
        ca.DurationSec = durationSec;
        ca.EasingKind = IntToEasingKind(easeKind);
        ca.EasingMode = IntToEasingMode(easeMode);
        ca.DelaySec = delaySec;
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
    public string RelativeSourceMode { get; set; } = "";
    public string RelativeSourceAncestorType { get; set; } = "";
    public bool HasFallbackValue { get; set; } = false;
    public bool HasTargetNullValue { get; set; } = false;
    private DependencyObject _fallbackBox;
    private DependencyObject _targetNullBox;

    public Binding() {}
    public Binding(string path) { Path = path == null ? "" : path; }

    public object Source {
        get { return DependencyObject.Unbox(_sourceBox); }
        set { _sourceBox = DependencyObject.BoxObject(value); }
    }
    public DependencyObject SourceBox {
        get { return _sourceBox; }
        set { _sourceBox = value; }
    }
    public object FallbackValue {
        get { return DependencyObject.Unbox(_fallbackBox); }
        set { _fallbackBox = DependencyObject.BoxObject(value); HasFallbackValue = true; }
    }
    public DependencyObject FallbackBox {
        get { return _fallbackBox; }
        set { _fallbackBox = value; HasFallbackValue = true; }
    }
    public object TargetNullValue {
        get { return DependencyObject.Unbox(_targetNullBox); }
        set { _targetNullBox = DependencyObject.BoxObject(value); HasTargetNullValue = true; }
    }
    public DependencyObject TargetNullBox {
        get { return _targetNullBox; }
        set { _targetNullBox = value; HasTargetNullValue = true; }
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
    public Inline TargetInline;
    public string TargetProp = "";
    public int Mode = 0; // 0=OneWay, 1=TwoWay, 2=OneTime
    public int Kind = 0; // 0=Lambda, 1=Path, 2=Element, 3=TemplateParent, 4=Multi

    public Func<DependencyObject> LambdaGetter;
    public Action<DependencyObject> LambdaSetter;

    public DependencyObject ExplicitSourceBox;
    public UIElement SourceElement;
    public string SourcePath = "";
    public string[] MultiPaths;
    public Binding[] MultiBindings;
    public string ElementName = "";
    public string RelativeSourceMode = "";
    public string RelativeSourceAncestorType = "";
    public IValueConverter Converter;
    public IMultiValueConverter MultiConverter;
    public string ConverterKey = "";
    public string ConverterParam = "";
    public string StringFormat = "";
    public bool HasFallback = false;
    public DependencyObject FallbackBox;
    public bool HasTargetNull = false;
    public DependencyObject TargetNullBox;

    public string LastHash = "";
    public int LastDirtyMask = -1;
    public bool OneTimeDone = false;
    private UIElement _lastFindNameRoot;
    private DependencyObject _lastTemplateValBox;
    private UIElement[] _multiElementSources;
    private UIElement[] _multiFindRoots;

    internal static UIElement FindElementScoped(UIElement host, string elementName) {
        if (host == null || elementName == null || elementName.Length == 0) { return null; }
        if (host.TemplatedParent != null) {
            UIElement t = host;
            while (t.Parent != null && t.Parent.TemplatedParent == host.TemplatedParent) {
                t = t.Parent;
            }
            UIElement localHit = t.FindName(elementName);
            if (localHit != null) { return localHit; }
        }
        UIElement cur = host;
        while (cur.Parent != null) {
            cur = cur.Parent;
            if (cur is ContentPresenter || cur is UserControl) {
                UIElement scopeHit = cur.FindName(elementName);
                if (scopeHit != null) { return scopeHit; }
                break;
            }
        }
        return null;
    }

    public DependencyObject ResolveSourceBox() {
        if (ExplicitSourceBox != null) { return ExplicitSourceBox; }
        UIElement host = Target != null ? Target : (TargetInline != null ? TargetInline.OwnerTextBlock : null);
        if (RelativeSourceMode == "Self") {
            return Target != null ? (DependencyObject)Target : (DependencyObject)TargetInline;
        }
        if (RelativeSourceMode == "TemplatedParent" && host != null) {
            return host.TemplatedParent;
        }
        if ((RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") && host != null) {
            UIElement anc = host;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && anc.GetTypeName() != RelativeSourceAncestorType) {
                anc = anc.Parent;
            }
            if (anc != null) { return anc; }
        }
        if (host != null) { return host.EffectiveDataContextBox(); }
        return null;
    }

    public IValueConverter ResolveConverter() {
        if (Converter != null) { return Converter; }
        UIElement host = Target != null ? Target : (TargetInline != null ? TargetInline.OwnerTextBlock : null);
        if (ConverterKey.Length > 0 && host != null) {
            DependencyObject box = host.FindResourceBox(ConverterKey);
            if (box is IValueConverter) { return box as IValueConverter; }
        }
        return null;
    }

    private static string FormatFixedDecimal(float v, int digits) {
        if (digits <= 0) {
            int iv = (int)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
            return "" + iv;
        }
        float scale = digits == 1 ? 10.0f : 100.0f;
        int mod = digits == 1 ? 10 : 100;
        bool neg = v < 0.0f;
        int scaled = (int)(Math.Absf(v) * scale + 0.5f);
        int whole = scaled / mod;
        int frac = scaled % mod;
        string fracStr = "" + frac;
        if (digits == 2 && frac < 10) { fracStr = "0" + fracStr; }
        return (neg && (whole > 0 || frac > 0) ? "-" : "") + whole + "." + fracStr;
    }

    private static string GroupThousands(string digits) {
        if (digits == null || digits.Length <= 3) { return digits == null ? "" : digits; }
        string res = "";
        int count = 0;
        for (int i = digits.Length - 1; i >= 0; i -= 1) {
            if (count > 0 && (count % 3) == 0) {
                res = "," + res;
            }
            res = digits.Substring(i, 1) + res;
            count += 1;
        }
        return res;
    }

    private static string FormatCustomNumericSpec(DependencyObject box, string spec) {
        if (spec == null || spec.Length == 0) { return null; }
        bool hasDigitToken = false;
        int dotIdx = -1;
        for (int i = 0; i < spec.Length; i += 1) {
            string ch = spec.Substring(i, 1);
            if (ch == "0" || ch == "#") {
                hasDigitToken = true;
            } else if (ch == ".") {
                if (dotIdx >= 0) { return null; }
                dotIdx = i;
            } else if (ch != ",") {
                return null;
            }
        }
        if (!hasDigitToken) { return null; }

        string intPart = dotIdx >= 0 ? spec.Substring(0, dotIdx) : spec;
        string fracPart = dotIdx >= 0 ? spec.Substring(dotIdx + 1) : "";

        int scaleCommas = 0;
        int p = intPart.Length - 1;
        while (p >= 0 && intPart.Substring(p, 1) == ",") {
            scaleCommas += 1;
            p -= 1;
        }
        bool hasGroupSep = false;
        int minInt = 0;
        for (int i = 0; i <= p; i += 1) {
            string ch = intPart.Substring(i, 1);
            if (ch == "0") { minInt += 1; }
            else if (ch == ",") { hasGroupSep = true; }
        }

        int minFrac = 0;
        int maxFrac = 0;
        for (int i = 0; i < fracPart.Length; i += 1) {
            string ch = fracPart.Substring(i, 1);
            if (ch == "0") { minFrac += 1; maxFrac += 1; }
            else if (ch == "#") { maxFrac += 1; }
        }
        if (maxFrac > 4) { maxFrac = 4; }
        if (minFrac > maxFrac) { minFrac = maxFrac; }

        bool neg = false;
        int wholeVal = 0;
        int fracVal = 0;
        int fracPow = 1;
        for (int i = 0; i < maxFrac; i += 1) { fracPow = fracPow * 10; }

        if (box is BoxedInt) {
            int iv = (box as BoxedInt).Value;
            neg = iv < 0;
            int absVal = neg ? -iv : iv;
            int remNum = 0;
            int remDen = 1;
            if (scaleCommas == 0) {
                wholeVal = absVal;
            } else {
                int reduced = absVal;
                for (int c = 0; c < scaleCommas - 1; c += 1) {
                    reduced = reduced / 1000;
                }
                wholeVal = reduced / 1000;
                remNum = reduced % 1000;
                remDen = 1000;
            }
            if (maxFrac > 0) {
                fracVal = (remNum * fracPow + remDen / 2) / remDen;
                if (fracVal >= fracPow) {
                    wholeVal = wholeVal + 1;
                    fracVal = fracVal - fracPow;
                }
            } else if (remNum * 2 >= remDen) {
                wholeVal = wholeVal + 1;
            }
        } else {
            float fv = DependencyObject.UnboxFloat(box, 0.0f);
            neg = fv < 0.0f;
            float mag = Math.Absf(fv);
            for (int c = 0; c < scaleCommas; c += 1) {
                mag = mag / 1000.0f;
            }
            int scaled = (int)(mag * (float)fracPow + 0.5f);
            wholeVal = scaled / fracPow;
            fracVal = scaled % fracPow;
        }

        string wholeStr = "";
        if (wholeVal == 0) {
            if (minInt > 0) {
                for (int i = 0; i < minInt; i += 1) { wholeStr = wholeStr + "0"; }
            }
        } else {
            wholeStr = "" + wholeVal;
            while (wholeStr.Length < minInt) { wholeStr = "0" + wholeStr; }
            if (hasGroupSep) { wholeStr = GroupThousands(wholeStr); }
        }

        string fracStr = "";
        if (maxFrac > 0) {
            string padded = "" + fracVal;
            while (padded.Length < maxFrac) { padded = "0" + padded; }
            int keep = padded.Length;
            while (keep > minFrac && padded.Substring(keep - 1, 1) == "0") {
                keep -= 1;
            }
            fracStr = keep > 0 ? padded.Substring(0, keep) : "";
        }

        if (wholeStr.Length == 0 && fracStr.Length == 0) {
            return "0";
        }
        string signPrefix = (neg && (wholeVal > 0 || fracVal > 0)) ? "-" : "";
        return signPrefix + wholeStr + (fracStr.Length > 0 ? ("." + fracStr) : "");
    }

    private static string FormatSpecOnly(DependencyObject box, string spec) {
        string raw = DependencyObject.UnboxString(box, "");
        if (spec == null || spec.Length == 0) { return raw; }
        if (box is BoxedFloat || box is BoxedInt) {
            float fv = DependencyObject.UnboxFloat(box, 0.0f);
            if (spec == "G" || spec == "g") {
                if (box is BoxedInt) { return "" + (box as BoxedInt).Value; }
                int iv = (int)(fv >= 0.0f ? (fv + 0.0001f) : (fv - 0.0001f));
                if (Math.Absf(fv - (float)iv) < 0.0001f) { return "" + iv; }
                return raw;
            }
            if (spec == "00" || spec == "D2") {
                int iv = (box is BoxedInt) ? (box as BoxedInt).Value : (int)(fv >= 0.0f ? (fv + 0.0001f) : (fv - 0.0001f));
                if (iv >= 0 && iv < 10) { return "0" + iv; }
                return "" + iv;
            }
            if (spec == "F0" || spec == "f0" || spec == "N0" || spec == "n0" || spec == "0") {
                return FormatFixedDecimal(fv, 0);
            }
            if (spec == "F1" || spec == "f1" || spec == "0.0") {
                return FormatFixedDecimal(fv, 1);
            }
            if (spec == "F2" || spec == "f2" || spec == "0.00") {
                return FormatFixedDecimal(fv, 2);
            }
            string customNum = FormatCustomNumericSpec(box, spec);
            if (customNum != null) { return customNum; }
        }
        return raw;
    }

    private static int FindSubstr(string s, string needle, int start) {
        if (s == null || needle == null || needle.Length == 0) { return -1; }
        int limit = s.Length - needle.Length;
        for (int i = start < 0 ? 0 : start; i <= limit; i += 1) {
            if (s.Substring(i, needle.Length) == needle) { return i; }
        }
        return -1;
    }

    public string FormatValue(DependencyObject box) {
        string fmt = StringFormat == null ? "" : StringFormat;
        if (fmt.Length >= 2 && fmt.Substring(0, 2) == "{}") {
            fmt = fmt.Substring(2);
        }
        if (fmt.Length == 0) { return DependencyObject.UnboxString(box, ""); }
        if (fmt == "G" || fmt == "00" || fmt == "F0" || fmt == "F1" || fmt == "F2" ||
            fmt == "D2" || fmt == "N0" || fmt == "0.0" || fmt == "0.00") {
            return FormatSpecOnly(box, fmt);
        }
        int colIdx = FindSubstr(fmt, "{0:", 0);
        if (colIdx >= 0) {
            int endBrace = FindSubstr(fmt, "}", colIdx);
            if (endBrace > colIdx + 3) {
                string spec = fmt.Substring(colIdx + 3, endBrace - (colIdx + 3));
                string formatted = FormatSpecOnly(box, spec);
                return fmt.Substring(0, colIdx) + formatted + fmt.Substring(endBrace + 1);
            }
        }
        string raw = DependencyObject.UnboxString(box, "");
        if (fmt.Contains("{0}")) {
            return fmt.Replace("{0}", raw);
        }
        return fmt + raw;
    }

    private void ApplyTargetBox(DependencyObject valBox) {
        if (Target != null) {
            Target.SetPropertyValue(TargetProp, valBox);
        } else if (TargetInline != null) {
            TargetInline.SetPropertyValue(TargetProp, valBox);
        }
    }

    public void Evaluate() {
        if (Target == null && TargetInline == null) { return; }
        UIElement host = Target != null ? Target : (TargetInline != null ? TargetInline.OwnerTextBlock : null);
        if (Mode == 2 && OneTimeDone) { return; }

        if (Kind == 0 && LambdaGetter != null) {
            DependencyObject val = LambdaGetter();
            string h = DependencyObject.UnboxString(val, "");
            if (h != LastHash || !OneTimeDone) {
                LastHash = h;
                OneTimeDone = true;
                ApplyTargetBox(val);
            }
            return;
        }

        if (Kind == 2) {
            if (SourceElement == null && ElementName.Length > 0 && host != null) {
                SourceElement = BindingEntry.FindElementScoped(host, ElementName);
                if (SourceElement == null) {
                    UIElement curRoot = host.RootElement();
                    if (curRoot != null && curRoot != _lastFindNameRoot) {
                        _lastFindNameRoot = curRoot;
                        SourceElement = curRoot.FindName(ElementName);
                    }
                }
            }
            if (SourceElement != null) {
                DependencyObject ev = null;
                if (SourcePath == null || SourcePath.Length == 0 || SourcePath == ".") {
                    ev = DependencyObject.BoxObject(SourceElement);
                } else {
                    ev = SourceElement.GetPropertyValue(SourcePath);
                    if (ev == null) {
                        ev = BindingEngine.ReadPathBox(SourceElement, SourcePath);
                    }
                    if (ev == null && SourceElement.DataContext != null) {
                        ev = BindingEngine.ReadPathBox(SourceElement.DataContext, SourcePath);
                    }
                }
                if (ev == null && HasFallback) { ev = FallbackBox; }
                bool isDirectUi = ev != null && (ev.UnboxObject() is UIElement);
                string sig = isDirectUi ? "<ui>" : (ev != null ? DependencyObject.UnboxString(ev, "") : "<null>");
                if (!OneTimeDone || (!isDirectUi && sig != LastHash)) {
                    LastHash = sig;
                    OneTimeDone = true;
                    ApplyConverted(ev);
                }
            } else if (HasFallback && host != null && !OneTimeDone) {
                OneTimeDone = true;
                ApplyRawBox(FallbackBox);
            }
            return;
        }

        if (Kind == 3 && SourceElement != null) {
            DependencyObject tv = SourceElement.GetPropertyValue(SourcePath);
            if (tv == null) {
                tv = BindingEngine.ReadPathBox(SourceElement, SourcePath);
            }
            if (tv == null && HasFallback) { tv = FallbackBox; }
            string sig = tv != null ? DependencyObject.UnboxString(tv, "") : "<null>";
            bool boxDiff = (tv is Brush || tv is UIElement || _lastTemplateValBox is Brush || _lastTemplateValBox is UIElement)
                && (tv != _lastTemplateValBox);
            if (boxDiff || sig != LastHash || !OneTimeDone) {
                _lastTemplateValBox = tv;
                LastHash = sig;
                OneTimeDone = true;
                ApplyConverted(tv);
            }
            return;
        }

        if (Kind == 4 && MultiBindings != null) {
            int mbLen = MultiBindings.Length;
            if (_multiElementSources == null || _multiElementSources.Length < mbLen) {
                _multiElementSources = new UIElement[mbLen];
                _multiFindRoots = new UIElement[mbLen];
            }
            var vals = new DependencyObject[mbLen];
            string sig = "";
            for (int i = 0; i < mbLen; i += 1) {
                Binding sub = MultiBindings[i];
                DependencyObject sv = null;
                if (sub.ElementName.Length > 0 && host != null) {
                    UIElement se = _multiElementSources[i];
                    if (se == null) {
                        se = BindingEntry.FindElementScoped(host, sub.ElementName);
                        if (se == null) {
                            UIElement curRoot = host.RootElement();
                            if (curRoot != null && curRoot != _multiFindRoots[i]) {
                                _multiFindRoots[i] = curRoot;
                                se = curRoot.FindName(sub.ElementName);
                            }
                        }
                        if (se != null) {
                            _multiElementSources[i] = se;
                        }
                    }
                    if (se != null) {
                        sv = se.GetPropertyValue(sub.Path);
                        if (sv == null) { sv = BindingEngine.ReadPathBox(se, sub.Path); }
                        if (sv == null && se.DataContext != null) { sv = BindingEngine.ReadPathBox(se.DataContext, sub.Path); }
                    }
                } else if (sub.RelativeSourceMode == "Self" && host != null) {
                    sv = Target != null ? Target.GetPropertyValue(sub.Path) : TargetInline.GetPropertyValue(sub.Path);
                    if (sv == null) { sv = BindingEngine.ReadPathBox(Target != null ? (object)Target : (object)TargetInline, sub.Path); }
                } else if (sub.RelativeSourceMode == "TemplatedParent" && host != null && host.TemplatedParent != null) {
                    sv = host.TemplatedParent.GetPropertyValue(sub.Path);
                    if (sv == null) { sv = BindingEngine.ReadPathBox(host.TemplatedParent, sub.Path); }
                } else {
                    object dc = sub.Source != null ? sub.Source : DependencyObject.Unbox(ResolveSourceBox());
                    if (dc != null) { sv = BindingEngine.ReadPathBox(dc, sub.Path); }
                }
                if (sv == null && sub.HasFallbackValue) { sv = sub.FallbackBox; }
                vals[i] = sv;
                sig = sig + "|" + DependencyObject.UnboxString(sv, "");
            }
            if (sig == LastHash && OneTimeDone) { return; }
            LastHash = sig;
            OneTimeDone = true;
            if (MultiConverter != null) {
                object res = MultiConverter.Convert(vals, TargetProp, DependencyObject.BoxString(ConverterParam), "");
                ApplyRawBox(DependencyObject.BoxObject(res));
            } else if (StringFormat.Length > 0) {
                string txt = StringFormat;
                if (txt.Length >= 2 && txt.Substring(0, 2) == "{}") { txt = txt.Substring(2); }
                for (int i = 0; i < vals.Length; i += 1) {
                    txt = txt.Replace("{" + i + "}", DependencyObject.UnboxString(vals[i], ""));
                }
                ApplyTargetBox(DependencyObject.BoxString(txt));
            }
            return;
        }

        if (Kind == 1) {
            if (host == null && RelativeSourceMode != "Self") { return; }
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) {
                if (HasFallback) { ApplyRawBox(FallbackBox); }
                return;
            }
            int dm = ParseUtil.IsNativeReady() ? Reflect_GetDirtyMask(srcObj) : -1;
            DependencyObject valBox = BindingEngine.ReadPathBox(srcObj, SourcePath);
            if (valBox == null && HasFallback) { valBox = FallbackBox; }
            string sig = DependencyObject.UnboxString(valBox, "");
            if (dm == LastDirtyMask && dm >= 0 && sig == LastHash && OneTimeDone) { return; }
            LastDirtyMask = dm;
            if (sig != LastHash || !OneTimeDone) {
                LastHash = sig;
                OneTimeDone = true;
                ApplyConverted(valBox);
            }
            return;
        }

        DependencyObject mSrcBox = ResolveSourceBox();
        object mSrcObj = DependencyObject.Unbox(mSrcBox);
        if (mSrcObj == null) {
            if (HasFallback) { ApplyRawBox(FallbackBox); }
            return;
        }

        if (Kind == 4 && MultiPaths != null) {
            var vals = new DependencyObject[MultiPaths.Length];
            string sig = "";
            for (int i = 0; i < MultiPaths.Length; i += 1) {
                vals[i] = BindingEngine.ReadPathBox(mSrcObj, MultiPaths[i]);
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
                if (txt.Length >= 2 && txt.Substring(0, 2) == "{}") { txt = txt.Substring(2); }
                for (int i = 0; i < vals.Length; i += 1) {
                    txt = txt.Replace("{" + i + "}", DependencyObject.UnboxString(vals[i], ""));
                }
                ApplyTargetBox(DependencyObject.BoxString(txt));
            }
            return;
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
        if (valBox == null && HasTargetNull) { valBox = TargetNullBox; }
        else if (HasTargetNull && valBox is BoxedString && (valBox as BoxedString).Value.Length == 0) {
            valBox = TargetNullBox;
        }
        if (StringFormat.Length > 0) {
            ApplyTargetBox(DependencyObject.BoxString(FormatValue(valBox)));
        } else {
            ApplyTargetBox(valBox);
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
        if (Kind == 3 && SourceElement != null) {
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

internal class ElementBindingList : DependencyObject {
    public BindingEntry[] Items;
    public ElementBindingList() { Items = new BindingEntry[0]; }
}

public static class BindingEngine {
    private static BindingEntry[] _entries;
    private static Action[] _preFlushHooks;

    public static void SetPreFlushHook(Action hook) {
        _preFlushHooks = new Action[0];
        if (hook != null) { _preFlushHooks.Push(hook); }
    }

    public static void AddPreFlushHook(Action hook) {
        if (hook == null) { return; }
        if (_preFlushHooks == null) { _preFlushHooks = new Action[0]; }
        _preFlushHooks.Push(hook);
    }

    private static void EnsureInit() {
        if (_entries == null) { _entries = new BindingEntry[0]; }
    }

    private static void RegisterEntry(BindingEntry entry) {
        EnsureInit();
        _entries.Push(entry);
        if (entry != null && entry.Target != null) {
            ElementBindingList list = null;
            if (entry.Target._ownBindingsBox is ElementBindingList) {
                list = entry.Target._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.Target._ownBindingsBox = list;
            }
            list.Items.Push(entry);
        }
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
        UIElement target, string targetProp, Control templatedParent, string sourceProp, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = mode;
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

    public static void BindPathEx(
        UIElement target, string targetProp, object source, string path,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null,
        string relativeMode = "", string ancestorType = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 1;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.SourcePath = path == null ? "" : path;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        e.RelativeSourceMode = relativeMode == null ? "" : relativeMode;
        e.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        RegisterEntry(e);
    }

    public static void BindElementEx(
        UIElement target, string targetProp, string elementName, string sourceProp,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 2;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntry(e);
    }

    public static void BindInlineEx(
        Inline target, string targetProp, string path, string elementName,
        int mode, IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null,
        string relativeMode = "", string ancestorType = "") {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.TargetInline = target;
        e.TargetProp = targetProp;
        e.Kind = (elementName != null && elementName.Length > 0) ? 2 : 1;
        e.Mode = mode;
        e.ElementName = elementName == null ? "" : elementName;
        e.SourcePath = path == null ? "" : path;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        e.RelativeSourceMode = relativeMode == null ? "" : relativeMode;
        e.RelativeSourceAncestorType = ancestorType == null ? "" : ancestorType;
        RegisterEntry(e);
    }

    public static void BindTemplateParentEx(
        UIElement target, string targetProp, Control templatedParent, string sourceProp,
        IValueConverter converter, string converterKey, string converterParam, string stringFormat,
        bool hasFallback = false, DependencyObject fallbackBox = null,
        bool hasTargetNull = false, DependencyObject targetNullBox = null, int mode = 0) {
        if (target == null || targetProp == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 3;
        e.Mode = mode;
        e.SourceElement = templatedParent;
        e.SourcePath = sourceProp == null ? "" : sourceProp;
        e.Converter = converter;
        e.ConverterKey = converterKey == null ? "" : converterKey;
        e.ConverterParam = converterParam == null ? "" : converterParam;
        e.StringFormat = stringFormat == null ? "" : stringFormat;
        e.HasFallback = hasFallback;
        e.FallbackBox = fallbackBox;
        e.HasTargetNull = hasTargetNull;
        e.TargetNullBox = targetNullBox;
        RegisterEntry(e);
    }

    public static void BindMultiBinding(UIElement target, string targetProp, MultiBinding mb) {
        if (target == null || targetProp == null || mb == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 4;
        e.Mode = 0;
        e.MultiBindings = mb.Bindings;
        e.MultiConverter = mb.Converter;
        e.ConverterParam = mb.ConverterParameter == null ? "" : mb.ConverterParameter;
        e.StringFormat = mb.StringFormat == null ? "" : mb.StringFormat;
        RegisterEntry(e);
    }

    public static void NotifyTargetPropertyChanged(UIElement target, string targetProp, DependencyObject newBox) {
        if (target == null || !(target._ownBindingsBox is ElementBindingList)) { return; }
        BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
        for (int i = 0; i < own.Length; i += 1) {
            BindingEntry e = own[i];
            if (e.TargetProp == targetProp && e.Mode == 1) {
                e.PushTwoWay(newBox);
            }
        }
    }

    public static void UnbindTarget(UIElement target) {
        if (target == null) { return; }
        target._ownBindingsBox = null;
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            if (_entries[i].Target == target) {
                _entries.RemoveAt(i);
            }
        }
    }

    public static void FlushSubtree(UIElement subtreeRoot) {
        if (subtreeRoot == null || subtreeRoot._isDisposedTree) { return; }
        var q = new UIElement[0];
        q.Push(subtreeRoot);
        int qi = 0;
        while (qi < q.Length) {
            UIElement node = q[qi];
            qi += 1;
            if (node == null || node._isDisposedTree) { continue; }
            if (node._ownBindingsBox is ElementBindingList) {
                BindingEntry[] own = (node._ownBindingsBox as ElementBindingList).Items;
                for (int k = 0; k < own.Length; k += 1) {
                    own[k].Evaluate();
                }
            }
            int cc = node.ChildCount();
            for (int c = 0; c < cc; c += 1) {
                UIElement ch = node.ChildAt(c);
                if (ch != null && !ch._isDisposedTree) {
                    q.Push(ch);
                }
            }
        }
    }

    public static void Flush() {
        if (_preFlushHooks != null) {
            for (int h = 0; h < _preFlushHooks.Length; h += 1) {
                var fn = _preFlushHooks[h];
                if (fn != null) { fn(); }
            }
        }
        if (_entries == null) { return; }
        int write = 0;
        for (int i = 0; i < _entries.Length; i += 1) {
            BindingEntry e = _entries[i];
            if (e.Target != null && e.Target._isDisposedTree) { continue; }
            if (write != i) { _entries[write] = e; }
            write += 1;
        }
        while (_entries.Length > write) {
            _entries.RemoveAt(_entries.Length - 1);
        }
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
        if (root is UIElement) {
            DependencyObject direct = (root as UIElement).GetPropertyValue(path);
            if (direct != null) { return direct; }
            if (path.Length > 2 && path.Substring(0, 1) == "(" && path.Substring(path.Length - 1, 1) == ")") {
                return null;
            }
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
        int kind = Reflect_GetPropTypeKind(cur, leaf);
        if (kind == 1) { return DependencyObject.BoxBool(Reflect_GetPropBool(cur, leaf)); }
        if (kind == 2) { return DependencyObject.BoxInt(Reflect_GetPropInt(cur, leaf)); }
        if (kind == 3) { return DependencyObject.BoxFloat(Reflect_GetPropFloat(cur, leaf)); }
        if (kind == 4) { return DependencyObject.BoxString(Reflect_GetPropString(cur, leaf)); }
        if (kind == 5) {
            object objVal = Reflect_GetPropObject(cur, leaf);
            return objVal != null ? DependencyObject.BoxObject(objVal) : null;
        }
        return null;
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
