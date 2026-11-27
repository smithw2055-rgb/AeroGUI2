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

    internal static int CurrentAmbientDepth() {
        return _ambientStack != null ? _ambientStack.Length : 0;
    }

    internal static ResourceDictionary CurrentAmbientTop() {
        return (_ambientStack != null && _ambientStack.Length > 0) ? _ambientStack[_ambientStack.Length - 1] : null;
    }

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
        dict._isSharedDict = true;
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
    private bool _hasBeenQueried = false;
    internal bool _isSharedDict = false;

    public int Count() { return _keys.Length; }

    internal void CollectOwnedUIElements(UIElement[] q) {
        if (_isSharedDict || this == _appResources || q == null || _vals == null) { return; }
        for (int i = 0; i < _vals.Length; i += 1) {
            if (_vals[i] is UIElement) {
                UIElement el = _vals[i] as UIElement;
                if (!el._isDisposedTree && el.Parent == null) {
                    q.Push(el);
                }
            }
        }
    }

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
            _appResources._isSharedDict = true;
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
        bool addedNewStyleKey = false;
        if (box is Style || DependencyObject.Unbox(box) is Style) {
            if (_registeredStyleKeys == null) {
                _registeredStyleKeys = new Dictionary<string, int>();
            }
            if (!_registeredStyleKeys.ContainsKey(key)) {
                _registeredStyleKeys[key] = 1;
                addedNewStyleKey = true;
            }
        }
        if (_hasBeenQueried || this == _appResources || addedNewStyleKey) {
            BumpGlobalMutationEpoch();
        }
    }

    public void Set(string key, object val) { SetBox(key, DependencyObject.BoxObject(val)); }
    public void Set(string key, string val) { SetBox(key, DependencyObject.BoxString(val)); }
    public void Set(string key, float val) { SetBox(key, DependencyObject.BoxFloat(val)); }
    public void Set(string key, int val) { SetBox(key, DependencyObject.BoxInt(val)); }
    public void Set(string key, bool val) { SetBox(key, DependencyObject.BoxBool(val)); }

    public DependencyObject TryGetLocalBox(string key) {
        _hasBeenQueried = true;
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
        other._isSharedDict = true;
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
