module Aero.UI;

import Aero.Native;
import RealScript.Math;

public class Brush : DependencyObject {
    public float Opacity { get; set; } = 1.0f;
    public virtual uint ResolveRgba() { return 0u; }
    public virtual int ResolveGradId() { return -1; }
    public virtual uint ResolveRgbaUnscaled() { return ResolveRgba(); }
    public virtual int ResolveGradIdUnscaled() { return ResolveGradId(); }
    public virtual bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) { return false; }
    public virtual bool NeedsLoadedRefresh() { return false; }
    public virtual Brush ApplySubProperty(string suffix, DependencyObject val, bool allowClone) { return null; }
    public virtual DependencyObject ReadSubProperty(string suffix) { return null; }
    internal virtual void ReleaseNativeMutable() {}
}

public class SolidColorBrush : Brush {
    public override string ImplicitDataTypeKey() { return "SolidColorBrush"; }
    private uint _rgba;
    private string _colorStr = "";

    public SolidColorBrush() {}
    public SolidColorBrush(uint argb) {
        _rgba = argb;
    }
    public SolidColorBrush(string color) {
        _colorStr = color == null ? "" : color;
        _rgba = ParseUtil.ParseColor(_colorStr);
    }
    public SolidColorBrush(uint argb, float opacity) {
        _rgba = argb;
        Opacity = opacity;
    }

    public uint ColorRgba {
        get { return _rgba; }
        set { _rgba = value; _colorStr = ""; }
    }
    public string Color {
        get {
            if (_colorStr.Length == 0) { _colorStr = ParseUtil.FormatArgbHex(_rgba); }
            return _colorStr;
        }
        set { _colorStr = value == null ? "" : value; _rgba = ParseUtil.ParseColor(_colorStr); }
    }

    public override uint ResolveRgba() {
        return ParseUtil.ApplyOpacityToArgb(_rgba, Opacity);
    }
    public override uint ResolveRgbaUnscaled() {
        return _rgba;
    }
    public override string ToDisplayString() { return Color; }
}

public static class Brushes {
    private static SolidColorBrush _transparent;
    private static SolidColorBrush _black;
    private static SolidColorBrush _white;
    private static SolidColorBrush _defaultFg;

    public static SolidColorBrush Transparent() {
        if (_transparent == null) { _transparent = new SolidColorBrush(0u); }
        return _transparent;
    }
    public static SolidColorBrush Black() {
        if (_black == null) { _black = new SolidColorBrush(4278190080u); }
        return _black;
    }
    public static SolidColorBrush White() {
        if (_white == null) { _white = new SolidColorBrush(4294967295u); }
        return _white;
    }
    public static SolidColorBrush DefaultForeground() {
        if (_defaultFg == null) { _defaultFg = new SolidColorBrush(ParseUtil.PackArgb(255u, 241u, 245u, 249u)); }
        return _defaultFg;
    }
    public static SolidColorBrush Red() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 255u, 0u, 0u)); }
    public static SolidColorBrush Green() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 128u, 0u)); }
    public static SolidColorBrush Blue() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 0u, 255u)); }
    public static SolidColorBrush Yellow() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 255u, 255u, 0u)); }
    public static SolidColorBrush Cyan() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 0u, 255u, 255u)); }
    public static SolidColorBrush Gray() { return new SolidColorBrush(ParseUtil.PackArgb(255u, 128u, 128u, 128u)); }
    public static SolidColorBrush FromHex(string hex) { return new SolidColorBrush(hex); }
}

public static class ThemePalette {
    private static SolidColorBrush _slate50;
    private static SolidColorBrush _slate300;
    private static SolidColorBrush _slate400;
    private static SolidColorBrush _slate700;
    private static SolidColorBrush _slate800;
    private static SolidColorBrush _slate900;
    private static SolidColorBrush _sky400;

    public static SolidColorBrush Transparent() { return Brushes.Transparent(); }
    public static SolidColorBrush Slate50() {
        if (_slate50 == null) { _slate50 = new SolidColorBrush("#FFF8FAFC"); }
        return _slate50;
    }
    public static SolidColorBrush Slate300() {
        if (_slate300 == null) { _slate300 = new SolidColorBrush("#FFCBD5E1"); }
        return _slate300;
    }
    public static SolidColorBrush Slate400() {
        if (_slate400 == null) { _slate400 = new SolidColorBrush("#FF94A3B8"); }
        return _slate400;
    }
    public static SolidColorBrush Slate700() {
        if (_slate700 == null) { _slate700 = new SolidColorBrush("#FF334155"); }
        return _slate700;
    }
    public static SolidColorBrush Slate800() {
        if (_slate800 == null) { _slate800 = new SolidColorBrush("#FF1E293B"); }
        return _slate800;
    }
    public static SolidColorBrush Slate900() {
        if (_slate900 == null) { _slate900 = new SolidColorBrush("#FF0F172A"); }
        return _slate900;
    }
    public static SolidColorBrush Sky400() {
        if (_sky400 == null) { _sky400 = new SolidColorBrush("#FF38BDF8"); }
        return _sky400;
    }
}

public class GradientStop : DependencyObject {
    public override string ImplicitDataTypeKey() { return "GradientStop"; }
    public uint ColorRgba { get; set; }
    public string Color {
        get { return ParseUtil.FormatArgbHex(ColorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }
    public float Offset { get; set; }

    public GradientStop() {}
    public GradientStop(uint rgba, float offset) { ColorRgba = rgba; Offset = offset; }
    public GradientStop(string color, float offset) { ColorRgba = ParseUtil.ParseColor(color); Offset = offset; }
    public override string ToDisplayString() { return Color + "@" + Offset; }
}

public class GradientBrush : Brush {
    private GradientStop[] _stops;
    protected int _cachedGradId = -1;
    protected bool _dirty = true;
    protected int _cachedUnscaledGradId = -1;
    protected bool _unscaledDirty = true;
    private int _spreadMethod = 0;
    private int _mappingMode = 0;
    protected float _relTx = 0.0f;
    protected float _relTy = 0.0f;
    internal bool _isMutableClone = false;

    public int SpreadMethod {
        get { return _spreadMethod; }
        set { if (_spreadMethod != value) { _spreadMethod = value; _dirty = true; _unscaledDirty = true; } }
    }
    public int MappingMode {
        get { return _mappingMode; }
        set { if (_mappingMode != value) { _mappingMode = value; _dirty = true; _unscaledDirty = true; } }
    }
    public float RelativeTranslateX {
        get { return _relTx; }
        set { if (_relTx != value) { _relTx = value; _dirty = true; _unscaledDirty = true; } }
    }
    public float RelativeTranslateY {
        get { return _relTy; }
        set { if (_relTy != value) { _relTy = value; _dirty = true; _unscaledDirty = true; } }
    }

    public GradientBrush() { _stops = new GradientStop[0]; }

    public GradientStop[] GradientStops() { return _stops; }

    public void AddStop(GradientStop stop) {
        if (stop == null) { return; }
        _stops.Push(stop);
        _dirty = true;
        _unscaledDirty = true;
    }
    public void AddStop(uint rgba, float offset) {
        AddStop(new GradientStop(rgba, offset));
    }
    public void AddStop(string color, float offset) {
        AddStop(new GradientStop(color, offset));
    }
    public void AddStopObj(object colorObj, float offset) {
        if (colorObj is BoxedString) {
            AddStop((colorObj as BoxedString).Value, offset);
        } else if (colorObj is SolidColorBrush) {
            AddStop((colorObj as SolidColorBrush).ResolveRgba(), offset);
        }
    }
    public void ClearStops() {
        _stops.Clear();
        _dirty = true;
        _unscaledDirty = true;
    }

    public int StopCount() { return _stops.Length; }

    public string GetStopColor(int idx) {
        if (idx < 0 || idx >= _stops.Length) { return "#00000000"; }
        return _stops[idx].Color;
    }

    public float GetStopOffset(int idx) {
        if (idx < 0 || idx >= _stops.Length) { return 0.0f; }
        return _stops[idx].Offset;
    }

    public bool SetStopColor(int idx, string hex) {
        if (idx < 0 || idx >= _stops.Length) { return false; }
        _stops[idx].Color = hex == null ? "#00000000" : hex;
        _dirty = true;
        _unscaledDirty = true;
        return true;
    }

    public bool SetStopOffset(int idx, float off) {
        if (idx < 0 || idx >= _stops.Length) { return false; }
        _stops[idx].Offset = Math.Clampf(off, 0.0f, 1.0f);
        _dirty = true;
        _unscaledDirty = true;
        return true;
    }

    protected void CopyGradientStopsAndPropsTo(GradientBrush dst) {
        if (dst == null) { return; }
        dst.Opacity = Opacity;
        dst.SpreadMethod = SpreadMethod;
        dst.MappingMode = MappingMode;
        dst._relTx = _relTx;
        dst._relTy = _relTy;
        for (int i = 0; i < _stops.Length; i += 1) {
            dst.AddStop(_stops[i].ColorRgba, _stops[i].Offset);
        }
    }

    public virtual GradientBrush CloneBrush() {
        var copy = new GradientBrush();
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }

    public GradientBrush CloneMutableBrush() {
        GradientBrush copy = CloneBrush();
        if (copy != null) { copy._isMutableClone = true; }
        return copy;
    }

    internal override void ReleaseNativeMutable() {
        if (_isMutableClone) {
            if (_cachedGradId > 0) {
                Visual_ReleaseMutableGradient(_cachedGradId);
                _cachedGradId = -1;
            }
            if (_cachedUnscaledGradId > 0) {
                Visual_ReleaseMutableGradient(_cachedUnscaledGradId);
                _cachedUnscaledGradId = -1;
            }
        }
    }

    public override Brush ApplySubProperty(string suffix, DependencyObject val, bool allowClone) {
        if (suffix == null) { return null; }
        bool isTx = (suffix == "RelativeTransform.X" || suffix == "RelativeTransform.TranslateX");
        bool isTy = (suffix == "RelativeTransform.Y" || suffix == "RelativeTransform.TranslateY");
        bool isStop = GradientBrush.ParseStopSlotIndex(suffix) >= 0;
        if (!isTx && !isTy && !isStop) { return null; }
        GradientBrush target = (allowClone || !_isMutableClone) ? CloneMutableBrush() : this;
        if (isTx) {
            target.RelativeTranslateX = DependencyObject.UnboxFloat(val, 0.0f);
            return target;
        }
        if (isTy) {
            target.RelativeTranslateY = DependencyObject.UnboxFloat(val, 0.0f);
            return target;
        }
        target.TryApplyStopProperty(suffix, val);
        return target;
    }

    public override DependencyObject ReadSubProperty(string suffix) {
        if (suffix == "RelativeTransform.X" || suffix == "RelativeTransform.TranslateX") {
            return DependencyObject.BoxFloat(_relTx);
        }
        if (suffix == "RelativeTransform.Y" || suffix == "RelativeTransform.TranslateY") {
            return DependencyObject.BoxFloat(_relTy);
        }
        return TryReadStopProperty(suffix);
    }

    internal static int ParseStopSlotIndex(string suffix) {
        if (suffix == null || suffix.Length < 10) { return -1; }
        if (suffix.Substring(0, 4) != "Stop") { return -1; }
        if (suffix.EndsWith("Color")) {
            string numC = suffix.Substring(4, suffix.Length - 9);
            return ParseUtil.ParseInt(numC, -1);
        }
        if (suffix.EndsWith("Offset")) {
            string numO = suffix.Substring(4, suffix.Length - 10);
            return ParseUtil.ParseInt(numO, -1);
        }
        return -1;
    }

    internal bool TryApplyStopProperty(string suffix, DependencyObject value) {
        int idx = ParseStopSlotIndex(suffix);
        if (idx < 0 || idx >= _stops.Length) { return false; }
        if (suffix.Contains("Color")) {
            if (value is BoxedColor) {
                _stops[idx].ColorRgba = (value as BoxedColor).Value;
                _dirty = true;
                _unscaledDirty = true;
                return true;
            }
            return SetStopColor(idx, DependencyObject.UnboxString(value, "#00000000"));
        }
        return SetStopOffset(idx, DependencyObject.UnboxFloat(value, 0.0f));
    }

    internal DependencyObject TryReadStopProperty(string suffix) {
        int idx = ParseStopSlotIndex(suffix);
        if (idx < 0) { return null; }
        if (suffix.Contains("Color")) {
            return DependencyObject.BoxString(GetStopColor(idx));
        }
        return DependencyObject.BoxFloat(GetStopOffset(idx));
    }

    protected string BuildStopsSpec() {
        string spec = "";
        for (int i = 0; i < _stops.Length; i += 1) {
            if (i > 0) { spec = spec + ";"; }
            uint col = ParseUtil.ApplyOpacityToArgb(_stops[i].ColorRgba, Opacity);
            spec = spec + _stops[i].Offset + ":" + ParseUtil.FormatArgbHex(col);
        }
        return spec;
    }

    protected string BuildStopsSpecUnscaled() {
        string spec = "";
        for (int i = 0; i < _stops.Length; i += 1) {
            if (i > 0) { spec = spec + ";"; }
            spec = spec + _stops[i].Offset + ":" + ParseUtil.FormatArgbHex(_stops[i].ColorRgba);
        }
        return spec;
    }

    public override uint ResolveRgba() {
        if (_stops.Length > 0) { return _stops[0].ColorRgba; }
        return 0u;
    }

    public override string ToDisplayString() {
        return "grad:" + _stops.Length + ":" + BuildStopsSpec();
    }
}

public class LinearGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "LinearGradientBrush"; }
    private float _x0 = 0.0f;
    private float _y0 = 0.0f;
    private float _x1 = 0.0f;
    private float _y1 = 1.0f;

    public LinearGradientBrush() : base() {}
    public LinearGradientBrush(float x0, float y0, float x1, float y1) : base() {
        _x0 = x0; _y0 = y0; _x1 = x1; _y1 = y1;
    }
    public LinearGradientBrush(Point start, Point end) : base() {
        if (start != null) { _x0 = start.X; _y0 = start.Y; }
        if (end != null) { _x1 = end.X; _y1 = end.Y; }
    }

    public float X0 { get { return _x0; } set { _x0 = value; _dirty = true; _unscaledDirty = true; } }
    public float Y0 { get { return _y0; } set { _y0 = value; _dirty = true; _unscaledDirty = true; } }
    public float X1 { get { return _x1; } set { _x1 = value; _dirty = true; _unscaledDirty = true; } }
    public float Y1 { get { return _y1; } set { _y1 = value; _dirty = true; _unscaledDirty = true; } }
    public Point StartPoint {
        get { return new Point(_x0, _y0); }
        set { if (value != null) { _x0 = value.X; _y0 = value.Y; _dirty = true; _unscaledDirty = true; } }
    }
    public Point EndPoint {
        get { return new Point(_x1, _y1); }
        set { if (value != null) { _x1 = value.X; _y1 = value.Y; _dirty = true; _unscaledDirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            if (_isMutableClone) {
                _cachedGradId = Visual_RegisterMutableLinearGradient(
                    _cachedGradId, _x0 + _relTx, _y0 + _relTy, _x1 + _relTx, _y1 + _relTy,
                    SpreadMethod, MappingMode, BuildStopsSpec());
            } else {
                _cachedGradId = Visual_RegisterLinearGradient(
                    _x0 + _relTx, _y0 + _relTy, _x1 + _relTx, _y1 + _relTy,
                    SpreadMethod, MappingMode, BuildStopsSpec());
            }
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            if (_isMutableClone) {
                _cachedUnscaledGradId = Visual_RegisterMutableLinearGradient(
                    _cachedUnscaledGradId, _x0 + _relTx, _y0 + _relTy, _x1 + _relTx, _y1 + _relTy,
                    SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            } else {
                _cachedUnscaledGradId = Visual_RegisterLinearGradient(
                    _x0 + _relTx, _y0 + _relTy, _x1 + _relTx, _y1 + _relTy,
                    SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            }
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new LinearGradientBrush(_x0, _y0, _x1, _y1);
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }
}

public class RadialGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "RadialGradientBrush"; }
    private float _cx = 0.5f;
    private float _cy = 0.5f;
    private float _ox = 0.5f;
    private float _oy = 0.5f;
    private float _rx = 0.5f;
    private float _ry = 0.5f;

    public RadialGradientBrush() : base() {}
    public RadialGradientBrush(float cx, float cy, float rx, float ry) : base() {
        _cx = cx; _cy = cy; _ox = cx; _oy = cy; _rx = rx; _ry = ry;
    }
    public RadialGradientBrush(float cx, float cy, float ox, float oy, float rx, float ry) : base() {
        _cx = cx; _cy = cy; _ox = ox; _oy = oy; _rx = rx; _ry = ry;
    }

    public float CenterX { get { return _cx; } set { _cx = value; _dirty = true; _unscaledDirty = true; } }
    public float CenterY { get { return _cy; } set { _cy = value; _dirty = true; _unscaledDirty = true; } }
    public float OriginX { get { return _ox; } set { _ox = value; _dirty = true; _unscaledDirty = true; } }
    public float OriginY { get { return _oy; } set { _oy = value; _dirty = true; _unscaledDirty = true; } }
    public float RadiusX { get { return _rx; } set { _rx = value; _dirty = true; _unscaledDirty = true; } }
    public float RadiusY { get { return _ry; } set { _ry = value; _dirty = true; _unscaledDirty = true; } }
    public Point Center {
        get { return new Point(_cx, _cy); }
        set { if (value != null) { _cx = value.X; _cy = value.Y; _dirty = true; _unscaledDirty = true; } }
    }
    public Point GradientOrigin {
        get { return new Point(_ox, _oy); }
        set { if (value != null) { _ox = value.X; _oy = value.Y; _dirty = true; _unscaledDirty = true; } }
    }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterRadialGradient(
                _cx, _cy, _ox, _oy, _rx, _ry, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            _cachedUnscaledGradId = Visual_RegisterRadialGradient(
                _cx, _cy, _ox, _oy, _rx, _ry, SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new RadialGradientBrush(_cx, _cy, _ox, _oy, _rx, _ry);
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }
}

public class ConicGradientBrush : GradientBrush {
    public override string ImplicitDataTypeKey() { return "ConicGradientBrush"; }
    private float _cx = 0.5f;
    private float _cy = 0.5f;

    public ConicGradientBrush() : base() {}
    public float CenterX { get { return _cx; } set { _cx = value; _dirty = true; _unscaledDirty = true; } }
    public float CenterY { get { return _cy; } set { _cy = value; _dirty = true; _unscaledDirty = true; } }

    public override int ResolveGradId() {
        if (_dirty) {
            _cachedGradId = Visual_RegisterConicGradient(
                _cx, _cy, SpreadMethod, MappingMode, BuildStopsSpec());
            _dirty = false;
        }
        return _cachedGradId;
    }

    public override int ResolveGradIdUnscaled() {
        if (_unscaledDirty) {
            _cachedUnscaledGradId = Visual_RegisterConicGradient(
                _cx, _cy, SpreadMethod, MappingMode, BuildStopsSpecUnscaled());
            _unscaledDirty = false;
        }
        return _cachedUnscaledGradId;
    }

    public override GradientBrush CloneBrush() {
        var copy = new ConicGradientBrush();
        copy.CenterX = _cx;
        copy.CenterY = _cy;
        CopyGradientStopsAndPropsTo(copy);
        return copy;
    }
}

public class TransformData : DependencyObject {
    public float TX = 0.0f;
    public float TY = 0.0f;
    public float SX = 1.0f;
    public float SY = 1.0f;
    public float RotDeg = 0.0f;
    public float SkewX = 0.0f;
    public float SkewY = 0.0f;
    public bool HasExplicitCenter = false;
    private static TransformData _scratch;

    internal static TransformData SharedScratch() {
        if (_scratch == null) { _scratch = new TransformData(); }
        _scratch.TX = 0.0f;
        _scratch.TY = 0.0f;
        _scratch.SX = 1.0f;
        _scratch.SY = 1.0f;
        _scratch.RotDeg = 0.0f;
        _scratch.SkewX = 0.0f;
        _scratch.SkewY = 0.0f;
        _scratch.HasExplicitCenter = false;
        return _scratch;
    }
}

public class Transform : DependencyObject {
    internal int _depPropEpoch = 1;
    internal override int DepPropEpoch() { return _depPropEpoch; }
    private string[] _attachedKeys;
    private DependencyObject[] _attachedVals;

    public virtual void Accumulate(TransformData acc) {}

    public void SetAttachedValue(string key, DependencyObject val) {
        if (key == null || key.Length == 0) { return; }
        _depPropEpoch += 1;
        if (_attachedKeys == null) {
            _attachedKeys = new string[0];
            _attachedVals = new DependencyObject[0];
        }
        for (int i = 0; i < _attachedKeys.Length; i += 1) {
            if (_attachedKeys[i] == key) {
                _attachedVals[i] = val;
                return;
            }
        }
        _attachedKeys.Push(key);
        _attachedVals.Push(val);
    }

    public DependencyObject GetAttachedValue(string key) {
        if (key == null || _attachedKeys == null) { return null; }
        for (int i = 0; i < _attachedKeys.Length; i += 1) {
            if (_attachedKeys[i] == key) { return _attachedVals[i]; }
        }
        return null;
    }
}

public class TranslateTransform : Transform {
    public override string ImplicitDataTypeKey() { return "TranslateTransform"; }
    public float X { get; set; }
    public float Y { get; set; }
    public TranslateTransform() {}
    public TranslateTransform(float x, float y) { X = x; Y = y; }
    public override void Accumulate(TransformData acc) {
        acc.TX = acc.TX + X;
        acc.TY = acc.TY + Y;
    }
}

public class ScaleTransform : Transform {
    public override string ImplicitDataTypeKey() { return "ScaleTransform"; }
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private bool _hasExplicitCenter = false;
    public float ScaleX { get; set; } = 1.0f;
    public float ScaleY { get; set; } = 1.0f;
    public float CenterX {
        get { return _centerX; }
        set { _centerX = value; _hasExplicitCenter = true; }
    }
    public float CenterY {
        get { return _centerY; }
        set { _centerY = value; _hasExplicitCenter = true; }
    }
    public ScaleTransform() {}
    public ScaleTransform(float sx, float sy) { ScaleX = sx; ScaleY = sy; }
    public ScaleTransform(float sx, float sy, float cx, float cy) {
        ScaleX = sx; ScaleY = sy; _centerX = cx; _centerY = cy; _hasExplicitCenter = true;
    }
    public override void Accumulate(TransformData acc) {
        if (_hasExplicitCenter || _centerX != 0.0f || _centerY != 0.0f) {
            acc.HasExplicitCenter = true;
        }
        if (_centerX != 0.0f || _centerY != 0.0f) {
            acc.TX = (acc.TX - _centerX) * ScaleX + _centerX;
            acc.TY = (acc.TY - _centerY) * ScaleY + _centerY;
        }
        acc.SX = acc.SX * ScaleX;
        acc.SY = acc.SY * ScaleY;
    }
}

public class RotateTransform : Transform {
    public override string ImplicitDataTypeKey() { return "RotateTransform"; }
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private bool _hasExplicitCenter = false;
    public float Angle { get; set; }
    public float CenterX {
        get { return _centerX; }
        set { _centerX = value; _hasExplicitCenter = true; }
    }
    public float CenterY {
        get { return _centerY; }
        set { _centerY = value; _hasExplicitCenter = true; }
    }
    public RotateTransform() {}
    public RotateTransform(float angle) { Angle = angle; }
    public RotateTransform(float angle, float cx, float cy) {
        Angle = angle; _centerX = cx; _centerY = cy; _hasExplicitCenter = true;
    }
    public override void Accumulate(TransformData acc) {
        if (_hasExplicitCenter || _centerX != 0.0f || _centerY != 0.0f) {
            acc.HasExplicitCenter = true;
        }
        if (Angle != 0.0f && (_centerX != 0.0f || _centerY != 0.0f)) {
            float rad = Angle * 0.0174532925f;
            float s = 0.0f;
            float c = 1.0f;
            if (ParseUtil.IsFinite(rad)) {
                float rawS = Math.Sinf(rad);
                float rawC = Math.Cosf(rad);
                if (ParseUtil.IsFinite(rawS) && ParseUtil.IsFinite(rawC)) {
                    s = rawS;
                    c = rawC;
                }
            }
            float dx = acc.TX - _centerX;
            float dy = acc.TY - _centerY;
            acc.TX = dx * c - dy * s + _centerX;
            acc.TY = dx * s + dy * c + _centerY;
        }
        acc.RotDeg = acc.RotDeg + Angle;
    }
}

public class SkewTransform : Transform {
    public override string ImplicitDataTypeKey() { return "SkewTransform"; }
    public float AngleX { get; set; }
    public float AngleY { get; set; }
    public float CenterX { get; set; }
    public float CenterY { get; set; }
    public SkewTransform() {}
    public SkewTransform(float ax, float ay) { AngleX = ax; AngleY = ay; }
    public SkewTransform(float ax, float ay, float cx, float cy) {
        AngleX = ax; AngleY = ay; CenterX = cx; CenterY = cy;
    }
    public override void Accumulate(TransformData acc) {
        acc.SkewX = acc.SkewX + AngleX;
        acc.SkewY = acc.SkewY + AngleY;
    }
}

public class TransformGroup : Transform {
    public override string ImplicitDataTypeKey() { return "TransformGroup"; }
    private Transform[] _children;
    private ArrayCollectionBox _childrenBoxCache;
    public TransformGroup() { _children = new Transform[0]; }
    public Transform[] Children() { return _children; }
    public DependencyObject ChildrenBox() {
        if (_childrenBoxCache != null) { return _childrenBoxCache; }
        var box = new ArrayCollectionBox();
        for (int i = 0; i < _children.Length; i += 1) { box.AddBox(_children[i]); }
        _childrenBoxCache = box;
        return box;
    }
    internal override DependencyObject GetBindingPropertyValue(string propName) {
        if (propName == "Children") { return ChildrenBox(); }
        return base.GetBindingPropertyValue(propName);
    }
    public void Add(Transform t) { if (t != null) { _children.Push(t); _childrenBoxCache = null; _depPropEpoch += 1; } }
    public void Clear() { _children.Clear(); _childrenBoxCache = null; _depPropEpoch += 1; }
    public override void Accumulate(TransformData acc) {
        for (int i = 0; i < _children.Length; i += 1) {
            _children[i].Accumulate(acc);
        }
    }
}

internal class EffectChangeHook {
    public Action Callback;
    public bool Active = true;
    public EffectChangeHook(Action cb) { Callback = cb; }
}

public class Effect : DependencyObject {
    private UIEffectKind _kind = UIEffectKind.None;
    private float _radius = 0.0f;
    private float _depth = 0.0f;
    private float _direction = 315.0f;
    private uint _colorRgba = 4278190080u;
    private float _opacity = 1.0f;
    private EffectChangeHook[] _listeners;

    internal EffectChangeHook AddChangedListener(Action cb) {
        if (cb == null) { return null; }
        if (_listeners == null) { _listeners = new EffectChangeHook[0]; }
        var hook = new EffectChangeHook(cb);
        _listeners.Push(hook);
        return hook;
    }

    internal void RemoveChangedListener(EffectChangeHook hook) {
        if (hook == null) { return; }
        hook.Active = false;
        if (_listeners == null) { return; }
        for (int i = _listeners.Length - 1; i >= 0; i -= 1) {
            if (_listeners[i] == hook || !_listeners[i].Active) {
                _listeners.RemoveAt(i);
            }
        }
    }

    internal void ClearChangedListeners() {
        if (_listeners == null) { return; }
        for (int i = 0; i < _listeners.Length; i += 1) {
            _listeners[i].Active = false;
        }
        _listeners.Clear();
    }

    private void NotifyChanged() {
        if (_listeners == null || _listeners.Length == 0) { return; }
        for (int i = _listeners.Length - 1; i >= 0; i -= 1) {
            EffectChangeHook h = _listeners[i];
            if (h == null || !h.Active || h.Callback == null) {
                _listeners.RemoveAt(i);
                continue;
            }
            var cb = h.Callback;
            cb();
        }
    }

    public UIEffectKind Kind {
        get { return _kind; }
        set { if (_kind == value) { return; } _kind = value; NotifyChanged(); }
    }
    public float Radius {
        get { return _radius; }
        set { if (_radius == value) { return; } _radius = value; NotifyChanged(); }
    }
    public float Depth {
        get { return _depth; }
        set { if (_depth == value) { return; } _depth = value; NotifyChanged(); }
    }
    public float Direction {
        get { return _direction; }
        set { if (_direction == value) { return; } _direction = value; NotifyChanged(); }
    }
    public uint ColorRgba {
        get { return _colorRgba; }
        set { if (_colorRgba == value) { return; } _colorRgba = value; NotifyChanged(); }
    }
    public float Opacity {
        get { return _opacity; }
        set { if (_opacity == value) { return; } _opacity = value; NotifyChanged(); }
    }

    public string Color {
        get { return ParseUtil.FormatArgbHex(_colorRgba); }
        set { ColorRgba = ParseUtil.ParseColor(value); }
    }

    public bool SetPropertyValue(string propName, DependencyObject value) {
        if (propName == null) { return false; }
        if (propName == "Radius" || propName == "BlurRadius" || propName == "Size" || propName == "PixelSize" || propName == "Intensity" || propName == "Factor") {
            Radius = DependencyObject.UnboxFloat(value, _radius);
            return true;
        }
        if (propName == "Depth" || propName == "ShadowDepth") {
            Depth = DependencyObject.UnboxFloat(value, _depth);
            return true;
        }
        if (propName == "Direction" || propName == "Angle") {
            Direction = DependencyObject.UnboxFloat(value, _direction);
            return true;
        }
        if (propName == "Opacity") {
            Opacity = Math.Clampf(DependencyObject.UnboxFloat(value, _opacity), 0.0f, 1.0f);
            return true;
        }
        if (propName == "Color") {
            if (value is BoxedColor) { ColorRgba = (value as BoxedColor).Value; }
            else if (value is BoxedInt) { ColorRgba = (uint)(value as BoxedInt).Value; }
            else { Color = DependencyObject.UnboxString(value, Color); }
            return true;
        }
        return false;
    }

    public DependencyObject GetPropertyValue(string propName) {
        if (propName == "Radius" || propName == "BlurRadius" || propName == "Size" || propName == "PixelSize" || propName == "Intensity" || propName == "Factor") {
            return DependencyObject.BoxFloat(_radius);
        }
        if (propName == "Depth" || propName == "ShadowDepth") { return DependencyObject.BoxFloat(_depth); }
        if (propName == "Direction" || propName == "Angle") { return DependencyObject.BoxFloat(_direction); }
        if (propName == "Opacity") { return DependencyObject.BoxFloat(_opacity); }
        if (propName == "Color") { return DependencyObject.BoxString(Color); }
        return null;
    }
}

public class UIEffect : Effect {
    public UIEffect() {}
    public UIEffect(UIEffectKind kind, float radius = 0.0f) { Kind = kind; Radius = radius; }
}

public class BlurEffect : Effect {
    public BlurEffect() { Kind = UIEffectKind.Blur; Radius = 8.0f; }
    public BlurEffect(float radius) { Kind = UIEffectKind.Blur; Radius = radius; }
}

public class DropShadowEffect : Effect {
    public DropShadowEffect() {
        Kind = UIEffectKind.DropShadow;
        Radius = 10.0f;
        Depth = 4.0f;
        Direction = 315.0f;
        Opacity = 0.45f;
        ColorRgba = ParseUtil.PackArgb(255u, 0u, 0u, 0u);
    }
    public DropShadowEffect(float blurRadius, float shadowDepth = 4.0f, float direction = 315.0f, string color = "#FF000000", float opacity = 0.45f) {
        Kind = UIEffectKind.DropShadow;
        Radius = blurRadius;
        Depth = shadowDepth;
        Direction = direction;
        ColorRgba = ParseUtil.ParseColor(color);
        Opacity = opacity;
    }
    public float BlurRadius { get { return Radius; } set { Radius = value; } }
    public float ShadowDepth { get { return Depth; } set { Depth = value; } }
}

public class BackdropBlurEffect : Effect {
    public BackdropBlurEffect(float radius = 16.0f) { Kind = UIEffectKind.BackdropBlur; Radius = radius; }
}

public class TintEffect : Effect {
    public TintEffect(string color = "#FF2266FF", float opacity = 0.5f) {
        Kind = UIEffectKind.Tint;
        ColorRgba = ParseUtil.ParseColor(color);
        Opacity = opacity;
    }
}

public class PixelateEffect : Effect {
    public PixelateEffect(float pixelSize = 6.0f) { Kind = UIEffectKind.Pixelate; Radius = pixelSize; }
}

public class ChromaticAberrationEffect : Effect {
    public ChromaticAberrationEffect(float intensity = 3.0f) { Kind = UIEffectKind.ChromaticAberration; Radius = intensity; }
}

public class DirectionalBlurEffect : Effect {
    public DirectionalBlurEffect(float radius = 8.0f, float direction = 0.0f) {
        Kind = UIEffectKind.DirectionalBlur; Radius = radius; Direction = direction;
    }
}

public class SaturationEffect : Effect {
    public SaturationEffect(float factor = 1.0f) { Kind = UIEffectKind.Saturation; Radius = factor; }
}

public class ImageBrush : Brush {
    public override string ImplicitDataTypeKey() { return "ImageBrush"; }
    private string _imageSource = "";
    private bool _plasmaUriDirty = false;
    public string ImageSource {
        get {
            if (IsPlasmaShader && _plasmaUriDirty) {
                RebuildPlasmaUriNow();
            }
            return _imageSource;
        }
        set {
            _imageSource = value == null ? "" : value;
            _plasmaUriDirty = false;
        }
    }
    public Stretch Stretch { get; set; } = Stretch.Fill;
    public string TileMode { get; set; } = "None";
    public string Viewbox { get; set; } = "0,0,1,1";
    public string Viewport { get; set; } = "";
    public string ViewportUnits { get; set; } = "RelativeToBoundingBox";
    public uint TintRgba { get; set; } = 0u;
    public bool IsPlasmaShader { get; set; } = false;
    private float _plasmaScaleX = 64.0f;
    private float _plasmaScaleY = 64.0f;
    private float _plasmaTime = 0.0f;
    private float _plasmaSeed = 100.0f;
    private string _plasmaColor = "#FF01BCFF";
    private uint _plasmaColorRgba = 4294949889u;
    private float _plasmaVw = 300.0f;
    private float _plasmaVh = 90.0f;

    public ImageBrush() {
        TintRgba = ParseUtil.PackArgb(48u, 32u, 56u, 96u);
        _plasmaColorRgba = ParseUtil.PackArgb(255u, 1u, 188u, 255u);
    }
    public ImageBrush(string source) {
        _imageSource = source == null ? "" : source;
        TintRgba = ParseUtil.PackArgb(48u, 32u, 56u, 96u);
        _plasmaColorRgba = ParseUtil.PackArgb(255u, 1u, 188u, 255u);
    }

    public void EnablePlasmaShader(float sx, float sy, float timeSec, float seed, string colorHex, float vw, float vh) {
        IsPlasmaShader = true;
        _plasmaScaleX = sx;
        _plasmaScaleY = sy;
        _plasmaTime = timeSec;
        _plasmaSeed = seed;
        _plasmaColor = (colorHex == null || colorHex.Length == 0) ? "#FF01BCFF" : colorHex;
        _plasmaColorRgba = ParseUtil.ParseColor(_plasmaColor);
        if (_plasmaColorRgba == 0u) { _plasmaColorRgba = ParseUtil.PackArgb(255u, 1u, 188u, 255u); }
        _plasmaVw = vw > 1.0f ? vw : 300.0f;
        _plasmaVh = vh > 1.0f ? vh : 90.0f;
        _plasmaUriDirty = true;
    }

    public float PlasmaScaleX { get { return _plasmaScaleX; } set { _plasmaScaleX = value; _plasmaUriDirty = true; } }
    public float PlasmaScaleY { get { return _plasmaScaleY; } set { _plasmaScaleY = value; _plasmaUriDirty = true; } }
    public float PlasmaTime {
        get { return _plasmaTime; }
        set {
            if (Math.Absf(value - _plasmaTime) >= 0.004f) {
                _plasmaTime = value;
                _plasmaUriDirty = true;
            }
        }
    }
    public float PlasmaSeed { get { return _plasmaSeed; } set { _plasmaSeed = value; _plasmaUriDirty = true; } }
    public uint PlasmaColorRgba {
        get { return _plasmaColorRgba; }
        set {
            if (_plasmaColorRgba != value) {
                _plasmaColorRgba = value;
                _plasmaColor = "";
                _plasmaUriDirty = true;
            }
        }
    }
    public string PlasmaColor {
        get {
            if (_plasmaColor.Length == 0) { _plasmaColor = ParseUtil.FormatArgbHex(_plasmaColorRgba); }
            return _plasmaColor;
        }
        set {
            string next = (value == null || value.Length == 0) ? "#FF01BCFF" : value;
            if (_plasmaColor != next) {
                _plasmaColor = next;
                _plasmaColorRgba = ParseUtil.ParseColor(_plasmaColor);
                if (_plasmaColorRgba == 0u) { _plasmaColorRgba = ParseUtil.PackArgb(255u, 1u, 188u, 255u); }
                _plasmaUriDirty = true;
            }
        }
    }

    private void RebuildPlasmaUriNow() {
        _plasmaUriDirty = false;
        if (!IsPlasmaShader) { return; }
        if (_plasmaColor.Length == 0) { _plasmaColor = ParseUtil.FormatArgbHex(_plasmaColorRgba); }
        _imageSource = "shader://plasma?sx=" + _plasmaScaleX +
                       "&sy=" + _plasmaScaleY +
                       "&seed=" + _plasmaSeed +
                       "&time=" + _plasmaTime +
                       "&color=" + _plasmaColor +
                       "&vw=" + _plasmaVw +
                       "&vh=" + _plasmaVh;
    }

    public ImageBrush CloneImageBrush() {
        var c = new ImageBrush(ImageSource);
        c.Stretch = Stretch;
        c.TileMode = TileMode;
        c.Viewbox = Viewbox;
        c.Viewport = Viewport;
        c.ViewportUnits = ViewportUnits;
        c.TintRgba = TintRgba;
        c.Opacity = Opacity;
        if (IsPlasmaShader) {
            c.EnablePlasmaShader(_plasmaScaleX, _plasmaScaleY, _plasmaTime, _plasmaSeed, PlasmaColor, _plasmaVw, _plasmaVh);
        }
        return c;
    }

    public override uint ResolveRgba() { return TintRgba; }

    public override bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) {
        uint alpha = tintByOpacity ? (uint)Math.Clampf(Opacity * 255.0f + 0.5f, 0.0f, 255.0f) : 255u;
        uint tint = ParseUtil.PackArgb(alpha, 255u, 255u, 255u);
        Visual_SetVisualBrushSource(h, ParseUtil.EmptyHandle(), 1, 1);
        if (IsPlasmaShader && ParseUtil.IsNativeReady()) {
            Visual_SetPlasmaImage(
                h,
                _plasmaScaleX,
                _plasmaScaleY,
                _plasmaTime,
                _plasmaSeed,
                _plasmaColorRgba,
                _plasmaVw,
                _plasmaVh,
                EnumConvert.ToInt(Stretch),
                EnumConvert.ToInt(StretchDirection.Both),
                tint);
        } else {
            Visual_SetImage(
                h,
                ImageSource,
                EnumConvert.ToInt(Stretch),
                EnumConvert.ToInt(StretchDirection.Both),
                tint);
        }
        UIElement.ApplyImageBrushViewbox(h, Viewbox);
        UIElement.ApplyImageBrushViewport(h, Viewport, ViewportUnits);
        Visual_SetImageTile(h, TileMode == "Tile");
        return true;
    }

    public override Brush ApplySubProperty(string suffix, DependencyObject val, bool allowClone) {
        if (suffix != "ImageSource" && suffix != "PlasmaSeed" && suffix != "PlasmaColor" &&
            suffix != "PlasmaTime" && suffix != "PlasmaScaleX" && suffix != "PlasmaScaleY") {
            return null;
        }
        ImageBrush target = allowClone ? CloneImageBrush() : this;
        if (suffix == "ImageSource") { target.ImageSource = DependencyObject.UnboxString(val, ""); }
        else if (suffix == "PlasmaSeed") { target.PlasmaSeed = DependencyObject.UnboxFloat(val, 100.0f); }
        else if (suffix == "PlasmaColor") {
            if (val is BoxedColor) { target.PlasmaColorRgba = (val as BoxedColor).Value; }
            else { target.PlasmaColor = DependencyObject.UnboxString(val, "#FF01BCFF"); }
        }
        else if (suffix == "PlasmaTime") { target.PlasmaTime = DependencyObject.UnboxFloat(val, 0.0f); }
        else if (suffix == "PlasmaScaleX") { target.PlasmaScaleX = DependencyObject.UnboxFloat(val, 64.0f); }
        else if (suffix == "PlasmaScaleY") { target.PlasmaScaleY = DependencyObject.UnboxFloat(val, 64.0f); }
        return target;
    }

    public override DependencyObject ReadSubProperty(string suffix) {
        if (suffix == "ImageSource") { return DependencyObject.BoxString(ImageSource); }
        if (suffix == "PlasmaSeed") { return DependencyObject.BoxFloat(PlasmaSeed); }
        if (suffix == "PlasmaColor") { return DependencyObject.BoxString(PlasmaColor); }
        if (suffix == "PlasmaTime") { return DependencyObject.BoxFloat(PlasmaTime); }
        if (suffix == "PlasmaScaleX") { return DependencyObject.BoxFloat(PlasmaScaleX); }
        if (suffix == "PlasmaScaleY") { return DependencyObject.BoxFloat(PlasmaScaleY); }
        return null;
    }
}

public class VisualBrush : Brush {
    public override string ImplicitDataTypeKey() { return "VisualBrush"; }
    public UIElement Visual { get; set; }
    public string VisualElementName { get; set; } = "";
    public Stretch Stretch { get; set; } = Stretch.Fill;
    public string TileMode { get; set; } = "None";
    public string Viewbox { get; set; } = "0,0,1,1";
    public string Viewport { get; set; } = "";
    public string ViewportUnits { get; set; } = "RelativeToBoundingBox";
    public string AlignmentX { get; set; } = "Center";
    public string AlignmentY { get; set; } = "Center";

    public VisualBrush() {}
    public VisualBrush(UIElement visual) { Visual = visual; }

    public UIElement ResolveVisual(UIElement host) {
        if (Visual != null) { return Visual; }
        if (VisualElementName != null && VisualElementName.Length > 0 && host != null) {
            UIElement found = host.FindName(VisualElementName);
            UIElement prev = host;
            UIElement cur = host.Parent != null ? host.Parent : host._logicalParent;
            int guard = 0;
            while (found == null && cur != null && guard < 128) {
                guard += 1;
                found = cur.FindNameExcluding(VisualElementName, prev);
                prev = cur;
                cur = cur.Parent != null ? cur.Parent : cur._logicalParent;
            }
            if (found == null) {
                UIElement root = host.RootElement();
                if (root != null) { found = root.FindName(VisualElementName); }
            }
            if (found != null) { Visual = found; }
            return found;
        }
        return null;
    }

    public static int ParseAlignX(string s) {
        if (s == null) { return 1; }
        string t = s.Trim();
        if (t == "Left" || t == "0") { return 0; }
        if (t == "Right" || t == "2") { return 2; }
        return 1;
    }

    public static int ParseAlignY(string s) {
        if (s == null) { return 1; }
        string t = s.Trim();
        if (t == "Top" || t == "0") { return 0; }
        if (t == "Bottom" || t == "2") { return 2; }
        return 1;
    }

    public override bool NeedsLoadedRefresh() { return true; }

    public override bool ApplySurfaceToNative(UIElement owner, handle h, bool tintByOpacity) {
        UIElement srcEl = ResolveVisual(owner);
        uint alpha = tintByOpacity ? (uint)Math.Clampf(Opacity * 255.0f + 0.5f, 0.0f, 255.0f) : 255u;
        uint tint = ParseUtil.PackArgb(alpha, 255u, 255u, 255u);
        Visual_SetImage(
            h,
            "",
            EnumConvert.ToInt(Stretch),
            EnumConvert.ToInt(StretchDirection.Both),
            tint);
        UIElement.ApplyImageBrushViewbox(h, Viewbox);
        UIElement.ApplyImageBrushViewport(h, Viewport, ViewportUnits);
        Visual_SetImageTile(h, TileMode == "Tile");
        handle srcH = (srcEl != null && ParseUtil.IsValidHandle(srcEl.__nativeHandle))
            ? srcEl.__nativeHandle
            : ParseUtil.EmptyHandle();
        Visual_SetVisualBrushSource(
            h,
            srcH,
            ParseAlignX(AlignmentX),
            ParseAlignY(AlignmentY));
        return true;
    }
}

public class PlaneProjection : DependencyObject {
    public float RotationX { get; set; } = 0.0f;
    public float RotationY { get; set; } = 0.0f;
    public float RotationZ { get; set; } = 0.0f;
    public float LocalOffsetX { get; set; } = 0.0f;
    public float LocalOffsetY { get; set; } = 0.0f;
    public float LocalOffsetZ { get; set; } = 0.0f;
    public float GlobalOffsetX { get; set; } = 0.0f;
    public float GlobalOffsetY { get; set; } = 0.0f;
    public float GlobalOffsetZ { get; set; } = 0.0f;
    public float CenterOfRotationX { get; set; } = 0.5f;
    public float CenterOfRotationY { get; set; } = 0.5f;
    public float CenterOfRotationZ { get; set; } = 0.0f;

    public PlaneProjection() {}
    public PlaneProjection(float rx, float ry, float rz) {
        RotationX = rx; RotationY = ry; RotationZ = rz;
    }
}

public class CompositeTransform3D : DependencyObject {
    private float _centerX = 0.0f;
    private float _centerY = 0.0f;
    private float _centerZ = 0.0f;
    public bool HasExplicitCenter { get; set; } = false;
    public float RotationX { get; set; } = 0.0f;
    public float RotationY { get; set; } = 0.0f;
    public float RotationZ { get; set; } = 0.0f;
    public float TranslateX { get; set; } = 0.0f;
    public float TranslateY { get; set; } = 0.0f;
    public float TranslateZ { get; set; } = 0.0f;
    public float ScaleX { get; set; } = 1.0f;
    public float ScaleY { get; set; } = 1.0f;
    public float ScaleZ { get; set; } = 1.0f;
    public float CenterX { get { return _centerX; } set { _centerX = value; HasExplicitCenter = true; } }
    public float CenterY { get { return _centerY; } set { _centerY = value; HasExplicitCenter = true; } }
    public float CenterZ { get { return _centerZ; } set { _centerZ = value; HasExplicitCenter = true; } }

    public CompositeTransform3D() {}
}

public class KeyBinding : DependencyObject {
    public KeyCode Key { get; set; } = KeyCode.None;
    public int Modifiers { get; set; } = 0;
    public ICommand Command { get; set; }
    public string CommandPath { get; set; } = "";
    public string CommandParameterPath { get; set; } = "";
    private DependencyObject _paramBox;

    public KeyBinding() {}
    public KeyBinding(ICommand command, KeyCode key, int modifiers = 0) {
        Command = command;
        Key = key;
        Modifiers = modifiers;
    }

    public object CommandParameter {
        get { return DependencyObject.Unbox(_paramBox); }
        set { _paramBox = DependencyObject.BoxObject(value); }
    }
    public void SetCommandParameter(string val) { _paramBox = DependencyObject.BoxString(val); }
    public void SetCommandParameterBox(DependencyObject box) { _paramBox = box; }
    public DependencyObject GetCommandParameterBox() { return _paramBox; }
}

// ============================================================================
// 6. UIElement & FrameworkElement
// ============================================================================
