module Aero.UI;
import Aero.Native;
import RealScript.Math;

// ============================================================================
// 0. XamlElementFactoryRegistry & CompiledXamlCatalog (4-Package & Build-Time Catalog)
// ============================================================================

public delegate DependencyObject XamlElementFactory();
public delegate UIElement CompiledXamlElementFactory(ResourceDictionary fallbackResources);
public delegate ResourceDictionary CompiledXamlDictFactory();

public static class XamlElementFactoryRegistry {
    private static string[] _keys;
    private static XamlElementFactory[] _factories;

    public static void Register(string typeName, XamlElementFactory factory) {
        if (typeName == null || typeName.Length == 0 || factory == null) { return; }
        if (_keys == null) {
            _keys = new string[0];
            _factories = new XamlElementFactory[0];
        }
        for (int i = 0; i < _keys.Length; i += 1) {
            if (_keys[i] == typeName) {
                _factories[i] = factory;
                return;
            }
        }
        _keys.Push(typeName);
        _factories.Push(factory);
    }

    public static DependencyObject TryCreate(string typeName) {
        if (typeName == null || typeName.Length == 0 || _keys == null) { return null; }
        for (int i = 0; i < _keys.Length; i += 1) {
            if (_keys[i] == typeName && _factories[i] != null) {
                var fn = _factories[i];
                return fn();
            }
        }
        return null;
    }

    public static void Clear() {
        _keys = null;
        _factories = null;
    }
}

public static class CompiledXamlCatalog {
    private static Dictionary<string, int> _elIndex;
    private static CompiledXamlElementFactory[] _elFactories;
    private static Dictionary<string, int> _dictIndex;
    private static CompiledXamlDictFactory[] _dictFactories;
    private static ResourceDictionary[] _dictCache;

    public static string NormalizeUriKey(string uri) {
        if (uri == null) { return ""; }
        string s = uri.Trim();
        if (s.Length == 0) { return ""; }
        if (s.Contains("\\")) {
            string[] parts = s.Split("\\");
            s = "";
            for (int i = 0; i < parts.Length; i += 1) {
                if (i > 0) { s = s + "/"; }
                s = s + parts[i];
            }
        }
        while (s.StartsWith("./")) { s = s.Substring(2); }
        while (s.StartsWith("/")) { s = s.Substring(1); }
        return s.ToLower();
    }

    private static string ExtractBaseFileName(string normKey) {
        if (normKey == null || normKey.Length == 0) { return ""; }
        int slash = normKey.LastIndexOf("/");
        if (slash >= 0 && slash + 1 < normKey.Length) {
            return normKey.Substring(slash + 1);
        }
        return normKey;
    }

    public static void RegisterElement(string uriKey, CompiledXamlElementFactory factory) {
        if (uriKey == null || factory == null) { return; }
        string k = NormalizeUriKey(uriKey);
        if (k.Length == 0) { return; }
        if (_elIndex == null) {
            _elIndex = new Dictionary<string, int>();
            _elFactories = new CompiledXamlElementFactory[0];
        }
        if (_elIndex.ContainsKey(k)) {
            int idx = _elIndex[k];
            _elFactories[idx] = factory;
            return;
        }
        int newIdx = _elFactories.Length;
        _elFactories.Push(factory);
        _elIndex[k] = newIdx;
    }

    public static void RegisterResourceDictionary(string uriKey, CompiledXamlDictFactory factory) {
        if (uriKey == null || factory == null) { return; }
        string k = NormalizeUriKey(uriKey);
        if (k.Length == 0) { return; }
        if (_dictIndex == null) {
            _dictIndex = new Dictionary<string, int>();
            _dictFactories = new CompiledXamlDictFactory[0];
            _dictCache = new ResourceDictionary[0];
        }
        if (_dictIndex.ContainsKey(k)) {
            int idx = _dictIndex[k];
            _dictFactories[idx] = factory;
            _dictCache[idx] = null;
            return;
        }
        int newIdx = _dictFactories.Length;
        _dictFactories.Push(factory);
        _dictCache.Push(null);
        _dictIndex[k] = newIdx;
    }

    private static int FindElementFactoryIndex(string uri) {
        if (_elIndex == null || uri == null) { return -1; }
        string k = NormalizeUriKey(uri);
        if (k.Length == 0) { return -1; }
        if (_elIndex.ContainsKey(k)) { return _elIndex[k]; }
        string baseName = ExtractBaseFileName(k);
        if (baseName.Length > 0 && baseName != k && _elIndex.ContainsKey(baseName)) {
            return _elIndex[baseName];
        }
        return -1;
    }

    private static int FindDictFactoryIndex(string uri) {
        if (_dictIndex == null || uri == null) { return -1; }
        string k = NormalizeUriKey(uri);
        if (k.Length == 0) { return -1; }
        if (_dictIndex.ContainsKey(k)) { return _dictIndex[k]; }
        string baseName = ExtractBaseFileName(k);
        if (baseName.Length > 0 && baseName != k && _dictIndex.ContainsKey(baseName)) {
            return _dictIndex[baseName];
        }
        return -1;
    }

    public static bool HasElement(string uriKey) { return FindElementFactoryIndex(uriKey) >= 0; }
    public static bool HasResourceDictionary(string uriKey) { return FindDictFactoryIndex(uriKey) >= 0; }
    public static int ElementCount() { return _elFactories != null ? _elFactories.Length : 0; }
    public static int ResourceDictionaryCount() { return _dictFactories != null ? _dictFactories.Length : 0; }

    public static UIElement TryLoadUri(string uri, ResourceDictionary fallbackResources = null) {
        int idx = FindElementFactoryIndex(uri);
        if (idx < 0 || _elFactories == null || idx >= _elFactories.Length || _elFactories[idx] == null) {
            return null;
        }
        var fn = _elFactories[idx];
        ResourceDictionary.PushAmbient(fallbackResources);
        UIElement._xamlBuildDepth += 1;
        BindingEngine.BeginBatch();
        UIElement el = fn(fallbackResources);
        UIElement._xamlBuildDepth -= 1;
        ResourceDictionary.PopAmbient(fallbackResources);
        if (el == null) {
            BindingEngine.EndBatch();
            return null;
        }
        el.SetUiFlag(1024, true);
        if (fallbackResources != null && el.AmbientResources == null) {
            el.AmbientResources = fallbackResources;
        }
        el.ApplyStyleSubtree();
        el.RefreshDynamicResourcesRecursive();
        BindingEngine.EndBatch();
        BindingEngine.FlushSubtree(el);
        return el;
    }

    public static ResourceDictionary TryLoadResourceDictionary(string uri) {
        int idx = FindDictFactoryIndex(uri);
        if (idx < 0 || _dictFactories == null || idx >= _dictFactories.Length || _dictFactories[idx] == null) {
            return null;
        }
        if (_dictCache != null && idx < _dictCache.Length && _dictCache[idx] != null) {
            return _dictCache[idx];
        }
        var fn = _dictFactories[idx];
        ResourceDictionary built = fn();
        if (built != null && _dictCache != null && idx < _dictCache.Length) {
            _dictCache[idx] = built;
        }
        return built;
    }

    public static void Clear() {
        _elIndex = null;
        _elFactories = null;
        _dictIndex = null;
        _dictFactories = null;
        _dictCache = null;
    }
}

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
    internal static int _globalMutationEpoch = 1;

    internal static void BumpGlobalMutationEpoch() {
        _globalMutationEpoch = _globalMutationEpoch >= 1000000000 ? 1 : (_globalMutationEpoch + 1);
    }

    public static void PushAmbient(ResourceDictionary rd) {
        if (rd == null) { return; }
        if (_ambientStack == null) { _ambientStack = new ResourceDictionary[0]; }
        _ambientStack.Push(rd);
        BumpGlobalMutationEpoch();
    }

    public static void PopAmbient(ResourceDictionary rd) {
        if (rd == null || _ambientStack == null) { return; }
        for (int i = _ambientStack.Length - 1; i >= 0; i -= 1) {
            if (_ambientStack[i] == rd) {
                _ambientStack.RemoveAt(i);
                BumpGlobalMutationEpoch();
                return;
            }
        }
    }

    public static DependencyObject FindAmbientBox(string key) {
        if (key == null || key.Length == 0 || _ambientStack == null) { return null; }
        for (int i = _ambientStack.Length - 1; i >= 0; i -= 1) {
            ResourceDictionary rd = _ambientStack[i];
            if (rd != null) {
                DependencyObject hit = rd.TryGetLocalBox(key);
                if (hit != null) { return hit; }
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

    public static object FindScopedResource(object host, string key) {
        DependencyObject ambient = FindAmbientBox(key);
        if (ambient != null) { return DependencyObject.Unbox(ambient); }
        if (host is UIElement) { return (host as UIElement).FindResource(key); }
        if (host is ResourceDictionary) {
            DependencyObject dbox = (host as ResourceDictionary).GetBox(key);
            if (dbox != null) { return DependencyObject.Unbox(dbox); }
        }
        ResourceDictionary appRes = GlobalApplicationResources();
        if (appRes != null && appRes.Contains(key)) {
            return DependencyObject.Unbox(appRes.GetBox(key));
        }
        return null;
    }

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
        BumpGlobalMutationEpoch();
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
        _keys = new string[0];
        _vals = new DependencyObject[0];
    }

    private Dictionary<string, int> _keyIndex;
    private DependencyObject[] _vals;

    public int Count() { return _keys.Length; }

    private int FindKeyIndex(string key) {
        if (_keyIndex != null) {
            return _keyIndex.GetOrDefault(key, -1);
        }
        for (int i = 0; i < _keys.Length; i += 1) {
            if (_keys[i] == key) { return i; }
        }
        return -1;
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
        int fbIdx = FindKeyIndex(key);
        if (fbIdx < 0) {
            int newIdx = _keys.Length;
            _keys.Push(key);
            _vals.Push(box);
            if (_keyIndex != null) {
                _keyIndex[key] = newIdx;
            } else if (_keys.Length >= 8) {
                _keyIndex = new Dictionary<string, int>();
                for (int i = 0; i < _keys.Length; i += 1) {
                    _keyIndex[_keys[i]] = i;
                }
            }
        } else {
            _vals[fbIdx] = box;
        }
        if (box is Style || DependencyObject.Unbox(box) is Style) {
            if (_registeredStyleKeys == null) {
                _registeredStyleKeys = new Dictionary<string, int>();
            }
            _registeredStyleKeys[key] = 1;
        }
        BumpGlobalMutationEpoch();
    }

    public void Set(string key, object val) { SetBox(key, DependencyObject.BoxObject(val)); }
    public void Set(string key, string val) { SetBox(key, DependencyObject.BoxString(val)); }
    public void Set(string key, float val) { SetBox(key, DependencyObject.BoxFloat(val)); }
    public void Set(string key, int val) { SetBox(key, DependencyObject.BoxInt(val)); }
    public void Set(string key, bool val) { SetBox(key, DependencyObject.BoxBool(val)); }

    public DependencyObject TryGetLocalBox(string key) {
        if (key == null || _keys.Length == 0) { return null; }
        int idx = FindKeyIndex(key);
        return (idx >= 0 && idx < _vals.Length) ? _vals[idx] : null;
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
        return FindKeyIndex(key) >= 0;
    }

    public bool Remove(string key) {
        if (key == null || _keys.Length == 0) { return false; }
        int idx = FindKeyIndex(key);
        if (idx < 0) { return false; }
        if (_keyIndex != null) { _keyIndex.Remove(key); }
        _keys.RemoveAt(idx);
        _vals.RemoveAt(idx);
        if (_keyIndex != null) {
            for (int i = idx; i < _keys.Length; i += 1) {
                _keyIndex[_keys[i]] = i;
            }
        }
        BumpGlobalMutationEpoch();
        return true;
    }

    public void Clear() {
        if (_keyIndex != null) { _keyIndex.Clear(); }
        _keys.Clear();
        _vals.Clear();
        BumpGlobalMutationEpoch();
    }

    public string[] Keys() { return _keys; }

    public void Merge(ResourceDictionary other) {
        if (other == null || other._keys.Length == 0) { return; }
        for (int i = 0; i < other._keys.Length; i += 1) {
            SetBox(other._keys[i], other._vals[i]);
        }
    }
    public void AddMergedDictionary(ResourceDictionary other) {
        Merge(other);
    }
}

public class ArrayCollectionSlot : CollectionSlotBase {
    public ArrayCollectionBox Target;
    public ArrayCollectionSlot(ArrayCollectionBox target) : base() { Target = target; }
    public override bool Matches(object col) {
        var o = col as ArrayCollectionBox;
        return o == Target;
    }
}

public class ArrayCollectionBox : DependencyObject, ICollectionNotifier {
    private DependencyObject[] _items;
    private CollectionSlotBase _notifierSlot;
    public ArrayCollectionBox() { _items = new DependencyObject[0]; }
    public ArrayCollectionBox(DependencyObject[] items) {
        _items = items == null ? new DependencyObject[0] : items;
    }
    public override object UnboxObject() { return this; }
    public CollectionSlotBase EnsureNotifierSlot() {
        if (_notifierSlot == null) {
            _notifierSlot = CollectionChangeHub.FindOrRegister(new ArrayCollectionSlot(this), this);
        }
        return _notifierSlot;
    }
    public void AddBox(DependencyObject b) { _items.Push(b); }
    public int ItemCount() { return _items.Length; }
    public DependencyObject BoxItemAt(int index) {
        if (index < 0 || index >= _items.Length) { return null; }
        return _items[index];
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

    public static Grid FindGridWithNamedDefinition(UIElement rootTarget, string defName) {
        if (rootTarget == null || defName == null || defName.Length == 0) { return null; }
        UIElement tplRoot = rootTarget.GetTemplateRootInternal();
        bool isTemplatedCtrl = (tplRoot != null);
        UIElement searchRoot = isTemplatedCtrl ? tplRoot : rootTarget;
        var q = new UIElement[0];
        q.Push(searchRoot);
        int idx = 0;
        while (idx < q.Length) {
            UIElement cur = q[idx];
            idx += 1;
            if (cur == null) { continue; }
            if (cur is Grid) {
                Grid g = cur as Grid;
                if (g.GetNamedDefinitionProperty(defName, "Height") != null ||
                    g.GetNamedDefinitionProperty(defName, "Width") != null) {
                    return g;
                }
            }
            if (isTemplatedCtrl && (cur is ContentPresenter || cur.IsItemsPresenterBoundary()) && cur.TemplatedParent == rootTarget) {
                continue;
            }
            for (int i = 0; i < cur.ChildCount(); i += 1) {
                UIElement ch = cur.ChildAt(i);
                if (ch == null) { continue; }
                if (isTemplatedCtrl) {
                    if (ch.IsOwnedByTemplateChainInternal(rootTarget)) { q.Push(ch); }
                } else if (ch.TemplatedParent == null) {
                    q.Push(ch);
                }
            }
        }
        return null;
    }

    public UIElement ResolveTarget(UIElement rootTarget) {
        if (rootTarget == null || Property.Length == 0) { return null; }
        return TargetName.Length > 0 ? Trigger.ResolveTemplateOrNamedElement(rootTarget, TargetName) : rootTarget;
    }

    public void ApplyTo(UIElement rootTarget) {
        ApplyToResolved(ResolveTarget(rootTarget), rootTarget);
    }

    internal void ApplyToResolved(UIElement dest, UIElement rootTarget) {
        if (dest != null) {
            if (!dest.HasUiFlag(256) && TargetName.Length == 0 && dest.HasLocalProperty(Property)) {
                return;
            }
            if (DynamicResourceKey.Length > 0) {
                dest.SetResourceReference(Property, DynamicResourceKey);
            } else if (BindingElementName.Length > 0) {
                dest.ClearResourceReference(Property);
                BindingEngine.BindElementEx(
                    dest, Property, BindingElementName, BindingPath, BindingMode,
                    BindingConverter, BindingConverterKey, BindingConverterParam, BindingStringFormat,
                    BindingHasFallback, _bindingFallbackBox);
            } else if (BindingPath.Length > 0) {
                dest.ClearResourceReference(Property);
                BindingEngine.BindPathEx(
                    dest, Property, null, BindingPath, BindingMode,
                    BindingConverter, BindingConverterKey, BindingConverterParam, BindingStringFormat,
                    BindingHasFallback, _bindingFallbackBox, false, null,
                    BindingRelativeMode, BindingAncestorType);
            } else {
                dest.ClearResourceReference(Property);
                if (Property == "ContextMenu" && _valueBox is UIElement) {
                    (_valueBox as UIElement).SetUiFlag(4096, true);
                }
                dest.SetPropertyValue(Property, _valueBox);
            }
            return;
        }
        if (TargetName.Length > 0) {
            Grid defGrid = FindGridWithNamedDefinition(rootTarget, TargetName);
            if (defGrid != null) {
                defGrid.SetNamedDefinitionProperty(TargetName, Property, _valueBox);
            }
        }
    }
}

public class TriggerBase : DependencyObject {
    protected Setter[] _setters;
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
    protected static int _sharedPropClobberedEpoch = 0;
    private static TriggerBase[] _triggersWithTargets;
    private static Setter[] _emptySetters;
    private static bool _disposeHookInstalled = false;
    internal static bool _pendingTriggerCleanup = false;



    internal static void EnsureDisposeHook() {
        if (!_disposeHookInstalled) {
            _disposeHookInstalled = true;
            UIElement.RegisterDisposeHook(() => { TriggerBase._pendingTriggerCleanup = true; });
        }
    }

    internal static void FlushPendingCleanup() {
        if (_pendingTriggerCleanup) {
            _pendingTriggerCleanup = false;
            CleanupDisposed();
        }
    }

    public TriggerBase() {}

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
        _pendingTriggerCleanup = false;
        if (_baseDests != null && _baseDests.Length > 0) {
            int bw = 0;
            for (int i = 0; i < _baseDests.Length; i += 1) {
                if (_baseDests[i] == null || _baseDests[i]._isDisposedTree) { continue; }
                if (bw != i) {
                    _baseDests[bw] = _baseDests[i];
                    _baseProps[bw] = _baseProps[i];
                    _baseBoxes[bw] = _baseBoxes[i];
                    _baseDynKeys[bw] = _baseDynKeys[i];
                    _baseRefs[bw] = _baseRefs[i];
                }
                bw += 1;
            }
            while (_baseDests.Length > bw) {
                int last = _baseDests.Length - 1;
                _baseDests.RemoveAt(last);
                _baseProps.RemoveAt(last);
                _baseBoxes.RemoveAt(last);
                _baseDynKeys.RemoveAt(last);
                _baseRefs.RemoveAt(last);
            }
        }
        if (_triggersWithTargets != null && _triggersWithTargets.Length > 0) {
            int tw = 0;
            for (int i = 0; i < _triggersWithTargets.Length; i += 1) {
                TriggerBase tr = _triggersWithTargets[i];
                if (tr == null) { continue; }
                if (tr._activeTargets != null && tr._activeTargets.Length > 0) {
                    int aw = 0;
                    for (int j = 0; j < tr._activeTargets.Length; j += 1) {
                        if (tr._activeTargets[j] != null && !tr._activeTargets[j]._isDisposedTree) {
                            if (aw != j) { tr._activeTargets[aw] = tr._activeTargets[j]; }
                            aw += 1;
                        }
                    }
                    while (tr._activeTargets.Length > aw) { tr._activeTargets.RemoveAt(tr._activeTargets.Length - 1); }
                }
                if (tr._savedOwners != null && tr._savedOwners.Length > 0) {
                    int sw = 0;
                    for (int j = 0; j < tr._savedOwners.Length; j += 1) {
                        if (tr._savedOwners[j] != null && !tr._savedOwners[j]._isDisposedTree &&
                            tr._savedDests[j] != null && !tr._savedDests[j]._isDisposedTree) {
                            if (sw != j) {
                                tr._savedOwners[sw] = tr._savedOwners[j];
                                tr._savedDests[sw] = tr._savedDests[j];
                                tr._savedProps[sw] = tr._savedProps[j];
                            }
                            sw += 1;
                        }
                    }
                    while (tr._savedOwners.Length > sw) {
                        int slast = tr._savedOwners.Length - 1;
                        tr._savedOwners.RemoveAt(slast);
                        tr._savedDests.RemoveAt(slast);
                        tr._savedProps.RemoveAt(slast);
                    }
                }
                tr.CleanupHookedTargets();
                bool hasAct = tr._activeTargets != null && tr._activeTargets.Length > 0;
                bool hasSaved = tr._savedOwners != null && tr._savedOwners.Length > 0;
                if (hasAct || hasSaved || tr.HasLiveHookedTargets()) {
                    if (tw != i) { _triggersWithTargets[tw] = tr; }
                    tw += 1;
                }
            }
            while (_triggersWithTargets.Length > tw) {
                _triggersWithTargets.RemoveAt(_triggersWithTargets.Length - 1);
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

    private static DependencyObject PopDirectBaseBox(UIElement dest, string prop) {
        if (dest == null || prop == null || _baseDests == null) { return null; }
        for (int i = _baseDests.Length - 1; i >= 0; i -= 1) {
            if (_baseDests[i] == dest && _baseProps[i] == prop) {
                DependencyObject baseBox = _baseBoxes[i];
                int nextRef = _baseRefs[i] - 1;
                if (nextRef <= 0) {
                    _baseDests.RemoveAt(i);
                    _baseProps.RemoveAt(i);
                    _baseBoxes.RemoveAt(i);
                    _baseDynKeys.RemoveAt(i);
                    _baseRefs.RemoveAt(i);
                } else {
                    _baseRefs[i] = nextRef;
                    _sharedPropClobberedEpoch = _sharedPropClobberedEpoch >= 1000000000 ? 1 : (_sharedPropClobberedEpoch + 1);
                }
                return baseBox;
            }
        }
        return null;
    }

    private static void AcquireDirectBaseBox(UIElement dest, string prop, DependencyObject curBox) {
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
        _baseBoxes.Push(curBox);
        _baseDynKeys.Push("");
        _baseRefs.Push(1);
    }

    private static void AcquireBaseValue(UIElement dest, string prop) {
        if (dest == null || prop == null || prop.Length == 0) { return; }
        dest.AddActiveTriggerProp(prop);
        BindingEngine.BumpStructEpoch();
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
        dest.RemoveActiveTriggerProp(prop);
        BindingEngine.BumpStructEpoch();
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
                    _sharedPropClobberedEpoch = _sharedPropClobberedEpoch >= 1000000000 ? 1 : (_sharedPropClobberedEpoch + 1);
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

    public Setter[] Setters() {
        if (_setters != null) { return _setters; }
        if (_emptySetters == null) { _emptySetters = new Setter[0]; }
        return _emptySetters;
    }

    public void AddSetter(Setter s) { if (s != null) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(s); } }
    public void AddSetter(string prop, object val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, string val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, float val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, int val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, bool val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetterBox(string prop, DependencyObject box, string targetName = "") {
        if (_setters == null) { _setters = new Setter[0]; }
        _setters.Push(Setter.FromBox(prop, box, targetName));
    }
    public void AddEnterAction(Storyboard sb) { if (sb != null) { if (_enterActions == null) { _enterActions = new Storyboard[0]; } _enterActions.Push(sb); } }
    public void AddExitAction(Storyboard sb) { if (sb != null) { if (_exitActions == null) { _exitActions = new Storyboard[0]; } _exitActions.Push(sb); } }
    public void AddEnterActionKey(string key) { if (key != null && key.Length > 0) { if (_enterActionKeys == null) { _enterActionKeys = new string[0]; } _enterActionKeys.Push(key); } }
    public void AddExitActionKey(string key) { if (key != null && key.Length > 0) { if (_exitActionKeys == null) { _exitActionKeys = new string[0]; } _exitActionKeys.Push(key); } }

    public virtual void Attach(UIElement target) {}

    public virtual bool IsMatched(UIElement target) { return false; }

    internal bool WasActiveFor(UIElement target) {
        if (target == null || _activeTargets == null) { return false; }
        for (int i = 0; i < _activeTargets.Length; i += 1) {
            if (_activeTargets[i] == target) { return true; }
        }
        return false;
    }

    private void SetActiveFor(UIElement target, bool active) {
        if (target == null) { return; }
        if (_activeTargets != null) {
            for (int i = _activeTargets.Length - 1; i >= 0; i -= 1) {
                if (_activeTargets[i] == target) {
                    if (!active) { _activeTargets.RemoveAt(i); }
                    return;
                }
            }
        }
        if (active) {
            if (_activeTargets == null) { _activeTargets = new UIElement[0]; }
            _activeTargets.Push(target);
            RegisterTriggerWithTargets(this);
        }
    }

    internal void DeactivateTarget(UIElement target, Style owningStyle, bool runExitActions) {
        if (target == null || !WasActiveFor(target)) { return; }
        SetActiveFor(target, false);
        if (_savedOwners != null) {
            for (int i = _savedOwners.Length - 1; i >= 0; i -= 1) {
                if (_savedOwners[i] == target) {
                    UIElement dest = _savedDests[i];
                    string prop = _savedProps[i];
                    _savedOwners.RemoveAt(i);
                    _savedDests.RemoveAt(i);
                    _savedProps.RemoveAt(i);
                    if (dest != null && prop.Length > 0) {
                        if (dest is Grid && prop.StartsWith("__GridDef:")) {
                            string rest = prop.Substring(10);
                            int sep = rest.IndexOf(":");
                            if (sep > 0) {
                                DependencyObject baseGl = PopDirectBaseBox(dest, prop);
                                if (baseGl != null) {
                                    (dest as Grid).SetNamedDefinitionProperty(rest.Substring(0, sep), rest.Substring(sep + 1), baseGl);
                                }
                            }
                        } else {
                            BindingEngine.UnbindTriggerProperty(dest, prop);
                            bool hadTrigFlag = dest.HasUiFlag(256);
                            if (!hadTrigFlag) { dest.SetUiFlag(256, true); }
                            ReleaseBaseValue(dest, prop);
                            if (!hadTrigFlag) { dest.SetUiFlag(256, false); }
                            BindingEngine.MarkFlushDirty();
                        }
                    }
                }
            }
        }
        if (runExitActions) {
            if (_exitActions != null) {
                for (int i = 0; i < _exitActions.Length; i += 1) {
                    _exitActions[i].Begin(target);
                }
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
        ApplyMatchedCore(target, owningStyle);
    }

    protected void ApplyMatchedCore(UIElement target, Style owningStyle) {
        bool wasActive = WasActiveFor(target);
        bool savePrev = !wasActive;
        if (!wasActive) {
            SetActiveFor(target, true);
        }
        if (_setters != null) {
            for (int i = 0; i < _setters.Length; i += 1) {
                Setter s = _setters[i];
                UIElement dest = s.ResolveTarget(target);
                if (savePrev) {
                    string prop = s.PropertyName();
                    if (dest != null && prop.Length > 0) {
                        AcquireBaseValue(dest, prop);
                        if (_savedOwners == null) {
                            _savedOwners = new UIElement[0];
                            _savedDests = new UIElement[0];
                            _savedProps = new string[0];
                        }
                        _savedOwners.Push(target);
                        _savedDests.Push(dest);
                        _savedProps.Push(prop);
                    } else if (s.TargetName.Length > 0 && prop.Length > 0) {
                        Grid defGrid = Setter.FindGridWithNamedDefinition(target, s.TargetName);
                        if (defGrid != null) {
                            DependencyObject curGl = defGrid.GetNamedDefinitionProperty(s.TargetName, prop);
                            string defKey = "__GridDef:" + s.TargetName + ":" + prop;
                            AcquireDirectBaseBox(defGrid, defKey, curGl);
                            if (_savedOwners == null) {
                                _savedOwners = new UIElement[0];
                                _savedDests = new UIElement[0];
                                _savedProps = new string[0];
                            }
                            _savedOwners.Push(target);
                            _savedDests.Push(defGrid);
                            _savedProps.Push(defKey);
                        }
                    }
                }
                if (wasActive && dest != null &&
                    (s.BindingElementName.Length > 0 || s.BindingPath.Length > 0) &&
                    BindingEngine.HasTriggerBinding(dest, s.PropertyName())) {
                    continue;
                }
                if (dest != null) {
                    bool hadTrigFlag = dest.HasUiFlag(256);
                    if (!hadTrigFlag) { dest.SetUiFlag(256, true); }
                    s.ApplyToResolved(dest, target);
                    if (!hadTrigFlag) { dest.SetUiFlag(256, false); }
                } else {
                    s.ApplyToResolved(null, target);
                }
            }
        }
        if (!wasActive) {
            RunEnterActions(target);
        }
    }

    protected void RunEnterActions(UIElement target) {
        if (target == null) { return; }
        if (_enterActions != null) {
            for (int i = 0; i < _enterActions.Length; i += 1) {
                _enterActions[i].Begin(target);
            }
        }
        if (_enterActionKeys != null) {
            for (int i = 0; i < _enterActionKeys.Length; i += 1) {
                target.BeginStoryboard(_enterActionKeys[i]);
            }
        }
    }

    public void Evaluate(UIElement target, Style owningStyle) {
        if (target == null) { return; }
        if (IsMatched(target)) {
            ApplyMatchedCore(target, owningStyle);
        } else if (WasActiveFor(target)) {
            DeactivateTarget(target, owningStyle, true);
        }
    }

    public static void EvaluateGroup(TriggerBase[] triggers, UIElement target, Style owningStyle) {
        if (target == null || triggers == null || triggers.Length == 0) { return; }
        for (int pass = 0; pass < 2; pass += 1) {
            bool changed = false;
            for (int i = 0; i < triggers.Length; i += 1) {
                TriggerBase tr = triggers[i];
                if (tr != null && tr.WasActiveFor(target) && !tr.IsMatched(target)) {
                    tr.DeactivateTarget(target, owningStyle, true);
                    changed = true;
                }
            }
            for (int i = 0; i < triggers.Length; i += 1) {
                TriggerBase tr = triggers[i];
                if (tr != null && tr.IsMatched(target)) {
                    if (!tr.WasActiveFor(target)) { changed = true; }
                    tr.ApplyIfMatched(target, owningStyle);
                }
            }
            if (!changed) { break; }
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
        if (target.GetTemplateRootInternal() != null) {
            UIElement tc = target.GetTemplateChildInternal(name);
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
        if (pn == "TabStripPlacement") {
            if (val == "Top") { return 0; }
            if (val == "Bottom") { return 1; }
            if (val == "Left") { return 2; }
            if (val == "Right") { return 3; }
            return -1;
        }
        if (pn == "TickPlacement") {
            if (val == "None") { return 0; }
            if (val == "TopLeft") { return 1; }
            if (val == "BottomRight") { return 2; }
            if (val == "Both") { return 3; }
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
        if (a is BoxedEnum) { oa = (a as BoxedEnum).Ordinal; }
        else if (a is BoxedInt) { oa = (a as BoxedInt).Value; }
        else if (a is BoxedString) { oa = ResolveKnownEnumOrdinal(propName, (a as BoxedString).Value); }
        if (oa < 0) { return false; }
        int ob = -1;
        if (b is BoxedEnum) { ob = (b as BoxedEnum).Ordinal; }
        else if (b is BoxedInt) { ob = (b as BoxedInt).Value; }
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
    private UIElement[] _hookedTargets;
    private UIElement[] _hookedSources;
    private static DataTrigger[] _reactiveTriggers;
    private static UIElement[] _reactiveTargets;
    private static DependencyObject[] _reactiveLastDc;
    private static int[] _reactiveLastVer;
    private static bool[] _reactiveLastMatch;
    private static UIElement[] _reactiveSrcEl;
    private static int[] _reactiveSrcTreeEpoch;
    private static int[] _reactiveSrcChainEpoch;
    private static int _lastSeenClobberEpoch = 0;
    private static int _reactiveCompactDisposeEpoch = -1;
    private static bool _reactiveHookInstalled = false;
    private static bool _reactiveAllFastEpochReady = false;
    private static int _reactiveFastNativeVmEp = -1;
    private static int _reactiveFastManagedVmEp = -1;
    private static int _reactiveFastUiPropEp = -1;
    private static int _reactiveFastLayoutEp = -1;
    private static int _reactiveFastDcEp = -1;
    private static int _reactiveFastTreeEp = -1;
    private static int _reactiveFastVisEp = -1;
    private static int _reactiveFastDispEp = -1;
    private static int _reactiveFastClobberEp = -1;
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

    private bool HasHooked(UIElement target, UIElement src) {
        if (_hookedTargets == null || _hookedSources == null) { return false; }
        for (int i = 0; i < _hookedTargets.Length; i += 1) {
            if (_hookedTargets[i] == target && _hookedSources[i] == src) { return true; }
        }
        return false;
    }

    internal override bool HasLiveHookedTargets() {
        return _hookedTargets != null && _hookedTargets.Length > 0;
    }

    internal override void CleanupHookedTargets() {
        if (_hookedTargets == null || _hookedSources == null) { return; }
        for (int i = _hookedTargets.Length - 1; i >= 0; i -= 1) {
            if (_hookedTargets[i] == null || _hookedTargets[i]._isDisposedTree ||
                _hookedSources[i] == null || _hookedSources[i]._isDisposedTree) {
                _hookedTargets.RemoveAt(i);
                _hookedSources.RemoveAt(i);
            }
        }
    }

    private bool HookRelativeSource(UIElement target) {
        if (target == null || target._isDisposedTree) { return false; }
        UIElement se = null;
        if (ElementName.Length > 0) {
            se = Trigger.ResolveTemplateOrNamedElement(target, ElementName);
        } else if (RelativeSourceMode == "TemplatedParent") {
            se = target.TemplatedParent;
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 &&
                   !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            se = anc;
        }
        if (se == null || se._isDisposedTree) { return false; }
        if (se != target && !HasHooked(target, se)) {
            if (_hookedTargets == null) {
                _hookedTargets = new UIElement[0];
                _hookedSources = new UIElement[0];
            }
            RegisterTriggerWithTargets(this);
            _hookedTargets.Push(target);
            _hookedSources.Push(se);
            se.AddStateInvalidatedHook(() => {
                if (!target._isDisposedTree) { Evaluate(target, null); }
            });
        }
        return true;
    }

    public override void Attach(UIElement target) {
        if (target == null) { return; }
        bool isRelOrNamed = ElementName.Length > 0 ||
            RelativeSourceAncestorType.Length > 0 ||
            RelativeSourceMode == "TemplatedParent" ||
            RelativeSourceMode == "FindAncestor" ||
            RelativeSourceMode == "AncestorType";
        bool hooked = HookRelativeSource(target);
        bool hasSubPath = BindingPath == "DataContext" || BindingPath.IndexOf(".") >= 0;
        if (_multiBindingBox != null || _dynamicBindingBox != null ||
            RelativeSourceMode.Length == 0 || RelativeSourceMode == "DataContext" ||
            hasSubPath || !isRelOrNamed) {
            if (RelativeSourceMode != "Self" || hasSubPath || _multiBindingBox != null || _dynamicBindingBox != null) {
                RegisterReactive(this, target);
            }
        }
        if (isRelOrNamed) {
            target.Loaded += (object s, RoutedEventArgs e) => {
                if (!HookRelativeSource(target)) {
                    RegisterReactive(this, target);
                }
                Evaluate(target, null);
            };
        }
    }

    private static void RegisterReactive(DataTrigger dt, UIElement target) {
        if (dt == null || target == null) { return; }
        if (_reactiveTriggers == null) {
            _reactiveTriggers = new DataTrigger[0];
            _reactiveTargets = new UIElement[0];
            _reactiveLastDc = new DependencyObject[0];
            _reactiveLastVer = new int[0];
            _reactiveLastMatch = new bool[0];
            _reactiveSrcEl = new UIElement[0];
            _reactiveSrcTreeEpoch = new int[0];
            _reactiveSrcChainEpoch = new int[0];
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
        _reactiveLastDc.Push(null);
        _reactiveLastVer.Push(-1);
        _reactiveLastMatch.Push(false);
        _reactiveSrcEl.Push(null);
        _reactiveSrcTreeEpoch.Push(0);
        _reactiveSrcChainEpoch.Push(0);
        _reactiveAllFastEpochReady = false;
    }

    internal bool CanUseFastDcCache() {
        return _multiBindingBox == null &&
               _dynamicBindingBox == null &&
               ElementName.Length == 0 &&
               RelativeSourceMode.Length == 0 &&
               RelativeSourceAncestorType.Length == 0 &&
               BindingPath.Length > 0 &&
               !BindingPath.Contains(".");
    }

    internal bool CanUseFastElemCache() {
        return _multiBindingBox == null &&
               _dynamicBindingBox == null &&
               (ElementName.Length > 0 || RelativeSourceMode.Length > 0 || RelativeSourceAncestorType.Length > 0) &&
               BindingPath.Length > 0 &&
               (!BindingPath.Contains(".") || UIElement.IsAttachedElementPropPath(BindingPath));
    }

    private UIElement ResolveFastElemSource(UIElement target, int idx) {
        UIElement cached = _reactiveSrcEl[idx];
        if (cached != null && !cached._isDisposedTree &&
            _reactiveSrcTreeEpoch[idx] == UIElement._treeParentEpoch &&
            _reactiveSrcChainEpoch[idx] == target._ancestorChainEpoch) {
            return cached;
        }
        UIElement se = null;
        if (ElementName.Length > 0) {
            se = Trigger.ResolveTemplateOrNamedElement(target, ElementName);
        } else if (RelativeSourceMode == "Self") {
            se = target;
        } else if (RelativeSourceMode == "TemplatedParent") {
            se = target.TemplatedParent;
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 &&
                   !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            se = anc;
        }
        if (se != null && !se._isDisposedTree) {
            _reactiveSrcEl[idx] = se;
            _reactiveSrcTreeEpoch[idx] = UIElement._treeParentEpoch;
            _reactiveSrcChainEpoch[idx] = target._ancestorChainEpoch;
        } else {
            _reactiveSrcEl[idx] = null;
        }
        return se;
    }

    internal bool HasDynamicSetterBindings() {
        if (_setters == null) { return false; }
        for (int i = 0; i < _setters.Length; i += 1) {
            Setter s = _setters[i];
            if (s.BindingElementName.Length > 0 || s.BindingPath.Length > 0) {
                return true;
            }
        }
        return false;
    }

    internal static void CleanupDisposedReactive() {
        if (_reactiveTriggers == null || _reactiveTargets == null) { return; }
        _reactiveAllFastEpochReady = false;
        for (int i = _reactiveTargets.Length - 1; i >= 0; i -= 1) {
            if (_reactiveTargets[i] == null || _reactiveTargets[i]._isDisposedTree) {
                _reactiveTriggers.RemoveAt(i);
                _reactiveTargets.RemoveAt(i);
                _reactiveLastDc.RemoveAt(i);
                _reactiveLastVer.RemoveAt(i);
                _reactiveLastMatch.RemoveAt(i);
                if (_reactiveSrcEl != null && i < _reactiveSrcEl.Length) {
                    _reactiveSrcEl.RemoveAt(i);
                }
                if (_reactiveSrcTreeEpoch != null && i < _reactiveSrcTreeEpoch.Length) {
                    _reactiveSrcTreeEpoch.RemoveAt(i);
                }
                if (_reactiveSrcChainEpoch != null && i < _reactiveSrcChainEpoch.Length) {
                    _reactiveSrcChainEpoch.RemoveAt(i);
                }
            }
        }
    }

    public static void FlushReactive() {
        if (_reactiveTriggers == null || _reactiveTargets == null || _reactiveTriggers.Length == 0) { return; }
        if (BindingEngine._inFlushPass && _reactiveAllFastEpochReady &&
            _reactiveFastNativeVmEp == BindingEngine._curNativeVmEpoch &&
            _reactiveFastManagedVmEp == BindingEngine._curManagedVmEpoch &&
            _reactiveFastUiPropEp == UIElement._globalPropEpoch &&
            _reactiveFastLayoutEp == BindingEngine._curLayoutSizeEpoch &&
            _reactiveFastDcEp == UIElement._dataContextEpoch &&
            _reactiveFastTreeEp == UIElement._treeParentEpoch &&
            _reactiveFastVisEp == UIElement._visibilityEpoch &&
            _reactiveFastDispEp == UIElement._disposeEpoch &&
            _reactiveFastClobberEp == _sharedPropClobberedEpoch) {
            return;
        }
        int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
        int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
        int snapUiPropEp = UIElement._globalPropEpoch;
        int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
        int snapDcEp = UIElement._dataContextEpoch;
        int snapTreeEp = UIElement._treeParentEpoch;
        int snapVisEp = UIElement._visibilityEpoch;
        int snapDispEp = UIElement._disposeEpoch;
        if (_reactiveSrcEl == null) {
            _reactiveSrcEl = new UIElement[0];
            _reactiveSrcTreeEpoch = new int[0];
            _reactiveSrcChainEpoch = new int[0];
        }
        while (_reactiveSrcEl.Length < _reactiveTriggers.Length) { _reactiveSrcEl.Push(null); }
        while (_reactiveSrcTreeEpoch.Length < _reactiveTriggers.Length) { _reactiveSrcTreeEpoch.Push(0); }
        while (_reactiveSrcChainEpoch.Length < _reactiveTriggers.Length) { _reactiveSrcChainEpoch.Push(0); }
        if (_reactiveCompactDisposeEpoch != UIElement._disposeEpoch) {
            _reactiveCompactDisposeEpoch = UIElement._disposeEpoch;
            int write = 0;
            for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
                UIElement t = _reactiveTargets[i];
                if (t == null || t._isDisposedTree) { continue; }
                if (write != i) {
                    _reactiveTriggers[write] = _reactiveTriggers[i];
                    _reactiveTargets[write] = t;
                    _reactiveLastDc[write] = _reactiveLastDc[i];
                    _reactiveLastVer[write] = _reactiveLastVer[i];
                    _reactiveLastMatch[write] = _reactiveLastMatch[i];
                    _reactiveSrcEl[write] = _reactiveSrcEl[i];
                    _reactiveSrcTreeEpoch[write] = _reactiveSrcTreeEpoch[i];
                    _reactiveSrcChainEpoch[write] = _reactiveSrcChainEpoch[i];
                }
                write += 1;
            }
            while (_reactiveTriggers.Length > write) {
                _reactiveTriggers.RemoveAt(_reactiveTriggers.Length - 1);
                _reactiveTargets.RemoveAt(_reactiveTargets.Length - 1);
                _reactiveLastDc.RemoveAt(_reactiveLastDc.Length - 1);
                _reactiveLastVer.RemoveAt(_reactiveLastVer.Length - 1);
                _reactiveLastMatch.RemoveAt(_reactiveLastMatch.Length - 1);
            }
            while (_reactiveSrcEl.Length > write) { _reactiveSrcEl.RemoveAt(_reactiveSrcEl.Length - 1); }
            while (_reactiveSrcTreeEpoch.Length > write) { _reactiveSrcTreeEpoch.RemoveAt(_reactiveSrcTreeEpoch.Length - 1); }
            while (_reactiveSrcChainEpoch.Length > write) { _reactiveSrcChainEpoch.RemoveAt(_reactiveSrcChainEpoch.Length - 1); }
        }
        int n = _reactiveTriggers.Length;
        if (n == 0) {
            _reactiveAllFastEpochReady = false;
            return;
        }
        int prevClobber = _lastSeenClobberEpoch;
        bool anyTransition = false;
        bool allFastVer = true;
        bool anyDynamicActive = false;
        for (int i = 0; i < n; i += 1) {
            DataTrigger dt = _reactiveTriggers[i];
            UIElement t = _reactiveTargets[i];
            if (t.IsInsideClosedPopup()) { continue; }
            if (dt.CanUseFastDcCache()) {
                DependencyObject dcBox = t.EffectiveDataContextBox();
                object dc = DependencyObject.Unbox(dcBox);
                int dm = -1;
                if (dc == null) {
                    dm = 0;
                } else if (dc is UIElement) {
                    UIElement dcEl = dc as UIElement;
                    if (BindingEntry.CanUseFastElementPropEpoch(dcEl, dt.BindingPath)) {
                        dm = BindingEntry.GetFastElementEpoch(dcEl, dt.BindingPath);
                    }
                } else if (ParseUtil.IsNativeReady()) {
                    dm = Reflect_GetDirtyMask(dc);
                }
                if (dm >= 0 && _reactiveLastVer[i] == dm && _reactiveLastDc[i] == dcBox) {
                    if (!_reactiveLastMatch[i] && dt.WasActiveFor(t)) {
                        anyTransition = true;
                        dt.DeactivateTarget(t, null, true);
                    }
                    continue;
                }
                bool matched = dt.IsMatched(t);
                _reactiveLastDc[i] = dcBox;
                _reactiveLastVer[i] = dm;
                _reactiveLastMatch[i] = matched;
                if (!matched && dt.WasActiveFor(t)) {
                    anyTransition = true;
                    dt.DeactivateTarget(t, null, true);
                }
            } else if (dt.CanUseFastElemCache()) {
                UIElement srcEl = dt.ResolveFastElemSource(t, i);
                DependencyObject srcBox = (DependencyObject)srcEl;
                int ep = (srcEl != null && BindingEntry.CanUseFastElementPropEpoch(srcEl, dt.BindingPath))
                    ? BindingEntry.GetFastElementEpoch(srcEl, dt.BindingPath)
                    : -1;
                if (ep >= 0) {
                    if (_reactiveLastVer[i] == ep && _reactiveLastDc[i] == srcBox) {
                        if (!_reactiveLastMatch[i] && dt.WasActiveFor(t)) {
                            anyTransition = true;
                            dt.DeactivateTarget(t, null, true);
                        }
                        continue;
                    }
                    bool matched = dt.IsMatched(t);
                    _reactiveLastDc[i] = srcBox;
                    _reactiveLastVer[i] = ep;
                    _reactiveLastMatch[i] = matched;
                    if (!matched && dt.WasActiveFor(t)) {
                        anyTransition = true;
                        dt.DeactivateTarget(t, null, true);
                    }
                } else {
                    _reactiveLastVer[i] = -1;
                    bool wasAct = dt.WasActiveFor(t);
                    dt.DeactivateIfUnmatched(t, null);
                    if (wasAct && !dt.WasActiveFor(t)) { anyTransition = true; }
                }
            } else {
                allFastVer = false;
                bool wasAct = dt.WasActiveFor(t);
                dt.DeactivateIfUnmatched(t, null);
                if (wasAct && !dt.WasActiveFor(t)) { anyTransition = true; }
            }
        }
        if (_sharedPropClobberedEpoch != prevClobber) {
            anyTransition = true;
            _lastSeenClobberEpoch = _sharedPropClobberedEpoch;
        }
        for (int i = 0; i < n; i += 1) {
            DataTrigger dt = _reactiveTriggers[i];
            UIElement t = _reactiveTargets[i];
            if (t.IsInsideClosedPopup()) {
                if (!dt.CanUseFastDcCache() && !dt.CanUseFastElemCache()) { allFastVer = false; }
                continue;
            }
            if (_reactiveLastVer[i] >= 0) {
                if (_reactiveLastMatch[i]) {
                    if (dt.HasDynamicSetterBindings()) { anyDynamicActive = true; }
                    if (!dt.WasActiveFor(t)) {
                        anyTransition = true;
                        dt.ApplyMatchedCore(t, null);
                    } else if (anyTransition || dt.HasDynamicSetterBindings()) {
                        dt.ApplyMatchedCore(t, null);
                    }
                }
            } else {
                allFastVer = false;
                bool wasAct = dt.WasActiveFor(t);
                dt.ApplyIfMatched(t, null);
                if (!wasAct && dt.WasActiveFor(t)) { anyTransition = true; }
            }
        }
        _lastSeenClobberEpoch = _sharedPropClobberedEpoch;
        if (allFastVer && !anyTransition && !anyDynamicActive &&
            BindingEngine._inFlushPass &&
            snapNativeVmEp == BindingEngine._curNativeVmEpoch &&
            snapManagedVmEp == BindingEngine._curManagedVmEpoch &&
            snapUiPropEp == UIElement._globalPropEpoch &&
            snapLayoutEp == BindingEngine._curLayoutSizeEpoch &&
            snapDcEp == UIElement._dataContextEpoch &&
            snapTreeEp == UIElement._treeParentEpoch &&
            snapVisEp == UIElement._visibilityEpoch &&
            snapDispEp == UIElement._disposeEpoch &&
            _sharedPropClobberedEpoch == prevClobber) {
            _reactiveAllFastEpochReady = true;
            _reactiveFastNativeVmEp = snapNativeVmEp;
            _reactiveFastManagedVmEp = snapManagedVmEp;
            _reactiveFastUiPropEp = snapUiPropEp;
            _reactiveFastLayoutEp = snapLayoutEp;
            _reactiveFastDcEp = snapDcEp;
            _reactiveFastTreeEp = snapTreeEp;
            _reactiveFastVisEp = snapVisEp;
            _reactiveFastDispEp = snapDispEp;
            _reactiveFastClobberEp = _sharedPropClobberedEpoch;
        } else {
            _reactiveAllFastEpochReady = false;
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
        } else if (RelativeSourceMode == "TemplatedParent") {
            UIElement tp = target.TemplatedParent;
            if (tp == null) { return false; }
            actual = tp.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(tp, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            if (anc == null) { return false; }
            actual = anc.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
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
        if (target == null) { return null; }
        if (ElementName.Length > 0) {
            return Trigger.ResolveTemplateOrNamedElement(target, ElementName);
        }
        if (RelativeSourceMode == "TemplatedParent") {
            return target.TemplatedParent;
        }
        if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
            RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 &&
                   !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            return anc;
        }
        return null;
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
        } else if (RelativeSourceMode == "TemplatedParent") {
            UIElement tp = target.TemplatedParent;
            if (tp == null) { return false; }
            actual = tp.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(tp, BindingPath); }
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            if (anc == null) { return false; }
            actual = anc.GetPropertyValue(BindingPath);
            if (actual == null) { actual = BindingEngine.ReadPathBox(anc, BindingPath); }
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
            if (cond == null) { continue; }
            if (cond.ElementName.Length == 0 &&
                cond.RelativeSourceMode != "TemplatedParent" &&
                cond.RelativeSourceMode != "FindAncestor" &&
                cond.RelativeSourceMode != "AncestorType" &&
                cond.RelativeSourceAncestorType.Length == 0) {
                continue;
            }
            UIElement se = cond.ResolveElementSource(target);
            if (se == null || se == target || se._isDisposedTree || HasHooked(target, se)) { continue; }
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

public interface IBehaviorTrigger {
    void Attach(UIElement associatedObject);
}

public class Style : DependencyObject {
    public string TargetType { get; set; } = "";
    private Style _basedOn;
    public Style BasedOn {
        get { return _basedOn; }
        set { _basedOn = value; _cachedResVer = -1; ResourceDictionary.BumpGlobalMutationEpoch(); }
    }
    private ResourceDictionary _resources;
    private static int _styleResourcesVersion = 0;
    private int _cachedResVer = -1;
    private bool _cachedHasAnyRes = false;
    public ResourceDictionary Resources {
        get {
            if (_resources == null) {
                _resources = new ResourceDictionary();
                ResourceDictionary.BumpGlobalMutationEpoch();
            }
            _styleResourcesVersion += 1;
            return _resources;
        }
        set {
            _resources = value;
            _styleResourcesVersion += 1;
            ResourceDictionary.BumpGlobalMutationEpoch();
        }
    }
    public bool HasResources() { return _resources != null && _resources.Count() > 0; }

    private Setter[] _setters;
    private TriggerBase[] _triggers;
    private IBehaviorTrigger[] _behaviorTriggers;
    private UIElement[] _evaluatingTargets;
    private static Setter[] _emptySetters;
    private static TriggerBase[] _emptyTriggers;

    public Style() {}
    public Style(string targetType) : this() {
        TargetType = targetType == null ? "" : targetType;
    }
    public Style(string targetType, Style basedOn) : this(targetType) {
        BasedOn = basedOn;
    }

    public Setter[] Setters() {
        if (_setters != null) { return _setters; }
        if (_emptySetters == null) { _emptySetters = new Setter[0]; }
        return _emptySetters;
    }
    public TriggerBase[] Triggers() {
        if (_triggers != null) { return _triggers; }
        if (_emptyTriggers == null) { _emptyTriggers = new TriggerBase[0]; }
        return _emptyTriggers;
    }

    public void AddSetter(Setter setter) { if (setter != null) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(setter); } }
    public void AddSetter(string propName, object value) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, string value) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, float value) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, int value) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(propName, value)); }
    public void AddSetter(string propName, bool value) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(propName, value)); }
    public void AddSetterBox(string propName, DependencyObject valueBox, string targetName = "") {
        if (_setters == null) { _setters = new Setter[0]; }
        _setters.Push(Setter.FromBox(propName, valueBox, targetName));
    }

    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { if (_triggers == null) { _triggers = new TriggerBase[0]; } _triggers.Push(trigger); } }
    public void AddDataTrigger(DataTrigger trigger) { AddTrigger(trigger); }
    public void AddEventTrigger(EventTrigger trigger) { AddTrigger(trigger); }
    public void AddBehaviorTrigger(IBehaviorTrigger trigger) {
        if (trigger != null) {
            if (_behaviorTriggers == null) { _behaviorTriggers = new IBehaviorTrigger[0]; }
            _behaviorTriggers.Push(trigger);
        }
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

    public DependencyObject TryFindStyleResourceBox(string key) {
        if (key == null || key.Length == 0) { return null; }
        if (_cachedResVer == _styleResourcesVersion && !_cachedHasAnyRes) {
            return null;
        }
        Style cur = this;
        int steps = 0;
        bool anyRes = false;
        while (cur != null && steps < 32) {
            if (cur._resources != null && cur._resources.Count() > 0) {
                anyRes = true;
                DependencyObject hit = cur._resources.TryGetLocalBox(key);
                if (hit != null) {
                    _cachedHasAnyRes = true;
                    _cachedResVer = _styleResourcesVersion;
                    return hit;
                }
            }
            cur = cur._basedOn;
            steps += 1;
        }
        _cachedHasAnyRes = anyRes;
        _cachedResVer = _styleResourcesVersion;
        return null;
    }

    internal bool HasNoStyleResourcesFast() {
        if (_cachedResVer == _styleResourcesVersion) { return !_cachedHasAnyRes; }
        Style cur = this;
        int steps = 0;
        while (cur != null && steps < 32) {
            if (cur._resources != null && cur._resources.Count() > 0) {
                _cachedHasAnyRes = true;
                _cachedResVer = _styleResourcesVersion;
                return false;
            }
            cur = cur._basedOn;
            steps += 1;
        }
        _cachedHasAnyRes = false;
        _cachedResVer = _styleResourcesVersion;
        return true;
    }

    public void ApplySettersOnly(UIElement target) {
        if (target == null) { return; }
        Style[] chain = Style.CollectChain(this);
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Setter[] ss = chain[c]._setters;
            if (ss != null) {
                for (int i = 0; i < ss.Length; i += 1) {
                    ss[i].ApplyTo(target);
                }
            }
        }
    }

    public void ApplyTo(UIElement target) {
        if (target == null) { return; }
        Style[] chain = Style.CollectChain(this);
        Setter templateSetter = null;
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Style st = chain[c];
            if (st._setters != null) {
                for (int i = 0; i < st._setters.Length; i += 1) {
                    Setter stItem = st._setters[i];
                    if (stItem.Property == "Template" && stItem.TargetName.Length == 0) {
                        templateSetter = stItem;
                    } else {
                        stItem.ApplyTo(target);
                    }
                }
            }
        }
        if (templateSetter != null) {
            templateSetter.ApplyTo(target);
        }
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Style st = chain[c];
            if (st._triggers != null) {
                for (int i = 0; i < st._triggers.Length; i += 1) {
                    st._triggers[i].Attach(target);
                }
            }
            if (st._behaviorTriggers != null) {
                for (int i = 0; i < st._behaviorTriggers.Length; i += 1) {
                    st._behaviorTriggers[i].Attach(target);
                }
            }
        }
        EvaluateTriggers(target);
    }

    public bool HasAnyTriggers() {
        Style cur = this;
        int guard = 0;
        while (cur != null && guard < 16) {
            if (cur._triggers != null && cur._triggers.Length > 0) { return true; }
            cur = cur.BasedOn;
            guard += 1;
        }
        return false;
    }

    public void EvaluateTriggers(UIElement target) {
        if (target == null || !HasAnyTriggers()) { return; }
        if (_evaluatingTargets == null) { _evaluatingTargets = new UIElement[0]; }
        for (int e = 0; e < _evaluatingTargets.Length; e += 1) {
            if (_evaluatingTargets[e] == target) { return; }
        }
        _evaluatingTargets.Push(target);
        Style[] chain = Style.CollectChain(this);
        for (int c = chain.Length - 1; c >= 0; c -= 1) {
            Style st = chain[c];
            if (st._triggers == null || st._triggers.Length == 0) { continue; }
            bool anyReverted = false;
            for (int i = 0; i < st._triggers.Length; i += 1) {
                if (st._triggers[i].WasActiveFor(target) && !st._triggers[i].IsMatched(target)) { anyReverted = true; break; }
            }
            if (anyReverted && st._setters != null) {
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
    private ResourceDictionary _resources;
    public ResourceDictionary Resources {
        get {
            if (_resources == null) {
                _resources = new ResourceDictionary();
                ResourceDictionary.BumpGlobalMutationEpoch();
            }
            return _resources;
        }
        set {
            _resources = value;
            ResourceDictionary.BumpGlobalMutationEpoch();
        }
    }
    public bool HasResources() { return _resources != null && _resources.Count() > 0; }
    public ResourceDictionary RawResourcesOrNull() { return _resources; }
    public ResourceDictionary AmbientResources { get; set; }

    private static TriggerBase[] _emptyTplTriggers;

    public ControlTemplate() {}
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
    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { if (_triggers == null) { _triggers = new TriggerBase[0]; } _triggers.Push(trigger); } }
    public TriggerBase[] Triggers() {
        if (_triggers != null) { return _triggers; }
        if (_emptyTplTriggers == null) { _emptyTplTriggers = new TriggerBase[0]; }
        return _emptyTplTriggers;
    }

    public UIElement Instantiate(Control owner) {
        if (_factory == null) { return null; }
        bool hasLocalRes = HasResources();
        ResourceDictionary.PushAmbient(AmbientResources);
        if (hasLocalRes) { ResourceDictionary.PushAmbient(_resources); }
        UIElement._xamlBuildDepth += 1;
        UIElement root = _factory(owner);
        UIElement._xamlBuildDepth -= 1;
        if (root != null) {
            if (AmbientResources != null && root.AmbientResources == null) {
                root.AmbientResources = AmbientResources;
            }
            if (hasLocalRes) {
                root.Resources.Merge(_resources);
            }
            MarkTemplatedParentSubtree(root, owner);
            if (UIElement._xamlBuildDepth == 0) {
                root.ApplyStyleSubtree();
                root.RefreshDynamicResourcesRecursive();
                BindingEngine.FlushSubtree(root);
            }
        }
        if (hasLocalRes) { ResourceDictionary.PopAmbient(_resources); }
        ResourceDictionary.PopAmbient(AmbientResources);
        return root;
    }

    public void AttachTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        for (int i = 0; i < _triggers.Length; i += 1) {
            _triggers[i].Attach(owner);
        }
    }

    public void AttachNonEventTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        for (int i = 0; i < _triggers.Length; i += 1) {
            if (!(_triggers[i] is EventTrigger)) {
                _triggers[i].Attach(owner);
            }
        }
    }

    public void AttachEventTriggers(Control owner) {
        if (owner == null || _triggers == null) { return; }
        for (int i = 0; i < _triggers.Length; i += 1) {
            if (_triggers[i] is EventTrigger) {
                _triggers[i].Attach(owner);
            }
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
            if (node.TemplatedParent == null) { node.SetTemplatedParentDirect(owner); }
            if (node is ContentPresenter || node.TemplatedParent != owner) { continue; }
            for (int i = 0; i < node.ChildCount(); i += 1) {
                q.Push(node.ChildAt(i));
            }
            if (node is ContentControl && node.TemplatedParent == owner) {
                DependencyObject cb = (node as ContentControl).ContentBox;
                if (cb is UIElement) {
                    UIElement cEl = cb as UIElement;
                    if (cEl.TemplatedParent == null && cEl != owner &&
                        !(owner is ContentControl && (owner as ContentControl).ContentBox == cb)) {
                        q.Push(cEl);
                    }
                }
            }
        }
    }
}

public class DataTemplate : DependencyObject {
    public string DataType { get; set; } = "";
    private Func<object, UIElement> _factory;
    private TriggerBase[] _triggers;
    private ResourceDictionary _resources;
    public ResourceDictionary Resources {
        get {
            if (_resources == null) {
                _resources = new ResourceDictionary();
                ResourceDictionary.BumpGlobalMutationEpoch();
            }
            return _resources;
        }
        set {
            _resources = value;
            ResourceDictionary.BumpGlobalMutationEpoch();
        }
    }
    public bool HasResources() { return _resources != null && _resources.Count() > 0; }
    public ResourceDictionary RawResourcesOrNull() { return _resources; }
    public ResourceDictionary AmbientResources { get; set; }

    public DataTemplate() {
        _triggers = new TriggerBase[0];
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

    public static string InferImplicitDataTypeKey(object raw) {
        if (raw == null) { return ""; }
        if (raw is UIElement) { return (raw as UIElement).GetTypeName(); }
        if (raw is DependencyObject) { return (raw as DependencyObject).ImplicitDataTypeKey(); }
        return "";
    }

    public static DataTemplate ResolveImplicitDataTemplate(UIElement scope, object raw) {
        if (scope == null || raw == null) { return null; }
        string dtKey = InferImplicitDataTypeKey(raw);
        if (dtKey.Length > 0) {
            DependencyObject res = scope.FindResourceBox(dtKey);
            if (res is DataTemplate) { return res as DataTemplate; }
        }
        return null;
    }

    public virtual UIElement Instantiate(object item) {
        if (_factory == null) { return null; }
        bool hasLocalRes = HasResources();
        ResourceDictionary.PushAmbient(AmbientResources);
        if (hasLocalRes) { ResourceDictionary.PushAmbient(_resources); }
        UIElement._xamlBuildDepth += 1;
        UIElement root = _factory(item);
        UIElement._xamlBuildDepth -= 1;
        if (root != null) {
            if (AmbientResources != null && root.AmbientResources == null) {
                root.AmbientResources = AmbientResources;
            }
            if (hasLocalRes) {
                root.Resources.Merge(_resources);
            }
            if (item != null) {
                root.DataContext = item;
            }
            if (UIElement._xamlBuildDepth == 0) {
                root.ApplyStyleSubtree();
                root.RefreshDynamicResourcesRecursive();
            }
            if (_triggers != null && _triggers.Length > 0) {
                AttachTriggers(root);
            }
        }
        if (hasLocalRes) { ResourceDictionary.PopAmbient(_resources); }
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

    private static Setter[] _emptySetters;

    public VisualState() {}
    public VisualState(string name) : this() { Name = name == null ? "" : name; }

    public Storyboard Storyboard {
        get { return _storyboard; }
        set { _storyboard = value; }
    }

    public void AddSetter(Setter s) { if (s != null) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(s); } }
    public void AddSetter(string prop, object val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, string val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, float val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetter(string prop, bool val) { if (_setters == null) { _setters = new Setter[0]; } _setters.Push(new Setter(prop, val)); }
    public void AddSetterBox(string propName, DependencyObject valueBox, string targetName = "") {
        if (_setters == null) { _setters = new Setter[0]; }
        _setters.Push(Setter.FromBox(propName, valueBox, targetName));
    }

    public Setter[] Setters() {
        if (_setters != null) { return _setters; }
        if (_emptySetters == null) { _emptySetters = new Setter[0]; }
        return _emptySetters;
    }

    internal void EnsureResolvedStoryboard(UIElement target) {
        if (_storyboard == null && StoryboardKey.Length > 0 && target != null) {
            DependencyObject box = target.FindResourceBox(StoryboardKey);
            if (box == null && target.TemplatedParent != null) {
                box = target.TemplatedParent.FindResourceBox(StoryboardKey);
            }
            if (box is Storyboard) { _storyboard = box as Storyboard; }
        }
        if (_storyboard != null && !_storyboardSettersExtracted) {
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
                        if ((tl.KeyFrames == null || tl.KeyFrames.Length == 0) && !tl.HasTo) { continue; }
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
        if (_setters != null) {
            for (int i = 0; i < _setters.Length; i += 1) {
                Setter s = _setters[i];
                if (s.BindingPath.Length == 0) {
                    UIElement dest = s.ResolveTarget(target);
                    string prop = s.PropertyName();
                    if (dest != null && prop.Length > 0) {
                        if (_savedTargets == null) {
                            _savedTargets = new UIElement[0];
                            _savedProps = new string[0];
                            _savedBoxes = new DependencyObject[0];
                        }
                        _savedTargets.Push(dest);
                        _savedProps.Push(prop);
                        _savedBoxes.Push(dest.GetPropertyValue(prop));
                    }
                }
                s.ApplyTo(target);
            }
        }
        StartStoryboardOnly(target);
    }

    public void Exit(UIElement target) {
        if (_savedTargets != null && _savedTargets.Length > 0) {
            for (int i = _savedTargets.Length - 1; i >= 0; i -= 1) {
                if (_savedTargets[i] != null) {
                    _savedTargets[i].SetPropertyValue(_savedProps[i], _savedBoxes[i]);
                }
            }
            _savedTargets.Clear();
            _savedProps.Clear();
            _savedBoxes.Clear();
        }
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
    private Storyboard _activeTransitionStoryboard;

    public VisualStateGroup() {}
    public VisualStateGroup(string name) : this() { Name = name == null ? "" : name; }

    public void AddState(VisualState st) { if (st != null) { if (_states == null) { _states = new VisualState[0]; } _states.Push(st); } }
    public void AddTransition(VisualTransition tr) { if (tr != null) { if (_transitions == null) { _transitions = new VisualTransition[0]; } _transitions.Push(tr); } }

    public VisualState FindState(string name) {
        if (_states == null) { return null; }
        for (int i = 0; i < _states.Length; i += 1) {
            if (_states[i].Name == name) { return _states[i]; }
        }
        return null;
    }

    private VisualTransition FindBestTransition(string fromName, string toName) {
        if (_transitions == null) { return null; }
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
        if (_baseTargets == null) {
            _baseTargets = new UIElement[0];
            _baseProps = new string[0];
            _baseBoxes = new DependencyObject[0];
        }
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
        if (_baseAnimTargets == null) {
            _baseAnimTargets = new UIElement[0];
            _baseAnimPropIds = new int[0];
            _baseAnimIsColor = new bool[0];
            _baseAnimFloats = new float[0];
            _baseAnimColors = new uint[0];
        }
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
            if (tl == null || !tl.HasAnimatedDuration() || tl.TargetProperty.Length == 0) { continue; }
            int pid = EnumConvert.ParseAnimPropOrInvalid(tl.TargetProperty);
            if (pid != propId) { continue; }
            if (TimelineAnimation.ResolveAnimTarget(scope, tl.TargetName) == dest) {
                return true;
            }
        }
        return false;
    }

    private static bool TransitionStoryboardTargets(Storyboard sb, UIElement scope, UIElement dest, int propId) {
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

        if (scope != null && (dur > 0.001f || (tr != null && tr.Storyboard != null))) {
            if (_baseTargets != null) {
                for (int i = 0; i < _baseTargets.Length; i += 1) {
                    int pid = EnumConvert.ParseAnimPropOrInvalid(_baseProps[i]);
                    TryCaptureAnimStart(_baseTargets[i], pid, IsColorAnimPropId(pid), next.Storyboard, scope,
                        animTargets, animPropIds, animIsColor, startFloats, startColors);
                }
            }
            if (_baseAnimTargets != null) {
                for (int i = 0; i < _baseAnimTargets.Length; i += 1) {
                    TryCaptureAnimStart(_baseAnimTargets[i], _baseAnimPropIds[i], _baseAnimIsColor[i], next.Storyboard, scope,
                        animTargets, animPropIds, animIsColor, startFloats, startColors);
                }
            }
        }

        if (_currentState != null) {
            _currentState.StopStoryboardOnly(scope);
        }

        if (_activeTransitionStoryboard != null) {
            _activeTransitionStoryboard.Stop(scope);
            _activeTransitionStoryboard = null;
        }
        if (_transitionTrackIds != null && _transitionTrackIds.Length > 0) {
            for (int i = 0; i < _transitionTrackIds.Length; i += 1) {
                Anim_StopTrack(_transitionTrackIds[i]);
            }
            _transitionTrackIds.Clear();
        }

        if (_baseTargets != null) {
            for (int i = _baseTargets.Length - 1; i >= 0; i -= 1) {
                if (_baseTargets[i] != null && !_baseTargets[i]._isDisposedTree) {
                    _baseTargets[i].SetPropertyValue(_baseProps[i], _baseBoxes[i]);
                }
            }
        }

        if (_baseAnimTargets != null) {
            for (int i = _baseAnimTargets.Length - 1; i >= 0; i -= 1) {
                UIElement dest = _baseAnimTargets[i];
                if (dest != null && !dest._isDisposedTree && ParseUtil.IsValidHandle(dest.__nativeHandle)) {
                    if (_baseAnimIsColor[i]) {
                        Anim_ApplyColor(dest.__nativeHandle, _baseAnimPropIds[i], _baseAnimColors[i]);
                    } else {
                        int bpid = _baseAnimPropIds[i];
                        float bfval = _baseAnimFloats[i];
                        Anim_ApplyFloat(dest.__nativeHandle, bpid, bfval);
                        if (bpid == 24) { dest.SetPropertyValue("LayoutTransform.ScaleX", DependencyObject.BoxFloat(bfval)); }
                        else if (bpid == 25) { dest.SetPropertyValue("LayoutTransform.ScaleY", DependencyObject.BoxFloat(bfval)); }
                        else if (bpid == 26) { dest.SetPropertyValue("LayoutTransform.Angle", DependencyObject.BoxFloat(bfval)); }
                    }
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

        if (scope != null && (dur > 0.001f || (tr != null && tr.Storyboard != null)) && animTargets.Length > 0) {
            int ek = tr != null ? EnumConvert.ToInt(tr.EasingKind) : 0;
            int em = tr != null ? EnumConvert.ToInt(tr.EasingMode) : 0;
            var endFloats = new float[0];
            for (int i = 0; i < animTargets.Length; i += 1) {
                UIElement dest = animTargets[i];
                int propId = animPropIds[i];
                bool handledByTrSb = (tr != null && tr.Storyboard != null) &&
                    TransitionStoryboardTargets(tr.Storyboard, scope, dest, propId);
                if (animIsColor[i]) {
                    endFloats.Push(0.0f);
                    if (!handledByTrSb && dur > 0.001f) {
                        uint startCol = startColors[i];
                        uint endCol = Anim_ReadCurrentColor(dest.__nativeHandle, propId);
                        if (endCol != startCol) {
                            Anim_ApplyColor(dest.__nativeHandle, propId, startCol);
                            int ctid = Anim_StartColorDelayed(dest.__nativeHandle, propId, startCol, endCol, dur, ek, em, 0.0f);
                            if (ctid > 0) {
                                if (tr != null) { TimelineAnimation.ApplyTrackEasing(ctid, tr.EaseParam0, tr.EaseParam1, tr.EaseParamMask); }
                                if (_transitionTrackIds == null) { _transitionTrackIds = new int[0]; }
                                _transitionTrackIds.Push(ctid);
                            }
                        }
                    }
                } else {
                    float startVal = startFloats[i];
                    float endVal = Anim_ReadCurrentFloat(dest.__nativeHandle, propId);
                    endFloats.Push(endVal);
                    if (!handledByTrSb && dur > 0.001f && Math.Absf(endVal - startVal) > 0.0005f) {
                        if (propId >= 24 && propId <= 26) { dest.EnsureLayoutTransformEnabledForAnim(); }
                        Anim_ApplyFloat(dest.__nativeHandle, propId, startVal);
                        int ftid = Anim_StartFloat(dest.__nativeHandle, propId, startVal, endVal, dur, ek, em, 0.0f, false, 1);
                        if (ftid > 0) {
                            if (tr != null) { TimelineAnimation.ApplyTrackEasing(ftid, tr.EaseParam0, tr.EaseParam1, tr.EaseParamMask); }
                            if (_transitionTrackIds == null) { _transitionTrackIds = new int[0]; }
                            _transitionTrackIds.Push(ftid);
                        }
                    }
                }
            }
            if (tr != null && tr.Storyboard != null) {
                TimelineAnimation[] trTls = tr.Storyboard.Children();
                if (trTls != null) {
                    for (int ti = 0; ti < trTls.Length; ti += 1) {
                        TimelineAnimation ttl = trTls[ti];
                        if (ttl != null && !ttl.HasFrom) {
                            int tpid = EnumConvert.ParseAnimPropOrInvalid(ttl.TargetProperty);
                            UIElement tdest = TimelineAnimation.ResolveAnimTarget(scope, ttl.TargetName);
                            if (tdest != null && tpid >= 0) {
                                for (int ai = 0; ai < animTargets.Length; ai += 1) {
                                    if (animTargets[ai] == tdest && animPropIds[ai] == tpid) {
                                        if (!ttl.IsColor && !animIsColor[ai]) {
                                            if (!ttl.HasTo) {
                                                ttl._transientToOverride = endFloats[ai];
                                                ttl._hasTransientToOverride = true;
                                            }
                                            if (tpid >= 24 && tpid <= 26) { tdest.EnsureLayoutTransformEnabledForAnim(); }
                                            Anim_ApplyFloat(tdest.__nativeHandle, tpid, startFloats[ai]);
                                        } else if (ttl.IsColor && animIsColor[ai]) {
                                            Anim_ApplyColor(tdest.__nativeHandle, tpid, startColors[ai]);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                tr.Storyboard.Begin(scope);
                _activeTransitionStoryboard = tr.Storyboard;
                if (trTls != null) {
                    for (int ti = 0; ti < trTls.Length; ti += 1) {
                        if (trTls[ti] != null) {
                            trTls[ti]._hasTransientToOverride = false;
                        }
                    }
                }
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
    internal static bool _anyNonControlVsg = false;

    public static void SetVisualStateGroups(UIElement control, VisualStateGroup[] groups) {
        if (control == null || groups == null) { return; }
        control._vsgBox = new VisualStateGroupList(groups);
        if (!(control is Control) && groups.Length > 0) {
            _anyNonControlVsg = true;
        }
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
    public bool IsDiscrete = false;

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
    public bool HasLastFloat = false;
    public float LastFloat = 0.0f;
    public bool HasLastColor = false;
    public uint LastColor = 0u;
    public bool SyncOnFinishOnly = false;
    public int NativePropId = -1;
}

public class TimelineAnimation : DependencyObject {
    public string TargetName = "";
    public string TargetProperty = "Opacity";
    public float FromFloat = 0.0f;
    public float ToFloat = 1.0f;
    public bool HasFrom = false;
    public bool HasTo = true;
    internal bool _hasTransientToOverride = false;
    internal float _transientToOverride = 0.0f;
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
    private static TimelineAnimation[] _animsWithTrackedScopes;
    private static bool _trackDisposeHookInstalled = false;

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

    internal static void CleanupDisposedTracked() {
        if (_animsWithTrackedScopes == null || _animsWithTrackedScopes.Length == 0) { return; }
        for (int i = _animsWithTrackedScopes.Length - 1; i >= 0; i -= 1) {
            TimelineAnimation ta = _animsWithTrackedScopes[i];
            if (ta == null) {
                _animsWithTrackedScopes.RemoveAt(i);
                continue;
            }
            ta._lastManagedBindings = null;
            if (ta._activeTrackIds != null) {
                for (int j = ta._activeTrackIds.Length - 1; j >= 0; j -= 1) {
                    int tid = ta._activeTrackIds[j];
                    UIElement sc = (ta._activeTrackScopes != null && j < ta._activeTrackScopes.Length)
                        ? ta._activeTrackScopes[j]
                        : null;
                    if (sc == null || sc._isDisposedTree || !Anim_IsTrackActive(tid)) {
                        if (sc != null && sc._isDisposedTree) {
                            Anim_StopTrack(tid);
                        }
                        ta._activeTrackIds.RemoveAt(j);
                        if (ta._activeTrackScopes != null && j < ta._activeTrackScopes.Length) {
                            ta._activeTrackScopes.RemoveAt(j);
                        }
                    }
                }
            }
            if (ta._activeTrackIds == null || ta._activeTrackIds.Length == 0) {
                _animsWithTrackedScopes.RemoveAt(i);
            }
        }
    }

    private int[] RecordStartedForScope(UIElement scope, UIElement dest, int propId, int[] started) {
        if (started != null && started.Length > 0) {
            if (!_trackDisposeHookInstalled) {
                _trackDisposeHookInstalled = true;
                UIElement.RegisterDisposeHook(() => { Storyboard._pendingStoryboardCleanup = true; });
            }
            if (_animsWithTrackedScopes == null) { _animsWithTrackedScopes = new TimelineAnimation[0]; }
            bool already = false;
            for (int k = 0; k < _animsWithTrackedScopes.Length; k += 1) {
                if (_animsWithTrackedScopes[k] == this) { already = true; break; }
            }
            if (!already) { _animsWithTrackedScopes.Push(this); }
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
        if (rootScope is Control && (rootScope as Control).TemplateRoot() != null) {
            UIElement tplHit = (rootScope as Control).GetTemplateChild(targetName);
            if (tplHit != null) { return tplHit; }
        }
        UIElement found = rootScope.FindName(targetName);
        if (found != null) { return found; }
        if (rootScope.TemplatedParent != null) {
            found = rootScope.TemplatedParent.FindName(targetName);
            if (found != null) { return found; }
        }
        UIElement prev = rootScope;
        UIElement cur = rootScope.Parent;
        while (cur != null) {
            found = cur.FindNameExcluding(targetName, prev);
            if (found != null) { return found; }
            prev = cur;
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
        dest.SetPropertyValue(propName, DependencyObject.BoxColor(rgba));
    }

    internal bool HasAnimatedDuration() {
        if (KeyFrames != null && KeyFrames.Length > 0) {
            if (AutoReverse || RepeatCount != 1) { return true; }
            for (int i = 0; i < KeyFrames.Length; i += 1) {
                if (KeyFrames[i] != null && (KeyFrames[i].KeyTimeSec > 0.001f || KeyFrames[i].ValueBinding != null)) {
                    return true;
                }
            }
            return false;
        }
        return DurationSec > 0.001f || AutoReverse || RepeatCount != 1 || UseSpring;
    }

    public void StartOn(UIElement rootScope) {
        StartOnTracked(rootScope, 1.0f, false, 0.0f, 0.0f);
        if (_lastManagedBindings != null && _lastManagedBindings.Length > 0) {
            Storyboard.RegisterAdHocManaged(_lastManagedBindings);
        }
        _lastManagedBindings = null;
    }

    private void PushSelfSyncTrack(UIElement dest, int tid, float endFloat, bool snapEnd) {
        if (tid > 0 && ParseUtil.IsNativeReady()) { Anim_MarkTrackScriptSync(tid); }
        if (_lastManagedBindings == null) { _lastManagedBindings = new ManagedAnimBinding[0]; }
        var mb = new ManagedAnimBinding();
        mb.TrackId = tid;
        mb.Dest = dest;
        mb.TargetProp = TargetProperty;
        mb.EndFloat = endFloat;
        mb.SnapEndOnFinish = snapEnd;
        _lastManagedBindings.Push(mb);
    }

    private void PushFinishOnlyTransformSync(UIElement dest, int tid, int propId) {
        if (tid <= 0 || dest == null || propId < 3 || propId > 9) { return; }
        if (_lastManagedBindings == null) { _lastManagedBindings = new ManagedAnimBinding[0]; }
        var mb = new ManagedAnimBinding();
        mb.TrackId = tid;
        mb.Dest = dest;
        mb.TargetProp = TargetProperty;
        mb.SyncOnFinishOnly = true;
        mb.NativePropId = propId;
        mb.SnapEndOnFinish = false;
        _lastManagedBindings.Push(mb);
    }

    private void PushManagedColorTrack(UIElement dest, int tid, int colPropId, uint endColor, bool snapEnd) {
        if (tid > 0 && ParseUtil.IsNativeReady()) { Anim_MarkTrackScriptSync(tid); }
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

    private static bool NeedsTransformSelfSync(UIElement dest) {
        if (dest == null || !(dest._ownBindingsBox is ElementBindingList)) {
            return false;
        }
        BindingEntry[] items = (dest._ownBindingsBox as ElementBindingList).Items;
        for (int i = 0; i < items.Length; i += 1) {
            BindingEntry e = items[i];
            if (e == null) { continue; }
            if (e.RelativeSourceMode == "Self") { return true; }
            string tp = e.TargetProp == null ? "" : e.TargetProp;
            if (tp == "X" || tp == "Y" || tp == "Angle" || tp == "Rotation" ||
                tp == "ScaleX" || tp == "ScaleY" || tp == "SkewX" || tp == "SkewY" ||
                tp == "TranslateX" || tp == "TranslateY" ||
                tp == "Canvas.Left" || tp == "Canvas.Top" ||
                tp.Contains("Transform")) {
                return true;
            }
        }
        return false;
    }

    internal int[] StartOnTracked(UIElement rootScope, float speedRatio, bool forceRepeatForever, float sbBeginSec = 0.0f, float sbExplicitDurSec = 0.0f) {
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
        float effDelay = (DelaySec + sbBeginSec) * invSpeed;
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
                if ((propId >= 24 && propId <= 26) || (propId >= 3 && propId <= 9 && NeedsTransformSelfSync(dest))) {
                    PushSelfSyncTrack(dest, tidSp, ToFloat, true);
                } else if (propId >= 3 && propId <= 9) {
                    PushFinishOnlyTransformSync(dest, tidSp, propId);
                }
            }
            return RecordStartedForScope(rootScope, dest, propId, started);
        }

        bool trackSelfSync = (propId < 0) || (propId >= 24 && propId <= 26) || (propId >= 3 && propId <= 9 && NeedsTransformSelfSync(dest));
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
                float holdDur = (sbExplicitDurSec > 0.001f) ? (sbExplicitDurSec * invSpeed) : 0.001f;
                int tid0 = Anim_StartFloat(
                    dest.__nativeHandle, nativeProp, firstVal, firstVal, holdDur,
                    EnumConvert.ToInt(KeyFrames[0].EasingKind), EnumConvert.ToInt(KeyFrames[0].EasingMode),
                    effDelay, AutoReverse, effRepeat);
                if (tid0 > 0) {
                    ApplyTrackEasing(tid0, KeyFrames[0].EaseParam0, KeyFrames[0].EaseParam1, KeyFrames[0].EaseParamMask);
                    started.Push(tid0);
                    if (trackSelfSync) { PushSelfSyncTrack(dest, tid0, firstVal, effRepeat > 0); }
                    else if (propId >= 3 && propId <= 9 && effRepeat > 0) { PushFinishOnlyTransformSync(dest, tid0, propId); }
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
                float maxKfSec = 0.0f;
                for (int i = 0; i < KeyFrames.Length; i += 1) {
                    DoubleKeyFrame kf = KeyFrames[i];
                    float kfVal = kf.ResolveValue(rootScope);
                    if (kf.KeyTimeSec >= maxKfSec) {
                        maxKfSec = kf.KeyTimeSec;
                        endVal = kfVal;
                    }
                    int kfMask = kf.EaseParamMask != 0
                        ? kf.EaseParamMask
                        : ((kf.EaseParam0 != 0.0f || kf.EaseParam1 != 0.0f) ? 3 : 0);
                    Anim_AddKeyFrameFloat(
                        ktid, Math.Maxf(0.0f, kf.KeyTimeSec * invSpeed), kfVal,
                        EnumConvert.ToInt(kf.EasingKind), EnumConvert.ToInt(kf.EasingMode),
                        kf.EaseParam0, kf.EaseParam1, kfMask, kf.IsDiscrete);
                }
                if (sbExplicitDurSec > maxKfSec + 0.001f) {
                    Anim_AddKeyFrameFloat(
                        ktid, sbExplicitDurSec * invSpeed, endVal,
                        0, 0, 0.0f, 0.0f, 0, true);
                }
                started.Push(ktid);
                if (trackSelfSync) {
                    float finishVal = AutoReverse ? (firstAtZero ? firstVal : startVal) : endVal;
                    PushSelfSyncTrack(dest, ktid, finishVal, effRepeat > 0);
                } else if (propId >= 3 && propId <= 9 && effRepeat > 0) {
                    PushFinishOnlyTransformSync(dest, ktid, propId);
                }
            }
            return RecordStartedForScope(rootScope, dest, propId, started);
        }

        float effToVal = _hasTransientToOverride ? _transientToOverride : ToFloat;
        if (!_hasTransientToOverride && !HasTo) {
            effToVal = propId < 0
                ? DependencyObject.UnboxFloat(dest.GetPropertyValue(TargetProperty), 0.0f)
                : Anim_ReadCurrentFloat(dest.__nativeHandle, propId);
        }
        float mainFromVal = FromFloat;
        if (propId < 0 && !HasFrom) {
            mainFromVal = DependencyObject.UnboxFloat(dest.GetPropertyValue(TargetProperty), 0.0f);
        }
        if (effDur <= 0.0005f && effDelay <= 0.0005f) {
            float snapVal = (propId < 0 && AutoReverse) ? mainFromVal : effToVal;
            dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(snapVal));
        } else if (propId < 0 && HasFrom && effDelay <= 0.0005f) {
            dest.SetPropertyValue(TargetProperty, DependencyObject.BoxFloat(mainFromVal));
        }
        int tidMain = 0;
        if (propId < 0 || HasFrom) {
            tidMain = Anim_StartFloat(dest.__nativeHandle, nativeProp, mainFromVal, effToVal, effDur, ek, em, effDelay, AutoReverse, effRepeat);
        } else {
            if (trackSelfSync && AutoReverse) { mainFromVal = Anim_ReadCurrentFloat(dest.__nativeHandle, propId); }
            tidMain = Anim_StartFloatTo(dest.__nativeHandle, propId, effToVal, effDur, ek, em, effDelay, AutoReverse, effRepeat);
        }
        if (tidMain > 0) {
            ApplyTrackEasing(tidMain, EaseParam0, EaseParam1, EaseParamMask);
            started.Push(tidMain);
            if (trackSelfSync) { PushSelfSyncTrack(dest, tidMain, AutoReverse ? mainFromVal : effToVal, effRepeat > 0); }
            else if (propId >= 3 && propId <= 9 && effRepeat > 0) { PushFinishOnlyTransformSync(dest, tidMain, propId); }
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
    public DoubleAnimationUsingKeyFrames() : base() {
        DurationSec = 0.0f;
    }
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
    public float BeginTimeSec { get; set; } = 0.0f;
    public float ExplicitDurationSec { get; set; } = 0.0f;
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

    private static TimelineAnimation[] _emptyChildren;

    public Storyboard() {}

    private void EnsureChildren() {
        if (_children == null) { _children = new TimelineAnimation[0]; }
    }

    public TimelineAnimation[] Children() {
        if (_children != null) { return _children; }
        if (_emptyChildren == null) { _emptyChildren = new TimelineAnimation[0]; }
        return _emptyChildren;
    }
    public void Add(TimelineAnimation anim) { if (anim != null) { EnsureChildren(); _children.Push(anim); } }
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
        if (kids != null && kids.Length > 0) {
            EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
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
        EnsureChildren();
        _children.Push(sa);
    }

    internal static bool _pendingStoryboardCleanup = false;

    private static void EnsureCompletionHook() {
        if (!_completionHookInstalled) {
            _completionHookInstalled = true;
            _activeStoryboards = new Storyboard[0];
            BindingEngine.AddPreFlushHook(() => { Storyboard.TickAllActive(); });
            UIElement.RegisterDisposeHook(() => { Storyboard._pendingStoryboardCleanup = true; });
        }
    }

    internal static void FlushPendingCleanup() {
        if (_pendingStoryboardCleanup) {
            _pendingStoryboardCleanup = false;
            CleanupDisposed();
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
        TimelineAnimation.CleanupDisposedTracked();
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
                    sb._lastScope = null;
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
        if (mb.SyncOnFinishOnly) {
            if (Anim_IsTrackActive(mb.TrackId)) {
                return true;
            }
            if (ParseUtil.IsNativeReady() && ParseUtil.IsValidHandle(mb.Dest.__nativeHandle)) {
                float curVal = Anim_ReadCurrentFloat(mb.Dest.__nativeHandle, mb.NativePropId);
                mb.Dest.UpdateManagedTransformScalarOnly(mb.NativePropId, curVal);
            }
            mb.TrackId = 0;
            return false;
        }
        if (Anim_IsTrackActive(mb.TrackId)) {
            if (Anim_IsTrackStarted(mb.TrackId)) {
                if (mb.IsColor) {
                    uint col = Anim_ReadTrackColor(mb.TrackId, mb.EndColor);
                    if (!mb.HasLastColor || col != mb.LastColor) {
                        mb.HasLastColor = true;
                        mb.LastColor = col;
                        TimelineAnimation.ApplyManagedColor(mb.Dest, mb.TargetProp, mb.BuiltinColorPropId, col);
                    }
                } else {
                    float val = Anim_ReadTrackFloat(mb.TrackId, mb.EndFloat);
                    if (!mb.HasLastFloat || Math.Absf(val - mb.LastFloat) > 0.00001f) {
                        mb.HasLastFloat = true;
                        mb.LastFloat = val;
                        mb.Dest.SetPropertyValue(mb.TargetProp, DependencyObject.BoxFloat(val));
                    }
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

    private int _lastPrunedDisposeEpoch = 0;

    public static void TickAllActive() {
        if (_adHocManaged != null && _adHocManaged.Length > 0) {
            int adWrite = 0;
            for (int i = 0; i < _adHocManaged.Length; i += 1) {
                ManagedAnimBinding mb = _adHocManaged[i];
                if (TickManagedBinding(mb)) {
                    if (adWrite != i) { _adHocManaged[adWrite] = mb; }
                    adWrite += 1;
                }
            }
            while (_adHocManaged.Length > adWrite) {
                _adHocManaged.RemoveAt(_adHocManaged.Length - 1);
            }
        }
        if (_activeStoryboards == null || _activeStoryboards.Length == 0) { return; }
        Storyboard[] finished = null;
        int remWrite = 0;
        for (int i = 0; i < _activeStoryboards.Length; i += 1) {
            Storyboard sb = _activeStoryboards[i];
            if (sb == null || (!sb._awaitingCompletion && !sb._keepTickingManaged)) { continue; }
            if (sb.Target != null && sb.Target._isDisposedTree) {
                sb._awaitingCompletion = false;
                sb.StopRunningTracksOnly();
                continue;
            }
            if (sb._lastPrunedDisposeEpoch != UIElement._disposeEpoch) {
                sb._lastPrunedDisposeEpoch = UIElement._disposeEpoch;
                sb.PruneDisposedScopes();
            }
            if (sb._pendingDiscrete != null && sb._pendingDiscrete.Length > 0) {
                int dWrite = 0;
                for (int d = 0; d < sb._pendingDiscrete.Length; d += 1) {
                    PendingDiscreteApply pd = sb._pendingDiscrete[d];
                    if (pd == null || pd.Applied) { continue; }
                    if (!Anim_IsTrackActive(pd.TrackId)) {
                        pd.Applied = true;
                        if (pd.Dest != null && !pd.Dest._isDisposedTree) {
                            pd.Dest.SetPropertyValue(pd.TargetProp, pd.ValueBox);
                        }
                    } else {
                        if (dWrite != d) { sb._pendingDiscrete[dWrite] = pd; }
                        dWrite += 1;
                    }
                }
                while (sb._pendingDiscrete.Length > dWrite) {
                    sb._pendingDiscrete.RemoveAt(sb._pendingDiscrete.Length - 1);
                }
                if (dWrite == 0) { sb._pendingDiscrete = null; }
            }
            bool anyManagedActive = false;
            if (sb._managedTracks != null && sb._managedTracks.Length > 0) {
                int mWrite = 0;
                for (int m = 0; m < sb._managedTracks.Length; m += 1) {
                    ManagedAnimBinding mb = sb._managedTracks[m];
                    if (TickManagedBinding(mb)) {
                        if (mWrite != m) { sb._managedTracks[mWrite] = mb; }
                        mWrite += 1;
                    }
                }
                while (sb._managedTracks.Length > mWrite) {
                    sb._managedTracks.RemoveAt(sb._managedTracks.Length - 1);
                }
                if (mWrite == 0) { sb._managedTracks = null; }
                anyManagedActive = mWrite > 0;
            }
            bool anyNativeActive = false;
            if (sb._runningTrackIds != null && sb._runningTrackIds.Length > 0) {
                bool hasScopes = sb._runningTrackScopes != null;
                if (!anyManagedActive && (sb._pendingDiscrete == null || sb._pendingDiscrete.Length == 0)) {
                    int tail = sb._runningTrackIds.Length - 1;
                    while (tail >= 0 && !Anim_IsTrackActive(sb._runningTrackIds[tail])) {
                        sb._runningTrackIds.RemoveAt(tail);
                        if (hasScopes && tail < sb._runningTrackScopes.Length) {
                            sb._runningTrackScopes.RemoveAt(tail);
                        }
                        tail -= 1;
                    }
                    anyNativeActive = tail >= 0;
                } else {
                    int tWrite = 0;
                    for (int j = 0; j < sb._runningTrackIds.Length; j += 1) {
                        int tid = sb._runningTrackIds[j];
                        if (Anim_IsTrackActive(tid)) {
                            if (tWrite != j) {
                                sb._runningTrackIds[tWrite] = tid;
                                if (hasScopes && j < sb._runningTrackScopes.Length) {
                                    sb._runningTrackScopes[tWrite] = sb._runningTrackScopes[j];
                                }
                            }
                            tWrite += 1;
                        }
                    }
                    while (sb._runningTrackIds.Length > tWrite) {
                        sb._runningTrackIds.RemoveAt(sb._runningTrackIds.Length - 1);
                    }
                    if (hasScopes) {
                        while (sb._runningTrackScopes.Length > tWrite) {
                            sb._runningTrackScopes.RemoveAt(sb._runningTrackScopes.Length - 1);
                        }
                    }
                    anyNativeActive = tWrite > 0;
                }
            }
            bool anyActive = anyManagedActive || anyNativeActive || (sb._pendingDiscrete != null && sb._pendingDiscrete.Length > 0);
            if (anyActive) {
                if (remWrite != i) { _activeStoryboards[remWrite] = sb; }
                remWrite += 1;
            } else {
                bool shouldNotify = sb._awaitingCompletion;
                sb._awaitingCompletion = false;
                sb._keepTickingManaged = false;
                if (sb._runningTrackIds != null) { sb._runningTrackIds.Clear(); }
                if (sb._runningTrackScopes != null) { sb._runningTrackScopes.Clear(); }
                sb._pendingDiscrete = null;
                sb._managedTracks = null;
                sb._lastScope = null;
                if (shouldNotify) {
                    if (finished == null) { finished = new Storyboard[0]; }
                    finished.Push(sb);
                }
            }
        }
        while (_activeStoryboards.Length > remWrite) {
            _activeStoryboards.RemoveAt(_activeStoryboards.Length - 1);
        }
        if (finished != null) {
            for (int k = 0; k < finished.Length; k += 1) {
                finished[k].RaiseCompleted();
            }
        }
    }

    private void RaiseCompleted() {
        var cb = _oneShotCompletion;
        _oneShotCompletion = null;
        Completed(this, EventArgs.Empty());
        if (cb != null) { cb(); }
    }

    private void PruneDisposedScopes() {
        if (_lastScope != null && _lastScope._isDisposedTree) { _lastScope = null; }
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
        if (_children != null) {
            for (int i = 0; i < _children.Length; i += 1) {
                TimelineAnimation anim = _children[i];
                if (animatedOnly && !anim.HasAnimatedDuration()) { continue; }
                string savedTName = anim.TargetName;
                if (anim.TargetName.Length == 0 && TargetName.Length > 0) {
                    anim.TargetName = TargetName;
                }
                if (anim.RepeatCount < 0) { hasInfinite = true; }
                int[] tids = anim.StartOnTracked(root, SpeedRatio, RepeatForever, BeginTimeSec, ExplicitDurationSec);
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
                    anim._lastManagedBindings = null;
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
            _lastScope = null;
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
            _lastScope = null;
            StopRunningTracksOnly();
            return;
        }
        StopForScope(root);
        if (_lastScope == root || (_lastScope != null && _lastScope._isDisposedTree)) {
            _lastScope = null;
        }
        if (_children != null) {
            for (int i = 0; i < _children.Length; i += 1) {
                _children[i].StopOn(root);
            }
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
    internal bool _isTriggerBinding = false;
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
    private UIElement _cachedAncestorHost;
    private UIElement _cachedAncestor;
    private int _cachedAncestorEpoch = 0;
    private int _cachedHostChainEpoch = 0;
    private bool _elemPropResolvedOnSelf = false;
    private int _lastMultiEpochHash = -1;
    private DependencyObject _lastTemplateValBox;
    private DependencyObject _lastPathValBox;
    private DependencyObject _lastSrcBox;
    private UIElement[] _multiElementSources;
    private UIElement[] _multiFindRoots;
    private int _fastPropCatCache = -1;
    private int _fastPropKindCache = 0;
    private DependencyObject _lastMidBox;
    private int _lastMidDirtyMask = -1;
    private bool _fastKind1VmClean = false;
    private bool _fastElemClean = false;
    private int _fastNativeVmEp = -1;
    private int _fastManagedVmEp = -1;
    private int _fastDcEp = -1;
    private int _fastTreeEp = -1;
    private int _fastVisEp = -1;
    private int _fastUiPropEp = -1;
    private int _fastLayoutEp = -1;
    private int _fastDispEp = -1;
    internal int TargetPropId = 0;
    internal int SourcePropId = 0;
    internal bool _inPollOnly = false;
    internal int _dirtyEnqueuedGen = 0;
    internal int _lastEvalWaveGen = 0;
    private UIElement _subscribedUiSrc;
    private int _subscribedVmToken = 0;
    private int _subscribedMidVmToken = 0;

    internal void BindUiSourceSub(UIElement el) {
        if (_subscribedUiSrc == el) { return; }
        if (_subscribedUiSrc != null) { _subscribedUiSrc.RemovePropSubscriber(this); }
        _subscribedUiSrc = el;
        if (el != null && !el._isDisposedTree) { el.AddPropSubscriber(this); }
    }

    internal void BindVmSourceSub(int tok0, int tok1) {
        if (_subscribedVmToken != tok0) {
            if (_subscribedVmToken > 0) { BindingEngine.UnsubscribeVmToken(_subscribedVmToken, this); }
            _subscribedVmToken = tok0;
            if (tok0 > 0) { BindingEngine.SubscribeVmToken(tok0, this); }
        }
        if (_subscribedMidVmToken != tok1) {
            if (_subscribedMidVmToken > 0) { BindingEngine.UnsubscribeVmToken(_subscribedMidVmToken, this); }
            _subscribedMidVmToken = tok1;
            if (tok1 > 0) { BindingEngine.SubscribeVmToken(tok1, this); }
        }
    }

    internal void ClearSourceSubscriptions() {
        BindUiSourceSub(null);
        BindVmSourceSub(0, 0);
    }

    internal bool IsDead() {
        if (Target != null && Target._isDisposedTree) { return true; }
        if (ContextHost != null && ContextHost._isDisposedTree) { return true; }
        if (Target == null && TargetEffect == null && TargetInline != null &&
            (TargetInline.OwnerTextBlock == null || TargetInline.OwnerTextBlock._isDisposedTree)) {
            return true;
        }
        return false;
    }

    internal bool IsEpochFastClean() {
        if (Target == null && TargetInline == null && TargetEffect == null) { return true; }
        if (Mode == 2 && OneTimeDone) { return true; }
        if (!OneTimeDone) { return false; }
        if (Target != null && Target.HasActiveTriggerProps()) {
            return false;
        }
        if (_fastKind1VmClean && (Kind == 1 || Kind == 5)) {
            return _subscribedVmToken > 0 &&
                   (IsSingleHopPath || _subscribedMidVmToken > 0) &&
                   BindingEngine._curNativeVmEpoch == _fastNativeVmEp &&
                   BindingEngine._curManagedVmEpoch == _fastManagedVmEp &&
                   UIElement._dataContextEpoch == _fastDcEp &&
                   UIElement._treeParentEpoch == _fastTreeEp &&
                   UIElement._visibilityEpoch == _fastVisEp &&
                   UIElement._disposeEpoch == _fastDispEp;
        }
        if (_fastElemClean) {
            return _subscribedUiSrc != null &&
                   !_subscribedUiSrc._isDisposedTree &&
                   UIElement._globalPropEpoch == _fastUiPropEp &&
                   BindingEngine._curLayoutSizeEpoch == _fastLayoutEp &&
                   UIElement._treeParentEpoch == _fastTreeEp &&
                   UIElement._dataContextEpoch == _fastDcEp &&
                   UIElement._visibilityEpoch == _fastVisEp &&
                   UIElement._disposeEpoch == _fastDispEp;
        }
        return false;
    }

    internal static bool CanUseFastElementPropEpoch(UIElement el, string prop) {
        if (el == null || prop == null || prop.Length == 0) {
            return false;
        }
        int cat = el.FastPropEpochCategory();
        if (cat == 0) {
            return false;
        }
        if (prop == "Background" || prop == "BorderBrush" || prop == "BorderThickness" ||
            prop == "CornerRadius" || prop == "Padding" || prop == "Margin" ||
            prop == "Foreground" || prop == "FontFamily" || prop == "FontSize" ||
            prop == "FontWeight" || prop == "FontStyle" ||
            prop == "Width" || prop == "Height" ||
            prop == "MinWidth" || prop == "MinHeight" ||
            prop == "MaxWidth" || prop == "MaxHeight" ||
            prop == "ActualWidth" || prop == "ActualHeight" ||
            prop == "HorizontalAlignment" || prop == "VerticalAlignment" ||
            prop == "HorizontalContentAlignment" || prop == "VerticalContentAlignment" ||
            prop == "Visibility" || prop == "Opacity" ||
            prop == "IsEnabled" || prop == "IsHitTestVisible" ||
            prop == "Focusable" || prop == "IsTabStop" ||
            prop == "IsFocused" || prop == "IsKeyboardFocused" || prop == "IsKeyboardFocusWithin" ||
            prop == "IsMouseOver" || prop == "IsPressed" ||
            prop == "Content" || prop == "Header" || prop == "Tag" ||
            prop == "AlternationIndex" || prop == "ItemsControl.AlternationIndex" ||
            prop == "ZIndex" || prop == "ClipToBounds" ||
            prop == "RenderTransform" || prop == "LayoutTransform" || prop == "RenderTransformOrigin" ||
            prop == "OpacityMask" || prop == "Effect") {
            return true;
        }
        if ((prop == "Text" || prop == "Placeholder" || prop == "PlaceholderText") &&
            (cat == 2 || cat == 6)) {
            return true;
        }
        if (prop == "IsReadOnly" && cat == 2) {
            return true;
        }
        if (prop == "IsChecked" && (cat == 3 || cat == 10)) {
            return true;
        }
        if (prop == "IsIndeterminate" && cat == 3) {
            return true;
        }
        if ((prop == "Value" || prop == "Minimum" || prop == "Maximum" || prop == "Orientation") &&
            cat == 4) {
            return true;
        }
        if ((prop == "SelectedIndex" || prop == "SelectedItem") && (cat == 5 || cat == 6)) {
            return true;
        }
        if ((prop == "IsDropDownOpen" || prop == "IsEditable") && cat == 6) { return true; }
        if (prop == "IsExpanded" && (cat == 7 || cat == 9)) { return true; }
        if (prop == "IsSelected" && (cat == 8 || cat == 9)) { return true; }
        if (prop == "IsSubmenuOpen" && cat == 10) { return true; }
        if (UIElement.IsAttachedElementPropPath(prop)) { return true; }
        return false;
    }

    internal static int GetFastElementEpoch(UIElement el, string prop) {
        if (el == null) { return -1; }
        if (prop == "ActualWidth" || prop == "ActualHeight") {
            int ep = (el._propEpoch % 100000) * 4099 + (BindingEngine._curLayoutSizeEpoch % 4099) + 1;
            return ep <= 0 ? 1 : ep;
        }
        return el._propEpoch;
    }

    private int TryGetCachedFastElementEpoch(UIElement el) {
        if (el == null) { return -1; }
        int cat = el.FastPropEpochCategory();
        if (cat == 0) { return -1; }
        if (_fastPropCatCache != cat) {
            _fastPropCatCache = cat;
            if (CanUseFastElementPropEpoch(el, SourcePath)) {
                _fastPropKindCache = (SourcePath == "ActualWidth" || SourcePath == "ActualHeight") ? 2 : 1;
            } else {
                _fastPropKindCache = -1;
            }
        }
        if (_fastPropKindCache <= 0) { return -1; }
        if (_fastPropKindCache == 2) {
            int ep = (el._propEpoch % 100000) * 4099 + (BindingEngine._curLayoutSizeEpoch % 4099) + 1;
            return ep <= 0 ? 1 : ep;
        }
        return el._propEpoch;
    }

    internal void InitPathSegments() {
        if (SourcePath != null && SourcePath.Length > 0 && SourcePath != ".") {
            if (!SourcePath.Contains(".")) {
                PathSegments = null;
                IsSingleHopPath = true;
                return;
            }
            if (UIElement.IsAttachedElementPropPath(SourcePath)) {
                PathSegments = new string[1];
                PathSegments[0] = SourcePath;
                IsSingleHopPath = true;
                return;
            }
            PathSegments = BindingEngine.SplitBindingPath(SourcePath);
            IsSingleHopPath = (PathSegments.Length == 1);
        } else {
            PathSegments = null;
            IsSingleHopPath = true;
        }
    }

    internal static UIElement FindElementScoped(UIElement host, string elementName) {
        if (host == null || elementName == null || elementName.Length == 0) { return null; }
        if (host._internalRootName == elementName || host.Name == elementName) { return host; }
        if (host.TemplatedParent != null) {
            UIElement t = host;
            while (t.Parent != null && t.Parent.TemplatedParent == host.TemplatedParent) {
                t = t.Parent;
            }
            if (t._internalRootName == elementName) { return t; }
            UIElement localHit = t.FindTemplateChildName(elementName, host.TemplatedParent);
            if (localHit != null) { return localHit; }
            if (host.TemplatedParent._internalRootName == elementName || host.TemplatedParent.Name == elementName) { return host.TemplatedParent; }
        }
        UIElement cur = host;
        while (cur.Parent != null) {
            cur = cur.Parent;
            if (cur._internalRootName == elementName || cur.Name == elementName) { return cur; }
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
        if (cur._logicalParent != null) { return cur._logicalParent; }
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
                   tn == "VirtualizingUniformGrid" || tn == "VirtualizingWrapGrid" ||
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
        if (sv != null) { return sv; }
        if (srcEl.IsExplicitNullProperty(path)) { return null; }
        sv = BindingEngine.ReadPathBox(srcEl, path);
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
            return Target != null ? (DependencyObject)Target : (TargetEffect != null ? (DependencyObject)TargetEffect : (DependencyObject)TargetInline);
        }
        if (RelativeSourceMode == "TemplatedParent" && host != null) {
            return host.TemplatedParent;
        }
        if ((RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType") && host != null) {
            if (_cachedAncestorHost == host &&
                _cachedAncestorEpoch == UIElement._treeParentEpoch &&
                _cachedHostChainEpoch == host._ancestorChainEpoch &&
                (_cachedAncestor == null || !_cachedAncestor._isDisposedTree)) {
                return _cachedAncestor;
            }
            UIElement anc = host;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = StepAncestor(anc);
            }
            _cachedAncestorHost = host;
            _cachedAncestor = anc;
            _cachedAncestorEpoch = UIElement._treeParentEpoch;
            _cachedHostChainEpoch = host._ancestorChainEpoch;
            return anc;
        }
        if (ExplicitSourceBox != null) { return ExplicitSourceBox; }
        if (Target != null && TargetProp == "DataContext" && RelativeSourceMode.Length == 0) {
            return Target.InheritedDataContextBox();
        }
        if (host != null) { return host.EffectiveDataContextBox(); }
        return null;
    }

    public IValueConverter ResolveConverter() {
        if (Converter != null) { return Converter; }
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (ConverterKey.Length > 0 && host != null) {
            DependencyObject box = host.FindResourceBox(ConverterKey);
            if (box is IValueConverter) {
                Converter = box as IValueConverter;
                return Converter;
            }
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
            if (spec == "C" || spec == "c" || spec == "C2" || spec == "c2") {
                return "$" + FormatFixedDecimal(fv, 2);
            }
            if (spec == "C0" || spec == "c0") {
                return "$" + FormatFixedDecimal(fv, 0);
            }
            if (spec == "C1" || spec == "c1") {
                return "$" + FormatFixedDecimal(fv, 1);
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
            fmt == "C" || fmt == "c" || fmt == "C0" || fmt == "c0" ||
            fmt == "C1" || fmt == "c1" || fmt == "C2" || fmt == "c2" ||
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
            if (TargetPropId > 0) {
                if (valBox is BoxedFloat && Target.TryWriteFloatById(TargetPropId, (valBox as BoxedFloat).Value)) {
                    // written via zero-dispatch float slot
                } else if (valBox is BoxedColor && Target.TryWriteColorById(TargetPropId, (valBox as BoxedColor).Value)) {
                    // written via zero-dispatch color slot
                } else {
                    Target.SetPropertyById(TargetPropId, TargetProp, valBox);
                }
            } else {
                Target.SetPropertyValue(TargetProp, valBox);
            }
        } else if (TargetInline != null) {
            TargetInline.SetPropertyValue(TargetProp, valBox);
        } else if (TargetEffect != null) {
            TargetEffect.SetPropertyValue(TargetProp, valBox);
        }
        _isApplying = prev;
    }

    private int TryComputeMultiEpochHash(UIElement host, int mbLen) {
        if (!OneTimeDone || _multiElementSources == null || _multiElementSources.Length < mbLen) {
            return -1;
        }
        int h = 17;
        for (int i = 0; i < mbLen; i += 1) {
            Binding sub = MultiBindings[i];
            string sp = sub.Path == null ? "" : sub.Path;
            int subEp = -1;
            if (sub.ElementName.Length > 0) {
                UIElement se = _multiElementSources[i];
                if (se == null || se._isDisposedTree) { return -1; }
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(se, sp)) {
                    subEp = GetFastElementEpoch(se, sp);
                } else if (sp.Length > 8 && sp.Substring(0, 8) == "Content." &&
                           !sp.Substring(8).Contains(".") && se is ContentControl) {
                    object cObj = (se as ContentControl).Content;
                    int cdm = (cObj != null && !(cObj is UIElement) && ParseUtil.IsNativeReady())
                        ? Reflect_GetDirtyMask(cObj)
                        : (cObj == null ? 0 : -1);
                    if (cdm >= 0) {
                        subEp = (se._propEpoch % 16384) * 16384 + (cdm % 16384) + 1;
                    }
                }
            } else if (sub.RelativeSourceMode == "Self" && Target != null) {
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(Target, sp)) {
                    subEp = GetFastElementEpoch(Target, sp);
                }
            } else if (sub.RelativeSourceMode == "TemplatedParent" && host != null && host.TemplatedParent != null) {
                if ((!sp.Contains(".") || UIElement.IsAttachedElementPropPath(sp)) &&
                    CanUseFastElementPropEpoch(host.TemplatedParent, sp)) {
                    subEp = GetFastElementEpoch(host.TemplatedParent, sp);
                }
            } else if (sub.ElementName.Length == 0 && sub.RelativeSourceMode.Length == 0 && sub.SourceBox == null) {
                object dc = sub.Source != null ? sub.Source : DependencyObject.Unbox(ResolveSourceBox());
                if (dc != null && !(dc is UIElement) && ParseUtil.IsNativeReady()) {
                    if (!sp.Contains(".")) {
                        subEp = Reflect_GetDirtyMask(dc);
                    } else {
                        int dot = sp.IndexOf(".");
                        if (dot > 0 && sp.Substring(dot + 1).IndexOf(".") < 0) {
                            int dm0 = Reflect_GetDirtyMask(dc);
                            if (dm0 >= 0) {
                                object mid = DependencyObject.Unbox(BindingEngine.ReadPathBox(dc, sp.Substring(0, dot)));
                                int dm1 = (mid != null && !(mid is UIElement))
                                    ? Reflect_GetDirtyMask(mid)
                                    : (mid == null ? 0 : -1);
                                if (dm1 >= 0) {
                                    subEp = (dm0 % 16384) * 16384 + (dm1 % 16384) + 1;
                                }
                            }
                        }
                    }
                }
            }
            if (subEp < 0) { return -1; }
            h = ((h * 31) + (subEp % 1000000)) % 10000007;
            if (h <= 0) { h = 1; }
        }
        return h;
    }

    public void Evaluate() {
        if (Target == null && TargetInline == null && TargetEffect == null) { return; }
        if (Mode == 2 && OneTimeDone) { return; }
        if (BindingEngine._inFlushPass && !BindingEngine._inSubtreeFlush && !BindingEngine._inPushWave && OneTimeDone &&
            (Target == null || !Target.HasActiveTriggerProps())) {
            if (_fastKind1VmClean && (Kind == 1 || Kind == 5) &&
                BindingEngine._curNativeVmEpoch == _fastNativeVmEp &&
                BindingEngine._curManagedVmEpoch == _fastManagedVmEp &&
                UIElement._dataContextEpoch == _fastDcEp &&
                UIElement._treeParentEpoch == _fastTreeEp &&
                UIElement._visibilityEpoch == _fastVisEp &&
                UIElement._disposeEpoch == _fastDispEp) {
                return;
            }
            if (_fastElemClean &&
                UIElement._globalPropEpoch == _fastUiPropEp &&
                BindingEngine._curLayoutSizeEpoch == _fastLayoutEp &&
                UIElement._treeParentEpoch == _fastTreeEp &&
                UIElement._dataContextEpoch == _fastDcEp &&
                UIElement._visibilityEpoch == _fastVisEp &&
                UIElement._disposeEpoch == _fastDispEp) {
                return;
            }
        }
        UIElement host = Target != null ? Target : (ContextHost != null ? ContextHost : (TargetInline != null ? TargetInline.OwnerTextBlock : null));
        if (!BindingEngine._inSubtreeFlush && host != null && host.IsInsideClosedPopup() && TargetProp != "IsOpen") {
            return;
        }
        if (!BindingEngine._inSubtreeFlush && OneTimeDone && !_isTriggerBinding && host != null &&
            TargetProp != "Visibility" && TargetProp != "IsVisible" &&
            TargetProp != "IsSelected" && TargetProp != "IsChecked" && TargetProp != "IsOpen" &&
            host.IsInsideCollapsedSubtree()) {
            return;
        }
        if (!_isTriggerBinding && Target != null && Target.HasActiveTriggerProp(TargetProp)) {
            return;
        }

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
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            if (SourceElement != null && SourceElement._isDisposedTree) {
                ClearSourceSubscriptions();
                SourceElement = null;
                _lastFindNameRoot = null;
                LastDirtyMask = -1;
                _elemPropResolvedOnSelf = false;
                _fastElemClean = false;
            }
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
                int elemDm = IsSingleHopPath ? TryGetCachedFastElementEpoch(SourceElement) : -1;
                bool canFastElem = elemDm >= 0;
                if (OneTimeDone && _elemPropResolvedOnSelf && elemDm >= 0 && elemDm == LastDirtyMask) {
                    if (IsSingleHopPath) {
                        MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                        if (_subscribedUiSrc != SourceElement || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                            BindUiSourceSub(SourceElement);
                            BindVmSourceSub(0, 0);
                        }
                    } else {
                        _fastElemClean = false;
                        ClearSourceSubscriptions();
                    }
                    return;
                }
                DependencyObject selfVal = canFastElem
                    ? (SourcePropId > 0 ? SourceElement.GetPropertyById(SourcePropId, SourcePath) : SourceElement.GetPropertyValue(SourcePath))
                    : null;
                bool explicitNull = (selfVal == null) && SourceElement.IsExplicitNullProperty(SourcePath);
                _elemPropResolvedOnSelf = canFastElem && (selfVal != null || explicitNull || SourceElement.DataContext == null);
                DependencyObject ev = (selfVal != null || explicitNull)
                    ? selfVal
                    : ReadElementOrDcPathBox(SourceElement, SourcePath);
                if (ev == null && !explicitNull && HasFallback) { ev = FallbackBox; }
                bool isDirectUi = ev != null && (ev.UnboxObject() is UIElement);
                bool colorSame = OneTimeDone && (ev is BoxedColor) && (_lastTemplateValBox is BoxedColor) &&
                                 ((ev as BoxedColor).Value == (_lastTemplateValBox as BoxedColor).Value);
                bool boxDiff = (ev is Brush || ev is UIElement || _lastTemplateValBox is Brush || _lastTemplateValBox is UIElement)
                    && (ev != _lastTemplateValBox);
                LastDirtyMask = _elemPropResolvedOnSelf ? elemDm : -1;
                if (!colorSame) {
                    string sig = isDirectUi ? "<ui>" : (ev != null ? DependencyObject.UnboxString(ev, "") : "<null>");
                    if (!OneTimeDone || boxDiff || (!isDirectUi && sig != LastHash)) {
                        _lastTemplateValBox = ev;
                        LastHash = sig;
                        OneTimeDone = true;
                        ApplyConverted(ev);
                    }
                } else {
                    _lastTemplateValBox = ev;
                }
                if (OneTimeDone && _elemPropResolvedOnSelf && IsSingleHopPath) {
                    MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                    BindUiSourceSub(SourceElement);
                    BindVmSourceSub(0, 0);
                } else {
                    _fastElemClean = false;
                    ClearSourceSubscriptions();
                }
            } else {
                _fastElemClean = false;
                ClearSourceSubscriptions();
                if (HasFallback && host != null && !OneTimeDone) {
                    OneTimeDone = true;
                    ApplyRawBox(FallbackBox);
                }
            }
            return;
        }

        if (Kind == 3 && SourceElement != null) {
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            int tpEpoch = IsSingleHopPath ? TryGetCachedFastElementEpoch(SourceElement) : -1;
            bool canFastTp = tpEpoch >= 0;
            if (OneTimeDone && _elemPropResolvedOnSelf && canFastTp && tpEpoch == LastDirtyMask) {
                MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                if (IsSingleHopPath) {
                    if (_subscribedUiSrc != SourceElement || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                        BindUiSourceSub(SourceElement);
                        BindVmSourceSub(0, 0);
                    }
                } else {
                    ClearSourceSubscriptions();
                }
                return;
            }
            DependencyObject tv = SourcePropId > 0
                ? SourceElement.GetPropertyById(SourcePropId, SourcePath)
                : SourceElement.GetPropertyValue(SourcePath);
            bool explicitNull = (tv == null) && SourceElement.IsExplicitNullProperty(SourcePath);
            _elemPropResolvedOnSelf = canFastTp && (tv != null || explicitNull || SourceElement.DataContext == null);
            if (tv == null && !explicitNull) {
                tv = BindingEngine.ReadPathBox(SourceElement, SourcePath);
            }
            if (tv == null && !explicitNull && HasFallback) { tv = FallbackBox; }
            bool colorSame = OneTimeDone && (tv is BoxedColor) && (_lastTemplateValBox is BoxedColor) &&
                             ((tv as BoxedColor).Value == (_lastTemplateValBox as BoxedColor).Value);
            bool boxDiff = (tv is Brush || tv is UIElement || _lastTemplateValBox is Brush || _lastTemplateValBox is UIElement)
                && (tv != _lastTemplateValBox);
            LastDirtyMask = _elemPropResolvedOnSelf ? tpEpoch : -1;
            if (!colorSame) {
                string sig = tv != null ? DependencyObject.UnboxString(tv, "") : "<null>";
                if (boxDiff || sig != LastHash || !OneTimeDone) {
                    _lastTemplateValBox = tv;
                    LastHash = sig;
                    OneTimeDone = true;
                    ApplyConverted(tv);
                }
            } else {
                _lastTemplateValBox = tv;
            }
            if (OneTimeDone && _elemPropResolvedOnSelf) {
                MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                if (IsSingleHopPath) {
                    BindUiSourceSub(SourceElement);
                    BindVmSourceSub(0, 0);
                } else {
                    ClearSourceSubscriptions();
                }
            } else {
                _fastElemClean = false;
                ClearSourceSubscriptions();
            }
            return;
        }

        if (Kind == 4 && MultiBindings != null) {
            int mbLen = MultiBindings.Length;
            if (_multiElementSources == null || _multiElementSources.Length < mbLen) {
                _multiElementSources = new UIElement[mbLen];
                _multiFindRoots = new UIElement[mbLen];
            }
            int preMultiHash = TryComputeMultiEpochHash(host, mbLen);
            if (preMultiHash >= 0 && preMultiHash == _lastMultiEpochHash) {
                return;
            }
            var vals = new DependencyObject[mbLen];
            string sig = "";
            for (int i = 0; i < mbLen; i += 1) {
                Binding sub = MultiBindings[i];
                DependencyObject sv = null;
                bool subExplicitNull = false;
                if (sub.ElementName.Length > 0 && host != null) {
                    UIElement se = _multiElementSources[i];
                    if (se != null && se._isDisposedTree) {
                        se = null;
                        _multiElementSources[i] = null;
                        _multiFindRoots[i] = null;
                    }
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
                        sv = ReadElementOrDcPathBox(se, sub.Path);
                        subExplicitNull = (sv == null) && se.IsExplicitNullProperty(sub.Path);
                    }
                } else if (sub.RelativeSourceMode == "Self" && host != null) {
                    if (Target != null) {
                        sv = Target.GetPropertyValue(sub.Path);
                        subExplicitNull = (sv == null) && Target.IsExplicitNullProperty(sub.Path);
                        if (sv == null && !subExplicitNull) { sv = BindingEngine.ReadPathBox(Target, sub.Path); }
                    } else {
                        sv = TargetInline.GetPropertyValue(sub.Path);
                        if (sv == null) { sv = BindingEngine.ReadPathBox(TargetInline, sub.Path); }
                    }
                } else if (sub.RelativeSourceMode == "TemplatedParent" && host != null && host.TemplatedParent != null) {
                    sv = host.TemplatedParent.GetPropertyValue(sub.Path);
                    subExplicitNull = (sv == null) && host.TemplatedParent.IsExplicitNullProperty(sub.Path);
                    if (sv == null && !subExplicitNull) { sv = BindingEngine.ReadPathBox(host.TemplatedParent, sub.Path); }
                } else if (sub.SourceBox != null && (sub.Path == null || sub.Path.Length == 0 || sub.Path == ".")) {
                    sv = sub.SourceBox;
                } else {
                    object dc = sub.Source != null ? sub.Source : DependencyObject.Unbox(ResolveSourceBox());
                    if (dc != null) {
                        sv = BindingEngine.ReadPathBox(dc, sub.Path);
                        if (sv == null && dc is UIElement) {
                            subExplicitNull = (dc as UIElement).IsExplicitNullProperty(sub.Path);
                        }
                    }
                }
                if (sv == null && !subExplicitNull && sub.HasFallbackValue) { sv = sub.FallbackBox; }
                vals[i] = sv;
                sig = sig + "|" + (sv != null ? DependencyObject.UnboxString(sv, "") : "<null>");
            }
            if (sig == LastHash && OneTimeDone) {
                _lastMultiEpochHash = TryComputeMultiEpochHash(host, mbLen);
                return;
            }
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
            _lastMultiEpochHash = TryComputeMultiEpochHash(host, mbLen);
            return;
        }

        if (Kind == 5 && CompiledUpdater != null) {
            if (Target == null) { return; }
            int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
            int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) {
                _fastKind1VmClean = false;
                ClearSourceSubscriptions();
                return;
            }
            int dm = ParseUtil.IsNativeReady() ? Reflect_GetDirtyMask(srcObj) : -1;
            bool sameSrc = DependencyObject.SameValueBox(srcBox, _lastSrcBox);
            bool canKind5VmFast = !(srcObj is UIElement) && dm >= 0;
            if (OneTimeDone && sameSrc && canKind5VmFast && dm == LastDirtyMask) {
                MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                if (_subscribedVmToken == 0 || _subscribedMidVmToken != 0 || _subscribedUiSrc != null) {
                    int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                    BindUiSourceSub(null);
                    BindVmSourceSub(tok0, 0);
                }
                return;
            }
            _lastSrcBox = srcBox;
            LastDirtyMask = dm;
            OneTimeDone = true;
            CompiledUpdater(Target, srcObj);
            BindingEngine.MarkFlushDirty();
            if (canKind5VmFast) {
                MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                BindUiSourceSub(null);
                BindVmSourceSub(tok0, 0);
            } else {
                _fastKind1VmClean = false;
                ClearSourceSubscriptions();
            }
            return;
        }

        if (Kind == 1) {
            if (host == null && RelativeSourceMode != "Self") { return; }
            int snapNativeVmEp = BindingEngine._curNativeVmEpoch;
            int snapManagedVmEp = BindingEngine._curManagedVmEpoch;
            int snapDcEp = UIElement._dataContextEpoch;
            int snapTreeEp = UIElement._treeParentEpoch;
            int snapVisEp = UIElement._visibilityEpoch;
            int snapUiPropEp = UIElement._globalPropEpoch;
            int snapLayoutEp = BindingEngine._curLayoutSizeEpoch;
            int snapDispEp = UIElement._disposeEpoch;
            DependencyObject srcBox = ResolveSourceBox();
            object srcObj = DependencyObject.Unbox(srcBox);
            if (srcObj == null) {
                _lastMidBox = null;
                _lastMidDirtyMask = -1;
                _fastKind1VmClean = false;
                _fastElemClean = false;
                ClearSourceSubscriptions();
                if (HasFallback) { ApplyRawBox(FallbackBox); }
                return;
            }
            bool srcIsUi = srcObj is UIElement;
            int dm = -1;
            if (srcIsUi) {
                dm = TryGetCachedFastElementEpoch(srcObj as UIElement);
            } else if (ParseUtil.IsNativeReady()) {
                dm = Reflect_GetDirtyMask(srcObj);
            }
            bool sameSrc = DependencyObject.SameValueBox(srcBox, _lastSrcBox);
            if (OneTimeDone && sameSrc && dm >= 0 && dm == LastDirtyMask) {
                if (IsSingleHopPath) {
                    if (!srcIsUi) {
                        MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                        if (_subscribedVmToken == 0 || _subscribedMidVmToken != 0 || _subscribedUiSrc != null) {
                            UpdateKind1Subscriptions(true, false, srcObj);
                        }
                    } else if (_elemPropResolvedOnSelf) {
                        MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp);
                        if (_subscribedUiSrc != (srcObj as UIElement) || _subscribedVmToken != 0 || _subscribedMidVmToken != 0) {
                            UpdateKind1Subscriptions(false, true, srcObj);
                        }
                    }
                    return;
                }
                if (_lastMidDirtyMask >= 0 && _lastMidBox != null && PathSegments != null && PathSegments.Length == 2) {
                    object cachedMid = DependencyObject.Unbox(_lastMidBox);
                    int midDm = -1;
                    if (cachedMid is UIElement) {
                        UIElement midEl = cachedMid as UIElement;
                        if (CanUseFastElementPropEpoch(midEl, PathSegments[1])) {
                            midDm = GetFastElementEpoch(midEl, PathSegments[1]);
                        }
                    } else if (cachedMid != null && !(cachedMid is DependencyObject) && ParseUtil.IsNativeReady()) {
                        midDm = Reflect_GetDirtyMask(cachedMid);
                    }
                    if (midDm >= 0 && midDm == _lastMidDirtyMask) {
                        if (!srcIsUi && cachedMid != null && !(cachedMid is UIElement)) {
                            MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp);
                            if (_subscribedVmToken == 0 || _subscribedMidVmToken == 0 || _subscribedUiSrc != null) {
                                int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
                                int tok1 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(cachedMid) : 0;
                                BindUiSourceSub(null);
                                BindVmSourceSub(tok0, tok1);
                            }
                        }
                        return;
                    }
                }
            }
            DependencyObject valBox = null;
            bool explicitNull = false;
            if (srcIsUi && IsSingleHopPath) {
                UIElement srcEl = srcObj as UIElement;
                valBox = SourcePropId > 0 ? srcEl.GetPropertyById(SourcePropId, SourcePath) : srcEl.GetPropertyValue(SourcePath);
                explicitNull = (valBox == null) && srcEl.IsExplicitNullProperty(SourcePath);
                _elemPropResolvedOnSelf = (dm >= 0) && (valBox != null || explicitNull || srcEl.DataContext == null);
            } else {
                _elemPropResolvedOnSelf = false;
            }
            if (valBox == null && !explicitNull) {
                valBox = BindingEngine.ReadSegmentsBox(srcObj, SourcePath, PathSegments);
            }
            if (valBox == null && !explicitNull && HasFallback) { valBox = FallbackBox; }
            bool boxDiff = (valBox != _lastPathValBox) && !DependencyObject.SameValueBox(valBox, _lastPathValBox);
            _lastMidBox = null;
            _lastMidDirtyMask = -1;
            bool midIsNonUiObservable = false;
            if (!IsSingleHopPath && PathSegments != null && PathSegments.Length == 2 && dm >= 0 &&
                !(srcObj is Transform) && !PathSegments[0].Contains("[") && !PathSegments[1].Contains("[")) {
                DependencyObject midBox = null;
                object midObj = null;
                if (srcIsUi) {
                    UIElement srcEl0 = srcObj as UIElement;
                    midBox = srcEl0.GetPropertyValue(PathSegments[0]);
                    if (midBox != null) {
                        midObj = DependencyObject.Unbox(midBox);
                    } else if (!srcEl0.IsExplicitNullProperty(PathSegments[0])) {
                        midObj = Reflect_GetPropObject(srcObj, PathSegments[0]);
                        if (midObj != null) { midBox = DependencyObject.BoxObject(midObj); }
                    }
                } else {
                    midObj = Reflect_GetPropObject(srcObj, PathSegments[0]);
                    if (midObj != null) { midBox = DependencyObject.BoxObject(midObj); }
                }
                if (midObj is UIElement) {
                    UIElement midEl = midObj as UIElement;
                    if (CanUseFastElementPropEpoch(midEl, PathSegments[1])) {
                        _lastMidBox = midBox;
                        _lastMidDirtyMask = GetFastElementEpoch(midEl, PathSegments[1]);
                    }
                } else if (midObj != null && !(midObj is DependencyObject) && ParseUtil.IsNativeReady()) {
                    _lastMidBox = midBox;
                    _lastMidDirtyMask = Reflect_GetDirtyMask(midObj);
                    midIsNonUiObservable = _lastMidDirtyMask >= 0;
                }
            }
            bool canKind1VmFast = !srcIsUi && dm >= 0 && (IsSingleHopPath || midIsNonUiObservable);
            bool canKind1ElemFast = srcIsUi && IsSingleHopPath && _elemPropResolvedOnSelf;
            if (!boxDiff && OneTimeDone && sameSrc &&
                (valBox == _lastPathValBox || valBox is BoxedInt || valBox is BoxedFloat || valBox is BoxedBool || valBox is BoxedString || valBox is BoxedColor)) {
                _lastSrcBox = srcBox;
                LastDirtyMask = dm;
                if (canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
                else { _fastKind1VmClean = false; }
                if (canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
                else { _fastElemClean = false; }
                UpdateKind1Subscriptions(canKind1VmFast, canKind1ElemFast, srcObj);
                return;
            }
            string sig = valBox != null ? DependencyObject.UnboxString(valBox, "") : "<null>";
            if (!boxDiff && sig == LastHash && OneTimeDone && sameSrc) {
                _lastSrcBox = srcBox;
                LastDirtyMask = dm;
                if (canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
                else { _fastKind1VmClean = false; }
                if (canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
                else { _fastElemClean = false; }
                UpdateKind1Subscriptions(canKind1VmFast, canKind1ElemFast, srcObj);
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
            if (OneTimeDone && canKind1VmFast) { MarkVmFastClean(snapNativeVmEp, snapManagedVmEp, snapDcEp, snapTreeEp, snapVisEp, snapDispEp); }
            else { _fastKind1VmClean = false; }
            if (OneTimeDone && canKind1ElemFast) { MarkElemFastClean(snapUiPropEp, snapLayoutEp, snapTreeEp, snapDcEp, snapVisEp, snapDispEp); }
            else { _fastElemClean = false; }
            UpdateKind1Subscriptions(_fastKind1VmClean, _fastElemClean, srcObj);
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

    private void MarkVmFastClean(int navEp, int manEp, int dcEp, int trEp, int visEp, int dispEp) {
        _fastKind1VmClean = true;
        _fastNativeVmEp = navEp;
        _fastManagedVmEp = manEp;
        _fastDcEp = dcEp;
        _fastTreeEp = trEp;
        _fastVisEp = visEp;
        _fastDispEp = dispEp;
    }

    private void MarkElemFastClean(int uiEp, int layEp, int trEp, int dcEp, int visEp, int dispEp) {
        _fastElemClean = true;
        _fastUiPropEp = uiEp;
        _fastLayoutEp = layEp;
        _fastTreeEp = trEp;
        _fastDcEp = dcEp;
        _fastVisEp = visEp;
        _fastDispEp = dispEp;
    }

    private void UpdateKind1Subscriptions(bool canVm, bool canElem, object srcObj) {
        if (canVm) {
            int tok0 = ParseUtil.IsNativeReady() ? Reflect_GetObjectToken(srcObj) : 0;
            int tok1 = 0;
            if (!IsSingleHopPath && _lastMidBox != null) {
                object midObj2 = DependencyObject.Unbox(_lastMidBox);
                if (midObj2 != null && ParseUtil.IsNativeReady()) {
                    tok1 = Reflect_GetObjectToken(midObj2);
                }
            }
            BindUiSourceSub(null);
            BindVmSourceSub(tok0, tok1);
        } else if (canElem) {
            BindUiSourceSub(srcObj as UIElement);
            BindVmSourceSub(0, 0);
        } else {
            ClearSourceSubscriptions();
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

internal class VmSubBucket {
    public BindingEntry[] Items;
    public VmSubBucket() { Items = new BindingEntry[0]; }
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
    private static Action[] _postLayoutHooks;
    private static FrameTickHook[] _frameTickHooks;
    private static int _batchDepth = 0;
    private static BindingEntry[] _batchPending;
    private static bool _flushPassDirty = false;
    private static bool _inPostFlush = false;
    internal static int _curLayoutSizeEpoch = 1;
    internal static int _managedVmNotifyEpoch = 1;
    internal static int _curNativeVmEpoch = 1;
    internal static int _curManagedVmEpoch = 1;
    internal static bool _inFlushPass = false;
    internal static bool _inSubtreeFlush = false;
    internal static bool _inPushWave = false;
    private static int _lastCompactDisposeEpoch = -1;
    private static int _structEpoch = 1;
    private static bool _hasFlushSnapshot = false;
    private static int _snapStructEpoch = -1;
    private static int _snapNativeVmEp = -1;
    private static int _snapManagedVmEp = -1;
    private static int _snapUiPropEp = -1;
    private static int _snapLayoutEp = -1;
    private static int _snapDcEp = -1;
    private static int _snapTreeEp = -1;
    private static int _snapVisEp = -1;
    private static int _snapDispEp = -1;
    private static BindingEntry[] _pollOnlyEntries;
    private static VmSubBucket[] _vmSubs;
    private static BindingEntry[] _dirtyQueue;
    private static int _dirtyWaveGen = 1;
    private static bool _pendingPushWave = false;
    private static int _debugShortCircuitFlushCount = 0;
    private static int _debugPushFlushCount = 0;
    private static int _debugEvaluatedEntryCount = 0;

    internal static void BumpStructEpoch() {
        _structEpoch = _structEpoch >= 1000000000 ? 1 : (_structEpoch + 1);
        _hasFlushSnapshot = false;
    }

    public static int DebugShortCircuitFlushCount() {
        return _debugShortCircuitFlushCount;
    }

    public static int DebugPushFlushCount() {
        return _debugPushFlushCount;
    }

    public static int DebugEvaluatedEntryCount() {
        return _debugEvaluatedEntryCount;
    }

    public static void DebugResetFlushStats() {
        _debugShortCircuitFlushCount = 0;
        _debugPushFlushCount = 0;
        _debugEvaluatedEntryCount = 0;
    }

    internal static void SubscribeVmToken(int tok, BindingEntry entry) {
        if (tok <= 0 || entry == null) { return; }
        if (_vmSubs == null) { _vmSubs = new VmSubBucket[0]; }
        while (_vmSubs.Length <= tok) { _vmSubs.Push(null); }
        VmSubBucket bucket = _vmSubs[tok];
        if (bucket == null) {
            bucket = new VmSubBucket();
            _vmSubs[tok] = bucket;
        }
        BindingEntry[] arr = bucket.Items;
        for (int i = 0; i < arr.Length; i += 1) {
            if (arr[i] == entry) { return; }
        }
        arr.Push(entry);
    }

    internal static void UnsubscribeVmToken(int tok, BindingEntry entry) {
        if (tok <= 0 || entry == null || _vmSubs == null || tok >= _vmSubs.Length) { return; }
        VmSubBucket bucket = _vmSubs[tok];
        if (bucket == null || bucket.Items == null) { return; }
        BindingEntry[] arr = bucket.Items;
        for (int i = arr.Length - 1; i >= 0; i -= 1) {
            if (arr[i] == null || arr[i] == entry) {
                arr.RemoveAt(i);
            }
        }
        if (arr.Length == 0) {
            _vmSubs[tok] = null;
        }
    }

    internal static void EnqueueDirtyEntry(BindingEntry entry) {
        if (entry == null || entry.IsDead()) { return; }
        if (entry._dirtyEnqueuedGen == _dirtyWaveGen) { return; }
        entry._dirtyEnqueuedGen = _dirtyWaveGen;
        if (_dirtyQueue == null) { _dirtyQueue = new BindingEntry[0]; }
        _dirtyQueue.Push(entry);
        _pendingPushWave = true;
    }

    private static bool StructEpochsMatch() {
        return _structEpoch == _snapStructEpoch &&
               _curManagedVmEpoch == _snapManagedVmEp &&
               UIElement._dataContextEpoch == _snapDcEp &&
               UIElement._treeParentEpoch == _snapTreeEp &&
               UIElement._visibilityEpoch == _snapVisEp &&
               UIElement._disposeEpoch == _snapDispEp;
    }

    private static void ClearDirtyQueue() {
        _dirtyQueue = new BindingEntry[0];
        _pendingPushWave = false;
        _dirtyWaveGen = _dirtyWaveGen >= 1000000000 ? 1 : (_dirtyWaveGen + 1);
    }

    private static int DrainCppDirtyQueues() {
        if (!ParseUtil.IsNativeReady()) { return 0; }
        int vmCnt = Reflect_BeginConsumeDirtyTokens();
        int uiCnt = Visual_BeginConsumeDirtyPropNodes();
        if (vmCnt >= 0 && _vmSubs != null) {
            for (int i = 0; i < vmCnt; i += 1) {
                int tok = Reflect_GetConsumedDirtyToken(i);
                if (tok > 0 && tok < _vmSubs.Length) {
                    VmSubBucket bucket = _vmSubs[tok];
                    if (bucket != null && bucket.Items != null) {
                        BindingEntry[] items = bucket.Items;
                        for (int j = items.Length - 1; j >= 0; j -= 1) {
                            BindingEntry e = items[j];
                            if (e == null || e.IsDead()) {
                                items.RemoveAt(j);
                                continue;
                            }
                            EnqueueDirtyEntry(e);
                        }
                        if (items.Length == 0) { _vmSubs[tok] = null; }
                    }
                }
            }
        }
        if (uiCnt > 0) {
            for (int i = 0; i < uiCnt; i += 1) {
                object owner = Visual_GetConsumedDirtyPropOwner(i);
                if (owner is UIElement) {
                    UIElement el = owner as UIElement;
                    if (el.HasUiFlag(16384)) {
                        el.BumpPropEpoch();
                    }
                }
            }
        }
        if (vmCnt < 0 || uiCnt < 0) { return -1; }
        return vmCnt + uiCnt;
    }

    internal static void BumpManagedVmEpoch() {
        _managedVmNotifyEpoch = _managedVmNotifyEpoch >= 1000000000 ? 1 : (_managedVmNotifyEpoch + 1);
    }

    private static void RefreshLayoutEpoch() {
        if (ParseUtil.IsNativeReady()) {
            _curLayoutSizeEpoch = Visual_GetWorldLayoutEpoch();
        }
    }

    internal static void RefreshVmEpochs() {
        _curManagedVmEpoch = _managedVmNotifyEpoch;
        if (ParseUtil.IsNativeReady()) {
            _curNativeVmEpoch = Reflect_GetGlobalObservableEpoch();
        }
    }

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
        BumpStructEpoch();
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        BindingEntry[] pending = _batchPending;
        _batchPending = new BindingEntry[0];
        for (int i = 0; i < pending.Length; i += 1) {
            BindingEntry e = pending[i];
            if (e != null &&
                (e.Target == null || !e.Target._isDisposedTree) &&
                (e.ContextHost == null || !e.ContextHost._isDisposedTree)) {
                e.Evaluate();
            }
        }
        _inFlushPass = prevInFlush;
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

    public static void AddPostLayoutHook(Action hook) {
        if (hook == null) { return; }
        if (_postLayoutHooks == null) { _postLayoutHooks = new Action[0]; }
        _postLayoutHooks.Push(hook);
    }

    public static void AddFrameTickHook(FrameTickHook hook) {
        if (hook == null) { return; }
        if (_frameTickHooks == null) { _frameTickHooks = new FrameTickHook[0]; }
        _frameTickHooks.Push(hook);
    }

    public static void StepFrameTicks(float dt) {
        if (dt <= 0.0f) { return; }
        InputDispatcher.AdvanceUiClock(dt);
        if (_frameTickHooks == null) { return; }
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
        BumpStructEpoch();
        if (entry != null) {
            entry.TargetPropId = (entry.Target != null && entry.TargetProp != null) ? AeroPropId.Resolve(entry.TargetProp) : 0;
            entry.SourcePropId = (entry.SourcePath != null) ? AeroPropId.Resolve(entry.SourcePath) : 0;
        }
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
        } else if (entry != null && entry.TargetEffect != null && entry.ContextHost != null) {
            ElementBindingList list = null;
            if (entry.ContextHost._ownBindingsBox is ElementBindingList) {
                list = entry.ContextHost._ownBindingsBox as ElementBindingList;
            } else {
                list = new ElementBindingList();
                entry.ContextHost._ownBindingsBox = list;
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
                    if (e != null) { e.ClearSourceSubscriptions(); }
                    _entries.RemoveAt(i);
                }
            }
        }
        BumpStructEpoch();
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
        target.ClearResourceReference(targetProp);
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
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) {
            UnbindTriggerProperty(target, targetProp);
        }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
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
        bool isTrigBind = target.HasUiFlag(256);
        if (isTrigBind) {
            UnbindTriggerProperty(target, targetProp);
        }
        var e = new BindingEntry();
        e._isTriggerBinding = isTrigBind;
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
        target.ClearResourceReference(targetProp);
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
        if (target == null) { return; }
        target.BumpPropEpoch();
        MarkFlushDirty();
        if (!(target._ownBindingsBox is ElementBindingList)) { return; }
        BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
        for (int i = 0; i < own.Length; i += 1) {
            BindingEntry e = own[i];
            if (e.Target == target && e.TargetProp == targetProp && e.Mode == 1 && !e._isApplying) {
                e.PushTwoWay(newBox);
            }
        }
    }

    public static void CleanupDisposed() {
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            BindingEntry e = _entries[i];
            if (e == null || e.IsDead()) {
                if (e != null) { e.ClearSourceSubscriptions(); }
                _entries.RemoveAt(i);
                continue;
            }
            if (e.SourceElement != null && e.SourceElement._isDisposedTree) {
                e.ClearSourceSubscriptions();
                e.SourceElement = null;
            }
        }
        BumpStructEpoch();
    }

    public static bool HasTriggerBinding(UIElement target, string prop) {
        if (target == null || prop == null || prop.Length == 0) { return false; }
        if (target._ownBindingsBox is ElementBindingList) {
            BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
            for (int i = 0; i < own.Length; i += 1) {
                if (own[i] != null && own[i]._isTriggerBinding && own[i].TargetProp == prop) {
                    return true;
                }
            }
        }
        return false;
    }

    public static void UnbindTriggerProperty(UIElement target, string prop) {
        if (target == null || prop == null || prop.Length == 0) { return; }
        if (target._ownBindingsBox is ElementBindingList) {
            BindingEntry[] own = (target._ownBindingsBox as ElementBindingList).Items;
            for (int i = own.Length - 1; i >= 0; i -= 1) {
                if (own[i] != null && own[i]._isTriggerBinding && own[i].TargetProp == prop) {
                    own.RemoveAt(i);
                }
            }
        }
        if (_entries != null) {
            for (int i = _entries.Length - 1; i >= 0; i -= 1) {
                BindingEntry e = _entries[i];
                if (e != null && e._isTriggerBinding && e.Target == target && e.TargetProp == prop) {
                    e.ClearSourceSubscriptions();
                    _entries.RemoveAt(i);
                }
            }
        }
        BumpStructEpoch();
    }

    public static void UnbindTarget(UIElement target) {
        if (target == null) { return; }
        target._ownBindingsBox = null;
        if (_entries == null) { return; }
        for (int i = _entries.Length - 1; i >= 0; i -= 1) {
            if (_entries[i].Target == target) {
                _entries[i].ClearSourceSubscriptions();
                _entries.RemoveAt(i);
            }
        }
        BumpStructEpoch();
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
        RefreshLayoutEpoch();
        bool prevSub = _inSubtreeFlush;
        _inSubtreeFlush = true;
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
        _inSubtreeFlush = prevSub;
        BumpStructEpoch();
    }

    private static void FlushCleanupOnly() {
        TriggerBase.FlushPendingCleanup();
        Storyboard.FlushPendingCleanup();
        if (_pendingDisposedCount > 0) {
            _pendingDisposedCount = 0;
            CleanupDisposed();
        }
    }

    private static void FlushBodyAndPostHooks() {
        if (_entries != null) {
            if (_lastCompactDisposeEpoch != UIElement._disposeEpoch) {
                _lastCompactDisposeEpoch = UIElement._disposeEpoch;
                BumpStructEpoch();
                int write = 0;
                for (int i = 0; i < _entries.Length; i += 1) {
                    BindingEntry e = _entries[i];
                    if (e == null || e.IsDead()) {
                        if (e != null) { e.ClearSourceSubscriptions(); }
                        continue;
                    }
                    if (write != i) { _entries[write] = e; }
                    write += 1;
                }
                while (_entries.Length > write) {
                    _entries.RemoveAt(_entries.Length - 1);
                }
            }
            RefreshLayoutEpoch();
            RefreshVmEpochs();
            int drainState = DrainCppDirtyQueues();
            bool structClean = _hasFlushSnapshot && drainState >= 0 && StructEpochsMatch();
            bool shortCircuited = false;
            if (structClean && !_pendingPushWave && drainState == 0 &&
                _curNativeVmEpoch == _snapNativeVmEp &&
                UIElement._globalPropEpoch == _snapUiPropEp &&
                _curLayoutSizeEpoch == _snapLayoutEp) {
                _flushPassDirty = false;
                if (_pollOnlyEntries != null && _pollOnlyEntries.Length > 0) {
                    for (int pi = 0; pi < _pollOnlyEntries.Length; pi += 1) {
                        _pollOnlyEntries[pi].Evaluate();
                        _debugEvaluatedEntryCount += 1;
                    }
                }
                if (!_flushPassDirty && !_pendingPushWave && _structEpoch == _snapStructEpoch) {
                    shortCircuited = true;
                    _debugShortCircuitFlushCount += 1;
                } else {
                    _hasFlushSnapshot = false;
                }
            } else if (structClean) {
                bool pushConverged = false;
                bool prevInPush = _inPushWave;
                _inPushWave = true;
                for (int wave = 0; wave < 4; wave += 1) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _flushPassDirty = false;
                    _dirtyWaveGen = _dirtyWaveGen >= 1000000000 ? 1 : (_dirtyWaveGen + 1);
                    int evalStamp = _dirtyWaveGen;
                    BindingEntry[] batch = _dirtyQueue;
                    _dirtyQueue = new BindingEntry[0];
                    _pendingPushWave = false;
                    if (batch != null) {
                        for (int bi = 0; bi < batch.Length; bi += 1) {
                            BindingEntry de = batch[bi];
                            if (de == null || de.IsDead()) { continue; }
                            de.Evaluate();
                            de._lastEvalWaveGen = evalStamp;
                            _debugEvaluatedEntryCount += 1;
                            if (!de._inPollOnly && !de.IsEpochFastClean()) {
                                if (_pollOnlyEntries == null) { _pollOnlyEntries = new BindingEntry[0]; }
                                _pollOnlyEntries.Push(de);
                                de._inPollOnly = true;
                            }
                        }
                    }
                    if ((wave == 0 || _flushPassDirty) && _pollOnlyEntries != null && _pollOnlyEntries.Length > 0) {
                        for (int pi = 0; pi < _pollOnlyEntries.Length; pi += 1) {
                            BindingEntry pe = _pollOnlyEntries[pi];
                            if (pe != null && pe._lastEvalWaveGen != evalStamp) {
                                pe.Evaluate();
                                pe._lastEvalWaveGen = evalStamp;
                                _debugEvaluatedEntryCount += 1;
                            }
                        }
                    }
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    if (!StructEpochsMatch()) {
                        pushConverged = false;
                        break;
                    }
                    int moreDrained = DrainCppDirtyQueues();
                    if (moreDrained < 0) {
                        pushConverged = false;
                        break;
                    }
                    if (!_pendingPushWave && (_dirtyQueue == null || _dirtyQueue.Length == 0)) {
                        pushConverged = true;
                        break;
                    }
                }
                _inPushWave = prevInPush;
                if (pushConverged) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _snapNativeVmEp = _curNativeVmEpoch;
                    _snapUiPropEp = UIElement._globalPropEpoch;
                    _snapLayoutEp = _curLayoutSizeEpoch;
                    _hasFlushSnapshot = true;
                    shortCircuited = true;
                    _debugPushFlushCount += 1;
                } else {
                    _hasFlushSnapshot = false;
                }
            }
            if (!shortCircuited) {
                ClearDirtyQueue();
                bool converged = false;
                for (int pass = 0; pass < 3; pass += 1) {
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _flushPassDirty = false;
                    for (int i = 0; i < _entries.Length; i += 1) {
                        _entries[i].Evaluate();
                        _debugEvaluatedEntryCount += 1;
                    }
                    if (!_flushPassDirty) {
                        converged = true;
                        break;
                    }
                }
                if (converged) {
                    if (_pollOnlyEntries == null) {
                        _pollOnlyEntries = new BindingEntry[0];
                    } else {
                        while (_pollOnlyEntries.Length > 0) {
                            _pollOnlyEntries.RemoveAt(_pollOnlyEntries.Length - 1);
                        }
                    }
                    for (int i = 0; i < _entries.Length; i += 1) {
                        BindingEntry pe = _entries[i];
                        if (pe != null) {
                            pe._inPollOnly = false;
                            if (!pe.IsEpochFastClean()) {
                                _pollOnlyEntries.Push(pe);
                                pe._inPollOnly = true;
                            }
                        }
                    }
                    int postDrain = DrainCppDirtyQueues();
                    ClearDirtyQueue();
                    RefreshLayoutEpoch();
                    RefreshVmEpochs();
                    _snapStructEpoch = _structEpoch;
                    _snapNativeVmEp = _curNativeVmEpoch;
                    _snapManagedVmEp = _curManagedVmEpoch;
                    _snapUiPropEp = UIElement._globalPropEpoch;
                    _snapLayoutEp = _curLayoutSizeEpoch;
                    _snapDcEp = UIElement._dataContextEpoch;
                    _snapTreeEp = UIElement._treeParentEpoch;
                    _snapVisEp = UIElement._visibilityEpoch;
                    _snapDispEp = UIElement._disposeEpoch;
                    _hasFlushSnapshot = (postDrain >= 0);
                } else {
                    ClearDirtyQueue();
                    _hasFlushSnapshot = false;
                }
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
                _hasFlushSnapshot = false;
                RefreshVmEpochs();
                _flushPassDirty = false;
                for (int i = 0; i < _entries.Length; i += 1) {
                    _entries[i].Evaluate();
                    _debugEvaluatedEntryCount += 1;
                }
            }
            _inPostFlush = false;
        }
    }

    public static void Flush() {
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        if (_preFlushHooks != null) {
            for (int h = 0; h < _preFlushHooks.Length; h += 1) {
                var fn = _preFlushHooks[h];
                if (fn != null) { fn(); }
            }
        }
        FlushCleanupOnly();
        FlushBodyAndPostHooks();
        _inFlushPass = prevInFlush;
    }

    public static void FlushPostLayout() {
        RefreshLayoutEpoch();
        RefreshVmEpochs();
        bool prevInFlush = _inFlushPass;
        _inFlushPass = true;
        FlushCleanupOnly();
        bool hasEntriesOrPost = (_entries != null && _entries.Length > 0) ||
                                (_postFlushHooks != null && _postFlushHooks.Length > 0);
        if (hasEntriesOrPost) {
            FlushBodyAndPostHooks();
        }
        if (_postLayoutHooks != null && _postLayoutHooks.Length > 0) {
            for (int h = 0; h < _postLayoutHooks.Length; h += 1) {
                var fn = _postLayoutHooks[h];
                if (fn != null) { fn(); }
            }
        }
        _inFlushPass = prevInFlush;
    }

    // ---- Reflection Path Traversals ----
    internal static string[] SplitBindingPath(string path) {
        if (path == null || path.Length == 0) { return new string[0]; }
        if (!path.Contains(".")) {
            var single = new string[1];
            single[0] = path;
            return single;
        }
        if (!path.Contains("(")) {
            return path.Split(".");
        }
        var parts = new string[0];
        int parenDepth = 0;
        int start = 0;
        for (int i = 0; i < path.Length; i += 1) {
            string ch = path.Substring(i, 1);
            if (ch == "(") {
                parenDepth += 1;
            } else if (ch == ")" && parenDepth > 0) {
                parenDepth -= 1;
            } else if (ch == "." && parenDepth == 0) {
                parts.Push(path.Substring(start, i - start));
                start = i + 1;
            }
        }
        parts.Push(path.Substring(start));
        return parts;
    }

    internal static DependencyObject ReadSegmentsBox(object root, string path, string[] segs) {
        if (root == null) { return null; }
        if (path == null || path.Length == 0 || path == ".") {
            if (root is DependencyObject) { return root as DependencyObject; }
            return DependencyObject.BoxObject(root);
        }
        if (root is UIElement) {
            UIElement rootEl = root as UIElement;
            DependencyObject direct = rootEl.GetPropertyValue(path);
            if (direct != null) { return direct; }
            if (rootEl.IsExplicitNullProperty(path)) { return null; }
            if (path.Length > 2 && path.Substring(0, 1) == "(" && path.Substring(path.Length - 1, 1) == ")") {
                return null;
            }
        }
        if (root is Transform) {
            Transform rootTr = root as Transform;
            DependencyObject rawAtt = rootTr.GetAttachedValue(path);
            if (rawAtt != null) { return rawAtt; }
            if (path.Length > 2 && path.Substring(0, 1) == "(" && path.Substring(path.Length - 1, 1) == ")") {
                string innerAtt = path.Substring(1, path.Length - 2).Trim();
                DependencyObject directAtt = rootTr.GetAttachedValue(innerAtt);
                if (directAtt != null) { return directAtt; }
            }
        }
        object cur = root;
        string leaf = path;
        if (segs != null || path.Contains(".")) {
            string[] parts = segs != null ? segs : SplitBindingPath(path);
            for (int i = 0; i < parts.Length - 1; i += 1) {
                string seg = parts[i];
                if (cur is UIElement) {
                    UIElement curEl = cur as UIElement;
                    DependencyObject sub = curEl.GetPropertyValue(seg);
                    cur = sub != null ? sub.UnboxObject() : (curEl.IsExplicitNullProperty(seg) ? null : Reflect_GetPropObject(cur, seg));
                } else {
                    cur = Reflect_GetPropObject(cur, seg);
                }
                if (cur == null) { return null; }
            }
            leaf = parts[parts.Length - 1];
        }
        if (cur is UIElement) {
            UIElement leafEl = cur as UIElement;
            DependencyObject local = leafEl.GetPropertyValue(leaf);
            if (local != null) { return local; }
            if (leafEl.IsExplicitNullProperty(leaf)) { return null; }
        }
        if (cur is SolidColorBrush) {
            SolidColorBrush scb = cur as SolidColorBrush;
            if (leaf == "Color") { return DependencyObject.BoxString(scb.Color); }
            if (leaf == "Opacity") { return DependencyObject.BoxFloat(scb.Opacity); }
        }
        if (cur is GradientStop) {
            GradientStop gs = cur as GradientStop;
            if (leaf == "Color") { return DependencyObject.BoxString(gs.Color); }
            if (leaf == "Offset") { return DependencyObject.BoxFloat(gs.Offset); }
        }
        if (cur is Transform) {
            Transform trObj = cur as Transform;
            string cleanLeaf = leaf;
            if (cleanLeaf.Length > 2 && cleanLeaf.Substring(0, 1) == "(" && cleanLeaf.Substring(cleanLeaf.Length - 1, 1) == ")") {
                cleanLeaf = cleanLeaf.Substring(1, cleanLeaf.Length - 2).Trim();
            }
            DependencyObject att = trObj.GetAttachedValue(cleanLeaf);
            if (att != null) { return att; }
            if (cur is TransformGroup && cleanLeaf == "Children") {
                Transform[] kids = (cur as TransformGroup).Children();
                var col = new ArrayCollectionBox();
                for (int k = 0; k < kids.Length; k += 1) { col.AddBox(kids[k]); }
                return col;
            }
            if (cur is RotateTransform && cleanLeaf == "Angle") { return DependencyObject.BoxFloat((cur as RotateTransform).Angle); }
            if (cur is ScaleTransform && cleanLeaf == "ScaleX") { return DependencyObject.BoxFloat((cur as ScaleTransform).ScaleX); }
            if (cur is ScaleTransform && cleanLeaf == "ScaleY") { return DependencyObject.BoxFloat((cur as ScaleTransform).ScaleY); }
            if (cur is TranslateTransform && cleanLeaf == "X") { return DependencyObject.BoxFloat((cur as TranslateTransform).X); }
            if (cur is TranslateTransform && cleanLeaf == "Y") { return DependencyObject.BoxFloat((cur as TranslateTransform).Y); }
            if (cur is SkewTransform && cleanLeaf == "AngleX") { return DependencyObject.BoxFloat((cur as SkewTransform).AngleX); }
            if (cur is SkewTransform && cleanLeaf == "AngleY") { return DependencyObject.BoxFloat((cur as SkewTransform).AngleY); }
        }
        if (cur is DependencyObject && !(cur is UIElement)) {
            DependencyObject depProp = (cur as DependencyObject).GetBindingPropertyValue(leaf);
            if (depProp != null) { return depProp; }
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
        if (kind == 6) {
            return DependencyObject.BoxEnum(
                Reflect_GetPropString(cur, leaf),
                Reflect_GetPropInt(cur, leaf));
        }
        if (kind == 5) {
            object objVal = Reflect_GetPropObject(cur, leaf);
            return objVal != null ? DependencyObject.BoxObject(objVal) : null;
        }
        return null;
    }

    public static DependencyObject ReadPathBox(object root, string path) {
        if (root is UIElement && path != null && path.Length > 0) {
            UIElement rootEl = root as UIElement;
            DependencyObject direct = rootEl.GetPropertyValue(path);
            if (direct != null) { return direct; }
            if (rootEl.IsExplicitNullProperty(path)) { return null; }
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
