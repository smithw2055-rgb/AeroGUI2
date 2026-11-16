module Aero.Native;

// Tree & lifecycle
public handle Visual_Create(int kind) { return Visual_Create(kind); }
public void Visual_Destroy(handle node) { }
public void Visual_AddChild(handle parent, handle child) { }
public void Visual_InsertChild(handle parent, int index, handle child) { }
public void Visual_RemoveChild(handle parent, handle child) { }
public void Visual_ClearChildren(handle parent) { }
public void Visual_BindOwner(handle node, object owner, int callbackMask) { }
public object Visual_GetOwner(handle node) { return null; }

// Geometry & Layout
public void Visual_SetSize(handle node, float w, float h) { }
public void Visual_SetMinMaxSize(handle node, float minW, float minH, float maxW, float maxH) { }
public void Visual_SetMargin(handle node, float l, float t, float r, float b) { }
public void Visual_SetPadding(handle node, float l, float t, float r, float b) { }
public void Visual_SetAlignment(handle node, int hAlign, int vAlign) { }
public void Visual_SetVisibility(handle node, int vis) { }
public void Visual_SetOpacity(handle node, float op) { }
public void Visual_SetZIndex(handle node, int z) { }
public void Visual_SetClipToBounds(handle node, bool clip) { }
public void Visual_SetInteractiveFlags(handle node, bool enabled, bool hitTest, bool focusable) { }
public void Visual_SetLayoutMode(handle node, int mode, float p0, float p1, int i0, int i1) { }
public void Visual_GridClearDefs(handle node) { }
public void Visual_GridAddRowDef(handle node, int unit, float val) { }
public void Visual_GridAddColDef(handle node, int unit, float val) { }
public void Visual_SetGridCell(handle node, int row, int col, int rowSpan, int colSpan) { }
public void Visual_SetDock(handle node, int dock) { }
public void Visual_SetCanvasOffsets(handle node, float l, float t, float r, float b) { }
public void Visual_SetScrollOffsets(handle node, float ox, float oy) { }
public float Visual_GetLayoutRect(handle node, int which) { return 0.0f; }
public float Visual_ScreenToLocal(handle node, float sx, float sy, int axis) { return 0.0f; }

// Appearance, Transforms & Effects
public void Visual_SetBoxStyle(handle node, uint bgRgba, uint borderRgba, float btL, float btT, float btR, float btB, float crTL, float crTR, float crBR, float crBL) { }
public void Visual_SetBoxGradients(handle node, int bgGradId, int borderGradId) { }
public int Visual_RegisterLinearGradient(float x0, float y0, float x1, float y1, int spread, int mapping, string stopsSpec) { return 0; }
public int Visual_RegisterRadialGradient(float cx, float cy, float ox, float oy, float rx, float ry, int spread, int mapping, string stopsSpec) { return 0; }
public void Visual_SetTransform2D(handle node, float tx, float ty, float sx, float sy, float rotDeg, float skewX, float skewY, float ox, float oy) { }
public void Visual_SetTransform3D(handle node, float rx, float ry, float rz, float tx, float ty, float tz, float camDist) { }
public void Visual_SetEffect(handle node, int kind, float radius, float depth, float dirDeg, uint colorRgba, float opacity) { }

// Text, Image & Vector
public void Visual_SetText(handle node, string text, string fontFamily, float fontSize, uint fgRgba, int wrap, int align, int trim) { }
public void Visual_SetTextMetrics(handle node, float lineHeight, float charSpacing, int maxLines, int decorations) { }
public void Visual_SetTextSelection(handle node, int selStartCp, int selLenCp, int caretCp, bool showCaret, uint selRgba, uint caretRgba) { }
public float Visual_MeasureText(string text, string fontFamily, float fontSize, float maxW, int wrap, int outAxis) { return 0.0f; }
public int Visual_HitTestTextCp(handle node, float localX, float localY) { return 0; }
public void Visual_SetImage(handle node, string uri, int stretch, int stretchDir, uint tintRgba) { }
public void Visual_SetVectorShape(handle node, int shapeKind, string pathOrParams, uint fillRgba, int fillGradId, uint strokeRgba, int strokeGradId, float strokeWidth, int stretch, float trimStart, float trimEnd) { }

// Retained DrawingContext
public void Draw_Begin(handle node) { }
public void Draw_End(handle node) { }
public void Draw_FillRect(handle node, float x, float y, float w, float h, uint rgba, int gradId) { }
public void Draw_StrokeRect(handle node, float x, float y, float w, float h, uint rgba, float strokeW) { }
public void Draw_FillRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, int gradId) { }
public void Draw_StrokeRoundedRect(handle node, float x, float y, float w, float h, float rx, float ry, uint rgba, float strokeW) { }
public void Draw_FillEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, int gradId) { }
public void Draw_StrokeEllipse(handle node, float cx, float cy, float rx, float ry, uint rgba, float strokeW) { }
public void Draw_Line(handle node, float x1, float y1, float x2, float y2, uint rgba, float strokeW) { }
public void Draw_Path(handle node, string svgPath, float x, float y, float w, float h, uint fillRgba, uint strokeRgba, float strokeW) { }
public void Draw_Text(handle node, string text, float x, float y, float w, float h, string font, float size, uint rgba, int hAlign, int vAlign) { }
public void Draw_PushClip(handle node, float x, float y, float w, float h) { }
public void Draw_PopClip(handle node) { }
public void Draw_PushTransform(handle node, float tx, float ty, float sx, float sy, float rotDeg) { }
public void Draw_PopTransform(handle node) { }
public void Visual_InvalidateDraw(handle node) { }

// Animation, Host & Reflection
public int Anim_StartFloat(handle node, int prop, float fromVal, float toVal, float durationSec, int easeKind, int easeMode, float delaySec, bool autoReverse, int repeatCount) { return 0; }
public int Anim_StartColor(handle node, int prop, uint fromRgba, uint toRgba, float durationSec, int easeKind, int easeMode) { return 0; }
public int Anim_SpringFloat(handle node, int prop, float targetVal, float stiffness, float damping) { return 0; }
public void Anim_StopAll(handle node) { }
public void Host_SetPointerCapture(handle node) { }
public void Host_ReleasePointerCapture(handle node) { }
public void Host_SyncImeRect(float x, float y, float w, float h, bool active) { }
public string Host_GetClipboardText() { return ""; }
public void Host_SetClipboardText(string text) { }
public void Host_RequestWake() { }
public void Host_CloseWindow() { }

public string Reflect_GetPropString(object target, string propPath) { return ""; }
public float Reflect_GetPropFloat(object target, string propPath) { return 0.0f; }
public int Reflect_GetPropInt(object target, string propPath) { return 0; }
public bool Reflect_GetPropBool(object target, string propPath) { return false; }
public object Reflect_GetPropObject(object target, string propPath) { return null; }
public bool Reflect_SetPropString(object target, string propPath, string val) { return false; }
public bool Reflect_SetPropFloat(object target, string propPath, float val) { return false; }
public bool Reflect_SetPropInt(object target, string propPath, int val) { return false; }
public bool Reflect_SetPropBool(object target, string propPath, bool val) { return false; }
public bool Reflect_SetPropObject(object target, string propPath, object val) { return false; }
public bool Reflect_InvokeCommand(object target, string propPath, string param) { return false; }
public int Reflect_GetDirtyMask(object target) { return 0; }
public uint Color_ParseRgba(string hexOrNamed) { return 0u; }
