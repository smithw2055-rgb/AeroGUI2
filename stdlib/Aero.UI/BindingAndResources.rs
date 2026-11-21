module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 1. ResourceDictionary
// ============================================================================

public class ResourceDictionary : DependencyObject {
    private static ResourceDictionary _appResources;
    private static Dictionary<string, ResourceDictionary> _locSources;
    private static string[] _locSourceKeys;
    private static ResourceDictionary[] _locSourceDicts;
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

    private static void RecordLocPair(string key, ResourceDictionary dict) {
        if (key == null || key.Length == 0 || dict == null) { return; }
        if (_locSourceKeys == null) {
            _locSourceKeys = new string[0];
            _locSourceDicts = new ResourceDictionary[0];
        }
        for (int i = 0; i < _locSourceKeys.Length; i += 1) {
            if (_locSourceKeys[i] == key) {
                _locSourceDicts[i] = dict;
                return;
            }
        }
        _locSourceKeys.Push(key);
        _locSourceDicts.Push(dict);
    }

    public static void RegisterLocSource(string sourceName, ResourceDictionary dict) {
        if (sourceName == null || dict == null) { return; }
        if (_locSources == null) { _locSources = new Dictionary<string, ResourceDictionary>(); }
        string k = NormalizeLocKey(sourceName);
        if (k.Length == 0) { return; }
        _locSources[k] = dict;
        RecordLocPair(k, dict);
        string baseName = LocBaseName(k);
        if (baseName.Length > 0) {
            _locSources[baseName] = dict;
            RecordLocPair(baseName, dict);
        }
    }

    public static ResourceDictionary GetLocSource(string sourceName) {
        if (sourceName == null) { return null; }
        string k = NormalizeLocKey(sourceName);
        if (k.Length == 0) { return null; }
        if (_locSources != null && _locSources.ContainsKey(k)) { return _locSources[k]; }
        string baseName = LocBaseName(k);
        if (_locSources != null && baseName.Length > 0 && _locSources.ContainsKey(baseName)) {
            return _locSources[baseName];
        }
        if (_locSourceKeys != null) {
            for (int i = 0; i < _locSourceKeys.Length; i += 1) {
                if (_locSourceKeys[i] == k || (baseName.Length > 0 && _locSourceKeys[i] == baseName)) {
                    return _locSourceDicts[i];
                }
            }
        }
        return null;
    }

    public ResourceDictionary() {
        _map = new Dictionary<string, DependencyObject>();
        _keyIndex = new Dictionary<string, int>();
        _keys = new string[0];
        _vals = new DependencyObject[0];
    }

    private Dictionary<string, int> _keyIndex;
    private DependencyObject[] _vals;

    private int FindFallbackKeyIndex(string key) {
        if (key == null || _keyIndex == null) { return -1; }
        return _keyIndex.GetOrDefault(key, -1);
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
        int fbIdx = _keyIndex.GetOrDefault(key, -1);
        if (fbIdx < 0) {
            _keyIndex[key] = _keys.Length;
            _keys.Push(key);
            _vals.Push(box);
        } else {
            _vals[fbIdx] = box;
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

    public DependencyObject TryGetLocalBox(string key) {
        if (key == null || _keys.Length == 0) { return null; }
        if (_map.ContainsKey(key)) { return _map[key]; }
        int fbIdx = _keyIndex.GetOrDefault(key, -1);
        if (fbIdx >= 0 && fbIdx < _vals.Length) { return _vals[fbIdx]; }
        return null;
    }

    public DependencyObject GetBox(string key) {
        if (key == null) { return null; }
        DependencyObject local = TryGetLocalBox(key);
        if (local != null) { return local; }
        ResourceDictionary appRes = GlobalApplicationResources();
        if (this != appRes && appRes != null) {
            return appRes.TryGetLocalBox(key);
        }
        return null;
    }

    public object Get(string key) {
        return DependencyObject.Unbox(GetBox(key));
    }

    public bool Contains(string key) {
        if (key == null || _keys.Length == 0) { return false; }
        if (_map.ContainsKey(key)) { return true; }
        return _keyIndex.ContainsKey(key);
    }

    public bool Remove(string key) {
        if (key == null || !Contains(key)) { return false; }
        _map.Remove(key);
        int idx = _keyIndex.GetOrDefault(key, -1);
        _keyIndex.Remove(key);
        if (idx >= 0) {
            _keys.RemoveAt(idx);
            _vals.RemoveAt(idx);
            for (int i = idx; i < _keys.Length; i += 1) {
                _keyIndex[_keys[i]] = i;
            }
        }
        return true;
    }

    public void Clear() {
        _map.Clear();
        _keyIndex.Clear();
        _keys.Clear();
        _vals.Clear();
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
    public string Property = "";
    public string TargetName = "";
    public string DynamicResourceKey = "";
    public string BindingPath = "";
    public string BindingElementName = "";
    public string BindingRelativeMode = "";
    public string BindingAncestorType = "";
    public IValueConverter BindingConverter;
    public string BindingConverterKey = "";
    public string BindingConverterParam = "";
    public string BindingStringFormat = "";
    public int BindingMode = 0;
    public bool BindingHasFallback = false;
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

    public static Setter FromDynamicResource(string property, string resourceKey, string targetName = "") {
        var s = new Setter();
        s.Property = property == null ? "" : property;
        s.DynamicResourceKey = resourceKey == null ? "" : resourceKey;
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
        return TargetName.Length > 0 ? Trigger.ResolveTemplateOrNamedElement(rootTarget, TargetName) : rootTarget;
    }

    public void ApplyTo(UIElement rootTarget) {
        UIElement dest = ResolveTarget(rootTarget);
        if (dest != null) {
            if (DynamicResourceKey.Length > 0) {
                dest.SetResourceReference(Property, DynamicResourceKey);
            } else if (BindingPath.Length > 0) {
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
    private string[] _enterActionKeys;
    private string[] _exitActionKeys;
    private UIElement[] _activeTargets;
    private UIElement[] _savedOwners;
    private UIElement[] _savedDests;
    private string[] _savedProps;

    private static UIElement[] _baseDests;
    private static string[] _baseProps;
    private static DependencyObject[] _baseBoxes;
    private static string[] _baseDynKeys;
    private static int[] _baseRefs;
    private static TriggerBase[] _triggersWithTargets;
    private static bool _disposeHookInstalled = false;

    internal static void EnsureDisposeHook() {
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { TriggerBase.CleanupDisposed(); });
        }
    }

    public TriggerBase() {
        _setters = new Setter[0];
        _enterActions = new Storyboard[0];
        _exitActions = new Storyboard[0];
        _enterActionKeys = new string[0];
        _exitActionKeys = new string[0];
        _activeTargets = new UIElement[0];
        _savedOwners = new UIElement[0];
        _savedDests = new UIElement[0];
        _savedProps = new string[0];
    }

    internal virtual bool HasLiveHookedTargets() { return false; }
    internal virtual void CleanupHookedTargets() {}

    protected static void RegisterTriggerWithTargets(TriggerBase trig) {
        if (trig == null) { return; }
        EnsureDisposeHook();
        if (_triggersWithTargets == null) { _triggersWithTargets = new TriggerBase[0]; }
        for (int i = 0; i < _triggersWithTargets.Length; i += 1) {
            if (_triggersWithTargets[i] == trig) { return; }
        }
        _triggersWithTargets.Push(trig);
    }

    public static void CleanupDisposed() {
        if (_baseDests != null) {
            for (int i = _baseDests.Length - 1; i >= 0; i -= 1) {
                if (_baseDests[i] == null || _baseDests[i]._isDisposedTree) {
                    _baseDests.RemoveAt(i);
                    _baseProps.RemoveAt(i);
                    _baseBoxes.RemoveAt(i);
                    _baseDynKeys.RemoveAt(i);
                    _baseRefs.RemoveAt(i);
                }
            }
        }
        if (_triggersWithTargets != null) {
            for (int i = _triggersWithTargets.Length - 1; i >= 0; i -= 1) {
                TriggerBase tr = _triggersWithTargets[i];
                if (tr == null) {
                    _triggersWithTargets.RemoveAt(i);
                    continue;
                }
                for (int j = tr._activeTargets.Length - 1; j >= 0; j -= 1) {
                    if (tr._activeTargets[j] == null || tr._activeTargets[j]._isDisposedTree) {
                        tr._activeTargets.RemoveAt(j);
                    }
                }
                for (int j = tr._savedOwners.Length - 1; j >= 0; j -= 1) {
                    if (tr._savedOwners[j] == null || tr._savedOwners[j]._isDisposedTree ||
                        tr._savedDests[j] == null || tr._savedDests[j]._isDisposedTree) {
                        tr._savedOwners.RemoveAt(j);
                        tr._savedDests.RemoveAt(j);
                        tr._savedProps.RemoveAt(j);
                    }
                }
                tr.CleanupHookedTargets();
                if (tr._activeTargets.Length == 0 && tr._savedOwners.Length == 0 && !tr.HasLiveHookedTargets()) {
                    _triggersWithTargets.RemoveAt(i);
                }
            }
        }
    }

    private static void EnsureBaseTables() {
        EnsureDisposeHook();
        if (_baseDests == null) {
            _baseDests = new UIElement[0];
            _baseProps = new string[0];
            _baseBoxes = new DependencyObject[0];
            _baseDynKeys = new string[0];
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
        _baseDynKeys.Push(dest.GetResourceReferenceKey(prop));
        _baseRefs.Push(1);
    }

    private static void ReleaseBaseValue(UIElement dest, string prop) {
        if (dest == null || prop == null || prop.Length == 0 || _baseDests == null) { return; }
        for (int i = _baseDests.Length - 1; i >= 0; i -= 1) {
            if (_baseDests[i] == dest && _baseProps[i] == prop) {
                DependencyObject baseBox = _baseBoxes[i];
                string baseDynKey = _baseDynKeys[i];
                int nextRef = _baseRefs[i] - 1;
                if (nextRef <= 0) {
                    _baseDests.RemoveAt(i);
                    _baseProps.RemoveAt(i);
                    _baseBoxes.RemoveAt(i);
                    _baseDynKeys.RemoveAt(i);
                    _baseRefs.RemoveAt(i);
                } else {
                    _baseRefs[i] = nextRef;
                }
                if (baseDynKey != null && baseDynKey.Length > 0) {
                    dest.SetResourceReference(prop, baseDynKey);
                } else {
                    dest.ClearResourceReference(prop);
                    dest.SetPropertyValue(prop, baseBox);
                }
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
    public void AddEnterActionKey(string key) { if (key != null && key.Length > 0) { _enterActionKeys.Push(key); } }
    public void AddExitActionKey(string key) { if (key != null && key.Length > 0) { _exitActionKeys.Push(key); } }

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
        if (active) {
            _activeTargets.Push(target);
            RegisterTriggerWithTargets(this);
        }
    }

    internal void DeactivateTarget(UIElement target, Style owningStyle, bool runExitActions) {
        if (target == null || !WasActiveFor(target)) { return; }
        SetActiveFor(target, false);
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
        if (runExitActions) {
            for (int i = 0; i < _exitActions.Length; i += 1) {
                _exitActions[i].Begin(target);
            }
            if (_exitActionKeys != null) {
                for (int i = 0; i < _exitActionKeys.Length; i += 1) {
                    target.BeginStoryboard(_exitActionKeys[i]);
                }
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
        bool savePrev = !wasActive;
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
            RunEnterActions(target);
        }
    }

    protected void RunEnterActions(UIElement target) {
        if (target == null) { return; }
        for (int i = 0; i < _enterActions.Length; i += 1) {
            _enterActions[i].Begin(target);
        }
        if (_enterActionKeys != null) {
            for (int i = 0; i < _enterActionKeys.Length; i += 1) {
                target.BeginStoryboard(_enterActionKeys[i]);
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

    private UIElement[] _hookedTargets;
    private UIElement[] _hookedSources;

    internal static UIElement ResolveTemplateOrNamedElement(UIElement target, string name) {
        if (target == null || name == null || name.Length == 0) { return null; }
        if (target is Control && (target as Control).TemplateRoot() != null) {
            UIElement tc = (target as Control).GetTemplateChild(name);
            if (tc != null) { return tc; }
        }
        UIElement se = BindingEntry.FindElementScoped(target, name);
        if (se != null) { return se; }
        se = target.FindName(name);
        if (se != null) { return se; }
        UIElement root = target.RootElement();
        if (root != null) { return root.FindName(name); }
        return null;
    }

    internal override bool HasLiveHookedTargets() {
        return _hookedTargets != null && _hookedTargets.Length > 0;
    }

    internal override void CleanupHookedTargets() {
        if (_hookedTargets == null) { return; }
        for (int i = _hookedTargets.Length - 1; i >= 0; i -= 1) {
            if (_hookedTargets[i] == null || _hookedTargets[i]._isDisposedTree ||
                _hookedSources[i] == null || _hookedSources[i]._isDisposedTree) {
                _hookedTargets.RemoveAt(i);
                _hookedSources.RemoveAt(i);
            }
        }
    }

    private void HookSourceIfNeeded(UIElement target) {
        if (target == null || target._isDisposedTree || SourceName.Length == 0) { return; }
        UIElement src = ResolveTemplateOrNamedElement(target, SourceName);
        if (src == null || src == target || src._isDisposedTree) { return; }
        if (_hookedTargets == null) {
            _hookedTargets = new UIElement[0];
            _hookedSources = new UIElement[0];
        }
        for (int i = 0; i < _hookedTargets.Length; i += 1) {
            if (_hookedTargets[i] == target && _hookedSources[i] == src) { return; }
        }
        RegisterTriggerWithTargets(this);
        _hookedTargets.Push(target);
        _hookedSources.Push(src);
        src.AddStateInvalidatedHook(() => {
            if (!target._isDisposedTree) { target.RefreshTriggersAndStates(); }
        });
    }

    public override void Attach(UIElement target) {
        if (target == null || SourceName.Length == 0) { return; }
        HookSourceIfNeeded(target);
        target.Loaded += (object s, RoutedEventArgs e) => {
            HookSourceIfNeeded(target);
        };
    }

    public override bool IsMatched(UIElement target) {
        if (target == null || Property.Length == 0) { return false; }
        UIElement src = SourceName.Length > 0 ? ResolveTemplateOrNamedElement(target, SourceName) : target;
        if (src == null) { return false; }
        DependencyObject actual = src.GetPropertyValue(Property);
        return BoxEqualsForProperty(Property, actual, _valueBox);
    }

    public static int ResolveKnownEnumOrdinal(string propName, string rawVal) {
        if (propName == null || rawVal == null) { return -1; }
        string pn = propName.Trim();
        if (pn.Length > 2 && pn.Substring(0, 1) == "(" && pn.Substring(pn.Length - 1, 1) == ")") {
            pn = pn.Substring(1, pn.Length - 2).Trim();
        }
        int dot = pn.LastIndexOf(".");
        if (dot >= 0 && dot + 1 < pn.Length) { pn = pn.Substring(dot + 1); }

        string val = rawVal.Trim();
        int vdot = val.LastIndexOf(".");
        if (vdot >= 0 && vdot + 1 < val.Length) { val = val.Substring(vdot + 1).Trim(); }
        if (val.Length == 0) { return -1; }

        if (pn == "Visibility") {
            if (val == "Visible") { return 0; }
            if (val == "Hidden") { return 1; }
            if (val == "Collapsed") { return 2; }
            return -1;
        }
        if (pn == "Orientation") {
            if (val == "Horizontal") { return 0; }
            if (val == "Vertical") { return 1; }
            return -1;
        }
        if (pn == "HorizontalAlignment" || pn == "HorizontalContentAlignment") {
            if (val == "Stretch") { return 0; }
            if (val == "Left") { return 1; }
            if (val == "Center") { return 2; }
            if (val == "Right") { return 3; }
            return -1;
        }
        if (pn == "VerticalAlignment" || pn == "VerticalContentAlignment") {
            if (val == "Stretch") { return 0; }
            if (val == "Top") { return 1; }
            if (val == "Center") { return 2; }
            if (val == "Bottom") { return 3; }
            return -1;
        }
        if (pn == "ExpandDirection") {
            if (val == "Down") { return 0; }
            if (val == "Up") { return 1; }
            if (val == "Left") { return 2; }
            if (val == "Right") { return 3; }
            return -1;
        }
        if (pn == "Stretch") {
            if (val == "None") { return 0; }
            if (val == "Fill") { return 1; }
            if (val == "Uniform") { return 2; }
            if (val == "UniformToFill") { return 3; }
            return -1;
        }
        if (pn == "StretchDirection") {
            if (val == "UpOnly") { return 0; }
            if (val == "DownOnly") { return 1; }
            if (val == "Both") { return 2; }
            return -1;
        }
        if (pn == "Dock") {
            if (val == "Left") { return 0; }
            if (val == "Top") { return 1; }
            if (val == "Right") { return 2; }
            if (val == "Bottom") { return 3; }
            return -1;
        }
        if (pn == "TextWrapping") {
            if (val == "NoWrap") { return 0; }
            if (val == "Wrap") { return 1; }
            if (val == "WrapWithOverflow") { return 2; }
            return -1;
        }
        if (pn == "TextAlignment") {
            if (val == "Left") { return 0; }
            if (val == "Center") { return 1; }
            if (val == "Right") { return 2; }
            if (val == "Justify") { return 3; }
            return -1;
        }
        if (pn == "TextTrimming") {
            if (val == "None") { return 0; }
            if (val == "CharacterEllipsis") { return 1; }
            if (val == "WordEllipsis") { return 2; }
            return -1;
        }
        if (pn == "SelectionMode") {
            if (val == "Single") { return 0; }
            if (val == "Multiple") { return 1; }
            if (val == "Extended") { return 2; }
            return -1;
        }
        if (pn == "ScrollBarVisibility" || pn == "HorizontalScrollBarVisibility" || pn == "VerticalScrollBarVisibility" ||
            pn == "ComputedHorizontalScrollBarVisibility" || pn == "ComputedVerticalScrollBarVisibility") {
            if (val == "Disabled") { return 0; }
            if (val == "Auto") { return 1; }
            if (val == "Hidden") { return 2; }
            if (val == "Visible") { return 3; }
            return -1;
        }
        return -1;
    }

    public static bool BoxEqualsForProperty(string propName, DependencyObject a, DependencyObject b) {
        if (BoxEquals(a, b)) { return true; }
        if (a == null || b == null || propName == null || propName.Length == 0) { return false; }
        int oa = -1;
        if (a is BoxedInt) { oa = (a as BoxedInt).Value; }
        else if (a is BoxedString) { oa = ResolveKnownEnumOrdinal(propName, (a as BoxedString).Value); }
        if (oa < 0) { return false; }
        int ob = -1;
        if (b is BoxedInt) { ob = (b as BoxedInt).Value; }
        else if (b is BoxedString) { ob = ResolveKnownEnumOrdinal(propName, (b as BoxedString).Value); }
        return ob >= 0 && oa == ob;
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
    private static DataTrigger[] _reactiveTriggers;
    private static UIElement[] _reactiveTargets;
    private static bool _reactiveHookInstalled = false;
    public string BindingPath { get; set; } = "";
    public string ElementName { get; set; } = "";
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

    internal DependencyObject _multiBindingBox;
    internal DependencyObject _dynamicBindingBox;
    internal static Func<UIElement, DependencyObject, DependencyObject> _evalMultiHook;
    internal static Func<UIElement, DependencyObject, DependencyObject> _evalSingleHook;

    public void SetMultiBinding(MultiBinding mb) {
        _multiBindingBox = mb;
        if (_evalMultiHook == null) {
            _evalMultiHook = (UIElement t, DependencyObject b) => { return MultiBinding.EvaluateBox(t, b); };
        }
    }
    public void SetDynamicBinding(Binding b) {
        _dynamicBindingBox = b;
        if (_evalSingleHook == null) {
            _evalSingleHook = (UIElement t, DependencyObject bx) => { return BindingEntry.EvaluateStandaloneBinding(t, bx as Binding); };
        }
    }

    public object Value {
        get { return DependencyObject.Unbox(_valueBox); }
        set { _valueBox = DependencyObject.BoxObject(value); }
    }

    public override void Attach(UIElement target) {
        if (target == null) { return; }
        if (_multiBindingBox != null || _dynamicBindingBox != null || ElementName.Length > 0 ||
            RelativeSourceMode.Length == 0 || RelativeSourceMode == "DataContext") {
            RegisterReactive(this, target);
        }

    }

    private static void RegisterReactive(DataTrigger dt, UIElement target) {
        if (dt == null || target == null) { return; }
        if (_reactiveTriggers == null) {
            _reactiveTriggers = new DataTrigger[0];
            _reactiveTargets = new UIElement[0];
        }
        if (!_reactiveHookInstalled) {
            _reactiveHookInstalled = true;
            BindingEngine.AddPreFlushHook(() => { DataTrigger.FlushReactive(); });
            UIElement.RegisterDisposeHook(() => { DataTrigger.CleanupDisposedReactive(); });
        }
        for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
            if (_reactiveTriggers[i] == dt && _reactiveTargets[i] == target) { return; }
        }
        _reactiveTriggers.Push(dt);
        _reactiveTargets.Push(target);
    }

    internal static void CleanupDisposedReactive() {
        if (_reactiveTriggers == null || _reactiveTargets == null) { return; }
        for (int i = _reactiveTargets.Length - 1; i >= 0; i -= 1) {
            if (_reactiveTargets[i] == null || _reactiveTargets[i]._isDisposedTree) {
                _reactiveTriggers.RemoveAt(i);
                _reactiveTargets.RemoveAt(i);
            }
        }
    }

    public static void FlushReactive() {
        if (_reactiveTriggers == null || _reactiveTargets == null) { return; }
        int write = 0;
        for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
            UIElement t = _reactiveTargets[i];
            if (t == null || t._isDisposedTree) { continue; }
            if (write != i) {
                _reactiveTriggers[write] = _reactiveTriggers[i];
                _reactiveTargets[write] = t;
            }
            write += 1;
        }
        while (_reactiveTriggers.Length > write) {
            _reactiveTriggers.RemoveAt(_reactiveTriggers.Length - 1);
            _reactiveTargets.RemoveAt(_reactiveTargets.Length - 1);
        }
        for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
            _reactiveTriggers[i].DeactivateIfUnmatched(_reactiveTargets[i], null);
        }
        for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
            _reactiveTriggers[i].ApplyIfMatched(_reactiveTargets[i], null);
        }
    }

    public override bool IsMatched(UIElement target) {
        if (target == null) { return false; }
        if (_multiBindingBox != null && _evalMultiHook != null) {
            var fnM = _evalMultiHook;
            return Trigger.BoxEqualsForProperty(BindingPath, fnM(target, _multiBindingBox), _valueBox);
        }
        if (_dynamicBindingBox != null && _evalSingleHook != null) {
            var fnS = _evalSingleHook;
            return Trigger.BoxEqualsForProperty(BindingPath, fnS(target, _dynamicBindingBox), _valueBox);
        }
        if (BindingPath.Length == 0) { return false; }
        DependencyObject actual = null;
        if (ElementName.Length > 0) {
            UIElement se = Trigger.ResolveTemplateOrNamedElement(target, ElementName);
            if (se != null) {
                actual = se.GetPropertyValue(BindingPath);
                if (actual == null) { actual = BindingEngine.ReadPathBox(se, BindingPath); }
            }
        } else if (RelativeSourceMode == "Self") {
            actual = target.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(target, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
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
        return Trigger.BoxEqualsForProperty(BindingPath, actual, _valueBox);
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
        return Trigger.ResolveTemplateOrNamedElement(target, ElementName);
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
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
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
        return Trigger.BoxEqualsForProperty(BindingPath, actual, ValueBox);
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

    public TriggerCondition[] Conditions() { return _conditions; }

    private bool HasHooked(UIElement target, UIElement src) {
        for (int i = 0; i < _hookedTargets.Length; i += 1) {
            if (_hookedTargets[i] == target && _hookedSources[i] == src) { return true; }
        }
        return false;
    }

    internal override bool HasLiveHookedTargets() {
        return _hookedTargets != null && _hookedTargets.Length > 0;
    }

    internal override void CleanupHookedTargets() {
        if (_hookedTargets == null) { return; }
        for (int i = _hookedTargets.Length - 1; i >= 0; i -= 1) {
            if (_hookedTargets[i] == null || _hookedTargets[i]._isDisposedTree ||
                _hookedSources[i] == null || _hookedSources[i]._isDisposedTree) {
                _hookedTargets.RemoveAt(i);
                _hookedSources.RemoveAt(i);
            }
        }
    }

    private void HookElementSources(UIElement target) {
        if (target == null || target._isDisposedTree) { return; }
        for (int i = 0; i < _conditions.Length; i += 1) {
            TriggerCondition cond = _conditions[i];
            if (cond.ElementName.Length == 0) { continue; }
            UIElement se = cond.ResolveElementSource(target);
            if (se == null || se._isDisposedTree || HasHooked(target, se)) { continue; }
            RegisterTriggerWithTargets(this);
            _hookedTargets.Push(target);
            _hookedSources.Push(se);
            se.AddStateInvalidatedHook(() => {
                if (!target._isDisposedTree) { Evaluate(target, null); }
            });
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

internal class EventTriggerSink : BehaviorEventSink {
    private EventTrigger _trig;
    private UIElement _owner;
    public EventTriggerSink(EventTrigger trig, UIElement owner) {
        _trig = trig;
        _owner = owner;
    }
    public override void OnBehaviorEvent(object sender, RoutedEventArgs e) {
        if (_trig != null && _owner != null) { _trig.RunActions(_owner); }
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
        UIElement src = SourceName.Length > 0 ? Trigger.ResolveTemplateOrNamedElement(target, SourceName) : target;
        if (src == null) { src = target; }
        string ev = RoutedEvent;
        if (ev == "Loaded" || ev == "FrameworkElement.Loaded") {
            src.Loaded += (object s, RoutedEventArgs e) => { RunActions(target); };
            if (src.HasLoadedFired()) { RunActions(target); }
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
        } else {
            string shortEv = ev;
            if (ev == "Button.Click" || ev == "ButtonBase.Click") { shortEv = "Click"; }
            else if (ev == "ToggleButton.Checked") { shortEv = "Checked"; }
            else if (ev == "ToggleButton.Unchecked") { shortEv = "Unchecked"; }
            else if (ev == "ToggleButton.Indeterminate") { shortEv = "Indeterminate"; }
            else if (ev == "TreeView.SelectedItemChanged") { shortEv = "SelectedItemChanged"; }
            else if (ev == "Selector.SelectionChanged") { shortEv = "SelectionChanged"; }
            src.SubscribeBehaviorRoutedEvent(shortEv, new EventTriggerSink(this, target));
        }
    }

    public void AddAction(Storyboard sb) { AddEnterAction(sb); }
    public void AddActionKey(string key) { AddEnterActionKey(key); }

    public void RunActions(UIElement target) {
        if (_storyboard != null) { _storyboard.Begin(target); }
        RunEnterActions(target);
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
        ResourceDictionary.PopAmbient(Resources);
        ResourceDictionary.PopAmbient(AmbientResources);
        return root;
    }

    public void AttachTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        for (int i = 0; i < _triggers.Length; i += 1) {
            _triggers[i].Attach(owner);
        }
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

    public static void HookTemplateAncestors(TriggerBase[] list, UIElement root, UIElement[] hookedAncestors) {
        if (list == null || root == null || hookedAncestors == null) { return; }
        for (int i = 0; i < list.Length; i += 1) {
            if (list[i] is DataTrigger) {
                DataTrigger dt = list[i] as DataTrigger;
                if (dt.RelativeSourceAncestorType.Length > 0) {
                    UIElement anc = root;
                    while (anc != null && !BindingEntry.MatchesAncestorType(anc, dt.RelativeSourceAncestorType)) {
                        anc = BindingEntry.StepAncestor(anc);
                    }
                    if (anc != null) {
                        bool already = false;
                        for (int h = 0; h < hookedAncestors.Length; h += 1) {
                            if (hookedAncestors[h] == anc) { already = true; break; }
                        }
                        if (!already) {
                            hookedAncestors.Push(anc);
                            anc.AddStateInvalidatedHook(() => { TriggerBase.EvaluateGroup(list, root, null); });
                        }
                    }
                }
            } else if (list[i] is MultiDataTrigger) {
                MultiDataTrigger mdt = list[i] as MultiDataTrigger;
                TriggerCondition[] conds = mdt.Conditions();
                for (int ci = 0; ci < conds.Length; ci += 1) {
                    TriggerCondition cond = conds[ci];
                    if (cond.RelativeSourceAncestorType.Length > 0) {
                        UIElement anc = root;
                        while (anc != null && !BindingEntry.MatchesAncestorType(anc, cond.RelativeSourceAncestorType)) {
                            anc = BindingEntry.StepAncestor(anc);
                        }
                        if (anc != null) {
                            bool already = false;
                            for (int h = 0; h < hookedAncestors.Length; h += 1) {
                                if (hookedAncestors[h] == anc) { already = true; break; }
                            }
                            if (!already) {
                                hookedAncestors.Push(anc);
                                anc.AddStateInvalidatedHook(() => { TriggerBase.EvaluateGroup(list, root, null); });
                            }
                        }
                    }
                }
            }
        }
    }

    private void AttachTriggers(UIElement root) {
        TriggerBase[] list = _triggers;
        for (int i = 0; i < list.Length; i += 1) {
            list[i].Attach(root);
        }
        var hookedAncestors = new UIElement[0];
        DataTemplate.HookTemplateAncestors(list, root, hookedAncestors);
        TriggerBase.EvaluateGroup(list, root, null);
        root.Loaded += (object s, RoutedEventArgs e) => {
            DataTemplate.HookTemplateAncestors(list, root, hookedAncestors);
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
    public string ItemTemplateKey { get; set; } = "";

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
    private bool _storyboardSettersExtracted = false;
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

    internal void EnsureResolvedStoryboard(UIElement target) {
        if (_storyboard == null && StoryboardKey.Length > 0 && target != null) {
            DependencyObject box = target.FindResourceBox(StoryboardKey);
            if (box == null && target.TemplatedParent != null) {
                box = target.TemplatedParent.FindResourceBox(StoryboardKey);
            }
            if (box is Storyboard) { _storyboard = box as Storyboard; }
        }
        if (_storyboard != null && StoryboardKey.Length > 0 && !_storyboardSettersExtracted) {
            _storyboardSettersExtracted = true;
            TimelineAnimation[] tls = _storyboard.Children();
            if (tls != null) {
                for (int i = 0; i < tls.Length; i += 1) {
                    TimelineAnimation tl = tls[i];
                    if (tl == null || tl.HasAnimatedDuration() || tl.TargetProperty.Length == 0) { continue; }
                    if (tl.IsColor) {
                        string hex = tl.ToColor.Length > 0
                            ? tl.ToColor
                            : ParseUtil.FormatArgbHex(tl.HasColorUint ? tl.ToRgba : 0u);
                        AddSetterBox(tl.TargetProperty, DependencyObject.BoxString(hex), tl.TargetName);
                    } else {
                        float val = (tl.KeyFrames != null && tl.KeyFrames.Length > 0)
                            ? tl.KeyFrames[tl.KeyFrames.Length - 1].Value
                            : tl.ToFloat;
                        AddSetterBox(tl.TargetProperty, DependencyObject.BoxFloat(val), tl.TargetName);
                    }
                }
            }
        }
    }

    internal void StartStoryboardOnly(UIElement target) {
        if (target == null) { return; }
        EnsureResolvedStoryboard(target);
        if (_storyboard != null) {
            _storyboard.BeginAnimatedOnly(target);
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
    public string From = "";
    public string To = "";
    public float GeneratedDurationSec = 0.0f;
    public EasingFunctionKind EasingKind = EasingFunctionKind.Linear;
    public EasingMode EasingMode = EasingMode.EaseOut;
    public float EaseParam0 = 0.0f;
    public float EaseParam1 = 0.0f;
    public int EaseParamMask = 0;
    public Storyboard Storyboard;

    public void SetEasing(int kind, int mode) {
        EasingKind = Storyboard.IntToEasingKind(kind);
        EasingMode = Storyboard.IntToEasingMode(mode);
    }

    public void SetEasingParams(float p0, float p1, int mask = 3) {
        EaseParam0 = p0;
        EaseParam1 = p1;
        EaseParamMask = mask;
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
    private UIElement[] _baseAnimTargets;
    private int[] _baseAnimPropIds;
    private bool[] _baseAnimIsColor;
    private float[] _baseAnimFloats;
    private uint[] _baseAnimColors;
    private int[] _transitionTrackIds;

    public VisualStateGroup() {
        _states = new VisualState[0];
        _transitions = new VisualTransition[0];
        _baseTargets = new UIElement[0];
        _baseProps = new string[0];
        _baseBoxes = new DependencyObject[0];
        _baseAnimTargets = new UIElement[0];
        _baseAnimPropIds = new int[0];
        _baseAnimIsColor = new bool[0];
        _baseAnimFloats = new float[0];
        _baseAnimColors = new uint[0];
        _transitionTrackIds = new int[0];
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

    private void RecordBaseAnimSnapshot(UIElement dest, int propId, bool isColor) {
        if (dest == null || propId < 0 || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { return; }
        for (int i = 0; i < _baseAnimTargets.Length; i += 1) {
            if (_baseAnimTargets[i] == dest && _baseAnimPropIds[i] == propId) {
                return;
            }
        }
        _baseAnimTargets.Push(dest);
        _baseAnimPropIds.Push(propId);
        _baseAnimIsColor.Push(isColor);
        if (isColor) {
            _baseAnimFloats.Push(0.0f);
            _baseAnimColors.Push(Anim_ReadCurrentColor(dest.__nativeHandle, propId));
        } else {
            _baseAnimFloats.Push(Anim_ReadCurrentFloat(dest.__nativeHandle, propId));
            _baseAnimColors.Push(0u);
        }
    }

    private static bool NextStoryboardAnimates(Storyboard sb, UIElement scope, UIElement dest, int propId) {
        if (sb == null || scope == null || dest == null || propId < 0) { return false; }
        TimelineAnimation[] tls = sb.Children();
        if (tls == null) { return false; }
        for (int i = 0; i < tls.Length; i += 1) {
            TimelineAnimation tl = tls[i];
            if (tl == null || tl.TargetProperty.Length == 0) { continue; }
            int pid = EnumConvert.ParseAnimPropOrInvalid(tl.TargetProperty);
            if (pid != propId) { continue; }
            if (TimelineAnimation.ResolveAnimTarget(scope, tl.TargetName) == dest) {
                return true;
            }
        }
        return false;
    }

    private static bool IsColorAnimPropId(int propId) {
        return propId == 16 || propId == 17 || propId == 18 || propId == 29 || propId == 30 ||
               (propId >= 37 && propId <= 40) || (propId >= 45 && propId <= 48);
    }

    private static void TryCaptureAnimStart(
        UIElement dest,
        int propId,
        bool isCol,
        Storyboard nextSb,
        UIElement scope,
        UIElement[] targets,
        int[] propIds,
        bool[] isCols,
        float[] sFloats,
        uint[] sColors) {
        if (dest == null || dest._isDisposedTree || propId < 0 || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { return; }
        if (NextStoryboardAnimates(nextSb, scope, dest, propId)) { return; }
        for (int d = 0; d < targets.Length; d += 1) {
            if (targets[d] == dest && propIds[d] == propId) { return; }
        }
        targets.Push(dest);
        propIds.Push(propId);
        isCols.Push(isCol);
        if (isCol) {
            sFloats.Push(0.0f);
            sColors.Push(Anim_ReadCurrentColor(dest.__nativeHandle, propId));
        } else {
            sFloats.Push(Anim_ReadCurrentFloat(dest.__nativeHandle, propId));
            sColors.Push(0u);
        }
    }

    public bool GoTo(VisualState next, UIElement scope, bool useTransitions = true) {
        if (next == null) { return false; }
        if (_currentState == next && CurrentStateName == next.Name) { return true; }

        if (scope != null) {
            next.EnsureResolvedStoryboard(scope);
        }
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
        if (scope != null && next.Storyboard != null) {
            TimelineAnimation[] sbTls = next.Storyboard.Children();
            if (sbTls != null) {
                for (int i = 0; i < sbTls.Length; i += 1) {
                    TimelineAnimation tl = sbTls[i];
                    if (tl == null || tl.TargetProperty.Length == 0) { continue; }
                    int pid = EnumConvert.ParseAnimPropOrInvalid(tl.TargetProperty);
                    if (pid < 0) { continue; }
                    UIElement dest = TimelineAnimation.ResolveAnimTarget(scope, tl.TargetName);
                    if (dest != null) {
                        RecordBaseAnimSnapshot(dest, pid, tl.IsColor || IsColorAnimPropId(pid));
                    }
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
                int pid = EnumConvert.ParseAnimPropOrInvalid(_baseProps[i]);
                TryCaptureAnimStart(_baseTargets[i], pid, IsColorAnimPropId(pid), next.Storyboard, scope,
                    animTargets, animPropIds, animIsColor, startFloats, startColors);
            }
            for (int i = 0; i < _baseAnimTargets.Length; i += 1) {
                TryCaptureAnimStart(_baseAnimTargets[i], _baseAnimPropIds[i], _baseAnimIsColor[i], next.Storyboard, scope,
                    animTargets, animPropIds, animIsColor, startFloats, startColors);
            }
        }

        if (_currentState != null) {
            _currentState.StopStoryboardOnly(scope);
        }

        if (_transitionTrackIds != null && _transitionTrackIds.Length > 0) {
            for (int i = 0; i < _transitionTrackIds.Length; i += 1) {
                Anim_StopTrack(_transitionTrackIds[i]);
            }
            _transitionTrackIds.Clear();
        }

        for (int i = _baseTargets.Length - 1; i >= 0; i -= 1) {
            if (_baseTargets[i] != null && !_baseTargets[i]._isDisposedTree) {
                _baseTargets[i].SetPropertyValue(_baseProps[i], _baseBoxes[i]);
            }
        }

        for (int i = _baseAnimTargets.Length - 1; i >= 0; i -= 1) {
            UIElement dest = _baseAnimTargets[i];
            if (dest != null && !dest._isDisposedTree && ParseUtil.IsValidHandle(dest.__nativeHandle)) {
                if (_baseAnimIsColor[i]) {
                    Anim_ApplyColor(dest.__nativeHandle, _baseAnimPropIds[i], _baseAnimColors[i]);
                } else {
                    Anim_ApplyFloat(dest.__nativeHandle, _baseAnimPropIds[i], _baseAnimFloats[i]);
                }
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
                        int ctid = Anim_StartColorDelayed(dest.__nativeHandle, propId, startCol, endCol, dur, ek, em, 0.0f);
                        if (ctid > 0) {
                            if (tr != null) { TimelineAnimation.ApplyTrackEasing(ctid, tr.EaseParam0, tr.EaseParam1, tr.EaseParamMask); }
                            _transitionTrackIds.Push(ctid);
                        }
                    }
                } else {
                    float startVal = startFloats[i];
                    float endVal = Anim_ReadCurrentFloat(dest.__nativeHandle, propId);
                    if (Math.Absf(endVal - startVal) > 0.0005f) {
                        int ftid = Anim_StartFloat(dest.__nativeHandle, propId, startVal, endVal, dur, ek, em, 0.0f, false, 1);
                        if (ftid > 0) {
                            if (tr != null) { TimelineAnimation.ApplyTrackEasing(ftid, tr.EaseParam0, tr.EaseParam1, tr.EaseParamMask); }
                            _transitionTrackIds.Push(ftid);
                        }
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

    private static UIElement FindDataTemplateVsgRoot(UIElement control, UIElement tplRoot) {
        if (!(control is ContentControl) || tplRoot == null) { return null; }
        ContentPresenter cp = (tplRoot is ContentPresenter)
            ? (tplRoot as ContentPresenter)
            : ContentControl.FindFirstTemplatedPresenter(tplRoot, control, false);
        if (cp != null && cp.ChildCount() > 0) {
            UIElement dtRoot = cp.ChildAt(0);
            if (dtRoot != null && !(dtRoot is Control) && dtRoot._vsgBox is VisualStateGroupList) {
                return dtRoot;
            }
        }
        return null;
    }

    public static bool HasState(UIElement control, string stateName) {
        if (control == null || stateName == null) { return false; }
        if (control is Control) {
            UIElement tplRoot = (control as Control).TemplateRoot();
            if (tplRoot != null && HasStateInSubtree(tplRoot, control, stateName)) {
                return true;
            }
            UIElement dtRoot = FindDataTemplateVsgRoot(control, tplRoot);
            if (dtRoot != null && HasState(dtRoot, stateName)) {
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

    public static bool GoToElementState(UIElement element, string stateName, bool useTransitions = true) {
        return GoToState(element, stateName, useTransitions);
    }

    public static bool GoToState(UIElement control, string stateName, bool useTransitions = true) {
        if (control == null || stateName == null) { return false; }
        if (control is Control) {
            UIElement tplRoot = (control as Control).TemplateRoot();
            if (tplRoot != null && TryGoToInSubtree(tplRoot, control, stateName, useTransitions)) {
                return true;
            }
            UIElement dtRoot = FindDataTemplateVsgRoot(control, tplRoot);
            if (dtRoot != null && GoToState(dtRoot, stateName, useTransitions)) {
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
    public float KeyTimeSec = 0.0f;
    public float Value = 0.0f;
    public Binding ValueBinding;
    public float EaseParam0 = 0.0f;
    public float EaseParam1 = 0.0f;
    public int EaseParamMask = 0;

    internal float ResolveValue(UIElement scope) {
        if (ValueBinding != null && scope != null) {
            DependencyObject sv = BindingEntry.EvaluateStandaloneBinding(scope, ValueBinding);
            if (sv != null) { return DependencyObject.UnboxFloat(sv, Value); }
        }
        return Value;
    }
    public EasingFunctionKind EasingKind = EasingFunctionKind.Linear;
    public EasingMode EasingMode = EasingMode.EaseOut;

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

internal class ManagedAnimBinding {
    public int TrackId = 0;
    public UIElement Dest;
    public UIElement Scope;
    public string TargetProp = "";
    public bool IsColor = false;
    public int BuiltinColorPropId = 0;
    public float EndFloat = 0.0f;
    public uint EndColor = 0u;
    public bool SnapEndOnFinish = false;
}

public class TimelineAnimation : DependencyObject {
    public string TargetName = "";
    public string TargetProperty = "Opacity";
    public float FromFloat = 0.0f;
    public float ToFloat = 1.0f;
    public bool HasFrom = false;
    public float DurationSec = 0.25f;
    public float DelaySec = 0.0f;
    public EasingFunctionKind EasingKind = EasingFunctionKind.Cubic;
    public EasingMode EasingMode = EasingMode.EaseOut;
    public bool AutoReverse = false;
    public int RepeatCount = 1;
    public bool IsColor = false;
    public string FromColor = "";
    public string ToColor = "";
    public uint FromRgba = 0u;
    public uint ToRgba = 0u;
    public bool HasColorUint = false;
    public bool UseSpring = false;
    public float Stiffness = 320.0f;
    public float Damping = 28.0f;
    public float EaseParam0 = 0.0f;
    public float EaseParam1 = 0.0f;
    public int EaseParamMask = 0;
    public DoubleKeyFrame[] KeyFrames;
    private int[] _activeTrackIds;
    private UIElement[] _activeTrackScopes;

    public TimelineAnimation() {
        KeyFrames = new DoubleKeyFrame[0];
        _activeTrackIds = new int[0];
        _activeTrackScopes = new UIElement[0];
    }

    private void StopTrackedForScope(UIElement scope) {
        if (_activeTrackIds == null || _activeTrackIds.Length == 0) { return; }
        if (scope == null) {
            for (int i = 0; i < _activeTrackIds.Length; i += 1) {
                Anim_StopTrack(_activeTrackIds[i]);
            }
            _activeTrackIds.Clear();
            _activeTrackScopes.Clear();
            return;
        }
        for (int i = _activeTrackIds.Length - 1; i >= 0; i -= 1) {
            UIElement sc = i < _activeTrackScopes.Length ? _activeTrackScopes[i] : null;
            if (sc == null || sc == scope || sc._isDisposedTree) {
                Anim_StopTrack(_activeTrackIds[i]);
                _activeTrackIds.RemoveAt(i);
                if (i < _activeTrackScopes.Length) { _activeTrackScopes.RemoveAt(i); }
            }
        }
    }

    private int[] RecordStartedForScope(UIElement scope, UIElement dest, int propId, int[] started) {
        if (started != null) {
            for (int i = 0; i < started.Length; i += 1) {
                _activeTrackIds.Push(started[i]);
                _activeTrackScopes.Push(scope);
            }
        }
        if (_lastManagedBindings != null) {
            for (int i = 0; i < _lastManagedBindings.Length; i += 1) {
                if (_lastManagedBindings[i] != null) { _lastManagedBindings[i].Scope = scope; }
            }
        }
        return started;
    }

    internal static UIElement ResolveAnimTarget(UIElement rootScope, string targetName) {
        if (rootScope == null) { return null; }
        if (targetName == null || targetName.Length == 0) { return rootScope; }
        if (rootScope._internalRootName == targetName) { return rootScope; }
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

    internal ManagedAnimBinding[] _lastManagedBindings;

    internal static uint ReadCurrentManagedColor(UIElement dest, string propName, int builtinPropId) {
        if (dest == null) { return 0u; }
        Brush b = null;
        if (builtinPropId == 16) { b = dest.Background; }
        else if (builtinPropId == 18) { b = dest.BorderBrush; }
        else if (builtinPropId == 17 && dest is Control) { b = (dest as Control).Foreground; }
        if (b != null) { return b.ResolveRgba(); }
        DependencyObject box = dest.GetPropertyValue(propName);
        if (box is Brush) { return (box as Brush).ResolveRgba(); }
        string s = DependencyObject.UnboxString(box, "");
        if (s.Length > 0) { return ParseUtil.ParseColor(s); }
        return 0u;
    }

    internal static void ApplyManagedColor(UIElement dest, string propName, int builtinPropId, uint rgba) {
        if (dest == null || dest._isDisposedTree) { return; }
        if (builtinPropId == 16) { dest.Background = new SolidColorBrush(rgba); return; }
        if (builtinPropId == 17) {
            if (dest is Control) { (dest as Control).Foreground = new SolidColorBrush(rgba); return; }
        }
        if (builtinPropId == 18) { dest.BorderBrush = new SolidColorBrush(rgba); return; }
        dest.SetPropertyValue(propName, DependencyObject.BoxString(ParseUtil.FormatArgbHex(rgba)));
    }

    internal bool HasAnimatedDuration() {
        return DurationSec > 0.001f || AutoReverse || RepeatCount != 1 || UseSpring ||
               (KeyFrames != null && KeyFrames.Length > 1);
    }

    public void StartOn(UIElement rootScope) {
        StartOnTracked(rootScope, 1.0f, false);
        if (_lastManagedBindings != null && _lastManagedBindings.Length > 0) {
            Storyboard.RegisterAdHocManaged(_lastManagedBindings);
        }
    }

    private void PushSelfSyncTrack(UIElement dest, int tid, float endFloat, bool snapEnd) {
        if (_lastManagedBindings == null) { _lastManagedBindings = new ManagedAnimBinding[0]; }
        var mb = new ManagedAnimBinding();
        mb.TrackId = tid;
        mb.Dest = dest;
        mb.TargetProp = TargetProperty;
        mb.EndFloat = endFloat;
        mb.SnapEndOnFinish = snapEnd;
        _lastManagedBindings.Push(mb);
    }

    private void PushManagedColorTrack(UIElement dest, int tid, int colPropId, uint endColor, bool snapEnd) {
        if (_lastManagedBindings == null) { _lastManagedBindings = new ManagedAnimBinding[0]; }
        var mb = new ManagedAnimBinding();
        mb.TrackId = tid;
        mb.Dest = dest;
        mb.TargetProp = TargetProperty;
        mb.IsColor = true;
        mb.BuiltinColorPropId = colPropId;
        mb.EndColor = endColor;
        mb.SnapEndOnFinish = snapEnd;
        _lastManagedBindings.Push(mb);
    }

    internal static void ApplyTrackEasing(int tid, float p0, float p1, int mask) {
        if (tid > 0 && (mask != 0 || p0 != 0.0f || p1 != 0.0f)) {
            Anim_SetTrackEasingParams(tid, p0, p1, mask != 0 ? mask : 3);
        }
    }

    internal int[] StartOnTracked(UIElement rootScope, float speedRatio, bool forceRepeatForever) {
        var started = new int[0];
        _lastManagedBindings = null;
        StopTrackedForScope(rootScope);
        if (rootScope == null) { return started; }
        UIElement dest = ResolveAnimTarget(rootScope, TargetName);
        if (dest == null || !ParseUtil.IsValidHandle(dest.__nativeHandle)) { return started; }
        int propId = EnumConvert.ParseAnimPropOrInvalid(TargetProperty);
        if (propId < 0 && (TargetProperty == null || TargetProperty.Length == 0)) { return started; }
        int ek = EnumConvert.ToInt(EasingKind);
        int em = EnumConvert.ToInt(EasingMode);
        float invSpeed = speedRatio > 0.001f ? (1.0f / speedRatio) : 1.0f;
        float effDur = DurationSec * invSpeed;
        float effDelay = DelaySec * invSpeed;
        int effRepeat = forceRepeatForever ? -1 : RepeatCount;
        if (propId >= 24 && propId <= 26) { dest.EnsureLayoutTransformEnabledForAnim(); }

        if (IsColor) {
            uint toCol = HasColorUint ? ToRgba : (ToColor.Length > 0 ? ParseUtil.ParseColor(ToColor) : 0u);
            bool managedCol = (propId < 0) ||
                (dest is Control && (dest as Control).Template != null && (propId == 16 || propId == 17 || propId == 18));
            if (managedCol) {
                int builtinId = propId < 0 ? 0 : propId;
                bool hasExpFrom = HasColorUint || HasFrom || FromColor.Length > 0;
                uint fromCol = hasExpFrom
                    ? (HasColorUint ? FromRgba : ParseUtil.ParseColor(FromColor))
                    : ReadCurrentManagedColor(dest, TargetProperty, builtinId);
                int tidC = Anim_StartColorEx(
                    dest.__nativeHandle, 54, fromCol, toCol, effDur, ek, em, effDelay, AutoReverse, effRepeat);
                if (effDur <= 0.0005f && effDelay <= 0.0005f) {
                    ApplyManagedColor(dest, TargetProperty, builtinId, AutoReverse ? fromCol : toCol);
                } else if (hasExpFrom && effDelay <= 0.0005f) {
                    ApplyManagedColor(dest, TargetProperty, builtinId, fromCol);
                }
                if (tidC > 0) {
                    started.Push(tidC);
                    PushManagedColorTrack(dest, tidC, builtinId, AutoReverse ? fromCol : toCol, effRepeat > 0);
                }
                return RecordStartedForScope(rootScope, dest, propId, started);
            }
            int tidCol = (HasColorUint || HasFrom || FromColor.Length > 0)
                ? Anim_StartColorEx(dest.__nativeHandle, propId,
                    HasColorUint ? FromRgba : ParseUtil.ParseColor(FromColor), toCol, effDur, ek, em, effDelay, AutoReverse, effRepeat)
                : Anim_StartColorToEx(dest.__nativeHandle, propId, toCol, effDur, ek, em, effDelay, AutoReverse, effRepeat);
            if (tidCol > 0) { started.Push(tidCol); }
            return RecordStartedForScope(rootScope, dest, propId, started);
        }

        if (propId >= 0 && UseSpring) {
            int tidSp = Anim_SpringFloat(dest.__nativeHandle, propId, ToFloat, Stiffness, Damping);
            if (tidSp > 0) {
                started.Push(tidSp);
                if (propId >= 3 && propId <= 9 && dest._ownBindingsBox != null) {
                    PushSelfSyncTrack(dest, tidSp, ToFloat, true);
                }
            }
            return RecordStartedForScope(rootScope, dest, propId, started);
        }

        bool trackSelfSync = (propId < 0) || (propId >= 3 && propId <= 9 && dest._ownBindingsBox != null);
        int nativeProp = propId < 0 ? 54 : propId;

        if (KeyFrames != null && KeyFrames.Length > 0) {
            bool firstAtZero = KeyFrames[0].KeyTimeSec <= 0.0001f;
            float firstVal = KeyFrames[0].ResolveValue(rootScope);
            if (KeyFrames.Length == 1 && firstAtZero) {
                if (effDelay <= 0.0005f) {
                    if (propId < 0 || trackSelfSync) {
                        dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(firstVal));
                    } else {
                        Anim_ApplyFloat(dest.__nativeHandle, nativeProp, firstVal);
                    }
                }
                int tid0 = Anim_StartFloat(
                    dest.__nativeHandle, nativeProp, firstVal, firstVal, 0.001f,
                    EnumConvert.ToInt(KeyFrames[0].EasingKind), EnumConvert.ToInt(KeyFrames[0].EasingMode),
                    effDelay, AutoReverse, effRepeat);
                if (tid0 > 0) {
                    ApplyTrackEasing(tid0, KeyFrames[0].EaseParam0, KeyFrames[0].EaseParam1, KeyFrames[0].EaseParamMask);
                    started.Push(tid0);
                    if (trackSelfSync) { PushSelfSyncTrack(dest, tid0, firstVal, effRepeat > 0); }
                }
                return RecordStartedForScope(rootScope, dest, propId, started);
            }
            float startVal = HasFrom
                ? FromFloat
                : (propId < 0 ? DependencyObject.UnboxFloat(dest.GetPropertyValue(TargetProperty), 0.0f) : Anim_ReadCurrentFloat(dest.__nativeHandle, propId));
            if (firstAtZero && effDelay <= 0.0005f) {
                if (propId < 0 || trackSelfSync) {
                    dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(firstVal));
                } else {
                    Anim_ApplyFloat(dest.__nativeHandle, nativeProp, firstVal);
                }
            }
            int ktid = Anim_BeginKeyFrames(dest.__nativeHandle, nativeProp, effDelay, AutoReverse, effRepeat);
            if (ktid > 0) {
                if (!firstAtZero) {
                    Anim_AddKeyFrameFloat(ktid, 0.0f, startVal, 0, 0, 0.0f, 0.0f, 0, false);
                }
                float endVal = startVal;
                for (int i = 0; i < KeyFrames.Length; i += 1) {
                    DoubleKeyFrame kf = KeyFrames[i];
                    float kfVal = kf.ResolveValue(rootScope);
                    endVal = kfVal;
                    int kfMask = kf.EaseParamMask != 0
                        ? kf.EaseParamMask
                        : ((kf.EaseParam0 != 0.0f || kf.EaseParam1 != 0.0f) ? 3 : 0);
                    Anim_AddKeyFrameFloat(
                        ktid, Math.Maxf(0.0f, kf.KeyTimeSec * invSpeed), kfVal,
                        EnumConvert.ToInt(kf.EasingKind), EnumConvert.ToInt(kf.EasingMode),
                        kf.EaseParam0, kf.EaseParam1, kfMask, false);
                }
                started.Push(ktid);
                if (trackSelfSync) {
                    float finishVal = AutoReverse ? (firstAtZero ? firstVal : startVal) : endVal;
                    PushSelfSyncTrack(dest, ktid, finishVal, effRepeat > 0);
                }
            }
            return RecordStartedForScope(rootScope, dest, propId, started);
        }

        float mainFromVal = FromFloat;
        if (propId < 0 && !HasFrom) {
            mainFromVal = DependencyObject.UnboxFloat(dest.GetPropertyValue(TargetProperty), 0.0f);
        }
        if (effDur <= 0.0005f && effDelay <= 0.0005f) {
            float snapVal = (propId < 0 && AutoReverse) ? mainFromVal : ToFloat;
            dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(snapVal));
        } else if (propId < 0 && HasFrom && effDelay <= 0.0005f) {
            dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(mainFromVal));
        }
        int tidMain = 0;
        if (propId < 0 || HasFrom) {
            tidMain = Anim_StartFloat(dest.__nativeHandle, nativeProp, mainFromVal, ToFloat, effDur, ek, em, effDelay, AutoReverse, effRepeat);
        } else {
            if (trackSelfSync && AutoReverse) { mainFromVal = Anim_ReadCurrentFloat(dest.__nativeHandle, propId); }
            tidMain = Anim_StartFloatTo(dest.__nativeHandle, propId, ToFloat, effDur, ek, em, effDelay, AutoReverse, effRepeat);
        }
        if (tidMain > 0) {
            ApplyTrackEasing(tidMain, EaseParam0, EaseParam1, EaseParamMask);
            started.Push(tidMain);
            if (trackSelfSync) { PushSelfSyncTrack(dest, tidMain, AutoReverse ? mainFromVal : ToFloat, effRepeat > 0); }
        }
        return RecordStartedForScope(rootScope, dest, propId, started);
    }

    public void StopOn(UIElement rootScope) {
        if (_activeTrackIds != null && _activeTrackIds.Length > 0) {
            StopTrackedForScope(rootScope);
            return;
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

internal class DiscreteKeyFrameEntry {
    public string TargetName = "";
    public string TargetProp = "";
    public float KeyTimeSec = 0.0f;
    public DependencyObject ValueBox;
    public DiscreteKeyFrameEntry(string name, string prop, float keyTime, DependencyObject box) {
        TargetName = name == null ? "" : name;
        TargetProp = prop == null ? "" : prop;
        KeyTimeSec = keyTime;
        ValueBox = box;
    }
}

internal class PendingDiscreteApply {
    public UIElement Scope;
    public UIElement Dest;
    public string TargetProp = "";
    public DependencyObject ValueBox;
    public int TrackId = 0;
    public bool Applied = false;
    public PendingDiscreteApply(UIElement dest, string prop, DependencyObject box, int tid) {
        Dest = dest;
        TargetProp = prop;
        ValueBox = box;
        TrackId = tid;
    }
}

public class Storyboard : DependencyObject {
    public string TargetName { get; set; } = "";
    public UIElement Target { get; set; }
    public float SpeedRatio { get; set; } = 1.0f;
    public bool RepeatForever { get; set; } = false;
    private TimelineAnimation[] _children;
    private DiscreteKeyFrameEntry[] _discreteKeyFrames;
    private PendingDiscreteApply[] _pendingDiscrete;
    private ManagedAnimBinding[] _managedTracks;
    private int[] _runningTrackIds;
    private UIElement[] _runningTrackScopes;
    private UIElement _lastScope;
    private bool _awaitingCompletion = false;
    private bool _keepTickingManaged = false;
    private Action _oneShotCompletion;
    private static Storyboard[] _activeStoryboards;
    private static ManagedAnimBinding[] _adHocManaged;
    private static bool _completionHookInstalled = false;
    public event EventHandler Completed;

    public Storyboard() {
        _children = new TimelineAnimation[0];
        _runningTrackIds = new int[0];
        _runningTrackScopes = new UIElement[0];
    }

    public TimelineAnimation[] Children() { return _children; }
    public void Add(TimelineAnimation anim) { if (anim != null) { _children.Push(anim); } }
    public void SetLastEasingParams(float p0, float p1, int mask = 3) {
        if (_children != null && _children.Length > 0) {
            TimelineAnimation last = _children[_children.Length - 1];
            if (last != null) {
                last.EaseParam0 = p0;
                last.EaseParam1 = p1;
                last.EaseParamMask = mask;
            }
        }
    }

    public void AddDiscreteKeyFrame(string targetName, string targetProp, float keyTimeSec, DependencyObject valueBox) {
        if (targetProp == null || targetProp.Length == 0) { return; }
        if (_discreteKeyFrames == null) { _discreteKeyFrames = new DiscreteKeyFrameEntry[0]; }
        _discreteKeyFrames.Push(new DiscreteKeyFrameEntry(targetName, targetProp, keyTimeSec, valueBox));
    }

    public void MergeFrom(Storyboard other) {
        if (other == null || other == this) { return; }
        TimelineAnimation[] kids = other.Children();
        if (kids != null) {
            for (int i = 0; i < kids.Length; i += 1) {
                if (kids[i] != null) { _children.Push(kids[i]); }
            }
        }
        if (other._discreteKeyFrames != null) {
            if (_discreteKeyFrames == null) { _discreteKeyFrames = new DiscreteKeyFrameEntry[0]; }
            for (int i = 0; i < other._discreteKeyFrames.Length; i += 1) {
                DiscreteKeyFrameEntry dk = other._discreteKeyFrames[i];
                if (dk != null) {
                    _discreteKeyFrames.Push(new DiscreteKeyFrameEntry(dk.TargetName, dk.TargetProp, dk.KeyTimeSec, dk.ValueBox));
                }
            }
        }
    }

    public static EasingFunctionKind IntToEasingKind(int kind) {
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

    public static EasingMode IntToEasingMode(int mode) {
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

    public void AddDoubleAnimationEx(
        string targetName, string targetProp, float from, float to, float durationSec,
        int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) {
        var da = new DoubleAnimation(from, to, durationSec);
        da.TargetName = targetName == null ? "" : targetName;
        da.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        da.EasingKind = IntToEasingKind(easeKind);
        da.EasingMode = IntToEasingMode(easeMode);
        da.DelaySec = delaySec;
        da.AutoReverse = autoReverse;
        da.RepeatCount = repeatCount;
        _children.Push(da);
    }

    public void AddDoubleToAnimationEx(
        string targetName, string targetProp, float to, float durationSec,
        int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) {
        var da = new DoubleAnimation(to, durationSec);
        da.TargetName = targetName == null ? "" : targetName;
        da.TargetProperty = targetProp == null ? "Opacity" : targetProp;
        da.EasingKind = IntToEasingKind(easeKind);
        da.EasingMode = IntToEasingMode(easeMode);
        da.DelaySec = delaySec;
        da.AutoReverse = autoReverse;
        da.RepeatCount = repeatCount;
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

    public void AddColorAnimationEx(
        string targetName, string targetProp, string fromColor, string toColor, float durationSec,
        int easeKind, int easeMode, float delaySec, bool hasFrom, bool autoReverse, int repeatCount) {
        var ca = new ColorAnimation();
        ca.IsColor = true;
        ca.HasFrom = hasFrom;
        ca.FromColor = hasFrom && fromColor != null ? fromColor : "";
        ca.ToColor = toColor == null ? "" : toColor;
        ca.DurationSec = durationSec;
        ca.TargetName = targetName == null ? "" : targetName;
        ca.TargetProperty = targetProp == null ? "Background" : targetProp;
        ca.EasingKind = IntToEasingKind(easeKind);
        ca.EasingMode = IntToEasingMode(easeMode);
        ca.DelaySec = delaySec;
        ca.AutoReverse = autoReverse;
        ca.RepeatCount = repeatCount;
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

    private static void EnsureCompletionHook() {
        if (!_completionHookInstalled) {
            _completionHookInstalled = true;
            _activeStoryboards = new Storyboard[0];
            BindingEngine.AddPreFlushHook(() => { Storyboard.TickAllActive(); });
            UIElement.RegisterDisposeHook(() => { Storyboard.CleanupDisposed(); });
        }
    }

    internal static void RegisterAdHocManaged(ManagedAnimBinding[] bindings) {
        if (bindings == null || bindings.Length == 0) { return; }
        EnsureCompletionHook();
        if (_adHocManaged == null) { _adHocManaged = new ManagedAnimBinding[0]; }
        for (int i = 0; i < bindings.Length; i += 1) {
            ManagedAnimBinding mb = bindings[i];
            if (mb != null && mb.TrackId > 0) { _adHocManaged.Push(mb); }
        }
    }

    private static void RegisterActiveStoryboard(Storyboard sb) {
        if (sb == null) { return; }
        if (_activeStoryboards == null) { _activeStoryboards = new Storyboard[0]; }
        for (int i = 0; i < _activeStoryboards.Length; i += 1) {
            if (_activeStoryboards[i] == sb) { return; }
        }
        _activeStoryboards.Push(sb);
    }

    internal static void CleanupDisposed() {
        if (_adHocManaged != null && _adHocManaged.Length > 0) {
            for (int i = _adHocManaged.Length - 1; i >= 0; i -= 1) {
                ManagedAnimBinding mb = _adHocManaged[i];
                if (mb == null || mb.Dest == null || mb.Dest._isDisposedTree) {
                    if (mb != null && mb.TrackId > 0) { Anim_StopTrack(mb.TrackId); }
                    _adHocManaged.RemoveAt(i);
                }
            }
        }
        if (_activeStoryboards == null || _activeStoryboards.Length == 0) { return; }
        for (int i = _activeStoryboards.Length - 1; i >= 0; i -= 1) {
            Storyboard sb = _activeStoryboards[i];
            if (sb == null || (sb.Target != null && sb.Target._isDisposedTree)) {
                if (sb != null) {
                    sb._awaitingCompletion = false;
                    sb.StopRunningTracksOnly();
                }
                _activeStoryboards.RemoveAt(i);
            } else {
                sb.PruneDisposedScopes();
            }
        }
    }

    private static bool TickManagedBinding(ManagedAnimBinding mb) {
        if (mb == null || mb.TrackId <= 0) { return false; }
        if (mb.Dest == null || mb.Dest._isDisposedTree) {
            Anim_StopTrack(mb.TrackId);
            mb.TrackId = 0;
            return false;
        }
        if (Anim_IsTrackActive(mb.TrackId)) {
            if (Anim_IsTrackStarted(mb.TrackId)) {
                if (mb.IsColor) {
                    uint col = Anim_ReadTrackColor(mb.TrackId, mb.EndColor);
                    TimelineAnimation.ApplyManagedColor(mb.Dest, mb.TargetProp, mb.BuiltinColorPropId, col);
                } else {
                    float val = Anim_ReadTrackFloat(mb.TrackId, mb.EndFloat);
                    mb.Dest.SetPropertyValue(mb.TargetProp, DependencyObject.BoxFloat(val));
                }
            }
            return true;
        }
        if (mb.SnapEndOnFinish) {
            if (mb.IsColor) {
                TimelineAnimation.ApplyManagedColor(mb.Dest, mb.TargetProp, mb.BuiltinColorPropId, mb.EndColor);
            } else {
                mb.Dest.SetPropertyValue(mb.TargetProp, DependencyObject.BoxFloat(mb.EndFloat));
            }
        }
        mb.TrackId = 0;
        return false;
    }

    public static void TickAllActive() {
        if (_adHocManaged != null && _adHocManaged.Length > 0) {
            var nextAdHoc = new ManagedAnimBinding[0];
            for (int i = 0; i < _adHocManaged.Length; i += 1) {
                ManagedAnimBinding mb = _adHocManaged[i];
                if (TickManagedBinding(mb)) { nextAdHoc.Push(mb); }
            }
            _adHocManaged = nextAdHoc;
        }
        if (_activeStoryboards == null || _activeStoryboards.Length == 0) { return; }
        var remaining = new Storyboard[0];
        var finished = new Storyboard[0];
        for (int i = 0; i < _activeStoryboards.Length; i += 1) {
            Storyboard sb = _activeStoryboards[i];
            if (sb == null || (!sb._awaitingCompletion && !sb._keepTickingManaged)) { continue; }
            if (sb.Target != null && sb.Target._isDisposedTree) {
                sb._awaitingCompletion = false;
                sb.StopRunningTracksOnly();
                continue;
            }
            sb.PruneDisposedScopes();
            if (sb._pendingDiscrete != null && sb._pendingDiscrete.Length > 0) {
                var nextDiscrete = new PendingDiscreteApply[0];
                for (int d = 0; d < sb._pendingDiscrete.Length; d += 1) {
                    PendingDiscreteApply pd = sb._pendingDiscrete[d];
                    if (pd == null || pd.Applied) { continue; }
                    if (!Anim_IsTrackActive(pd.TrackId)) {
                        pd.Applied = true;
                        if (pd.Dest != null && !pd.Dest._isDisposedTree) {
                            pd.Dest.SetPropertyValue(pd.TargetProp, pd.ValueBox);
                        }
                    } else {
                        nextDiscrete.Push(pd);
                    }
                }
                sb._pendingDiscrete = nextDiscrete.Length > 0 ? nextDiscrete : null;
            }
            bool anyManagedActive = false;
            if (sb._managedTracks != null && sb._managedTracks.Length > 0) {
                var nextManaged = new ManagedAnimBinding[0];
                for (int m = 0; m < sb._managedTracks.Length; m += 1) {
                    ManagedAnimBinding mb = sb._managedTracks[m];
                    if (TickManagedBinding(mb)) {
                        nextManaged.Push(mb);
                    }
                }
                sb._managedTracks = nextManaged.Length > 0 ? nextManaged : null;
                anyManagedActive = nextManaged.Length > 0;
            }
            bool anyNativeActive = false;
            if (sb._runningTrackIds != null && sb._runningTrackIds.Length > 0) {
                var nextIds = new int[0];
                var nextScopes = new UIElement[0];
                for (int j = 0; j < sb._runningTrackIds.Length; j += 1) {
                    int tid = sb._runningTrackIds[j];
                    if (Anim_IsTrackActive(tid)) {
                        nextIds.Push(tid);
                        UIElement sc = (sb._runningTrackScopes != null && j < sb._runningTrackScopes.Length)
                            ? sb._runningTrackScopes[j]
                            : null;
                        nextScopes.Push(sc);
                    }
                }
                sb._runningTrackIds = nextIds;
                sb._runningTrackScopes = nextScopes;
                anyNativeActive = nextIds.Length > 0;
            }
            bool anyActive = anyManagedActive || anyNativeActive || (sb._pendingDiscrete != null && sb._pendingDiscrete.Length > 0);
            if (anyActive) {
                remaining.Push(sb);
            } else {
                bool shouldNotify = sb._awaitingCompletion;
                sb._awaitingCompletion = false;
                sb._keepTickingManaged = false;
                sb._runningTrackIds = new int[0];
                sb._runningTrackScopes = new UIElement[0];
                sb._pendingDiscrete = null;
                sb._managedTracks = null;
                if (shouldNotify) { finished.Push(sb); }
            }
        }
        _activeStoryboards = remaining;
        for (int k = 0; k < finished.Length; k += 1) {
            finished[k].RaiseCompleted();
        }
    }

    private void RaiseCompleted() {
        var cb = _oneShotCompletion;
        _oneShotCompletion = null;
        Completed(this, EventArgs.Empty());
        if (cb != null) { cb(); }
    }

    private void PruneDisposedScopes() {
        if (_managedTracks != null) {
            for (int i = _managedTracks.Length - 1; i >= 0; i -= 1) {
                ManagedAnimBinding mb = _managedTracks[i];
                if (mb == null || mb.Dest == null || mb.Dest._isDisposedTree || (mb.Scope != null && mb.Scope._isDisposedTree)) {
                    if (mb != null && mb.TrackId > 0) { Anim_StopTrack(mb.TrackId); }
                    _managedTracks.RemoveAt(i);
                }
            }
        }
        if (_pendingDiscrete != null) {
            for (int i = _pendingDiscrete.Length - 1; i >= 0; i -= 1) {
                PendingDiscreteApply pd = _pendingDiscrete[i];
                if (pd == null || pd.Dest == null || pd.Dest._isDisposedTree || (pd.Scope != null && pd.Scope._isDisposedTree)) {
                    if (pd != null && pd.TrackId > 0) { Anim_StopTrack(pd.TrackId); }
                    _pendingDiscrete.RemoveAt(i);
                }
            }
        }
        if (_runningTrackIds != null) {
            for (int i = _runningTrackIds.Length - 1; i >= 0; i -= 1) {
                UIElement sc = (_runningTrackScopes != null && i < _runningTrackScopes.Length) ? _runningTrackScopes[i] : null;
                if (sc != null && sc._isDisposedTree) {
                    Anim_StopTrack(_runningTrackIds[i]);
                    _runningTrackIds.RemoveAt(i);
                    _runningTrackScopes.RemoveAt(i);
                }
            }
        }
    }

    private void StopForScope(UIElement scope) {
        if (scope == null) {
            StopRunningTracksOnly();
            return;
        }
        if (_managedTracks != null) {
            for (int i = _managedTracks.Length - 1; i >= 0; i -= 1) {
                ManagedAnimBinding mb = _managedTracks[i];
                if (mb == null || mb.Scope == null || mb.Scope == scope || mb.Scope._isDisposedTree) {
                    if (mb != null && mb.TrackId > 0) { Anim_StopTrack(mb.TrackId); }
                    _managedTracks.RemoveAt(i);
                }
            }
        }
        if (_pendingDiscrete != null) {
            for (int i = _pendingDiscrete.Length - 1; i >= 0; i -= 1) {
                PendingDiscreteApply pd = _pendingDiscrete[i];
                if (pd == null || pd.Scope == null || pd.Scope == scope || pd.Scope._isDisposedTree) {
                    if (pd != null && pd.TrackId > 0) { Anim_StopTrack(pd.TrackId); }
                    _pendingDiscrete.RemoveAt(i);
                }
            }
        }
        if (_runningTrackIds != null) {
            for (int i = _runningTrackIds.Length - 1; i >= 0; i -= 1) {
                UIElement sc = (_runningTrackScopes != null && i < _runningTrackScopes.Length) ? _runningTrackScopes[i] : null;
                if (sc == null || sc == scope || sc._isDisposedTree) {
                    Anim_StopTrack(_runningTrackIds[i]);
                    _runningTrackIds.RemoveAt(i);
                    if (_runningTrackScopes != null && i < _runningTrackScopes.Length) { _runningTrackScopes.RemoveAt(i); }
                }
            }
        }
        _keepTickingManaged = (_managedTracks != null && _managedTracks.Length > 0);
    }

    private void StopRunningTracksOnly() {
        _pendingDiscrete = null;
        _managedTracks = null;
        _keepTickingManaged = false;
        if (_runningTrackIds != null && _runningTrackIds.Length > 0) {
            for (int i = 0; i < _runningTrackIds.Length; i += 1) {
                Anim_StopTrack(_runningTrackIds[i]);
            }
            _runningTrackIds = new int[0];
            _runningTrackScopes = new UIElement[0];
        }
    }

    private void BeginCore(UIElement scope, bool animatedOnly) {
        UIElement root = scope != null ? scope : Target;
        if (root == null) { return; }
        StopForScope(root);
        _lastScope = root;
        if (_runningTrackIds == null) { _runningTrackIds = new int[0]; }
        if (_runningTrackScopes == null) { _runningTrackScopes = new UIElement[0]; }
        int startedThisCall = 0;
        bool hasInfinite = RepeatForever;
        for (int i = 0; i < _children.Length; i += 1) {
            TimelineAnimation anim = _children[i];
            if (animatedOnly && !anim.HasAnimatedDuration()) { continue; }
            string savedTName = anim.TargetName;
            if (anim.TargetName.Length == 0 && TargetName.Length > 0) {
                anim.TargetName = TargetName;
            }
            if (anim.RepeatCount < 0) { hasInfinite = true; }
            int[] tids = anim.StartOnTracked(root, SpeedRatio, RepeatForever);
            anim.TargetName = savedTName;
            for (int j = 0; j < tids.Length; j += 1) {
                _runningTrackIds.Push(tids[j]);
                _runningTrackScopes.Push(root);
                startedThisCall += 1;
            }
            if (anim._lastManagedBindings != null) {
                if (_managedTracks == null) { _managedTracks = new ManagedAnimBinding[0]; }
                for (int j = 0; j < anim._lastManagedBindings.Length; j += 1) {
                    if (anim._lastManagedBindings[j] != null) {
                        anim._lastManagedBindings[j].Scope = root;
                        _managedTracks.Push(anim._lastManagedBindings[j]);
                    }
                }
            }
        }
        if (_discreteKeyFrames != null && _discreteKeyFrames.Length > 0) {
            float invSpeed = SpeedRatio > 0.001f ? (1.0f / SpeedRatio) : 1.0f;
            for (int i = 0; i < _discreteKeyFrames.Length; i += 1) {
                DiscreteKeyFrameEntry dk = _discreteKeyFrames[i];
                if (dk == null || dk.TargetProp.Length == 0) { continue; }
                string tName = dk.TargetName.Length > 0 ? dk.TargetName : TargetName;
                UIElement dest = TimelineAnimation.ResolveAnimTarget(root, tName);
                if (dest == null || dest._isDisposedTree) { continue; }
                if (dk.KeyTimeSec <= 0.0005f) {
                    dest.SetPropertyValue(dk.TargetProp, dk.ValueBox);
                } else if (ParseUtil.IsValidHandle(dest.__nativeHandle)) {
                    int tid = Anim_StartFloat(
                        dest.__nativeHandle, 23, 0.0f, 1.0f, 0.001f,
                        0, 0, dk.KeyTimeSec * invSpeed, false, 1);
                    if (tid > 0) {
                        _runningTrackIds.Push(tid);
                        _runningTrackScopes.Push(root);
                        startedThisCall += 1;
                        if (_pendingDiscrete == null) { _pendingDiscrete = new PendingDiscreteApply[0]; }
                        var pda = new PendingDiscreteApply(dest, dk.TargetProp, dk.ValueBox, tid);
                        pda.Scope = root;
                        _pendingDiscrete.Push(pda);
                    }
                }
            }
        }
        bool hasManaged = _managedTracks != null && _managedTracks.Length > 0;
        bool hasPendingDisc = _pendingDiscrete != null && _pendingDiscrete.Length > 0;
        _keepTickingManaged = hasManaged;
        if (hasInfinite) {
            if ((hasManaged || hasPendingDisc) && _runningTrackIds.Length > 0) {
                EnsureCompletionHook();
                RegisterActiveStoryboard(this);
            }
            return;
        }
        if (startedThisCall == 0 && _runningTrackIds.Length == 0) {
            RaiseCompleted();
            return;
        }
        EnsureCompletionHook();
        _awaitingCompletion = true;
        RegisterActiveStoryboard(this);
    }

    public void Begin(UIElement scope = null) {
        _oneShotCompletion = null;
        BeginCore(scope, false);
    }

    internal void BeginWithCompletion(UIElement scope, Action onDone) {
        _oneShotCompletion = onDone;
        BeginCore(scope, false);
    }

    internal void BeginAnimatedOnly(UIElement scope = null) {
        _oneShotCompletion = null;
        BeginCore(scope, true);
    }

    public void Stop(UIElement scope = null) {
        UIElement root = scope != null ? scope : Target;
        if (root == null) {
            _oneShotCompletion = null;
            _awaitingCompletion = false;
            StopRunningTracksOnly();
            return;
        }
        StopForScope(root);
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].StopOn(root);
        }
        if (_runningTrackIds == null || _runningTrackIds.Length == 0) {
            _awaitingCompletion = false;
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

    public static DependencyObject EvaluateBox(UIElement target, DependencyObject mbBox) {
        MultiBinding mb = mbBox as MultiBinding;
        if (target == null || mb == null) { return null; }
        Binding[] subs = mb.Bindings;
        int n = subs != null ? subs.Length : 0;
        var vals = new DependencyObject[n];
        for (int i = 0; i < n; i += 1) {
            vals[i] = BindingEntry.EvaluateStandaloneBinding(target, subs[i]);
        }
        if (mb.Converter != null) {
            DependencyObject pBox = null;
            if (mb.ConverterParameter.Length > 0) {
                pBox = DependencyObject.BoxString(mb.ConverterParameter);
            }
            return DependencyObject.BoxObject(mb.Converter.Convert(vals, "", pBox, ""));
        }
        return n > 0 ? vals[0] : null;
    }

    public MultiBinding() { Bindings = new Binding[0]; }
    public void AddBinding(Binding b) { if (b != null) { Bindings.Push(b); } }
}

internal class BindingEntry : DependencyObject {
    public UIElement Target;
    public Inline TargetInline;
    public Effect TargetEffect;
    public UIElement ContextHost;
    internal bool _isApplying = false;
    public string TargetProp = "";
    public int Mode = 0; // 0=OneWay, 1=TwoWay, 2=OneTime
    public int Kind = 0; // 0=Lambda, 1=Path, 2=Element, 3=TemplateParent, 4=Multi, 5=Compiled

    public Func<DependencyObject> LambdaGetter;
    public Action<DependencyObject> LambdaSetter;
    public Action<UIElement, object> CompiledUpdater;

    public DependencyObject ExplicitSourceBox;
    public UIElement SourceElement;
    public string SourcePath = "";
    public string[] PathSegments;
    public bool IsSingleHopPath = false;
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
    private DependencyObject _lastPathValBox;
    private DependencyObject _lastSrcBox;
    private UIElement[] _multiElementSources;
    private UIElement[] _multiFindRoots;

    internal void InitPathSegments() {
        if (SourcePath != null && SourcePath.Length > 0 && SourcePath != ".") {
            PathSegments = SourcePath.Split(".");
            IsSingleHopPath = (PathSegments.Length == 1);
        } else {
            PathSegments = null;
            IsSingleHopPath = true;
        }
    }

    internal static UIElement FindElementScoped(UIElement host, string elementName) {
        if (host == null || elementName == null || elementName.Length == 0) { return null; }
        if (host._internalRootName == elementName) { return host; }
        if (host.TemplatedParent != null) {
            UIElement t = host;
            while (t.Parent != null && t.Parent.TemplatedParent == host.TemplatedParent) {
                t = t.Parent;
            }
            if (t._internalRootName == elementName) { return t; }
            UIElement localHit = t.FindName(elementName);
            if (localHit != null) { return localHit; }
            if (host.TemplatedParent._internalRootName == elementName) { return host.TemplatedParent; }
        }
        UIElement cur = host;
        while (cur.Parent != null) {
            cur = cur.Parent;
            if (cur._internalRootName == elementName) { return cur; }
            if (cur.IsNamescopeBoundary()) {
                UIElement scopeHit = cur.FindName(elementName);
                if (scopeHit != null) { return scopeHit; }
                break;
            }
        }
        return null;
    }

    private static bool IsToggleButtonType(string tn) {
        return tn == "ToggleButton" || tn == "CheckBox" || tn == "RadioButton";
    }
    private static bool IsButtonBaseType(string tn) {
        return tn == "ButtonBase" || tn == "Button" || tn == "RepeatButton" || IsToggleButtonType(tn);
    }
    private static bool IsListBoxType(string tn) {
        return tn == "ListBox" || tn == "ListView";
    }
    private static bool IsSelectorType(string tn) {
        return tn == "Selector" || tn == "ComboBox" || tn == "TabControl" || IsListBoxType(tn);
    }
    private static bool IsItemsControlType(string tn) {
        return tn == "ItemsControl" || tn == "TreeView" || tn == "TreeViewItem" ||
               tn == "Menu" || tn == "MenuItem" || tn == "ContextMenu" || tn == "MenuBase" ||
               tn == "StatusBar" || tn == "ToolBar" || IsSelectorType(tn);
    }
    private static bool IsHeaderedContentControlType(string tn) {
        return tn == "HeaderedContentControl" || tn == "GroupBox" || tn == "Expander";
    }
    private static bool IsContentControlType(string tn) {
        return tn == "ContentControl" || tn == "Label" || tn == "UserControl" ||
               tn == "Page" || tn == "Window" || tn == "ListBoxItem" || tn == "ComboBoxItem" ||
               tn == "ListViewItem" || tn == "TabItem" || tn == "SelectableItem" ||
               tn == "ScrollViewer" || tn == "StatusBarItem" ||
               IsHeaderedContentControlType(tn) || IsButtonBaseType(tn);
    }
    private static bool IsRangeBaseType(string tn) {
        return tn == "RangeBase" || tn == "Slider" || tn == "ProgressBar" || tn == "ScrollBar";
    }
    private static bool IsTextBoxBaseType(string tn) {
        return tn == "TextBoxBase" || tn == "TextBox";
    }

    internal static UIElement StepAncestor(UIElement cur) {
        if (cur == null) { return null; }
        UIElement logAnc = cur.GetLogicalAncestorOverride();
        if (logAnc != null) { return logAnc; }
        return cur.Parent != null ? cur.Parent : cur.TemplatedParent;
    }

    internal static bool MatchesAncestorType(UIElement anc, string typeName) {
        if (anc == null || typeName == null || typeName.Length == 0) { return false; }
        string tn = anc.GetTypeName();
        if (tn == typeName) { return true; }
        if (typeName == "UIElement" || typeName == "FrameworkElement") { return true; }
        if (typeName == "Control") {
            return tn == "Control" || tn == "PasswordBox" || IsContentControlType(tn) ||
                   IsItemsControlType(tn) || IsRangeBaseType(tn) || IsTextBoxBaseType(tn);
        }
        if (typeName == "ContentControl") { return IsContentControlType(tn); }
        if (typeName == "HeaderedContentControl") { return IsHeaderedContentControlType(tn); }
        if (typeName == "ItemsControl") { return IsItemsControlType(tn); }
        if (typeName == "Selector") { return IsSelectorType(tn); }
        if (typeName == "ListBox") { return IsListBoxType(tn); }
        if (typeName == "ButtonBase") { return IsButtonBaseType(tn); }
        if (typeName == "ToggleButton") { return IsToggleButtonType(tn); }
        if (typeName == "RangeBase") { return IsRangeBaseType(tn); }
        if (typeName == "Panel") {
            return tn == "Panel" || tn == "StackPanel" || tn == "VirtualizingStackPanel" ||
                   tn == "TabPanel" || tn == "ToolBarPanel" || tn == "DockPanel" ||
                   tn == "WrapPanel" || tn == "ToolBarOverflowPanel" || tn == "UniformGrid" ||
                   tn == "Canvas" || tn == "Grid";
        }
        if (typeName == "Shape") {
            return tn == "Shape" || tn == "Rectangle" || tn == "Ellipse" ||
                   tn == "Line" || tn == "Path" || tn == "Polygon" || tn == "Polyline";
        }
        if (typeName == "TextBoxBase") { return IsTextBoxBaseType(tn); }
        if (typeName == "Decorator") { return tn == "Decorator" || tn == "Border" || tn == "Viewbox"; }
        return false;
    }

    private static DependencyObject ReadElementOrDcPathBox(UIElement srcEl, string path) {
        if (srcEl == null) { return null; }
        if (path.Length == 0 || path == ".") { return srcEl; }
        DependencyObject sv = srcEl.GetPropertyValue(path);
        if (sv == null) { sv = BindingEngine.ReadPathBox(srcEl, path); }
        if (sv == null && path.Length > 12 && path.Substring(0, 12) == "DataContext." && srcEl.DataContext != null) {
            sv = BindingEngine.ReadPathBox(srcEl.DataContext, path.Substring(12));
        }
        if (sv == null && srcEl.DataContext != null) { sv = BindingEngine.ReadPathBox(srcEl.DataContext, path); }
        return sv;
    }

    public static DependencyObject EvaluateStandaloneBinding(UIElement associatedObject, Binding b) {
        if (b == null || associatedObject == null) { return null; }
        DependencyObject sv = null;
        string path = b.Path == null ? "" : b.Path;
        if (b.ElementName.Length > 0) {
            UIElement se = FindElementScoped(associatedObject, b.ElementName);
            if (se == null) { se = associatedObject.FindName(b.ElementName); }
            if (se == null && associatedObject.RootElement() != null) {
                se = associatedObject.RootElement().FindName(b.ElementName);
            }
            sv = ReadElementOrDcPathBox(se, path);
        } else if (b.RelativeSourceMode == "Self") {
            sv = ReadElementOrDcPathBox(associatedObject, path);
        } else if (b.RelativeSourceMode == "TemplatedParent") {
            sv = ReadElementOrDcPathBox(associatedObject.TemplatedParent, path);
        } else if (b.RelativeSourceMode == "FindAncestor" || b.RelativeSourceMode == "AncestorType") {
            UIElement anc = associatedObject;
            while (anc != null && b.RelativeSourceAncestorType.Length > 0 && !MatchesAncestorType(anc, b.RelativeSourceAncestorType)) {
                anc = StepAncestor(anc);
            }
            sv = ReadElementOrDcPathBox(anc, path);
        } else if (b.SourceBox != null && (path.Length == 0 || path == ".")) {
            sv = b.SourceBox;
        } else {
            object dc = b.Source != null ? b.Source : associatedObject.DataContext;
            if (dc != null && path.Length > 0) {
                sv = BindingEngine.ReadPathBox(dc, path);
            }
        }
        if (sv == null && b.HasFallbackValue) { sv = b.FallbackBox; }
        if (b.Converter != null) {
            DependencyObject paramBox = b.ConverterParameter.Length > 0 ? DependencyObject.BoxString(b.ConverterParameter) : null;
            sv = DependencyObject.BoxObject(b.Converter.Convert(sv, "", paramBox, ""));
        }
        return sv;
    }

    public DependencyObject ResolveSourceBox() {
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (RelativeSourceMode == "Self") {
            return Target != null ? (DependencyObject)Target : (DependencyObject)TargetInline;
        }
        if (RelativeSourceMode == "TemplatedParent" && host != null) {
            return host.TemplatedParent;
        }
        if ((RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") && host != null) {
            UIElement anc = host;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = StepAncestor(anc);
            }
            return anc;
        }
        if (ExplicitSourceBox != null) { return ExplicitSourceBox; }
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
            if (spec == "P0" || spec == "p0") {
                return FormatFixedDecimal(fv * 100.0f, 0) + "%";
            }
            if (spec == "P1" || spec == "p1") {
                return FormatFixedDecimal(fv * 100.0f, 1) + "%";
            }
            if (spec == "P" || spec == "p" || spec == "P2" || spec == "p2") {
                return FormatFixedDecimal(fv * 100.0f, 2) + "%";
            }
            if (spec == "P3" || spec == "p3") {
                return FormatFixedDecimal(fv * 100.0f, 3) + "%";
            }
            if (spec.Length >= 2 && spec.Substring(spec.Length - 1, 1) == "%") {
                string inner = spec.Substring(0, spec.Length - 1);
                string pctNum = FormatCustomNumericSpec(DependencyObject.BoxFloat(fv * 100.0f), inner);
                if (pctNum != null) { return pctNum + "%"; }
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
        if (fmt == "G" || fmt == "g" || fmt == "00" || fmt == "F0" || fmt == "f0" ||
            fmt == "F1" || fmt == "f1" || fmt == "F2" || fmt == "f2" ||
            fmt == "D2" || fmt == "N0" || fmt == "n0" || fmt == "0" || fmt == "0.0" || fmt == "0.00" ||
            fmt == "P" || fmt == "p" || fmt == "P0" || fmt == "p0" ||
            fmt == "P1" || fmt == "p1" || fmt == "P2" || fmt == "p2" || fmt == "P3" || fmt == "p3") {
            return FormatSpecOnly(box, fmt);
        }
        if (FindSubstr(fmt, "{", 0) < 0) {
            string direct = FormatSpecOnly(box, fmt);
            string rawCheck = DependencyObject.UnboxString(box, "");
            if (direct != rawCheck) { return direct; }
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
        BindingEngine.MarkFlushDirty();
        bool prev = _isApplying;
        _isApplying = true;
        if (Target != null) {
            Target.SetPropertyValue(TargetProp, valBox);
        } else if (TargetInline != null) {
            TargetInline.SetPropertyValue(TargetProp, valBox);
        } else if (TargetEffect != null) {
            TargetEffect.SetPropertyValue(TargetProp, valBox);
        }
        _isApplying = prev;
    }

    public void Evaluate() {
        if (Target == null && TargetInline == null && TargetEffect == null) { return; }
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
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
                DependencyObject ev = ReadElementOrDcPathBox(SourceElement, SourcePath);
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
                } else if (sub.SourceBox != null && (sub.Path == null || sub.Path.Length == 0 || sub.Path == ".")) {
                    sv = sub.SourceBox;
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

        if (Kind == 5 && CompiledUpdater != null) {
            if (Target == null) { return; }
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) { return; }
            int dm = ParseUtil.IsNativeReady() ? Reflect_GetDirtyMask(srcObj) : -1;
            bool sameSrc = ItemsControl.SameItemBox(srcBox, _lastSrcBox);
            if (OneTimeDone && sameSrc && !(srcObj is UIElement) && dm == 0 && LastDirtyMask == 0) {
                return;
            }
            _lastSrcBox = srcBox;
            LastDirtyMask = dm;
            OneTimeDone = true;
            CompiledUpdater(Target, srcObj);
            BindingEngine.MarkFlushDirty();
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
            bool sameSrc = ItemsControl.SameItemBox(srcBox, _lastSrcBox);
            if (OneTimeDone && sameSrc && !(srcObj is UIElement) && IsSingleHopPath && dm == 0 && LastDirtyMask == 0) {
                return;
            }
            DependencyObject valBox = (srcObj is UIElement && IsSingleHopPath)
                ? (srcObj as UIElement).GetPropertyValue(SourcePath)
                : null;
            if (valBox == null) {
                valBox = BindingEngine.ReadSegmentsBox(srcObj, SourcePath, PathSegments);
            }
            if (valBox == null && HasFallback) { valBox = FallbackBox; }
            bool boxDiff = (valBox != _lastPathValBox) && !ItemsControl.SameItemBox(valBox, _lastPathValBox);
            if (!boxDiff && OneTimeDone && sameSrc &&
                (valBox == _lastPathValBox || valBox is BoxedInt || valBox is BoxedFloat || valBox is BoxedBool || valBox is BoxedString)) {
                LastDirtyMask = dm;
                return;
            }
            string sig = DependencyObject.UnboxString(valBox, "");
            if (!boxDiff && sig == LastHash && OneTimeDone && sameSrc) {
                return;
            }
            _lastSrcBox = srcBox;
            LastDirtyMask = dm;
            if (boxDiff || sig != LastHash || !OneTimeDone || !sameSrc) {
                _lastPathValBox = valBox;
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

public delegate void FrameTickHook(float dt);

public static class BindingEngine {
    private static BindingEntry[] _entries;
    private static Action[] _preFlushHooks;
    private static Action[] _postFlushHooks;
    private static FrameTickHook[] _frameTickHooks;
    private static int _batchDepth = 0;
    private static BindingEntry[] _batchPending;
    private static bool _flushPassDirty = false;
    private static bool _inPostFlush = false;

    internal static void MarkFlushDirty() {
        _flushPassDirty = true;
    }

    public static void BeginBatch() {
        _batchDepth += 1;
        if (_batchPending == null) { _batchPending = new BindingEntry[0]; }
    }

    public static void EndBatch() {
        if (_batchDepth <= 0) { return; }
        _batchDepth -= 1;
        if (_batchDepth > 0 || _batchPending == null || _batchPending.Length == 0) { return; }
        BindingEntry[] pending = _batchPending;
        _batchPending = new BindingEntry[0];
        for (int i = 0; i < pending.Length; i += 1) {
            BindingEntry e = pending[i];
            if (e != null && (e.Target == null || !e.Target._isDisposedTree)) {
                e.Evaluate();
            }
        }
    }

    public static void SetPreFlushHook(Action hook) {
        _preFlushHooks = new Action[0];
        if (hook != null) { _preFlushHooks.Push(hook); }
    }

    public static void AddPreFlushHook(Action hook) {
        if (hook == null) { return; }
        if (_preFlushHooks == null) { _preFlushHooks = new Action[0]; }
        _preFlushHooks.Push(hook);
    }

    public static void AddPostFlushHook(Action hook) {
        if (hook == null) { return; }
        if (_postFlushHooks == null) { _postFlushHooks = new Action[0]; }
        _postFlushHooks.Push(hook);
    }

    public static void AddFrameTickHook(FrameTickHook hook) {
        if (hook == null) { return; }
        if (_frameTickHooks == null) { _frameTickHooks = new FrameTickHook[0]; }
        _frameTickHooks.Push(hook);
    }

    public static void StepFrameTicks(float dt) {
        if (dt <= 0.0f || _frameTickHooks == null) { return; }
        for (int h = 0; h < _frameTickHooks.Length; h += 1) {
            var fn = _frameTickHooks[h];
            if (fn != null) { fn(dt); }
        }
    }

    private static bool _disposeHookInstalled = false;
    private static int _pendingDisposedCount = 0;

    internal static void OnSubtreeDisposed() {
        _pendingDisposedCount += 1;
        if (_pendingDisposedCount >= 64) {
            _pendingDisposedCount = 0;
            CleanupDisposed();
        }
    }

    private static void EnsureInit() {
        if (_entries == null) { _entries = new BindingEntry[0]; }
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { BindingEngine.OnSubtreeDisposed(); });
        }
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
        } else if (entry != null && entry.TargetInline != null) {
            ElementBindingList list = null;
            if (entry.TargetInline._ownBindingsBox is ElementBindingList) {
                list = entry.TargetInline._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.TargetInline._ownBindingsBox = list;
            }
            list.Items.Push(entry);
        }
        if (_batchDepth > 0) {
            if (_batchPending == null) { _batchPending = new BindingEntry[0]; }
            _batchPending.Push(entry);
            return;
        }
        entry.Evaluate();
    }

    public static void ClearBindings(UIElement target, string propertyName) {
        if (target == null || propertyName == null || propertyName.Length == 0) { return; }
        if (target._ownBindingsBox is ElementBindingList) {
            ElementBindingList list = target._ownBindingsBox as ElementBindingList;
            for (int i = list.Items.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = list.Items[i];
                if (e == null || (e.Target == target && e.TargetProp == propertyName)) {
                    list.Items.RemoveAt(i);
                }
            }
        }
        if (_entries != null) {
            for (int i = _entries.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = _entries[i];
                if (e == null || (e.Target == target && e.TargetProp == propertyName)) {
                    _entries.RemoveAt(i);
                }
            }
        }
    }

    public static void BindCompiledAction(
        UIElement target, string targetProp, object source, int mode, Action<UIElement, object> updater) {
        if (target == null || targetProp == null || updater == null) { return; }
        var e = new BindingEntry();
        e.Target = target;
        e.TargetProp = targetProp;
        e.Kind = 5;
        e.Mode = mode;
        e.ExplicitSourceBox = source != null ? DependencyObject.BoxObject(source) : null;
        e.CompiledUpdater = updater;
        RegisterEntry(e);
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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
        e.InitPathSegments();
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

    public static void BindEffect(Effect targetEffect, UIElement contextHost, string targetProp, Binding b) {
        if (targetEffect == null || targetProp == null || b == null) { return; }
        var e = new BindingEntry();
        e.TargetEffect = targetEffect;
        e.ContextHost = contextHost;
        e.TargetProp = targetProp;
        e.Mode = b.Mode == BindingMode.TwoWay ? 1 : (b.Mode == BindingMode.OneTime ? 2 : (b.Mode == BindingMode.OneWayToSource ? 3 : 0));
        if (b.ElementName != null && b.ElementName.Length > 0) {
            e.Kind = 2;
            e.ElementName = b.ElementName;
        } else {
            e.Kind = 1;
            e.ExplicitSourceBox = b.SourceBox;
            e.RelativeSourceMode = b.RelativeSourceMode == null ? "" : b.RelativeSourceMode;
            e.RelativeSourceAncestorType = b.RelativeSourceAncestorType == null ? "" : b.RelativeSourceAncestorType;
        }
        e.SourcePath = b.Path == null ? "" : b.Path;
        e.InitPathSegments();
        e.Converter = b.Converter;
        e.ConverterKey = b.ConverterKey == null ? "" : b.ConverterKey;
        e.ConverterParam = b.ConverterParameter == null ? "" : b.ConverterParameter;
        e.StringFormat = b.StringFormat == null ? "" : b.StringFormat;
        e.HasFallback = b.HasFallbackValue;
        e.FallbackBox = b.FallbackBox;
        e.HasTargetNull = b.HasTargetNullValue;
        e.TargetNullBox = b.TargetNullBox;
        RegisterEntry(e);
    }

    public static void NotifyTargetPropertyChanged(UIElement target, string targetProp, DependencyObject newBox) {
        if (target == null || !(target._ownBindingsBox is ElementBindingList)) { return; }
        BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
        for (int i = 0; i < own.Length; i += 1) {
            BindingEntry e = own[i];
            if (e.TargetProp == targetProp && e.Mode == 1 && !e._isApplying) {
                e.PushTwoWay(newBox);
            }
        }
    }

    public static void CleanupDisposed() {
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            BindingEntry e = _entries[i];
            if (e == null) {
                _entries.RemoveAt(i);
                continue;
            }
            bool dead = false;
            if (e.Target != null && e.Target._isDisposedTree) { dead = true; }
            else if (e.ContextHost != null && e.ContextHost._isDisposedTree) { dead = true; }
            else if (e.Target == null && e.TargetEffect == null && e.TargetInline != null &&
                (e.TargetInline.OwnerTextBlock == null || e.TargetInline.OwnerTextBlock._isDisposedTree)) {
                dead = true;
            }
            if (dead) {
                _entries.RemoveAt(i);
                continue;
            }
            if (e.SourceElement != null && e.SourceElement._isDisposedTree) {
                e.SourceElement = null;
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

    public static void FlushInlinesFor(TextBlock tb) {
        if (tb == null || tb._isDisposedTree) { return; }
        Inline[] inlines = tb.Inlines();
        if (inlines == null) { return; }
        for (int i = 0; i < inlines.Length; i += 1) {
            Inline inl = inlines[i];
            if (inl != null && inl._ownBindingsBox is ElementBindingList) {
                BindingEntry[] own = (inl._ownBindingsBox as ElementBindingList).Items;
                for (int k = 0; k < own.Length; k += 1) {
                    own[k].Evaluate();
                }
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
            if (node is TextBlock) {
                FlushInlinesFor(node as TextBlock);
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
        if (_pendingDisposedCount > 0) {
            _pendingDisposedCount = 0;
            CleanupDisposed();
        }
        if (_entries != null) {
            int write = 0;
            for (int i = 0; i < _entries.Length; i += 1) {
                BindingEntry e = _entries[i];
                if (e.Target != null && e.Target._isDisposedTree) { continue; }
                if (e.ContextHost != null && e.ContextHost._isDisposedTree) { continue; }
                if (e.Target == null && e.TargetEffect == null && e.TargetInline != null &&
                    (e.TargetInline.OwnerTextBlock == null || e.TargetInline.OwnerTextBlock._isDisposedTree)) {
                    continue;
                }
                if (write != i) { _entries[write] = e; }
                write += 1;
            }
            while (_entries.Length > write) {
                _entries.RemoveAt(_entries.Length - 1);
            }
            for (int pass = 0; pass < 3; pass += 1) {
                _flushPassDirty = false;
                for (int i = 0; i < _entries.Length; i += 1) {
                    _entries[i].Evaluate();
                }
                if (!_flushPassDirty) { break; }
            }
        }
        if (!_inPostFlush && _postFlushHooks != null && _postFlushHooks.Length > 0) {
            _inPostFlush = true;
            _flushPassDirty = false;
            for (int h = 0; h < _postFlushHooks.Length; h += 1) {
                var fn = _postFlushHooks[h];
                if (fn != null) { fn(); }
            }
            if (_flushPassDirty && _entries != null) {
                _flushPassDirty = false;
                for (int i = 0; i < _entries.Length; i += 1) {
                    _entries[i].Evaluate();
                }
            }
            _inPostFlush = false;
        }
    }

    // ---- Reflection Path Traversals ----
    internal static DependencyObject ReadSegmentsBox(object root, string path, string[] segs) {
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
        string[] parts = segs != null ? segs : path.Split(".");
        object cur = root;
        for (int i = 0; i < parts.Length - 1; i += 1) {
            string seg = parts[i];
            if (cur is UIElement) {
                DependencyObject sub = (cur as UIElement).GetPropertyValue(seg);
                cur = sub != null ? sub.UnboxObject() : Reflect_GetPropObject(cur, seg);
            } else {
                cur = Reflect_GetPropObject(cur, seg);
            }
            if (cur == null) { return null; }
        }
        string leaf = parts[parts.Length - 1];
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

    public static DependencyObject ReadPathBox(object root, string path) {
        if (root is UIElement && path != null && path.Length > 0) {
            DependencyObject direct = (root as UIElement).GetPropertyValue(path);
            if (direct != null) { return direct; }
        }
        return ReadSegmentsBox(root, path, null);
    }

    private static int TryParseBracketIndexer(string seg, string[] outProp) {
        if (seg == null || seg.Length < 3) { return -1; }
        int lb = -1;
        int rb = -1;
        for (int i = 0; i < seg.Length; i += 1) {
            string ch = seg.Substring(i, 1);
            if (ch == "[" && lb < 0) { lb = i; }
            else if (ch == "]" && lb >= 0) { rb = i; break; }
        }
        if (lb < 0 || rb <= lb + 1) { return -1; }
        outProp[0] = seg.Substring(0, lb);
        return ParseUtil.ParseInt(seg.Substring(lb + 1, rb - lb - 1), -1);
    }

    public static void WritePathBox(object root, string path, DependencyObject valBox) {
        if (root == null || path == null || path.Length == 0) { return; }
        if (root is UIElement && (root as UIElement).SetPropertyValue(path, valBox)) { return; }
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
        var propOut = new string[1];
        int listIdx = TryParseBracketIndexer(leaf, propOut);
        if (listIdx >= 0) {
            object listObj = propOut[0].Length > 0 ? Reflect_GetPropObject(cur, propOut[0]) : cur;
            if (listObj != null) {
                Reflect_ListSet(listObj, listIdx, DependencyObject.Unbox(valBox));
                MarkFlushDirty();
            }
            return;
        }
        if (valBox is BoxedBool) { Reflect_SetPropBool(cur, leaf, (valBox as BoxedBool).Value); return; }
        if (valBox is BoxedInt) { Reflect_SetPropInt(cur, leaf, (valBox as BoxedInt).Value); return; }
        if (valBox is BoxedFloat) { Reflect_SetPropFloat(cur, leaf, (valBox as BoxedFloat).Value); return; }
        if (valBox is BoxedString) { Reflect_SetPropString(cur, leaf, (valBox as BoxedString).Value); return; }
        Reflect_SetPropObject(cur, leaf, DependencyObject.Unbox(valBox));
    }
}
