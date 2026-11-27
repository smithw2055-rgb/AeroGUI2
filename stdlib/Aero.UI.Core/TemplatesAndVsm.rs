module Aero.UI;

import Aero.Native;
import RealScript.Math;

public class ControlTemplate : DependencyObject {
    public string TargetType { get; set; } = "";
    internal int _knownVsmState = -1;
    private Func<Control, UIElement> _factory;
    private TriggerBase[] _triggers;
    private UIElement[] _evaluatingOwners;
    private ResourceDictionary _resources;
    public ResourceDictionary Resources {
        get {
            if (_resources == null) {
                _resources = new ResourceDictionary();
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
        bool pushAmb = AmbientResources != null && ResourceDictionary.CurrentAmbientTop() != AmbientResources;
        if (pushAmb) { ResourceDictionary.PushAmbient(AmbientResources); }
        if (hasLocalRes) { ResourceDictionary.PushAmbient(_resources); }
        UIElement._xamlBuildDepth += 1;
        UIElement root = _factory(owner);
        UIElement._xamlBuildDepth -= 1;
        if (root != null) {
            if (AmbientResources != null && root.AmbientResources == null) {
                root.AmbientResources = AmbientResources;
            }
            if (hasLocalRes && (owner == null || owner.Template != this)) {
                root.Resources.Merge(_resources);
            }
            UIElement ownerEl = owner;
            if (ownerEl != null && root.TemplatedParent != ownerEl) {
                MarkTemplatedParentSubtree(root, ownerEl);
            }
            if (UIElement._xamlBuildDepth == 0) {
                root.ApplyStyleSubtree();
                root.RefreshDynamicResourcesRecursive();
                BindingEngine.FlushSubtree(root);
            }
        }
        if (hasLocalRes) { ResourceDictionary.PopAmbient(_resources); }
        if (pushAmb) { ResourceDictionary.PopAmbient(AmbientResources); }
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
    private bool _hasAncestorTriggers = false;
    public void AddTrigger(TriggerBase trigger) {
        if (trigger != null) {
            _triggers.Push(trigger);
            if (trigger is DataTrigger && (trigger as DataTrigger).RelativeSourceAncestorType.Length > 0) {
                _hasAncestorTriggers = true;
            } else if (trigger is MultiDataTrigger) {
                TriggerCondition[] cs = (trigger as MultiDataTrigger).Conditions();
                for (int i = 0; i < cs.Length; i += 1) {
                    if (cs[i] != null && cs[i].RelativeSourceAncestorType.Length > 0) {
                        _hasAncestorTriggers = true;
                        break;
                    }
                }
            }
        }
    }

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
        bool pushAmb = AmbientResources != null && ResourceDictionary.CurrentAmbientTop() != AmbientResources;
        if (pushAmb) { ResourceDictionary.PushAmbient(AmbientResources); }
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
            if (item != null && root.EffectiveDataContextBox() == null) {
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
        if (pushAmb) { ResourceDictionary.PopAmbient(AmbientResources); }
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
        bool prevFresh = DataTrigger._attachingFreshTemplate;
        DataTrigger._attachingFreshTemplate = true;
        for (int i = 0; i < list.Length; i += 1) {
            list[i].Attach(root);
        }
        DataTrigger._attachingFreshTemplate = prevFresh;
        if (_hasAncestorTriggers) {
            var hookedAncestors = new UIElement[0];
            DataTemplate.HookTemplateAncestors(list, root, hookedAncestors);
            TriggerBase.EvaluateGroup(list, root, null);
            root.Loaded += (object s, RoutedEventArgs e) => {
                DataTemplate.HookTemplateAncestors(list, root, hookedAncestors);
                TriggerBase.EvaluateGroup(list, root, null);
            };
        } else {
            TriggerBase.EvaluateGroup(list, root, null);
        }
        root.DataContextChanged += (object s, DependencyPropertyChangedEventArgs e) => {
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
