module Aero.UI;

import Aero.Native;
import RealScript.Math;

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
