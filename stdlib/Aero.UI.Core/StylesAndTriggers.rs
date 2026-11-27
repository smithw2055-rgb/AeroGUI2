module Aero.UI;

import Aero.Native;
import RealScript.Math;

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
    private int _propId = -2;
    private int _srcPropId = -2;
    private string[] _cachedSegs;
    private bool _cachedSingleHop = true;
    private bool _bindPrepReady = false;

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
            if (_propId == -2) {
                _propId = Property.Length > 0 ? AeroPropId.Resolve(Property) : 0;
            }
            if (DynamicResourceKey.Length > 0) {
                dest.SetResourceReference(Property, DynamicResourceKey);
            } else if (BindingElementName.Length > 0) {
                dest.ClearResourceReference(Property);
                if (!_bindPrepReady) {
                    _bindPrepReady = true;
                    _srcPropId = BindingPath.Length > 0 ? AeroPropId.Resolve(BindingPath) : 0;
                    _cachedSegs = BindingEngine.PreparePathSegments(BindingPath);
                    _cachedSingleHop = (_cachedSegs == null || _cachedSegs.Length == 1);
                }
                BindingEngine.BindElementPrepared(
                    dest, _propId, Property, BindingElementName, _srcPropId, BindingPath,
                    _cachedSegs, _cachedSingleHop, BindingMode,
                    BindingConverter, BindingConverterKey, BindingConverterParam, BindingStringFormat,
                    BindingHasFallback, _bindingFallbackBox, false, null);
            } else if (BindingPath.Length > 0) {
                dest.ClearResourceReference(Property);
                if (!_bindPrepReady) {
                    _bindPrepReady = true;
                    _srcPropId = BindingPath.Length > 0 ? AeroPropId.Resolve(BindingPath) : 0;
                    _cachedSegs = BindingEngine.PreparePathSegments(BindingPath);
                    _cachedSingleHop = (_cachedSegs == null || _cachedSegs.Length == 1);
                }
                BindingEngine.BindPathPrepared(
                    dest, _propId, Property, null, _srcPropId, BindingPath,
                    _cachedSegs, _cachedSingleHop, BindingMode,
                    BindingConverter, BindingConverterKey, BindingConverterParam, BindingStringFormat,
                    BindingHasFallback, _bindingFallbackBox, false, null,
                    BindingRelativeMode, BindingAncestorType);
            } else {
                dest.ClearResourceReference(Property);
                if (Property == "ContextMenu" && _valueBox is UIElement) {
                    (_valueBox as UIElement).SetUiFlag(4096, true);
                }
                if (_propId > 0) {
                    dest.SetPropertyById(_propId, Property, _valueBox);
                } else {
                    dest.SetPropertyValue(Property, _valueBox);
                }
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
                    tr.ApplyMatchedCore(target, owningStyle);
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
    private int _propId = -2;
    private string _propIdName = "";
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
        if (target.Name == name) { return target; }
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
        if (_propId == -2 || _propIdName != Property) {
            _propIdName = Property;
            _propId = AeroPropId.Resolve(Property);
        }
        DependencyObject actual = _propId > 0 ? src.GetPropertyById(_propId, Property) : src.GetPropertyValue(Property);
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
        if (a is BoxedFloat && b is BoxedFloat) {
            return (a as BoxedFloat).Value == (b as BoxedFloat).Value;
        }
        string sa = DependencyObject.UnboxString(a, "");
        string sb = DependencyObject.UnboxString(b, "");
        return sa == sb;
    }
}

public class DataTrigger : TriggerBase {
    private DependencyObject _valueBox;
    private bool _hasExactFloatVal = false;
    private float _exactFloatVal = 0.0f;
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

    private void SyncExactFloatVal() {
        _hasExactFloatVal = false;
        _exactFloatVal = 0.0f;
        if (_valueBox is BoxedFloat) {
            float fv = (_valueBox as BoxedFloat).Value;
            if (ParseUtil.IsFinite(fv)) { _hasExactFloatVal = true; _exactFloatVal = fv; }
        } else if (_valueBox is BoxedInt && !(_valueBox is BoxedEnum)) {
            _hasExactFloatVal = true;
            _exactFloatVal = 1.0f * (_valueBox as BoxedInt).Value;
        } else if (_valueBox is BoxedString) {
            string s = (_valueBox as BoxedString).Value;
            if (s != null && s.Length > 0 && s.Length <= 9) {
                float parsed = ParseUtil.ParseFloat(s, ParseUtil.NaN());
                if (ParseUtil.IsFinite(parsed) && ("" + (int)parsed) == s) {
                    _hasExactFloatVal = true;
                    _exactFloatVal = parsed;
                }
            }
        }
    }

    public DataTrigger() : base() {}
    public DataTrigger(string bindingPath, object value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxObject(value);
        SyncExactFloatVal();
    }
    public DataTrigger(string bindingPath, string value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxString(value);
        SyncExactFloatVal();
    }
    public DataTrigger(string bindingPath, bool value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxBool(value);
    }
    public DataTrigger(string bindingPath, int value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxInt(value);
        SyncExactFloatVal();
    }
    public DataTrigger(string bindingPath, float value) : base() {
        BindingPath = bindingPath == null ? "" : bindingPath;
        _valueBox = DependencyObject.BoxFloat(value);
        SyncExactFloatVal();
    }
    public static DataTrigger CreateBox(string bindingPath, DependencyObject valueBox, string relMode = "", string ancestorType = "") {
        var dt = new DataTrigger();
        dt.BindingPath = bindingPath == null ? "" : bindingPath;
        dt._valueBox = valueBox;
        dt.SyncExactFloatVal();
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
        set { _valueBox = DependencyObject.BoxObject(value); SyncExactFloatVal(); }
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

    public static UIElement ResolveReactiveSourceElement(
        UIElement target,
        string elementName,
        string relativeSourceMode,
        string ancestorType,
        bool preferScopedElement) {
        if (target == null) { return null; }
        if (elementName != null && elementName.Length > 0) {
            if (preferScopedElement) {
                if (target.Name == elementName) { return target; }
                UIElement se = BindingEntry.FindElementScoped(target, elementName);
                if (se == null) { se = target.FindName(elementName); }
                if (se == null) {
                    UIElement root = target.RootElement();
                    if (root != null) { se = root.FindName(elementName); }
                }
                return se;
            }
            return Trigger.ResolveTemplateOrNamedElement(target, elementName);
        }
        if (relativeSourceMode == "Self") {
            return target;
        }
        if (relativeSourceMode == "TemplatedParent") {
            return target.TemplatedParent;
        }
        if (relativeSourceMode == "FindAncestor" || relativeSourceMode == "AncestorType" ||
            (ancestorType != null && ancestorType.Length > 0)) {
            UIElement anc = target;
            while (anc != null && ancestorType != null && ancestorType.Length > 0 &&
                   !BindingEntry.MatchesAncestorType(anc, ancestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            return anc;
        }
        return null;
    }

    private bool HookRelativeSource(UIElement target) {
        if (target == null || target._isDisposedTree) { return false; }
        UIElement se = (RelativeSourceMode == "Self")
            ? null
            : ResolveReactiveSourceElement(target, ElementName, RelativeSourceMode, RelativeSourceAncestorType, false);
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

    internal static bool _attachingFreshTemplate = false;

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
        if (!_attachingFreshTemplate && target.HasUiFlag(32768)) {
            for (int i = 0; i < _reactiveTriggers.Length; i += 1) {
                if (_reactiveTriggers[i] == dt && _reactiveTargets[i] == target) { return; }
            }
        } else {
            target.SetUiFlag(32768, true);
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
        UIElement se = ResolveReactiveSourceElement(
            target, ElementName, RelativeSourceMode, RelativeSourceAncestorType, false);
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

    private int _dtPropId = -2;
    private string _dtPropIdName = "";

    private DependencyObject ReadUiTriggerPropFast(UIElement el) {
        if (_dtPropId == -2 || _dtPropIdName != BindingPath) {
            _dtPropIdName = BindingPath;
            _dtPropId = AeroPropId.Resolve(BindingPath);
        }
        if (_dtPropId > 0) {
            DependencyObject v = el.GetPropertyById(_dtPropId, BindingPath);
            if (v != null || BindingEntry.IsGuaranteedUIElementProperty(el, BindingPath) || el.IsExplicitNullProperty(BindingPath)) {
                return v;
            }
        }
        DependencyObject dv = el.GetPropertyValue(BindingPath);
        if (dv != null || el.IsExplicitNullProperty(BindingPath)) { return dv; }
        return BindingEngine.ReadPathBox(el, BindingPath);
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
                actual = ReadUiTriggerPropFast(se);
            }
        } else if (RelativeSourceMode == "Self") {
            actual = ReadUiTriggerPropFast(target);
        } else if (RelativeSourceMode == "TemplatedParent") {
            UIElement tp = target.TemplatedParent;
            if (tp == null) { return false; }
            actual = ReadUiTriggerPropFast(tp);
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            if (anc == null) { return false; }
            actual = ReadUiTriggerPropFast(anc);
        } else {
            DependencyObject dcBox = target.EffectiveDataContextBox();
            if (dcBox == null) { return _valueBox == null; }
            if (dcBox is UIElement && !BindingPath.Contains(".")) {
                actual = ReadUiTriggerPropFast(dcBox as UIElement);
            } else {
                object dc = DependencyObject.Unbox(dcBox);
                if (dc == null) { return _valueBox == null; }
                actual = BindingEngine.ReadPathBox(dc, BindingPath);
            }
        }
        if (_hasExactFloatVal && actual != null) {
            if (actual is BoxedFloat) {
                return (actual as BoxedFloat).Value == _exactFloatVal;
            }
            if (actual is BoxedInt && !(actual is BoxedEnum)) {
                return (1.0f * (actual as BoxedInt).Value) == _exactFloatVal;
            }
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
    private int _condPropId = -2;
    private string _condPropIdName = "";

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

    private DependencyObject ReadCondUiPropFast(UIElement el) {
        if (_condPropId == -2 || _condPropIdName != BindingPath) {
            _condPropIdName = BindingPath;
            _condPropId = AeroPropId.Resolve(BindingPath);
        }
        if (_condPropId > 0) {
            DependencyObject v = el.GetPropertyById(_condPropId, BindingPath);
            if (v != null || BindingEntry.IsGuaranteedUIElementProperty(el, BindingPath) || el.IsExplicitNullProperty(BindingPath)) {
                return v;
            }
        }
        DependencyObject dv = el.GetPropertyValue(BindingPath);
        if (dv != null || el.IsExplicitNullProperty(BindingPath)) { return dv; }
        return BindingEngine.ReadPathBox(el, BindingPath);
    }

    public bool Evaluate(UIElement target) {
        if (target == null || BindingPath.Length == 0) { return false; }
        DependencyObject actual = null;
        if (ElementName.Length > 0) {
            UIElement se = ResolveElementSource(target);
            if (se == null) { return false; }
            actual = ReadCondUiPropFast(se);
            if (actual == null && !BindingEntry.IsGuaranteedUIElementProperty(se, BindingPath) && se.DataContext != null) {
                actual = BindingEngine.ReadPathBox(se.DataContext, BindingPath);
            }
        } else if (RelativeSourceMode == "Self") {
            actual = ReadCondUiPropFast(target);
        } else if (RelativeSourceMode == "TemplatedParent") {
            UIElement tp = target.TemplatedParent;
            if (tp == null) { return false; }
            actual = ReadCondUiPropFast(tp);
        } else if (RelativeSourceMode == "FindAncestor" || RelativeSourceMode == "AncestorType" ||
                   RelativeSourceAncestorType.Length > 0) {
            UIElement anc = target;
            while (anc != null && RelativeSourceAncestorType.Length > 0 && !BindingEntry.MatchesAncestorType(anc, RelativeSourceAncestorType)) {
                anc = BindingEntry.StepAncestor(anc);
            }
            if (anc == null) { return false; }
            actual = ReadCondUiPropFast(anc);
        } else {
            DependencyObject dcBox = target.EffectiveDataContextBox();
            if (dcBox == null) { return ValueBox == null; }
            if (dcBox is UIElement && !BindingPath.Contains(".")) {
                actual = ReadCondUiPropFast(dcBox as UIElement);
            } else {
                object dc = DependencyObject.Unbox(dcBox);
                if (dc == null) { return ValueBox == null; }
                actual = BindingEngine.ReadPathBox(dc, BindingPath);
            }
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
    private static int _styleChainVersion = 1;
    private Style[] _cachedChain;
    private int _cachedChainVer = -1;
    private int _cachedHasTrigVer = -1;
    private bool _cachedHasTrigVal = false;
    public Style BasedOn {
        get { return _basedOn; }
        set { _basedOn = value; _cachedResVer = -1; _styleChainVersion += 1; ResourceDictionary.BumpGlobalMutationEpoch(); }
    }
    private ResourceDictionary _resources;
    private static int _styleResourcesVersion = 0;
    private int _cachedResVer = -1;
    private bool _cachedHasAnyRes = false;
    public ResourceDictionary Resources {
        get {
            if (_resources == null) {
                _resources = new ResourceDictionary();
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

    public void AddTrigger(TriggerBase trigger) { if (trigger != null) { if (_triggers == null) { _triggers = new TriggerBase[0]; } _triggers.Push(trigger); _styleChainVersion += 1; } }
    public void AddDataTrigger(DataTrigger trigger) { AddTrigger(trigger); }
    public void AddEventTrigger(EventTrigger trigger) { AddTrigger(trigger); }
    public void AddBehaviorTrigger(IBehaviorTrigger trigger) {
        if (trigger != null) {
            if (_behaviorTriggers == null) { _behaviorTriggers = new IBehaviorTrigger[0]; }
            _behaviorTriggers.Push(trigger);
        }
    }

    private static Style[] CollectChain(Style leaf) {
        if (leaf == null) { return new Style[0]; }
        if (leaf._cachedChain != null && leaf._cachedChainVer == _styleChainVersion) {
            return leaf._cachedChain;
        }
        var chain = new Style[0];
        Style cur = leaf;
        while (cur != null && chain.Length < 32) {
            chain.Push(cur);
            cur = cur.BasedOn;
        }
        leaf._cachedChain = chain;
        leaf._cachedChainVer = _styleChainVersion;
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
        if (_cachedHasTrigVer == _styleChainVersion) { return _cachedHasTrigVal; }
        Style cur = this;
        int guard = 0;
        bool any = false;
        while (cur != null && guard < 16) {
            if (cur._triggers != null && cur._triggers.Length > 0) { any = true; break; }
            cur = cur.BasedOn;
            guard += 1;
        }
        _cachedHasTrigVal = any;
        _cachedHasTrigVer = _styleChainVersion;
        return any;
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
